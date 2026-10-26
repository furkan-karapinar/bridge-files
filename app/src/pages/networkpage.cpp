/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "networkpage.h"

#include "global.h"
#include "pages/tilesection.h"
#include "shellkit.h"

#include <KIO/CommandLauncherJob>
#include <KIO/JobUiDelegateFactory>
#include <KLocalizedString>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QEvent>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QLabel>
#include <QMenu>
#include <QNetworkInterface>
#include <QProcess>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QtEndian>

namespace
{
constexpr int PageMargin = 20;
constexpr int TileIconSize = 48;
/** avahi-browse --terminate ends once the cache is read; a slow network gets this long. */
constexpr int DiscoveryTimeoutMs = 10000;

QIcon colourIcon(const QIcon &icon)
{
    return icon.isNull() ? icon : QIcon(icon.pixmap(QSize(TileIconSize, TileIconSize)));
}

QIcon themeIcon(std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        const QIcon icon = QIcon::fromTheme(QLatin1String(name));
        if (!icon.isNull()) {
            return colourIcon(icon);
        }
    }
    return QIcon();
}

/** avahi-browse --parsable writes "\DDD" (a decimal byte of the UTF-8 name) for special characters. */
QString unescapeAvahi(const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    QByteArray result;
    for (int i = 0; i < bytes.size(); ++i) {
        bool decimal = false;
        const int code = bytes.mid(i + 1, 3).toInt(&decimal);
        if (bytes[i] == '\\' && i + 3 < bytes.size() && decimal) {
            result.append(char(code));
            i += 3;
        } else if (bytes[i] == '\\' && i + 1 < bytes.size()) {
            result.append(bytes[++i]);
        } else {
            result.append(bytes[i]);
        }
    }
    return QString::fromUtf8(result);
}

bool hasAny(const QStringList &types, std::initializer_list<const char *> wanted)
{
    for (const char *type : wanted) {
        if (types.contains(QLatin1String(type))) {
            return true;
        }
    }
    return false;
}

QStandardItem *newTile(const QIcon &icon, const QString &name, const QStringList &lines, const QUrl &url, const QString &key)
{
    auto tile = new QStandardItem(icon, name);
    tile->setData(lines, TileRoles::LinesRole);
    tile->setData(url, TileRoles::UrlRole);
    tile->setData(key, TileRoles::KeyRole);
    tile->setToolTip(lines.join(QLatin1Char('\n')));
    return tile;
}

bool isWebUrl(const QUrl &url)
{
    return url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https");
}
}

NetworkPage::NetworkPage(QWidget *parent)
    : ShellkitPage(parent)
    , m_discoveryTimeout(new QTimer(this))
{
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setBackgroundRole(QPalette::Base);
    viewport()->setAutoFillBackground(true);
    viewport()->installEventFilter(this);

    auto content = new QWidget(this);
    content->setBackgroundRole(QPalette::Base);
    auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(PageMargin, 12, PageMargin, PageMargin);
    layout->setSpacing(4);

    // The groups of the Windows Network folder, after this computer's own adapters.
    m_adapters = new TileSection(i18ndc(shellkitDomain, "@title:group", "Network adapters"), content);
    m_infrastructure = new TileSection(i18ndc(shellkitDomain, "@title:group", "Network infrastructure"), content);
    m_computers = new TileSection(i18ndc(shellkitDomain, "@title:group", "Computers"), content);
    m_media = new TileSection(i18ndc(shellkitDomain, "@title:group", "Media devices"), content);
    m_printers = new TileSection(i18ndc(shellkitDomain, "@title:group", "Printers"), content);
    m_others = new TileSection(i18ndc(shellkitDomain, "@title:group", "Other devices"), content);
    for (TileSection *section : sections()) {
        connectSection(section);
        layout->addWidget(section);
    }

    m_status = new QLabel(content);
    m_status->setWordWrap(true);
    m_status->setContentsMargins(8, 12, 8, 12);
    QPalette palette = m_status->palette();
    QColor secondary = palette.color(QPalette::Text);
    secondary.setAlphaF(0.75);
    palette.setColor(QPalette::WindowText, secondary);
    m_status->setPalette(palette);
    layout->addWidget(m_status);
    layout->addStretch();
    setWidget(content);

    m_discoveryTimeout->setSingleShot(true);
    m_discoveryTimeout->setInterval(DiscoveryTimeoutMs);
    connect(m_discoveryTimeout, &QTimer::timeout, this, [this]() {
        if (m_discovery) {
            m_discovery->kill();
        }
    });
}

NetworkPage::~NetworkPage()
{
    if (m_discovery) {
        m_discovery->disconnect(this);
        m_discovery->kill();
        m_discovery->waitForFinished(1000);
    }
}

void NetworkPage::refresh()
{
    fillAdapters();
    fillGateway();
    fillDevices();
    startDiscovery();
}

int NetworkPage::itemCount() const
{
    int count = 0;
    for (TileSection *section : sections()) {
        count += section->model()->rowCount();
    }
    return count;
}

bool NetworkPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == viewport() && event->type() == QEvent::Resize) {
        updateHeights();
    }
    return ShellkitPage::eventFilter(watched, event);
}

QList<TileSection *> NetworkPage::sections() const
{
    return {m_adapters, m_infrastructure, m_computers, m_media, m_printers, m_others};
}

void NetworkPage::fillAdapters()
{
    QStandardItemModel *model = m_adapters->model();
    model->clear();
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        if (interface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        QString ipv4;
        for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                ipv4 = entry.ip().toString();
                break;
            }
        }
        const bool up = interface.flags().testFlag(QNetworkInterface::IsRunning);
        QStringList lines;
        if (!up) {
            lines.append(i18ndc(shellkitDomain, "@info State of a network adapter", "Not connected"));
        } else if (ipv4.isEmpty()) {
            lines.append(i18ndc(shellkitDomain, "@info State of a network adapter", "Connected"));
        } else {
            lines.append(i18ndc(shellkitDomain, "@info %1 is an IP address", "Connected, %1", ipv4));
        }
        if (!interface.hardwareAddress().isEmpty()) {
            lines.append(interface.hardwareAddress());
        }
        QIcon icon;
        switch (interface.type()) {
        case QNetworkInterface::Wifi:
        case QNetworkInterface::Ieee80216:
            icon = themeIcon({"network-wireless", "network-wired"});
            break;
        case QNetworkInterface::Virtual:
            icon = themeIcon({"network-bridge", "network-wired"});
            break;
        default:
            icon = themeIcon({"network-wired", "network-card"});
            break;
        }
        model->appendRow(newTile(icon, interface.humanReadableName(), lines, QUrl(), ipv4));
    }
    updateHeights();
}

void NetworkPage::fillGateway()
{
    // The default route of /proc/net/route: destination 0, the gateway in network byte order.
    QStandardItemModel *model = m_infrastructure->model();
    model->clear();
    m_gateway.clear();
    QFile routes(QStringLiteral("/proc/net/route"));
    if (routes.open(QIODevice::ReadOnly)) {
        routes.readLine(); // the column titles
        while (!routes.atEnd()) {
            const QStringList fields = QString::fromLatin1(routes.readLine()).split(QLatin1Char('\t'), Qt::SkipEmptyParts);
            if (fields.size() < 3 || fields[1] != QLatin1String("00000000")) {
                continue;
            }
            bool ok = false;
            const quint32 raw = fields[2].toUInt(&ok, 16);
            if (!ok || raw == 0) {
                continue;
            }
            m_gateway = QHostAddress(qFromBigEndian(raw)).toString();
            QStandardItem *tile = newTile(themeIcon({"network-wireless-hotspot", "modem", "network-server"}),
                                          i18ndc(shellkitDomain, "@item The default gateway of the network", "Router"),
                                          {m_gateway, i18ndc(shellkitDomain, "@info %1 is the name of a network adapter", "Gateway of %1", fields[0])},
                                          QUrl(QStringLiteral("http://%1/").arg(m_gateway)),
                                          m_gateway);
            model->appendRow(tile);
            break;
        }
    }
    updateHeights();
}

void NetworkPage::startDiscovery()
{
    if (m_discovery) {
        return;
    }
    const QString avahi = QStandardPaths::findExecutable(QStringLiteral("avahi-browse"));
    m_avahiMissing = avahi.isEmpty();
    if (m_avahiMissing) {
        updateStatus();
        return;
    }
    m_discoveryOutput.clear();
    m_discovery = new QProcess(this);
    connect(m_discovery, &QProcess::readyReadStandardOutput, this, [this]() {
        m_discoveryOutput += QString::fromUtf8(m_discovery->readAllStandardOutput());
    });
    connect(m_discovery, &QProcess::finished, this, &NetworkPage::discoveryFinished);
    connect(m_discovery, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            discoveryFinished();
        }
    });
    m_discovery->start(avahi, {QStringLiteral("--all"), QStringLiteral("--resolve"), QStringLiteral("--parsable"), QStringLiteral("--terminate")});
    m_discoveryTimeout->start();
    updateStatus();
}

void NetworkPage::discoveryFinished()
{
    if (!m_discovery) {
        return;
    }
    m_discoveryTimeout->stop();
    m_discoveryOutput += QString::fromUtf8(m_discovery->readAllStandardOutput());
    m_discovery->deleteLater();
    m_discovery = nullptr;

    // Resolved services: =;interface;protocol;name;type;domain;host name;address;port;txt
    QMap<QString, Host> hosts;
    const QStringList lines = m_discoveryOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char(';'));
        if (fields.size() < 9 || fields[0] != QLatin1String("=")) {
            continue;
        }
        const QString hostName = fields[6];
        Host &host = hosts[hostName.isEmpty() ? fields[7] : hostName];
        host.hostName = hostName;
        const QString type = fields[4];
        if (!host.types.contains(type)) {
            host.types.append(type);
            host.ports.insert(type, fields[8].toInt());
        }
        // A printer's or a TV's service name says more than its host name.
        if (host.name.isEmpty() || hasAny({type}, {"_ipp._tcp", "_ipps._tcp", "_printer._tcp", "_airplay._tcp", "_googlecast._tcp", "_raop._tcp"})) {
            QString name = unescapeAvahi(fields[3]);
            if (type == QLatin1String("_raop._tcp")) {
                name = name.section(QLatin1Char('@'), 1); // "MAC@Name"
            }
            if (!name.isEmpty()) {
                host.name = name;
            }
        }
        if (host.address.isEmpty() || (fields[2] == QLatin1String("IPv4") && host.address.contains(QLatin1Char(':')))) {
            host.address = fields[7];
        }
    }
    m_hosts = hosts;
    fillDevices();
    Q_EMIT itemCountChanged();
}

void NetworkPage::fillDevices()
{
    for (TileSection *section : {m_computers, m_media, m_printers, m_others}) {
        section->model()->clear();
    }

    QStringList addresses;
    for (const Host &host : std::as_const(m_hosts)) {
        addresses.append(host.address);
        const QStringList &types = host.types;
        const QString shortName = host.hostName.section(QLatin1Char('.'), 0, 0);
        const QUrl web = hasAny(types, {"_https._tcp"})
            ? QUrl(QStringLiteral("https://%1:%2/").arg(host.address).arg(host.ports.value(QStringLiteral("_https._tcp"))))
            : hasAny(types, {"_http._tcp"}) ? QUrl(QStringLiteral("http://%1:%2/").arg(host.address).arg(host.ports.value(QStringLiteral("_http._tcp")))) : QUrl();
        const QStringList lines = {host.hostName, host.address};

        if (hasAny(types, {"_ipp._tcp", "_ipps._tcp", "_printer._tcp", "_pdl-datastream._tcp", "_uscan._tcp", "_scanner._tcp"})) {
            const QUrl page = web.isValid() ? web : QUrl(QStringLiteral("http://%1/").arg(host.address));
            m_printers->model()->appendRow(newTile(themeIcon({"printer", "printer-network"}), host.name, lines, page, host.address));
        } else if (hasAny(types, {"_airplay._tcp", "_raop._tcp", "_googlecast._tcp", "_spotify-connect._tcp", "_daap._tcp", "_sonos._tcp"})) {
            m_media->model()->appendRow(newTile(themeIcon({"video-television", "multimedia-player"}), host.name, lines, web, host.address));
        } else if (hasAny(types, {"_smb._tcp", "_sftp-ssh._tcp", "_ssh._tcp", "_workstation._tcp", "_device-info._tcp", "_rfb._tcp", "_afpovertcp._tcp", "_nfs._tcp", "_kdeconnect._udp"})) {
            // Shared folders first, as Windows opens a computer's shares.
            QUrl url = web;
            if (hasAny(types, {"_smb._tcp"})) {
                url = QUrl(QStringLiteral("smb://%1/").arg(host.hostName));
            } else if (hasAny(types, {"_sftp-ssh._tcp", "_ssh._tcp"})) {
                url = QUrl(QStringLiteral("sftp://%1/").arg(host.hostName));
            }
            m_computers->model()->appendRow(newTile(themeIcon({"computer", "network-server"}), shortName.isEmpty() ? host.name : shortName, lines, url, host.address));
        } else {
            m_others->model()->appendRow(newTile(themeIcon({"network-server", "network-workgroup"}), host.name.isEmpty() ? shortName : host.name, lines, web, host.address));
        }
    }

    // Devices that announce nothing but talked to this computer: the kernel's neighbour table.
    QFile neighbours(QStringLiteral("/proc/net/arp"));
    if (neighbours.open(QIODevice::ReadOnly)) {
        neighbours.readLine(); // the column titles
        while (!neighbours.atEnd()) {
            const QStringList fields = QString::fromLatin1(neighbours.readLine()).simplified().split(QLatin1Char(' '));
            // IP address, HW type, flags, HW address, mask, device; flag 0x2 is a complete entry.
            if (fields.size() < 6 || !(fields[2].toUInt(nullptr, 16) & 0x2) || fields[3] == QLatin1String("00:00:00:00:00:00")) {
                continue;
            }
            const QString address = fields[0];
            if (address == m_gateway || addresses.contains(address)) {
                continue;
            }
            addresses.append(address);
            m_others->model()->appendRow(newTile(themeIcon({"network-server", "network-workgroup"}), address, {fields[3], fields[5]}, QUrl(), address));
            // A name from the DNS, when it knows one.
            QHostInfo::lookupHost(address, this, [this, address](const QHostInfo &info) {
                if (info.error() != QHostInfo::NoError || info.hostName().isEmpty() || info.hostName() == address) {
                    return;
                }
                QStandardItemModel *model = m_others->model();
                for (int row = 0; row < model->rowCount(); ++row) {
                    QStandardItem *tile = model->item(row);
                    if (tile->data(TileRoles::KeyRole).toString() == address && tile->text() == address) {
                        tile->setText(info.hostName());
                    }
                }
            });
        }
    }
    updateHeights();
    updateStatus();
}

void NetworkPage::updateHeights()
{
    const int width = viewport()->width() - 2 * PageMargin;
    for (TileSection *section : sections()) {
        section->updateHeight(width);
    }
}

void NetworkPage::updateStatus()
{
    int devices = 0;
    for (TileSection *section : {m_computers, m_media, m_printers, m_others}) {
        devices += section->model()->rowCount();
    }
    if (m_avahiMissing) {
        m_status->setText(i18ndc(shellkitDomain,
                                 "@info",
                                 "Devices that announce themselves on the network are not shown, because avahi-browse is not installed (package avahi-tools)."));
    } else if (m_discovery) {
        m_status->setText(i18ndc(shellkitDomain, "@info", "Searching for devices on the network…"));
    } else if (devices == 0) {
        m_status->setText(i18ndc(shellkitDomain, "@info", "No other devices were found on the network."));
    } else {
        m_status->clear();
    }
    m_status->setVisible(!m_status->text().isEmpty());
}

void NetworkPage::connectSection(TileSection *section)
{
    connect(section, &TileSection::activated, this, [this, section](const QModelIndex &tile) {
        open(section, tile, false);
    });
    connect(section, &TileSection::middleClicked, this, [this, section](const QModelIndex &tile) {
        open(section, tile, true);
    });
    connect(section, &TileSection::contextMenuRequested, this, [this, section](const QModelIndex &tile, const QPoint &globalPos) {
        showContextMenu(section, tile, globalPos);
    });
}

void NetworkPage::open(TileSection *section, const QModelIndex &tile, bool newTab)
{
    if (section == m_adapters) {
        // Windows opens the adapter's status; Plasma has its network settings.
        auto job = new KIO::CommandLauncherJob(QStringLiteral("systemsettings"), {QStringLiteral("kcm_networkmanagement")}, this);
        job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, this));
        job->start();
        return;
    }
    const QUrl url = tile.data(TileRoles::UrlRole).toUrl();
    if (!url.isValid()) {
        return;
    }
    if (isWebUrl(url)) {
        QDesktopServices::openUrl(url);
    } else if (newTab) {
        Q_EMIT folderInNewTab(url);
    } else {
        Q_EMIT folderActivated(url);
    }
}

void NetworkPage::showContextMenu(TileSection *section, const QModelIndex &tile, const QPoint &globalPos)
{
    const QUrl url = tile.data(TileRoles::UrlRole).toUrl();
    const QString address = tile.data(TileRoles::KeyRole).toString();

    QMenu menu(this);
    QAction *openAction = nullptr;
    if (section == m_adapters) {
        openAction = menu.addAction(QIcon::fromTheme(QStringLiteral("configure")), i18ndc(shellkitDomain, "@action:inmenu", "Network settings"), this, [this, section, tile]() {
            open(section, tile, false);
        });
    } else if (isWebUrl(url)) {
        openAction = menu.addAction(QIcon::fromTheme(QStringLiteral("internet-web-browser")), i18ndc(shellkitDomain, "@action:inmenu", "View device webpage"), this, [url]() {
            QDesktopServices::openUrl(url);
        });
    } else if (url.isValid()) {
        openAction = menu.addAction(QIcon::fromTheme(QStringLiteral("document-open")), i18ndc(shellkitDomain, "@action:inmenu", "Open"), this, [this, section, tile]() {
            open(section, tile, false);
        });
        menu.addAction(QIcon::fromTheme(QStringLiteral("tab-new")), i18nc("@action:inmenu", "Open in New Tab"), this, [this, section, tile]() {
            open(section, tile, true);
        });
        menu.addAction(QIcon::fromTheme(QStringLiteral("window-new")), i18nc("@action:inmenu", "Open in New Window"), this, [this, url]() {
            Dolphin::openNewWindow({url}, this);
        });
    }
    if (openAction) {
        QFont font = openAction->font();
        font.setBold(true);
        openAction->setFont(font);
        menu.addSeparator();
    }
    if (!address.isEmpty()) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18ndc(shellkitDomain, "@action:inmenu", "Copy IP address"), this, [address]() {
            QApplication::clipboard()->setText(address);
        });
    }
    if (!menu.isEmpty()) {
        menu.exec(globalPos);
    }
}

#include "moc_networkpage.cpp"
