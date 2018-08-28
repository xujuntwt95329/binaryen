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
// Instruments the build with code to intercept all memory reads and writes.
// This can be useful in building tools that analyze memory access behaviour.
//
// The instrumentation is performed by calling FFI both for the pointers,
// and for the values. Each call also has an ID, to allow easy finding in
// the wasm. The instrumentation functions must return the proper values,
// as for simplicity and compactness we expect values to fall through them,
// specifically the pointer calls must return the address, and the value
// calls must return the value. An example should make this clear:
//
// Loads: load-ptr(id, bytes, offset, address) => address
//        load-i32(id, value) => value
//        load-f32, f64, etc.
//
//  Before:
//   (i32.load8_s align=1 offset=2 (i32.const 3))
//
//  After:
//   (call $load-i32
//    (i32.const n) // ID
//    (i32.load8_s align=1 offset=2
//     (call $load-ptr
//      (i32.const n) // ID
//      (i32.const 1) // bytes
//      (i32.const 2) // offset
//      (i32.const 3) // address
//     )
//    )
//   )
//
// Stores: store(id, bytes, offset, address) => address
//         store-i32(id, value) => value
//         store-f32, f64, etc.
//
//  Before:
//   (i32.store8 align=1 offset=2 (i32.const 3) (i32.const 4))
//
//  After:
//   (i32.store16 align=1 offset=2
//    (call $store-ptr
//     (i32.const n) // ID
//     (i32.const 1) // bytes
//     (i32.const 2) // offset
//     (i32.const 3) // address
//    )
//    (call $store-i32
//     (i32.const n) // ID
//     (i32.const 4)
//    )
//   )
//
// The JS loading code must provide the imports, for example,
// to alert on operation you might use this:
//
//  var importsForTheWasm = {
//    // other stuff here, like 'env' if you need it, etc.
//    'instrument': {
//      'load-ptr': function(id, bytes, offset, address) {
//        alert(['load', id, bytes, offset, address]);
//        return address;
//      },
//      'store-ptr': function(id, bytes, offset, address) {
//        alert(['store', id, bytes, offset, address]);
//        return address;
//      },
// XXX add
//    }
//  };
//

#include <wasm.h>
#include <wasm-builder.h>
#include <pass.h>
#include "shared-constants.h"
#include "asmjs/shared-constants.h"
#include "asm_v_wasm.h"

namespace wasm {

Name load("load");
Name store("store");
// TODO: Add support for atomicRMW/cmpxchg

struct InstrumentMemory : public WalkerPass<PostWalker<InstrumentMemory>> {
  void visitLoad(Load* curr) {
    makeLoadCall(curr);
  }
  void visitStore(Store* curr) {
    makeStoreCall(curr);
  }
  void addImport(Module *curr, Name name, std::string sig) {
    auto import = new Import;
    import->name = name;
    import->module = INSTRUMENT;
    import->base = name;
    import->functionType = ensureFunctionType(sig, curr)->name;
    import->kind = ExternalKind::Function;
    curr->addImport(import);
  }

  void visitModule(Module *curr) {
    addImport(curr, load,  "iiiii");
    addImport(curr, store,  "iiiii");
  }

private:
  std::atomic<Index> id; // TODO: this is ready for parallelization, but would
                         //       prevent deterministic output

  Expression* makeLoadCall(Load* curr) {
    Builder builder(*getModule());
    curr->ptr = builder.makeCallImport(load,
      { builder.makeConst(Literal(int32_t(id.fetch_add(1)))),
        builder.makeConst(Literal(int32_t(curr->bytes))),
        builder.makeConst(Literal(int32_t(curr->offset.addr))),
        curr->ptr},
      i32
    );
    return curr;
  }

  Expression* makeStoreCall(Store* curr) {
    Builder builder(*getModule());
    curr->ptr = builder.makeCallImport(store,
      { builder.makeConst(Literal(int32_t(id.fetch_add(1)))),
        builder.makeConst(Literal(int32_t(curr->bytes))),
        builder.makeConst(Literal(int32_t(curr->offset.addr))),
        curr->ptr },
      i32
    );
    return curr;
  }
};

Pass *createInstrumentMemoryPass() {
  return new InstrumentMemory();
}

} // namespace wasm
