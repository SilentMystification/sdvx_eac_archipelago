#include "ap_client.h"
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <sstream>
#include <vector>
#include "../include/json.hpp"

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

// ---- helpers ---------------------------------------------------------------

static std::wstring to_wstring(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
    return w;
}

static std::string from_wstring(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}

// ---- thread entry ----------------------------------------------------------
static DWORD WINAPI ap_thread_entry(LPVOID param) {
    reinterpret_cast<APClient*>(param)->thread_func();
    return 0;
}

// ============================================================
APClient::APClient() {}

APClient::~APClient() { stop(); }

void APClient::start(const std::string& host, uint16_t port,
                     const std::string& game, const std::string& slot,
                     const std::string& password) {
    host_     = host;
    port_     = port;
    game_     = game;
    slot_     = slot;
    password_ = password;

    running_.store(true);
    thread_handle_ = CreateThread(nullptr, 0, ap_thread_entry, this, 0, nullptr);
}

void APClient::stop() {
    running_.store(false);
    ws_disconnect();
    if (thread_handle_) {
        WaitForSingleObject(thread_handle_, 5000);
        CloseHandle(thread_handle_);
        thread_handle_ = nullptr;
    }
}

// ---- WebSocket helpers -----------------------------------------------------
bool APClient::ws_connect() {
    ws_disconnect();

    h_session_ = WinHttpOpen(L"SDVX-AP/1.0",
                             WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h_session_) return false;

    // Set timeouts (connect, send, receive in ms)
    DWORD timeout = 10000;
    WinHttpSetOption(h_session_, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));

    h_connect_ = WinHttpConnect(h_session_, to_wstring(host_).c_str(), port_, 0);
    if (!h_connect_) return false;

    h_request_ = WinHttpOpenRequest(h_connect_, L"GET", L"/",
                                    nullptr, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!h_request_) return false;

    // Mark request as WebSocket upgrade
    if (!WinHttpSetOption(h_request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                          nullptr, 0))
        return false;

    if (!WinHttpSendRequest(h_request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        return false;

    if (!WinHttpReceiveResponse(h_request_, nullptr)) return false;

    h_ws_ = WinHttpWebSocketCompleteUpgrade(h_request_, 0);
    if (!h_ws_) return false;

    // Set recv timeout
    DWORD recv_timeout = 5000;
    WinHttpSetOption(h_ws_, WINHTTP_OPTION_RECEIVE_TIMEOUT, &recv_timeout, sizeof(recv_timeout));

    return true;
}

void APClient::ws_disconnect() {
    if (h_ws_)      { WinHttpWebSocketClose(h_ws_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0); WinHttpCloseHandle(h_ws_);      h_ws_      = nullptr; }
    if (h_request_) { WinHttpCloseHandle(h_request_); h_request_ = nullptr; }
    if (h_connect_) { WinHttpCloseHandle(h_connect_); h_connect_ = nullptr; }
    if (h_session_) { WinHttpCloseHandle(h_session_); h_session_ = nullptr; }
    connected_.store(false);
}

bool APClient::ws_send(const std::string& text) {
    if (!h_ws_) return false;
    DWORD res = WinHttpWebSocketSend(h_ws_,
                                     WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                     (PVOID)text.data(),
                                     (DWORD)text.size());
    return res == ERROR_SUCCESS;
}

bool APClient::ws_recv(std::string& out_text) {
    if (!h_ws_) return false;

    std::string buf;
    buf.resize(65536);
    DWORD bytes_read = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE buf_type;

    DWORD res = WinHttpWebSocketReceive(h_ws_,
                                        buf.data(), (DWORD)buf.size(),
                                        &bytes_read, &buf_type);
    if (res != ERROR_SUCCESS) return false;
    if (buf_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return false;

    out_text.assign(buf.data(), bytes_read);
    return true;
}

// ---- Outbound queue --------------------------------------------------------
void APClient::send_raw(const std::string& json_text) {
    std::lock_guard<std::mutex> lk(out_mutex_);
    out_queue_.push(json_text);
}

// ---- Protocol helpers ------------------------------------------------------
void APClient::send_location_checks(const std::vector<APLocationID>& ids) {
    if (ids.empty()) return;
    json pkt = json::array();
    json msg;
    msg["cmd"] = "LocationChecks";
    msg["locations"] = ids;
    pkt.push_back(msg);
    send_raw(pkt.dump());
}

void APClient::send_status(APClientStatus status) {
    json pkt = json::array();
    json msg;
    msg["cmd"]    = "StatusUpdate";
    msg["status"] = static_cast<int>(status);
    pkt.push_back(msg);
    send_raw(pkt.dump());
}

void APClient::send_goal() {
    send_status(APClientStatus::Goal);
    if (on_goal) on_goal();
}

// ---- Incoming message handler ----------------------------------------------
void APClient::handle_message(const std::string& text) {
    json packets;
    try {
        packets = json::parse(text);
    } catch (...) {
        return;
    }
    if (!packets.is_array()) return;

    for (auto& pkt : packets) {
        std::string cmd = pkt.value("cmd", "");

        if (cmd == "RoomInfo") {
            // Respond with Connect
            json cp = json::array();
            json connect;
            connect["cmd"]            = "Connect";
            connect["password"]       = password_;
            connect["game"]           = game_;
            connect["name"]           = slot_;
            connect["uuid"]           = "sdvx-ap-dll";
            connect["version"]        = {{"major",0},{"minor",5},{"build",0},{"class","Version"}};
            connect["items_handling"] = 0b111;  // receive all items
            connect["tags"]           = json::array();
            connect["slot_data"]      = true;
            cp.push_back(connect);
            send_raw(cp.dump());

        } else if (cmd == "Connected") {
            slot_id_ = pkt.value("slot", -1);
            std::string sname = slot_;
            connected_.store(true);
            if (on_connected) on_connected(slot_id_, sname);
            // Catch up on any items we may have missed
            if (pkt.contains("checked_locations")) {
                // checked_locations contains location IDs already done — skip re-sending
            }
            send_status(APClientStatus::Playing);

        } else if (cmd == "ConnectionRefused") {
            connected_.store(false);
            std::string reason;
            if (pkt.contains("errors") && pkt["errors"].is_array() && !pkt["errors"].empty())
                reason = pkt["errors"][0].get<std::string>();
            if (on_print) on_print("[AP] Connection refused: " + reason);

        } else if (cmd == "ReceivedItems") {
            int index = pkt.value("index", 0);
            if (!pkt.contains("items") || !pkt["items"].is_array()) continue;

            // Parse the full packet payload.
            std::vector<APItem> raw;
            for (auto& it : pkt["items"]) {
                APItem ap_item;
                ap_item.item     = it.value("item",     (APItemID)0);
                ap_item.location = it.value("location", (APLocationID)0);
                ap_item.player   = it.value("player",   0);
                ap_item.flags    = it.value("flags",    0);
                raw.push_back(ap_item);
            }

            // `index` is the position of raw[0] in the server's full item list.
            // `last_item_index_` is how many items we have already processed.
            // Skip items whose server-position is below our watermark; process
            // the rest.  This correctly handles both incremental delivery and
            // the full-resend that AP performs on reconnection.
            int skip = last_item_index_ - index;
            if (skip < 0) skip = 0;
            if (skip >= (int)raw.size()) continue;  // nothing new

            std::vector<APItem> fresh(raw.begin() + skip, raw.end());
            last_item_index_ = index + (int)raw.size();
            if (on_items_received)
                on_items_received(fresh);

        } else if (cmd == "PrintJSON") {
            if (on_print && pkt.contains("data")) {
                std::string text_out;
                for (auto& part : pkt["data"]) {
                    if (part.contains("text"))
                        text_out += part["text"].get<std::string>();
                }
                if (!text_out.empty()) on_print(text_out);
            }

        } else if (cmd == "RoomUpdate") {
            // Could handle permission / hint point changes here
        }
    }
}

// ---- Main thread -----------------------------------------------------------
void APClient::thread_func() {
    while (running_.load()) {
        // Attempt connection
        if (!ws_connect()) {
            if (on_print) on_print("[AP] Could not connect to " + host_ + " — retrying in 10s");
            Sleep(10000);
            continue;
        }
        if (on_print) on_print("[AP] WebSocket connected to " + host_);

        while (running_.load()) {
            // Flush outbound queue
            {
                std::lock_guard<std::mutex> lk(out_mutex_);
                while (!out_queue_.empty()) {
                    ws_send(out_queue_.front());
                    out_queue_.pop();
                }
            }

            // Receive with a short timeout (5 s set above)
            std::string msg;
            if (ws_recv(msg)) {
                handle_message(msg);
            } else {
                // recv failed — likely disconnected
                break;
            }
        }

        connected_.store(false);
        ws_disconnect();
        if (running_.load()) {
            if (on_print) on_print("[AP] Disconnected — reconnecting in 10s");
            Sleep(10000);
        }
    }
}
