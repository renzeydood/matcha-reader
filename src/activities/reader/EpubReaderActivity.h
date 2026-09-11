#pragma once

#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/PageLink.h>
#include <Epub/Section.h>
#include <Epub/VerticalSection.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

#include "BookmarkEntry.h"
#include "ChapterPosition.h"
#include "EpubReaderMenuActivity.h"
#include "ProgressMapper.h"
#include "ReaderActivity.h"
#include "ReaderToolbarUi.h"
#include "components/OptionPopup.h"

class EpubReaderActivity final : public ReaderActivity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  std::unique_ptr<VerticalSection> verticalSection = nullptr;
  // Spine index whose vertical section last failed to build (e.g. transient low-heap allocation
  // failure). Prevents an immediate automatic retry loop: without this, verticalSection.reset()
  // on failure leaves `!verticalSection` true, so the very next render() call retries the same
  // expensive build (indexing an entire chapter) again -- observed on a real device as an
  // indefinite "Indexing" popup that silently re-failed every ~12 seconds with no visible error.
  int failedVerticalSpineIndex = -1;
  // Same guard as failedVerticalSpineIndex, for horizontal-mode Section builds. Without this,
  // section.reset() on a build failure leaves `!section` true, so the next render() retries the
  // same build immediately -- observed as an indefinite "Indexing" popup on a chapter whose
  // horizontal build fails for the same reason vertical builds can (a hard-to-satisfy contiguous
  // allocation, e.g. the zip inflate window, failing under a tight/fragmented heap).
  int failedSectionSpineIndex = -1;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  std::string pendingAnchor;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;

  // Snapshot of the layout-affecting settings the currently-resident section was
  // built with. The reader menu is pushed on top of this activity, so editing a
  // setting (e.g. screenMargin) never null-resets the section -- the new value
  // moves the draw origin but the cached line layout keeps the old width, so text
  // overflows one side until the book is reopened. render() compares this against
  // the current settings and reflows in place on a mismatch.
  struct LayoutSig {
    int fontId = -1;
    uint16_t viewportWidth = 0;
    uint16_t viewportHeight = 0;
    float lineCompression = 0.0f;
    uint8_t paragraphAlignment = 0;
    bool extraParagraphSpacing = false;
    bool hyphenationEnabled = false;
    bool embeddedStyle = false;
    uint8_t imageRendering = 0;
    bool focusReadingEnabled = false;
    bool bookCssMargins = false;
    // Vertical-only inputs. These key the vertical cache FILE, so they must be here too -- otherwise a
    // resident section built with the old values keeps being served and a mid-book line-spacing change
    // does not take effect until the book is reopened.
    uint8_t lineSpacing = 0;
    bool furigana = false;
    bool operator==(const LayoutSig& o) const {
      return fontId == o.fontId && viewportWidth == o.viewportWidth && viewportHeight == o.viewportHeight &&
             lineCompression == o.lineCompression && paragraphAlignment == o.paragraphAlignment &&
             extraParagraphSpacing == o.extraParagraphSpacing && hyphenationEnabled == o.hyphenationEnabled &&
             embeddedStyle == o.embeddedStyle && imageRendering == o.imageRendering &&
             focusReadingEnabled == o.focusReadingEnabled && bookCssMargins == o.bookCssMargins &&
             lineSpacing == o.lineSpacing && furigana == o.furigana;
    }
    bool operator!=(const LayoutSig& o) const { return !(*this == o); }
  };
  LayoutSig sectionLayoutSig;

  // Per-book reader preferences. The global settings page (opened from home)
  // holds the DEFAULTS: a book with no prefs file opens with them. Once a book
  // has been opened, its reading-relevant settings are pinned to the book
  // (readerprefs.bin in its cache dir) and reapplied on every open; settings
  // edited while reading affect only the book -- the global values captured in
  // globalPrefsSnapshot are restored (RAM and, if a mid-session save leaked
  // book values into the global file, re-saved) on exit. Vertical/furigana
  // overrides already live per-book in progress.bin and are untouched.
  struct ReaderPrefs {
    uint8_t fontFamily = 0;
    char sdFontFamilyName[32] = {};
    // Point size since the 1.5.0 merge (was a size-enum slot); see PREFS_VERSION.
    uint8_t fontPointSize = 0;
    uint8_t lineSpacing = 0;
    uint8_t screenMargin = 0;
    uint8_t bookCssMargins = 0;
    uint8_t paragraphAlignment = 0;
    uint8_t embeddedStyle = 0;
    uint8_t hyphenationEnabled = 0;
    uint8_t focusReadingEnabled = 0;
    uint8_t imageRendering = 0;
    uint8_t orientation = 0;
    bool operator==(const ReaderPrefs&) const = default;
  };
  ReaderPrefs globalPrefsSnapshot;
  static ReaderPrefs capturePrefsFromSettings();
  static void applyPrefsToSettings(const ReaderPrefs& prefs);
  bool loadBookPrefs(ReaderPrefs& out) const;
  void saveBookPrefs(const ReaderPrefs& prefs) const;
  std::optional<uint32_t> cachedVisibleTextOffset;
  std::optional<uint32_t> currentPageVisibleOffset;
  std::optional<uint32_t> pendingOffsetJump;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  int8_t pendingManualTurn = 0;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  bool pendingSyncSaveError = false;
  uint8_t pageLoadRetryCount = 0;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  bool skipNextButtonCheck = false;
  bool automaticPageTurnActive = false;
  bool showBookmarkMessage = false;
  bool showDictionaryMessage = false;
  unsigned long dictionaryMessageTime = 0UL;
  bool currentPageBookmarked = false;
  int idlePrewarmSpine = -1;
  int idlePrewarmPage = -1;
  unsigned long lastRenderCompleteMs = 0;
  bool bookmarkRemoved = false;
  std::vector<BookmarkEntry> cachedBookmarks;
  bool recentsEntryRemoved = false;
  // Per-book vertical text override: -1 = auto (detect from language), 0 = off, 1 = on
  int8_t verticalOverride = -1;
  // Per-book furigana override: -1 = auto (on by default), 0 = off, 1 = on
  int8_t furiganaOverride = -1;
  unsigned long bookmarkMessageTime = 0UL;
  unsigned long readingSessionStartMs = 0UL;
  bool ignoreNextConfirmRelease = false;
  // Set when the reader is left at end-of-book and SETTINGS.moveFinishedToReadFolder is on.
  // Consumed in onExit() to relocate the finished book into /Read/.
  bool pendingReadFolderMove = false;

  // Toolbar reader menu (SETTINGS.readerMenuStyle == READER_MENU_TOOLBAR): drawn
  // over the page instead of pushing the full-screen list menu. Select opens the
  // Toolbar; its tools open the Contents/Text/More bottom-sheet panels.
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::None;
  int focusedTool = 0;  // toolbar tool focus: 0=Contents, 1=Text, 2=More
  int panelIndex = 0;   // selected row within the active panel
  // Panel list navigation: a tap steps one row, a hold jumps PANEL_HOLD_STEP rows in one go
  // (a contents list runs to hundreds of chapters). One jump per hold, not a repeat -- every
  // step repaints the panel, so repeating is bounded by the e-ink refresh anyway and reads as
  // sluggish. True once a hold has jumped, so the release that ends it is swallowed.
  static constexpr unsigned long PANEL_HOLD_MS = 1500;
  static constexpr int PANEL_HOLD_STEP = 10;
  bool panelHoldJumped = false;
  // Whether the panel draws its cursor row. Button boards always do; touch
  // boards only once a button has moved it, so a tapped row is not left inverted.
  bool panelCursorShown = false;
  // FreeInkUI chrome + tap targets for the overlay; created when it opens,
  // released when it closes.
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  // Modal option picker over the panel (same component the Settings screens
  // use), for enum rows: font size / line spacing / alignment / orientation /
  // auto page turn. Toggle rows stay one-tap toggles, as in Settings.
  OptionPopup overlayPopup;
  // True while a clean-page snapshot (renderer.storeBwBuffer) backs the open
  // overlay, letting panel->toolbar steps restore the page without a full
  // re-render. Discarded on close / whenever the page under the overlay changes.
  bool overlayPageStored = false;
  int autoTurnOption = 0;  // current auto page-turn rate index (More panel)
  std::vector<EpubReaderMenuActivity::MenuItem> moreItems;

  // Footnote support
  std::vector<FootnoteEntry> currentPageFootnotes;
  // Chapter-wide footnote list from the section file's footnote table (v32+): the panel shows
  // ALL of the chapter's notes, opening at the one nearest the current page.
  std::vector<std::pair<uint16_t, FootnoteEntry>> sectionFootnotes;
  // Flattened entries handed to the footnote panel (must outlive the activity, which keeps a
  // reference); rebuilt on each open.
  std::vector<FootnoteEntry> footnotePanelEntries;
  std::vector<PageLink> currentPageLinks;
  int currentPageLinkMarginLeft = 0;
  int currentPageLinkMarginTop = 0;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;

  // --- Background image-cache warm (render-task tail) ---
  // After a page is fully displayed, the render task warms the NEXT page's image .pxc pixel
  // cache in place (ImageBlock::warmCache, cacheOnly decode) so landing on a full-page
  // illustration is a cache read instead of a multi-second decode. No extra task: the warm
  // runs at the tail of render(), still holding the RenderLock, and gets out of the way via
  // cooperative cancellation with two signals polled per decode block:
  //   1. imageWarmInputStamp_ -- bumped by the loop task on ANY button press (and in
  //      pageTurn() for tilt/auto turns) BEFORE any handler can push/pop an activity or take
  //      the RenderLock, so those blocking acquires only ever wait one decode block.
  //   2. the render task's own pending task-notification value -- a queued render (page turn
  //      already requested, requestUpdateAndWait from another task) cancels the warm even
  //      when no new button press is involved.
  std::atomic<uint32_t> imageWarmInputStamp_{0};
  uint32_t imageWarmStampSnapshot_ = 0;  // render task only: stamp value at warm start
  std::string imageWarmFailedPath_;      // render task only: give-up-once decode-failure target
  static constexpr uint32_t NO_IMAGE_REFINE = UINT32_MAX;
  std::atomic<uint32_t> pendingHorizontalImageRefine_{NO_IMAGE_REFINE};
  std::atomic<uint32_t> requestedHorizontalImageRefine_{NO_IMAGE_REFINE};
  void warmNextPageImageCache(uint16_t viewportWidth, uint16_t viewportHeight);
  static bool imageWarmShouldCancel(const void* ctx);
  // True when the next turn has already been requested: a button is physically down, or a render
  // is queued on this task. Call from the render task only.
  //
  // Gate SPECULATIVE tail work on this -- work whose only value is to make the NEXT render
  // faster. It runs on the render task, so it delays the render it is preparing for; when the
  // reader is paging rapidly the page it prepared is skipped past anyway.
  //
  // Do NOT gate work whose absence has a cost of its own. Two that must stay unconditional, both
  // learned the hard way: silentIndexNextChapterIfNeeded() (skipping defers a chapter build into
  // the reader's path, seconds with an Indexing popup) and saveProgress() (the record is restored
  // from by the progress-sync path, so a stale one snaps the reader backwards mid-skim).
  bool nextTurnAlreadyRequested() const;
  // Shared tail of both render paths: next-chapter index, neighbour-page glyph warm, progress
  // save. `vertical` selects the section type; the margins are used by the horizontal warm only.
  // Safe to call inside an async refresh window -- nothing here touches the framebuffer.
  void runPostRenderTail(uint16_t viewportWidth, uint16_t viewportHeight, bool vertical, int marginLeft, int marginTop);
  // Viewport of the last render(), captured so loop()'s lazy partial-extension start
  // builds with IDENTICAL layout parameters to the pages already rendered (a mismatch
  // would paginate differently than the partial being extended). 0 = no render yet.
  uint16_t buildViewportWidth = 0;
  uint16_t buildViewportHeight = 0;
  bool partialRebuildStartFailed = false;

  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft, bool glyphsAlreadyWarm = false,
                      bool grayscaleRefineOnly = false);
  // Horizontal analog of prewarmedVPage_: the page index whose glyphs currently sit warm in
  // the font cache from the idle next-page prewarm; -1 = cold/unknown. Written on the render
  // task only.
  int prewarmedHPage_ = -1;
  void renderStatusBar() const;
  // Bulk-loads a vertical page's glyphs into the SD-font mini cache (heap-gated). Returns
  // false when the heap was too tight to prewarm -- rendering still works via the slower
  // per-glyph on-demand path.
  bool prewarmVerticalPageGlyphs(const VerticalPage& vpage);
  // True only while a mid-build early render is running (the build is paused mid-layout and
  // resumes the moment it returns). Selects the conservative prewarm heap floor there, and the
  // reading floor everywhere else -- see prewarmVerticalPageGlyphs().
  bool duringEarlyBuildRender_ = false;
  // Draws one vertical TEXT page into the framebuffer; shared by the normal render path and
  // the early-first-render hook. Does not touch the display. glyphsAlreadyWarm skips the
  // prewarm when the page's glyphs were pre-loaded during idle (see prewarmedVPage_).
  void renderVerticalPageBody(const VerticalPage& vpage, bool glyphsAlreadyWarm = false);
  // Page index whose glyphs currently sit in the SD-font mini cache from the idle next-page
  // warm; -1 = cache cold/unknown. Kindle-class turns: the NEXT page's glyphs are loaded
  // while the reader looks at the current one, so a forward turn renders warm (~200ms)
  // instead of paying the ~500-700ms per-page SD bulk load at button time.
  int prewarmedVPage_ = -1;
  // The vertical page index actually drawn by the last render, captured BEFORE the draw.
  // verticalSection->currentPage can advance mid-render (a button press during a 100-700ms
  // render), so the post-render warm must not re-read it -- see the warm block in render().
  int renderedVPage_ = -1;
  // Evidence gate for the pre-render font release (see RESUME_HEAP_FLOOR in render()), which costs a
  // full font-cache rebuild on the next render -- kern classes, mini kern, advance table, glyph
  // groups, ~400ms. These track whether the PREVIOUS render ran short of glyph memory: a clean render
  // means the heap is adequate at this level, so do not release. Needed because vertical reading sits
  // at maxAlloc 12-32K, where a bare floor test is true almost always.
  uint32_t starvedGlyphsAtLastRender_ = 0;
  bool forceFontReleaseCheck_ = true;  // armed on entry: the resume path this was written for
  // Backoff for the silent next-chapter index when the heap is too tight to build clean:
  // without it the attempt re-fires every tick while the reader sits on a chapter's last two
  // pages, and each attempt releases the font caches (cold glyphs on the next turn) for
  // nothing -- observed on device firing at 1Hz+ with maxAlloc pinned at the same value.
  uint32_t silentIndexBackoffUntilMs_ = 0;
  // Page index the last vertical render actually drew. The idle warm evicts it (the mini font
  // cache holds exactly one page), so running that warm again after a re-render of the SAME
  // page -- status bar tick, closed menu, bookmark toast -- costs two bulk SD loads and buys
  // nothing. Device log: "idle warm page=20" twice around one re-render of page 19.
  int lastRenderedVPage_ = -1;
  // Direction of the most recent page turn; the idle warm follows it (forward turns warm
  // the next page, backward turns the previous one) so sustained paging in EITHER
  // direction hits a warm cache. Written by pageTurn() on the loop() task.
  std::atomic<bool> lastTurnForward_{true};
  // Early-first-render: invoked mid-build by VerticalSection the moment the reader's target
  // page is laid out (and again for every mid-build page-turn request), so text is on screen
  // seconds into a ~17s whole-chapter build and the user can keep turning pages while the
  // rest of the chapter builds.
  static void earlyRenderVerticalPageThunk(void* ctx, const VerticalPage& page, int pageIndex);
  // "Indexing" notice for a backward turn the running build cannot serve. Called between pages
  // on the render task, so it shares the framebuffer with the page render rather than racing it.
  static void buildNoticeThunk(void* ctx);
  void earlyRenderVerticalPage(const VerticalPage& page, int pageIndex);
  // True while a vertical chapter build runs on the render task. Read by pageTurn() on the
  // loop() task: while building, the section's pageCount is still 0, so the normal turn path
  // would misread every press as "past the last page" and jump to the next spine (observed:
  // a press during the build teleported the reader to the end of the book).
  std::atomic<bool> verticalBuildInProgress_{false};
  // Early target/currently shown page; seeded before the hook so build-time turns work.
  // Written on the render task, read by pageTurn() on the loop() task.
  std::atomic<int> earlyDisplayedPage_{-1};
  bool earlyPageActuallyDisplayed_ = false;
  void silentIndexNextChapterIfNeeded(uint16_t viewportWidth, uint16_t viewportHeight);
  bool saveProgress(int spineIndex, int currentPage, int pageCount, int8_t vertOverride, int8_t furiOverride);
  // Pages laid out per incremental-build pump: on the render path (catching up to the page
  // being shown) and per loop() tick (background build of a large chapter). Kept small so a
  // background build chunk never noticeably delays input or a pending render.
  static constexpr int BUILD_PAGES_PER_CHUNK = 8;
  static constexpr int BACKGROUND_BUILD_PAGES_PER_TICK = 2;
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 32 * 1024;
  static constexpr size_t BACKGROUND_BUILD_MIN_MAX_ALLOC = 16 * 1024;
  bool buildTickHeapGate();
  bool buildHeapPaused = false;
  static constexpr size_t RENDER_MIN_FREE_HEAP = 24 * 1024;
  static constexpr int BUILD_WINDOW_AHEAD = 5;
  // Reopening a partial does NOT immediately restart its extension build (a whole-chapter
  // re-layout from page 0 -- minutes of background CPU + SD writes on a giant spine, wasted
  // when the reader never crosses the watermark that session). Instead loop() starts it once
  // the reader is within this many pages of the watermark: at ~30s per page read and ~100-300ms
  // per page rebuilt, this margin gives the rebuild ample runway to catch up (and finalize)
  // before the reader arrives.
  static constexpr int PARTIAL_REBUILD_START_MARGIN = BUILD_WINDOW_AHEAD;
  // Show the indexing popup when an initial build must lay out more than this many pages up front
  // (a deep resume/jump into a not-yet-built section), so it isn't a silent wait. Kept independent
  // of the small look-ahead window so ordinary landings stay popup-free.
  static constexpr int BUILD_POPUP_PAGE_THRESHOLD = 20;
  static constexpr size_t BUILD_POPUP_BYTE_THRESHOLD = 96 * 1024;
  static constexpr unsigned long BUILD_POPUP_DEADLINE_MS = 1000;
  bool buildPopupPending = false;
  void showBuildPopup(GfxRenderer& renderer, int& pagesUntilFullRefresh);
  bool applyDeferredReposition();
  void clearDeferredReposition();
  void rememberCurrentContentOffset();
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  // Live section position, or the values cached before a child screen
  // released the section.
  ChapterPosition chapterPosition() const;
  int bookPercentFor(const ChapterPosition& position) const;
  void openReaderMenu();
  // Toolbar reader menu (see Overlay above).
  bool usesToolbarMenu() const;
  void openOverlay(Overlay target);
  void closeOverlayToPage();
  void discardOverlayPage();
  void handleOverlayInput();
  void renderOverlay();
  void renderOverlayAfterPage();
  std::string currentChapterTitle() const;
  // Text panel rows (font, size, line spacing, alignment, focus reading).
  std::string textRowName(int row) const;
  std::string textRowValue(int row) const;
  void showTextRowPopup(int row);
  // Persist + re-paginate + re-render under the open panel (live preview).
  void applyTextSettingLive();
  void paintOverlayPopup();
  // Persist the reader text settings, (re)load the selected SD font, and
  // re-paginate the current chapter so changes apply without re-opening the book.
  void applyReaderTextSettings();
  // More panel rows.
  void buildMoreActions();
  std::string moreRowName(int row) const;
  std::string moreRowValue(int row) const;
  void activateMoreRow(int row);
  void openDictionaryWordSelect();
  bool launchKOReaderSync();
  unsigned long confirmLongPressThreshold() const;
  void toggleAutoPageTurn(uint8_t selectedPageTurnOption);
  void loadCachedBookmarks();
  void addBookmark();
  void updateBookmarkFlag();

  void navigateToHref(const std::string& href, bool savePosition = false);
  void openFootnotesPanel();
  void openWordLookupPanel();
  static constexpr uint16_t kSpineProbeFailed = 0xFFFF;  // session marker: cache probe failed, don't retry
  // Page numbering across the logical ToC chapter: spine files without their own ToC entry
  // (inline illustration files etc.) inherit the previous entry's tocIndex, so the "page X/Y"
  // counter runs to the next REAL chapter instead of resetting at every spine-file boundary.
  // Sibling counts come from a cheap header-only cache peek; unbuilt siblings are estimated
  // from byte size. Cached per (spine, live page count, mode); mutable so the const render
  // path can refresh it.
  void updateChapterPageSpan(uint16_t viewportWidth, uint16_t viewportHeight) const;
  // Page-based book progress (pages read / total pages, like Apple Books) instead of the
  // byte-weighted estimate: furigana markup inflates ruby-dense chapters' byte share, so the
  // byte model lags several percent behind the rendered-page position on Japanese books.
  // Real counts come from section-cache headers; unindexed chapters are estimated from their
  // byte share of the already-indexed ones and refine as sections get built.
  // Page within the loaded section, 1-based, with that section's page count. Clamped, because a
  // section whose currentPage is briefly out of range must not reach the UI: a mode switch drops
  // both sections and render() repositions afterwards, and anything drawn in between was showing
  // the raw value.
  struct SectionPageSpan {
    int page;
    int count;
  };
  SectionPageSpan sectionPageSpan() const;
  int pageBasedPercent(int spineIndex, int sectionPage) const;  // sectionPage is 1-based
  mutable int chapterSpanSpine = -1;
  mutable int chapterSpanLivePages = -1;
  mutable bool chapterSpanVertical = false;
  // Part of the memo key: a span computed while a build held the card skipped the per-spine
  // probes, so it must be recomputed once the build releases it, even if nothing else changed.
  mutable bool chapterSpanBuildActive = false;
  mutable int chapterPagesBefore = 0;
  mutable int chapterPagesTotal = 0;
  mutable std::vector<uint16_t> spinePagesReal;       // 0 = not indexed yet
  mutable std::vector<uint16_t> spinePagesEffective;  // real or byte-estimated, never 0
  mutable int bookPagesBefore = 0;
  mutable int bookPagesTotal = 0;
  mutable uint16_t lastViewportWidth = 0;
  mutable uint16_t lastViewportHeight = 0;
  // The font the book is actually laid out and rendered in. Normally the user's selection;
  // when that font can't carry the book's PRIMARY script (built-in or Latin font with a
  // Japanese book, CJK-only font with a Latin book), the loaded companion font substitutes so
  // measurement and the vertical engine's font-adaptive positioning all derive from one font
  // that really contains the glyphs -- per-glyph fallback stays only for rare stragglers.
  int effectiveReaderFontId() const;
  // The horizontal layout spec for THIS book. SETTINGS.readerRenderSpec() knows only the
  // global store; two fields are per-book and have to be applied on top of it -- the
  // substituted font, and the furigana toggle's override. Both are cache-key fields, so a spec
  // built without them silently fails every parameter check and rebuilds the chapter. Build
  // every horizontal spec through here rather than repeating the fixups at each call site.
  ReaderRenderSpec readerSpec(uint16_t viewportWidth, uint16_t viewportHeight) const;
  void restoreSavedPosition();
  bool useVerticalText() const;
  // Space kept clear below the text, in addition to the panel's own bezel.
  //
  // Horizontal follows upstream: the status bar and the reader's margin describe the same strip,
  // so the larger of the two wins. Vertical ADDS them, because tategaki puts ink past its last
  // row by design -- the row's own ink hang, and 。/、 set past the line end (burasage). Measured
  // at 6px on a 29px cell, against a default margin that max() collapses to nothing whenever the
  // status bar is taller (~24px with a clock), which would drop that ink onto the clock.
  uint8_t readerBottomReserve(bool verticalMode) const;

  bool useFurigana() const;
  bool isJapaneseBook() const;
  bool showVerticalToggle() const;
  void applyVerticalFuriganaOverride(int8_t verticalOverrideIn, int8_t furiganaOverrideIn);

  void applyOrientation(uint8_t orientation);
  void applyInitialOrientation() override;
  // The orientation the current layout was built for. The control center's
  // orientation tile can move SETTINGS.orientation while this reader sits on
  // the activity stack, and Pop restores it without onEnter(), so the drift has
  // to be noticed here rather than assumed away.
  uint8_t appliedOrientation = 0;

  bool loadBook() override;
  std::string getBookTitle() const override { return epub ? epub->getTitle() : ""; }
  std::string getBookAuthor() const override { return epub ? epub->getAuthor() : ""; }
  std::string getBookThumbBmpPath() const override { return epub ? epub->getThumbBmpPath() : ""; }
  void renderBook() override;
  void onEndOfBookRendered() override;

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                              bool allowFastInitialRefresh)
      : ReaderActivity("EpubReader", renderer, mappedInput, std::move(bookPath), allowFastInitialRefresh) {}
  ~EpubReaderActivity() override;

  void onExit() override;
  void loop() override;

  bool pageTurn(bool isForward) override;
  bool skipPages(int amount) override;
  bool isAtEndOfBook() const override;
  void onReturnFromEndOfBook() override;

  bool skipLoopDelay() override;

  ScreenshotInfo getScreenshotInfo() const override;
  CrossPointPosition getCurrentPosition() const;
};
