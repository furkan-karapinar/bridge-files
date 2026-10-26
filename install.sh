#!/usr/bin/env bash
# Builds Bridge Files and installs it into your home folder, no sudo:
#   the app         ~/.local/opt/shellkit-files, with a launcher "File Explorer"
#   the Qt style    ~/.local/<Qt plugin dir>/styles (Windows 11 and Windows 7 looks)
#   the frame       ~/.local/<Qt plugin dir>/org.kde.kdecoration3 (tabs in the title bar)
#
# Usage: ./install.sh [--no-frame] [--use-frame]
#   --no-frame   skip the window frame
#   --use-frame  also make the frame KWin's window decoration (the previous one is saved and
#                ./uninstall.sh puts it back); sign out and in once afterwards
set -euo pipefail

root=$(cd "$(dirname "$0")" && pwd)
frame=1
useframe=0
for arg in "$@"; do
    case $arg in
    --no-frame) frame=0 ;;
    --use-frame) useframe=1 ;;
    *) echo "unknown option: $arg"; exit 1 ;;
    esac
done

for tool in cmake ninja qtpaths6 kwriteconfig6; do
    command -v "$tool" >/dev/null || { echo "missing: $tool (see README, Build dependencies)"; exit 1; }
done

prefix=$HOME/.local
appprefix=$prefix/opt/shellkit-files
build=${XDG_CACHE_HOME:-$HOME/.cache}/bridge-files/build
state=${XDG_DATA_HOME:-$HOME/.local/share}/bridge-files
# Mirror Qt's own plugin layout under ~/.local (ECM would pick lib64/plugins for a user prefix).
plugins=$(qtpaths6 --query QT_INSTALL_PLUGINS)
userplugins=$prefix/${plugins#/usr/}
mkdir -p "$build" "$state"

say() { printf '\n== %s\n' "$1"; }

say "Qt style"
cmake -S "$root/style" -B "$build/style" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DKDE_INSTALL_PLUGINDIR="$userplugins"
cmake --build "$build/style"
cmake --install "$build/style"

if [ "$frame" = 1 ]; then
    say "window frame"
    cmake -S "$root/decoration" -B "$build/decoration" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$prefix" -DKDE_INSTALL_PLUGINDIR="$userplugins"
    cmake --build "$build/decoration"
    cmake --install "$build/decoration"
    # KWin loads plugins from ~/.local only through QT_PLUGIN_PATH, set at sign-in.
    envdir=${XDG_CONFIG_HOME:-$HOME/.config}/plasma-workspace/env
    mkdir -p "$envdir"
    cat > "$envdir/bridge-files-plugins.sh" <<EOF
# Written by Bridge Files install.sh: lets KWin load the window frame from ~/.local.
export QT_PLUGIN_PATH="$userplugins\${QT_PLUGIN_PATH:+:\$QT_PLUGIN_PATH}"
EOF
fi

say "File Explorer (this takes a while)"
cmake -S "$root/app" -B "$build/app" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX="$appprefix" -DBUILD_TESTING=OFF
cmake --build "$build/app"
cmake --install "$build/app" >/dev/null

apps=${XDG_DATA_HOME:-$HOME/.local/share}/applications
mkdir -p "$apps"
# The launcher passes the plugin path itself, so the style loads even before a new sign-in.
cat > "$apps/org.shellkit.files.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=File Explorer
Name[tr]=Dosya Gezgini
GenericName=File Manager
GenericName[tr]=Dosya Yöneticisi
Comment=Bridge Files: a file manager laid out like the one on Windows
Exec=env QT_PLUGIN_PATH=$userplugins $appprefix/bin/shellkit-files %u
Icon=system-file-manager
Terminal=false
MimeType=inode/directory;
Categories=Qt;KDE;System;FileTools;FileManager;
StartupWMClass=shellkit-files
EOF

if [ "$useframe" = 1 ] && [ "$frame" = 1 ]; then
    say "window decoration"
    if [ ! -f "$state/kwin-decoration.bak" ]; then
        {
            echo "library=$(kreadconfig6 --file kwinrc --group org.kde.kdecoration2 --key library)"
            echo "theme=$(kreadconfig6 --file kwinrc --group org.kde.kdecoration2 --key theme)"
        } > "$state/kwin-decoration.bak"
    fi
    kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key library org.shellkit.win11
    kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key theme --delete
    echo "KWin will use the Windows 11 frame after you sign out and in."
fi

say "done"
echo "Start \"File Explorer\" from the app menu, or run: $appprefix/bin/shellkit-files"
if [ "$frame" = 1 ] && [ "$useframe" = 0 ]; then
    echo "For tabs in the title bar, pick the \"Windows 11 (shellkit)\" window decoration in"
    echo "System Settings > Colors & Themes > Window Decorations (after signing out and in once),"
    echo "or run ./install.sh --use-frame."
fi
