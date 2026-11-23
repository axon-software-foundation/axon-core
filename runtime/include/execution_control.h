#pragma once
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <algorithm>
#include <vector>

namespace axon {

class ExecutionInterrupted : public std::runtime_error {
public:
    explicit ExecutionInterrupted(const std::string& message) : std::runtime_error(message) {}
};

// Copies inherit the cancellation token and can only shorten an existing deadline.
class ExecutionControl {
public:
    using Clock = std::chrono::steady_clock;
    explicit ExecutionControl(std::shared_ptr<std::atomic<bool>> cancelled =
                                  std::make_shared<std::atomic<bool>>(false))
        : _cancelled(std::move(cancelled)) {}

    ExecutionControl child(double seconds, const std::string& owner) const {
        if (!std::isfinite(seconds) || seconds < 0)
            throw std::runtime_error(owner + ": timeout must be finite and non-negative");
        ExecutionControl result(*this);
        if (seconds > 0) {
            const auto now = Clock::now();
            const double max_seconds = std::chrono::duration<double>(Clock::time_point::max() - now).count();
            if (seconds >= max_seconds)
                throw std::runtime_error(owner + ": timeout is too large");
            const auto end = now + std::chrono::duration_cast<Clock::duration>(
                                       std::chrono::duration<double>(seconds));
            if (end < result._deadline) {
                result._deadline = end;
                result._owner = owner;
            }
        }
        return result;
    }

    void check() const {
        if (_cancelled->load(std::memory_order_relaxed))
            throw ExecutionInterrupted("CancelledError: execution cancelled");
        for (const auto& token : _groups)
            if (token->load(std::memory_order_relaxed))
                throw ExecutionInterrupted("CancelledError: parallel sibling failed");
        if (Clock::now() >= _deadline)
            throw ExecutionInterrupted("TimeoutError: " + _owner + " deadline exceeded");
    }

    bool has_deadline() const { return _deadline != Clock::time_point::max(); }

    ExecutionControl linked_cancel(std::shared_ptr<std::atomic<bool>> token) const {
        ExecutionControl result(*this);
        result._groups.push_back(std::move(token));
        return result;
    }

    void sleep(double seconds) const {
        if (!std::isfinite(seconds) || seconds < 0)
            throw std::runtime_error("sleep: duration must be finite and non-negative");
        // child() also checks overflow before converting seconds to clock ticks.
        const auto end = seconds == 0 ? Clock::now() : child(seconds, "sleep")._deadline;
        for (;;) {
            check();
            const auto now = Clock::now();
            if (now >= end) return;
            std::this_thread::sleep_for(std::min(end - now,
                std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(10))));
        }
    }

private:
    std::shared_ptr<std::atomic<bool>> _cancelled;
    std::vector<std::shared_ptr<std::atomic<bool>>> _groups;
    Clock::time_point _deadline = Clock::time_point::max();
    std::string _owner;
};
} // namespace axon
