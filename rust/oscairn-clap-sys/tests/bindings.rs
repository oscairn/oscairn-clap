// SPDX-License-Identifier: MIT
//! Checks the generated bindings against the headers. Struct sizes, alignments and field
//! offsets need no test here: bindgen's layout tests assert them at compile time.

use oscairn_clap_sys as sys;

const BINDINGS: &str = include_str!(concat!(env!("OUT_DIR"), "/bindings.rs"));

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
            let bound = [format!("pub const {name}:"), format!("pub type {name} ="), format!("pub mod {name} {{")]
                .iter()
                .any(|decl| BINDINGS.contains(decl.as_str()));
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

/// `clap-sys` and the generated bindings must agree on the CLAP types the extensions embed,
/// so a plugin can mix them.
#[test]
fn clap_sys_types_agree() {
    use std::mem::{align_of, size_of};
    assert_eq!(size_of::<sys::clap_plugin_t>(), size_of::<clap_sys::plugin::clap_plugin>());
    assert_eq!(align_of::<sys::clap_plugin_t>(), align_of::<clap_sys::plugin::clap_plugin>());
    assert_eq!(size_of::<sys::clap_host_t>(), size_of::<clap_sys::host::clap_host>());
    assert_eq!(size_of::<sys::clap_event_header_t>(), size_of::<clap_sys::events::clap_event_header>());
    assert_eq!(size_of::<sys::clap_istream_t>(), size_of::<clap_sys::stream::clap_istream>());
    assert_eq!(size_of::<sys::clap_ostream_t>(), size_of::<clap_sys::stream::clap_ostream>());
    assert_eq!(size_of::<sys::clap_audio_buffer_t>(), size_of::<clap_sys::audio_buffer::clap_audio_buffer>());
}
