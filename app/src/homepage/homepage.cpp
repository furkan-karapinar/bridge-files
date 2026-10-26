/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "homepage.h"

#include "dolphinplacesmodelsingleton.h"
#include "global.h"
#include "shellkit.h"

#include <KFilePlacesModel>
#include <KIO/JobUiDelegateFactory>
#include <KIO/OpenUrlJob>
#include <KLocalizedString>
#include <KPropertiesDialog>
#include <KRecentDocument>

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QXmlStreamReader>

#include <algorithm>

namespace
{
enum Role {
    UrlRole = Qt::UserRole + 1,
    SecondaryRole,
};

constexpr int TileWidth = 136;
constexpr int TileHeight = 120;
constexpr int TileIconSize = 64;
constexpr int RowHeight = 30;
constexpr int MaxRecentFiles = 20;
constexpr int PageMargin = 20;

/**
 * Small sizes of the Fluent theme are monochrome panel icons; the pages of Windows show
 * the coloured icon, so every icon here is drawn from a large pixmap.
 */
QIcon colourIcon(const QIcon &icon)
{
    return icon.isNull() ? icon : QIcon(icon.pixmap(QSize(TileIconSize, TileIconSize)));
}

QColor secondaryColor(const QPalette &palette)
{
    QColor color = palette.color(QPalette::Text);
    color.setAlphaF(0.75);
    return color;
}

/** A Quick access tile: the icon above the name and the location. */
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
        const QRect iconRect(r.center().x() - TileIconSize / 2, r.top() + 8, TileIconSize, TileIconSize);
        index.data(Qt::DecorationRole).value<QIcon>().paint(painter, iconRect);

        const QFontMetrics fm = option.fontMetrics;
        const QRect nameRect(r.left() + 6, iconRect.bottom() + 6, r.width() - 12, fm.height());
        painter->save();
        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop, fm.elidedText(index.data().toString(), Qt::ElideRight, nameRect.width()));
        painter->setPen(secondaryColor(option.palette));
        const QRect secondaryRect = nameRect.translated(0, fm.height());
        painter->drawText(secondaryRect,
                          Qt::AlignHCenter | Qt::AlignTop,
                          fm.elidedText(index.data(SecondaryRole).toString(), Qt::ElideRight, secondaryRect.width()));
        painter->restore();
    }
};

/** File list rows: the row fill comes from FileListView::drawRow(), the cells draw only content. */
class RowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        return QSize(QStyledItemDelegate::sizeHint(option, index).width(), RowHeight);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem plain(option);
        plain.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
        QStyledItemDelegate::paint(painter, plain, index);
    }
};

/** A list of files whose hover and selection cover the whole row, as in the details view. */
class FileListView : public QTreeView
{
public:
    explicit FileListView(QWidget *parent)
        : QTreeView(parent)
    {
        setRootIsDecorated(false);
        setUniformRowHeights(true);
        setItemsExpandable(false);
        setSelectionBehavior(QAbstractItemView::SelectRows);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setFrameShape(QFrame::NoFrame);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setMouseTracking(true);
        viewport()->setAttribute(Qt::WA_Hover);
        setItemDelegate(new RowDelegate(this));
        header()->setSectionsMovable(false);
        header()->setSectionsClickable(false);
        header()->setStretchLastSection(true);
        header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setIconSize(QSize(16, 16));
    }

protected:
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem row(option);
        row.rect = QRect(0, option.rect.y(), viewport()->width(), option.rect.height());
        if (selectionModel()->isSelected(index)) {
            row.state |= QStyle::State_Selected;
        }
        style()->drawPrimitive(QStyle::PE_PanelItemViewItem, &row, painter, this);
        QTreeView::drawRow(painter, option, index);
    }
};

QLabel *newEmptyLabel(const QString &text, QWidget *parent)
{
    auto label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setContentsMargins(8, 4, 8, 12);
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, secondaryColor(palette));
    label->setPalette(palette);
    return label;
}
}

HomePage::HomePage(QWidget *parent)
    : QScrollArea(parent)
    , m_quickAccess(nullptr)
    , m_quickAccessModel(new QStandardItemModel(this))
    , m_favorites(nullptr)
    , m_favoritesModel(new QStandardItemModel(this))
    , m_favoritesEmpty(nullptr)
    , m_recent(nullptr)
    , m_recentModel(new QStandardItemModel(this))
    , m_recentEmpty(nullptr)
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

    m_quickAccess = new QListView(content);
    m_quickAccess->setModel(m_quickAccessModel);
    m_quickAccess->setViewMode(QListView::IconMode);
    m_quickAccess->setFlow(QListView::LeftToRight);
    m_quickAccess->setWrapping(true);
    m_quickAccess->setResizeMode(QListView::Adjust);
    m_quickAccess->setMovement(QListView::Static);
    m_quickAccess->setGridSize(QSize(TileWidth, TileHeight));
    m_quickAccess->setUniformItemSizes(true);
    m_quickAccess->setFrameShape(QFrame::NoFrame);
    m_quickAccess->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_quickAccess->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_quickAccess->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_quickAccess->setMouseTracking(true);
    m_quickAccess->viewport()->setAttribute(Qt::WA_Hover);
    m_quickAccess->viewport()->installEventFilter(this);
    m_quickAccess->setItemDelegate(new TileDelegate(m_quickAccess));
    m_quickAccess->installEventFilter(this);
    connect(m_quickAccess, &QListView::activated, this, [this](const QModelIndex &index) {
        Q_EMIT folderActivated(index.data(UrlRole).toUrl());
    });
    m_quickAccess->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_quickAccess, &QListView::customContextMenuRequested, this, [this](const QPoint &pos) {
        showContextMenu(m_quickAccess, pos);
    });

    m_favorites = new FileListView(content);
    m_favorites->setModel(m_favoritesModel);
    m_favoritesEmpty = newEmptyLabel(i18ndc(shellkitDomain, "@info", "Files you add to favorites will show up here."), content);

    m_recent = new FileListView(content);
    m_recent->setModel(m_recentModel);
    m_recentEmpty = newEmptyLabel(i18ndc(shellkitDomain, "@info", "Files you open will show up here."), content);

    for (QTreeView *list : {m_favorites, m_recent}) {
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QTreeView::customContextMenuRequested, this, [this, list](const QPoint &pos) {
            showContextMenu(list, pos);
        });
        connect(list, &QTreeView::activated, this, [this, list](const QModelIndex &index) {
            const QUrl url = list->model()->index(index.row(), 0).data(UrlRole).toUrl();
            if (QFileInfo(url.toLocalFile()).isDir()) {
                Q_EMIT folderActivated(url);
            } else {
                openFile(url);
            }
        });
    }

    addSection(layout, i18ndc(shellkitDomain, "@title:group", "Quick access"), m_quickAccess, nullptr);
    addSection(layout, i18ndc(shellkitDomain, "@title:group", "Favorites"), m_favorites, m_favoritesEmpty);
    addSection(layout, i18ndc(shellkitDomain, "@title:group Recently opened files", "Recent"), m_recent, m_recentEmpty);
    layout->addStretch();
    setWidget(content);

    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    const auto placesChanged = [this]() {
        fillQuickAccess();
        updateHeights();
    };
    connect(places, &QAbstractItemModel::rowsInserted, this, placesChanged);
    connect(places, &QAbstractItemModel::rowsRemoved, this, placesChanged);
    connect(places, &QAbstractItemModel::dataChanged, this, placesChanged);
}

void HomePage::refresh()
{
    fillQuickAccess();
    fillFavorites();
    fillRecent();
    updateHeights();
}

int HomePage::itemCount() const
{
    return m_quickAccessModel->rowCount() + m_favoritesModel->rowCount() + m_recentModel->rowCount();
}

bool HomePage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize && (watched == viewport() || watched == m_quickAccess)) {
        updateHeights();
    } else if (watched == m_quickAccess->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::MiddleButton) {
            const QModelIndex index = m_quickAccess->indexAt(mouseEvent->position().toPoint());
            if (index.isValid()) {
                Q_EMIT folderInNewTab(index.data(UrlRole).toUrl());
                return true;
            }
        }
    }
    return QScrollArea::eventFilter(watched, event);
}

void HomePage::addSection(QVBoxLayout *layout, const QString &title, QWidget *body, QLabel *emptyLabel)
{
    // Section header: a chevron and the title; a click folds the section.
    auto header = new QToolButton(layout->parentWidget());
    header->setText(title);
    header->setAutoRaise(true);
    header->setProperty("expanded", true);
    header->setArrowType(Qt::DownArrow);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QFont font = header->font();
    font.setWeight(QFont::DemiBold);
    font.setPointSizeF(font.pointSizeF() * 1.1);
    header->setFont(font);
    layout->addSpacing(8);
    layout->addWidget(header, 0, Qt::AlignLeft);
    layout->addWidget(body);
    if (emptyLabel) {
        layout->addWidget(emptyLabel);
    }

    const auto apply = [header, body, emptyLabel]() {
        const bool expanded = header->property("expanded").toBool();
        const bool empty = body->property("shellkitEmpty").toBool();
        body->setVisible(expanded && !empty);
        if (emptyLabel) {
            emptyLabel->setVisible(expanded && empty);
        }
    };
    connect(header, &QToolButton::clicked, this, [header, apply]() {
        const bool expanded = !header->property("expanded").toBool();
        header->setProperty("expanded", expanded);
        header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        apply();
    });
    // Re-evaluated whenever the content changes, see fillQuickAccess() and fillRecent().
    body->setProperty("shellkitEmpty", false);
    m_sectionUpdaters.append(apply);
}

void HomePage::fillQuickAccess()
{
    m_quickAccessModel->clear();
    KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    const QUrl home = QUrl::fromLocalFile(QDir::homePath());
    const QModelIndex homeIndex = places->closestItem(home);
    const QString homeText = places->url(homeIndex).matches(home, QUrl::StripTrailingSlash) ? places->text(homeIndex) : QDir::home().dirName();
    for (int row = 0; row < places->rowCount(); ++row) {
        const QModelIndex index = places->index(row, 0);
        if (places->groupType(index) != KFilePlacesModel::PlacesType || places->isHidden(index)) {
            continue;
        }
        const QUrl url = places->url(index);
        if (url.matches(home, QUrl::StripTrailingSlash) || url.scheme() == QLatin1String("trash") || url.path() == QLatin1String("/")) {
            continue;
        }
        auto item = new QStandardItem(colourIcon(places->icon(index)), places->text(index));
        item->setData(url, UrlRole);
        // The location under the name: the folder that holds it.
        const QUrl parentUrl = url.adjusted(QUrl::StripTrailingSlash | QUrl::RemoveFilename);
        const QString parentName = parentUrl.matches(home, QUrl::StripTrailingSlash) ? homeText : QDir(parentUrl.path()).dirName();
        item->setData(parentName, SecondaryRole);
        m_quickAccessModel->appendRow(item);
    }
    m_quickAccess->setProperty("shellkitEmpty", m_quickAccessModel->rowCount() == 0);
    for (const auto &apply : std::as_const(m_sectionUpdaters)) {
        apply();
    }
}

void HomePage::fillFiles(QStandardItemModel *model, const QList<QUrl> &urls, const QList<QDateTime> &times)
{
    model->clear();
    QMimeDatabase mimeDatabase;
    const QString homePath = QDir::homePath();
    for (int i = 0; i < urls.count(); ++i) {
        const QFileInfo info(urls[i].toLocalFile());
        auto name = new QStandardItem(colourIcon(QIcon::fromTheme(mimeDatabase.mimeTypeForFile(info).iconName())), info.fileName());
        name->setData(urls[i], UrlRole);
        auto date = new QStandardItem(QLocale().toString(times[i].toLocalTime(), QLocale::ShortFormat));
        QString location = QDir::toNativeSeparators(info.absolutePath());
        if (location.startsWith(homePath)) {
            location.replace(0, homePath.length(), QStringLiteral("~"));
        }
        auto path = new QStandardItem(location);
        for (QStandardItem *secondary : {date, path}) {
            secondary->setForeground(secondaryColor(palette()));
        }
        model->appendRow({name, date, path});
    }
}

void HomePage::fillFavorites()
{
    // Written by the "Add to favorites" action of the context menus.
    QList<QUrl> urls;
    QList<QDateTime> times;
    for (const QUrl &url : shellkitFavorites()) {
        const QFileInfo info(url.toLocalFile());
        if (url.isLocalFile() && info.exists()) {
            urls.append(url);
            times.append(info.lastModified());
        }
    }
    fillFiles(m_favoritesModel, urls, times);
    m_favoritesModel->setHorizontalHeaderLabels({i18ndc("dolphin", "@label", "Name"),
                                                 i18ndc(shellkitDomain, "@title:column", "Date modified"),
                                                 i18ndc(shellkitDomain, "@title:column", "File location")});
    m_favorites->setProperty("shellkitEmpty", urls.isEmpty());
}

void HomePage::fillRecent()
{
    // The freedesktop.org list KRecentDocument and other applications write.
    struct Entry {
        QUrl url;
        QDateTime visited;
    };
    QList<Entry> entries;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/recently-used.xbel"));
    if (file.open(QIODevice::ReadOnly)) {
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement() || xml.name() != QLatin1String("bookmark")) {
                continue;
            }
            const auto attributes = xml.attributes();
            const QUrl url(attributes.value(QLatin1String("href")).toString());
            QDateTime visited = QDateTime::fromString(attributes.value(QLatin1String("visited")).toString(), Qt::ISODate);
            if (!visited.isValid()) {
                visited = QDateTime::fromString(attributes.value(QLatin1String("modified")).toString(), Qt::ISODate);
            }
            if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) {
                entries.append({url, visited});
            }
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.visited > b.visited;
    });
    QList<QUrl> urls;
    QList<QDateTime> times;
    for (const Entry &entry : std::as_const(entries)) {
        if (urls.count() == MaxRecentFiles) {
            break;
        }
        if (!urls.contains(entry.url)) {
            urls.append(entry.url);
            times.append(entry.visited);
        }
    }
    fillFiles(m_recentModel, urls, times);
    m_recentModel->setHorizontalHeaderLabels({i18ndc("dolphin", "@label", "Name"),
                                              i18ndc(shellkitDomain, "@title:column", "Date accessed"),
                                              i18ndc(shellkitDomain, "@title:column", "File location")});
    m_recent->setProperty("shellkitEmpty", urls.isEmpty());
    for (const auto &apply : std::as_const(m_sectionUpdaters)) {
        apply();
    }
}

void HomePage::updateHeights()
{
    // The page scrolls as a whole, so every list is as high as its content.
    const int width = viewport()->width() - 2 * PageMargin;
    const int columns = qMax(1, width / TileWidth);
    const int rows = (m_quickAccessModel->rowCount() + columns - 1) / columns;
    m_quickAccess->setFixedHeight(qMax(1, rows) * TileHeight + 4);

    for (QTreeView *list : {m_favorites, m_recent}) {
        const int count = list->model()->rowCount();
        list->setFixedHeight(list->header()->sizeHint().height() + count * RowHeight + 2);
        list->header()->resizeSection(0, qMax(200, width * 45 / 100));
        list->header()->resizeSection(1, 170);
    }
}

void HomePage::showContextMenu(QAbstractItemView *view, const QPoint &pos)
{
    const QModelIndex index = view->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    view->setCurrentIndex(index);
    const QUrl url = view->model()->index(index.row(), 0).data(UrlRole).toUrl();
    const bool folder = view == m_quickAccess || QFileInfo(url.toLocalFile()).isDir();

    // The Windows 11 menu of a Home item, with the same texts as the file list's menu.
    QMenu menu(this);
    QAction *open = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18ndc(shellkitDomain, "@action:inmenu", "Open"), this, [this, url, folder]() {
        if (folder) {
            Q_EMIT folderActivated(url);
        } else {
            openFile(url);
        }
    });
    QFont font = open->font();
    font.setBold(true);
    open->setFont(font);
    if (folder) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("tab-new")), i18nc("@action:inmenu", "Open in New Tab"), this, [this, url]() {
            Q_EMIT folderInNewTab(url);
        });
        menu.addAction(QIcon::fromTheme(QStringLiteral("window-new")), i18nc("@action:inmenu", "Open in New Window"), this, [this, url]() {
            Dolphin::openNewWindow({url}, this);
        });
    } else {
        menu.addAction(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18ndc(shellkitDomain, "@action:inmenu", "Open file location"), this, [this, url]() {
            Q_EMIT fileLocationRequested(url);
        });
    }
    menu.addSeparator();

    if (view == m_quickAccess) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("window-unpin")), i18ndc(shellkitDomain, "@action:inmenu", "Unpin from Quick access"), this, [url]() {
            KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
            const QModelIndex place = places->closestItem(url);
            if (place.isValid() && places->url(place).matches(url, QUrl::StripTrailingSlash)) {
                places->removePlace(place);
            }
        });
    } else if (!folder) {
        if (shellkitFavorites().contains(url)) {
            menu.addAction(QIcon::fromTheme(QStringLiteral("starred-symbolic")), i18ndc(shellkitDomain, "@action:inmenu", "Remove from Favorites"), this, [this, url]() {
                shellkitSetFavorite(url, false);
                refresh();
            });
        } else {
            menu.addAction(QIcon::fromTheme(QStringLiteral("non-starred-symbolic")), i18ndc(shellkitDomain, "@action:inmenu", "Add to Favorites"), this, [this, url]() {
                shellkitSetFavorite(url, true);
                refresh();
            });
        }
    }
    if (view == m_recent) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-clear-history")), i18ndc(shellkitDomain, "@action:inmenu", "Remove from Recent"), this, [this, url]() {
            KRecentDocument::removeFile(url);
            refresh();
        });
    }
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy-path")), i18ndc(shellkitDomain, "@action:incontextmenu", "Copy as path"), this, [url]() {
        QApplication::clipboard()->setText(shellkitPathText({url}));
    });
    menu.addAction(QIcon::fromTheme(QStringLiteral("document-properties")), i18nc("@action:inmenu File", "Properties"), this, [this, url]() {
        KPropertiesDialog::showDialog(url, this);
    });
    menu.exec(view->viewport()->mapToGlobal(pos));
}

void HomePage::openFile(const QUrl &url)
{
    auto job = new KIO::OpenUrlJob(url);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
    job->start();
}

#include "moc_homepage.cpp"
