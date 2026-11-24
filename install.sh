#!/bin/sh
# Compiles limrendir in the build folder next to this script, then installs a launcher for it, for the current user
# only (no sudo needed): limrendir then appears in the applications (it can be pinned to the dock from there) and,
# with --desktop, as an icon on the desktop. It also links ~/.local/bin/limrendir to the binary, to run limrendir from
# any terminal by typing limrendir.
# The dependencies (GTK, GStreamer, Aravis...) must be installed first: see README.md.
#
# Usage: ./install.sh [--desktop] [--no-terminal] [--no-build] [--setcap] [path/to/limrendir]
#   --desktop       also put a launcher on the desktop
#   --no-terminal   do not open a terminal next to limrendir (by default one shows the log: warnings, errors)
#   --no-build      do not compile: use the binary already compiled (build/limrendir, or limrendir, next to this
#                   script)
#   --setcap        allow limrendir to use packet sockets, for Ethernet cameras (asks for the sudo password).
#                   Compiling again removes this permission: use this option at every update
#   path            a limrendir binary compiled elsewhere (then nothing is compiled)
#
# To update limrendir: git pull, then run the script again, with the same options.
# Application icon: data/icons/limrendir.svg (preferred) or data/icons/limrendir.png (256x256), if present.
# Run the script again after adding or changing it, or after moving the limrendir folder.
# To remove everything: ./uninstall.sh

set -e

repo_dir=$(cd "$(dirname "$0")" && pwd)
desktop=false
terminal=true
build=true
setcap=false
binary=

for arg in "$@"; do
    case "$arg" in
        --desktop) desktop=true ;;
        --no-terminal) terminal=false ;;
        --no-build) build=false ;;
        --setcap) setcap=true ;;
        -h|--help) sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*) echo "Unknown option: $arg" >&2; exit 1 ;;
        *) binary=$arg ;;
    esac
done

# Whether a file may open packet sockets (getcap is often in /usr/sbin, outside the PATH of the users)
has_net_raw() {
    for getcap in "$(command -v getcap)" /usr/sbin/getcap /sbin/getcap; do
        if [ -x "$getcap" ]; then
            "$getcap" "$1" 2>/dev/null | grep -q cap_net_raw
            return
        fi
    done
    return 1
}

had_net_raw=false
if [ -z "$binary" ] && [ "$build" = true ]; then
    if ! command -v cmake > /dev/null 2>&1; then
        echo "cmake not found: install the dependencies first (see How to install in README.md)." >&2
        exit 1
    fi
    build_dir=$repo_dir/build
    # A build folder configured elsewhere (the limrendir folder was moved) is refused by CMake: configure it again
    if [ -f "$build_dir/CMakeCache.txt" ]; then
        cache_dir=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$build_dir/CMakeCache.txt")
        if [ "$(cd "$cache_dir" 2>/dev/null && pwd -P)" != "$(cd "$build_dir" && pwd -P)" ]; then
            echo "The build folder was configured in $cache_dir: configuring it again here"
            rm -rf "$build_dir/CMakeCache.txt" "$build_dir/CMakeFiles"
        fi
    fi
    # An existing build folder keeps its configuration (build type, options...): it is only compiled again
    if [ ! -f "$build_dir/CMakeCache.txt" ]; then
        if ! cmake -S "$repo_dir" -B "$build_dir"; then
            echo "Configuration failed: a dependency is probably missing (see the messages above, and How to install in" >&2
            echo "README.md)." >&2
            exit 1
        fi
    fi
    if [ -f "$build_dir/limrendir" ] && has_net_raw "$build_dir/limrendir"; then
        had_net_raw=true
    fi
    if ! cmake --build "$build_dir" --parallel "$(nproc 2>/dev/null || echo 2)"; then
        echo "Compilation failed (see the messages above)." >&2
        exit 1
    fi
    binary=$build_dir/limrendir
fi

if [ -z "$binary" ]; then
    for candidate in "$repo_dir/build/limrendir" "$repo_dir/limrendir"; do
        if [ -x "$candidate" ] && [ -f "$candidate" ]; then
            binary=$candidate
            break
        fi
    done
fi
if [ -z "$binary" ] || [ ! -x "$binary" ] || [ ! -f "$binary" ]; then
    echo "limrendir binary not found: compile it first (run the script without --no-build), or give its path." >&2
    exit 1
fi
binary=$(cd "$(dirname "$binary")" && pwd)/$(basename "$binary")

# Packet sockets (Ethernet cameras), a file capability: lost each time the binary is written again
if [ "$setcap" = true ]; then
    echo "Allowing limrendir to use packet sockets (sudo setcap cap_net_raw+ep $binary)"
    sudo setcap cap_net_raw+ep "$binary" || echo "  Failed: limrendir will not use packet sockets." >&2
elif [ "$had_net_raw" = true ] && ! has_net_raw "$binary"; then
    echo "Compiling again removed the permission to use packet sockets (Ethernet cameras): run the script with" >&2
    echo "--setcap to give it again." >&2
fi

data_dir=${XDG_DATA_HOME:-$HOME/.local/share}
applications_dir=$data_dir/applications
entry=$applications_dir/limrendir.desktop

# Application icon, installed in the user icon theme. Without one, a generic camera icon of the system theme.
icon=camera-video
for extension in svg png; do
    if [ -f "$repo_dir/data/icons/limrendir.$extension" ]; then
        if [ "$extension" = svg ]; then
            icon_dir=$data_dir/icons/hicolor/scalable/apps
        else
            icon_dir=$data_dir/icons/hicolor/256x256/apps
        fi
        mkdir -p "$icon_dir"
        cp "$repo_dir/data/icons/limrendir.$extension" "$icon_dir/"
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
