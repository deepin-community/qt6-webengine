// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/privacy_sandbox/privacy_sandbox_features.h"

#include "base/feature_list.h"

namespace privacy_sandbox {

#if BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kPrivacySandboxAdsNoticeCCT,
             "PrivacySandboxAdsNoticeCCT",
             base::FEATURE_DISABLED_BY_DEFAULT);

const char kPrivacySandboxAdsNoticeCCTAppIdName[] = "app-id";

const base::FeatureParam<std::string> kPrivacySandboxAdsNoticeCCTAppId{
    &kPrivacySandboxAdsNoticeCCT, kPrivacySandboxAdsNoticeCCTAppIdName, ""};
#endif  // BUILDFLAG(IS_ANDROID)

// Show the Tracking Protection onboarding flow if not already onboarded.
BASE_FEATURE(kPrivacySandboxSuppressDialogOnNonNormalBrowsers,
             "PrivacySandboxSuppressDialogOnNonNormalBrowsers",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kPrivacySandboxSettings4,
             "PrivacySandboxSettings4",
             base::FEATURE_ENABLED_BY_DEFAULT);

const char kPrivacySandboxSettings4ConsentRequiredName[] = "consent-required";
const char kPrivacySandboxSettings4NoticeRequiredName[] = "notice-required";
const char kPrivacySandboxSettings4RestrictedNoticeName[] = "restricted-notice";
const char kPrivacySandboxSettings4ForceShowConsentForTestingName[] =
    "force-show-consent-for-testing";
const char kPrivacySandboxSettings4ForceShowNoticeRowForTestingName[] =
    "force-show-notice-row-for-testing";
const char kPrivacySandboxSettings4ForceShowNoticeEeaForTestingName[] =
    "force-show-notice-eea-for-testing";
const char kPrivacySandboxSettings4ForceShowNoticeRestrictedForTestingName[] =
    "force-show-notice-restricted-for-testing";
const char kPrivacySandboxSettings4ForceRestrictedUserForTestingName[] =
    "force-restricted-user";
const char kPrivacySandboxSettings4ShowSampleDataForTestingName[] =
    "show-sample-data";

const base::FeatureParam<bool> kPrivacySandboxSettings4ConsentRequired{
    &kPrivacySandboxSettings4, kPrivacySandboxSettings4ConsentRequiredName,
    false};
const base::FeatureParam<bool> kPrivacySandboxSettings4NoticeRequired{
    &kPrivacySandboxSettings4, kPrivacySandboxSettings4NoticeRequiredName,
    false};
const base::FeatureParam<bool> kPrivacySandboxSettings4RestrictedNotice{
    &kPrivacySandboxSettings4, kPrivacySandboxSettings4RestrictedNoticeName,
    false};

const base::FeatureParam<bool>
    kPrivacySandboxSettings4ForceShowConsentForTesting{
        &kPrivacySandboxSettings4,
        kPrivacySandboxSettings4ForceShowConsentForTestingName, false};
const base::FeatureParam<bool>
    kPrivacySandboxSettings4ForceShowNoticeRowForTesting{
        &kPrivacySandboxSettings4,
        kPrivacySandboxSettings4ForceShowNoticeRowForTestingName, false};
const base::FeatureParam<bool>
    kPrivacySandboxSettings4ForceShowNoticeEeaForTesting{
        &kPrivacySandboxSettings4,
        kPrivacySandboxSettings4ForceShowNoticeEeaForTestingName, false};
const base::FeatureParam<bool>
    kPrivacySandboxSettings4ForceShowNoticeRestrictedForTesting{
        &kPrivacySandboxSettings4,
        kPrivacySandboxSettings4ForceShowNoticeRestrictedForTestingName, false};
const base::FeatureParam<bool>
    kPrivacySandboxSettings4ForceRestrictedUserForTesting{
        &kPrivacySandboxSettings4,
        kPrivacySandboxSettings4ForceRestrictedUserForTestingName, false};
const base::FeatureParam<bool> kPrivacySandboxSettings4ShowSampleDataForTesting{
    &kPrivacySandboxSettings4,
    kPrivacySandboxSettings4ShowSampleDataForTestingName, false};

const base::FeatureParam<bool>
    kPrivacySandboxSettings4SuppressDialogForExternalAppLaunches{
        &kPrivacySandboxSettings4, "suppress-dialog-for-external-app-launches",
        true};

const base::FeatureParam<bool> kPrivacySandboxSettings4CloseAllPrompts{
    &kPrivacySandboxSettings4, "close-all-prompts", true};

BASE_FEATURE(kOverridePrivacySandboxSettingsLocalTesting,
             "OverridePrivacySandboxSettingsLocalTesting",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kDisablePrivacySandboxPrompts,
             "DisablePrivacySandboxPrompts",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kPrivacySandboxFirstPartySetsUI,
             "PrivacySandboxFirstPartySetsUI",
             base::FEATURE_ENABLED_BY_DEFAULT);
const base::FeatureParam<bool> kPrivacySandboxFirstPartySetsUISampleSets{
    &kPrivacySandboxFirstPartySetsUI, "use-sample-sets", false};

BASE_FEATURE(kEnforcePrivacySandboxAttestations,
             "EnforcePrivacySandboxAttestations",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kDefaultAllowPrivacySandboxAttestations,
             "DefaultAllowPrivacySandboxAttestations",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kPrivacySandboxAttestationSentinel,
             "PrivacySandboxAttestationsSentinel",
             base::FEATURE_ENABLED_BY_DEFAULT);

const char kPrivacySandboxEnrollmentOverrides[] =
    "privacy-sandbox-enrollment-overrides";

BASE_FEATURE(kPrivacySandboxAttestationsHigherComponentRegistrationPriority,
             "PrivacySandboxAttestationsHigherComponentRegistrationPriority",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kPrivacySandboxAttestationsUserBlockingPriority,
             "PrivacySandboxAttestationsUserBlockingPriority",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kPrivacySandboxProactiveTopicsBlocking,
             "PrivacySandboxProactiveTopicsBlocking",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTrackingProtectionSettingsPageRollbackNotice,
             "TrackingProtectionSettingsPageRollbackNotice",
             base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kTrackingProtectionOnboardingSkipSecurePageCheck,
             "TrackingProtectionOnboardingSkipSecurePageCheck",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_ANDROID)

// Show the Tracking Protection rollback flow if previously onboarded.
BASE_FEATURE(kTrackingProtectionOnboardingRollback,
             "TrackingProtectionOnboardingRollback",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kAttributionDebugReportingCookieDeprecationTesting,
             "AttributionDebugReportingCookieDeprecationTesting",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kPrivateAggregationDebugReportingCookieDeprecationTesting,
             "PrivateAggregationDebugReportingCookieDeprecationTesting",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kPrivacySandboxInternalsDevUI,
             "PrivacySandboxInternalsDevUI",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kIpProtectionV1,
             "IpProtectionV1",
             base::FEATURE_DISABLED_BY_DEFAULT);

#if BUILDFLAG(IS_ANDROID)
BASE_FEATURE(kTrackingProtectionNoticeRequestTracking,
             "TrackingProtectionNoticeRequestTracking",
             base::FEATURE_DISABLED_BY_DEFAULT);
#endif  // BUILDFLAG(IS_ANDROID)

}  // namespace privacy_sandbox
