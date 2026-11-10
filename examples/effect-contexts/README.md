# effect-contexts — `oscairn.effect-contexts` draft 3

One gain engine, many independent effect slots: `create()` makes a separately
scheduled interface with its own parameters, state and host callbacks while the
others render; contexts share the engine's sample rate and outlive the interface
that made them; `create` refuses past the engine's capacity. The audio is one gain
stage, so a context's sound is exactly one value.

**Build:** `cmake --build build --target oscairn-example-effect-contexts`

**In Oscairn:** put several copies of this effect on one track: each insert keeps
its own Gain and its own state, and removing one never silences the others.

**Self-test:** `ctest --test-dir build -R oscairn-example-effect-contexts` — create
two contexts while processing, per-context gain audible, capacity refusal, state
round-trips per context.
