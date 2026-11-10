# assets — `oscairn.assets` and `oscairn.asset-dependencies` draft 2

A waveshaper whose curve comes from a file it *references* rather than owns: library and
relative path, never a local path. At state load it resolves the reference through the
host's `oscairn.assets` (which verifies the file and fills in its SHA-256), and lists it
through `oscairn.asset-dependencies` so a Performance can record and check it. The asset is
optional: with the file missing, the plugin keeps working on its built-in curve.

**Build:** `cmake --build build --target oscairn-example-assets`

**In Oscairn:** load a state naming `tables:curves/bright.bin`: the sound follows the
installed file; uninstall the library and the effect keeps working on its fallback curve,
and the Performance still names the missing asset.

**Self-test:** `ctest --test-dir build -R oscairn-example-assets` — a fresh instance uses
no assets; a state naming one loads, resolves and keeps the hash; the dependency reports
library, path and optional; the saved state carries the reference and no local path; with
the file gone the state still loads, still lists the asset, and the fallback curve plays.
