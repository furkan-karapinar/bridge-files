#pragma once
// Windows 11 window frame: a 32 px caption in the window colour, app icon and title on the
// left, 46 px minimize/maximize/close buttons on the right, rounded corners and a soft shadow.

#include <KDecoration3/Decoration>
#include <KDecoration3/DecorationButtonGroup>

#include <QVariant>

namespace Shellkit
{
class Decoration : public KDecoration3::Decoration
{
    Q_OBJECT

public:
    explicit Decoration(QObject *parent = nullptr, const QVariantList &args = QVariantList());
    ~Decoration() override;

    void paint(QPainter *painter, const QRectF &repaintArea) override;
    bool init() override;

    // Light palettes get dark hover fills and the other way round.
    bool isDark() const;
    QColor foreground() const;

private:
    void updateLayout();
    void updateShadow();
    bool hasCorners() const;
    // The window draws its own caption (its title carries the shellkit caption marker).
    bool clientCaption() const;

    KDecoration3::DecorationButtonGroup *m_left = nullptr;
    KDecoration3::DecorationButtonGroup *m_right = nullptr;
};
}
