#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <atomic>
#include <mutex>
#include <queue>
#include <windows.h>
#include <winhttp.h>

// ---- Archipelago item / location ids ---------------------------------------
using APItemID     = int64_t;
using APLocationID = int64_t;

struct APItem {
    APItemID   item;
    APLocationID location;
    int        player;
    int        flags;
};

// ---- Callbacks -------------------------------------------------------------
using OnItemsReceived = std::function<void(const std::vector<APItem>&)>;
using OnGoalAchieved  = std::function<void()>;
using OnPrintMessage  = std::function<void(const std::string&)>;
using OnConnected     = std::function<void(int slot, const std::string& slot_name)>;

// ---- Client status enum (mirrors AP ClientStatus) --------------------------
enum class APClientStatus : int {
    Unknown   = 0,
    Connected = 5,
    Ready     = 10,
    Playing   = 20,
    Goal      = 30,
};

// ============================================================
class APClient {
public:
    APClient();
    ~APClient();

    // Start background connection thread.
    void start(const std::string& host, uint16_t port,
               const std::string& game, const std::string& slot,
               const std::string& password);
    void stop();

    bool is_connected() const { return connected_.load(); }

    // Send location check IDs to the server.
    void send_location_checks(const std::vector<APLocationID>& ids);

    // Notify server of status change.
    void send_status(APClientStatus status);

    // Send goal completion.
    void send_goal();

    // Callbacks (set before calling start())
    OnItemsReceived  on_items_received;
    OnGoalAchieved   on_goal;
    OnPrintMessage   on_print;
    OnConnected      on_connected;

private:
    void thread_func();
    void handle_message(const std::string& json_text);
    void send_raw(const std::string& json_text);

    friend DWORD WINAPI ap_thread_entry(LPVOID param);

    bool ws_connect();
    void ws_disconnect();
    bool ws_send(const std::string& text);
    bool ws_recv(std::string& out_text);

    std::string  host_;
    uint16_t     port_  = 38281;
    std::string  game_;
    std::string  slot_;
    std::string  password_;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    HANDLE thread_handle_    = nullptr;

    // WinHTTP handles
    HINTERNET h_session_ = nullptr;
    HINTERNET h_connect_ = nullptr;
    HINTERNET h_request_ = nullptr;
    HINTERNET h_ws_      = nullptr;

    // Outbound queue (thread-safe)
    std::mutex              out_mutex_;
    std::queue<std::string> out_queue_;

    // AP state
    int   slot_id_          = -1;
    int   last_item_index_  = 0;
};
