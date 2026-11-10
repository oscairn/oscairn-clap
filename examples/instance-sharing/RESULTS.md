# instance-sharing results

Run 2026-11-10 on macOS 15 (arm64), Apple Clang, CLAP 1.2.10 fetched at tag `1.2.10`.
Checker: `oscairn/build-app/core/control/oscairn-plugin-check`.
JSON-RPC test server: `oscairn/build-app/core/control/oscairn-json-rpc-test`.

Commands and outcome:

```
cmake -S . -B build -DOSCAIRN_PLUGIN_CHECK=.../oscairn-plugin-check \
                   -DOSCAIRN_JSON_RPC_TEST=.../oscairn-json-rpc-test   # configure OK
cmake --build build -j8                                                # all targets, no warnings
ctest --test-dir build --output-on-failure                             # 23/23 passed
```

| Test | Verdict |
|---|---|
| `oscairn-example-instance-sharing-selftest` (label `self-test`) | PASSED — 57 checks: extensions offered (sound-contexts draft 5 with `state`, sound-snapshots draft 4 with `save_sound` + `sound_values`, `oscairn.ui`), 4 stereo outputs with one main / no audio input / 1 note in / no note out, the output-bus parameter stepped, writable, 0–3 with `OSCAIRN_ROLE_OUTPUT_BUS`, a sound on bus 2 rendering on port 2 and not the main port, a per-channel edit moving only its own sound, `save_sound` → `state->load` into a fresh instance holding exactly that sound, selected on all 16 channels, playing on the main port |
| `oscairn-example-instance-sharing-plugin-check` (label `plugin-check`, `--param 1=0.1`) | PASSED — the standard 20-check sequence: load, play, edit changes the sound, save/open round-trip, parameter persistence, export, install, reopen |
| `oscairn-example-instance-sharing-oscairn` (label `oscairn-sharing`) | PASSED — `oscairn-sharing.py` against the real Oscairn session; output: `ok: two parts share one instance, each its channel and bus` |

The other 20 tests (the ten existing examples' self-tests and plugin-checks) passed unchanged.

Evidence from `graph.get` after adding a second node of the plugin (`Right`) to a second part
and committing (the failure mode the fallback matrix warns about — each part on its own
instance — would show `"shared": null` on both):

```
synth  1 {"engine": 1, "channel": 0, "bus": 0}
Right  2 {"engine": 1, "channel": 1, "bus": 1}
```

Both nodes name the same engine node (1), on different MIDI channels and different output
buses. `params.list` on the shared node shows the host kept the hint:
`Output bus — role: outputBus, stepped: true, min/max: 0 3`, and a `params.set` of the
second part's Pitch moves its own sound only (asserted in the script).

Not checked here: Linux (CI covers the self-tests there; the plugin-check and
oscairn-sharing labels need a built Oscairn); ring-out of a *shared* instance's sounds
across a Performance change (host-internal; Oscairn's own `control-shared-parts` and
`shared-parts` tests cover it).
