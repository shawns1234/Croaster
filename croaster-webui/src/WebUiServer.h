#pragma once
// Serves the browser UI (web/, embedded gzipped in the firmware) over HTTP with
// ESP-IDF's esp_http_server. The server runs in its own task and only hands out
// constant data, so it never touches CroasterCore or the command handler: the
// page reads and controls the board through Croaster's WebSocket on port 81.
//
// Call loop() from the Arduino loop. It starts the server once the station is
// connected and WiFiManager's portal (which owns port 80 while it is open) is
// closed, and stops it if the portal opens again.
#include <Arduino.h>

class WebUiServer {
public:
    struct Options {
        uint16_t port = 80;            // HTTP port (CMON_WEBUI_PORT)
        uint16_t wsPort = 81;          // Croaster's WebSocket port, reported to the page
        bool controls = false;         // true on boards with the control add-on (shows the Controls section)
        const char* board = "unknown"; // board id, reported to the page
        int coreId = 0;                // server task core (0 = the WiFi core, away from the loop and control task)
        uint8_t taskPriority = 3;      // above the Arduino loop (1), below the WiFi stack
    };

    explicit WebUiServer(const Options& options);

    void loop();
    bool running() const {
        return handle_ != nullptr;
    }

private:
    bool start();
    void stop();

    Options options_;
    void* handle_ = nullptr; // httpd_handle_t
    String info_;            // body of /info.json, built once at start
    uint32_t lastCheckMs_ = 0;
};
