// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MACHINE_OPTIMIZATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_MACHINE_OPTIMIZATION_REDUCER_H_

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

#include "src/base/bits.h"
#include "src/base/division-by-constant.h"
#include "src/base/functional.h"
#include "src/base/ieee754.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/overflowing-math.h"
#include "src/base/template-utils.h"
#include "src/base/vector.h"
#include "src/builtins/builtins.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/machine-operator-reducer.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/handles/handles.h"
#include "src/numbers/conversions.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <typename>
class VariableReducer;

// The MachineOptimizationAssembler performs basic optimizations on low-level
// operations that can be performed on-the-fly, without requiring type analysis
// or analyzing uses. It largely corresponds to MachineOperatorReducer in
// sea-of-nodes Turbofan.
//
// Additional optimizations include some of the control-flow reductions that
// were previously done in CommonOperatorReducer, including:
//    1- Reducing Phis, whose all inputs are the same, replace
//      them with their input.

template <class Next>
class MachineOptimizationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE()
#if defined(__clang__)
  static_assert(reducer_list_contains<ReducerList, VariableReducer>::value);
#endif

  // TODO(mslekova): Implement ReduceSelect and ReducePhi,
  // by reducing `(f > 0) ? f : -f` to `fabs(f)`.

  OpIndex REDUCE(Change)(OpIndex input, ChangeOp::Kind kind,
                         ChangeOp::Assumption assumption,
                         RegisterRepresentation from,
                         RegisterRepresentation to) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceChange(input, kind, assumption, from, to);
    }
    using Kind = ChangeOp::Kind;
    if (from == WordRepresentation::Word32()) {
      input = TryRemoveWord32ToWord64Conversion(input);
    }
    if (uint64_t value;
        from.IsWord() && matcher.MatchIntegralWordConstant(
                             input, WordRepresentation(from), &value)) {
      using Rep = RegisterRepresentation;
      switch (multi(kind, from, to)) {
        case multi(Kind::kSignExtend, Rep::Word32(), Rep::Word64()):
          return __ Word64Constant(int64_t{static_cast<int32_t>(value)});
        case multi(Kind::kZeroExtend, Rep::Word32(), Rep::Word64()):
        case multi(Kind::kBitcast, Rep::Word32(), Rep::Word64()):
          return __ Word64Constant(uint64_t{static_cast<uint32_t>(value)});
        case multi(Kind::kBitcast, Rep::Word32(), Rep::Float32()):
          return __ Float32Constant(
              base::bit_cast<float>(static_cast<uint32_t>(value)));
        case multi(Kind::kBitcast, Rep::Word64(), Rep::Float64()):
          return __ Float64Constant(base::bit_cast<double>(value));
        case multi(Kind::kSignedToFloat, Rep::Word32(), Rep::Float64()):
          return __ Float64Constant(
              static_cast<double>(static_cast<int32_t>(value)));
        case multi(Kind::kSignedToFloat, Rep::Word64(), Rep::Float64()):
          return __ Float64Constant(
              static_cast<double>(static_cast<int64_t>(value)));
        case multi(Kind::kUnsignedToFloat, Rep::Word32(), Rep::Float64()):
          return __ Float64Constant(
              static_cast<double>(static_cast<uint32_t>(value)));
        case multi(Kind::kTruncate, Rep::Word64(), Rep::Word32()):
          return __ Word32Constant(static_cast<uint32_t>(value));
        default:
          break;
      }
    }
    if (float value; from == RegisterRepresentation::Float32() &&
                     matcher.MatchFloat32Constant(input, &value)) {
      if (kind == Kind::kFloatConversion &&
          to == RegisterRepresentation::Float64()) {
        return __ Float64Constant(value);
      }
      if (kind == Kind::kBitcast && to == WordRepresentation::Word32()) {
        return __ Word32Constant(base::bit_cast<uint32_t>(value));
      }
    }
    if (double value; from == RegisterRepresentation::Float64() &&
                      matcher.MatchFloat64Constant(input, &value)) {
      if (kind == Kind::kFloatConversion &&
          to == RegisterRepresentation::Float32()) {
        return __ Float32Constant(DoubleToFloat32_NoInline(value));
      }
      if (kind == Kind::kBitcast && to == WordRepresentation::Word64()) {
        return __ Word64Constant(base::bit_cast<uint64_t>(value));
      }
      if (kind == Kind::kSignedFloatTruncateOverflowToMin) {
        double truncated = std::trunc(value);
        if (to == WordRepresentation::Word64()) {
          int64_t result = std::numeric_limits<int64_t>::min();
          if (truncated >= std::numeric_limits<int64_t>::min() &&
              truncated <= kMaxDoubleRepresentableInt64) {
            result = static_cast<int64_t>(truncated);
          }
          return __ Word64Constant(result);
        }
        if (to == WordRepresentation::Word32()) {
          int32_t result = std::numeric_limits<int32_t>::min();
          if (truncated >= std::numeric_limits<int32_t>::min() &&
              truncated <= std::numeric_limits<int32_t>::max()) {
            result = static_cast<int32_t>(truncated);
          }
          return __ Word32Constant(result);
        }
      }
      if (kind == Kind::kJSFloatTruncate &&
          to == WordRepresentation::Word32()) {
        return __ Word32Constant(DoubleToInt32_NoInline(value));
      }
      if (kind == Kind::kExtractHighHalf) {
        DCHECK_EQ(to, RegisterRepresentation::Word32());
        return __ Word32Constant(
            static_cast<uint32_t>(base::bit_cast<uint64_t>(value) >> 32));
      }
      if (kind == Kind::kExtractLowHalf) {
        DCHECK_EQ(to, RegisterRepresentation::Word32());
        return __ Word32Constant(
            static_cast<uint32_t>(base::bit_cast<uint64_t>(value)));
      }
    }
    if (float value; from == RegisterRepresentation::Float32() &&
                     matcher.MatchFloat32Constant(input, &value)) {
      if (kind == Kind::kFloatConversion &&
          to == RegisterRepresentation::Float64()) {
        return __ Float64Constant(value);
      }
    }

    const Operation& input_op = matcher.Get(input);
    if (const ChangeOp* change_op = input_op.TryCast<ChangeOp>()) {
      if (change_op->from == to && change_op->to == from &&
          change_op->IsReversibleBy(kind, signalling_nan_possible)) {
        return change_op->input();
      }
    }
    return Next::ReduceChange(input, kind, assumption, from, to);
  }

  OpIndex REDUCE(BitcastWord32PairToFloat64)(OpIndex hi_word32,
                                             OpIndex lo_word32) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceBitcastWord32PairToFloat64(hi_word32, lo_word32);
    }
    uint32_t lo, hi;
    if (matcher.MatchIntegralWord32Constant(hi_word32, &hi) &&
        matcher.MatchIntegralWord32Constant(lo_word32, &lo)) {
      return __ Float64Constant(
          base::bit_cast<double>(uint64_t{hi} << 32 | uint64_t{lo}));
    }
    return Next::ReduceBitcastWord32PairToFloat64(hi_word32, lo_word32);
  }

  OpIndex REDUCE(TaggedBitcast)(OpIndex input, RegisterRepresentation from,
                                RegisterRepresentation to,
                                TaggedBitcastOp::Kind kind) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceTaggedBitcast(input, from, to, kind);
    }
    // A Tagged -> Untagged -> Tagged sequence can be short-cut.
    // An Untagged -> Tagged -> Untagged sequence however cannot be removed,
    // because the GC might have modified the pointer.
    if (auto* input_bitcast = matcher.TryCast<TaggedBitcastOp>(input)) {
      if (all_of(input_bitcast->to, from) ==
              RegisterRepresentation::PointerSized() &&
          all_of(input_bitcast->from, to) == RegisterRepresentation::Tagged()) {
        return input_bitcast->input();
      }
    }
    // An Untagged -> Smi -> Untagged sequence can be short-cut.
    if (auto* input_bitcast = matcher.TryCast<TaggedBitcastOp>(input);
        input_bitcast && to.IsWord() &&
        (kind == TaggedBitcastOp::Kind::kSmi ||
         input_bitcast->kind == TaggedBitcastOp::Kind::kSmi)) {
      if (input_bitcast->from == to) return input_bitcast->input();
      if (input_bitcast->from == RegisterRepresentation::Word32()) {
        DCHECK_EQ(to, RegisterRepresentation::Word64());
        return __ BitcastWord32ToWord64(input_bitcast->input());
      }
      DCHECK(input_bitcast->from == RegisterRepresentation::Word64() &&
             to == RegisterRepresentation::Word32());
      return __ TruncateWord64ToWord32(input_bitcast->input());
    }
    // Try to constant-fold TaggedBitcast from Word Constant to Word.
    if (to.IsWord()) {
      if (const ConstantOp* cst = matcher.TryCast<ConstantOp>(input)) {
        if (cst->kind == ConstantOp::Kind::kWord32 ||
            cst->kind == ConstantOp::Kind::kWord64) {
          if (to == RegisterRepresentation::Word64()) {
            return __ Word64Constant(cst->integral());
          } else {
            DCHECK_EQ(to, RegisterRepresentation::Word32());
            return __ Word32Constant(static_cast<uint32_t>(cst->integral()));
          }
        }
      }
    }
    return Next::ReduceTaggedBitcast(input, from, to, kind);
  }

  OpIndex REDUCE(FloatUnary)(OpIndex input, FloatUnaryOp::Kind kind,
                             FloatRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceFloatUnary(input, kind, rep);
    }
    if (float k; rep == FloatRepresentation::Float32() &&
                 matcher.MatchFloat32Constant(input, &k)) {
      if (std::isnan(k) && !signalling_nan_possible) {
        return __ Float32Constant(std::numeric_limits<float>::quiet_NaN());
      }
      switch (kind) {
        case FloatUnaryOp::Kind::kAbs:
          return __ Float32Constant(std::abs(k));
        case FloatUnaryOp::Kind::kNegate:
          return __ Float32Constant(-k);
        case FloatUnaryOp::Kind::kSilenceNaN:
          DCHECK(!std::isnan(k));
          return __ Float32Constant(k);
        case FloatUnaryOp::Kind::kRoundDown:
          return __ Float32Constant(std::floor(k));
        case FloatUnaryOp::Kind::kRoundUp:
          return __ Float32Constant(std::ceil(k));
        case FloatUnaryOp::Kind::kRoundToZero:
          return __ Float32Constant(std::trunc(k));
        case FloatUnaryOp::Kind::kRoundTiesEven:
          DCHECK_EQ(std::nearbyint(1.5), 2);
          DCHECK_EQ(std::nearbyint(2.5), 2);
          return __ Float32Constant(std::nearbyint(k));
        case FloatUnaryOp::Kind::kLog:
          return __ Float32Constant(base::ieee754::log(k));
        case FloatUnaryOp::Kind::kSqrt:
          return __ Float32Constant(std::sqrt(k));
        case FloatUnaryOp::Kind::kExp:
          return __ Float32Constant(base::ieee754::exp(k));
        case FloatUnaryOp::Kind::kExpm1:
          return __ Float32Constant(base::ieee754::expm1(k));
        case FloatUnaryOp::Kind::kSin:
          return __ Float32Constant(SIN_IMPL(k));
        case FloatUnaryOp::Kind::kCos:
          return __ Float32Constant(COS_IMPL(k));
        case FloatUnaryOp::Kind::kSinh:
          return __ Float32Constant(base::ieee754::sinh(k));
        case FloatUnaryOp::Kind::kCosh:
          return __ Float32Constant(base::ieee754::cosh(k));
        case FloatUnaryOp::Kind::kAcos:
          return __ Float32Constant(base::ieee754::acos(k));
        case FloatUnaryOp::Kind::kAsin:
          return __ Float32Constant(base::ieee754::asin(k));
        case FloatUnaryOp::Kind::kAsinh:
          return __ Float32Constant(base::ieee754::asinh(k));
        case FloatUnaryOp::Kind::kAcosh:
          return __ Float32Constant(base::ieee754::acosh(k));
        case FloatUnaryOp::Kind::kTan:
          return __ Float32Constant(base::ieee754::tan(k));
        case FloatUnaryOp::Kind::kTanh:
          return __ Float32Constant(base::ieee754::tanh(k));
        case FloatUnaryOp::Kind::kLog2:
          return __ Float32Constant(base::ieee754::log2(k));
        case FloatUnaryOp::Kind::kLog10:
          return __ Float32Constant(base::ieee754::log10(k));
        case FloatUnaryOp::Kind::kLog1p:
          return __ Float32Constant(base::ieee754::log1p(k));
        case FloatUnaryOp::Kind::kCbrt:
          return __ Float32Constant(base::ieee754::cbrt(k));
        case FloatUnaryOp::Kind::kAtan:
          return __ Float32Constant(base::ieee754::atan(k));
        case FloatUnaryOp::Kind::kAtanh:
          return __ Float32Constant(base::ieee754::atanh(k));
      }
    } else if (double k; rep == FloatRepresentation::Float64() &&
                         matcher.MatchFloat64Constant(input, &k)) {
      if (std::isnan(k)) {
        return __ Float64Constant(std::numeric_limits<double>::quiet_NaN());
      }
      switch (kind) {
        case FloatUnaryOp::Kind::kAbs:
          return __ Float64Constant(std::abs(k));
        case FloatUnaryOp::Kind::kNegate:
          return __ Float64Constant(-k);
        case FloatUnaryOp::Kind::kSilenceNaN:
          DCHECK(!std::isnan(k));
          return __ Float64Constant(k);
        case FloatUnaryOp::Kind::kRoundDown:
          return __ Float64Constant(std::floor(k));
        case FloatUnaryOp::Kind::kRoundUp:
          return __ Float64Constant(std::ceil(k));
        case FloatUnaryOp::Kind::kRoundToZero:
          return __ Float64Constant(std::trunc(k));
        case FloatUnaryOp::Kind::kRoundTiesEven:
          DCHECK_EQ(std::nearbyint(1.5), 2);
          DCHECK_EQ(std::nearbyint(2.5), 2);
          return __ Float64Constant(std::nearbyint(k));
        case FloatUnaryOp::Kind::kLog:
          return __ Float64Constant(base::ieee754::log(k));
        case FloatUnaryOp::Kind::kSqrt:
          return __ Float64Constant(std::sqrt(k));
        case FloatUnaryOp::Kind::kExp:
          return __ Float64Constant(base::ieee754::exp(k));
        case FloatUnaryOp::Kind::kExpm1:
          return __ Float64Constant(base::ieee754::expm1(k));
        case FloatUnaryOp::Kind::kSin:
          return __ Float64Constant(SIN_IMPL(k));
        case FloatUnaryOp::Kind::kCos:
          return __ Float64Constant(COS_IMPL(k));
        case FloatUnaryOp::Kind::kSinh:
          return __ Float64Constant(base::ieee754::sinh(k));
        case FloatUnaryOp::Kind::kCosh:
          return __ Float64Constant(base::ieee754::cosh(k));
        case FloatUnaryOp::Kind::kAcos:
          return __ Float64Constant(base::ieee754::acos(k));
        case FloatUnaryOp::Kind::kAsin:
          return __ Float64Constant(base::ieee754::asin(k));
        case FloatUnaryOp::Kind::kAsinh:
          return __ Float64Constant(base::ieee754::asinh(k));
        case FloatUnaryOp::Kind::kAcosh:
          return __ Float64Constant(base::ieee754::acosh(k));
        case FloatUnaryOp::Kind::kTan:
          return __ Float64Constant(base::ieee754::tan(k));
        case FloatUnaryOp::Kind::kTanh:
          return __ Float64Constant(base::ieee754::tanh(k));
        case FloatUnaryOp::Kind::kLog2:
          return __ Float64Constant(base::ieee754::log2(k));
        case FloatUnaryOp::Kind::kLog10:
          return __ Float64Constant(base::ieee754::log10(k));
        case FloatUnaryOp::Kind::kLog1p:
          return __ Float64Constant(base::ieee754::log1p(k));
        case FloatUnaryOp::Kind::kCbrt:
          return __ Float64Constant(base::ieee754::cbrt(k));
        case FloatUnaryOp::Kind::kAtan:
          return __ Float64Constant(base::ieee754::atan(k));
        case FloatUnaryOp::Kind::kAtanh:
          return __ Float64Constant(base::ieee754::atanh(k));
      }
    }
    return Next::ReduceFloatUnary(input, kind, rep);
  }

  OpIndex REDUCE(WordUnary)(OpIndex input, WordUnaryOp::Kind kind,
                            WordRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceWordUnary(input, kind, rep);
    }
    if (rep == WordRepresentation::Word32()) {
      input = TryRemoveWord32ToWord64Conversion(input);
    }
    if (uint32_t k; rep == WordRepresentation::Word32() &&
                    matcher.MatchIntegralWord32Constant(input, &k)) {
      switch (kind) {
        case WordUnaryOp::Kind::kReverseBytes:
          return __ Word32Constant(base::bits::ReverseBytes(k));
        case WordUnaryOp::Kind::kCountLeadingZeros:
          return __ Word32Constant(base::bits::CountLeadingZeros(k));
        case WordUnaryOp::Kind::kCountTrailingZeros:
          return __ Word32Constant(base::bits::CountTrailingZeros(k));
        case WordUnaryOp::Kind::kPopCount:
          return __ Word32Constant(base::bits::CountPopulation(k));
        case WordUnaryOp::Kind::kSignExtend8:
          return __ Word32Constant(int32_t{static_cast<int8_t>(k)});
        case WordUnaryOp::Kind::kSignExtend16:
          return __ Word32Constant(int32_t{static_cast<int16_t>(k)});
      }
    } else if (uint64_t k; rep == WordRepresentation::Word64() &&
                           matcher.MatchIntegralWord64Constant(input, &k)) {
      switch (kind) {
        case WordUnaryOp::Kind::kReverseBytes:
          return __ Word64Constant(base::bits::ReverseBytes(k));
        case WordUnaryOp::Kind::kCountLeadingZeros:
          return __ Word64Constant(uint64_t{base::bits::CountLeadingZeros(k)});
        case WordUnaryOp::Kind::kCountTrailingZeros:
          return __ Word64Constant(uint64_t{base::bits::CountTrailingZeros(k)});
        case WordUnaryOp::Kind::kPopCount:
          return __ Word64Constant(uint64_t{base::bits::CountPopulation(k)});
        case WordUnaryOp::Kind::kSignExtend8:
          return __ Word64Constant(int64_t{static_cast<int8_t>(k)});
        case WordUnaryOp::Kind::kSignExtend16:
          return __ Word64Constant(int64_t{static_cast<int16_t>(k)});
      }
    }
    return Next::ReduceWordUnary(input, kind, rep);
  }

  OpIndex REDUCE(FloatBinop)(OpIndex lhs, OpIndex rhs, FloatBinopOp::Kind kind,
                             FloatRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceFloatBinop(lhs, rhs, kind, rep);
    }

    using Kind = FloatBinopOp::Kind;

    // Place constant on the right for commutative operators.
    if (FloatBinopOp::IsCommutative(kind) && matcher.Is<ConstantOp>(lhs) &&
        !matcher.Is<ConstantOp>(rhs)) {
      return ReduceFloatBinop(rhs, lhs, kind, rep);
    }

    // constant folding
    if (float k1, k2; rep == FloatRepresentation::Float32() &&
                      matcher.MatchFloat32Constant(lhs, &k1) &&
                      matcher.MatchFloat32Constant(rhs, &k2)) {
      switch (kind) {
        case Kind::kAdd:
          return __ Float32Constant(k1 + k2);
        case Kind::kMul:
          return __ Float32Constant(k1 * k2);
        case Kind::kSub:
          return __ Float32Constant(k1 - k2);
        case Kind::kMin:
          return __ Float32Constant(JSMin(k1, k2));
        case Kind::kMax:
          return __ Float32Constant(JSMax(k1, k2));
        case Kind::kDiv:
          return __ Float32Constant(k1 / k2);
        case Kind::kPower:
          return __ Float32Constant(base::ieee754::pow(k1, k2));
        case Kind::kAtan2:
          return __ Float32Constant(base::ieee754::atan2(k1, k2));
        case Kind::kMod:
          UNREACHABLE();
      }
    }
    if (double k1, k2; rep == FloatRepresentation::Float64() &&
                       matcher.MatchFloat64Constant(lhs, &k1) &&
                       matcher.MatchFloat64Constant(rhs, &k2)) {
      switch (kind) {
        case Kind::kAdd:
          return __ Float64Constant(k1 + k2);
        case Kind::kMul:
          return __ Float64Constant(k1 * k2);
        case Kind::kSub:
          return __ Float64Constant(k1 - k2);
        case Kind::kMin:
          return __ Float64Constant(JSMin(k1, k2));
        case Kind::kMax:
          return __ Float64Constant(JSMax(k1, k2));
        case Kind::kDiv:
          return __ Float64Constant(k1 / k2);
        case Kind::kMod:
          return __ Float64Constant(Modulo(k1, k2));
        case Kind::kPower:
          return __ Float64Constant(base::ieee754::pow(k1, k2));
        case Kind::kAtan2:
          return __ Float64Constant(base::ieee754::atan2(k1, k2));
      }
    }

    // lhs <op> NaN  =>  NaN
    if (matcher.MatchNaN(rhs) ||
        (matcher.MatchNaN(lhs) && kind != Kind::kPower)) {
      // Return a quiet NaN since Wasm operations could have signalling NaN as
      // input but not as output.
      return __ FloatConstant(std::numeric_limits<double>::quiet_NaN(), rep);
    }

    if (matcher.Is<ConstantOp>(rhs)) {
      if (kind == Kind::kMul) {
        // lhs * 1  =>  lhs
        if (!signalling_nan_possible && matcher.MatchFloat(rhs, 1.0)) {
          return lhs;
        }
        // lhs * 2  =>  lhs + lhs
        if (matcher.MatchFloat(rhs, 2.0)) {
          return __ FloatAdd(lhs, lhs, rep);
        }
        // lhs * -1  =>  -lhs
        if (!signalling_nan_possible && matcher.MatchFloat(rhs, -1.0)) {
          return __ FloatNegate(lhs, rep);
        }
      }

      if (kind == Kind::kDiv) {
        // lhs / 1  =>  lhs
        if (!signalling_nan_possible && matcher.MatchFloat(rhs, 1.0)) {
          return lhs;
        }
        // lhs / -1  =>  -lhs
        if (!signalling_nan_possible && matcher.MatchFloat(rhs, -1.0)) {
          return __ FloatNegate(lhs, rep);
        }
        // All reciprocals of non-denormal powers of two can be represented
        // exactly, so division by power of two can be reduced to
        // multiplication by reciprocal, with the same result.
        // x / k  =>  x * (1 / k)
        if (rep == FloatRepresentation::Float32()) {
          if (float k;
              matcher.MatchFloat32Constant(rhs, &k) && std::isnormal(k) &&
              k != 0 && std::isfinite(k) &&
              base::bits::IsPowerOfTwo(base::Double(k).Significand())) {
            return __ FloatMul(lhs, __ FloatConstant(1.0 / k, rep), rep);
          }
        } else {
          DCHECK_EQ(rep, FloatRepresentation::Float64());
          if (double k;
              matcher.MatchFloat64Constant(rhs, &k) && std::isnormal(k) &&
              k != 0 && std::isfinite(k) &&
              base::bits::IsPowerOfTwo(base::Double(k).Significand())) {
            return __ FloatMul(lhs, __ FloatConstant(1.0 / k, rep), rep);
          }
        }
      }

      if (kind == Kind::kMod) {
        // x % 0  =>  NaN
        if (matcher.MatchFloat(rhs, 0.0)) {
          return __ FloatConstant(std::numeric_limits<double>::quiet_NaN(),
                                  rep);
        }
      }

      if (kind == Kind::kSub) {
        // lhs - +0.0  =>  lhs
        if (!signalling_nan_possible && matcher.MatchFloat(rhs, +0.0)) {
          return lhs;
        }
      }

      if (kind == Kind::kPower) {
        if (matcher.MatchFloat(rhs, 0.0) || matcher.MatchFloat(rhs, -0.0)) {
          // lhs ** 0  ==>  1
          return __ FloatConstant(1.0, rep);
        }
        if (matcher.MatchFloat(rhs, 2.0)) {
          // lhs ** 2  ==>  lhs * lhs
          return __ FloatMul(lhs, lhs, rep);
        }
        if (matcher.MatchFloat(rhs, 0.5)) {
          // lhs ** 0.5  ==>  sqrt(lhs)
          // (unless if lhs is -infinity)
          Variable result = __ NewLoopInvariantVariable(rep);
          IF (UNLIKELY(__ FloatLessThanOrEqual(
                  lhs, __ FloatConstant(-V8_INFINITY, rep), rep))) {
            __ SetVariable(result, __ FloatConstant(V8_INFINITY, rep));
          }
          ELSE {
            __ SetVariable(result, __ FloatSqrt(lhs, rep));
          }
          END_IF
          return __ GetVariable(result);
        }
      }
    }

    if (!signalling_nan_possible && kind == Kind::kSub &&
        matcher.MatchFloat(lhs, -0.0)) {
      // -0.0 - round_down(-0.0 - y) => round_up(y)
      if (OpIndex a, b, c;
          FloatUnaryOp::IsSupported(FloatUnaryOp::Kind::kRoundUp, rep) &&
          matcher.MatchFloatRoundDown(rhs, &a, rep) &&
          matcher.MatchFloatSub(a, &b, &c, rep) &&
          matcher.MatchFloat(b, -0.0)) {
        return __ FloatRoundUp(c, rep);
      }
      // -0.0 - rhs  =>  -rhs
      return __ FloatNegate(rhs, rep);
    }

    return Next::ReduceFloatBinop(lhs, rhs, kind, rep);
  }

  OpIndex REDUCE(WordBinop)(OpIndex left, OpIndex right, WordBinopOp::Kind kind,
                            WordRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceWordBinop(left, right, kind, rep);
    }

    using Kind = WordBinopOp::Kind;

    DCHECK_EQ(rep, any_of(WordRepresentation::Word32(),
                          WordRepresentation::Word64()));
    bool is_64 = rep == WordRepresentation::Word64();

    if (!is_64) {
      left = TryRemoveWord32ToWord64Conversion(left);
      right = TryRemoveWord32ToWord64Conversion(right);
    }

    // Place constant on the right for commutative operators.
    if (WordBinopOp::IsCommutative(kind) && matcher.Is<ConstantOp>(left) &&
        !matcher.Is<ConstantOp>(right)) {
      return ReduceWordBinop(right, left, kind, rep);
    }
    // constant folding
    if (uint64_t k1, k2; matcher.MatchIntegralWordConstant(left, rep, &k1) &&
                         matcher.MatchIntegralWordConstant(right, rep, &k2)) {
      switch (kind) {
        case Kind::kAdd:
          return __ WordConstant(k1 + k2, rep);
        case Kind::kMul:
          return __ WordConstant(k1 * k2, rep);
        case Kind::kBitwiseAnd:
          return __ WordConstant(k1 & k2, rep);
        case Kind::kBitwiseOr:
          return __ WordConstant(k1 | k2, rep);
        case Kind::kBitwiseXor:
          return __ WordConstant(k1 ^ k2, rep);
        case Kind::kSub:
          return __ WordConstant(k1 - k2, rep);
        case Kind::kSignedMulOverflownBits:
          return __ WordConstant(
              is_64 ? base::bits::SignedMulHigh64(static_cast<int64_t>(k1),
                                                  static_cast<int64_t>(k2))
                    : base::bits::SignedMulHigh32(static_cast<int32_t>(k1),
                                                  static_cast<int32_t>(k2)),
              rep);
        case Kind::kUnsignedMulOverflownBits:
          return __ WordConstant(
              is_64 ? base::bits::UnsignedMulHigh64(k1, k2)
                    : base::bits::UnsignedMulHigh32(static_cast<uint32_t>(k1),
                                                    static_cast<uint32_t>(k2)),
              rep);
        case Kind::kSignedDiv:
          return __ WordConstant(
              is_64 ? base::bits::SignedDiv64(k1, k2)
                    : base::bits::SignedDiv32(static_cast<int32_t>(k1),
                                              static_cast<int32_t>(k2)),
              rep);
        case Kind::kUnsignedDiv:
          return __ WordConstant(
              is_64 ? base::bits::UnsignedDiv64(k1, k2)
                    : base::bits::UnsignedDiv32(static_cast<uint32_t>(k1),
                                                static_cast<uint32_t>(k2)),
              rep);
        case Kind::kSignedMod:
          return __ WordConstant(
              is_64 ? base::bits::SignedMod64(k1, k2)
                    : base::bits::SignedMod32(static_cast<int32_t>(k1),
                                              static_cast<int32_t>(k2)),
              rep);
        case Kind::kUnsignedMod:
          return __ WordConstant(
              is_64 ? base::bits::UnsignedMod64(k1, k2)
                    : base::bits::UnsignedMod32(static_cast<uint32_t>(k1),
                                                static_cast<uint32_t>(k2)),
              rep);
      }
    }

    // TODO(tebbi): Detect and merge multiple bitfield checks for CSA/Torque
    // code.

    if (uint64_t right_value;
        matcher.MatchIntegralWordConstant(right, rep, &right_value)) {
      // TODO(jkummerow): computing {right_value_signed} could probably be
      // handled by the 4th argument to {MatchIntegralWordConstant}.
      int64_t right_value_signed =
          is_64 ? static_cast<int64_t>(right_value)
                : int64_t{static_cast<int32_t>(right_value)};
      // (a <op> k1) <op> k2  =>  a <op> (k1 <op> k2)
      if (OpIndex a, k1; WordBinopOp::IsAssociative(kind) &&
                         matcher.MatchWordBinop(left, &a, &k1, kind, rep) &&
                         matcher.Is<ConstantOp>(k1)) {
        OpIndex k2 = right;
        // This optimization allows to do constant folding of `k1` and `k2`.
        // However, if (a <op> k1) has to be calculated anyways, then constant
        // folding does not save any calculations during runtime, and it may
        // increase register pressure because it extends the lifetime of `a`.
        // Therefore we do the optimization only when `left = (a <op k1)` has no
        // other uses.
        if (matcher.Get(left).saturated_use_count.IsZero()) {
          return ReduceWordBinop(a, ReduceWordBinop(k1, k2, kind, rep), kind,
                                 rep);
        }
      }
      switch (kind) {
        case Kind::kSub:
          // left - k  => left + -k
          return ReduceWordBinop(left, __ WordConstant(-right_value, rep),
                                 Kind::kAdd, rep);
        case Kind::kAdd:
          // left + 0  =>  left
          if (right_value == 0) {
            return left;
          }
          break;
        case Kind::kBitwiseXor:
          // left ^ 0  =>  left
          if (right_value == 0) {
            return left;
          }
          // left ^ 1  =>  left == 0  if left is 0 or 1
          if (right_value == 1 && IsBit(left)) {
            return __ Word32Equal(left, __ Word32Constant(0));
          }
          // (x ^ -1) ^ -1  =>  x
          {
            OpIndex x, y;
            int64_t k;
            if (right_value_signed == -1 &&
                matcher.MatchBitwiseAnd(left, &x, &y, rep) &&
                matcher.MatchIntegralWordConstant(y, rep, &k) && k == -1) {
              return x;
            }
          }
          break;
        case Kind::kBitwiseOr:
          // left | 0  =>  left
          if (right_value == 0) {
            return left;
          }
          // left | -1  =>  -1
          if (right_value_signed == -1) {
            return right;
          }
          // (x & K1) | K2 => x | K2 if K2 has ones for every zero bit in K1.
          // This case can be constructed by UpdateWord and UpdateWord32 in CSA.
          {
            OpIndex x, y;
            uint64_t k1;
            uint64_t k2 = right_value;
            if (matcher.MatchBitwiseAnd(left, &x, &y, rep) &&
                matcher.MatchIntegralWordConstant(y, rep, &k1) &&
                (k1 | k2) == rep.MaxUnsignedValue()) {
              return __ WordBitwiseOr(x, right, rep);
            }
          }
          break;
        case Kind::kMul:
          // left * 0  =>  0
          if (right_value == 0) {
            return __ WordConstant(0, rep);
          }
          // left * 1  =>  left
          if (right_value == 1) {
            return left;
          }
          // left * -1 => 0 - left
          if (right_value_signed == -1) {
            return __ WordSub(__ WordConstant(0, rep), left, rep);
          }
          // left * 2^k  =>  left << k
          if (base::bits::IsPowerOfTwo(right_value)) {
            OpIndex shift_amount =
                __ Word32Constant(base::bits::WhichPowerOfTwo(right_value));
            return __ ShiftLeft(left, shift_amount, rep);
          }
          break;
        case Kind::kBitwiseAnd:
          // left & -1 => left
          if (right_value_signed == -1) {
            return left;
          }
          // x & 0  =>  0
          if (right_value == 0) {
            return right;
          }

          if (right_value == 1) {
            // (x + x) & 1  =>  0
            OpIndex left_ignore_extensions =
                IsWord32ConvertedToWord64(left)
                    ? UndoWord32ToWord64Conversion(left)
                    : left;
            if (OpIndex a, b;
                matcher.MatchWordAdd(left_ignore_extensions, &a, &b,
                                     WordRepresentation::Word32()) &&
                a == b) {
              return __ WordConstant(0, rep);
            }

            // CMP & 1  =>  CMP
            if (IsBit(left_ignore_extensions)) {
              return left;
            }

            // HeapObject & 1 => 1  ("& 1" is a Smi-check)
            // Note that we don't constant-fold the general case of
            // "HeapObject binop cst", because it's a bit unclear when such
            // operations would be used outside of smi-checks, and it's thus
            // unclear whether constant-folding would be safe.
            if (const ConstantOp* cst = matcher.TryCast<ConstantOp>(left)) {
              if (cst->kind ==
                  any_of(ConstantOp::Kind::kHeapObject,
                         ConstantOp::Kind::kCompressedHeapObject)) {
                return __ WordConstant(1, rep);
              }
            }
          }

          // asm.js often benefits from these transformations, to optimize out
          // unnecessary memory access alignment masks. Conventions used in
          // the comments below:
          // x, y: arbitrary values
          // K, L, M: arbitrary constants
          // (-1 << K) == mask: the right-hand side of the bitwise AND.
          if (IsNegativePowerOfTwo(right_value_signed)) {
            uint64_t mask = right_value;
            int K = base::bits::CountTrailingZeros64(mask);
            OpIndex x, y;
            {
              int L;
              //   (x << L) & (-1 << K)
              // => x << L               iff L >= K
              if (matcher.MatchConstantLeftShift(left, &x, rep, &L) && L >= K) {
                return left;
              }
            }

            if (matcher.MatchWordAdd(left, &x, &y, rep)) {
              uint64_t L;  // L == (M << K) iff (L & mask) == L.

              //    (x              + (M << K)) & (-1 << K)
              // => (x & (-1 << K)) + (M << K)
              if (matcher.MatchIntegralWordConstant(y, rep, &L) &&
                  (L & mask) == L) {
                return __ WordAdd(__ WordBitwiseAnd(x, right, rep),
                                  __ WordConstant(L, rep), rep);
              }

              //   (x1 * (M << K) + y) & (-1 << K)
              // => x1 * (M << K) + (y & (-1 << K))
              OpIndex x1, x2, y1, y2;
              if (matcher.MatchWordMul(x, &x1, &x2, rep) &&
                  matcher.MatchIntegralWordConstant(x2, rep, &L) &&
                  (L & mask) == L) {
                return __ WordAdd(x, __ WordBitwiseAnd(y, right, rep), rep);
              }
              // Same as above with swapped order:
              //    (x              + y1 * (M << K)) & (-1 << K)
              // => (x & (-1 << K)) + y1 * (M << K)
              if (matcher.MatchWordMul(y, &y1, &y2, rep) &&
                  matcher.MatchIntegralWordConstant(y2, rep, &L) &&
                  (L & mask) == L) {
                return __ WordAdd(__ WordBitwiseAnd(x, right, rep), y, rep);
              }

              //   ((x1 << K) + y) & (-1 << K)
              // => (x1 << K) + (y & (-1 << K))
              int K2;
              if (matcher.MatchConstantLeftShift(x, &x1, rep, &K2) && K2 == K) {
                return __ WordAdd(x, __ WordBitwiseAnd(y, right, rep), rep);
              }
              // Same as above with swapped order:
              //    (x +              (y1 << K)) & (-1 << K)
              // => (x & (-1 << K)) + (y1 << K)
              if (matcher.MatchConstantLeftShift(y, &y1, rep, &K2) && K2 == K) {
                return __ WordAdd(__ WordBitwiseAnd(x, right, rep), y, rep);
              }
            } else if (matcher.MatchWordMul(left, &x, &y, rep)) {
              // (x * (M << K)) & (-1 << K) => x * (M << K)
              uint64_t L;  // L == (M << K) iff (L & mask) == L.
              if (matcher.MatchIntegralWordConstant(y, rep, &L) &&
                  (L & mask) == L) {
                return left;
              }
            }
          }
          break;
        case WordBinopOp::Kind::kSignedDiv:
          return ReduceSignedDiv(left, right_value_signed, rep);
        case WordBinopOp::Kind::kUnsignedDiv:
          return ReduceUnsignedDiv(left, right_value, rep);
        case WordBinopOp::Kind::kSignedMod:
          // left % 0  =>  0
          // left % 1  =>  0
          // left % -1  =>  0
          if (right_value_signed == any_of(0, 1, -1)) {
            return __ WordConstant(0, rep);
          }
          if (right_value_signed != rep.MinSignedValue()) {
            right_value_signed = Abs(right_value_signed);
          }
          // left % 2^n  =>  ((left + m) & (2^n - 1)) - m
          // where m = (left >> bits-1) >>> bits-n
          // This is a branch-free version of the following:
          // left >= 0 ? left & (2^n - 1)
          //           : ((left + (2^n - 1)) & (2^n - 1)) - (2^n - 1)
          // Adding and subtracting (2^n - 1) before and after the bitwise-and
          // keeps the result congruent modulo 2^n, but shifts the resulting
          // value range to become -(2^n - 1) ... 0.
          if (base::bits::IsPowerOfTwo(right_value_signed)) {
            uint32_t bits = rep.bit_width();
            uint32_t n = base::bits::WhichPowerOfTwo(right_value_signed);
            OpIndex m = __ ShiftRightLogical(
                __ ShiftRightArithmetic(left, bits - 1, rep), bits - n, rep);
            return __ WordSub(
                __ WordBitwiseAnd(__ WordAdd(left, m, rep),
                                  __ WordConstant(right_value_signed - 1, rep),
                                  rep),
                m, rep);
          }
          // The `IntDiv` with a constant right-hand side will be turned into a
          // multiplication, avoiding the expensive integer division.
          return __ WordSub(
              left, __ WordMul(__ IntDiv(left, right, rep), right, rep), rep);
        case WordBinopOp::Kind::kUnsignedMod:
          // left % 0  =>  0
          // left % 1  =>  0
          if (right_value == 0 || right_value == 1) {
            return __ WordConstant(0, rep);
          }
          // x % 2^n => x & (2^n - 1)
          if (base::bits::IsPowerOfTwo(right_value)) {
            return __ WordBitwiseAnd(
                left, __ WordConstant(right_value - 1, rep), rep);
          }
          // The `UintDiv` with a constant right-hand side will be turned into a
          // multiplication, avoiding the expensive integer division.
          return __ WordSub(
              left, __ WordMul(right, __ UintDiv(left, right, rep), rep), rep);
        case WordBinopOp::Kind::kSignedMulOverflownBits:
        case WordBinopOp::Kind::kUnsignedMulOverflownBits:
          break;
      }
    }

    if (kind == Kind::kAdd) {
      OpIndex x, y, zero;
      // (0 - x) + y => y - x
      if (matcher.MatchWordSub(left, &zero, &x, rep) &&
          matcher.MatchZero(zero)) {
        y = right;
        return __ WordSub(y, x, rep);
      }
      // x + (0 - y) => x - y
      if (matcher.MatchWordSub(right, &zero, &y, rep) &&
          matcher.MatchZero(zero)) {
        x = left;
        return __ WordSub(x, y, rep);
      }
    }

    // 0 / right  =>  0
    // 0 % right  =>  0
    if (matcher.MatchZero(left) &&
        kind == any_of(Kind::kSignedDiv, Kind::kUnsignedDiv, Kind::kUnsignedMod,
                       Kind::kSignedMod)) {
      return __ WordConstant(0, rep);
    }

    if (left == right) {
      OpIndex x = left;
      switch (kind) {
        // x & x  =>  x
        // x | x  =>  x
        case WordBinopOp::Kind::kBitwiseAnd:
        case WordBinopOp::Kind::kBitwiseOr:
          return x;
        // x ^ x  =>  0
        // x - x  =>  0
        // x % x  =>  0
        case WordBinopOp::Kind::kBitwiseXor:
        case WordBinopOp::Kind::kSub:
        case WordBinopOp::Kind::kSignedMod:
        case WordBinopOp::Kind::kUnsignedMod:
          return __ WordConstant(0, rep);
        // x / x  =>  x != 0
        case WordBinopOp::Kind::kSignedDiv:
        case WordBinopOp::Kind::kUnsignedDiv: {
          OpIndex zero = __ WordConstant(0, rep);
          V<Word32> result = __ Word32Equal(__ Equal(left, zero, rep), 0);
          return __ ZeroExtendWord32ToRep(result, rep);
        }
        case WordBinopOp::Kind::kAdd:
        case WordBinopOp::Kind::kMul:
        case WordBinopOp::Kind::kSignedMulOverflownBits:
        case WordBinopOp::Kind::kUnsignedMulOverflownBits:
          break;
      }
    }

    if (base::Optional<OpIndex> ror = TryReduceToRor(left, right, kind, rep)) {
      return *ror;
    }

    return Next::ReduceWordBinop(left, right, kind, rep);
  }

  base::Optional<OpIndex> TryReduceToRor(OpIndex left, OpIndex right,
                                         WordBinopOp::Kind kind,
                                         WordRepresentation rep) {
    // Recognize rotation, we are matcher.Matching and transforming as follows
    // (assuming kWord32, kWord64 is handled correspondingly):
    //   x << y         |  x >>> (32 - y)    =>  x ror (32 - y)
    //   x << (32 - y)  |  x >>> y           =>  x ror y
    //   x << y         ^  x >>> (32 - y)    =>  x ror (32 - y)   if 1 <= y < 32
    //   x << (32 - y)  ^  x >>> y           =>  x ror y          if 1 <= y < 32
    // (As well as the commuted forms.)
    // Note the side condition for XOR: the optimization doesn't hold for
    // an effective rotation amount of 0.

    if (!(kind == any_of(WordBinopOp::Kind::kBitwiseOr,
                         WordBinopOp::Kind::kBitwiseXor))) {
      return {};
    }

    const ShiftOp* high = matcher.TryCast<ShiftOp>(left);
    if (!high) return {};
    const ShiftOp* low = matcher.TryCast<ShiftOp>(right);
    if (!low) return {};

    if (low->kind == ShiftOp::Kind::kShiftLeft) {
      std::swap(low, high);
    }
    if (high->kind != ShiftOp::Kind::kShiftLeft ||
        low->kind != ShiftOp::Kind::kShiftRightLogical) {
      return {};
    }
    OpIndex x = high->left();
    if (low->left() != x) return {};
    OpIndex amount;
    uint64_t k;
    if (OpIndex a, b; matcher.MatchWordSub(high->right(), &a, &b, rep) &&
                      matcher.MatchIntegralWordConstant(a, rep, &k) &&
                      b == low->right() && k == rep.bit_width()) {
      amount = b;
    } else if (OpIndex a, b; matcher.MatchWordSub(low->right(), &a, &b, rep) &&
                             a == high->right() &&
                             matcher.MatchIntegralWordConstant(b, rep, &k) &&
                             k == rep.bit_width()) {
      amount = low->right();
    } else if (uint64_t k1, k2;
               matcher.MatchIntegralWordConstant(high->right(), rep, &k1) &&
               matcher.MatchIntegralWordConstant(low->right(), rep, &k2) &&
               k1 + k2 == rep.bit_width() && k1 >= 0 && k2 >= 0) {
      if (k1 == 0 || k2 == 0) {
        if (kind == WordBinopOp::Kind::kBitwiseXor) {
          return __ WordConstant(0, rep);
        } else {
          DCHECK_EQ(kind, WordBinopOp::Kind::kBitwiseOr);
          return x;
        }
      }
      return __ RotateRight(x, low->right(), rep);
    } else {
      return {};
    }
    if (kind == WordBinopOp::Kind::kBitwiseOr) {
      return __ RotateRight(x, amount, rep);
    } else {
      DCHECK_EQ(kind, WordBinopOp::Kind::kBitwiseXor);
      // Can't guarantee that rotation amount is not 0.
      return {};
    }
  }

  OpIndex REDUCE(OverflowCheckedBinop)(OpIndex left, OpIndex right,
                                       OverflowCheckedBinopOp::Kind kind,
                                       WordRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceOverflowCheckedBinop(left, right, kind, rep);
    }
    using Kind = OverflowCheckedBinopOp::Kind;
    if (OverflowCheckedBinopOp::IsCommutative(kind) &&
        matcher.Is<ConstantOp>(left) && !matcher.Is<ConstantOp>(right)) {
      return ReduceOverflowCheckedBinop(right, left, kind, rep);
    }
    if (rep == WordRepresentation::Word32()) {
      left = TryRemoveWord32ToWord64Conversion(left);
      right = TryRemoveWord32ToWord64Conversion(right);
    }
    // constant folding
    if (rep == WordRepresentation::Word32()) {
      if (int32_t k1, k2; matcher.MatchIntegralWord32Constant(left, &k1) &&
                          matcher.MatchIntegralWord32Constant(right, &k2)) {
        bool overflow;
        int32_t res;
        switch (kind) {
          case OverflowCheckedBinopOp::Kind::kSignedAdd:
            overflow = base::bits::SignedAddOverflow32(k1, k2, &res);
            break;
          case OverflowCheckedBinopOp::Kind::kSignedMul:
            overflow = base::bits::SignedMulOverflow32(k1, k2, &res);
            break;
          case OverflowCheckedBinopOp::Kind::kSignedSub:
            overflow = base::bits::SignedSubOverflow32(k1, k2, &res);
            break;
        }
        return __ Tuple(__ Word32Constant(res), __ Word32Constant(overflow));
      }
    } else {
      DCHECK_EQ(rep, WordRepresentation::Word64());
      if (int64_t k1, k2; matcher.MatchIntegralWord64Constant(left, &k1) &&
                          matcher.MatchIntegralWord64Constant(right, &k2)) {
        bool overflow;
        int64_t res;
        switch (kind) {
          case OverflowCheckedBinopOp::Kind::kSignedAdd:
            overflow = base::bits::SignedAddOverflow64(k1, k2, &res);
            break;
          case OverflowCheckedBinopOp::Kind::kSignedMul:
            overflow = base::bits::SignedMulOverflow64(k1, k2, &res);
            break;
          case OverflowCheckedBinopOp::Kind::kSignedSub:
            overflow = base::bits::SignedSubOverflow64(k1, k2, &res);
            break;
        }
        return __ Tuple(__ Word64Constant(res), __ Word32Constant(overflow));
      }
    }

    // left + 0  =>  (left, false)
    // left - 0  =>  (left, false)
    if (kind == any_of(Kind::kSignedAdd, Kind::kSignedSub) &&
        matcher.MatchZero(right)) {
      return __ Tuple(left, right);
    }

    if (kind == Kind::kSignedMul) {
      if (int64_t k; matcher.MatchIntegralWordConstant(right, rep, &k)) {
        // left * 0  =>  (0, false)
        if (k == 0) {
          return __ Tuple(__ WordConstant(0, rep), __ Word32Constant(false));
        }
        // left * 1  =>  (left, false)
        if (k == 1) {
          return __ Tuple(left, __ Word32Constant(false));
        }
        // left * -1  =>  0 - left
        if (k == -1) {
          return __ IntSubCheckOverflow(__ WordConstant(0, rep), left, rep);
        }
        // left * 2  =>  left + left
        if (k == 2) {
          return __ IntAddCheckOverflow(left, left, rep);
        }
      }
    }

    // UntagSmi(x) + UntagSmi(x)  =>  (x, false)
    // (where UntagSmi(x) = x >> 1   with a ShiftOutZeros shift)
    if (kind == Kind::kSignedAdd && left == right) {
      uint16_t amount;
      if (OpIndex x; matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                         left, &x, WordRepresentation::Word32(), &amount) &&
                     amount == 1) {
        return __ Tuple(x, __ Word32Constant(0));
      }
    }

    return Next::ReduceOverflowCheckedBinop(left, right, kind, rep);
  }

  OpIndex REDUCE(Comparison)(OpIndex left, OpIndex right,
                             ComparisonOp::Kind kind,
                             RegisterRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceComparison(left, right, kind, rep);
    }
    if (kind == ComparisonOp::Kind::kEqual) {
      return ReduceCompareEqual(left, right, rep);
    }
    if (rep == WordRepresentation::Word32()) {
      left = TryRemoveWord32ToWord64Conversion(left);
      right = TryRemoveWord32ToWord64Conversion(right);
    }
    using Kind = ComparisonOp::Kind;
    if (left == right &&
        !(rep == any_of(RegisterRepresentation::Float32(),
                        RegisterRepresentation::Float64())) &&
        kind == any_of(Kind::kSignedLessThanOrEqual,
                       Kind::kUnsignedLessThanOrEqual)) {
      switch (kind) {
        case Kind::kEqual:
          UNREACHABLE();
        case Kind::kUnsignedLessThanOrEqual:
        case Kind::kSignedLessThanOrEqual:
          return __ Word32Constant(1);
        case Kind::kUnsignedLessThan:
        case Kind::kSignedLessThan:
          return __ Word32Constant(0);
      }
    }
    // constant folding
    if (matcher.Is<ConstantOp>(right) && matcher.Is<ConstantOp>(left)) {
      switch (rep.value()) {
        case RegisterRepresentation::Word32():
        case RegisterRepresentation::Word64(): {
          if (kind ==
              any_of(Kind::kSignedLessThan, Kind::kSignedLessThanOrEqual)) {
            if (int64_t k1, k2; matcher.MatchIntegralWordConstant(
                                    left, WordRepresentation(rep), &k1) &&
                                matcher.MatchIntegralWordConstant(
                                    right, WordRepresentation(rep), &k2)) {
              switch (kind) {
                case ComparisonOp::Kind::kSignedLessThan:
                  return __ Word32Constant(k1 < k2);
                case ComparisonOp::Kind::kSignedLessThanOrEqual:
                  return __ Word32Constant(k1 <= k2);
                case ComparisonOp::Kind::kEqual:
                case ComparisonOp::Kind::kUnsignedLessThan:
                case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
                  UNREACHABLE();
              }
            }
          } else {
            if (uint64_t k1, k2; matcher.MatchIntegralWordConstant(
                                     left, WordRepresentation(rep), &k1) &&
                                 matcher.MatchIntegralWordConstant(
                                     right, WordRepresentation(rep), &k2)) {
              switch (kind) {
                case ComparisonOp::Kind::kUnsignedLessThan:
                  return __ Word32Constant(k1 < k2);
                case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
                  return __ Word32Constant(k1 <= k2);
                case ComparisonOp::Kind::kEqual:
                case ComparisonOp::Kind::kSignedLessThan:
                case ComparisonOp::Kind::kSignedLessThanOrEqual:
                  UNREACHABLE();
              }
            }
          }
          break;
        }
        case RegisterRepresentation::Float32(): {
          if (float k1, k2; matcher.MatchFloat32Constant(left, &k1) &&
                            matcher.MatchFloat32Constant(right, &k2)) {
            switch (kind) {
              case ComparisonOp::Kind::kSignedLessThan:
                return __ Word32Constant(k1 < k2);
              case ComparisonOp::Kind::kSignedLessThanOrEqual:
                return __ Word32Constant(k1 <= k2);
              case ComparisonOp::Kind::kEqual:
              case ComparisonOp::Kind::kUnsignedLessThan:
              case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
                UNREACHABLE();
            }
          }
          break;
        }
        case RegisterRepresentation::Float64(): {
          if (double k1, k2; matcher.MatchFloat64Constant(left, &k1) &&
                             matcher.MatchFloat64Constant(right, &k2)) {
            switch (kind) {
              case ComparisonOp::Kind::kSignedLessThan:
                return __ Word32Constant(k1 < k2);
              case ComparisonOp::Kind::kSignedLessThanOrEqual:
                return __ Word32Constant(k1 <= k2);
              case ComparisonOp::Kind::kEqual:
              case ComparisonOp::Kind::kUnsignedLessThan:
              case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
                UNREACHABLE();
            }
          }
          break;
        }
        default:
          UNREACHABLE();
      }
    }
    if (rep == RegisterRepresentation::Float64() &&
        IsFloat32ConvertedToFloat64(left) &&
        IsFloat32ConvertedToFloat64(right)) {
      return __ Comparison(UndoFloat32ToFloat64Conversion(left),
                           UndoFloat32ToFloat64Conversion(right), kind,
                           RegisterRepresentation::Float32());
    }
    if (rep.IsWord()) {
      WordRepresentation rep_w{rep};
      if (kind == Kind::kUnsignedLessThanOrEqual) {
        // 0 <= x  =>  true
        if (uint64_t k;
            matcher.MatchIntegralWordConstant(left, rep_w, &k) && k == 0) {
          return __ Word32Constant(1);
        }
        // x <= MaxUint  =>  true
        if (uint64_t k; matcher.MatchIntegralWordConstant(right, rep_w, &k) &&
                        k == rep.MaxUnsignedValue()) {
          return __ Word32Constant(1);
        }
        // x <= 0  =>  x == 0
        if (uint64_t k;
            matcher.MatchIntegralWordConstant(right, rep_w, &k) && k == 0) {
          return __ Equal(left, __ WordConstant(0, rep_w), rep_w);
        }
      }
      if (kind == Kind::kUnsignedLessThan) {
        // x < 0  =>  false
        if (uint64_t k;
            matcher.MatchIntegralWordConstant(right, rep_w, &k) && k == 0) {
          return __ Word32Constant(0);
        }
        // MaxUint < x  =>  true
        if (uint64_t k; matcher.MatchIntegralWordConstant(left, rep_w, &k) &&
                        k == rep.MaxUnsignedValue()) {
          return __ Word32Constant(0);
        }
      }
      {
        // (x >> k) </<=  (y >> k)  =>  x </<=  y   if shifts reversible
        OpIndex x, y;
        uint16_t k1, k2;
        if (matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                left, &x, rep_w, &k1) &&
            matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                right, &y, rep_w, &k2) &&
            k1 == k2) {
          return __ Comparison(x, y, kind, rep_w);
        }
      }
      {
        // (x >> k1) </<= k2  =>  x </<= (k2 << k1)  if shifts reversible
        // Only perform the transformation if the shift is not used yet, to
        // avoid keeping both the shift and x alive.
        OpIndex x;
        uint16_t k1;
        int64_t k2;
        if (matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                left, &x, rep_w, &k1) &&
            matcher.MatchIntegralWordConstant(right, rep_w, &k2) &&
            CountLeadingSignBits(k2, rep_w) > k1) {
          if (matcher.Get(left).saturated_use_count.IsZero()) {
            return __ Comparison(
                x, __ WordConstant(base::bits::Unsigned(k2) << k1, rep_w), kind,
                rep_w);
          } else if constexpr (reducer_list_contains<
                                   ReducerList, ValueNumberingReducer>::value) {
            // If the shift has uses, we only apply the transformation if the
            // result would be GVNed away.
            OpIndex rhs =
                __ WordConstant(base::bits::Unsigned(k2) << k1, rep_w);
            static_assert(ComparisonOp::input_count == 2);
            static_assert(sizeof(ComparisonOp) == 8);
            base::SmallVector<OperationStorageSlot, 32> storage;
            ComparisonOp* cmp =
                CreateOperation<ComparisonOp>(storage, x, rhs, kind, rep_w);
            if (__ WillGVNOp(*cmp)) {
              return __ Comparison(x, rhs, kind, rep_w);
            }
          }
        }
        // k2 </<= (x >> k1)  =>  (k2 << k1) </<= x  if shifts reversible
        // Only perform the transformation if the shift is not used yet, to
        // avoid keeping both the shift and x alive.
        if (matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                right, &x, rep_w, &k1) &&
            matcher.MatchIntegralWordConstant(left, rep_w, &k2) &&
            CountLeadingSignBits(k2, rep_w) > k1) {
          if (matcher.Get(right).saturated_use_count.IsZero()) {
            return __ Comparison(
                __ WordConstant(base::bits::Unsigned(k2) << k1, rep_w), x, kind,
                rep_w);
          } else if constexpr (reducer_list_contains<
                                   ReducerList, ValueNumberingReducer>::value) {
            // If the shift has uses, we only apply the transformation if the
            // result would be GVNed away.
            OpIndex lhs =
                __ WordConstant(base::bits::Unsigned(k2) << k1, rep_w);
            static_assert(ComparisonOp::input_count == 2);
            static_assert(sizeof(ComparisonOp) == 8);
            base::SmallVector<OperationStorageSlot, 32> storage;
            ComparisonOp* cmp =
                CreateOperation<ComparisonOp>(storage, lhs, x, kind, rep_w);
            if (__ WillGVNOp(*cmp)) {
              return __ Comparison(lhs, x, kind, rep_w);
            }
          }
        }
      }
      // Map 64bit to 32bit comparisons.
      if (rep_w == WordRepresentation::Word64()) {
        base::Optional<bool> left_sign_extended;
        base::Optional<bool> right_sign_extended;
        if (IsWord32ConvertedToWord64(left, &left_sign_extended) &&
            IsWord32ConvertedToWord64(right, &right_sign_extended)) {
          if (left_sign_extended != true && right_sign_extended != true) {
            // Both sides were zero-extended, so the resulting comparison always
            // behaves unsigned even if it was a signed 64bit comparison.
            auto SetSigned = [](Kind kind, bool is_signed) {
              switch (kind) {
                case Kind::kSignedLessThan:
                case Kind::kUnsignedLessThan:
                  return is_signed ? Kind::kSignedLessThan
                                   : Kind::kUnsignedLessThan;
                case Kind::kSignedLessThanOrEqual:
                case Kind::kUnsignedLessThanOrEqual:
                  return is_signed ? Kind::kSignedLessThanOrEqual
                                   : Kind::kUnsignedLessThanOrEqual;
                case Kind::kEqual:
                  UNREACHABLE();
              }
            };
            return __ Comparison(UndoWord32ToWord64Conversion(left),
                                 UndoWord32ToWord64Conversion(right),
                                 SetSigned(kind, false),
                                 WordRepresentation::Word32());
          } else if (left_sign_extended != false &&
                     right_sign_extended != false) {
            // Both sides were sign-extended, this preserves both signed and
            // unsigned comparisons.
            return __ Comparison(UndoWord32ToWord64Conversion(left),
                                 UndoWord32ToWord64Conversion(right), kind,
                                 WordRepresentation::Word32());
          }
        }
      }
    }
    return Next::ReduceComparison(left, right, kind, rep);
  }

  OpIndex REDUCE(Shift)(OpIndex left, OpIndex right, ShiftOp::Kind kind,
                        WordRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceShift(left, right, kind, rep);
    }
    using Kind = ShiftOp::Kind;
    uint64_t c_unsigned;
    int64_t c_signed;
    if (matcher.MatchIntegralWordConstant(left, rep, &c_unsigned, &c_signed)) {
      if (uint32_t amount;
          matcher.MatchIntegralWord32Constant(right, &amount)) {
        amount = amount & (rep.bit_width() - 1);
        switch (kind) {
          case Kind::kShiftRightArithmeticShiftOutZeros:
            if (base::bits::CountTrailingZeros(c_signed) < amount) {
              // This assumes that we never hoist operations to before their
              // original place in the control flow.
              __ Unreachable();
              return OpIndex::Invalid();
            }
            [[fallthrough]];
          case Kind::kShiftRightArithmetic:
            switch (rep.value()) {
              case WordRepresentation::Word32():
                return __ Word32Constant(static_cast<int32_t>(c_signed) >>
                                         amount);
              case WordRepresentation::Word64():
                return __ Word64Constant(c_signed >> amount);
            }
          case Kind::kShiftRightLogical:
            switch (rep.value()) {
              case WordRepresentation::Word32():
                return __ Word32Constant(static_cast<uint32_t>(c_unsigned) >>
                                         amount);
              case WordRepresentation::Word64():
                return __ Word64Constant(c_unsigned >> amount);
            }
          case Kind::kShiftLeft:
            return __ WordConstant(c_unsigned << amount, rep);
          case Kind::kRotateRight:
            switch (rep.value()) {
              case WordRepresentation::Word32():
                return __ Word32Constant(base::bits::RotateRight32(
                    static_cast<uint32_t>(c_unsigned), amount));
              case WordRepresentation::Word64():
                return __ Word64Constant(
                    base::bits::RotateRight64(c_unsigned, amount));
            }
          case Kind::kRotateLeft:
            switch (rep.value()) {
              case WordRepresentation::Word32():
                return __ Word32Constant(base::bits::RotateLeft32(
                    static_cast<uint32_t>(c_unsigned), amount));
              case WordRepresentation::Word64():
                return __ Word64Constant(
                    base::bits::RotateLeft64(c_unsigned, amount));
            }
        }
      }
    }
    if (int32_t amount; matcher.MatchIntegralWord32Constant(right, &amount) &&
                        0 <= amount && amount < rep.bit_width()) {
      if (amount == 0) {
        return left;
      }
      if (kind == Kind::kShiftLeft) {
        // If x >> K only shifted out zeros:
        // (x >> K) << L => x           if K == L
        // (x >> K) << L => x >> (K-L) if K > L
        // (x >> K) << L => x << (L-K)  if K < L
        // Since this is used for Smi untagging, we currently only need it for
        // signed shifts.
        int k;
        OpIndex x;
        if (matcher.MatchConstantShift(
                left, &x, Kind::kShiftRightArithmeticShiftOutZeros, rep, &k)) {
          int32_t l = amount;
          if (k == l) {
            return x;
          } else if (k > l) {
            return __ ShiftRightArithmeticShiftOutZeros(
                x, __ Word32Constant(k - l), rep);
          } else if (k < l) {
            return __ ShiftLeft(x, __ Word32Constant(l - k), rep);
          }
        }
        // (x >>> K) << K => x & ~(2^K - 1)
        // (x >> K) << K => x & ~(2^K - 1)
        if (matcher.MatchConstantRightShift(left, &x, rep, &k) && k == amount) {
          return __ WordBitwiseAnd(
              x, __ WordConstant(rep.MaxUnsignedValue() << k, rep), rep);
        }
      }
      if (kind == any_of(Kind::kShiftRightArithmetic,
                         Kind::kShiftRightArithmeticShiftOutZeros)) {
        OpIndex x;
        int left_shift_amount;
        // (x << k) >> k
        if (matcher.MatchConstantShift(left, &x, ShiftOp::Kind::kShiftLeft, rep,
                                       &left_shift_amount) &&
            amount == left_shift_amount) {
          // x << (bit_width - 1) >> (bit_width - 1)  =>  0 - x  if x is 0 or 1
          if (amount == rep.bit_width() - 1 && IsBit(x)) {
            return __ WordSub(__ WordConstant(0, rep), x, rep);
          }
          // x << (bit_width - 8) >> (bit_width - 8)  =>  x  if x is within Int8
          if (amount <= rep.bit_width() - 8 && IsInt8(x)) {
            return x;
          }
          // x << (bit_width - 8) >> (bit_width - 8)  =>  x  if x is within Int8
          if (amount <= rep.bit_width() - 16 && IsInt16(x)) {
            return x;
          }
        }
      }
      if (rep == WordRepresentation::Word32() &&
          SupportedOperations::word32_shift_is_safe()) {
        // Remove the explicit 'and' with 0x1F if the shift provided by the
        // machine instruction matcher.Matches that required by JavaScript.
        if (OpIndex a, b; matcher.MatchBitwiseAnd(
                right, &a, &b, WordRepresentation::Word32())) {
#if defined(__clang__)
          static_assert(0x1f == WordRepresentation::Word32().bit_width() - 1);
#endif
          if (uint32_t b_value;
              matcher.MatchIntegralWord32Constant(b, &b_value) &&
              b_value == 0x1f) {
            return __ Shift(left, a, kind, rep);
          }
        }
      }
    }
    return Next::ReduceShift(left, right, kind, rep);
  }

  OpIndex REDUCE(Branch)(OpIndex condition, Block* if_true, Block* if_false,
                         BranchHint hint) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceBranch(condition, if_true, if_false, hint);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    // Try to replace the Branch by a Goto.
    if (base::Optional<bool> decision = MatchBoolConstant(condition)) {
      __ Goto(*decision ? if_true : if_false);
      return OpIndex::Invalid();
    }

    // Try to simplify the Branch's condition (eg, `if (x == 0) A else B` can
    // become `if (x) B else A`).
    bool negated = false;
    if (base::Optional<OpIndex> new_condition =
            ReduceBranchCondition(condition, &negated)) {
      if (negated) {
        std::swap(if_true, if_false);
        hint = NegateBranchHint(hint);
      }

      return __ ReduceBranch(new_condition.value(), if_true, if_false, hint);
    }

    goto no_change;
  }

  OpIndex REDUCE(DeoptimizeIf)(OpIndex condition, OpIndex frame_state,
                               bool negated,
                               const DeoptimizeParameters* parameters) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceDeoptimizeIf(condition, frame_state, negated,
                                      parameters);
    }
    if (base::Optional<bool> decision = MatchBoolConstant(condition)) {
      if (*decision != negated) {
        __ Deoptimize(frame_state, parameters);
      }
      // `DeoptimizeIf` doesn't produce a value.
      return OpIndex::Invalid();
    }
    if (base::Optional<OpIndex> new_condition =
            ReduceBranchCondition(condition, &negated)) {
      return __ ReduceDeoptimizeIf(new_condition.value(), frame_state, negated,
                                   parameters);
    } else {
      return Next::ReduceDeoptimizeIf(condition, frame_state, negated,
                                      parameters);
    }
  }

#if V8_ENABLE_WEBASSEMBLY
  OpIndex REDUCE(TrapIf)(OpIndex condition, OpIndex frame_state, bool negated,
                         TrapId trap_id) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceTrapIf(condition, frame_state, negated, trap_id);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;
    if (base::Optional<bool> decision = MatchBoolConstant(condition)) {
      if (*decision != negated) {
        Next::ReduceTrapIf(condition, frame_state, negated, trap_id);
        __ Unreachable();
      }
      // `TrapIf` doesn't produce a value.
      return OpIndex::Invalid();
    }
    if (base::Optional<OpIndex> new_condition =
            ReduceBranchCondition(condition, &negated)) {
      return __ ReduceTrapIf(new_condition.value(), frame_state, negated,
                             trap_id);
    } else {
      goto no_change;
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  OpIndex REDUCE(Select)(OpIndex cond, OpIndex vtrue, OpIndex vfalse,
                         RegisterRepresentation rep, BranchHint hint,
                         SelectOp::Implementation implem) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceSelect(cond, vtrue, vfalse, rep, hint, implem);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    // Try to remove the Select.
    if (base::Optional<bool> decision = MatchBoolConstant(cond)) {
      return *decision ? vtrue : vfalse;
    }

    goto no_change;
  }

  OpIndex REDUCE(StaticAssert)(OpIndex condition, const char* source) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceStaticAssert(condition, source);
    }
    if (base::Optional<bool> decision = MatchBoolConstant(condition)) {
      if (*decision) {
        // Drop the assert, the condition holds true.
        return OpIndex::Invalid();
      } else {
        // Leave the assert, as the condition is not true.
        goto no_change;
      }
    }
    goto no_change;
  }

  OpIndex REDUCE(Switch)(OpIndex input, base::Vector<SwitchOp::Case> cases,
                         Block* default_case, BranchHint default_hint) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceSwitch(input, cases, default_case, default_hint);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;
    if (int32_t value; matcher.MatchIntegralWord32Constant(input, &value)) {
      for (const SwitchOp::Case& if_value : cases) {
        if (if_value.value == value) {
          __ Goto(if_value.destination);
          return OpIndex::Invalid();
        }
      }
      __ Goto(default_case);
      return OpIndex::Invalid();
    }
    goto no_change;
  }

  OpIndex REDUCE(Store)(OpIndex base_idx, OptionalOpIndex index, OpIndex value,
                        StoreOp::Kind kind, MemoryRepresentation stored_rep,
                        WriteBarrierKind write_barrier, int32_t offset,
                        uint8_t element_scale,
                        bool maybe_initializing_or_transitioning,
                        IndirectPointerTag maybe_indirect_pointer_tag) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceStore(base_idx, index, value, kind, stored_rep,
                               write_barrier, offset, element_scale,
                               maybe_initializing_or_transitioning,
                               maybe_indirect_pointer_tag);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;
#if V8_TARGET_ARCH_32_BIT
    if (kind.is_atomic && stored_rep.SizeInBytes() == 8) {
      // AtomicWord32PairOp (as used by Int64Lowering) cannot handle
      // element_scale != 0 currently.
      // TODO(jkummerow): Add support for element_scale in AtomicWord32PairOp.
      goto no_change;
    }
#endif
    if (stored_rep.SizeInBytes() <= 4) {
      value = TryRemoveWord32ToWord64Conversion(value);
    }
    index = ReduceMemoryIndex(index.value_or_invalid(), &offset, &element_scale,
                              kind.tagged_base);
    switch (stored_rep) {
      case MemoryRepresentation::Uint8():
      case MemoryRepresentation::Int8():
        value = ReduceWithTruncation(value, std::numeric_limits<uint8_t>::max(),
                                     WordRepresentation::Word32());
        break;
      case MemoryRepresentation::Uint16():
      case MemoryRepresentation::Int16():
        value =
            ReduceWithTruncation(value, std::numeric_limits<uint16_t>::max(),
                                 WordRepresentation::Word32());
        break;
      case MemoryRepresentation::Uint32():
      case MemoryRepresentation::Int32():
        value =
            ReduceWithTruncation(value, std::numeric_limits<uint32_t>::max(),
                                 WordRepresentation::Word32());
        break;
      default:
        break;
    }

    // If index is invalid and base is `left+right`, we use `left` as base and
    // `right` as index.
    if (!index.valid() && matcher.Is<Opmask::kWord64Add>(base_idx)) {
      DCHECK_EQ(element_scale, 0);
      const WordBinopOp& base = matcher.Cast<WordBinopOp>(base_idx);
      base_idx = base.left();
      index = base.right();
      // We go through the Store stack again, which might merge {index} into
      // {offset}, or just do other optimizations on this Store.
      __ Store(base_idx, index, value, kind, stored_rep, write_barrier, offset,
               element_scale, maybe_initializing_or_transitioning,
               maybe_indirect_pointer_tag);
      return OpIndex::Invalid();
    }

    return Next::ReduceStore(base_idx, index, value, kind, stored_rep,
                             write_barrier, offset, element_scale,
                             maybe_initializing_or_transitioning,
                             maybe_indirect_pointer_tag);
  }

  OpIndex REDUCE(Load)(OpIndex base_idx, OptionalOpIndex index,
                       LoadOp::Kind kind, MemoryRepresentation loaded_rep,
                       RegisterRepresentation result_rep, int32_t offset,
                       uint8_t element_scale) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceLoad(base_idx, index, kind, loaded_rep, result_rep,
                              offset, element_scale);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;
#if V8_TARGET_ARCH_32_BIT
    if (kind.is_atomic && loaded_rep.SizeInBytes() == 8) {
      // AtomicWord32PairOp (as used by Int64Lowering) cannot handle
      // element_scale != 0 currently.
      // TODO(jkummerow): Add support for element_scale in AtomicWord32PairOp.
      goto no_change;
    }
#endif

    while (true) {
      index = ReduceMemoryIndex(index.value_or_invalid(), &offset,
                                &element_scale, kind.tagged_base);
      if (!kind.tagged_base && !index.valid()) {
        if (OpIndex left, right;
            matcher.MatchWordAdd(base_idx, &left, &right,
                                 WordRepresentation::PointerSized()) &&
            TryAdjustOffset(&offset, matcher.Get(right), element_scale,
                            kind.tagged_base)) {
          base_idx = left;
          continue;
        }
      }
      break;
    }

    if (!index.valid() && matcher.Is<ConstantOp>(base_idx)) {
      const ConstantOp& base = matcher.Cast<ConstantOp>(base_idx);
      if (base.kind == any_of(ConstantOp::Kind::kHeapObject,
                              ConstantOp::Kind::kCompressedHeapObject)) {
        if (offset == HeapObject::kMapOffset) {
          // Only few loads should be loading the map from a ConstantOp
          // HeapObject, so unparking the JSHeapBroker here rather than before
          // the optimization pass itself it probably more efficient.

          DCHECK_IMPLIES(PipelineData::Get().pipeline_kind() !=
                             TurboshaftPipelineKind::kCSA,
                         broker != nullptr);
          if (broker != nullptr) {
            UnparkedScopeIfNeeded scope(broker);
            AllowHandleDereference allow_handle_dereference;
            OptionalMapRef map = TryMakeRef(broker, base.handle()->map());
            if (MapLoadCanBeConstantFolded(map)) {
              return __ HeapConstant(map->object());
            }
          }
        }
        // TODO(dmercadier): consider constant-folding other accesses, in
        // particular for constant objects (ie, if
        // base.handle()->InReadOnlySpace() is true). We have to be a bit
        // careful though, because loading could be invalid (since we could
        // be in unreachable code). (all objects have a map, so loading the map
        // should always be safe, regardless of whether we are generating
        // unreachable code or not)
      }
    }

    // If index is invalid and base is `left+right`, we use `left` as base and
    // `right` as index.
    if (!index.valid() && matcher.Is<Opmask::kWord64Add>(base_idx)) {
      DCHECK_EQ(element_scale, 0);
      const WordBinopOp& base = matcher.Cast<WordBinopOp>(base_idx);
      base_idx = base.left();
      index = base.right();
      // We go through the Load stack again, which might merge {index} into
      // {offset}, or just do other optimizations on this Load.
      return __ Load(base_idx, index, kind, loaded_rep, result_rep, offset,
                     element_scale);
    }

    return Next::ReduceLoad(base_idx, index, kind, loaded_rep, result_rep,
                            offset, element_scale);
  }

 private:
  OpIndex ReduceCompareEqual(OpIndex left, OpIndex right,
                             RegisterRepresentation rep) {
    if (left == right && !rep.IsFloat()) {
      return __ Word32Constant(1);
    }
    if (rep == WordRepresentation::Word32()) {
      left = TryRemoveWord32ToWord64Conversion(left);
      right = TryRemoveWord32ToWord64Conversion(right);
    }
    if (matcher.Is<ConstantOp>(left) && !matcher.Is<ConstantOp>(right)) {
      return ReduceCompareEqual(right, left, rep);
    }
    if (matcher.Is<ConstantOp>(right)) {
      if (matcher.Is<ConstantOp>(left)) {
        // k1 == k2  =>  k
        switch (rep.value()) {
          case RegisterRepresentation::Word32():
          case RegisterRepresentation::Word64(): {
            if (uint64_t k1, k2; matcher.MatchIntegralWordConstant(
                                     left, WordRepresentation(rep), &k1) &&
                                 matcher.MatchIntegralWordConstant(
                                     right, WordRepresentation(rep), &k2)) {
              return __ Word32Constant(k1 == k2);
            }
            break;
          }
          case RegisterRepresentation::Float32(): {
            if (float k1, k2; matcher.MatchFloat32Constant(left, &k1) &&
                              matcher.MatchFloat32Constant(right, &k2)) {
              return __ Word32Constant(k1 == k2);
            }
            break;
          }
          case RegisterRepresentation::Float64(): {
            if (double k1, k2; matcher.MatchFloat64Constant(left, &k1) &&
                               matcher.MatchFloat64Constant(right, &k2)) {
              return __ Word32Constant(k1 == k2);
            }
            break;
          }
          case RegisterRepresentation::Tagged(): {
            if (Handle<HeapObject> o1, o2;
                matcher.MatchTaggedConstant(left, &o1) &&
                matcher.MatchTaggedConstant(right, &o2)) {
              return __ Word32Constant(o1.address() == o2.address());
            }
            break;
          }
          default:
            UNREACHABLE();
        }
      }
      if (rep.IsWord()) {
        WordRepresentation rep_w{rep};
        // x - y == 0  =>  x == y
        if (OpIndex x, y; matcher.MatchWordSub(left, &x, &y, rep_w) &&
                          matcher.MatchZero(right)) {
          return ReduceCompareEqual(x, y, rep);
        }
        {
          //     ((x >> shift_amount) & mask) == k
          // =>  (x & (mask << shift_amount)) == (k << shift_amount)
          OpIndex shift, x, mask_op;
          int shift_amount;
          uint64_t mask, k;
          if (matcher.MatchBitwiseAnd(left, &shift, &mask_op, rep_w) &&
              matcher.MatchConstantRightShift(shift, &x, rep_w,
                                              &shift_amount) &&
              matcher.MatchIntegralWordConstant(mask_op, rep_w, &mask) &&
              matcher.MatchIntegralWordConstant(right, rep_w, &k) &&
              mask <= rep.MaxUnsignedValue() >> shift_amount &&
              k <= rep.MaxUnsignedValue() >> shift_amount) {
            return ReduceCompareEqual(
                __ WordBitwiseAnd(
                    x, __ WordConstant(mask << shift_amount, rep_w), rep_w),
                __ WordConstant(k << shift_amount, rep_w), rep_w);
          }
        }
        {
          // (x >> k1) == k2  =>  x == (k2 << k1)  if shifts reversible
          // Only perform the transformation if the shift is not used yet, to
          // avoid keeping both the shift and x alive.
          OpIndex x;
          uint16_t k1;
          int64_t k2;
          if (matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                  left, &x, rep_w, &k1) &&
              matcher.MatchIntegralWordConstant(right, rep_w, &k2) &&
              CountLeadingSignBits(k2, rep_w) > k1 &&
              matcher.Get(left).saturated_use_count.IsZero()) {
            return __ Equal(
                x, __ WordConstant(base::bits::Unsigned(k2) << k1, rep_w),
                rep_w);
          }
        }
        // Map 64bit to 32bit equals.
        if (rep_w == WordRepresentation::Word64()) {
          base::Optional<bool> left_sign_extended;
          base::Optional<bool> right_sign_extended;
          if (IsWord32ConvertedToWord64(left, &left_sign_extended) &&
              IsWord32ConvertedToWord64(right, &right_sign_extended)) {
            if (left_sign_extended == right_sign_extended) {
              return __ Equal(UndoWord32ToWord64Conversion(left),
                              UndoWord32ToWord64Conversion(right),
                              WordRepresentation::Word32());
            }
          }
        }
      }
    }
    return Next::ReduceComparison(left, right, ComparisonOp::Kind::kEqual, rep);
  }

  // Try to match a constant and add it to `offset`. Return `true` if
  // successful.
  bool TryAdjustOffset(int32_t* offset, const Operation& maybe_constant,
                       uint8_t element_scale, bool tagged_base) {
    if (!maybe_constant.Is<ConstantOp>()) return false;
    const ConstantOp& constant = maybe_constant.Cast<ConstantOp>();
    if (constant.rep != WordRepresentation::PointerSized() ||
        !constant.IsIntegral()) {
      // This can only happen in unreachable code. Ideally, we identify this
      // situation and use `__ Unreachable()`. However, this is difficult to
      // do from within this helper, so we just don't perform the reduction.
      return false;
    }
    int64_t diff = constant.signed_integral();
    int32_t new_offset;
    if (diff <= (std::numeric_limits<int32_t>::max() >> element_scale) &&
        diff >= (std::numeric_limits<int32_t>::min() >> element_scale) &&
        !base::bits::SignedAddOverflow32(
            *offset,
            static_cast<int32_t>(base::bits::Unsigned(diff) << element_scale),
            &new_offset) &&
        LoadOp::OffsetIsValid(new_offset, tagged_base)) {
      *offset = new_offset;
      return true;
    }
    return false;
  }

  bool TryAdjustIndex(int32_t offset, OpIndex* index,
                      const Operation& maybe_constant, uint8_t element_scale) {
    if (!maybe_constant.Is<ConstantOp>()) return false;
    const ConstantOp& constant = maybe_constant.Cast<ConstantOp>();
    if (constant.rep != WordRepresentation::PointerSized() ||
        !constant.IsIntegral()) {
      // This can only happen in unreachable code. Ideally, we identify this
      // situation and use `__ Unreachable()`. However, this is difficult to
      // do from within this helper, so we just don't perform the reduction.
      return false;
    }
    int64_t diff = constant.signed_integral();
    int64_t new_index;
    if (!base::bits::SignedAddOverflow64(offset, diff << element_scale,
                                         &new_index)) {
      *index = __ IntPtrConstant(new_index);
      return true;
    }
    return false;
  }

  bool TryAdjustElementScale(uint8_t* element_scale, OpIndex maybe_constant) {
    uint64_t diff;
    if (!matcher.MatchIntegralWordConstant(
            maybe_constant, WordRepresentation::PointerSized(), &diff)) {
      return false;
    }
    DCHECK_LT(*element_scale, WordRepresentation::PointerSized().bit_width());
    if (diff < (WordRepresentation::PointerSized().bit_width() -
                uint64_t{*element_scale})) {
      *element_scale += diff;
      return true;
    }
    return false;
  }

  // Fold away operations in the computation of `index` while preserving the
  // value of `(index << element_scale) + offset)` by updating `offset`,
  // `element_scale` and returning the updated `index`.
  // Return `OpIndex::Invalid()` if the resulting index is zero.
  OpIndex ReduceMemoryIndex(OpIndex index, int32_t* offset,
                            uint8_t* element_scale, bool tagged_base) {
    while (index.valid()) {
      const Operation& index_op = matcher.Get(index);
      if (TryAdjustOffset(offset, index_op, *element_scale, tagged_base)) {
        index = OpIndex::Invalid();
        *element_scale = 0;
      } else if (TryAdjustIndex(*offset, &index, index_op, *element_scale)) {
        *element_scale = 0;
        *offset = 0;
        // This function cannot optimize the index further since at this point
        // it's just a WordPtrConstant.
        return index;
      } else if (const ShiftOp* shift_op = index_op.TryCast<ShiftOp>()) {
        if (shift_op->kind == ShiftOp::Kind::kShiftLeft &&
            TryAdjustElementScale(element_scale, shift_op->right())) {
          index = shift_op->left();
          continue;
        }
      } else if (const WordBinopOp* binary_op =
                     index_op.TryCast<WordBinopOp>()) {
        // TODO(jkummerow): This doesn't trigger for wasm32 memory operations
        // on 64-bit platforms, because `index_op` is a `Change` (from uint32
        // to uint64) in that case, and that Change's input is the addition
        // we're looking for. When we fix that, we must also teach the x64
        // instruction selector to support xchg with index *and* offset.
        if (binary_op->kind == WordBinopOp::Kind::kAdd &&
            TryAdjustOffset(offset, matcher.Get(binary_op->right()),
                            *element_scale, tagged_base)) {
          index = binary_op->left();
          continue;
        }
      }
      break;
    }
    return index;
  }

  bool IsFloat32ConvertedToFloat64(OpIndex value) {
    if (OpIndex input;
        matcher.MatchChange(value, &input, ChangeOp::Kind::kFloatConversion,
                            RegisterRepresentation::Float32(),
                            RegisterRepresentation::Float64())) {
      return true;
    }
    if (double c;
        matcher.MatchFloat64Constant(value, &c) && DoubleToFloat32(c) == c) {
      return true;
    }
    return false;
  }

  OpIndex UndoFloat32ToFloat64Conversion(OpIndex value) {
    if (OpIndex input;
        matcher.MatchChange(value, &input, ChangeOp::Kind::kFloatConversion,
                            RegisterRepresentation::Float32(),
                            RegisterRepresentation::Float64())) {
      return input;
    }
    if (double c;
        matcher.MatchFloat64Constant(value, &c) && DoubleToFloat32(c) == c) {
      return __ Float32Constant(DoubleToFloat32(c));
    }
    UNREACHABLE();
  }

  bool IsBit(OpIndex value) { return matcher.Is<ComparisonOp>(value); }

  bool IsInt8(OpIndex value) {
    if (auto* op = matcher.TryCast<LoadOp>(value)) {
      return op->loaded_rep == MemoryRepresentation::Int8();
    } else if (auto* op = matcher.TryCast<LoadOp>(value)) {
      return op->loaded_rep == MemoryRepresentation::Int8();
    }
    return false;
  }

  bool IsInt16(OpIndex value) {
    if (auto* op = matcher.TryCast<LoadOp>(value)) {
      return op->loaded_rep == any_of(MemoryRepresentation::Int16(),
                                      MemoryRepresentation::Int8());
    } else if (auto* op = matcher.TryCast<LoadOp>(value)) {
      return op->loaded_rep == any_of(MemoryRepresentation::Int16(),
                                      MemoryRepresentation::Int8());
    }
    return false;
  }

  bool IsWord32ConvertedToWord64(
      OpIndex value, base::Optional<bool>* sign_extended = nullptr) {
    if (const ChangeOp* change_op = matcher.TryCast<ChangeOp>(value)) {
      if (change_op->from == WordRepresentation::Word32() &&
          change_op->to == WordRepresentation::Word64()) {
        if (change_op->kind == ChangeOp::Kind::kSignExtend) {
          if (sign_extended) *sign_extended = true;
          return true;
        } else if (change_op->kind == ChangeOp::Kind::kZeroExtend) {
          if (sign_extended) *sign_extended = false;
          return true;
        }
      }
    }
    if (int64_t c; matcher.MatchIntegralWord64Constant(value, &c) &&
                   c >= std::numeric_limits<int32_t>::min()) {
      if (c < 0) {
        if (sign_extended) *sign_extended = true;
        return true;
      } else if (c <= std::numeric_limits<int32_t>::max()) {
        // Sign- and zero-extension produce the same result.
        if (sign_extended) *sign_extended = {};
        return true;
      } else if (c <= std::numeric_limits<uint32_t>::max()) {
        if (sign_extended) *sign_extended = false;
        return true;
      }
    }
    return false;
  }

  OpIndex UndoWord32ToWord64Conversion(OpIndex value) {
    DCHECK(IsWord32ConvertedToWord64(value));
    if (const ChangeOp* op = matcher.TryCast<ChangeOp>(value)) {
      return op->input();
    }
    return __ Word32Constant(matcher.Cast<ConstantOp>(value).word32());
  }

  OpIndex TryRemoveWord32ToWord64Conversion(OpIndex value) {
    if (const ChangeOp* op = matcher.TryCast<ChangeOp>(value)) {
      if (op->from == WordRepresentation::Word32() &&
          op->to == WordRepresentation::Word64() &&
          op->kind == any_of(ChangeOp::Kind::kZeroExtend,
                             ChangeOp::Kind::kSignExtend)) {
        return op->input();
      }
    }
    return value;
  }

  uint64_t TruncateWord(uint64_t value, WordRepresentation rep) {
    if (rep == WordRepresentation::Word32()) {
      return static_cast<uint32_t>(value);
    } else {
      DCHECK_EQ(rep, WordRepresentation::Word64());
      return value;
    }
  }

  // Reduce the given value under the assumption that only the bits set in
  // `truncation_mask` will be observed.
  OpIndex ReduceWithTruncation(OpIndex value, uint64_t truncation_mask,
                               WordRepresentation rep) {
    {  // Remove bitwise-and with a mask whose zero-bits are not observed.
      OpIndex input, mask;
      uint64_t mask_value;
      if (matcher.MatchBitwiseAnd(value, &input, &mask, rep) &&
          matcher.MatchIntegralWordConstant(mask, rep, &mask_value)) {
        if ((mask_value & truncation_mask) == truncation_mask) {
          return ReduceWithTruncation(input, truncation_mask, rep);
        }
      }
    }
    {
      int left_shift_amount;
      int right_shift_amount;
      WordRepresentation rep;
      OpIndex left_shift;
      ShiftOp::Kind right_shift_kind;
      OpIndex left_shift_input;
      if (matcher.MatchConstantShift(value, &left_shift, &right_shift_kind,
                                     &rep, &right_shift_amount) &&
          ShiftOp::IsRightShift(right_shift_kind) &&
          matcher.MatchConstantShift(left_shift, &left_shift_input,
                                     ShiftOp::Kind::kShiftLeft, rep,
                                     &left_shift_amount) &&
          ((rep.MaxUnsignedValue() >> right_shift_amount) & truncation_mask) ==
              truncation_mask) {
        if (left_shift_amount == right_shift_amount) {
          return left_shift_input;
        } else if (left_shift_amount < right_shift_amount) {
          OpIndex shift_amount =
              __ WordConstant(right_shift_amount - left_shift_amount, rep);
          return __ Shift(left_shift_input, shift_amount, right_shift_kind,
                          rep);
        } else if (left_shift_amount > right_shift_amount) {
          OpIndex shift_amount =
              __ WordConstant(left_shift_amount - right_shift_amount, rep);
          return __ Shift(left_shift_input, shift_amount,
                          ShiftOp::Kind::kShiftLeft, rep);
        }
      }
    }
    return value;
  }

  OpIndex ReduceSignedDiv(OpIndex left, int64_t right, WordRepresentation rep) {
    // left / -1 => 0 - left
    if (right == -1) {
      return __ WordSub(__ WordConstant(0, rep), left, rep);
    }
    // left / 0 => 0
    if (right == 0) {
      return __ WordConstant(0, rep);
    }
    // left / 1 => left
    if (right == 1) {
      return left;
    }
    // left / MinSignedValue  =>  left == MinSignedValue
    if (right == rep.MinSignedValue()) {
      OpIndex equal_op = __ Equal(left, __ WordConstant(right, rep), rep);
      return rep == WordRepresentation::Word64()
                 ? __ ChangeUint32ToUint64(equal_op)
                 : equal_op;
    }
    // left / -right  => -(left / right)
    if (right < 0) {
      DCHECK_NE(right, rep.MinSignedValue());
      return __ WordSub(__ WordConstant(0, rep),
                        ReduceSignedDiv(left, Abs(right), rep), rep);
    }

    OpIndex quotient = left;
    if (base::bits::IsPowerOfTwo(right)) {
      uint32_t shift = base::bits::WhichPowerOfTwo(right);
      DCHECK_GT(shift, 0);
      if (shift > 1) {
        quotient = __ ShiftRightArithmetic(quotient, rep.bit_width() - 1, rep);
      }
      quotient = __ ShiftRightLogical(quotient, rep.bit_width() - shift, rep);
      quotient = __ WordAdd(quotient, left, rep);
      quotient = __ ShiftRightArithmetic(quotient, shift, rep);
      return quotient;
    }
    DCHECK_GT(right, 0);
    // Compute the magic number for `right`, using a generic lambda to treat
    // 32- and 64-bit uniformly.
    auto LowerToMul = [this, left](auto right, WordRepresentation rep) {
      base::MagicNumbersForDivision<decltype(right)> magic =
          base::SignedDivisionByConstant(right);
      OpIndex quotient = __ IntMulOverflownBits(
          left, __ WordConstant(magic.multiplier, rep), rep);
      if (magic.multiplier < 0) {
        quotient = __ WordAdd(quotient, left, rep);
      }
      OpIndex sign_bit = __ ShiftRightLogical(left, rep.bit_width() - 1, rep);
      return __ WordAdd(__ ShiftRightArithmetic(quotient, magic.shift, rep),
                        sign_bit, rep);
    };
    if (rep == WordRepresentation::Word32()) {
      return LowerToMul(static_cast<int32_t>(right),
                        WordRepresentation::Word32());
    } else {
      DCHECK_EQ(rep, WordRepresentation::Word64());
      return LowerToMul(static_cast<int64_t>(right),
                        WordRepresentation::Word64());
    }
  }

  OpIndex ReduceUnsignedDiv(OpIndex left, uint64_t right,
                            WordRepresentation rep) {
    // left / 0 => 0
    if (right == 0) {
      return __ WordConstant(0, rep);
    }
    // left / 1 => left
    if (right == 1) {
      return left;
    }
    // left / 2^k  => left >> k
    if (base::bits::IsPowerOfTwo(right)) {
      return __ ShiftRightLogical(left, base::bits::WhichPowerOfTwo(right),
                                  rep);
    }
    DCHECK_GT(right, 0);
    // If `right` is even, we can avoid using the expensive fixup by
    // shifting `left` upfront.
    unsigned const shift = base::bits::CountTrailingZeros(right);
    left = __ ShiftRightLogical(left, shift, rep);
    right >>= shift;
    // Compute the magic number for `right`, using a generic lambda to treat
    // 32- and 64-bit uniformly.
    auto LowerToMul = [this, left, shift](auto right, WordRepresentation rep) {
      base::MagicNumbersForDivision<decltype(right)> const mag =
          base::UnsignedDivisionByConstant(right, shift);
      OpIndex quotient = __ UintMulOverflownBits(
          left, __ WordConstant(mag.multiplier, rep), rep);
      if (mag.add) {
        DCHECK_GE(mag.shift, 1);
        // quotient = (((left - quotient) >> 1) + quotient) >> (mag.shift -
        // 1)
        quotient = __ ShiftRightLogical(
            __ WordAdd(
                __ ShiftRightLogical(__ WordSub(left, quotient, rep), 1, rep),
                quotient, rep),
            mag.shift - 1, rep);
      } else {
        quotient = __ ShiftRightLogical(quotient, mag.shift, rep);
      }
      return quotient;
    };
    if (rep == WordRepresentation::Word32()) {
      return LowerToMul(static_cast<uint32_t>(right),
                        WordRepresentation::Word32());
    } else {
      DCHECK_EQ(rep, WordRepresentation::Word64());
      return LowerToMul(static_cast<uint64_t>(right),
                        WordRepresentation::Word64());
    }
  }

  base::Optional<OpIndex> ReduceBranchCondition(OpIndex condition,
                                                bool* negated) {
    // TODO(dmercadier): consider generalizing this function both Word32 and
    // Word64.
    bool reduced = false;
    while (true) {
      condition = TryRemoveWord32ToWord64Conversion(condition);
      // x == 0  =>  x with flipped branches
      if (OpIndex left, right;
          matcher.MatchEqual(condition, &left, &right,
                             WordRepresentation::Word32()) &&
          matcher.MatchZero(right)) {
        reduced = true;
        condition = left;
        *negated = !*negated;
        continue;
      }
      // x - y  =>  x == y with flipped branches
      if (OpIndex left, right; matcher.MatchWordSub(
              condition, &left, &right, WordRepresentation::Word32())) {
        reduced = true;
        condition = __ Word32Equal(left, right);
        *negated = !*negated;
        continue;
      }
      // x & (1 << k) == (1 << k)  =>  x & (1 << k)
      if (OpIndex left, right; matcher.MatchEqual(
              condition, &left, &right, WordRepresentation::Word32())) {
        OpIndex x, mask;
        uint32_t k1, k2;
        if (matcher.MatchBitwiseAnd(left, &x, &mask,
                                    WordRepresentation::Word32()) &&
            matcher.MatchIntegralWord32Constant(mask, &k1) &&
            matcher.MatchIntegralWord32Constant(right, &k2) && k1 == k2 &&
            base::bits::IsPowerOfTwo(k1)) {
          reduced = true;
          condition = left;
          continue;
        }
      }
      // (x >> k1) & k2   =>   x & (k2 << k1)
      {
        OpIndex shift, k2_index, x;
        int k1_int;
        uint32_t k1, k2;
        if (matcher.MatchBitwiseAnd(condition, &shift, &k2_index,
                                    WordRepresentation::Word32()) &&
            matcher.MatchConstantRightShift(
                shift, &x, WordRepresentation::Word32(), &k1_int) &&
            matcher.MatchIntegralWord32Constant(k2_index, &k2)) {
          k1 = static_cast<uint32_t>(k1_int);
          if (k1 <= base::bits::CountLeadingZeros(k2) &&
              (static_cast<uint64_t>(k2) << k1 <=
               std::numeric_limits<uint32_t>::max())) {
            return __ Word32BitwiseAnd(x, k2 << k1);
          }
        }
      }
      // Select(x, true, false) => x
      if (const SelectOp* select = matcher.TryCast<SelectOp>(condition)) {
        auto left_val = MatchBoolConstant(select->vtrue());
        auto right_val = MatchBoolConstant(select->vfalse());
        if (left_val && right_val) {
          if (*left_val == *right_val) {
            // Select(x, v, v) => v
            return __ Word32Constant(*left_val);
          }
          if (*left_val == false) {
            // Select(x, false, true) => !x
            *negated = !*negated;
          }
          condition = select->cond();
          reduced = true;
          continue;
        }
      }
      break;
    }
    return reduced ? base::Optional<OpIndex>(condition) : base::nullopt;
  }

  base::Optional<bool> MatchBoolConstant(OpIndex condition) {
    if (uint32_t value;
        matcher.MatchIntegralWord32Constant(condition, &value)) {
      return value != 0;
    }
    return base::nullopt;
  }

  // Returns true if loading the map of an object with map {map} can be constant
  // folded and done at compile time or not. For instance, doing this for
  // strings is not safe, since the map of a string could change during a GC,
  // but doing this for a HeapNumber is always safe.
  bool MapLoadCanBeConstantFolded(OptionalMapRef map) {
    if (!map.has_value()) return false;

    if (map->IsJSObjectMap() && map->is_stable()) {
      broker->dependencies()->DependOnStableMap(*map);
      // For JS objects, this is only safe is the map is stable.
      return true;
    }

    if (map->instance_type() ==
        any_of(BIG_INT_BASE_TYPE, HEAP_NUMBER_TYPE, ODDBALL_TYPE)) {
      return true;
    }

    return false;
  }

  static constexpr bool IsNegativePowerOfTwo(int64_t x) {
    if (x >= 0) return false;
    if (x == std::numeric_limits<int64_t>::min()) return true;
    int64_t x_abs = -x;   // This can't overflow after the check above.
    DCHECK_GE(x_abs, 1);  // The subtraction below can't underflow.
    return (x_abs & (x_abs - 1)) == 0;
  }

  static constexpr uint16_t CountLeadingSignBits(int64_t c,
                                                 WordRepresentation rep) {
    return base::bits::CountLeadingSignBits(c) - (64 - rep.bit_width());
  }

  JSHeapBroker* broker = PipelineData::Get().broker();
  const OperationMatcher& matcher = __ matcher();
#if V8_ENABLE_WEBASSEMBLY
  const bool signalling_nan_possible = PipelineData::Get().is_wasm();
#else
  static constexpr bool signalling_nan_possible = false;
#endif  // V8_ENABLE_WEBASSEMBLY
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_MACHINE_OPTIMIZATION_REDUCER_H_
