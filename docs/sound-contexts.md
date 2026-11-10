# oscairn.sound-contexts: prepared sounds in one instance

| | |
| --- | --- |
| ID | `OSCAIRN_EXT_SOUND_CONTEXTS` = `"oscairn.sound-contexts/draft-5"` (plugin extension) |
| Header | [`sound-contexts.h`](../include/oscairn-clap/sound-contexts.h) |
| SDK helper | [`sound-contexts-plugin.hpp`](sound-contexts-plugin.md) implements all of it |
| Design | Oscairn [RFC 0005](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0005-sound-contexts.md) |
| Example | [`examples/sound-contexts/`](../examples/sound-contexts/) |

An instrument with native sound contexts holds several prepared sounds (patches) in one instance.
Each MIDI channel selects one; a held note keeps the sound it started with. Oscairn uses them for
instant sound switching and multitimbral playing without an extra instance per sound, for presets
prepared while the current sound plays, and (with
[`oscairn.sound-snapshots`](sound-snapshots.md)) for instant Performance changes and parts
sharing one instance. New native Oscairn instruments implement it, on the SDK helper.

## Who implements what

| Side | Implements |
| --- | --- |
| Plugin | `oscairn_plugin_sound_contexts_t`: `capacity`, `get`, `prepare`, `select`, `discard` (required); `state` and `prepare_preset` (optional) |
| Host | Calls them; with `state`, saves and loads the plugin through it instead of `clap.state` |

## The model

- **Slots and IDs.** `capacity` is the number of slots, constant for the instance's lifetime (at
  most 256). Slot 0 holds the Default sound, ID 0, selected on every channel at first. A prepared
  sound gets a fresh ID, never reused during the instance's lifetime; unloading invalidates them
  all.
- **Concurrency.** The context-management calls (`capacity`, `get`, `prepare`, `select`,
  `discard`, `prepare_preset`) run on the **main thread, concurrently with `process`**, and must
  not stop voices or touch what a voice uses. `state`'s calls are exclusive: the host stops
  processing first.
- **Selection** is queued, bounded, and applied at the next process boundary, before that block's
  notes. Channel -1 selects all channels. An accepted selection pins its sound until it is
  applied. Voices, sustain and expression keep the sound that owns them.
- **Idle** means no selection, no pending request, no voice and no tail. `discard` fails unless
  the sound is idle.

## Functions

| Function | Thread | Does |
| --- | --- | --- |
| `uint32_t capacity(plugin)` | main | The slot count; Oscairn refuses more than 256 |
| `bool get(plugin, slot, oscairn_sound_context_info_t*)` | main, during `process` | False for an empty slot; else its `id`, `name`, `selected_channels` (bit mask), `active_voices` and `idle`. Telemetry fields are independent snapshots and may change while read |
| `uint32_t prepare(plugin, name, parameters, count, patch, base, message, capacity)` | main, during `process`; may read files | Builds an independent sound from the plugin's defaults plus `parameters`, or from `patch` text when the plugin defines one (the Legacy Sampler takes SFZ), and returns its new ID. Asset paths in `patch` are relative to `base`, a library folder resolved through the host's [`oscairn.assets`](assets.md) (`base->sha256` unused); both null for parameter-only sounds. `CLAP_INVALID_ID`: refused (an invalid patch or no free slot), optionally with a NUL-terminated diagnostic, with its source line, in `message` |
| `bool select(plugin, id, int16_t channel)` | main, during `process` | Queues the selection; channel -1 to 15. False for an unknown ID or a full queue |
| `bool discard(plugin, id)` | main, during `process` | Frees the slot; false while selected, queued, sounding or keeping a tail (the SDK also never discards the Default, ID 0) |
| `const clap_plugin_state_t* state` | main, **rendering excluded** | Optional: saves and loads every sound, its name and each channel's selection (accepted queued selections included, voices and histories excluded). `load` validates everything before it replaces anything and gives the sounds fresh IDs. It is not `CLAP_EXT_STATE`, but Oscairn uses it as the plugin's state when present |
| `uint32_t prepare_preset(plugin, name, location_kind, location, load_key, message, capacity)` | main, during `process`; may read files | Draft 5, optional: a sound from one of the presets the plugin lists through CLAP preset discovery, with the same rules as `prepare`. `location_kind`, `location` and `load_key` exactly as `clap_plugin_preset_load.from_location` takes them: `location` null for `CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN`, else a file path; `load_key` null for a file holding one preset |

## What Oscairn does

- **Load:** asks `get_extension(OSCAIRN_EXT_SOUND_CONTEXTS)` after `init` and calls `capacity`
  once. A plugin missing any function but `state` and `prepare_preset`, or reporting more than 256,
  is refused ("Invalid sound-context interface").
- **State:** with `state`, every save stops processing (`stop_processing`), flushes parameters and
  saves through it; every load deactivates, loads and activates. `oscairn.sound-snapshots` is
  asked for only when `state` is given.
- **Preparing:** before calling `prepare`, the host checks the name (at most 63 bytes), at most 128
  parameters, each existing, writable, finite, in range and not repeated, and a patch with no NUL
  bytes that comes with a library or not at all. With a memory budget it then checks the sound's
  declared `context_bytes` fit ([memory](memory.md)). Errors: "Invalid sound patch", "Invalid sound
  parameter", "Duplicate sound parameter", "Sound preparation rejected: …".
- **Presets:** a preset picked in Oscairn's sound lists becomes a sound through `prepare_preset`.
  A native instrument without it has its presets listed but refused ("Plugin cannot prepare sounds
  from presets").
- **Selecting:** a selection wakes a sleeping plugin, so it processes the boundary. Oscairn reads
  `get` to follow which sound each channel plays, and polls `discard` on outgoing sounds until it
  succeeds, so a sound with a voice or a tail rings out: report idleness honestly.
- **Parameter events** with a channel edit the sound that channel selects; a plugin should follow
  per-channel parameter events (the SDK's Engine does).
- **In a worker process** the same calls cross to the worker; the IDs the engine sees are mapped to
  the worker's, and a restarted worker gets back its state, the sounds prepared since and their
  selections.

## Without it

A plain instrument still switches sounds instantly in Oscairn: the host prepares each sound in an
extra instance of the plugin (up to eight per part, the Default one of them) and switches between
them at the cue. That needs an instrument with note input, at least one audio output, no audio
input and no note output, and costs a whole instance's memory and CPU per prepared sound. Its
presets are loaded with CLAP's `preset-load` into a spare instance. Each part plays its own
instance; Performance changes load the whole state into an instance.

## Minimal example

Built on the SDK helper, the extension is a table of the Engine's functions plus your `prepare`
(see [sound-contexts-plugin](sound-contexts-plugin.md) for the rest of the instrument):

```cpp
#include <oscairn-clap/sound-contexts-plugin.hpp>

struct Synth : oscairn::contexts::Engine<Synth, Voice, Patch> { /* see sound-contexts-plugin.md */ };

const clap_plugin_state_t snapshot{Synth::saveSnapshot, Synth::loadSnapshot};
const oscairn_plugin_sound_contexts_t soundContexts{Synth::capacity, Synth::get, Synth::prepare,
    Synth::select, Synth::discard, &snapshot, Synth::preparePreset};
// in get_extension:
if (!strcmp(id, OSCAIRN_EXT_SOUND_CONTEXTS)) return &soundContexts;
```

## Testing

- Over Oscairn's JSON-RPC interface: `sounds.prepare`, `sounds.select`, `sounds.discard`,
  `sounds.get` and `sounds.presets` drive the extension on a loaded plugin. Prepare a sound while
  a note holds, select it, check the held note keeps its sound and a new note takes the new one;
  check `discard` fails while the old note rings and succeeds after.
- Save and load the state with sounds prepared and channels selecting different ones, and check
  the reopened instance selects the same sounds.
- `oscairn-plugin-check PLUGIN ID synth` checks the plugin plays, saves and reopens (through
  `state`).
- Oscairn's tests: `sound-contexts` and `sound-contexts-workers` (Basic Synth),
  `control-sound-presets`, `control-native-sounds`.
