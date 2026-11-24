# Axon language reference — 0.4.0-alpha.1

This reference describes the current interpreter rather than the historical phase plan.
All `axon` fences here are complete programs checked by `docs.examples` in CTest.
`run` markers mean execution with assertions and no external services; `check` markers mean
syntax checking only. Provider-dependent behavior has separate evidence in [VALIDATION.md](VALIDATION.md).
For native APIs, see [MODULES.md](MODULES.md).

## Values, functions and control flow

Values are `string`, `number` (stored as a double), `bool`, `null`, `list` and `dict`.
`typeof(value)` returns those names. Functions currently use an internal dictionary representation,
so `typeof(function)` returns `dict`; do not depend on its internal fields.
Use `#` or `/* ... */` for comments. Undefined variables produce runtime errors.
`print` concatenates its arguments without inserting separators and adds a newline.

<!-- axon-check: run -->
```axon
func factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}
func multiplier(n) { return (x) => x * n }
main {
    triple = multiplier(3)
    square = func(x) { return x * x }
    if factorial(6) != 720 { raise "recursion" }
    if triple(5) != 15 { raise "closure" }
    if square(7) != 49 { raise "anonymous function" }
    if multiplier(4)(6) != 24 { raise "chained call" }
    if typeof(triple) != "dict" { raise "function representation" }
    message = f"result={triple(5)}"
    if message != "result=15" { raise "interpolation" }
}
```

Functions support positional arguments, recursion, closures, anonymous `func` expressions,
arrow functions and early `return`. User-defined functions require exactly the declared number
of arguments. Missing or extra arguments raise a catchable `ArityError` with the expected and
actual counts and the call's line number, before argument expressions or the body execute.
An explicit `null` counts as an argument. This rule also applies to imported functions,
closures and chained calls. Built-ins, modules and agent/task calls retain their existing contracts.
Named arguments, variadics, default parameter values and type annotations are pending.

<!-- axon-check: run -->
```axon
func add(a, b) { return a + b }
main {
    caught = false
    try { add(1) } catch err {
        if err.find("ArityError:") != 0 { raise "unexpected error" }
        caught = true
    }
    if caught == false { raise "missing argument was accepted" }
    if add(20, 22) != 42 { raise "valid call failed after recovery" }
}
```

Arithmetic includes `+ - * / % **`; comparisons include `== != < > <= >=`.
Logical operators are `&&`, `||` and `!`. Division by zero is a controlled error.
Use `if/else`, `for`, `while`, `match` and `try/catch` for control flow.
An uncaught `raise` produces a CLI error instead of aborting the host process.

<!-- axon-check: run -->
```axon
main {
    total = 0
    for n in [1, 2, 3] { total = total + n }
    while total < 8 { total = total + 1 }
    match total {
        8 -> { total = total + 1 }
        _ -> { raise "match" }
    }
    if total != 9 { raise "loop" }
    try { raise "expected" } catch err {
        if err != "expected" { raise "catch" }
    }
    values = [1, 2]
    values[0] = 42
    record = {"name": "Axon"}
    record["value"] = values[0]
    if record["value"] != 42 { raise "index assignment" }
    if values[-1] != 2 { raise "negative index" }
}
```

## Imports

Use quoted file imports such as `import "math.axon"` or `import "./nested/format.axon"`.
Paths resolve relative to the importing file, not the terminal directory. Canonical paths
prevent duplicate loads; repeated and circular imports are skipped with a warning rather
than executed twice. This is not a full cyclic dependency resolver. Imported names beginning
with `_` are private to the imported environment. Missing files and imported lexer/parser
errors fail execution with diagnostics.

Executable multi-file examples: [main.axon](../examples/validation/main.axon),
[math.axon](../examples/validation/math.axon) and
[format.axon](../examples/validation/nested/format.axon).
Native `file->`, `json->` and other modules do not require a package import.
Selective imports, a package registry and a general `session.new()` API are not documented
as supported contracts here. `axon check` checks the entry file; it does not recursively validate imports.

## Flows and agents

Use a global `main` block for explicit orchestration. A flow declares context, not a
separately callable function. Named inputs such as `Research.input` are available;
`flow.input` refers to the currently selected flow. Multiple flow declarations do not yet
provide complete ownership, selection and context isolation. There is no supported automatic
`Flow->run(...)` contract.

An agent may use a `main` block without any LLM call. This example checks direct calls and an agent pipeline:

<!-- axon-check: run -->
```axon
flow Research { input = "axon" debug = false }
agent Upper {
    provider ollama
    model llama3.2
    main { return __input__.upper() }
}
main {
    direct = Upper(Research.input)
    piped = "example" >> Upper
    if direct != "AXON" { raise "agent argument" }
    if piped != "EXAMPLE" { raise "pipeline argument" }
}
```

Agent-only pipelines are the validated pipeline form; list/group stages do not establish concurrent execution.

## Task input and output

Ollama example: requires a running service and `llama3.2`; CI checks syntax only.

<!-- axon-check: check -->
```axon
flow Research { input = "Choose a fruit." debug = false }
agent Processor {
    provider ollama
    model llama3.2
    temperature = 0.3
    num_ctx = 4096
    task Analyze {
        input = __input__
        prompt = ["Choose one fruit and explain your choice."]
    }
    task Summarize {
        input = Analyze.output
        prompt = ["Give three characteristics of that fruit."]
    }
}
main {
    full = Processor("Choose between apple and mandarin.")
    print(Processor.Analyze)
    print(Processor.Summarize.output)
    single = Processor->Analyze("Choose between banana and apple.")
    piped = "Choose a fruit." >> Processor
    print(full)
    print(single)
    print(piped)
}
```

| Expression or field | Current behavior |
| --- | --- |
| `Processor(value)` | Runs the complete agent; without a task graph, tasks follow declaration order |
| `Processor->Analyze(value)` | Runs only that task using the explicit value; no whole-agent delay |
| `value >> Processor >> Reviewer` | Each agent receives the previous result |
| `input = __input__` | Uses the whole-agent argument |
| No task `input` field | Uses previous task output, or the agent argument for the first task |
| `input = flow.input` | Explicitly selects flow input instead of the call argument |
| `input = null` | Explicit null; does not trigger a fallback |
| `Analyze.output`, `Processor.Analyze`, `Processor.Analyze.output` | Reads a published task result |
| `input = Processor->Analyze` | Reads the available cached result without rerunning the task |
| `input = Processor->Analyze(value)` | Explicitly reruns the task |

Forward references and stale results from previous agent invocations are rejected during
an active run. Outside an input field, a no-argument arrow call reads cache when available
and otherwise may execute with default input. Use explicit arguments when execution is intended.
Sequential and task-graph execution share input resolution and result publication.
Use `prompt = [...]` in new examples. The interpreter also accepts `prompts` as an alias;
older examples using the plural are not invalid for that reason.

`output = json` is not a general output schema guarantee: sequential/task-graph execution
retains the legacy ignored metadata behavior, while direct task calls retain their format
handling. Do not describe this as enforced typed output or guaranteed valid model JSON.
Similarly, `validation`/`learning` declarations are not evidence of automatic hallucination
prevention, RAG injection or model training in the interpreter.

## Task graphs

A basic chain is supported; this provider-dependent example receives syntax validation in CI.
Tests with a recording provider separately verify that the second task receives the first result.
Do not infer complete conditional/retry policy support from parser acceptance of edge attributes.

<!-- axon-check: check -->
```axon
flow Research { input = "Explain Axon." debug = false }
agent Processor {
    provider ollama
    model llama3.2
    task Analyze { input = __input__ prompt = ["Analyze the input."] }
    task Summarize { input = Analyze.output prompt = ["Summarize the analysis."] }
    task_graph { Analyze -> Summarize Summarize -> done }
}
main { print(Processor(flow.input)) }
```

The interpreter also has top-level agent graphs; a simple success chain is covered by
`test_graph_execution_order` in [test_interpreter.cpp](../tests/test_interpreter.cpp).
The C++ graph engine and scheduler have their own tests; these do not establish distributed
execution, transport integration or concurrent execution of language `parallel` blocks.

## Timing and providers

`sleep(seconds)` blocks execution. Task/agent/flow `delay` fields use finite non-negative
seconds and apply after successful completion: task before continuing, whole agent before
returning, and selected flow after program execution. A task called directly does not incur
the whole-agent delay. No post-delay is forced after an error.

`timeout` is a total elapsed budget in seconds on flows, agents and tasks. Zero inherits the parent
limit; an inner timeout cannot extend it. Budgets include post-delays. An agent/task `TimeoutError`
can be caught outside that scope while the parent remains active. A flow timeout or host cancellation
ends the run. See [execution control](EXECUTION_CONTROL.md) for cooperative limits and the host API.

<!-- axon-check: run -->
```axon
flow Demo { timeout=2 debug=false }
agent Processor {
    task Slow { timeout=0.02 delay=1 }
    task Fast { input=__input__ }
}
main {
    caught=false
    try { Processor->Slow(1) } catch err {
        if err.find("TimeoutError:") != 0 { raise "wrong timeout error" }
        caught=true
    }
    if caught == false { raise "task timeout was ignored" }
    if Processor->Fast(42) != 42 { raise "recovery failed" }
}
```

`parallel` blocks execute independent assignments/expressions concurrently using lexical snapshots.
Only their top-level assignment results and completed task outputs publish at a successful join,
in source order. Shared memory is synchronized; side effects are not rolled back on failure.
Pipeline groups retain their last-declared-result convention. See [PARALLEL.md](PARALLEL.md).

<!-- axon-check: run -->
```axon
flow Demo { debug=false timeout=5 }
func double_value(n) { sleep(0.01) return n*2 }
main {
    parallel { left=double_value(10) right=double_value(20) }
    if left != 20 { raise "left result" }
    if right != 40 { raise "right result" }
}
```


The CLI registers the Ollama adapter when its local service is available. Additional provider
names, transport declarations and endpoint metadata do not establish working adapters.
Missing providers and adapter failures raise catchable `ProviderError` values; no input passthrough
or silent `null` is returned for a failed model request.
`compute`, GPU/context/batch options and fallback depend on the adapter and installed Ollama/model;
CI does not verify GPU placement, VRAM usage or every option. The executable does not bundle models.

## Compatibility and source of truth

This alpha is not a stable language specification. [FEATURES.md](../FEATURES.md) maps capabilities
to evidence; [ROADMAP.md](ROADMAP.md) records pending work. Historical RFC numbering and the
Tree-sitter grammar are design/editor inputs, not a substitute for interpreter execution tests.

## Contextual database declarations

`database` inside a flow is reserved for a validated alias-to-configuration registry. Each alias must
contain explicit driver/database fields; currently sqlite is the implemented driver. It is an ordinary
identifier in main/functions. Opening resources remains explicit through sql->connect; see
[INTEROP.md](INTEROP.md) for schema rules, dynamic validation and migration from the first flat shape.
Literal duplicate declarations/aliases/options are check-time errors; other configuration values are
validated when run evaluates the declaration. This is not global keyword reservation or implicit DB I/O.
