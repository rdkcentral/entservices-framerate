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

#include <stdlib.h>
#include <errno.h>
#include <string>
#include <iomanip>
#include <sys/prctl.h>
#include <mutex>

#include "FrameRateImplementation.h"
#include "host.hpp"
#include "exception.hpp"

#include "UtilsJsonRpc.h"
#include "UtilsIarm.h"
#include "UtilsProcess.h"

//Defines
#define DEFAULT_FPS_COLLECTION_TIME_IN_MILLISECONDS 10000
#define MINIMUM_FPS_COLLECTION_TIME_IN_MILLISECONDS 100
#define DEFAULT_MIN_FPS_VALUE 60
#define DEFAULT_MAX_FPS_VALUE -1

#ifdef ENABLE_DEBUG
#define DBGINFO(fmt, ...) LOGINFO(fmt, ##__VA_ARGS__)
#else
#define DBGINFO(fmt, ...)
#endif

namespace WPEFramework
{
    namespace Plugin
    {
        SERVICE_REGISTRATION(FrameRateImplementation, 1, 0);
        FrameRateImplementation* FrameRateImplementation::_instance = nullptr;

        FrameRateImplementation::FrameRateImplementation()
            : _adminLock()
              , m_fpsCollectionFrequencyInMs(DEFAULT_FPS_COLLECTION_TIME_IN_MILLISECONDS)
              , m_minFpsValue(DEFAULT_MIN_FPS_VALUE)
              , m_maxFpsValue(DEFAULT_MAX_FPS_VALUE)
              , m_totalFpsValues(0)
              , m_numberOfFpsUpdates(0)
              , m_fpsCollectionInProgress(false)
              , m_lastFpsValue(0)
        {
            FrameRateImplementation::_instance = this;
            device::Host::getInstance().Register(this, "WPE::FrameRate");
            // Connect the timer callback handle for triggering FrameRate notifications.
            m_reportFpsTimer.connect(std::bind(&FrameRateImplementation::onReportFpsTimer, this));
        }

        FrameRateImplementation::~FrameRateImplementation()
        {
            device::Host::getInstance().UnRegister(this);
            //Stop the timer if running
            if (m_reportFpsTimer.isActive())
            {
                m_reportFpsTimer.stop();
            }
        }

        /******************************************* Notifications ****************************************/

        /**
         * @brief This function is used to dispatch 'onFpsEvent' to all registered notification sinks.
         * @param average - The average frame rate.
         * @param min - The minimum frame rate.
         * @param max - The maximum frame rate.
         * @return void
         */
        void FrameRateImplementation::dispatchOnFpsEvent(int average, int min, int max)
        {
            std::list<Exchange::IFrameRate::INotification*>::const_iterator index(_framerateNotification.begin());
            while (index != _framerateNotification.end())
            {
                (*index)->OnFpsEvent(average, min, max);
                ++index;
            }
            DBGINFO("average = %d, min = %d, max = %d.", average, min, max);
        }

        /**
         * @brief This function is used to dispatch 'onDisplayFrameRateChanging' to all registered notifications.
         * @param displayFrameRate - The display frame rate.
         * @return void
         */
        void FrameRateImplementation::dispatchOnDisplayFrameRateChangingEvent(const string& displayFrameRate)
        {
            std::list<Exchange::IFrameRate::INotification*>::const_iterator index(_framerateNotification.begin());
            while (index != _framerateNotification.end())
            {
                (*index)->OnDisplayFrameRateChanging(displayFrameRate);
                ++index;
            }
            DBGINFO("displayFrameRate: '%s'", displayFrameRate.c_str());
        }

        /**
         * @brief This function is used to dispatch 'onDisplayFrameRateChanged' to all registered notifications.
         * @param displayFrameRate - The display frame rate.
         * @return void
         */
        void FrameRateImplementation::dispatchOnDisplayFrameRateChangedEvent(const string& displayFrameRate)
        {
            std::list<Exchange::IFrameRate::INotification*>::const_iterator index(_framerateNotification.begin());
            while (index != _framerateNotification.end())
            {
                (*index)->OnDisplayFrameRateChanged(displayFrameRate);
                ++index;
            }
            DBGINFO("displayFrameRate: '%s'", displayFrameRate.c_str());
        }

        /**
         * @brief This function is used to dispatch the display frame rate change event, triggered by worker pool.
         * @param event - The event type (prechange or postchange).
         * @param params - The parameters associated with the event.
         * @return void
         */
        void FrameRateImplementation::DispatchDSMGRDisplayFramerateChangeEvent(Event event, const JsonValue params)
        {
            _adminLock.Lock();
            switch (event)
            {
                case  DSMGR_EVENT_DISPLAY_FRAMRATE_PRECHANGE:
                    dispatchOnDisplayFrameRateChangingEvent(params.String());
                    break;

                case  DSMGR_EVENT_DISPLAY_FRAMRATE_POSTCHANGE:
                    dispatchOnDisplayFrameRateChangedEvent(params.String());
                    break;
            }
            _adminLock.Unlock();
        }

        Core::hresult FrameRateImplementation::Register(Exchange::IFrameRate::INotification *notification)
        {
            Core::hresult status = Core::ERROR_NONE;
            ASSERT(nullptr != notification);
            _adminLock.Lock();

            // Check if the notification is already registered
            if (std::find(_framerateNotification.begin(), _framerateNotification.end(), notification) != _framerateNotification.end())
            {
                LOGERR("Same notification is registered already");
                status = Core::ERROR_ALREADY_CONNECTED;
            }

            _framerateNotification.push_back(notification);
            notification->AddRef();

            _adminLock.Unlock();
            return status;
        }

        Core::hresult FrameRateImplementation::Unregister(Exchange::IFrameRate::INotification *notification)
        {
            Core::hresult status = Core::ERROR_GENERAL;
            ASSERT(nullptr != notification);
            _adminLock.Lock();

            // Just unregister one notification once
            auto itr = std::find(_framerateNotification.begin(), _framerateNotification.end(), notification);
            if (itr != _framerateNotification.end())
            {
                (*itr)->Release();
                _framerateNotification.erase(itr);
                status = Core::ERROR_NONE;
            }
            LOGERR("Notification %p not found in _framerateNotification", notification);

            _adminLock.Unlock();
            return status;
        }

        /***************************************** Methods **********************************************/

        /**
         * @brief Returns the current display frame rate values.
         * @param framerate - The current display framerate setting (width x height x framerate)
         * @param success - Indicates whether the operation was successful.
         * @return Core::ERROR_NONE on success, Core::ERROR_GENERAL on failure.
         */
        Core::hresult FrameRateImplementation::GetDisplayFrameRate(string& framerate, bool& success)
        {
            DBGINFO();
            success = false;

            std::lock_guard<std::mutex> guard(m_callMutex);

            try
            {
                device::List<device::VideoDevice> videoDevices = device::Host::getInstance().getVideoDevices();
                if (videoDevices.size() == 0)
                {
                    LOGERR("No video devices available.");
                    return Core::ERROR_NOT_SUPPORTED;
                }

                char sFramerate[32] = {0};
                device::VideoDevice& device = videoDevices.at(0);
                if (!device.getCurrentDisframerate(sFramerate) && sFramerate[0] != '\0')
                {
                    framerate = sFramerate;
                    success = true;
                    return Core::ERROR_NONE;
                }

                LOGERR("getCurrentDisframerate error, DS::ERROR.");
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION0();
            }
            catch(const std::exception& err)
            {
                LOGERR("exception: %s", err.what());
                success = false;
            }
            catch(...)
            {
                LOGWARN("Unknown exception occurred");
                success = false;
            }

            return Core::ERROR_GENERAL;
        }

        /**
         * @brief Returns the current auto framerate mode.
         * @param autoFRMMode - The current auto framerate mode.
         * @param success - Indicates whether the operation was successful.
         * @return Core::ERROR_NONE on success, Core::ERROR_GENERAL on failure.
         */
        Core::hresult FrameRateImplementation::GetFrmMode(int &autoFRMMode, bool& success)
        {
            DBGINFO();
            std::lock_guard<std::mutex> guard(m_callMutex);

            success = false;
            try
            {
                device::List<device::VideoDevice> videoDevices = device::Host::getInstance().getVideoDevices();
                if (videoDevices.size() == 0)
                {
                    LOGERR("No video devices available.");
                    return Core::ERROR_NOT_SUPPORTED;
                }
                device::VideoDevice& device = videoDevices.at(0);
                if (!device.getFRFMode(&autoFRMMode))
                {
                    DBGINFO("Frame Mode: %d", autoFRMMode);
                    success = true;
                    return Core::ERROR_NONE;
                }
                LOGERR("getFRFMode failed DS::ERROR.");
            }
            catch(const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION0();
            }
            catch(const std::exception& err)
            {
                LOGERR("exception: %s", err.what());
                success = false;
            }
            catch(...)
            {
                LOGWARN("Unknown exception occurred");
                success = false;
            }
            return Core::ERROR_GENERAL;
        }

        /**
         * @brief This function is used to set the FPS data collection interval.
         * @param frequency - The amount of time in milliseconds. Default is 10000ms and min is 100ms.
         * @param success - Indicates whether the operation was successful.
         * @return Core::ERROR_NONE on success, Core::ERROR_GENERAL on failure.
         */
        Core::hresult FrameRateImplementation::SetCollectionFrequency(int frequency, bool& success)
        {
            DBGINFO();
            success = false;
            if (frequency < MINIMUM_FPS_COLLECTION_TIME_IN_MILLISECONDS)
            {
                LOGERR("Invalid frequency, minimum is %d ms.", MINIMUM_FPS_COLLECTION_TIME_IN_MILLISECONDS);
                return Core::ERROR_INVALID_PARAMETER;
            }

            std::lock_guard<std::mutex> guard(m_callMutex);
            try
            {
                m_fpsCollectionFrequencyInMs = frequency;
                DBGINFO("FrameRate collection frequency set to %d milliseconds.", frequency);
                success = true;
                return Core::ERROR_NONE;
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION0();
            }
            return Core::ERROR_GENERAL;
        }

        /**
         * @brief Sets the display framerate values.
         * @param framerate - The display frame rate in the format "WIDTHxHEIGHTxFPS".
         * @param success - Indicates whether the operation was successful.
         * @return Core::ERROR_NONE on success, Core::ERROR_GENERAL on failure.
         */
        Core::hresult FrameRateImplementation::SetDisplayFrameRate(const string& framerate, bool& success)
        {
            // framerate should be of "WIDTHxHEIGHTxFPS" as per DSHAL specification - setDisplayframerate
            // Eg: 1920px1080px60
            // check if we got two 'x' in the string at least.
            success = false;
            if (std::count(framerate.begin(), framerate.end(), 'x') != 2 ||
                    !isdigit(framerate.front()) || !isdigit(framerate.back()))
            {
                LOGERR("Invalid frame rate format: '%s'", framerate.c_str());
                return Core::ERROR_INVALID_PARAMETER;
            }
            string sFramerate = framerate;
            std::lock_guard<std::mutex> guard(m_callMutex);

            try
            {
                device::List<device::VideoDevice> videoDevices = device::Host::getInstance().getVideoDevices();
                if (videoDevices.size() == 0)
                {
                    LOGERR("No video devices available.");
                    return Core::ERROR_NOT_SUPPORTED;
                }
                device::VideoDevice& device = videoDevices.at(0);
                if (!device.setDisplayframerate(sFramerate.c_str()))
                {
                    success = true;
                    return Core::ERROR_NONE;
                }
                LOGERR("setDisplayframerate failed, DS::ERROR.");
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION0();
            }
            catch(const std::exception& err)
            {
                LOGERR("exception: %s", err.what());
                success = false;
            }
            catch(...)
            {
                LOGWARN("Unknown exception occurred");
                success = false;
            }
            return Core::ERROR_GENERAL;
        }

        /**
         * @brief Sets the auto framerate mode.
         * @param frmmode - The frame mode (0 or 1).
         * @param success - Indicates whether the operation was successful.
         * @return Core::ERROR_NONE on success, Core::ERROR_GENERAL on failure.
         */
        Core::hresult FrameRateImplementation::SetFrmMode(int frmmode, bool& success)
        {
            success = false;
            if (frmmode != 0 && frmmode != 1)
            {
                LOGERR("Invalid frame mode: %d", frmmode);
                return Core::ERROR_INVALID_PARAMETER;
            }

            std::lock_guard<std::mutex> guard(m_callMutex);

            try
            {
                device::List<device::VideoDevice> videoDevices = device::Host::getInstance().getVideoDevices();
                if (videoDevices.size() == 0)
                {
                    LOGERR("No video devices available.");
                    return Core::ERROR_NOT_SUPPORTED;
                }
                device::VideoDevice& device = videoDevices.at(0);
                if (!device.setFRFMode(frmmode))
                {
                    success = true;
                    return Core::ERROR_NONE;
                }
                DBGINFO("Failed to set frame mode DS::ERROR  %d", frmmode);
            }
            catch (const device::Exception& err)
            {
                LOG_DEVICE_EXCEPTION0();
            }
            catch(const std::exception& err)
            {
                LOGERR("exception: %s", err.what());
                success = false;
            }
            catch(...)
            {
                LOGWARN("Unknown exception occurred");
                success = false;
            }
            return Core::ERROR_GENERAL;
        }

        /**
         * @brief Starts the FPS data collection.
         * @param success - Indicates whether the operation was successful.
         * @return Core::ERROR_NONE on success, Core::ERROR_GENERAL on failure.
         */
        Core::hresult FrameRateImplementation::StartFpsCollection(bool& success)
        {
            DBGINFO();
            std::lock_guard<std::mutex> guard(m_callMutex);

            if (m_fpsCollectionInProgress)
            {
                DBGINFO("FPS collection is already in progress.");
            }
            if (m_reportFpsTimer.isActive())
            {
                m_reportFpsTimer.stop();
            }

            m_minFpsValue = DEFAULT_MIN_FPS_VALUE;
            m_maxFpsValue = DEFAULT_MAX_FPS_VALUE;
            m_totalFpsValues = 0;
            m_numberOfFpsUpdates = 0;
            m_fpsCollectionInProgress = true;
            int fpsCollectionFrequency = m_fpsCollectionFrequencyInMs;

            if (fpsCollectionFrequency < MINIMUM_FPS_COLLECTION_TIME_IN_MILLISECONDS)
            {
                fpsCollectionFrequency = MINIMUM_FPS_COLLECTION_TIME_IN_MILLISECONDS;
            }
            m_lastFpsValue = -1;
            m_reportFpsTimer.start(fpsCollectionFrequency);
            DBGINFO("FPS collection timer started with frequency %d milliseconds.", fpsCollectionFrequency);
            success = true;
            return Core::ERROR_NONE;
        }

        /**
         * @brief Stops the FPS data collection.
         * @param success - Indicates whether the operation was successful.
         * @return Core::ERROR_NONE on success, Core::ERROR_GENERAL on failure.
         */
        Core::hresult FrameRateImplementation::StopFpsCollection(bool& success)
        {
            DBGINFO();
            std::lock_guard<std::mutex> guard(m_callMutex);

            if (m_reportFpsTimer.isActive())
            {
                m_reportFpsTimer.stop();
            }
            if (m_fpsCollectionInProgress)
            {
                m_fpsCollectionInProgress = false;
                int averageFps = -1;
                int minFps = -1;
                int maxFps = -1;
                if (m_numberOfFpsUpdates > 0)
                {
                    averageFps = (m_totalFpsValues / m_numberOfFpsUpdates);
                    minFps = m_minFpsValue;
                    maxFps = m_maxFpsValue;
                    dispatchOnFpsEvent(averageFps, minFps, maxFps);
                }
            }
            success = true;
            return Core::ERROR_NONE;
        }

        /**
         * @brief Updates the FPS value.
         * @param newFpsValue - The new FPS value.
         * @param success - Indicates whether the operation was successful.
         * @return Core::ERROR_NONE on success, Core::ERROR_GENERAL on failure.
         */
        Core::hresult FrameRateImplementation::UpdateFps(int newFpsValue, bool& success)
        {
            DBGINFO();
            if (newFpsValue < 0)
            {
                LOGERR("Invalid FPS value: %d", newFpsValue);
                success = false;
                return Core::ERROR_INVALID_PARAMETER;
            }
            std::lock_guard<std::mutex> guard(m_callMutex);

            m_maxFpsValue = std::max(m_maxFpsValue, newFpsValue);
            m_minFpsValue = std::min(m_minFpsValue, newFpsValue);
            m_totalFpsValues += newFpsValue;
            m_numberOfFpsUpdates++;
            m_lastFpsValue = newFpsValue;
            DBGINFO("m_maxFpsValue = %d, m_minFpsValue = %d, m_totalFpsValues = %d, m_numberOfFpsUpdates = %d",
                    m_maxFpsValue, m_minFpsValue, m_totalFpsValues, m_numberOfFpsUpdates);

            success = true;
            return Core::ERROR_NONE;
        }

        /************************************** Implementation specific ****************************************/

        /**
         * @brief This function is used to handle the timer event for reporting FPS.
         * @return void
         */
        void FrameRateImplementation::onReportFpsTimer()
        {
            std::lock_guard<std::mutex> guard(m_callMutex);
            int averageFps = (m_numberOfFpsUpdates > 0) ? (m_totalFpsValues / m_numberOfFpsUpdates) : -1;
            int minFps = (m_numberOfFpsUpdates > 0) ? m_minFpsValue : DEFAULT_MIN_FPS_VALUE;
            int maxFps = (m_numberOfFpsUpdates > 0) ? m_maxFpsValue : DEFAULT_MAX_FPS_VALUE;

            dispatchOnFpsEvent(averageFps, minFps, maxFps);

            if (m_lastFpsValue >= 0)
            {
                // store the last fps value just in case there are no updates
                m_minFpsValue = m_lastFpsValue;
                m_maxFpsValue = m_lastFpsValue;
                m_totalFpsValues = m_lastFpsValue;
                m_numberOfFpsUpdates = 1;
            }
            else
            {
                m_minFpsValue = DEFAULT_MIN_FPS_VALUE;
                m_maxFpsValue = DEFAULT_MAX_FPS_VALUE;
                m_totalFpsValues = 0;
                m_numberOfFpsUpdates = 0;
            }
        }

        void FrameRateImplementation::OnDisplayFrameratePreChange(const std::string& frameRate)
        {
            LOGINFO("Received OnDisplayFrameratePreChange callback");
            Core::IWorkerPool::Instance().Submit(FrameRateImplementation::Job::Create(FrameRateImplementation::_instance,
                                    FrameRateImplementation::DSMGR_EVENT_DISPLAY_FRAMRATE_PRECHANGE,
                                    frameRate));
        }

        void FrameRateImplementation::OnDisplayFrameratePostChange(const std::string& frameRate)
        {
            LOGINFO("Received OnDisplayFrameratePostChange callback");
            Core::IWorkerPool::Instance().Submit(FrameRateImplementation::Job::Create(FrameRateImplementation::_instance,
                                    FrameRateImplementation::DSMGR_EVENT_DISPLAY_FRAMRATE_POSTCHANGE,
                                    frameRate));
        }
    } // namespace Plugin
} // namespace WPEFramework