// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_ASH_APP_INSTALL_APP_INSTALL_UI_H_
#define CHROME_BROWSER_UI_WEBUI_ASH_APP_INSTALL_APP_INSTALL_UI_H_

#include "chrome/browser/ui/webui/ash/app_install/app_install_page_handler.h"
#include "content/public/browser/webui_config.h"
#include "ui/web_dialogs/web_dialog_ui.h"

namespace ash::app_install {

// The WebUI for chrome://app-install-dialog, used for confirming app
// installation.
class AppInstallDialogUI : public ui::MojoWebDialogUI,
                              public mojom::PageHandlerFactory {
 public:
  explicit AppInstallDialogUI(content::WebUI* web_ui);
  AppInstallDialogUI(const AppInstallDialogUI&) = delete;
  AppInstallDialogUI& operator=(const AppInstallDialogUI&) = delete;
  ~AppInstallDialogUI() override;

  void SetDialogArgs(mojom::DialogArgsPtr args);
  void SetExpectedAppId(std::string expected_app_id);
  void SetDialogCallback(
      base::OnceCallback<void(bool accepted)> dialog_accepted_callback);
  void SetInstallComplete(const std::string* app_id);

  // Instantiates the implementor of the mojom::PageHandlerFactory mojo
  // interface passing the pending receiver that will be internally bound.
  void BindInterface(mojo::PendingReceiver<mojom::PageHandlerFactory> receiver);

  // mojom::PageHandlerFactory:
  void CreatePageHandler(
      mojo::PendingReceiver<mojom::PageHandler> pending_page_handler) override;

 private:
  void CloseDialog();

  mojom::DialogArgsPtr dialog_args_;
  std::string expected_app_id_;
  base::OnceCallback<void(bool accepted)> dialog_accepted_callback_;
  std::unique_ptr<AppInstallPageHandler> page_handler_;
  mojo::Receiver<mojom::PageHandlerFactory> factory_receiver_{this};

  WEB_UI_CONTROLLER_TYPE_DECL();
};

// WebUIConfig for chrome://app-install-dialog
class AppInstallDialogUIConfig
    : public content::DefaultWebUIConfig<AppInstallDialogUI> {
 public:
  AppInstallDialogUIConfig();

  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

}  // namespace ash::app_install

#endif  // CHROME_BROWSER_UI_WEBUI_ASH_APP_INSTALL_APP_INSTALL_UI_H_
