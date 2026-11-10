# voice-budget — `oscairn.voice-budget` draft 1

A four-voice sine that lets the host share one processing budget across plugins: `notes()`
reports what it plays and how loud, `active_voices()` how many voices sound and an estimate
of one's cost, and `set_limits()` tells it, between blocks, how many it may keep. It gives
voices up itself at the next block's start, in the order `voice-budget.h` gives (which the
SDK helper `voice-budget-plugin.hpp` decides), and fades them over 3 ms instead of cutting.
Every voice follows the volume note expression, so `honors_volume()` answers true.

**Build:** `cmake --build build --target oscairn-example-voice-budget`

**In Oscairn:** with many instruments playing, raise the load: this one fades out its
quietest and oldest voices when the host tightens the budget.

**Self-test:** `ctest --test-dir build -R oscairn-example-voice-budget` — six notes on four
voices reported with their loudness; under a limit of 2 the voices given up still fade 64
frames on and are gone a block later; released voices stop counting; an idle instance
sleeps; a volume expression of 0 silences its note.
