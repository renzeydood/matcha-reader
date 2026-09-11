# X4 Pro pre-flash boot review

Reviewed 2026-09-11. Target: the user's USB-locked Chinese X4 Pro, currently on
CrossPoint 1.6.0 installed via the CrossPoint Unlocker. The user confirms that
SD Firmware Update is accessible. No firmware was flashed during this review.

**Software hardening is implemented and build-validated; hardware validation is
pending.** The initial audit found no definite merge-induced early-boot crash,
but identified the recovery gaps documented below. These have been addressed
in source. This is not proof that every execution path or hardware variant works;
the full experimental integration is still not cleared for flashing.

## Follow-up implementation - 2026-09-11

The user authorized boot hardening. The two source-level gaps below are now
addressed in the working tree; the original findings remain as audit history.
Physical bootloader compatibility and device testing are still unverified.

- `HalBootValidation` overrides Arduino's weak deferral hook with a strong C-linkage
  symbol. It records NEW/PENDING_VERIFY state without writing OTA metadata and
  accepts the image only at the normal Home-render checkpoint. Read/accept errors
  are logged; unknown, valid and rejected image states are not overwritten.
- Pending updates force a normal Home startup instead of reader/translation resume.
  The render-task completion barrier returns before the image is marked valid.
  Recovery, SD errors and crash-report startup never reach this checkpoint.
  Restarting from recovery may roll back a pending image when the installed
  bootloader supports that behavior.
- Recovery branches immediately after GPIO/power/SD startup, before panic dumping,
  saved state, settings, recents, credentials, tilt, RTC, frontlight preferences,
  SD font discovery, splash/resume handling or translation stash processing.
  It uses compiled defaults and built-in fonts; no settings files are rewritten
  simply because recovery started.
- Its dedicated loop services input and activities without normal auto-sleep,
  screenshots, clock persistence or power-button shortcuts. ActivityManager blocks
  Home/light-panel gestures and Home navigation while recovery is active, including
  child file-picker/confirmation activities. Cancelling the picker returns to the
  recovery updater, which reopens the picker. After releasing the initial wake
  gesture, holding Power for two seconds restarts without saving normal state.
- Recovery updater/picker allocation uses makeUniqueNoThrow with error handling.
  These replace existing activity allocations rather than adding a second copy.
  The activity framework owns them, so stack/static objects cannot substitute.
  Validation itself adds one static bool and no heap buffer or task.

The checkpoint proves that the Home render returned, not that every peripheral
passed a self-test or every book works. Hardware/display errors that lower layers
only log are not converted into a failed health check. A bootloader without
rollback cannot gain that capability through this application-only update.
Global constructors, essential GPIO/power/SD/display setup, built-in font
registration, render-task creation and file-picker allocation remain dependencies.
A very large directory can still exhaust the general file browser's containers.

### Verification checklist (not yet performed on hardware)

1. On a test device with known rollback support, install a NEW app image. Confirm
   the log says acceptance is deferred and only says accepted after Home renders.
   The first boot should open Home even with a previously saved open book.
2. With a deliberately failing startup on a recoverable development device, reset
   before the Home checkpoint and verify rollback to the previous slot. Do not
   run a deliberately broken build on the user's USB-locked reader.
3. On X4 Pro, hold Down while waking with Power, then release once the picker
   appears. Verify default UI/built-in fonts, directory navigation, file selection,
   cancellation and invalid-image errors. Home/light-panel gestures must not exit
   recovery. Release the wake gesture and hold Power for two seconds to verify
   the deliberate restart. Keep the known-good X4 Pro image on the card.
4. Repeat recovery with invalid settings JSON and a missing/corrupt selected SD
   font on a spare card. Recovery must still reach the picker and must not rewrite
   those files. Normal startup should retain the user's saved preferences.
5. Test the no-SD error route. It must not try SD font discovery or accept a pending
   image. Release the power button, then hold it for two seconds to restart this
   restricted route after correcting the card problem.
6. After a successful normal boot, test sleep/wake, settings, touch, reader and a
   second SD update. Recovery and image validation do not replace these tests.

Final validation: 232/232 host tests pass, including seven tests compiling the
actual HalBootValidation.cpp against mocked OTA calls. X4 Pro and original X4
builds pass (82.29s / 60.01s). Both linker maps resolve verifyRollbackLater to
HalBootValidation.cpp.o; image checksum/SHA/board/size checks pass.
Logs: `.cache/x4pro-audit/boot-hardening-firmware-final.log`,
`boot-hardening-tests.log`, `boot-hardening-images.json` in the same directory.
Formatting uses the repository wrapper; the real merge index remains unstaged.

Latest X4 Pro app: 6,184,464 bytes, SHA-256
`6f4ff8c9c701f1bc8a12710b0c267f7bc81bcc431ad6544465443850c350a008`.
Earlier image measurements below are historical and have been superseded.

## Original findings (before the follow-up implementation)

### 1. Automatic validation happens before application startup (high)

The linked Arduino framework supplies `verifyOta()` returning true and
`verifyRollbackLater()` returning false. `initArduino()` calls
`esp_ota_mark_app_valid_cancel_rollback()` for a pending image before the loop
task calls our `setup()`.

Evidence:
- `.cache/pio-core/packages/framework-arduinoespressif32/cores/esp32/esp32-hal-misc.c:261`
  (default verification), `:286` (deferral hook), `:306` (initialization).
- `.cache/pio-core/packages/framework-arduinoespressif32/cores/esp32/main.cpp:112`
  (`initArduino()` before task creation).
- `.cache/pio-packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig:424`
  and `:4392`: bootloader and application rollback options enabled locally.
- `.pio/build/x4pro/firmware.map` resolves both verification hooks to
  `libFrameworkArduino.a(esp32-hal-misc.c.o)`; no application override found.

Consequently, even if the installed bootloader supports pending-image rollback,
a failure in our display initialization, fonts, home screen, or reader can occur
AFTER validation cancels it. The bootloader actually installed by the unlocker
has not been read or verified. Compiling a rollback-enabled bootloader does not
change the bootloader on an SD application-only update.

Recommendation: design deferred image validation with a defined startup health
checkpoint, and establish the installed bootloader's rollback behavior before
relying on it. Returning false from verifyOta is not a deferred-validation fix:
it requests immediate rollback. No rollback hooks were changed in this review.

### 2. Recovery shares ordinary startup dependencies (high)

`src/main.cpp:433` recognizes the recovery gesture early, but routes to the
updater only at `:588`. Before that it loads app state (`:456`), settings,
recents, credentials and theme (`:478`), and display/render task/SD fonts
(`:540`, `:386`). `src/SdCardFontSystem.cpp:45` discovers fonts and can load
selected font files and UI fallbacks. Thus an allocation failure or hang there
can prevent recovery as well as ordinary startup. An SD font problem is an
example of a shared dependency, not a reproduced failure.

The GPIO selection matches 1.6.0: X4 Pro uses Down, avoiding Up/GPIO0's boot strap.
Recovery takes routing priority over crash reporting and book reopening, but it
is an application route, not a separate recovery firmware.

Recommendation: add an early recovery route using built-in fonts and minimal
state, bypassing saved font discovery, recents, network credentials and ordinary
resume/splash processing. Essential hardware and SD initialization will still
be required. Merely keeping an official image on the card cannot recover a
firmware that never reaches an updater.

### 3. Reaching Home can execute the changed reading engine (medium)

`src/main.cpp:614` retains the normal reader-load guard and clears/persists the
open-book path before ordinary automatic reopening (`:621`). This reduces
repeated reader-start crashes if the state write succeeds. Silent reader resume
uses another branch (`:605`) without that same guard; panic handling precedes it.

Home is also not an entirely passive test: `src/activities/home/HomeActivity.cpp:441`
generates missing recent-book thumbnails before its first full paint.
`loadRecentCovers()` (`:96`) can load EPUB metadata/CSS and run image decoding.
These are substantially merged areas, and reader behavior review remains open.
A fresh test SD state avoids this input-driven workload on the first boot; it
does not establish that existing books, fonts or settings will work later.

## Comparisons and checks

Two baselines were distinguished: MERGE_HEAD is a later CrossPoint development
revision; it is not the user's official 1.6.0 release.

- Against MERGE_HEAD, the SDK pin, board profiles, partition table, display HAL,
  power manager and frontlight startup are unchanged. platformio.ini adds only
  the EPUB include path. GPIO adds a state accessor; storage adds a mutex-protected
  modification-timestamp accessor. HalClock adds a bounded 24-byte clock-stash read.
- Against official 1.6.0, FirmwareFlasher.cpp, OtaBootSwitch.cpp,
  SdFirmwareUpdateActivity.cpp, partitions.csv, HalSystem.cpp,
  HalPowerManager.cpp, HalFrontlight.cpp and UsbSerialJtagHandoff.cpp match exactly.
- Against 1.6.0 the platform DOES change: pioarduino 55.03.37 -> 55.03.311
  (Arduino 3.3.7 -> 3.3.11). The ROM-libc patch exits immediately for non-C3 MCUs.
- SDK changes from official `cb9167d541c0f6e9d57cf8eae1f564a939883ecc`
  to local `7f6bd0f47a766eea18206dd19f723f3707b6c9d3` span 13 files.
  BoardConfig and hardware begin routines do not change. Changes include
  grayscale capabilities/UC8279 image waveforms, Home-key activity handling,
  streamed SD-read watchdog yielding and UI behavior. Panel behavior remains
  a physical test item; the user's actual panel variant is unknown.
- Settings loading clamps enums, bounds copied strings, and uses language codes
  (`src/CrossPointSettings.cpp:115`). Missing/invalid JSON is rejected by the
  store (`lib/Serialization/PersistableStore.cpp:23`). The store still reads
  entire JSON files into memory, so this is not a proof against oversized files.
- The render task remains an 8192-byte task and its allocation is asserted
  (`src/activities/ActivityManager.cpp:34`). Global mutex creation and vector
  reservation remain inherited allocation dependencies (`ActivityManager.h:70`).
  Static RAM percentages from the linker do not measure boot-time free heap.
- Font decompressor slab allocations are lazy and null-checked; built-in Japanese
  font objects point to static data. SD font loading and C++ container/activity
  allocations remain dynamic. No memory headroom guarantee is inferred.
- The SD flasher writes the inactive app partition and OTA selection metadata;
  it does not install the generated factory image or rewrite the partition table.
  Actual device partition capacity is checked by the updater at runtime.

## Validation evidence and pending work

Application image inspection: 6,183,488 bytes; ESP32-S3 chip ID 9;
`CROSSPOINT-BOARD-V1:x4pro;`; segment bounds, XOR checksum, SHA-256 trailer and
exact image length pass. It fits the source partition's 6,553,600 bytes with
370,112 bytes left. This establishes image structure, not boot compatibility.
Evidence: `.cache/x4pro-audit/boot-image-check.json`.

SHA-256: `d962bf07fc24cd08e80c4d942363d6398566517198494e6093c82b9ad267bec8`.
Fresh X4 Pro build verification PASS (90.08 seconds), recorded in
`.cache/x4pro-audit/boot-review-build-x4pro.log`. Image checks were repeated
after the build completed. Prior full X4 Pro and C3 builds and 225 host tests pass.
No C++ changes were made by this audit, so existing tests were not rerun.

Remaining before recommending a locked-device flash: harden recovery startup,
resolve validation/rollback design and its device prerequisites, finish the
known behavior fixes, rebuild, then prepare a controlled first-boot test with
fresh SD state. Physical boot, touch/display initialization and actual recovery
remain untested. The emulator cannot establish those hardware properties.
