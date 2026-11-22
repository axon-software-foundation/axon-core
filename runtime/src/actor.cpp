/**
 * runtime/src/actor.cpp
 * ──────────────────────
 * Concrete IActor implementation.
 *
 * Each agent in Axon is an isolated Actor:
 *   - Own state machine (IDLE → ... → SUCCESS/FAILED)
 *   - Own mailbox (lock-free ring buffer for messages)
 *   - Own memory scope
 *   - Own validation config
 *
 * Thread safety: mailbox uses mutex + condition_variable.
 * The actor can be run from any thread in the scheduler pool.
 */

#pragma once
#include "../interfaces/interfaces.hpp"
#include <mutex>
#include <condition_variable>
#include <deque>
#include <stdexcept>
#include <iostream>

namespace axon {

/* ─── State machine transition table ───────────────────────── */
static bool state_transition_allowed(AgentState from, AgentState to) {
    using S = AgentState;
    // clang-format off
    switch (from) {
        case S::IDLE:       return to == S::PENDING;
        case S::PENDING:    return to == S::READY || to == S::FAILED;
        case S::READY:      return to == S::RUNNING || to == S::FAILED;
        case S::RUNNING:    return to == S::CHECKPOINT || to == S::EMITTING
                                || to == S::FAILED;
        case S::CHECKPOINT: return to == S::EMITTING  || to == S::RUNNING
                                || to == S::FAILED;
        case S::EMITTING:   return to == S::SUCCESS   || to == S::FLAGGED
                                || to == S::FAILED;
        case S::SUCCESS:    return to == S::IDLE;        // ready for reuse
        case S::FAILED:     return to == S::IDLE         // retry
                                || to == S::PENDING;
        case S::FLAGGED:    return to == S::IDLE
                                || to == S::RUNNING;     // retry after flag
        default:            return false;
    }
    // clang-format on
}

inline std::string agent_state_name(AgentState s) {
    switch (s) {
        case AgentState::IDLE:       return "IDLE";
        case AgentState::PENDING:    return "PENDING";
        case AgentState::READY:      return "READY";
        case AgentState::RUNNING:    return "RUNNING";
        case AgentState::CHECKPOINT: return "CHECKPOINT";
        case AgentState::EMITTING:   return "EMITTING";
        case AgentState::SUCCESS:    return "SUCCESS";
        case AgentState::FAILED:     return "FAILED";
        case AgentState::FLAGGED:    return "FLAGGED";
        default:                     return "UNKNOWN";
    }
}

/* ─── Mailbox ───────────────────────────────────────────────── */
class Mailbox {
public:
    void push(AgentMessage msg) {
        {
            std::lock_guard<std::mutex> lk(_mu);
            _queue.push_back(std::move(msg));
        }
        _cv.notify_one();
    }

    std::optional<AgentMessage> try_pop() {
        std::lock_guard<std::mutex> lk(_mu);
        if (_queue.empty()) return std::nullopt;
        auto msg = std::move(_queue.front());
        _queue.pop_front();
        return msg;
    }

    AgentMessage wait_pop(int timeout_ms = -1) {
        std::unique_lock<std::mutex> lk(_mu);
        if (timeout_ms < 0) {
            _cv.wait(lk, [this]{ return !_queue.empty(); });
        } else {
            bool ok = _cv.wait_for(lk,
                std::chrono::milliseconds(timeout_ms),
                [this]{ return !_queue.empty(); });
            if (!ok) throw std::runtime_error("Mailbox timeout");
        }
        auto msg = std::move(_queue.front());
        _queue.pop_front();
        return msg;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(_mu);
        return _queue.size();
    }

private:
    mutable std::mutex           _mu;
    std::condition_variable      _cv;
    std::deque<AgentMessage>     _queue;
};

/* ─── Checkpoint ────────────────────────────────────────────── */
struct AgentCheckpoint {
    AgentState              state;
    AxonValue               last_output;
    std::string             session_snapshot;   // serialized session slice
    int64_t                 timestamp_ms;
    int                     retry_count;
};

/* ─── Actor ─────────────────────────────────────────────────── */
class Actor final : public IActor {
public:
    explicit Actor(AgentID id,
                   ValidationConfig validation = {},
                   std::shared_ptr<IValidator> validator = nullptr)
        : _id(std::move(id))
        , _state(AgentState::IDLE)
        , _validation(std::move(validation))
        , _validator(std::move(validator))
        , _retry_count(0)
    {}

    /* ── IActor interface ────────────────────────────────── */
    AgentID    id()    const override { return _id; }
    AgentState state() const override {
        std::lock_guard<std::mutex> lk(_state_mu);
        return _state;
    }

    bool transition(AgentState next) override {
        std::lock_guard<std::mutex> lk(_state_mu);
        if (!state_transition_allowed(_state, next)) {
            std::cerr << "[Actor " << _id.value << "] illegal transition "
                      << agent_state_name(_state) << " -> "
                      << agent_state_name(next) << "\n";
            return false;
        }
        _state = next;
        return true;
    }

    void send(const AgentMessage& msg) override {
        _mailbox.push(msg);
    }

    std::optional<AgentMessage> recv() override {
        return _mailbox.try_pop();
    }

    /* ── Core execution ──────────────────────────────────── */
    AxonValue run(const AxonValue& input) override {
        transition(AgentState::PENDING);
        transition(AgentState::READY);
        transition(AgentState::RUNNING);

        AxonValue output = execute(input);   // subclass hook

        // Checkpoint after execution
        checkpoint();
        transition(AgentState::EMITTING);

        // Validate output
        if (_validator) {
            auto result = _validator->validate(output, _validation);
            if (!result.passed) {
                if (_retry_count < _validation.max_retries) {
                    _retry_count++;
                    transition(AgentState::FAILED);
                    transition(AgentState::IDLE);
                    return run(input);   // retry
                }
                transition(AgentState::FLAGGED);
                _last_checkpoint.state = AgentState::FLAGGED;
                return AxonValue::null();
            }
        }

        _retry_count = 0;
        transition(AgentState::SUCCESS);
        return output;
    }

    void checkpoint() override {
        std::lock_guard<std::mutex> lk(_state_mu);
        _last_checkpoint = {
            _state,
            _last_output,
            "",             // session snapshot: Phase 2
            current_time_ms(),
            _retry_count,
        };
    }

    void reset() override {
        std::lock_guard<std::mutex> lk(_state_mu);
        _state       = AgentState::IDLE;
        _retry_count = 0;
        _last_output = AxonValue::null();
    }

    /* ── Metrics exposure ────────────────────────────────── */
    int retry_count() const { return _retry_count; }
    const AgentCheckpoint& last_checkpoint() const { return _last_checkpoint; }

protected:
    // Override in concrete agent implementations
    virtual AxonValue execute(const AxonValue& input) {
        // Base: echo input (will be replaced by real execution in Phase 2)
        _last_output = input;
        return input;
    }

private:
    static int64_t current_time_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
    }

    AgentID                     _id;
    AgentState                  _state;
    mutable std::mutex          _state_mu;
    Mailbox                     _mailbox;
    ValidationConfig            _validation;
    std::shared_ptr<IValidator> _validator;
    int                         _retry_count;
    AxonValue                   _last_output;
    AgentCheckpoint             _last_checkpoint;
};

} // namespace axon
