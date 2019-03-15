#define CFG_DEBUG 1
/*
 * Copyright 2016 WebAssembly Community Group participants
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
// Coalesce locals, in order to reduce the total number of locals. This
// is similar to register allocation, however, there is never any
// spilling, and there isn't a fixed number of locals.
//
// Our main focus here is on minimizing the number of copies, and not
// locals (although fewer locals can mean fewer copies in many cases).
// The reason is that copies actually take code size in wasm, while usually
// defining more locals does not - it at worst makes the compressed size
// less efficient (due to using more indexes). We also do not need to care
// about register pressure; the wasm VM running the code will do that.
//
// We operate on Binaryen IR here, which is not in SSA form. Doing so
// gives us a guarantee of not increasing the number of locals, and also
// lets us see copies directly. The downside is that if two sets share a
// local index, we will not split them up - we assume they share it for a
// good reason (i.e. a phi). You can run the SSA pass before this one to
// make this pass more effective on already-coalesced code.
//
// While we don't work on SSA form, as we said copies matter a lot to us,
// and so we analyze them very carefully, which does entail analyzing each
// set to see where it is live. But as mentioned earlier, we keep sets of
// a single local grouped together, which simplifies things for us; again,
// you can optionally run the SSA pass earlier.
//


#include <algorithm>
#include <memory>
#include <unordered_set>

#include "wasm.h"
#include "pass.h"
#include "ir/local-utils.h"
#include "ir/properties.h"
#include "ir/utils.h"
#include "cfg/liveness-traversal.h"
#include "wasm-builder.h"
#include "support/learning.h"
#include "support/one_time_work_list.h"
#include "support/permutations.h"
#include "support/symmetric.h"
#ifdef CFG_PROFILE
#include "support/timing.h"
#endif

namespace wasm {

struct CoalesceLocals : public WalkerPass<LivenessWalker<CoalesceLocals, Visitor<CoalesceLocals>>> {
  bool isFunctionParallel() override { return true; }

  Pass* create() override { return new CoalesceLocals; }

  // main entry point

  void doWalkFunction(Function* func);

  Index numLocals;

protected:
  void pickIndicesFromOrder(std::vector<Index>& order, std::vector<Index>& indices);
  void pickIndicesFromOrder(std::vector<Index>& order, std::vector<Index>& indices, Index& removedCopies);

  virtual void pickIndices(std::vector<Index>& indices); // returns a vector of oldIndex => newIndex

  // Utility components. These might be refactored out at some point if others need them.

  // Calculate the sets that can reach each get.
  // TODO: verify against LocalGraph!
  class GetSets {
  public:
    GetSets(CoalesceLocals& parent) {
      // Flow the sets in each block to the end of the block.
      for (auto* block : parent.liveBlocks) {
        std::map<Index, Liveness::SetSet> indexSets;
        for (auto* set : block->startSets) {
          indexSets[set->index].insert(set);
        }
        for (auto& action : block->actions) {
          if (auto* set = action.getSet()) {
            // Possibly overwrite a previous set.
            auto& sets = indexSets[action.index];
            sets.clear();
            sets.insert(set);
          } else if (auto* get = action.getGet()) {
            getSetses[get] = indexSets[action.index];
          }
        }
      }
    }

    Liveness::SetSet& getSetsFor(GetLocal* get) {
      return getSetses[get];
    }

    // TODO: make private
    // The sets for each get.
    std::map<GetLocal*, Liveness::SetSet> getSetses;
  };

  // Calculate the gets each set can reach.
  // TODO: verify against LocalGraph!
  class SetGets {
  public:
    SetGets(GetSets& getSets) {
      for (auto& pair : getSets.getSetses) {
        auto* get = pair.first;
        auto& sets = pair.second;
        for (auto* set : sets) {
          setGetses[set].insert(get);
        }
      }
    }

    std::set<GetLocal*>& getGetsFor(SetLocal* set) {
      return setGetses[set];
    }

  private:
    // The sets for each get.
    std::map<SetLocal*, std::set<GetLocal*>> setGetses;
  };

  // Find copies between locals, and especially prioritize back edges, since a copy
  // there may force us to branch just to do that copy.
  class Copies {
  public:
    void compute(CoalesceLocals& parent) {
      totalCopies.resize(parent.numLocals);
      std::fill(totalCopies.begin(), totalCopies.end(), 0);
      for (auto* block : parent.liveBlocks) {
        for (auto& action : block->actions) {
          if (auto* set = action.getSet()) {
            auto copiedIndexes = getCopiedIndexes(set->value);
            for (auto index : copiedIndexes) {
              // add 2 units, so that backedge prioritization can decide ties, but not much more
              noteCopy(set->index, index, 2);
            }
          }
        }
      }
      // Add weight to backedges.
      for (auto* loopTop : parent.loopTops) {
        // ignore the first edge, it is the initial entry, we just want backedges
        auto& in = loopTop->in;
        for (Index i = 1; i < in.size(); i++) {
          auto* arrivingBlock = in[i];
          if (arrivingBlock->out.size() > 1) continue; // we just want unconditional branches to the loop top, true phi fragments
          for (auto& action : arrivingBlock->actions) {
            if (auto* set = action.getSet()) {
              auto copiedIndexes = getCopiedIndexes(set->value);
              for (auto index : copiedIndexes) {
                noteCopy(set->index, index, 1);
              }
            }
          }
        }
      }
    }

    Index getCopies(Index i, Index j) {
      return copies.get(i, j);
    }

    std::vector<Index>& getTotalCopies() {
      return totalCopies;
    }

  private:
    SymmetricPairMap<Index, Index> copies;
    std::vector<Index> totalCopies; // total # of copies for each set, with all others

    void noteCopy(Index i, Index j, Index amount) {
      copies.get(i, j) += amount;
      totalCopies[i] += amount;
      totalCopies[j] += amount;
    }

    // Get a list of indexes of copies that we might plausibly optimize out later.
    std::vector<Index> getCopiedIndexes(Expression* value) {
      if (auto* get = value->dynCast<GetLocal>()) {
        return { get->index };
      } else if (auto* set = value->dynCast<SetLocal>()) {
        if (set->isTee()) {
          return { set->index };
        }
      } else if (auto* iff = value->dynCast<If>()) {
        auto ret = getCopiedIndexes(iff->ifTrue);
        if (iff->ifFalse) {
          auto otherIndexes = getCopiedIndexes(iff->ifFalse);
          for (auto other : otherIndexes) {
            ret.push_back(other);
          }
        }
        return ret;
      }
#if 0 // TODO: can we plausibly optimize those out later?
        auto* fallthrough = Properties::getFallthrough(value);
        if (fallthrough != value) {
          return getCopiedIndexes(fallthrough);
        }
      }
#endif
      return {};
    }
  };

  // Equivalences between sets, that is, sets that have the exact same value assigned.
  // We can use this to avoid spurious interferences.
  // TODO: handle constants here, so separate assigns to "17" are equivalent?
  class Equivalences {
  public:
    Equivalences(CoalesceLocals& parent, GetSets& getSets) : getSets(getSets) {
      compute(parent);
    }

    bool areEquivalent(SetLocal* a, SetLocal* b) {
      return getKnownClass(a) == getKnownClass(b);
    }

  private:
    GetSets& getSets;

    // There is a unique id for each class, which this maps sets to.
    std::map<SetLocal*, Index> equivalenceClasses;

    // Return the class. 0 is the "null class" - we haven't calculated it yet.
    Index getClass(SetLocal* set) {
      auto iter = equivalenceClasses.find(set);
      if (iter == equivalenceClasses.end()) {
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

    void compute(CoalesceLocals& parent) {
      // Set up the graph of direct connections. We'll use this to calculate the final
      // equivalence classes (since being equivalent is a symmetric, transitivie, and
      // reflexive operation).
      struct Node {
        SetLocal* set;

        std::vector<Node*> directs; // direct equivalences, resulting from copying a value
        std::vector<Node*> mergesIn, mergesOut;

        void addDirect(Node* other) {
          directs.push_back(other);
          other->directs.push_back(this);
        }
      };
      std::vector<std::unique_ptr<Node>> nodes;
      std::map<SetLocal*, Node*> setNodes;
      // Add sets in the function body.
      for (auto* block : parent.liveBlocks) {
        for (auto& action : block->actions) {
          if (auto* set = action.getSet()) {
            auto node = make_unique<Node>();
            node->set = set;
            setNodes.emplace(set, node.get());
            nodes.push_back(std::move(node));
          }
        }
      }
      // Add connections.
      for (auto& node : nodes) {
        auto* set = node->set;
        auto* value = set->value;
        // Look through trivial fallthrough-ing (but stop if the value were used - TODO?)
        value = Properties::getUnusedFallthrough(value);
        if (auto* tee = value->dynCast<SetLocal>()) {
          node->addDirect(setNodes[tee]);
        } else if (auto* get = value->dynCast<GetLocal>()) {
          auto& sets = getSets.getSetsFor(get);
          if (sets.size() == 1) {
            node->addDirect(setNodes[*sets.begin()]);
          } else if (sets.size() > 1) {
            for (auto* otherSet : sets) {
              auto& otherNode = setNodes[otherSet];
              node->mergesIn.push_back(otherNode);
              otherNode->mergesOut.push_back(node.get());
            }
          }
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
        OneTimeWorkList<Node*> work;
        work.push(start.get());
        while (!work.empty()) {
          auto* curr = work.pop();
          auto* set = curr->set;
          assert(!known(set));
          equivalenceClasses[set] = currClass;
          for (auto* direct : curr->directs) {
            work.push(direct);
          }
          // Check outgoing merges - we may have enabled a node to be marked as
          // being in this equivalence class.
          for (auto* mergeOut : curr->mergesOut) {
            if (known(mergeOut->set)) {
              continue;
            }
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
    }
  };

  // Interferences between sets. We assume sets of the same indexes do not interfere.
  class Interferences {
  public:
    void compute(CoalesceLocals& parent, GetSets& getSets, SetGets& setGets) {
      // Equivalences let us see if two sets that have overlapping lifetimes are actually
      // in conflict.
      Equivalences equivalences(parent, getSets);
#if CFG_DEBUG
      std::cerr << "  step5.1\n";
#endif

      // TODO: perhaps leave this checking to a cleanup at the end?
      // Add an interference, if two sets can in fact interfere
      auto maybeInterfere = [&](SetLocal* a, SetLocal* b) {
        // 1. A set cannot intefere with itself.
        // 2. If a set has the same local index, it cannot interfere - we have proof!
        // 3. If we calculated the values are equivalent, they cannot interfere.
        if (a != b &&
            a->index != b->index &&
            !equivalences.areEquivalent(a, b)) {
          setInterferences.insert(a, b);
        }
      };

      auto interfereBetweenAll = [&](Liveness::SetSet& set) {
        for (auto* a : set) {
          for (auto* b : set) {
            maybeInterfere(a, b);
          }
        }
      };

      for (auto* block : parent.liveBlocks) {
        // Everything coming in might interfere for the first time here, as they
        // might come from a different block.
        auto live = block->endSets;
        interfereBetweenAll(live);
        // scan through the block itself
        auto& actions = block->actions;
        for (int i = int(actions.size()) - 1; i >= 0; i--) {
          auto& action = actions[i];
          if (auto* get = action.getGet()) {
            // Potentially new live sets start here.
            auto& sets = getSets.getSetsFor(get);
            for (auto* set : sets) {
              live.insert(set);
              for (auto* otherSet : live) {
                maybeInterfere(set, otherSet);
              }
            }
          } if (auto* set = action.getSet()) {
            // This set is no longer live before this.
            live.erase(set);
#ifndef NDEBUG
            // No other set of that index can be live now.
            for (auto* otherSet : live) {
              assert(otherSet->index != set->index);
            }
#endif
          }
        }
      }
#if CFG_DEBUG
      std::cerr << "  step5.2\n";
#endif
      // Note that we don't need any special-casing of params, since we assume the implicit
      // sets have been instrumented with InstrumentExplicitSets anyhow

      // We computed the interferences between sets. Use that to compute it between local
      // indexes. TODO: a flat matrix?
      for (auto& pair : setInterferences.data) {
        auto* a = pair.first;
        auto& bs = pair.second;
        for (auto* b : bs) {
          indexInterferences[a->index].insert(b->index);
          indexInterferences[b->index].insert(a->index);
        }
      }
#if CFG_DEBUG
      std::cerr << "  step5.3\n";
#endif

      // Used zero inits interfere with params; this avoids us seeing a param is unused
      // and reusing that for a zero init (that could work, but we'd need an explicit zero
      // init, wasting space). There is no problem with them interfering with other zero
      // inits, of course.
      // First, find the zero inits - we ran InstrumentExplicitSets so there are explicit
      // sets for them now.
      auto* entry = parent.entry;
      auto* func = parent.getFunction();
      assert(entry->actions.size() >= func->getNumLocals());
      for (Index i = func->getNumParams(); i < func->getNumLocals(); i++) {
        auto* set = entry->actions[i].getSet();
        assert(set && set->index == i);
        if (!setGets.getGetsFor(set).empty()) {
          for (Index j = 0; j < func->getNumParams(); j++) {
            indexInterferences[i].insert(j);
            indexInterferences[j].insert(i);
          }
        }
      }
#if CFG_DEBUG
      std::cerr << "  step5.4\n";
#endif
    }

    std::map<Index, std::set<Index>> indexInterferences;

  private:
    SymmetricRelation<SetLocal*> setInterferences;
  };

  void applyIndices(std::vector<Index>& indices, Expression* root, GetSets& getSets, SetGets& setGets);

  Copies copies;
  Interferences interferences;
};

void CoalesceLocals::doWalkFunction(Function* func) {
#if CFG_DEBUG
  std::cerr << "CoalesceLocals: " << func->name << '\n';
#endif
  numLocals = func->getNumLocals();
  InstrumentExplicitSets instrumenter(func, getModule());
#if CFG_DEBUG
  std::cerr << " step1\n";
#endif
  super::doWalkFunction(func);
#if CFG_DEBUG
  std::cerr << " step2\n";
#endif
  copies.compute(*this);
#if CFG_DEBUG
  std::cerr << " step3\n";
#endif
  GetSets getSets(*this);
#if CFG_DEBUG
  std::cerr << " step4\n";
#endif
  SetGets setGets(getSets);
#if CFG_DEBUG
  std::cerr << " step5\n";
#endif
  interferences.compute(*this, getSets, setGets);
#if CFG_DEBUG
  std::cerr << " step6\n";
#endif
  // pick new indices
  std::vector<Index> indices;
  pickIndices(indices);
#if CFG_DEBUG
  std::cerr << " step7\n";
#endif
  // apply indices
  applyIndices(indices, func->body, getSets, setGets);
}

// Indices decision making

void CoalesceLocals::pickIndicesFromOrder(std::vector<Index>& order, std::vector<Index>& indices) {
  Index removedCopies;
  pickIndicesFromOrder(order, indices, removedCopies);
}

void CoalesceLocals::pickIndicesFromOrder(std::vector<Index>& order, std::vector<Index>& indices, Index& removedCopies) {
  // mostly-simple greedy coloring
#if CFG_DEBUG
  std::cerr << "\npickIndicesFromOrder on " << getFunction()->name << '\n';
  std::cerr << "order:\n";
  for (auto i : order) std::cerr << i << ' ';
  std::cerr << '\n';
  std::cerr << "interferences:\n";
  for (Index i = 0; i < numLocals; i++) {
    std::cerr << i << ": ";
    for (auto j : interferences.indexInterferences[i]) {
      std::cerr << j << ' ';
    }
    std::cerr << '\n';
  }
  std::cerr << "copies:\n";
  for (Index i = 0; i < numLocals; i++) {
    std::cerr << i << ": ";
    for (Index j = 0; j < numLocals; j++) {
      auto c = copies.getCopies(i, j);
      if (c > 0) {
        std::cerr << j << ':' << c << ' ';
      }
    }
    std::cerr << '\n';
  }
  std::cerr << "total copies:\n";
  for (Index i = 0; i < numLocals; i++) {
    std::cerr << " $" << i << ": " << copies.getTotalCopies()[i] << '\n';
  }
#endif
  // TODO: take into account distribution (99-1 is better than 50-50 with two registers, for gzip)
  std::vector<Type> types;
  std::vector<bool> newInterferences; // new index * numLocals => list of all interferences of locals merged to it
  std::vector<uint8_t> newCopies; // new index * numLocals => list of all copies of locals merged to it
  indices.resize(numLocals);
  types.resize(numLocals);
  newInterferences.resize(numLocals * numLocals);
  std::fill(newInterferences.begin(), newInterferences.end(), false);
  auto numParams = getFunction()->getNumParams();
  newCopies.resize(numParams * numLocals); // start with enough room for the params
  std::fill(newCopies.begin(), newCopies.end(), 0);
  Index nextFree = 0;
  removedCopies = 0;
  // we can't reorder parameters, they are fixed in order, and cannot coalesce
  Index i = 0;
  for (; i < numParams; i++) {
    assert(order[i] == i); // order must leave the params in place
    indices[i] = i;
    types[i] = getFunction()->getLocalType(i);
    for (Index j = numParams; j < numLocals; j++) {
      newInterferences[numLocals * i + j] = interferences.indexInterferences[i].count(j);
      newCopies[numLocals * i + j] = copies.getCopies(i, j);
    }
    nextFree++;
  }
  for (; i < numLocals; i++) {
    Index actual = order[i];
    Index found = -1;
    uint8_t foundCopies = -1;
    for (Index j = 0; j < nextFree; j++) {
      if (!newInterferences[j * numLocals + actual] && getFunction()->getLocalType(actual) == types[j]) {
        // this does not interfere, so it might be what we want. but pick the one eliminating the most copies
        // (we could stop looking forward when there are no more items that have copies anyhow, but it doesn't seem to help)
        auto currCopies = newCopies[j * numLocals + actual];
        if (found == Index(-1) || currCopies > foundCopies) {
          indices[actual] = found = j;
          foundCopies = currCopies;
        }
      }
    }
    if (found == Index(-1)) {
      indices[actual] = found = nextFree;
      types[found] = getFunction()->getLocalType(actual);
      nextFree++;
      removedCopies += copies.getCopies(found, actual);
      newCopies.resize(nextFree * numLocals);
    } else {
      removedCopies += foundCopies;
    }
#if CFG_DEBUG
    std::cerr << "set local $" << actual << " to $" << found << '\n';
#endif
    // merge new interferences and copies for the new index
    for (Index k = i + 1; k < numLocals; k++) {
      auto j = order[k]; // go in the order, we only need to update for those we will see later
      newInterferences[found * numLocals + j] = newInterferences[found * numLocals + j] | interferences.indexInterferences[actual].count(j);
      newCopies[found * numLocals + j] += copies.getCopies(actual, j);
    }
  }
}

// given a baseline order, adjust it based on an important order of priorities (higher values
// are higher priority). The priorities take precedence, unless they are equal and then
// the original order should be kept.
std::vector<Index> adjustOrderByPriorities(std::vector<Index>& baseline, std::vector<Index>& priorities) {
  std::vector<Index> ret = baseline;
  std::vector<Index> reversed = makeReversed(baseline);
  std::sort(ret.begin(), ret.end(), [&priorities, &reversed](Index x, Index y) {
    return priorities[x] > priorities[y] || (priorities[x] == priorities[y] && reversed[x] < reversed[y]);
  });
  return ret;
};

void CoalesceLocals::pickIndices(std::vector<Index>& indices) {
  if (numLocals == 0) return;
  if (numLocals == 1) {
    indices.push_back(0);
    return;
  }
  // take into account total copies. but we must keep params in place, so give them max priority
  auto adjustedTotalCopies = copies.getTotalCopies();
  auto numParams = getFunction()->getNumParams();
  for (Index i = 0; i < numParams; i++) {
    adjustedTotalCopies[i] = std::numeric_limits<Index>::max();
  }
  // first try the natural order. this is less arbitrary than it seems, as the program
  // may have a natural order of locals inherent in it.
  auto order = makeIdentity(numLocals);
  order = adjustOrderByPriorities(order, adjustedTotalCopies);
  Index removedCopies;
  pickIndicesFromOrder(order, indices, removedCopies);
  auto maxIndex = *std::max_element(indices.begin(), indices.end());
  // next try the reverse order. this both gives us another chance at something good,
  // and also the very naturalness of the simple order may be quite suboptimal
  setIdentity(order);
  for (Index i = numParams; i < numLocals; i++) {
    order[i] = numParams + numLocals - 1 - i;
  }
  order = adjustOrderByPriorities(order, adjustedTotalCopies);
  std::vector<Index> reverseIndices;
  Index reverseRemovedCopies;
  pickIndicesFromOrder(order, reverseIndices, reverseRemovedCopies);
  auto reverseMaxIndex = *std::max_element(reverseIndices.begin(), reverseIndices.end());
  // prefer to remove copies foremost, as it matters more for code size (minus gzip), and
  // improves throughput.
  if (reverseRemovedCopies > removedCopies || (reverseRemovedCopies == removedCopies && reverseMaxIndex < maxIndex)) {
    indices.swap(reverseIndices);
  }
}

void CoalesceLocals::applyIndices(std::vector<Index>& indices, Expression* root, GetSets& getSets, SetGets& setGets) {
  assert(indices.size() == numLocals);
  for (auto& curr : basicBlocks) {
    auto& actions = curr->actions;
    for (auto& action : actions) {
      if (action.isGet()) {
        auto* get = (*action.origin)->cast<GetLocal>();
        get->index = indices[get->index];
      } else if (action.isSet()) {
        auto* set = (*action.origin)->cast<SetLocal>();
        set->index = indices[set->index];
        // in addition, we can optimize out redundant copies and ineffective sets
        GetLocal* get;
        if ((get = set->value->dynCast<GetLocal>()) && get->index == set->index) {
          action.removeSet();
          continue;
        }
        // remove unneeded sets
        if (setGets.getGetsFor(set).empty()) {
          action.removeSet();
          continue;
        }
      }
    }
  }
  // update type list
  auto numParams = getFunction()->getNumParams();
  Index newNumLocals = 0;
  for (auto index : indices) {
    newNumLocals = std::max(newNumLocals, index + 1);
  }
  auto oldVars = getFunction()->vars;
  getFunction()->vars.resize(newNumLocals - numParams);
  for (Index index = numParams; index < numLocals; index++) {
    Index newIndex = indices[index];
    if (newIndex >= numParams) {
      getFunction()->vars[newIndex - numParams] = oldVars[index - numParams];
    }
  }
  // names are gone
  getFunction()->localNames.clear();
  getFunction()->localIndices.clear();
}

struct CoalesceLocalsWithLearning : public CoalesceLocals {
  virtual Pass* create() override { return new CoalesceLocalsWithLearning; }

  virtual void pickIndices(std::vector<Index>& indices) override;
};

void CoalesceLocalsWithLearning::pickIndices(std::vector<Index>& indices) {
  if (getFunction()->getNumVars() <= 1) {
    // nothing to think about here
    CoalesceLocals::pickIndices(indices);
    return;
  }

  struct Order : public std::vector<Index> {
    void setFitness(double f) { fitness = f; }
    double getFitness() { return fitness; }
    void dump(std::string text) {
      std::cout << text + ": ( ";
      for (Index i = 0; i < size(); i++) std::cout << (*this)[i] << " ";
      std::cout << ")\n";
      std::cout << "of quality: " << getFitness() << "\n";
    }
  private:
    double fitness;
  };

  struct Generator {
    Generator(CoalesceLocalsWithLearning* parent) : parent(parent), noise(42) {}

    void computeFitness(Order* order) {
      // apply the order
      std::vector<Index> indices; // the phenotype
      Index removedCopies;
      parent->pickIndicesFromOrder(*order, indices, removedCopies);
      auto maxIndex = *std::max_element(indices.begin(), indices.end());
      assert(maxIndex <= parent->numLocals);
      // main part of fitness is the number of locals
      double fitness = parent->numLocals - maxIndex; // higher fitness is better
      // secondarily, it is nice to not reorder locals unnecessarily
      double fragment = 1.0 / (2.0 * parent->numLocals);
      for (Index i = 0; i < parent->numLocals; i++) {
        if ((*order)[i] == i) fitness += fragment; // boost for each that wasn't moved
      }
      fitness = (100 * fitness) + removedCopies; // removing copies is a secondary concern
      order->setFitness(fitness);
    }

    Order* makeRandom() {
      auto* ret = new Order;
      ret->resize(parent->numLocals);
      for (Index i = 0; i < parent->numLocals; i++) {
        (*ret)[i] = i;
      }
      if (first) {
        // as the first guess, use the natural order. this is not arbitrary for two reasons.
        // first, there may be an inherent order in the input (frequent indices are lower,
        // etc.). second, by ensuring we start with the natural order, we ensure we are at
        // least as good as the non-learning variant.
        // TODO: use ::pickIndices from the parent, so we literally get the simpler approach
        //       as our first option
        first = false;
      } else {
        // leave params alone, shuffle the rest
        std::shuffle(ret->begin() + parent->getFunction()->getNumParams(), ret->end(), noise);
      }
      computeFitness(ret);
#ifdef CFG_LEARN_DEBUG
      order->dump("new rando");
#endif
      return ret;
    }

    Order* makeMixture(Order* left, Order* right) {
      // perturb left using right. this is useful since
      // we don't care about absolute locations, relative ones matter more,
      // and a true merge of two vectors could obscure that (e.g.
      // a.......... and ..........a would merge a into the middle, for no
      // reason), and cause a lot of unnecessary noise
      Index size = left->size();
      Order reverseRight; // reverseRight[x] is the index of x in right
      reverseRight.resize(size);
      for (Index i = 0; i < size; i++) {
        reverseRight[(*right)[i]] = i;
      }
      auto* ret = new Order;
      *ret = *left;
      assert(size >= 1);
      for (Index i = parent->getFunction()->getNumParams(); i < size - 1; i++) {
        // if (i, i + 1) is in reverse order in right, flip them
        if (reverseRight[(*ret)[i]] > reverseRight[(*ret)[i + 1]]) {
          std::swap((*ret)[i], (*ret)[i + 1]);
          i++; // if we don't skip, we might end up pushing an element all the way to the end, which is not very perturbation-y
        }
      }
      computeFitness(ret);
#ifdef CFG_LEARN_DEBUG
      ret->dump("new mixture");
#endif
      return ret;
    }

  private:
    CoalesceLocalsWithLearning* parent;
    std::mt19937 noise;
    bool first = true;
  };

#ifdef CFG_LEARN_DEBUG
  std::cout << "[learning for " << getFunction()->name << "]\n";
#endif
  auto numVars = this->getFunction()->getNumVars();
  const int GENERATION_SIZE = std::min(Index(numVars * (numVars - 1)), Index(20));
  Generator generator(this);
  GeneticLearner<Order, double, Generator> learner(generator, GENERATION_SIZE);
#ifdef CFG_LEARN_DEBUG
  learner.getBest()->dump("first best");
#endif
  // keep working while we see improvement
  auto oldBest = learner.getBest()->getFitness();
  while (1) {
    learner.runGeneration();
    auto newBest = learner.getBest()->getFitness();
    if (newBest == oldBest) break; // unlikely we can improve
    oldBest = newBest;
#ifdef CFG_LEARN_DEBUG
    learner.getBest()->dump("current best");
#endif
  }
#ifdef CFG_LEARN_DEBUG
  learner.getBest()->dump("the best");
#endif
  this->pickIndicesFromOrder(*learner.getBest(), indices); // TODO: cache indices in Orders, at the cost of more memory?
}

// declare passes

Pass *createCoalesceLocalsPass() {
  return new CoalesceLocals();
}

Pass *createCoalesceLocalsWithLearningPass() {
  return new CoalesceLocalsWithLearning();
}

} // namespace wasm
