# Makefile for libc module
include ../Makefile.inc

OBJ := ../../bin/obji386

TARGETOBJ := \
	$(OBJ)/stdio.o \
	$(OBJ)/string.o \
	$(OBJ)/div64.o \
	$(OBJ)/vsprintf.o \
	$(OBJ)/sprintf.o \
	$(OBJ)/format.o \

.PHONY: clean help

all: $(TARGETOBJ)

$(OBJ)/%.o: ../../sdk/libc/%.c
	$(CC) -m32 -DBITS_PER_LONG=32 -g -I../include -I../../sdk/include $(CFLAGS_global) -c -o $@ $<

$(OBJ)/%.o: %.c
	$(CC) -m32 -DBITS_PER_LONG=32 -g -I../include -I../../sdk/include $(CFLAGS_global) -c -o $@ $<

clean:
	$(RM) $(TARGETOBJ)
