#!/bin/sh
# Run ON THE GPD (sudo): install our Mesa packages built on the desktop (mesa/build.sh, mesa/build.sh lib32).
# They are named NAME-gpd and provide/conflict NAME (mesa-gpd replaces mesa etc.), so Arch updates of NAME do
# not overwrite them; `scripts/aur check` reports when Arch has a newer NAME. Before replacing an Arch package,
# its package file is saved to /var/cache/gpd-mesa-stock for rollback.sh (paccache may clean the pacman cache).
#   mesa/install.sh /path/to/mesa-gpd-1:26.2.4-1.3-x86_64.pkg.tar.zst [more packages...]
# Takes effect for programs started afterwards (sway keeps the old Mesa until it restarts / reboot).
set -e
[ $# -gt 0 ] || { echo "usage: $0 PKG.pkg.tar.zst..."; exit 1; }
sudo mkdir -p /var/cache/gpd-mesa-stock
for pkg in "$@"; do
    [ -f "$pkg" ] || { echo "no such file: $pkg"; exit 1; }
    name=$(pacman -Qqp "$pkg"); base=${name%-gpd}
    cur=$(pacman -Q "$base" 2>/dev/null | cut -d' ' -f2) || true
    case "$cur" in
    "") ;;                                  # base not installed (already replaced by our -gpd package)
    *-*.*) echo "installed $base $cur is an earlier GPD build; Arch's package was saved before";;
    *)  f=/var/cache/pacman/pkg/$base-$cur-x86_64.pkg.tar.zst
        if [ ! -f /var/cache/gpd-mesa-stock/$(basename "$f") ]; then
            [ -f "$f" ] || { echo "Arch's $f not in the pacman cache: download it first (rollback needs it)"; exit 1; }
            sudo cp "$f" /var/cache/gpd-mesa-stock/
            [ ! -f "$f.sig" ] || sudo cp "$f.sig" /var/cache/gpd-mesa-stock/
        fi;;
    esac
done
ls /var/cache/gpd-mesa-stock/
# --ask=4: answer yes to "remove conflicting package NAME?" (mesa-gpd conflicts with mesa)
sudo pacman -U --noconfirm --ask=4 "$@"
for pkg in "$@"; do pacman -Q "$(pacman -Qqp "$pkg")"; done
