#include "button.h"

#include "decoration.h"
#include "metrics.h"

#include <KDecoration3/DecoratedWindow>

#include <QIcon>
#include <QPainter>
#include <QPainterPath>

namespace Shellkit
{
Button::Button(KDecoration3::DecorationButtonType type, Decoration *decoration, QObject *parent)
    : KDecoration3::DecorationButton(type, decoration, parent)
{
    // The icon button spans the caption height so its click area matches Windows'.
    const qreal width = type == KDecoration3::DecorationButtonType::Menu ? Metrics::TitleHeight : Metrics::ButtonWidth;
    setGeometry(QRectF(0, 0, width, Metrics::TitleHeight));
}

void Button::paint(QPainter *painter, const QRectF &repaintArea)
{
    Q_UNUSED(repaintArea)
    if (!isVisible()) {
        return;
    }
    auto *deco = qobject_cast<Decoration *>(decoration());
    if (!deco) {
        return;
    }
    const QRectF box = geometry();

    if (type() == KDecoration3::DecorationButtonType::Menu) {
        const QRectF icon(box.center().x() - Metrics::IconSize / 2, box.center().y() - Metrics::IconSize / 2, Metrics::IconSize, Metrics::IconSize);
        deco->window()->icon().paint(painter, icon.toRect());
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    const bool close = type() == KDecoration3::DecorationButtonType::Close;
    QColor glyph = deco->foreground();
    if (close && (isHovered() || isPressed())) {
        painter->setBrush(isPressed() ? Metrics::ClosePressed : Metrics::CloseHover);
        painter->drawRect(box);
        glyph = Qt::white;
    } else if (isHovered() || isPressed()) {
        // WinUI SubtleFillColorSecondary on hover, SubtleFillColorTertiary while pressed.
        const bool dark = deco->isDark();
        QColor fill = dark ? Qt::white : Qt::black;
        fill.setAlpha(dark ? (isPressed() ? 0x0a : 0x0f) : (isPressed() ? 0x06 : 0x09));
        painter->setBrush(fill);
        painter->drawRect(box);
    }
    if (!isEnabled()) {
        glyph.setAlphaF(glyph.alphaF() * 0.36);
    }

    QPen pen(glyph, 1.0);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    // Snap the 10 px glyph box to whole pixels so 1 px strokes stay crisp.
    const QRectF glyphBox(qRound(box.center().x() - Metrics::GlyphSize / 2), qRound(box.center().y() - Metrics::GlyphSize / 2), Metrics::GlyphSize, Metrics::GlyphSize);
    paintGlyph(painter, glyphBox);
    painter->restore();
}

void Button::paintGlyph(QPainter *painter, const QRectF &box) const
{
    const qreal x = box.left();
    const qreal y = box.top();
    const qreal s = box.width();
    // Corner radius of the maximize and restore squares.
    const qreal r = 1.5;

    switch (type()) {
    case KDecoration3::DecorationButtonType::Minimize:
        painter->drawLine(QPointF(x, y + s / 2 - 0.5), QPointF(x + s, y + s / 2 - 0.5));
        break;
    case KDecoration3::DecorationButtonType::Maximize:
        if (isChecked()) {
            // Restore: a front square bottom-left, the back one peeking out top-right.
            // Both squares are s - 3 across, two pixels apart; strokes sit on pixel centres.
            const qreal left = x + 0.5, top = y + 0.5, right = x + s - 0.5, bottom = y + s - 0.5;
            painter->drawRoundedRect(QRectF(left, top + 2, s - 3, s - 3), r, r);
            QPainterPath back;
            back.moveTo(left + 2, top + 2);
            back.lineTo(left + 2, top + r);
            back.quadTo(left + 2, top, left + 2 + r, top);
            back.lineTo(right - r, top);
            back.quadTo(right, top, right, top + r);
            back.lineTo(right, bottom - 2 - r);
            back.quadTo(right, bottom - 2, right - r, bottom - 2);
            back.lineTo(right - 2, bottom - 2);
            painter->drawPath(back);
        } else {
            painter->drawRoundedRect(QRectF(x + 0.5, y + 0.5, s - 1, s - 1), r, r);
        }
        break;
    case KDecoration3::DecorationButtonType::Close:
        painter->drawLine(QPointF(x, y), QPointF(x + s, y + s));
        painter->drawLine(QPointF(x + s, y), QPointF(x, y + s));
        break;
    default:
        break;
    }
}
}
