// Copyright 2017 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NINJA_SMALL_STRING_H_
#define NINJA_SMALL_STRING_H_

#include "string_piece.h"

#include <algorithm>

template <size_t stack_size>
class SmallString {
  void operator=(const SmallString&);
  SmallString(const SmallString&);

  char stack_buffer_[stack_size];
  char* ptr_;
  size_t len_;
  size_t cap_;

  bool grow(size_t s) {
    if (cap_ >= s) {
      return false;
    }

    size_t next_cap = std::max(cap_ * 2, s);
    char* new_ptr = new char[next_cap];

    memcpy(new_ptr, ptr_, len_ + 1);

    if (!is_small()) {
      delete[] ptr_;
    }

    ptr_ = new_ptr;
    cap_ = next_cap;
    return true;
  }

 public:
  SmallString() : ptr_(stack_buffer_), len_(0), cap_(stack_size) {
    grow(1);
    ptr_[0] = '\0';
  }

  ~SmallString() {
    if (!is_small()) {
      delete[] ptr_;
    }
  }

  void append(StringPiece s) {
    grow(len_ + s.size() + 1);
    memcpy(ptr_ + len_, s.str_, s.len_);
    len_ += s.len_;
    ptr_[len_] = '\0';
  }

  const char* c_str() const { return ptr_; }

  size_t size() const { return len_; }

  size_t cap() const { return cap_; }

  char operator[](size_t pos) const { return ptr_[pos]; }

  StringPiece str() const { return StringPiece(ptr_, len_); }

  bool operator==(const StringPiece& other) const { return str() == other; }

  bool is_small() const { return ptr_ == stack_buffer_; }
};

#endif  // NINJA_SMALL_STRING_H_
