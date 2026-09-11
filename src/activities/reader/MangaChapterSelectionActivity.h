#pragma once
#include <I18n.h>
#include <MangaPanel.h>

#include <vector>

#include "MappedInputManager.h"
#include "activities/UiListActivity.h"

class MangaChapterSelectionActivity final : public UiListActivity {
 public:
  explicit MangaChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::vector<manga::TocEntry> tocEntries, const uint32_t currentPage)
      : UiListActivity("MangaChapterSelection", renderer, mappedInput), tocEntries(std::move(tocEntries)) {
    // Pre-select whichever chapter the current page falls within.
    for (size_t i = 0; i < this->tocEntries.size(); i++) {
      if (this->tocEntries[i].pageIndex <= currentPage) {
        initialSelectedIndex = static_cast<int>(i);
      } else {
        break;
      }
    }
  }

  void onEnter() override;

 private:
  std::vector<manga::TocEntry> tocEntries;
  std::vector<freeink::ui::ListItem> rowItems;
  int initialSelectedIndex = 0;

  void rebuildRowItems();
  int listCount() const override { return static_cast<int>(tocEntries.size()); }
  const char* headerTitle() const override { return tr(STR_SELECT_CHAPTER); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
};
