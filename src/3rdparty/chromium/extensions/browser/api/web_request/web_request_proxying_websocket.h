// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_API_WEB_REQUEST_WEB_REQUEST_PROXYING_WEBSOCKET_H_
#define EXTENSIONS_BROWSER_API_WEB_REQUEST_WEB_REQUEST_PROXYING_WEBSOCKET_H_

#include <optional>
#include <string>
#include <vector>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "components/keyed_service/core/keyed_service_shutdown_notifier.h"
#include "extensions/browser/api/web_request/extension_web_request_event_router.h"
#include "extensions/browser/api/web_request/web_request_api.h"
#include "extensions/browser/api/web_request/web_request_info.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "services/network/public/mojom/websocket.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {
class IPEndPoint;
class SiteForCookies;
}  // namespace net

namespace extensions {

// A WebRequestProxyingWebSocket proxies a WebSocket connection and dispatches
// WebRequest API events.
class WebRequestProxyingWebSocket
    : public WebRequestAPI::Proxy,
      public network::mojom::WebSocketHandshakeClient,
      public network::mojom::WebSocketAuthenticationHandler,
      public network::mojom::TrustedHeaderClient {
 public:
  using WebSocketFactory = content::ContentBrowserClient::WebSocketFactory;

  WebRequestProxyingWebSocket(
      WebSocketFactory factory,
      const network::ResourceRequest& request,
      mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
          handshake_client,
      bool has_extra_headers,
      int process_id,
      int render_frame_id,
      content::BrowserContext* browser_context,
      WebRequestAPI::RequestIDGenerator* request_id_generator,
      WebRequestAPI::ProxySet* proxies);

  WebRequestProxyingWebSocket(const WebRequestProxyingWebSocket&) = delete;
  WebRequestProxyingWebSocket& operator=(const WebRequestProxyingWebSocket&) =
      delete;

  ~WebRequestProxyingWebSocket() override;

  void Start();

  // network::mojom::WebSocketHandshakeClient methods:
  void OnOpeningHandshakeStarted(
      network::mojom::WebSocketHandshakeRequestPtr request) override;
  void OnFailure(const std::string&, int net_error, int response_code) override;
  void OnConnectionEstablished(
      mojo::PendingRemote<network::mojom::WebSocket> websocket,
      mojo::PendingReceiver<network::mojom::WebSocketClient> client_receiver,
      network::mojom::WebSocketHandshakeResponsePtr response,
      mojo::ScopedDataPipeConsumerHandle readable,
      mojo::ScopedDataPipeProducerHandle writable) override;

  // network::mojom::WebSocketAuthenticationHandler method:
  void OnAuthRequired(const net::AuthChallengeInfo& auth_info,
                      const scoped_refptr<net::HttpResponseHeaders>& headers,
                      const net::IPEndPoint& remote_endpoint,
                      OnAuthRequiredCallback callback) override;

  // network::mojom::TrustedHeaderClient methods:
  void OnBeforeSendHeaders(const net::HttpRequestHeaders& headers,
                           OnBeforeSendHeadersCallback callback) override;
  void OnHeadersReceived(const std::string& headers,
                         const net::IPEndPoint& endpoint,
                         OnHeadersReceivedCallback callback) override;

  static void StartProxying(
      WebSocketFactory factory,
      const GURL& url,
      const net::SiteForCookies& site_for_cookies,
      const std::optional<std::string>& user_agent,
      mojo::PendingRemote<network::mojom::WebSocketHandshakeClient>
          handshake_client,
      bool has_extra_headers,
      int process_id,
      int render_frame_id,
      WebRequestAPI::RequestIDGenerator* request_id_generator,
      const url::Origin& origin,
      content::BrowserContext* browser_context,
      WebRequestAPI::ProxySet* proxies);

  static void EnsureAssociatedFactoryBuilt();

 private:
  void OnBeforeRequestComplete(int error_code);
  void OnBeforeSendHeadersComplete(const std::set<std::string>& removed_headers,
                                   const std::set<std::string>& set_headers,
                                   int error_code);
  void ContinueToStartRequest(int error_code);
  void OnHeadersReceivedComplete(int error_code);
  void ContinueToHeadersReceived();
  void OnAuthRequiredComplete(WebRequestEventRouter::AuthRequiredResponse rv);
  void OnHeadersReceivedCompleteForAuth(const net::AuthChallengeInfo& auth_info,
                                        int rv);
  void ContinueToCompleted();

  void PauseIncomingMethodCallProcessing();
  void ResumeIncomingMethodCallProcessing();
  void OnError(int result);
  // This is used for detecting errors on mojo connection with the network
  // service.
  void OnMojoConnectionErrorWithCustomReason(uint32_t custom_reason,
                                             const std::string& description);
  // This is used for detecting errors on mojo connection with original client
  // (i.e., renderer).
  void OnMojoConnectionError();

  WebSocketFactory factory_;
  const raw_ptr<content::BrowserContext> browser_context_;
  mojo::Remote<network::mojom::WebSocketHandshakeClient>
      forwarding_handshake_client_;
  mojo::Receiver<network::mojom::WebSocketHandshakeClient>
      receiver_as_handshake_client_{this};
  mojo::Receiver<network::mojom::WebSocketAuthenticationHandler>
      receiver_as_auth_handler_{this};
  mojo::Receiver<network::mojom::TrustedHeaderClient>
      receiver_as_header_client_{this};

  net::HttpRequestHeaders request_headers_;
  network::mojom::URLResponseHeadPtr response_;
  net::AuthCredentials auth_credentials_;
  OnAuthRequiredCallback auth_required_callback_;
  scoped_refptr<net::HttpResponseHeaders> override_headers_;
  std::vector<network::mojom::HttpHeaderPtr> additional_headers_;

  OnBeforeSendHeadersCallback on_before_send_headers_callback_;
  OnHeadersReceivedCallback on_headers_received_callback_;

  GURL redirect_url_;
  bool is_done_ = false;
  bool has_extra_headers_;
  mojo::PendingRemote<network::mojom::WebSocket> websocket_;
  mojo::PendingReceiver<network::mojom::WebSocketClient> client_receiver_;
  network::mojom::WebSocketHandshakeResponsePtr handshake_response_ = nullptr;
  mojo::ScopedDataPipeConsumerHandle readable_;
  mojo::ScopedDataPipeProducerHandle writable_;

  WebRequestInfo info_;

  // Owns |this|.
  const raw_ptr<WebRequestAPI::ProxySet> proxies_;

  // Notifies the proxy that the browser context has been shutdown.
  base::CallbackListSubscription shutdown_notifier_subscription_;

  base::WeakPtrFactory<WebRequestProxyingWebSocket> weak_factory_{this};
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_API_WEB_REQUEST_WEB_REQUEST_PROXYING_WEBSOCKET_H_
