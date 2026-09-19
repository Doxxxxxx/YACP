#include "DictionaryDefinitionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {

bool equalsAscii(const char* value, const size_t length, const char* expected) {
  const size_t expectedLength = strlen(expected);
  if (length != expectedLength) return false;
  for (size_t i = 0; i < length; i++) {
    if (std::tolower(static_cast<unsigned char>(value[i])) != expected[i]) return false;
  }
  return true;
}

bool isBlockTag(const char* tag, const size_t length) {
  return equalsAscii(tag, length, "br") || equalsAscii(tag, length, "p") || equalsAscii(tag, length, "div") ||
         equalsAscii(tag, length, "li") || equalsAscii(tag, length, "tr") || equalsAscii(tag, length, "h1") ||
         equalsAscii(tag, length, "h2") || equalsAscii(tag, length, "h3") || equalsAscii(tag, length, "h4") ||
         equalsAscii(tag, length, "h5") || equalsAscii(tag, length, "h6");
}

size_t appendUtf8(char* output, const size_t write, const uint32_t codepoint) {
  if (codepoint == 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return write;
  if (codepoint <= 0x7F) {
    output[write] = static_cast<char>(codepoint);
    return write + 1;
  }
  if (codepoint <= 0x7FF) {
    output[write] = static_cast<char>(0xC0 | (codepoint >> 6));
    output[write + 1] = static_cast<char>(0x80 | (codepoint & 0x3F));
    return write + 2;
  }
  if (codepoint <= 0xFFFF) {
    output[write] = static_cast<char>(0xE0 | (codepoint >> 12));
    output[write + 1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    output[write + 2] = static_cast<char>(0x80 | (codepoint & 0x3F));
    return write + 3;
  }
  output[write] = static_cast<char>(0xF0 | (codepoint >> 18));
  output[write + 1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
  output[write + 2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
  output[write + 3] = static_cast<char>(0x80 | (codepoint & 0x3F));
  return write + 4;
}

bool decodeNumericEntity(const char* entity, const size_t length, uint32_t& value) {
  if (length < 4 || entity[0] != '&' || entity[1] != '#' || entity[length - 1] != ';') return false;
  size_t pos = 2;
  uint32_t base = 10;
  if (entity[pos] == 'x' || entity[pos] == 'X') {
    base = 16;
    pos++;
  }
  if (pos >= length - 1) return false;
  value = 0;
  for (; pos < length - 1; pos++) {
    const unsigned char c = entity[pos];
    uint32_t digit = UINT32_MAX;
    if (c >= '0' && c <= '9') digit = c - '0';
    if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    if (digit >= base || value > (0x10FFFF - digit) / base) return false;
    value = value * base + digit;
  }
  return true;
}

}  // namespace

DictionaryDefinitionActivity::DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           std::unique_ptr<char[]> text, const size_t textLength,
                                                           const char* headword,
                                                           const Dictionary::DefinitionFormat format,
                                                           const int readerFontId, const bool sourceTruncated)
    : Activity("DictionaryDefinition", renderer, mappedInput),
      text_(std::move(text)),
      textLength_(textLength),
      format_(format),
      fontId_(readerFontId),
      sourceTruncated_(sourceTruncated) {
  if (headword) {
    strncpy(headword_, headword, sizeof(headword_) - 1);
    headword_[sizeof(headword_) - 1] = '\0';
  }
}

void DictionaryDefinitionActivity::onEnter() {
  Activity::onEnter();
  if (!text_) {
    LOG_ERR("DICT", "Definition activity has no text buffer");
    finish();
    return;
  }
  if (format_ == Dictionary::DefinitionFormat::Html) {
    textLength_ = normalizeHtmlInPlace();
  } else {
    normalizePlainText();
  }
  renderer.ensureSdCardFontReady(fontId_, text_.get(), 0x01);
  wrapText();
  requestUpdate();
}

void DictionaryDefinitionActivity::onExit() {
  text_.reset();
  Activity::onExit();
}

size_t DictionaryDefinitionActivity::normalizeHtmlInPlace() {
  char* const text = text_.get();
  size_t read = 0;
  size_t write = 0;
  while (read < textLength_) {
    if (text[read] == '<') {
      size_t close = read + 1;
      while (close < textLength_ && text[close] != '>') close++;
      if (close < textLength_) {
        size_t name = read + 1;
        while (name < close && (text[name] == '/' || std::isspace(static_cast<unsigned char>(text[name])))) name++;
        size_t end = name;
        while (end < close && std::isalnum(static_cast<unsigned char>(text[end]))) end++;
        if (isBlockTag(text + name, end - name) && write > 0 && text[write - 1] != '\n') {
          text[write++] = '\n';
        }
        read = close + 1;
        continue;
      }
    }

    if (text[read] == '&') {
      size_t semicolon = read + 1;
      while (semicolon < textLength_ && semicolon - read <= 12 && text[semicolon] != ';') semicolon++;
      if (semicolon < textLength_ && text[semicolon] == ';') {
        const size_t entityLength = semicolon - read + 1;
        const char* entity = text + read;
        char replacement = '\0';
        if (entityLength == 6 && strncmp(entity, "&nbsp;", entityLength) == 0) replacement = ' ';
        if (entityLength == 5 && strncmp(entity, "&amp;", entityLength) == 0) replacement = '&';
        if (entityLength == 4 && strncmp(entity, "&lt;", entityLength) == 0) replacement = '<';
        if (entityLength == 4 && strncmp(entity, "&gt;", entityLength) == 0) replacement = '>';
        if (entityLength == 6 && strncmp(entity, "&quot;", entityLength) == 0) replacement = '"';
        if (entityLength == 6 && strncmp(entity, "&apos;", entityLength) == 0) replacement = '\'';
        if (replacement != '\0') {
          text[write++] = replacement;
          read = semicolon + 1;
          continue;
        }
        uint32_t codepoint = 0;
        if (decodeNumericEntity(entity, entityLength, codepoint)) {
          write = appendUtf8(text, write, codepoint);
          read = semicolon + 1;
          continue;
        }
      }
    }

    const char value = text[read++];
    if (value == '\0' || value == '\r' || value == '\t') {
      if (write > 0 && text[write - 1] != ' ' && text[write - 1] != '\n') text[write++] = ' ';
    } else if (value == '\n') {
      if (write > 0 && text[write - 1] != '\n') text[write++] = '\n';
    } else {
      text[write++] = value;
    }
  }
  while (write > 0 && (text[write - 1] == ' ' || text[write - 1] == '\n')) write--;
  text[write] = '\0';
  return write;
}

void DictionaryDefinitionActivity::normalizePlainText() {
  char* const text = text_.get();
  for (size_t i = 0; i < textLength_; i++) {
    if (text[i] == '\0') text[i] = '\n';
    if (text[i] == '\r' || text[i] == '\t') text[i] = ' ';
  }
  text[textLength_] = '\0';
}

int DictionaryDefinitionActivity::measureSpan(const char* text, size_t length) const {
  char buffer[MAX_LINE_BYTES + 1];
  length = std::min(length, MAX_LINE_BYTES);
  memcpy(buffer, text, length);
  buffer[length] = '\0';
  return renderer.getTextAdvanceX(fontId_, buffer, EpdFontFamily::REGULAR);
}

void DictionaryDefinitionActivity::wrapText() {
  const char* const text = text_.get();
  lineCount_ = 0;
  layoutTruncated_ = false;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int bodyTop = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bodyBottom = safe.y + safe.height - metrics.verticalSpacing;
  const int bodyHeight = std::max(1, bodyBottom - bodyTop);
  const int maxWidth = std::max(1, safe.width - 2 * metrics.contentSidePadding);
  const int lineHeight = std::max(1, renderer.getLineHeight(fontId_));
  linesPerPage_ = static_cast<uint16_t>(std::max(1, bodyHeight / lineHeight));
  const int spaceWidth = renderer.getSpaceWidth(fontId_, EpdFontFamily::REGULAR);

  uint32_t lineStart = 0;
  uint32_t lineEnd = 0;
  int lineWidth = 0;
  const auto flush = [&](const uint32_t nextStart) {
    if (lineCount_ >= MAX_LINES) {
      layoutTruncated_ = true;
      return false;
    }
    lines_[lineCount_++] = {static_cast<uint16_t>(lineStart), static_cast<uint16_t>(lineEnd - lineStart)};
    lineStart = nextStart;
    lineEnd = nextStart;
    lineWidth = 0;
    return true;
  };

  uint32_t pos = 0;
  while (pos < textLength_) {
    const char value = text[pos];
    if (value == '\n') {
      if (!flush(pos + 1)) break;
      pos++;
      continue;
    }
    if (value == ' ' || value == '\t') {
      pos++;
      continue;
    }

    const uint32_t tokenStart = pos;
    while (pos < textLength_ && text[pos] != ' ' && text[pos] != '\t' && text[pos] != '\n' &&
           pos - tokenStart < MAX_LINE_BYTES) {
      pos++;
    }
    while (pos - tokenStart > 1 && pos < textLength_ && (static_cast<uint8_t>(text[pos]) & 0xC0) == 0x80) {
      pos--;
    }
    const uint32_t tokenLength = pos - tokenStart;
    const int tokenWidth = measureSpan(text + tokenStart, tokenLength);

    if (lineEnd == lineStart) {
      lineStart = tokenStart;
      lineEnd = tokenStart + tokenLength;
      lineWidth = tokenWidth;
    } else if (lineWidth + spaceWidth + tokenWidth <= maxWidth) {
      lineEnd = tokenStart + tokenLength;
      lineWidth += spaceWidth + tokenWidth;
    } else {
      if (!flush(tokenStart)) break;
      lineEnd = tokenStart + tokenLength;
      lineWidth = tokenWidth;
    }

    while (lineWidth > maxWidth && lineEnd - lineStart > 1) {
      const uint32_t length = lineEnd - lineStart;
      uint32_t lastFit = 0;
      for (uint32_t fit = 1; fit <= length; fit++) {
        if (fit == length || (static_cast<uint8_t>(text[lineStart + fit]) & 0xC0) != 0x80) {
          if (measureSpan(text + lineStart, fit) > maxWidth) break;
          lastFit = fit;
        }
      }
      if (lastFit == 0) {
        lastFit = 1;
        while (lastFit < length && (static_cast<uint8_t>(text[lineStart + lastFit]) & 0xC0) == 0x80) lastFit++;
      }
      const uint32_t rest = lineStart + lastFit;
      lineEnd = rest;
      if (!flush(rest)) break;
      lineEnd = rest + length - lastFit;
      lineWidth = measureSpan(text + lineStart, lineEnd - lineStart);
    }
    if (layoutTruncated_) break;
  }
  if (!layoutTruncated_ && lineEnd > lineStart) flush(static_cast<uint32_t>(textLength_));
  while (lineCount_ > 0 && lines_[lineCount_ - 1].length == 0) lineCount_--;

  totalPages_ = static_cast<uint16_t>(
      std::max(1, (static_cast<int>(lineCount_) + static_cast<int>(linesPerPage_) - 1) / linesPerPage_));
  currentPage_ = 0;
  if (sourceTruncated_ || layoutTruncated_) {
    LOG_INF("DICT", "Definition display truncated at %u bytes and %u lines", static_cast<unsigned>(textLength_),
            static_cast<unsigned>(lineCount_));
  }
}

void DictionaryDefinitionActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  if ((mappedInput.wasReleased(Button::Right) || mappedInput.wasReleased(Button::Down)) &&
      currentPage_ + 1 < totalPages_) {
    currentPage_++;
    requestUpdate();
  } else if ((mappedInput.wasReleased(Button::Left) || mappedInput.wasReleased(Button::Up)) && currentPage_ > 0) {
    currentPage_--;
    requestUpdate();
  }
}

void DictionaryDefinitionActivity::drawBody(const int x, const int y) const {
  const char* const text = text_.get();
  char buffer[MAX_LINE_BYTES + 1];
  const int lineHeight = renderer.getLineHeight(fontId_);
  const uint16_t first = static_cast<uint16_t>(currentPage_ * linesPerPage_);
  const uint16_t last = std::min<uint16_t>(lineCount_, static_cast<uint16_t>(first + linesPerPage_));
  for (uint16_t i = first; i < last; i++) {
    const Line& line = lines_[i];
    if (line.length == 0) continue;
    memcpy(buffer, text + line.start, line.length);
    buffer[line.length] = '\0';
    renderer.drawText(fontId_, x, y + (i - first) * lineHeight, buffer);
  }
}

void DictionaryDefinitionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight};

  char counter[16] = {};
  if (totalPages_ > 1) {
    snprintf(counter, sizeof(counter), "%u/%u", static_cast<unsigned>(currentPage_ + 1),
             static_cast<unsigned>(totalPages_));
  }
  GUI.drawHeader(renderer, header, headword_, counter[0] ? counter : nullptr, true);

  const int bodyX = safe.x + metrics.contentSidePadding;
  const int bodyY = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const auto drawDefinition = [this, bodyX, bodyY]() {
    drawBody(bodyX, bodyY);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", currentPage_ > 0 ? "<" : "",
                                               currentPage_ + 1 < totalPages_ ? ">" : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
    renderer.displayBuffer();
  };

  if (auto* cache = renderer.getFontCacheManager()) {
    auto scope = cache->createPrewarmScope();
    drawBody(bodyX, bodyY);
    if (!scope.endScanAndPrewarm()) {
      LOG_ERR("DICT", "Failed to prewarm reader font for dictionary definition");
    }
    drawDefinition();
    return;
  }
  drawDefinition();
}
