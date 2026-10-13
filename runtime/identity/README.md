# Identity checks for a new fit

`codec_reference.py` contains nine exact original validator functions, extracted
without numerical or validation changes. `SOURCE_PROVENANCE.json` records the
original generator SHAs and function AST SHAs. They require only Python's
standard library.

`inspect_assets.py` checks the serialized formats, exact original 88-chart
layout, scene/geometry, pairing, checksums, payloads, finite FP32 values and C16
source/protocol/baseline digests. It reads the supplied files and writes an inspection receipt and proposed
rows to a fresh output directory for review. It does not modify compiled headers or certify training,
physical reference QA, selection, native parity or runtime admission.

The reviewed, staged procedure and original receipt dependency graph are in
[docs/IDENTITY.md](../../docs/IDENTITY.md). The shipped original assets are
already admitted. Loading a fresh fit requires the existing runtime loader and
its validation checks to remain intact.
