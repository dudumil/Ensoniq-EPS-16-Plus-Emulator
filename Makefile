CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wno-unused-function
CFLAGS ?= -O2
INC = -Imusashi -Isrc
MUSASHI_OBJ = build/m68kcpu.o build/m68kops.o build/m68kdasm.o build/softfloat.o
CORE_OBJ = build/eps16.o build/duart.o build/wd1772.o build/hd63450.o build/es5505.o build/es5510.o build/panel.o build/scsi.o build/efe_disk.o
all: eps16-cli
build:
	mkdir -p build
build/m68kcpu.o: musashi/m68kcpu.c | build
	$(CC) $(CFLAGS) $(INC) -c $< -o $@
build/m68kops.o: musashi/m68kops.c | build
	$(CC) $(CFLAGS) $(INC) -c $< -o $@
build/m68kdasm.o: musashi/m68kdasm.c | build
	$(CC) $(CFLAGS) $(INC) -c $< -o $@
build/softfloat.o: musashi/softfloat/softfloat.c | build
	$(CC) $(CFLAGS) $(INC) -Imusashi/softfloat -c $< -o $@
build/%.o: src/%.cpp src/*.h | build
	$(CXX) $(CXXFLAGS) $(INC) -c $< -o $@
eps16-cli: $(MUSASHI_OBJ) $(CORE_OBJ) build/cli.o
	$(CXX) -o $@ $^
clean:
	rm -rf build eps16-cli
