# Offline mixer test: make && ./audio_test DISC_FILES_DIR [OUT_DIR]
CXX ?= g++
CXXFLAGS ?= -O2 -g -Wall -std=gnu++11
audio_test: audio_test.cc ../dsp_mixer.cpp ../dsp_mixer.h
	$(CXX) $(CXXFLAGS) -o $@ audio_test.cc ../dsp_mixer.cpp -lm
mixer_regression: mixer_regression.cc ../dsp_mixer.cpp ../dsp_mixer.h
	$(CXX) $(CXXFLAGS) -o $@ mixer_regression.cc ../dsp_mixer.cpp -lm
queue_test: queue_test.cc ../out_queue.h
	$(CXX) $(CXXFLAGS) -o $@ queue_test.cc
check: mixer_regression queue_test
	./mixer_regression
	./queue_test
clean:
	rm -f audio_test mixer_regression queue_test
.PHONY: clean check
