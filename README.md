# dmntp

Blocking NTPv4 time sampling for DMOD. Resolves IPv4 literals and DNS names,
queries servers over UDP/123 and returns POSIX UTC with nanoseconds, RTT and
jitter. It does not write an RTC or run a background clock discipline loop.

```c
#include "dmntp.h"

dmntp_t client = dmntp_create();
dmntp_add_server(client, "pool.ntp.org");
uint64_t seconds;
uint32_t nanoseconds;
int rc = dmntp_sync_with(client, 1000, &seconds, &nanoseconds, NULL, NULL);
dmntp_destroy(client);
```

`dmntp_sync()` uses `pool.ntp.org` and `time.google.com` by default.
Without an explicit override, each call first asks optional discovery providers
for current servers (DHCP option 42 when the updated dmdhcp is loaded), falling
back to these defaults when none are available. Set `DMNTP_SERVERS=server1,server2`
before the library is first loaded to override both discovery and defaults. For example, from dmell:

```text
export DMNTP_SERVERS=192.168.50.1
ntpprobe
ntpprobe time.google.com
```

Explicit handles start with no servers. Lists accept up to eight distinct
servers; adding a duplicate is idempotent. Calls serialize, including across
handles. Network processing must remain active while a query blocks: do not
call from an RX callback or the network pump itself.

Each exchange validates the source address/port, echoed request timestamp,
version, server mode, leap status, stratum, timestamp ordering and root distance.
With multiple responding sources, the maximum interval overlap must contain a
strict majority; disagreement returns `-EAGAIN`. RTT is the minimum measured
network delay among responding servers; jitter is the sample standard deviation
of offsets in the selected overlap. These are diagnostics from one sampling
round, not long-term accuracy estimates.

NTP is unauthenticated. This version supports NTP era 0 (1970 through the
February 2036 rollover), IPv4 and at most eight servers. DHCP option 42 discovery
is provided by [dmdhcp PR #5](https://github.com/choco-technologies/dmdhcp/pull/5),
using the optional `dmntp_provide_servers` DIF. A fresh snapshot is taken for every
call; stale DHCP addresses are not cached in NTP. Older DHCP modules simply
provide no NTP servers. INI parsing is not implemented.

## Build and test

```sh
cmake -S . -B build/native
cmake --build build/native --parallel
ctest --test-dir build/native --output-on-failure
python3 tests/native/run.py
```

Use `-DDMOD_DIR=/path/to/dmod` to use an existing checkout. For STM32F746G-DISCO,
configure with `-DDMOD_TOOLS_NAME=arch/armv7/cortex-m7`. Dependencies are dynamic
DMOD modules, including `dmudp`, `dmdns` and the platform's `dmosi`.

The native transport fixture compiles the real library sources with a fake
platform boundary. It covers valid/malformed/spoofed responses, timeouts, DNS and
send errors, immediate and duplicate replies, callbacks after cleanup, tick
wraparound, repeated calls and configuration. Run it with sanitizers using:

```sh
NTP_TEST_CFLAGS='-fsanitize=address,undefined -g' python3 tests/native/run.py
```

On hardware, load `dmntp.dmfc`, `test_dmntp.dmfc` and `ntpprobe.dmfc` from the
same build. Run `test_dmntp`, then `ntpprobe [SERVER]`; compare the returned
`seconds_hi`/`seconds_lo` and `ns` with a trusted UTC clock. See
[tests/probe/README.md](tests/probe/README.md) for the UART procedure.

[API reference](docs/api-reference.md) · [MIT license](LICENSE)
