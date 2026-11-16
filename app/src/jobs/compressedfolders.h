/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef COMPRESSEDFOLDERS_H
#define COMPRESSEDFOLDERS_H

#include "dolphin_export.h"

#include <KFileItem>
#include <KIO/Job>

#include <QDialog>
#include <QMutex>
#include <QUrl>

#include <atomic>
#include <functional>

class QCheckBox;
class QLineEdit;
class QThread;
class QTimer;

/**
 * @brief shellkit fork: compressed (zipped) folders as in Windows.
 *
 * "Compress to ZIP file" writes the selection into "name.zip" next to it (CompressJob, shown in
 * the copy dialog) and then lets the user rename it; "Extract All..." asks for a folder
 * (ExtractDialog) and copies the archive's contents there through the file operations service,
 * reading it with KIO's archive worker (zip:/, tar:/). Opening an archive shows it as a folder.
 */
namespace CompressedFolders
{
/** Whether \a item is an archive that "Extract All..." can open. */
DOLPHIN_EXPORT bool canExtract(const KFileItem &item);
/** The archive's contents as a folder (zip:/path/a.zip/), or empty. */
DOLPHIN_EXPORT QUrl archiveRoot(const KFileItem &item);
/** Compresses \a items (all local, in one folder) into a new zip file next to them; \a created
 * gets the new file once it is written. */
DOLPHIN_EXPORT void compress(const KFileItemList &items, QWidget *window, const std::function<void(const QUrl &)> &created);
/** Asks where to extract \a archive and extracts it; \a show opens the folder afterwards. */
DOLPHIN_EXPORT void extractAll(const KFileItem &archive, QWidget *window, const std::function<void(const QUrl &)> &show);
}

/**
 * Writes local files and folders into a zip file (deflate, UTF-8 names, Zip64 when needed)
 * on a thread. Reports bytes and files like a copy job; killing it removes the partial file.
 */
class DOLPHIN_EXPORT CompressJob : public KIO::Job
{
    Q_OBJECT

public:
    CompressJob(const QList<QUrl> &sources, const QUrl &archive);
    ~CompressJob() override;

    QList<QUrl> sources() const
    {
        return m_sources;
    }

    QUrl archive() const
    {
        return m_archive;
    }

    void start() override;

Q_SIGNALS:
    /** The file being compressed now. */
    void compressing(KIO::Job *job, const QUrl &file);

protected:
    bool doKill() override;
    bool doSuspend() override;
    bool doResume() override;

private:
    struct Result {
        int error = 0;
        QString text;
    };
    Result run();
    void report();
    void stopThread();

    const QList<QUrl> m_sources;
    const QUrl m_archive;
    QThread *m_thread = nullptr;
    QTimer *m_timer = nullptr;
    std::atomic_bool m_stop = false;
    std::atomic_bool m_paused = false;
    std::atomic<qulonglong> m_totalBytes = 0;
    std::atomic<qulonglong> m_doneBytes = 0;
    std::atomic<qulonglong> m_totalFiles = 0;
    std::atomic<qulonglong> m_doneFiles = 0;
    QMutex m_currentLock;
    QString m_current;
    qulonglong m_lastBytes = 0;
};

/** "Extract Compressed (Zipped) Folders": where to, and whether to show the files after. */
class DOLPHIN_EXPORT ExtractDialog : public QDialog
{
    Q_OBJECT

public:
    ExtractDialog(const KFileItem &archive, QWidget *parent);

    QUrl destination() const;
    bool showWhenDone() const;

private:
    QLineEdit *m_folder = nullptr;
    QCheckBox *m_show = nullptr;
};

#endif // COMPRESSEDFOLDERS_H
