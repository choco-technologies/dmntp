# dmntp API

Include `dmntp.h`. All functions are dynamically resolved through DMOD.

| Function | Behavior |
| --- | --- |
| `dmntp_create()` | Allocate an empty handle, or NULL on allocation failure. |
| `dmntp_destroy(h)` | Free a handle and its servers; NULL is accepted. |
| `dmntp_is_valid(h)` | Test a live handle's magic; does not make freed pointers safe. |
| `dmntp_add_server(h, host)` | Add an IPv4 literal or DNS name (1–253 bytes); duplicate adds return 0. |
| `dmntp_remove_server(h, host)` | Remove a server; absent entries return `-ENOENT`. |
| `dmntp_sync(timeout, seconds, ns, rtt, jitter)` | Sample using the configured singleton. |
| `dmntp_sync_with(h, timeout, seconds, ns, rtt, jitter)` | Sample using the handle's explicit servers. |

`timeout` is milliseconds per DNS server attempt and NTP exchange. Zero selects
3000 ms; values above 60000 return `-EINVAL`. DNS may perform several attempts,
and multiple servers are queried sequentially, so this is not a whole-call deadline.
`seconds` and `ns` are required, while `rtt` and `jitter` may be NULL. All provided
outputs are cleared on error. Success returns 0 and POSIX UTC (seconds since
1970-01-01, excluding leap seconds) with `ns` in [0, 1000000000).

An empty handle returns `-ENODATA`; no response returns `-ETIMEDOUT`; DNS,
transport and resource errors propagate. Contradictory responding servers
without a strict majority return `-EAGAIN`. Adding more than eight distinct
servers returns `-ENOSPC`. Errors do not write an RTC.

The singleton reads `DMNTP_SERVERS` once at library initialization. This is a
comma-separated replacement for the default pool.ntp.org/time.google.com list;
empty entries are errors. Configure before any caller loads the library.
Each explicit handle's list is independent of the singleton.

Calls to list/sync functions serialize; destruction and library unload require
that no other callers are using the object. Queries block while an independent
network pump handles receive callbacks. Never query from that pump/callback.
NTP is unauthenticated, supports only IPv4 and NTP era 0 (1970..2036), and does
not implement background discipline, INI configuration or DHCP discovery.

The 48-byte network packet and timestamp equations follow
[RFC 5905 sections 7–8](https://www.rfc-editor.org/rfc/rfc5905.html#section-8).
The implementation uses millisecond platform uptime as its local elapsed clock;
nanoseconds express the returned value, not a guarantee of nanosecond accuracy.
