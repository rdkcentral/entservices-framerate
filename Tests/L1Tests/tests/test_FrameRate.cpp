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
#include <cstdio>
#include <unistd.h>
#include <sys/syscall.h>

#include "FrameRate.h"

#include "FactoriesImplementation.h"
#include "ServiceMock.h"
#include "ThunderPortability.h"
#include "FrameRateImplementation.h"
#include "FrameRateMock.h"
#include "WorkerPoolImplementation.h"
#include "WrapsMock.h"

#include "DeviceSettingsMock.h"
#include "DeviceSettingsVideoDeviceMock.h"

using namespace WPEFramework;

using ::testing::NiceMock;

#define TESTSYNC_LOG(fmt, ...) do { fprintf(stderr, "[TestSync] [%d] " fmt "\n", (int)syscall(SYS_gettid), ##__VA_ARGS__); fflush(stderr); } while (0)

// Wraps FrameRateImplementation to observe DSHelper's OnDeviceSettingsActivated()/
// OnDeviceSettingsDeactivated() hooks directly, independent of whatever (if anything)
// those methods do internally — Activated() runs via the WorkerPool's async job, so
// tests must wait for it rather than assume it has run by the time Initialize() returns.
class TestableFrameRateImplementation : public Plugin::FrameRateImplementation {
public:
    void OnDeviceSettingsActivated() override
    {
        Plugin::FrameRateImplementation::OnDeviceSettingsActivated();
        TESTSYNC_LOG("OnDeviceSettingsActivated: acquiring lock to signal activation");
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _activated = true;
        }
        TESTSYNC_LOG("OnDeviceSettingsActivated: released lock, notifying waiters");
        _cv.notify_all();
    }

    void OnDeviceSettingsDeactivated() override
    {
        Plugin::FrameRateImplementation::OnDeviceSettingsDeactivated();
        TESTSYNC_LOG("OnDeviceSettingsDeactivated: acquiring lock to signal deactivation");
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _activated = false;
            _deactivated = true;
        }
        TESTSYNC_LOG("OnDeviceSettingsDeactivated: released lock, notifying waiters");
        _cv.notify_all();
    }

    bool WaitForActivated(const std::chrono::milliseconds timeout)
    {
        TESTSYNC_LOG("WaitForActivated: acquiring lock");
        std::unique_lock<std::mutex> lock(_mutex);
        const bool signalled = _cv.wait_for(lock, timeout, [&] { return _activated; });
        TESTSYNC_LOG("WaitForActivated: releasing lock, signalled=%d", signalled);
        return signalled;
    }

    bool WaitForDeactivated(const std::chrono::milliseconds timeout)
    {
        TESTSYNC_LOG("WaitForDeactivated: acquiring lock");
        std::unique_lock<std::mutex> lock(_mutex);
        const bool signalled = _cv.wait_for(lock, timeout, [&] { return _deactivated; });
        TESTSYNC_LOG("WaitForDeactivated: releasing lock, signalled=%d", signalled);
        return signalled;
    }

private:
    std::mutex _mutex;
    std::condition_variable _cv;
    bool _activated = false;
    bool _deactivated = false;
};

// Shared fixture plumbing. `hasVideoDevice` controls whether the mocked
// DeviceSettings config reports a video device (so DSHelper caches a valid
// handle) or none (so DSHelper stays ERROR_UNAVAILABLE) — set once per test
// fixture, before Initialize(), since DSHelper loads config lazily on first
// use and caches it for the DeviceSettings connection's lifetime.
class FrameRateTestBase : public ::testing::Test {
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
    TestableFrameRateImplementation* testableImpl = nullptr;

    explicit FrameRateTestBase(bool hasVideoDevice)
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

        ON_CALL(DeviceSettingsMock::Mock(), GetDeviceSettingConfigs(::testing::_))
            .WillByDefault(::testing::Invoke(
                [hasVideoDevice](Exchange::IDeviceSettings::DeviceSettingConfigs& configs) {
                    if (hasVideoDevice) {
                        Exchange::IDeviceSettings::VideoDeviceConfigInfo vdConfig{};
                        vdConfig.numSupportedDFCs = 1;
                        vdConfig.supportedDFCsMask = 1;
                        vdConfig.defaultDFC = 0;
                        configs.videoConfigs.push_back(vdConfig);
                    }
                    return Core::ERROR_NONE;
                }));
        ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), GetVideoDeviceHandle(0, ::testing::_))
            .WillByDefault(::testing::DoAll(
                ::testing::SetArgReferee<1>(0),
                ::testing::Return(Core::ERROR_NONE)));

        // DSHelper::Open() registers as an IPlugin::INotification observer for the
        // "org.rdk.DeviceSettings" callsign (RPC::PluginSmartInterfaceType); it does NOT
        // call QueryInterfaceByCallsign. Simulate DeviceSettings already being active by
        // invoking Activated() synchronously from within Register(), then hand back the
        // DeviceSettingsMock root when the framework QueryInterface()s the "plugin".
        ON_CALL(service, Register(::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](PluginHost::IPlugin::INotification* sink) {
                    sink->Activated("org.rdk.DeviceSettings", &service);
                }));

        // IShell::Root<Exchange::IFrameRate>() resolves ICOMLink via service->QueryInterface<ICOMLink>()
        // (NOT via a separate COMLink() accessor), and DSHelper's AcquireSubInterface resolves the
        // DeviceSettings root the same way. Both interface IDs are handled by this single mock —
        // returning the wrong object for an unrequested ID would be undefined behaviour (wrong vtable).
        ON_CALL(service, QueryInterface(::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](const uint32_t id) -> void* {
                    if (id == static_cast<uint32_t>(PluginHost::IShell::ICOMLink::ID)) {
                        return static_cast<PluginHost::IShell::ICOMLink*>(&comLinkMock);
                    }
                    if (id == static_cast<uint32_t>(Exchange::IDeviceSettings::ID)) {
                        auto* root = DeviceSettingsMock::Get();
                        root->AddRef();
                        return static_cast<void*>(static_cast<Exchange::IDeviceSettings*>(root));
                    }
                    return nullptr;
                }));

        ON_CALL(service, QueryInterfaceByCallsign(::testing::_, ::testing::_))
            .WillByDefault(::testing::Invoke(
                [&](const uint32_t, const string&) -> void* {
                    auto* root = DeviceSettingsMock::Get();
                    root->AddRef();
                    return static_cast<Exchange::IDeviceSettings*>(root);
                }));

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
                        auto testable = Core::ProxyType<TestableFrameRateImplementation>::Create();
                        testableImpl = &(*testable);
                        FrameRateImplem = testable;
                        return &FrameRateImplem;
                    }));
#else
	ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
	    .WillByDefault(::testing::Return(FrameRateImplem));
#endif

        Core::IWorkerPool::Assign(&(*workerPool));
            workerPool->Run();

        plugin->Initialize(&service);

        // Give the async DSHelper activation job (dispatched via the real WorkerPool) a
        // bounded chance to run OnDeviceSettingsActivated() before the test body executes;
        // harmless if DeviceSettings never activates.
        if (testableImpl != nullptr) {
            const bool activated = testableImpl->WaitForActivated(std::chrono::milliseconds(2000));
            TESTSYNC_LOG("FrameRateTestBase ctor: WaitForActivated returned %d", activated);
        }
    }
    virtual ~FrameRateTestBase()
    {
        plugin->Deinitialize(&service);

        // Close()/Deactivated() run synchronously, so this should already be signaled by
        // the time Deinitialize() returns; kept for symmetry with the activation-side wait.
        if (testableImpl != nullptr) {
            const bool deactivated = testableImpl->WaitForDeactivated(std::chrono::milliseconds(2000));
            TESTSYNC_LOG("FrameRateTestBase dtor: WaitForDeactivated returned %d", deactivated);
        }

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

        dispatcher->Deactivate();
        dispatcher->Release();

        PluginHost::IFactories::Assign(nullptr);

        DeviceSettingsMock::Delete();
    }
};

// Default fixture: one video device present at handle 0.
class FrameRateTest : public FrameRateTestBase {
protected:
    FrameRateTest() : FrameRateTestBase(true) {}
};

// Fixture for the "DeviceSettings has no video device" scenarios.
class FrameRateNoDeviceTest : public FrameRateTestBase {
protected:
    FrameRateNoDeviceTest() : FrameRateTestBase(false) {}
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
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), GetCurrentDisplayFrameRate(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(string("1920x1080x60")),
            ::testing::Return(Core::ERROR_NONE)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
    EXPECT_TRUE(response.find("\"framerate\":\"1920x1080x60\"") != string::npos);
}

TEST_F(FrameRateNoDeviceTest, GetDisplayFrameRate_NoVideoDevices)
{
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
}

TEST_F(FrameRateTest, GetDisplayFrameRate_DeviceError)
{
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), GetCurrentDisplayFrameRate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
}

TEST_F(FrameRateTest, GetFrmMode_Success)
{
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), GetFRFMode(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(1),
            ::testing::Return(Core::ERROR_NONE)));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getFrmMode"), _T("{}"), response));
    EXPECT_TRUE(response.find("\"success\":true") != string::npos);
    EXPECT_TRUE(response.find("\"auto-frm-mode\":1") != string::npos);
}

TEST_F(FrameRateNoDeviceTest, GetFrmMode_NoVideoDevices)
{
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("getFrmMode"), _T("{}"), response));
}

TEST_F(FrameRateTest, GetFrmMode_DeviceError)
{
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), GetFRFMode(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

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
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), SetDisplayFrameRate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_NONE));

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

TEST_F(FrameRateNoDeviceTest, SetDisplayFrameRate_NoVideoDevices)
{
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"1920x1080x60\"}"), response));
}

TEST_F(FrameRateTest, SetDisplayFrameRate_DeviceError)
{
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), SetDisplayFrameRate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"1920x1080x60\"}"), response));
}

TEST_F(FrameRateTest, SetFrmMode_Success_ModeZero)
{
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), SetFRFMode(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":0}"), response));
    EXPECT_TRUE(response.find("true") != string::npos);
}

TEST_F(FrameRateTest, SetFrmMode_Success_ModeOne)
{
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), SetFRFMode(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_NONE));

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

TEST_F(FrameRateNoDeviceTest, SetFrmMode_NoVideoDevices)
{
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":1}"), response));
}

TEST_F(FrameRateTest, SetFrmMode_DeviceError)
{
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), SetFRFMode(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

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

TEST_F(FrameRateNoDeviceTest, setFrmMode_NoVideoDevices)
{
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":0}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setFrmMode_DeviceError)
{
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), SetFRFMode(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](int32_t, int32_t param) {
                EXPECT_EQ(param, 0);
                return Core::ERROR_GENERAL;
            }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("setFrmMode"), _T("{\"frmmode\":0}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateNoDeviceTest, getFrmMode_NoVideoDevices)
{
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("getFrmMode"), _T("{}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, getFrmMode_DeviceError)
{
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), GetFRFMode(::testing::_, ::testing::_))
        .WillByDefault(::testing::DoAll(
            ::testing::SetArgReferee<1>(0),
            ::testing::Return(Core::ERROR_GENERAL)));

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

TEST_F(FrameRateNoDeviceTest, setDisplayFrameRate_NoVideoDevices)
{
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"3840x2160px48\"}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, setDisplayFrameRate_DeviceError)
{
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), SetDisplayFrameRate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](int32_t, const string& param) {
                EXPECT_EQ(param, string("3840x2160px48"));
                return Core::ERROR_GENERAL;
            }));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("setDisplayFrameRate"), _T("{\"framerate\":\"3840x2160px48\"}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateNoDeviceTest, getDisplayFrameRate_NoVideoDevices)
{
    EXPECT_EQ(Core::ERROR_UNAVAILABLE, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
    EXPECT_EQ(response, "");
}

TEST_F(FrameRateTest, getDisplayFrameRate_DeviceError)
{
    ON_CALL(DeviceSettingsVideoDeviceMock::Mock(), GetCurrentDisplayFrameRate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("getDisplayFrameRate"), _T("{}"), response));
    EXPECT_EQ(response, "");
}
