// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://resources/cr_elements/cr_shared_vars.css.js';
import './shared_style.css.js';
import 'chrome://resources/polymer/v3_0/iron-location/iron-location.js';
import 'chrome://resources/polymer/v3_0/iron-pages/iron-pages.js';

import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {getTemplate} from './log_types.html.js';
import {FeatureValues} from './types.js';


/** @polymer */
export class LogTypesElement extends PolymerElement {
  static get is() {
    return 'log-types';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      currentLogTypes: {
        type: FeatureValues,
        value: [
          FeatureValues.NEARBY_SHARE,
          FeatureValues.NEARBY_CONNECTIONS,
          FeatureValues.NEARBY_PRESENCE,
          FeatureValues.FAST_PAIR,
        ],
      },

    };
  }

  currentLogTypes: FeatureValues[];

  private nearbyPresenceCheckboxClicked_(): void {
    const checkbox: HTMLInputElement|null =
        this.shadowRoot!.querySelector('#nearbyPresenceCheckbox');
    let checked: boolean = true;
    if (checkbox) {
      checked = checkbox.checked;
    }

    if (checked &&
        !this.currentLogTypes.includes(FeatureValues.NEARBY_PRESENCE)) {
      this.currentLogTypes.push(FeatureValues.NEARBY_PRESENCE);
    }
    if (!checked &&
        this.currentLogTypes.includes(FeatureValues.NEARBY_PRESENCE)) {
      this.currentLogTypes.splice(
          this.currentLogTypes.lastIndexOf(FeatureValues.NEARBY_PRESENCE), 1);
    }
  }

  private nearbyShareCheckboxClicked_(): void {
    const checkbox: HTMLInputElement|null =
        this.shadowRoot!.querySelector('#nearbyShareCheckbox');
    let checked: boolean = true;
    if (checkbox) {
      checked = checkbox.checked;
    }

    if (checked && !this.currentLogTypes.includes(FeatureValues.NEARBY_SHARE)) {
      this.currentLogTypes.push(FeatureValues.NEARBY_SHARE);
    }
    if (!checked && this.currentLogTypes.includes(FeatureValues.NEARBY_SHARE)) {
      this.currentLogTypes.splice(
          this.currentLogTypes.lastIndexOf(FeatureValues.NEARBY_SHARE), 1);
    }
  }

  private nearbyConnectionsCheckboxClicked_(): void {
    const checkbox: HTMLInputElement|null =
        this.shadowRoot!.querySelector('#nearbyConnectionsCheckbox');
    let checked: boolean = true;
    if (checkbox) {
      checked = checkbox.checked;
    }

    if (checked &&
        !this.currentLogTypes.includes(FeatureValues.NEARBY_CONNECTIONS)) {
      this.currentLogTypes.push(FeatureValues.NEARBY_CONNECTIONS);
    }
    if (!checked &&
        this.currentLogTypes.includes(FeatureValues.NEARBY_CONNECTIONS)) {
      this.currentLogTypes.splice(
          this.currentLogTypes.lastIndexOf(FeatureValues.NEARBY_CONNECTIONS),
          1);
    }
  }

  private fastPairCheckboxClicked_(): void {
    const checkbox: HTMLInputElement|null =
        this.shadowRoot!.querySelector('#fastPairCheckbox');
    let checked: boolean = true;
    if (checkbox) {
      checked = checkbox.checked;
    }

    if (checked && !this.currentLogTypes.includes(FeatureValues.FAST_PAIR)) {
      this.currentLogTypes.push(FeatureValues.FAST_PAIR);
    }
    if (!checked && this.currentLogTypes.includes(FeatureValues.FAST_PAIR)) {
      this.currentLogTypes.splice(
          this.currentLogTypes.lastIndexOf(FeatureValues.FAST_PAIR), 1);
    }
  }
}

customElements.define(LogTypesElement.is, LogTypesElement);
