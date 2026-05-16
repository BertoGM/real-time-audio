#pragma once
#include <map>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <optional>
#include <cstdint>

struct JitterPacket {
    uint32_t seq;
    uint32_t timestamp_ms;
    std::vector<int16_t> pcm;
};

class JitterBuffer {
    public:
        explicit JitterBuffer(int jitter_ms = 80)
            : jitter_ms_(jitter_ms), expected_seq_(0), initialized_(false), closed_(false) {}
    
        void push(JitterPacket packet) {
            std::unique_lock lock(mu_);
            uint32_t seq_id = packet.seq;
            buf_.emplace(seq_id, std::move(packet));
            cv_.notify_one();
        }

        std::optional<JitterPacket> pop() {
            std::unique_lock lock(mu_);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(jitter_ms_);
            cv_.wait_until(lock, deadline, [this] {
                return closed_ || (!buf_.empty() && (!initialized_ || buf_.begin()->first == expected_seq_));
            });
            if (closed_ && buf_.empty()) {
                return std::nullopt;
            }

            if (buf_.empty()) {
                if (initialized_) {
                    ++expected_seq_;
                }
                return std::nullopt;
            }

            auto it = buf_.begin();
            if (!initialized_){
                expected_seq_ = it->first;
                initialized_ = true;
            }

            if (it->first == expected_seq_) {
                JitterPacket packet = std::move(it->second);
                buf_.erase(it);
                ++expected_seq_;
                return packet;
            }

            ++expected_seq_;
            return std::nullopt;
        }

        void close() {
            std::unique_lock lock(mu_);
            closed_ = true;
            cv_.notify_all();
        }

        size_t size() const {
            std::unique_lock lock(mu_);
            return buf_.size();
        }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::map<uint32_t, JitterPacket> buf_;
    int jitter_ms_;
    uint32_t expected_seq_;
    bool initialized_;
    bool closed_;
};