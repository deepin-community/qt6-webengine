## [Web Apps][2] - Installation

Installing a webapp can come from a variety of channels. This section serves to enumerate them all and show how they fit together in the installation pipeline.

### Flowchart

Here is a graphic of the installation flow:

![](webapp_installation_process.png)

[https://tinyurl.com/dpwa-installation-flowchart][4]

Note: The ExternallyManagedAppManager adds a few steps before this, and will sometimes (for placeholder apps) build a custom `WebAppInstallInfo` object to skip the 'build' steps.

### Installation Commands

There are a variety of [commands][5] used to install web apps. If introducing a new installation source, consider making a new command to isolate your operation (and prevent it from being complicated by other use-cases).

### Installation Sources

There are a variety of installation sources and expectations tied to those sources.

#### Omnibox install icon

User-initiated installation. To make the omnibox install icon visible, the document must: _Be promotable and installable_. NOT be inside of the scope of an installed WebApp with an effective [display mode display][6] mode that isn't `kBrowser`.

Triggers an install view that will show the name & icon to the user to confirm install. If the manifest also includes screenshots with a wide form-factor, then a more detailed install dialog will be shown.

This uses the [`FetchManifestAndInstallCommand`][7], providing just the `WebContents` of the installable page.

Fails if, after the user clicks : _After clicking on the install icon, the `WebContents` is no longer_ [_promotable_][8], _skipping engagement checks_. The user rejects the installation dialog.

#### 3-dot menu option "Install {App_Name}..."

User-initiated installation. To make the install menu option visible, the document must: _Be promotable and installable_. NOT be inside of the scope of an installed WebApp with an effective [display mode display][6] mode that isn't `kBrowser`.

Triggers an install view that will show the name & icon to the user to confirm install. If the manifest also includes screenshots with a wide form-factor, then a more detailed install dialog will be shown.

Calls [`FetchManifestAndInstallCommand`][7] with the `WebContents` of the installable page, and `use_fallback = true`.

Fails if: _The user rejects the installation dialog_.

Notably, this option does not go through the same exact pathway as the [omnibox install icon][10], as it shares the call-site as the "Create Shortcut" method below. The main functional difference here is that if the site becomes no longer [promotable][8] in between clicking on the menu option and the install actually happening, it will not fail and instead fall back to a fake manifest and/or fake icons based on the favicon. Practically, this option doesn't show up if the site is [promotable][8]. Should it share installation pathways as the the [omnibox install icon?][10] Probably, yes.

#### 3-dot menu option "Create Shortcut..."

User-initiated installation. This menu option is always available, except for internal chrome urls like chrome://settings.

Prompts the user whether the shortcut should "open in a window". If the user checks this option, then the resulting WebApp will have the [user display][11] set to `kStandalone` / open-in-a-window.

The document does not need to have a manifest for this install path to work. If no manifest is found, then a fake one is created with `start_url` equal to the document url, `name` equal to the document title, and the icons are generated from the favicon (if present).

Calls [`FetchManifestAndInstallCommand`][7] with the `WebContents` of the installable page, and `use_fallback = true`.

Fails if: _The user rejects the shortcut creation dialog_.

#### ChromeOS Management API

Checks [promotability][8] before installing, skipping engagement and serviceworker checks

Calls [`WebAppInstallManager::InstallWebAppFromManifest`][12], providing just the `WebContents` of the installable page.

TODO: Document when this API is called & why.

#### Externally Managed Apps

There are a number of apps that are managed externally. This means that there is an external manager keeps it's own list of web apps that need to be installed for a given external install source.

See the [`web_app::ExternalInstallSource`][13] enum to see all types of externally managed apps. Each source type should have an associated "manager" that gives the list of apps to `ExternallyManagedAppProvider::SynchronizeInstalledApps`.

These installations are customizable than user installations, as these external app management surfaces need to specify all of the options up front (e.g. create shortcut on desktop, open in window, run on login, etc). Thus the [`ExternallyManagedInstallCommand`][14] is called here, with the params generated by [`web_app::ConvertExternalInstallOptionsToParams`][15].

The general installation flow of an externally managed app is:

1. A call to [`ExternallyManagedAppProvider::SynchronizeInstalledApps`][16]
1. Finding all apps that need to be uninstalled and uninstalling them, find all apps that need to be installed and:
1. Queue an `ExternallyManagedAppInstallTask` to install each app sequentially.
1. Each task loads the url for the app.
1. If the url is successfully loaded, then use [`ExternallyManagedInstallCommand`][14], and continue installation on the normal pipeline (described above, and [flowchart][17] above).
1. If the url fails to fully load (usually a redirect if the user needs to sign in or corp credentials are not installed), and the external app manager specified a [placeholder app was required][18] then:
    1. Synthesize a web app with `start_url` as the document url, and `name` as the document title
    1. Install that webapp by using the finalizer directly. This is **not** part of the regular install pipeline, and basically directly saves the webapp into the database without running OS integration.

These placeholder apps are not meant to stay, and to replace them with the intended apps, the following occurs:

1. The WebAppProvider system listens to every page load.
1. If a [navigation is successful][20] to a url that the placeholder app is installed for, then
    1. The installation is started again with a call to [`WebAppInstallManager::InstallWebAppWithParams`][14].
    1. If successful, the placeholder app is uninstalled.

#### Sync

When the sync system receives an WebApp to install, it uses the [InstallFromSyncCommand`][21]. One major difference is if the installation fails for any reason (manifest is invalid or fails to load, etc), then a backup installation happens using information from the icon urls from the sync data, and document/favicons if available.

If the platform is not ChromeOS, then the app will not become [locally installed][23]. This means that OS integration will not be triggered, no platform shortcuts created, etc. 1. If the platform is ChromeOS, it will become [locally installed][23], and all OS integrations will be triggered (just like a normal user-initiated install.)

##### Retry on startup

Sync installs have a few extra complications:

- They need to be immediately saved to the database & be installed eventually.
- Many are often queued up during a new profile sign-in, and it's not uncommon for the user to quit before the installation queue finishes.

Due to this, unlike other installs, a special [`WebApp::is_from_sync_and_pending_installation`][24] (protobuf variable is saved in the database. WebApps with this set to true are treated as not fully installed, and are often left out of app listings. This variable is reset back to `false` when the app is finished installing.

To handle the cases above, on startup when the database is loaded, any WebApp with `is_from_sync_and_pending_installation` of `true` will be re-installed inside of [`WebAppSyncBridge::MaybeInstallAppsFromSyncAndPendingInstallation`][25]

### Installation State Modifications

#### Installing locally

On non-ChromeOS devices, an app can be [not locally installed][23]. To become locally installed, the user can follow a normal install method (install icon will show up), or they can interact with the app on `chrome://apps`.

The `chrome://apps` code is unique here, and instead of re-installing the app, in manually sets the locally_installed bit to true in [`AppLauncherHandler::HandleInstallAppLocally`][26], and triggers OS integration in [`AppLauncherHandler::InstallOsHooks`][26]

#### Creating Shortcuts

Similarly to above, in `chrome://apps` the user can "Create Shortcuts..." for a web app. This should overwrite any shortcuts already created, and basically triggers OS integration to install shortcuts again in [`AppLauncherHandler::HandleCreateAppShortcut`][27]

[2]: README.md
[4]: https://tinyurl.com/dpwa-installation-flowchart
[5]: https://source.chromium.org/search?q=f:install%20f:web_applications%2Fcommands&sq=&ss=chromium
[6]: concepts.md#effective-display-mode
[7]: https://source.chromium.org/search?q=FetchManifestAndInstallCommand&ss=chromium
[8]: concepts.md#promotable
[10]: #omnibox-install-icon
[11]: concepts.md#user-display-mode
[13]: https://source.chromium.org/search?q=web_app::ExternalInstallSource
[14]: https://source.chromium.org/search?q=ExternallyManagedInstallCommand&sq=&ss=chromium%2Fchromium%2Fsrc
[15]: https://source.chromium.org/search?q=web_app::ConvertExternalInstallOptionsToParams
[16]: https://source.chromium.org/search?q=ExternallyManagedAppProvider::SynchronizeInstalledApps
[17]: #flowchart
[18]: https://source.chromium.org/search?q=ExternalInstallOptions::install_placeholder
[20]: https://source.chromium.org/search?q=WebAppTabHelper::ReinstallPlaceholderAppIfNecessary
[21]: https://source.chromium.org/search?q=InstallFromSyncCommand&ss=chromium%2Fchromium%2Fsrc
[23]: ../README.md#locally-installed
[24]: https://source.chromium.org/search?q=WebApp::is_from_sync_and_pending_installation
[25]: https://source.chromium.org/search?q=WebAppSyncBridge::MaybeInstallAppsFromSyncAndPendingInstallation
[26]: https://source.chromium.org/search?q=AppLauncherHandler::HandleInstallAppLocally
[27]: https://source.chromium.org/search?q=AppLauncherHandler::HandleCreateAppShortcut