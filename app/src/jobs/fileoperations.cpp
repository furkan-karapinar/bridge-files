/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fileoperations.h"

#include "conflictdialog.h"
#include "copyprogressdialog.h"
#include "shellkit.h"

#include <KIO/AskUserActionInterface>
#include <KIO/CopyJob>
#include <KIO/DeleteOrTrashJob>
#include <KIO/FileUndoManager>
#include <KIO/Global>
#include <KIO/JobUiDelegate>
#include <KIO/JobUiDelegateFactory>
#include <KJobUiDelegate>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KWaylandExtras>
#include <KWindowSystem>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QFuture>
#include <QWidget>
#include <QWindow>

namespace
{
struct TypeEntry {
    FileOps::Type type;
    const char *name;
};

constexpr TypeEntry typeNames[] = {
    {FileOps::Type::Copy, "copy"},
    {FileOps::Type::Move, "move"},
    {FileOps::Type::Link, "link"},
    {FileOps::Type::CopyAs, "copyas"},
    {FileOps::Type::Trash, "trash"},
    {FileOps::Type::Delete, "delete"},
    {FileOps::Type::EmptyTrash, "emptytrash"},
};

// KIO's widget delegate with our own "ask the user" handler; its constructor is protected.
class ShellkitJobUiDelegate : public KIO::JobUiDelegate
{
public:
    ShellkitJobUiDelegate(KJobUiDelegate::Flags flags, QWidget *window, KIO::AskUserActionInterface *askUser)
        : KIO::JobUiDelegate(flags, window, {askUser})
    {
    }
};
}

QString FileOps::typeName(Type type)
{
    for (const auto &entry : typeNames) {
        if (entry.type == type) {
            return QString::fromLatin1(entry.name);
        }
    }
    return QString();
}

std::optional<FileOps::Type> FileOps::typeFromName(const QString &name)
{
    for (const auto &entry : typeNames) {
        if (name == QLatin1String(entry.name)) {
            return entry.type;
        }
    }
    return std::nullopt;
}

bool FileOps::hasDestination(Type type)
{
    return type == Type::Copy || type == Type::Move || type == Type::Link || type == Type::CopyAs;
}

qint64 fileOpsStamp()
{
    static qint64 last = 0;
    last = std::max(QDateTime::currentMSecsSinceEpoch(), last + 1);
    return last;
}

// FileOperationRunner

FileOperationRunner::FileOperationRunner(FileOps::Type type, const QUrl &destination)
    : m_type(type)
{
    // Items are selected only when they are direct children of the destination folder; the
    // destination of CopyAs is the new item itself.
    m_createdIn = type == FileOps::Type::CopyAs ? destination.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash)
                                                : destination.adjusted(QUrl::StripTrailingSlash);
}

FileOperationRunner *
FileOperationRunner::start(FileOps::Type type, const QList<QUrl> &sources, const QUrl &destination, QWidget *window, bool showErrors)
{
    using Iface = KIO::AskUserActionInterface;
    auto runner = new FileOperationRunner(type, destination);
    const KJobUiDelegate::Flags flags = showErrors ? KJobUiDelegate::AutoHandlingEnabled : KJobUiDelegate::AutoWarningHandlingEnabled;

    if (FileOps::hasDestination(type)) {
        KIO::CopyJob *job = nullptr;
        switch (type) {
        case FileOps::Type::Copy:
            job = KIO::copy(sources, destination, KIO::HideProgressInfo);
            break;
        case FileOps::Type::Move:
            job = KIO::move(sources, destination, KIO::HideProgressInfo);
            break;
        case FileOps::Type::Link:
            job = KIO::link(sources, destination, KIO::HideProgressInfo);
            break;
        default:
            job = KIO::copyAs(sources.value(0), destination, KIO::HideProgressInfo);
            job->setAutoRename(true);
            break;
        }
        // shellkit fork: "Replace or Skip Files" as on Windows (ConflictHandler).
        job->setUiDelegate(new ShellkitJobUiDelegate(flags, window, new ConflictHandler(runner)));
        KJobWidgets::setWindow(job, window);

        connect(job, &KIO::CopyJob::copyingDone, runner, [runner](KIO::Job *, const QUrl &, const QUrl &to, const QDateTime &, bool, bool) {
            runner->addCreated(to);
        });
        connect(job, &KIO::CopyJob::copyingLinkDone, runner, [runner](KIO::Job *, const QUrl &, const QString &, const QUrl &to) {
            runner->addCreated(to);
        });
        connect(job, &KIO::CopyJob::moving, runner, [runner](KIO::Job *, const QUrl &, const QUrl &to) {
            runner->addCreated(to);
        });
        connect(job, &KIO::CopyJob::linking, runner, [runner](KIO::Job *, const QString &, const QUrl &to) {
            runner->addCreated(to);
        });
        connect(job, &KJob::result, runner, &FileOperationRunner::onResult);

        KIO::FileUndoManager::self()->recordCopyJob(job);
        CopyProgressDialog::track(job, window);
        return runner;
    }

    const Iface::DeletionType deletionType = type == FileOps::Type::Trash ? Iface::Trash
        : type == FileOps::Type::Delete                                   ? Iface::Delete
                                                                          : Iface::EmptyTrash;
    // The trash job records itself for undo. Asking for confirmation follows the user's
    // "do not ask again" choices, as before.
    auto job = new KIO::DeleteOrTrashJob(type == FileOps::Type::EmptyTrash ? QList<QUrl>() : sources, deletionType, Iface::DefaultConfirmation, window);
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(flags, window));
    connect(job, &KJob::result, runner, &FileOperationRunner::onResult);
    if (type != FileOps::Type::EmptyTrash) {
        CopyProgressDialog::trackDeletion(job, window);
    }
    job->start();
    return runner;
}

void FileOperationRunner::addCreated(const QUrl &url)
{
    if (m_created.size() >= MaxCreated || !url.isValid()) {
        return;
    }
    if (!url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).matches(m_createdIn, QUrl::StripTrailingSlash)) {
        return;
    }
    if (!m_created.contains(url)) {
        m_created.append(url);
    }
}

void FileOperationRunner::onResult(KJob *job)
{
    const int error = job->error();
    Q_EMIT finished(error, error ? job->errorString() : QString(), m_created);
    deleteLater();
}

// FileOperation

void FileOperation::finish(int error, const QString &errorText, const QList<QUrl> &created, bool errorShown)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    Q_EMIT finished(error, errorText, created, errorShown);
    deleteLater();
}

// FileOperations

FileOperations *FileOperations::self()
{
    static FileOperations *instance = new FileOperations();
    return instance;
}

FileOperations::FileOperations()
{
    // SHELLKIT_FILEOPS_LOCAL=1 keeps everything in this process (for comparisons and tests).
    m_serviceUnavailable = qEnvironmentVariableIntValue("SHELLKIT_FILEOPS_LOCAL") == 1;

    const QString service = QString::fromLatin1(FileOps::serviceName);
    const QString path = QString::fromLatin1(FileOps::objectPath);
    const QString iface = QString::fromLatin1(FileOps::interfaceName);
    QDBusConnection bus = QDBusConnection::sessionBus();
    // The service sends both signals only to its callers, not to the whole bus.
    bus.connect(service, path, iface, QStringLiteral("Finished"), this, SLOT(slotFinished(uint, int, QString, QStringList)));
    bus.connect(service,
                path,
                iface,
                QStringLiteral("UndoStateChanged"),
                this,
                SLOT(slotUndoStateChanged(bool, QString, qlonglong, bool, QString, qlonglong)));
    auto watcher = new QDBusServiceWatcher(service, bus, QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, &FileOperations::serviceGone);

    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    connect(undoManager, &KIO::FileUndoManager::jobRecordingFinished, this, [this]() {
        if (m_localRedoing) {
            return;
        }
        m_localUndoStamps.push(fileOpsStamp());
        m_localRedoStamps.clear();
        Q_EMIT undoStateChanged();
    });
    connect(undoManager, &KIO::FileUndoManager::undoJobFinished, this, [this]() {
        m_localRedoing = false;
        syncLocalStamps();
        Q_EMIT undoStateChanged();
    });
    const auto changed = [this]() {
        syncLocalStamps();
        Q_EMIT undoStateChanged();
    };
    connect(undoManager, &KIO::FileUndoManager::undoAvailable, this, changed);
    connect(undoManager, &KIO::FileUndoManager::redoAvailable, this, changed);
    connect(undoManager, &KIO::FileUndoManager::undoTextChanged, this, changed);
    connect(undoManager, &KIO::FileUndoManager::redoTextChanged, this, changed);
}

FileOperation *FileOperations::start(FileOps::Type type, const QList<QUrl> &sources, const QUrl &destination, QWidget *window)
{
    const uint id = ++m_nextId;
    auto operation = new FileOperation(this);
    m_pending.insert(id, operation);
    if (m_serviceUnavailable) {
        startLocally(id, type, sources, destination, window);
    } else {
        startInService(id, type, sources, destination, window);
    }
    return operation;
}

void FileOperations::startInService(uint id, FileOps::Type type, const QList<QUrl> &sources, const QUrl &destination, QWidget *window)
{
    QStringList sourceList;
    sourceList.reserve(sources.size());
    for (const QUrl &url : sources) {
        sourceList.append(url.toString());
    }
    const QPointer<QWidget> guard(window);
    callService(QStringLiteral("Run"),
                {QVariant::fromValue(id), FileOps::typeName(type), sourceList, destination.toString()},
                window,
                [this, id, type, sources, destination, guard](const QDBusError &error) {
                    // Not installed or not startable: do it here, as before the service existed.
                    if (error.type() == QDBusError::ServiceUnknown || error.name().startsWith(QLatin1String("org.freedesktop.DBus.Error.Spawn"))) {
                        startLocally(id, type, sources, destination, guard);
                        return;
                    }
                    if (const QPointer<FileOperation> operation = m_pending.take(id)) {
                        operation->finish(KIO::ERR_INTERNAL, error.message(), {}, false);
                    }
                });
}

void FileOperations::startLocally(uint id, FileOps::Type type, const QList<QUrl> &sources, const QUrl &destination, QWidget *window)
{
    auto runner = FileOperationRunner::start(type, sources, destination, window, false);
    connect(runner, &FileOperationRunner::finished, this, [this, id](int error, const QString &errorText, const QList<QUrl> &created) {
        if (const QPointer<FileOperation> operation = m_pending.take(id)) {
            operation->finish(error, errorText, created, false);
        }
    });
}

void FileOperations::callService(const QString &method,
                                 const QVariantList &arguments,
                                 QWidget *window,
                                 const std::function<void(const QDBusError &)> &onError)
{
    // The last argument is an activation token, so the service's windows may come to the
    // front on Wayland as if this window had opened them.
    const auto send = [this, method, arguments, onError](const QString &token) {
        QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(FileOps::serviceName),
                                                              QString::fromLatin1(FileOps::objectPath),
                                                              QString::fromLatin1(FileOps::interfaceName),
                                                              method);
        message.setArguments(QVariantList(arguments) << token);
        auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [onError](QDBusPendingCallWatcher *call) {
            call->deleteLater();
            if (call->isError() && onError) {
                onError(call->error());
            }
        });
    };

    QWindow *handle = window && window->window() ? window->window()->windowHandle() : nullptr;
    if (handle && KWindowSystem::isPlatformWayland()) {
        KWaylandExtras::xdgActivationToken(handle, QStringLiteral("org.shellkit.files")).then(this, send);
    } else {
        send(QString());
    }
}

void FileOperations::slotFinished(uint id, int error, const QString &errorText, const QStringList &created)
{
    const QPointer<FileOperation> operation = m_pending.take(id);
    if (!operation) {
        return;
    }
    QList<QUrl> urls;
    urls.reserve(created.size());
    for (const QString &url : created) {
        urls.append(QUrl(url));
    }
    // The service shows its errors itself, so the window it came from may already be closed.
    operation->finish(error, errorText, urls, true);
}

void FileOperations::slotUndoStateChanged(bool canUndo, const QString &undoText, qlonglong undoStamp, bool canRedo, const QString &redoText, qlonglong redoStamp)
{
    m_serviceCanUndo = canUndo;
    m_serviceUndoText = undoText;
    m_serviceUndoStamp = undoStamp;
    m_serviceCanRedo = canRedo;
    m_serviceRedoText = redoText;
    m_serviceRedoStamp = redoStamp;
    Q_EMIT undoStateChanged();
}

void FileOperations::serviceGone()
{
    const auto pending = m_pending;
    m_pending.clear();
    for (const QPointer<FileOperation> &operation : pending) {
        if (operation) {
            operation->finish(KIO::ERR_WORKER_DIED, i18ndc(shellkitDomain, "@info", "The file operations service stopped unexpectedly."), {}, false);
        }
    }
    m_serviceCanUndo = false;
    m_serviceCanRedo = false;
    m_serviceUndoText.clear();
    m_serviceRedoText.clear();
    Q_EMIT undoStateChanged();
}

void FileOperations::syncLocalStamps()
{
    const KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    if (!undoManager->isUndoAvailable()) {
        m_localUndoStamps.clear();
    }
    if (!undoManager->isRedoAvailable()) {
        m_localRedoStamps.clear();
    }
}

FileOperations::Side FileOperations::undoSide() const
{
    const bool local = KIO::FileUndoManager::self()->isUndoAvailable();
    if (local && m_serviceCanUndo) {
        const qint64 localStamp = m_localUndoStamps.isEmpty() ? 0 : m_localUndoStamps.top();
        return localStamp >= m_serviceUndoStamp ? Side::Local : Side::Service;
    }
    return local ? Side::Local : m_serviceCanUndo ? Side::Service : Side::None;
}

FileOperations::Side FileOperations::redoSide() const
{
    const bool local = KIO::FileUndoManager::self()->isRedoAvailable();
    if (local && m_serviceCanRedo) {
        const qint64 localStamp = m_localRedoStamps.isEmpty() ? 0 : m_localRedoStamps.top();
        return localStamp >= m_serviceRedoStamp ? Side::Local : Side::Service;
    }
    return local ? Side::Local : m_serviceCanRedo ? Side::Service : Side::None;
}

bool FileOperations::isUndoAvailable() const
{
    return undoSide() != Side::None;
}

QString FileOperations::undoText() const
{
    return undoSide() == Side::Service ? m_serviceUndoText : KIO::FileUndoManager::self()->undoText();
}

bool FileOperations::isRedoAvailable() const
{
    return redoSide() != Side::None;
}

QString FileOperations::redoText() const
{
    return redoSide() == Side::Service ? m_serviceRedoText : KIO::FileUndoManager::self()->redoText();
}

void FileOperations::undo(QWidget *window)
{
    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    switch (undoSide()) {
    case Side::Local:
        if (!m_localUndoStamps.isEmpty()) {
            m_localUndoStamps.pop();
        }
        m_localRedoStamps.push(fileOpsStamp());
        undoManager->uiInterface()->setParentWidget(window);
        undoManager->undo();
        break;
    case Side::Service:
        callService(QStringLiteral("Undo"), {}, window, {});
        break;
    case Side::None:
        break;
    }
    Q_EMIT undoStateChanged();
}

void FileOperations::redo(QWidget *window)
{
    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    switch (redoSide()) {
    case Side::Local:
        if (!m_localRedoStamps.isEmpty()) {
            m_localRedoStamps.pop();
        }
        m_localUndoStamps.push(fileOpsStamp());
        // A redo may record its job again; that is not a new command.
        m_localRedoing = true;
        undoManager->uiInterface()->setParentWidget(window);
        undoManager->redo();
        break;
    case Side::Service:
        callService(QStringLiteral("Redo"), {}, window, {});
        break;
    case Side::None:
        break;
    }
    Q_EMIT undoStateChanged();
}

#include "moc_fileoperations.cpp"
