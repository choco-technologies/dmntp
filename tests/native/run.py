#!/usr/bin/env python3
"""Compile the real NTP implementation with deterministic platform fixtures."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
fixture = root / "tests/native"
with tempfile.TemporaryDirectory(prefix="dmntp-transport-") as directory:
    binary = Path(directory) / "transport-test"
    flags = shlex.split(os.environ.get("NTP_TEST_CFLAGS", ""))
    command = [os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L",
               "-Wall", "-Wextra", "-Werror", "-pthread", *flags,
               "-I" + str(fixture), "-I" + str(root / "src"), "-I" + str(root / "include")]
    command += [str(root / "src" / file) for file in
                ["dmntp.c", "dmntp_servers.c", "dmntp_codec.c", "dmntp_config.c", "dmntp_query.c"]]
    subprocess.run([*command, str(fixture / "transport_test.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
