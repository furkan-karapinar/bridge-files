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
#include <QPointer>
#include <QUrl>

class QLabel;
class QProgressBar;
class QToolButton;
class QWidget;
class SpeedGraph;

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
 * The default layout is the Windows 11 one ("45% complete", pause, cancel, and on request a
 * speed graph with the current file, time and items left). GeneralSettings::classicCopyDialog()
 * switches to the Windows 7 one: a banner, a table of details, a progress bar and Cancel.
 */
class DOLPHIN_EXPORT CopyProgressDialog : public QDialog
{
    Q_OBJECT

public:
    /** Shows a dialog for \a job if it is still running after a moment. */
    static void track(KIO::CopyJob *job, QWidget *window);

    /** Same for the trash or delete job that  job starts after asking the user. */
    static void trackDeletion(KIO::DeleteOrTrashJob *job, QWidget *window);

private:
    static void trackJob(KIO::Job *job, QWidget *window);
    CopyProgressDialog(KIO::Job *job, QWidget *parent);

    void setupModern();
    void setupClassic();
    void updateHeader();
    void updateProgress();
    void updateDetails();
    void setDetailsShown(bool shown);

    QPointer<KIO::Job> m_job;
    const bool m_classic;
    QUrl m_from;
    QUrl m_to;
    int m_itemCount = 0;
    bool m_moving = false;

    QLabel *m_header = nullptr;
    QLabel *m_percent = nullptr;
    QLabel *m_timeLine = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_detailsButton = nullptr;
    QWidget *m_details = nullptr;
    SpeedGraph *m_graph = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_fromLabel = nullptr;
    QLabel *m_toLabel = nullptr;
    QLabel *m_speed = nullptr;
    QLabel *m_timeLeft = nullptr;
    QLabel *m_itemsLeft = nullptr;

    qulonglong m_totalBytes = 0;
    qulonglong m_processedBytes = 0;
    qulonglong m_totalFiles = 0;
    qulonglong m_processedFiles = 0;
    unsigned long m_bytesPerSecond = 0;
};

#endif // COPYPROGRESSDIALOG_H
