/**
 * runtime/src/scheduler.cpp
 * ──────────────────────────
 * IScheduler implementation — real C++ thread pool.
 *
 * No Python GIL. Agents run in parallel as native OS threads.
 * Uses a priority queue so high-priority tasks run first.
 * Supports retry, cancellation, and result collection.
 */

#pragma once
#include "../interfaces/interfaces.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <future>
#include <functional>
#include <chrono>
#include <iostream>

namespace axon {

/* ─── Priority queue comparator ────────────────────────────── */
struct TaskPriority {
    bool operator()(const ScheduledTask& a, const ScheduledTask& b) const {
        return a.priority < b.priority;   // higher priority = first
    }
};

/* ─── Scheduler ─────────────────────────────────────────────── */
class Scheduler final : public IScheduler {
public:
    explicit Scheduler(
        std::unordered_map<std::string, std::shared_ptr<IActor>> actors)
        : _actors(std::move(actors))
        , _running(false)
    {}

    ~Scheduler() { stop(); }

    /* ── IScheduler interface ─────────────────────────────── */
    void start(size_t thread_count = 0) override {
        if (_running.load()) return;

        size_t n = thread_count > 0
            ? thread_count
            : std::max(1u, std::thread::hardware_concurrency());

        _running.store(true);

        for (size_t i = 0; i < n; ++i) {
            _pool.emplace_back([this]{ worker_loop(); });
        }

        std::cout << "[Scheduler] started with " << n << " threads\n";
    }

    void stop() override {
        {
            // The wait predicate must change under the same mutex as wait().
            // Otherwise a worker can see running=true, miss notify_all(), and
            // sleep forever while stop() waits in join().
            std::lock_guard<std::mutex> lk(_queue_mu);
            if (!_running.exchange(false)) return;
        }
        _cv.notify_all();
        for (auto& t : _pool) if (t.joinable()) t.join();
        _pool.clear();
        std::cout << "[Scheduler] stopped\n";
    }

    void submit(const ScheduledTask& task) override {
        {
            std::lock_guard<std::mutex> lk(_queue_mu);
            _queue.push(task);

            // Create a promise for result collection
            auto p = std::make_shared<std::promise<AxonValue>>();
            _results[task.agent_id.value] = p->get_future().share();
            _promises[task.agent_id.value] = std::move(p);
        }
        _cv.notify_one();
    }

    AxonValue await_result(const AgentID& id) override {
        std::shared_future<AxonValue> fut;
        {
            std::lock_guard<std::mutex> lk(_queue_mu);
            auto it = _results.find(id.value);
            if (it == _results.end())
                throw std::runtime_error(
                    "Scheduler: no result for agent '" + id.value + "'");
            fut = it->second;
        }
        return fut.get();
    }

    void cancel(const AgentID& id) override {
        std::lock_guard<std::mutex> lk(_queue_mu);
        _cancelled.insert(id.value);
    }

    size_t queue_depth() const override {
        std::lock_guard<std::mutex> lk(_queue_mu);
        return _queue.size();
    }

private:
    /* ── Worker thread loop ──────────────────────────────── */
    void worker_loop() {
        while (true) {
            ScheduledTask task;

            {
                std::unique_lock<std::mutex> lk(_queue_mu);
                _cv.wait(lk, [this]{
                    return !_running.load() || !_queue.empty();
                });

                if (!_running.load() && _queue.empty()) return;
                if (_queue.empty()) continue;

                task = _queue.top();
                _queue.pop();
            }

            // Check cancellation
            if (_cancelled.count(task.agent_id.value)) {
                resolve(task.agent_id, AxonValue::null());
                continue;
            }

            // Execute
            auto it = _actors.find(task.agent_id.value);
            if (it == _actors.end()) {
                std::cerr << "[Scheduler] unknown agent '"
                          << task.agent_id.value << "'\n";
                resolve(task.agent_id, AxonValue::null());
                continue;
            }

            auto& actor = it->second;
            int   attempts = 0;

            while (attempts <= task.max_retries) {
                try {
                    AxonValue result = actor->run(task.input);

                    if (actor->state() == AgentState::FLAGGED) {
                        std::cerr << "[Scheduler] agent '"
                                  << task.agent_id.value
                                  << "' FLAGGED by validator\n";
                    }

                    resolve(task.agent_id, result);
                    break;

                } catch (const std::exception& e) {
                    ++attempts;
                    std::cerr << "[Scheduler] agent '" << task.agent_id.value
                              << "' failed (attempt " << attempts << "): "
                              << e.what() << "\n";

                    if (attempts > task.max_retries) {
                        resolve(task.agent_id, AxonValue::null());
                    } else {
                        actor->reset();
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(100 * attempts));
                    }
                }
            }
        }
    }

    void resolve(const AgentID& id, AxonValue value) {
        std::lock_guard<std::mutex> lk(_queue_mu);
        auto it = _promises.find(id.value);
        if (it != _promises.end()) {
            it->second->set_value(std::move(value));
            _promises.erase(it);
        }
    }

    /* ── State ───────────────────────────────────────────── */
    std::unordered_map<std::string, std::shared_ptr<IActor>> _actors;
    std::atomic<bool>    _running;
    std::vector<std::thread> _pool;

    mutable std::mutex   _queue_mu;
    std::condition_variable _cv;

    std::priority_queue<
        ScheduledTask,
        std::vector<ScheduledTask>,
        TaskPriority>    _queue;

    std::unordered_map<std::string,
        std::shared_future<AxonValue>>  _results;
    std::unordered_map<std::string,
        std::shared_ptr<std::promise<AxonValue>>> _promises;
    std::unordered_set<std::string>  _cancelled;
};

} // namespace axon
