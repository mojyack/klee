SUDO = doas
SHELL = /bin/zsh
LLVM_VERSION=14.0.4
TARGET = x86_64-elf

ifndef BUILDENV
$(warning BUILDENV is not set)
BUILDENV = ../klee-buildenv/x86_64-elf
endif

INCLUDES = -I${BUILDENV}/include -I${BUILDENV}/include/freetype2 -I${BUILDENV}/include/c++/v1 -I$(abspath edk2/MdePkg/Include) -I$(abspath edk2/MdePkg/Include/X64)
LIBRARY = ${BUILDENV}/lib
COMMON_FLAGS = ${INCLUDES} -nostdlibinc -D__ELF__ -D_LDBL_EQ_DBL -D_GNU_SOURCE -D_POSIX_TIMERS -DEFIAPI='__attribute__((ms_abi))'

.PHONY: all run clean

out:
	mkdir out

out/loader.efi: out KleeLoaderPkg/main.c
	cd edk2; \
	if [ ! -e KleeLoaderPkg ]; then ln -s ../KleeLoaderPkg .; fi; \
	if [ ! -e build_rule.txt ]; then ln -s Conf/build_rule.txt .; fi; \
	echo -e "ACTIVE_PLATFORM = KleeLoaderPkg/loader.dsc\nTARGET = DEBUG\nTARGET_ARCH = X64\nTOOL_CHAIN_TAG = CLANGPDB" > Conf/target.txt \
	$(MAKE) -C BaseTools/Source/C; \
	source ./edksetup.sh; \
	build
	cp edk2/Build/loader-X64/DEBUG_CLANGPDB/X64/loader.efi out

out/kernel.elf: src/main.cpp
	clang++ -O0 -Wall -g -ffreestanding -fno-exceptions -std=c++20 --target=${TARGET} ${COMMON_FLAGS} -c -o out/main.o $^
	ld.lld --entry kernel_main -z norelro --image-base 0x100000 --static -L${LIBRARY} -o $@ out/main.o

out/volume:
	scripts/createimage.sh $@

run_prep: out/volume out/loader.efi out/kernel.elf ovmf/OVMF.fd
	mkdir -p out/root/EFI/BOOT
	cp out/loader.efi out/root/EFI/BOOT/BOOTX64.EFI
	cp out/kernel.elf out/root/kernel.elf
	scripts/copyfiles.sh out/volume out/root

run: run_prep
	qemu-system-x86_64 \
	-machine type=q35,accel=kvm \
	-bios ovmf/OVMF.fd \
	-cpu host,kvm=off \
	-display gtk,gl=on \
	-enable-kvm \
	-m 512M \
	-boot order=c \
	-drive file=out/volume,index=0,media=disk,format=raw \
	-usb -device usb-mouse -device usb-kbd \
	-nic none \
    -monitor stdio 

run_debug: run_prep
	qemu-system-x86_64 \
	-machine type=q35,accel=kvm \
	-bios ovmf/OVMF.fd \
	-cpu host,kvm=off \
	-display gtk,gl=on \
	-enable-kvm \
	-m 512M \
	-boot order=c \
	-drive file=out/volume,index=0,media=disk,format=raw \
	-usb -device usb-mouse -device usb-kbd \
	-nic none \
	-gdb tcp::8080 -S &
	lldb out/kernel.elf

clean:
	rm -r out
