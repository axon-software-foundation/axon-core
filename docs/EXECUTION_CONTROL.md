# Execution budgets, cancellation and provider errors

## Timeout contract

`timeout` accepts finite, non-negative seconds on a flow, agent or task. Zero means no additional
local limit; it does not disable a parent's deadline. Negative, non-numeric or excessively large
values are configuration errors. An inner scope can shorten, never extend, its parent's budget.
Deadlines use a monotonic clock and are total elapsed budgets, not per-read socket timeouts.

- The selected flow's budget starts after declaration/import collection and includes its main or
  graph execution and flow post-delay. Multi-flow ownership is otherwise unchanged.
- An agent's budget starts at invocation and includes its tasks and whole-agent post-delay.
- A direct task call inherits the agent timeout but does not add the whole-agent post-delay.
- A task's budget includes input resolution, prompt evaluation, provider completion and task
  post-delay. Its timeout expression is evaluated before entering that budget.
- A completed task is published after its post-delay succeeds. Failed tasks publish no new result;
  previous completed task results and external side effects are not rolled back.

An agent/task timeout raises `TimeoutError` outside that scope and can be caught in a still-active
outer `try/catch`. Expiration of the flow budget stops the entire run; catching it inside the expired
flow cannot renew its budget. An uncaught error produces CLI exit status 1.

## Cancellation and adapter contract

Embedders can call `Interpreter::request_cancel()` from another thread. Cancellation is cooperative:
the interpreter checks statements, expressions and loop iterations, and interrupts sleep/delays.
Call `reset_cancel()` while idle before reusing a cancelled interpreter. Other interpreter operations
remain single-threaded at the host API. Internal `parallel` workers use separate interpreter
state and linked group cancellation; see [PARALLEL.md](PARALLEL.md).
Cancellation produces `CancelledError` and unwinds the run; language catches cannot suppress it.
No new language keyword, CLI cancellation command or Ctrl+C handler is introduced here.

`LLMRequest::control` carries an optional shared execution control. Providers must check it during
blocking work. Ollama uses nonblocking connect/send/receive with short polling intervals, closing
the client socket on timeout/cancellation. An explicit enclosing deadline is used when present;
otherwise the request has a total 30-second transport budget. Slow arriving bytes do not reset it.
Closing the socket does not guarantee the remote model server has stopped computation.

These are cooperative limits, not process isolation or hard real-time guarantees. OS hostname
resolution is synchronous. File/document/OCR/semantic-memory native calls and shell/Python subprocesses
are not preempted internally in this patch; execution checks resume at the next interpreter boundary.
Third-party adapters that ignore `control` can only be checked after returning. Do not use this
mechanism as a sandbox for untrusted code or claim it bounds every external operation.

## Provider failures

Model calls with no registered adapter raise `ProviderError`; they no longer return the input as
if generation succeeded. Adapter connection/HTTP failures raise a catchable `ProviderError` instead
of silently returning `null`. Successful explicit null task values remain valid when no model call
is required. `__last_llm_error__` stores a provider failure and is cleared before a registered model
request. Timeout/cancellation stay distinct from provider failures.

Task chains stop on an unhandled failure. Existing graph failure routes can handle raised errors;
an unhandled graph raise no longer becomes a successful CLI result. General graph-policy completion
is still separate roadmap work. The Ollama health probe now uses GET `/api/tags` and requires HTTP 200.

## Verification

CTest includes `provider.transport`: real loopback requests covering health, chat payload, HTTP
failure, incomplete bodies, invalid chat responses, deadlines and cancellation. The fixture server
does not generate model output and is used only in tests. Interpreter regressions cover inherited
budgets, delays, recovery, host cancellation and provider failures.

With a real Ollama service and `llama3.2` installed, run:

```bash
./build-validation/axon run examples/validation/ollama_errors.axon
```

Use `axon.exe` on Windows. This performs real generation, checks an intentional missing-model HTTP
failure, then generates again. It is manual integration acceptance, separate from service-free CI.

## External interop processes

Python and shell blocks inherit ExecutionControl and can shorten it with AXON_INTEROP_TIMEOUT.
Cancellation/deadline termination applies to their child process group/job and joins the direct child.
This does not make unrelated native document/OCR operations preemptible. See
[PYTHON_INTEROP.md](PYTHON_INTEROP.md) for output limits and process boundaries.
