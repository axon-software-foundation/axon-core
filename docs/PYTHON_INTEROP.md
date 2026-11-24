# External Python and shell interoperability

Axon executes the user's Python interpreter in a new operating-system process for each block. It
neither embeds CPython nor reimplements Python, installs packages, or shares Python objects between
blocks. Python 3.9+ is required to execute Python blocks. Ordinary native Axon and SQLite execution
do not require Python. Building the complete test suite does require an interpreter.

## Syntax and inputs

```axon
main {
    name="Darío"
    values=[10,20,30]
    python [
        person = name,
        data = values,
    ] {
        print("Hello", person)
        return {"total":sum(data),"count":len(data)}
    }
    response=__result__
    print(response["total"])
}
```

`python [python_name=axon_expression, ...] { ... }` evaluates inputs once, in source order, in the
current Axon scope before starting Python. Spaces, newlines and indentation in the header are
insignificant; commas separate bindings and a trailing comma is accepted. `python { ... }` and
`python [] { ... }` have no inputs. Only explicitly supplied values cross the boundary. A valid
Python identifier such as `model` can be used even when Axon uses the word in another context.
Duplicate names are syntax errors. Python keywords/invalid identifiers and reserved implementation
names beginning with `__axon_` are rejected before executing user Python code.

Python blocks are statements in global `main` or `func` bodies, including their control blocks.
They are rejected directly in flow, agent and task declarations and in an agent's own main block.
An agent/task may call an ordinary Axon function that performs Python work; the restriction concerns
where the block is written, not the function's caller. Existing `parallel` syntax accepts assignments
or expressions, so use a function call as each parallel branch; a bare Python block is not a new
parallel branch form.

## Conversion contract

| Axon | Python | Rule |
| --- | --- | --- |
| null | None | Direct conversion. |
| bool | bool | Boolean, not a number. |
| number | int/float | Axon numbers are IEEE-754 doubles; integer-valued inputs may become Python int. |
| string | str | Valid UTF-8, including Unicode and embedded NUL. |
| list | list | Recursively converted by value. |
| dictionary with string keys | dict | Recursively converted by value. |
| list on return | tuple | Python tuples become Axon lists, recursively. |

Only these built-in Python types are accepted. Container aliases are copied as values, not preserved
as shared object identities. Cyclic containers, non-string dictionary keys, sets, bytes, NumPy objects,
DataFrames, models, functions, generators and user class instances are rejected. Runtime function
and SQL connection dictionaries containing reserved handle markers cannot be passed as inputs.
No unsupported object is silently stringified.

Axon does not distinguish integer and floating-point number types. Integers must be within
`[-9007199254740991, 9007199254740991]`; non-finite numbers are rejected. Integer-valued Python floats
outside that range are rejected too. Other floats retain double precision. Axon cannot recover
precision already lost in an Axon expression before the boundary. Convert large identifiers to
strings explicitly when needed. NumPy/pandas conversion is also explicit: for example, `.tolist()`,
`int(value)`, `float(value)` or `df.to_dict(orient="records")`, with compatible nested values.

Mutations in Python do not change the original Axon values. Return the changed value to receive it.
For large/complex objects, export a file and use an Axon reader that supports that format. A Python
connection or trained estimator stays inside its block or is exported by user code.

## Return and reserved result

Use ordinary Python `return expression`. Axon wraps the body in a generated function to support it;
returning inside a nested Python function still returns only from that nested function. The return
value is converted and published atomically in the read-only Axon identifier `__result__`.

- `return value` preserves a compatible scalar/container type.
- `return first, second` produces the list accessed with `__result__[0]`, `__result__[1]`.
- `return [first, second]` produces a list; `return {"first":first,"second":second}` a dictionary.
- No return, an empty return, and `return None` produce null.
- `print()` never becomes a return value and cannot spoof the result protocol.

`__result__` starts null for each program/function/parallel execution context. Normal Axon
expressions do not overwrite it. Control blocks such as if/try share their enclosing context;
function calls and parallel branches have independent contexts. Their results are not implicitly
merged into the caller/parent: return them from an Axon function or assign a branch expression.
Copy `saved=__result__` to retain a response before another interop operation. The previous result
can also appear in a Python input expression; inputs are captured before the result is cleared.
Direct assignment, parameter binding and indexed/attribute assignment to `__result__` are syntax
errors. Copy the value to an ordinary variable before modifying it.

After a Python input-evaluation, launch, execution or return-conversion error, `__result__` is null.
Errors are catchable with Axon try/catch. Python exceptions include type, message and traceback;
uncatchable process termination reports its exit status. Parent deadlines and host cancellation
must still unwind expired/cancelled parent execution. Missing Python does not affect unrelated
native Axon execution. It does make a Python block fail explicitly.

## Large blocks, imports and processes

The body contains ordinary synchronous Python: imports, functions, classes, loops and exception
handling are supported. Common block indentation is removed using Python's textwrap.dedent and
relative indentation is retained. This also dedents multiline string contents: use explicit newline
escapes when literal leading whitespace must remain exact. Braces in ordinary quoted/triple-quoted
strings and comments do not close the Axon block. Advanced Python f-string grammar with nested
same-quote expressions is not separately parsed by Axon; move such source to a .py module if needed.

Imports are resolved with the containing .axon file's directory first on sys.path. For an imported
Axon function this is its defining file's directory, not its caller's. Python retains its ordinary
standard-library/site-package paths. The working directory remains where Axon was launched;
relative data-file paths therefore use that working directory. Python sees __file__ as the defining
Axon source path and __name__ as __axon_interop__. Global declarations/import-star have Python's
normal restrictions inside the generated function. Background processes are terminated when the
block finishes; this interface does not launch persistent services or interactive stdin sessions.

For a large ML project, keep implementation in user-owned modules:

```axon
main {
    python [seed=42] {
        from python_helpers.ml_training import train_model
        return train_model(seed)
    }
    print(__result__["accuracy"])
}
```

The executable example is examples/validation/python_ml.axon. It needs scikit-learn in the selected
interpreter and uses a bundled sample dataset, without network downloads. It is optional, separate
from the standard-library-only test suite. Every block starts a fresh process, so imports/models
are loaded again. Keep related expensive work in one block if reuse is important.

## Configuration

Set variables in the launching shell; no new flow/agent/task configuration is introduced.

| Variable | Default | Meaning |
| --- | --- | --- |
| AXON_PYTHON | python on Windows; python3 on Unix | One executable path/name, with no extra command arguments. Use a venv's executable to select its dependencies. |
| AXON_INTEROP_TIMEOUT | 0 / inherited parent only | Optional local deadline in seconds; never extends the parent deadline. |
| AXON_INTEROP_MAX_BYTES | 16777216 | Serialized request/response byte ceiling, including protocol metadata/code; maximum 268435456. |
| AXON_INTEROP_MAX_OUTPUT_BYTES | 4194304 per stream | stdout/stderr ceiling; maximum 268435456. |
| AXON_INTEROP_MAX_DEPTH | 64 | Recursive protocol/container depth ceiling; maximum 128. |

Paths with spaces are passed as process arguments, not interpolated into shell commands. Python
uses the selected environment's packages; Axon does not run pip automatically. To inspect the
selected Python, print sys.executable inside a block. Tests use CMake's Python3_EXECUTABLE via
AXON_PYTHON; pass -DPython3_EXECUTABLE=... when testing a particular interpreter.

The result protocol uses separate temporary files, never stdout sentinels. stdout/stderr are
captured separately and forwarded after the child exits, not streamed live. Files are stored in
unique temporary directories and removed after execution. Output limits are polled while the
child runs and checked again before reading; they are not hard operating-system disk quotas and
can be briefly exceeded between polls. They do not cap the Python process's own RAM consumption.

On POSIX the process runs in a process group; on Windows in a Job Object. Deadline/cancellation
terminates its child group/job and waits for the direct child. This is process-memory isolation,
not a security sandbox: code retains host permissions, filesystem access, network and ordinary
shared external resources. A deliberately detached process is outside the isolation guarantee.

## Shell and SQL results

`shell { ... }` publishes a dictionary with stdout, stderr and exit_code in __result__. Nonzero shell
status is returned as data; launch failures, limits and interrupted execution raise errors. Unix
uses /bin/sh; Windows uses COMSPEC/cmd.exe. The language does not translate shell dialects. The
existing shell.stdout and shell.exitcode aliases remain; shell->exec(command) retains its direct
stdout-string return without automatically printing stdout, while updating the structured __result__. Both paths share the cancellable
process backend. There is no implicit Axon variable injection into shell source.

The next Python or SQL block replaces the result in that same context. Existing SQL block output
remains typed rows in sql.result and __result__; SQL connections and its query API are unchanged.

## Migration and validation

Legacy implicit scalar injection, assigning Python's __result__ as a return mechanism,
python.__result__ aliases and stringified JSON results are replaced by explicit inputs, return and
typed Axon __result__. Historical broad showcase examples may still illustrate the old interface;
only updated acceptance examples are advertised for this contract.

Run examples/validation/python_interop.axon for seven positive acceptance groups, and
examples/validation/interop_parallel.axon for independent calls. examples/validation/python_errors/
contains nine intentionally failing syntax/read-only fixtures. CTest python.syntax_errors asserts
their expected exit codes and diagnostics. Its flow_timeout.axon fixture is a separate expected
runtime failure (syntax check succeeds), asserted by python.deadline. The positive example also
checks that a failed parallel Python call cancels its sleeping sibling without committing bindings. python.process exercises real child processes, types,
streams, exceptions, cancellation, overlap, limits, executable selection and shell status. No
pandas/scikit-learn or model service is needed for automated acceptance. Windows/macOS and exact
portable acceptance must run for the new patch before platform support is claimed.
