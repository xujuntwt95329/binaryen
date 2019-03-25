/*
 * Copyright 2018 WebAssembly Community Group participants
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
// De-licm: Move a set of a local into a loop, if doing so allows us to
// get rid of a set and a get. This is good for code size, but may be bad
// for performance in baseline JITs, but should not have an effect on
// optimizing JITs which licm anyhow - so this may be a useful pass when
// optimizing for size.
//

#include <unordered_map>

#include "wasm.h"
#include "pass.h"
#include "wasm-builder.h"
#include "ir/local-graph.h"
#include "ir/effects.h"
#include "ir/find_all.h"
#include "ir/manipulation.h"

namespace wasm {

struct DeLoopInvariantCodeMotion : public WalkerPass<ControlFlowWalker<DeLoopInvariantCodeMotion>> {
  bool isFunctionParallel() override { return true; }

  Pass* create() override { return new DeLoopInvariantCodeMotion; }

  // main entry point

  LocalGraph* localGraph;

  void doWalkFunction(Function* func) {
    // The main algorithm here is to note potential sets - sets that have a single get,
    // and that get is in an inner loop, and it has no side effects. We can then see if
    // the set can reach the loop without being invalidated, and whether the loop (without
    // that get) invalidates it. If no problems arise, we can apply the optimization.
    // Compute all local dependencies first.
    LocalGraph localGraph_(func);
    localGraph = &localGraph_;
    localGraph->computeInfluences();
    localGraph->computeSSAIndexes();
    // Traverse the function.
    super::doWalkFunction(func);
  }

  struct SetInfo {
    EffectAnalyzer valueEffects;
    ExpressionStack stack;
    SetInfo(EffectAnalyzer&& valueEffects, ExpressionStack& stack)
      : valueEffects(valueEffects), stack(stack) {
    }
  };

  std::map<SetLocal*, SetInfo> setInfos;

  void visitSetLocal(SetLocal* curr) {
    if (curr->type != unreachable && !curr->isTee() && localGraph->isSSA(curr->index)) {
      // TODO if there is more than 1 get, we could create a tee etc., but must
      //      be careful to see the first get dominates the rest
      if (localGraph->setInfluences[curr].size() == 1) {
        EffectAnalyzer valueEffects(getPassOptions(), curr->value);
        if (!valueEffects.hasSideEffects()) {
          setInfos.emplace(std::make_pair(curr, SetInfo(std::move(valueEffects), controlFlowStack)));
        }
      }
    }
  }

  void visitGetLocal(GetLocal* curr) {
    if (localGraph->isSSA(curr->index)) {
      auto& sets = localGraph->getSetses[curr];
      if (sets.size() == 1) {
        auto* set = *sets.begin();
        auto iter = setInfos.find(set);
        if (iter != setInfos.end()) {
          auto* set = iter->first;
          auto& info = iter->second;
          // When checking for invalidation, we must look not just at what
          // has been traversed so far, but the entirety of relevant loops.
          // TODO: and vice versa, we are looking at too much here
          auto& setStack = info.stack;
          auto& getStack = controlFlowStack;
          Index i;
          for (i = 0; i < std::min(setStack.size(), getStack.size()); i++) {
            if (getStack[i] != setStack[i]) {
              break;
            }
          }
          if (i < getStack.size()) {
            EffectAnalyzer otherEffects(getPassOptions(), getStack[i]);
            if (!otherEffects.invalidates(info.valueEffects)) {
              replaceCurrent(set->value);
              ExpressionManipulator::nop(set);
              setInfos.erase(iter);
            }
          }
        }
      }
    }
  }
};

Pass *createDeLoopInvariantCodeMotionPass() {
  return new DeLoopInvariantCodeMotion();
}

} // namespace wasm

