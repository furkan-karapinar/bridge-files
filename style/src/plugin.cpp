// Qt style plugin entry: QStyleFactory::create("shellkit-win11") returns Shellkit::Style,
// "shellkit-win7" Shellkit::Win7Style (the File Explorer's Windows 7 look).

#include "style.h"
#include "win7style.h"

#include <QStylePlugin>

class ShellkitStylePlugin : public QStylePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QStyleFactoryInterface_iid FILE "shellkit-win11.json")

public:
    QStyle *create(const QString &key) override
    {
        if (key.compare(QLatin1String("shellkit-win11"), Qt::CaseInsensitive) == 0) {
            return new Shellkit::Style;
        }
        if (key.compare(QLatin1String("shellkit-win7"), Qt::CaseInsensitive) == 0) {
            return new Shellkit::Win7Style;
        }
        return nullptr;
    }
};

#include "plugin.moc"
