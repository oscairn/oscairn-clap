# oscairn.harmony: the part's key, scale and chord

| | |
| --- | --- |
| ID | Event space `OSCAIRN_EVENT_SPACE_HARMONY` = `"oscairn.harmony"` (draft), event type `OSCAIRN_EVENT_HARMONY` = 0; host extension `OSCAIRN_EXT_HARMONY` = `"oscairn.harmony/draft"` |
| Header | [`harmony.h`](../include/oscairn-clap/harmony.h) |
| Design | Oscairn [RFC 0033](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0033-script-block.md#host-side-status) |
| Example | [`examples/harmony/`](../examples/harmony/) |

For plugins that follow the harmony (a harmonizer, a chord memory, a script): the key and scale of
the part the plugin is in, and the chord held on that part with its root, type, bass and keys. This
is not tuning: the scale says which notes belong to the key, not how they are tuned.

The key and scale are the Performance's, or the part's own where it overrides them. The chord is
the host's reading of the keys held at the part's input, after the part's zones (a transposing zone
transposes it), not of the notes a plugin before this one sends: an arpeggiator's output does not
change it. Sustained keys no longer held do not count.

## Who implements what

| Side | Implements |
| --- | --- |
| Plugin | Queries the event space through `clap.event-registry`; reads `oscairn_event_harmony_t` in `process`; may call the host extension |
| Host | Reads the chord, sends the events, answers `oscairn_host_harmony_t.get` |

## Calls and events

- **`clap_host_event_registry.query(host, "oscairn.harmony", &space)`**, main thread, in `init`.
  Querying turns the events on. A host without the space returns false.
- **`oscairn_event_harmony_t`**, audio thread, in `process`'s input events: in the first block the
  plugin processes, and in each block where anything in `oscairn_harmony_t` changes, at the sample
  of the change: the sample of the note that changed the chord, before that note. The host never
  sends one equal to the last it delivered (it sends again after an activation).
- **`oscairn_host_harmony_t.get(host, &harmony)`**, main thread: the harmony of the last event the
  host delivered to the plugin. False before the first. So **a plugin must query the event space
  to read the harmony through the host extension too**; one that never queries always gets false.
  It follows the plugin's part: moved to another part, the plugin reads that part's harmony from
  its next block; taken out of its part, or with its part's harmony gone, it reads false again until
  it is in a part.
- A plugin must not send events in this space; Oscairn refuses them.

## `oscairn_harmony_t`

| Field | Meaning |
| --- | --- |
| `key_root` | The key's root, 0 = C to 11 = B |
| `chord_root` | The chord's root, 0..11; 0 with no chord |
| `chord_type` | `enum oscairn_chord_type`: the intervals above the root (major `0 4 7` to minor ninth `0 2 3 7 10`), `OSCAIRN_CHORD_NONE` for fewer than two pitch classes or none of the table's |
| `chord_bass` | The lowest held key's pitch class (an inversion's bass); 0 with no keys |
| `scale` | Bit n: the pitch class n semitones above `key_root` is in the scale; bit 0 always set |
| `reserved` | 0 |
| `held[2]` | The keys held at the part's input: bit k of `held[k / 64]` for MIDI key k |

The chord follows a fixed table of the types, in the enum's order. Where the same pitch classes
read as more than one chord (C6 and Am7, the symmetric augmented and diminished sevenths), the
reading rooted on the bass wins; otherwise the first in the table. Doubled and octave-spread keys
read as their pitch classes. Oscairn validates every event it sends (roots under 12, a known chord
type, a scale under 4096 with bit 0 set).

## Without it

A plugin that does not ask gets no events and no harmony from the host extension; it can read the
notes it receives itself, but only those that reach it (after any plugin before it in the part),
and it has no key or scale. Over the control interface, `harmony.get` and `harmony.set` read and set
a Performance's key and scale.

## Minimal example

Snapping incoming notes to the part's scale:

```cpp
#include <oscairn-clap/harmony.h>

uint16_t harmonySpace = UINT16_MAX;
oscairn_harmony_t harmony{0, 0, 0, 0, 0xfff, 0, {0, 0}}; // chromatic until the first event

// in init:
auto* registry = static_cast<const clap_host_event_registry_t*>(host->get_extension(host, CLAP_EXT_EVENT_REGISTRY));
if (!registry || !registry->query(host, OSCAIRN_EVENT_SPACE_HARMONY, &harmonySpace)) harmonySpace = UINT16_MAX;

// in process, for each input event h, in order:
if (h->space_id == harmonySpace && h->type == OSCAIRN_EVENT_HARMONY)
    harmony = reinterpret_cast<const oscairn_event_harmony_t*>(h)->harmony;

int16_t snap(int16_t key)
{
    for (int down = 0; down < 12; ++down)
        if (harmony.scale >> ((key - down - harmony.key_root + 120) % 12) & 1) return int16_t(key - down);
    return key;
}
```

## Testing

- With a mock host, answer the query and send an event with a C major scale (`key_root` 0, scale
  `0xAB5`) at sample 0, then a note on C#: the plugin should play C. Send a change mid-block before a
  note and check the note follows it.
- In Oscairn, hold a chord on the part and read the plugin's state or output; `harmony.set` changes
  the key and scale.
- Oscairn's tests: `harmony` and `free-beat` with `core/engine/tests/follower_fixture.cpp`, which
  writes what it receives to its audio outputs.
