// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/feature_engagement/public/group_configurations.h"

#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "components/feature_engagement/public/configuration.h"
#include "components/feature_engagement/public/event_constants.h"
#include "components/feature_engagement/public/group_constants.h"

namespace feature_engagement {

absl::optional<GroupConfig> GetClientSideGroupConfig(
    const base::Feature* group) {
#if BUILDFLAG(IS_IOS)
  if (kiOSFullscreenPromosGroup.name == group->name) {
    absl::optional<GroupConfig> config = GroupConfig();
    config->valid = true;
    config->session_rate = Comparator(EQUAL, 0);
    // Only show a fullscreen promo once every two days.
    config->trigger = EventConfig("fullscreen_promos_group_trigger",
                                  Comparator(LESS_THAN, 1), 2, 1000);
    // Only show a fullscreen promo three times every seven days.
    config->event_configs.insert(EventConfig(
        "fullscreen_promos_group_trigger", Comparator(LESS_THAN, 3), 7, 1000));
    return config;
  }

  if (kiOSDefaultBrowserPromosGroup.name == group->name) {
    absl::optional<GroupConfig> config = GroupConfig();
    config->valid = true;
    config->session_rate = Comparator(EQUAL, 0);
    // Default browser promos should be at least 14 days apart.
    config->trigger = EventConfig("default_browser_promos_group_trigger",
                                  Comparator(LESS_THAN, 1), 14, 365);
    // Default Browser promos shouldn't be shown if the Post Restore Default
    // Browser Promo has been shown in the past 7 days.
    config->event_configs.insert(
        EventConfig("post_restore_default_browser_promo_trigger",
                    Comparator(EQUAL, 0), 7, 365));

    // Default Browser promos can be shown only after Chrome has been opened 7
    // or more times.
    config->event_configs.insert(EventConfig(
        events::kChromeOpened, Comparator(GREATER_THAN_OR_EQUAL, 7), 365, 365));

    return config;
  }
#endif  // BUILDFLAG(IS_IOS)

  if (kIPHDummyGroup.name == group->name) {
    // Only used for tests. Various magic tricks are used below to ensure this
    // config is invalid and unusable.
    absl::optional<GroupConfig> config = GroupConfig();
    config->valid = true;
    config->session_rate = Comparator(LESS_THAN, 0);
    config->trigger =
        EventConfig("dummy_group_iph_trigger", Comparator(LESS_THAN, 0), 1, 1);
    return config;
  }

  return absl::nullopt;
}

}  // namespace feature_engagement
