# effect-contexts — `oscairn.effect-contexts`

One gain engine, many independent effect slots: `create()` makes a separately scheduled
interface with its own parameters, state and host callbacks while the others render;
contexts share the engine's sample rate and keep it alive after the interface that made
them is gone; `create` refuses past the engine's capacity of 8. `resources()` reports the
bytes it holds and whether its last block carried sound (a gain keeps no other tail).

**Build:** `cmake --build build --target oscairn-example-effect-contexts`

**In Oscairn:** put several copies of this effect on one track: each insert keeps its own
Gain and its own state, and removing one never silences the others.

**Self-test:** `ctest --test-dir build -R oscairn-example-effect-contexts` — a second
context made while the first is active, each at its own gain, shared bytes reported alike,
the tail set by sound and cleared by a silent block, refusal past the capacity, and slots
freed on destroy.
