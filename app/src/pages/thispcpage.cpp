/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "thispcpage.h"

#include "dolphinplacesmodelsingleton.h"
#include "global.h"
#include "pages/tilesection.h"
#include "shellkit.h"

#include <KFilePlacesModel>
#include <KLocalizedString>
#include <KPropertiesDialog>
#include <Solid/Device>
#include <Solid/StorageAccess>
#include <Solid/StorageVolume>

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QMenu>
#include <QPersistentModelIndex>
#include <QStandardItemModel>
#include <QStorageInfo>
#include <QVBoxLayout>

namespace
{
constexpr int PageMargin = 20;
constexpr int TileIconSize = 48;

/** Small sizes of the Fluent theme are monochrome; the page shows the coloured icon. */
QIcon colourIcon(const QIcon &icon)
{
    return icon.isNull() ? icon : QIcon(icon.pixmap(QSize(TileIconSize, TileIconSize)));
}

/** "53.4 GB free of 237 GB" and the used part, for the tile of a mounted file system. */
void setCapacity(QStandardItem *tile, const QString &path)
{
    const QStorageInfo storage(path);
    if (!storage.isValid() || !storage.isReady() || storage.bytesTotal() <= 0) {
        return;
    }
    const qint64 total = storage.bytesTotal();
    const qint64 free = storage.bytesAvailable();
    tile->setData(QStringList{i18ndc(shellkitDomain, "@info Free space of a drive", "%1 free of %2", shellkitStatusSize(free), shellkitStatusSize(total))},
                  TileRoles::LinesRole);
    tile->setData(1.0 - double(free) / double(total), TileRoles::UsageRole);
}
}

ThisPcPage::ThisPcPage(QWidget *parent)
    : ShellkitPage(parent)
    , m_drives(nullptr)
{
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setBackgroundRole(QPalette::Base);
    viewport()->setAutoFillBackground(true);
    viewport()->installEventFilter(this);

    auto content = new QWidget(this);
    content->setBackgroundRole(QPalette::Base);
    auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(PageMargin, 12, PageMargin, PageMargin);
    layout->setSpacing(4);

    m_drives = new TileSection(i18ndc(shellkitDomain, "@title:group", "Devices and drives"), content);
    connect(m_drives, &TileSection::activated, this, [this](const QModelIndex &tile) {
        open(tile, false);
    });
    connect(m_drives, &TileSection::middleClicked, this, [this](const QModelIndex &tile) {
        open(tile, true);
    });
    connect(m_drives, &TileSection::contextMenuRequested, this, &ThisPcPage::showContextMenu);
    layout->addWidget(m_drives);
    layout->addStretch();
    setWidget(content);

    // Drives come and go, and get mounted or unmounted.
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    const auto placesChanged = [this]() {
        if (isVisible()) {
            fill();
            Q_EMIT itemCountChanged();
        }
    };
    connect(places, &QAbstractItemModel::rowsInserted, this, placesChanged);
    connect(places, &QAbstractItemModel::rowsRemoved, this, placesChanged);
    connect(places, &QAbstractItemModel::dataChanged, this, placesChanged);
}

void ThisPcPage::refresh()
{
    fill();
}

int ThisPcPage::itemCount() const
{
    return m_drives->model()->rowCount();
}

QString ThisPcPage::driveName(KFilePlacesModel *places, const QModelIndex &index)
{
    const Solid::Device device = places->deviceForIndex(index);
    const auto access = device.as<Solid::StorageAccess>();
    if (!access || !access->isAccessible() || access->filePath().isEmpty()) {
        return places->text(index);
    }
    const auto volume = device.as<Solid::StorageVolume>();
    const QString label = volume ? volume->label() : QString();
    const QString mountPoint = access->filePath();
    if (!label.isEmpty()) {
        return i18ndc(shellkitDomain, "@item:inlistbox %1 is the label of a drive, %2 its mount point", "%1 (%2)", label, mountPoint);
    }
    if (places->groupType(index) == KFilePlacesModel::RemovableDevicesType) {
        return i18ndc(shellkitDomain, "@item:inlistbox %1 is a mount point", "USB Drive (%1)", mountPoint);
    }
    return i18ndc(shellkitDomain, "@item:inlistbox %1 is a mount point", "Local Disk (%1)", mountPoint);
}

bool ThisPcPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == viewport() && event->type() == QEvent::Resize) {
        updateHeights();
    }
    return ShellkitPage::eventFilter(watched, event);
}

void ThisPcPage::fill()
{
    QStandardItemModel *model = m_drives->model();
    model->clear();

    // The root file system first, as C: on Windows; the places model may not list it.
    const QUrl root = QUrl::fromLocalFile(QStringLiteral("/"));
    auto rootTile = new QStandardItem(colourIcon(QIcon::fromTheme(QStringLiteral("drive-harddisk"))),
                                      i18ndc(shellkitDomain, "@item:inlistbox %1 is a mount point", "Local Disk (%1)", QStringLiteral("/")));
    rootTile->setData(root, TileRoles::UrlRole);
    setCapacity(rootTile, QStringLiteral("/"));
    model->appendRow(rootTile);

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
        auto tile = new QStandardItem(colourIcon(places->icon(index)), driveName(places, index));
        tile->setData(url, TileRoles::UrlRole);
        tile->setData(places->deviceForIndex(index).udi(), TileRoles::KeyRole);
        if (!places->setupNeeded(index) && url.isLocalFile()) {
            setCapacity(tile, url.toLocalFile());
        }
        model->appendRow(tile);
    }
    updateHeights();
}

void ThisPcPage::updateHeights()
{
    m_drives->updateHeight(viewport()->width() - 2 * PageMargin);
}

QModelIndex ThisPcPage::placeIndex(const QString &udi) const
{
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    for (int row = 0; !udi.isEmpty() && row < places->rowCount(); ++row) {
        const QModelIndex index = places->index(row, 0);
        if (places->deviceForIndex(index).udi() == udi) {
            return index;
        }
    }
    return QModelIndex();
}

void ThisPcPage::open(const QModelIndex &tile, bool newTab)
{
    const auto go = [this, newTab](const QUrl &url) {
        if (newTab) {
            Q_EMIT folderInNewTab(url);
        } else {
            Q_EMIT folderActivated(url);
        }
    };
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    const QString udi = tile.data(TileRoles::KeyRole).toString();
    const QModelIndex place = placeIndex(udi);
    if (!place.isValid() || !places->setupNeeded(place)) {
        go(tile.data(TileRoles::UrlRole).toUrl());
        return;
    }
    // Mount it first; its URL is known only afterwards.
    connect(
        places,
        &KFilePlacesModel::setupDone,
        this,
        [this, places, udi, go](const QModelIndex &, bool success) {
            const QModelIndex mounted = placeIndex(udi);
            if (success && mounted.isValid()) {
                go(places->url(mounted));
            }
        },
        Qt::SingleShotConnection);
    places->requestSetup(place);
}

void ThisPcPage::showContextMenu(const QModelIndex &tile, const QPoint &globalPos)
{
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    const QModelIndex place = placeIndex(tile.data(TileRoles::KeyRole).toString());
    const QUrl url = tile.data(TileRoles::UrlRole).toUrl();
    const bool mounted = !place.isValid() || !places->setupNeeded(place);

    // The texts of the Home page's and the file list's menus.
    QMenu menu(this);
    QAction *openAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18ndc(shellkitDomain, "@action:inmenu", "Open"), this, [this, tile]() {
        open(tile, false);
    });
    QFont font = openAction->font();
    font.setBold(true);
    openAction->setFont(font);
    menu.addAction(QIcon::fromTheme(QStringLiteral("tab-new")), i18nc("@action:inmenu", "Open in New Tab"), this, [this, tile]() {
        open(tile, true);
    });
    if (mounted) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("window-new")), i18nc("@action:inmenu", "Open in New Window"), this, [this, url]() {
            Dolphin::openNewWindow({url}, this);
        });
    }
    menu.addSeparator();

    if (place.isValid()) {
        // "Unmount", "Safely Remove" or "Eject", as KDE's own places menu offers them.
        const QPersistentModelIndex persistent(place);
        if (QAction *teardown = places->teardownActionForIndex(place)) {
            teardown->setParent(&menu);
            connect(teardown, &QAction::triggered, this, [places, persistent]() {
                places->requestTeardown(persistent);
            });
            menu.addAction(teardown);
        }
        if (QAction *eject = places->ejectActionForIndex(place)) {
            eject->setParent(&menu);
            connect(eject, &QAction::triggered, this, [places, persistent]() {
                places->requestEject(persistent);
            });
            menu.addAction(eject);
        }
    }
    if (mounted && url.isLocalFile()) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy-path")), i18ndc(shellkitDomain, "@action:incontextmenu", "Copy as path"), this, [url]() {
            QApplication::clipboard()->setText(shellkitPathText({url}));
        });
        menu.addSeparator();
        menu.addAction(QIcon::fromTheme(QStringLiteral("document-properties")), i18nc("@action:inmenu File", "Properties"), this, [this, url]() {
            KPropertiesDialog::showDialog(url, this);
        });
    }
    menu.exec(globalPos);
}

#include "moc_thispcpage.cpp"
