#pragma once
// Windows 7 look for Qt Widgets, for the File Explorer's Windows 7 mode: the Windows 11 style
// with Windows 7 shapes where they show most (gradient selection boxes with a border, square
// text boxes, header dividers, triangles in trees, classic scroll bars, square grey menus,
// smaller controls).

#include "style.h"

namespace Shellkit
{
class Win7Style : public Style
{
    Q_OBJECT

public:
    Win7Style();

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl subControl, const QWidget *widget = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr, const QWidget *widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget = nullptr) const override;
};
}
