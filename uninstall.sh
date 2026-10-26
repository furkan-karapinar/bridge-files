#!/usr/bin/env bash
# Removes what install.sh put into your home folder and gives KWin back its previous window
# decoration. Your File Explorer settings (~/.config/shellkit-filesrc) are kept.
set -euo pipefail

prefix=$HOME/.local
state=${XDG_DATA_HOME:-$HOME/.local/share}/bridge-files
plugins=$(qtpaths6 --query QT_INSTALL_PLUGINS)
userplugins=$prefix/${plugins#/usr/}

if [ -f "$state/kwin-decoration.bak" ]; then
    library=$(sed -n 's/^library=//p' "$state/kwin-decoration.bak")
    theme=$(sed -n 's/^theme=//p' "$state/kwin-decoration.bak")
    if [ -n "$library" ]; then
        kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key library "$library"
    else
        kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key library --delete
    fi
    if [ -n "$theme" ]; then
        kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key theme "$theme"
    else
        kwriteconfig6 --file kwinrc --group org.kde.kdecoration2 --key theme --delete
    fi
    echo "KWin gets its previous window decoration back after you sign out and in."
fi

rm -rf "$prefix/opt/shellkit-files" "$state" "${XDG_CACHE_HOME:-$HOME/.cache}/bridge-files"
rm -f "$userplugins/styles/shellkit-win11.so" \
    "$userplugins/org.kde.kdecoration3/org.shellkit.win11.so" \
    "${XDG_DATA_HOME:-$HOME/.local/share}/applications/org.shellkit.files.desktop" \
    "${XDG_CONFIG_HOME:-$HOME/.config}/plasma-workspace/env/bridge-files-plugins.sh"
echo "Bridge Files removed."
