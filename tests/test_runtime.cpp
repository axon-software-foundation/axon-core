/**
 * tests/test_runtime.cpp
 * ───────────────────────
 * Phase 1 runtime tests.
 * Verifies: GraphEngine, Actor FSM, Scheduler, RuntimeMemory
 *
 * Compile: cmake --build build && ./build/axon_test_runtime
 */

#include <iostream>
#include <cassert>
#include <string>
#include <chrono>

// Include all implementations (header-only for Phase 1)
#include "../runtime/src/graph_engine.cpp"
#include "../runtime/src/actor.cpp"
#include "../runtime/src/scheduler.cpp"
#include "../runtime/src/runtime_memory.cpp"

using namespace axon;

/* ─── Helper ────────────────────────────────────────────────── */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "\n[TEST] " #name " ... " << std::flush; \
    try { \
        test_##name(); \
        std::cout << "PASS" << std::flush; \
        tests_passed++; \
    } catch (const std::exception& e) { \
        std::cout << "FAIL — " << e.what() << std::flush; \
        tests_failed++; \
    }

#define ASSERT(expr) \
    if (!(expr)) throw std::runtime_error("assert failed: " #expr " (" __FILE__ ":" + std::to_string(__LINE__) + ")")

/* ─────────────────────────────────────────────────────────────
 * Graph Engine Tests
 * ───────────────────────────────────────────────────────────── */
void test_graph_add_nodes() {
    GraphEngine g;
    g.add_node({"A"});
    g.add_node({"B"});
    g.add_node({"C"});
    ASSERT(g.node_count() == 3);
    ASSERT(g.edge_count() == 0);
}

void test_graph_add_edges() {
    GraphEngine g;
    g.add_edge({{"A"}, {"B"}, true, false, 0, ""});
    g.add_edge({{"B"}, {"C"}, true, false, 0, ""});
    ASSERT(g.node_count() == 3);
    ASSERT(g.edge_count() == 2);
}

void test_graph_bfs() {
    GraphEngine g;
    g.add_edge({{"A"}, {"B"}, true, false, 0, ""});
    g.add_edge({{"A"}, {"C"}, true, false, 0, ""});
    g.add_edge({{"B"}, {"D"}, true, false, 0, ""});

    auto order = g.bfs({"A"});
    ASSERT(order.size() == 4);
    ASSERT(order[0].value == "A");
}

void test_graph_dfs() {
    GraphEngine g;
    g.add_edge({{"A"}, {"B"}, true, false, 0, ""});
    g.add_edge({{"B"}, {"C"}, true, false, 0, ""});

    auto order = g.dfs({"A"});
    ASSERT(order.size() == 3);
    ASSERT(order[0].value == "A");
}

void test_graph_topological_sort() {
    GraphEngine g;
    g.add_edge({{"A"}, {"B"}, true, false, 0, ""});
    g.add_edge({{"B"}, {"C"}, true, false, 0, ""});
    g.add_edge({{"A"}, {"C"}, true, false, 0, ""});

    auto order = g.topological_order();
    ASSERT(order.size() == 3);
    // A must come before B and C
    auto pos = [&](const std::string& name) -> size_t {
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i].value == name) return i;
        return SIZE_MAX;
    };
    ASSERT(pos("A") < pos("B"));
    ASSERT(pos("A") < pos("C"));
}

void test_graph_no_cycle() {
    GraphEngine g;
    g.add_edge({{"A"}, {"B"}, true, false, 0, ""});
    g.add_edge({{"B"}, {"C"}, true, false, 0, ""});
    ASSERT(!g.has_cycle());
}

void test_graph_cycle_detected() {
    GraphEngine g;
    g.add_edge({{"A"}, {"B"}, true, false, 0, ""});
    g.add_edge({{"B"}, {"C"}, true, false, 0, ""});
    g.add_edge({{"C"}, {"A"}, false, true, 2, ""});   // retry cycle
    ASSERT(g.has_cycle());
}

void test_graph_successors() {
    GraphEngine g;
    g.add_edge({{"A"}, {"B"}, true, false, 0, ""});
    g.add_edge({{"A"}, {"C"}, true, false, 0, ""});
    auto s = g.successors({"A"});
    ASSERT(s.size() == 2);
}

/* ─────────────────────────────────────────────────────────────
 * Actor / State Machine Tests
 * ───────────────────────────────────────────────────────────── */
void test_actor_initial_state() {
    Actor a({"agent-1"});
    ASSERT(a.state() == AgentState::IDLE);
    ASSERT(a.id().value == "agent-1");
}

void test_actor_valid_transitions() {
    Actor a({"agent-1"});
    ASSERT(a.transition(AgentState::PENDING));
    ASSERT(a.transition(AgentState::READY));
    ASSERT(a.transition(AgentState::RUNNING));
    ASSERT(a.state() == AgentState::RUNNING);
}

void test_actor_invalid_transition() {
    Actor a({"agent-1"});
    // Cannot jump from IDLE to RUNNING directly
    ASSERT(!a.transition(AgentState::RUNNING));
    ASSERT(a.state() == AgentState::IDLE);   // state unchanged
}

void test_actor_run_and_success() {
    Actor a({"agent-1"});
    AxonValue input = AxonValue::from(std::string("hello"));
    AxonValue result = a.run(input);
    ASSERT(a.state() == AgentState::SUCCESS);
}

void test_actor_reset() {
    Actor a({"agent-1"});
    a.run(AxonValue::from(std::string("test")));
    a.reset();
    ASSERT(a.state() == AgentState::IDLE);
}

void test_actor_mailbox() {
    Actor a({"agent-1"});
    AgentMessage msg{{"sender"}, "input", AxonValue::from(42.0)};
    a.send(msg);
    auto recv = a.recv();
    ASSERT(recv.has_value());
    ASSERT(recv->type == "input");
}

void test_state_name() {
    ASSERT(agent_state_name(AgentState::IDLE)    == "IDLE");
    ASSERT(agent_state_name(AgentState::RUNNING) == "RUNNING");
    ASSERT(agent_state_name(AgentState::FLAGGED) == "FLAGGED");
}

/* ─────────────────────────────────────────────────────────────
 * Runtime Memory Tests
 * ───────────────────────────────────────────────────────────── */
void test_memory_store_retrieve() {
    RuntimeMemory mem;
    mem.store("key1", AxonValue::from(std::string("value1")));
    ASSERT(mem.exists("key1"));
    ASSERT(!mem.retrieve("key1").is_null());
}

void test_memory_forget() {
    RuntimeMemory mem;
    mem.store("key1", AxonValue::from(1.0));
    mem.forget("key1");
    ASSERT(!mem.exists("key1"));
    ASSERT(mem.retrieve("key1").is_null());
}

void test_memory_clear() {
    RuntimeMemory mem;
    mem.store("a", AxonValue::from(1.0));
    mem.store("b", AxonValue::from(2.0));
    mem.clear();
    ASSERT(mem.size() == 0);
}

void test_memory_type_name() {
    RuntimeMemory mem;
    ASSERT(mem.type_name() == "RuntimeMemory");
}

/* ─────────────────────────────────────────────────────────────
 * Session Memory Tests
 * ───────────────────────────────────────────────────────────── */
void test_session_create() {
    SessionMemory sm;
    auto id = sm.create();
    ASSERT(!id.value.empty());
    ASSERT(sm.session_count() == 1);
}

void test_session_append_history() {
    SessionMemory sm;
    auto id = sm.create();
    sm.append(id, {"user",      "Hello agent", 0});
    sm.append(id, {"assistant", "Hello user",  0});
    auto h = sm.history(id);
    ASSERT(h.size() == 2);
    ASSERT(h[0].role == "user");
    ASSERT(h[1].role == "assistant");
}

void test_session_ring_buffer() {
    SessionMemory sm(3);   // max 3 turns
    auto id = sm.create();
    for (int i = 0; i < 5; ++i)
        sm.append(id, {"user", "msg " + std::to_string(i), 0});

    auto h = sm.history(id);
    ASSERT(h.size() == 3);   // oldest 2 dropped
    ASSERT(h[0].content == "msg 2");
}

void test_session_close() {
    SessionMemory sm;
    auto id = sm.create();
    sm.close(id);
    ASSERT(sm.session_count() == 0);
}

/* ─────────────────────────────────────────────────────────────
 * Scheduler Tests
 * ───────────────────────────────────────────────────────────── */
void test_scheduler_run_agent() {
    auto actor = std::make_shared<Actor>(AgentID{"worker"});
    std::unordered_map<std::string, std::shared_ptr<IActor>> actors;
    actors["worker"] = actor;

    Scheduler sched(actors);
    sched.start(2);

    ScheduledTask task;
    task.agent_id    = {"worker"};
    task.input       = AxonValue::from(std::string("test input"));
    task.priority    = 1;
    task.max_retries = 0;

    sched.submit(task);
    AxonValue result = sched.await_result({"worker"});

    sched.stop();

    ASSERT(actor->state() == AgentState::SUCCESS);
}

void test_scheduler_queue_depth() {
    auto actor = std::make_shared<Actor>(AgentID{"worker"});
    std::unordered_map<std::string, std::shared_ptr<IActor>> actors;
    actors["worker"] = actor;

    Scheduler sched(actors);
    // Don't start — check queue accumulates
    ASSERT(sched.queue_depth() == 0);
}

void test_scheduler_stop_waiting_workers() {
    // Race shutdown against workers entering condition_variable::wait.
    // CTest's watchdog catches a lost wakeup; no timing threshold is asserted.
    for (int iteration = 0; iteration < 128; ++iteration) {
        Scheduler sched({});
        sched.start(2);
        std::this_thread::yield();
        sched.stop();
        sched.stop();  // shutdown remains idempotent
        ASSERT(sched.queue_depth() == 0);
    }
}

/* ─────────────────────────────────────────────────────────────
 * AxonValue Tests
 * ───────────────────────────────────────────────────────────── */
void test_value_null() {
    auto v = AxonValue::null();
    ASSERT(v.is_null());
    ASSERT(!v.is_truthy());
}

void test_value_bool() {
    auto t = AxonValue::from(true);
    auto f = AxonValue::from(false);
    ASSERT(!t.is_null());
    ASSERT(t.is_truthy());
    ASSERT(!f.is_truthy());
}

void test_value_number() {
    auto v = AxonValue::from(42.0);
    ASSERT(v.kind == AxonValue::Kind::Number);
    ASSERT(v.n == 42.0);
    ASSERT(v.is_truthy());
}

void test_value_zero_falsy() {
    auto v = AxonValue::from(0.0);
    ASSERT(!v.is_truthy());
}

void test_value_string() {
    auto v = AxonValue::from(std::string("hello"));
    ASSERT(v.kind == AxonValue::Kind::String);
    ASSERT(v.s == "hello");
    ASSERT(v.is_truthy());
}

/* ─────────────────────────────────────────────────────────────
 * Main
 * ───────────────────────────────────────────────────────────── */
int main() {
    std::cout << "═══════════════════════════════════════\n";
    std::cout << "  Axon Runtime — Phase 1 Tests\n";
    std::cout << "═══════════════════════════════════════\n";

    // Graph
    TEST(graph_add_nodes);
    TEST(graph_add_edges);
    TEST(graph_bfs);
    TEST(graph_dfs);
    TEST(graph_topological_sort);
    TEST(graph_no_cycle);
    TEST(graph_cycle_detected);
    TEST(graph_successors);

    // Actor
    TEST(actor_initial_state);
    TEST(actor_valid_transitions);
    TEST(actor_invalid_transition);
    TEST(actor_run_and_success);
    TEST(actor_reset);
    TEST(actor_mailbox);
    TEST(state_name);

    // Memory
    TEST(memory_store_retrieve);
    TEST(memory_forget);
    TEST(memory_clear);
    TEST(memory_type_name);

    // Session
    TEST(session_create);
    TEST(session_append_history);
    TEST(session_ring_buffer);
    TEST(session_close);

    // Scheduler
    TEST(scheduler_run_agent);
    TEST(scheduler_queue_depth);
    TEST(scheduler_stop_waiting_workers);

    // Values
    TEST(value_null);
    TEST(value_bool);
    TEST(value_number);
    TEST(value_zero_falsy);
    TEST(value_string);

    std::cout << "\n\n═══════════════════════════════════════\n";
    std::cout << "  Results: " << tests_passed << " passed, "
              << tests_failed << " failed\n";
    std::cout << "═══════════════════════════════════════\n";

    return tests_failed > 0 ? 1 : 0;
}
