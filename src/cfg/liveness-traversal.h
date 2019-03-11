/*
 * Copyright 2017 WebAssembly Community Group participants
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
// Computes liveness information for locals.
//

#ifndef liveness_traversal_h
#define liveness_traversal_h

#include "support/sorted_vector.h"
#include "wasm.h"
#include "wasm-builder.h"
#include "wasm-traversal.h"
#include "cfg-traversal.h"
#include "ir/utils.h"

namespace wasm {

namespace CFG {

// A liveness-relevant action. Supports a get, a set, or an
// "other" which can be used for other purposes, to mark
// their position in a block
struct LivenessAction {
  enum What {
    Get = 0,
    Set = 1,
    Other = 2
  };
  What what;
  Index index; // the local index read or written
  Expression** origin; // the origin

  LivenessAction(What what, Index index, Expression** origin) : what(what), index(index), origin(origin), effective(false) {
    assert(what != Other);
    if (what == Get) assert((*origin)->is<GetLocal>());
    if (what == Set) assert((*origin)->is<SetLocal>());
  }
  LivenessAction(Expression** origin) : what(Other), origin(origin) {}

  bool isGet() { return what == Get; }
  bool isSet() { return what == Set; }
  bool isOther() { return what == Other; }

  GetLocal* getGet() {
    if (isGet()) {
      return (*origin)->cast<GetLocal>();
    }
    return nullptr;
  }

  SetLocal* getSet() {
    if (isSet()) {
      return (*origin)->cast<SetLocal>();
    }
    return nullptr;
  }

  // Helper to remove a set that we know is not needed. This
  // updates both the IR and the action.
  void removeSet() {
    assert(isSet());
    auto* set = getSet();
    if (set->isTee()) {
      *origin = set->value->cast<GetLocal>();
    } else {
      ExpressionManipulator::nop(set);
    }
    // Mark as an other: even if we turned the origin into a get,
    // we already have another Action for that get, that properly
    // represents it.
    what = Other;
  }
};

// A set of local indexes. This is optimized for comparisons,
// mergings, and iteration on elements, assuming that there
// may be a great many potential elements but actual sets
// may be fairly small. Specifically, we use a sorted
// vector.
using IndexSet = SortedVector;

// A set of SetLocals.
using SetSet = std::set<SetLocal*>;

// information about liveness in a basic block
struct Liveness {
  std::vector<LivenessAction> actions; // actions occurring in this block

#if LIVENESS_DEBUG
  void dump(Function* func) {
    if (actions.empty()) return;
    std::cout << "    actions:\n";
    for (auto& action : actions) {
      std::cout << "      " << (action.isGet() ? "get" : (action.isSet() ? "set" : "other")) << " " << func->getLocalName(action.index) << "\n";
    }
  }
#endif // LIVENESS_DEBUG

  // Live indexes and sets, at the start and end
  IndexSet startIndexes, endIndexes;
  SetSet startSets, endSets;
};

template<typename SubType, typename VisitorType>
struct LivenessWalker : public CFGWalker<SubType, VisitorType, Liveness> {
  using Super = CFGWalker<SubType, VisitorType, Liveness>;
  using BasicBlock = Super::BasicBlock;

  Index numLocals;
  std::unordered_set<BasicBlock*> liveBlocks;

  // cfg traversal work

  static void doVisitGetLocal(SubType* self, Expression** currp) {
    auto* curr = (*currp)->cast<GetLocal>();
     // if in unreachable code, ignore
    if (!self->currBasicBlock) {
      *currp = Builder(*self->getModule()).replaceWithIdenticalType(curr);
      return;
    }
    self->currBasicBlock->actions.emplace_back(LivenessAction::Get, curr->index, currp);
  }

  static void doVisitSetLocal(SubType* self, Expression** currp) {
    auto* curr = (*currp)->cast<SetLocal>();
    // if in unreachable code, we don't need the tee (but might need the value, if it has side effects)
    if (!self->currBasicBlock) {
      if (curr->isTee()) {
        *currp = curr->value;
      } else {
        *currp = Builder(*self->getModule()).makeDrop(curr->value);
      }
      return;
    }
    self->currBasicBlock->actions.emplace_back(LivenessAction::Set, curr->index, currp);
  }

  // main entry point

  void doWalkFunction(Function* func) {
    numLocals = func->getNumLocals();
    // Create the CFG by walking the IR.
    Super::doWalkFunction(func);
    // Ignore links to dead blocks, so they don't confuse us and we can see their stores are all ineffective
    liveBlocks = Super::findLiveBlocks();
    Super::unlinkDeadBlocks(liveBlocks);
    // Flow index liveness first.
    flowIndexLiveness();
    // Flow sets, using the index liveness info.
    flowSetLiveness();
  }

  void flowIndexLiveness() {
    // merge starts of a list of blocks. return
    // whether anything changed vs an old state (which indicates further processing is necessary).
    auto mergeStartsAndCheckChange = [this](std::vector<BasicBlock*>& blocks, LocalSet& old, LocalSet& ret) {
      if (blocks.size() == 0) return false;
      ret = blocks[0]->startIndexes;
      if (blocks.size() > 1) {
        // more than one, so we must merge
        for (Index i = 1; i < blocks.size(); i++) {
          ret = ret.merge(blocks[i]->startIndexes);
        }
      }
      return old != ret;
    };

    auto scanLivenessThroughActions = [this](std::vector<LivenessAction>& actions, LocalSet& live) {
      // move towards the front
      for (int i = int(actions.size()) - 1; i >= 0; i--) {
        auto& action = actions[i];
        if (action.isGet()) {
          live.insert(action.index);
        } else if (action.isSet()) {
          live.erase(action.index);
        }
      }
    };

    // keep working while stuff is flowing
    std::set<BasicBlock*> queue;
    for (auto& block : Super::basicBlocks) {
      if (liveBlocks.count(block.get()) == 0) continue; // ignore dead blocks
      queue.insert(block.get());
      // do the first scan through the block, starting with nothing live at the end, and updating the liveness at the start
      scanLivenessThroughActions(block->actions, block->startIndexes);
    }
    // at every point in time, we assume we already noted interferences between things already known alive at the end, and scanned back through the block using that
    while (queue.size() > 0) {
      auto iter = queue.begin();
      auto* curr = *iter;
      queue.erase(iter);
      LocalSet live;
      if (!mergeStartsAndCheckChange(curr->out, curr->endIndexes, live)) continue;
      assert(curr->endIndexes.size() < live.size());
      curr->endIndexes = live;
      scanLivenessThroughActions(curr->actions, live);
      // liveness is now calculated at the start. if something
      // changed, all predecessor blocks need recomputation
      if (curr->startIndexes == live) continue;
      assert(curr->startIndexes.size() < live.size());
      curr->startIndexes = live;
      for (auto* in : curr->in) {
        queue.insert(in);
      }
    }
  }

  void flowSetLiveness() {
    // Flow the sets in each block to the end of the block.
    std::map<Index, SetLocal*> indexSets;
    for (auto& block : Super::basicBlocks) {
      if (liveBlocks.count(block.get()) == 0) continue; // ignore dead blocks
      for (auto& action : block.actions) {
        if (auto* set = action.getSet()) {
          // Possibly overwrite a previous set.
          indexSets[action.index] = set;
        }
      }
      // We know which sets may be live at the end. Verify by our knowledge of index liveness.
      for (auto& pair : indexSets) {
        auto index = pair.first;
        auto* set = pair.second;
        if (block->endIndexes.has(index)) {
          block->endSets.insert(set);
        }
      }
    }

    // Find out for each block which indexes are set in it. This lets us quickly see if
    // a set flows through a block.
    std::map<BasicBlock*, IndexSet> blockIndexesSet;
    for (auto& block : Super::basicBlocks) {
      if (liveBlocks.count(block.get()) == 0) continue; // ignore dead blocks
      for (auto& action : setBlock.actions) {
        if (action.isSet()) {
          blockIndexesSet[block.get()].insert(action.index);
        }
      }
    }

    // Flow sets forward through blocks.
    // TODO: batching?
    for (auto& block : Super::basicBlocks) {
      if (liveBlocks.count(block.get()) == 0) continue; // ignore dead blocks
      for (auto& action : setBlock.actions) {
        if (auto* set = action.getSet()) {
          if (block->endSets.count(set)) {
            // This set is live at the end of the block - do the flow.
            std::set<BasicBlock*> queue;
            for (auto* succ : block->out) {
              queue.insert(succ);
            }
            while (queue.size() > 0) {
              auto iter = queue.begin();
              auto* block = *iter;
              queue.erase(iter);
              // If already seen here, stop.
              if (block->startSets.count(set)) continue;
              block->startSets.insert(set);
              // If it doesn't flow through, stop.
              if (blockIndexesSet[block].has(set->index)) continue;
              // If the index is no longer live, stop.
              if (!block->endIndexes.has(set->index)) continue;
              // It made it all the way through!
              block->endSets.insert(set);
              for (auto* succ : block->out) {
                queue.insert(succ);
              }
            }
          }
        }
      }
    }
  }
};

} // namespace CFG

} // namespace wasm

#endif // liveness_traversal_h
