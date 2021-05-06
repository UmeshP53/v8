// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function main() {
  let a = []
  a.length = 4294967295.0;
  function f() {
    a.length = 0;
    return -1
  }
  const len = {valueOf:f};
  a.fill(1.1,0,len);
}

assertThrows(() => main(), RangeError);