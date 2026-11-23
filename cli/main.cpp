/**
 * cli/main.cpp — Axon CLI  v0.1.2 phase2b
 */
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <vector>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include "../runtime/include/platform.h"
#include "../runtime/include/lexer.h"
#include "../runtime/include/axon_parser.h"
#include "../runtime/include/interpreter.h"
#include "../adapters/ollama/ollama_adapter.hpp"

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#include "axon_version.h"

#ifdef AXON_OS_WINDOWS
  #define C_RESET ""
  #define C_GREEN ""
  #define C_YELLOW ""
  #define C_RED   ""
  #define C_CYAN  ""
  #define C_BOLD  ""
#else
  #define C_RESET  "\033[0m"
  #define C_GREEN  "\033[32m"
  #define C_YELLOW "\033[33m"
  #define C_RED    "\033[31m"
  #define C_CYAN   "\033[36m"
  #define C_BOLD   "\033[1m"
#endif

static void enable_ansi() {
#ifdef AXON_OS_WINDOWS
    // Force UTF-8 output so accents and unicode render correctly
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // Enable ANSI color sequences
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD m = 0;
        if (GetConsoleMode(h, &m))
            SetConsoleMode(h, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

static void print_logo() {
    std::cout
      << C_CYAN << C_BOLD
      << "\n  ██████╗ ██╗  ██╗ ██████╗ ███╗   ██╗\n"
      << "  ██╔══██╗╚██╗██╔╝██╔═══██╗████╗  ██║\n"
      << "  ███████║ ╚███╔╝ ██║   ██║██╔██╗ ██║\n"
      << "  ██╔══██║ ██╔██╗ ██║   ██║██║╚██╗██║\n"
      << "  ██║  ██║██╔╝ ██╗╚██████╔╝██║ ╚████║\n"
      << "  ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═══╝\n"
      << C_RESET << "  Cognitive Flow Oriented Language\n\n";
}

static void print_usage() {
    std::cout
      << C_BOLD << "Usage:" << C_RESET << " axon <command> [file]\n\n"
      << C_BOLD << "Commands:\n" << C_RESET
      << "  " << C_GREEN << "run"     << C_RESET << "   <file.axon>   Execute a program\n"
      << "  " << C_GREEN << "check"   << C_RESET << " <file.axon>   Parse and validate\n"
      << "  " << C_GREEN << "test"    << C_RESET << "               Run runtime tests\n"
      << "  " << C_GREEN << "version" << C_RESET << "            Show version\n\n";
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

// ── Shared: lex + parse ──────────────────────────────────────
static AxonNode* parse_source(const std::string& path,
                               const std::string& src,
                               AxonArena*         arena,
                               bool               verbose) {
    axon::Lexer lexer(src, path);
    auto tokens = lexer.tokenize();

    if (lexer.has_errors()) {
        for (auto& e : lexer.errors())
            std::cerr << C_RED << "[lex error]" << C_RESET
                      << " line " << e.line << ": " << e.message << "\n";
        return nullptr;
    }

    if (verbose)
        std::cout << C_CYAN << "[axon]" << C_RESET
                  << " Lexed " << tokens.size() << " tokens\n";

    axon::Parser parser(std::move(tokens), arena);
    AxonNode*    prog = parser.parse_program();

    if (parser.has_errors()) {
        for (auto& e : parser.errors())
            std::cerr << C_RED << "[parse error]" << C_RESET
                      << " line " << e.line << ": " << e.message << "\n";
        return nullptr;
    }

    return prog;
}

// ── axon check ───────────────────────────────────────────────
static int cmd_check(const std::string& path) {
    std::string src = read_file(path);
    if (src.empty()) {
        std::cerr << C_RED << "[error]" << C_RESET
                  << " Cannot read: " << path << "\n";
        return 1;
    }

    AxonArena* arena = axon_arena_new(256 * 1024);
    AxonNode*  prog  = parse_source(path, src, arena, true);
    if (!prog) { axon_arena_free(arena); return 1; }

    int agents=0, flows=0, graphs=0, tasks=0;
    for (size_t i = 0; i < prog->as.program.stmts.count; ++i) {
        AxonNode* s = prog->as.program.stmts.items[i];
        if (!s) continue;
        if (s->type == AXON_NODE_AGENT_DEF) {
            ++agents;
            tasks += (int)s->as.agent_def.tasks.count;
        }
        if (s->type == AXON_NODE_FLOW_DEF)  ++flows;
        if (s->type == AXON_NODE_GRAPH_DEF) ++graphs;
    }

    std::cout
      << C_CYAN << "[axon]" << C_RESET
      << " " << C_GREEN << "✓" << C_RESET
      << " " << path << " is valid\n\n"
      << "  Agents  : " << agents  << "\n"
      << "  Tasks   : " << tasks   << "\n"
      << "  Flows   : " << flows   << "\n"
      << "  Graphs  : " << graphs  << "\n\n";

    axon_arena_free(arena);
    return 0;
}

// ── axon run ─────────────────────────────────────────────────
static int cmd_run(const std::string& path) {
    std::string src = read_file(path);
    if (src.empty()) {
        std::cerr << C_RED << "[error]" << C_RESET
                  << " Cannot read: " << path << "\n";
        return 1;
    }

    AxonArena* arena = axon_arena_new(256 * 1024);
    AxonNode*  prog  = parse_source(path, src, arena, false); // TODO: si hay alguna flag de debug=TRUE
    if (!prog) { axon_arena_free(arena); return 1; }

    std::cout << C_CYAN << "[axon]" << C_RESET
              << " Running: " << path << "\n\n";

    // Build interpreter
    axon::Interpreter interp;
    interp.telemetry = true;

    // Register Ollama adapter (default localhost:11434)
    // Check debug = false in source to suppress connection messages
    bool quiet_mode = (src.find("debug = false") != std::string::npos ||
                       src.find("debug=false")   != std::string::npos);
    auto ollama = std::make_shared<axon::OllamaAdapter>();
    if (ollama->is_available()) {
        interp.register_provider(ollama);
        if (!quiet_mode)
            std::cout << C_GREEN << "[axon]" << C_RESET
                      << " Ollama connected (localhost:11434)\n\n";
    } else {
        if (!quiet_mode)
            std::cout << C_YELLOW << "[axon]" << C_RESET
                      << " Ollama not detected — model calls will raise ProviderError.\n"
                      << "       Start Ollama with: " << C_BOLD
                      << "ollama serve" << C_RESET << "\n\n";
    }

    // Pass source for error reporting
    interp.set_source(path, src);

    // Execute
    try {
        axon::Value result = interp.run(prog);

        std::cout << "\n" << C_GREEN << "[axon]" << C_RESET
                  << " Done.\n";

        // programmer controls all output via print()

    } catch (const std::exception& e) {
        std::cerr << C_RED << "[runtime error]" << C_RESET
                  << " " << e.what() << "\n";
        axon_arena_free(arena);
        return 1;
    }

    axon_arena_free(arena);
    return 0;
}

// ── axon version ─────────────────────────────────────────────
static int cmd_version() {
    print_logo();
    std::cout
      << "  Version  : " << C_CYAN << AXON_VERSION
      << " (" << AXON_PHASE << ")" << C_RESET << "\n"
      << "  Platform : " << AXON_OS_NAME  << "\n"
      << "  Compiler : " << AXON_COMPILER_NAME << "\n"
      << "  Built    : " << __DATE__ << "\n"
      << "  Runtime  : C++17 native\n\n";
    return 0;
}

// Resolve the running CLI, independently of cwd, PATH and spaces in paths.
static std::filesystem::path executable_directory(const char* argv0) {
#ifdef AXON_OS_WINDOWS
    std::vector<wchar_t> buffer(32768);
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
    if (size == 0 || size >= buffer.size())
        throw std::runtime_error("cannot locate axon executable");
    return std::filesystem::path(std::wstring(buffer.data(), size)).parent_path();
#elif defined(AXON_OS_LINUX)
    std::error_code error;
    auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) return executable.parent_path();
#elif defined(AXON_OS_MACOS)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0)
        return std::filesystem::canonical(buffer.data()).parent_path();
#endif
    return std::filesystem::canonical(argv0).parent_path();
}

static int run_test_binary(const std::filesystem::path& executable) {
#ifdef AXON_OS_WINDOWS
    const std::wstring path = executable.wstring();
    const wchar_t* args[] = {path.c_str(), nullptr};
    const intptr_t status = _wspawnv(_P_WAIT, path.c_str(), args);
    return status < 0 ? 1 : static_cast<int>(status);
#else
    const std::string path = executable.string();
    const pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) {
        execl(path.c_str(), path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return 1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
#endif
}

static int cmd_test(const char* argv0) {
    try {
        const auto directory = executable_directory(argv0);
        int result = 0;
        for (const char* suite : {"axon_test_runtime", "axon_test_interp"}) {
            auto path = directory / suite;
#ifdef AXON_OS_WINDOWS
            path += ".exe";
#endif
            if (!std::filesystem::is_regular_file(path)) {
                std::cerr << "[error] Missing test executable: " << path.string()
                          << "\nBuild all targets or use ctest --test-dir <build-dir>.\n";
                return 1;
            }
            std::cout << "[axon] Running " << suite << "...\n" << std::flush;
            const int status = run_test_binary(path);
            if (status != 0) {
                std::cerr << "[error] " << suite << " exited with " << status << "\n";
                if (result == 0) result = status;
            }
        }
        return result;
    } catch (const std::exception& error) {
        std::cerr << "[error] " << error.what() << "\n";
        return 1;
    }
}

// ── main ─────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    enable_ansi();
    if (argc < 2) { print_logo(); print_usage(); return 0; }

    std::string cmd = argv[1];
    if (cmd=="version"||cmd=="--version"||cmd=="-v") return cmd_version();
    if (cmd=="help"||cmd=="--help"||cmd=="-h") {
        print_logo(); print_usage(); return 0;
    }
    if (cmd == "test") return cmd_test(argv[0]);

    if (cmd == "run" || cmd == "check") {
        if (argc < 3) {
            std::cerr << C_RED << "[error]" << C_RESET
                      << " Usage: axon " << cmd << " <file.axon>\n";
            return 1;
        }
        return cmd == "run" ? cmd_run(argv[2]) : cmd_check(argv[2]);
    }

    std::cerr << C_RED << "[error]" << C_RESET
              << " Unknown command: " << cmd << "\n\n";
    print_usage();
    return 1;
}
