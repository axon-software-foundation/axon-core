# Interoperability and native SQL

The interoperability goal is to make Python, SQL and host shells usable from ordinary Axon programs,
not only inside agents/tasks. Implementation and acceptance proceed in independent blocks.
This change implements native SQLite connections. DuckDB is next, then PostgreSQL; they are not
supported drivers yet. Strengthening Python/shell data exchange and subprocess management remains
an independent block; this SQL change does not certify those integrations as complete.

## Connection contract

`sql->connect(config)` returns a runtime-scoped connection handle. Configuration is a dictionary,
so it can be declared in a flow, constructed in a function, or created in main. Opening a connection
is explicit; a flow configuration does not open a database automatically. Keep live resource creation
inside executable code so it runs under the flow's execution budget.

```axon
flow Data {
    debug=false
    database={"driver":"sqlite","database":":memory:","name":"primary"}
}
func open_database(config) { return sql->connect(config) }
main {
    db=open_database(Data.database)
    rows=sql->query(db,"SELECT ? AS answer",[42])
    print(rows[0]["answer"])
    sql->close(db)
}
```

See `examples/validation/sqlite.axon` for the automatically executed example. This document's code
is illustrative; the equivalent MODULES.md fence is also executed by docs.examples.

| Configuration key | Default | Contract |
| --- | --- | --- |
| `driver` | `"sqlite"` | Only sqlite is implemented; unknown drivers raise SqlError |
| `database` | `":memory:"` | Database file path, relative to the process working directory, or a private in-memory database |
| `name` | empty | Optional unique name while open, for `sql->connection(name)` |
| `readonly` | `false` | Open the file read-only |
| `create` | `true` | Allow creation when writable; ignored for read-only opens |
| `foreign_keys` | `true` | Enable SQLite foreign-key enforcement |
| `busy_timeout` | 5 seconds | Positive finite per-lock retry budget, at most 3600; request/parent deadlines still apply |

Unknown keys/types are errors. NUL in paths/names is rejected. Empty paths and SQLite `file:` URI
mode are rejected. This first contract uses explicit named fields, not an engine-specific connection
string parser. Future drivers will extend configuration deliberately rather than silently accept
credentials/options that are not used. It is not an SQLAlchemy URL API.

A handle can be passed as a function/task/agent argument or looked up by name from the same interpreter
family. Parallel branch snapshots share its underlying connection registry. Handles are process-local
resources, not serializable connections or a security boundary; copying the visible dictionary is not
an independent connection. Another SqlRuntime cannot use the handle. Closing removes its name, and a
later connection may reuse that name. At most 1024 open connections are retained by a runtime registry;
closed handles do not consume registry entries.

## SQL operations

| Call | Result / behavior |
| --- | --- |
| `sql->connect(config)` | Open and return a connection handle |
| `sql->connection(name)` | Look up an open named connection |
| `sql->is_open(db)` | Boolean for a handle from this runtime |
| `sql->close(db)` | Close, rolling back an unfinished transaction; true once, false if already closed |
| `sql->query(db, statement, params=[], options={})` | List of row dictionaries |
| `sql->execute(db, statement, params=[], options={})` | Dictionary with rows, columns, changes, last_insert_id and in_transaction |
| `sql->script(db, text, options={})` | Execute multiple statements in order; list of their execute-style results |
| `sql->begin(db)`, `sql->commit(db)`, `sql->rollback(db)` | Explicit transaction control; return true on success |
| `sql->use(db)` | Select the connection for sql blocks in the current lexical environment |
| `storage->query(db, statement, params=[], options={})` | Alias for sql->query; the old query-without-connection placeholder is removed |

Both query and execute accept one statement, and reject a second before executing the first.
They may execute statements with side effects; query merely returns the rows directly. Use execute
when you need metadata. `changes` is the total-changes delta for this statement, including trigger or
foreign-key effects. `last_insert_id` is the connection's last insert rowid, not proof that the current
statement inserted a row. Duplicate column names are rejected; assign aliases explicitly.

Parameters are bound through SQLite's C API, not interpolated into code. A list binds positional
parameters. A dictionary binds named parameters, using their exact names (including `:`, `@`, `$`)
or the unprefixed name. Missing/extra parameters fail. Lists/dictionaries cannot be bound as cell
values: serialize them deliberately if the schema stores JSON. `script` and raw blocks do not accept
parameter interpolation; use query/execute with bound parameters for external data.

## Values and limits

SQL NULL becomes Axon null. TEXT becomes string, BLOB becomes bytes (including empty BLOBs), finite
REAL becomes number. INTEGER values in +/-9007199254740991 become numbers; larger signed 64-bit
integers become decimal strings to preserve precision in Axon's double-based number representation.
Axon boolean parameters bind as 0/1; SQLite does not preserve a separate boolean result type.
An empty row list is a successful empty result; a failure raises SqlError, never a fake empty list.
Non-finite inputs/results raise errors.

Query/script options are:

- `timeout`: positive finite seconds, default 30, maximum 3600; cannot extend a parent deadline.
- `max_rows`: positive integer, default 10000, maximum 1000000.
- `max_bytes`: positive integer, default 8 MiB, maximum 64 MiB; counts materialized cell payloads
  and column/key bytes, not every allocator/SQLite working-memory byte.

Script limits are cumulative across all its result sets. Exceeding a limit raises an error rather
than returning a silently truncated list. Statements are limited to 1 MiB, SQLite individual values
to 16 MiB and bound parameter indices to 10000. Results are materialized; streaming cursors and bulk
binding are later additions. Large datasets should be filtered/aggregated in SQL before returning them.

## Transactions, parallel and cancellation

Calls on one connection are serialized with a per-connection lock. Independent connections do not
share Axon's global native-module lock, although SQLite/file locks can still serialize database work.
A transaction is owned by the interpreter branch that starts it. A sibling branch cannot use that
connection until the owner commits/rolls back; this prevents accidental transaction interleaving.
Create independent connections when branches need independent transactions. Separate `:memory:` opens
are separate databases. SQLite retains its normal file-locking/single-writer constraints.

A branch's unfinished transactions roll back when that branch interpreter is destroyed. Explicitly
close connections when finished; remaining handles close when the owning interpreter family is destroyed.
Use try/catch with rollback for transactional work. Scripts do not introduce an implicit transaction:
earlier autocommitted statements can remain applied if a later statement fails. Group atomic work
with begin/commit/rollback. SQL failures, limits and cancellation do not promise rollback of unrelated
external side effects. On some SQLite errors an explicit transaction is automatically rolled back;
check execute metadata or SQLite state through SQL when recovery requires distinguishing that case.

SQLite progress/busy callbacks check inherited cancellation/deadlines without throwing across C callbacks.
Waiting for a connection lock is interruptible. A local request timeout is catchable while its parent
budget remains active. Native filesystem operations may still block outside callback opportunities;
these are cooperative budgets, not hard real-time guarantees. The runtime owns busy/progress handlers;
use the connection's busy_timeout configuration rather than expecting PRAGMA busy_timeout to persist
between runtime calls. Host filesystem permissions apply; SQL and Python/shell are not a sandbox.

## Raw blocks

After `sql->use(db)`, `sql { ... }` runs the text through script on that connection. The last statement's
rows are published as typed `sql.result`, `sql.__result__` and `__result__`; `sql.results` contains all
statement metadata. Selection is lexical, so functions/branches can choose their own connection.
Braces in SQL strings, quoted identifiers and comments do not close the Axon block.
There is no implicit database creation and no Axon variable string substitution into SQL.

## Build and evidence

The build fetches official SQLite 3.54.1 amalgamation sources with pinned SHA256, then links SQLite
statically into CLI/interpreter tests. The first configure needs Internet unless
`FETCHCONTENT_SOURCE_DIR_AXON_SQLITE` points to extracted verified sources. No Python, sqlite3 CLI,
SQLite DLL or server is needed at runtime. See cmake/Sqlite.cmake and THIRD_PARTY_NOTICES.md.

SQLite uses its native threadsafe build with math functions; runtime loadable extensions are omitted.
This does not promise availability of every optional SQLite extension or another engine's SQL dialect.

`sql.native` covers parameter binding/injection, types, large integers, transactions, concurrency,
limits, cancellation/recovery, file persistence, read-only access, foreign keys and file-lock deadlines.
Interpreter cases cover main/function/flow/agent composition, raw SQL, named lookup, parallel reads,
transaction isolation/cleanup, catchable errors and storage aliasing. `cli.sqlite` runs the example
from an unrelated working directory. Native Windows/macOS/package acceptance is required for this patch.

## Remaining Python/shell work

Current Python blocks inject only selected scalar values, return legacy string results and suppress
stderr; a process/serialization failure can therefore look like a missing result. Native subprocess
work is still serialized and not preempted. Shell command behavior is platform-specific. Correct typed
exchange, explicit result presence, stderr/exit reporting, controlled process lifetimes and native
integration tests remain required before declaring these integrations complete. The C++ keyword is
not an implemented C++ interop backend. See the broader ROADMAP.md.
