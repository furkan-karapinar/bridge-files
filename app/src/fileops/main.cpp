/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// shellkit fork: shellkit-fileops, the service that runs the File Explorer's copy, move and
// delete operations, so they outlive its windows. Started by D-Bus on the first request.

#include "dolphin_version.h"
#include "fileopsservice.h"
#include "jobs/fileoperations.h"
#include "shellkit.h"

#include <KAboutData>
#include <KCrash>
#include <KIconTheme>
#include <KLocalizedString>

#define HAVE_STYLE_MANAGER __has_include(<KStyleManager>)
#if HAVE_STYLE_MANAGER
#include <KStyleManager>
#endif

#include <QApplication>
#include <QDBusConnection>
#include <QStyleFactory>

#include <iostream>

int main(int argc, char **argv)
{
    KIconTheme::initTheme();

    QApplication app(argc, argv);
    // Windows come and go with the operations; the service decides itself when to quit.
    app.setQuitOnLastWindowClosed(false);
    // KIO jobs hold a QEventLoopLocker; without this the end of the last job quits the service
    // and the undo it keeps for the File Explorer is lost.
    QCoreApplication::setQuitLockEnabled(false);

#if HAVE_STYLE_MANAGER
    KStyleManager::initStyle();
#endif
    // The same widget style as the File Explorer, so its dialogs look the same.
    if (QStyle *style = QStyleFactory::create(shellkitWin7() ? QStringLiteral("shellkit-win7") : QStringLiteral("shellkit-win11"))) {
        QApplication::setStyle(style);
    }

    KLocalizedString::setApplicationDomain("dolphin");
    KLocalizedString::addDomainLocaleDir(shellkitDomain, QCoreApplication::applicationDirPath() + QStringLiteral("/../share/locale"));

    KAboutData aboutData(QStringLiteral("shellkit-fileops"),
                         i18n("File Explorer"),
                         QStringLiteral(DOLPHIN_VERSION_STRING),
                         i18ndc(shellkitDomain, "@title", "File operations of the File Explorer"),
                         KAboutLicense::GPL);
    aboutData.setOrganizationDomain("shellkit.org");
    // Its windows belong to the File Explorer on the taskbar.
    aboutData.setDesktopFileName(QStringLiteral("org.shellkit.files"));
    KAboutData::setApplicationData(aboutData);
    KCrash::initialize();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        std::cerr << "shellkit-fileops: no D-Bus session bus\n";
        return EXIT_FAILURE;
    }
    auto service = new FileOpsService(&app);
    if (!bus.registerObject(QString::fromLatin1(FileOps::objectPath), service, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        std::cerr << "shellkit-fileops: cannot register the object\n";
        return EXIT_FAILURE;
    }
    if (!bus.registerService(QString::fromLatin1(FileOps::serviceName))) {
        std::cerr << "shellkit-fileops: already running\n";
        return EXIT_FAILURE;
    }
    return app.exec();
}
