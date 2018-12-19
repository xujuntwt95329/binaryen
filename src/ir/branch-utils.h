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

#ifndef wasm_ir_branch_h
#define wasm_ir_branch_h

#include "wasm.h"
#include "wasm-traversal.h"
#include "ir/iteration.h"

namespace wasm {

namespace BranchUtils {

inline std::set<Name> getUniqueTargets(Break* br) {
  std::set<Name> ret;
  ret.insert(br->name);
  return ret;
}

inline std::set<Name> getUniqueTargets(Switch* sw) {
  std::set<Name> ret;
  for (auto target : sw->targets) {
    ret.insert(target);
  }
  ret.insert(sw->default_);
  return ret;
}

// If we branch to 'from', change that to 'to' instead.
inline bool replacePossibleTarget(Expression* branch, Name from, Name to) {
  bool worked = false;
  if (auto* br = branch->dynCast<Break>()) {
    if (br->name == from) {
      br->name = to;
      worked = true;
    }
  } else if (auto* sw = branch->dynCast<Switch>()) {
    for (auto& target : sw->targets) {
      if (target == from) {
        target = to;
        worked = true;
      }
    }
    if (sw->default_ == from) {
      sw->default_ = to;
      worked = true;
    }
  } else {
    WASM_UNREACHABLE();
  }
  return worked;
}

// returns the set of targets to which we branch that are
// outside of a node
inline std::set<Name> getExitingBranches(Expression* ast) {
  struct Scanner : public PostWalker<Scanner> {
    std::set<Name> targets;

    void visitBreak(Break* curr) {
      targets.insert(curr->name);
    }
    void visitSwitch(Switch* curr) {
      for (auto target : targets) {
        targets.insert(target);
      }
      targets.insert(curr->default_);
    }
    void visitBlock(Block* curr) {
      if (curr->name.is()) {
        targets.erase(curr->name);
      }
    }
    void visitLoop(Loop* curr) {
      if (curr->name.is()) {
        targets.erase(curr->name);
      }
    }
  };
  Scanner scanner;
  scanner.walk(ast);
  // anything not erased is a branch out
  return scanner.targets;
}

// returns the list of all branch targets in a node

inline std::set<Name> getBranchTargets(Expression* ast) {
  struct Scanner : public PostWalker<Scanner> {
    std::set<Name> targets;

    void visitBlock(Block* curr) {
      if (curr->name.is()) {
        targets.insert(curr->name);
      }
    }
    void visitLoop(Loop* curr) {
      if (curr->name.is()) {
        targets.insert(curr->name);
      }
    }
  };
  Scanner scanner;
  scanner.walk(ast);
  return scanner.targets;
}

// Finds if there are branches targeting a name. Note that since names are
// unique in our IR, we just need to look for the name, and do not need
// to analyze scoping.
// By default we consider all branches, so any place there is a branch that
// names the target. You can unset 'named' to only note branches that appear
// reachable (i.e., are not obviously unreachable).
struct BranchSeeker : public PostWalker<BranchSeeker> {
  Name target;
  bool named = true;

  Index found;
  Type valueType;

  BranchSeeker(Name target) : target(target), found(0) {}

  void noteFound(Expression* value) {
    found++;
    if (found == 1) valueType = none;
    if (!value) valueType = none;
    else if (value->type != none) valueType = value->type;
  }

  void visitBreak(Break *curr) {
    // check the break
    if (curr->name == target) noteFound(curr->value);
  }

  void visitSwitch(Switch *curr) {
    // check the switch
    for (auto name : curr->targets) {
      if (name == target) noteFound(curr->value);
    }
    if (curr->default_ == target) noteFound(curr->value);
  }

  static bool hasReachable(Expression* tree, Name target) {
    if (!target.is()) return false;
    BranchSeeker seeker(target);
    seeker.named = false;
    seeker.walk(tree);
    return seeker.found > 0;
  }

  static Index countReachable(Expression* tree, Name target) {
    if (!target.is()) return 0;
    BranchSeeker seeker(target);
    seeker.named = false;
    seeker.walk(tree);
    return seeker.found;
  }

  static bool hasNamed(Expression* tree, Name target) {
    if (!target.is()) return false;
    BranchSeeker seeker(target);
    seeker.walk(tree);
    return seeker.found > 0;
  }

  static Index countNamed(Expression* tree, Name target) {
    if (!target.is()) return 0;
    BranchSeeker seeker(target);
    seeker.walk(tree);
    return seeker.found;
  }
};

// Check if unreachable code starts in this very node, that is, it stops
// normal control flow and does not flow out.
inline bool startsUnreachableCode(Expression* curr) {
  if (auto* br = curr->dynCast<Break>()) {
    return !br->condition;
  } else if (curr->is<Switch>() ||
             curr->is<Return>() ||
             curr->is<Unreachable>()) {
    return true;
  }
  return false;
}

// Check if control flow can flow out of the given expression. That does not
// include branches out to a higher scope. It roughly corresponds to an expression
// having the "unreachable" type in older Binaryen versions.
inline bool flowsOut(Expression* curr) {
  if (startsUnreachableCode(curr)) {
    return false;
  }
  if (auto* iff = curr->dynCast<If>()) {
    return flowsOut(iff->condition) &&
           (!iff->ifFalse ||
            flowsOut(iff->ifTrue) || flowsOut(iff->ifFalse));
  }
  // Otherwise, see if any children do not flow out.
  for (auto* child : ChildIterator(curr)) {
    if (flowsOut(child)) {
      return false;
    }
  }
  return true;
}

} // namespace BranchUtils

} // namespace wasm

#endif // wasm_ir_branch_h

