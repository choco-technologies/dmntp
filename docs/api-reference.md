# dmntp API

Include `dmntp.h`.

| Function | What it does |
| --- | --- |
| `dmntp_sync(timeout_ms, &time)` | The time of the default servers (below) |
| `dmntp_sync_with(ntp, timeout_ms, &time)` | The time of the servers of a list |
| `dmntp_create()` / `dmntp_destroy(ntp)` | A list of servers, empty / freed |
| `dmntp_add_server(ntp, host)` | Adds an IPv4 address or a name; adding one twice is no error |
| `dmntp_remove_server(ntp, host)` | Removes one (`-ENOENT`: not there) |
| `dmntp_is_valid(ntp)` | Whether it is a list that was not freed |

## The time

```c
dmntp_time_t time;
int rc = dmntp_sync(0, &time);
```

| Field | |
| --- | --- |
| `seconds`, `nanosecond` | UTC (POSIX, no leap seconds) at the return of the call |
| `error_ns` | How far it may be from UTC |
| `servers` | How many servers agree on it |
| `delay_us` | The shortest round trip |

Each server is asked once, in turn: a name lookup (dmdns) and an exchange,
each limited by `timeout_ms` (0: 3000 ms, at most 60000 ms) - the whole call
can take longer. Every reply has to come from the server's address and port
123, echo the request's (random) transmit timestamp, be of a synchronized
server (version 3 or 4, mode server, leap indicator not 3, stratum 1..15),
have times that add up and a root distance up to 1 s.

An exchange gives an offset of UTC from the local clock and an interval it is
in: offset +/- (delay / 2 + root delay / 2 + root dispersion + the local
clock's resolution, 1 ms). The point most intervals contain is found
(Marzullo); more than half of them have to contain it, else `-EAGAIN`. The
intervals that do are intersected: the time is its middle, `error_ns` half
its width.

Returns 0; `-ENODATA` (no server answered or none to ask); `-EAGAIN`; or the
last failure (`-ETIMEDOUT`, DNS and network errors). `time` is cleared on
failure.

Times are integer nanoseconds (no floating point). NTP timestamps before 1968
are taken as ones of the next era: right from 1968 to 2104, across the 2036
wrap.

## The servers of dmntp_sync()

1. `DMNTP_SERVERS` - a comma-separated list - when it is set (read on every call);
2. else the servers the `dmntp_provide_servers` DIF reports (dmdhcp: DHCP
   option 42 of the current leases);
3. else `pool.ntp.org` and `time.google.com`.

### dmntp_provide_servers (DIF)

```c
dmod_dmntp_dif(1.0, void, _provide_servers, (dmntp_server_sink_t add, void* sink_ctx));
```

Call `add(sink_ctx, ipv4)` for each server - four bytes, network order,
borrowed. Asked on every `dmntp_sync()`, from the syncing thread: report what
is valid now.

## UTC source of dmutc

dmntp implements dmutc's `dmutc_read_source` DIF: `dmntp_sync(0, ...)`'s
time and `error_ns`. dmutc asks it after its start and then every 24 hours
and sets the RTC from the source with the smallest error; in between the RTC
keeps the time. `configs/ntp.ini` loads dmntp at boot (`type=library`).

## Calls

`dmntp_sync()` / `dmntp_sync_with()` calls run one at a time. They block -
never call them from the network's receive path. NTP is unauthenticated and
IPv4 only.
