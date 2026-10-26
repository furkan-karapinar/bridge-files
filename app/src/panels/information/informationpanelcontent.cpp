/*
 * SPDX-FileCopyrightText: 2009 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "informationpanelcontent.h"

#include <KConfigGroup>
#include <KIO/PreviewJob>
#include <KIconEffect>
#include <KIconLoader>
#include <KIconUtils>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KStringHandler>

#include <QIcon>
#include <QStyle>
#include <QTextDocument>

#include <Baloo/FileMetaDataWidget>

#include <QDialogButtonBox>
#include <QGesture>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QScroller>
#include <QTextLayout>
#include <QTimer>
#include <QVBoxLayout>

#include "dolphin_informationpanelsettings.h"
#include "mediawidget.h"
#include "pixmapviewer.h"
#include "shellkit.h"

#include <QDir>
#include <QHBoxLayout>

namespace
{
// Icon size of the details pane preview in Windows.
constexpr int PreviewIconSize = 96;
// Height of the preview band, margins included.
constexpr int PreviewBandHeight = 220;
constexpr int PreviewBandMargin = 16;

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

/** shellkit fork: the slightly lighter band behind the preview, as in the Windows details pane. */
class PreviewBand : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), isDark(palette()) ? QColor(255, 255, 255, 10) : QColor(0, 0, 0, 8));
    }
};

/** shellkit fork: the rounded hint card shown while no single file is selected. */
class HintCard : public QWidget
{
public:
    explicit HintCard(QWidget *parent)
        : QWidget(parent)
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(10);
        m_icon = new QLabel(this);
        m_icon->setAlignment(Qt::AlignTop);
        layout->addWidget(m_icon, 0, Qt::AlignTop);
        auto *text = new QLabel(i18ndc(shellkitDomain, "@info Details pane", "Select a single file to get more information."), this);
        text->setWordWrap(true);
        layout->addWidget(text, 1);
        updateIcon();
    }

protected:
    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::PaletteChange) {
            updateIcon();
        }
        QWidget::changeEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor border = palette().color(QPalette::WindowText);
        border.setAlphaF(isDark(palette()) ? 0.14 : 0.1);
        const QColor fill = isDark(palette()) ? QColor(255, 255, 255, 10) : QColor(0, 0, 0, 5);
        painter.setPen(border);
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }

private:
    void updateIcon()
    {
        m_icon->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-information")).pixmap(16, 16));
    }

    QLabel *m_icon = nullptr;
};
}

InformationPanelContent::InformationPanelContent(QWidget *parent)
    : QWidget(parent)
    , m_item()
    , m_previewJob(nullptr)
    , m_outdatedPreviewTimer(nullptr)
    , m_preview(nullptr)
    , m_mediaWidget(nullptr)
    , m_nameLabel(nullptr)
    , m_metaDataWidget(nullptr)
    , m_metaDataArea(nullptr)
    , m_isVideo(false)
{
    parent->installEventFilter(this);

    // Initialize timer for disabling an outdated preview with a small
    // delay. This prevents flickering if the new preview can be generated
    // within a very small timeframe.
    m_outdatedPreviewTimer = new QTimer(this);
    m_outdatedPreviewTimer->setInterval(100);
    m_outdatedPreviewTimer->setSingleShot(true);
    connect(m_outdatedPreviewTimer, &QTimer::timeout, this, &InformationPanelContent::markOutdatedPreview);

    // shellkit fork: a full-width preview band on top, then the name and the
    // details with Windows margins.
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    m_previewBand = new PreviewBand(this);
    QVBoxLayout *bandLayout = new QVBoxLayout(m_previewBand);
    bandLayout->setContentsMargins(PreviewBandMargin, PreviewBandMargin, PreviewBandMargin, PreviewBandMargin);
    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(20, 16, 20, 12);
    layout->setSpacing(12);
    outerLayout->addWidget(m_previewBand);
    outerLayout->addLayout(layout, 1);

    // preview
    const int minPreviewWidth = KIconLoader::SizeEnormous + KIconLoader::SizeMedium;

    m_preview = new PixmapViewer(parent);
    m_preview->setMinimumWidth(minPreviewWidth);
    m_preview->setMinimumHeight(KIconLoader::SizeEnormous);

    m_mediaWidget = new MediaWidget(parent);
    m_mediaWidget->hide();
    m_mediaWidget->setMinimumWidth(minPreviewWidth);
    m_mediaWidget->setAutoPlay(InformationPanelSettings::previewsAutoPlay());
    connect(m_mediaWidget, &MediaWidget::hasVideoChanged, this, &InformationPanelContent::slotHasVideoChanged);

    // name
    m_nameLabel = new QTextEdit(parent);
    QFont font = m_nameLabel->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.15);
    m_nameLabel->setFont(font);
    m_nameLabel->setAlignment(Qt::AlignLeft);
    m_nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_nameLabel->setFrameShape(QFrame::NoFrame);
    // shellkit fork: no style sheet, it would freeze the text colour across theme changes.
    m_nameLabel->viewport()->setAutoFillBackground(false);
    m_nameLabel->document()->setDocumentMargin(0);
    m_nameLabel->setContentsMargins(0, 0, 0, 0); 
    m_nameLabel->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nameLabel->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QTextDocument *doc = m_nameLabel->document();
    QTextOption option = doc->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAnywhere);
    doc->setDefaultTextOption(option);

    const bool previewsShown = InformationPanelSettings::previewsShown();
    m_preview->setVisible(previewsShown);

    m_metaDataWidget = new Baloo::FileMetaDataWidget(parent);
    m_metaDataWidget->setDateFormat(static_cast<Baloo::DateFormats>(InformationPanelSettings::dateFormat()));
    connect(m_metaDataWidget, &Baloo::FileMetaDataWidget::urlActivated, this, &InformationPanelContent::urlActivated);
    m_metaDataWidget->setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
    m_metaDataWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // Configuration
    m_configureLabel = new QLabel(i18nc("@label::textbox", "Select which data should be shown:"), this);
    m_configureLabel->setWordWrap(true);
    m_configureLabel->setVisible(false);

    m_configureButtons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    m_configureButtons->setVisible(false);
    connect(m_configureButtons, &QDialogButtonBox::accepted, this, [this]() {
        m_metaDataWidget->setConfigurationMode(Baloo::ConfigurationMode::Accept);
        m_configureButtons->setVisible(false);
        m_configureLabel->setVisible(false);
        Q_EMIT configurationFinished();
    });
    connect(m_configureButtons, &QDialogButtonBox::rejected, this, [this]() {
        m_metaDataWidget->setConfigurationMode(Baloo::ConfigurationMode::Cancel);
        m_configureButtons->setVisible(false);
        m_configureLabel->setVisible(false);
        Q_EMIT configurationFinished();
    });

    m_metaDataArea = new QScrollArea(parent);
    m_metaDataArea->setWidget(m_metaDataWidget);
    m_metaDataArea->setWidgetResizable(true);
    m_metaDataArea->setFrameShape(QFrame::NoFrame);

    QWidget *viewport = m_metaDataArea->viewport();
    QScroller::grabGesture(viewport, QScroller::TouchGesture);
    viewport->installEventFilter(this);

    m_hintCard = new HintCard(this);
    m_hintCard->hide();

    bandLayout->addWidget(m_preview);
    bandLayout->addWidget(m_mediaWidget);
    layout->addWidget(m_nameLabel);
    layout->addWidget(m_hintCard);
    layout->addWidget(m_configureLabel);
    layout->addWidget(m_metaDataArea, 1);
    layout->addWidget(m_configureButtons);
    layout->addStretch(0);

    grabGesture(Qt::TapAndHoldGesture);
}

InformationPanelContent::~InformationPanelContent()
{
    InformationPanelSettings::self()->save();
}

void InformationPanelContent::showItem(const KFileItem &item)
{
    // compares item entries, comparing items only compares urls
    if (m_item.entry() != item.entry()) {
        m_item = item;
        m_preview->stopAnimatedImage();
        refreshMetaData();
    }

    setHintVisible(false);
    refreshPreview();
}

void InformationPanelContent::showFolder(const KFileItem &item)
{
    showItem(item);

    if (item.isLocalFile()) {
        const int count = QDir(item.localPath()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System).count();
        setNameLabelText(i18ndcp(shellkitDomain, "@label %2 is a folder name", "%2 (%1 item)", "%2 (%1 items)", count, item.text()));
    }
    setHintVisible(true);
}

void InformationPanelContent::setHintVisible(bool visible)
{
    m_hintCard->setVisible(visible);
    m_metaDataArea->setVisible(!visible);
}

void InformationPanelContent::refreshPixmapView()
{
    // If there is a preview job, kill it to prevent that we have jobs for
    // multiple items running, and thus a race condition (bug 250787).
    if (m_previewJob) {
        m_previewJob->kill();
    }

    // Reset disabled state when starting a new preview job
    m_disabledPreviewUrl.clear();

    // try to get a preview pixmap from the item...

    // Mark the currently shown preview as outdated. This is done
    // with a small delay to prevent a flickering when the next preview
    // can be shown within a short timeframe.
    m_outdatedPreviewTimer->start();

    const KConfigGroup globalConfig(KSharedConfig::openConfig(), "PreviewSettings");
    const QStringList plugins = globalConfig.readEntry("Plugins", KIO::PreviewJob::defaultPlugins());
    m_previewJob = new KIO::PreviewJob(KFileItemList() << m_item, QSize(m_preview->width(), m_preview->height()), &plugins);
    m_previewJob->setScaleType(KIO::PreviewJob::Unscaled);
    m_previewJob->setIgnoreMaximumSize(m_item.isLocalFile() && !m_item.isSlow());
    m_previewJob->setDevicePixelRatio(devicePixelRatioF());
    if (m_previewJob->uiDelegate()) {
        KJobWidgets::setWindow(m_previewJob, this);
    }

    connect(m_previewJob.data(), &KIO::PreviewJob::gotPreview, this, &InformationPanelContent::showPreview);
    connect(m_previewJob.data(), &KIO::PreviewJob::failed, this, &InformationPanelContent::showIcon);
}

void InformationPanelContent::refreshPreview()
{
    // If there is a preview job, kill it to prevent that we have jobs for
    // multiple items running, and thus a race condition (bug 250787).
    if (m_previewJob) {
        m_previewJob->kill();
    }

    m_preview->setCursor(Qt::ArrowCursor);
    setNameLabelText(m_item.text());
    m_previewBand->setVisible(InformationPanelSettings::previewsShown());
    if (InformationPanelSettings::previewsShown()) {
        const QUrl itemUrl = m_item.url();
        const bool isSearchUrl = itemUrl.scheme().contains(QLatin1String("search")) && m_item.localPath().isEmpty();
        if (isSearchUrl) {
            m_preview->show();
            m_mediaWidget->hide();

            // in the case of a search-URL the URL is not readable for humans
            // (at least not useful to show in the Information Panel)
            m_preview->setPixmap(QIcon::fromTheme(QStringLiteral("baloo")).pixmap(m_preview->height(), m_preview->width()));
        } else {
            refreshPixmapView();

            const QString mimeType = m_item.mimetype();
            const bool isAnimatedImage = m_preview->isAnimatedMimeType(mimeType);
            m_isVideo = !isAnimatedImage && mimeType.startsWith(QLatin1String("video/"));
            bool useMedia = m_isVideo || mimeType.startsWith(QLatin1String("audio/"));

            if (useMedia) {
                // change the cursor of the preview
                m_preview->setCursor(Qt::PointingHandCursor);
                m_preview->installEventFilter(m_mediaWidget);

                m_mediaWidget->show();

                // if the video is playing, has been paused or stopped
                // we don't need to update the preview/media widget states
                // unless the previewed file has changed,
                // or the setting previewshown has changed
                if ((m_mediaWidget->state() != QMediaPlayer::PlayingState && m_mediaWidget->state() != QMediaPlayer::PausedState
                     && m_mediaWidget->state() != QMediaPlayer::StoppedState)
                    || m_item.targetUrl() != m_mediaWidget->url() || (!m_preview->isVisible() && !m_mediaWidget->isVisible())) {
                    if (InformationPanelSettings::previewsAutoPlay() && m_isVideo) {
                        // hides the preview now to avoid flickering when the autoplay video starts
                        m_preview->hide();
                    } else {
                        // the video won't play before the preview is displayed
                        m_preview->show();
                    }

                    m_mediaWidget->setUrl(m_item.targetUrl(), m_isVideo ? MediaWidget::MediaKind::Video : MediaWidget::MediaKind::Audio);
                    adjustWidgetSizes(parentWidget()->width());
                }
            } else {
                if (isAnimatedImage) {
                    m_preview->setAnimatedImageFileName(itemUrl.toLocalFile());
                }
                // When we don't need it, hide the media widget first to avoid flickering
                m_mediaWidget->hide();
                m_preview->show();
                m_preview->removeEventFilter(m_mediaWidget);
                m_mediaWidget->clearUrl();
            }
        }
    } else {
        m_preview->stopAnimatedImage();
        m_preview->hide();
        m_mediaWidget->hide();
    }
}

void InformationPanelContent::configureShownProperties()
{
    setHintVisible(false);
    m_configureLabel->setVisible(true);
    m_configureButtons->setVisible(true);
    m_metaDataWidget->setConfigurationMode(Baloo::ConfigurationMode::ReStart);
}

void InformationPanelContent::refreshMetaData()
{
    m_metaDataWidget->setDateFormat(static_cast<Baloo::DateFormats>(InformationPanelSettings::dateFormat()));
    m_metaDataWidget->show();
    m_metaDataWidget->setItems(KFileItemList() << m_item);
}

void InformationPanelContent::showItems(const KFileItemList &items)
{
    // If there is a preview job, kill it to prevent that we have jobs for
    // multiple items running, and thus a race condition (bug 250787).
    if (m_previewJob) {
        m_previewJob->kill();
    }

    m_preview->stopAnimatedImage();

    m_preview->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-information")).pixmap(PreviewIconSize, PreviewIconSize));
    setNameLabelText(i18ncp("@label", "%1 item selected", "%1 items selected", items.count()));

    m_metaDataWidget->setItems(items);
    setHintVisible(true);

    m_mediaWidget->hide();

    m_item = KFileItem();
}

bool InformationPanelContent::eventFilter(QObject *obj, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Resize: {
        QResizeEvent *resizeEvent = static_cast<QResizeEvent *>(event);
        if (obj == m_metaDataArea->viewport()) {
            // The size of the meta text area has changed. Adjust the fixed
            // width in a way that no horizontal scrollbar needs to be shown.
            m_metaDataWidget->setFixedWidth(resizeEvent->size().width());
        } else if (obj == parent()) {
            adjustWidgetSizes(resizeEvent->size().width());
        }
        break;
    }

    case QEvent::Polish:
        adjustWidgetSizes(parentWidget()->width());
        break;

    case QEvent::FontChange:
        m_metaDataWidget->setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
        break;

    default:
        break;
    }

    return QWidget::eventFilter(obj, event);
}

bool InformationPanelContent::event(QEvent *event)
{
    if (event->type() == QEvent::Gesture) {
        gestureEvent(static_cast<QGestureEvent *>(event));
        return true;
    }
    return QWidget::event(event);
}

bool InformationPanelContent::gestureEvent(QGestureEvent *event)
{
    if (!underMouse()) {
        return false;
    }

    QTapAndHoldGesture *tap = static_cast<QTapAndHoldGesture *>(event->gesture(Qt::TapAndHoldGesture));

    if (tap) {
        if (tap->state() == Qt::GestureFinished) {
            Q_EMIT contextMenuRequested(tap->position().toPoint());
        }
        event->accept();
        return true;
    }
    return false;
}

void InformationPanelContent::showIcon(const KFileItem &item)
{
    m_outdatedPreviewTimer->stop();
    QIcon icon = QIcon::fromTheme(item.iconName());
    // shellkit fork: icons stay at the Windows preview size instead of filling the band.
    QPixmap pixmap = KIconUtils::addOverlays(icon, item.overlays()).pixmap(QSize(PreviewIconSize, PreviewIconSize), devicePixelRatioF());
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    m_preview->setPixmap(pixmap);
    m_disabledPreviewUrl.clear();
}

void InformationPanelContent::showPreview(const KFileItem &item, const QPixmap &pixmap)
{
    m_outdatedPreviewTimer->stop();

    QPixmap p = pixmap;
    if (!item.overlays().isEmpty()) {
        // Avoid scaling the images that are smaller than the preview size, to be consistent when there is no overlays
        if (pixmap.height() < m_preview->height() && pixmap.width() < m_preview->width()) {
            p = QPixmap(m_preview->size() * devicePixelRatioF());
            p.fill(Qt::transparent);
            p.setDevicePixelRatio(devicePixelRatioF());

            QPainter painter(&p);
            painter.drawPixmap(QPointF{m_preview->width() / 2.0 - pixmap.width() / pixmap.devicePixelRatioF() / 2,
                                       m_preview->height() / 2.0 - pixmap.height() / pixmap.devicePixelRatioF() / 2}
                                   .toPoint(),
                               pixmap);
        }
        p = KIconUtils::addOverlays(p, item.overlays()).pixmap(m_preview->size(), devicePixelRatioF());
        p.setDevicePixelRatio(devicePixelRatioF());
    }

    m_preview->setPixmap(p);
}

void InformationPanelContent::markOutdatedPreview()
{
    if (m_item.isDir()) {
        // directory preview can be long
        // but since we always have icons to display
        // use it until the preview is done
        showIcon(m_item);
    } else {
        // Only apply disabled effect once per URL to avoid repeated brightening
        if (m_disabledPreviewUrl == m_item.url()) {
            return;
        }
        m_disabledPreviewUrl = m_item.url();

        QPixmap disabledPixmap = m_preview->pixmap();
        if (!disabledPixmap.isNull()) {
            KIconEffect::toDisabled(disabledPixmap);
            m_preview->setPixmap(disabledPixmap);
        }
    }
}

KFileItemList InformationPanelContent::items()
{
    return m_metaDataWidget->items();
}

void InformationPanelContent::slotHasVideoChanged(bool hasVideo)
{
    m_preview->setVisible(InformationPanelSettings::previewsShown() && !hasVideo);
    if (m_preview->isVisible() && m_preview->size().width() != m_preview->pixmap().size().width()) {
        // in case the information panel has been resized when the preview was not displayed
        // we need to refresh its content
        refreshPixmapView();
    }
}

void InformationPanelContent::setPreviewAutoPlay(bool autoPlay)
{
    m_mediaWidget->setAutoPlay(autoPlay);
}

void InformationPanelContent::setNameLabelText(const QString &text)
{
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    const QString processedText = Qt::mightBeRichText(text) ? text : KStringHandler::preProcessWrap(text);

    m_nameLabel->setText(processedText);
    m_nameLabel->setFixedHeight(m_nameLabel->document()->size().height());
    m_nameLabel->setAlignment(Qt::AlignLeft);
}

void InformationPanelContent::adjustWidgetSizes(int width)
{
    // If the text inside the name label or the info label cannot
    // get wrapped, then the maximum width of the label is increased
    // so that the width of the information panel gets increased.
    // To prevent this, the maximum width is adjusted to
    // the current width of the panel.
    const int maxWidth = width - style()->layoutSpacing(QSizePolicy::DefaultType, QSizePolicy::DefaultType, Qt::Horizontal) * 4;
    m_nameLabel->setMaximumWidth(maxWidth);

    // The metadata widget also contains a text widget which may return
    // a large preferred width.
    m_metaDataWidget->setMaximumWidth(maxWidth);

    // try to increase the preview as large as possible
    // shellkit fork: the preview fills the fixed-height band, as in Windows.
    const int previewHeight = PreviewBandHeight - 2 * PreviewBandMargin;
    m_preview->setMinimumHeight(previewHeight);
    m_preview->setSizeHint(QSize(maxWidth, previewHeight));

    // assure that the size of the video player is the same as the preview size
    m_mediaWidget->setVideoSize(QSize(maxWidth, maxWidth));
}

#include "moc_informationpanelcontent.cpp"
