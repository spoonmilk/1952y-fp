/*
 * Copyright (c) 2010-2019 ARM Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/cache/hamming-cache.hh"

#include <iostream>

#include "mem/cache/cache_blk.hh"
#include "mem/request.hh"
#include "params/Cache.hh"
#include "sim/sim_exit.hh"

namespace gem5 {

HammingCache::HammingCache(const HammingCacheParams &p) : Cache(p),
  scrubIntervalCycles(p.scrub_interval_cycles),
  cyclesPerBlockCheck(p.cycles_per_block_check),
  correctionGraceTicks(p.correction_grace_ticks),
  scrubEvent([this] { this->scrubCache(); }, name() + ".scrubEvent"),
  hammingStats(this)
{
  // Schedule the first scrub event if scrubbing is enabled
  if (scrubIntervalCycles > 0) {
      schedule(scrubEvent, clockEdge(scrubIntervalCycles));
  }

  size_t total_data_bits = blkSize * 8;
  num_parity_bits = 0;
  while ((1ULL << num_parity_bits) < (total_data_bits + num_parity_bits + 1)) {
    num_parity_bits++;
  }

  // wrote this out bc inserting the parity bits would involve a lot of
  // non-trivial changes positions that are powers of 2 are parity bits;
  // everything else is a data bit we need enough codeword positions to cover
  // all data bits, accounting for the parity bits that get interleaved among
  // them
  size_t data_index = 0;
  size_t pos = 1;
  while (data_index < total_data_bits) {
    if ((pos & (pos - 1)) != 0) {
      // data bit position
      syndromeToBitLocation[pos] = data_index;
      bitLocationToSyndrome[data_index] = pos;
      data_index++;
    }
    pos++;
  }

  // once we reach here, there should actually be 
}

void HammingCache::updateBlockData(CacheBlk *blk, const PacketPtr cpkt,
                                   bool has_old_data) {
  if (blk == tempBlock) {
    // ignore the tempblock, it causes issues and is not relevant to experiments
    Cache::updateBlockData(blk, cpkt, has_old_data);
    return;
  }

  CacheDataUpdateProbeArg data_update(regenerateBlkAddr(blk), blk->isSecure(),
                                      blk->getSrcRequestorId(), accessor);
  if (ppDataUpdate->hasListeners()) {
    if (has_old_data) {
      data_update.oldData = std::vector<uint64_t>(
          blk->data, blk->data + (blkSize / sizeof(uint64_t)));
    }
  }

  // actually perform the data update
  if (cpkt) {
    cpkt->writeDataToBlock(blk->data, blkSize);
    // after the write, the data is available in the block, so grab from there
    // idea here is to grab the materials needed to make the struct, then append
    // them directly into the unordered_map
    // 1. get overall parity of the block
    // 2. record the specific parity bits (using a mask?)
    // the data is in *data, but it is not an iterable. The blksize is the only
    // thing delimiting the traversal
    copies[blk] = std::vector<uint8_t>(blk->data, blk->data + blkSize);

    size_t total_data_bits = blkSize * 8;
    HammingCode code;
    code.overallParityBit = 0;
    code.parityBits.assign(num_parity_bits, 0);

    // walk through every data bit, contributing to overall parity and to
    // each Hamming parity bit whose codeword position covers this data bit
    for (size_t data_index = 0; data_index < total_data_bits; data_index++) {
      size_t byte_idx = data_index / 8;
      size_t bit_idx = data_index % 8;
      uint8_t bit_value = (blk->data[byte_idx] >> bit_idx) & 1;

      if (bit_value) {
        // update overall parity
        code.overallParityBit ^= 1;

        // look up this data bit's codeword position
        size_t codeword_pos = bitLocationToSyndrome.at(data_index);

        // for each parity bit, check if its position (a power of 2)
        // shares a 1-bit with this data bit's codeword position.
        for (int p = 0; p < num_parity_bits; p++) {
          size_t parity_pos = (1ULL << p);
          if (codeword_pos &
              parity_pos) { // this parity bit covers this data bit
            code.parityBits[p] ^= 1;
          }
        }
      }
    }

    // store the computed ECC, overwriting any previous entry for this block
    blockECCBits[blk] = std::move(code);
  }

  if (ppDataUpdate->hasListeners()) {
    if (cpkt) {
      data_update.newData = std::vector<uint64_t>(
          blk->data, blk->data + (blkSize / sizeof(uint64_t)));
      data_update.hwPrefetched = blk->wasPrefetched();
    }
    ppDataUpdate->notify(data_update);
  }
}

bool HammingCache::operationReadsData(PacketPtr pkt) const {
  return pkt->isRead();
}

bool HammingCache::operationModifiesData(PacketPtr pkt) const {
  return pkt->isWrite() || pkt->cmd == MemCmd::SwapReq;
}

void HammingCache::recomputeAndStoreECC(CacheBlk *blk) {
  HammingCode code;
  code.overallParityBit = 0;
  code.parityBits.assign(num_parity_bits, 0);

  size_t total_data_bits = blkSize * 8;
  for (size_t data_index = 0; data_index < total_data_bits; data_index++) {
    size_t byte_idx = data_index / 8;
    size_t bit_idx = data_index % 8;
    uint8_t bit_value = (blk->data[byte_idx] >> bit_idx) & 1;

    if (bit_value) {
      code.overallParityBit ^= 1;
      size_t codeword_pos = bitLocationToSyndrome.at(data_index);
      for (int p = 0; p < num_parity_bits; p++) {
        if (codeword_pos & (1ULL << p)) {
          code.parityBits[p] ^= 1;
        }
      }
    }
  }

  blockECCBits[blk] = std::move(code);
}

HammingCache::ECCResult HammingCache::checkAndCorrectECC(CacheBlk *blk) {
  if (blk == tempBlock || curTick() < correctionGraceTicks) {
    return ECCResult::Clean;
  }

  auto it = blockECCBits.find(blk);
  if (it == blockECCBits.end()) {
    return ECCResult::Clean;
  }
  const HammingCode &stored = it->second;

  // Recompute parity over current block data
  HammingCode current;
  current.overallParityBit = 0;
  current.parityBits.assign(num_parity_bits, 0);

  size_t total_data_bits = blkSize * 8;
  for (size_t data_index = 0; data_index < total_data_bits; data_index++) {
    size_t byte_idx = data_index / 8;
    size_t bit_idx = data_index % 8;
    uint8_t bit_value = (blk->data[byte_idx] >> bit_idx) & 1;

    if (bit_value) {
      current.overallParityBit ^= 1;
      size_t codeword_pos = bitLocationToSyndrome.at(data_index);
      for (int p = 0; p < num_parity_bits; p++) {
        if (codeword_pos & (1ULL << p)) {
          current.parityBits[p] ^= 1;
        }
      }
    }
  }

  // compute syndrome
  size_t syndrome = 0;
  for (int p = 0; p < num_parity_bits; p++) {
    if (current.parityBits[p] ^ stored.parityBits[p]) {
      syndrome |= (1ULL << p);
    }
  }
  bool overall_mismatch =
      (current.overallParityBit ^ stored.overallParityBit) != 0;

  if (syndrome == 0 && !overall_mismatch) {
    return ECCResult::Clean;
  }

  if (syndrome != 0 && overall_mismatch) {

    // single-bit error in data or parity bit
    auto loc_it = syndromeToBitLocation.find(syndrome);
    if (loc_it != syndromeToBitLocation.end()) {
      // error in a data bit — flip it back
      size_t data_bit = loc_it->second;
      size_t byte_idx = data_bit / 8;
      size_t bit_idx = data_bit % 8;

      hammingStats.totalCorrected++;

      //std::cerr << "Correcting byte number " << byte_idx << "\n";

      // std::cerr << "correcting bit " << data_bit << " in block " << blk
      //           << " syndrome=" << syndrome
      //           << " stored.overall=" << (int)stored.overallParityBit
      //           << " current.overall=" << (int)current.overallParityBit
      //           << " stored.parity=[";
      // for (auto p : stored.parityBits)
      //   std::cerr << (int)p << ",";
      // std::cerr << "] current.parity=[";
      // for (auto p : current.parityBits)
      //   std::cerr << (int)p << ",";
      // std::cerr << "]\n";

      // std::cerr << "  Block data: ";
      // for (unsigned i = 0; i < blkSize; i++) {
      //   std::cerr << std::hex << (int)blk->data[i] << " ";
      // }
      // std::cerr << std::dec << "\n";

      blk->data[byte_idx] ^= (1 << bit_idx);
    } else {
      // error in a parity bit, data is fine, refresh stored ECC
      // TODO: add metric to catch refreshes due to parity bit errors 
      std::cerr << "correcting via refresh" << "\n";
      blockECCBits[blk] = std::move(current);
    }

    // check against the copy of the block we stored during the last update
    auto copy_it = copies.find(blk);
    if (copy_it != copies.end()) {
        const std::vector<uint8_t> &copy = copy_it->second;
        bool exit = false;
        for (size_t i = 0; i < blkSize; i++) {
            if (blk->data[i] != copy[i]) {
                std::cerr << "Verification after correction: mismatch found at byte " << i << " for block " << hammingStats.totalCorrected.value() << "\n";
                exit = true;
            }
        }
        if (exit) {
            exitSimLoop("Verification failure after ECC correction", 1);
        }
    }
    return ECCResult::Corrected;
  }

  // two-bit error or overall-only mismatch, unrecoverable
  return ECCResult::Unrecoverable;
}

void HammingCache::invalidateBlock(CacheBlk *blk) {
  blockECCBits.erase(blk);
  BaseCache::invalidateBlock(blk);
}

void HammingCache::satisfyRequest(PacketPtr pkt, CacheBlk *blk,
                                  bool deferred_response,
                                  bool pending_downgrade) {
  // run ECC check before any data leaves the block, but only if this
  // operation will actually read data
  if (blk && blk->isValid() && operationReadsData(pkt)) {
    ECCResult result = checkAndCorrectECC(blk);
    static int refresh_count = 0;

    if (result == ECCResult::Corrected) {
      hammingStats.numAccessCorrected++;
    } else if (result == ECCResult::Unrecoverable) {
      hammingStats.numAccessUnrecoverable++;

      if (blk->isSet(CacheBlk::DirtyBit)) {
        hammingStats.numUnrecoverableDirty++;  // add this stat
        std::cerr << "Unrecoverable error in dirty block at 0x"
                  << std::hex << regenerateBlkAddr(blk) << std::dec << "\n";

        exitSimLoop("Unrecoverable error in dirty block", 1);
      } else {
        refresh_count++;
        std::cerr << "ECC refresh #" << refresh_count << " at tick " << curTick()
                  << "\n";
        // pull fresh data from memory directly into the block.
        // build a request packet matching this block's address and use
        Addr blk_addr = regenerateBlkAddr(blk);
        RequestPtr req = std::make_shared<Request>(blk_addr, blkSize, 0,
                                                  Request::funcRequestorId);
        if (blk->isSecure()) {
          req->setFlags(Request::SECURE);
        }

        Packet refresh_pkt(req, MemCmd::ReadReq);
        refresh_pkt.dataStatic(blk->data); // write directly into block

        // functional access -> bypasses timing
        memSidePort.sendFunctional(&refresh_pkt);

        // recompute and store fresh ECC for the refreshed data
        recomputeAndStoreECC(blk);
      }
    }
    // for "clean" and "corrected", block data is now valid
    //  fall through to base satisfyRequest to deliver data.
  }

  Cache::satisfyRequest(pkt, blk, deferred_response, pending_downgrade);

  if (blk && blk->isValid() && operationModifiesData(pkt)) {
    recomputeAndStoreECC(blk);
  }
}


HammingCache::HammingCacheStats::HammingCacheStats(statistics::Group *parent)
    : statistics::Group(parent, "hamming"),
      ADD_STAT(numScrubPasses, statistics::units::Count::get(),
               "Total number of full scrub passes performed"),
      ADD_STAT(numScrubBlocksChecked, statistics::units::Count::get(),
               "Total number of valid blocks checked across all scrubs"),
      ADD_STAT(numScrubClean, statistics::units::Count::get(),
               "Blocks found clean during scrub"),
      ADD_STAT(numScrubCorrected, statistics::units::Count::get(),
               "Single-bit errors corrected during scrub (the value-add)"),
      ADD_STAT(numScrubUnrecoverable, statistics::units::Count::get(),
               "Multi-bit errors detected (uncorrectable) during scrub"),
      ADD_STAT(totalScrubCycles, statistics::units::Cycle::get(),
               "Total simulated cycles attributable to scrubbing"),
      ADD_STAT(numAccessCorrected, statistics::units::Count::get(),
               "Single-bit errors corrected on access"),
      ADD_STAT(numAccessUnrecoverable, statistics::units::Count::get(),
               "Multi-bit errors detected on access"),
      ADD_STAT(numUnrecoverableDirty, statistics::units::Count::get(),
               "Unrecoverable errors in dirty blocks (data loss, refetch skipped)"),
      ADD_STAT(totalCorrected, statistics::units::Count::get(),
               "Total single-bit errors corrected (scrub + access)")
{
}

void
HammingCache::scrubCache()
{
    unsigned blocks_checked = 0;

    tags->forEachBlk([this, &blocks_checked](CacheBlk &blk) {
        if (!blk.isValid()) {
            return;
        }
        blocks_checked++;
        ECCResult result = checkAndCorrectECC(&blk);
        switch (result) {
            case ECCResult::Clean:
                hammingStats.numScrubClean++;
                break;
            case ECCResult::Corrected:
                hammingStats.numScrubCorrected++;
                break;
            case ECCResult::Unrecoverable:
                hammingStats.numScrubUnrecoverable++;
                // for consistency with on-access path, refetch from memory
                {
                    if (blk.isSet(CacheBlk::DirtyBit)) {
                      hammingStats.numUnrecoverableDirty++;  // add this stat
                      std::cerr << "Unrecoverable error in dirty block at 0x"
                                << std::hex << regenerateBlkAddr(&blk) << std::dec << "\n";
                      exitSimLoop("Unrecoverable error in dirty block", 1);
                    } else {
                      Addr blk_addr = regenerateBlkAddr(&blk);
                      RequestPtr req = std::make_shared<Request>(
                          blk_addr, blkSize, 0, Request::funcRequestorId);
                      if (blk.isSecure()) {
                          req->setFlags(Request::SECURE);
                      }
                      Packet refresh_pkt(req, MemCmd::ReadReq);
                      refresh_pkt.dataStatic(blk.data);
                      memSidePort.sendFunctional(&refresh_pkt);
                      recomputeAndStoreECC(&blk);
                    }
                }
                break;
        }
    });

    hammingStats.numScrubPasses++;
    hammingStats.numScrubBlocksChecked += blocks_checked;
    hammingStats.totalScrubCycles += blocks_checked * cyclesPerBlockCheck;

    // reschedule next scrub
    if (scrubIntervalCycles > 0) {
        schedule(scrubEvent, clockEdge(scrubIntervalCycles));
    }
}

} // namespace gem5
