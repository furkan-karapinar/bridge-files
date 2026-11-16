/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "compressedfolders.h"
#include "copyprogressdialog.h"
#include "fileoperations.h"
#include "shellkit.h"

#include <KIO/ListJob>
#include <KIO/MkpathJob>
#include <KLocalizedString>
#include <KMessageBox>
#include <KProtocolManager>

#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeDatabase>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <sys/stat.h>
#include <zlib.h>

namespace
{
constexpr qint64 ChunkSize = 256 * 1024;
constexpr quint32 Max32 = 0xffffffffu;
// Files this large get Zip64 sizes in their local header: deflate may grow data a little.
constexpr qulonglong Zip64Threshold = 0xff000000ull;

void put16(QByteArray &data, quint16 value)
{
    data.append(char(value & 0xff));
    data.append(char(value >> 8));
}

void put32(QByteArray &data, quint32 value)
{
    put16(data, quint16(value & 0xffff));
    put16(data, quint16(value >> 16));
}

void put64(QByteArray &data, quint64 value)
{
    put32(data, quint32(value & Max32));
    put32(data, quint32(value >> 32));
}

/** MS-DOS date and time as zip stores them (local time, two-second steps, from 1980). */
void dosTime(const QDateTime &when, quint16 *time, quint16 *date)
{
    QDateTime local = when.toLocalTime();
    if (!local.isValid() || local.date().year() < 1980) {
        local = QDateTime(QDate(1980, 1, 1), QTime(0, 0));
    }
    const QDate d = local.date();
    const QTime t = local.time();
    *date = quint16(((d.year() - 1980) << 9) | (d.month() << 5) | d.day());
    *time = quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
}

struct Item {
    QString path; // on disk
    QByteArray name; // in the archive, '/' after folders
    bool dir = false;
    qulonglong size = 0;
    quint32 mode = 0;
    QDateTime modified;
};

struct Entry {
    QByteArray name;
    quint32 crc = 0;
    quint64 packed = 0;
    quint64 size = 0;
    quint64 offset = 0;
    quint16 method = 0;
    quint16 time = 0;
    quint16 date = 0;
    quint32 attributes = 0;
};

QString archiveName(const KFileItem &item)
{
    // "Report.docx" -> "Report.zip", a folder keeps its whole name.
    QString base = item.isDir() ? item.name() : QFileInfo(item.name()).completeBaseName();
    if (base.isEmpty()) {
        base = item.name();
    }
    return base;
}

/** The folder an archive extracts into: "a.zip" -> "a", "a.tar.gz" -> "a". */
QString extractedName(const KFileItem &archive)
{
    const QString name = archive.name();
    const QString suffix = QMimeDatabase().suffixForFileName(name);
    if (!suffix.isEmpty() && name.size() > suffix.size() + 1) {
        return name.left(name.size() - suffix.size() - 1);
    }
    return archiveName(archive);
}

/** "name.zip", or "name (2).zip" and so on when taken. */
QUrl freeArchiveUrl(const QUrl &folder, const QString &base)
{
    const QString dir = folder.toLocalFile();
    QString name = base + QStringLiteral(".zip");
    for (int i = 2; QFileInfo::exists(dir + QLatin1Char('/') + name); ++i) {
        name = QStringLiteral("%1 (%2).zip").arg(base).arg(i);
    }
    return QUrl::fromLocalFile(dir + QLatin1Char('/') + name);
}
}

CompressJob::CompressJob(const QList<QUrl> &sources, const QUrl &archive)
    : m_sources(sources)
    , m_archive(archive)
{
    setCapabilities(KJob::Killable | KJob::Suspendable);
    m_timer = new QTimer(this);
    m_timer->setInterval(200);
    connect(m_timer, &QTimer::timeout, this, &CompressJob::report);
}

CompressJob::~CompressJob()
{
    stopThread();
}

void CompressJob::start()
{
    m_timer->start();
    m_thread = QThread::create([this]() {
        const Result result = run();
        QMetaObject::invokeMethod(
            this,
            [this, result]() {
                if (m_stop) {
                    return; // killed: doKill() has finished the job
                }
                m_timer->stop();
                report();
                if (result.error) {
                    QFile::remove(m_archive.toLocalFile());
                    setError(result.error);
                    setErrorText(result.text);
                }
                emitResult();
            },
            Qt::QueuedConnection);
    });
    m_thread->start();
}

void CompressJob::stopThread()
{
    if (m_thread) {
        m_stop = true;
        m_paused = false;
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

bool CompressJob::doKill()
{
    stopThread();
    m_timer->stop();
    // A cancelled archive is not left behind half written.
    QFile::remove(m_archive.toLocalFile());
    return true;
}

bool CompressJob::doSuspend()
{
    m_paused = true;
    return true;
}

bool CompressJob::doResume()
{
    m_paused = false;
    return true;
}

void CompressJob::report()
{
    const qulonglong total = m_totalBytes;
    const qulonglong done = m_doneBytes;
    setTotalAmount(KJob::Files, m_totalFiles);
    setProcessedAmount(KJob::Files, m_doneFiles);
    setTotalAmount(KJob::Bytes, total);
    setProcessedAmount(KJob::Bytes, done);
    if (total > 0) {
        emitPercent(done, total);
    }
    if (!isSuspended()) {
        emitSpeed((done - std::min(done, m_lastBytes)) * 1000 / qulonglong(m_timer->interval()));
    }
    m_lastBytes = done;
    QString current;
    {
        QMutexLocker locker(&m_currentLock);
        current = m_current;
    }
    // Every time: the copy dialog only shows up after a moment and listens from then on.
    if (!current.isEmpty()) {
        Q_EMIT compressing(this, QUrl::fromLocalFile(current));
    }
}

CompressJob::Result CompressJob::run()
{
    // What goes in: each source with everything below it, named from the sources' folder.
    QList<Item> items;
    qulonglong totalBytes = 0;
    const auto add = [&](const QString &path, const QString &base) {
        struct stat info;
        if (::stat(QFile::encodeName(path).constData(), &info) != 0) {
            return; // vanished or a broken link: Windows skips those too
        }
        Item item;
        item.path = path;
        item.dir = S_ISDIR(info.st_mode);
        item.size = item.dir ? 0 : qulonglong(info.st_size);
        item.mode = info.st_mode;
        item.modified = QDateTime::fromSecsSinceEpoch(info.st_mtime);
        item.name = QDir(base).relativeFilePath(path).toUtf8();
        if (item.dir) {
            item.name += '/';
        }
        totalBytes += item.size;
        items.append(item);
    };
    for (const QUrl &source : m_sources) {
        const QString path = source.toLocalFile();
        const QString base = QFileInfo(path).absolutePath();
        add(path, base);
        if (QFileInfo(path).isDir() && !QFileInfo(path).isSymLink()) {
            QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
            while (it.hasNext() && !m_stop) {
                add(it.next(), base);
            }
        }
    }
    m_totalBytes = totalBytes;
    m_totalFiles = items.count();

    QFile out(m_archive.toLocalFile());
    if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        return {KIO::ERR_CANNOT_WRITE, m_archive.toLocalFile()};
    }
    const auto write = [&out](const QByteArray &data) {
        return out.write(data) == data.size();
    };

    QList<Entry> entries;
    QByteArray buffer(ChunkSize, Qt::Uninitialized);
    QByteArray packed(ChunkSize + 1024, Qt::Uninitialized);
    for (const Item &item : std::as_const(items)) {
        if (m_stop) {
            return {KJob::KilledJobError, QString()};
        }
        {
            QMutexLocker locker(&m_currentLock);
            m_current = item.path;
        }
        Entry entry;
        entry.name = item.name;
        entry.offset = quint64(out.pos());
        entry.size = item.size;
        entry.method = item.dir || item.size == 0 ? 0 : 8;
        entry.attributes = (item.mode << 16) | (item.dir ? 0x10 : 0);
        dosTime(item.modified, &entry.time, &entry.date);
        const bool zip64 = item.size >= Zip64Threshold;

        // Local header; CRC and sizes are filled in once the data is written.
        QByteArray header;
        put32(header, 0x04034b50);
        put16(header, zip64 ? 45 : 20);
        put16(header, 0x0800); // names are UTF-8
        put16(header, entry.method);
        put16(header, entry.time);
        put16(header, entry.date);
        put32(header, 0);
        put32(header, zip64 ? Max32 : 0);
        put32(header, zip64 ? Max32 : 0);
        put16(header, quint16(entry.name.size()));
        put16(header, zip64 ? 20 : 0);
        header += entry.name;
        if (zip64) {
            put16(header, 0x0001);
            put16(header, 16);
            put64(header, 0);
            put64(header, 0);
        }
        if (!write(header)) {
            return {KIO::ERR_CANNOT_WRITE, m_archive.toLocalFile()};
        }

        if (entry.method == 8) {
            QFile in(item.path);
            if (!in.open(QIODevice::ReadOnly)) {
                return {KIO::ERR_CANNOT_OPEN_FOR_READING, item.path};
            }
            z_stream stream = {};
            if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
                return {KIO::ERR_INTERNAL, item.path};
            }
            uLong crc = crc32(0, nullptr, 0);
            bool finished = false;
            bool failed = false;
            while (!finished && !failed) {
                while (m_paused && !m_stop) {
                    QThread::msleep(100);
                }
                if (m_stop) {
                    failed = true;
                    break;
                }
                const qint64 read = in.read(buffer.data(), ChunkSize);
                if (read < 0) {
                    failed = true;
                    break;
                }
                crc = crc32(crc, reinterpret_cast<const Bytef *>(buffer.constData()), uInt(read));
                stream.next_in = reinterpret_cast<Bytef *>(buffer.data());
                stream.avail_in = uInt(read);
                const int flush = in.atEnd() || read == 0 ? Z_FINISH : Z_NO_FLUSH;
                do {
                    stream.next_out = reinterpret_cast<Bytef *>(packed.data());
                    stream.avail_out = uInt(packed.size());
                    const int status = deflate(&stream, flush);
                    if (status == Z_STREAM_ERROR) {
                        failed = true;
                        break;
                    }
                    const qint64 have = packed.size() - stream.avail_out;
                    if (out.write(packed.constData(), have) != have) {
                        failed = true;
                        break;
                    }
                    if (status == Z_STREAM_END) {
                        finished = true;
                    }
                } while (stream.avail_out == 0);
                m_doneBytes += qulonglong(read);
            }
            entry.packed = stream.total_out;
            deflateEnd(&stream);
            if (m_stop) {
                return {KJob::KilledJobError, QString()};
            }
            if (failed) {
                return {KIO::ERR_CANNOT_WRITE, m_archive.toLocalFile()};
            }
            entry.crc = quint32(crc);
            // The file may have changed since it was counted.
            entry.size = stream.total_in;
        }

        // Patch CRC and sizes into the local header.
        const qint64 end = out.pos();
        QByteArray fields;
        put32(fields, entry.crc);
        if (zip64) {
            put32(fields, Max32);
            put32(fields, Max32);
        } else if (entry.size > Max32 || entry.packed > Max32) {
            return {KIO::ERR_CANNOT_WRITE, item.path}; // grew past 4 GB while it was read
        } else {
            put32(fields, quint32(entry.packed));
            put32(fields, quint32(entry.size));
        }
        if (!out.seek(qint64(entry.offset) + 14) || !write(fields)) {
            return {KIO::ERR_CANNOT_WRITE, m_archive.toLocalFile()};
        }
        if (zip64) {
            QByteArray sizes;
            put64(sizes, entry.size);
            put64(sizes, entry.packed);
            if (!out.seek(qint64(entry.offset) + 30 + entry.name.size() + 4) || !write(sizes)) {
                return {KIO::ERR_CANNOT_WRITE, m_archive.toLocalFile()};
            }
        }
        if (!out.seek(end)) {
            return {KIO::ERR_CANNOT_WRITE, m_archive.toLocalFile()};
        }
        entries.append(entry);
        ++m_doneFiles;
    }

    // Central directory.
    const quint64 directoryStart = quint64(out.pos());
    for (const Entry &entry : std::as_const(entries)) {
        QByteArray extra;
        if (entry.size >= Max32) {
            put64(extra, entry.size);
        }
        if (entry.packed >= Max32) {
            put64(extra, entry.packed);
        }
        if (entry.offset >= Max32) {
            put64(extra, entry.offset);
        }
        if (!extra.isEmpty()) {
            QByteArray block;
            put16(block, 0x0001);
            put16(block, quint16(extra.size()));
            extra.prepend(block);
        }
        QByteArray header;
        put32(header, 0x02014b50);
        put16(header, (3 << 8) | 45); // made on Unix, spec 4.5
        put16(header, extra.isEmpty() ? 20 : 45);
        put16(header, 0x0800);
        put16(header, entry.method);
        put16(header, entry.time);
        put16(header, entry.date);
        put32(header, entry.crc);
        put32(header, entry.packed >= Max32 ? Max32 : quint32(entry.packed));
        put32(header, entry.size >= Max32 ? Max32 : quint32(entry.size));
        put16(header, quint16(entry.name.size()));
        put16(header, quint16(extra.size()));
        put16(header, 0); // comment
        put16(header, 0); // disk
        put16(header, 0); // internal attributes
        put32(header, entry.attributes);
        put32(header, entry.offset >= Max32 ? Max32 : quint32(entry.offset));
        header += entry.name;
        header += extra;
        if (!write(header)) {
            return {KIO::ERR_CANNOT_WRITE, m_archive.toLocalFile()};
        }
    }
    const quint64 directoryEnd = quint64(out.pos());
    const quint64 directorySize = directoryEnd - directoryStart;
    const quint64 count = quint64(entries.count());

    QByteArray tail;
    const bool zip64 = count >= 0xffff || directoryStart >= Max32 || directorySize >= Max32;
    if (zip64) {
        put32(tail, 0x06064b50);
        put64(tail, 44);
        put16(tail, (3 << 8) | 45);
        put16(tail, 45);
        put32(tail, 0);
        put32(tail, 0);
        put64(tail, count);
        put64(tail, count);
        put64(tail, directorySize);
        put64(tail, directoryStart);
        put32(tail, 0x07064b50);
        put32(tail, 0);
        put64(tail, directoryEnd);
        put32(tail, 1);
    }
    put32(tail, 0x06054b50);
    put16(tail, 0);
    put16(tail, 0);
    put16(tail, quint16(std::min<quint64>(count, 0xffff)));
    put16(tail, quint16(std::min<quint64>(count, 0xffff)));
    put32(tail, quint32(std::min<quint64>(directorySize, Max32)));
    put32(tail, quint32(std::min<quint64>(directoryStart, Max32)));
    put16(tail, 0);
    if (!write(tail) || !out.flush()) {
        return {KIO::ERR_CANNOT_WRITE, m_archive.toLocalFile()};
    }
    out.close();
    return {};
}

ExtractDialog::ExtractDialog(const KFileItem &archive, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18ndc(shellkitDomain, "@title:window", "Extract Compressed (Zipped) Folders"));

    auto heading = new QLabel(i18ndc(shellkitDomain, "@title", "Select a Destination and Extract Files"), this);
    QFont font = heading->font();
    font.setPointSizeF(font.pointSizeF() * 1.35);
    heading->setFont(font);

    auto label = new QLabel(i18ndc(shellkitDomain, "@label:textbox", "Files will be extracted to this folder:"), this);
    // Next to the archive, in a folder named after it (Windows: "a.zip" -> "a").
    const QString folder = archive.url().adjusted(QUrl::RemoveFilename).toLocalFile() + extractedName(archive);
    m_folder = new QLineEdit(QDir::toNativeSeparators(folder), this);
    label->setBuddy(m_folder);
    auto browse = new QPushButton(i18ndc(shellkitDomain, "@action:button", "Browse..."), this);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString chosen = QFileDialog::getExistingDirectory(this,
                                                                 i18ndc(shellkitDomain, "@title:window", "Select a destination"),
                                                                 QFileInfo(m_folder->text()).absolutePath());
        if (!chosen.isEmpty()) {
            m_folder->setText(QDir::toNativeSeparators(chosen));
        }
    });
    auto row = new QHBoxLayout;
    row->addWidget(m_folder, 1);
    row->addWidget(browse);

    m_show = new QCheckBox(i18ndc(shellkitDomain, "@option:check", "Show extracted files when complete"), this);
    m_show->setChecked(true);

    auto buttons = new QDialogButtonBox(this);
    auto extract = buttons->addButton(i18ndc(shellkitDomain, "@action:button", "Extract"), QDialogButtonBox::AcceptRole);
    extract->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_folder, &QLineEdit::textChanged, this, [this, extract]() {
        extract->setEnabled(!m_folder->text().trimmed().isEmpty());
    });

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->addWidget(heading);
    layout->addSpacing(16);
    layout->addWidget(label);
    layout->addLayout(row);
    layout->addSpacing(8);
    layout->addWidget(m_show);
    layout->addStretch(1);
    layout->addSpacing(24);
    layout->addWidget(buttons);
    resize(560, sizeHint().height());
    m_folder->setFocus();
    m_folder->selectAll();
}

QUrl ExtractDialog::destination() const
{
    QString path = QDir::fromNativeSeparators(m_folder->text().trimmed());
    if (path.startsWith(QLatin1String("~/"))) {
        path = QDir::homePath() + path.mid(1);
    }
    return QUrl::fromLocalFile(QDir::cleanPath(path));
}

bool ExtractDialog::showWhenDone() const
{
    return m_show->isChecked();
}

namespace CompressedFolders
{
QUrl archiveRoot(const KFileItem &item)
{
    if (item.isNull() || !item.isFile() || !item.url().isLocalFile()) {
        return QUrl();
    }
    const QString protocol = KProtocolManager::protocolForArchiveMimetype(item.mimetype());
    if (protocol.isEmpty()) {
        return QUrl();
    }
    QUrl url = item.url();
    url.setScheme(protocol);
    url.setPath(url.path() + QLatin1Char('/'));
    return url;
}

bool canExtract(const KFileItem &item)
{
    return archiveRoot(item).isValid();
}

void compress(const KFileItemList &items, QWidget *window, const std::function<void(const QUrl &)> &created)
{
    if (items.isEmpty()) {
        return;
    }
    QList<QUrl> sources;
    for (const KFileItem &item : items) {
        if (!item.url().isLocalFile()) {
            return;
        }
        sources.append(item.url());
    }
    const QUrl folder = items.first().url().adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    const QUrl archive = freeArchiveUrl(folder, archiveName(items.first()));

    auto job = new CompressJob(sources, archive);
    QPointer<QWidget> parent(window);
    QObject::connect(job, &KJob::result, job, [job, parent, created, archive]() {
        if (job->error() == KJob::KilledJobError) {
            return;
        }
        if (job->error()) {
            KMessageBox::error(parent, job->errorString());
            return;
        }
        if (created) {
            created(archive);
        }
    });
    CopyProgressDialog::trackCompression(job, window);
    QTimer::singleShot(0, job, &CompressJob::start);
}

void extractAll(const KFileItem &archive, QWidget *window, const std::function<void(const QUrl &)> &show)
{
    const QUrl root = archiveRoot(archive);
    if (!root.isValid()) {
        return;
    }
    auto dialog = new ExtractDialog(archive, window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QObject::connect(dialog, &QDialog::accepted, dialog, [dialog, root, show]() {
        const QUrl destination = dialog->destination();
        const bool showFiles = dialog->showWhenDone();
        QPointer<QWidget> parent(dialog->parentWidget());
        // The folder first, then everything at the top of the archive is copied into it, so
        // the copy dialog, conflicts and undo work as for any copy.
        auto mkpath = KIO::mkpath(destination, QUrl(), KIO::HideProgressInfo);
        QObject::connect(mkpath, &KJob::result, [mkpath, root, destination, showFiles, parent, show]() {
            if (mkpath->error()) {
                KMessageBox::error(parent, mkpath->errorString());
                return;
            }
            auto list = KIO::listDir(root, KIO::HideProgressInfo);
            auto entries = std::make_shared<QList<QUrl>>();
            QObject::connect(list, &KIO::ListJob::entries, [root, entries](KIO::Job *, const KIO::UDSEntryList &found) {
                for (const KIO::UDSEntry &entry : found) {
                    const QString name = entry.stringValue(KIO::UDSEntry::UDS_NAME);
                    if (name != QLatin1String(".") && name != QLatin1String("..")) {
                        QUrl url = root;
                        url.setPath(root.path() + name);
                        entries->append(url);
                    }
                }
            });
            QObject::connect(list, &KJob::result, [list, entries, destination, showFiles, parent, show]() {
                if (list->error()) {
                    KMessageBox::error(parent, list->errorString());
                    return;
                }
                const auto openFolder = [showFiles, show, destination]() {
                    if (showFiles && show) {
                        show(destination);
                    }
                };
                if (entries->isEmpty()) {
                    openFolder();
                    return;
                }
                FileOperation *operation = FileOperations::self()->start(FileOps::Type::Copy, *entries, destination, parent);
                if (!operation) {
                    return;
                }
                QObject::connect(operation, &FileOperation::finished, [openFolder](int error) {
                    if (!error) {
                        openFolder();
                    }
                });
            });
        });
    });
    dialog->show();
}
}

#include "moc_compressedfolders.cpp"
