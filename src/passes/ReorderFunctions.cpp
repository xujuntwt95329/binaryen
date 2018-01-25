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
#include <ir/module-utils.h>
#include <support/hash.h>

namespace wasm {

struct ReorderFunctions : public Pass {
  enum {
    // we allow more then 256 hashes so that we look not just at individual bytes, but also larger windows
    MAX_HASHES_PER_PROFILE = 768
  };

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

  void run(PassRunner* runner, Module* module) override {
    auto& functions = module->functions;
    auto numFunctions = functions.size();
    // we can't move imports, but need to know how many there are
    ModuleUtils::BinaryIndexes indexes(*module);
    ssize_t firstNonImportedFunctionIndex = indexes.firstNonImportedFunctionIndex;
    // calculate the ranges within which the LEB size is the same
    std::vector<std::pair<Index, Index>> ranges;
    {
      // LEB uses 7 bits for data per 8, so functions with
      // index 0-127 take one bytes, and so forth. sort within each such chunk
      ssize_t absoluteStart = 0;
      ssize_t absoluteEnd = 128; // not inclusive
      while (1) {
        size_t start = std::max(absoluteStart - firstNonImportedFunctionIndex, ssize_t(0));
        size_t end = std::max(absoluteEnd - firstNonImportedFunctionIndex, ssize_t(0));
        end = std::min(end, numFunctions);
        if (start >= numFunctions) break;
        ranges.emplace_back(start, end);
        absoluteStart = absoluteEnd;
        absoluteEnd *= 128;
      }
    }
    // note original indexes, to break ties
    std::unordered_map<Name, Index> originalIndexes;
    for (Index i = 0; i < numFunctions; i++) {
      originalIndexes[functions[i]->name] = i;
    }
    // find use counts
    NameCountMap counts;
    // fill in info, as we operate on it in parallel (each function to its own entry)
    for (auto& func : functions) {
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
    // sort them all, to find which range each belongs to
    {
      std::vector<Name> sorted;
      for (auto& func : functions) {
        sorted.push_back(func->name);
      }
      // sort by uses, break ties with original order
      std::sort(sorted.begin(), sorted.end(), [&counts, &originalIndexes](
        const Name a,
        const Name b) -> bool {
        if (counts[a] == counts[b]) {
          return originalIndexes[a] < originalIndexes[b];
        }
        return counts[a] > counts[b];
      });
      // note the proper range for each one
      std::unordered_map<Name, Index> properRange;
      for (Index rangeIndex = 0; rangeIndex < ranges.size(); rangeIndex++) {
        auto& range = ranges[rangeIndex];
        auto start = range.first;
        auto end = range.second;
        for (auto i = start; i < end; i++) {
          properRange[sorted[i]] = rangeIndex;
        }
      }
      // sort into ranges, keeping original sort within range
      std::sort(functions.begin(), functions.end(), [&originalIndexes, &properRange](
        const std::unique_ptr<Function>& a,
        const std::unique_ptr<Function>& b) -> bool {
        if (properRange[a->name] == properRange[b->name]) {
          return originalIndexes[a->name] < originalIndexes[b->name];
        }
        return properRange[a->name] < properRange[b->name];
      });
    }
// 0 is the old way
// 1 is fast similarity checks
// 2 is do all the hard work
if (getenv("MODE")[0] == '0') return;
    // don't sort into chunks of this size: we sort the entire
    // list and then assume chunks of this size are ok to leave
    // as-is, which is generally true (transitivity) and much more
    // efficient
    // FIXME: really this should be function body sizes?
    //        or similarity measures?
    size_t SIMILARITY_SORT_CHUNK_SIZE;
double SIMILAR_SIMILARITY;
if (getenv("MODE")[0] == '1') {
  SIMILARITY_SORT_CHUNK_SIZE = 100;
  SIMILAR_SIMILARITY = 0.25;
} else {
  SIMILARITY_SORT_CHUNK_SIZE = 1; // the most work
  SIMILAR_SIMILARITY = 0.05;
}

    // secondarily, sort by similarity, but without changing LEB sizes
    // write out the binary so we can see function contents
    BufferWithRandomAccess buffer;
    WasmBinaryWriter writer(module, buffer);
    writer.write();
    // get a profile of each function, which we can then use to compare
    // TODO: parallelize
    std::unordered_map<Name, Profile> profiles;
    for (Index i = 0; i < numFunctions; i++) {
      auto& info = writer.tableOfContents.functionBodies[i];
//std::cout << "profile " << i << " / " << numFunctions << '\n';
      profiles[functions[i]->name] = Profile(&buffer[info.offset], info.size);
    }
    // work within each range where the LEB size is identical, don't cross them
    for (auto& range : ranges) {
      auto start = range.first;
      auto end = range.second;
//std::cout << "work from " << start << " to " << end << " / " << numFunctions << '\n';
      // process the elements from start to end in chunks. each time we sort
      // the whole thing, then leave the first sorted chunk, and continue.
      // TODO: this is still N^2 even if we did lower the constant factor a lot
      while (start < end) {
//std::cout << "piece of work from " << start << " to " << end << " / " << numFunctions << '\n';
        // we sort all the functions compared to a baseline: the previous
        // element if there is one, or the first
        auto baseline = start == 0 ? 0 : start - 1;
        auto& baselineProfile = profiles[functions[baseline]->name];
        std::unordered_map<Name, double> distances; // to the baseline
        for (auto i = start; i < end; i++) {
          auto name = functions[i]->name;
          distances[name] = baselineProfile.distance(profiles[name]);
        }
        std::sort(functions.begin() + start, functions.begin() + end, [&distances, &originalIndexes](
          const std::unique_ptr<Function>& a,
          const std::unique_ptr<Function>& b) -> bool {
          if (distances[a->name] == distances[b->name]) {
            return originalIndexes[a->name] < originalIndexes[b->name];
          }
          return distances[a->name] < distances[b->name];
        });
//std::cout << "distance: " << distances[functions[start]->name] << "\n";
// now that they are sorted, we can assume the first chunk are all similar
// to the baseline, and so also to themselves) and we can just leave them,
// and continue on to the next chunk
        // the first is now in the right place
        start++;
        // keep going while the distance to the rest is fairly small, by
        // the triangle inequality they are similar to each other too
        while (start < end && distances[functions[start]->name] < SIMILAR_SIMILARITY) {
          start++;
        }
if (0) SIMILARITY_SORT_CHUNK_SIZE = SIMILARITY_SORT_CHUNK_SIZE;
        //start += SIMILARITY_SORT_CHUNK_SIZE;
      }
    }
  }

  // represents a profile of binary data, suitable for making fuzzy comparisons
  // of similarity
  struct Profile {
    // the profile maps hashes of seen values or combinations with the amount of appearances of them
    typedef std::unordered_map<uint32_t, ssize_t> HashCounts;
    HashCounts hashCounts;
    size_t total = 0;

    Profile() {}
    Profile(unsigned char* data, size_t size) {
      // very simple algorithm, just use sliding windows of sizes 1, 2, and 4
      total = 0;
      uint32_t curr = 0;
      for (size_t i = 0; i < size; i++) {
        curr = (curr << 8) | *data;
        data++;
        hashCounts[hash(curr & 0xff)] += 2;
        total += 2;
        if (i > 0) {
          hashCounts[hash(curr & 0xffff)] += 1; // this line is necessary for non-gzip size to be ok. something is wrong
          total += 1;
        }
        // TODO: also 4?
      }
      // trim: ignore the long tail, leave just the popular ones
      if (hashCounts.size() > MAX_HASHES_PER_PROFILE) {
        std::vector<size_t> keys;
        for (auto& pair : hashCounts) {
          keys.push_back(pair.first);
        }
        std::sort(keys.begin(), keys.end(), [this](const size_t a, const size_t b) -> bool {
          ssize_t diff = hashCounts[a] - hashCounts[b];
          if (diff == 0) {
            return a < b;
          }
          return diff > 0;
        });
        HashCounts trimmed;
        total = 0;
        for (size_t i = 0; i < MAX_HASHES_PER_PROFILE; i++) {
          auto key = keys[i];
          auto value = hashCounts[key];
          trimmed[key] = value;
          total += value;
        }
        trimmed.swap(hashCounts);
      }
    }

    size_t hash(uint32_t x) {
      return rehash(x, 0);
    }

    double distance(const Profile& other) {
      size_t sum = 0;
      for (auto& pair : hashCounts) {
        auto value = pair.first;
        auto iter = other.hashCounts.find(value);
        if (iter != other.hashCounts.end()) {
          sum += std::abs(pair.second - iter->second);
        } else {
          sum += pair.second;
        }
      }
      for (auto& pair : other.hashCounts) {
        auto value = pair.first;
        auto iter = hashCounts.find(value);
        if (iter == hashCounts.end()) {
          sum += pair.second;
        }
      }
      auto normalized = double(sum) / (total + other.total);
      assert(normalized >= 0 && normalized <= 1);
      return normalized;
    }
  };
};

Pass *createReorderFunctionsPass() {
  return new ReorderFunctions();
}

} // namespace wasm
