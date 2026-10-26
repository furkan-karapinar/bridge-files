/*
    SPDX-FileCopyrightText: 2026 shellkit

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SHELLKIT_H
#define SHELLKIT_H

/**
 * shellkit fork: strings added by the fork live in their own catalogue (po/), so the
 * system "dolphin" catalogue keeps translating everything inherited from upstream.
 * Use i18ndc(shellkitDomain, ...) for new strings.
 */
inline constexpr const char shellkitDomain[] = "shellkit-files";

#include <KConfigGroup>
#include <KFileItem>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMimeType>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>

/**
 * Text of the Type column, as in Windows: folders are "File folder", text files "Text Document",
 * pictures, sound, video and unknown files "<EXT> File", everything else its MIME comment.
 */
inline QString shellkitTypeName(const KFileItem &item)
{
    if (item.isDir()) {
        return i18ndc(shellkitDomain, "@item:intable Type of a folder", "File folder");
    }
    const QMimeType mimeType = item.currentMimeType();
    const QString name = mimeType.name();
    if (name == QLatin1String("text/plain")) {
        return i18ndc(shellkitDomain, "@item:intable File type", "Text Document");
    }
    if (name == QLatin1String("application/zip")) {
        return i18ndc(shellkitDomain, "@item:intable File type", "Compressed (zipped) Folder");
    }
    const bool media = name.startsWith(QLatin1String("image/")) || name.startsWith(QLatin1String("audio/")) || name.startsWith(QLatin1String("video/"));
    if (media || mimeType.isDefault() || item.mimeComment().isEmpty()) {
        const QString suffix = QFileInfo(item.name()).suffix().toUpper();
        if (suffix.isEmpty()) {
            return i18ndc(shellkitDomain, "@item:intable Type of a file without extension", "File");
        }
        return i18ndc(shellkitDomain, "@item:intable %1 is a file extension such as PNG", "%1 File", suffix);
    }
    return item.mimeComment();
}

/** Size in the status bar, as in Windows: "8 bytes", "1.46 KB", "14.6 MB", "146 MB". */
inline QString shellkitStatusSize(qulonglong bytes)
{
    if (bytes < 1024) {
        return i18ndcp(shellkitDomain, "@info:status", "%1 byte", "%1 bytes", bytes);
    }
    static const char *const units[] = {"KB", "MB", "GB", "TB"};
    double value = bytes / 1024.0;
    int unit = 0;
    while (value >= 1000 && unit < 3) {
        value /= 1024;
        ++unit;
    }
    const int decimals = value < 10 ? 2 : (value < 100 ? 1 : 0);
    // Windows truncates instead of rounding.
    const double factor = std::pow(10, decimals);
    return QLocale().toString(std::floor(value * factor) / factor, 'f', decimals) + QLatin1Char(' ') + QLatin1String(units[unit]);
}

/** Size column of a file, as in Windows: whole kilobytes, rounded up ("0 KB", "1 KB", "2.442 KB"). */
inline QString shellkitFileSize(qulonglong bytes)
{
    return i18ndc(shellkitDomain, "@item:intable File size in kilobytes", "%1 KB", QLocale().toString((bytes + 1023) / 1024));
}

/**
 * The Home page has no folder behind it. Its URL carries the title that KUrlNavigator and
 * DolphinViewContainer::caption() show for a URL without a path.
 */
inline QUrl shellkitHomeUrl()
{
    QUrl url(QStringLiteral("shellkit-home:/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("title"), i18ndc(shellkitDomain, "@item:inlistbox Start page of the file explorer", "Home"));
    url.setQuery(query);
    return url;
}

inline bool isShellkitHomeUrl(const QUrl &url)
{
    return url.scheme() == QLatin1String("shellkit-home");
}

/** Files the user added to favorites, shown on the Home page. */
inline QList<QUrl> shellkitFavorites()
{
    const KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("Home"));
    return QUrl::fromStringList(group.readEntry("Favorites", QStringList()));
}

inline void shellkitSetFavorite(const QUrl &url, bool favorite)
{
    QList<QUrl> favorites = shellkitFavorites();
    favorites.removeAll(url);
    if (favorite) {
        favorites.prepend(url);
    }
    KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("Home"));
    group.writeEntry("Favorites", QUrl::toStringList(favorites));
    group.sync();
}

/**
 * The shellkit window frame (plasma/kdecoration) drops its title bar for windows whose caption
 * carries this invisible character; the main window then draws the caption buttons in its tab row.
 */
inline constexpr QChar shellkitCaptionMarker(0x2060);

/** True when KWin uses the shellkit window frame, so the tab row can stand in for its title bar. */
inline bool shellkitIntegratedCaption()
{
    static const bool integrated = [] {
        const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kwinrc"), KConfig::NoGlobals), QStringLiteral("org.kde.kdecoration2"));
        return group.readEntry("library", QString()) == QLatin1String("org.shellkit.win11");
    }();
    return integrated;
}

/**
 * True when the window looks like the Windows 7 Windows Explorer instead of the Windows 11 File
 * Explorer (GeneralSettings::win7Look). Read once: the widget style and the toolbars are chosen
 * at start, so a change takes effect the next time the app starts.
 */
inline bool shellkitWin7()
{
    // By name: the style is picked before KAboutData names the app.
    static const bool win7 = KConfigGroup(KSharedConfig::openConfig(QStringLiteral("shellkit-filesrc")), QStringLiteral("General")).readEntry("Win7Look", false);
    return win7;
}

/** Where a window or tab starts: the Home page, or the home folder in the Windows 7 look. */
inline QUrl shellkitStartUrl()
{
    return shellkitWin7() ? QUrl::fromLocalFile(QDir::homePath()) : shellkitHomeUrl();
}

/** "This PC" (Windows 7: "Computer"): the drives, as tiles with their free space. */
inline QUrl shellkitThisPcUrl()
{
    QUrl url(QStringLiteral("shellkit-thispc:/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("title"),
                       shellkitWin7() ? i18ndc(shellkitDomain, "@item:inlistbox", "Computer") : i18ndc(shellkitDomain, "@item:inlistbox", "This PC"));
    url.setQuery(query);
    return url;
}

/** "Network": the network adapters, the gateway and the devices found on the network. */
inline QUrl shellkitNetworkUrl()
{
    QUrl url(QStringLiteral("shellkit-network:/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("title"), i18ndc(shellkitDomain, "@item:inlistbox", "Network"));
    url.setQuery(query);
    return url;
}

/** The Home, This PC and Network pages, which DolphinViewContainer shows in place of the view. */
inline bool isShellkitPageUrl(const QUrl &url)
{
    return isShellkitHomeUrl(url) || url.scheme() == QLatin1String("shellkit-thispc") || url.scheme() == QLatin1String("shellkit-network");
}

/** "Copy as path": every path in quotes, one per line, as Windows does. */
inline QString shellkitPathText(const QList<QUrl> &urls)
{
    QStringList lines;
    for (const QUrl &url : urls) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toDisplayString();
        lines.append(QLatin1Char('"') + path + QLatin1Char('"'));
    }
    return lines.join(QLatin1Char('\n'));
}

#endif // SHELLKIT_H
