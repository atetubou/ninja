// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "depfile_parser.h"

// A note on backslashes in Makefiles, from reading the docs:
// Backslash-newline is the line continuation character.
// Backslash-# escapes a # (otherwise meaningful as a comment start).
// Backslash-% escapes a % (otherwise meaningful as a special).
// Finally, quoting the GNU manual, "Backslashes that are not in danger
// of quoting ‘%’ characters go unmolested."
// How do you end a line with a backslash?  The netbsd Make docs suggest
// reading the result of a shell command echoing a backslash!
//
// Rather than implement all of above, we do a simpler thing here:
// Backslashes escape a set of characters (see "escapes" defined below),
// otherwise they are passed through verbatim.
// If anyone actually has depfiles that rely on the more complicated
// behavior we can adjust this.
bool DepfileParser::Parse(string* content, string* err) {
  // in: current parser input point.
  // end: end of input.
  char* in = &(*content)[0];
  char* end = in + content->size();
  while (in < end) {
    // out: current output point (typically same as in, but can fall behind
    // as we de-escape backslashes).
    char* out = in;
    // filename: start of the current parsed filename.
    char* filename = out;
    for (;;) {
      // start: beginning of the current parsed span.
      const char* start = in;
      char yych;
      /*!re2c
      re2c:define:YYCTYPE = "char";
      re2c:define:YYCURSOR = in;
      re2c:define:YYLIMIT = end;

      re2c:yyfill:enable = 0;

      re2c:indent:top = 2;
      re2c:indent:string = "  ";

      re2c:yych:emit = 0;

      nul = "\000";
      escape = [ \\#*$[|];

      '\\' escape {
        // De-escape backslashed character.
        *out++ = yych;
        continue;
      }
      '\\'[^\000\n] {
        // Let backslash before other characters through verbatim.
        *out++ = '\\';
        *out++ = yych;
        continue;
      }
      [a-zA-Z0-9+,/_:.-]+ {
        // Got a span of plain text.  Copy it to out if necessary.
        int len = in - start;
        if (out < start)
          memmove(out, start, len);
        out += len;
        continue;
      }
      nul {
        break;
      }
      [^] {
        // For any other character (e.g. whitespace), swallow it here,
        // allowing the outer logic to loop around again.
        break;
      }
      */
    }

    int len = out - filename;
    if (len > 0 && filename[len - 1] == ':')
      len--;  // Strip off trailing colon, if any.

    if (len == 0)
      continue;

    if (!out_.str_) {
      out_ = StringPiece(filename, len);
    } else {
      ins_.push_back(StringPiece(filename, len));
    }
  }
  return true;
}