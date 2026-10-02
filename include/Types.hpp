#pragma once

// Types.hpp
// Core data structures for tracking, gallery profiles, and per-camera stream states.

#include "Protocol.hpp"
#include <vector>
#include <string>
#include <chrono>
#include <cstdint>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// Distinct Neon/Vivid colors for identities
static const COLORREF ID_COLORS[] = {
    RGB(0, 255, 255),    // Person 1: Cyan
    RGB(255, 140, 0),    // Person 2: Vivid Orange
    RGB(50, 255, 120),   // Person 3: Neon Green
    RGB(255, 60, 200),   // Person 4: Vibrant Magenta
    RGB(255, 230, 20),   // Person 5: Yellow
    RGB(130, 110, 255),  // Person 6: Purple
    RGB(0, 190, 255),    // Person 7: Sky Blue
    RGB(255, 80, 80)     // Person 8: Coral Red
};
static const int NUM_ID_COLORS = sizeof(ID_COLORS) / sizeof(ID_COLORS[0]);

// ReID Profile Structure (Unified Gallery / Shared Pool across all IP cameras)
struct PersonProfile {
    int id = 0;
    std::vector<float> feature;              // L2 normalized 128D feature vector
    COLORREF color = RGB(0, 255, 255);
    uint32_t last_seen_frame = 0;
    int last_cam_id = 0;                     // 1 or 2
    int match_count = 0;
    float last_similarity = 0.0f;
    std::chrono::steady_clock::time_point last_seen_time;
};

// ReID match score for a specific gallery person
struct PersonMatchScore {
    int person_id = 0;
    float sim = 0.0f;
};

// Spatial Multi-Object Track Structure (IoU tracking per camera)
struct ActiveTrack {
    int track_id = 0;               // Sequential Track ID (1, 2, ...) local to this camera
    int person_id = 0;              // Confirmed Shared Gallery Person ID (1, 2, ...) or 0 if unassigned
    float sim = 0.0f;               // Last match similarity
    COLORREF color = RGB(160, 160, 160);
    DetectionBox_t box = {};        // Last known bounding box coordinates
    std::vector<PersonMatchScore> all_scores; // Similarity score with each person in unified gallery
    std::chrono::steady_clock::time_point last_seen;
};

// Per-Camera Stream State (Independent frame assembly, buffers & tracks per IP)
struct CameraFeed {
    std::string ip = "";
    int cam_id = 0;                          // 1 or 2
    bool active = false;

    // Video chunk assembly & display buffers
    std::vector<uint8_t> raw_frame_rgb;      // 256 * 256 * 3
    std::vector<uint32_t> rgb32_buffer;      // 256 * 256 XRGB
    uint32_t active_frame_id = 0;
    uint32_t chunks_received = 0;
    uint32_t current_frame_id = 0;
    bool has_frame = false;

    // OD Metadata & Local Tracks
    OdMetadataPacket_t latest_meta = {};
    std::vector<ActiveTrack> tracks;
    int next_track_id = 1;

    // FPS & Telemetry
    uint32_t frame_count = 0;
    std::chrono::steady_clock::time_point fps_start;
    double stream_fps = 0.0;
    std::chrono::steady_clock::time_point last_packet_time;
};
