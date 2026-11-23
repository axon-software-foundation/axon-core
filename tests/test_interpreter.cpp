/**
 * tests/test_interpreter.cpp
 * ───────────────────────────
 * Interpreter tests — no Ollama required.
 * All agents use control flow only (no LLM calls).
 *
 * Run: ./build/axon_test_interp
 */

#include <iostream>
#include <string>
#include <cassert>
#include <sstream>
#include <chrono>
#include <vector>
#include "../runtime/include/lexer.h"
#include "../runtime/include/axon_parser.h"
#include "../runtime/include/interpreter.h"

using namespace axon;

// ── Test helpers ─────────────────────────────────────────────
static int passed = 0, failed = 0;

#define TEST(name) \
    std::cout << "[TEST] " #name " ... "; \
    try { test_##name(); std::cout << "\033[32mPASS\033[0m\n"; passed++; } \
    catch (const std::exception& e) { \
        std::cout << "\033[31mFAIL\033[0m — " << e.what() << "\n"; failed++; }

#define ASSERT_EQ(a,b) do { \
    if ((a) != (b)) { \
        std::ostringstream _ss; \
        _ss << "expected [" << (b) << "] got [" << (a) << "] at line " \
            << __LINE__; \
        throw std::runtime_error(_ss.str()); \
    } } while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) throw std::runtime_error( \
        std::string("assertion failed: " #x " at line ") \
        + std::to_string(__LINE__)); \
    } while(0)

// ── Run source string and return last value ───────────────────
static Value run(const std::string& src) {
    std::unique_ptr<AxonArena, decltype(&axon_arena_free)> arena(
        axon_arena_new(64 * 1024), &axon_arena_free);
    Lexer lexer(src, "<test>");
    auto tokens = lexer.tokenize();
    if (lexer.has_errors()) {
        const auto& error = lexer.errors().front();
        throw std::runtime_error("lex error line " + std::to_string(error.line) + ": " + error.message);
    }
    Parser parser(std::move(tokens), arena.get());
    AxonNode* prog = parser.parse_program();
    if (parser.has_errors()) {
        const auto& error = parser.errors().front();
        throw std::runtime_error("parse error line " + std::to_string(error.line) + ": " + error.message);
    }
    Interpreter interp;
    interp.telemetry = false;
    return interp.run(prog);
}

// A recording adapter is used ONLY in tests to inspect actual LLM request inputs.
class RecordingProvider final : public IProviderAdapter {
public:
    std::vector<LLMRequest> requests;
    std::vector<std::string> replies;
    std::string provider_name() const override { return "ollama"; }
    bool is_available() override { return true; }
    LLMResponse complete(const LLMRequest& request) override {
        requests.push_back(request);
        LLMResponse response{};
        response.content = replies.size() >= requests.size() ? replies[requests.size()-1]
                           : "reply-" + std::to_string(requests.size());
        return response;
    }
    std::string user(size_t index) const {
        for (const auto& message : requests.at(index).messages)
            if (message.role == "user") return message.content;
        return "<no user message>";
    }
};

static Value run_recorded(const std::string& source,
                          const std::shared_ptr<IProviderAdapter>& provider) {
    std::unique_ptr<AxonArena, decltype(&axon_arena_free)> arena(
        axon_arena_new(256 * 1024), &axon_arena_free);
    Lexer lexer(source, "<recorded-test>");
    auto tokens = lexer.tokenize();
    if (lexer.has_errors()) throw std::runtime_error(lexer.errors().front().message);
    Parser parser(std::move(tokens), arena.get());
    AxonNode* program = parser.parse_program();
    if (parser.has_errors()) throw std::runtime_error(parser.errors().front().message);
    Interpreter interpreter;
    interpreter.telemetry = false;
    interpreter.register_provider(provider);
    return interpreter.run(program);
}

static std::string processor(const std::string& tasks) {
    return "flow F { input=\"FLOW\" debug=false } "
           "agent Processor { provider ollama model llama3.2 " + tasks + " } ";
}

static void expect_error(const std::string& source, const std::string& expected) {
    bool caught = false;
    try { run(source); }
    catch (const std::runtime_error& error) {
        caught = std::string(error.what()).find(expected) != std::string::npos;
    }
    ASSERT_TRUE(caught);
}

static std::string run_str(const std::string& src) {
    Value v = run(src);
    if (v.kind == Value::Kind::String) return v.s;
    if (v.kind == Value::Kind::Number) {
        if (v.n == (long long)v.n) return std::to_string((long long)v.n);
        return std::to_string(v.n);
    }
    if (v.kind == Value::Kind::Bool) return v.b ? "true" : "false";
    return "null";
}

// ─────────────────────────────────────────────────────────────
// 1. Arithmetic expressions
// ─────────────────────────────────────────────────────────────
void test_arithmetic_add() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return 10 + 5 } }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(v.n, 15.0);
}

void test_arithmetic_subtract() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return 20 - 7 } }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(v.n, 13.0);
}

void test_arithmetic_multiply() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return 6 * 7 } }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(v.n, 42.0);
}

void test_arithmetic_divide() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return 10 / 4 } }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(v.n, 2.5);
}

void test_string_concat() {
    auto s = run_str(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return "hello" + " " + "world" } }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(s, std::string("hello world"));
}

// ─────────────────────────────────────────────────────────────
// 2. Variables and assignment
// ─────────────────────────────────────────────────────────────
void test_variable_assignment() {
    auto s = run_str(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                x = "axon"
                y = " rocks"
                return x + y
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(s, std::string("axon rocks"));
}

void test_variable_reassign() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                n = 1
                n = n + 1
                n = n + 1
                return n
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(v.n, 3.0);
}

// ─────────────────────────────────────────────────────────────
// 3. Control flow — if/else
// ─────────────────────────────────────────────────────────────
void test_if_true() {
    auto s = run_str(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                if 10 > 5 { return "yes" } else { return "no" }
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(s, std::string("yes"));
}

void test_if_false() {
    auto s = run_str(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                if 3 > 9 { return "yes" } else { return "no" }
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(s, std::string("no"));
}

void test_nested_if() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                x = 15
                if x > 10 {
                    if x > 20 { return 3 } else { return 2 }
                } else {
                    return 1
                }
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(v.n, 2.0);
}

// ─────────────────────────────────────────────────────────────
// 4. Control flow — for loop
// ─────────────────────────────────────────────────────────────
void test_for_loop_count() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                items = [1, 2, 3, 4, 5]
                total = 0
                for n in items { total = total + n }
                return total
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(v.n, 15.0);
}

void test_for_loop_string() {
    auto s = run_str(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                words = ["hello", " ", "world"]
                result = ""
                for w in words { result = result + w }
                return result
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(s, std::string("hello world"));
}

// ─────────────────────────────────────────────────────────────
// 5. Control flow — while loop
// ─────────────────────────────────────────────────────────────
void test_while_loop() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                i = 0
                sum = 0
                while i < 5 {
                    sum = sum + i
                    i = i + 1
                }
                return sum
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(v.n, 10.0);  // 0+1+2+3+4
}

// ─────────────────────────────────────────────────────────────
// 6. Control flow — match
// ─────────────────────────────────────────────────────────────
void test_match_string() {
    auto s = run_str(R"(
        flow F { input = "approved" }
        agent A { provider ollama  model llama3
            main {
                status = "approved"
                match status {
                    "approved" -> return "ok"
                    "rejected" -> return "no"
                    _          -> return "unknown"
                }
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(s, std::string("ok"));
}

void test_match_wildcard() {
    auto s = run_str(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                x = "something_else"
                match x {
                    "a" -> return "got a"
                    "b" -> return "got b"
                    _   -> return "wildcard"
                }
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(s, std::string("wildcard"));
}

// ─────────────────────────────────────────────────────────────
// 7. Control flow — try/catch
// ─────────────────────────────────────────────────────────────
void test_try_catch_raise() {
    auto s = run_str(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                try {
                    raise "TestError: boom"
                } catch err {
                    return "caught: " + err
                }
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(s, std::string("caught: TestError: boom"));
}

void test_try_no_raise() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                result = 0
                try {
                    result = 42
                } catch err {
                    result = -1
                }
                return result
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(v.n, 42.0);
}

// ─────────────────────────────────────────────────────────────
// 8. Agent chaining — flow.input + __input__
// ─────────────────────────────────────────────────────────────
void test_agent_chain_input() {
    auto s = run_str(R"(
        flow F { input = "start" }
        agent A { provider ollama  model llama3
            main { return __input__ + "-A" }
        }
        agent B { provider ollama  model llama3
            main { return __input__ + "-B" }
        }
        agent C { provider ollama  model llama3
            main { return __input__ + "-C" }
        }
        graph {
            A -> B [on_success]
            B -> C [on_success]
            C -> done
        }
    )");
    ASSERT_EQ(s, std::string("start-A-B-C"));
}

void test_flow_input() {
    auto s = run_str(R"(
        flow F { input = "flow_data" }
        agent A { provider ollama  model llama3
            main { return flow.input }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(s, std::string("flow_data"));
}

// ─────────────────────────────────────────────────────────────
// 9. Data types
// ─────────────────────────────────────────────────────────────
void test_bool_true() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return true }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_TRUE(v.kind == Value::Kind::Bool && v.b == true);
}

void test_bool_false() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return false }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_TRUE(v.kind == Value::Kind::Bool && v.b == false);
}

void test_null_value() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return null }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_TRUE(v.is_null());
}

void test_list_literal() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                items = [10, 20, 30]
                return items
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_TRUE(v.kind == Value::Kind::List);
    ASSERT_EQ(v.list.size(), (size_t)3);
    ASSERT_EQ(v.list[0].n, 10.0);
    ASSERT_EQ(v.list[2].n, 30.0);
}

void test_list_index() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                items = [11, 22, 33]
                return items[1]
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(v.n, 22.0);
}

// ─────────────────────────────────────────────────────────────
// 10. Method calls
// ─────────────────────────────────────────────────────────────
void test_isempty_true() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                s = ""
                return s.isempty()
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_TRUE(v.kind == Value::Kind::Bool && v.b == true);
}

void test_isempty_false() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main {
                s = "hello"
                return s.isempty()
            }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_TRUE(v.kind == Value::Kind::Bool && v.b == false);
}

void test_string_length() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return "hello".length() }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_EQ(v.n, 5.0);
}

// ─────────────────────────────────────────────────────────────
// 11. Logic operators
// ─────────────────────────────────────────────────────────────
void test_logic_and() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return true and false }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_TRUE(v.kind == Value::Kind::Bool && v.b == false);
}

void test_logic_or() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return false or true }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_TRUE(v.kind == Value::Kind::Bool && v.b == true);
}

void test_logic_not() {
    auto v = run(R"(
        flow F { input = "x" }
        agent A { provider ollama  model llama3
            main { return not true }
        }
        graph { A -> done [on_success] }
    )");
    ASSERT_TRUE(v.kind == Value::Kind::Bool && v.b == false);
}

// ─────────────────────────────────────────────────────────────
// 12. run task_graph
// ─────────────────────────────────────────────────────────────
void test_run_task_graph() {
    // Without Ollama, task_graph runs but LLM calls are skipped.
    // Should complete without crashing.
    Value v = run(R"(
        flow F { input = "data" }
        agent Pipeline {
            provider ollama
            model    llama3
            task Step1 { input = flow.input  output = json }
            task Step2 { input = Step1.output  output = json }
            task_graph {
                Step1 -> Step2 [on_success]
                Step2 -> done
            }
            main { run task_graph  return "done" }
        }
        graph { Pipeline -> done [on_success] }
    )");
    // Result is "done" because no LLM was called
    ASSERT_TRUE(v.kind == Value::Kind::String);
    ASSERT_EQ(v.s, std::string("done"));
}

// ─────────────────────────────────────────────────────────────
// 13. Graph execution order
// ─────────────────────────────────────────────────────────────
void test_graph_execution_order() {
    auto s = run_str(R"(
        flow F { input = "" }
        agent First  { provider ollama  model llama3  main { return "1" } }
        agent Second { provider ollama  model llama3  main { return __input__ + "2" } }
        agent Third  { provider ollama  model llama3  main { return __input__ + "3" } }
        graph {
            First  -> Second [on_success]
            Second -> Third  [on_success]
            Third  -> done
        }
    )");
    ASSERT_EQ(s, std::string("123"));
}

void test_function_basic() {
    ASSERT_EQ(run("func add(a,b) { return a+b } main { return add(20,22) }").n, 42.0);
}
void test_function_recursion() {
    ASSERT_EQ(run("func fact(n) { if n <= 1 { return 1 } return n*fact(n-1) } main { return fact(6) }").n, 720.0);
}
void test_function_anonymous() {
    ASSERT_EQ(run("main { f=func(x) { return x*3 } return f(7) }").n, 21.0);
}
void test_function_arrow() {
    ASSERT_EQ(run("main { twice=(x)=>x*2 return twice(21) }").n, 42.0);
}
void test_function_closure() {
    ASSERT_EQ(run("func make(x) { return func(y) { return x+y } } main { f=make(10) return f(5) }").n, 15.0);
}
void test_function_independent_closures() {
    ASSERT_EQ(run("func make(x) { return (y)=>x*y } main { a=make(3) b=make(10) return a(2)+b(2) }").n, 26.0);
}
void test_function_chained_call() {
    ASSERT_EQ(run("func make(x) { return (y)=>x*y } main { return make(4)(6) }").n, 24.0);
}
void test_function_higher_order() {
    ASSERT_EQ(run("func invoke(f,x) { return f(x) } main { return invoke((x)=>x*x,7) }").n, 49.0);
}
void test_function_nested_scope() {
    ASSERT_EQ(run("func outer(x) { factor=3 func inner(y) { return y*factor } return inner(x)+x } main { return outer(5) }").n, 20.0);
}
void test_function_parameter_shadowing() {
    ASSERT_EQ(run("func f(x) { return x+1 } main { x=100 y=f(4) return x+y }").n, 105.0);
}
void test_function_early_return() {
    ASSERT_EQ(run("func f(n) { if n > 0 { return 9 } return 1 } main { return f(3) }").n, 9.0);
}

static void expect_arity_error(const std::string& source, const std::string& detail) {
    try { run(source); }
    catch (const std::runtime_error& error) {
        ASSERT_TRUE(std::string(error.what()).find("uncaught raise: ArityError: " + detail)
                    != std::string::npos);
        return;
    }
    throw std::runtime_error("expected an arity error");
}

void test_function_arity_missing() {
    expect_arity_error("func add(a,b) { return a+b } main { add(1) }",
                       "function 'add' expects 2 argument(s), got 1");
}
void test_function_arity_extra() {
    expect_arity_error("func f(x) { return x } main { f(1,2) }",
                       "function 'f' expects 1 argument(s), got 2");
}
void test_function_arity_anonymous_and_arrow() {
    expect_arity_error("main { f=func(x) { return x } f() }",
                       "anonymous function expects 1 argument(s), got 0");
    expect_arity_error("main { f=(x)=>x f(1,2) }",
                       "anonymous function expects 1 argument(s), got 2");
}
void test_function_arity_closure_and_chained() {
    const std::string factory = "func make(x) { return (y)=>x+y } ";
    expect_arity_error(factory + "main { f=make(10) f() }",
                       "anonymous function expects 1 argument(s), got 0");
    expect_arity_error(factory + "main { make(10)(1,2) }",
                       "anonymous function expects 1 argument(s), got 2");
}
void test_function_arity_higher_order() {
    expect_arity_error("func invoke(f) { return f() } main { invoke((x)=>x) }",
                       "anonymous function expects 1 argument(s), got 0");
}
void test_function_arity_null_and_zero() {
    ASSERT_EQ(run("func identity(x) { return x } main { return typeof(identity(null)) }").s,
              std::string("null"));
    ASSERT_EQ(run("func f() { return 42 } main { return f() }").n, 42.0);
    expect_arity_error("func f() { return 42 } main { f(null) }",
                       "function 'f' expects 0 argument(s), got 1");
}
void test_function_arity_catch_and_recovery() {
    ASSERT_EQ(run(R"(
        func add(a,b) { return a+b }
        main {
            caught = false
            try { add(1) } catch err {
                if err.find("ArityError:") != 0 { raise "wrong error" }
                caught = true
            }
            if caught == false { raise "arity error was not caught" }
            return add(20,22)
        }
    )").n, 42.0);
}
void test_function_arity_rejects_before_side_effects() {
    auto provider = std::make_shared<RecordingProvider>();
    const std::string source = processor("task Analyze { prompt=[\"record\"] }") + R"(
        func f(x) { Processor("body") return x }
        main {
            try { f(Processor("first"), Processor("extra")) }
            catch err { return err }
        }
    )";
    ASSERT_TRUE(run_recorded(source, provider).s.find("ArityError:") == 0);
    ASSERT_TRUE(provider->requests.empty());
}
void test_hash_sha256_vector() {
    ASSERT_EQ(run("main { return hash->sha256(\"abc\") }").s,
              std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}
void test_hash_md5_vector() {
    ASSERT_EQ(run("main { return hash->md5(\"abc\") }").s,
              std::string("900150983cd24fb0d6963f7d28e17f72"));
}
void test_hash_unsupported_rejected() {
    for (const std::string name : {"sha1", "sha512"}) {
        bool rejected = false;
        try { run("main { return hash->" + name + "(\"abc\") }"); }
        catch (const std::runtime_error& error) {
            rejected = std::string(error.what()).find("is not implemented") != std::string::npos;
        }
        ASSERT_TRUE(rejected);
    }
}
void test_codec_roundtrip() {
    ASSERT_EQ(run("main { return codec->b64_decode(codec->b64_encode(\"Axon 123\")) }").s,
              std::string("Axon 123"));
}
void test_string_find_literal_contract() {
    ASSERT_EQ(run("main { s=\"hello 123\" return s.find(\"123\") }").n, 6.0);
    ASSERT_EQ(run("main { s=\"hello 123\" return s.find(\"[0-9]+\") }").n, -1.0);
}

void test_uncaught_raise_is_host_error() {
    bool caught = false;
    try { run("main { raise \"top-level failure\" }"); }
    catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "uncaught raise: top-level failure";
    }
    ASSERT_TRUE(caught);
}
void test_uncaught_function_raise_is_host_error() {
    bool caught = false;
    try { run("func fail() { raise \"function failure\" } main { fail() }"); }
    catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "uncaught raise: function failure";
    }
    ASSERT_TRUE(caught);
}
void test_caught_raise_remains_language_signal() {
    ASSERT_EQ(run("main { try { raise \"handled\" } catch err { return err } }").s,
              std::string("handled"));
}
void test_multiple_flow_inputs() {
    auto value = run(R"(
        flow Research { input = "axon" debug = false }
        flow Validation { input = "fallback" debug = false }
        main { return Research.input + ":" + Validation.input }
    )");
    ASSERT_EQ(value.s, std::string("axon:fallback"));
}
void test_direct_agent_uppercase() {
    auto value = run(R"(
        flow Research { input = "axon" debug = false }
        agent Fetcher {
            provider ollama
            model llama3.2
            main { return __input__.upper() }
        }
        main { return Fetcher(Research.input) }
    )");
    ASSERT_EQ(value.s, std::string("AXON"));
}
void test_multiple_flow_agent_pipeline() {
    auto value = run(R"(
        flow Research { input = "axon" debug = false }
        agent Fetcher {
            provider ollama
            model llama3.2
            main { return __input__.upper() }
        }
        flow Validation { input = "fallback" debug = false }
        agent Validator {
            provider ollama
            model llama3.2
            main { return __input__ + " validated" }
        }
        main {
            fetched = Fetcher(Research.input)
            return Validator(fetched)
        }
    )");
    ASSERT_EQ(value.s, std::string("AXON validated"));
}

void test_task_agent_argument() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { input=__input__ prompt=[\"p\"] }") +
                 "main { Processor(\"ARGUMENT\") }", provider);
    ASSERT_EQ(provider->user(0), std::string("ARGUMENT"));
}
void test_task_specific_argument() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { input=__input__ prompt=[\"p\"] }") +
                 "main { Processor->A(\"TASK_ARGUMENT\") }", provider);
    ASSERT_EQ(provider->user(0), std::string("TASK_ARGUMENT"));
    ASSERT_EQ(provider->requests.size(), size_t(1));
}
void test_task_pipeline_argument() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { input=__input__ prompt=[\"p\"] }") +
                 "main { \"PIPELINE\" >> Processor }", provider);
    ASSERT_EQ(provider->user(0), std::string("PIPELINE"));
}
void test_task_output_reference() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { input=__input__ prompt=[\"first\"] } "
                           "task B { input=A.output prompt=[\"second\"] }") +
                 "main { Processor(\"ARG\") }", provider);
    ASSERT_EQ(provider->user(1), std::string("reply-1"));
    ASSERT_EQ(provider->requests.size(), size_t(2));
}
void test_task_nonadjacent_reference() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { prompt=[\"first\"] } "
                           "task B { prompt=[\"second\"] } "
                           "task C { input=A.output prompt=[\"third\"] }") +
                 "main { Processor(\"ARG\") }", provider);
    ASSERT_EQ(provider->user(1), std::string("reply-1"));
    ASSERT_EQ(provider->user(2), std::string("reply-1"));
    ASSERT_EQ(provider->requests.size(), size_t(3));
}
void test_task_qualified_reference() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { prompt=[\"first\"] } "
                           "task B { input=Processor.A.output prompt=[\"second\"] }") +
                 "main { Processor(\"ARG\") }", provider);
    ASSERT_EQ(provider->user(1), std::string("reply-1"));
}
void test_task_cached_arrow_reference() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { prompt=[\"first\"] } "
                           "task B { input=Processor->A prompt=[\"second\"] }") +
                 "main { Processor(\"ARG\") }", provider);
    ASSERT_EQ(provider->user(1), std::string("reply-1"));
    ASSERT_EQ(provider->requests.size(), size_t(2));
}
void test_task_arrow_explicit_rerun() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { prompt=[\"first\"] } "
                           "task B { input=Processor->A(\"RERUN\") prompt=[\"second\"] }") +
                 "main { Processor(\"ARG\") }", provider);
    ASSERT_EQ(provider->requests.size(), size_t(3));
    ASSERT_EQ(provider->user(1), std::string("RERUN"));
    ASSERT_EQ(provider->user(2), std::string("reply-2"));
}
void test_task_repeated_agent_inputs() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { input=__input__ prompt=[\"first\"] } "
                           "task B { input=A.output prompt=[\"second\"] }") +
                 "main { Processor(\"ONE\") Processor(\"TWO\") }", provider);
    ASSERT_EQ(provider->user(0), std::string("ONE"));
    ASSERT_EQ(provider->user(2), std::string("TWO"));
    ASSERT_EQ(provider->user(3), std::string("reply-3"));
}
void test_task_forward_reference_rejected() {
    expect_error(processor("task A { input=B.output } task B { input=\"b\" }") +
                 "main { Processor(\"ARG\") }", "task output not available");
}
void test_task_stale_reference_rejected() {
    expect_error(processor("task A { input=B.output } task B { input=\"b\" }") +
                 "main { Processor->B(\"OLD\") Processor(\"NEW\") }", "task output not available");
}
void test_task_null_output_is_valid() {
    auto value = run(processor("task A { input=null } task B { input=A.output }") +
                     "main { return Processor(\"ARG\") }");
    ASSERT_TRUE(value.is_null());
}
void test_task_dict_output_not_intercepted() {
    ASSERT_EQ(run("main { data={\"output\":\"ordinary dict\"} return data.output }").s,
              std::string("ordinary dict"));
}
void test_task_external_output_reads() {
    auto provider = std::make_shared<RecordingProvider>();
    auto value = run_recorded(processor("task A { prompt=[\"first\"] }") +
                              "main { Processor(\"ARG\") return Processor.A.output }", provider);
    ASSERT_EQ(value.s, std::string("reply-1"));
    ASSERT_EQ(provider->requests.size(), size_t(1));
}
void test_task_graph_input_contract() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { input=__input__ prompt=[\"first\"] } "
                           "task B { input=A.output prompt=[\"second\"] } "
                           "task_graph { A -> B B -> done }") +
                 "main { Processor(\"GRAPH_ARG\") }", provider);
    ASSERT_EQ(provider->user(0), std::string("GRAPH_ARG"));
    ASSERT_EQ(provider->user(1), std::string("reply-1"));
}
void test_task_pipeline_two_agents() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { input=__input__ prompt=[\"first\"] }") +
                 "agent Reviewer { provider ollama model llama3.2 "
                 "task B { input=__input__ prompt=[\"second\"] } } "
                 "main { \"PIPE\" >> Processor >> Reviewer }", provider);
    ASSERT_EQ(provider->user(0), std::string("PIPE"));
    ASSERT_EQ(provider->user(1), std::string("reply-1"));
}
void test_task_explicit_flow_input() {
    auto provider = std::make_shared<RecordingProvider>();
    run_recorded(processor("task A { input=flow.input prompt=[\"p\"] }") +
                 "main { Processor(\"ARG\") }", provider);
    ASSERT_EQ(provider->user(0), std::string("FLOW"));
}
void test_task_post_delay() {
    const auto start = std::chrono::steady_clock::now();
    run(processor("task A { delay=0.03 } task B { delay=0.03 }") + "main { Processor(\"ARG\") }");
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    ASSERT_TRUE(elapsed >= 0.05);
}
void test_agent_post_delay() {
    const auto start = std::chrono::steady_clock::now();
    run("agent A { delay=0.04 main { return 1 } } main { A(1) }");
    ASSERT_TRUE(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() >= 0.03);
}
void test_flow_post_delay() {
    const auto start = std::chrono::steady_clock::now();
    run("flow F { delay=0.04 } main { return 1 }");
    ASSERT_TRUE(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() >= 0.03);
}
void test_negative_delay_rejected() {
    expect_error(processor("task A { delay=-0.01 }") + "main { Processor(1) }", "delay");
    expect_error(processor("task A { delay=\"bad\" }") + "main { Processor(1) }", "delay");
    expect_error("agent A { delay=-0.01 main { return 1 } } main { A(1) }", "delay");
    expect_error("flow F { delay=-0.01 } main { return 1 }", "delay");
}
void test_http_absence_is_explicit() {
    expect_error("main { http->get(\"http://127.0.0.1/\") }", "HTTP module is not implemented");
    expect_error("main { http->post(\"http://127.0.0.1/\",\"body\") }", "HTTP module is not implemented");
    expect_error("main { http->json(\"http://127.0.0.1/\") }", "HTTP module is not implemented");
}

void test_task_legacy_output_metadata_preserved() {
    auto provider = std::make_shared<RecordingProvider>();
    provider->replies = {"{\"key\":1}"};
    auto value = run_recorded(processor("task A { input=__input__ output=json prompt=[\"p\"] }") +
                              "main { return Processor(\"ARG\") }", provider);
    ASSERT_TRUE(value.kind == Value::Kind::String);
    ASSERT_EQ(value.s, std::string("{\"key\":1}"));
}
void test_direct_task_output_format_identifier() {
    auto provider = std::make_shared<RecordingProvider>();
    provider->replies = {"{\"key\":1}"};
    auto value = run_recorded(processor("task A { input=__input__ output=json prompt=[\"p\"] }") +
                              "main { return Processor->A(\"ARG\") }", provider);
    ASSERT_TRUE(value.kind == Value::Kind::Dict);
    ASSERT_EQ(value.dict.at("key").n, 1.0);
}

// ─────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────
static void expect_execution_error(const std::string& source, const std::string& text) {
    try { run(source); }
    catch (const std::runtime_error& error) {
        ASSERT_TRUE(std::string(error.what()).find(text) != std::string::npos);
        return;
    }
    throw std::runtime_error("expected execution error: " + text);
}
void test_timeout_flow_loop() {
    expect_execution_error("flow F { timeout=0.02 debug=false } main { while true {} }", "TimeoutError: flow");
}
void test_timeout_flow_sleep() {
    auto start = std::chrono::steady_clock::now();
    expect_execution_error("flow F { timeout=0.02 debug=false } main { sleep(10) }", "TimeoutError: flow");
    ASSERT_TRUE(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
}
void test_timeout_flow_post_delay() {
    expect_execution_error("flow F { timeout=0.02 delay=10 debug=false } main { return 42 }", "TimeoutError: flow");
}
void test_timeout_agent_recovery() {
    ASSERT_EQ(run(R"(
        agent Slow { timeout=0.02 main { sleep(10) } }
        main {
            caught=false
            try { Slow("x") } catch err {
                if err.find("TimeoutError: agent Slow") != 0 { raise "wrong timeout" }
                caught=true
            }
            if caught == false { raise "missing timeout" }
            return 42
        }
    )").n, 42.0);
}
void test_timeout_agent_post_delay() {
    expect_execution_error("agent Slow { timeout=0.02 delay=10 task T {} } main { Slow(42) }",
                           "TimeoutError: agent Slow");
}
void test_timeout_direct_task_agent_budget() {
    expect_execution_error("agent Slow { timeout=0.02 task T { delay=10 } } main { Slow->T(42) }",
                           "TimeoutError: agent Slow");
}
void test_timeout_task_recovery() {
    ASSERT_EQ(run(R"(
        agent A { task Slow { timeout=0.02 delay=10 } task Fast {} }
        main {
            caught=false
            try { A->Slow(1) } catch err {
                if err.find("TimeoutError: task A.Slow") != 0 { raise "wrong task timeout" }
                caught=true
            }
            if caught == false { raise "missing task timeout" }
            return A->Fast(42)
        }
    )").n, 42.0);
}
void test_timeout_parent_cannot_be_extended() {
    expect_execution_error(R"(
        flow F { timeout=0.02 debug=false }
        agent A { timeout=100 task T { timeout=100 delay=10 } }
        main { try { A(1) } catch err { return 42 } }
    )", "TimeoutError: flow");
}
void test_timeout_invalid_values() {
    for (const std::string value : {"-1", "\"bad\"", "999999999999999999999"}) {
        expect_execution_error("flow F { timeout=" + value + " } main {}", "timeout");
        expect_execution_error("agent A { timeout=" + value + " task T {} } main { A(1) }", "timeout");
        expect_execution_error("agent A { task T { timeout=" + value + " } } main { A(1) }", "timeout");
    }
}
void test_timeout_zero_inherits() {
    ASSERT_EQ(run("flow F { timeout=0 debug=false } agent A { timeout=0 task T { timeout=0 } } main { return A(42) }").n, 42.0);
    expect_execution_error("flow F { timeout=0.02 } agent A { timeout=0 task T { timeout=0 delay=10 } } main { A(1) }", "TimeoutError: flow");
}

// Only this host API is used across threads; AST/env/provider state stays on the run thread.
static void cancellation_case(bool during_run) {
    const std::string source = during_run ? "main { sleep(0.2) return 42 }" : "main { return 42 }";
    std::unique_ptr<AxonArena, decltype(&axon_arena_free)> arena(axon_arena_new(65536), &axon_arena_free);
    Lexer lexer(source, "<cancel-test>");
    Parser parser(lexer.tokenize(), arena.get());
    AxonNode* program = parser.parse_program();
    ASSERT_TRUE(!parser.has_errors());
    Interpreter interpreter;
    interpreter.telemetry = false;
    std::thread canceller;
    if (during_run) canceller = std::thread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        interpreter.request_cancel();
    });
    else interpreter.request_cancel();
    std::string error;
    try { interpreter.run(program); }
    catch (const std::exception& caught) { error = caught.what(); }
    if (canceller.joinable()) canceller.join();
    ASSERT_TRUE(error.find("CancelledError:") != std::string::npos);
    interpreter.reset_cancel();
    ASSERT_EQ(interpreter.run(program).n, 42.0);
}
void test_cancel_before_run_and_reset() { cancellation_case(false); }
void test_cancel_sleep_and_reset() { cancellation_case(true); }

class FailingProvider final : public IProviderAdapter {
public:
    int calls = 0;
    bool delayed = false;
    std::string provider_name() const override { return "ollama"; }
    bool is_available() override { return true; }
    LLMResponse complete(const LLMRequest& request) override {
        ++calls;
        if (delayed) { request.control->sleep(10); }
        if (calls == 1) throw std::runtime_error("fixture disconnected");
        return LLMResponse{"recovered", 1, 1, 0, "stop"};
    }
};
void test_provider_error_recovery() {
    auto provider = std::make_shared<FailingProvider>();
    ASSERT_EQ(run_recorded(processor("task T { prompt=[\"generate\"] }") + R"(
        main {
            caught=false
            try { Processor(1) } catch err {
                if err.find("ProviderError:") != 0 { raise "wrong provider error" }
                caught=true
            }
            if caught == false { raise "provider error was swallowed" }
            if typeof(__last_llm_error__) != "string" { raise "missing last error" }
            result=Processor(2)
            if typeof(__last_llm_error__) != "null" { raise "stale last error" }
            return result
        }
    )", provider).s, std::string("recovered"));
    ASSERT_EQ(provider->calls, 2);
}
void test_provider_error_stops_tasks() {
    auto provider = std::make_shared<FailingProvider>();
    std::string error;
    try { run_recorded(processor("task First { prompt=[\"first\"] } task Next { prompt=[\"next\"] }")
                       + "main { Processor(1) }", provider); }
    catch (const std::exception& caught) { error = caught.what(); }
    ASSERT_TRUE(error.find("ProviderError:") != std::string::npos);
    ASSERT_EQ(provider->calls, 1);
}
void test_provider_missing_is_error() {
    expect_execution_error(processor("task T { prompt=[\"generate\"] }") + "main { Processor(1) }",
                           "ProviderError: provider 'ollama' is not registered");
}
void test_provider_budget_reaches_adapter() {
    auto provider = std::make_shared<FailingProvider>(); provider->delayed = true;
    std::string error;
    try { run_recorded("agent A { timeout=0.02 task T { prompt=[\"generate\"] } } main { A(1) }", provider); }
    catch (const std::exception& caught) { error = caught.what(); }
    ASSERT_TRUE(error.find("TimeoutError: agent A") != std::string::npos);
    ASSERT_EQ(provider->calls, 1);
}
void test_graph_unhandled_provider_error() {
    expect_execution_error("agent A { task T { prompt=[\"generate\"] } }", "ProviderError:");
}

class ParallelProvider final : public IProviderAdapter {
public:
    std::atomic<int> active{0}, peak{0}, arrivals{0};
    bool concurrent = true, rendezvous = true, fail = false;
    std::string provider_name() const override { return "ollama"; }
    bool is_available() override { return true; }
    bool supports_concurrent_requests() const override { return concurrent; }
    LLMResponse complete(const LLMRequest& request) override {
        const int now = active.fetch_add(1) + 1;
        struct Leave { std::atomic<int>& active; ~Leave() { --active; } } leave{active};
        int old = peak.load();
        while (old < now && !peak.compare_exchange_weak(old, now)) {}
        ++arrivals;
        std::string input;
        for (const auto& message : request.messages) if (message.role == "user") input = message.content;
        if (fail && input == "FAIL") throw std::runtime_error("parallel fixture failure");
        if (fail) request.control->sleep(10);
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (rendezvous && arrivals.load() < 2) {
            if (std::chrono::steady_clock::now() > limit) throw std::runtime_error("branches did not overlap");
            request.control->sleep(0.001);
        }
        request.control->sleep(0.005);
        return LLMResponse{input,1,1,0,"stop"};
    }
};
void test_parallel_real_overlap_and_same_agent_outputs() {
    auto provider = std::make_shared<ParallelProvider>();
    ASSERT_EQ(run_recorded(processor("task T { prompt=[\"echo\"] }") + R"(
        main {
            parallel { a=Processor("LEFT") b=Processor("RIGHT") }
            if a != "LEFT" { raise "left input lost" }
            if b != "RIGHT" { raise "right input lost" }
            if Processor.T != "RIGHT" { raise "cache merge must follow source order" }
            return a + b
        }
    )",provider).s,std::string("LEFTRIGHT"));
    ASSERT_TRUE(provider->peak.load() >= 2);
}
void test_parallel_pipeline_last_declared_result() {
    auto provider = std::make_shared<ParallelProvider>();
    ASSERT_EQ(run_recorded(R"(
        flow F { debug=false }
        agent A { task T { input="FIRST" prompt=["echo"] } }
        agent B { task T { input="LAST" prompt=["echo"] } }
        agent C { task T {} }
        main { return "INPUT" >> [A,B] >> C }
    )",provider).s,std::string("LAST"));
    ASSERT_TRUE(provider->peak.load() >= 2);
}
void test_parallel_variables_are_snapshots() {
    ASSERT_EQ(run(R"(
        flow F { debug=false }
        func increment() { counter=counter+1 return counter }
        main { counter=0 parallel { a=increment() b=increment() } return a+b+counter }
    )").n,2.0);
}
void test_parallel_container_isolation() {
    ASSERT_EQ(run(R"(
        func change(n) { values[0]=n return values[0] }
        main { values=[0] parallel { a=change(10) b=change(20) } return values[0]+a+b }
    )").n,30.0);
}
void test_parallel_returned_closures() {
    ASSERT_EQ(run(R"(
        func make(x) { return (y)=>x*y }
        main { parallel { a=make(2) b=make(3) } return a(4)+b(4) }
    )").n,20.0);
}
void test_parallel_exported_existing_closure_state() {
    ASSERT_EQ(run(R"(
        func expose() { f() return f }
        main {
            counter=0
            f=func() { counter=counter+1 return counter }
            parallel { a=expose() b=expose() }
            return a()+b()+f()
        }
    )").n,5.0);
}
void test_parallel_nested_blocks() {
    ASSERT_EQ(run(R"(
        func work(n) { parallel { x=n+1 y=n+2 } return x+y }
        main { parallel { a=work(2) b=work(5) } return a+b }
    )").n,20.0);
}
void test_parallel_duplicate_targets_source_order() {
    ASSERT_EQ(run("main { parallel { x=1 x=2 } return x }").n,2.0);
}
void test_parallel_empty_and_single() {
    ASSERT_EQ(run("main { parallel {} parallel { x=42 } return x }").n,42.0);
}
void test_parallel_only_top_level_results_are_published() {
    expect_execution_error("main { parallel { a=1 b=a+1 } }","undefined variable 'a'");
}
void test_parallel_shared_memory() {
    ASSERT_EQ(run(R"(
        func store(key,value) { memory->store(key,value,"shared") return memory->get(key,"shared") }
        main {
            parallel { a=store("left","L") b=store("right","R") }
            if memory->get("left","shared") != "L" { raise "lost shared write" }
            return a+b+memory->get("right","shared")
        }
    )").s,std::string("LRR"));
}
void test_parallel_error_cancels_sibling_and_preserves_bindings() {
    const auto start=std::chrono::steady_clock::now();
    ASSERT_EQ(run(R"(
        func slow() { sleep(10) return 1 }
        func fail() { raise "parallel failure" }
        main {
            a=10 b=20
            try { parallel { a=slow() b=fail() } } catch err {
                if err != "parallel failure" { raise "wrong failure" }
            }
            return a+b
        }
    )").n,30.0);
    ASSERT_TRUE(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));
}
void test_parallel_timeout_cancels_siblings() {
    const auto start=std::chrono::steady_clock::now();
    expect_execution_error(R"(
        flow F { timeout=0.03 debug=false }
        func slow() { sleep(10) return 1 }
        main { parallel { a=slow() b=slow() } }
    )","TimeoutError: flow");
    ASSERT_TRUE(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));
}
void test_parallel_provider_failure_is_not_hidden() {
    auto provider=std::make_shared<ParallelProvider>(); provider->fail=true;
    std::string error;
    const auto start=std::chrono::steady_clock::now();
    try { run_recorded(processor("task T { prompt=[\"echo\"] }")+"main { parallel { a=Processor(\"SLOW\") b=Processor(\"FAIL\") } }",provider); }
    catch (const std::exception& caught) { error=caught.what(); }
    ASSERT_TRUE(error.find("parallel fixture failure")!=std::string::npos);
    ASSERT_TRUE(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));
    ASSERT_EQ(provider->active.load(),0);
}
void test_parallel_legacy_provider_serialized() {
    auto provider=std::make_shared<ParallelProvider>(); provider->concurrent=false; provider->rendezvous=false;
    ASSERT_EQ(run_recorded(processor("task T { prompt=[\"echo\"] }")+"main { parallel { a=Processor(\"L\") b=Processor(\"R\") } return a+b }",provider).s,std::string("LR"));
    ASSERT_EQ(provider->peak.load(),1);
}
void test_parallel_bounded_queue() {
    auto provider=std::make_shared<ParallelProvider>();
    std::string program=processor("task T { prompt=[\"echo\"] }")+"main { parallel { ";
    for (int i=0;i<24;++i) program+="v"+std::to_string(i)+"=Processor(\""+std::to_string(i)+"\") ";
    program+="} return v0+v23 }";
    ASSERT_EQ(run_recorded(program,provider).s,std::string("023"));
    ASSERT_EQ(provider->arrivals.load(),24);
    ASSERT_TRUE(provider->peak.load()>=2 && provider->peak.load()<=8);
}
void test_parallel_active_agent_frame() {
    ASSERT_EQ(run(R"(
        agent A { task T {} main { parallel { left=A->T("L") right=A->T("R") } return right } }
        main { value=A("input") if A.T != "R" { raise "frame/cache mismatch" } return value }
    )").s,std::string("R"));
}
void test_parallel_rejects_control_statements_before_start() {
    ASSERT_EQ(run(R"(
        main {
            x=0
            try { parallel { x=1 return 9 } } catch err {
                if err.find("ParallelError:") != 0 { raise "wrong validation error" }
            }
            return x
        }
    )").n,0.0);
}
void test_parallel_print_source_order() {
    std::ostringstream output;
    struct RestoreBuffer {
        std::streambuf* previous;
        ~RestoreBuffer() { std::cout.rdbuf(previous); }
    } restore{std::cout.rdbuf(output.rdbuf())};
    run(R"(
        flow F { debug=false }
        func emit_line(value,seconds) { sleep(seconds) print(value) return value }
        main { parallel { a=emit_line("FIRST",0.02) b=emit_line("LAST",0) } }
    )");
    ASSERT_EQ(output.str(),std::string("FIRST\nLAST\n"));
}
void test_parallel_host_cancellation() {
    const std::string source="func slow() { sleep(10) return 1 } main { parallel { a=slow() b=slow() } }";
    std::unique_ptr<AxonArena,decltype(&axon_arena_free)> arena(axon_arena_new(65536),&axon_arena_free);
    Lexer lexer(source,"<parallel-cancel>"); Parser parser(lexer.tokenize(),arena.get());
    AxonNode* program=parser.parse_program(); ASSERT_TRUE(!parser.has_errors());
    Interpreter interpreter; interpreter.telemetry=false;
    std::thread cancel([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); interpreter.request_cancel(); });
    std::string error;
    try { interpreter.run(program); } catch (const std::exception& caught) { error=caught.what(); }
    cancel.join();
    ASSERT_TRUE(error.find("CancelledError:")!=std::string::npos);
}
void test_parallel_nested_failure() {
    expect_execution_error(R"(
        func fail() { raise "nested failure" }
        func slow() { sleep(10) return 1 }
        func inner() { parallel { x=fail() y=slow() } return x }
        main { parallel { a=inner() b=slow() } }
    )","nested failure");
}

int main() {
    std::cout << "═══════════════════════════════════════════\n";
    std::cout << "  Axon Interpreter Tests\n";
    std::cout << "═══════════════════════════════════════════\n\n";

    std::cout << "── Arithmetic ──────────────────────────────\n";
    TEST(arithmetic_add);
    TEST(arithmetic_subtract);
    TEST(arithmetic_multiply);
    TEST(arithmetic_divide);
    TEST(string_concat);

    std::cout << "\n── Variables ───────────────────────────────\n";
    TEST(variable_assignment);
    TEST(variable_reassign);

    std::cout << "\n── If/Else ─────────────────────────────────\n";
    TEST(if_true);
    TEST(if_false);
    TEST(nested_if);

    std::cout << "\n── For Loop ────────────────────────────────\n";
    TEST(for_loop_count);
    TEST(for_loop_string);

    std::cout << "\n── While Loop ──────────────────────────────\n";
    TEST(while_loop);

    std::cout << "\n── Match ───────────────────────────────────\n";
    TEST(match_string);
    TEST(match_wildcard);

    std::cout << "\n── Try/Catch ───────────────────────────────\n";
    TEST(try_catch_raise);
    TEST(try_no_raise);

    std::cout << "\n── Agent Flow ──────────────────────────────\n";
    TEST(agent_chain_input);
    TEST(flow_input);

    std::cout << "\n── Data Types ──────────────────────────────\n";
    TEST(bool_true);
    TEST(bool_false);
    TEST(null_value);
    TEST(list_literal);
    TEST(list_index);

    std::cout << "\n── Method Calls ────────────────────────────\n";
    TEST(isempty_true);
    TEST(isempty_false);
    TEST(string_length);

    std::cout << "\n── Logic Operators ─────────────────────────\n";
    TEST(logic_and);
    TEST(logic_or);
    TEST(logic_not);

    std::cout << "\n── Task Graph ──────────────────────────────\n";
    TEST(run_task_graph);

    std::cout << "\n── Graph Execution ─────────────────────────\n";
    TEST(graph_execution_order);

    std::cout << "\n-- Functions and native module regressions --\n";
    TEST(function_basic);
    TEST(function_recursion);
    TEST(function_anonymous);
    TEST(function_arrow);
    TEST(function_closure);
    TEST(function_independent_closures);
    TEST(function_chained_call);
    TEST(function_higher_order);
    TEST(function_nested_scope);
    TEST(function_parameter_shadowing);
    TEST(function_early_return);
    TEST(function_arity_missing);
    TEST(function_arity_extra);
    TEST(function_arity_anonymous_and_arrow);
    TEST(function_arity_closure_and_chained);
    TEST(function_arity_higher_order);
    TEST(function_arity_null_and_zero);
    TEST(function_arity_catch_and_recovery);
    TEST(function_arity_rejects_before_side_effects);
    TEST(hash_sha256_vector);
    TEST(hash_md5_vector);
    TEST(hash_unsupported_rejected);
    TEST(codec_roundtrip);
    TEST(string_find_literal_contract);

    TEST(uncaught_raise_is_host_error);
    TEST(uncaught_function_raise_is_host_error);
    TEST(caught_raise_remains_language_signal);
    TEST(multiple_flow_inputs);
    TEST(direct_agent_uppercase);
    TEST(multiple_flow_agent_pipeline);

    TEST(task_agent_argument);
    TEST(task_specific_argument);
    TEST(task_pipeline_argument);
    TEST(task_output_reference);
    TEST(task_nonadjacent_reference);
    TEST(task_qualified_reference);
    TEST(task_cached_arrow_reference);
    TEST(task_arrow_explicit_rerun);
    TEST(task_repeated_agent_inputs);
    TEST(task_forward_reference_rejected);
    TEST(task_stale_reference_rejected);
    TEST(task_null_output_is_valid);
    TEST(task_dict_output_not_intercepted);
    TEST(task_external_output_reads);
    TEST(task_graph_input_contract);
    TEST(task_pipeline_two_agents);
    TEST(task_explicit_flow_input);
    TEST(task_post_delay);
    TEST(agent_post_delay);
    TEST(flow_post_delay);
    TEST(negative_delay_rejected);
    TEST(http_absence_is_explicit);

    TEST(task_legacy_output_metadata_preserved);
    TEST(direct_task_output_format_identifier);
    TEST(timeout_flow_loop);
    TEST(timeout_flow_sleep);
    TEST(timeout_flow_post_delay);
    TEST(timeout_agent_recovery);
    TEST(timeout_agent_post_delay);
    TEST(timeout_direct_task_agent_budget);
    TEST(timeout_task_recovery);
    TEST(timeout_parent_cannot_be_extended);
    TEST(timeout_invalid_values);
    TEST(timeout_zero_inherits);
    TEST(cancel_before_run_and_reset);
    TEST(cancel_sleep_and_reset);
    TEST(provider_error_recovery);
    TEST(provider_error_stops_tasks);
    TEST(provider_missing_is_error);
    TEST(provider_budget_reaches_adapter);
    TEST(graph_unhandled_provider_error);
    TEST(parallel_real_overlap_and_same_agent_outputs);
    TEST(parallel_pipeline_last_declared_result);
    TEST(parallel_variables_are_snapshots);
    TEST(parallel_container_isolation);
    TEST(parallel_returned_closures);
    TEST(parallel_exported_existing_closure_state);
    TEST(parallel_nested_blocks);
    TEST(parallel_duplicate_targets_source_order);
    TEST(parallel_empty_and_single);
    TEST(parallel_only_top_level_results_are_published);
    TEST(parallel_shared_memory);
    TEST(parallel_error_cancels_sibling_and_preserves_bindings);
    TEST(parallel_timeout_cancels_siblings);
    TEST(parallel_provider_failure_is_not_hidden);
    TEST(parallel_legacy_provider_serialized);
    TEST(parallel_bounded_queue);
    TEST(parallel_active_agent_frame);
    TEST(parallel_rejects_control_statements_before_start);
    TEST(parallel_print_source_order);
    TEST(parallel_host_cancellation);
    TEST(parallel_nested_failure);



    std::cout << "\n═══════════════════════════════════════════\n";
    std::cout << "  Results: \033[32m" << passed << " passed\033[0m, ";
    if (failed > 0)
        std::cout << "\033[31m" << failed << " failed\033[0m";
    else
        std::cout << failed << " failed";
    std::cout << "\n═══════════════════════════════════════════\n";
    return failed > 0 ? 1 : 0;
}
