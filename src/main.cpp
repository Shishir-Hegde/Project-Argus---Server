#include "Protocol.hpp"
#include "Types.hpp"
#include "CameraManager.hpp"
#include "ReidMatcher.hpp"
#include "NetworkServer.hpp"
#include "GuiRenderer.hpp"
#include <iostream>
#include <vector>
#include <atomic>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

int main() {
    CRITICAL_SECTION cs;
    InitializeCriticalSection(&cs);

    std::atomic<bool> running{true};

    // Shared ReID Gallery Pool and Live Inspector State
    std::vector<PersonProfile> gallery;
    std::vector<int8_t> raw_latest_emb(REID_EMBEDDING_DIM, 0);
    uint32_t latest_box = 0;
    uint32_t latest_frame = 0;
    int latest_cam_id = 1;
    uint32_t total_reid_count = 0;
    HWND hwnd = NULL;

    // Subsystems
    CameraManager camera_mgr;
    ReidMatcher   matcher; // Default threshold: 0.82f (82%)

    GuiRenderer gui(camera_mgr,
                    matcher,
                    gallery,
                    raw_latest_emb,
                    latest_box,
                    latest_frame,
                    latest_cam_id,
                    total_reid_count,
                    cs,
                    running);

    HINSTANCE hInstance = GetModuleHandle(NULL);
    if (!gui.init(hInstance, "Project Argus -- Dual-Camera Split-Screen ReID Monitor")) {
        std::cerr << "[!] Failed to initialize GUI renderer\n";
        DeleteCriticalSection(&cs);
        return 1;
    }

    hwnd = gui.getHwnd();

    NetworkServer server(camera_mgr,
                         matcher,
                         gallery,
                         raw_latest_emb,
                         latest_box,
                         latest_frame,
                         latest_cam_id,
                         total_reid_count,
                         cs,
                         hwnd,
                         running);

    if (!server.start(LISTEN_PORT)) {
        std::cerr << "[!] Failed to start network server\n";
        DeleteCriticalSection(&cs);
        return 1;
    }

    // Win32 message loop
    gui.runMessageLoop();

    running = false;
    server.stop();
    DeleteCriticalSection(&cs);

    std::cout << "[*] Project Argus Server terminated cleanly.\n";
    return 0;
}
