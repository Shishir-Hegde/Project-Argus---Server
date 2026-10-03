# Project Argus: Central Aggregator & Multi-Camera Tracking Server

[![Platform](https://img.shields.io/badge/Platform-Windows%20(Win32%20%2F%20C%2B%2B17)-blue.svg)](https://microsoft.com)
[![Protocol](https://img.shields.io/badge/Protocol-UDP%20(TRON%20Magic)-orange.svg)](https://www.tron.org/)
[![Networking](https://img.shields.io/badge/Port-5000-green.svg)](https://github.com/Shishir-Hegde/Project-Argus---Server)
[![License](https://img.shields.io/badge/License-MIT-purple.svg)](LICENSE)

The **Project Argus Server** is the central visualization, telemetry aggregator, and cross-camera tracking hub for the Project Argus distributed edge vision system. It ingests asynchronous, high-speed binary UDP datagrams from multiple **STM32N6570-DK** edge camera nodes, performs real-time metric-space feature matching, and renders a unified multi-camera tracking interface.

---

## 1. System Overview

Traditional multi-camera vision systems stream bandwidth-heavy raw 1080p/4K video to a central server and rely on expensive server-grade GPUs to run deep neural networks. 

**Project Argus reverses this paradigm:**
* **Edge Execution**: The STM32N6 edge microcontrollers handle hardware-accelerated video capture, Mobile YOLO object detection, and quantized OSNet appearance feature extraction on the integrated Neural-ART NPU.
* **Lightweight Server Role**: The server receives compact 128-byte mathematical identity vectors, coordinates, and downscaled verification tiles. It performs **sub-millisecond Cosine Similarity matching** and spatial track updates on a standard commodity CPU without requiring any GPU.

---

## 2. Key Features

1. **Multi-Camera Auto-Discovery**:
   - Automatically registers and binds incoming STM32 edge nodes based on their source IP address (e.g. `192.168.1.112`, `192.168.1.113`) without manual configuration.
2. **Two-Tier Tracking Hierarchy**:
   - **Tier 1 (Local Single-Camera)**: Uses spatial bounding box overlap (IoU) and centroid proximity to match consecutive frames smoothly at zero compute cost.
   - **Tier 2 (Global Cross-Camera Re-ID)**: Evaluates normalized 128-d appearance vectors against a dynamically updated global identity gallery using Cosine Similarity, assigning persistent Global IDs across non-overlapping camera blind spots.
3. **Mutual Exclusion & Identity Smoothing**:
   - Automatically prevents assigning the same identity to targets concurrently active in separate cameras.
   - Updates gallery embeddings smoothly using Exponential Moving Average (EMA) to adapt to changing target lighting and angles.
4. **Real-Time Split-Screen GUI**:
   - Native high-performance Win32 / GDI multi-camera canvas.
   - Live bounding box overlays, persistent ID labels, and real-time similarity metrics.

---

## 3. Network Protocol Specification

Communication occurs over connectionless UDP on port **5000** to eliminate TCP handshake overhead and head-of-line blocking. Every packet begins with a 4-byte synchronization magic word (`0x54524F4E` / ASCII `"TRON"`).

| Packet Type | Magic | Description | Payload Content |
| :--- | :--- | :--- | :--- |
| **`0x01` Video Chunk** | `0x54524F4E` | Visual verification tiles | Downscaled RGB565 / JPEG frame chunks with frame index |
| **`0x02` OD Metadata** | `0x54524F4E` | Detection telemetry | Up to 10 bounding boxes (cx, cy, w, h, conf) and inference latency (ms) |
| **`0x03` ReID Metadata** | `0x54524F4E` | Appearance descriptors | Bounding box coordinates + 128-byte quantized `int8` embedding vector |

---

## 4. Quick Start (Evaluation Guide)

### Prerequisites (Laptop Host)
* **OS**: Windows 10 / 11 (64-bit).
* **Compiler (if building from source)**: MinGW-w64 (GCC with C++17 support) or CMake 3.16+.
* **Precompiled Executable**: A ready-to-run binary (`Project-Argus-Server.exe`) is included directly in the repository root for evaluation.

---

### Step 1: Configure Laptop Static IP & Firewall

The STM32 edge nodes communicate on the `192.168.1.x` subnet. Configure your laptop's Ethernet adapter to static IP `192.168.1.100`:

#### Using PowerShell (Administrator):
```powershell
# Assign static IP
New-NetIPAddress -InterfaceAlias "Ethernet" -IPAddress 192.168.1.100 -PrefixLength 24 -DefaultGateway 192.168.1.1

# Allow UDP Port 5000 in Windows Firewall
New-NetFirewallRule -DisplayName "Project Argus Server (UDP 5000)" -Direction Inbound -Protocol UDP -LocalPort 5000 -Action Allow
```

---

### Step 2: Run the Server

#### Option A: Run Precompiled Binary (Fastest)
Simply double-click:
```cmd
Project-Argus-Server.exe
```

#### Option B: One-Click Build & Run
Run the included batch script:
```cmd
build_and_run.bat
```
*(This automatically compiles `src/*.cpp` using g++ with C++17 optimizations, links Winsock/GDI libraries, and launches the server).*

#### Option C: Build via CMake
```cmd
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

---

## 5. Directory Structure

```
Project-Argus-Server/
├── include/
│   ├── CameraManager.hpp     # Dynamic node registration & camera stream buffers
│   ├── GuiRenderer.hpp       # Win32 GDI multi-camera canvas renderer
│   ├── NetworkServer.hpp     # Asynchronous UDP socket listener & packet parser
│   ├── Protocol.hpp          # Binary TRON protocol packet definitions
│   ├── ReidMatcher.hpp       # Cosine similarity metric & global gallery management
│   └── Types.hpp             # Shared detection, tracking, and telemetry types
├── src/
│   ├── main.cpp              # Application entry point & main event loop
│   ├── CameraManager.cpp
│   ├── GuiRenderer.cpp
│   ├── NetworkServer.cpp
│   └── ReidMatcher.cpp
├── CMakeLists.txt            # Cross-platform CMake configuration
├── build_and_run.bat         # Automated Windows build and launch script
├── Project-Argus-Server.exe  # Standalone precompiled executable
└── README.md
```

---

## 6. Linked Repositories

* **Edge Firmware Repository**: [Project-Argus Firmware (TRON microT-Kernel 3.0 on STM32N6)](https://github.com/Shiken56/Project-Argus)
* **TRON Forum**: [https://www.tron.org/](https://www.tron.org/)
