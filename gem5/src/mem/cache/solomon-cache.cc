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

#include "mem/cache/solomon-cache.hh"

#include <iostream>

#include "base/logging.hh"
#include "mem/cache/cache_blk.hh"
#include "mem/request.hh"
#include "params/SolomonCache.hh"
#include "sim/sim_exit.hh"

namespace gem5 {

SolomonCache::SolomonCache(const SolomonCacheParams &p)
    : Cache(p), num_parity_symbols(2 * p.symbol_errors), refresh_count(0),
      rs_codec(nullptr), scrubIntervalCycles(p.scrub_interval_cycles),
      currentScrubIntervalCycles(Cycles(10)), // idea: start at minimum and adapt based on observed error rates
      minScrubIntervalCycles(Cycles(10)),
      scrubTightenFactor(p.scrub_tighten_factor),
      scrubRelaxFactor(p.scrub_relax_factor),
      cyclesPerBlockCheck(p.cycles_per_block_check),
      correctionGraceTicks(p.correction_grace_ticks),
      scrubEvent([this] { this->scrubCache(); }, name() + ".scrubEvent"),
      solomonStats(this) {
  if (blkSize > 255) {
    panic("SolomonCache: blkSize %d exceeds 255-byte GF(2^8) limit", blkSize);
  }
  if (p.symbol_errors < 1) {
    panic("SolomonCache: symbol_errors must be >= 1");
  }
  if (blkSize + num_parity_symbols > 255) {
    panic("SolomonCache: blkSize %d + 2*symbol_errors %d = %d exceeds "
          "255-symbol GF(2^8) codeword limit",
          blkSize, num_parity_symbols, blkSize + num_parity_symbols);
  }
  if (scrubTightenFactor == 1.0 && scrubRelaxFactor == 1.0) {
  currentScrubIntervalCycles = scrubIntervalCycles; // no adaptation, just use the specified interval
  std::cerr << "Scrubbing enabled with fixed interval of " << scrubIntervalCycles << " cycles\n";

  }
  else{
    std::cerr << "Scrubbing enabled with adaptive interval starting at " << currentScrubIntervalCycles
              << " cycles, tightening factor " << scrubTightenFactor
              << ", relax factor " << scrubRelaxFactor << "\n";
  }

  total_msg_size = blkSize + num_parity_symbols;
  enc_dec_buf.resize(total_msg_size);
  // TODO: Maybe worth allowing specification of root polynomials through python
  uint16_t rs_poly = correct_rs_primitive_polynomial_ccsds;
  // This is a semi-arbitrary choice, from my understanding.
  // See: https://berthub.eu/articles/posts/reed-solomon-for-programmers/
  uint8_t rs_first_const_root = 121;
  uint8_t rs_generator_root_gap = 1;
  rs_codec = correct_reed_solomon_create(
      rs_poly, rs_first_const_root, rs_generator_root_gap, num_parity_symbols);

  if (scrubIntervalCycles > 0) {
    schedule(scrubEvent, clockEdge(currentScrubIntervalCycles));  // ← uses currentScrub...
  }
}

SolomonCache::~SolomonCache() { correct_reed_solomon_destroy(rs_codec); }

void SolomonCache::updateBlockData(CacheBlk *blk, const PacketPtr cpkt,
                                   bool has_old_data) {
  if (blk == tempBlock) {
    Cache::updateBlockData(blk, cpkt, has_old_data);
    return;
  }

  Cache::updateBlockData(blk, cpkt, has_old_data);

  // keep bookkeeping in sync for all paths, including
  // functional writes that may call this with cpkt == nullptr
  copies[blk] = std::vector<uint8_t>(blk->data, blk->data + blkSize);
  recomputeAndStoreECC(blk);
}

void SolomonCache::functionalAccess(PacketPtr pkt, bool from_cpu_side)
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

    // route through updateBlockData workflow so ECC/copies stay consistent with normal timing writes
    if (fits_single_block) {
      updateBlockData(blk_after, pkt, true);
    } else {
      updateBlockData(blk_after, nullptr, true);
    }
  }
}

void SolomonCache::recomputeAndStoreECC(CacheBlk *blk) {
  memcpy(enc_dec_buf.data(), blk->data, blkSize);

  correct_reed_solomon_encode(rs_codec, enc_dec_buf.data(), blkSize,
                              enc_dec_buf.data());

  uint8_t *parity_bit_start = enc_dec_buf.data() + blkSize;
  const std::vector<uint8_t> parity_block = std::vector<uint8_t>(
      parity_bit_start, parity_bit_start + num_parity_symbols);
  blockParityMap[blk] = parity_block;

  copies[blk] = std::vector<uint8_t>(blk->data, blk->data + blkSize);
}

SolomonCache::ECCResult SolomonCache::checkAndCorrectECC(CacheBlk *blk) {

  if (blk == tempBlock || curTick() < correctionGraceTicks) {
    return {ECCStatus::Clean, false};
  }

  auto parity_it = blockParityMap.find(blk);
  if (parity_it == blockParityMap.end()) {
    // matches hamming for "magic blocks" added by gem5 internals
    return {ECCStatus::Clean, false};
  }

  memcpy(enc_dec_buf.data(), blk->data, blkSize);
  memcpy(enc_dec_buf.data() + blkSize, parity_it->second.data(),
         num_parity_symbols);

  ssize_t result = correct_reed_solomon_decode(
      rs_codec, enc_dec_buf.data(), total_msg_size, enc_dec_buf.data());
  // NOTE: There is a chance that RS can silently fail, but in that case we have
  // bigger problems.
  if (result < 0) {
    std::cerr << "Unrecoverable error in block at " << regenerateBlkAddr(blk) << "\n";
    return {ECCStatus::Unrecoverable, false};
  }

  if (memcmp(enc_dec_buf.data(), blk->data, blkSize) == 0) {
    // decoder ran but no symbols actually changed -> clean
    return {ECCStatus::Clean, false};
  }

  // decoder corrected one or more symbols, write back into the block
  memcpy(blk->data, enc_dec_buf.data(), blkSize);

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

  return {ECCStatus::Corrected, verified};
}

bool SolomonCache::operationReadsData(PacketPtr pkt) const {
  return pkt->isRead();
}

bool SolomonCache::operationModifiesData(PacketPtr pkt) const {
  return pkt->isWrite() || pkt->cmd == MemCmd::SwapReq;
}

void SolomonCache::tightenScrubInterval() {
  Cycles newInterval = Cycles((uint64_t)(currentScrubIntervalCycles / scrubTightenFactor));
  if (newInterval < minScrubIntervalCycles) {
    newInterval = minScrubIntervalCycles;
  }
  else {
    std::cerr << "Tightening scrub interval from " << currentScrubIntervalCycles << " cycles to " << newInterval << " cycles\n";
  }
  if (newInterval < currentScrubIntervalCycles) {
    currentScrubIntervalCycles = newInterval;
    // if the next scrub is already scheduled and the new time is sooner, reschedule it earlier.
    if (scrubEvent.scheduled()) {
      Tick newTick = clockEdge(currentScrubIntervalCycles);
      if (newTick < scrubEvent.when()) {
        reschedule(scrubEvent, newTick);
      }
    }
  }
}

void SolomonCache::relaxScrubInterval() {
  Cycles newInterval = Cycles((uint64_t)(currentScrubIntervalCycles * scrubRelaxFactor));
  if (newInterval > scrubIntervalCycles) {
    newInterval = scrubIntervalCycles;
  }
  else {
    std::cerr << "Relaxingxxx scrub interval from " << currentScrubIntervalCycles << " cycles to " << newInterval << " cycles\n";
  }
  currentScrubIntervalCycles = newInterval;
}

void SolomonCache::invalidateBlock(CacheBlk *blk) {
  blockParityMap.erase(blk);
  copies.erase(blk);
  BaseCache::invalidateBlock(blk);
}

void SolomonCache::satisfyRequest(PacketPtr pkt, CacheBlk *blk,
                                  bool deferred_response,
                                  bool pending_downgrade) {

  if (blk && blk->isValid() && operationReadsData(pkt)) {
    ECCResult result = checkAndCorrectECC(blk);
    if (result.status == ECCStatus::Corrected) {
      // attempted: decoder reported a correction
      solomonStats.numAccessAttemptedCorrections++;
      // successful: passed verification or no copy
      if (result.verified) {
        solomonStats.numAccessCorrected++;
        solomonStats.totalSuccessfulCorrections++;
      }
      tightenScrubInterval();
    } else if (result.status == ECCStatus::Unrecoverable) {
      solomonStats.numAccessUnrecoverable++;
      tightenScrubInterval();
      if (blk->isSet(CacheBlk::DirtyBit)) {
        solomonStats.numUnrecoverableDirty++;
        std::cerr << "Unrecoverable error in dirty block at 0x" << std::hex
                  << regenerateBlkAddr(blk) << std::dec << "\n";
        exitSimLoop("Unrecoverable error in dirty block", 1);
      } else {
        refresh_count++;
        std::cerr << "ECC refresh #" << refresh_count << " at tick "
                  << curTick() << "\n";
        Addr blk_addr = regenerateBlkAddr(blk);
        RequestPtr req = std::make_shared<Request>(blk_addr, blkSize, 0,
                                                   Request::funcRequestorId);
        if (blk->isSecure()) {
          req->setFlags(Request::SECURE);
        }

        Packet refresh_pkt(req, MemCmd::ReadReq);
        refresh_pkt.dataStatic(blk->data);

        memSidePort.sendFunctional(&refresh_pkt);
        recomputeAndStoreECC(blk);
      }
    }
  }
  Cache::satisfyRequest(pkt, blk, deferred_response, pending_downgrade);

  if (blk && blk->isValid() && operationModifiesData(pkt)) {
    recomputeAndStoreECC(blk);
  }
}

SolomonCache::SolomonCacheStats::SolomonCacheStats(statistics::Group *parent)
    : statistics::Group(parent, "solomon"),
      // scrub-pass bookkeeping
      ADD_STAT(numScrubPasses, statistics::units::Count::get(),
               "Total number of full scrub passes performed"),
      ADD_STAT(numScrubBlocksChecked, statistics::units::Count::get(),
               "Total number of valid blocks checked across all scrubs"),
      ADD_STAT(numScrubClean, statistics::units::Count::get(),
               "Blocks found clean during scrub"),
      ADD_STAT(numScrubAttemptedCorrections, statistics::units::Count::get(),
               "Corrections attempted during scrub (decoder reported a "
               "correction, before verification)"),
      ADD_STAT(numScrubCorrected, statistics::units::Count::get(),
               "Symbol errors corrected during scrub that passed verification"),
      ADD_STAT(numScrubUnrecoverable, statistics::units::Count::get(),
               "Multi-symbol errors detected (uncorrectable) during scrub"),
      ADD_STAT(totalScrubCycles, statistics::units::Cycle::get(),
               "Total simulated cycles attributable to scrubbing"),

      // on-access bookkeeping
      ADD_STAT(numAccessAttemptedCorrections, statistics::units::Count::get(),
               "Corrections attempted on access (decoder reported a "
               "correction, before verification)"),
      ADD_STAT(numAccessCorrected, statistics::units::Count::get(),
               "Errors corrected on access that passed verification"),
      ADD_STAT(numAccessUnrecoverable, statistics::units::Count::get(),
               "Unrecoverable errors detected on access"),

      // shared / aggregate 
      ADD_STAT(numUnrecoverableDirty, statistics::units::Count::get(),
               "Unrecoverable errors in dirty blocks (data loss, refetch skipped)"),
      ADD_STAT(totalSuccessfulCorrections, statistics::units::Count::get(),
               "Total successful corrections regardless of source (scrub or "
               "access); equals numScrubCorrected + numAccessCorrected")
{
}

void SolomonCache::scrubCache() {
  unsigned blocks_checked = 0;

  bool faultFoundThisScrub = false;

  tags->forEachBlk([this, &blocks_checked, &faultFoundThisScrub](CacheBlk &blk) {
    if (!blk.isValid()) {
      return;
    }
    blocks_checked++;
    ECCResult result = checkAndCorrectECC(&blk);
    switch (result.status) {
    case ECCStatus::Clean:
      solomonStats.numScrubClean++;
      break;
    case ECCStatus::Corrected:
      solomonStats.numScrubAttemptedCorrections++;
      if (result.verified) {
        solomonStats.numScrubCorrected++;
        solomonStats.totalSuccessfulCorrections++;
      }
      faultFoundThisScrub = true;
      break;
    case ECCStatus::Unrecoverable:
      faultFoundThisScrub = true;
      solomonStats.numScrubUnrecoverable++;
      if (blk.isSet(CacheBlk::DirtyBit)) {
        solomonStats.numUnrecoverableDirty++;
        std::cerr << "Unrecoverable error in dirty block at 0x" << std::hex
                  << regenerateBlkAddr(&blk) << std::dec << "\n";
        exitSimLoop("Unrecoverable error in dirty block during scrub", 1);
      } else {
        Addr blk_addr = regenerateBlkAddr(&blk);
        RequestPtr req = std::make_shared<Request>(blk_addr, blkSize, 0,
                                                   Request::funcRequestorId);
        if (blk.isSecure()) {
          req->setFlags(Request::SECURE);
        }
        Packet refresh_pkt(req, MemCmd::ReadReq);
        refresh_pkt.dataStatic(blk.data);
        memSidePort.sendFunctional(&refresh_pkt);
        recomputeAndStoreECC(&blk);
      }
      break;
    }
  });

  solomonStats.numScrubPasses++;
  solomonStats.numScrubBlocksChecked += blocks_checked;
  solomonStats.totalScrubCycles += blocks_checked * cyclesPerBlockCheck;

  // adapt scrub interval based on whether any faults were found
  if (faultFoundThisScrub) {
      tightenScrubInterval();
  } else {
      relaxScrubInterval();
  }

  if (scrubIntervalCycles > 0) {
    schedule(scrubEvent, clockEdge(currentScrubIntervalCycles));
  }
}

} // namespace gem5