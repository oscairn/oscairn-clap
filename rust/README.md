# Oscairn CLAP bindings for Rust

Rust bindings for the Oscairn CLAP extensions (the headers in [`include/oscairn-clap/`](../include/oscairn-clap/)),
plus one example plugin. MIT, like the rest of this repository.

| Crate | What |
| --- | --- |
| [`oscairn-clap-sys`](oscairn-clap-sys/) | raw FFI bindings for every C header, generated at build time with bindgen, and the extension ID constants |
| [`oscairn-clap`](oscairn-clap/) | thin safe wrappers: `oscairn.ui` (parameter hints and roles), `oscairn.voice-budget` (the host's limits), extension IDs |
| [`examples/ui-hints`](examples/ui-hints/) | a sine instrument that declares its hints through the wrapper |

## Depending on it

```toml
[dependencies]
oscairn-clap = { path = "rust/oscairn-clap" }   # or a git dependency on this repository
```

`oscairn-clap-sys` builds its bindings with bindgen and runs a small C program at build time,
so a C compiler and libclang are required wherever you build (a normal Rust plugin toolchain
already has both).

## What is wrapped, what is raw only

Wrapped (`oscairn-clap`):

- `oscairn.ui` — implement `ui::UiHints` (`param_hint`, `seconds`, `algorithm`) and put
  `ui_table!(YourPlugin)` in a `static`; return it from `get_extension` for
  `ids::OSCAIRN_EXT_UI` (one draft-5 table answers the draft-4 and draft-3 IDs too, whose
  members are a prefix). `Widget`, `Role` and `Field` carry the headers' values.
- `oscairn.voice-budget` — implement `voice_budget::VoiceBudget` and `voice_budget_table!`;
  `Limits` copies the host's `set_limits` entry and answers `of(context, selected)` with the
  `OSCAIRN_VOICE_BUDGET_SELECTED` fallback the header prescribes.
- Extension and event-space ID constants: `oscairn_clap::ids` (checked against the headers by
  a test).

Raw only (`oscairn-clap-sys`): everything else — sound contexts and snapshots, assets and
asset-dependencies, memory, effect contexts, param-target, beat, harmony — as generated
structs, tables and constants. Implement their tables directly if you need them.

## Building the bindings

`oscairn-clap-sys`'s build script runs bindgen over the repository's own
`include/oscairn-clap/*.h` (the C headers; not the `.hpp` SDK helpers) against the CLAP
**1.2.10** headers vendored at [`clap-headers/`](clap-headers/) — the version Oscairn pins.
(`clap-sys` 0.5.0 ships CLAP 1.2.2 types, which lack, among others, `clap_plugin_state` as
sound-contexts uses it; so bindings are generated from the pinned headers, and a test asserts
the shared CLAP types agree with `clap-sys`, which the wrapper's own types embed.) Every size,
alignment and field offset is double-checked against a C program compiled from the same
headers (`oscairn-clap-sys/layout/layout_gen.c`), and the extension ID strings against the
headers' `static const char[]`.

## Building the example

```sh
cargo build --release -p ui-hints
mkdir -p target/ui-hints.clap/Contents/MacOS            # macOS bundle; on Linux the plain
cp target/release/libui_hints.dylib \                   # .so is a valid .clap on its own:
   target/ui-hints.clap/Contents/MacOS/ui-hints         # cp target/release/libui_hints.so ui-hints.clap
```

Then check it with Oscairn's checker:

```sh
oscairn-plugin-check target/ui-hints.clap com.oscairn.examples.ui-hints synth
```

The example uses raw CLAP through `clap-sys` rather than `clack` or `nih-plug`: the host needs
only about eight entry points and four small extension tables (a C example is ~200 lines), the
extensions this plugin implements answer through one static table, and neither framework can
answer `oscairn.ui` without a `get_extension` escape hatch anyway — which is the whole plugin
in raw CLAP.

## Testing

- `cargo test` — the bindings' layout tests (every struct's size, alignment and field offsets
  against the C compiler's own numbers, IDs and enum values against the headers), the
  wrapper's round-trip tests (the tables' entry points driven through the C ABI), and a smoke
  test that walks the example through the CLAP entry point to a sounding block.
- `oscairn-plugin-check` — run locally against the built bundle (CI does not have it); see
  [RESULTS.md](RESULTS.md) for the last recorded run.

## License

MIT (see [LICENSE](../LICENSE)). `clap-headers/` is the CLAP SDK under its own MIT license
(see `clap-headers/LICENSE`).
