# Results

Run 2026-11-09 on macOS 15 (arm64), Apple Clang, CLAP 1.2.10 fetched at tag `1.2.10`.
Checker: `oscairn/build-app/core/control/oscairn-plugin-check`.

Commands and outcome:

```
cmake -S . -B build -DOSCAIRN_PLUGIN_CHECK=.../oscairn-plugin-check   # configure OK
cmake --build build -j8                                               # all targets, no warnings
ctest --test-dir build --output-on-failure                            # 20/20 passed
```

10 self-tests (label `self-test`) + 10 plugin-check tests (label `plugin-check`) —
every one passed. CI runs the self-tests only; plugin-check needs a built Oscairn.

Plugin-check runs an identical 20-check sequence per plugin (load, play, measure,
edit, save/open round-trip, parameter persistence, export, install, reopen from the
store). Results per example, with the check that proves the extension's host-side
behavior in bold:

| Example | Verdict | Notes |
|---|---|---|
| ui | PASSED | plays (rms .078), edit `2=0.1` changes the sound, edit survives save/reload and store reopen |
| memory | PASSED | effect changes the signal; host **measure and declaration both recorded** (`--estimate macos-arm64 .0024 1556480`); mix edit survives reopen |
| sound-contexts | PASSED | **loads in a part and plays; context edits persist** through save/reload/store |
| sound-snapshots | PASSED | same sequence; snapshot path exercised by the self-test (plugin-check drives state, not the snapshot prepare) |
| voice-budget | PASSED | plays; level edit persists; budget negotiation itself is host-internal — covered by the self-test |
| effect-contexts | PASSED | effect changes the signal; gain edit persists per state |
| param-target | PASSED | checked as a note effect (no audio of its own, like the in-repo LFO): **"plays (transforms notes)"**, state round-trips |
| beat | PASSED | plays; level edit persists (state carries the level since this session's fix) |
| harmony | PASSED | plays; level edit persists (state carries level + harmony reading since this session's fix) |
| assets | PASSED | effect changes the signal; amount edit persists; dependency listing verified by the self-test |

Self-tests drive what plugin-check cannot: the extension protocols themselves
(prepare/select/discard, snapshot ring-out, voice fades, lane events, beat events,
harmony events, asset resolve/fallback), each through a mini host (`mini-host.hpp`).
All 10 passed.

Failures found and fixed during this session (all confirmed passing after):

- memory: `%23s` sscanf truncated the 24-char state header → state reload rejected.
- beat, harmony: state saved no parameters → plugin-check "parameter as edited"
  failed after reopen; state now carries the level.
- sound-contexts: per-context level array sized 2, not 4 → stack overflow (ASan).
- examples/CMakeLists.txt: `--param ID=VALUE` was one CMake argument, so ctest
  passed plugin-check with the parameter value silently dropped; the CTest command
  now passes `--param` and `ID=VALUE` separately. param-target gets no `--param`
  (a lane-only modulator has no audible output to change — same as Oscairn's LFO).
- assets: state terminated the last field with `\n` instead of NUL → own state
  would not load back.

Not checked here: Linux builds (CI covers them); `--juce-state`, `--add-build`
checker modes (JUCE plugins / multi-platform archives — out of scope for these
single-platform examples).
