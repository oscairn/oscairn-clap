# oscairn.param-target: a plugin that changes other plugins' parameters

| | |
| --- | --- |
| ID | Event space `OSCAIRN_EVENT_SPACE_PARAM_TARGET` = `"oscairn.param-target"` (draft), event type `OSCAIRN_EVENT_PARAM_TARGET` = 0; plugin extension `OSCAIRN_EXT_PARAM_TARGET_LANES` = `"oscairn.param-target-lanes/draft"` |
| Header | [`param-target.h`](../include/oscairn-clap/param-target.h) |
| Design | Oscairn [RFC 0002](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0002-param-target.md), controls in [RFC 0011](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0011-performance-ui-and-editors.md#the-control-tree) |
| Example | [`examples/param-target/`](../examples/param-target/) |

A sequencer or modulator has numbered output **lanes**. It sends `oscairn_event_param_target_t`
events on them from `process`, and the host turns each into a parameter change for whatever the
user mapped the lane to, at the same sample, in the same block: parameter locks, LFOs, envelope
followers. The user maps a lane straight to one parameter, or makes it a control's source, and the
control then moves every target it has, each with its own range and curve, absolute or relative,
summing with the other controls on the same parameter. The plugin never knows which.

## Who implements what

| Side | Implements |
| --- | --- |
| Plugin | Queries the event space through `clap.event-registry`; sends the events; lists its lanes with `oscairn_plugin_param_target_lanes_t` (`count`, `get`) |
| Host | Answers the query, keeps the mapping, converts each event for its targets, orders the targets after the source |
| Target plugins | Nothing new: they receive `CLAP_EVENT_PARAM_VALUE` or `CLAP_EVENT_PARAM_MOD` |

## Calls and events

- **`clap_host_event_registry.query(host, "oscairn.param-target", &space)`**, main thread, in
  `init`. A host that does not know the space returns false; then send nothing.
- **Lanes:** `count(plugin)` and `get(plugin, index, info)`, **main thread**; Oscairn reads them
  when the plugin activates (at load and every restart). At most 64 are read; a `get` that
  returns false skips that lane. Each has a `lane` number and a `name`. Routing's **Connect lane
  to...** lists them and draws each connection as a control line.
- **`oscairn_event_param_target_t`**, pushed on `process`'s output events, audio thread, at the
  sample it applies: `header.space_id` the queried space, `header.type` 0, `header.size` exactly
  `sizeof(oscairn_event_param_target_t)`, `lane`, `mode`, and `note_id`, `port_index`, `channel`,
  `key` as in CLAP parameter events (-1 wildcards: `note_id`, `port_index` and `key` -1, and a
  channel or -1 for channel-wide changes), and a finite `value`. Oscairn refuses (`try_push` false)
  an event of another size, an unknown mode, a non-finite value, or one at or past the block's end.

### Modes

- **SET** (`value` 0..1, normalized over the target's range) is a position: a sequencer's lock, a
  step's value. Through a control it becomes the control's value.
- **MODULATE** (`value` -1..1 of the target's range) is movement around a position: an LFO, an
  envelope follower. Through a control it is added to the control's own value, so a bipolar lane
  swings around the knob and a unipolar one (0..1) moves up from it.

Mapped straight to a parameter, SET sets it (`CLAP_EVENT_PARAM_VALUE`) and MODULATE modulates it
(`CLAP_EVENT_PARAM_MOD`). Under a control, the target's own setting decides: an absolute target
sets the parameter; a relative one modulates it, so its value, the saved Performance and undo stay
as they are and it can still be turned by hand. Values are clamped to their range and scaled to the
target's plain units; a stepped target is rounded. Handle `CLAP_EVENT_PARAM_MOD` on modulatable
parameters for a modulator to reach them without moving them.

### Timing and limits

- The host converts each event at the same sample, in the same block, for every parameter the lane
  reaches, ordered before notes at that time, so a step's lock applies to that step's note. An
  event for one note (`note_id >= 0`) comes after the notes at its time, and reaches the target only
  when the source's notes reach it too (otherwise it is dropped and counted).
- The target is processed after the source in the same block (a mapping that makes a cycle is
  refused).
- Send at every point that matters (each step, or every few samples for a smooth shape). A plugin
  sends at most 512 events other than parameter changes a block; past that they are dropped and
  counted. A target takes at most 512 converted events a block from all the lanes reaching it; past
  that the newest replaces the last one waiting for the same parameter, and the loss is counted,
  without resetting the target.
- An event on a lane nobody mapped is ignored. Oscairn does not check mapped lane numbers against
  the declared lanes.
- What happens between points is the target's: one that applies parameter events once per block
  steps once per block. Parameter events are not an audio signal: audio-rate modulation such as FM
  needs an audio-rate path, which Oscairn does not have yet.

A plugin whose only outputs are its lanes, with no audio or note port, loads and runs as any node
(the Oscairn LFO is one). One with no audio output, no note output **and no lanes** is refused
("Plugin has neither audio nor note outputs"). In a worker process the lanes and events cross to
the engine unchanged.

## Without it

A plugin that does not ask for the space sends nothing (a host would not know the events). Without
the lanes extension, lanes can still be mapped by number through the control interface
(`graph.map`) and Performance files, but Routing lists none to pick. A plain CLAP plugin can only
change its own parameters; other plugins' parameters move only by the user's controls and scenes.

## Minimal example

An LFO on lane 0:

```cpp
#include <oscairn-clap/param-target.h>

uint16_t space = UINT16_MAX;
// in init:
auto* registry = static_cast<const clap_host_event_registry_t*>(host->get_extension(host, CLAP_EXT_EVENT_REGISTRY));
if (!registry || !registry->query(host, OSCAIRN_EVENT_SPACE_PARAM_TARGET, &space)) space = UINT16_MAX;

static const oscairn_plugin_param_target_lanes_t lanes{
    [](const clap_plugin_t*) -> uint32_t { return 1; },
    [](const clap_plugin_t*, uint32_t i, oscairn_param_target_lane_info_t* info) {
        if (i) return false;
        info->lane = 0; snprintf(info->name, sizeof info->name, "LFO");
        return true; }};

// in process, every 32 samples:
if (space != UINT16_MAX) {
    oscairn_event_param_target_t e{};
    e.header = {sizeof e, frame, space, OSCAIRN_EVENT_PARAM_TARGET, 0};
    e.lane = 0; e.mode = OSCAIRN_PARAM_TARGET_MODULATE;
    e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
    e.value = std::sin(phase);
    process->out_events->try_push(process->out_events, &e.header);
}
```

## Testing

- With a mock host, answer the query and collect the output events of a block: each has the exact
  size, its lane and mode, a finite value in range, and a time inside the block.
- In Oscairn, `graph.map` maps a lane to a node's parameter over JSON-RPC (or the desktop's
  Routing, **Connect lane to...**); `params.changed` then reports the target moving, or the sound
  changes under modulation.
- Oscairn's tests: `param-target` and `param-target-workers`, `control-source` (the LFO's lane listed
  by name), with `plugins/step-sequencer` and `plugins/lfo` as references.
