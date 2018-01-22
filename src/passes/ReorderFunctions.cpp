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
// Sorts functions by their static use count. This helps reduce the size of wasm
// binaries because fewer bytes are needed to encode references to frequently
// used functions.
//
// Secondarily, sorts by similarity, to keep similar functions close together,
// which can help with gzip size.
//


#include <memory>

#include <wasm.h>
#include <pass.h>
#include <wasm-binary.h>

namespace wasm {

typedef std::unordered_map<Name, std::atomic<Index>> NameCountMap;

struct CallCountScanner : public WalkerPass<PostWalker<CallCountScanner>> {
  bool isFunctionParallel() override { return true; }

  CallCountScanner(NameCountMap* counts) : counts(counts) {}

  CallCountScanner* create() override {
    return new CallCountScanner(counts);
  }

  void visitCall(Call* curr) {
    assert(counts->count(curr->target) > 0); // can't add a new element in parallel
    (*counts)[curr->target]++;
  }

private:
  NameCountMap* counts;
};

struct ReorderFunctions : public Pass {
  void run(PassRunner* runner, Module* module) override {
    // note original indexes, to break ties
    std::unordered_map<Name, Index> originalIndexes;
    auto numFunctions = module->functions.size();
    for (Index i = 0; i < numFunctions; i++) {
      originalIndexes[module->functions[i]->name] = i;
    }
    NameCountMap counts;
    // fill in info, as we operate on it in parallel (each function to its own entry)
    for (auto& func : module->functions) {
      counts[func->name];
    }
    // find counts on function calls
    {
      PassRunner runner(module);
      runner.setIsNested(true);
      runner.add<CallCountScanner>(&counts);
      runner.run();
    }
    // find counts on global usages
    if (module->start.is()) {
      counts[module->start]++;
    }
    for (auto& curr : module->exports) {
      counts[curr->value]++;
    }
    for (auto& segment : module->table.segments) {
      for (auto& curr : segment.data) {
        counts[curr]++;
      }
    }
    // sort by uses, break ties with original order
    std::sort(module->functions.begin(), module->functions.end(), [&counts, &originalIndexes](
      const std::unique_ptr<Function>& a,
      const std::unique_ptr<Function>& b) -> bool {
      if (counts[a->name] == counts[b->name]) {
        return originalIndexes[a->name] < originalIndexes[b->name];
      }
      return counts[a->name] > counts[b->name];
    });
    // secondarily, sort by similarity, but without changing LEB sizes
    // write out the binary so we can see function contents
    BufferWithRandomAccess buffer;
    WasmBinaryWriter writer(module, buffer);
    writer.write();
    // get a profile of each function, which we can then use to compare
    std::unordered_map<Name, Index> profiles;
    for (Index i = 0; i < numFunctions; i++) {
      auto& info = writer.tableOfContents.functionBodies[i];
      profiles[module->functions[i]->name] = Profile(&buffer[info.offset], info.size);
    }
    // sort in chunks: LEB uses 7 bits for data per 8, so functions with
    // index 0-127 take one bytes, and so forth. sort within each such chunk
    size_t start = 0;
    size_t end = 128; // not inclusive
    while (start < numFunctions) {
      end = std::min(end, numFunctions);
      // how sort fast and not N^2?
      // move on to next chunk
      start = end;
      end *= 128;
    }
  }

  // represents a profile of binary data, suitable for making fuzzy comparisons
  // of similarity
  struct Profile {
    Profile(char* data, size_t size) {
...
    }
  };
};

Pass *createReorderFunctionsPass() {
  return new ReorderFunctions();
}

} // namespace wasm
