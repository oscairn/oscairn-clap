// SPDX-License-Identifier: MIT
//! Wrapper tests: the tables' C entry points are driven the way the host drives them (through
//! function pointers, a fake `clap_plugin` whose `plugin_data` points at the plugin struct),
//! and the values that come back are the Rust ones.

use oscairn_clap::ui::{Icon, ParamHint, Role, Tab, UiHints, Widget};
use oscairn_clap::voice_budget::{Context, Limits, Note, VoiceBudget};
use std::cell::{Cell, RefCell};

struct MyPlugin;

impl UiHints for MyPlugin {
    fn param_hint(&self, param_id: u32) -> Option<ParamHint> {
        if param_id > 2 {
            return None;
        }
        Some(if param_id == 1 {
            ParamHint { widget: Widget::Knob, priority: 3, range_high: None, group: 2, role: Role::OSCAIRN_ROLE_CUTOFF, view: 0, field: 0, index: 0 }
        } else {
            // A range low end naming its high end, and part of an envelope view.
            ParamHint {
                widget: Widget::Range,
                priority: 1,
                range_high: Some(9),
                group: 0,
                role: Role::OSCAIRN_ROLE_ATTACK,
                view: 4,
                field: oscairn_clap::ui::Field::OSCAIRN_FIELD_ATTACK,
                index: 2,
            }
        })
    }

    fn seconds(&self, param_id: u32, value: f64) -> Option<f64> {
        (param_id == 2).then_some(value * 2.0)
    }
}

/// A `clap_plugin` (`clap-sys`'s, which has no `Default`) whose `plugin_data` is `data`; the
/// other fields go unused by the tables, so they are zero: null pointers and `None` entries.
fn plugin_with_data(data: *mut std::ffi::c_void) -> oscairn_clap_sys::clap_sys::plugin::clap_plugin {
    let mut plugin: oscairn_clap_sys::clap_sys::plugin::clap_plugin = unsafe { std::mem::zeroed() };
    plugin.plugin_data = data;
    plugin
}

fn fake_plugin(p: &MyPlugin) -> oscairn_clap_sys::clap_sys::plugin::clap_plugin {
    plugin_with_data((p as *const MyPlugin).cast_mut().cast())
}

#[test]
fn ui_table_round_trips_hints_through_the_c_abi() {
    let me = MyPlugin;
    let table = oscairn_clap::ui_table!(MyPlugin);
    let plugin = fake_plugin(&me);

    let mut hint = oscairn_clap_sys::oscairn_param_hint_t::default();
    // The host pre-zeroes the hint; a false answer must leave it untouched.
    assert!(!unsafe { (table.param_hint.unwrap())(&plugin, 7, &mut hint) });
    assert_eq!(hint.widget, 0);
    assert_eq!(hint.priority, 0);

    assert!(unsafe { (table.param_hint.unwrap())(&plugin, 1, &mut hint) });
    assert_eq!(hint.widget, Widget::Knob as u32);
    assert_eq!(hint.priority, 3);
    assert_eq!(hint.range_high, u32::MAX, "no range high is CLAP_INVALID_ID");
    assert_eq!(hint.group, 2);
    assert_eq!(hint.role, Role::OSCAIRN_ROLE_CUTOFF);

    assert!(unsafe { (table.param_hint.unwrap())(&plugin, 2, &mut hint) });
    assert_eq!(hint.widget, Widget::Range as u32);
    assert_eq!(hint.range_high, 9);
    assert_eq!(hint.view, 4);
    assert_eq!(hint.index, 2);

    let mut seconds = -1.0f64;
    assert!(unsafe { (table.seconds.unwrap())(&plugin, 2, 1.5, &mut seconds) });
    assert_eq!(seconds, 3.0);
    assert!(!unsafe { (table.seconds.unwrap())(&plugin, 1, 1.5, &mut seconds) });

    // An FM-less plugin answers algorithm false.
    let mut alg = oscairn_clap_sys::oscairn_fm_algorithm_t::default();
    assert!(!unsafe { (table.algorithm.unwrap())(&plugin, 2, 1, &mut alg) });
}

/// Two pages, Operators with three tabs and Pitch; names at the 63-byte limit.
struct Tabbed;

fn tabbed_entries() -> Vec<Tab> {
    let tab = |id, parent, name: &str| Tab { id, parent, name: name.to_string() };
    vec![
        tab(1, 0, "Operators"),
        tab(2, 0, "Pitch"),
        tab(3, 1, &format!("{}\u{e9}", "a".repeat(61))), // exactly 63 bytes: kept whole
        tab(4, 1, &format!("{}\u{e9}", "b".repeat(62))), // 64 bytes: the two-byte letter would straddle byte 63
        tab(5, 1, "Op 3"),
        tab(0, 1, "Refused"),
    ]
}

impl UiHints for Tabbed {
    fn param_hint(&self, _param_id: u32) -> Option<ParamHint> {
        None
    }
    const HAS_TABS: bool = true;
    fn tab_count(&self) -> u32 {
        tabbed_entries().len() as u32
    }
    fn tab(&self, index: u32) -> Option<Tab> {
        tabbed_entries().get(index as usize).cloned()
    }
    fn param_tab(&self, param_id: u32) -> Option<u32> {
        match param_id {
            10 => Some(3),
            11 => Some(1), // on the page itself
            12 => Some(0), // 0 is no id
            _ => None,     // unplaced
        }
    }
    const HAS_VALUE_ICONS: bool = true;
    fn value_icon(&self, param_id: u32, value: f64) -> Option<Icon> {
        match (param_id, value as i32) {
            (20, -1) => Some(Icon::Triangle),
            (20, 0) => Some(Icon::Sine),
            (20, 1) => Some(Icon::SawDown),
            (20, 2) => Some(Icon::SampleAndHold),
            _ => None,
        }
    }
}

fn name_of(entry: &oscairn_clap_sys::oscairn_ui_tab_t) -> String {
    let bytes: Vec<u8> = entry.name.iter().take_while(|&&c| c != 0).map(|&c| c as u8).collect();
    String::from_utf8(bytes).expect("a name cut on a character boundary is UTF-8")
}

#[test]
fn ui_table_round_trips_pages_tabs_and_icons() {
    let me = Tabbed;
    let table = oscairn_clap::ui_table!(Tabbed);
    let plugin = plugin_with_data((&me as *const Tabbed).cast_mut().cast());
    let count = unsafe { (table.tab_count.unwrap())(&plugin) };
    assert_eq!(count, 6);

    let mut entry = oscairn_clap_sys::oscairn_ui_tab_t::default();
    let expected = tabbed_entries();
    for (i, want) in expected.iter().enumerate().take(5) {
        assert!(unsafe { (table.tab.unwrap())(&plugin, i as u32, &mut entry) });
        assert_eq!((entry.id, entry.parent), (want.id, want.parent));
        let name = name_of(&entry);
        if i == 3 {
            assert_eq!(name, "b".repeat(62), "cut before the character that would pass 63 bytes");
        } else {
            assert_eq!(name, want.name);
        }
        assert_eq!(entry.name[63], 0, "the terminating NUL");
    }
    assert_eq!(name_of(&{ unsafe { (table.tab.unwrap())(&plugin, 2, &mut entry) }; entry }).len(), 63);
    assert!(!unsafe { (table.tab.unwrap())(&plugin, 5, &mut entry) }, "id 0 is refused");
    assert!(!unsafe { (table.tab.unwrap())(&plugin, 6, &mut entry) }, "past the end");

    let mut id = 99u32;
    assert!(unsafe { (table.param_tab.unwrap())(&plugin, 10, &mut id) });
    assert_eq!(id, 3);
    assert!(unsafe { (table.param_tab.unwrap())(&plugin, 11, &mut id) });
    assert_eq!(id, 1);
    id = 99;
    assert!(!unsafe { (table.param_tab.unwrap())(&plugin, 12, &mut id) }, "id 0 is no tab");
    assert!(!unsafe { (table.param_tab.unwrap())(&plugin, 13, &mut id) }, "unplaced");
    assert_eq!(id, 99);

    let mut icon = 99u32;
    let ask = |value: f64, icon: &mut u32| unsafe { (table.value_icon.unwrap())(&plugin, 20, value, icon) };
    assert!(ask(-1.0, &mut icon));
    assert_eq!(icon, Icon::Triangle as u32);
    assert!(ask(1.0, &mut icon));
    assert_eq!(icon, oscairn_clap_sys::oscairn_icon::OSCAIRN_ICON_SAW_DOWN);
    assert!(ask(2.0, &mut icon));
    assert_eq!(icon, oscairn_clap_sys::oscairn_icon::OSCAIRN_ICON_SAMPLE_AND_HOLD);
    icon = 99;
    assert!(!ask(3.0, &mut icon));
    assert_eq!(icon, 99);
}

#[test]
fn a_plugin_implementing_no_draft_6_member_has_them_null() {
    let table = oscairn_clap::ui_table!(MyPlugin);
    assert!(table.param_hint.is_some());
    assert!(table.tab_count.is_none() && table.tab.is_none() && table.param_tab.is_none() && table.value_icon.is_none());
}

struct Budget {
    limit: Cell<u32>,
    contexts: RefCell<Vec<Context>>,
    voices: Cell<u32>,
}

impl VoiceBudget for Budget {
    fn honors_volume(&self) -> bool {
        true
    }
    fn set_limits(&self, limit: u32, contexts: &[Context]) {
        self.limit.set(limit);
        *self.contexts.borrow_mut() = contexts.to_vec();
    }
    fn active_voices(&self, voice_cost: &mut f64) -> u32 {
        *voice_cost = 12.5;
        self.voices.get()
    }
    fn notes(&self, emit: &mut dyn FnMut(&Note) -> bool) {
        for i in 0..5 {
            let n = Note { note_id: i, port_index: 0, channel: 1, key: 60 + i as i16, voices: 2, loudness: 0.5 };
            if !emit(&n) {
                return;
            }
        }
    }
}

#[test]
fn voice_budget_table_round_trips_the_limits_and_report() {
    let me = Budget { limit: Cell::new(0), contexts: RefCell::new(Vec::new()), voices: Cell::new(3) };
    let table = oscairn_clap::voice_budget_table!(Budget);
    let plugin = plugin_with_data((&me as *const Budget).cast_mut().cast());

    assert!(unsafe { (table.honors_volume.unwrap())(&plugin) });

    let contexts = [
        oscairn_clap_sys::oscairn_voice_budget_context_t {
            context: oscairn_clap_sys::OSCAIRN_VOICE_BUDGET_SELECTED,
            limit: 0,
            reserve: 4,
            priority: 7,
            flags: oscairn_clap_sys::OSCAIRN_VOICE_BUDGET_MONO as u8,
        },
        oscairn_clap_sys::oscairn_voice_budget_context_t { context: 9, limit: 2, reserve: 0, priority: 1, flags: 0 },
    ];
    unsafe { (table.set_limits.unwrap())(&plugin, 8, contexts.as_ptr(), 2) };
    assert_eq!(me.limit.get(), 8);
    let stored = me.contexts.borrow();
    assert_eq!(stored.len(), 2);
    assert_eq!(stored[0].reserve, 4);
    assert_eq!(stored[0].priority, 7);
    assert!(stored[0].mono && !stored[0].legato);
    assert_eq!(stored[1].context, 9);

    let mut cost = 0.0f64;
    assert_eq!(unsafe { (table.active_voices.unwrap())(&plugin, &mut cost) }, 3);
    assert_eq!(cost, 12.5);

    let mut notes = [oscairn_clap_sys::oscairn_voice_budget_note_t::default(); 10];
    let n = unsafe { (table.notes.unwrap())(&plugin, notes.as_mut_ptr(), 3) };
    assert_eq!(n, 3, "capped at capacity");
    assert_eq!(notes[0].note_id, 0);
    assert_eq!(notes[2].key, 62);
    assert_eq!(notes[1].voices, 2);
    assert_eq!(notes[0].loudness, 0.5);
    assert_eq!(notes[3].voices, 0, "nothing written past capacity");
    let n = unsafe { (table.notes.unwrap())(&plugin, notes.as_mut_ptr(), 10) };
    assert_eq!(n, 5, "all five with room");
}

#[test]
fn limits_lookup_follows_the_selected_fallback() {
    let mut limits = Limits::default();
    let selected =
        Context { context: oscairn_clap_sys::OSCAIRN_VOICE_BUDGET_SELECTED, limit: 0, reserve: 4, priority: 7, mono: false, legato: true };
    let named = Context { context: 9, limit: 2, reserve: 0, priority: 1, mono: false, legato: false };
    limits.set(8, &[selected, named]);
    assert_eq!(limits.limit, 8);
    // A named context reads its own entry.
    assert_eq!(limits.of(9, false).limit, 2);
    // Any context a MIDI channel selects falls back to the SELECTED entry (RFC 0018 / the
    // header's `Limits::of(0, true)` rule); an unnamed non-selected one gets defaults.
    assert_eq!(limits.of(5, true).reserve, 4);
    assert!(limits.of(5, true).legato);
    assert_eq!(limits.of(5, false).priority, 0);
    assert_eq!(limits.of(5, false).reserve, 0);
}
