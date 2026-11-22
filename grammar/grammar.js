/**
 * grammar.js — Axon Language Tree-sitter Grammar
 * Cognitive Flow Oriented Language (.axon)
 *
 * Compile with:  tree-sitter generate
 * Test with:     tree-sitter parse examples/hello.axon
 */

module.exports = grammar({
  name: "axon",

  extras: $ => [
    $.comment,
    $.block_comment,
    /\s/,
  ],

  word: $ => $.identifier,

  rules: {

    // ── Root ──────────────────────────────────────────────
    source_file: $ => repeat($.top_level),

    top_level: $ => choice(
      $.import_stmt,
      $.flow_def,
      $.agent_def,
      $.embedding_def,
      $.graph_def,
      $.role_def,
    ),

    // ── Imports ───────────────────────────────────────────
    import_stmt: $ => seq(
      "import",
      $.module_path,
    ),

    module_path: $ => sep1($.identifier, "."),

    // ── Flow ──────────────────────────────────────────────
    flow_def: $ => seq(
      "flow",
      field("name", $.identifier),
      "{",
      repeat($.flow_entry),
      "}",
    ),

    flow_entry: $ => seq(
      field("key", $.identifier),
      "=",
      field("value", $.expr),
    ),

    // ── Agent ─────────────────────────────────────────────
    agent_def: $ => seq(
      "agent",
      field("name", $.identifier),
      "{",
      repeat($.agent_body_item),
      "}",
    ),

    agent_body_item: $ => choice(
      $.provider_decl,
      $.model_decl,
      $.role_decl,
      $.memory_block,
      $.storage_block,
      $.validation_block,
      $.learning_block,
      $.task_def,
      $.main_block,
      $.assignment,
    ),

    provider_decl: $ => seq("provider", field("name", $.identifier)),
    model_decl:    $ => seq("model",    field("name", $.model_name)),
    role_decl:     $ => seq("role",     field("name", $.identifier)),

    model_name: $ => /[a-zA-Z0-9_\-:.]+/,

    // ── Memory block ──────────────────────────────────────
    memory_block: $ => seq(
      "memory",
      "{",
      repeat($.memory_entry),
      "}",
    ),

    memory_entry: $ => seq(
      field("type", $.memory_type),
      "=",
      field("value", $.memory_value),
    ),

    memory_type: $ => choice(
      "runtime", "session", "semantic",
      "persistent", "shared", "temporary",
    ),

    memory_value: $ => choice(
      "enabled", "disabled",
      $.identifier,
      $.string,
    ),

    // ── Storage block ─────────────────────────────────────
    storage_block: $ => seq(
      "storage",
      "{",
      repeat($.storage_entry),
      "}",
    ),

    storage_entry: $ => seq(
      field("key", $.identifier),
      "=",
      field("value", $.expr),
    ),

    // ── Validation block ──────────────────────────────────
    validation_block: $ => seq(
      "validation",
      "{",
      repeat($.validation_rule),
      "}",
    ),

    validation_rule: $ => choice(
      $.confidence_rule,
      $.format_rule,
      $.retry_rule,
      $.onfail_rule,
    ),

    confidence_rule: $ => seq(
      "confidence",
      field("op", $.compare_op),
      field("value", $.number),
    ),

    format_rule: $ => seq(
      "format",
      "=",
      field("value", $.identifier),
    ),

    retry_rule: $ => seq(
      "max_retries",
      "=",
      field("value", $.number),
    ),

    onfail_rule: $ => seq(
      "on_fail",
      "=",
      field("action", choice("retry", "flag", "skip", "raise")),
    ),

    // ── Learning block ────────────────────────────────────
    learning_block: $ => seq(
      "learning",
      "{",
      repeat($.learning_entry),
      "}",
    ),

    learning_entry: $ => seq(
      field("key", $.identifier),
      "=",
      field("value", $.expr),
    ),

    // ── Task ──────────────────────────────────────────────
    task_def: $ => seq(
      "task",
      field("name", $.identifier),
      "{",
      repeat($.task_entry),
      "}",
    ),

    task_entry: $ => seq(
      field("key", $.identifier),
      "=",
      field("value", $.expr),
    ),

    // ── Main block ────────────────────────────────────────
    main_block: $ => seq(
      "main",
      "{",
      repeat($.statement),
      "}",
    ),

    // ── Embedding ─────────────────────────────────────────
    embedding_def: $ => seq(
      "embedding",
      field("name", $.identifier),
      "{",
      repeat($.embedding_entry),
      "}",
    ),

    embedding_entry: $ => seq(
      field("key", $.identifier),
      "=",
      field("value", $.expr),
    ),

    // ── Graph ─────────────────────────────────────────────
    graph_def: $ => seq(
      "graph",
      "{",
      repeat($.graph_edge),
      "}",
    ),

    graph_edge: $ => seq(
      field("from", $.identifier),
      "->",
      field("to",   $.identifier),
      optional($.edge_attrs),
    ),

    edge_attrs: $ => seq(
      "[",
      sep1($.edge_attr, ","),
      "]",
    ),

    edge_attr: $ => choice(
      $.on_success_attr,
      $.on_fail_attr,
      $.condition_attr,
      $.max_retries_attr,
    ),

    on_success_attr:   $ => "on_success",
    on_fail_attr:      $ => "on_fail",
    max_retries_attr:  $ => seq("max_retries", "=", $.number),
    condition_attr:    $ => seq(
      "condition",
      "=",
      $.string,
    ),

    // ── Role ──────────────────────────────────────────────
    role_def: $ => seq(
      "role",
      field("name", $.identifier),
      "{",
      repeat($.role_entry),
      "}",
    ),

    role_entry: $ => seq(
      field("key", $.identifier),
      "=",
      field("value", $.expr),
    ),

    // ── Statements ────────────────────────────────────────
    statement: $ => choice(
      $.assignment,
      $.if_stmt,
      $.for_stmt,
      $.while_stmt,
      $.match_stmt,
      $.try_stmt,
      $.raise_stmt,
      $.return_stmt,
      $.interop_block,
      $.expr_stmt,
    ),

    assignment: $ => seq(
      field("name", $.identifier),
      "=",
      field("value", $.expr),
    ),

    if_stmt: $ => seq(
      "if",
      field("condition", $.expr),
      "{",
      field("then", repeat($.statement)),
      "}",
      optional(seq(
        "else",
        "{",
        field("else", repeat($.statement)),
        "}",
      )),
    ),

    for_stmt: $ => seq(
      "for",
      field("var", $.identifier),
      "in",
      field("iter", $.expr),
      "{",
      repeat($.statement),
      "}",
    ),

    while_stmt: $ => seq(
      "while",
      field("cond", $.expr),
      "{",
      repeat($.statement),
      "}",
    ),

    // match — for agent state transitions
    match_stmt: $ => seq(
      "match",
      field("subject", $.expr),
      "{",
      repeat($.match_arm),
      "}",
    ),

    match_arm: $ => seq(
      field("pattern", $.match_pattern),
      "->",
      field("body", $.expr),
    ),

    match_pattern: $ => choice(
      $.identifier,
      $.string,
      $.number,
      "_",               // wildcard
    ),

    try_stmt: $ => seq(
      "try",
      "{",
      repeat($.statement),
      "}",
      "catch",
      field("var", $.identifier),
      "{",
      repeat($.statement),
      "}",
    ),

    raise_stmt:  $ => seq("raise",  $.expr),
    return_stmt: $ => seq("return", optional($.expr)),
    expr_stmt:   $ => $.expr,

    // ── Interop blocks ────────────────────────────────────
    interop_block: $ => seq(
      field("lang", $.interop_lang),
      "{",
      field("code", $.raw_code),
      "}",
    ),

    interop_lang: $ => choice(
      "python", "sql", "shell", "cpp",
    ),

    raw_code: $ => /[^}]*/,

    // ── Expressions ───────────────────────────────────────
    expr: $ => choice(
      $.binary_expr,
      $.unary_expr,
      $.postfix_expr,
      $.primary,
    ),

    binary_expr: $ => choice(
      prec.left(1, seq($.expr, choice("or"),              $.expr)),
      prec.left(2, seq($.expr, choice("and"),             $.expr)),
      prec.left(3, seq($.expr, $.compare_op,              $.expr)),
      prec.left(4, seq($.expr, choice("+", "-"),          $.expr)),
      prec.left(5, seq($.expr, choice("*", "/", "%"),     $.expr)),
    ),

    unary_expr: $ => prec(6, choice(
      seq("-",   $.expr),
      seq("not", $.expr),
    )),

    postfix_expr: $ => choice(
      prec.left(7, seq($.expr, ".", $.identifier)),
      prec.left(7, seq($.expr, ".", $.identifier,
                       "(", optional($.arg_list), ")")),
      prec.left(7, seq($.expr, "(", optional($.arg_list), ")")),
      prec.left(7, seq($.expr, "[", $.expr, "]")),
      prec.left(7, seq($.expr, "[", $.expr, "]", "?")),
      prec.left(7, seq($.expr, "?")),
    ),

    arg_list: $ => sep1($.expr, ","),

    primary: $ => choice(
      $.identifier,
      $.number,
      $.string,
      $.bool_lit,
      $.null_lit,
      $.list_lit,
      $.dict_lit,
      seq("(", $.expr, ")"),
    ),

    list_lit: $ => seq(
      "[", optional(sep1($.expr, ",")), "]",
    ),

    dict_lit: $ => seq(
      "{", optional(sep1($.dict_pair, ",")), "}",
    ),

    dict_pair: $ => seq(
      field("key",   choice($.string, $.identifier)),
      ":",
      field("value", $.expr),
    ),

    compare_op: $ => choice("==", "!=", "<", ">", "<=", ">="),

    bool_lit:  $ => choice("true", "false"),
    null_lit:  $ => "null",

    // ── Terminals ─────────────────────────────────────────
    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,

    number: $ => /[0-9]+(\.[0-9]+)?/,

    string: $ => choice(
      seq('"',  /[^"]*/, '"'),
      seq('"""', /[^]*?/, '"""'),
    ),

    comment: $ => /#[^\n]*/,
  },
});

// ── helpers ───────────────────────────────────────────────────
function sep1(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)));
}
