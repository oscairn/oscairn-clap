# beat — `oscairn.beat`

A beat position that keeps running while the transport is stopped — the same for
every plugin in a block — so things like arpeggiators and scripts keep time and
start together. The plugin asks CLAP's event registry for the space
`oscairn.beat/draft-1` and receives an `oscairn_event_beat` at sample 0 of every
block: free-running position, tempo, whether the transport is playing, and whether
time jumped forwards. Between blocks the position advances at the event's tempo.
Here it gates a sine on the beat.

**Build:** `cmake --build build --target oscairn-example-beat`

**In Oscairn:** load it with the transport stopped: it keeps pulsing at the
project tempo; hits on play and keeps the pulse through stops and seeks.

**Self-test:** `ctest --test-dir build -R oscairn-example-beat` — events move the
position forward only (never back on a backward jump), the position runs on at the
event's tempo between events, and gating is audible.
