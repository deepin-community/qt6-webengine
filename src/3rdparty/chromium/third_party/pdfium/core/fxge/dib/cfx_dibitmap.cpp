// Copyright 2017 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#include "core/fxge/dib/cfx_dibitmap.h"

#include <stdint.h>
#include <string.h>

#include <limits>
#include <memory>
#include <utility>

#include "build/build_config.h"
#include "core/fxcrt/data_vector.h"
#include "core/fxcrt/fx_coordinates.h"
#include "core/fxcrt/fx_safe_types.h"
#include "core/fxcrt/span_util.h"
#include "core/fxge/calculate_pitch.h"
#include "core/fxge/cfx_cliprgn.h"
#include "core/fxge/cfx_defaultrenderdevice.h"
#include "core/fxge/dib/cfx_scanlinecompositor.h"
#include "third_party/base/check.h"
#include "third_party/base/check_op.h"
#include "third_party/base/notreached.h"
#include "third_party/base/numerics/safe_conversions.h"

CFX_DIBitmap::CFX_DIBitmap() = default;

bool CFX_DIBitmap::Create(int width, int height, FXDIB_Format format) {
  return Create(width, height, format, nullptr, 0);
}

bool CFX_DIBitmap::Create(int width,
                          int height,
                          FXDIB_Format format,
                          uint8_t* pBuffer,
                          uint32_t pitch) {
  m_pBuffer = nullptr;
  m_Format = format;
  m_Width = 0;
  m_Height = 0;
  m_Pitch = 0;

  absl::optional<PitchAndSize> pitch_size =
      CalculatePitchAndSize(width, height, format, pitch);
  if (!pitch_size.has_value())
    return false;

  if (pBuffer) {
    m_pBuffer.Reset(pBuffer);
  } else {
    FX_SAFE_SIZE_T safe_buffer_size = pitch_size.value().size;
    safe_buffer_size += 4;
    if (!safe_buffer_size.IsValid())
      return false;

    m_pBuffer = std::unique_ptr<uint8_t, FxFreeDeleter>(
        FX_TryAlloc(uint8_t, safe_buffer_size.ValueOrDie()));
    if (!m_pBuffer)
      return false;
  }
  m_Width = width;
  m_Height = height;
  m_Pitch = pitch_size.value().pitch;
  return true;
}

bool CFX_DIBitmap::Copy(RetainPtr<const CFX_DIBBase> source) {
  if (m_pBuffer)
    return false;

  if (!Create(source->GetWidth(), source->GetHeight(), source->GetFormat())) {
    return false;
  }

  SetPalette(source->GetPaletteSpan());
  for (int row = 0; row < source->GetHeight(); row++) {
    memcpy(m_pBuffer.Get() + row * m_Pitch, source->GetScanline(row).data(),
           m_Pitch);
  }
  return true;
}

CFX_DIBitmap::~CFX_DIBitmap() = default;

pdfium::span<const uint8_t> CFX_DIBitmap::GetBuffer() const {
  if (!m_pBuffer)
    return pdfium::span<const uint8_t>();

  return {m_pBuffer.Get(), m_Height * m_Pitch};
}

pdfium::span<const uint8_t> CFX_DIBitmap::GetScanline(int line) const {
  auto buffer_span = GetBuffer();
  if (buffer_span.empty())
    return pdfium::span<const uint8_t>();

  return buffer_span.subspan(line * m_Pitch, m_Pitch);
}

size_t CFX_DIBitmap::GetEstimatedImageMemoryBurden() const {
  size_t result = CFX_DIBBase::GetEstimatedImageMemoryBurden();
  if (!GetBuffer().empty()) {
    int height = GetHeight();
    CHECK(pdfium::base::IsValueInRangeForNumericType<size_t>(height));
    result += static_cast<size_t>(height) * GetPitch();
  }
  return result;
}

#if BUILDFLAG(IS_WIN) || defined(PDF_USE_SKIA)
RetainPtr<const CFX_DIBitmap> CFX_DIBitmap::RealizeIfNeeded() const {
  if (GetBuffer().empty()) {
    return Realize();
  }
  return pdfium::WrapRetain(this);
}
#endif

void CFX_DIBitmap::TakeOver(RetainPtr<CFX_DIBitmap>&& pSrcBitmap) {
  m_pBuffer = std::move(pSrcBitmap->m_pBuffer);
  m_palette = std::move(pSrcBitmap->m_palette);
  pSrcBitmap->m_pBuffer = nullptr;
  m_Format = pSrcBitmap->m_Format;
  m_Width = pSrcBitmap->m_Width;
  m_Height = pSrcBitmap->m_Height;
  m_Pitch = pSrcBitmap->m_Pitch;
}

void CFX_DIBitmap::Clear(uint32_t color) {
  if (!m_pBuffer)
    return;

  uint8_t* pBuffer = m_pBuffer.Get();
  switch (GetFormat()) {
    case FXDIB_Format::k1bppMask:
      memset(pBuffer, (color & 0xff000000) ? 0xff : 0, m_Pitch * m_Height);
      break;
    case FXDIB_Format::k1bppRgb: {
      int index = FindPalette(color);
      memset(pBuffer, index ? 0xff : 0, m_Pitch * m_Height);
      break;
    }
    case FXDIB_Format::k8bppMask:
      memset(pBuffer, color >> 24, m_Pitch * m_Height);
      break;
    case FXDIB_Format::k8bppRgb: {
      int index = FindPalette(color);
      memset(pBuffer, index, m_Pitch * m_Height);
      break;
    }
    case FXDIB_Format::kRgb: {
      int a;
      int r;
      int g;
      int b;
      std::tie(a, r, g, b) = ArgbDecode(color);
      if (r == g && g == b) {
        memset(pBuffer, r, m_Pitch * m_Height);
      } else {
        int byte_pos = 0;
        for (int col = 0; col < m_Width; col++) {
          pBuffer[byte_pos++] = b;
          pBuffer[byte_pos++] = g;
          pBuffer[byte_pos++] = r;
        }
        for (int row = 1; row < m_Height; row++) {
          memcpy(pBuffer + row * m_Pitch, pBuffer, m_Pitch);
        }
      }
      break;
    }
    case FXDIB_Format::kRgb32:
    case FXDIB_Format::kArgb: {
      if (CFX_DefaultRenderDevice::UseSkiaRenderer() &&
          FXDIB_Format::kRgb32 == GetFormat()) {
        // TODO(crbug.com/pdfium/2016): This is not reliable because alpha may
        // be modified outside of this operation.
        color |= 0xFF000000;
      }
      for (int i = 0; i < m_Width; i++)
        reinterpret_cast<uint32_t*>(pBuffer)[i] = color;
      for (int row = 1; row < m_Height; row++)
        memcpy(pBuffer + row * m_Pitch, pBuffer, m_Pitch);
      break;
    }
    default:
      break;
  }
}

bool CFX_DIBitmap::TransferBitmap(int dest_left,
                                  int dest_top,
                                  int width,
                                  int height,
                                  RetainPtr<const CFX_DIBBase> source,
                                  int src_left,
                                  int src_top) {
  if (!m_pBuffer)
    return false;

  if (!GetOverlapRect(dest_left, dest_top, width, height, source->GetWidth(),
                      source->GetHeight(), src_left, src_top, nullptr)) {
    return true;
  }

  FXDIB_Format dest_format = GetFormat();
  FXDIB_Format src_format = source->GetFormat();
  if (dest_format != src_format) {
    return TransferWithUnequalFormats(dest_format, dest_left, dest_top, width,
                                      height, std::move(source), src_left,
                                      src_top);
  }

  if (GetBPP() != 1) {
    TransferWithMultipleBPP(dest_left, dest_top, width, height,
                            std::move(source), src_left, src_top);
    return true;
  }

  TransferEqualFormatsOneBPP(dest_left, dest_top, width, height,
                             std::move(source), src_left, src_top);
  return true;
}

bool CFX_DIBitmap::TransferWithUnequalFormats(
    FXDIB_Format dest_format,
    int dest_left,
    int dest_top,
    int width,
    int height,
    RetainPtr<const CFX_DIBBase> source,
    int src_left,
    int src_top) {
  if (HasPalette())
    return false;

  if (GetBppFromFormat(m_Format) == 8)
    dest_format = FXDIB_Format::k8bppMask;

  FX_SAFE_UINT32 offset = dest_left;
  offset *= GetBPP();
  offset /= 8;
  if (!offset.IsValid())
    return false;

  pdfium::span<uint8_t> dest_buf = GetWritableBuffer().subspan(
      dest_top * m_Pitch + static_cast<uint32_t>(offset.ValueOrDie()));
  DataVector<uint32_t> dest_palette = ConvertBuffer(
      dest_format, dest_buf, m_Pitch, width, height, source, src_left, src_top);
  CHECK(dest_palette.empty());
  return true;
}

void CFX_DIBitmap::TransferWithMultipleBPP(int dest_left,
                                           int dest_top,
                                           int width,
                                           int height,
                                           RetainPtr<const CFX_DIBBase> source,
                                           int src_left,
                                           int src_top) {
  int Bpp = GetBPP() / 8;
  for (int row = 0; row < height; ++row) {
    uint8_t* dest_scan =
        m_pBuffer.Get() + (dest_top + row) * m_Pitch + dest_left * Bpp;
    const uint8_t* src_scan =
        source->GetScanline(src_top + row).subspan(src_left * Bpp).data();
    memcpy(dest_scan, src_scan, width * Bpp);
  }
}

void CFX_DIBitmap::TransferEqualFormatsOneBPP(
    int dest_left,
    int dest_top,
    int width,
    int height,
    RetainPtr<const CFX_DIBBase> source,
    int src_left,
    int src_top) {
  for (int row = 0; row < height; ++row) {
    uint8_t* dest_scan = m_pBuffer.Get() + (dest_top + row) * m_Pitch;
    const uint8_t* src_scan = source->GetScanline(src_top + row).data();
    for (int col = 0; col < width; ++col) {
      int src_idx = src_left + col;
      int dest_idx = dest_left + col;
      if (src_scan[(src_idx) / 8] & (1 << (7 - (src_idx) % 8)))
        dest_scan[(dest_idx) / 8] |= 1 << (7 - (dest_idx) % 8);
      else
        dest_scan[(dest_idx) / 8] &= ~(1 << (7 - (dest_idx) % 8));
    }
  }
}

bool CFX_DIBitmap::SetChannelFromBitmap(Channel dest_channel,
                                        RetainPtr<const CFX_DIBBase> source) {
  if (!m_pBuffer)
    return false;

  if (!source->IsAlphaFormat() && !source->IsMaskFormat()) {
    return false;
  }

  if (source->GetBPP() == 1) {
    source = source->ConvertTo(FXDIB_Format::k8bppMask);
    if (!source) {
      return false;
    }
  }
  const int src_offset = source->GetFormat() == FXDIB_Format::kArgb ? 3 : 0;
  int dest_offset = 0;
  if (dest_channel == Channel::kAlpha) {
    if (IsMaskFormat()) {
      if (!ConvertFormat(FXDIB_Format::k8bppMask))
        return false;
    } else {
      if (!ConvertFormat(FXDIB_Format::kArgb))
        return false;

      dest_offset = 3;
    }
  } else {
    DCHECK_EQ(dest_channel, Channel::kRed);
    if (IsMaskFormat())
      return false;

    if (GetBPP() < 24) {
      if (IsAlphaFormat()) {
        if (!ConvertFormat(FXDIB_Format::kArgb))
          return false;
      } else {
        if (!ConvertFormat(kPlatformRGBFormat))
          return false;
      }
    }
    dest_offset = 2;
  }
  if (source->GetWidth() != m_Width || source->GetHeight() != m_Height) {
    source =
        source->StretchTo(m_Width, m_Height, FXDIB_ResampleOptions(), nullptr);
    if (!source) {
      return false;
    }
  }
  RetainPtr<CFX_DIBitmap> dest_bitmap(this);
  int src_bytes = source->GetBPP() / 8;
  int dest_bytes = dest_bitmap->GetBPP() / 8;
  for (int row = 0; row < m_Height; row++) {
    uint8_t* dest_pos =
        dest_bitmap->GetWritableScanline(row).subspan(dest_offset).data();
    const uint8_t* src_pos =
        source->GetScanline(row).subspan(src_offset).data();
    for (int col = 0; col < m_Width; col++) {
      *dest_pos = *src_pos;
      dest_pos += dest_bytes;
      src_pos += src_bytes;
    }
  }
  return true;
}

bool CFX_DIBitmap::SetAlphaFromBitmap(RetainPtr<const CFX_DIBBase> source) {
  return SetChannelFromBitmap(Channel::kAlpha, std::move(source));
}

bool CFX_DIBitmap::SetRedFromBitmap(RetainPtr<const CFX_DIBBase> source) {
  return SetChannelFromBitmap(Channel::kRed, std::move(source));
}

bool CFX_DIBitmap::SetUniformOpaqueAlpha() {
  if (!m_pBuffer)
    return false;

  if (IsMaskFormat()) {
    if (!ConvertFormat(FXDIB_Format::k8bppMask))
      return false;
  } else {
    if (!ConvertFormat(FXDIB_Format::kArgb))
      return false;
  }
  const int Bpp = GetBPP() / 8;
  if (Bpp == 1) {
    memset(m_pBuffer.Get(), 0xff, m_Height * m_Pitch);
    return true;
  }
  const int dest_offset = GetFormat() == FXDIB_Format::kArgb ? 3 : 0;
  for (int row = 0; row < m_Height; row++) {
    uint8_t* scan_line = m_pBuffer.Get() + row * m_Pitch + dest_offset;
    for (int col = 0; col < m_Width; col++) {
      *scan_line = 0xff;
      scan_line += Bpp;
    }
  }
  return true;
}

bool CFX_DIBitmap::MultiplyAlphaMask(RetainPtr<const CFX_DIBBase> source) {
  CHECK(source->IsMaskFormat());

  if (!m_pBuffer) {
    return false;
  }

  if (IsOpaqueImage()) {
    return SetAlphaFromBitmap(std::move(source));
  }

  RetainPtr<const CFX_DIBitmap> pSrcClone = source.As<const CFX_DIBitmap>();
  if (source->GetWidth() != m_Width || source->GetHeight() != m_Height) {
    pSrcClone =
        source->StretchTo(m_Width, m_Height, FXDIB_ResampleOptions(), nullptr);
    if (!pSrcClone)
      return false;
  }
  if (IsMaskFormat()) {
    if (!ConvertFormat(FXDIB_Format::k8bppMask))
      return false;

    for (int row = 0; row < m_Height; row++) {
      uint8_t* dest_scan = m_pBuffer.Get() + m_Pitch * row;
      uint8_t* src_scan = pSrcClone->m_pBuffer.Get() + pSrcClone->m_Pitch * row;
      if (pSrcClone->GetBPP() == 1) {
        for (int col = 0; col < m_Width; col++) {
          if (!((1 << (7 - col % 8)) & src_scan[col / 8]))
            dest_scan[col] = 0;
        }
      } else {
        for (int col = 0; col < m_Width; col++) {
          *dest_scan = (*dest_scan) * src_scan[col] / 255;
          dest_scan++;
        }
      }
    }
    return true;
  }

  DCHECK_EQ(GetFormat(), FXDIB_Format::kArgb);
  if (pSrcClone->GetBPP() == 1)
    return false;

  for (int row = 0; row < m_Height; row++) {
    uint8_t* dest_scan = m_pBuffer.Get() + m_Pitch * row + 3;
    uint8_t* src_scan = pSrcClone->m_pBuffer.Get() + pSrcClone->m_Pitch * row;
    for (int col = 0; col < m_Width; col++) {
      *dest_scan = (*dest_scan) * src_scan[col] / 255;
      dest_scan += 4;
    }
  }
  return true;
}

bool CFX_DIBitmap::MultiplyAlpha(float alpha) {
  CHECK_GE(alpha, 0.0f);
  CHECK_LE(alpha, 1.0f);

  if (alpha == 1.0f) {
    return true;
  }

  if (!m_pBuffer) {
    return false;
  }

  switch (GetFormat()) {
    case FXDIB_Format::k1bppMask:
      if (!ConvertFormat(FXDIB_Format::k8bppMask)) {
        return false;
      }
      MultiplyAlpha(alpha);
      break;
    case FXDIB_Format::k8bppMask: {
      const int bitmap_alpha = static_cast<int>(alpha * 255);
      for (int row = 0; row < m_Height; row++) {
        uint8_t* scan_line = m_pBuffer.Get() + row * m_Pitch;
        for (int col = 0; col < m_Width; col++) {
          scan_line[col] = scan_line[col] * bitmap_alpha / 255;
        }
      }
      break;
    }
    case FXDIB_Format::kArgb: {
      const int bitmap_alpha = static_cast<int>(alpha * 255);
      for (int row = 0; row < m_Height; row++) {
        uint8_t* scan_line = m_pBuffer.Get() + row * m_Pitch + 3;
        for (int col = 0; col < m_Width; col++) {
          *scan_line = (*scan_line) * bitmap_alpha / 255;
          scan_line += 4;
        }
      }
      break;
    }
    default:
      DCHECK(!IsAlphaFormat());
      if (!ConvertFormat(FXDIB_Format::kArgb)) {
        return false;
      }
      MultiplyAlpha(alpha);
      break;
  }
  return true;
}

#if defined(PDF_USE_SKIA)
uint32_t CFX_DIBitmap::GetPixelForTesting(int x, int y) const {
  if (!m_pBuffer)
    return 0;

  FX_SAFE_UINT32 offset = x;
  offset *= GetBPP();
  offset /= 8;
  if (!offset.IsValid())
    return 0;

  uint8_t* pos = m_pBuffer.Get() + y * m_Pitch + offset.ValueOrDie();
  switch (GetFormat()) {
    case FXDIB_Format::k1bppMask: {
      if ((*pos) & (1 << (7 - x % 8))) {
        return 0xff000000;
      }
      return 0;
    }
    case FXDIB_Format::k1bppRgb: {
      if ((*pos) & (1 << (7 - x % 8))) {
        return HasPalette() ? GetPaletteSpan()[1] : 0xffffffff;
      }
      return HasPalette() ? GetPaletteSpan()[0] : 0xff000000;
    }
    case FXDIB_Format::k8bppMask:
      return (*pos) << 24;
    case FXDIB_Format::k8bppRgb:
      return HasPalette() ? GetPaletteSpan()[*pos]
                          : ArgbEncode(0xff, *pos, *pos, *pos);
    case FXDIB_Format::kRgb:
    case FXDIB_Format::kRgb32:
      return FXARGB_GETDIB(pos) | 0xff000000;
    case FXDIB_Format::kArgb:
      return FXARGB_GETDIB(pos);
    default:
      break;
  }
  return 0;
}
#endif  // defined(PDF_USE_SKIA)

void CFX_DIBitmap::ConvertBGRColorScale(uint32_t forecolor,
                                        uint32_t backcolor) {
  int fr = FXSYS_GetRValue(forecolor);
  int fg = FXSYS_GetGValue(forecolor);
  int fb = FXSYS_GetBValue(forecolor);
  int br = FXSYS_GetRValue(backcolor);
  int bg = FXSYS_GetGValue(backcolor);
  int bb = FXSYS_GetBValue(backcolor);
  if (GetBppFromFormat(m_Format) <= 8) {
    if (forecolor == 0 && backcolor == 0xffffff && !HasPalette())
      return;

    BuildPalette();
    int size = 1 << GetBppFromFormat(m_Format);
    for (int i = 0; i < size; ++i) {
      int gray = FXRGB2GRAY(FXARGB_R(m_palette[i]), FXARGB_G(m_palette[i]),
                            FXARGB_B(m_palette[i]));
      m_palette[i] =
          ArgbEncode(0xff, br + (fr - br) * gray / 255,
                     bg + (fg - bg) * gray / 255, bb + (fb - bb) * gray / 255);
    }
    return;
  }
  if (forecolor == 0 && backcolor == 0xffffff) {
    for (int row = 0; row < m_Height; ++row) {
      uint8_t* scanline = m_pBuffer.Get() + row * m_Pitch;
      int gap = GetBppFromFormat(m_Format) / 8 - 2;
      for (int col = 0; col < m_Width; ++col) {
        int gray = FXRGB2GRAY(scanline[2], scanline[1], scanline[0]);
        *scanline++ = gray;
        *scanline++ = gray;
        *scanline = gray;
        scanline += gap;
      }
    }
    return;
  }
  for (int row = 0; row < m_Height; ++row) {
    uint8_t* scanline = m_pBuffer.Get() + row * m_Pitch;
    int gap = GetBppFromFormat(m_Format) / 8 - 2;
    for (int col = 0; col < m_Width; ++col) {
      int gray = FXRGB2GRAY(scanline[2], scanline[1], scanline[0]);
      *scanline++ = bb + (fb - bb) * gray / 255;
      *scanline++ = bg + (fg - bg) * gray / 255;
      *scanline = br + (fr - br) * gray / 255;
      scanline += gap;
    }
  }
}

bool CFX_DIBitmap::ConvertColorScale(uint32_t forecolor, uint32_t backcolor) {
  if (!m_pBuffer || IsMaskFormat())
    return false;

  ConvertBGRColorScale(forecolor, backcolor);
  return true;
}

// static
absl::optional<CFX_DIBitmap::PitchAndSize> CFX_DIBitmap::CalculatePitchAndSize(
    int width,
    int height,
    FXDIB_Format format,
    uint32_t pitch) {
  if (width <= 0 || height <= 0) {
    return absl::nullopt;
  }
  int bpp = GetBppFromFormat(format);
  if (!bpp) {
    return absl::nullopt;
  }
  if (pitch == 0) {
    absl::optional<uint32_t> pitch32 = fxge::CalculatePitch32(bpp, width);
    if (!pitch32.has_value()) {
      return absl::nullopt;
    }
    pitch = pitch32.value();
  } else {
    absl::optional<uint32_t> actual_pitch =
        fxge::CalculatePitch8(bpp, /*components=*/1, width);
    if (!actual_pitch.has_value() || pitch < actual_pitch.value()) {
      return absl::nullopt;
    }
  }
  size_t safe_size = pitch;
  safe_size *= height;
  // The final size of the buffer will be (*size + 4). We are trying to not exceed this.
  if (((std::numeric_limits<std::size_t>::max() - 4) / pitch) < static_cast<uint32_t>(height))
    return absl::nullopt;

  return PitchAndSize{pitch, safe_size};
}

bool CFX_DIBitmap::CompositeBitmap(int dest_left,
                                   int dest_top,
                                   int width,
                                   int height,
                                   RetainPtr<const CFX_DIBBase> source,
                                   int src_left,
                                   int src_top,
                                   BlendMode blend_type,
                                   const CFX_ClipRgn* pClipRgn,
                                   bool bRgbByteOrder) {
  // Should have called CompositeMask().
  CHECK(!source->IsMaskFormat());

  if (!m_pBuffer)
    return false;

  if (GetBppFromFormat(m_Format) < 8)
    return false;

  if (!GetOverlapRect(dest_left, dest_top, width, height, source->GetWidth(),
                      source->GetHeight(), src_left, src_top, pClipRgn)) {
    return true;
  }

  RetainPtr<CFX_DIBitmap> pClipMask;
  FX_RECT clip_box;
  if (pClipRgn && pClipRgn->GetType() != CFX_ClipRgn::kRectI) {
    pClipMask = pClipRgn->GetMask();
    clip_box = pClipRgn->GetBox();
  }
  CFX_ScanlineCompositor compositor;
  if (!compositor.Init(GetFormat(), source->GetFormat(),
                       source->GetPaletteSpan(), 0, blend_type,
                       pClipMask != nullptr, bRgbByteOrder)) {
    return false;
  }
  const int dest_Bpp = GetBppFromFormat(m_Format) / 8;
  const int src_Bpp = source->GetBPP() / 8;
  const bool bRgb = src_Bpp > 1;
  if (!bRgb && !source->HasPalette()) {
    return false;
  }

  for (int row = 0; row < height; row++) {
    pdfium::span<uint8_t> dest_scan =
        GetWritableScanline(dest_top + row).subspan(dest_left * dest_Bpp);
    pdfium::span<const uint8_t> src_scan =
        source->GetScanline(src_top + row).subspan(src_left * src_Bpp);
    pdfium::span<const uint8_t> clip_scan;
    if (pClipMask) {
      clip_scan = pClipMask->GetWritableScanline(dest_top + row - clip_box.top)
                      .subspan(dest_left - clip_box.left);
    }
    if (bRgb) {
      compositor.CompositeRgbBitmapLine(dest_scan, src_scan, width, clip_scan);
    } else {
      compositor.CompositePalBitmapLine(dest_scan, src_scan, src_left, width,
                                        clip_scan);
    }
  }
  return true;
}

bool CFX_DIBitmap::CompositeMask(int dest_left,
                                 int dest_top,
                                 int width,
                                 int height,
                                 const RetainPtr<const CFX_DIBBase>& pMask,
                                 uint32_t color,
                                 int src_left,
                                 int src_top,
                                 BlendMode blend_type,
                                 const CFX_ClipRgn* pClipRgn,
                                 bool bRgbByteOrder) {
  // Should have called CompositeBitmap().
  CHECK(pMask->IsMaskFormat());

  if (!m_pBuffer)
    return false;

  if (GetBppFromFormat(m_Format) < 8)
    return false;

  if (!GetOverlapRect(dest_left, dest_top, width, height, pMask->GetWidth(),
                      pMask->GetHeight(), src_left, src_top, pClipRgn)) {
    return true;
  }

  int src_alpha = FXARGB_A(color);
  if (src_alpha == 0)
    return true;

  RetainPtr<CFX_DIBitmap> pClipMask;
  FX_RECT clip_box;
  if (pClipRgn && pClipRgn->GetType() != CFX_ClipRgn::kRectI) {
    pClipMask = pClipRgn->GetMask();
    clip_box = pClipRgn->GetBox();
  }
  int src_bpp = pMask->GetBPP();
  int Bpp = GetBPP() / 8;
  CFX_ScanlineCompositor compositor;
  if (!compositor.Init(GetFormat(), pMask->GetFormat(), {}, color, blend_type,
                       pClipMask != nullptr, bRgbByteOrder)) {
    return false;
  }
  for (int row = 0; row < height; row++) {
    pdfium::span<uint8_t> dest_scan =
        GetWritableScanline(dest_top + row).subspan(dest_left * Bpp);
    pdfium::span<const uint8_t> src_scan = pMask->GetScanline(src_top + row);
    pdfium::span<const uint8_t> clip_scan;
    if (pClipMask) {
      clip_scan = pClipMask->GetScanline(dest_top + row - clip_box.top)
                      .subspan(dest_left - clip_box.left);
    }
    if (src_bpp == 1) {
      compositor.CompositeBitMaskLine(dest_scan, src_scan, src_left, width,
                                      clip_scan);
    } else {
      compositor.CompositeByteMaskLine(dest_scan, src_scan.subspan(src_left),
                                       width, clip_scan);
    }
  }
  return true;
}

void CFX_DIBitmap::CompositeOneBPPMask(int dest_left,
                                       int dest_top,
                                       int width,
                                       int height,
                                       RetainPtr<const CFX_DIBBase> source,
                                       int src_left,
                                       int src_top) {
  if (GetBPP() != 1) {
    return;
  }

  if (!GetOverlapRect(dest_left, dest_top, width, height, source->GetWidth(),
                      source->GetHeight(), src_left, src_top, nullptr)) {
    return;
  }

  for (int row = 0; row < height; ++row) {
    uint8_t* dest_scan = m_pBuffer.Get() + (dest_top + row) * m_Pitch;
    const uint8_t* src_scan = source->GetScanline(src_top + row).data();
    for (int col = 0; col < width; ++col) {
      int src_idx = src_left + col;
      int dest_idx = dest_left + col;
      if (src_scan[src_idx / 8] & (1 << (7 - src_idx % 8))) {
        dest_scan[dest_idx / 8] |= 1 << (7 - dest_idx % 8);
      }
    }
  }
}

bool CFX_DIBitmap::CompositeRect(int left,
                                 int top,
                                 int width,
                                 int height,
                                 uint32_t color) {
  if (!m_pBuffer)
    return false;

  int src_alpha = FXARGB_A(color);
  if (src_alpha == 0)
    return true;

  FX_RECT rect(left, top, left + width, top + height);
  rect.Intersect(0, 0, m_Width, m_Height);
  if (rect.IsEmpty())
    return true;

  width = rect.Width();
  uint32_t dst_color = color;
  uint8_t* color_p = reinterpret_cast<uint8_t*>(&dst_color);
  if (GetBppFromFormat(m_Format) == 8) {
    uint8_t gray = IsMaskFormat() ? 255
                                  : (uint8_t)FXRGB2GRAY((int)color_p[2],
                                                        color_p[1], color_p[0]);
    for (int row = rect.top; row < rect.bottom; row++) {
      uint8_t* dest_scan = m_pBuffer.Get() + row * m_Pitch + rect.left;
      if (src_alpha == 255) {
        memset(dest_scan, gray, width);
      } else {
        for (int col = 0; col < width; col++) {
          *dest_scan = FXDIB_ALPHA_MERGE(*dest_scan, gray, src_alpha);
          dest_scan++;
        }
      }
    }
    return true;
  }
  if (GetBppFromFormat(m_Format) == 1) {
    int left_shift = rect.left % 8;
    int right_shift = rect.right % 8;
    int new_width = rect.right / 8 - rect.left / 8;
    int index = 0;
    if (HasPalette()) {
      for (int i = 0; i < 2; i++) {
        if (GetPaletteSpan()[i] == color)
          index = i;
      }
    } else {
      index = (static_cast<uint8_t>(color) == 0xff) ? 1 : 0;
    }
    for (int row = rect.top; row < rect.bottom; row++) {
      uint8_t* dest_scan_top =
          GetWritableScanline(row).subspan(rect.left / 8).data();
      uint8_t* dest_scan_top_r =
          GetWritableScanline(row).subspan(rect.right / 8).data();
      uint8_t left_flag = *dest_scan_top & (255 << (8 - left_shift));
      uint8_t right_flag = *dest_scan_top_r & (255 >> right_shift);
      if (new_width) {
        memset(dest_scan_top + 1, index ? 255 : 0, new_width - 1);
        if (!index) {
          *dest_scan_top &= left_flag;
          *dest_scan_top_r &= right_flag;
        } else {
          *dest_scan_top |= ~left_flag;
          *dest_scan_top_r |= ~right_flag;
        }
      } else {
        if (!index) {
          *dest_scan_top &= left_flag | right_flag;
        } else {
          *dest_scan_top |= ~(left_flag | right_flag);
        }
      }
    }
    return true;
  }

  CHECK_GE(GetBppFromFormat(m_Format), 24);
  color_p[3] = static_cast<uint8_t>(src_alpha);
  int Bpp = GetBppFromFormat(m_Format) / 8;
  const bool bAlpha = IsAlphaFormat();
  if (bAlpha) {
    // Other formats with alpha have already been handled above.
    DCHECK_EQ(GetFormat(), FXDIB_Format::kArgb);
  }
  if (src_alpha == 255) {
    for (int row = rect.top; row < rect.bottom; row++) {
      uint8_t* dest_scan = m_pBuffer.Get() + row * m_Pitch + rect.left * Bpp;
      if (Bpp == 4) {
        uint32_t* scan = reinterpret_cast<uint32_t*>(dest_scan);
        for (int col = 0; col < width; col++)
          *scan++ = dst_color;
      } else {
        for (int col = 0; col < width; col++) {
          *dest_scan++ = color_p[0];
          *dest_scan++ = color_p[1];
          *dest_scan++ = color_p[2];
        }
      }
    }
    return true;
  }
  for (int row = rect.top; row < rect.bottom; row++) {
    uint8_t* dest_scan = m_pBuffer.Get() + row * m_Pitch + rect.left * Bpp;
    if (bAlpha) {
      for (int col = 0; col < width; col++) {
        uint8_t back_alpha = dest_scan[3];
        if (back_alpha == 0) {
          FXARGB_SETDIB(dest_scan, ArgbEncode(src_alpha, color_p[2], color_p[1],
                                              color_p[0]));
          dest_scan += 4;
          continue;
        }
        uint8_t dest_alpha =
            back_alpha + src_alpha - back_alpha * src_alpha / 255;
        int alpha_ratio = src_alpha * 255 / dest_alpha;
        *dest_scan = FXDIB_ALPHA_MERGE(*dest_scan, color_p[0], alpha_ratio);
        dest_scan++;
        *dest_scan = FXDIB_ALPHA_MERGE(*dest_scan, color_p[1], alpha_ratio);
        dest_scan++;
        *dest_scan = FXDIB_ALPHA_MERGE(*dest_scan, color_p[2], alpha_ratio);
        dest_scan++;
        *dest_scan++ = dest_alpha;
      }
    } else {
      for (int col = 0; col < width; col++) {
        for (int comps = 0; comps < Bpp; comps++) {
          if (comps == 3) {
            *dest_scan++ = 255;
            continue;
          }
          *dest_scan = FXDIB_ALPHA_MERGE(*dest_scan, color_p[comps], src_alpha);
          dest_scan++;
        }
      }
    }
  }
  return true;
}

bool CFX_DIBitmap::ConvertFormat(FXDIB_Format dest_format) {
  DCHECK(dest_format == FXDIB_Format::k8bppMask ||
         dest_format == FXDIB_Format::kArgb ||
         dest_format == FXDIB_Format::kRgb32 ||
         dest_format == FXDIB_Format::kRgb);

  if (dest_format == m_Format)
    return true;

  if (dest_format == FXDIB_Format::k8bppMask &&
      m_Format == FXDIB_Format::k8bppRgb && !HasPalette()) {
    m_Format = FXDIB_Format::k8bppMask;
    return true;
  }
  if (dest_format == FXDIB_Format::kArgb && m_Format == FXDIB_Format::kRgb32) {
    m_Format = FXDIB_Format::kArgb;
    for (int row = 0; row < m_Height; row++) {
      uint8_t* scanline = m_pBuffer.Get() + row * m_Pitch + 3;
      for (int col = 0; col < m_Width; col++) {
        *scanline = 0xff;
        scanline += 4;
      }
    }
    return true;
  }
  int dest_bpp = GetBppFromFormat(dest_format);
  int dest_pitch = fxge::CalculatePitch32OrDie(dest_bpp, m_Width);
  const size_t dest_buf_size = dest_pitch * m_Height + 4;
  std::unique_ptr<uint8_t, FxFreeDeleter> dest_buf(
      FX_TryAlloc(uint8_t, dest_buf_size));
  if (!dest_buf)
    return false;

  if (dest_format == FXDIB_Format::kArgb) {
    memset(dest_buf.get(), 0xff, dest_buf_size);
  }
  RetainPtr<CFX_DIBBase> holder(this);
  m_palette =
      ConvertBuffer(dest_format, {dest_buf.get(), dest_buf_size}, dest_pitch,
                    m_Width, m_Height, holder, /*src_left=*/0, /*src_top=*/0);
  m_pBuffer = std::move(dest_buf);
  m_Format = dest_format;
  m_Pitch = dest_pitch;
  return true;
}
