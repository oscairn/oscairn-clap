# beat — `oscairn.beat`

A beat position that keeps running while the transport is stopped, the same for every
plugin in a block, so arpeggiators and scripts keep time and start together. The plugin
asks CLAP's event registry for the space `oscairn.beat` and gets an `oscairn_event_beat`
(position and tempo) at sample 0 of every block. In the later process calls of a block the
host splits, the position moves on at the event's tempo. Here it clicks on every beat.
Its state is its Level parameter only: the position is the host's.

**Build:** `cmake --build build --target oscairn-example-beat`

**In Oscairn:** load it with the transport stopped: it keeps clicking at the tempo, and
through starts, stops and seeks.

**Self-test:** `ctest --test-dir build -R oscairn-example-beat` — silent and asleep before
the first event; the click lands on the frame where the position crosses the next beat at
the event's tempo, through blocks without an event; a new reading replaces the position.
