// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/preloading/prefetch/prefetch_features.h"
#include "base/feature_list.h"

namespace features {

BASE_FEATURE(kPrefetchUseContentRefactor,
             "PrefetchUseContentRefactor",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kPrefetchReusable,
             "PrefetchReusable",
             base::FEATURE_DISABLED_BY_DEFAULT);

const base::FeatureParam<int> kPrefetchReusableBodySizeLimit{
    &kPrefetchReusable, "prefetch_reusable_body_size_limit", 65536};

BASE_FEATURE(kPrefetchNIKScope,
             "PrefetchNIKScope",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kPrefetchDocumentManagerEarlyCookieCopySkipped,
             "PrefetchDocumentManagerEarlyCookieCopySkipped",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kPrefetchUsesHTTPCache,
             "PrefetchUsesHTTPCache",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kPrefetchProxy, "PrefetchProxy", base::FEATURE_ENABLED_BY_DEFAULT);

}  // namespace features
