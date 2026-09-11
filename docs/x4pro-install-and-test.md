# Installing and testing the experimental merge

Checkpoint: 2026-09-11. X4 Pro and original ESP32-C3/X4 firmware builds pass;
232 host tests pass. The merge is still uncommitted and behavior review is open.
The emulator has not been built or tested against this branch. No device has been flashed.

## User's actual hardware: USB-locked X4 Pro

The user's only reader is a USB-locked Chinese X4 Pro running official
CrossPoint **1.6.0**, installed through https://crosspointreader.com/unlocker.
USB flashing instructions below do not apply while that lock remains in place.

Source comparison against the official `1.6.0` tag on 2026-09-11 confirms that
`src/network/FirmwareFlasher.cpp`, `src/network/OtaBootSwitch.cpp`, and
`src/activities/settings/SdFirmwareUpdateActivity.cpp` were identical at the
initial review. Subsequent hardening added checked picker allocation to the SD
updater; the flasher and boot-switch code remain identical. Reference copies are under `.cache/x4pro-audit/official-1.6.0/`.
This supports using CrossPoint's SD Firmware Update flow for a future trial;
it does not establish that our whole application boots or can recover.

Preparation now (no flashing): back up the SD card, download the official
`crosspoint-1.6.0-x4pro.bin` from the
[1.6.0 release](https://github.com/crosspoint-reader/crosspoint-reader/releases/tag/1.6.0),
and confirm that Settings exposes SD Firmware Update. Do not select the generic
release `firmware.bin`, which is a different device image. Once the integration
is ready, the intended input to the SD updater is our application-only
`.pio/build/x4pro/firmware.bin`, not `firmware.factory.bin`.

Do not install the experimental image yet. Recovery selection runs inside the
application after initialization; a failure before that point may prevent entry.
Neither automatic rollback nor an independent recovery path has been verified
on this device. Keeping the official image on SD helps only if a working updater
can still be reached. The unlocker used for the initial Chinese-firmware migration
must not be assumed to remain an independent rescue route after that migration.

The merged Settings code hides wireless Check Updates on touch devices. Its
OtaUpdater also looks for `firmware-x4pro.bin`, while the official 1.6.0 release
asset is named `crosspoint-1.6.0-x4pro.bin`. Wireless updating therefore needs
separate review; retaining OTA source is insufficient to establish usability.

The user confirms SD Firmware Update is accessible. The subsequent
[boot review](x4pro-boot-review.md) found that Arduino validates pending images
before application setup and recovery shares normal initialization dependencies.
These source paths have now been hardened: early recovery skips optional startup,
and pending updates wait for a completed Home render before acceptance. Both firmware targets and all 232 host tests pass; linked-hook and image-integrity
checks also pass. Verification is tracked in the progress log. The installed bootloader and physical
recovery behavior remain unverified; the image is not yet cleared for flashing.

## Physical reader

Back up the SD card, including hidden `.crosspoint`, font and dictionary folders.
Prefer a spare test card and copies of books: this branch changes cache formats
and known reading-position issues remain under review. Keep a known-good firmware
for the exact device model available for rollback.

These instructions assume normal USB flashing is available. Do not use this custom
build through the Xteink Unlocker on a USB-locked device: upstream warns that
unsupported firmware on that route may leave no recovery path. See the
[upstream installation and USB-lock guidance](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/README.md#usb-locked-devices-xteink-unlocker).

### Choose the correct application image

| Device | Local application image |
| --- | --- |
| X4 Pro, ESP32-S3 | `E:/matcha-reader/.pio/build/x4pro/firmware.bin` |
| Original X4, ESP32-C3 | `E:/matcha-reader/.pio/build/default/firmware.bin` |

The `default` image is not for the Pro or the newer X4 Classic/X4C.
`firmware.factory.bin` is a combined image containing boot components; it is not
interchangeable with the application-only `firmware.bin` used below.

### Browser installation

1. Connect the powered-on reader using a data-capable cable. Leave USB Drive mode
   if active, safely ejecting it from Windows first. Close serial monitors.
2. Open [CrossPoint's flash tools](https://crosspointreader.com/#flash-tools) in a
   Web Serial-capable browser such as Chrome or Edge.
3. Select the exact device model, choose **Custom .bin**, and select the matching
   application image above. Follow the connection and flashing prompts.
4. Wait for success, then follow the tool's reboot instructions. Never select the
   original X4 profile for an X4 Pro.
5. If the device is absent or flashing fails, save the tool's message. Check cable,
   port and competing serial applications before attempting recovery. A verified
   model-specific boot-button/recovery procedure is not yet recorded for this project.

The custom-image route and official-firmware rollback route are documented by
[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/README.md#install-firmware).

### PlatformIO alternative on this Windows machine

This rebuilds/uploads the current working tree using its board configuration.
Replace COM5 with the reader's port, found using `device list`.

```powershell
Set-Location E:\matcha-reader
$pio = 'C:\Users\Renzey\.platformio\penv\Scripts\platformio.exe'
$env:PYTHONIOENCODING = 'utf-8'
$env:PYTHONUTF8 = '1'
$env:TEMP = 'E:\matcha-reader\.cache\p'
$env:TMP = $env:TEMP
& $pio device list
& $pio run -e x4pro -t upload --upload-port COM5
```

For the original X4, substitute `-e default`. Use either browser installation or
PlatformIO, not both. Keep `.cache/pio-shared-tools` and `.cache/pio-packages`:
the shared PlatformIO installation uses junctions into these directories.

Capture serial output after flashing (the port may change after reboot):

```powershell
& $pio device monitor --port COM5 --baud 115200
```

Copy useful logs, or use the project's richer monitor:

```powershell
python -m pip install pyserial colorama matplotlib
python scripts/debugging_monitor.py COM5
```

Close the monitor before another flash. Firmware version alone may not identify
our working tree: this build inherits CrossPoint's version string. Record the
image hash and date along with the device model:

```powershell
Get-FileHash .pio/build/x4pro/firmware.bin -Algorithm SHA256
```

## Desktop emulator: separate, unvalidated build

The [Matcha emulator](https://github.com/eszter007/Crosspoint-Emulator-Matcha) builds
firmware source into an SDL2 desktop application. It cannot run the ESP32 `.bin`
files. Its default layout and hardware stubs do not establish X4 Pro touch,
frontlight, USB, power or physical display correctness.

Its README and CMake default source locations differ. Explicitly set
`CROSSPOINT_ROOT` to our current workspace, as supported by its
[CMake configuration](https://github.com/eszter007/Crosspoint-Emulator-Matcha/blob/main/CMakeLists.txt).
Cloning the original Matcha firmware would test a different revision; our merge
edits are local and uncommitted.

The following is a setup/build attempt for Ubuntu under WSL with GUI support,
not a verified recipe for this merged branch. Native Windows prerequisites are
also described in the [upstream emulator README](https://github.com/jonmooreai/Crosspoint-Emulator#setup-on-windows).

In the Ubuntu terminal:

```bash
sudo apt update
sudo apt install build-essential cmake git libsdl2-dev libcurl4-openssl-dev python3 python3-yaml
cd ~
git clone https://github.com/eszter007/Crosspoint-Emulator-Matcha.git
cd Crosspoint-Emulator-Matcha
cmake -S . -B build -DCROSSPOINT_ROOT=/mnt/e/matcha-reader
cmake --build build -j 4
```

The existing firmware builds have already generated this workspace's i18n files.
If the emulator fails on missing APIs, source paths or hardware stubs, preserve
the first compiler error: adapting the emulator to the merged SDK is separate
work still needed. Do not switch to old firmware simply to make it compile.

If compilation succeeds, create `sdcard` inside the emulator directory, copy test
books and the required fonts/dictionaries there, then run from that directory:

```bash
mkdir -p sdcard
./build/crosspoint_emulator
```

The documented controls are arrows for directional buttons, Enter for Confirm,
Escape/Backspace for Back, and P for Power. These are desktop controls, not a
validated simulation of Pro touch gestures.

## Test order and results

Use one short English EPUB, one Japanese EPUB with ruby, one image-heavy EPUB,
and a prepared Matcha manga folder. Copy existing font and dictionary assets
from your backed-up card, preserving their paths.

| Test | Expected behavior |
| --- | --- |
| Boot and browse | Home appears; SD files and books can be opened |
| Page turns | Forward/backward work within and across chapters |
| Japanese reading | Vertical/horizontal text and ruby display; lookup opens and returns |
| Reading position | Reopen, cancel chapter selection, change font/layout, sleep/wake: check the same passage |
| Text layout | Spacing, focus reading and mixed font sizes do not overlap or clip |
| Images and manga | Images/panels render; chapter and page navigation work |
| Pro touch | Toolbar opens, rows respond, Back/Home return correctly |
| Pro frontlight | Top-edge panel adjusts brightness/warmth; sleep/wake settings apply |
| Pro USB Drive | A disposable file can be copied, safely ejected, and read after leaving USB mode |
| Stability | Repeated opening/closing and 20-30 page turns do not crash |

Position/sync and letter-spacing issues are already under review; a failure there
does not necessarily mean installation failed. Save/compare the passage, not just
the page number, because changing layout changes pagination. Test network sync
later with a disposable book so experimental progress does not replace a valued position.

Record: device/model, image SHA256, book, steps, expected/actual result, photo or
screenshot and serial output. Mark PASS/FAIL/NOT TESTED per row. Do not include
API keys or Wi-Fi passwords in shared logs. Results belong alongside the
[integration tracker](x4pro-merge-resolution-log.md).
