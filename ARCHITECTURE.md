# FrameRate Plugin Architecture

## Overview

The FrameRate plugin is a Thunder/WPEFramework-based service that provides real-time monitoring and reporting of video frame rate information in RDK environments. The plugin integrates with the Device Settings (DS) library and IARM bus to deliver comprehensive frame rate monitoring capabilities for video displays.

## System Architecture

### Core Components

```
┌─────────────────────────────────────────────────────────────────┐
│                     Thunder Framework                           │
├─────────────────────────────────────────────────────────────────┤
│  FrameRate Plugin                                              │
│  ┌─────────────────┐    ┌─────────────────────────────────────┐  │
│  │   FrameRate     │    │    FrameRateImplementation         │  │
│  │   (JSONRPC)     │◄───┤    (Core Business Logic)           │  │
│  │                 │    │                                     │  │
│  └─────────────────┘    └─────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                    Utilities Layer                              │
│  ┌─────────────────┐ ┌─────────────────┐ ┌─────────────────────┐ │
│  │  UtilsJsonRpc   │ │   UtilsIarm     │ │   UtilsProcess      │ │
│  └─────────────────┘ └─────────────────┘ └─────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│                 Platform Integration                            │
│  ┌─────────────────┐ ┌─────────────────┐ ┌─────────────────────┐ │
│  │  Device Settings│ │   IARM Bus      │ │   Video Device      │ │
│  │      (DS)       │ │                 │ │     Events          │ │
│  └─────────────────┘ └─────────────────┘ └─────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

### Component Descriptions

#### 1. FrameRate Plugin (JSON-RPC Interface)
- **Purpose**: Provides the external API interface for frame rate operations
- **Responsibilities**: 
  - JSON-RPC method handling
  - Event notification management
  - Thunder framework integration
- **Key Methods**: Setters, getters, and event notifications for frame rate monitoring

#### 2. FrameRateImplementation (Core Logic)
- **Purpose**: Implements the actual frame rate monitoring and collection logic
- **Responsibilities**:
  - Frame rate calculation and statistical analysis
  - Device Settings integration for display information
  - Event processing and notification dispatching
  - Background monitoring thread management

#### 3. Utilities Layer
- **UtilsJsonRpc**: JSON-RPC utility functions for parameter handling
- **UtilsIarm**: IARM bus communication utilities
- **UtilsProcess**: Process and system monitoring utilities
- **UtilsLogging**: Centralized logging infrastructure

## Data Flow and Interactions

### Frame Rate Monitoring Flow
1. **Initialization**: Plugin initializes and establishes connections to DS library and IARM bus
2. **Configuration**: Reads FPS collection parameters (timing, thresholds)
3. **Monitoring**: Background thread continuously monitors frame rate metrics
4. **Processing**: Calculates average, minimum, maximum frame rates
5. **Events**: Dispatches frame rate events to registered clients
6. **Display Changes**: Monitors display frame rate changes via DS events

### Event Processing Architecture
- **Display Frame Rate Events**: Asynchronous processing using job dispatch pattern
- **FPS Collection Events**: Timer-based collection with configurable intervals
- **Notification System**: Observer pattern implementation for client notifications

## Dependencies and Interfaces

### External Dependencies
- **Thunder Framework**: Core plugin infrastructure and JSON-RPC framework
- **Device Settings (DS)**: Display device management and video output control
- **IARM Bus**: Inter-process communication for RDK components
- **libds**: Low-level display settings interface

### Internal Dependencies
- **Minimal Helper Set**: Only essential utilities (JSON-RPC, IARM, Process, Logging)
- **No External Libraries**: Designed for minimal footprint and dependencies

## Plugin Framework Integration

### Thunder Framework Integration Points
- **IPlugin Interface**: Standard Thunder plugin lifecycle management
- **JSONRPC Interface**: RESTful API exposure for external clients
- **IDispatcher**: Event dispatching and job queue management
- **INotification**: Asynchronous event notification system

### Configuration and Startup
- **Dynamic Loading**: Plugin loaded as shared library (.so)
- **Configuration**: External configuration file support for runtime parameters
- **Startup Order**: Configurable startup ordering relative to other plugins
- **Preconditions**: Platform dependency management

## Technical Implementation Details

### Threading Model
- **Main Thread**: JSON-RPC request handling and initialization
- **Background Monitor**: Dedicated thread for continuous frame rate monitoring
- **Event Dispatcher**: Asynchronous job processing for DS events

### Performance Considerations
- **Configurable Collection Intervals**: Adjustable from 100ms to custom values
- **Statistical Processing**: Real-time calculation of min/max/average metrics
- **Memory Management**: RAII pattern with smart pointers for resource management
- **Event Throttling**: Prevents event flooding during rapid display changes

### Error Handling and Resilience
- **Graceful Degradation**: Continues operation even if optional components fail
- **Resource Cleanup**: Proper cleanup on plugin deactivation
- **Exception Safety**: Exception-safe design with proper resource management
- **Logging**: Comprehensive logging for debugging and monitoring