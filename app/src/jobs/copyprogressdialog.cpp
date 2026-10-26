/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "copyprogressdialog.h"

#include "dolphin_generalsettings.h"
#include "shellkit.h"

#include <KFormat>
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/DeleteOrTrashJob>
#include <KIO/JobTracker>
#include <KJobTrackerInterface>

#include <QDesktopServices>
#include <QLocale>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
// Short copies finish without a window, as in Windows.
constexpr int ShowDelay = 600;
// The layout fixes the dialog to its size hint, so the width comes from the content.
constexpr int ContentWidth = 420;

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

enum class Glyph {
    Cancel,
    Pause,
    Resume,
    ChevronUp,
    ChevronDown,
};

// Thin line glyphs in the text colour, as on the Windows dialog (the theme's icons are filled
// and window-close is red).
QIcon glyphIcon(Glyph glyph, const QPalette &palette)
{
    QIcon icon;
    for (const qreal scale : {1.0, 2.0}) {
        QPixmap pixmap(QSize(16, 16) * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette.color(QPalette::WindowText), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        switch (glyph) {
        case Glyph::Cancel:
            painter.drawLine(QPointF(4, 4), QPointF(12, 12));
            painter.drawLine(QPointF(12, 4), QPointF(4, 12));
            break;
        case Glyph::Pause:
            painter.drawLine(QPointF(5.5, 3.5), QPointF(5.5, 12.5));
            painter.drawLine(QPointF(10.5, 3.5), QPointF(10.5, 12.5));
            break;
        case Glyph::Resume:
            painter.drawPolygon(QPolygonF({QPointF(5, 3.5), QPointF(12.5, 8), QPointF(5, 12.5)}));
            break;
        case Glyph::ChevronUp:
            painter.drawPolyline(QPolygonF({QPointF(4, 10), QPointF(8, 6), QPointF(12, 10)}));
            break;
        case Glyph::ChevronDown:
            painter.drawPolyline(QPolygonF({QPointF(4, 6), QPointF(8, 10), QPointF(12, 6)}));
            break;
        }
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

QToolButton *glyphButton(Glyph glyph, const QString &toolTip, QWidget *parent)
{
    auto button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setIcon(glyphIcon(glyph, parent->palette()));
    button->setToolTip(toolTip);
    return button;
}

QString folderName(const QUrl &url)
{
    const QString name = url.adjusted(QUrl::StripTrailingSlash).fileName();
    return name.isEmpty() ? url.toDisplayString(QUrl::PreferLocalFile) : name;
}

/** A band across the dialog in a slightly different tone: the Windows footer and the Windows 7 banner. */
class Band : public QWidget
{
public:
    Band(Qt::Edge line, QWidget *parent)
        : QWidget(parent)
        , m_line(line)
    {
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const bool dark = isDark(palette());
        painter.fillRect(rect(), dark ? QColor(0, 0, 0, 40) : QColor(0, 0, 0, 8));
        const QColor line = dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 20);
        painter.fillRect(m_line == Qt::TopEdge ? QRect(0, 0, width(), 1) : QRect(0, height() - 1, width(), 1), line);
    }

private:
    const Qt::Edge m_line;
};
}

/**
 * The Windows speed graph: speed over the progress so far, the copied part tinted, and a line
 * with the current speed.
 */
class SpeedGraph : public QWidget
{
public:
    explicit SpeedGraph(QWidget *parent)
        : QWidget(parent)
    {
        setFixedHeight(96);
    }

    void addSample(qreal progress, qreal speed)
    {
        progress = std::clamp(progress, 0.0, 1.0);
        if (!m_samples.isEmpty() && progress <= m_samples.last().x()) {
            m_samples.last().setY(speed);
        } else {
            m_samples.append(QPointF(progress, speed));
        }
        m_progress = progress;
        m_maxSpeed = std::max(m_maxSpeed, speed);
        update();
    }

    void setProgress(qreal progress)
    {
        m_progress = std::clamp(progress, 0.0, 1.0);
        update();
    }

    void setSpeedText(const QString &text)
    {
        m_speedText = text;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QRectF area = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const QColor text = palette().color(QPalette::WindowText);
        const QColor accent = palette().color(QPalette::Highlight);
        const auto withAlpha = [](QColor color, qreal alpha) {
            color.setAlphaF(alpha);
            return color;
        };

        // Grid: tenths across, quarters up.
        painter.setPen(withAlpha(text, 0.07));
        for (int i = 1; i < 10; ++i) {
            const qreal x = area.left() + area.width() * i / 10;
            painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        }
        for (int i = 1; i < 4; ++i) {
            const qreal y = area.top() + area.height() * i / 4;
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        }

        const qreal done = area.width() * m_progress;
        painter.fillRect(QRectF(area.left(), area.top(), done, area.height()), withAlpha(accent, 0.16));

        const qreal scale = m_maxSpeed > 0 ? m_maxSpeed * 1.25 : 1;
        const auto pointAt = [&](const QPointF &sample) {
            return QPointF(area.left() + area.width() * sample.x(), area.bottom() - area.height() * sample.y() / scale);
        };
        if (!m_samples.isEmpty()) {
            QPainterPath curve;
            curve.moveTo(area.left(), area.bottom());
            curve.lineTo(area.left(), pointAt(m_samples.first()).y());
            for (const QPointF &sample : m_samples) {
                curve.lineTo(pointAt(sample));
            }
            curve.lineTo(area.left() + done, area.bottom());
            curve.closeSubpath();
            painter.setRenderHint(QPainter::Antialiasing);
            painter.fillPath(curve, withAlpha(accent, 0.55));
            painter.setRenderHint(QPainter::Antialiasing, false);

            // The current speed across the whole graph, its value above the line at the right.
            const qreal y = std::round(pointAt(m_samples.last()).y()) + 0.5;
            painter.setPen(withAlpha(text, 0.85));
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            if (!m_speedText.isEmpty()) {
                const QFontMetrics metrics(font());
                const qreal textTop = y - metrics.height() - 2 < area.top() ? y + 2 : y - metrics.height() - 2;
                painter.drawText(QRectF(area.left(), textTop, area.width() - 6, metrics.height()), Qt::AlignRight | Qt::AlignVCenter, m_speedText);
            }
        }

        painter.setPen(withAlpha(text, 0.25));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(area);
    }

private:
    QList<QPointF> m_samples;
    qreal m_progress = 0;
    qreal m_maxSpeed = 0;
    QString m_speedText;
};

void CopyProgressDialog::track(KIO::CopyJob *job, QWidget *window)
{
    trackJob(job, window);
}

void CopyProgressDialog::trackDeletion(KIO::DeleteOrTrashJob *job, QWidget *window)
{
    if (!job) {
        return;
    }
    QPointer<QWidget> parent(window);
    // The trash (copy) or delete job is a subjob created after the confirmation; it reports to
    // the Plasma notifications, so take it off the tracker and show this window instead.
    QObject::connect(job, &KIO::DeleteOrTrashJob::started, job, [job, parent]() {
        const auto subjobs = job->findChildren<KIO::Job *>(Qt::FindDirectChildrenOnly);
        for (KIO::Job *subjob : subjobs) {
            KIO::getJobTracker()->unregisterJob(subjob);
            trackJob(subjob, parent);
        }
    });
}

void CopyProgressDialog::trackJob(KIO::Job *job, QWidget *window)
{
    if (!job) {
        return;
    }
    QPointer<KIO::Job> guard(job);
    QPointer<QWidget> parent(window);
    QTimer::singleShot(ShowDelay, job, [guard, parent]() {
        if (guard && parent) {
            auto dialog = new CopyProgressDialog(guard, parent);
            dialog->show();
        }
    });
}

CopyProgressDialog::CopyProgressDialog(KIO::Job *job, QWidget *parent)
    : QDialog(parent)
    , m_job(job)
    // The Windows 7 look always uses the classic layout.
    , m_classic(GeneralSettings::classicCopyDialog() || shellkitWin7())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);

    const auto copyJob = qobject_cast<KIO::CopyJob *>(job);
    const auto deleteJob = qobject_cast<KIO::DeleteJob *>(job);
    const QList<QUrl> sources = copyJob ? copyJob->srcUrls() : deleteJob ? deleteJob->urls() : QList<QUrl>();
    m_itemCount = sources.count();
    if (!sources.isEmpty()) {
        m_from = sources.first().adjusted(QUrl::StripTrailingSlash | QUrl::RemoveFilename);
    }
    if (copyJob && copyJob->destUrl().scheme() != QLatin1String("trash")) {
        m_to = copyJob->destUrl();
        m_moving = copyJob->operationMode() == KIO::CopyJob::Move;
    }

    m_header = new QLabel(this);
    m_header->setWordWrap(true);
    m_header->setTextFormat(Qt::RichText);
    connect(m_header, &QLabel::linkActivated, this, [](const QString &link) {
        QDesktopServices::openUrl(QUrl(link));
    });

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);

    m_detailsButton = new QToolButton(this);
    m_detailsButton->setAutoRaise(true);
    m_detailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(m_detailsButton, &QToolButton::clicked, this, [this]() {
        const bool shown = !m_details->isVisible();
        setDetailsShown(shown);
        KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("CopyDialog"));
        group.writeEntry("Details", shown);
    });

    m_details = new QWidget(this);
    m_name = new QLabel(m_details);
    m_name->setTextFormat(Qt::PlainText);
    m_timeLeft = new QLabel(m_details);
    m_itemsLeft = new QLabel(m_details);

    if (m_classic) {
        setupClassic();
    } else {
        setupModern();
    }

    connect(job, &KJob::percentChanged, this, &CopyProgressDialog::updateProgress);
    connect(job, &KJob::totalAmountChanged, this, [this](KJob *, KJob::Unit unit, qulonglong amount) {
        if (unit == KJob::Bytes) {
            m_totalBytes = amount;
        } else if (unit == KJob::Files) {
            m_totalFiles = amount;
        }
        updateDetails();
    });
    connect(job, &KJob::processedAmountChanged, this, [this](KJob *, KJob::Unit unit, qulonglong amount) {
        if (unit == KJob::Bytes) {
            m_processedBytes = amount;
        } else if (unit == KJob::Files) {
            m_processedFiles = amount;
        }
        updateDetails();
    });
    connect(job, &KJob::speed, this, [this](KJob *, unsigned long bytesPerSecond) {
        m_bytesPerSecond = bytesPerSecond;
        if (m_graph && m_totalBytes > 0) {
            m_graph->addSample(qreal(m_processedBytes) / m_totalBytes, bytesPerSecond);
        }
        updateDetails();
    });
    const auto current = [this](KIO::Job *, const QUrl &source) {
        m_name->setText(source.fileName());
    };
    if (copyJob) {
        connect(copyJob, &KIO::CopyJob::copying, this, current);
        connect(copyJob, &KIO::CopyJob::moving, this, current);
    } else if (deleteJob) {
        connect(deleteJob, &KIO::DeleteJob::deleting, this, current);
    }
    connect(job, &KJob::suspended, this, &CopyProgressDialog::updateProgress);
    connect(job, &KJob::resumed, this, &CopyProgressDialog::updateProgress);
    connect(job, &KJob::finished, this, &QDialog::close);

    m_totalBytes = job->totalAmount(KJob::Bytes);
    m_processedBytes = job->processedAmount(KJob::Bytes);
    m_totalFiles = job->totalAmount(KJob::Files);
    m_processedFiles = job->processedAmount(KJob::Files);

    updateProgress();
    updateDetails();
    // Windows remembers whether the details were open last time.
    setDetailsShown(KConfigGroup(KSharedConfig::openConfig(), QStringLiteral("CopyDialog")).readEntry("Details", true));
}

void CopyProgressDialog::setupModern()
{
    // Windows 11: the header, "45% complete" with pause and cancel beside it, the speed graph
    // (or a thin bar when the details are hidden), the details as plain lines and a footer.
    m_header->setFixedWidth(ContentWidth);

    m_percent = new QLabel(this);
    QFont percentFont = m_percent->font();
    percentFont.setPointSizeF(percentFont.pointSizeF() * 1.4);
    m_percent->setFont(percentFont);

    m_pauseButton = glyphButton(Glyph::Pause, i18ndc(shellkitDomain, "@action:button", "Pause"), this);
    connect(m_pauseButton, &QToolButton::clicked, this, [this]() {
        if (!m_job) {
            return;
        }
        if (m_job->isSuspended()) {
            m_job->resume();
        } else {
            m_job->suspend();
        }
    });
    auto cancelButton = glyphButton(Glyph::Cancel, i18ndc(shellkitDomain, "@action:button", "Cancel"), this);
    connect(cancelButton, &QToolButton::clicked, this, [this]() {
        if (m_job) {
            m_job->kill(KJob::EmitResult);
        }
    });

    auto percentRow = new QHBoxLayout;
    percentRow->addWidget(m_percent, 1);
    percentRow->addWidget(m_pauseButton);
    percentRow->addWidget(cancelButton);

    m_progressBar->setFixedHeight(4);

    m_graph = new SpeedGraph(m_details);
    auto detailsLayout = new QVBoxLayout(m_details);
    detailsLayout->setContentsMargins(0, 4, 0, 0);
    detailsLayout->setSpacing(4);
    detailsLayout->addWidget(m_graph);
    detailsLayout->addSpacing(6);
    const auto addLine = [this, detailsLayout](const QString &label, QLabel *value) {
        auto line = new QHBoxLayout;
        line->setSpacing(4);
        line->addWidget(new QLabel(label, m_details));
        line->addWidget(value);
        line->addStretch(1);
        detailsLayout->addLayout(line);
    };
    addLine(i18ndc(shellkitDomain, "@label Name of the file being copied", "Name:"), m_name);
    addLine(i18ndc(shellkitDomain, "@label", "Time remaining:"), m_timeLeft);
    addLine(i18ndc(shellkitDomain, "@label", "Items remaining:"), m_itemsLeft);
    m_name->setMaximumWidth(ContentWidth - 60);

    auto content = new QVBoxLayout;
    content->setContentsMargins(20, 16, 20, 16);
    content->setSpacing(8);
    content->addWidget(m_header);
    content->addLayout(percentRow);
    content->addWidget(m_progressBar);
    content->addWidget(m_details);

    auto footer = new Band(Qt::TopEdge, this);
    auto footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(14, 8, 14, 8);
    footerLayout->addWidget(m_detailsButton);
    footerLayout->addStretch(1);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(content);
    layout->addWidget(footer);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    connect(m_job, &KJob::suspended, this, [this]() {
        m_pauseButton->setIcon(glyphIcon(Glyph::Resume, palette()));
        m_pauseButton->setToolTip(i18ndc(shellkitDomain, "@action:button", "Resume"));
    });
    connect(m_job, &KJob::resumed, this, [this]() {
        m_pauseButton->setIcon(glyphIcon(Glyph::Pause, palette()));
        m_pauseButton->setToolTip(i18ndc(shellkitDomain, "@action:button", "Pause"));
    });
}

void CopyProgressDialog::setupClassic()
{
    // Windows 7: a banner, the details as a table, a progress bar, and Cancel in the footer.
    auto banner = new Band(Qt::BottomEdge, this);
    QFont bannerFont = m_header->font();
    bannerFont.setPointSizeF(bannerFont.pointSizeF() * 1.15);
    m_header->setFont(bannerFont);
    QPalette bannerPalette = m_header->palette();
    bannerPalette.setColor(QPalette::WindowText, bannerPalette.color(QPalette::Link));
    m_header->setPalette(bannerPalette);
    m_header->setFixedWidth(ContentWidth);
    auto bannerLayout = new QVBoxLayout(banner);
    bannerLayout->setContentsMargins(20, 12, 20, 12);
    bannerLayout->addWidget(m_header);

    m_timeLine = new QLabel(this);

    m_fromLabel = new QLabel(m_details);
    m_toLabel = new QLabel(m_details);
    m_speed = new QLabel(m_details);
    auto grid = new QGridLayout(m_details);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(6);
    grid->setColumnStretch(1, 1);
    const auto addRow = [this, grid](const QString &label, QLabel *value) {
        const int row = grid->rowCount();
        grid->addWidget(new QLabel(label, m_details), row, 0);
        grid->addWidget(value, row, 1);
    };
    addRow(i18ndc(shellkitDomain, "@label Name of the file being copied", "Name:"), m_name);
    addRow(i18ndc(shellkitDomain, "@label Folder the files are copied from", "From:"), m_fromLabel);
    if (m_to.isValid()) {
        addRow(i18ndc(shellkitDomain, "@label Folder the files are copied to", "To:"), m_toLabel);
    }
    addRow(i18ndc(shellkitDomain, "@label", "Time remaining:"), m_timeLeft);
    addRow(i18ndc(shellkitDomain, "@label", "Items remaining:"), m_itemsLeft);
    addRow(i18ndc(shellkitDomain, "@label", "Speed:"), m_speed);
    m_name->setMaximumWidth(ContentWidth - 120);

    const auto place = [](const QUrl &url) {
        return QStringLiteral("<b>%1</b> (%2)").arg(folderName(url).toHtmlEscaped(), url.toDisplayString(QUrl::PreferLocalFile | QUrl::StripTrailingSlash).toHtmlEscaped());
    };
    m_fromLabel->setText(place(m_from));
    m_toLabel->setText(place(m_to));

    m_progressBar->setFixedHeight(16);

    auto content = new QVBoxLayout;
    content->setContentsMargins(20, 14, 20, 16);
    content->setSpacing(10);
    content->addWidget(m_timeLine);
    content->addWidget(m_details);
    content->addWidget(m_progressBar);

    auto cancelButton = new QPushButton(i18ndc(shellkitDomain, "@action:button", "Cancel"), this);
    connect(cancelButton, &QPushButton::clicked, this, [this]() {
        if (m_job) {
            m_job->kill(KJob::EmitResult);
        }
    });
    auto footer = new Band(Qt::TopEdge, this);
    auto footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(14, 8, 14, 8);
    footerLayout->addWidget(m_detailsButton);
    footerLayout->addStretch(1);
    footerLayout->addWidget(cancelButton);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(banner);
    layout->addLayout(content);
    layout->addWidget(footer);
    layout->setSizeConstraint(QLayout::SetFixedSize);
}

void CopyProgressDialog::updateHeader()
{
    const bool deleting = !m_to.isValid();
    // The job may be gone already: it deletes itself when it finishes.

    if (m_classic && m_details->isVisible()) {
        // "Copying 1 item (4.16 GB)"; the table below names the folders.
        const QString size = shellkitStatusSize(m_totalBytes);
        if (deleting) {
            m_header->setText(i18ndcp(shellkitDomain, "@info %2 is a size", "Deleting %1 item (%2)", "Deleting %1 items (%2)", m_itemCount, size));
        } else if (m_moving) {
            m_header->setText(i18ndcp(shellkitDomain, "@info %2 is a size", "Moving %1 item (%2)", "Moving %1 items (%2)", m_itemCount, size));
        } else {
            m_header->setText(i18ndcp(shellkitDomain, "@info %2 is a size", "Copying %1 item (%2)", "Copying %1 items (%2)", m_itemCount, size));
        }
        return;
    }

    // Folder names as links in the accent colour (Windows 11) or bold (Windows 7).
    const auto place = [this](const QUrl &url) {
        const QString name = folderName(url).toHtmlEscaped();
        if (m_classic) {
            return QStringLiteral("<b>%1</b>").arg(name);
        }
        const QString accent = palette().color(QPalette::Link).name();
        return QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none\">%3</a>").arg(url.toString().toHtmlEscaped(), accent, name);
    };
    const QString from = place(m_from);
    if (deleting) {
        m_header->setText(i18ndcp(shellkitDomain, "@info", "Deleting %1 item from %2", "Deleting %1 items from %2", m_itemCount, from));
    } else if (m_moving) {
        m_header->setText(i18ndcp(shellkitDomain, "@info", "Moving %1 item from %2 to %3", "Moving %1 items from %2 to %3", m_itemCount, from, place(m_to)));
    } else {
        m_header->setText(i18ndcp(shellkitDomain, "@info", "Copying %1 item from %2 to %3", "Copying %1 items from %2 to %3", m_itemCount, from, place(m_to)));
    }
}

void CopyProgressDialog::updateProgress()
{
    if (!m_job) {
        return;
    }
    const int percent = int(m_job->percent());
    m_progressBar->setValue(percent);
    if (m_graph) {
        m_graph->setProgress(percent / 100.0);
    }
    const QString text = m_job->isSuspended() ? i18ndc(shellkitDomain, "@info %1 is a number", "%1% complete, paused", percent)
                                              : i18ndc(shellkitDomain, "@info %1 is a number", "%1% complete", percent);
    if (m_percent) {
        m_percent->setText(text);
    }
    // Windows 7 names the time left in the title once it is known.
    if (!m_classic || m_bytesPerSecond == 0) {
        setWindowTitle(text);
    }
}

void CopyProgressDialog::updateDetails()
{
    const KFormat format;
    const QString speed = i18ndc(shellkitDomain, "@info Transfer speed", "%1/s", shellkitStatusSize(m_bytesPerSecond));
    if (m_graph) {
        m_graph->setSpeedText(m_bytesPerSecond > 0 ? i18ndc(shellkitDomain, "@info Transfer speed in the graph", "Speed: %1", speed) : QString());
    }
    if (m_speed) {
        m_speed->setText(m_bytesPerSecond > 0 ? speed : QString());
    }

    if (m_bytesPerSecond > 0 && m_totalBytes > m_processedBytes) {
        const qulonglong seconds = (m_totalBytes - m_processedBytes) / m_bytesPerSecond + 1;
        const QString duration = format.formatSpelloutDuration(seconds * 1000);
        m_timeLeft->setText(i18ndc(shellkitDomain, "@info Time remaining", "About %1", duration));
        if (m_classic) {
            const QString remaining = i18ndc(shellkitDomain, "@info %1 is a duration", "%1 remaining", duration);
            m_timeLine->setText(m_timeLeft->text());
            setWindowTitle(remaining);
        }
    } else {
        m_timeLeft->setText(i18ndc(shellkitDomain, "@info Time remaining", "Calculating..."));
        if (m_timeLine) {
            m_timeLine->setText(m_timeLeft->text());
        }
    }

    const qulonglong filesLeft = m_totalFiles > m_processedFiles ? m_totalFiles - m_processedFiles : 0;
    const qulonglong bytesLeft = m_totalBytes > m_processedBytes ? m_totalBytes - m_processedBytes : 0;
    m_itemsLeft->setText(
        i18ndc(shellkitDomain, "@info %1 is a number of files, %2 their size", "%1 (%2)", QLocale().toString(filesLeft), shellkitStatusSize(bytesLeft)));

    if (m_classic && m_details->isVisible()) {
        updateHeader();
    }
}

void CopyProgressDialog::setDetailsShown(bool shown)
{
    m_details->setVisible(shown);
    if (m_graph) {
        // The graph shows the progress, so the bar only stands in for it when collapsed.
        m_progressBar->setVisible(!shown);
    }
    if (m_timeLine) {
        m_timeLine->setVisible(!shown);
    }
    m_detailsButton->setIcon(glyphIcon(shown ? Glyph::ChevronUp : Glyph::ChevronDown, palette()));
    m_detailsButton->setText(shown ? i18ndc(shellkitDomain, "@action:button", "Fewer details") : i18ndc(shellkitDomain, "@action:button", "More details"));
    updateHeader();
}

#include "moc_copyprogressdialog.cpp"
