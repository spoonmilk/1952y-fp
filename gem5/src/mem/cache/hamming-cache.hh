/*
 * Copyright (c) 2012-2018 ARM Limited
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

#ifndef __MEM_CACHE_HAMMING_CACHE_HH__
#define __MEM_CACHE_HAMMING_CACHE_HH__

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "base/statistics.hh"
#include "mem/cache/cache.hh"
#include "mem/packet.hh"
#include "sim/eventq.hh"
#include "params/HammingCache.hh"

namespace gem5 {

class CacheBlk;
struct CacheParams;
struct HammingCacheParams;

class HammingCache : public Cache {
public:
  HammingCache(const HammingCacheParams &p);

  void updateBlockData(CacheBlk *blk, const PacketPtr cpkt,
                       bool has_old_data) override;

  struct HammingCode {
    uint8_t overallParityBit;
    std::vector<uint8_t> parityBits;
  };

  int num_parity_bits;
  std::unordered_map<CacheBlk *, HammingCode> blockECCBits;
  std::unordered_map<size_t, size_t> syndromeToBitLocation;
  std::unordered_map<size_t, size_t> bitLocationToSyndrome;
  std::unordered_map<CacheBlk*, std::vector<uint8_t>> copies;

  // status: high-level outcome callers switch on
  // kind: subtype meaningful only when status is Corrected
  enum class ECCStatus { Clean, Corrected, Unrecoverable };
  enum class CorrectionKind { None, DataBitFlip, ParityRefresh };

  struct ECCResult {
    ECCStatus status;
    CorrectionKind kind;       // None unless status is Corrected
    bool verified;             // for Corrected: did the post-correction copy match?
                               // (always false when no copy was available
  };

  bool operationReadsData(PacketPtr pkt) const;
  bool operationModifiesData(PacketPtr pkt) const;
  ECCResult checkAndCorrectECC(CacheBlk *blk);
  void recomputeAndStoreECC(CacheBlk *blk);

  void satisfyRequest(PacketPtr pkt, CacheBlk *blk,
                      bool deferred_response = false,
                      bool pending_downgrade = false) override;
  void functionalAccess(PacketPtr pkt, bool from_cpu_side) override;
  void invalidateBlock(CacheBlk *blk) override;

  // for scrubbing novel approach
  const Cycles scrubIntervalCycles;   // max interval (from param)
  Cycles currentScrubIntervalCycles;  // adaptive current interval
  const Cycles minScrubIntervalCycles;
  float scrubTightenFactor;           // divide factor when faults found 
  float scrubRelaxFactor;             // multiply factor when clean 
  Cycles cyclesPerBlockCheck;
  EventFunctionWrapper scrubEvent;
  Tick correctionGraceTicks;

  void tightenScrubInterval();
  void relaxScrubInterval();
  void scrubCache();

  struct HammingCacheStats : public statistics::Group
  {
    HammingCacheStats(statistics::Group *parent);

    // scrub-pass bookkeeping
    statistics::Scalar numScrubPasses;
    statistics::Scalar numScrubBlocksChecked;
    statistics::Scalar numScrubClean;

    // attempts during scrub, entered the correction branch (data-bit flip or
    // parity refresh), regardless of whether verification later succeeded
    statistics::Scalar numScrubAttemptedCorrections;
    // scrub corrections that passed verification
    statistics::Scalar numScrubCorrected;
    // scrub corrections that hit the parity-refresh path specifically
    statistics::Scalar numScrubParityRefreshed;
    // multi-bit / unrecoverable detections during scrub
    statistics::Scalar numScrubUnrecoverable;
    statistics::Scalar totalScrubCycles;

    // on-access bookkeeping
    statistics::Scalar numAccessAttemptedCorrections;
    statistics::Scalar numAccessCorrected;
    statistics::Scalar numAccessParityRefreshed;
    statistics::Scalar numAccessUnrecoverable;

    // shared / aggregate 
    // unrecoverable error in a dirty block (data loss, can't refetch)
    statistics::Scalar numUnrecoverableDirty;
    // every correction that passed verification, regardless of source (scrub or access) or kind (data-bit flip or parity refresh)
    statistics::Scalar totalSuccessfulCorrections;
  };
  HammingCacheStats hammingStats;
};

} // namespace gem5

#endif // __MEM_CACHE_HAMMING_CACHE_HH__
