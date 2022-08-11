.PHONY: all

all: $(OUT)/hlt

$(OUT)/hlt: hlt.asm Makefile
	nasm -f bin -o $@ $<
