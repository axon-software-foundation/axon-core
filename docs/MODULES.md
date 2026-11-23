# Native modules — 0.4.0-alpha.1

Native calls use `module->function(...)` without imports. The dispatcher is in
[runtime/src/interpreter.cpp](../runtime/src/interpreter.cpp); image, codec and hash operations
are delegated to [modules.cpp](../runtime/src/modules.cpp). Implementation and validation are
separate: the examples below assert selected behavior, not complete conformance of each API.

## Strings, numbers and lists

`find`/`index` is a **literal** substring search returning a zero-based index or `-1`.
Use `match` for the first regex match string (or null), `find_all` for all matches,
`test`/`matches` for regex search, `replace` for replacement, `split` for splitting and
`sub(pattern, replacement, count)` for a limited number of replacements. Regex syntax is
ECMAScript; `[0-9]` avoids string-escape ambiguity. Invalid regexes are not handled uniformly:
`match` returns null, `test` returns false and `replace`/`split` may fall back to literal operations.

<!-- axon-check: run -->
```axon
main {
    s = "Hello 123"
    if s.find("123") != 6 { raise "literal find" }
    if s.find("[0-9]+") != -1 { raise "find is not regex" }
    if s.match("[0-9]+") != "123" { raise "regex match" }
    if s.find_all("[0-9]+").size() != 1 { raise "regex list" }
    if s.test("^Hello") != true { raise "regex search" }
    if s.replace("[0-9]+", "X") != "Hello X" { raise "replace" }
    if s.split("[ ]+")[1] != "123" { raise "split" }
    if "a1b2".sub("[0-9]", "X", 1) != "aXb2" { raise "sub" }
    if " axon ".strip().upper() != "AXON" { raise "string methods" }
    n = -2.6
    if n.abs().floor() != 2 { raise "number methods" }
    if n.ceil().tostr() != "-2" { raise "number conversion" }
    items = [1, 2]
    items.append(3)
    if items.pop() != 3 { raise "list pop" }
    if items.first() != 1 || items.last() != 2 { raise "list ends" }
}
```

Additional implemented methods include string `contains`, `startswith`, `endswith`, `lower`,
`lstrip`, `rstrip`, `length`, `repeat`, `toint`, `tofloat`; number `round`; list `size`,
`contains`, `join`, `reverse`, `isempty`. String length/indexing uses bytes, not Unicode graphemes.
Do not assume a string `.tostr()` conversion: the current numeric `.tostr()` implementation
is distinct from string methods.

## JSON, CSV and files

<!-- axon-check: run -->
```axon
main {
    data = json->parse('{"answer":42}')
    if data["answer"] != 42 { raise "JSON parse" }
    if json->parse(json->stringify(data))["answer"] != 42 { raise "JSON roundtrip" }
    rows = csv->parse("name,value\naxon,42\n")
    if rows.size() != 1 || rows[0]["name"] != "axon" { raise "CSV parse" }
    if csv->parse(csv->stringify(rows))[0]["name"] != "axon" { raise "CSV roundtrip" }
    if file->write("doc-example.txt", "axon") != true { raise "file write" }
    file->append("doc-example.txt", " core")
    if file->read("doc-example.txt", true) != "axon core" { raise "file read" }
    if file->exists("doc-example.txt") != true { raise "file exists" }
    file->delete("doc-example.txt")
}
```

| API | Behavior / boundary |
| --- | --- |
| `file->read(path, raw=false)` | Auto-detects CSV/TSV, JSON, PDF, DOCX, XLSX; otherwise returns text. `true` forces raw text. Missing files return null with a warning |
| `file->write(path, text)`, `file->append(path, text)` | Writes to the filesystem with the process user's permissions |
| `file->exists(path)`, `file->delete(path)`, `file->mkdir(path)`, `file->rmdir(path)`, `file->ls(path)` | Native filesystem operations; not a sandbox |
| `json->parse(text)`, `json->stringify(value, indent=0)`, `json->write(path, value, indent=2)` | Native JSON conversion; exhaustive malformed-input conformance is not established |
| `csv->parse(text, delimiter=",", header=true)`, `csv->stringify(rows, delimiter=",")`, `csv->write(path, rows)` | Native CSV conversion; use a non-empty delimiter |

File paths are relative to the process working directory; this differs from import resolution.
Document readers are native code in `document_io.cpp`, `office_reader.cpp`, `pdf_reader.cpp`. They are not a MuPDF integration and are not a promise to handle arbitrary
PDF/Office files. `pdf->text(path)` and `pdf->parse(path, mode="t")` exist; mode `"i"` selects image
extraction. Document/image extraction needs a broader fixture corpus and cross-platform integration tests.

## Hashes and codecs

<!-- axon-check: run -->
```axon
main {
    if hash->sha256("abc") != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" { raise "SHA256" }
    if hash->md5("abc") != "900150983cd24fb0d6963f7d28e17f72" { raise "MD5" }
    if codec->b64_decode(codec->b64_encode("Axon")) != "Axon" { raise "Base64" }
    if codec->hex_decode(codec->hex_encode("Axon")) != "Axon" { raise "hex" }
    if codec->url_decode(codec->url_encode("a b&c")) != "a b&c" { raise "URL codec" }
}
```

Only SHA-256 and MD5 are implemented. SHA-1 and SHA-512 raise explicit unsupported errors.
MD5 is provided for compatibility, not as a recommended integrity/security primitive.

## Key-value and semantic memory

Values in key-value memory are converted to strings; this is not typed object storage.
Supported scopes are `runtime` (default), `session`, `shared`, `persistent`.
Persistent scope does not automatically write to disk: use `persist`/`load` explicitly.
Reading a missing key or a stored empty string currently yields null; `has` distinguishes presence.

<!-- axon-check: run -->
```axon
main {
    memory->store("answer", 42)
    if memory->get("answer") != "42" { raise "memory converts to string" }
    if memory->has("answer") != true { raise "memory has" }
    memory->remove("answer")
    if memory->has("answer") != false { raise "memory remove" }
}
```

Implemented key-value calls: `store(key, value, scope="runtime", ttl=0)`, `get(key, scope)`,
`has(key, scope)`, `remove(key, scope)`, `clear(scope)` (also `"all"`), `keys(scope)`, `size(scope)`,
`persist(path)` and `load(path)`. TTL is in seconds. Persistence and expiry need broader integration
coverage than the small example above.

Semantic memory uses Ollama embeddings and an HNSW L2 index. Implemented calls include
`embed(text)`, `index(text, metadata)`, `search(query, top_k)`, `sem_size()`, `sem_persist(path)` and
`sem_load(path)`. It is not an automatic RAG pipeline, and named memory policies do not imply
shared-memory IPC or automatic conversation persistence.

This configuration receives syntax checking only; install the embedding model separately.
A real acceptance test must verify retrieved content and persistence with the running service.

<!-- axon-check: check -->
```axon
embedding DocMemory {
    provider = "ollama"
    model = "nomic-embed-text"
    endpoint = "http://localhost:11434"
    dim = 768
}
main {
    memory->index("Axon orchestrates agents.", "overview")
    print(memory->search("What does Axon do?", 3))
}
```

## Dates

<!-- axon-check: run -->
```axon
main {
    if date->format("2026-07-05", "DD/MM/YYYY") != "05/07/2026" { raise "date format" }
    if date->before("2026-01-01", "2026-07-05") != true { raise "date order" }
    if date->after("2026-07-05", "2026-01-01") != true { raise "date order" }
}
```

`now()` and `timestamp()` use the process's local timezone; `unix()` returns epoch seconds.
`parse(text)`, `add(text, count, unit)`, `sub(text, count, unit)` and `diff(a, b, unit)` are
implemented. This is not a timezone-aware calendar library; DST, invalid dates and all calendar
boundaries are not certified by the example. Inspect the implementation before relying on those edge cases.

## External tools, images and interoperability

| Area | Implemented entry points | Validation boundary |
| --- | --- | --- |
| Images (stb) | `image->info(source)`, `save(path, base64, format)`, `resize(source, width, height)`, `convert(source, format)` | Image inputs may be paths/base64 depending on operation. Codec/format/error coverage remains incomplete |
| OCR (Tesseract) | `ocr->installed()`, `languages()`, `read(path, language="eng")` | Tesseract and language data are external. Local Linux success/failure checks passed; no complete CI OCR integration matrix |
| Shell | `shell { ... }`, `shell->exec(command)`, `shell.stdout`, `shell.exitcode` | Executes commands with host permissions; shell syntax and exit handling are platform-dependent |
| Python | `python { ... }` | External interpreter integration exists; not required or fully validated by the baseline. Not a native Python package manager |
| Storage | `storage->query(...)` | Stub returning an empty list; no working database driver |
| Whisper | `whisper->transcribe(...)` | Stub returning an empty string; no working transcription integration |
| HTTP/HTTPS | `http->get/post/json` | Public module in the working tree; verified TLS, structured responses and limits; see HTTP_STATUS.md |
| MCP, gRPC, WebSockets, mesh | Design/interface work | No supported public module contract in this alpha |

OCR uses unique temporary directories. Native calls are serialized across sibling interpreters;
this does not make arbitrary external processes preemptible.
Failures are returned as `[ocr] ...` strings (or null for missing arguments), not structured exceptions.
General TLS/HTTPS support cannot be inferred from the specialized Ollama HTTP clients.
See [HTTP_STATUS.md](HTTP_STATUS.md), [SECURITY.md](../SECURITY.md) and [ROADMAP.md](ROADMAP.md).

## Public HTTP/HTTPS

`http->get(url, options)` and `http->post(url, body, options)` return status, ok, body, final URL
and lowercase header lists. `http->json(url, options)` performs GET and strictly decodes a successful
response; `http->json(response)` decodes an existing response body, including non-2xx responses.
See [HTTP_STATUS.md](HTTP_STATUS.md) for argument types, defaults, TLS, redirect and size limits.

<!-- axon-check: run -->
```axon
main {
    response={"status":200,"body":"{\"value\":42}"}
    data=http->json(response)
    if data["value"] != 42 { raise "HTTP JSON decoding" }
    caught=false
    try { http->json({"body":"not JSON"}) } catch err {
        if err.find("HttpError:") != 0 { raise "unexpected HTTP error" }
        caught=true
    }
    if caught == false { raise "invalid HTTP JSON was accepted" }
}
```
