# oscairn.beat: a free-running beat

| | |
| --- | --- |
| ID | Event space `OSCAIRN_EVENT_SPACE_BEAT` = `"oscairn.beat"` (draft), event type `OSCAIRN_EVENT_BEAT` = 0 |
| Header | [`beat.h`](../include/oscairn-clap/beat.h) |
| Design | Oscairn [RFC 0032](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0032-arpeggiator.md#step-3-status) |
| Example | [`examples/beat/`](../examples/beat/) |

CLAP's transport stops moving when the transport stops. An arpeggiator's sync quantize, a
script's clock or a looper still needs to agree with the others on "the next beat" while the
transport is stopped. `oscairn.beat` gives every plugin that asks for it the same beat position,
which runs at the transport's tempo from the moment the host starts, through starts, stops, jumps
and Performance changes, and never goes back. Its origin means nothing; use it to agree, for
example on the next whole beat.

## Who implements what

| Side | Implements |
| --- | --- |
| Plugin | Queries the event space through CLAP's `clap.event-registry` host extension; reads `oscairn_event_beat_t` events in `process` |
| Host | Answers the query and sends the event |

## Calls and events

- **`clap_host_event_registry.query(host, "oscairn.beat", &space)`**, main thread, as CLAP's
  event registry is: call it in `init` (or later, on the main thread). It returns true and the
  space ID. Asking is what turns the events on: Oscairn sends them only to a plugin that has
  queried the space. A host without the space returns false; then the plugin gets no beat and
  should fall back to the transport.
- **`oscairn_event_beat_t`**, audio thread, in `process`'s input events: one at sample 0 of every
  block, before the block's other events, with `header.space_id` the space the query gave,
  `header.type` `OSCAIRN_EVENT_BEAT`, and:
  - `beats`: the free-running position at `header.time`, in `CLAP_BEATTIME_FACTOR` units;
  - `tempo`: the beats per minute it moves at through this block: the transport's, or 120 when
    the transport has no tempo; always finite and above 0.

  Oscairn sends it from its graph (parts and routing; a plain engine `Chain` sends none).
  A plugin that asked for it is processed every block, never skipped as asleep.

  When the host splits a block into several `process` calls, only the first carries it: the
  position moves on at `tempo` from there. Count it forward yourself between events.
- A plugin must not send events in this space: Oscairn refuses them (`try_push` returns false).

## Without it

A plugin that does not ask gets no beat events, and nothing else changes. It has CLAP's transport
(`clap_event_transport`), which while stopped does not move. A plugin that asks a host without
the space gets false from the query.

## Minimal example

```cpp
#include <oscairn-clap/beat.h>

uint16_t beatSpace = UINT16_MAX;
double beat = -1, beatsPerSample = 0, rate = 48000;

bool init(const clap_plugin_t* p) // and keep the host from create()
{
    auto* registry = static_cast<const clap_host_event_registry_t*>(host->get_extension(host, CLAP_EXT_EVENT_REGISTRY));
    if (!registry || !registry->query(host, OSCAIRN_EVENT_SPACE_BEAT, &beatSpace)) beatSpace = UINT16_MAX;
    return true;
}

// in process, for each input event h:
if (h->space_id == beatSpace && h->type == OSCAIRN_EVENT_BEAT) {
    const auto& e = *reinterpret_cast<const oscairn_event_beat_t*>(h);
    beat = double(e.beats) / CLAP_BEATTIME_FACTOR;
    beatsPerSample = e.tempo / 60.0 / rate;
}
// per sample: a whole beat starts where floor(beat) changes; then beat += beatsPerSample.
```

## Testing

- With a mock host, answer the query with a space ID, send an event at sample 0 with a known
  `beats` and `tempo`, and check what the plugin derives (the sample of the next whole beat).
- In Oscairn, play two parts with the plugin while the transport is stopped: they keep time with
  each other. Oscairn's own test fixture is `core/engine/tests/follower_fixture.cpp`, which writes
  the beat it counts to an audio output so a test can read it.
