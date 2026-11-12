# Contributing

The maintained application is the ESP-IDF project in `Temp_Humidity`. The Arduino sketch is a legacy reference.

## Branch workflow

Keep `develop` as the long-lived integration branch and submit pull requests from `develop` into `main`. After a pull request is merged, fetch the remote branches and merge `origin/main` into `develop` before starting more work. This keeps the ancestry correct after a squash or rebase merge as well.

```text
git fetch origin
git switch develop
git merge origin/main
```

Push reviewed changes to `develop` and keep that branch after merging. Avoid force-pushing shared history.

## Development environment

Use ESP-IDF 6.0 with the original ESP32 target. The development container pins its default image to `espressif/idf:v6.0`. Serial device access and the debug probe configuration depend on your board and host.

Open `Temp_Humidity` in VS Code. Configure the ESP-IDF installation and serial port locally, then build to create `build/compile_commands.json`. The checked-in editor settings use workspace-relative build paths.

## Required checks

From the repository root, run the JavaScript and native host tests.

```text
node --test Temp_Humidity/tests/test_web_ui.js Temp_Humidity/tests/test_wifi_setup_ui.js
python3 Temp_Humidity/tests/run_native.py
git diff --check
```

The native tests need Python 3 and a Linux GCC/Clang C toolchain. They use test doubles for hardware and SDK services. No third-party Python or Node packages are needed.

Then activate the ESP-IDF environment and build the target.

```text
cd Temp_Humidity
idf.py build
```

Describe what changed, why, the checks performed, and any remaining hardware checks in the pull request. Compare firmware size or measured execution costs when claiming a performance improvement, and state the measurement platform.

## Physical-board checks

Changes involving sensor timing, Wi-Fi transitions, flash writes or GPIO recovery need board testing. Check first-time setup, invalid credentials, reconnect after a router outage, a different router channel, a restart with saved settings and both BOOT actions. Verify sensor failures mark retained readings as stale and that history gaps do not appear as continuous data.

Keep real Wi-Fi credentials, generated firmware and local build output out of commits. History is held in RAM and is cleared by restarting.
