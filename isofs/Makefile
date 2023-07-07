include $(PS2SDK)/Defs.make

IRX_NAME = isofs
IRX_TARGET = $(IRX_NAME).irx

IOP_CPPFLAGS = -miop -O2 -G0 -I$(PS2SDK)/iop/include -I$(PS2SDK)/common/include -I./include -I./src -Wall -fno-builtin-printf -DIRX_NAME=$(IRX_NAME)

IRX_OBJS = isofs.o fsdriver.o
IRX_OBJS := $(IRX_OBJS:%=objs/%)

all: $(IRX_TARGET)

objs/%.o: src/%.c
	$(IOP_CC) $(IOP_CFLAGS) $(IOP_CPPFLAGS) -c $< -o $@

objs/%.o: src/%.cpp
	$(IOP_CXX) $(IOP_CXXFLAGS) $(IOP_CPPFLAGS) -c $< -o $@

objs/%.o: src/%.S
	$(IOP_CC) $(IOP_CPPFLAGS) -c $< -o $@

objs/%.o: src/%.s
	$(IOP_AS) $(IOP_ASFLAGS) -c $< -o $@

# A rule to build exports.tab.
objs/%.o : src/%.tab
	echo "#include \"irx.h\"" > objs/build-exports.c
	cat $< >> objs/build-exports.c
	$(IOP_CC) $(IOP_CFLAGS) $(IOP_CPPFLAGS) -Isrc/ -c objs/build-exports.c -o $@
	-rm -f objs/build-exports.c

$(IRX_TARGET): $(IRX_OBJS) src/imports.lst src/irx_imports.h
	echo "#include \"irx_imports.h\"" > src/build-imports.c
	cat src/imports.lst >> src/build-imports.c
	$(IOP_CC) $(IOP_CFLAGS) $(IOP_CPPFLAGS) -c src/build-imports.c -o objs/imports.o
	rm -f src/build-imports.c
	$(IOP_CC) $(IOP_LDFLAGS) -miop -nostdlib -s -o libs/$(IRX_TARGET) $(IRX_OBJS) objs/imports.o

clean:
	rm -f objs/*.o libs/$(IRX_TARGET)

rebuild: clean all
