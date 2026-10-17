#!/usr/bin/env python3
"""Run hardware NTP checks after transferring the three Cortex-M7 modules."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import re
import sys
import time
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--boot-dir', type=Path, default=Path('/data/codex/dmod-boot'))
parser.add_argument('--port', default='/dev/ttyACM0')
parser.add_argument('--log', type=Path, default=Path('ntp-board.log'))
args = parser.parse_args()
sys.path.insert(0, str(args.boot_dir / 'scripts'))
from ymodem_send import Link
link = Link(args.port, 921600)
with args.log.open('w') as log:
    def command(text, timeout=35):
        link.write((text + '\r').encode())
        deadline = time.monotonic() + timeout
        data = bytearray()
        while time.monotonic() < deadline:
            data.extend(link.drain(.15))
            if re.search(rb'\x1b\[0m> $', data):
                clean = re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', data.decode(errors='replace')).replace('\r', '')
                print(clean, end='', flush=True)
                log.write(clean); log.flush()
                return clean
        raise RuntimeError('Console timeout: ' + text)
    def probe(server=''):
        before = datetime.now(timezone.utc).timestamp()
        output = command('/ntpprobe.dmfc' + (' ' + server if server else ''))
        after = datetime.now(timezone.utc).timestamp()
        match = re.search(r'NTP_RESULT rc=(-?\d+) seconds_hi=(\d+) seconds_lo=(\d+) ns=(\d+) rtt_ms=(\d+) jitter_ms=(\d+)', output)
        assert match, output
        rc, high, low, ns, rtt, jitter = map(int, match.groups())
        if server == '192.0.2.1':
            assert rc < 0 and 'timed_out=1' in output and high == low == ns == rtt == jitter == 0, output
        else:
            assert rc == 0 and ns < 1000000000, output
            measured = (high << 32) + low + ns / 1000000000
            assert before - .5 <= measured <= after + .5, (before, measured, after)
        return output
    try:
        command('')
        command('export DMOD_REPO_PATHS=/:/flash/dmf:/flash/dmfc')
        assert '11/11 passed' in command('test_dmntp')
        probe()
        probe('time.google.com')
        for _ in range(5):
            probe('216.239.35.0')
        probe('192.0.2.1')
        assert '12/12 passed' in command('test_dmrtc')
        assert 'NTP_BOARD_ALIVE' in command('echo NTP_BOARD_ALIVE')
        command('ip addr')
        command('rtc get')
        result = 'HARDWARE_NTP: unit 11/11, 7 live queries, timeout, RTC 12/12 PASS\n'
        print(result, end=''); log.write(result)
    finally:
        try:
            command('export DMOD_REPO_PATHS=/flash/dmf:/flash/dmfc', timeout=3)
        finally:
            link.close()
