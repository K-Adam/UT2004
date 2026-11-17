#!/bin/sh
# Installs the unpacked archive for the current user, or for everyone with --system as root.
# Settings, saves and downloaded game content stay in ~/.ut2004 of each player.
set -eu

src=$(cd "$(dirname "$0")" && pwd)
if [ ! -x "$src/System/ut2004-bin" ]; then
  echo "Run this from the unpacked UT2004 archive." >&2
  exit 1
fi

case "${1:-}" in
  --system)
    if [ "$(id -u)" != 0 ]; then
      echo "--system installs to /opt/UT2004 and needs root, run it with sudo." >&2
      exit 1
    fi
    dir=/opt/UT2004 bin=/usr/local/bin share=/usr/local/share ;;
  "")
    share=${XDG_DATA_HOME:-$HOME/.local/share}
    dir=$share/UT2004 bin=$HOME/.local/bin ;;
  *)
    echo "Usage: $0 [--system]" >&2
    exit 1 ;;
esac

missing=$(cd "$src/System" && ldd ut2004-bin ./*.so 2>/dev/null | awk '/not found/ { print $1 }' | sort -u)
if [ -n "$missing" ]; then
  echo "The game needs these libraries, install them with your package manager:"
  echo "$missing" | sed 's/^/  /'
  echo "README.md lists what they belong to."
fi

apps=$share/applications icons=$share/icons/hicolor/48x48/apps
if [ "$src" = "$dir" ]; then
  echo "The archive is unpacked where the game gets installed, unpack it somewhere else." >&2
  exit 1
fi
# Only the program lives here, so an older version can go.
rm -rf "$dir"
mkdir -p "$dir" "$bin" "$apps" "$icons"
cp -R "$src/System" "$src/Help" "$src/KarmaData" "$src/Web" "$src/README.md" "$src/NOTICE.md" "$src/LICENSE" "$dir/"
cp "$src/ut2004.png" "$icons/ut2004.png"

cat > "$bin/ut2004" <<EOF
#!/bin/sh
# The game finds its files relative to the System folder.
cd "$dir/System" && exec ./ut2004-bin "\$@"
EOF
chmod +x "$bin/ut2004"

cat > "$apps/ut2004.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Unreal Tournament 2004
Exec="$bin/ut2004"
Icon=ut2004
Categories=Game;ActionGame;
Terminal=false
EOF

cat > "$dir/uninstall.sh" <<EOF
#!/bin/sh
set -eu
rm -f "$bin/ut2004" "$apps/ut2004.desktop" "$icons/ut2004.png"
rm -rf "$dir"
command -v update-desktop-database >/dev/null && update-desktop-database "$apps" || true
echo "UT2004 is removed."
if [ -d "\$HOME/.ut2004" ]; then
  echo "Your settings, saves and downloaded game content are still in \$HOME/.ut2004, delete that folder to remove them too."
fi
EOF
chmod +x "$dir/uninstall.sh"
command -v update-desktop-database >/dev/null && update-desktop-database "$apps" || true

echo "UT2004 is installed in $dir. Remove it with $dir/uninstall.sh"
echo "Start it with $bin/ut2004 or from the applications menu."
echo "The first start asks where the game content comes from: a download or the folder of a UT2004 install."
# A game started as root would keep its settings in root's home.
if [ "$(id -u)" != 0 ] && [ -t 0 ]; then
  printf "Start it now? [Y/n] "
  read -r answer
  case "$answer" in
    n*|N*) ;;
    *) exec "$bin/ut2004" ;;
  esac
fi
