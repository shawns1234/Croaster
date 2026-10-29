#include "WebUiServer.h"

#include <WiFi.h>
#include <esp_http_server.h>

#include <cstring>

#include "webui_assets.h"

namespace {

constexpr uint32_t kCheckMs = 1000; // WiFi state poll period in loop()

esp_err_t serveAsset(httpd_req_t* req) {
    const auto* asset = static_cast<const webui::Asset*>(req->user_ctx);

    // The ETag is the hash of the gzipped bytes, so a cached copy is reused until the firmware changes.
    char tag[24] = {};
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", tag, sizeof tag) == ESP_OK &&
        std::strcmp(tag, asset->etag) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        httpd_resp_set_hdr(req, "ETag", asset->etag);
        return httpd_resp_send(req, nullptr, 0);
    }

    httpd_resp_set_type(req, asset->type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache"); // revalidate with the ETag on every load
    httpd_resp_set_hdr(req, "ETag", asset->etag);
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, reinterpret_cast<const char*>(asset->data), asset->size);
}

esp_err_t serveInfo(httpd_req_t* req) {
    const auto* body = static_cast<const String*>(req->user_ctx);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, body->c_str(), body->length());
}

} // namespace

WebUiServer::WebUiServer(const Options& options)
    : options_(options) {}

void WebUiServer::loop() {
    const uint32_t now = millis();
    if (now - lastCheckMs_ < kCheckMs)
        return;
    lastCheckMs_ = now;

    // WiFiManager runs its portal in AP (or AP+STA) mode and serves it on port 80.
    const bool portalOpen = (WiFi.getMode() & WIFI_MODE_AP) != 0;
    if (running()) {
        if (portalOpen)
            stop();
        return;
    }
    if (!portalOpen && WiFi.status() == WL_CONNECTED)
        start();
}

bool WebUiServer::start() {
    // Built once here, read-only afterwards from the server task.
    info_ = String("{\"board\":\"") + options_.board + "\",\"controls\":" + (options_.controls ? "true" : "false") +
            ",\"wsPort\":" + options_.wsPort + "}";

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = options_.port;
    config.ctrl_port = 32768 + options_.port; // the default (32768) clashes if a second httpd ever runs
    config.core_id = options_.coreId;
    config.task_priority = options_.taskPriority;
    config.max_open_sockets = 4; // sockets are shared with the WebSocket server on port 81 (5 clients)
    config.lru_purge_enable = true;
    config.max_uri_handlers = webui::kAssetCount + 1;

    httpd_handle_t handle = nullptr;
    if (httpd_start(&handle, &config) != ESP_OK) {
        Serial.println("# Web UI: HTTP server failed to start");
        return false;
    }
    for (size_t i = 0; i < webui::kAssetCount; i++) {
        httpd_uri_t uri = {};
        uri.uri = webui::kAssets[i].path;
        uri.method = HTTP_GET;
        uri.handler = serveAsset;
        uri.user_ctx = const_cast<webui::Asset*>(&webui::kAssets[i]);
        httpd_register_uri_handler(handle, &uri);
    }
    httpd_uri_t info = {};
    info.uri = "/info.json";
    info.method = HTTP_GET;
    info.handler = serveInfo;
    info.user_ctx = &info_;
    httpd_register_uri_handler(handle, &info);

    handle_ = handle;
    Serial.println("# Web UI: http://" + WiFi.localIP().toString() +
                   (options_.port == 80 ? String("/") : ":" + String(options_.port) + "/"));
    return true;
}

void WebUiServer::stop() {
    httpd_stop(static_cast<httpd_handle_t>(handle_));
    handle_ = nullptr;
    Serial.println("# Web UI: stopped (WiFi portal open)");
}
