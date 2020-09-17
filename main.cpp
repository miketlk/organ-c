#include <stdio.h>
#include <iostream>
#include <sndfile.hh>
#include <samplerate.h>
#include "portaudio.h"
#include "pa_linux_alsa.h"
#include <thread>
#include <vector>
#include <cstring>
#include <memory>
#include "filter_common.h"
#include "filter_includes.h"
#include "RtMidi.h"

#define SAMPLE_RATE (48000)
#define FRAMES_PER_BUFFER (256)
#define SAMPLE_SILENCE (0.0f)

static int patestCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData)
{
    float *out = (float *)outputBuffer;
    unsigned long i;

    (void)timeInfo;
    (void)statusFlags;
    (void)inputBuffer;

    for (i = 0; i < framesPerBuffer; i++)
    {
    }
    return paContinue;
}

void mycallback(double deltatime, std::vector<unsigned char> *message, void *userData)
{
    unsigned int nBytes = message->size();
    for (unsigned int i = 0; i < nBytes; i++)
        std::cout << "Byte " << i << " = " << (int)message->at(i) << ", ";
    if (nBytes > 0)
        std::cout << "stamp = " << deltatime << std::endl;
}

int main(void);
int main(void)
{
    PaAlsaStreamInfo info;
    PaStreamParameters outputParameters;
    PaStream *stream;

    PaError err;

    SF_INSTRUMENT inst;

    PaAlsa_InitializeStreamInfo(&info);
    PaAlsa_EnableRealtimeScheduling(&stream, true);

    int data = 1;

    RtMidiIn *midiin = new RtMidiIn();
    // Check available ports.
    unsigned int nPorts = midiin->getPortCount();
    if (nPorts == 0)
    {
        std::cout << "No ports available!\n";
    }

    err = Pa_Initialize();
    if (err != paNoError)
        goto error;

    outputParameters.device = 0; /* default output device */
    if (outputParameters.device == paNoDevice)
    {
        fprintf(stderr, "Error: The selected audio device could not be found.\n");
        goto error;
    }
    outputParameters.channelCount = 1;         /* stereo output */
    outputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
        &stream,
        NULL,
        &outputParameters,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        paClipOff,
        patestCallback,
        &data);
    if (err != paNoError)
        goto error;

    err = Pa_StartStream(stream);
    if (err != paNoError)
        goto error;

    midiin->openPort(0);
    midiin->setCallback(&mycallback);
    midiin->ignoreTypes(false, false, false);
    
    while (true)
    {
        Pa_Sleep(5000);
    }

    err = Pa_StopStream(stream);
    if (err != paNoError)
        goto error;

    err = Pa_CloseStream(stream);
    if (err != paNoError)
        goto error;

    Pa_Terminate();

    return err;
error:
    Pa_Terminate();
    fprintf(stderr, "An error occured while using the portaudio stream\n");
    fprintf(stderr, "Error number: %d\n", err);
    fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    return err;
}