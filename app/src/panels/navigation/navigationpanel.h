/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NAVIGATIONPANEL_H
#define NAVIGATIONPANEL_H

#include "panels/panel.h"

#include <QSet>
#include <QUrl>

class NavigationTreeView;
class QStandardItem;
class QStandardItemModel;
class QTimer;

/**
 * @brief shellkit fork: the navigation pane of the Windows 11 File Explorer.
 *
 * One tree with Home, Gallery, the pinned folders (the places of the "Places"
 * group), This PC with the drives, and Network. Local folders expand lazily
 * into their subfolders. The item of the current folder is selected.
 */
class NavigationPanel : public Panel
{
    Q_OBJECT

public:
    explicit NavigationPanel(QWidget *parent = nullptr);
    ~NavigationPanel() override;

Q_SIGNALS:
    void folderActivated(const QUrl &url);
    void folderInNewTab(const QUrl &url);
    void folderInNewActiveTab(const QUrl &url);
    void folderInNewWindow(const QUrl &url);
    void errorMessage(const QString &error);

protected:
    bool urlChanged() override;
    void changeEvent(QEvent *event) override;

private:
    void rebuild();
    /** The Windows 7 tree (shellkitWin7()); the groups start open on the first build. */
    void rebuildWin7(bool firstBuild);
    void addPinnedFolders(QStandardItem *parent = nullptr);
    void addDrives(QStandardItem *thisPc);
    QStandardItem *addItem(QStandardItem *parent, const QIcon &icon, const QString &text, const QUrl &url, bool expandable = true);
    void addSeparator();
    void populate(QStandardItem *item);
    void slotExpanded(const QModelIndex &index);
    void activate(const QModelIndex &index, Qt::MouseButton button);
    void showContextMenu(const QPoint &pos);
    void selectCurrentUrl();

    NavigationTreeView *m_view;
    QStandardItemModel *m_model;
    QTimer *m_rebuildTimer;
    /** Folders that were expanded before a rebuild and get expanded again. */
    QSet<QUrl> m_expandedUrls;
    QString m_pendingSetupUdi;
};

#endif // NAVIGATIONPANEL_H
