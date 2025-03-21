// Copyright 2023 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "dawn/native/EventManager.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "dawn/common/Assert.h"
#include "dawn/common/FutureUtils.h"
#include "dawn/native/ChainUtils.h"
#include "dawn/native/Device.h"
#include "dawn/native/IntegerTypes.h"
#include "dawn/native/Queue.h"
#include "dawn/native/SystemEvent.h"
#include "dawn/native/WaitAnySystemEvent.h"

namespace dawn::native {

namespace {

// Wrapper around an iterator to yield system event receiver and a pointer
// to the ready bool. We pass this into WaitAnySystemEvent so it can extract
// the receivers and get pointers to the ready status - without allocating
// duplicate storage to store the receivers and ready bools.
class SystemEventAndReadyStateIterator {
  public:
    using WrappedIter = std::vector<TrackedFutureWaitInfo>::iterator;

    // Specify required iterator traits.
    using value_type = std::pair<const SystemEventReceiver&, bool*>;
    using difference_type = typename WrappedIter::difference_type;
    using iterator_category = typename WrappedIter::iterator_category;
    using pointer = value_type*;
    using reference = value_type&;

    SystemEventAndReadyStateIterator() = default;
    SystemEventAndReadyStateIterator(const SystemEventAndReadyStateIterator&) = default;
    SystemEventAndReadyStateIterator& operator=(const SystemEventAndReadyStateIterator&) = default;

    explicit SystemEventAndReadyStateIterator(WrappedIter wrappedIt) : mWrappedIt(wrappedIt) {}

    bool operator!=(const SystemEventAndReadyStateIterator& rhs) {
        return rhs.mWrappedIt != mWrappedIt;
    }
    bool operator==(const SystemEventAndReadyStateIterator& rhs) {
        return rhs.mWrappedIt == mWrappedIt;
    }
    difference_type operator-(const SystemEventAndReadyStateIterator& rhs) {
        return mWrappedIt - rhs.mWrappedIt;
    }

    SystemEventAndReadyStateIterator operator+(difference_type rhs) {
        return SystemEventAndReadyStateIterator{mWrappedIt + rhs};
    }

    SystemEventAndReadyStateIterator& operator++() {
        ++mWrappedIt;
        return *this;
    }

    value_type operator*() {
        return {
            std::get<Ref<SystemEvent>>(mWrappedIt->event->GetCompletionData())
                ->GetOrCreateSystemEventReceiver(),
            &mWrappedIt->ready,
        };
    }

  private:
    WrappedIter mWrappedIt;
};

// Wait/poll the queue for futures in range [begin, end). `waitSerial` should be
// the serial after which at least one future should be complete. All futures must
// have completion data of type QueueAndSerial.
// Returns true if at least one future is ready. If no futures are ready or the wait
// timed out, returns false.
bool WaitQueueSerialsImpl(DeviceBase* device,
                          QueueBase* queue,
                          ExecutionSerial waitSerial,
                          std::vector<TrackedFutureWaitInfo>::iterator begin,
                          std::vector<TrackedFutureWaitInfo>::iterator end,
                          Nanoseconds timeout) {
    bool success = false;
    if (device->ConsumedError([&]() -> MaybeError {
            if (waitSerial > queue->GetLastSubmittedCommandSerial()) {
                // Serial has not been submitted yet. Submit it now.
                // TODO(dawn:1413): This doesn't need to be a full tick. It just needs to
                // flush work up to `waitSerial`. This should be done after the
                // ExecutionQueue / ExecutionContext refactor.
                auto guard = device->GetScopedLock();
                queue->ForceEventualFlushOfCommands();
                DAWN_TRY(device->Tick());
            }
            // Check the completed serial.
            ExecutionSerial completedSerial = queue->GetCompletedCommandSerial();
            if (completedSerial < waitSerial) {
                if (timeout > Nanoseconds(0)) {
                    // Wait on the serial if it hasn't passed yet.
                    DAWN_TRY_ASSIGN(success, queue->WaitForQueueSerial(waitSerial, timeout));
                }
                // Update completed serials.
                DAWN_TRY(queue->CheckPassedSerials());
                completedSerial = queue->GetCompletedCommandSerial();
            }
            // Poll futures for completion.
            for (auto it = begin; it != end; ++it) {
                ExecutionSerial serial =
                    std::get<QueueAndSerial>(it->event->GetCompletionData()).completionSerial;
                if (serial <= completedSerial) {
                    success = true;
                    it->ready = true;
                }
            }
            return {};
        }())) {
        // There was an error. Pending submit may have failed or waiting for fences
        // may have lost the device. The device is lost inside ConsumedError.
        // Mark all futures as ready.
        for (auto it = begin; it != end; ++it) {
            it->ready = true;
        }
        success = true;
    }
    return success;
}

// We can replace the std::vector& when std::span is available via C++20.
wgpu::WaitStatus WaitImpl(std::vector<TrackedFutureWaitInfo>& futures, Nanoseconds timeout) {
    auto begin = futures.begin();
    const auto end = futures.end();
    bool anySuccess = false;
    // The following loop will partition [begin, end) based on the type of wait is required.
    // After each partition, it will wait/poll on the first partition, then advance `begin`
    // to the start of the next partition. Note that for timeout > 0 and unsupported mixed
    // sources, we validate that there is a single partition. If there is only one, then the
    // loop runs only once and the timeout does not stack.
    while (begin != end) {
        const auto& first = begin->event->GetCompletionData();

        DeviceBase* waitDevice;
        ExecutionSerial lowestWaitSerial;
        if (std::holds_alternative<Ref<SystemEvent>>(first)) {
            waitDevice = nullptr;
        } else {
            const auto& queueAndSerial = std::get<QueueAndSerial>(first);
            waitDevice = queueAndSerial.queue->GetDevice();
            lowestWaitSerial = queueAndSerial.completionSerial;
        }
        // Partition the remaining futures based on whether they match the same completion
        // data type as the first. Also keep track of the lowest wait serial.
        const auto mid =
            std::partition(std::next(begin), end, [&](const TrackedFutureWaitInfo& info) {
                const auto& completionData = info.event->GetCompletionData();
                if (std::holds_alternative<Ref<SystemEvent>>(completionData)) {
                    return waitDevice == nullptr;
                } else {
                    const auto& queueAndSerial = std::get<QueueAndSerial>(completionData);
                    if (waitDevice == queueAndSerial.queue->GetDevice()) {
                        lowestWaitSerial =
                            std::min(lowestWaitSerial, queueAndSerial.completionSerial);
                        return true;
                    } else {
                        return false;
                    }
                }
            });

        // There's a mix of wait sources if partition yielded an iterator that is not at the end.
        if (mid != end) {
            if (timeout > Nanoseconds(0)) {
                // Multi-source wait is unsupported.
                // TODO(dawn:2062): Implement support for this when the device supports it.
                // It should eventually gather the lowest serial from the queue(s), transform them
                // into completion events, and wait on all of the events. Then for any queues that
                // saw a completion, poll all futures related to that queue for completion.
                return wgpu::WaitStatus::UnsupportedMixedSources;
            }
        }

        bool success;
        if (waitDevice) {
            success = WaitQueueSerialsImpl(waitDevice, std::get<QueueAndSerial>(first).queue.Get(),
                                           lowestWaitSerial, begin, mid, timeout);
        } else {
            if (timeout > Nanoseconds(0)) {
                success = WaitAnySystemEvent(SystemEventAndReadyStateIterator{begin},
                                             SystemEventAndReadyStateIterator{mid}, timeout);
            } else {
                // Poll the completion events.
                success = false;
                for (auto it = begin; it != end; ++it) {
                    if (std::get<Ref<SystemEvent>>(it->event->GetCompletionData())->IsSignaled()) {
                        it->ready = true;
                        success = true;
                    }
                }
            }
        }
        anySuccess |= success;

        // Advance the iterator to the next partition.
        begin = mid;
    }
    if (!anySuccess) {
        return wgpu::WaitStatus::TimedOut;
    }
    return wgpu::WaitStatus::Success;
}

// Reorder callbacks to enforce callback ordering required by the spec.
// Returns an iterator just past the last ready callback.
auto PrepareReadyCallbacks(std::vector<TrackedFutureWaitInfo>& futures) {
    // Partition the futures so the following sort looks at fewer elements.
    auto endOfReady =
        std::partition(futures.begin(), futures.end(),
                       [](const TrackedFutureWaitInfo& future) { return future.ready; });

    // Enforce the following rules from https://gpuweb.github.io/gpuweb/#promise-ordering:
    // 1. For some GPUQueue q, if p1 = q.onSubmittedWorkDone() is called before
    //    p2 = q.onSubmittedWorkDone(), then p1 must settle before p2.
    // 2. For some GPUQueue q and GPUBuffer b on the same GPUDevice,
    //    if p1 = b.mapAsync() is called before p2 = q.onSubmittedWorkDone(),
    //    then p1 must settle before p2.
    //
    // To satisfy the rules, we need only put lower future ids before higher future
    // ids. Lower future ids were created first.
    std::sort(futures.begin(), endOfReady,
              [](const TrackedFutureWaitInfo& a, const TrackedFutureWaitInfo& b) {
                  return a.futureID < b.futureID;
              });

    return endOfReady;
}

}  // namespace

// EventManager

EventManager::EventManager() {
    mEvents.emplace();  // Construct the non-movable inner struct.
}

EventManager::~EventManager() {
    DAWN_ASSERT(!mEvents.has_value());
}

MaybeError EventManager::Initialize(const UnpackedPtr<InstanceDescriptor>& descriptor) {
    if (descriptor) {
        if (descriptor->features.timedWaitAnyMaxCount > kTimedWaitAnyMaxCountDefault) {
            // We don't yet support a higher timedWaitAnyMaxCount because it would be complicated
            // to implement on Windows, and it isn't that useful to implement only on non-Windows.
            return DAWN_VALIDATION_ERROR("Requested timedWaitAnyMaxCount is not supported");
        }
        mTimedWaitAnyEnable = descriptor->features.timedWaitAnyEnable;
        mTimedWaitAnyMaxCount =
            std::max(kTimedWaitAnyMaxCountDefault, descriptor->features.timedWaitAnyMaxCount);
    }

    return {};
}

void EventManager::ShutDown() {
    mEvents.reset();
}

FutureID EventManager::TrackEvent(wgpu::CallbackMode mode, Ref<TrackedEvent>&& future) {
    FutureID futureID = mNextFutureID++;
    if (!mEvents.has_value()) {
        return futureID;
    }

    mEvents->Use([&](auto events) { events->emplace(futureID, std::move(future)); });
    return futureID;
}

void EventManager::ProcessPollEvents() {
    DAWN_ASSERT(mEvents.has_value());

    std::vector<TrackedFutureWaitInfo> futures;
    mEvents->Use([&](auto events) {
        // Iterate all events and record poll events and spontaneous events since they are both
        // allowed to be completed in the ProcessPoll call. Note that spontaneous events are allowed
        // to trigger anywhere which is why we include them in the call.
        futures.reserve(events->size());
        for (auto& [futureID, event] : *events) {
            if (event->mCallbackMode != wgpu::CallbackMode::WaitAnyOnly) {
                futures.push_back(
                    TrackedFutureWaitInfo{futureID, TrackedEvent::WaitRef{event.Get()}, 0, false});
            }
        }
    });

    {
        // There cannot be two competing ProcessEvent calls, so we use a lock to prevent it.
        std::lock_guard<std::mutex> lock(mProcessEventLock);
        wgpu::WaitStatus waitStatus = WaitImpl(futures, Nanoseconds(0));
        if (waitStatus == wgpu::WaitStatus::TimedOut) {
            return;
        }
        DAWN_ASSERT(waitStatus == wgpu::WaitStatus::Success);
    }

    // Enforce callback ordering.
    auto readyEnd = PrepareReadyCallbacks(futures);

    // For all the futures we are about to complete, first ensure they're untracked. It's OK if
    // something actually isn't tracked anymore (because it completed elsewhere while waiting.)
    mEvents->Use([&](auto events) {
        for (auto it = futures.begin(); it != readyEnd; ++it) {
            events->erase(it->futureID);
        }
    });

    // Finally, call callbacks.
    for (auto it = futures.begin(); it != readyEnd; ++it) {
        it->event->EnsureComplete(EventCompletionType::Ready);
    }
}

wgpu::WaitStatus EventManager::WaitAny(size_t count, FutureWaitInfo* infos, Nanoseconds timeout) {
    DAWN_ASSERT(mEvents.has_value());

    // Validate for feature support.
    if (timeout > Nanoseconds(0)) {
        if (!mTimedWaitAnyEnable) {
            return wgpu::WaitStatus::UnsupportedTimeout;
        }
        if (count > mTimedWaitAnyMaxCount) {
            return wgpu::WaitStatus::UnsupportedCount;
        }
        // UnsupportedMixedSources is validated later, in WaitImpl.
    }

    if (count == 0) {
        return wgpu::WaitStatus::Success;
    }

    // Look up all of the futures and build a list of `TrackedFutureWaitInfo`s.
    std::vector<TrackedFutureWaitInfo> futures;
    futures.reserve(count);
    bool anyCompleted = false;
    mEvents->Use([&](auto events) {
        FutureID firstInvalidFutureID = mNextFutureID;
        for (size_t i = 0; i < count; ++i) {
            FutureID futureID = infos[i].future.id;

            // Check for cases that are undefined behavior in the API contract.
            DAWN_ASSERT(futureID != 0);
            DAWN_ASSERT(futureID < firstInvalidFutureID);
            // TakeWaitRef below will catch if the future is waited twice at the
            // same time (unless it's already completed).

            // Try to find the event.
            auto it = events->find(futureID);
            if (it == events->end()) {
                infos[i].completed = true;
                anyCompleted = true;
            } else {
                // TakeWaitRef below will catch if the future is waited twice at the same time
                // (unless it's already completed).
                infos[i].completed = false;
                TrackedEvent* event = it->second.Get();
                futures.push_back(
                    TrackedFutureWaitInfo{futureID, TrackedEvent::WaitRef{event}, i, false});
            }
        }
    });
    // If any completed, return immediately.
    if (anyCompleted) {
        return wgpu::WaitStatus::Success;
    }
    // Otherwise, we should have successfully looked up all of them.
    DAWN_ASSERT(futures.size() == count);

    wgpu::WaitStatus waitStatus = WaitImpl(futures, timeout);
    if (waitStatus != wgpu::WaitStatus::Success) {
        return waitStatus;
    }

    // Enforce callback ordering
    auto readyEnd = PrepareReadyCallbacks(futures);

    // For any futures that we're about to complete, first ensure they're untracked. It's OK if
    // something actually isn't tracked anymore (because it completed elsewhere while waiting.)
    mEvents->Use([&](auto events) {
        for (auto it = futures.begin(); it != readyEnd; ++it) {
            events->erase(it->futureID);
        }
    });

    // Finally, call callbacks and update return values.
    for (auto it = futures.begin(); it != readyEnd; ++it) {
        // Set completed before calling the callback.
        infos[it->indexInInfos].completed = true;
        it->event->EnsureComplete(EventCompletionType::Ready);
    }

    return wgpu::WaitStatus::Success;
}

// EventManager::TrackedEvent

EventManager::TrackedEvent::TrackedEvent(wgpu::CallbackMode callbackMode,
                                         Ref<SystemEvent> completionEvent)
    : mCallbackMode(callbackMode), mCompletionData(std::move(completionEvent)) {}

EventManager::TrackedEvent::TrackedEvent(wgpu::CallbackMode callbackMode,
                                         QueueBase* queue,
                                         ExecutionSerial completionSerial)
    : mCallbackMode(callbackMode), mCompletionData(QueueAndSerial{queue, completionSerial}) {}

EventManager::TrackedEvent::TrackedEvent(wgpu::CallbackMode callbackMode, Completed tag)
    : TrackedEvent(callbackMode, [] {
          // Make a system event that is already signaled.
          // Note that this won't make a real OS event because the OS
          // event is lazily created when waited on.
          return SystemEvent::CreateSignaled();
      }()) {}

EventManager::TrackedEvent::~TrackedEvent() {
    DAWN_ASSERT(mCompleted);
}

const EventManager::TrackedEvent::CompletionData& EventManager::TrackedEvent::GetCompletionData()
    const {
    return mCompletionData;
}

void EventManager::TrackedEvent::EnsureComplete(EventCompletionType completionType) {
    bool alreadyComplete = mCompleted.exchange(true);
    if (!alreadyComplete) {
        Complete(completionType);
    }
}

void EventManager::TrackedEvent::CompleteIfSpontaneous() {
    if (mCallbackMode == wgpu::CallbackMode::AllowSpontaneous) {
        bool alreadyComplete = mCompleted.exchange(true);
        // If it was already complete, but there was an error, we have no place
        // to report it, so DAWN_ASSERT. This shouldn't happen.
        DAWN_ASSERT(!alreadyComplete);
        Complete(EventCompletionType::Ready);
    }
}

// EventManager::TrackedEvent::WaitRef

EventManager::TrackedEvent::WaitRef::WaitRef(TrackedEvent* event) : mRef(event) {
#if DAWN_ENABLE_ASSERTS
    bool wasAlreadyWaited = mRef->mCurrentlyBeingWaited.exchange(true);
    DAWN_ASSERT(!wasAlreadyWaited);
#endif
}

EventManager::TrackedEvent::WaitRef::~WaitRef() {
#if DAWN_ENABLE_ASSERTS
    if (mRef.Get() != nullptr) {
        bool wasAlreadyWaited = mRef->mCurrentlyBeingWaited.exchange(false);
        DAWN_ASSERT(wasAlreadyWaited);
    }
#endif
}

EventManager::TrackedEvent* EventManager::TrackedEvent::WaitRef::operator->() {
    return mRef.Get();
}

const EventManager::TrackedEvent* EventManager::TrackedEvent::WaitRef::operator->() const {
    return mRef.Get();
}

}  // namespace dawn::native
