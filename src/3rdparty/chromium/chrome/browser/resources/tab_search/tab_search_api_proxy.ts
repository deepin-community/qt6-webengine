// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {stringToMojoString16} from 'chrome://resources/js/mojo_type_util.js';

import {PageCallbackRouter, PageHandlerFactory, PageHandlerRemote, ProfileData, SwitchToTabInfo, Tab, TabOrganizationSession, UserFeedback} from './tab_search.mojom-webui.js';

/**
 * These values are persisted to logs and should not be renumbered or re-used.
 * See tools/metrics/histograms/enums.xml.
 */
export enum RecentlyClosedItemOpenAction {
  WITHOUT_SEARCH = 0,
  WITH_SEARCH = 1,
}

export interface TabSearchApiProxy {
  closeTab(tabId: number): void;

  acceptTabOrganization(
      sessionId: number, organizationId: number, name: string,
      tabs: Tab[]): void;

  rejectTabOrganization(sessionId: number, organizationId: number): void;

  getProfileData(): Promise<{profileData: ProfileData}>;

  getTabOrganizationSession(): Promise<{session: TabOrganizationSession}>;

  openRecentlyClosedEntry(
      id: number, withSearch: boolean, isTab: boolean, index: number): void;

  requestTabOrganization(): void;

  restartSession(): void;

  switchToTab(info: SwitchToTabInfo): void;

  getCallbackRouter(): PageCallbackRouter;

  removeTabFromOrganization(
      sessionId: number, organizationId: number, tab: Tab): void;

  saveRecentlyClosedExpandedPref(expanded: boolean): void;

  setTabIndex(index: number): void;

  startTabGroupTutorial(): void;

  triggerFeedback(sessionId: number): void;

  triggerSync(): void;

  triggerSignIn(): void;

  openHelpPage(): void;

  openSyncSettings(): void;

  setUserFeedback(
      sessionId: number, organizationId: number, feedback: UserFeedback): void;

  showUi(): void;
}

export class TabSearchApiProxyImpl implements TabSearchApiProxy {
  callbackRouter: PageCallbackRouter = new PageCallbackRouter();
  handler: PageHandlerRemote = new PageHandlerRemote();

  constructor() {
    const factory = PageHandlerFactory.getRemote();
    factory.createPageHandler(
        this.callbackRouter.$.bindNewPipeAndPassRemote(),
        this.handler.$.bindNewPipeAndPassReceiver());
  }

  closeTab(tabId: number) {
    this.handler.closeTab(tabId);
  }

  acceptTabOrganization(
      sessionId: number, organizationId: number, name: string, tabs: Tab[]) {
    this.handler.acceptTabOrganization(
        sessionId, organizationId, stringToMojoString16(name), tabs);
  }

  rejectTabOrganization(sessionId: number, organizationId: number) {
    this.handler.rejectTabOrganization(sessionId, organizationId);
  }

  getProfileData() {
    return this.handler.getProfileData();
  }

  getTabOrganizationSession() {
    return this.handler.getTabOrganizationSession();
  }

  openRecentlyClosedEntry(
      id: number, withSearch: boolean, isTab: boolean, index: number) {
    chrome.metricsPrivate.recordEnumerationValue(
        isTab ? 'Tabs.TabSearch.WebUI.RecentlyClosedTabOpenAction' :
                'Tabs.TabSearch.WebUI.RecentlyClosedGroupOpenAction',
        withSearch ? RecentlyClosedItemOpenAction.WITH_SEARCH :
                     RecentlyClosedItemOpenAction.WITHOUT_SEARCH,
        Object.keys(RecentlyClosedItemOpenAction).length);
    chrome.metricsPrivate.recordSmallCount(
        withSearch ?
            'Tabs.TabSearch.WebUI.IndexOfOpenRecentlyClosedEntryInFilteredList' :
            'Tabs.TabSearch.WebUI.IndexOfOpenRecentlyClosedEntryInUnfilteredList',
        index);
    this.handler.openRecentlyClosedEntry(id);
  }

  requestTabOrganization() {
    this.handler.requestTabOrganization();
  }

  restartSession() {
    this.handler.restartSession();
  }

  switchToTab(info: SwitchToTabInfo) {
    this.handler.switchToTab(info);
  }

  getCallbackRouter() {
    return this.callbackRouter;
  }

  removeTabFromOrganization(
      sessionId: number, organizationId: number, tab: Tab) {
    this.handler.removeTabFromOrganization(sessionId, organizationId, tab);
  }

  saveRecentlyClosedExpandedPref(expanded: boolean) {
    this.handler.saveRecentlyClosedExpandedPref(expanded);
  }

  setTabIndex(index: number) {
    this.handler.setTabIndex(index);
  }

  startTabGroupTutorial() {
    this.handler.startTabGroupTutorial();
  }

  triggerFeedback(sessionId: number) {
    this.handler.triggerFeedback(sessionId);
  }

  triggerSync() {
    this.handler.triggerSync();
  }

  triggerSignIn() {
    this.handler.triggerSignIn();
  }

  openHelpPage() {
    this.handler.openHelpPage();
  }

  openSyncSettings() {
    this.handler.openSyncSettings();
  }

  setUserFeedback(
      sessionId: number, organizationId: number, feedback: UserFeedback) {
    this.handler.setUserFeedback(sessionId, organizationId, feedback);
  }

  showUi() {
    this.handler.showUI();
  }

  static getInstance(): TabSearchApiProxy {
    return instance || (instance = new TabSearchApiProxyImpl());
  }

  static setInstance(obj: TabSearchApiProxy) {
    instance = obj;
  }
}

let instance: TabSearchApiProxy|null = null;
