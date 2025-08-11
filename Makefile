CC      ?= gcc
CXX	?= g++
GLSLC	?= glslc

UNAME_S := $(shell uname -s)
DEBUG  	?= 1
# https://stackoverflow.com/a/1079861
# WAY easier way to build debug and release builds
ifeq ($(DEBUG), 1)
        BUILDDIR  = build/debug
        CFLAGS := -ggdb3 -fsanitize=undefined -Wall -Wextra -Wpedantic -Wno-unused-parameter -DDEBUG=1 $(DEBUG_CFLAGS) $(CFLAGS)
				LDFLAGS := -fsanitize=undefined
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
SHADERS 	 = $(wildcard $(SHADER_DIR)/untextured/*.vert $(SHADER_DIR)/untextured/*.frag $(SHADER_DIR)/textured/*.vert $(SHADER_DIR)/textured/*.frag)
SPV 		 = $(SHADERS:.vert=.vert.spv)
SPV 		:= $(SPV:.frag=.frag.spv)

# is macos?
ifeq ($(UNAME_S),Darwin)
    LIBNAME     := dylib
else ifeq ($(UNAME_S),Linux)
    LIBNAME     := so
else
    LIBNAME     := dll
    MINGW_FLAGS := -DCMAKE_SYSTEM_NAME=Windows \
      -DProtobuf_PROTOC_EXECUTABLE=/mingw64/bin/protoc.exe \
      -DOPENSSL_USE_STATIC_LIBS=ON \
      -G "Ninja"
endif

all: gamenetworkingsockets shaders $(TARGET)

gamenetworkingsockets:
ifeq ($(wildcard $(BUILDDIR)/libGameNetworkingSockets.$(LIBNAME)),)
	mkdir -p $(BUILDDIR)
	mkdir -p GameNetworkingSockets/build
	cd GameNetworkingSockets && patch -p1 < ../fix-string_view-return.patch
	cmake -S GameNetworkingSockets/ -B GameNetworkingSockets/build -DBUILD_STATIC_LIB=OFF $(MINGW_FLAGS)
	cmake --build GameNetworkingSockets/build --config Release
	cd GameNetworkingSockets && patch -p1 -R < ../fix-string_view-return.patch
	cp GameNetworkingSockets/build/bin/libGameNetworkingSockets.$(LIBNAME) $(BUILDDIR)
endif

$(TARGET): shaders gamenetworkingsockets $(OBJ)
	mkdir -p $(BUILDDIR)
	$(CC) $(OBJ) -o $(BUILDDIR)/$(TARGET) $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(BUILDDIR)/$(TARGET) $(OBJ)

shaders:
	for f in $(SHADERS); do $(GLSLC) -I shaders/ $$f -o $$f.spv; done

.PHONY: $(TARGET) clean gamenetworkingsockets shaders all
