#include <stdio.h>
#include <iostream>
#include <fstream>
#include <sndfile.hh>
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
#include "json.hpp"
#include <string>
#include <csignal>
#include <atomic>

using json = nlohmann::json;

#define SAMPLE_RATE (48000)
#define NUM_CHANNELS (2)
#define FRAMES_PER_BUFFER (256)
#define SAMPLE_SILENCE (0.0f)
#define FADEOUT_LENGTH (1000)
#define FADEIN_LENGTH (1000)

std::atomic<bool> exit_thread_flag{false};

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
    int thread = 1;
    int loops = 1;
    int loopStart = 0;
    int loopEnd = 0;
    int channel = 0;
    float pitchMult = 1.0;
    float volMult = 1.0;
    int enclosed = 1;
    int enclosure = 0;
    float previousEnclosureVol = 1.0;
    int fadeout = 0;
    float fadeoutPos = FADEOUT_LENGTH;
    int fadein = 0;
    float fadeinPos = 0;
} sample;

typedef struct
{
    int min;
    int max;
    int value;
} enclosurestep;

typedef struct
{
    float maxHighpass;
    float minHighpass;
    int highpass = 100;
    float maxLowpass;
    float minLowpass;
    int lowpass = 5000;
    float maxVolume;
    float minVolume;
    float volume = 1.0;
    int midichannel;
    int midinote;
    int selectedValue = 127;
    int enclosed = 0;
    int enclosure = 0;
    std::vector<enclosurestep> steps;
    void recalculate()
    {
        volume = (((maxVolume - minVolume) / 127) * selectedValue) + minVolume;
        lowpass = (int)(((maxLowpass - minLowpass) / 127) * selectedValue) + minLowpass;
        highpass = (int)(((maxHighpass - minHighpass) / 127) * selectedValue) + minHighpass;
        if (enclosed == 1)
        {
            enclosures[enclosure].recalculate();
            volume *= enclosures[enclosure].volume;
            if (enclosures[enclosure].highpass > highpass)
            {
                highpass = enclosures[enclosure].highpass;
            }
            if (enclosures[enclosure].lowpass < lowpass)
            {
                lowpass = enclosures[enclosure].lowpass;
            }
        }
    };
    void chooseValue(int input)
    {
        selectedValue = -1;
        for (auto &it : steps)
        {
            if (input >= it.min && input <= it.max)
            {
                selectedValue = it.value;
            }
        }
        if (selectedValue == -1)
        {
            selectedValue = input;
        }
    };
} enclosure;

float globalVolume = 1.0;
float globalPitch = 1.0;
std::vector<sample> samples;
std::vector<threadItem> audioThreads;
std::vector<enclosure> enclosures;

void signalHandler(int signum)
{
    exit_thread_flag = true;
}

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

    long unsigned int frames = NUM_CHANNELS * FRAMES_PER_BUFFER;

    std::vector<float> inbuffer(frames);
    std::vector<float> outmainbuffer(frames);
    std::fill(outmainbuffer.begin(), outmainbuffer.end(), SAMPLE_SILENCE);

    for (j = 0; j < audioThreads.size(); j++)
    {
        if (audioThreads[j].fillBuffer == 0)
        {
            inbuffer = audioThreads[j].buffer;
            audioThreads[j].fillBuffer = 1;

            for (i = 0; i < frames; i++)
            {
                outmainbuffer[i] += inbuffer[i];
            }
        }
    }
    for (i = 0; i < frames; i++)
    {
        *out++ = outmainbuffer[i];
    }
    return paContinue;
}

void audioThreadFunc(int index)
{
    unsigned long i;
    float pitch;
    int k = 0;
    float j;
    float fadeoutvol;
    float fadeinvol;
    float val;
    float enclosurevol;
    std::vector<float> workingbuffer(NUM_CHANNELS * FRAMES_PER_BUFFER);
    std::fill(workingbuffer.begin(), workingbuffer.end(), SAMPLE_SILENCE);

    std::unique_ptr<SO_BUTTERWORTH_LPF> lowpassFilter(new SO_BUTTERWORTH_LPF);
    std::unique_ptr<SO_BUTTERWORTH_HPF> highpassFilter(new SO_BUTTERWORTH_HPF);

    while (!exit_thread_flag)
    {
        if (audioThreads[index].fillBuffer == 1)
        {
            for (i = 0; i < FRAMES_PER_BUFFER * NUM_CHANNELS; i++)
            {
                workingbuffer[i] = SAMPLE_SILENCE;
            }
            if (samples.size() > 0)
            {
                for (auto &it : samples)
                {
                    if (it.thread == index && it.playing == 1)
                    {
                        pitch = it.pitchMult;
                        pitch *= globalPitch;
                        fadeoutvol = 1.0;
                        fadeinvol = 1.0;
                        enclosurevol = 1.0;
                        if (it.enclosed == 1)
                        {
                            //std::cout << enclosures[it.enclosure].highpass << std::endl;
                            enclosures[it.enclosure].recalculate();
                            lowpassFilter->calculate_coeffs(enclosures[it.enclosure].lowpass, SAMPLE_RATE);   // cut off everything above this frequency
                            highpassFilter->calculate_coeffs(enclosures[it.enclosure].highpass, SAMPLE_RATE); // cut off everything below this frequency
                            enclosurevol = enclosures[it.enclosure].volume;
                        }
                        for (i = 0; i < FRAMES_PER_BUFFER; i++)
                        {
                            j = it.pos + i * pitch;
                            k = (int)j;
                            if (k > it.loopEnd - 2 - pitch)
                            {
                                if (it.loops == 1)
                                {
                                    it.pos = it.loopStart + pitch;
                                }
                                else
                                {
                                    it.playing = 0;
                                }
                            }
                            if (it.fadeout == 1)
                            {
                                if (it.fadeoutPos == 0)
                                {
                                    it.fadeout = 0;
                                    it.playing = 0;
                                }
                                else
                                {
                                    fadeoutvol = it.fadeoutPos / FADEOUT_LENGTH;
                                    it.fadeoutPos -= 1;
                                }
                            }
                            if (it.fadein == 1)
                            {
                                if (it.fadeinPos == FADEIN_LENGTH)
                                {
                                    it.fadein = 0;
                                }
                                else
                                {
                                    fadeinvol = it.fadeinPos / FADEIN_LENGTH;
                                    it.fadeinPos += 1;
                                }
                            }
                            val = ((it.data.at(k) + (j - k) * (it.data.at(k + 1) - it.data.at(k))) * it.volMult);
                            if (it.enclosed == 1)
                            {
                                val = lowpassFilter->process(val);
                                val = highpassFilter->process(val);
                                val *= (((enclosurevol - it.previousEnclosureVol) / FRAMES_PER_BUFFER) * i) + it.previousEnclosureVol;
                            }
                            val = val * fadeoutvol * fadeinvol * globalVolume;
                            workingbuffer[(NUM_CHANNELS * i) + it.channel] += val;
                        }
                        it.previousEnclosureVol = enclosurevol;
                        it.pos += FRAMES_PER_BUFFER * pitch;
                    }
                }
            }
            for (i = 0; i < FRAMES_PER_BUFFER * NUM_CHANNELS; i++)
            {
                audioThreads[index].buffer[i] = workingbuffer[i];
            }
            audioThreads[index].fillBuffer = 0;
        }
    }
}

void voicingThreadFunc()
{
    while (!exit_thread_flag)
    {
    }
}

void windingThreadFunc()
{
    while (!exit_thread_flag)
    {
    }
}

void MidiCallback(double deltatime, std::vector<unsigned char> *message, void *userData)
{
    int messagetype;
    int messagechannel;
    int midinote;
    int messagevalue;
    unsigned int nBytes = message->size();
    messagetype = message->at(0) >> 4;
    //std::cout << "Type: " << messagetype << std::endl;
    messagechannel = (message->at(0) & 15) + 1;
    //std::cout << "Channel: " << messagechannel << std::endl;
    if (nBytes > 1)
    {
        midinote = message->at(1);
        //std::cout << "Note: " << midinote << std::endl;
    }
    if (nBytes > 2)
    {
        messagevalue = message->at(2);
        //std::cout << "Value: " << messagevalue << std::endl;
    }

    if (messagetype == 11)
    {
        // Handle expression
        for (auto &it : enclosures)
        {
            if (it.midichannel == messagechannel && it.midinote == midinote)
            {
                it.chooseValue(messagevalue);
            }
        }
    }
}

int main(void);
int main(void)
{
    signal(2, signalHandler); // SIGINT
    const auto processor_count = std::thread::hardware_concurrency();
    int available_threads = processor_count - 7;
    //std::cout << available_threads << std::endl;

    std::ifstream ii("config.json");
    json config;
    ii >> config;

    SNDFILE *wf;
    SF_INFO inFileInfo;
    SF_INSTRUMENT inst;
    int nframes;
    std::string filename;

    enclosures.push_back(enclosure());
    enclosures[0].midichannel = 1;
    enclosures[0].midinote = 7;
    enclosures[0].maxVolume = 1.0;
    enclosures[0].minVolume = 0.0;
    enclosures[0].minLowpass = 5000;
    enclosures[0].maxLowpass = 10000;
    enclosures[0].minHighpass = 500;
    enclosures[0].maxHighpass = 1000;
    enclosures[0].recalculate();
    enclosures[0].steps.push_back(enclosurestep());
    enclosures[0].steps[0].max = 50;
    enclosures[0].steps[0].min = 0;
    enclosures[0].steps[0].value = 10;

    int selectedThread = 0;
    for (long unsigned int i = 0; i < config["samples"].size(); i++)
    {
        if (selectedThread >= available_threads)
        {
            selectedThread = 0;
        }
        samples.push_back(sample());

        filename = config["samples"][i]["file"];

        wf = sf_open(filename.c_str(), SFM_READ, &inFileInfo);
        sf_command(wf, SFC_GET_INSTRUMENT, &inst, sizeof(inst));

        nframes = inFileInfo.frames * inFileInfo.channels;
        float data[nframes];

        sf_read_float(wf, data, nframes);

        sf_close(wf);

        std::vector<float> newbuffer(data, data + nframes);
        samples[i].data = newbuffer;
        samples[i].loopEnd = nframes;
        samples[i].playing = 1;
        samples[i].thread = selectedThread;
        selectedThread += 1;
    }

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
        fprintf(stderr, "Initialize error\n");
        fprintf(stderr, "Error number: %d\n", err);
        fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    }

    int numDevices;
    numDevices = Pa_GetDeviceCount();
    if (numDevices < 0)
    {
        printf("ERROR: Pa_CountDevices returned 0x%x\n", numDevices);
    }

    const PaDeviceInfo *deviceInfo;
    for (int ii = 0; ii < numDevices; ii++)
    {
        deviceInfo = Pa_GetDeviceInfo(ii);
        std::cout << ii << " - " << deviceInfo->name << std::endl;
    }

    outputParameters.device = 9;
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
        fprintf(stderr, "OpenStream error\n");
        fprintf(stderr, "Error number: %d\n", err);
        fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    }

    err = Pa_StartStream(stream);
    if (err != paNoError)
    {
        fprintf(stderr, "StartStream error\n");
        fprintf(stderr, "Error number: %d\n", err);
        fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    }

    midiin->openPort(1);
    midiin->setCallback(&MidiCallback);

    while (!exit_thread_flag)
    {
        Pa_Sleep(500);
    }

    voicingThread.join();
    windingThread.join();

    for (long unsigned int i = 0; i < audioThreads.size(); i++)
    {
        audioThreads[i].thread.join();
    }

    delete midiin;

    err = Pa_StopStream(stream);
    if (err != paNoError)
    {
        fprintf(stderr, "StopStream error\n");
        fprintf(stderr, "Error number: %d\n", err);
        fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    }

    err = Pa_CloseStream(stream);
    if (err != paNoError)
    {
        fprintf(stderr, "CloseStream error\n");
        fprintf(stderr, "Error number: %d\n", err);
        fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
    }

    Pa_Terminate();

    return err;
}