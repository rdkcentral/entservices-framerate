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

#pragma once

#include "Module.h"

#include <interfaces/IFrameRate.h>
#include <interfaces/json/JFrameRate.h>
#include <interfaces/json/JsonData_FrameRate.h>

namespace WPEFramework
{
    namespace Plugin
    {
        class FrameRate : public PluginHost::IPlugin, public PluginHost::JSONRPC
        {
            private:
                class Notification : public RPC::IRemoteConnection::INotification, public Exchange::IFrameRate::INotification
            {
                private:
                    Notification() = delete;
                    Notification(const Notification&) = delete;
                    Notification& operator=(const Notification&) = delete;
                public:
                    explicit Notification(FrameRate* parent)
                        : _parent(*parent)
                    {
                        ASSERT(parent != nullptr);
                    }

                    virtual ~Notification()
                    {
                    }

                    BEGIN_INTERFACE_MAP(Notification)
                        INTERFACE_ENTRY(Exchange::IFrameRate::INotification)
                        INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
                    END_INTERFACE_MAP

                    void Activated(RPC::IRemoteConnection*) override
                    {
                    }

                    void Deactivated(RPC::IRemoteConnection *connection) override
                    {
                        _parent.Deactivated(connection);
                    }

                    void OnFpsEvent(int average, int min, int max ) override
                    {
                        Exchange::JFrameRate::Event::OnFpsEvent(_parent, average, min, max);
                    }

                    void OnDisplayFrameRateChanging(const string& displayFrameRate) override
                    {
                        Exchange::JFrameRate::Event::OnDisplayFrameRateChanging(_parent, displayFrameRate);
                    }

                    void OnDisplayFrameRateChanged(const string& displayFrameRate) override
                    {
                        Exchange::JFrameRate::Event::OnDisplayFrameRateChanged(_parent, displayFrameRate);
                    }

                private:
                    FrameRate& _parent;
            };

            public:
                FrameRate(const FrameRate&) = delete;
                FrameRate& operator=(const FrameRate&) = delete;

                FrameRate();
                virtual ~FrameRate();

                BEGIN_INTERFACE_MAP(FrameRate)
                    INTERFACE_ENTRY(PluginHost::IPlugin)
                    INTERFACE_ENTRY(PluginHost::IDispatcher)
                    INTERFACE_AGGREGATE(Exchange::IFrameRate, _FrameRate)
                END_INTERFACE_MAP

                //  IPlugin methods
                // ------------------------------------------------------------------------------------------
                const string Initialize(PluginHost::IShell* service) override;
                void Deinitialize(PluginHost::IShell* service) override;
                string Information() const override;

            private:
                void Deactivated(RPC::IRemoteConnection* connection);

            private:
                PluginHost::IShell* _service{};
                uint32_t _connectionId{};
                Exchange::IFrameRate* _FrameRate{};
                Core::Sink<Notification> _FrameRateNotification;
        };
    } // namespace Plugin
} // namespace WPEFramework
