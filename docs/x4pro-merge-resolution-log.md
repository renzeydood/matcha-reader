# X4 Pro merge resolution log

## Current checkpoint — 2026-09-14 (read this first)

**Status: integration functionally complete and device-validated; behavior-review findings resolved and device-confirmed. Three follow-up bugs from the Vertical Text toggle are fixed and awaiting device confirmation.**
Japanese font sizing, furigana scaling and the word-lookup/translation touch work are
device-verified by the user. Both firmware targets build and the host suite is green
(see the Phase 5 validation checkpoint below). The five behavior-review findings have
been re-verified: three were real and are fixed, two were not defects (see that
section); the two position fixes are confirmed on hardware.
Most recent work: three bugs the user found while exercising the Vertical Text toggle
— a stale settings row, overrides never persisting (critical), and wrong word-lookup
highlight geometry in horizontal mode. All fixed and build/test-validated; see
"Vertical-text toggle follow-up bugs" below for the device checks they need.
This section supersedes older progress statements below, which are retained as history.
This log is the durable resume point after usage-limit interruptions. No build is running.
See the latest result and task table below before consulting historical failure notes.

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

Manga list touch support and manga reader page/menu touch support were added
next. The user device-tested the manga lists successfully and quick-tested the
manga reader touch path successfully. Work is currently paused from the touch
roadmap to fix a Japanese EPUB text-settings bug: the reader renders Japanese
through the effective companion font, but the font-size/settings surfaces were
still using the raw selected Latin family context in parts of the UI.

### Japanese font-size & Furigana scaling and Phase 3 Touch Support update - 2026-09-14

1. **Japanese Font Size and Furigana Resizing**:
   - Fixed overlay font picker in `EpubReaderActivity::showTextRowPopup()` by adding missing `case 0:` (Font Family picker).
   - Dynamic Furigana scaling in vertical mode (`VerticalTextBlock.cpp`, `CrossPointSettings.cpp`, `EpubReaderActivity.cpp`) now passes `effectiveReaderFontId()` for both main text and ruby text, achieving dynamic 50% superscript scaling across 12pt, 14pt, 16pt, and 18pt without requiring extra font files. Device-verified by user.

2. **Phase 3 Touch Support (Word Lookup and Page Translation)**:
   - Updated `WordSelectionScan.cpp` to store word bounding coordinates (`wx`, `wy`) in horizontal mode.
   - Added direct word touch selection, side navigation taps, and swipe gestures to `EpubReaderWordLookupActivity` and `MangaWordLookupActivity`. Tapping on or near any Japanese word on the book page directly activates lookup for that word.
   - Refined Word Lookup UI & Layout: Fixed font-size and formatting drift by passing `effectiveReaderFontId()` into `EpubReaderWordLookupActivity`; moved vertical Japanese side-lines (傍線 bousen) to the left side of vertical columns to prevent overlapping furigana on the right side; removed the floating overlay card from the page view to restore clean full-screen reading layout; pressing [Confirm] or tapping the active word now opens `DictionaryDefinitionActivity` directly in full screen (matching original Matcha Reader UX).
   - Added touch scrolling (swipes and upper/lower screen taps) and back gesture / header close taps to `EpubReaderTranslationActivity`.
   - Built and verified X4 Pro firmware (`matchareader-1.6.0-x4pro.bin`, `SUCCESS`).

### Phase 5 validation checkpoint - 2026-09-14

Scope agreed with the user: commit the pending word-lookup work, reconcile
documentation and re-validate. The open behavior findings below ("Specific
behavior-review findings still open") were **explicitly deferred** and are not
addressed by this checkpoint.

Verified results:

| Check | Result | Evidence |
| --- | --- | --- |
| X4 Pro firmware build | SUCCESS, 117.48 s. RAM 30.8% (100,896 / 327,680 B), Flash 94.4% (6,188,474 / 6,553,600 B) | `.cache/x4pro-audit/phase5-build-x4pro.log`; `matchareader-1.6.0-x4pro.bin` produced |
| ESP32-C3 firmware build | SUCCESS, 106.78 s. RAM 17.6% (57,720 / 327,680 B), Flash 96.4% (6,319,561 / 6,553,600 B) | `.cache/x4pro-audit/phase5-build-default.log` |
| Host test suite | 232/232 passed, 2.43 s | `.cache/x4pro-audit/phase5-host-tests.log` |
| Formatting | `bin/clang-format-fix -g` clean, 0/6 files changed | run before commit |

Documentation reconciled against shipped behavior rather than intent:

- `USER_GUIDE.md` §6.2 now describes the retained reading layout, left-side
  bousen in vertical text, underlines in horizontal text, the selection box,
  full-screen definitions on Confirm, and the touch gestures. A stale claim that
  the front Left/Right buttons "scroll" during selection was removed — the
  definition is a separate full-screen activity, so those buttons move between
  matched words instead.
- `USER_GUIDE.md` §6.3 and the README translation section document swipe and
  half-page-tap scrolling plus header-tap close.
- `docs/dictionary.md` documents the touch paths that CrossPoint 1.6.0 already
  ships in `DictionaryWordSelectActivity` and `DictionaryDefinitionActivity`.
  These were **not** added by this integration; the doc simply never covered them.
- README status banner updated: touch now covers Library, reading statistics,
  manga lists, manga reader, word lookup and page translation.

Flash headroom is the notable risk carried forward: 94.4% on X4 Pro and 96.4% on
ESP32-C3. Future feature work should budget for this before adding fonts or
assets.

**Build gotcha:** running PlatformIO without `PYTHONIOENCODING='utf-8'` makes the
stdout reader thread die with `UnicodeEncodeError: 'charmap' codec` while printing
the i18n language table, after which the pipe never drains and the build appears
to hang forever with no compilation occurring. Always use the environment shown
under "Reproducible local commands".

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

Last verified: 2026-09-12. Overall: **build milestone complete; X4 Pro smoke test passed; manga touch device-validated; Japanese font-size fix in progress**.
Both build processes have finished successfully. The user's initial X4 Pro flash
validated base CrossPoint behavior and button-driven Matcha features. The first
Library/Stats touch pass was device-tested; follow-up fixes for viewport
scrolling, tab tapping and touch entry to Language Stats now compile in the X4
Pro firmware build. Final polish also build-validates: non-tab taps hide
Language Stats again, and the X4 Pro build writes the named Matcha firmware copy.
Phase 2 has started with manga chapter/bookmark lists converted to the shared
FreeInkUI list base. The X4 Pro build passed, and the user device-tested this
slice successfully: manga chapter/bookmark touch features are working.
Phase 3 touch support and hardware button navigation for Word Lookup and Translation
were implemented and built into `matchareader-1.6.0-x4pro.bin` (SUCCESS).
Page-Highlight Mode with floating definition overlay implemented:
- All dictionary-detected words on the page are marked/underlined.
- Active word has a prominent highlight rectangle on top of the book text.
- Floating definition preview panel displayed at the bottom of the screen.
- Tapping or pressing Confirm on the floating panel opens the full multi-page definition entry.
Manga reader touch controls now source/build validate: page/panel turns and
menu gestures are routed through the same reader touch helpers as EPUB,
preserving existing button behavior and prefetch/grayscale lifecycles.
The current active bugfix makes Japanese Text Settings use the same effective
companion font context as EPUB rendering, and ensures returning from Reader
Settings invalidates/rebuilds the active text layout.

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
| Add touch support for Manga lists | Device validated | `MangaChapterSelectionActivity` and `MangaBookmarksActivity` converted toward `UiListActivity`; tap opens, swipes scroll, bookmark long-press uses a tap-operable delete popup. X4 Pro build PASS, 101.55 s, `.cache/x4pro-audit/phase2-manga-lists-build-x4pro.log`; user device test passed |
| Add touch support for Manga reader | Device validated | Route configured reader tap/swipe page turns and center/menu gestures through `MangaReaderActivity` without disturbing panel prefetch, deferred grayscale upgrades or existing button behavior. X4 Pro build PASS, 84.84 s, `.cache/x4pro-audit/phase2-manga-reader-build-x4pro.log`; user device test passed |
| Fix Japanese EPUB font-size editing | Complete, device verified | Text Settings and the reader Text panel consult the loaded Japanese companion font when the selected reader font lacks CJK coverage; the overlay font-family picker case was restored; furigana scales with the main text across 12/14/16/18 pt. User device-verified. Committed as `c18ae8ba` |
| Add touch support for lookup/translation panels | Device validated | `EpubReaderWordLookupActivity` renders the page with `effectiveReaderFontId()` so lookup keeps the reading layout; matched words carry left-side bousen (vertical) or underlines (horizontal); tap selects, tap-again/Confirm opens `DictionaryDefinitionActivity` full screen. `EpubReaderTranslationActivity` gained swipe/half-page-tap scrolling and header-tap close. User device-verified |
| Review merged reader and rendering behavior | Findings resolved; two device-verified | Five behavior findings re-verified against source: two were not defects (stale/overstated), three were fixed in `87e9f0c8`. The vertical-jump and vertical-toggle fixes are device-verified — they respectively restore bookmarks in Japanese books and position retention across a Vertical Text toggle. Letter-spacing fix is build-verified only. Remaining checklist items below still open |
| Build original ESP32-C3 firmware | Complete | SUCCESS, 358.71 s; build-default.log |
| Name X4 Pro firmware artifact | Complete | `.pio/build/x4pro/matchareader-1.6.0-x4pro.bin` produced by `pio run -e x4pro`; standard `firmware.bin` remains for upload/OTA flows |
| Format and reconcile final documentation | Started | Formatter passed; README, USER_GUIDE §6.2/§6.3 and `docs/dictionary.md` reconciled with the shipped lookup/translation touch behavior; file-format doc audit pending |
| Test on physical hardware | Started | Initial X4 Pro flash smoke test passed; Library/Stats touch tested; manga list touch tested and working |
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

### Specific behavior-review findings — resolved 2026-09-14

All five entries were re-verified against current source rather than carried forward
on the log's word. Two did not survive inspection; three were real and are now fixed.

**Fixed:**

- **Vertical progress/sync jump.** `EpubReaderActivity::onReaderMenuConfirm`'s
  visible-offset branch tested only `section`, which is always null in vertical
  mode, so every jump took the `else` and set `pendingOffsetJump` — a channel only
  the horizontal path consumed. `verticalSection` was never reset, leaving the
  stale chapter on screen. The handler now resolves same-spine jumps against
  whichever engine is active and drops both engines on a cross-spine jump; the
  vertical builder now consumes `pendingOffsetJump`, which (unlike
  `cachedVisibleTextOffset`) stays valid across spines.
  User-visible symptom: **bookmarks did not work in Japanese books** — opening one
  left the reader on the current chapter. `EpubReaderBookmarksActivity` feeds the
  same `progressChangeResultHandler` as KOSync, so bookmarks travelled the same
  broken branch. Device-verified fixed.
- **Offset cleared before use.** Horizontal section-cache loading reset
  `cachedVisibleTextOffset` before the `offsetJump` expression read it, so the
  fallback was always `nullopt` on a cache hit and the position fell back to a
  page number numbered in the other layout. The anchor is now captured into
  `carriedOffset` before the reset.
  User-visible symptom: **toggling Vertical Text lost the reading position** over an
  already-cached chapter. Device-verified fixed.
- **Missing letter spacing.** The greedy-breaking and natural-gap `getSpaceAdvance`
  calls in `ParsedText` now pass `blockStyle.letterSpacing`, matching the render
  paths and every sibling call.

**Not defects:**

- *Vertical toolbar overlay* — already correct. `renderOverlayAfterPage()` is called
  at both vertical tails (the early-display return and the normal tail), so the
  overlay is attached to vertical rendering. The log entry was stale.
- *`Epub::load` CSS `ParseResult` asymmetry* — overstated. A transient CSS failure
  is **not** persisted: `CssParser::endCacheAppend` refuses to promote when
  `heapTruncated_` or `ruleGrowthStopped_` is set, deletes the tmp file and lets the
  next open retry, so the "never persist a transient failure" rule already holds.
  What remains is a narrow inconsistency: the new-book path invalidates the sections
  dir on `Partial` while the cached path only does so on `Complete`. On a genuinely
  new book there are no prior sections, so the practical impact is a redundant
  rebuild rather than wrong data. Left as-is by agreement; revisit only if section
  rebuild cost becomes visible.

Still unverified (unchanged): toolbar More actions on image-only pages,
allocation-failure paths, and grayscale/SD-memory lifecycle.

**Device verification (2026-09-14): the two position fixes are confirmed on hardware.**
Both had user-visible symptoms that predated this work: bookmarks did not jump in
Japanese books, and toggling Vertical Text lost the reading position. Both now behave
correctly on device.

KOSync is **not** required to exercise either fix. `EpubReaderBookmarksActivity`
builds the same `ProgressChangeResult` and is routed through the same
`progressChangeResultHandler`, so opening a bookmark covers the visible-offset
branch — mid-chapter for the same-spine case, a different chapter for the cross-spine
case (the one that was broken). The Vertical Text toggle covers the cache-load fix
and touches no sync code at all.

The letter-spacing fix remains source-reasoned and build-verified only; it needs a
book with CSS `letter-spacing` on justified text, which is uncommon. Low risk — the
change aligns two calls with the three sibling call sites that already passed the
argument.

### Vertical-text toggle follow-up bugs — fixed 2026-09-14

Found by the user while exercising the Vertical Text toggle after the position
fixes above landed. All three are fixed; none were regressions from that work.

- **Settings row did not repaint after toggling.** The `DynamicToggle` branch in
  `SettingsActivity::onConfirm` returned early to skip `saveSettings()` — the
  override is book-scoped, not a global setting — but that early return also
  skipped `rebuildSettingsLists()`, which is what regenerates the row's value
  text. The row kept reading "Vertical" until the user left and re-entered the
  menu. The branch now rebuilds the lists (preserving `activeNav().selected`
  across the rebuild) and requests an update, still without `saveSettings()`.

- **Vertical never stuck; horizontal was permanent.** *(critical)* The
  vertical/furigana overrides live in `progress.bin` bytes 6 and 7, and their only
  write path was the render-path save — which is gated on `currentSpineIndex`,
  `page` and `pageCount`. An override changes **none** of those, so the gate held
  and nothing was written. `onExit()` does not save progress either (only the
  footnote-origin special case), so a toggle followed by sleep or by closing the
  book left the previous value on disk. The asymmetry the user saw was incidental:
  whichever value happened to be on disk won, and that was horizontal.

  `applyVerticalFuriganaOverride` now calls a new `persistOverrideChange()` the
  moment the override changes. Ordering matters: the call must happen **inside the
  RenderLock and before `section.reset()` / `verticalSection.reset()`**, because
  `saveProgress` derives the visible-text anchor from the live section. Saving
  after the reset would write a 9-byte record with no offset and destroy the anchor
  the rebuild needs. `persistOverrideChange()` also syncs `lastSavedSpineIndex` /
  `Page` / `PageCount` so the following render does not rewrite the same record.

  Principle: *a user decision must be durable at the moment it is made*, not as a
  side effect of a later position change.

  While here, the redundant horizontal save that followed
  `runPostRenderTail(..., vertical=false, ...)` was removed. `runPostRenderTail`
  has no early returns, so its identical gated save always runs; the duplicate
  additionally compared `section->pageCount` while storing
  `section->estimatedTotalPages()`, so during partial builds its guard never
  settled and it wrote to SD on every single render.

- **Word-lookup highlight geometry wrong in horizontal mode.** Two independent
  causes, both in `WordSelectionScan::initFromPage`:
  - *Box collapsed / drifted in x.* Every character of a word was pushed with the
    same x (`line.xPos + block.wordXpos(wi)`) — a `TextBlock` stores one x per
    **word**, not per character — so the bounding box was about one character wide
    at the word origin. The scan now walks a pen, advancing by each character's
    measured `getTextAdvanceX` using the word's own resolved font and style.
  - *Underline drawn through the middle of the word.* `line.yPos` is the top of the
    **reserved** line box, which includes the furigana band; `TextBlock::render`
    shifts words down by `getRubyShift(ascender)` when the line has ruby. Using the
    unshifted `yPos` with `getLineHeight()` put the underline near the glyph
    centres — which is why it only looked like a strikethrough on Japanese
    (ruby-bearing) lines. The scan now applies the same ruby shift and sizes the box
    as `ascender - descender`.

  `GlyphRef::column` / `row` are reused in horizontal mode to carry advance width
  and box height (they hold grid coordinates in tategaki), keeping `GlyphRef` at 20
  bytes and adding no per-glyph allocation. Required a new
  `GfxRenderer::getFontDescenderSize()` accessor, mirroring `getFontAscenderSize()`;
  it returns the raw `EpdFontData::descender`, which is **negative**, hence
  `ascender - descender` for height.

  Existing `wlscan.bin` caches stay valid: `glyphContentHash()` mixes only codepoint
  and paragraph index, never geometry. Geometry is recomputed from the page on every
  open, which is correct — the cache records *which* glyphs are selectable, a text
  property, not where they sit.

Validation: X4 Pro build SUCCESS (140.24 s, RAM 30.8%, Flash 94.4%); ESP32-C3 build
SUCCESS (117.64 s); host suite 232/232 in 4.29 s; `bin/clang-format-fix -g` applied.

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
    `MangaBookmarksActivity`) - device validated: convert toward `UiListActivity` where
   practical. Tap should open, swipes should scroll the viewport without moving
   the selected cursor, and bookmark long-press should use the same delete
   confirmation model as EPUB bookmarks.
4. **Manga reader touch support** (`MangaReaderActivity`) - source/build validated: add
   reader-style page/panel/menu touch controls while preserving panel prefetch,
   deferred grayscale upgrades and existing button behavior.
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

## Japanese Ebook Font Size & Overlay Picker Fix

- Added missing `case 0:` (Font Family) to `showTextRowPopup(const int row)` in `EpubReaderActivity.cpp` so clicking "Font" in the Text toolbar panel opens the Font Family selection popup.
- Resolved Japanese font sizing behavior across `SdCardFontSystem`, `TextSettingsActivity`, `TextSettingsPreview`, and `EpubReaderActivity`.
- Documented flash constraints: Built-in CJK glyphs in flash (`notosansjp_joyo_12_regular`) only exist at 12pt due to ESP32-C3 flash limits. For multi-size (12, 14, 16, 18pt) Japanese text rendering, an SD Japanese font (`NotoSansJP.cpfont` or `NotoSerifJP.cpfont`) is loaded automatically by `SdCardFontSystem` from `/.fonts/` or `/fonts/` on the SD card.
- Updated `CrossPointSettings::getRubyFontId()` and `EpubReaderActivity::renderVerticalPageBody()` so Furigana font size in vertical text mode scales dynamically with the active reader font point size (`effectiveReaderFontId()`).


