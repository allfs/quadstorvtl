ifeq ($(QUADSTOR_ROOT),)
	QUADSTOR_ROOT := /quadstor/quadstor
endif
ENABLE_STDERR := 0
RELEASE_BUILD := 0

UNAME := $(shell uname)

ifeq ($(UNAME), Linux)
CFLAGS += -DLINUX
CXXFLAGS += -DLINUX
endif

ifeq ($(UNAME), FreeBSD)
CFLAGS += -DFREEBSD
CXXFLAGS += -DFREEBSD
endif

CFLAGS += -Werror

ifeq "$(RELEASE_BUILD)" "1"
CFLAGS += -O2
else
CFLAGS += -g
endif

ifeq "$(ENABLE_STDERR)" "1"
CFLAGS += -DENABLE_STDERR
endif

CFLAGS += -I$(QUADSTOR_ROOT)/export -I$(QUADSTOR_ROOT)/common

CFLAGS += -DENABLE_LICENSING
