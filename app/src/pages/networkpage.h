/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef NETWORKPAGE_H
#define NETWORKPAGE_H

#include "pages/shellkitpage.h"

#include <QMap>
#include <QStringList>

class QLabel;
class QModelIndex;
class QProcess;
class QTimer;
class TileSection;

/**
 * @brief shellkit fork: the Network page of the File Explorer.
 *
 * Shown for shellkitNetworkUrl(). Lists this computer's network adapters, the default
 * gateway, and the computers, media devices, printers and other devices that announce
 * themselves on the local network (DNS-SD through avahi-browse) or answered this
 * computer lately (the kernel's neighbour table). A double click opens a computer's
 * shared files (smb:/ or sftp:/) or a device's web page.
 */
class NetworkPage : public ShellkitPage
{
    Q_OBJECT

public:
    explicit NetworkPage(QWidget *parent = nullptr);
    ~NetworkPage() override;

    void refresh() override;
    int itemCount() const override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /** A host found by avahi-browse, with the service types it announces. */
    struct Host {
        QString name;
        QString hostName;
        QString address;
        QStringList types;
        QMap<QString, int> ports;
    };

    void fillAdapters();
    void fillGateway();
    void startDiscovery();
    void discoveryFinished();
    void fillDevices();
    void updateHeights();
    void updateStatus();
    /** Connects the tiles of  section to open() and showContextMenu(). */
    void connectSection(TileSection *section);
    /** Opens a computer's files here or in a new tab, a web page in the browser. */
    void open(TileSection *section, const QModelIndex &tile, bool newTab);
    void showContextMenu(TileSection *section, const QModelIndex &tile, const QPoint &globalPos);

    QList<TileSection *> sections() const;

    TileSection *m_adapters;
    TileSection *m_infrastructure;
    TileSection *m_computers;
    TileSection *m_media;
    TileSection *m_printers;
    TileSection *m_others;
    QLabel *m_status;
    QProcess *m_discovery = nullptr;
    QTimer *m_discoveryTimeout;
    QString m_discoveryOutput;
    QMap<QString, Host> m_hosts;
    QString m_gateway;
    bool m_avahiMissing = false;
};

#endif // NETWORKPAGE_H
