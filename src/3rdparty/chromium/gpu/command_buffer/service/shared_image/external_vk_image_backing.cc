// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/command_buffer/service/shared_image/external_vk_image_backing.h"

#include <utility>
#include <vector>

#include "base/memory/raw_ptr_exclusion.h"
#include "build/build_config.h"
#include "components/viz/common/resources/resource_format_utils.h"
#include "components/viz/common/resources/resource_sizes.h"
#include "gpu/command_buffer/service/gl_utils.h"
#include "gpu/command_buffer/service/shared_image/external_vk_image_gl_representation.h"
#include "gpu/command_buffer/service/shared_image/external_vk_image_overlay_representation.h"
#include "gpu/command_buffer/service/shared_image/external_vk_image_skia_representation.h"
#include "gpu/command_buffer/service/shared_image/gl_texture_holder.h"
#include "gpu/command_buffer/service/shared_image/shared_image_format_utils.h"
#include "gpu/command_buffer/service/skia_utils.h"
#include "gpu/ipc/common/vulkan_ycbcr_info.h"
#include "gpu/vulkan/vma_wrapper.h"
#include "gpu/vulkan/vulkan_command_buffer.h"
#include "gpu/vulkan/vulkan_command_pool.h"
#include "gpu/vulkan/vulkan_device_queue.h"
#include "gpu/vulkan/vulkan_fence_helper.h"
#include "gpu/vulkan/vulkan_image.h"
#include "gpu/vulkan/vulkan_implementation.h"
#include "gpu/vulkan/vulkan_util.h"
#include "third_party/abseil-cpp/absl/cleanup/cleanup.h"
#include "third_party/skia/include/gpu/GrBackendSemaphore.h"
#include "ui/gfx/buffer_format_util.h"
#include "ui/gl/buildflags.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_utils.h"
#include "ui/gl/gl_version_info.h"
#include "ui/gl/scoped_binders.h"

#if (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)) && BUILDFLAG(USE_DAWN)
#include "gpu/command_buffer/service/shared_image/external_vk_image_dawn_representation.h"
#endif

#if BUILDFLAG(IS_FUCHSIA)
#include "gpu/vulkan/fuchsia/vulkan_fuchsia_ext.h"
#endif

#define GL_DEDICATED_MEMORY_OBJECT_EXT 0x9581
#define GL_TEXTURE_TILING_EXT 0x9580
#define GL_TILING_TYPES_EXT 0x9583
#define GL_OPTIMAL_TILING_EXT 0x9584
#define GL_LINEAR_TILING_EXT 0x9585
#define GL_HANDLE_TYPE_OPAQUE_FD_EXT 0x9586
#define GL_HANDLE_TYPE_OPAQUE_WIN32_EXT 0x9587
#define GL_HANDLE_TYPE_ZIRCON_VMO_ANGLE 0x93AE
#define GL_HANDLE_TYPE_ZIRCON_EVENT_ANGLE 0x93AF

namespace gpu {

namespace {

class ScopedDedicatedMemoryObject {
 public:
  explicit ScopedDedicatedMemoryObject(gl::GLApi* api) : api_(api) {
    api_->glCreateMemoryObjectsEXTFn(1, &id_);
    int dedicated = GL_TRUE;
    api_->glMemoryObjectParameterivEXTFn(id_, GL_DEDICATED_MEMORY_OBJECT_EXT,
                                         &dedicated);
  }
  ~ScopedDedicatedMemoryObject() { api_->glDeleteMemoryObjectsEXTFn(1, &id_); }

  GLuint id() const { return id_; }

 private:
  // This field is not a raw_ptr<> because it was filtered by the rewriter for:
  // #union
  RAW_PTR_EXCLUSION gl::GLApi* const api_;
  GLuint id_;
};

bool UseSeparateGLTexture(SharedContextState* context_state,
                          viz::SharedImageFormat format) {
  if (!context_state->support_vulkan_external_object())
    return true;

  if (format != viz::SinglePlaneFormat::kBGRA_8888) {
    return false;
  }

  auto* gl_context = context_state->real_context();
  const auto* version_info = gl_context->GetVersionInfo();
  const auto& ext = gl_context->GetCurrentGL()->Driver->ext;
  if (!ext.b_GL_EXT_texture_format_BGRA8888)
    return true;

  if (!version_info->is_angle)
    return false;

  // If ANGLE is using vulkan, there is no problem for importing BGRA8888
  // textures.
  if (version_info->is_angle_vulkan)
    return false;

  // ANGLE claims GL_EXT_texture_format_BGRA8888, but glTexStorageMem2DEXT
  // doesn't work correctly.
  // TODO(crbug.com/angleproject/4831): fix ANGLE and return false.
  return true;
}

bool UseTexStorage2D(SharedContextState* context_state) {
  auto* gl_context = context_state->real_context();
  const auto* version_info = gl_context->GetVersionInfo();
  const auto& ext = gl_context->GetCurrentGL()->Driver->ext;
  return ext.b_GL_EXT_texture_storage || ext.b_GL_ARB_texture_storage ||
         version_info->is_es3 || version_info->IsAtLeastGL(4, 2);
}

bool UseMinimalUsageFlags(SharedContextState* context_state) {
  return context_state->support_gl_external_object_flags();
}

void WaitSemaphoresOnGrContext(GrDirectContext* gr_context,
                               std::vector<ExternalSemaphore>* semaphores) {
  DCHECK(!gr_context->abandoned());
  std::vector<GrBackendSemaphore> backend_senampres;
  backend_senampres.reserve(semaphores->size());
  for (auto& semaphore : *semaphores) {
    backend_senampres.emplace_back();
    backend_senampres.back().initVulkan(semaphore.GetVkSemaphore());
  }
  gr_context->wait(backend_senampres.size(), backend_senampres.data(),
                   /*deleteSemaphoreAfterWait=*/false);
}

}  // namespace

// static
std::unique_ptr<ExternalVkImageBacking> ExternalVkImageBacking::Create(
    scoped_refptr<SharedContextState> context_state,
    VulkanCommandPool* command_pool,
    const Mailbox& mailbox,
    viz::SharedImageFormat format,
    const gfx::Size& size,
    const gfx::ColorSpace& color_space,
    GrSurfaceOrigin surface_origin,
    SkAlphaType alpha_type,
    uint32_t usage,
    const base::flat_map<VkFormat, VkImageUsageFlags>& image_usage_cache,
    base::span<const uint8_t> pixel_data) {
  bool is_external = context_state->support_vulkan_external_object();

  auto* device_queue = context_state->vk_context_provider()->GetDeviceQueue();
  VkFormat vk_format = ToVkFormat(format);
  constexpr auto kUsageNeedsColorAttachment =
      SHARED_IMAGE_USAGE_GLES2 | SHARED_IMAGE_USAGE_GLES2_FRAMEBUFFER_HINT |
      SHARED_IMAGE_USAGE_RASTER | SHARED_IMAGE_USAGE_OOP_RASTERIZATION |
      SHARED_IMAGE_USAGE_WEBGPU;
  VkImageUsageFlags vk_usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (usage & kUsageNeedsColorAttachment) {
    vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    if (format.IsCompressed()) {
      DLOG(ERROR) << "ETC1 format cannot be used as color attachment.";
      return nullptr;
    }
  }

  auto it = image_usage_cache.find(vk_format);
  DCHECK(it != image_usage_cache.end());
  auto vk_tiling_usage = it->second;

  // Requested usage flags must be supported.
  DCHECK_EQ(vk_usage & vk_tiling_usage, vk_usage);

  // Must request all available image usage flags if aliasing GL texture. This
  // is a spec requirement per EXT_memory_object. However, if
  // ANGLE_memory_object_flags is supported, usage flags can be arbitrary.
  if (is_external && (usage & SHARED_IMAGE_USAGE_GLES2) &&
      !UseMinimalUsageFlags(context_state.get())) {
    vk_usage |= vk_tiling_usage;
  }

  VkImageCreateFlags vk_flags = 0;

  std::unique_ptr<VulkanImage> image;
  if (is_external) {
    image = VulkanImage::CreateWithExternalMemory(device_queue, size, vk_format,
                                                  vk_usage, vk_flags,
                                                  VK_IMAGE_TILING_OPTIMAL);
  } else {
    image = VulkanImage::Create(device_queue, size, vk_format, vk_usage,
                                vk_flags, VK_IMAGE_TILING_OPTIMAL);
  }
  if (!image)
    return nullptr;

  bool use_separate_gl_texture =
      UseSeparateGLTexture(context_state.get(), format);
  auto backing = std::make_unique<ExternalVkImageBacking>(
      base::PassKey<ExternalVkImageBacking>(), mailbox, format, size,
      color_space, surface_origin, alpha_type, usage, std::move(context_state),
      std::move(image), command_pool, use_separate_gl_texture);

  if (!pixel_data.empty()) {
    size_t stride = BitsPerPixel(format) / 8 * size.width();
    SkPixmap pixmap(backing->AsSkImageInfo(), pixel_data.data(), stride);
    backing->UploadToVkImage(pixmap);

    // Mark the backing as cleared.
    backing->SetCleared();
    backing->latest_content_ = kInVkImage;
  }

  return backing;
}

// static
std::unique_ptr<ExternalVkImageBacking> ExternalVkImageBacking::CreateFromGMB(
    scoped_refptr<SharedContextState> context_state,
    VulkanCommandPool* command_pool,
    const Mailbox& mailbox,
    gfx::GpuMemoryBufferHandle handle,
    gfx::BufferFormat buffer_format,
    const gfx::Size& size,
    const gfx::ColorSpace& color_space,
    GrSurfaceOrigin surface_origin,
    SkAlphaType alpha_type,
    uint32_t usage) {
  if (!gpu::IsImageSizeValidForGpuMemoryBufferFormat(size, buffer_format)) {
    DLOG(ERROR) << "Invalid image size for format.";
    return nullptr;
  }

  auto* vulkan_implementation =
      context_state->vk_context_provider()->GetVulkanImplementation();
  auto si_format = viz::SharedImageFormat::SinglePlane(
      viz::GetResourceFormat(buffer_format));
  auto* device_queue = context_state->vk_context_provider()->GetDeviceQueue();
  DCHECK(vulkan_implementation->CanImportGpuMemoryBuffer(device_queue,
                                                         handle.type));

  VkFormat vk_format = ToVkFormat(si_format);
  auto image = vulkan_implementation->CreateImageFromGpuMemoryHandle(
      device_queue, std::move(handle), size, vk_format, color_space);
  if (!image) {
    DLOG(ERROR) << "Failed to create VkImage from GpuMemoryHandle.";
    return nullptr;
  }

  bool use_separate_gl_texture =
      UseSeparateGLTexture(context_state.get(), si_format);
  auto backing = std::make_unique<ExternalVkImageBacking>(
      base::PassKey<ExternalVkImageBacking>(), mailbox, si_format, size,
      color_space, surface_origin, alpha_type, usage, std::move(context_state),
      std::move(image), command_pool, use_separate_gl_texture);
  backing->SetCleared();
  return backing;
}

ExternalVkImageBacking::ExternalVkImageBacking(
    base::PassKey<ExternalVkImageBacking>,
    const Mailbox& mailbox,
    viz::SharedImageFormat format,
    const gfx::Size& size,
    const gfx::ColorSpace& color_space,
    GrSurfaceOrigin surface_origin,
    SkAlphaType alpha_type,
    uint32_t usage,
    scoped_refptr<SharedContextState> context_state,
    std::unique_ptr<VulkanImage> image,
    VulkanCommandPool* command_pool,
    bool use_separate_gl_texture)
    : ClearTrackingSharedImageBacking(mailbox,
                                      format,
                                      size,
                                      color_space,
                                      surface_origin,
                                      alpha_type,
                                      usage,
                                      image->device_size(),
                                      false /* is_thread_safe */),
      context_state_(std::move(context_state)),
      image_(std::move(image)),
      backend_texture_(size.width(),
                       size.height(),
                       CreateGrVkImageInfo(image_.get())),
      promise_texture_(SkPromiseImageTexture::Make(backend_texture_)),
      command_pool_(command_pool),
      use_separate_gl_texture_(use_separate_gl_texture) {}

ExternalVkImageBacking::~ExternalVkImageBacking() {
  auto semaphores = std::move(read_semaphores_);
  if (write_semaphore_)
    semaphores.emplace_back(std::move(write_semaphore_));

  if (!semaphores.empty() && !context_state()->gr_context()->abandoned()) {
    WaitSemaphoresOnGrContext(context_state()->gr_context(), &semaphores);
    ReturnPendingSemaphoresWithFenceHelper(std::move(semaphores));
  }

  fence_helper()->EnqueueVulkanObjectCleanupForSubmittedWork(std::move(image_));
  backend_texture_ = GrBackendTexture();

  if (gl_texture_) {
    // Ensure that a context is current before glDeleteTexture().
    MakeGLContextCurrent();
    if (!have_context()) {
      gl_texture_->SetContextLost();
    }
    gl_texture_.reset();
  }
}

bool ExternalVkImageBacking::BeginAccess(
    bool readonly,
    std::vector<ExternalSemaphore>* external_semaphores,
    bool is_gl) {
  DLOG_IF(ERROR, gl_reads_in_progress_ != 0 && !is_gl)
      << "Backing is being accessed by both GL and Vulkan.";
  // Do not need do anything for the second and following GL read access.
  if (is_gl && readonly && gl_reads_in_progress_) {
    ++gl_reads_in_progress_;
    return true;
  }

  if (readonly && !reads_in_progress_) {
    UpdateContent(kInVkImage);
    if (gl_texture_) {
      UpdateContent(kInGLTexture);
    }
  }

  if (gl_reads_in_progress_ && need_synchronization()) {
    // To avoid concurrent read access from both GL and vulkan, if there is
    // unfinished GL read access, we will release the GL texture temporarily.
    // And when this vulkan access is over, we will acquire the GL texture to
    // resume the GL access.
    DCHECK(!is_gl);
    DCHECK(readonly);
    DCHECK(gl_texture_);

    GLuint texture_id = gl_texture_->GetServiceId();
    MakeGLContextCurrent();

    GrVkImageInfo info;
    auto result = backend_texture_.getVkImageInfo(&info);
    DCHECK(result);
    DCHECK_EQ(info.fCurrentQueueFamily, VK_QUEUE_FAMILY_EXTERNAL);
    DCHECK_NE(info.fImageLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    DCHECK_NE(info.fImageLayout, VK_IMAGE_LAYOUT_PREINITIALIZED);
    auto release_semaphore =
        ExternalVkImageGLRepresentationShared::ReleaseTexture(
            external_semaphore_pool(), texture_id, info.fImageLayout);
    if (!release_semaphore) {
      context_state_->MarkContextLost();
      return false;
    }
    EndAccessInternal(readonly, std::move(release_semaphore));
  }

  if (!BeginAccessInternal(readonly, external_semaphores))
    return false;

  if (!is_gl)
    return true;

  if (need_synchronization() && external_semaphores->empty()) {
    // For the first time GL BeginAccess(), external_semaphores could be empty,
    // since the Vulkan usage will not provide semaphore for EndAccess() call,
    // if ProduceGL*() is never called. In this case, image layout and queue
    // family will not be ready for GL access as well.
    auto* gr_context = context_state()->gr_context();
    gr_context->setBackendTextureState(
        backend_texture_,
        GrBackendSurfaceMutableState(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                     VK_QUEUE_FAMILY_EXTERNAL));

    ExternalSemaphore external_semaphore =
        external_semaphore_pool()->GetOrCreateSemaphore();
    GrBackendSemaphore semaphore;
    semaphore.initVulkan(external_semaphore.GetVkSemaphore());

    GrFlushInfo flush_info;
    flush_info.fNumSemaphores = 1;
    flush_info.fSignalSemaphores = &semaphore;

    if (gr_context->flush(flush_info) != GrSemaphoresSubmitted::kYes) {
      LOG(ERROR) << "Failed to create a signaled semaphore";
      return false;
    }

    if (!gr_context->submit()) {
      LOG(ERROR) << "Failed GrContext submit";
      return false;
    }

    external_semaphores->push_back(std::move(external_semaphore));
  }

  if (readonly) {
    DCHECK(!gl_reads_in_progress_);
    gl_reads_in_progress_ = 1;
  }
  return true;
}

void ExternalVkImageBacking::EndAccess(bool readonly,
                                       ExternalSemaphore external_semaphore,
                                       bool is_gl) {
  if (is_gl && readonly) {
    DCHECK(gl_reads_in_progress_);
    if (--gl_reads_in_progress_ > 0) {
      DCHECK(!external_semaphore);
      return;
    }
  }

  EndAccessInternal(readonly, std::move(external_semaphore));
  if (!readonly) {
    if (use_separate_gl_texture()) {
      latest_content_ = is_gl ? kInGLTexture : kInVkImage;
    } else {
      latest_content_ = kInVkImage;
    }
  }

  if (gl_reads_in_progress_ && need_synchronization()) {
    // When vulkan read access is finished, if there is unfinished GL read
    // access, we need to resume GL read access.
    DCHECK(!is_gl);
    DCHECK(readonly);
    DCHECK(gl_texture_);
    GLuint texture_id = gl_texture_->GetServiceId();
    MakeGLContextCurrent();
    std::vector<ExternalSemaphore> external_semaphores;
    BeginAccessInternal(true, &external_semaphores);
    DCHECK_LE(external_semaphores.size(), 1u);

    for (auto& semaphore : external_semaphores) {
      GrVkImageInfo info;
      auto result = backend_texture_.getVkImageInfo(&info);
      DCHECK(result);
      DCHECK_EQ(info.fCurrentQueueFamily, VK_QUEUE_FAMILY_EXTERNAL);
      DCHECK_NE(info.fImageLayout, VK_IMAGE_LAYOUT_UNDEFINED);
      DCHECK_NE(info.fImageLayout, VK_IMAGE_LAYOUT_PREINITIALIZED);
      ExternalVkImageGLRepresentationShared::AcquireTexture(
          &semaphore, texture_id, info.fImageLayout);
    }
    // |external_semaphores| has been waited on a GL context, so it can not be
    // reused until a vulkan GPU work depends on the following GL task is over.
    // So add it to the pending semaphores list, and they will be returned to
    // external semaphores pool when the next skia access is over.
    AddSemaphoresToPendingListOrRelease(std::move(external_semaphores));
  }
}

SharedImageBackingType ExternalVkImageBacking::GetType() const {
  return SharedImageBackingType::kExternalVkImage;
}

void ExternalVkImageBacking::Update(std::unique_ptr<gfx::GpuFence> in_fence) {
  DCHECK(!in_fence);
}

bool ExternalVkImageBacking::UploadFromMemory(
    const std::vector<SkPixmap>& pixmaps) {
  DCHECK_EQ(pixmaps.size(), 1u);
  auto& pixmap = pixmaps[0];

  if (!UploadToVkImage(pixmap))
    return false;

  SetCleared();
  latest_content_ = kInVkImage;

  // Also upload to GL texture if there is a separate one.
  if (use_separate_gl_texture() && gl_texture_) {
    UploadToGLTexture(pixmap);
    latest_content_ |= kInGLTexture;
  }

  return true;
}

void ExternalVkImageBacking::AddSemaphoresToPendingListOrRelease(
    std::vector<ExternalSemaphore> semaphores) {
  constexpr size_t kMaxPendingSemaphores = 4;
  DCHECK_LE(pending_semaphores_.size(), kMaxPendingSemaphores);

#if DCHECK_IS_ON()
  for (auto& semaphore : semaphores)
    DCHECK(semaphore);
#endif
  while (pending_semaphores_.size() < kMaxPendingSemaphores &&
         !semaphores.empty()) {
    pending_semaphores_.push_back(std::move(semaphores.back()));
    semaphores.pop_back();
  }
  if (!semaphores.empty()) {
    // |semaphores| may contain VkSemephores which are submitted to queue for
    // signalling but have not been signalled. In that case, we have to release
    // them via fence helper to make sure all submitted GPU works is finished
    // before releasing them.
    fence_helper()->EnqueueCleanupTaskForSubmittedWork(base::BindOnce(
        [](scoped_refptr<SharedContextState> shared_context_state,
           std::vector<ExternalSemaphore>, VulkanDeviceQueue* device_queue,
           bool device_lost) {
          if (!gl::GLContext::GetCurrent()) {
            shared_context_state->MakeCurrent(/*surface=*/nullptr,
                                              /*needs_gl=*/true);
          }
        },
        context_state_, std::move(semaphores)));
  }
}

scoped_refptr<gfx::NativePixmap> ExternalVkImageBacking::GetNativePixmap() {
  return image_->native_pixmap();
}

void ExternalVkImageBacking::ReturnPendingSemaphoresWithFenceHelper(
    std::vector<ExternalSemaphore> semaphores) {
  std::move(semaphores.begin(), semaphores.end(),
            std::back_inserter(pending_semaphores_));
  external_semaphore_pool()->ReturnSemaphoresWithFenceHelper(
      std::move(pending_semaphores_));
  pending_semaphores_.clear();
}

std::unique_ptr<DawnImageRepresentation> ExternalVkImageBacking::ProduceDawn(
    SharedImageManager* manager,
    MemoryTypeTracker* tracker,
    WGPUDevice wgpuDevice,
    WGPUBackendType backend_type,
    std::vector<WGPUTextureFormat> view_formats) {
#if (BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)) && BUILDFLAG(USE_DAWN)
  auto wgpu_format = ToWGPUFormat(format());

  if (wgpu_format == WGPUTextureFormat_Undefined) {
    DLOG(ERROR) << "Format not supported for Dawn";
    return nullptr;
  }

  GrVkImageInfo image_info;
  bool result = backend_texture_.getVkImageInfo(&image_info);
  DCHECK(result);

  auto memory_fd = image_->GetMemoryFd();
  if (!memory_fd.is_valid()) {
    return nullptr;
  }

  return std::make_unique<ExternalVkImageDawnImageRepresentation>(
      manager, this, tracker, wgpuDevice, wgpu_format, std::move(view_formats),
      std::move(memory_fd));
#else  // (!BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CHROMEOS)) ||
       // !BUILDFLAG(USE_DAWN)
  NOTIMPLEMENTED_LOG_ONCE();
  return nullptr;
#endif
}

bool ExternalVkImageBacking::MakeGLContextCurrent() {
  if (gl::GLContext::GetCurrent()) {
    return true;
  }
  return context_state()->MakeCurrent(/*surface=*/nullptr, /*needs_gl=*/true);
}

bool ExternalVkImageBacking::ProduceGLTextureInternal(bool is_passthrough) {
  gl::GLApi* api = gl::g_current_gl_context;
  absl::optional<ScopedDedicatedMemoryObject> memory_object;
  if (!use_separate_gl_texture()) {
    GrVkImageInfo image_info;
    bool result = backend_texture_.getVkImageInfo(&image_info);
    DCHECK(result);

#if BUILDFLAG(IS_POSIX)
    auto memory_fd = image_->GetMemoryFd();
    if (!memory_fd.is_valid()) {
      return false;
    }
    memory_object.emplace(api);
    api->glImportMemoryFdEXTFn(memory_object->id(), image_info.fAlloc.fSize,
                               GL_HANDLE_TYPE_OPAQUE_FD_EXT,
                               memory_fd.release());
#elif BUILDFLAG(IS_WIN)
    auto memory_handle = image_->GetMemoryHandle();
    if (!memory_handle.IsValid()) {
      return false;
    }
    memory_object.emplace(api);
    api->glImportMemoryWin32HandleEXTFn(
        memory_object->id(), image_info.fAlloc.fSize,
        GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, memory_handle.Take());
#elif BUILDFLAG(IS_FUCHSIA)
    zx::vmo vmo = image_->GetMemoryZirconHandle();
    if (!vmo)
      return false;
    memory_object.emplace(api);
    api->glImportMemoryZirconHandleANGLEFn(
        memory_object->id(), image_info.fAlloc.fSize,
        GL_HANDLE_TYPE_ZIRCON_VMO_ANGLE, vmo.release());
#else
#error Unsupported OS
#endif
  }

  bool use_rgbx = context_state()
                      ->feature_info()
                      ->feature_flags()
                      .angle_rgbx_internal_format;
  GLFormatDesc format_desc =
      ToGLFormatDesc(format(), /*plane_index=*/0, use_rgbx);

  GLuint texture_service_id = 0;
  api->glGenTexturesFn(1, &texture_service_id);
  gl::ScopedTextureBinder scoped_texture_binder(GL_TEXTURE_2D,
                                                texture_service_id);
  api->glTexParameteriFn(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  api->glTexParameteriFn(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  api->glTexParameteriFn(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  api->glTexParameteriFn(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  if (use_separate_gl_texture()) {
    DCHECK(!memory_object);
    if (UseTexStorage2D(context_state_.get())) {
      api->glTexStorage2DEXTFn(GL_TEXTURE_2D, 1,
                               format_desc.storage_internal_format,
                               size().width(), size().height());
    } else {
      api->glTexImage2DFn(GL_TEXTURE_2D, 0, format_desc.image_internal_format,
                          size().width(), size().height(), 0,
                          format_desc.data_format, format_desc.data_type,
                          nullptr);
    }
  } else {
    DCHECK(memory_object);
    // If ANGLE_memory_object_flags is supported, use that to communicate the
    // exact create and usage flags the image was created with.
    //
    // Currently, no extension structs are appended to VkImageCreateInfo::pNext
    // when creating the image, so communicate that information to ANGLE.  This
    // makes sure that ANGLE recreates the VkImage identically to Chromium.
    DCHECK(image_->usage() != 0);
    if (UseMinimalUsageFlags(context_state())) {
      api->glTexStorageMemFlags2DANGLEFn(
          GL_TEXTURE_2D, 1, format_desc.storage_internal_format, size().width(),
          size().height(), memory_object->id(), 0, image_->flags(),
          image_->usage(), nullptr);
    } else {
      api->glTexStorageMem2DEXTFn(
          GL_TEXTURE_2D, 1, format_desc.storage_internal_format, size().width(),
          size().height(), memory_object->id(), 0);
    }
  }

  gl_texture_ = std::make_unique<GLTextureHolder>(
      format().resource_format(), size(), is_passthrough, nullptr);

  if (is_passthrough) {
    auto texture = base::MakeRefCounted<gpu::gles2::TexturePassthrough>(
        texture_service_id, GL_TEXTURE_2D, format_desc.storage_internal_format,
        size().width(), size().height(),
        /*depth=*/1, /*border=*/0, format_desc.data_format,
        format_desc.data_type);
    gl_texture_->InitializeWithTexture(format_desc, std::move(texture));
  } else {
    auto* texture = gles2::CreateGLES2TextureWithLightRef(texture_service_id,
                                                          GL_TEXTURE_2D);
    // If the backing is already cleared, no need to clear it again.
    gfx::Rect cleared_rect;
    if (IsCleared()) {
      cleared_rect = gfx::Rect(size());
    }

    texture->SetLevelInfo(GL_TEXTURE_2D, 0, format_desc.storage_internal_format,
                          size().width(), size().height(), 1, 0,
                          format_desc.data_format, format_desc.data_type,
                          cleared_rect);
    texture->SetImmutable(true, true);
    gl_texture_->InitializeWithTexture(format_desc, texture);
  }

  return true;
}

std::unique_ptr<GLTextureImageRepresentation>
ExternalVkImageBacking::ProduceGLTexture(SharedImageManager* manager,
                                         MemoryTypeTracker* tracker) {
  if (!(usage() & SHARED_IMAGE_USAGE_GLES2)) {
    DLOG(ERROR) << "The backing is not created with GLES2 usage.";
    return nullptr;
  }

  if (!gl_texture_) {
    if (!ProduceGLTextureInternal(/*is_passthrough=*/false)) {
      return nullptr;
    }
  }
  return std::make_unique<ExternalVkImageGLRepresentation>(
      manager, this, tracker, gl_texture_->texture());
}

std::unique_ptr<GLTexturePassthroughImageRepresentation>
ExternalVkImageBacking::ProduceGLTexturePassthrough(
    SharedImageManager* manager,
    MemoryTypeTracker* tracker) {
  if (!(usage() & SHARED_IMAGE_USAGE_GLES2)) {
    DLOG(ERROR) << "The backing is not created with GLES2 usage.";
    return nullptr;
  }

  if (!gl_texture_) {
    if (!ProduceGLTextureInternal(/*is_passthrough=*/true)) {
      return nullptr;
    }
  }

  return std::make_unique<ExternalVkImageGLPassthroughRepresentation>(
      manager, this, tracker, gl_texture_->passthrough_texture());
}

std::unique_ptr<SkiaImageRepresentation> ExternalVkImageBacking::ProduceSkia(
    SharedImageManager* manager,
    MemoryTypeTracker* tracker,
    scoped_refptr<SharedContextState> context_state) {
  // This backing type is only used when vulkan is enabled, so SkiaRenderer
  // should also be using Vulkan.
  DCHECK_EQ(context_state_, context_state);
  DCHECK(context_state->GrContextIsVulkan());
  return std::make_unique<ExternalVkImageSkiaImageRepresentation>(manager, this,
                                                                  tracker);
}

std::unique_ptr<OverlayImageRepresentation>
ExternalVkImageBacking::ProduceOverlay(SharedImageManager* manager,
                                       MemoryTypeTracker* tracker) {
  return std::make_unique<ExternalVkImageOverlayImageRepresentation>(
      manager, this, tracker);
}

void ExternalVkImageBacking::UpdateContent(uint32_t content_flags) {
  // Only support one backing for now.
  DCHECK(content_flags == kInVkImage || content_flags == kInGLTexture);

  // There is no need to update content when there is only one texture.
  if (!use_separate_gl_texture())
    return;

  if ((latest_content_ & content_flags) == content_flags)
    return;

  if (content_flags == kInVkImage) {
    if ((latest_content_ & kInGLTexture)) {
      CopyPixelsFromGLTextureToVkImage();
      latest_content_ |= kInVkImage;
    }
  } else if (content_flags == kInGLTexture) {
    if (latest_content_ & kInVkImage) {
      CopyPixelsFromVkImageToGLTexture();
      latest_content_ |= kInGLTexture;
    }
  }
}

void ExternalVkImageBacking::CopyPixelsFromGLTextureToVkImage() {
  DCHECK(use_separate_gl_texture());
  DCHECK(gl_texture_);

  // Make sure GrContext is not using GL. So we don't need reset GrContext
  DCHECK(!context_state_->GrContextIsGL());

  // Make sure a gl context is current, since textures are shared between all gl
  // contexts, we don't care which gl context is current.
  if (!MakeGLContextCurrent()) {
    return;
  }

  SkImageInfo sk_image_info = AsSkImageInfo();
  size_t data_size = sk_image_info.computeMinByteSize();

  VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = data_size,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocator allocator =
      context_state()->vk_context_provider()->GetDeviceQueue()->vma_allocator();
  VkBuffer stage_buffer = VK_NULL_HANDLE;
  VmaAllocation stage_allocation = VK_NULL_HANDLE;
  VkResult result = vma::CreateBuffer(allocator, &buffer_create_info,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      0, &stage_buffer, &stage_allocation);
  if (result != VK_SUCCESS) {
    DLOG(ERROR) << "vkCreateBuffer() failed." << result;
    return;
  }

  absl::Cleanup destroy_buffer = [&]() {
    vma::DestroyBuffer(allocator, stage_buffer, stage_allocation);
  };

  void* buffer = nullptr;
  result = vma::MapMemory(allocator, stage_allocation, &buffer);
  if (result != VK_SUCCESS) {
    DLOG(ERROR) << "vma::MapMemory() failed. " << result;
    return;
  }

  SkPixmap pixmap(sk_image_info, buffer, sk_image_info.minRowBytes());
  if (!gl_texture_->ReadbackToMemory(pixmap)) {
    DLOG(ERROR) << "GL readback failed";
    vma::UnmapMemory(allocator, stage_allocation);
    return;
  }

  vma::UnmapMemory(allocator, stage_allocation);

  std::vector<ExternalSemaphore> external_semaphores;
  if (!BeginAccessInternal(/*readonly=*/false, &external_semaphores)) {
    DLOG(ERROR) << "BeginAccess() failed.";
    return;
  }

  auto command_buffer = command_pool_->CreatePrimaryCommandBuffer();
  CHECK(command_buffer);
  {
    ScopedSingleUseCommandBufferRecorder recorder(*command_buffer);
    GrVkImageInfo image_info;
    bool success = backend_texture_.getVkImageInfo(&image_info);
    DCHECK(success);
    if (image_info.fImageLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
      command_buffer->TransitionImageLayout(
          image_info.fImage, image_info.fImageLayout,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      backend_texture_.setVkImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }
    command_buffer->CopyBufferToImage(stage_buffer, image_info.fImage,
                                      size().width(), size().height(),
                                      size().width(), size().height());
  }

  SetCleared();

  // Everything was successful so `stage_buffer` + `stage_allocation` ownership
  // will be passed to EnqueueBufferCleanupForSubmittedWork().
  std::move(destroy_buffer).Cancel();

  if (!need_synchronization()) {
    DCHECK(external_semaphores.empty());
    command_buffer->Submit(0, nullptr, 0, nullptr);
    EndAccessInternal(/*readonly=*/false, ExternalSemaphore());

    fence_helper()->EnqueueVulkanObjectCleanupForSubmittedWork(
        std::move(command_buffer));
    fence_helper()->EnqueueBufferCleanupForSubmittedWork(stage_buffer,
                                                         stage_allocation);
    return;
  }

  std::vector<VkSemaphore> begin_access_semaphores;
  begin_access_semaphores.reserve(external_semaphores.size());
  for (auto& external_semaphore : external_semaphores) {
    begin_access_semaphores.emplace_back(external_semaphore.GetVkSemaphore());
  }

  auto end_access_semaphore = external_semaphore_pool()->GetOrCreateSemaphore();
  VkSemaphore vk_end_access_semaphore = end_access_semaphore.GetVkSemaphore();
  command_buffer->Submit(begin_access_semaphores.size(),
                         begin_access_semaphores.data(), 1,
                         &vk_end_access_semaphore);

  EndAccessInternal(/*readonly=*/false, std::move(end_access_semaphore));
  // |external_semaphores| have been waited on and can be reused when submitted
  // GPU work is done.
  ReturnPendingSemaphoresWithFenceHelper(std::move(external_semaphores));

  fence_helper()->EnqueueVulkanObjectCleanupForSubmittedWork(
      std::move(command_buffer));
  fence_helper()->EnqueueBufferCleanupForSubmittedWork(stage_buffer,
                                                       stage_allocation);
}

void ExternalVkImageBacking::CopyPixelsFromVkImageToGLTexture() {
  DCHECK(use_separate_gl_texture());
  DCHECK(gl_texture_);

  // Make sure GrContext is not using GL. So we don't need reset GrContext
  DCHECK(!context_state_->GrContextIsGL());

  // Make sure a gl context is current, since textures are shared between all gl
  // contexts, we don't care which gl context is current.
  if (!MakeGLContextCurrent()) {
    return;
  }

  SkImageInfo sk_image_info = AsSkImageInfo();
  size_t data_size = sk_image_info.computeMinByteSize();

  VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = data_size,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocator allocator =
      context_state()->vk_context_provider()->GetDeviceQueue()->vma_allocator();
  VkBuffer stage_buffer = VK_NULL_HANDLE;
  VmaAllocation stage_allocation = VK_NULL_HANDLE;
  VkResult result = vma::CreateBuffer(allocator, &buffer_create_info,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      0, &stage_buffer, &stage_allocation);
  if (result != VK_SUCCESS) {
    DLOG(ERROR) << "vkCreateBuffer() failed." << result;
    return;
  }

  absl::Cleanup destroy_buffer = [&]() {
    vma::DestroyBuffer(allocator, stage_buffer, stage_allocation);
  };

  // ReadPixelsWithCallback() is only called for separate texture.
  DCHECK(!need_synchronization());

  std::vector<ExternalSemaphore> external_semaphores;
  if (!BeginAccessInternal(/*readonly=*/true, &external_semaphores)) {
    DLOG(ERROR) << "BeginAccess() failed.";
    return;
  }
  DCHECK(external_semaphores.empty());

  auto command_buffer = command_pool_->CreatePrimaryCommandBuffer();
  CHECK(command_buffer);
  {
    ScopedSingleUseCommandBufferRecorder recorder(*command_buffer);
    GrVkImageInfo image_info;
    bool success = backend_texture_.getVkImageInfo(&image_info);
    DCHECK(success);
    if (image_info.fImageLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
      command_buffer->TransitionImageLayout(
          image_info.fImage, image_info.fImageLayout,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
      backend_texture_.setVkImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    }
    command_buffer->CopyImageToBuffer(stage_buffer, image_info.fImage,
                                      size().width(), size().height(),
                                      size().width(), size().height());
  }

  command_buffer->Submit(0, nullptr, 0, nullptr);
  command_buffer->Wait(UINT64_MAX);
  command_buffer->Destroy();
  EndAccessInternal(/*readonly=*/true, ExternalSemaphore());

  void* buffer = nullptr;
  result = vma::MapMemory(allocator, stage_allocation, &buffer);
  if (result != VK_SUCCESS) {
    DLOG(ERROR) << "vma::MapMemory() failed. " << result;
    return;
  }

  SkPixmap pixmap(sk_image_info, buffer, sk_image_info.minRowBytes());
  if (!gl_texture_->UploadFromMemory(pixmap)) {
    DLOG(ERROR) << "GL upload failed";
  }

  vma::UnmapMemory(allocator, stage_allocation);
}

bool ExternalVkImageBacking::UploadToVkImage(const SkPixmap& pixmap) {
  std::vector<ExternalSemaphore> external_semaphores;
  if (!BeginAccessInternal(/*readonly=*/false, &external_semaphores)) {
    DLOG(ERROR) << "BeginAccess() failed.";
    return false;
  }
  auto* gr_context = context_state_->gr_context();
  WaitSemaphoresOnGrContext(gr_context, &external_semaphores);

  if (!gr_context->updateBackendTexture(backend_texture_, &pixmap,
                                        /*numLevels=*/1, nullptr, nullptr)) {
    DLOG(ERROR) << "updateBackendTexture() failed.";
  }

  if (!need_synchronization()) {
    DCHECK(external_semaphores.empty());
    EndAccessInternal(/*readonly=*/false, ExternalSemaphore());
    return true;
  }

  gr_context->flush({});
  gr_context->setBackendTextureState(
      backend_texture_,
      GrBackendSurfaceMutableState(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_QUEUE_FAMILY_EXTERNAL));

  auto end_access_semaphore = external_semaphore_pool()->GetOrCreateSemaphore();
  VkSemaphore vk_end_access_semaphore = end_access_semaphore.GetVkSemaphore();
  GrBackendSemaphore end_access_backend_semaphore;
  end_access_backend_semaphore.initVulkan(vk_end_access_semaphore);
  GrFlushInfo flush_info = {
      .fNumSemaphores = 1,
      .fSignalSemaphores = &end_access_backend_semaphore,
  };
  gr_context->flush(flush_info);

  // Submit so the |end_access_semaphore| is ready for waiting.
  gr_context->submit();

  EndAccessInternal(/*readonly=*/false, std::move(end_access_semaphore));
  // |external_semaphores| have been waited on and can be reused when submitted
  // GPU work is done.
  ReturnPendingSemaphoresWithFenceHelper(std::move(external_semaphores));
  return true;
}

void ExternalVkImageBacking::UploadToGLTexture(const SkPixmap& pixmap) {
  DCHECK(use_separate_gl_texture());
  DCHECK(gl_texture_);

  // Make sure a gl context is current, since textures are shared between all gl
  // contexts, we don't care which gl context is current.
  if (!MakeGLContextCurrent()) {
    return;
  }

  gl_texture_->UploadFromMemory(pixmap);
}

bool ExternalVkImageBacking::BeginAccessInternal(
    bool readonly,
    std::vector<ExternalSemaphore>* external_semaphores) {
  DCHECK(external_semaphores);
  DCHECK(external_semaphores->empty());
  if (is_write_in_progress_) {
    DLOG(ERROR) << "Unable to begin read or write access because another write "
                   "access is in progress";
    return false;
  }

  if (reads_in_progress_ && !readonly) {
    DLOG(ERROR)
        << "Unable to begin write access because a read access is in progress";
    return false;
  }

  if (readonly) {
    DLOG_IF(ERROR, reads_in_progress_)
        << "Concurrent reading may cause problem.";
    ++reads_in_progress_;
    // If a shared image is read repeatedly without any write access,
    // |read_semaphores_| will never be consumed and released, and then
    // chrome will run out of file descriptors. To avoid this problem, we wait
    // on read semaphores for readonly access too. And in most cases, a shared
    // image is only read from one vulkan device queue, so it should not have
    // performance impact.
    // TODO(penghuang): avoid waiting on read semaphores.
    *external_semaphores = std::move(read_semaphores_);
    read_semaphores_.clear();

    // A semaphore will become unsignaled, when it has been signaled and waited,
    // so it is not safe to reuse it.
    if (write_semaphore_)
      external_semaphores->push_back(std::move(write_semaphore_));
  } else {
    is_write_in_progress_ = true;
    *external_semaphores = std::move(read_semaphores_);
    read_semaphores_.clear();
    if (write_semaphore_)
      external_semaphores->push_back(std::move(write_semaphore_));
  }
  return true;
}

void ExternalVkImageBacking::EndAccessInternal(
    bool readonly,
    ExternalSemaphore external_semaphore) {
  if (readonly) {
    DCHECK_GT(reads_in_progress_, 0u);
    --reads_in_progress_;
  } else {
    DCHECK(is_write_in_progress_);
    is_write_in_progress_ = false;
  }

  if (need_synchronization()) {
    DCHECK(!is_write_in_progress_);
    DCHECK(external_semaphore);
    if (readonly) {
      read_semaphores_.push_back(std::move(external_semaphore));
    } else {
      DCHECK(!write_semaphore_);
      DCHECK(read_semaphores_.empty());
      write_semaphore_ = std::move(external_semaphore);
    }
  } else {
    DCHECK(!external_semaphore);
  }
}

}  // namespace gpu