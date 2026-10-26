/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "tilesection.h"

#include "shellkit.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
constexpr int TileWidth = 264;
constexpr int TileHeight = 70;
constexpr int TileIconSize = 48;
constexpr int BarHeight = 14;

QColor secondaryColor(const QPalette &palette)
{
    QColor color = palette.color(QPalette::Text);
    color.setAlphaF(0.75);
    return color;
}

/** A tile: the icon on the left; beside it the name, the capacity bar and the grey lines. */
class TileDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(TileWidth - 4, TileHeight - 4);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QWidget *widget = option.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        QStyleOptionViewItem panel(option);
        panel.rect = option.rect.adjusted(2, 2, -2, -2);
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &panel, painter, widget);

        const QRect r = panel.rect;
        const QRect iconRect(r.left() + 8, r.center().y() - TileIconSize / 2, TileIconSize, TileIconSize);
        index.data(Qt::DecorationRole).value<QIcon>().paint(painter, iconRect);

        const QFontMetrics fm = option.fontMetrics;
        const QStringList lines = index.data(TileRoles::LinesRole).toStringList();
        const QVariant usage = index.data(TileRoles::UsageRole);
        const int textLeft = iconRect.right() + 10;
        const int textWidth = r.right() - 8 - textLeft;
        const int lineCount = 1 + lines.count();
        const int contentHeight = lineCount * fm.height() + (usage.isValid() ? BarHeight + 4 : 0);
        int y = r.center().y() - contentHeight / 2;

        painter->save();
        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(QRect(textLeft, y, textWidth, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(index.data().toString(), Qt::ElideRight, textWidth));
        y += fm.height();
        if (usage.isValid()) {
            drawBar(painter, QRect(textLeft, y + 2, textWidth, BarHeight), usage.toDouble(), option.palette);
            y += BarHeight + 4;
        }
        painter->setPen(secondaryColor(option.palette));
        for (const QString &line : lines) {
            painter->drawText(QRect(textLeft, y, textWidth, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(line, Qt::ElideRight, textWidth));
            y += fm.height();
        }
        painter->restore();
    }

private:
    /** The capacity bar of Windows: accent blue, red once the drive is 90% full. */
    static void drawBar(QPainter *painter, const QRect &rect, double usage, const QPalette &palette)
    {
        const bool dark = palette.color(QPalette::Base).lightness() < 128;
        const bool full = usage >= 0.9;
        QColor fill = full ? QColor(0xda, 0x26, 0x26) : (shellkitWin7() ? QColor(0x26, 0xa0, 0xda) : palette.color(QPalette::Highlight));
        QColor track = dark ? QColor(0x3c, 0x3c, 0x3c) : QColor(0xe6, 0xe6, 0xe6);
        QColor border = dark ? QColor(0x5a, 0x5a, 0x5a) : QColor(0xbc, 0xbc, 0xbc);

        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(border);
        painter->setBrush(track);
        painter->drawRect(rect.adjusted(0, 0, -1, -1));
        const QRect inner = rect.adjusted(1, 1, -1, -1);
        const int used = qRound(inner.width() * qBound(0.0, usage, 1.0));
        if (used > 0) {
            const QRect bar(inner.left(), inner.top(), used, inner.height());
            if (shellkitWin7()) {
                // Windows 7 shades the bar from light at the top.
                QLinearGradient gradient(bar.topLeft(), bar.bottomLeft());
                gradient.setColorAt(0, fill.lighter(135));
                gradient.setColorAt(1, fill);
                painter->fillRect(bar, gradient);
            } else {
                painter->fillRect(bar, fill);
            }
        }
    }
};
}

TileSection::TileSection(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_header(new QToolButton(this))
    , m_list(new QListView(this))
    , m_model(new QStandardItemModel(this))
{
    // As high as the header and the tiles; the page's stretch takes the rest.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(4);

    // The header: a chevron and the title; a click folds the section.
    m_header->setText(title);
    m_header->setAutoRaise(true);
    m_header->setArrowType(Qt::DownArrow);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QFont font = m_header->font();
    font.setWeight(QFont::DemiBold);
    font.setPointSizeF(font.pointSizeF() * 1.1);
    m_header->setFont(font);
    connect(m_header, &QToolButton::clicked, this, [this]() {
        m_expanded = !m_expanded;
        m_header->setArrowType(m_expanded ? Qt::DownArrow : Qt::RightArrow);
        updateVisibility();
    });
    layout->addWidget(m_header, 0, Qt::AlignLeft);

    m_list->setModel(m_model);
    m_list->setViewMode(QListView::IconMode);
    m_list->setFlow(QListView::LeftToRight);
    m_list->setWrapping(true);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setGridSize(QSize(TileWidth, TileHeight));
    m_list->setUniformItemSizes(true);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setMouseTracking(true);
    m_list->viewport()->setAttribute(Qt::WA_Hover);
    m_list->viewport()->installEventFilter(this);
    m_list->setItemDelegate(new TileDelegate(m_list));
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListView::activated, this, &TileSection::activated);
    connect(m_list, &QListView::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = m_list->indexAt(pos);
        if (index.isValid()) {
            m_list->setCurrentIndex(index);
            Q_EMIT contextMenuRequested(index, m_list->viewport()->mapToGlobal(pos));
        }
    });
    layout->addWidget(m_list);

    connect(m_model, &QAbstractItemModel::rowsInserted, this, &TileSection::updateVisibility);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &TileSection::updateVisibility);
    connect(m_model, &QAbstractItemModel::modelReset, this, &TileSection::updateVisibility);
    updateVisibility();
}

QStandardItemModel *TileSection::model() const
{
    return m_model;
}

QListView *TileSection::list() const
{
    return m_list;
}

void TileSection::updateHeight(int width)
{
    const int columns = qMax(1, width / TileWidth);
    const int rows = (m_model->rowCount() + columns - 1) / columns;
    m_list->setFixedHeight(qMax(1, rows) * TileHeight + 4);
}

bool TileSection::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::MiddleButton) {
            const QModelIndex index = m_list->indexAt(mouseEvent->position().toPoint());
            if (index.isValid()) {
                Q_EMIT middleClicked(index);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void TileSection::updateVisibility()
{
    setVisible(m_model->rowCount() > 0);
    m_list->setVisible(m_expanded);
}

#include "moc_tilesection.cpp"
