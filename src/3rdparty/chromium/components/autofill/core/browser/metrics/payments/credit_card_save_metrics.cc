// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/metrics/payments/credit_card_save_metrics.h"

#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/strcat.h"
#include "services/metrics/public/cpp/ukm_builders.h"

namespace autofill::autofill_metrics {

void LogCardUploadDecisionMetrics(int upload_decision_metrics) {
  DCHECK(upload_decision_metrics);
  DCHECK_LT(upload_decision_metrics, 1 << kNumCardUploadDecisionMetrics);

  for (int metric = 0; metric < kNumCardUploadDecisionMetrics; ++metric)
    if (upload_decision_metrics & (1 << metric))
      UMA_HISTOGRAM_ENUMERATION("Autofill.CardUploadDecisionMetric", metric,
                                kNumCardUploadDecisionMetrics);
}

void LogCardUploadDecisionsUkm(ukm::UkmRecorder* ukm_recorder,
                               ukm::SourceId source_id,
                               const GURL& url,
                               int upload_decision_metrics) {
  DCHECK(upload_decision_metrics);
  DCHECK_LT(upload_decision_metrics, 1 << kNumCardUploadDecisionMetrics);
  if (!url.is_valid())
    return;
  ukm::builders::Autofill_CardUploadDecision(source_id)
      .SetUploadDecision(upload_decision_metrics)
      .Record(ukm_recorder);
}

void LogCardUploadEnabledMetric(
    CardUploadEnabled metric_value,
    AutofillMetrics::PaymentsSigninState sync_state) {
  const std::string parent_metric = std::string("Autofill.CardUploadEnabled");
  base::UmaHistogramEnumeration(parent_metric, metric_value);

  const std::string child_metric =
      parent_metric + AutofillMetrics::GetMetricsSyncStateSuffix(sync_state);
  base::UmaHistogramEnumeration(child_metric, metric_value);
}

void LogCreditCardSaveNotOfferedDueToMaxStrikesMetric(
    AutofillMetrics::SaveTypeMetric metric) {
  UMA_HISTOGRAM_ENUMERATION(
      "Autofill.StrikeDatabase.CreditCardSaveNotOfferedDueToMaxStrikes",
      metric);
}

void LogCreditCardUploadLegalMessageLinkClicked() {
  base::RecordAction(base::UserMetricsAction(
      "Autofill_CreditCardUpload_LegalMessageLinkClicked"));
}

void LogSaveCardCardholderNamePrefilled(bool prefilled) {
  UMA_HISTOGRAM_BOOLEAN("Autofill.SaveCardCardholderNamePrefilled", prefilled);
}

void LogSaveCardCardholderNameWasEdited(bool edited) {
  UMA_HISTOGRAM_BOOLEAN("Autofill.SaveCardCardholderNameWasEdited", edited);
}

void LogSaveCardPromptOfferMetric(
    SaveCardPromptOffer metric,
    bool is_uploading,
    bool is_reshow,
    AutofillClient::SaveCreditCardOptions options,
    security_state::SecurityLevel security_level,
    AutofillMetrics::PaymentsSigninState sync_state) {
  DCHECK_LE(metric, SaveCardPromptOffer::kMaxValue);
  std::string base_histogram_name = "Autofill.SaveCreditCardPromptOffer";
  std::string destination = is_uploading ? ".Upload" : ".Local";
  std::string show = is_reshow ? ".Reshows" : ".FirstShow";
  std::string metric_with_destination_and_show =
      base::StrCat({base_histogram_name, destination, show});

  base::UmaHistogramEnumeration(metric_with_destination_and_show, metric);

  base::UmaHistogramEnumeration(
      metric_with_destination_and_show +
          AutofillMetrics::GetMetricsSyncStateSuffix(sync_state),
      metric);

  if (options.should_request_name_from_user) {
    base::UmaHistogramEnumeration(
        metric_with_destination_and_show + ".RequestingCardholderName", metric);
  }
  if (options.should_request_expiration_date_from_user) {
    base::UmaHistogramEnumeration(
        metric_with_destination_and_show + ".RequestingExpirationDate", metric);
  }
  if (options.has_multiple_legal_lines) {
    base::UmaHistogramEnumeration(
        metric_with_destination_and_show + ".WithMultipleLegalLines", metric);
  }
  if (options.has_same_last_four_as_server_card_but_different_expiration_date) {
    base::UmaHistogramEnumeration(metric_with_destination_and_show +
                                      ".WithSameLastFourButDifferentExpiration",
                                  metric);
  }
  if (options.card_save_type ==
      AutofillClient::CardSaveType::kCardSaveWithCvc) {
    base::UmaHistogramEnumeration(
        metric_with_destination_and_show + ".SavingWithCvc", metric);
  }

  if (security_level != security_state::SecurityLevel::SECURITY_LEVEL_COUNT) {
    base::UmaHistogramEnumeration(
        security_state::GetSecurityLevelHistogramName(
            base_histogram_name + destination, security_level),
        metric);
  }
}

void LogSaveCardPromptResultMetric(
    SaveCardPromptResult metric,
    bool is_uploading,
    bool is_reshow,
    AutofillClient::SaveCreditCardOptions options,
    security_state::SecurityLevel security_level,
    AutofillMetrics::PaymentsSigninState sync_state) {
  DCHECK_LE(metric, SaveCardPromptResult::kMaxValue);
  std::string base_histogram_name = "Autofill.SaveCreditCardPromptResult";
  std::string destination = is_uploading ? ".Upload" : ".Local";
  std::string show = is_reshow ? ".Reshows" : ".FirstShow";
  std::string metric_with_destination_and_show =
      base::StrCat({base_histogram_name, destination, show});

  base::UmaHistogramEnumeration(metric_with_destination_and_show, metric);

  base::UmaHistogramEnumeration(
      metric_with_destination_and_show +
          AutofillMetrics::GetMetricsSyncStateSuffix(sync_state),
      metric);

  if (options.should_request_name_from_user) {
    base::UmaHistogramEnumeration(
        metric_with_destination_and_show + ".RequestingCardholderName", metric);
  }
  if (options.should_request_expiration_date_from_user) {
    base::UmaHistogramEnumeration(
        metric_with_destination_and_show + ".RequestingExpirationDate", metric);
  }
  if (options.has_multiple_legal_lines) {
    base::UmaHistogramEnumeration(
        metric_with_destination_and_show + ".WithMultipleLegalLines", metric);
  }
  if (options.has_same_last_four_as_server_card_but_different_expiration_date) {
    base::UmaHistogramEnumeration(metric_with_destination_and_show +
                                      ".WithSameLastFourButDifferentExpiration",
                                  metric);
  }
  if (options.card_save_type ==
      AutofillClient::CardSaveType::kCardSaveWithCvc) {
    base::UmaHistogramEnumeration(
        metric_with_destination_and_show + ".SavingWithCvc", metric);
  }

  if (security_level != security_state::SecurityLevel::SECURITY_LEVEL_COUNT) {
    base::UmaHistogramEnumeration(
        security_state::GetSecurityLevelHistogramName(
            base_histogram_name + destination, security_level),
        metric);
  }
}

void LogSaveCvcPromptOfferMetric(SaveCardPromptOffer metric,
                                 bool is_uploading,
                                 bool is_reshow) {
  DCHECK_LE(metric, SaveCardPromptOffer::kMaxValue);
  std::string_view base_histogram_name = "Autofill.SaveCvcPromptOffer";
  std::string_view destination = is_uploading ? ".Upload" : ".Local";
  std::string_view show = is_reshow ? ".Reshows" : ".FirstShow";

  base::UmaHistogramEnumeration(
      base::StrCat({base_histogram_name, destination, show}), metric);
}

void LogSaveCvcPromptResultMetric(SaveCardPromptResult metric,
                                  bool is_uploading,
                                  bool is_reshow) {
  DCHECK_LE(metric, SaveCardPromptResult::kMaxValue);
  std::string_view base_histogram_name = "Autofill.SaveCvcPromptResult";
  std::string_view destination = is_uploading ? ".Upload" : ".Local";
  std::string_view show = is_reshow ? ".Reshows" : ".FirstShow";

  base::UmaHistogramEnumeration(
      base::StrCat({base_histogram_name, destination, show}), metric);
}

void LogCvcInfoBarMetric(AutofillMetrics::InfoBarMetric metric,
                         bool is_uploading) {
  CHECK_LT(metric, AutofillMetrics::InfoBarMetric::NUM_INFO_BAR_METRICS);
  base::UmaHistogramEnumeration(
      base::StrCat(
          {"Autofill.CvcInfoBar", is_uploading ? ".Upload" : ".Local"}),
      metric, AutofillMetrics::InfoBarMetric::NUM_INFO_BAR_METRICS);
}

void LogSaveCardRequestExpirationDateReasonMetric(
    SaveCardRequestExpirationDateReason reason) {
  DCHECK_LE(reason, SaveCardRequestExpirationDateReason::kMaxValue);
  UMA_HISTOGRAM_ENUMERATION("Autofill.SaveCardRequestExpirationDateReason",
                            reason);
}

// Clank-specific metrics.
void LogSaveCreditCardPromptResult(
    SaveCreditCardPromptResult event,
    bool is_upload,
    AutofillClient::SaveCreditCardOptions options) {
  if (!is_upload) {
    base::UmaHistogramEnumeration("Autofill.CreditCardSaveFlowResult.Local",
                                  event);
    return;
  }
  base::UmaHistogramEnumeration("Autofill.CreditCardSaveFlowResult.Server",
                                event);
  if (options.should_request_expiration_date_from_user) {
    base::UmaHistogramEnumeration(
        "Autofill.CreditCardSaveFlowResult.Server.RequestingExpirationDate",
        event);
  }
  if (options.should_request_name_from_user) {
    base::UmaHistogramEnumeration(
        "Autofill.CreditCardSaveFlowResult.Server.RequestingCardholderName",
        event);
  }
}

}  // namespace autofill::autofill_metrics
