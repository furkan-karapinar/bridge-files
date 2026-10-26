/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "shellkitcontextmenu.h"

#include "dolphinmainwindow.h"
#include "dolphinplacesmodelsingleton.h"
#include "dolphinviewcontainer.h"
#include "shellkit.h"
#include "views/dolphinview.h"

#include <KActionCollection>
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KFilePlacesModel>
#include <KIO/CommandLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <KNewFileMenu>
#include <KStandardAction>
#include <kio/global.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QWidgetAction>

namespace
{
QModelIndex placeIndex(const QUrl &url)
{
    const KFilePlacesModel *places = DolphinPlacesModelSingleton::instance().placesModel();
    const QModelIndex index = places->closestItem(url);
    return index.isValid() && places->url(index).matches(url, QUrl::StripTrailingSlash) ? index : QModelIndex();
}
}

ShellkitContextMenu::ShellkitContextMenu(DolphinMainWindow *mainWindow,
                                         const KFileItem &item,
                                         const KFileItemList &selectedItems,
                                         const QUrl &baseUrl,
                                         KFileItemActions *fileItemActions)
    : QMenu(mainWindow)
    , m_mainWindow(mainWindow)
    , m_item(item)
    , m_selectedItems(selectedItems)
    , m_baseUrl(baseUrl)
    , m_fileItemActions(fileItemActions)
    , m_showMoreRequested(false)
{
    if (!m_item.isNull() && !m_selectedItems.isEmpty()) {
        addItemActions();
    } else {
        addViewportActions();
    }
    addShowMoreAction();
}

bool ShellkitContextMenu::showMoreRequested() const
{
    return m_showMoreRequested;
}

bool ShellkitContextMenu::handles(const QUrl &baseUrl)
{
    const QString scheme = baseUrl.scheme();
    return scheme != QLatin1String("trash") && !scheme.contains(QLatin1String("search")) && !scheme.contains(QLatin1String("timeline"))
        && scheme != QLatin1String("recentlyused");
}

void ShellkitContextMenu::addCommandRow(const QList<QAction *> &actions)
{
    // Icon buttons on one row; a click closes the menu first, then runs the command.
    auto row = new QWidget(this);
    auto layout = new QHBoxLayout(row);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);
    for (QAction *action : actions) {
        if (!action) {
            continue;
        }
        auto button = new QToolButton(row);
        button->setAutoRaise(true);
        button->setIcon(action->icon());
        button->setToolTip(action->text().remove(QLatin1Char('&')));
        button->setAccessibleName(button->toolTip());
        button->setEnabled(action->isEnabled());
        button->setFixedSize(36, 32);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, this, [this, action]() {
            close();
            QTimer::singleShot(0, action, &QAction::trigger);
        });
        layout->addWidget(button);
    }
    layout->addStretch();
    auto rowAction = new QWidgetAction(this);
    rowAction->setDefaultWidget(row);
    addAction(rowAction);
    addSeparator();
}

void ShellkitContextMenu::addItemActions()
{
    const KActionCollection *collection = m_mainWindow->actionCollection();
    const KFileItemListProperties properties(m_selectedItems);
    m_fileItemActions->setItemListProperties(properties);
    const bool single = m_selectedItems.count() == 1;
    const bool folder = single && m_item.isDir();

    QAction *remove = collection->action(KStandardAction::name(properties.isLocal() && properties.supportsMoving() ? KStandardAction::MoveToTrash
                                                                                                                  : KStandardAction::DeleteFile));
    addCommandRow({collection->action(KStandardAction::name(KStandardAction::Cut)),
                   collection->action(KStandardAction::name(KStandardAction::Copy)),
                   collection->action(KStandardAction::name(KStandardAction::RenameFile)),
                   remove});

    QAction *open = addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18ndc(shellkitDomain, "@action:inmenu", "Open"), this, [this]() {
        openItems();
    });
    QFont font = open->font();
    font.setBold(true);
    open->setFont(font);

    if (folder) {
        addAction(collection->action(QStringLiteral("open_in_new_tab")));
        addAction(collection->action(QStringLiteral("open_in_new_window")));
    } else {
        m_fileItemActions->insertOpenWithActionsTo(nullptr, this, QStringList{qApp->desktopFileName()});
    }
    addSeparator();

    if (folder && m_item.isLocalFile()) {
        const QUrl url = m_item.url();
        if (placeIndex(url).isValid()) {
            addAction(QIcon::fromTheme(QStringLiteral("window-unpin")), i18ndc(shellkitDomain, "@action:inmenu", "Unpin from Quick access"), this, [url]() {
                const QModelIndex index = placeIndex(url);
                if (index.isValid()) {
                    DolphinPlacesModelSingleton::instance().placesModel()->removePlace(index);
                }
            });
        } else {
            const QString name = m_item.name();
            addAction(QIcon::fromTheme(QStringLiteral("window-pin")), i18ndc(shellkitDomain, "@action:inmenu", "Pin to Quick access"), this, [url, name]() {
                DolphinPlacesModelSingleton::instance().placesModel()->addPlace(name, url, KIO::iconNameForUrl(url));
            });
        }
    }
    if (single && !folder && m_item.isLocalFile()) {
        const QUrl url = m_item.url();
        if (shellkitFavorites().contains(url)) {
            addAction(QIcon::fromTheme(QStringLiteral("starred-symbolic")), i18ndc(shellkitDomain, "@action:inmenu", "Remove from Favorites"), this, [url]() {
                shellkitSetFavorite(url, false);
            });
        } else {
            addAction(QIcon::fromTheme(QStringLiteral("non-starred-symbolic")), i18ndc(shellkitDomain, "@action:inmenu", "Add to Favorites"), this, [url]() {
                shellkitSetFavorite(url, true);
            });
        }
    }

    // Compressing goes through Ark, as its own "Compress to zip" service menu does.
    if (properties.isLocal() && !QStandardPaths::findExecutable(QStringLiteral("ark")).isEmpty()) {
        const QList<QUrl> urls = m_selectedItems.urlList();
        addAction(QIcon::fromTheme(QStringLiteral("archive-insert")), i18ndc(shellkitDomain, "@action:inmenu", "Compress to ZIP file"), this, [this, urls]() {
            QStringList arguments{QStringLiteral("--changetofirstpath"), QStringLiteral("--add"), QStringLiteral("--autofilename"), QStringLiteral("zip")};
            for (const QUrl &url : urls) {
                arguments.append(url.toLocalFile());
            }
            auto job = new KIO::CommandLauncherJob(QStringLiteral("ark"), arguments);
            job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, m_mainWindow));
            job->start();
        });
    }
    addAction(collection->action(QStringLiteral("copy_location")));
    addAction(collection->action(QStringLiteral("properties")));
}

void ShellkitContextMenu::addViewportActions()
{
    const KActionCollection *collection = m_mainWindow->actionCollection();

    addAction(collection->action(QStringLiteral("view_mode")));
    addAction(collection->action(QStringLiteral("sort")));
    addSeparator();

    QAction *paste = collection->action(KStandardAction::name(KStandardAction::Paste));
    if (paste && paste->isEnabled()) {
        addAction(paste);
    }
    QAction *undo = collection->action(KStandardAction::name(KStandardAction::Undo));
    if (undo && undo->isEnabled()) {
        addAction(undo);
    }
    QAction *terminal = collection->action(QStringLiteral("open_terminal_here"));
    if (terminal && m_baseUrl.isLocalFile()) {
        addAction(terminal);
    }
    addSeparator();

    KNewFileMenu *newFileMenu = m_mainWindow->newFileMenu();
    newFileMenu->checkUpToDate();
    newFileMenu->setWorkingDirectory(m_baseUrl);
    addMenu(newFileMenu->menu());
    addSeparator();

    addAction(collection->action(QStringLiteral("properties")));
}

void ShellkitContextMenu::addShowMoreAction()
{
    addSeparator();
    QAction *more = addAction(QIcon::fromTheme(QStringLiteral("overflow-menu")), i18ndc(shellkitDomain, "@action:inmenu", "Show more options"), this, [this]() {
        m_showMoreRequested = true;
    });
    more->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F10));
}

void ShellkitContextMenu::openItems()
{
    // Folders open here (the first) or in new tabs (the rest); files in their default application.
    KFileItemList files;
    bool openedHere = false;
    for (const KFileItem &item : std::as_const(m_selectedItems)) {
        const QUrl folderUrl = DolphinView::openItemAsFolderUrl(item);
        if (folderUrl.isEmpty()) {
            files.append(item);
        } else if (!openedHere) {
            m_mainWindow->changeUrl(folderUrl);
            openedHere = true;
        } else {
            m_mainWindow->openNewTab(folderUrl);
        }
    }
    if (!files.isEmpty()) {
        m_fileItemActions->runPreferredApplications(files);
    }
}

#include "moc_shellkitcontextmenu.cpp"
