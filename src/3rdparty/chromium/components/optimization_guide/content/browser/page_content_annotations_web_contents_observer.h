// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_OPTIMIZATION_GUIDE_CONTENT_BROWSER_PAGE_CONTENT_ANNOTATIONS_WEB_CONTENTS_OBSERVER_H_
#define COMPONENTS_OPTIMIZATION_GUIDE_CONTENT_BROWSER_PAGE_CONTENT_ANNOTATIONS_WEB_CONTENTS_OBSERVER_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/optimization_guide/content/browser/salient_image_retriever.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

class TemplateURLService;

namespace prerender {
class NoStatePrefetchManager;
}  // namespace prerender

namespace optimization_guide {

class PageContentAnnotationsService;

// This class is used to dispatch page content to the
// PageContentAnnotationsService to be annotated.
class PageContentAnnotationsWebContentsObserver
    : public content::WebContentsObserver,
      public content::WebContentsUserData<
          PageContentAnnotationsWebContentsObserver> {
 public:
  ~PageContentAnnotationsWebContentsObserver() override;

  PageContentAnnotationsWebContentsObserver(
      const PageContentAnnotationsWebContentsObserver&) = delete;
  PageContentAnnotationsWebContentsObserver& operator=(
      const PageContentAnnotationsWebContentsObserver&) = delete;

 protected:
  PageContentAnnotationsWebContentsObserver(
      content::WebContents* web_contents,
      PageContentAnnotationsService* page_content_annotations_service,
      TemplateURLService* template_url_service,
      prerender::NoStatePrefetchManager* no_state_prefetch_manager);

 private:
  friend class content::WebContentsUserData<
      PageContentAnnotationsWebContentsObserver>;
  friend class PageContentAnnotationsWebContentsObserverTest;

  // content::WebContentsObserver:
  void DocumentOnLoadCompletedInPrimaryMainFrame() override;

  void DidStopLoading() override;

  // Not owned. Guaranteed to outlive |this|.
  raw_ptr<TemplateURLService> template_url_service_;

  // Not owned. Guaranteed to outlive |this|.
  raw_ptr<PageContentAnnotationsService> page_content_annotations_service_;

  SalientImageRetriever salient_image_retriever_;

  // Not owned. Guaranteed to outlive |this|.
  raw_ptr<prerender::NoStatePrefetchManager> no_state_prefetch_manager_;

  base::WeakPtrFactory<PageContentAnnotationsWebContentsObserver>
      weak_ptr_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace optimization_guide

#endif  // COMPONENTS_OPTIMIZATION_GUIDE_CONTENT_BROWSER_PAGE_CONTENT_ANNOTATIONS_WEB_CONTENTS_OBSERVER_H_
