# sound-contexts-plugin.hpp: a native instrument's engine

| | |
| --- | --- |
| Header | [`sound-contexts-plugin.hpp`](../include/oscairn-clap/sound-contexts-plugin.hpp) (C++17, header-only, MIT; relicensed from AGPL-3.0-or-later on 2026-10-31) |
| Implements | [`oscairn.sound-contexts/draft-5`](sound-contexts.md), [`oscairn.sound-snapshots/draft-4`](sound-snapshots.md), [`oscairn.voice-budget/draft-1`](voice-budget.md), and the voice and note handling around them |
| Example | [`examples/sound-contexts/`](../examples/sound-contexts/), [`examples/sound-snapshots/`](../examples/sound-snapshots/) |
| References | Oscairn's Basic Synth (`plugins/basic-synth/basic_synth.cpp`) and the Legacy Sampler (github.com/oscairn/sampler) |

`oscairn::contexts::Engine<Derived, Voice, Patch, ContextCapacity = 8, VoiceCount = 128>` is the
part of a native instrument that is the same in every instrument: the preparation slots, the
bounded selection queue, voice ownership and release rules, sustain, `NOTE_END`, choke fades, pitch
bend and the mod wheel per channel, the exclusive context snapshot, snapshots prepared beside,
ring-out, one sound saved alone, and the voice budget. The instrument (`Derived`, which inherits
from `Engine`) supplies its patch type, voice DSP, parameters and patch serialization. `Voice`
derives from `oscairn::contexts::VoiceBase`.

## What the instrument provides

Required:

| Member | Called | Does |
| --- | --- | --- |
| `bool accepts(const Patch&, const clap_event_note_t&)` | audio | Whether a note starts a voice (a note that starts none still gets its `NOTE_END`) |
| `void start(Voice&, Context&)` | audio | Initialize the voice's DSP from the context's patch |
| `void release(Voice&)` | audio | Enter the release stage |
| `void render(const clap_process_t*, uint32_t from, uint32_t to)` | audio | Render every active voice for frames `from` to `to`; call `finishVoice(v)` when a voice ends |
| `void applyParam(const clap_event_header_t*)` | audio | A `CLAP_EVENT_PARAM_VALUE` or `_MOD`: apply it to the sound each matching channel selects (`selected[channel]`), every channel for -1 |
| `void selectedGlobally(Context&, const clap_output_events_t*)` | audio | After a selection on all channels: typically report the new sound's values as parameter events |
| `static uint32_t prepare(plugin, name, parameters, count, patch, base, message, capacity)` | main | The extension's `prepare`: validate, take `freeSlot()`, fill its patch, `publish(*slot, name)` |
| `static constexpr const char* stateVersion`, `static constexpr size_t maxStateBytes` | | The snapshot's first token, and the most bytes a snapshot read accepts |
| `bool writePatch(std::ostream&, const Patch&)`, `bool readPatch(std::istream&, Patch&)` | main | One patch, text, on the rest of its line; `readPatch` validates (also on the main thread while rendering, for snapshots prepared beside: into a patch not yet published, touching nothing a voice uses) |
| `void restored()` | main | After a snapshot load: refresh what the parameters report (`selected[0]`'s patch, say) |

Optional, each with a default:

| Member | For |
| --- | --- |
| `bool readPreset(uint32_t kind, const char* location, const char* loadKey, Patch&, std::string& error)` | Presets (draft 5): read the preset that [preset discovery](preset-discovery-plugin.md) listed into a patch that starts as `Patch{}`; set the table's `prepare_preset` to `Engine::preparePreset` |
| `void alone(Patch&)` | `save_sound` (draft 4): the patch as played on the main output port (its output bus at 0) |
| `uint32_t patchValues(const Patch&, oscairn_sound_parameter_t*, uint32_t capacity)` | `sound_values` (draft 4): every writable parameter's value; return the count |
| `bool buildPreset(Context&, std::string& error)` | Called with a preset's or a snapshot's patch in place, before the context is published, to build per-sound state (decode samples); false refuses it. To be fast, reuse what other contexts hold |
| `void snapshotLoading(bool)` | Called around reading a snapshot's patches |
| `void voiceFinished(Voice&)` | Called whenever a voice ends: released out, choked, stolen or reset, so the instrument need not poll |
| `void discarded(Context&)` | Main thread, after a context is unpinned and discarded |
| `double loudness(const Voice&) const` | The voice's level now, linear (default: velocity × volume expression × choke fade) |
| `bool mono(const Context&) const` | The sound plays one note at a time (default false) |
| `double voiceCost() const` | Microseconds a voice costs a block (default 0, no estimate) |
| `static constexpr bool volumeExpressions` | True when `render` multiplies by `amplitude()` (default false) |

## What the Engine gives the instrument

- `process` (use it as the plugin's `process`): applies queued selections, keeps within the voice
  limits, renders between events at their exact samples, and handles notes, chokes, MIDI (notes,
  poly and channel pressure, pitch bend, CC 1, CC 64 sustain, CC 120 and 123) and note
  expressions (pressure, volume). It needs a main output of at least two channels. Only
  core-space events are read.
- `amplitude(voice, offset)`: the voice's gain at `offset` frames into the segment: its volume
  expression and choke fade. Multiply each sample by it; a voice that ignores it still ends when
  its fade would have, but with a cut. A choke (the host's, or a voice given up) fades over 3 ms.
- `followControls(voice)`, once per sample per voice: moves its `bend` (-1 to 1 of your range) and
  `modWheel` (0 to 1) smoothly toward its channel's (a 5 ms time constant), held and released voices
  alike. Per-note bend (`CLAP_NOTE_EXPRESSION_TUNING`, MPE) is not applied.
- `setSampleRate(rate)`: call it from `activate`, which keeps the choke and the glide in time.
- `resetVoices()`: ends every voice now (call it from `activate` and `reset`). The host takes a
  reset as ending every note, so no `NOTE_END` is needed for them.
- `finishVoice(v)`: ends a voice, sends its `NOTE_END` and unpins its context.
- `applySelections(out)`: call it from the params extension's `flush` too.
- `contexts`, `selected[16]`, `voices`, `freeSlot()`, `publish()`, `retiring(slot)`, `ringOut`.
- The extension functions, to put in your tables: `capacity`, `get`, `select`, `discard`,
  `saveSnapshot`, `loadSnapshot`, `preparePreset`, `prepareSnapshot`, `setRingOut`,
  `chokeRetiring`, `saveSound`, `soundValues`, `honorsVolume`, `setLimits`, `activeVoices`,
  `voiceNotes`. Build the tables as plain globals in your own translation unit, not template
  statics shared across modules.

## Rules it implements

- **Selections** are queued (up to 32) and applied at the next process boundary, before notes; a
  queued or selected sound and each voice pin their context, and `discard` refuses a pinned one.
  Slot 0, the Default, is never discarded. IDs are never reused.
- **Release:** a note-off with an ID releases that note; raw MIDI with no IDs releases the oldest
  held voice on the key first. Sustain holds released voices; a wildcard release also releases
  pedal-held voices.
- **Snapshots:** the text format is `stateVersion`, then a line per slot (`0`, or `1`, the quoted
  name and the patch), then each channel's slot. A load validates everything first (refusing it
  changes nothing and does not call `restored`); a load that succeeds resets every context's pins
  and ends every voice, so in `restored()` forget any pins the instrument holds itself (a ringing
  tail's), not in `snapshotLoading(true)`. `prepareSnapshot` is all or nothing and never takes a
  slot in use.
- **When the pool is full:** a new note cuts a voice already fading (the furthest gone) first,
  then the voice the voice-budget order picks, then the oldest. The Engine uses this order with or
  without the extension.

## Ringing out

While `Engine::ringOut` is set (the host gave a buffer for this `process`), `render` adds the main
output of every context for which `retiring(slot)` is not null into the two channels it returns,
instead of the main port: `ringOut[0..1]` for a sound no channel selects, `ringOut[2..3]` for one
`chokeRetiring` is fading. Other ports are unchanged. The Engine clears `ringOut` when each
`process` returns. A plugin with tails outside its voices (an instrument's own delay), which the
Engine cannot choke, fades them itself in that context while `retiring(slot)` returns
`ringOut + 2`.

```cpp
// in render, per context c with voices, per sample i:
if (float* const* ring = retiring(c)) { ring[0][i] += left; ring[1][i] += right; }
else { mainL[i] += left; mainR[i] += right; }
```

## Voice budget

Return `oscairn_plugin_voice_budget_t{Engine::honorsVolume, Engine::setLimits,
Engine::activeVoices, Engine::voiceNotes}` from `get_extension(OSCAIRN_EXT_VOICE_BUDGET)`. The
Engine then keeps within the voices the host gives and each context's limit, reserve, priority and
mode, giving voices up in [the order](voice-budget.md#the-order-to-give-voices-up-in): a voice past
a limit fades out over the choke time; one is cut only when the pool has no free slot, a voice
already fading first. `voiceNotes` reports each note's voices with `loudness()`.

## Presets

Implement `readPreset` and set `prepare_preset` to `Engine::preparePreset`: it checks the name (at
most 63 bytes) and the location, takes a free slot, reads the preset into `Patch{}`, calls
`buildPreset` and publishes the sound.

## Minimal example

The skeleton of an instrument (see Oscairn's Basic Synth for a whole one):

```cpp
#include <oscairn-clap/sound-contexts-plugin.hpp>

struct Voice : oscairn::contexts::VoiceBase { double phase = 0, increment = 0, level = 0; };
struct Patch { double gain = 0.5; };

struct Synth : oscairn::contexts::Engine<Synth, Voice, Patch>
{
    static constexpr const char* stateVersion = "example-synth-1";
    static constexpr size_t maxStateBytes = 16384;
    static constexpr bool volumeExpressions = true;
    double rate = 48000;

    bool accepts(const Patch&, const clap_event_note_t&) { return true; }
    void start(Voice& v, Context&) { v.increment = 440 * std::pow(2.0, (v.key - 69) / 12.0) / rate; v.level = 1; }
    void release(Voice& v) { v.level = std::min(v.level, 0.999); }
    void applyParam(const clap_event_header_t*) {}
    void selectedGlobally(Context&, const clap_output_events_t*) {}
    void restored() {}
    bool writePatch(std::ostream& out, const Patch& p) { out << ' ' << p.gain; return true; }
    bool readPatch(std::istream& in, Patch& p) { return bool(in >> p.gain) && p.gain >= 0 && p.gain <= 1; }
    void render(const clap_process_t* process, uint32_t from, uint32_t to)
    {
        float* l = process->audio_outputs[0].data32[0]; float* r = process->audio_outputs[0].data32[1];
        for (uint32_t i = from; i < to; ++i) {
            l[i] = r[i] = 0;
            for (auto& v : voices) {
                if (!v.active) continue;
                if (!v.held && (v.level *= 0.9995) < 1e-4) { finishVoice(v); continue; }
                const double y = std::sin(6.283185307 * v.phase) * v.level * v.velocity
                               * contexts[v.context].patch.gain * amplitude(v, i - from);
                v.phase += v.increment; v.phase -= std::floor(v.phase);
                if (float* const* ring = retiring(v.context)) { ring[0][i] += float(y); ring[1][i] += float(y); }
                else { l[i] += float(y); r[i] += float(y); }
            }
        }
    }
    static uint32_t prepare(const clap_plugin_t* p, const char* name, const oscairn_sound_parameter_t*, uint32_t count,
                            const char* patch, const oscairn_asset_ref_t* base, char*, uint32_t)
    {
        if (patch || base || count || !name || std::strlen(name) > 63) return CLAP_INVALID_ID;
        auto& s = self(p); auto* c = s.freeSlot();
        return c ? (c->patch = Patch{}, s.publish(*c, name)) : CLAP_INVALID_ID;
    }
};
// activate: s.rate = rate; s.setSampleRate(rate); s.resetVoices();   reset: s.resetVoices();
// process: Synth::process.   Tables: see sound-contexts.md and sound-snapshots.md.
```

## Testing

Test the instrument as [sound-contexts](sound-contexts.md#testing) and
[sound-snapshots](sound-snapshots.md#testing) describe. In particular: a snapshot save and load
round trip with several sounds; a refused load changing nothing; `discard` refused while a voice
rings; `NOTE_END` once per note, also for a note `accepts` refused; a choke fading over 3 ms; and
that the output does not depend on the block size.
