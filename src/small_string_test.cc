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

#include "small_string.h"

#include "test.h"

TEST(SmallStringTest, Grow) {
  {
    SmallString<0> len0;
    EXPECT_FALSE(len0.is_small());
    EXPECT_EQ(len0.size(), 0);
    EXPECT_EQ(len0, "");

    len0.append("a");
    EXPECT_EQ(len0.size(), 1);
    EXPECT_EQ(len0, "a");
  }

  {
    SmallString<1> len1;
    EXPECT_TRUE(len1.is_small());
    EXPECT_EQ(len1.size(), 0);
    EXPECT_EQ(len1, "");

    len1.append("a");
    EXPECT_FALSE(len1.is_small());
    EXPECT_EQ(len1.size(), 1);
    EXPECT_EQ(len1, "a");
  }

  {
    SmallString<128> len128;
    EXPECT_TRUE(len128.is_small());
    EXPECT_EQ(len128.size(), 0);
    EXPECT_EQ(len128, "");

    len128.append("a");
    EXPECT_TRUE(len128.is_small());
    EXPECT_EQ(len128.size(), 1);
    EXPECT_EQ(len128, "a");
  }
}
