// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CONTENT_RENDERER_AUTOFILL_AGENT_H_
#define COMPONENTS_AUTOFILL_CONTENT_RENDERER_AUTOFILL_AGENT_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/types/strong_alias.h"
#include "components/autofill/content/common/mojom/autofill_agent.mojom.h"
#include "components/autofill/content/common/mojom/autofill_driver.mojom.h"
#include "components/autofill/content/renderer/form_autofill_util.h"
#include "components/autofill/content/renderer/form_tracker.h"
#include "components/autofill/core/common/mojom/autofill_types.mojom-shared.h"
#include "content/public/renderer/render_frame_observer.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_registry.h"
#include "third_party/blink/public/web/web_autofill_client.h"
#include "third_party/blink/public/web/web_autofill_state.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_form_control_element.h"
#include "third_party/blink/public/web/web_form_element.h"
#include "third_party/blink/public/web/web_input_element.h"
#include "ui/accessibility/ax_mode.h"

namespace blink {
class WebNode;
class WebView;
class WebFormControlElement;
class WebFormElement;
}  // namespace blink

namespace autofill {

struct FormData;
class FormCache;
class PasswordAutofillAgent;
class PasswordGenerationAgent;
class FieldDataManager;

// AutofillAgent deals with Autofill related communications between WebKit and
// the browser.
//
// Each AutofillAgent is associated with exactly one RenderFrame and
// communicates with exactly one ContentAutofillDriver throughout its entire
// lifetime.
//
// AutofillAgent is deleted asynchronously because it may itself take action
// that (via JavaScript) causes the associated RenderFrame's deletion.
// AutofillAgent is pending deletion between OnDestruct() and ~AutofillAgent().
// To handle this state, care must be taken to check for nullptrs:
// - `unsafe_autofill_driver()`
// - `unsafe_render_frame()`
// - `form_cache_`
//
// This RenderFrame owns all forms and fields in the renderer-browser
// communication:
// - AutofillAgent may assume that forms and fields received in the
//   mojom::AutofillAgent events are owned by that RenderFrame.
// - Conversely, the forms and fields which AutofillAgent passes to
//   mojom::AutofillDriver events must be owned by that RenderFrame.
//
// Note that Autofill encompasses:
// - single text field suggestions, that we usually refer to as Autocomplete,
// - password form fill, referred to as Password Autofill, and
// - entire form fill based on one field entry, referred to as Form Autofill.
class AutofillAgent : public content::RenderFrameObserver,
                      public FormTracker::Observer,
                      public blink::WebAutofillClient,
                      public mojom::AutofillAgent {
 public:
  static constexpr base::TimeDelta kFormsSeenThrottle = base::Milliseconds(100);

  using UsesKeyboardAccessoryForSuggestions =
      base::StrongAlias<class UsesKeyboardAccessoryForSuggestionsTag, bool>;
  using ExtractAllDatalists =
      base::StrongAlias<class ExtractAllDatalistsTag, bool>;

  struct Config {
    // Is true iff the platform doesn't show any popups but renders the same
    // information in or near the keyboard instead.
    UsesKeyboardAccessoryForSuggestions uses_keyboard_accessory_for_suggestions{
        BUILDFLAG(IS_ANDROID)};

    // Controls whether or not all datalists shall be extracted into
    // FormFieldData. This feature is enabled when all datalists (instead of
    // only the focused one) shall be extracted and sent to the Android Autofill
    // service when the autofill session is created.
    ExtractAllDatalists extract_all_datalists{false};
  };

  // PasswordAutofillAgent is guaranteed to outlive AutofillAgent.
  // PasswordGenerationAgent and AutofillAssistantAgent may be nullptr. If they
  // are not, then they are also guaranteed to outlive AutofillAgent.
  AutofillAgent(
      content::RenderFrame* render_frame,
      Config config,
      std::unique_ptr<PasswordAutofillAgent> password_autofill_agent,
      std::unique_ptr<PasswordGenerationAgent> password_generation_agent,
      blink::AssociatedInterfaceRegistry* registry);

  AutofillAgent(const AutofillAgent&) = delete;
  AutofillAgent& operator=(const AutofillAgent&) = delete;

  ~AutofillAgent() override;

  void BindPendingReceiver(
      mojo::PendingAssociatedReceiver<mojom::AutofillAgent> pending_receiver);

  // Callers must not store the returned value longer than a function scope.
  // unsafe_autofill_driver() is nullptr if unsafe_render_frame() is nullptr and
  // the `autofill_driver_` has not been bound yet.
  mojom::AutofillDriver* unsafe_autofill_driver();
  mojom::PasswordManagerDriver& GetPasswordManagerDriver();

  // mojom::AutofillAgent:
  void TriggerFormExtraction() override;
  void TriggerFormExtractionWithResponse(
      base::OnceCallback<void(bool)> callback) override;
  void ApplyFormAction(mojom::ActionType action_type,
                       mojom::ActionPersistence action_persistence,
                       FormRendererId form_renderer_id,
                       const std::vector<FormFieldData>& fields) override;
  void ApplyFieldAction(mojom::ActionPersistence action_persistence,
                        mojom::TextReplacement text_replacement,
                        FieldRendererId field_id,
                        const std::u16string& value) override;
  void ExtractForm(FormRendererId form,
                   base::OnceCallback<void(const std::optional<FormData>&)>
                       callback) override;
  void FieldTypePredictionsAvailable(
      const std::vector<FormDataPredictions>& forms) override;
  void ClearSection() override;
  // Besides cases that "actually" clear the form, this function needs to be
  // called before all filling operations. This is because filled fields are no
  // longer considered previewed - and any state tied to the preview needs to
  // be reset.
  void ClearPreviewedForm() override;
  void TriggerSuggestions(
      FieldRendererId field_id,
      AutofillSuggestionTriggerSource trigger_source) override;
  void SetSuggestionAvailability(
      FieldRendererId field_id,
      mojom::AutofillSuggestionAvailability suggestion_availability) override;
  void AcceptDataListSuggestion(FieldRendererId field_id,
                                const std::u16string& suggested_value) override;
  void PreviewPasswordSuggestion(const std::u16string& username,
                                 const std::u16string& password) override;
  void PreviewPasswordGenerationSuggestion(
      const std::u16string& password) override;
  void SetUserGestureRequired(bool required) override;
  void SetSecureContextRequired(bool required) override;
  void SetFocusRequiresScroll(bool require) override;
  void SetQueryPasswordSuggestion(bool required) override;
  void EnableHeavyFormDataScraping() override;
  void GetPotentialLastFourCombinationsForStandaloneCvc(
      base::OnceCallback<void(const std::vector<std::string>&)>
          potential_matches) override;

  void FormControlElementClicked(const blink::WebFormControlElement& element);

  base::WeakPtr<AutofillAgent> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  // FormTracker::Observer
  void OnProvisionallySaveForm(const blink::WebFormElement& form,
                               const blink::WebFormControlElement& element,
                               SaveFormReason source) override;
  void OnProbablyFormSubmitted() override;
  void OnFormSubmitted(const blink::WebFormElement& form) override;
  void OnInferredFormSubmission(mojom::SubmissionSource source) override;

  void AddFormObserver(Observer* observer);
  void RemoveFormObserver(Observer* observer);

  // Instructs `form_tracker_` to track the autofilled `element`.
  void TrackAutofilledElement(const blink::WebFormControlElement& element);

  // Function that should be called whenever the value of |element| changes due
  // to user input. This is separate from OnTextFieldDidChange() as that
  // function may trigger UI and should only be called when other UI won't be
  // shown.
  void UpdateStateForTextChange(const blink::WebFormControlElement& element,
                                FieldPropertiesFlags flag);

  bool is_heavy_form_data_scraping_enabled() {
    return is_heavy_form_data_scraping_enabled_;
  }

  bool IsPrerendering() const;

  blink::WebFormControlElement focused_element() const {
    return last_queried_element_.GetField();
  }

  FieldDataManager& field_data_manager() const {
    return *field_data_manager_.get();
  }

 protected:
  // blink::WebAutofillClient:

  // Signals from blink that a form related element changed dynamically,
  // passing the changed element as well as the type of the change.
  // TODO(crbug.com/1483242): Fire the signal for elements that become hidden.
  void DidChangeFormRelatedElementDynamically(
      const blink::WebElement&,
      blink::WebFormRelatedChangeType) override;

 private:
  class DeferringAutofillDriver;
  friend class AutofillAgentTestApi;

  // This class ensures that the driver will only receive notifications only
  // when a focused field or its type (FocusedFieldType) change.
  class FocusStateNotifier {
   public:
    // Creates a new notifier that uses the agent which owns it to access the
    // real driver implementation.
    explicit FocusStateNotifier(AutofillAgent* agent);

    FocusStateNotifier(const FocusStateNotifier&) = delete;
    FocusStateNotifier& operator=(const FocusStateNotifier&) = delete;

    ~FocusStateNotifier();

    // Notifies the driver about focusing the node.
    void FocusedInputChanged(const blink::WebNode& node);
    // Notifies the password manager driver about removing the focus from the
    // currently focused node (with no setting it to a new one).
    void ResetFocus();

   private:
    mojom::FocusedFieldType GetFieldType(
        const blink::WebFormControlElement& node);
    void NotifyIfChanged(mojom::FocusedFieldType new_focused_field_type,
                         FieldRendererId new_focused_field_id);

    FieldRendererId focused_field_id_;
    mojom::FocusedFieldType focused_field_type_ =
        mojom::FocusedFieldType::kUnknown;
    const raw_ref<AutofillAgent, ExperimentalRenderer> agent_;
  };

  // content::RenderFrameObserver:
  void DidCommitProvisionalLoad(ui::PageTransition transition) override;
  void DidDispatchDOMContentLoadedEvent() override;
  void DidChangeScrollOffset() override;
  void FocusedElementChanged(const blink::WebElement& element) override;
  void AccessibilityModeChanged(const ui::AXMode& mode) override;
  void OnDestruct() override;

  // The RenderFrame* is nullptr while the AutofillAgent is pending deletion,
  // between OnDestruct() and ~AutofillAgent().
  content::RenderFrame* unsafe_render_frame() const {
    return content::RenderFrameObserver::render_frame();
  }

  // Use unsafe_render_frame() instead.
  template <typename T = int>
  content::RenderFrame* render_frame(T* = 0) const {
    static_assert(
        std::is_void_v<T>,
        "Beware that the RenderFrame may become nullptr by OnDestruct() "
        "because AutofillAgent destructs itself asynchronously. Use "
        "unsafe_render_frame() instead and make test that it is non-nullptr.");
  }

  // Fires Mojo messages for a given form submission.
  void FireHostSubmitEvents(const FormData& form_data,
                            bool known_success,
                            mojom::SubmissionSource source);

  // blink::WebAutofillClient:
  void TextFieldDidEndEditing(const blink::WebInputElement& element) override;
  void TextFieldDidChange(const blink::WebFormControlElement& element) override;
  void ContentEditableDidChange(const blink::WebElement& element) override;
  void TextFieldDidReceiveKeyDown(
      const blink::WebInputElement& element,
      const blink::WebKeyboardEvent& event) override;
  void OpenTextDataListChooser(const blink::WebInputElement& element) override;
  void DataListOptionsChanged(const blink::WebInputElement& element) override;
  void UserGestureObserved() override;
  void AjaxSucceeded() override;
  void JavaScriptChangedAutofilledValue(
      const blink::WebFormControlElement& element,
      const blink::WebString& old_value) override;
  void DidCompleteFocusChangeInFrame() override;
  void DidReceiveLeftMouseDownOrGestureTapInNode(
      const blink::WebNode& node) override;
  void SelectOrSelectListFieldOptionsChanged(
      const blink::WebFormControlElement& element) override;
  void SelectControlDidChange(
      const blink::WebFormControlElement& element) override;
  bool ShouldSuppressKeyboard(
      const blink::WebFormControlElement& element) override;
  void FormElementReset(const blink::WebFormElement& form) override;
  void PasswordFieldReset(const blink::WebInputElement& element) override;
  std::vector<blink::WebAutofillClient::FormIssue>
  ProccessFormsAndReturnIssues() override;

  void HandleFocusChangeComplete(bool focused_node_was_last_clicked);
  void SendFocusedInputChangedNotificationToBrowser(
      const blink::WebElement& node);

  void OnTextFieldDidChange(const blink::WebFormControlElement& element);
  void DidChangeScrollOffsetImpl(const blink::WebFormControlElement& element);

  // Shows the autofill suggestions for |element|. This call is asynchronous
  // and may or may not lead to the showing of a suggestion popup (no popup is
  // shown if there are no available suggestions).
  void ShowSuggestions(const blink::WebFormControlElement& element,
                       AutofillSuggestionTriggerSource trigger_source);

  // Queries the browser for Autocomplete and Autofill suggestions for the given
  // |element|.
  void QueryAutofillSuggestions(const blink::WebFormControlElement& element,
                                AutofillSuggestionTriggerSource trigger_source);

  // Sets the selected value of the the field identified by |field_id| to
  // |suggested_value|.
  void DoAcceptDataListSuggestion(FieldRendererId field_id,
                                  const std::u16string& suggested_value);

  // Set `element` to display the given `value`.
  void DoFillFieldWithValue(std::u16string_view value,
                            blink::WebFormControlElement& element,
                            blink::WebAutofillState autofill_state);

  // Notifies the AutofillDriver in the browser process of new and/or removed
  // forms, modulo throttling.
  //
  // Throttling means that the actual work -- that is, extracting the forms and
  // invoking AutofillDriver::FormsSeen() -- is delayed by (at least) 100 ms.
  // All subsequent calls within the next (at least) 100 ms return early.
  //
  // Calls `callback(true)` asynchronously after the timer is completed.
  // Otherwise, calls `callback(false)` immediately.
  void ExtractForms(base::OneShotTimer& timer,
                    base::OnceCallback<void(bool)> callback);

  // This function can be implemented through the one above, but it exists to
  // avoid memory allocation for the OnceCallback state. Allocation and
  // destruction of this callback in the hot path (when timer is already
  // running) is expensive.
  void ExtractFormsForPasswordAutofillAgent(base::OneShotTimer& timer);

  void ExtractFormsUnthrottled(base::OnceCallback<void(bool)> callback);

  // Hides any currently showing Autofill popup.
  void HidePopup();

  // Attempt to get submitted FormData from last_interacted_form_ or
  // provisionally_saved_form_, return the form in question if found, and
  // std::nullopt otherwise.
  std::optional<FormData> GetSubmittedForm() const;

  // Pushes the value of GetSubmittedForm() to the AutofillDriver.
  void SendPotentiallySubmittedFormToBrowser();

  void ResetLastInteractedElements();
  void UpdateLastInteractedForm(const blink::WebFormElement& form);

  // Called when current form is no longer submittable, submitted_forms_ is
  // cleared in this method.
  void OnFormNoLongerSubmittable();

  // Amends the given `extract_options` with datalists if required.
  DenseSet<form_util::ExtractOption> MaybeExtractDatalist(
      DenseSet<form_util::ExtractOption> extract_options);

  // Helpers for SelectOrSelectListFieldOptionsChanged() and
  // DataListOptionsChanged(), which get called after a timer that is restarted
  // when another event of the same type started.
  void BatchSelectOrSelectListOptionChange(
      const blink::WebFormControlElement& element);
  void BatchDataListOptionChange(const blink::WebFormControlElement& element);

  // Stores immutable configuration this agent was created with. It contains
  // features and settings that are available for the lifetime of this class.
  const Config config_;

  // Return the next web node of `current_node` in the DOM. `next` determines
  // the direction to traverse in.
  blink::WebNode NextWebNode(const blink::WebNode& current_node, bool next);

  // Contains the form of the document. Does not survive navigation and is
  // reset when the AutofillAgent is pending deletion.
  std::unique_ptr<FormCache> form_cache_;

  std::unique_ptr<PasswordAutofillAgent> password_autofill_agent_;
  std::unique_ptr<PasswordGenerationAgent> password_generation_agent_;

  // The element corresponding to the last request sent for form field Autofill.
  FieldRef last_queried_element_;

  // List of elements that are currently being previewed, along with their
  // autofill state before the preview.
  std::vector<std::pair<FieldRef, blink::WebAutofillState>> previewed_elements_;

  // Records the last autofill action (Fill or Undo) done by the agent. Used in
  // ClearPreviewedForm to get the default state of previewed fields
  // post-clearing.
  mojom::ActionType last_action_type_ = mojom::ActionType::kFill;

  // Last form which was interacted with by the user.
  FormRef last_interacted_form_;

  // When dealing with an unowned form, we keep track of the unowned fields
  // the user has modified so we can determine when submission occurs.
  // An additional sufficient condition for the form submission detection is
  // that the form has been autofilled.
  std::set<FieldRendererId> formless_elements_user_edited_;
  bool formless_elements_were_autofilled_ = false;

  // The form the user interacted with last. It is used if last_interacted_form_
  // or a formless form can't be converted to FormData at the time of form
  // submission (e.g. because they have been removed from the DOM).
  std::optional<FormData> provisionally_saved_form_;

  // Keeps track of the forms for which form submitted event has been sent to
  // AutofillDriver. We use it to avoid fire duplicated submission event when
  // WILL_SEND_SUBMIT_EVENT and form submitted are both fired for same form.
  // The submitted_forms_ is cleared when we know no more submission could
  // happen for that form.
  std::set<FormRendererId> submitted_forms_;

  // Whether the Autofill popup is possibly visible.  This is tracked as a
  // performance improvement, so that the IPC channel isn't flooded with
  // messages to close the Autofill popup when it can't possibly be showing.
  bool is_popup_possibly_visible_;

  // Whether or not the secure context is required to query autofill suggestion.
  // Default to false.
  bool is_secure_context_required_;

  // This flag denotes whether or not password suggestions need to be
  // programmatically queried. This is needed on Android WebView because it
  // doesn't use PasswordAutofillAgent to handle password form.
  bool query_password_suggestion_ = false;

  bool last_left_mouse_down_or_gesture_tap_in_node_caused_focus_ = false;

  // This is never null, it is created at construction time and is not changed
  // until destruction time.
  std::unique_ptr<FormTracker> form_tracker_;

  // Whether or not we delay focus handling until scrolling occurs.
  bool focus_requires_scroll_ = true;

  mojo::AssociatedReceiver<mojom::AutofillAgent> receiver_{this};

  mojo::AssociatedRemote<mojom::AutofillDriver> autofill_driver_;

  // For deferring messages to the browser process while prerendering.
  std::unique_ptr<DeferringAutofillDriver> deferring_autofill_driver_;

  bool was_last_action_fill_ = false;

  // Timers for throttling handling of frequent events.
  base::OneShotTimer select_or_selectlist_option_change_batch_timer_;
  base::OneShotTimer datalist_option_change_batch_timer_;
  // TODO(crbug.com/1444566): Merge some or all of these timers?
  base::OneShotTimer process_forms_after_dynamic_change_timer_;
  base::OneShotTimer process_forms_form_extraction_timer_;
  base::OneShotTimer process_forms_form_extraction_with_response_timer_;

  // True iff DidDispatchDOMContentLoadedEvent() fired.
  bool is_dom_content_loaded_ = false;

  // Will be set when accessibility mode changes, depending on what the new mode
  // is.
  bool is_screen_reader_enabled_ = false;

  // Whether agents should enable heavy scraping of form data (e.g., button
  // titles for unowned forms).
  bool is_heavy_form_data_scraping_enabled_ = false;

  // Map WebFormControlElement to the pair of:
  // 1) The most recent text that user typed or autofilled in input elements.
  // Used for storing credit card number/username/password before JavaScript
  // changes them.
  // 2) Field properties mask, i.e. whether the field was autofilled, modified
  // by user, etc. (see FieldPropertiesMask).
  scoped_refptr<FieldDataManager> field_data_manager_ =
      base::MakeRefCounted<FieldDataManager>();

  // This notifier is used to avoid sending redundant messages to the password
  // manager driver mojo interface.
  FocusStateNotifier focus_state_notifier_;

  base::WeakPtrFactory<AutofillAgent> weak_ptr_factory_{this};
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CONTENT_RENDERER_AUTOFILL_AGENT_H_
