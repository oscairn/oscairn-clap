# oscairn.sound-snapshots: instant Performance changes and parts sharing one instance

| | |
| --- | --- |
| ID | `OSCAIRN_EXT_SOUND_SNAPSHOTS` = `"oscairn.sound-snapshots/draft-4"` (plugin extension) |
| Header | [`sound-contexts.h`](../include/oscairn-clap/sound-contexts.h) |
| Requires | [`oscairn.sound-contexts`](sound-contexts.md) with its `state` |
| SDK helper | [`sound-contexts-plugin.hpp`](sound-contexts-plugin.md) implements all of it |
| Design | Oscairn [RFC 0005](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0005-sound-contexts.md#preparing-a-snapshot-beside), [RFC 0021](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0021-sounds-performances-and-library.md#native-sound-contexts-across-a-change) |
| Example | [`examples/sound-snapshots/`](../examples/sound-snapshots/) |

Three things a native instrument can do with whole snapshots (what `state->save` writes):

1. **Prepare a snapshot beside** the playing sounds (`prepare`): at a Performance change, the next
   Performance's sounds for an instrument already playing are prepared as new contexts in that
   instance and selected at the cue, instead of loading its whole snapshot into an instance (for a
   sampler, its samples again).
2. **Ring out** the outgoing sounds into a hidden buffer (`ring_out`, `choke_retiring`), so their
   tails play through the outgoing Performance's effects and level rather than the incoming one's.
3. **Save one sound on its own** and read its values (`save_sound`, `sound_values`), which lets
   several parts share one instance.

## Who implements what

| Side | Implements |
| --- | --- |
| Plugin | `oscairn_plugin_sound_snapshots_t`: `prepare` (required); `ring_out` and `choke_retiring` (optional, together); `save_sound` and `sound_values` (optional, draft 4; both needed for sharing) |
| Host | Calls them; routes the ring-out buffer; decides which parts share an instance |

Oscairn asks for the extension only when `oscairn.sound-contexts` gives a `state`. A table without
`prepare`, or with only one of `ring_out` and `choke_retiring`, is refused at load ("Invalid
sound-snapshot interface").

## Functions

### `bool prepare(plugin, const clap_istream_t* snapshot, uint32_t* selected, char* message, uint32_t message_capacity)`

**Main thread, concurrently with `process`**, may read files, never touches a playing sound,
selection or voice. `snapshot` is a whole snapshot as `state->save` writes it. The plugin validates
all of it, then prepares every sound in it (its Default too) as a new context with a fresh ID: all
or nothing, never stealing a slot. `selected[16]` receives the new ID each MIDI channel selects in
the snapshot; nothing is selected yet. False: nothing was added, with an optional NUL-terminated
diagnostic in `message`.

Oscairn then selects the new sounds at the cue (one `select(id, -1)` when all 16 channels select
the same sound, else one per channel) and discards each outgoing sound once it is idle. With a
memory budget, the new sounds' context bytes are checked after the call and the sounds discarded
again if they do not fit. With too few free contexts (the incoming sounds are prepared beside the
playing ones, the snapshot's Default counts) the call fails and the change falls back to loading
the snapshot into an instance.

### `void ring_out(plugin, const clap_audio_buffer_t* buffer)` (draft 3, optional)

**Audio thread, immediately before every `process` call**, with a zeroed buffer of four `data32`
channels of that call's `frames_count`, or null for none; it is valid only until that `process`
returns, and a `process` not preceded by a call has none. Only store the pointer: no allocation,
lock or wait. During that `process`, the plugin adds the main-port output of every **retiring**
context (selected by no channel, with voices or a tail left) into channels 0 and 1 instead of its
main output port, and that of a retiring context `choke_retiring` is fading into channels 2 and 3.
It is not an audio port.

### `bool choke_retiring(plugin)` (draft 3, required with `ring_out`)

**Main thread.** Only one change's tail carries over. At a second Performance change while the
first change's tail still rings, Oscairn calls it just before selecting the incoming sounds. It is
queued like `select`: at that process boundary, before the selections queued after it, the plugin
chokes every context retiring then (a short fade, as a CLAP choke, no click), whose main output
goes into channels 2 and 3 until it ends; the sounds the new selections leave take channels 0 and 1.
False when nothing could be queued.

### `bool save_sound(plugin, uint32_t id, const clap_ostream_t* stream)` (draft 4, optional)

**Main thread, rendering excluded** (as `state->save`). Writes a whole snapshot holding only sound
`id`: what an instance holding that sound alone would save, as its Default, selected on every
channel and playing on the main output port (its output-bus parameter at 0). False for an unknown
ID. Oscairn uses it to save a shared part's sound as a Sound of its own.

### `uint32_t sound_values(plugin, uint32_t id, oscairn_sound_parameter_t* values, uint32_t capacity)` (draft 4, optional)

**Main thread, concurrently with `process`.** Writes up to `capacity` of the sound's parameter
values (every writable parameter's) and returns how many it has; 0 for an unknown ID or one
selected on a MIDI channel (whose values the audio thread may be changing). Oscairn uses it to
show the values of the sound a shared part plays; it may ask for a selected sound, and takes 0 as
"use what was saved".

## Ringing out: what the player hears

With `ring_out`, three limits a plugin needs no code for beyond the SDK's:

- **One tail carries over.** A second change while a first change's tail still rings cuts that
  tail with the choke fade (3 ms on the SDK), rendered into channels 2 and 3, which the host still
  routes through the first change's chain. So no tail jumps to another chain and nothing clicks,
  but a long tail (a held pad, an instrument's own delay) ends at the second cue. A plugin with
  tails outside its voices, which the SDK cannot choke, fades them itself in that context while
  the SDK's `retiring(slot)` returns `ringOut + 2`.
- **Main output only.** Only a retiring sound's main output port rings out through the outgoing
  chain. Its other output ports keep playing through the instance's ports, which follow the
  incoming Performance's chain.
- **30 seconds.** An outgoing effect fed only by the ring-out buffer goes once 30 seconds pass with
  no note through it. A note held through the change for longer loses the outgoing effects, and its
  tail moves back to the main output, through the incoming chain.

Without `ring_out` (both calls null), outgoing tails play through the incoming chain. A plugin built
against draft 1 or 2 must be rebuilt (neither is read any more): draft 3 added `choke_retiring`, and
on the SDK `retiring(slot)` returns the channels (null when not retiring) instead of a bool.

## Parts sharing one instance

Parts whose instruments are the same plugin share one instance, each with its own sound, MIDI
channel and output bus (RFC 0021): adding the plugin where an instance of it has room, opening a
Performance, or changing to one all put a part's sound into an instance with room; a part alone
keeps an instance of its own. The host sends a part's notes and parameter edits on its channel and
takes its sound from its output port.

A plugin is shared only when **all** of these hold (Oscairn checks each):

- native sound contexts (`oscairn.sound-contexts`) with their `state`;
- this extension with `save_sound` and `sound_values`;
- at least one note input port, **no note output port** and **no audio input port**;
- **more than one audio output port**;
- an **output-bus parameter**: stepped (`CLAP_PARAM_IS_STEPPED`), writable (not read-only), minimum
  0, maximum at least 1, whose value is the index of the output port a sound plays on (0 the main
  port), tagged with `oscairn.ui`'s `OSCAIRN_ROLE_OUTPUT_BUS` ([ui](ui.md));
- a sound selected on all 16 channels when the first part joins (the plugin's state as a lone
  part leaves it), and the plugin not failed;
- at a Performance load: no effect contexts made from it, and audio taken only from its main
  output.

The host sets the bus per sound with a parameter event on the part's channel, so the plugin must
apply per-channel parameter events to the sound that channel selects (the SDK's Engine does).
Voices keep the port they started on. An instance holds as many parts as it has free sounds,
output ports (up to the bus parameter's maximum + 1) and MIDI channels; past that the host takes
another instance. Parts sharing an instance share its voice pool, so a plugin meant to share needs
a pool sized for several parts; per-part reserves and priorities come through
[`oscairn.voice-budget`](voice-budget.md), one entry per part's sound.

## Without it

- **A Performance change** takes a native instrument over whole: the incoming snapshot is loaded
  into a free or warm instance (reset, `state->load`) or a new one, and the outgoing instance rings
  out alone, until idle but no longer than 30 seconds after the change with no note held, or 2
  minutes with one (a panic ends it at once). A sampler loads its samples again.
- **No ring-out:** the outgoing sounds' tails play through the incoming chain (when they share the
  instance through `prepare`).
- **No sharing:** each part plays an instance of its own.

## Minimal example

On the SDK helper, one table and one `get_extension` line, plus `alone` and `patchValues` in the
instrument for draft 4:

```cpp
const oscairn_plugin_sound_snapshots_t soundSnapshots{Synth::prepareSnapshot, Synth::setRingOut,
    Synth::chokeRetiring, Synth::saveSound, Synth::soundValues};
// in get_extension:
if (!strcmp(id, OSCAIRN_EXT_SOUND_SNAPSHOTS)) return &soundSnapshots;

// in the instrument (Derived):
void alone(Patch& patch) { patch.values[outputBusIndex] = 0; } // plays on the main port
uint32_t patchValues(const Patch& patch, oscairn_sound_parameter_t* out, uint32_t capacity)
{
    for (uint32_t i = 0; i < count && i < capacity; ++i) out[i] = {i + 1, patch.values[i]};
    return count;
}
```

`render` then adds each context's main output into `retiring(slot)`'s two channels when it is not
null, instead of the main port (see [sound-contexts-plugin](sound-contexts-plugin.md#ringing-out)).

## Testing

- Save a snapshot from one instance, `prepare` it beside in another that is playing, check the
  returned `selected` IDs select the snapshot's sounds after `select`, and that a held note keeps
  its old sound until released.
- With a mock host, call `ring_out` before `process` with a zeroed four-channel buffer while an
  unselected sound has a tail, and check the tail lands in channels 0 and 1 and not the main port;
  call `choke_retiring`, process, and check it fades into channels 2 and 3 and ends.
- `save_sound` then `state->load` into a fresh instance must play that sound on every channel on
  the main port.
- In Oscairn: `instruments.share` over JSON-RPC puts a part's sound into an instance (and says
  which requirement is missing when it cannot); `parts.saveSound` saves a shared part's Sound.
- Oscairn's tests: `control-performance-switch` (ring-out routes and the older tail's choke),
  `shared-engine` and `shared-engine-workers`, `control-shared-parts`, `worker-recovery`.
