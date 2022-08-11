SHELL = /bin/zsh
LLVM_VERSION = 14.0.4
TARGET = x86_64-elf
INCLUDES = $(addprefix -I, $(BUILDENV)/include $(BUILDENV)/include/freetype2 $(BUILDENV)/include/c++/v1 $(abspath edk2/MdePkg/Include) $(abspath edk2/MdePkg/Include/X64))
COMMON_FLAGS = -O3 -Wall --target=$(TARGET) -nostdlibinc -ffreestanding -U__linux__ -D__ELF__ -D_LDBL_EQ_DBL -D_GNU_SOURCE -D_POSIX_TIMERS -DEFIAPI='__attribute__((ms_abi))'
CXX = clang++ -fno-exceptions -mno-red-zone -fno-rtti -std=c++20 -Wno-address-of-packed-member -march=x86-64-v2 $(INCLUDES) $(COMMON_FLAGS) -c
LIBRARY = $(BUILDENV)/lib

ifndef BUILDENV
$(warning BUILDENV is not set)
BUILDENV = $(abspath ../klee-buildenv/x86_64-elf)
endif

.PHONY: all run apps clean

all: run

out/loader.efi: KleeLoaderPkg/main.c KleeLoaderPkg/elf.c KleeLoaderPkg/memory.c
	mkdir -p out
	cd edk2; \
	if [ ! -e KleeLoaderPkg ]; then ln -s ../KleeLoaderPkg .; fi; \
	if [ ! -e build_rule.txt ]; then ln -s Conf/build_rule.txt .; fi; \
	echo -e "ACTIVE_PLATFORM = KleeLoaderPkg/loader.dsc\nTARGET = DEBUG\nTARGET_ARCH = X64\nTOOL_CHAIN_TAG = CLANGPDB" > Conf/target.txt; \
	$(MAKE) -C BaseTools/Source/C; \
	source ./edksetup.sh; \
	build
	cp edk2/Build/loader-X64/DEBUG_CLANGPDB/X64/loader.efi out

out/stub.o: src/stub.cpp
	${CXX} -o $@ $<

out/asmcode.o: src/asmcode.asm src/asmcode.h
	nasm -f elf64 -o $@ $<

out/libc-support.o: src/libc-support.cpp
	${CXX} -o $@ $<

out/main.o: src/main.cpp src
	${CXX} -o $@ $<

out/font.o: src/font.txt
	mkdir -p out
	python scripts/makefont.py -o out/font.bin $<
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 out/font.bin $@
	objcopy \
	--redefine-sym _binary_out_font_bin_start=font_start \
	--redefine-sym _binary_out_font_bin_end=font_end \
	--redefine-sym _binary_out_font_bin_size=font_limit \
	$@ $@

out/kernel.elf: out/main.o out/stub.o out/font.o out/asmcode.o out/libc-support.o
	ld.lld --entry kernel_entry -z norelro --image-base 0x100000 --static -lc -lm -lc++ -lc++abi -L${LIBRARY} -o $@ $^

out/volume:
	mkdir -p out
	scripts/createimage.sh $@

apps:
	$(MAKE) -C apps INCLUDES="$(INCLUDES)" COMMON_FLAGS="$(COMMON_FLAGS)" all

run_prep: out/volume out/loader.efi out/kernel.elf apps ovmf/OVMF.fd
	mkdir -p out/root/EFI/BOOT
	cp out/loader.efi out/root/EFI/BOOT/BOOTX64.EFI
	cp out/kernel.elf out/root/kernel.elf
	mkdir -p out/root/apps
	cp -r out/apps/* out/root/apps
	scripts/copyfiles.sh out/volume out/root

run: run_prep
	SDL_VIDEODRIVER=wayland \
	qemu-system-x86_64 \
	-machine type=q35,accel=kvm \
	-bios ovmf/OVMF.fd \
	-cpu host,kvm=off \
	-display sdl \
	-enable-kvm \
	-m 512M \
	-boot order=c \
	-device virtio-gpu,xres=1280,yres=1024 \
	-drive file=out/volume,index=0,media=disk,format=raw \
	-device qemu-xhci \
	-device usb-mouse -device usb-kbd \
	-nic none \
	-monitor stdio \
	-d guest_errors \
	-d int \
	-no-reboot -no-shutdown
	#-vga virtio \

run_debug: run_prep
	SDL_VIDEODRIVER=wayland \
	qemu-system-x86_64 \
	-machine type=q35,accel=kvm \
	-bios ovmf/OVMF.fd \
	-cpu host,kvm=off \
	-display sdl \
	-enable-kvm \
	-m 512M \
	-boot order=c \
	-device virtio-gpu,xres=1280,yres=1024 \
	-drive file=out/volume,index=0,media=disk,format=raw \
	-device qemu-xhci \
	-device usb-mouse -device usb-kbd \
	-nic none \
	-gdb tcp::8080 -S & \
	lldb out/kernel.elf

clean:
	rm -r out
