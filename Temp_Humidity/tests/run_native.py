#!/usr/bin/env python3
"""Run host C tests with GCC/Clang on Linux. Does not emulate ESP32 hardware."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

tests = Path(__file__).resolve().parent
headers = [
    "esp_err.h", "esp_check.h", "esp_log.h", "esp_timer.h", "esp_event.h",
    "esp_netif.h", "esp_http_server.h", "driver/gpio.h",
    "freertos/FreeRTOS.h", "freertos/task.h", "freertos/semphr.h",
]
with tempfile.TemporaryDirectory(prefix="qair-native-") as scratch:
    scratch = Path(scratch)
    for name in headers:
        header = scratch / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "sdk_stub.h"\n')
    for source in sorted((tests / "native").glob("test_*.c")):
        executable = scratch / source.stem
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")),
            "-std=gnu17", "-Wall", "-Wextra", "-Werror",
            "-ffunction-sections", "-fdata-sections",
            "-I", str(scratch), "-I", str(tests / "native"),
            "-I", str(tests.parent / "main/include"),
            str(source), "-Wl,--gc-sections", "-lm", "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
