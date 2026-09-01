# qrcodegen — QR Code generator library (C)

Project Nayuki's QR Code generator, the C port. Two files, MIT, no dependencies
beyond the C standard library.

- Upstream: <https://github.com/nayuki/QR-Code-generator> (`c/` subdirectory)
- Homepage: <https://www.nayuki.io/page/qr-code-generator-library>
- License: MIT — see [LICENSE](LICENSE), and the header of each source file.

## The pin

| | |
|---|---|
| Fetched from | `master` @ `3c6d0b3cefb4e049dc337e82237c9644399716a8` |
| `c/` last changed at | `8329a7108fc22be3e1eec0a9f9318978579e3621` (2024-09-01) |
| Release | v1.8.0 plus that one commit |
| Vendored | 2026-09-01, unmodified |

`master` moves for the other seven language ports in that repo; these two files
have not changed since 2024. Both facts are recorded because only the second one
describes the code that is actually here, and only the first says where to look.

The files are byte-identical to upstream — no local edits, no reformatting:

```
f3046ee69e4325f12b573c34cf1e7e32  qrcodegen.c
669e5d1fdde4c7ce77906a72f517d619  qrcodegen.h
```

Keep it that way. A fix belongs upstream and comes back as a new pin; a local
edit here is invisible at the next update and gets silently reverted by it.

## Copied, not a submodule

The unusual choice in this repo, where `oldschool-clientc` is a submodule and
the reasoning for that is written down at length in the root `CMakeLists.txt`.
The difference is what a version costs. `oldschool-clientc` is a live
dependency: its Xtensa raster kernels are developed alongside this firmware, so
its version has to be a commit this repo can move. qrcodegen is 60 KB of frozen,
self-contained code inside a repo that also carries Java, Python, Rust, C++,
TypeScript and two more ports — a submodule would clone all of them, and the
`--init --recursive` step is already a documented tripwire for fresh clones. The
two files plus a recorded SHA give the same traceability for none of that.

## Using it

`components/qrcodegen` is the whole build integration — there is no
configuration and nothing to keep in step.

```c
#include "qrcodegen.h"

uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
if (qrcodegen_encodeText("https://example.com", tmp, qr,
        qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true)) {
    int size = qrcodegen_getSize(qr);          // modules per side
    bool dark = qrcodegen_getModule(qr, x, y); // true = dark module
}
```

Nothing in this firmware calls it directly. [`components/qrstyle`](../../components/qrstyle)
does, and it is what turns that matrix into pixels — module shapes, styled
finder patterns, gradients and a logo. It reads this library and never modifies
it, which is the arrangement that lets the files above stay byte-identical to
upstream.

## What it costs

The caller owns every buffer; the library allocates nothing (`stdlib.h` is there
for `abs`/`labs`, not `malloc`) and holds no static state — `.data` and `.bss`
are both zero. Measured with `xtensa-esp32s3-elf-gcc 14.2.0`:

| Build | text |
|---|---|
| `-O2` | 11.4 KB |
| `-Os -DNDEBUG` | 5.6 KB |

Most of that difference is `assert` strings, and IDF release builds define
`NDEBUG` already.

The RAM cost is the two buffers, and it is set by the *maximum* version you
allow, not by the data — `qrcodegen_BUFFER_LEN_FOR_VERSION(n)` bytes each:

| Max version | Modules | Bytes per buffer | Two buffers |
|---|---|---|---|
| 4 | 33×33 | 138 | 276 |
| 10 | 57×57 | 408 | 816 |
| 40 | 177×177 | 3918 | 7836 |

Version 40 is 7.8 KB of a part that has no PSRAM. Cap `maxVersion` at what the
payload actually needs; a URL of a few dozen characters fits well inside 10.

## Updating

```sh
BASE=https://raw.githubusercontent.com/nayuki/QR-Code-generator/master/c
curl -sSLO $BASE/qrcodegen.c
curl -sSLO $BASE/qrcodegen.h
```

Then update the SHAs, the date and the checksums in this file, and re-measure if
the footprint table is quoted anywhere else.
