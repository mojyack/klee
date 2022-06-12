#!/bin/zsh

if [[ $# != 2 ]] {
    echo "usage: copyfiles.sh IMAGE ROOT"
    exit 1
}

mnt=/tmp/mnt
if [[ -e $mnt ]] {
    echo "mount point exists"
    exit 1
}

dev=$(losetup -f)
doas losetup -P -f "$1"
mkdir $mnt
doas mount -o uid=$(id -u),gid=$(id -g) "${dev}p1" $mnt
cp -r "$2"/* $mnt
doas umount $mnt
rmdir $mnt
doas losetup -d "$dev"
