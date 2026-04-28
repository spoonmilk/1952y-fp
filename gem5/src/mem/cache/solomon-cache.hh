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

#include "libcorrect/include/correct.h"
#include "mem/cache/cache.hh"
#include "mem/packet.hh"

namespace gem5 {

class CacheBlk;
struct CacheParams;

class SolomonCache : public Cache {
public:
  // symbolErrors = Desired # of correctable symbol errors
  SolomonCache(const CacheParams &p, const int symbolErrors);
  ~SolomonCache();

  void updateBlockData(CacheBlk *blk, const PacketPtr cpkt,
                       bool has_old_data) override;

  size_t num_parity_symbols; // 2 * t where t = symbol errors
  size_t max_data_size;      // Cache block size - parity symbols

  struct RSParity {
    std::vector<uint8_t> parityBits;
  };

  std::unordered_map<CacheBlk *, RSParity> blockParityBytes;

  enum class ECCResult { Clean, Corrected, Unrecoverable };

  bool operationReadsData(PacketPtr pkt) const;
  bool operationModifiesData(PacketPtr pkt) const;
  ECCResult checkAndCorrectECC(CacheBlk *blk);
  void recomputeAndStoreECC(CacheBlk *blk);

  void satisfyRequest(PacketPtr pkt, CacheBlk *blk,
                      bool deferred_response = false,
                      bool pending_downgrade = false) override;
  void invalidateBlock(CacheBlk *blk) override;

private:
  // NOTE: It might be a problem to encode all blocks based on the same codec,
  // but I think this is better than block-specific codecs to avoid regeneration
  correct_reed_solomon *rs_codec;
  // bc. of how libcorrect works it's nicer to keep encoding/decoding
  // buffers with the cache.
  std::vector<uint8_t> encode_buf;
  std::vector<uint8_t> decode_buf;
};

} // namespace gem5

#endif // __MEM_CACHE_SOLOMON_CACHE_HH__
