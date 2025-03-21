// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_COMMON_AUTOFILL_FEATURES_H_
#define COMPONENTS_AUTOFILL_CORE_COMMON_AUTOFILL_FEATURES_H_

#include "base/component_export.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial_params.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace autofill::features {

// All features in alphabetical order.
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillAcrossIframesIos);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillGivePrecedenceToNumericQuantities);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillAddressProfileSavePromptNicknameSupport);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillAddressUserPerceptionSurvey);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillAllowDuplicateFormSubmissions);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillAssociateForms);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<base::TimeDelta> kAutofillAssociateFormsTTL;
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillInferCountryCallingCode);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillConsiderPhoneNumberSeparatorsValidLabels);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillContentEditables);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillDefaultToCityAndNumber);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillDeferSubmissionClassificationAfterAjax);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillDontPreserveAutofillState);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableSelectList);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableSupportForBetweenStreets);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableSupportForAdminLevel2);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableSupportForAddressOverflow);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableSupportForAddressOverflowAndLandmark);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableSupportForBetweenStreetsOrLandmark);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableSupportForLandmark);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableParsingOfStreetLocation);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableRationalizationEngineForMX);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillDisableFilling);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillDisableAddressImport);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillDisableShadowHeuristics);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableBirthdateParsing);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableDependentLocalityParsing);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableDevtoolsIssues);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableExpirationDateImprovements);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableImportWhenMultiplePhoneNumbers);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableParsingEmptyPhoneNumberLabels);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableRankingFormulaAddressProfiles);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<int>
    kAutofillRankingFormulaAddressProfilesUsageHalfLife;
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableRankingFormulaCreditCards);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<int>
    kAutofillRankingFormulaCreditCardsUsageHalfLife;
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<int> kAutofillRankingFormulaVirtualCardBoost;
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<int>
    kAutofillRankingFormulaVirtualCardBoostHalfLife;
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillRelaxCreditCardImport);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableEmailHeuristicOnlyAddressForms);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableSupportForApartmentNumbers);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableLabelPrecedenceForTurkishAddresses);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableSupportForParsingWithSharedLabels);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableZipOnlyAddressForms);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableSupportForPhoneNumberTrunkTypes);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillDetectRemovedFormControls);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillReplaceCachedWebElementsByRendererIds);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillUseAddressRewriterInProfileSubsetComparison);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillUseI18nAddressModel);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillUseDEAddressModel);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillUndo);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillConvergeToExtremeLengthStreetAddress);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<bool> kAutofillConvergeToLonger;
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(
    kAutofillStreetNameOrHouseNumberPrecedenceOverAutocomplete);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillIgnoreUnmappableAutocompleteValues);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillLocalHeuristicsOverrides);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillHighlightOnlyChangedValuesInPreviewMode);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillLabelAffixRemoval);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillModelPredictions);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<bool> kAutofillModelPredictionsAreActive;
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillOverwritePlaceholdersOnly);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillSkipPreFilledFields);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillPageLanguageDetection);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillParseNameAsAutocompleteType);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillParsingPatternProvider);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<std::string>
    kAutofillParsingPatternActiveSource;
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillAlwaysParsePlaceholders);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillPopupDisablePaintChecks);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillPopupDoesNotOverlapWithContextMenu);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillPopupExtensionCursorSuppression);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillPopupImprovedTimingChecks);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillPopupUseLatencyInformationForAcceptThreshold);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillPreferLabelsInSomeCountries);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillProbableFormSubmissionInBrowser);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillServerBehaviors);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<int> kAutofillServerBehaviorsParam;
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillSharedAutofill);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillStructuredFieldsDisableAddressLines);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillShowAutocompleteDeleteButton);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillGranularFillingAvailable);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillForUnclassifiedFieldsAvailable);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillTestFormWithDevtools);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillSilentProfileUpdateForInsufficientImport);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillTextAreaChangeEvents);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillContentEditableChangeEvents);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillTrackProfileTokenQuality);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillUseNewSectioningMethod);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillUseParameterizedSectioning);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillUseTypedCreditCardNumber);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(
    kAutofillDontCheckForDisappearingFormlessElementsForSubmission);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<bool> kAutofillSectioningModeIgnoreAutocomplete;
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<bool> kAutofillSectioningModeCreateGaps;
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<bool> kAutofillSectioningModeExpand;
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillEnableAblationStudy);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<bool>
    kAutofillAblationStudyEnabledForAddressesParam;
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<bool>
    kAutofillAblationStudyEnabledForPaymentsParam;
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<int>
    kAutofillAblationStudyAblationWeightPerMilleParam;
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillVoteForSelectOptionValues);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<int>
    kAutofillMoreProminentPopupMaxOffsetToCenterParam;
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillMoreProminentPopup);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillLogUKMEventsWithSampleRate);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillUseUpdatedRequiredFieldsForAddressImport);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillVirtualCardsOnTouchToFillAndroid);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillAndroidDisableSuggestionsOnJSFocus);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillEnableCacheForRegexMatching);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<int>
    kAutofillEnableCacheForRegexMatchingCacheSizeParam;

#if BUILDFLAG(IS_ANDROID)
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillVirtualViewStructureAndroid);
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_APPLE)
// Returns true if whether the views autofill popup feature is enabled or the
// we're using the views browser.
COMPONENT_EXPORT(AUTOFILL)
bool IsMacViewsAutofillPopupExperimentEnabled();
#endif  // BUILDFLAG(IS_APPLE)

// The features in this namespace contains are not meant to be rolled out. They
// are are only intended for manual testing purposes.
namespace test {

COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillAllowNonHttpActivation);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillCapturedSiteTestsMetricsScraper);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<std::string>
    kAutofillCapturedSiteTestsMetricsScraperOutputDir;
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<std::string>
    kAutofillCapturedSiteTestsMetricsScraperHistogramRegex;
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillDisableProfileUpdates);
COMPONENT_EXPORT(AUTOFILL)
BASE_DECLARE_FEATURE(kAutofillDisableSilentProfileUpdates);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillLogToTerminal);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillOverridePredictions);
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<std::string>
    kAutofillOverridePredictionsSpecification;
COMPONENT_EXPORT(AUTOFILL)
extern const base::FeatureParam<std::string>
    kAutofillOverridePredictionsForAlternativeFormSignaturesSpecification;
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillServerCommunication);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillShowTypePredictions);
COMPONENT_EXPORT(AUTOFILL) BASE_DECLARE_FEATURE(kAutofillUploadThrottling);

}  // namespace test

}  // namespace autofill::features

#endif  // COMPONENTS_AUTOFILL_CORE_COMMON_AUTOFILL_FEATURES_H_
