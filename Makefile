CC       	= gcc
CXX			= g++

DEBUG 		?= 1

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
SRC_CC  	 = $(wildcard src/*.c)
SRC_CXX		 = $(wildcard src/*.cc)
OBJ_CC  	 = $(SRC_CC:.c=.o)
OBJ_CXX		 = $(SRC_CXX:.cc=.o)
OBJ		 = $(OBJ_CC) $(OBJ_CXX)
LDFLAGS   	+= -lSDL3 -lSDL3_ttf -lSDL3_image -lGameNetworkingSockets -lstdc++
CFLAGS  	?= -mtune=generic -march=native
CFLAGS        += -fvisibility=hidden -Iinclude -I/usr/local/include/GameNetworkingSockets $(VARS) -DVERSION=\"$(VERSION)\"
CXXFLAGS	= $(CFLAGS)

all: $(TARGET)
$(TARGET): $(OBJ)
	mkdir -p $(BUILDDIR)
	$(CC) $(OBJ) -o $(BUILDDIR)/$(TARGET) $(LDFLAGS)

clean:
	rm -rf $(BUILDDIR)/$(TARGET) $(OBJ)

.PHONY: $(TARGET) all
