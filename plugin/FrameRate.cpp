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

#include <exception>
#include "FrameRate.h"
#include "manager.hpp"
#include "UtilsJsonRpc.h"

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework
{
    namespace {
        static Plugin::Metadata<Plugin::FrameRate> metadata(
            // Version (Major, Minor, Patch)
            API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
            // Preconditions
            {},
            // Terminations
            {},
            // Controls
            {}
        );
    }

    namespace Plugin
    {
        /*
         *Register FrameRate module as wpeframework plugin
         **/
        SERVICE_REGISTRATION(FrameRate, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

        FrameRate::FrameRate()
            : _service(nullptr)
              , _connectionId(0)
              , _FrameRate(nullptr)
              , _FrameRateNotification(this)
        {
            SYSLOG(Logging::Startup, (_T("FrameRate Constructor")));
        }

        FrameRate::~FrameRate()
        {
            SYSLOG(Logging::Shutdown, (string(_T("FrameRate Destructor"))));
        }

        const string FrameRate::Initialize(PluginHost::IShell* service)
        {
            string message = "";

            ASSERT(nullptr != service);
            ASSERT(nullptr == _service);
            ASSERT(nullptr == _FrameRate);
            ASSERT(0 == _connectionId);

            SYSLOG(Logging::Startup, (_T("FrameRate::Initialize: PID=%u"), getpid()));

            try
            {
                device::Manager::Initialize();
                LOGINFO("device::Manager::Initialize success");
            }
            catch(const std::exception& e)
            {
                LOGERR("device::Manager::Initialize failed, Exception: {%s}", e.what());
            }

            _service = service;
            _service->AddRef();
            _service->Register(&_FrameRateNotification);
            _FrameRate = _service->Root<Exchange::IFrameRate>(_connectionId, 5000, _T("FrameRateImplementation"));

            if (nullptr != _FrameRate)
            {
                // Register for notifications
                _FrameRate->Register(&_FrameRateNotification);
                // Invoking Plugin API register to wpeframework
                Exchange::JFrameRate::Register(*this, _FrameRate);
            }
            else
            {
                SYSLOG(Logging::Startup, (_T("FrameRate::Initialize: Failed to initialise FrameRate plugin")));
                message = _T("FrameRate plugin could not be initialised");
            }

            return message;
        }

        void FrameRate::Deinitialize(PluginHost::IShell* service)
        {
            ASSERT(_service == service);

            SYSLOG(Logging::Shutdown, (string(_T("FrameRate::Deinitialize"))));

            // Make sure the Activated and Deactivated are no longer called before we start cleaning up..
            _service->Unregister(&_FrameRateNotification);

            if (nullptr != _FrameRate)
            {
                _FrameRate->Unregister(&_FrameRateNotification);
                Exchange::JFrameRate::Unregister(*this);

                // Stop processing:
                RPC::IRemoteConnection* connection = service->RemoteConnection(_connectionId);
                VARIABLE_IS_NOT_USED uint32_t result = _FrameRate->Release();
                _FrameRate = nullptr;

                // It should have been the last reference we are releasing,
                // so it should endup in a DESTRUCTION_SUCCEEDED, if not we
                // are leaking...
                ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);

                // If this was running in a (container) process...
                if (connection != nullptr)
                {
                    // Lets trigger the cleanup sequence for
                    // out-of-process code. Which will guard
                    // that unwilling processes, get shot if
                    // not stopped friendly :-)
                    try
                    {
                        connection->Terminate();
                        TRACE(Trace::Warning, (_T("Connection terminated successfully")));
                    }
                    catch (const std::exception& e)
                    {
                        std::string errorMessage = "Failed to terminate connection: ";
                        errorMessage += e.what();
                        TRACE(Trace::Warning, (_T("%s"), errorMessage.c_str()));
                    }
                    connection->Release();
                }
            }

            _connectionId = 0;
            _service->Release();
            _service = nullptr;

            try
            {
                device::Manager::DeInitialize();
                LOGINFO("device::Manager::DeInitialize success");
            }
            catch(const std::exception& e)
            {
                LOGERR("device::Manager::DeInitialize failed, Exception: {%s}", e.what());
            }

            SYSLOG(Logging::Shutdown, (string(_T("FrameRate de-initialised"))));
        }

        string FrameRate::Information() const
        {
            return "Plugin which exposes FrameRate related methods and notifications.";
        }

        void FrameRate::Deactivated(RPC::IRemoteConnection* connection)
        {
            if (connection->Id() == _connectionId) {
                ASSERT(nullptr != _service);
                Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(_service, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
            }
        }
    } // namespace Plugin
} // namespace WPEFramework
