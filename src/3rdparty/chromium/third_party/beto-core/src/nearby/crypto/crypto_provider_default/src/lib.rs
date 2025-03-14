// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! Provides multiple implementations of CryptoProvider through the same struct, configurable by
//! feature flag.

#![no_std]

cfg_if::cfg_if! {
    if #[cfg(feature = "rustcrypto")] {
        pub use crypto_provider_rustcrypto::RustCrypto as CryptoProviderImpl;
    } else if #[cfg(feature = "boringssl")] {
        pub use crypto_provider_boringssl::Boringssl as CryptoProviderImpl;
    } else if #[cfg(any(feature = "openssl", feature = "opensslbssl"))] {
        pub use crypto_provider_openssl::Openssl as CryptoProviderImpl;
    } else {
        compile_error!("No crypto_provider feature enabled!");
    }
}
