// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/content/renderer/form_tracker.h"

#include "components/autofill/content/renderer/autofill_agent_test_api.h"
#include "components/autofill/content/renderer/autofill_renderer_test.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/web/web_form_control_element.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace autofill {
namespace {

class MockFormTrackerObserver : public FormTracker::Observer {
 public:
  // Not a mock method so that gmock ignores calls to this method.
  void OnProvisionallySaveForm(const blink::WebFormElement&,
                               const blink::WebFormControlElement&,
                               SaveFormReason) override {}

  MOCK_METHOD0(OnProbablyFormSubmitted, void());
  MOCK_METHOD1(OnFormSubmitted, void(const blink::WebFormElement&));
  MOCK_METHOD1(OnInferredFormSubmission, void(mojom::SubmissionSource));
};

class FormTrackerTest : public test::AutofillRendererTest {
 public:
  blink::WebFormControlElement GetFormControlById(const std::string& id) {
    return GetMainFrame()
        ->GetDocument()
        .GetElementById(blink::WebString::FromUTF8(id))
        .DynamicTo<blink::WebFormControlElement>();
  }
};

// Check that submission is detected on a page with no <form> when in sequence:
// 1) User types into a field.
// 2) Page does an XHR.
// 3) Page hides all of the inputs.
TEST_F(FormTrackerTest, FormlessXHRThenHide) {
  LoadHTML("<!DOCTYPE HTML><input id='input1'><input id='input2'/>");

  blink::WebFormControlElement input1 = GetFormControlById("input1");

  testing::StrictMock<MockFormTrackerObserver> observer;
  test_api(autofill_agent()).form_tracker().AddObserver(&observer);

  GetMainFrame()->NotifyUserActivation(
      blink::mojom::UserActivationNotificationType::kTest);
  ExecuteJavaScriptForTests("document.getElementById('input1').focus();");
  test_api(autofill_agent()).form_tracker().TextFieldDidChange(input1);

  task_environment_.RunUntilIdle();

  test_api(autofill_agent()).form_tracker().AjaxSucceeded();
  task_environment_.RunUntilIdle();
  // FormTracker should not think there is a submission because the <input>s are
  // still visible.

  // FormTracker should detect a submission after the <input>s are hidden.
  EXPECT_CALL(observer, OnInferredFormSubmission).Times(1);
  ExecuteJavaScriptForTests(
      "document.getElementById('input1').style.display = 'none';"
      "document.getElementById('input2').style.display = 'none';");
}

// Check that submission is detected on a page with no <form> when in sequence:
// 1) User types into a field.
// 2) Page hides all of the inputs.
// 3) Page does an XHR.
TEST_F(FormTrackerTest, FormlessHideThenXhr) {
  LoadHTML("<!DOCTYPE HTML><input id='input1'><input id='input2'/>");

  blink::WebFormControlElement input1 = GetFormControlById("input1");

  testing::StrictMock<MockFormTrackerObserver> observer;
  test_api(autofill_agent()).form_tracker().AddObserver(&observer);

  GetMainFrame()->NotifyUserActivation(
      blink::mojom::UserActivationNotificationType::kTest);
  ExecuteJavaScriptForTests("document.getElementById('input1').focus();");
  test_api(autofill_agent()).form_tracker().TextFieldDidChange(input1);
  task_environment_.RunUntilIdle();

  ExecuteJavaScriptForTests(
      "document.getElementById('input1').style.display = 'none';"
      "document.getElementById('input2').style.display = 'none';");
  task_environment_.RunUntilIdle();
  // FormTracker should not think there is a submission because the page has not
  // done any XHRs.

  // FormTracker should detect a submission when the XHR succeeds.
  EXPECT_CALL(observer, OnInferredFormSubmission).Times(1);
  test_api(autofill_agent()).form_tracker().AjaxSucceeded();
  task_environment_.RunUntilIdle();
}

}  // anonymous namespace
}  // namespace autofill
