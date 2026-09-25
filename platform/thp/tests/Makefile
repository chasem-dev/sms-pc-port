# Host test for the THP decoders (decomp THPDec.c/THPAudio.c + thp-0*.patch).
#   make run DISC=/path/to/GMSE01/files [MOVIE=data/openingA.thp] [FRAMES=...]
PORT   := $(abspath ../../..)
DECOMP := $(PORT)/decomp
ARCH   ?= -m32
CC     ?= gcc
CXX    ?= g++
CFLAGS := $(ARCH) -O2 -g -std=gnu99 -w -fno-strict-aliasing -DTARGET_PC -DGEKKO \
          -I$(PORT)/src/port_include -I$(DECOMP)/include -include $(PORT)/src/port_compat.h
# Patched copies and objects live outside platform/ (the port globs platform/**/*.c).
WORK   ?= $(PORT)/build-thp-test
OUT    ?= $(WORK)/out

$(WORK)/patched/.stamp: $(wildcard $(PORT)/decomp-patches/thp-*.patch) $(DECOMP)/src/dolphin/thp/THPDec.c $(DECOMP)/src/dolphin/thp/THPAudio.c
	rm -rf $(WORK)/patched && mkdir -p $(WORK)/patched/src/dolphin/thp
	cp $(DECOMP)/src/dolphin/thp/THPDec.c $(DECOMP)/src/dolphin/thp/THPAudio.c $(WORK)/patched/src/dolphin/thp/
	for p in $(PORT)/decomp-patches/thp-*.patch; do patch -p1 --quiet -d $(WORK)/patched -i $$p || exit 1; done
	touch $@

$(WORK)/thp_test: thp_test.cc $(WORK)/patched/.stamp
	$(CC) $(CFLAGS) -c -o $(WORK)/THPDec.o $(WORK)/patched/src/dolphin/thp/THPDec.c
	$(CC) $(CFLAGS) -c -o $(WORK)/THPAudio.o $(WORK)/patched/src/dolphin/thp/THPAudio.c
	$(CXX) $(ARCH) -O2 -g -std=gnu++11 -o $@ thp_test.cc $(WORK)/THPDec.o $(WORK)/THPAudio.o -lm

run: $(WORK)/thp_test
	mkdir -p $(OUT) && $(WORK)/thp_test $(OUT)

clean:
	rm -rf $(WORK)
.PHONY: run clean
