// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/cert_verifier.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/strings/string_util.h"
#include "base/types/optional_util.h"
#include "build/build_config.h"
#include "net/base/features.h"
#include "net/cert/caching_cert_verifier.h"
#include "net/cert/cert_verify_proc.h"
#include "net/cert/coalescing_cert_verifier.h"
#include "net/cert/crl_set.h"
#include "net/cert/do_nothing_ct_verifier.h"
#include "net/cert/multi_threaded_cert_verifier.h"
#include "net/net_buildflags.h"
#include "third_party/boringssl/src/include/openssl/pool.h"
#include "third_party/boringssl/src/include/openssl/sha.h"

namespace net {

namespace {

class DefaultCertVerifyProcFactory : public net::CertVerifyProcFactory {
 public:
  scoped_refptr<net::CertVerifyProc> CreateCertVerifyProc(
      scoped_refptr<net::CertNetFetcher> cert_net_fetcher,
      const CertVerifyProc::ImplParams& impl_params,
      const CertVerifyProc::InstanceParams& instance_params) override {
#if BUILDFLAG(CHROME_ROOT_STORE_OPTIONAL)
    if (impl_params.use_chrome_root_store) {
      return CertVerifyProc::CreateBuiltinWithChromeRootStore(
          std::move(cert_net_fetcher), impl_params.crl_set,
          std::make_unique<net::DoNothingCTVerifier>(),
          base::MakeRefCounted<DefaultCTPolicyEnforcer>(),
          base::OptionalToPtr(impl_params.root_store_data), instance_params);
    }
#endif
#if BUILDFLAG(CHROME_ROOT_STORE_ONLY)
    return CertVerifyProc::CreateBuiltinWithChromeRootStore(
        std::move(cert_net_fetcher), impl_params.crl_set,
        std::make_unique<net::DoNothingCTVerifier>(),
        base::MakeRefCounted<DefaultCTPolicyEnforcer>(),
        base::OptionalToPtr(impl_params.root_store_data), instance_params);
#elif BUILDFLAG(IS_FUCHSIA)
    return CertVerifyProc::CreateBuiltinVerifyProc(
        std::move(cert_net_fetcher), impl_params.crl_set,
        std::make_unique<net::DoNothingCTVerifier>(),
        base::MakeRefCounted<DefaultCTPolicyEnforcer>(), instance_params);
#else
    return CertVerifyProc::CreateSystemVerifyProc(std::move(cert_net_fetcher),
                                                  impl_params.crl_set);
#endif
  }

 private:
  ~DefaultCertVerifyProcFactory() override = default;
};

}  // namespace

CertVerifier::Config::Config() = default;
CertVerifier::Config::Config(const Config&) = default;
CertVerifier::Config::Config(Config&&) = default;
CertVerifier::Config::~Config() = default;
CertVerifier::Config& CertVerifier::Config::operator=(const Config&) = default;
CertVerifier::Config& CertVerifier::Config::operator=(Config&&) = default;

CertVerifier::RequestParams::RequestParams() = default;

CertVerifier::RequestParams::RequestParams(
    scoped_refptr<X509Certificate> certificate,
    std::string_view hostname,
    int flags,
    std::string_view ocsp_response,
    std::string_view sct_list)
    : certificate_(std::move(certificate)),
      hostname_(hostname),
      flags_(flags),
      ocsp_response_(ocsp_response),
      sct_list_(sct_list) {
  // For efficiency sake, rather than compare all of the fields for each
  // comparison, compute a hash of their values. This is done directly in
  // this class, rather than as an overloaded hash operator, for efficiency's
  // sake.
  SHA256_CTX ctx;
  SHA256_Init(&ctx);
  SHA256_Update(&ctx, CRYPTO_BUFFER_data(certificate_->cert_buffer()),
                CRYPTO_BUFFER_len(certificate_->cert_buffer()));
  for (const auto& cert_handle : certificate_->intermediate_buffers()) {
    SHA256_Update(&ctx, CRYPTO_BUFFER_data(cert_handle.get()),
                  CRYPTO_BUFFER_len(cert_handle.get()));
  }
  SHA256_Update(&ctx, hostname.data(), hostname.size());
  SHA256_Update(&ctx, &flags, sizeof(flags));
  SHA256_Update(&ctx, ocsp_response.data(), ocsp_response.size());
  SHA256_Update(&ctx, sct_list.data(), sct_list.size());
  SHA256_Final(reinterpret_cast<uint8_t*>(
                   base::WriteInto(&key_, SHA256_DIGEST_LENGTH + 1)),
               &ctx);
}

CertVerifier::RequestParams::RequestParams(const RequestParams& other) =
    default;
CertVerifier::RequestParams::~RequestParams() = default;

bool CertVerifier::RequestParams::operator==(
    const CertVerifier::RequestParams& other) const {
  return key_ == other.key_;
}

bool CertVerifier::RequestParams::operator<(
    const CertVerifier::RequestParams& other) const {
  return key_ < other.key_;
}

// static
std::unique_ptr<CertVerifierWithUpdatableProc>
CertVerifier::CreateDefaultWithoutCaching(
    scoped_refptr<CertNetFetcher> cert_net_fetcher) {
  auto proc_factory = base::MakeRefCounted<DefaultCertVerifyProcFactory>();
  return std::make_unique<MultiThreadedCertVerifier>(
      proc_factory->CreateCertVerifyProc(std::move(cert_net_fetcher), {}, {}),
      proc_factory);
}

// static
std::unique_ptr<CertVerifier> CertVerifier::CreateDefault(
    scoped_refptr<CertNetFetcher> cert_net_fetcher) {
  return std::make_unique<CachingCertVerifier>(
      std::make_unique<CoalescingCertVerifier>(
          CreateDefaultWithoutCaching(std::move(cert_net_fetcher))));
}

bool operator==(const CertVerifier::Config& lhs,
                const CertVerifier::Config& rhs) {
  return std::tie(
             lhs.enable_rev_checking, lhs.require_rev_checking_local_anchors,
             lhs.enable_sha1_local_anchors, lhs.disable_symantec_enforcement) ==
         std::tie(
             rhs.enable_rev_checking, rhs.require_rev_checking_local_anchors,
             rhs.enable_sha1_local_anchors, rhs.disable_symantec_enforcement);
}

bool operator!=(const CertVerifier::Config& lhs,
                const CertVerifier::Config& rhs) {
  return !(lhs == rhs);
}

}  // namespace net
