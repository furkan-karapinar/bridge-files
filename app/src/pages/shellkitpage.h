/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SHELLKITPAGE_H
#define SHELLKITPAGE_H

#include <QScrollArea>
#include <QUrl>

/**
 * @brief shellkit fork: a page DolphinViewContainer shows in place of the view.
 *
 * The Home, This PC and Network pages (isShellkitPageUrl()) have no folder behind them.
 */
class ShellkitPage : public QScrollArea
{
    Q_OBJECT

public:
    using QScrollArea::QScrollArea;

    /** Reads the page's content again; called whenever the page is shown. */
    virtual void refresh() = 0;

    /** Number of items on the page, for the status bar. */
    virtual int itemCount() const = 0;

Q_SIGNALS:
    void folderActivated(const QUrl &url);
    void folderInNewTab(const QUrl &url);
    /** The content changed after refresh(), so itemCount() did too. */
    void itemCountChanged();
};

#endif // SHELLKITPAGE_H
