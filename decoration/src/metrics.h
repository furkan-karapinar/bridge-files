#pragma once
// Windows 11 caption metrics in logical pixels, from the standard Win32 frame at 100 % scale.

#include <QColor>

namespace Shellkit::Metrics
{
// Caption height, also the height of every caption button.
inline constexpr qreal TitleHeight = 32;
inline constexpr qreal ButtonWidth = 46;
// App icon: 16 px square, inset from the left edge, caption text after it.
inline constexpr qreal IconSize = 16;
inline constexpr qreal IconInset = 12;
inline constexpr qreal CaptionGap = 8;
// Window corner radius (ControlCornerRadius for top-level windows).
inline constexpr qreal CornerRadius = 8;
// Caption glyphs (Segoe Fluent ChromeMinimize/Maximize/Restore/Close) are 10 px boxes.
inline constexpr qreal GlyphSize = 10;
// Invisible resize margins outside the visible frame, like Windows' resize borders.
inline constexpr qreal ResizeMargin = 6;

// Close button: the system critical red, white glyph.
inline const QColor CloseHover(0xc4, 0x2b, 0x1c);
inline const QColor ClosePressed(0xc4, 0x2b, 0x1c, 0xe6);
}
