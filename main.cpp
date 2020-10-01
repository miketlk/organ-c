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
#include <chrono>
#include <math.h>
#include <algorithm>
#include <chrono>

using json = nlohmann::json;

static unsigned long SAMPLE_RATE = 48000;
static unsigned long NUM_CHANNELS = 2;
static unsigned long FRAMES_PER_BUFFER = 256;
static double GLOBAL_VOL = 1.0;
#define SAMPLE_SILENCE (0.0)
#define FADEOUT_LENGTH (2000)
#define FADEIN_LENGTH (4500)

static double d2r(double d)
{
    return (d / 180.0) * ((double)M_PI);
}

int closest(std::vector<int> vec, int value)
{
    int output;
    std::vector<int>::iterator it;
    it = lower_bound(vec.begin(), vec.end(), value);
    if (it == vec.begin())
    {
        output = *it;
    }
    else if (it == vec.end())
    {
        output = *(it - 1);
    }
    else
    {
        output = -1;
    }
    return output;
}

double closest(std::vector<double> vec, int value)
{
    double output;
    std::vector<double>::iterator it;
    it = lower_bound(vec.begin(), vec.end(), value);
    if (it == vec.begin())
    {
        output = *it;
    }
    else if (it == vec.end())
    {
        output = *(it - 1);
    }
    else
    {
        output = -1;
    }
    return output;
}

int flip(int num, int min, int max)
{
    return (max + min) - num;
}

std::atomic<bool> exit_thread_flag{false};

typedef struct
{
    std::thread thread;
    std::vector<double> buffer;
    int fillBuffer = 1;
} threadItem;

typedef struct
{
    std::vector<double> data;
    int pos = 0;
    int playing = 0;
    int thread = 0;
    int loops = 0;
    int loopStart = 0;
    int loopEnd = 0;
    int channelOne = 0;
    int channelTwo = -1;
    double pitchMult = 1.0;
    double volMult = 1.0;
    std::string enclosure = "";
    std::string windchest = "";
    std::string tremulant = "";
    double previousEnclosureVol = 1.0;
    int fadeout = 0;
    double fadeoutPos = 0;
    int fadein = 0;
    double fadeinPos = 0;
    double panAngle = 45.0;
    std::string filename = "";
} sample;

std::vector<sample> samples;

typedef struct
{
    int start;
    int end;
} loop;

typedef struct
{
    int selectedSample;
    std::vector<loop> loops;
    void play(int fadein)
    {
        if (samples[selectedSample].playing != 1 || (samples[selectedSample].playing == 1 && samples[selectedSample].fadeout == 1))
        {
            samples[selectedSample].pos = 0;
            samples[selectedSample].fadeout = 0;
            samples[selectedSample].fadeinPos = 0;
            samples[selectedSample].fadeoutPos = 0;
            samples[selectedSample].fadein = 0;
            if (fadein == 1)
            {
                samples[selectedSample].fadein = 1;
            }
            if (loops.size() > 0)
            {
                int Random = std::rand() % loops.size();
                samples[selectedSample].loopStart = loops[Random].start;
                samples[selectedSample].loopEnd = loops[Random].end;
            }
            samples[selectedSample].playing = 1;
        }
    };
    void stop(int fadeout)
    {
        if (samples[selectedSample].playing == 1)
        {
            if (fadeout == 1)
            {
                samples[selectedSample].fadeoutPos = 0;
                samples[selectedSample].fadeout = 1;
            }
            else
            {
                samples[selectedSample].playing = 0;
            }
        }
    };
} sampleItem;

typedef struct
{
    int min;
    int max;
    int value;
} shoeStage;

typedef struct
{
    std::string name;
    double maxHighpass;
    double minHighpass;
    double highpassLogFactor = 1.0;
    int highpass = 100;
    double maxLowpass;
    double minLowpass;
    double lowpassLogFactor = 1.0;
    int lowpass = 5000;
    double maxVolume;
    double minVolume;
    double volumeLogFactor = 1.0;
    double volume = 1.0;
    int midichannel;
    int midinote;
    int selectedValue = 127;
    int targetValue = 127;
    int stageRate = 100;
    int stageRateIndex = 0;
    std::string enclosure = "";
    std::vector<shoeStage> stages;
    void recalculate();
    void chooseValue(int input)
    {
        targetValue = -1;
        for (auto &it : stages)
        {
            if (input >= it.min && input <= it.max)
            {
                targetValue = it.value;
            }
        }
        if (targetValue == -1)
        {
            targetValue = input;
        }
    };
} enclosure;

std::unordered_map<std::string, enclosure> enclosures;

void enclosure::recalculate()
{
    /*if (maxVolume > minVolume) {
        volume = exp(-1*(((maxVolume - minVolume) / 127)-1)) * (selectedValue / 127);
    } else {
        volume = exp(1*((maxVolume - minVolume) / 127)) * (1 - (selectedValue / 127));
    }*/
    volume = (((maxVolume - minVolume) / 127) * selectedValue) + minVolume;
    lowpass = (int)(((maxLowpass - minLowpass) / 127) * selectedValue) + minLowpass;
    highpass = (int)(((maxHighpass - minHighpass) / 127) * selectedValue) + minHighpass;
    if (enclosure != "")
    {
        enclosures.at(enclosure).recalculate();
        volume *= enclosures.at(enclosure).volume;
        if (enclosures.at(enclosure).highpass > highpass)
        {
            highpass = enclosures.at(enclosure).highpass;
        }
        if (enclosures.at(enclosure).lowpass < lowpass)
        {
            lowpass = enclosures.at(enclosure).lowpass;
        }
    }
};

typedef struct
{
    std::string name;
    double pitchMult = 0.0;
    void recalculate()
    {
        pitchMult = 0.0;
    }
} windchest;

std::unordered_map<std::string, windchest> windchests;

typedef struct
{
    std::string name;
    int active = 0;
    double pitchMult = 0.0;
    int speedMidichannel;
    int speedMidinote;
    int speedSelectedValue = 127;
    int speedTargetValue = 127;
    int speedStageRate = 100;
    int speedStageRateIndex = 0;
    int depthMidichannel;
    int depthMidinote;
    int depthSelectedValue = 127;
    int depthTargetValue = 127;
    int depthStageRate = 100;
    int depthStageRateIndex = 0;
    int index = 0;
    int frequency = 5000;
    double depth = -0.015;
    std::vector<shoeStage> speedStages;
    std::vector<shoeStage> depthStages;
    std::vector<sampleItem> onNoises;
    std::vector<sampleItem> offNoises;
    sampleItem *onNoise = NULL;
    sampleItem *offNoise = NULL;
    std::vector<std::string> onFor;
    void recalculate()
    {
        if (active == 1)
        {
            if (index > frequency)
            {
                index = 0;
            }
            pitchMult = sin(((double)index / (double)frequency) * M_PI * 2.) * depth + (depth / 2);
            index += 1;
        }
        else
        {
            pitchMult = 0.0;
        }
    };
    void chooseSpeedValue(int input)
    {
        speedTargetValue = -1;
        for (auto &it : speedStages)
        {
            if (input >= it.min && input <= it.max)
            {
                speedTargetValue = it.value;
            }
        }
        if (speedTargetValue == -1)
        {
            speedTargetValue = input;
        }
    };
    void chooseDepthValue(int input)
    {
        depthTargetValue = -1;
        for (auto &it : depthStages)
        {
            if (input >= it.min && input <= it.max)
            {
                depthTargetValue = it.value;
            }
        }
        if (depthTargetValue == -1)
        {
            depthTargetValue = input;
        }
    };
    void on(std::string stopName)
    {
        int Random;
        if (std::find(onFor.begin(), onFor.end(), stopName) == onFor.end())
        {
            onFor.push_back(stopName);
        }
        if (active == 0)
        {
            active = 1;
            if (onNoises.size() > 0)
            {
                Random = std::rand() % onNoises.size();
                onNoise = &onNoises[Random];
                onNoises[Random].play(1);
            }
            if (offNoise)
            {
                offNoise->stop(1);
                offNoise = NULL;
            }
        }
    };
    void off(std::string stopName)
    {
        int Random;
        if (active == 1)
        {
            onFor.erase(std::remove(onFor.begin(), onFor.end(), stopName), onFor.end());
            if (onFor.empty())
            {
                active = 0;
                if (offNoises.size() > 0)
                {
                    Random = std::rand() % offNoises.size();
                    offNoise = &offNoises[Random];
                    offNoises[Random].play(1);
                }
                if (onNoise)
                {
                    onNoise->stop(1);
                    onNoise = NULL;
                }
            }
        }
    };
} tremulant;

std::unordered_map<std::string, tremulant> tremulants;

bool sortfunction(int i, int j) { return (i < j); }

typedef struct
{
    double pitchMult = 1.0;
    double volMult = 1.0;
    std::string enclosure = "";
    std::string windchest = "";
    std::string tremulant = "";
    double windWeight = 0.0;
    int channelOne = -1;
    int channelTwo = -1;
    double panAngle = -1.0;
    sampleItem *playingAttack = NULL;
    sampleItem *playingRelease = NULL;
    std::unordered_map<int, std::unordered_map<double, std::vector<sampleItem>>> attacks;
    std::unordered_map<int, std::unordered_map<double, std::vector<sampleItem>>> releases;
    int playing = 0;
    std::vector<std::string> playingFor;
    std::chrono::system_clock::time_point lastPlayed = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point lastStopped = std::chrono::system_clock::now();
    void play(int velocity, std::string stopName)
    {
        if (std::find(playingFor.begin(), playingFor.end(), stopName) == playingFor.end())
        {
            playingFor.push_back(stopName);
        }
        if (playing == 0)
        {
            lastPlayed = std::chrono::system_clock::now();

            std::vector<int> velKeys;
            std::vector<double> timeKeys;
            for (auto kv : attacks)
            {
                velKeys.push_back(kv.first);
            }
            std::sort(velKeys.begin(), velKeys.end(), sortfunction);
            int selectedVel = closest(velKeys, velocity);
            if (selectedVel != -1)
            {
                for (auto kv : attacks[selectedVel])
                {
                    timeKeys.push_back(kv.first);
                }
                std::sort(timeKeys.begin(), timeKeys.end(), sortfunction);
                std::chrono::system_clock::time_point newTime = std::chrono::system_clock::now();
                double selectedTime = closest(timeKeys, std::chrono::duration_cast<std::chrono::milliseconds>(newTime - lastStopped).count());
                if (selectedTime != -1)
                {
                    int Random;
                    Random = std::rand() % attacks[selectedVel][selectedTime].size();
                    playingAttack = &attacks[selectedVel][selectedTime][Random];
                    playing = 1;
                    attacks[selectedVel][selectedTime][Random].play(1);
                    if (playingRelease)
                    {
                        playingRelease->stop(1);
                        playingRelease = NULL;
                    }
                }
            }
        }
    }
    void stop(int velocity, std::string stopName)
    {
        if (playing == 1)
        {
            lastStopped = std::chrono::system_clock::now();
            playingFor.erase(std::remove(playingFor.begin(), playingFor.end(), stopName), playingFor.end());
            if (playingFor.empty())
            {

                std::vector<int> velKeys;
                std::vector<double> timeKeys;
                for (auto kv : releases)
                {
                    velKeys.push_back(kv.first);
                }
                std::sort(velKeys.begin(), velKeys.end(), sortfunction);
                int selectedVel = closest(velKeys, velocity);
                if (selectedVel != -1)
                {
                    for (auto kv : releases[selectedVel])
                    {
                        timeKeys.push_back(kv.first);
                    }
                    std::sort(timeKeys.begin(), timeKeys.end(), sortfunction);
                    std::chrono::system_clock::time_point newTime = std::chrono::system_clock::now();
                    double selectedTime = closest(timeKeys, std::chrono::duration_cast<std::chrono::milliseconds>(newTime - lastPlayed).count());
                    if (selectedTime != -1)
                    {
                        int Random;
                        Random = std::rand() % releases[selectedVel][selectedTime].size();
                        playingRelease = &releases[selectedVel][selectedTime][Random];
                        releases[selectedVel][selectedTime][Random].play(1);
                        playing = 0;
                        if (playingAttack)
                        {
                            playingAttack->stop(1);
                            playingAttack = NULL;
                        }
                    }
                }
            }
        }
    }
} pipe;

typedef struct
{
    std::string name;
    double pitchMult = 1.0;
    double volMult = 1.0;
    std::string enclosure = "";
    std::string windchest = "";
    std::string tremulant = "";
    int trebleChannelOne = 0;
    int trebleChannelTwo = -1;
    int bassChannelOne = 0;
    int bassChannelTwo = -1;
    int trebleBassSplit = 45;
    int startNote = 0;
    int endNote = 127;
    int layoutMode = 0;
    std::unordered_map<int, pipe> pipes;
    void play(int note, int velocity, std::string stopName)
    {
        if (pipes.find(note) != pipes.end())
        {
            pipes[note].play(velocity, stopName);
        }
    }
    void stop(int note, int velocity, std::string stopName)
    {
        if (pipes.find(note) != pipes.end())
        {
            pipes[note].stop(velocity, stopName);
        }
    }
} rank;

std::unordered_map<std::string, rank> ranks;

typedef struct
{
    std::string name;
    int lowNote;
    int highNote;
    int offset;
} rankMapping;

typedef struct
{
    int midichannel;
    int midinote;
    std::string keyboard;
    int active = 0;
    std::vector<rankMapping> rnks;
    std::string name;
    std::vector<sampleItem> onNoises;
    std::vector<sampleItem> offNoises;
    std::vector<std::string> trems;
    sampleItem *onNoise = NULL;
    sampleItem *offNoise = NULL;
    void play(int note, int velocity)
    {
        if (active == 1)
        {
            for (auto &it : rnks)
            {
                if ((note + it.offset) >= it.lowNote && (note + it.offset) <= it.highNote)
                {
                    ranks.at(it.name).play(note + it.offset, velocity, name);
                }
            }
        }
    };
    void stop(int note, int velocity)
    {
        if (active == 1)
        {
            for (auto &it : rnks)
            {
                if ((note + it.offset) >= it.lowNote && (note + it.offset) <= it.highNote)
                {
                    ranks.at(it.name).stop(note + it.offset, velocity, name);
                }
            }
        }
    };
    void on(int overrideActive);
    void off()
    {
        int Random;
        if (active == 1)
        {
            active = 0;
            if (offNoises.size() > 0)
            {
                Random = std::rand() % offNoises.size();
                offNoise = &offNoises[Random];
                offNoises[Random].play(1);
            }
            if (onNoise)
            {
                onNoise->stop(1);
                onNoise = NULL;
            }
            for (auto &it : rnks)
            {
                for (int ki = 0; ki < 128; ki++)
                {
                    if ((ki + it.offset) >= it.lowNote && (ki + it.offset) <= it.highNote)
                    {
                        ranks.at(it.name).stop(ki + it.offset, 0, name);
                    }
                }
            }
            for (auto &it : trems)
            {
                tremulants.at(it).off(name);
            }
        }
    };
} stop;

std::unordered_map<std::string, stop> stops;

typedef struct
{
    std::string name;
    int midichannel;
    int notes[128] = {0};
    void play(int note, int velocity)
    {
        notes[note] = 1;
        for (auto &it : stops)
        {
            if (it.second.keyboard == name)
            {
                it.second.play(note, velocity);
            }
        }
    };
    void stop(int note, int velocity)
    {
        notes[note] = 0;
        for (auto &it : stops)
        {
            if (it.second.keyboard == name)
            {
                it.second.stop(note, velocity);
            }
        }
    };
} keyboard;

std::unordered_map<std::string, keyboard> keyboards;

void stop::on(int overrideActive)
{
    int Random;
    if (active == 0 || overrideActive == 1)
    {
        active = 1;
        if (onNoises.size() > 0)
        {
            Random = std::rand() % onNoises.size();
            onNoise = &onNoises[Random];
            onNoises[Random].play(1);
        }
        if (offNoise)
        {
            offNoise->stop(1);
            offNoise = NULL;
        }
        for (auto &it : rnks)
        {
            for (int ki = 0; ki < 128; ki++)
            {
                if (keyboards[keyboard].notes[ki] == 1)
                {
                    if ((ki + it.offset) >= it.lowNote && (ki + it.offset) <= it.highNote)
                    {
                        ranks.at(it.name).play(ki + it.offset, 64, name);
                    }
                }
            }
        }
        for (auto &it : trems)
        {
            tremulants.at(it).on(name);
        }
    }
};

std::vector<threadItem> audioThreads;

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

    std::vector<double> inbuffer(frames);
    std::vector<double> outmainbuffer(frames);
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
        else
        {
            std::cout << "underflow thread: " << j << std::endl;
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
    double pitch;
    int k = 0;
    double j;
    double fadeoutvol;
    double fadeinvol;
    double val;
    double enclosurevol;
    int enclosurehighpass;
    int enclosurelowpass;
    std::vector<double> workingbuffer(NUM_CHANNELS * FRAMES_PER_BUFFER);
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
                        if (it.windchest != "")
                        {
                            pitch += windchests.at(it.windchest).pitchMult;
                        }
                        if (it.tremulant != "")
                        {
                            pitch += tremulants.at(it.tremulant).pitchMult;
                        }
                        fadeoutvol = 1.0;
                        fadeinvol = 1.0;
                        if (it.enclosure != "")
                        {
                            enclosurevol = enclosures.at(it.enclosure).volume;
                            enclosurehighpass = enclosures.at(it.enclosure).highpass;
                            enclosurelowpass = enclosures.at(it.enclosure).lowpass;
                            lowpassFilter->calculate_coeffs(enclosurelowpass, SAMPLE_RATE);   // cut off everything above this frequency
                            highpassFilter->calculate_coeffs(enclosurehighpass, SAMPLE_RATE); // cut off everything below this frequency
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
                            if (it.playing == 1)
                            {
                                if (it.fadeout == 1)
                                {
                                    if (it.fadeoutPos == FADEOUT_LENGTH)
                                    {
                                        it.playing = 0;
                                        it.fadeout = 0;
                                    }
                                    else
                                    {
                                        fadeoutvol = exp(1 * (it.fadeoutPos / FADEOUT_LENGTH)) * (1 - (it.fadeoutPos / FADEOUT_LENGTH));
                                        it.fadeoutPos += 1;
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
                                        fadeinvol = exp(-1 * ((it.fadeinPos / FADEIN_LENGTH) - 1)) * (it.fadeinPos / FADEIN_LENGTH);
                                        it.fadeinPos += 1;
                                    }
                                }
                                val = (it.data.at(k) + (j - k) * (it.data.at(k + 1) - it.data.at(k))) * it.volMult;
                                if (it.enclosure != "")
                                {
                                    val = lowpassFilter->process(val);
                                    val = highpassFilter->process(val);
                                    val *= (((enclosurevol - it.previousEnclosureVol) / FRAMES_PER_BUFFER) * i) + it.previousEnclosureVol;
                                }
                                val = val * fadeoutvol * fadeinvol;
                                if (it.channelTwo != -1)
                                {
                                    workingbuffer[(NUM_CHANNELS * i) + it.channelOne] += (val * sin(d2r(it.panAngle))) * GLOBAL_VOL;
                                    workingbuffer[(NUM_CHANNELS * i) + it.channelTwo] += (val * cos(d2r(it.panAngle))) * GLOBAL_VOL;
                                }
                                else
                                {
                                    workingbuffer[(NUM_CHANNELS * i) + it.channelOne] += val * GLOBAL_VOL;
                                }
                            }
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

void windThreadFunc()
{
    while (!exit_thread_flag)
    {
        for (auto &it : windchests)
        {
            it.second.recalculate();
        }
    }
}

void tremThreadFunc()
{
    while (!exit_thread_flag)
    {
        for (auto &it : enclosures)
        {
            if (it.second.stageRateIndex == it.second.stageRate)
            {
                it.second.stageRateIndex = 0;
                if (it.second.targetValue != it.second.selectedValue)
                {
                    if (it.second.targetValue > it.second.selectedValue)
                    {
                        it.second.selectedValue += 1;
                    }
                    else
                    {
                        it.second.selectedValue -= 1;
                    }
                }
            }
            it.second.stageRateIndex += 1;
            it.second.recalculate();
        }
        for (auto &it : tremulants)
        {
            if (it.second.speedStageRateIndex == it.second.speedStageRate)
            {
                it.second.speedStageRateIndex = 0;
                if (it.second.speedTargetValue != it.second.speedSelectedValue)
                {
                    if (it.second.speedTargetValue > it.second.speedSelectedValue)
                    {
                        it.second.speedSelectedValue += 1;
                    }
                    else
                    {
                        it.second.speedSelectedValue -= 1;
                    }
                }
            }
            it.second.speedStageRateIndex += 1;
            if (it.second.depthStageRateIndex == it.second.depthStageRate)
            {
                it.second.depthStageRateIndex = 0;
                if (it.second.depthTargetValue != it.second.depthSelectedValue)
                {
                    if (it.second.depthTargetValue > it.second.depthSelectedValue)
                    {
                        it.second.depthSelectedValue += 1;
                    }
                    else
                    {
                        it.second.depthSelectedValue -= 1;
                    }
                }
            }
            it.second.depthStageRateIndex += 1;
            it.second.recalculate();
        }
        std::this_thread::sleep_for(std::chrono::microseconds(10));
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
            if (it.second.midichannel == messagechannel && it.second.midinote == midinote)
            {
                it.second.chooseValue(messagevalue);
            }
        }
        // Handle trems
        for (auto &it : tremulants)
        {
            if (it.second.speedMidichannel == messagechannel && it.second.speedMidinote == midinote)
            {
                it.second.chooseSpeedValue(messagevalue);
            }
            if (it.second.depthMidichannel == messagechannel && it.second.depthMidinote == midinote)
            {
                it.second.chooseDepthValue(messagevalue);
            }
        }
    }
    else if (messagetype == 8 || (messagetype == 9 && messagevalue == 0))
    {
        // Note off
        for (auto &it : keyboards)
        {
            if (it.second.midichannel == messagechannel)
            {
                it.second.stop(midinote, messagevalue);
            }
        }
        // Stop off
        for (auto &it : stops)
        {
            if (it.second.midichannel == messagechannel && it.second.midinote == midinote)
            {
                it.second.off();
            }
        }
    }
    else if (messagetype == 9)
    {
        // Note on
        for (auto &it : keyboards)
        {
            if (it.second.midichannel == messagechannel)
            {
                it.second.play(midinote, messagevalue);
            }
        }
        // Stop on
        for (auto &it : stops)
        {
            if (it.second.midichannel == messagechannel && it.second.midinote == midinote)
            {
                it.second.on(0);
            }
        }
    }
}

double calculatePanAngle(int note, int startNote, int endNote, int layoutMode)
{
    double angle = 45.0;
    if (layoutMode == 0)
    {
        angle = (90.0 / (endNote - startNote)) * (note - startNote);
    }
    else if (layoutMode == 1)
    {
        angle = (90.0 / (endNote - startNote)) * (flip(note, startNote, endNote) - startNote);
    }
    return angle;
}

int main(void)
{
    signal(2, signalHandler); // SIGINT
    const auto processor_count = std::thread::hardware_concurrency();
    int available_threads = processor_count - 10;
    //std::cout << available_threads << std::endl;

    std::ifstream iis("config.json");
    json sconfig;
    iis >> sconfig;
    iis.close();

    NUM_CHANNELS = sconfig["numChannels"];
    SAMPLE_RATE = sconfig["sampleRate"];
    FRAMES_PER_BUFFER = sconfig["framesPerBuffer"];
    GLOBAL_VOL = sconfig["globalVol"];

    std::ifstream ii("instrument/" + sconfig["instrumentConfig"].get<std::string>());
    json config;
    ii >> config;
    ii.close();

    SNDFILE *wf;
    SF_INFO inFileInfo;
    SF_INSTRUMENT inst;
    int nframes;
    std::string filename;

    for (auto &it : config["keyboards"])
    {
        keyboards[it["name"]] = keyboard();
        keyboards[it["name"]].name = it["name"];
        keyboards[it["name"]].midichannel = it["midichannel"];
    }

    for (auto &it : config["stops"])
    {
        stops[it["name"]] = stop();
        stops[it["name"]].name = it["name"];
        stops[it["name"]].keyboard = it["keyboard"];
        stops[it["name"]].midichannel = it["midichannel"];
        stops[it["name"]].midinote = it["midinote"];
        stops[it["name"]].active = it["active"];
        for (auto &ri : it["ranks"])
        {
            rankMapping newMapping;
            newMapping.name = ri["name"];
            newMapping.lowNote = ri["lowNote"];
            newMapping.highNote = ri["highNote"];
            newMapping.offset = ri["noteOffset"];
            stops[it["name"]].rnks.push_back(newMapping);
        }

        for (auto &ri : it["tremulants"])
        {
            if (std::find(stops[it["name"]].trems.begin(), stops[it["name"]].trems.end(), ri) == stops[it["name"]].trems.end())
            {
                stops[it["name"]].trems.push_back(ri);
            }
        }

        for (auto &ri : it["onNoises"])
        {
            sampleItem newOnNoise;
            sample newSample;

            newSample.loops = ri["loops"];
            newSample.channelOne = ri["channelOne"];
            newSample.channelTwo = ri["channelTwo"];
            newSample.panAngle = ri["panAngle"];
            newSample.pitchMult = ri["pitchMult"];
            newSample.volMult = ri["volMult"];
            newSample.enclosure = ri["enclosure"];

            filename = "instrument/noises/" + ri["file"].get<std::string>();
            newSample.filename = filename;
            wf = sf_open(filename.c_str(), SFM_READ, &inFileInfo);
            sf_command(wf, SFC_GET_INSTRUMENT, &inst, sizeof(inst));

            nframes = inFileInfo.frames * inFileInfo.channels;
            double data[nframes];

            sf_read_double(wf, data, nframes);

            sf_close(wf);

            std::vector<double> newbuffer(data, data + nframes);
            newSample.data = newbuffer;
            newSample.loopEnd = nframes;

            samples.push_back(newSample);
            newOnNoise.selectedSample = samples.size() - 1;

            if (ri["loops"] == 1)
            {
                if (inst.loop_count > 0)
                {
                    for (int ll = 0; ll < inst.loop_count; ll++)
                    {
                        loop newLoop;
                        newLoop.start = inst.loops[ll].start;
                        newLoop.end = inst.loops[ll].end;
                        newOnNoise.loops.push_back(newLoop);
                    }
                }
            }
            stops[it["name"]].onNoises.push_back(newOnNoise);
        }
        for (auto &ri : it["offNoises"])
        {
            sampleItem newOffNoise;
            sample newSample;

            newSample.loops = ri["loops"];
            newSample.channelOne = ri["channelOne"];
            newSample.channelTwo = ri["channelTwo"];
            newSample.panAngle = ri["panAngle"];
            newSample.pitchMult = ri["pitchMult"];
            newSample.volMult = ri["volMult"];
            newSample.enclosure = ri["enclosure"];

            filename = "instrument/noises/" + ri["file"].get<std::string>();
            newSample.filename = filename;
            wf = sf_open(filename.c_str(), SFM_READ, &inFileInfo);
            sf_command(wf, SFC_GET_INSTRUMENT, &inst, sizeof(inst));

            nframes = inFileInfo.frames * inFileInfo.channels;
            double data[nframes];

            sf_read_double(wf, data, nframes);

            sf_close(wf);

            std::vector<double> newbuffer(data, data + nframes);
            newSample.data = newbuffer;
            newSample.loopEnd = nframes;

            samples.push_back(newSample);
            newOffNoise.selectedSample = samples.size() - 1;

            if (ri["loops"] == 1)
            {
                if (inst.loop_count > 0)
                {
                    for (int ll = 0; ll < inst.loop_count; ll++)
                    {
                        loop newLoop;
                        newLoop.start = inst.loops[ll].start;
                        newLoop.end = inst.loops[ll].end;
                        newOffNoise.loops.push_back(newLoop);
                    }
                }
            }
            stops[it["name"]].offNoises.push_back(newOffNoise);
        }
    }

    for (auto &it : config["tremulants"])
    {
        tremulants[it["name"]] = tremulant();
        tremulants[it["name"]].active = it["active"];
        tremulants[it["name"]].name = it["name"];
        tremulants[it["name"]].speedMidichannel = it["speedMidichannel"];
        tremulants[it["name"]].speedMidinote = it["speedMidinote"];
        tremulants[it["name"]].depthMidichannel = it["depthMidichannel"];
        tremulants[it["name"]].depthMidinote = it["depthMidinote"];
        tremulants[it["name"]].frequency = it["frequency"];
        tremulants[it["name"]].depth = it["depth"];
        tremulants[it["name"]].speedStageRate = it["speedStageRate"];
        tremulants[it["name"]].depthStageRate = it["depthStageRate"];
        for (auto &si : it["speedStages"])
        {
            shoeStage newStage;
            newStage.max = si["max"];
            newStage.min = si["min"];
            newStage.value = si["value"];
            tremulants[it["name"]].speedStages.push_back(newStage);
        }
        for (auto &si : it["depthStages"])
        {
            shoeStage newStage;
            newStage.max = si["max"];
            newStage.min = si["min"];
            newStage.value = si["value"];
            tremulants[it["name"]].depthStages.push_back(newStage);
        }
        for (auto &ri : it["onNoises"])
        {
            sampleItem newOnNoise;
            sample newSample;

            newSample.loops = ri["loops"];
            newSample.channelOne = ri["channelOne"];
            newSample.channelTwo = ri["channelTwo"];
            newSample.panAngle = ri["panAngle"];
            newSample.pitchMult = ri["pitchMult"];
            newSample.volMult = ri["volMult"];
            newSample.enclosure = ri["enclosure"];

            filename = "instrument/noises/" + ri["file"].get<std::string>();
            newSample.filename = filename;
            wf = sf_open(filename.c_str(), SFM_READ, &inFileInfo);
            sf_command(wf, SFC_GET_INSTRUMENT, &inst, sizeof(inst));

            if (inFileInfo.channels > 1)
            {
                std::cout << "file is not mono: " << filename << std::endl;
                continue;
            }

            nframes = inFileInfo.frames * inFileInfo.channels;
            double data[nframes];

            sf_read_double(wf, data, nframes);

            sf_close(wf);

            std::vector<double> newbuffer(data, data + nframes);
            newSample.data = newbuffer;
            newSample.loopEnd = nframes;

            samples.push_back(newSample);
            newOnNoise.selectedSample = samples.size() - 1;

            if (ri["loops"] == 1)
            {
                if (inst.loop_count > 0)
                {
                    for (int ll = 0; ll < inst.loop_count; ll++)
                    {
                        loop newLoop;
                        newLoop.start = inst.loops[ll].start;
                        newLoop.end = inst.loops[ll].end;
                        newOnNoise.loops.push_back(newLoop);
                    }
                }
            }
            tremulants[it["name"]].onNoises.push_back(newOnNoise);
        }
        for (auto &ri : it["offNoises"])
        {
            sampleItem newOffNoise;
            sample newSample;

            newSample.loops = ri["loops"];
            newSample.channelOne = ri["channelOne"];
            newSample.channelTwo = ri["channelTwo"];
            newSample.panAngle = ri["panAngle"];
            newSample.pitchMult = ri["pitchMult"];
            newSample.volMult = ri["volMult"];
            newSample.enclosure = ri["enclosure"];

            filename = "instrument/noises/" + ri["file"].get<std::string>();
            newSample.filename = filename;
            wf = sf_open(filename.c_str(), SFM_READ, &inFileInfo);
            sf_command(wf, SFC_GET_INSTRUMENT, &inst, sizeof(inst));

            nframes = inFileInfo.frames * inFileInfo.channels;
            double data[nframes];

            sf_read_double(wf, data, nframes);

            sf_close(wf);

            std::vector<double> newbuffer(data, data + nframes);
            newSample.data = newbuffer;
            newSample.loopEnd = nframes;

            samples.push_back(newSample);
            newOffNoise.selectedSample = samples.size() - 1;

            if (ri["loops"] == 1)
            {
                if (inst.loop_count > 0)
                {
                    for (int ll = 0; ll < inst.loop_count; ll++)
                    {
                        loop newLoop;
                        newLoop.start = inst.loops[ll].start;
                        newLoop.end = inst.loops[ll].end;
                        newOffNoise.loops.push_back(newLoop);
                    }
                }
            }
            tremulants[it["name"]].offNoises.push_back(newOffNoise);
        }
    }

    for (auto &it : config["enclosures"])
    {
        enclosures[it["name"]] = enclosure();
        enclosures[it["name"]].name = it["name"];
        enclosures[it["name"]].midichannel = it["midichannel"];
        enclosures[it["name"]].midinote = it["midinote"];
        enclosures[it["name"]].enclosure = it["enclosure"];
        enclosures[it["name"]].maxHighpass = it["maxHighpass"];
        enclosures[it["name"]].minHighpass = it["minHighpass"];
        enclosures[it["name"]].highpassLogFactor = it["highpassLogFactor"];
        enclosures[it["name"]].maxLowpass = it["maxLowpass"];
        enclosures[it["name"]].minLowpass = it["minLowpass"];
        enclosures[it["name"]].lowpassLogFactor = it["lowpassLogFactor"];
        enclosures[it["name"]].maxVolume = it["maxVolume"];
        enclosures[it["name"]].minVolume = it["minVolume"];
        enclosures[it["name"]].volumeLogFactor = it["volumeLogFactor"];
        enclosures[it["name"]].stageRate = it["stageRate"];
        for (auto &si : it["stages"])
        {
            shoeStage newStage;
            newStage.max = si["max"];
            newStage.min = si["min"];
            newStage.value = si["value"];
            enclosures[it["name"]].stages.push_back(newStage);
        }
    }

    for (auto &it : config["windchests"])
    {
        windchests[it["name"]] = windchest();
        windchests[it["name"]].name = it["name"];
    }

    for (auto &it : config["ranks"])
    {
        ranks[it["name"]] = rank();
        ranks[it["name"]].name = it["name"];
        ranks[it["name"]].pitchMult = it["pitchMult"];
        ranks[it["name"]].volMult = it["volMult"];
        ranks[it["name"]].enclosure = it["enclosure"];
        ranks[it["name"]].windchest = it["windchest"];
        ranks[it["name"]].tremulant = it["tremulant"];
        ranks[it["name"]].trebleChannelOne = it["trebleChannelOne"];
        ranks[it["name"]].trebleChannelTwo = it["trebleChannelTwo"];
        ranks[it["name"]].bassChannelOne = it["bassChannelOne"];
        ranks[it["name"]].bassChannelTwo = it["bassChannelTwo"];
        ranks[it["name"]].trebleBassSplit = it["trebleBassSplit"];
        ranks[it["name"]].startNote = it["startNote"];
        ranks[it["name"]].endNote = it["endNote"];
        ranks[it["name"]].layoutMode = it["layoutMode"];
        std::ifstream rc("instrument/ranks/" + it["folder"].get<std::string>() + "/config.json");
        json rankConfig;
        rc >> rankConfig;
        rc.close();

        std::string enclosureItem = "";
        std::string tremulantItem = "";
        std::string windchestItem = "";
        int channelOneItem = 0;
        int channelTwoItem = -1;
        double panAngleItem = 45.0;
        for (auto &ipe : rankConfig)
        {
            pipe newPipe;
            if (ipe["windchest"] != "")
            {
                windchestItem = ipe["windchest"];
            }
            else
            {
                windchestItem = it["windchest"];
            }
            if (ipe["enclosure"] != "")
            {
                enclosureItem = ipe["enclosure"];
            }
            else
            {
                enclosureItem = it["enclosure"];
            }
            if (ipe["tremulant"] != "")
            {
                tremulantItem = ipe["tremulant"];
            }
            else
            {
                tremulantItem = it["tremulant"];
            }
            if (ipe["channelOne"] != -1)
            {
                channelOneItem = ipe["channelOne"];
            }
            else
            {
                if (ipe["number"] >= it["trebleBassSplit"])
                {
                    channelOneItem = it["trebleChannelOne"];
                }
                else
                {
                    channelOneItem = it["bassChannelOne"];
                }
            }
            if (ipe["channelTwo"] != -1)
            {
                channelTwoItem = ipe["channelTwo"];
            }
            else
            {
                if (ipe["number"] >= it["trebleBassSplit"])
                {
                    channelTwoItem = it["trebleChannelTwo"];
                }
                else
                {
                    channelTwoItem = it["bassChannelTwo"];
                }
            }
            if (ipe["panAngle"] != -1.0)
            {
                panAngleItem = ipe["panAngle"];
            }
            else
            {
                panAngleItem = calculatePanAngle(ipe["number"], it["startNote"], it["endNote"], it["layoutMode"]);
            }
            //std::cout << panAngleItem << std::endl;
            newPipe.windchest = windchestItem;
            //newPipe.windWeight
            newPipe.enclosure = enclosureItem;
            newPipe.tremulant = tremulantItem;
            newPipe.pitchMult = ipe["pitchMult"];
            newPipe.volMult = ipe["volMult"];
            for (auto &aElement : ipe["attacks"])
            {
                sampleItem newSItem;
                sample newPipeSample;
                newPipeSample.windchest = windchestItem;
                newPipeSample.enclosure = enclosureItem;
                newPipeSample.tremulant = tremulantItem;
                newPipeSample.loops = aElement["loops"];
                newPipeSample.pitchMult = aElement["pitchMult"].get<double>() * newPipe.pitchMult * ranks[it["name"]].pitchMult;
                newPipeSample.volMult = aElement["volMult"].get<double>() * newPipe.volMult * ranks[it["name"]].volMult;
                newPipeSample.channelOne = channelOneItem;
                newPipeSample.channelTwo = channelTwoItem;
                newPipeSample.panAngle = panAngleItem;
                filename = "instrument/ranks/" + it["folder"].get<std::string>() + "/attack/" + aElement["file"].get<std::string>();
                newPipeSample.filename = filename;
                wf = sf_open(filename.c_str(), SFM_READ, &inFileInfo);
                sf_command(wf, SFC_GET_INSTRUMENT, &inst, sizeof(inst));

                nframes = inFileInfo.frames * inFileInfo.channels;
                double data[nframes];

                sf_read_double(wf, data, nframes);

                sf_close(wf);

                std::vector<double> newbuffer(data, data + nframes);
                newPipeSample.data = newbuffer;
                newPipeSample.loopEnd = nframes;

                samples.push_back(newPipeSample);
                newSItem.selectedSample = samples.size() - 1;

                if (aElement["loops"] == 1)
                {
                    if (inst.loop_count > 0)
                    {
                        for (int ll = 0; ll < inst.loop_count; ll++)
                        {
                            loop newLoop;
                            newLoop.start = inst.loops[ll].start;
                            newLoop.end = inst.loops[ll].end;
                            newSItem.loops.push_back(newLoop);
                        }
                    }
                }
                newPipe.attacks[aElement["velocity"]][aElement["time"]].push_back(newSItem);
            }
            for (auto &aElement : ipe["releases"])
            {
                sampleItem newSItem;
                sample newPipeSample;
                newPipeSample.windchest = windchestItem;
                newPipeSample.enclosure = enclosureItem;
                newPipeSample.tremulant = tremulantItem;
                newPipeSample.loops = aElement["loops"];
                newPipeSample.pitchMult = aElement["pitchMult"].get<double>() * newPipe.pitchMult * ranks[it["name"]].pitchMult;
                newPipeSample.volMult = aElement["volMult"].get<double>() * newPipe.volMult * ranks[it["name"]].volMult;
                newPipeSample.channelOne = channelOneItem;
                newPipeSample.channelTwo = channelTwoItem;
                newPipeSample.panAngle = panAngleItem;

                filename = "instrument/ranks/" + it["folder"].get<std::string>() + "/release/" + aElement["file"].get<std::string>();
                newPipeSample.filename = filename;
                wf = sf_open(filename.c_str(), SFM_READ, &inFileInfo);
                sf_command(wf, SFC_GET_INSTRUMENT, &inst, sizeof(inst));

                nframes = inFileInfo.frames * inFileInfo.channels;
                double data[nframes];

                sf_read_double(wf, data, nframes);

                sf_close(wf);

                std::vector<double> newbuffer(data, data + nframes);
                newPipeSample.data = newbuffer;
                newPipeSample.loopEnd = nframes;

                samples.push_back(newPipeSample);
                newSItem.selectedSample = samples.size() - 1;

                if (aElement["loops"] == 1)
                {
                    if (inst.loop_count > 0)
                    {
                        for (int ll = 0; ll < inst.loop_count; ll++)
                        {
                            loop newLoop;
                            newLoop.start = inst.loops[ll].start;
                            newLoop.end = inst.loops[ll].end;
                            newSItem.loops.push_back(newLoop);
                        }
                    }
                }
                newPipe.releases[aElement["velocity"]][aElement["time"]].push_back(newSItem);
            }
            ranks[it["name"]].pipes[ipe["number"]] = newPipe;
        }
    }

    int selectedThread = 0;
    for (auto &it : samples)
    {
        if (selectedThread >= available_threads)
        {
            selectedThread = 0;
        }
        it.thread = selectedThread;
        selectedThread += 1;
    }

    for (auto &it : enclosures)
    {
        it.second.recalculate();
    }

    for (auto &it : stops)
    {
        if (it.second.active == 1)
        {
            it.second.on(1);
        }
    }

    for (int i = 0; i < available_threads; i++)
    {
        audioThreads.push_back(threadItem());
        std::vector<double> newbuffer(FRAMES_PER_BUFFER * NUM_CHANNELS);
        audioThreads[i].buffer = newbuffer;
        std::fill(audioThreads[i].buffer.begin(), audioThreads[i].buffer.end(), SAMPLE_SILENCE);
        audioThreads[i].thread = std::thread(audioThreadFunc, i);
    };

    std::thread voicingThread(voicingThreadFunc);
    std::thread windThread(windThreadFunc);
    std::thread tremThread(tremThreadFunc);

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

    outputParameters.device = sconfig["audioDevice"];
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

    midiin->openPort(sconfig["midiDevice"]);
    midiin->setCallback(&MidiCallback);

    while (!exit_thread_flag)
    {
        Pa_Sleep(500);
    }

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

    voicingThread.join();
    windThread.join();
    tremThread.join();

    for (long unsigned int i = 0; i < audioThreads.size(); i++)
    {
        audioThreads[i].thread.join();
    }

    delete midiin;

    return err;
}