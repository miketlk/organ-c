#include <stdio.h>
#include <iostream>
#include <sndfile.hh>
#include "portaudio.h"
#include "pa_linux_alsa.h"
#include <thread>
#include <vector>
#include <cstring>
#include <memory>
#include "filter_common.h"
#include "filter_includes.h"

#define SAMPLE_RATE (48000)
#define FRAMES_PER_BUFFER (256)

#ifndef M_PI
#define M_PI (3.14159265)
#endif

std::vector<float> samples;
int pos = 0;

float buffer[FRAMES_PER_BUFFER];
int fillBuffer = 1;

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

    float val;

    float inbuffer[framesPerBuffer];

    std::copy(std::begin(buffer), std::end(buffer), inbuffer);

    if (fillBuffer == 0)
    {
        fillBuffer = 1;
    }

    for (i = 0; i < framesPerBuffer; i++)
    {
        val = inbuffer[i];
        *out++ = val;
    }
    return paContinue;
}

void testFunction()
{
    unsigned long i;

    float val;
    std::unique_ptr<SO_BUTTERWORTH_LPF> filter (new SO_BUTTERWORTH_LPF);
    while (true)
    {
        if (fillBuffer == 1)
        {
            fillBuffer = 0;

            if (samples.size() > 0)
            {
                filter->calculate_coeffs(500, 48000);
                if (pos > samples.size() - FRAMES_PER_BUFFER)
                {
                    pos = 0;
                }
                for (i = 0; i < FRAMES_PER_BUFFER; i++)
                {
                    val = samples.at(pos + i);
                    buffer[i] = filter->process(val);
                }
                pos += FRAMES_PER_BUFFER;
            }
        }
    }
}

int main(void);
int main(void)
{
    PaStreamParameters outputParameters;
    PaStream *stream;
    PaAlsaStreamInfo info;
    PaError err;

    SNDFILE *wf;
    SF_INFO inFileInfo;
    SF_INSTRUMENT inst;
    int nframes;

    wf = sf_open("test.wav", SFM_READ, &inFileInfo);

    sf_command(wf, SFC_GET_INSTRUMENT, &inst, sizeof(inst));

    nframes = inFileInfo.frames * inFileInfo.channels;

    float data[nframes];

    sf_read_float(wf, data, nframes);

    sf_close(wf);

    samples.resize(nframes);
    memcpy(&samples[0], data, nframes * sizeof(float));

    std::thread t1(testFunction);

    PaAlsa_InitializeStreamInfo(&info);

    err = Pa_Initialize();
    if (err != paNoError)
        goto error;

    outputParameters.device = 9; /* default output device */
    if (outputParameters.device == paNoDevice)
    {
        fprintf(stderr, "Error: The selected audio device could not be found.\n");
        goto error;
    }
    outputParameters.channelCount = 1;         /* stereo output */
    outputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    PaAlsa_EnableRealtimeScheduling(&stream, true);

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