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

#include "ir/branch-utils.h"
#include "ir/find_all.h"
#include "ir/utils.h"

namespace wasm {

static Type getValueType(Expression* value) {
  return value ? value->type : none;
}

void ReFinalize::visitBlock(Block* curr) {
  if (curr->list.size() == 0) {
    curr->type = none;
    return;
  }
  curr->type = curr->list.back()->type;
}
void ReFinalize::visitIf(If* curr) { curr->finalize(); }
void ReFinalize::visitLoop(Loop* curr) { curr->finalize(); }
void ReFinalize::visitBreak(Break* curr) {
  curr->finalize();
  auto valueType = getValueType(curr->value);
  updateBreakValueType(curr->name, valueType);
}
void ReFinalize::visitSwitch(Switch* curr) {
  curr->finalize();
  auto valueType = getValueType(curr->value);
  for (auto target : curr->targets) {
    updateBreakValueType(target, valueType);
  }
  updateBreakValueType(curr->default_, valueType);
}
void ReFinalize::visitCall(Call* curr) { curr->finalize(); }
void ReFinalize::visitCallIndirect(CallIndirect* curr) { curr->finalize(); }
void ReFinalize::visitGetLocal(GetLocal* curr) { curr->finalize(); }
void ReFinalize::visitSetLocal(SetLocal* curr) { curr->finalize(); }
void ReFinalize::visitGetGlobal(GetGlobal* curr) { curr->finalize(); }
void ReFinalize::visitSetGlobal(SetGlobal* curr) { curr->finalize(); }
void ReFinalize::visitLoad(Load* curr) { curr->finalize(); }
void ReFinalize::visitStore(Store* curr) { curr->finalize(); }
void ReFinalize::visitAtomicRMW(AtomicRMW* curr) { curr->finalize(); }
void ReFinalize::visitAtomicCmpxchg(AtomicCmpxchg* curr) { curr->finalize(); }
void ReFinalize::visitAtomicWait(AtomicWait* curr) { curr->finalize(); }
void ReFinalize::visitAtomicWake(AtomicWake* curr) { curr->finalize(); }
void ReFinalize::visitSIMDExtract(SIMDExtract* curr) { curr->finalize(); }
void ReFinalize::visitSIMDReplace(SIMDReplace* curr) { curr->finalize(); }
void ReFinalize::visitSIMDShuffle(SIMDShuffle* curr) { curr->finalize(); }
void ReFinalize::visitSIMDBitselect(SIMDBitselect* curr) { curr->finalize(); }
void ReFinalize::visitSIMDShift(SIMDShift* curr) { curr->finalize(); }
void ReFinalize::visitConst(Const* curr) { curr->finalize(); }
void ReFinalize::visitUnary(Unary* curr) { curr->finalize(); }
void ReFinalize::visitBinary(Binary* curr) { curr->finalize(); }
void ReFinalize::visitSelect(Select* curr) { curr->finalize(); }
void ReFinalize::visitDrop(Drop* curr) { curr->finalize(); }
void ReFinalize::visitReturn(Return* curr) { curr->finalize(); }
void ReFinalize::visitHost(Host* curr) { curr->finalize(); }
void ReFinalize::visitNop(Nop* curr) { curr->finalize(); }
void ReFinalize::visitUnreachable(Unreachable* curr) { curr->finalize(); }

void ReFinalize::visitFunction(Function* curr) {
}

void ReFinalize::visitFunctionType(FunctionType* curr) { WASM_UNREACHABLE(); }
void ReFinalize::visitExport(Export* curr) { WASM_UNREACHABLE(); }
void ReFinalize::visitGlobal(Global* curr) { WASM_UNREACHABLE(); }
void ReFinalize::visitTable(Table* curr) { WASM_UNREACHABLE(); }
void ReFinalize::visitMemory(Memory* curr) { WASM_UNREACHABLE(); }
void ReFinalize::visitModule(Module* curr) { WASM_UNREACHABLE(); }

void ReFinalize::updateBreakValueType(Name name, Type type) {
  if (type != none || breakValues.count(name) == 0) {
    breakValues[name] = type;
  }
}

} // namespace wasm
