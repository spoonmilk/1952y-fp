/*
 * Copyright (c) 2011, 2014 ARM Limited
 * Copyright (c) 2022-2023 The University of Edinburgh
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2006 The Regents of The University of Michigan
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

#include "cpu/pred/hybrid.hh"
#include "base/random.hh"
#include "base/types.hh"
#include <vector>

// #include "base/bitfield.hh"
// #include "base/intmath.hh"

namespace gem5 {

namespace branch_prediction {

HybridBP::HybridBP(const HybridBPParams &params)
    : BPredUnit(params), localBP(params.localBP),
      tournamentBP(params.tournamentBP), bimodeBP(params.bimodeBP),
      perceptronBP(params.perceptronBP), tageBP(params.tageBP), stats(this) {
  buffer = new HBPBuffer(512);
  hbpIndexMask = 512 - 1;
}

void deserCounters(uint16_t counters, uint8_t *out) {
  out[0] = (uint8_t)(counters & 0x03);
  out[1] = (uint8_t)(counters >> 2 & 0x03);
  out[2] = (uint8_t)(counters >> 4 & 0x03);
  out[3] = (uint8_t)(counters >> 6 & 0x03);
  out[4] = (uint8_t)(counters >> 8 & 0x03);
}

uint16_t serCounters(uint8_t *ctrs) {
  return (uint16_t)(ctrs[0] | (ctrs[1] << 2) | (ctrs[2] << 4) | (ctrs[3] << 6) |
                    (ctrs[4] << 8));
}

bool HybridBP::lookup(ThreadID tid, Addr pc, void *&bp_history) {
  PredHistPtrContainer *phpc = new PredHistPtrContainer;

  // Run all predictors simultaneously
  bool localPred = localBP->lookup(tid, pc, phpc->localHist);
  bool tournamentPred = tournamentBP->lookup(tid, pc, phpc->tournamentHist);
  bool bimodePred = bimodeBP->lookup(tid, pc, phpc->bimodeHist);
  bool perceptronPred = perceptronBP->lookup(tid, pc, phpc->perceptronHist);
  bool tagePred = tageBP->lookup(tid, pc, phpc->tageHist);

  phpc->localPred = localPred;
  phpc->tournamentPred = tournamentPred;
  phpc->bimodePred = bimodePred;
  phpc->perceptronPred = perceptronPred;
  phpc->tagePred = tagePred;

  bp_history = phpc;
  uint32_t bp_index = pc & hbpIndexMask;
  HBPEntry *predEntry = &(*buffer)[bp_index];

  // 0=TAGE, 1=Perceptron, 2=BiMode, 3=Tournament, 4=Local
  uint8_t ctrs[5];
  deserCounters(predEntry->counters, ctrs);
  bool preds[5] = {tagePred, perceptronPred, bimodePred, tournamentPred,
                   localPred};

  uint8_t maxVal = 0;
  for (int i = 0; i < 5; i++) {
    if (ctrs[i] > maxVal)
      maxVal = ctrs[i];
  }
  int numAtMax = 0;
  for (int i = 0; i < 5; i++) {
    if (ctrs[i] == maxVal)
      numAtMax++;
  }

  PredictorID selected;
  if (numAtMax == 1 && maxVal > CTHRESH) {
    for (int i = 0; i < 5; i++) {
      if (ctrs[i] == maxVal) {
        selected = static_cast<PredictorID>(4 - i);
        break;
      }
    }
  } else if (numAtMax > 1) {
    // To resolve tie, scan through stack for first at max (if > 1)
    uint8_t slen = predEntry->stack & 0x03;
    selected = PredictorID::None;
    for (int s = 0; s < slen && selected == PredictorID::None; s++) {
      PredictorID candidate =
          static_cast<PredictorID>((predEntry->stack >> (2 + s * 3)) & 0x07);
      if (ctrs[4 - (int)candidate] == maxVal)
        selected = candidate;
    }
    if (selected == PredictorID::None)
      selected = static_cast<PredictorID>(random_mt.random<int>(0, 4));
  } else {
    // Random otherwise
    selected = static_cast<PredictorID>(random_mt.random<int>(0, 4));
  }

  phpc->selected = selected;

  switch (selected) {
  case PredictorID::Local:
    stats.localPreds++;
    break;
  case PredictorID::Tournament:
    stats.tournamentPreds++;
    break;
  case PredictorID::BiMode:
    stats.bimodePreds++;
    break;
  case PredictorID::Perceptron:
    stats.perceptronPreds++;
    break;
  case PredictorID::TAGE:
    stats.tagePreds++;
    break;
  default:
    break;
  }

  return preds[4 - (int)selected];
}

void HybridBP::updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                               Addr target, void *&bp_history) {
  assert(uncond || bp_history);

  if (!bp_history) {
    bp_history = new PredHistPtrContainer;
  }
  PredHistPtrContainer *phpc = static_cast<PredHistPtrContainer *>(bp_history);

  localBP->updateHistories(tid, pc, uncond, taken, target, phpc->localHist);
  tournamentBP->updateHistories(tid, pc, uncond, taken, target,
                                phpc->tournamentHist);
  bimodeBP->updateHistories(tid, pc, uncond, taken, target, phpc->bimodeHist);
  perceptronBP->updateHistories(tid, pc, uncond, taken, target,
                                phpc->perceptronHist);
  tageBP->updateHistories(tid, pc, uncond, taken, target, phpc->tageHist);
}

void HybridBP::update(ThreadID tid, Addr pc, bool taken, void *&bp_history,
                      bool squashed, const StaticInstPtr &inst, Addr target) {
  assert(bp_history);
  PredHistPtrContainer *phpc = static_cast<PredHistPtrContainer *>(bp_history);

  localBP->update(tid, pc, taken, phpc->localHist, squashed, inst, target);
  tournamentBP->update(tid, pc, taken, phpc->tournamentHist, squashed, inst,
                       target);
  bimodeBP->update(tid, pc, taken, phpc->bimodeHist, squashed, inst, target);
  perceptronBP->update(tid, pc, taken, phpc->perceptronHist, squashed, inst,
                       target);
  tageBP->update(tid, pc, taken, phpc->tageHist, squashed, inst, target);

  if (squashed) {
    return;
  }

  assert(!phpc->localHist);
  assert(!phpc->tournamentHist);
  assert(!phpc->bimodeHist);
  assert(!phpc->perceptronHist);
  assert(!phpc->tageHist);

  uint32_t bp_index = pc & hbpIndexMask;
  HBPEntry *predEntry = &(*buffer)[bp_index];

  uint8_t ctrs[5];
  deserCounters(predEntry->counters, ctrs);

  bool preds[5] = {phpc->tagePred, phpc->perceptronPred, phpc->bimodePred,
                   phpc->tournamentPred, phpc->localPred};

  // Inc on correct prediction, dec on wrong.
  for (int i = 0; i < 5; i++) {
    if (preds[i] == taken) {
      if (ctrs[i] < 3)
        ctrs[i]++;
    } else {
      if (ctrs[i] > 0)
        ctrs[i]--;
    }
  }

  predEntry->counters = serCounters(ctrs);

  // If correct, push selected onto recency queue
  int selIdx = 4 - (int)phpc->selected;
  if (preds[selIdx] == taken) {
    uint8_t selId = (uint8_t)phpc->selected;
    uint8_t slen = predEntry->stack & 0x03;
    uint8_t slot0 = (predEntry->stack >> 2) & 0x07;
    uint8_t slot1 = (predEntry->stack >> 5) & 0x07;
    uint16_t newStack = (uint16_t)selId << 2;
    if (slen >= 1)
      newStack |= (uint16_t)slot0 << 5;
    if (slen >= 2)
      newStack |= (uint16_t)slot1 << 8;
    newStack |= (slen < 3) ? slen + 1 : 3;
    predEntry->stack = newStack;
  }

  delete phpc;
  bp_history = nullptr;
}

void HybridBP::squash(ThreadID tid, void *&bp_history) {
  assert(bp_history);
  PredHistPtrContainer *phpc = static_cast<PredHistPtrContainer *>(bp_history);

  localBP->squash(tid, phpc->localHist);
  tournamentBP->squash(tid, phpc->tournamentHist);
  bimodeBP->squash(tid, phpc->bimodeHist);
  perceptronBP->squash(tid, phpc->perceptronHist);
  tageBP->squash(tid, phpc->tageHist);

  assert(!phpc->localHist);
  assert(!phpc->tournamentHist);
  assert(!phpc->bimodeHist);
  assert(!phpc->perceptronHist);
  assert(!phpc->tageHist);
  delete phpc;
  bp_history = nullptr;
}

HybridBP::HybridBPStats::HybridBPStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(localPreds, statistics::units::Count::get(),
               "Number of times local predictor was used"),
      ADD_STAT(tournamentPreds, statistics::units::Count::get(),
               "Number of times tournament predictor was used"),
      ADD_STAT(bimodePreds, statistics::units::Count::get(),
               "Number of times BiMode predictor was used"),
      ADD_STAT(tagePreds, statistics::units::Count::get(),
               "Number of times TAGE predictor was used"),
      ADD_STAT(perceptronPreds, statistics::units::Count::get(),
               "Number of times perceptron predictor was used") {}

} // namespace branch_prediction
} // namespace gem5
