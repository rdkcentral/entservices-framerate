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
#include <interfaces/IFrameRate.h>
#include "devicesettings.h"
#include "FrontPanelIndicatorMock.h"

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
    IARM_EventHandler_t _iarmDSFramerateEventHandler = nullptr;
    Core::JSONRPC::Message message;
    string response;

    virtual ~FrameRate_L2test() override;

public:
    FrameRate_L2test();
    device::Host::IVideoDeviceEvents* l_listener;
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

    EXPECT_CALL(*p_managerImplMock, Initialize())
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Return());

    ON_CALL(*p_hostImplMock, Register(::testing::Matcher<device::Host::IVideoDeviceEvents*>(::testing::_)))
             .WillByDefault(::testing::Invoke(
                 [&](device::Host::IVideoDeviceEvents* listener) {
                    l_listener = listener;
                    return dsERR_NONE;
         }));

    /* Activate plugin in constructor */
    status = ActivateService("org.rdk.FrameRate");
    EXPECT_EQ(Core::ERROR_NONE, status);

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

    ON_CALL(*p_hostImplMock, UnRegister(::testing::Matcher<device::Host::IVideoDeviceEvents*>(::testing::_)))
             .WillByDefault(::testing::Invoke(
                 [&](device::Host::IVideoDeviceEvents* listener) {
                    l_listener = nullptr;
                    return dsERR_NONE;
         }));

    if (m_FrameRateplugin) {
        m_FrameRateplugin->Unregister(&notify);
        m_FrameRateplugin->Release();
    }

    /* Deactivate plugin in destructor */
    status = DeactivateService("org.rdk.FrameRate");
    EXPECT_EQ(Core::ERROR_NONE, status);
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
    device::VideoDevice videoDevice;
    ON_CALL(*p_hostImplMock, getVideoDevices())
            .WillByDefault(::testing::Return(device::List<device::VideoDevice>({ videoDevice })));
    ON_CALL(*p_videoDeviceMock, setDisplayframerate(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](const char *param) {
                EXPECT_EQ(param, string("3840x2160px48"));
                return 0;
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
    device::VideoDevice videoDevice;
    ON_CALL(*p_hostImplMock, getVideoDevices())
            .WillByDefault(::testing::Return(device::List<device::VideoDevice>({ videoDevice })));
    ON_CALL(*p_videoDeviceMock, getCurrentDisframerate(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](char *param) {
                string framerate("3840x2160px48");
                ::memcpy(param, framerate.c_str(), framerate.length());
                return 0;
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
    device::VideoDevice videoDevice;
    ON_CALL(*p_hostImplMock, getVideoDevices())
            .WillByDefault(::testing::Return(device::List<device::VideoDevice>({ videoDevice })));

    ON_CALL(*p_videoDeviceMock, setFRFMode(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](int param) {
                EXPECT_EQ(param, 0);
                return 0;
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
    device::VideoDevice videoDevice;
    ON_CALL(*p_hostImplMock, getVideoDevices())
            .WillByDefault(::testing::Return(device::List<device::VideoDevice>({ videoDevice })));
    ON_CALL(*p_videoDeviceMock, getFRFMode(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](int *param) {
                *param = 0;
                return 0;
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
    l_listener->OnDisplayFrameratePreChange("3840x2160px48");
}

/************Test case Details **************************
** 1.Checking onDisplayFrameRateChanged
*******************************************************/
TEST_F(FrameRate_L2test, onDisplayFrameRateChanged)
{
    l_listener->OnDisplayFrameratePostChange("3840x2160px48");
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

    device::VideoDevice videoDevice;
    ON_CALL(*p_hostImplMock, getVideoDevices())
            .WillByDefault(::testing::Return(device::List<device::VideoDevice>({ videoDevice })));
    ON_CALL(*p_videoDeviceMock, setDisplayframerate(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](const char *param) {
                EXPECT_EQ(param, string("3840x2160px48"));
                return 0;
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
    
    device::VideoDevice videoDevice;
    ON_CALL(*p_hostImplMock, getVideoDevices())
            .WillByDefault(::testing::Return(device::List<device::VideoDevice>({ videoDevice })));
    ON_CALL(*p_videoDeviceMock, getCurrentDisframerate(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](char *param) {
                string framerate("3840x2160px48");
                ::memcpy(param, framerate.c_str(), framerate.length());
                return 0;
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

    device::VideoDevice videoDevice;
    ON_CALL(*p_hostImplMock, getVideoDevices())
            .WillByDefault(::testing::Return(device::List<device::VideoDevice>({ videoDevice })));
    ON_CALL(*p_videoDeviceMock, setFRFMode(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](int param) {
                EXPECT_EQ(param, 0);
                return 0;
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

    device::VideoDevice videoDevice;
    ON_CALL(*p_hostImplMock, getVideoDevices())
            .WillByDefault(::testing::Return(device::List<device::VideoDevice>({ videoDevice })));
    ON_CALL(*p_videoDeviceMock, getFRFMode(::testing::_))
        .WillByDefault(::testing::Invoke(
            [&](int *param) {
                *param = 0;
                return 0;
            }));

    /*With both Params expecting Success*/
    params["frmmode"] = 0;
    status = InvokeServiceMethod(FrameRate_CALLSIGN, "getFrmMode", params, result);
    EXPECT_TRUE(result["success"].Boolean());
}