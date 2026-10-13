# PerHost SRTLA Send

Experimental Android SRTLA sender library maintained for **PerHost streaming tests**.

This repository isolates the SRTLA transport layer used by the private PerHost Android application. Current work focuses on Wi-Fi/mobile bonding, handover reliability, stalled-link detection, packet-loss handling and connection statistics.

> Experimental: changes are validated incrementally through automated checks and real-world PerHost streaming tests. This is not a production stability guarantee.

## Upstream and license

The Android SRTLA implementation is derived from the SRTLA library shipped with [Bond Bunny](https://github.com/dimadesu/bond-bunny), which implements the [BELABOX SRTLA](https://github.com/BELABOX/srtla) protocol. Original authorship remains with the respective upstream authors.

PerHost maintains the modifications in this repository. See [LICENSE.txt](LICENSE.txt) for the applicable AGPL-3.0 license terms.

## Integration

The Android module is `srtla-lib`. The private PerHost Live application consumes this repository as a Git submodule/composite Gradle build. The PerHost application and APK are not published here.
