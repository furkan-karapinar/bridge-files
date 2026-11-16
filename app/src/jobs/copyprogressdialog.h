/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COPYPROGRESSDIALOG_H
#define COPYPROGRESSDIALOG_H

#include "dolphin_export.h"

#include <KJob>

#include <QDialog>
#include <QList>

class CopyProgressItem;
class QCloseEvent;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace KIO
{
class CopyJob;
class DeleteOrTrashJob;
class Job;
}

/**
 * @brief shellkit fork: the Windows copy dialog.
 *
 * Shows the progress of a copy or move in a small window of its own instead of a Plasma
 * notification. Jobs passed here should be created with KIO::HideProgressInfo. Moving to the
 * trash and deleting use the same window ("Deleting 5 items from Documents").
 *
 * As in Windows, operations that run at the same time share one window ("2 operations
 * running"), one section each; the file operations service has one such window, a File
 * Explorer window running operations itself one per window.
 *
 * The default layout is the Windows 11 one ("45% complete", pause, cancel, and on request a
 * speed graph with the current file, time and items left). GeneralSettings::classicCopyDialog()
 * switches to the Windows 7 one: a banner, a table of details, a progress bar and Cancel.
 */
class DOLPHIN_EXPORT CopyProgressDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * Shows \a job in a dialog if it is still running after a moment. \a window may be null:
     * the dialog is then a window of its own (file operations service).
     */
    static void track(KIO::CopyJob *job, QWidget *window);

    /** Same for the trash or delete job that  job starts after asking the user. */
    static void trackDeletion(KIO::DeleteOrTrashJob *job, QWidget *window);
    /** A "Compress to ZIP file" job (CompressJob) in the same dialog. */
    static void trackCompression(KIO::Job *job, QWidget *window);

    /**
     * Hides the section of \a job while it asks the user something (ConflictHandler) and shows
     * it again afterwards, as Windows does with its single window.
     */
    static void setQuestionShown(KJob *job, bool shown);

protected:
    // Closing the window (Alt+F4, the title bar, Esc) cancels its jobs, as in Windows.
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    explicit CopyProgressDialog(QWidget *parent);
    static void trackJob(KIO::Job *job, QWidget *window);
    static CopyProgressDialog *dialogFor(QWidget *window);

    void addJob(KIO::Job *job);
    void removeItem(CopyProgressItem *item);
    /** Title, separators and visibility after a section came, went, hid or changed. */
    void updateWindow();
    void setDetailsShown(bool shown);
    void showWindow();

    QWidget *const m_owner;
    const bool m_classic;
    QVBoxLayout *m_itemLayout = nullptr;
    QToolButton *m_detailsButton = nullptr;
    QList<CopyProgressItem *> m_items;
    bool m_detailsShown = true;
    bool m_closing = false;
};

#endif // COPYPROGRESSDIALOG_H
