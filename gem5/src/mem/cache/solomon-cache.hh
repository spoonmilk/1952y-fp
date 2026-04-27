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

#include "mem/cache/cache.hh"
#include "mem/packet.hh"

namespace gem5 {

class CacheBlk;
struct CacheParams;

class SolomonCache : public Cache {
public:
  SolomonCache(const CacheParams &p);

  void updateBlockData(CacheBlk *blk, const PacketPtr cpkt,
                       bool has_old_data) override;

  struct RSCode {
    uint32_t symbol_size;
    uint64_t data_symbols;
    uint64_t parity_symbols;
    std::vector<uint8_t> parityBits;
  };

  int num_parity_bits;
  std::unordered_map<CacheBlk *, RSCode> blockECCBits;
  std::unordered_map<uint8_t, size_t> syndromeToBitLocation;
  std::unordered_map<size_t, uint8_t> bitLocationToSyndrome;

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
  uint32_t symbol_size;
  uint64_t data_symbols;
  uint64_t parity_symbols;
};

} // namespace gem5

#endif // __MEM_CACHE_SOLOMON_CACHE_HH__
