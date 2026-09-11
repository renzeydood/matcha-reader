#pragma once

#include <HalStorage.h>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "CssSelector.h"
#include "CssStyle.h"

/**
 * Lightweight CSS parser for EPUB stylesheets
 *
 * Parses CSS files and extracts styling information relevant for e-ink display.
 * Uses a two-phase approach: first tokenizes the CSS content, then builds
 * a rule database that can be queried during HTML parsing.
 *
 * Supported selectors:
 *   - Element selectors: p, div, h1, etc.
 *   - Class selectors: .classname
 *   - Combined: element.classname
 *   - Grouped: selector1, selector2 { }
 *   - Descendant: .callout p, div.sidebar span  (two compounds)
 *   - Child: blockquote > p                     (two compounds)
 *
 * Not supported (silently ignored):
 *   - Three or more compounds (.a .b p) -- rejected rather than approximated, see CssSelector.h
 *   - Pseudo-classes and pseudo-elements
 *   - Sibling combinators, attribute and id selectors, the universal selector
 *   - Media queries (content is skipped)
 *   - @import, @font-face, etc.
 */
class CssParser {
 public:
  enum class ParseResult : uint8_t {
    Complete,
    Partial,
    Error,
  };

  enum class CacheStatus : uint8_t {
    Missing,
    Complete,
    Partial,
    Invalid,
  };

  enum class CacheLoadResult : uint8_t {
    Complete,
    LowMemory,
    Invalid,
  };

  // Bump when CSS cache format or rules change; section caches are invalidated when this changes
  // Combined flags header and Matcha style payload; incompatible with both parents.
  static constexpr uint8_t CSS_CACHE_VERSION = 22;

  explicit CssParser(std::string cachePath) : cachePath(std::move(cachePath)) {}
  ~CssParser() = default;

  // Non-copyable
  CssParser(const CssParser&) = delete;
  CssParser& operator=(const CssParser&) = delete;

  /**
   * Load and parse CSS from a file stream.
   * Can be called multiple times to accumulate rules from multiple stylesheets.
   * @param source Open file handle to read from
   * @return Complete unless bounded storage stopped rule growth or the source was invalid
   */
  ParseResult loadFromStream(HalFile& source);

  /**
   * Look up the style for an HTML element from its tag, its classes and -- when the caller
   * tracks one -- the chain of elements it sits inside.
   *
   * Cascade: every matching rule is collected and applied in ascending CSS specificity, so
   * `p` < `div p` < `.note` < {`p.note`, `.callout p`} < `div p.note` < `.callout .note`.
   * Equal specificity (the braced pair above) resolves to the LAST match in enumeration order
   * -- simple before compound, outer ancestor before inner, descendant before child -- rather
   * than to document order, which the rule table does not preserve. See resolveStyle() in the
   * .cpp for why.
   *
   * @param tagName The HTML element name (e.g., "p", "div")
   * @param classAttr The class attribute value (may contain multiple space-separated classes)
   * @param path Open-element chain with THIS element on top; nullptr disables descendant and
   *             child matching (simple selectors are unaffected)
   * @return Combined style with all applicable rules merged
   */
  [[nodiscard]] CssStyle resolveStyle(std::string_view tagName, std::string_view classAttr,
                                      const CssElementPath* path = nullptr) const;

  /**
   * Parse an inline style attribute string.
   * @param styleValue The value of a style="" attribute
   * @return Parsed style properties
   */
  [[nodiscard]] static CssStyle parseInlineStyle(std::string_view styleValue);

  /**
   * Check if any rules have been loaded
   */
  [[nodiscard]] bool empty() const { return entryCount_ == 0; }

  /**
   * Get count of loaded rule sets
   */
  [[nodiscard]] size_t ruleCount() const { return entryCount_; }

  /**
   * Clear all loaded rules
   */
  void clear() {
    entries_.reset();
    selectorPool_.reset();
    stylePool_.reset();
    entryCount_ = entryCapacity_ = 0;
    selectorPoolSize_ = selectorPoolCapacity_ = 0;
    styleCount_ = styleCapacity_ = 0;
    ruleGrowthStopped_ = false;
  }

  /**
   * True if a parse had to drop selectors because the heap ran low (transient condition, NOT
   * the deterministic MAX_RULES cap). A partial rule table must not be persisted: the cached
   * rule count becomes the permanent styling for every future open, even after the heap
   * recovers. Callers check this before saveToCache().
   */
  [[nodiscard]] bool wasHeapTruncated() const { return heapTruncated_; }

  /**
   * True if the last loadFromCache() aborted because the heap was too low to hold the rule
   * table -- the cache FILE itself is valid. Callers must treat this as "retry next open",
   * NOT as a stale cache: deleting it triggers a full CSS re-parse (worse heap pressure than
   * the load) plus a section-cache invalidation cascade on every subsequent boot.
   */
  [[nodiscard]] bool cacheLoadFailedForHeap() const { return cacheLoadFailedForHeap_; }

  /**
   * Check if CSS rules cache file exists
   */
  bool hasCache() const;

  /** Read the cache header without hydrating its rule map. */
  CacheStatus inspectCache() const;

  /**
   * Delete CSS rules cache file exists
   */
  void deleteCache() const;

  /**
   * Save parsed CSS rules to a cache file.
   * @return true if cache was written successfully
   */
  bool saveToCache(bool complete) const;

  /**
   * Load CSS rules from a cache file.
   * Clears any existing rules before loading.
   * usedClasses (optional): class names actually present in the chapter being built. When set,
   * class-based selectors whose class is not in the list are skipped -- the full EBPAJ template
   * defines hundreds of utility-class variants per writing mode, and materializing all of them
   * (observed: the 1500-rule cap) cannot fit in a mid-session heap, while a single chapter uses
   * a few dozen. Tag-type selectors are always loaded.
   * @return true if cache was loaded successfully
   */
  CacheLoadResult loadFromCache(const std::vector<std::string>* usedClasses = nullptr);

  /**
   * Structurally validate the cache file WITHOUT materializing the rule map. Book open only
   * needs to know "is the cache present and readable" -- building the full map (thousands of
   * small allocations for a heavy book) just to throw it away was itself the low-heap failure
   * that triggered the delete/re-parse cascade. Walks version, rule count, and every record's
   * framing using a few stack bytes. Removes the file on version mismatch (like loadFromCache).
   */
  bool validateCache() const;

  /**
   * Incremental cache writing, one flush per parsed CSS file, so only ONE file's rules are ever
   * resident while parsing (a heavy book's full table blocked the remaining files from parsing
   * at all: 818 resident rules left ~23KB free vs the 64KB the next parse needs).
   * Usage: beginCacheAppend() once, then per file: parse -> appendRulesToCache() -> clear().
   * Finish with endCacheAppend(discard): discard=true (or zero rules) deletes the file instead.
   * Duplicate selectors across files are resolved at load time (loadFromCache merges like the
   * parser does).
   */
  bool beginCacheAppend();
  bool appendRulesToCache();
  bool endCacheAppend(bool discard);

  /**
   * Distilled per-block layout parameters the VERTICAL engine consumes, in em units (the
   * layout converts em -> cells). Populated from unscoped rules plus the EBPAJ "v|" scope.
   */
  struct VerticalBlockStyle {
    float startEm = 0;         // v margin-top: every column of the block starts this many cells down
    float beforeEm = 0;        // v margin-right: extra gap before the block's first column
    float afterEm = 0;         // v margin-left: extra gap after the block's last column
    float hangEm = 0;          // v padding-top + negative text-indent: hanging indent for wrapped lines
    bool alignCenter = false;  // text-align: center -> column content vertically centered
    uint8_t borderEdges = 0;   // CssStyle::BORDER_* edge mask; vertical rendering stays solid
    [[nodiscard]] bool any() const {
      return startEm > 0 || beforeEm > 0 || afterEm > 0 || hangEm > 0 || alignCenter || borderEdges != 0;
    }
  };

  /**
   * Stream the on-disk rules cache and collect (selector -> VerticalBlockStyle) for every
   * selector with at least one vertical-relevant property, without materializing the rule map.
   * Returns the number collected (bounded by maxOut).
   */
  size_t collectVerticalStyles(std::vector<std::pair<std::string, VerticalBlockStyle>>& out, size_t maxOut = 256) const;

 private:
  enum class RuleInsertResult : uint8_t {
    Inserted,
    Merged,
    Limit,
    OutOfMemory,
  };

  enum class PoolResult : uint8_t {
    Ready,
    Limit,
    OutOfMemory,
  };

  bool heapTruncated_ = false;           // see wasHeapTruncated()
  bool cacheLoadFailedForHeap_ = false;  // see cacheLoadFailedForHeap()

  // Set when a rule with that combinator is present. resolveStyle walks the ancestor chain
  // only when one is set, so a book with no compound selectors (the common case) does the
  // exact same lookups it did before this existed. Sticky within a parser: clear() empties
  // the map as a write buffer between stylesheets, and a stale `true` costs lookups, never
  // correctness.
  bool hasDescendantRules_ = false;
  bool hasChildRules_ = false;

  /** Record which combinators the table contains, from a stored (already normalized) key. */
  void noteCombinatorsIn(std::string_view key);

  // Incremental cache writing state (beginCacheAppend/appendRulesToCache/endCacheAppend).
  HalFile cacheAppendFile_;
  uint16_t appendedRuleCount_ = 0;
  bool cacheAppendActive_ = false;
  bool cacheAppendFailed_ = false;
  bool promoteCache() const;

  struct SelectorEntry {
    uint32_t offset;
    uint16_t styleIndex;
    uint16_t length;
  };
  static_assert(sizeof(SelectorEntry) == 8);

  // Bounded flat storage keeps every growth operation fallible and avoids the
  // throwing node allocations used by std::unordered_map.
  std::unique_ptr<SelectorEntry[]> entries_;
  std::unique_ptr<char[]> selectorPool_;
  std::unique_ptr<CssStyle[]> stylePool_;
  uint16_t entryCount_ = 0;
  uint16_t entryCapacity_ = 0;
  uint32_t selectorPoolSize_ = 0;
  uint32_t selectorPoolCapacity_ = 0;
  uint16_t styleCount_ = 0;
  uint16_t styleCapacity_ = 0;
  bool ruleGrowthStopped_ = false;

  std::string cachePath;

  /**
   * The colours seen so far inside ONE `{ ... }` rule block, as luma (0 = black, 255 = white).
   * Purely transient: it lives on the stack for the duration of the block and is folded into the
   * single CssStyle::inkMode byte by resolveInkMode(). Deliberately not a CssStyle member -- a
   * heavy book's rule map holds hundreds of styles at once and every byte there multiplies.
   */
  struct InkColors {
    static constexpr int16_t UNSET = -1;
    int16_t textLuma = UNSET;  // `color`
    int16_t bgLuma = UNSET;    // `background-color`
    [[nodiscard]] bool any() const { return textLuma != UNSET || bgLuma != UNSET; }
  };

  // Internal parsing helpers
  bool restoreCacheBackupIfNeeded() const;
  void processRuleBlockWithStyle(std::string_view selectorGroup, const CssStyle& style);
  [[nodiscard]] const CssStyle* findStyle(std::span<const std::string_view> pieces) const;
  [[nodiscard]] int compareEntryToPieces(const SelectorEntry& entry, std::span<const std::string_view> pieces) const;
  [[nodiscard]] size_t lowerBound(std::string_view p0, std::string_view p1, std::string_view p2, bool& exact) const;
  [[nodiscard]] const CssStyle* findStyle(std::string_view p0, std::string_view p1 = {},
                                          std::string_view p2 = {}) const;
  [[nodiscard]] std::string_view selectorAt(size_t index) const;
  RuleInsertResult insertOrMerge(std::string_view selector, const CssStyle& style);
  PoolResult ensureEntryCapacity(size_t needed);
  PoolResult ensureSelectorPoolCapacity(size_t needed);
  PoolResult ensureStyleCapacity(size_t needed);
  PoolResult internStyle(const CssStyle& style, uint16_t& indexOut);
  static CssStyle parseDeclarations(std::string_view declBlock);
  static void parseDeclarationIntoStyle(std::string_view decl, CssStyle& style, InkColors& ink);
  /** Fold the block's colours into style.inkMode. Call once, after the block's last declaration. */
  static void resolveInkMode(CssStyle& style, const InkColors& ink);

  // Individual property value parsers
  static CssTextAlign interpretAlignment(std::string_view val);
  static CssFontStyle interpretFontStyle(std::string_view val);
  static CssFontWeight interpretFontWeight(std::string_view val);
  static CssTextDecoration interpretDecoration(std::string_view val);
  static CssTextEmphasis interpretTextEmphasis(std::string_view val);
  static CssListStyleType interpretListStyleType(std::string_view val);
  static CssLength interpretLength(std::string_view val);
  /** Returns true only when a numeric length was parsed (e.g. 2em, 50%). False for auto/inherit/initial. */
  static bool tryInterpretLength(std::string_view val, CssLength& out);
};
