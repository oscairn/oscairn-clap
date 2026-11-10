# oscairn.effect-contexts: many effect slots on one engine

| | |
| --- | --- |
| ID | `OSCAIRN_EXT_EFFECT_CONTEXTS` = `"oscairn.effect-contexts/draft"` (plugin extension) |
| Header | [`effect-contexts.h`](../include/oscairn-clap/effect-contexts.h) |
| Design | Oscairn [RFC 0007](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0007-effect-contexts.md) |
| Example | [`examples/effect-contexts/`](../examples/effect-contexts/) |

An effect engine that can run many independent slots sharing its memory (tables, a convolution
kernel, a sample pool) offers each slot as a context: a separately scheduled CLAP plugin interface,
with its own parameters, state, history, host callbacks and lifecycle, made from an instance
already playing. Oscairn uses it for several sends or inserts of one effect, and at a Performance
change: the next Performance's effects become new contexts of an engine already playing while the
old ones ring out, so an engine outlives its first context.

## Who implements what

| Side | Implements |
| --- | --- |
| Plugin | `oscairn_plugin_effect_contexts_t`: `create` and `resources`, both required |
| Host | Calls them, and runs each context as a plugin of its own |

## Functions

### `const clap_plugin_t* create(plugin, const clap_host_t* host)`

**Main thread**, safe while existing contexts render. Returns a new, **uninitialized** context
owned by the caller, with `host` as its host; the normal CLAP rules follow (`init`, `activate`,
`destroy`). Null when it cannot: past the engine's capacity it must refuse. A failure leaves
existing contexts untouched.

Oscairn then initializes the context with the parent's sample rate and block size and no state,
asks its extensions (and `oscairn.memory`'s `estimate`), activates it, and loads the node's saved
state into it when it has one.

### `bool resources(plugin, oscairn_effect_resources_t* out)`

Any context: the engine's `shared_bytes` (counted once per engine), this context's
`context_bytes`, the engine's `capacity` and the number `prepared` (both including the
originating context), and `tail_active`, whether this context still has a tail (an atomic
snapshot, not permission to destroy a live audio plan). False: the host treats the plugin as
having no contexts.

Oscairn asks it from the main thread at load and whenever it plans a Performance change (whether
the engine has room: `capacity >= prepared + the contexts it needs`), for budgets, and for
`effects.resources` over JSON-RPC. Answer it cheaply and without locking against the audio thread.

## Rules

- All contexts use the same sample rate. The host never processes two contexts of one engine at
  the same time (Oscairn renders them in sequence, on one thread at a time, also in a worker).
- Children keep the engine's resources alive after the originating interface is destroyed; the
  host keeps the module loaded.
- The host retires every audio plan that refers to an interface before destroying it.
- Destroying a context cuts its tail. A host that wants it keeps feeding silence until
  `tail_active` is false (Oscairn does, at a Performance change).
- No engine address or slot is a persistent identity: state belongs to each context.
- A context's memory: without `oscairn.memory` (or when its `estimate` returns false), Oscairn
  counts the engine at `resources`' `shared_bytes` + `context_bytes` and each other context at
  its `context_bytes`. With `oscairn.memory`, a new context is checked and counted at its declared
  `context_bytes` + `asset_bytes`.

## Errors

"Plugin has no native effect contexts" (`create` or `resources` null, or asked of a plugin
without the extension), "Effect context capacity exhausted" (`create` returned null), "Plugin threw
preparing an effect context". In a worker process the contexts live in the root's worker; if
`resources` returned false when the root loaded, the worker refuses to make contexts.

## Without it

Every effect node is an instance of its own: a new instance is loaded (or a free one taken over)
for each slot and at each Performance change. A Performance saved with contexts of a shared engine
opens with stand-ins for the contexts when the engine's plugin lacks the extension ("its shared
engine is not here").

## Minimal example

```cpp
#include <oscairn-clap/effect-contexts.h>

struct Engine { std::vector<float> table; uint32_t contexts = 0; };
constexpr uint32_t capacity = 16;

static const clap_plugin_t* createContext(const clap_plugin_t* p, const clap_host_t* host)
{
    auto engine = self(p).engine; // a std::shared_ptr<Engine> each context holds
    if (!engine || engine->contexts >= capacity) return nullptr;
    return makePlugin(host, std::move(engine)); // ++contexts; uninitialized, like the factory's create
}
static bool resources(const clap_plugin_t* p, oscairn_effect_resources_t* out)
{
    const auto& s = self(p);
    if (!s.engine) return false;
    *out = {s.engine->table.size() * sizeof(float), sizeof(s) + s.line.capacity() * sizeof(float),
            capacity, s.engine->contexts, s.tailActive.load()};
    return true;
}
static const oscairn_plugin_effect_contexts_t effectContexts{createContext, resources};
```

## Testing

- Create contexts until `create` returns null and check that `prepared` counts them and that
  each renders independently (different parameters, its own tail); destroy the originating
  interface first and check the children still render.
- In Oscairn, `effects.create` makes a context of a node over JSON-RPC and `effects.resources`
  reports the engine; `oscairn-plugin-check PLUGIN ID effect` checks the plugin loads, plays and
  saves (it does not make contexts).
- Oscairn's tests: `effect-contexts` (with Oscairn Delay, `plugins/delay/delay.cpp`, the
  reference), `control-native-effects`.
