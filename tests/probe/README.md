# STM32F746G-DISCO NTP probe

Build for `arch/armv7/cortex-m7` against the dmod revision used by firmware.
Keep a separate network pump running (`networkd@eth0` in dmod-boot).

With dmod-boot's UART helper at 921600 baud:

```sh
python3 /data/codex/dmod-boot/scripts/ymodem_send.py --dest / --card none \
  build/stm32f7/dmfc/dmntp.dmfc \
  build/stm32f7/dmfc/test_dmntp.dmfc \
  build/stm32f7/dmfc/ntpprobe.dmfc
```

If an older dmntp is already loaded, reset before transferring. In dmell:

```text
export DMOD_REPO_PATHS=/:/flash/dmf:/flash/dmfc
test_dmntp
/ntpprobe.dmfc
/ntpprobe.dmfc time.google.com
/ntpprobe.dmfc 216.239.35.0
/ntpprobe.dmfc 192.0.2.1
export DMOD_REPO_PATHS=/flash/dmf:/flash/dmfc
```

The documentation address 192.0.2.1 should time out with zero outputs on an
ordinary network. On success the probe prints `NTP_RESULT rc=0`, Unix seconds
in two 32-bit halves, nanoseconds, RTT and jitter. Compare against host UTC at
receipt. Repeat calls to check resource cleanup and continued console operation.
Files sent to `/` live in RAM; a reset removes them. No firmware flash is needed.
