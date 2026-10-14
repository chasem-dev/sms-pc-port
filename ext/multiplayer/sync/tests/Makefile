# Host test for the shared save file's merge rules (ext/multiplayer/sync).
#   make run
CXX  ?= g++
CXXFLAGS ?= -O2 -g -std=gnu++17 -Wall -Wextra
WORK ?= $(abspath ../../../..)/build-multiplayer-test
$(WORK)/mp_flags_test: mp_flags_test.cc ../mp_flags.cpp ../mp_flags.h ../../net/mp_wire.h
	mkdir -p $(WORK)
	$(CXX) $(CXXFLAGS) -I.. -o $@ mp_flags_test.cc ../mp_flags.cpp
run: $(WORK)/mp_flags_test
	$(WORK)/mp_flags_test
clean:
	rm -rf $(WORK)
.PHONY: run clean
