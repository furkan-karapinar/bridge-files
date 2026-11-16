/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "conflictdialog.h"

#include "copyprogressdialog.h"

#include "dolphin_generalsettings.h"
#include "shellkit.h"

#include <KIO/CopyJob>
#include <KIO/Global>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KWindowSystem>

#include <QCheckBox>
#include <QPainterPath>
#include <QDesktopServices>
#include <QDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMimeDatabase>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <functional>

namespace
{
constexpr int ContentWidth = 460;

enum class Glyph {
    Replace,
    Skip,
    Compare,
};

// Thin line glyphs in the text colour, like the ones on the Windows dialog.
QIcon glyphIcon(Glyph glyph, const QPalette &palette)
{
    QIcon icon;
    for (const qreal scale : {1.0, 2.0}) {
        QPixmap pixmap(QSize(20, 20) * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette.color(QPalette::WindowText), 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        switch (glyph) {
        case Glyph::Replace:
            painter.drawPolyline(QPolygonF({QPointF(4, 10.5), QPointF(8, 14.5), QPointF(16, 5.5)}));
            break;
        case Glyph::Skip: {
            // A curved arrow passing over a dot.
            QPainterPath path;
            path.moveTo(3.5, 13);
            path.cubicTo(5, 5, 14, 5, 16, 11.5);
            painter.drawPath(path);
            painter.drawPolyline(QPolygonF({QPointF(12.5, 11), QPointF(16, 11.8), QPointF(16.8, 8.2)}));
            painter.setBrush(palette.color(QPalette::WindowText));
            painter.drawEllipse(QPointF(10, 15.5), 1, 1);
            break;
        }
        case Glyph::Compare:
            painter.drawRoundedRect(QRectF(3.5, 5.5, 8, 11), 1, 1);
            painter.drawRoundedRect(QRectF(8.5, 3.5, 8, 11), 1, 1);
            break;
        }
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

QString folderName(const QUrl &url)
{
    const QString name = url.adjusted(QUrl::StripTrailingSlash).fileName();
    return name.isEmpty() ? url.toDisplayString(QUrl::PreferLocalFile) : name;
}

QIcon fileIcon(const QUrl &url)
{
    static QMimeDatabase db;
    const QMimeType type = url.isLocalFile() ? db.mimeTypeForFile(url.toLocalFile()) : db.mimeTypeForFile(url.fileName(), QMimeDatabase::MatchExtension);
    return QIcon::fromTheme(type.iconName(), QIcon::fromTheme(type.genericIconName(), QIcon::fromTheme(QStringLiteral("unknown"))));
}

QLabel *mouseTransparent(QLabel *label)
{
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    return label;
}

/** A flat button holding a layout of labels: the Windows "command link". */
class CommandLink : public QToolButton
{
public:
    CommandLink(const QIcon &icon, int iconSize, const QString &title, bool accentTitle, QWidget *parent)
        : QToolButton(parent)
    {
        setAutoRaise(true);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto row = new QHBoxLayout(this);
        row->setContentsMargins(10, 8, 10, 8);
        row->setSpacing(10);
        if (!icon.isNull()) {
            auto iconLabel = mouseTransparent(new QLabel(this));
            iconLabel->setPixmap(icon.pixmap(iconSize, iconSize));
            iconLabel->setAlignment(Qt::AlignTop);
            row->addWidget(iconLabel);
        }
        m_column = new QVBoxLayout;
        m_column->setSpacing(4);
        auto titleLabel = mouseTransparent(new QLabel(title, this));
        if (accentTitle) {
            QFont font = titleLabel->font();
            font.setPointSizeF(font.pointSizeF() * 1.15);
            titleLabel->setFont(font);
            // A palette set here is lost once the tool button is polished, so the colour goes in
            // a style sheet of the label's own.
            titleLabel->setStyleSheet(QStringLiteral("color: %1;").arg(titleLabel->palette().color(QPalette::Link).name()));
        }
        m_column->addWidget(titleLabel);
        row->addLayout(m_column, 1);
    }

    void addDetail(QWidget *widget)
    {
        widget->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_column->addWidget(widget);
    }

    // The size comes from the labels inside, not from the (empty) tool button text.
    QSize sizeHint() const override
    {
        return layout()->sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        return layout()->minimumSize();
    }

private:
    QVBoxLayout *m_column;
};

struct FileDetails {
    QUrl url;
    KIO::filesize_t size = 0;
    QDateTime modified;
};

/** Icon, name, folder, size and date of one of the two files. */
QWidget *detailsWidget(const FileDetails &file, bool withName, bool withFolder, QWidget *parent)
{
    auto widget = new QWidget(parent);
    auto row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 2, 0, 0);
    row->setSpacing(10);
    auto icon = mouseTransparent(new QLabel(widget));
    icon->setPixmap(fileIcon(file.url).pixmap(48, 48));
    icon->setAlignment(Qt::AlignTop);
    row->addWidget(icon);
    auto lines = new QVBoxLayout;
    lines->setSpacing(1);
    const auto addLine = [&](const QString &text) {
        auto label = mouseTransparent(new QLabel(text, widget));
        label->setTextFormat(Qt::PlainText);
        lines->addWidget(label);
    };
    if (withName) {
        addLine(file.url.fileName());
    }
    if (withFolder) {
        addLine(folderName(file.url.adjusted(QUrl::RemoveFilename)));
    }
    addLine(i18ndc(shellkitDomain, "@info %1 is a size", "Size: %1", KIO::convertSize(file.size)));
    if (file.modified.isValid()) {
        addLine(i18ndc(shellkitDomain, "@info %1 is a date and time", "Date modified: %1", QLocale().toString(file.modified.toLocalTime(), QLocale::ShortFormat)));
    }
    lines->addStretch();
    row->addLayout(lines, 1);
    return widget;
}

class ConflictDialog : public QDialog
{
public:
    std::function<void(KIO::RenameDialog_Result, const QUrl &)> decided;

    ConflictDialog(KJob *job, const FileDetails &source, const FileDetails &destination, bool multiple, QWidget *parent)
        : QDialog(parent)
        , m_source(source)
        , m_destination(destination)
        , m_classic(GeneralSettings::classicCopyDialog() || shellkitWin7())
    {
        setAttribute(Qt::WA_DeleteOnClose);
        const auto copyJob = qobject_cast<KIO::CopyJob *>(job);
        m_moving = copyJob && copyJob->operationMode() == KIO::CopyJob::Move;
        m_itemCount = copyJob ? int(copyJob->srcUrls().count()) : 1;

        auto layout = new QVBoxLayout(this);
        layout->setSizeConstraint(QLayout::SetFixedSize);
        layout->setContentsMargins(20, 16, 20, 16);
        layout->setSpacing(10);

        if (multiple) {
            m_forAll = new QCheckBox(i18ndc(shellkitDomain, "@option:check", "Do this for all conflicts"), this);
        }
        if (m_classic) {
            setupClassic(layout);
        } else {
            setupModern(layout);
        }
    }

protected:
    void reject() override
    {
        finish(KIO::Result_Cancel);
    }

private:
    void finish(KIO::RenameDialog_Result result, const QUrl &newUrl = {})
    {
        if (m_done) {
            return;
        }
        m_done = true;
        if (decided) {
            decided(result, newUrl);
        }
        QDialog::reject();
    }

    bool forAll() const
    {
        return m_forAll && m_forAll->isChecked();
    }

    void replace()
    {
        finish(forAll() ? KIO::Result_OverwriteAll : KIO::Result_Overwrite);
    }

    void skip()
    {
        finish(forAll() ? KIO::Result_AutoSkip : KIO::Result_Skip);
    }

    void keepBoth()
    {
        if (forAll()) {
            finish(KIO::Result_AutoRename);
        } else {
            finish(KIO::Result_Rename, ConflictHandler::keepBothUrl(m_destination.url));
        }
    }

    QString headerText() const
    {
        const QString accent = palette().color(QPalette::Link).name();
        const auto place = [&](const QUrl &url) {
            return QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none\">%3</a>")
                .arg(url.toString().toHtmlEscaped(), accent, folderName(url).toHtmlEscaped());
        };
        const QString from = place(m_source.url.adjusted(QUrl::RemoveFilename));
        const QString to = place(m_destination.url.adjusted(QUrl::RemoveFilename));
        return m_moving ? i18ndcp(shellkitDomain, "@info", "Moving %1 item from %2 to %3", "Moving %1 items from %2 to %3", m_itemCount, from, to)
                        : i18ndcp(shellkitDomain, "@info", "Copying %1 item from %2 to %3", "Copying %1 items from %2 to %3", m_itemCount, from, to);
    }

    QLabel *headerLabel()
    {
        auto label = new QLabel(headerText(), this);
        label->setTextFormat(Qt::RichText);
        label->setWordWrap(true);
        connect(label, &QLabel::linkActivated, this, [](const QString &link) {
            QDesktopServices::openUrl(QUrl(link));
        });
        return label;
    }

    QLabel *bigLabel(const QString &text)
    {
        auto label = new QLabel(text, this);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setFixedWidth(ContentWidth);
        QFont font = label->font();
        font.setPointSizeF(font.pointSizeF() * 1.35);
        label->setFont(font);
        return label;
    }

    // Windows 11: three choices, the third one opens the comparison of both files.
    void setupModern(QVBoxLayout *layout)
    {
        setWindowTitle(i18ndc(shellkitDomain, "@title:window", "Replace or Skip Files"));

        auto choose = new QWidget(this);
        auto chooseLayout = new QVBoxLayout(choose);
        chooseLayout->setContentsMargins(0, 0, 0, 0);
        chooseLayout->setSpacing(8);
        auto header = headerLabel();
        header->setFixedWidth(ContentWidth);
        chooseLayout->addWidget(header);
        chooseLayout->addWidget(bigLabel(i18ndc(shellkitDomain, "@info", "The destination already has a file named \"%1\"", m_destination.url.fileName())));
        chooseLayout->addSpacing(4);
        const QPalette pal = palette();
        auto replaceButton = new CommandLink(glyphIcon(Glyph::Replace, pal), 20, i18ndc(shellkitDomain, "@action:button", "Replace the file in the destination"), false, choose);
        auto skipButton = new CommandLink(glyphIcon(Glyph::Skip, pal), 20, i18ndc(shellkitDomain, "@action:button", "Skip this file"), false, choose);
        auto compareButton = new CommandLink(glyphIcon(Glyph::Compare, pal), 20, i18ndc(shellkitDomain, "@action:button", "Compare info for both files"), false, choose);
        for (auto button : {replaceButton, skipButton, compareButton}) {
            chooseLayout->addWidget(button);
        }
        connect(replaceButton, &QToolButton::clicked, this, &ConflictDialog::replace);
        connect(skipButton, &QToolButton::clicked, this, &ConflictDialog::skip);
        layout->addWidget(choose);

        auto compare = new QWidget(this);
        auto compareLayout = new QVBoxLayout(compare);
        compareLayout->setContentsMargins(0, 0, 0, 0);
        compareLayout->setSpacing(8);
        compareLayout->addWidget(bigLabel(i18ndcp(shellkitDomain, "@title", "%1 file conflict", "%1 file conflicts", 1)));
        auto question = new QLabel(i18ndc(shellkitDomain, "@info", "Which files do you want to keep?"), compare);
        compareLayout->addWidget(question);
        auto hint = new QLabel(i18ndc(shellkitDomain, "@info", "If you select both versions, the copied file will have a number added to its name."), compare);
        hint->setWordWrap(true);
        hint->setFixedWidth(ContentWidth);
        compareLayout->addWidget(hint);

        auto grid = new QGridLayout;
        grid->setHorizontalSpacing(24);
        grid->setVerticalSpacing(6);
        auto keepSource = new QCheckBox(i18ndc(shellkitDomain, "@option:check %1 is a folder", "Files from %1", folderName(m_source.url.adjusted(QUrl::RemoveFilename))), compare);
        auto keepDestination =
            new QCheckBox(i18ndc(shellkitDomain, "@option:check %1 is a folder", "Files already in %1", folderName(m_destination.url.adjusted(QUrl::RemoveFilename))), compare);
        grid->addWidget(keepSource, 0, 0);
        grid->addWidget(keepDestination, 0, 1);
        auto name = new QLabel(m_destination.url.fileName(), compare);
        name->setTextFormat(Qt::PlainText);
        QFont bold = name->font();
        bold.setBold(true);
        name->setFont(bold);
        grid->addWidget(name, 1, 0, 1, 2);
        grid->addWidget(detailsWidget(m_source, false, false, compare), 2, 0);
        grid->addWidget(detailsWidget(m_destination, false, false, compare), 2, 1);
        compareLayout->addLayout(grid);

        auto buttons = new QHBoxLayout;
        buttons->addStretch();
        auto continueButton = new QPushButton(i18ndc(shellkitDomain, "@action:button", "Continue"), compare);
        continueButton->setEnabled(false);
        auto cancelButton = new QPushButton(i18ndc(shellkitDomain, "@action:button", "Cancel"), compare);
        buttons->addWidget(continueButton);
        buttons->addWidget(cancelButton);
        compareLayout->addSpacing(6);
        compareLayout->addLayout(buttons);
        layout->addWidget(compare);
        compare->hide();

        const auto updateContinue = [=]() {
            continueButton->setEnabled(keepSource->isChecked() || keepDestination->isChecked());
        };
        connect(keepSource, &QCheckBox::toggled, this, updateContinue);
        connect(keepDestination, &QCheckBox::toggled, this, updateContinue);
        connect(continueButton, &QPushButton::clicked, this, [=, this]() {
            if (keepSource->isChecked() && keepDestination->isChecked()) {
                keepBoth();
            } else if (keepSource->isChecked()) {
                replace();
            } else {
                skip();
            }
        });
        connect(cancelButton, &QPushButton::clicked, this, &ConflictDialog::reject);
        connect(compareButton, &QToolButton::clicked, this, [=, this]() {
            choose->hide();
            compare->show();
            if (m_forAll) {
                // Comparing decides for this file only.
                m_forAll->setChecked(false);
                m_forAll->hide();
            }
            keepSource->setFocus();
        });

        if (m_forAll) {
            layout->addWidget(m_forAll);
        }
        replaceButton->setFocus();
    }

    // Windows 7 "Copy File": the three choices show the file each one keeps.
    void setupClassic(QVBoxLayout *layout)
    {
        setWindowTitle(m_moving ? i18ndc(shellkitDomain, "@title:window", "Move File") : i18ndc(shellkitDomain, "@title:window", "Copy File"));
        auto intro = new QLabel(i18ndc(shellkitDomain, "@info", "There is already a file with the same name in this location."), this);
        intro->setFixedWidth(ContentWidth);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        auto instruction = bigLabel(i18ndc(shellkitDomain, "@info", "Click the file you want to keep"));
        QPalette palette = instruction->palette();
        palette.setColor(QPalette::WindowText, palette.color(QPalette::Link));
        instruction->setPalette(palette);
        layout->addWidget(instruction);

        const QString replaceTitle = m_moving ? i18ndc(shellkitDomain, "@action:button", "Move and Replace") : i18ndc(shellkitDomain, "@action:button", "Copy and Replace");
        auto replaceButton = new CommandLink(QIcon(), 0, replaceTitle, true, this);
        replaceButton->addDetail(new QLabel(m_moving ? i18ndc(shellkitDomain, "@info", "Replace the file in the destination folder with the file you are moving:")
                                                     : i18ndc(shellkitDomain, "@info", "Replace the file in the destination folder with the file you are copying:"),
                                            replaceButton));
        replaceButton->addDetail(detailsWidget(m_source, true, true, replaceButton));

        const QString skipTitle = m_moving ? i18ndc(shellkitDomain, "@action:button", "Don't move") : i18ndc(shellkitDomain, "@action:button", "Don't copy");
        auto skipButton = new CommandLink(QIcon(), 0, skipTitle, true, this);
        skipButton->addDetail(new QLabel(i18ndc(shellkitDomain, "@info", "No files will be changed. Leave this file in the destination folder:"), skipButton));
        skipButton->addDetail(detailsWidget(m_destination, true, true, skipButton));

        const QString bothTitle = m_moving ? i18ndc(shellkitDomain, "@action:button", "Move, but keep both files") : i18ndc(shellkitDomain, "@action:button", "Copy, but keep both files");
        auto bothButton = new CommandLink(QIcon(), 0, bothTitle, true, this);
        const QString newName = ConflictHandler::keepBothUrl(m_destination.url).fileName();
        auto bothDetail = new QLabel(m_moving ? i18ndc(shellkitDomain, "@info", "The file you are moving will be renamed \"%1\"", newName)
                                              : i18ndc(shellkitDomain, "@info", "The file you are copying will be renamed \"%1\"", newName),
                                     bothButton);
        bothDetail->setWordWrap(true);
        bothButton->addDetail(bothDetail);

        for (auto button : {replaceButton, skipButton, bothButton}) {
            button->setFixedWidth(ContentWidth);
            layout->addWidget(button);
        }
        connect(replaceButton, &QToolButton::clicked, this, &ConflictDialog::replace);
        connect(skipButton, &QToolButton::clicked, this, &ConflictDialog::skip);
        connect(bothButton, &QToolButton::clicked, this, &ConflictDialog::keepBoth);

        auto bottom = new QHBoxLayout;
        if (m_forAll) {
            bottom->addWidget(m_forAll);
        }
        bottom->addStretch();
        auto cancelButton = new QPushButton(i18ndc(shellkitDomain, "@action:button", "Cancel"), this);
        connect(cancelButton, &QPushButton::clicked, this, &ConflictDialog::reject);
        bottom->addWidget(cancelButton);
        layout->addLayout(bottom);
        replaceButton->setFocus();
    }

    const FileDetails m_source;
    const FileDetails m_destination;
    const bool m_classic;
    bool m_moving = false;
    int m_itemCount = 1;
    bool m_done = false;
    QCheckBox *m_forAll = nullptr;
};
}

ConflictHandler::ConflictHandler(QObject *parent)
    : KIO::WidgetsAskUserActionHandler(parent)
{
}

void ConflictHandler::askUserRename(KJob *job,
                                    const QString &title,
                                    const QUrl &src,
                                    const QUrl &dest,
                                    KIO::RenameDialog_Options options,
                                    KIO::filesize_t sizeSrc,
                                    KIO::filesize_t sizeDest,
                                    const QDateTime &ctimeSrc,
                                    const QDateTime &ctimeDest,
                                    const QDateTime &mtimeSrc,
                                    const QDateTime &mtimeDest)
{
    // Only "a file of this name is already there": folders (merging), a file onto itself and
    // resuming keep KIO's dialog.
    const bool fileConflict = (options & KIO::RenameDialog_Overwrite) && !(options & KIO::RenameDialog_OverwriteItself)
        && !(options & KIO::RenameDialog_Resume) && !(options & (KIO::RenameDialog_DestIsDirectory | KIO::RenameDialog_SourceIsDirectory));
    if (!fileConflict) {
        KIO::WidgetsAskUserActionHandler::askUserRename(job, title, src, dest, options, sizeSrc, sizeDest, ctimeSrc, ctimeDest, mtimeSrc, mtimeDest);
        return;
    }

    QWidget *window = KJobWidgets::window(job);
    auto dialog = new ConflictDialog(job, {src, sizeSrc, mtimeSrc}, {dest, sizeDest, mtimeDest}, options & KIO::RenameDialog_MultipleItems, window);
    QPointer<KJob> guard(job);
    dialog->decided = [this, guard](KIO::RenameDialog_Result result, const QUrl &newUrl) {
        if (guard) {
            Q_EMIT askUserRenameResult(result, newUrl, guard);
        }
    };
    // A job cancelled from the copy window takes its question with it.
    connect(job, &QObject::destroyed, dialog, &QWidget::close);
    // The question takes the place of the copy window until it is answered.
    CopyProgressDialog::setQuestionShown(job, true);
    connect(dialog, &QObject::destroyed, [job]() {
        CopyProgressDialog::setQuestionShown(job, false);
    });
    dialog->show();
    if (!window && dialog->windowHandle()) {
        // A window of its own (file operations service): bring it up over the File Explorer.
        KWindowSystem::activateWindow(dialog->windowHandle());
    }
}

QUrl ConflictHandler::keepBothUrl(const QUrl &dest)
{
    const QString name = dest.fileName();
    QString suffix = QMimeDatabase().suffixForFileName(name);
    if (suffix.isEmpty()) {
        const int dot = name.lastIndexOf(QLatin1Char('.'));
        if (dot > 0) {
            suffix = name.mid(dot + 1);
        }
    }
    const QString base = suffix.isEmpty() ? name : name.left(name.size() - suffix.size() - 1);
    const QUrl folder = dest.adjusted(QUrl::RemoveFilename);
    QUrl candidate;
    for (int n = 2; n < 10000; ++n) {
        const QString newName = suffix.isEmpty() ? QStringLiteral("%1 (%2)").arg(base).arg(n) : QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(suffix);
        candidate = folder;
        candidate.setPath(folder.path() + newName);
        // Elsewhere than on a local disk, a name that is taken comes back as a new question.
        if (!dest.isLocalFile() || !QFileInfo::exists(candidate.toLocalFile())) {
            break;
        }
    }
    return candidate;
}

#include "moc_conflictdialog.cpp"
