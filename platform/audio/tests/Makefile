# Offline mixer test: make && ./audio_test [disc files dir] [out dir]
CXX ?= g++
CXXFLAGS ?= -O2 -g -Wall -std=gnu++11
audio_test: audio_test.cc ../dsp_mixer.cpp ../dsp_mixer.h
	$(CXX) $(CXXFLAGS) -o $@ audio_test.cc ../dsp_mixer.cpp -lm
clean:
	rm -f audio_test
.PHONY: clean
