CC      ?= gcc
CXX	?= g++

DEBUG  	?= 1
# https://stackoverflow.com/a/1079861
# WAY easier way to build debug and release builds
ifeq ($(DEBUG), 1)
        BUILDDIR  = build/debug
        CFLAGS := -ggdb3 -Wall -Wextra -Wpedantic -Wno-unused-parameter -DDEBUG=1 $(DEBUG_CFLAGS) $(CFLAGS)
else
	# Check if an optimization flag is not already set
	ifneq ($(filter -O%,$(CFLAGS)),)
    		$(info Keeping the existing optimization flag in CFLAGS)
	else
    		CFLAGS := -O3 $(CFLAGS)
	endif
        BUILDDIR  = build/release
endif

NAME		 = game
TARGET		?= $(NAME)
VERSION    	 = 1.0.0
SRC_CC  	 = $(wildcard src/*.c src/scenes/*.c src/scenes/game/*.c)
SRC_CXX		 = $(wildcard src/*.cc)
OBJ_CC  	 = $(SRC_CC:.c=.o)
OBJ_CXX		 = $(SRC_CXX:.cc=.o)
OBJ		 = $(OBJ_CC) $(OBJ_CXX)
LDFLAGS   	+= -L$(BUILDDIR) -Wl,-rpath,$(BUILDDIR)
LDLIBS		+= -lm -lSDL3 -lSDL3_ttf -lSDL3_image -lGameNetworkingSockets -lassimp -lcglm -lstdc++
CFLAGS  	?= -mtune=generic -march=native
CFLAGS		+= -fvisibility=hidden -std=c23 -Iinclude -Iinclude/cglm -IGameNetworkingSockets/include $(VARS) -DVERSION=\"$(VERSION)\"
CXXFLAGS	 = $(CFLAGS)

SHADER_DIR 	 = shaders
SHADERS 	 = $(wildcard $(SHADER_DIR)/*.vert $(SHADER_DIR)/*.frag)
SPV 		 = $(SHADERS:.vert=.vert.spv)
SPV 		:= $(SPV:.frag=.frag.spv)

all: gamenetworkingsockets shaders $(TARGET)

gamenetworkingsockets:
ifeq ($(wildcard $(BUILDDIR)/libGameNetworkingSockets.so),)
	mkdir -p $(BUILDDIR)/GameNetworkingSockets
	mkdir -p GameNetworkingSockets/build
	cd GameNetworkingSockets && patch -p1 < ../fix-string_view-return.patch
	cmake -S GameNetworkingSockets/ -B GameNetworkingSockets/build
	cmake --build GameNetworkingSockets/build
	cd GameNetworkingSockets && patch -p1 -R < ../fix-string_view-return.patch
	cp GameNetworkingSockets/build/bin/libGameNetworkingSockets.so $(BUILDDIR)
endif

$(TARGET): shaders gamenetworkingsockets $(OBJ)
	mkdir -p $(BUILDDIR)
	$(CC) $(OBJ) -o $(BUILDDIR)/$(TARGET) $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(BUILDDIR)/$(TARGET) $(OBJ)

shaders: $(SPV)
%.vert.spv: %.vert
	glslangValidator -V $< -o $@

%.frag.spv: %.frag
	glslangValidator -V $< -o $@

.PHONY: $(TARGET) clean gamenetworkingsockets all
