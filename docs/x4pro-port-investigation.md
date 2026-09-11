# X4 Pro port investigation

Investigated 2026-09-10. Phase 0 only: no firmware changes, build, or device flashing.

## Pinned evidence

| Repository / relationship | Revision |
|---|---|
| Local Matcha and current eszter007/matcha-reader HEAD | `61ca61ba86e3c5709a24d1b9c4f3cf2d41488012` |
| Current CrossPoint `develop` | `1f3d7458a77c31641581667887029a5384ac4d3c` |
| Actual Git merge-base | `d4733bfc65aa6b95518ddb39b9554d5372d96018` (2026-08-09, split GH build action) |
| Matcha SDK gitlink | `a485dc46ef5fb2283e4bdb674002ddbef97a9268` |
| CrossPoint SDK gitlink | `7f6bd0f47a766eea18206dd19f723f3707b6c9d3` |

Source references below use these revisions. `CP:` means CrossPoint, `SDK:` means its pinned SDK; unprefixed paths mean Matcha. Reference checkouts are in `.cache/x4pro-audit/{crosspoint,freeink-sdk}`. The cache also contains upstream/Matcha changed-file lists and their intersection (`overlap.txt`), calculated against the merge-base. These are review aids, not proposed wholesale imports.

The local branch is `master`, with `origin` pointing to `renzeydood/matcha-reader`. The handoff was the only pre-existing untracked file. Remotes and the working branch were left unchanged. SDK source was inspected in the reference checkout at both revisions; the working project's empty SDK submodule was deliberately not initialized during this documentation-only investigation.

The Git revisions were verified directly over HTTPS. GitHub's rendered repository page showed a different SDK revision from the live Git checkout, so the Git tree is authoritative here.

## Conclusion

### Provenance correction after follow-up history review

The initial conflict map identifies files requiring review, but overstates which mechanisms originated in Matcha. Comparing Matcha directly against the merge-base shows:

- Storage locking is inherited CrossPoint behavior, including `88b10c82` (serialize file destruction to prevent concurrent SdFat SPI-state corruption). Matcha's functional HAL storage addition is `HalFile::modifiedStamp()`, a locked FAT timestamp read used to avoid re-examining unchanged library books.
- Matcha's HAL GPIO addition is `anyButtonDownRaw()` (`e4190942`): cover generation could block input polling for long periods, so conversion checks live button state and yields. The commit records a 151-second loop gap on device. This is an additive helper, not a driver rewrite.
- Silent WiFi-exit reboot already existed upstream (`7acc31bc`) to recover fragmented heap. Matcha extends it with a bounded translation retry (`20f93a8a`), stashing page text on SD before reboot because TLS could lack a sufficiently large contiguous allocation despite ample total free memory.
- The custom Arduino-core tuning was already present in the shared baseline. Matcha's net `platformio.ini` difference from that baseline is the EPUB include path. The X4 Pro inheritance issue is therefore upstream evolution to integrate, not conflicting Matcha-specific hardware tuning.

Merge implication: keep current CrossPoint hardware behavior, reapply these small feature hooks, and assess translation restart policy with actual S3 memory behavior. Preserve the fallback until evidence supports changing it. Shared filenames alone do not establish conflicting implementations.

Matcha still lacks an X4 Pro environment. Its SDK already knows about X4 Pro, SDMMC, GT911 and frontlight, but that does **not** provide current CrossPoint behavior. Use the current pinned SDK together with a selective application integration. A blind merge would also pull unrelated reader, UI and other-device changes into Matcha.

The smallest coherent Phase 1 is a platform/SDK/HAL/input/USB/update integration, with basic touch navigation in the existing library and reader. Adding an environment alone is insufficient. The exact compile-fix list remains subject to compiler feedback; this report is not a claim that the port builds.

## Build configuration

Authoritative target: [CrossPoint platformio.ini](https://github.com/crosspoint-reader/crosspoint-reader/blob/1f3d7458a77c31641581667887029a5384ac4d3c/platformio.ini#L282).

| Setting | Current upstream X4 Pro |
|---|---|
| Inheritance | `base` only; no `firmware_tuned` |
| Platform | pioarduino `55.03.311` (Matcha uses `55.03.37`) |
| Board / MCU | `esp32-s3-devkitc1-n16r8` / `esp32s3` |
| Arduino memory variant | `dio_opi` |
| PSRAM | `BOARD_HAS_PSRAM`, 8 MB hardware |
| Device | `FREEINK_DEVICE_X4PRO=1` |
| Storage | `USE_BLOCK_DEVICE_INTERFACE=1`, native 1-bit SDMMC |
| USB | `FREEINK_CAP_USB_MSC=1`; inherited `ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1` |
| USB descriptors | `CrossPoint_X4_Pro`, manufacturer `CrossPoint` |
| Development logging | `ENABLE_SERIAL_LOG`, `CROSSPOINT_WAIT_FOR_USB_SERIAL`, `LOG_LEVEL=2` |
| Version | `${crosspoint.version}-x4pro` (adapt version branding deliberately) |
| Flash | 16 MB, DIO, inherited maximum size 16777216 |
| Upload / monitor | 921600 / 115200; inherited upload offset `0x10000` |
| Partitions | `partitions.csv`, identical between these revisions |

Critical dependency: Matcha `platformio.ini:82` and `:109` put `custom_sdkconfig` and component removal in `base`. CP `platformio.ini:130` moves those into opt-in profiles. CP `:282` explicitly selects the prebuilt `dio_opi` core because the custom rebuild omits the TinyUSB component graph. Preserve existing Matcha C3/Sticky tuning while excluding X4 Pro from it.

Add SDK `UsbMassStorage` and `FrontlightManager` dependencies (CP `platformio.ini:101`). Review the platform upgrade alongside `scripts/patch_pioarduino_cache.py` and `scripts/patch_arduino_rom_libc.py`; do not assume all newer C3 tuning is required for S3. Retain Matcha's EPUB include path and wolfSSL fixes.

Partition equivalence: NVS `0x9000/0x5000`, OTA data `0xe000/0x2000`, app0 `0x10000/0x640000`, app1 `0x650000/0x640000`, SPIFFS `0xc90000/0x360000`, coredump `0xff0000/0x10000`. Source equivalence does not establish the actual installed device partition table.

## SDK delta

There are 113 changed files between the two SDK pins, including unrelated boards and UI work. Prefer a pinned SDK update over maintaining a private collection of driver patches. Do not modify vendor pin definitions in Matcha.

| SDK area | What needs preserving / adapting |
|---|---|
| `libs/hardware/BoardConfig/include/BoardConfig.h:1495` | Current `XTEINK_X4_PRO` profile and capability gating; display, digital input, GT911 Home, frontlight, SDMMC, gauge and rail definitions |
| `libs/display/FreeInkDisplay/` and `libs/hardware/XteinkDetect/` | Current panel detection and driver behavior; X4 Pro production batches include SSD1677 and UC8179; audit renderer API/refresh changes |
| `libs/hardware/InputManager/` | Changed touch/gesture APIs and classification, Home handling and orientation; application bridge must consume them |
| `libs/hardware/SDCardManager/` | Raw block-device/MSC coordination and SDMMC changes; `src/SdmmcBlockDevice.cpp:64` replaces per-transfer allocations with a bounded 4 KiB internal DMA buffer |
| `libs/hardware/UsbMassStorage/` | Host state, disconnect and exclusive raw-card ownership APIs |
| `libs/hardware/BatteryMonitor/` | Updated checked gauge reads and charging state; application must avoid treating failed readings as valid zero readings |
| `libs/hardware/FrontlightManager/` | Current brightness/warmth control and hardware behavior |
| `libs/hardware/PowerManager/src/PowerManager.cpp:73` | Hold panel reset at the appropriate level through sleep; preserve gated-rail shutdown and wake hold release |
| `libs/ui/FreeInkUI/` | Substantial app/input/component API evolution; adapt Matcha's UI host and component uses against the pinned version |

Reference: [pinned X4 Pro SDK documentation](https://github.com/Free-Ink/freeink-sdk/blob/7f6bd0f47a766eea18206dd19f723f3707b6c9d3/docs/xteink-x4pro-support.md). Some hardware details there are explicitly marked unconfirmed. Copy the working source profile; do not turn provisional documentation into new wiring assumptions.

## Application integration and conflict map

EASY means additive or little Matcha overlap, not automatically safe to copy. MERGE means both sides have compatible intent but require manual integration. RISKY means hardware, update or storage ownership must be audited. UNKNOWN means physical validation remains necessary.

| Class | Exact files / families likely to change | Integration requirement |
|---|---|---|
| RISKY | `platformio.ini`, `freeink-sdk` | Core inheritance, board/PSRAM/USB/SDMMC and pinned SDK |
| RISKY | `lib/hal/HalStorage.{cpp,h}` | Preserve Matcha's mutex-protected `HalFile` behavior while adding exclusive USB drive ownership; CP `HalStorage.cpp:56` begins the handoff |
| RISKY | `src/main.cpp`, `src/SilentRestart.h`, new `src/platform/UsbSerialJtagHandoff.{cpp,h}` | Matcha silent-reboot targets and reading state must survive integration; CP `main.cpp:205` returns USB PHY ownership before reboot, `:437` restores frontlight, `:639` includes touch in activity detection |
| RISKY | `lib/hal/HalPowerManager.cpp`, `lib/hal/HalGPIO.{cpp,h}`, `lib/hal/HalSystem.cpp` | Power-latch hold, gauge/USB detection, cold boot and wake logic; CP `HalPowerManager.cpp:84` onward holds configured latches through deep sleep |
| EASY / MERGE | `lib/hal/HalDisplay.{cpp,h}`, `lib/hal/HalMemory.{cpp,h}` | Import only required current SDK adapters; keep Matcha rendering/allocation policy |
| MERGE | `src/MappedInputManager.{cpp,h}`, `src/components/UiAppHost.*` | Route touch and Home gestures through oriented logical input; CP `MappedInputManager.cpp:298` onward and `HalGPIO.h:95` expose touch mechanisms |
| MERGE / RISKY | `src/activities/Activity.{cpp,h}`, `src/activities/ActivityManager.{cpp,h}` | Preserve Matcha activity hooks; add Home handling and exclusive-storage loop guards (`CP:Activity.h:49`) so normal activity work cannot access the mounted host card |
| EASY / MERGE | New `lib/hal/HalFrontlight.{cpp,h}`, `src/activities/util/FrontlightPanelActivity.{cpp,h}` | Basic frontlight controls plus initialization/settings persistence; CP `ActivityManager.cpp:104` routes panel gestures |
| EASY / RISKY | New `src/activities/network/UsbDriveActivity.{cpp,h}`, existing `NetworkModeSelectionActivity.{cpp,h}`, `CrossPointWebServerActivity.cpp` | Expose USB drive mode; preserve host timeout/eject/error/reboot behavior and stop all other SD access |
| MERGE | `src/CrossPointSettings.{cpp,h}`, `src/SettingsList.h`, `src/activities/settings/SettingsActivity.{cpp,h}` | Add capability-gated controls/defaults without dropping Matcha per-book, Japanese, dictionary, translation or statistics settings |
| MERGE | `src/activities/home/{HomeActivity,RecentBooksActivity,FileBrowserActivity}.{cpp,h}` | Minimal touch selection/back/navigation while retaining Matcha covers, manga, library and stats integration |
| MERGE | `src/activities/reader/{ReaderActivity,EpubReaderActivity,EpubReaderMenuActivity}.{cpp,h}`, dictionary activities | Keep Matcha vertical layout, ruby, dictionary selection and deinflection; add basic paging/menu input. Do not replace Matcha EPUB code with upstream files |
| RISKY | New `src/network/FirmwareBoardTag.{cpp,h}`, existing `FirmwareFlasher.{cpp,h}`, `OtaUpdater.cpp`, release parser and update activities | Preserve chip validation; add board tags and board-specific asset selection. Matcha's updater still looks for `firmware.bin` (`OtaUpdater.cpp:51`), whereas CP `OtaUpdater.cpp:36` selects a board suffix |
| RISKY / review | `src/network/OtaBootSwitch.{cpp,h}`, `src/activities/settings/SdFirmwareUpdateActivity.cpp` | Compare recovery/boot-slot switching before retaining or adapting; a shared MCU is insufficient to identify a compatible firmware image |
| MERGE | `lib/I18n/translations/*.yaml`, `README.md`, applicable build workflows | Add source strings, document support status, and eventually produce separately named X4 Pro artifacts; never hand-edit generated translation files |
| UNKNOWN | `src/activities/reader/MangaReaderActivity.*`, Japanese selection/translation flows, sleep/wake and USB | Validate reachable controls on hardware. Side page keys alone do not establish menu usability |

The changed-file intersection confirms simultaneous Matcha and CrossPoint changes in HAL GPIO/storage, settings, main, activity management, library screens, EPUB and dictionary activities. This is why whole-file replacement is inappropriate.

Useful history anchors: `bbca4886` (X4 Pro/PaperMono introduction), `81e19270` (retain battery drain fix while reverting light sleep), `f0c65506` (X4 Pro latch hold), `8bc1603e` (charging-state USB fallback), `f2ee89a1` (device-specific cold boot), `2e5a4762` (opt-in custom SDK build). Use current file contents as the destination; cherry-picking the introduction alone misses later fixes.

## Proposed Phase 1 sequence

1. Create `feature/x4pro`; initialize the working SDK and move its gitlink to the pinned CP SDK revision. Keep the original Matcha SHA recorded for comparison.
2. Restructure build inheritance; introduce X4 Pro with the upstream platform/core/PSRAM/USB/SDMMC configuration and required dependencies. Inspect effective environment settings before compiling.
3. Integrate current HAL, power, startup and update/recovery behavior, preserving Matcha hooks. Add USB exclusive ownership and return-to-Serial/JTAG support as one unit.
4. Adapt input/UI-host APIs; add basic Home, library selection, EPUB paging/menu and frontlight controls. Preserve Matcha feature implementations; defer advanced touch word selection and manga gestures.
5. Run `pio run -e x4pro` and resolve actual API/compiler errors. Verify image MCU, board tag and size against the app slot. Then build `default` and `sticky` for the shared-SDK regression surface.
6. Run applicable host tests and record results; update README and produce a pre-flash audit separately. Stop before device flashing, as requested in the handoff.

This is several reviewable steps, not a promise of a one-file patch. USB ownership and recovery must not be deferred merely to obtain a passing build.

## Validation and unresolved questions

- `pio` is not on PATH and the available Python reports no `platformio` module. Check for a VS Code-managed PlatformIO installation before installing another copy during Phase 1.
- Host suite entry point: `cmake -S test -B build/test`, `cmake --build build/test`, `ctest --test-dir build/test --output-on-failure`. Existing suites cover reading stats, dictionary sibling windows and release parsing among other logic.
- `dev/vertical_host_test/README.md` documents structural vertical-layout tests with stub metrics; these cannot establish real font/ruby visual correctness. `dev/dict_host_test/README.md` documents dictionary tooling. A complete interactive desktop emulator was not established by this inspection.
- Add meaningful checks around newly adapted board-tag validation, asset selection and USB exclusivity where host abstractions permit. Do not treat compiler success as runtime validation.
- Before flashing, obtain the exact installed CrossPoint version, verify its actual update/recovery path and partition layout, and retain a known-good official **X4 Pro** image. No rollback image was downloaded and no device-specific rollback procedure was verified in Phase 0.
- Matcha's current release channel must not offer a C3 image to this target. Upstream asset-selection logic needs coordination with this fork's eventual release naming; do not silently redirect users to a different firmware project.
- First physical checks: boot/display/SD, Home/buttons/touch, library/EPUB/page turns, sleep/wake on battery and USB, USB drive eject and serial recovery. Then Japanese vertical/ruby/dictionary/translation/manga checks, including all supported orientations.

The handoff explicitly says to review the investigation before Phase 1. This document is that review artifact; implementation and first-flash safety approval remain separate milestones.
