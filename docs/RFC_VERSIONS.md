> Historical design record. Phase numbers, example APIs and proposed versions below are not the current release contract. See [ROADMAP.md](ROADMAP.md) and the current language reference for implementation status.

# Axon RFC — Version History

## v0.1.0 — Phase 1 · Runtime Core
```
runtime/
  interfaces/interfaces.hpp   ← abstract interfaces for ALL modules
  include/ast.h               ← C structs for every AST node
  include/platform.h          ← cross-platform layer (Win/Mac/Linux)
  src/graph_engine.cpp        ← BFS, DFS, topological sort, cycle detection
  src/actor.cpp               ← agent FSM (IDLE→RUNNING→SUCCESS/FAILED)
  src/scheduler.cpp           ← C++ thread pool, no GIL, priority queue
  src/runtime_memory.cpp      ← RuntimeMemory + SessionMemory (ring buffer)
```
**Deliverable:** `axon_test_runtime` — 30/30 tests passing

---

## v0.1.1 — Phase 2A · Lexer + Parser
```
runtime/
  include/lexer.h             ← all token types (TK enum)
  src/lexer.cpp               ← hand-written single-pass lexer
  include/axon_parser.h       ← recursive descent parser interface
  src/axon_parser.cpp         ← parser + arena allocator implementation
cli/
  main.cpp                    ← axon check / axon run (parse stage)
grammar/
  grammar.js                  ← Tree-sitter grammar (reference + editor tooling)
```
**New language features:** `task_graph { }`, `io { receive/emit }`, `run`
**Deliverable:** `axon check file.axon` — real parse with error reporting

---

## v0.1.2 — Phase 2B · Interpreter + Ollama  ← NEXT
```
runtime/
  include/interpreter.h       ← AST tree-walker interface
  src/interpreter.cpp         ← executes agents, flows, statements, expressions
adapters/
  ollama/ollama_adapter.hpp   ← HTTP client → Ollama REST API
```
**Deliverable:** `axon run file.axon` — real execution against local Ollama

---

## v0.2.0 — Phase 3 · Memory + Embeddings  (planned)
```
runtime/src/semantic_memory.cpp    ← HNSW + llama.cpp embeddings
runtime/src/persistent_memory.cpp  ← mmap to disk
runtime/src/shared_memory.cpp      ← cross-agent shared state
native/vector_ops/                 ← SIMD AVX2/512 vector operations
```

---

## v0.3.0 — Phase 4 · Data + Network  (planned)
```
adapters/openai/   adapters/anthropic/   adapters/google/
native/data/       ← libpq, MySQL C API, mongoc, DuckDB, Arrow
native/network/    ← gRPC, WebSockets, raw sockets, streams
```

---

## v0.4.0 — Phase 5 · Learning  (planned)
```
runtime/src/rag_pipeline.cpp
runtime/src/lora_finetuner.cpp
runtime/src/cognitive_imprinting.cpp
```

---

## v0.5.0 — Phase 6 · Mesh  (planned)
```
native/physical_mesh/   ← PortAudio/RtAudio, FSK 18-22kHz, ultrasonic protocol
runtime/src/gossip.cpp  ← epidemic knowledge propagation
runtime/src/consensus.cpp
```
