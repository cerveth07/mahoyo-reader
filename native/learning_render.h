#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include <array>
#include <utility>
#include <vector>

namespace mh {
bool loadReaderFont(const std::wstring& file);
const wchar_t* readerFace();
const wchar_t* uiFace();
void loadJapaneseUiFace(const std::wstring& ini);
const wchar_t* japaneseUiFace();
struct Box { int l=0,t=0,r=0,b=0; };
// Pixels are straight-alpha 0xAARRGGBB words (BGRA bytes on Windows).
struct Raster { int w=0,h=0; std::vector<std::uint32_t> pixels; };
Raster makeBookmark(bool collected,bool hovered,int scale,bool annotated=false);
Raster makeControlLabel(const std::wstring& text,int textPx,int radius,int scale);
using ClauseAnchors=std::vector<std::pair<std::size_t,std::size_t>>; // clean Japanese, oneLine Chinese
struct ClauseRange {
    std::size_t jaStart=0,jaEnd=0,zhStart=0,zhEnd=0;
    bool operator==(const ClauseRange& b)const{return jaStart==b.jaStart&&jaEnd==b.jaEnd&&zhStart==b.zhStart&&zhEnd==b.zhEnd;}
};
bool clauseAt(const ClauseAnchors& anchors,std::size_t japaneseOffset,ClauseRange& range);
struct CaptionSlice {
    std::shared_ptr<const ClauseAnchors> anchors;
    std::size_t zhStart=0;
    std::vector<float> edges; // One normalized texture boundary per UTF-16 offset, including the end.
};
struct PlacedRaster { int x=0,y=0; Raster image; CaptionSlice slice; };
// The shared layout plan is also used by the offline whole-chapter audit.
// Punctuation anchors are evidence of clause boundaries, not a translation
// model: needsManualReview explicitly identifies ambiguous or relaxed cases.
struct ChineseLayoutRow { Box body; std::wstring text; int japaneseLine=0; };
struct ChineseLayout {
    std::vector<ChineseLayoutRow> rows;
    int pixelHeight=0;
    int targetRubyHeight=0;
    int outlinePixels=1;
    int shadowOffset=0;
    bool horizontalCompression=false;
    bool punctuationAnchored=false;
    bool relaxedAnchors=false;
    bool needsManualReview=false;
    bool handReviewed=false;
    bool groupedRecord=false;
    unsigned matchedInternalAnchors=0;
    double anchorConfidence=0;
    ClauseAnchors highlightAnchors;
};

Raster rasterText(const std::wstring& text, int pixelHeight, std::uint32_t argb,
                  const wchar_t* fontFace=nullptr);

// bodyLines must contain the actual visible Japanese glyph bounds, in local
// texture pixels. Existing ruby space must be available above those bounds.
// Chinese is wrapped within their horizontal spans without adding story lines.
// Extremely disproportionate translations use smaller text and, only as a last
// resort, horizontal compression; they are never truncated or put on a new page.
std::vector<PlacedRaster> makeChinese(const std::wstring& zh,
                                      const std::vector<Box>& bodyLines);
// japaneseLines are exact original substrings for those native line boxes,
// including punctuation. This overload keeps matched clause boundaries on the
// corresponding Japanese rows when feasible at a readable font size.
std::vector<PlacedRaster> makeChinese(const std::wstring& zh,
                                      const std::vector<Box>& bodyLines,
                                      const std::vector<std::wstring>& japaneseLines);
// Actual native slot height, including outline/shadow. The whole record shares
// one optically fitted font size. Zero retains inferred-height compatibility.
std::vector<PlacedRaster> makeChinese(const std::wstring& zh,
                                      const std::vector<Box>& bodyLines,
                                      const std::vector<std::wstring>& japaneseLines,
                                      int targetRubyHeight,
                                      const std::vector<std::vector<Box>>& originalRuby={});
ChineseLayout planChinese(const std::wstring& zh,
                           const std::vector<Box>& bodyLines,
                           const std::vector<std::wstring>& japaneseLines={},
                           int targetRubyHeight=0);

// A projected caption uses the original glyph ink and leaves author ruby intact.
// japaneseLine identifies the physical row even if a caption moves sideways.
struct ProjectedCaption { PlacedRaster raster; int japaneseLine; };
std::vector<ProjectedCaption> projectChinese(const std::wstring& text,
    const std::vector<Box>& rows,const std::vector<std::wstring>& japanese,
    const std::vector<Box>& obstacles,int bodyHeight,bool speechBubble);

// width caps the physical pixel width. Scale is a percentage for typography
// and padding. The card is at most 300*scale/100 pixels high.
std::vector<std::wstring> splitMora(const std::wstring& reading);
bool pitchPattern(const std::wstring& reading,const std::wstring& pitch,std::vector<bool>& high,bool& particleHigh);
enum class CardContent { Word, Translation };
Raster makeTooltip(const std::wstring& term, const std::wstring& reading,
                   const std::wstring& pos, const std::wstring& definition,
                   const std::wstring& level, const std::wstring& pitch,
                   int width=420, int scale=100, CardContent content=CardContent::Word,
                   const std::wstring& primaryAction=L"");

// A reading-only hover uses compact, measured content with no badge or definition.
Raster makeReadingBubble(const std::wstring& term, const std::wstring& reading, int scale);

// These rectangles are relative to the complete painted card and also drive its hit targets.
Raster withWordActions(const Raster& body,const std::wstring& primary,int scale,Box& edit,Box& select,Box& save,bool compact=false);
// Apply to the complete card, after any action bar is attached.
void roundCardCorners(Raster& card,int scale);
Raster makeToast(const std::wstring& text,int scale);

// Replace the clipped rectangle, including transparent pixels. This is
// deliberately idempotent when a locked text texture is refreshed repeatedly.
// The caller must supply the top-left pixel and a positive row pitch >= w*4.
void paint(std::uint8_t* dst, int w, int h, int pitch,
           const PlacedRaster& placed);
} // namespace mh
