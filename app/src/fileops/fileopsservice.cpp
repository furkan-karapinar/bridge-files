/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fileopsservice.h"

#include "jobs/fileoperations.h"
#include "shellkit.h"

#include <KIO/FileUndoManager>
#include <KIO/Global>
#include <KLocalizedString>
#include <KWindowSystem>

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <unistd.h>

namespace
{
constexpr int IdleCheckInterval = 30 * 1000;
/** How long the Allow button stays disabled, so a click meant for another window cannot hit it. */
constexpr int AllowDelay = 1000;
constexpr int ListedItems = 5;

QString busName()
{
    return QString::fromLatin1(FileOps::serviceName);
}

QString programName(const QString &executable)
{
    return executable.isEmpty() ? i18ndc(shellkitDomain, "@info", "An unknown program") : QFileInfo(executable).fileName();
}

QString displayUrl(const QUrl &url)
{
    return url.toDisplayString(QUrl::PreferLocalFile);
}

/** The sentence that says what the other program asks for. */
QString requestText(FileOps::Type type, const QString &program, int count, const QUrl &destination)
{
    const QString target = displayUrl(destination);
    switch (type) {
    case FileOps::Type::Copy:
        return i18ndcp(shellkitDomain, "@info %2 is a program, %3 a folder", "%2 wants to copy 1 item to %3.", "%2 wants to copy %1 items to %3.", count, program, target);
    case FileOps::Type::CopyAs:
        return i18ndc(shellkitDomain, "@info %1 is a program, %2 the new name", "%1 wants to make a copy named %2.", program, target);
    case FileOps::Type::Move:
        return i18ndcp(shellkitDomain, "@info %2 is a program, %3 a folder", "%2 wants to move 1 item to %3.", "%2 wants to move %1 items to %3.", count, program, target);
    case FileOps::Type::Link:
        return i18ndcp(shellkitDomain,
                       "@info %2 is a program, %3 a folder",
                       "%2 wants to create a link to 1 item in %3.",
                       "%2 wants to create links to %1 items in %3.",
                       count,
                       program,
                       target);
    case FileOps::Type::Trash:
        return i18ndcp(shellkitDomain,
                       "@info %2 is a program",
                       "%2 wants to move 1 item to the Recycle Bin.",
                       "%2 wants to move %1 items to the Recycle Bin.",
                       count,
                       program);
    case FileOps::Type::Delete:
        return i18ndcp(shellkitDomain, "@info %2 is a program", "%2 wants to permanently delete 1 item.", "%2 wants to permanently delete %1 items.", count, program);
    case FileOps::Type::EmptyTrash:
        return i18ndc(shellkitDomain, "@info %1 is a program", "%1 wants to empty the Recycle Bin.", program);
    }
    return QString();
}

QLabel *plainLabel(const QString &text, QWidget *parent)
{
    // Plain text only: names of files and programs must never be read as markup.
    auto label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

/**
 * Asks the user whether a program other than the File Explorer may run a file operation.
 * Deny is the default button, Escape and closing the window deny as well.
 */
class ConsentDialog : public QDialog
{
public:
    ConsentDialog(const QString &executable, const QString &commandLine, uint pid, FileOps::Type type, const QList<QUrl> &sources, const QUrl &destination)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(i18ndc(shellkitDomain, "@title:window", "Allow file operation?"));
        setWindowIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));
        setWindowFlag(Qt::WindowStaysOnTopHint);

        auto icon = new QLabel(this);
        const int iconSize = style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, this);
        icon->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-warning")).pixmap(iconSize));
        icon->setAlignment(Qt::AlignTop);

        auto heading = plainLabel(requestText(type, programName(executable), sources.size(), destination), this);
        QFont headingFont = heading->font();
        headingFont.setBold(true);
        heading->setFont(headingFont);

        auto text = new QVBoxLayout;
        text->setSpacing(8);
        text->addWidget(heading);

        if (!sources.isEmpty()) {
            QStringList names;
            for (int i = 0; i < sources.size() && i < ListedItems; ++i) {
                names.append(displayUrl(sources.at(i)));
            }
            if (sources.size() > ListedItems) {
                names.append(i18ndcp(shellkitDomain, "@info", "and 1 more item", "and %1 more items", sources.size() - ListedItems));
            }
            text->addWidget(plainLabel(names.join(QLatin1Char('\n')), this));
        }

        QStringList details;
        details.append(i18ndc(shellkitDomain, "@info %1 is a path", "Program: %1", executable.isEmpty() ? programName(executable) : executable));
        if (!commandLine.isEmpty()) {
            details.append(i18ndc(shellkitDomain, "@info %1 is a command line", "Command: %1", commandLine));
        }
        if (pid) {
            details.append(i18ndc(shellkitDomain, "@info", "Process ID: %1", QString::number(pid)));
        }
        auto detailsLabel = plainLabel(details.join(QLatin1Char('\n')), this);
        detailsLabel->setEnabled(false);
        text->addWidget(detailsLabel);
        text->addWidget(plainLabel(i18ndc(shellkitDomain, "@info", "Only allow this if you asked that program to do it."), this));

        auto body = new QHBoxLayout;
        body->setSpacing(16);
        body->addWidget(icon);
        body->addLayout(text, 1);

        auto buttons = new QDialogButtonBox(this);
        QPushButton *allow = buttons->addButton(i18ndc(shellkitDomain, "@action:button", "Allow"), QDialogButtonBox::AcceptRole);
        QPushButton *deny = buttons->addButton(i18ndc(shellkitDomain, "@action:button", "Deny"), QDialogButtonBox::RejectRole);
        deny->setDefault(true);
        deny->setFocus();
        allow->setEnabled(false);
        QTimer::singleShot(AllowDelay, allow, [allow]() {
            allow->setEnabled(true);
        });
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto layout = new QVBoxLayout(this);
        layout->setSpacing(16);
        layout->addLayout(body);
        layout->addWidget(buttons);
        setMinimumWidth(460);
        setMaximumWidth(640);
    }
};
}

FileOpsService::FileOpsService(QObject *parent)
    : QObject(parent)
{
    m_clientWatcher = new QDBusServiceWatcher(this);
    m_clientWatcher->setConnection(QDBusConnection::sessionBus());
    m_clientWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_clientWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &name) {
        m_clients.remove(name);
        m_clientWatcher->removeWatchedService(name);
        m_pendingConsents.remove(name);
    });

    // Quit after two idle checks in a row, so a window that just closed or a call on its way
    // gets its time.
    m_idleTimer = new QTimer(this);
    m_idleTimer->setInterval(IdleCheckInterval);
    connect(m_idleTimer, &QTimer::timeout, this, &FileOpsService::checkIdle);
    m_idleTimer->start();

    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    connect(undoManager, &KIO::FileUndoManager::jobRecordingFinished, this, [this]() {
        if (m_redoing) {
            return;
        }
        m_undoStamps.push(fileOpsStamp());
        m_redoStamps.clear();
        sendUndoState();
    });
    connect(undoManager, &KIO::FileUndoManager::undoJobFinished, this, [this]() {
        m_redoing = false;
        sendUndoState();
    });
    connect(undoManager, &KIO::FileUndoManager::undoAvailable, this, &FileOpsService::sendUndoState);
    connect(undoManager, &KIO::FileUndoManager::redoAvailable, this, &FileOpsService::sendUndoState);
    connect(undoManager, &KIO::FileUndoManager::undoTextChanged, this, &FileOpsService::sendUndoState);
    connect(undoManager, &KIO::FileUndoManager::redoTextChanged, this, &FileOpsService::sendUndoState);
}

FileOpsService::Caller FileOpsService::identifyCaller()
{
    Caller caller;
    caller.busName = message().service();
    QDBusConnectionInterface *bus = connection().interface();
    const QDBusReply<uint> pid = bus->servicePid(caller.busName);
    const QDBusReply<uint> uid = bus->serviceUid(caller.busName);
    if (!pid.isValid() || !uid.isValid()) {
        return caller;
    }
    caller.pid = pid.value();

    QString executable = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(caller.pid));
    // A program whose file was replaced (a rebuilt File Explorer) still points at its old path.
    const QLatin1String deleted(" (deleted)");
    if (executable.endsWith(deleted)) {
        executable.chop(deleted.size());
    }
    caller.executable = executable;

    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(caller.pid));
    if (cmdline.open(QIODevice::ReadOnly)) {
        QByteArray bytes = cmdline.read(1024);
        bytes.replace('\0', ' ');
        caller.commandLine = QString::fromLocal8Bit(bytes).trimmed();
    }

    // Trusted: the File Explorer installed beside this service, run by this user.
    const QString explorer = QFileInfo(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("shellkit-files"))).canonicalFilePath();
    const QString callerPath = executable.isEmpty() ? QString() : QFileInfo(executable).canonicalFilePath();
    caller.trusted = uid.value() == getuid() && !explorer.isEmpty() && callerPath == explorer;
    return caller;
}

void FileOpsService::Run(uint id, const QString &operation, const QStringList &sources, const QString &destination, const QString &activationToken)
{
    const std::optional<FileOps::Type> type = FileOps::typeFromName(operation);
    if (!type) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Unknown operation: %1").arg(operation));
        return;
    }
    if (sources.size() > MaxSources) {
        sendErrorReply(QDBusError::LimitsExceeded, QStringLiteral("Too many items"));
        return;
    }

    QList<QUrl> urls;
    urls.reserve(sources.size());
    for (const QString &source : sources) {
        const QUrl url(source, QUrl::StrictMode);
        if (!url.isValid() || url.isRelative()) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Not a valid URL: %1").arg(source));
            return;
        }
        urls.append(url);
    }

    QUrl destinationUrl;
    if (FileOps::hasDestination(*type)) {
        destinationUrl = QUrl(destination, QUrl::StrictMode);
        if (!destinationUrl.isValid() || destinationUrl.isRelative()) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Not a valid destination: %1").arg(destination));
            return;
        }
        if (urls.isEmpty() || (*type == FileOps::Type::CopyAs && urls.size() != 1)) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Wrong number of sources"));
            return;
        }
    } else if (*type == FileOps::Type::EmptyTrash) {
        urls.clear();
    } else if (urls.isEmpty()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("No items"));
        return;
    }

    const Caller caller = identifyCaller();
    if (caller.trusted) {
        addClient(caller.busName);
        runAllowed(caller, id, int(*type), urls, destinationUrl, activationToken);
        return;
    }
    if (m_pendingConsents.value(caller.busName) >= MaxPendingPerCaller) {
        sendErrorReply(QDBusError::LimitsExceeded, QStringLiteral("Too many requests are waiting for the user"));
        return;
    }
    askUser(caller, id, int(*type), urls, destinationUrl, activationToken);
}

void FileOpsService::runAllowed(const Caller &caller, uint id, int type, const QList<QUrl> &sources, const QUrl &destination, const QString &activationToken)
{
    useToken(activationToken);
    ++m_runningJobs;
    auto runner = FileOperationRunner::start(FileOps::Type(type), sources, destination, nullptr, true);
    const QString client = caller.busName;
    connect(runner, &FileOperationRunner::finished, this, [this, client, id](int error, const QString &errorText, const QList<QUrl> &created) {
        QStringList urls;
        urls.reserve(created.size());
        for (const QUrl &url : created) {
            urls.append(url.toString());
        }
        sendFinished(client, id, error, errorText, urls);
        --m_runningJobs;
    });
}

void FileOpsService::askUser(const Caller &caller, uint id, int type, const QList<QUrl> &sources, const QUrl &destination, const QString &activationToken)
{
    ++m_pendingConsents[caller.busName];
    auto dialog = new ConsentDialog(caller.executable, caller.commandLine, caller.pid, FileOps::Type(type), sources, destination);
    connect(dialog, &QDialog::finished, this, [this, caller, id, type, sources, destination](int result) {
        auto pending = m_pendingConsents.find(caller.busName);
        if (pending != m_pendingConsents.end() && --pending.value() <= 0) {
            m_pendingConsents.erase(pending);
        }
        if (result == QDialog::Accepted) {
            runAllowed(caller, id, type, sources, destination, QString());
        } else {
            sendFinished(caller.busName, id, KIO::ERR_ACCESS_DENIED, i18ndc(shellkitDomain, "@info", "The user did not allow this file operation."), {});
        }
    });
    useToken(activationToken);
    dialog->show();
    if (QWindow *window = dialog->windowHandle()) {
        KWindowSystem::activateWindow(window);
    }
}

void FileOpsService::Undo(const QString &activationToken)
{
    const Caller caller = identifyCaller();
    if (!caller.trusted) {
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Only the File Explorer may undo"));
        return;
    }
    addClient(caller.busName);
    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    if (!undoManager->isUndoAvailable()) {
        sendUndoState();
        return;
    }
    if (!m_undoStamps.isEmpty()) {
        m_undoStamps.pop();
    }
    m_redoStamps.push(fileOpsStamp());
    useToken(activationToken);
    undoManager->uiInterface()->setParentWidget(nullptr);
    undoManager->undo();
    sendUndoState();
}

void FileOpsService::Redo(const QString &activationToken)
{
    const Caller caller = identifyCaller();
    if (!caller.trusted) {
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Only the File Explorer may redo"));
        return;
    }
    addClient(caller.busName);
    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    if (!undoManager->isRedoAvailable()) {
        sendUndoState();
        return;
    }
    if (!m_redoStamps.isEmpty()) {
        m_redoStamps.pop();
    }
    m_undoStamps.push(fileOpsStamp());
    m_redoing = true;
    useToken(activationToken);
    undoManager->uiInterface()->setParentWidget(nullptr);
    undoManager->redo();
    sendUndoState();
}

void FileOpsService::sendFinished(const QString &client, uint id, int error, const QString &errorText, const QStringList &created)
{
    QDBusMessage signal = QDBusMessage::createTargetedSignal(client,
                                                             QString::fromLatin1(FileOps::objectPath),
                                                             QString::fromLatin1(FileOps::interfaceName),
                                                             QStringLiteral("Finished"));
    signal << id << error << errorText << created;
    QDBusConnection::sessionBus().send(signal);
}

void FileOpsService::sendUndoState()
{
    KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    const bool canUndo = undoManager->isUndoAvailable();
    const bool canRedo = undoManager->isRedoAvailable();
    if (!canUndo) {
        m_undoStamps.clear();
    }
    if (!canRedo) {
        m_redoStamps.clear();
    }
    const qlonglong undoStamp = m_undoStamps.isEmpty() ? 0 : m_undoStamps.top();
    const qlonglong redoStamp = m_redoStamps.isEmpty() ? 0 : m_redoStamps.top();
    for (const QString &client : std::as_const(m_clients)) {
        QDBusMessage signal = QDBusMessage::createTargetedSignal(client,
                                                                 QString::fromLatin1(FileOps::objectPath),
                                                                 QString::fromLatin1(FileOps::interfaceName),
                                                                 QStringLiteral("UndoStateChanged"));
        signal << canUndo << undoManager->undoText() << undoStamp << canRedo << undoManager->redoText() << redoStamp;
        QDBusConnection::sessionBus().send(signal);
    }
}

void FileOpsService::addClient(const QString &client)
{
    if (client.isEmpty() || m_clients.contains(client)) {
        return;
    }
    m_clients.insert(client);
    m_clientWatcher->addWatchedService(client);
}

void FileOpsService::useToken(const QString &activationToken)
{
    if (activationToken.isEmpty()) {
        return;
    }
    // Used by the next window that asks to be activated, and by Qt for a new window.
    KWindowSystem::setCurrentXdgActivationToken(activationToken);
    qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
}

void FileOpsService::checkIdle()
{
    static int idleChecks = 0;
    bool windowOpen = false;
    const auto widgets = QApplication::topLevelWidgets();
    for (const QWidget *widget : widgets) {
        if (widget->isVisible()) {
            windowOpen = true;
            break;
        }
    }
    const KIO::FileUndoManager *undoManager = KIO::FileUndoManager::self();
    const bool undoWanted = !m_clients.isEmpty() && (undoManager->isUndoAvailable() || undoManager->isRedoAvailable());
    const bool idle = m_runningJobs == 0 && m_pendingConsents.isEmpty() && !windowOpen && !undoWanted;
    idleChecks = idle ? idleChecks + 1 : 0;
    if (idleChecks < 2) {
        return;
    }

    // Give the name up first: a call sent from now on starts a new service, and a call that
    // is already here is still answered before the next check.
    QDBusConnection::sessionBus().unregisterService(busName());
    QTimer::singleShot(1000, this, [this]() {
        if (m_runningJobs == 0 && m_pendingConsents.isEmpty()) {
            QCoreApplication::quit();
        } else {
            QDBusConnection::sessionBus().registerService(busName());
        }
    });
}

#include "moc_fileopsservice.cpp"
