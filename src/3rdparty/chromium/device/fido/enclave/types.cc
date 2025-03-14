// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/fido/enclave/types.h"

#include "components/sync/protocol/webauthn_credential_specifics.pb.h"

namespace device::enclave {
EnclaveIdentity::EnclaveIdentity() = default;
EnclaveIdentity::~EnclaveIdentity() = default;
EnclaveIdentity::EnclaveIdentity(const EnclaveIdentity&) = default;
ClientSignature::ClientSignature() = default;
ClientSignature::~ClientSignature() = default;
ClientSignature::ClientSignature(const ClientSignature&) = default;
ClientSignature::ClientSignature(ClientSignature&&) = default;
CredentialRequest::CredentialRequest() = default;
CredentialRequest::~CredentialRequest() = default;
CredentialRequest::CredentialRequest(CredentialRequest&&) = default;
}  // namespace device::enclave
