#!/bin/zsh
export SDL_VIDEODRIVER=wayland

args=(
    -machine type=q35,accel=kvm
    -enable-kvm
    -cpu host,kvm=off
    -smp cores=2,threads=2,sockets=1,maxcpus=4
    -m 512M

    -bios ovmf/OVMF.fd
    -display sdl

    -boot order=c
    -device virtio-gpu,xres=1280,yres=1024
    -drive file=out/volume,index=0,media=disk,format=raw
    -device qemu-xhci
    -device usb-mouse -device usb-kbd
    -nic none
    -monitor stdio

    -d guest_errors
    -d int
    -no-reboot -no-shutdown
)

exec qemu-system-x86_64 "${args[@]}"
