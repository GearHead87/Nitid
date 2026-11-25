#pragma once

// Fixed-capacity blocking queue between the decoder thread and the render thread.
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace nitid {

template <class T> class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity) : cap_(capacity) {}

    // Blocks while full. Returns false if the queue was closed.
    bool push(T v)
    {
        std::unique_lock l(m_);
        not_full_.wait(l, [&] { return closed_ || q_.size() < cap_; });
        if (closed_) return false;
        q_.push_back(std::move(v));
        not_empty_.notify_one();
        return true;
    }

    // Blocks while empty. Returns nullopt when closed and drained.
    std::optional<T> pop()
    {
        std::unique_lock l(m_);
        not_empty_.wait(l, [&] { return closed_ || !q_.empty(); });
        if (q_.empty()) return std::nullopt;
        T v = std::move(q_.front());
        q_.pop_front();
        not_full_.notify_one();
        return v;
    }

    // Producer is done: pop() drains what is left, then returns nullopt.
    void finish()
    {
        std::lock_guard l(m_);
        closed_ = true;  // wakes the consumer; push() is no longer called after finish()
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    // Abort: drop everything and wake both sides.
    void close()
    {
        std::lock_guard l(m_);
        closed_ = true;
        q_.clear();
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    void reopen()
    {
        std::lock_guard l(m_);
        closed_ = false;
        q_.clear();
    }

private:
    size_t cap_;
    std::mutex m_;
    std::condition_variable not_empty_, not_full_;
    std::deque<T> q_;
    bool closed_ = false;
};

} // namespace nitid
