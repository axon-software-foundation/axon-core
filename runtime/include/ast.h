/**
 * runtime/include/ast.h
 * ──────────────────────
 * C structs for every AST node in the Axon language.
 * These are the in-memory representation after parsing.
 *
 * Design rules:
 *   - Every node starts with AxonNodeType (tagged union style)
 *   - Strings are interned: const char* points to the string pool
 *   - Lists use AxonNodeList (dynamic array, no linked list overhead)
 *   - No heap allocation inside nodes — arena allocator owns memory
 */

#ifndef AXON_AST_H
#define AXON_AST_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─────────────────────────────────────────────────────────────
 * Source location (for error reporting)
 * ───────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t line;
    uint32_t col;
    uint32_t offset;   /* byte offset in source */
} AxonSrcLoc;

/* ─────────────────────────────────────────────────────────────
 * Node type tags
 * ───────────────────────────────────────────────────────────── */
typedef enum {
    /* Top-level */
    AXON_NODE_PROGRAM = 0,
    AXON_NODE_IMPORT,
    AXON_NODE_FLOW_DEF,
    AXON_NODE_AGENT_DEF,
    AXON_NODE_EMBEDDING_DEF,
    AXON_NODE_GRAPH_DEF,
    AXON_NODE_ROLE_DEF,

    /* Agent body */
    AXON_NODE_PROVIDER_DECL,
    AXON_NODE_MODEL_DECL,
    AXON_NODE_ROLE_DECL,
    AXON_NODE_MEMORY_BLOCK,
    AXON_NODE_STORAGE_BLOCK,
    AXON_NODE_VALIDATION_BLOCK,
    AXON_NODE_LEARNING_BLOCK,
    AXON_NODE_TASK_DEF,
    AXON_NODE_MAIN_BLOCK,

    /* Graph */
    AXON_NODE_GRAPH_EDGE,
    AXON_NODE_EDGE_ATTRS,

    /* Statements */
    AXON_NODE_ASSIGNMENT,
    AXON_NODE_IF_STMT,
    AXON_NODE_FOR_STMT,
    AXON_NODE_WHILE_STMT,
    AXON_NODE_MATCH_STMT,
    AXON_NODE_MATCH_ARM,
    AXON_NODE_TRY_STMT,
    AXON_NODE_RAISE_STMT,
    AXON_NODE_RETURN_STMT,
    AXON_NODE_INTEROP_BLOCK,
    AXON_NODE_EXPR_STMT,

    /* Expressions */
    AXON_NODE_BINARY_EXPR,
    AXON_NODE_UNARY_EXPR,
    AXON_NODE_ATTR_ACCESS,
    AXON_NODE_METHOD_CALL,
    AXON_NODE_FUNC_CALL,
    AXON_NODE_INDEX_ACCESS,
    AXON_NODE_SAFE_ACCESS,

    /* Literals */
    AXON_NODE_IDENTIFIER,
    AXON_NODE_NUMBER,
    AXON_NODE_STRING,
    AXON_NODE_BOOL,
    AXON_NODE_NULL,
    AXON_NODE_FSTRING,   // f"hello {name}" interpolated string
    AXON_NODE_TYPEOF,    // typeof(expr)
    AXON_NODE_INDEX_ASSIGN, // obj[key] = value
    AXON_NODE_ATTR_ASSIGN,  // obj.attr = value
    AXON_NODE_LIST,
    AXON_NODE_DICT,
    AXON_NODE_DICT_PAIR,

    // Task graph & I/O
    AXON_NODE_TASK_GRAPH,   // task_graph { ... }
    AXON_NODE_TASK_EDGE,    // edge inside task_graph
    AXON_NODE_IO_BLOCK,     // io { receive = ... emit = ... }
    AXON_NODE_IO_CHANNEL,   // single receive/emit declaration
    AXON_NODE_RUN_STMT,     // run task_graph

    // Phase 2D nodes
    AXON_NODE_TASK_CALL,      // Analyzer->Summarize(input)
    AXON_NODE_PIPELINE,       // a >> b >> [c,d] >> e
    AXON_NODE_PIPELINE_STAGE, // one stage in a pipeline (parallel or single)

    // Phase 2B nodes
    AXON_NODE_STORAGE_DEF,    // top-level: storage Name { }
    AXON_NODE_GLOBAL_MAIN,    // top-level: main { }
    AXON_NODE_PARALLEL_BLOCK, // parallel { }
    AXON_NODE_SPAWN_STMT,     // spawn AgentName(...)
    AXON_NODE_FUNC_DEF,       // func name(params) { body }
    AXON_NODE_ANON_FUNC,      // func(params) { } or (params) => expr

    AXON_NODE_COUNT   /* sentinel */
} AxonNodeType;

/* ─────────────────────────────────────────────────────────────
 * Forward declaration
 * ───────────────────────────────────────────────────────────── */
typedef struct AxonNode AxonNode;

/* ─────────────────────────────────────────────────────────────
 * Dynamic node list (used everywhere in the AST)
 * ───────────────────────────────────────────────────────────── */
typedef struct {
    AxonNode** items;
    size_t     count;
    size_t     capacity;
} AxonNodeList;

/* ─────────────────────────────────────────────────────────────
 * Key-value pair (used in flow entries, task entries, etc.)
 * ───────────────────────────────────────────────────────────── */
typedef struct {
    const char* key;
    AxonNode*   value;
} AxonKVPair;

typedef struct {
    AxonKVPair* items;
    size_t      count;
    size_t      capacity;
} AxonKVList;

/* ─────────────────────────────────────────────────────────────
 * Memory block entry
 * ───────────────────────────────────────────────────────────── */
typedef enum {
    AXON_MEM_RUNTIME    = 0,
    AXON_MEM_SESSION    = 1,
    AXON_MEM_SEMANTIC   = 2,
    AXON_MEM_PERSISTENT = 3,
    AXON_MEM_SHARED     = 4,
    AXON_MEM_TEMPORARY  = 5,
} AxonMemoryType;

typedef struct {
    AxonMemoryType type;
    int            enabled;    /* 1 = enabled, 0 = disabled */
    const char*    ref;        /* optional named reference */
} AxonMemoryEntry;

/* ─────────────────────────────────────────────────────────────
 * Output / logging configuration
 * ───────────────────────────────────────────────────────────── */
typedef enum {
    AXON_OUT_STRUCTURED = 0,
    AXON_OUT_MINIMAL    = 1,
    AXON_OUT_JSON       = 2,
    AXON_OUT_SILENT     = 3,
} AxonOutputFormat;

typedef enum {
    AXON_LOG_DEBUG = 0,
    AXON_LOG_INFO  = 1,
    AXON_LOG_WARN  = 2,
    AXON_LOG_ERROR = 3,
} AxonLogLevel;

typedef struct {
    AxonOutputFormat format;
    AxonLogLevel     level;
    int              to_file;     /* 1 = write to file */
    int              to_stdout;   /* 1 = write to stdout */
    const char*      file_path;   /* NULL = no file output */
    int              metrics;     /* 1 = print metrics table at end */
} AxonOutputConfig;

/* ─────────────────────────────────────────────────────────────
 * Validation rules
 * ───────────────────────────────────────────────────────────── */
typedef enum {
    AXON_ONFAIL_RETRY = 0,
    AXON_ONFAIL_FLAG,
    AXON_ONFAIL_SKIP,
    AXON_ONFAIL_RAISE,
} AxonOnFail;

typedef struct {
    double      confidence_threshold;   /* -1 = not set */
    const char* format;                 /* NULL = not set */
    int         max_retries;            /* -1 = not set */
    AxonOnFail  on_fail;
    int         has_confidence;
    int         has_format;
    int         has_retries;
    int         has_onfail;
} AxonValidation;

/* ─────────────────────────────────────────────────────────────
 * I/O channel types
 * ───────────────────────────────────────────────────────────── */
typedef enum {
    AXON_IO_RECEIVE = 0,
    AXON_IO_EMIT    = 1,
} AxonIODirection;

typedef enum {
    AXON_IO_GRPC       = 0,   /* grpc("host:port/Service/Method") */
    AXON_IO_SOCKET     = 1,   /* socket("host:port") */
    AXON_IO_ULTRASONIC = 2,   /* ultrasonic — physical mesh */
    AXON_IO_NEXT_TASK  = 3,   /* next_task — pass to next in task_graph */
    AXON_IO_FLOW_INPUT = 4,   /* flow.input */
} AxonIOTransport;

/* ─────────────────────────────────────────────────────────────
 * Graph edge attributes
 * ───────────────────────────────────────────────────────────── */
typedef struct {
    int         on_success;
    int         on_fail;
    int         has_max_retries;
    int         max_retries;
    const char* condition;      /* NULL = unconditional */
} AxonEdgeAttrs;

/* ─────────────────────────────────────────────────────────────
 * Interop language
 * ───────────────────────────────────────────────────────────── */
typedef enum {
    AXON_INTEROP_PYTHON = 0,
    AXON_INTEROP_SQL,
    AXON_INTEROP_SHELL,
    AXON_INTEROP_CPP,
} AxonInteropLang;

/* ─────────────────────────────────────────────────────────────
 * Binary / Unary operators
 * ───────────────────────────────────────────────────────────── */
typedef enum {
    AXON_OP_ADD, AXON_OP_SUB, AXON_OP_MUL, AXON_OP_DIV, AXON_OP_MOD, AXON_OP_POW,
    AXON_OP_EQ, AXON_OP_NEQ, AXON_OP_LT, AXON_OP_GT, AXON_OP_LTE, AXON_OP_GTE,
    AXON_OP_AND, AXON_OP_OR,
    AXON_OP_NEG, AXON_OP_NOT,
} AxonOp;

/* ─────────────────────────────────────────────────────────────
 * The universal AST node (tagged union)
 * ───────────────────────────────────────────────────────────── */
struct AxonNode {
    AxonNodeType type;
    AxonSrcLoc   loc;

    union {

        /* PROGRAM */
        struct {
            AxonNodeList stmts;
        } program;

        /* IMPORT */
        struct {
            const char* module;   /* e.g. "session", "stdlib.io" */
        } import_stmt;

        /* FLOW_DEF */
        struct {
            const char*     name;
            AxonKVList      entries;
            AxonOutputConfig output_config;
        } flow_def;

        /* AGENT_DEF */
        struct {
            const char*    name;
            const char*    provider;
            const char*    model;
            const char*    role;
            AxonMemoryEntry memory[6];   /* one per AxonMemoryType */
            AxonValidation  validation;
            AxonKVList      learning;
            AxonNodeList    tasks;
            AxonNode*       task_graph;  /* optional task_graph { } */
            const char*     transport;   /* rest | grpc | socket */
            const char*     endpoint;    /* provider endpoint URL */
            AxonNode*       main_block;
            AxonKVList      extra;       /* other key-value pairs */
        } agent_def;

        /* EMBEDDING_DEF */
        struct {
            const char* name;
            AxonKVList  entries;
        } embedding_def;

        /* GRAPH_DEF */
        struct {
            AxonNodeList edges;
        } graph_def;

        /* GRAPH_EDGE */
        struct {
            const char*   from;
            const char*   to;
            AxonEdgeAttrs attrs;
        } graph_edge;

        /* ROLE_DEF */
        struct {
            const char* name;
            AxonKVList  entries;
        } role_def;

        /* TASK_DEF */
        struct {
            const char* name;
            AxonKVList  entries;
            AxonNode*   io_block;   /* optional io { } block */
        } task_def;

        /* MAIN_BLOCK */
        struct {
            AxonNodeList body;
        } main_block;

        /* ASSIGNMENT */
        struct {
            const char* name;
            AxonNode*   value;
        } assignment;

        /* IF_STMT */
        struct {
            AxonNode*    condition;
            AxonNodeList then_body;
            AxonNodeList else_body;
        } if_stmt;

        /* FOR_STMT */
        struct {
            const char*  var;
            AxonNode*    iter;
            AxonNodeList body;
        } for_stmt;

        /* WHILE_STMT */
        struct {
            AxonNode*    condition;
            AxonNodeList body;
        } while_stmt;

        /* MATCH_STMT */
        struct {
            AxonNode*    subject;
            AxonNodeList arms;
        } match_stmt;

        /* MATCH_ARM */
        struct {
            AxonNode* pattern;
            AxonNode* body;
        } match_arm;

        /* TRY_STMT */
        struct {
            AxonNodeList try_body;
            const char*  catch_var;
            AxonNodeList catch_body;
        } try_stmt;

        /* RAISE_STMT */
        struct { AxonNode* value; } raise_stmt;

        /* RETURN_STMT */
        struct { AxonNode* value; } return_stmt;   /* value may be NULL */

        /* INTEROP_BLOCK */
        struct {
            AxonInteropLang lang;
            const char*     code;
        } interop_block;

        /* EXPR_STMT */
        struct { AxonNode* expr; } expr_stmt;

        /* BINARY_EXPR */
        struct {
            AxonOp    op;
            AxonNode* left;
            AxonNode* right;
        } binary_expr;

        /* UNARY_EXPR */
        struct {
            AxonOp    op;
            AxonNode* operand;
        } unary_expr;

        /* ATTR_ACCESS */
        struct {
            AxonNode*   obj;
            const char* attr;
        } attr_access;

        /* METHOD_CALL */
        struct {
            AxonNode*    obj;
            const char*  method;
            AxonNodeList args;
        } method_call;

        /* FUNC_CALL */
        struct {
            AxonNode*    callee;
            AxonNodeList args;
        } func_call;

        /* INDEX_ACCESS */
        struct {
            AxonNode* obj;
            AxonNode* index;
            int       safe;    /* 1 = arr[i]? */
        } index_access;

        /* SAFE_ACCESS */
        struct { AxonNode* obj; } safe_access;

        /* TASK_CALL: Agent->Task(args) */
        struct {
            AxonNode*    agent;       /* IDENTIFIER node for agent name */
            const char*  task;        /* task name */
            AxonNodeList args;
        } task_call;

        /* PIPELINE: input >> stage >> [a,b] >> result */
        struct {
            AxonNode*    input;       /* left side */
            AxonNodeList stages;      /* right side: 1 or more nodes */
            int          parallel;    /* 1 if stages are in [] */
        } pipeline;

        /* STORAGE_DEF */
        struct {
            const char* name;
            AxonKVList  entries;
        } storage_def;

        /* GLOBAL_MAIN */
        struct {
            AxonNodeList body;
        } global_main;

        /* PARALLEL_BLOCK */
        struct {
            AxonNodeList assignments;  /* each is an ASSIGNMENT node */
        } parallel_block;

        /* SPAWN_STMT */
        struct {
            const char* agent_name;
            const char* var_name;    /* optional: result var */
            AxonNode*   input;
        } spawn_stmt;

        /* TASK_GRAPH */
        struct {
            AxonNodeList edges;     /* AxonNode* of type TASK_EDGE */
        } task_graph;

        /* TASK_EDGE */
        struct {
            const char*   from;
            const char*   to;       /* task name or "done" */
            AxonEdgeAttrs attrs;
        } task_edge;

        /* IO_BLOCK */
        struct {
            AxonNodeList channels;  /* list of IO_CHANNEL nodes */
        } io_block;

        /* IO_CHANNEL */
        struct {
            AxonIODirection direction;
            AxonIOTransport transport;
            const char*     address;   /* NULL for ultrasonic/next_task */
        } io_channel;

        /* RUN_STMT */
        struct {
            const char* target;   /* e.g. "task_graph" */
        } run_stmt;

        /* INDEX_ASSIGN: obj[key] = value */
        struct {
            AxonNode* obj;
            AxonNode* key;
            AxonNode* value;
        } index_assign;

        /* ATTR_ASSIGN: obj.attr = value */
        struct {
            AxonNode* obj;
            const char* attr;
            AxonNode* value;
        } attr_assign;

        /* TYPEOF */
        struct { AxonNode* expr; } typeof_expr;

        /* FSTRING */
        struct {
            const char* raw;   /* raw template with {expr} markers */
        } fstring;

        /* IDENTIFIER */
        struct { const char* name; } identifier;

        /* NUMBER */
        struct { double value; } number;

        /* STRING */
        struct { const char* value; } string;

        /* BOOL */
        struct { int value; } boolean;

        /* LIST */
        struct { AxonNodeList elements; } list;

        /* DICT */
        struct {
            AxonNodeList pairs;   /* each pair is AXON_NODE_DICT_PAIR */
        } dict;

        /* DICT_PAIR */
        struct {
            const char* key;
            AxonNode*   value;
        } dict_pair;

        /* FUNC_DEF / ANON_FUNC */
        struct {
            const char*   name;       /* null for anonymous */
            bool          is_arrow;   /* true if => syntax */
            AxonNodeList  params;     /* parameter name nodes */
            AxonNode*     body;       /* block or expression */
        } func_def;

    } as;
};

/* ─────────────────────────────────────────────────────────────
 * Arena allocator (owns all AST memory)
 * ───────────────────────────────────────────────────────────── */
typedef struct AxonArena {
    uint8_t*        buf;
    size_t          cap;
    size_t          used;
    struct AxonArena* next;   /* linked list of arena pages */
} AxonArena;

AxonArena* axon_arena_new(size_t initial_cap);
void*      axon_arena_alloc(AxonArena* arena, size_t size);
char*      axon_arena_strdup(AxonArena* arena, const char* s);
void       axon_arena_free(AxonArena* arena);

/* ─────────────────────────────────────────────────────────────
 * Node constructors (allocate from arena)
 * ───────────────────────────────────────────────────────────── */
AxonNode* axon_node_new(AxonArena* arena, AxonNodeType type, AxonSrcLoc loc);

/* ─────────────────────────────────────────────────────────────
 * NodeList helpers
 * ───────────────────────────────────────────────────────────── */
void axon_nodelist_init(AxonNodeList* list);
void axon_nodelist_push(AxonNodeList* list, AxonNode* node, AxonArena* arena);

/* ─────────────────────────────────────────────────────────────
 * KVList helpers
 * ───────────────────────────────────────────────────────────── */
void axon_kvlist_init(AxonKVList* list);
void axon_kvlist_push(AxonKVList* list, const char* key,
                      AxonNode* value, AxonArena* arena);

/* ─────────────────────────────────────────────────────────────
 * Debug printer
 * ───────────────────────────────────────────────────────────── */
void axon_ast_print(const AxonNode* node, int indent);

#ifdef __cplusplus
}
#endif

#endif /* AXON_AST_H */
