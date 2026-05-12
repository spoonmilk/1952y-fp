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
  currentScrubIntervalCycles(Cycles(10)), // idea: start at minimum and adapt based on observed error rates
  minScrubIntervalCycles(Cycles(10)),
  scrubTightenFactor(p.scrub_tighten_factor),
  scrubRelaxFactor(p.scrub_relax_factor),
  cyclesPerBlockCheck(p.cycles_per_block_check),
  correctionGraceTicks(p.correction_grace_ticks),
  scrubEvent([this] { this->scrubCache(); }, name() + ".scrubEvent"),
  hammingStats(this)
{

  if (scrubTightenFactor == 1.0 && scrubRelaxFactor == 1.0) {
    currentScrubIntervalCycles = scrubIntervalCycles; // no adaptation, just use the specified interval
    std::cerr << "Scrubbing enabled with fixed interval of " << scrubIntervalCycles << " cycles\n";
  }
  else{
    std::cerr << "Scrubbing enabled with adaptive interval starting at " << currentScrubIntervalCycles
              << " cycles, tightening factor " << scrubTightenFactor
              << ", relax factor " << scrubRelaxFactor << "\n";
  }
  // Schedule the first scrub event if scrubbing is enabled
  if (scrubIntervalCycles > 0) {
      schedule(scrubEvent, clockEdge(currentScrubIntervalCycles));
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

  // Actually perform the data update if a packet payload is provided.
  // For some paths (e.g., functional writes) data may have already been
  // mutated outside this method and cpkt will be null; in that case we still
  // refresh ECC/copies below.
  if (cpkt) {
    cpkt->writeDataToBlock(blk->data, blkSize);
  }

  recomputeAndStoreECC(blk);

  if (ppDataUpdate->hasListeners()) {
    data_update.newData = std::vector<uint64_t>(
        blk->data, blk->data + (blkSize / sizeof(uint64_t)));
    data_update.hwPrefetched = blk->wasPrefetched();
    ppDataUpdate->notify(data_update);
  }
}

void HammingCache::functionalAccess(PacketPtr pkt, bool from_cpu_side)
{
  CacheBlk *blk_before = nullptr;
  std::vector<uint8_t> before;

  // snapshot block bytes so we can detect in-place functional writes
  if (pkt->isWrite()) {
    blk_before = tags->findBlock(pkt->getAddr(), pkt->isSecure());
    if (blk_before && blk_before->isValid() && blk_before != tempBlock) {
      before = std::vector<uint8_t>(blk_before->data, blk_before->data + blkSize);
    } else {
      blk_before = nullptr;
    }
  }

  BaseCache::functionalAccess(pkt, from_cpu_side);

  if (!blk_before) {
    return;
  }

  CacheBlk *blk_after = tags->findBlock(pkt->getAddr(), pkt->isSecure());
  if (!blk_after || !blk_after->isValid() || blk_after != blk_before) {
    return;
  }

  if (memcmp(before.data(), blk_after->data, blkSize) != 0) {
    const bool fits_single_block =
        pkt->getOffset(blkSize) + pkt->getSize() <= blkSize;

    if (fits_single_block) {
      updateBlockData(blk_after, pkt, true);
    } else {
      updateBlockData(blk_after, nullptr, true);
    }
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
  copies[blk] = std::vector<uint8_t>(blk->data, blk->data + blkSize);
}

HammingCache::ECCResult HammingCache::checkAndCorrectECC(CacheBlk *blk) {
  // stat updates live in the callers (satisfyRequest / scrubCache)
  // so scrub vs. access counters stay coherent

  if (blk == tempBlock || curTick() < correctionGraceTicks) {
    return {ECCStatus::Clean, CorrectionKind::None, false};
  }

  auto it = blockECCBits.find(blk);
  if (it == blockECCBits.end()) {
    return {ECCStatus::Clean, CorrectionKind::None, false};
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
    return {ECCStatus::Clean, CorrectionKind::None, false};
  }

  if (syndrome != 0 && overall_mismatch) {
    // single-bit error in data or parity bit
    CorrectionKind kind = CorrectionKind::None;

    auto loc_it = syndromeToBitLocation.find(syndrome);
    if (loc_it != syndromeToBitLocation.end()) {
      // error in a data bit — flip it back
      size_t data_bit = loc_it->second;
      size_t byte_idx = data_bit / 8;
      size_t bit_idx = data_bit % 8;
      blk->data[byte_idx] ^= (1 << bit_idx);
      kind = CorrectionKind::DataBitFlip;
    } else {
      // error in a parity bit, data is fine, refresh stored ECC
      blockECCBits[blk] = std::move(current);
      kind = CorrectionKind::ParityRefresh;
    }

    // verify against the copy of the block data we stored at last update.
    // 'verified' is only meaningful for Corrected results. If no copy is
    // available we treat it as verified (same optimistic behavior as before).
    bool verified = true;
    auto copy_it = copies.find(blk);
    if (copy_it != copies.end()) {
      const std::vector<uint8_t> &copy = copy_it->second;
      for (size_t i = 0; i < blkSize; i++) {
        if (blk->data[i] != copy[i]) {
          std::cerr << "Verification after correction: mismatch at byte "
                    << i << " for block " << blk << "\n";
          verified = false;
          break;
        }
      }
      if (!verified) {
        exitSimLoop("Verification failure after ECC correction", 1);
      }
    }

    return {ECCStatus::Corrected, kind, verified};
  }

  // two-bit error or overall-only mismatch, unrecoverable
  return {ECCStatus::Unrecoverable, CorrectionKind::None, false};
}

void HammingCache::tightenScrubInterval() {
  Cycles newInterval = Cycles((uint64_t)(currentScrubIntervalCycles / scrubTightenFactor));
  if (newInterval < minScrubIntervalCycles) {
    newInterval = minScrubIntervalCycles;
  }
  if (newInterval < currentScrubIntervalCycles) {
    std::cerr << "Tightening scrub interval from " << currentScrubIntervalCycles << " cycles to " << newInterval << " cycles\n";
    currentScrubIntervalCycles = newInterval;
    // if the next scrub is already scheduled and the new time is sooner, reschedule it earlier
    if (scrubEvent.scheduled()) {
      Tick newTick = clockEdge(currentScrubIntervalCycles);
      if (newTick < scrubEvent.when()) {
        reschedule(scrubEvent, newTick);
      }
    }
  }
}

void HammingCache::relaxScrubInterval() {
  Cycles newInterval = Cycles((uint64_t)(currentScrubIntervalCycles * scrubRelaxFactor));
  if (newInterval > scrubIntervalCycles) {
    newInterval = scrubIntervalCycles;
  }
  if (newInterval > currentScrubIntervalCycles) {
    std::cerr << "Relaxing scrub interval from " << currentScrubIntervalCycles << " cycles to " << newInterval << " cycles\n";
  }
  currentScrubIntervalCycles = newInterval;
}

void HammingCache::invalidateBlock(CacheBlk *blk) {
  blockECCBits.erase(blk);
  copies.erase(blk);
  BaseCache::invalidateBlock(blk);
}

void HammingCache::satisfyRequest(PacketPtr pkt, CacheBlk *blk,
                                  bool deferred_response,
                                  bool pending_downgrade) {
  // run ECC check before any data leaves the block, but only if this
  // operation will actually read data
  if (blk && blk->isValid() && operationReadsData(pkt)) {
    ECCResult result = checkAndCorrectECC(blk);
    hammingStats.numAccessAttemptedCorrections++; // count the block checked
    static int refresh_count = 0;

    if (result.status == ECCStatus::Corrected) {
      // attempted: we entered the correction branch
      // successful: passed verification (or no copy available — see callee)
      if (result.verified) {
        hammingStats.numAccessCorrected++;
        hammingStats.totalSuccessfulCorrections++;
        if (result.kind == CorrectionKind::ParityRefresh) {
          hammingStats.numAccessParityRefreshed++;
        }
      }
      tightenScrubInterval();
    } else if (result.status == ECCStatus::Unrecoverable) {
      hammingStats.numAccessUnrecoverable++;
      tightenScrubInterval();

      if (blk->isSet(CacheBlk::DirtyBit)) {
        hammingStats.numUnrecoverableDirty++;
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
        hammingStats.totalSuccessfulCorrections++; // count this as a successful correction since we end up with correct data in the block, even though we couldn't correct it in-place 
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
      // scrub-pass bookkeeping
      ADD_STAT(numScrubPasses, statistics::units::Count::get(),
               "Total number of full scrub passes performed"),
      ADD_STAT(numScrubBlocksChecked, statistics::units::Count::get(),
               "Total number of valid blocks checked across all scrubs"),
      ADD_STAT(numScrubClean, statistics::units::Count::get(),
               "Blocks found clean during scrub"),
      ADD_STAT(numScrubAttemptedCorrections, statistics::units::Count::get(),
               "Corrections attempted during scrub (entered correction branch, "
               "before verification)"),
      ADD_STAT(numScrubCorrected, statistics::units::Count::get(),
               "Single-bit errors corrected during scrub that passed verification"),
      ADD_STAT(numScrubParityRefreshed, statistics::units::Count::get(),
               "Scrub corrections handled via parity-bit refresh (subset of "
               "numScrubCorrected)"),
      ADD_STAT(numScrubUnrecoverable, statistics::units::Count::get(),
               "Multi-bit errors detected (uncorrectable) during scrub"),
      ADD_STAT(totalScrubCycles, statistics::units::Cycle::get(),
               "Total simulated cycles attributable to scrubbing"),

      // on-access bookkeeping
      ADD_STAT(numAccessAttemptedCorrections, statistics::units::Count::get(),
               "Corrections attempted on access (entered correction branch, "
               "before verification)"),
      ADD_STAT(numAccessCorrected, statistics::units::Count::get(),
               "Single-bit errors corrected on access that passed verification"),
      ADD_STAT(numAccessParityRefreshed, statistics::units::Count::get(),
               "Access corrections handled via parity-bit refresh (subset of "
               "numAccessCorrected)"),
      ADD_STAT(numAccessUnrecoverable, statistics::units::Count::get(),
               "Multi-bit errors detected on access"),

      // shared / aggregate
      ADD_STAT(numUnrecoverableDirty, statistics::units::Count::get(),
               "Unrecoverable errors in dirty blocks (data loss, refetch skipped)"),
      ADD_STAT(totalSuccessfulCorrections, statistics::units::Count::get(),
               "Total successful corrections regardless of source (scrub or "
               "access) or kind (data-bit flip or parity refresh); equals "
               "numScrubCorrected + numAccessCorrected")
{
}

void
HammingCache::scrubCache()
{
    unsigned blocks_checked = 0;

    bool faultFoundThisScrub = false;

    tags->forEachBlk([this, &blocks_checked, &faultFoundThisScrub](CacheBlk &blk) {
        if (!blk.isValid()) {
            return;
        }
        blocks_checked++;
        ECCResult result = checkAndCorrectECC(&blk);
        hammingStats.numScrubAttemptedCorrections++; // count the block that's checked
        switch (result.status) {
            case ECCStatus::Clean:
                hammingStats.numScrubClean++;
                break;
            case ECCStatus::Corrected:
                if (result.verified) {
                    hammingStats.numScrubCorrected++;
                    hammingStats.totalSuccessfulCorrections++;
                    if (result.kind == CorrectionKind::ParityRefresh) {
                        hammingStats.numScrubParityRefreshed++;
                    }
                }
                faultFoundThisScrub = true;
                break;
            case ECCStatus::Unrecoverable:
                faultFoundThisScrub = true;
                hammingStats.numScrubUnrecoverable++;
                // for consistency with on-access path, refetch from memory
                {
                    if (blk.isSet(CacheBlk::DirtyBit)) {
                      hammingStats.numUnrecoverableDirty++;
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
                      hammingStats.totalSuccessfulCorrections++; // count this as a successful correction since we end up with correct data in the block, even though we couldn't correct it in-place
                      hammingStats.numScrubCorrected++;
                    }
                }
                break;
        }
    });

    hammingStats.numScrubPasses++;
    hammingStats.numScrubBlocksChecked += blocks_checked;
    hammingStats.totalScrubCycles += blocks_checked * cyclesPerBlockCheck;

    // adapt scrub interval based on whether any faults were found
    if (faultFoundThisScrub) {
        tightenScrubInterval();
    } else {
        relaxScrubInterval();
    }

    // reschedule next scrub
    if (scrubIntervalCycles > 0) {
        schedule(scrubEvent, clockEdge(currentScrubIntervalCycles));
    }
}

} // namespace gem5