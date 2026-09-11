#include "CssParser.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string_view>

namespace {

// Stack-allocated string buffer to avoid heap reallocations during parsing
// Provides string-like interface with fixed capacity
struct StackBuffer {
  static constexpr size_t CAPACITY = 1024;
  char data[CAPACITY];
  size_t len = 0;

  bool push_back(char c) {
    if (len >= CAPACITY) return false;
    data[len++] = c;
    return true;
  }

  void clear() { len = 0; }
  bool empty() const { return len == 0; }
  size_t size() const { return len; }

  // Get string view of current content (zero-copy)
  std::string_view view() const { return std::string_view(data, len); }
  operator std::string_view() const noexcept { return view(); }
};

// Buffer size for reading CSS files
constexpr size_t READ_BUFFER_SIZE = 512;

// Flat rule-store caps. The index is 12KB at MAX_RULES, selector text is
// bounded to 32KB, and deduplicated style bodies are bounded to about 26KB.
constexpr size_t MAX_RULES = 1500;
// Rules the CACHE FILE may hold. Deliberately larger than MAX_RULES, which bounds the rule map held
// in RAM: the cache lives on the SD card and is read back FILTERED -- loadFromCache() keeps only the
// selectors a chapter's classes use, and collectVerticalStyles() streams the file for vertical-block
// rules. Capping the FILE at the RAM figure truncates in file order, so whether a book's styling
// survives depends on where it sits in its stylesheet: the EBPAJ template runs past 1500 rules, and
// everything after that point was absent from every book built on it.
constexpr size_t MAX_CACHED_RULES = 4000;
constexpr size_t SELECTOR_POOL_CAP = 32 * 1024;
constexpr size_t MAX_UNIQUE_STYLES = 256;

// Maximum length for a single selector string
// Prevents parsing of extremely long or malformed selectors
constexpr size_t MAX_SELECTOR_LENGTH = 256;

// Check if character is CSS whitespace
constexpr bool isCssWhitespace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

constexpr std::string_view trimCssWhitespace(std::string_view s) {
  while (!s.empty() && isCssWhitespace(s.front())) s.remove_prefix(1);
  while (!s.empty() && isCssWhitespace(s.back())) s.remove_suffix(1);
  return s;
}

constexpr char asciiToLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Case-insensitive equality on ASCII. lowercaseKeyword MUST already be
// lowercase; CSS keywords are ASCII by spec so byte-wise tolower is safe.
constexpr bool iequalsAscii(std::string_view value, std::string_view lowercaseKeyword) {
  return std::equal(value.begin(), value.end(), lowercaseKeyword.begin(), lowercaseKeyword.end(),
                    [](char a, char b) { return asciiToLower(a) == b; });
}

// Walk s and invoke fn(token) for each non-empty run between delimiters.
// Tokens are boundary-trimmed and yielded as string_views into s; no
// allocation. Runs of consecutive delimiters coalesce — no empty tokens are
// emitted. `isDelimiter` is invoked once per character.
template <typename Pred, typename F>
void forEachDelimitedToken(std::string_view s, Pred isDelimiter, F&& fn) {
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || isDelimiter(s[i])) {
      const std::string_view trimmed = trimCssWhitespace(s.substr(start, i - start));
      if (!trimmed.empty()) {
        fn(trimmed);
      }
      start = i + 1;
    }
  }
}

// Parse the entirety of s as a number into `out`. Accepts an optional leading
// '+' (which std::from_chars rejects by spec) so callers can pass CSS-style
// signed numbers without manual trimming. Returns false on empty input, a
// non-numeric suffix, or any from_chars error.
template <typename T>
bool tryParseNumber(std::string_view s, T& out) {
  const char* begin = s.data();
  const char* end = s.data() + s.size();
  if (begin < end && *begin == '+') ++begin;
  const auto r = std::from_chars(begin, end, out);
  return r.ec == std::errc{} && r.ptr == end;
}

// Collect up to 4 whitespace-separated tokens for a CSS edge-value shorthand
// (margin, padding, and the border-* family). Returns the number of tokens
// written; extras are silently dropped. Callers apply the 1/2/3/4-value
// fallback rule using the returned count.
size_t collectEdgeValueTokens(std::string_view s, std::string_view (&out)[4]) {
  size_t count = 0;
  forEachDelimitedToken(s, isCssWhitespace, [&](std::string_view tok) {
    if (count < 4) out[count++] = tok;
  });
  return count;
}

// True when the token is a bare number with no unit suffix ("1.4", "2"). tryInterpretLength()
// resolves an absent unit to Pixels, which is right for every property that takes a length --
// line-height is the one property whose unitless form means a MULTIPLE of the font size, so it
// has to be told apart afterwards. Looking at the last character is enough: every unit this
// parser understands (em, rem, pt, px, %) ends in something that is not a digit or a dot.
constexpr bool isUnitlessNumber(std::string_view v) {
  v = trimCssWhitespace(v);
  return !v.empty() && ((v.back() >= '0' && v.back() <= '9') || v.back() == '.');
}

std::string_view stripTrailingImportant(std::string_view value) {
  constexpr std::string_view IMPORTANT = "!important";

  while (!value.empty() && isCssWhitespace(value.back())) {
    value.remove_suffix(1);
  }

  if (value.size() < IMPORTANT.size()) {
    return value;
  }

  const size_t suffixPos = value.size() - IMPORTANT.size();
  if (!iequalsAscii(value.substr(suffixPos), IMPORTANT)) {
    return value;
  }

  value.remove_suffix(IMPORTANT.size());
  while (!value.empty() && isCssWhitespace(value.back())) {
    value.remove_suffix(1);
  }
  return value;
}

// page-break-* / break-* values. Everything that means "start a new page" on a single-column
// reader collapses to Always: the spread keywords (left/right/recto/verso) cannot be honoured as
// spreads here, and forcing the break is much closer to the author's intent than ignoring it.
// `avoid-page` is the break-* spelling of `avoid`.
//
// Anything else -- auto, inherit, the column/region keywords (avoid-column, column, region) --
// returns Auto, which never overwrites an existing value and never sets the defined bit. A
// property we cannot honour must stay absent rather than be cached as an assertion.
constexpr CssPageBreak interpretPageBreak(const std::string_view value) {
  if (iequalsAscii(value, "always") || iequalsAscii(value, "page") || iequalsAscii(value, "left") ||
      iequalsAscii(value, "right") || iequalsAscii(value, "recto") || iequalsAscii(value, "verso")) {
    return CssPageBreak::Always;
  }
  if (iequalsAscii(value, "avoid") || iequalsAscii(value, "avoid-page")) {
    return CssPageBreak::Avoid;
  }
  return CssPageBreak::Auto;
}

// --- colour -> luma ---------------------------------------------------------------------------
//
// Rec.601 luma as integers: (77R + 151G + 28B) >> 8 approximates 0.299R + 0.587G + 0.114B to
// within one 8-bit step, with no float and no table lookup. Deliberately NOT the gamma-decoded
// WCAG relative luminance, which needs a pow() per channel: the only question ever asked of the
// result is which of two colours is lighter (resolveInkMode), and both measures agree on that
// for the near-neutral greys books actually use for panels.
constexpr uint8_t lumaOfRgb(const uint32_t r, const uint32_t g, const uint32_t b) {
  return static_cast<uint8_t>((77u * r + 151u * g + 28u * b) >> 8);
}
constexpr uint8_t lumaOfHex(const uint32_t hex) {
  return lumaOfRgb((hex >> 16) & 0xFFu, (hex >> 8) & 0xFFu, hex & 0xFFu);
}

// The CSS named colours EPUB stylesheets realistically use, reduced to luma at COMPILE time: the
// table is 8 bytes per entry in flash and no RGB triple exists at runtime.
struct NamedColorLuma {
  const char* name;
  uint8_t luma;
};
constexpr NamedColorLuma NAMED_COLORS[] = {
    {"black", lumaOfHex(0x000000)},      {"white", lumaOfHex(0xFFFFFF)},       {"silver", lumaOfHex(0xC0C0C0)},
    {"gray", lumaOfHex(0x808080)},       {"grey", lumaOfHex(0x808080)},        {"darkgray", lumaOfHex(0xA9A9A9)},
    {"darkgrey", lumaOfHex(0xA9A9A9)},   {"lightgray", lumaOfHex(0xD3D3D3)},   {"lightgrey", lumaOfHex(0xD3D3D3)},
    {"dimgray", lumaOfHex(0x696969)},    {"dimgrey", lumaOfHex(0x696969)},     {"slategray", lumaOfHex(0x708090)},
    {"slategrey", lumaOfHex(0x708090)},  {"gainsboro", lumaOfHex(0xDCDCDC)},   {"whitesmoke", lumaOfHex(0xF5F5F5)},
    {"ghostwhite", lumaOfHex(0xF8F8FF)}, {"snow", lumaOfHex(0xFFFAFA)},        {"ivory", lumaOfHex(0xFFFFF0)},
    {"beige", lumaOfHex(0xF5F5DC)},      {"linen", lumaOfHex(0xFAF0E6)},       {"red", lumaOfHex(0xFF0000)},
    {"darkred", lumaOfHex(0x8B0000)},    {"crimson", lumaOfHex(0xDC143C)},     {"maroon", lumaOfHex(0x800000)},
    {"orange", lumaOfHex(0xFFA500)},     {"orangered", lumaOfHex(0xFF4500)},   {"gold", lumaOfHex(0xFFD700)},
    {"yellow", lumaOfHex(0xFFFF00)},     {"lightyellow", lumaOfHex(0xFFFFE0)}, {"olive", lumaOfHex(0x808000)},
    {"khaki", lumaOfHex(0xF0E68C)},      {"tan", lumaOfHex(0xD2B48C)},         {"brown", lumaOfHex(0xA52A2A)},
    {"green", lumaOfHex(0x008000)},      {"darkgreen", lumaOfHex(0x006400)},   {"lime", lumaOfHex(0x00FF00)},
    {"lightgreen", lumaOfHex(0x90EE90)}, {"teal", lumaOfHex(0x008080)},        {"cyan", lumaOfHex(0x00FFFF)},
    {"aqua", lumaOfHex(0x00FFFF)},       {"blue", lumaOfHex(0x0000FF)},        {"darkblue", lumaOfHex(0x00008B)},
    {"navy", lumaOfHex(0x000080)},       {"lightblue", lumaOfHex(0xADD8E6)},   {"steelblue", lumaOfHex(0x4682B4)},
    {"royalblue", lumaOfHex(0x4169E1)},  {"purple", lumaOfHex(0x800080)},      {"indigo", lumaOfHex(0x4B0082)},
    {"violet", lumaOfHex(0xEE82EE)},     {"magenta", lumaOfHex(0xFF00FF)},     {"fuchsia", lumaOfHex(0xFF00FF)},
    {"pink", lumaOfHex(0xFFC0CB)},
};

constexpr int hexDigitValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  const char lower = asciiToLower(c);
  if (lower >= 'a' && lower <= 'f') return lower - 'a' + 10;
  return -1;
}

// s is the body after '#'. Accepts 3/4/6/8 digits; the 4- and 8-digit forms carry alpha, which is
// dropped (a 1-bit panel has nothing to blend against).
bool tryParseHexColorLuma(const std::string_view s, uint8_t& lumaOut) {
  if (s.size() != 3 && s.size() != 4 && s.size() != 6 && s.size() != 8) return false;
  int digits[8];
  for (size_t i = 0; i < s.size(); ++i) {
    digits[i] = hexDigitValue(s[i]);
    if (digits[i] < 0) return false;
  }
  if (s.size() <= 4) {
    // Shorthand: each digit is doubled, #abc == #aabbcc, i.e. value * 17.
    lumaOut = lumaOfRgb(digits[0] * 17, digits[1] * 17, digits[2] * 17);
  } else {
    lumaOut = lumaOfRgb(digits[0] * 16 + digits[1], digits[2] * 16 + digits[3], digits[4] * 16 + digits[5]);
  }
  return true;
}

// rgb()/rgba(), with integer or percentage channels and either comma or space separators.
bool tryParseRgbFunctionLuma(const std::string_view s, uint8_t& lumaOut) {
  const size_t open = s.find('(');
  if (open == std::string_view::npos || s.empty() || s.back() != ')') return false;
  const std::string_view fn = trimCssWhitespace(s.substr(0, open));
  if (!iequalsAscii(fn, "rgb") && !iequalsAscii(fn, "rgba")) return false;

  const std::string_view args = s.substr(open + 1, s.size() - open - 2);
  uint32_t channels[3] = {0, 0, 0};
  size_t count = 0;
  bool malformed = false;
  forEachDelimitedToken(
      args, [](const char c) { return c == ',' || c == '/' || isCssWhitespace(c); },
      [&](std::string_view token) {
        if (malformed || count >= 3) return;  // 4th token is alpha; ignored
        float channelValue = 0.0f;
        if (token.back() == '%') {
          if (!tryParseNumber(token.substr(0, token.size() - 1), channelValue)) {
            malformed = true;
            return;
          }
          channelValue = channelValue * 255.0f / 100.0f;
        } else if (!tryParseNumber(token, channelValue)) {
          malformed = true;
          return;
        }
        channels[count++] = static_cast<uint32_t>(std::clamp(channelValue, 0.0f, 255.0f));
      });
  if (malformed || count < 3) return false;
  lumaOut = lumaOfRgb(channels[0], channels[1], channels[2]);
  return true;
}

// Parse a CSS colour to luma (0 = black, 255 = white). Anything unrecognised -- `transparent`,
// `inherit`, `currentColor`, hsl(), a var() reference -- returns false, and the caller leaves the
// property UNSET rather than guessing. Never persist a guess: an ink mode is stored in the CSS
// cache and would then be believed forever.
bool tryParseColorLuma(std::string_view val, uint8_t& lumaOut) {
  val = trimCssWhitespace(stripTrailingImportant(val));
  if (val.empty()) return false;
  if (val.front() == '#') return tryParseHexColorLuma(val.substr(1), lumaOut);
  if (val.find('(') != std::string_view::npos) return tryParseRgbFunctionLuma(val, lumaOut);
  for (const auto& [name, luma] : NAMED_COLORS) {
    if (iequalsAscii(val, name)) {
      lumaOut = luma;
      return true;
    }
  }
  return false;
}

// The `background` shorthand carries the colour among image/position/repeat keywords; take the
// first token that parses as one.
bool tryParseBackgroundShorthandLuma(const std::string_view val, uint8_t& lumaOut) {
  bool found = false;
  forEachDelimitedToken(stripTrailingImportant(val), isCssWhitespace, [&](const std::string_view token) {
    if (!found && tryParseColorLuma(token, lumaOut)) found = true;
  });
  return found;
}

constexpr std::array STYLE_LENGTH_FIELDS = {
    &CssStyle::textIndent,   &CssStyle::marginTop,     &CssStyle::marginBottom,  &CssStyle::marginLeft,
    &CssStyle::marginRight,  &CssStyle::paddingTop,    &CssStyle::paddingBottom, &CssStyle::paddingLeft,
    &CssStyle::paddingRight, &CssStyle::imageHeight,   &CssStyle::imageWidth,    &CssStyle::fontSize,
    &CssStyle::lineHeight,   &CssStyle::letterSpacing,
};
constexpr size_t STYLE_LENGTH_FIELD_COUNT = STYLE_LENGTH_FIELDS.size();
constexpr size_t STYLE_WIRE_BYTES =
    5 + STYLE_LENGTH_FIELD_COUNT * (sizeof(decltype(CssLength::value)) + 1) + 9 + sizeof(uint32_t);
constexpr uint32_t CSS_DEFINED_BITS_MASK = (1u << 29) - 1;

void encodeStyleWire(const CssStyle& style, uint8_t (&out)[STYLE_WIRE_BYTES]) {
  size_t offset = 0;
  out[offset++] = static_cast<uint8_t>(style.textAlign);
  out[offset++] = static_cast<uint8_t>(style.fontStyle);
  out[offset++] = static_cast<uint8_t>(style.fontWeight);
  out[offset++] = static_cast<uint8_t>(style.textDecoration);
  out[offset++] = static_cast<uint8_t>(style.direction);

  const auto putLength = [&out, &offset](const CssLength& length) {
    memcpy(out + offset, &length.value, sizeof(length.value));
    offset += sizeof(length.value);
    out[offset++] = static_cast<uint8_t>(length.unit);
  };
  for (const auto field : STYLE_LENGTH_FIELDS) {
    putLength(style.*field);
  }
  out[offset++] = static_cast<uint8_t>(style.display);
  out[offset++] = static_cast<uint8_t>(style.verticalAlign);
  out[offset++] = static_cast<uint8_t>(style.borderEdges);
  out[offset++] = static_cast<uint8_t>(style.textEmphasis);
  out[offset++] = static_cast<uint8_t>(style.fontVariant);
  out[offset++] = static_cast<uint8_t>(style.listStyleType);
  out[offset++] = static_cast<uint8_t>(style.inkMode);
  out[offset++] = static_cast<uint8_t>(style.pageBreaks);
  out[offset++] = static_cast<uint8_t>(style.textFlags);

  uint32_t definedBits = 0;
  if (style.defined.textAlign) definedBits |= 1 << 0;
  if (style.defined.fontStyle) definedBits |= 1 << 1;
  if (style.defined.fontWeight) definedBits |= 1 << 2;
  if (style.defined.textDecoration) definedBits |= 1 << 3;
  if (style.defined.textIndent) definedBits |= 1 << 4;
  if (style.defined.marginTop) definedBits |= 1 << 5;
  if (style.defined.marginBottom) definedBits |= 1 << 6;
  if (style.defined.marginLeft) definedBits |= 1 << 7;
  if (style.defined.marginRight) definedBits |= 1 << 8;
  if (style.defined.paddingTop) definedBits |= 1 << 9;
  if (style.defined.paddingBottom) definedBits |= 1 << 10;
  if (style.defined.paddingLeft) definedBits |= 1 << 11;
  if (style.defined.paddingRight) definedBits |= 1 << 12;
  if (style.defined.imageHeight) definedBits |= 1 << 13;
  if (style.defined.imageWidth) definedBits |= 1 << 14;
  if (style.defined.display) definedBits |= 1 << 15;
  if (style.defined.direction) definedBits |= 1 << 16;
  if (style.defined.verticalAlign) definedBits |= 1 << 17;
  if (style.defined.border) definedBits |= 1u << 18;
  if (style.defined.textEmphasis) definedBits |= 1u << 19;
  if (style.defined.fontVariant) definedBits |= 1u << 20;
  if (style.defined.listStyleType) definedBits |= 1u << 21;
  if (style.defined.fontSize) definedBits |= 1u << 22;
  if (style.defined.inkMode) definedBits |= 1u << 23;
  if (style.defined.pageBreak) definedBits |= 1u << 24;
  if (style.defined.lineHeight) definedBits |= 1u << 25;
  if (style.defined.textTransform) definedBits |= 1u << 26;
  if (style.defined.hyphens) definedBits |= 1u << 27;
  if (style.defined.letterSpacing) definedBits |= 1u << 28;
  memcpy(out + offset, &definedBits, sizeof(definedBits));
}

bool decodeStyleWire(const uint8_t (&in)[STYLE_WIRE_BYTES], CssStyle& style) {
  size_t offset = 0;
  const uint8_t textAlign = in[offset++];
  const uint8_t fontStyle = in[offset++];
  const uint8_t fontWeight = in[offset++];
  const uint8_t textDecoration = in[offset++];
  const uint8_t direction = in[offset++];
  if (textAlign > static_cast<uint8_t>(CssTextAlign::None) || fontStyle > static_cast<uint8_t>(CssFontStyle::Italic) ||
      fontWeight > static_cast<uint8_t>(CssFontWeight::Bold) || (textDecoration & ~CSS_TEXT_DECORATION_MASK) != 0 ||
      direction > static_cast<uint8_t>(CssTextDirection::Rtl)) {
    return false;
  }
  style.textAlign = static_cast<CssTextAlign>(textAlign);
  style.fontStyle = static_cast<CssFontStyle>(fontStyle);
  style.fontWeight = static_cast<CssFontWeight>(fontWeight);
  style.textDecoration = static_cast<CssTextDecoration>(textDecoration);
  style.direction = static_cast<CssTextDirection>(direction);

  const auto getLength = [&in, &offset](CssLength& length) {
    decltype(CssLength::value) value = 0;
    memcpy(&value, in + offset, sizeof(value));
    offset += sizeof(length.value);
    const uint8_t unit = in[offset++];
    if (!std::isfinite(value) || unit > static_cast<uint8_t>(CssUnit::Number)) return false;
    length.value = value;
    length.unit = static_cast<CssUnit>(unit);
    return true;
  };
  for (const auto field : STYLE_LENGTH_FIELDS) {
    if (!getLength(style.*field)) return false;
  }

  const uint8_t display = in[offset++];
  const uint8_t verticalAlign = in[offset++];
  if (display > static_cast<uint8_t>(CssDisplay::InlineBlock) ||
      verticalAlign > static_cast<uint8_t>(CssVerticalAlign::Sub)) {
    return false;
  }
  style.display = static_cast<CssDisplay>(display);
  style.verticalAlign = static_cast<CssVerticalAlign>(verticalAlign);
  style.borderEdges = static_cast<decltype(style.borderEdges)>(in[offset++]);
  style.textEmphasis = static_cast<decltype(style.textEmphasis)>(in[offset++]);
  style.fontVariant = static_cast<decltype(style.fontVariant)>(in[offset++]);
  style.listStyleType = static_cast<decltype(style.listStyleType)>(in[offset++]);
  style.inkMode = static_cast<decltype(style.inkMode)>(in[offset++]);
  style.pageBreaks = static_cast<decltype(style.pageBreaks)>(in[offset++]);
  style.textFlags = static_cast<decltype(style.textFlags)>(in[offset++]);
  if (style.textEmphasis > CssTextEmphasis::OpenDoubleCircle || style.fontVariant > CssFontVariant::SmallCaps ||
      style.listStyleType > CssListStyleType::NoMarker || style.inkMode > CssInkMode::Inverted ||
      (style.pageBreaks & 0xc0) != 0 || (style.textFlags & ~7u) != 0)
    return false;
  for (unsigned shift = 0; shift <= 4; shift += 2) {
    if (((style.pageBreaks >> shift) & 3) == 3) return false;
  }

  uint32_t definedBits = 0;
  memcpy(&definedBits, in + offset, sizeof(definedBits));
  if ((definedBits & ~CSS_DEFINED_BITS_MASK) != 0) return false;
  style.defined.textAlign = (definedBits & 1 << 0) != 0;
  style.defined.fontStyle = (definedBits & 1 << 1) != 0;
  style.defined.fontWeight = (definedBits & 1 << 2) != 0;
  style.defined.textDecoration = (definedBits & 1 << 3) != 0;
  style.defined.textIndent = (definedBits & 1 << 4) != 0;
  style.defined.marginTop = (definedBits & 1 << 5) != 0;
  style.defined.marginBottom = (definedBits & 1 << 6) != 0;
  style.defined.marginLeft = (definedBits & 1 << 7) != 0;
  style.defined.marginRight = (definedBits & 1 << 8) != 0;
  style.defined.paddingTop = (definedBits & 1 << 9) != 0;
  style.defined.paddingBottom = (definedBits & 1 << 10) != 0;
  style.defined.paddingLeft = (definedBits & 1 << 11) != 0;
  style.defined.paddingRight = (definedBits & 1 << 12) != 0;
  style.defined.imageHeight = (definedBits & 1 << 13) != 0;
  style.defined.imageWidth = (definedBits & 1 << 14) != 0;
  style.defined.display = (definedBits & 1 << 15) != 0;
  style.defined.direction = (definedBits & 1 << 16) != 0;
  style.defined.verticalAlign = (definedBits & 1 << 17) != 0;
  style.defined.border = (definedBits & (1u << 18)) != 0;
  style.defined.textEmphasis = (definedBits & (1u << 19)) != 0;
  style.defined.fontVariant = (definedBits & (1u << 20)) != 0;
  style.defined.listStyleType = (definedBits & (1u << 21)) != 0;
  style.defined.fontSize = (definedBits & (1u << 22)) != 0;
  style.defined.inkMode = (definedBits & (1u << 23)) != 0;
  style.defined.pageBreak = (definedBits & (1u << 24)) != 0;
  style.defined.lineHeight = (definedBits & (1u << 25)) != 0;
  style.defined.textTransform = (definedBits & (1u << 26)) != 0;
  style.defined.hyphens = (definedBits & (1u << 27)) != 0;
  style.defined.letterSpacing = (definedBits & (1u << 28)) != 0;

  return true;
}

}  // anonymous namespace

int CssParser::compareEntryToPieces(const SelectorEntry& entry, std::span<const std::string_view> pieces) const {
  const char* stored = selectorPool_.get() + entry.offset;
  size_t index = 0;
  for (const std::string_view piece : pieces) {
    for (const char c : piece) {
      if (index == entry.length) return -1;
      const auto storedByte = static_cast<unsigned char>(stored[index]);
      const auto probeByte = static_cast<unsigned char>(asciiToLower(c));
      if (storedByte != probeByte) return storedByte < probeByte ? -1 : 1;
      ++index;
    }
  }
  return index == entry.length ? 0 : 1;
}

size_t CssParser::lowerBound(const std::string_view p0, const std::string_view p1, const std::string_view p2,
                             bool& exact) const {
  const std::string_view pieces[] = {p0, p1, p2};
  size_t low = 0;
  size_t high = entryCount_;
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if (compareEntryToPieces(entries_[middle], pieces) < 0) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  exact = low < entryCount_ && compareEntryToPieces(entries_[low], pieces) == 0;
  return low;
}

const CssStyle* CssParser::findStyle(const std::string_view p0, const std::string_view p1,
                                     const std::string_view p2) const {
  bool exact = false;
  const size_t index = lowerBound(p0, p1, p2, exact);
  return exact ? &stylePool_[entries_[index].styleIndex] : nullptr;
}

const CssStyle* CssParser::findStyle(std::span<const std::string_view> pieces) const {
  size_t low = 0, high = entryCount_;
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if (compareEntryToPieces(entries_[middle], pieces) < 0)
      low = middle + 1;
    else
      high = middle;
  }
  return low < entryCount_ && compareEntryToPieces(entries_[low], pieces) == 0 ? &stylePool_[entries_[low].styleIndex]
                                                                               : nullptr;
}

std::string_view CssParser::selectorAt(const size_t index) const {
  const SelectorEntry& entry = entries_[index];
  return {selectorPool_.get() + entry.offset, entry.length};
}

CssParser::PoolResult CssParser::ensureEntryCapacity(const size_t needed) {
  if (needed <= entryCapacity_) return PoolResult::Ready;
  if (needed > MAX_RULES) return PoolResult::Limit;

  size_t capacity = entryCapacity_ ? entryCapacity_ * 2u : 128u;
  while (capacity < needed) capacity *= 2u;
  capacity = std::min(capacity, MAX_RULES);
  auto grown = makeUniqueNoThrow<SelectorEntry[]>(capacity);
  if (!grown) {
    LOG_ERR("CSS", "OOM: selector index (%zu entries)", capacity);
    return PoolResult::OutOfMemory;
  }
  if (entryCount_ > 0) memcpy(grown.get(), entries_.get(), entryCount_ * sizeof(SelectorEntry));
  entries_ = std::move(grown);
  entryCapacity_ = static_cast<uint16_t>(capacity);
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::ensureSelectorPoolCapacity(const size_t needed) {
  if (needed <= selectorPoolCapacity_) return PoolResult::Ready;
  if (needed > SELECTOR_POOL_CAP) return PoolResult::Limit;

  size_t capacity = selectorPoolCapacity_ ? selectorPoolCapacity_ * 2u : 4096u;
  while (capacity < needed) capacity *= 2u;
  capacity = std::min(capacity, SELECTOR_POOL_CAP);
  auto grown = makeUniqueNoThrow<char[]>(capacity);
  if (!grown) {
    LOG_ERR("CSS", "OOM: selector pool (%zu bytes)", capacity);
    return PoolResult::OutOfMemory;
  }
  if (selectorPoolSize_ > 0) memcpy(grown.get(), selectorPool_.get(), selectorPoolSize_);
  selectorPool_ = std::move(grown);
  selectorPoolCapacity_ = static_cast<uint32_t>(capacity);
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::ensureStyleCapacity(const size_t needed) {
  if (needed <= styleCapacity_) return PoolResult::Ready;
  if (needed > MAX_UNIQUE_STYLES) return PoolResult::Limit;

  size_t capacity = styleCapacity_ ? styleCapacity_ * 2u : 16u;
  while (capacity < needed) capacity *= 2u;
  capacity = std::min(capacity, MAX_UNIQUE_STYLES);
  auto grownStyles = makeUniqueNoThrow<CssStyle[]>(capacity);
  if (!grownStyles) {
    LOG_ERR("CSS", "OOM: style pool (%zu styles)", capacity);
    return PoolResult::OutOfMemory;
  }
  for (size_t i = 0; i < styleCount_; ++i) grownStyles[i] = stylePool_[i];
  stylePool_ = std::move(grownStyles);
  styleCapacity_ = static_cast<uint16_t>(capacity);
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::internStyle(const CssStyle& style, uint16_t& indexOut) {
  uint8_t wire[STYLE_WIRE_BYTES];
  encodeStyleWire(style, wire);
  for (uint16_t i = 0; i < styleCount_; ++i) {
    uint8_t existingWire[STYLE_WIRE_BYTES];
    encodeStyleWire(stylePool_[i], existingWire);
    if (memcmp(existingWire, wire, STYLE_WIRE_BYTES) == 0) {
      indexOut = i;
      return PoolResult::Ready;
    }
  }

  const PoolResult capacityResult = ensureStyleCapacity(static_cast<size_t>(styleCount_) + 1);
  if (capacityResult != PoolResult::Ready) return capacityResult;
  stylePool_[styleCount_] = style;
  indexOut = styleCount_++;
  return PoolResult::Ready;
}

CssParser::RuleInsertResult CssParser::insertOrMerge(const std::string_view selector, const CssStyle& style) {
  bool exact = false;
  const size_t position = lowerBound(selector, {}, {}, exact);
  if (exact) {
    const uint16_t currentStyleIndex = entries_[position].styleIndex;
    CssStyle merged = stylePool_[currentStyleIndex];
    merged.applyOver(style);

    bool styleIsShared = false;
    for (uint16_t i = 0; i < entryCount_; ++i) {
      if (i != position && entries_[i].styleIndex == currentStyleIndex) {
        styleIsShared = true;
        break;
      }
    }
    if (!styleIsShared) {
      stylePool_[currentStyleIndex] = merged;
      return RuleInsertResult::Merged;
    }

    uint16_t styleIndex = 0;
    const PoolResult result = internStyle(merged, styleIndex);
    if (result == PoolResult::Limit) return RuleInsertResult::Limit;
    if (result == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;
    entries_[position].styleIndex = styleIndex;
    return RuleInsertResult::Merged;
  }

  const PoolResult entryResult = ensureEntryCapacity(static_cast<size_t>(entryCount_) + 1);
  if (entryResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (entryResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  const size_t requiredSelectorBytes = static_cast<size_t>(selectorPoolSize_) + selector.size();
  const PoolResult selectorResult = ensureSelectorPoolCapacity(requiredSelectorBytes);
  if (selectorResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (selectorResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  uint16_t styleIndex = 0;
  const PoolResult styleResult = internStyle(style, styleIndex);
  if (styleResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (styleResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  const uint32_t selectorOffset = selectorPoolSize_;
  char* destination = selectorPool_.get() + selectorOffset;
  for (const char c : selector) *destination++ = asciiToLower(c);
  selectorPoolSize_ = static_cast<uint32_t>(requiredSelectorBytes);

  SelectorEntry* entries = entries_.get();
  memmove(entries + position + 1, entries + position, (entryCount_ - position) * sizeof(SelectorEntry));
  entries[position] = {selectorOffset, styleIndex, static_cast<uint16_t>(selector.size())};
  ++entryCount_;
  return RuleInsertResult::Inserted;
}

// Property value interpreters

CssTextAlign CssParser::interpretAlignment(std::string_view val) {
  val = trimCssWhitespace(val);

  if (iequalsAscii(val, "left") || iequalsAscii(val, "start")) return CssTextAlign::Left;
  if (iequalsAscii(val, "right") || iequalsAscii(val, "end")) return CssTextAlign::Right;
  if (iequalsAscii(val, "center")) return CssTextAlign::Center;
  if (iequalsAscii(val, "justify")) return CssTextAlign::Justify;

  return CssTextAlign::Left;
}

CssFontStyle CssParser::interpretFontStyle(std::string_view val) {
  val = trimCssWhitespace(val);

  if (iequalsAscii(val, "italic") || iequalsAscii(val, "oblique")) return CssFontStyle::Italic;
  return CssFontStyle::Normal;
}

CssFontWeight CssParser::interpretFontWeight(std::string_view val) {
  val = trimCssWhitespace(val);

  // Named values
  if (iequalsAscii(val, "bold") || iequalsAscii(val, "bolder")) return CssFontWeight::Bold;
  if (iequalsAscii(val, "normal") || iequalsAscii(val, "lighter")) return CssFontWeight::Normal;

  // Numeric values: 100-900
  // CSS spec: 400 = normal, 700 = bold
  // We use: 0-400 = normal, 700+ = bold, 500-600 = normal (conservative)
  long numericWeight = 0;
  if (tryParseNumber(val, numericWeight)) {
    return numericWeight >= 700 ? CssFontWeight::Bold : CssFontWeight::Normal;
  }
  return CssFontWeight::Normal;
}

CssTextDecoration CssParser::interpretDecoration(std::string_view val) {
  // text-decoration can have multiple space-separated values. Compare whole tokens
  // so malformed values like "notunderline" do not accidentally enable a line.
  CssTextDecoration result = CssTextDecoration::None;
  bool explicitNone = false;
  forEachDelimitedToken(val, isCssWhitespace, [&](const std::string_view token) {
    if (iequalsAscii(token, "none")) {
      explicitNone = true;
    } else if (iequalsAscii(token, "underline")) {
      result = result | CssTextDecoration::Underline;
    } else if (iequalsAscii(token, "line-through")) {
      result = result | CssTextDecoration::LineThrough;
    }
  });
  return explicitNone ? CssTextDecoration::None : result;
}

CssTextEmphasis CssParser::interpretTextEmphasis(std::string_view val) {
  // Value is fill + shape in either order ("filled sesame", "open dot", ...).
  // "none" wins outright; a missing fill keyword means filled per the CSS spec.
  //
  // Tokenised rather than substring-matched: "double-circle" contains "circle", so a contains()
  // approach only works if the longer keyword is tested first -- an ordering dependency that is
  // easy to break. Whole-token comparison removes it.
  bool none = false, open = false;
  CssTextEmphasis shape = CssTextEmphasis::FilledSesame;
  bool haveShape = false;
  forEachDelimitedToken(val, isCssWhitespace, [&](const std::string_view token) {
    if (iequalsAscii(token, "none")) {
      none = true;
    } else if (iequalsAscii(token, "open")) {
      open = true;
    } else if (iequalsAscii(token, "sesame")) {
      shape = CssTextEmphasis::FilledSesame;
      haveShape = true;
    } else if (iequalsAscii(token, "double-circle")) {
      shape = CssTextEmphasis::FilledDoubleCircle;
      haveShape = true;
    } else if (iequalsAscii(token, "circle")) {
      shape = CssTextEmphasis::FilledCircle;
      haveShape = true;
    } else if (iequalsAscii(token, "triangle")) {
      shape = CssTextEmphasis::FilledTriangle;
      haveShape = true;
    } else if (iequalsAscii(token, "dot")) {
      shape = CssTextEmphasis::FilledDot;
      haveShape = true;
    }
  });
  if (none) return CssTextEmphasis::None;
  // Bare "filled"/"open" (or a string mark we don't support): default shape. The JP bouten
  // convention is the sesame dot, which is also what EBPAJ books use.
  if (!haveShape) shape = CssTextEmphasis::FilledSesame;
  if (!open) return shape;
  switch (shape) {
    case CssTextEmphasis::FilledDoubleCircle:
      return CssTextEmphasis::OpenDoubleCircle;
    case CssTextEmphasis::FilledCircle:
      return CssTextEmphasis::OpenCircle;
    case CssTextEmphasis::FilledTriangle:
      return CssTextEmphasis::OpenTriangle;
    case CssTextEmphasis::FilledDot:
      return CssTextEmphasis::OpenDot;
    default:
      return CssTextEmphasis::OpenSesame;
  }
}

CssListStyleType CssParser::interpretListStyleType(std::string_view val) {
  // Whole-keyword comparison, not substring: a @counter-style name containing "circle" is not
  // list-style-type: circle, and the ordered families are matched by their real CSS names.
  CssListStyleType result = CssListStyleType::Disc;
  forEachDelimitedToken(val, isCssWhitespace, [&](const std::string_view token) {
    if (iequalsAscii(token, "none")) {
      result = CssListStyleType::NoMarker;
    } else if (iequalsAscii(token, "square")) {
      result = CssListStyleType::Square;
    } else if (iequalsAscii(token, "circle")) {
      result = CssListStyleType::Circle;
    } else if (iequalsAscii(token, "disc")) {
      result = CssListStyleType::Disc;
    } else {
      // Numbered and alphabetic/roman ordered types all render as decimal numbers.
      static constexpr const char* kOrdered[] = {"decimal",     "decimal-leading-zero", "lower-alpha", "upper-alpha",
                                                 "lower-latin", "upper-latin",          "lower-roman", "upper-roman",
                                                 "cjk-decimal", "japanese-informal"};
      for (const char* k : kOrdered) {
        if (iequalsAscii(token, k)) {
          result = CssListStyleType::Decimal;
          break;
        }
      }
    }
  });
  return result;
}

CssLength CssParser::interpretLength(std::string_view val) {
  CssLength result;
  tryInterpretLength(val, result);
  return result;
}

bool CssParser::tryInterpretLength(std::string_view val, CssLength& out) {
  val = trimCssWhitespace(val);
  if (val.empty()) {
    out = CssLength{};
    return false;
  }

  size_t unitStart = val.size();
  for (size_t i = 0; i < val.size(); ++i) {
    const char c = val[i];
    if (!std::isdigit(c) && c != '.' && c != '-' && c != '+') {
      unitStart = i;
      break;
    }
  }

  float numericValue;
  if (!tryParseNumber(val.substr(0, unitStart), numericValue)) {
    out = CssLength{};
    return false;  // No number parsed (e.g. auto, inherit, initial)
  }

  const std::string_view unitPart = val.substr(unitStart);
  auto unit = CssUnit::Pixels;
  if (iequalsAscii(unitPart, "em")) {
    unit = CssUnit::Em;
  } else if (iequalsAscii(unitPart, "rem")) {
    unit = CssUnit::Rem;
  } else if (iequalsAscii(unitPart, "pt")) {
    unit = CssUnit::Points;
  } else if (unitPart == "%") {
    unit = CssUnit::Percent;
  }

  out = CssLength{numericValue, unit};
  return true;
}

CssBorderLineStyle borderLineStyleFrom(const std::string_view value) {
  CssBorderLineStyle result = CssBorderLineStyle::Solid;
  forEachDelimitedToken(value, isCssWhitespace, [&result](const std::string_view token) {
    if (iequalsAscii(token, "dotted")) {
      result = CssBorderLineStyle::Dotted;
    } else if (iequalsAscii(token, "dashed")) {
      result = CssBorderLineStyle::Dashed;
    } else if (iequalsAscii(token, "double")) {
      result = CssBorderLineStyle::Double;
    }
  });
  return result;
}

uint8_t borderLineWidthFrom(const std::string_view value) {
  uint8_t result = 1;
  forEachDelimitedToken(value, isCssWhitespace, [&result](const std::string_view token) {
    size_t numericEnd = 0;
    while (numericEnd < token.size() && (std::isdigit(token[numericEnd]) || token[numericEnd] == '.' ||
                                         token[numericEnd] == '+' || token[numericEnd] == '-')) {
      ++numericEnd;
    }
    float width = 0.0f;
    if (numericEnd == 0 || !tryParseNumber(token.substr(0, numericEnd), width) || !(width > 0.0f)) return;
    const int rounded = static_cast<int>(std::lround(width));
    result = static_cast<uint8_t>(std::clamp(rounded, 1, 4));
  });
  return result;
}

// Declaration parsing

void CssParser::parseDeclarationIntoStyle(std::string_view decl, CssStyle& style, InkColors& ink) {
  const size_t colonPos = decl.find(':');
  if (colonPos == std::string_view::npos || colonPos == 0) return;

  const std::string_view name = trimCssWhitespace(decl.substr(0, colonPos));
  std::string_view value = trimCssWhitespace(decl.substr(colonPos + 1));

  if (name.empty() || value.empty()) return;

  value = stripTrailingImportant(value);

  if (iequalsAscii(name, "text-align")) {
    style.textAlign = interpretAlignment(value);
    style.defined.textAlign = 1;
  } else if (iequalsAscii(name, "font-style")) {
    style.fontStyle = interpretFontStyle(value);
    style.defined.fontStyle = 1;
  } else if (iequalsAscii(name, "font-weight")) {
    style.fontWeight = interpretFontWeight(value);
    style.defined.fontWeight = 1;
  } else if (iequalsAscii(name, "text-decoration") || iequalsAscii(name, "text-decoration-line")) {
    style.textDecoration = interpretDecoration(value);
    style.defined.textDecoration = 1;
  } else if (iequalsAscii(name, "text-indent")) {
    style.textIndent = interpretLength(value);
    style.defined.textIndent = 1;
  } else if (iequalsAscii(name, "text-emphasis-style") || iequalsAscii(name, "text-emphasis") ||
             iequalsAscii(name, "-epub-text-emphasis-style") || iequalsAscii(name, "-webkit-text-emphasis-style") ||
             iequalsAscii(name, "-epub-text-emphasis") || iequalsAscii(name, "-webkit-text-emphasis")) {
    style.textEmphasis = interpretTextEmphasis(value);
    style.defined.textEmphasis = 1;
  } else if (iequalsAscii(name, "font-size")) {
    // Stored as a raw length; the em base is the reader's own font size, resolved at layout
    // time (cssBlockFontId). Absolute units are normalised into em multiples of the CSS
    // initial size (16px / 12pt) so one scale factor covers every unit downstream.
    // stripTrailingImportant matters here: "4em !important" would otherwise leave "em
    // !important" as the unit and fall back to pixels.
    const std::string_view sizeValue = stripTrailingImportant(value);
    CssLength len;
    if (tryInterpretLength(sizeValue, len)) {
      style.fontSize = len;
      style.defined.fontSize = 1;
    } else {
      // Keywords. Ratios follow the CSS absolute-size scale; smaller/larger are the
      // relative-size steps applied to the parent (approximated as the reader size).
      float em = 0.0f;
      if (iequalsAscii(sizeValue, "xx-small")) {
        em = 0.6f;
      } else if (iequalsAscii(sizeValue, "x-small")) {
        em = 0.75f;
      } else if (iequalsAscii(sizeValue, "small") || iequalsAscii(sizeValue, "smaller")) {
        em = 0.83f;
      } else if (iequalsAscii(sizeValue, "medium")) {
        em = 1.0f;
      } else if (iequalsAscii(sizeValue, "large")) {
        em = 1.2f;
      } else if (iequalsAscii(sizeValue, "larger")) {
        em = 1.2f;
      } else if (iequalsAscii(sizeValue, "x-large")) {
        em = 1.5f;
      } else if (iequalsAscii(sizeValue, "xx-large")) {
        em = 2.0f;
      }
      if (em > 0.0f) {
        style.fontSize = CssLength{em, CssUnit::Em};
        style.defined.fontSize = 1;
      }
      // Anything else (inherit/initial/unset) leaves the property undefined so the
      // cascade keeps whatever an ancestor set.
    }
  } else if (iequalsAscii(name, "line-height")) {
    // Stored raw like font-size, and for the same reason: the em base is the BLOCK's own font
    // size, which is not known until font-size has been resolved to a font id at layout time.
    // `normal` (and inherit/initial/unset) parses as no length and leaves the property UNSET,
    // so the reader's own leading stands -- the correct answer, not a value to record.
    const std::string_view lhValue = stripTrailingImportant(value);
    CssLength len;
    if (tryInterpretLength(lhValue, len)) {
      // The unitless form is the one CSS itself prefers and the one books use most, but
      // tryInterpretLength has no unit to key on and defaults to Pixels -- which would read
      // `line-height: 1.4` as 1.4 PIXELS and collapse the block to a single band of ink.
      if (isUnitlessNumber(lhValue)) len.unit = CssUnit::Number;
      style.lineHeight = len;
      style.defined.lineHeight = 1;
    }
  } else if (iequalsAscii(name, "letter-spacing")) {
    // Tracking. Stored raw for the same reason line-height is: `0.05em` has to resolve against
    // the BLOCK's own font size. `normal` (and inherit/initial) parses as no length and leaves
    // the property UNSET, which renders identically to 0 but still stops the cascade.
    // A bare number is invalid CSS here (unlike line-height), so the Pixels fallback in
    // tryInterpretLength is the right reading of a unitless value and Number is never produced.
    const std::string_view spacingValue = stripTrailingImportant(value);
    CssLength len;
    if (iequalsAscii(spacingValue, "normal")) {
      // Explicit zero is defined so it cancels inherited tracking.
      style.letterSpacing = CssLength{};
      style.defined.letterSpacing = 1;
    } else if (tryInterpretLength(spacingValue, len)) {
      style.letterSpacing = len;
      style.defined.letterSpacing = 1;
    }
  } else if (iequalsAscii(name, "text-transform")) {
    const std::string_view ttValue = stripTrailingImportant(value);
    bool recognised = true;
    if (iequalsAscii(ttValue, "uppercase")) {
      style.setTextTransform(CssTextTransform::Uppercase);
    } else if (iequalsAscii(ttValue, "lowercase")) {
      style.setTextTransform(CssTextTransform::Lowercase);
    } else if (iequalsAscii(ttValue, "capitalize")) {
      style.setTextTransform(CssTextTransform::Capitalize);
    } else if (iequalsAscii(ttValue, "none")) {
      // Recorded, not ignored: an explicit `none` is how a book cancels an ancestor's uppercase.
      style.setTextTransform(CssTextTransform::None);
    } else {
      // full-width / full-size-kana (the CJK values) and inherit/initial/unset: leave the
      // property alone rather than silently rewriting the text in a way the book did not ask for.
      recognised = false;
    }
    if (recognised) style.defined.textTransform = 1;
  } else if (iequalsAscii(name, "hyphens") || iequalsAscii(name, "-moz-hyphens") ||
             iequalsAscii(name, "-webkit-hyphens") || iequalsAscii(name, "-ms-hyphens") ||
             iequalsAscii(name, "-epub-hyphens") || iequalsAscii(name, "adobe-hyphenate")) {
    // The book may only SUPPRESS hyphenation, never force it on: the user's global Hyphenation
    // setting stays the outer gate (see ParsedText::hyphenationActive). `auto`/`manual` are
    // therefore recorded as "not suppressed" -- which is what makes a child's `hyphens: auto`
    // cancel an ancestor's `hyphens: none` rather than inherit it.
    // Adobe's `adobe-hyphenate: none` is the EPUB2-era spelling of the same request.
    const std::string_view hValue = stripTrailingImportant(value);
    if (iequalsAscii(hValue, "none")) {
      style.setHyphensNone(true);
      style.defined.hyphens = 1;
    } else if (iequalsAscii(hValue, "auto") || iequalsAscii(hValue, "manual")) {
      style.setHyphensNone(false);
      style.defined.hyphens = 1;
    }
  } else if (iequalsAscii(name, "hyphenate-limit-chars") || iequalsAscii(name, "hyphenate-limit-lines") ||
             iequalsAscii(name, "hyphenate-limit-zone") || iequalsAscii(name, "hyphenate-limit-last") ||
             iequalsAscii(name, "hyphenate-limit-before") || iequalsAscii(name, "hyphenate-limit-after") ||
             iequalsAscii(name, "-moz-hyphenate-limit-chars") || iequalsAscii(name, "-moz-hyphenate-limit-lines") ||
             iequalsAscii(name, "-moz-hyphenate-limit-zone") || iequalsAscii(name, "-moz-hyphenate-limit-last") ||
             iequalsAscii(name, "-moz-hyphenate-limit-before") || iequalsAscii(name, "-moz-hyphenate-limit-after") ||
             iequalsAscii(name, "-webkit-hyphenate-limit-before") ||
             iequalsAscii(name, "-webkit-hyphenate-limit-after") ||
             iequalsAscii(name, "-webkit-hyphenate-limit-chars") ||
             iequalsAscii(name, "-webkit-hyphenate-limit-lines") ||
             iequalsAscii(name, "-webkit-hyphenate-limit-zone") || iequalsAscii(name, "-webkit-hyphenate-limit-last") ||
             iequalsAscii(name, "-ms-hyphenate-limit-chars") || iequalsAscii(name, "-ms-hyphenate-limit-lines") ||
             iequalsAscii(name, "-ms-hyphenate-limit-zone") || iequalsAscii(name, "-ms-hyphenate-limit-last") ||
             iequalsAscii(name, "-ms-hyphenate-limit-before") || iequalsAscii(name, "-ms-hyphenate-limit-after") ||
             iequalsAscii(name, "-epub-hyphenate-limit-chars") || iequalsAscii(name, "-epub-hyphenate-limit-lines") ||
             iequalsAscii(name, "-epub-hyphenate-limit-zone") || iequalsAscii(name, "-epub-hyphenate-limit-last") ||
             iequalsAscii(name, "-epub-hyphenate-limit-before") || iequalsAscii(name, "-epub-hyphenate-limit-after")) {
    // Accepted and ignored, deliberately. The hyphenator's break points come from the pattern
    // dictionary, not from a per-book budget, so there is nothing here to honour -- but naming
    // the family keeps 18 declarations out of the "unknown property" bucket, where they would
    // otherwise be counted as a gap that still needs closing.
  } else if (iequalsAscii(name, "page-break-before") || iequalsAscii(name, "break-before") ||
             iequalsAscii(name, "page-break-after") || iequalsAscii(name, "break-after") ||
             iequalsAscii(name, "page-break-inside") || iequalsAscii(name, "break-inside")) {
    // Both spellings, one code path: EPUB3 books ship the CSS3 break-* names, EPUB2 books the
    // page-break-* ones, and plenty of trade stylesheets carry both for the same element.
    const CssPageBreak resolved = interpretPageBreak(stripTrailingImportant(value));
    if (resolved != CssPageBreak::Auto) {
      const CssPageBreakSlot slot =
          (iequalsAscii(name, "page-break-inside") || iequalsAscii(name, "break-inside")) ? CssPageBreakSlot::Inside
          : (iequalsAscii(name, "page-break-after") || iequalsAscii(name, "break-after")) ? CssPageBreakSlot::After
                                                                                          : CssPageBreakSlot::Before;
      style.setPageBreak(slot, resolved);
      style.defined.pageBreak = 1;
    }
  } else if (iequalsAscii(name, "font-variant") || iequalsAscii(name, "font-variant-caps")) {
    // Only small-caps matters for rendering; anything else resets to normal.
    style.fontVariant =
        (value.find("small-caps") != std::string_view::npos) ? CssFontVariant::SmallCaps : CssFontVariant::Normal;
    style.defined.fontVariant = 1;
  } else if (iequalsAscii(name, "list-style-type") || iequalsAscii(name, "list-style")) {
    style.listStyleType = interpretListStyleType(value);
    style.defined.listStyleType = 1;
  } else if (iequalsAscii(name, "color")) {
    // Only the luma is kept, and only until the rule block closes -- see InkColors and
    // resolveInkMode(). An unparseable colour records nothing, so the cascade keeps whatever an
    // ancestor set instead of this rule asserting a polarity it could not derive.
    uint8_t luma = 0;
    if (tryParseColorLuma(value, luma)) ink.textLuma = luma;
  } else if (iequalsAscii(name, "background-color")) {
    uint8_t luma = 0;
    if (tryParseColorLuma(value, luma)) ink.bgLuma = luma;
  } else if (iequalsAscii(name, "background")) {
    uint8_t luma = 0;
    if (tryParseBackgroundShorthandLuma(value, luma)) ink.bgLuma = luma;
  } else if (iequalsAscii(name, "margin-top")) {
    style.marginTop = interpretLength(value);
    style.defined.marginTop = 1;
  } else if (iequalsAscii(name, "margin-bottom")) {
    style.marginBottom = interpretLength(value);
    style.defined.marginBottom = 1;
  } else if (iequalsAscii(name, "margin-left")) {
    style.marginLeft = interpretLength(value);
    style.defined.marginLeft = 1;
  } else if (iequalsAscii(name, "margin-right")) {
    style.marginRight = interpretLength(value);
    style.defined.marginRight = 1;
  } else if (iequalsAscii(name, "margin")) {
    std::string_view margins[4];
    const size_t count = collectEdgeValueTokens(value, margins);
    if (count > 0) {
      style.marginTop = interpretLength(margins[0]);
      style.marginRight = count >= 2 ? interpretLength(margins[1]) : style.marginTop;
      style.marginBottom = count >= 3 ? interpretLength(margins[2]) : style.marginTop;
      style.marginLeft = count >= 4 ? interpretLength(margins[3]) : style.marginRight;
      style.defined.marginTop = style.defined.marginRight = style.defined.marginBottom = style.defined.marginLeft = 1;
    }
  } else if (iequalsAscii(name, "border-style")) {
    // Per-edge mask following the CSS 1/2/3/4-value edge rule (top, right, bottom, left).
    // A full mask is a boxed/kakomi block; a partial mask (e.g. "solid none none none",
    // EBPAJ .k-solid-top) is a separator rule on that edge.
    std::string_view sides[4];
    const size_t count = collectEdgeValueTokens(value, sides);
    uint8_t edges = 0;
    if (count > 0) {
      auto styled = [&](const std::string_view v) { return !iequalsAscii(v, "none") && !iequalsAscii(v, "hidden"); };
      const std::string_view top = sides[0];
      const std::string_view right = count >= 2 ? sides[1] : sides[0];
      const std::string_view bottom = count >= 3 ? sides[2] : sides[0];
      const std::string_view left = count >= 4 ? sides[3] : right;
      if (styled(top)) edges |= CssStyle::BORDER_TOP;
      if (styled(right)) edges |= CssStyle::BORDER_RIGHT;
      if (styled(bottom)) edges |= CssStyle::BORDER_BOTTOM;
      if (styled(left)) edges |= CssStyle::BORDER_LEFT;
    }
    style.setBorderSpec(edges, borderLineStyleFrom(value), 1);
    style.defined.border = 1;
  } else if (iequalsAscii(name, "border")) {
    // Shorthand ("1px solid #000" / "none"): a stroke-style keyword means all four sides.
    const bool styled =
        value.find("solid") != std::string_view::npos || value.find("double") != std::string_view::npos ||
        value.find("dashed") != std::string_view::npos || value.find("dotted") != std::string_view::npos;
    style.setBorderSpec(styled ? CssStyle::BORDER_ALL : 0, borderLineStyleFrom(value), borderLineWidthFrom(value));
    style.defined.border = 1;
  } else if (iequalsAscii(name, "border-top") || iequalsAscii(name, "border-bottom") ||
             iequalsAscii(name, "border-left") || iequalsAscii(name, "border-right")) {
    // Per-side shorthands ("border-bottom: 1px solid #333"): books use these as
    // separator rules under headings and table rows. Merge into the edge mask
    // (|=/&~) so several per-side declarations in one rule compose instead of
    // overwriting each other.
    const bool styled =
        value.find("solid") != std::string_view::npos || value.find("double") != std::string_view::npos ||
        value.find("dashed") != std::string_view::npos || value.find("dotted") != std::string_view::npos;
    uint8_t bit = CssStyle::BORDER_TOP;
    if (iequalsAscii(name, "border-bottom")) {
      bit = CssStyle::BORDER_BOTTOM;
    } else if (iequalsAscii(name, "border-left")) {
      bit = CssStyle::BORDER_LEFT;
    } else if (iequalsAscii(name, "border-right")) {
      bit = CssStyle::BORDER_RIGHT;
    }
    uint8_t edges = style.borderEdgeMask();
    edges = styled ? static_cast<uint8_t>(edges | bit) : static_cast<uint8_t>(edges & ~bit);
    if (styled) {
      style.setBorderSpec(edges, borderLineStyleFrom(value), borderLineWidthFrom(value));
    } else {
      style.setBorderEdgeMask(edges);
    }
    style.defined.border = 1;
  } else if (iequalsAscii(name, "font")) {
    // font shorthand ("italic small-caps bold 1em/1.4 serif"): only the style, variant and
    // weight keywords render here -- size and family are the reader's settings. Matching whole
    // tokens rather than substrings also stops a family name like "BoldFace" from registering
    // as bold.
    forEachDelimitedToken(value, isCssWhitespace, [&style](const std::string_view token) {
      if (iequalsAscii(token, "italic") || iequalsAscii(token, "oblique")) {
        style.fontStyle = CssFontStyle::Italic;
        style.defined.fontStyle = 1;
      } else if (iequalsAscii(token, "bold")) {
        style.fontWeight = CssFontWeight::Bold;
        style.defined.fontWeight = 1;
      } else if (iequalsAscii(token, "small-caps")) {
        style.fontVariant = CssFontVariant::SmallCaps;
        style.defined.fontVariant = 1;
      }
    });
  } else if (iequalsAscii(name, "padding-top")) {
    style.paddingTop = interpretLength(value);
    style.defined.paddingTop = 1;
  } else if (iequalsAscii(name, "padding-bottom")) {
    style.paddingBottom = interpretLength(value);
    style.defined.paddingBottom = 1;
  } else if (iequalsAscii(name, "padding-left")) {
    style.paddingLeft = interpretLength(value);
    style.defined.paddingLeft = 1;
  } else if (iequalsAscii(name, "padding-right")) {
    style.paddingRight = interpretLength(value);
    style.defined.paddingRight = 1;
  } else if (iequalsAscii(name, "padding")) {
    std::string_view paddings[4];
    const size_t count = collectEdgeValueTokens(value, paddings);
    if (count > 0) {
      style.paddingTop = interpretLength(paddings[0]);
      style.paddingRight = count >= 2 ? interpretLength(paddings[1]) : style.paddingTop;
      style.paddingBottom = count >= 3 ? interpretLength(paddings[2]) : style.paddingTop;
      style.paddingLeft = count >= 4 ? interpretLength(paddings[3]) : style.paddingRight;
      style.defined.paddingTop = style.defined.paddingRight = style.defined.paddingBottom = style.defined.paddingLeft =
          1;
    }
  } else if (iequalsAscii(name, "height")) {
    CssLength len;
    if (tryInterpretLength(value, len)) {
      style.imageHeight = len;
      style.defined.imageHeight = 1;
    }
  } else if (iequalsAscii(name, "width") || iequalsAscii(name, "max-width")) {
    CssLength len;
    if (tryInterpretLength(value, len)) {
      style.imageWidth = len;
      style.defined.imageWidth = 1;
    }
  } else if (iequalsAscii(name, "display")) {
    const std::string_view displayValue = stripTrailingImportant(value);
    if (iequalsAscii(displayValue, "none")) {
      style.display = CssDisplay::None;
    } else if (iequalsAscii(displayValue, "inline-block")) {
      style.display = CssDisplay::InlineBlock;
    } else {
      style.display = CssDisplay::Block;
    }
    style.defined.display = 1;
  } else if (iequalsAscii(name, "direction")) {
    if (iequalsAscii(value, "rtl")) {
      style.direction = CssTextDirection::Rtl;
      style.defined.direction = 1;
    } else if (iequalsAscii(value, "ltr")) {
      style.direction = CssTextDirection::Ltr;
      style.defined.direction = 1;
    }
  } else if (iequalsAscii(name, "vertical-align")) {
    if (iequalsAscii(value, "super")) {
      style.verticalAlign = CssVerticalAlign::Super;
      style.defined.verticalAlign = 1;
    } else if (iequalsAscii(value, "sub")) {
      style.verticalAlign = CssVerticalAlign::Sub;
      style.defined.verticalAlign = 1;
    } else {
      // Numeric offsets: publishers commonly write footnote references as
      // ".apnb { vertical-align: 70%; font-size: 60% }" instead of the super/sub keywords.
      // Any positive raise reads as superscript, any negative as subscript; keyword values
      // like baseline/middle fail tryInterpretLength and stay ignored.
      CssLength len;
      if (tryInterpretLength(stripTrailingImportant(value), len) && len.value != 0.0f) {
        style.verticalAlign = len.value > 0 ? CssVerticalAlign::Super : CssVerticalAlign::Sub;
        style.defined.verticalAlign = 1;
      }
    }
  }
}

// Minimum luma separation before a rule counts as light-on-dark. 64 is a quarter of the 0..255
// range: high enough that a subtle tint stays Normal (white on #ddd is a delta of 34), low enough
// that the panels books actually draw do invert (the h1 trap's #fff on #a7a9ac is 87).
//
// The asymmetry is deliberate. Normal draws black text on the untouched page, which is legible
// whatever the book intended, so a missed inversion costs only fidelity. Only Inverted can be
// wrong in a way that hurts -- it commits a whole block to a black panel.
constexpr int16_t INK_INVERT_MIN_LUMA_DELTA = 64;

void CssParser::resolveInkMode(CssStyle& style, const InkColors& ink) {
  // Said nothing about colour: leave the property unset so an ancestor's polarity still applies.
  if (!ink.any()) return;

  // The unspecified side falls back to the page's own polarity: black text on a white page. That
  // is what makes a lone `color:#fff` come out Normal (delta 0, drawn black and therefore
  // visible) and a lone dark `background-color` come out Normal too -- inverting there would put
  // the book's default BLACK text on a black panel.
  const int16_t textLuma = ink.textLuma != InkColors::UNSET ? ink.textLuma : 0;
  const int16_t bgLuma = ink.bgLuma != InkColors::UNSET ? ink.bgLuma : 255;
  style.inkMode = (textLuma - bgLuma >= INK_INVERT_MIN_LUMA_DELTA) ? CssInkMode::Inverted : CssInkMode::Normal;
  style.defined.inkMode = 1;
}

CssStyle CssParser::parseDeclarations(std::string_view declBlock) {
  CssStyle style;
  InkColors ink;

  size_t start = 0;
  for (size_t i = 0; i <= declBlock.size(); ++i) {
    if (i == declBlock.size() || declBlock[i] == ';') {
      if (i > start) {
        parseDeclarationIntoStyle(declBlock.substr(start, i - start), style, ink);
      }
      start = i + 1;
    }
  }
  resolveInkMode(style, ink);

  return style;
}

// Rule processing

void CssParser::processRuleBlockWithStyle(std::string_view selectorGroup, const CssStyle& style) {
  // Skip rules that don't define any supported properties to save RAM.
  if (!style.defined.anySet()) {
    return;
  }

  // Walk comma-separated selectors in place. The bounded store reports every
  // capacity or allocation failure without crossing a throwing STL boundary.
  forEachDelimitedToken(
      selectorGroup, [](char c) { return c == ','; },
      [&](std::string_view sel) {
        if (cacheAppendActive_ && entryCount_ >= 200) {
          if (!appendRulesToCache()) {
            ruleGrowthStopped_ = true;
            return;
          }
          clear();
        }
        if (sel.size() > MAX_SELECTOR_LENGTH) {
          LOG_DBG("CSS", "Selector too long (%zu > %zu), skipping", sel.size(), MAX_SELECTOR_LENGTH);
          return;
        }

        // Normalize the selector into the key it is stored under, or drop it.
        // CssSelector::parse accepts `tag`, `.class`, `tag.class`, and two of those joined by
        // a descendant or child combinator; everything else (pseudo, attribute, id, sibling,
        // and three-compound selectors) is rejected -- see CssSelector.h.
        CssSelector::Selector parsed;
        if (!CssSelector::parse(sel, parsed)) {
          return;  // unsupported selector syntax
        }

        // The EBPAJ template's writing-mode scoping keeps its own key space and takes
        // precedence over ordinary descendant storage: `.hltr X` / `.vrtl X` (the body carries
        // class hltr or vrtl) carry the entire h/v split of the standard Japanese template
        // (margins, indents, rules), and are stored under a scope-prefixed key ("h|X" / "v|X";
        // '|' can never appear in a real selector) that only scope-aware lookups
        // (resolveStyle's "h|", the vertical collector's "v|") ever find. Storing `.vrtl X` as
        // an ordinary descendant rule instead would let the HORIZONTAL engine apply
        // vertical-only styling to a Japanese book -- the one mis-match this whole area has to
        // avoid. `.hltr > X` stays dropped exactly as it was before compound support: routing
        // it through the scope would widen it to every X, and routing it through the generic
        // path would need the `.vrtl` twin to behave differently from its sibling spelling.
        const bool scopedSelector =
            parsed.combinator == CssSelector::DESCENDANT_COMBINATOR && parsed.ancestor.tag.empty() &&
            (iequalsAscii(parsed.ancestor.cls, "hltr") || iequalsAscii(parsed.ancestor.cls, "vrtl"));
        char builtKey[MAX_SELECTOR_LENGTH];
        size_t keyLength = 0;
        const auto appendPiece = [&](const std::string_view piece) {
          if (keyLength + piece.size() <= sizeof(builtKey)) {
            memcpy(builtKey + keyLength, piece.data(), piece.size());
            keyLength += piece.size();
          }
        };
        if (scopedSelector) {
          appendPiece(iequalsAscii(parsed.ancestor.cls, "hltr") ? "h|" : "v|");
          const CssSelector::Selector subjectOnly{{}, parsed.subject, 0};
          CssSelector::forEachKeyPiece(subjectOnly, appendPiece);
          sel = {builtKey, keyLength};
        } else if (parsed.combinator != 0) {
          CssSelector::forEachKeyPiece(parsed, appendPiece);
          sel = {builtKey, keyLength};
        }

        if (ruleGrowthStopped_) {
          // Continue the cascade for stored selectors without retrying failed
          // allocations for new rules.
          bool exact = false;
          const size_t matchingIndex = lowerBound(sel, {}, {}, exact);
          if (!exact || matchingIndex >= entryCount_) return;
        }
        const RuleInsertResult result = insertOrMerge(sel, style);
        if (result == RuleInsertResult::Limit) {
          LOG_ERR("CSS", "CSS rule store limit reached at %u rules", entryCount_);
          ruleGrowthStopped_ = true;
        } else if (result == RuleInsertResult::OutOfMemory) {
          heapTruncated_ = true;
          LOG_ERR("CSS", "OOM while growing CSS rule store at %u rules", entryCount_);
          ruleGrowthStopped_ = true;
        }
        noteCombinatorsIn(sel);
      });
}

void CssParser::noteCombinatorsIn(const std::string_view key) {
  if (key.find(CssSelector::DESCENDANT_COMBINATOR) != std::string_view::npos) hasDescendantRules_ = true;
  if (key.find(CssSelector::CHILD_COMBINATOR) != std::string_view::npos) hasChildRules_ = true;
}

// Main parsing entry point

CssParser::ParseResult CssParser::loadFromStream(HalFile& source) {
  if (!source) {
    LOG_ERR("CSS", "Cannot read from invalid file");
    return ParseResult::Error;
  }

  size_t totalRead = 0;

  // Use stack-allocated buffers for parsing to avoid heap reallocations
  StackBuffer selector;
  StackBuffer declBuffer;

  bool inComment = false;
  bool maybeSlash = false;
  bool prevStar = false;

  bool inAtRule = false;
  int atDepth = 0;

  int bodyDepth = 0;
  bool skippingRule = false;
  bool selectorTruncated = false;
  bool declarationTruncated = false;
  bool inputTruncated = false;
  CssStyle currentStyle;
  // Scoped to the rule block being read, exactly like currentStyle: colours are only comparable
  // against each other within one block, and nothing about them outlives resolveInkMode().
  InkColors currentInk;

  auto handleChar = [&](const char c) {
    if (inAtRule) {
      if (c == '{') {
        ++atDepth;
      } else if (c == '}') {
        if (atDepth > 0) --atDepth;
        if (atDepth == 0) inAtRule = false;
      } else if (c == ';' && atDepth == 0) {
        inAtRule = false;
      }
      return;
    }

    if (bodyDepth == 0) {
      if (selector.empty() && isCssWhitespace(c)) {
        return;
      }
      if (c == '@' && selector.empty()) {
        inAtRule = true;
        atDepth = 0;
        return;
      }
      if (c == '{') {
        bodyDepth = 1;
        currentStyle = CssStyle{};
        currentInk = InkColors{};
        declBuffer.clear();
        skippingRule = selectorTruncated || selector.size() > MAX_SELECTOR_LENGTH * 4;
        return;
      }
      if (!selector.push_back(c)) {
        selectorTruncated = true;
        inputTruncated = true;
      }
      return;
    }

    // bodyDepth > 0
    if (c == '{') {
      ++bodyDepth;
      return;
    }
    if (c == '}') {
      --bodyDepth;
      if (bodyDepth == 0) {
        if (!skippingRule && !declarationTruncated && !declBuffer.empty()) {
          parseDeclarationIntoStyle(declBuffer, currentStyle, currentInk);
        }
        if (!skippingRule) {
          resolveInkMode(currentStyle, currentInk);
          processRuleBlockWithStyle(selector, currentStyle);
        }
        selector.clear();
        declBuffer.clear();
        skippingRule = false;
        selectorTruncated = false;
        declarationTruncated = false;
        return;
      }
      return;
    }
    if (bodyDepth > 1) {
      return;
    }
    if (!skippingRule) {
      if (c == ';') {
        if (!declarationTruncated && !declBuffer.empty()) {
          parseDeclarationIntoStyle(declBuffer, currentStyle, currentInk);
        }
        declBuffer.clear();
        declarationTruncated = false;
      } else {
        if (!declBuffer.push_back(c)) {
          declarationTruncated = true;
          inputTruncated = true;
        }
      }
    }
  };

  char buffer[READ_BUFFER_SIZE];
  while (source.available()) {
    int bytesRead = source.read(buffer, sizeof(buffer));
    if (bytesRead <= 0) break;

    totalRead += static_cast<size_t>(bytesRead);

    for (int i = 0; i < bytesRead; ++i) {
      const char c = buffer[i];

      if (inComment) {
        if (prevStar && c == '/') {
          inComment = false;
          prevStar = false;
          continue;
        }
        prevStar = c == '*';
        continue;
      }

      if (maybeSlash) {
        if (c == '*') {
          inComment = true;
          maybeSlash = false;
          prevStar = false;
          continue;
        }
        handleChar('/');
        maybeSlash = false;
        // fall through to process current char
      }

      if (c == '/') {
        maybeSlash = true;
        continue;
      }

      handleChar(c);
    }
  }

  if (maybeSlash) {
    handleChar('/');
  }

  if (inputTruncated) {
    LOG_ERR("CSS", "CSS input exceeded parser buffer; cache will remain partial");
  }
  const bool incompleteInput = bodyDepth > 0 || inAtRule || inComment || !selector.empty();
  LOG_DBG("CSS", "Parsed %zu rules from %zu bytes", ruleCount(), totalRead);
  return ruleGrowthStopped_ || inputTruncated || incompleteInput ? ParseResult::Partial : ParseResult::Complete;
}

// Style resolution

CssStyle CssParser::resolveStyle(const std::string_view tagName, const std::string_view classAttr,
                                 const CssElementPath* path) const {
  // Matching rules are collected first and applied in ascending specificity afterwards,
  // because the enumeration order is not the cascade order once compound selectors exist:
  // `div p` (1+1) must apply before `.note` (16), which is found later, and `.callout p`
  // (16+1) ties with `p.note` (1+16). Storage is a fixed stack array: an element matching more
  // than MAX_CANDIDATES rules has never been observed, and the overflow path drops the
  // LOWEST-specificity match, which is the one the rest would have overridden anyway.
  //
  // Deviation from the real cascade: CSS breaks a specificity tie by document order, which
  // this rule table does not preserve (the store is sorted, and the cache is written in selector
  // order and merged per selector on load). A tie therefore resolves to the last match in
  // enumeration order -- simple before compound, outer ancestor before inner, descendant
  // before child -- i.e. the more narrowly scoped rule wins. Same-selector rules from
  // anywhere in the stylesheet(s) are still merged in source order by applyOver at parse and
  // cache-load time, so the common `p {...} ... p {...}` case is unaffected.
  struct Candidate {
    const CssStyle* style;
    uint8_t spec;
  };
  constexpr size_t MAX_CANDIDATES = 16;
  Candidate candidates[MAX_CANDIDATES];
  size_t candidateCount = 0;

  const auto addCandidate = [&](const CssStyle* style, const uint8_t spec) {
    if (candidateCount < MAX_CANDIDATES) {
      candidates[candidateCount++] = Candidate{style, spec};
      return;
    }
    size_t weakest = 0;
    for (size_t i = 1; i < candidateCount; ++i) {
      if (candidates[i].spec < candidates[weakest].spec) weakest = i;
    }
    if (candidates[weakest].spec >= spec) return;  // nothing to gain by swapping
    for (size_t i = weakest; i + 1 < candidateCount; ++i) candidates[i] = candidates[i + 1];
    candidates[candidateCount - 1] = Candidate{style, spec};
  };

  // A simple selector also has a "h|"-scoped twin (the EBPAJ template's `.hltr X`
  // horizontal-mode variant), which applies over its unscoped form at the same specificity:
  // this resolver feeds the HORIZONTAL layout engine, which renders exactly what those rules
  // describe. The vertical engine reads the "v|" scope through collectVerticalStyles().
  const auto emit = [&](const std::string_view* pieces, const size_t count, const uint8_t spec, const bool simple) {
    if (const auto* style = findStyle({pieces, count})) {
      addCandidate(style, spec);
    }
    if (!simple) return;
    std::string_view scoped[CssSelector::MAX_KEY_PIECES + 1];
    scoped[0] = "h|";
    for (size_t i = 0; i < count; ++i) scoped[i + 1] = pieces[i];
    if (const auto* style = findStyle({scoped, count + 1})) {
      addCandidate(style, spec);
    }
  };

  // The ancestor walk is skipped entirely unless the table actually holds a compound rule,
  // so a book without them does exactly the lookups it did before compound support existed.
  const bool walkAncestors = path != nullptr && (hasDescendantRules_ || hasChildRules_);
  const size_t ancestorCount = walkAncestors ? path->ancestorCount() : 0;
  CssSelector::forEachCandidate(
      CssSelector::ElementRef{tagName, classAttr}, [path](const size_t i) { return path->ancestor(i); }, ancestorCount,
      walkAncestors && path->parentRecorded(), hasDescendantRules_, hasChildRules_, emit);

  // Stable insertion sort, ascending specificity: equal specificities keep emission order.
  for (size_t i = 1; i < candidateCount; ++i) {
    const Candidate v = candidates[i];
    size_t j = i;
    while (j > 0 && candidates[j - 1].spec > v.spec) {
      candidates[j] = candidates[j - 1];
      --j;
    }
    candidates[j] = v;
  }

  CssStyle result;
  for (size_t i = 0; i < candidateCount; ++i) result.applyOver(*candidates[i].style);
  return result;
}

// Inline style parsing (static - doesn't need rule database)

CssStyle CssParser::parseInlineStyle(std::string_view styleValue) { return parseDeclarations(styleValue); }

// Cache file name (version is CssParser::CSS_CACHE_VERSION)
constexpr char rulesCache[] = "/css_rules.cache";
constexpr char rulesCacheTmp[] = "/css_rules.cache.tmp";
constexpr char rulesCacheBackup[] = "/css_rules.cache.bak";
constexpr uint8_t CSS_CACHE_FLAG_PARTIAL = 1 << 0;
constexpr uint8_t CSS_CACHE_KNOWN_FLAGS = CSS_CACHE_FLAG_PARTIAL;

bool CssParser::hasCache() const { return Storage.exists((cachePath + rulesCache).c_str()); }

bool CssParser::restoreCacheBackupIfNeeded() const {
  if (cachePath.empty()) {
    return false;
  }

  const std::string finalPath = cachePath + rulesCache;
  if (Storage.exists(finalPath.c_str())) {
    return true;
  }

  const std::string backupPath = cachePath + rulesCacheBackup;
  if (!Storage.exists(backupPath.c_str())) {
    return false;
  }

  if (!Storage.rename(backupPath.c_str(), finalPath.c_str())) {
    LOG_ERR("CSS", "Failed to restore CSS cache backup");
    return false;
  }

  LOG_DBG("CSS", "Restored CSS cache backup after interrupted replacement");
  return true;
}

void CssParser::deleteCache() const {
  if (hasCache()) Storage.remove((cachePath + rulesCache).c_str());
  Storage.remove((cachePath + rulesCacheTmp).c_str());
  Storage.remove((cachePath + rulesCacheBackup).c_str());
}

CssParser::CacheStatus CssParser::inspectCache() const {
  if (cachePath.empty() || (!hasCache() && !restoreCacheBackupIfNeeded())) {
    return CacheStatus::Missing;
  }

  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
    return CacheStatus::Invalid;
  }

  uint8_t version = 0;
  uint8_t flags = 0;
  uint16_t ruleCount = 0;
  if (file.read(&version, sizeof(version)) != sizeof(version) || version != CSS_CACHE_VERSION ||
      file.read(&flags, sizeof(flags)) != sizeof(flags) || (flags & ~CSS_CACHE_KNOWN_FLAGS) != 0 ||
      file.read(&ruleCount, sizeof(ruleCount)) != sizeof(ruleCount) || ruleCount > MAX_CACHED_RULES) {
    return CacheStatus::Invalid;
  }

  const bool partial = (flags & CSS_CACHE_FLAG_PARTIAL) != 0;
  const auto skipBytes = [&file](const size_t byteCount) {
    return static_cast<size_t>(file.available()) >= byteCount && file.seekCur(byteCount);
  };
  for (uint16_t i = 0; i < ruleCount; ++i) {
    uint16_t selectorLen = 0;
    if (file.read(&selectorLen, sizeof(selectorLen)) != sizeof(selectorLen) || selectorLen == 0 ||
        selectorLen > MAX_SELECTOR_LENGTH) {
      return CacheStatus::Invalid;
    }
    uint8_t wire[STYLE_WIRE_BYTES];
    CssStyle style;
    if (!skipBytes(selectorLen) || file.read(wire, sizeof(wire)) != sizeof(wire) || !decodeStyleWire(wire, style)) {
      return CacheStatus::Invalid;
    }
  }

  if (file.available() != 0) {
    return CacheStatus::Invalid;
  }

  return partial ? CacheStatus::Partial : CacheStatus::Complete;
}

bool CssParser::saveToCache(const bool complete) const {
  if (cachePath.empty()) {
    return false;
  }

  const std::string finalPath = cachePath + rulesCache;
  const std::string tmpPath = cachePath + rulesCacheTmp;
  const std::string backupPath = cachePath + rulesCacheBackup;

  Storage.remove(tmpPath.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("CSS", tmpPath, file)) {
    return false;
  }

  bool writeOk = true;
  const auto writeBytes = [&file, &writeOk](const void* data, const size_t size) {
    if (writeOk && size > 0 && file.write(data, size) != size) {
      writeOk = false;
    }
  };
  const auto writeByte = [&writeBytes](const uint8_t value) { writeBytes(&value, sizeof(value)); };

  writeByte(CssParser::CSS_CACHE_VERSION);

  // A partial cache can style the current low-memory session, but the next
  // EPUB load must retry the source stylesheets instead of trusting it.
  writeByte(complete ? 0 : CSS_CACHE_FLAG_PARTIAL);

  // Write rule count
  const uint16_t ruleCount = entryCount_;
  writeBytes(&ruleCount, sizeof(ruleCount));

  // Write each rule: selector string + CssStyle fields
  for (uint16_t i = 0; i < entryCount_; ++i) {
    const std::string_view selector = selectorAt(i);
    // Write selector string (length-prefixed)
    const auto selectorLen = static_cast<uint16_t>(selector.size());
    writeBytes(&selectorLen, sizeof(selectorLen));
    writeBytes(selector.data(), selectorLen);

    uint8_t styleWire[STYLE_WIRE_BYTES];
    encodeStyleWire(stylePool_[entries_[i].styleIndex], styleWire);
    writeBytes(styleWire, sizeof(styleWire));
    if (!writeOk) break;
  }

  if (!writeOk || !file.close()) {
    LOG_ERR("CSS", "Failed to write temporary CSS cache");
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (!promoteCache()) return false;

  LOG_DBG("CSS", "Saved %u rules to %s cache", ruleCount, complete ? "complete" : "partial");
  return true;
}

CssParser::CacheLoadResult CssParser::loadFromCache(const std::vector<std::string>* usedClasses) {
  cacheLoadFailedForHeap_ = false;
  if (cachePath.empty()) {
    return CacheLoadResult::Invalid;
  }

  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
    if (!restoreCacheBackupIfNeeded() || !Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
      return CacheLoadResult::Invalid;
    }
  }

  // Clear existing rules
  clear();

  // Read and verify version
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != CssParser::CSS_CACHE_VERSION) {
    LOG_DBG("CSS", "Cache version mismatch (got %u, expected %u), removing stale cache for rebuild", version,
            CssParser::CSS_CACHE_VERSION);
    // Explicitly close() file before calling Storage.remove()
    file.close();
    Storage.remove((cachePath + rulesCache).c_str());
    return CacheLoadResult::Invalid;
  }

  uint8_t flags = 0;
  if (file.read(&flags, sizeof(flags)) != sizeof(flags) || (flags & ~CSS_CACHE_KNOWN_FLAGS) != 0) {
    LOG_DBG("CSS", "Invalid CSS cache flags: %u", flags);
    return CacheLoadResult::Invalid;
  }

  // Read rule count
  uint16_t ruleCount = 0;
  if (file.read(&ruleCount, sizeof(ruleCount)) != sizeof(ruleCount)) {
    return CacheLoadResult::Invalid;
  }

  if (ruleCount > MAX_CACHED_RULES) {
    LOG_DBG("CSS", "Invalid cache rule count (%u > %zu)", ruleCount, MAX_RULES);
    clear();
    return CacheLoadResult::Invalid;
  }

  auto selectorBuffer = ruleCount > 0 ? makeUniqueNoThrow<char[]>(MAX_SELECTOR_LENGTH) : nullptr;
  if (ruleCount > 0 && !selectorBuffer) {
    clear();
    cacheLoadFailedForHeap_ = true;
    return CacheLoadResult::LowMemory;
  }

  // Read each rule
  for (uint16_t i = 0; i < ruleCount; ++i) {
    // Read selector string
    uint16_t selectorLen = 0;
    if (file.read(&selectorLen, sizeof(selectorLen)) != sizeof(selectorLen)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    if (selectorLen == 0 || selectorLen > MAX_SELECTOR_LENGTH) {
      LOG_DBG("CSS", "Invalid selector length in cache: %u", selectorLen);
      clear();
      return CacheLoadResult::Invalid;
    }

    if (file.read(selectorBuffer.get(), selectorLen) != selectorLen) {
      clear();
      return CacheLoadResult::Invalid;
    }

    uint8_t styleWire[STYLE_WIRE_BYTES];
    if (file.read(styleWire, sizeof(styleWire)) != sizeof(styleWire)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    CssStyle style;
    if (!decodeStyleWire(styleWire, style)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    const std::string_view selector(selectorBuffer.get(), selectorLen);
    // Vertical-scoped rules ("v|...") are consumed exclusively through the streaming
    // collectVerticalStyles() -- loadFromCache feeds the HORIZONTAL layout engine only.
    // Materializing them here grew the resident map by hundreds of EBPAJ rules and pushed the
    // in-session section build over its heap budget (observed: every rebuild aborting with
    // "CSS cache didn't fit in heap").
    if (selector.size() >= 2 && selector[0] == 'v' && selector[1] == '|') continue;

    // Chapter-usage filter: skip class selectors the chapter never references (see header doc).
    // A compound selector names a class on either side and can only match if EVERY one of them
    // is present, so each is checked -- taking the whole tail after the first '.' (as this did
    // before compound keys existed) would test the nonexistent class "callout p" for
    // ".callout p" and drop every descendant rule the chapter needs.
    if (usedClasses != nullptr) {
      std::string_view sel(selector);
      if (sel.size() >= 2 && sel[0] == 'h' && sel[1] == '|') sel.remove_prefix(2);
      bool allClassesUsed = true;
      while (!sel.empty()) {
        const size_t end = sel.find_first_of(" >");
        const std::string_view compound = sel.substr(0, end);
        if (const size_t dot = compound.find('.'); dot != std::string_view::npos) {
          const std::string_view cls = compound.substr(dot + 1);
          bool used = false;
          for (const auto& u : *usedClasses) {
            if (u.size() == cls.size() && std::equal(u.begin(), u.end(), cls.begin(), [](char a, char b) {
                  return asciiToLower(a) == asciiToLower(b);
                })) {
              used = true;
              break;
            }
          }
          if (!used) {
            allClassesUsed = false;
            break;
          }
        }
        if (end == std::string_view::npos) break;
        sel.remove_prefix(end + 1);
      }
      if (!allClassesUsed) continue;
    }

    const RuleInsertResult insertResult = insertOrMerge(std::string_view(selectorBuffer.get(), selectorLen), style);
    if (insertResult == RuleInsertResult::OutOfMemory) {
      clear();
      cacheLoadFailedForHeap_ = true;
      return CacheLoadResult::LowMemory;
    }
    if (insertResult == RuleInsertResult::Limit) {
      clear();
      cacheLoadFailedForHeap_ = true;
      return CacheLoadResult::LowMemory;
    }
    noteCombinatorsIn(selector);
  }

  if (file.available() != 0) {
    clear();
    return CacheLoadResult::Invalid;
  }

  const bool partial = (flags & CSS_CACHE_FLAG_PARTIAL) != 0;
  LOG_DBG("CSS", "Loaded %u rules from %s cache", ruleCount, partial ? "partial" : "complete");
  return CacheLoadResult::Complete;
}

bool CssParser::promoteCache() const {
  const std::string finalPath = cachePath + rulesCache;
  const std::string tmpPath = cachePath + rulesCacheTmp;
  const std::string backupPath = cachePath + rulesCacheBackup;
  const bool hadExistingCache = Storage.exists(finalPath.c_str());
  if (hadExistingCache) {
    Storage.remove(backupPath.c_str());
    if (!Storage.rename(finalPath.c_str(), backupPath.c_str())) {
      LOG_ERR("CSS", "Failed to back up existing CSS cache");
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }

  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("CSS", "Failed to promote temporary CSS cache");
    Storage.remove(tmpPath.c_str());
    if (Storage.exists(backupPath.c_str()) && !Storage.rename(backupPath.c_str(), finalPath.c_str())) {
      LOG_ERR("CSS", "Failed to restore previous CSS cache");
    }
    return false;
  }

  Storage.remove(backupPath.c_str());

  return true;
}

bool CssParser::validateCache() const { return inspectCache() == CacheStatus::Complete; }

size_t CssParser::collectVerticalStyles(std::vector<std::pair<std::string, VerticalBlockStyle>>& out,
                                        const size_t maxOut) const {
  out.clear();
  out.reserve(maxOut);
  if (cachePath.empty()) return 0;

  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) return 0;

  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != CssParser::CSS_CACHE_VERSION) return 0;
  uint8_t flags = 0;
  if (file.read(&flags, 1) != 1 || (flags & ~CSS_CACHE_KNOWN_FLAGS) != 0) return 0;
  uint16_t ruleCount = 0;
  if (file.read(&ruleCount, sizeof(ruleCount)) != sizeof(ruleCount)) return 0;
  if (ruleCount > MAX_CACHED_RULES) return 0;

  auto emOf = [](const float v, const uint8_t unit) -> float {
    return unit == static_cast<uint8_t>(CssUnit::Em) || unit == static_cast<uint8_t>(CssUnit::Rem) ? v : 0.0f;
  };

  std::string selector;
  for (uint16_t i = 0; i < ruleCount && out.size() < maxOut; i++) {
    uint16_t selectorLen = 0;
    if (file.read(&selectorLen, sizeof(selectorLen)) != sizeof(selectorLen)) break;
    if (selectorLen == 0 || selectorLen > MAX_SELECTOR_LENGTH) break;
    selector.resize(selectorLen);
    if (file.read(selector.data(), selectorLen) != selectorLen) break;

    uint8_t wire[STYLE_WIRE_BYTES];
    CssStyle style;
    if (file.read(wire, sizeof(wire)) != sizeof(wire) || !decodeStyleWire(wire, style)) break;

    // Only unscoped and "v|"-scoped selectors feed the vertical engine.
    if (selector.size() >= 2 && selector[1] == '|') {
      if (selector[0] != 'v') continue;
      selector.erase(0, 2);
    }

    // Compound selectors (v16+) are skipped: the vertical engine matches a block by its own
    // tag/class alone and tracks no ancestor chain, so it could not honour one -- and keeping
    // them would spend entries of the bounded `out` cap on rules that can never match there.
    if (selector.find(CssSelector::DESCENDANT_COMBINATOR) != std::string::npos ||
        selector.find(CssSelector::CHILD_COMBINATOR) != std::string::npos) {
      continue;
    }

    VerticalBlockStyle vs;
    const auto em = [&emOf](const CssLength& length) { return emOf(length.value, static_cast<uint8_t>(length.unit)); };
    if (style.defined.marginTop) vs.startEm = em(style.marginTop);
    if (style.defined.marginRight) vs.beforeEm = em(style.marginRight);
    if (style.defined.marginLeft) vs.afterEm = em(style.marginLeft);
    if (style.defined.paddingTop && style.defined.textIndent && em(style.textIndent) < 0)
      vs.hangEm = em(style.paddingTop);
    if (style.defined.textAlign) vs.alignCenter = style.textAlign == CssTextAlign::Center;
    if (style.defined.border) vs.borderEdges = style.borderEdgeMask();
    if (!vs.any()) continue;

    // Merge with an existing entry for the same selector (later record overrides per property).
    bool merged = false;
    for (auto& [sel, existing] : out) {
      if (sel == selector) {
        if (vs.startEm > 0) existing.startEm = vs.startEm;
        if (vs.beforeEm > 0) existing.beforeEm = vs.beforeEm;
        if (vs.afterEm > 0) existing.afterEm = vs.afterEm;
        if (vs.hangEm > 0) existing.hangEm = vs.hangEm;
        existing.alignCenter = existing.alignCenter || vs.alignCenter;
        if (vs.borderEdges != 0) existing.borderEdges = vs.borderEdges;
        merged = true;
        break;
      }
    }
    if (!merged) out.emplace_back(selector, vs);
  }
  if (out.size() >= maxOut) {
    LOG_ERR("CSS", "collectVerticalStyles hit the %u-entry cap; later rules (e.g. borders) may be dropped",
            static_cast<unsigned>(maxOut));
  }
  return out.size();
}

bool CssParser::beginCacheAppend() {
  if (cacheAppendActive_) return false;
  appendedRuleCount_ = 0;
  cacheAppendFailed_ = false;
  heapTruncated_ = false;
  if (cachePath.empty()) return false;
  if (!Storage.openFileForWrite("CSS", cachePath + rulesCacheTmp, cacheAppendFile_)) return false;
  const uint8_t header[] = {CSS_CACHE_VERSION, CSS_CACHE_FLAG_PARTIAL, 0, 0};
  cacheAppendActive_ = true;
  if (cacheAppendFile_.write(header, sizeof(header)) != sizeof(header)) {
    cacheAppendFailed_ = true;
    endCacheAppend(true);
    return false;
  }
  return true;
}

bool CssParser::appendRulesToCache() {
  if (!cacheAppendActive_ || cacheAppendFailed_) return false;
  for (uint16_t i = 0; i < entryCount_; ++i) {
    if (appendedRuleCount_ >= MAX_CACHED_RULES) {
      cacheAppendFailed_ = true;
      return false;
    }
    const std::string_view selector = selectorAt(i);
    const auto length = static_cast<uint16_t>(selector.size());
    uint8_t wire[STYLE_WIRE_BYTES];
    encodeStyleWire(stylePool_[entries_[i].styleIndex], wire);
    if (cacheAppendFile_.write(&length, sizeof(length)) != sizeof(length) ||
        cacheAppendFile_.write(selector.data(), length) != length ||
        cacheAppendFile_.write(wire, sizeof(wire)) != sizeof(wire)) {
      cacheAppendFailed_ = true;
      return false;
    }
    ++appendedRuleCount_;
  }
  return true;
}

bool CssParser::endCacheAppend(const bool discard) {
  if (!cacheAppendActive_) return false;
  cacheAppendActive_ = false;
  bool ok = !discard && !cacheAppendFailed_ && !heapTruncated_ && !ruleGrowthStopped_;
  const uint8_t flags = 0;
  if (ok) {
    ok = cacheAppendFile_.seek(1) && cacheAppendFile_.write(&flags, sizeof(flags)) == sizeof(flags) &&
         cacheAppendFile_.write(&appendedRuleCount_, sizeof(appendedRuleCount_)) == sizeof(appendedRuleCount_);
  }
  const bool closed = cacheAppendFile_.close();
  if (!ok || !closed) {
    Storage.remove((cachePath + rulesCacheTmp).c_str());
    return false;
  }
  return promoteCache();
}
