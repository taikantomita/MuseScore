/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2022 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "wasapiaudiodriver.h"

#include "log.h"
#include "translation.h"

#include "wasapiaudioclient.h"

using namespace winrt;
using namespace mu::audio;

static constexpr char DEFAULT_DEVICE_ID[] = "default";

inline int refTimeToSamples(const REFERENCE_TIME& t, double sampleRate) noexcept
{
    return sampleRate * ((double)t) * 0.0000001;
}

inline REFERENCE_TIME samplesToRefTime(int numSamples, double sampleRate) noexcept
{
    return (REFERENCE_TIME)((numSamples * 10000.0 * 1000.0 / sampleRate) + 0.5);
}

struct WasapiData {
    HANDLE clientStartedEvent;
    HANDLE clientFailedToStartEvent;
    HANDLE clientStoppedEvent;

    winrt::com_ptr<winrt::WasapiAudioClient> wasapiClient;
};

static WasapiData s_data;

WasapiAudioDriver::WasapiAudioDriver()
{
    s_data.clientStartedEvent = CreateEvent(NULL, FALSE, FALSE, L"WASAPI_Client_Started");
    s_data.clientFailedToStartEvent = CreateEvent(NULL, FALSE, FALSE, L"WASAPI_Client_Failed_To_Start");
    s_data.clientStoppedEvent = CreateEvent(NULL, FALSE, FALSE, L"WASAPI_Client_Stopped");

    m_devicesListener.startWithCallback([this]() {
        return availableOutputDevices();
    });

    m_devicesListener.devicesChanged().onNotify(this, [this]() {
        m_availableOutputDevicesChanged.notify();
    });
}

void WasapiAudioDriver::init()
{
}

std::string WasapiAudioDriver::name() const
{
    return "WASAPI_driver";
}

bool WasapiAudioDriver::open(const Spec& spec, Spec* activeSpec)
{
    if (!s_data.wasapiClient.get()) {
        s_data.wasapiClient
            = make_self<WasapiAudioClient>(s_data.clientStartedEvent, s_data.clientFailedToStartEvent, s_data.clientStoppedEvent);
    }

    m_desiredSpec = spec;

    s_data.wasapiClient->setBufferDuration(samplesToRefTime(spec.samples, spec.sampleRate));

    bool lowLatencyModeRequired = spec.samples <= s_data.wasapiClient->lowLatencyUpperBound();

    s_data.wasapiClient->setLowLatency(lowLatencyModeRequired);
    s_data.wasapiClient->setSampleRequestCallback(spec.callback);

    LOGI() << "WASAPI: trying to open the audio end-point with the following sample rate - " << spec.sampleRate;
    LOGI() << "WASAPI: trying to open the audio end-point with the following samples per channel number - " << spec.samples;

    hstring deviceId;

    if (m_deviceId.empty() || m_deviceId == DEFAULT_DEVICE_ID) {
        deviceId = s_data.wasapiClient->defaultDeviceId();
    } else {
        deviceId = to_hstring<std::string>(m_deviceId);
    }

    s_data.wasapiClient->asyncInitializeAudioDevice(deviceId);

    static constexpr DWORD handleCount = 2;
    const HANDLE handles[handleCount] = { s_data.clientStartedEvent, s_data.clientFailedToStartEvent };

    DWORD waitResult = WaitForMultipleObjects(handleCount, handles, false, INFINITE);
    if (waitResult != WAIT_OBJECT_0) {
        // Either the event was the second event (namely s_data.clientFailedToStartEvent)
        // Or some wait error occurred
        return false;
    }

    m_activeSpec = m_desiredSpec;
    m_activeSpec.sampleRate = s_data.wasapiClient->sampleRate();
    *activeSpec = m_activeSpec;

    m_isOpened = true;

    return true;
}

void WasapiAudioDriver::close()
{
    if (!s_data.wasapiClient.get()) {
        return;
    }

    s_data.wasapiClient->stopPlaybackAsync();

    WaitForSingleObject(s_data.clientStoppedEvent, INFINITE);
    s_data.wasapiClient = nullptr;

    m_isOpened = false;
}

bool WasapiAudioDriver::isOpened() const
{
    return m_isOpened;
}

AudioDeviceID WasapiAudioDriver::outputDevice() const
{
    return m_deviceId;
}

bool WasapiAudioDriver::selectOutputDevice(const AudioDeviceID& id)
{
    bool result = true;

    if (m_deviceId == id) {
        return result;
    }

    m_deviceId = id;

    if (isOpened()) {
        close();
        result = open(m_activeSpec, &m_activeSpec);
    }

    if (result) {
        m_outputDeviceChanged.notify();
    }

    return result;
}

bool WasapiAudioDriver::resetToDefaultOutputDevice()
{
    return selectOutputDevice(DEFAULT_DEVICE_ID);
}

mu::async::Notification WasapiAudioDriver::outputDeviceChanged() const
{
    return m_outputDeviceChanged;
}

AudioDeviceList WasapiAudioDriver::availableOutputDevices() const
{
    using namespace Windows::Devices::Enumeration;

    if (!s_data.wasapiClient.get()) {
        return {};
    }

    AudioDeviceList result;

    result.push_back({ DEFAULT_DEVICE_ID, trc("audio", "System default") });

    DeviceInformationCollection devices = s_data.wasapiClient->availableDevices();
    for (const auto& deviceInfo : devices) {
        AudioDevice device { to_string(deviceInfo.Id()), to_string(deviceInfo.Name()) };
        result.emplace_back(std::move(device));
    }

    return result;
}

mu::async::Notification WasapiAudioDriver::availableOutputDevicesChanged() const
{
    return m_availableOutputDevicesChanged;
}

unsigned int WasapiAudioDriver::outputDeviceBufferSize() const
{
    return m_activeSpec.samples;
}

bool WasapiAudioDriver::setOutputDeviceBufferSize(unsigned int bufferSize)
{
    bool result = true;

    if (isOpened()) {
        close();

        m_activeSpec.samples = bufferSize;
        result = open(m_activeSpec, &m_activeSpec);
    } else {
        m_desiredSpec.samples = bufferSize;
    }

    m_outputDeviceBufferSizeChanged.notify();

    return result;
}

mu::async::Notification WasapiAudioDriver::outputDeviceBufferSizeChanged() const
{
    return m_outputDeviceBufferSizeChanged;
}

std::vector<unsigned int> WasapiAudioDriver::availableOutputDeviceBufferSizes() const
{
    std::vector<unsigned int> result;

    unsigned int n = 4096;
    while (n >= MINIMUM_BUFFER_SIZE) {
        result.push_back(n);
        n /= 2;
    }

    return result;
}

void WasapiAudioDriver::resume()
{
}

void WasapiAudioDriver::suspend()
{
}
