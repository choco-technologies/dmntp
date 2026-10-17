# dmntp

The time of NTP servers for DMOD - an SNTP client (NTPv4 over UDP, IPv4).
It asks the servers, checks their replies and returns UTC together with how
far it may be off. It keeps no clock and does not touch the RTC: it is one
of dmutc's external UTC sources, and dmutc sets the RTC from it after the
start and once a day.

```c
#include "dmntp.h"

dmntp_time_t time;
if (dmntp_sync(0, &time) == 0)
    Dmod_Printf("%lld.%09u +/- %llu ns\n", (long long)time.seconds, (unsigned)time.nanosecond,
                (unsigned long long)time.error_ns);
```

Servers: `DMNTP_SERVERS=server1,server2` when it is set, else the ones a
DHCP lease offered (dmdhcp, option 42), else `pool.ntp.org` and
`time.google.com`.

On a board: install with `dmntp service=ntp.ini` - the unit loads dmntp at
boot, dmutc finds it.

## Build and test

```sh
cmake -S . -B build -DDMOD_TOOLS_NAME=arch/x86_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`-DDMOD_TOOLS_NAME=arch/armv7/cortex-m7` for STM32F7. The tests (`tests/`)
check the protocol's arithmetic and the lists of servers.

[API reference](docs/api-reference.md) · [MIT license](LICENSE)
