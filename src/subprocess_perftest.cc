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

#include <stdio.h>
#include <stdlib.h>

#include "metrics.h"
#include "subprocess.h"

#ifdef _WIN32
const char* kSimpleCommand = "cmd /c dir \\";
#else
const char* kSimpleCommand = "ls /";
#endif

int main(int argc, char* argv[]) {
  for (int limit = 1 << 10; limit < (1<<20); limit *= 2) {
    SubprocessSet subprocs;
    int64_t start = GetTimeMillis();
    std::vector<Subprocess*> subprocesses;
    for (int rep = 0; rep < limit; ++rep) {
      subprocesses.push_back(subprocs.Add(kSimpleCommand));
    }

    while (true) {
      bool end = true;
      
      for (size_t i = 0; i < subprocesses.size(); ++i) {
        if (!subprocesses[i]->Done()) {
          end = false;
          break;
        }
      }

      if (end) {
        break;
      }
      subprocs.DoWork();
    }

    for (size_t i = 0; i < subprocesses.size(); ++i) {
      subprocesses[i]->Finish();
    }

    int64_t end = GetTimeMillis();

    if (end - start > 2000) {
      int delta_ms = (int)(end - start);
      printf("Output of `%s`: %s", kSimpleCommand, subprocesses[0]->GetOutput().c_str());
      printf("Parse %d times in %dms avg %.1fus\n",
             limit, delta_ms, float(delta_ms * 1000) / limit);
      break;
    }
  }

  return 0;
}
