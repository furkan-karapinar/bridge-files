/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "navigationpanel.h"

#include "dolphinplacesmodelsingleton.h"
#include "pages/thispcpage.h"
#include "shellkit.h"

#include <KFilePlacesModel>
#include <KLocalizedString>
#include <KPropertiesDialog>
#include <Solid/Device>

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>

namespace
{
enum Role {
    UrlRole = Qt::UserRole + 1,
    SeparatorRole,
    PlaceholderRole,
    PopulatedRole,
    UdiRole,
    PinnedRole,
    FallbackRole, // Selected only when no other row shows the folder (Gallery).
};

constexpr int RowHeight = 32;
constexpr int SeparatorHeight = 13;
// Windows 7: 22 px rows, groups apart by a blank gap.
constexpr int Win7RowHeight = 22;
constexpr int Win7SeparatorHeight = 10;
constexpr int RowInset = 4;
constexpr qreal RowRadius = 4;

/**
 * Windows 11 shows colour icons in the navigation pane; the theme's 16 px places are
 * monochrome, so the large (colour) artwork is scaled down instead.
 */
QIcon colourIcon(const QIcon &themed)
{
    const QPixmap large = themed.pixmap(QSize(64, 64));
    if (large.isNull()) {
        return themed;
    }
    QIcon icon;
    for (const int size : {16, 32}) {
        icon.addPixmap(large.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    return icon;
}

/** A monochrome theme icon painted in one colour (the orange Home icon). */
QIcon tintedIcon(const QIcon &themed, const QColor &color)
{
    QIcon icon;
    for (const int size : {16, 32}) {
        QPixmap pixmap = themed.pixmap(QSize(size, size));
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

/** The pin shown after pinned folders, as in Quick access. */
void drawPin(QPainter *painter, const QRectF &rect, const QColor &color)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(rect.center());
    painter->rotate(45);
    painter->setPen(QPen(color, 1.0));
    painter->setBrush(Qt::NoBrush);
    // Head, collar and needle of a push pin, pointing down-left.
    painter->drawRoundedRect(QRectF(-2.5, -6, 5, 6), 1, 1);
    painter->drawLine(QPointF(-4.5, 0), QPointF(4.5, 0));
    painter->drawLine(QPointF(0, 0), QPointF(0, 6));
    painter->restore();
}

bool hasSubfolders(const QString &path)
{
    QDirIterator it(path, QDir::Dirs | QDir::NoDotAndDotDot);
    return it.hasNext();
}

/** Draws separators and leaves hover and selection to NavigationTreeView::drawRow(). */
class NavigationDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        if (shellkitWin7()) {
            size.setHeight(index.data(SeparatorRole).toBool() ? Win7SeparatorHeight : Win7RowHeight);
        } else {
            size.setHeight(index.data(SeparatorRole).toBool() ? SeparatorHeight : RowHeight);
        }
        return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (index.data(SeparatorRole).toBool()) {
            if (shellkitWin7()) {
                return;
            }
            QColor line = option.palette.color(QPalette::WindowText);
            line.setAlphaF(0.12);
            const int y = option.rect.center().y();
            painter->fillRect(QRect(RowInset + 8, y, option.widget->width() - 2 * (RowInset + 8), 1), line);
            return;
        }
        QStyleOptionViewItem opt(option);
        opt.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        const bool pinned = index.data(PinnedRole).toBool();
        if (pinned) {
            opt.rect.adjust(0, 0, -24, 0);
        }
        QStyledItemDelegate::paint(painter, opt, index);
        if (pinned) {
            QColor color = option.palette.color(QPalette::WindowText);
            color.setAlphaF(0.6);
            drawPin(painter, QRectF(option.widget->width() - RowInset - 24, option.rect.top(), 16, option.rect.height()), color);
        }
    }
};
}

/** Tree with Windows 11 rows: a rounded hover/selection fill across the whole row. */
class NavigationTreeView : public QTreeView
{
    Q_OBJECT

public:
    explicit NavigationTreeView(QWidget *parent)
        : QTreeView(parent)
    {
        setMouseTracking(true);
    }

Q_SIGNALS:
    void itemClicked(const QModelIndex &index, Qt::MouseButton button);

protected:
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool selected = selectionModel()->isSelected(index);
        const bool hovered = index == m_hovered;
        if (shellkitWin7() && (selected || hovered) && !index.data(SeparatorRole).toBool()) {
            // Windows 7: the style's selection box, from the icon to the right edge.
            QStyleOptionViewItem box(option);
            const int left = visualRect(index).left() - 2;
            box.rect = QRect(left, option.rect.top(), viewport()->width() - left - 2, option.rect.height());
            box.state = QStyle::State_Enabled | (hasFocus() ? QStyle::State_Active : QStyle::State_None);
            box.state |= selected ? QStyle::State_Selected : QStyle::State_None;
            box.state |= hovered ? QStyle::State_MouseOver : QStyle::State_None;
            style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &box, painter, this);
        } else if ((selected || hovered) && !index.data(SeparatorRole).toBool()) {
            QColor fill = palette().color(QPalette::WindowText);
            fill.setAlphaF(selected ? (hovered ? 0.12 : 0.09) : 0.06);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            const QRectF row = QRectF(option.rect).adjusted(RowInset, 1, -RowInset, -1);
            painter->drawRoundedRect(QRectF(RowInset, row.top(), viewport()->width() - 2 * RowInset, row.height()), RowRadius, RowRadius);
            painter->restore();
        }
        QTreeView::drawRow(painter, option, index);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QModelIndex index = indexAt(event->position().toPoint());
        if (index != m_hovered) {
            m_hovered = index;
            viewport()->update();
        }
        QTreeView::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hovered = QPersistentModelIndex();
        viewport()->update();
        QTreeView::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_pressed = indexAt(event->position().toPoint());
        QTreeView::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const QModelIndex index = indexAt(event->position().toPoint());
        // A click on the expand arrow only expands; it lies left of the item's visual rect.
        const bool onItem = visualRect(index).contains(event->position().toPoint());
        QTreeView::mouseReleaseEvent(event);
        if (index.isValid() && index == m_pressed && onItem) {
            Q_EMIT itemClicked(index, event->button());
        }
        m_pressed = QPersistentModelIndex();
    }

private:
    QPersistentModelIndex m_hovered;
    QPersistentModelIndex m_pressed;
};

NavigationPanel::NavigationPanel(QWidget *parent)
    : Panel(parent)
    , m_view(new NavigationTreeView(this))
    , m_model(new QStandardItemModel(this))
    , m_rebuildTimer(new QTimer(this))
{
    m_view->setModel(m_model);
    m_view->setItemDelegate(new NavigationDelegate(m_view));
    m_view->setHeaderHidden(true);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setIconSize(QSize(16, 16));
    m_view->setIndentation(shellkitWin7() ? 14 : 16);
    m_view->setUniformRowHeights(false);
    m_view->setExpandsOnDoubleClick(true);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setAccessibleName(i18ndc(shellkitDomain, "@label", "Navigation pane"));
    // The pane has the window colour in Windows 11, not the base colour of item views.
    m_view->viewport()->setAutoFillBackground(false);
    m_view->setAutoFillBackground(false);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->addWidget(m_view);

    connect(m_view, &QTreeView::expanded, this, &NavigationPanel::slotExpanded);
    connect(m_view, &NavigationTreeView::itemClicked, this, &NavigationPanel::activate);
    connect(m_view, &QTreeView::activated, this, [this](const QModelIndex &index) {
        // Keyboard activation; mouse clicks arrive through itemClicked.
        if (!(QApplication::mouseButtons() & Qt::LeftButton)) {
            activate(index, Qt::LeftButton);
        }
    });
    connect(m_view, &QWidget::customContextMenuRequested, this, &NavigationPanel::showContextMenu);

    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(200);
    connect(m_rebuildTimer, &QTimer::timeout, this, &NavigationPanel::rebuild);

    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    auto scheduleRebuild = [this]() {
        m_rebuildTimer->start();
    };
    connect(places, &QAbstractItemModel::rowsInserted, this, scheduleRebuild);
    connect(places, &QAbstractItemModel::rowsRemoved, this, scheduleRebuild);
    connect(places, &QAbstractItemModel::modelReset, this, scheduleRebuild);
    connect(places, &KFilePlacesModel::reloaded, this, scheduleRebuild);
    connect(places, &KFilePlacesModel::errorMessage, this, &NavigationPanel::errorMessage);
    connect(places, &KFilePlacesModel::setupDone, this, [this, places](const QModelIndex &index, bool success) {
        if (success && places->deviceForIndex(index).udi() == m_pendingSetupUdi) {
            Q_EMIT folderActivated(places->url(index));
        }
        m_pendingSetupUdi.clear();
        m_rebuildTimer->start();
    });

    rebuild();
}

NavigationPanel::~NavigationPanel() = default;

bool NavigationPanel::urlChanged()
{
    selectCurrentUrl();
    return true;
}

void NavigationPanel::changeEvent(QEvent *event)
{
    Panel::changeEvent(event);
    // Tinted and monochrome icons are drawn in the colours of the old scheme; draw them again.
    if (event->type() == QEvent::PaletteChange && m_model->rowCount() > 0) {
        QTimer::singleShot(0, this, &NavigationPanel::rebuild);
    }
}

void NavigationPanel::rebuild()
{
    // Remember what was open so the tree looks the same after a device or place change.
    std::function<void(const QModelIndex &)> collect = [&](const QModelIndex &parent) {
        for (int row = 0; row < m_model->rowCount(parent); ++row) {
            const QModelIndex index = m_model->index(row, 0, parent);
            if (m_view->isExpanded(index)) {
                m_expandedUrls.insert(index.data(UrlRole).toUrl());
                collect(index);
            }
        }
    };
    if (m_model->rowCount() > 0) {
        m_expandedUrls.clear();
        collect(QModelIndex());
    }
    const bool firstBuild = m_model->rowCount() == 0;
    m_model->clear();

    if (shellkitWin7()) {
        rebuildWin7(firstBuild);
        return;
    }

    addItem(nullptr, tintedIcon(QIcon::fromTheme(QStringLiteral("go-home")), QColor(0xee, 0x83, 0x08)), i18ndc(shellkitDomain, "@item:inlistbox Start page of the file explorer", "Home"), shellkitHomeUrl(), false);
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    addItem(nullptr,
            QIcon::fromTheme(QStringLiteral("folder-pictures")),
            i18ndc(shellkitDomain, "@item:inlistbox Photo collection", "Gallery"),
            QUrl::fromLocalFile(pictures),
            false)
        ->setData(true, FallbackRole);

    addSeparator();
    addPinnedFolders();
    addSeparator();

    QStandardItem *thisPc =
        addItem(nullptr, QIcon::fromTheme(QStringLiteral("computer")), i18ndc(shellkitDomain, "@item:inlistbox", "This PC"), shellkitThisPcUrl(), false);
    addDrives(thisPc);
    thisPc->setData(true, PopulatedRole);

    addItem(nullptr, QIcon::fromTheme(QStringLiteral("network-workgroup")), i18ndc(shellkitDomain, "@item:inlistbox", "Network"), shellkitNetworkUrl(), false);

    // Expanding re-populates through slotExpanded(), which expands remembered children in turn.
    // This PC starts open, so the root folder is in sight.
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex index = m_model->index(row, 0);
        const QUrl url = index.data(UrlRole).toUrl();
        if (((firstBuild && url == shellkitThisPcUrl()) || m_expandedUrls.contains(url)) && m_model->hasChildren(index)) {
            m_view->expand(index);
        }
    }
    selectCurrentUrl();
}

void NavigationPanel::rebuildWin7(bool firstBuild)
{
    // Windows 7 groups: Favorites (the pinned folders), Libraries (the user folders),
    // Computer (the drives) and Network.
    // A yellow star as in Windows 7, the same on light and dark.
    QStandardItem *favorites = addItem(nullptr, tintedIcon(QIcon::fromTheme(QStringLiteral("starred"), QIcon::fromTheme(QStringLiteral("emblem-favorite"))), QColor(0xf2, 0xb7, 0x1c)),
                                       i18ndc(shellkitDomain, "@item:inlistbox Windows 7 navigation pane group", "Favorites"), QUrl(), false);
    addPinnedFolders(favorites);
    addItem(favorites,
            QIcon::fromTheme(QStringLiteral("document-open-recent")),
            i18ndc(shellkitDomain, "@item:inlistbox Recently used folders", "Recent Places"),
            QUrl(QStringLiteral("recentlyused:/locations/")),
            false);
    favorites->setData(true, PopulatedRole);
    addSeparator();

    QStandardItem *libraries = addItem(nullptr, QIcon::fromTheme(QStringLiteral("folder-library"), QIcon::fromTheme(QStringLiteral("folder"))),
                                       i18ndc(shellkitDomain, "@item:inlistbox Windows 7 navigation pane group", "Libraries"), QUrl::fromLocalFile(QDir::homePath()), false);
    const struct {
        QStandardPaths::StandardLocation location;
        const char *icon;
    } userFolders[] = {
        {QStandardPaths::DocumentsLocation, "folder-documents"},
        {QStandardPaths::MusicLocation, "folder-music"},
        {QStandardPaths::PicturesLocation, "folder-pictures"},
        {QStandardPaths::MoviesLocation, "folder-videos"},
    };
    for (const auto &folder : userFolders) {
        const QString path = QStandardPaths::writableLocation(folder.location);
        if (!path.isEmpty() && QDir(path).exists() && path != QDir::homePath()) {
            addItem(libraries, QIcon::fromTheme(QLatin1String(folder.icon)), QDir(path).dirName(), QUrl::fromLocalFile(path));
        }
    }
    libraries->setData(true, PopulatedRole);
    addSeparator();

    QStandardItem *computer =
        addItem(nullptr, QIcon::fromTheme(QStringLiteral("computer")), i18ndc(shellkitDomain, "@item:inlistbox", "Computer"), shellkitThisPcUrl(), false);
    addDrives(computer);
    computer->setData(true, PopulatedRole);
    addSeparator();

    addItem(nullptr, QIcon::fromTheme(QStringLiteral("network-workgroup")), i18ndc(shellkitDomain, "@item:inlistbox", "Network"), shellkitNetworkUrl(), false);

    // The groups start open, as in Windows 7; later rebuilds keep what the user opened.
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex index = m_model->index(row, 0);
        if ((firstBuild || m_expandedUrls.contains(index.data(UrlRole).toUrl())) && m_model->hasChildren(index)) {
            m_view->expand(index);
        }
    }
    selectCurrentUrl();
}

void NavigationPanel::addPinnedFolders(QStandardItem *parent)
{
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    const QUrl home = QUrl::fromLocalFile(QDir::homePath());
    for (int row = 0; row < places->rowCount(); ++row) {
        const QModelIndex index = places->index(row, 0);
        if (places->groupType(index) != KFilePlacesModel::PlacesType || places->isHidden(index)) {
            continue;
        }
        const QUrl url = places->url(index);
        if (url.matches(home, QUrl::StripTrailingSlash) || url.scheme() == QLatin1String("trash") || url.path() == QLatin1String("/")) {
            continue;
        }
        // Under Windows 7's Favorites the group says it; no pin glyph.
        addItem(parent, places->icon(index), places->text(index), url)->setData(parent == nullptr, PinnedRole);
    }
}

void NavigationPanel::addDrives(QStandardItem *thisPc)
{
    // The root file system first, as C: on Windows; the drives as the This PC page names them.
    addItem(thisPc,
            QIcon::fromTheme(QStringLiteral("drive-harddisk")),
            i18ndc(shellkitDomain, "@item:inlistbox %1 is a mount point", "Local Disk (%1)", QStringLiteral("/")),
            QUrl::fromLocalFile(QStringLiteral("/")));
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    for (int row = 0; row < places->rowCount(); ++row) {
        const QModelIndex index = places->index(row, 0);
        const auto group = places->groupType(index);
        if ((group != KFilePlacesModel::DevicesType && group != KFilePlacesModel::RemovableDevicesType) || places->isHidden(index)) {
            continue;
        }
        const QUrl url = places->url(index);
        if (url.isLocalFile() && url.toLocalFile() == QLatin1String("/")) {
            continue;
        }
        QStandardItem *drive = addItem(thisPc, places->icon(index), ThisPcPage::driveName(places, index), url);
        drive->setData(places->deviceForIndex(index).udi(), UdiRole);
    }
}

QStandardItem *NavigationPanel::addItem(QStandardItem *parent, const QIcon &icon, const QString &text, const QUrl &url, bool expandable)
{
    auto item = new QStandardItem(colourIcon(icon), text);
    item->setData(url, UrlRole);
    item->setToolTip(url.isLocalFile() ? url.toLocalFile() : url.toDisplayString());
    if (expandable && url.isLocalFile() && hasSubfolders(url.toLocalFile())) {
        // Placeholder so the expand arrow shows; the real children come in populate().
        auto placeholder = new QStandardItem;
        placeholder->setData(true, PlaceholderRole);
        placeholder->setFlags(Qt::NoItemFlags);
        item->appendRow(placeholder);
    }
    (parent ? parent : m_model->invisibleRootItem())->appendRow(item);
    return item;
}

void NavigationPanel::addSeparator()
{
    auto separator = new QStandardItem;
    separator->setData(true, SeparatorRole);
    separator->setFlags(Qt::NoItemFlags);
    m_model->appendRow(separator);
}

void NavigationPanel::populate(QStandardItem *item)
{
    if (item->data(PopulatedRole).toBool()) {
        return;
    }
    item->setData(true, PopulatedRole);
    item->removeRows(0, item->rowCount());

    const QUrl url = item->data(UrlRole).toUrl();
    if (!url.isLocalFile()) {
        return;
    }
    const QDir dir(url.toLocalFile());
    const QFileInfoList folders = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase | QDir::LocaleAware);
    const QIcon folderIcon = QIcon::fromTheme(QStringLiteral("folder"));
    for (const QFileInfo &folder : folders) {
        addItem(item, folderIcon, folder.fileName(), QUrl::fromLocalFile(folder.absoluteFilePath()));
    }
}

void NavigationPanel::slotExpanded(const QModelIndex &index)
{
    QStandardItem *item = m_model->itemFromIndex(index);
    if (!item) {
        return;
    }
    populate(item);
    const QUrl url = item->data(UrlRole).toUrl();
    for (int row = 0; row < item->rowCount(); ++row) {
        QStandardItem *child = item->child(row);
        // The root drive shares "/" with Computer; opening Computer does not open the drive.
        const QUrl childUrl = child->data(UrlRole).toUrl();
        if (m_expandedUrls.contains(childUrl) && childUrl != url && child->hasChildren()) {
            m_view->expand(child->index());
        }
    }
    selectCurrentUrl();
}

void NavigationPanel::activate(const QModelIndex &index, Qt::MouseButton button)
{
    if (!index.isValid() || index.data(SeparatorRole).toBool() || index.data(PlaceholderRole).toBool()) {
        return;
    }

    const QString udi = index.data(UdiRole).toString();
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    if (!udi.isEmpty()) {
        for (int row = 0; row < places->rowCount(); ++row) {
            const QModelIndex placeIndex = places->index(row, 0);
            if (places->deviceForIndex(placeIndex).udi() == udi && places->setupNeeded(placeIndex)) {
                // Mount first; setupDone() opens the drive.
                m_pendingSetupUdi = udi;
                places->requestSetup(placeIndex);
                return;
            }
        }
    }

    const QUrl url = index.data(UrlRole).toUrl();
    if (!url.isValid()) {
        return;
    }
    if (button == Qt::MiddleButton) {
        Q_EMIT folderInNewTab(url);
    } else if (button == Qt::LeftButton) {
        Q_EMIT folderActivated(url);
    }
}

void NavigationPanel::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_view->indexAt(pos);
    const QUrl url = index.data(UrlRole).toUrl();
    if (!url.isValid()) {
        return;
    }

    QMenu menu(this);
    menu.addAction(QIcon::fromTheme(QStringLiteral("tab-new")), i18nc("@action:inmenu", "Open in New Tab"), this, [this, url]() {
        Q_EMIT folderInNewActiveTab(url);
    });
    menu.addAction(QIcon::fromTheme(QStringLiteral("window-new")), i18nc("@action:inmenu", "Open in New Window"), this, [this, url]() {
        Q_EMIT folderInNewWindow(url);
    });
    if (!isShellkitPageUrl(url)) {
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme(QStringLiteral("document-properties")), i18nc("@action:inmenu", "Properties"), this, [this, url]() {
            KPropertiesDialog::showDialog(url, this);
        });
    }
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void NavigationPanel::selectCurrentUrl()
{
    const QUrl current = url();
    QModelIndex match;
    QModelIndex fallback;
    std::function<void(const QModelIndex &)> find = [&](const QModelIndex &parent) {
        for (int row = 0; row < m_model->rowCount(parent) && !match.isValid(); ++row) {
            const QModelIndex index = m_model->index(row, 0, parent);
            if (index.data(UrlRole).toUrl().matches(current, QUrl::StripTrailingSlash)) {
                if (!index.data(FallbackRole).toBool()) {
                    match = index;
                    return;
                }
                fallback = index;
            }
            if (m_view->isExpanded(index)) {
                find(index);
            }
        }
    };
    if (current.isValid()) {
        find(QModelIndex());
    }
    if (!match.isValid()) {
        match = fallback;
    }
    if (match.isValid()) {
        m_view->setCurrentIndex(match);
    } else {
        m_view->clearSelection();
    }
}

#include "navigationpanel.moc"
#include "moc_navigationpanel.cpp"
