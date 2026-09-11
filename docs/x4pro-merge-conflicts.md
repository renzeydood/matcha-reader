# Exploratory full merge: conflict map

Date: 2026-09-10. Status: inventory completed; resolutions not yet applied.

## Repository state

- Integration branch: `feature/x4pro`.
- Matcha parent / unchanged `master`: `61ca61ba86e3c5709a24d1b9c4f3cf2d41488012`.
- CrossPoint merge parent: `1f3d7458a77c31641581667887029a5384ac4d3c`.
- Merge-base: `d4733bfc65aa6b95518ddb39b9554d5372d96018`.
- Ran `git merge --no-commit --no-ff` against the pinned CrossPoint parent.
- Git stopped with **75 conflicted files, containing 285 conflict blocks**. The overall working-tree comparison against Matcha contains 371 changed tracked paths.
- No manual conflict resolutions, commit, push, build or flash were performed. The branch is intentionally left in an unfinished merge state for the resolution pass. HEAD still points at the Matcha parent; MERGE_HEAD records CrossPoint.
- The user's handoff and the earlier investigation document remain untracked and preserved. This report is also untracked.

The complete initial inventory appears below. Line numbers refer to this initial conflicted working tree and will move during resolution. Raw conflict excerpts and machine-readable counts are saved in ignored `.cache/x4pro-audit/conflict-excerpts.md` and `conflict-inventory.json`.

## What merged automatically

`platformio.ini`, the SDK gitlink, `lib/hal/HalGPIO.{cpp,h}`, `lib/hal/HalStorage.{cpp,h}` and the power HAL have no textual conflicts. The SDK index entry is the expected `7f6bd0f47a766eea18206dd19f723f3707b6c9d3`; the working submodule still needs initialization before building.

The merged configuration contains X4 Pro, upstream opt-in core tuning and Matcha's `-Ilib/Epub/Epub`. The merged HAL retains `anyButtonDownRaw()` and `modifiedStamp()`. This supports the history-based finding that Matcha's hardware-adapter changes are small.

Automatic merging is not verification. Review these files together with `src/activities/ActivityManager.cpp`, `src/CrossPointSettings.cpp`, `src/MappedInputManager.*`, firmware/update code and new USB activity/PHY handoff code. They can reference conflicting interfaces or introduce changed semantics even without conflict markers.

## Concrete decisions to resolve first

### 1. Settings values: a real semantic collision

Evidence: `src/CrossPointSettings.h:144` assigns value 5 to Matcha `WORD_LOOKUP` and CrossPoint `PWR_CONFIRM`. Both cannot retain that value in the integrated enum. Other settings conflicts include sleep-screen naming (`:24`) and touch/default/dictionary fields (`:325`).

Recommendation: preserve existing Matcha persisted meanings, assign a distinct value to Confirm, and update settings descriptors, defaults and all dispatch sites together. For settings brought from official CrossPoint on the user's SD card, establish a migration/source-detection policy; do not guess whether an unversioned 5 means lookup or Confirm. Test settings loaded from both parents, a fresh install, and invalid values. Do not assume combining enum lines fixes compatibility.

### 2. Restart functions: compatible additions with different ordering needs

Evidence: `src/SilentRestart.h:10` places Matcha's translation retry declarations opposite CrossPoint's storage-handoff reboot declaration. `src/main.cpp:220` contains their implementation conflict. Matcha commit `20f93a8a` explains the bounded fresh-heap TLS retry; CrossPoint `218dc6aa` introduces USB mass storage.

Recommendation: retain both entry points. Preserve translation stash consumption and bounded retry, and preserve the USB path's exclusive-storage shutdown and PHY return before reboot. Check automatically merged touch-board Wi-Fi shutdown logic against translation's requirement for an actual restart. Verify each destination and missing-stash fallback; no arbitrary consolidation into a single restart path.

### 3. Activity navigation: a simple additive conflict

Evidence: `src/activities/ActivityManager.h:85` offers Matcha `goToReadingStats()` versus CrossPoint `goToUsbDrive()`.

Recommendation: keep both declarations and verify both implementations and menu routes. This is a same-location insertion, not a feature choice.

### 4. Lists and screens: framework migration under Matcha features

Evidence: `src/activities/network/NetworkModeSelectionActivity.h:22` mixes a newly merged `UiListActivity` base with the old `Activity` constructor and lifecycle declarations. Upstream `9e3cea2e` converts lists/tabs to FUI; `218dc6aa` adds the USB option. Similar overlaps occur in settings, library and reader menus.

Recommendation: use the current UI lifecycle/input model and port Matcha menu contents, per-book settings, covers, background scanning and statistics hooks into it. Do not keep an old constructor simply because its side contains Matcha code. Test touch and physical input, cancel/back behavior and deferred background work.

### 5. Reader architecture: largest conflict concentration

`src/activities/reader/EpubReaderActivity.cpp` has 45 conflict blocks, plus 8 in its header. CrossPoint `a757b9d1` consolidates reader activities, `b4cd1af6` adds the overlay reader menu, and `7bcc3a69` adds touch link navigation. Matcha has vertical rendering, per-book options, dictionary/translation and reading statistics in the same lifecycle and input paths.

Recommendation: reconcile `ReaderActivity` and shared interfaces before concrete readers. Preserve Matcha layout and Japanese feature behavior while integrating current overlay/input precedence. Then adapt EPUB, TXT and XTC implementations consistently. Inspect Matcha-only manga/translation activities even though they do not appear in the conflict list: shared base/API changes may break them.

### 6. EPUB representation and cache formats

Evidence: `lib/Epub/Epub/Page.h:129` conflicts between Matcha's ruby-aware render signature and upstream link support. `CssParser.h:57` has competing cache-format evolution. `ParsedText.cpp` has 17 conflict blocks; `CssParser.cpp` has 15; `ChapterHtmlSlimParser.cpp` has 9. Upstream changes include table columns (`da3d50c2`), touch links (`7bcc3a69`), ruby-group fixes (`c6fe67d4`) and RTL inheritance (`ce6c6cbc`).

Recommendation: merge the data structures and serialization contracts first, then the parser, layout and rendering consumers. Keep Matcha ruby/vertical/CSS support and upstream fixes. Assign integrated cache versions that cannot mistakenly accept incompatible parent layouts; choosing the numerically larger version alone is insufficient. Validate Japanese vertical/ruby and ordinary EPUB, tables, links and stale-cache regeneration.

### 7. Rendering: preserve distinct capabilities

Evidence: `lib/GfxRenderer/GfxRenderer.h:68` juxtaposes Matcha panel-residue tracking with upstream one-shot refresh promotion; `:311` juxtaposes manga upscaling arguments with dark-mode image polarity preservation. These are distinct requirements, not mutually exclusive alternatives.

Recommendation: retain both capabilities, reconcile the implementations and call sites, and evaluate residue bookkeeping using the refresh mode actually sent after promotion. Preserve clipping/overflow fixes (`d3b3b566`) and latest X3 antialiasing fixes (`1f3d7458`). Validate manga zoom, image polarity, overlay close and sleep-screen ghosting on the relevant devices.

## Resolution batches and validation

1. Resolve repository instructions, build workflows and ignore rules. Upstream moved the development guide from `.skills/SKILL.md` to `AGENTS.md`; preserve Matcha-specific guidance in the resulting location. No instructions should be dropped merely to remove the rename/content conflict.
2. Audit the automatically merged platform, SDK/HAL and update/recovery integration; resolve startup and settings semantics. Initialize the pinned SDK when beginning implementation.
3. Reconcile rendering/data contracts and versioning, then EPUB/parser/font implementations and host-test definitions.
4. Integrate shared reader/UI framework changes, then Matcha library, settings, Japanese/dictionary and manga consumers.
5. Reconcile translation YAML keys and documentation, keeping Matcha identity and features; review release asset naming and repository destinations.
6. After all conflicts are resolved, compile X4 Pro and address actual errors, then build default/Sticky and run applicable host tests. Review the final diff against both parents. Device tests and recovery audit follow separately.

Many conflicts are neighboring additions, but UI/reader restructuring and cache/settings compatibility are substantive. A full merge is feasible as an integration project; it is not a quick hardware-only merge. No per-hunk correctness claim is made yet: the inventory is exhaustive, while the rationale analysis above covers representative and high-risk conflicts to guide the next pass.

## Complete initial conflict inventory

Counts below are literal conflict blocks, not independent bugs or effort estimates.

### Build, documentation and shared utilities
11 files; 23 conflict blocks.
| File | Blocks | Initial conflict lines |
|---|---:|---|
| `.github/workflows/ci.yml` | 4 | 24, 63, 135, 189 |
| `.github/workflows/release-fonts.yml` | 1 | 27 |
| `.github/workflows/release_candidate.yml` | 2 | 29, 44 |
| `.gitignore` | 1 | 32 |
| `AGENTS.md` | 1 | 15 |
| `README.md` | 7 | 7, 92, 101, 198, 227, 260, 278 |
| `USER_GUIDE.md` | 2 | 539, 878 |
| `docs/file-formats.md` | 2 | 93, 109 |
| `docs/i18n.md` | 1 | 31 |
| `lib/XmlParserUtils/XmlParserUtils.h` | 1 | 5 |
| `test/CMakeLists.txt` | 1 | 52 |

### Fonts and rendering
5 files; 8 conflict blocks.
| File | Blocks | Initial conflict lines |
|---|---:|---|
| `lib/EpdFont/SdCardFont.cpp` | 1 | 204 |
| `lib/GfxRenderer/FontCacheManager.cpp` | 1 | 48 |
| `lib/GfxRenderer/FontCacheManager.h` | 1 | 18 |
| `lib/GfxRenderer/GfxRenderer.cpp` | 2 | 709, 1927 |
| `lib/GfxRenderer/GfxRenderer.h` | 3 | 68, 229, 311 |

### EPUB engine
17 files; 69 conflict blocks.
| File | Blocks | Initial conflict lines |
|---|---:|---|
| `lib/Epub/Epub.cpp` | 4 | 377, 430, 508, 1103 |
| `lib/Epub/Epub/Page.h` | 1 | 129 |
| `lib/Epub/Epub/ParsedText.cpp` | 17 | 409, 487, 564, 593, 614, 664, 684, 829, 930, 1085, 1198, 1298, 1381, 1456, 1523, 1790, 1811 |
| `lib/Epub/Epub/ParsedText.h` | 2 | 28, 119 |
| `lib/Epub/Epub/Section.cpp` | 2 | 138, 534 |
| `lib/Epub/Epub/blocks/ImageBlock.cpp` | 2 | 430, 442 |
| `lib/Epub/Epub/blocks/ImageBlock.h` | 1 | 31 |
| `lib/Epub/Epub/blocks/TextBlock.cpp` | 1 | 55 |
| `lib/Epub/Epub/blocks/TextBlock.h` | 1 | 89 |
| `lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp` | 1 | 6 |
| `lib/Epub/Epub/converters/ImageToFramebufferDecoder.h` | 1 | 79 |
| `lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp` | 2 | 58, 145 |
| `lib/Epub/Epub/converters/PngToFramebufferConverter.cpp` | 5 | 60, 259, 297, 353, 494 |
| `lib/Epub/Epub/css/CssParser.cpp` | 15 | 45, 173, 1190, 1281, 1363, 1484, 1510, 1660, 1730, 1752, 2220, 2292, 2317, 2331, 2400 |
| `lib/Epub/Epub/css/CssParser.h` | 3 | 57, 190, 277 |
| `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp` | 9 | 226, 433, 1222, 1617, 1857, 2118, 2141, 2622, 2677 |
| `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.h` | 2 | 168, 270 |

### Translations
5 files; 26 conflict blocks.
| File | Blocks | Initial conflict lines |
|---|---:|---|
| `lib/I18n/translations/arabic.yaml` | 1 | 458 |
| `lib/I18n/translations/english.yaml` | 2 | 208, 346 |
| `lib/I18n/translations/german.yaml` | 1 | 515 |
| `lib/I18n/translations/russian.yaml` | 2 | 383, 472 |
| `lib/I18n/translations/slovak.yaml` | 20 | 4, 17, 29, 60, 75, 99, 116, 129, 148, 169, 184, 200, 256, 268, 322, 338, 442, 454, 478, 527 |

### Startup, settings, activity and network coordination
9 files; 21 conflict blocks.
| File | Blocks | Initial conflict lines |
|---|---:|---|
| `src/CrossPointSettings.h` | 3 | 24, 144, 325 |
| `src/SettingsList.h` | 4 | 193, 236, 341, 364 |
| `src/SilentRestart.h` | 1 | 10 |
| `src/activities/ActivityManager.h` | 1 | 85 |
| `src/activities/boot_sleep/SleepActivity.cpp` | 5 | 32, 551, 621, 801, 817 |
| `src/activities/boot_sleep/SleepActivity.h` | 1 | 20 |
| `src/activities/network/CrossPointWebServerActivity.cpp` | 1 | 70 |
| `src/activities/network/NetworkModeSelectionActivity.h` | 1 | 22 |
| `src/main.cpp` | 4 | 143, 220, 471, 597 |

### Library, settings UI and themes
14 files; 48 conflict blocks.
| File | Blocks | Initial conflict lines |
|---|---:|---|
| `src/activities/home/FileBrowserActivity.cpp` | 5 | 73, 88, 366, 516, 537 |
| `src/activities/home/HomeActivity.cpp` | 1 | 476 |
| `src/activities/home/RecentBooksActivity.cpp` | 7 | 26, 103, 850, 870, 884, 1593, 1615 |
| `src/activities/home/RecentBooksActivity.h` | 3 | 7, 36, 57 |
| `src/activities/settings/SettingsActivity.cpp` | 7 | 67, 133, 218, 332, 355, 393, 739 |
| `src/activities/settings/SettingsActivity.h` | 2 | 170, 249 |
| `src/activities/settings/TextSettingsActivity.cpp` | 7 | 28, 72, 98, 118, 224, 353, 614 |
| `src/activities/settings/TextSettingsActivity.h` | 2 | 75, 117 |
| `src/components/themes/BaseTheme.cpp` | 3 | 3, 277, 553 |
| `src/components/themes/BaseTheme.h` | 3 | 137, 238, 257 |
| `src/components/themes/lyra/LyraTheme.cpp` | 4 | 28, 47, 149, 206 |
| `src/components/themes/lyra/LyraTheme.h` | 1 | 89 |
| `src/components/themes/roundedraff/RoundedRaffTheme.cpp` | 1 | 207 |
| `src/components/themes/roundedraff/RoundedRaffTheme.h` | 2 | 92, 103 |

### Reader and dictionary
14 files; 90 conflict blocks.
| File | Blocks | Initial conflict lines |
|---|---:|---|
| `src/activities/reader/DictionaryWordSelectActivity.cpp` | 1 | 256 |
| `src/activities/reader/EpubReaderActivity.cpp` | 45 | 60, 189, 313, 383, 407, 578, 642, 666, 681, 917, 958, 997, 1027, 1324, 1357, 1399, 1429, 1448, 1466, 1595, 1610, 1624, 1665, 1692, 1720, 1742, 1914, 1937, 2459, 2741, 2756, 2782, 2980, 3264, 3277, 3308, 3354, 3490, 3519, 3895, 3955, 4604, 4777, 4850, 4902 |
| `src/activities/reader/EpubReaderActivity.h` | 8 | 10, 141, 182, 202, 252, 336, 358, 494 |
| `src/activities/reader/EpubReaderFootnotesActivity.cpp` | 4 | 8, 18, 45, 93 |
| `src/activities/reader/EpubReaderFootnotesActivity.h` | 3 | 5, 13, 56 |
| `src/activities/reader/EpubReaderMenuActivity.cpp` | 5 | 14, 44, 101, 157, 328 |
| `src/activities/reader/EpubReaderMenuActivity.h` | 3 | 17, 42, 87 |
| `src/activities/reader/ReaderActivity.cpp` | 5 | 6, 18, 49, 78, 226 |
| `src/activities/reader/ReaderActivity.h` | 1 | 11 |
| `src/activities/reader/TxtReaderActivity.cpp` | 2 | 27, 46 |
| `src/activities/reader/TxtReaderActivity.h` | 1 | 17 |
| `src/activities/reader/XtcReaderActivity.cpp` | 7 | 14, 31, 56, 116, 444, 458, 475 |
| `src/activities/reader/XtcReaderActivity.h` | 3 | 7, 23, 65 |
| `src/util/Dictionary.cpp` | 2 | 67, 403 |
