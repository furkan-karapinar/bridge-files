/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef THISPCPAGE_H
#define THISPCPAGE_H

#include "pages/shellkitpage.h"

class KFilePlacesModel;
class TileSection;
class QModelIndex;

/**
 * @brief shellkit fork: the This PC page (Windows 7: Computer) of the File Explorer.
 *
 * Shown for shellkitThisPcUrl(). "Devices and drives" lists the root file system first
 * and then the drives of the places model, each with its capacity bar and free space.
 * A drive that is not mounted yet is mounted when it is opened.
 */
class ThisPcPage : public ShellkitPage
{
    Q_OBJECT

public:
    explicit ThisPcPage(QWidget *parent = nullptr);

    void refresh() override;
    int itemCount() const override;

    /**
     * The name of a drive as Windows writes it, "Label (mount point)" or "Local Disk
     * (mount point)"; the places model's own text while the drive is not mounted.
     */
    static QString driveName(KFilePlacesModel *places, const QModelIndex &index);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void fill();
    void updateHeights();
    /** The places model row of the drive with  udi. */
    QModelIndex placeIndex(const QString &udi) const;
    /** Opens the drive of  tile, here or in a new tab, after mounting it if needed. */
    void open(const QModelIndex &tile, bool newTab);
    void showContextMenu(const QModelIndex &tile, const QPoint &globalPos);

    TileSection *m_drives;
};

#endif // THISPCPAGE_H
