/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include <QList>
#include <QScrollArea>
#include <QUrl>

#include <functional>

class QAbstractItemView;
class QDateTime;
class QLabel;
class QListView;
class QStandardItemModel;
class QTreeView;
class QVBoxLayout;

/**
 * @brief shellkit fork: the Home page of the Windows 11 File Explorer.
 *
 * Shown by DolphinViewContainer in place of the view for shellkitHomeUrl(). Three
 * collapsible sections: Quick access (the pinned folders), Favorites (files the user
 * favorited, stored in the "Home" group of the config) and Recent (recently-used.xbel).
 */
class HomePage : public QScrollArea
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);

    /** Reads the pinned folders, favorites and recent files again. */
    void refresh();

    /** Number of items on the page, for the status bar. */
    int itemCount() const;

Q_SIGNALS:
    void folderActivated(const QUrl &url);
    void folderInNewTab(const QUrl &url);
    /** "Open file location": show the folder of  fileUrl with the file selected. */
    void fileLocationRequested(const QUrl &fileUrl);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void addSection(QVBoxLayout *layout, const QString &title, QWidget *body, QLabel *emptyLabel);
    void fillQuickAccess();
    void fillFiles(QStandardItemModel *model, const QList<QUrl> &urls, const QList<QDateTime> &times);
    void fillFavorites();
    void fillRecent();
    void updateHeights();
    void openFile(const QUrl &url);
    void showContextMenu(QAbstractItemView *view, const QPoint &pos);

    QListView *m_quickAccess;
    QStandardItemModel *m_quickAccessModel;
    QTreeView *m_favorites;
    QStandardItemModel *m_favoritesModel;
    QLabel *m_favoritesEmpty;
    QTreeView *m_recent;
    QStandardItemModel *m_recentModel;
    QLabel *m_recentEmpty;
    /** Show each section's list or its empty text, unless the section is folded. */
    QList<std::function<void()>> m_sectionUpdaters;
};

#endif // HOMEPAGE_H
