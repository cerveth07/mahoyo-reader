#pragma once

#include <windows.h>
#include <cstdint>

namespace reader_palette {
// COLORREF carries RGB only.
inline constexpr COLORREF Ink=RGB(23,36,47);
inline constexpr COLORREF Shadow=RGB(0,0,0);
inline constexpr COLORREF Card=RGB(20,31,40);
inline constexpr COLORREF Field=RGB(16,29,38);
inline constexpr COLORREF Raised=RGB(27,41,50);
inline constexpr COLORREF Line=RGB(62,80,88);
inline constexpr COLORREF Body=RGB(238,236,224);
inline constexpr COLORREF Muted=RGB(183,198,197);
inline constexpr COLORREF Dim=RGB(143,163,163);
inline constexpr COLORREF Accent=RGB(223,200,154);
inline constexpr COLORREF AccentHover=RGB(235,218,182);
inline constexpr COLORREF Danger=RGB(196,163,160);
inline constexpr COLORREF HoverTint=RGB(223,200,154);
inline constexpr COLORREF Caption=RGB(240,236,227);

inline constexpr COLORREF OrbHoverFill=RGB(45,62,68); // Orb fill when hovered or selected.
inline constexpr COLORREF OrbInnerRing=RGB(74,91,93); // Recessed inner ring on the orb.
inline constexpr COLORREF OrbIcon=RGB(239,230,207); // Ivory outline of the orb book icon.
inline constexpr COLORREF ShelfPressedFill=RGB(88,99,97); // Pressed state of native shelf buttons.
inline constexpr COLORREF GlyphMask=RGB(255,255,255); // White coverage mask for GDI text rasterization.

namespace argb {
constexpr std::uint32_t fromColorRef(COLORREF color,std::uint32_t alpha=255){
    return (alpha<<24)|(std::uint32_t(GetRValue(color))<<16)|
           (std::uint32_t(GetGValue(color))<<8)|std::uint32_t(GetBValue(color));
}

inline constexpr std::uint32_t Ink=fromColorRef(reader_palette::Ink);
inline constexpr std::uint32_t Card=fromColorRef(reader_palette::Card,245);
inline constexpr std::uint32_t Field=fromColorRef(reader_palette::Field);
inline constexpr std::uint32_t Raised=fromColorRef(reader_palette::Raised);
inline constexpr std::uint32_t Line=fromColorRef(reader_palette::Line);
inline constexpr std::uint32_t Body=fromColorRef(reader_palette::Body);
inline constexpr std::uint32_t Muted=fromColorRef(reader_palette::Muted);
inline constexpr std::uint32_t Dim=fromColorRef(reader_palette::Dim);
inline constexpr std::uint32_t Accent=fromColorRef(reader_palette::Accent);
inline constexpr std::uint32_t AccentHover=fromColorRef(reader_palette::AccentHover);
inline constexpr std::uint32_t Danger=fromColorRef(reader_palette::Danger);
inline constexpr std::uint32_t HoverTint=fromColorRef(reader_palette::HoverTint,26);
inline constexpr std::uint32_t Caption=fromColorRef(reader_palette::Caption);
inline constexpr std::uint32_t BubbleCaption=fromColorRef(RGB(112,70,40));

inline constexpr std::uint32_t CardEdge=0xe36a756fu; // Word card perimeter.
inline constexpr std::uint32_t CardAccentBar=0xf8c4ae7fu; // Subdued gold stripe along the word card.
inline constexpr std::uint32_t CardActionDivider=fromColorRef(reader_palette::Line,0xd3); // Translucent divider above the word card actions.
inline constexpr std::uint32_t RaisedActionFill=fromColorRef(reader_palette::Raised,0xf5); // Word card action bar.
inline constexpr std::uint32_t CardDetail=fromColorRef(RGB(159,177,174));
inline constexpr std::uint32_t ControlActiveFill=0xf23c3933u; // Warm fill for active overlay controls.
inline constexpr std::uint32_t ToastFill=fromColorRef(reader_palette::Card,224);
inline constexpr std::uint32_t ToastEdge=fromColorRef(reader_palette::Accent,97);
inline constexpr std::uint32_t PickHighlight=fromColorRef(reader_palette::Accent,0x24); // Translucent highlight for the glyph under the word picker.
inline constexpr std::uint32_t TextureModulation=0xffffffffu; // Neutral modulation for overlay textures.
} // namespace argb
} // namespace reader_palette
