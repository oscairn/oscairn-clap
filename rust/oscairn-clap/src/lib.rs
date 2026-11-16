// SPDX-License-Identifier: MIT
//! Thin safe wrappers over the raw [`oscairn_clap_sys`] bindings, for the parts a plugin
//! implements: `oscairn.ui` (parameter hints, roles and rich views), `oscairn.voice-budget`
//! (the host's limits and the note report), and the extension ID constants.
//!
//! Implement [`ui::UiHints`] and/or [`voice_budget::VoiceBudget`] on your plugin struct, put
//! `ui_table!(YourPlugin)` / `voice_budget_table!(YourPlugin)` in a `static`, and return a
//! reference to it from `get_extension`. The `plugin_data` of every `clap_plugin_t` you
//! create must point at your struct, which is what the tables' trampolines cast it to.
//!
//! Everything raw stays here, in a few marked casts; the rest is plain Rust values.

pub use oscairn_clap_sys::ids;

use oscairn_clap_sys::clap_sys::plugin::clap_plugin as clap_plugin_t;

/// The plugin behind a `clap_plugin_t` the host hands back.
///
/// Safety: `plugin` is a live `clap_plugin_t` this plugin created, and its `plugin_data`
/// points at a live `P` (the contract of the table macros).
unsafe fn plugin_at<'a, P>(plugin: *const clap_plugin_t) -> &'a P {
    // The one cast: plugin_data is set to `*mut P` by the plugin's own constructor.
    unsafe { &*(*plugin).plugin_data.cast::<P>() }
}

/// Types the exported table macros name in expanded plugin code.
#[doc(hidden)]
pub mod reexport {
    pub use oscairn_clap_sys::{oscairn_plugin_ui_t, oscairn_plugin_voice_budget_t};
}

/// `oscairn.ui`: how each parameter is best shown and played.
pub mod ui {
    use crate::plugin_at;
    use oscairn_clap_sys::clap_sys::plugin::clap_plugin as clap_plugin_t;
    use oscairn_clap_sys::{oscairn_fm_algorithm_t, oscairn_icon, oscairn_param_hint_t, oscairn_ui_tab_t, oscairn_widget};

    /// The control a parameter asks for (`enum oscairn_widget`).
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    #[repr(u32)]
    pub enum Widget {
        Auto = oscairn_widget::OSCAIRN_WIDGET_AUTO,
        Slider = oscairn_widget::OSCAIRN_WIDGET_SLIDER,
        Knob = oscairn_widget::OSCAIRN_WIDGET_KNOB,
        Buttons = oscairn_widget::OSCAIRN_WIDGET_BUTTONS,
        Switch = oscairn_widget::OSCAIRN_WIDGET_SWITCH,
        List = oscairn_widget::OSCAIRN_WIDGET_LIST,
        Range = oscairn_widget::OSCAIRN_WIDGET_RANGE,
    }

    /// A parameter's shared role, `enum oscairn_role` values as `Role::OSCAIRN_ROLE_*`.
    pub use oscairn_clap_sys::oscairn_role as Role;

    /// A parameter's place in a rich view, `enum oscairn_field` values as `Field::OSCAIRN_FIELD_*`.
    pub use oscairn_clap_sys::oscairn_field as Field;

    /// An icon a stepped parameter's value may name (`enum oscairn_icon`), drawn by the host in
    /// its own style. `None` where a function returns `Option<Icon>` is `OSCAIRN_ICON_NONE`.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    #[repr(u32)]
    pub enum Icon {
        Sine = oscairn_icon::OSCAIRN_ICON_SINE,
        Triangle = oscairn_icon::OSCAIRN_ICON_TRIANGLE,
        Saw = oscairn_icon::OSCAIRN_ICON_SAW,
        SawDown = oscairn_icon::OSCAIRN_ICON_SAW_DOWN,
        Square = oscairn_icon::OSCAIRN_ICON_SQUARE,
        Pulse = oscairn_icon::OSCAIRN_ICON_PULSE,
        Noise = oscairn_icon::OSCAIRN_ICON_NOISE,
        SampleAndHold = oscairn_icon::OSCAIRN_ICON_SAMPLE_AND_HOLD,
    }

    /// One page or tab of the editor (`oscairn_ui_tab_t`). `id` is the plugin's own and must be
    /// nonzero, unique among all the entries and stable across rescans; `parent` is 0 for a page,
    /// else the id of a page (two levels). `name` is the display name: it is written into the
    /// 64-byte field as UTF-8, cut on a character boundary to at most 63 bytes, and should not be
    /// empty. The wrapper refuses only an id of 0; the host leaves out a repeated id, an empty name
    /// and a parent that is not a page's id, as the header says.
    #[derive(Debug, Clone, PartialEq, Eq)]
    pub struct Tab {
        pub id: u32,
        pub parent: u32,
        pub name: String,
    }

    /// One parameter's hint, in Rust types. `range_high` is the high end of a `Widget::Range`;
    /// `None` for every other widget.
    #[derive(Debug, Clone, Copy)]
    pub struct ParamHint {
        pub widget: Widget,
        /// 1 is the most important; 0 is none, after every numbered one.
        pub priority: u32,
        pub range_high: Option<u32>,
        /// Parameters of one module with the same nonzero group are drawn joined.
        pub group: u32,
        pub role: Role::Type,
        /// The rich view this parameter is part of; 0 for none.
        pub view: u32,
        pub field: Field::Type,
        /// Whose field it is: an operator's, 1 to 8; an element's or a zone's, 1 to 32 (a draft 5
        /// host draws only 1 to 8 and ignores a larger one); 0 for the view's own. Not checked here.
        pub index: u32,
    }

    impl Default for ParamHint {
        fn default() -> Self {
            ParamHint {
                widget: Widget::Auto,
                priority: 0,
                range_high: None,
                group: 0,
                role: Role::OSCAIRN_ROLE_NONE,
                view: 0,
                field: Field::OSCAIRN_FIELD_NONE,
                index: 0,
            }
        }
    }

    /// One FM algorithm's routing, for a view whose field is `Field::ALGORITHM`.
    #[derive(Debug, Clone, Copy, Default)]
    pub struct FmAlgorithm {
        /// The operators modulating operator n + 1; bit k is operator k + 1.
        pub inputs: [u8; 8],
        pub carriers: u8,
        pub feedback_from: u8,
        pub feedback_to: u8,
    }

    /// What a plugin answers for its parameters; all on the main thread, as the header says.
    pub trait UiHints {
        /// Fill the hint for `param_id`; `None` when it has none.
        fn param_hint(&self, param_id: u32) -> Option<ParamHint>;

        /// The routing of the algorithm parameter `param_id` at `value`; `None` for none.
        fn algorithm(&self, _param_id: u32, _value: u32) -> Option<FmAlgorithm> {
            None
        }

        /// How long the envelope stage of the time/rate parameter `param_id` takes at
        /// `value`; `None` for none.
        fn seconds(&self, _param_id: u32, _value: f64) -> Option<f64> {
            None
        }

        /// Set to `true` when `tab_count`, `tab` and `param_tab` are implemented: `ui_table!` then
        /// fills those three members, else they are null (a plugin written before draft 6 is
        /// unchanged).
        const HAS_TABS: bool = false;

        /// The number of entries `tab` has, pages and tabs together; 0 declares none.
        fn tab_count(&self) -> u32 {
            0
        }

        /// Entry `index` of the pages and tabs, in the order the host draws them; `None` for none.
        /// An entry with id 0 is left out.
        fn tab(&self, _index: u32) -> Option<Tab> {
            None
        }

        /// The id of the page or tab the parameter `param_id` is on; `None` leaves it to its
        /// module path. Id 0 is no id.
        fn param_tab(&self, _param_id: u32) -> Option<u32> {
            None
        }

        /// Set to `true` when `value_icon` is implemented: `ui_table!` then fills that member,
        /// else it is null.
        const HAS_VALUE_ICONS: bool = false;

        /// The icon of the stepped parameter `param_id` at `value`, its plain value (the number
        /// `value_to_text` takes, which may be negative); `None` for no icon.
        fn value_icon(&self, _param_id: u32, _value: f64) -> Option<Icon> {
            None
        }
    }

    // The trampolines are public only for `ui_table!`. Safety, for each: `plugin` is as
    // `plugin_at` needs, and the out pointer is writable, as the header's contract says.

    #[doc(hidden)]
    pub unsafe extern "C" fn trampoline_param_hint<P: UiHints>(
        plugin: *const clap_plugin_t,
        param_id: u32,
        hint: *mut oscairn_param_hint_t,
    ) -> bool {
        let me = unsafe { plugin_at::<P>(plugin) };
        let Some(h) = me.param_hint(param_id) else { return false };
        unsafe {
            *hint = oscairn_param_hint_t {
                widget: h.widget as u32,
                priority: h.priority,
                range_high: h.range_high.unwrap_or(u32::MAX),
                group: h.group,
                role: h.role,
                view: h.view,
                field: h.field,
                index: h.index,
            }
        };
        true
    }

    #[doc(hidden)]
    pub unsafe extern "C" fn trampoline_algorithm<P: UiHints>(
        plugin: *const clap_plugin_t,
        param_id: u32,
        value: u32,
        algorithm: *mut oscairn_fm_algorithm_t,
    ) -> bool {
        let me = unsafe { plugin_at::<P>(plugin) };
        let Some(a) = me.algorithm(param_id, value) else { return false };
        unsafe {
            *algorithm = oscairn_fm_algorithm_t {
                inputs: a.inputs,
                carriers: a.carriers,
                feedback_from: a.feedback_from,
                feedback_to: a.feedback_to,
            }
        };
        true
    }

    #[doc(hidden)]
    pub unsafe extern "C" fn trampoline_seconds<P: UiHints>(
        plugin: *const clap_plugin_t,
        param_id: u32,
        value: f64,
        seconds: *mut f64,
    ) -> bool {
        let me = unsafe { plugin_at::<P>(plugin) };
        let Some(t) = me.seconds(param_id, value) else { return false };
        unsafe { *seconds = t };
        true
    }

    #[doc(hidden)]
    pub unsafe extern "C" fn trampoline_tab_count<P: UiHints>(plugin: *const clap_plugin_t) -> u32 {
        let me = unsafe { plugin_at::<P>(plugin) };
        me.tab_count()
    }

    #[doc(hidden)]
    pub unsafe extern "C" fn trampoline_tab<P: UiHints>(plugin: *const clap_plugin_t, index: u32, tab: *mut oscairn_ui_tab_t) -> bool {
        let me = unsafe { plugin_at::<P>(plugin) };
        let Some(t) = me.tab(index) else { return false };
        if t.id == 0 {
            return false;
        }
        let mut entry = oscairn_ui_tab_t { id: t.id, parent: t.parent, name: [0; 64] };
        // At most 63 bytes, ending on a character boundary, then the NUL the zeros already give.
        let mut len = t.name.len().min(63);
        while !t.name.is_char_boundary(len) {
            len -= 1;
        }
        for (dst, src) in entry.name.iter_mut().zip(&t.name.as_bytes()[..len]) {
            *dst = *src as ::std::os::raw::c_char;
        }
        unsafe { *tab = entry };
        true
    }

    #[doc(hidden)]
    pub unsafe extern "C" fn trampoline_param_tab<P: UiHints>(plugin: *const clap_plugin_t, param_id: u32, tab_id: *mut u32) -> bool {
        let me = unsafe { plugin_at::<P>(plugin) };
        let Some(id) = me.param_tab(param_id).filter(|&id| id != 0) else { return false };
        unsafe { *tab_id = id };
        true
    }

    #[doc(hidden)]
    pub unsafe extern "C" fn trampoline_value_icon<P: UiHints>(
        plugin: *const clap_plugin_t,
        param_id: u32,
        value: f64,
        icon: *mut u32,
    ) -> bool {
        let me = unsafe { plugin_at::<P>(plugin) };
        let Some(i) = me.value_icon(param_id, value) else { return false };
        unsafe { *icon = i as u32 };
        true
    }

    /// The `oscairn_plugin_ui_t` table for `P`: put it in a `static` and return a reference to it
    /// from `get_extension` when asked for `ids::OSCAIRN_EXT_UI` (a draft-6 table answers the
    /// older draft IDs too, whose members are a prefix).
    ///
    /// Safety contract: every `clap_plugin_t` handed to this table's functions must have
    /// `plugin_data` pointing at a live `P`. The draft 6 members are null unless `P` sets
    /// `HAS_TABS` or `HAS_VALUE_ICONS`.
    #[macro_export]
    macro_rules! ui_table {
        ($P:ty) => {
            $crate::reexport::oscairn_plugin_ui_t {
                param_hint: Some($crate::ui::trampoline_param_hint::<$P>),
                algorithm: Some($crate::ui::trampoline_algorithm::<$P>),
                seconds: Some($crate::ui::trampoline_seconds::<$P>),
                tab_count: if <$P as $crate::ui::UiHints>::HAS_TABS { Some($crate::ui::trampoline_tab_count::<$P>) } else { None },
                tab: if <$P as $crate::ui::UiHints>::HAS_TABS { Some($crate::ui::trampoline_tab::<$P>) } else { None },
                param_tab: if <$P as $crate::ui::UiHints>::HAS_TABS { Some($crate::ui::trampoline_param_tab::<$P>) } else { None },
                value_icon: if <$P as $crate::ui::UiHints>::HAS_VALUE_ICONS { Some($crate::ui::trampoline_value_icon::<$P>) } else { None },
            }
        };
    }
}

/// `oscairn.voice-budget`: the host's voice limits, and the plugin's voice and note reports.
pub mod voice_budget {
    use crate::plugin_at;
    use oscairn_clap_sys::clap_sys::plugin::clap_plugin as clap_plugin_t;
    use oscairn_clap_sys::{
        oscairn_voice_budget_context_t, oscairn_voice_budget_note_t, OSCAIRN_VOICE_BUDGET_LEGATO, OSCAIRN_VOICE_BUDGET_MAX_CONTEXTS,
        OSCAIRN_VOICE_BUDGET_MONO, OSCAIRN_VOICE_BUDGET_SELECTED,
    };

    /// One context's settings as the host sent them.
    #[derive(Debug, Clone, Copy, Default)]
    pub struct Context {
        pub context: u32,
        /// Voices it may keep; 0 for no limit of its own.
        pub limit: u32,
        /// Voices never given up for another context's.
        pub reserve: u32,
        /// 0 to 10; a context of lower priority gives way first.
        pub priority: u8,
        pub mono: bool,
        pub legato: bool,
    }

    const MAX_CONTEXTS: usize = OSCAIRN_VOICE_BUDGET_MAX_CONTEXTS as usize;

    /// What `set_limits` gave the plugin, copied for the audio thread (the host's array is only
    /// valid during the call); fixed-size, so copying never allocates. An instrument without
    /// sound contexts reads `of(0, true)`.
    #[derive(Debug, Clone, Default)]
    pub struct Limits {
        /// Instance limit: keep at most this many voices (0: as many as the pool holds).
        pub limit: u32,
        contexts: [Context; MAX_CONTEXTS],
        count: usize,
    }

    impl Limits {
        /// Copy what the host passed at `set_limits`, at most `OSCAIRN_VOICE_BUDGET_MAX_CONTEXTS`.
        pub fn set(&mut self, limit: u32, contexts: &[Context]) {
            self.limit = limit;
            self.count = contexts.len().min(MAX_CONTEXTS);
            self.contexts[..self.count].copy_from_slice(&contexts[..self.count]);
        }

        /// A context's settings: its own entry; else, for one a MIDI channel selects, the entry
        /// for `OSCAIRN_VOICE_BUDGET_SELECTED`; else none. As voice-budget-plugin.hpp's.
        pub fn of(&self, context: u32, selected: bool) -> Context {
            let contexts = &self.contexts[..self.count];
            let own = contexts.iter().find(|c| c.context == context && context != OSCAIRN_VOICE_BUDGET_SELECTED);
            let shared = || contexts.iter().rfind(|c| c.context == OSCAIRN_VOICE_BUDGET_SELECTED).filter(|_| selected);
            own.or_else(shared).copied().unwrap_or(Context { context, ..Context::default() })
        }
    }

    /// One note still sounding, for the report the host compares across plugins.
    #[derive(Debug, Clone, Copy)]
    pub struct Note {
        pub note_id: i32,
        pub port_index: i16,
        pub channel: i16,
        pub key: i16,
        pub voices: u32,
        /// The largest of its voices' gains now, linear, 1 at full level.
        pub loudness: f32,
    }

    /// What a plugin answers; threads as the header says (main for [`VoiceBudget::honors_volume`],
    /// audio for the rest, none during `process`). All `&self`: the host may reach the plugin
    /// from the main thread while `process` runs, so keep the mutable parts behind interior
    /// mutability (`Cell`, `AtomicU32`) in your struct.
    pub trait VoiceBudget {
        /// Whether every voice follows `CLAP_NOTE_EXPRESSION_VOLUME`.
        fn honors_volume(&self) -> bool;
        /// The limits to honor from the next block on; copy what you need (the slice is only
        /// valid during the call).
        fn set_limits(&self, limit: u32, contexts: &[Context]);
        /// Voices sounding now (fading ones included), and your estimate of one voice's cost
        /// in microseconds per block at the current block size (0: none).
        fn active_voices(&self, voice_cost: &mut f64) -> u32;
        /// Report each note still sounding through `emit`; stop when it returns false.
        fn notes(&self, emit: &mut dyn FnMut(&Note) -> bool);
    }

    // The trampolines are public only for `voice_budget_table!`. Safety, for each: `plugin` is
    // as `plugin_at` needs, and the pointers are as the header's contract says.

    #[doc(hidden)]
    pub unsafe extern "C" fn trampoline_honors_volume<P: VoiceBudget>(plugin: *const clap_plugin_t) -> bool {
        unsafe { plugin_at::<P>(plugin) }.honors_volume()
    }

    #[doc(hidden)]
    pub unsafe extern "C" fn trampoline_set_limits<P: VoiceBudget>(
        plugin: *const clap_plugin_t,
        limit: u32,
        contexts: *const oscairn_voice_budget_context_t,
        count: u32,
    ) {
        let me = unsafe { plugin_at::<P>(plugin) };
        // Copied to the stack: this runs on the audio thread, and the host reuses its array.
        let mut copied = [Context::default(); MAX_CONTEXTS];
        let n = if contexts.is_null() { 0 } else { (count as usize).min(MAX_CONTEXTS) };
        // Safety: the host passes `count` valid entries, or null with 0.
        let from = if n == 0 { &[][..] } else { unsafe { std::slice::from_raw_parts(contexts, n) } };
        for (to, c) in copied.iter_mut().zip(from) {
            *to = Context {
                context: c.context,
                limit: c.limit,
                reserve: c.reserve,
                priority: c.priority,
                mono: u32::from(c.flags) & OSCAIRN_VOICE_BUDGET_MONO != 0,
                legato: u32::from(c.flags) & OSCAIRN_VOICE_BUDGET_LEGATO != 0,
            };
        }
        me.set_limits(limit, &copied[..n]);
    }

    #[doc(hidden)]
    pub unsafe extern "C" fn trampoline_active_voices<P: VoiceBudget>(plugin: *const clap_plugin_t, voice_cost: *mut f64) -> u32 {
        let me = unsafe { plugin_at::<P>(plugin) };
        let mut cost = 0.0f64;
        let n = me.active_voices(&mut cost);
        unsafe { *voice_cost = cost };
        n
    }

    #[doc(hidden)]
    pub unsafe extern "C" fn trampoline_notes<P: VoiceBudget>(
        plugin: *const clap_plugin_t,
        notes: *mut oscairn_voice_budget_note_t,
        capacity: u32,
    ) -> u32 {
        let me = unsafe { plugin_at::<P>(plugin) };
        let mut written = 0u32;
        // Safety: the host gives room for `capacity` entries; we write at most that many.
        unsafe {
            me.notes(&mut |note| {
                if written >= capacity {
                    return false;
                }
                *notes.add(written as usize) = oscairn_voice_budget_note_t {
                    note_id: note.note_id,
                    port_index: note.port_index,
                    channel: note.channel,
                    key: note.key,
                    voices: note.voices,
                    loudness: note.loudness,
                };
                written += 1;
                true
            });
        }
        written
    }

    /// The `oscairn_plugin_voice_budget_t` table for `P`; put it in a `static` and return a
    /// reference to it from `get_extension` when asked for `ids::OSCAIRN_EXT_VOICE_BUDGET`. No
    /// null entries: the host refuses a table with one.
    ///
    /// Safety contract: as in [`crate::ui_table`].
    #[macro_export]
    macro_rules! voice_budget_table {
        ($P:ty) => {
            $crate::reexport::oscairn_plugin_voice_budget_t {
                honors_volume: Some($crate::voice_budget::trampoline_honors_volume::<$P>),
                set_limits: Some($crate::voice_budget::trampoline_set_limits::<$P>),
                active_voices: Some($crate::voice_budget::trampoline_active_voices::<$P>),
                notes: Some($crate::voice_budget::trampoline_notes::<$P>),
            }
        };
    }
}
