# sound-snapshots — `oscairn.sound-snapshots` draft 4

Instant Performance changes for a native instrument: the next Performance's sounds
are prepared *beside* the ones still playing (`prepare` from snapshot state), then
selected at the cue. Retired sounds ring out into a hidden four-channel ring-out
buffer the host hands `ring_out()` for one process(), and `choke_retiring()` fades
them. Same engine as `../sound-contexts`, plus one table and three entry points.

**Build:** `cmake --build build --target oscairn-example-sound-snapshots`

**In Oscairn:** switch Performances while this instrument plays: notes started
before the cue keep their old sound and fade out instead of being cut.

**Self-test:** `ctest --test-dir build -R oscairn-example-sound-snapshots` —
snapshot prepares beside the playing sound with fresh IDs, the held voice stays
with its original sound, and after the cue the faded tail arrives in the ring-out
buffer while the new sound plays on the main port.
