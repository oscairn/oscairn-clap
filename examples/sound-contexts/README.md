# sound-contexts — `oscairn.sound-contexts` draft 5

Four sound contexts (the Default plus three sounds) in one instance, built with the
SDK helper `sound-contexts-plugin.hpp`. The host prepares sounds from parameter
lists while audio runs, selects them per channel at the next process boundary, and
a held note keeps the sound it started on even after its channel selects a new one.

**Build:** `cmake --build build --target oscairn-example-sound-contexts`

**In Oscairn:** every part of a Performance that plays this instrument gets its
sounds prepared here; switching sounds mid-note doesn't cut the tail — the old
sound keeps ringing until its voices end.

**Self-test:** `ctest --test-dir build -R oscairn-example-sound-contexts` —
capacity, Default semantics, bad-parameter refusal with message, prepare/select
flows, discard rules, per-context levels audible.
