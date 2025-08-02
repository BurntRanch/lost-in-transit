CC      ?= gcc
CXX	?= g++
GLSLC	?= glslc

UNAME_S := $(shell uname -s)
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
# is macos?
ifeq ($(UNAME_S),Darwin)
    LIBNAME     := dylib
else ifeq ($(UNAME_S),Linux)
    LIBNAME     := so
else
    LIBNAME     := dll
    GNS_FLAGS   := -DCMAKE_PREFIX_PATH=/mingw64 \
      -DOPENSSL_ROOT_DIR=/mingw64 \
      -DCMAKE_SYSTEM_NAME=Windows \
      -DProtobuf_PROTOC_EXECUTABLE=/mingw64/bin/protoc.exe \
      -DOPENSSL_USE_STATIC_LIBS=ON \
      -DCMAKE_C_COMPILER=/mingw64/bin/gcc.exe \
      -DCMAKE_CXX_COMPILER=/mingw64/bin/g++.exe \
      -G "Ninja"
endif

gamenetworkingsockets:
ifeq ($(wildcard $(BUILDDIR)/libGameNetworkingSockets.$(LIBNAME)),)
	mkdir -p $(BUILDDIR)
	mkdir -p GameNetworkingSockets/build
	cd GameNetworkingSockets && patch -p1 < ../fix-string_view-return.patch
	cmake -S GameNetworkingSockets/ -B GameNetworkingSockets/build $(GNS_FLAGS)
	cmake --build GameNetworkingSockets/build --config Release
	cd GameNetworkingSockets && patch -p1 -R < ../fix-string_view-return.patch
	cp GameNetworkingSockets/build/bin/Release/GameNetworkingSockets.$(LIBNAME) $(BUILDDIR) || \
	cp GameNetworkingSockets/build/libGameNetworkingSockets.$(LIBNAME) $(BUILDDIR)
endif

$(TARGET): shaders gamenetworkingsockets $(OBJ)
	mkdir -p $(BUILDDIR)
	$(CC) $(OBJ) -o $(BUILDDIR)/$(TARGET) $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(BUILDDIR)/$(TARGET) $(OBJ)

shaders:
	for f in $(SHADERS); do $(GLSLC) $$f -o $$f.spv; done

.PHONY: $(TARGET) clean gamenetworkingsockets shaders all
