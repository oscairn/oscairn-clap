// SPDX-License-Identifier: MIT
//! Drives the plugin the way a host does, through the exported `clap_entry`: factory, create,
//! init, a parameter flush, activate, a block with a note-on and a parameter event, then the
//! oscairn.ui table.

use ui_hints::clap_entry;
use clap_sys::events::{clap_event_header, clap_event_note, clap_event_param_value, clap_input_events, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, CLAP_EVENT_PARAM_VALUE};
use clap_sys::ext::params::{clap_plugin_params, CLAP_EXT_PARAMS};
use clap_sys::factory::plugin_factory::{clap_plugin_factory, CLAP_PLUGIN_FACTORY_ID};
use clap_sys::plugin::clap_plugin;
use clap_sys::process::clap_process;

extern "C" fn in_size(_l: *const clap_input_events) -> u32 {
    2
}

static NOTE: clap_event_note = clap_event_note {
    header: clap_event_header {
        size: std::mem::size_of::<clap_event_note>() as u32,
        time: 10,
        space_id: CLAP_CORE_EVENT_SPACE_ID,
        type_: CLAP_EVENT_NOTE_ON,
        flags: 0,
    },
    note_id: 1,
    port_index: 0,
    channel: 0,
    key: 69,
    velocity: 0.8,
};

static PARAM: clap_event_param_value = clap_event_param_value {
    header: clap_event_header {
        size: std::mem::size_of::<clap_event_param_value>() as u32,
        time: 0,
        space_id: CLAP_CORE_EVENT_SPACE_ID,
        type_: CLAP_EVENT_PARAM_VALUE,
        flags: 0,
    },
    param_id: 2, // Level
    cookie: std::ptr::null_mut(),
    note_id: -1,
    port_index: -1,
    channel: -1,
    key: -1,
    value: 1.0,
};

extern "C" fn in_get(_l: *const clap_input_events, index: u32) -> *const clap_event_header {
    match index {
        0 => std::ptr::addr_of!(PARAM).cast(),
        1 => std::ptr::addr_of!(NOTE).cast(),
        _ => std::ptr::null(),
    }
}

extern "C" fn flush_size(_l: *const clap_input_events) -> u32 {
    1
}

#[test]
fn plugin_makes_sound_and_answers_ui_hints() {
    unsafe {
        assert!((clap_entry.init.unwrap())(std::ptr::null()));
        let factory = (clap_entry.get_factory.unwrap())(CLAP_PLUGIN_FACTORY_ID.as_ptr()).cast::<clap_plugin_factory>();
        assert!(!factory.is_null());
        assert_eq!(((*factory).get_plugin_count.unwrap())(factory), 1);
        let id = c"org.oscairn.example.ui-hints";
        let plugin = ((*factory).create_plugin.unwrap())(factory, std::ptr::null(), id.as_ptr()) as *mut clap_plugin;
        assert!(!plugin.is_null());

        assert!(((*plugin).init.unwrap())(plugin));

        // Level (default 0.5) set through flush, as a host does while not processing.
        let params = ((*plugin).get_extension.unwrap())(plugin, CLAP_EXT_PARAMS.as_ptr()).cast::<clap_plugin_params>();
        let flush_events = clap_input_events { ctx: std::ptr::null_mut(), size: Some(flush_size), get: Some(in_get) };
        ((*params).flush.unwrap())(plugin, &flush_events, std::ptr::null());
        let mut level = 0.0;
        assert!(((*params).get_value.unwrap())(plugin, 2, &mut level));
        assert_eq!(level, 1.0);
        assert!(((*plugin).activate.unwrap())(plugin, 48000.0, 64, 512));
        assert!(((*plugin).start_processing.unwrap())(plugin));

        // One 256-frame block, one event at sample 0, the note at 10.
        let mut l = [0f32; 256];
        let mut r = [0f32; 256];
        let mut channels = [l.as_mut_ptr(), r.as_mut_ptr()];
        let in_events = clap_input_events { ctx: std::ptr::null_mut(), size: Some(in_size), get: Some(in_get) };
        let block = clap_process {
            steady_time: 0,
            frames_count: 256,
            transport: std::ptr::null(),
            audio_inputs: std::ptr::null(),
            audio_outputs: std::ptr::null_mut(),
            audio_inputs_count: 0,
            audio_outputs_count: 1,
            in_events: &in_events,
            out_events: std::ptr::null(),
        };
        let mut out = clap_sys::audio_buffer::clap_audio_buffer {
            data32: channels.as_mut_ptr(),
            data64: std::ptr::null_mut(),
            channel_count: 2,
            latency: 0,
            constant_mask: 0,
        };
        let mut block = block;
        block.audio_outputs = &mut out;
        let status = ((*plugin).process.unwrap())(plugin, &block);
        assert_eq!(status, clap_sys::process::CLAP_PROCESS_CONTINUE);
        let energy: f32 = l.iter().map(|x| x * x).sum();
        assert!(energy > 1e-4, "the block after a note-on has sound: energy {energy}");

        // oscairn.ui answers with the declared role for Cutoff.
        let ui_id = c"oscairn.ui/draft-5";
        let ui = ((*plugin).get_extension.unwrap())(plugin, ui_id.as_ptr()).cast::<oscairn_clap_sys::oscairn_plugin_ui_t>();
        assert!(!ui.is_null());
        let mut hint: oscairn_clap_sys::oscairn_param_hint_t = std::mem::zeroed();
        assert!(((*ui).param_hint.unwrap())(plugin.cast::<oscairn_clap_sys::clap_plugin_t>(), 1, &mut hint));
        assert_eq!(hint.widget, oscairn_clap_sys::oscairn_widget::OSCAIRN_WIDGET_KNOB);
        assert_eq!(hint.role, oscairn_clap_sys::oscairn_role::OSCAIRN_ROLE_CUTOFF);
        let mut seconds = -1.0;
        assert!(((*ui).seconds.unwrap())(plugin.cast::<oscairn_clap_sys::clap_plugin_t>(), 3, 0.5, &mut seconds)); // Attack: identity
        assert_eq!(seconds, 0.5);

        ((*plugin).stop_processing.unwrap())(plugin);
        ((*plugin).deactivate.unwrap())(plugin);
        ((*plugin).destroy.unwrap())(plugin);
        (clap_entry.deinit.unwrap())();
    }
}
