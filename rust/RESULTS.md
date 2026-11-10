# Results

Run on 2026-11-10 on macOS arm64 (macOS 15), cargo 1.95.0 / rustc 1.95.0, from a clean
`cargo clean` state.

## `cargo build`

**Passed.** Workspace `rust/` (oscairn-clap-sys, oscairn-clap, examples/ui-hints) builds
warning-free.

## `cargo test`

**Passed — 8 tests, 0 failures.**

- `oscairn-clap-sys/tests/layout.rs` (4): `c_layout_matches` (size, alignment and every field
  offset of all 22 extension structs, asserted against the output of `layout/layout_gen.c`
  compiled at build time with the same headers bindgen sees, including the `CLAP_INVALID_ID`
  sentinel); `extension_ids_match_the_headers` (the 18 `ids::` constants against the headers'
  `static const char[]`); `enum_values_match_the_headers`; `clap_sys_types_agree` (the CLAP
  types the extensions embed match `clap-sys` 0.5.0's).
- `oscairn-clap/tests/wrapper.rs` (3): the `oscairn_plugin_ui_t` and
  `oscairn_plugin_voice_budget_t` tables' entry points driven through the C ABI (hint fields,
  `CLAP_INVALID_ID` for no range high, `seconds`, capacity capping of the note report, the
  `OSCAIRN_VOICE_BUDGET_SELECTED` fallback of `Limits::of`).
- `examples/ui-hints/tests/host_smoke.rs` (1): the plugin walked through the exported
  `clap_entry` — factory, create, init, activate, one 256-frame block with a note-on and a
  parameter event (non-silent output asserted), then `oscairn.ui/draft-5`'s `param_hint` and
  `seconds` through `get_extension`.

## Example bundle

**Built.** `cargo build --release -p ui-hints`, packaged as
`target/ui-hints.clap/Contents/MacOS/ui-hints`. `otool -L` shows only
`/usr/lib/libSystem.B.dylib` beyond the bundle itself.

## `oscairn-plugin-check`

**Passed (exit 0, `PASSED`).**

```
oscairn-plugin-check target/ui-hints.clap com.oscairn.examples.ui-hints synth
```

All 15 checks ok: loads only system libraries and its own bundle; loads in a part; plays
(rms 0.138584); measured while it played; opens as saved; plays as saved; opened again with
every parameter as saved (reopened render identical, rms 0); exported with nothing left out;
the archive carries the build; installs with nothing to report; opens from the plugin store
on the fresh device; every parameter as saved; plays there; the store reports the open
Performance using it. The cross-device render differs by rms 0.00048882: the oscillator's
phase is not saved state, which the checker itself lists as not a failure.

## Not checked

- The Linux leg of `.github/workflows/rust.yml` was not run locally (no Linux toolchain
  here); CI runs `cargo test` and builds the example on both OSes. The Linux bundle is the
  plain `.so` as `.clap`, which Oscairn's loader opens as a file directly.
- `oscairn.voice-budget` is exercised by unit tests only; the example plugin does not
  implement it (a sine has one voice), and `oscairn-plugin-check` sets no CPU budget, so the
  host never calls `set_limits` on it.
