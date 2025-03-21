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

use tinyvec::ArrayVec;

use crate::elliptic_curve::{Curve, EcdhProvider, PublicKey};
use core::fmt::Debug;

/// Marker type for P256 implementation. This is used by EcdhProvider as its type parameter.
#[derive(Debug, PartialEq, Eq)]
pub enum P256 {}
impl Curve for P256 {}

/// Longest length for a sec-1 encoded P256 public key, which is the uncompressed format
/// `04 || X || Y` as defined in section 2.3.3 of the SECG SEC 1 ("Elliptic Curve Cryptography")
/// standard.
const P256_PUBLIC_KEY_MAX_LENGTH: usize = 65;

/// Whether an elliptic curve point should be compressed or not.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PointCompression {
    /// The elliptic curve point should be compressed (`02 || X` or `03 || X`),
    /// as defined in section 2.3.3 of the SECG SEC 1 ("Elliptic Curve
    /// Cryptography").
    Compressed,
    /// The elliptic curve point should be uncompressed (`04 || X || Y`), as
    /// defined in section 2.3.3 of the SECG SEC 1 ("Elliptic Curve
    /// Cryptography").
    Uncompressed,
}

/// Trait for a NIST-P256 public key.
pub trait P256PublicKey: Sized + PartialEq + Debug {
    /// The error type associated with this implementation.
    type Error: Debug;

    /// Creates a public key from the given sec1-encoded bytes, as described in section 2.3.4 of
    /// the SECG SEC 1 ("Elliptic Curve Cryptography") standard.
    fn from_sec1_bytes(bytes: &[u8]) -> Result<Self, Self::Error>;

    /// Serializes this key into sec1-encoded bytes, as described in section 2.3.3 of the SECG SEC 1
    /// ("Elliptic Curve Cryptography") standard. Note that it is not necessarily true that
    /// `from_sec1_bytes(bytes)?.to_sec1_bytes() == bytes` because of point compression. (But it is
    /// always true that `from_sec1_bytes(key.to_sec1_bytes())? == key`).
    fn to_sec1_bytes(
        &self,
        point_compression: PointCompression,
    ) -> ArrayVec<[u8; P256_PUBLIC_KEY_MAX_LENGTH]>;

    /// Converts this public key's x and y coordinates on the elliptic curve to big endian octet
    /// strings.
    fn to_affine_coordinates(&self) -> Result<([u8; 32], [u8; 32]), Self::Error>;

    /// Creates a public key from the X and Y coordinates on the elliptic curve.
    fn from_affine_coordinates(x: &[u8; 32], y: &[u8; 32]) -> Result<Self, Self::Error>;
}

impl<P: P256PublicKey> PublicKey<P256> for P {
    type Error = <Self as P256PublicKey>::Error;
    type EncodedPublicKey = ArrayVec<[u8; P256_PUBLIC_KEY_MAX_LENGTH]>;

    fn from_bytes(bytes: &[u8]) -> Result<Self, Self::Error> {
        Self::from_sec1_bytes(bytes)
    }

    fn to_bytes(&self) -> Self::EncodedPublicKey {
        Self::to_sec1_bytes(self, PointCompression::Uncompressed)
    }
}

/// Equivalent to EcdhProvider<P256, PublicKey: P256PublicKey> if associated type bounds are
/// supported.
pub trait P256EcdhProvider:
    EcdhProvider<P256, PublicKey = <Self as P256EcdhProvider>::PublicKey>
{
    /// Same as EcdhProvider::PublicKey.
    type PublicKey: P256PublicKey;
}

impl<E> P256EcdhProvider for E
where
    E: EcdhProvider<P256>,
    E::PublicKey: P256PublicKey,
{
    type PublicKey = E::PublicKey;
}
