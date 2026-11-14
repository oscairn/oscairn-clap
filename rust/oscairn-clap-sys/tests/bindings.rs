// SPDX-License-Identifier: MIT
//! Checks the generated bindings against the headers. Struct sizes, alignments and field
//! offsets need no test here: bindgen's layout tests assert them at compile time.

use oscairn_clap_sys as sys;

const BINDINGS: &str = include_str!("../src/bindings.rs");

/// The names a header declares at file scope: its typedefs, named enums, `static const`s and
/// `OSCAIRN_` macros. Enough of C for these headers, which keep one declaration per line.
fn declared_names(header: &str) -> Vec<String> {
    let mut names = Vec::new();
    for line in header.lines().map(str::trim) {
        let words: Vec<&str> = line.split_whitespace().collect();
        let name = match words.as_slice() {
            ["static", "const", _, name, ..] => name.split('[').next(),
            ["#define", name, ..] if name.starts_with("OSCAIRN_") => Some(*name),
            ["enum", name, ..] if *name != "{" => Some(name.trim_end_matches('{')),
            _ => line.rsplit_once('}').and_then(|(_, rest)| rest.trim().strip_suffix(';')).filter(|n| !n.is_empty()),
        };
        names.extend(name.map(String::from));
    }
    names
}

/// A new header in `include/oscairn-clap/` is bound without anyone listing it; this fails if
/// one is not, or if any name it declares is missing from the bindings.
#[test]
fn every_header_is_bound() {
    let dir = concat!(env!("CARGO_MANIFEST_DIR"), "/../../include/oscairn-clap");
    // Whitespace-blind, as the output is unformatted where rustfmt is not installed.
    let bindings: String = BINDINGS.split_whitespace().collect();
    let mut headers = 0;
    for entry in std::fs::read_dir(dir).unwrap() {
        let path = entry.unwrap().path();
        if path.extension().is_none_or(|e| e != "h") {
            continue;
        }
        headers += 1;
        let names = declared_names(&std::fs::read_to_string(&path).unwrap());
        assert!(!names.is_empty(), "{} declares nothing this test recognizes", path.display());
        for name in names {
            let bound = [format!("pubconst{name}:"), format!("pubtype{name}="), format!("pubmod{name}{{")]
                .iter()
                .any(|decl| bindings.contains(decl.as_str()));
            assert!(bound, "{name} from {} is not in the bindings", path.display());
        }
    }
    assert!(headers >= 10, "found only {headers} headers in {dir}");
}

#[test]
fn ids_are_the_header_strings() {
    assert_eq!(sys::ids::OSCAIRN_EXT_UI, c"oscairn.ui/draft-5");
    assert_eq!(sys::ids::OSCAIRN_EXT_HARDWARE, c"oscairn.hardware/draft");
    assert_eq!(sys::ids::OSCAIRN_EXT_HARDWARE.to_bytes_with_nul(), sys::OSCAIRN_EXT_HARDWARE);
}

/// A few values per enum, against the headers.
#[test]
fn enum_and_constant_values_match_the_headers() {
    use sys::{oscairn_field as f, oscairn_hardware_status as hw, oscairn_role as r, oscairn_widget as w};
    assert_eq!((w::OSCAIRN_WIDGET_AUTO, w::OSCAIRN_WIDGET_RANGE), (0, 6));
    assert_eq!((r::OSCAIRN_ROLE_NONE, r::OSCAIRN_ROLE_CUTOFF, r::OSCAIRN_ROLE_OUTPUT_BUS), (0, 1, 35));
    assert_eq!((f::OSCAIRN_FIELD_ATTACK, f::OSCAIRN_FIELD_LEVEL_0, f::OSCAIRN_FIELD_TIME_1, f::OSCAIRN_FIELD_FIXED), (2, 16, 32, 102));
    assert_eq!((sys::OSCAIRN_VOICE_BUDGET_MONO, sys::OSCAIRN_VOICE_BUDGET_LEGATO), (1, 2));
    assert_eq!(sys::OSCAIRN_VOICE_BUDGET_SELECTED, u32::MAX);
    assert_eq!(sys::OSCAIRN_VOICE_BUDGET_MAX_CONTEXTS, 16);
    assert_eq!(sys::oscairn_chord_type::OSCAIRN_CHORD_MINOR9, 22);
    assert_eq!((sys::OSCAIRN_EVENT_BEAT, sys::OSCAIRN_EVENT_HARMONY, sys::OSCAIRN_EVENT_PARAM_TARGET), (0, 0, 0));
    assert_eq!((hw::OSCAIRN_HARDWARE_PENDING, hw::OSCAIRN_HARDWARE_UNKNOWN_CAPACITY), (1, 20));
    assert_eq!(sys::OSCAIRN_HARDWARE_UNKNOWN, u32::MAX);
    assert_eq!(sys::OSCAIRN_HARDWARE_UNKNOWN_BYTES, u64::MAX);
}

/// The generated structs use `clap-sys`'s CLAP types themselves, not copies: each field below
/// is passed where the `clap-sys` type is expected, which only compiles if they are one type.
#[test]
fn clap_sys_types_are_the_generated_ones() {
    use sys::clap_sys::{host::clap_host, plugin::clap_plugin};
    fn host(_: *const clap_host) {}
    fn plugin(_: *const clap_plugin) {}
    fn header(_: sys::clap_sys::events::clap_event_header) {}
    let _ = |t: sys::oscairn_plugin_ui_t| t.param_hint.map(|f| unsafe { f(std::ptr::null::<clap_plugin>(), 0, std::ptr::null_mut()) });
    let _ = |t: sys::oscairn_host_harmony_t| t.get.map(|f| unsafe { f(std::ptr::null::<clap_host>(), std::ptr::null_mut()) });
    let _ = |e: sys::oscairn_event_beat_t| header(e.header);
    let _ = (host, plugin);
}
