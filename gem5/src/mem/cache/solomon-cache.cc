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

#include "base/logging.hh"
#include "libcorrect/include/correct.h"
#include "mem/cache/cache_blk.hh"
#include "mem/request.hh"
#include "params/Cache.hh"

namespace gem5 {

SolomonCache::SolomonCache(const CacheParams &p, const int symbolErrors)
    : Cache(p), num_parity_symbols(2 * symbolErrors), rs_codec(nullptr) {
  max_data_size = blkSize - num_parity_symbols;
  if (blkSize > 255) {
    panic("SolomonCache::SolomonCache; Cannot generate cache w/ blkSize > 255 "
          "over libcorrect GF(2^8)");
  }
  encode_buf.resize(blkSize);
  decode_buf.resize(blkSize);
  // TODO: Maybe worth allowing specification of root polynomials through python
  uint16_t rs_poly = correct_rs_primitive_polynomial_ccsds;
  // This is a semi-arbitrary choice, from my understanding.
  // See: https://berthub.eu/articles/posts/reed-solomon-for-programmers/
  uint8_t rs_first_const_root = 121;
  uint8_t rs_generator_root_gap = 1;
  rs_codec = correct_reed_solomon_create(
      rs_poly, rs_first_const_root, rs_generator_root_gap, num_parity_symbols);
}

SolomonCache::~SolomonCache() { correct_reed_solomon_destroy(rs_codec); }

void SolomonCache::updateBlockData(CacheBlk *blk, const PacketPtr cpkt,
                                   bool has_old_data) {
  if (blk == tempBlock) {
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
  Cache::updateBlockData(blk, cpkt, has_old_data);
}

void SolomonCache::recomputeAndStoreECC(CacheBlk *blk) {
  uint8_t *data = blk->data;

  correct_reed_solomon_encode(correct_reed_solomon * rs, const uint8_t *msg,
                              size_t msg_length, uint8_t *encoded) blk->data
}

SolomonCache::ECCResult SolomonCache::checkAndCorrectECC(CacheBlk *blk) {
  return ECCResult::Clean;
}

bool SolomonCache::operationReadsData(PacketPtr pkt) const {
  return pkt->isRead();
}

bool SolomonCache::operationModifiesData(PacketPtr pkt) const {
  return pkt->isWrite() || pkt->cmd == MemCmd::SwapReq;
}

void SolomonCache::invalidateBlock(CacheBlk *blk) {
  blockParityBytes.erase(blk);
  BaseCache::invalidateBlock(blk);
}

void SolomonCache::satisfyRequest(PacketPtr pkt, CacheBlk *blk,
                                  bool deferred_response,
                                  bool pending_downgrade) {
  Cache::satisfyRequest(pkt, blk, deferred_response, pending_downgrade);
}

} // namespace gem5
