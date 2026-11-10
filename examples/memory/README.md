# memory — `oscairn.memory`

A gain-plus-delay effect that declares what activating would allocate
(`estimate()`), so a host near its memory limit can refuse the plugin *before* the
allocation. `current()` reports what the instance actually holds once activated.
The delay line is 2 s per channel — the honest declaration is the whole point: the
host counts the larger of the declaration and its own measurement.

**Build:** `cmake --build build --target oscairn-example-memory`

**In Oscairn:** load it as an effect. plugin-check compares its declaration with the
host's own measurement (`--estimate` output); a lying declaration shows up as a
refused or re-measured load.

**Self-test:** `ctest --test-dir build -R oscairn-example-memory` — estimate at
48 kHz, `current()` refused before activate, equal to the estimate after, Mix
verified audible.
