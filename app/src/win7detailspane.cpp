/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "win7detailspane.h"

#include "shellkit.h"
#include "views/dolphinview.h"

#include <KIO/PreviewJob>
#include <KLocalizedString>

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr int IconSize = 48;
constexpr int FieldRows = 3;

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

QString dateText(const QDateTime &time)
{
    return QLocale().toString(time, QLocale::ShortFormat);
}
}

Win7DetailsPane::Win7DetailsPane(QWidget *parent)
    : QWidget(parent)
    , m_icon(new QLabel(this))
    , m_name(new QLabel(this))
    , m_type(new QLabel(this))
    , m_fields(new QGridLayout)
    , m_updateTimer(new QTimer(this))
{
    setFixedHeight(66);
    setAccessibleName(i18ndc(shellkitDomain, "@label", "Details pane"));

    m_icon->setFixedSize(IconSize + 8, IconSize);
    m_icon->setAlignment(Qt::AlignCenter);
    QFont nameFont = font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.15);
    m_name->setFont(nameFont);
    m_name->setTextFormat(Qt::PlainText);
    m_type->setTextFormat(Qt::PlainText);
    m_type->setForegroundRole(QPalette::PlaceholderText);

    auto textColumn = new QVBoxLayout;
    textColumn->setSpacing(2);
    textColumn->addStretch(1);
    textColumn->addWidget(m_name);
    textColumn->addWidget(m_type);
    textColumn->addStretch(1);

    m_fields->setHorizontalSpacing(6);
    m_fields->setVerticalSpacing(2);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(12);
    layout->addWidget(m_icon);
    layout->addLayout(textColumn);
    layout->addSpacing(24);
    layout->addLayout(m_fields);
    layout->addStretch(1);

    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(50);
    connect(m_updateTimer, &QTimer::timeout, this, &Win7DetailsPane::refresh);
}

Win7DetailsPane::~Win7DetailsPane()
{
    if (m_previewJob) {
        m_previewJob->kill();
    }
}

void Win7DetailsPane::setView(DolphinView *view)
{
    if (m_view == view) {
        return;
    }
    if (m_view) {
        m_view->disconnect(this);
    }
    m_view = view;
    if (m_view) {
        connect(m_view, &DolphinView::selectionChanged, this, &Win7DetailsPane::scheduleUpdate);
        connect(m_view, &DolphinView::itemCountChanged, this, &Win7DetailsPane::scheduleUpdate);
        connect(m_view, &DolphinView::directoryLoadingCompleted, this, &Win7DetailsPane::scheduleUpdate);
        connect(m_view, &DolphinView::urlChanged, this, &Win7DetailsPane::scheduleUpdate);
    }
    scheduleUpdate();
}

void Win7DetailsPane::paintEvent(QPaintEvent *)
{
    // A light band with a line on top, as below the files in Windows 7.
    QPainter painter(this);
    const bool dark = isDark(palette());
    QLinearGradient fill(0, 0, 0, height());
    fill.setColorAt(0, dark ? QColor(0x2b, 0x32, 0x3c) : QColor(0xf7, 0xfa, 0xfd));
    fill.setColorAt(1, dark ? QColor(0x23, 0x29, 0x31) : QColor(0xe4, 0xec, 0xf7));
    painter.fillRect(rect(), fill);
    painter.fillRect(QRect(0, 0, width(), 1), dark ? QColor(0x3d, 0x47, 0x55) : QColor(0xd5, 0xdf, 0xec));
}

void Win7DetailsPane::scheduleUpdate()
{
    m_updateTimer->start();
}

void Win7DetailsPane::addField(const QString &label, const QString &value)
{
    if (value.isEmpty()) {
        return;
    }
    const int index = m_fields->count() / 2;
    const int row = index % FieldRows;
    const int column = (index / FieldRows) * 2;
    auto labelWidget = new QLabel(label, this);
    labelWidget->setForegroundRole(QPalette::PlaceholderText);
    labelWidget->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto valueWidget = new QLabel(value, this);
    valueWidget->setTextFormat(Qt::PlainText);
    m_fields->addWidget(labelWidget, row, column);
    m_fields->addWidget(valueWidget, row, column + 1);
    if (column > 0) {
        m_fields->setColumnMinimumWidth(column, 0);
        labelWidget->setContentsMargins(24, 0, 0, 0);
    }
}

void Win7DetailsPane::setIcon(const KFileItem &item)
{
    if (m_previewJob) {
        m_previewJob->kill();
    }
    m_icon->setPixmap(QIcon::fromTheme(item.iconName()).pixmap(QSize(IconSize, IconSize), devicePixelRatioF()));
    if (item.isNull() || item.isDir()) {
        return;
    }
    // A thumbnail replaces the icon when one can be made, as Windows 7 shows it.
    const QStringList plugins = KIO::PreviewJob::defaultPlugins();
    m_previewJob = new KIO::PreviewJob(KFileItemList{item}, QSize(IconSize, IconSize) * devicePixelRatioF(), &plugins);
    m_previewJob->setScaleType(KIO::PreviewJob::ScaledAndCached);
    connect(m_previewJob, &KIO::PreviewJob::gotPreview, this, [this](const KFileItem &, const QPixmap &preview) {
        QPixmap pixmap = preview;
        pixmap.setDevicePixelRatio(devicePixelRatioF());
        m_icon->setPixmap(pixmap);
    });
}

void Win7DetailsPane::refresh()
{
    while (QLayoutItem *item = m_fields->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (!m_view) {
        return;
    }

    const KFileItemList selection = m_view->selectedItems();
    if (selection.isEmpty()) {
        const KFileItem folder = m_view->rootItem();
        setIcon(folder.isNull() ? KFileItem() : folder);
        if (folder.isNull()) {
            m_icon->setPixmap(QIcon::fromTheme(QStringLiteral("folder")).pixmap(QSize(IconSize, IconSize), devicePixelRatioF()));
        }
        m_name->setText(i18ndcp(shellkitDomain, "@info:status", "%1 item", "%1 items", m_view->itemsCount()));
        m_type->clear();
        return;
    }

    if (selection.count() == 1) {
        const KFileItem item = selection.first();
        setIcon(item);
        m_name->setText(item.text());
        m_type->setText(shellkitTypeName(item));
        addField(i18ndc(shellkitDomain, "@label", "Date modified:"), dateText(item.time(KFileItem::ModificationTime)));
        if (!item.isDir()) {
            addField(i18ndc(shellkitDomain, "@label", "Size:"), shellkitStatusSize(item.size()));
        }
        const QDateTime created = item.time(KFileItem::CreationTime);
        if (created.isValid()) {
            addField(i18ndc(shellkitDomain, "@label", "Date created:"), dateText(created));
        }
        return;
    }

    setIcon(selection.first());
    m_name->setText(i18ndcp(shellkitDomain, "@info:status", "%1 item selected", "%1 items selected", selection.count()));
    m_type->clear();
    qulonglong size = 0;
    bool hasFiles = false;
    QDateTime oldest;
    QDateTime newest;
    for (const KFileItem &item : selection) {
        if (!item.isDir()) {
            size += item.size();
            hasFiles = true;
        }
        const QDateTime modified = item.time(KFileItem::ModificationTime);
        if (modified.isValid()) {
            oldest = oldest.isValid() ? qMin(oldest, modified) : modified;
            newest = newest.isValid() ? qMax(newest, modified) : modified;
        }
    }
    if (oldest.isValid()) {
        const QString range = oldest.date() == newest.date() ? dateText(newest)
                                                              : i18ndc(shellkitDomain, "@label %1 and %2 are dates", "%1 - %2", QLocale().toString(oldest.date(), QLocale::ShortFormat), QLocale().toString(newest.date(), QLocale::ShortFormat));
        addField(i18ndc(shellkitDomain, "@label", "Date modified:"), range);
    }
    if (hasFiles) {
        addField(i18ndc(shellkitDomain, "@label", "Size:"), shellkitStatusSize(size));
    }
}

#include "moc_win7detailspane.cpp"
