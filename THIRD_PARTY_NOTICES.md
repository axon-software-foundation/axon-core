# Third-party notices

Axon's own code uses the MIT license in LICENSE. This does not replace
the licenses of embedded components or toolchain runtime code.

| Component | Source | License / distributed text |
| --- | --- | --- |
| libcurl, bundled Windows HTTP build | https://curl.se / https://github.com/curl/curl | curl license, licenses/curl.txt; pinned 8.23.0 archive below |
| hnswlib headers | https://github.com/nmslib/hnswlib | Apache 2.0, licenses/hnswlib-Apache-2.0.txt |
| zlib 1.3.2 sources | https://zlib.net | zlib notice, licenses/zlib.txt |
| stb_image, stb_image_write, stb_image_resize2 | https://github.com/nothings/stb | Embedded dual license preserved in licenses/stb_*.txt (MIT option available) |
| MinGW GCC runtime, when linked | https://gcc.gnu.org | GPLv3 with GCC Runtime Library Exception 3.1; both texts in licenses/ |
| MinGW-w64 CRT, Windows package | https://www.mingw-w64.org | Notices copied from the actual toolchain into licenses/toolchain/crt/ |
| winpthreads, Windows package | https://www.mingw-w64.org | Notices copied from the actual toolchain into licenses/toolchain/winpthreads/ |

The file third_party/miniz/miniz.h is an Axon-maintained minimal ZIP reader
using the vendored zlib API. A development transcript supplied by the maintainer
records failed attempts to integrate upstream Miniz, followed by replacement
with this small compatibility implementation developed with Claude assistance.
Its mz_zip_* function names follow the API shape used by the Office reader;
the file is not an unmodified upstream Miniz distribution. See
the ZIP reader provenance record below for the evidence and limits.
The current path/name is retained for compatibility; it is not an upstream
version or authorship claim. zlib retains its separate license and notices.

The portable builder copies the original notices embedded in the stb and zlib
headers and adds the missing Apache 2.0 license text for hnswlib. It does not
replace vendored source or claim an upstream commit that was not recorded in
the supplied snapshot. A public tag must preserve applicable source notices.

Windows packaging is initially restricted to MinGW GCC with static runtime
linking and rejects unexpected DLL dependencies. The GCC exception text
describes its conditions for independent modules and eligible compilation.
Customized toolchains and compiler plugins require a separate review of the
actual components used. Ollama, model weights and Tesseract are external tools
and are not redistributed in this archive.

Reference license locations for MSYS2 UCRT64:

- https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-winpthreads
- https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-crt
- https://github.com/gcc-mirror/gcc/blob/master/COPYING.RUNTIME
- https://github.com/nmslib/hnswlib/blob/master/LICENSE

## Axon ZIP reader provenance

Recorded on 22 November 2026 from a development transcript supplied by the
project maintainer. The transcript is development evidence, not a record of
an independently audited upstream release.

- The initial integration attempted to download miniz.h and miniz.c from
  https://github.com/richgel999/miniz. It failed on a missing miniz_export.h;
  subsequent header/release-download attempts also failed.
- The recorded replacement command explicitly says "Use zlib + our own
  minimal ZIP reader instead of miniz" and writes a small header implementing
  only the mz_zip_* API subset needed for Office document extraction.
- A second recorded rewrite introduces mz_u16/mz_u32, mz_r16/mz_r32,
  mz_zip_entry, central-directory scanning and stored/Deflate extraction.
  These structures and code paths correspond to the implementation currently
  maintained in Axon. Later changes include using the vendored zlib header
  and copying/terminating the filename buffer explicitly.
- The implementation was developed with Claude assistance during Axon
  development. API compatibility names are not evidence that this file is an
  unmodified upstream Miniz release. No upstream release number or commit is
  assigned to it on the basis of the failed downloads.

This record clarifies the observed development history. It does not establish
that every line is independently original or constitute a complete source
provenance/security audit. Preserve any additional attribution evidence if
recovered. The separate upstream zlib notices remain applicable to zlib.

The historical transcript includes successful checks using synthetic DOCX and
XLSX files. Its 62 passing tests were the general runtime/interpreter suites,
not 62 Office-format tests. Malformed/truncated ZIP handling, resource limits,
XML/Office variants and cross-platform document fixtures need broader coverage.

For v0.4.0-alpha.1, this clarification may accompany the existing tagged
artifacts as release metadata or a supplemental notice. It does not change
that tag, its source archive, packaged notices or already-built checksums.
Future builds should carry this updated notice together with the source.

## Public HTTP transport

Windows builds fetch curl 8.23.0 from the official curl/curl GitHub release and verify
SHA256 `8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5`.
The minimal static build uses Windows Schannel; optional third-party compression/IDN/SSH/HTTP2
dependencies are disabled. The unmodified release COPYING is preserved as licenses/curl.txt.
Source URL: https://github.com/curl/curl/releases/download/curl-8_23_0/curl-8.23.0.tar.xz

Unix builds link the system libcurl and its system TLS/runtime dependencies instead. Their
licenses remain applicable; this package does not bundle those shared libraries. OpenSSL is
also a build/test dependency for the controlled TLS fixture; that fixture executable and test
private key are not part of portable runtime payloads. The optional bundled-curl Unix build uses
system OpenSSL. Review exact dependencies and notices when changing this configuration.
