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
#define NUM_CHANNELS (1)
#define FRAMES_PER_BUFFER (256)
#define SAMPLE_SILENCE (0.0f)

typedef struct
{
    std::thread thread;
    std::vector<float> buffer;
    int fillBuffer = 1;
} threadItem;

typedef struct
{
    std::vector<float> data;
    int pos = 0;
    int playing = 0;
    int thread = 0;
    int loopStart = 0;
    int loopEnd = 0;
    int channel = 0;
} sample;

std::vector<sample> samples;
std::vector<threadItem> audioThreads;

static int paAudioCallback(const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo *timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData)
{
    float *out = (float *)outputBuffer;
    unsigned long i;
    unsigned long j;

    (void)timeInfo;
    (void)statusFlags;
    (void)inputBuffer;
    (void)userData;

    float inbuffer[FRAMES_PER_BUFFER * NUM_CHANNELS];

    for (j = 0; j < audioThreads.size(); j++)
    {
        if (audioThreads[j].fillBuffer == 0)
        {
            std::copy(std::begin(audioThreads[j].buffer), std::end(audioThreads[j].buffer), inbuffer);
            audioThreads[j].fillBuffer = 1;

            for (i = 0; i < framesPerBuffer; i++)
            {
                *out++ = inbuffer[i];
            }
        }
    }
    return paContinue;
}

void audioThreadFunc(int index)
{
    unsigned long i;
    float val;
    while (true)
    {
        if (audioThreads[index].fillBuffer == 1)
        {
            for (i = 0; i < FRAMES_PER_BUFFER * NUM_CHANNELS; i++)
            {
                audioThreads[index].buffer[i] = SAMPLE_SILENCE;
            }
            if (samples.size() > 0)
            {
                for (auto &it : samples)
                {
                    if (it.thread == index && it.playing == 1)
                    {
                        if (it.pos > it.loopEnd)
                        {
                            it.pos = it.loopStart;
                        }
                        for (i = 0; i < FRAMES_PER_BUFFER; i++)
                        {
                            val = it.data.at(it.pos + i);
                            audioThreads[index].buffer[i] += val;
                        }
                        it.pos += FRAMES_PER_BUFFER;
                    }
                }
            }
            audioThreads[index].fillBuffer = 0;
        }
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
    int available_threads = processor_count - 7;
    //std::cout << available_threads << std::endl;

    for (int i = 0; i < available_threads; i++)
    {
        audioThreads.push_back(threadItem());
        std::vector<float> newbuffer(FRAMES_PER_BUFFER * NUM_CHANNELS);
        audioThreads[i].buffer = newbuffer;
        std::fill(audioThreads[i].buffer.begin(), audioThreads[i].buffer.end(), SAMPLE_SILENCE);
        audioThreads[i].thread = std::thread(audioThreadFunc, i);
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
    outputParameters.channelCount = NUM_CHANNELS;
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
        &available_threads);
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