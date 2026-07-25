/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2024 RDK Management
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

#include <gtest/gtest.h>
#include "COMLinkMock.h"
#include <gmock/gmock.h>

#include <condition_variable>
#include <mutex>
#include <chrono>

#include "FrameRate.h"

#include "FactoriesImplementation.h"
#include "HostMock.h"
#include "IarmBusMock.h"
#include "ServiceMock.h"
#include "VideoDeviceMock.h"
#include "devicesettings.h"
#include "ManagerMock.h"
#include "ThunderPortability.h"
#include "FrameRateImplementation.h"
#include "FrameRateMock.h"
#include "WorkerPoolImplementation.h"
#include "WrapsMock.h"

using namespace WPEFramework;

using ::testing::NiceMock;

class FrameRateTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::FrameRate> plugin;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    NiceMock<ServiceMock> service;
    NiceMock<COMLinkMock> comLinkMock;
    Core::ProxyType<WorkerPoolImplementation> workerPool;
    Core::ProxyType<Plugin::FrameRateImplementation> FrameRateImplem;
    Exchange::IFrameRate::INotification *FrameRateNotification = nullptr;
    NiceMock<FactoriesImplementation> factoriesImplementation;
    PLUGINHOST_DISPATCHER *dispatcher;
    string response;
    Core::JSONRPC::Message message;
    ServiceMock  *p_serviceMock  = nullptr;
    WrapsImplMock* p_wrapsImplMock = nullptr;
    FrameRateMock* p_framerateMock = nullptr;
    HostImplMock      *p_hostImplMock = nullptr;
    VideoDeviceMock   *p_videoDeviceMock = nullptr;
    IARM_EventHandler_t _iarmDSFramerateEventHandler;
    IarmBusImplMock   *p_iarmBusImplMock = nullptr ;
    ManagerImplMock   *p_managerImplMock = nullptr ;

    FrameRateTest()
        : plugin(Core::ProxyType<Plugin::FrameRate>::Create())
        , handler(*(plugin))
        , INIT_CONX(1, 0)
	, workerPool(Core::ProxyType<WorkerPoolImplementation>::Create(
          2, Core::Thread::DefaultStackSize(), 16))
    {
	    p_serviceMock = new NiceMock <ServiceMock>;

        p_framerateMock  = new NiceMock <FrameRateMock>;

        p_wrapsImplMock = new NiceMock<WrapsImplMock>;
        Wraps::setImpl(p_wrapsImplMock);

        p_managerImplMock  = new NiceMock <ManagerImplMock>;
        device::Manager::setImpl(p_managerImplMock);

        p_hostImplMock  = new NiceMock <HostImplMock>;
        device::Host::setImpl(p_hostImplMock);

        EXPECT_CALL(*p_managerImplMock, Initialize())
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Return());

        PluginHost::IFactories::Assign(&factoriesImplementation);

        dispatcher = static_cast<PLUGINHOST_DISPATCHER*>(
        plugin->QueryInterface(PLUGINHOST_DISPATCHER_ID));
        dispatcher->Activate(&service);

        ON_CALL(*p_framerateMock, Register(::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](Exchange::IFrameRate::INotification* notification) {
                    FrameRateNotification = notification;
		    return Core::ERROR_NONE;
                }));

#ifdef USE_THUNDER_R4
        ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_))
                .WillByDefault(::testing::Invoke(
                    [&](const RPC::Object& object, const uint32_t waitTime, uint32_t& connectionId) {
                        FrameRateImplem = Core::ProxyType<Plugin::FrameRateImplementation>::Create();
                        return &FrameRateImplem;
                    }));
#else
	ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
	    .WillByDefault(::testing::Return(FrameRateImplem));
#endif
        p_iarmBusImplMock  = new NiceMock <IarmBusImplMock>;
        IarmBus::setImpl(p_iarmBusImplMock);

        Core::IWorkerPool::Assign(&(*workerPool));
            workerPool->Run();

        plugin->Initialize(&service);

        device::VideoDevice videoDevice;
        p_videoDeviceMock  = new NiceMock <VideoDeviceMock>;
        device::VideoDevice::setImpl(p_videoDeviceMock);

        ON_CALL(*p_hostImplMock, getVideoDevices())
            .WillByDefault(::testing::Return(device::List<device::VideoDevice>({ videoDevice })));
    }
    virtual ~FrameRateTest()
    {
        plugin->Deinitialize(&service);
        Core::IWorkerPool::Assign(nullptr);
        workerPool.Release();

	    if (p_serviceMock != nullptr)
        {
            delete p_serviceMock;
            p_serviceMock = nullptr;
        }

        if (p_framerateMock != nullptr) {
            delete p_framerateMock;
            p_framerateMock = nullptr;
        }

        Wraps::setImpl(nullptr);
        if (p_wrapsImplMock != nullptr) {
            delete p_wrapsImplMock;
            p_wrapsImplMock = nullptr;
        }

        device::VideoDevice::setImpl(nullptr);
        if (p_videoDeviceMock != nullptr)
        {
            delete p_videoDeviceMock;
            p_videoDeviceMock = nullptr;
        }

        EXPECT_CALL(*p_managerImplMock, DeInitialize())
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Return());

        device::Manager::setImpl(nullptr);
        if (p_managerImplMock != nullptr)
        {
            delete p_managerImplMock;
            p_managerImplMock = nullptr;
        }

        device::Host::setImpl(nullptr);
        if (p_hostImplMock != nullptr)
        {
            delete p_hostImplMock;
            p_hostImplMock = nullptr;
        }
        dispatcher->Deactivate();
        dispatcher->Release();

        PluginHost::IFactories::Assign(nullptr);
	IarmBus::setImpl(nullptr);
        if (p_iarmBusImplMock != nullptr) {
            delete p_iarmBusImplMock;
            p_iarmBusImplMock = nullptr;
        }

    }
};

typedef enum : uint32_t {
    FrameRate_OnDisplayFrameRateChanging = 0x00000001,
    FrameRate_OnDisplayFrameRateChanged = 0x00000002,
    FrameRate_OnFpsEvent = 0x00000004,
} FrameRateEventType_t;

class L1FrameRateNotificationHandler : public Exchange::IFrameRate::INotification {
    private:
        std::mutex m_mutex;
        std::condition_variable m_condition_variable;
        uint32_t m_event_signalled;
        bool m_OnDisplayFrameRateChanging_signalled = false;
        bool m_OnDisplayFrameRateChanged_signalled = false;
        bool m_OnFpsEvent_signalled = false;
        string m_lastFrameRate;
        int m_lastAverage;
        int m_lastMin;
        int m_lastMax;
        mutable uint32_t m_refCount;

        BEGIN_INTERFACE_MAP(L1FrameRateNotificationHandler)
        INTERFACE_ENTRY(Exchange::IFrameRate::INotification)
        END_INTERFACE_MAP

    public:
        L1FrameRateNotificationHandler() : m_event_signalled(0), m_lastAverage(0), m_lastMin(0), m_lastMax(0), m_refCount(1) {}
        ~L1FrameRateNotificationHandler() {}

        uint32_t AddRef() const override
        {
            return Core::InterlockedIncrement(m_refCount);
        }

        uint32_t Release() const override
        {
            uint32_t result = Core::InterlockedDecrement(m_refCount);
            if (result == 0) {
                delete this;
            }
            return result;
        }

        void OnDisplayFrameRateChanging(const string& frameRate) override
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_event_signalled |= FrameRate_OnDisplayFrameRateChanging;
            m_OnDisplayFrameRateChanging_signalled = true;
            m_lastFrameRate = frameRate;
            m_condition_variable.notify_one();
        }

        void OnDisplayFrameRateChanged(const string& frameRate) override
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_event_signalled |= FrameRate_OnDisplayFrameRateChanged;
            m_OnDisplayFrameRateChanged_signalled = true;
            m_lastFrameRate = frameRate;
            m_condition_variable.notify_one();
        }

        void OnFpsEvent(const int average, const int min, const int max) override
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_event_signalled |= FrameRate_OnFpsEvent;
            m_OnFpsEvent_signalled = true;
            m_lastAverage = average;
            m_lastMin = min;
            m_lastMax = max;
            m_condition_variable.notify_one();
        }

        bool WaitForRequestStatus(uint32_t timeout_ms, FrameRateEventType_t expected_status)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            auto now = std::chrono::system_clock::now();
            std::chrono::milliseconds timeout(timeout_ms);
            bool signalled = false;

            while (!(expected_status & m_event_signalled))
            {
                if (m_condition_variable.wait_until(lock, now + timeout) == std::cv_status::timeout)
                {
                    break;
                }
            }

            switch(expected_status)
            {
                case FrameRate_OnDisplayFrameRateChanging:
                    signalled = m_OnDisplayFrameRateChanging_signalled;
                    break;
                case FrameRate_OnDisplayFrameRateChanged:
                    signalled = m_OnDisplayFrameRateChanged_signalled;
                    break;
                case FrameRate_OnFpsEvent:
                    signalled = m_OnFpsEvent_signalled;
                    break;
                default:
                    signalled = false;
                    break;
            }

            return signalled;
        }

        string GetLastFrameRate() const { return m_lastFrameRate; }
        int GetLastAverage() const { return m_lastAverage; }
        int GetLastMin() const { return m_lastMin; }
        int GetLastMax() const { return m_lastMax; }

        void Reset()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_event_signalled = 0;
            m_OnDisplayFrameRateChanging_signalled = false;
            m_OnDisplayFrameRateChanged_signalled = false;
            m_OnFpsEvent_signalled = false;
            m_lastFrameRate.clear();
            m_lastAverage = 0;
            m_lastMin = 0;
            m_lastMax = 0;
        }
};


TEST_F(FrameRateTest, GetDisplayFrameRate_Success)
{
    ON_CALL(*p_videoDeviceMock, getCurrentDisframerate(::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArrayArgument<0>("1920x1080x60", "1920x1080x60" + 13),
            ::testing::Return(0)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
    EXPECT_TRUE(response.find("\"framerate\":\"1920x1080x60\"") != string::npos);
}

TEST_F(FrameRateTest, GetDisplayFrameRate_NoVideoDevices)
{
    ON_CALL(*p_hostImplMock, getVideoDevices())
        .WillByDefault(::testing::Return(device::List<device::VideoDevice>()));

    EXPECT_EQ(Core::ERROR_NOT_SUPPORTED, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
}

TEST_F(FrameRateTest, GetDisplayFrameRate_DeviceError)
{
    ON_CALL(*p_videoDeviceMock, getCurrentDisframerate(::testing::_))
        .WillByDefault(::testing::Return(1));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
}

TEST_F(FrameRateTest, GetFrmMode_Success)
{
    ON_CALL(*p_videoDeviceMock, getFRFMode(::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgPointee<0>(1),
            ::testing::Return(0)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getFrmMode"), _T("{}"), response));
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
    EXPECT_TRUE(response.find("\"auto-frm-mode\":1") != string::npos);
}

TEST_F(FrameRateTest, GetFrmMode_NoVideoDevices)
{
    ON_CALL(*p_hostImplMock, getVideoDevices())
        .WillByDefault(::testing::Return(device::List<device::VideoDevice>()));

    EXPECT_EQ(Core::ERROR_NOT_SUPPORTED, handler.Invoke(connection, _T("getFrmMode"), _T("{}"), response));
}

TEST_F(FrameRateTest, GetFrmMode_DeviceError)
{
    ON_CALL(*p_videoDeviceMock, getFRFMode(::testing::_))
        .WillByDefault(::testing::Return(1));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getFrmMode"), _T("{}"), response));
}

TEST_F(FrameRateTest, SetCollectionFrequency_Success)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setCollectionFrequency"), _T("{\"frequency\":5000}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, SetCollectionFrequency_InvalidParameterTooLow)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setCollectionFrequency"), _T("{\"frequency\":50}"), response));
}

TEST_F(FrameRateTest, SetCollectionFrequency_MinimumValue)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setCollectionFrequency"), _T("{\"frequency\":100}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, SetDisplayFrameRate_Success)
{
    ON_CALL(*p_videoDeviceMock, setDisplayframerate(::testing::_))
        .WillByDefault(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"1920x1080x60\"}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, SetDisplayFrameRate_InvalidFormat_MissingX)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"1920_1080_60\"}"), response));
}

TEST_F(FrameRateTest, SetDisplayFrameRate_InvalidFormat_OneX)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"1920x108060\"}"), response));
}

TEST_F(FrameRateTest, SetDisplayFrameRate_InvalidFormat_NonDigitStart)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"x1920x1080x60\"}"), response));
}

TEST_F(FrameRateTest, SetDisplayFrameRate_InvalidFormat_NonDigitEnd)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"1920x1080x60x\"}"), response));
}

TEST_F(FrameRateTest, SetDisplayFrameRate_NoVideoDevices)
{
    ON_CALL(*p_hostImplMock, getVideoDevices())
        .WillByDefault(::testing::Return(device::List<device::VideoDevice>()));

    EXPECT_EQ(Core::ERROR_NOT_SUPPORTED, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"1920x1080x60\"}"), response));
}

TEST_F(FrameRateTest, SetDisplayFrameRate_DeviceError)
{
    ON_CALL(*p_videoDeviceMock, setDisplayframerate(::testing::_))
        .WillByDefault(::testing::Return(1));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"1920x1080x60\"}"), response));
}

TEST_F(FrameRateTest, SetFrmMode_Success_ModeZero)
{
    ON_CALL(*p_videoDeviceMock, setFRFMode(::testing::_))
        .WillByDefault(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":0}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, SetFrmMode_Success_ModeOne)
{
    ON_CALL(*p_videoDeviceMock, setFRFMode(::testing::_))
        .WillByDefault(::testing::Return(0));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":1}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, SetFrmMode_InvalidParameter_NegativeValue)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":-1}"), response));
}

TEST_F(FrameRateTest, SetFrmMode_InvalidParameter_ValueTwo)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":2}"), response));
}

TEST_F(FrameRateTest, SetFrmMode_NoVideoDevices)
{
    ON_CALL(*p_hostImplMock, getVideoDevices())
        .WillByDefault(::testing::Return(device::List<device::VideoDevice>()));

    EXPECT_EQ(Core::ERROR_NOT_SUPPORTED, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":1}"), response));
}

TEST_F(FrameRateTest, SetFrmMode_DeviceError)
{
    ON_CALL(*p_videoDeviceMock, setFRFMode(::testing::_))
        .WillByDefault(::testing::Return(1));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":1}"), response));
}

TEST_F(FrameRateTest, StartFpsCollection_Success)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("startFpsCollection"), _T("{}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, StartFpsCollection_AlreadyInProgress)
{
    handler.Invoke(connection, _T("startFpsCollection"), _T("{}"), response);
    
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("startFpsCollection"), _T("{}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, StopFpsCollection_Success)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("stopFpsCollection"), _T("{}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, StopFpsCollection_NotStarted)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("stopFpsCollection"), _T("{}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, UpdateFps_Success)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("updateFps"), _T("{\"newFpsValue\":30}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, UpdateFps_SuccessZeroValue)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("updateFps"), _T("{\"newFpsValue\":0}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, UpdateFps_InvalidParameter_NegativeValue)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("updateFps"), _T("{\"newFpsValue\":-1}"), response));
}

TEST_F(FrameRateTest, UpdateFps_HighValue)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("updateFps"), _T("{\"newFpsValue\":120}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, OnReportFpsTimer_WithUpdates)
{
    L1FrameRateNotificationHandler* notificationHandler = new L1FrameRateNotificationHandler();

    Plugin::FrameRateImplementation::_instance->Register(notificationHandler);
    
    bool success;
    Plugin::FrameRateImplementation::_instance->UpdateFps(60, success);
    Plugin::FrameRateImplementation::_instance->UpdateFps(58, success);
    Plugin::FrameRateImplementation::_instance->UpdateFps(62, success);
    
    Plugin::FrameRateImplementation::_instance->onReportFpsTimer();
    
    EXPECT_TRUE(notificationHandler->WaitForRequestStatus(1000, FrameRate_OnFpsEvent));
    EXPECT_EQ(60, notificationHandler->GetLastAverage());
    EXPECT_EQ(58, notificationHandler->GetLastMin());
    EXPECT_EQ(62, notificationHandler->GetLastMax());
    
    Plugin::FrameRateImplementation::_instance->Unregister(notificationHandler);
    
    notificationHandler->Release();
}

TEST_F(FrameRateTest, OnReportFpsTimer_NoUpdates)
{
    L1FrameRateNotificationHandler* notificationHandler = new L1FrameRateNotificationHandler();
    
    if (Plugin::FrameRateImplementation::_instance != nullptr)
    {
        Plugin::FrameRateImplementation::_instance->Register(notificationHandler);
        
        Plugin::FrameRateImplementation::_instance->onReportFpsTimer();
        
        EXPECT_TRUE(notificationHandler->WaitForRequestStatus(1000, FrameRate_OnFpsEvent));
        EXPECT_EQ(-1, notificationHandler->GetLastAverage());
        
        Plugin::FrameRateImplementation::_instance->Unregister(notificationHandler);
    }
    
    notificationHandler->Release();
}

TEST_F(FrameRateTest, OnReportFpsTimer_SingleUpdate)
{
    L1FrameRateNotificationHandler* notificationHandler = new L1FrameRateNotificationHandler();
    
    if (Plugin::FrameRateImplementation::_instance != nullptr)
    {
        Plugin::FrameRateImplementation::_instance->Register(notificationHandler);
        
        bool success;
        Plugin::FrameRateImplementation::_instance->UpdateFps(30, success);
        
        Plugin::FrameRateImplementation::_instance->onReportFpsTimer();
        
        EXPECT_TRUE(notificationHandler->WaitForRequestStatus(1000, FrameRate_OnFpsEvent));
        EXPECT_EQ(30, notificationHandler->GetLastAverage());
        EXPECT_EQ(30, notificationHandler->GetLastMin());
        EXPECT_EQ(30, notificationHandler->GetLastMax());
        
        Plugin::FrameRateImplementation::_instance->Unregister(notificationHandler);
    }
    
    notificationHandler->Release();
}

TEST_F(FrameRateTest, OnDisplayFrameratePreChange_ValidFrameRate)
{
    L1FrameRateNotificationHandler* notificationHandler = new L1FrameRateNotificationHandler();
    
    if (Plugin::FrameRateImplementation::_instance != nullptr)
    {
        Plugin::FrameRateImplementation::_instance->Register(notificationHandler);
        
        Plugin::FrameRateImplementation::_instance->OnDisplayFrameratePreChange("3840x2160x48");
        
        EXPECT_TRUE(notificationHandler->WaitForRequestStatus(1000, FrameRate_OnDisplayFrameRateChanging));
        EXPECT_EQ("3840x2160x48", notificationHandler->GetLastFrameRate());
        
        Plugin::FrameRateImplementation::_instance->Unregister(notificationHandler);
    }
    
    notificationHandler->Release();
}

TEST_F(FrameRateTest, OnDisplayFrameratePreChange_EmptyFrameRate)
{
    L1FrameRateNotificationHandler* notificationHandler = new L1FrameRateNotificationHandler();
    
    if (Plugin::FrameRateImplementation::_instance != nullptr)
    {
        Plugin::FrameRateImplementation::_instance->Register(notificationHandler);
        
        Plugin::FrameRateImplementation::_instance->OnDisplayFrameratePreChange("");
        
        EXPECT_TRUE(notificationHandler->WaitForRequestStatus(1000, FrameRate_OnDisplayFrameRateChanging));
        EXPECT_EQ("", notificationHandler->GetLastFrameRate());
        
        Plugin::FrameRateImplementation::_instance->Unregister(notificationHandler);
    }
    
    notificationHandler->Release();
}

TEST_F(FrameRateTest, OnDisplayFrameratePreChange_StandardResolution)
{
    L1FrameRateNotificationHandler* notificationHandler = new L1FrameRateNotificationHandler();
    
    if (Plugin::FrameRateImplementation::_instance != nullptr)
    {
        Plugin::FrameRateImplementation::_instance->Register(notificationHandler);
        
        Plugin::FrameRateImplementation::_instance->OnDisplayFrameratePreChange("1920x1080x60");
        
        EXPECT_TRUE(notificationHandler->WaitForRequestStatus(1000, FrameRate_OnDisplayFrameRateChanging));
        EXPECT_EQ("1920x1080x60", notificationHandler->GetLastFrameRate());
        
        Plugin::FrameRateImplementation::_instance->Unregister(notificationHandler);
    }
    
    notificationHandler->Release();
}

TEST_F(FrameRateTest, OnDisplayFrameratePostChange_ValidFrameRate)
{
    L1FrameRateNotificationHandler* notificationHandler = new L1FrameRateNotificationHandler();
    
    if (Plugin::FrameRateImplementation::_instance != nullptr)
    {
        Plugin::FrameRateImplementation::_instance->Register(notificationHandler);
        
        Plugin::FrameRateImplementation::_instance->OnDisplayFrameratePostChange("3840x2160x48");
        
        EXPECT_TRUE(notificationHandler->WaitForRequestStatus(1000, FrameRate_OnDisplayFrameRateChanged));
        EXPECT_EQ("3840x2160x48", notificationHandler->GetLastFrameRate());
        
        Plugin::FrameRateImplementation::_instance->Unregister(notificationHandler);
    }
    
    notificationHandler->Release();
}

TEST_F(FrameRateTest, OnDisplayFrameratePostChange_EmptyFrameRate)
{
    L1FrameRateNotificationHandler* notificationHandler = new L1FrameRateNotificationHandler();
    
    if (Plugin::FrameRateImplementation::_instance != nullptr)
    {
        Plugin::FrameRateImplementation::_instance->Register(notificationHandler);
        
        Plugin::FrameRateImplementation::_instance->OnDisplayFrameratePostChange("");
        
        EXPECT_TRUE(notificationHandler->WaitForRequestStatus(1000, FrameRate_OnDisplayFrameRateChanged));
        EXPECT_EQ("", notificationHandler->GetLastFrameRate());
        
        Plugin::FrameRateImplementation::_instance->Unregister(notificationHandler);
    }
    
    notificationHandler->Release();
}

TEST_F(FrameRateTest, OnDisplayFrameratePostChange_StandardResolution)
{
    L1FrameRateNotificationHandler* notificationHandler = new L1FrameRateNotificationHandler();
    
    if (Plugin::FrameRateImplementation::_instance != nullptr)
    {
        Plugin::FrameRateImplementation::_instance->Register(notificationHandler);
        
        Plugin::FrameRateImplementation::_instance->OnDisplayFrameratePostChange("1920x1080x60");
        
        EXPECT_TRUE(notificationHandler->WaitForRequestStatus(1000, FrameRate_OnDisplayFrameRateChanged));
        EXPECT_EQ("1920x1080x60", notificationHandler->GetLastFrameRate());
        
        Plugin::FrameRateImplementation::_instance->Unregister(notificationHandler);
    }
    
    notificationHandler->Release();
}

TEST_F(FrameRateTest, OnReportFpsTimer_MultipleUpdatesAverageCalculation)
{
    L1FrameRateNotificationHandler* notificationHandler = new L1FrameRateNotificationHandler();
    
    if (Plugin::FrameRateImplementation::_instance != nullptr)
    {
        Plugin::FrameRateImplementation::_instance->Register(notificationHandler);
        
        bool success;
        Plugin::FrameRateImplementation::_instance->UpdateFps(50, success);
        Plugin::FrameRateImplementation::_instance->UpdateFps(60, success);
        Plugin::FrameRateImplementation::_instance->UpdateFps(70, success);
        Plugin::FrameRateImplementation::_instance->UpdateFps(80, success);
        
        Plugin::FrameRateImplementation::_instance->onReportFpsTimer();
        
        EXPECT_TRUE(notificationHandler->WaitForRequestStatus(1000, FrameRate_OnFpsEvent));
        EXPECT_EQ(65, notificationHandler->GetLastAverage());
        EXPECT_EQ(50, notificationHandler->GetLastMin());
        EXPECT_EQ(80, notificationHandler->GetLastMax());
        
        Plugin::FrameRateImplementation::_instance->Unregister(notificationHandler);
    }
    
    notificationHandler->Release();
}

TEST_F(FrameRateTest, OnReportFpsTimer_ZeroFpsUpdate)
{
    L1FrameRateNotificationHandler* notificationHandler = new L1FrameRateNotificationHandler();
    
    if (Plugin::FrameRateImplementation::_instance != nullptr)
    {
        Plugin::FrameRateImplementation::_instance->Register(notificationHandler);
        
        bool success;
        Plugin::FrameRateImplementation::_instance->UpdateFps(0, success);
        
        Plugin::FrameRateImplementation::_instance->onReportFpsTimer();
        
        EXPECT_TRUE(notificationHandler->WaitForRequestStatus(1000, FrameRate_OnFpsEvent));
        EXPECT_EQ(0, notificationHandler->GetLastAverage());
        EXPECT_EQ(0, notificationHandler->GetLastMin());
        EXPECT_EQ(0, notificationHandler->GetLastMax());
        
        Plugin::FrameRateImplementation::_instance->Unregister(notificationHandler);
    }
    
    notificationHandler->Release();
}

TEST_F(FrameRateTest, Information_Success)
{
    string info = plugin->Information();
    EXPECT_EQ("Plugin which exposes FrameRate related methods and notifications.", info);
}

/** Negative Test Cases **/
TEST_F(FrameRateTest, setCollectionFrequency_InvalidParameter_BelowMinimum)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setCollectionFrequency"), _T("{\"frequency\":50}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, updateFps_InvalidParameter_NegativeValue)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("updateFps"), _T("{\"newFpsValue\":-1}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setFrmMode_InvalidParameter_InvalidMode)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":2}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setFrmMode_InvalidParameter_NegativeMode)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":-1}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setFrmMode_NoVideoDevices)
{
    ON_CALL(*p_hostImplMock, getVideoDevices())
        .WillByDefault(::testing::Return(device::List<device::VideoDevice>()));

    EXPECT_EQ(Core::ERROR_NOT_SUPPORTED, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":0}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setFrmMode_DeviceException)
{
    ON_CALL(*p_videoDeviceMock, setFRFMode(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](int param) {
                throw device::Exception("Test exception");
                return 0;
            }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":0}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setFrmMode_DeviceError)
{
    ON_CALL(*p_videoDeviceMock, setFRFMode(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](int param) {
                EXPECT_EQ(param, 0);
                return 1;
            }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":0}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, getFrmMode_NoVideoDevices)
{
    ON_CALL(*p_hostImplMock, getVideoDevices())
        .WillByDefault(::testing::Return(device::List<device::VideoDevice>()));

    EXPECT_EQ(Core::ERROR_NOT_SUPPORTED, handler.Invoke(connection, _T("getFrmMode"), _T("{}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, getFrmMode_DeviceException)
{
    ON_CALL(*p_videoDeviceMock, getFRFMode(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](int* param) {
                throw device::Exception("Test exception");
                *param = 0;
                return 0;
            }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getFrmMode"), _T("{}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, getFrmMode_DeviceError)
{
    ON_CALL(*p_videoDeviceMock, getFRFMode(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](int* param) {
                *param = 0;
                return 1;
            }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getFrmMode"), _T("{}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setDisplayFrameRate_InvalidParameter_InvalidFormat_NoX)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"3840z2160px48\"}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setDisplayFrameRate_InvalidParameter_InvalidFormat_OneX)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"3840x2160z48\"}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setDisplayFrameRate_InvalidParameter_InvalidFormat_StartsWithLetter)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"a3840x2160px48\"}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setDisplayFrameRate_InvalidParameter_InvalidFormat_EndsWithLetter)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"3840x2160px48a\"}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setDisplayFrameRate_NoVideoDevices)
{
    ON_CALL(*p_hostImplMock, getVideoDevices())
        .WillByDefault(::testing::Return(device::List<device::VideoDevice>()));

    EXPECT_EQ(Core::ERROR_NOT_SUPPORTED, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"3840x2160px48\"}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setDisplayFrameRate_DeviceException)
{
    ON_CALL(*p_videoDeviceMock, setDisplayframerate(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](const char* param) {
                throw device::Exception("Test exception");
                return 0;
            }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"3840x2160px48\"}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setDisplayFrameRate_DeviceError)
{
    ON_CALL(*p_videoDeviceMock, setDisplayframerate(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](const char* param) {
                EXPECT_EQ(param, string("3840x2160px48"));
                return 1;
            }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"3840x2160px48\"}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, getDisplayFrameRate_NoVideoDevices)
{
    ON_CALL(*p_hostImplMock, getVideoDevices())
        .WillByDefault(::testing::Return(device::List<device::VideoDevice>()));

    EXPECT_EQ(Core::ERROR_NOT_SUPPORTED, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, getDisplayFrameRate_DeviceException)
{
    ON_CALL(*p_videoDeviceMock, getCurrentDisframerate(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](char* param) {
                throw device::Exception("Test exception");
                string framerate("3840x2160px48");
                ::memcpy(param, framerate.c_str(), framerate.length());
                return 0;
            }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, getDisplayFrameRate_DeviceError)
{
    ON_CALL(*p_videoDeviceMock, getCurrentDisframerate(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](char* param) {
                param[0] = '\0';
                return 1;
            }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, getDisplayFrameRate_EmptyFramerate)
{
    ON_CALL(*p_videoDeviceMock, getCurrentDisframerate(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](char* param) {
                param[0] = '\0';
                return 0;
            }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
    EXPECT_EQ(response, "");
}
