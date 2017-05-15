// Copyright 2012 Google Inc. All Rights Reserved.
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

#include "includes_normalize.h"

#include "string_piece.h"
#include "string_piece_util.h"
#include "util.h"

#include <algorithm>
#include <iterator>
#include <sstream>

#include <windows.h>

namespace {

bool IsPathSeparator(char c) {
  return c == '/' ||  c == '\\';
}

// Return true if paths a and b are on the same windows drive.
// Return false if this funcation cannot check
// whether or not on the same windows drive.
bool SameDriveFast(StringPiece a, StringPiece b) {
  if (a.size() < 3 || b.size() < 3) {
    return false;
  }

  if (!islatinalpha(a[0]) || !islatinalpha(b[0])) {
    return false;
  }

  if (ToLowerASCII(a[0]) != ToLowerASCII(b[0])) {
    return false;
  }

  if (a[1] != ':' || b[1] != ':') {
    return false;
  }

  return IsPathSeparator(a[2]) && IsPathSeparator(b[2]);
}

// Return true if paths a and b are on the same Windows drive.
bool SameDrive(StringPiece a, StringPiece b)  {
  if (SameDriveFast(a, b)) {
    return true;
  }

  char a_absolute[_MAX_PATH];
  char b_absolute[_MAX_PATH];
  GetFullPathName(a.AsString().c_str(), sizeof(a_absolute), a_absolute, NULL);
  GetFullPathName(b.AsString().c_str(), sizeof(b_absolute), b_absolute, NULL);
  char a_drive[_MAX_DIR];
  char b_drive[_MAX_DIR];
  _splitpath(a_absolute, a_drive, NULL, NULL, NULL);
  _splitpath(b_absolute, b_drive, NULL, NULL, NULL);
  return _stricmp(a_drive, b_drive) == 0;
}

// Check path |s| is FullPath style returned by GetFullPathName.
// This ignores difference of path separator.
// This is used not to call very slow GetFullPathName API.
bool IsFullPathName(StringPiece s) {
  if (s.size() < 3 ||
      !islatinalpha(s[0]) ||
      s[1] != ':' ||
      !IsPathSeparator(s[2])) {
    return false;
  }

  // Check "." or ".." is contained in path.
  for (size_t i = 2; i < s.size(); ++i) {
    if (!IsPathSeparator(s[i])) {
      continue;
    }

    // Check ".".
    if (i + 1 < s.size() && s[i+1] == '.' &&
        (i + 2 >= s.size() || IsPathSeparator(s[i+2]))) {
      return false;
    }

    // Check "..".
    if (i + 2 < s.size() && s[i+1] == '.' && s[i+2] == '.' &&
        (i + 3 >= s.size() || IsPathSeparator(s[i+3]))) {
      return false;
    }
  }

  return true;
}

}  // anonymous namespace

IncludesNormalize::IncludesNormalize(const string& relative_to) {
  relative_to_.append(relative_to);
  AbsPath(&relative_to_);
  split_relative_to_ = SplitStringPiece(relative_to_.str(), '/');
}

void IncludesNormalize::AbsPath(SmallPath* s) {
  if (IsFullPathName(s->str())) {
    for (size_t i = 0; i < s->size(); ++i) {
      if ((*s)[i] == '\\') {
        (*s)[i] = '/';
      }
    }
    return;
  }

  char result[_MAX_PATH];
  GetFullPathName(s.AsString().c_str(), sizeof(result), result, NULL);
  for (char* c = result; *c; ++c)
    if (*c == '\\')
      *c = '/';
  s->clear();
  s->append(result);
}

string IncludesNormalize::Relativize(
    SmallPath* path, const vector<StringPiece>& start_list) {
  AbsPath(path);
  vector<StringPiece> path_list = SplitStringPiece(path, '/');
  int i;
  for (i = 0; i < static_cast<int>(min(start_list.size(), path_list.size()));
       ++i) {
    if (!EqualsCaseInsensitiveASCII(start_list[i], path_list[i])) {
      break;
    }
  }

  SmallPath ret;
  for (int j = 0; j < static_cast<int>(start_list.size() - i); ++j) {
    ret.append("..");
    ret.append("/");
  }
  for (int j = i; j < static_cast<int>(path_list.size()); ++j) {
    ret.append(path_list[j]);
    ret.append("/");
  }
  if (ret.empty())
    return ".";
  return string(ret.c_str(), ret.size() - 1);
}

bool IncludesNormalize::Normalize(const string& input,
                                  string* result, string* err) const {
  char copy[_MAX_PATH + 1];
  size_t len = input.size();
  if (len > _MAX_PATH) {
    *err = "path too long";
    return false;
  }
  strncpy(copy, input.c_str(), input.size() + 1);
  uint64_t slash_bits;
  if (!CanonicalizePath(copy, &len, &slash_bits, err))
    return false;
  StringPiece partially_fixed(copy, len);
  SmallPath abs_input;
  abs_input.append(paritally_fixed);

  AbsPath(&abs_input);

  if (!SameDrive(abs_input.str(), relative_to_)) {
    *result = partially_fixed.AsString();
    return true;
  }
  *result = Relativize(&abs_input, split_relative_to_);
  return true;
}
