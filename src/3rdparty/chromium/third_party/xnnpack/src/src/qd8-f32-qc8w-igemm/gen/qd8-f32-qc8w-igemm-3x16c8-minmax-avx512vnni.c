// Auto-generated file. Do not edit!
//   Template: src/qs8-igemm/MRx16c8-avx512vnni.c.in
//   Generator: tools/xngen
//
// Copyright 2023 Google LLC
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include <assert.h>

#include <immintrin.h>

#include <xnnpack/gemm.h>
#include <xnnpack/intrinsics-polyfill.h>
#include <xnnpack/math.h>
#include <xnnpack/unaligned.h>


void xnn_qd8_f32_qc8w_igemm_minmax_ukernel_3x16c8__avx512vnni(
    size_t mr,
    size_t nc,
    size_t kc,
    size_t ks,
    const int8_t** restrict a,
    const void* restrict w,
    float* restrict c,
    size_t cm_stride,
    size_t cn_stride,
    size_t a_offset,
    const int8_t* zero,
    const int8_t* zero_data,
    const union xnn_f32_minmax_params params[restrict XNN_MIN_ELEMENTS(1)],
    const struct xnn_qd8_quantization_params quantization_params[restrict XNN_MIN_ELEMENTS(1)]) XNN_OOB_READS
{
  assert(mr != 0);
  assert(mr <= 3);
  assert(nc != 0);
  assert(kc != 0);
  assert(kc % sizeof(int8_t) == 0);
  assert(a != NULL);
  assert(w != NULL);
  assert(c != NULL);

  kc = round_up_po2(kc, 8 * sizeof(int8_t));
  float* c0 = c;
  float* c1 = (float*) ((uintptr_t) c0 + cm_stride);
  if XNN_UNPREDICTABLE(mr < 2) {
    c1 = c0;
  }
  float* c2 = (float*) ((uintptr_t) c1 + cm_stride);
  if XNN_UNPREDICTABLE(mr <= 2) {
    c2 = c1;
  }

  const __m512i vinput_zero_point = _mm512_set1_epi32((int) quantization_params->zero_point + 128);
  const __m512 vinput_inv_scale = _mm512_set1_ps(quantization_params->inv_scale);
  const __m512 voutput_min = _mm512_set1_ps(params->avx512vnni.min);
  const __m512 voutput_max = _mm512_set1_ps(params->avx512vnni.max);
  const __m512i vsign_mask = _mm512_set1_epi8(params->avx512vnni.sign_mask);  // 0x80
  do {
    const __m512i vksum0123456789ABCDEF = _mm512_load_epi32(w);
    const __m512i vsum0x0123456789ABCDEF = _mm512_mullo_epi32(vksum0123456789ABCDEF, vinput_zero_point);
    __m512i vacc0x01234567 = _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(vsum0x0123456789ABCDEF, 0));
    __m512i vacc0x89ABCDEF = _mm512_cvtepu32_epi64(_mm512_extracti64x4_epi64(vsum0x0123456789ABCDEF, 1));
    __m512i vacc1x01234567 = vacc0x01234567;
    __m512i vacc1x89ABCDEF = vacc0x89ABCDEF;
    __m512i vacc2x01234567 = vacc0x01234567;
    __m512i vacc2x89ABCDEF = vacc0x89ABCDEF;
    w = (const int32_t*) w + 16;

    size_t p = ks;
    do {
      const int8_t* restrict a0 = a[0];
      if XNN_UNPREDICTABLE(a0 != zero) {
        a0 = (const int8_t*) ((uintptr_t) a0 + a_offset);
      } else {
        a0 = zero_data;
      }
      const int8_t* restrict a1 = a[1];
      if XNN_UNPREDICTABLE(a1 != zero) {
        a1 = (const int8_t*) ((uintptr_t) a1 + a_offset);
      } else {
        a1 = zero_data;
      }
      const int8_t* restrict a2 = a[2];
      if XNN_UNPREDICTABLE(a2 != zero) {
        a2 = (const int8_t*) ((uintptr_t) a2 + a_offset);
      } else {
        a2 = zero_data;
      }
      a += 3;

      size_t k = kc;
      while (k >= 16 * sizeof(int8_t)) {
        const __m512i va0x01234567 = _mm512_xor_epi64(_mm512_broadcast_i32x2(_mm_loadl_epi64((const __m128i*) a0)), vsign_mask);
        const __m512i va0x89ABCDEF = _mm512_xor_epi64(_mm512_broadcast_i32x2(_mm_loadl_epi64((const __m128i*) (a0 + 8))), vsign_mask);
        a0 += 16;
        const __m512i va1x01234567 = _mm512_xor_epi64(_mm512_broadcast_i32x2(_mm_loadl_epi64((const __m128i*) a1)), vsign_mask);
        const __m512i va1x89ABCDEF = _mm512_xor_epi64(_mm512_broadcast_i32x2(_mm_loadl_epi64((const __m128i*) (a1 + 8))), vsign_mask);
        a1 += 16;
        const __m512i va2x01234567 = _mm512_xor_epi64(_mm512_broadcast_i32x2(_mm_loadl_epi64((const __m128i*) a2)), vsign_mask);
        const __m512i va2x89ABCDEF = _mm512_xor_epi64(_mm512_broadcast_i32x2(_mm_loadl_epi64((const __m128i*) (a2 + 8))), vsign_mask);
        a2 += 16;

        const __m512i vb01234567x01234567 = _mm512_load_si512(w);
        const __m512i vb89ABCDEFx01234567 = _mm512_load_si512((const int8_t*) w + 64);
        const __m512i vb01234567x89ABCDEF = _mm512_load_si512((const int8_t*) w + 128);
        const __m512i vb89ABCDEFx89ABCDEF = _mm512_load_si512((const int8_t*) w + 192);

        vacc0x01234567 = _mm512_dpbusd_epi32(vacc0x01234567, va0x01234567, vb01234567x01234567);
        vacc0x89ABCDEF = _mm512_dpbusd_epi32(vacc0x89ABCDEF, va0x01234567, vb89ABCDEFx01234567);
        vacc1x01234567 = _mm512_dpbusd_epi32(vacc1x01234567, va1x01234567, vb01234567x01234567);
        vacc1x89ABCDEF = _mm512_dpbusd_epi32(vacc1x89ABCDEF, va1x01234567, vb89ABCDEFx01234567);
        vacc2x01234567 = _mm512_dpbusd_epi32(vacc2x01234567, va2x01234567, vb01234567x01234567);
        vacc2x89ABCDEF = _mm512_dpbusd_epi32(vacc2x89ABCDEF, va2x01234567, vb89ABCDEFx01234567);
        vacc0x01234567 = _mm512_dpbusd_epi32(vacc0x01234567, va0x89ABCDEF, vb01234567x89ABCDEF);
        vacc0x89ABCDEF = _mm512_dpbusd_epi32(vacc0x89ABCDEF, va0x89ABCDEF, vb89ABCDEFx89ABCDEF);
        vacc1x01234567 = _mm512_dpbusd_epi32(vacc1x01234567, va1x89ABCDEF, vb01234567x89ABCDEF);
        vacc1x89ABCDEF = _mm512_dpbusd_epi32(vacc1x89ABCDEF, va1x89ABCDEF, vb89ABCDEFx89ABCDEF);
        vacc2x01234567 = _mm512_dpbusd_epi32(vacc2x01234567, va2x89ABCDEF, vb01234567x89ABCDEF);
        vacc2x89ABCDEF = _mm512_dpbusd_epi32(vacc2x89ABCDEF, va2x89ABCDEF, vb89ABCDEFx89ABCDEF);

        w = (const int8_t*) w + 256;
        k -= 16 * sizeof(int8_t);
      }

      if (k != 0) {
        const __m512i va0x01234567 = _mm512_xor_epi64(_mm512_broadcast_i32x2(_mm_loadl_epi64((const __m128i*) a0)), vsign_mask);
        a0 += 8;
        const __m512i va1x01234567 = _mm512_xor_epi64(_mm512_broadcast_i32x2(_mm_loadl_epi64((const __m128i*) a1)), vsign_mask);
        a1 += 8;
        const __m512i va2x01234567 = _mm512_xor_epi64(_mm512_broadcast_i32x2(_mm_loadl_epi64((const __m128i*) a2)), vsign_mask);
        a2 += 8;

        const __m512i vb01234567x01234567 = _mm512_load_si512(w);
        const __m512i vb89ABCDEFx01234567 = _mm512_load_si512((const int8_t*) w + 64);

        vacc0x01234567 = _mm512_dpbusd_epi32(vacc0x01234567, va0x01234567, vb01234567x01234567);
        vacc0x89ABCDEF = _mm512_dpbusd_epi32(vacc0x89ABCDEF, va0x01234567, vb89ABCDEFx01234567);
        vacc1x01234567 = _mm512_dpbusd_epi32(vacc1x01234567, va1x01234567, vb01234567x01234567);
        vacc1x89ABCDEF = _mm512_dpbusd_epi32(vacc1x89ABCDEF, va1x01234567, vb89ABCDEFx01234567);
        vacc2x01234567 = _mm512_dpbusd_epi32(vacc2x01234567, va2x01234567, vb01234567x01234567);
        vacc2x89ABCDEF = _mm512_dpbusd_epi32(vacc2x89ABCDEF, va2x01234567, vb89ABCDEFx01234567);

        w = (const int8_t*) w + 128;
        k -= 8 * sizeof(int8_t);
      }

      p -= 3 * sizeof(void*);
    } while (p != 0);

    // Add adjacent pairs
    const __m512i vidx = _mm512_set_epi32(30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8, 6, 4, 2, 0);
    const __m512i vsum0x01234567 = _mm512_add_epi32(vacc0x01234567, _mm512_srai_epi64(vacc0x01234567, 32));
    const __m512i vsum0x89ABCDEF = _mm512_add_epi32(vacc0x89ABCDEF, _mm512_srai_epi64(vacc0x89ABCDEF, 32));
    __m512i vacc0x0123456789ABCDEF = _mm512_permutex2var_epi32(vsum0x01234567, vidx, vsum0x89ABCDEF);
    const __m512i vsum1x01234567 = _mm512_add_epi32(vacc1x01234567, _mm512_srai_epi64(vacc1x01234567, 32));
    const __m512i vsum1x89ABCDEF = _mm512_add_epi32(vacc1x89ABCDEF, _mm512_srai_epi64(vacc1x89ABCDEF, 32));
    __m512i vacc1x0123456789ABCDEF = _mm512_permutex2var_epi32(vsum1x01234567, vidx, vsum1x89ABCDEF);
    const __m512i vsum2x01234567 = _mm512_add_epi32(vacc2x01234567, _mm512_srai_epi64(vacc2x01234567, 32));
    const __m512i vsum2x89ABCDEF = _mm512_add_epi32(vacc2x89ABCDEF, _mm512_srai_epi64(vacc2x89ABCDEF, 32));
    __m512i vacc2x0123456789ABCDEF = _mm512_permutex2var_epi32(vsum2x01234567, vidx, vsum2x89ABCDEF);

    __m512 vscaled0x0123456789ABCDEF = _mm512_cvtepi32_ps(vacc0x0123456789ABCDEF);
    __m512 vscaled1x0123456789ABCDEF = _mm512_cvtepi32_ps(vacc1x0123456789ABCDEF);
    __m512 vscaled2x0123456789ABCDEF = _mm512_cvtepi32_ps(vacc2x0123456789ABCDEF);

    vscaled0x0123456789ABCDEF = _mm512_mul_ps(vscaled0x0123456789ABCDEF, vinput_inv_scale);
    vscaled1x0123456789ABCDEF = _mm512_mul_ps(vscaled1x0123456789ABCDEF, vinput_inv_scale);
    vscaled2x0123456789ABCDEF = _mm512_mul_ps(vscaled2x0123456789ABCDEF, vinput_inv_scale);

    const __m512 vfilter_output_scale0123456789ABCDEF = _mm512_load_ps((const float*) w);
    const __m512 vbias0123456789ABCDEF = _mm512_load_ps((const float*) w + 16);
    w = (const float*) w + 32;

    vscaled0x0123456789ABCDEF = _mm512_fmadd_ps(vscaled0x0123456789ABCDEF, vfilter_output_scale0123456789ABCDEF, vbias0123456789ABCDEF);
    vscaled1x0123456789ABCDEF = _mm512_fmadd_ps(vscaled1x0123456789ABCDEF, vfilter_output_scale0123456789ABCDEF, vbias0123456789ABCDEF);
    vscaled2x0123456789ABCDEF = _mm512_fmadd_ps(vscaled2x0123456789ABCDEF, vfilter_output_scale0123456789ABCDEF, vbias0123456789ABCDEF);

    vscaled0x0123456789ABCDEF = _mm512_max_ps(vscaled0x0123456789ABCDEF, voutput_min);
    vscaled1x0123456789ABCDEF = _mm512_max_ps(vscaled1x0123456789ABCDEF, voutput_min);
    vscaled2x0123456789ABCDEF = _mm512_max_ps(vscaled2x0123456789ABCDEF, voutput_min);

    vscaled0x0123456789ABCDEF = _mm512_min_ps(vscaled0x0123456789ABCDEF, voutput_max);
    vscaled1x0123456789ABCDEF = _mm512_min_ps(vscaled1x0123456789ABCDEF, voutput_max);
    vscaled2x0123456789ABCDEF = _mm512_min_ps(vscaled2x0123456789ABCDEF, voutput_max);

    if(nc >= 16) {
      _mm512_storeu_ps(c2, vscaled2x0123456789ABCDEF);
      c2 = (float*) ((uintptr_t) c2 + cn_stride);
      _mm512_storeu_ps(c1, vscaled1x0123456789ABCDEF);
      c1 = (float*) ((uintptr_t) c1 + cn_stride);
      _mm512_storeu_ps(c0, vscaled0x0123456789ABCDEF);
      c0 = (float*) ((uintptr_t) c0 + cn_stride);
      a = (const int8_t**restrict) ((uintptr_t) a - ks);
      nc -= 16;
    } else {
      // Prepare mask for valid 32-bit elements (depends on nc).
      const __mmask16 vmask = _cvtu32_mask16((UINT32_C(1) << nc) - 1);
      _mm512_mask_storeu_ps(c2, vmask, vscaled2x0123456789ABCDEF);
      _mm512_mask_storeu_ps(c1, vmask, vscaled1x0123456789ABCDEF);
      _mm512_mask_storeu_ps(c0, vmask, vscaled0x0123456789ABCDEF);
      nc = 0;
    }
  } while (nc != 0);
}
