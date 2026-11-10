// SPDX-License-Identifier: MIT
//! `ui-hints`: a minimal mono sine instrument in raw CLAP (`clap-sys` + the `oscairn-clap`
//! wrapper; no clack or nih-plug, see rust/README.md). It declares its six parameters through
//! `oscairn.ui`: cutoff and level on the Filter/Levels role banks, and an ADSR drawn as a rich
//! envelope view with `seconds` times. rust/README.md says how to build it as a `.clap`.

use clap_sys::entry::clap_plugin_entry;
use clap_sys::events::{
    clap_event_header, clap_event_note, clap_event_param_value, clap_input_events, clap_output_events, CLAP_CORE_EVENT_SPACE_ID,
    CLAP_EVENT_NOTE_CHOKE, CLAP_EVENT_NOTE_END, CLAP_EVENT_NOTE_OFF, CLAP_EVENT_NOTE_ON, CLAP_EVENT_PARAM_VALUE,
};
use clap_sys::ext::audio_ports::{clap_audio_port_info, clap_plugin_audio_ports, CLAP_EXT_AUDIO_PORTS, CLAP_AUDIO_PORT_IS_MAIN, CLAP_PORT_STEREO};
use clap_sys::ext::note_ports::{clap_note_port_info, clap_plugin_note_ports, CLAP_EXT_NOTE_PORTS, CLAP_NOTE_DIALECT_CLAP};
use clap_sys::ext::params::{clap_param_info, clap_plugin_params, CLAP_EXT_PARAMS, CLAP_PARAM_IS_AUTOMATABLE};
use clap_sys::ext::state::{clap_plugin_state, CLAP_EXT_STATE};
use clap_sys::factory::plugin_factory::{clap_plugin_factory, CLAP_PLUGIN_FACTORY_ID};
use clap_sys::host::clap_host;
use clap_sys::plugin::{clap_plugin, clap_plugin_descriptor};
use clap_sys::plugin_features::{CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER};
use clap_sys::process::{clap_process, clap_process_status, CLAP_PROCESS_CONTINUE, CLAP_PROCESS_ERROR};
use clap_sys::stream::{clap_istream, clap_ostream};
use clap_sys::string_sizes::{CLAP_NAME_SIZE, CLAP_PATH_SIZE};
use clap_sys::version::CLAP_VERSION;
use oscairn_clap::ids;
use oscairn_clap::ui::{Field, ParamHint, Role, UiHints, Widget};
use std::cell::Cell;
use std::ffi::{c_char, c_void, CStr};
use std::sync::atomic::{AtomicU64, Ordering};

const PLUGIN_ID: &CStr = c"com.oscairn.examples.ui-hints";
const PLUGIN_NAME: &CStr = c"Oscairn UI Hints";
const PLUGIN_VENDOR: &CStr = c"Oscairn";
const PLUGIN_URL: &CStr = c"https://github.com/oscairn/oscairn-clap";
const PLUGIN_VERSION: &CStr = c"0.1.0";
const PLUGIN_DESCRIPTION: &CStr = c"A sine with declared parameter hints";

/// Parameter IDs: 1 Cutoff, 2 Level, 3 Attack, 4 Decay, 5 Sustain, 6 Release.
const P_CUTOFF: u32 = 1;
const P_LEVEL: u32 = 2;
const P_ATTACK: u32 = 3;
const P_DECAY: u32 = 4;
const P_SUSTAIN: u32 = 5;
const P_RELEASE: u32 = 6;
const PARAM_COUNT: u32 = 6;

/// The envelope's parameters are drawn joined (group 1) and as one rich view (view 1).
const ENVELOPE: u32 = 1;

/// (min, max, default) per parameter ID; the envelope times are plain seconds.
fn param_range(id: u32) -> (f64, f64, f64) {
    match id {
        P_CUTOFF => (20.0, 20_000.0, 8_000.0),
        P_LEVEL => (0.0, 1.0, 0.5),
        P_ATTACK => (0.001, 2.0, 0.01),
        P_DECAY => (0.001, 2.0, 0.3),
        P_SUSTAIN => (0.0, 1.0, 0.7),
        P_RELEASE => (0.001, 2.0, 0.2),
        _ => (0.0, 1.0, 0.0),
    }
}

fn param_name(id: u32) -> &'static CStr {
    match id {
        P_CUTOFF => c"Cutoff",
        P_LEVEL => c"Level",
        P_ATTACK => c"Attack",
        P_DECAY => c"Decay",
        P_SUSTAIN => c"Sustain",
        P_RELEASE => c"Release",
        _ => c"",
    }
}

impl UiHints for UiHintsPlugin {
    fn param_hint(&self, param_id: u32) -> Option<ParamHint> {
        let (priority, role, group, field) = match param_id {
            P_CUTOFF => (1, Role::OSCAIRN_ROLE_CUTOFF, 0, Field::OSCAIRN_FIELD_NONE),
            P_LEVEL => (2, Role::OSCAIRN_ROLE_LEVEL, 0, Field::OSCAIRN_FIELD_NONE),
            P_ATTACK => (0, Role::OSCAIRN_ROLE_ATTACK, ENVELOPE, Field::OSCAIRN_FIELD_ATTACK),
            P_DECAY => (0, Role::OSCAIRN_ROLE_DECAY, ENVELOPE, Field::OSCAIRN_FIELD_DECAY),
            P_SUSTAIN => (0, Role::OSCAIRN_ROLE_SUSTAIN, ENVELOPE, Field::OSCAIRN_FIELD_SUSTAIN),
            P_RELEASE => (0, Role::OSCAIRN_ROLE_RELEASE, ENVELOPE, Field::OSCAIRN_FIELD_RELEASE),
            _ => return None,
        };
        // The group doubles as the view: the envelope's parameters are both.
        Some(ParamHint { widget: Widget::Knob, priority, range_high: None, group, role, view: group, field, index: 0 })
    }

    /// The stages are plain seconds already, so the mapping is the identity. Sustain is a
    /// level, not a time: no answer for it.
    fn seconds(&self, param_id: u32, value: f64) -> Option<f64> {
        matches!(param_id, P_ATTACK | P_DECAY | P_RELEASE).then_some(value)
    }
}

/// Values set from both threads ride atomics; the voice's state is `Cell` (the host touches
/// it only from the audio thread after activate, and from the main thread only while stopped).
pub struct UiHintsPlugin {
    values: [AtomicU64; PARAM_COUNT as usize],
    sample_rate: Cell<f64>,
    phase: Cell<f64>,
    lowpass: Cell<f64>,
    env: Cell<f64>,
    gate: Cell<bool>,
    key: Cell<i16>,
    velocity: Cell<f64>,
}

unsafe impl Send for UiHintsPlugin {}
unsafe impl Sync for UiHintsPlugin {}

impl UiHintsPlugin {
    fn value(&self, id: u32) -> f64 {
        f64::from_bits(self.values[(id - 1) as usize].load(Ordering::Relaxed))
    }
    fn set_value(&self, id: u32, v: f64) {
        let (min, max, _) = param_range(id);
        self.values[(id - 1) as usize].store(v.clamp(min, max).to_bits(), Ordering::Relaxed);
    }
}

fn cstr_array<const N: usize>(s: &CStr) -> [c_char; N] {
    let mut out = [0 as c_char; N];
    for (o, b) in out.iter_mut().zip(s.to_bytes()) {
        *o = *b as c_char;
    }
    out
}

fn plugin<'a>(p: *const clap_plugin) -> &'a UiHintsPlugin {
    // Safety: plugin_data was set by create_plugin to a live Box<UiHintsPlugin> allocation,
    // and the host hands back that same clap_plugin on every call until destroy.
    unsafe { &*(*p).plugin_data.cast::<UiHintsPlugin>() }
}

extern "C" fn plugin_init(p: *const clap_plugin) -> bool {
    let me = plugin(p);
    for id in 1..=PARAM_COUNT {
        me.set_value(id, param_range(id).2);
    }
    true
}

extern "C" fn destroy(p: *const clap_plugin) {
    if !p.is_null() {
        // Safety: `p` came from Box::into_raw in create_plugin, and this is its one call; the
        // inner UiHintsPlugin is freed with the clap_plugin's plugin_data field below.
        let boxed = unsafe { Box::from_raw(p.cast_mut()) };
        let _data = unsafe { Box::from_raw((*p).plugin_data.cast::<UiHintsPlugin>()) };
        drop(boxed);
    }
}

extern "C" fn activate(p: *const clap_plugin, sample_rate: f64, _min: u32, _max: u32) -> bool {
    let me = plugin(p);
    me.sample_rate.set(sample_rate.max(1.0));
    me.phase.set(0.0);
    me.lowpass.set(0.0);
    me.env.set(0.0);
    me.gate.set(false);
    true
}

extern "C" fn start_processing(_p: *const clap_plugin) -> bool {
    true
}

extern "C" fn reset(p: *const clap_plugin) {
    let me = plugin(p);
    me.phase.set(0.0);
    me.env.set(0.0);
    me.gate.set(false);
}

/// The (param, value) of a core-space parameter event.
///
/// Safety: `header` is a valid core-space event header owned by the host for this block.
unsafe fn param_event(header: *const clap_event_header) -> Option<(u32, f64)> {
    let h = unsafe { &*header };
    if h.space_id != CLAP_CORE_EVENT_SPACE_ID || h.type_ != CLAP_EVENT_PARAM_VALUE || h.size != std::mem::size_of::<clap_event_param_value>() as u32 {
        return None;
    }
    // Safety: size matches clap_event_param_value, and a param-value event is a header prefix.
    let e = unsafe { &*(header as *const clap_event_param_value) };
    Some((e.param_id, e.value))
}

/// The (type, body) of a core-space note event.
///
/// Safety: `header` is a valid core-space event header owned by the host for this block.
unsafe fn note_event(header: *const clap_event_header) -> Option<(u16, &'static clap_event_note)> {
    let h = unsafe { &*header };
    if h.space_id != CLAP_CORE_EVENT_SPACE_ID || h.size != std::mem::size_of::<clap_event_note>() as u32 {
        return None;
    }
    if !matches!(h.type_, CLAP_EVENT_NOTE_ON | CLAP_EVENT_NOTE_OFF | CLAP_EVENT_NOTE_CHOKE | CLAP_EVENT_NOTE_END) {
        return None;
    }
    // Safety: size matches clap_event_note; a note event is a header prefix. 'static is a lie
    // bounded by the block; the caller only reads the fields within process.
    Some((h.type_, unsafe { &*(header as *const clap_event_note) }))
}

extern "C" fn process(p: *const clap_plugin, block: *const clap_process) -> clap_process_status {
    let me = plugin(p);
    // Safety: the host passes a live clap_process whose one declared stereo output has
    // frames_count writable frames per channel; the event lists live until we return.
    let block = unsafe { &*block };
    if block.audio_outputs_count != 1 {
        return CLAP_PROCESS_ERROR;
    }
    let out = unsafe { &*block.audio_outputs };
    let (l, r) = unsafe {
        (
            std::slice::from_raw_parts_mut(*out.data32.add(0), block.frames_count as usize),
            std::slice::from_raw_parts_mut(*out.data32.add(1), block.frames_count as usize),
        )
    };

    if let Some(in_events) = unsafe { block.in_events.as_ref() } {
        let size = unsafe { (in_events.size.expect("host event list without size"))(in_events) };
        for i in 0..size {
            // Safety: the pointer is null or a valid event header owned by the host for this block.
            let Some(header) = (unsafe { (in_events.get.expect("host event list without get"))(in_events, i).as_ref() }) else { continue };
            if let Some((id, v)) = unsafe { param_event(header) } {
                me.set_value(id, v);
            } else if let Some((type_, n)) = unsafe { note_event(header) } {
                if type_ == CLAP_EVENT_NOTE_ON {
                    me.key.set(n.key);
                    me.velocity.set(n.velocity);
                    me.gate.set(true);
                } else {
                    me.gate.set(false); // OFF, CHOKE and END all close the one gate
                }
            }
        }
    }

    let sr = me.sample_rate.get();
    let (attack, decay, sustain, release, cutoff, level) =
        (me.value(P_ATTACK), me.value(P_DECAY), me.value(P_SUSTAIN), me.value(P_RELEASE), me.value(P_CUTOFF), me.value(P_LEVEL));
    let freq = 440.0 * 2f64.powf((me.key.get().max(0) as f64 - 69.0) / 12.0);
    let lp_alpha = (2.0 * std::f64::consts::PI * cutoff / sr).min(1.0);
    // A simple linear-attack / exponential-decay-to-sustain / exponential-release ADSR.
    let a_step = 1.0 / (attack * sr);
    let d_k = (-1.0 / (decay * sr)).exp();
    let r_k = (-1.0 / (release * sr)).exp();
    for frame in 0..block.frames_count as usize {
        me.phase.set((me.phase.get() + freq / sr) % 1.0);
        let mut env = me.env.get();
        if me.gate.get() {
            // Attack to the sustain level (no decay stage: the attack reaches it), then relax to it.
            env = if env < sustain { (env + a_step).min(sustain) } else { (env * d_k).max(sustain) };
        } else {
            env *= r_k;
            if env < 1e-5 {
                env = 0.0;
            }
        }
        me.env.set(env);
        let raw = (me.phase.get() * std::f64::consts::TAU).sin();
        me.lowpass.set(me.lowpass.get() + lp_alpha * (raw - me.lowpass.get()));
        let sample = (me.lowpass.get() * env * me.velocity.get() * level) as f32;
        l[frame] = sample;
        r[frame] = sample;
    }
    CLAP_PROCESS_CONTINUE
}

extern "C" fn params_count(_p: *const clap_plugin) -> u32 {
    PARAM_COUNT
}

extern "C" fn params_get_info(_p: *const clap_plugin, index: u32, out: *mut clap_param_info) -> bool {
    if index >= PARAM_COUNT || out.is_null() {
        return false;
    }
    let id = index + 1;
    let (min, max, default) = param_range(id);
    let info = clap_param_info {
        id,
        flags: CLAP_PARAM_IS_AUTOMATABLE,
        cookie: std::ptr::null_mut(),
        name: cstr_array::<CLAP_NAME_SIZE>(param_name(id)),
        module: cstr_array::<CLAP_PATH_SIZE>(if id >= P_ATTACK { c"Amp EG" } else { c"" }),
        min_value: min,
        max_value: max,
        default_value: default,
    };
    // Safety: `out` is a writable clap_param_info owned by the host.
    unsafe { *out = info };
    true
}

extern "C" fn params_get_value(p: *const clap_plugin, id: u32, out: *mut f64) -> bool {
    if id == 0 || id > PARAM_COUNT || out.is_null() {
        return false;
    }
    // Safety: `out` is a writable double owned by the host (main thread).
    unsafe { *out = plugin(p).value(id) };
    true
}

extern "C" fn params_flush(_p: *const clap_plugin, _in: *const clap_input_events, _out: *const clap_output_events) {}

extern "C" fn audio_ports_count(_p: *const clap_plugin, is_input: bool) -> u32 {
    u32::from(!is_input)
}

extern "C" fn audio_ports_get(_p: *const clap_plugin, index: u32, is_input: bool, out: *mut clap_audio_port_info) -> bool {
    if is_input || index != 0 || out.is_null() {
        return false;
    }
    // Safety: `out` is a writable port info owned by the host.
    unsafe {
        *out = clap_audio_port_info {
            id: 1,
            name: cstr_array::<CLAP_NAME_SIZE>(c"Main"),
            flags: CLAP_AUDIO_PORT_IS_MAIN,
            channel_count: 2,
            port_type: CLAP_PORT_STEREO.as_ptr(),
            in_place_pair: u32::MAX,
        }
    };
    true
}

extern "C" fn note_ports_count(_p: *const clap_plugin, is_input: bool) -> u32 {
    u32::from(is_input)
}

extern "C" fn note_ports_get(_p: *const clap_plugin, index: u32, is_input: bool, out: *mut clap_note_port_info) -> bool {
    if !is_input || index != 0 || out.is_null() {
        return false;
    }
    // Safety: `out` is a writable port info owned by the host.
    unsafe {
        *out = clap_note_port_info {
            id: 1,
            supported_dialects: CLAP_NOTE_DIALECT_CLAP,
            preferred_dialect: CLAP_NOTE_DIALECT_CLAP,
            name: cstr_array::<CLAP_NAME_SIZE>(c"Notes"),
        }
    };
    true
}

extern "C" fn state_save(p: *const clap_plugin, stream: *const clap_ostream) -> bool {
    let me = plugin(p);
    let mut buf = [0u8; 8 * PARAM_COUNT as usize];
    for i in 0..PARAM_COUNT as usize {
        buf[i * 8..(i + 1) * 8].copy_from_slice(&me.value(i as u32 + 1).to_le_bytes());
    }
    // Safety: the host's stream fills from our buffer and returns bytes written or -1.
    let written = unsafe { (stream.read().write.expect("state stream without write"))(stream, buf.as_ptr().cast::<c_void>(), buf.len() as u64) };
    written == buf.len() as i64
}

extern "C" fn state_load(p: *const clap_plugin, stream: *const clap_istream) -> bool {
    let me = plugin(p);
    let mut buf = [0u8; 8 * PARAM_COUNT as usize];
    let mut got = 0usize;
    while got < buf.len() {
        // Safety: the host's stream fills our buffer and returns bytes read or <= 0 at end.
        let n = unsafe { (stream.read().read.expect("state stream without read"))(stream, buf[got..].as_mut_ptr().cast::<c_void>(), (buf.len() - got) as u64) };
        if n <= 0 {
            return false;
        }
        got += n as usize;
    }
    for i in 0..PARAM_COUNT as usize {
        let mut b = [0u8; 8];
        b.copy_from_slice(&buf[i * 8..(i + 1) * 8]);
        me.set_value(i as u32 + 1, f64::from_le_bytes(b));
    }
    true
}

/// One draft-5 table answers all three ID strings: the host reads only the members an older
/// draft has, and those are a prefix of this struct.
static UI_TABLE: oscairn_clap::reexport::oscairn_plugin_ui_t = oscairn_clap::ui_table!(UiHintsPlugin);

static PARAMS: clap_plugin_params = clap_plugin_params {
    count: Some(params_count),
    get_info: Some(params_get_info),
    get_value: Some(params_get_value),
    value_to_text: None,
    text_to_value: None,
    flush: Some(params_flush),
};

static AUDIO_PORTS: clap_plugin_audio_ports = clap_plugin_audio_ports { count: Some(audio_ports_count), get: Some(audio_ports_get) };

static NOTE_PORTS: clap_plugin_note_ports = clap_plugin_note_ports { count: Some(note_ports_count), get: Some(note_ports_get) };

static STATE: clap_plugin_state = clap_plugin_state { save: Some(state_save), load: Some(state_load) };

/// `*const c_char` is not `Sync`; the strings are static, so this is safe.
#[repr(transparent)]
struct CStrPtr(*const c_char);
unsafe impl Sync for CStrPtr {}
static FEATURES: [CStrPtr; 3] =
    [CStrPtr(CLAP_PLUGIN_FEATURE_INSTRUMENT.as_ptr()), CStrPtr(CLAP_PLUGIN_FEATURE_SYNTHESIZER.as_ptr()), CStrPtr(std::ptr::null())];

static DESCRIPTOR: clap_plugin_descriptor = clap_plugin_descriptor {
    clap_version: CLAP_VERSION,
    id: PLUGIN_ID.as_ptr(),
    name: PLUGIN_NAME.as_ptr(),
    vendor: PLUGIN_VENDOR.as_ptr(),
    url: PLUGIN_URL.as_ptr(),
    manual_url: PLUGIN_URL.as_ptr(),
    support_url: PLUGIN_URL.as_ptr(),
    version: PLUGIN_VERSION.as_ptr(),
    description: PLUGIN_DESCRIPTION.as_ptr(),
    // SAFETY: clap_plugin_descriptor's `features` is a NULL-terminated array of C strings;
    // CStrPtr has repr(transparent) over the same `*const c_char`, so this cast preserves the
    // memory layout, and FEATURES lives for the process.
    features: FEATURES.as_ptr().cast::<*const c_char>(),
};

extern "C" fn get_extension(_p: *const clap_plugin, id: *const c_char) -> *const c_void {
    if id.is_null() {
        return std::ptr::null();
    }
    // Safety: the host passes a NUL-terminated extension ID.
    let id = unsafe { CStr::from_ptr(id) };
    let core = |c: &CStr| id == c;
    if core(CLAP_EXT_PARAMS) {
        &PARAMS as *const _ as *const c_void
    } else if core(CLAP_EXT_AUDIO_PORTS) {
        &AUDIO_PORTS as *const _ as *const c_void
    } else if core(CLAP_EXT_NOTE_PORTS) {
        &NOTE_PORTS as *const _ as *const c_void
    } else if core(CLAP_EXT_STATE) {
        &STATE as *const _ as *const c_void
    } else if [ids::OSCAIRN_EXT_UI, ids::OSCAIRN_EXT_UI_DRAFT_4, ids::OSCAIRN_EXT_UI_DRAFT_3].contains(&id) {
        &UI_TABLE as *const _ as *const c_void
    } else {
        std::ptr::null()
    }
}

extern "C" fn factory_count(_f: *const clap_plugin_factory) -> u32 {
    1
}

extern "C" fn factory_descriptor(_f: *const clap_plugin_factory, index: u32) -> *const clap_plugin_descriptor {
    if index == 0 {
        &DESCRIPTOR
    } else {
        std::ptr::null()
    }
}

extern "C" fn noop(_p: *const clap_plugin) {}

extern "C" fn factory_create(_f: *const clap_plugin_factory, _host: *const clap_host, plugin_id: *const c_char) -> *const clap_plugin {
    // Safety: the host passes a NUL-terminated ID it read from the descriptor.
    if plugin_id.is_null() || unsafe { CStr::from_ptr(plugin_id) } != PLUGIN_ID {
        return std::ptr::null();
    }
    let values = std::array::from_fn(|i| AtomicU64::new(param_range(i as u32 + 1).2.to_bits()));
    let me = UiHintsPlugin {
        values,
        sample_rate: Cell::new(48000.0),
        phase: Cell::new(0.0),
        lowpass: Cell::new(0.0),
        env: Cell::new(0.0),
        gate: Cell::new(false),
        key: Cell::new(60),
        velocity: Cell::new(0.8),
    };
    let data = Box::into_raw(Box::new(me)).cast::<c_void>();
    Box::into_raw(Box::new(clap_plugin {
        desc: &DESCRIPTOR,
        plugin_data: data,
        init: Some(plugin_init),
        destroy: Some(destroy),
        activate: Some(activate),
        deactivate: Some(noop),
        start_processing: Some(start_processing),
        stop_processing: Some(noop),
        reset: Some(reset),
        process: Some(process),
        get_extension: Some(get_extension),
        on_main_thread: Some(noop),
    }))
}

static FACTORY: clap_plugin_factory =
    clap_plugin_factory { get_plugin_count: Some(factory_count), get_plugin_descriptor: Some(factory_descriptor), create_plugin: Some(factory_create) };

extern "C" fn entry_init(_path: *const c_char) -> bool {
    true
}

extern "C" fn entry_deinit() {}

extern "C" fn entry_get_factory(factory_id: *const c_char) -> *const c_void {
    // Safety: NUL-terminated factory ID from the host.
    if !factory_id.is_null() && unsafe { CStr::from_ptr(factory_id) } == CLAP_PLUGIN_FACTORY_ID {
        &FACTORY as *const _ as *const c_void
    } else {
        std::ptr::null()
    }
}

/// The CLAP entry point the host dlsyms.
#[no_mangle]
pub static clap_entry: clap_plugin_entry =
    clap_plugin_entry { clap_version: CLAP_VERSION, init: Some(entry_init), deinit: Some(entry_deinit), get_factory: Some(entry_get_factory) };
