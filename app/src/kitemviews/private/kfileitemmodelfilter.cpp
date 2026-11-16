/*
 * SPDX-FileCopyrightText: 2011 Janardhan Reddy <annapareddyjanardhanreddy@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kfileitemmodelfilter.h"

#include <QDate>
#include <QLocale>
#include <QMimeType>
#include <QRegularExpression>

#include <algorithm>

#include <KFileItem>

KFileItemModelFilter::KFileItemModelFilter()
    : m_filterMode(Glob)
    , m_caseSensitive(false)
    , m_regExp(nullptr)
    , m_lowerCasePattern()
    , m_pattern()
{
}

KFileItemModelFilter::~KFileItemModelFilter()
{
    delete m_regExp;
    m_regExp = nullptr;
}

void KFileItemModelFilter::setPattern(const QString &filter)
{
    m_pattern = filter;
    m_lowerCasePattern = filter.toLower();

    updateFilter();
}

void KFileItemModelFilter::setFilterMode(FilterMode mode)
{
    m_filterMode = mode;
    updateFilter();
}

KFileItemModelFilter::FilterMode KFileItemModelFilter::filterMode() const
{
    return m_filterMode;
}

void KFileItemModelFilter::setCaseSensitive(bool caseSensitive)
{
    m_caseSensitive = caseSensitive;
    updateFilter();
}

bool KFileItemModelFilter::isCaseSensitive() const
{
    return m_caseSensitive;
}

QString KFileItemModelFilter::pattern() const
{
    return m_pattern;
}

void KFileItemModelFilter::updateFilter()
{
    if (m_filterMode == PlainText) {
        return;
    }

    if (!m_regExp) {
        m_regExp = new QRegularExpression();
    }

    QRegularExpression::PatternOptions options = m_caseSensitive ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption;
    if (m_filterMode == Regex) {
        m_regExp->setPattern(m_pattern);
        m_regExp->setPatternOptions(options);
    } else if (m_filterMode == Glob) {
        m_regExp->setPattern(QRegularExpression::wildcardToRegularExpression(m_pattern, QRegularExpression::UnanchoredWildcardConversion));
        m_regExp->setPatternOptions(options);
    }
}

void KFileItemModelFilter::setMimeTypes(const QStringList &types)
{
    m_mimeTypes = types;
}

QStringList KFileItemModelFilter::mimeTypes() const
{
    return m_mimeTypes;
}

void KFileItemModelFilter::setExcludeMimeTypes(const QStringList &types)
{
    m_excludeMimeTypes = types;
}

QStringList KFileItemModelFilter::excludeMimeTypes() const
{
    return m_excludeMimeTypes;
}

bool KFileItemModelFilter::hasSetFilters() const
{
    return (!m_pattern.isEmpty() || !m_mimeTypes.isEmpty() || !m_excludeMimeTypes.isEmpty() || !m_searchOptions.isEmpty());
}

bool KFileItemModelFilter::matches(const KFileItem &item) const
{
    if (!matchesSearchOptions(item)) {
        return false;
    }

    const bool hasPatternFilter = !m_pattern.isEmpty();
    const bool hasMimeTypesFilter = !m_mimeTypes.isEmpty() || !m_excludeMimeTypes.isEmpty();

    // If no filter is set, return true.
    if (!hasPatternFilter && !hasMimeTypesFilter) {
        return true;
    }

    // If both filters are set, return true when both filters are matched
    if (hasPatternFilter && hasMimeTypesFilter) {
        return (matchesPattern(item) && matchesType(item));
    }

    // If only one filter is set, return true when that filter is matched
    if (hasPatternFilter) {
        return matchesPattern(item);
    }

    return matchesType(item);
}

bool KFileItemModelFilter::matchesPattern(const KFileItem &item) const
{
    if (m_filterMode == Glob || m_filterMode == Regex) {
        return m_regExp->isValid() && m_regExp->match(item.text()).hasMatch();
    } else if (m_caseSensitive) {
        return item.text().contains(m_pattern);
    } else {
        return item.text().toLower().contains(m_lowerCasePattern);
    }
}

bool KFileItemModelFilter::matchesType(const KFileItem &item) const
{
    bool excluded = std::any_of(m_excludeMimeTypes.constBegin(), m_excludeMimeTypes.constEnd(), [item](const QString &excludeMimetype) {
        // Currently only really used for "temporary" files which are all idenfied by glob.
        // If you use the filter for anything else, this must be revisited.
        return item.currentMimeType().name() == excludeMimetype;
    });
    if (excluded) {
        return false;
    }

    bool included = std::any_of(m_mimeTypes.constBegin(), m_mimeTypes.constEnd(), [item](const QString &mimeType) {
        return item.mimetype() == mimeType;
    });
    if (included) {
        return true;
    }

    return m_mimeTypes.isEmpty();
}

void KFileItemModelFilter::setSearchOptions(const SearchOptions &options)
{
    m_searchOptions = options;
}

KFileItemModelFilter::SearchOptions KFileItemModelFilter::searchOptions() const
{
    return m_searchOptions;
}

namespace
{
using SearchOptions = KFileItemModelFilter::SearchOptions;

bool isMimeType(const QMimeType &type, std::initializer_list<const char *> names)
{
    return std::any_of(names.begin(), names.end(), [&type](const char *name) {
        return type.inherits(QLatin1String(name));
    });
}

bool startsWithAny(const QString &name, std::initializer_list<const char *> prefixes)
{
    return std::any_of(prefixes.begin(), prefixes.end(), [&name](const char *prefix) {
        return name.startsWith(QLatin1String(prefix));
    });
}

bool isPlaylist(const QMimeType &type)
{
    return isMimeType(type, {"audio/x-mpegurl", "audio/mpegurl", "application/vnd.apple.mpegurl", "audio/x-scpls", "application/xspf+xml", "audio/x-ms-asx"});
}

bool matchesKind(const KFileItem &item, SearchOptions::Kind kind)
{
    if (kind == SearchOptions::Folder) {
        return item.isDir();
    }
    if (kind == SearchOptions::Link) {
        return item.isLink() || item.mimetype() == QLatin1String("application/x-mswinurl");
    }
    if (item.isDir()) {
        return false;
    }
    // The extension alone can mislead ("prog.bin" is a program): look at the contents too.
    KFileItem detected(item);
    const QMimeType type = detected.determineMimeType();
    const QString name = type.name();
    switch (kind) {
    case SearchOptions::Calendar:
        return isMimeType(type, {"text/calendar"});
    case SearchOptions::Contact:
        return isMimeType(type, {"text/vcard", "text/x-vcard", "text/directory"});
    case SearchOptions::Document:
        // Calendars, contacts, mail, playlists and scripts are text too, but have kinds of their own.
        for (const auto other : {SearchOptions::Calendar, SearchOptions::Contact, SearchOptions::Email, SearchOptions::Playlist, SearchOptions::Program}) {
            if (matchesKind(item, other)) {
                return false;
            }
        }
        return startsWithAny(name, {"application/vnd.oasis.opendocument.", "application/vnd.openxmlformats-officedocument.", "application/vnd.ms-"})
            || isMimeType(type, {"text/plain", "application/pdf", "application/msword", "application/rtf", "application/epub+zip", "application/x-abiword"});
    case SearchOptions::Email:
        return isMimeType(type, {"message/rfc822", "application/mbox", "application/vnd.ms-outlook"});
    case SearchOptions::Playlist:
        return isPlaylist(type);
    case SearchOptions::Music:
        return name.startsWith(QLatin1String("audio/")) && !isPlaylist(type);
    case SearchOptions::Picture:
        return name.startsWith(QLatin1String("image/"));
    case SearchOptions::Video:
        return name.startsWith(QLatin1String("video/"));
    case SearchOptions::Program:
        return isMimeType(type,
                          {"application/x-executable", "application/x-pie-executable", "application/x-shellscript", "application/x-desktop",
                           "application/vnd.appimage", "application/vnd.microsoft.portable-executable", "application/x-ms-dos-executable",
                           "application/x-msdownload", "application/x-msi"});
    default:
        return true;
    }
}

bool matchesDate(const KFileItem &item, SearchOptions::Date date)
{
    const QDate modified = item.time(KFileItem::ModificationTime).toLocalTime().date();
    if (!modified.isValid()) {
        return false;
    }
    const QDate today = QDate::currentDate();
    // Weeks start on the locale's first day, as in Windows.
    const int fromWeekStart = (today.dayOfWeek() - int(QLocale().firstDayOfWeek()) + 7) % 7;
    const QDate weekStart = today.addDays(-fromWeekStart);
    const QDate monthStart(today.year(), today.month(), 1);
    const QDate yearStart(today.year(), 1, 1);
    const auto between = [&modified](const QDate &from, const QDate &before) {
        return modified >= from && modified < before;
    };
    switch (date) {
    case SearchOptions::Today:
        return modified == today;
    case SearchOptions::Yesterday:
        return modified == today.addDays(-1);
    case SearchOptions::ThisWeek:
        return between(weekStart, today.addDays(1));
    case SearchOptions::LastWeek:
        return between(weekStart.addDays(-7), weekStart);
    case SearchOptions::ThisMonth:
        return between(monthStart, today.addDays(1));
    case SearchOptions::LastMonth:
        return between(monthStart.addMonths(-1), monthStart);
    case SearchOptions::ThisYear:
        return between(yearStart, today.addDays(1));
    case SearchOptions::LastYear:
        return between(yearStart.addYears(-1), yearStart);
    default:
        return true;
    }
}

bool matchesSize(const KFileItem &item, SearchOptions::Size size)
{
    // Folders have no size here, as in Windows.
    if (item.isDir()) {
        return false;
    }
    constexpr KIO::filesize_t KB = 1024;
    constexpr KIO::filesize_t MB = 1024 * KB;
    constexpr KIO::filesize_t GB = 1024 * MB;
    const KIO::filesize_t bytes = item.size();
    switch (size) {
    case SearchOptions::Empty:
        return bytes == 0;
    case SearchOptions::Tiny:
        return bytes > 0 && bytes < 16 * KB;
    case SearchOptions::Small:
        return bytes >= 16 * KB && bytes < MB;
    case SearchOptions::Medium:
        return bytes >= MB && bytes < 128 * MB;
    case SearchOptions::Large:
        return bytes >= 128 * MB && bytes < GB;
    case SearchOptions::Huge:
        return bytes >= GB && bytes < 4 * GB;
    case SearchOptions::Gigantic:
        return bytes >= 4 * GB;
    default:
        return true;
    }
}
}

bool KFileItemModelFilter::matchesSearchOptions(const KFileItem &item) const
{
    const SearchOptions &options = m_searchOptions;
    return (options.date == SearchOptions::AnyDate || matchesDate(item, options.date))
        && (options.kind == SearchOptions::AnyKind || matchesKind(item, options.kind))
        && (options.size == SearchOptions::AnySize || matchesSize(item, options.size));
}
