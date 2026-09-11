#pragma once

#include <EpdFontFamily.h>

#include <deque>
#include <string>

class GfxRenderer {
 public:
  struct FrameBufferLoan {
    explicit FrameBufferLoan(GfxRenderer&) {}
  };
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  int getLineHeight(int, float = 1.0f) const { return 16; }
  int getFontAscenderSize(int) const { return 12; }
  int getSpaceWidth(int, EpdFontFamily::Style, int8_t letterSpacing = 0) const { return 4 + letterSpacing; }
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style, int8_t letterSpacing = 0) const {
    int width = 0;
    while (*text++) width += 8 + letterSpacing;
    return width;
  }
  int getKerning(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 0; }
  int getSpaceAdvance(int, uint32_t, uint32_t, EpdFontFamily::Style, int8_t letterSpacing = 0) const {
    return 4 + letterSpacing;
  }
  bool isSdCardFont(int) const { return false; }
  void ensureSdCardFontReady(int, const std::deque<std::string>&, bool, uint8_t) const {}
};
