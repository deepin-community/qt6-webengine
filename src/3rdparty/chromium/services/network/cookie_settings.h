// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_COOKIE_SETTINGS_H_
#define SERVICES_NETWORK_COOKIE_SETTINGS_H_

#include <set>
#include <string>
#include <vector>

#include "base/component_export.h"
#include "base/containers/flat_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/content_settings/core/common/content_settings_utils.h"
#include "components/content_settings/core/common/cookie_settings_base.h"
#include "components/content_settings/core/common/host_indexed_content_settings.h"
#include "net/base/network_delegate.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_setting_override.h"
#include "net/cookies/cookie_util.h"
#include "net/first_party_sets/first_party_set_metadata.h"
#include "services/network/public/cpp/session_cookie_delete_predicate.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

class GURL;

namespace net {
class SiteForCookies;
class CookieInclusionStatus;
}  // namespace net

namespace url {
class Origin;
}  // namespace url

namespace network {

// Handles cookie access and deletion logic for the network service.
class COMPONENT_EXPORT(NETWORK_SERVICE) CookieSettings
    : public content_settings::CookieSettingsBase {
 public:
  CookieSettings();

  CookieSettings(const CookieSettings&) = delete;
  CookieSettings& operator=(const CookieSettings&) = delete;

  ~CookieSettings() override;

  void set_block_third_party_cookies(bool block_third_party_cookies) {
    block_third_party_cookies_ = block_third_party_cookies;
  }

  bool are_third_party_cookies_blocked() const {
    return block_third_party_cookies_;
  }

  void set_secure_origin_cookies_allowed_schemes(
      const std::vector<std::string>& secure_origin_cookies_allowed_schemes) {
    secure_origin_cookies_allowed_schemes_.clear();
    secure_origin_cookies_allowed_schemes_.insert(
        secure_origin_cookies_allowed_schemes.begin(),
        secure_origin_cookies_allowed_schemes.end());
  }

  void set_matching_scheme_cookies_allowed_schemes(
      const std::vector<std::string>& matching_scheme_cookies_allowed_schemes) {
    matching_scheme_cookies_allowed_schemes_.clear();
    matching_scheme_cookies_allowed_schemes_.insert(
        matching_scheme_cookies_allowed_schemes.begin(),
        matching_scheme_cookies_allowed_schemes.end());
  }

  void set_third_party_cookies_allowed_schemes(
      const std::vector<std::string>& third_party_cookies_allowed_schemes) {
    third_party_cookies_allowed_schemes_.clear();
    third_party_cookies_allowed_schemes_.insert(
        third_party_cookies_allowed_schemes.begin(),
        third_party_cookies_allowed_schemes.end());
  }

  void set_content_settings(ContentSettingsType type,
                            const ContentSettingsForOneType& settings);

  void set_block_truncated_cookies(bool block_truncated_cookies) {
    block_truncated_cookies_ = block_truncated_cookies;
  }

  void set_mitigations_enabled_for_3pcd(bool enable) {
    mitigations_enabled_for_3pcd_ = enable;
  }

  void set_tracking_protection_enabled_for_3pcd(bool enable) {
    tracking_protection_enabled_for_3pcd_ = enable;
  }

  bool are_truncated_cookies_blocked() const {
    return block_truncated_cookies_;
  }

  // Returns a predicate that takes the domain of a cookie and a bool whether
  // the cookie is secure and returns true if the cookie should be deleted on
  // exit.
  DeleteCookiePredicate CreateDeleteCookieOnExitPredicate() const;

  // content_settings::CookieSettingsBase:
  bool ShouldIgnoreSameSiteRestrictions(
      const GURL& url,
      const net::SiteForCookies& site_for_cookies) const override;

  // Returns kStateDisallowed iff the given |url| has to be requested over
  // connection that is not tracked by the server. Usually is kStateAllowed,
  // unless user privacy settings block cookies from being get or set.
  // It may be set to kPartitionedStateAllowedOnly if the request allows
  // partitioned state to be sent over the connection, but unpartitioned
  // state should be blocked.
  net::NetworkDelegate::PrivacySetting IsPrivacyModeEnabled(
      const GURL& url,
      const net::SiteForCookies& site_for_cookies,
      const absl::optional<url::Origin>& top_frame_origin,
      net::CookieSettingOverrides overrides) const;

  // Returns true and maybe update `cookie_inclusion_status` to include reason
  // to warn about the given cookie if it is accessible according to user
  // cookie-blocking settings.
  bool IsCookieAccessible(
      const net::CanonicalCookie& cookie,
      const GURL& url,
      const net::SiteForCookies& site_for_cookies,
      const absl::optional<url::Origin>& top_frame_origin,
      const net::FirstPartySetMetadata& first_party_set_metadata,
      net::CookieSettingOverrides overrides,
      net::CookieInclusionStatus* cookie_inclusion_status) const;

  // Annotates `maybe_included_cookies` and `excluded_cookies` with
  // ExclusionReasons if needed, per user's cookie blocking settings, and
  // ensures that all excluded cookies from `maybe_included_cookies` are moved
  // to `excluded_cookies`.  Returns false if the CookieSettings blocks access
  // to all cookies; true otherwise. Does not change the relative ordering of
  // the cookies in `maybe_included_cookies`, since this order is important when
  // building the cookie line.
  bool AnnotateAndMoveUserBlockedCookies(
      const GURL& url,
      const net::SiteForCookies& site_for_cookies,
      const url::Origin* top_frame_origin,
      const net::FirstPartySetMetadata& first_party_set_metadata,
      net::CookieSettingOverrides overrides,
      net::CookieAccessResultList& maybe_included_cookies,
      net::CookieAccessResultList& excluded_cookies) const;

  // Check content settings to decide whether to allow PST operations from a
  // given top level site. PST for specific origins can be disabled through
  // content settings.
  bool ArePrivateStateTokensAllowed(const GURL& primary_url) const {
    ContentSetting setting =
        GetContentSetting(primary_url, primary_url,
                          ContentSettingsType::COOKIES, /*info=*/nullptr);
    return (setting == CONTENT_SETTING_ALLOW);
  }

 private:
  // content_settings::CookieSettingsBase:
  bool ShouldAlwaysAllowCookies(const GURL& url,
                                const GURL& first_party_url) const override;
  ContentSetting GetContentSetting(
      const GURL& primary_url,
      const GURL& secondary_url,
      ContentSettingsType content_type,
      content_settings::SettingInfo* info) const override;
  bool IsThirdPartyCookiesAllowedScheme(
      const std::string& scheme) const override;
  bool ShouldBlockThirdPartyCookies() const override;
  bool MitigationsEnabledFor3pcd() const override;
  bool IsStorageAccessApiEnabled() const override;

  bool IsThirdPartyPhaseoutEnabled() const;

  const ContentSettingsForOneType& GetContentSettings(
      ContentSettingsType type) const;

  // Returns a host-indexed map of ContentSettingPatternSources associated with
  // the input `type`.
  const content_settings::HostIndexedContentSettings&
  GetHostIndexedContentSettings(ContentSettingsType type) const;

  // An enum that represents the scope of cookies to which the user's
  // third-party-cookie-blocking setting applies, in a given context.
  using ThirdPartyBlockingScope = CookieSettingsBase::ThirdPartyBlockingScope;

  // Returns whether the given cookie should be allowed to be sent, according
  // to the user's settings. Assumes that the `cookie.access_result` has been
  // correctly filled in by the cookie store. Note that the cookie may be
  // "excluded" for other reasons, even if this method returns true.
  static bool IsCookieAllowed(const net::CanonicalCookie& cookie,
                              const CookieSettingWithMetadata& setting);

  // Computes the PrivacySetting that should be used in this context.
  static net::NetworkDelegate::PrivacySetting PrivacySetting(
      const CookieSettingWithMetadata& setting);

  // Returns the cookie setting for the given request, along with metadata
  // associated with the lookup. Namely, whether the setting is due to
  // third-party cookie blocking settings or not.
  CookieSettingWithMetadata GetCookieSettingWithMetadata(
      const GURL& url,
      const net::SiteForCookies& site_for_cookies,
      const url::Origin* top_frame_origin,
      net::CookieSettingOverrides overrides) const;

  // Returns true if at least one content settings is session only.
  bool HasSessionOnlyOrigins() const;

  // Returns true if user blocks 3PC or 3PCD is on.
  bool block_third_party_cookies_ =
      net::cookie_util::IsForceThirdPartyCookieBlockingEnabled();
  bool block_truncated_cookies_ = true;
  bool mitigations_enabled_for_3pcd_ = false;
  // This bool makes sure the correct cookie exclusion reasons are used.
  bool tracking_protection_enabled_for_3pcd_ = false;
  std::set<std::string> secure_origin_cookies_allowed_schemes_;
  std::set<std::string> matching_scheme_cookies_allowed_schemes_;
  std::set<std::string> third_party_cookies_allowed_schemes_;

  base::flat_map<ContentSettingsType, ContentSettingsForOneType>
      content_settings_;

  base::flat_map<ContentSettingsType,
                 std::unique_ptr<content_settings::HostIndexedContentSettings>>
      host_indexed_content_settings_;
};

}  // namespace network

#endif  // SERVICES_NETWORK_COOKIE_SETTINGS_H_
