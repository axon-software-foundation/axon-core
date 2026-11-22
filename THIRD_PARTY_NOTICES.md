# Third-party notices

Axon's own code uses the MIT license in LICENSE. This does not replace
the licenses of embedded components or toolchain runtime code.

| Component | Source | License / distributed text |
| --- | --- | --- |
| hnswlib headers | https://github.com/nmslib/hnswlib | Apache 2.0, licenses/hnswlib-Apache-2.0.txt |
| zlib 1.3.2 sources | https://zlib.net | zlib notice, licenses/zlib.txt |
| stb_image, stb_image_write, stb_image_resize2 | https://github.com/nothings/stb | Embedded dual license preserved in licenses/stb_*.txt (MIT option available) |
| MinGW GCC runtime, when linked | https://gcc.gnu.org | GPLv3 with GCC Runtime Library Exception 3.1; both texts in licenses/ |
| MinGW-w64 CRT, Windows package | https://www.mingw-w64.org | Notices copied from the actual toolchain into licenses/toolchain/crt/ |
| winpthreads, Windows package | https://www.mingw-w64.org | Notices copied from the actual toolchain into licenses/toolchain/winpthreads/ |

The file third_party/miniz/miniz.h in this snapshot is a small ZIP reader
implemented over the vendored zlib API. It has no separate upstream miniz
copyright notice or release identifier; do not describe it as an upstream
miniz release. Its origin and any local modifications to vendored files need
to be recorded during repository provenance review before the public release.

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
