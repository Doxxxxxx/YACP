#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

int EpubReaderChapterSelectionActivity::getTotalItems() const { return epub->getTocItemsCount(); }

int EpubReaderChapterSelectionActivity::getPageItems() const {
  return std::max(1, UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false));
}

void EpubReaderChapterSelectionActivity::prewarmVisibleMetadata(const int pageItems) {
  if (pageItems <= 0) return;

  const int pageStart = selectorIndex / pageItems * pageItems;
  if (pageStart != metadataPageStart) {
    visibleMetadataTitles.clear();
    const int visibleCount = std::min(pageItems, static_cast<int>(MAX_VISIBLE_METADATA_TITLES));
    const int pageEnd = std::min(getTotalItems(), pageStart + visibleCount);
    for (int index = pageStart; index < pageEnd; ++index) {
      visibleMetadataTitles.push_back(epub->getTocItem(index).title);
    }
    metadataPageStart = pageStart;
  }

  const char* metadataText[MAX_VISIBLE_METADATA_TITLES] = {};
  std::size_t metadataTextCount = 0;
  for (const auto& title : visibleMetadataTitles) {
    metadataText[metadataTextCount++] = title.c_str();
  }
  renderer.prewarmUiMetadata(metadataText, metadataTextCount, 0x01);
}

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  visibleMetadataTitles.reserve(MAX_VISIBLE_METADATA_TITLES);
  selectorIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selectorIndex == -1) {
    selectorIndex = 0;
  }

  // Trigger first update
  requestUpdate();
}

void EpubReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderChapterSelectionActivity::loop() {
  const int pageItems = getPageItems();
  const int totalItems = getTotalItems();

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto tocItem = epub->getTocItem(selectorIndex);
    if (tocItem.spineIndex == -1) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    } else {
      setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
      finish();
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER), nullptr, true);

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
  prewarmVisibleMetadata(getPageItems());

  const int totalItems = getTotalItems();
  GUI.drawList(renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, totalItems, selectorIndex,
               [this](int index) {
                 auto item = epub->getTocItem(index);
                 std::string indent((item.level - 1) * 2, ' ');
                 return indent + item.title;
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}
