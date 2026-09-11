#include "EpubReaderMenuActivity.h"

#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

EpubReaderMenuActivity::EpubReaderMenuActivity(
    GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title, const int currentPage,
    const int totalPages, const int bookProgressPercent, const uint8_t currentOrientation, const bool hasFootnotes,
    const bool hasBookmarks, const bool hasWordLookup, const bool verticalEnabled, const bool furiganaEnabled,
    const bool hasPageText, const bool imageReaderMinimal, const bool mangaMode, const bool hideGenericLookup,
    const bool showPanelsOnlyToggle, const bool panelsOnlyEnabled)
    : UiListActivity("EpubReaderMenu", renderer, mappedInput),
      menuItems(buildMenuItems(hasFootnotes, hasBookmarks, hasWordLookup, imageReaderMinimal, mangaMode,
                               hideGenericLookup, showPanelsOnlyToggle)),
      hasPageText(hasPageText),
      title(title),
      pendingOrientation(currentOrientation),
      pendingVerticalEnabled(verticalEnabled),
      pendingFuriganaEnabled(furiganaEnabled),
      panelsOnlyEnabled(panelsOnlyEnabled),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
  buildMenuRowItems();
}

// Populates menuRowItems's labels/actionValue from menuItems. Called once
// here since menuItems (and thus which rows exist) never changes after
// construction; buildScreen() only touches the two rows with a live value.
void EpubReaderMenuActivity::buildMenuRowItems() {
  for (size_t i = 0; i < menuItems.size() && i < MAX_MENU_ITEMS; i++) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[i].labelId);
    item.actionValue = static_cast<int16_t>(i);
    const auto action = menuItems[i].action;
    if (!hasPageText &&
        (action == MenuAction::WORD_LOOKUP || action == MenuAction::TRANSLATE_PAGE || action == MenuAction::DISPLAY_QR))
      item.state = fui::StateDisabled;
    menuRowItems[i] = item;
  }
}

std::vector<EpubReaderMenuActivity::MenuItem> EpubReaderMenuActivity::buildMenuItems(
    bool hasFootnotes, bool hasBookmarks, bool hasWordLookup, bool imageReaderMinimal, bool mangaMode,
    bool hideGenericLookup, bool showPanelsOnlyToggle) {
  std::vector<MenuItem> items;
  items.reserve(MAX_MENU_ITEMS);

  // Minimal menu for the image readers (XTC): a page-based format has no text/footnotes/vertical
  // toggles, so only chapter select (when present), Go-to-page, bookmarks, screenshot and
  // clear-cache apply. hasFootnotes is repurposed as "has chapters" in this mode.
  if (imageReaderMinimal) {
    if (hasFootnotes) items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
    items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
    items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
    if (hasBookmarks) items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
    items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
    items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
    return items;
  }

  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  if (hasWordLookup) {
    items.push_back({MenuAction::WORD_LOOKUP, StrId::STR_WORD_LOOKUP});
  }
  items.push_back({MenuAction::TRANSLATE_PAGE, StrId::STR_TRANSLATE_PAGE});
  // Vertical Text and Furigana moved into Reader Settings (SettingInfo::DynamicToggle, gated
  // there on the same condition this menu used to gate TOGGLE_VERTICAL/TOGGLE_FURIGANA) so they
  // sit with the rest of the reading-experience settings instead of this per-page action menu.
  // Reader Settings itself is shown for manga too, filtered down to the settings manga supports.
  if (showPanelsOnlyToggle) {
    items.push_back({MenuAction::TOGGLE_PANELS_ONLY, StrId::STR_PANELS_ONLY});
  }
  items.push_back({MenuAction::READER_SETTINGS, StrId::STR_READER_SETTINGS});
  if (hasBookmarks) {
    items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
  // Free-form dictionary lookup doesn't apply to manga (Word Lookup covers OCR'd text) or to
  // unsegmented Japanese text, where Word Lookup is the only lookup that makes sense.
  if (!mangaMode && !hideGenericLookup) {
    items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  }
  // Reading Orientation removed from this quick menu entirely; it's a proper setting under
  // Settings > Reader (SettingsList.h, tied to CrossPointSettings::orientation directly) for
  // every reader type now that Reader Settings is reachable from manga too (see above) --
  // ROTATE_SCREEN's handling below (the option popup, pendingOrientation) is unreachable but
  // left in place rather than torn out along with it.
  if (!mangaMode) items.push_back({MenuAction::TEXT_SETTINGS, StrId::STR_TEXT_SETTINGS});
  items.push_back({MenuAction::NIGHT_MODE, StrId::STR_NIGHT_MODE});
  if (Frontlight.present()) {
    items.push_back({MenuAction::FRONTLIGHT, StrId::STR_FRONTLIGHT});
  }
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  return items;
}

void EpubReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  // The toggles must ride along on EVERY exit path. Vertical Text and Furigana don't close the
  // menu when pressed -- they flip a pending flag and redraw -- so leaving with Back IS how the
  // user commits them. Omitting them here leaves MenuResult's -1 defaults, which the reader
  // reads as "unchanged", and the toggle silently does nothing. (Upstream added this early Back
  // handler; the fork's own Back branch further down carried the flags and became unreachable.)
  result.data =
      MenuResult{-1, pendingOrientation, selectedPageTurnOption, static_cast<int8_t>(pendingVerticalEnabled ? 1 : 0),
                 static_cast<int8_t>(pendingFuriganaEnabled ? 1 : 0)};
  setResult(std::move(result));
  finish();
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void EpubReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive() || index < 0 || index >= static_cast<int>(menuItems.size())) return;
  // The activated row leaves this screen (popup or finish); a lingering flash
  // would gray an unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;

  const auto selectedAction = menuItems[index].action;
  if (selectedAction == MenuAction::ROTATE_SCREEN) {
    optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                     pendingOrientation, [this](int idx) {
                       pendingOrientation = idx;
                       // Rotate the menu immediately. Only the renderer turns;
                       // SETTINGS.orientation stays unchanged so the reader's
                       // result handler still detects the change and reflows.
                       ReaderUtils::applyOrientation(renderer, pendingOrientation);
                       app.setDevice(uiTarget.deviceContext());  // hit rects follow the new frame
                       requestUpdate(true);
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
    optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                     static_cast<int>(pageTurnLabels.size()), selectedPageTurnOption, [this](int idx) {
                       selectedPageTurnOption = idx;
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::NIGHT_MODE) {
    SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::FRONTLIGHT) {
    const bool lightOn = !Frontlight.isOn();
    Frontlight.setOn(lightOn);
    SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption,
                       static_cast<int8_t>(pendingVerticalEnabled), static_cast<int8_t>(pendingFuriganaEnabled)});
  finish();
}

bool EpubReaderMenuActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool EpubReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }

  return false;
}

void EpubReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  // Progress summary where the old sub-header band sat.
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight));
  const int16_t pad = screen.theme().headerSidePadding;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), progressLine.c_str(), screen.theme().smallText);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // menuRowItems's labels/actionValue were set once in the constructor (see
  // buildMenuRowItems()); only rows with live values need refreshing here.
  for (size_t i = 0; i < menuItems.size(); i++) {
    const auto action = menuItems[i].action;
    if (action == MenuAction::TOGGLE_PANELS_ONLY) {
      menuRowItems[i].value = I18N.get(panelsOnlyEnabled ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    } else if (action == MenuAction::ROTATE_SCREEN) {
      menuRowItems[i].value = I18N.get(orientationLabels[pendingOrientation]);
    } else if (action == MenuAction::AUTO_PAGE_TURN) {
      menuRowItems[i].value = pageTurnLabels[selectedPageTurnOption];
    } else if (action == MenuAction::NIGHT_MODE) {
      menuRowItems[i].value = I18N.get(SETTINGS.screenInverted ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    } else if (action == MenuAction::FRONTLIGHT) {
      menuRowItems[i].value = I18N.get(Frontlight.isOn() ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
    }
  }

  fui::ListProps props;
  props.items = menuRowItems;
  props.count = static_cast<uint16_t>(menuItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void EpubReaderMenuActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 title.c_str());
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  drawChrome();

  renderUi();

  drawFooter();
  renderer.displayBuffer();
}
