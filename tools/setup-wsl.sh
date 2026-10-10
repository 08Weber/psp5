#!/bin/bash
# Installerar pspdev-verktygskedjan + psp-cfw-sdk i WSL-distron "pspdev".
# Kör från Windows:  wsl -d pspdev -u root -- bash /mnt/c/path/to/psp5/tools/setup-wsl.sh
set -e
PSPDEV_VERSION=v20261001
export PSPDEV=/usr/local/pspdev

if [ ! -x "$PSPDEV/bin/psp-gcc" ]; then
    echo "== Laddar ner pspdev $PSPDEV_VERSION"
    curl -sL -o /tmp/pspdev.tgz \
        "https://github.com/pspdev/pspdev/releases/download/$PSPDEV_VERSION/pspdev-ubuntu-latest-x86_64.tar.gz"
    tar -xzf /tmp/pspdev.tgz -C /usr/local
    rm /tmp/pspdev.tgz
fi

cat > /etc/profile.d/pspdev.sh <<'EOF'
export PSPDEV=/usr/local/pspdev
export PATH=$PATH:$PSPDEV/bin
EOF
. /etc/profile.d/pspdev.sh

echo "== Verktygskedja"
psp-gcc --version | head -1

echo "== Paket"
for pkg in psp-cfw-sdk libpng libintrafont zlib; do
    if psp-pacman -Q "$pkg" >/dev/null 2>&1; then
        echo "$pkg: redan installerat"
    else
        psp-pacman -S --noconfirm "$pkg"
    fi
done
psp-pacman -Q psp-cfw-sdk libpng libintrafont zlib
