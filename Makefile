CC       	= gcc

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
OBJ_CC  	 = $(SRC_CC:.c=.o)
OBJ		 = $(OBJ_CC)
LDFLAGS   	+= -lSDL3 -lSDL3_ttf -lSDL3_image
CFLAGS  	?= -mtune=generic -march=native
CFLAGS        += -fvisibility=hidden -Iinclude $(VARS) -DVERSION=\"$(VERSION)\"

all: $(TARGET)
$(TARGET): $(OBJ)
	mkdir -p $(BUILDDIR)
	$(CC) $(OBJ) -o $(BUILDDIR)/$(TARGET) $(LDFLAGS)

clean:
	rm -rf $(BUILDDIR)/$(TARGET) $(OBJ)

.PHONY: $(TARGET) all
