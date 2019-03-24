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
// Eliminate redundant local.sets: if a local already has a particular
// value, we don't need to set it again. A common case here is loops
// that start at zero, since the default value is initialized to
// zero anyhow.
//
// A risk here is that we extend live ranges, e.g. we may use the default
// value at the very end of a function, keeping that local alive throughout.
// For that reason it is probably better to run this near the end of
// optimization, and especially after coalesce-locals. A final vaccum
// should be done after it, as this pass can leave around drop()s of
// values no longer necessary.
//

#include <wasm.h>
#include <pass.h>
#include <wasm-builder.h>
#include <ir/find_all.h>
#include <ir/local-graph.h>
#include <ir/literal-utils.h>
#include <ir/properties.h>
#include <ir/utils.h>
#include <support/work_list.h>

namespace wasm {

namespace {

// Finds which sets are equivalent, that is, must contain the same value.
// In addition to sets, constant values are also tracked (for the zero-init
// values in particular, which have no sets).
class Equivalences {
public:
  Equivalences(Function* func) : func(func), graph(func) {
    compute();
  }

  bool areEquivalent(SetLocal* a, SetLocal* b) {
    return getKnownClass(a) == getKnownClass(b);
  }

  // Return the class. 0 is the "null class" - we haven't calculated it yet.
  Index getClass(SetLocal* set) {
    auto iter = setClasses.find(set);
    if (iter == setClasses.end()) {
      return 0;
    }
    auto ret = iter->second;
    assert(ret != 0);
    return ret;
  }

  Index getKnownClass(SetLocal* set) {
    auto ret = getClass(set);
    assert(ret != 0);
    return ret;
  }

  bool known(SetLocal* set) {
    return getClass(set) != 0;
  }

  Index getClass(Literal literal) {
    auto iter = literalClasses.find(literal);
    if (iter == literalClasses.end()) {
      return 0;
    }
    return iter->second;
  }

private:
  Function* func;
  LocalGraph graph;

  // There is a unique id for each class, which this maps sets to.
  std::unordered_map<SetLocal*, Index> setClasses;

  std::unordered_map<Literal, Index> literalClasses;

  void compute() {
    FindAll<SetLocal> allSets(func->body);
    // Set up the graph of direct connections. We'll use this to calculate the final
    // equivalence classes (since being equivalent is a symmetric, transitivie, and
    // reflexive operation).
    struct Node {
      SetLocal* set = nullptr;
      Literal literal;

      std::vector<Node*> directs; // direct equivalences, resulting from copying a value
      std::vector<Node*> mergesIn, mergesOut;

      void addDirect(Node* other) {
        directs.push_back(other);
        other->directs.push_back(this);
      }
      void addMergeIn(Node* other) {
        mergesIn.push_back(other);
        other->mergesOut.push_back(this);
      }
    };
    std::vector<std::unique_ptr<Node>> nodes;
    // Add sets in the function body.
    std::map<SetLocal*, Node*> setNodes;
    for (auto* set : allSets.list) {
      auto node = make_unique<Node>();
      node->set = set;
      setNodes.emplace(set, node.get());
      nodes.push_back(std::move(node));
    }
    // Add zeros of all types, for the zero inits.
    std::map<Literal, Node*> literalNodes;
    for (auto type : { i32, i64, f32, f64, v128 }) { // TODO: centralize?
      auto node = make_unique<Node>();
      node->literal = LiteralUtils::makeZero(type);
      literalNodes.emplace(node->literal, node.get());
      nodes.push_back(std::move(node));
    }
    // Utility to get a node, where set may be nullptr, in which case it is
    // the zero init.
    auto getNode = [&](SetLocal* set, Type type) {
      if (set) {
        return setNodes[set];
      } else {
        return literalNodes[Literal::makeZero(type)];
      }
    };
    // Add connections.
    for (auto& node : nodes) {
      auto* set = node->set;
      // Literal nodes will be connected to by others.
      if (!set) continue;
      auto* value = set->value;
      // Don't connect unreachable sets, just ignore them.
      if (value->type == unreachable) continue;
      // Look through trivial fallthrough-ing (but stop if the value were used - TODO?)
      value = Properties::getUnusedFallthrough(value);
      if (auto* tee = value->dynCast<SetLocal>()) {
        node->addDirect(setNodes[tee]);
      } else if (auto* get = value->dynCast<GetLocal>()) {
        auto& sets = graph.getSetses[get];
        if (sets.size() == 1) {
          node->addDirect(getNode(*sets.begin(), value->type));
        } else if (sets.size() > 1) {
          for (auto* otherSet : sets) {
            node->addMergeIn(getNode(otherSet, value->type));
          }
        }
      } else if (auto* c = value->dynCast<Const>()) {
        auto literal = c->value;
        auto iter = literalNodes.find(literal);
        if (iter != literalNodes.end()) {
          node->addDirect(iter->second);
        } else {
          literalNodes[literal] = node.get();
        }
        node->literal = literal;
      }
    }
    // Calculating the final classes is mostly a simple floodfill operation,
    // however, merges are more interesting: we can only see that a merge
    // set is equivalent to another if all the things it merges are equivalent.
    Index currClass = 0;
    for (auto& start : nodes) {
      if (known(start->set)) continue;
      currClass++;
      // Floodfill the current node.
      WorkList<Node*> work;
      work.push(start.get());
      while (!work.empty()) {
        auto* node = work.pop();
        auto* set = node->set;
        // At this point the class may be unknown, or it may be another class - consider
        // the case that A and B are linked, and merge into C, and we start from C. Then C
        // by itself can do nothing yet, until we first see the other two are identical,
        // and get prompted to look again at C. In that case, we will trample the old
        // class. In other words, we should only stop here if we see the class we are
        // currently flooding (as we can do nothing more for it).
        if (getClass(set) == currClass) continue;
        setClasses[set] = currClass;
        literalClasses[node->literal] = currClass;
        for (auto* direct : node->directs) {
          work.push(direct);
        }
        // Check outgoing merges - we may have enabled a node to be marked as
        // being in this equivalence class.
        for (auto* mergeOut : node->mergesOut) {
          if (getClass(mergeOut->set) == currClass) continue;
          assert(!mergeOut->mergesIn.empty());
          bool ok = true;
          for (auto* mergeIn : mergeOut->mergesIn) {
            if (getClass(mergeIn->set) != currClass) {
              ok = false;
              break;
            }
          }
          if (ok) {
            work.push(mergeOut);
          }
        }
      }
    }

#if EQUIVALENCES_DEBUG
    for (auto& node : nodes) {
      auto* set = node->set;
      std::cout << "set " << set << " has index " << set->index << " and class " << getClass(set) << '\n';
    }
#endif
  }
};

// Instrumentation helpers.

static Expression*& getInstrumentedValue(SetLocal* set) {
  return set->value->cast<Block>()->list[0]->cast<Drop>()->value;
}

static GetLocal* getInstrumentedGet(SetLocal* set) {
  return set->value->cast<Block>()->list[1]->cast<GetLocal>();
}

// Marks a set as unneeded, and so we can remove it during uninstrumentation.
// We remove the get from it, which is no longer needed anyhow at this point.
static void markSetAsUnneeded(SetLocal* set) {
  set->value->cast<Block>()->list.pop_back();
}

static bool isSetUnneeded(SetLocal* set) {
  return set->value->cast<Block>()->list.size() == 1;
}

// Main class.

struct RedundantSetElimination : public WalkerPass<PostWalker<RedundantSetElimination>> {
  bool isFunctionParallel() override { return true; }

  Pass* create() override { return new RedundantSetElimination(); }

  // main entry point

  void doWalkFunction(Function* func) {
    Equivalences equivalences_(func);
    equivalences = &equivalences_;
    // Instrument the function so we can tell what value is present at a local
    // index right before each set.
    instrument(func);
    {
      // Compute the getSets across the instrumented function.
      LocalGraph graph_(func);
      graph = &graph_;
      // Remove redundant sets.
      WalkerPass<PostWalker<RedundantSetElimination>>::doWalkFunction(func);
      graph = nullptr; // TODO: is there some std::dependent/borrowed_ptr?
    }
    // Clean up.
    unInstrument(func);
    equivalences = nullptr;
  }

  LocalGraph* graph;
  Equivalences* equivalences;

  void visitSetLocal(SetLocal* curr) {
    if (curr->type == unreachable) return;
    auto* getBeforeSet = getInstrumentedGet(curr);
    auto& sets = graph->getSetses[getBeforeSet];
    if (sets.size() == 1) { // TODO: if multiple, check if all equivalent
      auto* parent = *sets.begin();
      Index parentClass;
      if (parent) {
        parentClass = equivalences->getClass(parent);
      } else {
        parentClass = equivalences->getClass(Literal::makeZero(curr->value->type));
      }
      if (equivalences->getClass(curr) == parentClass) {
        markSetAsUnneeded(curr);
      }        
    }
  }

private:
  MixedArena tempAllocations;

  void instrument(Function* func) {
    // We replace
    //  (local.set $x
    //    (value)
    //  )
    // with
    //  (local.set $x
    //    (block
    //      (drop (value))
    //      (local.get $x)
    //    )
    //  )
    // Note that this changes the logic, but all we care about is being able
    // to find the sets for that get that happens right before the set.
    struct Instrumenter : public PostWalker<Instrumenter> {
      MixedArena& tempAllocations;

      Instrumenter(Function* func, MixedArena& tempAllocations) : tempAllocations(tempAllocations) {
        walk(func->body);
      }

      void visitSetLocal(SetLocal* curr) {
        if (curr->type == unreachable) return;
        Builder builder(tempAllocations);
        curr->value = builder.makeSequence(
          builder.makeDrop(curr->value),
          builder.makeGetLocal(curr->index, curr->value->type)
        );
      }
    } instrumenter(func, tempAllocations);
  }

  void unInstrument(Function* func) {
    struct UnInstrumenter : public PostWalker<UnInstrumenter> {
      UnInstrumenter(Function* func) {
        walk(func->body);
      }

      void visitSetLocal(SetLocal* curr) {
        if (curr->type == unreachable) return;
        auto* value = getInstrumentedValue(curr);
        if (!isSetUnneeded(curr)) {
          curr->value = value;
        } else {
          if (curr->type == none) {
            auto* drop = ExpressionManipulator::convert<SetLocal, Drop>(curr);
            drop->value = value;
          } else {
            replaceCurrent(value);
          }
        }
      }
    } unInstrumenter(func);
  }
};

} // anonymous namespace

Pass *createRedundantSetEliminationPass() {
  return new RedundantSetElimination();
}

} // namespace wasm
