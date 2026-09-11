# Matcha Reader → XTEINK X4 Pro Port — VS Code/Codex Handoff

## Purpose

Port **eszter007/matcha-reader** to the **XTEINK X4 Pro** while preserving the current, working CrossPoint X4 Pro hardware, flashing, recovery, storage, touch, frontlight, USB, and power-management behavior.

Primary repositories:
- Matcha Reader: https://github.com/eszter007/matcha-reader
- Upstream CrossPoint: https://github.com/crosspoint-reader/crosspoint-reader

This document is a handoff from a prior ChatGPT investigation. **Do not treat file names, commit hashes, or API details here as authoritative if the repositories have changed. Inspect the current repositories before modifying anything.**

---

## 1. User/device context

Device:
- XTEINK X4 Pro
- ESP32-S3 generation, NOT the ESP32-C3 X3/X4 target.
- Device originally shipped with locked Chinese-region firmware.
- It has now successfully been moved to official CrossPoint firmware using the CrossPoint/Xteink OTA unlock process.
- CrossPoint is currently installed and working well.

Important implication:
- We should no longer need the original Chinese-firmware OTA-unlock procedure for ordinary CrossPoint-compatible development flashing.
- However, do **not** assume the device is permanently/unconditionally hardware-unlocked.
- Preserve current CrossPoint X4 Pro recovery/flashing mechanisms. An experimental firmware must not casually remove the path back to known-good CrossPoint.

The previous Windows OTA unlock was difficult because of socket error 10013. It eventually succeeded after aggressively eliminating networking conflicts (including Tailscale/ICS-related state and a Winsock reset). Avoid creating a situation that unnecessarily requires repeating that procedure.

---

## 2. Why Matcha

Matcha Reader is essentially the exact feature direction wanted for the device.

Desired Matcha features include:
- Japanese vertical text / tategaki
- Furigana/ruby handling
- Japanese dictionary lookup
- Verb deinflection
- Japanese names/grammar dictionary support
- Page translation
- Manga panel reader
- Library/cover improvements
- Reading statistics
- Per-book reader settings
- CJK fallback/font improvements
- Other Matcha UX additions

The objective is NOT to redesign Matcha or create a separate reader.

The objective is:

    CURRENT CROSSPOINT X4 PRO FOUNDATION
                    +
            MATCHA READER FEATURES
                    ↓
              MATCHA X4 PRO

Preserve as much upstream compatibility as possible so the result can potentially be contributed back to Matcha.

---

## 3. Current investigation findings

At investigation time:

### Matcha

Matcha identifies itself as a fork of CrossPoint for X4/X3 and says it tracks upstream CrossPoint.

Its README says its changes are mostly additive, including:
- new dictionary libraries
- manga panel support
- Japanese vertical text engine
- new reader activities
- menu/glue integration into existing CrossPoint code

Matcha already has ESP32-S3 awareness because it supports the Seeed Sticky target. Therefore the application itself is not fundamentally ESP32-C3-only.

At the time of investigation, Matcha did NOT expose an X4 Pro PlatformIO target.

Matcha's freeink-sdk submodule was observed at a different revision from current upstream CrossPoint, which strongly suggests the hardware foundation needs to be brought forward carefully rather than simply adding one build stanza.

### CrossPoint

Current upstream CrossPoint explicitly supports:
- ESP32-C3 X3/X4
- ESP32-S3 X4 Pro
- other ESP32-S3 devices

CrossPoint's X4 Pro support includes hardware-specific behavior such as:
- ESP32-S3 target configuration
- PSRAM configuration
- X4 Pro display/panel support
- capacitive touch
- Home key
- frontlight
- SDMMC/storage behavior
- USB mass-storage/native USB behavior
- sleep/wake/power-management handling
- touch reader interactions

The current CrossPoint X4 Pro implementation should be considered the reference implementation for all hardware-facing behavior.

---

## 4. Critical architectural rule

DO NOT attempt to make Matcha run on the X4 Pro by merely copying an `[env:x4pro]` block into `platformio.ini`.

That may compile while still missing important runtime X4 Pro behavior.

Instead, identify all relevant differences between:
1. Matcha's current CrossPoint baseline
2. current upstream CrossPoint
3. current freeink-sdk revisions
4. X4 Pro-specific upstream changes

Then bring the hardware/platform foundation forward in a controlled way.

Preferred mental model:

    Matcha application/features
              |
        CrossPoint application
              |
          freeink-sdk
        /      |       \
     X3/X4   Sticky    X4 Pro

We want Matcha's feature layer on top of the current X4 Pro-capable CrossPoint/freeink foundation.

---

## 5. Do-not-break list

Treat these as safety-critical integration requirements.

Do not regress or replace current upstream X4 Pro implementations for:

- X4 Pro board/device definition
- ESP32-S3 MCU target
- PSRAM configuration
- flash layout / partition assumptions
- boot/recovery behavior
- firmware-update path
- USB/native USB behavior
- USB mass-storage behavior
- SDMMC storage
- display/panel initialization
- touch controller
- capacitive Home key
- frontlight
- sleep/wake
- deep sleep / USB-related power behavior
- battery/power management

If Matcha has older versions of these mechanisms and upstream has newer X4 Pro-aware versions, prefer the current upstream implementation and adapt Matcha's feature hooks around it.

NEVER build or flash an X3/X4 ESP32-C3 binary to the X4 Pro.

---

## 6. Suggested Git setup

Use a dedicated fork/branch.

Suggested remotes:

    origin     -> user's Matcha fork
    matcha     -> eszter007/matcha-reader
    crosspoint -> crosspoint-reader/crosspoint-reader

Suggested working branch:

    feature/x4pro

Before coding:
1. Fetch all remotes.
2. Initialize/update submodules recursively.
3. Record exact current commit IDs for Matcha, CrossPoint, and freeink-sdk.
4. Determine Matcha's actual merge-base/history relative to CrossPoint.
5. Inspect Matcha's own CLAUDE.md / AGENTS.md / repository instructions.
6. Do not overwrite Matcha-specific work with a blind upstream merge.

---

## 7. Phase 0 — repository archaeology

Before making functional changes, answer these questions and document them:

### A. Build target

Compare Matcha and CrossPoint `platformio.ini`.

Identify:
- exact current upstream `[env:x4pro]`
- inherited/common envs
- build flags
- board
- MCU
- memory type
- PSRAM flags
- USB flags
- storage/block-device flags
- partition configuration
- upload settings

### B. freeink-sdk

Compare:
- Matcha's freeink-sdk commit
- CrossPoint's freeink-sdk commit

Identify X4 Pro-related changes in:
- BoardConfig
- EInkDisplay
- InputManager
- SDCardManager
- BatteryMonitor
- PowerManager
- FreeInkUI
- any USB/storage abstractions
- device capability flags

Determine whether updating Matcha's SDK pointer cleanly supplies most X4 Pro support or whether application changes are also required.

### C. CrossPoint application layer

Search current upstream for:
- `FREEINK_DEVICE_X4PRO`
- `X4PRO`
- X4 Pro capability checks
- native USB handling
- USB MSC
- SDMMC
- touch
- frontlight
- Home key
- sleep/deep sleep
- recovery/update handling

Compare every relevant hit with Matcha.

### D. Matcha divergence

Identify Matcha modifications in the same files touched by upstream X4 Pro changes.

Classify conflicts as:
- EASY: unrelated/additive
- MERGE: both projects changed same area but intent is compatible
- RISKY: hardware/recovery/storage/power code overlaps
- UNKNOWN: needs device testing

Do not start by resolving everything. Produce this map first.

---

## 8. Preferred implementation sequence

### Phase 1 — Build-only X4 Pro target

Goal:

    pio run -e x4pro

must succeed.

At this stage:
- no physical flashing
- preserve Matcha functionality
- import/update only the minimum coherent upstream foundation required for a legitimate X4 Pro build

Compiler errors are useful evidence. Resolve them systematically rather than adding compatibility hacks blindly.

Likely categories:
- renamed APIs
- constructor/signature changes
- InputManager changes
- FreeInkUI changes
- storage API changes
- capability flags
- device-specific branches

### Phase 2 — Regression testing off-device

Use Matcha's desktop emulator/test facilities where applicable.

Verify existing Matcha behavior:
- normal EPUB reading
- Japanese vertical EPUB
- furigana
- dictionary lookup
- settings
- library
- manga
- stats
- translation UI where testable

The hardware merge should not destroy Matcha's existing functionality.

### Phase 3 — First-flash safety audit

DO NOT flash merely because the project compiles.

Before first device flash, verify explicitly:

- target is `x4pro`
- MCU is ESP32-S3
- current upstream X4 Pro board config is used
- PSRAM config matches upstream
- flash/partition layout matches upstream expectations
- recovery/update mechanisms are preserved
- USB/native USB behavior is preserved
- SDMMC configuration matches upstream
- current known-good official CrossPoint X4 Pro firmware/recovery path is available
- exact rollback procedure is known
- no X3/X4-specific assumptions can overwrite X4 Pro hardware configuration

Produce a short safety report before recommending flashing.

### Phase 4 — Minimal physical smoke test

The first device build should be intentionally boring.

Test in this order:
1. boots
2. display initializes correctly
3. SD card mounts
4. buttons/Home input works
5. touch initializes
6. library opens
7. EPUB opens
8. page navigation works
9. clean exit/sleep/wake
10. USB/recovery path still works

Do not polish Matcha touch UX before this foundation is stable.

### Phase 5 — Japanese feature validation

Then test the actual reason for the port:
- Japanese EPUB detection
- vertical text
- right-to-left column flow
- furigana
- kinsoku behavior
- Japanese font fallback
- word selection
- dictionary lookup
- deinflection
- names dictionary
- grammar dictionary
- translation
- per-book settings

### Phase 6 — X4 Pro UX polish

Only after core stability:
- touch word selection
- touch settings navigation
- swipe page turns
- Home key behavior
- frontlight controls/UI
- manga touch/panel navigation
- orientation interactions
- sleep/wake edge cases
- USB file transfer / MSC
- any X4 Pro-specific layout improvements

---

## 9. Expected difficulty

Previous investigation estimate:

Basic working Matcha X4 Pro:
    ~4–5 / 10

Fully polished Matcha X4 Pro:
    ~6 / 10

Reason:
- Matcha and CrossPoint share the same architecture/history.
- Matcha intentionally tracks upstream.
- Matcha changes are largely additive.
- Upstream has already solved X4 Pro hardware support.
- Matcha already runs on at least one ESP32-S3 target.

The primary work is integration/rebasing, not writing X4 Pro drivers from scratch.

Likely hardest areas:
- SDK/API drift
- touch behavior in Matcha-specific activities
- power/USB/recovery integration if Matcha's baseline is behind
- validating hardware behavior on the actual device

---

## 10. Expected first prototype state

A successful early prototype does NOT need every X4 Pro interaction polished.

An acceptable first milestone could be:

    Boot                  YES
    Display               YES
    SD                    YES
    Library               YES
    Open EPUB             YES
    Japanese vertical     YES
    Physical navigation   YES
    Basic touch           YES
    Dictionary            YES, even if button-driven initially
    Tap-to-select word    MAYBE / later
    Manga touch polish    later
    Frontlight UI polish  later

Prioritize stability and recoverability over feature completeness.

---

## 11. Flashing philosophy

The device is currently in the CrossPoint ecosystem and official CrossPoint supports X4 Pro development/custom firmware workflows.

Nevertheless:
- Do not call the device "permanently unlocked" without verifying what the installed firmware/config actually permits.
- Experimental firmware should retain CrossPoint's recovery/update infrastructure.
- Keep a known-good official X4 Pro CrossPoint image and rollback instructions ready.
- Do not erase more flash regions than necessary.
- Do not improvise partition offsets.
- Do not use an X3/X4 binary.
- Do not perform first-flash experiments until the safety audit passes.

When possible, use the same documented X4 Pro flashing path as current upstream CrossPoint.

---

## 12. User preferences for this project

The user is technically comfortable but wants the agent to:
- explain significant architectural decisions
- work incrementally
- avoid risky blind changes
- use compiler/test feedback methodically
- keep changes easy to review
- avoid massive refactors unrelated to X4 Pro compatibility
- preserve future upstream mergeability
- explicitly warn before any device-flashing step
- provide copy/paste-friendly commands when manual action is required

Do not automatically flash hardware simply because a build succeeds. Ask/stop at the flash boundary and provide the safety assessment first.

---

## 13. First task for Codex

Start with investigation, NOT implementation.

Suggested prompt/task:

> Read this handoff and all repository-local agent/development instructions. Inspect the current Matcha Reader repository, current upstream CrossPoint repository, and their freeink-sdk revisions. Determine exactly what is missing for XTEINK X4 Pro support. Map X4 Pro-specific build flags, SDK changes, application-layer changes, and Matcha conflict zones. Do not flash hardware and do not make broad functional changes yet. Produce a concrete port plan, exact files likely to change, risks, and the smallest coherent Phase 1 change that should make `pio run -e x4pro` valid.

After that report is reviewed, proceed with Phase 1.

---

## 14. Important reminder about freshness

This handoff captures prior investigation and strategy, not immutable facts.

At the start of work:
- inspect current repository state
- inspect current branches/releases
- inspect current PlatformIO configuration
- inspect current freeink-sdk commits
- inspect current CrossPoint X4 Pro implementation
- prefer current source code over assumptions in this document

If current Matcha has gained official X4 Pro support since this handoff was written, STOP and reassess whether a custom port is still necessary.

---

## 15. Success definition

The project is successful when Matcha's Japanese-learning/reading experience runs reliably on the XTEINK X4 Pro while retaining the robustness and recoverability of current upstream CrossPoint X4 Pro support.

Long-term ideal:
- minimal X4 Pro-specific Matcha code
- hardware support inherited from upstream
- Matcha feature code remains device-agnostic
- clean enough to propose upstream to Matcha
- future CrossPoint merges remain manageable
