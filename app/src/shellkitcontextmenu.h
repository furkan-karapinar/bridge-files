/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SHELLKITCONTEXTMENU_H
#define SHELLKITCONTEXTMENU_H

#include <KFileItem>

#include <QMenu>
#include <QUrl>

class DolphinMainWindow;
class KFileItemActions;

/**
 * @brief shellkit fork: the short context menu of the Windows 11 File Explorer.
 *
 * A row of icon buttons (cut, copy, rename, delete) above the common commands. "Show more
 * options" closes it and asks for the full Dolphin menu (DolphinContextMenu), see
 * showMoreRequested().
 */
class ShellkitContextMenu : public QMenu
{
    Q_OBJECT

public:
    ShellkitContextMenu(DolphinMainWindow *mainWindow,
                        const KFileItem &item,
                        const KFileItemList &selectedItems,
                        const QUrl &baseUrl,
                        KFileItemActions *fileItemActions);

    /** True when the menu was closed with "Show more options". */
    bool showMoreRequested() const;

    /** Contexts that only the full menu handles (trash, search results and the like). */
    static bool handles(const QUrl &baseUrl);

private:
    void addCommandRow(const QList<QAction *> &actions);
    void addItemActions();
    void addViewportActions();
    void addShowMoreAction();
    void openItems();

    DolphinMainWindow *m_mainWindow;
    KFileItem m_item;
    KFileItemList m_selectedItems;
    QUrl m_baseUrl;
    KFileItemActions *m_fileItemActions;
    bool m_showMoreRequested;
};

#endif // SHELLKITCONTEXTMENU_H
