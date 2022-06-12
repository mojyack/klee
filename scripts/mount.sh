#!/bin/zsh

if [[ $# != 3 ]] {
    echo "usage: mount.sh 1 IMAGE MOUNTPOINT"
    echo "       mount.sh 0 DEVICE MOUNTPOINT"
    exit 1
}

if [[ $1 == 1 ]] {
    if [[ -e $3 ]] {
        echo "mount point exists"
        exit 1
    }
    dev=$(losetup -f)
    echo $dev
    doas losetup -P -f "$2"
    mkdir $3
    doas mount -o uid=$(id -u),gid=$(id -g) "${dev}p1" "$3"
} elif [[ $1 == 0 ]] {
    doas umount $3
    rmdir $3
    doas losetup -d "$2"
}
