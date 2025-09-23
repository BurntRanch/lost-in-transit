CC      ?= gcc
CXX	?= g++
GLSLC	?= glslc
CSTD	?= c23
CXXSTD  ?= c++20

UNAME_S := $(shell uname -s)
DEBUG  	?= 1
# https://stackoverflow.com/a/1079861
# WAY easier way to build debug and release builds
ifeq ($(DEBUG), 1)
        BUILDDIR  = build/debug
        CFLAGS := -ggdb3 -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-newline-eof -DDEBUG=1 $(DEBUG_CFLAGS) $(CFLAGS)
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
VERSION    	 = 0.1.2
SRC_CC  	 = $(wildcard src/*.c src/scenes/*.c src/scenes/game/*.c)
SRC_CXX		 = $(wildcard src/*.cc)
OBJ_CC  	 = $(SRC_CC:.c=.o)
OBJ_CXX		 = $(SRC_CXX:.cc=.o)
OBJ		 = $(OBJ_CC) $(OBJ_CXX)
LDFLAGS   	+= -L$(BUILDDIR) $(shell pkg-config --libs-only-L --libs-only-other sdl3 sdl3-ttf sdl3-image)
LDLIBS		+= $(BUILDDIR)/libassimp.a $(BUILDDIR)/libGameNetworkingSockets.a $(shell pkg-config --libs-only-l sdl3 sdl3-ttf sdl3-image) -lm -lz -lminizip -lcglm -lstdc++ -lprotobuf -lcrypto
CFLAGS		+= -fvisibility=hidden -Iinclude -Iexternal/assimp/include -Iinclude/cglm -IGameNetworkingSockets/include $(VARS) $(shell pkg-config --cflags sdl3 sdl3-ttf sdl3-image) -DLIT_VERSION=\"$(VERSION)\"
CXXFLAGS	+= $(CFLAGS) -std=$(CXXSTD) -DSTEAMNETWORKINGSOCKETS_STATIC_LINK=1
CFLAGS		+= -std=$(CSTD)

SHADER_DIR 	 = shaders
SHADERS 	 = $(wildcard $(SHADER_DIR)/untextured/*.vert $(SHADER_DIR)/untextured/*.frag $(SHADER_DIR)/textured/*.vert $(SHADER_DIR)/textured/*.frag)
SPV 		 = $(SHADERS:.vert=.vert.spv)
SPV 		:= $(SPV:.frag=.frag.spv)

# This is an hacky ugly bastard way to check if we're not in windows
# just to add UBSAN
ifeq ($(UNAME_S),Darwin)
    LDFLAGS	+= -L/usr/local/lib -Wl,-rpath,/usr/local/lib
    ifeq ($(DEBUG), 1)
    	CFLAGS	 += -fsanitize=undefined
        CXXFLAGS += -fsanitize=undefined
    	LDFLAGS  += -fsanitize=undefined
    endif
endif
ifeq ($(UNAME_S),Linux)
    ifeq ($(DEBUG), 1)
        CFLAGS   += -fsanitize=undefined
        CXXFLAGS += -fsanitize=undefined
        LDFLAGS  += -fsanitize=undefined
    endif
else ifneq ($(UNAME_S),Darwin)
    LDLIBS	+= -lws2_32 -liphlpapi -ladvapi32 -lcrypt32 -lwinmm
    MINGW_FLAGS := -DOPENSSL_ROOT_DIR=/mingw64 \
  -DOPENSSL_INCLUDE_DIR=/mingw64/include \
  -DOPENSSL_CRYPTO_LIBRARY=/mingw64/lib/libcrypto.dll.a \
  -DOPENSSL_SSL_LIBRARY=/mingw64/lib/libssl.dll.a
endif

all: gamenetworkingsockets assimp shaders $(TARGET)

gamenetworkingsockets:
ifeq ($(wildcard $(BUILDDIR)/libGameNetworkingSockets.a),)
	mkdir -p $(BUILDDIR) GameNetworkingSockets/build GameNetworkingSockets/build/src
	cd GameNetworkingSockets && patch -p1 < ../fix-gns-patches.patch
	cmake -S GameNetworkingSockets/ -B GameNetworkingSockets/build -DBUILD_SHARED_LIB=OFF -DBUILD_TESTS=OFF $(MINGW_FLAGS)
	cmake --build GameNetworkingSockets/build --config Release
	cd GameNetworkingSockets && patch -p1 -R < ../fix-gns-patches.patch
	cp GameNetworkingSockets/build/src/libGameNetworkingSockets_s.a $(BUILDDIR)/libGameNetworkingSockets.a
endif

assimp:
ifeq ($(wildcard $(BUILDDIR)/libassimp.a),)
	mkdir -p $(BUILDDIR) external/assimp/build
	cd external/assimp && patch -p1 < ../../fix-assimp-6144.patch
	cmake -S external/assimp -B external/assimp/build -DASSIMP_BUILD_TESTS=OFF -DBUILD_SHARED_LIBS=OFF
	cmake --build external/assimp/build
	cd external/assimp && patch -p1 -R < ../../fix-assimp-6144.patch
	cp external/assimp/build/lib/libassimp.a $(BUILDDIR)
	cp external/assimp/build/include/assimp/config.h external/assimp/build/include/assimp/revision.h external/assimp/include/assimp/
endif

$(TARGET): shaders assimp gamenetworkingsockets $(OBJ)
	mkdir -p $(BUILDDIR)
	$(CC) $(OBJ) -o $(BUILDDIR)/$(TARGET) $(LDFLAGS) $(LDLIBS)

clean:
	rm -rf $(BUILDDIR)/$(TARGET) $(OBJ)

shaders:
	for f in $(SHADERS); do $(GLSLC) -I shaders/ $$f -o $$f.spv; done

.PHONY: $(TARGET) clean gamenetworkingsockets assimp shaders all
