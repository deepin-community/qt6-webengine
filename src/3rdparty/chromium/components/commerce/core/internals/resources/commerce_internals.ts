// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {getTrustedHTML} from 'chrome://resources/js/static_types.js';
import {getRequiredElement} from 'chrome://resources/js/util.js';

import {EligibleEntry} from './commerce_internals.mojom-webui.js';
import {CommerceInternalsApiProxy} from './commerce_internals_api_proxy.js';

const SUBSCRIPTION_ROWS =
    ['Cluster ID', 'Domain', 'Price', 'Previous Price', 'Product'];
const CLUSTER_ID_COLUMN_IDX = 0;
const DOMAIN_COLUMN_IDX = 1;
const CURRENT_PRICE_COLUMN_IDX = 2;
const PREVIOUS_PRICE_COLUMN_IDX = 3;
const PRODUCT_COLUMN_IDX = 4;

function getProxy(): CommerceInternalsApiProxy {
  return CommerceInternalsApiProxy.getInstance();
}

function entryVerificationElement(value: boolean, expected: boolean) {
  const checkmarkHTML = getTrustedHTML`&#10004;`;
  const crossmarkHTML = getTrustedHTML`&#10006;`;
  const span = document.createElement('span');
  const isValid: boolean = value === expected;
  span.innerHTML = isValid ? checkmarkHTML : crossmarkHTML;
  span.classList.add(isValid ? 'eligible' : 'ineligible');
  return span;
}

function createLiElement(factor: string, entry: EligibleEntry) {
  const li = document.createElement('li');
  li.innerText = factor + (entry.value ? ': true ' : ': false ');
  li.appendChild(entryVerificationElement(entry.value, entry.expectedValue));
  return li;
}

function seeEligibleDetails() {
  getProxy().getShoppingListEligibleDetails().then(({detail}) => {
    const element = getRequiredElement('shopping-list-eligible-details');
    getRequiredElement('shopping-list-eligible-see-details-btn').innerText =
        'Refresh';
    while (element.hasChildNodes()) {
      element.removeChild(element.firstElementChild!);
    }

    const ul = document.createElement('ul');
    ul.appendChild(createLiElement(
        'IsRegionLockedFeatureEnabled', detail.isRegionLockedFeatureEnabled));
    ul.appendChild(createLiElement(
        'IsShoppingListAllowedForEnterprise',
        detail.isShoppingListAllowedForEnterprise));
    ul.appendChild(
        createLiElement('IsAccountCheckerValid', detail.isAccountCheckerValid));
    ul.appendChild(createLiElement('IsSignedIn', detail.isSignedIn));
    ul.appendChild(
        createLiElement('IsSyncingBookmarks', detail.isSyncingBookmarks));
    ul.appendChild(createLiElement(
        'IsAnonymizedUrlDataCollectionEnabled',
        detail.isAnonymizedUrlDataCollectionEnabled));
    ul.appendChild(createLiElement(
        'IsSubjectToParentalControls', detail.isSubjectToParentalControls));

    element.appendChild(ul);
  });
}

function initialize() {
  getRequiredElement('shopping-list-eligible-see-details-btn')
      .addEventListener('click', seeEligibleDetails);

  getRequiredElement('reset-price-tracking-email-pref-button')
      .addEventListener('click', () => {
        getProxy().resetPriceTrackingEmailPref();
      });

  getProxy().getCallbackRouter().onShoppingListEligibilityChanged.addListener(
      (eligible: boolean) => {
        updateShoppingListEligibleStatus(eligible);
      });

  getProxy().getIsShoppingListEligible().then(({eligible}) => {
    updateShoppingListEligibleStatus(eligible);
    if (eligible) {
      renderSubscriptions();
    }
  });
}

function renderSubscriptions() {
  getProxy().getSubscriptionDetails().then(({subscriptions}) => {
    if (!subscriptions || subscriptions.length == 0) {
      return;
    }

    const subscriptionsElement = document.getElementById('subscriptions');
    if (!subscriptionsElement) {
      return;
    }
    const table = document.createElement('table');
    const thead = document.createElement('thead');
    const tr = document.createElement('tr');

    for (const colName of SUBSCRIPTION_ROWS) {
      const th = document.createElement('th');
      th.innerText = colName;
      th.setAttribute('align', 'left');
      tr.appendChild(th);
    }
    thead.appendChild(tr);
    table.appendChild(thead);

    for (let i = 0; i < subscriptions.length; i++) {
      const productInfos = subscriptions[i]!.productInfos;

      // Highlight red if there are no bookmarks for the subscription.
      const row = createRow();
      if (productInfos.length == 0) {
        row.classList.add('error-row');
        row.setAttribute('bgcolor', 'FF7F7F');
        const columns = row.getElementsByTagName('td');
        columns[CLUSTER_ID_COLUMN_IDX]!.textContent =
            BigInt(subscriptions[i]!.clusterId).toString();
        table.appendChild(row);
        continue;
      }

      for (let j = 0; j < productInfos.length; j++) {
        const columns = row.getElementsByTagName('td');
        columns[CLUSTER_ID_COLUMN_IDX]!.textContent =
            BigInt(productInfos[j]!.info!.clusterId!).toString();
        columns[DOMAIN_COLUMN_IDX]!.textContent = productInfos[j]!.info.domain!;
        columns[CURRENT_PRICE_COLUMN_IDX]!.textContent =
            productInfos[j]!.info.currentPrice!;
        columns[PREVIOUS_PRICE_COLUMN_IDX]!.textContent =
            productInfos[j]!.info.previousPrice!;

        const url = productInfos[j]!.info.productUrl.url;
        const productCell = columns[PRODUCT_COLUMN_IDX]!;
        if (url == undefined) {
          productCell.textContent = productInfos[j]!.info.title!;
        } else {
          const a = document.createElement('a');
          a.textContent = productInfos[j]!.info.title!;
          a.setAttribute('href', url);
          productCell.appendChild(a);
        }
        const imageUrl = productInfos[j]?.info.imageUrl;
        if (imageUrl != undefined) {
          const space = document.createElement('span');
          space.textContent = ' ';
          productCell.appendChild(space);
          const imgLink = document.createElement('a');
          imgLink.textContent = '(image)';
          imgLink.setAttribute('href', imageUrl.url);
          productCell.appendChild(imgLink);
        }

        row.appendChild(productCell);
        table.appendChild(row);
        subscriptionsElement.appendChild(table);
      }
    }
  });
}

function createRow() {
  const clusterIdCell = document.createElement('td');
  const domainCell = document.createElement('td');
  const currentPriceCell = document.createElement('td');
  const previousPriceCell = document.createElement('td');
  const productCell = document.createElement('td');
  const row = document.createElement('tr');
  for (const cell
           of [clusterIdCell, domainCell, currentPriceCell, previousPriceCell,
               productCell]) {
    row.appendChild(cell);
  }
  return row;
}

function updateShoppingListEligibleStatus(eligible: boolean) {
  const eligibleText: string = eligible ? 'true' : 'false';
  const element = getRequiredElement('shopping-list-eligible');
  element.classList.remove('eligible');
  element.classList.remove('ineligible');
  element.innerText = eligibleText;
  element.classList.add(eligible ? 'eligible' : 'ineligible');
}

document.addEventListener('DOMContentLoaded', initialize);
