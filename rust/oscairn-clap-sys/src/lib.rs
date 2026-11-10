// SPDX-License-Identifier: MIT
//! Raw FFI bindings for every header in the repository's `include/oscairn-clap/`, generated
//! at build time with bindgen against the vendored CLAP 1.2.10 headers (`rust/clap-headers`,
//! the version Oscairn pins; the `clap-sys` crate ships 1.2.2, too old for
//! `clap_plugin_state`, so bindings are generated from the headers and cross-checked
//! against `clap-sys` in the layout tests instead of reusing its types).
//!
//! `ids` holds the extension ID strings as constants; the generated module keeps bindgen's
//! own statics from the headers, and `tests/layout.rs` asserts the two agree and that every
//! struct's size, alignment and field offsets match what the C compiler reports.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]
#![allow(clippy::all)]

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

include!(concat!(env!("OUT_DIR"), "/c_layout.rs"));

/// Extension, event-space and draft ID strings, as constants (the headers keep them as
/// `static const char[]`; `tests/layout.rs` checks these against the values bindgen read
/// from the C side).
pub mod ids {
    pub const OSCAIRN_EXT_UI: &str = "oscairn.ui/draft-5";
    pub const OSCAIRN_EXT_UI_DRAFT_4: &str = "oscairn.ui/draft-4";
    pub const OSCAIRN_EXT_UI_DRAFT_3: &str = "oscairn.ui/draft-3";

    pub const OSCAIRN_EXT_VOICE_BUDGET: &str = "oscairn.voice-budget/draft-1";

    pub const OSCAIRN_EXT_SOUND_CONTEXTS: &str = "oscairn.sound-contexts/draft-5";
    pub const OSCAIRN_EXT_SOUND_SNAPSHOTS: &str = "oscairn.sound-snapshots/draft-4";

    pub const OSCAIRN_EXT_ASSETS: &str = "oscairn.assets/draft-3";
    pub const OSCAIRN_EXT_ASSETS_DRAFT_2: &str = "oscairn.assets/draft-2";
    pub const OSCAIRN_EXT_ASSETS_DRAFT_1: &str = "oscairn.assets/draft-1";
    pub const OSCAIRN_EXT_ASSET_DEPENDENCIES: &str = "oscairn.asset-dependencies/draft-2";
    pub const OSCAIRN_EXT_ASSET_DEPENDENCIES_DRAFT_1: &str = "oscairn.asset-dependencies/draft-1";

    pub const OSCAIRN_EXT_MEMORY: &str = "oscairn.memory/draft";

    pub const OSCAIRN_EXT_EFFECT_CONTEXTS: &str = "oscairn.effect-contexts/draft";

    pub const OSCAIRN_EXT_PARAM_TARGET_LANES: &str = "oscairn.param-target-lanes/draft";
    pub const OSCAIRN_EXT_HARMONY: &str = "oscairn.harmony/draft";

    pub const OSCAIRN_EVENT_SPACE_BEAT: &str = "oscairn.beat";
    pub const OSCAIRN_EVENT_SPACE_HARMONY: &str = "oscairn.harmony";
    pub const OSCAIRN_EVENT_SPACE_PARAM_TARGET: &str = "oscairn.param-target";
}
