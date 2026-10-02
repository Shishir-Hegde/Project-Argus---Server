#pragma once

// NetworkServer.hpp
// Dedicated high-throughput UDP listener thread for multi-camera streaming.

#include "Protocol.hpp"
#include "Types.hpp"
#include "CameraManager.hpp"
#include "ReidMatcher.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <vector>
#include <atomic>

class NetworkServer {
public:
    NetworkServer(CameraManager& camera_mgr,
                  ReidMatcher& matcher,
                  std::vector<PersonProfile>& gallery,
                  std::vector<int8_t>& raw_latest_emb,
                  uint32_t& latest_box,
                  uint32_t& latest_frame,
                  int& latest_cam_id,
                  uint32_t& total_reid_count,
                  CRITICAL_SECTION& cs,
                  HWND& hwnd,
                  std::atomic<bool>& running);
    ~NetworkServer();

    bool start(uint16_t port = LISTEN_PORT);
    void stop();

private:
    static DWORD WINAPI ThreadEntry(LPVOID lpParam);
    void run();

    CameraManager&              m_camera_mgr;
    ReidMatcher&                m_matcher;
    std::vector<PersonProfile>& m_gallery;
    std::vector<int8_t>&        m_raw_latest_emb;
    uint32_t&                   m_latest_box;
    uint32_t&                   m_latest_frame;
    int&                        m_latest_cam_id;
    uint32_t&                   m_total_reid_count;
    CRITICAL_SECTION&           m_cs;
    HWND&                       m_hwnd;
    std::atomic<bool>&          m_running;

    uint16_t                    m_port = LISTEN_PORT;
    SOCKET                      m_sock = INVALID_SOCKET;
    HANDLE                      m_hThread = NULL;
};
