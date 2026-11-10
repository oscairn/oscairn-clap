# harmony — `oscairn.harmony`

The key and scale of the part a plugin is in, and the chord currently held on it.
Two readings: events — ask CLAP's event registry for `oscairn.harmony/draft-1` and
an `oscairn_event_harmony` arrives in the first block and whenever the harmony
changes, at the sample of the change; and the host extension `oscairn.harmony` on
the main thread, for the last harmony as of the last processed block. This plays one
sine per key held at the part's input and saves the extension's reading verbatim.

**Build:** `cmake --build build --target oscairn-example-harmony`

**In Oscairn:** put it on a part with a chord source (harmony track, arpeggiator
with a key zone): it sounds only the notes inside the part's key and follows the
held chord as it changes.

**Self-test:** `ctest --test-dir build -R oscairn-example-harmony` — asleep with
nothing held, sounds while keys are held, keeps the last harmony between events,
stops when released, and state round-trips the host's reading.
