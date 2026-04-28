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
#include "mem/cache/cache_blk.hh"
#include "mem/request.hh"

namespace gem5 {

SolomonCache::SolomonCache(const SolomonCacheParams &p)
    : Cache(p), num_parity_symbols(2 * p.symbol_errors), rs_codec(nullptr) {
  if (blkSize > 255) {
    panic("SolomonCache::SolomonCache; Cannot generate cache w/ blkSize > 255 "
          "over libcorrect GF(2^8)");
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

  if (cpkt) {
    cpkt->writeDataToBlock(blk->data, blkSize);
    recomputeAndStoreECC(blk);
  }
  Cache::updateBlockData(blk, cpkt, has_old_data);
}

void SolomonCache::recomputeAndStoreECC(CacheBlk *blk) {
  memcpy(enc_dec_buf.data(), blk->data, blkSize);

  correct_reed_solomon_encode(rs_codec, enc_dec_buf.data(), total_msg_size,
                              enc_dec_buf.data());

  uint8_t *parity_bit_start = enc_dec_buf.data() + blkSize;
  const std::vector<uint8_t> parity_block = std::vector<uint8_t>(
      parity_bit_start, parity_bit_start + num_parity_symbols);
  blockParityMap[blk] = parity_block;
}

SolomonCache::ECCResult SolomonCache::checkAndCorrectECC(CacheBlk *blk) {
  // Reconstruct the ECC block from cache block and map
  memcpy(enc_dec_buf.data(), blk->data, blkSize);
  memcpy(enc_dec_buf.data() + blkSize, blockParityMap[blk].data(),
         num_parity_symbols);

  ssize_t result = correct_reed_solomon_decode(rs_codec, enc_dec_buf.data(),
                                               blkSize, enc_dec_buf.data());
  // NOTE: There is a chance that RS can silently fail, but in that case we have
  // bigger problems If bad, just mark it as such. If corrected, write back the
  // corrections.
  if (result < 0) {
    return ECCResult::Unrecoverable;
  } else if (memcmp(enc_dec_buf.data(), blk->data, blkSize) != 0) {
    memcpy(blk->data, enc_dec_buf.data(), blkSize);
    return ECCResult::Corrected;
  }
  return ECCResult::Clean;
}

bool SolomonCache::operationReadsData(PacketPtr pkt) const {
  return pkt->isRead();
}

bool SolomonCache::operationModifiesData(PacketPtr pkt) const {
  return pkt->isWrite() || pkt->cmd == MemCmd::SwapReq;
}

void SolomonCache::invalidateBlock(CacheBlk *blk) {
  blockParityMap.erase(blk);
  BaseCache::invalidateBlock(blk);
}

void SolomonCache::satisfyRequest(PacketPtr pkt, CacheBlk *blk,
                                  bool deferred_response,
                                  bool pending_downgrade) {

  if (blk && blk->isValid() && operationReadsData(pkt)) {
    ECCResult result = checkAndCorrectECC(blk);
    static int refresh_count = 0;
    if (result == ECCResult::Unrecoverable) {
      refresh_count++;
      std::cerr << "ECC refresh #" << refresh_count << " at tick " << curTick()
                << "\n";
      // pull fresh data from memory directly into the block.
      // build a request packet matching this block's address and use
      // a functional access to fill it synchronously.
      Addr blk_addr = regenerateBlkAddr(blk);
      RequestPtr req = std::make_shared<Request>(blk_addr, blkSize, 0,
                                                 Request::funcRequestorId);
      if (blk->isSecure()) {
        req->setFlags(Request::SECURE);
      }

      Packet refresh_pkt(req, MemCmd::ReadReq);
      refresh_pkt.dataStatic(blk->data); // write directly into block

      // functional access: synchronous, bypasses timing
      memSidePort.sendFunctional(&refresh_pkt);

      // recompute and store fresh ECC for the refreshed data
      recomputeAndStoreECC(blk);

      // TODO: when doing stats, compute the ecc refresh here
      // ex. stats.eccRefreshes++;
    }
  }
  Cache::satisfyRequest(pkt, blk, deferred_response, pending_downgrade);

  if (blk && blk->isValid() && operationModifiesData(pkt)) {
    recomputeAndStoreECC(blk);
  }
}

} // namespace gem5
