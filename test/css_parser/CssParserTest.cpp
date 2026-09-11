#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "CssParser.h"

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxRules = 1500;
constexpr size_t kMaxUniqueStyles = 256;
constexpr size_t kCacheHeaderBytes = sizeof(uint8_t) * 2 + sizeof(uint16_t);
constexpr size_t kStyleEnumPrefixBytes = 5;
constexpr size_t kStyleLengthFieldCount = 14;
constexpr size_t kStyleLengthBytes = sizeof(decltype(CssLength::value)) + sizeof(uint8_t);

class CssParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    directory_ = fs::temp_directory_path() / "crosspoint_css_parser_test" / info->name();
    fs::remove_all(directory_);
    fs::create_directories(directory_);
    Storage.clearFailures();
  }

  void TearDown() override {
    Storage.clearFailures();
    fs::remove_all(directory_);
  }

  std::string cachePath() const { return directory_.string(); }
  fs::path cacheFile() const { return directory_ / "css_rules.cache"; }
  fs::path cacheTempFile() const { return directory_ / "css_rules.cache.tmp"; }
  fs::path cacheBackupFile() const { return directory_ / "css_rules.cache.bak"; }

  std::vector<uint8_t> readCache() const {
    std::ifstream input(cacheFile(), std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  void writeCache(const std::vector<uint8_t>& bytes) const {
    std::ofstream output(cacheFile(), std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  CssParser::ParseResult loadCss(CssParser& parser, const std::string& css) const {
    const fs::path sourcePath = directory_ / "input.css";
    std::ofstream output(sourcePath, std::ios::binary);
    output.write(css.data(), static_cast<std::streamsize>(css.size()));
    output.close();

    HalFile source;
    EXPECT_TRUE(HalStorage::getInstance().openFileForRead("TST", sourcePath.string(), source));
    return parser.loadFromStream(source);
  }

  fs::path directory_;
};

TEST_F(CssParserTest, ResolvesCaseInsensitiveCascadeAndMergesDuplicates) {
  CssParser parser(cachePath());
  ASSERT_EQ(loadCss(parser,
                    "P { text-align: center; }\n"
                    ".Note { font-weight: bold; text-align: right; }\n"
                    "p.note { font-style: italic; text-align: justify; }\n"
                    ".note { margin-top: 2em; }\n"),
            CssParser::ParseResult::Complete);

  EXPECT_EQ(parser.ruleCount(), 3u);
  const CssStyle style = parser.resolveStyle("p", "NOTE");
  EXPECT_EQ(style.textAlign, CssTextAlign::Justify);
  EXPECT_EQ(style.fontWeight, CssFontWeight::Bold);
  EXPECT_EQ(style.fontStyle, CssFontStyle::Italic);
  ASSERT_TRUE(style.hasMarginTop());
  EXPECT_FLOAT_EQ(style.marginTop.value, 2.0f);
  EXPECT_EQ(style.marginTop.unit, CssUnit::Em);
}

TEST_F(CssParserTest, DeduplicatedStylesReachTheBoundedRuleCap) {
  CssParser parser(cachePath());
  std::string css;
  for (size_t i = 0; i < kMaxRules + 20; ++i) {
    css += ".class-" + std::to_string(i) + " { font-weight: bold; text-indent: 1.5em; }\n";
  }

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial);
  EXPECT_EQ(parser.ruleCount(), kMaxRules);
  EXPECT_EQ(parser.resolveStyle("div", "class-1499").fontWeight, CssFontWeight::Bold);
  EXPECT_FALSE(parser.resolveStyle("div", "class-1500").hasFontWeight());
}

TEST_F(CssParserTest, CascadeUpdatesContinueAfterRuleCap) {
  CssParser parser(cachePath());
  std::string css;
  for (size_t i = 0; i < kMaxRules + 20; ++i) {
    css += ".class-" + std::to_string(i) + " { font-weight: bold; }\n";
  }
  css += ".class-1499 { font-style: italic; }\n";

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial);
  EXPECT_EQ(parser.ruleCount(), kMaxRules);
  const CssStyle style = parser.resolveStyle("div", "class-1499");
  EXPECT_EQ(style.fontWeight, CssFontWeight::Bold);
  EXPECT_EQ(style.fontStyle, CssFontStyle::Italic);
}

TEST_F(CssParserTest, UniqueStyleCapStopsWithoutCorruptingAcceptedRules) {
  CssParser parser(cachePath());
  std::string css;
  for (size_t i = 0; i < kMaxUniqueStyles + 20; ++i) {
    css += ".unique-" + std::to_string(i) + " { text-indent: " + std::to_string(i + 1) + "px; }\n";
  }

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial);
  EXPECT_EQ(parser.ruleCount(), kMaxUniqueStyles);
  EXPECT_FLOAT_EQ(parser.resolveStyle("p", "unique-255").textIndent.value, 256.0f);
  EXPECT_FALSE(parser.resolveStyle("p", "unique-256").hasTextIndent());
}

TEST_F(CssParserTest, RepeatedOverridesReuseAnUnsharedStyleSlot) {
  CssParser parser(cachePath());
  std::string css;
  for (size_t i = 0; i < kMaxUniqueStyles + 20; ++i) {
    css += ".same { text-indent: " + std::to_string(i + 1) + "px; }\n";
  }

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Complete);
  EXPECT_EQ(parser.ruleCount(), 1u);
  EXPECT_FLOAT_EQ(parser.resolveStyle("p", "same").textIndent.value, static_cast<float>(kMaxUniqueStyles + 20));
}

TEST_F(CssParserTest, OversizedSelectorGroupMarksParsePartialAndSkipsRule) {
  CssParser parser(cachePath());
  const std::string css = "." + std::string(1100, 'a') +
                          " { font-weight: bold; }\n"
                          ".valid { font-style: italic; }\n";

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial);
  EXPECT_EQ(parser.ruleCount(), 1u);
  EXPECT_EQ(parser.resolveStyle("span", "valid").fontStyle, CssFontStyle::Italic);
}

TEST_F(CssParserTest, OversizedDeclarationMarksParsePartialAndKeepsFollowingDeclarations) {
  CssParser parser(cachePath());
  const std::string css = ".long { font-weight: " + std::string(1100, 'x') + "; font-style: italic; }\n";

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial);
  EXPECT_EQ(parser.ruleCount(), 1u);
  const CssStyle style = parser.resolveStyle("span", "long");
  EXPECT_FALSE(style.hasFontWeight());
  EXPECT_EQ(style.fontStyle, CssFontStyle::Italic);
}

TEST_F(CssParserTest, IncompleteInputMarksParsePartial) {
  for (const char* css : {".a { font-weight: bold;", "@media screen {", "/* unfinished", ".unfinished"}) {
    CssParser parser(cachePath());
    EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial) << css;
  }
}

TEST_F(CssParserTest, CanonicalCacheRoundTripPreservesStyles) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer,
                    "p { text-align: justify; margin-top: 2em; }\n"
                    ".bold { font-weight: bolder; }\n"
                    ".hidden { display: none; }\n"),
            CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));
  EXPECT_EQ(writer.inspectCache(), CssParser::CacheStatus::Complete);

  CssParser reader(cachePath());
  ASSERT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Complete);
  EXPECT_EQ(reader.ruleCount(), writer.ruleCount());
  EXPECT_EQ(reader.resolveStyle("span", "bold").fontWeight, CssFontWeight::Bold);
  EXPECT_EQ(reader.resolveStyle("div", "hidden").display, CssDisplay::None);
  const CssStyle paragraph = reader.resolveStyle("p", "");
  EXPECT_EQ(paragraph.textAlign, CssTextAlign::Justify);
  EXPECT_FLOAT_EQ(paragraph.marginTop.value, 2.0f);
}

TEST_F(CssParserTest, PartialCacheIsValidatedDuringInspection) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, ".a { font-weight: bold; }\n"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(false));
  ASSERT_EQ(writer.inspectCache(), CssParser::CacheStatus::Partial);

  const auto size = fs::file_size(cacheFile());
  fs::resize_file(cacheFile(), size - 1);
  EXPECT_EQ(writer.inspectCache(), CssParser::CacheStatus::Invalid);
}

TEST_F(CssParserTest, CompleteCacheIsValidatedWithoutHydration) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, ".a { font-weight: bold; }\n"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));

  const auto size = fs::file_size(cacheFile());
  fs::resize_file(cacheFile(), size - 1);
  EXPECT_EQ(writer.inspectCache(), CssParser::CacheStatus::Invalid);

  CssParser reader(cachePath());
  EXPECT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Invalid);
  EXPECT_TRUE(reader.empty());
}

TEST_F(CssParserTest, FailedPromotionRestoresTheOnlyBackupCache) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, ".original { font-weight: bold; }\n"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));
  fs::rename(cacheFile(), cacheBackupFile());
  ASSERT_FALSE(fs::exists(cacheFile()));
  ASSERT_EQ(loadCss(writer, ".replacement { font-style: italic; }\n"), CssParser::ParseResult::Complete);

  Storage.failNextRename(cacheTempFile().string(), cacheFile().string());
  EXPECT_FALSE(writer.saveToCache(true));
  EXPECT_TRUE(fs::exists(cacheFile()));
  EXPECT_FALSE(fs::exists(cacheTempFile()));
  EXPECT_FALSE(fs::exists(cacheBackupFile()));

  CssParser reader(cachePath());
  ASSERT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Complete);
  EXPECT_EQ(reader.ruleCount(), 1u);
  EXPECT_EQ(reader.resolveStyle("span", "original").fontWeight, CssFontWeight::Bold);
  EXPECT_FALSE(reader.resolveStyle("span", "replacement").hasFontStyle());
}

TEST_F(CssParserTest, CacheHydrationRejectsInvalidStyleEnumBytes) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, ".a { font-weight: bold; margin-top: 2em; }\n"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));

  const std::vector<uint8_t> validCache = readCache();
  ASSERT_GE(validCache.size(), kCacheHeaderBytes + sizeof(uint16_t));
  uint16_t selectorLength = 0;
  memcpy(&selectorLength, validCache.data() + kCacheHeaderBytes, sizeof(selectorLength));
  const size_t styleOffset = kCacheHeaderBytes + sizeof(selectorLength) + selectorLength;

  std::vector<size_t> enumOffsets = {0, 1, 2, 3, 4};
  for (size_t i = 0; i < kStyleLengthFieldCount; ++i) {
    enumOffsets.push_back(kStyleEnumPrefixBytes + i * kStyleLengthBytes + sizeof(decltype(CssLength::value)));
  }
  enumOffsets.push_back(kStyleEnumPrefixBytes + kStyleLengthFieldCount * kStyleLengthBytes);
  enumOffsets.push_back(kStyleEnumPrefixBytes + kStyleLengthFieldCount * kStyleLengthBytes + 1);

  for (const size_t enumOffset : enumOffsets) {
    SCOPED_TRACE(enumOffset);
    std::vector<uint8_t> corruptedCache = validCache;
    ASSERT_LT(styleOffset + enumOffset, corruptedCache.size());
    corruptedCache[styleOffset + enumOffset] = 0xff;
    writeCache(corruptedCache);

    CssParser reader(cachePath());
    EXPECT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Invalid);
    EXPECT_TRUE(reader.empty());
  }
}

TEST_F(CssParserTest, CacheHydrationRejectsNonFiniteStyleLengths) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, ".a { margin-top: 2em; }\n"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));

  const std::vector<uint8_t> validCache = readCache();
  ASSERT_GE(validCache.size(), kCacheHeaderBytes + sizeof(uint16_t));
  uint16_t selectorLength = 0;
  memcpy(&selectorLength, validCache.data() + kCacheHeaderBytes, sizeof(selectorLength));
  const size_t firstLengthOffset = kCacheHeaderBytes + sizeof(selectorLength) + selectorLength + kStyleEnumPrefixBytes;

  using LengthValue = decltype(CssLength::value);
  for (const LengthValue invalidValue :
       {std::numeric_limits<LengthValue>::quiet_NaN(), std::numeric_limits<LengthValue>::infinity(),
        -std::numeric_limits<LengthValue>::infinity()}) {
    std::vector<uint8_t> corruptedCache = validCache;
    ASSERT_LE(firstLengthOffset + sizeof(invalidValue), corruptedCache.size());
    memcpy(corruptedCache.data() + firstLengthOffset, &invalidValue, sizeof(invalidValue));
    writeCache(corruptedCache);

    CssParser reader(cachePath());
    EXPECT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Invalid);
    EXPECT_TRUE(reader.empty());
  }
}

TEST_F(CssParserTest, MatchaStylePropertiesSurviveCacheRoundTrip) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer,
                    ".note { display: inline-block; font-size: 1.5em; line-height: 1.8; "
                    "letter-spacing: 0.2em; border: 2px dashed black; text-emphasis: sesame; "
                    "font-variant: small-caps; list-style-type: square; color: white; background-color: black; "
                    "page-break-before: always; text-transform: uppercase; hyphens: none; }"),
            CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));
  CssParser reader(cachePath());
  ASSERT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Complete);
  const CssStyle style = reader.resolveStyle("p", "note");
  EXPECT_EQ(style.display, CssDisplay::InlineBlock);
  EXPECT_FLOAT_EQ(style.fontSize.value, 1.5f);
  EXPECT_FLOAT_EQ(style.lineHeight.value, 1.8f);
  EXPECT_EQ(style.lineHeight.unit, CssUnit::Number);
  EXPECT_FLOAT_EQ(style.letterSpacing.value, 0.2f);
  EXPECT_EQ(style.borderLineStyle(), CssBorderLineStyle::Dashed);
  EXPECT_EQ(style.borderLineWidth(), 2u);
  EXPECT_EQ(style.textEmphasis, CssTextEmphasis::FilledSesame);
  EXPECT_EQ(style.fontVariant, CssFontVariant::SmallCaps);
  EXPECT_EQ(style.listStyleType, CssListStyleType::Square);
  EXPECT_EQ(style.inkMode, CssInkMode::Inverted);
  EXPECT_EQ(style.pageBreakBefore(), CssPageBreak::Always);
  EXPECT_EQ(style.textTransform(), CssTextTransform::Uppercase);
  EXPECT_TRUE(style.hyphensNone());
}

TEST_F(CssParserTest, IncrementalCacheFiltersLargeBooksAndMergesAcrossFlushes) {
  CssParser writer(cachePath());
  ASSERT_TRUE(writer.beginCacheAppend());
  std::string css;
  for (size_t i = 0; i < 1700; ++i) css += ".c" + std::to_string(i) + " { font-weight: bold; }\n";
  ASSERT_EQ(loadCss(writer, css), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.appendRulesToCache());
  writer.clear();
  ASSERT_EQ(loadCss(writer, ".c1699 { font-style: italic; } .vrtl .c1699 { margin-top: 3em; }"),
            CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.appendRulesToCache());
  ASSERT_TRUE(writer.endCacheAppend(false));
  EXPECT_EQ(writer.inspectCache(), CssParser::CacheStatus::Complete);
  CssParser reader(cachePath());
  const std::vector<std::string> used = {"c1699"};
  ASSERT_EQ(reader.loadFromCache(&used), CssParser::CacheLoadResult::Complete);
  EXPECT_EQ(reader.ruleCount(), 1u);
  const CssStyle style = reader.resolveStyle("p", "c1699");
  EXPECT_EQ(style.fontWeight, CssFontWeight::Bold);
  EXPECT_EQ(style.fontStyle, CssFontStyle::Italic);
  EXPECT_FALSE(style.defined.marginTop);
  std::vector<std::pair<std::string, CssParser::VerticalBlockStyle>> vertical;
  ASSERT_EQ(reader.collectVerticalStyles(vertical), 1u);
  EXPECT_EQ(vertical[0].first, ".c1699");
  EXPECT_FLOAT_EQ(vertical[0].second.startEm, 3.0f);
}

TEST_F(CssParserTest, DiscardedIncrementalWriteKeepsPreviousCache) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, "p { font-weight: bold; }"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));
  const auto original = readCache();
  writer.clear();
  ASSERT_TRUE(writer.beginCacheAppend());
  ASSERT_EQ(loadCss(writer, "p { font-style: italic; }"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.appendRulesToCache());
  EXPECT_FALSE(writer.endCacheAppend(true));
  EXPECT_EQ(readCache(), original);
  EXPECT_FALSE(fs::exists(cacheTempFile()));
}

TEST_F(CssParserTest, FailedIncrementalPromotionRestoresPreviousCache) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, "p { font-weight: bold; }"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));
  const auto original = readCache();
  writer.clear();
  ASSERT_TRUE(writer.beginCacheAppend());
  ASSERT_EQ(loadCss(writer, "p { font-style: italic; }"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.appendRulesToCache());
  Storage.failNextRename(cachePath() + "/css_rules.cache.tmp", cachePath() + "/css_rules.cache");
  EXPECT_FALSE(writer.endCacheAppend(false));
  EXPECT_EQ(readCache(), original);
}

}  // namespace
