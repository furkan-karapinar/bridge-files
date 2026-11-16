/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PROPERTIESDIALOG_H
#define PROPERTIESDIALOG_H

#include "dolphin_export.h"

#include <KFileItem>

#include <QDialog>
#include <QUrl>

#include <atomic>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QThread;

/**
 * @brief shellkit fork: the Windows Properties dialog.
 *
 * "General" as in Windows: icon and name, type and the program that opens it, location, size,
 * size on disk and (for folders) what it contains, counted in the background, the dates, and
 * the Read-only and Hidden attributes; "Security" lists the owner, the group and everyone with
 * what each may do, and "Edit..." opens the full KDE dialog. OK, Cancel and Apply.
 *
 * Only for local files; anything else (remote places, the trash, devices) still gets
 * KPropertiesDialog.
 */
class DOLPHIN_EXPORT PropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    /** Shows the dialog for \a items (the KDE one when they are not all local files). */
    static void showProperties(const KFileItemList &items, QWidget *parent);
    static void showProperties(const QList<QUrl> &urls, QWidget *parent);

    ~PropertiesDialog() override;

private:
    PropertiesDialog(const KFileItemList &items, QWidget *parent);

    QWidget *createGeneralPage();
    QWidget *createSecurityPage();
    void startCounting();
    void setCounts(qulonglong files, qulonglong folders, qulonglong size, qulonglong disk);
    void updateOpensWith();
    void setChanged();
    /** Renames and changes attributes; false when the new name is not valid. */
    bool apply();

    const KFileItemList m_items;
    // Where the items are now: Apply may have renamed them.
    QList<QUrl> m_urls;
    bool m_single = false;
    bool m_folder = false;

    QLineEdit *m_name = nullptr;
    QLabel *m_openIcon = nullptr;
    QLabel *m_openName = nullptr;
    QLabel *m_size = nullptr;
    QLabel *m_disk = nullptr;
    QLabel *m_contains = nullptr;
    QCheckBox *m_readOnly = nullptr;
    QCheckBox *m_hidden = nullptr;
    QDialogButtonBox *m_buttons = nullptr;

    QThread *m_counter = nullptr;
    std::atomic_bool m_stop = false;
};

#endif // PROPERTIESDIALOG_H
