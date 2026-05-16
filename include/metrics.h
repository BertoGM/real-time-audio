#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <climits>
#include <mutex>
#include <chrono>

class Metrics {
public:
    Metrics() { reset(); }

    void record_sent() { ++sent_; }
    void record_recv() { ++recv_; }
    void record_lost() { ++lost_; }

    void record_latency_ms(int32_t ms) {
        std::unique_lock<std::mutex> lock(mu_);
        if (ms < lat_min_) lat_min_ = ms;
        if (ms > lat_max_) lat_max_ = ms;
        lat_sum_ += ms;
        lat_count_++;
        ring_[ring_pos_++ % RING] = ms;
    }

    void maybe_print() {
        auto now = std::chrono::steady_clock::now();
        if (now - last_print_ < std::chrono::seconds(10)) return;
        print();
        last_print_ = now;
        reset();
    }

    void print() const {
        uint64_t s = sent_.load();
        uint64_t r = recv_.load();
        uint64_t l = lost_.load();
        double loss_pct = s > 0 ? (100.0 * l / s) : 0.0;
        int32_t avg = 0, p95 = 0;
        {
            std::unique_lock<std::mutex> lock(mu_);
            if (lat_count_ > 0) avg = (int32_t)(lat_sum_ / lat_count_);
            int32_t tmp[RING];
            size_t cnt = lat_count_ < RING ? lat_count_ : RING;
            for (size_t i = 0; i < cnt; ++i) tmp[i] = ring_[i];
            for (size_t i = 1; i < cnt; ++i) {
                int32_t k = tmp[i]; size_t j = i;
                while (j > 0 && tmp[j-1] > k) { tmp[j] = tmp[j-1]; --j; }
                tmp[j] = k;
            }
            if (cnt > 0) p95 = tmp[(size_t)(cnt * 0.95)];
        }
        printf("+- Stream Health ----------------------------------------------\n");
        printf("| Sent: %6llu  Recv: %6llu  Lost: %llu (%.1f%%)\n",
               (unsigned long long)s, (unsigned long long)r,
               (unsigned long long)l, loss_pct);
        printf("| Latency:  min=%dms  avg=%dms  p95=%dms  max=%dms\n",
               lat_min_ == INT_MAX ? 0 : lat_min_, avg, p95,
               lat_max_ == INT_MIN ? 0 : lat_max_);
        printf("+-------------------------------------------------------------\n");
        fflush(stdout);
    }

private:
    static constexpr size_t RING = 100;

    void reset() {
        sent_ = recv_ = lost_ = 0;
        lat_min_   = INT_MAX;
        lat_max_   = INT_MIN;
        lat_sum_   = 0;
        lat_count_ = 0;
        ring_pos_  = 0;
        last_print_ = std::chrono::steady_clock::now();
    }

    std::atomic<uint64_t> sent_{0}, recv_{0}, lost_{0};
    mutable std::mutex mu_;
    int32_t lat_min_{INT_MAX}, lat_max_{INT_MIN};
    int64_t lat_sum_{0};
    size_t  lat_count_{0};
    int32_t ring_[RING]{};
    size_t  ring_pos_{0};
    std::chrono::steady_clock::time_point last_print_;
};

inline bool g_debug = false;

#define DLOG(fmt, ...) do { if (g_debug) fprintf(stderr, "[DBG] " fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG(fmt, ...)  fprintf(stderr, "[INF] " fmt "\n", ##__VA_ARGS__)
#define ERR(fmt, ...)  fprintf(stderr, "[ERR] " fmt "\n", ##__VA_ARGS__)
