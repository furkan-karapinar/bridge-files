/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WIN7DETAILSPANE_H
#define WIN7DETAILSPANE_H

#include <KFileItem>

#include <QPointer>
#include <QWidget>

class DolphinView;
class QGridLayout;
class QLabel;
class QTimer;

namespace KIO
{
class PreviewJob;
}

/**
 * @brief shellkit fork: the details pane of the Windows 7 Windows Explorer.
 *
 * A band along the bottom of the window: a large icon or thumbnail, the name and type of the
 * selected item with its dates and size beside it, the selection count and size for several
 * items, or the item count of the folder when nothing is selected. Follows the active view
 * (setView()).
 */
class Win7DetailsPane : public QWidget
{
    Q_OBJECT

public:
    explicit Win7DetailsPane(QWidget *parent = nullptr);
    ~Win7DetailsPane() override;

    void setView(DolphinView *view);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void scheduleUpdate();
    void refresh();
    void addField(const QString &label, const QString &value);
    void setIcon(const KFileItem &item);

    QPointer<DolphinView> m_view;
    QLabel *m_icon;
    QLabel *m_name;
    QLabel *m_type;
    QGridLayout *m_fields;
    QPointer<KIO::PreviewJob> m_previewJob;
    QTimer *m_updateTimer;
};

#endif // WIN7DETAILSPANE_H
