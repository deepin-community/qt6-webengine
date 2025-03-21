// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/base/video_frame_converter_internals.h"

#include "third_party/libyuv/include/libyuv.h"

namespace {

bool IsSupportedRGBFormat(media::VideoPixelFormat format) {
  return format == media::PIXEL_FORMAT_XBGR ||
         format == media::PIXEL_FORMAT_XRGB ||
         format == media::PIXEL_FORMAT_ABGR ||
         format == media::PIXEL_FORMAT_ARGB;
}

}  // namespace

namespace media::internals {

bool ARGBScale(const VideoFrame& src_frame,
               VideoFrame& dest_frame,
               libyuv::FilterMode filter) {
  DCHECK(IsSupportedRGBFormat(src_frame.format()));
  return libyuv::ARGBScale(
             src_frame.visible_data(VideoFrame::kARGBPlane),
             src_frame.stride(VideoFrame::kARGBPlane),
             src_frame.visible_rect().width(),
             src_frame.visible_rect().height(),
             dest_frame.GetWritableVisibleData(VideoFrame::kARGBPlane),
             dest_frame.stride(VideoFrame::kARGBPlane),
             dest_frame.visible_rect().width(),
             dest_frame.visible_rect().height(), filter) == 0;
}

bool ARGBToI420x(const VideoFrame& src_frame,
                 VideoFrame& dest_frame,
                 libyuv::FilterMode filter) {
  DCHECK(IsSupportedRGBFormat(src_frame.format()));
  DCHECK(dest_frame.format() == PIXEL_FORMAT_I420 ||
         dest_frame.format() == PIXEL_FORMAT_I420A);
  auto convert_fn = (src_frame.format() == PIXEL_FORMAT_XBGR ||
                     src_frame.format() == PIXEL_FORMAT_ABGR)
                        ? libyuv::ABGRToI420
                        : libyuv::ARGBToI420;
  if (convert_fn(src_frame.visible_data(VideoFrame::kARGBPlane),
                 src_frame.stride(VideoFrame::kARGBPlane),
                 dest_frame.GetWritableVisibleData(VideoFrame::kYPlane),
                 dest_frame.stride(VideoFrame::kYPlane),
                 dest_frame.GetWritableVisibleData(VideoFrame::kUPlane),
                 dest_frame.stride(VideoFrame::kUPlane),
                 dest_frame.GetWritableVisibleData(VideoFrame::kVPlane),
                 dest_frame.stride(VideoFrame::kVPlane),
                 dest_frame.visible_rect().width(),
                 dest_frame.visible_rect().height()) != 0) {
    return false;
  }
  if (dest_frame.format() == PIXEL_FORMAT_I420) {
    return true;
  }
  return libyuv::ARGBExtractAlpha(
             src_frame.visible_data(VideoFrame::kARGBPlane),
             src_frame.stride(VideoFrame::kARGBPlane),
             dest_frame.GetWritableVisibleData(VideoFrame::kAPlane),
             dest_frame.stride(VideoFrame::kAPlane),
             dest_frame.visible_rect().width(),
             dest_frame.visible_rect().height()) == 0;
}

bool ARGBToNV12x(const VideoFrame& src_frame,
                 VideoFrame& dest_frame,
                 libyuv::FilterMode filter) {
  DCHECK(IsSupportedRGBFormat(src_frame.format()));
  DCHECK(dest_frame.format() == PIXEL_FORMAT_NV12 ||
         dest_frame.format() == PIXEL_FORMAT_NV12A);

  auto convert_fn = (src_frame.format() == PIXEL_FORMAT_XBGR ||
                     src_frame.format() == PIXEL_FORMAT_ABGR)
                        ? libyuv::ABGRToNV12
                        : libyuv::ARGBToNV12;
  if (convert_fn(src_frame.visible_data(VideoFrame::kARGBPlane),
                 src_frame.stride(VideoFrame::kARGBPlane),
                 dest_frame.GetWritableVisibleData(VideoFrame::kYPlane),
                 dest_frame.stride(VideoFrame::kYPlane),
                 dest_frame.GetWritableVisibleData(VideoFrame::kUVPlane),
                 dest_frame.stride(VideoFrame::kUVPlane),
                 dest_frame.visible_rect().width(),
                 dest_frame.visible_rect().height()) != 0) {
    return false;
  }
  if (dest_frame.format() == PIXEL_FORMAT_NV12) {
    return true;
  }
  return libyuv::ARGBExtractAlpha(
             src_frame.visible_data(VideoFrame::kARGBPlane),
             src_frame.stride(VideoFrame::kARGBPlane),
             dest_frame.GetWritableVisibleData(VideoFrame::kAPlaneTriPlanar),
             dest_frame.stride(VideoFrame::kAPlaneTriPlanar),
             dest_frame.visible_rect().width(),
             dest_frame.visible_rect().height()) == 0;
}

void I4xxxScale(const VideoFrame& src_frame,
                VideoFrame& dest_frame,
                libyuv::FilterMode filter) {
  DCHECK(src_frame.format() == PIXEL_FORMAT_I420 ||
         src_frame.format() == PIXEL_FORMAT_I420A ||
         src_frame.format() == PIXEL_FORMAT_I422 ||
         src_frame.format() == PIXEL_FORMAT_I422A ||
         src_frame.format() == PIXEL_FORMAT_I444 ||
         src_frame.format() == PIXEL_FORMAT_I444A);
  DCHECK(dest_frame.format() == PIXEL_FORMAT_I420 ||
         dest_frame.format() == PIXEL_FORMAT_I420A ||
         dest_frame.format() == PIXEL_FORMAT_I422 ||
         dest_frame.format() == PIXEL_FORMAT_I422A ||
         dest_frame.format() == PIXEL_FORMAT_I444 ||
         dest_frame.format() == PIXEL_FORMAT_I444A);
  DCHECK_GE(VideoFrame::NumPlanes(src_frame.format()),
            VideoFrame::NumPlanes(dest_frame.format()));
  for (size_t i = 0; i < VideoFrame::NumPlanes(dest_frame.format()); ++i) {
    libyuv::ScalePlane(
        src_frame.visible_data(i), src_frame.stride(i),
        VideoFrame::Columns(i, src_frame.format(),
                            src_frame.visible_rect().size().width()),
        VideoFrame::Rows(i, src_frame.format(),
                         src_frame.visible_rect().size().height()),
        dest_frame.GetWritableVisibleData(i), dest_frame.stride(i),
        VideoFrame::Columns(i, dest_frame.format(),
                            dest_frame.visible_rect().size().width()),
        VideoFrame::Rows(i, dest_frame.format(),
                         dest_frame.visible_rect().size().height()),
        filter);
  }
}

bool I420xToNV12x(const VideoFrame& src_frame,
                  VideoFrame& dest_frame,
                  libyuv::FilterMode filter) {
  DCHECK(src_frame.format() == PIXEL_FORMAT_I420 ||
         src_frame.format() == PIXEL_FORMAT_I420A);
  DCHECK(dest_frame.format() == PIXEL_FORMAT_NV12 ||
         dest_frame.format() == PIXEL_FORMAT_NV12A);
  DCHECK_EQ(src_frame.visible_rect().size(), dest_frame.visible_rect().size());
  if (libyuv::I420ToNV12(
          src_frame.visible_data(VideoFrame::kYPlane),
          src_frame.stride(VideoFrame::kYPlane),
          src_frame.visible_data(VideoFrame::kUPlane),
          src_frame.stride(VideoFrame::kUPlane),
          src_frame.visible_data(VideoFrame::kVPlane),
          src_frame.stride(VideoFrame::kVPlane),
          dest_frame.GetWritableVisibleData(VideoFrame::kYPlane),
          dest_frame.stride(VideoFrame::kYPlane),
          dest_frame.GetWritableVisibleData(VideoFrame::kUVPlane),
          dest_frame.stride(VideoFrame::kUVPlane),
          dest_frame.visible_rect().width(),
          dest_frame.visible_rect().height()) != 0) {
    return false;
  }
  if (dest_frame.format() == PIXEL_FORMAT_NV12) {
    return true;
  }
  libyuv::CopyPlane(
      src_frame.visible_data(VideoFrame::kAPlane),
      src_frame.stride(VideoFrame::kAPlane),
      dest_frame.GetWritableVisibleData(VideoFrame::kAPlaneTriPlanar),
      dest_frame.stride(VideoFrame::kAPlaneTriPlanar),
      dest_frame.visible_rect().width(), dest_frame.visible_rect().height());
  return true;
}

void MergeUV(const VideoFrame& src_frame, VideoFrame& dest_frame) {
  DCHECK(src_frame.format() == PIXEL_FORMAT_I420 ||
         src_frame.format() == PIXEL_FORMAT_I420A);
  DCHECK(dest_frame.format() == PIXEL_FORMAT_NV12 ||
         dest_frame.format() == PIXEL_FORMAT_NV12A);
  DCHECK_EQ(src_frame.visible_rect().size(), dest_frame.visible_rect().size());
  libyuv::MergeUVPlane(src_frame.visible_data(VideoFrame::kUPlane),
                       src_frame.stride(VideoFrame::kUPlane),
                       src_frame.visible_data(VideoFrame::kVPlane),
                       src_frame.stride(VideoFrame::kVPlane),
                       dest_frame.GetWritableVisibleData(VideoFrame::kUVPlane),
                       dest_frame.stride(VideoFrame::kUVPlane),
                       dest_frame.visible_rect().width() / 2,
                       dest_frame.visible_rect().height() / 2);
}

void SplitUV(const VideoFrame& src_frame, VideoFrame& dest_frame) {
  DCHECK(src_frame.format() == PIXEL_FORMAT_NV12 ||
         src_frame.format() == PIXEL_FORMAT_NV12A);
  DCHECK(dest_frame.format() == PIXEL_FORMAT_I420 ||
         dest_frame.format() == PIXEL_FORMAT_I420A);
  DCHECK_EQ(src_frame.visible_rect().size(), dest_frame.visible_rect().size());
  libyuv::SplitUVPlane(src_frame.visible_data(VideoFrame::kUVPlane),
                       src_frame.stride(VideoFrame::kUVPlane),
                       dest_frame.GetWritableVisibleData(VideoFrame::kUPlane),
                       dest_frame.stride(VideoFrame::kUPlane),
                       dest_frame.GetWritableVisibleData(VideoFrame::kVPlane),
                       dest_frame.stride(VideoFrame::kVPlane),
                       dest_frame.visible_rect().width() / 2,
                       dest_frame.visible_rect().height() / 2);
}

bool NV12xScale(const VideoFrame& src_frame,
                VideoFrame& dest_frame,
                libyuv::FilterMode filter) {
  DCHECK(src_frame.format() == PIXEL_FORMAT_NV12 ||
         src_frame.format() == PIXEL_FORMAT_NV12A);
  DCHECK(dest_frame.format() == PIXEL_FORMAT_NV12 ||
         dest_frame.format() == PIXEL_FORMAT_NV12A);
  if (libyuv::NV12Scale(src_frame.visible_data(VideoFrame::kYPlane),
                        src_frame.stride(VideoFrame::kYPlane),
                        src_frame.visible_data(VideoFrame::kUVPlane),
                        src_frame.stride(VideoFrame::kUVPlane),
                        src_frame.visible_rect().width(),
                        src_frame.visible_rect().height(),
                        dest_frame.GetWritableVisibleData(VideoFrame::kYPlane),
                        dest_frame.stride(VideoFrame::kYPlane),
                        dest_frame.GetWritableVisibleData(VideoFrame::kUVPlane),
                        dest_frame.stride(VideoFrame::kUVPlane),
                        dest_frame.visible_rect().width(),
                        dest_frame.visible_rect().height(), filter) != 0) {
    return false;
  }
  if (dest_frame.format() == PIXEL_FORMAT_NV12) {
    return true;
  }
  libyuv::ScalePlane(
      src_frame.visible_data(VideoFrame::kAPlaneTriPlanar),
      src_frame.stride(VideoFrame::kAPlaneTriPlanar),
      src_frame.visible_rect().width(), src_frame.visible_rect().height(),
      dest_frame.GetWritableVisibleData(VideoFrame::kAPlaneTriPlanar),
      dest_frame.stride(VideoFrame::kAPlaneTriPlanar),
      dest_frame.visible_rect().width(), dest_frame.visible_rect().height(),
      filter);
  return true;
}

bool NV12xToI420x(const VideoFrame& src_frame,
                  VideoFrame& dest_frame,
                  libyuv::FilterMode filter) {
  DCHECK(src_frame.format() == PIXEL_FORMAT_NV12 ||
         src_frame.format() == PIXEL_FORMAT_NV12A);
  DCHECK(dest_frame.format() == PIXEL_FORMAT_I420 ||
         dest_frame.format() == PIXEL_FORMAT_I420A);
  DCHECK_EQ(src_frame.visible_rect().size(), dest_frame.visible_rect().size());
  if (libyuv::NV12ToI420(src_frame.visible_data(VideoFrame::kYPlane),
                         src_frame.stride(VideoFrame::kYPlane),
                         src_frame.visible_data(VideoFrame::kUVPlane),
                         src_frame.stride(VideoFrame::kUVPlane),
                         dest_frame.GetWritableVisibleData(VideoFrame::kYPlane),
                         dest_frame.stride(VideoFrame::kYPlane),
                         dest_frame.GetWritableVisibleData(VideoFrame::kUPlane),
                         dest_frame.stride(VideoFrame::kUPlane),
                         dest_frame.GetWritableVisibleData(VideoFrame::kVPlane),
                         dest_frame.stride(VideoFrame::kVPlane),
                         dest_frame.visible_rect().width(),
                         dest_frame.visible_rect().height()) != 0) {
    return false;
  }
  if (dest_frame.format() == PIXEL_FORMAT_I420) {
    return true;
  }
  libyuv::CopyPlane(src_frame.visible_data(VideoFrame::kAPlaneTriPlanar),
                    src_frame.stride(VideoFrame::kAPlaneTriPlanar),
                    dest_frame.GetWritableVisibleData(VideoFrame::kAPlane),
                    dest_frame.stride(VideoFrame::kAPlane),
                    dest_frame.visible_rect().width(),
                    dest_frame.visible_rect().height());
  return true;
}

}  // namespace media::internals
