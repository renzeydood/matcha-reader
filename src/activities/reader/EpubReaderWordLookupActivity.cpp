#include "EpubReaderWordLookupActivity.h"

#include <Arduino.h>
#include <DictIndex.h>
#include <Epub/RubyGlossary.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <SdCardFontSystem.h>
#include <WordLookup.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "DefinitionTextRenderer.h"
#include "DictionaryDefinitionActivity.h"
#include "Epub/Page.h"
#include "Epub/blocks/VerticalTextBlock.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"

EpubReaderWordLookupActivity::EpubReaderWordLookupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           const VerticalPage& page, std::string scanCachePath,
                                                           const uint16_t spineIndex, const uint16_t pageIndex,
                                                           int readerFontId)
    : Activity("WordLookup", renderer, mappedInput),
      vpage(&page),
      scanCachePath(std::move(scanCachePath)),
      scanSpine(spineIndex),
      scanPage(pageIndex) {
  const size_t slash = this->scanCachePath.find_last_of('/');
  if (slash != std::string::npos) bookCachePath = this->scanCachePath.substr(0, slash);
  fontId = (readerFontId != 0) ? readerFontId : SETTINGS.getReaderFontId();
  reclaimFontHeap();  // BEFORE building the scan -- see reclaimFontHeap()
  scan.initFromVerticalPage(page);
  initScanFromCacheOrBurst("vertical");
}

EpubReaderWordLookupActivity::EpubReaderWordLookupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           const Page& page, std::string scanCachePath,
                                                           const uint16_t spineIndex, const uint16_t pageIndex,
                                                           int readerFontId)
    : Activity("WordLookup", renderer, mappedInput),
      scanCachePath(std::move(scanCachePath)),
      scanSpine(spineIndex),
      scanPage(pageIndex) {
  const size_t slash = this->scanCachePath.find_last_of('/');
  if (slash != std::string::npos) bookCachePath = this->scanCachePath.substr(0, slash);
  fontId = (readerFontId != 0) ? readerFontId : SETTINGS.getReaderFontId();
  hpage = std::make_shared<Page>(page);
  reclaimFontHeap();  // BEFORE building the scan -- see reclaimFontHeap()
  scan.initFromPage(page);
  initScanFromCacheOrBurst("horizontal");
}

// Self-heal fragmentation BEFORE the scan builds its glyph vectors. Two reasons this must run
// first, not after initFrom*():
//   1) Building allGlyphs on a fragmented heap truncates it (pushGlyphSafe can't grow), so the
//      scan finds too few/zero selectable words. Coalescing first gives it room to complete.
//   2) Device telemetry showed maxAlloc degrading monotonically across open/close cycles
//      (28.7K -> 22.5K -> 19.4K ...) while total free fully recovered: font hot-group/slab
//      buffers regrown while RENDERING definitions persist past onExit and split the large
//      block the dict caches vacate. Left unchecked this ends in an allocation abort() a few
//      pages later (confirmed crash_report).
// Threshold is 40K (not the historical 28K): on the X3 (wider 528px viewport) the reader's
// resident font slab is larger, so the dict caches can fail to find contiguous space even above
// the old floor, surfacing as an empty scan ("no matches found"). Matches EpubReaderActivity's
// RESUME_HEAP_FLOOR so the tight X3-resume path (huge CSS book, maxAlloc bottoming near 7K)
// reliably reclaims before the scan runs. Fonts reload lazily; the reader re-warms on return.
void EpubReaderWordLookupActivity::reclaimFontHeap() {
  if (ESP.getMaxAllocHeap() < 40 * 1024) {
    LOG_INF("WLA", "Low contiguous heap (maxAlloc=%u); releasing font caches", ESP.getMaxAllocHeap());
    if (auto* fcm = renderer.getFontCacheManager()) {
      // This runs on the main task; the render task may be mid-render with glyph
      // pointers into the font cache (it holds the render lock for the whole
      // render()). Freeing under the lock waits that render out -- releasing
      // without it is a cross-task use-after-free (confirmed crash_report:
      // renderCharImpl faulted while this path freed the cache).
      RenderLock lock;
      fcm->releaseAllFontMemory();
      LOG_INF("WLA", "After font release: maxAlloc=%u", ESP.getMaxAllocHeap());
    }
  }
}

// A persisted scan for this exact page skips all scanning; otherwise start progressively.
void EpubReaderWordLookupActivity::initScanFromCacheOrBurst(const char* label) {
  if (!scanCachePath.empty() && scan.tryLoadCache(scanCachePath, scanSpine, scanPage)) {
    return;
  }
  runInitialBurst(label);
}

// Progressive open: scan only far enough to find the FIRST selectable word so the panel can show
// a definition within a few hundred ms. The rest of the page is mapped incrementally from loop()
// while the user reads (see there); moveCursor() scans further on demand if the user outruns it.
// The cap bounds the open even on a pathological page with no early match.
void EpubReaderWordLookupActivity::runInitialBurst(const char* label) {
  const uint32_t scanStart = millis();
  LOG_INF("WLA", "progressive scan (%s): %u characters", label, static_cast<unsigned>(scan.allGlyphs.size()));
  while (!scan.isDone() && scan.selectableGlyphs.empty() && millis() - scanStart < 1500) {
    stepScan(50);
  }
  LOG_INF("WLA", "progressive scan (%s): first word after %u ms", label, millis() - scanStart);
}

// See the header: heal a low-heap-truncated scan once by freeing fonts and re-walking the intact
// glyph list. cursorIndex is intentionally left alone -- the rebuilt selectable list only grows,
// and every caller already guards against an out-of-range cursor while it refills, so the user's
// position resumes naturally once the rescan passes it again.
bool EpubReaderWordLookupActivity::stepScan(uint32_t budgetMs) {
  // Definition rendering leaves compressed-font groups resident. Reclaim before the next scan
  // slice needs to grow a vector; waiting until that growth fails discards progress and rescans
  // the whole page. This is the same recovery used below, just before damage instead of after it.
  if (ESP.getMaxAllocHeap() < 20 * 1024) {
    RenderLock lock;
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->releaseAllFontMemory();
      LOG_INF("WLA", "Reclaimed fonts before scan: maxAlloc=%u", ESP.getMaxAllocHeap());
    }
  }
  const bool done = scan.step(budgetMs);
  if (scan.wasTruncated() && !scanHealAttempted && !scan.allGlyphs.empty()) {
    scanHealAttempted = true;
    LOG_INF("WLA", "Scan truncated by low heap; releasing fonts and rescanning (maxAlloc=%u)", ESP.getMaxAllocHeap());
    // Heal under the render lock: this runs on the main task (loop()), and the
    // render task may be mid-render, drawing definition text from font-cache
    // glyphs and reading scan.selectableGlyphs. Freeing the cache / resetting the
    // scan without the lock is a cross-task use-after-free (confirmed
    // crash_report: renderCharImpl faulted at this exact moment).
    RenderLock lock;
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->releaseAllFontMemory();
      LOG_INF("WLA", "After font release: maxAlloc=%u", ESP.getMaxAllocHeap());
    }
    scan.restartStepScan();
    return false;  // not done -- caller keeps stepping over the freshly-reset scan
  }
  return done;
}

void EpubReaderWordLookupActivity::onEnter() {
  Activity::onEnter();
  // Heap telemetry for the word-lookup OOM crash hunt (crash_report showed abort() on a tiny
  // string allocation inside performLookupImpl -- heap exhausted, cause unknown). Logged at
  // enter AND exit so a leak per open/close cycle shows as a declining series.
  LOG_INF("WLA", "onEnter heap: free=%u maxAlloc=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  // A scan-cache hit remembers the position the user was last at on this exact page -- resume
  // there instead of making them click back through every entry they've already seen.
  if (scan.restoredCursorIndex != WordSelectionScan::kNoRestoredCursor &&
      scan.restoredCursorIndex < scan.selectableGlyphs.size()) {
    cursorIndex = scan.restoredCursorIndex;
    performLookup();
    requestUpdate();
    return;
  }
  // Find first position with a match
  const int maxIdx = static_cast<int>(scan.selectableGlyphs.size()) - 1;
  for (cursorIndex = 0; cursorIndex <= maxIdx; cursorIndex++) {
    performLookup();
    if (hasResult) break;
  }
  if (cursorIndex > maxIdx) cursorIndex = 0;
  requestUpdate();
}

void EpubReaderWordLookupActivity::onExit() {
  LOG_INF("WLA", "onExit heap: free=%u maxAlloc=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  // Persist the current cursor position (a no-op if the scan never finished, or the cache path
  // is unset) so the next open of this exact page resumes here instead of at word one.
  if (!scanCachePath.empty()) {
    scan.saveCache(scanCachePath, scanSpine, scanPage, static_cast<uint16_t>(cursorIndex));
  }
  // Return the dictionary cache memory (~30KB) to the pool -- the reader needs it for heavy
  // operations like re-pagination (zip inflate wants one contiguous 32KB block).
  DictIndex::releaseCaches();
  Activity::onExit();
}

void EpubReaderWordLookupActivity::moveCursor(int delta) {
  // Moving past the last already-discovered word while the background scan is still running:
  // scan forward just enough to reveal the next one (typically a few hundred ms), so early
  // rapid navigation works instead of clamping at a stale end.
  if (delta > 0 && !scan.isDone() && cursorIndex + delta >= static_cast<int>(scan.selectableGlyphs.size())) {
    const size_t want = static_cast<size_t>(cursorIndex + delta) + 1;
    while (!scan.isDone() && scan.selectableGlyphs.size() < want) {
      stepScan(50);
    }
  }
  if (scan.selectableGlyphs.empty()) return;
  const int maxIdx = static_cast<int>(scan.selectableGlyphs.size()) - 1;
  // scan.selectableGlyphs is already the pre-filtered list of positions buildSelectableGlyphs()
  // confirmed have a dictionary match -- every index in it is valid by construction, so this just
  // moves one step and shows whatever's there. The previous version re-validated via
  // performLookup() and kept advancing past any position where that didn't independently agree
  // with the scan, silently skipping entries end-users could never reach -- confirmed on a real
  // device as "every second entry is skipped" during navigation (1, 3, 5, 7, ...).
  int newIndex = cursorIndex + delta;
  if (scan.isDone()) {
    // The full page is mapped, so "the end" is real -- cycle past it instead of dead-ending,
    // matching how e-reader dictionaries commonly let you loop through a page's word list.
    if (newIndex < 0)
      newIndex = maxIdx;
    else if (newIndex > maxIdx)
      newIndex = 0;
  } else {
    // Background scan still running: "the end" isn't final yet, so clamp instead of cycling --
    // wrapping to word one here would be surprising and skip words not yet discovered.
    if (newIndex < 0) newIndex = 0;
    if (newIndex > maxIdx) newIndex = maxIdx;
  }
  cursorIndex = newIndex;
  performLookup();
}

std::string EpubReaderWordLookupActivity::buildLookupText(size_t startIdx) const {
  std::string text;
  if (startIdx >= scan.selectableGlyphs.size() || startIdx >= scan.selectToAllIdx.size()) return text;

  const size_t allStart = scan.selectToAllIdx[startIdx];
  const uint32_t paraIdx = scan.allGlyphs[allStart].paragraphIndex;
  int charCount = 0;

  for (size_t i = allStart; i < scan.allGlyphs.size() && charCount < WordSelectionScan::kMaxLookupChars; i++) {
    const auto& g = scan.allGlyphs[i];
    if (g.paragraphIndex != paraIdx) break;
    WordSelectionScan::encodeUtf8(g.codepoint, text);
    charCount++;
  }
  return text;
}

void EpubReaderWordLookupActivity::prependBookReading(const std::string& surface) {
  if (bookCachePath.empty() || surface.empty()) return;
  std::string readings;
  if (!RubyGlossary::lookup(bookCachePath, surface, readings)) return;
  std::string line = tr(STR_IN_THIS_BOOK);
  line += ' ';
  line += readings;
  // Blank line: DefinitionText::drawWrapped renders an empty line as a half-line gap,
  // visually separating the book reading from the dictionary entry below it.
  line += "\n\n";
  resultDefinition = line + resultDefinition;
}

void EpubReaderWordLookupActivity::performLookup() {
  // Hold the rendering mutex while the result strings are rebuilt: the render task wraps and
  // draws resultDefinition/resultHeadword CONCURRENTLY on its own task, and mutating them
  // mid-render tears the string under the renderer -- confirmed crash_report: out_of_range
  // abort inside DefinitionText::drawWrapped when navigation triggered a lookup during a slow
  // (multi-second) e-ink refresh. The lock briefly delays one render; requestUpdate() then
  // redraws with the fresh result.
  RenderLock lock;
  // Mid-session self-heal: the open-time check can't help when the heap degrades DURING a long
  // navigation session (font glyphs loaded per rendered definition accumulate; a crash_report
  // showed the definition read inside DictIndex aborting after renders had slowed from 1.2s to
  // 6.3s as the heap ran down). Same release as at open; fonts reload lazily.
  if (ESP.getMaxAllocHeap() < 20 * 1024) {
    LOG_INF("WLA", "Low heap mid-session (maxAlloc=%u); releasing font caches", ESP.getMaxAllocHeap());
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->releaseAllFontMemory();
    }
  }
  // Signals render() to show "Loading..." instead of "No match found" while the lookup below
  // runs -- fast navigation otherwise briefly flashes the no-match text in the window between
  // clearing the previous result and the next lookup (~100-300ms) completing.
  lookupInFlight = true;
  performLookupImpl();
  lookupInFlight = false;
}

void EpubReaderWordLookupActivity::performLookupImpl() {
  hasResult = false;
  resultHeadword.clear();
  resultDefinition.clear();
  resultMatchLen = 0;
  scrollOffset = 0;
  totalLines = 9999;

  std::string text = buildLookupText(static_cast<size_t>(cursorIndex));
  if (text.empty()) return;

  // If the text starts with digits (2年, １５人), look up the counter/word that
  // follows and show the digits as a prefix so the reading is clear (2年).
  std::string digitPrefix;
  {
    size_t b = 0;
    while (b < text.size()) {
      auto c = static_cast<unsigned char>(text[b]);
      if (c >= '0' && c <= '9') {
        digitPrefix.push_back(static_cast<char>(c));
        b += 1;
      } else if (c == 0xEF && b + 2 < text.size() && static_cast<unsigned char>(text[b + 1]) == 0xBC &&
                 static_cast<unsigned char>(text[b + 2]) >= 0x90 && static_cast<unsigned char>(text[b + 2]) <= 0x99) {
        // Fullwidth digit ０-９ (U+FF10–U+FF19)
        digitPrefix.append(text, b, 3);
        b += 3;
      } else {
        break;
      }
    }
    if (b > 0 && b < text.size()) {
      text = text.substr(b);  // look up the part after the digits
    } else {
      digitPrefix.clear();  // nothing after digits, or no digits
    }
  }

  // Fictional katakana name + honorific (ヘムレンさん): if the dictionary only covers a prefix of
  // the name (ヘム) or nothing, show the whole katakana run instead. It has no dictionary entry,
  // so the definition body stays empty -- but it reads as one name rather than "heme". Dictionary
  // names (スナフキン) cover the whole run, so this branch doesn't fire for them.
  const size_t nameRun = WordSelectionScan::katakanaNameRunBeforeHonorific(text);
  if (nameRun >= 2) {
    WordLookupResult nr;
    int nrChars = 0;
    if (WordLookup::lookup(text, 0, nr)) {
      size_t pos = 0;
      while (pos < nr.matchLength && pos < text.size()) {
        auto c = static_cast<unsigned char>(text[pos]);
        if (c < 0x80)
          pos += 1;
        else if ((c & 0xE0) == 0xC0)
          pos += 2;
        else if ((c & 0xF0) == 0xE0)
          pos += 3;
        else
          pos += 4;
        nrChars++;
      }
    }
    if (static_cast<int>(nameRun) > nrChars) {
      size_t nb = 0;
      int nc = 0;
      while (nb < text.size() && nc < static_cast<int>(nameRun)) {
        auto c = static_cast<unsigned char>(text[nb]);
        if (c < 0x80)
          nb += 1;
        else if ((c & 0xE0) == 0xC0)
          nb += 2;
        else if ((c & 0xF0) == 0xE0)
          nb += 3;
        else
          nb += 4;
        nc++;
      }
      hasResult = true;
      resultHeadword = digitPrefix + text.substr(0, nb);
      resultDefinition = tr(STR_LOOKUP_NAME);  // no dictionary entry -- label it as a name
      resultMatchLen = static_cast<int>(nameRun);
      // Names are the glossary's prime case: the book's own furigana is often the ONLY
      // source for a name's reading.
      prependBookReading(text.substr(0, nb));
      requestUpdate();  // this early return would otherwise skip the requestUpdate() at the end,
                        // leaving the name un-rendered (screen keeps the previous word -> looks skipped)
      return;
    }
  }

  WordLookupResult result;
  if (WordLookup::lookup(text, 0, result)) {
    WordSelectionScan::stripTrailingParticle(text, result);
    hasResult = true;
    resultHeadword = digitPrefix + result.entry.headword;
    resultDefinition = std::move(result.entry.definition);
    prependBookReading(text.substr(0, std::min(result.matchLength, text.size())));
    int chars = 0;
    size_t pos = 0;
    while (pos < result.matchLength && pos < text.size()) {
      auto c = static_cast<unsigned char>(text[pos]);
      if (c < 0x80)
        pos += 1;
      else if ((c & 0xE0) == 0xC0)
        pos += 2;
      else if ((c & 0xF0) == 0xE0)
        pos += 3;
      else
        pos += 4;
      chars++;
    }
    resultMatchLen = chars;

    // For short hiragana-only matches (≤3 chars), check if the grammar dict
    // has a better entry and promote it to the main result. Functional words
    // like こと, もの, よう get unhelpful JMdict hits ("ancient capital").
    if (chars <= 3 && Storage.exists(DictIndex::grammarIdxPath())) {
      bool allHiragana = true;
      for (size_t b = 0; b < result.matchLength && b < text.size();) {
        auto c = static_cast<unsigned char>(text[b]);
        uint32_t cp = 0;
        if (c < 0x80) {
          cp = c;
          b += 1;
        } else if ((c & 0xE0) == 0xC0) {
          cp = ((c & 0x1F) << 6) | (text[b + 1] & 0x3F);
          b += 2;
        } else if ((c & 0xF0) == 0xE0) {
          cp = ((c & 0x0F) << 12) | ((text[b + 1] & 0x3F) << 6) | (text[b + 2] & 0x3F);
          b += 3;
        } else {
          b += 4;
        }
        if (cp < 0x3040 || cp > 0x309F) {
          allHiragana = false;
          break;
        }
      }
      if (allHiragana) {
        DictEntry gramEntry;
        if (DictIndex::lookupInFile(resultHeadword.c_str(), DictIndex::grammarIdxPath(), DictIndex::grammarDatPath(),
                                    gramEntry)) {
          resultDefinition = std::move(gramEntry.definition);
        }
      }
    }
  }

  // Grammar scan: search for grammar patterns in a window around the cursor.
  // Try starting from a few characters BEFORE the cursor (to catch patterns
  // like ことになる when cursor is on こと) and also from the cursor itself.
  hasGrammar = false;
  grammarHeadword.clear();
  grammarDefinition.clear();
  // The grammar overlay is a nicety on top of the main result. Its lookups build several
  // transient strings and read whole grammar entries; under a near-exhausted heap those
  // allocations abort() (-fno-exceptions) -- confirmed by a real device crash_report with a
  // ~30-byte string allocation failing in this block. Show the plain result instead of crashing.
  if (ESP.getMaxAllocHeap() < 16 * 1024) {
    LOG_ERR("WLA", "Skipping grammar scan, heap too low (maxAlloc=%u)", ESP.getMaxAllocHeap());
  } else if (Storage.exists(DictIndex::grammarIdxPath())) {
    const size_t allStart = scan.selectToAllIdx[static_cast<size_t>(cursorIndex)];
    const uint32_t paraIdx = scan.allGlyphs[allStart].paragraphIndex;

    // Try starting positions: cursor-3, cursor-2, cursor-1, cursor
    int bestGramLen = 0;
    for (int backoff = 3; backoff >= 0; backoff--) {
      size_t scanStart = allStart;
      for (int b = 0; b < backoff && scanStart > 0; b++) {
        scanStart--;
        if (scan.allGlyphs[scanStart].paragraphIndex != paraIdx) {
          scanStart++;
          break;
        }
      }

      std::string gramText;
      int gCharCount = 0;
      for (size_t j = scanStart; j < scan.allGlyphs.size() && gCharCount < 12; j++) {
        if (scan.allGlyphs[j].paragraphIndex != paraIdx) break;
        WordSelectionScan::encodeUtf8(scan.allGlyphs[j].codepoint, gramText);
        gCharCount++;
      }

      for (int wLen = std::min(gCharCount, 10); wLen >= 2; wLen--) {
        size_t byteEnd = 0;
        int cnt = 0;
        for (size_t b = 0; b < gramText.size() && cnt < wLen; cnt++) {
          auto c = static_cast<unsigned char>(gramText[b]);
          if (c < 0x80)
            b += 1;
          else if ((c & 0xE0) == 0xC0)
            b += 2;
          else if ((c & 0xF0) == 0xE0)
            b += 3;
          else
            b += 4;
          byteEnd = b;
        }
        std::string window = gramText.substr(0, byteEnd);
        DictEntry gramEntry;
        if (DictIndex::lookupInFile(window.c_str(), DictIndex::grammarIdxPath(), DictIndex::grammarDatPath(),
                                    gramEntry)) {
          if (gramEntry.headword != resultHeadword && wLen > bestGramLen) {
            bestGramLen = wLen;
            hasGrammar = true;
            grammarHeadword = std::move(gramEntry.headword);
            grammarDefinition = std::move(gramEntry.definition);
          }
          break;
        }
      }
    }
  }

  // Merge grammar into the definition so the single scroll-aware render loop
  // handles it (gets maxDefY clamping and scroll offset for free).
  if (hasGrammar) {
    // Built with one guarded reserve + appends: the old `a + b + c` temporary chain peaked at
    // roughly twice the combined definition size in contiguous heap -- an abort() risk exactly
    // when definitions are long. If even the reserve doesn't fit, keep the main result alone.
    const size_t mergedLen = resultDefinition.size() + grammarHeadword.size() + grammarDefinition.size() + 32;
    if (ESP.getMaxAllocHeap() > mergedLen + 8 * 1024) {
      resultDefinition.reserve(mergedLen);
      resultDefinition += "\n\n— Grammar: ";
      resultDefinition += grammarHeadword;
      resultDefinition += " —\n";
      resultDefinition += grammarDefinition;
    } else {
      LOG_ERR("WLA", "Skipping grammar merge, heap too low (maxAlloc=%u)", ESP.getMaxAllocHeap());
    }
  }

  requestUpdate();
}

int EpubReaderWordLookupActivity::findSelectableWordAt(int tx, int ty) const {
  if (scan.selectableGlyphs.empty()) return -1;
  int bestIdx = -1;
  uint32_t minDistSq = UINT32_MAX;

  for (size_t i = 0; i < scan.selectableGlyphs.size(); i++) {
    WordRect r = getWordBoundingBox(i);
    if (!r.valid) continue;

    if (tx >= r.x - 8 && tx <= r.x + r.w + 8 && ty >= r.y - 8 && ty <= r.y + r.h + 8) {
      int cx = r.x + r.w / 2;
      int cy = r.y + r.h / 2;
      int dx = cx - tx;
      int dy = cy - ty;
      uint32_t distSq = static_cast<uint32_t>(dx * dx + dy * dy);
      if (distSq < minDistSq) {
        minDistSq = distSq;
        bestIdx = static_cast<int>(i);
      }
    }
  }
  return bestIdx;
}

void EpubReaderWordLookupActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasBackGesture() ||
      ReaderUtils::powerClickLeavesWordLookup(mappedInput)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  // Touch swipe handling
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    moveCursor(1);
    return;
  } else if (swipe == MappedInputManager::SwipeDir::Right) {
    moveCursor(-1);
    return;
  }

  // Touch tap handling
  int tx = 0, ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    auto& theme = UITheme::getInstance();
    Rect screen = theme.getScreenSafeArea(renderer, true, false);

    // Direct word hit-test on page text
    const int wordHit = findSelectableWordAt(tx, ty);
    if (wordHit >= 0) {
      if (wordHit == cursorIndex) {
        // Tapped active word -> open full definition view!
        if (hasResult) {
          startActivityForResult(
              std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, resultHeadword, resultDefinition),
              [this](const ActivityResult&) { requestUpdate(); });
        }
      } else {
        cursorIndex = wordHit;
        performLookup();
      }
      return;
    }

    // Side navigation
    if (tx < screen.x + screen.width * 0.3) {
      moveCursor(-1);
      return;
    } else if (tx > screen.x + screen.width * 0.7) {
      moveCursor(1);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (hasResult) {
      startActivityForResult(
          std::make_unique<DictionaryDefinitionActivity>(renderer, mappedInput, resultHeadword, resultDefinition),
          [this](const ActivityResult&) { requestUpdate(); });
    }
    return;
  }

  const bool sideButtonsForLookup = (SETTINGS.wordLookupSideButtons != 0 || mappedInput.hasTouch()) &&
                                    SETTINGS.sideButtonLayout != CrossPointSettings::SIDE_BUTTONS_DISABLED;
  buttonNavigator.onPressAndContinuous(
      {sideButtonsForLookup ? MappedInputManager::Button::PageForward : MappedInputManager::Button::Right,
       MappedInputManager::Button::NavNext},
      [this] { moveCursor(1); });
  buttonNavigator.onPressAndContinuous(
      {sideButtonsForLookup ? MappedInputManager::Button::PageBack : MappedInputManager::Button::Left,
       MappedInputManager::Button::NavPrevious},
      [this] { moveCursor(-1); });

  // Progressive background scan
  if (!scan.isDone() && !RenderLock::peek()) {
    const bool done = stepScan(40);
    if (!hasResult && !scan.selectableGlyphs.empty()) {
      performLookup();
      requestUpdate();
    }
    if (done) {
      DictIndex::logAndResetStats("progressive scan complete");
      requestUpdate();  // redraw the position counter with the final total
    }
  }
}

EpubReaderWordLookupActivity::WordRect EpubReaderWordLookupActivity::getWordBoundingBox(size_t selectIdx) const {
  WordRect rect;
  if (selectIdx >= scan.selectableGlyphs.size() || selectIdx >= scan.selectToAllIdx.size()) return rect;

  const size_t allStart = scan.selectToAllIdx[selectIdx];
  const size_t numChars =
      (static_cast<int>(selectIdx) == cursorIndex && resultMatchLen > 0) ? static_cast<size_t>(resultMatchLen) : 1;

  int marginTop = 0, marginLeft = 0, marginRight = 0, marginBottom = 0;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  marginTop += SETTINGS.screenMargin;
  marginLeft += SETTINGS.screenMargin;

  int minX = INT_MAX, minY = INT_MAX;
  int maxX = INT_MIN, maxY = INT_MIN;

  if (vpage) {
    const int cellPx = verticalCellPx(renderer, fontId);
    for (size_t c = 0; c < numChars && (allStart + c) < scan.allGlyphs.size(); c++) {
      const auto& glyph = scan.allGlyphs[allStart + c];
      if (glyph.x == 0 && glyph.y == 0) continue;
      int cellX = glyph.x + marginLeft;
      int cellY = glyph.y + marginTop;

      if (cellX < minX) minX = cellX;
      if (cellY < minY) minY = cellY;
      if (cellX + cellPx > maxX) maxX = cellX + cellPx;
      if (cellY + cellPx > maxY) maxY = cellY + cellPx;
    }
  } else if (hpage) {
    const int lineH = renderer.getLineHeight(fontId);
    for (size_t c = 0; c < numChars && (allStart + c) < scan.allGlyphs.size(); c++) {
      const auto& glyph = scan.allGlyphs[allStart + c];
      if (glyph.x == 0 && glyph.y == 0) continue;
      int cellX = glyph.x + marginLeft;
      int cellY = glyph.y + marginTop;

      std::string utf8Char;
      WordSelectionScan::encodeUtf8(glyph.codepoint, utf8Char);
      int charW = renderer.getTextAdvanceX(fontId, utf8Char.c_str(), EpdFontFamily::REGULAR);
      if (charW <= 0) charW = lineH;

      if (cellX < minX) minX = cellX;
      if (cellY < minY) minY = cellY;
      if (cellX + charW > maxX) maxX = cellX + charW;
      if (cellY + lineH > maxY) maxY = cellY + lineH;
    }
  }

  if (minX != INT_MAX && maxX > minX && maxY > minY) {
    rect.x = minX;
    rect.y = minY;
    rect.w = maxX - minX;
    rect.h = maxY - minY;
    rect.valid = true;
  }
  return rect;
}

void EpubReaderWordLookupActivity::drawWordHighlights() {
  if (scan.selectableGlyphs.empty()) return;

  // 1. Draw side-lines (bousen) or underlines for all detected words
  for (size_t i = 0; i < scan.selectableGlyphs.size(); i++) {
    if (static_cast<int>(i) == cursorIndex) continue;
    WordRect r = getWordBoundingBox(i);
    if (!r.valid) continue;

    if (vpage) {
      // Vertical text: Japanese side-line (傍線) on LEFT side of word column
      // (Furigana is on the right side of the column, so left side avoids overlap)
      renderer.fillRect(r.x - 3, r.y, 2, r.h, true);
    } else {
      // Horizontal text: underline under word line
      renderer.fillRect(r.x, r.y + r.h + 1, r.w, 2, true);
    }
  }

  // 2. Active word highlight box
  if (cursorIndex >= 0 && cursorIndex < static_cast<int>(scan.selectableGlyphs.size())) {
    WordRect r = getWordBoundingBox(static_cast<size_t>(cursorIndex));
    if (r.valid) {
      renderer.drawRect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, true);
      renderer.drawRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, true);
    }
  }
}

void EpubReaderWordLookupActivity::renderContentArea(const Rect& screen, int contentTop) {}

void EpubReaderWordLookupActivity::render(RenderLock&&) {
  auto& theme = UITheme::getInstance();
  Rect screen = theme.getScreenSafeArea(renderer, true, false);

  renderer.clearScreen();

  // 1. Draw page text background
  int marginTop = 0, marginLeft = 0, marginRight = 0, marginBottom = 0;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  marginTop += SETTINGS.screenMargin;
  marginLeft += SETTINGS.screenMargin;

  if (vpage) {
    VerticalTextBlock block(*vpage);
    block.render(renderer, fontId, fontId, marginLeft, marginTop, true);
  } else if (hpage) {
    hpage->render(renderer, fontId, marginLeft, marginTop, false);
  }

  // 2. Draw word highlight side-lines/underlines and active selection box
  drawWordHighlights();

  // 3. Draw clean bottom button hints with position counter
  std::string posText;
  if (!scan.selectableGlyphs.empty()) {
    posText = std::to_string(cursorIndex + 1) + "/" +
              (scan.isDone() ? std::to_string(scan.selectableGlyphs.size()) : std::string("\xe2\x80\xa6"));
  }

  const bool sideButtonsForLookup = (SETTINGS.wordLookupSideButtons != 0 || mappedInput.hasTouch()) &&
                                    SETTINGS.sideButtonLayout != CrossPointSettings::SIDE_BUTTONS_DISABLED;
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), sideButtonsForLookup ? tr(STR_DIR_UP) : tr(STR_DIR_LEFT),
                            sideButtonsForLookup ? tr(STR_DIR_DOWN) : tr(STR_DIR_RIGHT));

  std::string confirmLabel = labels.btn2;
  if (!posText.empty()) {
    confirmLabel += " [";
    confirmLabel += posText;
    confirmLabel += "]";
  }

  GUI.drawButtonHints(renderer, labels.btn1, confirmLabel.c_str(), labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseAllFontMemory();
}
