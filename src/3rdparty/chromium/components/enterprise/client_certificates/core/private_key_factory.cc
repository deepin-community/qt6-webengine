// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/enterprise/client_certificates/core/private_key_factory.h"

#include <array>

#include "base/functional/callback.h"
#include "components/enterprise/client_certificates/core/private_key.h"

namespace client_certificates {

PrivateKeyFactory::PrivateKeyFactory() = default;
PrivateKeyFactory::~PrivateKeyFactory() = default;

class PrivateKeyFactoryImpl : public PrivateKeyFactory {
 public:
  explicit PrivateKeyFactoryImpl(PrivateKeyFactoriesMap sub_factories);

  ~PrivateKeyFactoryImpl() override;

  // PrivateKeyFactory:
  void CreatePrivateKey(PrivateKeyCallback callback) override;

 private:
  PrivateKeyFactoriesMap sub_factories_;
};

PrivateKeyFactoryImpl::PrivateKeyFactoryImpl(
    PrivateKeyFactory::PrivateKeyFactoriesMap sub_factories)
    : sub_factories_(std::move(sub_factories)) {}

PrivateKeyFactoryImpl::~PrivateKeyFactoryImpl() = default;

void PrivateKeyFactoryImpl::CreatePrivateKey(
    PrivateKeyFactory::PrivateKeyCallback callback) {
  // Go through the supported key sources in order of most secure to least, and
  // delegate the key creation to that sub factory.
  if (sub_factories_.contains(PrivateKeySource::kUnexportableKey)) {
    sub_factories_[PrivateKeySource::kUnexportableKey]->CreatePrivateKey(
        std::move(callback));
    return;
  }

  if (sub_factories_.contains(PrivateKeySource::kSoftwareKey)) {
    sub_factories_[PrivateKeySource::kSoftwareKey]->CreatePrivateKey(
        std::move(callback));
    return;
  }

  std::move(callback).Run(nullptr);
}

// static
std::unique_ptr<PrivateKeyFactory> PrivateKeyFactory::Create(
    PrivateKeyFactoriesMap sub_factories) {
  return std::make_unique<PrivateKeyFactoryImpl>(std::move(sub_factories));
}

}  // namespace client_certificates
