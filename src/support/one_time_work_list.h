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

//
// A work list of items, where each item should only be handled once, but may
// be attempted to be added more than once.
//

#ifndef wasm_support_one_time_work_list_h
#define wasm_support_one_time_work_list_h

#include <queue>
#include <unordered_map>

namespace wasm {

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

} // namespace wasm

#endif // wasm_support_one_time_work_list_h
