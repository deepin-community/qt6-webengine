// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/memory/raw_ptr.h"
#include "base/numerics/safe_conversions.h"
#include "base/observer_list.h"
#include "base/strings/string_piece.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/api/passwords_private/passwords_private_delegate.h"
#include "chrome/browser/extensions/api/passwords_private/passwords_private_delegate_factory.h"
#include "chrome/browser/extensions/api/passwords_private/passwords_private_event_router.h"
#include "chrome/browser/extensions/api/passwords_private/passwords_private_event_router_factory.h"
#include "chrome/browser/extensions/api/passwords_private/test_passwords_private_delegate.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/common/extensions/api/passwords_private.h"
#include "chrome/test/base/testing_profile.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/mock_configuration_policy_provider.h"
#include "components/policy/core/common/policy_map.h"
#include "components/policy/core/common/policy_types.h"
#include "components/policy/policy_constants.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/test_utils.h"
#include "extensions/common/switches.h"
#include "ui/base/l10n/time_format.h"

using policy::PolicyMap;
using testing::NiceMock;

namespace extensions {

namespace {

class PasswordsPrivateApiTest : public ExtensionApiTest {
 public:
  PasswordsPrivateApiTest() = default;

  PasswordsPrivateApiTest(const PasswordsPrivateApiTest&) = delete;
  PasswordsPrivateApiTest& operator=(const PasswordsPrivateApiTest&) = delete;

  ~PasswordsPrivateApiTest() override = default;

  void SetUpOnMainThread() override {
    ExtensionApiTest::SetUpOnMainThread();
    test_delegate_ = base::MakeRefCounted<TestPasswordsPrivateDelegate>();
    PasswordsPrivateDelegateFactory::GetInstance()->SetTestingFactory(
        profile(), base::BindRepeating(&PasswordsPrivateApiTest::Create,
                                       base::Unretained(this)));
    test_delegate_->SetProfile(profile());
    content::RunAllPendingInMessageLoop();
  }

  std::unique_ptr<KeyedService> Create(content::BrowserContext* context) {
    return std::make_unique<PasswordsPrivateDelegateProxy>(context,
                                                           test_delegate_);
  }

  void SetUpInProcessBrowserTestFixture() override {
    ExtensionApiTest::SetUpInProcessBrowserTestFixture();
    policy_provider_.SetDefaultReturns(
        /*is_initialization_complete_return=*/true,
        /*is_first_policy_load_complete_return=*/true);
    policy::BrowserPolicyConnector::SetPolicyProviderForTesting(
        &policy_provider_);
  }

  void UpdateProviderPolicy(const PolicyMap& policy) {
    PolicyMap policy_with_defaults = policy.Clone();
#if BUILDFLAG(IS_CHROMEOS)
    SetEnterpriseUsersDefaults(&policy_with_defaults);
#endif
    policy_provider_.UpdateChromePolicy(policy_with_defaults);
  }

 protected:
  NiceMock<policy::MockConfigurationPolicyProvider> policy_provider_;

  bool RunPasswordsSubtest(const std::string& subtest) {
    const std::string extension_url = "main.html?" + subtest;
    return RunExtensionTest("passwords_private",
                            {.extension_url = extension_url.c_str()},
                            {.load_as_component = true});
  }

  bool importPasswordsWasTriggered() {
    return test_delegate_->ImportPasswordsTriggered();
  }

  bool fetch_family_members_was_triggered() {
    return test_delegate_->FetchFamilyMembersTriggered();
  }

  bool share_password_was_triggered() {
    return test_delegate_->SharePasswordTriggered();
  }

  bool continue_import_was_triggered() {
    return test_delegate_->ContinueImportTriggered();
  }

  bool reset_importer_was_triggered() {
    return test_delegate_->ResetImporterTriggered();
  }

  bool exportPasswordsWasTriggered() {
    return test_delegate_->ExportPasswordsTriggered();
  }

  bool start_password_check_triggered() {
    return test_delegate_->StartPasswordCheckTriggered();
  }

  void set_start_password_check_state(
      password_manager::BulkLeakCheckService::State state) {
    test_delegate_->SetStartPasswordCheckState(state);
  }

  bool IsOptedInForAccountStorage() {
    return test_delegate_->IsOptedInForAccountStorage();
  }

  void SetOptedInForAccountStorage(bool opted_in) {
    test_delegate_->SetAccountStorageOptIn(opted_in, nullptr);
  }

  void ResetPlaintextPassword() { test_delegate_->ResetPlaintextPassword(); }

  void AddCompromisedCredential(int id) {
    test_delegate_->AddCompromisedCredential(id);
  }

  void SetIsAccountStoreDefault(bool is_default) {
    test_delegate_->SetIsAccountStoreDefault(is_default);
  }

  const std::vector<int>& last_moved_passwords() const {
    return test_delegate_->last_moved_passwords();
  }

  bool get_authenticator_interaction_status() const {
    return test_delegate_->get_authenticator_interaction_status();
  }

  bool get_add_shortcut_dialog_shown() const {
    return test_delegate_->get_add_shortcut_dialog_shown();
  }

  bool get_exported_file_shown_in_shell() const {
    return test_delegate_->get_exported_file_shown_in_shell();
  }

 private:
  scoped_refptr<TestPasswordsPrivateDelegate> test_delegate_;
};

}  // namespace

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       IsAccountStoreDefaultWhenFalse) {
  EXPECT_TRUE(RunPasswordsSubtest("isAccountStoreDefaultWhenFalse"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, IsAccountStoreDefaultWhenTrue) {
  SetIsAccountStoreDefault(true);
  EXPECT_TRUE(RunPasswordsSubtest("isAccountStoreDefaultWhenTrue")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       GetUrlCollectionWhenUrlValidSucceeds) {
  EXPECT_TRUE(RunPasswordsSubtest("getUrlCollectionWhenUrlValidSucceeds"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       GetUrlCollectionWhenUrlInvalidFails) {
  EXPECT_TRUE(RunPasswordsSubtest("getUrlCollectionWhenUrlInvalidFails"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       AddPasswordWhenOperationSucceeds) {
  EXPECT_TRUE(RunPasswordsSubtest("addPasswordWhenOperationSucceeds"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, AddPasswordWhenOperationFails) {
  EXPECT_TRUE(RunPasswordsSubtest("addPasswordWhenOperationFails")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       AddPasswordOperationDisabledByPolicy) {
  // Set kPasswordManagerEnabled policy which corresponds to
  // password_manager::prefs::kCredentialsEnableService.
  PolicyMap policies;
  policies.Set(policy::key::kPasswordManagerEnabled,
               policy::POLICY_LEVEL_MANDATORY, policy::POLICY_SCOPE_USER,
               policy::POLICY_SOURCE_CLOUD, base::Value(false), nullptr);
  UpdateProviderPolicy(policies);

  EXPECT_TRUE(RunPasswordsSubtest("addPasswordOperationDisabledByPolicy"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       ImportPasswordsOperationDisabledByPolicy) {
  // Set kPasswordManagerEnabled policy which corresponds to
  // password_manager::prefs::kCredentialsEnableService.
  PolicyMap policies;
  policies.Set(policy::key::kPasswordManagerEnabled,
               policy::POLICY_LEVEL_MANDATORY, policy::POLICY_SCOPE_USER,
               policy::POLICY_SOURCE_CLOUD, base::Value(false), nullptr);
  UpdateProviderPolicy(policies);

  EXPECT_TRUE(RunPasswordsSubtest("importPasswordsOperationDisabledByPolicy"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       ChangeCredentialChangePassword) {
  EXPECT_TRUE(RunPasswordsSubtest("changeCredentialChangePassword"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, ChangeCredentialChangePasskey) {
  EXPECT_TRUE(RunPasswordsSubtest("changeCredentialChangePasskey")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, ChangeCredentialNotFound) {
  EXPECT_TRUE(RunPasswordsSubtest("changeCredentialNotFound")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       RemoveAndUndoRemoveSavedPassword) {
  EXPECT_TRUE(RunPasswordsSubtest("removeAndUndoRemoveSavedPassword"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       RemoveAndUndoRemovePasswordException) {
  EXPECT_TRUE(RunPasswordsSubtest("removeAndUndoRemovePasswordException"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, RemovePasskey) {
  EXPECT_TRUE(RunPasswordsSubtest("removePasskey")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, RequestPlaintextPassword) {
  EXPECT_TRUE(RunPasswordsSubtest("requestPlaintextPassword")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, RequestPlaintextPasswordFails) {
  ResetPlaintextPassword();
  EXPECT_TRUE(RunPasswordsSubtest("requestPlaintextPasswordFails")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, RequestCredentialsDetails) {
  EXPECT_TRUE(RunPasswordsSubtest("requestCredentialsDetails")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       RequestCredentialsDetailsFails) {
  ResetPlaintextPassword();
  EXPECT_TRUE(RunPasswordsSubtest("requestCredentialsDetailsFails"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, GetSavedPasswordList) {
  EXPECT_TRUE(RunPasswordsSubtest("getSavedPasswordList")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, GetPasswordExceptionList) {
  EXPECT_TRUE(RunPasswordsSubtest("getPasswordExceptionList")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, FetchFamilyMembers) {
  EXPECT_FALSE(fetch_family_members_was_triggered());
  EXPECT_TRUE(RunPasswordsSubtest("fetchFamilyMembers")) << message_;
  EXPECT_TRUE(fetch_family_members_was_triggered());
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, SharePassword) {
  EXPECT_FALSE(share_password_was_triggered());
  EXPECT_TRUE(RunPasswordsSubtest("sharePassword")) << message_;
  EXPECT_TRUE(share_password_was_triggered());
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, ImportPasswords) {
  EXPECT_FALSE(importPasswordsWasTriggered());
  EXPECT_TRUE(RunPasswordsSubtest("importPasswords")) << message_;
  EXPECT_TRUE(importPasswordsWasTriggered());
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, ContinueImport) {
  EXPECT_FALSE(continue_import_was_triggered());
  EXPECT_TRUE(RunPasswordsSubtest("continueImport")) << message_;
  EXPECT_TRUE(continue_import_was_triggered());
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, ResetImporter) {
  EXPECT_FALSE(reset_importer_was_triggered());
  EXPECT_TRUE(RunPasswordsSubtest("resetImporter")) << message_;
  EXPECT_TRUE(reset_importer_was_triggered());
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, ExportPasswords) {
  EXPECT_FALSE(exportPasswordsWasTriggered());
  EXPECT_TRUE(RunPasswordsSubtest("exportPasswords")) << message_;
  EXPECT_TRUE(exportPasswordsWasTriggered());
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, RequestExportProgressStatus) {
  EXPECT_TRUE(RunPasswordsSubtest("requestExportProgressStatus")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, IsNotOptedInForAccountStorage) {
  EXPECT_TRUE(RunPasswordsSubtest("isNotOptedInForAccountStorage")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, IsOptedInForAccountStorage) {
  SetOptedInForAccountStorage(true);
  EXPECT_TRUE(RunPasswordsSubtest("isOptedInForAccountStorage")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, GetInsecureCredentials) {
  EXPECT_TRUE(RunPasswordsSubtest("getInsecureCredentials")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, OptInForAccountStorage) {
  SetOptedInForAccountStorage(false);
  EXPECT_TRUE(RunPasswordsSubtest("optInForAccountStorage")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, OptOutForAccountStorage) {
  SetOptedInForAccountStorage(true);
  EXPECT_TRUE(RunPasswordsSubtest("optOutForAccountStorage")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, MuteInsecureCredentialFails) {
  EXPECT_TRUE(RunPasswordsSubtest("muteInsecureCredentialFails")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       UnmuteInsecureCredentialSucceeds) {
  AddCompromisedCredential(0);
  EXPECT_TRUE(RunPasswordsSubtest("unmuteInsecureCredentialSucceeds"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, UnmuteInsecureCredentialFails) {
  EXPECT_TRUE(RunPasswordsSubtest("unmuteInsecureCredentialFails")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, StartPasswordCheck) {
  set_start_password_check_state(
      password_manager::BulkLeakCheckService::State::kRunning);
  EXPECT_FALSE(start_password_check_triggered());
  EXPECT_TRUE(RunPasswordsSubtest("startPasswordCheck")) << message_;
  EXPECT_TRUE(start_password_check_triggered());
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, StartPasswordCheckFailed) {
  set_start_password_check_state(
      password_manager::BulkLeakCheckService::State::kIdle);
  EXPECT_FALSE(start_password_check_triggered());
  EXPECT_TRUE(RunPasswordsSubtest("startPasswordCheckFailed")) << message_;
  EXPECT_TRUE(start_password_check_triggered());
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, GetPasswordCheckStatus) {
  EXPECT_TRUE(RunPasswordsSubtest("getPasswordCheckStatus")) << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, MovePasswordsToAccount) {
  EXPECT_TRUE(last_moved_passwords().empty());
  EXPECT_TRUE(RunPasswordsSubtest("movePasswordsToAccount")) << message_;
  EXPECT_EQ(42, last_moved_passwords()[0]);
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, ExtendAuthValidity) {
  EXPECT_FALSE(get_authenticator_interaction_status());
  EXPECT_TRUE(RunPasswordsSubtest("extendAuthValidity")) << message_;
  EXPECT_TRUE(get_authenticator_interaction_status());
}

#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       SwitchBiometricAuthBeforeFillingState) {
  EXPECT_FALSE(get_authenticator_interaction_status());
  EXPECT_TRUE(RunPasswordsSubtest("switchBiometricAuthBeforeFillingState"))
      << message_;
  EXPECT_TRUE(get_authenticator_interaction_status());
}
#endif  // BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, AddShortcut) {
  EXPECT_FALSE(get_add_shortcut_dialog_shown());
  EXPECT_TRUE(RunPasswordsSubtest("showAddShortcutDialog")) << message_;
  EXPECT_TRUE(get_add_shortcut_dialog_shown());
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, GetCredentialGroups) {
  EXPECT_TRUE(RunPasswordsSubtest("getCredentialGroups"));
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest,
                       GetCredentialsWithReusedPassword) {
  EXPECT_TRUE(RunPasswordsSubtest("getCredentialsWithReusedPassword"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(PasswordsPrivateApiTest, ShowExportedFileInShell) {
  EXPECT_FALSE(get_exported_file_shown_in_shell());
  EXPECT_TRUE(RunPasswordsSubtest("showExportedFileInShell")) << message_;
  EXPECT_TRUE(get_exported_file_shown_in_shell());
}

}  // namespace extensions
