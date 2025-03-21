// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/command_line.h"
#include "base/containers/span.h"
#include "base/feature_list.h"
#include "base/hash/hash.h"
#include "base/memory/ref_counted.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/metrics_hashes.h"
#include "base/ranges/algorithm.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "components/autofill/core/browser/autofill_compose_delegate.h"
#include "components/autofill/core/browser/autofill_experiments.h"
#include "components/autofill/core/browser/autofill_form_test_utils.h"
#include "components/autofill/core/browser/autofill_suggestion_generator.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/browser_autofill_manager.h"
#include "components/autofill/core/browser/browser_autofill_manager_test_api.h"
#include "components/autofill/core/browser/crowdsourcing/mock_autofill_crowdsourcing_manager.h"
#include "components/autofill/core/browser/data_model/autofill_profile.h"
#include "components/autofill/core/browser/data_model/credit_card.h"
#include "components/autofill/core/browser/filling_product.h"
#include "components/autofill/core/browser/form_data_importer_test_api.h"
#include "components/autofill/core/browser/form_structure_test_api.h"
#include "components/autofill/core/browser/geo/alternative_state_name_map_test_utils.h"
#include "components/autofill/core/browser/metrics/form_events/form_events.h"
#include "components/autofill/core/browser/metrics/log_event.h"
#include "components/autofill/core/browser/mock_autofill_compose_delegate.h"
#include "components/autofill/core/browser/mock_autofill_optimization_guide.h"
#include "components/autofill/core/browser/mock_single_field_form_fill_router.h"
#include "components/autofill/core/browser/payments/test_credit_card_save_manager.h"
#include "components/autofill/core/browser/payments/test_payments_network_interface.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/profile_token_quality.h"
#include "components/autofill/core/browser/strike_databases/payments/test_credit_card_save_strike_database.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/browser/test_autofill_clock.h"
#include "components/autofill/core/browser/test_autofill_driver.h"
#include "components/autofill/core/browser/test_autofill_external_delegate.h"
#include "components/autofill/core/browser/test_autofill_manager_waiter.h"
#include "components/autofill/core/browser/test_browser_autofill_manager.h"
#include "components/autofill/core/browser/test_form_data_importer.h"
#include "components/autofill/core/browser/test_personal_data_manager.h"
#include "components/autofill/core/browser/test_utils/vote_uploads_test_matchers.h"
#include "components/autofill/core/browser/ui/payments/bubble_show_options.h"
#include "components/autofill/core/browser/ui/popup_item_ids.h"
#include "components/autofill/core/browser/ui/suggestion.h"
#include "components/autofill/core/browser/validation.h"
#include "components/autofill/core/common/autocomplete_parsing_util.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/autofill/core/common/autofill_test_utils.h"
#include "components/autofill/core/common/autofill_util.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/autofill/core/common/mojom/autofill_types.mojom-shared.h"
#include "components/autofill/core/common/signatures.h"
#include "components/feature_engagement/public/feature_constants.h"
#include "components/plus_addresses/plus_address_metrics.h"
#include "components/plus_addresses/plus_address_service.h"
#include "components/prefs/pref_service.h"
#include "components/security_interstitials/core/pref_names.h"
#include "components/security_state/core/security_state.h"
#include "components/strings/grit/components_strings.h"
#include "components/sync/test/test_sync_service.h"
#include "components/translate/core/common/language_detection_details.h"
#include "components/variations/variations_associated_data.h"
#include "components/version_info/channel.h"
#include "net/base/url_util.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"
#include "url/url_canon.h"

namespace autofill {

namespace {

using ::base::UTF8ToUTF16;
using mojom::SubmissionIndicatorEvent;
using mojom::SubmissionSource;
using test::CreateTestAddressFormData;
using test::CreateTestFormField;
using test::CreateTestIbanFormData;
using test::CreateTestPersonalInformationFormData;
using test::CreateTestSelectField;
using test::CreateTestSelectOrSelectListField;
using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::AnyOf;
using ::testing::AtLeast;
using ::testing::Contains;
using ::testing::DoAll;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::Matcher;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Property;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;
using upload_contents_matchers::FieldAutofillTypeIs;
using upload_contents_matchers::FieldsAre;
using upload_contents_matchers::FormSignatureIs;
using upload_contents_matchers::ObservedSubmissionIs;

const std::string kArbitraryNickname = "Grocery Card";
const std::u16string kArbitraryNickname16 = u"Grocery Card";
Suggestion::Icon kAddressEntryIcon = Suggestion::Icon::kAccount;

const std::string_view kPlusAddressSuggestionMetric =
    "Autofill.PlusAddresses.Suggestion.Events";

bool ShouldSplitCardNameAndLastFourDigitsForMetadata() {
  // Splitting card name and last four logic does not apply to iOS because the
  // AutofillSuggestionGenerator on iOS doesn't currently support it.
#if BUILDFLAG(IS_IOS)
  return false;
#else
  return base::FeatureList::IsEnabled(
             features::kAutofillEnableVirtualCardMetadata) &&
         base::FeatureList::IsEnabled(
             features::kAutofillEnableCardProductName) &&
         base::FeatureList::IsEnabled(features::kAutofillEnableCardArtImage);
#endif
}

// The number of obfuscation dots we use as a prefix when showing a credit
// card's last four.
int ObfuscationLengthForCreditCardLastFourDigits() {
#if BUILDFLAG(IS_ANDROID)
  return 2;
#elif BUILDFLAG(IS_IOS)
  return base::FeatureList::IsEnabled(
             features::kAutofillUseTwoDotsForLastFourDigits)
             ? 2
             : 4;
#else
  return 4;
#endif
}

// Generates credit card suggestion labels. If metadata is enabled, we produce
// shortened labels regardless of whether there is card metadata or not.
std::vector<std::vector<Suggestion::Text>> GenerateLabelsFromCreditCard(
    CreditCard& card) {
  std::vector<std::vector<Suggestion::Text>> suggestion_labels;

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  // Android and iOS do not adhere to card label splitting.
  suggestion_labels = {
      {Suggestion::Text(card.ObfuscatedNumberWithVisibleLastFourDigits(
          ObfuscationLengthForCreditCardLastFourDigits()))}};
#else
  if (ShouldSplitCardNameAndLastFourDigitsForMetadata()) {
    // First label contains card name details and second label contains
    // obfuscated last four.
    suggestion_labels = {
        {Suggestion::Text(card.CardNameForAutofillDisplay(),
                          Suggestion::Text::IsPrimary(false),
                          Suggestion::Text::ShouldTruncate(true)),
         Suggestion::Text(card.ObfuscatedNumberWithVisibleLastFourDigits(
             ObfuscationLengthForCreditCardLastFourDigits()))}};
  } else {
    // Desktop uses the descriptive label.
    suggestion_labels = {
        {Suggestion::Text(card.CardIdentifierStringAndDescriptiveExpiration(
            /*app_locale=*/"en-US"))}};
  }
#endif
  return suggestion_labels;
}

Suggestion GenerateSuggestionFromCardDetails(
    const std::string& network,
    const Suggestion::Icon icon,
    const std::string& last_four,
    std::string expiration_date_label,
    const std::string& nickname = std::string()) {
  const std::string& network_or_nickname =
      nickname.empty()
          ? base::UTF16ToUTF8(CreditCard::NetworkForDisplay(network))
          : nickname;
  if (ShouldSplitCardNameAndLastFourDigitsForMetadata()) {
    return Suggestion(
        network_or_nickname,
        test::ObfuscatedCardDigitsAsUTF8(
            last_four, ObfuscationLengthForCreditCardLastFourDigits()),
        expiration_date_label, icon, PopupItemId::kCreditCardEntry);
  } else {
#if !BUILDFLAG(IS_ANDROID) && !BUILDFLAG(IS_IOS)
    // We use a longer label on desktop platforms.
    expiration_date_label = std::string("Expires on ") + expiration_date_label;
#endif
    return Suggestion(
        base::StrCat(
            {network_or_nickname, std::string("  "),
             test::ObfuscatedCardDigitsAsUTF8(
                 last_four, ObfuscationLengthForCreditCardLastFourDigits())}),
        expiration_date_label, icon, PopupItemId::kCreditCardEntry);
  }
}

Suggestion GetCardSuggestion(const std::string& network,
                             const std::string& nickname = std::string()) {
  Suggestion::Icon icon = Suggestion::Icon::kCardGeneric;
  std::string last_four;
  std::string expiration_date;
  if (network == kVisaCard) {
    icon = Suggestion::Icon::kCardVisa;
    last_four = "3456";
    expiration_date = "04/99";
  } else if (network == kMasterCard) {
    icon = Suggestion::Icon::kCardMasterCard;
    last_four = "8765";
    expiration_date = "10/98";
  } else if (network == kAmericanExpressCard) {
    icon = Suggestion::Icon::kCardAmericanExpress;
    last_four = "0005";
    expiration_date = "04/10";
  } else {
    NOTREACHED_NORETURN();
  }
  return GenerateSuggestionFromCardDetails(network, icon, last_four,
                                           expiration_date, nickname);
}

struct TestAddressFillData {
  TestAddressFillData(const char* first,
                      const char* middle,
                      const char* last,
                      const char* address1,
                      const char* address2,
                      const char* city,
                      const char* state,
                      const char* postal_code,
                      const char* country,
                      const char* country_short,
                      const char* phone,
                      const char* email,
                      const char* company)
      : first(first),
        middle(middle),
        last(last),
        address1(address1),
        address2(address2),
        city(city),
        state(state),
        postal_code(postal_code),
        country(country),
        country_short(country_short),
        phone(phone),
        email(email),
        company(company) {}

  const char* first;
  const char* middle;
  const char* last;
  const char* address1;
  const char* address2;
  const char* city;
  const char* state;
  const char* postal_code;
  const char* country;
  const char* country_short;
  const char* phone;
  const char* email;
  const char* company;
};

struct TestCardFillData {
  TestCardFillData(const char* name_on_card,
                   const char* card_number,
                   const char* expiration_month,
                   const char* expiration_year,
                   bool use_month_type)
      : name_on_card(name_on_card),
        card_number(card_number),
        expiration_month(expiration_month),
        expiration_year(expiration_year),
        use_month_type(use_month_type) {}
  const char* name_on_card;
  const char* card_number;
  const char* expiration_month;
  const char* expiration_year;
  bool use_month_type;
};

const TestAddressFillData
    kEmptyAddressFillData("", "", "", "", "", "", "", "", "", "", "", "", "");

const TestCardFillData kEmptyCardFillData("",
                                          "",
                                          "",
                                          "",
                                          /*use_month_type=*/false);

const TestCardFillData kElvisCardFillData("Elvis Presley",
                                          "4234567890123456",
                                          "04",
                                          "2999",
                                          /*use_month_type*/ false);

TestAddressFillData GetElvisAddressFillData() {
  return {
      "Elvis",
      "Aaron",
      "Presley",
      "3734 Elvis Presley Blvd.",
      "Apt. 10",
      "Memphis",
      "Tennessee",
      "38116",
      "United States",
      "US",
      base::FeatureList::IsEnabled(features::kAutofillDefaultToCityAndNumber)
          ? "2345678901"
          : "12345678901",
      "theking@gmail.com",
      "RCA"};
}

// Matches a FillFieldLogEvent by equality of fields. Use FillEventId(-1) if
// you want to ignore the fill_event_id.
auto EqualsFillFieldLogEvent(const FillFieldLogEvent& expected) {
  return AllOf(
      testing::Conditional(
          expected.fill_event_id == FillEventId(-1), _,
          Field("fill_event_id", &FillFieldLogEvent::fill_event_id,
                expected.fill_event_id)),
      Field("had_value_before_filling",
            &FillFieldLogEvent::had_value_before_filling,
            expected.had_value_before_filling),
      Field("autofill_skipped_status",
            &FillFieldLogEvent::autofill_skipped_status,
            expected.autofill_skipped_status),
      Field("was_autofilled_before_security_policy",
            &FillFieldLogEvent::was_autofilled_before_security_policy,
            expected.was_autofilled_before_security_policy),
      Field("had_value_after_filling",
            &FillFieldLogEvent::had_value_after_filling,
            expected.had_value_after_filling),
      Field("filling_method", &FillFieldLogEvent::filling_method,
            expected.filling_method),
      Field("filling_prevented_by_iframe_security_policy",
            &FillFieldLogEvent::filling_prevented_by_iframe_security_policy,
            expected.filling_prevented_by_iframe_security_policy));
}

// Creates a GUID for testing. For example,
// MakeGuid(123) = "00000000-0000-0000-0000-000000000123";
std::string MakeGuid(size_t last_digit) {
  return base::StringPrintf("00000000-0000-0000-0000-%012zu", last_digit);
}

std::string kElvisProfileGuid = MakeGuid(1);

// Used to control the plus addressing feature, such that it is deterministic
// and does not trigger any UI elements.
class MockPlusAddressService : public plus_addresses::PlusAddressService {
 public:
  MOCK_METHOD(bool,
              SupportsPlusAddresses,
              (url::Origin, bool is_off_the_record),
              (override));
};

class MockCreditCardAccessManager : public CreditCardAccessManager {
 public:
  MockCreditCardAccessManager(AutofillDriver* driver,
                              AutofillClient* client,
                              PersonalDataManager* personal_data_manager,
                              autofill_metrics::CreditCardFormEventLogger*
                                  credit_card_form_event_logger)
      : CreditCardAccessManager(driver,
                                client,
                                personal_data_manager,
                                credit_card_form_event_logger) {}
  MOCK_METHOD(void, PrepareToFetchCreditCard, (), (override));
};

class MockAutofillClient : public TestAutofillClient {
 public:
  MockAutofillClient() {
    ON_CALL(*this, GetChannel())
        .WillByDefault(Return(version_info::Channel::UNKNOWN));
    ON_CALL(*this, IsPasswordManagerEnabled()).WillByDefault(Return(true));
  }
  MockAutofillClient(const MockAutofillClient&) = delete;
  MockAutofillClient& operator=(const MockAutofillClient&) = delete;
  ~MockAutofillClient() override = default;

  MOCK_METHOD(version_info::Channel, GetChannel, (), (const override));
  MOCK_METHOD(AutofillOptimizationGuide*,
              GetAutofillOptimizationGuide,
              (),
              (const override));
  MOCK_METHOD(profile_metrics::BrowserProfileType,
              GetProfileType,
              (),
              (const override));
  MOCK_METHOD(void, HideAutofillPopup, (PopupHidingReason reason), (override));
  MOCK_METHOD(bool, IsPasswordManagerEnabled, (), (override));
  MOCK_METHOD(void,
              DidFillOrPreviewForm,
              (mojom::ActionPersistence action_persistence,
               AutofillTriggerSource trigger_source,
               bool is_refill),
              (override));
  MOCK_METHOD(bool, HasCreditCardScanFeature, (), (const override));
  MOCK_METHOD(void,
              TriggerUserPerceptionOfAutofillSurvey,
              ((const std::map<std::string, std::string>&)),
              (override));
  MOCK_METHOD(plus_addresses::PlusAddressService*,
              GetPlusAddressService,
              (),
              (override));
  MOCK_METHOD(AutofillComposeDelegate*, GetComposeDelegate, (), (override));
  MOCK_METHOD(void,
              OnVirtualCardDataAvailable,
              (const VirtualCardManualFallbackBubbleOptions&),
              (override));
};

class MockTouchToFillDelegate : public TouchToFillDelegate {
 public:
  MockTouchToFillDelegate() = default;
  MockTouchToFillDelegate(const MockTouchToFillDelegate&) = delete;
  MockTouchToFillDelegate& operator=(const MockTouchToFillDelegate&) = delete;
  ~MockTouchToFillDelegate() override = default;
  MOCK_METHOD(BrowserAutofillManager*, GetManager, (), (override));
  MOCK_METHOD(bool,
              IntendsToShowTouchToFill,
              (FormGlobalId, FieldGlobalId, const FormData&),
              (override));
  MOCK_METHOD(bool,
              TryToShowTouchToFill,
              (const FormData&, const FormFieldData&),
              (override));
  MOCK_METHOD(bool, IsShowingTouchToFill, (), (override));
  MOCK_METHOD(void, HideTouchToFill, (), (override));
  MOCK_METHOD(void, Reset, (), (override));
  MOCK_METHOD(bool, ShouldShowScanCreditCard, (), (override));
  MOCK_METHOD(void, ScanCreditCard, (), (override));
  MOCK_METHOD(void, OnCreditCardScanned, (const CreditCard& card), (override));
  MOCK_METHOD(void, ShowCreditCardSettings, (), (override));
  MOCK_METHOD(void,
              SuggestionSelected,
              (std::string unique_id, bool is_virtual),
              (override));
  MOCK_METHOD(void, OnDismissed, (bool dismissed_by_user), (override));
  MOCK_METHOD(void,
              LogMetricsAfterSubmission,
              (const FormStructure&),
              (override));
};

class MockFastCheckoutDelegate : public FastCheckoutDelegate {
 public:
  MockFastCheckoutDelegate() = default;
  MockFastCheckoutDelegate(const MockFastCheckoutDelegate&) = delete;
  MockFastCheckoutDelegate& operator=(const MockFastCheckoutDelegate&) = delete;
  ~MockFastCheckoutDelegate() override = default;

  MOCK_METHOD(bool,
              TryToShowFastCheckout,
              (const FormData&,
               const FormFieldData&,
               base::WeakPtr<AutofillManager>),
              (override));
  MOCK_METHOD(bool,
              IntendsToShowFastCheckout,
              (AutofillManager&, FormGlobalId, FieldGlobalId, const FormData&),
              (const, override));
  MOCK_METHOD(bool, IsShowingFastCheckoutUI, (), (const, override));
  MOCK_METHOD(void, HideFastCheckout, (bool), (override));
};

AutofillProfile FillDataToAutofillProfile(const TestAddressFillData& data) {
  AutofillProfile profile(i18n_model_definition::kLegacyHierarchyCountryCode);
  test::SetProfileInfo(&profile, data.first, data.middle, data.last, data.email,
                       data.company, data.address1, data.address2, data.city,
                       data.state, data.postal_code, data.country_short,
                       data.phone);
  return profile;
}

CreditCard FillDataToCreditCardInfo(const TestCardFillData& data) {
  CreditCard card;
  test::SetCreditCardInfo(&card, data.name_on_card, data.card_number,
                          data.expiration_month, data.expiration_year, "1");
  return card;
}

void ExpectFilledField(const char* expected_label,
                       const char* expected_name,
                       const char* expected_value,
                       FormControlType expected_form_control_type,
                       const FormFieldData& field) {
  SCOPED_TRACE(expected_label);
  EXPECT_EQ(UTF8ToUTF16(expected_label), field.label);
  EXPECT_EQ(UTF8ToUTF16(expected_name), field.name);
  EXPECT_EQ(UTF8ToUTF16(expected_value), field.value);
  EXPECT_EQ(expected_form_control_type, field.form_control_type);
}

// Verifies that the |filled_form| has been filled with the given data.
// Verifies address fields if |has_address_fields| is true, and verifies
// credit card fields if |has_credit_card_fields| is true. Verifies both if both
// are true. |use_month_type| is used for credit card input month type.
void ExpectFilledForm(
    const FormData& filled_form,
    const std::optional<TestAddressFillData>& address_fill_data,
    const std::optional<TestCardFillData>& card_fill_data) {
  // The number of fields in the address and credit card forms created above.
  const size_t kAddressFormSize = 11;
  const size_t kCreditCardFormSizeMonthType = 4;
  const size_t kCreditCardFormSizeNotMonthType = 5;

  EXPECT_EQ(u"MyForm", filled_form.name);
  EXPECT_EQ(GURL("https://myform.com/form.html"), filled_form.url);
  EXPECT_EQ(GURL("https://myform.com/submit.html"), filled_form.action);

  size_t form_size = 0;
  if (address_fill_data) {
    form_size += kAddressFormSize;
  }
  if (card_fill_data) {
    form_size += card_fill_data->use_month_type
                     ? kCreditCardFormSizeMonthType
                     : kCreditCardFormSizeNotMonthType;
  }
  ASSERT_EQ(form_size, filled_form.fields.size());

  if (address_fill_data) {
    ExpectFilledField("First Name", "firstname", address_fill_data->first,
                      FormControlType::kInputText, filled_form.fields[0]);
    ExpectFilledField("Middle Name", "middlename", address_fill_data->middle,
                      FormControlType::kInputText, filled_form.fields[1]);
    ExpectFilledField("Last Name", "lastname", address_fill_data->last,
                      FormControlType::kInputText, filled_form.fields[2]);
    ExpectFilledField("Address Line 1", "addr1", address_fill_data->address1,
                      FormControlType::kInputText, filled_form.fields[3]);
    ExpectFilledField("Address Line 2", "addr2", address_fill_data->address2,
                      FormControlType::kInputText, filled_form.fields[4]);
    ExpectFilledField("City", "city", address_fill_data->city,
                      FormControlType::kInputText, filled_form.fields[5]);
    ExpectFilledField("State", "state", address_fill_data->state,
                      FormControlType::kInputText, filled_form.fields[6]);
    ExpectFilledField("Postal Code", "zipcode", address_fill_data->postal_code,
                      FormControlType::kInputText, filled_form.fields[7]);
    ExpectFilledField("Country", "country", address_fill_data->country,
                      FormControlType::kInputText, filled_form.fields[8]);
    ExpectFilledField("Phone Number", "phonenumber", address_fill_data->phone,
                      FormControlType::kInputTelephone, filled_form.fields[9]);
    ExpectFilledField("Email", "email", address_fill_data->email,
                      FormControlType::kInputEmail, filled_form.fields[10]);
  }

  if (card_fill_data) {
    size_t offset = address_fill_data ? kAddressFormSize : 0;
    ExpectFilledField("Name on Card", "nameoncard",
                      card_fill_data->name_on_card, FormControlType::kInputText,
                      filled_form.fields[offset + 0]);
    ExpectFilledField("Card Number", "cardnumber", card_fill_data->card_number,
                      FormControlType::kInputText,
                      filled_form.fields[offset + 1]);
    if (card_fill_data->use_month_type) {
      std::string exp_year = card_fill_data->expiration_year;
      std::string exp_month = card_fill_data->expiration_month;
      std::string date;
      if (!exp_year.empty() && !exp_month.empty())
        date = exp_year + "-" + exp_month;

      ExpectFilledField("Expiration Date", "ccmonth", date.c_str(),
                        FormControlType::kInputMonth,
                        filled_form.fields[offset + 2]);
    } else {
      ExpectFilledField(
          "Expiration Date", "ccmonth", card_fill_data->expiration_month,
          FormControlType::kInputText, filled_form.fields[offset + 2]);
      ExpectFilledField("", "ccyear", card_fill_data->expiration_year,
                        FormControlType::kInputText,
                        filled_form.fields[offset + 3]);
    }
  }
}

void ExpectFilledAddressFormElvis(const FormData& filled_form,
                                  bool has_credit_card_fields) {
  std::optional<TestCardFillData> expected_card_fill_data;
  if (has_credit_card_fields) {
    expected_card_fill_data = kEmptyCardFillData;
  }
  ExpectFilledForm(filled_form, GetElvisAddressFillData(),
                   expected_card_fill_data);
}

void ExpectFilledCreditCardFormElvis(const FormData& filled_form,
                                     bool has_address_fields) {
  std::optional<TestAddressFillData> expected_address_fill_data;
  if (has_address_fields) {
    expected_address_fill_data = kEmptyAddressFillData;
  }
  ExpectFilledForm(filled_form, expected_address_fill_data, kElvisCardFillData);
}

void CheckThatOnlyFieldByIndexHasThisPossibleType(
    const FormStructure& form_structure,
    size_t field_index,
    FieldType type,
    FieldPropertiesMask mask) {
  EXPECT_TRUE(field_index < form_structure.field_count());

  for (size_t i = 0; i < form_structure.field_count(); i++) {
    if (i == field_index) {
      EXPECT_THAT(form_structure.field(i)->possible_types(), ElementsAre(type));
      EXPECT_EQ(mask, form_structure.field(i)->properties_mask);
    } else {
      EXPECT_THAT(form_structure.field(i)->possible_types(),
                  Not(Contains(type)));
    }
  }
}

void CheckThatNoFieldHasThisPossibleType(const FormStructure& form_structure,
                                         FieldType type) {
  for (size_t i = 0; i < form_structure.field_count(); i++) {
    EXPECT_THAT(form_structure.field(i)->possible_types(), Not(Contains(type)));
  }
}

// Returns a matcher that checks a `FormStructure`'s renderer id.
auto FormStructureHasRendererId(FormRendererId form_renderer_id) {
  return Pointee(Property(&FormStructure::global_id,
                          Field(&FormGlobalId::renderer_id, form_renderer_id)));
}

class MockAutofillDriver : public TestAutofillDriver {
 public:
  MockAutofillDriver() = default;
  MockAutofillDriver(const MockAutofillDriver&) = delete;
  MockAutofillDriver& operator=(const MockAutofillDriver&) = delete;

  // Mock methods to enable testability.
  MOCK_METHOD((base::flat_set<FieldGlobalId>),
              ApplyFormAction,
              (mojom::ActionType action_type,
               mojom::ActionPersistence action_persistence,
               const FormData& data,
               const url::Origin& triggered_origin,
               (const base::flat_map<FieldGlobalId, FieldType>&)),
              (override));
  MOCK_METHOD(void,
              ApplyFieldAction,
              (mojom::ActionPersistence action_persistence,
               mojom::TextReplacement text_replacement,
               const FieldGlobalId& field_id,
               const std::u16string& value),
              (override));
  MOCK_METHOD(
      void,
      SendAutofillTypePredictionsToRenderer,
      (const std::vector<vector_experimental_raw_ptr<FormStructure>>& forms),
      (override));
};

}  // namespace

class BrowserAutofillManagerTest : public testing::Test {
 public:
  void SetUp() override {
    // Advance the mock clock to a fixed, arbitrary, somewhat recent date.
    base::Time year2020;
    ASSERT_TRUE(base::Time::FromString("01/01/20", &year2020));
    task_environment_.FastForwardBy(year2020 - base::Time::Now());

    autofill_client_.SetPrefs(test::PrefServiceForTesting());
    personal_data().set_auto_accept_address_imports_for_testing(true);
    personal_data().SetPrefService(autofill_client_.GetPrefs());
    personal_data().SetSyncServiceForTest(&sync_service_);

    autofill_driver_ = std::make_unique<NiceMock<MockAutofillDriver>>();
    autofill_client_.set_test_payments_network_interface(
        std::make_unique<payments::TestPaymentsNetworkInterface>(
            autofill_client_.GetURLLoaderFactory(),
            autofill_client_.GetIdentityManager(), &personal_data()));
    auto credit_card_save_manager = std::make_unique<TestCreditCardSaveManager>(
        autofill_driver_.get(), &autofill_client_,
        &personal_data());
    credit_card_save_manager->SetCreditCardUploadEnabled(true);
    autofill_client_.set_test_form_data_importer(
        std::make_unique<autofill::TestFormDataImporter>(
            &autofill_client_, std::move(credit_card_save_manager),
            /*iban_save_manager=*/nullptr, &personal_data(), "en-US"));

    ResetBrowserAutofillManager();
    // By default, if we offer single field form fill, suggestions should be
    // returned because it is assumed |field.should_autocomplete| is set to
    // true. This should be overridden in tests where
    // |field.should_autocomplete| is set to false.
    ON_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions)
        .WillByDefault(Return(true));

    autofill_client_.set_crowdsourcing_manager(
        std::make_unique<NiceMock<MockAutofillCrowdsourcingManager>>(
            &autofill_client_));

    browser_autofill_manager_->set_touch_to_fill_delegate(
        std::make_unique<NiceMock<MockTouchToFillDelegate>>());
    ON_CALL(touch_to_fill_delegate(), GetManager())
        .WillByDefault(Return(browser_autofill_manager_.get()));
    ON_CALL(touch_to_fill_delegate(), IsShowingTouchToFill())
        .WillByDefault(Return(false));

    browser_autofill_manager_->set_fast_checkout_delegate(
        std::make_unique<NiceMock<MockFastCheckoutDelegate>>());
    ON_CALL(fast_checkout_delegate(), IsShowingFastCheckoutUI())
        .WillByDefault(Return(false));

    autofill_client_.set_test_strike_database(
        std::make_unique<TestStrikeDatabase>());

    // Initialize the TestPersonalDataManager with some default data.
    CreateTestAutofillProfiles();
    CreateTestCreditCards();

    // Mandatory re-auth is required for credit card autofill on automotive, so
    // the authenticator response needs to be properly mocked.
#if BUILDFLAG(IS_ANDROID)
    autofill_client_.SetUpDeviceBiometricAuthenticatorSuccessOnAutomotive();
#endif
  }

  void CreateTestServerCreditCards() {
    personal_data().ClearCreditCards();

    CreditCard masked_server_card;
    test::SetCreditCardInfo(&masked_server_card, "Elvis Presley",
                            "4234567890123456",  // Visa
                            "04", "2999", "1");
    masked_server_card.set_guid(MakeGuid(7));
    masked_server_card.set_record_type(
        CreditCard::RecordType::kMaskedServerCard);
    personal_data().AddServerCreditCard(masked_server_card);

    CreditCard full_server_card;
    test::SetCreditCardInfo(&full_server_card, "Buddy Holly",
                            "5187654321098765",  // Mastercard
                            "10", "2998", "1");
    full_server_card.set_guid(MakeGuid(8));
    full_server_card.set_record_type(CreditCard::RecordType::kFullServerCard);
    personal_data().AddServerCreditCard(full_server_card);
  }

  void CreateTestServerAndLocalCreditCards() {
    personal_data().ClearCreditCards();

    CreditCard masked_server_card;
    test::SetCreditCardInfo(&masked_server_card, "Elvis Presley",
                            "4234567890123456",  // Visa
                            "04", "2999", "1");
    masked_server_card.set_guid(MakeGuid(7));
    masked_server_card.set_record_type(
        CreditCard::RecordType::kMaskedServerCard);
    personal_data().AddServerCreditCard(masked_server_card);

    CreditCard full_server_card;
    test::SetCreditCardInfo(&full_server_card, "Buddy Holly",
                            "5187654321098765",  // Mastercard
                            "10", "2998", "1");
    full_server_card.set_guid(MakeGuid(8));
    full_server_card.set_record_type(CreditCard::RecordType::kFullServerCard);
    personal_data().AddServerCreditCard(full_server_card);

    CreditCard local_card;
    test::SetCreditCardInfo(&local_card, "Elvis Presley",
                            "4234567890123456",  // Visa
                            "04", "2999", "1");
    local_card.set_guid(MakeGuid(9));
    local_card.set_record_type(CreditCard::RecordType::kLocalCard);
    personal_data().AddCreditCard(local_card);
  }

  void CreateUniqueTestServerAndLocalCreditCards() {
    personal_data().ClearCreditCards();

    CreditCard masked_server_card;
    test::SetCreditCardInfo(&masked_server_card, "Elvis Presley",
                            "4234567890123456",  // Visa
                            "04", "2999", "1");
    masked_server_card.set_guid(MakeGuid(7));
    masked_server_card.set_record_type(
        CreditCard::RecordType::kMaskedServerCard);
    personal_data().AddServerCreditCard(masked_server_card);

    CreditCard full_server_card;
    test::SetCreditCardInfo(&full_server_card, "Buddy Holly",
                            "5187654321098765",  // Mastercard
                            "10", "2998", "1");
    full_server_card.set_guid(MakeGuid(8));
    full_server_card.set_record_type(CreditCard::RecordType::kFullServerCard);
    personal_data().AddServerCreditCard(full_server_card);

    CreditCard local_card;
    test::SetCreditCardInfo(&local_card, "Elvis Presley",
                            "4234567890121237",  // Visa
                            "04", "2999", "1");
    local_card.set_guid(MakeGuid(9));
    local_card.set_record_type(CreditCard::RecordType::kLocalCard);
    personal_data().AddCreditCard(local_card);
  }

  void TearDown() override {
    // Order of destruction is important as BrowserAutofillManager relies on
    // PersonalDataManager to be around when it gets destroyed.
    browser_autofill_manager_.reset();

    personal_data().SetPrefService(nullptr);
    personal_data().ClearCreditCards();
  }

  MockTouchToFillDelegate& touch_to_fill_delegate() {
    return *static_cast<MockTouchToFillDelegate*>(
        browser_autofill_manager_->touch_to_fill_delegate());
  }

  MockFastCheckoutDelegate& fast_checkout_delegate() {
    return *static_cast<MockFastCheckoutDelegate*>(
        browser_autofill_manager_->fast_checkout_delegate());
  }

  void GetAutofillSuggestions(
      const FormData& form,
      const FormFieldData& field,
      AutofillSuggestionTriggerSource trigger_source =
          AutofillSuggestionTriggerSource::kTextFieldDidChange) {
    browser_autofill_manager_->OnAskForValuesToFill(form, field, gfx::RectF(),
                                                    trigger_source);
  }

  void DidShowAutofillSuggestions(const FormData& form,
                                  size_t field_index = 0) {
    browser_autofill_manager_->DidShowSuggestions(
        std::vector<PopupItemId>({PopupItemId::kAddressEntry}), form,
        form.fields[field_index]);
  }

  void TryToShowTouchToFill(const FormData& form,
                            const FormFieldData& field,
                            bool form_element_was_clicked) {
    browser_autofill_manager_->OnAskForValuesToFill(
        form, field, gfx::RectF(),
        form_element_was_clicked
            ? AutofillSuggestionTriggerSource::kFormControlElementClicked
            : AutofillSuggestionTriggerSource::kTextFieldDidChange);
  }

  void FormsSeen(const std::vector<FormData>& forms) {
    browser_autofill_manager_->OnFormsSeen(/*updated_forms=*/forms,
                                           /*removed_forms=*/{});
  }

  void FormSubmitted(const FormData& form) {
    browser_autofill_manager_->OnFormSubmitted(
        form, false, SubmissionSource::FORM_SUBMISSION);
  }

  // TODO(crbug.com/1330108): Have separate functions for profile and credit
  // card filling.
  void FillAutofillFormData(
      const FormData& form,
      const FormFieldData& field,
      std::string guid,
      AutofillTriggerDetails trigger_details = {
          .trigger_source = AutofillTriggerSource::kPopup}) {
    browser_autofill_manager_->OnAskForValuesToFill(
        form, field, {},
        AutofillSuggestionTriggerSource::kTextFieldDidReceiveKeyDown);
    if (const AutofillProfile* profile =
            personal_data().GetProfileByGUID(guid)) {
      browser_autofill_manager_->FillOrPreviewProfileForm(
          mojom::ActionPersistence::kFill, form, field, *profile,
          trigger_details);
    } else if (const CreditCard* card =
                   personal_data().GetCreditCardByGUID(guid)) {
      browser_autofill_manager_->FillOrPreviewCreditCardForm(
          mojom::ActionPersistence::kFill, form, field, *card, trigger_details);
    }
  }

  // Calls |browser_autofill_manager_->OnFillAutofillFormData()| with the
  // specified input parameters after setting up the expectation that the mock
  // driver's |ApplyFormAction()| method will be called and saving the parameter
  // of that call into the |response_data| output parameter.
  FormData FillAutofillFormDataAndGetResults(
      const FormData& input_form,
      const FormFieldData& input_field,
      std::string guid,
      AutofillTriggerDetails trigger_details = {
          .trigger_source = AutofillTriggerSource::kPopup}) {
    FormData response_data;
    std::vector<FieldGlobalId> global_ids;
    for (const auto& field : input_form.fields) {
      global_ids.push_back(field.global_id());
    }
    EXPECT_CALL(*autofill_driver_, ApplyFormAction)
        .WillOnce(DoAll(SaveArg<2>(&response_data), Return(global_ids)));
    FillAutofillFormData(input_form, input_field, guid, trigger_details);
    return response_data;
  }

  FormData PreviewVirtualCardDataAndGetResults(
      mojom::ActionPersistence action_persistence,
      const FormData& input_form,
      const FormFieldData& input_field,
      const CreditCard& virtual_card) {
    FormData response_data;
    EXPECT_CALL(*autofill_driver_, ApplyFormAction)
        .WillOnce((DoAll(SaveArg<2>(&response_data),
                         Return(std::vector<FieldGlobalId>{}))));
    browser_autofill_manager_->FillOrPreviewCreditCardForm(
        action_persistence, input_form, input_field, virtual_card,
        {.trigger_source = AutofillTriggerSource::kPopup});
    return response_data;
  }

  FormData CreateTestCreditCardFormData(bool is_https, bool use_month_type) {
    FormData form;
    CreateTestCreditCardFormData(&form, is_https, use_month_type);
    return form;
  }

  // Populates |form| with data corresponding to a simple credit card form.
  // Note that this actually appends fields to the form data, which can be
  // useful for building up more complex test forms.
  void CreateTestCreditCardFormData(FormData* form,
                                    bool is_https,
                                    bool use_month_type) {
    form->name = u"MyForm";
    if (is_https) {
      GURL::Replacements replacements;
      replacements.SetSchemeStr(url::kHttpsScheme);
      autofill_client_.set_form_origin(
          autofill_client_.form_origin().ReplaceComponents(replacements));
      form->url = GURL("https://myform.com/form.html");
      form->action = GURL("https://myform.com/submit.html");
    } else {
      // If we are testing a form that submits over HTTP, we also need to set
      // the main frame to HTTP, otherwise mixed form warnings will trigger and
      // autofill will be disabled.
      GURL::Replacements replacements;
      replacements.SetSchemeStr(url::kHttpScheme);
      autofill_client_.set_form_origin(
          autofill_client_.form_origin().ReplaceComponents(replacements));
      form->url = GURL("http://myform.com/form.html");
      form->action = GURL("http://myform.com/submit.html");
    }

    form->fields.push_back(CreateTestFormField("Name on Card", "nameoncard", "",
                                               FormControlType::kInputText));
    form->fields.push_back(CreateTestFormField("Card Number", "cardnumber", "",
                                               FormControlType::kInputText));
    if (use_month_type) {
      form->fields.push_back(CreateTestFormField(
          "Expiration Date", "ccmonth", "", FormControlType::kInputMonth));
    } else {
      form->fields.push_back(CreateTestFormField(
          "Expiration Date", "ccmonth", "", FormControlType::kInputText));
      form->fields.push_back(
          CreateTestFormField("", "ccyear", "", FormControlType::kInputText));
    }
    form->fields.push_back(
        CreateTestFormField("CVC", "cvc", "", FormControlType::kInputText));
  }

  void PrepareForRealPanResponse(FormData* form, CreditCard* card) {
    // This line silences the warning from PaymentsNetworkInterface about
    // matching sync and Payments server types.
    base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
        "sync-url", "https://google.com");

    CreateTestCreditCardFormData(form, true, false);
    FormsSeen({*form});
    *card = CreditCard(CreditCard::RecordType::kMaskedServerCard, "a123");
    test::SetCreditCardInfo(card, "John Dillinger", "1881" /* Visa */, "01",
                            "2017", "1");
    card->SetNetworkForMaskedCard(kVisaCard);

    EXPECT_CALL(*autofill_driver_, ApplyFormAction).Times(AtLeast(1));
    browser_autofill_manager_->FillOrPreviewCreditCardForm(
        mojom::ActionPersistence::kFill, *form, form->fields[0], *card,
        {.trigger_source = AutofillTriggerSource::kPopup});
  }

  void OnDidGetRealPan(AutofillClient::PaymentsRpcResult result,
                       const std::string& real_pan,
                       bool is_virtual_card = false) {
    payments::FullCardRequest* full_card_request =
        browser_autofill_manager_->client()
            .GetCvcAuthenticator()
            ->full_card_request_.get();
    DCHECK(full_card_request);

    // Mock user response.
    payments::FullCardRequest::UserProvidedUnmaskDetails details;
    details.cvc = u"123";
    full_card_request->OnUnmaskPromptAccepted(details);

    // Mock payments response.
    payments::PaymentsNetworkInterface::UnmaskResponseDetails response;
    response.card_type = is_virtual_card
                             ? AutofillClient::PaymentsRpcCardType::kVirtualCard
                             : AutofillClient::PaymentsRpcCardType::kServerCard;
    full_card_request->OnDidGetRealPan(result,
                                       response.with_real_pan(real_pan));
  }

  // Convenience method to cast the FullCardRequest into a CardUnmaskDelegate.
  CardUnmaskDelegate* full_card_unmask_delegate() {
    payments::FullCardRequest* full_card_request =
        browser_autofill_manager_->client()
            .GetCvcAuthenticator()
            ->full_card_request_.get();
    DCHECK(full_card_request);
    return static_cast<CardUnmaskDelegate*>(full_card_request);
  }

  void DisableAutofillViaAblation(
      base::test::ScopedFeatureList& scoped_feature_list,
      bool for_addresses,
      bool for_credit_cards) {
    base::FieldTrialParams feature_parameters{
        {features::kAutofillAblationStudyEnabledForAddressesParam.name,
         for_addresses ? "true" : "false"},
        {features::kAutofillAblationStudyEnabledForPaymentsParam.name,
         for_credit_cards ? "true" : "false"},
        {features::kAutofillAblationStudyAblationWeightPerMilleParam.name,
         "1000"},
    };
    scoped_feature_list.InitAndEnableFeatureWithParameters(
        features::kAutofillEnableAblationStudy, feature_parameters);
  }

  void ResetBrowserAutofillManager() {
    browser_autofill_manager_ = std::make_unique<TestBrowserAutofillManager>(
        autofill_driver_.get(), &autofill_client_);

    test_api(*browser_autofill_manager_)
        .set_single_field_form_fill_router(
            std::make_unique<NiceMock<MockSingleFieldFormFillRouter>>(
                autofill_client_.GetMockAutocompleteHistoryManager(),
                autofill_client_.GetMockIbanManager(),
                autofill_client_.GetMockMerchantPromoCodeManager()));
    test_api(*browser_autofill_manager_)
        .SetExternalDelegate(std::make_unique<TestAutofillExternalDelegate>(
            browser_autofill_manager_.get(),
            /*call_parent_methods=*/true));
    test_api(*browser_autofill_manager_)
        .set_credit_card_access_manager(
            std::make_unique<NiceMock<MockCreditCardAccessManager>>(
                autofill_driver_.get(), &autofill_client_, &personal_data(),
                test_api(*browser_autofill_manager_)
                    .credit_card_form_event_logger()));
  }

  // Matches a AskForValuesToFillFieldLogEvent by equality of fields.
  auto Equal(const AskForValuesToFillFieldLogEvent& expected) {
    return VariantWith<AskForValuesToFillFieldLogEvent>(
        AllOf(Field("has_suggestion",
                    &AskForValuesToFillFieldLogEvent::has_suggestion,
                    expected.has_suggestion),
              Field("suggestion_is_shown",
                    &AskForValuesToFillFieldLogEvent::suggestion_is_shown,
                    expected.suggestion_is_shown)));
  }

  // Matches a TriggerFillFieldLogEvent by equality of fields.
  auto Equal(const TriggerFillFieldLogEvent& expected) {
    return VariantWith<TriggerFillFieldLogEvent>(
        AllOf(Field("fill_event_id", &TriggerFillFieldLogEvent::fill_event_id,
                    expected.fill_event_id),
              Field("data_type", &TriggerFillFieldLogEvent::data_type,
                    expected.data_type),
              Field("associated_country_code",
                    &TriggerFillFieldLogEvent::associated_country_code,
                    expected.associated_country_code),
              Field("timestamp", &TriggerFillFieldLogEvent::timestamp,
                    expected.timestamp)));
  }

  // Matches a FillFieldLogEvent by equality of fields. Use FillEventId(-1) if
  // you want to ignore the fill_event_id.
  auto Equal(const FillFieldLogEvent& expected) {
    return VariantWith<FillFieldLogEvent>(AllOf(
        testing::Conditional(
            expected.fill_event_id == FillEventId(-1), _,
            Field("fill_event_id", &FillFieldLogEvent::fill_event_id,
                  expected.fill_event_id)),
        Field("had_value_before_filling",
              &FillFieldLogEvent::had_value_before_filling,
              expected.had_value_before_filling),
        Field("autofill_skipped_status",
              &FillFieldLogEvent::autofill_skipped_status,
              expected.autofill_skipped_status),
        Field("was_autofilled_before_security_policy",
              &FillFieldLogEvent::was_autofilled_before_security_policy,
              expected.was_autofilled_before_security_policy),
        Field("had_value_after_filling",
              &FillFieldLogEvent::had_value_after_filling,
              expected.had_value_after_filling),
        Field("filling_method", &FillFieldLogEvent::filling_method,
              expected.filling_method),
        Field("filling_prevented_by_iframe_security_policy",
              &FillFieldLogEvent::filling_prevented_by_iframe_security_policy,
              expected.filling_prevented_by_iframe_security_policy)));
  }

  // Matches a TypingFieldLogEvent by equality of fields.
  auto Equal(const TypingFieldLogEvent& expected) {
    return VariantWith<TypingFieldLogEvent>(Field(
        "has_value_after_typing", &TypingFieldLogEvent::has_value_after_typing,
        expected.has_value_after_typing));
  }

  // Matches a HeuristicPredictionFieldLogEvent by equality of fields.
  auto Equal(const HeuristicPredictionFieldLogEvent& expected) {
    return VariantWith<HeuristicPredictionFieldLogEvent>(AllOf(
        Field("field_type", &HeuristicPredictionFieldLogEvent::field_type,
              expected.field_type),
        Field("pattern_source",
              &HeuristicPredictionFieldLogEvent::pattern_source,
              expected.pattern_source),
        Field("is_active_pattern_source",
              &HeuristicPredictionFieldLogEvent::is_active_pattern_source,
              expected.is_active_pattern_source),
        Field("rank_in_field_signature_group",
              &HeuristicPredictionFieldLogEvent::rank_in_field_signature_group,
              expected.rank_in_field_signature_group)));
  }

  // Matches a AutocompleteAttributeFieldLogEvent by equality of fields.
  auto Equal(const AutocompleteAttributeFieldLogEvent& expected) {
    return VariantWith<AutocompleteAttributeFieldLogEvent>(AllOf(
        Field("html_type", &AutocompleteAttributeFieldLogEvent::html_type,
              expected.html_type),
        Field("html_mode", &AutocompleteAttributeFieldLogEvent::html_mode,
              expected.html_mode),
        Field(
            "rank_in_field_signature_group",
            &AutocompleteAttributeFieldLogEvent::rank_in_field_signature_group,
            expected.rank_in_field_signature_group)));
  }

  // Matches a ServerPredictionFieldLogEvent by equality of fields.
  auto Equal(const ServerPredictionFieldLogEvent& expected) {
    return VariantWith<ServerPredictionFieldLogEvent>(AllOf(
        Field("server_type1", &ServerPredictionFieldLogEvent::server_type1,
              expected.server_type1),
        Field("prediction_source1",
              &ServerPredictionFieldLogEvent::prediction_source1,
              expected.prediction_source1),
        Field("server_type2", &ServerPredictionFieldLogEvent::server_type2,
              expected.server_type2),
        Field("prediction_source2",
              &ServerPredictionFieldLogEvent::prediction_source2,
              expected.prediction_source2),
        Field(
            "server_type_prediction_is_override",
            &ServerPredictionFieldLogEvent::server_type_prediction_is_override,
            expected.server_type_prediction_is_override),
        Field("rank_in_field_signature_group",
              &ServerPredictionFieldLogEvent::rank_in_field_signature_group,
              expected.rank_in_field_signature_group)));
  }

  // Matches a RationalizationFieldLogEvent by equality of fields.
  auto Equal(const RationalizationFieldLogEvent& expected) {
    return VariantWith<RationalizationFieldLogEvent>(
        AllOf(Field("field_type", &RationalizationFieldLogEvent::field_type,
                    expected.field_type),
              Field("section_id", &RationalizationFieldLogEvent::section_id,
                    expected.section_id),
              Field("type_changed", &RationalizationFieldLogEvent::type_changed,
                    expected.type_changed)));
  }

  // Matches a vector of FieldLogEventType objects by equality of fields of each
  // log event type.
  auto ArrayEquals(
      const std::vector<AutofillField::FieldLogEventType>& expected) {
    static_assert(
        absl::variant_size<AutofillField::FieldLogEventType>() == 9,
        "If you add a new field event type, you need to update this function");
    std::vector<Matcher<AutofillField::FieldLogEventType>> matchers;
    for (const auto& event : expected) {
      if (absl::holds_alternative<AskForValuesToFillFieldLogEvent>(event)) {
        matchers.push_back(
            Equal(absl::get<AskForValuesToFillFieldLogEvent>(event)));
      } else if (absl::holds_alternative<TriggerFillFieldLogEvent>(event)) {
        matchers.push_back(Equal(absl::get<TriggerFillFieldLogEvent>(event)));
      } else if (absl::holds_alternative<FillFieldLogEvent>(event)) {
        matchers.push_back(Equal(absl::get<FillFieldLogEvent>(event)));
      } else if (absl::holds_alternative<TypingFieldLogEvent>(event)) {
        matchers.push_back(Equal(absl::get<TypingFieldLogEvent>(event)));
      } else if (absl::holds_alternative<HeuristicPredictionFieldLogEvent>(
                     event)) {
        matchers.push_back(
            Equal(absl::get<HeuristicPredictionFieldLogEvent>(event)));
      } else if (absl::holds_alternative<AutocompleteAttributeFieldLogEvent>(
                     event)) {
        matchers.push_back(
            Equal(absl::get<AutocompleteAttributeFieldLogEvent>(event)));
      } else if (absl::holds_alternative<ServerPredictionFieldLogEvent>(
                     event)) {
        matchers.push_back(
            Equal(absl::get<ServerPredictionFieldLogEvent>(event)));
      } else if (absl::holds_alternative<RationalizationFieldLogEvent>(event)) {
        matchers.push_back(
            Equal(absl::get<RationalizationFieldLogEvent>(event)));
      } else {
        NOTREACHED();
      }
    }
    return ElementsAreArray(matchers);
  }

  // Always show only the `last_four` digits.
  std::string MakeCardLabel(const std::string& nickname,
                            const std::string& last_four) {
    return nickname + "  " +
           test::ObfuscatedCardDigitsAsUTF8(
               last_four, ObfuscationLengthForCreditCardLastFourDigits());
  }

  TestPersonalDataManager& personal_data() {
    return *autofill_client_.GetPersonalDataManager();
  }

  MockCreditCardAccessManager& cc_access_manager() {
    return static_cast<MockCreditCardAccessManager&>(
        browser_autofill_manager_->GetCreditCardAccessManager());
  }

 protected:
  MockAutofillCrowdsourcingManager* crowdsourcing_manager() {
    return static_cast<MockAutofillCrowdsourcingManager*>(
        autofill_client_.GetCrowdsourcingManager());
  }
  TestAutofillExternalDelegate* external_delegate() {
    return static_cast<TestAutofillExternalDelegate*>(
        test_api(*browser_autofill_manager_).external_delegate());
  }
  TestFormDataImporter& form_data_importer() {
    return static_cast<TestFormDataImporter&>(
        *autofill_client_.GetFormDataImporter());
  }
  MockSingleFieldFormFillRouter& single_field_form_fill_router() {
    return static_cast<MockSingleFieldFormFillRouter&>(
        test_api(*browser_autofill_manager_).single_field_form_fill_router());
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  test::AutofillUnitTestEnvironment autofill_test_environment_;
  NiceMock<MockAutofillClient> autofill_client_;
  std::unique_ptr<MockAutofillDriver> autofill_driver_;
  syncer::TestSyncService sync_service_;
  std::unique_ptr<TestBrowserAutofillManager> browser_autofill_manager_;

 private:
  int ToHistogramSample(autofill_metrics::CardUploadDecision metric) {
    for (int sample = 0; sample < metric + 1; ++sample)
      if (metric & (1 << sample))
        return sample;

    NOTREACHED();
    return 0;
  }

  void CreateTestAutofillProfiles() {
    AutofillProfile profile1 =
        FillDataToAutofillProfile(GetElvisAddressFillData());
    profile1.set_guid(kElvisProfileGuid);
    profile1.set_use_date(base::Time::Now() - base::Days(2));
    personal_data().AddProfile(profile1);

    AutofillProfile profile2(
        i18n_model_definition::kLegacyHierarchyCountryCode);
    test::SetProfileInfo(&profile2, "Charles", "Hardin", "Holley",
                         "buddy@gmail.com", "Decca", "123 Apple St.", "unit 6",
                         "Lubbock", "Texas", "79401", "US", "23456789012");
    profile2.set_guid(MakeGuid(2));
    profile2.set_use_date(base::Time::Now() - base::Days(1));
    personal_data().AddProfile(profile2);

    AutofillProfile profile3(
        i18n_model_definition::kLegacyHierarchyCountryCode);
    test::SetProfileInfo(&profile3, "", "", "", "", "", "", "", "", "", "", "",
                         "");
    profile3.set_guid(MakeGuid(3));
    profile3.set_use_date(base::Time::Now());
    personal_data().AddProfile(profile3);
  }

  void CreateTestCreditCards() {
    CreditCard credit_card1;
    test::SetCreditCardInfo(&credit_card1, "Elvis Presley",
                            "4234567890123456",  // Visa
                            "04", "2999", "1");
    credit_card1.set_guid(MakeGuid(4));
    credit_card1.set_use_count(10);
    credit_card1.set_use_date(AutofillClock::Now() - base::Days(5));
    personal_data().AddCreditCard(credit_card1);

    CreditCard credit_card2;
    test::SetCreditCardInfo(&credit_card2, "Buddy Holly",
                            "5187654321098765",  // Mastercard
                            "10", "2998", "1");
    credit_card2.set_guid(MakeGuid(5));
    credit_card2.set_use_count(5);
    credit_card2.set_use_date(AutofillClock::Now() - base::Days(4));
    personal_data().AddCreditCard(credit_card2);

    CreditCard credit_card3;
    test::SetCreditCardInfo(&credit_card3, "", "", "08", "2999", "");
    credit_card3.set_guid(MakeGuid(6));
    personal_data().AddCreditCard(credit_card3);
  }
};

class SuggestionMatchingTest : public BrowserAutofillManagerTest,
                               public testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    BrowserAutofillManagerTest::SetUp();
    InitializeFeatures();
  }
  bool IsMetadataEnabled() const { return GetParam(); }
  void InitializeFeatures();

  base::test::ScopedFeatureList features_;
};

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
void SuggestionMatchingTest::InitializeFeatures() {}
#else
void SuggestionMatchingTest::InitializeFeatures() {
  features_.InitWithFeatureStates(
      {{features::kAutofillEnableVirtualCardMetadata, IsMetadataEnabled()},
       {features::kAutofillEnableCardProductName, IsMetadataEnabled()},
       {features::kAutofillEnableCardArtImage, IsMetadataEnabled()}});
}
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)

// Credit card suggestion tests related with keyboard accessory.
class CreditCardSuggestionTest : public BrowserAutofillManagerTest {
 protected:
  void SetUp() override {
    BrowserAutofillManagerTest::SetUp();
    feature_list_card_metadata_and_product_name_.InitWithFeatures(
        /* enabled_features */ {},
        /* disabled_features */ {features::kAutofillEnableVirtualCardMetadata,
                                 features::kAutofillEnableCardProductName});
  }

 private:
  base::test::ScopedFeatureList feature_list_card_metadata_and_product_name_;
};

// Test that calling OnFormsSeen with an empty set of forms (such as when
// reloading a page or when the renderer processes a set of forms but detects
// no changes) does not load the forms again.
TEST_F(BrowserAutofillManagerTest, OnFormsSeen_Empty) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();

  base::HistogramTester histogram_tester;
  FormsSeen({form});
  histogram_tester.ExpectUniqueSample("Autofill.UserHappiness",
                                      0 /* FORMS_LOADED */, 1);

  // No more forms, metric is not logged.
  FormsSeen({});
  histogram_tester.ExpectUniqueSample("Autofill.UserHappiness",
                                      0 /* FORMS_LOADED */, 1);
}

// Test that calling OnFormsSeen consecutively with a different set of forms
// will query for each separately.
TEST_F(BrowserAutofillManagerTest, OnFormsSeen_DifferentFormStructures) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormData form2;
  form2.host_frame = test::MakeLocalFrameToken();
  form2.unique_renderer_id = test::MakeFormRendererId();
  form2.name = u"MyForm";
  form2.url = GURL("https://myform.com/form.html");
  form2.action = GURL("https://myform.com/submit.html");
  form2.fields = {
      CreateTestFormField("First Name", "firstname", "",
                          FormControlType::kInputText),
      CreateTestFormField("Last Name", "lastname", "",
                          FormControlType::kInputText),
      CreateTestFormField("Email", "email", "", FormControlType::kInputText)};

  EXPECT_CALL(*crowdsourcing_manager(), StartQueryRequest).Times(AnyNumber());
  EXPECT_CALL(
      *crowdsourcing_manager(),
      StartQueryRequest(
          ElementsAre(FormStructureHasRendererId(form.unique_renderer_id)), _,
          _));
  EXPECT_CALL(
      *crowdsourcing_manager(),
      StartQueryRequest(
          ElementsAre(FormStructureHasRendererId(form2.unique_renderer_id)), _,
          _));

  base::HistogramTester histogram_tester;
  FormsSeen({form});
  histogram_tester.ExpectUniqueSample("Autofill.UserHappiness",
                                      0 /* FORMS_LOADED */, 1);

  FormsSeen({form2});
  histogram_tester.ExpectUniqueSample("Autofill.UserHappiness",
                                      0 /* FORMS_LOADED */, 2);
}

// Tests that only fields from `field_types_to_fill` are filled.
TEST_F(BrowserAutofillManagerTest,
       FillingDetails_FieldTypesToFill_FillOnlySpecificFields) {
  base::test::ScopedFeatureList enabled_features(
      features::kAutofillGranularFillingAvailable);
  // Set up our form data.
  FormData form = test::GetFormData(
      {.fields = {
           {.role = NAME_FIRST, .autocomplete_attribute = "given-name"},
           {.role = NAME_LAST, .autocomplete_attribute = "family-name"}}});
  FormsSeen({form});
  // Only `NAME_FIRST` fields should be filled.
  FieldTypeSet target_fields = FieldTypeSet({NAME_FIRST});
  FormData response_data = FillAutofillFormDataAndGetResults(
      form, form.fields[0], MakeGuid(1),
      {.trigger_source = AutofillTriggerSource::kPopup,
       .field_types_to_fill = target_fields});

  ASSERT_EQ(response_data.fields.size(), 2u);
  // The city field was filled.
  EXPECT_EQ(response_data.fields[0].name, u"firstName");
  EXPECT_EQ(response_data.fields[0].value,
            UTF8ToUTF16(GetElvisAddressFillData().first));
  // The city field was NOT filled.
  EXPECT_EQ(response_data.fields[1].name, u"lastName");
  EXPECT_EQ(response_data.fields[1].value, u"");
}

// Test that when forms are seen, the renderer is updated with the predicted
// field types
TEST_F(BrowserAutofillManagerTest,
       OnFormsSeen_SendAutofillTypePredictionsToRenderer) {
  // Set up a queryable form.
  FormData form1 = CreateTestAddressFormData();

  // Set up a non-queryable form.
  FormData form2;
  form2.host_frame = test::MakeLocalFrameToken();
  form2.unique_renderer_id = test::MakeFormRendererId();
  form2.name = u"NonQueryable";
  form2.url = form1.url;
  form2.action = GURL("https://myform.com/submit.html");
  form2.fields = {
      CreateTestFormField("Querty", "qwerty", "", FormControlType::kInputText)};

  // Package the forms for observation.

  // Setup expectations.
  EXPECT_CALL(*autofill_driver_, SendAutofillTypePredictionsToRenderer(_))
      .Times(2);
  FormsSeen({form1, form2});
}

// Test that no autofill suggestions are returned for a field with an
// unrecognized autocomplete attribute on desktop.
// On mobile, the keyboard accessory is shown unconditionally.
TEST_F(BrowserAutofillManagerTest,
       GetProfileSuggestions_UnrecognizedAttribute) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      // Set a valid autocomplete attribute for the first name.
      CreateTestFormField("First name", "firstname", "",
                          FormControlType::kInputText, "given-name"),
      // Set no autocomplete attribute for the middle name.
      CreateTestFormField("Middle name", "middle", "",
                          FormControlType::kInputText, ""),
      // Set an unrecognized autocomplete attribute for the last name.
      CreateTestFormField("Last Name", "lastname", "",
                          FormControlType::kInputText, "unrecognized")};
  FormsSeen({form});

  // Ensure that the SingleFieldFormFillRouter is not called for
  // suggestions either.
  EXPECT_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions)
      .Times(0);

  // Suggestions should be returned for the first two fields.
  GetAutofillSuggestions(form, form.fields[0]);
  external_delegate()->CheckSuggestionCount(form.fields[0].global_id(), 3);
  GetAutofillSuggestions(form, form.fields[1]);
  external_delegate()->CheckSuggestionCount(form.fields[1].global_id(), 3);

  GetAutofillSuggestions(form, form.fields[2]);
  // For the third field, suggestions should only be shown on mobile due to the
  // unrecognized autocomplete attribute.
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  external_delegate()->CheckSuggestionCount(form.fields[2].global_id(), 3);
#else
  external_delegate()->CheckNoSuggestions(form.fields[2].global_id());
#endif
}

// Tests that ac=unrecognized fields only activate suggestions when triggered
// through manual fallbacks (even though the field has a type in both cases) on
// desktop.
// On mobile, suggestions are shown even for ac=unrecognized fields.
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
TEST_F(BrowserAutofillManagerTest,
       GetProfileSuggestions_UnrecognizedAttribute_Predictions_Mobile) {
  // Create a form where the first field has ac=unrecognized.
  FormData form = CreateTestAddressFormData();
  form.fields[0].parsed_autocomplete =
      AutocompleteParsingResult{.field_type = HtmlFieldType::kUnrecognized};
  FormsSeen({form});

  // Expect that two suggestions + footer are returned for all fields,
  // independently of the autocomplete attribute. Two, because the fixture
  // created three profiles during set up, one of which is empty and cannot be
  // suggested (see `CreateTestAutofillProfiles()`).
  for (const FormFieldData& field : form.fields) {
    GetAutofillSuggestions(form, field);
    external_delegate()->CheckSuggestionCount(field.global_id(), 3);
  }
}
#else
TEST_F(BrowserAutofillManagerTest,
       AutofillManualFallback_UnclassifiedField_SuggestionsShown) {
  // Create a form where the first field is unclassifiable.
  FormData form = CreateTestAddressFormData();
  form.fields[0].label = u"unclassified";
  form.fields[0].name = u"unclassified";
  FormsSeen({form});

  // Expect that no suggestions are returned for the first field.
  const FormFieldData& first_field = form.fields[0];
  GetAutofillSuggestions(form, first_field);
  external_delegate()->CheckSuggestionsNotReturned(first_field.global_id());

  // Expect that no suggestions are returned because the field is unclassified.
  // TODO(crbug.com/1493361): Revisit when address suggestions are generated for
  // unclassified fields.
  GetAutofillSuggestions(
      form, first_field,
      AutofillSuggestionTriggerSource::kManualFallbackAddress);
  external_delegate()->CheckSuggestionsNotReturned(first_field.global_id());
  // Expect 3 credit card suggestions + footer because the fixture created 3
  // credit cards during setup (see `CreateTestCreditCards()`).
  GetAutofillSuggestions(
      form, first_field,
      AutofillSuggestionTriggerSource::kManualFallbackPayments);
  external_delegate()->CheckSuggestionCount(first_field.global_id(), 4);
}

TEST_F(BrowserAutofillManagerTest,
       AutofillManualFallback_AutocompleteUnrecognized_SuggestionsShown) {
  // Create a form where the first field has ac=unrecognized.
  FormData form = CreateTestAddressFormData();
  form.fields[0].parsed_autocomplete =
      AutocompleteParsingResult{.field_type = HtmlFieldType::kUnrecognized};
  FormsSeen({form});

  // Expect that no suggestions are returned for the first field by default.
  const FormFieldData& first_field = form.fields[0];
  GetAutofillSuggestions(form, first_field);
  external_delegate()->CheckNoSuggestions(first_field.global_id());

  // Expect 2 address suggestions + footer because the fixture created three
  // profiles during set up, one of which is empty and cannot be suggested
  // (see `CreateTestAutofillProfiles()`).
  GetAutofillSuggestions(
      form, first_field,
      AutofillSuggestionTriggerSource::kManualFallbackAddress);
  external_delegate()->CheckSuggestionCount(first_field.global_id(), 3);
  // Expect 4 credit card suggestions + footer because the fixture created 3
  // credit cards during setup (see `CreateTestCreditCards()`).
  GetAutofillSuggestions(
      form, first_field,
      AutofillSuggestionTriggerSource::kManualFallbackPayments);
  external_delegate()->CheckSuggestionCount(first_field.global_id(), 4);

  // Expect that two address suggestions + footer are returned for all other
  // fields.
  for (size_t i = 1; i < form.fields.size(); i++) {
    GetAutofillSuggestions(form, form.fields[i]);
    external_delegate()->CheckSuggestionCount(form.fields[i].global_id(), 3);
  }
}

TEST_F(BrowserAutofillManagerTest,
       AutofillManualFallback_ClassifiedField_AddressForm_ShowSuggestions) {
  // Create a form where all fields can be classified.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  for (const auto& field : form.fields) {
    // Expect 2 address suggestions + footer because the fixture created three
    // profiles during set up, one of which is empty and cannot be suggested
    // (see `CreateTestAutofillProfiles()`).
    GetAutofillSuggestions(
        form, field, AutofillSuggestionTriggerSource::kManualFallbackAddress);
    external_delegate()->CheckSuggestionCount(field.global_id(), 3);
    base::ranges::all_of(
        external_delegate()->suggestions(), [](const Suggestion& suggestion) {
          return suggestion.popup_item_id == PopupItemId::kAddressEntry;
        });
    // Expect 3 credit card suggestions + footer because the fixture created 3
    // credit cards during setup (see `CreateTestCreditCards()`).
    GetAutofillSuggestions(
        form, field, AutofillSuggestionTriggerSource::kManualFallbackPayments);
    external_delegate()->CheckSuggestionCount(field.global_id(), 4);
    EXPECT_TRUE(base::ranges::all_of(
        external_delegate()->suggestions(), [](const Suggestion& suggestion) {
          return suggestion.popup_item_id != PopupItemId::kAddressEntry ||
                 !suggestion.is_acceptable;
        }));
  }
}

TEST_F(BrowserAutofillManagerTest,
       AutofillManualFallback_ClassifiedField_PaymentsForm_ShowSuggestions) {
  // Create a form where all fields can be classified.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  // TODO(crbug.com/1493361): add expectation on the address manual fallback
  // suggestions shown after the address suggestions are generated for
  // unclassified fields.
  const FormFieldData& cc_name_field = form.fields[0];
  // Expect 2 credit card suggestions + footer because manual fallback flow
  // triggered on a classified credit card field should generate regular
  // suggestions.
  GetAutofillSuggestions(
      form, cc_name_field,
      AutofillSuggestionTriggerSource::kManualFallbackPayments);
  external_delegate()->CheckSuggestionCount(cc_name_field.global_id(), 3);
  EXPECT_EQ(external_delegate()->GetMainFillingProduct(),
            FillingProduct::kCreditCard);
}
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)

// Test that when small forms are disabled (min required fields enforced) no
// suggestions are returned when there are less than three fields and none of
// them have an autocomplete attribute.
TEST_F(BrowserAutofillManagerTest,
       GetProfileSuggestions_MinFieldsEnforced_NoAutocomplete) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {CreateTestFormField("First Name", "firstname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Last Name", "lastname", "",
                                     FormControlType::kInputText)};

  FormsSeen({form});

  // Ensure that the SingleFieldFormFillRouter is called for both fields.
  EXPECT_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions)
      .Times(2);

  GetAutofillSuggestions(form, form.fields[0]);
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());

  GetAutofillSuggestions(form, form.fields[1]);
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

// Test that when small forms are disabled (min required fields enforced)
// for a form with two fields with one that has an autocomplete attribute,
// suggestions are only made for the one that has the attribute.
TEST_F(BrowserAutofillManagerTest,
       GetProfileSuggestions_MinFieldsEnforced_WithOneAutocomplete) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {CreateTestFormField("First Name", "firstname", "",
                                     FormControlType::kInputText, "given-name"),
                 CreateTestFormField("Last Name", "lastname", "",
                                     FormControlType::kInputText, "")};

  FormsSeen({form});

  // Check that suggestions are made for the field that has the autocomplete
  // attribute.
  GetAutofillSuggestions(form, form.fields[0]);
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion("Charles", "", Suggestion::Icon::kNoIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("Elvis", "", Suggestion::Icon::kNoIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});

  // Check that there are no suggestions for the field without the autocomplete
  // attribute.
  GetAutofillSuggestions(form, form.fields[1]);
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

// Test that for a form with two fields with autocomplete attributes,
// suggestions are made for both fields. This is true even if a minimum number
// of fields is enforced.
TEST_F(BrowserAutofillManagerTest,
       GetProfileSuggestions_SmallFormWithTwoAutocomplete) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      CreateTestFormField("First Name", "firstname", "",
                          FormControlType::kInputText, "given-name"),
      CreateTestFormField("Last Name", "lastname", "",
                          FormControlType::kInputText, "family-name")};

  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[0]);
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion("Charles", "Charles Hardin Holley", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("Elvis", "Elvis Aaron Presley", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});

  GetAutofillSuggestions(form, form.fields[1]);
  external_delegate()->CheckSuggestions(
      form.fields[1].global_id(),
      {Suggestion("Holley", "Charles Hardin Holley", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("Presley", "Elvis Aaron Presley", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that the call is properly forwarded to its SingleFieldFormFillRouter.
TEST_F(BrowserAutofillManagerTest, OnSingleFieldSuggestionSelected) {
  std::u16string test_value = u"TestValue";
  FormData form = CreateTestAddressFormData();
  FormFieldData field = form.fields[0];

  EXPECT_CALL(single_field_form_fill_router(),
              OnSingleFieldSuggestionSelected(test_value,
                                              PopupItemId::kAutocompleteEntry))
      .Times(1);

  browser_autofill_manager_->OnSingleFieldSuggestionSelected(
      test_value, PopupItemId::kAutocompleteEntry, form, field);

  EXPECT_CALL(single_field_form_fill_router(),
              OnSingleFieldSuggestionSelected(test_value,
                                              PopupItemId::kAutocompleteEntry))
      .Times(1);

  browser_autofill_manager_->OnSingleFieldSuggestionSelected(
      test_value, PopupItemId::kAutocompleteEntry, form, field);

  EXPECT_CALL(
      single_field_form_fill_router(),
      OnSingleFieldSuggestionSelected(test_value, PopupItemId::kIbanEntry))
      .Times(1);

  browser_autofill_manager_->OnSingleFieldSuggestionSelected(
      test_value, PopupItemId::kIbanEntry, form, field);

  EXPECT_CALL(single_field_form_fill_router(),
              OnSingleFieldSuggestionSelected(
                  test_value, PopupItemId::kMerchantPromoCodeEntry))
      .Times(1);

  browser_autofill_manager_->OnSingleFieldSuggestionSelected(
      test_value, PopupItemId::kMerchantPromoCodeEntry, form, field);
}

// Tests that BrowserAutofillManager correctly returns virtual cards with usage
// data and VCN last four for a standalone cvc field.
TEST_F(BrowserAutofillManagerTest, GetVirtualCreditCardsForStandaloneCvcField) {
  base::test::ScopedFeatureList feature(
      features::kAutofillParseVcnCardOnFileStandaloneCvcFields);

  // Set up four_digit_combinations_in_dom, essentially mocking logic in
  // AutofillAgent.
  std::vector<std::string> combinations = {"1234"};
  test_api(*browser_autofill_manager_)
      .SetFourDigitCombinationsInDOM(combinations);

  // Set up virtual card usage data and credit cards.
  personal_data().ClearCreditCards();
  CreditCard masked_server_card = test::GetVirtualCard();
  masked_server_card.set_guid("1234");
  VirtualCardUsageData virtual_card_usage_data =
      test::GetVirtualCardUsageData1();
  masked_server_card.set_instrument_id(
      *virtual_card_usage_data.instrument_id());

  // Add credit card and usage data to personal data manager.
  personal_data().AddVirtualCardUsageData(virtual_card_usage_data);
  personal_data().AddServerCreditCard(masked_server_card);

  // Call GetCreditCardsForStandaloneCvcField, should return credit card.
  base::flat_map<std::string, VirtualCardUsageData::VirtualCardLastFour>
      matches = test_api(*browser_autofill_manager_)
                    .GetVirtualCreditCardsForStandaloneCvcField(
                        virtual_card_usage_data.merchant_origin());

  ASSERT_EQ(matches.size(), 1u);
  EXPECT_EQ(matches[masked_server_card.guid()],
            virtual_card_usage_data.virtual_card_last_four());
}

// Test that we return all address profile suggestions when all form fields
// are empty.
TEST_P(SuggestionMatchingTest, GetProfileSuggestions_EmptyValue) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[0]);
  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion("Charles", "123 Apple St.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("Elvis", "3734 Elvis Presley Blvd.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return only matching address profile suggestions when the
// selected form field has been partially filled out.
TEST_P(SuggestionMatchingTest, GetProfileSuggestions_MatchCharacter) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  FormFieldData field = CreateTestFormField("First Name", "firstname", "E",
                                            FormControlType::kInputText);
  GetAutofillSuggestions(form, field);
  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      field.global_id(),
      {Suggestion("Elvis", "3734 Elvis Presley Blvd.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Tests that we return address profile suggestions values when the section
// is already autofilled, and that we merge identical values.
TEST_P(SuggestionMatchingTest,
       GetProfileSuggestions_AlreadyAutofilledMergeValues) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // First name is already autofilled which will make the section appear as
  // "already autofilled".
  form.fields[0].is_autofilled = true;

  // Two profiles have the same last name, and the third shares the same first
  // letter for last name.
  AutofillProfile profile1(i18n_model_definition::kLegacyHierarchyCountryCode);
  profile1.set_guid(MakeGuid(103));
  profile1.set_use_date(base::Time::Now() - base::Days(2));
  profile1.SetInfo(NAME_FIRST, u"Robin", "en-US");
  profile1.SetInfo(NAME_LAST, u"Grimes", "en-US");
  profile1.SetInfo(ADDRESS_HOME_LINE1, u"1234 Smith Blvd.", "en-US");
  personal_data().AddProfile(profile1);

  AutofillProfile profile2(i18n_model_definition::kLegacyHierarchyCountryCode);
  profile2.set_guid(MakeGuid(124));
  profile2.set_use_date(base::Time::Now() - base::Days(1));
  profile2.SetInfo(NAME_FIRST, u"Carl", "en-US");
  profile2.SetInfo(NAME_LAST, u"Grimes", "en-US");
  profile2.SetInfo(ADDRESS_HOME_LINE1, u"1234 Smith Blvd.", "en-US");
  personal_data().AddProfile(profile2);

  AutofillProfile profile3(i18n_model_definition::kLegacyHierarchyCountryCode);
  profile3.set_guid(MakeGuid(126));
  profile3.set_use_date(base::Time::Now());
  profile3.SetInfo(NAME_FIRST, u"Aaron", "en-US");
  profile3.SetInfo(NAME_LAST, u"Googler", "en-US");
  profile3.SetInfo(ADDRESS_HOME_LINE1, u"1600 Amphitheater pkwy", "en-US");
  personal_data().AddProfile(profile3);

  FormFieldData field = CreateTestFormField("Last Name", "lastname", "G",
                                            FormControlType::kInputText);
  GetAutofillSuggestions(form, field);
  external_delegate()->CheckSuggestions(
      field.global_id(),
      {Suggestion("Googler", "1600 Amphitheater pkwy", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("Grimes", "1234 Smith Blvd.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Tests that we return address profile suggestions values when the section
// is already autofilled, and that they have no label.
TEST_P(SuggestionMatchingTest,
       GetProfileSuggestions_AlreadyAutofilledNoLabels) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // First name is already autofilled which will make the section appear as
  // "already autofilled".
  form.fields[0].is_autofilled = true;

  FormFieldData field = CreateTestFormField("First Name", "firstname", "E",
                                            FormControlType::kInputText);
  GetAutofillSuggestions(form, field);
  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      field.global_id(),
      {Suggestion("Elvis", "3734 Elvis Presley Blvd.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return no suggestions when the form has no relevant fields.
TEST_F(BrowserAutofillManagerTest, GetProfileSuggestions_UnknownFields) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      CreateTestFormField("Username", "username", "",
                          FormControlType::kInputText),
      CreateTestFormField("Password", "password", "",
                          FormControlType::kInputPassword),
      CreateTestFormField("Quest", "quest", "", FormControlType::kInputText),
      CreateTestFormField("Color", "color", "", FormControlType::kInputText)};

  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields.back());
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

// Test parameter data for asserting that the expected suggestion types are
// returned when triggering Autofill using manual fallback. Note that the tests
// that use this param are only about manual fallback for fields that are not
// classified as the target `FillingProduct` defined by the chosen
// `manual_fallback_option`. Therefore, manual fallbacks for `ac=unrecognized`
// fields are not covered here.
struct ManualFallbackTestParams {
  const FormType form_type;
  const AutofillSuggestionTriggerSource manual_fallback_option;
  const FillingProduct expected_main_filling_product;
  const std::string test_name;
};

// Test fixture that covers Autofill being triggered from fields that are not
// classified as the target `FillingProduct`. For example, triggering address
// manual fallback from an unclassified field.
class ManualFallbackTest
    : public BrowserAutofillManagerTest,
      public ::testing::WithParamInterface<ManualFallbackTestParams> {
 public:
  FormData GetFormDataFromTestParam() {
    const FormType form_type = GetParam().form_type;
    if (form_type == FormType::kAddressForm) {
      return test::CreateTestAddressFormData();
    } else if (form_type == FormType::kCreditCardForm) {
      return CreateTestCreditCardFormData(/*is_https=*/true,
                                          /*use_month_type=*/false);
    } else {
      CHECK(form_type == FormType::kUnknownFormType);
      return test::GetFormData(
          {.fields = {{.label = u"unclassified", .name = u"unclassified"}}});
    }
  }
};

TEST_P(ManualFallbackTest, ReturnsExpectedSuggestionTypes) {
  base::test::ScopedFeatureList feature(
      features::kAutofillForUnclassifiedFieldsAvailable);

  const FormData form = GetFormDataFromTestParam();
  FormsSeen({form});
  const ManualFallbackTestParams& params = GetParam();

  GetAutofillSuggestions(form, form.fields.back(),
                         params.manual_fallback_option);

  EXPECT_EQ(external_delegate()->GetMainFillingProduct(),
            params.expected_main_filling_product);
}

INSTANTIATE_TEST_SUITE_P(
    BrowserAutofillManagerTest,
    ManualFallbackTest,
    ::testing::ValuesIn(std::vector<ManualFallbackTestParams>(
        {// Tests that address suggestions are rendered when address manual
         // fallback is triggered on an unclassified field.
         {.form_type = FormType::kUnknownFormType,
          .manual_fallback_option =
              AutofillSuggestionTriggerSource::kManualFallbackAddress,
          .expected_main_filling_product = FillingProduct::kAddress,
          .test_name = "_UnclassifiedField_AddressFallback"},
         // Tests that address suggestions are rendered when address manual
         // fallback is
         // triggered on a credit card field.
         {.form_type = FormType::kCreditCardForm,
          .manual_fallback_option =
              AutofillSuggestionTriggerSource::kManualFallbackAddress,
          .expected_main_filling_product = FillingProduct::kAddress,

          .test_name = "_CreditCardField_AddressFallback"},
         // Tests that payments suggestions are rendered when payments manual
         // fallback is triggered on an unclassified field.
         {.form_type = FormType::kUnknownFormType,
          .manual_fallback_option =
              AutofillSuggestionTriggerSource::kManualFallbackPayments,
          .expected_main_filling_product = FillingProduct::kCreditCard,
          .test_name = "_UnclassifiedField_CreditCard"},
         // Tests that payments suggestions are rendered when payments manual
         // fallback is
         // triggered on an address field.
         {.form_type = FormType::kAddressForm,
          .manual_fallback_option =
              AutofillSuggestionTriggerSource::kManualFallbackPayments,
          .expected_main_filling_product = FillingProduct::kCreditCard,
          .test_name = "_AddressField_CreditCard"}})),
    [](const ::testing::TestParamInfo<ManualFallbackTest::ParamType>& info) {
      return info.param.test_name;
    });

// Test that we call duplicate profile suggestions.
TEST_P(SuggestionMatchingTest, GetProfileSuggestions_WithDuplicates) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // Add a duplicate profile.
  AutofillProfile duplicate_profile =
      *personal_data().GetProfileByGUID(MakeGuid(1));
  personal_data().AddProfile(duplicate_profile);

  GetAutofillSuggestions(form, form.fields[0]);

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion("Charles", "123 Apple St.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("Elvis", "3734 Elvis Presley Blvd.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return no suggestions when autofill is disabled.
TEST_F(BrowserAutofillManagerTest,
       GetProfileSuggestions_AutofillDisabledByUser) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // Disable Autofill.
  browser_autofill_manager_->SetAutofillProfileEnabled(autofill_client_, false);
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              false);

  GetAutofillSuggestions(form, form.fields[0]);
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

TEST_F(BrowserAutofillManagerTest,
       OnSuggestionsReturned_CallsExternalDelegate) {
  FormData form = CreateTestAddressFormData();
  form.fields = {CreateTestFormField("Some Field", "somefield", "",
                                     FormControlType::kInputText)};
  FormsSeen({form});

  std::vector<Suggestion> suggestions = {Suggestion(u"one"),
                                         Suggestion(u"two")};

  // Mock returning some autocomplete `suggestions`.
  EXPECT_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions)
      .WillOnce([&](const FormFieldData& field, const AutofillClient& client,
                    SingleFieldFormFiller::OnSuggestionsReturnedCallback
                        on_suggestions_returned,
                    const SuggestionsContext& context) {
        std::move(on_suggestions_returned).Run(field.global_id(), suggestions);
        return true;
      });
  GetAutofillSuggestions(
      form, form.fields[0],
      AutofillSuggestionTriggerSource::kFormControlElementClicked);

  EXPECT_EQ(external_delegate()->trigger_source(),
            AutofillSuggestionTriggerSource::kFormControlElementClicked);
  external_delegate()->CheckSuggestions(form.fields[0].global_id(),
                                        {suggestions[0], suggestions[1]});
}

class BrowserAutofillManagerTestForMetadataCardSuggestions
    : public BrowserAutofillManagerTest,
      public testing::WithParamInterface<bool> {
 public:
  BrowserAutofillManagerTestForMetadataCardSuggestions() {
    if (IsMetadataEnabled()) {
      card_metadata_flags_.InitWithFeatures(
          /*enabled_features=*/{features::kAutofillEnableVirtualCardMetadata,
                                features::kAutofillEnableCardProductName,
                                features::kAutofillEnableCardArtImage},
          /*disabled_features=*/{});
    } else {
      card_metadata_flags_.InitWithFeatures(
          /*enabled_features=*/{},
          /*=disabled_features=*/{features::kAutofillEnableVirtualCardMetadata,
                                  features::kAutofillEnableCardProductName,
                                  features::kAutofillEnableCardArtImage});
    }
  }

  bool IsMetadataEnabled() const { return GetParam(); }

 private:
  base::test::ScopedFeatureList card_metadata_flags_;
};

INSTANTIATE_TEST_SUITE_P(All,
                         BrowserAutofillManagerTestForMetadataCardSuggestions,
                         ::testing::Bool());

// Test that we return all credit card profile suggestions when all form fields
// are empty.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_EmptyValue) {
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[1]);

  // Test that we sent the credit card suggestions to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[1].global_id(),
      {GetCardSuggestion(kVisaCard), GetCardSuggestion(kMasterCard),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return all credit card profile suggestions when the triggering
// field has whitespace in it.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_Whitespace) {
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  FormFieldData field = form.fields[1];
  field.value = u"       ";
  GetAutofillSuggestions(form, field);

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      field.global_id(),
      {GetCardSuggestion(kVisaCard), GetCardSuggestion(kMasterCard),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return all credit card profile suggestions when the triggering
// field has stop characters in it, which should be removed.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_StopCharsOnly) {
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  FormFieldData field = form.fields[1];
  field.value = u"____-____-____-____";
  GetAutofillSuggestions(form, field);
  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      field.global_id(),
      {GetCardSuggestion(kVisaCard), GetCardSuggestion(kMasterCard),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return all credit card profile suggestions when the triggering
// field has some invisible unicode characters in it.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_InvisibleUnicodeOnly) {
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  FormFieldData field = form.fields[1];
  field.value = std::u16string({0x200E, 0x200F});
  GetAutofillSuggestions(form, field);

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      field.global_id(),
      {GetCardSuggestion(kVisaCard), GetCardSuggestion(kMasterCard),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return all credit card profile suggestions when the triggering
// field has stop characters in it and some input.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_StopCharsWithInput) {
  // Add a credit card with particular numbers that we will attempt to recall.
  CreditCard credit_card;
  test::SetCreditCardInfo(&credit_card, "John Smith",
                          "5255667890168765",  // Mastercard
                          "10", "2098", "1");
  credit_card.set_guid(MakeGuid(7));
  personal_data().AddCreditCard(credit_card);

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  FormFieldData field = form.fields[1];
  field.value = u"5255-66__-____-____";
  GetAutofillSuggestions(form, field);

  // Test that we sent the right value to the external delegate.
  external_delegate()->CheckSuggestions(
      field.global_id(), {GetCardSuggestion(kMasterCard),
                          AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return only matching credit card profile suggestions when the
// selected form field has been partially filled out.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_MatchCharacter) {
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  FormFieldData field = CreateTestFormField("Card Number", "cardnumber", "78",
                                            FormControlType::kInputText);
  GetAutofillSuggestions(form, field);

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      field.global_id(), {GetCardSuggestion(kVisaCard),
                          AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return credit card profile suggestions when the selected form
// field is the credit card number field.
TEST_F(CreditCardSuggestionTest, GetCreditCardSuggestions_CCNumber) {
  // Set nickname with the corresponding guid of the Mastercard 8765.
  personal_data().SetNicknameForCardWithGUID(MakeGuid(5), kArbitraryNickname);
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  const FormFieldData& credit_card_number_field = form.fields[1];
  GetAutofillSuggestions(form, credit_card_number_field);
  const std::string visa_value =
      std::string("Visa  ") +
      test::ObfuscatedCardDigitsAsUTF8(
          "3456", ObfuscationLengthForCreditCardLastFourDigits());
  // Mastercard has a valid nickname. Display nickname + last four in the
  // suggestion title.
  const std::string master_card_value =
      kArbitraryNickname + "  " +
      test::ObfuscatedCardDigitsAsUTF8(
          "8765", ObfuscationLengthForCreditCardLastFourDigits());

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  const std::string visa_label = std::string("04/99");
  const std::string master_card_label = std::string("10/98");
#else
  const std::string visa_label = std::string("Expires on 04/99");
  const std::string master_card_label = std::string("Expires on 10/98");
#endif

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      credit_card_number_field.global_id(),
      {Suggestion(visa_value, visa_label, Suggestion::Icon::kCardVisa,
                  PopupItemId::kCreditCardEntry),
       Suggestion(master_card_value, master_card_label,
                  Suggestion::Icon::kCardMasterCard,
                  PopupItemId::kCreditCardEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return credit card profile suggestions when the selected form
// field is not the credit card number field.
TEST_F(CreditCardSuggestionTest, GetCreditCardSuggestions_NonCCNumber) {
  // Set nickname with the corresponding guid of the Mastercard 8765.
  personal_data().SetNicknameForCardWithGUID(MakeGuid(5), kArbitraryNickname);
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  const FormFieldData& cardholder_name_field = form.fields[0];
  GetAutofillSuggestions(form, cardholder_name_field);

  const std::string obfuscated_last_four_digits1 =
      test::ObfuscatedCardDigitsAsUTF8(
          "3456", ObfuscationLengthForCreditCardLastFourDigits());
  const std::string obfuscated_last_four_digits2 =
      test::ObfuscatedCardDigitsAsUTF8(
          "8765", ObfuscationLengthForCreditCardLastFourDigits());

#if BUILDFLAG(IS_ANDROID)
  // For Android always show obfuscated last four.
  const std::string visa_label = obfuscated_last_four_digits1;
  // Mastercard has a valid nickname.
  const std::string master_card_label = obfuscated_last_four_digits2;

#elif BUILDFLAG(IS_IOS)
  const std::string visa_label = obfuscated_last_four_digits1;
  const std::string master_card_label = obfuscated_last_four_digits2;

#else
  // If no nickname available, we will show network.
  const std::string visa_label = base::JoinString(
      {"Visa  ", obfuscated_last_four_digits1, ", expires on 04/99"}, "");
  // When nickname is available, show nickname. Otherwise, show network.
  const std::string master_card_label =
      base::JoinString({kArbitraryNickname + "  ", obfuscated_last_four_digits2,
                        ", expires on 10/98"},
                       "");
#endif

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      cardholder_name_field.global_id(),
      {Suggestion("Elvis Presley", visa_label, Suggestion::Icon::kCardVisa,
                  PopupItemId::kCreditCardEntry),
       Suggestion("Buddy Holly", master_card_label,
                  Suggestion::Icon::kCardMasterCard,
                  PopupItemId::kCreditCardEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return a warning explaining that credit card profile suggestions
// are unavailable when the page is secure, but the form action URL is valid but
// not secure.
TEST_F(BrowserAutofillManagerTest,
       GetCreditCardSuggestions_SecureContext_FormActionNotHTTPS) {
  // Set up our form data.
  FormData form = CreateTestCreditCardFormData(/*is_https=*/true, false);
  // However we set the action (target URL) to be HTTP after all.
  form.action = GURL("http://myform.com/submit.html");
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[0]);

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion(l10n_util::GetStringUTF8(IDS_AUTOFILL_WARNING_MIXED_FORM), "",
                  Suggestion::Icon::kNoIcon, PopupItemId::kMixedFormMessage)});

  // Clear the test credit cards and try again -- we should still show the
  // mixed form warning.
  personal_data().ClearCreditCards();
  GetAutofillSuggestions(form, form.fields[0]);
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion(l10n_util::GetStringUTF8(IDS_AUTOFILL_WARNING_MIXED_FORM), "",
                  Suggestion::Icon::kNoIcon, PopupItemId::kMixedFormMessage)});
}

// Test that we return credit card suggestions for secure pages that have an
// empty form action target URL.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_SecureContext_EmptyFormAction) {
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  // Clear the form action.
  form.action = GURL();
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[1]);

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[1].global_id(),
      {GetCardSuggestion(kVisaCard), GetCardSuggestion(kMasterCard),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return credit card suggestions for secure pages that have a
// form action set to "javascript:something".
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_SecureContext_JavascriptFormAction) {
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  // Have the form action be a javascript function (which is a valid URL).
  form.action = GURL("javascript:alert('Hello');");
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[1]);

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[1].global_id(),
      {GetCardSuggestion(kVisaCard), GetCardSuggestion(kMasterCard),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return all credit card suggestions in the case that two cards
// have the same obfuscated number.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_RepeatedObfuscatedNumber) {
  // Add a credit card with the same obfuscated number as Elvis's.
  // |credit_card| will be owned by the mock PersonalDataManager.
  CreditCard credit_card;
  test::SetCreditCardInfo(&credit_card, "Elvis Presley",
                          "5255667890168765",  // Mastercard
                          "10", "2098", "1");
  credit_card.set_guid(MakeGuid(7));
  credit_card.set_use_date(AutofillClock::Now() - base::Days(15));
  personal_data().AddCreditCard(credit_card);

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[1]);

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[1].global_id(),
      {GetCardSuggestion(kVisaCard), GetCardSuggestion(kMasterCard),
       GetCardSuggestion(kMasterCard),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that a masked server card is not suggested if more than six digits
// have been typed in the field.
TEST_F(BrowserAutofillManagerTest,
       GetCreditCardSuggestions_MaskedCardWithMoreThan6Digits) {
  // Add a masked server card.
  personal_data().ClearCreditCards();

  CreditCard masked_server_card;
  test::SetCreditCardInfo(&masked_server_card, "Elvis Presley",
                          "4234567890123456",  // Visa
                          "04", "2999", "1");
  masked_server_card.set_guid(MakeGuid(7));
  masked_server_card.set_record_type(CreditCard::RecordType::kMaskedServerCard);
  personal_data().AddServerCreditCard(masked_server_card);
  EXPECT_EQ(1U, personal_data().GetCreditCards().size());

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  FormFieldData field = form.fields[1];
  field.value = u"12345678";
  GetAutofillSuggestions(form, field);

  external_delegate()->CheckNoSuggestions(field.global_id());
}

// Test that expired cards are ordered by their ranking score and are always
// suggested after non expired cards even if they have a higher ranking score.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_ExpiredCards) {
  personal_data().ClearCreditCards();

  // Add a never used non expired credit card.
  CreditCard credit_card0("002149C1-EE28-4213-A3B9-DA243FFF021B",
                          test::kEmptyOrigin);
  test::SetCreditCardInfo(&credit_card0, "Bonnie Parker",
                          "5105105105108765" /* Mastercard */, "10", "2098",
                          "1");
  credit_card0.set_guid(MakeGuid(1));
  personal_data().AddCreditCard(credit_card0);

  // Add an expired card with a higher ranking score.
  CreditCard credit_card1("287151C8-6AB1-487C-9095-28E80BE5DA15",
                          test::kEmptyOrigin);
  test::SetCreditCardInfo(&credit_card1, "Clyde Barrow",
                          "378282246310005" /* American Express */, "04",
                          "2010", "1");
  credit_card1.set_guid(MakeGuid(2));
  credit_card1.set_use_count(300);
  credit_card1.set_use_date(AutofillClock::Now() - base::Days(10));
  personal_data().AddCreditCard(credit_card1);

  // Add an expired card with a lower ranking score.
  CreditCard credit_card2("1141084B-72D7-4B73-90CF-3D6AC154673B",
                          test::kEmptyOrigin);
  credit_card2.set_use_count(3);
  credit_card2.set_use_date(AutofillClock::Now() - base::Days(1));
  test::SetCreditCardInfo(&credit_card2, "John Dillinger",
                          "4234567890123456" /* Visa */, "04", "2011", "1");
  credit_card2.set_guid(MakeGuid(3));
  personal_data().AddCreditCard(credit_card2);

  ASSERT_EQ(3U, personal_data().GetCreditCards().size());

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});
  GetAutofillSuggestions(form, form.fields[1]);

  Suggestion visa_suggestion = GenerateSuggestionFromCardDetails(
      kVisaCard, Suggestion::Icon::kCardVisa, "3456", "04/11");
  Suggestion amex_suggestion = GetCardSuggestion(kAmericanExpressCard);
  Suggestion mastercard_suggestion = GetCardSuggestion(kMasterCard);

  // Test that we sent the credit card suggestions to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[1].global_id(),
      {mastercard_suggestion, amex_suggestion, visa_suggestion,
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test cards that are expired AND disused are suppressed when suppression is
// enabled and the input field is empty.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_SuppressDisusedCreditCardsOnEmptyField) {
  personal_data().ClearCreditCards();
  ASSERT_EQ(0U, personal_data().GetCreditCards().size());

  // Add a never used non expired local credit card.
  CreditCard credit_card0(MakeGuid(0), test::kEmptyOrigin);
  test::SetCreditCardInfo(&credit_card0, "Bonnie Parker",
                          "5105105105105100" /* Mastercard */, "04", "2999",
                          "1");
  personal_data().AddCreditCard(credit_card0);

  auto now = AutofillClock::Now();

  // Add an expired local card last used 10 days ago
  CreditCard credit_card1(MakeGuid(1), test::kEmptyOrigin);
  test::SetCreditCardInfo(&credit_card1, "Clyde Barrow",
                          "4234567890123456" /* Visa */, "04", "2010", "1");
  credit_card1.set_use_date(now - base::Days(10));
  personal_data().AddCreditCard(credit_card1);

  // Add an expired local card last used 180 days ago.
  CreditCard credit_card2(MakeGuid(2), test::kEmptyOrigin);
  credit_card2.set_use_date(now - base::Days(182));
  test::SetCreditCardInfo(&credit_card2, "John Dillinger",
                          "378282246310005" /* American Express */, "01",
                          "2010", "1");
  personal_data().AddCreditCard(credit_card2);

  ASSERT_EQ(3U, personal_data().GetCreditCards().size());

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  // Query with empty string only returns card0 and card1. Note expired
  // masked card2 is not suggested on empty fields.
  {
    GetAutofillSuggestions(form, form.fields[0]);

    external_delegate()->CheckSuggestions(
        form.fields[0].global_id(),
        {Suggestion("Bonnie Parker", GenerateLabelsFromCreditCard(credit_card0),
                    Suggestion::Icon::kCardMasterCard,
                    PopupItemId::kCreditCardEntry),
         Suggestion("Clyde Barrow", GenerateLabelsFromCreditCard(credit_card1),
                    Suggestion::Icon::kCardVisa, PopupItemId::kCreditCardEntry),
         AutofillSuggestionGenerator::CreateSeparator()});
  }

  // Query with name prefix for card0 returns card0.
  {
    FormFieldData field = form.fields[0];
    field.value = u"B";
    GetAutofillSuggestions(form, field);

    external_delegate()->CheckSuggestions(
        form.fields[0].global_id(),
        {Suggestion("Bonnie Parker", GenerateLabelsFromCreditCard(credit_card0),
                    Suggestion::Icon::kCardMasterCard,
                    PopupItemId::kCreditCardEntry),
         AutofillSuggestionGenerator::CreateSeparator()});
  }

  // Query with name prefix for card1 returns card1.
  {
    FormFieldData field = form.fields[0];
    field.value = u"Cl";
    GetAutofillSuggestions(form, field);

    external_delegate()->CheckSuggestions(
        form.fields[0].global_id(),
        {Suggestion("Clyde Barrow", GenerateLabelsFromCreditCard(credit_card1),
                    Suggestion::Icon::kCardVisa, PopupItemId::kCreditCardEntry),
         AutofillSuggestionGenerator::CreateSeparator()});
  }

  // Query with name prefix for card2 returns card2.
  {
    FormFieldData field = form.fields[0];
    field.value = u"Jo";
    GetAutofillSuggestions(form, field);

    external_delegate()->CheckSuggestions(
        form.fields[0].global_id(),
        {Suggestion("John Dillinger",
                    GenerateLabelsFromCreditCard(credit_card2),
                    Suggestion::Icon::kCardAmericanExpress,
                    PopupItemId::kCreditCardEntry),
         AutofillSuggestionGenerator::CreateSeparator()});
  }
}

// Test that a card that doesn't have a number is not shown in the
// suggestions when querying credit cards by their number, and is shown when
// querying other fields.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_NumberMissing) {
  // Create one normal credit card and one credit card with the number
  // missing.
  personal_data().ClearCreditCards();
  ASSERT_EQ(0U, personal_data().GetCreditCards().size());

  CreditCard credit_card0("287151C8-6AB1-487C-9095-28E80BE5DA15",
                          test::kEmptyOrigin);
  test::SetCreditCardInfo(&credit_card0, "Clyde Barrow",
                          "378282246310005" /* American Express */, "04",
                          "2910", "1");
  credit_card0.set_guid(MakeGuid(1));
  credit_card0.set_use_date(base::Time::Now() - base::Days(1));
  personal_data().AddCreditCard(credit_card0);

  CreditCard credit_card1("1141084B-72D7-4B73-90CF-3D6AC154673B",
                          test::kEmptyOrigin);
  test::SetCreditCardInfo(&credit_card1, "John Dillinger", "", "01", "2999",
                          "1");
  credit_card1.set_guid(MakeGuid(2));
  personal_data().AddCreditCard(credit_card1);

  ASSERT_EQ(2U, personal_data().GetCreditCards().size());

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  // Query by card number field.
  GetAutofillSuggestions(form, form.fields[1]);

// Sublabel is expiration date when filling card number. The second card
// doesn't have a number so it should not be included in the suggestions.
  external_delegate()->CheckSuggestions(
      form.fields[1].global_id(),
      {GetCardSuggestion(kAmericanExpressCard),
       AutofillSuggestionGenerator::CreateSeparator()});

  // Query by cardholder name field.
  GetAutofillSuggestions(form, form.fields[0]);

  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion("John Dillinger", "", Suggestion::Icon::kCardGeneric,
                  PopupItemId::kCreditCardEntry),
       Suggestion("Clyde Barrow", GenerateLabelsFromCreditCard(credit_card0),
                  Suggestion::Icon::kCardAmericanExpress,
                  PopupItemId::kCreditCardEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we return profile and credit card suggestions for combined forms.
TEST_P(SuggestionMatchingTest, GetAddressAndCreditCardSuggestions) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  CreateTestCreditCardFormData(&form, true, false);
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[0]);
  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion("Charles", "123 Apple St.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("Elvis", "3734 Elvis Presley Blvd.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});

  FormFieldData field = CreateTestFormField("Card Number", "cardnumber", "",
                                            FormControlType::kInputText);
  GetAutofillSuggestions(form, field);

  // Test that we sent the credit card suggestions to the external delegate.
  external_delegate()->CheckSuggestions(
      field.global_id(),
      {GetCardSuggestion(kVisaCard), GetCardSuggestion(kMasterCard),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that for non-https forms with both address and credit card fields, we
// only return address suggestions. Instead of credit card suggestions, we
// should return a warning explaining that credit card profile suggestions are
// unavailable when the form is not https.
TEST_F(BrowserAutofillManagerTest, GetAddressAndCreditCardSuggestionsNonHttps) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  CreateTestCreditCardFormData(&form, false, false);
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[0]);

  // Verify that suggestions are returned.
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());

  FormFieldData field = CreateTestFormField("Card Number", "cardnumber", "",
                                            FormControlType::kInputText);
  GetAutofillSuggestions(form, field);

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      field.global_id(),
      {Suggestion(
          l10n_util::GetStringUTF8(IDS_AUTOFILL_WARNING_INSECURE_CONNECTION),
          "", Suggestion::Icon::kNoIcon,
          PopupItemId::kInsecureContextPaymentDisabledMessage)});

  // Clear the test credit cards and try again -- we shouldn't return a warning.
  personal_data().ClearCreditCards();
  GetAutofillSuggestions(form, field);
  external_delegate()->CheckNoSuggestions(field.global_id());
}

TEST_F(BrowserAutofillManagerTest,
       ShouldShowAddressSuggestionsIfCreditCardAutofillDisabled) {
  base::test::ScopedFeatureList features;
  DisableAutofillViaAblation(features, /*for_addresses=*/false,
                             /*for_credit_cards=*/true);

  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[0]);
  // Verify that suggestions are returned.
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());
}

TEST_F(BrowserAutofillManagerTest,
       ShouldShowCreditCardSuggestionsIfAddressAutofillDisabled) {
  base::test::ScopedFeatureList features;
  DisableAutofillViaAblation(features, /*for_addresses=*/true,
                             /*for_credit_cards=*/false);

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[0]);
  // Verify that suggestions are returned.
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());
}

// Test that the correct section is filled.
TEST_F(BrowserAutofillManagerTest, FillTriggeredSection) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  size_t index_of_trigger_field = form.fields.size();
  base::ranges::move(CreateTestAddressFormData().fields,
                     std::back_inserter(form.fields));
  FormsSeen({form});

  // Check that the form has been parsed into two sections.
  ASSERT_NE(form.fields.size(), 0u);
  ASSERT_EQ(index_of_trigger_field, form.fields.size() / 2);
  {
    FormStructure* form_structure;
    AutofillField* autofill_field;
    bool found = browser_autofill_manager_->GetCachedFormAndField(
        form, form.fields[index_of_trigger_field], &form_structure,
        &autofill_field);
    ASSERT_TRUE(found);
    for (size_t i = 0; i < form.fields.size() / 2; ++i) {
      size_t j = form.fields.size() / 2 + i;
      ASSERT_EQ(form_structure->field(i)->name, form_structure->field(j)->name);
      ASSERT_NE(form_structure->field(i)->section,
                form_structure->field(j)->section);
      ASSERT_TRUE(form_structure->field(i)->SameFieldAs(form.fields[j]));
      ASSERT_TRUE(form_structure->field(j)->SameFieldAs(form.fields[i]));
    }
  }

  AutofillProfile* profile = personal_data().GetProfileByGUID(MakeGuid(1));
  ASSERT_TRUE(profile);
  EXPECT_EQ(1U, profile->use_count());
  EXPECT_NE(base::Time(), profile->use_date());

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, form.fields[index_of_trigger_field], MakeGuid(1));
  // Extract the sections into individual forms to reduce boiler plate code.
  size_t mid = response_data.fields.size() / 2;
  FormData section1 = response_data;
  FormData section2 = response_data;
  section1.fields.erase(section1.fields.begin() + mid, section1.fields.end());
  section2.fields.erase(section2.fields.begin(), section2.fields.end() - mid);
  // First section should be empty, second should be filled.
  ExpectFilledForm(section1, kEmptyAddressFillData,
                   /*card_fill_data=*/std::nullopt);
  ExpectFilledAddressFormElvis(section2, false);
}

MATCHER_P(HasValue, value, "") {
  return arg.value == value;
}

// Test that if the form cache is outdated because a field has changed, filling
// is aborted after that field.
TEST_F(BrowserAutofillManagerTest, DoNotFillIfFormFieldChanged) {
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));

  // Modify `form` so that it doesn't match `form_structure` anymore.
  ASSERT_GE(form.fields.size(), 3u);
  for (auto it = form.fields.begin() + 2; it != form.fields.end(); ++it)
    *it = FormFieldData();

  AutofillProfile* profile =
      personal_data().GetProfileByGUID(kElvisProfileGuid);
  ASSERT_TRUE(profile);

  FormData response_data;
  EXPECT_CALL(*autofill_driver_, ApplyFormAction)
      .WillOnce((DoAll(SaveArg<2>(&response_data),
                       Return(std::vector<FieldGlobalId>{}))));
  test_api(*browser_autofill_manager_)
      .FillOrPreviewDataModelForm(mojom::ActionPersistence::kFill, form,
                                  form.fields.front(), profile, nullptr,
                                  form_structure, autofill_field);
  std::vector<FormFieldData> filled_fields(response_data.fields.begin(),
                                           response_data.fields.begin() + 2);
  std::vector<FormFieldData> skipped_fields(response_data.fields.begin() + 2,
                                            response_data.fields.end());

  EXPECT_THAT(filled_fields, Each(Not(HasValue(u""))));
  EXPECT_THAT(skipped_fields, Each(HasValue(u"")));
}

// Test that if the form cache is outdated because the form has changed, filling
// is aborted because of that change.
TEST_F(BrowserAutofillManagerTest, DoNotFillIfFormChanged) {
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));

  // Modify `form` so that it doesn't match `form_structure` anymore.
  ASSERT_GE(form.fields.size(), 3u);
  form.fields.pop_back();

  AutofillProfile* profile =
      personal_data().GetProfileByGUID(kElvisProfileGuid);
  ASSERT_TRUE(profile);

  FormData response_data;
  EXPECT_CALL(*autofill_driver_, ApplyFormAction).Times(0);
  test_api(*browser_autofill_manager_)
      .FillOrPreviewDataModelForm(mojom::ActionPersistence::kFill, form,
                                  form.fields.front(), profile, nullptr,
                                  form_structure, autofill_field);
}

TEST_F(BrowserAutofillManagerTest, SkipFillIfFieldIsMeaningfullyPreFilled) {
  base::test::ScopedFeatureList placeholders_feature{
      features::kAutofillOverwritePlaceholdersOnly};

  const FieldType kSkippedType = ADDRESS_HOME_LINE1;
  FormData form = test::GetFormData(
      {.fields = {{.role = NAME_FIRST, .value = u"Triggering field (filled)"},
                  {.role = NAME_LAST, .value = u"Placeholder (filled)"},
                  {.role = EMAIL_ADDRESS, .value = u"No data (filled)"},
                  {.role = kSkippedType,
                   .value = u"Meaningfully pre-filled (skipped)"}}});
  FormsSeen({form});

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));
  form_structure->fields()[0]->set_may_use_prefilled_placeholder(false);
  form_structure->fields()[1]->set_may_use_prefilled_placeholder(true);
  form_structure->fields()[3]->set_may_use_prefilled_placeholder(false);

  AutofillProfile* profile =
      personal_data().GetProfileByGUID(kElvisProfileGuid);
  ASSERT_TRUE(profile);

  FormData filled_form;
  EXPECT_CALL(*autofill_driver_, ApplyFormAction)
      .WillOnce((DoAll(SaveArg<2>(&filled_form),
                       Return(std::vector<FieldGlobalId>{}))));
  test_api(*browser_autofill_manager_)
      .FillOrPreviewDataModelForm(mojom::ActionPersistence::kFill, form,
                                  form.fields.front(), profile, nullptr,
                                  form_structure, autofill_field);

  auto expect_hash = [&](const FormFieldData& field,
                         std::optional<size_t> expected_hash) {
    AutofillField* autofill_field = nullptr;
    FormStructure* form_structure = nullptr;
    ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
        form, field, &form_structure, &autofill_field));
    ASSERT_TRUE(autofill_field);
    EXPECT_THAT(
        autofill_field->field_log_events(),
        Contains(VariantWith<FillFieldLogEvent>(Field(
            "value_that_would_have_been_filled_in_a_prefilled_field_hash",
            &FillFieldLogEvent::
                value_that_would_have_been_filled_in_a_prefilled_field_hash,
            testing::Conditional(expected_hash.has_value(),
                                 testing::Optional(expected_hash),
                                 Eq(std::nullopt))))));
  };

  const auto& filled_fields = filled_form.fields;
  EXPECT_TRUE(filled_fields[0].is_autofilled);
  EXPECT_EQ(filled_fields[0].value, profile->GetRawInfo(NAME_FIRST));
  expect_hash(filled_fields[0], std::nullopt);
  EXPECT_TRUE(filled_fields[1].is_autofilled);
  EXPECT_EQ(filled_fields[1].value, profile->GetRawInfo(NAME_LAST));
  expect_hash(filled_fields[1], std::nullopt);
  EXPECT_TRUE(filled_fields[2].is_autofilled);
  EXPECT_EQ(filled_fields[2].value, profile->GetRawInfo(EMAIL_ADDRESS));
  expect_hash(filled_fields[2], std::nullopt);
  EXPECT_FALSE(filled_fields[3].is_autofilled);
  EXPECT_EQ(filled_fields[3].value, form.fields[3].value);
  expect_hash(
      filled_fields[3],
      base::FastHash(base::UTF16ToUTF8(profile->GetRawInfo(kSkippedType))));
}

TEST_F(BrowserAutofillManagerTest,
       SkipAllPreFilledFieldsExceptIfFieldIsAPlaceholder) {
  base::test::ScopedFeatureList placeholders_features;
  placeholders_features.InitWithFeatures(
      {features::kAutofillOverwritePlaceholdersOnly,
       features::kAutofillSkipPreFilledFields},
      {});

  FormData form = test::GetFormData(
      {.fields = {{.role = NAME_FIRST, .value = u"Triggering field (filled)"},
                  {.role = NAME_LAST, .value = u"Placeholder (filled)"},
                  {.role = EMAIL_ADDRESS, .value = u"No data (skipped)"},
                  {.role = ADDRESS_HOME_LINE1,
                   .value = u"No placeholder (skipped)"}}});
  FormsSeen({form});

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));
  form_structure->fields()[0]->set_may_use_prefilled_placeholder(true);
  form_structure->fields()[1]->set_may_use_prefilled_placeholder(true);
  form_structure->fields()[3]->set_may_use_prefilled_placeholder(false);

  AutofillProfile* profile =
      personal_data().GetProfileByGUID(kElvisProfileGuid);
  ASSERT_TRUE(profile);

  FormData filled_form;
  EXPECT_CALL(*autofill_driver_, ApplyFormAction)
      .WillOnce((DoAll(SaveArg<2>(&filled_form),
                       Return(std::vector<FieldGlobalId>{}))));
  test_api(*browser_autofill_manager_)
      .FillOrPreviewDataModelForm(mojom::ActionPersistence::kFill, form,
                                  form.fields.front(), profile, nullptr,
                                  form_structure, autofill_field);

  const auto& filled_fields = filled_form.fields;
  EXPECT_TRUE(filled_fields[0].is_autofilled);
  EXPECT_EQ(filled_fields[0].value, profile->GetRawInfo(NAME_FIRST));
  EXPECT_TRUE(filled_fields[1].is_autofilled);
  EXPECT_EQ(filled_fields[1].value, profile->GetRawInfo(NAME_LAST));
  EXPECT_FALSE(filled_fields[2].is_autofilled);
  EXPECT_EQ(filled_fields[2].value, form.fields[2].value);
  EXPECT_FALSE(filled_fields[3].is_autofilled);
  EXPECT_EQ(filled_fields[3].value, form.fields[3].value);
}

TEST_F(BrowserAutofillManagerTest, UndoSavesFormFillingData) {
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});
  FormStructure* form_structure;
  AutofillField* autofill_field;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));

  base::flat_set<FieldGlobalId> safe_fields{form.fields.front().global_id()};
  EXPECT_CALL(*autofill_driver_, ApplyFormAction)
      .Times(2)
      .WillRepeatedly(Return(safe_fields));

  test_api(*browser_autofill_manager_)
      .FillOrPreviewDataModelForm(
          mojom::ActionPersistence::kFill, form, form.fields.front(),
          personal_data().GetProfiles().front(), /*optional_cvc=*/nullptr,
          form_structure, autofill_field);

  // Undo early returns if it has no filling history for the trigger field,
  // which is initially empty, therefore calling the driver is proof that data
  // was successfully stored.
  browser_autofill_manager_->UndoAutofill(mojom::ActionPersistence::kFill, form,
                                          form.fields.front());
}

TEST_F(BrowserAutofillManagerTest, UndoSavesFieldByFieldFillingData) {
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});
  FormStructure* form_structure;
  AutofillField* autofill_field;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));

  EXPECT_CALL(*autofill_driver_, ApplyFieldAction);
  browser_autofill_manager_->FillOrPreviewField(
      mojom::ActionPersistence::kFill, mojom::TextReplacement::kReplaceAll,
      form, form.fields.front(), u"Test Value",
      PopupItemId::kAddressFieldByFieldFilling);

  // Undo early returns if it has no filling history for the trigger field,
  // which is initially empty, therefore calling the driver is proof that data
  // was successfully stored.
  EXPECT_CALL(*autofill_driver_, ApplyFormAction);
  browser_autofill_manager_->UndoAutofill(mojom::ActionPersistence::kFill, form,
                                          form.fields.front());
}

TEST_F(BrowserAutofillManagerTest, UndoResetsCachedAutofillState) {
  FormData form = CreateTestAddressFormData();
  AutofillField filled_autofill_field(form.fields.front());

  FormFieldData* field_ptr = &form.fields.front();
  AutofillField* autofill_field_ptr = &filled_autofill_field;
  form.fields.front().is_autofilled = false;
  test_api(*browser_autofill_manager_)
      .AddFormFillEntry(base::make_span(&field_ptr, 1u),
                        base::make_span(&autofill_field_ptr, 1u),
                        FillingProduct::kAddress, /*is_refill=*/false);
  form.fields.front().is_autofilled = true;
  FormsSeen({form});

  FormStructure* form_structure;
  AutofillField* autofill_field;
  base::flat_set<FieldGlobalId> safe_fields{form.fields.front().global_id()};
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));
  ASSERT_TRUE(autofill_field->is_autofilled);

  browser_autofill_manager_->UndoAutofill(mojom::ActionPersistence::kFill, form,
                                          form.fields.front());

  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));
  EXPECT_FALSE(autofill_field->is_autofilled);
}

TEST_F(BrowserAutofillManagerTest,
       FillOrPreviewDataModelFormCallsDidFillOrPreviewForm) {
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});
  FormStructure* form_structure;
  AutofillField* autofill_field;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));
  EXPECT_CALL(autofill_client_, DidFillOrPreviewForm);
  test_api(*browser_autofill_manager_)
      .FillOrPreviewDataModelForm(
          mojom::ActionPersistence::kFill, form, form.fields.front(),
          personal_data().GetCreditCards()[0], /*optional_cvc=*/nullptr,
          form_structure, autofill_field);
}

// Test that if the form cache is outdated because a field was removed, filling
// is aborted.
TEST_F(BrowserAutofillManagerTest, DoNotFillIfFormFieldRemoved) {
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));

  // Modify |form| so that it doesn't match |form_structure| anymore.
  ASSERT_GE(form.fields.size(), 2u);
  form.fields.pop_back();

  AutofillProfile* profile = personal_data().GetProfileByGUID(MakeGuid(1));
  ASSERT_TRUE(profile);

  EXPECT_CALL(*autofill_driver_, ApplyFormAction).Times(0);
}

// Tests that BrowserAutofillManager ignores loss of focus events sent from the
// renderer if the renderer did not have a previously-interacted form.
// TODO(crbug.com/1140473): Remove this test when workaround is no longer
// needed.
TEST_F(BrowserAutofillManagerTest,
       ShouldIgnoreLossOfFocusWithNoPreviouslyInteractedForm) {
  FormData form = CreateTestAddressFormData();

  browser_autofill_manager_->UpdatePendingForm(form);
  ASSERT_TRUE(test_api(*browser_autofill_manager_)
                  .pending_form_data()
                  ->SameFormAs(form));

  // Receiving a notification that focus is no longer on the form *without* the
  // renderer having a previously-interacted form should not result in
  // any changes to the pending form.
  browser_autofill_manager_->OnFocusNoLongerOnForm(
      /*had_interacted_form=*/false);
  EXPECT_TRUE(test_api(*browser_autofill_manager_)
                  .pending_form_data()
                  ->SameFormAs(form));
}

TEST_F(BrowserAutofillManagerTest,
       ShouldNotShowCreditCardsSuggestionsIfCreditCardAutofillDisabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  DisableAutofillViaAblation(scoped_feature_list, /*for_addresses=*/false,
                             /*for_credit_cards=*/true);

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[0]);

  // Check that credit card suggestions will not be available.
  external_delegate()->CheckNoSuggestions(form.fields[0].global_id());
}

TEST_F(BrowserAutofillManagerTest,
       ShouldNotShowAddressSuggestionsIfAddressAutofillDisabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  DisableAutofillViaAblation(scoped_feature_list, /*for_addresses=*/true,
                             /*for_credit_cards=*/false);

  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[0]);

  // Check that credit card suggestions will not be available.
  external_delegate()->CheckNoSuggestions(form.fields[0].global_id());
}

struct LogAblationTestParams {
  const char* description;
  // Whether any autofillable data is stored.
  bool run_with_data_on_file = true;
  // If true, the credit card owner name field is filled with value that is not
  // a prefix of any stored credit card and then autofill suggestions are
  // queried a second time.
  bool second_query_for_suggestions_with_typed_prefix = false;
  // Whether the form should be submitted before validating the metrics.
  bool submit_form = true;
};

enum class LogAblationFormType {
  kAddress,
  kPayment,
  kMixed,  // address fields followed by payment fields
};

class BrowserAutofillManagerLogAblationTest
    : public BrowserAutofillManagerTest,
      public testing::WithParamInterface<
          std::tuple<LogAblationTestParams, LogAblationFormType>> {
 public:
  BrowserAutofillManagerLogAblationTest() = default;
  ~BrowserAutofillManagerLogAblationTest() override = default;
};

// Validate that UMA logging works correctly for ablation studies.
INSTANTIATE_TEST_SUITE_P(
    All,
    BrowserAutofillManagerLogAblationTest,
    testing::Combine(
        testing::Values(
            // Test that if autofillable data is stored and the ablation is
            // enabled, we record metrics as expected.
            LogAblationTestParams{.description = "Having data to fill"},
            // Test that if NO autofillable data is stored and the ablation is
            // enabled, we record "UnconditionalAblation" metrics but no
            // "ConditionalAblation" metrics. The latter only recoded on the
            // condition that we have data to fill.
            LogAblationTestParams{.description = "Having NO data to fill",
                                  .run_with_data_on_file = false},
            // In this test we trigger the GetAutofillSuggestions call twice. By
            // the second time the user has typed a value that is not the prefix
            // of any existing autofill data. This means that autofill would not
            // create any suggestions. We still want to consider this a
            // conditional ablation (the condition to have fillable data on file
            // is met).
            LogAblationTestParams{
                .description = "Typed unknown prefix",
                .second_query_for_suggestions_with_typed_prefix = false},
            // Test that the right events are recorded in case the user
            // interacts with a form but does not submit it.
            LogAblationTestParams{.description = "No form submission",
                                  .submit_form = false}),
        testing::Values(LogAblationFormType::kAddress,
                        LogAblationFormType::kPayment,
                        LogAblationFormType::kMixed)));

TEST_P(BrowserAutofillManagerLogAblationTest, TestLogging) {
  const LogAblationTestParams& params = std::get<0>(GetParam());
  LogAblationFormType form_type = std::get<1>(GetParam());

  SCOPED_TRACE(testing::Message() << params.description << " Form type: "
                                  << static_cast<int>(form_type));

  if (!params.run_with_data_on_file) {
    personal_data().ClearAllServerDataForTesting();
    personal_data().ClearAllLocalData();
  }

  base::test::ScopedFeatureList scoped_feature_list;
  DisableAutofillViaAblation(scoped_feature_list, /*for_addresses=*/true,
                             /*for_credit_cards=*/true);
  base::HistogramTester histogram_tester;

  // Set up our form data. In the kMixed case the form will contain the fields
  // of an address form followed by the fields of fields of a payment form. The
  // triggering for autofill suggestions will happen on an address field in this
  // case.
  FormData form;
  if (form_type == LogAblationFormType::kAddress ||
      form_type == LogAblationFormType::kMixed) {
    form = CreateTestAddressFormData();
  }
  if (form_type == LogAblationFormType::kPayment ||
      form_type == LogAblationFormType::kMixed) {
    CreateTestCreditCardFormData(&form, true, false);
  }
  FormsSeen({form});

  // Simulate retrieving autofill suggestions with the first field as a trigger
  // script. This should emit signals that lead to recorded metrics later on.
  FormFieldData field = form.fields[0];
  GetAutofillSuggestions(form, field);

  // Simulate user typing into field (due to the ablation we would not fill).
  field.value = u"Unknown User";
  browser_autofill_manager_->OnTextFieldDidChange(form, field, gfx::RectF(),
                                                  base::TimeTicks::Now());

  if (params.second_query_for_suggestions_with_typed_prefix) {
    // Do another lookup. We won't have any suggestions because they would not
    // be compatible with the "Unknown User" username.
    GetAutofillSuggestions(form, field);
  }

  // Advance time and possibly submit the form.
  base::TimeDelta time_delta = base::Seconds(42);
  task_environment_.FastForwardBy(time_delta);
  if (params.submit_form)
    FormSubmitted(form);

  // Flush FormEventLoggers.
  browser_autofill_manager_->Reset();

  // Validate the recorded metrics.
  std::string form_type_str = (form_type == LogAblationFormType::kAddress ||
                               form_type == LogAblationFormType::kMixed)
                                  ? "Address"
                                  : "CreditCard";

  // If data was on file, we expect conditional ablation metrics.
  if (params.run_with_data_on_file) {
    histogram_tester.ExpectUniqueSample(
        "Autofill.Ablation.FormSubmissionAfterInteraction." + form_type_str +
            ".ConditionalAblation",
        /*sample=*/params.submit_form ? 1 : 0,
        /*expected_bucket_count=*/1);
  } else {
    histogram_tester.ExpectTotalCount(
        "Autofill.Ablation.FormSubmissionAfterInteraction." + form_type_str +
            ".ConditionalAblation",
        /*expected_count=*/0);
  }
  // Only if data was on file an a submission happened, we can record the
  // duration from interaction to submission.
  if (params.run_with_data_on_file && params.submit_form) {
    histogram_tester.ExpectUniqueTimeSample(
        "Autofill.Ablation.FillDurationSinceInteraction." + form_type_str +
            ".ConditionalAblation",
        time_delta,
        /*expected_bucket_count=*/1);
  } else {
    histogram_tester.ExpectTotalCount(
        "Autofill.Ablation.FillDurationSinceInteraction." + form_type_str +
            ".ConditionalAblation",
        /*expected_count=*/0);
  }
  // The unconditional ablation metrics should always be logged as this the
  // ablation study is always enabled.
  histogram_tester.ExpectUniqueSample(
      "Autofill.Ablation.FormSubmissionAfterInteraction." + form_type_str +
          ".UnconditionalAblation",
      /*sample=*/params.submit_form ? 1 : 0,
      /*expected_bucket_count=*/1);
  // Expect a time from interaction to submission the form was submitted.
  if (params.submit_form) {
    histogram_tester.ExpectUniqueTimeSample(
        "Autofill.Ablation.FillDurationSinceInteraction." + form_type_str +
            ".UnconditionalAblation",
        time_delta,
        /*expected_bucket_count=*/1);
  } else {
    histogram_tester.ExpectTotalCount(
        "Autofill.Ablation.FillDurationSinceInteraction." + form_type_str +
            ".UnconditionalAblation",
        /*expected_count=*/0);
  }

  // Ensure that no metrics are recorded for the complementary form type.
  // I.e. if the trigger element is of an address form, the credit card form
  // should have no metrics.
  std::string complementary_form_type_str =
      (form_type == LogAblationFormType::kAddress ||
       form_type == LogAblationFormType::kMixed)
          ? "CreditCard"
          : "Address";
  for (const char* metric :
       {"FormSubmissionAfterInteraction", "FillDurationSinceInteraction"}) {
    for (const char* ablation_type :
         {"UnconditionalAblation", "ConditionalAblation"}) {
      histogram_tester.ExpectTotalCount(
          base::StrCat({"Autofill.Ablation.", metric, ".",
                        complementary_form_type_str.c_str(), ".",
                        ablation_type}),
          /*expected_count=*/0);
    }
  }
}

// Test that we properly match typed values to stored state data.
TEST_F(BrowserAutofillManagerTest, DetermineStateFieldTypeForUpload) {
  test::ClearAlternativeStateNameMapForTesting();
  test::PopulateAlternativeStateNameMapForTesting();

  AutofillProfile profile(i18n_model_definition::kLegacyHierarchyCountryCode);
  test::SetProfileInfo(&profile, "", "", "", "", "", "", "", "", "Bavaria", "",
                       "DE", "");

  const char* const kValidMatches[] = {"by", "Bavaria", "Bayern",
                                       "BY", "B.Y",     "B-Y"};
  for (const char* valid_match : kValidMatches) {
    SCOPED_TRACE(valid_match);
    FormData form;
    form.fields = {CreateTestFormField("Name", "Name", "Test",
                                       FormControlType::kInputText),
                   CreateTestFormField("State", "state", valid_match,
                                       FormControlType::kInputText)};

    FormStructure form_structure(form);
    EXPECT_EQ(form_structure.field_count(), 2U);

    test_api(*browser_autofill_manager_)
        .PreProcessStateMatchingTypes({profile}, &form_structure);
    EXPECT_TRUE(form_structure.field(1)->state_is_a_matching_type());
  }

  const char* const kInvalidMatches[] = {"Garbage", "BYA",   "BYA is a state",
                                         "Bava",    "Empty", ""};
  for (const char* invalid_match : kInvalidMatches) {
    SCOPED_TRACE(invalid_match);
    FormData form;
    form.fields = {CreateTestFormField("Name", "Name", "Test",
                                       FormControlType::kInputText),
                   CreateTestFormField("State", "state", invalid_match,
                                       FormControlType::kInputText)};

    FormStructure form_structure(form);
    EXPECT_EQ(form_structure.field_count(), 2U);

    test_api(*browser_autofill_manager_)
        .PreProcessStateMatchingTypes({profile}, &form_structure);
    EXPECT_FALSE(form_structure.field(1)->state_is_a_matching_type());
  }

  test::PopulateAlternativeStateNameMapForTesting(
      "US", "California",
      {{.canonical_name = "California",
        .abbreviations = {"CA"},
        .alternative_names = {}}});

  test::SetProfileInfo(&profile, "", "", "", "", "", "", "", "", "California",
                       "", "US", "");

  FormData form;
  form.fields = {
      CreateTestFormField("Name", "Name", "Test", FormControlType::kInputText),
      CreateTestFormField("State", "state", "CA", FormControlType::kInputText)};

  FormStructure form_structure(form);
  EXPECT_EQ(form_structure.field_count(), 2U);

  test_api(*browser_autofill_manager_)
      .PreProcessStateMatchingTypes({profile}, &form_structure);
  EXPECT_TRUE(form_structure.field(1)->state_is_a_matching_type());
}

// Ensures that if autofill is disabled but the password manager is enabled,
// Autofill still performs a lookup to the server.
TEST_F(BrowserAutofillManagerTest,
       OnFormsSeen_AutofillDisabledPasswordManagerEnabled) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();

  // Disable autofill and the password manager.
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              false);
  browser_autofill_manager_->SetAutofillProfileEnabled(autofill_client_, false);
  ON_CALL(autofill_client_, IsPasswordManagerEnabled())
      .WillByDefault(Return(false));

  // As neither autofill nor password manager are enabled, the form should
  // not be parsed.
  {
    base::HistogramTester histogram_tester;
    FormsSeen({form});
    EXPECT_EQ(0, histogram_tester.GetBucketCount("Autofill.UserHappiness",
                                                 0 /* FORMS_LOADED */));
  }

  // Now enable the password manager.
  ON_CALL(autofill_client_, IsPasswordManagerEnabled())
      .WillByDefault(Return(true));
  // If the password manager is enabled, that's enough to parse the form.
  {
    base::HistogramTester histogram_tester;
    EXPECT_CALL(*crowdsourcing_manager(), StartQueryRequest).Times(AnyNumber());
    EXPECT_CALL(
        *crowdsourcing_manager(),
        StartQueryRequest(
            ElementsAre(FormStructureHasRendererId(form.unique_renderer_id)), _,
            _));
    FormsSeen({form});
    histogram_tester.ExpectUniqueSample("Autofill.UserHappiness",
                                        0 /* FORMS_LOADED */, 1);
  }
}

// Test that we return normal Autofill suggestions when trying to autofill
// already filled forms.
TEST_P(SuggestionMatchingTest, GetFieldSuggestionsWhenFormIsAutofilled) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // Mark one of the fields as filled.
  form.fields[2].is_autofilled = true;
  GetAutofillSuggestions(form, form.fields[0]);
  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion("Charles", "123 Apple St.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("Elvis", "3734 Elvis Presley Blvd.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// The method `AutofillSuggestionGenerator::GetPrefixMatchedProfiles` prevents
// that Android users see values that would override already filled fields
// due to the narrow surface and a missing preview.
#if !BUILDFLAG(IS_ANDROID)
// Test that we do not return duplicate values drawn from multiple profiles when
// filling an already filled field.
TEST_P(SuggestionMatchingTest, GetFieldSuggestionsWithDuplicateValues) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // |profile| will be owned by the mock PersonalDataManager.
  AutofillProfile profile(i18n_model_definition::kLegacyHierarchyCountryCode);
  test::SetProfileInfo(&profile, "Elvis", "", "", "", "", "", "", "", "", "",
                       "", "");
  profile.set_guid(MakeGuid(101));
  personal_data().AddProfile(profile);

  FormFieldData& field = form.fields[0];
  field.is_autofilled = true;
  field.value = u"Elvis";
  GetAutofillSuggestions(form, field);
  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      field.global_id(),
      {Suggestion("Elvis", "3734 Elvis Presley Blvd.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}
#endif

TEST_F(BrowserAutofillManagerTest,
       GetProfileSuggestions_ForPhonePrefixOrSuffix) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  struct {
    const char* const label;
    const char* const name;
    size_t max_length;
    const char* const autocomplete_attribute;
  } test_fields[] = {{"country code", "country_code", 1, "tel-country-code"},
                     {"area code", "area_code", 3, "tel-area-code"},
                     {"phone", "phone_prefix", 3, "tel-local-prefix"},
                     {"-", "phone_suffix", 4, "tel-local-suffix"},
                     {"Phone Extension", "ext", 5, "tel-extension"}};

  for (const auto& test_field : test_fields) {
    form.fields.push_back(CreateTestFormField(test_field.label, test_field.name,
                                              "", FormControlType::kInputText,
                                              "", test_field.max_length));
  }

  FormsSeen({form});

  personal_data().ClearProfiles();
  AutofillProfile profile(i18n_model_definition::kLegacyHierarchyCountryCode);
  profile.set_guid(MakeGuid(104));
  profile.SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"1800FLOWERS");
  personal_data().AddProfile(profile);

  const FormFieldData& phone_prefix = form.fields[2];
  GetAutofillSuggestions(form, phone_prefix);

  // Test that we sent the right prefix values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[2].global_id(),
      {Suggestion("356", "1800FLOWERS", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});

  const FormFieldData& phone_suffix = form.fields[3];
  GetAutofillSuggestions(form, phone_suffix);

  // Test that we sent the right suffix values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields[3].global_id(),
      {Suggestion("9377", "1800FLOWERS", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Tests that the suggestion consists of phone number without the country code
// when a length limit is imposed in the field due to which filling with
// country code is not possible.
TEST_F(BrowserAutofillManagerTest, GetProfileSuggestions_ForPhoneField) {
  FormData form = CreateTestAddressFormData();
  form.fields[9].max_length = 10;
  FormsSeen({form});

  AutofillProfile profile(i18n_model_definition::kLegacyHierarchyCountryCode);
  profile.set_guid(MakeGuid(103));
  profile.SetInfo(NAME_FULL, u"Natty Bumppo", "en-US");
  profile.SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"+886123456789");
  personal_data().ClearProfiles();
  personal_data().AddProfile(profile);

  GetAutofillSuggestions(form, form.fields[9]);

  external_delegate()->CheckSuggestions(
      form.fields[9].global_id(),
      {Suggestion("123456789", "Natty Bumppo", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Tests that we return email profile suggestions values
// when the email field with username autocomplete attribute exist.
TEST_F(BrowserAutofillManagerTest,
       GetProfileSuggestions_ForEmailFieldWithUserNameAutocomplete) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  struct {
    const char* const label;
    const char* const name;
    size_t max_length;
    const char* const autocomplete_attribute;
  } test_fields[] = {{"First Name", "firstname", 30, "given-name"},
                     {"Last Name", "lastname", 30, "family-name"},
                     {"Email", "email", 30, "username"},
                     {"Password", "password", 30, "new-password"}};

  for (const auto& test_field : test_fields) {
    FormControlType field_type = strcmp(test_field.name, "password") == 0
                                     ? FormControlType::kInputPassword
                                     : FormControlType::kInputText;
    form.fields.push_back(CreateTestFormField(
        test_field.label, test_field.name, "", field_type,
        test_field.autocomplete_attribute, test_field.max_length));
  }

  FormsSeen({form});

  personal_data().ClearProfiles();
  AutofillProfile profile(i18n_model_definition::kLegacyHierarchyCountryCode);
  profile.set_guid(MakeGuid(103));
  profile.SetRawInfo(NAME_FULL, u"Natty Bumppo");
  profile.SetRawInfo(EMAIL_ADDRESS, u"test@example.com");
  personal_data().AddProfile(profile);

  GetAutofillSuggestions(form, form.fields[2]);
  external_delegate()->CheckSuggestions(
      form.fields[2].global_id(),
      {Suggestion("test@example.com", "Natty Bumppo", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that we correctly fill an address form.
TEST_F(BrowserAutofillManagerTest, FillAddressForm) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  AutofillProfile* profile = personal_data().GetProfileByGUID(MakeGuid(1));
  ASSERT_TRUE(profile);
  EXPECT_EQ(1U, profile->use_count());
  const base::Time last_used_date = AutofillClock::Now() - base::Hours(1);
  profile->set_use_date(last_used_date);

  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));
  ExpectFilledAddressFormElvis(response_data, false);

  EXPECT_EQ(2U, profile->use_count());
  EXPECT_LT(last_used_date, profile->use_date());
}

// Tests that `ProfileTokenQuality` is correctly integrated into
// `AutofillProfile` and that on form submit, observations are collected.
TEST_F(BrowserAutofillManagerTest, FillAddressForm_CollectObservations) {
  base::test::ScopedFeatureList profile_token_quality_feature{
      features::kAutofillTrackProfileTokenQuality};
  AutofillProfile* profile =
      personal_data().GetProfileByGUID(kElvisProfileGuid);
  profile->token_quality().disable_randomization_for_testing();

  // Create and fill an address form with profile `kElvisProfileGuid`.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});
  FormData filled_form = FillAutofillFormDataAndGetResults(form, form.fields[0],
                                                           kElvisProfileGuid);

  // Expect that no observations for any of the form's types were collected yet.
  FormStructure* form_structure =
      browser_autofill_manager_->FindCachedFormById(filled_form.global_id());
  EXPECT_TRUE(base::ranges::all_of(
      *form_structure, [&](const std::unique_ptr<AutofillField>& field) {
        return profile->token_quality()
            .GetObservationTypesForFieldType(field->Type().GetStorableType())
            .empty();
      }));

  // Submit the form and expect observations for all of the form's types. This
  // updates the `profile` in `personal_data()`, invalidating the pointer.
  FormSubmitted(filled_form);
  profile = personal_data().GetProfileByGUID(kElvisProfileGuid);
  EXPECT_TRUE(base::ranges::none_of(
      *form_structure, [&](const std::unique_ptr<AutofillField>& field) {
        return profile->token_quality()
            .GetObservationTypesForFieldType(field->Type().GetStorableType())
            .empty();
      }));
}

// ac=unrecognized fields:
// - Are not filled by default.
// - Are filled through manual fallbacks.
TEST_F(BrowserAutofillManagerTest, AutocompleteUnrecognizedFillingBehavior) {
  // Create a form where the middle name field has ac=unrecognized.
  FormData form = CreateTestAddressFormData();
  ASSERT_EQ(form.fields[1].name, u"middlename");
  form.fields[1].parsed_autocomplete =
      AutocompleteParsingResult{.field_type = HtmlFieldType::kUnrecognized};
  FormsSeen({form});

  // Fill the `form` regularly and expect that everything but the middle name
  // gets filled.
  FormData filled_form = FillAutofillFormDataAndGetResults(form, form.fields[0],
                                                           kElvisProfileGuid);
  TestAddressFillData fill_data = GetElvisAddressFillData();
  fill_data.middle = "";
  ExpectFilledForm(filled_form, fill_data, /*card_fill_data=*/std::nullopt);

  // Fill the `form` as-if through manual fallbacks. Expect that every field
  // gets filled.
  EXPECT_CALL(*autofill_driver_, ApplyFormAction)
      .WillOnce(DoAll(SaveArg<2>(&filled_form),
                      Return(base::flat_set<FieldGlobalId>{})));
  browser_autofill_manager_->FillOrPreviewProfileForm(
      mojom::ActionPersistence::kFill, form, form.fields[0],
      *personal_data().GetProfileByGUID(kElvisProfileGuid),
      {.trigger_source = AutofillTriggerSource::kManualFallback});

  ExpectFilledForm(filled_form, GetElvisAddressFillData(),
                   /*card_fill_data=*/std::nullopt);
}

// Tests that fields with unrecognized autocomplete attribute don't contribute
// to key metrics.
TEST_F(BrowserAutofillManagerTest, AutocompleteUnrecognizedFields_KeyMetrics) {
  // Create an address form where field 1 has an unrecognized autocomplete
  // attribute.
  FormData form = CreateTestAddressFormData();
  ASSERT_GE(form.fields.size(), 2u);
  form.fields[1].parsed_autocomplete =
      AutocompleteParsingResult{.field_type = HtmlFieldType::kUnrecognized};

  // Interact with an ac != unrecognized field: Expect key metrics to be
  // emitted. Note that "interacting" means querying suggestions, usually
  // caused by clicking into a field.
  {
    FormsSeen({form});
    GetAutofillSuggestions(form, form.fields[0]);
    FormSubmitted(form);

    base::HistogramTester histogram_tester;
    browser_autofill_manager_->Reset();
    histogram_tester.ExpectTotalCount(
        "Autofill.KeyMetrics.FillingAssistance.Address", 1);
  }

  // Interact with an ac = unrecognized field: Expect no key metric to be
  // emitted.
  {
    FormsSeen({form});
    GetAutofillSuggestions(form, form.fields[1]);
    FormSubmitted(form);

    base::HistogramTester histogram_tester;
    browser_autofill_manager_->Reset();
    histogram_tester.ExpectTotalCount(
        "Autofill.KeyMetrics.FillingAssistance.Address", 0);
  }
}

TEST_F(BrowserAutofillManagerTest,
       OnCreditCardFetchedSuccessfully_LocalCreditCard) {
  const CreditCard local_card = test::GetCreditCard();
  EXPECT_CALL(autofill_client_, OnVirtualCardDataAvailable).Times(0);

  browser_autofill_manager_->OnCreditCardFetchedSuccessfully(local_card);
  EXPECT_THAT(test_api(form_data_importer()).fetched_card_instrument_id(),
              testing::Optional(local_card.instrument_id()));
}

TEST_F(BrowserAutofillManagerTest,
       OnCreditCardFetchedSuccessfully_ServerCreditCard) {
  const CreditCard server_card = test::GetMaskedServerCard();
  EXPECT_CALL(autofill_client_, OnVirtualCardDataAvailable).Times(0);

  browser_autofill_manager_->OnCreditCardFetchedSuccessfully(server_card);
  EXPECT_THAT(test_api(form_data_importer()).fetched_card_instrument_id(),
              testing::Optional(server_card.instrument_id()));
}

TEST_F(BrowserAutofillManagerTest,
       OnCreditCardFetchedSuccessfully_VirtualCreditCard) {
  const CreditCard virtual_card = test::WithCvc(test::GetVirtualCard());
  using Options = VirtualCardManualFallbackBubbleOptions;
  EXPECT_CALL(
      autofill_client_,
      OnVirtualCardDataAvailable(
          AllOf(Field(&Options::masked_card_name,
                      virtual_card.CardNameForAutofillDisplay()),
                Field(&Options::masked_card_number_last_four,
                      virtual_card.ObfuscatedNumberWithVisibleLastFourDigits()),
                Field(&Options::virtual_card_cvc, virtual_card.cvc()),
                Field(&Options::virtual_card, virtual_card))));

  browser_autofill_manager_->OnCreditCardFetchedSuccessfully(virtual_card);
  EXPECT_THAT(test_api(form_data_importer()).fetched_card_instrument_id(),
              testing::Optional(virtual_card.instrument_id()));
}

// Test that we correctly log FIELD_WAS_AUTOFILLED event in UserHappiness.
TEST_F(BrowserAutofillManagerTest, FillCreditCardForm_LogFieldWasAutofill) {
  // Set up our form data.
  FormData form;
  // Construct a form with a 4 fields: cardholder name, card number,
  // expiration date and cvc.
  CreateTestCreditCardFormData(&form, true, true);
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(4));
  // Cardholder name, card number, expiration data were autofilled but cvc was
  // not be autofilled.
  histogram_tester.ExpectBucketCount("Autofill.UserHappiness.CreditCard",
                                     AutofillMetrics::FIELD_WAS_AUTOFILLED, 3);
}

// Test that we correctly fill a credit card form.
TEST_F(BrowserAutofillManagerTest, FillCreditCardForm_Simple) {
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(4));
  ExpectFilledCreditCardFormElvis(response_data, /*has_address_fields=*/false);
}

// Test that whitespace is stripped from the credit card number.
TEST_F(BrowserAutofillManagerTest,
       FillCreditCardForm_StripCardNumberWhitespace) {
  // Same as the SetUp(), but generate Elvis card with whitespace in credit
  // card number.  |credit_card| will be owned by the TestPersonalDataManager.
  personal_data().ClearCreditCards();
  CreditCard credit_card;
  test::SetCreditCardInfo(&credit_card, "Elvis Presley",
                          "4234 5678 9012 3456",  // Visa
                          "04", "2999", "1");
  credit_card.set_guid(MakeGuid(8));
  personal_data().AddCreditCard(credit_card);
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(8));
  ExpectFilledCreditCardFormElvis(response_data, /*has_address_fields=*/false);
}

// Test that separator characters are stripped from the credit card number.
TEST_F(BrowserAutofillManagerTest,
       FillCreditCardForm_StripCardNumberSeparators) {
  // Same as the SetUp(), but generate Elvis card with separator characters in
  // credit card number.  |credit_card| will be owned by the
  // TestPersonalDataManager.
  personal_data().ClearCreditCards();
  CreditCard credit_card;
  test::SetCreditCardInfo(&credit_card, "Elvis Presley",
                          "4234-5678-9012-3456",  // Visa
                          "04", "2999", "1");
  credit_card.set_guid(MakeGuid(9));
  personal_data().AddCreditCard(credit_card);
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(9));
  ExpectFilledCreditCardFormElvis(response_data, /*has_address_fields=*/false);
}

// Test that we correctly fill a credit card form with month input type.
// Test 1 of 4: Empty month, empty year
TEST_F(BrowserAutofillManagerTest, FillCreditCardForm_NoYearNoMonth) {
  personal_data().ClearCreditCards();

  TestCardFillData card_fill_data = kElvisCardFillData;
  card_fill_data.expiration_month = "";
  card_fill_data.expiration_year = "";
  card_fill_data.use_month_type = true;
  CreditCard credit_card = FillDataToCreditCardInfo(card_fill_data);

  credit_card.set_guid(MakeGuid(7));
  personal_data().AddCreditCard(credit_card);
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/true);
  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(7));
  ExpectFilledForm(response_data, /*address_fill_data=*/std::nullopt,
                   card_fill_data);
}

// Test that we correctly fill a credit card form with month input type.
// Test 2 of 4: Non-empty month, empty year
TEST_F(BrowserAutofillManagerTest, FillCreditCardForm_NoYearMonth) {
  personal_data().ClearCreditCards();
  TestCardFillData card_fill_data = kElvisCardFillData;
  card_fill_data.expiration_month = "04";
  card_fill_data.expiration_year = "";
  card_fill_data.use_month_type = true;
  CreditCard credit_card = FillDataToCreditCardInfo(card_fill_data);

  credit_card.set_guid(MakeGuid(7));
  personal_data().AddCreditCard(credit_card);
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/true);
  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(7));
  ExpectFilledForm(response_data, /*address_fill_data=*/std::nullopt,
                   card_fill_data);
}

// Test that we correctly fill a credit card form with month input type.
// Test 3 of 4: Empty month, non-empty year
TEST_F(BrowserAutofillManagerTest, FillCreditCardForm_YearNoMonth) {
  // Same as the SetUp(), but generate 4 credit cards with year month
  // combination.
  personal_data().ClearCreditCards();
  TestCardFillData card_fill_data = kElvisCardFillData;
  card_fill_data.expiration_month = "";
  card_fill_data.expiration_year = "2999";
  card_fill_data.use_month_type = true;
  CreditCard credit_card = FillDataToCreditCardInfo(card_fill_data);
  credit_card.set_guid(MakeGuid(7));
  personal_data().AddCreditCard(credit_card);
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/true);
  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(7));
  ExpectFilledForm(response_data, /*address_fill_data=*/std::nullopt,
                   card_fill_data);
}

// Test that we correctly fill a credit card form with month input type.
// Test 4 of 4: Non-empty month, non-empty year
TEST_F(BrowserAutofillManagerTest, FillCreditCardForm_YearMonth) {
  personal_data().ClearCreditCards();
  TestCardFillData card_fill_data = kElvisCardFillData;
  card_fill_data.expiration_month = "04";
  card_fill_data.expiration_year = "2999";
  card_fill_data.use_month_type = true;
  CreditCard credit_card = FillDataToCreditCardInfo(card_fill_data);
  credit_card.set_guid(MakeGuid(7));
  personal_data().AddCreditCard(credit_card);
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/true);
  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(7));
  ExpectFilledForm(response_data, /*address_fill_data=*/std::nullopt,
                   card_fill_data);
}

// Test that only the first 16 credit card number fields are filled.
TEST_F(BrowserAutofillManagerTest,
       FillOnlyFirstNineteenCreditCardNumberFields) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {CreateTestFormField("Card Name", "cardname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Last Name", "cardlastname", "",
                                     FormControlType::kInputText)};

  // Add 20 credit card number fields with distinct names.
  for (int i = 0; i < 20; i++) {
    std::u16string field_name = u"Card Number " + base::NumberToString16(i + 1);
    form.fields.push_back(
        CreateTestFormField(base::UTF16ToASCII(field_name).c_str(),
                            "cardnumber", "", FormControlType::kInputText));
  }

  form.fields.push_back(
      CreateTestFormField("CVC", "cvc", "", FormControlType::kInputText));

  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(4));
  ExpectFilledField("Card Name", "cardname", "Elvis",
                    FormControlType::kInputText, response_data.fields[0]);
  ExpectFilledField("Last Name", "cardlastname", "Presley",
                    FormControlType::kInputText, response_data.fields[1]);

  // Verify that the first 19 credit card number fields are filled.
  for (int i = 0; i < 19; i++) {
    std::u16string field_name = u"Card Number " + base::NumberToString16(i + 1);
    ExpectFilledField(base::UTF16ToASCII(field_name).c_str(), "cardnumber",
                      "4234567890123456", FormControlType::kInputText,
                      response_data.fields[2 + i]);
  }

  // Verify that the 20th. credit card number field is not filled.
  ExpectFilledField("Card Number 20", "cardnumber", "",
                    FormControlType::kInputText, response_data.fields[21]);

  ExpectFilledField("CVC", "cvc", "", FormControlType::kInputText,
                    response_data.fields[22]);
}

// Test that only the first 16 of identical fields are filled.
TEST_F(BrowserAutofillManagerTest,
       FillOnlyFirstSixteenIdenticalCreditCardNumberFields) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {CreateTestFormField("Card Name", "cardname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Last Name", "cardlastname", "",
                                     FormControlType::kInputText)};

  // Add 20 identical card number fields.
  for (int i = 0; i < 20; i++) {
    form.fields.push_back(CreateTestFormField("Card Number", "cardnumber", "",
                                              FormControlType::kInputText));
  }
  form.fields.push_back(
      CreateTestFormField("CVC", "cvc", "", FormControlType::kInputText));

  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(4));
  ExpectFilledField("Card Name", "cardname", "Elvis",
                    FormControlType::kInputText, response_data.fields[0]);
  ExpectFilledField("Last Name", "cardlastname", "Presley",
                    FormControlType::kInputText, response_data.fields[1]);

  // Verify that the first 19 card number fields are filled.
  for (int i = 0; i < 19; i++) {
    ExpectFilledField("Card Number", "cardnumber", "4234567890123456",
                      FormControlType::kInputText, response_data.fields[2 + i]);
  }
  // Verify that the 20th. card number field is not filled.
  ExpectFilledField("Card Number", "cardnumber", "",
                    FormControlType::kInputText, response_data.fields[21]);

  ExpectFilledField("CVC", "cvc", "", FormControlType::kInputText,
                    response_data.fields[22]);
}

// Test the credit card number is filled correctly into single-digit fields.
TEST_F(BrowserAutofillManagerTest, FillCreditCardNumberIntoSingleDigitFields) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {CreateTestFormField("Card Name", "cardname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Last Name", "cardlastname", "",
                                     FormControlType::kInputText)};

  // Add 20 identical card number fields.
  for (int i = 0; i < 20; i++) {
    FormFieldData field = CreateTestFormField("Card Number", "cardnumber", "",
                                              FormControlType::kInputText);
    field.host_frame = form.host_frame;
    field.host_form_id = form.unique_renderer_id;
    field.max_length = i < 19 ? 1 : std::numeric_limits<int>::max();
    form.fields.push_back(std::move(field));
  }
  form.fields.push_back(
      CreateTestFormField("CVC", "cvc", "", FormControlType::kInputText));

  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(4));
  ExpectFilledField("Card Name", "cardname", "Elvis",
                    FormControlType::kInputText, response_data.fields[0]);
  ExpectFilledField("Last Name", "cardlastname", "Presley",
                    FormControlType::kInputText, response_data.fields[1]);

  // Verify that the first 19 card number fields are filled.
  std::u16string card_number = u"4234567890123456";
  for (unsigned int i = 0; i < 19; i++) {
    ExpectFilledField("Card Number", "cardnumber",
                      i < card_number.length()
                          ? base::UTF16ToASCII(card_number.substr(i, 1)).c_str()
                          : "4234567890123456",
                      FormControlType::kInputText, response_data.fields[2 + i]);
  }

  // Verify that the 20th. card number field is contains the full value.
  ExpectFilledField("Card Number", "cardnumber", "",
                    FormControlType::kInputText, response_data.fields[21]);

  ExpectFilledField("CVC", "cvc", "", FormControlType::kInputText,
                    response_data.fields[22]);
}

// Test that we correctly fill a credit card form with first and last cardholder
// name.
TEST_F(BrowserAutofillManagerTest, FillCreditCardForm_SplitName) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      CreateTestFormField("Card Name", "cardname", "",
                          FormControlType::kInputText),
      CreateTestFormField("Last Name", "cardlastname", "",
                          FormControlType::kInputText),
      CreateTestFormField("Card Number", "cardnumber", "",
                          FormControlType::kInputText),
      CreateTestFormField("CVC", "cvc", "", FormControlType::kInputText)};

  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(4));
  ExpectFilledField("Card Name", "cardname", "Elvis",
                    FormControlType::kInputText, response_data.fields[0]);
  ExpectFilledField("Last Name", "cardlastname", "Presley",
                    FormControlType::kInputText, response_data.fields[1]);
  ExpectFilledField("Card Number", "cardnumber", "4234567890123456",
                    FormControlType::kInputText, response_data.fields[2]);
}

// Test that only filled selection boxes are counted for the type filling limit.
TEST_F(BrowserAutofillManagerTest,
       OnlyCountFilledSelectionBoxesForTypeFillingLimit) {
  test::PopulateAlternativeStateNameMapForTesting(
      "US", "Tennessee",
      {{.canonical_name = "Tennessee",
        .abbreviations = {"TN"},
        .alternative_names = {}}});
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {CreateTestFormField("First Name", "firstname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Middle Name", "middlename", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Last Name", "lastname", "",
                                     FormControlType::kInputText),

                 // Create a selection box for the state that hat the correct
                 // entry to be filled with user data. Note, TN is the official
                 // abbreviation for Tennessee.
                 CreateTestSelectField("State", "state", "", {"AA", "BB", "TN"},
                                       {"AA", "BB", "TN"})};

  // Add 20 selection boxes that can not be filled since the correct entry
  // is missing.
  for (int i = 0; i < 20; i++) {
    form.fields.push_back(CreateTestSelectField(
        "State", "state", "", {"AA", "BB", "CC"}, {"AA", "BB", "CC"}));
  }

  // Add 20 other selection boxes that should be fillable since the correct
  // entry is present.
  for (int i = 0; i < 20; i++) {
    form.fields.push_back(CreateTestSelectField(
        "State", "state", "", {"AA", "BB", "TN"}, {"AA", "BB", "TN"}));
  }

  // Create a selection box for the state that hat the correct entry to be
  // filled with user data. Note, TN is the official abbreviation for Tennessee.
  for (int i = 0; i < 20; ++i) {
    form.fields.push_back(CreateTestSelectField(
        "Country", "country", "", {"DE", "FR", "US"}, {"DE", "FR", "US"}));
  }

  FormsSeen({form});

  TestAddressFillData profile_info_data = GetElvisAddressFillData();
  profile_info_data.company = "1987";
  AutofillProfile profile = FillDataToAutofillProfile(profile_info_data);
  profile.set_guid(MakeGuid(123));
  personal_data().AddProfile(profile);

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(123));

  // Verify the correct filling of the name entries.
  ExpectFilledField("First Name", "firstname", "Elvis",
                    FormControlType::kInputText, response_data.fields[0]);
  ExpectFilledField("Middle Name", "middlename", "Aaron",
                    FormControlType::kInputText, response_data.fields[1]);
  ExpectFilledField("Last Name", "lastname", "Presley",
                    FormControlType::kInputText, response_data.fields[2]);

  // Verify that the first selection box is correctly filled.
  ExpectFilledField("State", "state", "TN", FormControlType::kSelectOne,
                    response_data.fields[3]);

  // Verify that the next 20 selection boxes are not filled.
  for (int i = 0; i < 20; i++) {
    ExpectFilledField("State", "state", "", FormControlType::kSelectOne,
                      response_data.fields[4 + i]);
  }

  // Verify that the remaining selection boxes are correctly filled again
  // because there's no limit on filling ADDRESS_HOME_STATE fields.
  for (int i = 0; i < 20; i++) {
    ExpectFilledField("State", "state", "TN", FormControlType::kSelectOne,
                      response_data.fields[24 + i]);
  }

  // Verify that only the first 9 of the remaining selection boxes are
  // correctly filled due to the limit on filling ADDRESS_HOME_COUNTRY fields.
  for (int i = 0; i < 20; i++) {
    ExpectFilledField("Country", "country", i < 9 ? "US" : "",
                      FormControlType::kSelectOne,
                      response_data.fields[44 + i]);
  }
}

// Test that we correctly fill a combined address and credit card form.
TEST_F(BrowserAutofillManagerTest, FillAddressAndCreditCardForm) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  CreateTestCreditCardFormData(&form, true, false);
  FormsSeen({form});

  // First fill the address data.
  FormData response_data;
  {
    SCOPED_TRACE("Address");
    response_data =
        FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));
    ExpectFilledAddressFormElvis(response_data, true);
  }

  // Now fill the credit card data.
  {
    response_data = FillAutofillFormDataAndGetResults(form, form.fields.back(),
                                                      MakeGuid(4));
    SCOPED_TRACE("Credit card");
    ExpectFilledCreditCardFormElvis(response_data, /*has_address_fields=*/true);
  }
}

// Test parameter data for tests with a simple structure: Create a form,
// autofill it, check that values have been correctly filled.
struct AutofillSimpleFormCase {
  struct FormFieldExpectedData {
    const char* label;
    const char* name;
    const char* value = "";
  };

  const std::string test_name;
  const std::string cc_guid = "";
  const std::string profile_guid = MakeGuid(1);

  const test::FormDescription form_description;
  const std::vector<FormFieldExpectedData> expected_form_fields;
};

class AutofillSimpleFormTest
    : public BrowserAutofillManagerTest,
      public ::testing::WithParamInterface<AutofillSimpleFormCase> {};

const AutofillSimpleFormCase kAutofillSimpleFormCases[] = {
    // Test that a field with an unrecognized autocomplete attribute is not
    // filled.
    {.test_name = "FillAddressForm_UnrecognizedAttribute",
     .form_description =
         {.fields = {{.label = u"First name",
                      .name = u"firstname",
                      .autocomplete_attribute = "given-name"},
                     {.label = u"Middle name", .name = u"middle"},
                     {.label = u"Last name",
                      .name = u"lastname",
                      .autocomplete_attribute = "unrecognized"}}},
     .expected_form_fields =
         {{.label = "First name", .name = "firstname", .value = "Elvis"},
          {.label = "Middle name", .name = "middle", .value = "Aaron"},
          {.label = "Last name", .name = "lastname"}}},

    // Test that non credit card related fields with the autocomplete attribute
    // set to off are filled on all platforms when the feature to autofill all
    // addresses is enabled (default).
    {.test_name = "FillAddressForm_AutocompleteOffNotRespected",
     .form_description =
         {.fields = {{.label = u"First name", .name = u"firstname"},
                     {.label = u"Middle name",
                      .name = u"middle",
                      .should_autocomplete = false},
                     {.label = u"Last name", .name = u"lastname"},
                     {.label = u"Address Line 1",
                      .name = u"addr1",
                      .should_autocomplete = false}}},
     .expected_form_fields =
         {{.label = "First name", .name = "firstname", .value = "Elvis"},
          {.label = "Middle name", .name = "middle", .value = "Aaron"},
          {.label = "Last name", .name = "lastname", .value = "Presley"},
          {.label = "Address Line 1",
           .name = "addr1",
           .value = "3734 Elvis Presley Blvd."}}},

    // Test that a field with a value equal to it's placeholder attribute is
    // filled.
    {.test_name = "FillAddressForm_PlaceholderEqualsValue",
     .form_description = {.fields = {{.label = u"First name",
                                      .name = u"firstname",
                                      .value = u"First Name",
                                      .placeholder = u"First Name"},
                                     {.label = u"Middle name",
                                      .name = u"middle",
                                      .value = u"Middle Name",
                                      .placeholder = u"Middle Name"},
                                     {.label = u"Last name",
                                      .name = u"lastname",
                                      .value = u"Last Name",
                                      .placeholder = u"Last Name"}}},
     .expected_form_fields =
         {{.label = "First name", .name = "firstname", .value = "Elvis"},
          {.label = "Middle name", .name = "middle", .value = "Aaron"},
          {.label = "Last name", .name = "lastname", .value = "Presley"}}},

    // Test that a credit card field with an unrecognized autocomplete attribute
    // gets filled.
    {.test_name = "FillCreditCardForm_UnrecognizedAttribute",
     .cc_guid = MakeGuid(4),
     .profile_guid = "",
     .form_description =
         {.fields = {{.label = u"Name on Card",
                      .name = u"nameoncard",
                      .autocomplete_attribute = "cc-name"},
                     {.label = u"Card Number", .name = u"cardnumber"},
                     {.label = u"Expiration Date",
                      .name = u"ccmonth",
                      .autocomplete_attribute = "unrecognized"}}},
     .expected_form_fields = {{.label = "Name on Card",
                               .name = "nameoncard",
                               .value = "Elvis Presley"},
                              {.label = "Card Number",
                               .name = "cardnumber",
                               .value = "4234567890123456"},
                              {.label = "Expiration Date",
                               .name = "ccmonth",
                               .value = "04/2999"}}},

};

TEST_P(AutofillSimpleFormTest, FillSimpleForm) {
  const AutofillSimpleFormCase& params = GetParam();
  FormData form = test::GetFormData(params.form_description);
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, form.fields[0],
      params.cc_guid.empty() ? params.profile_guid : params.cc_guid);

  ASSERT_EQ(response_data.fields.size(), params.expected_form_fields.size());
  for (size_t i = 0; i < response_data.fields.size(); ++i) {
    SCOPED_TRACE(params.test_name + ", fields expectations");
    const auto& [label, name, value] = params.expected_form_fields[i];
    ExpectFilledField(label, name, value, FormControlType::kInputText,
                      response_data.fields[i]);
  }
}

INSTANTIATE_TEST_SUITE_P(
    BrowserAutofillManagerTest,
    AutofillSimpleFormTest,
    ::testing::ValuesIn(kAutofillSimpleFormCases),
    [](const ::testing::TestParamInfo<AutofillSimpleFormTest::ParamType>&
           info) { return info.param.test_name; });

// Test that credit card fields are filled even if they have the autocomplete
// attribute set to off.
TEST_F(BrowserAutofillManagerTest, FillCreditCardForm_AutocompleteOff) {
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);

  // Set the autocomplete=off on all fields.
  for (FormFieldData field : form.fields) {
    field.should_autocomplete = false;
  }

  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(4));

  // All fields should be filled.
  ExpectFilledCreditCardFormElvis(response_data, /*has_address_fields=*/false);
}

// Test that selecting an expired credit card fills everything except the
// expiration date.
TEST_F(BrowserAutofillManagerTest, FillCreditCardForm_ExpiredCard) {
  personal_data().ClearCreditCards();
  CreditCard expired_card;
  test::SetCreditCardInfo(&expired_card, "Homer Simpson",
                          "4234567890654321",  // Visa
                          "05", "2000", "1");
  expired_card.set_guid(MakeGuid(9));
  personal_data().AddCreditCard(expired_card);

  // Set up the form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  // Create a credit card form.
  std::vector<const char*> kCreditCardTypes = {"Visa", "Mastercard", "AmEx",
                                               "discover"};
  form.fields = {
      CreateTestFormField("Name on Card", "nameoncard", "",
                          FormControlType::kInputText, "cc-name"),
      CreateTestSelectField("Card Type", "cardtype", "", "cc-type",
                            kCreditCardTypes, kCreditCardTypes),
      CreateTestFormField("Card Number", "cardnumber", "",
                          FormControlType::kInputText, "cc-number"),
      CreateTestFormField("Expiration Month", "ccmonth", "",
                          FormControlType::kInputText, "cc-exp-month"),
      CreateTestFormField("Expiration Year", "ccyear", "",
                          FormControlType::kInputText, "cc-exp-year")};
  FormsSeen({form});

  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(9));

  // The credit card name, type and number should be filled.
  ExpectFilledField("Name on Card", "nameoncard", "Homer Simpson",
                    FormControlType::kInputText, response_data.fields[0]);
  ExpectFilledField("Card Type", "cardtype", "Visa",
                    FormControlType::kSelectOne, response_data.fields[1]);
  ExpectFilledField("Card Number", "cardnumber", "4234567890654321",
                    FormControlType::kInputText, response_data.fields[2]);

  // The expiration month and year should not be filled.
  ExpectFilledField("Expiration Month", "ccmonth", "",
                    FormControlType::kInputText, response_data.fields[3]);
  ExpectFilledField("Expiration Year", "ccyear", "",
                    FormControlType::kInputText, response_data.fields[4]);
}

TEST_F(BrowserAutofillManagerTest, PreviewCreditCardForm_VirtualCard) {
  personal_data().ClearCreditCards();
  CreditCard virtual_card = test::GetVirtualCard();
  personal_data().AddServerCreditCard(virtual_card);
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  FormData response_data = PreviewVirtualCardDataAndGetResults(
      mojom::ActionPersistence::kPreview, form, form.fields[1], virtual_card);

  std::u16string expected_cardholder_name = u"Lorem Ipsum";
  // Virtual card number using obfuscated dots only: Virtual card Mastercard
  // ••••4444
  std::u16string expected_card_number =
      u"Virtual card Mastercard  " +
      virtual_card.ObfuscatedNumberWithVisibleLastFourDigits();
  // Virtual card expiration month using obfuscated dots: ••
  std::u16string expected_exp_month =
      CreditCard::GetMidlineEllipsisPlainDots(/*num_dots=*/2);
  // Virtual card expiration year using obfuscated dots: ••••
  std::u16string expected_exp_year =
      CreditCard::GetMidlineEllipsisPlainDots(/*num_dots=*/4);
  // Virtual card cvc using obfuscated dots: •••
  std::u16string expected_cvc =
      CreditCard::GetMidlineEllipsisPlainDots(/*num_dots=*/3);

  EXPECT_EQ(response_data.fields[0].value, expected_cardholder_name);
  EXPECT_EQ(response_data.fields[1].value, expected_card_number);
  EXPECT_EQ(response_data.fields[2].value, expected_exp_month);
  EXPECT_EQ(response_data.fields[3].value, expected_exp_year);
  EXPECT_EQ(response_data.fields[4].value, expected_cvc);
}

// Test that unfocusable fields aren't filled, except for <select> fields (but
// not <selectlist> fields).
TEST_F(BrowserAutofillManagerTest, DoNotFillUnfocusableFieldsExceptForSelect) {
  // Create a form with both focusable and non-focusable fields.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      CreateTestFormField("First Name", "firstname", "",
                          FormControlType::kInputText),
      CreateTestFormField("", "lastname", "", FormControlType::kInputText),
      CreateTestFormField("Postal Code", "postal_code", "",
                          FormControlType::kInputText)};
  form.fields.back().is_focusable = false;

  form.fields.push_back(CreateTestSelectOrSelectListField(
      "Country", "country", "", "", {"CA", "US"}, {"Canada", "United States"},
      FormControlType::kSelectList));
  form.fields.back().is_focusable = false;

  form.fields.push_back(CreateTestSelectOrSelectListField(
      "Country", "country", "", "", {"CA", "US"}, {"Canada", "United States"},
      FormControlType::kSelectList));
  form.fields.push_back(CreateTestSelectOrSelectListField(
      "Country", "country", "", "", {"CA", "US"}, {"Canada", "United States"},
      FormControlType::kSelectOne));
  form.fields.back().is_focusable = false;

  FormsSeen({form});

  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));

  ASSERT_EQ(6U, response_data.fields.size());
  ExpectFilledField("First Name", "firstname", "Elvis",
                    FormControlType::kInputText, response_data.fields[0]);
  ExpectFilledField("", "lastname", "Presley", FormControlType::kInputText,
                    response_data.fields[1]);
  ExpectFilledField("Postal Code", "postal_code", "",
                    FormControlType::kInputText, response_data.fields[2]);
  ExpectFilledField("Country", "country", "", FormControlType::kSelectList,
                    response_data.fields[3]);
  ExpectFilledField("Country", "country", "US", FormControlType::kSelectList,
                    response_data.fields[4]);
  ExpectFilledField("Country", "country", "US", FormControlType::kSelectOne,
                    response_data.fields[5]);
}

// Test that non-focusable field is ignored while inferring boundaries between
// sections, but not filled.
TEST_F(BrowserAutofillManagerTest, FillFormWithNonFocusableFields) {
  bool default_to_city_and_number =
      base::FeatureList::IsEnabled(features::kAutofillDefaultToCityAndNumber);

  // Create a form with both focusable and non-focusable fields.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      CreateTestFormField("First Name", "firstname", "",
                          FormControlType::kInputText),
      CreateTestFormField("", "lastname", "", FormControlType::kInputText),
      CreateTestFormField("", "email", "", FormControlType::kInputText),
      CreateTestFormField("Phone Number", "phonenumber", "",
                          FormControlType::kInputTelephone),
      CreateTestFormField("", "email_", "", FormControlType::kInputText)};
  form.fields.back().is_focusable = false;
  form.fields.push_back(CreateTestFormField("Country", "country", "",
                                            FormControlType::kInputText));

  FormsSeen({form});

  // Fill the form
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));

  // All the visible fields should be filled as all the fields belong to the
  // same logical section.
  ASSERT_EQ(6U, response_data.fields.size());
  ExpectFilledField("First Name", "firstname", "Elvis",
                    FormControlType::kInputText, response_data.fields[0]);
  ExpectFilledField("", "lastname", "Presley", FormControlType::kInputText,
                    response_data.fields[1]);
  ExpectFilledField("", "email", "theking@gmail.com",
                    FormControlType::kInputText, response_data.fields[2]);
  ExpectFilledField("Phone Number", "phonenumber",
                    default_to_city_and_number ? "2345678901" : "12345678901",
                    FormControlType::kInputTelephone, response_data.fields[3]);
  ExpectFilledField("", "email_", "", FormControlType::kInputText,
                    response_data.fields[4]);
  ExpectFilledField("Country", "country", "United States",
                    FormControlType::kInputText, response_data.fields[5]);
}

// Test that we correctly fill a form that has multiple logical sections, e.g.
// both a billing and a shipping address.
TEST_F(BrowserAutofillManagerTest, FillFormWithMultipleSections) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  const size_t kAddressFormSize = form.fields.size();
  base::ranges::move(CreateTestAddressFormData().fields,
                     std::back_inserter(form.fields));
  for (size_t i = kAddressFormSize; i < form.fields.size(); ++i) {
    // Make sure the fields have distinct names.
    form.fields[i].name = form.fields[i].name + u"_";
  }
  FormsSeen({form});

  // Fill the first section.
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));
  {
    SCOPED_TRACE("Address 1");
    // The second address section should be empty.
    ASSERT_EQ(response_data.fields.size(), 2 * kAddressFormSize);
    for (size_t i = kAddressFormSize; i < form.fields.size(); ++i) {
      EXPECT_EQ(std::u16string(), response_data.fields[i].value);
    }

    // The first address section should be filled with Elvis's data.
    response_data.fields.resize(kAddressFormSize);
    ExpectFilledAddressFormElvis(response_data, false);
  }

  // Fill the second section, with the initiating field somewhere in the middle
  // of the section.
  ASSERT_LT(9U, kAddressFormSize);
  response_data = FillAutofillFormDataAndGetResults(
      form, form.fields[kAddressFormSize + 9], MakeGuid(1));
  {
    SCOPED_TRACE("Address 2");
    ASSERT_EQ(response_data.fields.size(), form.fields.size());

    // The first address section should be empty.
    ASSERT_EQ(response_data.fields.size(), 2 * kAddressFormSize);
    for (size_t i = 0; i < kAddressFormSize; ++i) {
      EXPECT_EQ(std::u16string(), response_data.fields[i].value);
    }

    // The second address section should be filled with Elvis's data.
    FormData secondSection = response_data;
    secondSection.fields.erase(secondSection.fields.begin(),
                               secondSection.fields.begin() + kAddressFormSize);
    for (size_t i = 0; i < kAddressFormSize; ++i) {
      // Restore the expected field names.
      std::u16string name = secondSection.fields[i].name;
      std::u16string original_name = name.substr(0, name.size() - 1);
      secondSection.fields[i].name = original_name;
    }
    ExpectFilledAddressFormElvis(secondSection, false);
  }
}

// Test that we correctly fill a form that has author-specified sections, which
// might not match our expected section breakdown.
TEST_F(BrowserAutofillManagerTest, FillFormWithAuthorSpecifiedSections) {
  // Create a form with a billing section and an unnamed section, interleaved.
  // The billing section includes both address and credit card fields.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      CreateTestFormField("", "country", "", FormControlType::kInputText,
                          "section-billing country"),
      CreateTestFormField("", "firstname", "", FormControlType::kInputText,
                          "given-name"),
      CreateTestFormField("", "lastname", "", FormControlType::kInputText,
                          "family-name"),
      CreateTestFormField("", "address", "", FormControlType::kInputText,
                          "section-billing address-line1"),
      CreateTestFormField("", "city", "", FormControlType::kInputText,
                          "section-billing locality"),
      CreateTestFormField("", "state", "", FormControlType::kInputText,
                          "section-billing region"),
      CreateTestFormField("", "zip", "", FormControlType::kInputText,
                          "section-billing postal-code"),
      CreateTestFormField("", "ccname", "", FormControlType::kInputText,
                          "section-billing cc-name"),
      CreateTestFormField("", "ccnumber", "", FormControlType::kInputText,
                          "section-billing cc-number"),
      CreateTestFormField("", "ccexp", "", FormControlType::kInputText,
                          "section-billing cc-exp"),
      CreateTestFormField("", "email", "", FormControlType::kInputText,
                          "email")};

  FormsSeen({form});

  // Fill the unnamed section.
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[1], MakeGuid(1));
  {
    SCOPED_TRACE("Unnamed section");
    EXPECT_EQ(u"MyForm", response_data.name);
    EXPECT_EQ(GURL("https://myform.com/form.html"), response_data.url);
    EXPECT_EQ(GURL("https://myform.com/submit.html"), response_data.action);
    ASSERT_EQ(11U, response_data.fields.size());

    ExpectFilledField("", "country", "", FormControlType::kInputText,
                      response_data.fields[0]);
    ExpectFilledField("", "firstname", "Elvis", FormControlType::kInputText,
                      response_data.fields[1]);
    ExpectFilledField("", "lastname", "Presley", FormControlType::kInputText,
                      response_data.fields[2]);
    ExpectFilledField("", "address", "", FormControlType::kInputText,
                      response_data.fields[3]);
    ExpectFilledField("", "city", "", FormControlType::kInputText,
                      response_data.fields[4]);
    ExpectFilledField("", "state", "", FormControlType::kInputText,
                      response_data.fields[5]);
    ExpectFilledField("", "zip", "", FormControlType::kInputText,
                      response_data.fields[6]);
    ExpectFilledField("", "ccname", "", FormControlType::kInputText,
                      response_data.fields[7]);
    ExpectFilledField("", "ccnumber", "", FormControlType::kInputText,
                      response_data.fields[8]);
    ExpectFilledField("", "ccexp", "", FormControlType::kInputText,
                      response_data.fields[9]);
    ExpectFilledField("", "email", "theking@gmail.com",
                      FormControlType::kInputText, response_data.fields[10]);
  }

  // Fill the address portion of the billing section.
  response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));
  {
    SCOPED_TRACE("Billing address");
    EXPECT_EQ(u"MyForm", response_data.name);
    EXPECT_EQ(GURL("https://myform.com/form.html"), response_data.url);
    EXPECT_EQ(GURL("https://myform.com/submit.html"), response_data.action);
    ASSERT_EQ(11U, response_data.fields.size());

    ExpectFilledField("", "country", "US", FormControlType::kInputText,
                      response_data.fields[0]);
    ExpectFilledField("", "firstname", "", FormControlType::kInputText,
                      response_data.fields[1]);
    ExpectFilledField("", "lastname", "", FormControlType::kInputText,
                      response_data.fields[2]);
    ExpectFilledField("", "address", "3734 Elvis Presley Blvd.",
                      FormControlType::kInputText, response_data.fields[3]);
    ExpectFilledField("", "city", "Memphis", FormControlType::kInputText,
                      response_data.fields[4]);
    ExpectFilledField("", "state", "Tennessee", FormControlType::kInputText,
                      response_data.fields[5]);
    ExpectFilledField("", "zip", "38116", FormControlType::kInputText,
                      response_data.fields[6]);
    ExpectFilledField("", "ccname", "", FormControlType::kInputText,
                      response_data.fields[7]);
    ExpectFilledField("", "ccnumber", "", FormControlType::kInputText,
                      response_data.fields[8]);
    ExpectFilledField("", "ccexp", "", FormControlType::kInputText,
                      response_data.fields[9]);
    ExpectFilledField("", "email", "", FormControlType::kInputText,
                      response_data.fields[10]);
  }

  // Fill the credit card portion of the billing section.
  response_data = FillAutofillFormDataAndGetResults(
      form, form.fields[form.fields.size() - 2], MakeGuid(4));
  {
    SCOPED_TRACE("Credit card");
    EXPECT_EQ(u"MyForm", response_data.name);
    EXPECT_EQ(GURL("https://myform.com/form.html"), response_data.url);
    EXPECT_EQ(GURL("https://myform.com/submit.html"), response_data.action);
    ASSERT_EQ(11U, response_data.fields.size());

    ExpectFilledField("", "country", "", FormControlType::kInputText,
                      response_data.fields[0]);
    ExpectFilledField("", "firstname", "", FormControlType::kInputText,
                      response_data.fields[1]);
    ExpectFilledField("", "lastname", "", FormControlType::kInputText,
                      response_data.fields[2]);
    ExpectFilledField("", "address", "", FormControlType::kInputText,
                      response_data.fields[3]);
    ExpectFilledField("", "city", "", FormControlType::kInputText,
                      response_data.fields[4]);
    ExpectFilledField("", "state", "", FormControlType::kInputText,
                      response_data.fields[5]);
    ExpectFilledField("", "zip", "", FormControlType::kInputText,
                      response_data.fields[6]);
    ExpectFilledField("", "ccname", "Elvis Presley",
                      FormControlType::kInputText, response_data.fields[7]);
    ExpectFilledField("", "ccnumber", "4234567890123456",
                      FormControlType::kInputText, response_data.fields[8]);
    ExpectFilledField("", "ccexp", "04/2999", FormControlType::kInputText,
                      response_data.fields[9]);
    ExpectFilledField("", "email", "", FormControlType::kInputText,
                      response_data.fields[10]);
  }
}

// Test that we correctly fill a form that has a single logical section with
// multiple email address fields.
TEST_F(BrowserAutofillManagerTest, FillFormWithMultipleEmails) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  form.fields.push_back(CreateTestFormField("Confirm email", "email2", "",
                                            FormControlType::kInputText));

  FormsSeen({form});

  // Fill the form.
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));

  // The second email address should be filled.
  EXPECT_EQ(u"theking@gmail.com", response_data.fields.back().value);

  // The remainder of the form should be filled as usual.
  response_data.fields.pop_back();
  ExpectFilledAddressFormElvis(response_data, false);
}

// Test that we correctly fill a previously auto-filled form.
TEST_F(BrowserAutofillManagerTest, FillAutofilledForm) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  // Mark the address fields as autofilled.
  for (auto& field : form.fields) {
    field.is_autofilled = true;
  }

  CreateTestCreditCardFormData(&form, true, false);
  FormsSeen({form});

  // First fill the address data.
  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(1));
  {
    SCOPED_TRACE("Address");
    TestAddressFillData expected_address_fill_data = kEmptyAddressFillData;
    expected_address_fill_data.first = "Elvis";
    ExpectFilledForm(response_data, expected_address_fill_data,
                     kEmptyCardFillData);
  }

  // Now fill the credit card data.
  response_data =
      FillAutofillFormDataAndGetResults(form, form.fields.back(), MakeGuid(4));
  {
    SCOPED_TRACE("Credit card 1");
    ExpectFilledCreditCardFormElvis(response_data, /*has_address_fields=*/true);
  }

  // Now set the credit card fields to also be auto-filled, and try again to
  // fill the credit card data
  for (auto& field : form.fields) {
    field.is_autofilled = true;
  }

  response_data = FillAutofillFormDataAndGetResults(
      form, form.fields[form.fields.size() - 2], MakeGuid(4));
  {
    SCOPED_TRACE("Credit card 2");
    TestCardFillData expected_card_fill_data = kEmptyCardFillData;
    expected_card_fill_data.expiration_year = "2999";
    ExpectFilledForm(response_data, kEmptyAddressFillData,
                     expected_card_fill_data);
  }
}

// Test that we correctly fill a previously partly auto-filled form.
TEST_F(BrowserAutofillManagerTest, FillPartlyAutofilledForm) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  // Mark couple of the address fields as autofilled.
  form.fields[3].is_autofilled = true;
  form.fields[4].is_autofilled = true;
  form.fields[5].is_autofilled = true;
  form.fields[6].is_autofilled = true;
  form.fields[10].is_autofilled = true;

  CreateTestCreditCardFormData(&form, true, false);
  FormsSeen({form});

  // First fill the address data.
  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(1));
  {
    SCOPED_TRACE("Address");
    TestAddressFillData expected_address_fill_data = kEmptyAddressFillData;
    expected_address_fill_data.first = "Elvis";
    expected_address_fill_data.middle = "Aaron";
    expected_address_fill_data.last = "Presley";
    expected_address_fill_data.postal_code = "38116";
    expected_address_fill_data.country = "United States";
    bool default_to_city_and_number =
        base::FeatureList::IsEnabled(features::kAutofillDefaultToCityAndNumber);
    expected_address_fill_data.phone =
        default_to_city_and_number ? "2345678901" : "12345678901";

    ExpectFilledForm(response_data, expected_address_fill_data,
                     kEmptyCardFillData);
  }

  // Now fill the credit card data.
  response_data =
      FillAutofillFormDataAndGetResults(form, form.fields.back(), MakeGuid(4));
  {
    SCOPED_TRACE("Credit card 1");
    ExpectFilledCreditCardFormElvis(response_data, /*has_address_fields=*/true);
  }
}

// Test that we correctly fill a previously partly auto-filled form.
TEST_F(BrowserAutofillManagerTest, FillPartlyManuallyFilledForm) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();

  CreateTestCreditCardFormData(&form, true, false);
  FormsSeen({form});

  // Michael will be overridden with Elvis because Autofill is triggered from
  // the first field.
  form.fields[0].value = u"Michael";
  form.fields[0].properties_mask |= kUserTyped;

  // Jackson will be preserved.
  form.fields[2].value = u"Jackson";
  form.fields[2].properties_mask |= kUserTyped;

  FormsSeen({form});

  // First fill the address data.
  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(1));
  {
    SCOPED_TRACE("Address");
    TestAddressFillData expected_address_fill_data = GetElvisAddressFillData();
    expected_address_fill_data.last = "Jackson";
    ExpectFilledForm(response_data, expected_address_fill_data,
                     kEmptyCardFillData);
  }

  // Now fill the credit card data.
  response_data =
      FillAutofillFormDataAndGetResults(form, form.fields.back(), MakeGuid(4));
  {
    SCOPED_TRACE("Credit card 1");
    TestAddressFillData expected_address_fill_data = kEmptyAddressFillData;
    expected_address_fill_data.first = "Michael";
    expected_address_fill_data.last = "Jackson";
    ExpectFilledForm(response_data, expected_address_fill_data,
                     kElvisCardFillData);
  }
}

// Test that we correctly fill a phone number split across multiple fields.
TEST_F(BrowserAutofillManagerTest, FillPhoneNumber) {
  // In one form, rely on the max length attribute to imply US phone number
  // parts. In the other form, rely on the autocomplete type attribute.
  FormData form_with_us_number_max_length;
  form_with_us_number_max_length.unique_renderer_id =
      test::MakeFormRendererId();
  form_with_us_number_max_length.name = u"MyMaxlengthPhoneForm";
  form_with_us_number_max_length.url =
      GURL("https://myform.com/phone_form.html");
  form_with_us_number_max_length.action =
      GURL("https://myform.com/phone_submit.html");
  FormData form_with_autocompletetype = form_with_us_number_max_length;
  form_with_autocompletetype.unique_renderer_id = test::MakeFormRendererId();
  form_with_autocompletetype.name = u"MyAutocompletetypePhoneForm";

  struct {
    const char* label;
    const char* name;
    size_t max_length;
    const char* autocomplete_attribute;
  } test_fields[] = {{"country code", "country_code", 1, "tel-country-code"},
                     {"area code", "area_code", 3, "tel-area-code"},
                     {"phone", "phone_prefix", 3, "tel-local-prefix"},
                     {"-", "phone_suffix", 4, "tel-local-suffix"},
                     {"Phone Extension", "ext", 3, "tel-extension"}};

  constexpr uint64_t default_max_length = 0;
  for (const auto& test_field : test_fields) {
    FormFieldData field = CreateTestFormField(test_field.label, test_field.name,
                                              "", FormControlType::kInputText,
                                              "", test_field.max_length);
    form_with_us_number_max_length.fields.push_back(field);

    field.max_length = default_max_length;
    field.autocomplete_attribute = test_field.autocomplete_attribute;
    field.parsed_autocomplete =
        ParseAutocompleteAttribute(test_field.autocomplete_attribute);
    form_with_autocompletetype.fields.push_back(field);
  }

  FormsSeen({form_with_us_number_max_length, form_with_autocompletetype});

  // We should be able to fill prefix and suffix fields for US numbers.
  AutofillProfile* work_profile = personal_data().GetProfileByGUID(MakeGuid(2));
  ASSERT_TRUE(work_profile != nullptr);
  work_profile->SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"16505554567");

  std::string guid = work_profile->guid();
  FormData response_data1 = FillAutofillFormDataAndGetResults(
      form_with_us_number_max_length,
      *form_with_us_number_max_length.fields.begin(), guid);

  ASSERT_EQ(5U, response_data1.fields.size());
  EXPECT_EQ(u"1", response_data1.fields[0].value);
  EXPECT_EQ(u"650", response_data1.fields[1].value);
  EXPECT_EQ(u"555", response_data1.fields[2].value);
  EXPECT_EQ(u"4567", response_data1.fields[3].value);
  EXPECT_EQ(std::u16string(), response_data1.fields[4].value);

  FormData response_data2 = FillAutofillFormDataAndGetResults(
      form_with_autocompletetype, *form_with_autocompletetype.fields.begin(),
      guid);

  ASSERT_EQ(5U, response_data2.fields.size());
  EXPECT_EQ(u"1", response_data2.fields[0].value);
  EXPECT_EQ(u"650", response_data2.fields[1].value);
  EXPECT_EQ(u"555", response_data2.fields[2].value);
  EXPECT_EQ(u"4567", response_data2.fields[3].value);
  EXPECT_EQ(std::u16string(), response_data2.fields[4].value);

  // For other countries, fill prefix and suffix fields with best effort.
  work_profile->SetRawInfo(ADDRESS_HOME_COUNTRY, u"GB");
  work_profile->SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"447700954321");
  FormData response_data3 = FillAutofillFormDataAndGetResults(
      form_with_us_number_max_length,
      *form_with_us_number_max_length.fields.begin(), guid);

  ASSERT_EQ(5U, response_data3.fields.size());
  EXPECT_EQ(u"4", response_data3.fields[0].value);
  EXPECT_EQ(u"700", response_data3.fields[1].value);
  EXPECT_EQ(u"95", response_data3.fields[2].value);
  EXPECT_EQ(u"4321", response_data3.fields[3].value);
  EXPECT_EQ(std::u16string(), response_data3.fields[4].value);

  FormData response_data4 = FillAutofillFormDataAndGetResults(
      form_with_autocompletetype, *form_with_autocompletetype.fields.begin(),
      guid);

  ASSERT_EQ(5U, response_data4.fields.size());
  EXPECT_EQ(u"44", response_data4.fields[0].value);
  EXPECT_EQ(u"7700", response_data4.fields[1].value);
  EXPECT_EQ(u"95", response_data4.fields[2].value);
  EXPECT_EQ(u"4321", response_data4.fields[3].value);
  EXPECT_EQ(std::u16string(), response_data4.fields[4].value);
}

TEST_F(BrowserAutofillManagerTest, FillFirstPhoneNumber_ComponentizedNumbers) {
  AutofillProfile* work_profile = personal_data().GetProfileByGUID(MakeGuid(2));
  ASSERT_TRUE(work_profile != nullptr);
  work_profile->SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"16505554567");

  std::string guid = work_profile->guid();

  // Verify only the first complete number is filled when there are multiple
  // componentized number fields.
  FormData form_with_multiple_componentized_phone_fields;
  form_with_multiple_componentized_phone_fields.unique_renderer_id =
      test::MakeFormRendererId();
  form_with_multiple_componentized_phone_fields.url =
      GURL("https://www.foo.com/");

  // Default is zero, have to set to a number autofill can process.
  constexpr uint64_t kMaxLength = 10;
  form_with_multiple_componentized_phone_fields.name =
      u"multiple_componentized_number_fields";
  form_with_multiple_componentized_phone_fields.fields = {
      CreateTestFormField("Full Name", "full_name", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("country code", "country_code", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("area code", "area_code", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("number", "phone_number", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("extension", "extension", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("shipping country code", "shipping_country_code", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("shipping area code", "shipping_area_code", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("shipping number", "shipping_phone_number", "",
                          FormControlType::kInputText, "", kMaxLength)};

  FormsSeen({form_with_multiple_componentized_phone_fields});
  FormData response_data = FillAutofillFormDataAndGetResults(
      form_with_multiple_componentized_phone_fields,
      *form_with_multiple_componentized_phone_fields.fields.begin(), guid);

  // Verify only the first complete set of phone number fields are filled.
  ASSERT_EQ(8U, response_data.fields.size());
  EXPECT_EQ(u"Charles Hardin Holley", response_data.fields[0].value);
  EXPECT_EQ(u"1", response_data.fields[1].value);
  EXPECT_EQ(u"650", response_data.fields[2].value);
  EXPECT_EQ(u"5554567", response_data.fields[3].value);
  EXPECT_EQ(std::u16string(), response_data.fields[4].value);
  EXPECT_EQ(std::u16string(), response_data.fields[5].value);
  EXPECT_EQ(std::u16string(), response_data.fields[6].value);
  EXPECT_EQ(std::u16string(), response_data.fields[7].value);
}

TEST_F(BrowserAutofillManagerTest, FillFirstPhoneNumber_WholeNumbers) {
  AutofillProfile* work_profile = personal_data().GetProfileByGUID(MakeGuid(2));
  ASSERT_TRUE(work_profile != nullptr);
  work_profile->SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"16505554567");

  std::string guid = work_profile->guid();

  FormData form_with_multiple_whole_number_fields;
  form_with_multiple_whole_number_fields.unique_renderer_id =
      test::MakeFormRendererId();
  form_with_multiple_whole_number_fields.url = GURL("https://www.foo.com/");

  // Default is zero, have to set to a number autofill can process.
  constexpr uint64_t kMaxLength = 10;
  form_with_multiple_whole_number_fields.name = u"multiple_whole_number_fields";
  form_with_multiple_whole_number_fields.fields = {
      CreateTestFormField("Full Name", "full_name", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("number", "phone_number", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("extension", "extension", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("shipping number", "shipping_phone_number", "",
                          FormControlType::kInputText, "", kMaxLength)};

  FormsSeen({form_with_multiple_whole_number_fields});
  FormData response_data = FillAutofillFormDataAndGetResults(
      form_with_multiple_whole_number_fields,
      *form_with_multiple_whole_number_fields.fields.begin(), guid);

  // Verify only the first complete set of phone number fields are filled.
  ASSERT_EQ(4U, response_data.fields.size());
  EXPECT_EQ(u"Charles Hardin Holley", response_data.fields[0].value);
  EXPECT_EQ(u"6505554567", response_data.fields[1].value);
  EXPECT_EQ(std::u16string(), response_data.fields[2].value);
  EXPECT_EQ(std::u16string(), response_data.fields[3].value);
}

TEST_F(BrowserAutofillManagerTest, FillFirstPhoneNumber_FillPartsOnceOnly) {
  AutofillProfile* work_profile = personal_data().GetProfileByGUID(MakeGuid(2));
  ASSERT_TRUE(work_profile != nullptr);
  work_profile->SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"16505554567");

  std::string guid = work_profile->guid();

  // Verify only the first complete number is filled when there are multiple
  // componentized number fields.
  FormData form_with_multiple_componentized_phone_fields;
  form_with_multiple_componentized_phone_fields.unique_renderer_id =
      test::MakeFormRendererId();
  form_with_multiple_componentized_phone_fields.url =
      GURL("https://www.foo.com/");

  // Default is zero, have to set to a number autofill can process.
  constexpr uint64_t kMaxLength = 10;
  form_with_multiple_componentized_phone_fields.name =
      u"multiple_componentized_number_fields";
  form_with_multiple_componentized_phone_fields.fields = {
      CreateTestFormField("Full Name", "full_name", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("country code", "country_code", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("area code", "area_code", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("number", "phone_number", "",
                          FormControlType::kInputText, "tel-national",
                          kMaxLength),
      CreateTestFormField("extension", "extension", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("shipping country code", "shipping_country_code", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("shipping area code", "shipping_area_code", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("shipping number", "shipping_phone_number", "",
                          FormControlType::kInputText, "", kMaxLength)};

  FormsSeen({form_with_multiple_componentized_phone_fields});
  FormData response_data = FillAutofillFormDataAndGetResults(
      form_with_multiple_componentized_phone_fields,
      *form_with_multiple_componentized_phone_fields.fields.begin(), guid);

  // Verify only the first complete set of phone number fields are filled,
  // and phone components are not filled more than once.
  ASSERT_EQ(8U, response_data.fields.size());
  EXPECT_EQ(u"Charles Hardin Holley", response_data.fields[0].value);
  EXPECT_EQ(u"1", response_data.fields[1].value);
  EXPECT_EQ(std::u16string(), response_data.fields[2].value);
  EXPECT_EQ(u"6505554567", response_data.fields[3].value);
  EXPECT_EQ(std::u16string(), response_data.fields[4].value);
  EXPECT_EQ(std::u16string(), response_data.fields[5].value);
  EXPECT_EQ(std::u16string(), response_data.fields[6].value);
  EXPECT_EQ(std::u16string(), response_data.fields[7].value);
}

// Verify when extension is misclassified, and there is a complete
// phone field, we do not fill anything to extension field.
TEST_F(BrowserAutofillManagerTest,
       FillFirstPhoneNumber_NotFillMisclassifiedExtention) {
  AutofillProfile* work_profile = personal_data().GetProfileByGUID(MakeGuid(2));
  ASSERT_TRUE(work_profile != nullptr);
  work_profile->SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"16505554567");

  std::string guid = work_profile->guid();

  FormData form_with_misclassified_extension;
  form_with_misclassified_extension.unique_renderer_id =
      test::MakeFormRendererId();
  form_with_misclassified_extension.url = GURL("https://www.foo.com/");

  // Default is zero, have to set to a number autofill can process.
  constexpr uint64_t kMaxLength = 10;
  form_with_misclassified_extension.name =
      u"complete_phone_form_with_extension";
  form_with_misclassified_extension.fields = {
      CreateTestFormField("Full Name", "full_name", "",
                          FormControlType::kInputText, "name", kMaxLength),
      CreateTestFormField("address", "address", "", FormControlType::kInputText,
                          "addresses", kMaxLength),
      CreateTestFormField("area code", "area_code", "",
                          FormControlType::kInputText, "tel-area-code",
                          kMaxLength),
      CreateTestFormField("number", "phone_number", "",
                          FormControlType::kInputText, "tel-local", kMaxLength),
      CreateTestFormField("extension", "extension", "",
                          FormControlType::kInputText, "tel-local",
                          kMaxLength)};

  FormsSeen({form_with_misclassified_extension});
  FormData response_data = FillAutofillFormDataAndGetResults(
      form_with_misclassified_extension,
      *form_with_misclassified_extension.fields.begin(), guid);

  // Verify the misclassified extension field is not filled.
  ASSERT_EQ(5U, response_data.fields.size());
  EXPECT_EQ(u"Charles Hardin Holley", response_data.fields[0].value);
  EXPECT_EQ(std::u16string(), response_data.fields[1].value);
  EXPECT_EQ(u"650", response_data.fields[2].value);
  EXPECT_EQ(u"5554567", response_data.fields[3].value);
  EXPECT_EQ(std::u16string(), response_data.fields[4].value);
}

// Verify that phone number fields annotated with the autocomplete attribute
// are filled best-effort.
// Phone number local heuristics only succeed if a PHONE_HOME_NUMBER field is
// present.
TEST_F(BrowserAutofillManagerTest, FillFirstPhoneNumber_BestEffortFilling) {
  AutofillProfile* work_profile = personal_data().GetProfileByGUID(MakeGuid(2));
  ASSERT_TRUE(work_profile != nullptr);
  work_profile->SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"16505554567");

  std::string guid = work_profile->guid();

  FormData form_with_no_complete_number;
  form_with_no_complete_number.unique_renderer_id = test::MakeFormRendererId();
  form_with_no_complete_number.url = GURL("https://www.foo.com/");

  // Default is zero, have to set to a number autofill can process.
  constexpr uint64_t kMaxLength = 10;
  form_with_no_complete_number.name = u"no_complete_phone_form";
  form_with_no_complete_number.fields = {
      CreateTestFormField("Full Name", "full_name", "",
                          FormControlType::kInputText, "name", kMaxLength),
      CreateTestFormField("address", "address", "", FormControlType::kInputText,
                          "address", kMaxLength),
      CreateTestFormField("area code", "area_code", "",
                          FormControlType::kInputText, "tel-area-code",
                          kMaxLength),
      CreateTestFormField("extension", "extension", "",
                          FormControlType::kInputText, "extension",
                          kMaxLength)};

  FormsSeen({form_with_no_complete_number});
  FormData response_data = FillAutofillFormDataAndGetResults(
      form_with_no_complete_number,
      *form_with_no_complete_number.fields.begin(), guid);

  // Verify when there is no complete phone number fields, we do best effort
  // filling.
  ASSERT_EQ(4U, response_data.fields.size());
  EXPECT_EQ(u"Charles Hardin Holley", response_data.fields[0].value);
  EXPECT_EQ(u"123 Apple St., unit 6", response_data.fields[1].value);
  EXPECT_EQ(u"650", response_data.fields[2].value);
  EXPECT_EQ(std::u16string(), response_data.fields[3].value);
}

// When the focus is on second phone field explicitly, we will fill the
// entire form, both first phone field and second phone field included.
TEST_F(BrowserAutofillManagerTest,
       FillFirstPhoneNumber_FocusOnSecondPhoneNumber) {
  AutofillProfile* work_profile = personal_data().GetProfileByGUID(MakeGuid(2));
  ASSERT_TRUE(work_profile != nullptr);
  work_profile->SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"16505554567");

  std::string guid = work_profile->guid();

  FormData form_with_multiple_whole_number_fields;
  form_with_multiple_whole_number_fields.unique_renderer_id =
      test::MakeFormRendererId();
  form_with_multiple_whole_number_fields.url = GURL("https://www.foo.com/");

  // Default is zero, have to set to a number autofill can process.
  constexpr uint64_t kMaxLength = 10;
  form_with_multiple_whole_number_fields.name = u"multiple_whole_number_fields";
  form_with_multiple_whole_number_fields.fields = {
      CreateTestFormField("Full Name", "full_name", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("number", "phone_number", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("extension", "extension", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("shipping number", "shipping_phone_number", "",
                          FormControlType::kInputText, "", kMaxLength)};

  FormsSeen({form_with_multiple_whole_number_fields});
  auto it = form_with_multiple_whole_number_fields.fields.begin();
  // Move it to point to "shipping number".
  std::advance(it, 3);
  FormData response_data = FillAutofillFormDataAndGetResults(
      form_with_multiple_whole_number_fields, *it, guid);

  // Verify when the second phone number field is being focused, we fill
  // that field *AND* the first phone number field.
  ASSERT_EQ(4U, response_data.fields.size());
  EXPECT_EQ(u"Charles Hardin Holley", response_data.fields[0].value);
  EXPECT_EQ(u"6505554567", response_data.fields[1].value);
  EXPECT_EQ(std::u16string(), response_data.fields[2].value);
  EXPECT_EQ(u"6505554567", response_data.fields[3].value);
}

TEST_F(BrowserAutofillManagerTest,
       FillFirstPhoneNumber_HiddenFieldShouldNotCount) {
  AutofillProfile* work_profile = personal_data().GetProfileByGUID(MakeGuid(2));
  ASSERT_TRUE(work_profile != nullptr);
  work_profile->SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"16505554567");

  std::string guid = work_profile->guid();

  FormData form_with_multiple_whole_number_fields;
  form_with_multiple_whole_number_fields.unique_renderer_id =
      test::MakeFormRendererId();
  form_with_multiple_whole_number_fields.url = GURL("https://www.foo.com/");

  // Default is zero, have to set to a number autofill can process.
  constexpr uint64_t kMaxLength = 10;
  form_with_multiple_whole_number_fields.name = u"multiple_whole_number_fields";
  form_with_multiple_whole_number_fields.fields = {
      CreateTestFormField("Full Name", "full_name", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("number", "phone_number", "",
                          FormControlType::kInputText, "", kMaxLength)};
  form_with_multiple_whole_number_fields.fields.back().is_focusable = false;
  form_with_multiple_whole_number_fields.fields.push_back(
      CreateTestFormField("extension", "extension", "",
                          FormControlType::kInputText, "", kMaxLength));
  form_with_multiple_whole_number_fields.fields.push_back(
      CreateTestFormField("shipping number", "shipping_phone_number", "",
                          FormControlType::kInputText, "", kMaxLength));

  FormsSeen({form_with_multiple_whole_number_fields});
  FormData response_data = FillAutofillFormDataAndGetResults(
      form_with_multiple_whole_number_fields,
      *form_with_multiple_whole_number_fields.fields.begin(), guid);

  // Verify hidden/non-focusable phone field is set to only_fill_when_focused.
  ASSERT_EQ(4U, response_data.fields.size());
  EXPECT_EQ(u"Charles Hardin Holley", response_data.fields[0].value);
  EXPECT_EQ(std::u16string(), response_data.fields[1].value);
  EXPECT_EQ(std::u16string(), response_data.fields[2].value);
  EXPECT_EQ(u"6505554567", response_data.fields[3].value);
}

// The hidden and the presentational fields should be filled, only if their
// control type is 'select-one'. This exception is made to support synthetic
// fields.
TEST_F(BrowserAutofillManagerTest, FormWithHiddenOrPresentationalSelects) {
  FormData form;
  form.unique_renderer_id = test::MakeFormRendererId();
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {CreateTestFormField("First name", "firstname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Last name", "lastname", "",
                                     FormControlType::kInputText)};

  {
    const std::vector<const char*> values{"CA", "US", "BR"};
    const std::vector<const char*> contents{"Canada", "United States",
                                            "Banana Republic"};
    form.fields.push_back(
        CreateTestSelectField("Country", "country", "", values, contents));
    form.fields.back().is_focusable = false;
  }
  {
    const std::vector<const char*> values{"NY", "CA", "TN"};
    const std::vector<const char*> contents{"New York", "California",
                                            "Tennessee"};
    form.fields.push_back(
        CreateTestSelectField("State", "state", "", values, contents));
    form.fields.back().role = FormFieldData::RoleAttribute::kPresentation;
  }

  form.fields.push_back(
      CreateTestFormField("City", "city", "", FormControlType::kInputText));
  form.fields.back().is_focusable = false;
  form.fields.push_back(CreateTestFormField("Street Address", "address", "",
                                            FormControlType::kInputText));
  form.fields.back().role = FormFieldData::RoleAttribute::kPresentation;

  FormsSeen({form});

  base::HistogramTester histogram_tester;

  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));
  histogram_tester.ExpectTotalCount(
      "Autofill.HiddenOrPresentationalSelectFieldsFilled", 2);

  ExpectFilledField("First name", "firstname", "Elvis",
                    FormControlType::kInputText, response_data.fields[0]);
  ExpectFilledField("Last name", "lastname", "Presley",
                    FormControlType::kInputText, response_data.fields[1]);
  ExpectFilledField("Country", "country", "US", FormControlType::kSelectOne,
                    response_data.fields[2]);
  ExpectFilledField("State", "state", "TN", FormControlType::kSelectOne,
                    response_data.fields[3]);
  ExpectFilledField("City", "city", "", FormControlType::kInputText,
                    response_data.fields[4]);
  ExpectFilledField("Street Address", "address", "",
                    FormControlType::kInputText, response_data.fields[5]);
}

TEST_F(BrowserAutofillManagerTest,
       FillFirstPhoneNumber_MultipleSectionFilledCorrectly) {
  AutofillProfile* work_profile = personal_data().GetProfileByGUID(MakeGuid(2));
  ASSERT_TRUE(work_profile != nullptr);
  work_profile->SetRawInfo(PHONE_HOME_WHOLE_NUMBER, u"16505554567");

  std::string guid = work_profile->guid();

  FormData form_with_multiple_sections;
  form_with_multiple_sections.unique_renderer_id = test::MakeFormRendererId();
  form_with_multiple_sections.url = GURL("https://www.foo.com/");

  // Default is zero, have to set to a number autofill can process.
  constexpr uint64_t kMaxLength = 10;
  form_with_multiple_sections.name = u"multiple_section_fields";
  form_with_multiple_sections.fields = {
      CreateTestFormField("Full Name", "full_name", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("Address", "address", "", FormControlType::kInputText,
                          "", kMaxLength),
      CreateTestFormField("number", "phone_number", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("other number", "other_phone_number", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("extension", "extension", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("Full Name", "full_name", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("Shipping Address", "shipping_address", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("shipping number", "shipping_phone_number", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("other shipping number",
                          "other_shipping_phone_number", "",
                          FormControlType::kInputText, "", kMaxLength)};

  FormsSeen({form_with_multiple_sections});
  // Fill first sections.
  FormData response_data = FillAutofillFormDataAndGetResults(
      form_with_multiple_sections, *form_with_multiple_sections.fields.begin(),
      guid);

  // Verify first section is filled with rationalization.
  ASSERT_EQ(9U, response_data.fields.size());
  EXPECT_EQ(u"Charles Hardin Holley", response_data.fields[0].value);
  EXPECT_EQ(u"123 Apple St.", response_data.fields[1].value);
  EXPECT_EQ(u"6505554567", response_data.fields[2].value);
  EXPECT_EQ(std::u16string(), response_data.fields[3].value);
  EXPECT_EQ(std::u16string(), response_data.fields[4].value);
  EXPECT_EQ(std::u16string(), response_data.fields[5].value);
  EXPECT_EQ(std::u16string(), response_data.fields[6].value);
  EXPECT_EQ(std::u16string(), response_data.fields[7].value);
  EXPECT_EQ(std::u16string(), response_data.fields[8].value);

  // Fill second section.
  auto it = form_with_multiple_sections.fields.begin();
  std::advance(it, 6);  // Pointing to second section.

  response_data =
      FillAutofillFormDataAndGetResults(form_with_multiple_sections, *it, guid);

  // Verify second section is filled with rationalization.
  ASSERT_EQ(9U, response_data.fields.size());
  EXPECT_EQ(std::u16string(), response_data.fields[0].value);
  EXPECT_EQ(std::u16string(), response_data.fields[1].value);
  EXPECT_EQ(std::u16string(), response_data.fields[2].value);
  EXPECT_EQ(std::u16string(), response_data.fields[3].value);
  EXPECT_EQ(std::u16string(), response_data.fields[4].value);
  EXPECT_EQ(u"Charles Hardin Holley", response_data.fields[5].value);
  EXPECT_EQ(u"123 Apple St.", response_data.fields[6].value);
  EXPECT_EQ(u"6505554567", response_data.fields[7].value);
  EXPECT_EQ(std::u16string(), response_data.fields[8].value);
}

// Test that we can still fill a form when a field has been removed from it.
TEST_F(BrowserAutofillManagerTest, FormChangesRemoveField) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();

  // Add a field -- we'll remove it again later.
  form.fields.insert(
      form.fields.begin() + 3,
      CreateTestFormField("Some", "field", "", FormControlType::kInputText));

  FormsSeen({form});

  // Now, after the call to |FormsSeen|, we remove the field before filling.
  form.fields.erase(form.fields.begin() + 3);

  FormsSeen({form});

  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));
  ExpectFilledAddressFormElvis(response_data, false);
}

// Test that we can still fill a form when a field has been added to it.
TEST_F(BrowserAutofillManagerTest, FormChangesAddField) {
  // The offset of the phone field in the address form.
  const int kPhoneFieldOffset = 9;

  // Set up our form data.
  FormData form = CreateTestAddressFormData();

  // Remove the phone field -- we'll add it back later.
  auto pos = form.fields.begin() + kPhoneFieldOffset;
  FormFieldData field = *pos;
  pos = form.fields.erase(pos);

  FormsSeen({form});

  // Now, after the call to |FormsSeen|, we restore the field before filling.
  form.fields.insert(pos, field);

  FormsSeen({form});

  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));
  ExpectFilledAddressFormElvis(response_data, false);
}

// Test that we can still fill a form when the visibility of some fields
// changes.
TEST_F(BrowserAutofillManagerTest, FormChangesVisibilityOfFields) {
  // Set up our form data.
  FormData form;
  form.unique_renderer_id = test::MakeFormRendererId();
  form.url = GURL("https://www.foo.com/");

  // Default is zero, have to set to a number autofill can process.
  constexpr uint64_t kMaxLength = 10;
  form.name = u"multiple_groups_fields";
  form.fields = {
      CreateTestFormField("First Name", "first_name", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("Last Name", "last_name", "",
                          FormControlType::kInputText, "", kMaxLength),
      CreateTestFormField("Address", "address", "", FormControlType::kInputText,
                          "", kMaxLength),
      CreateTestFormField("Postal Code", "postal_code", "",
                          FormControlType::kInputText, "", kMaxLength)};
  form.fields.back().is_focusable = false;
  form.fields.push_back(CreateTestFormField("Country", "country", "",
                                            FormControlType::kInputText));
  form.fields.back().is_focusable = false;

  FormsSeen({form});

  // Fill the form with the first profile. The hidden fields will not get
  // filled.
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));

  ASSERT_EQ(5U, response_data.fields.size());
  ExpectFilledField("First Name", "first_name", "Elvis",
                    FormControlType::kInputText, response_data.fields[0]);
  ExpectFilledField("Last Name", "last_name", "Presley",
                    FormControlType::kInputText, response_data.fields[1]);
  ExpectFilledField("Address", "address", "3734 Elvis Presley Blvd.",
                    FormControlType::kInputText, response_data.fields[2]);
  ExpectFilledField("Postal Code", "postal_code", "",
                    FormControlType::kInputText, response_data.fields[3]);
  ExpectFilledField("Country", "country", "", FormControlType::kInputText,
                    response_data.fields[4]);

  // Two other fields will show up. Select the second profile. The fields that
  // were already filled, would be left unchanged, and the rest would be filled
  // with the second profile. (Two different profiles are selected, to make sure
  // the right fields are getting filled.)
  response_data.fields[3].is_focusable = true;
  response_data.fields[4].is_focusable = true;

  FormData later_response_data = FillAutofillFormDataAndGetResults(
      response_data, response_data.fields[4], MakeGuid(2));
  ASSERT_EQ(5U, later_response_data.fields.size());
  ExpectFilledField("First Name", "first_name", "Elvis",
                    FormControlType::kInputText, later_response_data.fields[0]);
  ExpectFilledField("Last Name", "last_name", "Presley",
                    FormControlType::kInputText, later_response_data.fields[1]);
  ExpectFilledField("Address", "address", "3734 Elvis Presley Blvd.",
                    FormControlType::kInputText, later_response_data.fields[2]);
  ExpectFilledField("Postal Code", "postal_code", "79401",
                    FormControlType::kInputText, later_response_data.fields[3]);
  ExpectFilledField("Country", "country", "United States",
                    FormControlType::kInputText, later_response_data.fields[4]);
}

// Test that the importing logic is called on form submit.
TEST_F(BrowserAutofillManagerTest, FormSubmitted_FormDataImporter) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // Fill the form.
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));
  ExpectFilledAddressFormElvis(response_data, false);
  AutofillProfile filled_profile =
      *personal_data().GetProfileByGUID(MakeGuid(1));

  // Remove the filled profile and simulate form submission. Since the
  // `personal_data()`'s auto accept imports for testing is enabled, expect
  // that the profile is imported again.
  personal_data().ClearAllLocalData();
  ASSERT_TRUE(personal_data().GetProfiles().empty());
  FormSubmitted(response_data);
  // Since the imported profile has a random GUID, AutofillProfile::operator==
  // cannot be used.
  ASSERT_EQ(personal_data().GetProfiles().size(), 1u);
  EXPECT_TRUE(personal_data().GetProfiles()[0]->Compare(filled_profile));
}

// Test that the user perception of autofill survey is triggered after a form
// submission.
TEST_F(BrowserAutofillManagerTest,
       UserPerceptionOfAutofillSurvey_MinFormSizeReached_TriggerSurvey) {
  base::test::ScopedFeatureList enabled_features(
      features::kAutofillAddressUserPerceptionSurvey);
  TestAutofillClock clock(AutofillClock::Now());
  // Set up a form with 4 fields (minimum form size to trigger a survey) and
  // fill them. The specific field types do not matter.
  const size_t n_fields = 4;
  FormData form = test::GetFormData(
      {.fields = {{.role = NAME_FIRST, .autocomplete_attribute = "given-name"},
                  {.role = NAME_LAST, .autocomplete_attribute = "family-name"},
                  {.role = ADDRESS_HOME_LINE1,
                   .autocomplete_attribute = "address-line1"},
                  {.role = ADDRESS_HOME_LINE2,
                   .autocomplete_attribute = "address-line2"}}});
  FormsSeen({form});
  // Fill the form.
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));
  const std::map<std::string, std::string> expected_field_filling_stats_data = {
      {"Accepted fields", base::NumberToString(n_fields)},
      {"Corrected to same type", "0"},
      {"Corrected to a different type", "0"},
      {"Corrected to an unknown type", "0"},
      {"Corrected to empty", "0"},
      {"Manually filled to same type", "0"},
      {"Manually filled to a different type", "0"},
      {"Manually filled to an unknown type", "0"},
      {"Total corrected", "0"},
      {"Total filled", base::NumberToString(n_fields)},
      {"Total unfilled", "0"},
      {"Total manually filled", "0"},
      {"Total number of fields", base::NumberToString(n_fields)}};

  EXPECT_CALL(autofill_client_, TriggerUserPerceptionOfAutofillSurvey(
                                    expected_field_filling_stats_data));

  // Simulate form submission.
  FormSubmitted(response_data);
}

TEST_F(
    BrowserAutofillManagerTest,
    UserPerceptionOfAutofillSurvey_MinFormSizeNotReached_DoNotTriggerSurvey) {
  base::test::ScopedFeatureList enabled_features(
      features::kAutofillAddressUserPerceptionSurvey);
  TestAutofillClock clock(AutofillClock::Now());
  // Set up a form with only one field and fill it.
  FormData form =
      test::GetFormData({.fields = {{.role = NAME_FIRST,
                                     .autocomplete_attribute = "given-name"}}});
  FormsSeen({form});
  // Fill the form.
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));

  EXPECT_CALL(autofill_client_, TriggerUserPerceptionOfAutofillSurvey).Times(0);

  // Simulate form submission.
  FormSubmitted(response_data);
}
// Test the field log events at the form submission.
class BrowserAutofillManagerWithLogEventsTest
    : public BrowserAutofillManagerTest {
 protected:
  BrowserAutofillManagerWithLogEventsTest() {
    scoped_features_.InitWithFeatures(
        /*enabled_features=*/{features::kAutofillLogUKMEventsWithSampleRate,
                              features::kAutofillParsingPatternProvider},
        /*disabled_features=*/{});
  }

  std::vector<AutofillField::FieldLogEventType> ToFieldTypeEvents(
      FieldType heuristic_type,
      FieldType overall_type,
      size_t field_signature_rank = 1) {
    std::vector<AutofillField::FieldLogEventType> expected_events;
#if BUILDFLAG(USE_INTERNAL_AUTOFILL_PATTERNS)
    // Default pattern.
    expected_events.push_back(HeuristicPredictionFieldLogEvent{
        .field_type = heuristic_type,
        .pattern_source = PatternSource::kDefault,
        .is_active_pattern_source = true,
        .rank_in_field_signature_group = field_signature_rank,
    });
    // Experimental pattern.
    expected_events.push_back(HeuristicPredictionFieldLogEvent{
        .field_type = heuristic_type,
        .pattern_source = PatternSource::kExperimental,
        .is_active_pattern_source = false,
        .rank_in_field_signature_group = field_signature_rank,
    });
#else
    // Legacy pattern.
    expected_events.push_back(HeuristicPredictionFieldLogEvent{
        .field_type = heuristic_type,
        .pattern_source = PatternSource::kLegacy,
        .is_active_pattern_source = true,
        .rank_in_field_signature_group = field_signature_rank,
    });
#endif
    // Rationalization.
    expected_events.push_back(RationalizationFieldLogEvent{
        .field_type = overall_type,
        .section_id = 1,
        .type_changed = false,
    });
    return expected_events;
  }

  // n = 1 means the first instance.
  template <class T>
  const T* FindNthEventOfType(
      const std::vector<AutofillField::FieldLogEventType>& events,
      size_t n) {
    // |count| represents the number of events of type T having been seen so
    // far.
    size_t count = 0;
    for (const auto& event : events) {
      if (const T* log_event = absl::get_if<T>(&event)) {
        ++count;
        if (count == n) {
          return log_event;
        }
      }
    }
    return nullptr;
  }

  template <class T>
  size_t CountEventOfType(
      const std::vector<AutofillField::FieldLogEventType>& events) {
    return base::ranges::count_if(events, [](const auto& event) {
      return absl::holds_alternative<T>(event);
    });
  }

  template <class T>
  const T* FindFirstEventOfType(
      const std::vector<AutofillField::FieldLogEventType>& events) {
    return FindNthEventOfType<T>(events, 1);
  }

 private:
  base::test::ScopedFeatureList scoped_features_;
};

// Test that we record TriggerFillFieldLogEvent for the field we click to show
// the autofill suggestion and FillFieldLogEvent for every field in the form.
TEST_F(BrowserAutofillManagerWithLogEventsTest, LogEventsAtFormSubmitted) {
  TestAutofillClock clock(AutofillClock::Now());
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // Fill the form.
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));
  ExpectFilledAddressFormElvis(response_data, false);

  // Simulate form submission.
  FormSubmitted(response_data);

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));
  ASSERT_TRUE(form_structure);

  const std::vector<AutofillField::FieldLogEventType>& focus_field_log_events =
      autofill_field->field_log_events();
  ASSERT_EQ(u"First Name", autofill_field->parseable_label());
  const TriggerFillFieldLogEvent* trigger_fill_field_log_event =
      FindFirstEventOfType<TriggerFillFieldLogEvent>(focus_field_log_events);
  ASSERT_TRUE(trigger_fill_field_log_event);

  for (const auto& autofill_field_ptr : *form_structure) {
    SCOPED_TRACE(autofill_field_ptr->parseable_label());
    std::vector<AutofillField::FieldLogEventType> expected_events =
        ToFieldTypeEvents(autofill_field_ptr->heuristic_type(),
                          autofill_field_ptr->heuristic_type());

    if (autofill_field_ptr->parseable_label() == u"First Name") {
      // The "First Name" field is the trigger field, so it contains the
      // TriggerFillFieldLogEvent followed by a FillFieldLogEvent.
      expected_events.push_back(TriggerFillFieldLogEvent{
          .fill_event_id = trigger_fill_field_log_event->fill_event_id,
          .data_type = FillDataType::kAutofillProfile,
          .associated_country_code = "US",
          .timestamp = AutofillClock::Now()});
    }
    // All filled fields share the same expected FillFieldLogEvent.
    // The first TriggerFillFieldLogEvent determines the fill_event_id for
    // all following FillFieldLogEvents.
    expected_events.push_back(FillFieldLogEvent{
        .fill_event_id = trigger_fill_field_log_event->fill_event_id,
        .had_value_before_filling = OptionalBoolean::kFalse,
        .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
        .was_autofilled_before_security_policy = OptionalBoolean::kTrue,
        .had_value_after_filling = OptionalBoolean::kTrue,
        .filling_method = AutofillFillingMethod::kFullForm,
        .filling_prevented_by_iframe_security_policy = OptionalBoolean::kFalse,
    });
    EXPECT_THAT(autofill_field_ptr->field_log_events(),
                ArrayEquals(expected_events));
  }
}

// Test that we record FillFieldLogEvents correctly after autofill when the
// field has nothing to fill or the field contains a user typed value already.
TEST_F(BrowserAutofillManagerWithLogEventsTest,
       LogEventsFillPartlyManuallyFilledForm) {
  TestAutofillClock clock(AutofillClock::Now());
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // Michael will be overridden with Elvis because Autofill is triggered from
  // the first field.
  form.fields[0].value = u"Michael";
  form.fields[0].properties_mask |= kUserTyped;

  // Jackson will be preserved, only override the first field.
  form.fields[2].value = u"Jackson";
  form.fields[2].properties_mask |= kUserTyped;

  // Fill the address data.
  TestAddressFillData address_fill_data(
      "Buddy", "Aaron", "Holly", "3734 Elvis Presley Blvd.", "Apt. 10",
      "Memphis", "Tennessee", "38116", "United States", "US", /*phone=*/"",
      /*email=*/"", "RCA");
  AutofillProfile profile1 = FillDataToAutofillProfile(address_fill_data);
  profile1.set_guid(MakeGuid(100));
  personal_data().AddProfile(profile1);
  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(100));

  TestAddressFillData expected_address_fill_data = address_fill_data;
  expected_address_fill_data.last = "Jackson";
  ExpectFilledForm(response_data, expected_address_fill_data,
                   /*card_fill_data=*/std::nullopt);

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));
  ASSERT_TRUE(form_structure);

  const std::vector<AutofillField::FieldLogEventType>& focus_field_log_events =
      autofill_field->field_log_events();
  ASSERT_EQ(u"First Name", autofill_field->parseable_label());
  const TriggerFillFieldLogEvent* trigger_fill_field_log_event =
      FindFirstEventOfType<TriggerFillFieldLogEvent>(focus_field_log_events);
  ASSERT_TRUE(trigger_fill_field_log_event);

  // The first TriggerFillFieldLogEvent determines the fill_event_id for
  // all following FillFieldLogEvents.
  FillEventId fill_event_id = trigger_fill_field_log_event->fill_event_id;
  for (const auto& autofill_field_ptr : *form_structure) {
    SCOPED_TRACE(autofill_field_ptr->parseable_label());
    std::vector<AutofillField::FieldLogEventType> expected_events =
        ToFieldTypeEvents(autofill_field_ptr->heuristic_type(),
                          autofill_field_ptr->heuristic_type());

    if (autofill_field_ptr->parseable_label() == u"First Name") {
      // The "First Name" field is the trigger field, so it contains the
      // TriggerFillFieldLogEvent followed by a FillFieldLogEvent.
      expected_events.push_back(
          TriggerFillFieldLogEvent{.fill_event_id = fill_event_id,
                                   .data_type = FillDataType::kAutofillProfile,
                                   .associated_country_code = "US",
                                   .timestamp = AutofillClock::Now()});
      expected_events.push_back(FillFieldLogEvent{
          .fill_event_id = fill_event_id,
          .had_value_before_filling = OptionalBoolean::kTrue,
          .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
          .was_autofilled_before_security_policy = OptionalBoolean::kTrue,
          .had_value_after_filling = OptionalBoolean::kTrue,
          .filling_method = AutofillFillingMethod::kFullForm,
          .filling_prevented_by_iframe_security_policy =
              OptionalBoolean::kFalse,
      });
    } else if (autofill_field_ptr->parseable_label() == u"Phone Number" ||
               autofill_field_ptr->parseable_label() == u"Email") {
      // Not filled because the address profile contained no data to fill.
      expected_events.push_back(FillFieldLogEvent{
          .fill_event_id = fill_event_id,
          .had_value_before_filling = OptionalBoolean::kFalse,
          .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
          .was_autofilled_before_security_policy = OptionalBoolean::kFalse,
          .had_value_after_filling = OptionalBoolean::kFalse,
          .filling_method = AutofillFillingMethod::kFullForm,
          .filling_prevented_by_iframe_security_policy =
              OptionalBoolean::kUndefined,
      });
    } else if (autofill_field_ptr->parseable_label() == u"Last Name") {
      // Not filled because the field contained a user typed value already.
      expected_events.push_back(FillFieldLogEvent{
          .fill_event_id = fill_event_id,
          .had_value_before_filling = OptionalBoolean::kTrue,
          .autofill_skipped_status = FieldFillingSkipReason::kUserFilledFields,
          .was_autofilled_before_security_policy = OptionalBoolean::kFalse,
          .had_value_after_filling = OptionalBoolean::kTrue,
          .filling_prevented_by_iframe_security_policy =
              OptionalBoolean::kUndefined,
      });
    } else {
      expected_events.push_back(FillFieldLogEvent{
          .fill_event_id = fill_event_id,
          .had_value_before_filling = OptionalBoolean::kFalse,
          .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
          .was_autofilled_before_security_policy = OptionalBoolean::kTrue,
          .had_value_after_filling = OptionalBoolean::kTrue,
          .filling_method = AutofillFillingMethod::kFullForm,
          .filling_prevented_by_iframe_security_policy =
              OptionalBoolean::kFalse,
      });
    }
    EXPECT_THAT(autofill_field_ptr->field_log_events(),
                ArrayEquals(expected_events));
  }
}

TEST_F(BrowserAutofillManagerWithLogEventsTest,
       FillingMethod_TargetedAllFields_FullForm) {
  base::test::ScopedFeatureList enabled_features(
      features::kAutofillGranularFillingAvailable);

  FormData form =
      test::GetFormData({.fields = {{.role = NAME_FIRST,
                                     .autocomplete_attribute = "given-name"}}});
  FormsSeen({form});
  FillAutofillFormDataAndGetResults(
      form, form.fields[0], MakeGuid(1),
      {.trigger_source = AutofillTriggerSource::kPopup,
       .field_types_to_fill = kAllFieldTypes});
  const std::vector<AutofillField::FieldLogEventType>& fill_field_log_events =
      browser_autofill_manager_->GetAutofillField(form, form.fields[0])
          ->field_log_events();

  ASSERT_EQ(CountEventOfType<FillFieldLogEvent>(fill_field_log_events), 1u);
  EXPECT_THAT(
      *FindFirstEventOfType<FillFieldLogEvent>(fill_field_log_events),
      EqualsFillFieldLogEvent(FillFieldLogEvent{
          .fill_event_id = FillEventId(-1),
          .had_value_before_filling = OptionalBoolean::kFalse,
          .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
          .was_autofilled_before_security_policy = OptionalBoolean::kTrue,
          .had_value_after_filling = OptionalBoolean::kTrue,
          .filling_method = AutofillFillingMethod::kFullForm,
          .filling_prevented_by_iframe_security_policy =
              OptionalBoolean::kFalse,
      }));
}

TEST_F(BrowserAutofillManagerWithLogEventsTest,
       FillingMethod_TargetedGranularFillingGroup_GroupFilling) {
  base::test::ScopedFeatureList enabled_features(
      features::kAutofillGranularFillingAvailable);

  FormData form =
      test::GetFormData({.fields = {{.role = NAME_FIRST,
                                     .autocomplete_attribute = "given-name"}}});
  FormsSeen({form});
  FillAutofillFormDataAndGetResults(
      form, form.fields[0], MakeGuid(1),
      {.trigger_source = AutofillTriggerSource::kPopup,
       .field_types_to_fill = GetFieldTypesOfGroup(FieldTypeGroup::kName)});
  const std::vector<AutofillField::FieldLogEventType>& fill_field_log_events =
      browser_autofill_manager_->GetAutofillField(form, form.fields[0])
          ->field_log_events();

  ASSERT_EQ(CountEventOfType<FillFieldLogEvent>(fill_field_log_events), 1u);
  EXPECT_THAT(
      *FindFirstEventOfType<FillFieldLogEvent>(fill_field_log_events),
      EqualsFillFieldLogEvent(FillFieldLogEvent{
          .fill_event_id = FillEventId(-1),
          .had_value_before_filling = OptionalBoolean::kFalse,
          .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
          .was_autofilled_before_security_policy = OptionalBoolean::kTrue,
          .had_value_after_filling = OptionalBoolean::kTrue,
          .filling_method = AutofillFillingMethod::kGroupFilling,
          .filling_prevented_by_iframe_security_policy =
              OptionalBoolean::kFalse,
      }));
}

TEST_F(BrowserAutofillManagerWithLogEventsTest,
       FillingMethod_TargetedSingleField_FieldByFieldFilling) {
  base::test::ScopedFeatureList features(
      features::kAutofillGranularFillingAvailable);

  FormData form =
      test::GetFormData({.fields = {{.role = NAME_FIRST,
                                     .autocomplete_attribute = "given-name"}}});
  FormsSeen({form});
  FillAutofillFormDataAndGetResults(
      form, form.fields[0], MakeGuid(1),
      {.trigger_source = AutofillTriggerSource::kPopup,
       .field_types_to_fill = {NAME_FIRST}});
  const std::vector<AutofillField::FieldLogEventType>& fill_field_log_events =
      browser_autofill_manager_->GetAutofillField(form, form.fields[0])
          ->field_log_events();

  ASSERT_EQ(CountEventOfType<FillFieldLogEvent>(fill_field_log_events), 1u);
  EXPECT_THAT(
      *FindFirstEventOfType<FillFieldLogEvent>(fill_field_log_events),
      EqualsFillFieldLogEvent(FillFieldLogEvent{
          .fill_event_id = FillEventId(-1),
          .had_value_before_filling = OptionalBoolean::kFalse,
          .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
          .was_autofilled_before_security_policy = OptionalBoolean::kTrue,
          .had_value_after_filling = OptionalBoolean::kTrue,
          .filling_method = AutofillFillingMethod::kFieldByFieldFilling,
          .filling_prevented_by_iframe_security_policy =
              OptionalBoolean::kFalse,
      }));
}

// Test that we record FillFieldLogEvents after filling a form twice, the first
// time some field values are missing when autofilling.
TEST_F(BrowserAutofillManagerWithLogEventsTest, LogEventsAtRefillForm) {
  TestAutofillClock clock(AutofillClock::Now());
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  std::vector<FormData> forms(1, form);
  FormsSeen(forms);

  // First fill the address data which does not have email and phone number.
  TestAddressFillData address_fill_data(
      "Buddy", "Aaron", "Holly", "3734 Elvis Presley Blvd.", "Apt. 10",
      "Memphis", "Tennessee", "38116", "United States", "US", /*phone=*/"",
      /*email=*/"", "RCA");
  AutofillProfile profile1 = FillDataToAutofillProfile(address_fill_data);
  profile1.set_guid(MakeGuid(100));
  personal_data().AddProfile(profile1);
  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(100));

  TestAddressFillData expected_address_fill_data = address_fill_data;
  ExpectFilledForm(response_data, expected_address_fill_data,
                   /*card_fill_data=*/std::nullopt);

  // Refill the address data with all the field values.
  response_data = FillAutofillFormDataAndGetResults(
      response_data, *response_data.fields.begin(), MakeGuid(1));

  bool default_to_city_and_number =
      base::FeatureList::IsEnabled(features::kAutofillDefaultToCityAndNumber);
  expected_address_fill_data.first = "Elvis";
  expected_address_fill_data.phone =
      default_to_city_and_number ? "2345678901" : "12345678901";
  expected_address_fill_data.email = "theking@gmail.com";
  ExpectFilledForm(response_data, expected_address_fill_data,
                   /*card_fill_data=*/std::nullopt);

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));
  ASSERT_TRUE(form_structure);

  const std::vector<AutofillField::FieldLogEventType>& focus_field_log_events =
      autofill_field->field_log_events();
  ASSERT_EQ(u"First Name", autofill_field->parseable_label());
  const TriggerFillFieldLogEvent* trigger_fill_field_log_event1 =
      FindNthEventOfType<TriggerFillFieldLogEvent>(focus_field_log_events, 1);
  ASSERT_TRUE(trigger_fill_field_log_event1);
  const TriggerFillFieldLogEvent* trigger_fill_field_log_event2 =
      FindNthEventOfType<TriggerFillFieldLogEvent>(focus_field_log_events, 2);
  ASSERT_TRUE(trigger_fill_field_log_event2);

  // The first TriggerFillFieldLogEvent determines the fill_event_id for
  // all following FillFieldLogEvents.
  FillFieldLogEvent expected_fill_field_log_event1{
      .fill_event_id = trigger_fill_field_log_event1->fill_event_id,
      .had_value_before_filling = OptionalBoolean::kFalse,
      .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
      .was_autofilled_before_security_policy = OptionalBoolean::kTrue,
      .had_value_after_filling = OptionalBoolean::kTrue,
      .filling_method = AutofillFillingMethod::kFullForm,
      .filling_prevented_by_iframe_security_policy = OptionalBoolean::kFalse,
  };

  for (const auto& autofill_field_ptr : *form_structure) {
    SCOPED_TRACE(autofill_field_ptr->parseable_label());
    std::vector<AutofillField::FieldLogEventType> expected_events =
        ToFieldTypeEvents(autofill_field_ptr->heuristic_type(),
                          autofill_field_ptr->heuristic_type());

    if (autofill_field_ptr->parseable_label() == u"First Name") {
      // The "First Name" field is the trigger field, so it contains the
      // TriggerFillFieldLogEvent followed by a FillFieldLogEvent.
      expected_events.push_back(TriggerFillFieldLogEvent{
          .fill_event_id = trigger_fill_field_log_event1->fill_event_id,
          .data_type = FillDataType::kAutofillProfile,
          .associated_country_code = "US",
          .timestamp = AutofillClock::Now()});
      expected_events.push_back(expected_fill_field_log_event1);
      expected_events.push_back(TriggerFillFieldLogEvent{
          .fill_event_id = trigger_fill_field_log_event2->fill_event_id,
          .data_type = FillDataType::kAutofillProfile,
          .associated_country_code = "US",
          .timestamp = AutofillClock::Now()});
      expected_events.push_back(FillFieldLogEvent{
          .fill_event_id = trigger_fill_field_log_event2->fill_event_id,
          .had_value_before_filling = OptionalBoolean::kTrue,
          .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
          .was_autofilled_before_security_policy = OptionalBoolean::kTrue,
          .had_value_after_filling = OptionalBoolean::kTrue,
          .filling_method = AutofillFillingMethod::kFullForm,
          .filling_prevented_by_iframe_security_policy =
              OptionalBoolean::kFalse});
    } else if (autofill_field_ptr->parseable_label() == u"Phone Number" ||
               autofill_field_ptr->parseable_label() == u"Email") {
      FillFieldLogEvent expected_event = expected_fill_field_log_event1;
      expected_event.was_autofilled_before_security_policy =
          OptionalBoolean::kFalse;
      expected_event.had_value_after_filling = OptionalBoolean::kFalse;
      expected_event.filling_prevented_by_iframe_security_policy =
          OptionalBoolean::kUndefined;
      expected_events.push_back(expected_event);
      expected_events.push_back(FillFieldLogEvent{
          .fill_event_id = trigger_fill_field_log_event2->fill_event_id,
          .had_value_before_filling = OptionalBoolean::kFalse,
          .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
          .was_autofilled_before_security_policy = OptionalBoolean::kTrue,
          .had_value_after_filling = OptionalBoolean::kTrue,
          .filling_method = AutofillFillingMethod::kFullForm,
          .filling_prevented_by_iframe_security_policy =
              OptionalBoolean::kFalse});
    } else {
      expected_events.push_back(expected_fill_field_log_event1);
      expected_events.push_back(FillFieldLogEvent{
          .fill_event_id = trigger_fill_field_log_event2->fill_event_id,
          .had_value_before_filling = OptionalBoolean::kTrue,
          .autofill_skipped_status =
              FieldFillingSkipReason::kAutofilledFieldsNotRefill,
          .was_autofilled_before_security_policy = OptionalBoolean::kFalse,
          .had_value_after_filling = OptionalBoolean::kTrue,
          .filling_method = AutofillFillingMethod::kNone,
          .filling_prevented_by_iframe_security_policy =
              OptionalBoolean::kUndefined});
    }
    EXPECT_THAT(autofill_field_ptr->field_log_events(),
                ArrayEquals(expected_events));
  }
}

// Test that we record user typing log event correctly after autofill.
TEST_F(BrowserAutofillManagerWithLogEventsTest, LogEventsAtUserTypingInField) {
  TestAutofillClock clock(AutofillClock::Now());
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  std::vector<FormData> forms(1, form);
  FormsSeen(forms);

  // Fill the form.
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], MakeGuid(1));
  ExpectFilledAddressFormElvis(response_data, false);

  FormFieldData field = form.fields[0];
  // Simulate editing the first field.
  field.value = u"Michael";
  browser_autofill_manager_->OnTextFieldDidChange(form, field, gfx::RectF(),
                                                  base::TimeTicks::Now());

  // Simulate form submission.
  FormSubmitted(response_data);

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));
  ASSERT_TRUE(form_structure);

  const std::vector<AutofillField::FieldLogEventType>& focus_field_log_events =
      autofill_field->field_log_events();
  ASSERT_EQ(u"First Name", autofill_field->parseable_label());
  const TriggerFillFieldLogEvent* trigger_fill_field_log_event =
      FindFirstEventOfType<TriggerFillFieldLogEvent>(focus_field_log_events);
  ASSERT_TRUE(trigger_fill_field_log_event);

  // All filled fields share the same expected FillFieldLogEvent.
  // The first TriggerFillFieldLogEvent determines the fill_event_id for
  // all following FillFieldLogEvents.
  FillFieldLogEvent expected_fill_field_log_event{
      .fill_event_id = trigger_fill_field_log_event->fill_event_id,
      .had_value_before_filling = OptionalBoolean::kFalse,
      .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
      .was_autofilled_before_security_policy = OptionalBoolean::kTrue,
      .had_value_after_filling = OptionalBoolean::kTrue,
      .filling_method = AutofillFillingMethod::kFullForm,
      .filling_prevented_by_iframe_security_policy = OptionalBoolean::kFalse,
  };

  for (const auto& autofill_field_ptr : *form_structure) {
    SCOPED_TRACE(autofill_field_ptr->parseable_label());
    std::vector<AutofillField::FieldLogEventType> expected_events =
        ToFieldTypeEvents(autofill_field_ptr->heuristic_type(),
                          autofill_field_ptr->heuristic_type());

    if (autofill_field_ptr->parseable_label() == u"First Name") {
      // The "First Name" field is the trigger field, so it contains the
      // TriggerFillFieldLogEvent followed by a FillFieldLogEvent.
      expected_events.push_back(TriggerFillFieldLogEvent{
          .fill_event_id = trigger_fill_field_log_event->fill_event_id,
          .data_type = FillDataType::kAutofillProfile,
          .associated_country_code = "US",
          .timestamp = AutofillClock::Now()});
      expected_events.push_back(expected_fill_field_log_event);
      expected_events.push_back(TypingFieldLogEvent{
          .has_value_after_typing = OptionalBoolean::kTrue,
      });
    } else {
      expected_events.push_back(expected_fill_field_log_event);
    }
    EXPECT_THAT(autofill_field_ptr->field_log_events(),
                ArrayEquals(expected_events));
  }
}

// Test that we record field log events correctly when the user touches to fill
// and fills the credit card form with a suggestion.
TEST_F(BrowserAutofillManagerWithLogEventsTest,
       LogEventsAutofillSuggestionsOrTouchToFill) {
  TestAutofillClock clock(AutofillClock::Now());
  FormData form;
  CreateTestCreditCardFormData(&form, /*is_https=*/true,
                               /*use_month_type=*/false);
  FormsSeen({form});
  const FormFieldData& field = form.fields[0];

  // Touch the field of "Name on Card" and autofill suggestion is shown.
  EXPECT_CALL(touch_to_fill_delegate(), TryToShowTouchToFill)
      .WillOnce(Return(false));
  TryToShowTouchToFill(form, field, /*form_element_was_clicked=*/true);
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());

  // Fill the form by triggering the suggestion from "Name on Card" field.
  FormData response_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(4));
  ExpectFilledCreditCardFormElvis(response_data, /*has_address_fields=*/false);

  // Simulate form submission.
  FormSubmitted(response_data);

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  ASSERT_TRUE(browser_autofill_manager_->GetCachedFormAndField(
      form, form.fields.front(), &form_structure, &autofill_field));
  ASSERT_TRUE(form_structure);

  const std::vector<AutofillField::FieldLogEventType>& focus_field_log_events =
      autofill_field->field_log_events();
  ASSERT_EQ(u"Name on Card", autofill_field->parseable_label());
  const TriggerFillFieldLogEvent* trigger_fill_field_log_event =
      FindFirstEventOfType<TriggerFillFieldLogEvent>(focus_field_log_events);
  ASSERT_TRUE(trigger_fill_field_log_event);

  // All filled fields share the same expected FillFieldLogEvent.
  // The first TriggerFillFieldLogEvent determines the fill_event_id for
  // all following FillFieldLogEvents.
  FillFieldLogEvent expected_fill_field_log_event{
      .fill_event_id = trigger_fill_field_log_event->fill_event_id,
      .had_value_before_filling = OptionalBoolean::kFalse,
      .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
      .was_autofilled_before_security_policy = OptionalBoolean::kTrue,
      .had_value_after_filling = OptionalBoolean::kTrue,
      .filling_method = AutofillFillingMethod::kFullForm,
      .filling_prevented_by_iframe_security_policy = OptionalBoolean::kFalse,
  };

  for (const auto& autofill_field_ptr : *form_structure) {
    SCOPED_TRACE(autofill_field_ptr->parseable_label());
    std::vector<AutofillField::FieldLogEventType> expected_events =
        ToFieldTypeEvents(autofill_field_ptr->heuristic_type(),
                          autofill_field_ptr->heuristic_type());

    if (autofill_field_ptr->parseable_label() == u"Name on Card") {
      // The "Name on Card" field gets focus and shows a suggestion so it
      // contains the AskForValuesToFillFieldLogEvent.
      expected_events.push_back(AskForValuesToFillFieldLogEvent{
          .has_suggestion = OptionalBoolean::kTrue,
          .suggestion_is_shown = OptionalBoolean::kTrue,
      });
      expected_events.push_back(TriggerFillFieldLogEvent{
          .fill_event_id = trigger_fill_field_log_event->fill_event_id,
          .data_type = FillDataType::kCreditCard,
          .associated_country_code = "",
          .timestamp = AutofillClock::Now()});
      // The "Name on Card" field is the trigger field, so it contains the
      // TriggerFillFieldLogEvent followed by a FillFieldLogEvent.
      expected_events.push_back(expected_fill_field_log_event);
    } else if (autofill_field_ptr->parseable_label() == u"CVC") {
      // CVC field is not autofilled.
      expected_events.push_back(FillFieldLogEvent{
          .fill_event_id = trigger_fill_field_log_event->fill_event_id,
          .had_value_before_filling = OptionalBoolean::kFalse,
          .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
          .was_autofilled_before_security_policy = OptionalBoolean::kFalse,
          .had_value_after_filling = OptionalBoolean::kFalse,
          .filling_method = AutofillFillingMethod::kFullForm,
          .filling_prevented_by_iframe_security_policy =
              OptionalBoolean::kUndefined,
      });
    } else {
      expected_events.push_back(expected_fill_field_log_event);
    }
    EXPECT_THAT(autofill_field_ptr->field_log_events(),
                ArrayEquals(expected_events));
  }
}

// Test that we record AutocompleteAttributeFieldLogEvent for the fields with
// autocomplete attributes in the form.
TEST_F(BrowserAutofillManagerWithLogEventsTest,
       LogEventsOnAutocompleteAttributeField) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      CreateTestFormField("First Name", "firstname", "",
                          FormControlType::kInputText, "given-name"),
      CreateTestFormField("Last Name", "lastname", "",
                          FormControlType::kInputText, "family-name"),
      // Set no autocomplete attribute for the middle name.
      CreateTestFormField("Middle name", "middle", "",
                          FormControlType::kInputText, ""),
      // Set an unrecognized autocomplete attribute for the last name.
      CreateTestFormField("Email", "email", "", FormControlType::kInputText,
                          "unrecognized")};

  // Simulate having seen this form on page load.
  auto form_structure_instance = std::make_unique<FormStructure>(form);
  FormStructure* form_structure = form_structure_instance.get();
  form_structure->DetermineHeuristicTypes(GeoIpCountryCode(""), nullptr,
                                          nullptr);
  browser_autofill_manager_->AddSeenFormStructure(
      std::move(form_structure_instance));

  // Simulate form submission.
  FormSubmitted(form);

  for (const auto& autofill_field_ptr : *form_structure) {
    SCOPED_TRACE(autofill_field_ptr->parseable_label());
    FieldType overall_type = autofill_field_ptr->heuristic_type();
    std::vector<AutofillField::FieldLogEventType> expected_events =
        ToFieldTypeEvents(autofill_field_ptr->heuristic_type(), overall_type);
    if (autofill_field_ptr->parseable_label() != u"Middle name") {
      expected_events.insert(expected_events.begin(),
                             AutocompleteAttributeFieldLogEvent{
                                 .html_type = autofill_field_ptr->html_type(),
                                 .html_mode = HtmlFieldMode::kNone,
                                 .rank_in_field_signature_group = 1,
                             });
    }
    EXPECT_THAT(autofill_field_ptr->field_log_events(),
                ArrayEquals(expected_events));
  }
}

// Test that we record field log events correctly for autofill crowdsourced
// server prediction.
TEST_F(BrowserAutofillManagerWithLogEventsTest,
       LogEventsParseQueryResponseServerPrediction) {
  // Set up our form data.
  FormData form;
  form.host_frame = test::MakeLocalFrameToken();
  form.unique_renderer_id = test::MakeFormRendererId();
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      CreateTestFormField(/*label=*/"Name", /*name=*/"name", /*value=*/"",
                          /*type=*/FormControlType::kInputText),
      CreateTestFormField(/*label=*/"Street", /*name=*/"Street", /*value=*/"",
                          /*type=*/FormControlType::kInputText),
      CreateTestFormField(/*label=*/"City", /*name=*/"city", /*value=*/"",
                          /*type=*/FormControlType::kInputText),
      CreateTestFormField(/*label=*/"State", /*name=*/"state", /*value=*/"",
                          /*type=*/FormControlType::kInputText),
      CreateTestFormField(/*label=*/"Postal Code", /*name=*/"zipcode",
                          /*value=*/"", /*type=*/FormControlType::kInputText)};

  // Simulate having seen this form on page load.
  // |form_structure_instance| will be owned by |browser_autofill_manager_|.
  auto form_structure_instance = std::make_unique<FormStructure>(form);
  // This pointer is valid as long as autofill manager lives.
  FormStructure* form_structure = form_structure_instance.get();
  form_structure->DetermineHeuristicTypes(GeoIpCountryCode(""), nullptr,
                                          nullptr);
  browser_autofill_manager_->AddSeenFormStructure(
      std::move(form_structure_instance));

  // Make API response with suggestions.
  AutofillQueryResponse response;
  AutofillQueryResponse::FormSuggestion* form_suggestion;
  // Set suggestions for form.
  form_suggestion = response.add_form_suggestions();
  autofill::test::AddFieldPredictionsToForm(
      form.fields[0],
      {test::CreateFieldPrediction(NAME_FIRST,
                                   FieldPrediction::SOURCE_AUTOFILL_DEFAULT),
       test::CreateFieldPrediction(USERNAME,
                                   FieldPrediction::SOURCE_PASSWORDS_DEFAULT)},
      form_suggestion);
  autofill::test::AddFieldPredictionsToForm(
      form.fields[1],
      {test::CreateFieldPrediction(ADDRESS_HOME_LINE1,
                                   FieldPrediction::SOURCE_OVERRIDE)},
      form_suggestion);
  autofill::test::AddFieldPredictionToForm(form.fields[2], ADDRESS_HOME_CITY,
                                           form_suggestion);
  autofill::test::AddFieldPredictionToForm(form.fields[3], ADDRESS_HOME_STATE,
                                           form_suggestion);
  autofill::test::AddFieldPredictionToForm(form.fields[4], ADDRESS_HOME_ZIP,
                                           form_suggestion);

  std::string response_string;
  ASSERT_TRUE(response.SerializeToString(&response_string));
  // Query autofill server for the field type prediction.
  test_api(*browser_autofill_manager_)
      .OnLoadedServerPredictions(base::Base64Encode(response_string),
                                 test::GetEncodedSignatures(*form_structure));
  std::vector<FieldType> types{NAME_FIRST, ADDRESS_HOME_LINE1,
                               ADDRESS_HOME_CITY, ADDRESS_HOME_STATE,
                               ADDRESS_HOME_ZIP};
  for (size_t i = 0; i < types.size(); ++i) {
    EXPECT_EQ(types[i], form_structure->field(i)->Type().GetStorableType());
  }

  // Simulate form submission.
  FormSubmitted(form);

  for (const auto& autofill_field_ptr : *form_structure) {
    SCOPED_TRACE(autofill_field_ptr->parseable_label());
    std::vector<AutofillField::FieldLogEventType> expected_events =
        ToFieldTypeEvents(autofill_field_ptr->heuristic_type(),
                          autofill_field_ptr->heuristic_type());
    // The autofill server applies two predictions on the "Name" field.
    FieldType server_type2 = autofill_field_ptr->parseable_label() == u"Name"
                                 ? USERNAME
                                 : NO_SERVER_DATA;
    FieldPrediction::Source prediction_source2 =
        autofill_field_ptr->parseable_label() == u"Name"
            ? FieldPrediction::SOURCE_PASSWORDS_DEFAULT
            : FieldPrediction::SOURCE_UNSPECIFIED;
    // The server prediction overrides the type predicted by local heuristic on
    // the field of label "Street".
    bool server_type_prediction_is_override =
        autofill_field_ptr->parseable_label() == u"Street" ? true : false;
    expected_events.push_back(ServerPredictionFieldLogEvent{
        .server_type1 = autofill_field_ptr->server_type(),
        .prediction_source1 =
            autofill_field_ptr->server_predictions()[0].source(),
        .server_type2 = server_type2,
        .prediction_source2 = prediction_source2,
        .server_type_prediction_is_override =
            server_type_prediction_is_override,
        .rank_in_field_signature_group = 1,
    });
    // Rationalization.
    expected_events.push_back(RationalizationFieldLogEvent{
        .field_type = autofill_field_ptr->server_type(),
        .section_id = 1,
        .type_changed = false,
    });
    EXPECT_THAT(autofill_field_ptr->field_log_events(),
                ArrayEquals(expected_events));
  }
}

// Test that we record field log events correctly for rationalization when
// there are two address fields.
TEST_F(BrowserAutofillManagerWithLogEventsTest,
       LogEventsRationalizationTwoAddresses) {
  // Set up our form data.
  FormData form;
  form.host_frame = test::MakeLocalFrameToken();
  form.unique_renderer_id = test::MakeFormRendererId();
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      CreateTestFormField(/*label=*/"Full Name", /*name=*/"fullName",
                          /*value=*/"", /*type=*/FormControlType::kInputText),
      CreateTestFormField(/*label=*/"Address", /*name=*/"address", /*value=*/"",
                          /*type=*/FormControlType::kInputText),
      CreateTestFormField(/*label=*/"Address", /*name=*/"address", /*value=*/"",
                          /*type=*/FormControlType::kInputText),
      CreateTestFormField(/*label=*/"City", /*name=*/"city", /*value=*/"",
                          /*type=*/FormControlType::kInputText)};

  // Simulate having seen this form on page load.
  // |form_structure_instance| will be owned by |browser_autofill_manager_|.
  auto form_structure_instance = std::make_unique<FormStructure>(form);
  // This pointer is valid as long as autofill manager lives.
  FormStructure* form_structure = form_structure_instance.get();
  form_structure->DetermineHeuristicTypes(GeoIpCountryCode(""), nullptr,
                                          nullptr);
  browser_autofill_manager_->AddSeenFormStructure(
      std::move(form_structure_instance));

  // Make API response with suggestions.
  AutofillQueryResponse response;
  AutofillQueryResponse::FormSuggestion* form_suggestion;
  // Set suggestions for form.
  form_suggestion = response.add_form_suggestions();
  std::vector<FieldType> server_types{NAME_FULL, ADDRESS_HOME_STREET_ADDRESS,
                                      ADDRESS_HOME_STREET_ADDRESS,
                                      ADDRESS_HOME_CITY};
  for (size_t i = 0; i < server_types.size(); ++i) {
    autofill::test::AddFieldPredictionToForm(form.fields[i], server_types[i],
                                             form_suggestion);
  }

  std::string response_string;
  ASSERT_TRUE(response.SerializeToString(&response_string));
  // Query autofill server for the field type prediction.
  test_api(*browser_autofill_manager_)
      .OnLoadedServerPredictions(base::Base64Encode(response_string),
                                 test::GetEncodedSignatures(*form_structure));
  std::vector<FieldType> overall_types{NAME_FULL, ADDRESS_HOME_LINE1,
                                       ADDRESS_HOME_LINE2, ADDRESS_HOME_CITY};
  for (size_t i = 0; i < server_types.size(); ++i) {
    EXPECT_EQ(overall_types[i],
              form_structure->field(i)->Type().GetStorableType());
  }

  // Simulate form submission.
  FormSubmitted(form);

  for (const auto& autofill_field_ptr : *form_structure) {
    SCOPED_TRACE(autofill_field_ptr->parseable_label());
    size_t field_signature_rank =
        autofill_field_ptr->heuristic_type() == ADDRESS_HOME_LINE2 ? 2 : 1;
    std::vector<AutofillField::FieldLogEventType> expected_events =
        ToFieldTypeEvents(autofill_field_ptr->heuristic_type(),
                          autofill_field_ptr->heuristic_type(),
                          field_signature_rank);
    expected_events.push_back(ServerPredictionFieldLogEvent{
        .server_type1 = autofill_field_ptr->server_type(),
        .prediction_source1 =
            autofill_field_ptr->server_predictions()[0].source(),
        .server_type2 = NO_SERVER_DATA,
        .prediction_source2 = FieldPrediction::SOURCE_UNSPECIFIED,
        .server_type_prediction_is_override = false,
        .rank_in_field_signature_group = field_signature_rank,
    });
    // Rationalization.
    bool type_changed =
        autofill_field_ptr->parseable_label() == u"Address" ? true : false;
    expected_events.push_back(RationalizationFieldLogEvent{
        .field_type = autofill_field_ptr->Type().GetStorableType(),
        .section_id = 1,
        .type_changed = type_changed,
    });
    EXPECT_THAT(autofill_field_ptr->field_log_events(),
                ArrayEquals(expected_events));
  }
}

// Test that when Autocomplete is enabled and Autofill is disabled, form
// submissions are still received by the SingleFieldFormFillRouter.
TEST_F(BrowserAutofillManagerTest, FormSubmittedAutocompleteEnabled) {
  browser_autofill_manager_->SetAutofillProfileEnabled(autofill_client_, false);
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              false);

  // Set up our form data.
  FormData form = CreateTestAddressFormData();

  EXPECT_CALL(single_field_form_fill_router(), OnWillSubmitForm(_, _, true));
  FormSubmitted(form);
}

// Test that the value patterns metric is reported.
TEST_F(BrowserAutofillManagerTest, ValuePatternsMetric) {
  struct ValuePatternTestCase {
    const char* value;
    autofill::ValuePatternsMetric pattern;
  } kTestCases[] = {
      {"user@okaxis", autofill::ValuePatternsMetric::kUpiVpa},
      {"IT60X0542811101000000123456", autofill::ValuePatternsMetric::kIban}};
  for (const ValuePatternTestCase test_case : kTestCases) {
    // Set up our form data.
    FormData form;
    form.name = u"my-form";
    form.url = GURL("https://myform.com/form.html");
    form.action = GURL("https://myform.com/submit.html");
    form.fields = {CreateTestFormField("Some label", "my-field",
                                       test_case.value,
                                       FormControlType::kInputText)};
    FormsSeen({form});

    base::HistogramTester histogram_tester;
    FormSubmitted(form);
    histogram_tester.ExpectUniqueSample("Autofill.SubmittedValuePatterns",
                                        test_case.pattern, 1);
  }
}

// Test that when Autofill is disabled, single field form fill suggestions are
// still queried as a fallback.
TEST_F(BrowserAutofillManagerTest,
       SingleFieldFormFillSuggestions_SomeWhenAutofillDisabled) {
  browser_autofill_manager_->SetAutofillProfileEnabled(autofill_client_, false);
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              false);

  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // Expect the SingleFieldFormFillRouter to be called for suggestions.
  EXPECT_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions);

  GetAutofillSuggestions(form, form.fields[0]);

  // Single field form fill suggestions were returned, so we should not go
  // through the normal autofill flow.
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

// Test that we do not query for single field form fill suggestions when there
// are Autofill suggestions available.
TEST_F(BrowserAutofillManagerTest,
       SingleFieldFormFillSuggestions_NoneWhenAutofillPresent) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // SingleFieldFormFillRouter is not called for suggestions.
  EXPECT_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions)
      .Times(0);

  GetAutofillSuggestions(form, form.fields[0]);
  // Verify that suggestions are returned.
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());
}

// Test that we query for single field form fill suggestions when there are no
// Autofill suggestions available.
TEST_F(BrowserAutofillManagerTest,
       SingleFieldFormFillSuggestions_SomeWhenAutofillEmpty) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // No suggestions matching "donkey".
  FormFieldData field = CreateTestFormField("Email", "email", "donkey",
                                            FormControlType::kInputEmail);

  // Single field form fill manager is called for suggestions because Autofill
  // is empty.
  EXPECT_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions);

  GetAutofillSuggestions(form, field);
}

// Test that when Autofill is disabled and the field is a credit card name
// field, single field form fill suggestions are queried.
TEST_F(BrowserAutofillManagerTest,
       SingleFieldFormFillSuggestions_CreditCardNameFieldShouldAutocomplete) {
  // Since we are testing a form that submits over HTTP, we also need to set
  // the main frame to HTTP in the client, otherwise mixed form warnings will
  // trigger and autofill will be disabled.
  GURL::Replacements replacements;
  replacements.SetSchemeStr(url::kHttpScheme);
  autofill_client_.set_form_origin(
      autofill_client_.form_origin().ReplaceComponents(replacements));
  ResetBrowserAutofillManager();
  browser_autofill_manager_->SetAutofillProfileEnabled(autofill_client_, false);
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              false);

  // Set up our form data.
  FormData form = CreateTestCreditCardFormData(/*is_https=*/false,
                                               /*use_month_type=*/false);
  FormsSeen({form});
  // The first field is "Name on card", which should autocomplete.
  FormFieldData field = form.fields[0];
  field.should_autocomplete = true;

  // SingleFieldFormFillRouter is called for suggestions.
  EXPECT_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions);

  GetAutofillSuggestions(form, field);
}

// Test that when Autofill is disabled and the field is a credit card number
// field, single field form fill suggestions are not queried.
TEST_F(BrowserAutofillManagerTest,
       SingleFieldFormFillSuggestions_CreditCardNumberShouldNotAutocomplete) {
  // Since we are testing a form that submits over HTTP, we also need to set
  // the main frame to HTTP in the client, otherwise mixed form warnings will
  // trigger and autofill will be disabled.
  GURL::Replacements replacements;
  replacements.SetSchemeStr(url::kHttpScheme);
  autofill_client_.set_form_origin(
      autofill_client_.form_origin().ReplaceComponents(replacements));
  ResetBrowserAutofillManager();
  browser_autofill_manager_->SetAutofillProfileEnabled(autofill_client_, false);
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              false);

  // Set up our form data.
  FormData form = CreateTestCreditCardFormData(/*is_https=*/false,
                                               /*use_month_type=*/false);
  FormsSeen({form});
  // The second field is "Card Number", which should not autocomplete.
  FormFieldData field = form.fields[1];
  field.should_autocomplete = true;

  // SingleFieldFormFillRouter is not called for suggestions.
  EXPECT_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions)
      .Times(0);

  GetAutofillSuggestions(form, field);
}

// Test that the situation where there are no Autofill suggestions available,
// and no single field form fill conditions were met is correctly handled. The
// single field form fill conditions were not met because autocomplete is set to
// off and the field is not recognized as a promo code field.
TEST_F(
    BrowserAutofillManagerTest,
    SingleFieldFormFillSuggestions_NoneWhenAutofillEmptyAndSingleFieldFormFillConditionsNotMet) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // No suggestions matching "donkey".
  FormFieldData field = CreateTestFormField("Email", "email", "donkey",
                                            FormControlType::kInputEmail);
  field.should_autocomplete = false;

  // Autocomplete is set to off, so suggestions should not get returned from
  // single_field_form_fill_router()|.
  EXPECT_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions)
      .WillRepeatedly(Return(false));

  GetAutofillSuggestions(form, field);

  // Single field form fill was not triggered, so go through the normal autofill
  // flow.
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());
}

// Test that the situation where no single field form fill conditions were met
// is handled correctly. The single field form fill conditions were not met
// because autocomplete is set to off and the field is not recognized as a promo
// code field.
TEST_F(
    BrowserAutofillManagerTest,
    SingleFieldFormFillSuggestions_NoneWhenSingleFieldFormFillConditionsNotMet) {
  browser_autofill_manager_->SetAutofillProfileEnabled(autofill_client_, false);
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              false);

  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});
  FormFieldData& field = form.fields[0];
  field.should_autocomplete = false;

  // Autocomplete is set to off, so suggestions should not get returned from
  // |single_field_form_fill_router()|.
  EXPECT_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions)
      .WillRepeatedly(Return(false));

  GetAutofillSuggestions(form, field);

  // Single field form fill was not triggered, so go through the normal autofill
  // flow.
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());
}

TEST_F(BrowserAutofillManagerTest,
       DestructorCancelsSingleFieldFormFillQueries) {
  EXPECT_CALL(single_field_form_fill_router(), CancelPendingQueries);
  browser_autofill_manager_.reset();
}

// Test that OnLoadedServerPredictions can obtain the FormStructure with the
// signature of the queried form from the API and apply type predictions.
// What we test here:
//  * The API response parser is used.
//  * The query can be processed with a response from the API.
TEST_F(BrowserAutofillManagerTest, OnLoadedServerPredictionsFromApi) {
  // First form on the page.
  FormData form;
  form.host_frame = test::MakeLocalFrameToken();
  form.unique_renderer_id = test::MakeFormRendererId();
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      CreateTestFormField(/*label=*/"City", /*name=*/"city", /*value=*/"",
                          /*type=*/FormControlType::kInputText),
      CreateTestFormField(/*label=*/"State", /*name=*/"state", /*value=*/"",
                          /*type=*/FormControlType::kInputText),
      CreateTestFormField(/*label=*/"Postal Code", /*name=*/"zipcode",
                          /*value=*/"", /*type=*/FormControlType::kInputText)};
  // Simulate having seen this form on page load.
  // |form_structure_instance| will be owned by |browser_autofill_manager_|.
  auto form_structure_instance = std::make_unique<FormStructure>(form);
  // This pointer is valid as long as autofill manager lives.
  FormStructure* form_structure = form_structure_instance.get();
  form_structure->DetermineHeuristicTypes(GeoIpCountryCode(""), nullptr,
                                          nullptr);
  browser_autofill_manager_->AddSeenFormStructure(
      std::move(form_structure_instance));

  // Second form on the page.
  FormData form2;
  form2.host_frame = test::MakeLocalFrameToken();
  form2.unique_renderer_id = test::MakeFormRendererId();
  form2.name = u"MyForm2";
  form2.url = GURL("https://myform.com/form.html");
  form2.action = GURL("https://myform.com/submit.html");
  form2.fields = {CreateTestFormField("Last Name", "lastname", "",
                                      FormControlType::kInputText),
                  CreateTestFormField("Middle Name", "middlename", "",
                                      FormControlType::kInputText),
                  CreateTestFormField("Postal Code", "zipcode", "",
                                      FormControlType::kInputText)};
  auto form_structure_instance2 = std::make_unique<FormStructure>(form2);
  // This pointer is valid as long as autofill manager lives.
  FormStructure* form_structure2 = form_structure_instance2.get();
  form_structure2->DetermineHeuristicTypes(GeoIpCountryCode(""), nullptr,
                                           nullptr);
  browser_autofill_manager_->AddSeenFormStructure(
      std::move(form_structure_instance2));

  // Make API response with suggestions.
  AutofillQueryResponse response;
  AutofillQueryResponse::FormSuggestion* form_suggestion;
  // Set suggestions for form 1.
  form_suggestion = response.add_form_suggestions();
  autofill::test::AddFieldPredictionToForm(form.fields[0], ADDRESS_HOME_CITY,
                                           form_suggestion);
  autofill::test::AddFieldPredictionToForm(form.fields[1], ADDRESS_HOME_STATE,
                                           form_suggestion);
  autofill::test::AddFieldPredictionToForm(form.fields[2], ADDRESS_HOME_ZIP,
                                           form_suggestion);
  // Set suggestions for form 2.
  form_suggestion = response.add_form_suggestions();
  autofill::test::AddFieldPredictionToForm(form2.fields[0], NAME_LAST,
                                           form_suggestion);
  autofill::test::AddFieldPredictionToForm(form2.fields[1], NAME_MIDDLE,
                                           form_suggestion);
  autofill::test::AddFieldPredictionToForm(form2.fields[2], ADDRESS_HOME_ZIP,
                                           form_suggestion);

  std::string response_string;
  ASSERT_TRUE(response.SerializeToString(&response_string));
  std::vector<FormSignature> signatures =
      test::GetEncodedSignatures({form_structure, form_structure2});

  // Run method under test.
  base::HistogramTester histogram_tester;
  test_api(*browser_autofill_manager_)
      .OnLoadedServerPredictions(base::Base64Encode(response_string),
                                 signatures);

  // Verify whether the relevant histograms were updated.
  histogram_tester.ExpectBucketCount("Autofill.ServerQueryResponse",
                                     AutofillMetrics::QUERY_RESPONSE_RECEIVED,
                                     1);
  histogram_tester.ExpectBucketCount("Autofill.ServerQueryResponse",
                                     AutofillMetrics::QUERY_RESPONSE_PARSED, 1);

  // We expect the server suggestions to have been applied to the first field of
  // the first form.
  EXPECT_EQ(ADDRESS_HOME_CITY,
            form_structure->field(0)->Type().GetStorableType());
  EXPECT_EQ(ADDRESS_HOME_STATE,
            form_structure->field(1)->Type().GetStorableType());
  EXPECT_EQ(ADDRESS_HOME_ZIP,
            form_structure->field(2)->Type().GetStorableType());
  // We expect the server suggestions to have been applied to the second form as
  // well.
  EXPECT_EQ(NAME_LAST, form_structure2->field(0)->Type().GetStorableType());
  EXPECT_EQ(NAME_MIDDLE, form_structure2->field(1)->Type().GetStorableType());
  EXPECT_EQ(ADDRESS_HOME_ZIP,
            form_structure2->field(2)->Type().GetStorableType());
}

// Test that OnLoadedServerPredictions does not call ParseQueryResponse if the
// BrowserAutofillManager has been reset between the time the query was sent and
// the response received.
TEST_F(BrowserAutofillManagerTest, OnLoadedServerPredictions_ResetManager) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();

  // Simulate having seen this form on page load.
  // |form_structure| will be owned by |browser_autofill_manager_|.
  auto form_structure = std::make_unique<FormStructure>(form);
  form_structure->DetermineHeuristicTypes(GeoIpCountryCode(""), nullptr,
                                          nullptr);
  std::vector<FormSignature> signatures =
      test::GetEncodedSignatures(*form_structure);
  browser_autofill_manager_->AddSeenFormStructure(std::move(form_structure));

  AutofillQueryResponse response;
  auto* form_suggestion = response.add_form_suggestions();
  form_suggestion->add_field_suggestions()->add_predictions()->set_type(3);
  for (int i = 0; i < 7; ++i) {
    form_suggestion->add_field_suggestions()->add_predictions()->set_type(0);
  }
  form_suggestion->add_field_suggestions()->add_predictions()->set_type(3);
  form_suggestion->add_field_suggestions()->add_predictions()->set_type(2);
  form_suggestion->add_field_suggestions()->add_predictions()->set_type(61);

  std::string response_string;
  ASSERT_TRUE(response.SerializeToString(&response_string));
  // Reset the manager (such as during a navigation).
  browser_autofill_manager_->Reset();

  base::HistogramTester histogram_tester;
  test_api(*browser_autofill_manager_)
      .OnLoadedServerPredictions(base::Base64Encode(response_string),
                                 signatures);

  // Verify that FormStructure::ParseQueryResponse was NOT called.
  histogram_tester.ExpectTotalCount("Autofill.ServerQueryResponse", 0);
}

// Test that when server predictions disagree with the heuristic ones, the
// overall types and sections would be set based on the server one.
TEST_F(BrowserAutofillManagerTest, DetermineHeuristicsWithOverallPrediction) {
  // Set up our form data.
  FormData form;
  form.url = GURL("https://www.myform.com");
  form.fields = {CreateTestFormField("First Name", "firstname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Last Name", "lastname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Card Number", "cardnumber", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Expiration Year", "exp_year", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Expiration Month", "exp_month", "",
                                     FormControlType::kInputText)};

  // Simulate having seen this form on page load.
  // |form_structure| will be owned by |browser_autofill_manager_|.
  FormStructure* form_structure = [&] {
    auto form_structure = std::make_unique<FormStructure>(form);
    FormStructure* ptr = form_structure.get();
    form_structure->DetermineHeuristicTypes(GeoIpCountryCode(""), nullptr,
                                            nullptr);
    browser_autofill_manager_->AddSeenFormStructure(std::move(form_structure));
    return ptr;
  }();

  AutofillQueryResponse response;
  auto* form_suggestion = response.add_form_suggestions();
  autofill::test::AddFieldPredictionToForm(
      form.fields[0], CREDIT_CARD_NAME_FIRST, form_suggestion);
  autofill::test::AddFieldPredictionToForm(
      form.fields[1], CREDIT_CARD_NAME_LAST, form_suggestion);
  autofill::test::AddFieldPredictionToForm(form.fields[2], CREDIT_CARD_NUMBER,
                                           form_suggestion);
  autofill::test::AddFieldPredictionToForm(
      form.fields[3], CREDIT_CARD_EXP_MONTH, form_suggestion);
  autofill::test::AddFieldPredictionToForm(
      form.fields[4], CREDIT_CARD_EXP_4_DIGIT_YEAR, form_suggestion);

  std::string response_string;
  ASSERT_TRUE(response.SerializeToString(&response_string));
  base::HistogramTester histogram_tester;
  test_api(*browser_autofill_manager_)
      .OnLoadedServerPredictions(base::Base64Encode(response_string),
                                 test::GetEncodedSignatures(*form_structure));
  // Verify that FormStructure::ParseQueryResponse was called (here and below).
  histogram_tester.ExpectBucketCount("Autofill.ServerQueryResponse",
                                     AutofillMetrics::QUERY_RESPONSE_RECEIVED,
                                     1);
  histogram_tester.ExpectBucketCount("Autofill.ServerQueryResponse",
                                     AutofillMetrics::QUERY_RESPONSE_PARSED, 1);

  // Since the card holder name appears as the first name + last name (rather
  // than the full name), and since they appears as the first fields of the
  // section, the heuristics detect them as the address first/last name.
  EXPECT_EQ(NAME_FIRST, form_structure->field(0)->heuristic_type());
  EXPECT_EQ(NAME_LAST, form_structure->field(1)->heuristic_type());
  EXPECT_EQ(CREDIT_CARD_NUMBER, form_structure->field(2)->heuristic_type());
  EXPECT_EQ(CREDIT_CARD_EXP_MONTH, form_structure->field(3)->heuristic_type());
  EXPECT_EQ(CREDIT_CARD_EXP_4_DIGIT_YEAR,
            form_structure->field(4)->heuristic_type());

  // We expect to see the server type as the overall type.
  EXPECT_EQ(CREDIT_CARD_NAME_FIRST,
            form_structure->field(0)->Type().GetStorableType());
  EXPECT_EQ(CREDIT_CARD_NAME_LAST,
            form_structure->field(1)->Type().GetStorableType());
  EXPECT_EQ(CREDIT_CARD_NUMBER, form_structure->field(2)->heuristic_type());
  EXPECT_EQ(CREDIT_CARD_EXP_MONTH, form_structure->field(3)->heuristic_type());
  EXPECT_EQ(CREDIT_CARD_EXP_4_DIGIT_YEAR,
            form_structure->field(4)->heuristic_type());

  // Although the heuristic types of the first two fields belongs to the address
  // section, the final fields' section should be based on the overall
  // prediction, therefore they should be grouped in one section.
  const auto section = form_structure->field(0)->section;
  EXPECT_EQ(section, form_structure->field(1)->section);
  EXPECT_EQ(section, form_structure->field(2)->section);
  EXPECT_EQ(section, form_structure->field(3)->section);
  EXPECT_EQ(section, form_structure->field(4)->section);
}

// Test that the form signature for an uploaded form always matches the form
// signature from the query.
TEST_F(BrowserAutofillManagerTest, FormSubmittedWithDifferentFields) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // Cache the expected form signature.
  std::string signature = FormStructure(form).FormSignatureAsStr();

  // Change the structure of the form prior to submission.
  // Websites would typically invoke JavaScript either on page load or on form
  // submit to achieve this.
  form.fields.pop_back();
  FormFieldData field = form.fields[3];
  form.fields[3] = form.fields[7];
  form.fields[7] = field;

  // Simulate form submission.
  FormSubmitted(form);
  EXPECT_EQ(signature, browser_autofill_manager_->GetSubmittedFormSignature());
}

// Test that we do not save form data when submitted fields contain default
// values.
TEST_F(BrowserAutofillManagerTest, FormSubmittedWithDefaultValues) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormFieldData* addr1_field = form.FindFieldByName(u"addr1");
  ASSERT_TRUE(addr1_field != nullptr);
  addr1_field->value = u"Enter your address";

  FormsSeen({form});

  // Fill the form.
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, *addr1_field, kElvisProfileGuid);
  // Set the address field's value back to the default value.
  response_data.fields[3].value = u"Enter your address";

  // Simulate form submission. The profile should not be updated with the
  // meaningless default value of the street address field.
  personal_data()
      .GetProfileByGUID(kElvisProfileGuid)
      ->ClearFields({ADDRESS_HOME_STREET_ADDRESS});
  FormSubmitted(response_data);
  EXPECT_FALSE(personal_data()
                   .GetProfileByGUID(kElvisProfileGuid)
                   ->HasInfo(ADDRESS_HOME_STREET_ADDRESS));
}

void DoTestFormSubmittedControlWithDefaultValue(
    BrowserAutofillManagerTest* test,
    FormControlType form_control_type) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();

  // Convert the state field to a <select> popup, to make sure that we only
  // reject default values for text fields.
  FormFieldData* state_field = form.FindFieldByName(u"state");
  ASSERT_TRUE(state_field != nullptr);
  state_field->form_control_type = form_control_type;
  state_field->value = base::UTF8ToUTF16(GetElvisAddressFillData().state);

  test->FormsSeen({form});

  // Fill the form.
  FormData response_data = test->FillAutofillFormDataAndGetResults(
      form, form.fields[3], kElvisProfileGuid);

  test->personal_data()
      .GetProfileByGUID(kElvisProfileGuid)
      ->ClearFields({ADDRESS_HOME_STATE});
  test->FormSubmitted(response_data);
  // Expect that the profile was updated with the value of the state select.
  EXPECT_EQ(state_field->value, test->personal_data()
                                    .GetProfileByGUID(kElvisProfileGuid)
                                    ->GetRawInfo(ADDRESS_HOME_STATE));
}

// Test that we save form data when a <select> in the form contains the
// default value.
TEST_F(BrowserAutofillManagerTest, FormSubmittedSelectWithDefaultValue) {
  DoTestFormSubmittedControlWithDefaultValue(this, FormControlType::kSelectOne);
}

// Test that we save form data when a <selectlist> in the form contains the
// default value.
TEST_F(BrowserAutofillManagerTest, FormSubmittedSelectListWithDefaultValue) {
  DoTestFormSubmittedControlWithDefaultValue(this,
                                             FormControlType::kSelectList);
}

void DoTestFormSubmittedNonAddressControlWithDefaultValue(
    BrowserAutofillManagerTest* test,
    FormControlType form_control_type) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();

  // Remove phonenumber field.
  auto phonenumber_it =
      base::ranges::find(form.fields, u"phonenumber", &FormFieldData::name);
  ASSERT_TRUE(phonenumber_it != form.fields.end());
  form.fields.erase(phonenumber_it);

  // Insert country code and national phone number fields.
  form.fields.push_back(CreateTestFormField("Country Code", "countrycode", "1",
                                            form_control_type,
                                            "tel-country-code"));
  form.fields.push_back(CreateTestFormField("Phone Number", "phonenumber", "",
                                            FormControlType::kInputText,
                                            "tel-national"));

  test->FormsSeen({form});

  // Fill the form.
  FormData response_data = test->FillAutofillFormDataAndGetResults(
      form, form.fields[3], kElvisProfileGuid);

  // Value of country code field should have been saved.
  test->personal_data()
      .GetProfileByGUID(kElvisProfileGuid)
      ->ClearFields({PHONE_HOME_WHOLE_NUMBER});
  test->FormSubmitted(response_data);
  std::u16string formatted_phone_number =
      test->personal_data()
          .GetProfileByGUID(kElvisProfileGuid)
          ->GetRawInfo(PHONE_HOME_WHOLE_NUMBER);
  std::u16string phone_number_numbers_only;
  base::RemoveChars(formatted_phone_number, u"+- ", &phone_number_numbers_only);
  EXPECT_TRUE(phone_number_numbers_only.starts_with(u"1"));
}

// Test that we save form data when a non-country, non-state <select> in the
// form contains the default value.
TEST_F(BrowserAutofillManagerTest,
       FormSubmittedNonAddressSelectWithDefaultValue) {
  DoTestFormSubmittedNonAddressControlWithDefaultValue(
      this, FormControlType::kSelectOne);
}

// Test that we save form data when a non-country, non-state <selectlist> in the
// form contains the default value.
TEST_F(BrowserAutofillManagerTest,
       FormSubmittedNonAddressSelectListWithDefaultValue) {
  DoTestFormSubmittedNonAddressControlWithDefaultValue(
      this, FormControlType::kSelectList);
}

struct ProfileMatchingTypesTestCase {
  const char* input_value;         // The value to input in the field.
  FieldTypeSet field_types;        // The expected field types to be determined.
};

class ProfileMatchingTypesTest
    : public BrowserAutofillManagerTest,
      public ::testing::WithParamInterface<ProfileMatchingTypesTestCase> {
 protected:
  void SetUp() override { BrowserAutofillManagerTest::SetUp(); }
};

const ProfileMatchingTypesTestCase kProfileMatchingTypesTestCases[] = {
    // Profile fields matches.
    {"Elvis", {NAME_FIRST}},
    {"Aaron", {NAME_MIDDLE}},
    {"A", {NAME_MIDDLE_INITIAL}},
    {"Presley", {NAME_LAST, NAME_LAST_SECOND}},
    {"Elvis Aaron Presley", {NAME_FULL}},
    {"theking@gmail.com", {EMAIL_ADDRESS}},
    {"RCA", {COMPANY_NAME}},
    {"3734 Elvis Presley Blvd.",
     {ADDRESS_HOME_LINE1, ADDRESS_HOME_STREET_LOCATION}},
    {"3734", {ADDRESS_HOME_HOUSE_NUMBER}},
    {"Elvis Presley Blvd.", {ADDRESS_HOME_STREET_NAME}},
    {"Apt. 10", {ADDRESS_HOME_LINE2, ADDRESS_HOME_SUBPREMISE}},
    {"Memphis", {ADDRESS_HOME_CITY}},
    {"Tennessee", {ADDRESS_HOME_STATE}},
    {"38116", {ADDRESS_HOME_ZIP}},
    {"USA", {ADDRESS_HOME_COUNTRY}},
    {"United States", {ADDRESS_HOME_COUNTRY}},
    {"12345678901", {PHONE_HOME_WHOLE_NUMBER}},
    {"+1 (234) 567-8901", {PHONE_HOME_WHOLE_NUMBER}},
    {"(234)567-8901", {PHONE_HOME_CITY_AND_NUMBER}},
    {"2345678901", {PHONE_HOME_CITY_AND_NUMBER}},
    {"1", {PHONE_HOME_COUNTRY_CODE}},
    {"234", {PHONE_HOME_CITY_CODE}},
    {"5678901", {PHONE_HOME_NUMBER}},
    {"567", {PHONE_HOME_NUMBER_PREFIX}},
    {"8901", {PHONE_HOME_NUMBER_SUFFIX}},

    // Test a European profile.
    {"Paris", {ADDRESS_HOME_CITY}},
    {"Île de France", {ADDRESS_HOME_STATE}},    // Exact match
    {"Ile de France", {ADDRESS_HOME_STATE}},    // Missing accent.
    {"-Ile-de-France-", {ADDRESS_HOME_STATE}},  // Extra punctuation.
    {"île dÉ FrÃÑÇË", {ADDRESS_HOME_STATE}},  // Other accents & case mismatch.
    {"75008", {ADDRESS_HOME_ZIP}},
    {"FR", {ADDRESS_HOME_COUNTRY}},
    {"France", {ADDRESS_HOME_COUNTRY}},
    {"33249197070", {PHONE_HOME_WHOLE_NUMBER}},
    {"+33 2 49 19 70 70", {PHONE_HOME_WHOLE_NUMBER}},
    {"02 49 19 70 70", {PHONE_HOME_CITY_AND_NUMBER}},
    {"0249197070", {PHONE_HOME_CITY_AND_NUMBER}},
    {"33", {PHONE_HOME_COUNTRY_CODE}},
    {"2", {PHONE_HOME_CITY_CODE}},

    // Credit card fields matches.
    {"John Doe", {CREDIT_CARD_NAME_FULL}},
    {"John", {CREDIT_CARD_NAME_FIRST}},
    {"Doe", {CREDIT_CARD_NAME_LAST}},
    {"4234-5678-9012-3456", {CREDIT_CARD_NUMBER}},
    {"04", {CREDIT_CARD_EXP_MONTH}},
    {"April", {CREDIT_CARD_EXP_MONTH}},
    {"2999", {CREDIT_CARD_EXP_4_DIGIT_YEAR}},
    {"99", {CREDIT_CARD_EXP_2_DIGIT_YEAR}},
    {"04/2999", {CREDIT_CARD_EXP_DATE_4_DIGIT_YEAR}},

    // Make sure whitespace and invalid characters are handled properly.
    {"", {EMPTY_TYPE}},
    {" ", {EMPTY_TYPE}},
    {"***", {UNKNOWN_TYPE}},
    {" Elvis", {NAME_FIRST}},
    {"Elvis ", {NAME_FIRST}},

    // Make sure fields that differ by case match.
    {"elvis ", {NAME_FIRST}},
    {"UnItEd StAtEs", {ADDRESS_HOME_COUNTRY}},

    // Make sure fields that differ by punctuation match.
    {"3734 Elvis Presley Blvd",
     {ADDRESS_HOME_LINE1, ADDRESS_HOME_STREET_LOCATION}},
    {"3734, Elvis    Presley Blvd.",
     {ADDRESS_HOME_LINE1, ADDRESS_HOME_STREET_LOCATION}},

    // Make sure that a state's full name and abbreviation match.
    {"TN", {ADDRESS_HOME_STATE}},     // Saved as "Tennessee" in profile.
    {"Texas", {ADDRESS_HOME_STATE}},  // Saved as "TX" in profile.

    // Special phone number case. A profile with no country code should
    // only match PHONE_HOME_CITY_AND_NUMBER.
    {"5142821292", {PHONE_HOME_CITY_AND_NUMBER}},

    // Make sure unsupported variants do not match.
    {"Elvis Aaron", {UNKNOWN_TYPE}},
    {"Mr. Presley", {UNKNOWN_TYPE}},
    {"3734 Elvis Presley", {UNKNOWN_TYPE}},
    {"38116-1023", {UNKNOWN_TYPE}},
    {"5", {UNKNOWN_TYPE}},
    {"56", {UNKNOWN_TYPE}},
    {"901", {UNKNOWN_TYPE}},
};

// Tests that DeterminePossibleFieldTypesForUpload finds accurate possible
// types.
TEST_P(ProfileMatchingTypesTest, DeterminePossibleFieldTypesForUpload) {
  // Unpack the test parameters
  const auto& test_case = GetParam();

  SCOPED_TRACE(base::StringPrintf(
      "Test: input_value='%s', field_type=%s, structured_names=%s ",
      test_case.input_value,
      FieldTypeToString(*test_case.field_types.begin()).c_str(), "true"));

  // Take the field types depending on the state of the structured names
  // feature.
  const FieldTypeSet& expected_possible_types = test_case.field_types;

  // Set up the test profiles.
  std::vector<AutofillProfile> profiles(
      3, AutofillProfile(i18n_model_definition::kLegacyHierarchyCountryCode));

  TestAddressFillData profile_info_data = GetElvisAddressFillData();
  profile_info_data.phone = "+1 (234) 567-8901";
  profiles[0] = FillDataToAutofillProfile(profile_info_data);

  profiles[0].set_guid(MakeGuid(1));

  test::SetProfileInfo(&profiles[1], "Charles", "", "Holley", "buddy@gmail.com",
                       "Decca", "123 Apple St.", "unit 6", "Lubbock", "TX",
                       "79401", "US", "5142821292");
  profiles[1].set_guid(MakeGuid(2));

  test::SetProfileInfo(&profiles[2], "Charles", "", "Baudelaire",
                       "lesfleursdumal@gmail.com", "", "108 Rue Saint-Lazare",
                       "Apt. 11", "Paris", "Île de France", "75008", "FR",
                       "+33 2 49 19 70 70");
  profiles[2].set_guid(MakeGuid(1));

  // Set up the test credit cards.
  std::vector<CreditCard> credit_cards;
  CreditCard credit_card;
  test::SetCreditCardInfo(&credit_card, "John Doe", "4234-5678-9012-3456", "04",
                          "2999", "1");
  credit_card.set_guid(MakeGuid(3));
  credit_cards.push_back(credit_card);

  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields.push_back(CreateTestFormField("", "1", test_case.input_value,
                                            FormControlType::kInputText));

  FormStructure form_structure(form);

  BrowserAutofillManagerTestApi::DeterminePossibleFieldTypesForUpload(
      profiles, credit_cards, std::u16string(), "en-us", &form_structure);

  ASSERT_EQ(1U, form_structure.field_count());

  FieldTypeSet possible_types = form_structure.field(0)->possible_types();
  EXPECT_EQ(possible_types, expected_possible_types);
}

// TODO(crbug.com/1395740). Remove parameter, once
// kAutofillVoteForSelectOptionValues has settled on stable.
class DeterminePossibleFieldTypesForUploadOfSelectTest
    : public BrowserAutofillManagerTest,
      public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override { BrowserAutofillManagerTest::SetUp(); }
};

void DoTestDeterminePossibleFieldTypesForUploadOfSelect(
    bool enable_autofill_vote_for_select_option_values,
    FormControlType field_type) {
  base::test::ScopedFeatureList features;
  features.InitWithFeatureState(features::kAutofillVoteForSelectOptionValues,
                                enable_autofill_vote_for_select_option_values);

  // Set up a profile and no credit cards.
  std::vector<AutofillProfile> profiles = {
      AutofillProfile(i18n_model_definition::kLegacyHierarchyCountryCode)};
  TestAddressFillData profile_info_data = GetElvisAddressFillData();
  profile_info_data.phone = "+1 (234) 567-8901";
  profiles[0] = FillDataToAutofillProfile(profile_info_data);
  profiles[0].set_guid(MakeGuid(1));
  std::vector<CreditCard> credit_cards;

  // Set up the form to be tested.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  // We want the "Memphis" in <option value="2">Memphis</option> to be
  // recognized.
  FormFieldData city_field = CreateTestSelectOrSelectListField(
      "label", "name", /*value=*/"2", /*autocomplete=*/"",
      /*values=*/{"1", "2", "3"},
      /*contents=*/{"New York", "Memphis", "Gotham City"}, field_type);

  // We want the +1 in <option value="US">USA (+1)</option> to be recognized
  // as a phone country code. Despite the value "US", we don't want this to be
  // recognized as a country field.
  FormFieldData phone_country_code_field = CreateTestSelectOrSelectListField(
      "label", "name", /*value=*/"US", /*autocomplete=*/"",
      /*values=*/{"US", "DE"}, /*contents=*/{"USA (+1)", "Germany (+49)"},
      field_type);

  form.fields = {city_field, phone_country_code_field};

  FormStructure form_structure(form);

  // Validate expectations.
  BrowserAutofillManagerTestApi::DeterminePossibleFieldTypesForUpload(
      profiles, credit_cards, std::u16string(), "en-us", &form_structure);

  ASSERT_EQ(2U, form_structure.field_count());
  if (base::FeatureList::IsEnabled(
          autofill::features::kAutofillVoteForSelectOptionValues)) {
    EXPECT_EQ(form_structure.field(0)->possible_types(),
              FieldTypeSet({ADDRESS_HOME_CITY}));
    EXPECT_EQ(form_structure.field(1)->possible_types(),
              FieldTypeSet({PHONE_HOME_COUNTRY_CODE}));
  } else {
    EXPECT_EQ(form_structure.field(0)->possible_types(),
              FieldTypeSet({UNKNOWN_TYPE}));
    EXPECT_EQ(form_structure.field(1)->possible_types(),
              FieldTypeSet({ADDRESS_HOME_COUNTRY}));
  }
}

// Tests that DeterminePossibleFieldTypesForUpload considers both the value
// and the human readable part of an <option> element in a <select> element:
// <option value="this is the value">this is the human readable part</option>
//
// In particular <option value="US">USA (+1)</option> is probably part of a
// phone number country code.
TEST_P(DeterminePossibleFieldTypesForUploadOfSelectTest,
       DeterminePossibleFieldTypesForUploadOfSelect) {
  DoTestDeterminePossibleFieldTypesForUploadOfSelect(
      /*enable_autofill_vote_for_select_option_values=*/GetParam(),
      FormControlType::kSelectOne);
}

// Tests that DeterminePossibleFieldTypesForUpload considers both the value
// and the human readable part of an <option> element in a <selectlist> element:
// <option value="this is the value">this is the human readable part</option>
//
// In particular <option value="US">USA (+1)</option> is probably part of a
// phone number country code.
TEST_P(DeterminePossibleFieldTypesForUploadOfSelectTest,
       DeterminePossibleFieldTypesForUploadOfSelectList) {
  DoTestDeterminePossibleFieldTypesForUploadOfSelect(
      /*enable_autofill_vote_for_select_option_values=*/GetParam(),
      FormControlType::kSelectList);
}

INSTANTIATE_TEST_SUITE_P(All,
                         DeterminePossibleFieldTypesForUploadOfSelectTest,
                         testing::Bool());

// Tests that DeterminePossibleFieldTypesForUpload is called when a form is
// submitted.
TEST_F(BrowserAutofillManagerTest,
       DeterminePossibleFieldTypesForUpload_IsTriggered) {
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  std::vector<FieldTypeSet> expected_types;
  std::vector<std::u16string> expected_values;

  // These fields should all match.
  FieldTypeSet types;

  expected_values.push_back(u"Elvis");
  types.clear();
  types.insert(NAME_FIRST);
  form.fields.push_back(
      CreateTestFormField("", "1", "", FormControlType::kInputText));
  expected_types.push_back(types);

  expected_values.push_back(u"Aaron");
  types.clear();
  types.insert(NAME_MIDDLE);
  form.fields.push_back(
      CreateTestFormField("", "2", "", FormControlType::kInputText));
  expected_types.push_back(types);

  expected_values.push_back(u"A");
  types.clear();
  types.insert(NAME_MIDDLE_INITIAL);
  form.fields.push_back(
      CreateTestFormField("", "3", "", FormControlType::kInputText));
  expected_types.push_back(types);

  // Make sure the form is in the cache so that it is processed for Autofill
  // upload.
  FormsSeen({form});

  // Once the form is cached, fill the values.
  EXPECT_EQ(form.fields.size(), expected_values.size());
  for (size_t i = 0; i < expected_values.size(); i++) {
    form.fields[i].value = expected_values[i];
  }

  browser_autofill_manager_->SetExpectedSubmittedFieldTypes(expected_types);
  FormSubmitted(form);
}

// Tests that DisambiguateUploadTypes makes the correct choices.
TEST_F(BrowserAutofillManagerTest, DisambiguateUploadTypes) {
  // Set up the test profile.
  std::vector<AutofillProfile> profiles;
  TestAddressFillData profile_info_data = GetElvisAddressFillData();
  profile_info_data.address2 = "";
  profile_info_data.phone = "(234) 567-8901";
  AutofillProfile profile = FillDataToAutofillProfile(profile_info_data);

  profile.set_guid(MakeGuid(1));
  profiles.push_back(profile);

  // Set up the test credit card.
  std::vector<CreditCard> credit_cards;
  CreditCard credit_card;
  test::SetCreditCardInfo(&credit_card, "Elvis Presley", "4234-5678-9012-3456",
                          "04", "2999", "1");
  credit_card.set_guid(MakeGuid(3));
  credit_cards.push_back(credit_card);

  struct TestFieldData {
    std::string input_value;
    FieldType predicted_type;
    bool expect_disambiguation;
    FieldType expected_upload_type;
  };
  using TestCase = std::vector<TestFieldData>;

  std::vector<TestCase> test_cases;

  // Address disambiguation.
  // An ambiguous address line followed by a field predicted as a line 2 and
  // that is empty should be disambiguated as an ADDRESS_HOME_LINE1.
  test_cases.push_back({{"3734 Elvis Presley Blvd.", ADDRESS_HOME_LINE1, true,
                         ADDRESS_HOME_LINE1},
                        {"", ADDRESS_HOME_LINE2, true, EMPTY_TYPE}});

  // An ambiguous address line followed by a field predicted as a line 2 but
  // filled with another know profile value should be disambiguated as an
  // ADDRESS_HOME_STREET_ADDRESS.
  test_cases.push_back(
      {{"3734 Elvis Presley Blvd.", ADDRESS_HOME_STREET_ADDRESS, true,
        ADDRESS_HOME_STREET_ADDRESS},
       {"38116", ADDRESS_HOME_LINE2, true, ADDRESS_HOME_ZIP}});

  // An ambiguous address line followed by an empty field predicted as
  // something other than a line 2 should be disambiguated as an
  // ADDRESS_HOME_STREET_ADDRESS.
  test_cases.push_back(
      {{"3734 Elvis Presley Blvd.", ADDRESS_HOME_STREET_ADDRESS, true,
        ADDRESS_HOME_STREET_ADDRESS},
       {"", ADDRESS_HOME_ZIP, true, EMPTY_TYPE}});

  // An ambiguous address line followed by no other field should be
  // disambiguated as an ADDRESS_HOME_STREET_ADDRESS.
  test_cases.push_back(
      {{"3734 Elvis Presley Blvd.", ADDRESS_HOME_STREET_ADDRESS, true,
        ADDRESS_HOME_STREET_ADDRESS}});

  // Name disambiguation.
  // An ambiguous name field that has no next field and that is preceded by
  // a non credit card field should be disambiguated as a non credit card
  // name.
  test_cases.push_back({{"Memphis", ADDRESS_HOME_CITY, true, ADDRESS_HOME_CITY},
                        {"Elvis", CREDIT_CARD_NAME_FIRST, true, NAME_FIRST},
                        {"Presley", CREDIT_CARD_NAME_LAST, true, NAME_LAST}});

  // An ambiguous name field that has no next field and that is preceded by
  // a credit card field should be disambiguated as a credit card name.
  test_cases.push_back(
      {{"4234-5678-9012-3456", CREDIT_CARD_NUMBER, true, CREDIT_CARD_NUMBER},
       {"Elvis", NAME_FIRST, true, CREDIT_CARD_NAME_FIRST},
       {"Presley", NAME_LAST, true, CREDIT_CARD_NAME_LAST}});

  // An ambiguous name field that has no previous field and that is
  // followed by a non credit card field should be disambiguated as a non
  // credit card name.
  test_cases.push_back(
      {{"Elvis", CREDIT_CARD_NAME_FIRST, true, NAME_FIRST},
       {"Presley", CREDIT_CARD_NAME_LAST, true, NAME_LAST},

       {"Memphis", ADDRESS_HOME_CITY, true, ADDRESS_HOME_CITY}});

  // An ambiguous name field that has no previous field and that is followed
  // by a credit card field should be disambiguated as a credit card name.
  test_cases.push_back(
      {{"Elvis", NAME_FIRST, true, CREDIT_CARD_NAME_FIRST},
       {"Presley", NAME_LAST, true, CREDIT_CARD_NAME_LAST},
       {"4234-5678-9012-3456", CREDIT_CARD_NUMBER, true, CREDIT_CARD_NUMBER}});

  // An ambiguous name field that is preceded and followed by non credit
  // card fields should be disambiguated as a non credit card name.
  test_cases.push_back(
      {{"Memphis", ADDRESS_HOME_CITY, true, ADDRESS_HOME_CITY},
       {"Elvis", CREDIT_CARD_NAME_FIRST, true, NAME_FIRST},
       {"Presley", CREDIT_CARD_NAME_LAST, true, NAME_LAST},
       {"Tennessee", ADDRESS_HOME_STATE, true, ADDRESS_HOME_STATE}});

  // An ambiguous name field that is preceded and followed by credit card
  // fields should be disambiguated as a credit card name.
  test_cases.push_back(
      {{"4234-5678-9012-3456", CREDIT_CARD_NUMBER, true, CREDIT_CARD_NUMBER},
       {"Elvis", NAME_FIRST, true, CREDIT_CARD_NAME_FIRST},
       {"Presley", NAME_LAST, true, CREDIT_CARD_NAME_LAST},
       {"2999", CREDIT_CARD_EXP_4_DIGIT_YEAR, true,
        CREDIT_CARD_EXP_4_DIGIT_YEAR}});

  // An ambiguous name field that is preceded by a non credit card field and
  // followed by a credit card field should not be disambiguated.
  test_cases.push_back({{"Memphis", ADDRESS_HOME_CITY, true, ADDRESS_HOME_CITY},
                        {"Elvis", NAME_FIRST, false, CREDIT_CARD_NAME_FIRST},
                        {"Presley", NAME_LAST, false, CREDIT_CARD_NAME_LAST},
                        {"2999", CREDIT_CARD_EXP_4_DIGIT_YEAR, true,
                         CREDIT_CARD_EXP_4_DIGIT_YEAR}});

  // An ambiguous name field that is preceded by a credit card field and
  // followed by a non credit card field should not be disambiguated.
  test_cases.push_back(
      {{"2999", CREDIT_CARD_EXP_4_DIGIT_YEAR, true,
        CREDIT_CARD_EXP_4_DIGIT_YEAR},
       {"Elvis", NAME_FIRST, false, CREDIT_CARD_NAME_FIRST},
       {"Presley", NAME_LAST, false, CREDIT_CARD_NAME_LAST},
       {"Memphis", ADDRESS_HOME_CITY, true, ADDRESS_HOME_CITY}});

  for (const TestCase& test_fields : test_cases) {
    FormData form;
    form.name = u"MyForm";
    form.url = GURL("https://myform.com/form.html");
    form.action = GURL("https://myform.com/submit.html");

    // Create the form fields specified in the test case.
    for (const TestFieldData& test_field : test_fields) {
      form.fields.push_back(CreateTestFormField("", "1", test_field.input_value,
                                                FormControlType::kInputText));
    }

    // Assign the specified predicted type for each field in the test case.
    FormStructure form_structure(form);
    for (size_t i = 0; i < test_fields.size(); ++i) {
      form_structure.field(i)->set_server_predictions(
          {::autofill::test::CreateFieldPrediction(
              test_fields[i].predicted_type)});
    }

    BrowserAutofillManagerTestApi::DeterminePossibleFieldTypesForUpload(
        profiles, credit_cards, std::u16string(), "en-us", &form_structure);
    ASSERT_EQ(test_fields.size(), form_structure.field_count());

    // Make sure the disambiguation method selects the expected upload type.
    FieldTypeSet possible_types;
    for (size_t i = 0; i < test_fields.size(); ++i) {
      possible_types = form_structure.field(i)->possible_types();
      if (test_fields[i].expect_disambiguation) {
        // It is possible that a field as two out of three
        // possible classifications: NAME_FULL, NAME_LAST,
        // NAME_LAST_FIRST/SECOND. Note, all cases contain NAME_LAST.
        // Alternatively, if the street address contains only one line, the
        // street address and the address line1 are identical resulting in a
        // vote for each.
        if (possible_types.size() == 2) {
          EXPECT_TRUE((possible_types.contains(NAME_LAST) &&
                       (possible_types.contains(NAME_LAST_SECOND) ||
                        possible_types.contains(NAME_LAST_FIRST) ||
                        possible_types.contains(NAME_FULL))) ||
                      (possible_types.contains(ADDRESS_HOME_LINE1) &&
                       possible_types.contains(ADDRESS_HOME_STREET_ADDRESS)));
        } else if (possible_types.size() == 3) {
          // Or even all three.
          EXPECT_TRUE((possible_types.contains(NAME_FULL) &&
                       possible_types.contains(NAME_LAST) &&
                       (possible_types.contains(NAME_LAST_SECOND) ||
                        possible_types.contains(NAME_LAST_FIRST))) ||
                      (possible_types.contains(ADDRESS_HOME_LINE1) &&
                       possible_types.contains(ADDRESS_HOME_STREET_ADDRESS) &&
                       possible_types.contains(ADDRESS_HOME_STREET_LOCATION)));
        } else {
          EXPECT_EQ(1U, possible_types.size());
        }
        EXPECT_NE(possible_types.end(),
                  possible_types.find(test_fields[i].expected_upload_type));
      } else {
        // In the context of those tests, it is expected that the type is
        // ambiguous.
        EXPECT_NE(1U, possible_types.size());
      }
    }
  }
}

// If a server-side credit card is unmasked by entering the CVC, the
// BrowserAutofillManager reuses the CVC value to identify a potentially
// existing CVC form field to cast a |CREDIT_CARD_VERIFICATION_CODE|-type vote.
TEST_F(BrowserAutofillManagerTest, CrowdsourceCVCFieldByValue) {
  std::vector<AutofillProfile> profiles;
  std::vector<CreditCard> credit_cards;

  constexpr char kCvc[] = "1234";
  constexpr char16_t kCvc16[] = u"1234";
  constexpr char kFourDigitButNotCvc[] = "6676";
  constexpr char kCreditCardNumber[] = "4234-5678-9012-3456";

  FormData form;
  form.fields = {
      CreateTestFormField("number", "number", kCreditCardNumber,
                          FormControlType::kInputText),
      // This field would not be detected as CVC heuristically if the CVC value
      // wouldn't be known.
      CreateTestFormField("not_cvc", "not_cvc", kFourDigitButNotCvc,
                          FormControlType::kInputText),
      // This field has the CVC value used to unlock the card and should be
      // detected as the CVC field.
      CreateTestFormField("c_v_c", "c_v_c", kCvc, FormControlType::kInputText)};

  FormStructure form_structure(form);
  form_structure.field(0)->set_possible_types({CREDIT_CARD_NUMBER});

  BrowserAutofillManagerTestApi::DeterminePossibleFieldTypesForUpload(
      profiles, credit_cards, kCvc16, "en-us", &form_structure);

  CheckThatOnlyFieldByIndexHasThisPossibleType(
      form_structure, 2, CREDIT_CARD_VERIFICATION_CODE,
      FieldPropertiesFlags::kKnownValue);
}

// Expiration year field was detected by the server. The other field with a
// 4-digit value should be detected as CVC.
TEST_F(BrowserAutofillManagerTest,
       CrowdsourceCVCFieldAfterInvalidExpDateByHeuristics) {
  constexpr char credit_card_number[] = "4234-5678-9012-3456";
  constexpr char actual_credit_card_exp_year[] = "2030";
  constexpr char user_entered_credit_card_exp_year[] = "2031";
  constexpr char cvc[] = "1234";

  FormData form;
  form.fields = {CreateTestFormField("number", "number", credit_card_number,
                                     FormControlType::kInputText),
                 // Expiration date, but is not the expiration date of the used
                 // credit card.
                 CreateTestFormField("exp_year", "exp_year",
                                     user_entered_credit_card_exp_year,
                                     FormControlType::kInputText),
                 // Must be CVC since expiration date was already identified.
                 CreateTestFormField("cvc_number", "cvc_number", cvc,
                                     FormControlType::kInputText)};

  FormStructure form_structure(form);

  // Set the field types.
  form_structure.field(0)->set_possible_types({CREDIT_CARD_NUMBER});
  form_structure.field(1)->set_possible_types({CREDIT_CARD_EXP_4_DIGIT_YEAR});
  form_structure.field(2)->set_possible_types({UNKNOWN_TYPE});

  // Set up the test credit cards.
  std::vector<CreditCard> credit_cards;
  CreditCard credit_card;
  test::SetCreditCardInfo(&credit_card, "John Doe", credit_card_number, "04",
                          actual_credit_card_exp_year, "1");
  credit_card.set_guid(MakeGuid(3));
  credit_cards.push_back(credit_card);

  // Set up the test profiles.
  std::vector<AutofillProfile> profiles;

  BrowserAutofillManagerTestApi::DeterminePossibleFieldTypesForUpload(
      profiles, credit_cards, std::u16string(), "en-us", &form_structure);

  CheckThatOnlyFieldByIndexHasThisPossibleType(form_structure, 2,
                                               CREDIT_CARD_VERIFICATION_CODE,
                                               FieldPropertiesFlags::kNoFlags);
}

// Tests if the CVC field is heuristically detected if it appears after the
// expiration year field as it was predicted by the server.
// The value in the CVC field would be a valid expiration year value.
TEST_F(BrowserAutofillManagerTest,
       CrowdsourceCVCFieldAfterExpDateByHeuristics) {
  constexpr char credit_card_number[] = "4234-5678-9012-3456";
  constexpr char actual_credit_card_exp_year[] = "2030";
  constexpr char cvc[] = "1234";

  FormData form;
  form.fields = {
      CreateTestFormField("number", "number", credit_card_number,
                          FormControlType::kInputText),
      // Expiration date, that is the expiration date of the used credit card.
      CreateTestFormField("date_or_cvc1", "date_or_cvc1",
                          actual_credit_card_exp_year,
                          FormControlType::kInputText),
      // Must be CVC since expiration date was already identified.
      CreateTestFormField("date_or_cvc2", "date_or_cvc2", cvc,
                          FormControlType::kInputText)};

  FormStructure form_structure(form);

  // Set the field types.
  form_structure.field(0)->set_possible_types({CREDIT_CARD_NUMBER});
  form_structure.field(1)->set_possible_types({CREDIT_CARD_EXP_4_DIGIT_YEAR});
  form_structure.field(2)->set_possible_types({UNKNOWN_TYPE});

  // Set up the test credit cards.
  std::vector<CreditCard> credit_cards;
  CreditCard credit_card;
  test::SetCreditCardInfo(&credit_card, "John Doe", credit_card_number, "04",
                          actual_credit_card_exp_year, "1");
  credit_card.set_guid(MakeGuid(3));
  credit_cards.push_back(credit_card);

  // Set up the test profiles.
  std::vector<AutofillProfile> profiles;

  BrowserAutofillManagerTestApi::DeterminePossibleFieldTypesForUpload(
      profiles, credit_cards, std::u16string(), "en-us", &form_structure);

  CheckThatOnlyFieldByIndexHasThisPossibleType(form_structure, 2,
                                               CREDIT_CARD_VERIFICATION_CODE,
                                               FieldPropertiesFlags::kNoFlags);
}

// Tests if the CVC field is heuristically detected if it contains a value which
// is not a valid expiration year.
TEST_F(BrowserAutofillManagerTest,
       CrowdsourceCVCFieldBeforeExpDateByHeuristics) {
  constexpr char credit_card_number[] = "4234-5678-9012-3456";
  constexpr char actual_credit_card_exp_year[] = "2030";
  constexpr char user_entered_credit_card_exp_year[] = "2031";

  FormData form;
  form.fields = {CreateTestFormField("number", "number", credit_card_number,
                                     FormControlType::kInputText),
                 // Must be CVC since it is an implausible expiration date.
                 CreateTestFormField("date_or_cvc2", "date_or_cvc2", "2130",
                                     FormControlType::kInputText),
                 // A field which is filled with a plausible expiration date
                 // which is not the date of the credit card.
                 CreateTestFormField("date_or_cvc1", "date_or_cvc1",
                                     user_entered_credit_card_exp_year,
                                     FormControlType::kInputText)};

  FormStructure form_structure(form);

  // Set the field types.
  form_structure.field(0)->set_possible_types({CREDIT_CARD_NUMBER});
  form_structure.field(1)->set_possible_types({UNKNOWN_TYPE});
  form_structure.field(2)->set_possible_types({UNKNOWN_TYPE});

  // Set up the test credit cards.
  std::vector<CreditCard> credit_cards;
  CreditCard credit_card;
  test::SetCreditCardInfo(&credit_card, "John Doe", credit_card_number, "04",
                          actual_credit_card_exp_year, "1");
  credit_card.set_guid(MakeGuid(3));
  credit_cards.push_back(credit_card);

  // Set up the test profiles.
  std::vector<AutofillProfile> profiles;

  BrowserAutofillManagerTestApi::DeterminePossibleFieldTypesForUpload(
      profiles, credit_cards, std::u16string(), "en-us", &form_structure);

  CheckThatOnlyFieldByIndexHasThisPossibleType(form_structure, 1,
                                               CREDIT_CARD_VERIFICATION_CODE,
                                               FieldPropertiesFlags::kNoFlags);
}

// Tests if no CVC field is heuristically detected due to the missing of a
// credit card number field.
TEST_F(BrowserAutofillManagerTest,
       CrowdsourceNoCVCFieldDueToMissingCreditCardNumber) {
  constexpr char credit_card_number[] = "4234-5678-9012-3456";
  constexpr char actual_credit_card_exp_year[] = "2030";
  constexpr char user_entered_credit_card_exp_year[] = "2031";
  constexpr char cvc[] = "2031";

  FormData form;
  form.fields = {CreateTestFormField("number", "number", credit_card_number,
                                     FormControlType::kInputText),
                 // Server predicted as expiration year.
                 CreateTestFormField("date_or_cvc1", "date_or_cvc1",
                                     user_entered_credit_card_exp_year,
                                     FormControlType::kInputText),
                 // Must be CVC since expiration date was already identified.
                 CreateTestFormField("date_or_cvc2", "date_or_cvc2", cvc,
                                     FormControlType::kInputText)};

  FormStructure form_structure(form);

  // Set the field types.
  form_structure.field(0)->set_possible_types({UNKNOWN_TYPE});
  form_structure.field(1)->set_possible_types({CREDIT_CARD_EXP_4_DIGIT_YEAR});
  form_structure.field(2)->set_possible_types({UNKNOWN_TYPE});

  // Set up the test credit cards.
  std::vector<CreditCard> credit_cards;
  CreditCard credit_card;
  test::SetCreditCardInfo(&credit_card, "John Doe", credit_card_number, "04",
                          actual_credit_card_exp_year, "1");
  credit_card.set_guid(MakeGuid(3));
  credit_cards.push_back(credit_card);

  // Set up the test profiles.
  std::vector<AutofillProfile> profiles;

  BrowserAutofillManagerTestApi::DeterminePossibleFieldTypesForUpload(
      profiles, credit_cards, std::u16string(), "en-us", &form_structure);
  CheckThatNoFieldHasThisPossibleType(form_structure,
                                      CREDIT_CARD_VERIFICATION_CODE);
}

// Test if no CVC is found because the candidate has no valid CVC value.
TEST_F(BrowserAutofillManagerTest, CrowdsourceNoCVCDueToInvalidCandidateValue) {
  constexpr char credit_card_number[] = "4234-5678-9012-3456";
  constexpr char credit_card_exp_year[] = "2030";
  constexpr char cvc[] = "12";

  FormData form;
  form.fields = {
      CreateTestFormField("number", "number", credit_card_number,
                          FormControlType::kInputText),
      // Server predicted as expiration year.
      CreateTestFormField("date_or_cvc1", "date_or_cvc1", credit_card_exp_year,
                          FormControlType::kInputText),
      // Must be CVC since expiration date was already identified.
      CreateTestFormField("date_or_cvc2", "date_or_cvc2", cvc,
                          FormControlType::kInputText)};

  FormStructure form_structure(form);

  // Set the field types.
  form_structure.field(0)->set_possible_types(
      {CREDIT_CARD_NUMBER, UNKNOWN_TYPE});
  form_structure.field(1)->set_possible_types({CREDIT_CARD_EXP_4_DIGIT_YEAR});
  form_structure.field(2)->set_possible_types({UNKNOWN_TYPE});

  // Set up the test credit cards.
  std::vector<CreditCard> credit_cards;
  CreditCard credit_card;
  test::SetCreditCardInfo(&credit_card, "John Doe", credit_card_number, "04",
                          credit_card_exp_year, "1");
  credit_card.set_guid(MakeGuid(3));
  credit_cards.push_back(credit_card);

  // Set up the test profiles.
  std::vector<AutofillProfile> profiles;

  BrowserAutofillManagerTestApi::DeterminePossibleFieldTypesForUpload(
      profiles, credit_cards, std::u16string(), "en-us", &form_structure);

  CheckThatNoFieldHasThisPossibleType(form_structure,
                                      CREDIT_CARD_VERIFICATION_CODE);
}

TEST_F(BrowserAutofillManagerTest, RemoveProfile) {
  // Add and remove an Autofill profile.
  AutofillProfile profile = test::GetFullProfile();
  personal_data().AddProfile(profile);

  EXPECT_TRUE(browser_autofill_manager_->RemoveAutofillProfileOrCreditCard(
      Suggestion::Guid(profile.guid())));

  EXPECT_FALSE(personal_data().GetProfileByGUID(profile.guid()));
}

TEST_F(BrowserAutofillManagerTest, RemoveLocalCreditCard) {
  // Add and remove an Autofill credit card.
  CreditCard local_card = test::GetCreditCard();
  personal_data().AddCreditCard(local_card);

  EXPECT_TRUE(browser_autofill_manager_->RemoveAutofillProfileOrCreditCard(
      Suggestion::Guid(local_card.guid())));

  EXPECT_FALSE(personal_data().GetCreditCardByGUID(local_card.guid()));
}

TEST_F(BrowserAutofillManagerTest, RemoveServerCreditCard) {
  CreditCard server_card = test::GetMaskedServerCard();
  personal_data().AddServerCreditCard(server_card);

  EXPECT_FALSE(browser_autofill_manager_->RemoveAutofillProfileOrCreditCard(
      Suggestion::Guid(server_card.guid())));

  EXPECT_TRUE(personal_data().GetCreditCardByGUID(server_card.guid()));
}

// Test our external delegate is called at the right time.
TEST_F(BrowserAutofillManagerTest, TestExternalDelegate) {
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});
  // Should call the delegate's OnQuery().
  GetAutofillSuggestions(form, form.fields[0]);

  EXPECT_TRUE(external_delegate()->on_query_seen());
}

// Test that unfocusing a filled form sends an upload with types matching the
// fields.
TEST_F(BrowserAutofillManagerTest, OnTextFieldDidChangeAndUnfocus_Upload) {
  // Set up our form data (it's already filled out with user data).
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  std::vector<FieldTypeSet> expected_types;
  FieldTypeSet types;

  form.fields.push_back(CreateTestFormField("First Name", "firstname", "",
                                            FormControlType::kInputText));
  types.insert(NAME_FIRST);
  expected_types.push_back(types);

  form.fields.push_back(CreateTestFormField("Last Name", "lastname", "",
                                            FormControlType::kInputText));
  types.clear();
  types.insert(NAME_LAST);
  types.insert(NAME_LAST_SECOND);
  expected_types.push_back(types);

  form.fields.push_back(
      CreateTestFormField("Email", "email", "", FormControlType::kInputText));
  types.clear();
  types.insert(EMAIL_ADDRESS);
  expected_types.push_back(types);

  FormsSeen({form});

  // We will expect these types in the upload and no observed submission (the
  // callback initiated by WaitForAsyncUploadProcess checks these expectations.)
  browser_autofill_manager_->SetExpectedSubmittedFieldTypes(expected_types);
  browser_autofill_manager_->SetExpectedObservedSubmission(false);

  // The fields are edited after calling FormsSeen on them. This is because
  // default values are not used for upload comparisons.
  form.fields[0].value = u"Elvis";
  form.fields[1].value = u"Presley";
  form.fields[2].value = u"theking@gmail.com";
  // Simulate editing a field.
  browser_autofill_manager_->OnTextFieldDidChange(
      form, form.fields.front(), gfx::RectF(), base::TimeTicks::Now());

  // Simulate lost of focus on the form.
  browser_autofill_manager_->OnFocusNoLongerOnForm(true);
}

// Test that navigating with a filled form sends an upload with types matching
// the fields.
TEST_F(BrowserAutofillManagerTest, OnTextFieldDidChangeAndNavigation_Upload) {
  // Set up our form data (it's already filled out with user data).
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  std::vector<FieldTypeSet> expected_types;
  FieldTypeSet types;

  form.fields.push_back(CreateTestFormField("First Name", "firstname", "",
                                            FormControlType::kInputText));
  types.insert(NAME_FIRST);
  expected_types.push_back(types);

  form.fields.push_back(CreateTestFormField("Last Name", "lastname", "",
                                            FormControlType::kInputText));
  types.clear();
  types.insert(NAME_LAST_SECOND);
  types.insert(NAME_LAST);
  expected_types.push_back(types);

  form.fields.push_back(
      CreateTestFormField("Email", "email", "", FormControlType::kInputText));
  types.clear();
  types.insert(EMAIL_ADDRESS);
  expected_types.push_back(types);

  FormsSeen({form});

  // We will expect these types in the upload and no observed submission. (the
  // callback initiated by WaitForAsyncUploadProcess checks these expectations.)
  browser_autofill_manager_->SetExpectedSubmittedFieldTypes(expected_types);
  browser_autofill_manager_->SetExpectedObservedSubmission(false);

  // The fields are edited after calling FormsSeen on them. This is because
  // default values are not used for upload comparisons.
  form.fields[0].value = u"Elvis";
  form.fields[1].value = u"Presley";
  form.fields[2].value = u"theking@gmail.com";
  // Simulate editing a field.
  browser_autofill_manager_->OnTextFieldDidChange(
      form, form.fields.front(), gfx::RectF(), base::TimeTicks::Now());

  // Simulate a navigation so that the pending form is uploaded.
  browser_autofill_manager_->Reset();
}

// Test that unfocusing a filled form sends an upload with types matching the
// fields.
TEST_F(BrowserAutofillManagerTest, OnDidFillAutofillFormDataAndUnfocus_Upload) {
  // Set up our form data (empty).
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  std::vector<FieldTypeSet> expected_types;

  // These fields should all match.
  FieldTypeSet types;
  form.fields.push_back(CreateTestFormField("First Name", "firstname", "",
                                            FormControlType::kInputText));
  types.insert(NAME_FIRST);
  expected_types.push_back(types);

  form.fields.push_back(CreateTestFormField("Last Name", "lastname", "",
                                            FormControlType::kInputText));
  types.clear();
  types.insert(NAME_LAST_SECOND);
  types.insert(NAME_LAST);
  expected_types.push_back(types);

  form.fields.push_back(
      CreateTestFormField("Email", "email", "", FormControlType::kInputText));
  types.clear();
  types.insert(EMAIL_ADDRESS);
  expected_types.push_back(types);

  FormsSeen({form});

  // We will expect these types in the upload and no observed submission. (the
  // callback initiated by WaitForAsyncUploadProcess checks these expectations.)
  browser_autofill_manager_->SetExpectedSubmittedFieldTypes(expected_types);
  browser_autofill_manager_->SetExpectedObservedSubmission(false);

  // Form was autofilled with user data.
  form.fields[0].value = u"Elvis";
  form.fields[1].value = u"Presley";
  form.fields[2].value = u"theking@gmail.com";
  browser_autofill_manager_->OnDidFillAutofillFormData(form,
                                                       base::TimeTicks::Now());

  // Simulate lost of focus on the form.
  browser_autofill_manager_->OnFocusNoLongerOnForm(true);
}

// Test that suggestions are returned for credit card fields with an
// unrecognized
// autocomplete attribute.
TEST_F(BrowserAutofillManagerTest,
       GetCreditCardSuggestions_UnrecognizedAttribute) {
  // Set up the form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      // Set a valid autocomplete attribute on the card name.
      CreateTestFormField("Name on Card", "nameoncard", "",
                          FormControlType::kInputText, "cc-name"),
      // Set no autocomplete attribute on the card number.
      CreateTestFormField("Card Number", "cardnumber", "",
                          FormControlType::kInputText),
      // Set an unrecognized autocomplete attribute on the expiration month.
      CreateTestFormField("Expiration Date", "ccmonth", "",
                          FormControlType::kInputText, "unrecognized")};
  FormsSeen({form});

  // Suggestions should be returned for the first two fields
  GetAutofillSuggestions(form, form.fields[0]);
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());
  GetAutofillSuggestions(form, form.fields[1]);
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());

  // Suggestions should still be returned for the third field because it is a
  // credit card field.
  GetAutofillSuggestions(form, form.fields[2]);
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());
}

// Test to verify suggestions appears for forms having credit card number split
// across fields.
TEST_P(BrowserAutofillManagerTestForMetadataCardSuggestions,
       GetCreditCardSuggestions_ForNumberSplitAcrossFields) {
  // Set up our form data with credit card number split across fields.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {CreateTestFormField("Name on Card", "nameoncard", "",
                                     FormControlType::kInputText)};

  // Add new 4 |card_number_field|s to the |form|.
  constexpr uint64_t kMaxLength = 4;
  form.fields.push_back(CreateTestFormField("Card Number", "cardnumber_1", "",
                                            FormControlType::kInputText, "",
                                            kMaxLength));
  form.fields.push_back(CreateTestFormField(
      "", "cardnumber_2", "", FormControlType::kInputText, "", kMaxLength));
  form.fields.push_back(CreateTestFormField(
      "", "cardnumber_3", "", FormControlType::kInputText, "", kMaxLength));
  form.fields.push_back(CreateTestFormField(
      "", "cardnumber_4", "", FormControlType::kInputText, "", kMaxLength));

  form.fields.push_back(CreateTestFormField("Expiration Date", "ccmonth", "",
                                            FormControlType::kInputText));
  form.fields.push_back(
      CreateTestFormField("", "ccyear", "", FormControlType::kInputText));

  FormsSeen({form});

  // Verify whether suggestions are populated correctly for one of the middle
  // credit card number fields when filled partially.
  FormFieldData number_field = form.fields[3];
  number_field.value = u"901";

  // Get the suggestions for already filled credit card |number_field|.
  GetAutofillSuggestions(form, number_field);

  external_delegate()->CheckSuggestions(
      form.fields[3].global_id(),
      {GetCardSuggestion(kVisaCard),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Test that inputs detected to be CVC inputs are forced to
// !should_autocomplete for SingleFieldFormFillRouter::OnWillSubmitForm.
TEST_F(BrowserAutofillManagerTest, DontSaveCvcInAutocompleteHistory) {
  FormData form_seen_by_ahm;
  EXPECT_CALL(single_field_form_fill_router(), OnWillSubmitForm(_, _, true))
      .WillOnce(SaveArg<0>(&form_seen_by_ahm));

  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  struct {
    const char* label;
    const char* name;
    const char* value;
    FieldType expected_field_type;
  } test_fields[] = {
      {"Card number", "1", "4234-5678-9012-3456", CREDIT_CARD_NUMBER},
      {"Card verification code", "2", "123", CREDIT_CARD_VERIFICATION_CODE},
      {"expiration date", "3", "04/2020", CREDIT_CARD_EXP_4_DIGIT_YEAR},
  };

  for (const auto& test_field : test_fields) {
    form.fields.push_back(CreateTestFormField(test_field.label, test_field.name,
                                              test_field.value,
                                              FormControlType::kInputText));
  }

  FormsSeen({form});
  FormSubmitted(form);

  EXPECT_EQ(form.fields.size(), form_seen_by_ahm.fields.size());
  ASSERT_EQ(std::size(test_fields), form_seen_by_ahm.fields.size());
  for (size_t i = 0; i < std::size(test_fields); ++i) {
    EXPECT_EQ(
        form_seen_by_ahm.fields[i].should_autocomplete,
        test_fields[i].expected_field_type != CREDIT_CARD_VERIFICATION_CODE);
  }
}

TEST_F(BrowserAutofillManagerTest, DontOfferToSavePaymentsCard) {
  FormData form;
  CreditCard card;
  PrepareForRealPanResponse(&form, &card);

  // Manually fill out |form| so we can use it in OnFormSubmitted.
  for (auto& field : form.fields) {
    if (field.name == u"cardnumber")
      field.value = u"4012888888881881";
    else if (field.name == u"nameoncard")
      field.value = u"John H Dillinger";
    else if (field.name == u"ccmonth")
      field.value = u"01";
    else if (field.name == u"ccyear")
      field.value = u"2017";
  }

  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  full_card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess,
                  "4012888888881881");
  browser_autofill_manager_->OnFormSubmitted(form, false,
                                             SubmissionSource::FORM_SUBMISSION);
}

TEST_F(BrowserAutofillManagerTest, FillInUpdatedExpirationDate) {
  FormData form;
  CreditCard card;
  PrepareForRealPanResponse(&form, &card);

  CardUnmaskDelegate::UserProvidedUnmaskDetails details;
  details.cvc = u"123";
  details.exp_month = u"02";
  details.exp_year = u"2018";
  full_card_unmask_delegate()->OnUnmaskPromptAccepted(details);
  OnDidGetRealPan(AutofillClient::PaymentsRpcResult::kSuccess,
                  "4012888888881881");
}

TEST_F(BrowserAutofillManagerTest, ProfileDisabledDoesNotFillFormData) {
  browser_autofill_manager_->SetAutofillProfileEnabled(autofill_client_, false);

  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  // Expect no fields filled, no form data sent to renderer.
  EXPECT_CALL(*autofill_driver_, ApplyFormAction).Times(0);

  FillAutofillFormData(form, *form.fields.begin(), MakeGuid(1));
}

TEST_F(BrowserAutofillManagerTest, ProfileDisabledDoesNotSuggest) {
  browser_autofill_manager_->SetAutofillProfileEnabled(autofill_client_, false);

  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  GetAutofillSuggestions(
      form,
      CreateTestFormField("Email", "email", "", FormControlType::kInputEmail));
  // Expect no suggestions as autofill and autocomplete are disabled for
  // addresses.
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

TEST_F(BrowserAutofillManagerTest, CreditCardDisabledDoesNotFillFormData) {
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              false);

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  // Expect no fields filled, no form data sent to renderer.
  EXPECT_CALL(*autofill_driver_, ApplyFormAction).Times(0);
  FillAutofillFormData(form, *form.fields.begin(), MakeGuid(4));
}

TEST_F(BrowserAutofillManagerTest, CreditCardDisabledDoesNotSuggest) {
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              false);

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  GetAutofillSuggestions(form,
                         CreateTestFormField("Name on Card", "nameoncard", "",
                                             FormControlType::kInputText));
  // Expect no suggestions as autofill and autocomplete are disabled for credit
  // cards.
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

TEST_F(BrowserAutofillManagerTest, ShouldUploadForm) {
  // Note: The enforcement of a minimum number of required fields for upload
  // is disabled by default. This tests validates both the disabled and enabled
  // scenarios.
  FormData form;
  form.name = u"TestForm";
  form.url = GURL("https://example.com/form.html");
  form.action = GURL("https://example.com/submit.html");

  // Empty Form.
  EXPECT_FALSE(
      browser_autofill_manager_->ShouldUploadForm(FormStructure(form)));

  // Add a field to the form.
  form.fields.push_back(
      CreateTestFormField("Name", "name", "", FormControlType::kInputText));

  EXPECT_TRUE(browser_autofill_manager_->ShouldUploadForm(FormStructure(form)));

  // Add a second field to the form.
  form.fields.push_back(
      CreateTestFormField("Email", "email", "", FormControlType::kInputText));

  EXPECT_TRUE(browser_autofill_manager_->ShouldUploadForm(FormStructure(form)));

  // Has less than 3 fields but has autocomplete attribute.
  constexpr char autocomplete[] = "given-name";
  form.fields[0].autocomplete_attribute = autocomplete;
  form.fields[0].parsed_autocomplete = ParseAutocompleteAttribute(autocomplete);

  EXPECT_TRUE(browser_autofill_manager_->ShouldUploadForm(FormStructure(form)));

  // Has more than 3 fields, no autocomplete attribute.
  form.fields.push_back(CreateTestFormField("Country", "country", "",
                                            FormControlType::kInputText, ""));
  FormStructure form_structure_3(form);
  EXPECT_TRUE(browser_autofill_manager_->ShouldUploadForm(FormStructure(form)));

  // Has more than 3 fields and at least one autocomplete attribute.
  form.fields[0].autocomplete_attribute = autocomplete;
  form.fields[0].parsed_autocomplete = ParseAutocompleteAttribute(autocomplete);
  EXPECT_TRUE(browser_autofill_manager_->ShouldUploadForm(FormStructure(form)));

  // Is off the record.
  autofill_client_.set_is_off_the_record(true);
  EXPECT_FALSE(
      browser_autofill_manager_->ShouldUploadForm(FormStructure(form)));

  // Make sure it's reset for the next test case.
  autofill_client_.set_is_off_the_record(false);
  EXPECT_TRUE(browser_autofill_manager_->ShouldUploadForm(FormStructure(form)));

  // Has one field which is appears to be a password field.
  form.fields = {CreateTestFormField("Password", "password", "",
                                     FormControlType::kInputPassword)};

  // With min required fields disabled.
  EXPECT_TRUE(browser_autofill_manager_->ShouldUploadForm(FormStructure(form)));

  // Autofill disabled.
  browser_autofill_manager_->SetAutofillProfileEnabled(autofill_client_, false);
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              false);
  EXPECT_FALSE(
      browser_autofill_manager_->ShouldUploadForm(FormStructure(form)));
}

// Verify that no suggestions are shown on desktop for non credit card related
// fields if the initiating field has the "autocomplete" attribute set to off.
TEST_F(BrowserAutofillManagerTest,
       DisplaySuggestions_AutocompleteOffNotRespected_AddressField) {
  // Set up an address form.
  FormData mixed_form;
  mixed_form.name = u"MyForm";
  mixed_form.url = GURL("https://myform.com/form.html");
  mixed_form.action = GURL("https://myform.com/submit.html");
  mixed_form.fields.push_back(CreateTestFormField("First name", "firstname", "",
                                                  FormControlType::kInputText));
  mixed_form.fields.back().should_autocomplete = false;
  mixed_form.fields.push_back(CreateTestFormField("Last name", "lastname", "",
                                                  FormControlType::kInputText));
  mixed_form.fields.push_back(CreateTestFormField("Address", "address", "",
                                                  FormControlType::kInputText));
  FormsSeen({mixed_form});

  // Suggestions should be displayed on desktop for this field in all
  // circumstances.
  GetAutofillSuggestions(mixed_form, mixed_form.fields[0]);
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());

  // Suggestions should always be displayed for all the other fields.
  for (size_t i = 1U; i < mixed_form.fields.size(); ++i) {
    GetAutofillSuggestions(mixed_form, mixed_form.fields[i]);
    EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());
  }
}

// Verify that suggestions are shown on desktop for credit card related fields
// even if the initiating field has the "autocomplete" attribute set to off.
TEST_F(BrowserAutofillManagerTest,
       DisplaySuggestions_AutocompleteOff_CreditCardField) {
  // Set up a credit card form.
  FormData mixed_form;
  mixed_form.name = u"MyForm";
  mixed_form.url = GURL("https://myform.com/form.html");
  mixed_form.action = GURL("https://myform.com/submit.html");
  mixed_form.fields = {CreateTestFormField("Name on Card", "nameoncard", "",
                                           FormControlType::kInputText)};
  mixed_form.fields.back().should_autocomplete = false;
  mixed_form.fields.push_back(CreateTestFormField(
      "Card Number", "cardnumber", "", FormControlType::kInputText));
  mixed_form.fields.push_back(CreateTestFormField(
      "Expiration Month", "ccexpiresmonth", "", FormControlType::kInputText));
  mixed_form.fields.back().should_autocomplete = false;
  FormsSeen({mixed_form});

  // Suggestions should always be displayed.
  for (const FormFieldData& mixed_form_field : mixed_form.fields) {
    // Single field form fill suggestions being returned are directly correlated
    // to whether or not the field has autocomplete set to true or false. We
    // know autocomplete must be the single field form filler in this case due
    // to the field not having a type that would route to any of the other
    // single field form fillers.
    ON_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions)
        .WillByDefault(Return(mixed_form_field.should_autocomplete));
    GetAutofillSuggestions(mixed_form, mixed_form_field);

    EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());
  }
}

// Tests that a form with server only types is still autofillable if the form
// gets updated in cache.
TEST_F(BrowserAutofillManagerTest,
       DisplaySuggestionsForUpdatedServerTypedForm) {
  // Create a form with unknown heuristic fields.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      CreateTestFormField("Field 1", "field1", "", FormControlType::kInputText),
      CreateTestFormField("Field 2", "field2", "", FormControlType::kInputText),
      CreateTestFormField("Field 3", "field3", "",
                          FormControlType::kInputText)};

  auto form_structure = std::make_unique<FormStructure>(form);
  form_structure->DetermineHeuristicTypes(GeoIpCountryCode(""), nullptr,
                                          nullptr);
  // Make sure the form can not be autofilled now.
  ASSERT_EQ(0u, form_structure->autofill_count());
  for (size_t idx = 0; idx < form_structure->field_count(); ++idx) {
    ASSERT_EQ(UNKNOWN_TYPE, form_structure->field(idx)->heuristic_type());
  }

  // Prepare and set known server fields.
  const std::vector<FieldType> heuristic_types(form.fields.size(),
                                               UNKNOWN_TYPE);
  const std::vector<FieldType> server_types{NAME_FIRST, NAME_MIDDLE, NAME_LAST};
  test_api(*form_structure).SetFieldTypes(heuristic_types, server_types);
  browser_autofill_manager_->AddSeenFormStructure(std::move(form_structure));

  // Make sure the form can be autofilled.
  for (const FormFieldData& form_field : form.fields) {
    GetAutofillSuggestions(form, form_field);
    ASSERT_TRUE(external_delegate()->on_suggestions_returned_seen());
  }

  // Modify one of the fields in the original form.
  form.fields[0].css_classes += u"a";

  // Expect the form still can be autofilled.
  for (const FormFieldData& form_field : form.fields) {
    GetAutofillSuggestions(form, form_field);
    EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());
  }

  // Modify form action URL. This can happen on in-page navigation if the form
  // doesn't have an actual action (attribute is empty).
  form.action = net::AppendQueryParameter(form.action, "arg", "value");

  // Expect the form still can be autofilled.
  for (const FormFieldData& form_field : form.fields) {
    GetAutofillSuggestions(form, form_field);
    EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());
  }
}

// Test that is_all_server_suggestions is true if there are only
// full_server_card and masked_server_card on file.
TEST_F(BrowserAutofillManagerTest,
       GetCreditCardSuggestions_IsAllServerSuggestionsTrue) {
  // Create server credit cards.
  CreateTestServerCreditCards();

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[1]);

  // Test that we sent the right values to the external delegate.
  ASSERT_TRUE(external_delegate()->is_all_server_suggestions());
}

// Test that is_all_server_suggestions is false if there is at least one
// unique local_card on file with local_card deduped.
TEST_F(BrowserAutofillManagerTest,
       GetCreditCardSuggestions_IsAllServerSuggestionsFalse_LocalCardDeduped) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndEnableFeature(
      features::kAutofillSuggestServerCardInsteadOfLocalCard);
  // Create unique server and local credit cards.
  CreateUniqueTestServerAndLocalCreditCards();

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[1]);

  // Test that we sent the right values to the external delegate.
  ASSERT_FALSE(external_delegate()->is_all_server_suggestions());
}

// Test that is_all_server_suggestions is false if there is at least one
// unique local_card on file with server_card deduped.
TEST_F(BrowserAutofillManagerTest,
       GetCreditCardSuggestions_IsAllServerSuggestionsFalse_ServerCardDeduped) {
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitAndDisableFeature(
      features::kAutofillSuggestServerCardInsteadOfLocalCard);
  // Create unique server and local credit cards.
  CreateUniqueTestServerAndLocalCreditCards();

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  GetAutofillSuggestions(form, form.fields[1]);

  // Test that we sent the right values to the external delegate.
  ASSERT_FALSE(external_delegate()->is_all_server_suggestions());
}

TEST_F(BrowserAutofillManagerTest, GetCreditCardSuggestions_VirtualCard) {
  personal_data().ClearCreditCards();
  CreditCard masked_server_card(CreditCard::RecordType::kMaskedServerCard,
                                /*server_id=*/"a123");
  test::SetCreditCardInfo(&masked_server_card, "Elvis Presley",
                          "4234567890123456",  // Visa
                          "04", "2999", "1");
  masked_server_card.SetNetworkForMaskedCard(kVisaCard);
  masked_server_card.set_guid(MakeGuid(7));
  masked_server_card.set_virtual_card_enrollment_state(
      CreditCard::VirtualCardEnrollmentState::kEnrolled);
  masked_server_card.SetNickname(u"nickname");
  personal_data().AddServerCreditCard(masked_server_card);

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  // Card number field.
  GetAutofillSuggestions(form, form.fields[1]);

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  std::string label = std::string("04/99");
#else
  std::string label = std::string("Expires on 04/99");
#endif

  Suggestion virtual_card_suggestion = Suggestion(
      "Virtual card",
      std::string("nickname  ") +
          test::ObfuscatedCardDigitsAsUTF8(
              "3456", ObfuscationLengthForCreditCardLastFourDigits()),
      label, Suggestion::Icon::kCardVisa,
      autofill::PopupItemId::kVirtualCreditCardEntry);

  external_delegate()->CheckSuggestions(
      form.fields[1].global_id(),
      {virtual_card_suggestion,
       Suggestion(
           std::string("nickname  ") +
               test::ObfuscatedCardDigitsAsUTF8(
                   "3456", ObfuscationLengthForCreditCardLastFourDigits()),
           label, Suggestion::Icon::kCardVisa, PopupItemId::kCreditCardEntry),
       AutofillSuggestionGenerator::CreateSeparator()});

  // Non card number field (cardholder name field).
  GetAutofillSuggestions(form, form.fields[0]);

#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  label = test::ObfuscatedCardDigitsAsUTF8(
      "3456", ObfuscationLengthForCreditCardLastFourDigits());
#else
  label = std::string("nickname  ") +
          test::ObfuscatedCardDigitsAsUTF8(
              "3456", ObfuscationLengthForCreditCardLastFourDigits()) +
          std::string(", expires on 04/99");
#endif

  virtual_card_suggestion =
      Suggestion("Virtual card", std::string("Elvis Presley"), label,
                 Suggestion::Icon::kCardVisa,
                 autofill::PopupItemId::kVirtualCreditCardEntry);

  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {virtual_card_suggestion,
       Suggestion("Elvis Presley", label, Suggestion::Icon::kCardVisa,
                  PopupItemId::kCreditCardEntry),
       AutofillSuggestionGenerator::CreateSeparator()});

  // Incomplete form.
  GetAutofillSuggestions(form, form.fields[0]);

  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {virtual_card_suggestion,
       Suggestion("Elvis Presley", label, Suggestion::Icon::kCardVisa,
                  PopupItemId::kCreditCardEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

TEST_F(BrowserAutofillManagerTest,
       IbanFormProcessed_AutofillOptimizationGuidePresent) {
  FormData form_data = CreateTestIbanFormData();
  FormStructure form_structure{form_data};
  test_api(form_structure).SetFieldTypes({IBAN_VALUE}, {IBAN_VALUE});

  MockAutofillOptimizationGuide autofill_optimization_guide;
  ON_CALL(autofill_client_, GetAutofillOptimizationGuide)
      .WillByDefault(Return(&autofill_optimization_guide));

  // We reset `browser_autofill_manager_` here so that `autofill_client_`
  // initializes `autofill_optimization_guide` in `browser_autofill_manager_`.
  ResetBrowserAutofillManager();
  EXPECT_CALL(autofill_optimization_guide, OnDidParseForm).Times(1);

  test_api(*browser_autofill_manager_)
      .OnFormProcessed(form_data, form_structure);
}

TEST_F(BrowserAutofillManagerTest,
       IbanFormProcessed_AutofillOptimizationGuideNotPresent) {
  FormData form_data = CreateTestIbanFormData();
  FormStructure form_structure{form_data};
  test_api(form_structure).SetFieldTypes({IBAN_VALUE}, {IBAN_VALUE});

  // Test that form processing doesn't crash when we have an IBAN form but no
  // AutofillOptimizationGuide present.
  test_api(*browser_autofill_manager_)
      .OnFormProcessed(form_data, form_structure);
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_LogAutocomplete_NoHappinessMetricsEmitted) {
  FormData form;
  form.name = u"NothingSpecial";
  form.fields = {CreateTestFormField("Something", "something", "",
                                     FormControlType::kInputText)};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  browser_autofill_manager_->DidShowSuggestions(
      std::vector<PopupItemId>({PopupItemId::kAutocompleteEntry}), form,
      form.fields.back());
  // No Autofill logs.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(histograms,
              Not(AnyOf(HasSubstr("Autofill.UserHappiness"),
                        HasSubstr("Autofill.FormEvents.Address"),
                        HasSubstr("Autofill.FormEvents.CreditCard"))));
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_LogAutofillAddressShownMetric) {
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount("Autofill.UserHappiness",
                                     AutofillMetrics::SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount("Autofill.UserHappiness.Address",
                                     AutofillMetrics::SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.UserHappiness", AutofillMetrics::SUGGESTIONS_SHOWN_ONCE, 1);
  histogram_tester.ExpectBucketCount("Autofill.UserHappiness.Address",
                                     AutofillMetrics::SUGGESTIONS_SHOWN_ONCE,
                                     1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // No Autocomplete or credit cards logs.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(histograms,
              Not(AnyOf(HasSubstr("Autocomplete.Events2"),
                        HasSubstr("Autofill.FormEvents.CreditCard"))));
}

TEST_F(BrowserAutofillManagerTest, DidShowSuggestions_LogByType_AddressOnly) {
  // Create a form with name and address fields.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {CreateTestFormField("First Name", "firstname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Last Name", "lastname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Address Line 1", "addr1", "",
                                     FormControlType::kInputText)};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressOnly",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressOnly",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(
          HasSubstr("Autofill.FormEvents.Address.AddressPlusContact"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmail "),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmailPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.ContactOnly"),
          HasSubstr("Autofill.FormEvents.Address.PhoneOnly"),
          HasSubstr("Autofill.FormEvents.Address.Other"))));
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_LogByType_AddressOnlyWithoutName) {
  // Create a form with address fields.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {CreateTestFormField("Address Line 1", "addr1", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Address Line 2", "addr2", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Postal Code", "zipcode", "",
                                     FormControlType::kInputText)};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);

  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressOnly",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressOnly",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(
          HasSubstr("Autofill.FormEvents.Address.AddressPlusContact"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmail "),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmailPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.ContactOnly"),
          HasSubstr("Autofill.FormEvents.Address.PhoneOnly"),
          HasSubstr("Autofill.FormEvents.Address.Other"))));
}

TEST_F(BrowserAutofillManagerTest, DidShowSuggestions_LogByType_ContactOnly) {
  // Create a form with name and contact fields.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {
      CreateTestFormField("First Name", "firstname", "",
                          FormControlType::kInputText),
      CreateTestFormField("Last Name", "lastname", "",
                          FormControlType::kInputText),
      CreateTestFormField("Email", "email", "", FormControlType::kInputEmail)};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.ContactOnly",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.ContactOnly",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(
          HasSubstr("Autofill.FormEvents.Address.AddressPlusContact"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmail "),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmailPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressOnly"),
          HasSubstr("Autofill.FormEvents.Address.PhoneOnly"),
          HasSubstr("Autofill.FormEvents.Address.Other"))));
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_LogByType_ContactOnlyWithoutName) {
  // Create a form with contact fields.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {
      CreateTestFormField("Phone Number", "phonenumber1", "",
                          FormControlType::kInputTelephone, "tel-country-code"),
      CreateTestFormField("Phone Number", "phonenumber2", "",
                          FormControlType::kInputTelephone, "tel-national"),
      CreateTestFormField("Email", "email", "", FormControlType::kInputEmail,
                          "email")};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.ContactOnly",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.ContactOnly",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(
          HasSubstr("Autofill.FormEvents.Address.AddressPlusContact"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmail "),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmailPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressOnly"),
          HasSubstr("Autofill.FormEvents.Address.PhoneOnly"),
          HasSubstr("Autofill.FormEvents.Address.Other"))));
}

TEST_F(BrowserAutofillManagerTest, DidShowSuggestions_LogByType_PhoneOnly) {
  // Create a form with phone field.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {
      CreateTestFormField("Phone Number", "phonenumber", "",
                          FormControlType::kInputTelephone, "tel-country-code"),
      CreateTestFormField("Phone Number", "phonenumber", "",
                          FormControlType::kInputTelephone, "tel-area-code"),
      CreateTestFormField("Phone Number", "phonenumber", "",
                          FormControlType::kInputTelephone, "tel-local")};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.PhoneOnly",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.PhoneOnly",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmailPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmail"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusContact"),
          HasSubstr("Autofill.FormEvents.Address.AddressOnly"),
          HasSubstr("Autofill.FormEvents.Address.ContactOnly"),
          HasSubstr("Autofill.FormEvents.Address.Other"))));
}

TEST_F(BrowserAutofillManagerTest, DidShowSuggestions_LogByType_Other) {
  // Create a form with name fields.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {CreateTestFormField("First Name", "firstname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Middle Name", "middlename", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Last Name", "lastname", "",
                                     FormControlType::kInputText)};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.Other",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.Other",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(
          HasSubstr("Autofill.FormEvents.Address.AddressPlusContact"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmail "),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmailPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressOnly"),
          HasSubstr("Autofill.FormEvents.Address.PhoneOnly"),
          HasSubstr("Autofill.FormEvents.Address.ContactOnly"))));
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_LogByType_AddressPlusEmail) {
  // Create a form with name, address, and email fields.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {
      CreateTestFormField("First Name", "firstname", "",
                          FormControlType::kInputText),
      CreateTestFormField("Last Name", "lastname", "",
                          FormControlType::kInputText),
      CreateTestFormField("Address Line 1", "addr1", "",
                          FormControlType::kInputText),
      CreateTestFormField("Email", "email", "", FormControlType::kInputEmail)};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusEmail",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusEmail",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmailPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressOnly"),
          HasSubstr("Autofill.FormEvents.Address.ContactOnly"),
          HasSubstr("Autofill.FormEvents.Address.PhoneOnly"),
          HasSubstr("Autofill.FormEvents.Address.Other"))));
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_LogByType_AddressPlusEmailWithoutName) {
  // Create a form with address and email fields.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {
      CreateTestFormField("Address Line 1", "addr1", "",
                          FormControlType::kInputText),
      CreateTestFormField("Address Line 2", "addr2", "",
                          FormControlType::kInputText),
      CreateTestFormField("Email", "email", "", FormControlType::kInputEmail)};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusEmail",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusEmail",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmailPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressOnly"),
          HasSubstr("Autofill.FormEvents.Address.ContactOnly"),
          HasSubstr("Autofill.FormEvents.Address.PhoneOnly"),
          HasSubstr("Autofill.FormEvents.Address.Other"))));
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_LogByType_AddressPlusPhone) {
  // Create a form with name fields.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {CreateTestFormField("First Name", "firstname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Last Name", "lastname", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Address Line 1", "addr1", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Phone Number", "phonenumber", "",
                                     FormControlType::kInputTelephone)};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusPhone",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusPhone",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmailPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmail "),
          HasSubstr("Autofill.FormEvents.Address.AddressOnly"),
          HasSubstr("Autofill.FormEvents.Address.ContactOnly"),
          HasSubstr("Autofill.FormEvents.Address.PhoneOnly"),
          HasSubstr("Autofill.FormEvents.Address.Other"))));
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_LogByType_AddressPlusPhoneWithoutName) {
  // Create a form with name, address, and phone fields.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {CreateTestFormField("Address Line 1", "addr1", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Address Line 2", "addr2", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Phone Number", "phonenumber", "",
                                     FormControlType::kInputTelephone)};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusPhone",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusPhone",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmailPlusPhone"),
          HasSubstr("Autofill.FormEvents.Address.AddressPlusEmail "),
          HasSubstr("Autofill.FormEvents.Address.AddressOnly"),
          HasSubstr("Autofill.FormEvents.Address.ContactOnly"),
          HasSubstr("Autofill.FormEvents.Address.PhoneOnly"),
          HasSubstr("Autofill.FormEvents.Address.Other"))));
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_LogByType_AddressPlusEmailPlusPhone) {
  // Create a form with name, address, phone, and email fields.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {
      CreateTestFormField("First Name", "firstname", "",
                          FormControlType::kInputText),
      CreateTestFormField("Last Name", "lastname", "",
                          FormControlType::kInputText),
      CreateTestFormField("Address Line 1", "addr1", "",
                          FormControlType::kInputText),
      CreateTestFormField("Phone Number", "phonenumber", "",
                          FormControlType::kInputTelephone),
      CreateTestFormField("Email", "email", "", FormControlType::kInputEmail)};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusEmailPlusPhone",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusEmailPlusPhone",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(HasSubstr("Autofill.FormEvents.Address.AddressPlusPhone"),
                HasSubstr("Autofill.FormEvents.Address.AddressPlusEmail "),
                HasSubstr("Autofill.FormEvents.Address.AddressOnly"),
                HasSubstr("Autofill.FormEvents.Address.ContactOnly"),
                HasSubstr("Autofill.FormEvents.Address.PhoneOnly"),
                HasSubstr("Autofill.FormEvents.Address.Other"))));
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_LogByType_AddressPlusEmailPlusPhoneWithoutName) {
  // Create a form with address, phone, and email fields.
  FormData form;
  form.name = u"MyForm";
  form.button_titles = {std::make_pair(
      u"Submit", mojom::ButtonTitleType::BUTTON_ELEMENT_SUBMIT_TYPE)};
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.main_frame_origin =
      url::Origin::Create(GURL("https://myform_root.com/form.html"));
  form.submission_event = SubmissionIndicatorEvent::SAME_DOCUMENT_NAVIGATION;
  form.fields = {
      CreateTestFormField("Address Line 1", "addr1", "",
                          FormControlType::kInputText),
      CreateTestFormField("Phone Number", "phonenumber", "",
                          FormControlType::kInputTelephone),
      CreateTestFormField("Email", "email", "", FormControlType::kInputEmail)};
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusEmailPlusPhone",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusEmailPlusPhone",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.Address.AddressPlusContact",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // Logging is not done for other types of address forms.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(
      histograms,
      Not(AnyOf(HasSubstr("Autofill.FormEvents.Address.AddressPlusPhone"),
                HasSubstr("Autofill.FormEvents.Address.AddressPlusEmail "),
                HasSubstr("Autofill.FormEvents.Address.AddressOnly"),
                HasSubstr("Autofill.FormEvents.Address.ContactOnly"),
                HasSubstr("Autofill.FormEvents.Address.PhoneOnly"),
                HasSubstr("Autofill.FormEvents.Address.Other"))));
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_LogAutofillCreditCardShownMetric) {
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  base::HistogramTester histogram_tester;
  DidShowAutofillSuggestions(form);
  histogram_tester.ExpectBucketCount("Autofill.UserHappiness",
                                     AutofillMetrics::SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount("Autofill.UserHappiness.CreditCard",
                                     AutofillMetrics::SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.UserHappiness", AutofillMetrics::SUGGESTIONS_SHOWN_ONCE, 1);
  histogram_tester.ExpectBucketCount("Autofill.UserHappiness.CreditCard",
                                     AutofillMetrics::SUGGESTIONS_SHOWN_ONCE,
                                     1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN, 1);
  histogram_tester.ExpectBucketCount(
      "Autofill.FormEvents.CreditCard",
      autofill_metrics::FORM_EVENT_SUGGESTIONS_SHOWN_ONCE, 1);

  // No Autocomplete or address logs.
  const std::string histograms = histogram_tester.GetAllHistogramsRecorded();
  EXPECT_THAT(histograms, Not(AnyOf(HasSubstr("Autocomplete.Events2"),
                                    HasSubstr("Autofill.FormEvents.Address"))));
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_CreditCard_PreflightFetchingCall) {
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  EXPECT_CALL(cc_access_manager(), PrepareToFetchCreditCard)
      .Times(IsCreditCardFidoAuthenticationEnabled() ? 1 : 0);
  browser_autofill_manager_->DidShowSuggestions(
      std::vector<PopupItemId>({PopupItemId::kCreditCardEntry}), form,
      form.fields[0]);
}

TEST_F(BrowserAutofillManagerTest,
       DidShowSuggestions_AddessSuggestion_NoPreflightFetchingCall) {
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});

  EXPECT_CALL(cc_access_manager(), PrepareToFetchCreditCard).Times(0);
  browser_autofill_manager_->DidShowSuggestions(
      std::vector<PopupItemId>({PopupItemId::kAddressEntry}), form,
      form.fields[0]);
}

TEST_F(BrowserAutofillManagerTest, PageLanguageGetsCorrectlySet) {
  FormData form = CreateTestAddressFormData();

  browser_autofill_manager_->OnFormsSeen({form}, {});
  FormStructure* parsed_form =
      browser_autofill_manager_->FindCachedFormById(form.global_id());

  ASSERT_TRUE(parsed_form);
  ASSERT_EQ(LanguageCode(), parsed_form->current_page_language());

  autofill_client_.GetLanguageState()->SetCurrentLanguage("zh");

  browser_autofill_manager_->OnFormsSeen({form}, {});
  parsed_form = browser_autofill_manager_->FindCachedFormById(form.global_id());

  ASSERT_EQ(LanguageCode("zh"), parsed_form->current_page_language());
}

// Test language detection on frames depending on whether the frame is active or
// not.
class BrowserAutofillManagerTestPageLanguageDetection
    : public BrowserAutofillManagerTest,
      public testing::WithParamInterface<bool> {
 public:
  BrowserAutofillManagerTestPageLanguageDetection() {
    scoped_features_.InitAndEnableFeature(
        features::kAutofillPageLanguageDetection);
  }

  bool is_in_active_frame() const { return GetParam(); }

 private:
  base::test::ScopedFeatureList scoped_features_;
};

TEST_P(BrowserAutofillManagerTestPageLanguageDetection, GetsCorrectlyDetected) {
  FormData form = CreateTestAddressFormData();

  browser_autofill_manager_->OnFormsSeen({form}, {});
  FormStructure* parsed_form =
      browser_autofill_manager_->FindCachedFormById(form.global_id());

  ASSERT_TRUE(parsed_form);
  ASSERT_EQ(LanguageCode(), parsed_form->current_page_language());

  translate::LanguageDetectionDetails language_detection_details;
  language_detection_details.adopted_language = "hu";
  autofill_driver_->SetIsInActiveFrame(is_in_active_frame());
  browser_autofill_manager_->OnLanguageDetermined(language_detection_details);

  autofill_client_.GetLanguageState()->SetCurrentLanguage("hu");

  parsed_form = browser_autofill_manager_->FindCachedFormById(form.global_id());

  // Language detection is used only for active frames.
  auto expected_language_code =
      is_in_active_frame() ? LanguageCode("hu") : LanguageCode();

  ASSERT_EQ(*expected_language_code, *parsed_form->current_page_language());
}

INSTANTIATE_TEST_SUITE_P(All,
                         BrowserAutofillManagerTestPageLanguageDetection,
                         testing::Bool());

// BrowserAutofillManagerTest with different browser profile types.
class BrowserAutofillManagerProfileMetricsTest
    : public BrowserAutofillManagerTest,
      public testing::WithParamInterface<profile_metrics::BrowserProfileType> {
 public:
  BrowserAutofillManagerProfileMetricsTest() : profile_type_(GetParam()) {
    EXPECT_CALL(autofill_client_, GetProfileType())
        .WillRepeatedly(Return(profile_type_));
  }

  const profile_metrics::BrowserProfileType profile_type_;
};

// Tests if submitting a form in different browser profile types records correct
// |Autofill.FormSubmission.PerProfileType| metric.
TEST_P(BrowserAutofillManagerProfileMetricsTest,
       FormSubmissionPerProfileTypeMetrics) {
  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  FormsSeen({form});
  GetAutofillSuggestions(form, form.fields[0]);

  base::HistogramTester histogram_tester;

  FormSubmitted(form);
  histogram_tester.ExpectBucketCount("Autofill.FormSubmission.PerProfileType",
                                     profile_type_, 1);
  histogram_tester.ExpectTotalCount("Autofill.FormSubmission.PerProfileType",
                                    1);
}

INSTANTIATE_TEST_SUITE_P(
    All,
    BrowserAutofillManagerProfileMetricsTest,
    testing::ValuesIn({profile_metrics::BrowserProfileType::kRegular,
                       profile_metrics::BrowserProfileType::kIncognito,
                       profile_metrics::BrowserProfileType::kGuest}));

// Tests that autocomplete-related metrics are emitted correctly on form
// submission.
TEST_F(BrowserAutofillManagerTest, AutocompleteMetrics) {
  // `kAutocompleteValues` corresponds to empty, valid, garbage and off.
  constexpr const char* kAutocompleteValues[]{"", "name", "asdf", "off"};
  // The 4 possible combinations of heuristic and server type status:
  // - Neither a fillable heuristic type nor a fillable server type.
  // - Only a fillable server type.
  // - Only a fillable heuristic type.
  // - Both a fillable heuristic type and a fillable server type.
  // NO_SERVER_DATA and UNKNOWN_TYPE are both unfillable types, but
  // NO_SERVER_DATA is ignored in the PredictionCollisionType metric.
  constexpr FieldType kTypeClasses[][2]{{UNKNOWN_TYPE, NO_SERVER_DATA},
                                        {UNKNOWN_TYPE, EMAIL_ADDRESS},
                                        {ADDRESS_HOME_COUNTRY, UNKNOWN_TYPE},
                                        {ADDRESS_HOME_COUNTRY, EMAIL_ADDRESS}};

  // Create a form with one field per kAutofillValue x kTypeClass combination.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  std::vector<FieldType> heuristic_types, server_types;
  for (const char* autocomplete : kAutocompleteValues) {
    for (const auto& types : kTypeClasses) {
      form.fields.push_back(CreateTestFormField(
          "", "", "", FormControlType::kInputText, autocomplete));
      heuristic_types.push_back(types[0]);
      server_types.push_back(types[1]);
    }
  }
  // Override the types and simulate seeing the form on page load.
  auto form_structure = std::make_unique<FormStructure>(form);
  form_structure->DetermineHeuristicTypes(GeoIpCountryCode(""), nullptr,
                                          nullptr);
  test_api(*form_structure).SetFieldTypes(heuristic_types, server_types);
  browser_autofill_manager_->AddSeenFormStructure(std::move(form_structure));

  // Submit the form and verify that all metrics are collected correctly.
  base::HistogramTester histogram_tester;
  FormSubmitted(form);

  // Expect one entry for each possible PredictionStateAutocompleteStatePair.
  // Fields without type predictions and autocomplete attributes are ignored.
  histogram_tester.ExpectTotalCount(
      "Autofill.Autocomplete.PredictionCollisionState", form.fields.size() - 1);
  for (int i = 0; i < 15; i++) {
    histogram_tester.ExpectBucketCount(
        "Autofill.Autocomplete.PredictionCollisionState", i, 1);
  }

  // A separate PredictionCollisionType metric exists for every prediction-type
  // autocomplete status combination. Above, we created four fields per
  // `kAutocompleteValues` - so we expect 4 samples in every bucket.
  // The exception is the .Server metric, which is not emitted for
  // NO_SERVER_DATA and hence expects only three samples.
  const std::string kTypeHistogram =
      "Autofill.Autocomplete.PredictionCollisionType2.";
  for (const char* suffix : {"Garbage", "None", "Off", "Valid"}) {
    histogram_tester.ExpectTotalCount(kTypeHistogram + "Heuristics." + suffix,
                                      4);
    histogram_tester.ExpectTotalCount(kTypeHistogram + "Server." + suffix, 3);
    histogram_tester.ExpectTotalCount(
        kTypeHistogram + "ServerOrHeuristics." + suffix, 4);
  }
}

struct ContextMenuImpressionTestCase {
  // Autocomplete attribute value.
  const char* autocomplete_attribute_value;
  // Heuristic type for the field in the test case.
  FieldType heuristic_type;
  // Server type for the field in the test case.
  FieldType server_type;
  // Expected autocomplete state that would be logged in the metrics.
  AutofillMetrics::AutocompleteState expected_autocomplete_state;
  // Expected autofill type that would be logged in the metrics.
  FieldType expected_autofill_type;
};

class BrowserAutofillManagerContextMenuImpressionsTest
    : public testing::WithParamInterface<ContextMenuImpressionTestCase>,
      public BrowserAutofillManagerTest {};

INSTANTIATE_TEST_SUITE_P(
    BrowserAutofillManagerContextMenuTests,
    BrowserAutofillManagerContextMenuImpressionsTest,
    testing::Values(
        // Empty Autocomplete attribute
        ContextMenuImpressionTestCase{"", UNKNOWN_TYPE, NO_SERVER_DATA,
                                      AutofillMetrics::AutocompleteState::kNone,
                                      UNKNOWN_TYPE},
        // Valid Autocomplete attribute
        ContextMenuImpressionTestCase{
            "name", UNKNOWN_TYPE, EMAIL_ADDRESS,
            AutofillMetrics::AutocompleteState::kValid, NAME_FULL},
        // Garbage Autocomplete attribute
        ContextMenuImpressionTestCase{
            "asdf", ADDRESS_HOME_COUNTRY, UNKNOWN_TYPE,
            AutofillMetrics::AutocompleteState::kGarbage, UNKNOWN_TYPE},
        // Off Autocomplete attribute
        ContextMenuImpressionTestCase{
            "off", ADDRESS_HOME_COUNTRY, EMAIL_ADDRESS,
            AutofillMetrics::AutocompleteState::kOff, EMAIL_ADDRESS}));

// Tests that metrics are emitted correctly on form submission for the fields
// from where the context menu was triggered.
TEST_P(BrowserAutofillManagerContextMenuImpressionsTest,
       ContextMenuImpressionMetrics) {
  auto test_case = GetParam();

  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {CreateTestFormField("", "", "", FormControlType::kInputText,
                                     test_case.autocomplete_attribute_value)};

  // Override the types and simulate seeing the form on page load.
  auto form_structure = std::make_unique<FormStructure>(form);
  test_api(*form_structure)
      .SetFieldTypes({test_case.heuristic_type}, {test_case.server_type});
  browser_autofill_manager_->AddSeenFormStructure(std::move(form_structure));

  // Simulate context menu trigger for all the fields.
  browser_autofill_manager_->OnContextMenuShownInField(
      form.global_id(), form.fields[0].global_id());

  // Submit the form and verify that all metrics are collected correctly.
  base::HistogramTester histogram_tester;
  FormSubmitted(form);

  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          "Autofill.FieldContextMenuImpressions.ByAutocomplete"),
      BucketsAre(base::Bucket(test_case.expected_autocomplete_state, 1)));

  EXPECT_THAT(histogram_tester.GetAllSamples(
                  "Autofill.FieldContextMenuImpressions.ByAutofillType"),
              BucketsAre(base::Bucket(test_case.expected_autofill_type, 1)));

  EXPECT_THAT(histogram_tester.GetAllSamples(
                  "Autofill.FormContextMenuImpressions.ByNumberOfFields"),
              BucketsAre(base::Bucket(1, 1)));
}

// Test that if a form is mixed content we show a warning instead of any
// suggestions.
TEST_F(BrowserAutofillManagerTest, GetSuggestions_MixedForm) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("http://myform.com/submit.html");
  form.fields = {CreateTestFormField("Name on Card", "nameoncard", "",
                                     FormControlType::kInputText)};

  GetAutofillSuggestions(form, form.fields[0]);

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields.back().global_id(),
      {Suggestion(l10n_util::GetStringUTF8(IDS_AUTOFILL_WARNING_MIXED_FORM), "",
                  Suggestion::Icon::kNoIcon, PopupItemId::kMixedFormMessage)});
}

// Test that if a form is mixed content we do not show a warning if the opt out
// policy is set.
TEST_F(BrowserAutofillManagerTest, GetSuggestions_MixedFormOptOutPolicy) {
  // Set pref to disabled.
  autofill_client_.GetPrefs()->SetBoolean(::prefs::kMixedFormsWarningsEnabled,
                                          false);

  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("http://myform.com/submit.html");
  form.fields = {CreateTestFormField("Name on Card", "nameoncard", "",
                                     FormControlType::kInputText)};
  GetAutofillSuggestions(form, form.fields[0]);

  // Check there is no warning.
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

// Test that we dismiss the mixed form warning if user starts typing.
TEST_F(BrowserAutofillManagerTest, GetSuggestions_MixedFormUserTyped) {
  // Set up our form data.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("http://myform.com/submit.html");
  form.fields = {CreateTestFormField("Name on Card", "nameoncard", "",
                                     FormControlType::kInputText)};

  GetAutofillSuggestions(form, form.fields[0]);

  // Test that we sent the right values to the external delegate.
  external_delegate()->CheckSuggestions(
      form.fields.back().global_id(),
      {Suggestion(l10n_util::GetStringUTF8(IDS_AUTOFILL_WARNING_MIXED_FORM), "",
                  Suggestion::Icon::kNoIcon, PopupItemId::kMixedFormMessage)});

  // Pretend user started typing and make sure we no longer set suggestions.
  form.fields[0].value = u"Michael";
  form.fields[0].properties_mask |= kUserTyped;
  GetAutofillSuggestions(form, form.fields[0]);
  external_delegate()->CheckNoSuggestions(form.fields[0].global_id());
}

// Test that we don't treat javascript scheme target URLs as mixed forms.
// Regression test for crbug.com/1135173
TEST_F(BrowserAutofillManagerTest, GetSuggestions_JavascriptUrlTarget) {
  // Set up our form data, using a javascript scheme target URL.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("javascript:alert('hello');");
  form.fields = {CreateTestFormField("Name on Card", "nameoncard", "",
                                     FormControlType::kInputText)};
  GetAutofillSuggestions(form, form.fields[0]);

  // Check there is no warning.
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

// Test that we don't treat about:blank target URLs as mixed forms.
TEST_F(BrowserAutofillManagerTest, GetSuggestions_AboutBlankTarget) {
  // Set up our form data, using a javascript scheme target URL.
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("about:blank");
  form.fields = {CreateTestFormField("Name on Card", "nameoncard", "",
                                     FormControlType::kInputText)};
  GetAutofillSuggestions(form, form.fields[0]);

  // Check there is no warning.
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

// Tests that both Autofill popup and TTF are hidden on renderer event.
TEST_F(BrowserAutofillManagerTest, HideAutofillPopupAndOtherPopups) {
  EXPECT_CALL(autofill_client_,
              HideAutofillPopup(PopupHidingReason::kRendererEvent));
  EXPECT_CALL(touch_to_fill_delegate(), HideTouchToFill);
  EXPECT_CALL(fast_checkout_delegate(),
              HideFastCheckout(/*allow_further_runs=*/false));
  browser_autofill_manager_->OnHidePopup();
}

// Tests that only Autofill popup is hidden on editing end, but not TTF or FC.
TEST_F(BrowserAutofillManagerTest, OnDidEndTextFieldEditing) {
  EXPECT_CALL(autofill_client_,
              HideAutofillPopup(PopupHidingReason::kEndEditing));
  EXPECT_CALL(touch_to_fill_delegate(), HideTouchToFill).Times(0);
  EXPECT_CALL(fast_checkout_delegate(),
              HideFastCheckout(/*allow_further_runs=*/false))
      .Times(0);
  browser_autofill_manager_->OnDidEndTextFieldEditing();
}

// Tests that Autofill suggestions are not shown if TTF is eligible and shown.
TEST_F(BrowserAutofillManagerTest, AutofillSuggestionsOrTouchToFill) {
  FormData form;
  CreateTestCreditCardFormData(&form, /*is_https=*/true,
                               /*use_month_type=*/false);
  FormsSeen({form});
  const FormFieldData& field = form.fields[1];

  // Not a form element click, Autofill suggestions shown.
  EXPECT_CALL(touch_to_fill_delegate(), TryToShowTouchToFill).Times(0);
  TryToShowTouchToFill(form, field, /*form_element_was_clicked=*/false);
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());

  // TTF not available, Autofill suggestions shown.
  EXPECT_CALL(touch_to_fill_delegate(), TryToShowTouchToFill)
      .WillOnce(Return(false));
  TryToShowTouchToFill(form, field, /*form_element_was_clicked=*/true);
  EXPECT_TRUE(external_delegate()->on_suggestions_returned_seen());

  // A form element click and TTF available, Autofill suggestions not shown.
  EXPECT_CALL(touch_to_fill_delegate(), TryToShowTouchToFill)
      .WillOnce(Return(true));
  TryToShowTouchToFill(form, field, /*form_element_was_clicked=*/true);
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

// Tests that neither Autofill suggestions nor TTF is triggered if TTF is
// already shown.
TEST_F(BrowserAutofillManagerTest, ShowNothingIfTouchToFillAlreadyShown) {
  FormData form;
  CreateTestCreditCardFormData(&form, /*is_https=*/true,
                               /*use_month_type=*/false);
  FormsSeen({form});
  const FormFieldData& field = form.fields[1];

  EXPECT_CALL(touch_to_fill_delegate(), IsShowingTouchToFill)
      .WillOnce(Return(true));
  EXPECT_CALL(touch_to_fill_delegate(), TryToShowTouchToFill).Times(0);
  TryToShowTouchToFill(form, field, /*form_element_was_clicked=*/true);
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
}

// Test that 'Scan New Card' suggestion is shown based on whether autofill
// credit card is enabled or disabled.
TEST_F(BrowserAutofillManagerTest, ScanCreditCardBasedOnAutofillPreference) {
  ON_CALL(autofill_client_, HasCreditCardScanFeature())
      .WillByDefault(Return(true));

  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  const FormFieldData& card_number_field = form.fields[1];
  ASSERT_EQ(card_number_field.name, u"cardnumber");

  // Test case where autofill is enabled.
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              true);
  EXPECT_TRUE(browser_autofill_manager_->ShouldShowScanCreditCard(
      form, card_number_field));

  // Test case where autofill is disabled.
  browser_autofill_manager_->SetAutofillPaymentMethodsEnabled(autofill_client_,
                                                              false);
  EXPECT_FALSE(browser_autofill_manager_->ShouldShowScanCreditCard(
      form, card_number_field));
}

// Test that 'Scan New Card' suggestion is shown based on whether platform
// supports card scanning.
TEST_F(BrowserAutofillManagerTest, ScanCreditCardBasedOnPlatformSupport) {
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  const FormFieldData& card_number_field = form.fields[1];
  ASSERT_EQ(card_number_field.name, u"cardnumber");

  // Test case where device and platform support scanning credit cards.
  ON_CALL(autofill_client_, HasCreditCardScanFeature())
      .WillByDefault(Return(true));
  EXPECT_TRUE(browser_autofill_manager_->ShouldShowScanCreditCard(
      form, card_number_field));

  // Test case where device and platform do not support scanning credit cards.
  ON_CALL(autofill_client_, HasCreditCardScanFeature())
      .WillByDefault(Return(false));
  EXPECT_FALSE(browser_autofill_manager_->ShouldShowScanCreditCard(
      form, card_number_field));
}

// Test that 'Scan New Card' suggestion is shown based on whether form field
// chosen is a credit card number field.
TEST_F(BrowserAutofillManagerTest, ScanCreditCardBasedOnCreditCardNumberField) {
  ON_CALL(autofill_client_, HasCreditCardScanFeature())
      .WillByDefault(Return(true));

  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  // Test case for credit-card-number field.
  const FormFieldData& card_number_field = form.fields[1];
  ASSERT_EQ(card_number_field.name, u"cardnumber");
  EXPECT_TRUE(browser_autofill_manager_->ShouldShowScanCreditCard(
      form, card_number_field));

  // Test case for non-credit-card-number field.
  const FormFieldData& cvc_field = form.fields[4];
  ASSERT_EQ(cvc_field.name, u"cvc");
  EXPECT_FALSE(
      browser_autofill_manager_->ShouldShowScanCreditCard(form, cvc_field));
}

// Test that 'Scan New Card' suggestion is shown based on whether the form is
// secure.
TEST_F(BrowserAutofillManagerTest, ScanCreditCardBasedOnIsFormSecure) {
  ON_CALL(autofill_client_, HasCreditCardScanFeature())
      .WillByDefault(Return(true));

  // Test case for HTTP form.
  FormData form_http = CreateTestCreditCardFormData(/*is_https=*/false,
                                                    /*use_month_type=*/false);
  FormsSeen({form_http});

  const FormFieldData& card_number_field_http = form_http.fields[1];
  ASSERT_EQ(card_number_field_http.name, u"cardnumber");
  EXPECT_FALSE(browser_autofill_manager_->ShouldShowScanCreditCard(
      form_http, card_number_field_http));

  // Test case for HTTPS form.
  FormData form_https =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form_https});

  const FormFieldData& card_number_field_https = form_https.fields[1];
  ASSERT_EQ(card_number_field_https.name, u"cardnumber");
  EXPECT_TRUE(browser_autofill_manager_->ShouldShowScanCreditCard(
      form_https, card_number_field_https));
}

// Test that fields will be assigned with the source profile that was used for
// autofill.
TEST_F(BrowserAutofillManagerTest, TrackFillingOrigin) {
  // Set up our form data.
  FormData form = CreateTestPersonalInformationFormData();

  FormsSeen({form});

  AutofillProfile profile = test::GetFullProfile();
  personal_data().AddProfile(profile);
  FillAutofillFormData(form, form.fields[0], profile.guid());

  FormStructure* form_structure =
      browser_autofill_manager_->FindCachedFormById(form.global_id());
  ASSERT_TRUE(form_structure);
  for (const auto& autofill_field_ptr : form_structure->fields()) {
    EXPECT_THAT(autofill_field_ptr->autofill_source_profile_guid(),
                testing::Optional(profile.guid()));
  }
}

// Test that filling with multiple autofill profiles will set different source
// profiles for fields.
TEST_F(BrowserAutofillManagerTest,
       TrackFillingOriginWithUsingMultipleProfiles) {
  // Set up our form data.
  FormData form = CreateTestPersonalInformationFormData();

  FormsSeen({form});

  // Fill the form with a profile without email
  AutofillProfile profile1 = test::GetFullProfile();
  profile1.ClearFields({EMAIL_ADDRESS});
  personal_data().AddProfile(profile1);
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], profile1.guid());

  // Check that the email field has no filling source.
  FormStructure* form_structure =
      browser_autofill_manager_->FindCachedFormById(form.global_id());
  ASSERT_EQ(form.fields[3].label, u"Email");
  EXPECT_EQ(form_structure->field(3)->autofill_source_profile_guid(),
            std::nullopt);

  // Then fill the email field using the second profile
  AutofillProfile profile2 = test::GetFullProfile();
  personal_data().AddProfile(profile2);
  FormData later_response_data = FillAutofillFormDataAndGetResults(
      response_data, form.fields[3], profile2.guid());

  // Check that the first three fields have the first profile as filling source
  // and the last field has the second profile.
  form_structure =
      browser_autofill_manager_->FindCachedFormById(form.global_id());
  ASSERT_TRUE(form_structure);
  EXPECT_THAT(form_structure->field(0)->autofill_source_profile_guid(),
              testing::Optional(profile1.guid()));
  EXPECT_THAT(form_structure->field(1)->autofill_source_profile_guid(),
              testing::Optional(profile1.guid()));
  EXPECT_THAT(form_structure->field(2)->autofill_source_profile_guid(),
              testing::Optional(profile1.guid()));
  EXPECT_THAT(form_structure->field(3)->autofill_source_profile_guid(),
              testing::Optional(profile2.guid()));
}

// Test that an autofilled and edited field will be assigned with the autofill
// profile.
TEST_F(BrowserAutofillManagerTest, TrackFillingOriginOnEditedField) {
  // Set up our form data.
  FormData form = CreateTestPersonalInformationFormData();

  FormsSeen({form});

  AutofillProfile profile = test::GetFullProfile();
  personal_data().AddProfile(profile);
  FormData response_data =
      FillAutofillFormDataAndGetResults(form, form.fields[0], profile.guid());

  // Simulate editing the first field.
  response_data.fields[0].value = u"Michael";
  browser_autofill_manager_->OnTextFieldDidChange(
      response_data, response_data.fields[0], gfx::RectF(),
      base::TimeTicks::Now());

  FormStructure* form_structure =
      browser_autofill_manager_->FindCachedFormById(form.global_id());
  ASSERT_TRUE(form_structure);
  AutofillField* edited_field = form_structure->field(0);
  ASSERT_FALSE(edited_field->is_autofilled);
  ASSERT_TRUE(edited_field->previously_autofilled());
  EXPECT_THAT(edited_field->autofill_source_profile_guid(),
              testing::Optional(profile.guid()));
}

// Test that only autofilled fields will be assigned with the autofill profile.
TEST_F(BrowserAutofillManagerTest, TrackFillingOriginWorksOnlyOnFilledField) {
  // Set up our form data.
  FormData form = CreateTestPersonalInformationFormData();

  FormsSeen({form});

  // Fill the form with a profile without email field.
  AutofillProfile profile = test::GetFullProfile();
  profile.ClearFields({EMAIL_ADDRESS});
  personal_data().AddProfile(profile);
  FillAutofillFormData(form, form.fields[0], profile.guid());

  FormStructure* form_structure =
      browser_autofill_manager_->FindCachedFormById(form.global_id());
  ASSERT_TRUE(form_structure);
  // Check that the email field has no filling source.
  EXPECT_EQ(form_structure->field(3)->autofill_source_profile_guid(),
            std::nullopt);
}

// Ensure that the experimental plus_addresses feature is not shown by default.
// This is true even if the PlusAddressService had existing data for the current
// domain.
TEST_F(BrowserAutofillManagerTest, NoPlusAddressSuggestionsByDefault) {
  plus_addresses::PlusAddressService plus_address_service;
  plus_address_service.SavePlusAddress(
      autofill_client_.GetLastCommittedPrimaryMainFrameOrigin(),
      "plus+plus@plus.plus");
  ON_CALL(autofill_client_, GetPlusAddressService)
      .WillByDefault(Return(&plus_address_service));
  // Set up our form data. Notably, the first field is an email address.
  FormData form = test::GetFormData(
      {.fields = {{.role = EMAIL_ADDRESS, .autocomplete_attribute = "email"}}});
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  FormsSeen({form});

  // Check that suggestions are made for the field that has the autocomplete
  // attribute, and ensure that they are exactly the expected set, with no plus
  // address data.
  GetAutofillSuggestions(form, form.fields[0]);
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion("buddy@gmail.com", "", Suggestion::Icon::kNoIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("theking@gmail.com", "", Suggestion::Icon::kNoIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Tests that compose suggestions are not queried if Autofill has suggestions
// itself.
TEST_F(BrowserAutofillManagerTest, NoComposeSuggestionsByDefault) {
  MockAutofillComposeDelegate compose_delegate;
  ON_CALL(autofill_client_, GetComposeDelegate)
      .WillByDefault(Return(&compose_delegate));

  FormData form = CreateTestAddressFormData();
  form.fields[3].form_control_type = FormControlType::kTextArea;
  FormsSeen({form});

  // The third field is meant to correspond to address line 1. For that (unlike
  // for first and last name), parsing also derives that type if it is a
  // textarea.
  EXPECT_CALL(compose_delegate, ShouldOfferComposePopup).Times(0);
  GetAutofillSuggestions(form, form.fields[3]);
  external_delegate()->CheckSuggestions(
      form.fields[3].global_id(),
      {Suggestion("123 Apple St., unit 6", "123 Apple St.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("3734 Elvis Presley Blvd., Apt. 10",
                  "3734 Elvis Presley Blvd.", kAddressEntryIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// Tests that Compose suggestions are queried if the trigger source indicates
// that the focus change happened without click/tap interaction. It also
// verifies that neither Autofill nor single form fill suggestions are queried.
TEST_F(BrowserAutofillManagerTest, ComposeSuggestionsOnFocusWithoutClick) {
  MockAutofillComposeDelegate compose_delegate;
  ON_CALL(autofill_client_, GetComposeDelegate)
      .WillByDefault(Return(&compose_delegate));

  FormData form = CreateTestAddressFormData();
  form.fields[3].form_control_type = FormControlType::kTextArea;
  FormsSeen({form});

  EXPECT_CALL(single_field_form_fill_router(), OnGetSingleFieldSuggestions)
      .Times(0);
  EXPECT_CALL(compose_delegate,
              ShouldOfferComposePopup(Property(&FormFieldData::global_id,
                                               Eq(form.fields[3].global_id()))))
      .WillOnce(Return(true));
  GetAutofillSuggestions(
      form, form.fields[3],
      AutofillSuggestionTriggerSource::kTextareaFocusedWithoutClick);
  external_delegate()->CheckSuggestionCount(form.fields[3].global_id(), 1);
}

// Tests that compose suggestions are queried for textareas if Autofill does not
// have suggestions of its own and the OS not Android, iOS or ChromeOS.
TEST_F(BrowserAutofillManagerTest, ComposeSuggestionsAreQueriedForTextareas) {
  MockAutofillComposeDelegate compose_delegate;
  ON_CALL(autofill_client_, GetComposeDelegate)
      .WillByDefault(Return(&compose_delegate));

  FormData form = test::GetFormData(
      {.fields = {{.form_control_type = FormControlType::kTextArea}}});
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  FormsSeen({form});

  EXPECT_CALL(compose_delegate,
              ShouldOfferComposePopup(Property(
                  &FormFieldData::global_id, Eq(form.fields[0].global_id()))));
  GetAutofillSuggestions(form, form.fields[0]);
}

// Test param indicates if there is an active screen reader.
class OnFocusOnFormFieldTest : public BrowserAutofillManagerTest,
                               public testing::WithParamInterface<bool> {
 protected:
  OnFocusOnFormFieldTest() = default;
  ~OnFocusOnFormFieldTest() override = default;

  void SetUp() override {
    BrowserAutofillManagerTest::SetUp();

    has_active_screen_reader_ = GetParam();
    external_delegate()->set_has_active_screen_reader(
        has_active_screen_reader_);
  }

  void TearDown() override {
    external_delegate()->set_has_active_screen_reader(false);
    BrowserAutofillManagerTest::TearDown();
  }

  void CheckSuggestionsAvailableIfScreenReaderRunning() {
#if BUILDFLAG(IS_CHROMEOS_ASH)
    // The only existing functions for determining whether ChromeVox is in use
    // are in the src/chrome directory, which cannot be included in components.
    // Thus, if the platform is ChromeOS, we assume that ChromeVox is in use at
    // this point in the code.
    EXPECT_EQ(true,
              external_delegate()->has_suggestions_available_on_field_focus());
#else
    EXPECT_EQ(has_active_screen_reader_,
              external_delegate()->has_suggestions_available_on_field_focus());
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
  }

  void CheckNoSuggestionsAvailableOnFieldFocus() {
    EXPECT_FALSE(
        external_delegate()->has_suggestions_available_on_field_focus());
  }

  bool has_active_screen_reader_;
};

TEST_P(OnFocusOnFormFieldTest, AddressSuggestions) {
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      // Set a valid autocomplete attribute for the first name.
      CreateTestFormField("First name", "firstname", "",
                          FormControlType::kInputText, "given-name"),
      // Set an unrecognized autocomplete attribute for the last name.
      CreateTestFormField("Last Name", "lastname", "",
                          FormControlType::kInputText, "unrecognized")};
  FormsSeen({form});

  // Suggestions should be returned for the first field.
  browser_autofill_manager_->OnFocusOnFormFieldImpl(form, form.fields[0],
                                                    gfx::RectF());
  CheckSuggestionsAvailableIfScreenReaderRunning();

  // No suggestions should be provided for the second field because of its
  // unrecognized autocomplete attribute.
  browser_autofill_manager_->OnFocusOnFormFieldImpl(form, form.fields[1],
                                                    gfx::RectF());
  CheckNoSuggestionsAvailableOnFieldFocus();
}

TEST_P(OnFocusOnFormFieldTest, AddressSuggestions_AutocompleteOffNotRespected) {
  FormData form;
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {
      // Set a valid autocomplete attribute for the first name.
      CreateTestFormField("First name", "firstname", "",
                          FormControlType::kInputText, "given-name"),
      // Set an autocomplete=off attribute for the last name.
      CreateTestFormField("Last Name", "lastname", "",
                          FormControlType::kInputText, "given-name")};
  form.fields.back().should_autocomplete = false;
  FormsSeen({form});

  browser_autofill_manager_->OnFocusOnFormFieldImpl(form, form.fields[1],
                                                    gfx::RectF());
  CheckSuggestionsAvailableIfScreenReaderRunning();
}

TEST_P(OnFocusOnFormFieldTest, AddressSuggestions_Ablation) {
  base::test::ScopedFeatureList scoped_feature_list;
  DisableAutofillViaAblation(scoped_feature_list, /*for_addresses=*/true,
                             /*for_credit_cards=*/false);

  // Set up our form data.
  FormData form = CreateTestAddressFormData();
  // Clear the form action.
  form.action = GURL();
  FormsSeen({form});

  browser_autofill_manager_->OnFocusOnFormFieldImpl(form, form.fields[1],
                                                    gfx::RectF());
  CheckNoSuggestionsAvailableOnFieldFocus();
}

TEST_P(OnFocusOnFormFieldTest, CreditCardSuggestions_SecureContext) {
  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  // Clear the form action.
  form.action = GURL();
  FormsSeen({form});

  browser_autofill_manager_->OnFocusOnFormFieldImpl(form, form.fields[1],
                                                    gfx::RectF());
  CheckSuggestionsAvailableIfScreenReaderRunning();
}

TEST_P(OnFocusOnFormFieldTest, CreditCardSuggestions_NonSecureContext) {
  // Set up our form data.
  FormData form = CreateTestCreditCardFormData(/*is_https=*/false,
                                               /*use_month_type=*/false);
  // Clear the form action.
  form.action = GURL();
  FormsSeen({form});

  browser_autofill_manager_->OnFocusOnFormFieldImpl(form, form.fields[1],
                                                    gfx::RectF());
  // In a non-HTTPS context, there will be a warning indicating the page is
  // insecure.
  CheckSuggestionsAvailableIfScreenReaderRunning();
}

TEST_P(OnFocusOnFormFieldTest, CreditCardSuggestions_Ablation) {
  base::test::ScopedFeatureList scoped_feature_list;
  DisableAutofillViaAblation(scoped_feature_list, /*for_addresses=*/false,
                             /*for_credit_cards=*/true);

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  // Clear the form action.
  form.action = GURL();
  FormsSeen({form});

  browser_autofill_manager_->OnFocusOnFormFieldImpl(form, form.fields[1],
                                                    gfx::RectF());
  CheckNoSuggestionsAvailableOnFieldFocus();
}

INSTANTIATE_TEST_SUITE_P(BrowserAutofillManagerTest,
                         ProfileMatchingTypesTest,
                         testing::ValuesIn(kProfileMatchingTypesTestCases));

INSTANTIATE_TEST_SUITE_P(All, OnFocusOnFormFieldTest, testing::Bool());

#if BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID)
INSTANTIATE_TEST_SUITE_P(, SuggestionMatchingTest, testing::Values(false));
#else
INSTANTIATE_TEST_SUITE_P(All, SuggestionMatchingTest, testing::Bool());
#endif  // BUILDFLAG(IS_IOS) || BUILDFLAG(IS_ANDROID)

struct ShareNicknameTestParam {
  std::string local_nickname;
  std::string server_nickname;
  std::string expected_nickname;
  bool metadata_enabled;
};

const ShareNicknameTestParam kShareNicknameTestParam[] = {
    {"", "", "", false},
    {"", "server nickname", "server nickname", false},
    {"local nickname", "", "local nickname", false},
    {"local nickname", "server nickname", "local nickname", false},
    {"local nickname", "server nickname", "local nickname", true},
};

class BrowserAutofillManagerTestForSharingNickname
    : public BrowserAutofillManagerTest,
      public testing::WithParamInterface<ShareNicknameTestParam> {
 public:
  BrowserAutofillManagerTestForSharingNickname()
      : local_nickname_(GetParam().local_nickname),
        server_nickname_(GetParam().server_nickname),
        expected_nickname_(GetParam().expected_nickname) {
    if (GetParam().metadata_enabled) {
      card_metadata_flags_.InitWithFeatures(
          /*enabled_features=*/{features::kAutofillEnableVirtualCardMetadata,
                                features::kAutofillEnableCardProductName,
                                features::kAutofillEnableCardArtImage},
          /*disabled_features=*/{});
    } else {
      card_metadata_flags_.InitWithFeatures(
          /*enabled_features=*/{},
          /*disabled_features=*/{features::kAutofillEnableVirtualCardMetadata,
                                 features::kAutofillEnableCardProductName,
                                 features::kAutofillEnableCardArtImage});
    }
  }

  CreditCard GetLocalCard() {
    CreditCard local_card("287151C8-6AB1-487C-9095-28E80BE5DA15",
                          test::kEmptyOrigin);
    test::SetCreditCardInfo(&local_card, "Clyde Barrow",
                            "378282246310005" /* American Express */, "04",
                            "2910", "1");
    local_card.set_use_count(3);
    local_card.set_use_date(AutofillClock::Now() - base::Days(1));
    local_card.SetNickname(base::UTF8ToUTF16(local_nickname_));
    local_card.set_guid(MakeGuid(1));
    return local_card;
  }

  CreditCard GetServerCard() {
    CreditCard full_server_card(CreditCard::RecordType::kFullServerCard,
                                "c789");
    test::SetCreditCardInfo(&full_server_card, "Clyde Barrow",
                            "378282246310005" /* American Express */, "04",
                            "2910", "1");
    full_server_card.SetNickname(base::UTF8ToUTF16(server_nickname_));
    full_server_card.set_guid(MakeGuid(2));
    return full_server_card;
  }

  base::test::ScopedFeatureList card_metadata_flags_;
  std::string local_nickname_;
  std::string server_nickname_;
  std::string expected_nickname_;
};

INSTANTIATE_TEST_SUITE_P(,
                         BrowserAutofillManagerTestForSharingNickname,
                         testing::ValuesIn(kShareNicknameTestParam));

TEST_P(BrowserAutofillManagerTestForSharingNickname,
       VerifySuggestion_DuplicateCards) {
  personal_data().ClearCreditCards();
  ASSERT_EQ(0U, personal_data().GetCreditCards().size());
  CreditCard local_card = GetLocalCard();
  personal_data().AddCreditCard(local_card);
  personal_data().AddServerCreditCard(GetServerCard());
  ASSERT_EQ(2U, personal_data().GetCreditCards().size());

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  // Query by card number field.
  GetAutofillSuggestions(form, form.fields[1]);

  external_delegate()->CheckSuggestions(
      form.fields[1].global_id(),
      {GetCardSuggestion(kAmericanExpressCard, expected_nickname_),
       AutofillSuggestionGenerator::CreateSeparator()});
}

TEST_P(BrowserAutofillManagerTestForSharingNickname,
       VerifySuggestion_UnrelatedCards) {
  personal_data().ClearCreditCards();
  ASSERT_EQ(0U, personal_data().GetCreditCards().size());
  CreditCard local_card = GetLocalCard();
  personal_data().AddCreditCard(local_card);

  std::vector<CreditCard> server_cards;
  CreditCard server_card = GetServerCard();
  // Make sure the cards are different by giving a different card number.
  server_card.SetNumber(u"371449635320005");
  personal_data().AddServerCreditCard(server_card);

  ASSERT_EQ(2U, personal_data().GetCreditCards().size());

  // Set up our form data.
  FormData form =
      CreateTestCreditCardFormData(/*is_https=*/true, /*use_month_type=*/false);
  FormsSeen({form});

  // Query by card number field.
  GetAutofillSuggestions(form, form.fields[1]);

  external_delegate()->CheckSuggestions(
      form.fields[1].global_id(),
      {GetCardSuggestion(kAmericanExpressCard, local_nickname_),
       GetCardSuggestion(kAmericanExpressCard, server_nickname_),
       AutofillSuggestionGenerator::CreateSeparator()});
}

// The following Refill Tests ensure that Autofill can handle the situation
// where it fills a credit card form with an expiration date like 04/2999
// and the website tries to reformat the input with whitespaces around the
// slash and then sacrifices the wrong digits in the expiration date. I.e.,
// the website replaces "04/2099" with "04 / 20". The tests ensure that this
// triggers a refill with "04 / 29".
struct RefillTestCase {
  // The value that JavaScript owned by the website sets for the expiration
  // date filed.
  std::u16string exp_date_from_js;
  // Whether we expect a refill from in this test case.
  bool triggers_refill;
  // What value we expect in the refill.
  const char* refilled_exp_date = nullptr;
};

class BrowserAutofillManagerRefillTest
    : public BrowserAutofillManagerTest,
      public testing::WithParamInterface<RefillTestCase> {};

TEST_P(BrowserAutofillManagerRefillTest,
       RefillModifiedCreditCardExpirationDates) {
  RefillTestCase test_case = GetParam();

  // Set up a CC form with name, cc number and expiration date.
  FormData form;
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  form.fields = {CreateTestFormField("Name on Card", "nameoncard", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Card Number", "cardnumber", "",
                                     FormControlType::kInputText),
                 CreateTestFormField("Expiration date", "exp_date", "",
                                     FormControlType::kInputText)};

  // Notify BrowserAutofillManager of the form.
  FormsSeen({form});

  // Simulate filling and store the data to be filled in |first_fill_data|.
  FormData first_fill_data = FillAutofillFormDataAndGetResults(
      form, *form.fields.begin(), MakeGuid(4));
  ASSERT_EQ(3u, first_fill_data.fields.size());
  ExpectFilledField("Name on Card", "nameoncard", "Elvis Presley",
                    FormControlType::kInputText, first_fill_data.fields[0]);
  ExpectFilledField("Card Number", "cardnumber", "4234567890123456",
                    FormControlType::kInputText, first_fill_data.fields[1]);
  ExpectFilledField("Expiration date", "exp_date", "04/2999",
                    FormControlType::kInputText, first_fill_data.fields[2]);

  FormData refilled_form;
  if (test_case.triggers_refill) {
    // Prepare intercepting the filling operation to the driver and capture
    // the re-filled form data.
    EXPECT_CALL(*autofill_driver_, ApplyFormAction)
        .Times(1)
        .WillOnce(DoAll(SaveArg<2>(&refilled_form),
                        Return(std::vector<FieldGlobalId>{})));
  } else {
    EXPECT_CALL(*autofill_driver_, ApplyFormAction).Times(0);
  }
  // Simulate that JavaScript modifies the expiration date field.
  FormData form_after_js_modification = first_fill_data;
  form_after_js_modification.fields[2].value = test_case.exp_date_from_js;
  browser_autofill_manager_->OnJavaScriptChangedAutofilledValue(
      form_after_js_modification, form_after_js_modification.fields[2],
      u"04/2999");

  testing::Mock::VerifyAndClearExpectations(autofill_driver_.get());

  if (test_case.triggers_refill) {
    ASSERT_EQ(3u, refilled_form.fields.size());
    ExpectFilledField("Name on Card", "nameoncard", "Elvis Presley",
                      FormControlType::kInputText, refilled_form.fields[0]);
    EXPECT_FALSE(refilled_form.fields[0].force_override);
    ExpectFilledField("Card Number", "cardnumber", "4234567890123456",
                      FormControlType::kInputText, refilled_form.fields[1]);
    EXPECT_FALSE(refilled_form.fields[1].force_override);
    ExpectFilledField("Expiration date", "exp_date",
                      test_case.refilled_exp_date, FormControlType::kInputText,
                      refilled_form.fields[2]);
    EXPECT_TRUE(refilled_form.fields[2].force_override);
  }
}

INSTANTIATE_TEST_SUITE_P(
    All,
    BrowserAutofillManagerRefillTest,
    testing::Values(
        // This is the classic case: Autofill filled 04/2999, website overrode
        // 04 / 29, we need to fix this to 04 / 99.
        RefillTestCase{.exp_date_from_js = u"04 / 29",
                       .triggers_refill = true,
                       .refilled_exp_date = "04 / 99"},
        // Maybe the website replaced the separator and added whitespaces.
        RefillTestCase{.exp_date_from_js = u"04 - 29",
                       .triggers_refill = true,
                       .refilled_exp_date = "04 - 99"},
        // Maybe the website only replaced the separator.
        RefillTestCase{.exp_date_from_js = u"04-29",
                       .triggers_refill = true,
                       .refilled_exp_date = "04-99"},
        // Maybe the website was smart and dropped the correct digits.
        RefillTestCase{
            .exp_date_from_js = u"04 / 99",
            .triggers_refill = false,
        },
        // Maybe the website did not modify the values at all.
        RefillTestCase{
            .exp_date_from_js = u"04/2999",
            .triggers_refill = false,
        },
        // Maybe the website did something we don't support.
        RefillTestCase{
            .exp_date_from_js = u"April / 2999",
            .triggers_refill = false,
        },
        // Maybe the website just added some whitespaces.
        RefillTestCase{
            .exp_date_from_js = u"04 / 2999",
            .triggers_refill = false,
        },
        // Don't trigger refill on 3 digit years.
        RefillTestCase{
            .exp_date_from_js = u"04 / 299",
            .triggers_refill = false,
        }));

// Tests that analyze metrics logging in case JavaScript clears a field
// immediately after it was filled.
class BrowserAutofillManagerClearFieldTest : public BrowserAutofillManagerTest {
 public:
  void SetUp() override {
    BrowserAutofillManagerTest::SetUp();

    // Set up a CC form.
    FormData form;
    form.url = GURL("https://myform.com/form.html");
    form.action = GURL("https://myform.com/submit.html");
    form.fields = {CreateTestFormField("Name on Card", "nameoncard", "",
                                       FormControlType::kInputText),
                   CreateTestFormField("Card Number", "cardnumber", "",
                                       FormControlType::kInputText),
                   CreateTestFormField("Expiration date", "exp_date", "",
                                       FormControlType::kInputText)};

    // Notify BrowserAutofillManager of the form.
    FormsSeen({form});

    // Simulate filling and store the data to be filled in `fill_data_`.
    fill_data_ = FillAutofillFormDataAndGetResults(form, *form.fields.begin(),
                                                   MakeGuid(4));
    ASSERT_EQ(3u, fill_data_.fields.size());
    ExpectFilledField("Name on Card", "nameoncard", "Elvis Presley",
                      FormControlType::kInputText, fill_data_.fields[0]);
    ExpectFilledField("Card Number", "cardnumber", "4234567890123456",
                      FormControlType::kInputText, fill_data_.fields[1]);
    ExpectFilledField("Expiration date", "exp_date", "04/2999",
                      FormControlType::kInputText, fill_data_.fields[2]);
  }

  void SimulateOverrideFieldByJavaScript(size_t field_index,
                                         const std::u16string& new_value) {
    std::u16string old_value = fill_data_.fields[field_index].value;
    fill_data_.fields[field_index].value = new_value;
    browser_autofill_manager_->OnJavaScriptChangedAutofilledValue(
        fill_data_, fill_data_.fields[field_index], old_value);
  }

  // Content of the form.
  FormData fill_data_;

  base::HistogramTester histogram_tester_;

  // Shorter alias of the Autofill.FormEvents we are interested in.
  const autofill_metrics::FormEvent kEvent = autofill_metrics::
      FORM_EVENT_AUTOFILLED_FIELD_CLEARED_BY_JAVASCRIPT_AFTER_FILL_ONCE;
};

// Ensure that we log the appropriate Autofill.FormEvents event if an autofilled
// field is cleared by JavaScript immediately after the filling.
TEST_F(BrowserAutofillManagerClearFieldTest, OneClearedField) {
  // Simulate that JavaScript clears the first field.
  SimulateOverrideFieldByJavaScript(0, u"");

  EXPECT_THAT(histogram_tester_.GetAllSamples("Autofill.FormEvents.CreditCard"),
              base::BucketsInclude(base::Bucket(kEvent, 1)));
}

// Ensure that we log a single Autofill.FormEvents event even if *two*
// autofilled fields are cleared by JavaScript immediately after the filling.
TEST_F(BrowserAutofillManagerClearFieldTest, TwoClearedFields) {
  // Simulate that JavaScript clears the first two field.
  SimulateOverrideFieldByJavaScript(0, u"");
  SimulateOverrideFieldByJavaScript(1, u"");

  EXPECT_THAT(histogram_tester_.GetAllSamples("Autofill.FormEvents.CreditCard"),
              base::BucketsInclude(base::Bucket(kEvent, 1)));
}

// Ensure that we do not log an Autofill.FormEvents event for the case that
// JavaScript modifies an autofilled field but does not clear it.
TEST_F(BrowserAutofillManagerClearFieldTest, ModifiedButDidNotClearField) {
  // Simulate that JavaScript modifies the value of the field but does not clear
  // the field.
  SimulateOverrideFieldByJavaScript(0, u"Elvis Aaron Presley");

  EXPECT_THAT(histogram_tester_.GetAllSamples("Autofill.FormEvents.CreditCard"),
              base::BucketsInclude(base::Bucket(kEvent, 0)));
}

// Ensure that we do not log an appropriate Autofill.FormEvents event if an
// autofilled field is cleared by JavaScript too long after it was filled.
TEST_F(BrowserAutofillManagerClearFieldTest, NoLoggingAfterDelay) {
  task_environment_.FastForwardBy(base::Seconds(5));

  // Simulate that JavaScript clears the first field.
  SimulateOverrideFieldByJavaScript(0, u"");

  EXPECT_THAT(histogram_tester_.GetAllSamples("Autofill.FormEvents.CreditCard"),
              base::BucketsInclude(base::Bucket(kEvent, 0)));
}

class BrowserAutofillManagerVotingTest : public BrowserAutofillManagerTest {
 public:
  void SetUp() override {
    BrowserAutofillManagerTest::SetUp();

    // All uploads should be expected explicitly.
    EXPECT_CALL(*crowdsourcing_manager(), StartUploadRequest(_, _, _, _))
        .Times(0);

    form_.name = u"MyForm";
    form_.url = GURL("https://myform.com/form.html");
    form_.action = GURL("https://myform.com/submit.html");
    form_.fields = {
        CreateTestFormField("First Name", "firstname", "",
                            FormControlType::kInputText, "given-name"),
        CreateTestFormField("Last Name", "lastname", "",
                            FormControlType::kInputText, "family-name")};

    // Set up our form data.
    FormsSeen({form_});
  }

  void SimulateTypingFirstNameIntoFirstField() {
    form_.fields[0].value = u"Elvis";
    browser_autofill_manager_->OnTextFieldDidChange(
        form_, form_.fields[0], gfx::RectF(), base::TimeTicks::Now());
  }

 protected:
  FormData form_;
};

// Ensure that a vote is submitted after a regular form submission.
TEST_F(BrowserAutofillManagerVotingTest, Submission) {
  SimulateTypingFirstNameIntoFirstField();
  EXPECT_CALL(*crowdsourcing_manager(),
              StartUploadRequest(
                  FirstElementIs(AllOf(
                      FormSignatureIs(CalculateFormSignature(form_)),
                      FieldsAre(FieldAutofillTypeIs(
                                    {FieldType::NAME_FIRST,
                                     FieldType::CREDIT_CARD_NAME_FIRST}),
                                FieldAutofillTypeIs({FieldType::EMPTY_TYPE})),
                      ObservedSubmissionIs(true))),
                  _, _, _));
  FormSubmitted(form_);
}

// Test that when modifying the form, a blur vote can be sent for the early
// version and a submission vote can be sent for the final version.
TEST_F(BrowserAutofillManagerVotingTest, DynamicFormSubmission) {
  // 1. Simulate typing.
  SimulateTypingFirstNameIntoFirstField();

  // 2. Simulate removing focus from the form, which triggers a blur vote.
  FormSignature first_form_signature = CalculateFormSignature(form_);
  browser_autofill_manager_->OnFocusNoLongerOnForm(true);

  // 3. Simulate typing into second field
  form_.fields[1].value = u"Presley";
  browser_autofill_manager_->OnTextFieldDidChange(
      form_, form_.fields[1], gfx::RectF(), base::TimeTicks::Now());

  // 4. Simulate removing the focus from the form, which generates a second blur
  // vote which should be sent.
  EXPECT_CALL(
      *crowdsourcing_manager(),
      StartUploadRequest(
          FirstElementIs(AllOf(
              FormSignatureIs(first_form_signature),
              FieldsAre(
                  FieldAutofillTypeIs({FieldType::NAME_FIRST,
                                       FieldType::CREDIT_CARD_NAME_FIRST}),
                  FieldAutofillTypeIs({FieldType::NAME_LAST,
                                       FieldType::CREDIT_CARD_NAME_LAST,
                                       FieldType::NAME_LAST_SECOND})),
              ObservedSubmissionIs(false))),
          _, _, _));
  browser_autofill_manager_->OnFocusNoLongerOnForm(true);

  // 5. Grow the form by one field, which changes the form signature.
  form_.fields.push_back(CreateTestFormField(
      "Zip code", "zip", "", FormControlType::kInputText, "postal-code"));
  FormsSeen({form_});

  // 6. Ensure that a form submission triggers votes for the new form.
  // Adding a field should have changed the form signature.
  FormSignature second_form_signature = CalculateFormSignature(form_);
  EXPECT_NE(first_form_signature, second_form_signature);
  // Because the next field after the two names is not a credit card field,
  // field disambiguation removes the credit card name votes.
  EXPECT_CALL(
      *crowdsourcing_manager(),
      StartUploadRequest(
          FirstElementIs(AllOf(
              FormSignatureIs(second_form_signature),
              FieldsAre(FieldAutofillTypeIs({FieldType::NAME_FIRST}),
                        FieldAutofillTypeIs({FieldType::NAME_LAST,
                                             FieldType::NAME_LAST_SECOND}),
                        FieldAutofillTypeIs({FieldType::EMPTY_TYPE})),
              ObservedSubmissionIs(true))),
          _, _, _));
  FormSubmitted(form_);
}

// Ensure that a blur votes is sent after a navigation.
TEST_F(BrowserAutofillManagerVotingTest, BlurVoteOnNavigation) {
  SimulateTypingFirstNameIntoFirstField();

  // Simulate removing focus from form, which triggers a blur vote.
  EXPECT_CALL(*crowdsourcing_manager(),
              StartUploadRequest(
                  FirstElementIs(AllOf(
                      FormSignatureIs(CalculateFormSignature(form_)),
                      FieldsAre(FieldAutofillTypeIs(
                                    {FieldType::NAME_FIRST,
                                     FieldType::CREDIT_CARD_NAME_FIRST}),
                                FieldAutofillTypeIs({FieldType::EMPTY_TYPE})),
                      ObservedSubmissionIs(false))),
                  _, _, _));
  browser_autofill_manager_->OnFocusNoLongerOnForm(true);

  // Simulate a navigation. This is when the vote is sent.
  browser_autofill_manager_->Reset();
}

// Ensure that a submission vote blocks sending a blur vote for the same form
// signature.
TEST_F(BrowserAutofillManagerVotingTest, NoBlurVoteOnSubmission) {
  SimulateTypingFirstNameIntoFirstField();

  // Simulate removing focus from form, which enqueues a blur vote. The blur
  // vote will be ignored and only the submission will be sent.
  browser_autofill_manager_->OnFocusNoLongerOnForm(true);
  EXPECT_CALL(*crowdsourcing_manager(),
              StartUploadRequest(
                  FirstElementIs(AllOf(
                      FormSignatureIs(CalculateFormSignature(form_)),
                      FieldsAre(FieldAutofillTypeIs(
                                    {FieldType::NAME_FIRST,
                                     FieldType::CREDIT_CARD_NAME_FIRST}),
                                FieldAutofillTypeIs({FieldType::EMPTY_TYPE})),
                      ObservedSubmissionIs(true))),
                  _, _, _));
  FormSubmitted(form_);
}

class BrowserAutofillManagerPlusAddressTest
    : public BrowserAutofillManagerTest {
 protected:
  void SetUp() override {
    BrowserAutofillManagerTest::SetUp();
    mock_plus_address_service_ =
        std::make_unique<NiceMock<MockPlusAddressService>>();
    ON_CALL(*mock_plus_address_service_, SupportsPlusAddresses)
        .WillByDefault(Return(true));
    ON_CALL(autofill_client_, GetPlusAddressService)
        .WillByDefault(Return(mock_plus_address_service_.get()));
  }
  std::unique_ptr<NiceMock<MockPlusAddressService>> mock_plus_address_service_;
  base::HistogramTester histogram_tester_;
};

// Ensure that plus address options aren't shown unexpectedly.
TEST_F(BrowserAutofillManagerPlusAddressTest, NoPlusAddressesWithNameFields) {
  // Set up our form data.
  FormData form = test::GetFormData(
      {.fields = {{.role = NAME_FIRST, .autocomplete_attribute = "given-name"},
                  {.role = NAME_LAST}}});
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  FormsSeen({form});

  // Check that suggestions are made for the field that has the autocomplete
  // attribute. Ensure that, despite enabling the plus addresses feature, there
  // is no plus address option shown, because those options are only applicable
  // to email fields.
  GetAutofillSuggestions(form, form.fields[0]);
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion("Charles", "", Suggestion::Icon::kNoIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("Elvis", "", Suggestion::Icon::kNoIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});

  // Also check that there are no suggestions for the field without the
  // autocomplete attribute, ensuring that unrecognized fields don't get plus
  // address options.
  GetAutofillSuggestions(form, form.fields[1]);
  EXPECT_FALSE(external_delegate()->on_suggestions_returned_seen());
  histogram_tester_.ExpectTotalCount(kPlusAddressSuggestionMetric, 0);
}

// Ensure that existing plus addresses are offered.
TEST_F(BrowserAutofillManagerPlusAddressTest, PlusAddressSuggestionShown) {
  plus_addresses::PlusAddressService* plus_address_service =
      autofill_client_.GetPlusAddressService();
  plus_address_service->SavePlusAddress(
      autofill_client_.GetLastCommittedPrimaryMainFrameOrigin(),
      "plus+plus@plus.plus");
  // Set up our form data. Notably, the first field is an email address.
  FormData form = test::GetFormData(
      {.fields = {{.role = EMAIL_ADDRESS, .autocomplete_attribute = "email"}}});
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");
  FormsSeen({form});

  // Check that suggestions are made for the field that has the autocomplete
  // attribute, and ensure that they are exactly the expected set.
  GetAutofillSuggestions(form, form.fields[0]);
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion("plus+plus@plus.plus", "", Suggestion::Icon::kPlusAddress,
                  PopupItemId::kFillExistingPlusAddress),
       Suggestion("buddy@gmail.com", "", Suggestion::Icon::kNoIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("theking@gmail.com", "", Suggestion::Icon::kNoIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});
  EXPECT_THAT(
      histogram_tester_.GetAllSamples(kPlusAddressSuggestionMetric),
      BucketsAre(base::Bucket(
          plus_addresses::PlusAddressMetrics::
              PlusAddressAutofillSuggestionEvent::kExistingPlusAddressSuggested,
          1)));
}

// Ensure that email fields without existing plus addresses for the domain, but
// with the feature enabled, still offer creation of a new plus address.
TEST_F(BrowserAutofillManagerPlusAddressTest,
       CreatePlusAddressSuggestionShown) {
  plus_addresses::PlusAddressService* plus_address_service =
      autofill_client_.GetPlusAddressService();
  // Set up our form data. Notably, the first field is an email address.
  FormData form = test::GetFormData(
      {.fields = {{.role = EMAIL_ADDRESS, .autocomplete_attribute = "email"}}});
  form.name = u"MyForm";
  form.url = GURL("https://myform.com/form.html");
  form.action = GURL("https://myform.com/submit.html");

  FormsSeen({form});

  // Check that suggestions are made for the field that has the autocomplete
  // attribute, and ensure that they are exactly the expected set.
  GetAutofillSuggestions(form, form.fields[0]);
  external_delegate()->CheckSuggestions(
      form.fields[0].global_id(),
      {Suggestion(
           base::UTF16ToUTF8(plus_address_service->GetCreateSuggestionLabel()),
           "", Suggestion::Icon::kPlusAddress,
           PopupItemId::kCreateNewPlusAddress),
       Suggestion("buddy@gmail.com", "", Suggestion::Icon::kNoIcon,
                  PopupItemId::kAddressEntry),
       Suggestion("theking@gmail.com", "", Suggestion::Icon::kNoIcon,
                  PopupItemId::kAddressEntry),
       AutofillSuggestionGenerator::CreateSeparator()});

  EXPECT_THAT(histogram_tester_.GetAllSamples(kPlusAddressSuggestionMetric),
              BucketsAre(base::Bucket(plus_addresses::PlusAddressMetrics::
                                          PlusAddressAutofillSuggestionEvent::
                                              kCreateNewPlusAddressSuggested,
                                      1)));
}

// Test that plus address inputs are forced to !should_autocomplete
// for `SingleFieldFormFillRouter::OnWillSubmitForm()`.
TEST_F(BrowserAutofillManagerPlusAddressTest,
       DontSavePlusAddressInAutocompleteHistory) {
  const std::string kDummyPlusAddress = "plus+plus@plus.plus";
  // Save a dummy plus address so that the service is aware of it.
  plus_addresses::PlusAddressService* plus_address_service =
      autofill_client_.GetPlusAddressService();
  plus_address_service->SavePlusAddress(
      autofill_client_.GetLastCommittedPrimaryMainFrameOrigin(),
      kDummyPlusAddress);
  FormData form_seen_by_autocomplete;
  EXPECT_CALL(single_field_form_fill_router(),
              OnWillSubmitForm(_, _, /*is_autocomplete_enabled=*/true))
      .WillOnce(SaveArg<0>(&form_seen_by_autocomplete));

  FormData form = test::GetFormData(
      {.fields = {{.role = EMAIL_ADDRESS, .name = u"email"},
                  {.role = EMAIL_ADDRESS, .name = u"unfilled-email"}}});

  // First, note the field with the empty value.
  FormsSeen({form});
  // Then fill in the dummy plus address.
  form.fields[0].value = base::UTF8ToUTF16(kDummyPlusAddress);

  // Submit the form, capturing it as it is passed to the autocomplete history
  // manager. The first field should not be autocomplete eligible.
  FormSubmitted(form);

  EXPECT_EQ(form.fields.size(), form_seen_by_autocomplete.fields.size());
  EXPECT_FALSE(form_seen_by_autocomplete.fields[0].should_autocomplete);
  EXPECT_TRUE(form_seen_by_autocomplete.fields[1].should_autocomplete);
}

}  // namespace autofill
