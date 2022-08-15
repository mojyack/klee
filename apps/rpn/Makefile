COMMON_FLAGS = $(COMMON_FLAGS) -g
CFLAGS 		 = $(COMMON_FLAGS)
CXXFLAGS 	 = $(COMMON_FLAGS) -fno-exceptions -fno-rtti -std=c++20 -mcmodel=large
LDFLAGS 	 = --entry start -z norelro --image-base 0xFFFF800000000000 --static

.PHONY: all

all: $(OUT)/rpn

$(BUILD)/rpn.o: main.cpp Makefile
	clang++ $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD)/syscall.o: syscall.asm Makefile
	nasm -f elf64 -o $@ $<

$(OUT)/rpn: $(BUILD)/rpn.o $(BUILD)/syscall.o
	ld.lld $(LDFLAGS) -o $@ $^
