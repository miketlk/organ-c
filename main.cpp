#include "RtAudio.h"
#include <iostream>
#include <cstdlib>
#include <thread>
#include <sndfile.hh>
#include <cstring>

class Sound
{
public:
    std::string fname;
    int midnote, channel, rate, loop, nframes;
    double *data;
    void init(std::string filename, bool release, int midinote);
};

void Sound::init(std::string filename, bool release, int midinote)
{
    SNDFILE *wf;
    SF_INFO inFileInfo;

    channel = 0;

    wf = sf_open(filename.c_str(), SFM_READ, &inFileInfo);

    SF_INSTRUMENT inst;
    sf_command(wf, SFC_GET_INSTRUMENT, &inst, sizeof(inst));

    if (release == false && inst.loop_count > 0)
    {
        loop = inst.loops[0].start;
        nframes = inst.loops[0].end;
    }
    else
    {
        loop = -1;
        nframes = inFileInfo.frames;
    }

    data = (double *)malloc((nframes * 2) * sizeof(double));
    sf_read_double(wf, data, nframes * 2);

    rate = inFileInfo.samplerate;

    sf_close(wf);
};

std::vector<Sound> sounds;
std::vector<double *> storedData;

int audioCallback(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
                  double streamTime, RtAudioStreamStatus status, void *userData)
{
    double *buffer = (double *)outputBuffer;
    memset(buffer, 0, sizeof(double) * nBufferFrames * 2);

    return 0;
}

void loadSamples()
{
    Sound test;
    test.init("test.wav", false, 36);
    sounds.push_back(test);
}

int main()
{
    RtAudio dac;
    if (dac.getDeviceCount() < 1)
    {
        std::cout << "\nNo audio devices found!\n";
        exit(0);
    }

    const auto processor_count = std::thread::hardware_concurrency();
    std::cout << processor_count << std::endl;

    RtAudio::StreamParameters parameters;
    parameters.deviceId = dac.getDefaultOutputDevice();
    parameters.nChannels = 2;
    parameters.firstChannel = 0;
    unsigned int sampleRate = 192000;
    unsigned int bufferFrames = 1024;
    double data[2];
    std::thread loadingThread(loadSamples);
    loadingThread.join();

    try
    {
        dac.openStream(&parameters, NULL, RTAUDIO_FLOAT64,
                       sampleRate, &bufferFrames, &audioCallback, (void *)&data);
        dac.startStream();
    }
    catch (RtAudioError &e)
    {
        e.printMessage();
        exit(0);
    }

    char input;
    std::cout << "\nPlaying ... press <enter> to quit.\n";
    std::cin.get(input);
    try
    {
        dac.stopStream();
    }
    catch (RtAudioError &e)
    {
        e.printMessage();
    }

    if (dac.isStreamOpen())
        dac.closeStream();

    return 0;
}