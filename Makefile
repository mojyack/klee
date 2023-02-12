SHELL        = /bin/zsh
TARGET       = x86_64-elf
INCLUDES     = $(addprefix -I, \
                   $(BUILDENV)/include/c++/v1 \
                   $(BUILDENV)/include  \
                   $(BUILDENV)/include/freetype2 \
                   $(abspath edk2/MdePkg/Include) \
                   $(abspath edk2/MdePkg/Include/X64) \
               )

COMMON_FLAGS = -O3 -march=x86-64-v2 -masm=intel --target=$(TARGET) \
               -nostdlibinc -ffreestanding -mlong-double-64 \
               -U__linux__ -D__ELF__ -D_GNU_SOURCE -D_POSIX_TIMERS -DEFIAPI='__attribute__((ms_abi))' \
			   -Wall -Wfatal-errors

CXX = clang++ $(INCLUDES) $(COMMON_FLAGS) \
	  -fno-exceptions -mno-red-zone -fno-rtti -std=c++20 \
	  -Wno-deprecated-experimental-coroutine

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
	${CXX} -c -o $@ $<

out/asmcode.o: src/asmcode.asm
	nasm -f elf64 -o $@ $<

out/process.o: src/process/process.asm
	nasm -f elf64 -o $@ $<

out/trampoline.o: src/smp/trampoline.asm
	nasm -f elf64 -o $@ $<

out/libc-support.o: src/libc-support.cpp
	${CXX} -c -o $@ $<

out/main.o: src/main.cpp $(shell find src/ -type f -name '*.hpp') $(shell find src/ -type f -name '*.h')
	${CXX} -c -o $@ $<

out/font.o: src/font.txt
	mkdir -p out
	python scripts/makefont.py -o out/font.bin $<
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 out/font.bin $@
	objcopy \
	--redefine-sym _binary_out_font_bin_start=font_start \
	--redefine-sym _binary_out_font_bin_end=font_end \
	--redefine-sym _binary_out_font_bin_size=font_limit \
	$@ $@

out/kernel.elf: out/main.o out/stub.o out/font.o out/asmcode.o out/process.o out/trampoline.o out/libc-support.o
	ld.lld --entry kernel_entry -z norelro --image-base 0x100000 --static -lc -lm -lc++ -lc++abi -L${LIBRARY} -o $@ $^

out/volume:
	mkdir -p out
	scripts/createimage.sh $@

apps:
	mkdir -p out/apps
	$(MAKE) -C apps INCLUDES="$(INCLUDES)" COMMON_FLAGS="$(COMMON_FLAGS)" LIBRARY="$(LIBRARY)" all

run_prep: out/volume out/loader.efi out/kernel.elf apps ovmf/OVMF.fd
	mkdir -p out/root/EFI/BOOT
	cp out/loader.efi out/root/EFI/BOOT/BOOTX64.EFI
	cp out/kernel.elf out/root/kernel.elf
	cp src/main.cpp out/root/source
	mkdir -p out/root/apps
	cp -r out/apps/* out/root/apps
	scripts/copyfiles.sh out/volume out/root

run: run_prep
	scripts/run-qemu.sh

clean:
	rm -r out
