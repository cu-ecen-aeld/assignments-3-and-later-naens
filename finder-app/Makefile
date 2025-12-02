.PHONY:	all clean

ifndef CROSS_COMPILE
GCC := gcc
else
GCC := aarch64-none-linux-gnu-gcc
endif

all: writer

writer: writer.c
	$(GCC) writer.c -o writer

clean:
	rm -f writer
