#include "MangaBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookmarkFile.h"

namespace fui = freeink::ui;

namespace {
constexpr int ENTER_DELETE_MODE_MS = 700;
}

void MangaBookmarksActivity::onEnter() {
  UiListActivity::onEnter();

  if (!BookmarkFile::load(bookPath, bookmarks)) {
    bookmarks.shrink_to_fit();
  }
  LOG_DBG("MNG", "Loaded %d bookmarks for book: %s", static_cast<int>(bookmarks.size()), bookPath.c_str());
  rebuildBookmarkRowItems();
}

void MangaBookmarksActivity::rebuildBookmarkRowItems() {
  bookmarkSubtitles.clear();
  bookmarkRowItems.clear();
  bookmarkSubtitles.reserve(bookmarks.size());
  bookmarkRowItems.reserve(bookmarks.size());

  for (const auto& bookmark : bookmarks) {
    std::string chapterTitle = tr(STR_UNNAMED);
    for (const auto& entry : tocEntries) {
      if (entry.pageIndex <= bookmark.computedChapterProgress) {
        chapterTitle = entry.title;
      } else {
        break;
      }
    }
    std::string subtitle =
        std::to_string(bookmark.computedChapterProgress + 1) + "/" + std::to_string(bookmark.computedChapterPageCount);
    if (!tocEntries.empty()) {
      subtitle += " - " + chapterTitle;
    }
    bookmarkSubtitles.push_back(std::move(subtitle));

    fui::ListItem item;
    item.label = bookmark.summary.c_str();
    item.subtitle = bookmarkSubtitles.back().c_str();
    item.icon = listIconFor(UIIcon::Bookmark, 32);
    item.actionValue = static_cast<int16_t>(bookmarkRowItems.size());
    bookmarkRowItems.push_back(item);
  }
}

void MangaBookmarksActivity::openSelectedBookmark() {
  if (bookmarks.empty() || nav.selected < 0 || nav.selected >= static_cast<int>(bookmarks.size())) {
    return;
  }
  const auto& bookmark = bookmarks.at(nav.selected);
  setResult(PageResult{bookmark.computedChapterProgress});
  finish();
}

void MangaBookmarksActivity::activateIndex(const int index) {
  if (confirmPopup.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  openSelectedBookmark();
}

void MangaBookmarksActivity::onRowLongPress(const int index) {
  if (confirmPopup.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showDeleteConfirmation();
}

bool MangaBookmarksActivity::handleCustomInput() {
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete) {
    confirmingDelete = false;
    requestUpdate();
    return true;
  }
  return false;
}

bool MangaBookmarksActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() > ENTER_DELETE_MODE_MS) {
      showDeleteConfirmation();
    } else {
      openSelectedBookmark();
    }
    return true;
  }

  return false;
}

void MangaBookmarksActivity::showDeleteConfirmation() {
  if (bookmarks.empty() || confirmPopup.isActive()) {
    return;
  }
  confirmingDelete = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup.show(tr(STR_CONFIRM_DELETE_BOOKMARK), options, 2, 0, [this](int idx) {
    confirmingDelete = false;
    if (idx == 1) {
      deleteSelectedBookmark();
    }
    requestUpdate();
  });
  requestUpdate();
}

void MangaBookmarksActivity::deleteSelectedBookmark() {
  if (nav.selected < 0 || nav.selected >= static_cast<int>(bookmarks.size())) {
    return;
  }

  bookmarks.erase(bookmarks.begin() + nav.selected);
  rebuildBookmarkRowItems();
  if (!BookmarkFile::save(bookPath, bookmarks)) {
    LOG_ERR("MNG", "Failed to save bookmarks after delete");
  }

  if (nav.selected >= static_cast<int>(bookmarks.size()) && nav.selected > 0) {
    nav.selected--;
  }

  if (bookmarks.empty()) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  nav.follow(listCount());
  requestUpdate(true);
}

void MangaBookmarksActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (bookmarks.empty()) {
    screen.centeredText(tr(STR_NO_BOOKMARKS), screen.theme().bodyText);
    return;
  }

  if (!mappedInput.hasTouch()) {
    const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{band.x, band.y + metrics.verticalSpacing, band.width, helpLineHeight},
                     tr(STR_HOLD_OPEN_TO_DELETE));
  }

  fui::ListProps props;
  props.items = bookmarkRowItems.data();
  props.count = static_cast<uint16_t>(bookmarkRowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void MangaBookmarksActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();

  if (confirmPopup.processRender(renderer, mappedInput)) return;

  const auto confirmLabel = bookmarks.empty() ? "" : tr(STR_SELECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
