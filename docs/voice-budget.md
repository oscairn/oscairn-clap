# oscairn.voice-budget: a plugin that gives up its own voices

| | |
| --- | --- |
| ID | `OSCAIRN_EXT_VOICE_BUDGET` = `"oscairn.voice-budget/draft-1"` (plugin extension) |
| Header | [`voice-budget.h`](../include/oscairn-clap/voice-budget.h) |
| SDK helpers | [`voice-budget-plugin.hpp`](voice-budget-plugin.md) (any engine), [`sound-contexts-plugin.hpp`](sound-contexts-plugin.md#voice-budget) (built in) |
| Design | Oscairn [RFC 0018](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0018-voice-budget-and-stealing.md#step-4-status) |
| Example | [`examples/voice-budget/`](../examples/voice-budget/) |

With a CPU budget set, Oscairn shares it among every plugin by measured cost. For a plugin without
this extension it fades out and chokes whole notes, released and quiet ones first. A plugin with
it gives up its own voices instead, within a limit the host sets between blocks: an instrument
that can give up one voice of a note (a layer, an element, a unison copy, one region of a sampled
sound) loses its least audible voice rather than a whole note.

A **voice** is whatever the plugin renders and can give up on its own. A note may sound with
several; giving one up need not end the note. The plugin fades what it gives up (2 to 5 ms),
never cuts it, except when its pool has no free slot at all, a voice already fading first.

## Who implements what

| Side | Implements |
| --- | --- |
| Plugin | `oscairn_plugin_voice_budget_t`: `honors_volume`, `set_limits`, `active_voices`, `notes`, all required (no null entries) |
| Host | Calls them; nothing for the plugin to call |

A table with a null entry is refused at load ("Invalid voice-budget interface").

## Functions

| Function | Thread and time | Does |
| --- | --- | --- |
| `bool honors_volume(plugin)` | main, once after `init` (in a worker, once at load) | True when every voice follows `CLAP_NOTE_EXPRESSION_VOLUME`, so the host's own 5 ms fade before a choke works. Under pressure the host fades a held note of a plugin that says so before one that does not |
| `void set_limits(plugin, limit, contexts, count)` | audio, **between blocks, never during `process`**, only when something changed | Keep at most `limit` voices from the next `process` on (0: as many as the pool holds), and per sound context the settings below. Copy what you need (at most `OSCAIRN_VOICE_BUDGET_MAX_CONTEXTS`, 16, entries). At the next block's start, fade out voices past a lowered limit, in the order below; a new note past it takes a voice the same way |
| `uint32_t active_voices(plugin, double* voice_cost)` | audio, after every `process` | The voices sounding now, fading ones included, and your estimate of one voice's cost in microseconds a block at the current block size (0: none). The host measures the block's time either way and shares it per voice rather than per note |
| `uint32_t notes(plugin, oscairn_voice_budget_note_t* notes, capacity)` | audio, after every `process` | Each note still sounding, by the address the host gave it (note ID, port, channel, key), with its voices and its loudness now: the largest of its voices' gains, linear, 1 at full level (envelope, velocity and volume expression; not the patch's output level). Write at most `capacity`, return how many. The host compares notes across plugins by it |

Send `CLAP_EVENT_NOTE_END` when a note's last voice ends, not each voice's.

### Each context's settings

`oscairn_voice_budget_context_t`: `context` (a sound context's ID, or
`OSCAIRN_VOICE_BUDGET_SELECTED` for every context a MIDI channel selects, apart from those named by
ID), `limit` (its own; 0 none), `reserve` (voices never given up for another context's), `priority`
(0 to 10, lower gives way first) and `flags` (`OSCAIRN_VOICE_BUDGET_MONO`, `_LEGATO`). A context
named by no entry has no limit or reserve and priority 0.

**What Oscairn sends**, today:

- An instance one part plays alone gets **one entry, for `OSCAIRN_VOICE_BUDGET_SELECTED`**, with the
  part's reserve and priority. It also covers a Performance change (the outgoing sounds ringing out
  beside the incoming ones). A plugin **without sound contexts reads this entry**, not context 0's
  (on the SDK, `Limits::of(0, true)`).
- Parts sharing one instance ([sound-snapshots](sound-snapshots.md#parts-sharing-one-instance)) get
  one entry each, for the sound their channel selects, with that part's reserve and priority, so
  one pool across parts keeps each part's reserve.
- No entry carries a `limit` of its own (the host keeps a part's note limit itself) or flags (parts
  have no mono or legato mode yet).
- `set_limits` is called only while a CPU budget is set and the plugin takes CLAP notes from its
  part. The instance limit falls by at most a quarter a block, and never below 1.

## The order to give voices up in

1. Never a held voice of a context marked mono or legato (its one key, a legato line's included),
   nor a held voice of a context at or under its reserve, unless the new voice is for that same
   context.
2. Released voices (key up, pedal or not) before held ones.
3. The new voice's own context before others.
4. The context of lower priority before higher.
5. The quietest, then the oldest.

A plugin may refine this (one element of a layered note before a whole note) but should not break
rule 1. [`voice-budget-plugin.hpp`](voice-budget-plugin.md) implements it.

## What the host does with the report

The engine keeps at most 512 notes of a report and 65,536 voices, and bounds the cost estimate.
Each note's loudness stands for its velocity when the shared budget compares notes across
plugins. A plugin that stays over its limit for more than 16 blocks in a row has its notes stolen
by the host like any plugin's. The extension works the same for a plugin in a worker process:
the limits and the report ride each block's shared memory, with the same threads and timing.

## Without it

The host steals whole notes under pressure: it lowers a note's volume expression to silence over
5 ms in eight steps, then chokes it, released and quiet notes first, preferring a plugin that
honors the volume expression. Each note is costed by the plugin's measured time per block,
shared among the notes it holds until their `NOTE_END`, so a plugin that sends `NOTE_END` late is
charged for voices it no longer plays. The host steals only CLAP notes, never a MIDI-only plugin's.
A plugin that ignores `NOTE_CHOKE` and the volume expression clicks.

## Minimal example

On `voice-budget-plugin.hpp`, for an engine without sound contexts:

```cpp
#include <oscairn-clap/voice-budget-plugin.hpp>

oscairn::voices::Limits limits; // audio thread

static bool honorsVolume(const clap_plugin_t*) { return true; }
static void setLimits(const clap_plugin_t* p, uint32_t limit, const oscairn_voice_budget_context_t* c, uint32_t n)
{ self(p).limits.set(limit, c, n); }
static uint32_t activeVoices(const clap_plugin_t* p, double* cost) { *cost = 0; return self(p).sounding(); }
static uint32_t notes(const clap_plugin_t* p, oscairn_voice_budget_note_t* out, uint32_t capacity)
{
    oscairn::voices::Report<128> report;
    for (const auto& v : self(p).voices)
        if (v.active) report.add(v.noteId, v.port, v.channel, v.key, v.gain);
    return report.write(out, capacity);
}
static const oscairn_plugin_voice_budget_t voiceBudget{honorsVolume, setLimits, activeVoices, notes};

// At a block's start, and when a new voice needs a slot:
// while (limits.limit && sounding() > limits.limit) fadeOut(victim());
int victim()
{
    std::array<oscairn::voices::Candidate, 128> c{};
    for (size_t i = 0; i < voices.size(); ++i)
        c[i] = {0, true, voices[i].held, voices[i].fading, false, voices[i].gain, voices[i].started};
    return oscairn::voices::victim(voices.size(),
        [&](size_t i) { return voices[i].active ? &c[i] : nullptr; }, limits, oscairn::voices::none);
}
```

An instrument on `sound-contexts-plugin.hpp` returns
`{Engine::honorsVolume, Engine::setLimits, Engine::activeVoices, Engine::voiceNotes}` instead.

## Testing

- With a mock host, call `set_limits(4, {SELECTED entry}, 1)` between blocks while eight voices
  sound; after the next block, `active_voices` must count the fading ones and, once they end, no
  more than four; released voices must go first.
- Check `notes` reports one entry per note with the voice count and a loudness that falls with the
  envelope, and that `NOTE_END` comes once per note.
- In Oscairn, set a CPU budget (`budget.set`) and play more notes than it allows.
- Oscairn's tests: `voice-extension` and `voice-extension-workers` (Basic Synth and a layered test
  plugin, `core/engine/tests/voice_fixture.cpp`), `voice-budget` (host-side stealing),
  `shared-engine` (per-part entries).
