// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/signin/internal/identity_manager/primary_account_manager.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/image_fetcher/core/fake_image_decoder.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/prefs/testing_pref_service.h"
#include "components/signin/internal/identity_manager/account_fetcher_service.h"
#include "components/signin/internal/identity_manager/account_tracker_service.h"
#include "components/signin/internal/identity_manager/fake_account_capabilities_fetcher_factory.h"
#include "components/signin/internal/identity_manager/fake_profile_oauth2_token_service_delegate.h"
#include "components/signin/internal/identity_manager/profile_oauth2_token_service.h"
#include "components/signin/public/base/signin_client.h"
#include "components/signin/public/base/signin_metrics.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/signin/public/base/test_signin_client.h"
#include "components/signin/public/identity_manager/account_info.h"
#include "components/signin/public/identity_manager/identity_test_utils.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

using base::Bucket;
using signin::ConsentLevel;
using signin_metrics::AccessPoint;
using signin_metrics::ProfileSignout;
using testing::ElementsAreArray;

namespace {
struct ExpectedAccessPoints {
  absl::optional<AccessPoint> sign_in = absl::nullopt;
  absl::optional<AccessPoint> sync_opt_in = absl::nullopt;
  absl::optional<ProfileSignout> sign_out = absl::nullopt;
  absl::optional<ProfileSignout> turn_off_sync = absl::nullopt;
};
}  // namespace

class PrimaryAccountManagerTest : public testing::Test,
                                  public PrimaryAccountManager::Observer {
 public:
  PrimaryAccountManagerTest() : test_signin_client_(&user_prefs_) {
#if BUILDFLAG(IS_ANDROID)
    // Mock AccountManagerFacade in java code for tests that require its
    // initialization.
    signin::SetUpMockAccountManagerFacade();
#endif
    AccountFetcherService::RegisterPrefs(user_prefs_.registry());
    AccountTrackerService::RegisterPrefs(user_prefs_.registry());
    ProfileOAuth2TokenService::RegisterProfilePrefs(user_prefs_.registry());
    PrimaryAccountManager::RegisterProfilePrefs(user_prefs_.registry());
    account_tracker_ = std::make_unique<AccountTrackerService>();
    account_tracker_->Initialize(&user_prefs_, base::FilePath());
    token_service_ = std::make_unique<ProfileOAuth2TokenService>(
        &user_prefs_,
        std::make_unique<FakeProfileOAuth2TokenServiceDelegate>());
    account_fetcher_ = std::make_unique<AccountFetcherService>();
    account_fetcher_->Initialize(
        &test_signin_client_, token_service_.get(), account_tracker_.get(),
        std::make_unique<image_fetcher::FakeImageDecoder>(),
        std::make_unique<FakeAccountCapabilitiesFetcherFactory>());
  }

  ~PrimaryAccountManagerTest() override {
    if (manager_)
      ShutDownManager();
    test_signin_client_.Shutdown();
  }

  TestSigninClient* signin_client() { return &test_signin_client_; }

  AccountTrackerService* account_tracker() { return account_tracker_.get(); }
  AccountFetcherService* account_fetcher() { return account_fetcher_.get(); }
  PrefService* prefs() { return &user_prefs_; }

  // Seed the account tracker with information from logged in user.  Normally
  // this is done by UI code before calling PrimaryAccountManager.
  // Returns the string to use as the account_id.
  CoreAccountId AddToAccountTracker(const std::string& gaia_id,
                                    const std::string& email) {
    account_tracker_->SeedAccountInfo(gaia_id, email);
    return account_tracker_->PickAccountIdForAccount(gaia_id, email);
  }

  void CheckSigninMetrics(ExpectedAccessPoints access_points) {
    std::vector<Bucket> expected_sign_in_buckets;
    if (access_points.sign_in.has_value()) {
      expected_sign_in_buckets.emplace_back(*access_points.sign_in, 1);
    }
    EXPECT_THAT(histogram_tester_.GetAllSamples("Signin.SignIn.Completed"),
                ElementsAreArray(expected_sign_in_buckets));

    std::vector<Bucket> expected_sync_opt_in_buckets;
    if (access_points.sync_opt_in.has_value()) {
      expected_sync_opt_in_buckets.emplace_back(*access_points.sync_opt_in, 1);
    }
    EXPECT_THAT(histogram_tester_.GetAllSamples("Signin.SyncOptIn.Completed"),
                ElementsAreArray(expected_sync_opt_in_buckets));

    std::vector<Bucket> expected_sign_out_buckets;
    if (access_points.sign_out.has_value()) {
      expected_sign_out_buckets.emplace_back(*access_points.sign_out, 1);
    }
    EXPECT_THAT(histogram_tester_.GetAllSamples("Signin.SignOut.Completed"),
                ElementsAreArray(expected_sign_out_buckets));

    std::vector<Bucket> expected_turn_off_sync_buckets;
    if (access_points.turn_off_sync.has_value()) {
      expected_turn_off_sync_buckets.emplace_back(*access_points.turn_off_sync,
                                                  1);
    }
    EXPECT_THAT(histogram_tester_.GetAllSamples("Signin.SyncTurnOff.Completed"),
                ElementsAreArray(expected_turn_off_sync_buckets));
  }

  void CheckInitializeAccountInfoStateHistogram(
      PrimaryAccountManager::InitializeAccountInfoState expected_sample) {
    histogram_tester_.ExpectUniqueSample(
        "Signin.PAMInitialize.PrimaryAccountInfoState",
        /*sample=*/expected_sample, /*expected_bucket_count=*/1);
  }

  void CreatePrimaryAccountManager() {
    DCHECK(!manager_);
    manager_ = std::make_unique<PrimaryAccountManager>(
        &test_signin_client_, token_service_.get(), account_tracker_.get());
    manager_->Initialize();
    manager_->AddObserver(this);
  }

  // Shuts down |manager_|.
  void ShutDownManager() {
    DCHECK(manager_);
    manager_->RemoveObserver(this);
    manager_.reset();
  }

  void OnPrimaryAccountChanged(
      const signin::PrimaryAccountChangeEvent& event_details) override {
    DCHECK(event_details.GetEventTypeFor(signin::ConsentLevel::kSync) !=
               signin::PrimaryAccountChangeEvent::Type::kNone ||
           event_details.GetEventTypeFor(signin::ConsentLevel::kSignin) !=
               signin::PrimaryAccountChangeEvent::Type::kNone)
        << "PrimaryAccountChangeEvent with no change: " << event_details;

    switch (event_details.GetEventTypeFor(ConsentLevel::kSync)) {
      case signin::PrimaryAccountChangeEvent::Type::kSet:
        num_successful_signins_++;
        break;
      case signin::PrimaryAccountChangeEvent::Type::kCleared:
        num_successful_signouts_++;
        break;
      case signin::PrimaryAccountChangeEvent::Type::kNone:
        break;
    }
    switch (event_details.GetEventTypeFor(ConsentLevel::kSignin)) {
      case signin::PrimaryAccountChangeEvent::Type::kSet:
      case signin::PrimaryAccountChangeEvent::Type::kCleared:
        num_unconsented_account_changed_++;
        break;
      case signin::PrimaryAccountChangeEvent::Type::kNone:
        break;
    }
  }

  base::test::TaskEnvironment task_environment_;
  sync_preferences::TestingPrefServiceSyncable user_prefs_;
  TestSigninClient test_signin_client_;
  std::unique_ptr<AccountTrackerService> account_tracker_;
  std::unique_ptr<ProfileOAuth2TokenService> token_service_;
  std::unique_ptr<AccountFetcherService> account_fetcher_;
  std::unique_ptr<PrimaryAccountManager> manager_;
  std::vector<std::string> oauth_tokens_fetched_;
  std::vector<std::string> cookies_;
  base::HistogramTester histogram_tester_;
  int num_successful_signins_{0};
  int num_successful_signouts_{0};
  int num_unconsented_account_changed_{0};
};

#if !BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PrimaryAccountManagerTest, SignOut) {
  CreatePrimaryAccountManager();
  CoreAccountId main_account_id =
      AddToAccountTracker("account_id", "user@gmail.com");
  manager_->SetPrimaryAccountInfo(
      account_tracker()->GetAccountInfo(main_account_id), ConsentLevel::kSync,
      AccessPoint::ACCESS_POINT_UNKNOWN);
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_UNKNOWN});

  manager_->ClearPrimaryAccount(signin_metrics::ProfileSignout::kTest,
                                signin_metrics::SignoutDelete::kIgnoreMetric);
  EXPECT_EQ(1, num_successful_signouts_);
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  EXPECT_TRUE(
      manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).email.empty());
  EXPECT_TRUE(manager_->GetPrimaryAccountId(ConsentLevel::kSync).empty());
  EXPECT_TRUE(manager_->GetPrimaryAccountInfo(ConsentLevel::kSignin).IsEmpty());
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sign_out = signin_metrics::ProfileSignout::kTest,
                      .turn_off_sync = signin_metrics::ProfileSignout::kTest});

  // Should not be persisted anymore
  ShutDownManager();
  CreatePrimaryAccountManager();
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  EXPECT_TRUE(
      manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).email.empty());
  EXPECT_TRUE(manager_->GetPrimaryAccountId(ConsentLevel::kSync).empty());
  EXPECT_TRUE(manager_->GetPrimaryAccountInfo(ConsentLevel::kSignin).IsEmpty());
}

TEST_F(PrimaryAccountManagerTest, SignOutRevoke) {
  CreatePrimaryAccountManager();
  CoreAccountId main_account_id =
      AddToAccountTracker("main_id", "user@gmail.com");
  CoreAccountId other_account_id =
      AddToAccountTracker("other_id", "other@gmail.com");
  token_service_->UpdateCredentials(main_account_id, "token");
  token_service_->UpdateCredentials(other_account_id, "token");
  manager_->SetPrimaryAccountInfo(
      account_tracker()->GetAccountInfo(main_account_id), ConsentLevel::kSync,
      AccessPoint::ACCESS_POINT_UNKNOWN);
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_UNKNOWN});
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  EXPECT_EQ(main_account_id,
            manager_->GetPrimaryAccountId(ConsentLevel::kSync));

  manager_->ClearPrimaryAccount(signin_metrics::ProfileSignout::kTest,
                                signin_metrics::SignoutDelete::kIgnoreMetric);
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sign_out = signin_metrics::ProfileSignout::kTest,
                      .turn_off_sync = signin_metrics::ProfileSignout::kTest});

  // Tokens are revoked.
  EXPECT_EQ(1, num_successful_signouts_);
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  EXPECT_TRUE(token_service_->GetAccounts().empty());
}

TEST_F(PrimaryAccountManagerTest, SignOutWhileProhibited) {
  CreatePrimaryAccountManager();
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  EXPECT_TRUE(
      manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).email.empty());
  EXPECT_TRUE(manager_->GetPrimaryAccountId(ConsentLevel::kSync).empty());

  CoreAccountId main_account_id =
      AddToAccountTracker("gaia_id", "user@gmail.com");
  manager_->SetPrimaryAccountInfo(
      account_tracker()->GetAccountInfo(main_account_id), ConsentLevel::kSync,
      AccessPoint::ACCESS_POINT_UNKNOWN);
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_UNKNOWN});

  signin_client()->set_is_clear_primary_account_allowed_for_testing(
      SigninClient::SignoutDecision::CLEAR_PRIMARY_ACCOUNT_DISALLOWED);
  manager_->ClearPrimaryAccount(signin_metrics::ProfileSignout::kTest,
                                signin_metrics::SignoutDelete::kIgnoreMetric);
  EXPECT_EQ(0, num_successful_signouts_);
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_UNKNOWN});

  signin_client()->set_is_clear_primary_account_allowed_for_testing(
      SigninClient::SignoutDecision::REVOKE_SYNC_DISALLOWED);
  manager_->ClearPrimaryAccount(signin_metrics::ProfileSignout::kTest,
                                signin_metrics::SignoutDelete::kIgnoreMetric);
  EXPECT_EQ(0, num_successful_signouts_);
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_UNKNOWN});

  signin_client()->set_is_clear_primary_account_allowed_for_testing(
      SigninClient::SignoutDecision::ALLOW);
  manager_->ClearPrimaryAccount(signin_metrics::ProfileSignout::kTest,
                                signin_metrics::SignoutDelete::kIgnoreMetric);
  EXPECT_EQ(1, num_successful_signouts_);
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sign_out = signin_metrics::ProfileSignout::kTest,
                      .turn_off_sync = signin_metrics::ProfileSignout::kTest});
}

TEST_F(PrimaryAccountManagerTest, UnconsentedSignOutWhileProhibited) {
  CreatePrimaryAccountManager();
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  EXPECT_TRUE(
      manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).email.empty());
  EXPECT_TRUE(manager_->GetPrimaryAccountId(ConsentLevel::kSync).empty());

  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  CoreAccountInfo account_info = account_tracker()->GetAccountInfo(account_id);
  manager_->SetPrimaryAccountInfo(account_info, ConsentLevel::kSignin,
                                  AccessPoint::ACCESS_POINT_UNKNOWN);
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN});

  signin_client()->set_is_clear_primary_account_allowed_for_testing(
      SigninClient::SignoutDecision::CLEAR_PRIMARY_ACCOUNT_DISALLOWED);
  manager_->ClearPrimaryAccount(signin_metrics::ProfileSignout::kTest,
                                signin_metrics::SignoutDelete::kIgnoreMetric);
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN});

  signin_client()->set_is_clear_primary_account_allowed_for_testing(
      SigninClient::SignoutDecision::ALLOW);
  manager_->ClearPrimaryAccount(signin_metrics::ProfileSignout::kTest,
                                signin_metrics::SignoutDelete::kIgnoreMetric);
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sign_out = signin_metrics::ProfileSignout::kTest});
}
#endif

TEST_F(PrimaryAccountManagerTest, RevokeSyncConsentAllowedSignoutProhibited) {
  CreatePrimaryAccountManager();
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  EXPECT_TRUE(
      manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).email.empty());
  EXPECT_TRUE(manager_->GetPrimaryAccountId(ConsentLevel::kSync).empty());

  CoreAccountId main_account_id =
      AddToAccountTracker("gaia_id", "user@gmail.com");
  manager_->SetPrimaryAccountInfo(
      account_tracker()->GetAccountInfo(main_account_id), ConsentLevel::kSync,
      AccessPoint::ACCESS_POINT_UNKNOWN);
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_UNKNOWN});

  signin_client()->set_is_clear_primary_account_allowed_for_testing(
      SigninClient::SignoutDecision::CLEAR_PRIMARY_ACCOUNT_DISALLOWED);
  manager_->RevokeSyncConsent(signin_metrics::ProfileSignout::kTest,
                              signin_metrics::SignoutDelete::kIgnoreMetric);
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .turn_off_sync = signin_metrics::ProfileSignout::kTest});

  // Unconsented primary account not changed.
  EXPECT_EQ(1, num_unconsented_account_changed_);
  // Sync consent cleared
  EXPECT_EQ(1, num_successful_signouts_);
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));

#if !BUILDFLAG(IS_CHROMEOS_ASH)
  manager_->ClearPrimaryAccount(signin_metrics::ProfileSignout::kTest,
                                signin_metrics::SignoutDelete::kIgnoreMetric);
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_UNKNOWN,
                      .turn_off_sync = signin_metrics::ProfileSignout::kTest});
#endif
}

// Regression test for https://crbug.com/1155519.
TEST_F(PrimaryAccountManagerTest, NoopSignOutDoesNotNotifyObservers) {
  CreatePrimaryAccountManager();
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));

  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  CoreAccountInfo account_info = account_tracker()->GetAccountInfo(account_id);
  manager_->SetPrimaryAccountInfo(account_info, ConsentLevel::kSignin,
                                  AccessPoint::ACCESS_POINT_UNKNOWN);
  EXPECT_EQ(1, num_unconsented_account_changed_);
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));

  manager_->RevokeSyncConsent(signin_metrics::ProfileSignout::kTest,
                              signin_metrics::SignoutDelete::kIgnoreMetric);

  // Since there was no sync consent, observers shouldn't be notified.
  EXPECT_EQ(0, num_successful_signouts_);
  EXPECT_EQ(1, num_unconsented_account_changed_);
}

TEST_F(PrimaryAccountManagerTest, SignIn) {
  CreatePrimaryAccountManager();
  EXPECT_EQ("", manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).email);
  EXPECT_EQ(CoreAccountId(),
            manager_->GetPrimaryAccountId(ConsentLevel::kSync));
  EXPECT_EQ(0, num_successful_signins_);
  EXPECT_EQ(0, num_unconsented_account_changed_);
  CheckSigninMetrics({});

  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  base::RunLoop loop;
  manager_->SetPrimaryAccountInfo(
      account_tracker()->GetAccountInfo(account_id), ConsentLevel::kSync,
      AccessPoint::ACCESS_POINT_SETTINGS, loop.QuitClosure());

  EXPECT_EQ(1, num_successful_signins_);
  EXPECT_EQ(1, num_unconsented_account_changed_);
  EXPECT_EQ("user@gmail.com",
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).email);
  EXPECT_EQ(account_id, manager_->GetPrimaryAccountId(ConsentLevel::kSync));
  EXPECT_EQ(manager_->GetPrimaryAccountInfo(ConsentLevel::kSignin),
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSync));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_SETTINGS,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_SETTINGS});

  // The primary account info and metrics should be changed synchronously, only
  // the prefs commit should happen asynchronously and be verified after the
  // `loop.Run()` here.
  loop.Run();
  EXPECT_TRUE(user_prefs_.user_prefs_store()->committed());
}

TEST_F(PrimaryAccountManagerTest,
       ExternalSignIn_ReauthShouldNotSendNotification) {
  CreatePrimaryAccountManager();
  EXPECT_EQ("", manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).email);
  EXPECT_EQ(CoreAccountId(),
            manager_->GetPrimaryAccountId(ConsentLevel::kSync));
  EXPECT_EQ(0, num_successful_signins_);
  EXPECT_EQ(0, num_unconsented_account_changed_);

  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  manager_->SetPrimaryAccountInfo(account_tracker()->GetAccountInfo(account_id),
                                  ConsentLevel::kSync,
                                  AccessPoint::ACCESS_POINT_SETTINGS);
  EXPECT_EQ(1, num_successful_signins_);
  EXPECT_EQ(1, num_unconsented_account_changed_);
  EXPECT_EQ("user@gmail.com",
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).email);
  EXPECT_EQ(account_id, manager_->GetPrimaryAccountId(ConsentLevel::kSync));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_SETTINGS,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_SETTINGS});

  manager_->SetPrimaryAccountInfo(account_tracker()->GetAccountInfo(account_id),
                                  ConsentLevel::kSync,
                                  AccessPoint::ACCESS_POINT_WEB_SIGNIN);
  EXPECT_EQ(1, num_successful_signins_);
  EXPECT_EQ(1, num_unconsented_account_changed_);
  EXPECT_EQ("user@gmail.com",
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).email);
  EXPECT_EQ(account_id, manager_->GetPrimaryAccountId(ConsentLevel::kSync));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_SETTINGS,
                      .sync_opt_in = AccessPoint::ACCESS_POINT_SETTINGS});
}

#if BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PrimaryAccountManagerTest, GaiaIdMigration) {
  ASSERT_EQ(AccountTrackerService::MIGRATION_DONE,
            account_tracker()->GetMigrationState());
  std::string email = "user@gmail.com";
  std::string gaia_id = "account_gaia_id";

  PrefService* client_prefs = signin_client()->GetPrefs();
  client_prefs->SetInteger(prefs::kAccountIdMigrationState,
                           AccountTrackerService::MIGRATION_NOT_STARTED);
  ScopedListPrefUpdate update(client_prefs, prefs::kAccountInfo);
  update->clear();
  base::Value::Dict dict;
  dict.Set("account_id", email);
  dict.Set("email", email);
  dict.Set("gaia", gaia_id);
  update->Append(std::move(dict));

  account_tracker()->ResetForTesting();

  client_prefs->SetString(prefs::kGoogleServicesAccountId, email);
  client_prefs->SetBoolean(prefs::kGoogleServicesConsentedToSync, true);

  CreatePrimaryAccountManager();

  EXPECT_EQ(CoreAccountId::FromGaiaId(gaia_id),
            manager_->GetPrimaryAccountId(ConsentLevel::kSync));
  EXPECT_EQ(gaia_id, user_prefs_.GetString(prefs::kGoogleServicesAccountId));
}

TEST_F(PrimaryAccountManagerTest, GaiaIdMigrationCrashInTheMiddle) {
  ASSERT_EQ(AccountTrackerService::MIGRATION_DONE,
            account_tracker()->GetMigrationState());
  std::string email = "user@gmail.com";
  std::string gaia_id = "account_gaia_id";

  PrefService* client_prefs = signin_client()->GetPrefs();
  client_prefs->SetInteger(prefs::kAccountIdMigrationState,
                           AccountTrackerService::MIGRATION_NOT_STARTED);
  ScopedListPrefUpdate update(client_prefs, prefs::kAccountInfo);
  update->clear();
  base::Value::Dict dict;
  dict.Set("account_id", email);
  dict.Set("email", email);
  dict.Set("gaia", gaia_id);
  update->Append(std::move(dict));

  account_tracker()->ResetForTesting();

  client_prefs->SetString(prefs::kGoogleServicesAccountId, gaia_id);
  client_prefs->SetBoolean(prefs::kGoogleServicesConsentedToSync, true);

  CreatePrimaryAccountManager();
  EXPECT_EQ(CoreAccountId::FromGaiaId(gaia_id),
            manager_->GetPrimaryAccountId(ConsentLevel::kSync));
  EXPECT_EQ(gaia_id, user_prefs_.GetString(prefs::kGoogleServicesAccountId));

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(AccountTrackerService::MIGRATION_DONE,
            account_tracker()->GetMigrationState());
}
#endif

TEST_F(PrimaryAccountManagerTest, RestoreFromPrefsConsented) {
  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  user_prefs_.SetString(prefs::kGoogleServicesAccountId, account_id.ToString());
  user_prefs_.SetBoolean(prefs::kGoogleServicesConsentedToSync, true);
  CreatePrimaryAccountManager();
  EXPECT_EQ("user@gmail.com",
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).email);
  EXPECT_EQ(account_id, manager_->GetPrimaryAccountId(ConsentLevel::kSync));
  EXPECT_EQ(manager_->GetPrimaryAccountInfo(ConsentLevel::kSignin),
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSync));

  // Not a logged event.
  CheckSigninMetrics({});
}

TEST_F(PrimaryAccountManagerTest, RestoreFromPrefsUnconsented) {
  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  user_prefs_.SetString(prefs::kGoogleServicesAccountId, account_id.ToString());
  user_prefs_.SetBoolean(prefs::kGoogleServicesConsentedToSync, false);
  CreatePrimaryAccountManager();
  EXPECT_EQ("user@gmail.com",
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSignin).email);
  EXPECT_EQ(account_id,
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSignin).account_id);
  EXPECT_TRUE(manager_->GetPrimaryAccountInfo(ConsentLevel::kSync).IsEmpty());

  // Not a logged event.
  CheckSigninMetrics({});
}

TEST_F(PrimaryAccountManagerTest, SetUnconsentedPrimaryAccountInfo) {
  CreatePrimaryAccountManager();
  EXPECT_EQ(CoreAccountInfo(),
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSignin));
  EXPECT_EQ(0, num_unconsented_account_changed_);
  EXPECT_EQ(0, num_successful_signins_);
  CheckSigninMetrics({});

  // Set the unconsented primary account.
  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  CoreAccountInfo account_info = account_tracker()->GetAccountInfo(account_id);

  base::RunLoop loop;
  manager_->SetPrimaryAccountInfo(account_info, ConsentLevel::kSignin,
                                  AccessPoint::ACCESS_POINT_SETTINGS,
                                  loop.QuitClosure());

  EXPECT_EQ(0, num_successful_signins_);
  EXPECT_EQ(0, num_successful_signouts_);
  EXPECT_EQ(1, num_unconsented_account_changed_);
  EXPECT_EQ(account_info,
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSignin));
  EXPECT_EQ(CoreAccountInfo(),
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSync));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_SETTINGS});

  // The primary account info and metrics should be changed synchronously, only
  // the prefs commit should happen asynchronously and be verified after the
  // `loop.Run()` here.
  loop.Run();
  EXPECT_TRUE(user_prefs_.user_prefs_store()->committed());

  // Set the same account again.
  manager_->SetPrimaryAccountInfo(account_info, ConsentLevel::kSignin,
                                  AccessPoint::ACCESS_POINT_WEB_SIGNIN);
  EXPECT_EQ(0, num_successful_signins_);
  EXPECT_EQ(0, num_successful_signouts_);
  EXPECT_EQ(1, num_unconsented_account_changed_);
  EXPECT_EQ(account_info,
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSignin));
  EXPECT_EQ(CoreAccountInfo(),
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSync));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_SETTINGS});

  // Change the email to another equivalent email. The account is updated but
  // observers are not notified.
  account_info.email = "us.er@gmail.com";
  manager_->SetPrimaryAccountInfo(account_info, ConsentLevel::kSignin,
                                  AccessPoint::ACCESS_POINT_SIGNIN_PROMO);
  EXPECT_EQ(0, num_successful_signins_);
  EXPECT_EQ(0, num_successful_signouts_);
  EXPECT_EQ(1, num_unconsented_account_changed_);
  EXPECT_EQ(account_info,
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSignin));
  EXPECT_EQ(CoreAccountInfo(),
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSync));
  CheckSigninMetrics({.sign_in = AccessPoint::ACCESS_POINT_SETTINGS});
}

TEST_F(PrimaryAccountManagerTest, RevokeSyncConsent) {
  CreatePrimaryAccountManager();
  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  manager_->SetPrimaryAccountInfo(account_tracker()->GetAccountInfo(account_id),
                                  ConsentLevel::kSync,
                                  AccessPoint::ACCESS_POINT_UNKNOWN);
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSync));

  manager_->RevokeSyncConsent(signin_metrics::ProfileSignout::kTest,
                              signin_metrics::SignoutDelete::kIgnoreMetric);
  EXPECT_EQ(1, num_successful_signouts_);
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  EXPECT_EQ(account_id,
            manager_->GetPrimaryAccountInfo(ConsentLevel::kSignin).account_id);
}

#if !BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(PrimaryAccountManagerTest, ClearPrimaryAccount) {
  CreatePrimaryAccountManager();
  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  manager_->SetPrimaryAccountInfo(account_tracker()->GetAccountInfo(account_id),
                                  ConsentLevel::kSync,
                                  AccessPoint::ACCESS_POINT_UNKNOWN);
  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSync));

  manager_->ClearPrimaryAccount(signin_metrics::ProfileSignout::kTest,
                                signin_metrics::SignoutDelete::kIgnoreMetric);
  EXPECT_EQ(1, num_successful_signouts_);
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
}
#endif  // !BUILDFLAG(IS_CHROMEOS_ASH)

TEST_F(PrimaryAccountManagerTest,
       RecordExistingPreviousSyncAccountIfCurrentlySignedOut) {
  user_prefs_.SetString(prefs::kGoogleServicesLastSyncingGaiaId,
                        "previous_gaia_id");
  CreatePrimaryAccountManager();
  ASSERT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));

  histogram_tester_.ExpectUniqueSample(
      "Signin.HadPreviousSyncAccount.SyncOffOnProfileLoad",
      /*sample=*/true, /*expected_bucket_count=*/1);
  histogram_tester_.ExpectUniqueSample(
      "Signin.HadPreviousSyncAccount.SignedOutOnProfileLoad",
      /*sample=*/true, /*expected_bucket_count=*/1);
}

TEST_F(PrimaryAccountManagerTest,
       RecordExistingPreviousSyncAccountIfCurrentlyUnconsented) {
  user_prefs_.SetString(prefs::kGoogleServicesLastSyncingGaiaId,
                        "previous_gaia_id");
  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  user_prefs_.SetString(prefs::kGoogleServicesAccountId, account_id.ToString());
  user_prefs_.SetBoolean(prefs::kGoogleServicesConsentedToSync, false);
  CreatePrimaryAccountManager();
  ASSERT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  ASSERT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));

  // If a signed in primary account exists but sync is off, only one of the
  // metrics should be recorded.
  histogram_tester_.ExpectUniqueSample(
      "Signin.HadPreviousSyncAccount.SyncOffOnProfileLoad",
      /*sample=*/true, /*expected_bucket_count=*/1);
  histogram_tester_.ExpectTotalCount(
      "Signin.HadPreviousSyncAccount.SignedOutOnProfileLoad",
      /*expected_count=*/0);
}

TEST_F(PrimaryAccountManagerTest,
       DoNotRecordExistingPreviousSyncAccountIfCurrentlyConsented) {
  user_prefs_.SetString(prefs::kGoogleServicesLastSyncingGaiaId,
                        "previous_gaia_id");
  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  user_prefs_.SetString(prefs::kGoogleServicesAccountId, account_id.ToString());
  user_prefs_.SetBoolean(prefs::kGoogleServicesConsentedToSync, true);
  CreatePrimaryAccountManager();
  ASSERT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSync));

  // If sync is currently on, none of the metrics should be recorded.
  histogram_tester_.ExpectTotalCount(
      "Signin.HadPreviousSyncAccount.SyncOffOnProfileLoad",
      /*expected_count=*/0);
  histogram_tester_.ExpectTotalCount(
      "Signin.HadPreviousSyncAccount.SignedOutOnProfileLoad",
      /*expected_count=*/0);
}

TEST_F(PrimaryAccountManagerTest,
       RecordAbsenceOfPreviousSyncAccountIfCurrentlySignedOut) {
  // Leave `prefs::kGoogleServicesLastSyncingGaiaId` unset so there is no
  // previous sync account.
  CreatePrimaryAccountManager();
  ASSERT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));

  histogram_tester_.ExpectUniqueSample(
      "Signin.HadPreviousSyncAccount.SyncOffOnProfileLoad",
      /*sample=*/false, /*expected_bucket_count=*/1);
  histogram_tester_.ExpectUniqueSample(
      "Signin.HadPreviousSyncAccount.SignedOutOnProfileLoad",
      /*sample=*/false, /*expected_bucket_count=*/1);
}

TEST_F(PrimaryAccountManagerTest,
       RecordAbsenceOfPreviousSyncAccountIfCurrentlyUnconsented) {
  // Leave `prefs::kGoogleServicesLastSyncingGaiaId` unset so there is no
  // previous sync account.
  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  user_prefs_.SetString(prefs::kGoogleServicesAccountId, account_id.ToString());
  user_prefs_.SetBoolean(prefs::kGoogleServicesConsentedToSync, false);
  CreatePrimaryAccountManager();
  CheckInitializeAccountInfoStateHistogram(
      PrimaryAccountManager::InitializeAccountInfoState::kAccountInfoAvailable);
  ASSERT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  ASSERT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSync));

  // If a signed in primary account exists but sync is off, only one of the
  // metrics should be recorded.
  histogram_tester_.ExpectUniqueSample(
      "Signin.HadPreviousSyncAccount.SyncOffOnProfileLoad",
      /*sample=*/false, /*expected_bucket_count=*/1);
  histogram_tester_.ExpectTotalCount(
      "Signin.HadPreviousSyncAccount.SignedOutOnProfileLoad",
      /*expected_count=*/0);
}

TEST_F(PrimaryAccountManagerTest,
       DoNotRecordAbsenceOfPreviousSyncAccountIfCurrentlyConsented) {
  // Leave `prefs::kGoogleServicesLastSyncingGaiaId` unset so there is no
  // previous sync account.
  CoreAccountId account_id = AddToAccountTracker("gaia_id", "user@gmail.com");
  user_prefs_.SetString(prefs::kGoogleServicesAccountId, account_id.ToString());
  user_prefs_.SetBoolean(prefs::kGoogleServicesConsentedToSync, true);
  CreatePrimaryAccountManager();
  CheckInitializeAccountInfoStateHistogram(
      PrimaryAccountManager::InitializeAccountInfoState::kAccountInfoAvailable);
  ASSERT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSync));

  // If sync is currently on, none of the metrics should be recorded.
  histogram_tester_.ExpectTotalCount(
      "Signin.HadPreviousSyncAccount.SyncOffOnProfileLoad",
      /*expected_count=*/0);
  histogram_tester_.ExpectTotalCount(
      "Signin.HadPreviousSyncAccount.SignedOutOnProfileLoad",
      /*expected_count=*/0);
}

TEST_F(PrimaryAccountManagerTest, RestoreSyncAccountInfo) {
  user_prefs_.SetString(prefs::kGoogleServicesLastSyncingUsername,
                        "user@gmail.com");
  user_prefs_.SetString(prefs::kGoogleServicesLastSyncingGaiaId, "gaia_id");
  CoreAccountId account_id =
      account_tracker()->PickAccountIdForAccount("gaia_id", "user@gmail.com");
  ASSERT_FALSE(account_id.empty());
  ASSERT_TRUE(account_tracker()->GetAccountInfo(account_id).IsEmpty());
  user_prefs_.SetString(prefs::kGoogleServicesAccountId, account_id.ToString());
  user_prefs_.SetBoolean(prefs::kGoogleServicesConsentedToSync, true);
  CreatePrimaryAccountManager();

  EXPECT_TRUE(manager_->HasPrimaryAccount(ConsentLevel::kSync));
  CoreAccountInfo account_info = account_tracker()->GetAccountInfo(account_id);
  ASSERT_FALSE(account_info.IsEmpty());
  EXPECT_EQ(account_id, account_info.account_id);
  EXPECT_EQ("gaia_id", account_info.gaia);
  EXPECT_EQ("user@gmail.com", account_info.email);
  CheckInitializeAccountInfoStateHistogram(
      PrimaryAccountManager::InitializeAccountInfoState::
          kEmptyAccountInfo_RestoreSuccessFromLastSyncInfo);
}

TEST_F(PrimaryAccountManagerTest, RestoreFailedLastSyncGaiaIDMissing) {
  user_prefs_.SetString(prefs::kGoogleServicesLastSyncingUsername,
                        "user@gmail.com");
  CoreAccountId account_id =
      account_tracker()->PickAccountIdForAccount("gaia_id", "user@gmail.com");
  ASSERT_FALSE(account_id.empty());
  ASSERT_TRUE(account_tracker()->GetAccountInfo(account_id).IsEmpty());
  user_prefs_.SetString(prefs::kGoogleServicesAccountId, account_id.ToString());
  user_prefs_.SetBoolean(prefs::kGoogleServicesConsentedToSync, true);
  CreatePrimaryAccountManager();

  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  EXPECT_TRUE(account_tracker()->GetAccountInfo(account_id).IsEmpty());
  CheckInitializeAccountInfoStateHistogram(
      PrimaryAccountManager::InitializeAccountInfoState::
          kEmptyAccountInfo_RestoreFailedNoLastSyncGaiaId);
}

TEST_F(PrimaryAccountManagerTest, RestoreFailedLastSyncEmailMissing) {
  user_prefs_.SetString(prefs::kGoogleServicesLastSyncingGaiaId, "gaia_id");
  CoreAccountId account_id =
      account_tracker()->PickAccountIdForAccount("gaia_id", "user@gmail.com");
  ASSERT_FALSE(account_id.empty());
  ASSERT_TRUE(account_tracker()->GetAccountInfo(account_id).IsEmpty());
  user_prefs_.SetString(prefs::kGoogleServicesAccountId, account_id.ToString());
  user_prefs_.SetBoolean(prefs::kGoogleServicesConsentedToSync, true);
  CreatePrimaryAccountManager();

  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  EXPECT_TRUE(account_tracker()->GetAccountInfo(account_id).IsEmpty());
  CheckInitializeAccountInfoStateHistogram(
      PrimaryAccountManager::InitializeAccountInfoState::
          kEmptyAccountInfo_RestoreFailedNoLastSyncEmail);
}

TEST_F(PrimaryAccountManagerTest, RestoreFailedNotSyncing) {
  CoreAccountId account_id =
      account_tracker()->PickAccountIdForAccount("gaia_id", "user@gmail.com");
  ASSERT_FALSE(account_id.empty());
  ASSERT_TRUE(account_tracker()->GetAccountInfo(account_id).IsEmpty());
  user_prefs_.SetString(prefs::kGoogleServicesAccountId, account_id.ToString());
  user_prefs_.SetBoolean(prefs::kGoogleServicesConsentedToSync, false);
  CreatePrimaryAccountManager();

  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  EXPECT_TRUE(account_tracker()->GetAccountInfo(account_id).IsEmpty());
  CheckInitializeAccountInfoStateHistogram(
      PrimaryAccountManager::InitializeAccountInfoState::
          kEmptyAccountInfo_RestoreFailedNotSyncConsented);
}

TEST_F(PrimaryAccountManagerTest, RestoreFailedFeatureNotEnabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndDisableFeature(kRestorePrimaryAccountInfo);

  user_prefs_.SetString(prefs::kGoogleServicesLastSyncingUsername,
                        "user@gmail.com");
  user_prefs_.SetString(prefs::kGoogleServicesLastSyncingGaiaId, "gaia_id");
  CoreAccountId account_id =
      account_tracker()->PickAccountIdForAccount("gaia_id", "user@gmail.com");
  ASSERT_FALSE(account_id.empty());
  ASSERT_TRUE(account_tracker()->GetAccountInfo(account_id).IsEmpty());
  user_prefs_.SetString(prefs::kGoogleServicesAccountId, account_id.ToString());
  user_prefs_.SetBoolean(prefs::kGoogleServicesConsentedToSync, true);
  CreatePrimaryAccountManager();

  EXPECT_FALSE(manager_->HasPrimaryAccount(ConsentLevel::kSignin));
  EXPECT_TRUE(account_tracker()->GetAccountInfo(account_id).IsEmpty());
  CheckInitializeAccountInfoStateHistogram(
      PrimaryAccountManager::InitializeAccountInfoState::
          kEmptyAccountInfo_RestoreFailedAsRestoreFeatureIsDisabled);
}

TEST_F(PrimaryAccountManagerTest, ExplicitSigninPref) {
  CreatePrimaryAccountManager();
  CoreAccountId account_id =
      AddToAccountTracker("account_id", "user@gmail.com");

  ASSERT_FALSE(prefs()->GetBoolean(prefs::kExplicitBrowserSignin));

  // Simulate an explicit signin through the Chrome Signin Intercept bubble.
  manager_->SetPrimaryAccountInfo(
      account_tracker()->GetAccountInfo(account_id),
      signin::ConsentLevel::kSignin,
      signin_metrics::AccessPoint::ACCESS_POINT_CHROME_SIGNIN_INTERCEPT_BUBBLE);

  EXPECT_TRUE(prefs()->GetBoolean(prefs::kExplicitBrowserSignin));

#if !BUILDFLAG(IS_CHROMEOS_ASH)
  // Clearing signin.
  manager_->ClearPrimaryAccount(signin_metrics::ProfileSignout::kTest,
                                signin_metrics::SignoutDelete::kIgnoreMetric);

  EXPECT_FALSE(prefs()->GetBoolean(prefs::kExplicitBrowserSignin));
#endif
}

TEST_F(PrimaryAccountManagerTest, ImplicitSigninDoesNotSetExplicitSigninPref) {
  CreatePrimaryAccountManager();
  CoreAccountId account_id =
      AddToAccountTracker("account_id", "user@gmail.com");

  ASSERT_FALSE(prefs()->GetBoolean(prefs::kExplicitBrowserSignin));

  // Simulate an implicit signin through a web signin event.
  manager_->SetPrimaryAccountInfo(
      account_tracker()->GetAccountInfo(account_id),
      signin::ConsentLevel::kSignin,
      signin_metrics::AccessPoint::ACCESS_POINT_WEB_SIGNIN);

  EXPECT_FALSE(prefs()->GetBoolean(prefs::kExplicitBrowserSignin));
}

TEST_F(PrimaryAccountManagerTest, ExplicitSigninFollowedByUnknownSignin) {
  CreatePrimaryAccountManager();
  CoreAccountId account_id =
      AddToAccountTracker("account_id", "user@gmail.com");

  ASSERT_FALSE(prefs()->GetBoolean(prefs::kExplicitBrowserSignin));

  // Simulate an explicit signin through the Chrome Signin Intercept bubble.
  manager_->SetPrimaryAccountInfo(
      account_tracker()->GetAccountInfo(account_id),
      signin::ConsentLevel::kSignin,
      signin_metrics::AccessPoint::ACCESS_POINT_CHROME_SIGNIN_INTERCEPT_BUBBLE);

  EXPECT_TRUE(prefs()->GetBoolean(prefs::kExplicitBrowserSignin));

  // Creating a second account.
  CoreAccountId account_id2 =
      AddToAccountTracker("account_id2", "user2@gmail.com");

  // Simulating an sign in from an unknown access point without prior sign out.
  manager_->SetPrimaryAccountInfo(
      account_tracker()->GetAccountInfo(account_id2),
      signin::ConsentLevel::kSignin,
      signin_metrics::AccessPoint::ACCESS_POINT_UNKNOWN);

  // The explicit sign in pref should be cleared.
  EXPECT_FALSE(prefs()->GetBoolean(prefs::kExplicitBrowserSignin));
}
