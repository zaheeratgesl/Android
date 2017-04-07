/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

class Transform {
  public void sayHi(Runnable r) {
    // Use lower 'h' to make sure the string will have a different string id
    // than the transformation (the transformation code is the same except
    // the actual printed String, which was making the test inacurately passing
    // in JIT mode when loading the string from the dex cache, as the string ids
    // of the two different strings were the same).
    // We know the string ids will be different because lexicographically:
    // "Hello" < "LTransform;" < "hello".
    System.out.println("hello");
    r.run();
    System.out.println("goodbye");
  }
}
