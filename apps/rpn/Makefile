COMMON_FLAGS = $(COMMON_FLAGS) -g
CFLAGS 		 = $(COMMON_FLAGS)
CXXFLAGS 	 = $(COMMON_FLAGS) -fno-exceptions -fno-rtti -std=c++20 -mcmodel=large
LDFLAGS 	 = --entry start -z norelro --image-base 0xFFFF800000000000 --static -lc -lm -lc++ -lc++abi $(addprefix -L,$(LIBRARY))

OBJS = main.o libc-support.o

.PHONY: all

all: $(OUT)/rpn

$(BUILD)/syscall.o: syscall.asm Makefile
	nasm -f elf64 -o $@ $<

$(BUILD)/%.o: %.cpp Makefile
	clang++ $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OUT)/rpn: $(addprefix $(BUILD)/, $(OBJS)) $(BUILD)/syscall.o
	ld.lld $(LDFLAGS) -o $@ $^
