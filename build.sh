g++ -Wall -g -D__LINUX_ALSA__ main.cpp libportaudio.a RtMidi.cpp -I./lib -lrt -lasound -pthread -lsndfile -std=c++11 -o organ.app