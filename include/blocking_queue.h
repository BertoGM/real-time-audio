#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>

template<typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(size_t max_size = 64) :
        max_size_(max_size), closed_(false) {}

    bool push(T item) {
        std::unique_lock lock(mu_);
        not_full_.wait(lock, [this] {
            return queue_.size() < max_size_ || closed_;
        });
        if (closed_) {
            return false;
        }
        queue_.push(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock lock(mu_);
        not_empty_.wait(lock, [this] {
            return !queue_.empty() || closed_;
        });
        if (queue_.empty()) {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return item;
    }

    std::optional<T> try_pop() {
        std::unique_lock lock(mu_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return item;
    }

    void close() {
        std::unique_lock lcok(mu_);
        closed_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    size_t size() const {
        std::unique_lock lock(mu_);
        return queue_.size();
    }

    bool empty() const {
        std::unique_lock lock(mu_);
        return queue_.empty();
    }

    private:
    mutable std::mutex mu_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::queue<T> queue_;
    size_t max_size_;
    bool closed_;
};