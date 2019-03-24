/*
 * Copyright 2019 WebAssembly Community Group participants
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

#ifndef wasm_support_work_list_h
#define wasm_support_work_list_h

#include <queue>
#include <unordered_map>

namespace wasm {

// A work list of items, where each item should only be handled once, but may
// be attempted to be added more than once.
template<typename T>
struct OneTimeWorkList {
  std::vector<T> work;
  std::set<T> addedToWork;

  void push(T item) {
    if (!addedToWork.count(item)) {
      work.push_back(item);
      addedToWork.insert(item);
    }
  }

  T pop() {
    assert(!empty());
    auto back = work.back();
    work.pop_back();
    return back;
  }

  size_t size() {
    return work.size();
  }

  bool empty() {
    return size() == 0;
  }
};

// A work list of items, where each item may be handled multiple times. This class
// avoids the overhead of having the item more than once in the work at the same time.
template<typename T>
struct WorkList {
  std::set<T> work;

  void push(T item) {
    work.insert(item);
  }

  T pop() {
    assert(!empty());
    auto iter = work.begin();
    auto ret = *iter;
    work.erase(iter);
    return ret;
  }

  size_t size() {
    return work.size();
  }

  bool empty() {
    return size() == 0;
  }
};

} // namespace wasm

#endif // wasm_support_work_list_h
