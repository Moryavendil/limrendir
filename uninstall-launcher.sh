#!/bin/sh
# Removes what install-launcher.sh installed: the applications launcher, the desktop launcher, the icon and the
# command ~/.local/bin/limrendir.
# limrendir itself (the compiled binary) is left untouched.

data_dir=${XDG_DATA_HOME:-$HOME/.local/share}
desktop_dir=$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")

for file in "$data_dir/applications/limrendir.desktop" \
            "$desktop_dir/limrendir.desktop" \
            "$data_dir/icons/hicolor/scalable/apps/limrendir.svg" \
            "$data_dir/icons/hicolor/256x256/apps/limrendir.png"; do
    if [ -e "$file" ]; then
        rm -f "$file"
        echo "Removed $file"
    fi
done

# The command is removed only if it is a link (the one made by install-launcher.sh), not a file put there otherwise
link=$HOME/.local/bin/limrendir
if [ -L "$link" ]; then
    rm -f "$link"
    echo "Removed $link"
fi

update-desktop-database -q "$data_dir/applications" 2>/dev/null || true
gtk-update-icon-cache -q -t "$data_dir/icons/hicolor" 2>/dev/null || true
