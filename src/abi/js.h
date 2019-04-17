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

#ifndef wasm_abi_abi_h
#define wasm_abi_abi_h

#include "asm_v_wasm.h"
#include "shared-constants.h"
#include "wasm.h"
#include "wasm-builder.h"
#include "ir/function-type-utils.h"

namespace wasm {

namespace ABI {

enum class LegalizationLevel {
  Full = 0,
  Minimal = 1
};

inline std::string getLegalizationPass(LegalizationLevel level) {
  if (level == LegalizationLevel::Full) {
    return "legalize-js-interface";
  } else {
    return "legalize-js-interface-minimally";
  }
}

// Ensures i64 support for passing the high bits to and from JS exists. Use the
// existing support if present, as we must have just a single implementation of
// this.
inline Name ensureI64Support(Module& wasm) {
  if (wasm.getGlobalOrNull(TEMP_RET0)) {
    if (!wasm.getExportOrNull(GET_TEMP_RET0) ||
        !wasm.getExportOrNull(SET_TEMP_RET0)) {
      Fatal() << "partial/confusing JS i64 support - missing exported functions";
    }
    return TEMP_RET0;
  }
  if (wasm.getExportOrNull(GET_TEMP_RET0) ||
      wasm.getExportOrNull(SET_TEMP_RET0)) {
    Fatal() << "partial/confusing JS i64 support - excessive exported functions";
  }

  // Create the support.
  Builder builder(wasm);

  auto highBits = make_unique<Global>();
  highBits->type = i32;
  highBits->name = TEMP_RET0;
  highBits->init = builder.makeConst(Literal(int32_t(0)));
  highBits->mutable_ = true;
  wasm.addGlobal(highBits.release());
  {
    auto get = make_unique<Function>();
    get->name = GET_TEMP_RET0;
    auto* functionType = ensureFunctionType("i", &wasm);
    get->type = functionType->name;
    FunctionTypeUtils::fillFunction(get.get(), functionType);
    get->body = builder.makeGetGlobal(TEMP_RET0, i32);
    wasm.addFunction(std::move(get));
  }
  {
    auto set = make_unique<Function>();
    set->name = SET_TEMP_RET0;
    auto* functionType = ensureFunctionType("vi", &wasm);
    set->type = functionType->name;
    FunctionTypeUtils::fillFunction(set.get(), functionType);
    set->body = builder.makeSetGlobal(TEMP_RET0, builder.makeGetLocal(0, i32));
    wasm.addFunction(std::move(set));
  }

  return TEMP_RET0;
}

} // namespace ABI

} // namespace wasm

#endif // wasm_abi_abi_h
