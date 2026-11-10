# assets — `oscairn.asset-dependencies` draft 2

A gain effect shaped by one curve file it *references* rather than owns:
library + relative path, never a local path. At state load it resolves the
reference through the host's `oscairn.assets` extension (which verifies the file
and fills the SHA-256), keeps working on a built-in fallback when the file is
missing — the asset is optional — and lists the reference through
`oscairn.asset-dependencies/draft-2` so a Performance can record and check it.
`library_root` (draft 2) is the folder the host installs the library's files in.

**Build:** `cmake --build build --target oscairn-example-assets`

**In Oscairn:** load a state naming `tables:curves/bright.bin`: the sound follows
the installed file; uninstall the library and the effect keeps working on its
fallback curve, and the Performance still names the missing asset.

**Self-test:** `ctest --test-dir build -R oscairn-example-assets` — a fresh
instance uses no assets, a state naming one loads and resolves (hash filled),
dependencies report library/path/optional correctly, state round-trips the
reference, and sound still works with the file gone.
