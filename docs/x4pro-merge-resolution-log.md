# X4 Pro merge resolution log

## Current checkpoint — 2026-09-11 (read this first)

**Status: integration incomplete; X4 Pro flash smoke test passed; phase-1 touch polish build-validated.**
This section supersedes older progress statements below, which are retained as history.
This log is the durable resume point after usage-limit interruptions. The X4 Pro
and original ESP32-C3 builds pass after boot hardening. No build is running. See the latest
result and task table below before consulting historical failure notes.

### Physical smoke test update - 2026-09-11

The initial integrated firmware was flashed to the user's X4 Pro and completed a
successful smoke test. Base CrossPoint features, including touch input, work on
device. Matcha-added features also work through their existing button paths.

Library and Reading Statistics touch support has now been implemented and
source/build validated. First device feedback found Library tap-to-open and
Stats scrolling working, but Library touch scroll behaved like cursor movement,
Library tab taps missed the drawn tab labels, and overall Stats still required
Confirm to reveal language tabs. Follow-up source fixes now make Library
vertical swipes scroll the viewport with scrollbars, route Library tab taps
through the theme tab hit helper, and open Language Stats on an overall-Stats
tap. Remaining follow-up: device-test the refined touch paths, then add
touch-first interaction to the remaining Matcha-added UI surfaces.

Latest device feedback after the Library/Stats refinement: touch controls are
working and feel more intuitive. Final phase-1 polish now build-validates: a
non-tab tap on Language Stats returns to overall Stats, and the X4 Pro build
produces a Matcha-named app binary (`matchareader-1.6.0-x4pro.bin`) alongside
the standard PlatformIO output.

### Latest verified results - 2026-09-11

**Boot hardening COMPLETE (source and build validation):** early recovery skips
saved state, SD fonts, credentials, optional peripherals and normal shortcuts.
The Arduino hook defers acceptance of NEW/PENDING images until a normal Home
render completes; the first update boot opens Home. Recovery/error/crash startup
does not accept the image. Release the recovery wake gesture, then hold Power
for two seconds to restart without saving normal state.

| Final check | Result | Evidence |
| --- | --- | --- |
| X4 Pro firmware | PASS, 82.29 s | `.cache/x4pro-audit/boot-hardening-firmware-final.log` |
| Original X4 firmware | PASS, 60.01 s | same log |
| Host tests | 232/232 PASS, including 7 OTA validation tests | `.cache/x4pro-audit/boot-hardening-tests.log` |
| Formatting / whitespace | PASS; wrapper includes new files via temporary index | `.cache/x4pro-audit/boot-hardening-format.log` |
| Both application images | Checksum, SHA trailer, bounds, board tag, size PASS | `.cache/x4pro-audit/boot-hardening-images.json` |
| Linked rollback hook | Both targets resolve to HalBootValidation.cpp.o | both firmware.map files / image-check JSON |

Latest X4 Pro application: 6,184,464 bytes, 369,136 bytes below configured slot
capacity; SHA-256 `6f4ff8c9c701f1bc8a12710b0c267f7bc81bcc431ad6544465443850c350a008`.
Original X4 application: 6,323,456 bytes. Source review and verification checklist
are in [the boot review](x4pro-boot-review.md). README/setup guidance is updated.
No flashing, commit, push or staging of the actual merge index occurred.

**Next:** device-test the new Library and Reading Statistics touch paths, then
implement touch support for the remaining Matcha-added manga, lookup and
translation surfaces. Continue the outstanding reader behavior fixes and prepare
deeper boot/recovery verification. Installed bootloader rollback remains
unverified. This completed hardening task and smoke test do not mark the whole
integration complete.

**Original pre-flash boot review (superseded by hardening above):** see [boot review](x4pro-boot-review.md).
User confirms SD Firmware Update is accessible. No definite new boot crash was
identified, but two high-impact recovery gaps prevent recommending flashing:
Arduino validates pending images before setup(), and recovery shares settings,
SD font and render initialization. Installed bootloader rollback is unverified.
Review also identified newer platform/SDK versions versus official 1.6.0.
No firmware code changes or flashing in this audit. Image structure checks pass.
Fresh X4 Pro build PASS (90.08 seconds); log: `.cache/x4pro-audit/boot-review-build-x4pro.log`.
Post-build image checksum, SHA trailer and board identity checks PASS. No build remains running.


**Build milestone COMPLETE. Integration remains incomplete.**

**Hardware constraint update:** the user's only device is a USB-locked Chinese
X4 Pro running official CrossPoint **1.6.0**, installed through
https://crosspointreader.com/unlocker. USB upload instructions do not apply.
The initial audit found the flasher, boot switch and SD updater identical to
official 1.6.0. The updater now has checked picker allocation; the flasher and
boot-switch implementation remain unchanged. SD update is the
intended future installation path. Application startup/recovery remains under
review; automatic rollback and independent rescue are unverified. Wireless
Check Updates is hidden on touch devices and the local updater's expected Pro
asset name differs from the official 1.6.0 release asset. No flashing performed.

Installation/test instructions are now saved in [x4pro-install-and-test.md](x4pro-install-and-test.md).
This documents physical flashing, an unverified emulator setup, and a test matrix.
Providing these instructions does not mark emulator or hardware validation complete;
neither installation nor flashing was performed.

| Check | Result | Evidence |
| --- | --- | --- |
| X4 Pro full firmware build | PASS, 86.19 s | `.cache/x4pro-audit/build-x4pro.log` |
| Original X4 / ESP32-C3 default build | PASS, 358.71 s including framework setup | `.cache/x4pro-audit/build-default.log` |
| Host suite, refreshed after this session's changes | PASS, 225/225 | `.cache/x4pro-audit/host-tests-all.log` |
| Repository formatter | PASS | `.cache/x4pro-audit/format.log` |
| Conflict-marker scan in src/lib/docs/test | No markers found | Git index still intentionally unmerged pending review |
| Hardware validation | NOT STARTED | No flashing performed |

Both builds generated firmware.bin and firmware.factory.bin. X4 Pro flash usage:
6,178,466 / 6,553,600 bytes (94.3%); ESP32-C3: 6,308,243 / 6,553,600 (96.3%).
Reported static RAM usage: X4 Pro 100,880 bytes; C3 57,712 bytes. These are link-time
figures, not runtime heap measurements. No compiler warnings/errors were found in
the final build logs; PlatformIO's Windows long-path advisory remains.

**No build process is running. No commit, push or flash was performed.**
Next task: reader-behavior review, starting with the visible-offset sync path and
layout-switch position preservation listed under Specific behavior-review findings.
Do not treat firmware binaries as device-validated releases.

### Active-session update - 2026-09-11

The build milestone has resumed. Verified and fixed successive compile failures:

- FileBrowserActivity: restored `RecentBooksStore.h`; the RECENT_BOOKS and idxB errors cleared.
- MangaChapterSelectionActivity: replaced the removed pagination helper with the
  rendered content area's size and the theme's existing list-page calculation.
- BmpViewerActivity: changed the two remaining upstream `TRANSPARENT_CUSTOM`
  references to Matcha's retained `TRANSPARENT` enum name (persisted value 7).

Reader audit in parallel with build waiting: the toolbar render tail now runs for
vertical as well as horizontal pages, including early-rendered vertical pages;
the overlay's guard/progress calculation and queued-turn guard accept either
layout engine. Individual compiler checks passed before the final guard edits;
the full build is the next verification. These changes need device UI tests.

Additional fixes in this session:
- Lyra icon lookup now returns nullptr for unsupported sizes.
- ButtonNavigator press helpers are instance methods, retaining per-instance repeat timing.
- Styled dictionary parsing supplies the merged furigana argument.
- Legacy Japanese font initializers migrated using the checked-in
  `lib/EpdFont/scripts/migrate-legacy-font-initializers.py`; the glyph/kerning
  arrays were compared against HEAD and are unchanged. No font rerasterization.
- Vertical toolbar guards/progress, queued page turns and automatic turning accept
  the vertical engine; chapter selection records vertical position before release.
- Restored the missing `buildTickHeapGate` implementation after the first link
  attempt failed with its undefined reference (171.75-second build).
- Repository formatting wrapper completed successfully. README now describes
  experimental toolbar and overlay paths. Device validation is still pending.
- Individual target checks passed for main.cpp, ButtonNavigator.cpp,
  DictHtmlPages.cpp, LyraTheme.cpp, EpubReaderActivity.cpp and BmpViewerActivity.cpp
  at their checked revisions. Full build and host-suite refresh are running.

Historical attempt notes (superseded by the successful build above):
Prior attempt failed after 111.45 seconds on the now-fixed image-viewer enum.
No new host-test changes; the saved 225/225 result remains the latest host run.

### Task status board

Last verified: 2026-09-11. Overall: **build milestone complete; X4 Pro smoke test passed; phase-1 touch polish build-validated**.
Both build processes have finished successfully. The user's initial X4 Pro flash
validated base CrossPoint behavior and button-driven Matcha features. The first
Library/Stats touch pass was device-tested; follow-up fixes for viewport
scrolling, tab tapping and touch entry to Language Stats now compile in the X4
Pro firmware build. Final polish also build-validates: non-tab taps hide
Language Stats again, and the X4 Pro build writes the named Matcha firmware copy.

| Task | Status | Evidence / next action |
| --- | --- | --- |
| Agree merge strategy and conflict decisions | Complete | Accepted decisions below |
| Apply source conflict resolutions | Complete, validation pending | No remaining markers in src/lib/docs/test |
| Set up local compiler and host-test tools | Complete | Compile database and host build succeed |
| Run current host test suite | Complete | 225/225 passed; host-tests-all.log |
| Build X4 Pro firmware | Complete | SUCCESS, 86.19 s; build-x4pro.log; device validation pending |
| Smoke-test X4 Pro firmware | Complete | User flashed the initial build; base CrossPoint features and button-driven Matcha features work |
| Add touch support for Matcha-added Library | Feedback fixes source/build validated; device retest pending | Tap/long-press routing works on device; follow-up changes make swipes scroll the viewport with scrollbars and route Books/Shelves tab taps through the theme tab hit helper. X4 Pro build PASS, 85.65 s, `.cache/x4pro-audit/touch-library-stats-feedback-build-x4pro.log` |
| Add touch support for Reading Statistics | Phase-1 polish source/build validated; device retest pending | Stats vertical scroll and tap-to-language-details work on device; final polish makes a non-tab tap on Language Stats return to overall Stats. X4 Pro build PASS, 98.73 s, `.cache/x4pro-audit/phase1-polish-build-x4pro.log` |
| Add touch support for Manga lists | Pending | Convert manga chapter/bookmark lists toward `UiListActivity`; tap opens, long-press deletes bookmarks |
| Add touch support for Manga reader | Pending | Add reader-style page/panel/menu touch controls without disturbing prefetch/grayscale paths |
| Add touch support for lookup/translation panels | Pending | Add touch scrolling and entry navigation to word lookup and translation result screens |
| Review merged reader and rendering behavior | Started; incomplete | Follow reader/cache checklist below |
| Build original ESP32-C3 firmware | Complete | SUCCESS, 358.71 s; build-default.log |
| Name X4 Pro firmware artifact | Complete | `.pio/build/x4pro/matchareader-1.6.0-x4pro.bin` produced by `pio run -e x4pro`; standard `firmware.bin` remains for upload/OTA flows |
| Format and reconcile final documentation | Started | Formatter passed; README updated; user-guide and format audit pending |
| Test on physical hardware | Started | Initial X4 Pro flash smoke test passed; first Library/Stats touch pass tested; refined Library/Stats touch paths need device retest |
| Final integration review | Pending | Successful builds, relevant tests, documented device results/limits |

Tracking convention: before starting a milestone, mark it Started; after working,
record the result, evidence/log path and exact next action. If work stops without
a verified result, leave it incomplete rather than marking it Complete. On resume,
check any active process and its log before restarting a command. Keep this board
and the current checkpoint updated; older notes below are historical.

### Repository and agreed scope

- Workspace: `E:/matcha-reader`; branch: `MATC-001-marge-crosspoint-1.6.0`.
- Full local CrossPoint merge into Matcha, preserving Japanese reading, ruby, manga,
  dictionary/translation, library and reading statistics while adding X4 Pro hardware support.
- Matcha HEAD: `61ca61ba86e3c5709a24d1b9c4f3cf2d41488012`.
- CrossPoint MERGE_HEAD: `1f3d7458a77c31641581667887029a5384ac4d3c`.
- SDK: `7f6bd0f47a766eea18206dd19f723f3707b6c9d3`.
- Merge remains uncommitted. Original unmerged index entries remain even where the
  working files have been edited; Git's unmerged-file count is not the marker count.
- No conflict markers found in `src`, `lib`, `docs`, or `test` at this checkpoint.
  Marker removal does **not** mean semantic integration is finished.
- Initial X4 Pro flash smoke test succeeded after this checkpoint. No commit made
  and no push performed.

### Completed edits and evidence

- Foundation/settings/navigation retain both Matcha and CrossPoint additions;
  Matcha power-button Word Lookup stays persisted value 5, touch Confirm uses 6.
- Translation generator succeeded: 35 languages, 547 keys.
- Combined CSS cache version 22 and section cache version 76; bounded upstream
  CSS storage retains Matcha extended properties, scoped/compound selectors,
  chapter filtering and incremental writes with failure-safe promotion.
- ParsedText and HTML parser combine per-word fonts/ruby/visible offsets with
  upstream focus boundaries, link geometry and table handling.
- ReaderActivity uses CrossPoint's shared lifecycle. EPUB, TXT and XTC retain
  Matcha reading features/stats; manga directory dispatch is retained.
- Shared reader menus, settings, text settings and file browser use upstream UI
  controls with Matcha options/filtering. Matcha's custom library and footnote
  screens are retained, with compatibility edits.
- Library now registers FreeInkUI-backed touch hit targets over its custom grid,
  shelf and tab render paths. Reading Statistics screens now support touch swipe
  scrolling/month navigation and language-tab taps.
- Sleep edits retain Matcha's legacy transparent-art directories alongside upstream
  overlay paths. README/user-guide reconciliation still needs a final review.
- Individual X4 Pro compiler checks passed for CSS/parser/section/EPUB, TXT/XTC,
  manga, footnotes, recent library, settings/text settings, themes, Home,
  dictionary word selection and ActivityManager at intermediate revisions.
  Subsequent edits still require the full build; these checks are not device tests.
- **All 225 host tests passed**, including the 18 CSS tests and two chapter-parser
  tests. Log: `.cache/x4pro-audit/host-tests-all.log`.
  Build log: `.cache/x4pro-audit/host-build-all.log`.
- Host harness adaptations include Windows-portable directory creation, merged
  parser signatures and font API stubs. The font stubs do not validate real SD-font I/O.

### Previous build failure (resolved)

Last command: `platformio run -e x4pro`.
Log: `.cache/x4pro-audit/build-x4pro.log`.
Result: **FAILED after 69.15 seconds**.

`src/activities/home/FileBrowserActivity.cpp:86` references `RECENT_BOOKS` without
its declaration being visible. The same compile reports `idxB` undeclared at lines
121/123; check whether this is a cascading error after fixing the missing include.
An earlier erroneous `MangaBook.h` include was already removed; `MangaPanel.h`
provides the existing manga API. Do not reintroduce that header.

The X4 Pro build now passes (see latest result above). Original ESP32-C3 `default`
validation also passed. No hardware behavior has been validated.

### Specific behavior-review findings still open

These are not covered by the passing build/host suite and must be addressed before
calling the integration complete:

- `EpubReaderActivity::onReaderMenuConfirm`'s visible-offset sync branch maps only
  a horizontal section and otherwise sets `pendingOffsetJump`, without resetting
  the vertical section. The vertical builder reads `cachedVisibleTextOffset`
  instead. Reconcile the two paths and validate same-/cross-spine vertical jumps.
- Horizontal section-cache loading clears `cachedVisibleTextOffset` before
  choosing the deferred offset. Check position preservation when toggling back
  to an already-cached layout; do not assume a successful cache read makes the
  old page number valid across layout modes.
- Two `ParsedText::getSpaceAdvance` calls (greedy breaking and natural-gap total)
  still omit `blockStyle.letterSpacing` while the surrounding render paths pass
  it. Reconcile measurements and rendering, then add focused layout regression
  coverage if feasible.
- `Epub::load`'s new-book CSS path still checks ParseResult != Error, whereas the
  cached-book path treats only Complete as a promoted cache. Review partial and
  low-memory outcomes for unnecessary section invalidation.
- Confirm toolbar More actions on image-only pages, allocation-failure paths,
  touch routes for custom manga/library screens, and grayscale/SD-memory lifecycle.

### Matcha-added touch support plan

Priority order agreed after the first successful X4 Pro flash:

1. **Library touch support** (`RecentBooksActivity`) - feedback fixes source/build validated:
   keep the custom Matcha
   cover-grid/shelf renderer, but add FreeInkUI-backed hit routing instead of
   new manual touch geometry. Touch should support Books-grid tap-to-open,
   long-press-to-stats, Shelves-tab tap switching, shelf-row opening, and
   shelf-detail book tap/long-press. Device feedback from the first pass showed
   tap-to-open working; follow-up changes make vertical swipes scroll the grid
   viewport instead of moving the selection cursor and add grid scrollbars.
2. **Reading Statistics touch support** (`ReadingStatsActivity`,
   `LanguageStatsActivity`, `BookStatsActivity`) - feedback fixes source/build validated: add vertical swipe scrolling,
   horizontal month navigation, Details activation from overall stats, and
   language-tab selection. Back swipe should continue to use the global Back
   mapping.
3. **Manga list touch support** (`MangaChapterSelectionActivity`,
   `MangaBookmarksActivity`): convert toward `UiListActivity` where practical.
   Tap should open, and bookmark long-press should use the same delete
   confirmation model as EPUB bookmarks.
4. **Manga reader touch support** (`MangaReaderActivity`): add reader-style
   page/panel/menu touch controls while preserving panel prefetch, deferred
   grayscale upgrades and existing button behavior.
5. **Lookup and translation touch polish** (`EpubReaderWordLookupActivity`,
   `MangaWordLookupActivity`, `EpubReaderTranslationActivity`): add touch
   scrolling and entry navigation without changing dictionary scan/cache
   semantics.

Implementation rule: new touch surfaces should use the FreeInkUI routing stack
from `UiAppHost`/`UiListActivity` where feasible. Do not add new legacy
`rowTouch`, `colTouch` or raw rectangle hit tests except in the already-allowed
Home/reader-page surfaces.

### Resume checklist (small milestones)

1. **Build milestone COMPLETE:** X4 Pro and original ESP32-C3 compile/link and
   binary generation passed. Do not mark the merge complete just because it compiles.
2. **Touch follow-up milestone:** device-test Library and Reading Statistics
   touch support, then add manga lists, manga reader, and lookup/translation
   polish in that order. Preserve existing button behavior and avoid
   steady-state heap allocations in render loops.
3. **Reader behavior milestone:** audit vertical and horizontal chapter navigation,
   position preservation when opening/cancelling chapter selection, progress/sync
   jumps, settings return and orientation changes. Recent edits reset both layout
   engines but the chapter-selection path still needs careful position review.
   Review toolbar rendering on vertical pages: overlay rendering is currently
   attached to a horizontal render tail and may need its vertical counterpart.
   Check Japanese toolbar filtering, disabled text actions on image-only pages,
   footnote/link routes, auto-turn and end-of-book/statistics behavior.
4. **Rendering/cache milestone:** audit merged ParsedText space-advance calls for
   Matcha letter spacing and per-word fonts, and merged image decode/grayscale/
   polarity/cancellation paths. Confirm CSS partial/error outcomes never promote
   incomplete caches or persist transient failure. Add meaningful regressions for
   any discovered bugs; current parser tests cover only a narrow link case.
5. **Validation milestone:** both builds and formatting passed at this checkpoint.
   After behavior fixes, rerun affected tests/builds and only `bin/clang-format-fix -g`
   for formatting. No raw clang-format invocation/probing.
6. **Documentation/review milestone:** reconcile README/user guide with final menus,
   sleep overlay paths and experimental hardware support; review file-format docs
   against actual serialization. Update this log with exact results and remaining
   device checks. Leave flashing, committing and pushing to explicit user requests.

### Reproducible local commands

PowerShell firmware build (temporary files stay on E:):

```powershell
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONUTF8='1'
$env:TEMP='E:\matcha-reader\.cache\p'
$env:TMP=$env:TEMP
& 'C:\Users\Renzey\.platformio\penv\Scripts\platformio.exe' run -e x4pro *> .cache/x4pro-audit/build-x4pro.log
```

Host tests:

```powershell
$env:PATH='E:\matcha-reader\.cache\host-tools\llvm-mingw-20260908-ucrt-x86_64\bin;'+$env:PATH
& .cache/host-python/cmake/data/bin/cmake.exe --build .cache/host-build --parallel 4
& .cache/host-python/cmake/data/bin/ctest.exe --test-dir .cache/host-build --output-on-failure -j 4
```

Individual target syntax helper (sequential use only; shared response file):
`python .cache/x4pro-audit/check-syntax.py FileBrowserActivity.cpp`.

**Do not rerun the one-shot `resolve-*.py` or `fix-*.py` scripts** under
`.cache/x4pro-audit`: their edits are already applied. Inspect source and patch
incrementally. This cache also contains pre-edit reference copies and compiler logs.

**Do not delete `.cache/pio-shared-tools` or `.cache/pio-packages`: shared PlatformIO
paths on C: are junctions into these directories.** `E:/mpio` points to this
workspace's `.cache/pio-core`; `platformio.local.ini` is intentionally ignored.
Read `AGENTS.md` before proceeding. Do not spawn subagents unless explicitly asked.

---

## Earlier chronological notes

Work in progress on `feature/x4pro`. Firmware and hardware validation remain pending. The CSS host suite passes (18 tests).

## Accepted decisions

The user approved preserving Matcha's power-button lookup behavior, retaining both restart/navigation additions, following CrossPoint's UI and reader lifecycle, preserving Matcha features as extensions, reconciling cache/data contracts and retaining both rendering capabilities.

## Foundation edits

- `WORD_LOOKUP` remains persisted value 5; touch-capable Confirm uses value 6. The settings options follow that order. Existing unversioned value 5 is interpreted with Matcha semantics, as requested; it is not heuristically reinterpreted as a CrossPoint setting.
- Retained both Reading Stats and USB Drive routes.
- Retained translation retry and exclusive-storage reboot entry points. Translation resumed directly after boot now explicitly requests a reader reboot on exit: CrossPoint's normal no-reboot Wi-Fi exit on touch hardware would otherwise pop an empty activity stack and land Home. The deep-sleep guard still supersedes both restart paths.
- Kept the upstream FUI gesture/frontlight settings and Matcha dictionary settings. Touch defaults follow current CrossPoint.
- Preserved Matcha README-maintenance instructions in the relocated `AGENTS.md` and its ignored API-key file. CI uses the upstream device matrix and current build dependency setup; Matcha's duplicate Sticky job is redundant with that matrix.
- Initialized the SDK at `7f6bd0f47a766eea18206dd19f723f3707b6c9d3`.

## Rendering interface edits (not yet validated)

- Retained both font memory-release APIs, lazy font-file reopening and upstream resident-cache release.
- Retained ruby-aware rendering and added link metadata; TextBlock carries both Matcha per-word font IDs and upstream link spans.
- Retained manga upscaling and image polarity preservation. Panel-residue tracking uses the effective refresh mode after upstream promotion.
- Combined decoder cancellation with periodic watchdog yielding; retained cache-only decoding and Matcha lightening/cropping behavior while incorporating upstream PNG alpha and image-dimension validation.
- Preserved Japanese gaiji replacement and dictionary headword normalization alongside upstream XML/dictionary additions.

These edits remain unstaged over the conflict index until reviewed and verified. Major CSS/parser, reader lifecycle and UI conflicts are still open. Marker removal alone is not recorded as a validated resolution.

## Translation validation

Reconciled duplicate keys in both conflicted and automatically merged YAML files, retaining Matcha feature strings and compatible format placeholders. Japanese now has the `ja` language tag and a unique legacy order value. The translation generator passes: 35 languages, 547 keys. Generated C++ remains ignored.

## Local tooling

Found PlatformIO at `C:/Users/Renzey/.platformio/penv/Scripts/platformio.exe`. Initial dependency installation failed while extracting an ESP32-C6 header under a long Windows cache path. A shorter cache path allowed package installation, but subsequent ESP-IDF tool setup exhausted C:.

The newly updated Arduino framework libraries were moved to `E:/matcha-reader/.cache/pio-packages/framework-arduinoespressif32-libs`; a junction preserves their original PlatformIO path on C:. **This directory is now referenced by the shared PlatformIO installation; do not delete it as disposable cache.** This recovered roughly 2 GB on C: at the time of the move.

Ignored `platformio.local.ini` now sets this project's core directory to `E:/matcha-reader/.cache/pio-core` and cache directory to `E:/matcha-reader/.cache/p`. Compile-database setup is still pending; no firmware build or flash has completed. Setup logs are in `.cache/x4pro-audit/compiledb.log`.

## Resume checkpoint

The user reported a usage-limit notice during work. All edits are on disk; the branch remains an uncommitted merge. The initial conflict index is intentionally retained even for edited files, so `git diff --diff-filter=U` overstates files still containing markers. Inspect both conflict markers and the resolution log before continuing; do not rerun the one-shot resolution scripts against already edited files.

The compile-database setup attempt has stopped; no tool installation is intentionally left running. C: still has little free space and tool setup needs further repair before compilation. The latest failure is recorded in `compiledb.log`.

Next: reconcile the substantial CSS storage/API redesign against Matcha's chapter-filtered and vertical cache handling; then integrate the reader base lifecycle and feature-specific readers/UI. Remaining syntax and behavioral problems are expected until those contracts are complete. No test or safety approval should be inferred from the number of removed markers.

## Resumed integration: CSS and text layout

- Combined CrossPoint's bounded, fallible flat CSS storage with Matcha scoped/compound selectors, extended properties, chapter filtering and incremental cache writes. The merged CSS format is version 22 (flags header plus the expanded 88-byte style payload).
- Incremental writes now use the temporary/backup promotion path. Failed or incomplete parses preserve the previous cache. Duplicate selector records merge in order when a chapter loads. Cache inspection validates records without hydrating the book-wide table.
- Section cache version is 76; its partial sentinel derives from that version. EPUB section builds retain Matcha's filtered loading and distinguish low memory from invalid cache through the upstream typed result.
- CSS host suite: **18/18 passed**, including new extended-property round trips, 1,700-rule chapter filtering, duplicate flush merging, vertical collection, discard preservation and failed promotion recovery. The test storage shim now compares Windows paths structurally so rename-failure injection works with mixed separators.
- `ParsedText` conflicts have been edited to retain per-word font sizes and visible offsets with upstream focus-boundary and link metadata. This part is not yet compiled or behaviorally validated.

### Tooling recovery

Moved shared `C:/Users/Renzey/.platformio/tools` to `E:/matcha-reader/.cache/pio-shared-tools` and left a junction at its original path. **Do not delete this workspace directory as disposable cache: the shared PlatformIO installation references it.**

`E:/mpio` is a short junction to this project's `.cache/pio-core`, and ignored `platformio.local.ini` now uses it. Keeping temporary downloads on E: and setting Python UTF-8 output fixed the subsequent setup failures. **X4 Pro compile-database generation succeeded**; this is not a firmware build.

Portable LLVM-MinGW and CMake were installed under ignored `.cache/host-tools` and `.cache/host-python`. Host test build files live in `.cache/host-build`. Evidence: `.cache/x4pro-audit/css-tests.log`, `css-build.log`, `compiledb.log`.

The CSS and ParsedText resolution scripts are one-shot transforms; do not rerun them on edited files. CSS pre-edit backups are in `.cache/x4pro-audit/css-before.{h,cpp}`. Reader, HTML parser and UI conflicts remain open; no firmware has been flashed, committed or pushed.
