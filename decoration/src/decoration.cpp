#include "decoration.h"

#include "button.h"
#include "metrics.h"

#include <KDecoration3/DecoratedWindow>
#include <KDecoration3/DecorationSettings>
#include <KDecoration3/DecorationShadow>

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace Shellkit
{
namespace
{
// One drop shadow layer: a blurred copy of the window shape, moved down by dy.
struct ShadowLayer {
    qreal alpha;
    qreal sigma;
    qreal dy;
};

// Windows 11 windows sit on a wide, soft key shadow plus a tight contact shadow; inactive
// windows drop to a smaller, fainter pair.
constexpr ShadowLayer ActiveShadow[] = {{0.37, 16, 12}, {0.20, 3, 1}};
constexpr ShadowLayer InactiveShadow[] = {{0.22, 10, 6}, {0.16, 2, 1}};
const QMarginsF ShadowPadding(48, 40, 48, 64);

// Signed distance from p to a rounded rectangle (negative inside).
qreal roundedRectDistance(QPointF p, const QRectF &rect, qreal radius)
{
    const qreal qx = std::abs(p.x() - rect.center().x()) - (rect.width() / 2 - radius);
    const qreal qy = std::abs(p.y() - rect.center().y()) - (rect.height() / 2 - radius);
    const qreal outside = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0));
    return outside + std::min(std::max(qx, qy), 0.0) - radius;
}

std::shared_ptr<KDecoration3::DecorationShadow> makeShadow(bool active)
{
    // The window part is a 9-patch: corners of the radius plus one stretchable pixel.
    const qreal core = 2 * Metrics::CornerRadius + 1;
    const QSize size(qRound(ShadowPadding.left() + core + ShadowPadding.right()), qRound(ShadowPadding.top() + core + ShadowPadding.bottom()));
    const QRectF window(ShadowPadding.left(), ShadowPadding.top(), core, core);

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const QPointF p(x + 0.5, y + 0.5);
            qreal clear = 1;
            for (const ShadowLayer &layer : active ? ActiveShadow : InactiveShadow) {
                const qreal d = roundedRectDistance(p, window.translated(0, layer.dy), Metrics::CornerRadius);
                const qreal falloff = d <= 0 ? 1 : std::exp(-d * d / (2 * layer.sigma * layer.sigma));
                clear *= 1 - layer.alpha * falloff;
            }
            line[x] = qPremultiply(qRgba(0, 0, 0, qRound((1 - clear) * 255)));
        }
    }

    auto shadow = std::make_shared<KDecoration3::DecorationShadow>();
    shadow->setPadding(ShadowPadding);
    shadow->setInnerShadowRect(window);
    shadow->setShadow(image);
    return shadow;
}
}

Decoration::Decoration(QObject *parent, const QVariantList &args)
    : KDecoration3::Decoration(parent, args)
{
}

Decoration::~Decoration() = default;

bool Decoration::init()
{
    auto *w = window();

    // Fixed Windows layout: icon (window menu) left, minimize/maximize/close right.
    m_left = new KDecoration3::DecorationButtonGroup(this);
    m_left->addButton(new Button(KDecoration3::DecorationButtonType::Menu, this, m_left));
    m_right = new KDecoration3::DecorationButtonGroup(this);
    for (auto type : {KDecoration3::DecorationButtonType::Minimize, KDecoration3::DecorationButtonType::Maximize, KDecoration3::DecorationButtonType::Close}) {
        m_right->addButton(new Button(type, this, m_right));
    }

    connect(w, &KDecoration3::DecoratedWindow::widthChanged, this, &Decoration::updateLayout);
    connect(w, &KDecoration3::DecoratedWindow::maximizedChanged, this, &Decoration::updateLayout);
    connect(w, &KDecoration3::DecoratedWindow::adjacentScreenEdgesChanged, this, &Decoration::updateLayout);
    connect(w, &KDecoration3::DecoratedWindow::minimizeableChanged, this, &Decoration::updateLayout);
    connect(w, &KDecoration3::DecoratedWindow::maximizeableChanged, this, &Decoration::updateLayout);
    connect(settings().get(), &KDecoration3::DecorationSettings::reconfigured, this, &Decoration::updateLayout);
    connect(w, &KDecoration3::DecoratedWindow::activeChanged, this, [this] {
        updateShadow();
        update();
    });
    connect(w, &KDecoration3::DecoratedWindow::captionChanged, this, [this] {
        if (clientCaption() != (borders().top() == 0)) {
            updateLayout();
        }
        update();
    });
    connect(w, &KDecoration3::DecoratedWindow::paletteChanged, this, [this] {
        updateLayout();
        update();
    });

    updateLayout();
    updateShadow();
    return true;
}

bool Decoration::hasCorners() const
{
    // Windows squares the corners of maximized and snapped windows.
    return !window()->isMaximized() && window()->adjacentScreenEdges() == Qt::Edges();
}

bool Decoration::clientCaption() const
{
    // U+2060 WORD JOINER, set by the shellkit File Explorer (shellkitCaptionMarker).
    return window()->caption().contains(QChar(0x2060));
}

bool Decoration::isDark() const
{
    return window()->color(KDecoration3::ColorGroup::Active, KDecoration3::ColorRole::TitleBar).lightness() < 128;
}

QColor Decoration::foreground() const
{
    const auto group = window()->isActive() ? KDecoration3::ColorGroup::Active : KDecoration3::ColorGroup::Inactive;
    return window()->color(group, KDecoration3::ColorRole::Foreground);
}

void Decoration::updateLayout()
{
    auto *w = window();
    const qreal width = w->width();
    const bool maximized = w->isMaximized();

    // A window with its own caption keeps the corners, edge, shadow and resize margins only.
    const qreal titleHeight = clientCaption() ? 0 : Metrics::TitleHeight;
    setBorders(QMarginsF(0, titleHeight, 0, 0));
    const qreal resize = maximized ? 0 : Metrics::ResizeMargin;
    setResizeOnlyBorders(QMarginsF(resize, resize, resize, resize));
    setTitleBar(QRectF(0, 0, width, titleHeight));

    const KDecoration3::BorderRadius radius = hasCorners() ? KDecoration3::BorderRadius(Metrics::CornerRadius) : KDecoration3::BorderRadius();
    setBorderRadius(radius);
    // The 1 px window edge: WinUI's control stroke, faint on both palettes.
    QColor edge(0x75, 0x75, 0x75, isDark() ? 0x66 : 0x4d);
    setBorderOutline(maximized ? KDecoration3::BorderOutline() : KDecoration3::BorderOutline(1, edge, radius));

    const bool caption = !clientCaption();
    for (auto *button : m_left->buttons()) {
        button->setVisible(caption);
    }
    for (auto *button : m_right->buttons()) {
        if (button->type() == KDecoration3::DecorationButtonType::Minimize) {
            button->setVisible(caption && w->isMinimizeable());
        } else if (button->type() == KDecoration3::DecorationButtonType::Maximize) {
            button->setVisible(caption && w->isMaximizeable());
        } else {
            button->setVisible(caption);
        }
    }
    m_left->setPos(QPointF(Metrics::IconInset - (Metrics::TitleHeight - Metrics::IconSize) / 2, 0));
    m_right->setPos(QPointF(width - m_right->geometry().width(), 0));
    update();
}

void Decoration::updateShadow()
{
    static std::shared_ptr<KDecoration3::DecorationShadow> active;
    static std::shared_ptr<KDecoration3::DecorationShadow> inactive;
    auto &shadow = window()->isActive() ? active : inactive;
    if (!shadow) {
        shadow = makeShadow(window()->isActive());
    }
    setShadow(shadow);
}

void Decoration::paint(QPainter *painter, const QRectF &repaintArea)
{
    auto *w = window();
    if (clientCaption()) {
        return;
    }
    const qreal width = w->width();
    const qreal r = hasCorners() ? Metrics::CornerRadius : 0;

    // Caption in the window colour (Mica falls back to this flat fill when inactive).
    QPainterPath caption;
    caption.moveTo(0, Metrics::TitleHeight);
    caption.lineTo(0, r);
    caption.arcTo(QRectF(0, 0, 2 * r, 2 * r), 180, -90);
    caption.lineTo(width - r, 0);
    caption.arcTo(QRectF(width - 2 * r, 0, 2 * r, 2 * r), 90, -90);
    caption.lineTo(width, Metrics::TitleHeight);
    caption.closeSubpath();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    const auto group = w->isActive() ? KDecoration3::ColorGroup::Active : KDecoration3::ColorGroup::Inactive;
    painter->setBrush(w->color(group, KDecoration3::ColorRole::TitleBar));
    painter->drawPath(caption);

    const qreal textLeft = Metrics::IconInset + Metrics::IconSize + Metrics::CaptionGap;
    const qreal textRight = m_right->geometry().left() - Metrics::CaptionGap;
    if (textRight > textLeft) {
        const QRectF textRect(textLeft, 0, textRight - textLeft, Metrics::TitleHeight);
        painter->setFont(settings()->font());
        painter->setPen(foreground());
        const QString text = painter->fontMetrics().elidedText(w->caption(), Qt::ElideRight, qFloor(textRect.width()));
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
    }

    // Button fills stop at the rounded corner, as the close button's red does on Windows.
    painter->setClipPath(caption);
    m_left->paint(painter, repaintArea);
    m_right->paint(painter, repaintArea);
    painter->restore();
}
}
