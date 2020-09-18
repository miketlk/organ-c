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
#include <unordered_map>

#define SAMPLE_RATE (48000)
#define FRAMES_PER_BUFFER (256)
#define SAMPLE_SILENCE (0.0f)

typedef struct
{
    std::vector<float> data;
    int pos;
    int active;
} sample;

std::vector<sample> samples;
std::vector<std::vector<float>> buffers;
std::vector<std::thread> audioThreads;

static int paAudioCallback(const void *inputBuffer, void *outputBuffer,
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

void audioThreadFunc(int index)
{
    std::cout << index << std::endl;
    while (true)
    {
       
    }
}

void voicingThreadFunc()
{
    while (true)
    {
       
    }
}

void windingThreadFunc()
{
    while (true)
    {
       
    }
}

void MidiCallback(double deltatime, std::vector<unsigned char> *message, void *userData)
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
    const auto processor_count = std::thread::hardware_concurrency();
    int available_threads = processor_count - 8;
    std::cout << available_threads << std::endl;

    for (int i = 0; i < available_threads; i++) {
        std::vector<float> newbuffer(FRAMES_PER_BUFFER);
        std::fill(newbuffer.begin(), newbuffer.end(), SAMPLE_SILENCE); 
        buffers.push_back(newbuffer);
        audioThreads.emplace_back([&]{audioThreadFunc(i);});
    };

    std::thread voicingThread(voicingThreadFunc);
    std::thread windingThread(windingThreadFunc);

    PaAlsaStreamInfo info;
    PaStreamParameters outputParameters;
    PaStream *stream;

    PaError err;

    SF_INSTRUMENT inst;

    PaAlsa_InitializeStreamInfo(&info);
    PaAlsa_EnableRealtimeScheduling(&stream, true);

    int data = 1;

    RtMidiIn *midiin = new RtMidiIn();
    unsigned int nPorts = midiin->getPortCount();
    if (nPorts == 0)
    {
        std::cout << "No ports available!\n";
    }

    err = Pa_Initialize();
    if (err != paNoError)
    {
        fprintf(stderr, "Error number: %d\n", err);
        fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    }

    outputParameters.device = 0;
    if (outputParameters.device == paNoDevice)
    {
        fprintf(stderr, "Error: The selected audio device could not be found.\n");
    }
    outputParameters.channelCount = 1;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
        &stream,
        NULL,
        &outputParameters,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        paClipOff,
        paAudioCallback,
        &data);
    if (err != paNoError)
    {
        fprintf(stderr, "Error number: %d\n", err);
        fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    }

    err = Pa_StartStream(stream);
    if (err != paNoError)
    {
        fprintf(stderr, "Error number: %d\n", err);
        fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    }

    midiin->openPort(1);
    midiin->setCallback(&MidiCallback);

    while (true)
    {
        Pa_Sleep(5000);
    }

    err = Pa_StopStream(stream);
    if (err != paNoError)
    {
        fprintf(stderr, "Error number: %d\n", err);
        fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    }

    err = Pa_CloseStream(stream);
    if (err != paNoError)
    {
        fprintf(stderr, "Error number: %d\n", err);
        fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    }

    Pa_Terminate();

    return err;
}