#!/usr/bin/env bash
set -euo pipefail

VM_NAME="Vex-OS"
SKIP_BUILD=0

for arg in "$@"; do
    case "$arg" in
        --vm-name=*) VM_NAME="${arg#--vm-name=}" ;;
        --skip-build) SKIP_BUILD=1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ISO_PATH="$ROOT/build/vex-os.iso"
SERIAL_LOG="$ROOT/build/vex-serial.log"

# Charge vboxdrv si absent
if ! lsmod | grep -q vboxdrv; then
    echo "Chargement du module vboxdrv..."
    sudo modprobe vboxdrv || {
        echo "Erreur : impossible de charger vboxdrv." >&2
        echo "Installe linux-zen-headers + virtualbox-host-dkms puis relance." >&2
        exit 1
    }
fi

if ! command -v VBoxManage &>/dev/null; then
    echo "Erreur : 'VBoxManage' introuvable dans PATH." >&2
    exit 1
fi

if [ "$SKIP_BUILD" -eq 0 ]; then
    pwsh -NoProfile -File "$SCRIPT_DIR/build.ps1"
elif [ ! -f "$ISO_PATH" ]; then
    echo "Erreur : ISO introuvable à '$ISO_PATH'. Lance sans --skip-build pour compiler." >&2
    exit 1
fi

EXISTING_VM=$(VBoxManage list vms | grep "\"$VM_NAME\"" || true)

if [ -z "$EXISTING_VM" ]; then
    VBoxManage createvm --name "$VM_NAME" --ostype Other_64 --register > /dev/null
    VBoxManage modifyvm "$VM_NAME" \
        --memory 512 --vram 128 \
        --firmware efi \
        --graphicscontroller vmsvga \
        --audio-enabled on --audio-controller hda --audio-in on --audio-out on \
        --nic1 nat --nictype1 82540EM --cableconnected1 on \
        --boot1 dvd --boot2 none --boot3 none --boot4 none
    VBoxManage storagectl "$VM_NAME" --name "IDE" --add ide --controller PIIX4
else
    VBoxManage modifyvm "$VM_NAME" \
        --memory 512 --vram 128 \
        --firmware efi \
        --graphicscontroller vmsvga \
        --audio-enabled on --audio-controller hda --audio-in on --audio-out on \
        --nic1 nat --nictype1 82540EM --cableconnected1 on
fi

[ -f "$SERIAL_LOG" ] && rm -f "$SERIAL_LOG"
VBoxManage modifyvm "$VM_NAME" --uart1 0x3F8 4 --uartmode1 file "$SERIAL_LOG"

VBoxManage setextradata "$VM_NAME" CustomVideoMode1 1920x1080x32
VBoxManage setextradata "$VM_NAME" VBoxInternal2/EfiGraphicsResolution 1920x1080
VBoxManage setextradata global GUI/MaxGuestResolution any

VBoxManage storageattach "$VM_NAME" \
    --storagectl "IDE" \
    --port 0 \
    --device 0 \
    --type dvddrive \
    --medium "$ISO_PATH"

VBoxManage startvm "$VM_NAME"
sleep 1
VBoxManage controlvm "$VM_NAME" setvideomodehint 1920 1080 32 0
