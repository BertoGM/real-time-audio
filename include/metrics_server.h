#pragma once
#include "metrics.h"

#include <libwebsockets.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstring>
#include <algorithm>

struct WsSession {
    bool dirty{false};
};

class MetricsServer {
public:
    struct Snapshot {
        int32_t  lat_min{0}, lat_avg{0}, lat_p95{0}, lat_max{0};
        uint64_t sent{0}, recv{0}, lost{0};
        double   loss_pct{0};
        size_t   buf_fill{0}, buf_cap{4};
        struct ClientInfo { std::string ip; uint64_t pkts; };
        std::vector<ClientInfo> clients;
    };

    MetricsServer() : context_(nullptr), running_(false) {}
    ~MetricsServer() { stop(); }

    bool start(int port, const std::string& html) {
        port_ = port;
        html_ = html;

        static lws_protocols protocols[] = {
            { "http",
              lws_callback_http_dummy,
              0, 0, 0, nullptr, 0 },
            { "audiostream-metrics",
              MetricsServer::ws_callback,
              sizeof(WsSession),
              65536, 0, nullptr, 0 },
            { nullptr, nullptr, 0, 0, 0, nullptr, 0 }
        };

        lws_context_creation_info info{};
        info.port      = port;
        info.protocols = protocols;
        info.user      = this;

        context_ = lws_create_context(&info);
        if (!context_) {
            ERR("Failed to create lws context on port %d", port);
            return false;
        }

        running_ = true;
        thread_  = std::thread([this] { run_loop(); });
        LOG("Metrics dashboard: http://localhost:%d", port_);
        return true;
    }

    void stop() {
        if (!running_) return;
        running_ = false;
        if (context_) lws_cancel_service(context_);
        if (thread_.joinable()) thread_.join();
        if (context_) { lws_context_destroy(context_); context_ = nullptr; }
    }

    void update(const Snapshot& snap) {
        std::string json = to_json(snap);
        std::unique_lock<std::mutex> lock(mu_);
        latest_json_ = json;
        pending_     = true;
        lock.unlock();
        if (context_) lws_cancel_service(context_);
    }

    static int ws_callback(lws* wsi, lws_callback_reasons reason,
                            void* user, void* in, size_t len) {
        auto* self = static_cast<MetricsServer*>(
            lws_context_user(lws_get_context(wsi)));
        auto* sess = static_cast<WsSession*>(user);
        if (!self) return 0;
        return self->handle(wsi, reason, sess, in, len);
    }

private:
    void run_loop() {
        while (running_) {
            lws_service(context_, 50);
            std::unique_lock<std::mutex> lock(mu_);
            if (pending_) {
                pending_ = false;
                lock.unlock();
                for (lws* w : ws_clients_) lws_callback_on_writable(w);
            }
        }
    }

    int handle(lws* wsi, lws_callback_reasons reason,
               WsSession* /*sess*/, void* /*in*/, size_t /*len*/) {
        switch (reason) {

        case LWS_CALLBACK_ESTABLISHED: {
            std::unique_lock<std::mutex> lock(mu_);
            ws_clients_.push_back(wsi);
            DLOG("WS browser connected (total=%zu)", ws_clients_.size());
            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            std::unique_lock<std::mutex> lock(mu_);
            ws_clients_.erase(
                std::remove(ws_clients_.begin(), ws_clients_.end(), wsi),
                ws_clients_.end());
            break;
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            std::string json;
            { std::unique_lock<std::mutex> lock(mu_); json = latest_json_; }
            if (json.empty()) break;
            std::vector<uint8_t> buf(LWS_PRE + json.size());
            memcpy(buf.data() + LWS_PRE, json.data(), json.size());
            lws_write(wsi, buf.data() + LWS_PRE, json.size(), LWS_WRITE_TEXT);
            break;
        }

        case LWS_CALLBACK_HTTP: {
            std::string resp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Connection: close\r\n\r\n" + html_;
            unsigned char* p = reinterpret_cast<unsigned char*>(resp.data());
            lws_write(wsi, p, resp.size(), LWS_WRITE_HTTP);
            return -1;
        }

        default: break;
        }
        return 0;
    }

    static std::string to_json(const Snapshot& s) {
        char buf[4096];
        int  n = 0;
        n += snprintf(buf+n, sizeof(buf)-n,
            "{\"lat_min\":%d,\"lat_avg\":%d,\"lat_p95\":%d,\"lat_max\":%d,"
            "\"sent\":%llu,\"recv\":%llu,\"lost\":%llu,\"loss_pct\":%.2f,"
            "\"buf_fill\":%zu,\"buf_cap\":%zu,\"clients\":[",
            s.lat_min, s.lat_avg, s.lat_p95, s.lat_max,
            (unsigned long long)s.sent,
            (unsigned long long)s.recv,
            (unsigned long long)s.lost,
            s.loss_pct, s.buf_fill, s.buf_cap);
        for (size_t i = 0; i < s.clients.size(); ++i)
            n += snprintf(buf+n, sizeof(buf)-n,
                "%s{\"ip\":\"%s\",\"pkts\":%llu}",
                i?",":"", s.clients[i].ip.c_str(),
                (unsigned long long)s.clients[i].pkts);
        n += snprintf(buf+n, sizeof(buf)-n, "]}");
        return std::string(buf, n);
    }

    int               port_{9002};
    std::string       html_;
    lws_context*      context_;
    std::atomic<bool> running_;
    std::thread       thread_;

    std::mutex        mu_;
    std::string       latest_json_;
    bool              pending_{false};
    std::vector<lws*> ws_clients_;
};
