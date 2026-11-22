# Axon Language Reference
**Version:** 0.1.0-phase2a
**Paradigm:** Cognitive Flow Oriented
**File extension:** `.axon`

> Axon is a minimalist language for orchestrating LLM-powered agents.
> The language is small. The runtime is powerful.

---

## Table of Contents

1. [Philosophy](#1-philosophy)
2. [Building & Running](#2-building--running)
3. [Program Structure](#3-program-structure)
4. [Imports](#4-imports)
5. [Flows](#5-flows)
6. [Roles](#6-roles)
7. [Agents](#7-agents)
8. [Tasks](#8-tasks)
9. [Task Graph](#9-task-graph)
10. [I/O Channels](#10-io-channels)
11. [Memory](#11-memory)
12. [Validation & Hallucination Control](#12-validation--hallucination-control)
13. [Learning](#13-learning)
14. [Embeddings](#14-embeddings)
15. [Flow Graph](#15-flow-graph)
16. [Control Flow](#16-control-flow)
17. [Interoperability](#17-interoperability)
18. [Types & Values](#18-types--values)
19. [Expressions](#19-expressions)
20. [Errors](#20-errors)
21. [Built-in Variables](#21-built-in-variables)
22. [Complete Example](#22-complete-example)
23. [What Axon is NOT](#23-what-axon-is-not)
24. [Roadmap](#24-roadmap)

---

## 1. Philosophy

Axon has two layers:

```
Small core language (.axon)
+
Powerful cognitive runtime (C++/Cython)
```

The **language** declares agents, flows, graphs, memory, and validation.
The **runtime** handles parallelism, memory management, session persistence,
hallucination control, embeddings, and network communication.

If you need complex logic — math, data processing, ML — use an interop block.
Axon does not try to replace Python. It orchestrates it.

---

## 2. Building & Running

### Requirements

| Platform | Compiler           | CMake |
|----------|--------------------|-------|
| Linux    | GCC 11+ / Clang 13+ | 3.20+ |
| macOS    | Apple Clang 14+    | 3.20+ |
| Windows  | MSVC 2019+ / MinGW | 3.20+ |

### Linux & macOS

```bash
git clone https://github.com/your-org/axon
cd axon
./scripts/build.sh release
./build/axon version
```

### Windows

```powershell
git clone https://github.com/your-org/axon
cd axon
.\scripts\build.ps1 release
.\build\axon.exe version
```

### Generate installer

```powershell
# Windows — NSIS installer (.exe)
cd build && cpack -G NSIS

# Windows — WiX MSI (.msi)  requires WiX Toolset v3
cd build && cpack -G WIX

# Linux — Debian package
cd build && cpack -G DEB

# Linux — RPM package
cd build && cpack -G RPM
```

### Install to PATH

```bash
# Linux / macOS
./scripts/install.sh

# Windows (as Administrator)
.\scripts\install.ps1
```

After installation:

```bash
axon version
axon check  myfile.axon
axon run    myfile.axon
axon test
```

---

## 3. Program Structure

An `.axon` file is a flat sequence of top-level declarations.
Declare before using.

```axon
# 1. Imports
import session
import memory

# 2. Flow declaration
flow MyFlow { ... }

# 3. Roles (optional)
role Analyst { ... }

# 4. Embeddings (optional)
embedding DocStore { ... }

# 5. Agents
agent MyAgent { ... }

# 6. Execution graph
graph { ... }
```

Comments use `#` for single lines and `/* */` for blocks.

```axon
# single-line comment

/*
   block comment — can span
   multiple lines
*/

agent MyAgent {
    provider ollama   /* inline block comment */
    model    llama3
    main {
        x = 10  # also inline
    }
}
```

---

## 4. Imports

```axon
import session
import memory
import stdlib.io
```

Built-in modules — always available, no installation needed:

| Module       | Provides                                          |
|--------------|---------------------------------------------------|
| `session`    | `session.new()`, `session.current()`, `session.get(id)` |
| `memory`     | memory type declarations                          |
| `stdlib.io`  | file read/write                                   |
| `stdlib.tools` | built-in agent tools                            |
| `error`      | error types for `raise`                           |

---

## 5. Flows

A `flow` declares the execution context shared by all agents.

```axon
flow ResearchPipeline {
    version   = "1.0"
    session   = session.new()
    telemetry = true
    input     = "What are the AI trends in 2025?"
}
```

| Key         | Type    | Description                            |
|-------------|---------|----------------------------------------|
| `version`   | string  | flow version identifier                |
| `session`   | session | session used by all agents             |
| `telemetry` | bool    | enable runtime metrics collection      |
| `input`     | any     | initial input to the first agent       |

Access flow values from agents using `flow.key`:

```axon
agent MyAgent {
    session = flow.session
    main {
        data = flow.input
    }
}
```

---

## 6. Roles

Roles define the purpose and behaviour profile of an agent.

```axon
role Researcher {
    goal  = "gather and synthesize information"
    style = "thorough"
}

role Critic {
    goal  = "validate accuracy and flag problems"
    style = "skeptical"
}
```

Assign to an agent:

```axon
agent DataAgent {
    role Researcher
    ...
}
```

---

## 7. Agents

Agents are the primary unit of execution in Axon.

```axon
agent Analyst {
    provider    anthropic
    model       "claude-3-5-sonnet"

    role        Researcher
    session     = flow.session
    temperature = 0.8

    memory     { ... }
    validation { ... }
    learning   { ... }

    task Analyze { ... }

    task_graph { ... }

    main { ... }
}
```

### Provider and model

`provider` and `model` are always separate. The runtime abstracts the provider.

```axon
# Simple model names — no quotes needed
provider  ollama
model     llama3

# Compound model names with dashes — use quotes
provider  anthropic
model     "claude-3-5-sonnet"

provider  openai
model     "gpt-4o"

provider  google
model     "gemini-2.5-pro"

# Never combine them
# llm = "gemini"    ← wrong
```

### Supported providers

| Provider    | Status      |
|-------------|-------------|
| `ollama`    | ✅ Phase 1  |
| `openai`    | 📋 Phase 3  |
| `anthropic` | 📋 Phase 3  |
| `google`    | 📋 Phase 3  |

---

## 8. Tasks

Tasks declare what an agent can do. They do not execute on their own —
execution happens in `main` or through a `task_graph`.

```axon
task Analyze {
    input   = __input__
    output  = json
    prompts = [
        "You are a senior data analyst.",
        "Return JSON with keys: summary, confidence, recommendation."
    ]
}
```

| Key       | Description                                             |
|-----------|---------------------------------------------------------|
| `input`   | where input comes from (`__input__`, `flow.input`, var) |
| `output`  | expected output format (`json`, `text`, `markdown`, `dataframe`) |
| `prompts` | list of prompt strings sent to the model                |

Multi-line prompts:

```axon
task Summarize {
    prompts = [
        "You are a concise summarizer.",
        """
        Summarize the following in 3 bullet points.
        Be direct. Avoid filler phrases.
        """
    ]
}
```

---

## 9. Task Graph

Every agent can have its own internal task graph — a second level of
orchestration below the flow graph.

```
Flow graph  →  connects agents       (inter-agent)
Task graph  →  connects tasks        (intra-agent)
```

### Declaring a task_graph

```axon
agent DataProcessor {
    provider  ollama
    model     llama3

    task Ingest    { input = flow.input       output = json      }
    task Transform { input = Ingest.output    output = dataframe }
    task Validate  { input = Transform.output output = json      }

    task_graph {
        Ingest    -> Transform  [on_success]
        Ingest    -> Ingest     [on_fail, max_retries = 2]
        Transform -> Validate   [on_success]
        Transform -> Ingest     [condition = "confidence < 0.7"]
        Validate  -> done
    }

    main {
        run task_graph
    }
}
```

Edge attributes inside `task_graph` are identical to the top-level `graph` block:

| Attribute             | Description                                       |
|-----------------------|---------------------------------------------------|
| `on_success`          | follow this edge when task succeeds               |
| `on_fail`             | follow this edge when task fails                  |
| `max_retries = N`     | retry at most N times before giving up            |
| `condition = "expr"`  | follow edge only if expression is true            |

### run statement

```axon
main {
    run task_graph   # execute the full internal task graph
    run Ingest       # execute a single task by name
}
```

---

## 10. I/O Channels

Each task can declare where it receives input from and where it sends
output to. This connects tasks to external systems.

```axon
task Ingest {
    input  = flow.input
    output = json

    io {
        receive = grpc("data-service:50051/DataService/Stream")
        emit    = next_task
    }
}

task Transform {
    input  = Ingest.output
    output = dataframe

    io {
        receive = next_task
        emit    = socket("reviewer-service:9000")
    }
}

task Broadcast {
    input  = Transform.output
    output = json

    io {
        receive = next_task
        emit    = ultrasonic
    }
}
```

### Available transports

| Transport      | Syntax                                    | Description                       |
|----------------|-------------------------------------------|-----------------------------------|
| `grpc`         | `grpc("host:port/Service/Method")`        | gRPC stream (bidirectional)       |
| `socket`       | `socket("host:port")`                     | Raw TCP socket                    |
| `ultrasonic`   | `ultrasonic`                              | Physical mesh 18-22 kHz, ~10-30m  |
| `next_task`    | `next_task`                               | Pass to/from adjacent task in graph |

### I/O flow between levels

```
External gRPC
      ↓ receive
[Task Ingest] ──emit next_task──► [Task Transform] ──emit socket──► External
                                                            ↓ also emit
                                                       [Task Broadcast] ──► ultrasonic mesh
```

The runtime handles connection lifecycle automatically: opens on task start,
streams tokens as they are generated, closes on task completion.

### Ultrasonic channel

The ultrasonic transport uses the device microphone and speaker at
18–22 kHz (inaudible to humans). Its bandwidth is intentionally limited:

- Use it for **signaling, discovery, and small packets** (session IDs,
  state signals, triggers).
- For large data, the runtime upgrades to gRPC or socket automatically
  after ultrasonic handshake.

---

## 11. Memory

Each agent declares which memory layers it uses. The runtime manages
storage, retrieval, and lifecycle automatically.

```axon
memory {
    runtime    = enabled    # variables, fastest, volatile
    session    = enabled    # conversation history
    semantic   = enabled    # embeddings + similarity search
    persistent = enabled    # survives process restart
    shared     = enabled    # visible to other agents in the flow
    temporary  = enabled    # cleared when flow ends
}
```

### Memory types

| Type         | Backed by       | Lifetime     | Use for                  |
|--------------|-----------------|--------------|--------------------------|
| `runtime`    | C++ heap        | Process      | Variables, fast lookups  |
| `session`    | Ring buffer     | Session      | Conversation history     |
| `semantic`   | HNSW index      | Configurable | Embedding search         |
| `persistent` | mmap / disk     | Permanent    | Long-term knowledge      |
| `shared`     | Shared memory   | Flow         | Cross-agent state        |
| `temporary`  | Heap            | Flow         | Scratch space            |

### Named memory references

```axon
embedding DocStore {
    model   = "nomic-embed-text"
    dim     = 768
    index   = hnsw
    persist = "./memory/docs.idx"
}

agent Researcher {
    memory {
        semantic = DocStore
    }
}
```

---

## 12. Validation & Hallucination Control

The runtime validates every agent output at the `CHECKPOINT` state
before passing it to the next agent. No manual code needed.

```axon
validation {
    confidence  > 0.85
    format      = json
    max_retries = 3
    on_fail     = retry
}
```

### on_fail options

| Value   | Behaviour                                            |
|---------|------------------------------------------------------|
| `retry` | Re-run the agent with the same input                 |
| `flag`  | Mark output as `FLAGGED`, continue flow              |
| `skip`  | Pass `null` to the next agent silently               |
| `raise` | Halt the flow and raise an error                     |

### Agent states

```
IDLE → PENDING → READY → RUNNING → CHECKPOINT → EMITTING → SUCCESS
                                       ↓                       ↑
                                     FAILED ──── retry ────────┘
                                     FLAGGED  (validator rejected)
```

---

## 13. Learning

The `learning` block tells the runtime how to improve the agent from
its own execution history.

```axon
learning {
    method    = imprinting
    capture   = on_success
    threshold = 0.9
    compress  = semantic
    apply     = next_execution
}
```

### Learning methods

| Method        | What it does                                                  |
|---------------|---------------------------------------------------------------|
| `rag`         | Searches vector store before each prompt and injects context  |
| `lora`        | Fine-tunes model weights via LoRA (requires llama.cpp)        |
| `imprinting`  | Captures successful outputs as compressed context for reuse   |

---

## 14. Embeddings

```axon
embedding DocumentMemory {
    model      = "nomic-embed-text"
    dim        = 768
    index      = hnsw
    persist    = "./memory/docs.idx"

    quantization {
        method = pq
        bits   = 8
    }
}
```

### Index types

| Index  | Speed  | Accuracy | When to use                     |
|--------|--------|----------|---------------------------------|
| `hnsw` | Fast   | ~95%     | Default, large collections      |
| `flat` | Slow   | 100%     | Small collections, exact search |
| `ivf`  | Medium | ~90%     | Very large collections          |

### Querying in main

```axon
agent Researcher {
    memory {
        semantic = DocumentMemory
    }

    main {
        results = memory.semantic.search(__input__, top_k = 5)
        return results
    }
}
```

---

## 15. Flow Graph

The top-level `graph` block connects agents and defines execution flow.

```axon
graph {
    AgentA -> AgentB [on_success]
    AgentA -> AgentA [on_fail, max_retries = 2]
    AgentB -> AgentC [condition = "confidence > 0.9"]
    AgentB -> AgentA [condition = "confidence <= 0.9"]
    AgentC -> done
}
```

### Edge attributes

| Attribute            | Description                                      |
|----------------------|--------------------------------------------------|
| `on_success`         | follow edge when agent succeeds                  |
| `on_fail`            | follow edge when agent fails                     |
| `max_retries = N`    | retry at most N times                            |
| `condition = "expr"` | follow edge only if expression evaluates to true |

### Special targets

| Target | Meaning                          |
|--------|----------------------------------|
| `done` | flow completed successfully      |
| `fail` | flow ended with error            |

### Cycles are allowed

```axon
graph {
    Validator -> Researcher [on_fail, max_retries = 2]
    Researcher -> Validator [on_success]
}
```

### Two levels of graphs

```
Flow graph  (graph { })          Task graph  (task_graph { })
─────────────────────────        ────────────────────────────
AgentA -> AgentB -> AgentC       Ingest -> Transform -> Validate
    inter-agent routing              intra-agent task routing
```

---

## 16. Control Flow

### if / else

```axon
if condition {
    # then
} else {
    # else
}
```

### for

```axon
for item in collection {
    # body
}
```

### while

```axon
while condition {
    # body
}
```

### match

Designed for agent state transitions and value branching.

```axon
match agent.state {
    READY   -> execute()
    FAILED  -> retry()
    FLAGGED -> notify()
    _       -> null
}

match response {
    "approved" -> proceed()
    "rejected" -> raise "ValidationFailed"
    _          -> return null
}
```

### try / catch

```axon
try {
    result = risky_operation()
} catch err {
    return "error: " + err
}
```

### raise / return

```axon
raise "ErrorType: description"
raise error.ModelError

return value
return null
return
```

---

## 17. Interoperability

Embed foreign code directly. Executed by the native runtime bridges.

### Python

```axon
python {
    import pandas as pd
    df   = pd.read_csv("sales.csv")
    mean = df["revenue"].mean()
    __cog_result__ = {"mean_revenue": mean, "rows": len(df)}
}

result = __cog_result__
```

Rules:
- Variables defined in a `python {}` block are injected back into Axon scope.
- Use `__cog_result__` to explicitly return a value.
- Axon variables are accessible inside the block by their name.
- The namespace persists across multiple `python {}` blocks in the same agent.

### SQL

```axon
sql {
    SELECT product, SUM(revenue) as total
    FROM   sales
    WHERE  active = 1
    GROUP  BY product
    ORDER  BY total DESC
}
```

Configure the SQL backend in the agent:

```axon
storage {
    sql_backend = "postgres"
    sql_dsn     = "postgresql://user:pass@localhost:5432/mydb"
}
```

Supported backends: `sqlite` (default), `postgres`, `duckdb`, `mysql`.

### Shell

```axon
shell {
    mkdir -p ./outputs
    echo "Run complete: $(date)" >> ./outputs/log.txt
}
```

### C++

```axon
cpp {
    #include <cmath>
    double result = std::sqrt(2.0);
    axon_set_result(result);
}
```

---

## 18. Types & Values

### Primitive types

```axon
name    = "Axon"
version = 1.0
active  = true
empty   = null
```

### Collections

```axon
tags   = ["research", "analytics", "llm"]
config = {"temperature": 0.7, "max_tokens": 2048}
```

### Null-safe access

```axon
session = sessions[0]?       # null if index doesn't exist
value   = obj?               # null if obj is null
name    = user?.profile?.name?
```

---

## 19. Expressions

### Arithmetic

```axon
total = price * quantity
avg   = (a + b + c) / 3
mod   = value % 10
```

### Comparison

```axon
if score >= 0.8 { ... }
if status == "approved" { ... }
if count != 0 { ... }
```

### Logic

```axon
if active and score > 0.5 { ... }
if failed or flagged { ... }
if not valid { ... }
```

### Method calls

```axon
empty  = model.isempty()
len    = items.length()
upper  = text.upper()
sess   = session.current()
```

### Attribute access

```axon
name  = agent.name
turn  = session.history[0]
conf  = result["confidence"]
```

---

## 20. Errors

### Raising errors

```axon
raise "InputError: query cannot be empty"
raise error.ModelError
raise error.ValidationFailed
```

### Built-in error types

| Error                  | When to use                              |
|------------------------|------------------------------------------|
| `error.ModelError`     | LLM not configured or unavailable        |
| `error.LaunchError`    | Agent cannot start                       |
| `error.SessionError`   | Session operation failed                 |
| `error.MemoryError`    | Memory read/write failed                 |
| `error.ToolError`      | Tool call failed                         |
| `error.FlowError`      | Flow graph error                         |

### Catching errors

```axon
try {
    result = agent.run(input)
} catch err {
    if err == "ModelError" {
        raise error.LaunchError
    }
    return null
}
```

---

## 21. Built-in Variables

These variables are automatically injected by the runtime:

| Variable         | Available in  | Description                              |
|------------------|---------------|------------------------------------------|
| `__input__`      | `main` block  | Output from previous agent in flow       |
| `__cog_result__` | After `python {}` | Last value set in a Python block     |
| `flow.input`     | Any agent     | Initial flow input                       |
| `flow.session`   | Any agent     | Shared flow session                      |
| `agent.state`    | `main` block  | Current state of this agent              |
| `agent.retry_count` | `main` block | How many retries so far               |

---

## 22. Complete Example

```axon
import session
import memory

flow AnalysisPipeline {
    version   = "1.0"
    session   = session.new()
    telemetry = true
    input     = "Analyze Q4 sales data"
}

role Researcher {
    goal  = "gather and structure information accurately"
    style = "thorough"
}

agent DataProcessor {
    provider  ollama
    model     llama3
    role      Researcher
    session   = flow.session

    memory {
        runtime  = enabled
        session  = enabled
        semantic = enabled
    }

    validation {
        confidence  > 0.80
        format      = json
        max_retries = 2
        on_fail     = retry
    }

    learning {
        method    = imprinting
        capture   = on_success
        threshold = 0.9
        compress  = semantic
    }

    task Ingest {
        input  = flow.input
        output = json
        io {
            receive = grpc("data-service:50051/DataService/Stream")
            emit    = next_task
        }
    }

    task Transform {
        input  = Ingest.output
        output = dataframe
        io {
            receive = next_task
            emit    = next_task
        }
    }

    task Validate {
        input  = Transform.output
        output = json
        io {
            receive = next_task
            emit    = socket("reviewer:9000")
        }
    }

    task_graph {
        Ingest    -> Transform [on_success]
        Ingest    -> Ingest    [on_fail, max_retries = 2]
        Transform -> Validate  [on_success]
        Validate  -> done
    }

    main {
        if flow.input.isempty() {
            raise "InputError: flow input is required"
        }
        run task_graph
    }
}

agent Reviewer {
    provider  anthropic
    model     "claude-3-5-sonnet"
    session   = flow.session

    memory {
        runtime = enabled
        session = enabled
    }

    validation {
        confidence  > 0.90
        max_retries = 1
        on_fail     = flag
    }

    task Review {
        input  = __input__
        output = json
        io {
            receive = socket("reviewer:9000")
            emit    = next_task
        }
    }

    task_graph {
        Review -> done [on_success]
        Review -> Review [on_fail, max_retries = 1]
    }

    main {
        run task_graph
    }
}

graph {
    DataProcessor -> Reviewer     [on_success]
    DataProcessor -> DataProcessor [on_fail, max_retries = 1]
    Reviewer      -> done          [on_success]
    Reviewer      -> DataProcessor [condition = "confidence < 0.85"]
}
```

---

## 23. What Axon is NOT

| Axon does NOT have        | Use instead                         |
|---------------------------|-------------------------------------|
| Complex arithmetic        | `python { }` block                  |
| String manipulation       | `python { }` block                  |
| File I/O                  | `shell { }` or `python { }` block   |
| Package management        | Python packages via `python { import ... }` |
| Class definitions         | Axon has agents, not classes         |
| Async/await syntax        | Parallelism is automatic via runtime scheduler |
| Type annotations          | Axon is dynamically typed            |
| General-purpose data structures | `python { }` or native C++ extensions |

---

## 24. Roadmap

| Phase | Feature                                                         | Status      |
|-------|-----------------------------------------------------------------|-------------|
| 1     | Grammar, AST, Graph Engine, Actor FSM, Scheduler, Runtime Memory | ✅ Done     |
| 2A    | C++ Lexer, Recursive Descent Parser, `axon check` / `axon run` | ✅ Done     |
| 2B    | AST interpreter, Ollama provider, real agent execution          | 🚧 Next     |
| 3     | Semantic Memory (HNSW), Persistent Memory, Embeddings (llama.cpp) | 📋 Planned |
| 4     | gRPC, WebSockets, PostgreSQL, MySQL, MongoDB native drivers      | 📋 Planned |
| 5     | RAG pipeline, LoRA fine-tuning, Cognitive Imprinting             | 📋 Planned |
| 6     | Ultrasonic mesh, Gossip protocol, Federated Imprinting           | 📋 Planned |
| 7     | Full telemetry, OpenTelemetry export, cost estimation            | 📋 Planned |
