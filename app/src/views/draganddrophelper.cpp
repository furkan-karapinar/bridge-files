/*
 * SPDX-FileCopyrightText: 2007-2011 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2007 David Faure <faure@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "draganddrophelper.h"
#include "jobs/copyprogressdialog.h"
#include "jobs/fileoperations.h"

#include <KIO/DropJob>
#include <KConfigGroup>
#include <KJobWidgets>
#include <KSharedConfig>
#include <KUrlMimeData>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QStorageInfo>

#include <memory>

QHash<QUrl, bool> DragAndDropHelper::m_urlListMatchesUrlCache;

bool DragAndDropHelper::urlListMatchesUrl(const QList<QUrl> &urls, const QUrl &destUrl)
{
    auto iteratorResult = m_urlListMatchesUrlCache.constFind(destUrl);
    if (iteratorResult != m_urlListMatchesUrlCache.constEnd()) {
        return *iteratorResult;
    }

    const bool destUrlMatches = std::find_if(urls.constBegin(),
                                             urls.constEnd(),
                                             [destUrl](const QUrl &url) {
                                                 return url.matches(destUrl, QUrl::StripTrailingSlash);
                                             })
        != urls.constEnd();

    return *m_urlListMatchesUrlCache.insert(destUrl, destUrlMatches);
}

namespace
{
bool isDropOntoOtherDevice(const QDropEvent *event, const QUrl &destUrl)
{
    if (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier)) {
        return false;
    }
    const KConfigGroup kdeGroup(KSharedConfig::openConfig(), QStringLiteral("KDE"));
    if (kdeGroup.readEntry("DndBehavior") != QLatin1String("MoveIfSameDevice")) {
        return false;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty() || !destUrl.isLocalFile() || !QFileInfo(destUrl.toLocalFile()).isDir()) {
        return false;
    }
    const QByteArray destDevice = QStorageInfo(destUrl.toLocalFile()).device();
    return std::all_of(urls.cbegin(), urls.cend(), [&destDevice](const QUrl &url) {
        return url.isLocalFile() && QStorageInfo(url.toLocalFile()).device() != destDevice;
    });
}
}

/**
 * shellkit fork: files dropped onto a folder on this computer are copied, moved or linked by
 * the file operations service, with Windows' rules: Ctrl copies, Shift moves, Ctrl+Shift or Alt
 * links, and a plain drop moves on the same device and copies onto another one. Anything else
 * (another drop behaviour chosen in System Settings, remote places, drops onto programs, a
 * move within the same folder) is left to KIO.
 */
static std::optional<FileOps::Type> serviceDropType(const QDropEvent *event, const QUrl &destUrl, const QList<QUrl> &urls)
{
    const KConfigGroup kdeGroup(KSharedConfig::openConfig(), QStringLiteral("KDE"));
    if (kdeGroup.readEntry("DndBehavior") != QLatin1String("MoveIfSameDevice")) {
        return std::nullopt;
    }
    if (urls.isEmpty() || !destUrl.isLocalFile()) {
        return std::nullopt;
    }
    const QFileInfo dest(destUrl.toLocalFile());
    if (!dest.isDir()) {
        return std::nullopt;
    }
    const QUrl destDir = destUrl.adjusted(QUrl::StripTrailingSlash);
    bool allInDest = true;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            return std::nullopt;
        }
        if (!url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).matches(destDir, QUrl::StripTrailingSlash)) {
            allInDest = false;
        }
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool control = modifiers & Qt::ControlModifier;
    const bool shift = modifiers & Qt::ShiftModifier;
    FileOps::Type type;
    Qt::DropAction action;
    if ((control && shift) || (modifiers & Qt::AltModifier)) {
        type = FileOps::Type::Link;
        action = Qt::LinkAction;
    } else if (control) {
        type = FileOps::Type::Copy;
        action = Qt::CopyAction;
    } else if (shift) {
        type = FileOps::Type::Move;
        action = Qt::MoveAction;
    } else {
        const QByteArray destDevice = QStorageInfo(dest.absoluteFilePath()).device();
        int sameDevice = 0;
        for (const QUrl &url : urls) {
            if (QStorageInfo(url.toLocalFile()).device() == destDevice) {
                ++sameDevice;
            }
        }
        if (sameDevice == urls.size()) {
            type = FileOps::Type::Move;
            action = Qt::MoveAction;
        } else if (sameDevice == 0) {
            type = FileOps::Type::Copy;
            action = Qt::CopyAction;
        } else {
            return std::nullopt;
        }
    }
    if (type == FileOps::Type::Move && allInDest) {
        return std::nullopt;
    }
    if (!(event->possibleActions() & action)) {
        return std::nullopt;
    }
    return type;
}

KIO::DropJob *
DragAndDropHelper::dropUrls(const QUrl &destUrl, QDropEvent *event, QWidget *window, KIO::DropJobFlags dropjobFlags, FileOperation **operation)
{
    const QMimeData *mimeData = event->mimeData();
    if (isArkDndMimeType(mimeData)) {
        const QString remoteDBusClient = QString::fromUtf8(mimeData->data(arkDndServiceMimeType()));
        const QString remoteDBusPath = QString::fromUtf8(mimeData->data(arkDndPathMimeType()));

        QDBusMessage message = QDBusMessage::createMethodCall(remoteDBusClient,
                                                              remoteDBusPath,
                                                              QStringLiteral("org.kde.ark.DndExtract"),
                                                              QStringLiteral("extractSelectedFilesTo"));
        message.setArguments({destUrl.toDisplayString(QUrl::PreferLocalFile)});
        QDBusConnection::sessionBus().call(message);
    } else {
        if (urlListMatchesUrl(event->mimeData()->urls(), destUrl)) {
            return nullptr;
        }

        const QList<QUrl> urls = KUrlMimeData::urlsFromMimeData(mimeData, KUrlMimeData::PreferLocalUrls);
        if (const std::optional<FileOps::Type> type = serviceDropType(event, destUrl, urls)) {
            FileOperation *started = FileOperations::self()->start(*type, urls, destUrl, window ? window->window() : nullptr);
            if (operation) {
                *operation = started;
            }
            return nullptr;
        }

        // shellkit fork: as in Windows, a plain drop onto another device copies without asking
        // (KIO's "MoveIfSameDevice" only covers the same device and asks otherwise).
        std::unique_ptr<QDropEvent> copyEvent;
        if (isDropOntoOtherDevice(event, destUrl)) {
            copyEvent = std::make_unique<QDropEvent>(event->position(),
                                                     event->possibleActions(),
                                                     mimeData,
                                                     event->buttons(),
                                                     event->modifiers() | Qt::ControlModifier);
        }

        // Drop into a directory or a desktop-file
        // shellkit fork: progress in the Windows 11 copy dialog, not in a notification.
        KIO::DropJob *job = KIO::drop(copyEvent ? copyEvent.get() : event, destUrl, dropjobFlags, KIO::HideProgressInfo);
        KJobWidgets::setWindow(job, window);
        QObject::connect(job, &KIO::DropJob::copyJobStarted, job, [window](KIO::CopyJob *copyJob) {
            CopyProgressDialog::track(copyJob, window ? window->window() : nullptr);
        });
        return job;
    }

    return nullptr;
}

bool DragAndDropHelper::supportsDropping(const KFileItem &destItem)
{
    return (destItem.isDir() && destItem.isWritable()) || destItem.isDesktopFile() || (destItem.isFile() && destItem.isLocalFile() && destItem.isExecutable());
}

void DragAndDropHelper::updateDropAction(QDropEvent *event, const QUrl &destUrl)
{
    if (urlListMatchesUrl(event->mimeData()->urls(), destUrl)) {
        event->setDropAction(Qt::IgnoreAction);
        event->ignore();
    }
    KFileItem item(destUrl);
    if (!item.isLocalFile() || supportsDropping(item)) {
        event->setDropAction(event->proposedAction());
        event->accept();
    } else {
        event->setDropAction(Qt::IgnoreAction);
        event->ignore();
    }
}

void DragAndDropHelper::clearUrlListMatchesUrlCache()
{
    DragAndDropHelper::m_urlListMatchesUrlCache.clear();
}

bool DragAndDropHelper::isArkDndMimeType(const QMimeData *mimeData)
{
    return mimeData->hasFormat(arkDndServiceMimeType()) && mimeData->hasFormat(arkDndPathMimeType());
}
