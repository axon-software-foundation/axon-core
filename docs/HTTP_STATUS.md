# HTTP and communications status

In `0.4.0-alpha.1`, there is no public implementation of `http->get`, `http->post` or `http->json`.
Calls produce an explicit error containing:

```text
HTTP module is not implemented in this Core snapshot
```

Tests assert this absence; they do not send requests or certify HTTP/HTTPS support.
The Ollama adapter and semantic-memory client contain specialized HTTP code, not a general language module.

## Future implementation

HTTP needs a specified contract for arguments, response values, status/headers/body, JSON, certificate
verification, deadlines, limits, redirects and errors. Tests should use a real local server to cover GET,
POST, JSON, error status, connection failure, incomplete responses and limits. TLS tests need controlled
certificates and active verification, not disabled checks.

gRPC, WebSockets and MCP belong to the communications roadmap. MCP needs its own protocol version,
role/capabilities, transport, connection lifecycle, authorization, cancellation and errors; it is not
an alias for HTTP or gRPC. These integrations remain pending and are not implemented by this documentation change.
