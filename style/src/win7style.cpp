#include "win7style.h"

#include <QLineEdit>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QToolBar>

namespace Shellkit
{
namespace
{
// Windows 7 controls: 3 px corners, 22 px text boxes and rows, 23 px push buttons.
constexpr qreal Radius = 3;
constexpr int ControlHeight = 24;
constexpr int PushButtonHeight = 24;
constexpr int MenuChevronWidth = 16;
constexpr int ButtonPadding = 8;
// Menus: 22 px items, separators 7 px, a line after the icon column.
constexpr int MenuItemHeight = 22;
constexpr int MenuSeparatorHeight = 7;
constexpr int MenuMargin = 2;
// Extra width of the icon column, so the line after it clears the icons and the labels.
constexpr int MenuIconExtra = 14;

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

/** A rounded box with a vertical gradient fill and a 1 px border. */
void drawBox(QPainter *painter, const QRectF &rect, const QColor &top, const QColor &bottom, const QColor &border, qreal radius = Radius)
{
    const QRectF r = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient fill(r.topLeft(), r.bottomLeft());
    fill.setColorAt(0, top);
    fill.setColorAt(1, bottom);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(border);
    painter->setBrush(fill);
    painter->drawRoundedRect(r, radius, radius);
    painter->restore();
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

// The Windows 7 sky blue of selection boxes, as a translucent tint on dark.
const QColor DarkTint(0x33, 0x99, 0xff);

/** Selection and hover boxes of item views (files, navigation pane). */
void drawItemBox(QPainter *painter, const QRectF &rect, const QPalette &palette, bool selected, bool hover, bool active)
{
    if (isDark(palette)) {
        if (selected) {
            drawBox(painter, rect, withAlpha(DarkTint, hover ? 0x55 : 0x45), withAlpha(DarkTint, hover ? 0x45 : 0x35), withAlpha(DarkTint, active ? 0xa0 : 0x60));
        } else {
            drawBox(painter, rect, withAlpha(DarkTint, 0x22), withAlpha(DarkTint, 0x16), withAlpha(DarkTint, 0x55));
        }
        return;
    }
    if (selected && !active) {
        drawBox(painter, rect, QColor(0xf8, 0xf8, 0xf8), QColor(0xe5, 0xe5, 0xe5), QColor(0xd9, 0xd9, 0xd9));
    } else if (selected) {
        drawBox(painter, rect, QColor(0xdc, 0xeb, 0xfc), QColor(0xc1, 0xdb, 0xfc), hover ? QColor(0x66, 0x8f, 0xc4) : QColor(0x7d, 0xa2, 0xce));
    } else {
        drawBox(painter, rect, QColor(0xfa, 0xfb, 0xfd), QColor(0xeb, 0xf3, 0xfd), QColor(0xb8, 0xd6, 0xfb));
    }
}

/** Command bar buttons: a light glassy box on hover, a darker one while pressed or open. */
void drawToolBox(QPainter *painter, const QRectF &rect, const QPalette &palette, bool pressed)
{
    if (isDark(palette)) {
        drawBox(painter, rect, QColor(255, 255, 255, pressed ? 0x10 : 0x1c), QColor(255, 255, 255, pressed ? 0x08 : 0x0c), QColor(255, 255, 255, 0x30));
    } else if (pressed) {
        drawBox(painter, rect, QColor(0xc9, 0xd9, 0xee), QColor(0xdb, 0xe7, 0xf5), QColor(0x7f, 0x97, 0xb5));
    } else {
        drawBox(painter, rect, QColor(0xfb, 0xfd, 0xff), QColor(0xdf, 0xea, 0xf7), QColor(0x9a, 0xb3, 0xd2));
    }
}

/** Tree branches: a hollow triangle pointing right, or a filled one pointing down-right. */
void drawTreeTriangle(QPainter *painter, const QRectF &rect, bool open, const QPalette &palette)
{
    const QPointF c = rect.center();
    QPainterPath path;
    if (open) {
        path.moveTo(c.x() + 3, c.y() - 3);
        path.lineTo(c.x() + 3, c.y() + 3);
        path.lineTo(c.x() - 3, c.y() + 3);
    } else {
        path.moveTo(c.x() - 2, c.y() - 4);
        path.lineTo(c.x() + 2, c.y());
        path.lineTo(c.x() - 2, c.y() + 4);
    }
    path.closeSubpath();
    QColor color = palette.color(QPalette::WindowText);
    color.setAlphaF(open ? 0.7 : 0.45);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(color, 1.0));
    painter->setBrush(open ? QBrush(color) : QBrush(Qt::NoBrush));
    painter->drawPath(path);
    painter->restore();
}

bool isPaddedButton(const QStyleOptionToolButton &button)
{
    return button.toolButtonStyle == Qt::ToolButtonTextBesideIcon && !button.text.isEmpty()
        && !(button.features & QStyleOptionToolButton::MenuButtonPopup);
}

/** The x of the line after a menu's icon column. */
int menuGutter(const QStyleOptionMenuItem &item)
{
    // Fusion puts the icon column 4 px in, at least 21 px wide; the line goes near its end,
    // between the icons and the labels.
    return item.rect.left() + 4 + qMax(item.maxIconWidth + MenuIconExtra, 21) - 2;
}

/** The two-tone line after the icon column, or across a separator. */
void drawMenuLine(QPainter *painter, const QPalette &palette, const QPoint &from, const QPoint &to)
{
    const bool dark = isDark(palette);
    const QPoint step = from.x() == to.x() ? QPoint(1, 0) : QPoint(0, 1);
    painter->save();
    painter->setPen(dark ? QColor(0x45, 0x45, 0x45) : QColor(0xe2, 0xe3, 0xe3));
    painter->drawLine(from, to);
    painter->setPen(dark ? QColor(0x1f, 0x1f, 0x1f) : QColor(Qt::white));
    painter->drawLine(from + step, to + step);
    painter->restore();
}

QColor headerText(const QPalette &palette)
{
    if (isDark(palette)) {
        QColor color = palette.color(QPalette::WindowText);
        color.setAlphaF(0.75);
        return color;
    }
    return QColor(0x4c, 0x60, 0x7a);
}
}

Win7Style::Win7Style()
{
    setObjectName(QStringLiteral("shellkit-win7"));
}

void Win7Style::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    const QPalette &palette = option->palette;
    const bool enabled = option->state & State_Enabled;
    const bool hover = enabled && (option->state & State_MouseOver);
    const bool pressed = enabled && (option->state & (State_Sunken | State_On));

    switch (element) {
    case PE_PanelButtonTool:
        if (option->state & State_AutoRaise) {
            // Round back/forward buttons draw their own glow (QIcon::Active).
            if (widget && widget->property("shellkitBare").toBool()) {
                return;
            }
            if (pressed || hover) {
                drawToolBox(painter, QRectF(option->rect), palette, pressed);
            }
            return;
        }
        [[fallthrough]];
    case PE_PanelButtonCommand:
        if (isDark(palette)) {
            drawBox(painter, QRectF(option->rect), QColor(255, 255, 255, pressed ? 0x0c : hover ? 0x1e : 0x14), QColor(255, 255, 255, pressed ? 0x08 : hover ? 0x14 : 0x0a), QColor(255, 255, 255, hover ? 0x50 : 0x38));
        } else if (pressed) {
            drawBox(painter, QRectF(option->rect), QColor(0xe5, 0xf4, 0xfc), QColor(0x98, 0xd1, 0xef), QColor(0x2c, 0x62, 0x8b));
        } else if (hover) {
            drawBox(painter, QRectF(option->rect), QColor(0xea, 0xf6, 0xfd), QColor(0xbe, 0xe6, 0xfd), QColor(0x3c, 0x7f, 0xb1));
        } else {
            drawBox(painter, QRectF(option->rect), QColor(0xf2, 0xf2, 0xf2), QColor(0xdd, 0xdd, 0xdd), QColor(0x70, 0x70, 0x70));
        }
        return;
    case PE_PanelLineEdit: {
        const auto lineEdit = qobject_cast<const QLineEdit *>(widget);
        if (lineEdit && !lineEdit->hasFrame()) {
            return;
        }
        const bool focus = option->state & State_HasFocus;
        QColor border;
        if (isDark(palette)) {
            border = focus ? QColor(0x56, 0x9d, 0xe5) : QColor(255, 255, 255, hover ? 0x60 : 0x40);
        } else {
            border = focus ? QColor(0x3d, 0x7b, 0xad) : hover ? QColor(0x7e, 0xb4, 0xea) : QColor(0xab, 0xad, 0xb3);
        }
        const QColor fill = isDark(palette) ? QColor(0x1e, 0x1e, 0x1e) : QColor(Qt::white);
        drawBox(painter, QRectF(option->rect), fill, fill, border, 2);
        return;
    }
    case PE_PanelItemViewItem: {
        const auto item = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        if (item && item->backgroundBrush.style() != Qt::NoBrush) {
            painter->fillRect(option->rect, item->backgroundBrush);
        }
        const bool selected = option->state & State_Selected;
        if (selected || hover) {
            drawItemBox(painter, QRectF(option->rect), palette, selected, hover, option->state & State_Active);
        }
        return;
    }
    case PE_PanelMenu: {
        // Windows 7 menus: a square light grey panel with a grey border.
        const bool dark = isDark(palette);
        painter->fillRect(option->rect, dark ? QColor(0x2b, 0x2b, 0x2b) : QColor(0xf0, 0xf0, 0xf0));
        painter->save();
        painter->setPen(dark ? QColor(0x5a, 0x5a, 0x5a) : QColor(0x97, 0x97, 0x97));
        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
        painter->restore();
        return;
    }
    case PE_IndicatorBranch:
        if (option->state & State_Children) {
            drawTreeTriangle(painter, QRectF(option->rect), option->state & State_Open, palette);
        }
        return;
    default:
        break;
    }
    Style::drawPrimitive(element, option, painter, widget);
}

void Win7Style::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    if (element == CE_Header) {
        // Windows 7 column headers: white, a full-height divider after each column, a light
        // blue box under the mouse, the sort direction as a small chevron above the label.
        const auto header = qstyleoption_cast<const QStyleOptionHeader *>(option);
        if (!header) {
            return;
        }
        const QPalette &palette = option->palette;
        const QRect r = option->rect;
        if (header->text.isEmpty()) {
            return;
        }
        const bool dark = isDark(palette);
        if (option->state & State_Enabled) {
            if (option->state & State_Sunken) {
                drawBox(painter, QRectF(r), dark ? QColor(255, 255, 255, 0x18) : QColor(0xe3, 0xec, 0xf6), dark ? QColor(255, 255, 255, 0x10) : QColor(0xd9, 0xe4, 0xf0), dark ? QColor(255, 255, 255, 0x30) : QColor(0xc0, 0xcb, 0xd9), 0);
            } else if (option->state & State_MouseOver) {
                drawBox(painter, QRectF(r), dark ? QColor(255, 255, 255, 0x12) : QColor(0xf3, 0xf8, 0xfd), dark ? QColor(255, 255, 255, 0x0a) : QColor(0xef, 0xf3, 0xf9), dark ? QColor(255, 255, 255, 0x28) : QColor(0xe3, 0xe8, 0xee), 0);
            }
        }
        painter->fillRect(QRect(r.right(), r.top(), 1, r.height()), dark ? QColor(255, 255, 255, 0x20) : QColor(0xe3, 0xe8, 0xee));
        const QRect text = r.adjusted(6, 0, -6, 0);
        painter->save();
        painter->setPen(headerText(palette));
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, option->fontMetrics.elidedText(header->text, Qt::ElideRight, text.width()));
        if (header->sortIndicator != QStyleOptionHeader::None) {
            // Dolphin passes SortDown for ascending order, shown as an upward chevron.
            const bool up = header->sortIndicator == QStyleOptionHeader::SortDown;
            const QPointF c(r.center().x(), r.top() + 4);
            QPainterPath path;
            path.moveTo(c.x() - 3.5, c.y() + (up ? 1.5 : -1.5));
            path.lineTo(c.x(), c.y() + (up ? -1.5 : 1.5));
            path.lineTo(c.x() + 3.5, c.y() + (up ? 1.5 : -1.5));
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(QPen(dark ? QColor(255, 255, 255, 0x90) : QColor(0x8a, 0x9a, 0xb0), 1.0));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(path);
        }
        painter->restore();
        return;
    }
    if (element == CE_MenuItem) {
        const auto item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (!item) {
            return;
        }
        const QRect r = option->rect;
        const int gutter = menuGutter(*item);
        // The icon column line runs past the items into the menu margins.
        drawMenuLine(painter, option->palette, QPoint(gutter, r.top() - MenuMargin), QPoint(gutter, r.bottom() + MenuMargin));
        if (item->menuItemType == QStyleOptionMenuItem::Separator) {
            drawMenuLine(painter, option->palette, QPoint(gutter + 2, r.center().y()), QPoint(r.right() - 1, r.center().y()));
            return;
        }
        // Fusion draws the content without its square highlight; the Windows 7 box goes below.
        const bool selected = (item->state & State_Selected) && (item->state & State_Enabled);
        if (selected) {
            if (isDark(option->palette)) {
                drawItemBox(painter, QRectF(r).adjusted(1, 0, -1, 0), option->palette, false, true, true);
            } else {
                drawBox(painter, QRectF(r).adjusted(1, 0, -1, 0), QColor(0xf2, 0xf7, 0xfd), QColor(0xdc, 0xe9, 0xf8), QColor(0xa9, 0xc9, 0xee));
            }
        } else if ((item->state & State_Selected)) {
            // A disabled item under the mouse: a plain grey box.
            drawBox(painter, QRectF(r).adjusted(1, 0, -1, 0), Qt::transparent, Qt::transparent, isDark(option->palette) ? QColor(0x5a, 0x5a, 0x5a) : QColor(0xc6, 0xc6, 0xc6));
        }
        QStyleOptionMenuItem plain(*item);
        plain.state &= ~State_Selected;
        plain.maxIconWidth += MenuIconExtra;
        QProxyStyle::drawControl(element, &plain, painter, widget);
        return;
    }
    Style::drawControl(element, option, painter, widget);
}

void Win7Style::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget) const
{
    if (control == CC_ScrollBar) {
        // Classic scroll bars with arrows and a track.
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }
    Style::drawComplexControl(control, option, painter, widget);
}

QRect Win7Style::subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl subControl, const QWidget *widget) const
{
    if (control == CC_ScrollBar) {
        return QProxyStyle::subControlRect(control, option, subControl, widget);
    }
    return Style::subControlRect(control, option, subControl, widget);
}

int Win7Style::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    switch (metric) {
    case PM_ToolBarItemMargin:
        return widget && widget->objectName() == QLatin1String("shellkitTabRow") ? 0 : 3;
    case PM_ToolBarItemSpacing:
        return 2;
    case PM_ScrollBarExtent:
        return 17;
    case PM_ScrollBarSliderMin:
        return QProxyStyle::pixelMetric(metric, option, widget);
    case PM_DockWidgetSeparatorExtent:
    case PM_SplitterWidth:
        return 3;
    case PM_MenuHMargin:
    case PM_MenuVMargin:
        return MenuMargin;
    default:
        return Style::pixelMetric(metric, option, widget);
    }
}

QSize Win7Style::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const
{
    switch (type) {
    case CT_ToolButton: {
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        const auto button = qstyleoption_cast<const QStyleOptionToolButton *>(option);
        if (button && isPaddedButton(*button)) {
            const bool chevron = (button->features & QStyleOptionToolButton::HasMenu) && !(button->features & QStyleOptionToolButton::PopupDelay);
            result.setWidth(size.width() + 2 * ButtonPadding + (chevron ? MenuChevronWidth : 0));
        }
        if (widget && widget->property("shellkitBare").toBool()) {
            return size;
        }
        // Command bar and address row buttons are 24 px high; icon buttons square-ish.
        if (qobject_cast<const QToolBar *>(widget ? widget->parentWidget() : nullptr)) {
            const bool iconOnly = button && (button->toolButtonStyle == Qt::ToolButtonIconOnly || button->text.isEmpty());
            result = result.expandedTo(QSize(iconOnly ? 28 : ControlHeight, ControlHeight));
        } else {
            result = result.expandedTo(QSize(22, 22));
        }
        return result;
    }
    case CT_LineEdit:
    case CT_HeaderSection: {
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        result.setHeight(qMax(result.height(), ControlHeight));
        return result;
    }
    case CT_PushButton: {
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        result.setHeight(qMax(result.height(), PushButtonHeight));
        result.setWidth(qMax(result.width() + 8, 75));
        return result;
    }
    case CT_MenuItem: {
        const auto item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        QStyleOptionMenuItem wider;
        if (item) {
            wider = *item;
            wider.maxIconWidth += MenuIconExtra;
        }
        QSize result = QProxyStyle::sizeFromContents(type, item ? &wider : option, size + QSize(item ? MenuIconExtra : 0, 0), widget);
        if (item && item->menuItemType == QStyleOptionMenuItem::Separator) {
            result.setHeight(MenuSeparatorHeight);
        } else {
            result.setHeight(qMax(result.height(), MenuItemHeight));
            result.rwidth() += 16;
        }
        return result;
    }
    default:
        return Style::sizeFromContents(type, option, size, widget);
    }
}
}
