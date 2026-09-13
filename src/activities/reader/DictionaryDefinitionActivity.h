#pragma once

#include <Dictionary.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "activities/Activity.h"

class DictionaryDefinitionActivity final : public Activity {
 public:
  static constexpr size_t DEFINITION_CAPACITY = 8193;

  DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<char[]> text,
                               size_t textLength, const char* headword, Dictionary::DefinitionFormat format,
                               int readerFontId, bool sourceTruncated);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowReaderIdlePowerSaving() const override { return true; }

 private:
  struct Line {
    uint16_t start = 0;
    uint16_t length = 0;
  };

  static constexpr size_t MAX_LINE_BYTES = 191;
  static constexpr size_t MAX_LINES = 256;

  size_t normalizeHtmlInPlace();
  void normalizePlainText();
  void wrapText();
  int measureSpan(const char* text, size_t length) const;
  void drawBody(int x, int y) const;

  std::unique_ptr<char[]> text_;
  size_t textLength_ = 0;
  char headword_[128] = {};
  Dictionary::DefinitionFormat format_ = Dictionary::DefinitionFormat::Plain;
  int fontId_ = 0;
  bool sourceTruncated_ = false;
  bool layoutTruncated_ = false;

  std::array<Line, MAX_LINES> lines_{};
  uint16_t lineCount_ = 0;
  uint16_t linesPerPage_ = 1;
  uint16_t currentPage_ = 0;
  uint16_t totalPages_ = 1;
};
