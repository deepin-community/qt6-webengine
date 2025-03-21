// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/commerce/core/webui/shopping_list_handler.h"

#include <memory>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/commerce/core/commerce_constants.h"
#include "components/commerce/core/commerce_feature_list.h"
#include "components/commerce/core/price_tracking_utils.h"
#include "components/commerce/core/shopping_service.h"
#include "components/commerce/core/subscriptions/commerce_subscription.h"
#include "components/commerce/core/webui/webui_utils.h"
#include "components/feature_engagement/public/tracker.h"
#include "components/payments/core/currency_formatter.h"
#include "components/power_bookmarks/core/power_bookmark_utils.h"
#include "components/power_bookmarks/core/proto/power_bookmark_meta.pb.h"
#include "components/power_bookmarks/core/proto/shopping_specifics.pb.h"
#include "components/prefs/pref_service.h"
#include "components/url_formatter/elide_url.h"
#include "url/gurl.h"

namespace commerce {
namespace {

shopping_list::mojom::BookmarkProductInfoPtr BookmarkNodeToMojoProduct(
    bookmarks::BookmarkModel& model,
    const bookmarks::BookmarkNode* node,
    const std::string& locale) {
  auto bookmark_info = shopping_list::mojom::BookmarkProductInfo::New();
  bookmark_info->bookmark_id = node->id();

  std::unique_ptr<power_bookmarks::PowerBookmarkMeta> meta =
      power_bookmarks::GetNodePowerBookmarkMeta(&model, node);
  const power_bookmarks::ShoppingSpecifics specifics =
      meta->shopping_specifics();

  bookmark_info->info = shopping_list::mojom::ProductInfo::New();
  bookmark_info->info->title = specifics.title();
  bookmark_info->info->domain = base::UTF16ToUTF8(
      url_formatter::FormatUrlForDisplayOmitSchemePathAndTrivialSubdomains(
          GURL(node->url())));

  bookmark_info->info->product_url = node->url();
  bookmark_info->info->image_url = GURL(meta->lead_image().url());
  bookmark_info->info->cluster_id = specifics.product_cluster_id();

  const power_bookmarks::ProductPrice price = specifics.current_price();
  std::string currency_code = price.currency_code();

  std::unique_ptr<payments::CurrencyFormatter> formatter =
      std::make_unique<payments::CurrencyFormatter>(currency_code, locale);
  formatter->SetMaxFractionalDigits(2);

  bookmark_info->info->current_price =
      base::UTF16ToUTF8(formatter->Format(base::NumberToString(
          static_cast<float>(price.amount_micros()) / kToMicroCurrency)));

  // Only send the previous price if it is higher than the current price. This
  // is exclusively used to decide whether to show the price drop chip in the
  // UI.
  if (specifics.has_previous_price() &&
      specifics.previous_price().amount_micros() >
          specifics.current_price().amount_micros()) {
    const power_bookmarks::ProductPrice previous_price =
        specifics.previous_price();
    bookmark_info->info->previous_price =
        base::UTF16ToUTF8(formatter->Format(base::NumberToString(
            static_cast<float>(previous_price.amount_micros()) /
            kToMicroCurrency)));
  }

  return bookmark_info;
}

shopping_list::mojom::PriceInsightsInfoPtr PriceInsightsInfoToMojoObject(
    const absl::optional<PriceInsightsInfo>& info,
    const std::string& locale) {
  auto insights_info = shopping_list::mojom::PriceInsightsInfo::New();

  if (!info.has_value()) {
    return insights_info;
  }

  insights_info->cluster_id = info->product_cluster_id.value();

  std::unique_ptr<payments::CurrencyFormatter> formatter =
      std::make_unique<payments::CurrencyFormatter>(info->currency_code,
                                                    locale);
  formatter->SetMaxFractionalDigits(2);

  if (info->typical_low_price_micros.has_value() &&
      info->typical_high_price_micros.has_value()) {
    insights_info->typical_low_price =
        base::UTF16ToUTF8(formatter->Format(base::NumberToString(
            static_cast<float>(info->typical_low_price_micros.value()) /
            kToMicroCurrency)));

    insights_info->typical_high_price =
        base::UTF16ToUTF8(formatter->Format(base::NumberToString(
            static_cast<float>(info->typical_high_price_micros.value()) /
            kToMicroCurrency)));
  }

  if (info->catalog_attributes.has_value()) {
    insights_info->catalog_attributes = info->catalog_attributes.value();
  }

  if (info->jackpot_url.has_value()) {
    insights_info->jackpot = info->jackpot_url.value();
  }

  shopping_list::mojom::PriceInsightsInfo::PriceBucket bucket;
  switch (info->price_bucket) {
    case PriceBucket::kUnknown:
      bucket = shopping_list::mojom::PriceInsightsInfo::PriceBucket::kUnknown;
      break;
    case PriceBucket::kLowPrice:
      bucket = shopping_list::mojom::PriceInsightsInfo::PriceBucket::kLow;
      break;
    case PriceBucket::kTypicalPrice:
      bucket = shopping_list::mojom::PriceInsightsInfo::PriceBucket::kTypical;
      break;
    case PriceBucket::kHighPrice:
      bucket = shopping_list::mojom::PriceInsightsInfo::PriceBucket::kHigh;
      break;
  }
  insights_info->bucket = bucket;

  insights_info->has_multiple_catalogs = info->has_multiple_catalogs;

  for (auto history_price : info->catalog_history_prices) {
    auto point = shopping_list::mojom::PricePoint::New();
    point->date = std::get<0>(history_price);

    auto price =
        static_cast<float>(std::get<1>(history_price)) / kToMicroCurrency;
    point->price = price;
    point->formatted_price =
        base::UTF16ToUTF8(formatter->Format(base::NumberToString(price)));
    insights_info->history.push_back(std::move(point));
  }

  insights_info->locale = locale;
  insights_info->currency_code = info->currency_code;

  return insights_info;
}

}  // namespace

using shopping_list::mojom::BookmarkProductInfo;
using shopping_list::mojom::BookmarkProductInfoPtr;

ShoppingListHandler::ShoppingListHandler(
    mojo::PendingRemote<shopping_list::mojom::Page> remote_page,
    mojo::PendingReceiver<shopping_list::mojom::ShoppingListHandler> receiver,
    bookmarks::BookmarkModel* bookmark_model,
    ShoppingService* shopping_service,
    PrefService* prefs,
    feature_engagement::Tracker* tracker,
    const std::string& locale,
    std::unique_ptr<Delegate> delegate)
    : remote_page_(std::move(remote_page)),
      receiver_(this, std::move(receiver)),
      bookmark_model_(bookmark_model),
      shopping_service_(shopping_service),
      pref_service_(prefs),
      tracker_(tracker),
      locale_(locale),
      delegate_(std::move(delegate)) {
  scoped_subscriptions_observation_.Observe(shopping_service_);
  scoped_bookmark_model_observation_.Observe(bookmark_model_);
  // It is safe to schedule updates and observe bookmarks. If the feature is
  // disabled, no new information will be fetched or provided to the frontend.
  shopping_service_->ScheduleSavedProductUpdate();
}

ShoppingListHandler::~ShoppingListHandler() = default;

void ShoppingListHandler::GetAllPriceTrackedBookmarkProductInfo(
    GetAllPriceTrackedBookmarkProductInfoCallback callback) {
  shopping_service_->WaitForReady(base::BindOnce(
      [](base::WeakPtr<ShoppingListHandler> handler,
         GetAllPriceTrackedBookmarkProductInfoCallback callback,
         ShoppingService* service) {
        if (!service || !service->IsShoppingListEligible() ||
            handler.WasInvalidated()) {
          base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE, base::BindOnce(std::move(callback),
                                        std::vector<BookmarkProductInfoPtr>()));
          return;
        }

        service->GetAllPriceTrackedBookmarks(
            base::BindOnce(&ShoppingListHandler::OnFetchPriceTrackedBookmarks,
                           handler, std::move(callback)));
      },
      weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void ShoppingListHandler::OnFetchPriceTrackedBookmarks(
    GetAllPriceTrackedBookmarkProductInfoCallback callback,
    std::vector<const bookmarks::BookmarkNode*> bookmarks) {
  std::vector<BookmarkProductInfoPtr> info_list =
      BookmarkListToMojoList(*bookmark_model_, bookmarks, locale_);

  if (!info_list.empty()) {
    // Record usage for price tracking promo.
    tracker_->NotifyEvent("price_tracking_side_panel_shown");
  }

  std::move(callback).Run(std::move(info_list));
}

void ShoppingListHandler::GetAllShoppingBookmarkProductInfo(
    GetAllShoppingBookmarkProductInfoCallback callback) {
  shopping_service_->WaitForReady(base::BindOnce(
      [](base::WeakPtr<ShoppingListHandler> handler,
         GetAllShoppingBookmarkProductInfoCallback callback,
         ShoppingService* service) {
        if (!service || !service->IsShoppingListEligible() ||
            handler.WasInvalidated()) {
          std::move(callback).Run({});
          return;
        }

        std::vector<const bookmarks::BookmarkNode*> bookmarks =
            service->GetAllShoppingBookmarks();

        std::vector<BookmarkProductInfoPtr> info_list = BookmarkListToMojoList(
            *(handler->bookmark_model_), bookmarks, handler->locale_);

        std::move(callback).Run(std::move(info_list));
      },
      weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void ShoppingListHandler::TrackPriceForBookmark(int64_t bookmark_id) {
  commerce::SetPriceTrackingStateForBookmark(
      shopping_service_, bookmark_model_,
      bookmarks::GetBookmarkNodeByID(bookmark_model_, bookmark_id), true,
      base::BindOnce(&ShoppingListHandler::onPriceTrackResult,
                     weak_ptr_factory_.GetWeakPtr(), bookmark_id,
                     bookmark_model_, true));
}

void ShoppingListHandler::UntrackPriceForBookmark(int64_t bookmark_id) {
  commerce::SetPriceTrackingStateForBookmark(
      shopping_service_, bookmark_model_,
      bookmarks::GetBookmarkNodeByID(bookmark_model_, bookmark_id), false,
      base::BindOnce(&ShoppingListHandler::onPriceTrackResult,
                     weak_ptr_factory_.GetWeakPtr(), bookmark_id,
                     bookmark_model_, false));
}

void ShoppingListHandler::OnSubscribe(const CommerceSubscription& subscription,
                                      bool succeeded) {
  if (succeeded) {
    HandleSubscriptionChange(subscription, true);
  }
}

void ShoppingListHandler::OnUnsubscribe(
    const CommerceSubscription& subscription,
    bool succeeded) {
  if (succeeded) {
    HandleSubscriptionChange(subscription, false);
  }
}

void ShoppingListHandler::BookmarkModelChanged() {}

void ShoppingListHandler::BookmarkNodeMoved(
    bookmarks::BookmarkModel* model,
    const bookmarks::BookmarkNode* old_parent,
    size_t old_index,
    const bookmarks::BookmarkNode* new_parent,
    size_t new_index) {
  const bookmarks::BookmarkNode* node = new_parent->children()[new_index].get();
  if (!node) {
    return;
  }
  std::unique_ptr<power_bookmarks::PowerBookmarkMeta> meta =
      power_bookmarks::GetNodePowerBookmarkMeta(bookmark_model_, node);
  if (!meta || !meta->has_shopping_specifics() ||
      !meta->shopping_specifics().has_product_cluster_id()) {
    return;
  }
  remote_page_->OnProductBookmarkMoved(
      BookmarkNodeToMojoProduct(*bookmark_model_, node, locale_));
}

void ShoppingListHandler::HandleSubscriptionChange(
    const CommerceSubscription& sub,
    bool is_tracking) {
  if (sub.id_type != IdentifierType::kProductClusterId) {
    return;
  }

  uint64_t cluster_id;
  if (!base::StringToUint64(sub.id, &cluster_id)) {
    return;
  }

  std::vector<const bookmarks::BookmarkNode*> bookmarks =
      GetBookmarksWithClusterId(bookmark_model_, cluster_id);
  // Special handling when the unsubscription is caused by bookmark deletion and
  // therefore the bookmark can no longer be retrieved.
  // TODO(crbug.com/1462668): Update mojo call to pass cluster ID and make
  // BookmarkProductInfo a nullable parameter.
  if (!bookmarks.size()) {
    auto bookmark_info = shopping_list::mojom::BookmarkProductInfo::New();
    bookmark_info->info = shopping_list::mojom::ProductInfo::New();
    bookmark_info->info->cluster_id = cluster_id;
    remote_page_->PriceUntrackedForBookmark(std::move(bookmark_info));
    return;
  }
  for (auto* node : bookmarks) {
    auto product = BookmarkNodeToMojoProduct(*bookmark_model_, node, locale_);
    if (is_tracking) {
      remote_page_->PriceTrackedForBookmark(std::move(product));
    } else {
      remote_page_->PriceUntrackedForBookmark(std::move(product));
    }
  }
}

std::vector<BookmarkProductInfoPtr> ShoppingListHandler::BookmarkListToMojoList(
    bookmarks::BookmarkModel& model,
    const std::vector<const bookmarks::BookmarkNode*>& bookmarks,
    const std::string& locale) {
  std::vector<BookmarkProductInfoPtr> info_list;

  for (const bookmarks::BookmarkNode* node : bookmarks) {
    info_list.push_back(BookmarkNodeToMojoProduct(model, node, locale));
  }

  return info_list;
}

void ShoppingListHandler::onPriceTrackResult(int64_t bookmark_id,
                                             bookmarks::BookmarkModel* model,
                                             bool is_tracking,
                                             bool success) {
  if (success)
    return;

  // We only do work here if price tracking failed. When the UI is interacted
  // with, we assume success. In the event it failed, we switch things back.
  // So in this case, if we were trying to untrack and that action failed, set
  // the UI back to "tracking".
  auto* node = bookmarks::GetBookmarkNodeByID(bookmark_model_, bookmark_id);
  auto product = BookmarkNodeToMojoProduct(*bookmark_model_, node, locale_);

  if (!is_tracking) {
    remote_page_->PriceTrackedForBookmark(std::move(product));
  } else {
    remote_page_->PriceUntrackedForBookmark(std::move(product));
  }
  // Pass in whether the failed operation was to track or untrack price. It
  // should be the reverse of the current tracking status since the operation
  // failed.
  remote_page_->OperationFailedForBookmark(
      BookmarkNodeToMojoProduct(*bookmark_model_, node, locale_), is_tracking);
}

void ShoppingListHandler::GetProductInfoForCurrentUrl(
    GetProductInfoForCurrentUrlCallback callback) {
  if (!shopping_service_->IsPriceInsightsEligible() || !delegate_ ||
      !delegate_->GetCurrentTabUrl().has_value()) {
    std::move(callback).Run(shopping_list::mojom::ProductInfo::New());
    return;
  }

  shopping_service_->GetProductInfoForUrl(
      delegate_->GetCurrentTabUrl().value(),
      base::BindOnce(&ShoppingListHandler::OnFetchProductInfoForCurrentUrl,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void ShoppingListHandler::IsShoppingListEligible(
    IsShoppingListEligibleCallback callback) {
  std::move(callback).Run(shopping_service_->IsShoppingListEligible());
}

void ShoppingListHandler::GetShoppingCollectionBookmarkFolderId(
    GetShoppingCollectionBookmarkFolderIdCallback callback) {
  const bookmarks::BookmarkNode* collection =
      commerce::GetShoppingCollectionBookmarkFolder(bookmark_model_);
  std::move(callback).Run(collection ? collection->id() : -1);
}

void ShoppingListHandler::GetPriceTrackingStatusForCurrentUrl(
    GetPriceTrackingStatusForCurrentUrlCallback callback) {
  // The URL may or may not have a bookmark associated with it. Prioritize
  // accessing the product info for the URL before looking at an existing
  // bookmark.
  shopping_service_->GetProductInfoForUrl(
      delegate_->GetCurrentTabUrl().value(),
      base::BindOnce(
          [](base::WeakPtr<ShoppingListHandler> handler,
             GetPriceTrackingStatusForCurrentUrlCallback callback,
             const GURL& url, const absl::optional<const ProductInfo>& info) {
            if (!info.has_value() || !info->product_cluster_id.has_value() ||
                !handler || !CanTrackPrice(info)) {
              std::move(callback).Run(false);
              return;
            }
            CommerceSubscription sub(
                SubscriptionType::kPriceTrack,
                IdentifierType::kProductClusterId,
                base::NumberToString(info->product_cluster_id.value()),
                ManagementType::kUserManaged);

            handler->shopping_service_->IsSubscribed(
                sub,
                base::BindOnce(
                    [](GetPriceTrackingStatusForCurrentUrlCallback callback,
                       bool subscribed) {
                      std::move(callback).Run(subscribed);
                    },
                    std::move(callback)));
          },
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void ShoppingListHandler::SetPriceTrackingStatusForCurrentUrl(bool track) {
  if (track) {
    // If the product on the page isn't already tracked, create a bookmark for
    // it and start tracking.
    TrackPriceForBookmark(delegate_->GetOrAddBookmarkForCurrentUrl()->id());
  } else {
    // If the product is already tracked, there must be a bookmark, but it's not
    // necessarily the page the user is currently on (i.e. multi-merchant
    // tracking). Prioritize accessing the product info for the URL before
    // attempting to access the bookmark.

    base::OnceCallback<void(uint64_t)> unsubscribe = base::BindOnce(
        [](base::WeakPtr<ShoppingListHandler> handler, uint64_t id) {
          if (!handler) {
            return;
          }

          commerce::SetPriceTrackingStateForClusterId(
              handler->shopping_service_, handler->bookmark_model_, id, false,
              base::BindOnce([](bool success) {}));
        },
        weak_ptr_factory_.GetWeakPtr());

    shopping_service_->GetProductInfoForUrl(
        delegate_->GetCurrentTabUrl().value(),
        base::BindOnce(
            [](base::WeakPtr<ShoppingListHandler> handler,
               base::OnceCallback<void(uint64_t)> unsubscribe, const GURL& url,
               const absl::optional<const ProductInfo>& info) {
              if (!handler) {
                return;
              }

              if (!info.has_value() || !info->product_cluster_id.has_value()) {
                absl::optional<uint64_t> cluster_id =
                    GetProductClusterIdFromBookmark(url,
                                                    handler->bookmark_model_);

                if (cluster_id.has_value()) {
                  std::move(unsubscribe).Run(cluster_id.value());
                }

                return;
              }

              std::move(unsubscribe).Run(info->product_cluster_id.value());
            },
            weak_ptr_factory_.GetWeakPtr(), std::move(unsubscribe)));
  }
}

void ShoppingListHandler::GetParentBookmarkFolderNameForCurrentUrl(
    GetParentBookmarkFolderNameForCurrentUrlCallback callback) {
  const GURL current_url = delegate_->GetCurrentTabUrl().value();
  std::move(callback).Run(
      commerce::GetBookmarkParentName(bookmark_model_, current_url)
          .value_or(base::EmptyString16()));
}

void ShoppingListHandler::ShowBookmarkEditorForCurrentUrl() {
  delegate_->ShowBookmarkEditorForCurrentUrl();
}

void ShoppingListHandler::OnFetchProductInfoForCurrentUrl(
    GetProductInfoForCurrentUrlCallback callback,
    const GURL& url,
    const absl::optional<const ProductInfo>& info) {
  std::move(callback).Run(ProductInfoToMojoProduct(url, info, locale_));
}

void ShoppingListHandler::GetPriceInsightsInfoForCurrentUrl(
    GetPriceInsightsInfoForCurrentUrlCallback callback) {
  if (!shopping_service_->IsPriceInsightsEligible() || !delegate_ ||
      !delegate_->GetCurrentTabUrl().has_value()) {
    std::move(callback).Run(shopping_list::mojom::PriceInsightsInfo::New());
    return;
  }

  shopping_service_->GetPriceInsightsInfoForUrl(
      delegate_->GetCurrentTabUrl().value(),
      base::BindOnce(
          &ShoppingListHandler::OnFetchPriceInsightsInfoForCurrentUrl,
          weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void ShoppingListHandler::OnFetchPriceInsightsInfoForCurrentUrl(
    GetPriceInsightsInfoForCurrentUrlCallback callback,
    const GURL& url,
    const absl::optional<PriceInsightsInfo>& info) {
  std::move(callback).Run(PriceInsightsInfoToMojoObject(info, locale_));
}

void ShoppingListHandler::ShowInsightsSidePanelUI() {
  if (delegate_) {
    delegate_->ShowInsightsSidePanelUI();
  }
}

void ShoppingListHandler::OnGetPriceTrackingStatusForCurrentUrl(
    GetPriceTrackingStatusForCurrentUrlCallback callback,
    bool tracked) {
  std::move(callback).Run(tracked);
}

void ShoppingListHandler::OpenUrlInNewTab(const GURL& url) {
  if (delegate_) {
    delegate_->OpenUrlInNewTab(url);
  }
}

void ShoppingListHandler::ShowFeedback() {
  if (delegate_) {
    delegate_->ShowFeedback();
  }
}

}  // namespace commerce
