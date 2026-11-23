# Public HTTP/HTTPS module — working-tree implementation

This implementation is new development after the published 0.4.0-alpha.1 artifacts; rebuilding
this source enables it. Existing downloaded binaries do not gain the module automatically.
The public module is separate from Ollama's specialized client. HTTP requests can overlap in
parallel branches and inherit execution deadlines/cancellation.

## Functions and response

| Call | Result |
| --- | --- |
| `http->get(url, options={})` | Response dictionary |
| `http->post(url, body, options={})` | Response dictionary; string bodies are sent unchanged, other values serialize as JSON |
| `http->json(url, options={})` | GET and strictly decode a successful response's JSON body |
| `http->json(response)` | Strictly decode `response["body"]`, including an HTTP error response |

A response has `status` (number), `ok` (true for 2xx), `body` (string, preserving binary bytes),
`url` (final requested URL), and `headers` (dictionary of lowercase names to lists of string values).
Lists preserve repeated headers without merging Set-Cookie values incorrectly. Names are case-insensitive
on input and lowercase on output. No cookies, credentials or connection pool persist between calls.

GET/POST return 4xx/5xx responses normally so callers can inspect their status/body. Transport,
certificate, framing/truncation, limit and option errors raise catchable `HttpError`. `json(url)`
also raises on non-2xx status. `json(response)` is useful to inspect a JSON error body explicitly.
Invalid JSON is an error, distinct from a valid JSON `null`. HTTP's private JSON reader validates
syntax, UTF-8, Unicode surrogate pairs, finite double numbers and a nesting limit of 128. It does
not change the older `json->parse` module. Duplicate JSON object keys retain their last value.

## Options

| Key | Default | Contract |
| --- | --- | --- |
| `timeout` | 30 seconds | Positive finite seconds, maximum 3600; a child cannot extend its parent budget |
| `connect_timeout` | 10 seconds | Positive finite seconds, capped by request timeout |
| `headers` | `{}` | String names/values; Host, Content-Length and Transfer-Encoding are managed by the client |
| `max_body_bytes` | 8 MiB | Positive integer, maximum 64 MiB; cumulative across redirects |
| `max_header_bytes` | 64 KiB | Positive integer, maximum 1 MiB; cumulative across redirects/interim responses |
| `follow_redirects` | `false` | Explicit opt-in; only same-origin targets are followed |
| `max_redirects` | 5 | Integer from 1 through 10 |
| `ca_file` | system trust | Optional PEM trust bundle; verification remains enabled |
| `proxy` | libcurl environment configuration | Explicit empty string disables environment proxies for this request |

Unknown options, invalid arity/types and newline/NUL header injection are rejected. Request bodies
are limited to 8 MiB. JSON POST sets application/json unless the caller supplies Content-Type.
Only HTTP/HTTPS URLs are accepted; embedded URL credentials are rejected. HTTPS requires TLS 1.2+
and verifies both certificate trust and hostname. There is no insecure/skip-verification option.

Redirects retain a single total budget. POST becomes GET after 301/302/303; 307/308 retain method/body.
The conservative same-origin rule compares scheme, host and effective port, preventing silent header/body
forwarding to a different origin and HTTPS downgrade. Cross-origin redirect following, streaming,
multipart uploads, cookie sessions and general authentication helpers remain separate work.

The multi-handle transport polls at short intervals, checking host/group cancellation and absolute
budgets. Local HTTP timeouts can be caught while a parent budget remains active. Parent expiration
or cancellation unwinds the containing run/group. DNS interruption depends on libcurl's resolver
build; bundled curl enables its threaded resolver. These are cooperative limits, not a sandbox.

## Example

```axon
flow Request { debug=false timeout=30 }
main {
    response=http->get("https://api.github.com/repos/axon-software-foundation/axon-core",
                       {"timeout":10,"max_body_bytes":1048576})
    print("HTTP status: ",response["status"])
    if response["ok"] == false { raise "request was not successful" }
    repository=http->json(response)
    print(repository["full_name"])
}
```

The example requires Internet access; the reproducible fixture tests use loopback only. See
`examples/validation/http.axon` for manual acceptance. The API's status/rate limits belong to that
external service; an HTTP status alone is not a language/runtime failure.

## Build, packages and evidence

Unix builds require libcurl development headers. Controlled TLS fixture tests additionally require
OpenSSL development headers/libraries. Windows builds fetch curl 8.23.0 from the official GitHub
release with a pinned SHA256 and build a minimal static Schannel-backed client. Optional compression,
IDN, SSH and HTTP/2 dependencies are disabled in that build. The Windows runtime uses the system
certificate store; it does not need curl.exe or OpenSSL DLLs to make HTTP requests. The TLS test
executable uses OpenSSL but is not shipped in the portable payload. See THIRD_PARTY_NOTICES.md.

`http.transport` makes real local GET/POST/JSON, non-2xx, redirect, truncation, resource-limit,
timeout/cancellation and HTTPS fixture requests. Trusted test CA, unknown CA and hostname mismatch
cases leave verification active. Tests generate an ephemeral CA, server certificate/key and signed CRL
with a loopback distribution endpoint for Schannel revocation checks. No test root is installed into
the OS trust store; temporary PEM files are removed when the fixture finishes. A revoked fixture is
checked with OpenSSL CRL verification on all platforms; an additional assertion checks rejection
by the actual HTTP client when using Schannel.
Static files under tests/fixtures/http are legacy fixtures and are no longer required by this test.
Interpreter tests prove actual HTTP request overlap
with a rendezvous server and verify catchable local timeouts. Model services and Python are not required.

Official references: [multi polling](https://curl.se/libcurl/c/curl_multi_poll.html),
[certificate verification](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYPEER.html),
[hostname verification](https://curl.se/libcurl/c/CURLOPT_SSL_VERIFYHOST.html),
[CA bundles](https://curl.se/libcurl/c/CURLOPT_CAINFO.html).

MCP, gRPC and WebSockets retain their separate roadmap contracts. This HTTP implementation does not
implement them, browser rendering or the planned HTML scraper.
