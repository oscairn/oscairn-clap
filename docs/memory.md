# oscairn.memory: memory declared before it is allocated

| | |
| --- | --- |
| ID | `OSCAIRN_EXT_MEMORY` = `"oscairn.memory/draft"` (plugin extension) |
| Header | [`memory.h`](../include/oscairn-clap/memory.h) |
| Design | Oscairn [RFC 0025](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0025-resource-budgets.md#declarations) |
| Example | [`examples/memory/`](../examples/memory/) |

A plugin that allocates a lot when it activates (delay lines, sample pools, reverb tables) says
how much before it does, so a device near its memory limit can refuse it before the allocation
instead of failing after it, and a worker process gets a memory limit that fits the plugin.

## Who implements what

| Side | Implements |
| --- | --- |
| Plugin | `oscairn_plugin_memory_t`: `estimate` and `current`, both required |
| Host | Calls them; nothing for the plugin to call |

## The numbers

`oscairn_memory_t` holds three byte counts of the plugin's own storage, without host or
allocator overhead:

- `shared_bytes`: once per instance, or once per engine shared by effect contexts (tables,
  engine-wide buffers, audio shared between contexts);
- `context_bytes`: each context. An instance without contexts is one context; with
  `oscairn.effect-contexts` or `oscairn.sound-contexts` the originating interface is one too;
- `asset_bytes`: decoded assets (samples) the current state uses.

An instance's total is the sum of the three. Every node of an engine, the originating interface
and each effect context made from it, reports the engine's same `shared_bytes`: the host counts
it once, and keeps counting it while any node lives, after the originating interface is destroyed
too. An engine that also offers `oscairn.effect-contexts` reports the same shared and context
bytes through both.

## Functions

### `bool estimate(plugin, double sample_rate, uint32_t max_frames, oscairn_memory_t* out)`

**Main thread**, after `init` and after the state is loaded, before `activate`: what activating at
this rate and block size would allocate for the current state (its mode, its samples). It must
not make an allocation of that size itself. False: not known for this state; the host uses its
measurement.

Oscairn calls it before **every** activation: a load, a state load (and the reactivation after a
refused one), a new effect context, a new sample rate or block size, and a restart the plugin asks
for (`request_restart`, a latency or port change, `CLAP_PARAM_RESCAN_ALL`, or an info rescan that
changed more than info may).

### `bool current(plugin, oscairn_memory_t* out)`

**Main thread**, after `activate`: what the plugin holds now. False: not known. Oscairn requires
the function but does not call it for its budget (it counts its own measurement), and a plugin in
a worker is never asked.

## What the host does with it

- **Refusals**, before `activate`: a plugin offering one function without the other is refused at
  load ("Incomplete memory interface"). A declaration whose parts overflow when added is "Plugin's
  memory declaration is not credible". With a memory budget, a declaration over what the budget
  leaves is "Plugin needs N MiB of memory where M MiB is left". A new effect context is checked at
  its `context_bytes` plus `asset_bytes`; an instance at all three.
- **Budget:** Oscairn counts the larger of the declaration and its own measurement for an instance,
  and keeps its measurements per declared size, so a plugin's modes (a reverb's algorithms) are not
  all counted at the largest. An effect context made from another is counted at its declaration
  alone. A plugin that holds more than it declared is counted at what it holds; distrusting its
  later declarations is planned.
- **Sound contexts:** with `oscairn.sound-contexts`, `estimate` covers every sound the state holds,
  and each sound prepared after activating (`prepare`, `prepare_preset`, or a snapshot's sounds
  prepared beside) costs `context_bytes` more, a discarded one that much less, until the next
  activation's estimate. So report one sound's memory as `context_bytes` (a plugin may count the
  state's other sounds in `shared_bytes`). The host refuses a prepare whose context bytes are more
  than the memory the budget leaves before calling the plugin, and a snapshot whose prepared sounds'
  bytes are, discarding them again.
- **In a worker process** (one plugin instance per process, Oscairn's desktop): when every plugin
  in the worker declares, the worker's memory limit is what they declare (the largest
  `shared_bytes` once, plus each node's context and asset bytes) plus half again and 64 MiB,
  instead of the flat limit (4 GiB by default). Before each activation the worker sends the
  engine the new declaration and waits while the limit is raised, so a mode that needs more is
  allowed it before it allocates; a smaller mode lowers it afterwards, no further than what the
  worker then holds plus 64 MiB. Each sound context prepared after activating raises the limit by
  its context bytes before the plugin prepares it. A declaration over four times the flat limit
  (1 TiB with none) is refused before the plugin activates or prepares. Past the limit the worker
  is stopped like a crash.

## Without it

The host measures the plugin: in a worker, what the worker process holds (physical footprint on
macOS, resident memory on Linux, read every 10 ms); in the engine's process, what the process grew
by while the plugin loaded. A plugin never measured on the device counts as 32 MiB until it is (or
at the `--estimate` its plugin package gives). A worker gets the flat memory limit. The plugin is
never refused before it allocates; one that allocates past the budget is found afterwards. An
effect engine without `oscairn.memory` is counted from its `oscairn.effect-contexts` `resources`
instead.

## Minimal example

A delay whose line is two seconds at the sample rate:

```cpp
#include <oscairn-clap/memory.h>

static bool estimate(const clap_plugin_t*, double rate, uint32_t, oscairn_memory_t* out)
{
    *out = {0, uint64_t(2.0 * rate + 1) * 2 * sizeof(float) + sizeof(Delay), 0};
    return true;
}
static bool current(const clap_plugin_t* p, oscairn_memory_t* out)
{
    const auto& d = self(p);
    *out = {0, (d.left.capacity() + d.right.capacity()) * sizeof(float) + sizeof(Delay), 0};
    return true;
}
static const oscairn_plugin_memory_t memory{estimate, current};
// in get_extension:
if (!strcmp(id, OSCAIRN_EXT_MEMORY)) return &memory;
```

## Testing

- Check that `estimate` before `activate` matches `current` after it, at several sample rates
  and in each mode, and that `estimate` allocates nothing large (measure the process around it).
- In Oscairn, `session.status` lists each plugin in a worker with its memory; with a memory
  budget set (`budget.set`), a plugin that declares more than is left is refused with the message
  above before it activates.
- Oscairn's tests: `kick-clap-host` (its memory fixture), `control-plugin-failures` (worker
  limits, refusals, a declaration that is not credible), `control-worker-sound-memory` (sounds
  prepared after activating).
