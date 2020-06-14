#include "RtAudio.h"
#include <iostream>
#include <cstdlib>
#include <thread>

int saw(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
        double streamTime, RtAudioStreamStatus status, void *userData)
{
    double *buffer = (double *)outputBuffer;
    if (status)
        std::cout << "Stream underflow detected!" << std::endl;

    return 0;
}

int main()
{
    RtAudio dac;
    if (dac.getDeviceCount() < 1)
    {
        std::cout << "\nNo audio devices found!\n";
        exit(0);
    }

    RtAudio::StreamParameters parameters;
    parameters.deviceId = dac.getDefaultOutputDevice();
    parameters.nChannels = 2;
    parameters.firstChannel = 0;
    unsigned int sampleRate = 192000;
    unsigned int bufferFrames = 256;
    double data[2];
    try
    {
        dac.openStream(&parameters, NULL, RTAUDIO_FLOAT64,
                       sampleRate, &bufferFrames, &saw, (void *)&data);
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