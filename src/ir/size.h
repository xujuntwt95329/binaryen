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

#ifndef wasm_ir_size_h
#define wasm_ir_size_h

#include <wasm.h>
#include <wasm-binary.h>
#include <wasm-traversal.h>
#include <ir/features.h>
#include <ir/iteration.h>

namespace wasm {

// Estimate the binary size of an AST. This is an *estimate*, since the
// final binary size depends on the LEB sizes of things that depend on other
// things in the binary. The estimate here is a lower estimate, that is,
// it assumes LEBs and other variable things are at their minimal size. We
// also make assumptions like unnamed blocks not being emitted in the binary
// (which is possible in stacky code).

struct SizeAnalyzer : public Visitor<SizeAnalyzer, Index> {
  SizeAnalyzer(Expression *ast) {
    size = visitRecursively(ast);
  }

  Index size;

  // Calculate the actual written some of something.
  template<typename T>
  static Index getWrittenSize(const T& thing) {
    BufferWithRandomAccess buffer;
    buffer << thing;
    return buffer.size();
  }

  // Get the binary written size of a literal. This is smaller than a Const
  // node, which would also have an opcode for the type.
  static Index getLiteralSize(Literal value) {
    switch (value.type) {
      case i32: {
        // TODO: if this is slow, we could just estimate
        return getWrittenSize(S32LEB(value.geti32()));
      }
      case i64: {
        return getWrittenSize(S64LEB(value.geti64()));
      }
      case f32:
      case f64:
      case v128: {
        return getTypeSize(value.type);
      }
      default: {
        WASM_UNREACHABLE();
      }
    }
  }

  Index visitRecursively(Expression* curr) {
    Index ret = visit(curr);
    // Child nodes simply add to the parent size.
    for (auto* child : ChildIterator(curr)) {
      ret += visitRecursively(child);
    }
    return ret;
  }

  Index maybeVisit(Expression* curr) {
    return curr ? visit(curr) : 0;
  }

  Index visitBlock(Block* curr) {
    // Without a name, blocks do not need to be emitted at all, since it is valid in
    // stack wasm code to just emit sequences. (With a name, we have a block start,
    // a type, and a block end.)
    return curr->name.is() ? 3 : 0;
  }
  Index visitIf(If* curr) {
    return curr->ifFalse ? 4 : 3;
  }
  Index visitLoop(Loop* curr) {
    return 3;
  }
  Index visitBreak(Break* curr) {
    // Assume the index LEB32 is of minimal size.
    return 2;
  }
  Index visitSwitch(Switch* curr) {
    // Assume the break LEB32s are of minimal size.
    // TODO: compute the LEB size for the # of targets
    return 3 + curr->targets.size();
  }
  Index visitCall(Call* curr) {
    // Assume the index LEB32 is of minimal size.
    return 2;
  }
  Index visitCallIndirect(CallIndirect* curr) {
    // Assume the index LEB32 is of minimal size.
    return 3;
  }
  Index visitGetLocal(GetLocal* curr) {
    // Assume the index LEB32 is of minimal size.
    return 2;
  }
  Index visitSetLocal(SetLocal* curr) {
    // Assume the index LEB32 is of minimal size.
    return 2;
  }
  Index visitGetGlobal(GetGlobal* curr) {
    // Assume the index LEB32 is of minimal size.
    return 2;
  }
  Index visitSetGlobal(SetGlobal* curr) {
    // Assume the index LEB32 is of minimal size.
    return 2;
  }
  Index visitLoad(Load* curr) {
    // Assume the LEB32s are of minimal size.
    return curr->isAtomic ? 4 : 3;
  }
  Index visitStore(Store* curr) {
    // Assume the LEB32s are of minimal size.
    return curr->isAtomic ? 4 : 3;
  }
  Index visitAtomicRMW(AtomicRMW* curr) {
    // Assume the LEB32s are of minimal size.
    return 4;
  }
  Index visitAtomicCmpxchg(AtomicCmpxchg* curr) {
    // Assume the LEB32s are of minimal size.
    return 4;
  }
  Index visitConst(Const* curr) {
    return 1 + getLiteralSize(curr->value) + (isVectorType(curr->type) ? 1 : 0);
  }
  Index visitUnary(Unary* curr) {
    // Post-MVP ops are all prefixed.
    return 1 + (Features::get(curr->op).isMVP() ? 0 : 1);
  }
  Index visitBinary(Binary* curr) {
    // Post-MVP ops are all prefixed.
    return 1 + (Features::get(curr->op).isMVP() ? 0 : 1);
  }
  Index visitSelect(Select* curr) {
    return 1;
  }
  Index visitDrop(Drop* curr) {
    return 1;
  }
  Index visitReturn(Return* curr) {
    return 1;
  }
  Index visitHost(Host* curr) {
    return 2;
  }
  Index visitNop(Nop* curr) {
    return 1;
  }
  Index visitUnreachable(Unreachable* curr) {
    return 1;
  }
};

} // namespace wasm

#endif // wasm_ir_size_h
