/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "propertiesdialog.h"

#include "shellkit.h"

#include <KApplicationTrader>
#include <KIO/ChmodJob>
#include <KIO/CopyJob>
#include <KIO/FileUndoManager>
#include <KJobUiDelegate>
#include <KJobWidgets>
#include <KOpenWithDialog>
#include <KPropertiesDialog>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <sys/stat.h>

#include <algorithm>

namespace
{
// Width of the value column, as in the Windows dialog.
constexpr int ValueWidth = 250;

QFrame *separator(QWidget *parent)
{
    auto line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

QLabel *valueLabel(const QString &text, QWidget *parent)
{
    auto label = new QLabel(text, parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    label->setFixedWidth(ValueWidth);
    return label;
}

/** "Monday, November 16, 2026, 6:46:12 PM" in the user's language, as in Windows. */
QString longDateTime(const QDateTime &dateTime)
{
    const QLocale locale;
    QString timeFormat = locale.timeFormat(QLocale::LongFormat);
    // The long time format ends in the time zone, which Windows leaves out.
    timeFormat.remove(QRegularExpression(QStringLiteral("\\s*t+")));
    return i18ndc(shellkitDomain, "@info %1 is a date, %2 a time", "%1, %2", locale.toString(dateTime.date(), QLocale::LongFormat), locale.toString(dateTime.time(), timeFormat));
}

/** "4.00 KB (4,096 bytes)". */
QString sizeText(qulonglong bytes)
{
    return i18ndc(shellkitDomain, "@info %1 is a size such as 1.46 KB, %2 the exact number of bytes", "%1 (%2 bytes)", shellkitStatusSize(bytes), QLocale().toString(bytes));
}

QString countText(qulonglong files, qulonglong folders)
{
    return i18ndc(shellkitDomain, "@info", "%1 Files, %2 Folders", QLocale().toString(files), QLocale().toString(folders));
}

bool isHiddenName(const QString &name)
{
    return name.startsWith(QLatin1Char('.'));
}

Qt::CheckState combinedState(const QList<bool> &states)
{
    if (states.isEmpty() || !states.contains(true)) {
        return Qt::Unchecked;
    }
    return states.contains(false) ? Qt::PartiallyChecked : Qt::Checked;
}

void setCheckState(QCheckBox *box, Qt::CheckState state)
{
    box->setTristate(state == Qt::PartiallyChecked);
    box->setCheckState(state);
    box->setProperty("initial", int(state));
}

bool checkStateChanged(const QCheckBox *box)
{
    return box && box->checkState() != Qt::PartiallyChecked && box->checkState() != box->property("initial").toInt();
}
}

void PropertiesDialog::showProperties(const KFileItemList &items, QWidget *parent)
{
    if (items.isEmpty()) {
        return;
    }
    const bool local = std::all_of(items.cbegin(), items.cend(), [](const KFileItem &item) {
        return item.url().isLocalFile() && !item.isNull();
    });
    QDialog *dialog = nullptr;
    if (local) {
        dialog = new PropertiesDialog(items, parent);
    } else {
        dialog = new KPropertiesDialog(items, parent);
    }
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void PropertiesDialog::showProperties(const QList<QUrl> &urls, QWidget *parent)
{
    KFileItemList items;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile() || !QFileInfo::exists(url.toLocalFile())) {
            // Not a local file (or gone): the KDE dialog knows how to stat it.
            auto dialog = new KPropertiesDialog(urls, parent);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
            return;
        }
        // An item without a mode stats the local file.
        items.append(KFileItem(url));
    }
    showProperties(items, parent);
}

PropertiesDialog::PropertiesDialog(const KFileItemList &items, QWidget *parent)
    : QDialog(parent)
    , m_items(items)
    , m_single(items.count() == 1)
    , m_folder(items.count() == 1 && items.first().isDir())
{
    for (const KFileItem &item : items) {
        m_urls.append(item.url());
    }
    const QString name = m_items.first().name();
    setWindowTitle(m_single ? i18ndc(shellkitDomain, "@title:window %1 is a file name", "%1 Properties", name)
                            : i18ndc(shellkitDomain, "@title:window %1 is the first of several file names", "%1, ... Properties", name));

    auto tabs = new QTabWidget(this);
    tabs->addTab(createGeneralPage(), i18ndc(shellkitDomain, "@title:tab", "General"));
    if (m_single) {
        tabs->addTab(createSecurityPage(), i18ndc(shellkitDomain, "@title:tab", "Security"));
    }

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    m_buttons->button(QDialogButtonBox::Apply)->setEnabled(false);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (apply()) {
            accept();
        }
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        if (apply()) {
            m_buttons->button(QDialogButtonBox::Apply)->setEnabled(false);
        }
    });

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->addWidget(tabs);
    layout->addWidget(m_buttons);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    startCounting();
}

PropertiesDialog::~PropertiesDialog()
{
    if (m_counter) {
        m_stop = true;
        m_counter->wait();
    }
}

QWidget *PropertiesDialog::createGeneralPage()
{
    auto page = new QWidget;
    auto grid = new QGridLayout(page);
    grid->setContentsMargins(12, 14, 12, 12);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->setColumnMinimumWidth(0, 90);

    const KFileItem &first = m_items.first();
    int files = 0;
    int folders = 0;
    for (const KFileItem &item : m_items) {
        (item.isDir() ? folders : files)++;
    }

    // Icon and name.
    auto icon = new QLabel(page);
    const QIcon itemIcon = m_single ? QIcon::fromTheme(first.iconName()) : QIcon::fromTheme(QStringLiteral("document-multiple"), QIcon::fromTheme(first.iconName()));
    icon->setPixmap(itemIcon.pixmap(32, 32));
    m_name = new QLineEdit(page);
    m_name->setFixedWidth(ValueWidth);
    if (m_single) {
        m_name->setText(first.name());
        connect(m_name, &QLineEdit::textEdited, this, &PropertiesDialog::setChanged);
    } else {
        m_name->setText(countText(files, folders));
        m_name->setReadOnly(true);
    }
    int row = 0;
    grid->addWidget(icon, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(m_name, row++, 1);
    grid->addWidget(separator(page), row++, 0, 1, 2);

    const auto addRow = [&](const QString &label, QWidget *value) {
        grid->addWidget(new QLabel(label, page), row, 0, Qt::AlignLeft | Qt::AlignTop);
        grid->addWidget(value, row++, 1);
    };

    // Type and the program that opens it.
    QString type;
    if (m_single) {
        type = shellkitTypeName(first);
        const QString suffix = QFileInfo(first.name()).suffix();
        if (!first.isDir() && !suffix.isEmpty()) {
            type = i18ndc(shellkitDomain, "@info %1 is a file type, %2 its extension", "%1 (.%2)", type, suffix);
        }
    } else {
        const QString firstType = shellkitTypeName(first);
        const bool sameType = std::all_of(m_items.cbegin(), m_items.cend(), [&firstType](const KFileItem &item) {
            return shellkitTypeName(item) == firstType;
        });
        type = sameType ? i18ndc(shellkitDomain, "@info %1 is a file type", "All of type %1", firstType)
                        : i18ndc(shellkitDomain, "@info Type of several files", "Multiple types");
    }
    addRow(m_single && !m_folder ? i18ndc(shellkitDomain, "@label", "Type of file:") : i18ndc(shellkitDomain, "@label", "Type:"), valueLabel(type, page));

    if (m_single && !m_folder) {
        auto opens = new QWidget(page);
        opens->setFixedWidth(ValueWidth);
        auto opensLayout = new QHBoxLayout(opens);
        opensLayout->setContentsMargins(0, 0, 0, 0);
        m_openIcon = new QLabel(opens);
        m_openName = new QLabel(opens);
        auto change = new QPushButton(i18ndc(shellkitDomain, "@action:button", "Change..."), opens);
        connect(change, &QPushButton::clicked, this, [this]() {
            KOpenWithDialog dialog({m_items.first().url()}, this);
            dialog.hideNoCloseOnExit();
            dialog.hideRunInTerminal();
            dialog.exec();
            updateOpensWith();
        });
        opensLayout->addWidget(m_openIcon);
        opensLayout->addWidget(m_openName, 1);
        opensLayout->addWidget(change);
        addRow(i18ndc(shellkitDomain, "@label", "Opens with:"), opens);
        updateOpensWith();
    }
    grid->addWidget(separator(page), row++, 0, 1, 2);

    // Location and sizes.
    const QString parentPath = first.url().adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).toLocalFile();
    QString location = parentPath;
    if (!m_single) {
        const bool sameFolder = std::all_of(m_items.cbegin(), m_items.cend(), [&parentPath](const KFileItem &item) {
            return item.url().adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).toLocalFile() == parentPath;
        });
        location = sameFolder ? i18ndc(shellkitDomain, "@info %1 is a folder", "All in %1", parentPath)
                              : i18ndc(shellkitDomain, "@info Location of files in several folders", "Various folders");
    }
    addRow(i18ndc(shellkitDomain, "@label", "Location:"), valueLabel(location, page));
    m_size = valueLabel(QString(), page);
    addRow(i18ndc(shellkitDomain, "@label", "Size:"), m_size);
    m_disk = valueLabel(QString(), page);
    addRow(i18ndc(shellkitDomain, "@label", "Size on disk:"), m_disk);
    if (m_folder) {
        m_contains = valueLabel(QString(), page);
        addRow(i18ndc(shellkitDomain, "@label", "Contains:"), m_contains);
    }
    grid->addWidget(separator(page), row++, 0, 1, 2);

    // Dates.
    if (m_single) {
        const QDateTime created = first.time(KFileItem::CreationTime);
        if (created.isValid()) {
            addRow(i18ndc(shellkitDomain, "@label", "Created:"), valueLabel(longDateTime(created), page));
        }
        addRow(i18ndc(shellkitDomain, "@label", "Modified:"), valueLabel(longDateTime(first.time(KFileItem::ModificationTime)), page));
        const QDateTime accessed = first.time(KFileItem::AccessTime);
        if (accessed.isValid()) {
            addRow(i18ndc(shellkitDomain, "@label", "Accessed:"), valueLabel(longDateTime(accessed), page));
        }
        grid->addWidget(separator(page), row++, 0, 1, 2);
    }

    // Attributes: read-only is the write permission, hidden the leading dot.
    auto attributes = new QWidget(page);
    auto attributesLayout = new QHBoxLayout(attributes);
    attributesLayout->setContentsMargins(0, 0, 0, 0);
    QList<bool> readOnly;
    QList<bool> hidden;
    for (const KFileItem &item : m_items) {
        if (!item.isDir()) {
            readOnly.append(!(item.permissions() & (S_IWUSR | S_IWGRP | S_IWOTH)));
        }
        hidden.append(isHiddenName(item.name()));
    }
    if (!readOnly.isEmpty()) {
        m_readOnly = new QCheckBox(i18ndc(shellkitDomain, "@option:check File attribute", "Read-only"), attributes);
        setCheckState(m_readOnly, combinedState(readOnly));
        connect(m_readOnly, &QCheckBox::clicked, this, [this]() {
            // A mixed state goes to on and off, not back to mixed.
            m_readOnly->setTristate(false);
            setChanged();
        });
        attributesLayout->addWidget(m_readOnly);
    }
    m_hidden = new QCheckBox(i18ndc(shellkitDomain, "@option:check File attribute", "Hidden"), attributes);
    setCheckState(m_hidden, combinedState(hidden));
    connect(m_hidden, &QCheckBox::clicked, this, [this]() {
        m_hidden->setTristate(false);
        setChanged();
    });
    attributesLayout->addWidget(m_hidden);
    attributesLayout->addStretch(1);
    addRow(i18ndc(shellkitDomain, "@label", "Attributes:"), attributes);

    grid->setRowStretch(row, 1);
    return page;
}

QWidget *PropertiesDialog::createSecurityPage()
{
    const KFileItem &item = m_items.first();
    const mode_t mode = item.permissions();

    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 14, 12, 12);
    layout->setSpacing(8);

    auto objectName = new QLabel(i18ndc(shellkitDomain, "@label %1 is a path", "Object name:  %1", item.localPath()), page);
    objectName->setTextInteractionFlags(Qt::TextSelectableByMouse);
    objectName->setWordWrap(true);
    layout->addWidget(objectName);

    layout->addWidget(new QLabel(i18ndc(shellkitDomain, "@label", "Group or user names:"), page));
    auto principals = new QTreeWidget(page);
    principals->setHeaderHidden(true);
    principals->setRootIsDecorated(false);
    principals->setFixedHeight(84);
    struct Principal {
        QString name;
        QString icon;
        mode_t read;
        mode_t write;
        mode_t execute;
    };
    const QList<Principal> list = {
        {i18ndc(shellkitDomain, "@item %1 is a user name", "%1 (Owner)", item.user()), QStringLiteral("user"), S_IRUSR, S_IWUSR, S_IXUSR},
        {i18ndc(shellkitDomain, "@item %1 is a group name", "%1 (Group)", item.group()), QStringLiteral("system-users"), S_IRGRP, S_IWGRP, S_IXGRP},
        {i18ndc(shellkitDomain, "@item All other users", "Everyone"), QStringLiteral("system-users"), S_IROTH, S_IWOTH, S_IXOTH},
    };
    for (const Principal &principal : list) {
        auto entry = new QTreeWidgetItem(principals, {principal.name});
        entry->setIcon(0, QIcon::fromTheme(principal.icon));
    }
    layout->addWidget(principals);

    auto permissionsLabel = new QLabel(page);
    layout->addWidget(permissionsLabel);
    auto permissions = new QTreeWidget(page);
    permissions->setRootIsDecorated(false);
    permissions->setSelectionMode(QAbstractItemView::NoSelection);
    permissions->setFocusPolicy(Qt::NoFocus);
    permissions->setHeaderLabels({i18ndc(shellkitDomain, "@title:column", "Permissions"), i18ndc(shellkitDomain, "@title:column", "Allow")});
    permissions->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    // Wide enough for the whole header: ResizeToContents only measures the check marks.
    permissions->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    permissions->header()->resizeSection(1, permissions->header()->fontMetrics().horizontalAdvance(permissions->headerItem()->text(1)) + 40);
    permissions->header()->setStretchLastSection(false);
    permissions->setFixedHeight(104);
    const QStringList rights = {
        i18ndc(shellkitDomain, "@item Permission", "Read"),
        i18ndc(shellkitDomain, "@item Permission", "Write"),
        item.isDir() ? i18ndc(shellkitDomain, "@item Permission of a folder", "Open folder") : i18ndc(shellkitDomain, "@item Permission of a file", "Run as a program"),
    };
    for (const QString &right : rights) {
        auto entry = new QTreeWidgetItem(permissions, {right, QString()});
        entry->setTextAlignment(1, Qt::AlignCenter);
    }
    layout->addWidget(permissions);

    const auto showPrincipal = [=]() {
        const int index = std::max(0, principals->indexOfTopLevelItem(principals->currentItem()));
        const Principal &principal = list.at(index);
        permissionsLabel->setText(i18ndc(shellkitDomain, "@label %1 is a user or group", "Permissions for %1", principal.name));
        const mode_t bits[] = {principal.read, principal.write, principal.execute};
        for (int i = 0; i < 3; ++i) {
            permissions->topLevelItem(i)->setText(1, (mode & bits[i]) ? QStringLiteral("✓") : QString());
        }
    };
    connect(principals, &QTreeWidget::currentItemChanged, page, showPrincipal);
    principals->setCurrentItem(principals->topLevelItem(0));
    showPrincipal();

    auto editRow = new QHBoxLayout;
    auto editText = new QLabel(i18ndc(shellkitDomain, "@info", "To change permissions, click Edit."), page);
    editText->setWordWrap(true);
    auto edit = new QPushButton(i18ndc(shellkitDomain, "@action:button", "Edit..."), page);
    connect(edit, &QPushButton::clicked, this, [this]() {
        // The KDE dialog has the permissions page, owner, group and the advanced settings.
        auto dialog = new KPropertiesDialog(m_items.first(), this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    editRow->addWidget(editText, 1);
    editRow->addWidget(edit);
    layout->addLayout(editRow);
    layout->addStretch(1);
    return page;
}

void PropertiesDialog::startCounting()
{
    if (m_single && !m_folder) {
        QT_STATBUF info;
        const KFileItem &item = m_items.first();
        if (QT_LSTAT(QFile::encodeName(item.localPath()).constData(), &info) == 0) {
            setCounts(1, 0, info.st_size, qulonglong(info.st_blocks) * 512);
        } else {
            setCounts(1, 0, item.size(), item.size());
        }
        return;
    }

    QStringList paths;
    for (const KFileItem &item : m_items) {
        paths.append(item.localPath());
    }
    const bool countSelf = !m_folder;
    // Folders are counted in the background; the numbers grow as in Windows.
    m_counter = QThread::create([this, paths, countSelf]() {
        qulonglong files = 0;
        qulonglong folders = 0;
        qulonglong size = 0;
        qulonglong disk = 0;
        QElapsedTimer timer;
        timer.start();
        const auto report = [&]() {
            QMetaObject::invokeMethod(
                this,
                [this, files, folders, size, disk]() {
                    setCounts(files, folders, size, disk);
                },
                Qt::QueuedConnection);
        };
        const auto add = [&](const QString &path, bool top) {
            QT_STATBUF info;
            if (QT_LSTAT(QFile::encodeName(path).constData(), &info) != 0) {
                return false;
            }
            if (S_ISDIR(info.st_mode)) {
                if (!top || countSelf) {
                    ++folders;
                }
                return true;
            }
            ++files;
            size += info.st_size;
            disk += qulonglong(info.st_blocks) * 512;
            return false;
        };
        for (const QString &path : paths) {
            if (!add(path, true)) {
                continue;
            }
            QDirIterator it(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext() && !m_stop) {
                add(it.next(), false);
                if (timer.elapsed() > 150) {
                    report();
                    timer.restart();
                }
            }
        }
        if (!m_stop) {
            report();
        }
    });
    m_counter->start();
}

void PropertiesDialog::setCounts(qulonglong files, qulonglong folders, qulonglong size, qulonglong disk)
{
    m_size->setText(sizeText(size));
    m_disk->setText(sizeText(disk));
    if (m_contains) {
        m_contains->setText(countText(files, folders));
    }
}

void PropertiesDialog::updateOpensWith()
{
    const KService::Ptr service = KApplicationTrader::preferredService(m_items.first().mimetype());
    if (service) {
        m_openIcon->setPixmap(QIcon::fromTheme(service->icon()).pixmap(16, 16));
        m_openName->setText(service->name());
    } else {
        m_openIcon->clear();
        m_openName->setText(i18ndc(shellkitDomain, "@info No program opens this file type", "Pick an app"));
    }
}

void PropertiesDialog::setChanged()
{
    m_buttons->button(QDialogButtonBox::Apply)->setEnabled(true);
}

bool PropertiesDialog::apply()
{
    // New names: the edited name and the leading dot of Hidden. m_urls are the files as they are
    // now: an earlier Apply may have renamed them.
    const QList<QUrl> before = m_urls;
    QList<QUrl> urls = m_urls;
    QList<QPair<QUrl, QUrl>> renames;
    for (int i = 0; i < urls.count(); ++i) {
        const QUrl url = urls.at(i);
        QString name = m_single ? m_name->text().trimmed() : url.fileName();
        if (name.isEmpty() || name.contains(QLatin1Char('/')) || name == QLatin1String(".") || name == QLatin1String("..")) {
            m_name->setFocus();
            m_name->selectAll();
            return false;
        }
        if (checkStateChanged(m_hidden)) {
            const bool hide = m_hidden->checkState() == Qt::Checked;
            if (hide && !isHiddenName(name)) {
                name.prepend(QLatin1Char('.'));
            } else if (!hide) {
                while (isHiddenName(name) && name.size() > 1) {
                    name.remove(0, 1);
                }
            }
        }
        if (name != url.fileName()) {
            QUrl dest = url.adjusted(QUrl::RemoveFilename);
            dest.setPath(dest.path() + name);
            renames.append({url, dest});
            urls[i] = dest;
        }
    }
    m_urls = urls;
    if (m_single) {
        // As in Windows, the title follows a rename.
        setWindowTitle(i18ndc(shellkitDomain, "@title:window %1 is a file name", "%1 Properties", m_urls.first().fileName()));
    }

    const auto startRenames = [renames, window = QPointer<QWidget>(window())]() {
        for (const auto &rename : renames) {
            KIO::CopyJob *job = KIO::moveAs(rename.first, rename.second, KIO::HideProgressInfo);
            KJobWidgets::setWindow(job, window);
            if (job->uiDelegate()) {
                job->uiDelegate()->setAutoErrorHandlingEnabled(true);
            }
            KIO::FileUndoManager::self()->recordCopyJob(job);
        }
    };

    // Read-only first: the renames would move the files under it.
    if (checkStateChanged(m_readOnly)) {
        KFileItemList files;
        for (int i = 0; i < m_items.count(); ++i) {
            if (!m_items.at(i).isDir()) {
                // The item as it is now (an earlier Apply may have renamed it).
                files.append(KFileItem(before.at(i)));
            }
        }
        const bool readOnly = m_readOnly->checkState() == Qt::Checked;
        KIO::Job *job = KIO::chmod(files, readOnly ? 0 : S_IWUSR, readOnly ? (S_IWUSR | S_IWGRP | S_IWOTH) : S_IWUSR, QString(), QString(), false, KIO::HideProgressInfo);
        KJobWidgets::setWindow(job, window());
        if (job->uiDelegate()) {
            job->uiDelegate()->setAutoErrorHandlingEnabled(true);
        }
        connect(job, &KJob::result, job, startRenames);
        m_readOnly->setProperty("initial", int(m_readOnly->checkState()));
    } else {
        startRenames();
    }
    if (m_hidden) {
        m_hidden->setProperty("initial", int(m_hidden->checkState()));
    }
    return true;
}

#include "moc_propertiesdialog.cpp"
