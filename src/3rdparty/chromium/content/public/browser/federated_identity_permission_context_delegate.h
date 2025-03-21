// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_BROWSER_FEDERATED_IDENTITY_PERMISSION_CONTEXT_DELEGATE_H_
#define CONTENT_PUBLIC_BROWSER_FEDERATED_IDENTITY_PERMISSION_CONTEXT_DELEGATE_H_

#include <vector>

#include "base/observer_list.h"
#include "url/origin.h"

namespace content {

// Delegate interface for the FedCM implementation in content to query and
// manage permission grants associated with the ability to share identity
// information from a given provider to a given relying party.
class FederatedIdentityPermissionContextDelegate {
 public:
  // Observes IdP sign-in status changes.
  class IdpSigninStatusObserver : public base::CheckedObserver {
   public:
    // Called every time we receive a signed-in status (so we can refresh
    // the account list if a new account is now signed in) and also when
    // the status changes from signed-in to signed-out.
    virtual void OnIdpSigninStatusReceived(const url::Origin& idp_origin,
                                           bool idp_signin_status) = 0;

   protected:
    IdpSigninStatusObserver() = default;
    ~IdpSigninStatusObserver() override = default;
  };

  FederatedIdentityPermissionContextDelegate() = default;
  virtual ~FederatedIdentityPermissionContextDelegate() = default;

  // Adds/removes observer for IdP sign-in status.
  virtual void AddIdpSigninStatusObserver(
      IdpSigninStatusObserver* observer) = 0;
  virtual void RemoveIdpSigninStatusObserver(
      IdpSigninStatusObserver* observer) = 0;

  // Determine whether there is an existing permission grant to share identity
  // information for the given account to the `relying_party_requester` when
  // embedded in `relying_party_embedder`. `account_id` can be omitted to
  // represent "sharing permission for any account".
  virtual bool HasSharingPermission(
      const url::Origin& relying_party_requester,
      const url::Origin& relying_party_embedder,
      const url::Origin& identity_provider,
      const std::optional<std::string>& account_id) = 0;

  // Determine whether there is an existing permission grant to share identity
  // information for any account to the `relying_party_requester`.
  virtual bool HasSharingPermission(
      const url::Origin& relying_party_requester) = 0;

  // Grants permission to share identity information for the given account to
  // `relying_party_requester` when embedded in `relying_party_embedder`.
  virtual void GrantSharingPermission(
      const url::Origin& relying_party_requester,
      const url::Origin& relying_party_embedder,
      const url::Origin& identity_provider,
      const std::string& account_id) = 0;

  // Revokes a previously granted sharing permission. If there is no sharing
  // permission associated with the given `account_id`, an arbitrary sharing
  // permission is revoked.
  virtual void RevokeSharingPermission(
      const url::Origin& relying_party_requester,
      const url::Origin& relying_party_embedder,
      const url::Origin& identity_provider,
      const std::string& account_id) = 0;

  // Returns whether the user is signed in with the IDP. If unknown, return
  // std::nullopt.
  virtual std::optional<bool> GetIdpSigninStatus(
      const url::Origin& idp_origin) = 0;

  // Updates the IDP sign-in status. This could be called by
  //   1. IdpSigninStatus API
  //   2. fetching accounts response callback
  virtual void SetIdpSigninStatus(const url::Origin& idp_origin,
                                  bool idp_signin_status) = 0;

  // Returns all origins that are registered as IDP.
  virtual std::vector<GURL> GetRegisteredIdPs() = 0;

  // Registers an IdP.
  virtual void RegisterIdP(const GURL& url) = 0;

  // Unregisters an IdP.
  virtual void UnregisterIdP(const GURL& url) = 0;
};

}  // namespace content

#endif  // CONTENT_PUBLIC_BROWSER_FEDERATED_IDENTITY_PERMISSION_CONTEXT_DELEGATE_H_
