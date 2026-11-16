# Oscairn CLAP bindings for Rust

Rust bindings for the Oscairn CLAP extensions (the headers in [`include/oscairn-clap/`](../include/oscairn-clap/)),
plus one example plugin. MIT, like the rest of this repository.

| Crate | What |
| --- | --- |
| [`oscairn-clap-sys`](oscairn-clap-sys/) | raw FFI bindings for every C header, generated with bindgen and checked in, and the extension ID constants |
| [`oscairn-clap`](oscairn-clap/) | thin safe wrappers: `oscairn.ui` (parameter hints and roles), `oscairn.voice-budget` (the host's limits), extension IDs |
| [`examples/ui-hints`](examples/ui-hints/) | a sine instrument that declares its hints through the wrapper |

## Depending on it

```toml
[dependencies]
oscairn-clap = { path = "rust/oscairn-clap" }   # or a git dependency on this repository
```

`oscairn-clap-sys` ships its bindings pre-generated (`src/bindings.rs`), so building needs no
libclang. The `generate` feature regenerates them.

The CLAP types the extensions use (`clap_plugin`, `clap_host`, `clap_event_header`, `clap_id`, ...)
are [`clap-sys`](https://crates.io/crates/clap-sys) 0.5.0's own, not copies, so a plugin built on
`clap-sys` hands its types to the `oscairn_*` tables without a cast. `oscairn-clap-sys`
re-exports the crate, so a consumer gets the same version:

```rust
use oscairn_clap_sys::clap_sys::{host::clap_host, plugin::clap_plugin}; // or depend on clap-sys = "=0.5.0" directly
use oscairn_clap_sys::oscairn_plugin_ui_t; // its `param_hint` takes `*const clap_plugin`
```

## What is wrapped, what is raw only

Wrapped (`oscairn-clap`):

- `oscairn.ui` — implement `ui::UiHints` (`param_hint`, `seconds`, `algorithm`, and for draft 6 `tab_count`, `tab`,
  `param_tab` behind `HAS_TABS` and `value_icon` behind `HAS_VALUE_ICONS`, null unless set) and put
  `ui_table!(YourPlugin)` in a `static`; return it from `get_extension` for
  `ids::OSCAIRN_EXT_UI` (one draft-6 table answers the draft-5, draft-4 and draft-3 IDs too, whose
  members are a prefix). `Widget`, `Role` and `Field` carry the headers' values.
- `oscairn.voice-budget` — implement `voice_budget::VoiceBudget` and `voice_budget_table!`;
  `Limits` copies the host's `set_limits` entry and answers `of(context, selected)` with the
  `OSCAIRN_VOICE_BUDGET_SELECTED` fallback the header prescribes.
- Extension and event-space ID constants: `oscairn_clap::ids`, a `&CStr` for each ID string
  in the headers, generated from the bindings.

Raw only (`oscairn-clap-sys`): everything else — sound contexts and snapshots, assets and
asset-dependencies, memory, effect contexts, param-target, beat, harmony, hardware — as
generated structs, tables and constants. Implement their tables directly if you need them.

## Building the bindings

`oscairn-clap-sys/src/bindings.rs` is checked in, one file for every target: macOS arm64 and
Linux x86_64 and aarch64 generate it identically, and Windows differs only in the integer type
of the enums' `Type` aliases (`c_int` on MSVC, unused by any struct or function), so the layout
assertions hold there too. After any change under `include/` or `clap-headers/`, regenerate it (needs libclang; use the pinned
rustc, as CI does, so rustfmt formats it the same):

```sh
cargo build -p oscairn-clap-sys --features generate
```

CI regenerates it and fails if the file changes. With `generate`, the build script runs
bindgen over every `*.h` in the repository's own `include/oscairn-clap/` (the C headers; not the `.hpp` SDK helpers), so a new header is bound
without an edit here, against the CLAP **1.2.10** headers vendored at
[`clap-headers/`](clap-headers/) — the version Oscairn pins. bindgen needs C headers to
parse, and `clap-sys` ships only Rust, so the vendored headers stay as bindgen's include path
only: bindgen blocklists the `clap_*` types and the generated code imports `clap-sys`'s
instead (the `use` lines at the top of `bindings.rs`, written by `build.rs`; a test checks
the generated structs take them). bindgen's layout tests assert every
struct's size, alignment and field offsets against clang's layout at compile time, and a test
fails if any header's declarations are missing from the bindings.

## Building the example

```sh
cargo build --release -p ui-hints
# macOS: a .clap is a bundle.
mkdir -p target/ui-hints.clap/Contents/MacOS
cp target/release/libui_hints.dylib target/ui-hints.clap/Contents/MacOS/ui-hints
# Linux: the shared library itself is the .clap.
cp target/release/libui_hints.so target/ui-hints.clap
```

Then check it with Oscairn's checker:

```sh
oscairn-plugin-check target/ui-hints.clap org.oscairn.example.ui-hints synth
```

The example uses raw CLAP through `clap-sys` rather than `clack` or `nih-plug`: the host needs
only about eight entry points and four small extension tables (a C example is ~200 lines), the
extensions this plugin implements answer through one static table, and neither framework can
answer `oscairn.ui` without a `get_extension` escape hatch anyway — which is the whole plugin
in raw CLAP.

## Testing

- `cargo test` — the bindings' tests (every header bound, IDs and enum values against the
  headers, the generated structs taking `clap-sys`'s types), the
  wrapper's round-trip tests (the tables' entry points driven through the C ABI), and a smoke
  test that walks the example through the CLAP entry point to a sounding block.
- `oscairn-plugin-check` — run locally against the built bundle; CI does not have it.
  `oscairn.voice-budget` is covered by the wrapper's tests only: the example does not
  implement it (a sine has one voice).

## License

MIT (see [LICENSE](../LICENSE)). `clap-headers/` is the CLAP SDK under its own MIT license
(see `clap-headers/LICENSE`).
