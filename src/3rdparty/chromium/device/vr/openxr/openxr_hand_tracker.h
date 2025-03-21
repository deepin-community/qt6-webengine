// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_VR_OPENXR_OPENXR_HAND_TRACKER_H_
#define DEVICE_VR_OPENXR_OPENXR_HAND_TRACKER_H_

#include <optional>

#include "base/memory/raw_ref.h"
#include "device/vr/public/mojom/openxr_interaction_profile_type.mojom-forward.h"
#include "device/vr/public/mojom/vr_service.mojom-forward.h"
#include "third_party/openxr/src/include/openxr/openxr.h"

namespace gfx {
class Transform;
}

namespace device {

class GamepadButton;
enum class GamepadMapping;
enum class OpenXrButtonType;
enum class OpenXrHandednessType;
class OpenXrExtensionHelper;

// This interface is used to encapsulate methods that `OpenXrController` would
// need to generate full controller state from a tracked hand. If a tracked hand
// lacks at least the ability to parse out when a "select" gesture is
// implemented, it should not implement this interface. Users are expected to
// call update at their desired cadence on the `OpenXrHandTracker` that the
// interface is retrieved from before querying for state from the interface.
//
// An entry should be added to the `device::mojom::OpenXrInteractionProfileType`
// enum and the `GetOpenXrInputProfilesMap` (from openxr_interaction_profiles),
// but an entry should NOT be added to `GetOpenXrControllerInteractionProfiles`
// when extending this interface.
class OpenXrHandController {
 public:
  virtual mojom::OpenXrInteractionProfileType interaction_profile() const = 0;

  virtual GamepadMapping gamepad_mapping() const = 0;

  // Gets the `base_from_grip` transform, where the `base` space is the one that
  // was passed in to "Update".
  virtual std::optional<gfx::Transform> GetBaseFromGripTransform() const = 0;

  virtual std::optional<gfx::Transform> GetGripFromPointerTransform() const = 0;

  virtual std::optional<GamepadButton> GetButton(
      OpenXrButtonType type) const = 0;
};

class OpenXrHandTracker {
 public:
  OpenXrHandTracker(const OpenXrExtensionHelper& extension_helper,
                    XrSession session,
                    OpenXrHandednessType type);
  virtual ~OpenXrHandTracker();

  XrResult Update(XrSpace base_space, XrTime predicted_display_time);

  mojom::XRHandTrackingDataPtr GetHandTrackingData() const;

  // Gets an `OpenXrHandController` for this hand tracker if it supports parsing
  // data separately from any interaction profile implementation. A hand tracker
  // should either always return null or always return non-null.
  virtual const OpenXrHandController* controller() const;

 protected:
  bool IsDataValid() const;
  virtual void AppendToLocationStruct(XrHandJointLocationsEXT& locations) {}

  // Gets the `base_from_grip` transform, where the `base` space is the one that
  // was passed in to "Update". This is calculated based on the palm position,
  // and exposed as a helper to any child classes.
  std::optional<gfx::Transform> GetBaseFromPalmTransform() const;

 private:
  XrResult InitializeHandTracking();

  const raw_ref<const OpenXrExtensionHelper> extension_helper_;
  XrSession session_;
  OpenXrHandednessType type_;
  XrHandTrackerEXT hand_tracker_{XR_NULL_HANDLE};

  XrHandJointLocationEXT joint_locations_buffer_[XR_HAND_JOINT_COUNT_EXT];
  XrHandJointLocationsEXT locations_{XR_TYPE_HAND_JOINT_LOCATIONS_EXT};
};

}  // namespace device

#endif  // DEVICE_VR_OPENXR_OPENXR_HAND_TRACKER_H_
