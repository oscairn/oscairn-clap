# Oscairn CLAP extension examples

One minimal plugin per Oscairn CLAP extension (`include/oscairn-clap/`). Each is a
single `.cpp` with a sine or a gain stage for audio — everything beyond the extension
under demo is deliberately small. Each folder has its own `README.md`.

| Folder | Extension | Plugin ID |
|---|---|---|
| `ui` | `oscairn.ui` draft 5 (hints, groups, views, stage times) | `org.oscairn.example.ui` |
| `memory` | `oscairn.memory` (worker memory declarations) | `org.oscairn.example.memory` |
| `sound-contexts` | `oscairn.sound-contexts` draft 5 | `org.oscairn.example.sound-contexts` |
| `sound-snapshots` | `oscairn.sound-snapshots` draft 4 | `org.oscairn.example.sound-snapshots` |
| `voice-budget` | `oscairn.voice-budget` draft 1 | `org.oscairn.example.voice-budget` |
| `effect-contexts` | `oscairn.effect-contexts` draft 3 | `org.oscairn.example.effect-contexts` |
| `param-target` | `oscairn.param-target` (lane-only modulator) | `org.oscairn.example.param-target` |
| `beat` | `oscairn.beat` (free-running beat events) | `org.oscairn.example.beat` |
| `harmony` | `oscairn.harmony` (key/scale of the part) | `org.oscairn.example.harmony` |
| `assets` | `oscairn.asset-dependencies` draft 2 | `org.oscairn.example.assets` |
| `instance-sharing` | parts sharing one instance (sound contexts + sound-snapshots draft 4 + output-bus role) | `org.oscairn.example.instance-sharing` |

## Build

```sh
cmake -S . -B build && cmake --build build -j8
```

CLAP 1.2.10 is fetched with FetchContent. macOS gets `.clap` bundles, Linux plain
`.so`s.

## Test

```sh
ctest --test-dir build --output-on-failure                      # everything
ctest --test-dir build -L self-test                             # self-tests only
```

Every example has a self-test: a mini host (`mini-host.hpp`) dlopens the built
`.clap` and drives its extension directly. If `oscairn-plugin-check` (built from the
Oscairn repository) is available, register its checks too:

```sh
cmake -S . -B build -DOSCAIRN_PLUGIN_CHECK=/path/to/oscairn-plugin-check
ctest --test-dir build -L plugin-check
```

With `-DOSCAIRN_JSON_RPC_TEST=/path/to/oscairn-json-rpc-test` (also built from Oscairn),
the instance-sharing example registers one more test (label `oscairn-sharing`): two parts
of its plugin sharing one instance in a running Oscairn, checked through `graph.get`.

`RESULTS.md` records what passed and failed on the machine these were developed on.
