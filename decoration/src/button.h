#pragma once
// One caption button: app icon (window menu), minimize, maximize/restore or close.

#include <KDecoration3/DecorationButton>

namespace Shellkit
{
class Decoration;

class Button : public KDecoration3::DecorationButton
{
    Q_OBJECT

public:
    Button(KDecoration3::DecorationButtonType type, Decoration *decoration, QObject *parent = nullptr);

    void paint(QPainter *painter, const QRectF &repaintArea) override;

private:
    void paintGlyph(QPainter *painter, const QRectF &box) const;
};
}
