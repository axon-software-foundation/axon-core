# Parallel execution contract

## Branches and results

`parallel { left = work(a) right = work(b) }` executes independent assignments or expressions
on the native scheduler. Wrap loops, declarations or control-flow statements in a function/agent
and call it from a branch; unsupported top-level statements are rejected before any branch starts.
An empty block returns null. A single branch follows the same isolation rules as multiple branches.

Each branch receives a deep snapshot of the lexical environment at block entry, including captured
closure environments, lists and dictionaries. A branch cannot read a sibling's new assignment.
Only top-level assignment results and completed task outputs are published back after all branches
succeed. Other variable mutations inside a branch stay local. Returning closures is supported;
their captured snapshot remains alive after the worker exits.

Publication follows source order, independent of completion order. Duplicate assignment targets
and task-cache keys therefore keep the last declared branch's value. Two calls to the same agent
have independent task frames; their assigned return values remain separate. Axon `print` output
and interpreter diagnostics are buffered per branch and replayed in source order after joining,
including when a branch fails. File logs are synchronized but follow actual execution order.

`input >> [AgentA, AgentB]` runs both branches with the same input. For compatibility, the expression
returns the last declared branch's result, not a new list type. Cached task outputs are available
after the join. Use named assignments in a parallel block when both return values are needed.

## Scheduling, failure and cancellation

Each block uses at most eight workers, bounded by branch count and detected CPU concurrency
(at least two worker slots when multiple branches exist). Jobs beyond capacity queue. Nested blocks
use their own bounded pools to avoid waiting for work trapped in a parent's exhausted pool;
this is a per-block limit, not a global process thread budget.

On the first observed branch failure, siblings receive a cooperative cancellation token. Every
started/queued job is joined before returning or throwing. No assignment or task-cache update is
committed when a branch fails. The original failure is propagated; simultaneous failures have no
specified ordering. Parent deadlines and host cancellation still stop the entire group.
Catchable language/provider errors can be handled in an active outer `try/catch`. Use that caught
error for diagnostics; a worker's branch-local `__last_llm_error__` is not merged into the parent.

Explicit memory, file/network writes and external processes are side effects, not a transaction;
they are not rolled back on failure. Non-preemptible native/interop operations can delay the join.
See [EXECUTION_CONTROL.md](EXECUTION_CONTROL.md) for deadline and cancellation limits.

## Shared services and providers

The existing key-value scopes remain explicitly shared through the interpreter's memory service;
they do not acquire new per-agent/per-branch ownership in this change. Native module operations,
including memory, semantic memory and file/document/OCR, are serialized across sibling interpreters
for safety. External Python and shell use independent processes and do not hold that native lock. Compound read-modify-write sequences are not atomic transactions.
Ollama task requests, ordinary expression evaluation and direct `sleep()` calls can overlap.

OCR and external interop use unique temporary directories with cleanup. Python inputs are explicit,
returns are typed, and every block starts a new process. Each parallel branch/function owns a
separate __result__ context, which is not merged implicitly into its caller. Use ordinary returned
function values in parallel assignments. Child processes support inherited deadlines/cancellation;
other non-preemptible native services can still delay joins. See [PYTHON_INTEROP.md](PYTHON_INTEROP.md).

Ollama opts into concurrent requests. Existing/custom provider adapters are serialized unless they
override `supports_concurrent_requests()` to return true; that opt-in promises thread-safe `complete`
calls and deadline/cancellation handling through `LLMRequest::control`. A model server may still queue
generation internally according to its own scheduling and hardware. Runtime request overlap is not
a guarantee of GPU concurrency or reduced model latency.

Host calls to `run()`, registration and configuration remain single-threaded; only `request_cancel()`
is a supported concurrent host operation. Internal workers use separate interpreter state.
`spawn` retains its prior synchronous behavior and is not upgraded by this change.

## Verification

Interpreter tests use a synchronized rendezvous provider: two requests must be active together
before either can finish. This proves overlap rather than inferring it from a wall-clock speedup.
Other cases cover closures, container/variable isolation, nested blocks, memory, queues, task frames,
source-order publication/output, provider serialization, failure, deadlines and host cancellation.
The test provider is only a fixture; it does not generate model output.

Run the service-free example:

```bash
./build-validation/axon run examples/validation/parallel.axon
```

For real generation, with Ollama and `llama3.2` installed:

```bash
./build-validation/axon run examples/validation/ollama_parallel.axon
```

Use `.exe` on Windows. The latter validates two real results and task-cache publication;
server-side generation overlap depends on the server, not just the language runtime.

Python interop acceptance (requires a Python executable on PATH):

```bash
./build-validation/axon run examples/validation/interop_parallel.axon
```
