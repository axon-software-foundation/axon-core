/**
 * runtime/src/axon_parser.cpp
 * ────────────────────────────
 * Recursive descent parser for the Axon language.
 */

#include "../include/axon_parser.h"
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <sstream>

// ─────────────────────────────────────────────────────────────
// Arena allocator implementation
// ─────────────────────────────────────────────────────────────
extern "C" {

AxonArena* axon_arena_new(size_t initial_cap) {
    auto* a = (AxonArena*)malloc(sizeof(AxonArena));
    a->buf  = (uint8_t*)malloc(initial_cap);
    a->cap  = initial_cap;
    a->used = 0;
    a->next = nullptr;
    return a;
}

void* axon_arena_alloc(AxonArena* arena, size_t size) {
    // align to 8 bytes
    size = (size + 7) & ~(size_t)7;
    if (arena->used + size > arena->cap) {
        size_t new_cap = (arena->cap * 2 > size) ? arena->cap * 2 : size * 2;
        AxonArena* next = axon_arena_new(new_cap);
        next->next = arena->next;
        arena->next = next;
        return axon_arena_alloc(next, size);
    }
    void* ptr = arena->buf + arena->used;
    arena->used += size;
    return ptr;
}

char* axon_arena_strdup(AxonArena* arena, const char* s) {
    size_t len = strlen(s) + 1;
    char* dst  = (char*)axon_arena_alloc(arena, len);
    memcpy(dst, s, len);
    return dst;
}

void axon_arena_free(AxonArena* arena) {
    while (arena) {
        AxonArena* next = arena->next;
        free(arena->buf);
        free(arena);
        arena = next;
    }
}

AxonNode* axon_node_new(AxonArena* arena, AxonNodeType type, AxonSrcLoc loc) {
    auto* n = (AxonNode*)axon_arena_alloc(arena, sizeof(AxonNode));
    memset(n, 0, sizeof(AxonNode));
    n->type = type;
    n->loc  = loc;
    return n;
}

void axon_nodelist_init(AxonNodeList* list) {
    list->items = nullptr; list->count = 0; list->capacity = 0;
}

void axon_nodelist_push(AxonNodeList* list, AxonNode* node, AxonArena* arena) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity == 0 ? 4 : list->capacity * 2;
        auto** new_items = (AxonNode**)axon_arena_alloc(arena, new_cap * sizeof(AxonNode*));
        if (list->items)
            memcpy(new_items, list->items, list->count * sizeof(AxonNode*));
        list->items    = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = node;
}

void axon_kvlist_init(AxonKVList* list) {
    list->items = nullptr; list->count = 0; list->capacity = 0;
}

void axon_kvlist_push(AxonKVList* list, const char* key,
                      AxonNode* value, AxonArena* arena) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity == 0 ? 4 : list->capacity * 2;
        auto* new_items = (AxonKVPair*)axon_arena_alloc(arena, new_cap * sizeof(AxonKVPair));
        if (list->items)
            memcpy(new_items, list->items, list->count * sizeof(AxonKVPair));
        list->items    = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = {key, value};
}

} // extern "C"

// ─────────────────────────────────────────────────────────────
// Parser implementation
// ─────────────────────────────────────────────────────────────
namespace axon {

Parser::Parser(std::vector<Token> tokens, AxonArena* arena)
    : _tokens(std::move(tokens)), _arena(arena) {}

// ── Navigation ───────────────────────────────────────────────
const Token& Parser::peek(int offset) const {
    size_t idx = _pos + (size_t)offset;
    if (idx >= _tokens.size()) return _tokens.back(); // EOF
    return _tokens[idx];
}

const Token& Parser::advance() {
    if (!at_end()) ++_pos;
    return _tokens[_pos - 1];
}

bool Parser::match(TK t) {
    if (check(t)) { advance(); return true; }
    return false;
}

const Token& Parser::expect(TK t, const std::string& ctx) {
    if (check(t)) return advance();
    std::ostringstream ss;
    ss << "expected " << Token{t,"",0,0}.type_name()
       << " in " << ctx
       << ", got '" << current().value
       << "' (" << current().type_name() << ")"
       << " at line " << current().line;
    error(ss.str());
    return current();
}

bool Parser::at_end() const {
    return _pos >= _tokens.size()
        || _tokens[_pos].type == TK::END_OF_FILE;
}

AxonNode* Parser::node(AxonNodeType type) {
    return axon_node_new(_arena, type, loc());
}

AxonSrcLoc Parser::loc() const {
    return {current().line, current().col, 0};
}

const char* Parser::intern(const std::string& s) {
    return axon_arena_strdup(_arena, s.c_str());
}

void Parser::error(const std::string& msg) {
    _errors.push_back({msg, current().line, current().col});
}

void Parser::synchronize() {
    // skip tokens until we find a safe restart point
    while (!at_end()) {
        TK t = current().type;
        if (t==TK::KW_AGENT||t==TK::KW_FLOW||t==TK::KW_GRAPH||
            t==TK::KW_ROLE||t==TK::KW_EMBEDDING||t==TK::KW_IMPORT||
            t==TK::RBRACE) return;
        advance();
    }
}

// ── Program ──────────────────────────────────────────────────
AxonNode* Parser::parse_program() {
    AxonNode* prog = node(AXON_NODE_PROGRAM);
    axon_nodelist_init(&prog->as.program.stmts);
    while (!at_end()) {
        AxonNode* stmt = parse_top_level();
        if (stmt)
            axon_nodelist_push(&prog->as.program.stmts, stmt, _arena);
    }
    return prog;
}

AxonNode* Parser::parse_top_level() {
    try {
        switch (current().type) {
            case TK::KW_IMPORT:    return parse_import();
            case TK::KW_FLOW:      return parse_flow();
            case TK::KW_AGENT:     return parse_agent();
            case TK::KW_EMBEDDING: return parse_embedding();
            case TK::KW_GRAPH:     return parse_graph();
            case TK::KW_ROLE:      return parse_role();
            case TK::KW_STORAGE:   return parse_storage_def();
            case TK::KW_MAIN:      return parse_global_main();
            case TK::KW_FUNC:      return parse_func_def();
            default:
                error("[strict] unexpected '" + current().value
                      + "' at top level (line "
                      + std::to_string(current().line) + ")");
                advance(); // skip and recover
                return nullptr;
        }
    } catch (...) { synchronize(); return nullptr; }
}

// ── Import ───────────────────────────────────────────────────
AxonNode* Parser::parse_import() {
    AxonNode* n = node(AXON_NODE_IMPORT);
    advance(); // consume 'import'

    // File path: import "./flows/research.axon"
    // Module name: import session (legacy)
    std::string path;
    if (current().type == TK::STRING) {
        path = current().value; advance();
    } else {
        // legacy dot-separated module name
        path = current().value; advance();
        while (check(TK::DOT)) {
            advance(); path += "." + current().value; advance();
        }
    }
    n->as.import_stmt.module = intern(path);
    return n;
}

// ── Flow ─────────────────────────────────────────────────────
AxonNode* Parser::parse_flow() {
    AxonNode* n = node(AXON_NODE_FLOW_DEF);
    advance(); // 'flow'
    n->as.flow_def.name = intern(expect(TK::IDENT, "flow name").value);
    axon_kvlist_init(&n->as.flow_def.entries);
    memset(&n->as.flow_def.output_config, 0, sizeof(AxonOutputConfig));
    n->as.flow_def.output_config.to_stdout = 1;
    n->as.flow_def.output_config.level     = AXON_LOG_INFO;
    n->as.flow_def.output_config.format    = AXON_OUT_STRUCTURED;
    expect(TK::LBRACE, "flow body");
    while (!check(TK::RBRACE) && !at_end()) {
        if (check(TK::KW_OUTPUT)) {
            advance();
            parse_output_config(&n->as.flow_def.output_config);
        } else {
            // Guard: skip tokens that can't be a key=value pair
            if (!check(TK::IDENT) && current().type < TK::KW_IMPORT) {
                advance(); continue;
            }
            std::string key = current().value; advance();
            // Guard: if no '=' follows, skip to avoid infinite loop
            if (!check(TK::ASSIGN)) { continue; }
            advance(); // consume '='
            AxonNode* val = parse_expr();
            if (val)
                axon_kvlist_push(&n->as.flow_def.entries,
                                 intern(key), val, _arena);
        }
    }
    expect(TK::RBRACE, "flow body close");
    return n;
}

// ── Agent ─────────────────────────────────────────────────────
AxonNode* Parser::parse_agent() {
    AxonNode* n = node(AXON_NODE_AGENT_DEF);
    advance(); // 'agent'
    n->as.agent_def.name = intern(expect(TK::IDENT,"agent name").value);
    axon_kvlist_init(&n->as.agent_def.extra);
    axon_nodelist_init(&n->as.agent_def.tasks);
    axon_kvlist_init(&n->as.agent_def.learning);
    expect(TK::LBRACE, "agent body");
    parse_agent_body(n);
    return n;
}

void Parser::parse_agent_body(AxonNode* agent) {
    while (!check(TK::RBRACE) && !at_end()) {
        if (check(TK::KW_PROVIDER)) {
            advance();
            auto v = current().value; advance();
            agent->as.agent_def.provider = intern(v);
        } else if (check(TK::KW_MODEL)) {
            advance();
            auto v = current().value; advance();
            agent->as.agent_def.model = intern(v);
        } else if (check(TK::KW_ROLE)) {
            advance();
            auto v = expect(TK::IDENT,"role").value;
            agent->as.agent_def.role = intern(v);
        } else if (check(TK::KW_MEMORY)) {
            advance(); parse_memory_block(agent);
        } else if (check(TK::KW_VALIDATION)) {
            advance(); parse_validation_block(agent);
        } else if (check(TK::KW_LEARNING)) {
            advance(); parse_learning_block(agent);
        } else if (check(TK::KW_TRANSPORT)) {
            advance();
            auto v = current().value; advance();
            agent->as.agent_def.transport = intern(v);
        } else if (check(TK::KW_ENDPOINT)) {
            advance();
            // endpoint can be a string or bare value
            if (check(TK::STRING)) {
                agent->as.agent_def.endpoint = intern(advance().value);
            } else {
                agent->as.agent_def.endpoint = intern(current().value);
                advance();
            }
        } else if (check(TK::KW_TASK_GRAPH)) {
            agent->as.agent_def.task_graph = parse_task_graph();
        } else if (check(TK::KW_TASK)) {
            AxonNode* t = parse_task();
            if (t) axon_nodelist_push(&agent->as.agent_def.tasks, t, _arena);
        } else if (check(TK::KW_MAIN)) {
            agent->as.agent_def.main_block = parse_main_block();
        } else {
            // generic key = value
            std::string key = current().value; advance();
            expect(TK::ASSIGN, "agent kv");
            AxonNode* val = parse_expr();
            axon_kvlist_push(&agent->as.agent_def.extra,
                             intern(key), val, _arena);
        }
    }
    expect(TK::RBRACE, "agent body close");
}

void Parser::parse_memory_block(AxonNode* agent) {
    expect(TK::LBRACE, "memory block");
    while (!check(TK::RBRACE) && !at_end()) {
        if (!current().is_memory_type()) {
            error("expected memory type, got '" + current().value + "'");
            advance(); continue;
        }
        AxonMemoryType mt;
        switch(current().type) {
            case TK::KW_RUNTIME:    mt=AXON_MEM_RUNTIME;    break;
            case TK::KW_SESSION:    mt=AXON_MEM_SESSION;    break;
            case TK::KW_SEMANTIC:   mt=AXON_MEM_SEMANTIC;   break;
            case TK::KW_PERSISTENT: mt=AXON_MEM_PERSISTENT; break;
            case TK::KW_SHARED:     mt=AXON_MEM_SHARED;     break;
            default:                mt=AXON_MEM_TEMPORARY;  break;
        }
        advance();
        expect(TK::ASSIGN, "memory entry");
        int enabled = 1;
        const char* ref = nullptr;
        if (check(TK::KW_ENABLED))       { enabled=1; advance(); }
        else if (check(TK::KW_DISABLED)) { enabled=0; advance(); }
        else if (check(TK::IDENT)) {
            ref = intern(current().value); advance();
        }
        agent->as.agent_def.memory[(int)mt] = {mt, enabled, ref};
    }
    expect(TK::RBRACE, "memory block close");
}

void Parser::parse_validation_block(AxonNode* agent) {
    AxonValidation& v = agent->as.agent_def.validation;
    expect(TK::LBRACE, "validation block");
    while (!check(TK::RBRACE) && !at_end()) {
        if (check(TK::KW_CONFIDENCE)) {
            advance();
            // consume comparison op (>, <, >=, <=, ==)
            advance(); // skip operator
            v.confidence_threshold = std::atof(
                expect(TK::NUMBER,"confidence value").value.c_str());
            v.has_confidence = 1;
        } else if (check(TK::KW_FORMAT)) {
            advance(); expect(TK::ASSIGN,"format");
            v.format = intern(current().value); advance();
            v.has_format = 1;
        } else if (check(TK::KW_MAX_RETRIES)) {
            advance(); expect(TK::ASSIGN,"max_retries");
            v.max_retries = std::atoi(
                expect(TK::NUMBER,"max_retries value").value.c_str());
            v.has_retries = 1;
        } else if (check(TK::KW_ON_FAIL)) {
            advance(); expect(TK::ASSIGN,"on_fail");
            if      (check(TK::KW_RETRY)) v.on_fail=AXON_ONFAIL_RETRY;
            else if (check(TK::KW_FLAG))  v.on_fail=AXON_ONFAIL_FLAG;
            else if (check(TK::KW_SKIP))  v.on_fail=AXON_ONFAIL_SKIP;
            else if (check(TK::KW_RAISE)) v.on_fail=AXON_ONFAIL_RAISE;
            v.has_onfail = 1; advance();
        } else { advance(); }
    }
    expect(TK::RBRACE, "validation block close");
}

void Parser::parse_learning_block(AxonNode* agent) {
    expect(TK::LBRACE, "learning block");
    while (!check(TK::RBRACE) && !at_end()) {
        std::string key = current().value; advance();
        expect(TK::ASSIGN, "learning kv");
        AxonNode* val = parse_expr();
        axon_kvlist_push(&agent->as.agent_def.learning,
                         intern(key), val, _arena);
    }
    expect(TK::RBRACE, "learning block close");
}

AxonNode* Parser::parse_task() {
    AxonNode* n = node(AXON_NODE_TASK_DEF);
    advance(); // 'task'
    n->as.task_def.name = intern(expect(TK::IDENT,"task name").value);
    axon_kvlist_init(&n->as.task_def.entries);
    expect(TK::LBRACE,"task body");
    while (!check(TK::RBRACE) && !at_end()) {
        if (check(TK::KW_IO)) {
            n->as.task_def.io_block = parse_io_block();
        } else {
            std::string key = current().value; advance();
            expect(TK::ASSIGN,"task kv");
            AxonNode* val = parse_expr();
            axon_kvlist_push(&n->as.task_def.entries, intern(key), val, _arena);
        }
    }
    expect(TK::RBRACE,"task body close");
    return n;
}

AxonNode* Parser::parse_main_block() {
    AxonNode* n = node(AXON_NODE_MAIN_BLOCK);
    advance(); // 'main'
    axon_nodelist_init(&n->as.main_block.body);
    expect(TK::LBRACE,"main body");
    while (!check(TK::RBRACE) && !at_end()) {
        AxonNode* s = parse_statement();
        if (s) axon_nodelist_push(&n->as.main_block.body, s, _arena);
    }
    expect(TK::RBRACE,"main body close");
    return n;
}

// ── Statements ───────────────────────────────────────────────
AxonNode* Parser::parse_statement() {
    switch (current().type) {
        case TK::KW_IF:     return parse_if();
        case TK::KW_FOR:    return parse_for();
        case TK::KW_WHILE:  return parse_while();
        case TK::KW_MATCH:  return parse_match();
        case TK::KW_TRY:    return parse_try();
        case TK::KW_RAISE:  return parse_raise();
        case TK::KW_RETURN: return parse_return();
        case TK::KW_FUNC:   return parse_func_def();
        case TK::KW_RUN:      return parse_run();
        case TK::KW_PARALLEL: return parse_parallel_block();
        case TK::KW_SPAWN:    return parse_spawn();
        case TK::KW_PYTHON: case TK::KW_SQL:
        case TK::KW_SHELL:  case TK::KW_CPP:
            return parse_interop_block();
        default:
            return parse_assignment_or_expr();
    }
}

AxonNode* Parser::parse_assignment_or_expr() {
    // Fast path: simple IDENT = expr
    bool is_simple = (check(TK::IDENT) || check(TK::WILDCARD) ||
                      current().type >= TK::KW_IMPORT)
                     && peek(1).type == TK::ASSIGN;
    if (is_simple) {
        AxonNode* n = node(AXON_NODE_ASSIGNMENT);
        n->as.assignment.name  = intern(advance().value);
        advance(); // consume '='
        n->as.assignment.value = parse_expr();
        return n;
    }

    // Parse full expression (may be LHS of index/attr assignment)
    AxonNode* lhs = parse_expr();

    // obj[key] = value
    if (lhs && lhs->type == AXON_NODE_INDEX_ACCESS && check(TK::ASSIGN)) {
        advance(); // consume '='
        AxonNode* n = node(AXON_NODE_INDEX_ASSIGN);
        n->as.index_assign.obj   = lhs->as.index_access.obj;
        n->as.index_assign.key   = lhs->as.index_access.index;
        n->as.index_assign.value = parse_expr();
        return n;
    }

    // obj.attr = value
    if (lhs && lhs->type == AXON_NODE_ATTR_ACCESS && check(TK::ASSIGN)) {
        advance(); // consume '='
        AxonNode* n = node(AXON_NODE_ATTR_ASSIGN);
        n->as.attr_assign.obj   = lhs->as.attr_access.obj;
        n->as.attr_assign.attr  = lhs->as.attr_access.attr;
        n->as.attr_assign.value = parse_expr();
        return n;
    }

    AxonNode* n = node(AXON_NODE_EXPR_STMT);
    n->as.expr_stmt.expr = lhs;
    return n;
}

AxonNode* Parser::parse_if() {
    AxonNode* n = node(AXON_NODE_IF_STMT);
    advance(); // 'if'
    n->as.if_stmt.condition = parse_expr();
    axon_nodelist_init(&n->as.if_stmt.then_body);
    axon_nodelist_init(&n->as.if_stmt.else_body);
    expect(TK::LBRACE,"if body");
    while (!check(TK::RBRACE)&&!at_end()) {
        AxonNode* s=parse_statement();
        if(s) axon_nodelist_push(&n->as.if_stmt.then_body,s,_arena);
    }
    expect(TK::RBRACE,"if body close");
    // else if / else — check BEFORE consuming else token
    if (check(TK::KW_ELSE)) {
        advance(); // consume 'else'
        if (check(TK::KW_IF)) {
            // else if: recurse directly without expecting braces
            AxonNode* elif_node = parse_if();
            if (elif_node)
                axon_nodelist_push(&n->as.if_stmt.else_body, elif_node, _arena);
        } else {
            // plain else: expect braces
            expect(TK::LBRACE, "else body");
            while (!check(TK::RBRACE) && !at_end()) {
                AxonNode* s = parse_statement();
                if (s) axon_nodelist_push(&n->as.if_stmt.else_body, s, _arena);
            }
            expect(TK::RBRACE, "else body close");
        }
    }
    return n;
}

AxonNode* Parser::parse_for() {
    AxonNode* n = node(AXON_NODE_FOR_STMT);
    advance(); // 'for'
    n->as.for_stmt.var = intern(expect(TK::IDENT,"for var").value);
    expect(TK::KW_IN,"for in");
    n->as.for_stmt.iter = parse_expr();
    axon_nodelist_init(&n->as.for_stmt.body);
    expect(TK::LBRACE,"for body");
    while (!check(TK::RBRACE)&&!at_end()) {
        AxonNode* s=parse_statement();
        if(s) axon_nodelist_push(&n->as.for_stmt.body,s,_arena);
    }
    expect(TK::RBRACE,"for body close");
    return n;
}

AxonNode* Parser::parse_while() {
    AxonNode* n = node(AXON_NODE_WHILE_STMT);
    advance(); // 'while'
    n->as.while_stmt.condition = parse_expr();
    axon_nodelist_init(&n->as.while_stmt.body);
    expect(TK::LBRACE,"while body");
    while (!check(TK::RBRACE)&&!at_end()) {
        AxonNode* s=parse_statement();
        if(s) axon_nodelist_push(&n->as.while_stmt.body,s,_arena);
    }
    expect(TK::RBRACE,"while body close");
    return n;
}

AxonNode* Parser::parse_match() {
    AxonNode* n = node(AXON_NODE_MATCH_STMT);
    advance(); // 'match'
    n->as.match_stmt.subject = parse_expr();
    axon_nodelist_init(&n->as.match_stmt.arms);
    expect(TK::LBRACE,"match body");
    while (!check(TK::RBRACE)&&!at_end()) {
        AxonNode* arm = node(AXON_NODE_MATCH_ARM);
        // pattern: ident, string, number, or _
        if (check(TK::WILDCARD)) {
            AxonNode* w=node(AXON_NODE_IDENTIFIER);
            w->as.identifier.name=intern("_"); advance();
            arm->as.match_arm.pattern=w;
        } else {
            arm->as.match_arm.pattern = parse_primary();
        }
        expect(TK::ARROW,"match arm ->");
        // body: block, return, raise, or single expr
        if (check(TK::LBRACE)) {
            // treat as inline block — parse statements
            advance();
            AxonNode* blk=node(AXON_NODE_MAIN_BLOCK);
            axon_nodelist_init(&blk->as.main_block.body);
            while (!check(TK::RBRACE)&&!at_end()) {
                AxonNode* s=parse_statement();
                if(s) axon_nodelist_push(&blk->as.main_block.body,s,_arena);
            }
            expect(TK::RBRACE,"match arm block close");
            arm->as.match_arm.body = blk;
        } else if (check(TK::KW_RETURN)) {
            arm->as.match_arm.body = parse_return();
        } else if (check(TK::KW_RAISE)) {
            arm->as.match_arm.body = parse_raise();
        } else {
            arm->as.match_arm.body = parse_expr();
        }
        axon_nodelist_push(&n->as.match_stmt.arms, arm, _arena);
    }
    expect(TK::RBRACE,"match body close");
    return n;
}

AxonNode* Parser::parse_try() {
    AxonNode* n = node(AXON_NODE_TRY_STMT);
    advance(); // 'try'
    axon_nodelist_init(&n->as.try_stmt.try_body);
    expect(TK::LBRACE,"try body");
    while (!check(TK::RBRACE)&&!at_end()) {
        AxonNode* s=parse_statement();
        if(s) axon_nodelist_push(&n->as.try_stmt.try_body,s,_arena);
    }
    expect(TK::RBRACE,"try body close");
    expect(TK::KW_CATCH,"catch");
    n->as.try_stmt.catch_var = intern(expect(TK::IDENT,"catch var").value);
    axon_nodelist_init(&n->as.try_stmt.catch_body);
    expect(TK::LBRACE,"catch body");
    while (!check(TK::RBRACE)&&!at_end()) {
        AxonNode* s=parse_statement();
        if(s) axon_nodelist_push(&n->as.try_stmt.catch_body,s,_arena);
    }
    expect(TK::RBRACE,"catch body close");
    return n;
}

AxonNode* Parser::parse_raise() {
    AxonNode* n = node(AXON_NODE_RAISE_STMT);
    advance(); // 'raise'
    n->as.raise_stmt.value = parse_expr();
    return n;
}

AxonNode* Parser::parse_return() {
    AxonNode* n = node(AXON_NODE_RETURN_STMT);
    advance(); // 'return'
    if (!check(TK::RBRACE) && !at_end())
        n->as.return_stmt.value = parse_expr();
    return n;
}

// ── func definition ─────────────────────────────────────────
// func name(p1, p2) { body }   — named
// func(p1, p2) { body }        — anonymous expression
// (p1, p2) => expr             — arrow
AxonNode* Parser::parse_func_def() {
    AxonNode* n = node(AXON_NODE_FUNC_DEF);
    n->as.func_def.name     = nullptr;
    n->as.func_def.is_arrow = false;
    axon_nodelist_init(&n->as.func_def.params);

    advance(); // consume 'func'

    // Optional name
    if (current().type == TK::IDENT) {
        n->as.func_def.name = intern(current().value);
        advance();
    }

    // Parameters ( p1, p2, ... )
    expect(TK::LPAREN, "func params");
    while (!check(TK::RPAREN) && !at_end()) {
        std::string pname;
        // Accept any identifier or keyword as param name
        pname = current().value; advance();
        // Skip ': type' annotation
        if (check(TK::COLON)) { advance(); advance(); }
        // Skip '= default' value
        if (check(TK::ASSIGN)) { advance(); parse_expr(); }
        // Build identifier node for param name
        AxonNode* p = node(AXON_NODE_IDENTIFIER);
        p->as.identifier.name = intern(pname);
        axon_nodelist_push(&n->as.func_def.params, p, _arena);
        if (check(TK::COMMA)) advance();
    }
    expect(TK::RPAREN, "func params close");

    // Skip '-> return_type' annotation
    if (check(TK::ARROW)) { advance(); advance(); }

    // Body: { block } or => expression
    if (check(TK::FAT_ARROW)) {
        advance(); // consume '=>'
        n->as.func_def.is_arrow = true;
        n->as.func_def.body = parse_expr();
    } else {
        // Parse block as a main-block-like node
        n->as.func_def.body = node(AXON_NODE_MAIN_BLOCK);
        axon_nodelist_init(&n->as.func_def.body->as.main_block.body);
        expect(TK::LBRACE, "func body");
        while (!check(TK::RBRACE) && !at_end()) {
            AxonNode* s = parse_statement();
            if (s) axon_nodelist_push(
                &n->as.func_def.body->as.main_block.body, s, _arena);
        }
        expect(TK::RBRACE, "func body close");
    }
    return n;
}

AxonNode* Parser::parse_interop_block() {
    AxonNode* n = node(AXON_NODE_INTEROP_BLOCK);
    AxonInteropLang lang;
    switch(current().type) {
        case TK::KW_PYTHON: lang=AXON_INTEROP_PYTHON; break;
        case TK::KW_SQL:    lang=AXON_INTEROP_SQL;    break;
        case TK::KW_SHELL:  lang=AXON_INTEROP_SHELL;  break;
        default:            lang=AXON_INTEROP_CPP;    break;
    }
    advance(); // consume lang keyword
    expect(TK::LBRACE,"interop block open");
    // Use lexer's raw block reader — but we already have tokens.
    // We need the raw source. Collect token values until matching '}'.
    // Simpler: RAW_CODE token is produced by the lexer before we reach here
    // if the lexer's read_raw_block() was called. Since we tokenized
    // upfront, we need to collect raw code from token stream.
    // Solution: after LBRACE is consumed, collect everything until
    // matching RBRACE as raw code.
    std::string code;
    int depth = 1;
    while (!at_end() && depth > 0) {
        const Token& t = current();
        if (t.type == TK::LBRACE) { ++depth; code += "{\n"; }
        else if (t.type == TK::RBRACE) {
            if (--depth == 0) break;
            code += "}\n";
        } else {
            code += t.value;
            if (t.type == TK::STRING) code = "\"" + code + "\"";
            code += " ";
        }
        advance();
    }
    if (!check(TK::RBRACE)) error("unterminated interop block");
    else advance(); // consume '}'

    n->as.interop_block.lang = lang;
    n->as.interop_block.code = intern(code);
    return n;
}

// ── Graph ─────────────────────────────────────────────────────
AxonNode* Parser::parse_graph() {
    AxonNode* n = node(AXON_NODE_GRAPH_DEF);
    advance(); // 'graph'
    axon_nodelist_init(&n->as.graph_def.edges);
    expect(TK::LBRACE,"graph body");
    while (!check(TK::RBRACE)&&!at_end()) {
        AxonNode* e = parse_graph_edge();
        if (e) axon_nodelist_push(&n->as.graph_def.edges, e, _arena);
    }
    expect(TK::RBRACE,"graph body close");
    return n;
}

AxonNode* Parser::parse_graph_edge() {
    AxonNode* n = node(AXON_NODE_GRAPH_EDGE);
    n->as.graph_edge.from = intern(expect(TK::IDENT,"edge from").value);
    expect(TK::ARROW,"edge arrow");
    // 'to' can be IDENT or 'done'
    if (check(TK::KW_DONE)) {
        n->as.graph_edge.to = intern("done"); advance();
    } else {
        n->as.graph_edge.to = intern(expect(TK::IDENT,"edge to").value);
    }
    // optional edge attrs [...]
    if (match(TK::LBRACKET)) {
        while (!check(TK::RBRACKET)&&!at_end()) {
            if (check(TK::KW_ON_SUCCESS)) {
                n->as.graph_edge.attrs.on_success=1; advance();
            } else if (check(TK::KW_ON_FAIL)) {
                n->as.graph_edge.attrs.on_fail=1; advance();
            } else if (check(TK::KW_MAX_RETRIES)) {
                advance(); expect(TK::ASSIGN,"max_retries");
                n->as.graph_edge.attrs.max_retries =
                    std::atoi(expect(TK::NUMBER,"retry count").value.c_str());
                n->as.graph_edge.attrs.has_max_retries=1;
            } else if (check(TK::KW_CONDITION)) {
                advance(); expect(TK::ASSIGN,"condition");
                n->as.graph_edge.attrs.condition =
                    intern(expect(TK::STRING,"condition expr").value);
            } else { advance(); }
            match(TK::COMMA);
        }
        expect(TK::RBRACKET,"edge attrs close");
    }
    return n;
}

// ── Role ─────────────────────────────────────────────────────
AxonNode* Parser::parse_role() {
    AxonNode* n = node(AXON_NODE_ROLE_DEF);
    advance(); // 'role'
    n->as.role_def.name = intern(expect(TK::IDENT,"role name").value);
    axon_kvlist_init(&n->as.role_def.entries);
    expect(TK::LBRACE,"role body");
    parse_kv_block(&n->as.role_def.entries);
    return n;
}

// ── Embedding ─────────────────────────────────────────────────
AxonNode* Parser::parse_embedding() {
    AxonNode* n = node(AXON_NODE_EMBEDDING_DEF);
    advance(); // 'embedding'
    n->as.embedding_def.name = intern(expect(TK::IDENT,"embedding name").value);
    axon_kvlist_init(&n->as.embedding_def.entries);
    expect(TK::LBRACE,"embedding body");
    parse_kv_block(&n->as.embedding_def.entries);
    return n;
}

// ── KV block  { key = val key = val } ────────────────────────
void Parser::parse_kv_block(AxonKVList* list) {
    while (!check(TK::RBRACE) && !at_end()) {
        // Guard: only idents and keywords can be keys
        if (!check(TK::IDENT) && current().type < TK::KW_IMPORT) {
            advance(); continue;
        }
        std::string key = current().value; advance();
        // Guard: must have = after key
        if (!check(TK::ASSIGN)) { continue; }
        advance(); // consume =
        AxonNode* val = parse_expr();
        if (val) axon_kvlist_push(list, intern(key), val, _arena);
    }
    expect(TK::RBRACE, "block close");
}

// ── Expressions ───────────────────────────────────────────────
AxonNode* Parser::parse_expr()    { return parse_pipeline(); }

// ── parse_pipeline: a >> b >> [c,d] >> e ─────────────────────
AxonNode* Parser::parse_pipeline() {
    AxonNode* left = parse_or();
    while (check(TK::PIPELINE)) {
        advance(); // consume >>
        AxonNode* n = node(AXON_NODE_PIPELINE);
        n->as.pipeline.input    = left;
        n->as.pipeline.parallel = 0;
        axon_nodelist_init(&n->as.pipeline.stages);
        if (check(TK::LBRACKET)) {
            // parallel group: [AgentA, AgentB]
            advance();
            n->as.pipeline.parallel = 1;
            while (!check(TK::RBRACKET) && !at_end()) {
                axon_nodelist_push(&n->as.pipeline.stages,
                                   parse_or(), _arena);
                match(TK::COMMA);
            }
            expect(TK::RBRACKET, "pipeline parallel group");
        } else {
            axon_nodelist_push(&n->as.pipeline.stages,
                               parse_or(), _arena);
        }
        left = n;
    }
    return left;
}

AxonNode* Parser::parse_or() {
    AxonNode* left = parse_and();
    while (check(TK::KW_OR)) {
        advance();
        AxonNode* n = node(AXON_NODE_BINARY_EXPR);
        n->as.binary_expr.op=AXON_OP_OR;
        n->as.binary_expr.left=left;
        n->as.binary_expr.right=parse_and();
        left=n;
    }
    return left;
}

AxonNode* Parser::parse_and() {
    AxonNode* left = parse_not();
    while (check(TK::KW_AND)) {
        advance();
        AxonNode* n = node(AXON_NODE_BINARY_EXPR);
        n->as.binary_expr.op=AXON_OP_AND;
        n->as.binary_expr.left=left;
        n->as.binary_expr.right=parse_not();
        left=n;
    }
    return left;
}

AxonNode* Parser::parse_not() {
    if (check(TK::KW_NOT)) {
        advance();
        AxonNode* n=node(AXON_NODE_UNARY_EXPR);
        n->as.unary_expr.op=AXON_OP_NOT;
        n->as.unary_expr.operand=parse_not();
        return n;
    }
    return parse_compare();
}

AxonNode* Parser::parse_compare() {
    AxonNode* left = parse_add();
    while (true) {
        AxonOp op;
        bool found=true;
        switch(current().type){
            case TK::EQ:  op=AXON_OP_EQ;  break;
            case TK::NEQ: op=AXON_OP_NEQ; break;
            case TK::LT:  op=AXON_OP_LT;  break;
            case TK::GT:  op=AXON_OP_GT;  break;
            case TK::LTE: op=AXON_OP_LTE; break;
            case TK::GTE: op=AXON_OP_GTE; break;
            default: found=false;
        }
        if (!found) break;
        advance();
        AxonNode* n=node(AXON_NODE_BINARY_EXPR);
        n->as.binary_expr.op=op;
        n->as.binary_expr.left=left;
        n->as.binary_expr.right=parse_add();
        left=n;
    }
    return left;
}

AxonNode* Parser::parse_add() {
    AxonNode* left = parse_mul();
    while (check(TK::PLUS)||check(TK::MINUS)) {
        AxonOp op = check(TK::PLUS)?AXON_OP_ADD:AXON_OP_SUB;
        advance();
        AxonNode* n=node(AXON_NODE_BINARY_EXPR);
        n->as.binary_expr.op=op;
        n->as.binary_expr.left=left;
        n->as.binary_expr.right=parse_mul();
        left=n;
    }
    return left;
}

AxonNode* Parser::parse_mul() {
    AxonNode* left = parse_power();
    while (check(TK::STAR)||check(TK::SLASH)||check(TK::PERCENT)) {
        AxonOp op;
        if(check(TK::STAR))op=AXON_OP_MUL;
        else if(check(TK::SLASH))op=AXON_OP_DIV;
        else op=AXON_OP_MOD;
        advance();
        AxonNode* n=node(AXON_NODE_BINARY_EXPR);
        n->as.binary_expr.op=op;
        n->as.binary_expr.left=left;
        n->as.binary_expr.right=parse_power();
        left=n;
    }
    return left;
}

AxonNode* Parser::parse_power() {
    AxonNode* base = parse_unary();
    if (check(TK::POWER)) {
        advance();
        AxonNode* n = node(AXON_NODE_BINARY_EXPR);
        n->as.binary_expr.op    = AXON_OP_POW;
        n->as.binary_expr.left  = base;
        n->as.binary_expr.right = parse_power(); // right-associative
        return n;
    }
    return base;
}

AxonNode* Parser::parse_unary() {
    if (check(TK::MINUS)) {
        advance();
        AxonNode* n=node(AXON_NODE_UNARY_EXPR);
        n->as.unary_expr.op=AXON_OP_NEG;
        n->as.unary_expr.operand=parse_unary();
        return n;
    }
    return parse_postfix();
}

AxonNode* Parser::parse_postfix() {
    AxonNode* base = parse_primary();
    while (true) {
        if (check(TK::DOT)) {
            advance();
            std::string attr = current().value; advance();
            if (check(TK::LPAREN)) {
                // method call
                advance();
                AxonNode* n=node(AXON_NODE_METHOD_CALL);
                n->as.method_call.obj=base;
                n->as.method_call.method=intern(attr);
                axon_nodelist_init(&n->as.method_call.args);
                while (!check(TK::RPAREN)&&!at_end()) {
                    AxonNode* a=parse_expr();
                    axon_nodelist_push(&n->as.method_call.args,a,_arena);
                    match(TK::COMMA);
                }
                expect(TK::RPAREN,"method call");
                base=n;
            } else {
                AxonNode* n=node(AXON_NODE_ATTR_ACCESS);
                n->as.attr_access.obj=base;
                n->as.attr_access.attr=intern(attr);
                base=n;
            }
        } else if (check(TK::LBRACKET) && base && base->type != AXON_NODE_LIST) {
            advance();
            AxonNode* n=node(AXON_NODE_INDEX_ACCESS);
            n->as.index_access.obj=base;
            n->as.index_access.index=parse_expr();
            expect(TK::RBRACKET,"index access");
            n->as.index_access.safe = match(TK::QUESTION) ? 1 : 0;
            base=n;
        } else if (check(TK::LPAREN)) {
            advance();
            AxonNode* n=node(AXON_NODE_FUNC_CALL);
            n->as.func_call.callee=base;
            axon_nodelist_init(&n->as.func_call.args);
            while (!check(TK::RPAREN)&&!at_end()) {
                axon_nodelist_push(&n->as.func_call.args,
                                   parse_expr(),_arena);
                match(TK::COMMA);
            }
            expect(TK::RPAREN,"func call");
            base=n;
        } else if (check(TK::ARROW)) {
            // Agent->Task(args) or module->func(args)
            advance(); // consume ->
            std::string task_name = current().value; advance();
            AxonNode* n = node(AXON_NODE_TASK_CALL);
            n->as.task_call.agent = base;
            n->as.task_call.task  = intern(task_name);
            axon_nodelist_init(&n->as.task_call.args);
            if (check(TK::LPAREN)) {
                advance(); // consume (
                while (!check(TK::RPAREN) && !at_end()) {
                    axon_nodelist_push(&n->as.task_call.args,
                                       parse_expr(), _arena);
                    match(TK::COMMA);
                }
                expect(TK::RPAREN, "task call args");
            }
            base = n;
        } else if (check(TK::QUESTION)) {
            advance();
            AxonNode* n=node(AXON_NODE_SAFE_ACCESS);
            n->as.safe_access.obj=base;
            base=n;
        } else break;
    }
    return base;
}

AxonNode* Parser::parse_primary() {
    // Anonymous function as expression: func(params){} or (params)=>expr
    if (check(TK::KW_FUNC)) {
        return parse_func_def();
    }
    // Arrow function: (params) => expr
    // Detect: LPAREN followed by ident/comma patterns then RPAREN FAT_ARROW
    // Simple heuristic: if next after potential params is =>, treat as arrow
    if (check(TK::LPAREN)) {
        // Peek to see if this is (params) => ...
        // Save position, try to parse params, check for FAT_ARROW
        size_t saved_pos = _pos;
        bool is_arrow = false;
        advance(); // (
        int depth = 1;
        while (!at_end() && depth > 0) {
            if (check(TK::LPAREN)) depth++;
            else if (check(TK::RPAREN)) depth--;
            advance();
        }
        if (check(TK::FAT_ARROW)) is_arrow = true;
        _pos = saved_pos; // restore

        if (is_arrow) {
            AxonNode* n = node(AXON_NODE_ANON_FUNC);
            n->as.func_def.name     = nullptr;
            n->as.func_def.is_arrow = true;
            axon_nodelist_init(&n->as.func_def.params);
            advance(); // consume (
            while (!check(TK::RPAREN) && !at_end()) {
                std::string pname = current().value; advance();
                if (check(TK::COLON)) { advance(); advance(); } // skip type
                AxonNode* p = node(AXON_NODE_IDENTIFIER);
                p->as.identifier.name = intern(pname);
                axon_nodelist_push(&n->as.func_def.params, p, _arena);
                if (check(TK::COMMA)) advance();
            }
            expect(TK::RPAREN, "arrow params close");
            expect(TK::FAT_ARROW, "=>");
            n->as.func_def.body = parse_expr();
            return n;
        }
    }

    switch (current().type) {
        case TK::NUMBER: {
            AxonNode* n=node(AXON_NODE_NUMBER);
            n->as.number.value=std::atof(advance().value.c_str());
            return n;
        }
        case TK::STRING: {
            AxonNode* n=node(AXON_NODE_STRING);
            n->as.string.value=intern(advance().value);
            return n;
        }
        case TK::KW_TRUE: {
            advance();
            AxonNode* n=node(AXON_NODE_BOOL);
            n->as.boolean.value=1; return n;
        }
        case TK::KW_FALSE: {
            advance();
            AxonNode* n=node(AXON_NODE_BOOL);
            n->as.boolean.value=0; return n;
        }
        case TK::KW_NULL: {
            advance(); return node(AXON_NODE_NULL);
        }
        case TK::KW_TYPEOF: {
            advance(); // consume typeof
            expect(TK::LPAREN, "typeof(");
            AxonNode* n = node(AXON_NODE_TYPEOF);
            n->as.typeof_expr.expr = parse_expr();
            expect(TK::RPAREN, "typeof)");
            return n;
        }
        case TK::FSTRING: {
            AxonNode* n = node(AXON_NODE_FSTRING);
            n->as.fstring.raw = intern(advance().value);
            return n;
        }
        case TK::LBRACKET: return parse_list();
        case TK::LBRACE:   return parse_dict();
        case TK::LPAREN: {
            advance();
            AxonNode* e=parse_expr();
            expect(TK::RPAREN,"grouped expr");
            return e;
        }
        // Keywords used as identifiers in expressions (e.g. flow.input)
        default: {
            AxonNode* n=node(AXON_NODE_IDENTIFIER);
            n->as.identifier.name=intern(advance().value);
            return n;
        }
    }
}

AxonNode* Parser::parse_list() {
    AxonNode* n=node(AXON_NODE_LIST);
    axon_nodelist_init(&n->as.list.elements);
    advance(); // '['
    while (!check(TK::RBRACKET)&&!at_end()) {
        axon_nodelist_push(&n->as.list.elements,parse_expr(),_arena);
        match(TK::COMMA);
    }
    expect(TK::RBRACKET,"list close");
    return n;
}

AxonNode* Parser::parse_dict() {
    AxonNode* n=node(AXON_NODE_DICT);
    axon_nodelist_init(&n->as.dict.pairs);
    advance(); // '{'
    while (!check(TK::RBRACE)&&!at_end()) {
        AxonNode* pair=node(AXON_NODE_DICT_PAIR);
        pair->as.dict_pair.key=intern(advance().value);
        expect(TK::COLON,"dict pair");
        pair->as.dict_pair.value=parse_expr();
        axon_nodelist_push(&n->as.dict.pairs,pair,_arena);
        match(TK::COMMA);
    }
    expect(TK::RBRACE,"dict close");
    return n;
}


// ── parse_task_graph ─────────────────────────────────────────
AxonNode* Parser::parse_task_graph() {
    AxonNode* n = node(AXON_NODE_TASK_GRAPH);
    advance(); // consume 'task_graph'
    axon_nodelist_init(&n->as.task_graph.edges);
    expect(TK::LBRACE, "task_graph body");
    while (!check(TK::RBRACE) && !at_end()) {
        AxonNode* edge = node(AXON_NODE_TASK_EDGE);
        edge->as.task_edge.from = intern(expect(TK::IDENT, "task edge from").value);
        expect(TK::ARROW, "task edge arrow");
        // to: task name or 'done'
        if (check(TK::KW_DONE)) {
            edge->as.task_edge.to = intern("done"); advance();
        } else {
            edge->as.task_edge.to = intern(expect(TK::IDENT, "task edge to").value);
        }
        // optional edge attributes [...]
        if (match(TK::LBRACKET)) {
            while (!check(TK::RBRACKET) && !at_end()) {
                if (check(TK::KW_ON_SUCCESS)) {
                    edge->as.task_edge.attrs.on_success = 1; advance();
                } else if (check(TK::KW_ON_FAIL)) {
                    edge->as.task_edge.attrs.on_fail = 1; advance();
                } else if (check(TK::KW_MAX_RETRIES)) {
                    advance(); expect(TK::ASSIGN, "max_retries");
                    edge->as.task_edge.attrs.max_retries =
                        std::atoi(expect(TK::NUMBER, "retry count").value.c_str());
                    edge->as.task_edge.attrs.has_max_retries = 1;
                } else if (check(TK::KW_CONDITION)) {
                    advance(); expect(TK::ASSIGN, "condition");
                    edge->as.task_edge.attrs.condition =
                        intern(expect(TK::STRING, "condition value").value);
                } else { advance(); }
                match(TK::COMMA);
            }
            expect(TK::RBRACKET, "task edge attrs close");
        }
        axon_nodelist_push(&n->as.task_graph.edges, edge, _arena);
    }
    expect(TK::RBRACE, "task_graph body close");
    return n;
}

// ── parse_io_block ────────────────────────────────────────────
AxonNode* Parser::parse_io_block() {
    AxonNode* n = node(AXON_NODE_IO_BLOCK);
    advance(); // consume 'io'
    axon_nodelist_init(&n->as.io_block.channels);
    expect(TK::LBRACE, "io block");
    while (!check(TK::RBRACE) && !at_end()) {
        AxonNode* ch = node(AXON_NODE_IO_CHANNEL);

        // direction: receive | emit
        if (check(TK::KW_RECEIVE)) {
            ch->as.io_channel.direction = AXON_IO_RECEIVE; advance();
        } else if (check(TK::KW_EMIT)) {
            ch->as.io_channel.direction = AXON_IO_EMIT; advance();
        } else {
            error("expected 'receive' or 'emit' in io block");
            advance(); continue;
        }
        expect(TK::ASSIGN, "io channel");

        // transport: grpc(...) | socket(...) | ultrasonic | next_task
        if (check(TK::KW_GRPC)) {
            ch->as.io_channel.transport = AXON_IO_GRPC; advance();
            expect(TK::LPAREN, "grpc address");
            ch->as.io_channel.address =
                intern(expect(TK::STRING, "grpc address string").value);
            expect(TK::RPAREN, "grpc address close");
        } else if (check(TK::KW_SOCKET)) {
            ch->as.io_channel.transport = AXON_IO_SOCKET; advance();
            expect(TK::LPAREN, "socket address");
            ch->as.io_channel.address =
                intern(expect(TK::STRING, "socket address string").value);
            expect(TK::RPAREN, "socket address close");
        } else if (check(TK::KW_ULTRASONIC)) {
            ch->as.io_channel.transport = AXON_IO_ULTRASONIC;
            ch->as.io_channel.address   = nullptr;
            advance();
        } else if (check(TK::KW_NEXT_TASK)) {
            ch->as.io_channel.transport = AXON_IO_NEXT_TASK;
            ch->as.io_channel.address   = nullptr;
            advance();
        } else {
            error("unknown io transport: '" + current().value + "'");
            advance(); continue;
        }
        axon_nodelist_push(&n->as.io_block.channels, ch, _arena);
    }
    expect(TK::RBRACE, "io block close");
    return n;
}

// ── parse_run ─────────────────────────────────────────────────
AxonNode* Parser::parse_run() {
    AxonNode* n = node(AXON_NODE_RUN_STMT);
    advance(); // consume 'run'
    // target: task_graph or a task name
    n->as.run_stmt.target = intern(current().value);
    advance();
    return n;
}


// ── parse_expression (public) ─────────────────────────────────
AxonNode* Parser::parse_expression() {
    return parse_expr();
}

// ── parse_storage_def ─────────────────────────────────────────
AxonNode* Parser::parse_storage_def() {
    AxonNode* n = node(AXON_NODE_STORAGE_DEF);
    advance(); // consume 'storage'
    n->as.storage_def.name = intern(expect(TK::IDENT, "storage name").value);
    axon_kvlist_init(&n->as.storage_def.entries);
    expect(TK::LBRACE, "storage body");
    parse_kv_block(&n->as.storage_def.entries);
    return n;
}

// ── parse_global_main ─────────────────────────────────────────
AxonNode* Parser::parse_global_main() {
    AxonNode* n = node(AXON_NODE_GLOBAL_MAIN);
    advance(); // consume 'main'
    axon_nodelist_init(&n->as.global_main.body);
    expect(TK::LBRACE, "main body");
    while (!check(TK::RBRACE) && !at_end()) {
        AxonNode* s = parse_statement();
        if (s) axon_nodelist_push(&n->as.global_main.body, s, _arena);
    }
    expect(TK::RBRACE, "main body close");
    return n;
}

// ── parse_output_config ────────────────────────────────────────
void Parser::parse_output_config(AxonOutputConfig* cfg) {
    expect(TK::LBRACE, "output block");
    while (!check(TK::RBRACE) && !at_end()) {
        std::string key = current().value; advance();
        expect(TK::ASSIGN, "output kv");
        std::string val = current().value; advance();

        if (key == "format") {
            if      (val == "minimal")    cfg->format = AXON_OUT_MINIMAL;
            else if (val == "json")       cfg->format = AXON_OUT_JSON;
            else if (val == "silent")     cfg->format = AXON_OUT_SILENT;
            else                          cfg->format = AXON_OUT_STRUCTURED;
        } else if (key == "level") {
            if      (val == "debug")      cfg->level = AXON_LOG_DEBUG;
            else if (val == "warn")       cfg->level = AXON_LOG_WARN;
            else if (val == "error")      cfg->level = AXON_LOG_ERROR;
            else                          cfg->level = AXON_LOG_INFO;
        } else if (key == "target") {
            cfg->to_stdout = (val == "stdout" || val == "both") ? 1 : 0;
            cfg->to_file   = (val == "file"   || val == "both") ? 1 : 0;
        } else if (key == "file") {
            cfg->file_path = intern(val);
        } else if (key == "metrics") {
            cfg->metrics = (val == "true") ? 1 : 0;
        }
    }
    expect(TK::RBRACE, "output block close");
}

// ── parse_parallel_block ───────────────────────────────────────
AxonNode* Parser::parse_parallel_block() {
    AxonNode* n = node(AXON_NODE_PARALLEL_BLOCK);
    advance(); // consume 'parallel'
    axon_nodelist_init(&n->as.parallel_block.assignments);
    expect(TK::LBRACE, "parallel body");
    while (!check(TK::RBRACE) && !at_end()) {
        // Each line: var = Agent.Task(input) or expr
        AxonNode* s = parse_statement();
        if (s) axon_nodelist_push(&n->as.parallel_block.assignments, s, _arena);
    }
    expect(TK::RBRACE, "parallel body close");
    return n;
}

// ── parse_spawn ────────────────────────────────────────────────
AxonNode* Parser::parse_spawn() {
    AxonNode* n = node(AXON_NODE_SPAWN_STMT);
    advance(); // consume 'spawn'
    n->as.spawn_stmt.agent_name = intern(expect(TK::IDENT, "spawn agent").value);
    n->as.spawn_stmt.var_name   = nullptr;
    n->as.spawn_stmt.input      = nullptr;
    if (check(TK::LPAREN)) {
        advance();
        if (!check(TK::RPAREN))
            n->as.spawn_stmt.input = parse_expr();
        expect(TK::RPAREN, "spawn args");
    }
    return n;
}

} // namespace axon
