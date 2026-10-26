/*
 * SPDX-FileCopyrightText: 2007-2011 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2007 David Faure <faure@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "draganddrophelper.h"
#include "jobs/copyprogressdialog.h"

#include <KIO/DropJob>
#include <KConfigGroup>
#include <KJobWidgets>
#include <KSharedConfig>

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

KIO::DropJob *DragAndDropHelper::dropUrls(const QUrl &destUrl, QDropEvent *event, QWidget *window, KIO::DropJobFlags dropjobFlags)
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
