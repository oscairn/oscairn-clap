# voice-budget-plugin.hpp: the voice budget for any engine

| | |
| --- | --- |
| Header | [`voice-budget-plugin.hpp`](../include/oscairn-clap/voice-budget-plugin.hpp) (C++17, header-only, MIT) |
| Implements | The plugin side of [`oscairn.voice-budget/draft-1`](voice-budget.md) |
| Example | [`examples/voice-budget/`](../examples/voice-budget/) |

The settings the host gives (`Limits`), the order to give voices up in (`victim`), and the report
of each note's loudness (`Report`). No allocation; everything here runs on the audio thread. An
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

A voice as the order sees it: `context`, `selected` (a channel selects its context), `held` (key
down), `ending` (already fading out, so giving it up frees nothing new), `mono` (the plugin's own
sound plays mono, whatever the host says), `loudness` (linear, now) and `age` (larger is newer).

## `int oscairn::voices::victim(count, get, limits, incoming, only = none)`

The voice to give up among `count` voices, where `get(i)` returns a `const Candidate*` or nullptr
for a free slot: for a new voice in context `incoming` (or `oscairn::voices::none` when no new voice
waits), and from context `only` alone when it is not `none` (to keep that context within its own
limit). Returns -1 when every voice is protected or ending. It applies
[the order](voice-budget.md#the-order-to-give-voices-up-in): never a held voice of a mono or legato
context, nor one of a context at or under its reserve unless the new voice is that context's;
released before held; the new voice's own context first; lower priority first; the quietest, then
the oldest. It gives voices up one at a time, so keep each of a note's voices a Candidate with its
own loudness: a layered note then loses its quietest layer before another note loses its only
voice. Past 64 distinct contexts, the reserve count shares its last slot (no Oscairn plugin has
that many).

How to use it:

- When a new voice is needed, or at a block's start while more voices sound than `limits.limit`
  (or a context's own limit) allows, describe each voice and call `victim`.
- Fade the voice it names out over 2 to 5 ms rather than cutting it, unless the pool has no free
  slot at all (then cut, a voice already fading first).

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
