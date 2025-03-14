// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/chromeos/libyuv_image_processor_backend.h"

#include <sys/mman.h>

#include "base/containers/contains.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/numerics/checked_math.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/trace_event/trace_event.h"
#include "media/gpu/chromeos/fourcc.h"
#include "media/gpu/macros.h"
#include "media/gpu/video_frame_mapper.h"
#include "media/gpu/video_frame_mapper_factory.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"
#include "third_party/libyuv/include/libyuv/convert_from_argb.h"
#include "third_party/libyuv/include/libyuv/scale.h"

namespace media {

namespace {

enum class SupportResult {
  Supported,
  SupportedWithI420Pivot,
  SupportedWithNV12Pivot,
  Unsupported,
};

enum class Transform {
  kConversion,
  kScaling,
};

static constexpr struct {
  uint32_t input;
  uint32_t output;
  Transform transform;
  SupportResult support_result;
} kSupportFormatConversionArray[] = {
#define CONV(in, out, trans, result) \
  {Fourcc::in, Fourcc::out, Transform::trans, SupportResult::result}
    // Conversion.
#if BUILDFLAG(IS_LINUX)
    CONV(NV12, AR24, kConversion, Supported),
#endif
    CONV(NV12, NV12, kConversion, Supported),
    CONV(YM16, NV12, kConversion, Supported),
    CONV(YM16, YU12, kConversion, Supported),
    CONV(YU12, NV12, kConversion, Supported),
    CONV(YU12, YU12, kConversion, Supported),
    CONV(YUYV, NV12, kConversion, Supported),
    CONV(YUYV, YU12, kConversion, Supported),
    CONV(YV12, NV12, kConversion, Supported),
    CONV(MM21, NV12, kConversion, Supported),
    CONV(MT2T, P010, kConversion, Supported),
    // Scaling.
    CONV(NV12, NV12, kScaling, Supported),
    CONV(YM16, NV12, kScaling, SupportedWithNV12Pivot),
    CONV(YM16, YU12, kScaling, SupportedWithI420Pivot),
    CONV(YU12, YU12, kScaling, Supported),
    CONV(YUYV, NV12, kScaling, SupportedWithNV12Pivot),
    CONV(YUYV, YU12, kScaling, SupportedWithI420Pivot),
#undef CONV
};

SupportResult IsConversionSupported(Fourcc input_fourcc,
                                    Fourcc output_fourcc,
                                    Transform transform) {
  const auto single_input_fourcc = input_fourcc.ToSinglePlanar();
  const auto single_output_fourcc = output_fourcc.ToSinglePlanar();
  if (!single_input_fourcc || !single_output_fourcc)
    return SupportResult::Unsupported;

  // Compare fourccs by formatting single planar formats because LibyuvIP can
  // process either single- or multi-planar formats.
  for (const auto& conv : kSupportFormatConversionArray) {
    const auto conv_input_fourcc = Fourcc::FromUint32(conv.input);
    const auto conv_output_fourcc = Fourcc::FromUint32(conv.output);
    if (!conv_input_fourcc || !conv_output_fourcc)
      continue;
    const auto single_conv_input_fourcc = conv_input_fourcc->ToSinglePlanar();
    const auto single_conv_output_fourcc = conv_output_fourcc->ToSinglePlanar();
    if (!single_conv_input_fourcc || !single_conv_output_fourcc)
      continue;

    if (single_input_fourcc == single_conv_input_fourcc &&
        single_output_fourcc == single_conv_output_fourcc &&
        transform == conv.transform) {
      return conv.support_result;
    }
  }

  return SupportResult::Unsupported;
}

}  // namespace

// static
std::unique_ptr<ImageProcessorBackend> LibYUVImageProcessorBackend::Create(
    const PortConfig& input_config,
    const PortConfig& output_config,
    OutputMode output_mode,
    ErrorCB error_cb) {
  return CreateWithTaskRunner(input_config, output_config, output_mode,
                              error_cb,
                              base::ThreadPool::CreateSequencedTaskRunner(
                                  {base::TaskPriority::USER_VISIBLE}));
}

// static
std::unique_ptr<ImageProcessorBackend>
LibYUVImageProcessorBackend::CreateWithTaskRunner(
    const PortConfig& input_config,
    const PortConfig& output_config,
    OutputMode output_mode,
    ErrorCB error_cb,
    scoped_refptr<base::SequencedTaskRunner> backend_task_runner) {
  VLOGF(2);
  DCHECK_EQ(output_mode, OutputMode::IMPORT)
      << "Only OutputMode::IMPORT supported";

  if (!gfx::Rect(input_config.size).Contains(input_config.visible_rect)) {
    VLOGF(1) << "Input size should contain input visible rect.";
    return nullptr;
  }
  if (!gfx::Rect(output_config.size).Contains(output_config.visible_rect)) {
    VLOGF(1) << "Output size should contain output visible rect.";
    return nullptr;
  }

  std::unique_ptr<VideoFrameMapper> input_frame_mapper;
  // LibYUVImageProcessorBackend supports only memory-based video frame for
  // input.
  VideoFrame::StorageType input_storage_type = VideoFrame::STORAGE_UNKNOWN;
  for (auto input_type : input_config.preferred_storage_types) {
    if (input_type == VideoFrame::STORAGE_DMABUFS ||
        input_type == VideoFrame::STORAGE_GPU_MEMORY_BUFFER) {
      // The LibYUVImageProcessorBackend is not currently used to read from
      // Intel media compressed buffers, so we don't need the VideoFrameMapper
      // to support those.
      input_frame_mapper = VideoFrameMapperFactory::CreateMapper(
          input_config.fourcc.ToVideoPixelFormat(), input_type,
          /*force_linear_buffer_mapper=*/true,
          /*must_support_intel_media_compressed_buffers=*/false);
      if (input_frame_mapper) {
        input_storage_type = input_type;
        break;
      }
    }

    if (VideoFrame::IsStorageTypeMappable(input_type)) {
      input_storage_type = input_type;
      break;
    }
  }
  if (input_storage_type == VideoFrame::STORAGE_UNKNOWN) {
    VLOGF(2) << "Unsupported input storage type";
    return nullptr;
  }

  std::unique_ptr<VideoFrameMapper> output_frame_mapper;
  VideoFrame::StorageType output_storage_type = VideoFrame::STORAGE_UNKNOWN;
  for (auto output_type : output_config.preferred_storage_types) {
    if (output_type == VideoFrame::STORAGE_DMABUFS ||
        output_type == VideoFrame::STORAGE_GPU_MEMORY_BUFFER) {
      // The LibYUVImageProcessorBackend is not currently used to write onto
      // Intel media compressed buffers, so we don't need the VideoFrameMapper
      // to support those.
      output_frame_mapper = VideoFrameMapperFactory::CreateMapper(
          output_config.fourcc.ToVideoPixelFormat(), output_type,
          /*force_linear_buffer_mapper=*/true,
          /*must_support_intel_media_compressed_buffers=*/false);
      if (output_frame_mapper) {
        output_storage_type = output_type;
        break;
      }
    }

    if (VideoFrame::IsStorageTypeMappable(output_type)) {
      output_storage_type = output_type;
      break;
    }
  }
  if (output_storage_type == VideoFrame::STORAGE_UNKNOWN) {
    VLOGF(2) << "Unsupported output storage type";
    return nullptr;
  }

  const gfx::Size& input_size = input_config.visible_rect.size();
  const gfx::Size& output_size = output_config.visible_rect.size();
  Transform transform = Transform::kConversion;
  if (input_size != output_size) {
    transform = Transform::kScaling;
  }

  SupportResult res = IsConversionSupported(input_config.fourcc,
                                            output_config.fourcc, transform);
  if (res == SupportResult::Unsupported) {
    VLOGF(2) << "Conversion from " << input_size.ToString() << "/"
             << input_config.fourcc.ToString() << " to "
             << output_size.ToString() << "/" << output_config.fourcc.ToString()
             << " is not supported";
    return nullptr;
  }

  scoped_refptr<VideoFrame> intermediate_frame;
  if (res == SupportResult::SupportedWithI420Pivot ||
      res == SupportResult::SupportedWithNV12Pivot) {
    intermediate_frame = VideoFrame::CreateFrame(
        res == SupportResult::SupportedWithI420Pivot ? PIXEL_FORMAT_I420
                                                     : PIXEL_FORMAT_NV12,
        input_config.visible_rect.size(),
        gfx::Rect(input_config.visible_rect.size()),
        input_config.visible_rect.size(), base::TimeDelta());
    if (!intermediate_frame) {
      VLOGF(1) << "Failed to create intermediate frame";
      return nullptr;
    }
  }

  // This intermediate frame is only needed in the event of a crop operation
  // that does not start with the upper left corner at (0,0). Tiled formats such
  // as MM21 can not easily be converted starting at arbitrary origins. With
  // this intermediate buffer the tiled format can be converted to a linear
  // format that can be easily cropped.
  scoped_refptr<VideoFrame> crop_intermediate_frame;
  if (input_config.visible_rect.origin() != gfx::Point(0, 0) &&
      input_config.fourcc == Fourcc(Fourcc::MM21)) {
    if (transform != Transform::kScaling) {
      crop_intermediate_frame = VideoFrame::CreateFrame(
          output_config.fourcc.ToVideoPixelFormat(), input_config.size,
          input_config.visible_rect, input_config.size, base::TimeDelta());
      if (!crop_intermediate_frame) {
        VLOGF(1) << "Failed to create cropping intermediate frame";
        return nullptr;
      }
    } else {
      VLOGF(1)
          << "Scaling and cropping simultaneously are not supported for MM21.";
      return nullptr;
    }
  }

  auto processor =
      base::WrapUnique<ImageProcessorBackend>(new LibYUVImageProcessorBackend(
          std::move(input_frame_mapper), std::move(output_frame_mapper),
          std::move(intermediate_frame), std::move(crop_intermediate_frame),
          PortConfig(input_config.fourcc, input_config.size,
                     input_config.planes, input_config.visible_rect,
                     {input_storage_type}),
          PortConfig(output_config.fourcc, output_config.size,
                     output_config.planes, output_config.visible_rect,
                     {output_storage_type}),
          OutputMode::IMPORT, std::move(error_cb),
          std::move(backend_task_runner)));
  VLOGF(2) << "LibYUVImageProcessorBackend created for converting from "
           << input_config.ToString() << " to " << output_config.ToString();
  return processor;
}

LibYUVImageProcessorBackend::LibYUVImageProcessorBackend(
    std::unique_ptr<VideoFrameMapper> input_frame_mapper,
    std::unique_ptr<VideoFrameMapper> output_frame_mapper,
    scoped_refptr<VideoFrame> intermediate_frame,
    scoped_refptr<VideoFrame> crop_intermediate_frame,
    const PortConfig& input_config,
    const PortConfig& output_config,
    OutputMode output_mode,
    ErrorCB error_cb,
    scoped_refptr<base::SequencedTaskRunner> backend_task_runner)
    : ImageProcessorBackend(input_config,
                            output_config,
                            output_mode,
                            std::move(error_cb),
                            std::move(backend_task_runner)),
      input_frame_mapper_(std::move(input_frame_mapper)),
      output_frame_mapper_(std::move(output_frame_mapper)),
      intermediate_frame_(std::move(intermediate_frame)),
      crop_intermediate_frame_(std::move(crop_intermediate_frame)) {}

LibYUVImageProcessorBackend::~LibYUVImageProcessorBackend() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(backend_sequence_checker_);
}

std::string LibYUVImageProcessorBackend::type() const {
  return "LibYUVImageProcessor";
}

void LibYUVImageProcessorBackend::Process(
    scoped_refptr<VideoFrame> input_frame,
    scoped_refptr<VideoFrame> output_frame,
    FrameReadyCB cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(backend_sequence_checker_);
  DVLOGF(4);
  if (input_frame->storage_type() == VideoFrame::STORAGE_DMABUFS ||
      input_frame->storage_type() == VideoFrame::STORAGE_GPU_MEMORY_BUFFER) {
    DCHECK_NE(input_frame_mapper_.get(), nullptr);
    int mapping_permissions = PROT_READ;
    if (input_frame->storage_type() != VideoFrame::STORAGE_DMABUFS)
      mapping_permissions |= PROT_WRITE;
    input_frame =
        input_frame_mapper_->Map(std::move(input_frame), mapping_permissions);
    if (!input_frame) {
      VLOGF(1) << "Failed to map input VideoFrame";
      error_cb_.Run();
      return;
    }
  }

  // We don't replace |output_frame| with a mapped frame, because |output_frame|
  // is the output of ImageProcessor.
  scoped_refptr<VideoFrame> mapped_frame = output_frame;
  if (output_frame->storage_type() == VideoFrame::STORAGE_DMABUFS ||
      output_frame->storage_type() == VideoFrame::STORAGE_GPU_MEMORY_BUFFER) {
    DCHECK_NE(output_frame_mapper_.get(), nullptr);
    mapped_frame =
        output_frame_mapper_->Map(output_frame, PROT_READ | PROT_WRITE);
    if (!mapped_frame) {
      VLOGF(1) << "Failed to map output VideoFrame";
      error_cb_.Run();
      return;
    }
  }

  int res;
  {
    TRACE_EVENT2("media", "LibYUVImageProcessorBackend::Process", "input_frame",
                 input_frame->AsHumanReadableString(), "output_frame",
                 mapped_frame->AsHumanReadableString());
    SCOPED_UMA_HISTOGRAM_TIMER("LibYUVImageProcessorBackend::Process");
    if (input_config_.visible_rect.origin() == gfx::Point(0, 0) ||
        input_config_.fourcc != Fourcc(Fourcc::MM21)) {
      res = DoConversion(input_frame.get(), mapped_frame.get());
    } else {
      res = DoConversion(input_frame.get(), crop_intermediate_frame_.get());

      // This is cropping routine that should be able to support any (known)
      // pixel format).
      if (!res) {
        for (size_t plane = 0;
             plane < VideoFrame::NumPlanes(crop_intermediate_frame_->format());
             plane++) {
          const uint8_t* src_row_ptr =
              crop_intermediate_frame_->visible_data(plane);
          uint8_t* dst_row_ptr = mapped_frame->GetWritableVisibleData(plane);
          for (size_t row = 0;
               row < VideoFrame::Rows(
                         plane, crop_intermediate_frame_->format(),
                         crop_intermediate_frame_->visible_rect().height());
               row++) {
            memcpy(dst_row_ptr, src_row_ptr,
                   VideoFrame::Columns(
                       plane, crop_intermediate_frame_->format(),
                       crop_intermediate_frame_->visible_rect().width()) *
                       VideoFrame::BytesPerElement(
                           crop_intermediate_frame_->format(), plane));
            src_row_ptr += crop_intermediate_frame_->row_bytes(plane);
            dst_row_ptr += mapped_frame->row_bytes(plane);
          }
        }
      }
    }
  }

  if (res != 0) {
    VLOGF(1) << "libyuv returns non-zero code: " << res;
    error_cb_.Run();
    return;
  }
  output_frame->set_timestamp(input_frame->timestamp());
  output_frame->set_color_space(input_frame->ColorSpace());

  std::move(cb).Run(std::move(output_frame));
}

int LibYUVImageProcessorBackend::DoConversion(const VideoFrame* const input,
                                              VideoFrame* const output) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(backend_sequence_checker_);

#define Y_U_V_DATA(fr)                                                        \
  fr->visible_data(VideoFrame::kYPlane), fr->stride(VideoFrame::kYPlane),     \
      fr->visible_data(VideoFrame::kUPlane), fr->stride(VideoFrame::kUPlane), \
      fr->visible_data(VideoFrame::kVPlane), fr->stride(VideoFrame::kVPlane)

#define Y_U_V_DATA_W(fr)                               \
  fr->GetWritableVisibleData(VideoFrame::kYPlane),     \
      fr->stride(VideoFrame::kYPlane),                 \
      fr->GetWritableVisibleData(VideoFrame::kUPlane), \
      fr->stride(VideoFrame::kUPlane),                 \
      fr->GetWritableVisibleData(VideoFrame::kVPlane), \
      fr->stride(VideoFrame::kVPlane)

#define Y_V_U_DATA(fr)                                                        \
  fr->visible_data(VideoFrame::kYPlane), fr->stride(VideoFrame::kYPlane),     \
      fr->visible_data(VideoFrame::kVPlane), fr->stride(VideoFrame::kVPlane), \
      fr->visible_data(VideoFrame::kUPlane), fr->stride(VideoFrame::kUPlane)

#define Y_UV_DATA(fr)                                                     \
  fr->visible_data(VideoFrame::kYPlane), fr->stride(VideoFrame::kYPlane), \
      fr->visible_data(VideoFrame::kUVPlane), fr->stride(VideoFrame::kUVPlane)

#define Y_UV_DATA_W(fr)                                 \
  fr->GetWritableVisibleData(VideoFrame::kYPlane),      \
      fr->stride(VideoFrame::kYPlane),                  \
      fr->GetWritableVisibleData(VideoFrame::kUVPlane), \
      fr->stride(VideoFrame::kUVPlane)

#define YUY2_DATA(fr) \
  fr->visible_data(VideoFrame::kYPlane), fr->stride(VideoFrame::kYPlane)

#define Y_UV_DATA_10BIT(fr)                                                 \
  reinterpret_cast<const uint16_t*>(fr->visible_data(VideoFrame::kYPlane)), \
      fr->stride(VideoFrame::kYPlane),                                      \
      reinterpret_cast<const uint16_t*>(                                    \
          fr->visible_data(VideoFrame::kUVPlane)),                          \
      fr->stride(VideoFrame::kUVPlane)

#define Y_UV_DATA_W_10BIT(fr)                                \
  reinterpret_cast<uint16_t*>(                               \
      fr->GetWritableVisibleData(VideoFrame::kYPlane)),      \
      fr->stride(VideoFrame::kYPlane),                       \
      reinterpret_cast<uint16_t*>(                           \
          fr->GetWritableVisibleData(VideoFrame::kUVPlane)), \
      fr->stride(VideoFrame::kUVPlane)

#if BUILDFLAG(IS_LINUX)
#define ARGB_DATA(fr)                                 \
  fr->GetWritableVisibleData(VideoFrame::kARGBPlane), \
      fr->stride(VideoFrame::kARGBPlane)
#endif

#define LIBYUV_FUNC(func, i, o)                      \
  libyuv::func(i, o, output->visible_rect().width(), \
               output->visible_rect().height())

  if (output->format() == PIXEL_FORMAT_NV12) {
    switch (input->format()) {
      case PIXEL_FORMAT_I420:
        return LIBYUV_FUNC(I420ToNV12, Y_U_V_DATA(input), Y_UV_DATA_W(output));
      case PIXEL_FORMAT_YV12:
        return LIBYUV_FUNC(I420ToNV12, Y_V_U_DATA(input), Y_UV_DATA_W(output));

      case PIXEL_FORMAT_NV12:
        // MM21 mode.
        if (input_config_.fourcc == Fourcc(Fourcc::MM21)) {
          return libyuv::MM21ToNV12(input->data(VideoFrame::kYPlane),
                                    input->stride(VideoFrame::kYPlane),
                                    input->data(VideoFrame::kUVPlane),
                                    input->stride(VideoFrame::kUVPlane),
                                    output->writable_data(VideoFrame::kYPlane),
                                    output->stride(VideoFrame::kYPlane),
                                    output->writable_data(VideoFrame::kUVPlane),
                                    output->stride(VideoFrame::kUVPlane),
                                    std::min(output->coded_size().width(),
                                             input->coded_size().width()),
                                    std::min(output->coded_size().height(),
                                             input->coded_size().height()));
        }

        // Scaling mode.
        return libyuv::NV12Scale(
            Y_UV_DATA(input), input->visible_rect().width(),
            input->visible_rect().height(), Y_UV_DATA_W(output),
            output->visible_rect().width(), output->visible_rect().height(),
            libyuv::kFilterBilinear);

      case PIXEL_FORMAT_YUY2:
        if (input->visible_rect().size() == output->visible_rect().size()) {
          return LIBYUV_FUNC(YUY2ToNV12, YUY2_DATA(input), Y_UV_DATA_W(output));
        } else {
          DCHECK_EQ(intermediate_frame_->format(), PIXEL_FORMAT_NV12);
          int ret = libyuv::YUY2ToNV12(
              YUY2_DATA(input), Y_UV_DATA_W(intermediate_frame_),
              intermediate_frame_->visible_rect().width(),
              intermediate_frame_->visible_rect().height());
          if (ret != 0)
            return ret;
          return libyuv::NV12Scale(
              Y_UV_DATA(intermediate_frame_),
              intermediate_frame_->visible_rect().width(),
              intermediate_frame_->visible_rect().height(), Y_UV_DATA_W(output),
              output->visible_rect().width(), output->visible_rect().height(),
              libyuv::kFilterBilinear);
        }
      case PIXEL_FORMAT_I422:
        if (input->visible_rect().size() == output->visible_rect().size()) {
          return LIBYUV_FUNC(I422ToNV21, Y_V_U_DATA(input),
                             Y_UV_DATA_W(output));
        } else {
          DCHECK_EQ(intermediate_frame_->format(), PIXEL_FORMAT_NV12);
          int ret = libyuv::I422ToNV21(
              Y_V_U_DATA(input), Y_UV_DATA_W(intermediate_frame_),
              intermediate_frame_->visible_rect().width(),
              intermediate_frame_->visible_rect().height());
          if (ret != 0)
            return ret;
          return libyuv::NV12Scale(
              Y_UV_DATA(intermediate_frame_),
              intermediate_frame_->visible_rect().width(),
              intermediate_frame_->visible_rect().height(), Y_UV_DATA_W(output),
              output->visible_rect().width(), output->visible_rect().height(),
              libyuv::kFilterBilinear);
        }
      default:
        VLOGF(1) << "Unexpected input format: " << input->format();
        return -1;
    }
  }

  if (output->format() == PIXEL_FORMAT_I420) {
    switch (input->format()) {
      case PIXEL_FORMAT_I420:
        return libyuv::I420Scale(
            Y_U_V_DATA(input), input->visible_rect().width(),
            input->visible_rect().height(), Y_U_V_DATA_W(output),
            output->visible_rect().width(), output->visible_rect().height(),
            libyuv::kFilterBilinear);
      case PIXEL_FORMAT_YUY2:
        if (input->visible_rect().size() == output->visible_rect().size()) {
          return LIBYUV_FUNC(YUY2ToI420, YUY2_DATA(input),
                             Y_U_V_DATA_W(output));
        } else {
          DCHECK_EQ(intermediate_frame_->format(), PIXEL_FORMAT_I420);
          int ret = libyuv::YUY2ToI420(
              YUY2_DATA(input), Y_U_V_DATA_W(intermediate_frame_),
              intermediate_frame_->visible_rect().width(),
              intermediate_frame_->visible_rect().height());
          if (ret != 0)
            return ret;
          return libyuv::I420Scale(
              Y_U_V_DATA(intermediate_frame_),
              intermediate_frame_->visible_rect().width(),
              intermediate_frame_->visible_rect().height(),
              Y_U_V_DATA_W(output), output->visible_rect().width(),
              output->visible_rect().height(), libyuv::kFilterBilinear);
        }
      case PIXEL_FORMAT_I422:
        if (input->visible_rect().size() == output->visible_rect().size()) {
          return LIBYUV_FUNC(I422ToI420, Y_U_V_DATA(input),
                             Y_U_V_DATA_W(output));
        } else {
          DCHECK_EQ(intermediate_frame_->format(), PIXEL_FORMAT_I420);
          int ret = libyuv::I422ToI420(
              Y_U_V_DATA(input), Y_U_V_DATA_W(intermediate_frame_),
              intermediate_frame_->visible_rect().width(),
              intermediate_frame_->visible_rect().height());
          if (ret != 0)
            return ret;
          return libyuv::I420Scale(
              Y_U_V_DATA(intermediate_frame_),
              intermediate_frame_->visible_rect().width(),
              intermediate_frame_->visible_rect().height(),
              Y_U_V_DATA_W(output), output->visible_rect().width(),
              output->visible_rect().height(), libyuv::kFilterBilinear);
        }
      default:
        VLOGF(1) << "Unexpected input format: " << input->format();
        return -1;
    }
  }

  if (output->format() == PIXEL_FORMAT_P016LE) {
    if (input_config_.fourcc == Fourcc(Fourcc::MT2T)) {
      return LIBYUV_FUNC(MT2TToP010, Y_UV_DATA(input),
                         Y_UV_DATA_W_10BIT(output));
    }
  }

#if BUILDFLAG(IS_LINUX)
  if (output->format() == PIXEL_FORMAT_ARGB) {
    if (input_config_.fourcc == Fourcc(Fourcc::NV12)) {
      return LIBYUV_FUNC(NV12ToARGB, Y_UV_DATA(input),
                         ARGB_DATA(output));
    }
  }
#endif

#undef Y_U_V_DATA
#undef Y_V_U_DATA
#undef Y_UV_DATA
#undef LIBYUV_FUNC

  VLOGF(1) << "Unexpected output format: " << output->format();
  return -1;
}

bool LibYUVImageProcessorBackend::needs_linear_output_buffers() const {
  return true;
}

std::vector<Fourcc> LibYUVImageProcessorBackend::GetSupportedOutputFormats(
    Fourcc input_format) {
  std::vector<Fourcc> supported_formats;
  for (const auto& conv : kSupportFormatConversionArray) {
    if (Fourcc::FromUint32(conv.input) &&
        *Fourcc::FromUint32(conv.input) == input_format &&
        Fourcc::FromUint32(conv.output))
      supported_formats.emplace_back(*Fourcc::FromUint32(conv.output));
  }
  return supported_formats;
}

bool LibYUVImageProcessorBackend::supports_incoherent_buffers() const {
  return true;
}

}  // namespace media
