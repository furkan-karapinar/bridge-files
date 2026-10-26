// Plugin entry point KWin looks up under org.kde.kdecoration3.
#include "decoration.h"

#include <KPluginFactory>

K_PLUGIN_FACTORY_WITH_JSON(ShellkitWin11DecorationFactory, "shellkit-win11.json", registerPlugin<Shellkit::Decoration>();)

#include "plugin.moc"
