﻿/*
 ------------------------------------------------------------------
 StreamPlugin
 Copyright (C) 2021 - present Neuro-Electronics Research Flanders

 This file is part of the Open Ephys GUI
 Copyright (C) 2016 Open Ephys
 ------------------------------------------------------------------

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */


#include "StreamPlugin.h"
#include <iostream>
#include <chrono>
#include <ctime>

StreamPlugin::StreamPlugin()
    : GenericProcessor("Falcon Output"), flatBuilder(1024)
{
    context = zmq_ctx_new();
    socket = 0;
    flag = 0;
    messageNumber = 0;
    port = 3335;

    setProcessorType(PROCESSOR_TYPE_SINK);
    if (!socket)
        createSocket();
}

StreamPlugin::~StreamPlugin()
{
    closeSocket();
    if (context)
    {
        zmq_ctx_destroy(context);
        context = 0;
    }
}

void StreamPlugin::createSocket()
{
    if (!socket)
    {
        socket = zmq_socket(context, ZMQ_PUB);

        if (!socket){
            std::cout << "couldn't create a socket" << std::endl;
            std::cout << zmq_strerror(zmq_errno()) << std::endl;
            jassert(false);
        }

        auto urlstring = "tcp://*:" + std::to_string(port);

        if (zmq_bind(socket, urlstring.c_str()))
        {
            std::cout << "couldn't open data socket" << std::endl;
            std::cout << zmq_strerror(zmq_errno()) << std::endl;
            jassert(false);
        }
    }
}

void StreamPlugin::closeSocket()
{
    if (socket)
    {
        std::cout << "close data socket" << std::endl;
        zmq_close(socket);
        socket = 0;
    }
}

void StreamPlugin::sendData(AudioSampleBuffer& audioBuffer, int nSamples,
                           uint64 timestamp, int sampleRate)
{
    
    messageNumber++;
    uint64_t nChannels = audioBuffer.getNumChannels();
    audioBuffer.setSize(nChannels, nSamples, true, true, true);

    // Create message
    auto samples = flatBuilder.CreateVector(*(audioBuffer.getArrayOfReadPointers()), nChannels*nSamples);
    auto zmqBuffer = openephysflatbuffer::CreateContinuousData(flatBuilder, samples,
                                                               nChannels, nSamples, timestamp,
                                                               messageNumber, sampleRate);
    flatBuilder.Finish(zmqBuffer);

    uint8_t *buf = flatBuilder.GetBufferPointer();
    int size = flatBuilder.GetSize();

    // Send packet
    zmq_msg_t request;
    zmq_msg_init_size(&request, size);
    memcpy(zmq_msg_data(&request), (void *)buf, size);
    int size_m = zmq_msg_send(&request, socket, 0);
    zmq_msg_close(&request);

    flatBuilder.Clear();
}

AudioProcessorEditor* StreamPlugin::createEditor()
{
    editor = new StreamPluginEditor(this, true);
    return editor;
}

void StreamPlugin::process(AudioSampleBuffer& buffer)
{
    if (!socket)
        createSocket();

    uint64_t firstTs = getTimestamp(0);
    float sampleRate;
    // Simplified sample rate detection (could check channel type or report
    // sampling rate of all channels separately in case they differ)
    if (dataChannelArray.size() > 0) 
    {
        sampleRate = dataChannelArray[0]->getSampleRate();
    }
    else 
    {   // this should never run - if no data channels, then no data...
        sampleRate = CoreServices::getGlobalSampleRate();
    }

    int ch_count = getTotalDataChannels();
    int sample_count = (int)getNumSamples(0);
    float* sampleRates = new float[ch_count];
    const float** ptrs = buffer.getArrayOfReadPointers();
    //const float** AP_channel_ptrs = new const float*[ch_count / 2];
    /*for (int i = 0; i < ch_count / 2; i++) {
        AP_channel_ptrs[i] = new const float[sample_count];
    }*/
    AudioSampleBuffer newBuffer = AudioSampleBuffer(ch_count / 2, sample_count);

    // std::cout << "CoreServices::getGlobalSampleRate() " << CoreServices::getGlobalSampleRate() << std::endl;
    // std::cout << "getTotalDataChannels() " << getTotalDataChannels() << std::endl;
    // std::cout << "buffer.getNumChannels()" << buffer.getNumChannels() << std::endl;
    int x = 0;
    for (int i = 0; i < ch_count; i++) {
        sampleRates[i] = dataChannelArray[i]->getSampleRate();
        if (dataChannelArray[i]->getSampleRate() == sampleRate) {
            //AP_channel_ptrs[x] = ptrs[i];
            newBuffer.copyFrom(x, 0, buffer, i, 0, sample_count);
            x++;
        }
    }

    // std::cout << "x " << x << std::endl;

    // buffer.setDataToReferTo(AP_channel_ptrs, ch_count / 2, sample_count);
    // std::cout << "getNumSamples(0)" << getNumSamples(0) << std::endl;
    // std::cout << "buffer.getNumSamples()" << buffer.getNumSamples() << std::endl;
    if(getNumSamples(0) > 0){
        sendData(newBuffer, getNumSamples(0), firstTs, (int)sampleRate);
    }
    // std::cout << "\n\n" << x << std::endl;
    if(1) {
        // https://stackoverflow.com/questions/997946/how-to-get-current-time-and-date-in-c 
        std::cout << "messageId: " << messageNumber << std::endl;
        long long x = 1000354800000;
        long long microseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

        std::cout << "tic: " << microseconds - x << "\n" << std::endl;
    }
}

