/**
 * runtime/include/axon_parser.h
 * ──────────────────────────────
 * Recursive descent parser for the Axon language.
 * Consumes a token stream (from Lexer) and produces
 * AxonNode* structs allocated in an AxonArena.
 */
#pragma once
#include "lexer.h"
#include "ast.h"
#include <string>
#include <vector>

namespace axon {

struct ParseError {
    std::string message;
    uint32_t    line;
    uint32_t    col;
};

class Parser {
public:
    // Takes ownership of the token stream and an arena.
    // The arena must outlive the resulting AST.
    Parser(std::vector<Token> tokens, AxonArena* arena);

    // Parse the full program. Returns root node.
    // Check errors() for any parse errors encountered.
    AxonNode* parse_program();
    AxonNode* parse_expression();
    AxonNode* parse_pipeline();

    const std::vector<ParseError>& errors() const { return _errors; }
    bool has_errors() const { return !_errors.empty(); }

private:
    // ── Token navigation ─────────────────────────────────────
    const Token& peek(int offset = 0) const;
    const Token& current() const { return peek(0); }
    const Token& advance();
    bool  check(TK t) const { return current().type == t; }
    bool  match(TK t);
    const Token& expect(TK t, const std::string& context);
    bool  at_end() const;

    // ── Top-level parsers ────────────────────────────────────
    AxonNode* parse_top_level();
    AxonNode* parse_import();
    AxonNode* parse_flow();
    AxonNode* parse_agent();
    AxonNode* parse_embedding();
    AxonNode* parse_graph();
    AxonNode* parse_role();

    // ── Agent body ───────────────────────────────────────────
    void parse_agent_body(AxonNode* agent);
    void parse_memory_block(AxonNode* agent);
    void parse_validation_block(AxonNode* agent);
    void parse_learning_block(AxonNode* agent);
    AxonNode* parse_task();
    AxonNode* parse_main_block();

    // ── Statements ───────────────────────────────────────────
    AxonNode* parse_statement();
    AxonNode* parse_assignment_or_expr();
    AxonNode* parse_if();
    AxonNode* parse_for();
    AxonNode* parse_while();
    AxonNode* parse_match();
    AxonNode* parse_try();
    AxonNode* parse_raise();
    AxonNode* parse_return();
    AxonNode* parse_func_def();
    AxonNode* parse_task_graph();
    AxonNode* parse_storage_def();
    AxonNode* parse_global_main();
    AxonNode* parse_parallel_block();
    AxonNode* parse_spawn();
    void      parse_output_config(AxonOutputConfig* cfg);
    AxonNode* parse_io_block();
    AxonNode* parse_run();
    AxonNode* parse_interop_block();

    // ── Expressions ──────────────────────────────────────────
    AxonNode* parse_expr();
    AxonNode* parse_or();
    AxonNode* parse_and();
    AxonNode* parse_not();
    AxonNode* parse_compare();
    AxonNode* parse_add();
    AxonNode* parse_mul();
    AxonNode* parse_power();
    AxonNode* parse_unary();
    AxonNode* parse_postfix();
    AxonNode* parse_primary();
    AxonNode* parse_list();
    AxonNode* parse_dict();
    AxonNodeList parse_arg_list();

    // ── Graph helpers ────────────────────────────────────────
    AxonNode* parse_graph_edge();

    // ── KV pair helpers ──────────────────────────────────────
    void parse_kv_block(AxonKVList* list);  // { key = val ... }

    // ── Node allocation helpers ──────────────────────────────
    AxonNode* node(AxonNodeType type);
    AxonSrcLoc loc() const;
    const char* intern(const std::string& s);
    void error(const std::string& msg);
    void synchronize(); // error recovery: skip to next safe point

    // ── State ────────────────────────────────────────────────
    std::vector<Token>   _tokens;
    size_t               _pos = 0;
    int _python_scope = 0;
    AxonArena*           _arena;
    std::vector<ParseError> _errors;
};

} // namespace axon
