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
// A set of sorted elements.
//

#ifndef wasm_support_sorted_set_h
#define wasm_support_sorted_set_h

#include <support/sorted_vector.h>

namespace wasm {

template<typename T>
struct SortedSet : public SortedVector<T> {
  SortedSet() = default;

  // Returns whether we inserted.
  bool insert(T x) {
    auto it = std::lower_bound(this->begin(), this->end(), x);
    if (it == this->end()) {
      this->push_back(x);
      return true;
    }
    if (*it == x) {
      return false;
    }
    size_t i = it - this->begin();
    this->resize(this->size() + 1);
    std::move_backward(this->begin() + i, this->begin() + this->size() - 1, this->end());
    (*this)[i] = x;
    return true;
  }

  bool erase(T x) {
    auto it = std::lower_bound(this->begin(), this->end(), x);
    if (it != this->end() && *it == x) {
      std::move(it + 1, this->end(), it);
      this->resize(this->size() - 1);
      return true;
    }
    return false;
  }

  bool has(T x) {
    auto it = std::lower_bound(this->begin(), this->end(), x);
    return it != this->end() && *it == x;
  }
};

} // namespace wasm

#endif // wasm_support_sorted_set_h
