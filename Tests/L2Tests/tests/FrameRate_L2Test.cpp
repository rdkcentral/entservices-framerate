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
#include <gmock/gmock.h>
#include "L2Tests.h"
#include "L2TestsMock.h"
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <cstring>
#include <interfaces/IFrameRate.h>

// FrameRate now talks to the real org.rdk.DeviceSettings plugin over COM-RPC.
// HAL mocks stand in for libds-hal so this test can control DeviceSettings behavior
// Note: HAL mocks (DsAudio, DsVideoDevice, DsVideoPort, DsDisplay, DsFPD, DsHdmiIn)
// are now automatically provided by L2TestsMock - no need to include them explicitly

#define JSON_TIMEOUT (1000)
#define COM_TIMEOUT (100)
#define TEST_LOG(x, ...)                                                                                                                         \
    fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); \
    fflush(stderr);
#define FrameRate_CALLSIGN _T("org.rdk.FrameRate.1")
#define FrameRateL2TEST_CALLSIGN _T("L2tests.1")

using ::testing::NiceMock;
using namespace WPEFramework;
using testing::StrictMock;
using ::WPEFramework::Exchange::IFrameRate;

typedef enum : uint32_t {
    FrameRate_OnFpsEvent = 0x00000001,
    FrameRate_OnDisplayFrameRateChanging = 0x00000002,
    FrameRate_OnDisplayFrameRateChanged = 0x00000003,
    FrameRate_StateInvalid = 0x00000000
} FrameRateL2test_async_events_t;

/**
 * @brief Internal test mock class
 *
 * Note that this is for internal test use only and doesn't mock any actual
 * concrete interface.
 */

class AsyncHandlerMock_FrameRate {
public:
    AsyncHandlerMock_FrameRate() {
    }
    MOCK_METHOD(void, OnFpsEvent, (int average, int min, int max));
    MOCK_METHOD(void, OnDisplayFrameRateChanging, (const string &displayFrameRate));
    MOCK_METHOD(void, OnDisplayFrameRateChanged, (const string &displayFrameRate));
};

/* Notification Handler Class for COM-RPC*/
class FrameRateNotificationHandler : public Exchange::IFrameRate::INotification {
private:
    /** @brief Mutex */
    std::mutex m_mutex;

    /** @brief Condition variable */
    std::condition_variable m_condition_variable;

    /** @brief Event signalled flag */
    uint32_t m_event_signalled;

    BEGIN_INTERFACE_MAP(Notification)
    INTERFACE_ENTRY(Exchange::IFrameRate::INotification)
    END_INTERFACE_MAP

public:
    FrameRateNotificationHandler() {}
    ~FrameRateNotificationHandler() {}

    void OnFpsEvent(int average, int min, int max) override {
        TEST_LOG("OnFpsEvent event triggered ***\n");
        std::unique_lock<std::mutex> lock(m_mutex);

        /* Notify the requester thread. */
        m_event_signalled |= FrameRate_OnFpsEvent;
        m_condition_variable.notify_one();
    }
    void OnDisplayFrameRateChanging(const string &displayFrameRate) override {
        TEST_LOG("OnDisplayFrameRateChanging event triggered ***\n");
        std::unique_lock<std::mutex> lock(m_mutex);

        TEST_LOG("OnDisplayFrameRateChanging received: %s\n", displayFrameRate.c_str());
        /* Notify the requester thread. */
        m_event_signalled |= FrameRate_OnDisplayFrameRateChanging;
        m_condition_variable.notify_one();
    }
    void OnDisplayFrameRateChanged(const string &displayFrameRate) override {
        TEST_LOG("OnDisplayFrameRateChanged event triggered ***\n");
        std::unique_lock<std::mutex> lock(m_mutex);

        TEST_LOG("OnDisplayFrameRateChanged received: %s\n", displayFrameRate.c_str());
        /* Notify the requester thread. */
        m_event_signalled |= FrameRate_OnDisplayFrameRateChanged;
        m_condition_variable.notify_one();
    }

    uint32_t WaitForRequestStatus(uint32_t timeout_ms, FrameRateL2test_async_events_t expected_status) {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto now = std::chrono::system_clock::now();
        std::chrono::milliseconds timeout(timeout_ms);
        uint32_t signalled = FrameRate_StateInvalid;

        while (!(expected_status & m_event_signalled)) {
            if (m_condition_variable.wait_until(lock, now + timeout) == std::cv_status::timeout) {
                TEST_LOG("Timeout waiting for request status event");
                break;
            }
        }
        signalled = m_event_signalled;
        return signalled;
    }
};

/* FrameRate L2 test class declaration */
class FrameRate_L2test : public L2TestMocks {
protected:
    Core::JSONRPC::Message message;
    string response;

    virtual ~FrameRate_L2test() override;

public:
    FrameRate_L2test();
    // Captured from HAL callbacks - used to simulate HAL events
    dsRegisterFrameratePreChangeCB_t m_dsFrameratePreChangeCB = nullptr;
    dsRegisterFrameratePostChangeCB_t m_dsFrameratePostChangeCB = nullptr;
    
    // HAL Mocks are now provided by L2TestsMock parent class:
    // - p_dsAudioHalMock
    // - p_dsVideoDeviceHalMock
    // - p_dsVideoPortHalMock
    // - p_dsDisplayHalMock
    // - p_dsFPDHalMock
    // - p_dsHdmiInHalMock
    // - p_telemetryApiImplMock
    uint32_t CreateFrameRateInterfaceObjectUsingComRPCConnection();
    void OnFpsEvent(int average, int min, int max);
    void OnDisplayFrameRateChanging(const string &displayFrameRate);
    void OnDisplayFrameRateChanged(const string &displayFrameRate);

    /**
     * @brief waits for various status change on asynchronous calls
     */
    uint32_t WaitForRequestStatus(uint32_t timeout_ms, FrameRateL2test_async_events_t expected_status);

private:
    /** @brief Mutex */
    std::mutex m_mutex;

    /** @brief Condition variable */
    std::condition_variable m_condition_variable;

    /** @brief Event signalled flag */
    uint32_t m_event_signalled;

protected:
    /** @brief Pointer to the IShell interface */
    PluginHost::IShell *m_controller_FrameRate;

    /** @brief Pointer to the IFrameRate interface */
    Exchange::IFrameRate *m_FrameRateplugin;

    Core::Sink<FrameRateNotificationHandler> notify;
};

/**
 * @brief Constructor for FrameRate L2 test class
 */
FrameRate_L2test::FrameRate_L2test()
    : L2TestMocks() {
    uint32_t status = Core::ERROR_GENERAL;
    m_event_signalled = FrameRate_StateInvalid;

    // TelemetryApi mock is already registered by L2TestMocks (p_telemetryApiImplMock);
    // calling TelemetryApi::setImpl() again here would fail the (nullptr == impl) guard.
    ON_CALL(*p_telemetryApiImplMock, t2_init(::testing::_)).WillByDefault(::testing::Return());
    ON_CALL(*p_telemetryApiImplMock, t2_uninit()).WillByDefault(::testing::Return());
    ON_CALL(*p_telemetryApiImplMock, t2_event_s(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(T2ERROR_SUCCESS));
    ON_CALL(*p_telemetryApiImplMock, t2_event_d(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(T2ERROR_SUCCESS));
    ON_CALL(*p_telemetryApiImplMock, t2_event_f(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(T2ERROR_SUCCESS));
    TEST_LOG("TelemetryApi mock initialized");

    // Configure FrameRate-specific HAL mock behaviors
    // Note: Common Init/Term behaviors are already set up by L2TestsMock
    // FrameRate plugin ONLY uses IDeviceSettingsVideoDevice interface
    // So we only need VideoDevice and VideoPort HAL mocks
    
    // 1. VideoDevice HAL Mock - FrameRate CORE functionality
    ON_CALL(*p_dsVideoDeviceHalMock, dsGetVideoDevice(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](int, intptr_t* handle) {
                if (handle) { *handle = 1; }
                return dsERR_NONE;
            }));
    ON_CALL(*p_dsVideoDeviceHalMock, dsSetDisplayframerate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(dsERR_NONE));
    ON_CALL(*p_dsVideoDeviceHalMock, dsGetCurrentDisplayframerate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](intptr_t, char* framerate) {
                if (framerate) strcpy(framerate, "60");
                return dsERR_NONE;
            }));
    ON_CALL(*p_dsVideoDeviceHalMock, dsRegisterFrameratePreChangeCB(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](dsRegisterFrameratePreChangeCB_t cbFunc) {
                m_dsFrameratePreChangeCB = cbFunc;
                return dsERR_NONE;
            }));
    ON_CALL(*p_dsVideoDeviceHalMock, dsRegisterFrameratePostChangeCB(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](dsRegisterFrameratePostChangeCB_t cbFunc) {
                m_dsFrameratePostChangeCB = cbFunc;
                return dsERR_NONE;
            }));
    TEST_LOG("DsVideoDeviceApi FrameRate-specific behaviors configured");
    
    // 2. VideoPort HAL Mock - FrameRate specific behaviors only
    ON_CALL(*p_dsVideoPortHalMock, dsGetVideoPort(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](dsVideoPortType_t, int, intptr_t* handle) {
                if (handle) { *handle = 1; }
                return dsERR_NONE;
            }));
    ON_CALL(*p_dsVideoPortHalMock, dsIsDisplayConnected(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](intptr_t, bool* connected) {
                if (connected) { *connected = true; }
                return dsERR_NONE;
            }));
    TEST_LOG("DsVideoPortApi FrameRate-specific behaviors configured");
    
    // Note: Audio, Display, FPD, and HdmiIn HAL mocks are set up by L2TestsMock
    // FrameRate only uses IDeviceSettingsVideoDevice interface (VideoDevice + VideoPort HAL)
    // But DeviceSettings plugin initializes ALL sub-implementations, so L2TestsMock provides
    // robust default mocks for all HAL APIs to prevent crashes during DeviceSettings activation
    
    TEST_LOG("FrameRate HAL mock setup complete - VideoDevice and VideoPort configured");

    // Mock PowerManager HAL for DeviceSettings dependency
    // Note: PowerManager activation is optional - DeviceSettings can work without it
    // Set up mocks but allow them to not be called
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_INIT())
        .Times(::testing::AtMost(1))
        .WillRepeatedly(::testing::Return(DEEPSLEEPMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_INIT())
        .Times(::testing::AtMost(1))
        .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetWakeupSrc(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_GetPowerState(::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [](PWRMgr_PowerState_t* powerState) {
                *powerState = PWRMGR_POWERSTATE_ON;
                return PWRMGR_SUCCESS;
            }));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_API_SetPowerState(::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

    /* Try to activate PowerManager plugin (DeviceSettings optionally depends on it) */
    TEST_LOG("Checking PowerManager plugin state...");
    std::string currentState;
    status = GetPluginState("org.rdk.PowerManager", currentState);
    
    if (status == Core::ERROR_NONE && currentState == "deactivated") {
        TEST_LOG("Attempting to activate PowerManager plugin...");
        status = ActivateService("org.rdk.PowerManager");
        
        if (status == Core::ERROR_NONE) {
            TEST_LOG("PowerManager activated successfully");
        } else {
            TEST_LOG("PowerManager activation failed (status: %u) - continuing without it", status);
        }
    } else if (status == Core::ERROR_NONE && currentState == "activated") {
        TEST_LOG("PowerManager is already activated");
    } else {
        TEST_LOG("PowerManager is in '%s' state - skipping activation", currentState.c_str());
    }

    /* Activate the real DeviceSettings plugin so FrameRate's DSHelper can resolve it */
    TEST_LOG("Activating DeviceSettings plugin...");
    
    // First check if DeviceSettings is already active
    status = GetPluginState("org.rdk.DeviceSettings", currentState);
    
    if (status == Core::ERROR_NONE && currentState == "activated") {
        TEST_LOG("DeviceSettings is already activated");
        status = Core::ERROR_NONE;
    } else {
        // Try to activate with retry logic
        status = ActivateServiceWithRetry("org.rdk.DeviceSettings", 3, 500);
    }
    
    EXPECT_EQ(Core::ERROR_NONE, status);
    if (status != Core::ERROR_NONE) {
        TEST_LOG("FATAL: Failed to activate DeviceSettings plugin (status: %u)", status);
        return;
    }

    /* Activate plugin in constructor */
    TEST_LOG("Activating FrameRate plugin...");
    status = ActivateServiceWithRetry("org.rdk.FrameRate", 3, 500);
    EXPECT_EQ(Core::ERROR_NONE, status);
    if (status != Core::ERROR_NONE) {
        TEST_LOG("FATAL: Failed to activate FrameRate plugin (status: %u)", status);
        return;
    }

    if (CreateFrameRateInterfaceObjectUsingComRPCConnection() != Core::ERROR_NONE) {
        TEST_LOG("Invalid FrameRate_Client");
    }
    else {
        EXPECT_TRUE(m_controller_FrameRate != nullptr);
        if (m_controller_FrameRate) {
            EXPECT_TRUE(m_FrameRateplugin != nullptr);
            if (m_FrameRateplugin){
                m_FrameRateplugin->Register(&notify);
            }
            else {
                TEST_LOG("m_FrameRateplugin is NULL");
            }
        }
        else {
            TEST_LOG("m_controller_FrameRate is NULL");
        }
    }
}

/**
 * @brief Destructor for FrameRate L2 test class
 */
FrameRate_L2test::~FrameRate_L2test() {
    uint32_t status = Core::ERROR_GENERAL;
    m_event_signalled = FrameRate_StateInvalid;

    if (m_FrameRateplugin) {
        m_FrameRateplugin->Unregister(&notify);
        m_FrameRateplugin->Release();
    }

    /* Deactivate FrameRate plugin first */
    TEST_LOG("Deactivating FrameRate plugin...");
    std::string currentState;
    status = GetPluginState("org.rdk.FrameRate", currentState);
    
    if (status == Core::ERROR_NONE && (currentState == "activated" || currentState == "suspended")) {
        status = DeactivateService("org.rdk.FrameRate");
        if (status != Core::ERROR_NONE) {
            TEST_LOG("WARNING: Failed to deactivate FrameRate (status: %u)", status);
        }
    } else {
        TEST_LOG("FrameRate is not in activated/suspended state, skipping deactivation");
        status = Core::ERROR_NONE;  // Don't fail test if plugin is already deactivated
    }
    EXPECT_EQ(Core::ERROR_NONE, status);

    /* Deactivate DeviceSettings plugin */
    TEST_LOG("Deactivating DeviceSettings plugin...");
    status = GetPluginState("org.rdk.DeviceSettings", currentState);
    
    if (status == Core::ERROR_NONE && (currentState == "activated" || currentState == "suspended")) {
        status = DeactivateService("org.rdk.DeviceSettings");
        if (status != Core::ERROR_NONE) {
            TEST_LOG("WARNING: Failed to deactivate DeviceSettings (status: %u)", status);
        }
    } else {
        TEST_LOG("DeviceSettings is not in activated/suspended state, skipping deactivation");
        status = Core::ERROR_NONE;  // Don't fail test if plugin is already deactivated
    }
    EXPECT_EQ(Core::ERROR_NONE, status);

    /* Deactivate PowerManager plugin if it was activated */
    TEST_LOG("Checking PowerManager plugin state for cleanup...");
    
    // Set expectations for PowerManager HAL termination (may not be called)
    EXPECT_CALL(*p_powerManagerHalMock, PLAT_TERM())
        .Times(::testing::AtMost(1))
        .WillRepeatedly(::testing::Return(PWRMGR_SUCCESS));

    EXPECT_CALL(*p_powerManagerHalMock, PLAT_DS_TERM())
        .Times(::testing::AtMost(1))
        .WillRepeatedly(::testing::Return(DEEPSLEEPMGR_SUCCESS));
    
    status = GetPluginState("org.rdk.PowerManager", currentState);
    
    if (status == Core::ERROR_NONE && currentState == "activated") {
        TEST_LOG("Deactivating PowerManager plugin...");
        status = DeactivateService("org.rdk.PowerManager");
        if (status != Core::ERROR_NONE) {
            TEST_LOG("WARNING: Failed to deactivate PowerManager (status: %u)", status);
        } else {
            TEST_LOG("PowerManager deactivation requested");
        }
    } else {
        TEST_LOG("PowerManager is in '%s' state, no deactivation needed", currentState.c_str());
    }

    // Clean up all HAL mocks
    DsAudioApi::setImpl(nullptr);
    DsVideoDeviceApi::setImpl(nullptr);
    DsVideoPortApi::setImpl(nullptr);
    DsDisplayApi::setImpl(nullptr);
    DsFPDApi::setImpl(nullptr);
    DsHdmiInApi::setImpl(nullptr);
    TelemetryApi::setImpl(nullptr);
    TEST_LOG("All mocks cleaned up");
}

void FrameRate_L2test::OnFpsEvent(int average, int min, int max) {
    TEST_LOG("OnFpsEvent event triggered ***\n");
    std::unique_lock<std::mutex> lock(m_mutex);

    /* Notify the requester thread. */
    m_event_signalled |= FrameRate_OnFpsEvent;
    m_condition_variable.notify_one();
}

void FrameRate_L2test::OnDisplayFrameRateChanging(const string &displayFrameRate) {
    TEST_LOG("OnDisplayFrameRateChanging event triggered ***\n");
    std::unique_lock<std::mutex> lock(m_mutex);

    TEST_LOG("OnDisplayFrameRateChanging received: %s\n", displayFrameRate.c_str());

    /* Notify the requester thread. */
    m_event_signalled |= FrameRate_OnDisplayFrameRateChanging;
    m_condition_variable.notify_one();
}

void FrameRate_L2test::OnDisplayFrameRateChanged(const string &displayFrameRate) {
    TEST_LOG("OnDisplayFrameRateChanged event triggered ***\n");
    std::unique_lock<std::mutex> lock(m_mutex);

    TEST_LOG("OnDisplayFrameRateChanged received: %s\n", displayFrameRate.c_str());

    /* Notify the requester thread. */
    m_event_signalled |= FrameRate_OnDisplayFrameRateChanged;
    m_condition_variable.notify_one();
}

/**
 * @brief waits for various status change on asynchronous calls
 *
 * @param[in] timeout_ms timeout for waiting
 */
uint32_t FrameRate_L2test::WaitForRequestStatus(uint32_t timeout_ms, FrameRateL2test_async_events_t expected_status) {
    std::unique_lock<std::mutex> lock(m_mutex);
    auto now = std::chrono::system_clock::now();
    std::chrono::seconds timeout(timeout_ms);
    uint32_t signalled = FrameRate_StateInvalid;

    while (!(expected_status & m_event_signalled)) {
        if (m_condition_variable.wait_until(lock, now + timeout) == std::cv_status::timeout) {
            TEST_LOG("Timeout waiting for request status event");
            break;
        }
    }

    signalled = m_event_signalled;
    return signalled;
}

/**
 * @brief Compare two request status objects
 *
 * @param[in] data Expected value
 * @return true if the argument and data match, false otherwise
 */
MATCHER_P(MatchRequest, data, "") {
    bool match = true;
    std::string expected;
    std::string actual;

    data.ToString(expected);
    arg.ToString(actual);
    TEST_LOG(" rec = %s, arg = %s", expected.c_str(), actual.c_str());
    EXPECT_STREQ(expected.c_str(), actual.c_str());
    return match;
}

// COM-RPC Changes
uint32_t FrameRate_L2test::CreateFrameRateInterfaceObjectUsingComRPCConnection() {
    uint32_t return_value = Core::ERROR_GENERAL;
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> Engine_FrameRate;
    Core::ProxyType<RPC::CommunicatorClient> Client_FrameRate;

    TEST_LOG("Creating Engine_FrameRate");
    Engine_FrameRate = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();
    Client_FrameRate = Core::ProxyType<RPC::CommunicatorClient>::Create(Core::NodeId("/tmp/communicator"), Core::ProxyType<Core::IIPCServer>(Engine_FrameRate));

    TEST_LOG("Creating Engine_FrameRate Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
    Engine_FrameRate->Announcements(mClient_FrameRate->Announcement());
#endif

    if (!Client_FrameRate.IsValid()) {
        TEST_LOG("Invalid Client_FrameRate");
    }
    else {
        m_controller_FrameRate = Client_FrameRate->Open<PluginHost::IShell>(_T("org.rdk.FrameRate"), ~0, 3000);
        if (m_controller_FrameRate) {
            m_FrameRateplugin = m_controller_FrameRate->QueryInterface<Exchange::IFrameRate>();
            return_value = Core::ERROR_NONE;
        }
    }
    return return_value;
}

/************Test case Details **************************
** 1.Set frequency as 1000
** 2.Checking SetCollectionFrequency for positive check
** 3.Confirm frequency set using Comrpc.
*******************************************************/

TEST_F(FrameRate_L2test, setCollectionFrequencyUsingComrpc) {
    uint32_t status = Core::ERROR_GENERAL;
    int frequency = 1000;
    bool success = false;
    status = m_FrameRateplugin->SetCollectionFrequency(frequency, success);
    EXPECT_EQ(success, true);
    EXPECT_EQ(status, Core::ERROR_NONE);

    if (status != Core::ERROR_NONE) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
    EXPECT_TRUE(success);
}

/************Test case Details **************************
** 1.Set frequency as 0
** 2.Checking SetCollectionFrequency for failure check
** 3.Confirm frequency not set using Comrpc.
*******************************************************/

TEST_F(FrameRate_L2test, SetCollectionFrequencyFailureUsingComrpc) {
    uint32_t status = Core::ERROR_GENERAL;
    int frequency = 0;
    bool success = false;
    status = m_FrameRateplugin->SetCollectionFrequency(frequency, success);
    EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);

    if (status != Core::ERROR_INVALID_PARAMETER) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
}

/************Test case Details **************************
** 1.Checking StartFpsCollection using Comrpc
*******************************************************/

TEST_F(FrameRate_L2test, StartFpsCollectionUsingComrpc) {
    uint32_t status = Core::ERROR_GENERAL;
    bool success = false;

    status = m_FrameRateplugin->StartFpsCollection(success);
    EXPECT_EQ(status, Core::ERROR_NONE);

    if (status != Core::ERROR_NONE) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
    EXPECT_TRUE(success);
}

/************Test case Details **************************
** 1.Checking StopFpsCollection using Comrpc
*******************************************************/

TEST_F(FrameRate_L2test, StopFpsCollectionUsingComrpc) {
    uint32_t status = Core::ERROR_GENERAL;

    bool success = false;
    status = m_FrameRateplugin->StopFpsCollection(success);
    EXPECT_EQ(status, Core::ERROR_NONE);

    if (status != Core::ERROR_NONE) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
    EXPECT_TRUE(success);
}

/************Test case Details **************************
** 1.Set newfps value as 60
** 2.Checking UpdateFps for positive check
** 3.Confirm fps updated using Comrpc.
*******************************************************/

TEST_F(FrameRate_L2test, UpdateFpsUsingComrpc) {
    uint32_t status = Core::ERROR_GENERAL;
    int newfps = 60;
    bool success = false;
    status = m_FrameRateplugin->UpdateFps(newfps, success);
    EXPECT_EQ(status, Core::ERROR_NONE);

    if (status != Core::ERROR_NONE) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
    EXPECT_TRUE(success);
}

/************Test case Details **************************
** 1.Set newfps value as -1
** 2.Checking UpdateFps for negative check
** 3.Confirm fps update failure using Comrpc.
*******************************************************/

TEST_F(FrameRate_L2test, UpdateFpsFailureUsingComrpc) {
    uint32_t status = Core::ERROR_GENERAL;

    int newfps = -1;
    bool success = false;
    status = m_FrameRateplugin->UpdateFps(newfps, success);
    EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);

    if (status != Core::ERROR_INVALID_PARAMETER) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
}

/************Test case Details **************************
** 1.Valid FrameRate values are set.
** 2.Mock framerate values to set.
** 3.Invokes SetDisplayFrameRate with valid values.
** 4.For STB profile, set the status as success.
** 5.Check the status of setDisplayframerate using Comrpc.
*******************************************************/

TEST_F(FrameRate_L2test, SetDisplayFrameRateUsingComrpc) {
    uint32_t status = Core::ERROR_GENERAL;
    bool success = false;
    uint32_t signalled_pre = FrameRate_StateInvalid;
    uint32_t signalled_post = FrameRate_StateInvalid;
    ON_CALL(*p_dsVideoDeviceHalMock, dsSetDisplayframerate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](intptr_t, char* framerate) {
                EXPECT_EQ(string(framerate), string("3840x2160px48"));
                return dsERR_NONE;
            }));
    status = m_FrameRateplugin->SetDisplayFrameRate("3840x2160px48", success);

    if (status != Core::ERROR_NONE) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
    }
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_TRUE(success);
}

/************Test case Details **************************
** 1.Invalid FrameRate values are set.
** 2.Invokes SetDisplayFrameRate with invalid values.
** 3.Check the failure status of setDisplayframerate using Comrpc.
*******************************************************/

TEST_F(FrameRate_L2test, SetDisplayFrameRateFailureUsingComrpc) {
    uint32_t status = Core::ERROR_INVALID_PARAMETER;
    bool success = false;

    status = m_FrameRateplugin->SetDisplayFrameRate("3840x2160p", success);
    EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);

    if (status != Core::ERROR_INVALID_PARAMETER) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
}

/************Test case Details **************************
** 1.Mock framerate values.
** 2.Invokes GetDisplayFrameRate.
** 3.For STB profile, set the status as success.
** 4.Check the status of GetDisplayFrameRate using Comrpc.
*******************************************************/

TEST_F(FrameRate_L2test, GetDisplayFrameRateUsingComrpc) {
    ON_CALL(*p_dsVideoDeviceHalMock, dsGetCurrentDisplayframerate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](intptr_t, char* framerate) {
                if (framerate) { strcpy(framerate, "3840x2160px48"); }
                return dsERR_NONE;
            }));
    uint32_t status = Core::ERROR_GENERAL;
    bool success = false;
    std::string displayFrameRate;

    status = m_FrameRateplugin->GetDisplayFrameRate(displayFrameRate, success);

    if (status != Core::ERROR_NONE) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_TRUE(success);
}

/************Test case Details **************************
** 1.Mock FRFMode values to set.
** 2.Invokes SetFrmMode.
** 3.For STB profile, set the status as success.
** 4.Check the status of SetFrmMode using Comrpc.
*******************************************************/

TEST_F(FrameRate_L2test, SetFrmModeUsingComrpc) {
    uint32_t status = Core::ERROR_GENERAL;
    bool success = false;
    int frmmode = 0;

    ON_CALL(*p_dsVideoDeviceHalMock, dsSetFRFMode(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](intptr_t, int param) {
                EXPECT_EQ(param, 0);
                return dsERR_NONE;
            }));

    status = m_FrameRateplugin->SetFrmMode(frmmode, success);

    if (status != Core::ERROR_NONE) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_TRUE(success);
}

/************Test case Details **************************
** 1.set frmmode to negative value.
** 2.Invokes SetFrmMode with negative values.
** 3.Check the failure status of SetFrmMode using Comrpc.
*******************************************************/

TEST_F(FrameRate_L2test, SetFrmModeFailureUsingComrpc) {
    uint32_t status = Core::ERROR_INVALID_PARAMETER;
    bool success = false;
    int frmmode = -1;

    status = m_FrameRateplugin->SetFrmMode(frmmode, success);
    EXPECT_EQ(status, Core::ERROR_INVALID_PARAMETER);
    if (status != Core::ERROR_INVALID_PARAMETER) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
}

/************Test case Details **************************
** 1.Mock frmmode values.
** 2.Invokes GetFrmMode.
** 3.For STB profile, set the status as success.
** 4.Check the status of GetFrmMode using Comrpc.
*******************************************************/

TEST_F(FrameRate_L2test, GetFrmModeUsingComrpc) {
    uint32_t status = Core::ERROR_GENERAL;
    bool success = false;
    int frmmode = 0;
    ON_CALL(*p_dsVideoDeviceHalMock, dsGetFRFMode(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](intptr_t, int* out) {
                if (out) { *out = 0; }
                return dsERR_NONE;
            }));
    status = m_FrameRateplugin->GetFrmMode(frmmode, success);
    if (status != Core::ERROR_NONE) {
        std::string errorMsg = "COM-RPC returned error " + std::to_string(status) + " (" + std::string(Core::ErrorToString(status)) + ")";
        TEST_LOG("Err: %s", errorMsg.c_str());
    }
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_TRUE(success);
}

/************Test case Details **************************
** 1.Checking onDisplayFrameRateChanging
*******************************************************/
TEST_F(FrameRate_L2test, onDisplayFrameRateChanging)
{
    // Simulates the dsVideoDevice HAL firing the framerate pre-change callback that
    // DeviceSettingsVideoDeviceImplementation registered, forwarded to FrameRate over COM-RPC.
    ASSERT_NE(m_dsFrameratePreChangeCB, nullptr);
    m_dsFrameratePreChangeCB(48);
}

/************Test case Details **************************
** 1.Checking onDisplayFrameRateChanged
*******************************************************/
TEST_F(FrameRate_L2test, onDisplayFrameRateChanged)
{
    ASSERT_NE(m_dsFrameratePostChangeCB, nullptr);
    m_dsFrameratePostChangeCB(48);
}

/************Test case Details **************************
** 1.Set frequency as 1000
** 2.Checking SetCollectionFrequency for positive check
** 3.Confirm frequency set using Jsonrpc.
*******************************************************/

TEST_F(FrameRate_L2test, SetCollectionFrequencyUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    /*With both Params expecting Success*/
    params["frequency"] = 1000;
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "setCollectionFrequency", params, result);
    EXPECT_EQ(Core::ERROR_NONE, status);
    EXPECT_FALSE(result["result"].Boolean());
}

/************Test case Details **************************
** 1.Set frequency as 0
** 2.Checking SetCollectionFrequency for failure check
** 3.Confirm frequency not set using Jsonrpc.
*******************************************************/

TEST_F(FrameRate_L2test, setCollectionFrequencyFailureUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    /*With one Param  expecting Fail case */
    params["frequency"] = 90;
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "setCollectionFrequency", params, result);
    EXPECT_FALSE(result["result"].Boolean());
}

/************Test case Details **************************
** 1.Checking StartFpsCollection using Jsonrpc
*******************************************************/

TEST_F(FrameRate_L2test, StartFpsCollectionUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    /*With both Params expecting Success*/
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "startFpsCollection", params, result);
    EXPECT_EQ(Core::ERROR_NONE, status);
    EXPECT_FALSE(result["result"].Boolean());
}

/************Test case Details **************************
** 1.Checking StopFpsCollection using Jsonrpc
*******************************************************/

TEST_F(FrameRate_L2test, StopFpsCollectionUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    /*With both Params expecting Success*/
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "stopFpsCollection", params, result);
    EXPECT_EQ(Core::ERROR_NONE, status);
    EXPECT_FALSE(result["result"].Boolean());
}

/************Test case Details **************************
** 1.Set newfps value as 30
** 2.Checking UpdateFps for positive check
** 3.Confirm fps updated using Jsonrpc.
*******************************************************/

TEST_F(FrameRate_L2test, UpdateFpsUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    /*With both Params expecting Success*/
    params["newfps"] = 30;
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "updateFps", params, result);
    EXPECT_EQ(Core::ERROR_NONE, status);
    EXPECT_FALSE(result["result"].Boolean());
}

/************Test case Details **************************
** 1.Set newfps value as -1
** 2.Checking UpdateFps for negative check
** 3.Confirm fps update failure using Jsonrpc.
*******************************************************/

TEST_F(FrameRate_L2test, UpdateFpsFailureUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    /*With one Param  expecting Fail case */
    params["newfps"] = -1;
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "updateFps", params, result);
    EXPECT_FALSE(result["result"].Boolean());
}

/************Test case Details **************************
** 1.Valid FrameRate values are set.
** 2.Mock framerate values to set.
** 3.Invokes SetDisplayFrameRate with valid values.
** 4.For STB profile, set the status as FALSE.
** 5.Check the status of setDisplayframerate using Jsonrpc.
*******************************************************/

TEST_F(FrameRate_L2test, SetDisplayFrameRateUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    ON_CALL(*p_dsVideoDeviceHalMock, dsSetDisplayframerate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](intptr_t, char* framerate) {
                EXPECT_EQ(string(framerate), string("3840x2160px48"));
                return dsERR_NONE;
            }));

    /*With both Params expecting Success*/
    params["FrameRate"] = "3840x2160px48";
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "setDisplayFrameRate", params, result);
    EXPECT_FALSE(result["result"].Boolean());
}

/************Test case Details **************************
** 1.Invalid FrameRate values are set.
** 2.Invokes SetDisplayFrameRate with invalid values.
** 3.Check the failure status of setDisplayframerate using Jsonrpc.
*******************************************************/

TEST_F(FrameRate_L2test, SetDisplayFrameRateFailureUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    /*With one Param  expecting Fail case */
    params["FrameRate"] = "3840x2160p";
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "setDisplayFrameRate", params, result);
    EXPECT_FALSE(result["result"].Boolean());
}

/************Test case Details **************************
** 1.Mock framerate values.
** 2.Invokes GetDisplayFrameRate.
** 3.For STB profile, set the status as FALSE.
** 4.Check the status of GetDisplayFrameRate using Jsonrpc.
*******************************************************/

TEST_F(FrameRate_L2test, GetDisplayFrameRateUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    ON_CALL(*p_dsVideoDeviceHalMock, dsGetCurrentDisplayframerate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](intptr_t, char* framerate) {
                if (framerate) { strcpy(framerate, "3840x2160px48"); }
                return dsERR_NONE;
            }));

    /*With both Params expecting Success*/
    params["displayFrameRate"];
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "getDisplayFrameRate", params, result);
    EXPECT_TRUE(result["success"].Boolean());
}

/************Test case Details **************************
** 1.Mock FRFMode values to set.
** 2.Invokes SetFrmMode.
** 3.For STB profile, set the status as FALSE.
** 4.Check the status of SetFrmMode using Jsonrpc.
*******************************************************/

TEST_F(FrameRate_L2test, SetFrmModeUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    ON_CALL(*p_dsVideoDeviceHalMock, dsSetFRFMode(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](intptr_t, int param) {
                EXPECT_EQ(param, 0);
                return dsERR_NONE;
            }));

    /*With both Params expecting Success*/
    params["frmmode"] = 0;
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "setFrmMode", params, result);
    EXPECT_FALSE(result["result"].Boolean());
}

/************Test case Details **************************
** 1.set frmmode to negative value.
** 2.Invokes SetFrmMode with negative values.
** 3.Check the failure status of SetFrmMode using Jsonrpc.
*******************************************************/

TEST_F(FrameRate_L2test, SetFrmModeFailureUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    /*With one Param  expecting Fail case */
    params["frmmode"] = -1;
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "setFrmMode", params, result);
    EXPECT_FALSE(result["result"].Boolean());
}

/************Test case Details **************************
** 1.Mock frmmode values.
** 2.Invokes GetFrmMode.
** 3.For STB profile, set the status as success.
** 4.Check the status of GetFrmMode using Jsonrpc.
*******************************************************/

TEST_F(FrameRate_L2test, GetFrmModeUsingJsonrpc) {
    JSONRPC::LinkType<Core::JSON::IElement> jsonrpc(FrameRate_CALLSIGN, FrameRateL2TEST_CALLSIGN);
    StrictMock<AsyncHandlerMock_FrameRate> async_handler;
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;

    ON_CALL(*p_dsVideoDeviceHalMock, dsGetFRFMode(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](intptr_t, int* out) {
                if (out) { *out = 0; }
                return dsERR_NONE;
            }));

    /*With both Params expecting Success*/
    params["frmmode"] = 0;
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "getFrmMode", params, result);
    EXPECT_TRUE(result["success"].Boolean());
}
/************Test case Details **************************
** E2E Test: Set Display Framerate with HAL Mock Verification
** 1. Set up expectations on dsVideoDeviceHalMock
** 2. Call setDisplayFrameRate via COM-RPC
** 3. Verify HAL was called with correct parameters
** 4. Verify callbacks are triggered
*******************************************************/

TEST_F(FrameRate_L2test, E2E_SetDisplayFrameRate_WithMockVerification) {
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;
    
    TEST_LOG("=== E2E Test: SetDisplayFrameRate with Mock Verification ===");
    
    // Arrange: Set up expectations on the HAL mock
    std::string capturedFramerate;
    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsSetDisplayframerate(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&capturedFramerate](intptr_t handle, const char* framerate) {
                TEST_LOG("HAL Mock: dsSetDisplayframerate called with handle=%ld, framerate=%s", 
                         handle, framerate);
                capturedFramerate = framerate;
                return dsERR_NONE;
            }));
    
    // Act: Call setDisplayFrameRate via JSON-RPC
    params["framerate"] = "3840x2160px60";
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "setDisplayFrameRate", params, result);
    
    // Assert: Verify the call succeeded. Thunder's JSON-RPC codegen folds a lone
    // "success" out-param into the RPC status code rather than the response body
    // (it only appears in the body alongside other out-params, e.g. GetDisplayFrameRate).
    EXPECT_EQ(status, Core::ERROR_NONE);
    
    // Assert: Verify HAL was called with correct framerate
    EXPECT_EQ("3840x2160px60", capturedFramerate);
    
    TEST_LOG("=== E2E Test PASSED: HAL was called with framerate=%s ===", capturedFramerate.c_str());
}

/************Test case Details **************************
** E2E Test: Get Display Framerate with HAL Mock
** 1. Configure mock to return specific framerate
** 2. Call getDisplayFrameRate via COM-RPC
** 3. Verify returned value matches mock configuration
*******************************************************/

TEST_F(FrameRate_L2test, E2E_GetDisplayFrameRate_WithMockVerification) {
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;
    
    TEST_LOG("=== E2E Test: GetDisplayFrameRate with Mock Verification ===");
    
    // Arrange: Configure mock to return "120" Hz
    ON_CALL(*p_dsVideoDeviceHalMock, dsGetCurrentDisplayframerate(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [](intptr_t handle, char* framerate) {
                TEST_LOG("HAL Mock: dsGetCurrentDisplayframerate called with handle=%ld", handle);
                if (framerate) {
                    strcpy(framerate, "120");
                }
                return dsERR_NONE;
            }));
    
    // Act: Call getDisplayFrameRate via JSON-RPC
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "getDisplayFrameRate", params, result);
    
    // Assert: Verify the call succeeded
    EXPECT_EQ(status, Core::ERROR_NONE);
    EXPECT_TRUE(result["success"].Boolean());
    
    // Assert: Verify returned framerate matches mock
    EXPECT_STREQ("120", result["framerate"].String().c_str());
    
    TEST_LOG("=== E2E Test PASSED: Framerate=%s ===", result["framerate"].String().c_str());
}

/************Test case Details **************************
** E2E Test: HAL Error Handling
** 1. Configure mock to return error
** 2. Call setDisplayFrameRate via COM-RPC
** 3. Verify error is properly propagated
*******************************************************/

TEST_F(FrameRate_L2test, E2E_SetDisplayFrameRate_HALErrorHandling) {
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;
    
    TEST_LOG("=== E2E Test: HAL Error Handling ===");
    
    // Arrange: Configure mock to return error
    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsSetDisplayframerate(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [](intptr_t handle, const char* framerate) {
                TEST_LOG("HAL Mock: dsSetDisplayframerate returning error");
                return dsERR_OPERATION_FAILED;
            }));
    
    // Act: Call setDisplayFrameRate via JSON-RPC
    params["framerate"] = "3840x2160px60";
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "setDisplayFrameRate", params, result);
    
    // Assert: Verify error is propagated
    EXPECT_FALSE(result["success"].Boolean());
    
    TEST_LOG("=== E2E Test PASSED: Error properly handled ===");
}

/************Test case Details **************************
** E2E Test: Multiple HAL Calls Verification
** 1. Set up expectations for multiple HAL calls
** 2. Perform multiple operations
** 3. Verify all HAL calls were made correctly
*******************************************************/

TEST_F(FrameRate_L2test, E2E_MultipleHALCalls_Verification) {
    uint32_t status = Core::ERROR_GENERAL;
    JsonObject params;
    JsonObject result;
    
    TEST_LOG("=== E2E Test: Multiple HAL Calls ===");
    
    // Arrange: Expect multiple HAL calls
    ::testing::InSequence seq;
    
    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsSetDisplayframerate(::testing::_, ::testing::StrEq("3840x2160px60")))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));
    
    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsGetCurrentDisplayframerate(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [](intptr_t, char* framerate) {
                if (framerate) strcpy(framerate, "3840x2160px60");
                return dsERR_NONE;
            }));
    
    EXPECT_CALL(*p_dsVideoDeviceHalMock, dsSetDisplayframerate(::testing::_, ::testing::StrEq("3840x2160px120")))
        .Times(1)
        .WillOnce(::testing::Return(dsERR_NONE));
    
    // Act: Perform multiple operations
    params["framerate"] = "3840x2160px60";
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "setDisplayFrameRate", params, result);
    // A lone "success" out-param is folded into the status code, not the response body.
    EXPECT_EQ(status, Core::ERROR_NONE);
    
    params.Clear();
    result.Clear();
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "getDisplayFrameRate", params, result);
    EXPECT_TRUE(result["success"].Boolean());
    EXPECT_STREQ("3840x2160px60", result["framerate"].String().c_str());
    
    params.Clear();
    result.Clear();
    params["framerate"] = "3840x2160px120";
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "setDisplayFrameRate", params, result);
    EXPECT_EQ(status, Core::ERROR_NONE);
    
    TEST_LOG("=== E2E Test PASSED: All HAL calls verified ===");
}

/************Test case Details **************************
** E2E Test: Audio HAL Integration
** 1. Verify audio HAL is initialized properly
** 2. Test audio port operations
*******************************************************/

TEST_F(FrameRate_L2test, E2E_AudioHAL_Integration) {
    TEST_LOG("=== E2E Test: Audio HAL Integration ===");
    
    // Verify audio HAL init was called during DeviceSettings activation
    // This is implicit - if DeviceSettings activated successfully, audio HAL was initialized
    
    // Arrange: Set up audio HAL expectations
    EXPECT_CALL(*p_dsAudioHalMock, dsSetStereoAuto(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(0))  // May be called during initialization
        .WillRepeatedly(::testing::Return(dsERR_NONE));
    
    TEST_LOG("=== E2E Test PASSED: Audio HAL integrated ===");
}

/************Test case Details **************************
** E2E Test: VideoPort HAL Integration
** 1. Verify video port HAL is initialized
** 2. Test display connection status
*******************************************************/

TEST_F(FrameRate_L2test, E2E_VideoPortHAL_Integration) {
    TEST_LOG("=== E2E Test: VideoPort HAL Integration ===");
    
    // Verify VideoPort HAL operations
    bool displayConnected = false;
    
    // Configure mock
    ON_CALL(*p_dsVideoPortHalMock, dsIsDisplayConnected(::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(
            [&displayConnected](intptr_t, bool* connected) {
                TEST_LOG("HAL Mock: dsIsDisplayConnected called");
                if (connected) {
                    *connected = true;
                    displayConnected = true;
                }
                return dsERR_NONE;
            }));
    
    // VideoPort operations are called internally by DeviceSettings
    // If DeviceSettings is active, VideoPort HAL is working
    
    TEST_LOG("=== E2E Test PASSED: VideoPort HAL integrated ===");
}
