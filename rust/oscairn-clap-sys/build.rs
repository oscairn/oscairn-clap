// SPDX-License-Identifier: MIT
//! With the `generate` feature, regenerates `src/bindings.rs` with bindgen (without it this
//! script does nothing and nobody needs libclang), from every `*.h` in the repository's
//! own `include/oscairn-clap/` (the C headers, not the `.hpp` SDK helpers), against the
//! vendored CLAP 1.2.10 headers (`rust/clap-headers`, the version Oscairn pins). Nothing here
//! names a header, so a new one is bound without an edit. bindgen's layout tests are on: they
//! assert, at compile time, that every struct's size, alignment and field offsets match the
//! layout clang computes for the C headers.

#[cfg(feature = "generate")]
use std::path::PathBuf;

#[cfg(not(feature = "generate"))]
fn main() {}

#[cfg(feature = "generate")]
fn main() {
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let out = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let include = manifest.join("../../include");
    let headers_dir = include.join("oscairn-clap");
    let clap_include = manifest.join("../clap-headers");
    println!("cargo:rerun-if-changed={}", headers_dir.display());
    println!("cargo:rerun-if-changed={}", clap_include.display());

    let mut headers: Vec<String> = std::fs::read_dir(&headers_dir)
        .expect("reading include/oscairn-clap failed")
        .map(|entry| entry.unwrap().file_name().into_string().unwrap())
        .filter(|name| name.ends_with(".h"))
        .collect();
    headers.sort();
    // glibc spells UINT64_MAX through a function-like macro, and MSVC spells UINT32_MAX with a
    // `ui32` suffix; bindgen evaluates neither, so the macros using them vanish there. One
    // plain literal each gives every platform the same value.
    let mut all = String::from(
        "#include <stdint.h>\n#undef UINT32_MAX\n#define UINT32_MAX 4294967295U\n#undef UINT64_MAX\n#define UINT64_MAX 18446744073709551615ULL\n",
    );
    all.extend(headers.iter().map(|h| format!("#include <oscairn-clap/{h}>\n")));
    let all_path = out.join("oscairn_clap_all.h");
    std::fs::write(&all_path, all).expect("writing oscairn_clap_all.h failed");

    let bindings = bindgen::Builder::default()
        .header(all_path.to_str().unwrap())
        .clang_arg(format!("-I{}", include.display()))
        .clang_arg(format!("-I{}", clap_include.display()))
        // Everything the Oscairn headers declare. The CLAP types they use are `clap-sys`'s: bindgen
        // is told not to emit them, and the prelude below names each one the way the headers do.
        .allowlist_file(".*/oscairn-clap/[^/]*\\.h")
        .blocklist_type("clap_.*")
        .raw_line(CLAP_SYS_PRELUDE)
        // A named enum's values in a module of its name; an anonymous enum's at the top.
        .constified_enum_module("oscairn_.*")
        .derive_default(true)
        .layout_tests(true)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .parse_callbacks(Box::new(UnsignedMax))
        .generate()
        .expect("bindgen failed over the Oscairn CLAP headers");
    let rust = bindings.to_string();

    // Each `static const char NAME[]` (an extension or event-space ID) becomes `ids::NAME`, a
    // `&CStr` over bindgen's own byte string.
    let mut ids = String::from("pub mod ids {\n    use core::ffi::CStr;\n");
    // Whitespace-blind: the output may be unformatted where rustfmt is not installed.
    for chunk in rust.split("pub const ").skip(1) {
        let decl: String = chunk.split('=').next().unwrap().split_whitespace().collect();
        if let Some((name, _)) = decl.split_once(':').filter(|(_, ty)| ty.starts_with("&[u8;")) {
            ids.push_str(&format!("    pub const {name}: &CStr = crate::id(crate::{name});\n"));
        }
    }
    ids.push_str("}\n");

    std::fs::write(manifest.join("src/bindings.rs"), rust + &ids).expect("writing src/bindings.rs failed");
}

/// The CLAP items the generated code names, each at its `clap-sys` path under its C name (the
/// headers say `clap_host_t`; `clap-sys` says `clap_host`).
#[cfg(feature = "generate")]
const CLAP_SYS_PRELUDE: &str = "\
use clap_sys::audio_buffer::clap_audio_buffer as clap_audio_buffer_t;
use clap_sys::events::clap_event_header as clap_event_header_t;
use clap_sys::ext::state::clap_plugin_state as clap_plugin_state_t;
use clap_sys::fixedpoint::clap_beattime;
use clap_sys::host::clap_host as clap_host_t;
use clap_sys::id::clap_id;
use clap_sys::plugin::clap_plugin as clap_plugin_t;
use clap_sys::stream::{clap_istream as clap_istream_t, clap_ostream as clap_ostream_t};
";

/// bindgen reads `#define X UINT64_MAX` as an i32 -1. The headers define no negative macro,
/// so a negative one is an unsigned maximum. shortcut: a real negative `#define` would come out
/// as a u64; `tests/bindings.rs` pins the two there are.
#[cfg(feature = "generate")]
#[derive(Debug)]
struct UnsignedMax;

#[cfg(feature = "generate")]
impl bindgen::callbacks::ParseCallbacks for UnsignedMax {
    fn int_macro(&self, name: &str, value: i64) -> Option<bindgen::callbacks::IntKind> {
        (name.starts_with("OSCAIRN_") && value < 0).then_some(bindgen::callbacks::IntKind::U64)
    }
}
