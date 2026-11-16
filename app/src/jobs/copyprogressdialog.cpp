/*
 * SPDX-FileCopyrightText: 2026 shellkit
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "copyprogressdialog.h"
#include "compressedfolders.h"

#include "dolphin_generalsettings.h"
#include "shellkit.h"

#include <KFormat>
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/DeleteOrTrashJob>
#include <KIO/JobTracker>
#include <KJobTrackerInterface>
#include <KConfigGroup>
#include <KSharedConfig>
#include <KWindowSystem>

#include <QCloseEvent>
#include <QDesktopServices>
#include <QHash>
#include <QLocale>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

namespace
{
// Short copies finish without a window, as in Windows.
constexpr int ShowDelay = 600;
// Jobs that are asking the user something (ConflictHandler).
QSet<KJob *> s_asking;
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


namespace
{
/** The line between two operations in one window. */
class SeparatorLine : public QWidget
{
public:
    explicit SeparatorLine(QWidget *parent)
        : QWidget(parent)
    {
        setFixedHeight(1);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), isDark(palette()) ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 20));
    }
};

void setDetailsButton(QToolButton *button, bool shown)
{
    button->setIcon(glyphIcon(shown ? Glyph::ChevronUp : Glyph::ChevronDown, button->palette()));
    button->setText(shown ? i18ndc(shellkitDomain, "@action:button", "Fewer details") : i18ndc(shellkitDomain, "@action:button", "More details"));
}

QToolButton *detailsButton(QWidget *parent)
{
    auto button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    return button;
}

KConfigGroup copyDialogConfig()
{
    return KConfigGroup(KSharedConfig::openConfig(QStringLiteral("shellkit-filesrc")), QStringLiteral("CopyDialog"));
}
}

/** One operation in the copy dialog: its header, progress and details. */
class CopyProgressItem : public QWidget
{
public:
    CopyProgressItem(KIO::Job *job, bool classic, QWidget *parent);

    KIO::Job *job() const
    {
        return m_job;
    }

    bool isRunning() const
    {
        return m_job && !m_jobDone;
    }

    QString title() const
    {
        return m_title;
    }

    void setSeparatorShown(bool shown)
    {
        m_separator->setVisible(shown);
    }

    void setDetailsShown(bool shown);

    // Set by the dialog.
    std::function<void(CopyProgressItem *)> finished;
    std::function<void()> titleChanged;

private:
    void setupModern();
    void setupClassic();
    void updateHeader();
    void updateProgress();
    void updateDetails();

    void setTitle(const QString &title)
    {
        if (title != m_title) {
            m_title = title;
            if (titleChanged) {
                titleChanged();
            }
        }
    }

    QPointer<KIO::Job> m_job;
    const bool m_classic;
    bool m_jobDone = false;
    bool m_detailsShown = true;
    QString m_title;
    QUrl m_from;
    QUrl m_to;
    int m_itemCount = 0;
    bool m_moving = false;
    bool m_compressing = false;

    QWidget *m_separator = nullptr;
    QLabel *m_header = nullptr;
    QLabel *m_percent = nullptr;
    QLabel *m_timeLine = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_detailsButton = nullptr;
    QWidget *m_details = nullptr;
    SpeedGraph *m_graph = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_fromLabel = nullptr;
    QLabel *m_toLabel = nullptr;
    QLabel *m_speed = nullptr;
    QLabel *m_timeLeft = nullptr;
    QLabel *m_itemsLeft = nullptr;

    qulonglong m_totalBytes = 0;
    qulonglong m_processedBytes = 0;
    qulonglong m_totalFiles = 0;
    qulonglong m_processedFiles = 0;
    unsigned long m_bytesPerSecond = 0;
};

namespace
{
// The section of each job, and the dialog of each File Explorer window (null: the one of the
// file operations service).
QHash<KJob *, QPointer<CopyProgressItem>> s_items;
QHash<QWidget *, QPointer<CopyProgressDialog>> s_windows;
}

void CopyProgressDialog::track(KIO::CopyJob *job, QWidget *window)
{
    trackJob(job, window);
}

void CopyProgressDialog::trackCompression(KIO::Job *job, QWidget *window)
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
    // Without a window (in the file operations service) the dialog is a window of its own.
    const bool ownWindow = !window;
    // A job that is already done (it is deleted a moment later) gets no section.
    auto done = std::make_shared<bool>(false);
    QObject::connect(job, &KJob::finished, job, [done]() {
        *done = true;
    });
    QTimer::singleShot(ShowDelay, job, [guard, parent, ownWindow, done]() {
        if (guard && !*done && (parent || ownWindow)) {
            dialogFor(parent)->addJob(guard);
        }
    });
}

CopyProgressDialog *CopyProgressDialog::dialogFor(QWidget *window)
{
    QPointer<CopyProgressDialog> &dialog = s_windows[window];
    if (!dialog || dialog->m_closing) {
        dialog = new CopyProgressDialog(window);
    }
    return dialog;
}

void CopyProgressDialog::setQuestionShown(KJob *job, bool shown)
{
    if (shown) {
        s_asking.insert(job);
    } else {
        s_asking.remove(job);
    }
    if (CopyProgressItem *item = s_items.value(job)) {
        item->setVisible(!shown);
        if (auto dialog = qobject_cast<CopyProgressDialog *>(item->window())) {
            dialog->updateWindow();
        }
    }
}

CopyProgressDialog::CopyProgressDialog(QWidget *parent)
    : QDialog(parent)
    , m_owner(parent)
    // The Windows 7 look always uses the classic layout.
    , m_classic(GeneralSettings::classicCopyDialog() || shellkitWin7())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    m_detailsShown = copyDialogConfig().readEntry("Details", true);

    m_itemLayout = new QVBoxLayout;
    m_itemLayout->setContentsMargins(0, 0, 0, 0);
    m_itemLayout->setSpacing(0);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(m_itemLayout);
    if (!m_classic) {
        // Windows 11: one "Fewer details" for all operations; Windows 7 has one per operation.
        m_detailsButton = detailsButton(this);
        setDetailsButton(m_detailsButton, m_detailsShown);
        connect(m_detailsButton, &QToolButton::clicked, this, [this]() {
            setDetailsShown(!m_detailsShown);
        });
        auto footer = new Band(Qt::TopEdge, this);
        auto footerLayout = new QHBoxLayout(footer);
        footerLayout->setContentsMargins(14, 8, 14, 8);
        footerLayout->addWidget(m_detailsButton);
        footerLayout->addStretch(1);
        layout->addWidget(footer);
    }
    layout->setSizeConstraint(QLayout::SetFixedSize);
}

void CopyProgressDialog::addJob(KIO::Job *job)
{
    auto item = new CopyProgressItem(job, m_classic, this);
    if (!m_classic) {
        item->setDetailsShown(m_detailsShown);
    }
    // While the job asks the user something, the question stands in for its section.
    item->setVisible(!s_asking.contains(job));
    s_items.insert(job, item);
    connect(item, &QObject::destroyed, this, [job]() {
        s_items.remove(job);
    });
    item->finished = [this](CopyProgressItem *done) {
        removeItem(done);
    };
    item->titleChanged = [this]() {
        updateWindow();
    };
    m_items.append(item);
    m_itemLayout->addWidget(item);
    updateWindow();
}

void CopyProgressDialog::removeItem(CopyProgressItem *item)
{
    if (!m_items.removeOne(item)) {
        return;
    }
    m_itemLayout->removeWidget(item);
    item->hide();
    item->deleteLater();
    if (m_items.isEmpty()) {
        if (!m_closing) {
            m_closing = true;
            close();
        }
        return;
    }
    updateWindow();
}

void CopyProgressDialog::updateWindow()
{
    if (m_closing) {
        return;
    }
    QList<CopyProgressItem *> shown;
    for (CopyProgressItem *item : std::as_const(m_items)) {
        if (!item->isHidden()) {
            shown.append(item);
        }
    }
    for (CopyProgressItem *item : std::as_const(shown)) {
        item->setSeparatorShown(item != shown.first());
    }
    if (shown.isEmpty()) {
        hide();
        return;
    }
    if (shown.count() == 1) {
        setWindowTitle(shown.first()->title());
    } else {
        setWindowTitle(i18ndcp(shellkitDomain, "@title:window", "%1 operation running", "%1 operations running", shown.count()));
    }
    if (!isVisible()) {
        showWindow();
    }
}

void CopyProgressDialog::setDetailsShown(bool shown)
{
    m_detailsShown = shown;
    copyDialogConfig().writeEntry("Details", shown);
    if (m_detailsButton) {
        setDetailsButton(m_detailsButton, shown);
    }
    for (CopyProgressItem *item : std::as_const(m_items)) {
        item->setDetailsShown(shown);
    }
}

void CopyProgressDialog::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    // Copy the list: each killed job removes its section.
    const QList<CopyProgressItem *> items = m_items;
    for (CopyProgressItem *item : items) {
        if (item->isRunning()) {
            item->job()->kill(KJob::EmitResult);
        }
    }
    // Not QDialog::closeEvent: it calls reject(), which closes again, and the nested close
    // leaves the event ignored and an empty window behind.
    event->accept();
}

void CopyProgressDialog::reject()
{
    close();
}

void CopyProgressDialog::showWindow()
{
    show();
    if (!m_owner && windowHandle()) {
        KWindowSystem::activateWindow(windowHandle());
    }
}

CopyProgressItem::CopyProgressItem(KIO::Job *job, bool classic, QWidget *parent)
    : QWidget(parent)
    , m_job(job)
    , m_classic(classic)
{
    m_separator = new SeparatorLine(this);


    const auto copyJob = qobject_cast<KIO::CopyJob *>(job);
    const auto deleteJob = qobject_cast<KIO::DeleteJob *>(job);
    const auto compressJob = qobject_cast<CompressJob *>(job);
    const QList<QUrl> sources = copyJob ? copyJob->srcUrls()
        : deleteJob                     ? deleteJob->urls()
        : compressJob                   ? compressJob->sources()
                                        : QList<QUrl>();
    m_itemCount = sources.count();
    if (!sources.isEmpty()) {
        m_from = sources.first().adjusted(QUrl::StripTrailingSlash | QUrl::RemoveFilename);
    }
    if (copyJob && copyJob->destUrl().scheme() != QLatin1String("trash")) {
        m_to = copyJob->destUrl();
        m_moving = copyJob->operationMode() == KIO::CopyJob::Move;
    }
    if (compressJob) {
        m_to = compressJob->archive();
        m_compressing = true;
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

    m_detailsButton = detailsButton(this);
    connect(m_detailsButton, &QToolButton::clicked, this, [this]() {
        const bool shown = !m_detailsShown;
        setDetailsShown(shown);
        copyDialogConfig().writeEntry("Details", shown);
    });

    m_details = new QWidget(this);
    m_name = new QLabel(m_details);
    m_name->setTextFormat(Qt::PlainText);
    // The dialog opens after a moment, often after the job announced its (only) file.
    if (!sources.isEmpty()) {
        m_name->setText(sources.first().fileName());
    }
    m_timeLeft = new QLabel(m_details);
    m_itemsLeft = new QLabel(m_details);

    if (m_classic) {
        setupClassic();
    } else {
        setupModern();
    }

    connect(job, &KJob::percentChanged, this, &CopyProgressItem::updateProgress);
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
    } else if (compressJob) {
        connect(compressJob, &CompressJob::compressing, this, current);
    }
    connect(job, &KJob::suspended, this, &CopyProgressItem::updateProgress);
    connect(job, &KJob::resumed, this, &CopyProgressItem::updateProgress);
    connect(job, &KJob::finished, this, [this]() {
        m_jobDone = true;
        if (finished) {
            finished(this);
        }
    });

    m_totalBytes = job->totalAmount(KJob::Bytes);
    m_processedBytes = job->processedAmount(KJob::Bytes);
    m_totalFiles = job->totalAmount(KJob::Files);
    m_processedFiles = job->processedAmount(KJob::Files);

    updateProgress();
    updateDetails();
    // Windows remembers whether the details were open last time.
    setDetailsShown(copyDialogConfig().readEntry("Details", true));
}


void CopyProgressItem::setupModern()
{
    // Windows 11: the header, "45% complete" with pause and cancel beside it, the speed graph
    // (or a thin bar when the details are hidden) and the details as plain lines.
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

    // The window has one "Fewer details" button for all its sections.
    delete m_detailsButton;
    m_detailsButton = nullptr;

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_separator);
    layout->addLayout(content);
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

void CopyProgressItem::setupClassic()
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
    layout->addWidget(m_separator);
    layout->addWidget(banner);
    layout->addLayout(content);
    layout->addWidget(footer);
    layout->setSizeConstraint(QLayout::SetFixedSize);
}

void CopyProgressItem::updateHeader()
{
    const bool deleting = !m_to.isValid();
    // The job may be gone already: it deletes itself when it finishes.

    if (m_classic && m_detailsShown) {
        // "Copying 1 item (4.16 GB)"; the table below names the folders.
        const QString size = shellkitStatusSize(m_totalBytes);
        if (deleting) {
            m_header->setText(i18ndcp(shellkitDomain, "@info %2 is a size", "Deleting %1 item (%2)", "Deleting %1 items (%2)", m_itemCount, size));
        } else if (m_compressing) {
            m_header->setText(i18ndcp(shellkitDomain, "@info %2 is a size", "Compressing %1 item (%2)", "Compressing %1 items (%2)", m_itemCount, size));
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
    } else if (m_compressing) {
        m_header->setText(i18ndcp(shellkitDomain, "@info", "Compressing %1 item from %2 to %3", "Compressing %1 items from %2 to %3", m_itemCount, from, place(m_to)));
    } else if (m_moving) {
        m_header->setText(i18ndcp(shellkitDomain, "@info", "Moving %1 item from %2 to %3", "Moving %1 items from %2 to %3", m_itemCount, from, place(m_to)));
    } else {
        m_header->setText(i18ndcp(shellkitDomain, "@info", "Copying %1 item from %2 to %3", "Copying %1 items from %2 to %3", m_itemCount, from, place(m_to)));
    }
}

void CopyProgressItem::updateProgress()
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
        setTitle(text);
    }
}

void CopyProgressItem::updateDetails()
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
            setTitle(remaining);
        }
    } else {
        m_timeLeft->setText(i18ndc(shellkitDomain, "@info Time remaining", "Calculating..."));
        if (m_timeLine) {
            m_timeLine->setText(m_timeLeft->text());
        }
    }

    qulonglong filesLeft = m_totalFiles > m_processedFiles ? m_totalFiles - m_processedFiles : 0;
    const qulonglong bytesLeft = m_totalBytes > m_processedBytes ? m_totalBytes - m_processedBytes : 0;
    // The file being copied still counts, as in Windows ("1 (650 MB)").
    if (filesLeft == 0 && bytesLeft > 0) {
        filesLeft = 1;
    }
    m_itemsLeft->setText(
        i18ndc(shellkitDomain, "@info %1 is a number of files, %2 their size", "%1 (%2)", QLocale().toString(filesLeft), shellkitStatusSize(bytesLeft)));

    if (m_classic && m_detailsShown) {
        updateHeader();
    }
}

void CopyProgressItem::setDetailsShown(bool shown)
{
    m_detailsShown = shown;
    m_details->setVisible(shown);
    if (m_graph) {
        // The graph shows the progress, so the bar only stands in for it when collapsed.
        m_progressBar->setVisible(!shown);
    }
    if (m_timeLine) {
        m_timeLine->setVisible(!shown);
    }
    if (m_detailsButton) {
        setDetailsButton(m_detailsButton, shown);
    }
    updateHeader();
}

#include "moc_copyprogressdialog.cpp"
