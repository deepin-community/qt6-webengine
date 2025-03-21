// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/enterprise/client_certificates/core/unexportable_private_key.h"

#include <array>

#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "components/enterprise/client_certificates/core/private_key.h"
#include "crypto/scoped_mock_unexportable_key_provider.h"
#include "crypto/unexportable_key.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace client_certificates {

TEST(UnexportablePrivateKeyTest, SupportedCreateKey) {
  crypto::ScopedMockUnexportableKeyProvider scoped_provider;
  auto provider = crypto::GetUnexportableKeyProvider();
  ASSERT_TRUE(provider);

  // The mock only works with the ECDSA_SHA256 algorithm.
  std::array<crypto::SignatureVerifier::SignatureAlgorithm, 1>
      kAcceptableAlgorithms = {crypto::SignatureVerifier::ECDSA_SHA256};
  auto unexportable_key =
      provider->GenerateSigningKeySlowly(kAcceptableAlgorithms);
  ASSERT_TRUE(unexportable_key);

  auto private_key =
      base::MakeRefCounted<UnexportablePrivateKey>(std::move(unexportable_key));

  auto spki_bytes = private_key->GetSubjectPublicKeyInfo();
  EXPECT_GT(spki_bytes.size(), 0U);
  EXPECT_EQ(private_key->GetAlgorithm(),
            crypto::SignatureVerifier::ECDSA_SHA256);
  EXPECT_TRUE(private_key->SignSlowly(spki_bytes).has_value());
}

}  // namespace client_certificates
