/*
 * Copyright 2019 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//
// Propagate SSA local indexes through copies. That is,
//
//  a = b
//  c = a
//
// =>
//
//  a = b
//  c = b
//
// Using original indexes, instead of intermediate ones, lets us skip copies in
// the middle, which may be optimized our later. Also, earlier sets tend to have
// lower indexes which can have smaller LEB sizes.
//

#include <algorithm>

#include <wasm.h>
#include <pass.h>
#include <ir/utils.h>
#include <ir/local-graph.h>
#include <ir/properties.h>
#include <support/work_list.h>

namespace wasm {

struct CopyPropagation : public WalkerPass<PostWalker<CopyPropagation>> {
  bool isFunctionParallel() override { return true; }

  Pass* create() override { return new CopyPropagation(); }

  void doWalkFunction(Function* func) {
    // Track our changes, as it is possible that in unreachable code we end up
    // in a cycle (which we just need to break out of - it doesn't matter).
    std::set<std::pair<GetLocal*, Index>> changesDone;
    while (1) {
      bool worked = false;
      LocalGraph localGraph(func);
      localGraph.computeInfluences();
      localGraph.computeSSAIndexes();
      for (auto& pair : localGraph.locations) {
        auto* curr = pair.first;
        if (auto* get = curr->dynCast<GetLocal>()) {
          if (localGraph.isSSA(get->index)) {
            // Given a get, we have a relevant set if it has exactly one set, the set
            // is not nullptr, and it is reachable.
            auto getRelevantSet = [&](GetLocal* get) -> SetLocal* {
              auto& sets = localGraph.getSetses[get];
              if (sets.size() == 1) {
                auto* set = *sets.begin();
                if (set && set->type != unreachable) {
                  return set;
                }
              } 
              return nullptr;
            };
            // A relevant set-value is one that is itself a set, or a get.
            auto getRelevantSetValue = [&](SetLocal* set) -> Expression* {
              auto* value = Properties::getUnusedFallthrough(set->value);
              if (value->is<GetLocal>() || value->is<SetLocal>()) {
                return value;
              }
              return nullptr;
            };
            if (auto* set = getRelevantSet(get)) {
              if (auto* value = getRelevantSetValue(set)) {
                // Looks relevant - go as far back as possible.
                Index bestIndex = get->index;
                OneTimeWorkList<Expression*> work;
                work.push(value);
                while (!work.empty()) {
                  auto* value = work.pop();
                  if (auto* otherSet = value->dynCast<SetLocal>()) {
                    auto index = otherSet->index;
                    if (localGraph.isSSA(index)) {
                      if (index != get->index) {
                        bestIndex = index;
                      }
                      if (auto* otherValue = getRelevantSetValue(otherSet)) {
                        work.push(otherValue);
                      }
                    }
                  } else if (auto* otherGet = value->dynCast<GetLocal>()) {
                    auto index = otherGet->index;
                    if (localGraph.isSSA(index)) {
                      if (index != get->index) {
                        bestIndex = index;
                      }
                      if (auto* otherSet = getRelevantSet(otherGet)) {
                        work.push(otherSet);
                      }
                    }
                  } else {
                    WASM_UNREACHABLE();
                  }
                }
                // We found all the possible indexes that are equivalent to our own, pick the best.
                if (bestIndex != get->index) {
                  auto change = std::make_pair(get, bestIndex);
                  if (changesDone.insert(change).second) {
                    get->index = bestIndex;
                    worked = true;
                    // Note that we don't update getSets here - we work on the original data, and just
                    // make changes that preserve equivalence while we work.
                  }
                }
              }
            }
          }
        }
      }
      if (!worked) break;
    }
  }
};

Pass *createCopyPropagationPass() {
  return new CopyPropagation();
}

} // namespace wasm
