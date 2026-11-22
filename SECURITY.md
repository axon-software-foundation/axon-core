# Security policy

Axon is alpha software. Only the latest development revision is actively maintained; there is
no stable security-support line or guaranteed response time yet.

## Reporting vulnerabilities

Use **Security → Advisories → Report a vulnerability** on the official repository when private
vulnerability reporting is enabled. Do not include exploits, credentials or sensitive data in
public issues or pull requests.

If no private reporting button/contact is available, publicly request only a private security
contact without technical details. Wait for maintainers to establish the channel before sending
the report. Maintainers must verify that channel before launching the release; its availability
is not established by this document.

Include version/commit, OS, architecture, impact, reproduction steps and a minimal synthetic example.
Maintainers will coordinate assessment, mitigation and disclosure with the reporter.

## Execution boundary

Treat `.axon` programs as code running with the user's permissions. Core can access files,
execute shell commands and contact local services. There is no sandbox for safely running
untrusted programs. Do not mount credentials or sensitive files into test containers.
The public playground is pending and needs isolation and resource limits before accepting code.
Docker deployment alone is not a guarantee of isolation against hostile scripts.

Alpha packages are not code-signed. SHA256 checks byte integrity, not publisher identity.
Windows application-control policy blocked a locally built executable during earlier testing;
rebuilding is not a guaranteed remedy. Do not disable organizational security policy to run Axon.
Signing and the distribution policy remain release work.
