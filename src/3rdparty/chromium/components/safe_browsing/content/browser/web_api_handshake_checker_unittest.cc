// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/content/browser/web_api_handshake_checker.h"

#include "base/memory/scoped_refptr.h"
#include "base/test/bind.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "components/safe_browsing/core/browser/db/fake_database_manager.h"
#include "components/safe_browsing/core/browser/url_checker_delegate.h"
#include "components/safe_browsing/core/common/features.h"
#include "components/security_interstitials/core/unsafe_resource.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace safe_browsing {

class FakeUrlCheckerDelegate : public UrlCheckerDelegate {
 public:
  explicit FakeUrlCheckerDelegate(
      scoped_refptr<SafeBrowsingDatabaseManager> database_manager)
      : database_manager_(database_manager),
        threat_types_(
            SBThreatTypeSet({safe_browsing::SB_THREAT_TYPE_URL_PHISHING})) {}

  // UrlCheckerDelegate overrides:
  void MaybeDestroyNoStatePrefetchContents(
      base::OnceCallback<content::WebContents*()> web_contents_getter)
      override {}

  void StartDisplayingBlockingPageHelper(
      const security_interstitials::UnsafeResource& resource,
      const std::string& method,
      const net::HttpRequestHeaders& headers,
      bool is_main_frame,
      bool has_user_gesture) override {
    security_interstitials::UnsafeResource::UrlCheckResult result(
        /*proceed=*/false, /*showed_interstitial=*/false,
        /*has_post_commit_interstitial_skipped=*/false);
    resource.callback.Run(result);
  }

  void StartObservingInteractionsForDelayedBlockingPageHelper(
      const security_interstitials::UnsafeResource& resource,
      bool is_main_frame) override {}

  bool IsUrlAllowlisted(const GURL& url) override { return false; }

  void SetPolicyAllowlistDomains(
      const std::vector<std::string>& allowlist_domains) override {}

  bool ShouldSkipRequestCheck(
      const GURL& original_url,
      int frame_tree_node_id,
      int render_process_id,
      base::optional_ref<const base::UnguessableToken> render_frame_token,
      bool originated_from_service_worker) override {
    return false;
  }

  void NotifySuspiciousSiteDetected(
      const base::RepeatingCallback<content::WebContents*()>&
          web_contents_getter) override {}

  const SBThreatTypeSet& GetThreatTypes() override { return threat_types_; }

  SafeBrowsingDatabaseManager* GetDatabaseManager() override {
    return database_manager_.get();
  }

  BaseUIManager* GetUIManager() override { return nullptr; }

 protected:
  friend class base::RefCountedThreadSafe<FakeUrlCheckerDelegate>;
  ~FakeUrlCheckerDelegate() override = default;

 private:
  scoped_refptr<SafeBrowsingDatabaseManager> database_manager_;
  SBThreatTypeSet threat_types_;
};

class WebApiHandshakeCheckerTest : public testing::Test,
                                   public testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    if (GetParam()) {
      feature_list_.InitAndEnableFeature(kSafeBrowsingSkipSubresources2);
    } else {
      feature_list_.InitAndDisableFeature(kSafeBrowsingSkipSubresources2);
    }
    database_manager_ = base::MakeRefCounted<FakeSafeBrowsingDatabaseManager>(
        content::GetUIThreadTaskRunner({}), content::GetIOThreadTaskRunner({}));
    delegate_ = base::MakeRefCounted<FakeUrlCheckerDelegate>(database_manager_);
    handshake_checker_ = std::make_unique<WebApiHandshakeChecker>(
        base::BindOnce(&WebApiHandshakeCheckerTest::GetDelegate,
                       base::Unretained(this)),
        base::BindRepeating(&WebApiHandshakeCheckerTest::GetWebContents,
                            base::Unretained(this)),
        /*frame_tree_node_id=*/-1);
  }

  FakeSafeBrowsingDatabaseManager* database_manager() {
    return database_manager_.get();
  }

  WebApiHandshakeChecker::CheckResult Check(const GURL& url) {
    WebApiHandshakeChecker::CheckResult out;
    base::RunLoop loop;
    handshake_checker_->Check(
        url, base::BindLambdaForTesting(
                 [&](WebApiHandshakeChecker::CheckResult result) {
                   out = result;
                   loop.Quit();
                 }));
    loop.Run();
    return out;
  }

 private:
  scoped_refptr<UrlCheckerDelegate> GetDelegate() { return delegate_; }

  content::WebContents* GetWebContents() { return nullptr; }

  content::BrowserTaskEnvironment task_environment_;
  scoped_refptr<FakeSafeBrowsingDatabaseManager> database_manager_;
  scoped_refptr<FakeUrlCheckerDelegate> delegate_;
  std::unique_ptr<WebApiHandshakeChecker> handshake_checker_;
  base::test::ScopedFeatureList feature_list_;
};

INSTANTIATE_TEST_SUITE_P(All, WebApiHandshakeCheckerTest, testing::Bool());

TEST_P(WebApiHandshakeCheckerTest, CheckSafeUrl) {
  base::HistogramTester histogram_tester;
  const GURL kUrl("https://example.test");
  EXPECT_EQ(Check(kUrl), WebApiHandshakeChecker::CheckResult::kProceed);
  histogram_tester.ExpectUniqueSample(
      "SafeBrowsing.WebApiHandshakeCheck.Skipped", GetParam(), 1);
}

TEST_P(WebApiHandshakeCheckerTest, CheckDangerousUrl) {
  base::HistogramTester histogram_tester;
  const GURL kUrl("https://example.test");
  database_manager()->AddDangerousUrl(kUrl, SB_THREAT_TYPE_URL_PHISHING);
  if (GetParam()) {
    EXPECT_EQ(Check(kUrl), WebApiHandshakeChecker::CheckResult::kProceed);
  } else {
    EXPECT_EQ(Check(kUrl), WebApiHandshakeChecker::CheckResult::kBlocked);
  }
  histogram_tester.ExpectUniqueSample(
      "SafeBrowsing.WebApiHandshakeCheck.Skipped", GetParam(), 1);
}

}  // namespace safe_browsing
