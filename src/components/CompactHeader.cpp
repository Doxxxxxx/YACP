#include "CompactHeader.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "HeaderDate.h"
#include "UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kHeaderHeight = 67;
constexpr int kHeaderTopGap = 6;
constexpr int kHeaderTitleLift = 5;
constexpr int kHeaderBaselineLift = 2;

int visibleHeaderHeight(const ThemeMetrics& metrics) { return std::min(metrics.headerHeight, kHeaderHeight); }

int titleBaselineY(const GfxRenderer& renderer, const ThemeMetrics& metrics, const int fontId) {
  const int availableH = visibleHeaderHeight(metrics) - metrics.batteryBarHeight;
  const int titleLineHeight = renderer.getLineHeight(fontId);
  const int titleY =
      metrics.topPadding + metrics.batteryBarHeight + (availableH - titleLineHeight) / 2 - kHeaderTitleLift;
  return titleY + renderer.getFontAscenderSize(fontId) - kHeaderBaselineLift;
}
}  // namespace

namespace CompactHeader {
int headerBottomY(const ThemeMetrics& metrics) { return metrics.topPadding + visibleHeaderHeight(metrics); }

int contentTop(const ThemeMetrics& metrics) { return headerBottomY(metrics) + kHeaderTopGap; }

void drawTitle(const GfxRenderer& renderer, const char* title, const bool showDate) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, visibleHeaderHeight(metrics)}, "");

  const int titleX = metrics.contentSidePadding;
  const int batteryStartX = pageWidth - metrics.contentSidePadding - metrics.batteryWidth;
  const int dateStartX = showDate ? pageWidth - headerDateReservedWidth(renderer) : pageWidth;
  const int titleRightX = std::min(batteryStartX, dateStartX) - metrics.contentSidePadding;
  const int maxTitleWidth = std::max(1, titleRightX - titleX);
  const int titleFontId = renderer.uiMetadataFontForText(UI_12_FONT_ID, title);
  const int baselineY = titleBaselineY(renderer, metrics, titleFontId);
  const std::string visibleTitle = renderer.truncatedText(titleFontId, title, maxTitleWidth, EpdFontFamily::BOLD);

  renderer.drawText(titleFontId, titleX, baselineY - renderer.getFontAscenderSize(titleFontId), visibleTitle.c_str(),
                    true, EpdFontFamily::BOLD);
  if (showDate) {
    drawHeaderDateAtBaseline(renderer, pageWidth, baselineY);
  }
}

void drawTitleWithoutStatus(const GfxRenderer& renderer, const char* title) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int titleX = metrics.contentSidePadding;
  const int maxTitleWidth = std::max(1, pageWidth - titleX - metrics.contentSidePadding);
  const int titleFontId = renderer.uiMetadataFontForText(UI_12_FONT_ID, title);
  const int baselineY = titleBaselineY(renderer, metrics, titleFontId);
  const std::string visibleTitle = renderer.truncatedText(titleFontId, title, maxTitleWidth, EpdFontFamily::BOLD);

  renderer.drawText(titleFontId, titleX, baselineY - renderer.getFontAscenderSize(titleFontId), visibleTitle.c_str(),
                    true, EpdFontFamily::BOLD);
  const int separatorY = headerBottomY(metrics) - 1;
  renderer.drawLine(0, separatorY, pageWidth, separatorY);
}
}  // namespace CompactHeader
