# Axon Language

**Cognitive Flow Oriented Programming Language for LLM Agent Orchestration**

Official repository: [axon-software-foundation/axon-core](https://github.com/axon-software-foundation/axon-core).

Current development version: **0.4.0-alpha.1** (not published yet).
The authoritative version is [VERSION](VERSION). For portable packaging and
external-folder testing, see [RELEASING.md](docs/RELEASING.md).
Experimental limits and pending work are tracked in [ROADMAP.md](docs/ROADMAP.md).

Windows x64: strict GCC 16.2 build and 22/22 CTest passed; the portable was
also confirmed on a second Windows machine. The alpha is **not published yet**.
Linux archives are native validation builds; Docker and macOS await CI results.

To help, start with [CONTRIBUTING.md](CONTRIBUTING.md),
[CI](docs/CI.md), [SECURITY.md](SECURITY.md) and
[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md). The publication guide is
[GITHUB_LAUNCH.md](docs/GITHUB_LAUNCH.md); changes are in [CHANGELOG.md](CHANGELOG.md).


## Table of Contents

- [Philosophy](#philosophy)
- [Requirements](#requirements)
- [Build](#build)
    - [Windows](#build--windows)
    - [Linux via Docker](#build--linux-docker-from-windows)
- [Project Structure](#project-structure)
- [Language Reference](#language-reference)
    - [Entry Point](#entry-point)
    - [Flow Definition](#flow-definition)
    - [Agent Definition](#agent-definition)
    - [Calling Agents](#calling-agents)
    - [task_graph](#task_graph-custom-chaining)
    - [Types](#types)
    - [F-Strings](#f-strings)
    - [Operators](#operators)
    - [Control Flow](#control-flow)
    - [Variables](#variables)
    - [Dict / List Assignment](#dict--list-assignment)
    - [String Methods](#string-methods)
    - [Number Methods](#number-methods)
    - [List Methods](#list-methods)
    - [Sleep / Timing](#sleep--timing)
- [File I/O](#file-io)
    - [CSV](#csv)
    - [JSON](#json)
    - [PDF](#pdf)
- [Memory](#memory)
    - [Key-Value Memory](#key-value-memory)
    - [Semantic Memory](#semantic-memory-hnsw--embeddings)
- [image-> Module](#image--module)
- [codec-> Module](#codec--module)
- [hash-> Module](#hash--module)
- [date-> Module](#date--module)
- [Shell Interop](#shell-interop)
- [Pipeline Operator](#pipeline-operator)
- [Error Messages](#error-messages)
- [Embedding Configuration](#embedding-configuration)
- [CLI Commands](#cli-commands)
- [Ollama Setup](#ollama-setup)
- [Compute Configuration](#compute-configuration)
- [Tests](#tests)
- [Roadmap](#roadmap)
- [License](#license)

----
## Philosophy
Axon is an open source language designed to orchestrate AI agents, process documents, manage memory, and chain LLM calls — without Python boilerplate.

```axon
flow Research {
    input = "Analyze this document"
    debug = false
}

agent Analyzer {
    provider    ollama
    model       llama3.2
    temperature = 0.3
    max_tokens  = 2048
    compute     = auto

    task Extract {
        input  = __input__
        prompt = ["Extract key insights from this text"]
    }
    task Summarize {
        input  = Analyzer->Extract
        prompt = ["Summarize in 3 bullet points"]
    }
}

main {
    doc    = file->read("./report.pdf")
    result = Analyzer(doc)
    print(result)
}
```

---

## Requirements

**Windows (primary):**
- MSYS2 UCRT64 GCC (validated toolchain) and Ninja for the portable build
- CMake 3.20+
- Ollama (optional, for LLM calls) — https://ollama.com

**Linux:**
- C++17 compiler, CMake 3.20+ and Make/Ninja; Docker is an alternative (see [DOCKER_BUILD.md](DOCKER_BUILD.md))

---

## Build — Windows

From the source root in Git Bash (adjust the MSYS2 prefix if necessary):

```bash
bash scripts/validate.sh Debug -G Ninja \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
./build-validation/axon.exe version
./build-validation/axon.exe run examples/validation/tasks.axon
```

For a Release ZIP, use [docs/RELEASING.md](docs/RELEASING.md). MSVC and macOS
packaging require separate validation. Legacy install scripts copy only the
main executable; use the full portable folder for this alpha.

## Build — Linux (Docker from Windows)

```bash
docker compose run --build --rm build
docker compose run --rm test
```

Build products stay in a Linux Docker volume. For the tested runtime image,
archive export and mount examples, see [DOCKER_BUILD.md](DOCKER_BUILD.md).
Docker execution is pending CI; this configuration does not certify macOS or Windows binaries.

---

## Project Structure

```
Axon-core/
├── cli/
│   └── main.cpp                    ← axon CLI entry point
├── runtime/
│   ├── include/
│   │   ├── ast.h                   ← AST node definitions
│   │   ├── lexer.h                 ← Token types + Lexer
│   │   ├── axon_parser.h           ← Parser interface
│   │   ├── interpreter.h           ← Interpreter + Env class
│   │   ├── memory_store.h          ← Key-value memory store
│   │   ├── semantic_memory.h       ← HNSW semantic memory
│   │   ├── document_io.h           ← CSV/JSON/DOCX/XLSX parsers
│   │   └── modules.h               ← image/codec/hash modules
│   └── src/
│       ├── lexer.cpp
│       ├── axon_parser.cpp
│       ├── interpreter.cpp
│       ├── memory_store.cpp
│       ├── semantic_memory.cpp
│       ├── document_io.cpp
│       ├── office_reader.cpp       ← DOCX/XLSX readers
│       ├── pdf_reader.cpp          ← PDF text + image extractor
│       └── modules.cpp             ← image/codec/hash implementations
├── adapters/
│   └── ollama/
│       └── ollama_adapter.hpp      ← Ollama REST adapter
├── tests/
│   ├── test_runtime.cpp            ← 31 runtime tests
│   └── test_interpreter.cpp        ← 78 interpreter assertions
├── third_party/
│   ├── hnswlib/                    ← HNSW vector index (header-only)
│   ├── miniz/                      ← ZIP reader (uses vendored zlib)
│   ├── stb/                        ← Image processing (header-only)
│   └── zlib/                       ← zlib sources (vendored)
├── examples/
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── FEATURES.md
└── README.md
```

---

## Language Reference

### Entry Point

```axon
main {
    # all logic goes here
}
```

One `main {}` per project. No other entry points.

---

### Flow Definition

```axon
flow MyFlow {
    version   = "1.0"
    input     = "your input data"
    telemetry = true
    debug     = false     # false = silent (production)
                          # true  = show connection/skip messages

    output {
        level   = info
        target  = stdout
        metrics = true
    }
}
```

Access flow input anywhere: `flow.input`

---

### Agent Definition

```axon
agent Processor {
    # Provider
    provider    ollama
    model       llama3.2
    transport   rest
    endpoint    "localhost:11434"
    temperature = 0.7

    # Tokens & context
    max_tokens  = 2048      # tokens to generate
    num_ctx     = 32768     # context window size

    # Compute
    num_gpu     = -1        # -1=all GPU, 0=CPU only, N=N layers on GPU
    num_thread  = 0         # CPU threads (0=auto)
    main_gpu    = 0         # which GPU (multi-GPU)
    num_batch   = 512
    f16_kv      = true      # float16 KV cache (less VRAM)
    low_vram    = false
    compute     = auto      # auto/gpu/cpu — auto enables gpu_fallback
    gpu_fallback= true      # retry on CPU if GPU crashes (HTTP 500)

    # Timing
    delay       = 0.5       # seconds to wait after agent finishes
    timeout     = 30        # metadata; enforcement pending

    # Memory
    memory { runtime = enabled  session = enabled }

    # Validation
    validation {
        confidence  > 0.7
        max_retries = 1
        on_fail     = skip
    }

    # Tasks
    task Analyze {
        input   = __input__     # or: flow.input, Agent->Task
        prompt  = ["Analyze this document carefully"]
        delay   = 0.2           # seconds after this task
        timeout = 15            # metadata; enforcement pending
    }

    task Summarize {
        input  = Processor->Analyze   # reads cached output
        prompt = ["Summarize in 3 paragraphs"]
    }
}
```

### Calling Agents

```axon
main {
    # Form 1: run all tasks in declaration order
    result = Processor(flow.input)

    # Form 2: run a specific task
    result = Processor->Analyze(flow.input)

    # Form 3: pipeline operator
    result = flow.input >> Processor >> Reviewer

    # Access task outputs after run
    print(Processor.Analyze)
    print(Processor.Summarize)
}
```

### task_graph (custom chaining)

```axon
task_graph {
    Analyze  -> Validate [on_success]
    Analyze  -> Retry    [on_fail]
    Validate -> done
}
```

Without `task_graph`, tasks run in declaration order automatically.

---

### Types

| Type | Example |
|------|---------|
| string | `"hello"`, `'world'` |
| number | `42`, `3.14`, `0.5` |
| bool | `true`, `false` |
| null | `null` |
| list | `[1, 2, 3]` |
| dict | `{"key": "value"}` |

```axon
typeof(val)   # → "string", "number", "bool", "null", "list", "dict"
```

### F-Strings

```axon
name = "Axon"
msg  = f"Hello {name}, score={0.9 * 100}%"
msg2 = f'Result: {2 ** 10}'
```

---

### Operators

```axon
# Arithmetic
x = 2 ** 10     # power → 1024
r = 10 % 3      # modulo → 1

# Logical
ok = a && b
ok = a || b

# Comparison
==  !=  <  >  <=  >=
```

### Control Flow

```axon
# if / else if / else
if score >= 90 { grade = "A" }
else if score >= 80 { grade = "B" }
else { grade = "F" }

# for loop
for item in list {
    print(item)
}

# while loop
while i < 10 { i = i + 1 }

# match (pattern matching)
match status {
    "ok"  -> { print("good") }
    "err" -> { print("bad") }
    _     -> { print("unknown") }
}

# try / catch
try {
    raise "Something failed"
} catch err {
    print("caught:", err)
}
```

### Variables

```axon
_ = "discard"          # underscore as variable
_result = 42           # underscore prefix

n = null               # assign null — no error
x = undefined_var      # [runtime error] undefined variable 'x'
```

### Dict / List Assignment

```axon
d = {}
d['key']   = 'value'           # add new key
d['user']['name'] = 'Alice'    # nested (auto-creates intermediate dicts)

lst = [1, 2, 3]
lst[0] = 99                    # index assignment
```

---

### String Methods

```axon
s = "Hello World 123"

# Basic
s.upper()         s.lower()         s.strip()
s.length()        s.contains("lo")  s.startswith("He")
s.endswith("23")  s.repeat(3)       s.tostr()

# Regex (ECMAScript syntax, use [0-9] style or \\d for backslash-d)
s.find("[0-9]+")            # first match → "123"
s.find_all("[A-Za-z]+")     # all matches → ["Hello", "World"]
s.replace("[0-9]+", "NUM")  # → "Hello World NUM"
s.split("[ ]+")             # → ["Hello", "World", "123"]
s.test("^Hello")            # → true
s.sub("[0-9]+", "X", 1)     # first N replacements
```

### Number Methods

```axon
n.abs()   n.floor()   n.ceil()   n.round()   n.tostr()
```

### List Methods

```axon
lst.append(val)   lst.pop()      lst.first()   lst.last()
lst.size()        lst.contains(v) lst.join(", ") lst.reverse()
```

---

### Sleep / Timing

```axon
sleep(1)       # 1 second
sleep(0.5)     # 500ms
sleep(0.1)     # 100ms
```

---

## File I/O

```axon
# Read — auto-detects format by extension
data = file->read("./sales.csv")       # → List of Dicts
cfg  = file->read("./config.json")     # → Dict or List
doc  = file->read("./report.docx")     # → plain text string
rows = file->read("./data.xlsx")       # → List of Dicts
text = file->read("./report.pdf")      # → plain text string
raw  = file->read("./data.csv", true)  # force raw text

# Write
file->write("./out.txt", content)      # text (UTF-8)
file->append("./out.txt", more)

# File operations
file->delete(path)
file->mkdir(path)
file->rmdir(path)
entries = file->ls(path)
ok = file->exists(path)
```

### CSV

```axon
rows = csv->parse(raw_text)
rows = csv->parse(raw_text, ";")      # custom delimiter
txt  = csv->stringify(rows)
csv->write("./out.csv", rows)
```

### JSON

```axon
obj = json->parse(raw_text)
txt = json->stringify(obj)            # compact
txt = json->stringify(obj, 2)         # pretty-print
json->write("./out.json", obj, 2)
```

### PDF

```axon
# Simple — text only
text = file->read("./report.pdf")

# Full — structured
pdf = pdf->parse("./report.pdf")

pdf["text"]                            # full text
pdf["pages"]                           # page count
pdf["pages_content"]                   # list of page dicts
pdf["images"]                          # list of image dicts

for pg in pdf["pages_content"] {
    print("page", pg["number"])
    print(pg["text"])
}

for img in pdf["images"] {
    print(img["format"])               # "jpeg", "png", "raw"
    print(img["page"])                 # page number
    print(img["width"], img["height"])
    print(img["data"])                 # base64 encoded
    image->save("./img.jpg", img["data"])
}
```

---

## Memory

### Key-Value Memory

```axon
# Scopes: runtime (default), session, shared, persistent
memory->store("key", "value")
memory->store("key", "value", "session")
memory->store("key", "value", "persistent")
memory->store("key", "value", "runtime", 30)   # TTL in seconds

val  = memory->get("key")
ok   = memory->has("key")
memory->remove("key")
memory->clear("runtime")    # or "all"
keys = memory->keys("session")
sz   = memory->size()

# Persist key-value to disk
memory->persist("./data/mem.bin")
memory->load("./data/mem.bin")
```

### Semantic Memory (HNSW + Embeddings)

```axon
embedding DocMemory {
    provider = "ollama"
    model    = "nomic-embed-text"
    endpoint = "http://localhost:11434"
    dim      = 768
}

memory->index("text to index", "optional metadata")
results = memory->search("query", 5)       # top_k = 5

for r in results {
    print(r["text"], r["score"], r["metadata"])
}

memory->sem_persist("./memory/docs.idx")   # single file
memory->sem_load("./memory/docs.idx")
print(memory->sem_size())
```

---

## image-> Module

```axon
# Save image from base64 (e.g., extracted from PDF)
image->save("./output.jpg", img["data"])
image->save("./output.png", img["data"], "png")   # force format

# Get image info
info = image->info("./photo.jpg")
info = image->info(img["data"])          # from base64
print(info["width"], info["height"], info["channels"])

# Resize → returns base64 of resized image
small_b64 = image->resize(img["data"], 512, 512)
small_b64 = image->resize("./photo.jpg", 512, 512)
image->save("./small.jpg", small_b64)

# Convert format → returns base64
png_b64 = image->convert("./photo.jpg", "png")
image->save("./photo.png", png_b64)
```

---

## codec-> Module

```axon
b64  = codec->b64_encode("plain text")
txt  = codec->b64_decode(b64)

hex  = codec->hex_encode("axon")
raw  = codec->hex_decode(hex)

url  = codec->url_encode("hello world & more")
orig = codec->url_decode(url)
```

---

## hash-> Module

```axon
h256 = hash->sha256("content")
hmd5 = hash->md5("content")
```

---

## date-> Module

```axon
now  = date->now()                         # "2026-07-05"
ts   = date->timestamp()                   # "2026-07-05T14:32:10"
unix = date->unix()                        # 1751723530

fmt  = date->format(now, "DD/MM/YYYY")    # "05/07/2026"
# Tokens: YYYY YY MM DD HH mm SS

parts = date->parse("2026-07-05")
# → {year, month, day, hour, minute, second}

tomorrow  = date->add(now, 1, "days")
last_week = date->sub(now, 7, "days")
# units: days, months, years, hours

diff = date->diff("2026-01-01", now, "days")

ok = date->before("2025-01-01", now)
ok = date->after(now, "2025-01-01")
```

---

## Shell Interop

```axon
shell { echo "hello world" }
out  = shell.stdout
code = shell.exitcode
```

---

## Pipeline Operator

```axon
result = flow.input >> AgentA >> AgentB >> AgentC

# Parallel groups
result = flow.input >> [AgentA, AgentB] >> Merger
```

---

## Error Messages

**Runtime error:**
```
[runtime error] undefined variable 'foo'
  --> examples/main.axon:14
   14 |     result = foo
```

**Syntax error:**
```
[syntax error] unterminated string
  --> examples/main.axon:6
    6 |     x = "hello
```

**LLM error:**
```
[llm error] HTTP 400: request exceeds context size
  Tip: reduce input or add 'num_ctx = N' to your agent

[axon] GPU crash detected — retrying on CPU
```

---

## Embedding Configuration

```axon
embedding DocMemory {
    provider = "ollama"
    model    = "nomic-embed-text"    # ollama pull nomic-embed-text
    endpoint = "http://localhost:11434"
    dim      = 768
}
```

---

## CLI Commands

```bash
axon run   file.axon     # run a program
axon check file.axon     # syntax check only
axon version             # show version
```

---

## Ollama Setup

```bash
# Install Ollama
# https://ollama.com

# Pull a model
ollama pull llama3.2
ollama pull nomic-embed-text    # for semantic memory

# Run
ollama serve
```

---

## Compute Configuration

```axon
agent Fast {
    provider = ollama
    model    = llama3.2

    compute     = auto    # auto-detect GPU, fallback to CPU
    gpu_fallback= true    # retry on CPU if GPU crashes
    max_tokens  = 2048
    num_ctx     = 8192
    f16_kv      = true
}

agent CPUOnly {
    provider   = ollama
    model      = llama3.2
    num_gpu    = 0
    num_thread = 8
    num_ctx    = 32768    # more context available with CPU+RAM
}
```

---

## Tests

```bash
# Run all tests
.\build\axon_test_runtime.exe
.\build\axon_test_interp.exe

# Validation baseline: 31 runtime + 78 interpreter cases; 22 CTest cases.
# See docs/VALIDATION.md and docs/BUILD_WARNINGS.md.
```

---

## Roadmap

See [docs/ROADMAP.md](docs/ROADMAP.md) for implemented, validated and pending
capabilities. HTTP/REST, MCP, effective timeouts, actual parallel execution and
additional platforms require further implementation or validation.

---

## License

MIT — see [LICENSE.txt](LICENSE.txt). Third-party components retain their own
licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
