#include "style.h"

#include <QAbstractButton>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>

namespace Shellkit
{
namespace
{
// WinUI ControlCornerRadius and OverlayCornerRadius.
constexpr qreal ControlRadius = 4;
constexpr qreal OverlayRadius = 8;
// Command bar and text box height, menu item height.
constexpr int ControlHeight = 32;
constexpr int MenuItemHeight = 30;
constexpr int ScrollBarExtent = 12;
// Room for the chevron after the label of a menu button.
constexpr int MenuChevronWidth = 16;
// Command bar: icon buttons 40 px wide, labelled buttons padded by 8 px, rows 48 px high.
constexpr int IconButtonWidth = 40;
constexpr int ButtonPadding = 8;
constexpr int ToolBarPadding = 8;
// Document-mode tabs (File Explorer): row height, the gap above the tab shapes, padding,
// close button size.
constexpr int TabRowHeight = 40;
constexpr int TabTop = 6;
constexpr int TabPadding = 12;
constexpr int TabCloseSize = 24;

/** Explorer-style tabs: a QTabBar in document mode. Dialog tabs keep the Fusion look. */
const QStyleOptionTab *documentTab(const QStyleOption *option)
{
    const auto tab = qstyleoption_cast<const QStyleOptionTab *>(option);
    return tab && tab->documentMode && tab->shape == QTabBar::RoundedNorth ? tab : nullptr;
}

/** The part of a document tab that the tab shape covers. */
QRect tabShapeRect(const QRect &rect)
{
    return rect.adjusted(0, TabTop, 0, 0);
}

bool inToolBar(const QWidget *widget)
{
    for (const QWidget *w = widget ? widget->parentWidget() : nullptr; w; w = w->parentWidget()) {
        if (qobject_cast<const QToolBar *>(w)) {
            return true;
        }
    }
    return false;
}

/** Text beside an icon, in one piece (not a split menu button): drawn with padding. */
bool isPaddedButton(const QStyleOptionToolButton &button)
{
    return button.toolButtonStyle == Qt::ToolButtonTextBesideIcon && !button.text.isEmpty()
        && !(button.features & QStyleOptionToolButton::MenuButtonPopup);
}

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

/** A WinUI translucent fill: white on dark, black on light, with the given opacities. */
QColor overlay(const QPalette &palette, qreal lightAlpha, qreal darkAlpha)
{
    QColor color = isDark(palette) ? QColor(Qt::white) : QColor(Qt::black);
    color.setAlphaF(isDark(palette) ? darkAlpha : lightAlpha);
    return color;
}

// SubtleFillColorSecondary / Tertiary: hover and pressed of borderless controls.
QColor subtleHover(const QPalette &p)
{
    return overlay(p, 0x09 / 255.0, 0x0f / 255.0);
}
QColor subtlePressed(const QPalette &p)
{
    return overlay(p, 0x06 / 255.0, 0x0a / 255.0);
}
// ControlFillColorDefault / Secondary / Tertiary: buttons and text boxes.
QColor controlFill(const QPalette &p, bool hover, bool pressed)
{
    if (isDark(p)) {
        QColor c(Qt::white);
        c.setAlphaF((pressed ? 0x08 : hover ? 0x15 : 0x0f) / 255.0);
        return c;
    }
    return pressed ? QColor(0xf9, 0xf9, 0xf9, 0x4d) : hover ? QColor(0xf9, 0xf9, 0xf9, 0x80) : QColor(0xff, 0xff, 0xff, 0xb3);
}
// ControlStrokeColorDefault and the stronger bottom edge of text boxes.
QColor controlStroke(const QPalette &p)
{
    return overlay(p, 0x0f / 255.0, 0x12 / 255.0);
}
QColor strongStroke(const QPalette &p)
{
    return overlay(p, 0x72 / 255.0, 0x8b / 255.0);
}
// Flyout surface: menus sit a step lighter than the window in dark mode.
QColor menuBackground(const QPalette &p)
{
    return isDark(p) ? QColor(0x2c, 0x2c, 0x2c) : QColor(0xf9, 0xf9, 0xf9);
}

void fillRounded(QPainter *painter, const QRectF &rect, const QColor &color, qreal radius = ControlRadius)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawRoundedRect(rect, radius, radius);
    painter->restore();
}

void drawChevron(QPainter *painter, const QRectF &rect, Qt::ArrowType direction, const QColor &color)
{
    const QPointF c = rect.center();
    const qreal s = qMin<qreal>(4, qMin(rect.width(), rect.height()) / 2 - 1);
    QPainterPath path;
    switch (direction) {
    case Qt::DownArrow:
        path.moveTo(c.x() - s, c.y() - s / 2);
        path.lineTo(c.x(), c.y() + s / 2);
        path.lineTo(c.x() + s, c.y() - s / 2);
        break;
    case Qt::UpArrow:
        path.moveTo(c.x() - s, c.y() + s / 2);
        path.lineTo(c.x(), c.y() - s / 2);
        path.lineTo(c.x() + s, c.y() + s / 2);
        break;
    case Qt::LeftArrow:
        path.moveTo(c.x() + s / 2, c.y() - s);
        path.lineTo(c.x() - s / 2, c.y());
        path.lineTo(c.x() + s / 2, c.y() + s);
        break;
    default:
        path.moveTo(c.x() - s / 2, c.y() - s);
        path.lineTo(c.x() + s / 2, c.y());
        path.lineTo(c.x() - s / 2, c.y() + s);
        break;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
    painter->restore();
}

QColor secondaryText(const QPalette &palette)
{
    QColor color = palette.color(QPalette::WindowText);
    color.setAlphaF(0.75);
    return color;
}
}

Style::Style()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
{
    setObjectName(QStringLiteral("shellkit-win11"));
}

void Style::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    const QPalette &palette = option->palette;
    const bool enabled = option->state & State_Enabled;
    const bool hover = enabled && (option->state & State_MouseOver);
    const bool pressed = enabled && (option->state & State_Sunken);

    switch (element) {
    case PE_PanelButtonTool: {
        // Command bar buttons are borderless; a fill shows only on hover, press or when checked.
        const bool on = option->state & State_On;
        if (option->state & State_AutoRaise) {
            const bool commandBar = inToolBar(widget);
            if (on && !pressed && !commandBar) {
                // A toggled button outside the command bar (the status bar layout buttons) is a
                // raised box, as in Windows.
                const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(overlay(palette, 0x24 / 255.0, 0x40 / 255.0));
                painter->setBrush(overlay(palette, 0x0f / 255.0, hover ? 0x3a / 255.0 : 0x33 / 255.0));
                painter->drawRoundedRect(rect, ControlRadius, ControlRadius);
                painter->restore();
                return;
            }
            if (pressed) {
                fillRounded(painter, option->rect, subtlePressed(palette));
            } else if (hover) {
                // Toggles in the command bar ("Details") show no state, as in Windows.
                fillRounded(painter, option->rect, subtleHover(palette));
            }
            return;
        }
        [[fallthrough]];
    }
    case PE_PanelButtonCommand: {
        const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(controlStroke(palette));
        painter->setBrush(controlFill(palette, hover, pressed || (option->state & State_On)));
        painter->drawRoundedRect(rect, ControlRadius, ControlRadius);
        painter->restore();
        return;
    }
    case PE_PanelLineEdit: {
        const auto lineEdit = qobject_cast<const QLineEdit *>(widget);
        if (lineEdit && !lineEdit->hasFrame()) {
            return; // Embedded editors: the surrounding control draws the field.
        }
        const bool focus = option->state & State_HasFocus;
        const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath shape;
        shape.addRoundedRect(rect, ControlRadius, ControlRadius);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        // TextControlBackgroundFocused is opaque; the resting field is a translucent control fill.
        const QColor fill = focus ? (isDark(palette) ? QColor(0x1e, 0x1e, 0x1e) : QColor(Qt::white)) : controlFill(palette, hover, false);
        painter->setPen(controlStroke(palette));
        painter->setBrush(fill);
        painter->drawPath(shape);
        // Bottom edge: a strong 1 px line, or a 2 px accent line while focused.
        painter->setClipPath(shape);
        const int edge = focus ? 2 : 1;
        painter->fillRect(QRectF(rect.left(), rect.bottom() - edge + 0.5, rect.width(), edge), focus ? palette.color(QPalette::Highlight) : strongStroke(palette));
        painter->restore();
        return;
    }
    case PE_PanelItemViewItem: {
        const auto item = qstyleoption_cast<const QStyleOptionViewItem *>(option);
        if (item && item->backgroundBrush.style() != Qt::NoBrush) {
            fillRounded(painter, option->rect, item->backgroundBrush.color());
        }
        // File list selection: light blue on light, a neutral lift on dark; the text keeps its colour.
        const QRectF rect = QRectF(option->rect).adjusted(1, 1, -1, -1);
        const bool selected = option->state & State_Selected;
        if (!selected && !hover) {
            return;
        }
        QColor fill;
        if (isDark(palette)) {
            fill = QColor(255, 255, 255, selected ? (hover ? 0x33 : 0x29) : 0x14);
        } else {
            fill = selected ? (hover ? QColor(0xc0, 0xe2, 0xff) : QColor(0xcc, 0xe8, 0xff)) : QColor(0xe5, 0xf3, 0xff);
        }
        if (selected && !(option->state & State_Active)) {
            fill.setAlphaF(fill.alphaF() * 0.75);
        }
        fillRounded(painter, rect, fill);
        return;
    }
    case PE_IndicatorBranch:
        if (option->state & State_Children) {
            drawChevron(painter, QRectF(option->rect), (option->state & State_Open) ? Qt::DownArrow : Qt::RightArrow, secondaryText(palette));
        }
        return;
    case PE_IndicatorArrowDown:
    case PE_IndicatorArrowUp:
    case PE_IndicatorArrowLeft:
    case PE_IndicatorArrowRight: {
        const Qt::ArrowType direction = element == PE_IndicatorArrowDown ? Qt::DownArrow
            : element == PE_IndicatorArrowUp                             ? Qt::UpArrow
            : element == PE_IndicatorArrowLeft                           ? Qt::LeftArrow
                                                                         : Qt::RightArrow;
        drawChevron(painter, QRectF(option->rect), direction, enabled ? secondaryText(palette) : palette.color(QPalette::Disabled, QPalette::WindowText));
        return;
    }
    case PE_PanelMenu: {
        // Translucent window (see polish()), so the rounded corners show the desktop behind.
        const QRectF rect = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(isDark(palette) ? QColor(0, 0, 0, 0x66) : QColor(0, 0, 0, 0x0f));
        painter->setBrush(palette.color(QPalette::Window));
        painter->drawRoundedRect(rect, OverlayRadius, OverlayRadius);
        painter->restore();
        return;
    }
    case PE_IndicatorTabClose: {
        // A thin cross on a rounded hover square.
        const QRectF r = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        if (hover || pressed) {
            fillRounded(painter, r, pressed ? subtlePressed(palette) : subtleHover(palette));
        }
        const QPointF c = r.center();
        const qreal s = 4.5;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(palette.color(QPalette::WindowText), 1.1, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(c.x() - s, c.y() - s), QPointF(c.x() + s, c.y() + s));
        painter->drawLine(QPointF(c.x() + s, c.y() - s), QPointF(c.x() - s, c.y() + s));
        painter->restore();
        return;
    }
    case PE_FrameTabBarBase: {
        const auto base = qstyleoption_cast<const QStyleOptionTabBarBase *>(option);
        if (base && base->documentMode) {
            return;
        }
        break;
    }
    case PE_IndicatorToolBarSeparator: {
        // A short divider line centred in the separator.
        QColor color = overlay(palette, 0x0f / 255.0, 0x15 / 255.0);
        const QRect r = option->rect;
        if (option->state & State_Horizontal) {
            painter->fillRect(QRect(r.center().x(), r.center().y() - ControlHeight / 2, 1, ControlHeight), color);
        } else {
            painter->fillRect(QRect(r.center().x() - 8, r.center().y(), 16, 1), color);
        }
        return;
    }
    case PE_FrameMenu:
    case PE_FrameLineEdit:
    case PE_FrameFocusRect:
    case PE_FrameButtonTool:
    case PE_FrameDockWidget:
    case PE_FrameStatusBarItem:
    case PE_FrameWindow:
    case PE_Frame:
    case PE_PanelToolBar:
    case PE_PanelStatusBar:
    case PE_IndicatorToolBarHandle:
    case PE_IndicatorButtonDropDown:
        return;
    case PE_IndicatorDockWidgetResizeHandle: {
        // A hairline between the panes, as between the navigation pane and the files.
        const QColor color = overlay(palette, 0x14 / 255.0, 0x14 / 255.0);
        const QRect r = option->rect;
        // The state flag names the dock orientation, not the separator's, so go by the shape.
        if (r.height() > r.width()) {
            painter->fillRect(QRect(r.center().x(), r.top(), 1, r.height()), color);
        } else {
            painter->fillRect(QRect(r.left(), r.center().y(), r.width(), 1), color);
        }
        return;
    }
    default:
        break;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void Style::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    switch (element) {
    case CE_ToolBar:
    case CE_MenuEmptyArea:
    case CE_MenuBarEmptyArea:
        return;
    case CE_TabBarTabShape: {
        // Windows 11 File Explorer tabs: the selected tab rises from the layer below it with
        // rounded top corners and curved feet; the others show a fill on hover only and a short
        // divider between them.
        const auto tab = documentTab(option);
        if (!tab) {
            break;
        }
        const QPalette &palette = option->palette;
        const QRectF r = tabShapeRect(tab->rect);
        const bool selected = tab->state & State_Selected;
        const bool hover = (tab->state & State_MouseOver) && (tab->state & State_Enabled);
        const qreal radius = OverlayRadius;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        if (selected) {
            QPainterPath path;
            path.moveTo(r.left() - radius, r.bottom() + 1);
            path.quadTo(r.left(), r.bottom() + 1, r.left(), r.bottom() + 1 - radius);
            path.lineTo(r.left(), r.top() + radius);
            path.quadTo(r.left(), r.top(), r.left() + radius, r.top());
            path.lineTo(r.right() + 1 - radius, r.top());
            path.quadTo(r.right() + 1, r.top(), r.right() + 1, r.top() + radius);
            path.lineTo(r.right() + 1, r.bottom() + 1 - radius);
            path.quadTo(r.right() + 1, r.bottom() + 1, r.right() + 1 + radius, r.bottom() + 1);
            path.closeSubpath();
            painter->setBrush(palette.color(QPalette::Base));
            painter->drawPath(path);
        } else {
            if (hover) {
                painter->setBrush(subtleHover(palette));
                painter->drawRoundedRect(r.adjusted(2, 0, -2, -4), radius, radius);
            }
            const bool nextSelected = tab->selectedPosition == QStyleOptionTab::NextIsSelected;
            const bool last = tab->position == QStyleOptionTab::End || tab->position == QStyleOptionTab::OnlyOneTab;
            if (!hover && !nextSelected && !last) {
                const QRectF line(r.right(), r.center().y() - 8, 1, 16);
                painter->fillRect(line, overlay(palette, 0x0f / 255.0, 0x15 / 255.0));
            }
        }
        painter->restore();
        return;
    }
    case CE_TabBarTabLabel: {
        const auto tab = documentTab(option);
        if (!tab) {
            break;
        }
        const QRect shape = tabShapeRect(tab->rect);
        const int iconSize = 16;
        if (!tab->icon.isNull()) {
            const QRect iconRect(shape.left() + TabPadding, shape.center().y() - iconSize / 2, iconSize, iconSize);
            // Scaled down from a large size: Fluent's 16 px icons are monochrome.
            const QPixmap pixmap = tab->icon.pixmap(QSize(64, 64), painter->device()->devicePixelRatioF());
            painter->save();
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            painter->drawPixmap(iconRect, pixmap);
            painter->restore();
        }
        const QRect textRect = subElementRect(SE_TabBarTabText, option, widget);
        const QString text = painter->fontMetrics().elidedText(tab->text, Qt::ElideRight, textRect.width());
        painter->save();
        painter->setPen(tab->palette.color(QPalette::WindowText));
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextHideMnemonic, text);
        painter->restore();
        return;
    }
    case CE_MenuItem: {
        const auto item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (!item) {
            break;
        }
        if (item->menuItemType == QStyleOptionMenuItem::Separator) {
            const QRect r = option->rect;
            painter->fillRect(QRect(r.left() - 4, r.center().y(), r.width() + 8, 1), overlay(option->palette, 0x0f / 255.0, 0x15 / 255.0));
            return;
        }
        // Fusion draws the content without its square highlight; the rounded hover goes on top.
        QStyleOptionMenuItem plain(*item);
        const bool selected = (item->state & State_Selected) && (item->state & State_Enabled);
        plain.state &= ~State_Selected;
        QProxyStyle::drawControl(element, &plain, painter, widget);
        if (selected) {
            fillRounded(painter, QRectF(option->rect).adjusted(0, 1, 0, -1), subtleHover(option->palette));
        }
        return;
    }
    case CE_Header: {
        // Details view column headers: no fill at rest, a divider after each column, the sort
        // direction as a small chevron above the middle of the label.
        const auto header = qstyleoption_cast<const QStyleOptionHeader *>(option);
        if (!header) {
            break;
        }
        const QPalette &palette = option->palette;
        const QRect r = option->rect;
        if (header->text.isEmpty()) {
            return;
        }
        if (option->state & State_Enabled) {
            if (option->state & State_Sunken) {
                fillRounded(painter, QRectF(r).adjusted(0, 2, -1, -2), subtlePressed(palette));
            } else if (option->state & State_MouseOver) {
                fillRounded(painter, QRectF(r).adjusted(0, 2, -1, -2), subtleHover(palette));
            }
        }
        painter->fillRect(QRect(r.right(), r.top() + 6, 1, r.height() - 12), overlay(palette, 0x15 / 255.0, 0x1f / 255.0));
        const QRect text = r.adjusted(8, 0, -8, 0);
        painter->save();
        painter->setPen(secondaryText(palette));
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, option->fontMetrics.elidedText(header->text, Qt::ElideRight, text.width()));
        painter->restore();
        if (header->sortIndicator != QStyleOptionHeader::None) {
            // Dolphin passes SortDown for ascending order; Windows shows ascending as an up chevron.
            const Qt::ArrowType direction = header->sortIndicator == QStyleOptionHeader::SortDown ? Qt::UpArrow : Qt::DownArrow;
            drawChevron(painter, QRectF(r.center().x() - 5, r.top(), 10, 8), direction, secondaryText(palette));
        }
        return;
    }
    case CE_MenuBarItem: {
        const auto item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (!item) {
            break;
        }
        QStyleOptionMenuItem plain(*item);
        const bool selected = item->state & (State_Selected | State_Sunken);
        plain.state &= ~(State_Selected | State_Sunken);
        QProxyStyle::drawControl(element, &plain, painter, widget);
        if (selected) {
            fillRounded(painter, QRectF(option->rect).adjusted(2, 2, -2, -2), subtleHover(option->palette));
        }
        return;
    }
    default:
        break;
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void Style::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget) const
{
    if (control == CC_ToolButton) {
        const auto button = qstyleoption_cast<const QStyleOptionToolButton *>(option);
        if (!button) {
            return;
        }
        QStyleOptionToolButton plain(*button);
        // Back/forward keep their history on a long press, and icon-only menu buttons ("See more")
        // open their menu, both without an arrow, as in Windows.
        if ((plain.features & QStyleOptionToolButton::PopupDelay) || plain.toolButtonStyle == Qt::ToolButtonIconOnly || plain.text.isEmpty()) {
            plain.features &= ~QStyleOptionToolButton::HasMenu;
        }
        if (!isPaddedButton(plain)) {
            QProxyStyle::drawComplexControl(control, &plain, painter, widget);
            return;
        }
        // Labelled buttons ("Details") get padding; menu buttons ("New", "Sort", "View") a
        // chevron right after the label.
        const bool chevron = plain.features & QStyleOptionToolButton::HasMenu;
        plain.features &= ~QStyleOptionToolButton::HasMenu;
        drawPrimitive(PE_PanelButtonTool, &plain, painter, widget);
        QStyleOptionToolButton label(plain);
        label.rect = plain.rect.adjusted(ButtonPadding, 0, -ButtonPadding - (chevron ? MenuChevronWidth : 0), 0);
        drawControl(CE_ToolButtonLabel, &label, painter, widget);
        if (chevron) {
            const QRectF arrow(label.rect.right() + 4, plain.rect.top(), MenuChevronWidth - 4, plain.rect.height());
            drawChevron(painter, arrow, Qt::DownArrow, secondaryText(option->palette));
        }
        return;
    }
    if (control == CC_ScrollBar) {
        // Windows 11 scroll bars: no track or arrows, a thin pill that widens under the mouse.
        const auto bar = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (!bar || bar->maximum == bar->minimum) {
            return;
        }
        const bool hover = (option->state & State_MouseOver) && (option->state & State_Enabled);
        const QRect slider = subControlRect(CC_ScrollBar, option, SC_ScrollBarSlider, widget);
        const qreal thickness = hover ? 6 : 2;
        QRectF thumb;
        if (bar->orientation == Qt::Horizontal) {
            thumb = QRectF(slider.left() + 2, option->rect.center().y() + 0.5 - thickness / 2, slider.width() - 4, thickness);
        } else {
            thumb = QRectF(option->rect.center().x() + 0.5 - thickness / 2, slider.top() + 2, thickness, slider.height() - 4);
        }
        QColor color = option->palette.color(QPalette::WindowText);
        color.setAlphaF(hover ? 0.55 : 0.4);
        fillRounded(painter, thumb, color, thickness / 2);
        return;
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

QRect Style::subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl subControl, const QWidget *widget) const
{
    if (control == CC_ScrollBar) {
        // The whole bar is the groove: no line buttons.
        const auto bar = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (!bar) {
            return QRect();
        }
        const QRect r = bar->rect;
        const bool horizontal = bar->orientation == Qt::Horizontal;
        const int length = horizontal ? r.width() : r.height();
        const int range = bar->maximum - bar->minimum;
        int sliderLength = length;
        if (range > 0) {
            sliderLength = int(qint64(bar->pageStep) * length / (range + bar->pageStep));
            sliderLength = qBound(pixelMetric(PM_ScrollBarSliderMin, option, widget), sliderLength, length);
        }
        const int start = sliderPositionFromValue(bar->minimum, bar->maximum, bar->sliderPosition, length - sliderLength, bar->upsideDown);
        QRect result;
        switch (subControl) {
        case SC_ScrollBarGroove:
            result = r;
            break;
        case SC_ScrollBarSlider:
            result = horizontal ? QRect(r.x() + start, r.y(), sliderLength, r.height()) : QRect(r.x(), r.y() + start, r.width(), sliderLength);
            break;
        case SC_ScrollBarSubPage:
            result = horizontal ? QRect(r.x(), r.y(), start, r.height()) : QRect(r.x(), r.y(), r.width(), start);
            break;
        case SC_ScrollBarAddPage:
            result = horizontal ? QRect(r.x() + start + sliderLength, r.y(), length - start - sliderLength, r.height())
                                : QRect(r.x(), r.y() + start + sliderLength, r.width(), length - start - sliderLength);
            break;
        default:
            break;
        }
        return visualRect(bar->direction, r, result);
    }
    return QProxyStyle::subControlRect(control, option, subControl, widget);
}

QRect Style::subElementRect(SubElement element, const QStyleOption *option, const QWidget *widget) const
{
    if (const auto tab = documentTab(option)) {
        const QRect shape = tabShapeRect(tab->rect);
        const int closeLeft = shape.right() + 1 - 8 - TabCloseSize;
        switch (element) {
        case SE_TabBarTabRightButton:
            return QRect(closeLeft, shape.center().y() + 1 - TabCloseSize / 2, TabCloseSize, TabCloseSize);
        case SE_TabBarTabLeftButton:
            return QRect();
        case SE_TabBarTabText: {
            const int left = shape.left() + TabPadding + (tab->icon.isNull() ? 0 : 16 + 8);
            const int right = tab->rightButtonSize.isEmpty() ? shape.right() - TabPadding : closeLeft - 4;
            return QRect(left, shape.top(), qMax(0, right - left + 1), shape.height());
        }
        default:
            break;
        }
    }
    return QProxyStyle::subElementRect(element, option, widget);
}

int Style::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    switch (metric) {
    case PM_TabCloseIndicatorWidth:
    case PM_TabCloseIndicatorHeight:
        return TabCloseSize;
    case PM_ToolBarItemMargin:
        // The tab row sits right under the title bar; other toolbars are 48 px rows.
        return widget && widget->objectName() == QLatin1String("shellkitTabRow") ? 0 : ToolBarPadding;
    case PM_ToolBarFrameWidth:
    case PM_ToolBarHandleExtent:
    case PM_DockWidgetFrameWidth:
    case PM_DockWidgetTitleBarButtonMargin:
        return 0;
    case PM_ToolBarItemSpacing:
        return 8;
    case PM_ToolBarSeparatorExtent:
        return 9;
    case PM_ToolBarIconSize:
    case PM_SmallIconSize:
    case PM_ButtonIconSize:
        return 16;
    case PM_ScrollBarExtent:
        return ScrollBarExtent;
    case PM_ScrollBarSliderMin:
        return 32;
    case PM_MenuPanelWidth:
        return 1;
    case PM_MenuHMargin:
    case PM_MenuVMargin:
        return 4;
    case PM_DockWidgetSeparatorExtent:
    case PM_SplitterWidth:
        return 4;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int Style::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget, QStyleHintReturn *returnData) const
{
    switch (hint) {
    case SH_ItemView_ShowDecorationSelected:
    case SH_UnderlineShortcut:
    case SH_EtchDisabledText:
    case SH_DialogButtonBox_ButtonsHaveIcons:
    case SH_Widget_Animation_Duration:
        return 0;
    case SH_Menu_Scrollable:
        return 1;
    default:
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
}

QSize Style::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const
{
    QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
    switch (type) {
    case CT_ToolButton: {
        const auto button = qstyleoption_cast<const QStyleOptionToolButton *>(option);
        if (button && isPaddedButton(*button)) {
            const bool chevron = (button->features & QStyleOptionToolButton::HasMenu) && !(button->features & QStyleOptionToolButton::PopupDelay);
            result.setWidth(size.width() + 2 * ButtonPadding + (chevron ? MenuChevronWidth : 0));
        }
        // Command bar and address row buttons are 32 px high (icon buttons 40 px wide); smaller
        // ones elsewhere keep 24 px.
        if (qobject_cast<const QToolBar *>(widget ? widget->parentWidget() : nullptr)) {
            const bool iconOnly = button && (button->toolButtonStyle == Qt::ToolButtonIconOnly || button->text.isEmpty());
            result = result.expandedTo(QSize(iconOnly ? IconButtonWidth : ControlHeight, ControlHeight));
        } else {
            result = result.expandedTo(QSize(24, 24));
        }
        break;
    }
    case CT_TabBarTab:
        if (documentTab(option)) {
            // The label, its padding and the close button; QTabBar adds the button width.
            result = QSize(size.width() + 2 * TabPadding + 8, TabRowHeight);
        }
        break;
    case CT_LineEdit:
    case CT_HeaderSection:
        result.setHeight(qMax(result.height(), ControlHeight));
        break;
    case CT_PushButton:
        result.setHeight(qMax(result.height(), ControlHeight));
        result.rwidth() += 8;
        break;
    case CT_MenuItem: {
        const auto item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
        if (item && item->menuItemType == QStyleOptionMenuItem::Separator) {
            result.setHeight(9);
        } else {
            result.setHeight(qMax(result.height(), MenuItemHeight));
            result.rwidth() += 16;
        }
        break;
    }
    default:
        break;
    }
    return result;
}

void Style::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    if (auto menu = qobject_cast<QMenu *>(widget)) {
        menu->setAttribute(Qt::WA_TranslucentBackground);
        QPalette palette = menu->palette();
        palette.setColor(QPalette::Window, menuBackground(palette));
        menu->setPalette(palette);
    } else if (qobject_cast<QScrollBar *>(widget) || qobject_cast<QLineEdit *>(widget) || qobject_cast<QAbstractButton *>(widget)
               || qobject_cast<QTabBar *>(widget)) {
        widget->setAttribute(Qt::WA_Hover);
    }
}

void Style::unpolish(QWidget *widget)
{
    if (auto menu = qobject_cast<QMenu *>(widget)) {
        menu->setAttribute(Qt::WA_TranslucentBackground, false);
        menu->setPalette(QPalette());
    }
    QProxyStyle::unpolish(widget);
}
}
