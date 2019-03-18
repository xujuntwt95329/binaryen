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
// A vector of sorted elements.
//

#ifndef wasm_support_sorted_vector_h
#define wasm_support_sorted_vector_h

#include <vector>

namespace wasm {

template<typename T>
struct SortedVector : public std::vector<T> {
  SortedVector() = default;

  SortedVector merge(const SortedVector& other) const {
    SortedVector ret;
    ret.resize(this->size() + other.size());
    size_t i = 0, j = 0, t = 0;
    while (i < this->size() && j < other.size()) {
      auto left = (*this)[i];
      auto right = other[j];
      if (left < right) {
        ret[t++] = left;
        i++;
      } else if (left > right) {
        ret[t++] = right;
        j++;
      } else {
        ret[t++] = left;
        i++;
        j++;
      }
    }
    while (i < this->size()) {
      ret[t++] = (*this)[i];
      i++;
    }
    while (j < other.size()) {
      ret[t++] = other[j];
      j++;
    }
    ret.resize(t);
    return ret;
  }

  void insert(T x) {
    auto it = std::lower_bound(this->begin(), this->end(), x);
    if (it == this->end()) this->push_back(x);
    else if (*it > x) {
      size_t i = it - this->begin();
      this->resize(this->size() + 1);
      std::move_backward(this->begin() + i, this->begin() + this->size() - 1, this->end());
      (*this)[i] = x;
    }
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

  template<typename U>
  SortedVector& filter(U keep) {
    size_t skip = 0;
    for (size_t i = 0; i < this->size(); i++) {
      if (keep((*this)[i])) {
        (*this)[i - skip] = (*this)[i];
      } else {
        skip++;
      }
    }
    this->resize(this->size() - skip);
    return *this;
  }

  template<typename U>
  void forEach(U func) {
    for (size_t i = 0; i < this->size(); i++) {
      func((*this)[i]);
    }
  }

  void verify() const {
    for (size_t i = 1; i < this->size(); i++) {
      assert((*this)[i - 1] <= (*this)[i]);
    }
  }

  void dump(const char* str = "SortedVector:") const {
    std::cout << str;
    for (auto x : *this) std::cout << x << " ";
    std::cout << '\n';
  }
};

} // namespace wasm

#endif // wasm_support_sorted_vector_h
