// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/accessibility_main_handler.h"

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/screen_ai/screen_ai_install_state.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_web_ui.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/accessibility_features.h"
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)

namespace settings {

namespace {

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
const char kWebUIListenerCall[] = "cr.webUIListenerCallback";
const char kPdfOcrDownloadingProgressChangedEventName[] =
    "pdf-ocr-downloading-progress-changed";
const char kPdfOcrStateChangedEventName[] = "pdf-ocr-state-changed";

class TestScreenAIInstallState : public screen_ai::ScreenAIInstallState {
 public:
  TestScreenAIInstallState() = default;

  TestScreenAIInstallState(const TestScreenAIInstallState&) = delete;
  TestScreenAIInstallState& operator=(const TestScreenAIInstallState&) = delete;

  ~TestScreenAIInstallState() override = default;

  // screen_ai::ScreenAIInstallState:
  void SetLastUsageTime() override {}
  void DownloadComponentInternal() override {}
};

class TestAccessibilityMainHandler : public AccessibilityMainHandler {
 public:
  explicit TestAccessibilityMainHandler(
      TestScreenAIInstallState* screen_ai_install_state)
      : test_screen_ai_install_state_(screen_ai_install_state) {}

  ~TestAccessibilityMainHandler() override = default;

  // Make public for testing.
  using AccessibilityMainHandler::AllowJavascript;
  using AccessibilityMainHandler::set_web_ui;

 private:
  raw_ptr<TestScreenAIInstallState> test_screen_ai_install_state_ = nullptr;
  base::ScopedObservation<screen_ai::ScreenAIInstallState,
                          screen_ai::ScreenAIInstallState::Observer>
      component_ready_observer_{this};
};
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)

}  // namespace

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)
class AccessibilityMainHandlerPdfOcrTest : public testing::Test {
 public:
  AccessibilityMainHandlerPdfOcrTest() : features_({features::kPdfOcr}) {}

  AccessibilityMainHandlerPdfOcrTest(
      const AccessibilityMainHandlerPdfOcrTest&) = delete;
  AccessibilityMainHandlerPdfOcrTest& operator=(
      const AccessibilityMainHandlerPdfOcrTest&) = delete;

  ~AccessibilityMainHandlerPdfOcrTest() override = default;

  // testing::Test:
  void SetUp() override {
    TestingProfile::Builder builder;
    profile_ = builder.Build();
    web_contents_ = content::WebContents::Create(
        content::WebContents::CreateParams(profile_.get()));

    test_web_ui_ = std::make_unique<content::TestWebUI>();
    test_web_ui_->set_web_contents(web_contents_.get());

    test_screen_ai_install_state_ =
        std::make_unique<TestScreenAIInstallState>();

    handler_ = std::make_unique<TestAccessibilityMainHandler>(
        test_screen_ai_install_state_.get());
    handler_->set_web_ui(test_web_ui_.get());
    handler_->RegisterMessages();
    handler_->AllowJavascript();
    ASSERT_TRUE(handler_->IsJavascriptAllowed());

    // Run until idle so the handler picks up initial screen ai install state,
    // which is screen_ai::ScreenAIInstallState::State::kNotDownloaded.
    browser_task_environment_.RunUntilIdle();
  }

  void TearDown() override { handler_.reset(); }

  void ExpectCallToWebUI(const std::string& type,
                         const std::string& func_name,
                         const int expected_arg,
                         size_t call_count) {
    EXPECT_EQ(test_web_ui()->call_data().size(), call_count);
    // Get the last call data based on the given call_count value.
    const content::TestWebUI::CallData& call_data =
        *test_web_ui()->call_data()[call_count - 1];
    EXPECT_EQ(call_data.function_name(), type);
    EXPECT_EQ(call_data.arg1()->GetString(), func_name);
    EXPECT_EQ(call_data.arg2()->GetInt(), expected_arg);
  }

  void SimulateSetDownloadProgress(double progress) {
    test_screen_ai_install_state_->SetDownloadProgress(progress);
  }

  void SimulateSetState(screen_ai::ScreenAIInstallState::State state) {
    test_screen_ai_install_state_->SetStateForTesting(state);
  }

  content::TestWebUI* test_web_ui() const { return test_web_ui_.get(); }
  TestAccessibilityMainHandler* handler() const { return handler_.get(); }

 protected:
  base::test::ScopedFeatureList features_;
  content::BrowserTaskEnvironment browser_task_environment_;

  std::unique_ptr<TestAccessibilityMainHandler> handler_;
  std::unique_ptr<TestingProfile> profile_;
  std::unique_ptr<TestScreenAIInstallState> test_screen_ai_install_state_;
  std::unique_ptr<content::TestWebUI> test_web_ui_;
  std::unique_ptr<content::WebContents> web_contents_;
};

TEST_F(AccessibilityMainHandlerPdfOcrTest, MessageForScreenAIDownloadingState) {
  size_t call_data_count_before_call = test_web_ui()->call_data().size();

  screen_ai::ScreenAIInstallState::State state =
      screen_ai::ScreenAIInstallState::State::kDownloading;
  SimulateSetState(state);
  ExpectCallToWebUI(kWebUIListenerCall, kPdfOcrStateChangedEventName,
                    /*expected_arg=*/static_cast<int>(state),
                    /*call_count=*/call_data_count_before_call + 1u);
}

TEST_F(AccessibilityMainHandlerPdfOcrTest,
       MessageForScreenAIDownloadingProgress) {
  // State needs to be `kDownloading` before updating the download progress.
  size_t call_data_count_before_call = test_web_ui()->call_data().size();

  screen_ai::ScreenAIInstallState::State state =
      screen_ai::ScreenAIInstallState::State::kDownloading;
  SimulateSetState(state);
  ExpectCallToWebUI(kWebUIListenerCall, kPdfOcrStateChangedEventName,
                    /*expected_arg=*/static_cast<int>(state),
                    /*call_count=*/++call_data_count_before_call);

  const double progress = 0.3;
  SimulateSetDownloadProgress(progress);
  // `progress` is expected to be converted into percentage in a message.
  const int expected_progress_in_percentage = progress * 100;
  ExpectCallToWebUI(kWebUIListenerCall,
                    kPdfOcrDownloadingProgressChangedEventName,
                    /*expected_arg=*/expected_progress_in_percentage,
                    /*call_count=*/call_data_count_before_call + 1u);
}

TEST_F(AccessibilityMainHandlerPdfOcrTest, MessageForScreenAIDownloadedState) {
  size_t call_data_count_before_call = test_web_ui()->call_data().size();

  screen_ai::ScreenAIInstallState::State state =
      screen_ai::ScreenAIInstallState::State::kDownloaded;
  SimulateSetState(state);
  ExpectCallToWebUI(kWebUIListenerCall, kPdfOcrStateChangedEventName,
                    /*expected_arg=*/static_cast<int>(state),
                    /*call_count=*/call_data_count_before_call + 1u);
}

TEST_F(AccessibilityMainHandlerPdfOcrTest,
       MessageForScreenAIDownloadFailedState) {
  size_t call_data_count_before_call = test_web_ui()->call_data().size();

  screen_ai::ScreenAIInstallState::State state =
      screen_ai::ScreenAIInstallState::State::kFailed;
  SimulateSetState(state);
  ExpectCallToWebUI(kWebUIListenerCall, kPdfOcrStateChangedEventName,
                    /*expected_arg=*/static_cast<int>(state),
                    /*call_count=*/call_data_count_before_call + 1u);
}

TEST_F(AccessibilityMainHandlerPdfOcrTest, MessageForScreenAIReadyState) {
  size_t call_data_count_before_call = test_web_ui()->call_data().size();

  screen_ai::ScreenAIInstallState::State state =
      screen_ai::ScreenAIInstallState::State::kReady;
  SimulateSetState(state);
  ExpectCallToWebUI(kWebUIListenerCall, kPdfOcrStateChangedEventName,
                    /*expected_arg=*/static_cast<int>(state),
                    /*call_count=*/call_data_count_before_call + 1u);
}

TEST_F(AccessibilityMainHandlerPdfOcrTest,
       MessageForScreenAINotDownloadedState) {
  size_t call_data_count_before_call = test_web_ui()->call_data().size();

  // Either `kReady` or `kFailed` needs to be set for testing `kNotDownloaded`.
  screen_ai::ScreenAIInstallState::State state =
      screen_ai::ScreenAIInstallState::State::kReady;
  SimulateSetState(state);
  ExpectCallToWebUI(kWebUIListenerCall, kPdfOcrStateChangedEventName,
                    /*expected_arg=*/static_cast<int>(state),
                    /*call_count=*/++call_data_count_before_call);

  state = screen_ai::ScreenAIInstallState::State::kNotDownloaded;
  SimulateSetState(state);
  ExpectCallToWebUI(kWebUIListenerCall, kPdfOcrStateChangedEventName,
                    /*expected_arg=*/static_cast<int>(state),
                    /*call_count=*/call_data_count_before_call + 1u);
}
#endif  // BUILDFLAG(IS_WIN) || BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC)

}  // namespace settings
