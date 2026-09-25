#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "Epub.h"
#include "EpubRenderMode.h"

class Page;
class GfxRenderer;
class ChapterHtmlSlimParser;
class CssParser;

struct SectionBuildOptions {
  const char* previewAnchor = nullptr;
  uint16_t previewMaxPages = 0;

  bool isPreview() const { return previewAnchor && previewAnchor[0] != '\0' && previewMaxPages > 0; }
};

class Section {
  struct PageLutEntry {
    uint32_t fileOffset = 0;
    uint16_t paragraphIndex = 0;
    uint16_t listItemIndex = 0;
  };

  struct AdaptiveBuildContext {
    std::unique_ptr<ChapterHtmlSlimParser> parser;
    std::unique_ptr<PageLutEntry[]> lut;
    uint16_t lutCapacity = 0;
    uint16_t lutCount = 0;
    std::string parsePath;
    std::string contentBase;
    std::string imageBasePath;
    std::string htmlPath;
    std::string tmpHtmlPath;
    CssParser* cssParser = nullptr;
    int fontId = 0;
    bool htmlCached = false;
    bool pageCompletionFailed = false;
    bool imagesWereSuppressed = false;
  };

  std::shared_ptr<Epub> epub;
  const int spineIndex;
  GfxRenderer& renderer;
  std::string filePath;
  HalFile file;
  std::unique_ptr<AdaptiveBuildContext> adaptiveBuild_;
  uint16_t adaptiveBuiltPageCount_ = 0;
  bool adaptiveBuildComplete_ = false;
  bool writeSectionFileHeader(int fontId, float lineCompression, bool extraParagraphSpacing, bool forceParagraphIndents,
                              uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                              bool hyphenationEnabled, bool embeddedStyle, uint8_t imageRendering,
                              bool bionicReadingEnabled, bool guideReadingEnabled, EpubRenderMode renderMode);
  uint32_t onPageComplete(std::unique_ptr<Page> page);
  bool finalizeAdaptiveBuild();
  void abandonAdaptiveBuild();
  std::unique_ptr<Page> loadPageDuringAdaptiveBuild(int page);

 public:
  uint16_t pageCount = 0;
  int currentPage = 0;

  explicit Section(const std::shared_ptr<Epub>& epub, int spineIndex, GfxRenderer& renderer,
                   const char* cacheSuffix = "");
  ~Section();
  bool loadSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, bool forceParagraphIndents,
                       uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                       bool hyphenationEnabled, bool embeddedStyle, uint8_t imageRendering, bool bionicReadingEnabled,
                       bool guideReadingEnabled, EpubRenderMode renderMode);
  bool clearCache() const;
  bool createSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, bool forceParagraphIndents,
                         uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                         bool hyphenationEnabled, bool embeddedStyle, uint8_t imageRendering, bool bionicReadingEnabled,
                         bool guideReadingEnabled, const std::function<void()>& popupFn = nullptr,
                         bool* imagesWereSuppressed = nullptr, bool* layoutAbortedForLowMemory = nullptr,
                         EpubRenderMode renderMode = EpubRenderMode::CrossInkDefault,
                         SectionBuildOptions buildOptions = {});

  // Low-memory fallback. Unlike CrossPoint's universal incremental indexing,
  // YACP enters this mode only after the normal full-chapter path cannot keep a safe heap margin.
  bool startAdaptiveBuild(int fontId, float lineCompression, bool extraParagraphSpacing, bool forceParagraphIndents,
                          uint8_t paragraphAlignment, uint16_t viewportWidth, uint16_t viewportHeight,
                          bool hyphenationEnabled, bool embeddedStyle, uint8_t imageRendering,
                          bool bionicReadingEnabled, bool guideReadingEnabled,
                          const std::function<void()>& popupFn = nullptr,
                          EpubRenderMode renderMode = EpubRenderMode::CrossInkDefault);
  bool buildAdaptivePages(uint16_t maxAdditionalPages);
  bool isAdaptiveBuilding() const { return static_cast<bool>(adaptiveBuild_); }
  bool isAdaptiveBuildComplete() const { return adaptiveBuildComplete_; }

  std::unique_ptr<Page> loadPageFromSectionFile();
  std::string getTextFromSectionFile();

  // True if this spine's unzipped HTML is already cached, so a build won't pay the (multi-second on a
  // giant spine) zip inflation.
  bool hasHtmlCache() const;

  // Look up the page number for an anchor id from the section cache file.
  std::optional<uint16_t> getPageForAnchor(const std::string& anchor) const;

  // Get the page count from the section cache file without fully loading it.
  std::optional<uint16_t> getCachedPageCount() const;

  // Look up the page number for a synthetic paragraph index from XPath p[N].
  std::optional<uint16_t> getPageForParagraphIndex(uint16_t pIndex) const;

  // Look up the page number for a running list-item index from the li LUT.
  std::optional<uint16_t> getPageForListItemIndex(uint16_t liIndex) const;

  // Look up the synthetic paragraph index for the given rendered page.
  std::optional<uint16_t> getParagraphIndexForPage(uint16_t page) const;

  // Look up the running list-item index for the given rendered page.
  std::optional<uint16_t> getListItemIndexForPage(uint16_t page) const;
};
