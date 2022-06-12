#!/bin/zsh

if [[ $# != 1 ]] {
    exit 1
}

fallocate -l 128M "$1"
sgdisk -n 1:0: -t 1:ef00 -c 1:"EFI System" "$1"

dev=$(losetup -f)
doas losetup -P -f "$1"
doas mkfs.vfat -F 32 "${dev}p1"
doas losetup -d "$dev"
