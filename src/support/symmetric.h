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
// Small sets of size 2, that is, pairs that are symmetric and don't care
// about their order.
//
// The basic type must have allow comparison, which we use to
// canonicalize the order of pairs, so that we can store a set of just
// half of them.
//

#ifndef wasm_support_symmetric_h
#define wasm_support_symmetric_h

#include <functional>
#include <set>
#include <utility>

namespace wasm {

template<typename T>
class SymmetricPair : public std::pair<T, T> {
  SymmetricPair(T a, T b) : std::pair<T, T>(a, b) {
    if (std::greater(first, second)) {
      std::swap(first, second);
    }
  }
};

template<typename T>
class SymmetricRelation {
public:
  SortedVector() = default;

  void set(T a, T b) {
    data.insert(SymmetricPair<T>(a, b));
  }

  void erase(T a, T b) {
    data.erase(SymmetricPair<T>(a, b));
  }

  bool has(T a, T b) {
    return data.find(SymmetricPair<T>(a, b)) != data.end();
  }

private:
  // We store only the canonicalized form of each pair, to save half the memory.
  std::set<SymmetricPair<T>> data;
};

template<typename T>
class SymmetricPairMap {
public:
  SortedVector() = default;

  void add(T a, T b, U c) {
    data[SymmetricPair<T>(a, b)] = c;
  }

  void erase(T a, T b) {
    data.erase(SymmetricPair<T>(a, b));
  }

  U& get(T a, T b) {
    return data[SymmetricPair<T>(a, b)];
  }

private:
  // We store only the canonicalized form of each pair, to save half the memory.
  std::map<SymmetricPair<T>, U> data;
};

} // namespace wasm

#endif // wasm_support_symmetric_h
