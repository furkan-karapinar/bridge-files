/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FILEOPSSERVICE_H
#define FILEOPSSERVICE_H

#include <QDBusContext>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStack>

class QDBusServiceWatcher;
class QTimer;

/**
 * shellkit fork: the file operations service (org.shellkit.FileOperations).
 *
 * Every call is checked. The caller's process is looked up through the bus (its PID, then
 * /proc/PID/exe): a call from the File Explorer of the same installation runs at once; a call
 * from any other program shows a window that names the program and the operation, and the
 * operation only starts once the user allows it. Undo and redo are only taken from the File
 * Explorer. This keeps other programs from using the service quietly; it is not a wall
 * against code that already runs as the same user, which can touch the files itself.
 *
 * The service quits on its own when nothing is running, no window is open and no File
 * Explorer still needs its undo history.
 */
class FileOpsService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.shellkit.FileOperations")

public:
    explicit FileOpsService(QObject *parent = nullptr);

    /** Longest list of sources one call may pass. */
    static constexpr int MaxSources = 100000;
    /** Requests of one other program that may wait for the user at the same time. */
    static constexpr int MaxPendingPerCaller = 3;

public Q_SLOTS:
    /**
     * Starts \a operation (copy, move, link, copyas, trash, delete, emptytrash). \a id is the
     * caller's own number for it, sent back with Finished. \a activationToken may be empty.
     */
    Q_SCRIPTABLE void Run(uint id, const QString &operation, const QStringList &sources, const QString &destination, const QString &activationToken);
    Q_SCRIPTABLE void Undo(const QString &activationToken);
    Q_SCRIPTABLE void Redo(const QString &activationToken);

Q_SIGNALS:
    // Declared for introspection; both are sent only to the callers concerned.
    Q_SCRIPTABLE void Finished(uint id, int error, const QString &errorText, const QStringList &created);
    Q_SCRIPTABLE void UndoStateChanged(bool canUndo, const QString &undoText, qlonglong undoStamp, bool canRedo, const QString &redoText, qlonglong redoStamp);

private:
    struct Caller {
        QString busName;
        uint pid = 0;
        QString executable;
        QString commandLine;
        bool trusted = false;
    };

    Caller identifyCaller();
    void runAllowed(const Caller &caller, uint id, int type, const QList<QUrl> &sources, const QUrl &destination, const QString &activationToken);
    void askUser(const Caller &caller, uint id, int type, const QList<QUrl> &sources, const QUrl &destination, const QString &activationToken);
    void sendFinished(const QString &busName, uint id, int error, const QString &errorText, const QStringList &created);
    void sendUndoState();
    void addClient(const QString &busName);
    void useToken(const QString &activationToken);
    void checkIdle();

    QSet<QString> m_clients; ///< File Explorers that may still undo
    QDBusServiceWatcher *m_clientWatcher = nullptr;
    QHash<QString, int> m_pendingConsents;
    int m_runningJobs = 0;
    QTimer *m_idleTimer = nullptr;

    QStack<qint64> m_undoStamps;
    QStack<qint64> m_redoStamps;
    bool m_redoing = false;
};

#endif // FILEOPSSERVICE_H
