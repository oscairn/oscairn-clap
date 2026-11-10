// SPDX-License-Identifier: MIT
//! Generates the raw bindings at build time with bindgen, from every `*.h` in the repository's
//! own `include/oscairn-clap/` (the C headers, not the `.hpp` SDK helpers), against the
//! vendored CLAP 1.2.10 headers (`rust/clap-headers`, the version Oscairn pins). Nothing here
//! names a header, so a new one is bound without an edit. bindgen's layout tests are on: they
//! assert, at compile time, that every struct's size, alignment and field offsets match the
//! layout clang computes for the C headers.

use std::path::PathBuf;

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
    // glibc spells UINT64_MAX through a function-like macro bindgen cannot evaluate, so the
    // macros using it vanish on Linux; one plain literal gives every platform the same value.
    let mut all = String::from("#include <stdint.h>\n#undef UINT64_MAX\n#define UINT64_MAX 18446744073709551615ULL\n");
    all.extend(headers.iter().map(|h| format!("#include <oscairn-clap/{h}>\n")));
    let all_path = out.join("oscairn_clap_all.h");
    std::fs::write(&all_path, all).expect("writing oscairn_clap_all.h failed");

    let bindings = bindgen::Builder::default()
        .header(all_path.to_str().unwrap())
        .clang_arg(format!("-I{}", include.display()))
        .clang_arg(format!("-I{}", clap_include.display()))
        // Everything the Oscairn headers declare, and the CLAP types they use.
        .allowlist_file(".*/oscairn-clap/[^/]*\\.h")
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
    // Whitespace-blind, as the output is unformatted where rustfmt is not installed.
    for chunk in rust.split("pub const ").skip(1) {
        let decl: String = chunk.split('=').next().unwrap().split_whitespace().collect();
        if let Some((name, _)) = decl.split_once(':').filter(|(_, ty)| ty.starts_with("&[u8;")) {
            ids.push_str(&format!("    pub const {name}: &CStr = crate::id(crate::{name});\n"));
        }
    }
    ids.push_str("}\n");

    std::fs::write(out.join("bindings.rs"), rust + &ids).expect("writing bindings.rs failed");
}

/// bindgen reads `#define X UINT64_MAX` as an i32 -1. The headers define no negative macro,
/// so a negative one is an unsigned maximum. shortcut: a real negative `#define` would come out
/// as a u64; `tests/bindings.rs` pins the two there are.
#[derive(Debug)]
struct UnsignedMax;

impl bindgen::callbacks::ParseCallbacks for UnsignedMax {
    fn int_macro(&self, name: &str, value: i64) -> Option<bindgen::callbacks::IntKind> {
        (name.starts_with("OSCAIRN_") && value < 0).then_some(bindgen::callbacks::IntKind::U64)
    }
}
