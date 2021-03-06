/*
 * Copyright (C) 2017 The Android Open Source Project
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

class SubA extends Super {
  int getValue() { return 42; }
}

class SubB extends Super {
  int getValue() { return 38; }
}

class SubD extends Super {
  int getValue() { return 10; }
}

class SubE extends Super {
  int getValue() { return -4; }
}

public class Main {

  /// CHECK-START: int Main.inlineMonomorphicSubA(Super) inliner (before)
  /// CHECK:       InvokeVirtual method_name:Super.getValue

  /// CHECK-START: int Main.inlineMonomorphicSubA(Super) inliner (after)
  /// CHECK-NOT:   InvokeVirtual method_name:Super.getValue

  /// CHECK-START: int Main.inlineMonomorphicSubA(Super) inliner (after)
  /// CHECK:  <<SubARet:i\d+>>      IntConstant 42
  /// CHECK:  <<ObjClass:l\d+>>     InstanceFieldGet field_name:java.lang.Object.shadow$_klass_
  /// CHECK:  <<InlineClass:l\d+>>  LoadClass class_name:SubA
  /// CHECK:  <<Test:z\d+>>         NotEqual [<<InlineClass>>,<<ObjClass>>]
  /// CHECK:                        Deoptimize [<<Test>>]
  /// CHECK:                        Return [<<SubARet>>]
  public static int inlineMonomorphicSubA(Super a) {
    return a.getValue();
  }

  /// CHECK-START: int Main.inlinePolymophicSubASubB(Super) inliner (before)
  /// CHECK:       InvokeVirtual method_name:Super.getValue

  /// CHECK-START: int Main.inlinePolymophicSubASubB(Super) inliner (after)
  /// CHECK-NOT:   InvokeVirtual method_name:Super.getValue

  // Note that the order in which the types are added to the inline cache in the profile matters.

  /// CHECK-START: int Main.inlinePolymophicSubASubB(Super) inliner (after)
  /// CHECK-DAG:  <<SubARet:i\d+>>          IntConstant 42
  /// CHECK-DAG:  <<SubBRet:i\d+>>          IntConstant 38
  /// CHECK:      <<ObjClassSubA:l\d+>>     InstanceFieldGet field_name:java.lang.Object.shadow$_klass_
  /// CHECK:      <<InlineClassSubA:l\d+>>  LoadClass class_name:SubA
  /// CHECK:      <<TestSubA:z\d+>>         NotEqual [<<InlineClassSubA>>,<<ObjClassSubA>>]
  /// CHECK:                                If [<<TestSubA>>]

  /// CHECK:      <<ObjClassSubB:l\d+>>     InstanceFieldGet field_name:java.lang.Object.shadow$_klass_
  /// CHECK:      <<InlineClassSubB:l\d+>>  LoadClass class_name:SubB
  /// CHECK:      <<TestSubB:z\d+>>         NotEqual [<<InlineClassSubB>>,<<ObjClassSubB>>]
  /// CHECK:                                Deoptimize [<<TestSubB>>]

  /// CHECK:      <<Ret:i\d+>>              Phi [<<SubARet>>,<<SubBRet>>]
  /// CHECK:                                Return [<<Ret>>]
  public static int inlinePolymophicSubASubB(Super a) {
    return a.getValue();
  }

  /// CHECK-START: int Main.inlinePolymophicCrossDexSubASubC(Super) inliner (before)
  /// CHECK:       InvokeVirtual method_name:Super.getValue

  /// CHECK-START: int Main.inlinePolymophicCrossDexSubASubC(Super) inliner (after)
  /// CHECK-NOT:   InvokeVirtual method_name:Super.getValue

  // Note that the order in which the types are added to the inline cache in the profile matters.

  /// CHECK-START: int Main.inlinePolymophicCrossDexSubASubC(Super) inliner (after)
  /// CHECK-DAG:  <<SubARet:i\d+>>          IntConstant 42
  /// CHECK-DAG:  <<SubCRet:i\d+>>          IntConstant 24
  /// CHECK:      <<ObjClassSubA:l\d+>>     InstanceFieldGet field_name:java.lang.Object.shadow$_klass_
  /// CHECK:      <<InlineClassSubA:l\d+>>  LoadClass class_name:SubA
  /// CHECK:      <<TestSubA:z\d+>>         NotEqual [<<InlineClassSubA>>,<<ObjClassSubA>>]
  /// CHECK:                                If [<<TestSubA>>]

  /// CHECK:      <<ObjClassSubC:l\d+>>     InstanceFieldGet field_name:java.lang.Object.shadow$_klass_
  /// CHECK:      <<InlineClassSubC:l\d+>>  LoadClass class_name:SubC
  /// CHECK:      <<TestSubC:z\d+>>         NotEqual [<<InlineClassSubC>>,<<ObjClassSubC>>]
  /// CHECK:                                Deoptimize [<<TestSubC>>]

  /// CHECK:      <<Ret:i\d+>>              Phi [<<SubARet>>,<<SubCRet>>]
  /// CHECK:                                Return [<<Ret>>]
  public static int inlinePolymophicCrossDexSubASubC(Super a) {
    return a.getValue();
  }

  /// CHECK-START: int Main.inlineMegamorphic(Super) inliner (before)
  /// CHECK:       InvokeVirtual method_name:Super.getValue

  /// CHECK-START: int Main.inlineMegamorphic(Super) inliner (after)
  /// CHECK:       InvokeVirtual method_name:Super.getValue
  public static int inlineMegamorphic(Super a) {
    return a.getValue();
  }

  /// CHECK-START: int Main.inlineMissingTypes(Super) inliner (before)
  /// CHECK:       InvokeVirtual method_name:Super.getValue

  /// CHECK-START: int Main.inlineMissingTypes(Super) inliner (after)
  /// CHECK:       InvokeVirtual method_name:Super.getValue
  public static int inlineMissingTypes(Super a) {
    return a.getValue();
  }

  /// CHECK-START: int Main.noInlineCache(Super) inliner (before)
  /// CHECK:       InvokeVirtual method_name:Super.getValue

  /// CHECK-START: int Main.noInlineCache(Super) inliner (after)
  /// CHECK:       InvokeVirtual method_name:Super.getValue
  public static int noInlineCache(Super a) {
    return a.getValue();
  }

  public static void testInlineMonomorphic() {
    if (inlineMonomorphicSubA(new SubA()) != 42) {
      throw new Error("Expected 42");
    }

    // Call with a different type than the one from the inline cache.
    if (inlineMonomorphicSubA(new SubB()) != 38) {
      throw new Error("Expected 38");
    }
  }

  public static void testInlinePolymorhic() {
    if (inlinePolymophicSubASubB(new SubA()) != 42) {
      throw new Error("Expected 42");
    }

    if (inlinePolymophicSubASubB(new SubB()) != 38) {
      throw new Error("Expected 38");
    }

    // Call with a different type than the one from the inline cache.
    if (inlinePolymophicSubASubB(new SubC()) != 24) {
      throw new Error("Expected 25");
    }

    if (inlinePolymophicCrossDexSubASubC(new SubA()) != 42) {
      throw new Error("Expected 42");
    }

    if (inlinePolymophicCrossDexSubASubC(new SubC()) != 24) {
      throw new Error("Expected 24");
    }

    // Call with a different type than the one from the inline cache.
    if (inlinePolymophicCrossDexSubASubC(new SubB()) != 38) {
      throw new Error("Expected 38");
    }
  }

  public static void testInlineMegamorphic() {
    if (inlineMegamorphic(new SubA()) != 42) {
      throw new Error("Expected 42");
    }
  }


  public static void testNoInlineCache() {
    if (noInlineCache(new SubA()) != 42) {
      throw new Error("Expected 42");
    }
  }

  public static void main(String[] args) {
    testInlineMonomorphic();
    testInlinePolymorhic();
    testInlineMegamorphic();
    testNoInlineCache();
  }

}
