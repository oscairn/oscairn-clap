// SPDX-License-Identifier: MIT
//! Asserts the generated bindings match the C headers: sizes, alignments and field offsets
//! come from a C program (`layout/layout_gen.c`) compiled at build time with the same headers
//! bindgen sees, and `assert_c_layout` (generated from its output) checks every one.

/// Every struct's size, alignment and field offsets, against the C compiler's own numbers.
#[test]
fn c_layout_matches() {
    oscairn_clap_sys::assert_c_layout();
}

/// The Rust-side ids must agree with the `static const char[]` in the headers (bindgen
/// surfaces them as byte arrays).
#[test]
fn extension_ids_match_the_headers() {
    fn header(c: &[u8]) -> &str {
        std::str::from_utf8(&c[..c.len() - 1]).unwrap() // drop the NUL
    }
    let pairs: [(&str, &[u8]); 17] = [
        (oscairn_clap_sys::ids::OSCAIRN_EXT_UI, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_UI)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_UI_DRAFT_4, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_UI_DRAFT_4)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_UI_DRAFT_3, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_UI_DRAFT_3)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_VOICE_BUDGET, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_VOICE_BUDGET)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_SOUND_CONTEXTS, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_SOUND_CONTEXTS)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_SOUND_SNAPSHOTS, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_SOUND_SNAPSHOTS)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_ASSETS, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_ASSETS)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_ASSETS_DRAFT_2, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_ASSETS_DRAFT_2)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_ASSETS_DRAFT_1, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_ASSETS_DRAFT_1)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_ASSET_DEPENDENCIES, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_ASSET_DEPENDENCIES)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_ASSET_DEPENDENCIES_DRAFT_1, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_ASSET_DEPENDENCIES_DRAFT_1)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_MEMORY, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_MEMORY)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_EFFECT_CONTEXTS, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_EFFECT_CONTEXTS)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_PARAM_TARGET_LANES, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_PARAM_TARGET_LANES)),
        (oscairn_clap_sys::ids::OSCAIRN_EXT_HARMONY, bytemock(&oscairn_clap_sys::OSCAIRN_EXT_HARMONY)),
        (oscairn_clap_sys::ids::OSCAIRN_EVENT_SPACE_BEAT, bytemock(&oscairn_clap_sys::OSCAIRN_EVENT_SPACE_BEAT)),
        (oscairn_clap_sys::ids::OSCAIRN_EVENT_SPACE_HARMONY, bytemock(&oscairn_clap_sys::OSCAIRN_EVENT_SPACE_HARMONY)),
    ];
    for (rust, c) in pairs {
        assert_eq!(rust, header(c));
    }
    assert_eq!(
        oscairn_clap_sys::ids::OSCAIRN_EVENT_SPACE_PARAM_TARGET,
        header(bytemock(&oscairn_clap_sys::OSCAIRN_EVENT_SPACE_PARAM_TARGET))
    );
}

fn bytemock<const N: usize>(a: &[u8; N]) -> &[u8] {
    a
}

/// The values the enums must carry (each `Type` from its constified module).
#[test]
fn enum_values_match_the_headers() {
    use oscairn_clap_sys::{oscairn_field as f, oscairn_role as r, oscairn_widget as w};
    assert_eq!((w::OSCAIRN_WIDGET_AUTO, w::OSCAIRN_WIDGET_RANGE), (0, 6));
    assert_eq!((r::OSCAIRN_ROLE_NONE, r::OSCAIRN_ROLE_CUTOFF, r::OSCAIRN_ROLE_OUTPUT_BUS), (0, 1, 35));
    assert_eq!((f::OSCAIRN_FIELD_ATTACK, f::OSCAIRN_FIELD_LEVEL_0, f::OSCAIRN_FIELD_TIME_1, f::OSCAIRN_FIELD_FIXED), (2, 16, 32, 102));
    assert_eq!((oscairn_clap_sys::OSCAIRN_VOICE_BUDGET_MONO, oscairn_clap_sys::OSCAIRN_VOICE_BUDGET_LEGATO), (1, 2));
    assert_eq!(oscairn_clap_sys::OSCAIRN_VOICE_BUDGET_SELECTED, u32::MAX);
    assert_eq!(oscairn_clap_sys::OSCAIRN_VOICE_BUDGET_MAX_CONTEXTS, 16);
    assert_eq!(oscairn_clap_sys::oscairn_chord_type::OSCAIRN_CHORD_MINOR9, 22);
    assert_eq!(oscairn_clap_sys::OSCAIRN_EVENT_BEAT, 0);
    assert_eq!(oscairn_clap_sys::OSCAIRN_EVENT_HARMONY, 0);
    assert_eq!(oscairn_clap_sys::OSCAIRN_EVENT_PARAM_TARGET, 0);
}

/// `clap-sys` and the generated bindings must agree on the CLAP types the extensions embed,
/// so a plugin can mix them (that is the point of the pinned CLAP 1.2.10 headers).
#[test]
fn clap_sys_types_agree() {
    use std::mem::{align_of, size_of};
    assert_eq!(size_of::<oscairn_clap_sys::clap_plugin_t>(), size_of::<clap_sys::plugin::clap_plugin>());
    assert_eq!(align_of::<oscairn_clap_sys::clap_plugin_t>(), align_of::<clap_sys::plugin::clap_plugin>());
    assert_eq!(size_of::<oscairn_clap_sys::clap_event_header_t>(), size_of::<clap_sys::events::clap_event_header>());
    assert_eq!(size_of::<oscairn_clap_sys::clap_istream_t>(), size_of::<clap_sys::stream::clap_istream>());
    assert_eq!(size_of::<oscairn_clap_sys::clap_ostream_t>(), size_of::<clap_sys::stream::clap_ostream>());
    assert_eq!(size_of::<oscairn_clap_sys::clap_audio_buffer_t>(), size_of::<clap_sys::audio_buffer::clap_audio_buffer>());
    assert_eq!(std::mem::offset_of!(oscairn_clap_sys::oscairn_plugin_sound_contexts_t, state), std::mem::offset_of!(oscairn_clap_sys::oscairn_plugin_sound_contexts_t, discard) + size_of::<usize>());
}
