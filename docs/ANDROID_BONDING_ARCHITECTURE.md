# PerHost SRTLA: Android multi-network reliability decision

## Decision

**Hybrid architecture.** Keep the Android-specific Bond Bunny JNI/network socket binding and SRTLA wire implementation. Adapt independently testable scheduling concepts from [irlserver/srtla_send](https://github.com/irlserver/srtla_send) instead of embedding its Linux/desktop Rust binary in the Android APK.

The Rust implementation assumes a set of routable local source IPs. Android requires a `ConnectivityManager.Network`-bound socket per uplink. Replacing the engine outright would mean rebuilding Android network lifecycle, JNI callbacks, telemetry, Moblink compatibility, and failure handling. A direct binary swap does not prove that each socket uses a different physical path.

## Data path

```text
Camera/Audio -> StreamPack SRT -> local UDP -> PerHost SRTLA sender
                                              |
                                   quality-aware scheduler
                                      /      |      \\
                             Wi-Fi socket  SIM A   SIM B
                                      \\      |      /
                                      SRTLA receiver
                                            |
                                      SRT ingest
```

Each Android `Network` must own a distinct UDP socket. The scheduler must never interpret an Android network callback alone as proof of successful video delivery.

## Link state and scheduling

1. **Eligible:** registered path with recent receiver activity.
2. **Stalled:** high outstanding packet count and no recent *earned SRTLA ACK*; temporarily deselect only if another eligible path exists.
3. **Lossy:** attributed NAKs reduce this path's selection score, with monotonic time-based recovery.
4. **Dead:** timeout excludes path; other links remain active and the group must not be reset unnecessarily.
5. **Recovery:** keep control traffic alive and allow normal registration/ACKs to restore eligibility. Uncorrelated keepalives are not delivery proof.
6. **All paths degraded:** retain best-effort fallback; do not deliberately blackhole the stream.

## Automated acceptance checks

- Two-path handover: replacing a Wi-Fi socket must not close or reset cellular's socket/window.
- Three-path outage: when SIM A loses all connectivity, scheduler selects only Wi-Fi or SIM B.
- Three-path silent stall: a SIM with 40 outstanding packets and no earned ACK for >3s must not be selected while another healthy link exists.
- Attributed NAK: penalty must affect only the uplink that transmitted the lost packet.
- Concurrency: Android/JNI network mapping and telemetry updates must not race with sender state.
- Build: native sanitizer suite and Android AAR/APK build must pass before merging.

## What this does *not* prove

- A physical Android device actually routes all three sockets over distinct carriers.
- An SRT retransmission always arrives before the configured latency expires.
- A high-bitrate stream survives a loss when the remaining links have insufficient upload capacity.
- Server-side SRTLA/SRT receiver behavior is correct. Validate against the deployed receiver separately.

## Rollout gates

1. CI native tests and Android library compilation.
2. Private APK build against pinned library SHA.
3. No automatic merge; preserve previous sender as a rollback path.
4. Optional physical device test when available: disconnect SIM A during a 6 Mbps stream, confirm receiver continuity, per-link bitrate and SRT NAK/retransmit counters.

## License

PerHost's extracted sender retains upstream AGPL-3.0 notices. irlserver's Rust implementation is MIT licensed; this design borrows concepts rather than copying its Rust source.
