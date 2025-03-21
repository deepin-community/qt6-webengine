// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/base64.h"
#include "base/base_paths.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/test/bind.h"
#include "base/test/gmock_expected_support.h"
#include "base/test/values_test_util.h"
#include "base/time/time.h"
#include "components/aggregation_service/features.h"
#include "components/attribution_reporting/source_registration_time_config.mojom.h"
#include "components/attribution_reporting/suitable_origin.h"
#include "content/browser/aggregation_service/aggregatable_report.h"
#include "content/browser/aggregation_service/aggregation_service.h"
#include "content/browser/aggregation_service/aggregation_service_features.h"
#include "content/browser/aggregation_service/aggregation_service_impl.h"
#include "content/browser/aggregation_service/aggregation_service_test_utils.h"
#include "content/browser/aggregation_service/public_key.h"
#include "content/browser/attribution_reporting/aggregatable_attribution_utils.h"
#include "content/browser/attribution_reporting/attribution_test_utils.h"
#include "content/browser/storage_partition_impl.h"
#include "content/public/common/content_paths.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/numeric/int128.h"
#include "third_party/abseil-cpp/absl/types/variant.h"
#include "third_party/boringssl/src/include/openssl/hpke.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace content {
namespace {

constexpr char kKeyAggregationServicePayloads[] =
    "aggregation_service_payloads";

std::string ReadStringFromFile(const base::FilePath& file, bool trim = false) {
  std::string str;
  EXPECT_TRUE(base::ReadFileToString(file, &str));
  if (trim) {
    str = std::string(base::TrimString(str, base::kWhitespaceASCII,
                                       base::TrimPositions::TRIM_ALL));
  }
  return str;
}

base::Value ParseJsonFromFile(const base::FilePath& file) {
  return base::test::ParseJson(ReadStringFromFile(file));
}

// See
// //content/test/data/attribution_reporting/aggregatable_report_goldens/README.md.
class AttributionAggregatableReportGoldenLatestVersionTest
    : public testing::Test {
 public:
  void SetUp() override {
    base::PathService::Get(content::DIR_TEST_DATA, &input_dir_);
    input_dir_ = input_dir_.AppendASCII(
        "attribution_reporting/aggregatable_report_goldens/latest");

    ASSERT_OK_AND_ASSIGN(
        PublicKeyset keyset,
        aggregation_service::ReadAndParsePublicKeys(
            input_dir_.AppendASCII("public_key.json"), base::Time::Now()));
    ASSERT_EQ(keyset.keys.size(), 1u);

    aggregation_service().SetPublicKeysForTesting(
        GetAggregationServiceProcessingUrl(url::Origin::Create(
            GURL(::aggregation_service::kAggregationServiceCoordinatorAwsCloud
                     .Get()))),
        keyset);
    aggregation_service().SetPublicKeysForTesting(
        GetAggregationServiceProcessingUrl(url::Origin::Create(
            GURL(::aggregation_service::kAggregationServiceCoordinatorGcpCloud
                     .Get()))),
        keyset);

    std::optional<std::vector<uint8_t>> private_key =
        base::Base64Decode(ReadStringFromFile(
            input_dir_.AppendASCII("private_key.txt"), /*trim=*/true));
    ASSERT_TRUE(private_key);
    ASSERT_EQ(static_cast<int>(private_key->size()), X25519_PRIVATE_KEY_LEN);

    ASSERT_TRUE(EVP_HPKE_KEY_init(full_hpke_key_.get(),
                                  EVP_hpke_x25519_hkdf_sha256(),
                                  private_key->data(), private_key->size()));
  }

 protected:
  void AssembleAndVerifyReport(AttributionReport report,
                               base::StringPiece report_file,
                               base::StringPiece cleartext_payloads_file) {
    base::Value expected_report =
        ParseJsonFromFile(input_dir_.AppendASCII(report_file));
    ASSERT_TRUE(expected_report.is_dict());

    base::Value expected_cleartext_payloads =
        ParseJsonFromFile(input_dir_.AppendASCII(cleartext_payloads_file));
    ASSERT_TRUE(expected_cleartext_payloads.is_list());
    ASSERT_EQ(expected_cleartext_payloads.GetList().size(), 1u);

    const std::string* base64_encoded_expected_cleartext_payload =
        expected_cleartext_payloads.GetList().front().GetIfString();
    ASSERT_TRUE(base64_encoded_expected_cleartext_payload);

    std::optional<AggregatableReportRequest> request =
        CreateAggregatableReportRequest(report);
    ASSERT_TRUE(request);

    base::RunLoop run_loop;

    aggregation_service().AssembleReport(
        std::move(*request),
        base::BindLambdaForTesting(
            [&](AggregatableReportRequest,
                std::optional<AggregatableReport> assembled_report,
                AggregationService::AssemblyStatus status) {
              EXPECT_EQ(status, AggregationService::AssemblyStatus::kOk);
              ASSERT_TRUE(assembled_report);
              auto* data =
                  absl::get_if<AttributionReport::AggregatableAttributionData>(
                      &report.data());
              if (data) {
                data->common_data.assembled_report =
                    std::move(*assembled_report);
              } else {
                auto* null_data =
                    absl::get_if<AttributionReport::NullAggregatableData>(
                        &report.data());
                ASSERT_TRUE(null_data);
                null_data->common_data.assembled_report =
                    std::move(*assembled_report);
              }
              EXPECT_TRUE(VerifyReport(
                  report.ReportBody(), std::move(expected_report).TakeDict(),
                  *base64_encoded_expected_cleartext_payload))
                  << "There was an error, consider bumping "
                     "AttributionReport::AggregatableAttributionData::kVersion,"
                     " actual output for "
                  << report_file << " is:\n"
                  << report.ReportBody();
              run_loop.Quit();
            }));

    run_loop.Run();
  }

 private:
  AggregationServiceImpl& aggregation_service() {
    return *static_cast<AggregationServiceImpl*>(
        static_cast<StoragePartitionImpl*>(
            browser_context_.GetDefaultStoragePartition())
            ->GetAggregationService());
  }

  testing::AssertionResult VerifyReport(
      base::Value::Dict actual_report,
      base::Value::Dict expected_report,
      const std::string& base64_encoded_expected_cleartext_payload) {
    std::optional<base::Value> actual_payloads =
        actual_report.Extract(kKeyAggregationServicePayloads);
    if (!actual_payloads) {
      return testing::AssertionFailure() << kKeyAggregationServicePayloads
                                         << " not present in the actual report";
    }

    std::optional<base::Value> expected_payloads =
        expected_report.Extract(kKeyAggregationServicePayloads);
    if (!expected_payloads) {
      return testing::AssertionFailure()
             << kKeyAggregationServicePayloads
             << " not present in the expected report";
    }

    // All other fields are deterministic.
    if (actual_report != expected_report) {
      return testing::AssertionFailure()
             << "The actual report and expected reports do not match, ignoring "
                "the aggregation service payloads";
    }

    static constexpr char kKeySharedInfo[] = "shared_info";
    const std::string* shared_info = expected_report.FindString(kKeySharedInfo);
    if (!shared_info) {
      return testing::AssertionFailure()
             << kKeySharedInfo << " not present in the report";
    }

    if (!actual_payloads->is_list()) {
      return testing::AssertionFailure() << kKeyAggregationServicePayloads
                                         << " not a list in the actual report";
    }

    if (!expected_payloads->is_list()) {
      return testing::AssertionFailure()
             << kKeyAggregationServicePayloads
             << " not a list in the expected report";
    }

    return VerifyAggregationServicePayloads(
        std::move(*actual_payloads).TakeList(),
        std::move(*expected_payloads).TakeList(),
        base64_encoded_expected_cleartext_payload, *shared_info);
  }

  testing::AssertionResult VerifyAggregationServicePayloads(
      base::Value::List actual_payloads,
      base::Value::List expected_payloads,
      const std::string& base64_encoded_expected_cleartext_payload,
      const std::string& shared_info) {
    if (actual_payloads.size() != 1u) {
      return testing::AssertionFailure()
             << kKeyAggregationServicePayloads
             << " not a list of size 1 in the actual report";
    }

    base::Value::Dict* actual_payload = actual_payloads.front().GetIfDict();
    if (!actual_payload) {
      return testing::AssertionFailure()
             << kKeyAggregationServicePayloads
             << "[0] not a dictionary in the actual report";
    }

    if (expected_payloads.size() != 1u) {
      return testing::AssertionFailure()
             << kKeyAggregationServicePayloads
             << " not a list of size 1 in the expected report";
    }

    base::Value::Dict* expected_payload = expected_payloads.front().GetIfDict();
    if (!expected_payload) {
      return testing::AssertionFailure()
             << kKeyAggregationServicePayloads
             << "[0] not a dictionary in the expected report";
    }

    static constexpr char kKeyPayload[] = "payload";

    std::optional<base::Value> actual_encrypted_payload =
        actual_payload->Extract(kKeyPayload);
    if (!actual_encrypted_payload) {
      return testing::AssertionFailure()
             << kKeyPayload << " not present in the actual report";
    }

    std::optional<base::Value> expected_encrypted_payload =
        expected_payload->Extract(kKeyPayload);
    if (!expected_encrypted_payload) {
      return testing::AssertionFailure()
             << kKeyPayload << " not present in the expected report";
    }

    // All other fields are deterministic.
    if (*actual_payload != *expected_payload) {
      return testing::AssertionFailure()
             << "The actual and expected aggregation service payloads do not "
                "match, ignoring the encrypted payloads";
    }

    std::vector<uint8_t> actual_decrypted_payload =
        DecryptPayload(actual_encrypted_payload->GetString(), shared_info);
    if (actual_decrypted_payload.empty()) {
      return testing::AssertionFailure()
             << "Failed to decrypt payload in the actual report";
    }

    std::vector<uint8_t> expected_decrypted_payload =
        DecryptPayload(expected_encrypted_payload->GetString(), shared_info);
    if (expected_decrypted_payload.empty()) {
      return testing::AssertionFailure()
             << "Failed to decrypt payload in the expected payload";
    }

    if (actual_decrypted_payload != expected_decrypted_payload) {
      return testing::AssertionFailure()
             << "The actual and expected decrypted payloads do not match";
    }

    if (std::string base64_encoded_decrypted_payload =
            base::Base64Encode(actual_decrypted_payload);
        base64_encoded_decrypted_payload !=
        base64_encoded_expected_cleartext_payload) {
      return testing::AssertionFailure()
             << "The expected cleartext payload does not match actual "
                "decrypted payload, actual output is "
             << base64_encoded_decrypted_payload;
    }

    return testing::AssertionSuccess();
  }

  // Returns empty vector in case of an error.
  std::vector<uint8_t> DecryptPayload(
      const std::string& base64_encoded_encrypted_payload,
      const std::string& shared_info) {
    std::optional<std::vector<uint8_t>> encrypted_payload =
        base::Base64Decode(base64_encoded_encrypted_payload);
    if (!encrypted_payload) {
      return {};
    }

    return aggregation_service::DecryptPayloadWithHpke(
        *encrypted_payload, *full_hpke_key_.get(), shared_info);
  }

  BrowserTaskEnvironment task_environment_;
  TestBrowserContext browser_context_;
  base::FilePath input_dir_;
  bssl::ScopedEVP_HPKE_KEY full_hpke_key_;
};

TEST_F(AttributionAggregatableReportGoldenLatestVersionTest,
       VerifyGoldenReport) {
  const auto kGcpCoordinatorOrigin =
      *attribution_reporting::SuitableOrigin::Deserialize(
          ::aggregation_service::kAggregationServiceCoordinatorGcpCloud.Get());

  struct {
    AttributionReport report;
    base::StringPiece report_file;
    base::StringPiece cleartext_payloads_file;
  } kTestCases[] = {
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().SetDebugKey(456).Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483200000))
                   .SetDebugKey(123)
                   .SetDebugCookieSet(true)
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/1, /*value=*/2)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486400000))
               .BuildAggregatableAttribution(),
       .report_file = "report_1.json",
       .cleartext_payloads_file = "report_1_cleartext_payloads.json"},
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483200000))
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/1, /*value=*/2)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486400000))
               .BuildAggregatableAttribution(),
       .report_file = "report_2.json",
       .cleartext_payloads_file = "report_2_cleartext_payloads.json"},
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().SetDebugKey(456).Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483300000))
                   .SetDebugKey(123)
                   .SetDebugCookieSet(true)
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/1, /*value=*/2),
                    AggregatableHistogramContribution(/*key=*/3, /*value=*/4)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486500000))
               .BuildAggregatableAttribution(),
       .report_file = "report_3.json",
       .cleartext_payloads_file = "report_3_cleartext_payloads.json"},
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483300000))
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/1, /*value=*/2),
                    AggregatableHistogramContribution(/*key=*/3, /*value=*/4)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486500000))
               .BuildAggregatableAttribution(),
       .report_file = "report_4.json",
       .cleartext_payloads_file = "report_4_cleartext_payloads.json"},
      {.report = ReportBuilder(
                     AttributionInfoBuilder().SetDebugKey(456).Build(),
                     SourceBuilder(base::Time::FromMillisecondsSinceUnixEpoch(
                                       1234483400000))
                         .SetDebugKey(123)
                         .SetDebugCookieSet(true)
                         .BuildStored())
                     .SetAggregatableHistogramContributions(
                         {AggregatableHistogramContribution(
                             /*key=*/absl::Uint128Max(), /*value=*/1000)})
                     .SetReportTime(base::Time::FromMillisecondsSinceUnixEpoch(
                         1234486600000))
                     .BuildAggregatableAttribution(),
       .report_file = "report_5.json",
       .cleartext_payloads_file = "report_5_cleartext_payloads.json"},
      {.report = ReportBuilder(
                     AttributionInfoBuilder().Build(),
                     SourceBuilder(base::Time::FromMillisecondsSinceUnixEpoch(
                                       1234483400000))
                         .BuildStored())
                     .SetAggregatableHistogramContributions(
                         {AggregatableHistogramContribution(
                             /*key=*/absl::Uint128Max(), /*value=*/1000)})
                     .SetReportTime(base::Time::FromMillisecondsSinceUnixEpoch(
                         1234486600000))
                     .BuildAggregatableAttribution(),
       .report_file = "report_6.json",
       .cleartext_payloads_file = "report_6_cleartext_payloads.json"},
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483200000))
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/0, /*value=*/1)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486400000))
               .SetSourceRegistrationTimeConfig(
                   attribution_reporting::mojom::SourceRegistrationTimeConfig::
                       kExclude)
               .BuildAggregatableAttribution(),
       .report_file = "report_7.json",
       .cleartext_payloads_file = "report_7_cleartext_payloads.json"},
      {.report = ReportBuilder(
                     AttributionInfoBuilder().Build(),
                     SourceBuilder(base::Time::FromMillisecondsSinceUnixEpoch(
                                       1234483200000))
                         .BuildStored())
                     .SetReportTime(base::Time::FromMillisecondsSinceUnixEpoch(
                         1234486400000))
                     .SetSourceRegistrationTimeConfig(
                         attribution_reporting::mojom::
                             SourceRegistrationTimeConfig::kExclude)
                     .BuildNullAggregatable(),
       .report_file = "report_8.json",
       .cleartext_payloads_file = "report_8_cleartext_payloads.json"},
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483200000))
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/0, /*value=*/1)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486400000))
               .SetSourceRegistrationTimeConfig(
                   attribution_reporting::mojom::SourceRegistrationTimeConfig::
                       kExclude)
               .SetTriggerContextId("example")
               .BuildAggregatableAttribution(),
       .report_file = "report_9.json",
       .cleartext_payloads_file = "report_9_cleartext_payloads.json"},
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().SetDebugKey(456).Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483200000))
                   .SetDebugKey(123)
                   .SetDebugCookieSet(true)
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/1, /*value=*/2)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486400000))
               .SetAggregationCoordinatorOrigin(kGcpCoordinatorOrigin)
               .BuildAggregatableAttribution(),
       .report_file = "report_gcp_1.json",
       .cleartext_payloads_file = "report_gcp_1_cleartext_payloads.json"},
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483200000))
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/1, /*value=*/2)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486400000))
               .SetAggregationCoordinatorOrigin(kGcpCoordinatorOrigin)
               .BuildAggregatableAttribution(),
       .report_file = "report_gcp_2.json",
       .cleartext_payloads_file = "report_gcp_2_cleartext_payloads.json"},
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().SetDebugKey(456).Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483300000))
                   .SetDebugKey(123)
                   .SetDebugCookieSet(true)
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/1, /*value=*/2),
                    AggregatableHistogramContribution(/*key=*/3, /*value=*/4)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486500000))
               .SetAggregationCoordinatorOrigin(kGcpCoordinatorOrigin)
               .BuildAggregatableAttribution(),
       .report_file = "report_gcp_3.json",
       .cleartext_payloads_file = "report_gcp_3_cleartext_payloads.json"},
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483300000))
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/1, /*value=*/2),
                    AggregatableHistogramContribution(/*key=*/3, /*value=*/4)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486500000))
               .SetAggregationCoordinatorOrigin(kGcpCoordinatorOrigin)
               .BuildAggregatableAttribution(),
       .report_file = "report_gcp_4.json",
       .cleartext_payloads_file = "report_gcp_4_cleartext_payloads.json"},
      {.report = ReportBuilder(
                     AttributionInfoBuilder().SetDebugKey(456).Build(),
                     SourceBuilder(base::Time::FromMillisecondsSinceUnixEpoch(
                                       1234483400000))
                         .SetDebugKey(123)
                         .SetDebugCookieSet(true)
                         .BuildStored())
                     .SetAggregatableHistogramContributions(
                         {AggregatableHistogramContribution(
                             /*key=*/absl::Uint128Max(), /*value=*/1000)})
                     .SetReportTime(base::Time::FromMillisecondsSinceUnixEpoch(
                         1234486600000))
                     .SetAggregationCoordinatorOrigin(kGcpCoordinatorOrigin)
                     .BuildAggregatableAttribution(),
       .report_file = "report_gcp_5.json",
       .cleartext_payloads_file = "report_gcp_5_cleartext_payloads.json"},
      {.report = ReportBuilder(
                     AttributionInfoBuilder().Build(),
                     SourceBuilder(base::Time::FromMillisecondsSinceUnixEpoch(
                                       1234483400000))
                         .BuildStored())
                     .SetAggregatableHistogramContributions(
                         {AggregatableHistogramContribution(
                             /*key=*/absl::Uint128Max(), /*value=*/1000)})
                     .SetReportTime(base::Time::FromMillisecondsSinceUnixEpoch(
                         1234486600000))
                     .SetAggregationCoordinatorOrigin(kGcpCoordinatorOrigin)
                     .BuildAggregatableAttribution(),
       .report_file = "report_gcp_6.json",
       .cleartext_payloads_file = "report_gcp_6_cleartext_payloads.json"},
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483200000))
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/0, /*value=*/1)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486400000))
               .SetSourceRegistrationTimeConfig(
                   attribution_reporting::mojom::SourceRegistrationTimeConfig::
                       kExclude)
               .SetAggregationCoordinatorOrigin(kGcpCoordinatorOrigin)
               .BuildAggregatableAttribution(),
       .report_file = "report_gcp_7.json",
       .cleartext_payloads_file = "report_gcp_7_cleartext_payloads.json"},
      {.report = ReportBuilder(
                     AttributionInfoBuilder().Build(),
                     SourceBuilder(base::Time::FromMillisecondsSinceUnixEpoch(
                                       1234483200000))
                         .BuildStored())
                     .SetReportTime(base::Time::FromMillisecondsSinceUnixEpoch(
                         1234486400000))
                     .SetSourceRegistrationTimeConfig(
                         attribution_reporting::mojom::
                             SourceRegistrationTimeConfig::kExclude)
                     .SetAggregationCoordinatorOrigin(kGcpCoordinatorOrigin)
                     .BuildNullAggregatable(),
       .report_file = "report_gcp_8.json",
       .cleartext_payloads_file = "report_gcp_8_cleartext_payloads.json"},
      {.report =
           ReportBuilder(
               AttributionInfoBuilder().Build(),
               SourceBuilder(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234483200000))
                   .BuildStored())
               .SetAggregatableHistogramContributions(
                   {AggregatableHistogramContribution(/*key=*/0, /*value=*/1)})
               .SetReportTime(
                   base::Time::FromMillisecondsSinceUnixEpoch(1234486400000))
               .SetSourceRegistrationTimeConfig(
                   attribution_reporting::mojom::SourceRegistrationTimeConfig::
                       kExclude)
               .SetTriggerContextId("example")
               .SetAggregationCoordinatorOrigin(kGcpCoordinatorOrigin)
               .BuildAggregatableAttribution(),
       .report_file = "report_gcp_9.json",
       .cleartext_payloads_file = "report_gcp_9_cleartext_payloads.json"},
  };

  for (auto& test_case : kTestCases) {
    AssembleAndVerifyReport(std::move(test_case.report), test_case.report_file,
                            test_case.cleartext_payloads_file);
  }
}

std::vector<base::FilePath> GetLegacyVersions() {
  base::FilePath input_dir;
  base::PathService::Get(base::DIR_SRC_TEST_DATA_ROOT, &input_dir);
  input_dir = input_dir.AppendASCII(
      "content/test/data/attribution_reporting/aggregatable_report_goldens");

  std::vector<base::FilePath> input_paths;

  base::FileEnumerator e(input_dir, /*recursive=*/false,
                         base::FileEnumerator::DIRECTORIES);

  for (base::FilePath name = e.Next(); !name.empty(); name = e.Next()) {
    if (name.BaseName() == base::FilePath(FILE_PATH_LITERAL("latest"))) {
      continue;
    }

    input_paths.push_back(std::move(name));
  }

  return input_paths;
}

// Verifies that legacy versions are properly labeled/stored. Note that there
// is an implicit requirement that "version" is located in the "shared_info"
// field in the report.
class AttributionAggregatableReportGoldenLegacyVersionTest
    : public ::testing::TestWithParam<base::FilePath> {};

TEST_P(AttributionAggregatableReportGoldenLegacyVersionTest,
       HasExpectedVersion) {
  static constexpr base::StringPiece prefix = "version_";

  base::FilePath dir = GetParam();

  std::string base_name = dir.BaseName().MaybeAsASCII();
  ASSERT_TRUE(base::StartsWith(base_name, prefix));

  std::string expected_version = base_name.substr(prefix.size());

  base::FileEnumerator e(dir, /*recursive=*/false, base::FileEnumerator::FILES,
                         FILE_PATH_LITERAL("*.json"));

  for (base::FilePath name = e.Next(); !name.empty(); name = e.Next()) {
    base::Value value = ParseJsonFromFile(name);
    if (!value.is_dict()) {
      continue;
    }

    const base::Value::Dict& dict = value.GetDict();
    if (const std::string* shared_info = dict.FindString("shared_info")) {
      base::Value shared_info_value = base::test::ParseJson(*shared_info);
      EXPECT_TRUE(shared_info_value.is_dict()) << name;
      if (!shared_info_value.is_dict()) {
        continue;
      }

      const std::string* version =
          shared_info_value.GetDict().FindString("version");
      EXPECT_TRUE(version) << name;
      if (!version) {
        continue;
      }

      EXPECT_EQ(*version, expected_version) << name;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(All,
                         AttributionAggregatableReportGoldenLegacyVersionTest,
                         ::testing::ValuesIn(GetLegacyVersions()));

}  // namespace
}  // namespace content
