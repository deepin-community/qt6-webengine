// Copyright 2021-2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sharing/common/nearby_share_prefs.h"

#include <string>
#include <vector>

#include "absl/time/clock.h"
#include "sharing/internal/api/preference_manager.h"
#include "sharing/proto/enums.pb.h"

namespace nearby {
namespace sharing {
namespace prefs {
namespace {
using ::nearby::sharing::api::PreferenceManager;

using DataUsage = ::nearby::sharing::proto::DataUsage;
using FastInitiationNotificationState =
    ::nearby::sharing::proto::FastInitiationNotificationState;
}  // namespace

const char kNearbySharingAllowedContactsName[] =
    "nearby_sharing.allowed_contacts";
const char kNearbySharingBackgroundVisibilityName[] =
    "nearby_sharing.background_visibility";
const char kNearbySharingBackgroundTemporarilyVisibleName[] =
    "nearby_sharing.background_temporarily_visible";
const char kNearbySharingBackgroundFallbackVisibilityName[] =
    "nearby_sharing.background_fallback_visibility";
const char kNearbySharingBackgroundVisibilityExpirationSeconds[] =
    "nearby_sharing.background_visibility_expiration_seconds";
const char kNearbySharingContactUploadHashName[] =
    "nearby_sharing.contact_upload_hash";
const char kNearbySharingCustomSavePath[] = "nearby_sharing.custom_save_path";
const char kNearbySharingDataUsageName[] = "nearby_sharing.data_usage";
const char kNearbySharingDeviceIdName[] = "nearby_sharing.device_id";
const char kNearbySharingDeviceNameName[] = "nearby_sharing.device_name";
const char kNearbySharingEnabledName[] = "nearby_sharing.enabled";
const char kNearbySharingFastInitiationNotificationStateName[] =
    "nearby_sharing.fast_initiation_notification_state";
const char kNearbySharingOnboardingCompleteName[] =
    "nearby_sharing.onboarding_complete";
const char kNearbySharingFullNameName[] = "nearby_sharing.full_name";
const char kNearbySharingIconUrlName[] = "nearby_sharing.icon_url";
const char kNearbySharingIconTokenName[] = "nearby_sharing.icon_token";
const char kNearbySharingOnboardingDismissedTimeName[] =
    "nearby_sharing.onboarding_dismissed_time";
const char kNearbySharingPublicCertificateExpirationDictName[] =
    "nearbyshare.public_certificate_expiration_dict";
const char kNearbySharingPrivateCertificateListName[] =
    "nearbyshare.private_certificate_list";
const char kNearbySharingSchedulerContactDownloadAndUploadName[] =
    "nearby_sharing.scheduler.contact_download_and_upload";
const char kNearbySharingSchedulerDownloadDeviceDataName[] =
    "nearby_sharing.scheduler.download_device_data";
const char kNearbySharingSchedulerDownloadPublicCertificatesName[] =
    "nearby_sharing.scheduler.download_public_certificates";
const char kNearbySharingSchedulerPeriodicContactUploadName[] =
    "nearby_sharing.scheduler.periodic_contact_upload";
const char kNearbySharingSchedulerPrivateCertificateExpirationName[] =
    "nearby_sharing.scheduler.private_certificate_expiration";
const char kNearbySharingSchedulerPublicCertificateExpirationName[] =
    "nearby_sharing.scheduler.public_certificate_expiration";
const char kNearbySharingSchedulerUploadDeviceNameName[] =
    "nearby_sharing.scheduler.upload_device_name";
const char kNearbySharingSchedulerUploadLocalDeviceCertificatesName[] =
    "nearby_sharing.scheduler.upload_local_device_certificates";
const char kNearbySharingUsersName[] = "nearby_sharing.users";
const char kNearbySharingIsReceivingName[] = "nearby_sharing.is_receiving";
const char kNearbySharingIsAnalyticsEnabledName[] =
    "nearby_sharing.is_analytics_enabled";
const char kNearbySharingIsAllContactsEnabledName[] =
    "nearby_sharing.is_all_contacts_enabled";
const char kNearbySharingAutoAppStartEnabledName[] =
    "nearby_sharing.auto_app_start_enabled";

void RegisterNearbySharingPrefs(PreferenceManager& preference_manager,
                                bool skip_persistent_ones) {
  // These prefs are not synced across devices on purpose.

  if (!skip_persistent_ones) {
    // During logging out, we reset all settings and set these settings to new
    // values. To avoid setting them twice, we skip them here if
    // skip_persistent_ones is set to true.
    preference_manager.SetBoolean(kNearbySharingEnabledName, false);
    preference_manager.SetString(kNearbySharingCustomSavePath, std::string());
    preference_manager.SetBoolean(kNearbySharingOnboardingCompleteName, false);
    preference_manager.SetInteger(kNearbySharingBackgroundVisibilityName,
                                  static_cast<int>(kDefaultVisibility));
    preference_manager.SetInteger(
        kNearbySharingBackgroundFallbackVisibilityName,
        static_cast<int>(kDefaultFallbackVisibility));
    preference_manager.SetBoolean(kNearbySharingIsReceivingName, true);
  }

  preference_manager.SetInteger(
      kNearbySharingFastInitiationNotificationStateName,
      /*value=*/static_cast<int>(
          FastInitiationNotificationState::ENABLED_FAST_INIT));

  preference_manager.SetInteger(
      kNearbySharingDataUsageName,
      static_cast<int>(DataUsage::WIFI_ONLY_DATA_USAGE));

  preference_manager.SetString(kNearbySharingContactUploadHashName,
                               std::string());

  preference_manager.SetString(kNearbySharingDeviceIdName, std::string());

  preference_manager.SetString(kNearbySharingDeviceNameName, std::string());

  preference_manager.SetStringArray(kNearbySharingAllowedContactsName,
                                    std::vector<std::string>());

  preference_manager.SetString(kNearbySharingFullNameName, std::string());

  preference_manager.SetString(kNearbySharingIconUrlName, std::string());

  preference_manager.SetString(kNearbySharingIconTokenName, std::string());

  preference_manager.SetTime(kNearbySharingOnboardingDismissedTimeName,
                             absl::Now());

  preference_manager.Remove(kNearbySharingPublicCertificateExpirationDictName);
  preference_manager.Remove(kNearbySharingPrivateCertificateListName);
  preference_manager.Remove(
      kNearbySharingSchedulerContactDownloadAndUploadName);
  preference_manager.Remove(kNearbySharingSchedulerDownloadDeviceDataName);
  preference_manager.Remove(
      kNearbySharingSchedulerDownloadPublicCertificatesName);
  preference_manager.Remove(kNearbySharingSchedulerPeriodicContactUploadName);
  preference_manager.Remove(
      kNearbySharingSchedulerPrivateCertificateExpirationName);
  preference_manager.Remove(
      kNearbySharingSchedulerPublicCertificateExpirationName);
  preference_manager.Remove(kNearbySharingSchedulerUploadDeviceNameName);
  preference_manager.Remove(
      kNearbySharingSchedulerUploadLocalDeviceCertificatesName);
  preference_manager.Remove(kNearbySharingUsersName);

  preference_manager.SetBoolean(kNearbySharingIsAnalyticsEnabledName, false);
  preference_manager.SetBoolean(kNearbySharingIsAllContactsEnabledName, true);
  preference_manager.SetBoolean(kNearbySharingAutoAppStartEnabledName, true);
}

void ResetSchedulers(PreferenceManager& preference_manager) {
  preference_manager.Remove(
      kNearbySharingSchedulerContactDownloadAndUploadName);
  preference_manager.Remove(kNearbySharingSchedulerDownloadDeviceDataName);
  preference_manager.Remove(
      kNearbySharingSchedulerDownloadPublicCertificatesName);
  preference_manager.Remove(kNearbySharingSchedulerPeriodicContactUploadName);
  preference_manager.Remove(
      kNearbySharingSchedulerPrivateCertificateExpirationName);
  preference_manager.Remove(
      kNearbySharingSchedulerPublicCertificateExpirationName);
  preference_manager.Remove(kNearbySharingSchedulerUploadDeviceNameName);
  preference_manager.Remove(
      kNearbySharingSchedulerUploadLocalDeviceCertificatesName);
}

}  // namespace prefs
}  // namespace sharing
}  // namespace nearby
