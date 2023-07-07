
IOP_OBJS_DIR = obj/
IOP_BIN_DIR = bin/
IOP_SRC_DIR = src/
IOP_INC_DIR = include/

IRX_NAME = isofs

IOP_BIN = $(IRX_NAME).irx
IOP_OBJS = isofs.o fsdriver.o imports.o

IOP_CFLAGS += -I./include -I./src -Wall -DIRX_NAME=$(IRX_NAME)

all: $(IOP_BIN)

clean:
	rm -f -r $(IOP_OBJS_DIR) $(IOP_BIN_DIR)

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal
