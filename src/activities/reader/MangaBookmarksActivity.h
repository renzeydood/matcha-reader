#pragma once
#include <I18n.h>
#include <MangaPanel.h>

#include <string>
#include <vector>

#include "../../BookmarkEntry.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Manga bookmark list. Unlike EpubReaderBookmarksActivity, a manga bookmark's
// position is a plain page index (computedChapterProgress) -- there's no
// spine/xpath to resolve, so "open" just returns that page number directly.
class MangaBookmarksActivity final : public UiListActivity {
  std::string bookPath;
  std::vector<manga::TocEntry> tocEntries;
  std::vector<BookmarkEntry> bookmarks;
  std::vector<std::string> bookmarkSubtitles;
  std::vector<freeink::ui::ListItem> bookmarkRowItems;
  bool confirmingDelete = false;
  OptionPopup confirmPopup;

 public:
  explicit MangaBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                                  std::vector<manga::TocEntry> tocEntries)
      : UiListActivity("MangaBookmarks", renderer, mappedInput, /*wantsTouchLongPress=*/true),
        bookPath(std::move(bookPath)),
        tocEntries(std::move(tocEntries)) {}
  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(bookmarks.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  const char* headerTitle() const override { return tr(STR_BOOKMARKS); }

  void rebuildBookmarkRowItems();
  void openSelectedBookmark();
  void showDeleteConfirmation();
  void deleteSelectedBookmark();
};
