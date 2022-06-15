SHELL = /bin/zsh
LLVM_VERSION = 14.0.4
TARGET = x86_64-elf
INCLUDES = -I${BUILDENV}/include -I${BUILDENV}/include/freetype2 -I${BUILDENV}/include/c++/v1 -I$(abspath edk2/MdePkg/Include) -I$(abspath edk2/MdePkg/Include/X64)
COMMON_FLAGS = -nostdlibinc -D__ELF__ -D_LDBL_EQ_DBL -D_GNU_SOURCE -D_POSIX_TIMERS -DEFIAPI='__attribute__((ms_abi))'
CXX = clang++ -O3 -Wall -ffreestanding -fno-exceptions -mno-red-zone -fno-rtti -std=c++20 --target=${TARGET} ${INCLUDES} ${COMMON_FLAGS} -c
LIBRARY = ${BUILDENV}/lib

ifndef BUILDENV
$(warning BUILDENV is not set)
BUILDENV = ../klee-buildenv/x86_64-elf
endif

.PHONY: all run clean

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

out/main.o: src/main.cpp src/framebuffer-forward.h src/framebuffer.hpp src/type.hpp src/console.hpp src/mousecursor.hpp src/error.hpp src/pci.hpp
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

out/kernel.elf: out/main.o out/stub.o out/font.o out/asmcode.o
	ld.lld --entry kernel_main -z norelro --image-base 0x100000 --static -L${LIBRARY} -lc -o $@ $^

out/volume:
	mkdir -p out
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
