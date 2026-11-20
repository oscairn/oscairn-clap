# voice-budget-plugin.hpp: the voice budget for any engine

| | |
| --- | --- |
| Header | [`voice-budget-plugin.hpp`](../include/oscairn-clap/voice-budget-plugin.hpp) (C++17, header-only, MIT) |
| Implements | The plugin side of [`oscairn.voice-budget/draft-2`](voice-budget.md) |
| Example | [`examples/voice-budget/`](../examples/voice-budget/) |

The settings the host gives (`Limits`), the order to give voices up in (`victim`), the report
of each note's loudness (`Report`) and of each context's least important voices (`Importance`). No allocation; everything here runs on the audio thread. An
instrument built on [`sound-contexts-plugin.hpp`](sound-contexts-plugin.md#voice-budget) gets all
of it from its Engine and needs nothing from this page. Use it directly in any other engine, such
as one playing several regions or elements a note.

## `oscairn::voices::Limits`

- `limit`: voices for the instance (0: as many as the pool holds); `contexts` and `count`: the
  entries `set_limits` gave (at most 16).
- `set(limit, list, n)`: call it from `set_limits` (never during `process`).
- `of(id, selected)`: a context's settings: its own entry; else, for a context a channel selects
  (`selected` true), the `OSCAIRN_VOICE_BUDGET_SELECTED` entry; else none (no limit or reserve,
  priority 0). **An engine without sound contexts calls `of(0, true)`**: Oscairn sends it only the
  `SELECTED` entry.

## `oscairn::voices::Candidate`

A voice as the order sees it, your unit of giving up: `context`, `selected` (a channel selects its
context), `held` (key down), `ending` (already fading out, so giving it up frees nothing new), `mono`
(the plugin's own sound plays mono, whatever the host says), `loudness` (linear, now: envelope ×
velocity × volume expression × the sound's output level, no pass over its samples), `age` (larger is
newer; it MUST be a note-on serial unique to each note a slot holds, never a frame stamp or a time), `pedaled` (key up, a pedal holding it), `ageMs` (since its note-on, from your frame count;
below 0 when you keep no time) and `weight` (its cost against your other voices; default 1).

## `int oscairn::voices::victim(count, get, limits, incoming, only = none)`

The voice to give up among `count` voices, where `get(i)` returns a `const Candidate*` or nullptr
for a free slot: for a new voice in context `incoming` (or `oscairn::voices::none` when no new voice
waits), and from context `only` alone when it is not `none` (to keep that context within its own
limit). Returns -1 when every voice is protected or ending. It applies
[the order](voice-budget.md#the-order-to-give-voices-up-in): never a held voice of a mono or legato
context, nor one of a context at or under its reserve unless the new voice is that context's;
by tier (released or inaudible, then pedaled, then key held); the new voice's own context first;
lower priority first; one struck under 50 ms ago last in its tier; the least audible, then the
oldest. It gives voices up one at a time. An engine that can give up one layer of a note alone may
keep each layer a Candidate with its own loudness, so a layered note loses its quietest layer
before another note loses its only voice; one that gives a note up only whole (unison copies,
layers it cannot drop alone) makes the note one Candidate, its `weight` the engine voices or layers
it runs, and counts its limits and `active_voices` in notes. Past 64 distinct contexts, the reserve count shares its last slot (no Oscairn plugin has
that many).

How to use it:

- When a new voice is needed, describe each voice and call `victim`. At a block's start while more
  voices sound than `limits.limit` (or a context's own limit) allows, call `Importance::next`
  instead (below), so what you listed goes first.
- Fade the voice it names out over 2 to 5 ms rather than cutting it, unless the pool has no free
  slot at all (then cut, a voice already fading first).

## `oscairn::voices::Importance<Contexts = 16>`

Draft 2's report. After each block, `list(count, get, limits)` over the same candidates `victim()`
sees, then `write(out, capacity)` from `importance`. One pass, no sort: each voice's key is worked
out once (one log10) and kept only if it beats the 16th kept (about 1 µs a block for 128 voices on
an Apple M-series core). Past its reserve a context lists only the held voices beyond it.
`truncated` says the last `list` or `write` left a context out (past `Contexts`, or past the host's
capacity).

`next(count, get, limits, only = none)`: the voice to give up at a block's start, past a lowered
limit: the first voice the last `list` named that still sounds in its slot (the same `age`, which is
why `age` must be a note-on serial, and the same context, not ending, not kept
by rule 1 now), a context's in listed order, across contexts the first of each by `victim`'s order (tier, then priority, then the key);
when none is left, `victim`'s. So a context the host lowers by n gives up exactly the n it listed
first, though the order has moved since; a listed voice that ended or whose slot a new note took
is skipped. In a debug build (no `NDEBUG`) it asserts when the voice in a listed slot has the listed
`age` and context but is younger than the listed one: another note given the same `age`. It cannot
catch that when you keep no time (`ageMs` below 0), nor when the two notes started on the same
frame, so the serial is yours to keep.

## `oscairn::voices::Report<Capacity>`

`add(noteId, port, channel, key, loudness)` for every sounding voice: voices of the same note
address are counted together and the loudest kept. `write(out, capacity)` copies the notes out for
`notes` and returns how many. Notes past `Capacity` are left out.

## Minimal example

See [voice-budget](voice-budget.md#minimal-example).

## Testing

`victim` is a pure function: test your Candidate mapping with fixed voices (two held in a context
at its reserve, one released) and check which index it returns for each `incoming`. Then test the
plugin as [voice-budget](voice-budget.md#testing) describes.
