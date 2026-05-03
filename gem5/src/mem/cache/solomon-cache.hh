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

#ifndef __MEM_CACHE_SOLOMON_CACHE_HH__
#define __MEM_CACHE_SOLOMON_CACHE_HH__

#include <cstdint>
#include <unordered_map>
#include <vector>

extern "C" {
#include "libcorrect/include/correct.h"
}
#include "base/statistics.hh"
#include "mem/cache/cache.hh"
#include "mem/packet.hh"
#include "params/SolomonCache.hh"
#include "sim/eventq.hh"

namespace gem5 {

class CacheBlk;

class SolomonCache : public Cache {
public:
  SolomonCache(const SolomonCacheParams &p);
  ~SolomonCache();

  void updateBlockData(CacheBlk *blk, const PacketPtr cpkt,
                       bool has_old_data) override;

  size_t num_parity_symbols; // 2 * t where t = symbol errors
  size_t total_msg_size;     // blkSize + num_parity_symbols
  int refresh_count;
  enum class ECCResult { Clean, Corrected, Unrecoverable };

  bool operationReadsData(PacketPtr pkt) const;
  bool operationModifiesData(PacketPtr pkt) const;
  ECCResult checkAndCorrectECC(CacheBlk *blk);
  void recomputeAndStoreECC(CacheBlk *blk);

  void satisfyRequest(PacketPtr pkt, CacheBlk *blk,
                      bool deferred_response = false,
                      bool pending_downgrade = false) override;
  void invalidateBlock(CacheBlk *blk) override;

  // for scrubbing novel approach
  Cycles scrubIntervalCycles;
  Cycles cyclesPerBlockCheck;
  EventFunctionWrapper scrubEvent;
  Tick correctionGraceTicks;
  std::unordered_map<CacheBlk*, std::vector<uint8_t>> copies;

  void scrubCache();

  struct SolomonCacheStats : public statistics::Group
  {
    SolomonCacheStats(statistics::Group *parent);
    statistics::Scalar numScrubPasses;
    statistics::Scalar numScrubBlocksChecked;
    statistics::Scalar numScrubClean;
    statistics::Scalar numScrubCorrected;
    statistics::Scalar numScrubUnrecoverable;
    statistics::Scalar totalScrubCycles;
    statistics::Scalar numAccessCorrected;
    statistics::Scalar numAccessUnrecoverable;
    statistics::Scalar numUnrecoverableDirty;
  };
  SolomonCacheStats solomonStats;

private:
  // NOTE: It might be a problem to encode all blocks based on the same codec,
  // but I think this is better than block-specific codecs to avoid
  // regeneration
  correct_reed_solomon *rs_codec;
  // bc. of how libcorrect works it's nicer to keep encoding/decoding
  // buffers with the cache.
  std::vector<uint8_t> enc_dec_buf;
  // Despite RS codes co-locating the parity bits and data, I felt a need
  // to use the map approach because it allows us to preserve a full
  // block size, and the actual 'reconstruction' of the parity bits and
  // message can be done in the enc_dec_buf.
  std::unordered_map<CacheBlk *, std::vector<uint8_t>> blockParityMap;
};

} // namespace gem5

#endif // __MEM_CACHE_SOLOMON_CACHE_HH__
