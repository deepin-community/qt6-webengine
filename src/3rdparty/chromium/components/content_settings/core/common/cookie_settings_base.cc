// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/content_settings/core/common/cookie_settings_base.h"

#include <functional>
#include <optional>
#include <string>

#include "base/check.h"
#include "base/feature_list.h"
#include "base/metrics/histogram_macros.h"
#include "base/notreached.h"
#include "base/types/optional_util.h"
#include "build/build_config.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/content_settings/core/common/features.h"
#include "net/base/features.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/cookies/cookie_constants.h"
#include "net/cookies/cookie_setting_override.h"
#include "net/cookies/cookie_util.h"
#include "net/cookies/site_for_cookies.h"
#include "net/cookies/static_cookie_policy.h"
#include "third_party/abseil-cpp/absl/types/variant.h"
#include "url/gurl.h"

namespace content_settings {

namespace {

using net::cookie_util::StorageAccessResult;
using ThirdPartyCookieAllowMechanism =
    CookieSettingsBase::ThirdPartyCookieAllowMechanism;

bool IsAllowedByCORS(const net::CookieSettingOverrides& overrides,
                     const GURL& request_url,
                     const GURL& first_party_url) {
  return overrides.Has(
             net::CookieSettingOverride::kCrossSiteCredentialedWithCORS) &&
         base::FeatureList::IsEnabled(
             net::features::kThirdPartyCookieTopLevelSiteCorsException) &&
         net::SchemefulSite(request_url) == net::SchemefulSite(first_party_url);
}

constexpr StorageAccessResult GetStorageAccessResult(
    ThirdPartyCookieAllowMechanism mechanism) {
  switch (mechanism) {
    case ThirdPartyCookieAllowMechanism::kNone:
      return StorageAccessResult::ACCESS_BLOCKED;
    case ThirdPartyCookieAllowMechanism::kAllowByExplicitSetting:
    case ThirdPartyCookieAllowMechanism::kAllowByGlobalSetting:
      return StorageAccessResult::ACCESS_ALLOWED;
    case ThirdPartyCookieAllowMechanism::kAllowBy3PCDMetadata:
      return StorageAccessResult::ACCESS_ALLOWED_3PCD_METADATA_GRANT;
    case ThirdPartyCookieAllowMechanism::kAllowBy3PCD:
      return StorageAccessResult::ACCESS_ALLOWED_3PCD_TRIAL;
    case ThirdPartyCookieAllowMechanism::kAllowByTopLevel3PCD:
      return StorageAccessResult::ACCESS_ALLOWED_TOP_LEVEL_3PCD_TRIAL;
    case ThirdPartyCookieAllowMechanism::kAllowBy3PCDHeuristics:
      return StorageAccessResult::ACCESS_ALLOWED_3PCD_HEURISTICS_GRANT;
    case ThirdPartyCookieAllowMechanism::kAllowByStorageAccess:
      return StorageAccessResult::ACCESS_ALLOWED_STORAGE_ACCESS_GRANT;
    case ThirdPartyCookieAllowMechanism::kAllowByTopLevelStorageAccess:
      return StorageAccessResult::ACCESS_ALLOWED_TOP_LEVEL_STORAGE_ACCESS_GRANT;
    case ThirdPartyCookieAllowMechanism::kAllowByCORSException:
      return StorageAccessResult::ACCESS_ALLOWED_CORS_EXCEPTION;
  }
}

constexpr std::optional<SettingSource> GetSettingSource(
    ThirdPartyCookieAllowMechanism mechanism) {
  switch (mechanism) {
    // 3PCD-related mechanisms all map to `SETTING_SOURCE_TPCD_GRANT`.
    case ThirdPartyCookieAllowMechanism::kAllowBy3PCDMetadata:
    case ThirdPartyCookieAllowMechanism::kAllowBy3PCD:
    case ThirdPartyCookieAllowMechanism::kAllowBy3PCDHeuristics:
    case ThirdPartyCookieAllowMechanism::kAllowByTopLevel3PCD:
      return SettingSource::SETTING_SOURCE_TPCD_GRANT;
    // Other mechanisms do not map to a `SettingSource`.
    case ThirdPartyCookieAllowMechanism::kNone:
    case ThirdPartyCookieAllowMechanism::kAllowByExplicitSetting:
    case ThirdPartyCookieAllowMechanism::kAllowByGlobalSetting:
    case ThirdPartyCookieAllowMechanism::kAllowByStorageAccess:
    case ThirdPartyCookieAllowMechanism::kAllowByTopLevelStorageAccess:
    case ThirdPartyCookieAllowMechanism::kAllowByCORSException:
      return std::nullopt;
  }
}

}  // namespace

bool CookieSettingsBase::storage_access_api_grants_unpartitioned_storage_ =
    false;

void CookieSettingsBase::
    SetStorageAccessAPIGrantsUnpartitionedStorageForTesting(bool grants) {
  storage_access_api_grants_unpartitioned_storage_ = grants;
}

CookieSettingsBase::CookieSettingsBase()
    : is_storage_partitioned_(base::FeatureList::IsEnabled(
          net::features::kThirdPartyStoragePartitioning)) {}

CookieSettingsBase::CookieSettingWithMetadata::CookieSettingWithMetadata(
    ContentSetting cookie_setting,
    std::optional<ThirdPartyBlockingScope> third_party_blocking_scope,
    bool is_explicit_setting,
    ThirdPartyCookieAllowMechanism third_party_cookie_allow_mechanism)
    : cookie_setting_(cookie_setting),
      third_party_blocking_scope_(third_party_blocking_scope),
      is_explicit_setting_(is_explicit_setting),
      third_party_cookie_allow_mechanism_(third_party_cookie_allow_mechanism) {
  DCHECK(!third_party_blocking_scope_.has_value() ||
         !IsAllowed(cookie_setting_));
}

bool CookieSettingsBase::CookieSettingWithMetadata::
    BlockedByThirdPartyCookieBlocking() const {
  return !IsAllowed(cookie_setting_) && third_party_blocking_scope_.has_value();
}

bool CookieSettingsBase::CookieSettingWithMetadata::IsPartitionedStateAllowed()
    const {
  return IsAllowed(cookie_setting_) ||
         third_party_blocking_scope_ ==
             ThirdPartyBlockingScope::kUnpartitionedOnly;
}

// static
const CookieSettingsBase::CookieSettingsTypeSet&
CookieSettingsBase::GetContentSettingsTypes() {
  static constexpr auto kInstance =
      base::MakeFixedFlatSet<ContentSettingsType>({
          ContentSettingsType::COOKIES,
          ContentSettingsType::LEGACY_COOKIE_ACCESS,
          ContentSettingsType::STORAGE_ACCESS,
          ContentSettingsType::TOP_LEVEL_STORAGE_ACCESS,
          ContentSettingsType::TPCD_HEURISTICS_GRANTS,
          ContentSettingsType::TPCD_TRIAL,
          ContentSettingsType::TOP_LEVEL_TPCD_TRIAL,
      });
  return kInstance;
}

// static
bool CookieSettingsBase::IsThirdPartyRequest(
    const GURL& url,
    const net::SiteForCookies& site_for_cookies) {
  net::StaticCookiePolicy policy(
      net::StaticCookiePolicy::BLOCK_ALL_THIRD_PARTY_COOKIES);
  return policy.CanAccessCookies(url, site_for_cookies) != net::OK;
}

// static
GURL CookieSettingsBase::GetFirstPartyURL(
    const net::SiteForCookies& site_for_cookies,
    const url::Origin* top_frame_origin) {
  return top_frame_origin != nullptr ? top_frame_origin->GetURL()
                                     : site_for_cookies.RepresentativeUrl();
}

bool CookieSettingsBase::ShouldDeleteCookieOnExit(
    const ContentSettingsForOneType& cookie_settings,
    const std::string& domain,
    net::CookieSourceScheme scheme) const {
  // Cookies with an unknown (kUnset) scheme will be treated as having a not
  // secure scheme.
  GURL origin = net::cookie_util::CookieOriginToURL(
      domain, scheme == net::CookieSourceScheme::kSecure);
  // Pass GURL() as first_party_url since we don't know the context and
  // don't want to match against (*, exception) pattern.
  // No overrides are given since existing ones only pertain to 3P checks.
  ContentSetting setting =
      GetCookieSettingInternal(origin, GURL(),
                               /*is_third_party_request=*/false,
                               net::CookieSettingOverrides(), nullptr)
          .cookie_setting();
  DCHECK(IsValidSetting(setting));
  if (setting == CONTENT_SETTING_ALLOW) {
    return false;
  }
  // Non-secure cookies are readable by secure sites. We need to check for
  // the secure pattern if non-secure is not allowed. The section below is
  // independent of the scheme so we can just retry from here.
  if (scheme != net::CookieSourceScheme::kSecure) {
    return ShouldDeleteCookieOnExit(cookie_settings, domain,
                                    net::CookieSourceScheme::kSecure);
  }
  // Check if there is a more precise rule that "domain matches" this cookie.
  bool matches_session_only_rule = false;
  for (const auto& entry : cookie_settings) {
    // Skip WebUI third-party cookie exceptions.
    if (entry.source == "webui_allowlist" &&
        !entry.secondary_pattern.MatchesAllHosts()) {
      continue;
    }
    // While we don't know on which top-frame-origin a cookie was set, we still
    // use exceptions that only specify a secondary pattern to handle cookies
    // that match this pattern.
    const std::string& host = entry.primary_pattern.MatchesAllHosts()
                                  ? entry.secondary_pattern.GetHost()
                                  : entry.primary_pattern.GetHost();
    if (net::cookie_util::IsDomainMatch(domain, host)) {
      if (entry.GetContentSetting() == CONTENT_SETTING_ALLOW) {
        return false;
      } else if (entry.GetContentSetting() == CONTENT_SETTING_SESSION_ONLY) {
        matches_session_only_rule = true;
      }
    }
  }
  return setting == CONTENT_SETTING_SESSION_ONLY || matches_session_only_rule;
}

ContentSetting CookieSettingsBase::GetCookieSetting(
    const GURL& url,
    const GURL& first_party_url,
    net::CookieSettingOverrides overrides,
    content_settings::SettingInfo* info) const {
  return GetCookieSettingInternal(
             url, first_party_url,
             IsThirdPartyRequest(url,
                                 net::SiteForCookies::FromUrl(first_party_url)),
             overrides, info)
      .cookie_setting();
}

CookieSettingsBase::ThirdPartyCookieAllowMechanism
CookieSettingsBase::GetThirdPartyCookieAllowMechanism(
    const GURL& url,
    const GURL& first_party_url,
    net::CookieSettingOverrides overrides,
    content_settings::SettingInfo* info) const {
  return GetCookieSettingInternal(
             url, first_party_url,
             IsThirdPartyRequest(url,
                                 net::SiteForCookies::FromUrl(first_party_url)),
             overrides, info)
      .third_party_cookie_allow_mechanism();
}

bool CookieSettingsBase::IsFullCookieAccessAllowed(
    const GURL& url,
    const net::SiteForCookies& site_for_cookies,
    const std::optional<url::Origin>& top_frame_origin,
    net::CookieSettingOverrides overrides,
    CookieSettingWithMetadata* cookie_settings) const {
  CookieSettingWithMetadata setting = GetCookieSettingInternal(
      url,
      GetFirstPartyURL(site_for_cookies, base::OptionalToPtr(top_frame_origin)),
      IsThirdPartyRequest(url, site_for_cookies), overrides, nullptr);

  if (cookie_settings) {
    *cookie_settings = setting;
  }

  return IsAllowed(setting.cookie_setting());
}

bool CookieSettingsBase::IsCookieSessionOnly(const GURL& origin) const {
  // Pass GURL() as first_party_url since we don't know the context and
  // don't want to match against (*, exception) pattern.
  // No overrides are given since existing ones only pertain to 3P checks.
  ContentSetting setting =
      GetCookieSettingInternal(origin, GURL(),
                               /*is_third_party_request=*/false,
                               net::CookieSettingOverrides(), nullptr)
          .cookie_setting();
  DCHECK(IsValidSetting(setting));
  return setting == CONTENT_SETTING_SESSION_ONLY;
}

net::CookieAccessSemantics
CookieSettingsBase::GetCookieAccessSemanticsForDomain(
    const std::string& cookie_domain) const {
  ContentSetting setting = GetSettingForLegacyCookieAccess(cookie_domain);
  DCHECK(IsValidSettingForLegacyAccess(setting));
  switch (setting) {
    case CONTENT_SETTING_ALLOW:
      return net::CookieAccessSemantics::LEGACY;
    case CONTENT_SETTING_BLOCK:
      return net::CookieAccessSemantics::NONLEGACY;
    default:
      NOTREACHED();
  }
  return net::CookieAccessSemantics::UNKNOWN;
}

bool CookieSettingsBase::ShouldConsider3pcdTrialSettings(
    net::CookieSettingOverrides overrides) const {
  return base::FeatureList::IsEnabled(net::features::kTpcdTrialSettings) &&
         MitigationsEnabledFor3pcd() &&
         !overrides.Has(net::CookieSettingOverride::kSkipTPCDTrial);
}

bool CookieSettingsBase::ShouldConsiderTopLevel3pcdTrialSettings(
    net::CookieSettingOverrides overrides) const {
  return base::FeatureList::IsEnabled(
             net::features::kTopLevelTpcdTrialSettings) &&
         MitigationsEnabledFor3pcd() &&
         !overrides.Has(net::CookieSettingOverride::kSkipTopLevelTPCDTrial);
}

bool CookieSettingsBase::ShouldConsider3pcdMetadataGrantsSettings(
    net::CookieSettingOverrides overrides) const {
  return base::FeatureList::IsEnabled(net::features::kTpcdMetadataGrants) &&
         MitigationsEnabledFor3pcd() &&
         !overrides.Has(net::CookieSettingOverride::kSkipTPCDMetadataGrant);
}

bool CookieSettingsBase::ShouldConsider3pcdHeuristicsGrantsSettings(
    net::CookieSettingOverrides overrides) const {
  return features::kTpcdReadHeuristicsGrants.Get() &&
         MitigationsEnabledFor3pcd() &&
         !overrides.Has(net::CookieSettingOverride::kSkipTPCDHeuristicsGrant);
}

bool CookieSettingsBase::ShouldConsiderStorageAccessGrants(
    net::CookieSettingOverrides overrides) const {
  return overrides.Has(net::CookieSettingOverride::kStorageAccessGrantEligible);
}

net::CookieSettingOverrides CookieSettingsBase::SettingOverridesForStorage()
    const {
  net::CookieSettingOverrides overrides;
  if (storage_access_api_grants_unpartitioned_storage_ ||
      is_storage_partitioned_) {
    overrides.Put(net::CookieSettingOverride::kStorageAccessGrantEligible);
  }
  if (is_storage_partitioned_) {
    overrides.Put(
        net::CookieSettingOverride::kTopLevelStorageAccessGrantEligible);
  }
  return overrides;
}

bool CookieSettingsBase::ShouldConsiderTopLevelStorageAccessGrants(
    net::CookieSettingOverrides overrides) const {
  return overrides.Has(
      net::CookieSettingOverride::kTopLevelStorageAccessGrantEligible);
}

absl::variant<CookieSettingsBase::AllowAllCookies,
              CookieSettingsBase::AllowPartitionedCookies,
              CookieSettingsBase::BlockAllCookies>
CookieSettingsBase::DecideAccess(const GURL& url,
                                 const GURL& first_party_url,
                                 bool is_third_party_request,
                                 net::CookieSettingOverrides overrides,
                                 const ContentSetting& setting,
                                 bool is_explicit_setting) const {
  CHECK(!url.SchemeIsWSOrWSS());

  if (!IsAllowed(setting)) {
    return BlockAllCookies{};
  }
  if (is_explicit_setting) {
    return AllowAllCookies{
        ThirdPartyCookieAllowMechanism::kAllowByExplicitSetting};
  }
  if (!is_third_party_request) {
    return AllowAllCookies{ThirdPartyCookieAllowMechanism::kNone};
  }
  if (!ShouldBlockThirdPartyCookies()) {
    return AllowAllCookies{
        ThirdPartyCookieAllowMechanism::kAllowByGlobalSetting};
  }
  if (IsThirdPartyCookiesAllowedScheme(first_party_url.scheme())) {
    return AllowAllCookies{ThirdPartyCookieAllowMechanism::kNone};
  }
  if (ShouldConsider3pcdTrialSettings(overrides) &&
      GetContentSetting(url, first_party_url, ContentSettingsType::TPCD_TRIAL,
                        /*info=*/nullptr) == CONTENT_SETTING_ALLOW) {
    return AllowAllCookies{ThirdPartyCookieAllowMechanism::kAllowBy3PCD};
  }
  if (ShouldConsiderTopLevel3pcdTrialSettings(overrides) &&
      IsAllowedByTopLevel3pcdTrialSetting(first_party_url)) {
    return AllowAllCookies{
        ThirdPartyCookieAllowMechanism::kAllowByTopLevel3PCD};
  }
  if (ShouldConsider3pcdHeuristicsGrantsSettings(overrides) &&
      GetContentSetting(url, first_party_url,
                        ContentSettingsType::TPCD_HEURISTICS_GRANTS,
                        /*info=*/nullptr) == CONTENT_SETTING_ALLOW) {
    return AllowAllCookies{
        ThirdPartyCookieAllowMechanism::kAllowBy3PCDHeuristics};
  }
  if (IsAllowedByCORS(overrides, url, first_party_url)) {
    return AllowAllCookies{
        ThirdPartyCookieAllowMechanism::kAllowByCORSException};
  }

  const bool has_storage_access_opt_in =
      ShouldConsiderStorageAccessGrants(overrides);
  const bool has_storage_access_permission_grant =
      IsAllowedByStorageAccessGrant(url, first_party_url);
  net::cookie_util::FireStorageAccessInputHistogram(
      has_storage_access_opt_in, has_storage_access_permission_grant);

  if (IsStorageAccessApiEnabled()) {
    if (ShouldConsiderTopLevelStorageAccessGrants(overrides) &&
        GetContentSetting(url, first_party_url,
                          ContentSettingsType::TOP_LEVEL_STORAGE_ACCESS,
                          /*info=*/nullptr) == CONTENT_SETTING_ALLOW) {
      return AllowAllCookies{
          ThirdPartyCookieAllowMechanism::kAllowByTopLevelStorageAccess};
    }
    if (has_storage_access_opt_in && has_storage_access_permission_grant) {
      return AllowAllCookies{
          ThirdPartyCookieAllowMechanism::kAllowByStorageAccess};
    }
  }

  // Sets the 3PCD Metadata Grants last, to prioritize grants from other
  // mitigations and access protocols.
  if (ShouldConsider3pcdMetadataGrantsSettings(overrides) &&
      IsAllowed(GetContentSetting(url, first_party_url,
                                  ContentSettingsType::TPCD_METADATA_GRANTS,
                                  /*info=*/nullptr))) {
    return AllowAllCookies{
        ThirdPartyCookieAllowMechanism::kAllowBy3PCDMetadata};
  }

  return AllowPartitionedCookies{};
}

CookieSettingsBase::CookieSettingWithMetadata
CookieSettingsBase::GetCookieSettingInternal(
    const GURL& request_url,
    const GURL& first_party_url,
    bool is_third_party_request,
    net::CookieSettingOverrides overrides,
    SettingInfo* info) const {
  SCOPED_UMA_HISTOGRAM_TIMER_MICROS(
      "ContentSettings.GetCookieSettingInternal.Duration");

  // Apply http and https exceptions to ws and wss schemes.
  std::reference_wrapper<const GURL> url = request_url;
  GURL websocket_mapped_url;
  if (url.get().SchemeIsWSOrWSS()) {
    websocket_mapped_url = net::ChangeWebSocketSchemeToHttpScheme(request_url);
    url = websocket_mapped_url;
  }

  // Auto-allow in extensions or for WebUI embedding a secure origin.
  if (ShouldAlwaysAllowCookies(url, first_party_url)) {
    if (info) {
      *info = SettingInfo{};
    }
    return CookieSettingWithMetadata{
        /*cookie_setting=*/CONTENT_SETTING_ALLOW,
        /*third_party_blocking_scope=*/std::nullopt,
        /*is_explicit_setting=*/false,
        /*third_party_cookie_allow_mechanism=*/
        ThirdPartyCookieAllowMechanism::kNone};
  }

  SettingInfo setting_info;
  const ContentSetting cookie_setting = GetContentSetting(
      url, first_party_url, ContentSettingsType::COOKIES, &setting_info);

  const bool is_explicit_setting =
      !setting_info.primary_pattern.MatchesAllHosts() ||
      !setting_info.secondary_pattern.MatchesAllHosts();

  const absl::variant<AllowAllCookies, AllowPartitionedCookies, BlockAllCookies>
      choice = DecideAccess(url, first_party_url, is_third_party_request,
                            overrides, cookie_setting, is_explicit_setting);

  if (const AllowAllCookies* allow_cookies =
          absl::get_if<AllowAllCookies>(&choice)) {
    CHECK(IsAllowed(cookie_setting));

    FireStorageAccessHistogram(
        GetStorageAccessResult(allow_cookies->mechanism));

    if (info) {
      if (std::optional<SettingSource> source =
              GetSettingSource(allow_cookies->mechanism);
          source.has_value()) {
        setting_info.source = *source;
      }
      *info = setting_info;
    }
    const CookieSettingWithMetadata out{
        cookie_setting,
        /*third_party_blocking_scope=*/std::nullopt,
        is_explicit_setting,
        /*third_party_cookie_allow_mechanism=*/allow_cookies->mechanism,
    };
    CHECK(!out.BlockedByThirdPartyCookieBlocking());
    CHECK(out.IsPartitionedStateAllowed());
    return out;
  }

  if (absl::holds_alternative<AllowPartitionedCookies>(choice)) {
    FireStorageAccessHistogram(StorageAccessResult::ACCESS_BLOCKED);

    if (info) {
      *info = setting_info;
    }
    const CookieSettingWithMetadata out{
        CONTENT_SETTING_BLOCK,
        ThirdPartyBlockingScope::kUnpartitionedOnly,
        is_explicit_setting,
        ThirdPartyCookieAllowMechanism::kNone,
    };
    CHECK(out.BlockedByThirdPartyCookieBlocking());
    CHECK(out.IsPartitionedStateAllowed());
    return out;
  }

  CHECK(absl::holds_alternative<BlockAllCookies>(choice));
  FireStorageAccessHistogram(StorageAccessResult::ACCESS_BLOCKED);

  if (info) {
    *info = setting_info;
  }
  const CookieSettingWithMetadata out{
      CONTENT_SETTING_BLOCK,
      /*third_party_blocking_scope=*/std::nullopt,
      is_explicit_setting,
      ThirdPartyCookieAllowMechanism::kNone,
  };
  CHECK(!out.BlockedByThirdPartyCookieBlocking());
  CHECK(!out.IsPartitionedStateAllowed());
  return out;
}

bool CookieSettingsBase::IsAllowedByStorageAccessGrant(
    const GURL& url,
    const GURL& first_party_url) const {
  // The Storage Access API allows access in A(B(A)) case (or similar). Do the
  // same-origin check first for performance reasons.
  const url::Origin origin = url::Origin::Create(url);
  const url::Origin first_party_origin = url::Origin::Create(first_party_url);
  if (origin.IsSameOriginWith(first_party_origin) ||
      net::SchemefulSite(origin) == net::SchemefulSite(first_party_origin)) {
    return true;
  }

  return GetContentSetting(url, first_party_url,
                           ContentSettingsType::STORAGE_ACCESS,
                           /*info=*/nullptr) == CONTENT_SETTING_ALLOW;
}

bool CookieSettingsBase::IsAllowedByTopLevel3pcdTrialSetting(
    const GURL& first_party_url) const {
  // Top-level 3pcd trial settings use
  // |WebsiteSettingsInfo::TOP_ORIGIN_ONLY_SCOPE| by default and as a result
  // only use a primary pattern (with wildcard placeholder for the secondary
  // pattern).
  return GetContentSetting(first_party_url, first_party_url,
                           ContentSettingsType::TOP_LEVEL_TPCD_TRIAL,
                           /*info=*/nullptr) == CONTENT_SETTING_ALLOW;
}

ContentSetting CookieSettingsBase::GetSettingForLegacyCookieAccess(
    const std::string& cookie_domain) const {
  // The content setting patterns are treated as domains, not URLs, so the
  // scheme is irrelevant (so we can just arbitrarily pass false).
  GURL cookie_domain_url = net::cookie_util::CookieOriginToURL(
      cookie_domain, false /* secure scheme */);

  return GetContentSetting(cookie_domain_url, GURL(),
                           ContentSettingsType::LEGACY_COOKIE_ACCESS,
                           /*info=*/nullptr);
}

// static
bool CookieSettingsBase::IsValidSetting(ContentSetting setting) {
  return (setting == CONTENT_SETTING_ALLOW ||
          setting == CONTENT_SETTING_SESSION_ONLY ||
          setting == CONTENT_SETTING_BLOCK);
}

// static
bool CookieSettingsBase::IsAllowed(ContentSetting setting) {
  DCHECK(IsValidSetting(setting));
  return (setting == CONTENT_SETTING_ALLOW ||
          setting == CONTENT_SETTING_SESSION_ONLY);
}

// static
bool CookieSettingsBase::IsValidSettingForLegacyAccess(ContentSetting setting) {
  return (setting == CONTENT_SETTING_ALLOW || setting == CONTENT_SETTING_BLOCK);
}

}  // namespace content_settings
