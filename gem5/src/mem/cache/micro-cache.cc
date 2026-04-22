/*
 * Author: YOUR NAME HERE!
 * Copyright (c) 2023 Brown University
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

#include "mem/cache/micro-cache.hh"
#include "base/types.hh"
#include "params/MicroCache.hh"

namespace gem5 {

MicroCache::MicroCache(const MicroCacheParams *p)
    : SimObject(*p), cpu_side_port(p->name + ".cpu_side_port", this),
      mem_side_port(p->name + ".mem_side_port", this),
      memSendEvent([this] { sendToMem(); }, name()),
      cpuSendEvent([this] { sendToCpu(); }, name()),
      writebackEvent([this] { writebackToMem(); }, name()),
      unblockEvent([this] { unblock(); }, name()), toMem(nullptr),
      toCPU(nullptr), toWriteback(nullptr), latency(p->latency * 1000),
      assoc(p->assoc), blocked(false), writingback(false), pending(nullptr),
      stats(this), blockSize(0), numBlocks(0), associativity(0), setTotal(0)
// TODO: YOUR ADDITIONAL FIELDS HERE!
{
  assert(p->size >= 64);

  blockSize = 64;
  numBlocks = p->size / blockSize;
  associativity = p->assoc; // Ways
  setTotal = numBlocks / associativity;

  // Allocate numBlocks blocks
  // I decided to treat as a 1D array since it's simpler tbh
  blks = (Block *)malloc(sizeof(Block) * numBlocks);
  // Initialize all blocks
  for (int i = 0; i < numBlocks; i++) {
    blks[i].valid = false;
    blks[i].dirty = false;
    blks[i].tag = 0;
    blks[i].last_access_time = 0;
    memset(blks[i].data, 0, blockSize);
  }

  DPRINTF(MicroCache, "Initialized MicroCache with size %d, associativity %d\n",
          p->size, p->assoc);
};

/* Method required for Cache object to communicate */
Port &MicroCache::getPort(const std::string &if_name, PortID idx) {
  if (if_name == "cpu_side") {
    return cpu_side_port;
  } else if (if_name == "mem_side") {
    return mem_side_port;
  }
  return SimObject::getPort(if_name, idx);
}

/* Request from CPU side */
bool MicroCache::handleRequest(PacketPtr pkt) {
  if (blocked) {
    return false;
  }
  blocked = true;
  assert(pkt != nullptr);

  // Grab all the things
  Addr pktAddr = pkt->getAddr();
  int set = getSetIndex(pktAddr);
  Addr tag = getTag(pktAddr);
  Block *blk = nullptr;
  blk = findBlock(pktAddr); // nullptr if miss

  DPRINTF(MicroCache, "Received request for addr 0x%lx (set %d, tag 0x%lx)\n",
          pktAddr, set, tag);

  bool hitOrMiss = (blk != nullptr); // false -> no hit, true -> hit
  bool doSomething = pkt->isRead() || pkt->isWrite();

  if (hitOrMiss && doSomething) {
    // hit case!
    stats.hits++;                      // scaff
    blk->last_access_time = curTick(); // for LRU

    DPRINTF(MicroCache, "Cache hit for addr 0x%lx (set %d, tag 0x%lx)\n",
            pktAddr, set, tag);

    if (pkt->isWrite()) {
      blk->dirty = true;
      pkt->writeData(blk->data);
    }
    if (pkt->needsResponse()) {
      pkt->setData(blk->data);
      pkt->makeTimingResponse();
      toCPU = pkt;

      // schedule response event to be sent
      schedule(cpuSendEvent, curTick() + latency);
    } else {
      schedule(unblockEvent, curTick() + latency);
    }
  } else if (!hitOrMiss && doSomething) {
    assert(pkt->isRead() || pkt->isWrite());
    // miss case!
    stats.misses++;
    requestFromMem(pktAddr);
    DPRINTF(MicroCache, "Cache miss for addr 0x%lx (set %d, tag 0x%lx)\n",
            pktAddr, set, tag);
    pending = pkt; // track the request we sent
  } else {
    // Ignore non-read, non-write packets
    blocked = false;
  }

  return true;
}

void MicroCache::handleResponse(PacketPtr pkt) {
  assert(pending != nullptr);
  Addr pendingAddr = pending->getAddr() & ~((Addr)63);
  int set = getSetIndex(pendingAddr);
  Addr tag = getTag(pendingAddr);
  DPRINTF(MicroCache, "Received response for addr 0x%lx (set %d, tag 0x%lx)\n",
          pendingAddr, set, tag);

  Block *evictee = findEvictee(set);

  if (writingback) {
    writingback = false;
    assert(pkt->isWrite() && pkt->isResponse());
  } else {

    DPRINTF(MicroCache, "Evicting block with tag 0x%lx from set %d\n",
            evictee->tag, set);

    if (evictee->valid && evictee->dirty) {
      Addr evicteeAddr = getBlockAddr(evictee->tag, set);
      writebackData(true, evicteeAddr, evictee->data);
      writingback = true;
    }

    pkt->writeData(evictee->data);
    if (pending->isWrite()) {
      pending->writeData(evictee->data);
    }
    if (pending->needsResponse()) {
      pending->setData(evictee->data);
    }

    // now set metadata
    evictee->tag = tag;
    evictee->valid = true;
    evictee->dirty = pending->isWrite();
    evictee->last_access_time = curTick();
  }

  // respond to CPU if necessary and unblock
  if (!writingback) {
    assert(blocked);
    if (pending->needsResponse()) {
      // make sure pending data is set here or above!
      pending->setData(evictee->data);
      pending->makeTimingResponse();
      toCPU = pending;
      pending = nullptr;

      // schedule response event to be sent
      schedule(cpuSendEvent, curTick() + latency);
    } else {
      pending = nullptr;
      schedule(unblockEvent, curTick() + latency);
    }
  }

  delete pkt; // scaff
}

MicroCache::MicroCacheStats::MicroCacheStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(hits, statistics::units::Count::get(), "Number of hits"),
      ADD_STAT(misses, statistics::units::Count::get(), "Number of misses"),
      ADD_STAT(hitRate, statistics::units::Ratio::get(),
               "Number of hits/ (hits + misses)", hits / (hits + misses))

{}

gem5::MicroCache *MicroCacheParams::create() const {
  return new gem5::MicroCache(this);
}

} // namespace gem5