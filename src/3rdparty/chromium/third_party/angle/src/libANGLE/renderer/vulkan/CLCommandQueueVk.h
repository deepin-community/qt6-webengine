//
// Copyright 2021 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// CLCommandQueueVk.h: Defines the class interface for CLCommandQueueVk,
// implementing CLCommandQueueImpl.

#ifndef LIBANGLE_RENDERER_VULKAN_CLCOMMANDQUEUEVK_H_
#define LIBANGLE_RENDERER_VULKAN_CLCOMMANDQUEUEVK_H_

#include "libANGLE/renderer/vulkan/cl_types.h"

#include "libANGLE/renderer/CLCommandQueueImpl.h"

namespace rx
{

class CLCommandQueueVk : public CLCommandQueueImpl
{
  public:
    CLCommandQueueVk(const cl::CommandQueue &commandQueue);
    ~CLCommandQueueVk() override;

    angle::Result setProperty(cl::CommandQueueProperties properties, cl_bool enable) override;

    angle::Result enqueueReadBuffer(const cl::Buffer &buffer,
                                    bool blocking,
                                    size_t offset,
                                    size_t size,
                                    void *ptr,
                                    const cl::EventPtrs &waitEvents,
                                    CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueWriteBuffer(const cl::Buffer &buffer,
                                     bool blocking,
                                     size_t offset,
                                     size_t size,
                                     const void *ptr,
                                     const cl::EventPtrs &waitEvents,
                                     CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueReadBufferRect(const cl::Buffer &buffer,
                                        bool blocking,
                                        const size_t bufferOrigin[3],
                                        const size_t hostOrigin[3],
                                        const size_t region[3],
                                        size_t bufferRowPitch,
                                        size_t bufferSlicePitch,
                                        size_t hostRowPitch,
                                        size_t hostSlicePitch,
                                        void *ptr,
                                        const cl::EventPtrs &waitEvents,
                                        CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueWriteBufferRect(const cl::Buffer &buffer,
                                         bool blocking,
                                         const size_t bufferOrigin[3],
                                         const size_t hostOrigin[3],
                                         const size_t region[3],
                                         size_t bufferRowPitch,
                                         size_t bufferSlicePitch,
                                         size_t hostRowPitch,
                                         size_t hostSlicePitch,
                                         const void *ptr,
                                         const cl::EventPtrs &waitEvents,
                                         CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueCopyBuffer(const cl::Buffer &srcBuffer,
                                    const cl::Buffer &dstBuffer,
                                    size_t srcOffset,
                                    size_t dstOffset,
                                    size_t size,
                                    const cl::EventPtrs &waitEvents,
                                    CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueCopyBufferRect(const cl::Buffer &srcBuffer,
                                        const cl::Buffer &dstBuffer,
                                        const size_t srcOrigin[3],
                                        const size_t dstOrigin[3],
                                        const size_t region[3],
                                        size_t srcRowPitch,
                                        size_t srcSlicePitch,
                                        size_t dstRowPitch,
                                        size_t dstSlicePitch,
                                        const cl::EventPtrs &waitEvents,
                                        CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueFillBuffer(const cl::Buffer &buffer,
                                    const void *pattern,
                                    size_t patternSize,
                                    size_t offset,
                                    size_t size,
                                    const cl::EventPtrs &waitEvents,
                                    CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueMapBuffer(const cl::Buffer &buffer,
                                   bool blocking,
                                   cl::MapFlags mapFlags,
                                   size_t offset,
                                   size_t size,
                                   const cl::EventPtrs &waitEvents,
                                   CLEventImpl::CreateFunc *eventCreateFunc,
                                   void *&mapPtr) override;

    angle::Result enqueueReadImage(const cl::Image &image,
                                   bool blocking,
                                   const size_t origin[3],
                                   const size_t region[3],
                                   size_t rowPitch,
                                   size_t slicePitch,
                                   void *ptr,
                                   const cl::EventPtrs &waitEvents,
                                   CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueWriteImage(const cl::Image &image,
                                    bool blocking,
                                    const size_t origin[3],
                                    const size_t region[3],
                                    size_t inputRowPitch,
                                    size_t inputSlicePitch,
                                    const void *ptr,
                                    const cl::EventPtrs &waitEvents,
                                    CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueCopyImage(const cl::Image &srcImage,
                                   const cl::Image &dstImage,
                                   const size_t srcOrigin[3],
                                   const size_t dstOrigin[3],
                                   const size_t region[3],
                                   const cl::EventPtrs &waitEvents,
                                   CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueFillImage(const cl::Image &image,
                                   const void *fillColor,
                                   const size_t origin[3],
                                   const size_t region[3],
                                   const cl::EventPtrs &waitEvents,
                                   CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueCopyImageToBuffer(const cl::Image &srcImage,
                                           const cl::Buffer &dstBuffer,
                                           const size_t srcOrigin[3],
                                           const size_t region[3],
                                           size_t dstOffset,
                                           const cl::EventPtrs &waitEvents,
                                           CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueCopyBufferToImage(const cl::Buffer &srcBuffer,
                                           const cl::Image &dstImage,
                                           size_t srcOffset,
                                           const size_t dstOrigin[3],
                                           const size_t region[3],
                                           const cl::EventPtrs &waitEvents,
                                           CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueMapImage(const cl::Image &image,
                                  bool blocking,
                                  cl::MapFlags mapFlags,
                                  const size_t origin[3],
                                  const size_t region[3],
                                  size_t *imageRowPitch,
                                  size_t *imageSlicePitch,
                                  const cl::EventPtrs &waitEvents,
                                  CLEventImpl::CreateFunc *eventCreateFunc,
                                  void *&mapPtr) override;

    angle::Result enqueueUnmapMemObject(const cl::Memory &memory,
                                        void *mappedPtr,
                                        const cl::EventPtrs &waitEvents,
                                        CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueMigrateMemObjects(const cl::MemoryPtrs &memObjects,
                                           cl::MemMigrationFlags flags,
                                           const cl::EventPtrs &waitEvents,
                                           CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueNDRangeKernel(const cl::Kernel &kernel,
                                       cl_uint workDim,
                                       const size_t *globalWorkOffset,
                                       const size_t *globalWorkSize,
                                       const size_t *localWorkSize,
                                       const cl::EventPtrs &waitEvents,
                                       CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueTask(const cl::Kernel &kernel,
                              const cl::EventPtrs &waitEvents,
                              CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueNativeKernel(cl::UserFunc userFunc,
                                      void *args,
                                      size_t cbArgs,
                                      const cl::BufferPtrs &buffers,
                                      const std::vector<size_t> bufferPtrOffsets,
                                      const cl::EventPtrs &waitEvents,
                                      CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueMarkerWithWaitList(const cl::EventPtrs &waitEvents,
                                            CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueMarker(CLEventImpl::CreateFunc &eventCreateFunc) override;

    angle::Result enqueueWaitForEvents(const cl::EventPtrs &events) override;

    angle::Result enqueueBarrierWithWaitList(const cl::EventPtrs &waitEvents,
                                             CLEventImpl::CreateFunc *eventCreateFunc) override;

    angle::Result enqueueBarrier() override;

    angle::Result flush() override;

    angle::Result finish() override;
};

}  // namespace rx

#endif  // LIBANGLE_RENDERER_VULKAN_CLCOMMANDQUEUEVK_H_
