#!/bin/sh
# Installs a launcher for limrendir, for the current user only (no sudo needed): limrendir then appears in the
# applications (it can be pinned to the dock from there) and, with --desktop, as an icon on the desktop.
# It also links ~/.local/bin/limrendir to the binary, to run limrendir from any terminal by typing limrendir.
#
# Usage: ./install-launcher.sh [--desktop] [--no-terminal] [path/to/limrendir]
#   --desktop       also put a launcher on the desktop
#   --no-terminal   do not open a terminal next to limrendir (by default one shows the log: warnings, errors)
#   path            the limrendir binary (default: build/limrendir, or limrendir, next to this script)
#
# Application icon: icons/limrendir.svg (preferred) or icons/limrendir.png (256x256), if present.
# Run the script again after adding or changing it, or after moving the binary.
# To remove everything: ./uninstall-launcher.sh

set -e

repo_dir=$(cd "$(dirname "$0")" && pwd)
desktop=false
terminal=true
binary=

for arg in "$@"; do
    case "$arg" in
        --desktop) desktop=true ;;
        --no-terminal) terminal=false ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "Unknown option: $arg" >&2; exit 1 ;;
        *) binary=$arg ;;
    esac
done

if [ -z "$binary" ]; then
    for candidate in "$repo_dir/build/limrendir" "$repo_dir/limrendir"; do
        if [ -x "$candidate" ] && [ -f "$candidate" ]; then
            binary=$candidate
            break
        fi
    done
fi
if [ -z "$binary" ] || [ ! -x "$binary" ] || [ ! -f "$binary" ]; then
    echo "limrendir binary not found: compile it first (see README.md), or give its path." >&2
    exit 1
fi
binary=$(cd "$(dirname "$binary")" && pwd)/$(basename "$binary")

data_dir=${XDG_DATA_HOME:-$HOME/.local/share}
applications_dir=$data_dir/applications
entry=$applications_dir/limrendir.desktop

# Application icon, installed in the user icon theme. Without one, a generic camera icon of the system theme.
icon=camera-video
for extension in svg png; do
    if [ -f "$repo_dir/icons/limrendir.$extension" ]; then
        if [ "$extension" = svg ]; then
            icon_dir=$data_dir/icons/hicolor/scalable/apps
        else
            icon_dir=$data_dir/icons/hicolor/256x256/apps
        fi
        mkdir -p "$icon_dir"
        cp "$repo_dir/icons/limrendir.$extension" "$icon_dir/"
        icon=limrendir
        gtk-update-icon-cache -q -t "$data_dir/icons/hicolor" 2>/dev/null || true
        break
    fi
done

mkdir -p "$applications_dir"
# Exec needs an absolute path, quoted in case it contains spaces
cat > "$entry" <<EOF
[Desktop Entry]
Type=Application
Name=Limrendir
Comment=Camera viewer and recorder
Exec="$binary"
Icon=$icon
Terminal=$terminal
Categories=AudioVideo;Video;
EOF
chmod +x "$entry"
update-desktop-database -q "$applications_dir" 2>/dev/null || true
echo "Launcher installed: $entry"
echo "  limrendir: $binary"
echo "  icon: $icon"

# Command line: a symbolic link, so that it keeps pointing to the binary after recompiling
bin_dir=$HOME/.local/bin
link=$bin_dir/limrendir
if [ -e "$link" ] && [ ! -L "$link" ]; then
    echo "Not creating $link: a file that is not a link already exists there." >&2
else
    mkdir -p "$bin_dir"
    ln -sfn "$binary" "$link"
    echo "Command: $link -> $binary"
    case ":$PATH:" in
        *":$bin_dir:"*) ;;
        *) echo "  $bin_dir is not in your PATH: open a new terminal (on Debian/Ubuntu ~/.profile adds it once it"
           echo "  exists), or add  export PATH=\"\$HOME/.local/bin:\$PATH\"  to ~/.bashrc" ;;
    esac
fi

if [ "$desktop" = true ]; then
    desktop_dir=$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")
    mkdir -p "$desktop_dir"
    cp "$entry" "$desktop_dir/limrendir.desktop"
    chmod +x "$desktop_dir/limrendir.desktop"
    # Ubuntu only launches desktop icons marked as trusted ("Allow Launching" in their menu)
    gio set "$desktop_dir/limrendir.desktop" metadata::trusted true 2>/dev/null || true
    echo "Desktop launcher: $desktop_dir/limrendir.desktop"
    echo "  (if it does not start on double click: right click on it, then Allow Launching)"
fi
