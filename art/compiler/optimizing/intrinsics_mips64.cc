/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "intrinsics_mips64.h"

#include "arch/mips64/instruction_set_features_mips64.h"
#include "art_method.h"
#include "code_generator_mips64.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "intrinsics.h"
#include "mirror/array-inl.h"
#include "mirror/string.h"
#include "thread.h"
#include "utils/mips64/assembler_mips64.h"
#include "utils/mips64/constants_mips64.h"

namespace art {

namespace mips64 {

IntrinsicLocationsBuilderMIPS64::IntrinsicLocationsBuilderMIPS64(CodeGeneratorMIPS64* codegen)
  : arena_(codegen->GetGraph()->GetArena()) {
}

Mips64Assembler* IntrinsicCodeGeneratorMIPS64::GetAssembler() {
  return reinterpret_cast<Mips64Assembler*>(codegen_->GetAssembler());
}

ArenaAllocator* IntrinsicCodeGeneratorMIPS64::GetAllocator() {
  return codegen_->GetGraph()->GetArena();
}

#define __ codegen->GetAssembler()->

static void MoveFromReturnRegister(Location trg,
                                   Primitive::Type type,
                                   CodeGeneratorMIPS64* codegen) {
  if (!trg.IsValid()) {
    DCHECK_EQ(type, Primitive::kPrimVoid);
    return;
  }

  DCHECK_NE(type, Primitive::kPrimVoid);

  if (Primitive::IsIntegralType(type) || type == Primitive::kPrimNot) {
    GpuRegister trg_reg = trg.AsRegister<GpuRegister>();
    if (trg_reg != V0) {
      __ Move(V0, trg_reg);
    }
  } else {
    FpuRegister trg_reg = trg.AsFpuRegister<FpuRegister>();
    if (trg_reg != F0) {
      if (type == Primitive::kPrimFloat) {
        __ MovS(F0, trg_reg);
      } else {
        __ MovD(F0, trg_reg);
      }
    }
  }
}

static void MoveArguments(HInvoke* invoke, CodeGeneratorMIPS64* codegen) {
  InvokeDexCallingConventionVisitorMIPS64 calling_convention_visitor;
  IntrinsicVisitor::MoveArguments(invoke, codegen, &calling_convention_visitor);
}

// Slow-path for fallback (calling the managed code to handle the
// intrinsic) in an intrinsified call. This will copy the arguments
// into the positions for a regular call.
//
// Note: The actual parameters are required to be in the locations
//       given by the invoke's location summary. If an intrinsic
//       modifies those locations before a slowpath call, they must be
//       restored!
class IntrinsicSlowPathMIPS64 : public SlowPathCodeMIPS64 {
 public:
  explicit IntrinsicSlowPathMIPS64(HInvoke* invoke)
     : SlowPathCodeMIPS64(invoke), invoke_(invoke) { }

  void EmitNativeCode(CodeGenerator* codegen_in) OVERRIDE {
    CodeGeneratorMIPS64* codegen = down_cast<CodeGeneratorMIPS64*>(codegen_in);

    __ Bind(GetEntryLabel());

    SaveLiveRegisters(codegen, invoke_->GetLocations());

    MoveArguments(invoke_, codegen);

    if (invoke_->IsInvokeStaticOrDirect()) {
      codegen->GenerateStaticOrDirectCall(invoke_->AsInvokeStaticOrDirect(),
                                          Location::RegisterLocation(A0));
    } else {
      codegen->GenerateVirtualCall(invoke_->AsInvokeVirtual(), Location::RegisterLocation(A0));
    }
    codegen->RecordPcInfo(invoke_, invoke_->GetDexPc(), this);

    // Copy the result back to the expected output.
    Location out = invoke_->GetLocations()->Out();
    if (out.IsValid()) {
      DCHECK(out.IsRegister());  // TODO: Replace this when we support output in memory.
      DCHECK(!invoke_->GetLocations()->GetLiveRegisters()->ContainsCoreRegister(out.reg()));
      MoveFromReturnRegister(out, invoke_->GetType(), codegen);
    }

    RestoreLiveRegisters(codegen, invoke_->GetLocations());
    __ Bc(GetExitLabel());
  }

  const char* GetDescription() const OVERRIDE { return "IntrinsicSlowPathMIPS64"; }

 private:
  // The instruction where this slow path is happening.
  HInvoke* const invoke_;

  DISALLOW_COPY_AND_ASSIGN(IntrinsicSlowPathMIPS64);
};

#undef __

bool IntrinsicLocationsBuilderMIPS64::TryDispatch(HInvoke* invoke) {
  Dispatch(invoke);
  LocationSummary* res = invoke->GetLocations();
  return res != nullptr && res->Intrinsified();
}

#define __ assembler->

static void CreateFPToIntLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresRegister());
}

static void MoveFPToInt(LocationSummary* locations, bool is64bit, Mips64Assembler* assembler) {
  FpuRegister in  = locations->InAt(0).AsFpuRegister<FpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  if (is64bit) {
    __ Dmfc1(out, in);
  } else {
    __ Mfc1(out, in);
  }
}

// long java.lang.Double.doubleToRawLongBits(double)
void IntrinsicLocationsBuilderMIPS64::VisitDoubleDoubleToRawLongBits(HInvoke* invoke) {
  CreateFPToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitDoubleDoubleToRawLongBits(HInvoke* invoke) {
  MoveFPToInt(invoke->GetLocations(), /* is64bit */ true, GetAssembler());
}

// int java.lang.Float.floatToRawIntBits(float)
void IntrinsicLocationsBuilderMIPS64::VisitFloatFloatToRawIntBits(HInvoke* invoke) {
  CreateFPToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitFloatFloatToRawIntBits(HInvoke* invoke) {
  MoveFPToInt(invoke->GetLocations(), /* is64bit */ false, GetAssembler());
}

static void CreateIntToFPLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresFpuRegister());
}

static void MoveIntToFP(LocationSummary* locations, bool is64bit, Mips64Assembler* assembler) {
  GpuRegister in  = locations->InAt(0).AsRegister<GpuRegister>();
  FpuRegister out = locations->Out().AsFpuRegister<FpuRegister>();

  if (is64bit) {
    __ Dmtc1(in, out);
  } else {
    __ Mtc1(in, out);
  }
}

// double java.lang.Double.longBitsToDouble(long)
void IntrinsicLocationsBuilderMIPS64::VisitDoubleLongBitsToDouble(HInvoke* invoke) {
  CreateIntToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitDoubleLongBitsToDouble(HInvoke* invoke) {
  MoveIntToFP(invoke->GetLocations(), /* is64bit */ true, GetAssembler());
}

// float java.lang.Float.intBitsToFloat(int)
void IntrinsicLocationsBuilderMIPS64::VisitFloatIntBitsToFloat(HInvoke* invoke) {
  CreateIntToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitFloatIntBitsToFloat(HInvoke* invoke) {
  MoveIntToFP(invoke->GetLocations(), /* is64bit */ false, GetAssembler());
}

static void CreateIntToIntLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

static void GenReverseBytes(LocationSummary* locations,
                            Primitive::Type type,
                            Mips64Assembler* assembler) {
  GpuRegister in  = locations->InAt(0).AsRegister<GpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  switch (type) {
    case Primitive::kPrimShort:
      __ Dsbh(out, in);
      __ Seh(out, out);
      break;
    case Primitive::kPrimInt:
      __ Rotr(out, in, 16);
      __ Wsbh(out, out);
      break;
    case Primitive::kPrimLong:
      __ Dsbh(out, in);
      __ Dshd(out, out);
      break;
    default:
      LOG(FATAL) << "Unexpected size for reverse-bytes: " << type;
      UNREACHABLE();
  }
}

// int java.lang.Integer.reverseBytes(int)
void IntrinsicLocationsBuilderMIPS64::VisitIntegerReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitIntegerReverseBytes(HInvoke* invoke) {
  GenReverseBytes(invoke->GetLocations(), Primitive::kPrimInt, GetAssembler());
}

// long java.lang.Long.reverseBytes(long)
void IntrinsicLocationsBuilderMIPS64::VisitLongReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitLongReverseBytes(HInvoke* invoke) {
  GenReverseBytes(invoke->GetLocations(), Primitive::kPrimLong, GetAssembler());
}

// short java.lang.Short.reverseBytes(short)
void IntrinsicLocationsBuilderMIPS64::VisitShortReverseBytes(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitShortReverseBytes(HInvoke* invoke) {
  GenReverseBytes(invoke->GetLocations(), Primitive::kPrimShort, GetAssembler());
}

static void GenNumberOfLeadingZeroes(LocationSummary* locations,
                                     bool is64bit,
                                     Mips64Assembler* assembler) {
  GpuRegister in  = locations->InAt(0).AsRegister<GpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  if (is64bit) {
    __ Dclz(out, in);
  } else {
    __ Clz(out, in);
  }
}

// int java.lang.Integer.numberOfLeadingZeros(int i)
void IntrinsicLocationsBuilderMIPS64::VisitIntegerNumberOfLeadingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitIntegerNumberOfLeadingZeros(HInvoke* invoke) {
  GenNumberOfLeadingZeroes(invoke->GetLocations(), /* is64bit */ false, GetAssembler());
}

// int java.lang.Long.numberOfLeadingZeros(long i)
void IntrinsicLocationsBuilderMIPS64::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitLongNumberOfLeadingZeros(HInvoke* invoke) {
  GenNumberOfLeadingZeroes(invoke->GetLocations(), /* is64bit */ true, GetAssembler());
}

static void GenNumberOfTrailingZeroes(LocationSummary* locations,
                                      bool is64bit,
                                      Mips64Assembler* assembler) {
  Location in = locations->InAt(0);
  Location out = locations->Out();

  if (is64bit) {
    __ Dsbh(out.AsRegister<GpuRegister>(), in.AsRegister<GpuRegister>());
    __ Dshd(out.AsRegister<GpuRegister>(), out.AsRegister<GpuRegister>());
    __ Dbitswap(out.AsRegister<GpuRegister>(), out.AsRegister<GpuRegister>());
    __ Dclz(out.AsRegister<GpuRegister>(), out.AsRegister<GpuRegister>());
  } else {
    __ Rotr(out.AsRegister<GpuRegister>(), in.AsRegister<GpuRegister>(), 16);
    __ Wsbh(out.AsRegister<GpuRegister>(), out.AsRegister<GpuRegister>());
    __ Bitswap(out.AsRegister<GpuRegister>(), out.AsRegister<GpuRegister>());
    __ Clz(out.AsRegister<GpuRegister>(), out.AsRegister<GpuRegister>());
  }
}

// int java.lang.Integer.numberOfTrailingZeros(int i)
void IntrinsicLocationsBuilderMIPS64::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitIntegerNumberOfTrailingZeros(HInvoke* invoke) {
  GenNumberOfTrailingZeroes(invoke->GetLocations(), /* is64bit */ false, GetAssembler());
}

// int java.lang.Long.numberOfTrailingZeros(long i)
void IntrinsicLocationsBuilderMIPS64::VisitLongNumberOfTrailingZeros(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitLongNumberOfTrailingZeros(HInvoke* invoke) {
  GenNumberOfTrailingZeroes(invoke->GetLocations(), /* is64bit */ true, GetAssembler());
}

static void GenReverse(LocationSummary* locations,
                       Primitive::Type type,
                       Mips64Assembler* assembler) {
  DCHECK(type == Primitive::kPrimInt || type == Primitive::kPrimLong);

  GpuRegister in  = locations->InAt(0).AsRegister<GpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  if (type == Primitive::kPrimInt) {
    __ Rotr(out, in, 16);
    __ Wsbh(out, out);
    __ Bitswap(out, out);
  } else {
    __ Dsbh(out, in);
    __ Dshd(out, out);
    __ Dbitswap(out, out);
  }
}

// int java.lang.Integer.reverse(int)
void IntrinsicLocationsBuilderMIPS64::VisitIntegerReverse(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitIntegerReverse(HInvoke* invoke) {
  GenReverse(invoke->GetLocations(), Primitive::kPrimInt, GetAssembler());
}

// long java.lang.Long.reverse(long)
void IntrinsicLocationsBuilderMIPS64::VisitLongReverse(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitLongReverse(HInvoke* invoke) {
  GenReverse(invoke->GetLocations(), Primitive::kPrimLong, GetAssembler());
}

static void CreateFPToFPLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
}

static void GenBitCount(LocationSummary* locations,
                        const Primitive::Type type,
                        Mips64Assembler* assembler) {
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();
  GpuRegister in = locations->InAt(0).AsRegister<GpuRegister>();

  DCHECK(type == Primitive::kPrimInt || type == Primitive::kPrimLong);

  // https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
  //
  // A generalization of the best bit counting method to integers of
  // bit-widths up to 128 (parameterized by type T) is this:
  //
  // v = v - ((v >> 1) & (T)~(T)0/3);                           // temp
  // v = (v & (T)~(T)0/15*3) + ((v >> 2) & (T)~(T)0/15*3);      // temp
  // v = (v + (v >> 4)) & (T)~(T)0/255*15;                      // temp
  // c = (T)(v * ((T)~(T)0/255)) >> (sizeof(T) - 1) * BITS_PER_BYTE; // count
  //
  // For comparison, for 32-bit quantities, this algorithm can be executed
  // using 20 MIPS instructions (the calls to LoadConst32() generate two
  // machine instructions each for the values being used in this algorithm).
  // A(n unrolled) loop-based algorithm requires 25 instructions.
  //
  // For a 64-bit operand this can be performed in 24 instructions compared
  // to a(n unrolled) loop based algorithm which requires 38 instructions.
  //
  // There are algorithms which are faster in the cases where very few
  // bits are set but the algorithm here attempts to minimize the total
  // number of instructions executed even when a large number of bits
  // are set.

  if (type == Primitive::kPrimInt) {
    __ Srl(TMP, in, 1);
    __ LoadConst32(AT, 0x55555555);
    __ And(TMP, TMP, AT);
    __ Subu(TMP, in, TMP);
    __ LoadConst32(AT, 0x33333333);
    __ And(out, TMP, AT);
    __ Srl(TMP, TMP, 2);
    __ And(TMP, TMP, AT);
    __ Addu(TMP, out, TMP);
    __ Srl(out, TMP, 4);
    __ Addu(out, out, TMP);
    __ LoadConst32(AT, 0x0F0F0F0F);
    __ And(out, out, AT);
    __ LoadConst32(TMP, 0x01010101);
    __ MulR6(out, out, TMP);
    __ Srl(out, out, 24);
  } else if (type == Primitive::kPrimLong) {
    __ Dsrl(TMP, in, 1);
    __ LoadConst64(AT, 0x5555555555555555L);
    __ And(TMP, TMP, AT);
    __ Dsubu(TMP, in, TMP);
    __ LoadConst64(AT, 0x3333333333333333L);
    __ And(out, TMP, AT);
    __ Dsrl(TMP, TMP, 2);
    __ And(TMP, TMP, AT);
    __ Daddu(TMP, out, TMP);
    __ Dsrl(out, TMP, 4);
    __ Daddu(out, out, TMP);
    __ LoadConst64(AT, 0x0F0F0F0F0F0F0F0FL);
    __ And(out, out, AT);
    __ LoadConst64(TMP, 0x0101010101010101L);
    __ Dmul(out, out, TMP);
    __ Dsrl32(out, out, 24);
  }
}

// int java.lang.Integer.bitCount(int)
void IntrinsicLocationsBuilderMIPS64::VisitIntegerBitCount(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitIntegerBitCount(HInvoke* invoke) {
  GenBitCount(invoke->GetLocations(), Primitive::kPrimInt, GetAssembler());
}

// int java.lang.Long.bitCount(long)
void IntrinsicLocationsBuilderMIPS64::VisitLongBitCount(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitLongBitCount(HInvoke* invoke) {
  GenBitCount(invoke->GetLocations(), Primitive::kPrimLong, GetAssembler());
}

static void MathAbsFP(LocationSummary* locations, bool is64bit, Mips64Assembler* assembler) {
  FpuRegister in = locations->InAt(0).AsFpuRegister<FpuRegister>();
  FpuRegister out = locations->Out().AsFpuRegister<FpuRegister>();

  if (is64bit) {
    __ AbsD(out, in);
  } else {
    __ AbsS(out, in);
  }
}

// double java.lang.Math.abs(double)
void IntrinsicLocationsBuilderMIPS64::VisitMathAbsDouble(HInvoke* invoke) {
  CreateFPToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathAbsDouble(HInvoke* invoke) {
  MathAbsFP(invoke->GetLocations(), /* is64bit */ true, GetAssembler());
}

// float java.lang.Math.abs(float)
void IntrinsicLocationsBuilderMIPS64::VisitMathAbsFloat(HInvoke* invoke) {
  CreateFPToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathAbsFloat(HInvoke* invoke) {
  MathAbsFP(invoke->GetLocations(), /* is64bit */ false, GetAssembler());
}

static void CreateIntToInt(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

static void GenAbsInteger(LocationSummary* locations, bool is64bit, Mips64Assembler* assembler) {
  GpuRegister in  = locations->InAt(0).AsRegister<GpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  if (is64bit) {
    __ Dsra32(AT, in, 31);
    __ Xor(out, in, AT);
    __ Dsubu(out, out, AT);
  } else {
    __ Sra(AT, in, 31);
    __ Xor(out, in, AT);
    __ Subu(out, out, AT);
  }
}

// int java.lang.Math.abs(int)
void IntrinsicLocationsBuilderMIPS64::VisitMathAbsInt(HInvoke* invoke) {
  CreateIntToInt(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathAbsInt(HInvoke* invoke) {
  GenAbsInteger(invoke->GetLocations(), /* is64bit */ false, GetAssembler());
}

// long java.lang.Math.abs(long)
void IntrinsicLocationsBuilderMIPS64::VisitMathAbsLong(HInvoke* invoke) {
  CreateIntToInt(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathAbsLong(HInvoke* invoke) {
  GenAbsInteger(invoke->GetLocations(), /* is64bit */ true, GetAssembler());
}

static void GenMinMaxFP(LocationSummary* locations,
                        bool is_min,
                        Primitive::Type type,
                        Mips64Assembler* assembler) {
  FpuRegister a = locations->InAt(0).AsFpuRegister<FpuRegister>();
  FpuRegister b = locations->InAt(1).AsFpuRegister<FpuRegister>();
  FpuRegister out = locations->Out().AsFpuRegister<FpuRegister>();

  Mips64Label noNaNs;
  Mips64Label done;
  FpuRegister ftmp = ((out != a) && (out != b)) ? out : FTMP;

  // When Java computes min/max it prefers a NaN to a number; the
  // behavior of MIPSR6 is to prefer numbers to NaNs, i.e., if one of
  // the inputs is a NaN and the other is a valid number, the MIPS
  // instruction will return the number; Java wants the NaN value
  // returned. This is why there is extra logic preceding the use of
  // the MIPS min.fmt/max.fmt instructions. If either a, or b holds a
  // NaN, return the NaN, otherwise return the min/max.
  if (type == Primitive::kPrimDouble) {
    __ CmpUnD(FTMP, a, b);
    __ Bc1eqz(FTMP, &noNaNs);

    // One of the inputs is a NaN
    __ CmpEqD(ftmp, a, a);
    // If a == a then b is the NaN, otherwise a is the NaN.
    __ SelD(ftmp, a, b);

    if (ftmp != out) {
      __ MovD(out, ftmp);
    }

    __ Bc(&done);

    __ Bind(&noNaNs);

    if (is_min) {
      __ MinD(out, a, b);
    } else {
      __ MaxD(out, a, b);
    }
  } else {
    DCHECK_EQ(type, Primitive::kPrimFloat);
    __ CmpUnS(FTMP, a, b);
    __ Bc1eqz(FTMP, &noNaNs);

    // One of the inputs is a NaN
    __ CmpEqS(ftmp, a, a);
    // If a == a then b is the NaN, otherwise a is the NaN.
    __ SelS(ftmp, a, b);

    if (ftmp != out) {
      __ MovS(out, ftmp);
    }

    __ Bc(&done);

    __ Bind(&noNaNs);

    if (is_min) {
      __ MinS(out, a, b);
    } else {
      __ MaxS(out, a, b);
    }
  }

  __ Bind(&done);
}

static void CreateFPFPToFPLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetInAt(1, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister(), Location::kNoOutputOverlap);
}

// double java.lang.Math.min(double, double)
void IntrinsicLocationsBuilderMIPS64::VisitMathMinDoubleDouble(HInvoke* invoke) {
  CreateFPFPToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathMinDoubleDouble(HInvoke* invoke) {
  GenMinMaxFP(invoke->GetLocations(), /* is_min */ true, Primitive::kPrimDouble, GetAssembler());
}

// float java.lang.Math.min(float, float)
void IntrinsicLocationsBuilderMIPS64::VisitMathMinFloatFloat(HInvoke* invoke) {
  CreateFPFPToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathMinFloatFloat(HInvoke* invoke) {
  GenMinMaxFP(invoke->GetLocations(), /* is_min */ true, Primitive::kPrimFloat, GetAssembler());
}

// double java.lang.Math.max(double, double)
void IntrinsicLocationsBuilderMIPS64::VisitMathMaxDoubleDouble(HInvoke* invoke) {
  CreateFPFPToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathMaxDoubleDouble(HInvoke* invoke) {
  GenMinMaxFP(invoke->GetLocations(), /* is_min */ false, Primitive::kPrimDouble, GetAssembler());
}

// float java.lang.Math.max(float, float)
void IntrinsicLocationsBuilderMIPS64::VisitMathMaxFloatFloat(HInvoke* invoke) {
  CreateFPFPToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathMaxFloatFloat(HInvoke* invoke) {
  GenMinMaxFP(invoke->GetLocations(), /* is_min */ false, Primitive::kPrimFloat, GetAssembler());
}

static void GenMinMax(LocationSummary* locations,
                      bool is_min,
                      Mips64Assembler* assembler) {
  GpuRegister lhs = locations->InAt(0).AsRegister<GpuRegister>();
  GpuRegister rhs = locations->InAt(1).AsRegister<GpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  if (lhs == rhs) {
    if (out != lhs) {
      __ Move(out, lhs);
    }
  } else {
    // Some architectures, such as ARM and MIPS (prior to r6), have a
    // conditional move instruction which only changes the target
    // (output) register if the condition is true (MIPS prior to r6 had
    // MOVF, MOVT, and MOVZ). The SELEQZ and SELNEZ instructions always
    // change the target (output) register.  If the condition is true the
    // output register gets the contents of the "rs" register; otherwise,
    // the output register is set to zero. One consequence of this is
    // that to implement something like "rd = c==0 ? rs : rt" MIPS64r6
    // needs to use a pair of SELEQZ/SELNEZ instructions.  After
    // executing this pair of instructions one of the output registers
    // from the pair will necessarily contain zero. Then the code ORs the
    // output registers from the SELEQZ/SELNEZ instructions to get the
    // final result.
    //
    // The initial test to see if the output register is same as the
    // first input register is needed to make sure that value in the
    // first input register isn't clobbered before we've finished
    // computing the output value. The logic in the corresponding else
    // clause performs the same task but makes sure the second input
    // register isn't clobbered in the event that it's the same register
    // as the output register; the else clause also handles the case
    // where the output register is distinct from both the first, and the
    // second input registers.
    if (out == lhs) {
      __ Slt(AT, rhs, lhs);
      if (is_min) {
        __ Seleqz(out, lhs, AT);
        __ Selnez(AT, rhs, AT);
      } else {
        __ Selnez(out, lhs, AT);
        __ Seleqz(AT, rhs, AT);
      }
    } else {
      __ Slt(AT, lhs, rhs);
      if (is_min) {
        __ Seleqz(out, rhs, AT);
        __ Selnez(AT, lhs, AT);
      } else {
        __ Selnez(out, rhs, AT);
        __ Seleqz(AT, lhs, AT);
      }
    }
    __ Or(out, out, AT);
  }
}

static void CreateIntIntToIntLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

// int java.lang.Math.min(int, int)
void IntrinsicLocationsBuilderMIPS64::VisitMathMinIntInt(HInvoke* invoke) {
  CreateIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathMinIntInt(HInvoke* invoke) {
  GenMinMax(invoke->GetLocations(), /* is_min */ true, GetAssembler());
}

// long java.lang.Math.min(long, long)
void IntrinsicLocationsBuilderMIPS64::VisitMathMinLongLong(HInvoke* invoke) {
  CreateIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathMinLongLong(HInvoke* invoke) {
  GenMinMax(invoke->GetLocations(), /* is_min */ true, GetAssembler());
}

// int java.lang.Math.max(int, int)
void IntrinsicLocationsBuilderMIPS64::VisitMathMaxIntInt(HInvoke* invoke) {
  CreateIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathMaxIntInt(HInvoke* invoke) {
  GenMinMax(invoke->GetLocations(), /* is_min */ false, GetAssembler());
}

// long java.lang.Math.max(long, long)
void IntrinsicLocationsBuilderMIPS64::VisitMathMaxLongLong(HInvoke* invoke) {
  CreateIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathMaxLongLong(HInvoke* invoke) {
  GenMinMax(invoke->GetLocations(), /* is_min */ false, GetAssembler());
}

// double java.lang.Math.sqrt(double)
void IntrinsicLocationsBuilderMIPS64::VisitMathSqrt(HInvoke* invoke) {
  CreateFPToFPLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathSqrt(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Mips64Assembler* assembler = GetAssembler();
  FpuRegister in = locations->InAt(0).AsFpuRegister<FpuRegister>();
  FpuRegister out = locations->Out().AsFpuRegister<FpuRegister>();

  __ SqrtD(out, in);
}

static void CreateFPToFP(ArenaAllocator* arena,
                         HInvoke* invoke,
                         Location::OutputOverlap overlaps = Location::kOutputOverlap) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresFpuRegister(), overlaps);
}

// double java.lang.Math.rint(double)
void IntrinsicLocationsBuilderMIPS64::VisitMathRint(HInvoke* invoke) {
  CreateFPToFP(arena_, invoke, Location::kNoOutputOverlap);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathRint(HInvoke* invoke) {
  LocationSummary* locations = invoke->GetLocations();
  Mips64Assembler* assembler = GetAssembler();
  FpuRegister in = locations->InAt(0).AsFpuRegister<FpuRegister>();
  FpuRegister out = locations->Out().AsFpuRegister<FpuRegister>();

  __ RintD(out, in);
}

// double java.lang.Math.floor(double)
void IntrinsicLocationsBuilderMIPS64::VisitMathFloor(HInvoke* invoke) {
  CreateFPToFP(arena_, invoke);
}

const constexpr uint16_t kFPLeaveUnchanged = kPositiveZero |
                                             kPositiveInfinity |
                                             kNegativeZero |
                                             kNegativeInfinity |
                                             kQuietNaN |
                                             kSignalingNaN;

enum FloatRoundingMode {
  kFloor,
  kCeil,
};

static void GenRoundingMode(LocationSummary* locations,
                            FloatRoundingMode mode,
                            Mips64Assembler* assembler) {
  FpuRegister in = locations->InAt(0).AsFpuRegister<FpuRegister>();
  FpuRegister out = locations->Out().AsFpuRegister<FpuRegister>();

  DCHECK_NE(in, out);

  Mips64Label done;

  // double floor/ceil(double in) {
  //     if in.isNaN || in.isInfinite || in.isZero {
  //         return in;
  //     }
  __ ClassD(out, in);
  __ Dmfc1(AT, out);
  __ Andi(AT, AT, kFPLeaveUnchanged);   // +0.0 | +Inf | -0.0 | -Inf | qNaN | sNaN
  __ MovD(out, in);
  __ Bnezc(AT, &done);

  //     Long outLong = floor/ceil(in);
  //     if outLong == Long.MAX_VALUE {
  //         // floor()/ceil() has almost certainly returned a value
  //         // which can't be successfully represented as a signed
  //         // 64-bit number.  Java expects that the input value will
  //         // be returned in these cases.
  //         // There is also a small probability that floor(in)/ceil(in)
  //         // correctly truncates/rounds up the input value to
  //         // Long.MAX_VALUE.  In that case, this exception handling
  //         // code still does the correct thing.
  //         return in;
  //     }
  if (mode == kFloor) {
    __ FloorLD(out, in);
  } else  if (mode == kCeil) {
    __ CeilLD(out, in);
  }
  __ Dmfc1(AT, out);
  __ MovD(out, in);
  __ LoadConst64(TMP, kPrimLongMax);
  __ Beqc(AT, TMP, &done);

  //     double out = outLong;
  //     return out;
  __ Dmtc1(AT, out);
  __ Cvtdl(out, out);
  __ Bind(&done);
  // }
}

void IntrinsicCodeGeneratorMIPS64::VisitMathFloor(HInvoke* invoke) {
  GenRoundingMode(invoke->GetLocations(), kFloor, GetAssembler());
}

// double java.lang.Math.ceil(double)
void IntrinsicLocationsBuilderMIPS64::VisitMathCeil(HInvoke* invoke) {
  CreateFPToFP(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMathCeil(HInvoke* invoke) {
  GenRoundingMode(invoke->GetLocations(), kCeil, GetAssembler());
}

static void GenRound(LocationSummary* locations, Mips64Assembler* assembler, Primitive::Type type) {
  FpuRegister in = locations->InAt(0).AsFpuRegister<FpuRegister>();
  FpuRegister half = locations->GetTemp(0).AsFpuRegister<FpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  DCHECK(type == Primitive::kPrimFloat || type == Primitive::kPrimDouble);

  Mips64Label done;
  Mips64Label finite;
  Mips64Label add;

  // if (in.isNaN) {
  //   return 0;
  // }
  //
  // out = floor(in);
  //
  // /*
  //  * TODO: Amend this code when emulator FCSR.NAN2008=1 bug is fixed.
  //  *
  //  * Starting with MIPSR6, which always sets FCSR.NAN2008=1, negative
  //  * numbers which are too large to be represented in a 32-/64-bit
  //  * signed integer will be processed by floor.X.Y to output
  //  * Integer.MIN_VALUE/Long.MIN_VALUE, and will no longer be
  //  * processed by this "if" statement.
  //  *
  //  * However, this bug in the 64-bit MIPS emulator causes the
  //  * behavior of floor.X.Y to be the same as pre-R6 implementations
  //  * of MIPS64.  When that bug is fixed this logic should be amended.
  //  */
  // if (out == MAX_VALUE) {
  //   TMP = (in < 0.0) ? 1 : 0;
  //   /*
  //    * If TMP is 1, then adding it to out will wrap its value from
  //    * MAX_VALUE to MIN_VALUE.
  //    */
  //   return out += TMP;
  // }
  //
  // /*
  //  * For negative values not handled by the previous "if" statement the
  //  * test here will correctly set the value of TMP.
  //  */
  // TMP = ((in - out) >= 0.5) ? 1 : 0;
  // return out += TMP;

  // Test for NaN.
  if (type == Primitive::kPrimDouble) {
    __ CmpUnD(FTMP, in, in);
  } else {
    __ CmpUnS(FTMP, in, in);
  }

  // Return zero for NaN.
  __ Move(out, ZERO);
  __ Bc1nez(FTMP, &done);

  // out = floor(in);
  if (type == Primitive::kPrimDouble) {
    __ FloorLD(FTMP, in);
    __ Dmfc1(out, FTMP);
  } else {
    __ FloorWS(FTMP, in);
    __ Mfc1(out, FTMP);
  }

  // TMP = (out = java.lang.Integer.MAX_VALUE) ? 1 : 0;
  if (type == Primitive::kPrimDouble) {
    __ LoadConst64(AT, std::numeric_limits<int64_t>::max());
  } else {
    __ LoadConst32(AT, std::numeric_limits<int32_t>::max());
  }
  __ Bnec(AT, out, &finite);

  if (type == Primitive::kPrimDouble) {
    __ Dmtc1(ZERO, FTMP);
    __ CmpLtD(FTMP, in, FTMP);
    __ Dmfc1(AT, FTMP);
  } else {
    __ Mtc1(ZERO, FTMP);
    __ CmpLtS(FTMP, in, FTMP);
    __ Mfc1(AT, FTMP);
  }

  __ Bc(&add);

  __ Bind(&finite);

  // TMP = (0.5 <= (in - out)) ? -1 : 0;
  if (type == Primitive::kPrimDouble) {
    __ Cvtdl(FTMP, FTMP);  // Convert output of floor.l.d back to "double".
    __ LoadConst64(AT, bit_cast<int64_t, double>(0.5));
    __ SubD(FTMP, in, FTMP);
    __ Dmtc1(AT, half);
    __ CmpLeD(FTMP, half, FTMP);
    __ Dmfc1(AT, FTMP);
  } else {
    __ Cvtsw(FTMP, FTMP);  // Convert output of floor.w.s back to "float".
    __ LoadConst32(AT, bit_cast<int32_t, float>(0.5f));
    __ SubS(FTMP, in, FTMP);
    __ Mtc1(AT, half);
    __ CmpLeS(FTMP, half, FTMP);
    __ Mfc1(AT, FTMP);
  }

  __ Bind(&add);

  // Return out -= TMP.
  if (type == Primitive::kPrimDouble) {
    __ Dsubu(out, out, AT);
  } else {
    __ Subu(out, out, AT);
  }

  __ Bind(&done);
}

// int java.lang.Math.round(float)
void IntrinsicLocationsBuilderMIPS64::VisitMathRoundFloat(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->AddTemp(Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorMIPS64::VisitMathRoundFloat(HInvoke* invoke) {
  GenRound(invoke->GetLocations(), GetAssembler(), Primitive::kPrimFloat);
}

// long java.lang.Math.round(double)
void IntrinsicLocationsBuilderMIPS64::VisitMathRoundDouble(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresFpuRegister());
  locations->AddTemp(Location::RequiresFpuRegister());
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorMIPS64::VisitMathRoundDouble(HInvoke* invoke) {
  GenRound(invoke->GetLocations(), GetAssembler(), Primitive::kPrimDouble);
}

// byte libcore.io.Memory.peekByte(long address)
void IntrinsicLocationsBuilderMIPS64::VisitMemoryPeekByte(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMemoryPeekByte(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  GpuRegister adr = invoke->GetLocations()->InAt(0).AsRegister<GpuRegister>();
  GpuRegister out = invoke->GetLocations()->Out().AsRegister<GpuRegister>();

  __ Lb(out, adr, 0);
}

// short libcore.io.Memory.peekShort(long address)
void IntrinsicLocationsBuilderMIPS64::VisitMemoryPeekShortNative(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMemoryPeekShortNative(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  GpuRegister adr = invoke->GetLocations()->InAt(0).AsRegister<GpuRegister>();
  GpuRegister out = invoke->GetLocations()->Out().AsRegister<GpuRegister>();

  __ Lh(out, adr, 0);
}

// int libcore.io.Memory.peekInt(long address)
void IntrinsicLocationsBuilderMIPS64::VisitMemoryPeekIntNative(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMemoryPeekIntNative(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  GpuRegister adr = invoke->GetLocations()->InAt(0).AsRegister<GpuRegister>();
  GpuRegister out = invoke->GetLocations()->Out().AsRegister<GpuRegister>();

  __ Lw(out, adr, 0);
}

// long libcore.io.Memory.peekLong(long address)
void IntrinsicLocationsBuilderMIPS64::VisitMemoryPeekLongNative(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMemoryPeekLongNative(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  GpuRegister adr = invoke->GetLocations()->InAt(0).AsRegister<GpuRegister>();
  GpuRegister out = invoke->GetLocations()->Out().AsRegister<GpuRegister>();

  __ Ld(out, adr, 0);
}

static void CreateIntIntToVoidLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
}

// void libcore.io.Memory.pokeByte(long address, byte value)
void IntrinsicLocationsBuilderMIPS64::VisitMemoryPokeByte(HInvoke* invoke) {
  CreateIntIntToVoidLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMemoryPokeByte(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  GpuRegister adr = invoke->GetLocations()->InAt(0).AsRegister<GpuRegister>();
  GpuRegister val = invoke->GetLocations()->InAt(1).AsRegister<GpuRegister>();

  __ Sb(val, adr, 0);
}

// void libcore.io.Memory.pokeShort(long address, short value)
void IntrinsicLocationsBuilderMIPS64::VisitMemoryPokeShortNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMemoryPokeShortNative(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  GpuRegister adr = invoke->GetLocations()->InAt(0).AsRegister<GpuRegister>();
  GpuRegister val = invoke->GetLocations()->InAt(1).AsRegister<GpuRegister>();

  __ Sh(val, adr, 0);
}

// void libcore.io.Memory.pokeInt(long address, int value)
void IntrinsicLocationsBuilderMIPS64::VisitMemoryPokeIntNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMemoryPokeIntNative(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  GpuRegister adr = invoke->GetLocations()->InAt(0).AsRegister<GpuRegister>();
  GpuRegister val = invoke->GetLocations()->InAt(1).AsRegister<GpuRegister>();

  __ Sw(val, adr, 00);
}

// void libcore.io.Memory.pokeLong(long address, long value)
void IntrinsicLocationsBuilderMIPS64::VisitMemoryPokeLongNative(HInvoke* invoke) {
  CreateIntIntToVoidLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitMemoryPokeLongNative(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  GpuRegister adr = invoke->GetLocations()->InAt(0).AsRegister<GpuRegister>();
  GpuRegister val = invoke->GetLocations()->InAt(1).AsRegister<GpuRegister>();

  __ Sd(val, adr, 0);
}

// Thread java.lang.Thread.currentThread()
void IntrinsicLocationsBuilderMIPS64::VisitThreadCurrentThread(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kNoCall,
                                                            kIntrinsified);
  locations->SetOut(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorMIPS64::VisitThreadCurrentThread(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  GpuRegister out = invoke->GetLocations()->Out().AsRegister<GpuRegister>();

  __ LoadFromOffset(kLoadUnsignedWord,
                    out,
                    TR,
                    Thread::PeerOffset<kMips64PointerSize>().Int32Value());
}

static void CreateIntIntIntToIntLocations(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::NoLocation());        // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister(), Location::kNoOutputOverlap);
}

static void GenUnsafeGet(HInvoke* invoke,
                         Primitive::Type type,
                         bool is_volatile,
                         CodeGeneratorMIPS64* codegen) {
  LocationSummary* locations = invoke->GetLocations();
  DCHECK((type == Primitive::kPrimInt) ||
         (type == Primitive::kPrimLong) ||
         (type == Primitive::kPrimNot));
  Mips64Assembler* assembler = codegen->GetAssembler();
  // Object pointer.
  GpuRegister base = locations->InAt(1).AsRegister<GpuRegister>();
  // Long offset.
  GpuRegister offset = locations->InAt(2).AsRegister<GpuRegister>();
  GpuRegister trg = locations->Out().AsRegister<GpuRegister>();

  __ Daddu(TMP, base, offset);
  if (is_volatile) {
    __ Sync(0);
  }
  switch (type) {
    case Primitive::kPrimInt:
      __ Lw(trg, TMP, 0);
      break;

    case Primitive::kPrimNot:
      __ Lwu(trg, TMP, 0);
      __ MaybeUnpoisonHeapReference(trg);
      break;

    case Primitive::kPrimLong:
      __ Ld(trg, TMP, 0);
      break;

    default:
      LOG(FATAL) << "Unsupported op size " << type;
      UNREACHABLE();
  }
}

// int sun.misc.Unsafe.getInt(Object o, long offset)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafeGet(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafeGet(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimInt, /* is_volatile */ false, codegen_);
}

// int sun.misc.Unsafe.getIntVolatile(Object o, long offset)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafeGetVolatile(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafeGetVolatile(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimInt, /* is_volatile */ true, codegen_);
}

// long sun.misc.Unsafe.getLong(Object o, long offset)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafeGetLong(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafeGetLong(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimLong, /* is_volatile */ false, codegen_);
}

// long sun.misc.Unsafe.getLongVolatile(Object o, long offset)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafeGetLongVolatile(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafeGetLongVolatile(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimLong, /* is_volatile */ true, codegen_);
}

// Object sun.misc.Unsafe.getObject(Object o, long offset)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafeGetObject(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafeGetObject(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimNot, /* is_volatile */ false, codegen_);
}

// Object sun.misc.Unsafe.getObjectVolatile(Object o, long offset)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafeGetObjectVolatile(HInvoke* invoke) {
  CreateIntIntIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafeGetObjectVolatile(HInvoke* invoke) {
  GenUnsafeGet(invoke, Primitive::kPrimNot, /* is_volatile */ true, codegen_);
}

static void CreateIntIntIntIntToVoid(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::NoLocation());        // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
}

static void GenUnsafePut(LocationSummary* locations,
                         Primitive::Type type,
                         bool is_volatile,
                         bool is_ordered,
                         CodeGeneratorMIPS64* codegen) {
  DCHECK((type == Primitive::kPrimInt) ||
         (type == Primitive::kPrimLong) ||
         (type == Primitive::kPrimNot));
  Mips64Assembler* assembler = codegen->GetAssembler();
  // Object pointer.
  GpuRegister base = locations->InAt(1).AsRegister<GpuRegister>();
  // Long offset.
  GpuRegister offset = locations->InAt(2).AsRegister<GpuRegister>();
  GpuRegister value = locations->InAt(3).AsRegister<GpuRegister>();

  __ Daddu(TMP, base, offset);
  if (is_volatile || is_ordered) {
    __ Sync(0);
  }
  switch (type) {
    case Primitive::kPrimInt:
    case Primitive::kPrimNot:
      if (kPoisonHeapReferences && type == Primitive::kPrimNot) {
        __ PoisonHeapReference(AT, value);
        __ Sw(AT, TMP, 0);
      } else {
        __ Sw(value, TMP, 0);
      }
      break;

    case Primitive::kPrimLong:
      __ Sd(value, TMP, 0);
      break;

    default:
      LOG(FATAL) << "Unsupported op size " << type;
      UNREACHABLE();
  }
  if (is_volatile) {
    __ Sync(0);
  }

  if (type == Primitive::kPrimNot) {
    bool value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MarkGCCard(base, value, value_can_be_null);
  }
}

// void sun.misc.Unsafe.putInt(Object o, long offset, int x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafePut(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafePut(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimInt,
               /* is_volatile */ false,
               /* is_ordered */ false,
               codegen_);
}

// void sun.misc.Unsafe.putOrderedInt(Object o, long offset, int x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafePutOrdered(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafePutOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimInt,
               /* is_volatile */ false,
               /* is_ordered */ true,
               codegen_);
}

// void sun.misc.Unsafe.putIntVolatile(Object o, long offset, int x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafePutVolatile(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafePutVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimInt,
               /* is_volatile */ true,
               /* is_ordered */ false,
               codegen_);
}

// void sun.misc.Unsafe.putObject(Object o, long offset, Object x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafePutObject(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafePutObject(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimNot,
               /* is_volatile */ false,
               /* is_ordered */ false,
               codegen_);
}

// void sun.misc.Unsafe.putOrderedObject(Object o, long offset, Object x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafePutObjectOrdered(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafePutObjectOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimNot,
               /* is_volatile */ false,
               /* is_ordered */ true,
               codegen_);
}

// void sun.misc.Unsafe.putObjectVolatile(Object o, long offset, Object x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafePutObjectVolatile(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafePutObjectVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimNot,
               /* is_volatile */ true,
               /* is_ordered */ false,
               codegen_);
}

// void sun.misc.Unsafe.putLong(Object o, long offset, long x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafePutLong(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafePutLong(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimLong,
               /* is_volatile */ false,
               /* is_ordered */ false,
               codegen_);
}

// void sun.misc.Unsafe.putOrderedLong(Object o, long offset, long x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafePutLongOrdered(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafePutLongOrdered(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimLong,
               /* is_volatile */ false,
               /* is_ordered */ true,
               codegen_);
}

// void sun.misc.Unsafe.putLongVolatile(Object o, long offset, long x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafePutLongVolatile(HInvoke* invoke) {
  CreateIntIntIntIntToVoid(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafePutLongVolatile(HInvoke* invoke) {
  GenUnsafePut(invoke->GetLocations(),
               Primitive::kPrimLong,
               /* is_volatile */ true,
               /* is_ordered */ false,
               codegen_);
}

static void CreateIntIntIntIntIntToInt(ArenaAllocator* arena, HInvoke* invoke) {
  LocationSummary* locations = new (arena) LocationSummary(invoke,
                                                           LocationSummary::kNoCall,
                                                           kIntrinsified);
  locations->SetInAt(0, Location::NoLocation());        // Unused receiver.
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->SetInAt(4, Location::RequiresRegister());

  locations->SetOut(Location::RequiresRegister());
}

static void GenCas(LocationSummary* locations, Primitive::Type type, CodeGeneratorMIPS64* codegen) {
  Mips64Assembler* assembler = codegen->GetAssembler();
  GpuRegister base = locations->InAt(1).AsRegister<GpuRegister>();
  GpuRegister offset = locations->InAt(2).AsRegister<GpuRegister>();
  GpuRegister expected = locations->InAt(3).AsRegister<GpuRegister>();
  GpuRegister value = locations->InAt(4).AsRegister<GpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  DCHECK_NE(base, out);
  DCHECK_NE(offset, out);
  DCHECK_NE(expected, out);

  if (type == Primitive::kPrimNot) {
    // Mark card for object assuming new value is stored.
    bool value_can_be_null = true;  // TODO: Worth finding out this information?
    codegen->MarkGCCard(base, value, value_can_be_null);
  }

  Mips64Label loop_head, exit_loop;
  __ Daddu(TMP, base, offset);

  if (kPoisonHeapReferences && type == Primitive::kPrimNot) {
    __ PoisonHeapReference(expected);
    // Do not poison `value`, if it is the same register as
    // `expected`, which has just been poisoned.
    if (value != expected) {
      __ PoisonHeapReference(value);
    }
  }

  // do {
  //   tmp_value = [tmp_ptr] - expected;
  // } while (tmp_value == 0 && failure([tmp_ptr] <- r_new_value));
  // result = tmp_value != 0;

  __ Sync(0);
  __ Bind(&loop_head);
  if (type == Primitive::kPrimLong) {
    __ Lld(out, TMP);
  } else {
    // Note: We will need a read barrier here, when read barrier
    // support is added to the MIPS64 back end.
    __ Ll(out, TMP);
    if (type == Primitive::kPrimNot) {
      // The LL instruction sign-extends the 32-bit value, but
      // 32-bit references must be zero-extended. Zero-extend `out`.
      __ Dext(out, out, 0, 32);
    }
  }
  __ Dsubu(out, out, expected);         // If we didn't get the 'expected'
  __ Sltiu(out, out, 1);                // value, set 'out' to false, and
  __ Beqzc(out, &exit_loop);            // return.
  __ Move(out, value);  // Use 'out' for the 'store conditional' instruction.
                        // If we use 'value' directly, we would lose 'value'
                        // in the case that the store fails.  Whether the
                        // store succeeds, or fails, it will load the
                        // correct Boolean value into the 'out' register.
  if (type == Primitive::kPrimLong) {
    __ Scd(out, TMP);
  } else {
    __ Sc(out, TMP);
  }
  __ Beqzc(out, &loop_head);    // If we couldn't do the read-modify-write
                                // cycle atomically then retry.
  __ Bind(&exit_loop);
  __ Sync(0);

  if (kPoisonHeapReferences && type == Primitive::kPrimNot) {
    __ UnpoisonHeapReference(expected);
    // Do not unpoison `value`, if it is the same register as
    // `expected`, which has just been unpoisoned.
    if (value != expected) {
      __ UnpoisonHeapReference(value);
    }
  }
}

// boolean sun.misc.Unsafe.compareAndSwapInt(Object o, long offset, int expected, int x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafeCASInt(HInvoke* invoke) {
  CreateIntIntIntIntIntToInt(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafeCASInt(HInvoke* invoke) {
  GenCas(invoke->GetLocations(), Primitive::kPrimInt, codegen_);
}

// boolean sun.misc.Unsafe.compareAndSwapLong(Object o, long offset, long expected, long x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafeCASLong(HInvoke* invoke) {
  CreateIntIntIntIntIntToInt(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafeCASLong(HInvoke* invoke) {
  GenCas(invoke->GetLocations(), Primitive::kPrimLong, codegen_);
}

// boolean sun.misc.Unsafe.compareAndSwapObject(Object o, long offset, Object expected, Object x)
void IntrinsicLocationsBuilderMIPS64::VisitUnsafeCASObject(HInvoke* invoke) {
  CreateIntIntIntIntIntToInt(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitUnsafeCASObject(HInvoke* invoke) {
  GenCas(invoke->GetLocations(), Primitive::kPrimNot, codegen_);
}

// int java.lang.String.compareTo(String anotherString)
void IntrinsicLocationsBuilderMIPS64::VisitStringCompareTo(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnMainAndSlowPath,
                                                            kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  Location outLocation = calling_convention.GetReturnLocation(Primitive::kPrimInt);
  locations->SetOut(Location::RegisterLocation(outLocation.AsRegister<GpuRegister>()));
}

void IntrinsicCodeGeneratorMIPS64::VisitStringCompareTo(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  // Note that the null check must have been done earlier.
  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  GpuRegister argument = locations->InAt(1).AsRegister<GpuRegister>();
  SlowPathCodeMIPS64* slow_path = new (GetAllocator()) IntrinsicSlowPathMIPS64(invoke);
  codegen_->AddSlowPath(slow_path);
  __ Beqzc(argument, slow_path->GetEntryLabel());

  codegen_->InvokeRuntime(kQuickStringCompareTo, invoke, invoke->GetDexPc(), slow_path);
  __ Bind(slow_path->GetExitLabel());
}

// boolean java.lang.String.equals(Object anObject)
void IntrinsicLocationsBuilderMIPS64::VisitStringEquals(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kNoCall,
                                                            kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetOut(Location::RequiresRegister());

  // Temporary registers to store lengths of strings and for calculations.
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorMIPS64::VisitStringEquals(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  GpuRegister str = locations->InAt(0).AsRegister<GpuRegister>();
  GpuRegister arg = locations->InAt(1).AsRegister<GpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  GpuRegister temp1 = locations->GetTemp(0).AsRegister<GpuRegister>();
  GpuRegister temp2 = locations->GetTemp(1).AsRegister<GpuRegister>();
  GpuRegister temp3 = locations->GetTemp(2).AsRegister<GpuRegister>();

  Mips64Label loop;
  Mips64Label end;
  Mips64Label return_true;
  Mips64Label return_false;

  // Get offsets of count, value, and class fields within a string object.
  const int32_t count_offset = mirror::String::CountOffset().Int32Value();
  const int32_t value_offset = mirror::String::ValueOffset().Int32Value();
  const int32_t class_offset = mirror::Object::ClassOffset().Int32Value();

  // Note that the null check must have been done earlier.
  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  // If the register containing the pointer to "this", and the register
  // containing the pointer to "anObject" are the same register then
  // "this", and "anObject" are the same object and we can
  // short-circuit the logic to a true result.
  if (str == arg) {
    __ LoadConst64(out, 1);
    return;
  }

  StringEqualsOptimizations optimizations(invoke);
  if (!optimizations.GetArgumentNotNull()) {
    // Check if input is null, return false if it is.
    __ Beqzc(arg, &return_false);
  }

  // Reference equality check, return true if same reference.
  __ Beqc(str, arg, &return_true);

  if (!optimizations.GetArgumentIsString()) {
    // Instanceof check for the argument by comparing class fields.
    // All string objects must have the same type since String cannot be subclassed.
    // Receiver must be a string object, so its class field is equal to all strings' class fields.
    // If the argument is a string object, its class field must be equal to receiver's class field.
    __ Lw(temp1, str, class_offset);
    __ Lw(temp2, arg, class_offset);
    __ Bnec(temp1, temp2, &return_false);
  }

  // Load `count` fields of this and argument strings.
  __ Lw(temp1, str, count_offset);
  __ Lw(temp2, arg, count_offset);
  // Check if `count` fields are equal, return false if they're not.
  // Also compares the compression style, if differs return false.
  __ Bnec(temp1, temp2, &return_false);
  // Return true if both strings are empty. Even with string compression `count == 0` means empty.
  static_assert(static_cast<uint32_t>(mirror::StringCompressionFlag::kCompressed) == 0u,
                "Expecting 0=compressed, 1=uncompressed");
  __ Beqzc(temp1, &return_true);

  // Don't overwrite input registers
  __ Move(TMP, str);
  __ Move(temp3, arg);

  // Assertions that must hold in order to compare strings 8 bytes at a time.
  DCHECK_ALIGNED(value_offset, 8);
  static_assert(IsAligned<8>(kObjectAlignment), "String of odd length is not zero padded");

  if (mirror::kUseStringCompression) {
    // For string compression, calculate the number of bytes to compare (not chars).
    __ Dext(temp2, temp1, 0, 1);         // Extract compression flag.
    __ Srl(temp1, temp1, 1);             // Extract length.
    __ Sllv(temp1, temp1, temp2);        // Double the byte count if uncompressed.
  }

  // Loop to compare strings 8 bytes at a time starting at the beginning of the string.
  // Ok to do this because strings are zero-padded to kObjectAlignment.
  __ Bind(&loop);
  __ Ld(out, TMP, value_offset);
  __ Ld(temp2, temp3, value_offset);
  __ Bnec(out, temp2, &return_false);
  __ Daddiu(TMP, TMP, 8);
  __ Daddiu(temp3, temp3, 8);
  // With string compression, we have compared 8 bytes, otherwise 4 chars.
  __ Addiu(temp1, temp1, mirror::kUseStringCompression ? -8 : -4);
  __ Bgtzc(temp1, &loop);

  // Return true and exit the function.
  // If loop does not result in returning false, we return true.
  __ Bind(&return_true);
  __ LoadConst64(out, 1);
  __ Bc(&end);

  // Return false and exit the function.
  __ Bind(&return_false);
  __ LoadConst64(out, 0);
  __ Bind(&end);
}

static void GenerateStringIndexOf(HInvoke* invoke,
                                  Mips64Assembler* assembler,
                                  CodeGeneratorMIPS64* codegen,
                                  ArenaAllocator* allocator,
                                  bool start_at_zero) {
  LocationSummary* locations = invoke->GetLocations();
  GpuRegister tmp_reg = start_at_zero ? locations->GetTemp(0).AsRegister<GpuRegister>() : TMP;

  // Note that the null check must have been done earlier.
  DCHECK(!invoke->CanDoImplicitNullCheckOn(invoke->InputAt(0)));

  // Check for code points > 0xFFFF. Either a slow-path check when we don't know statically,
  // or directly dispatch for a large constant, or omit slow-path for a small constant or a char.
  SlowPathCodeMIPS64* slow_path = nullptr;
  HInstruction* code_point = invoke->InputAt(1);
  if (code_point->IsIntConstant()) {
    if (!IsUint<16>(code_point->AsIntConstant()->GetValue())) {
      // Always needs the slow-path. We could directly dispatch to it,
      // but this case should be rare, so for simplicity just put the
      // full slow-path down and branch unconditionally.
      slow_path = new (allocator) IntrinsicSlowPathMIPS64(invoke);
      codegen->AddSlowPath(slow_path);
      __ Bc(slow_path->GetEntryLabel());
      __ Bind(slow_path->GetExitLabel());
      return;
    }
  } else if (code_point->GetType() != Primitive::kPrimChar) {
    GpuRegister char_reg = locations->InAt(1).AsRegister<GpuRegister>();
    __ LoadConst32(tmp_reg, std::numeric_limits<uint16_t>::max());
    slow_path = new (allocator) IntrinsicSlowPathMIPS64(invoke);
    codegen->AddSlowPath(slow_path);
    __ Bltuc(tmp_reg, char_reg, slow_path->GetEntryLabel());    // UTF-16 required
  }

  if (start_at_zero) {
    DCHECK_EQ(tmp_reg, A2);
    // Start-index = 0.
    __ Clear(tmp_reg);
  }

  codegen->InvokeRuntime(kQuickIndexOf, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickIndexOf, int32_t, void*, uint32_t, uint32_t>();

  if (slow_path != nullptr) {
    __ Bind(slow_path->GetExitLabel());
  }
}

// int java.lang.String.indexOf(int ch)
void IntrinsicLocationsBuilderMIPS64::VisitStringIndexOf(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnMainAndSlowPath,
                                                            kIntrinsified);
  // We have a hand-crafted assembly stub that follows the runtime
  // calling convention. So it's best to align the inputs accordingly.
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  Location outLocation = calling_convention.GetReturnLocation(Primitive::kPrimInt);
  locations->SetOut(Location::RegisterLocation(outLocation.AsRegister<GpuRegister>()));

  // Need a temp for slow-path codepoint compare, and need to send start-index=0.
  locations->AddTemp(Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
}

void IntrinsicCodeGeneratorMIPS64::VisitStringIndexOf(HInvoke* invoke) {
  GenerateStringIndexOf(invoke, GetAssembler(), codegen_, GetAllocator(), /* start_at_zero */ true);
}

// int java.lang.String.indexOf(int ch, int fromIndex)
void IntrinsicLocationsBuilderMIPS64::VisitStringIndexOfAfter(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnMainAndSlowPath,
                                                            kIntrinsified);
  // We have a hand-crafted assembly stub that follows the runtime
  // calling convention. So it's best to align the inputs accordingly.
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
  Location outLocation = calling_convention.GetReturnLocation(Primitive::kPrimInt);
  locations->SetOut(Location::RegisterLocation(outLocation.AsRegister<GpuRegister>()));
}

void IntrinsicCodeGeneratorMIPS64::VisitStringIndexOfAfter(HInvoke* invoke) {
  GenerateStringIndexOf(
      invoke, GetAssembler(), codegen_, GetAllocator(), /* start_at_zero */ false);
}

// java.lang.StringFactory.newStringFromBytes(byte[] data, int high, int offset, int byteCount)
void IntrinsicLocationsBuilderMIPS64::VisitStringNewStringFromBytes(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnMainAndSlowPath,
                                                            kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
  locations->SetInAt(3, Location::RegisterLocation(calling_convention.GetRegisterAt(3)));
  Location outLocation = calling_convention.GetReturnLocation(Primitive::kPrimInt);
  locations->SetOut(Location::RegisterLocation(outLocation.AsRegister<GpuRegister>()));
}

void IntrinsicCodeGeneratorMIPS64::VisitStringNewStringFromBytes(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  GpuRegister byte_array = locations->InAt(0).AsRegister<GpuRegister>();
  SlowPathCodeMIPS64* slow_path = new (GetAllocator()) IntrinsicSlowPathMIPS64(invoke);
  codegen_->AddSlowPath(slow_path);
  __ Beqzc(byte_array, slow_path->GetEntryLabel());

  codegen_->InvokeRuntime(kQuickAllocStringFromBytes, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickAllocStringFromBytes, void*, void*, int32_t, int32_t, int32_t>();
  __ Bind(slow_path->GetExitLabel());
}

// java.lang.StringFactory.newStringFromChars(int offset, int charCount, char[] data)
void IntrinsicLocationsBuilderMIPS64::VisitStringNewStringFromChars(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnMainOnly,
                                                            kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  locations->SetInAt(1, Location::RegisterLocation(calling_convention.GetRegisterAt(1)));
  locations->SetInAt(2, Location::RegisterLocation(calling_convention.GetRegisterAt(2)));
  Location outLocation = calling_convention.GetReturnLocation(Primitive::kPrimInt);
  locations->SetOut(Location::RegisterLocation(outLocation.AsRegister<GpuRegister>()));
}

void IntrinsicCodeGeneratorMIPS64::VisitStringNewStringFromChars(HInvoke* invoke) {
  // No need to emit code checking whether `locations->InAt(2)` is a null
  // pointer, as callers of the native method
  //
  //   java.lang.StringFactory.newStringFromChars(int offset, int charCount, char[] data)
  //
  // all include a null check on `data` before calling that method.
  codegen_->InvokeRuntime(kQuickAllocStringFromChars, invoke, invoke->GetDexPc());
  CheckEntrypointTypes<kQuickAllocStringFromChars, void*, int32_t, int32_t, void*>();
}

// java.lang.StringFactory.newStringFromString(String toCopy)
void IntrinsicLocationsBuilderMIPS64::VisitStringNewStringFromString(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kCallOnMainAndSlowPath,
                                                            kIntrinsified);
  InvokeRuntimeCallingConvention calling_convention;
  locations->SetInAt(0, Location::RegisterLocation(calling_convention.GetRegisterAt(0)));
  Location outLocation = calling_convention.GetReturnLocation(Primitive::kPrimInt);
  locations->SetOut(Location::RegisterLocation(outLocation.AsRegister<GpuRegister>()));
}

void IntrinsicCodeGeneratorMIPS64::VisitStringNewStringFromString(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  GpuRegister string_to_copy = locations->InAt(0).AsRegister<GpuRegister>();
  SlowPathCodeMIPS64* slow_path = new (GetAllocator()) IntrinsicSlowPathMIPS64(invoke);
  codegen_->AddSlowPath(slow_path);
  __ Beqzc(string_to_copy, slow_path->GetEntryLabel());

  codegen_->InvokeRuntime(kQuickAllocStringFromString, invoke, invoke->GetDexPc(), slow_path);
  CheckEntrypointTypes<kQuickAllocStringFromString, void*, void*>();
  __ Bind(slow_path->GetExitLabel());
}

static void GenIsInfinite(LocationSummary* locations,
                          bool is64bit,
                          Mips64Assembler* assembler) {
  FpuRegister in = locations->InAt(0).AsFpuRegister<FpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  if (is64bit) {
    __ ClassD(FTMP, in);
  } else {
    __ ClassS(FTMP, in);
  }
  __ Mfc1(out, FTMP);
  __ Andi(out, out, kPositiveInfinity | kNegativeInfinity);
  __ Sltu(out, ZERO, out);
}

// boolean java.lang.Float.isInfinite(float)
void IntrinsicLocationsBuilderMIPS64::VisitFloatIsInfinite(HInvoke* invoke) {
  CreateFPToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitFloatIsInfinite(HInvoke* invoke) {
  GenIsInfinite(invoke->GetLocations(), /* is64bit */ false, GetAssembler());
}

// boolean java.lang.Double.isInfinite(double)
void IntrinsicLocationsBuilderMIPS64::VisitDoubleIsInfinite(HInvoke* invoke) {
  CreateFPToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitDoubleIsInfinite(HInvoke* invoke) {
  GenIsInfinite(invoke->GetLocations(), /* is64bit */ true, GetAssembler());
}

// void java.lang.String.getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin)
void IntrinsicLocationsBuilderMIPS64::VisitStringGetCharsNoCheck(HInvoke* invoke) {
  LocationSummary* locations = new (arena_) LocationSummary(invoke,
                                                            LocationSummary::kNoCall,
                                                            kIntrinsified);
  locations->SetInAt(0, Location::RequiresRegister());
  locations->SetInAt(1, Location::RequiresRegister());
  locations->SetInAt(2, Location::RequiresRegister());
  locations->SetInAt(3, Location::RequiresRegister());
  locations->SetInAt(4, Location::RequiresRegister());

  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
  locations->AddTemp(Location::RequiresRegister());
}

void IntrinsicCodeGeneratorMIPS64::VisitStringGetCharsNoCheck(HInvoke* invoke) {
  Mips64Assembler* assembler = GetAssembler();
  LocationSummary* locations = invoke->GetLocations();

  // Check assumption that sizeof(Char) is 2 (used in scaling below).
  const size_t char_size = Primitive::ComponentSize(Primitive::kPrimChar);
  DCHECK_EQ(char_size, 2u);
  const size_t char_shift = Primitive::ComponentSizeShift(Primitive::kPrimChar);

  GpuRegister srcObj = locations->InAt(0).AsRegister<GpuRegister>();
  GpuRegister srcBegin = locations->InAt(1).AsRegister<GpuRegister>();
  GpuRegister srcEnd = locations->InAt(2).AsRegister<GpuRegister>();
  GpuRegister dstObj = locations->InAt(3).AsRegister<GpuRegister>();
  GpuRegister dstBegin = locations->InAt(4).AsRegister<GpuRegister>();

  GpuRegister dstPtr = locations->GetTemp(0).AsRegister<GpuRegister>();
  GpuRegister srcPtr = locations->GetTemp(1).AsRegister<GpuRegister>();
  GpuRegister numChrs = locations->GetTemp(2).AsRegister<GpuRegister>();

  Mips64Label done;
  Mips64Label loop;

  // Location of data in char array buffer.
  const uint32_t data_offset = mirror::Array::DataOffset(char_size).Uint32Value();

  // Get offset of value field within a string object.
  const int32_t value_offset = mirror::String::ValueOffset().Int32Value();

  __ Beqc(srcEnd, srcBegin, &done);  // No characters to move.

  // Calculate number of characters to be copied.
  __ Dsubu(numChrs, srcEnd, srcBegin);

  // Calculate destination address.
  __ Daddiu(dstPtr, dstObj, data_offset);
  __ Dlsa(dstPtr, dstBegin, dstPtr, char_shift);

  if (mirror::kUseStringCompression) {
    Mips64Label uncompressed_copy, compressed_loop;
    const uint32_t count_offset = mirror::String::CountOffset().Uint32Value();
    // Load count field and extract compression flag.
    __ LoadFromOffset(kLoadWord, TMP, srcObj, count_offset);
    __ Dext(TMP, TMP, 0, 1);

    // If string is uncompressed, use uncompressed path.
    __ Bnezc(TMP, &uncompressed_copy);

    // Copy loop for compressed src, copying 1 character (8-bit) to (16-bit) at a time.
    __ Daddu(srcPtr, srcObj, srcBegin);
    __ Bind(&compressed_loop);
    __ LoadFromOffset(kLoadUnsignedByte, TMP, srcPtr, value_offset);
    __ StoreToOffset(kStoreHalfword, TMP, dstPtr, 0);
    __ Daddiu(numChrs, numChrs, -1);
    __ Daddiu(srcPtr, srcPtr, 1);
    __ Daddiu(dstPtr, dstPtr, 2);
    __ Bnezc(numChrs, &compressed_loop);

    __ Bc(&done);
    __ Bind(&uncompressed_copy);
  }

  // Calculate source address.
  __ Daddiu(srcPtr, srcObj, value_offset);
  __ Dlsa(srcPtr, srcBegin, srcPtr, char_shift);

  __ Bind(&loop);
  __ Lh(AT, srcPtr, 0);
  __ Daddiu(numChrs, numChrs, -1);
  __ Daddiu(srcPtr, srcPtr, char_size);
  __ Sh(AT, dstPtr, 0);
  __ Daddiu(dstPtr, dstPtr, char_size);
  __ Bnezc(numChrs, &loop);

  __ Bind(&done);
}

static void GenHighestOneBit(LocationSummary* locations,
                             Primitive::Type type,
                             Mips64Assembler* assembler) {
  DCHECK(type == Primitive::kPrimInt || type == Primitive::kPrimLong) << PrettyDescriptor(type);

  GpuRegister in = locations->InAt(0).AsRegister<GpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  if (type == Primitive::kPrimLong) {
    __ Dclz(TMP, in);
    __ LoadConst64(AT, INT64_C(0x8000000000000000));
    __ Dsrlv(AT, AT, TMP);
  } else {
    __ Clz(TMP, in);
    __ LoadConst32(AT, 0x80000000);
    __ Srlv(AT, AT, TMP);
  }
  // For either value of "type", when "in" is zero, "out" should also
  // be zero. Without this extra "and" operation, when "in" is zero,
  // "out" would be either Integer.MIN_VALUE, or Long.MIN_VALUE because
  // the MIPS logical shift operations "dsrlv", and "srlv" don't use
  // the shift amount (TMP) directly; they use either (TMP % 64) or
  // (TMP % 32), respectively.
  __ And(out, AT, in);
}

// int java.lang.Integer.highestOneBit(int)
void IntrinsicLocationsBuilderMIPS64::VisitIntegerHighestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitIntegerHighestOneBit(HInvoke* invoke) {
  GenHighestOneBit(invoke->GetLocations(), Primitive::kPrimInt, GetAssembler());
}

// long java.lang.Long.highestOneBit(long)
void IntrinsicLocationsBuilderMIPS64::VisitLongHighestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitLongHighestOneBit(HInvoke* invoke) {
  GenHighestOneBit(invoke->GetLocations(), Primitive::kPrimLong, GetAssembler());
}

static void GenLowestOneBit(LocationSummary* locations,
                            Primitive::Type type,
                            Mips64Assembler* assembler) {
  DCHECK(type == Primitive::kPrimInt || type == Primitive::kPrimLong) << PrettyDescriptor(type);

  GpuRegister in = locations->InAt(0).AsRegister<GpuRegister>();
  GpuRegister out = locations->Out().AsRegister<GpuRegister>();

  if (type == Primitive::kPrimLong) {
    __ Dsubu(TMP, ZERO, in);
  } else {
    __ Subu(TMP, ZERO, in);
  }
  __ And(out, TMP, in);
}

// int java.lang.Integer.lowestOneBit(int)
void IntrinsicLocationsBuilderMIPS64::VisitIntegerLowestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitIntegerLowestOneBit(HInvoke* invoke) {
  GenLowestOneBit(invoke->GetLocations(), Primitive::kPrimInt, GetAssembler());
}

// long java.lang.Long.lowestOneBit(long)
void IntrinsicLocationsBuilderMIPS64::VisitLongLowestOneBit(HInvoke* invoke) {
  CreateIntToIntLocations(arena_, invoke);
}

void IntrinsicCodeGeneratorMIPS64::VisitLongLowestOneBit(HInvoke* invoke) {
  GenLowestOneBit(invoke->GetLocations(), Primitive::kPrimLong, GetAssembler());
}

UNIMPLEMENTED_INTRINSIC(MIPS64, ReferenceGetReferent)
UNIMPLEMENTED_INTRINSIC(MIPS64, SystemArrayCopyChar)
UNIMPLEMENTED_INTRINSIC(MIPS64, SystemArrayCopy)

UNIMPLEMENTED_INTRINSIC(MIPS64, MathCos)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathSin)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathAcos)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathAsin)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathAtan)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathAtan2)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathCbrt)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathCosh)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathExp)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathExpm1)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathHypot)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathLog)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathLog10)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathNextAfter)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathSinh)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathTan)
UNIMPLEMENTED_INTRINSIC(MIPS64, MathTanh)

UNIMPLEMENTED_INTRINSIC(MIPS64, StringStringIndexOf);
UNIMPLEMENTED_INTRINSIC(MIPS64, StringStringIndexOfAfter);
UNIMPLEMENTED_INTRINSIC(MIPS64, StringBufferAppend);
UNIMPLEMENTED_INTRINSIC(MIPS64, StringBufferLength);
UNIMPLEMENTED_INTRINSIC(MIPS64, StringBufferToString);
UNIMPLEMENTED_INTRINSIC(MIPS64, StringBuilderAppend);
UNIMPLEMENTED_INTRINSIC(MIPS64, StringBuilderLength);
UNIMPLEMENTED_INTRINSIC(MIPS64, StringBuilderToString);

// 1.8.
UNIMPLEMENTED_INTRINSIC(MIPS64, UnsafeGetAndAddInt)
UNIMPLEMENTED_INTRINSIC(MIPS64, UnsafeGetAndAddLong)
UNIMPLEMENTED_INTRINSIC(MIPS64, UnsafeGetAndSetInt)
UNIMPLEMENTED_INTRINSIC(MIPS64, UnsafeGetAndSetLong)
UNIMPLEMENTED_INTRINSIC(MIPS64, UnsafeGetAndSetObject)

UNIMPLEMENTED_INTRINSIC(MIPS64, IntegerValueOf)

UNREACHABLE_INTRINSICS(MIPS64)

#undef __

}  // namespace mips64
}  // namespace art
