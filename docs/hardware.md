# oscairn.hardware: a plugin's backends for a device's accelerators

| | |
| --- | --- |
| ID | Host extension `OSCAIRN_EXT_HARDWARE` = `"oscairn.hardware/draft"` |
| Header | [`hardware.h`](../include/oscairn-clap/hardware.h) |
| Design | Oscairn [RFC 0004](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0004-hardware-services.md) (hardware services) and [RFC 0006](https://github.com/oscairn/oscairn/blob/main/docs/rfcs/0006-packages-and-portability.md#backend-variants-and-device-specific-dependencies) (backend variants) |
| Example | None yet: Oscairn's test plugin (`core/hardware/tests/`) uses it against a simulated accelerator |

Some devices have an accelerator beside the CPU: a DSP, an FPGA region, a dedicated sound chip
or a real-time core. A plugin with a backend for one can move work to it on those devices and
keep its CPU path everywhere else. The host lists what the device actually offers, counts
capacity across every plugin, and carries jobs to the accelerator through buffers it owns. It
never runs plugin code on the accelerator, and a plugin never touches registers or device memory.

**Draft.** The extension is new. In Oscairn today every device is CPU-only (`count()` is 0) and a
plugin in a worker process sees no accelerators; a real accelerator service and the package
resolver that installs a backend only where it is used are later RFC 0004 steps.

## Who implements what

| Side | Implements |
| --- | --- |
| Plugin | Asks for the host extension; for an accelerator whose `abi` it has a backend for, reserves, submits jobs and collects results; keeps a CPU path that matches the backend within the engine's stated tolerance |
| Host | Lists accelerators from the device's capability records, reserves capacity, carries and validates jobs, records failures |

## Calls

All in `oscairn_host_hardware_t`, from `clap_host.get_extension(host, OSCAIRN_EXT_HARDWARE)`.

1. **`count(host)`, `get(host, index, &accelerator)`, `supports(host, index, operation)`**, main
   thread: list the accelerators. Pick one whose `abi` your backend speaks. `available` false means
   it is in the device's profile but cannot be used now (no service in this build, its firmware
   missing, or failed). Capacities the device does not state are `OSCAIRN_HARDWARE_UNKNOWN`.
2. **`reserve(host, index, &request, &lease)`**, main thread, before your `activate` returns. The
   request names the operation, and any bank, exclusive region, voices, parts, memory and the
   largest job input and output. A refusal returns a status (`UNAVAILABLE`, `UNSUPPORTED`,
   `OVER_CAPACITY`, `REGION_BUSY`, `UNKNOWN_CAPACITY`); use your CPU path then.
   `cpu_fallback_us` asks the host to also reserve the CPU time your CPU path needs, so it can take
   over without an interruption; `lease.cpu_fallback` says whether it could.
3. **`submit(host, lease, &job)`** then **`collect(host, lease, &result)`**, audio thread, for the
   block being processed. A job's time is `clap_process.steady_time` plus the frame it starts at.
   With `pipeline_frames` 0 the result is ready in the same `process` call; with more, `collect`
   returns `PENDING` until a later call, and you report that delay through `clap.latency`. One
   job per lease at a time (`BUSY` otherwise). Neither call allocates.
4. **Late and failed.** `LATE`: the result is discarded; render that block on your CPU path or
   mute it. `FAILED`: the lease is lost until released; with `lease.cpu_fallback` continue on your
   CPU path from that block, without it mute, and the host reports the interruption. A job from
   before your current activation collects as `STALE_EPOCH`.
5. **`release(host, lease)`**, main thread, at the latest in `deactivate`. The host releases what
   a plugin still holds when it destroys it.

Offline rendering always uses the CPU path. Nothing of an accelerator belongs in the plugin's
state: a Sound made on a desktop plays on a device with the accelerator and the other way round.

## Operations and backends

An operation is named `<abi family>.<name>/<version>`. Its job's input and output bytes are defined
by the accelerator's service ABI, not by this header. Backends of one engine share its parameters,
presets and state and must match its CPU path within the engine's stated tolerance; an audibly
different algorithm is a separate saved mode, not a backend.

## Without it

A host without the extension, or a device without accelerators, leaves the plugin on its CPU
path, which every plugin needs anyway. See the [fallback matrix](fallback-matrix.md).

## Testing

Oscairn's `hardware-plugin`, `hardware-service` and `hardware-timeline` tests exercise the
extension against a simulated accelerator: identical output on the accelerator and the CPU path,
refused reservations, a failure mid-play with and without a CPU fallback, late jobs and offline
rendering. A device profile's `hardware` section declares accelerators (Oscairn RFC 0024).
