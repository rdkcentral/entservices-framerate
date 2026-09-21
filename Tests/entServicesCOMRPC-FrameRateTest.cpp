/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/

#ifndef MODULE_NAME
#define MODULE_NAME COMRPCTestApp
#endif

#include <iostream>
#include <Thunder/com/com.h>
#include <Thunder/core/core.h>
#include "Thunder/interfaces/IFrameRate.h"

#include <chrono>
#include <iomanip>
#include <sstream>

using namespace Thunder;

// RAII Wrapper for IFrameRate
class FrameRateProxy {
    public:
        explicit FrameRateProxy(Exchange::IFrameRate* frameRate) : _frameRate(frameRate) {}
        ~FrameRateProxy() {
            if (_frameRate != nullptr) {
                std::cout << "Releasing FrameRate proxy..." << std::endl;
                _frameRate->Release();
            }
        }

        Exchange::IFrameRate* operator->() const { return _frameRate; }
        Exchange::IFrameRate* Get() const { return _frameRate; }

    private:
        Exchange::IFrameRate* _frameRate;
};

/********************************* Test All IFrameRate Events **********************************/
class FrameRateEventHandler : public Exchange::IFrameRate::INotification {
    private:
        static std::string CurrentTimestamp() {
            using namespace std::chrono;
            auto now = system_clock::now();
            auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
            std::time_t t = system_clock::to_time_t(now);
            std::tm tm;
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            std::ostringstream oss;
            oss << std::put_time(&tm, "[%Y-%m-%d %H:%M:%S")
                << '.' << std::setfill('0') << std::setw(3) << ms.count() << "]";
            return oss.str();
        }

        template<typename... Args>
            void LogEvent(const std::string& eventName, Args&&... args) const {
                std::cout << CurrentTimestamp() << " " << eventName;
                ((std::cout << (sizeof...(Args) > 1 ? " " : "") << args), ...);
                std::cout << std::endl;
            }

    public:
        FrameRateEventHandler() : _refCount(1) {}
        void AddRef() const override {
            _refCount.fetch_add(1, std::memory_order_relaxed);
        }
        uint32_t Release() const override {
            uint32_t count = _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (count == 0) {
                delete this;
            }
            return count;
        }

        void OnFpsEvent(int average, int min, int max) override {
            LogEvent("OnFpsEvent -", "Average:", average, ", Min:", min, ", Max:", max);
        }

        void OnDisplayFrameRateChanging(const std::string& displayFrameRate) override {
            LogEvent("OnDisplayFrameRateChanging -", displayFrameRate);
        }

        void OnDisplayFrameRateChanged(const std::string& displayFrameRate) override {
            LogEvent("OnDisplayFrameRateChanged -", displayFrameRate);
        }

        BEGIN_INTERFACE_MAP(FrameRateEventHandler)
            INTERFACE_ENTRY(Exchange::IFrameRate::INotification)
        END_INTERFACE_MAP

    private:
        mutable std::atomic<uint32_t> _refCount;
};

int main(int argc, char* argv[])
{
    std::string callsign = (argc > 1) ? argv[1] : "org.rdk.FrameRate";
    /******************************************* Init *******************************************/
    // Get environment variables
    const char* thunderAccess = std::getenv("THUNDER_ACCESS");
    std::string envThunderAccess = (thunderAccess != nullptr) ? thunderAccess : "/tmp/communicator";
    std::cout << "Using THUNDER_ACCESS: " << envThunderAccess << std::endl;
    std::cout << "Using callsign: " << callsign << std::endl;

    // Initialize COMRPC
    Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), envThunderAccess.c_str());
    Core::ProxyType<RPC::CommunicatorClient> client = Core::ProxyType<RPC::CommunicatorClient>::Create(
            Core::NodeId(envThunderAccess.c_str()));

    if (client.IsValid() == false) {
        std::cerr << "Failed to create COMRPC client." << std::endl;
        return 1;
    }

    /************************************* Plugin Connector **************************************/
    // Create a proxy for the FrameRate plugin
    Exchange::IFrameRate* rawFrameRate = client->Open<Exchange::IFrameRate>(_T(callsign.c_str()));
    if (rawFrameRate == nullptr) {
        std::cerr << "Failed to connect to FrameRate plugin." << std::endl;
        return 1;
    }
    std::cout << "Connected to FrameRate plugin." << std::endl;

    // Use RAII wrapper for FrameRate proxy
    FrameRateProxy frameRate(rawFrameRate);

    /************************************ Subscribe to Events ************************************/
    FrameRateEventHandler eventHandler;
    frameRate->Register(&eventHandler);
    std::cout << "Event handler registered." << std::endl;

    /************************************* Test All Methods **************************************/
    bool success = false;
    std::string displayFrameRateStr;
    // Gets the Display Frame Rate - GetDisplayFrameRate method
    uint32_t result = frameRate->GetDisplayFrameRate(displayFrameRateStr, success);
    if (result == Core::ERROR_NONE && success) {
        std::cout << "Success GetDisplayFrameRate: framerate - " << displayFrameRateStr << std::endl;
    } else {
        std::cerr << "Failure GetDisplayFrameRate: result - " << result << " success - "<< std::boolalpha << success << std::endl;
    }

    // Gets framerate mode - GetFrmMode method
    int frmmode = 0;
    success = false;
    result = frameRate->GetFrmMode(frmmode, success);
    if (result == Core::ERROR_NONE && success) {
        std::cout << "Success GetFrmMode: auto-frm-mode - " << frmmode << std::endl;
    } else {
        std::cerr << "Failure GetFrmMode: result - " << result << " success - "<< std::boolalpha << success << std::endl;
    }

    // Sets the FPS data collection interval - SetCollectionFrequency method
    int frequencyInMS = 3000;
    success = false;
    result = frameRate->SetCollectionFrequency(frequencyInMS, success);
    if (result == Core::ERROR_NONE && success) {
        std::cout << "Success SetCollectionFrequency: frequency - " << frequencyInMS << std::endl;
    } else {
        std::cerr << "Failure SetCollectionFrequency: result - " << result << " success - "<< std::boolalpha << success << std::endl;
    }

    // Starts the FPS data collection - StartFpsCollection method
    success = false;
    result = frameRate->StartFpsCollection(success);
    if (result == Core::ERROR_NONE && success) {
        std::cout << "Success StartFpsCollection: success - " << std::boolalpha << success << std::endl;
    } else {
        std::cerr << "Failure StartFpsCollection: result - " << result << " success - "<< std::boolalpha << success << std::endl;
    }

    // Wait for a while to collect FPS data
    std::cout << "Waiting for " << (frequencyInMS * 5) << "ms to collect FPS data..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(frequencyInMS*5));

    // Sets the display framerate values - SetDisplayFrameRate method
    const char* displayFrameRate = "1920pxx1080px30";
    success = false;
    result = frameRate->SetDisplayFrameRate(displayFrameRate, success);
    if (result == Core::ERROR_NONE && success) {
        std::cout << "Success SetDisplayFrameRate: framerate - " << displayFrameRate << std::endl;
    } else {
        std::cerr << "Failure SetDisplayFrameRate: result - " << result << " success - "<< std::boolalpha << success << std::endl;
    }

    // Sets the auto framerate mode - SetFrmMode method
    int autoFrameRate = 1;
    success = false;
    result = frameRate->SetFrmMode(autoFrameRate, success);
    if (result == Core::ERROR_NONE && success) {
        std::cout << "Success SetFrmMode: frmmode - " << autoFrameRate << std::endl;
    } else {
        std::cerr << "Failure SetFrmMode: result - " << result << " success - "<< std::boolalpha << success << std::endl;
    }

    // Stops the FPS data collection - StopFpsCollection method
    success = false;
    result = frameRate->StopFpsCollection(success);
    if (result == Core::ERROR_NONE && success) {
        std::cout << "Success StopFpsCollection: success - " << std::boolalpha << success << std::endl;
    } else {
        std::cerr << "Failure StopFpsCollection: result - " << result << " success - "<< std::boolalpha << success << std::endl;
    }

    // Update the FPS values - UpdateFps method
    int fps = 60;
    success = false;
    result = frameRate->UpdateFps(fps, success);
    if (result == Core::ERROR_NONE && success) {
        std::cout << "Success UpdateFps: fps - " << fps << std::endl;
    } else {
        std::cerr << "Failure UpdateFps: result - " << result << " success - "<< std::boolalpha << success << std::endl;
    }

    /*********************************** Wait for Notifications ***********************************/

    std::cout << "Waiting for events... Press Ctrl+C to exit." << std::endl;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }

    /******************************************* Clean-Up *****************************************/
    // FrameRateProxy destructor will automatically release the proxy
    std::cout << "Exiting..." << std::endl;
    frameRate.Get()->Unregister(&eventHandler);
    return 0;
}

