# memory — `oscairn.memory`

A delay that declares what activating would allocate (`estimate()`), so a host near its
memory limit can refuse the plugin *before* the allocation, and reports what it holds once
active (`current()`). The delay line is 2 s, mono; the declaration must be honest, since the
host counts the larger of it and its own measurement.

**Build:** `cmake --build build --target oscairn-example-memory`

**In Oscairn:** load it as an effect: Oscairn reads the estimate before activating it, and
counts the larger of it and its own measurement.

**Self-test:** `ctest --test-dir build -R oscairn-example-memory` — the estimate names the
line's bytes and grows with the rate, `current()` is refused before activate and equals the
estimate after, and the wet mix passes the delayed signal.
