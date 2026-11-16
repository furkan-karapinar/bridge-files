/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef CONFLICTDIALOG_H
#define CONFLICTDIALOG_H

#include "dolphin_export.h"

#include <KIO/WidgetsAskUserActionHandler>

/**
 * @brief shellkit fork: asks "Replace or Skip Files" the Windows way.
 *
 * Handed to a copy or move job's UI delegate (FileOperationRunner). When a file of the same name
 * is already in the destination, the Windows 11 dialog offers to replace it, skip it or compare
 * both files; the Windows 7 look (or the classic copy dialog) shows "Copy and Replace",
 * "Don't copy" and "Copy, but keep both files" with the details of each file. Keeping both
 * names the new file "name (2).ext". Everything else (folders, resuming, errors) goes to KIO's
 * own dialogs.
 */
class DOLPHIN_EXPORT ConflictHandler : public KIO::WidgetsAskUserActionHandler
{
    Q_OBJECT

public:
    explicit ConflictHandler(QObject *parent = nullptr);

    void askUserRename(KJob *job,
                       const QString &title,
                       const QUrl &src,
                       const QUrl &dest,
                       KIO::RenameDialog_Options options,
                       KIO::filesize_t sizeSrc,
                       KIO::filesize_t sizeDest,
                       const QDateTime &ctimeSrc = {},
                       const QDateTime &ctimeDest = {},
                       const QDateTime &mtimeSrc = {},
                       const QDateTime &mtimeDest = {}) override;

    /** The name Windows gives the second copy: "name (2).ext", the first number that is free. */
    static QUrl keepBothUrl(const QUrl &dest);
};

#endif // CONFLICTDIALOG_H
