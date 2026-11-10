# voice-budget — `oscairn.voice-budget` draft 1

A four-voice sine pool that lets the host share one processing budget across
plugins: `notes()` reports what it plays, `active_voices()` the plugin's cost, and
`set_limits()` tells it between blocks how many voices it may keep. The plugin gives
voices up itself — never a held note's only voice — and *fades* what it gives up
instead of cutting. The SDK helper `voice-budget-plugin.hpp` decides the steal
order; the plugin owns the voices and the fades.

**Build:** `cmake --build build --target oscairn-example-voice-budget`

**In Oscairn:** with many instruments playing, raise the load: this one drops its
quietest/oldest voices smoothly when the host tightens the budget, and picks them
back up when it loosens.

**Self-test:** `ctest --test-dir build -R oscairn-example-voice-budget` —
limit 2 honored while 6 notes are held, fades (not cuts), released voices stop
counting, idle instance reports sleep.
