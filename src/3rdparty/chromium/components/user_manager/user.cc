// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/user_manager/user.h"

#include <stddef.h>

#include <memory>

#include "base/functional/callback.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "components/account_id/account_id.h"
#include "components/user_manager/user_manager.h"
#include "google_apis/gaia/gaia_auth_util.h"

namespace user_manager {

namespace {

// Must be in sync with histogram enum UserTypeChanged in enums.xml.
// The values must never be changed (only new ones can be added) as they
// are stored in UMA logs.
enum class UserTypeChangeHistogram {
  UNKNOWN_FATAL = 0,
  REGULAR_TO_CHILD = 1,
  CHILD_TO_REGULAR = 2,
  COUNT,  // Not a value, just a count of other values.
};
void UMAUserTypeChanged(const UserTypeChangeHistogram value) {
  UMA_HISTOGRAM_ENUMERATION("UserManager.UserTypeChanged", value,
                            UserTypeChangeHistogram::COUNT);
}

// Returns account name portion of an email.
std::string GetUserName(const std::string& email) {
  std::string::size_type i = email.find('@');
  if (i == 0 || i == std::string::npos) {
    return email;
  }
  return email.substr(0, i);
}

}  // namespace

// static
bool User::TypeHasGaiaAccount(UserType user_type) {
  return user_type == USER_TYPE_REGULAR ||
         user_type == USER_TYPE_CHILD;
}

// static
bool User::TypeIsKiosk(UserType type) {
  return type == USER_TYPE_KIOSK_APP || type == USER_TYPE_ARC_KIOSK_APP ||
         type == USER_TYPE_WEB_KIOSK_APP;
}

User::User(const AccountId& account_id, UserType type)
    : account_id_(account_id), type_(type), user_image_(new UserImage()) {
  switch (type_) {
    case user_manager::USER_TYPE_REGULAR:
    case user_manager::USER_TYPE_CHILD:
    case user_manager::USER_TYPE_KIOSK_APP:
    case user_manager::USER_TYPE_ARC_KIOSK_APP:
    case user_manager::USER_TYPE_WEB_KIOSK_APP:
      set_display_email(account_id.GetUserEmail());
      break;
    case user_manager::USER_TYPE_GUEST:
    case user_manager::USER_TYPE_PUBLIC_ACCOUNT:
      // Public accounts nor guest account do not have a real email address,
      // so they do not set |display_email_|.
      break;
  }

  if (type_ == user_manager::USER_TYPE_REGULAR ||
      type_ == user_manager::USER_TYPE_CHILD) {
    set_can_lock(true);
  }
}

User::~User() = default;

std::string User::GetDisplayEmail() const {
  return display_email();
}

std::u16string User::GetDisplayName() const {
  // Fallback to the email account name in case display name haven't been set.
  return display_name_.empty() ? base::UTF8ToUTF16(GetAccountName(true))
                               : display_name_;
}

std::u16string User::GetGivenName() const {
  return given_name_;
}

const gfx::ImageSkia& User::GetImage() const {
  return user_image_->image();
}

const AccountId& User::GetAccountId() const {
  return account_id_;
}

void User::UpdateType(UserType new_type) {
  // Can only change between regular and child.
  if ((type_ == user_manager::USER_TYPE_CHILD ||
       type_ == user_manager::USER_TYPE_REGULAR) &&
      (new_type == user_manager::USER_TYPE_CHILD ||
       new_type == user_manager::USER_TYPE_REGULAR)) {
    // We want all the other type changes to crash, that is why this check is
    // not at the top level.
    if (type_ == new_type) {
      return;
    }

    LOG(WARNING) << "User type has changed: " << type_ << " -> " << new_type;
    type_ = new_type;

    UMAUserTypeChanged(new_type == user_manager::USER_TYPE_CHILD
                           ? UserTypeChangeHistogram::REGULAR_TO_CHILD
                           : UserTypeChangeHistogram::CHILD_TO_REGULAR);
    return;
  }

  UMAUserTypeChanged(UserTypeChangeHistogram::UNKNOWN_FATAL);
  LOG(FATAL) << "Unsupported user type change " << type_ << "=>" << new_type;
}

bool User::HasGaiaAccount() const {
  return TypeHasGaiaAccount(GetType());
}

bool User::IsActiveDirectoryUser() const {
  return false;
}

bool User::IsChild() const {
  return GetType() == USER_TYPE_CHILD;
}

std::string User::GetAccountName(bool use_display_email) const {
  if (use_display_email && !display_email_.empty())
    return GetUserName(display_email_);
  else
    return GetUserName(account_id_.GetUserEmail());
}

bool User::HasDefaultImage() const {
  return UserManager::Get()->IsValidDefaultUserImageId(image_index_);
}

std::string User::display_email() const {
  return display_email_;
}

bool User::can_lock() const {
  return can_lock_;
}

const std::string& User::username_hash() const {
  return username_hash_;
}

bool User::is_logged_in() const {
  return is_logged_in_;
}

bool User::is_active() const {
  return is_active_;
}

bool User::has_gaia_account() const {
  static_assert(static_cast<int>(user_manager::UserType::kMaxValue) == 9,
                "kMaxValue should equal 9");
  switch (GetType()) {
    case user_manager::USER_TYPE_REGULAR:
    case user_manager::USER_TYPE_CHILD:
      return true;
    case user_manager::USER_TYPE_GUEST:
    case user_manager::USER_TYPE_PUBLIC_ACCOUNT:
    case user_manager::USER_TYPE_KIOSK_APP:
    case user_manager::USER_TYPE_ARC_KIOSK_APP:
    case user_manager::USER_TYPE_WEB_KIOSK_APP:
      return false;
  }
  return false;
}

void User::AddProfileCreatedObserver(base::OnceClosure on_profile_created) {
  if (profile_is_created_)
    std::move(on_profile_created).Run();
  else
    on_profile_created_observers_.push_back(std::move(on_profile_created));
}

void User::SetProfileIsCreated() {
  profile_is_created_ = true;
  for (auto& callback : on_profile_created_observers_) {
    std::move(callback).Run();
  }
  on_profile_created_observers_.clear();
}

bool User::IsAffiliated() const {
  // Device local accounts are always affiliated.
  if (IsDeviceLocalAccount()) {
    return true;
  }

  return is_affiliated_.value_or(false);
}

void User::IsAffiliatedAsync(
    base::OnceCallback<void(bool)> is_affiliated_callback) {
  // TODO(b/278643115): Conceptually, we should call
  //   std::move(is_affiliated_callback).Run(true)
  // here immediately if this is for device local account.

  if (is_affiliated_.has_value()) {
    std::move(is_affiliated_callback).Run(is_affiliated_.value());
  } else {
    on_affiliation_set_callbacks_.push_back(std::move(is_affiliated_callback));
  }
}

void User::SetAffiliation(bool is_affiliated) {
  // Device local accounts are always affiliated. No affiliation
  // modification must happen.
  CHECK(!IsDeviceLocalAccount());

  is_affiliated_ = is_affiliated;
  for (auto& callback : on_affiliation_set_callbacks_) {
    std::move(callback).Run(is_affiliated_.value());
  }
  on_affiliation_set_callbacks_.clear();
}

bool User::IsDeviceLocalAccount() const {
  switch (type_) {
    case user_manager::USER_TYPE_REGULAR:
    case user_manager::USER_TYPE_CHILD:
    case user_manager::USER_TYPE_GUEST:
      return false;
    case user_manager::USER_TYPE_PUBLIC_ACCOUNT:
    case user_manager::USER_TYPE_KIOSK_APP:
    case user_manager::USER_TYPE_ARC_KIOSK_APP:
    case user_manager::USER_TYPE_WEB_KIOSK_APP:
      return true;
  }
  return false;
}

bool User::IsKioskType() const {
  return TypeIsKiosk(GetType());
}

User* User::CreateRegularUser(const AccountId& account_id,
                              const UserType type) {
  CHECK(type == USER_TYPE_REGULAR || type == USER_TYPE_CHILD)
      << "Invalid user type " << type;

  return new User(account_id, type);
}

User* User::CreateGuestUser(const AccountId& guest_account_id) {
  return new User(guest_account_id, USER_TYPE_GUEST);
}

User* User::CreateKioskAppUser(const AccountId& kiosk_app_account_id) {
  return new User(kiosk_app_account_id, USER_TYPE_KIOSK_APP);
}

User* User::CreateArcKioskAppUser(const AccountId& arc_kiosk_account_id) {
  return new User(arc_kiosk_account_id, USER_TYPE_ARC_KIOSK_APP);
}

User* User::CreateWebKioskAppUser(const AccountId& web_kiosk_account_id) {
  return new User(web_kiosk_account_id, USER_TYPE_WEB_KIOSK_APP);
}

User* User::CreatePublicAccountUser(const AccountId& account_id,
                                    bool is_using_saml) {
  User* user = new User(account_id, USER_TYPE_PUBLIC_ACCOUNT);
  user->set_using_saml(is_using_saml);
  return user;
}

void User::SetAccountLocale(const std::string& resolved_account_locale) {
  account_locale_ = std::make_unique<std::string>(resolved_account_locale);
}

void User::SetImage(std::unique_ptr<UserImage> user_image, int image_index) {
  user_image_ = std::move(user_image);
  image_index_ = image_index;
  image_is_stub_ = false;
  image_is_loading_ = false;
  DCHECK(HasDefaultImage() || user_image_->has_image_bytes());
}

void User::SetImageURL(const GURL& image_url) {
  user_image_->set_url(image_url);
}

void User::SetStubImage(std::unique_ptr<UserImage> stub_user_image,
                        int image_index,
                        bool is_loading) {
  user_image_ = std::move(stub_user_image);
  image_index_ = image_index;
  image_is_stub_ = true;
  image_is_loading_ = is_loading;
}

}  // namespace user_manager
