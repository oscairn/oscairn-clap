# harmony — `oscairn.harmony`

The key and scale of the part a plugin is in, and the chord held at the part's input. Two
readings: events, by asking CLAP's event registry for the space `oscairn.harmony` (one in
the first block and wherever the harmony changes, at the sample of the change); and the
host extension `oscairn.harmony/draft` on the main thread, the last harmony the host sent.
This plays one sine per held key, and reads the host extension at activate, since no event
comes until the harmony changes. Its state is its Level parameter only.

**Build:** `cmake --build build --target oscairn-example-harmony`

**In Oscairn:** put it on a part and play chords on the part's input: it sounds the keys
held, from the harmony events, not from its own notes.

**Self-test:** `ctest --test-dir build -R oscairn-example-harmony` — asleep with nothing
held, sounds while keys are held, keeps the last harmony between events and through a
reset, resumes the host's reading after a new activation, stops when the keys let go.
