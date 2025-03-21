// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cups/ipp.h>

#include "base/functional/callback_helpers.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/values_test_util.h"
#include "chrome/browser/chromeos/printing/cups_wrapper.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/extensions/api/printing/printing_test_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/web_applications/test/isolated_web_app_test_utils.h"
#include "chromeos/printing/printer_configuration.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/features_generated.h"

#if BUILDFLAG(IS_CHROMEOS_LACROS)
#include "chrome/browser/printing/local_printer_utils_chromeos.h"
#include "chrome/test/chromeos/printing/mock_local_printer_chromeos.h"
#include "chromeos/lacros/lacros_service.h"
#endif

namespace printing {

namespace {

constexpr char kId[] = "id";
constexpr char kName[] = "name";

constexpr std::string_view kPrintScriptWithJobStatePlaceholder = R"(
    (async () => {
      const pdf = `%PDF-1.0
1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj 2 0 ` +
`obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj 3 0 ` +
`obj<</Type/Page/MediaBox[0 0 3 3]>>endobj
xref
0 4
0000000000 65535 f
0000000010 00000 n
0000000053 00000 n
0000000102 00000 n
trailer<</Size 4/Root 1 0 R>>
startxref
149
%EOF`;

    const pdfBlob = new Blob([pdf], {type: 'application/pdf'});
    const printers = await navigator.printing.getPrinters();

    const printJob = await printers[0].printJob("Title", { data: pdfBlob }, {});
    const printJobComplete = new Promise((resolve, reject) => {
      printJob.onjobstatechange = () => {
        if (printJob.attributes().jobState === $1) {
          resolve();
          return;
        }
      };
    });
    await printJobComplete;
   })();
  )";

using testing::_;
using testing::AtMost;

#if BUILDFLAG(IS_CHROMEOS_LACROS)
using testing::DoAll;
using testing::InSequence;
using testing::NiceMock;
using testing::WithArg;
using testing::WithArgs;
#endif

class MockCupsWrapper : public chromeos::CupsWrapper {
 public:
  ~MockCupsWrapper() override = default;

  MOCK_METHOD(void,
              QueryCupsPrintJobs,
              (const std::vector<std::string>& printer_ids,
               base::OnceCallback<void(std::unique_ptr<QueryResult>)> callback),
              (override));
  MOCK_METHOD(void,
              CancelJob,
              (const std::string& printer_id, int job_id),
              (override));
  MOCK_METHOD(
      void,
      QueryCupsPrinterStatus,
      (const std::string& printer_id,
       base::OnceCallback<void(std::unique_ptr<::printing::PrinterStatus>)>
           callback),
      (override));
};

}  // namespace

class WebPrintingBrowserTestBase
    : public web_app::IsolatedWebAppBrowserTestHarness {
 public:
  void SetUpOnMainThread() override {
    IsolatedWebAppBrowserTestHarness::SetUpOnMainThread();
    iwa_dev_server_ =
        CreateAndStartServer(FILE_PATH_LITERAL("web_apps/simple_isolated_app"));
    web_app::IsolatedWebAppUrlInfo url_info =
        InstallDevModeProxyIsolatedWebApp(iwa_dev_server_->GetOrigin());
    app_frame_ = OpenApp(url_info.app_id());

    chromeos::CupsWrapper::SetCupsWrapperFactoryForTesting(
        base::BindRepeating([]() -> std::unique_ptr<chromeos::CupsWrapper> {
          auto wrapper = std::make_unique<MockCupsWrapper>();
          EXPECT_CALL(*wrapper, QueryCupsPrinterStatus(_, _))
              .Times(AtMost(1))
              .WillOnce(base::test::RunOnceCallback<1>([] {
                auto status = std::make_unique<PrinterStatus>();
                status->state = IPP_PSTATE_IDLE;
                status->reasons.push_back(
                    {.reason =
                         PrinterStatus::PrinterReason::Reason::kUnknownReason,
                     .severity =
                         PrinterStatus::PrinterReason::Severity::kReport});
                status->reasons.push_back(
                    {.reason =
                         PrinterStatus::PrinterReason::Reason::kDeveloperLow,
                     .severity =
                         PrinterStatus::PrinterReason::Severity::kWarning});
                status->message = "Ready to Print!";
                return status;
              }()));
          return wrapper;
        }));

    HostContentSettingsMapFactory::GetForProfile(profile())
        ->SetDefaultContentSetting(ContentSettingsType::WEB_PRINTING,
                                   ContentSetting::CONTENT_SETTING_ALLOW);
  }

  void TearDownOnMainThread() override {
    app_frame_ = nullptr;
    iwa_dev_server_.reset();
    chromeos::CupsWrapper::SetCupsWrapperFactoryForTesting(
        base::NullCallback());
  }

 protected:
  content::RenderFrameHost* app_frame() { return app_frame_; }

 private:
  base::test::ScopedFeatureList feature_list_{blink::features::kWebPrinting};

  raw_ptr<content::RenderFrameHost> app_frame_ = nullptr;
  std::unique_ptr<net::EmbeddedTestServer> iwa_dev_server_;
};

#if BUILDFLAG(IS_CHROMEOS_ASH)
class WebPrintingBrowserTest : public WebPrintingBrowserTestBase {
 public:
  void PreRunTestOnMainThread() override {
    WebPrintingBrowserTestBase::PreRunTestOnMainThread();
    helper_->Init(profile());
  }

  void TearDownOnMainThread() override {
    helper_.reset();
    WebPrintingBrowserTestBase::TearDownOnMainThread();
  }

  void SetUpInProcessBrowserTestFixture() override {
    WebPrintingBrowserTestBase::SetUpInProcessBrowserTestFixture();
    helper_ = std::make_unique<extensions::PrintingTestHelper>();
  }

 protected:
  void AddPrinterWithSemanticCaps(
      const std::string& printer_id,
      const std::string& printer_display_name,
      std::unique_ptr<printing::PrinterSemanticCapsAndDefaults> caps) {
    helper_->AddAvailablePrinter(printer_id, printer_display_name,
                                 std::move(caps));
  }

  extensions::PrintingBackendInfrastructureHelper& printing_infra_helper() {
    return helper_->printing_infra_helper();
  }

 private:
  std::unique_ptr<extensions::PrintingTestHelper> helper_;
};
#elif BUILDFLAG(IS_CHROMEOS_LACROS)
class WebPrintingBrowserTest : public WebPrintingBrowserTestBase {
 public:
  void SetUpOnMainThread() override {
    WebPrintingBrowserTestBase::SetUpOnMainThread();
    printing_infra_helper_ =
        std::make_unique<extensions::PrintingBackendInfrastructureHelper>();
  }

  void CreatedBrowserMainParts(
      content::BrowserMainParts* browser_main_parts) override {
    WebPrintingBrowserTestBase::CreatedBrowserMainParts(browser_main_parts);
    chromeos::LacrosService::Get()->InjectRemoteForTesting(
        local_printer_receiver_.BindNewPipeAndPassRemote());

    // When WebPrintingServiceChromeOS is created, it attempts to bind the
    // observer for print jobs.
    EXPECT_CALL(local_printer(), AddPrintJobObserver(_, _, _))
        .WillOnce(WithArgs<0, 2>(
            [&](mojo::PendingRemote<crosapi::mojom::PrintJobObserver> remote,
                MockLocalPrinter::AddPrintJobObserverCallback callback) {
              observer_remote_.Bind(std::move(remote));
              std::move(callback).Run();
            }));
  }

 protected:
  NiceMock<MockLocalPrinter>& local_printer() { return local_printer_; }
  crosapi::mojom::PrintJobObserver* observer_remote() {
    return observer_remote_.get();
  }
  extensions::PrintingBackendInfrastructureHelper& printing_infra_helper() {
    return *printing_infra_helper_;
  }

 private:
  NiceMock<MockLocalPrinter> local_printer_;
  mojo::Receiver<crosapi::mojom::LocalPrinter> local_printer_receiver_{
      &local_printer_};
  mojo::Remote<crosapi::mojom::PrintJobObserver> observer_remote_;

  std::unique_ptr<extensions::PrintingBackendInfrastructureHelper>
      printing_infra_helper_;
};
#endif

IN_PROC_BROWSER_TEST_F(WebPrintingBrowserTest, GetPrinters) {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  AddPrinterWithSemanticCaps(kId, kName,
                             extensions::ConstructPrinterCapabilities());
#elif BUILDFLAG(IS_CHROMEOS_LACROS)
  EXPECT_CALL(local_printer(), GetPrinters(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          extensions::ConstructGetPrintersResponse(kId, kName)));
#endif

  constexpr std::string_view kGetPrintersScript = R"(
    (async () => {
      try {
        const printers = await navigator.printing.getPrinters();
        if (printers.length !== 1 ||
            printers[0].cachedAttributes().printerName !== $1) {
          return false;
        }
        return true;
      } catch (err) {
        console.log(err);
        return false;
      }
    })();
  )";

  ASSERT_TRUE(EvalJs(app_frame(), content::JsReplace(kGetPrintersScript, kName))
                  .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(WebPrintingBrowserTest, FetchAttributes) {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  AddPrinterWithSemanticCaps(kId, kName,
                             extensions::ConstructPrinterCapabilities());
#elif BUILDFLAG(IS_CHROMEOS_LACROS)
  EXPECT_CALL(local_printer(), GetPrinters(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          extensions::ConstructGetPrintersResponse(kId, kName)));

  EXPECT_CALL(local_printer(), GetCapability(kId, _))
      .WillOnce(base::test::RunOnceCallback<1>(
          printing::PrinterWithCapabilitiesToMojom(
              chromeos::Printer(kId),
              *extensions::ConstructPrinterCapabilities())));
#endif

  // Keep in sync with extensions::ConstructPrinterCapabilities().
  constexpr std::string_view kExpectedAttributes = R"({
    "copiesDefault": 1,
    "copiesSupported": {
      "from": 1,
      "to": 2
    },
    "documentFormatDefault": "application/pdf",
    "documentFormatSupported": [ "application/pdf" ],
    "multipleDocumentHandlingDefault": "separate-documents-uncollated-copies",
    "multipleDocumentHandlingSupported": [
      "separate-documents-uncollated-copies",
      "separate-documents-collated-copies"
    ],
    "printerResolutionDefault": {
      "crossFeedDirectionResolution": 300,
      "feedDirectionResolution": 400,
      "units": "dots-per-inch",
    },
    "printerResolutionSupported": [{
      "crossFeedDirectionResolution": 300,
      "feedDirectionResolution": 400,
      "units": "dots-per-inch",
    }],
    "printColorModeDefault": "monochrome",
    "printColorModeSupported": [ "monochrome", "color" ],
    "printerName": "name",
    "printerState": "idle",
    "printerStateMessage": "Ready to Print!",
    "printerStateReasons": [ "other", "developer-low" ],
    "sidesDefault": "one-sided",
    "sidesSupported": [ "one-sided" ]
  })";

  constexpr std::string_view kFetchAttributesScript = R"(
    (async () => {
      const printers = await navigator.printing.getPrinters();
      return await printers[0].fetchAttributes();
    })();
  )";

  auto eval_result = EvalJs(app_frame(), kFetchAttributesScript);
  ASSERT_THAT(eval_result, content::EvalJsResult::IsOk());

  const auto& attributes = eval_result.value.GetDict();
  EXPECT_THAT(attributes, base::test::DictionaryHasValues(
                              base::test::ParseJsonDict(kExpectedAttributes)));
}

IN_PROC_BROWSER_TEST_F(WebPrintingBrowserTest, Print) {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  AddPrinterWithSemanticCaps(kId, kName,
                             extensions::ConstructPrinterCapabilities());
#elif BUILDFLAG(IS_CHROMEOS_LACROS)
  InSequence s;

  EXPECT_CALL(local_printer(), GetPrinters(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          extensions::ConstructGetPrintersResponse(kId, kName)));

  EXPECT_CALL(local_printer(), GetCapability(kId, _))
      .WillOnce(base::test::RunOnceCallback<1>(
          printing::PrinterWithCapabilitiesToMojom(
              chromeos::Printer(kId),
              *extensions::ConstructPrinterCapabilities())));

  // Pretends to acknowledge the incoming Lacros print job creation request and
  // responds with PrintJobStatus::kStarted event.
  // The callback is ignored by the implementation -- for this reason the
  // invocation order doesn't really matter here (however, dropping it would
  // yield a mojo error).
  EXPECT_CALL(local_printer(), CreatePrintJob(_, _))
      .WillOnce(DoAll(
          WithArg<0>([&](const auto& job) {
            auto started_update = crosapi::mojom::PrintJobUpdate::New();
            started_update->status = crosapi::mojom::PrintJobStatus::kStarted;
            observer_remote()->OnPrintJobUpdate(kId, job->job_id,
                                                std::move(started_update));
            auto done_update = crosapi::mojom::PrintJobUpdate::New();
            done_update->status = crosapi::mojom::PrintJobStatus::kDone;
            observer_remote()->OnPrintJobUpdate(kId, job->job_id,
                                                std::move(done_update));
          }),
          base::test::RunOnceCallback<1>()));

  printing_infra_helper()
      .test_printing_context_factory()
      .SetPrinterNameForSubsequentContexts(kId);
#endif

  const auto script = content::JsReplace(kPrintScriptWithJobStatePlaceholder,
                                         /*job_state=*/"completed");
  ASSERT_THAT(EvalJs(app_frame(), script), content::EvalJsResult::IsOk());
}

IN_PROC_BROWSER_TEST_F(WebPrintingBrowserTest, PrintFailure) {
#if BUILDFLAG(IS_CHROMEOS_ASH)
  AddPrinterWithSemanticCaps(kId, kName,
                             extensions::ConstructPrinterCapabilities());
#elif BUILDFLAG(IS_CHROMEOS_LACROS)
  InSequence s;

  EXPECT_CALL(local_printer(), GetPrinters(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          extensions::ConstructGetPrintersResponse(kId, kName)));

  EXPECT_CALL(local_printer(), GetCapability(kId, _))
      .WillOnce(base::test::RunOnceCallback<1>(
          printing::PrinterWithCapabilitiesToMojom(
              chromeos::Printer(kId),
              *extensions::ConstructPrinterCapabilities())));

  printing_infra_helper()
      .test_printing_context_factory()
      .SetPrinterNameForSubsequentContexts(kId);
#endif
  printing_infra_helper()
      .test_printing_context_factory()
      .SetFailedErrorOnNewDocument(/*cause_errors=*/true);

  const auto script = content::JsReplace(kPrintScriptWithJobStatePlaceholder,
                                         /*job_state=*/"aborted");
  ASSERT_THAT(EvalJs(app_frame(), script), content::EvalJsResult::IsOk());
}

}  // namespace printing
