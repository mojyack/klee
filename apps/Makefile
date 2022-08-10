OUT = ../out/apps
TARGET = $(OUT)/hlt
.PHONY: all

$(OUT):
	mkdir -p $@

all: $(OUT) $(TARGET)

$(OUT)/hlt: hlt.asm Makefile
	nasm -f bin -o $@ $<
