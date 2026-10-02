#pragma once

// GuiRenderer.hpp
// Hardware-accelerated Win32 split-screen GUI renderer and interaction handler.

#include "Protocol.hpp"
#include "Types.hpp"
#include "CameraManager.hpp"
#include "ReidMatcher.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <vector>
#include <atomic>

class GuiRenderer {
public:
    GuiRenderer(CameraManager& camera_mgr,
                ReidMatcher& matcher,
                std::vector<PersonProfile>& gallery,
                std::vector<int8_t>& raw_latest_emb,
                uint32_t& latest_box,
                uint32_t& latest_frame,
                int& latest_cam_id,
                uint32_t& total_reid_count,
                CRITICAL_SECTION& cs,
                std::atomic<bool>& running);
    ~GuiRenderer();

    bool init(HINSTANCE hInstance, const std::string& title = "Project Argus -- Multi-Camera Re-ID Surveillance Monitor");
    void runMessageLoop();
    HWND getHwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void onPaint(HWND hwnd);
    void onLButtonDown(HWND hwnd, int mx, int my);
    void onMouseWheel(HWND hwnd, short delta);
    void onKeyDown(HWND hwnd, WPARAM wParam);

    CameraManager&              m_camera_mgr;
    ReidMatcher&                m_matcher;
    std::vector<PersonProfile>& m_gallery;
    std::vector<int8_t>&        m_raw_latest_emb;
    uint32_t&                   m_latest_box;
    uint32_t&                   m_latest_frame;
    int&                        m_latest_cam_id;
    uint32_t&                   m_total_reid_count;
    CRITICAL_SECTION&           m_cs;
    std::atomic<bool>&          m_running;

    HWND                        m_hwnd = NULL;
};
