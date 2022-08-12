COMMON_FLAGS = $(COMMON_FLAGS) -g
CFLAGS 		 = $(COMMON_FLAGS)
CXXFLAGS 	 = $(COMMON_FLAGS) -fno-exceptions -fno-rtti -std=c++20 -mcmodel=large
LDFLAGS 	 = --entry main -z norelro --image-base 0xFFFF800000000000 --static

.PHONY: all

all: $(OUT)/rpn

$(BUILD)/rpn.o: main.cpp Makefile
	clang++ $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OUT)/rpn: $(BUILD)/rpn.o
	ld.lld $(LDFLAGS) -o $@ $<
