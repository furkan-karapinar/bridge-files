/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FILEOPERATIONS_H
#define FILEOPERATIONS_H

#include "dolphin_export.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStack>
#include <QUrl>

#include <functional>
#include <optional>

class KJob;
class QDBusError;
class QWidget;

/**
 * shellkit fork: copy, move, link, trash and delete run in a service of their own,
 * shellkit-fileops (D-Bus name org.shellkit.FileOperations), so they go on when the File
 * Explorer window closes, as in Windows. The service checks every caller: requests from the
 * File Explorer run at once, requests from any other program wait until the user allows them.
 *
 * When the service cannot be reached the operation runs inside the File Explorer as before.
 */
namespace FileOps
{
inline constexpr char serviceName[] = "org.shellkit.FileOperations";
inline constexpr char objectPath[] = "/FileOperations";
inline constexpr char interfaceName[] = "org.shellkit.FileOperations";

enum class Type {
    Copy,
    Move,
    Link,
    CopyAs, ///< one source, the destination is the new name (Duplicate)
    Trash,
    Delete,
    EmptyTrash,
};

DOLPHIN_EXPORT QString typeName(Type type);
DOLPHIN_EXPORT std::optional<Type> typeFromName(const QString &name);
/** Whether \a type needs a destination. */
DOLPHIN_EXPORT bool hasDestination(Type type);
}

/**
 * Runs one operation in the current process: creates the KIO job, shows the copy dialog,
 * records the operation for undo and collects the items it created directly in the
 * destination (for selecting them afterwards). Deletes itself when the job is done.
 * Used by the service and, as a fallback, by the File Explorer itself.
 */
class DOLPHIN_EXPORT FileOperationRunner : public QObject
{
    Q_OBJECT

public:
    /**
     * \a window is the parent of dialogs and may be null (in the service). With
     * \a showErrors the job's UI delegate shows errors itself; otherwise the caller does.
     */
    static FileOperationRunner *
    start(FileOps::Type type, const QList<QUrl> &sources, const QUrl &destination, QWidget *window, bool showErrors);

    /** Most created items reported back; nobody selects more. */
    static constexpr int MaxCreated = 5000;

Q_SIGNALS:
    void finished(int error, const QString &errorText, const QList<QUrl> &created);

private:
    FileOperationRunner(FileOps::Type type, const QUrl &destination);
    void addCreated(const QUrl &url);
    void onResult(KJob *job);

    const FileOps::Type m_type;
    QUrl m_createdIn;
    QList<QUrl> m_created;
};

/**
 * Handle of an operation started with FileOperations::start(). Emits finished() once and
 * deletes itself afterwards.
 */
class DOLPHIN_EXPORT FileOperation : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

Q_SIGNALS:
    /**
     * \a created holds the items created directly in the destination. \a errorShown is true
     * when the user has already seen the error (the service shows its own error dialogs).
     */
    void finished(int error, const QString &errorText, const QList<QUrl> &created, bool errorShown);

private:
    friend class FileOperations;
    void finish(int error, const QString &errorText, const QList<QUrl> &created, bool errorShown);
    bool m_finished = false;
};

/**
 * The File Explorer side: sends operations to the service and keeps one undo history over
 * the service's operations and the ones that still run here (rename, new folder, ...).
 */
class DOLPHIN_EXPORT FileOperations : public QObject
{
    Q_OBJECT

public:
    static FileOperations *self();

    /** Starts \a type; \a window is the window the request comes from. */
    FileOperation *start(FileOps::Type type, const QList<QUrl> &sources, const QUrl &destination, QWidget *window);

    bool isUndoAvailable() const;
    QString undoText() const;
    bool isRedoAvailable() const;
    QString redoText() const;
    void undo(QWidget *window);
    void redo(QWidget *window);

Q_SIGNALS:
    void undoStateChanged();

private Q_SLOTS:
    void slotFinished(uint id, int error, const QString &errorText, const QStringList &created);
    void slotUndoStateChanged(bool canUndo, const QString &undoText, qlonglong undoStamp, bool canRedo, const QString &redoText, qlonglong redoStamp);

private:
    enum class Side {
        None,
        Local,
        Service,
    };

    FileOperations();
    void startInService(uint id, FileOps::Type type, const QList<QUrl> &sources, const QUrl &destination, QWidget *window);
    void startLocally(uint id, FileOps::Type type, const QList<QUrl> &sources, const QUrl &destination, QWidget *window);
    void callService(const QString &method, const QVariantList &arguments, QWidget *window, const std::function<void(const QDBusError &)> &onError);
    void serviceGone();
    void syncLocalStamps();
    Side undoSide() const;
    Side redoSide() const;

    uint m_nextId = 0;
    bool m_serviceUnavailable = false;
    QHash<uint, QPointer<FileOperation>> m_pending;

    // Undo order: when each side's last command was done (undo) or undone (redo).
    QStack<qint64> m_localUndoStamps;
    QStack<qint64> m_localRedoStamps;
    bool m_localRedoing = false;
    bool m_serviceCanUndo = false;
    bool m_serviceCanRedo = false;
    QString m_serviceUndoText;
    QString m_serviceRedoText;
    qint64 m_serviceUndoStamp = 0;
    qint64 m_serviceRedoStamp = 0;
};

/** A time stamp that grows on every call, in milliseconds since the epoch. */
DOLPHIN_EXPORT qint64 fileOpsStamp();

#endif // FILEOPERATIONS_H
