/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TILESECTION_H
#define TILESECTION_H

#include <QListView>
#include <QWidget>

class QLabel;
class QStandardItemModel;
class QToolButton;

/** shellkit fork: the data of a tile on the This PC and Network pages. */
namespace TileRoles
{
enum Role {
    /** Where a double click goes, may be empty. */
    UrlRole = Qt::UserRole + 1,
    /** The lines under the name, a QStringList. */
    LinesRole,
    /** The used part of a drive from 0 to 1 for the capacity bar, absent for no bar. */
    UsageRole,
    /** What the page needs to find the tile's object again, such as a device udi. */
    KeyRole,
};
}

/**
 * @brief shellkit fork: a section of the This PC and Network pages.
 *
 * A header that folds the section, as on the Home page, above tiles laid out in rows:
 * the icon on the left, the name, an optional capacity bar and grey lines beside it, as
 * in "Devices and drives" of the Windows File Explorer. The section hides itself while it
 * has no tiles.
 */
class TileSection : public QWidget
{
    Q_OBJECT

public:
    TileSection(const QString &title, QWidget *parent);

    QStandardItemModel *model() const;
    QListView *list() const;

    /** Fits the tiles' height to  width; the page scrolls as a whole. */
    void updateHeight(int width);

Q_SIGNALS:
    void activated(const QModelIndex &index);
    void middleClicked(const QModelIndex &index);
    void contextMenuRequested(const QModelIndex &index, const QPoint &globalPos);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateVisibility();

    QToolButton *m_header;
    QListView *m_list;
    QStandardItemModel *m_model;
    bool m_expanded = true;
};

#endif // TILESECTION_H
