#include "CameraManager.hpp"
#include "ReidMatcher.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cmath>

CameraManager::CameraManager() {
    for (int i = 0; i < MAX_CAMERAS; ++i) {
        m_cams[i].cam_id = i + 1;
        m_cams[i].raw_frame_rgb.assign(FRAME_W * FRAME_H * 3, 0);
        m_cams[i].rgb32_buffer.assign(FRAME_W * FRAME_H, 0);
        m_cams[i].fps_start = std::chrono::steady_clock::now();
    }
}

int CameraManager::getOrRegisterSlot(const std::string& ip) {
    auto it = m_ip_to_slot.find(ip);
    if (it != m_ip_to_slot.end()) {
        return it->second;
    }

    int assigned = -1;
    if (m_num_registered < MAX_CAMERAS) {
        assigned = m_num_registered++;
    } else {
        assigned = 1;
    }

    m_ip_to_slot[ip] = assigned;
    m_cams[assigned].ip = ip;
    m_cams[assigned].cam_id = assigned + 1;
    m_cams[assigned].active = true;
    m_cams[assigned].raw_frame_rgb.assign(FRAME_W * FRAME_H * 3, 0);
    m_cams[assigned].rgb32_buffer.assign(FRAME_W * FRAME_H, 0);
    m_cams[assigned].fps_start = std::chrono::steady_clock::now();

    std::cout << "\n[CAMERA SLOT ASSIGNED] Board IP: " << ip
              << " registered to Split-Screen Pane #" << (assigned + 1)
              << " (Camera #" << (assigned + 1) << ")\n\n";

    return assigned;
}

void CameraManager::processOdPacket(int cam_idx, const OdMetadataPacket_t& meta) {
    if (cam_idx < 0 || cam_idx >= MAX_CAMERAS) return;
    auto& cam = m_cams[cam_idx];
    cam.latest_meta = meta;
    auto now_t = std::chrono::steady_clock::now();

    std::vector<bool> track_matched(cam.tracks.size(), false);
    for (uint8_t i = 0; i < meta.num_boxes && i < OD_MAX_BOXES; ++i) {
        int best_t = -1;
        float best_overlap = 0.15f;

        for (size_t t = 0; t < cam.tracks.size(); ++t) {
            if (track_matched[t]) continue;
            float iou = ReidMatcher::computeIoU(meta.boxes[i], cam.tracks[t].box);
            float dx = meta.boxes[i].cx - cam.tracks[t].box.cx;
            float dy = meta.boxes[i].cy - cam.tracks[t].box.cy;
            float dist = std::sqrt(dx * dx + dy * dy);
            float score = iou + (dist < 0.15f ? (0.20f - dist) : 0.0f);

            if (score > best_overlap) {
                best_overlap = score;
                best_t = static_cast<int>(t);
            }
        }

        if (best_t >= 0) {
            cam.tracks[best_t].box = meta.boxes[i];
            cam.tracks[best_t].last_seen = now_t;
            track_matched[best_t] = true;
        } else {
            ActiveTrack new_t = {};
            new_t.track_id = cam.next_track_id++;
            new_t.person_id = 0;
            new_t.sim = 0.0f;
            new_t.color = RGB(160, 160, 160);
            new_t.box = meta.boxes[i];
            new_t.last_seen = now_t;
            cam.tracks.push_back(new_t);
        }
    }

    // Prune tracks unseen for > 2.0s
    cam.tracks.erase(
        std::remove_if(cam.tracks.begin(), cam.tracks.end(),
            [&](const ActiveTrack& trk) {
                return std::chrono::duration<double>(now_t - trk.last_seen).count() > 2.0;
            }),
        cam.tracks.end()
    );
}

bool CameraManager::processVideoChunk(int cam_idx, const uint8_t* buf, int bytes) {
    if (cam_idx < 0 || cam_idx >= MAX_CAMERAS || bytes < static_cast<int>(sizeof(VideoChunkHeader_t))) {
        return false;
    }

    VideoChunkHeader_t hdr;
    std::memcpy(&hdr, buf, sizeof(VideoChunkHeader_t));

    int payload_len = hdr.payload_len;
    if (sizeof(VideoChunkHeader_t) + payload_len > static_cast<size_t>(bytes)) {
        return false;
    }

    auto& cam = m_cams[cam_idx];
    int stride = (hdr.total_chunks <= 150) ? 1400 : 1024;
    int offset = hdr.chunk_idx * stride;

    if (offset + payload_len <= static_cast<int>(cam.raw_frame_rgb.size())) {
        std::memcpy(cam.raw_frame_rgb.data() + offset,
                    buf + sizeof(VideoChunkHeader_t),
                    payload_len);
    }

    if (hdr.frame_id != cam.active_frame_id) {
        cam.active_frame_id = hdr.frame_id;
        cam.chunks_received = 0;
    }
    cam.chunks_received++;

    // Frame complete or last chunk arrived
    if (cam.chunks_received >= hdr.total_chunks || hdr.chunk_idx == hdr.total_chunks - 1) {
        const uint8_t* pRgb = cam.raw_frame_rgb.data();
        for (int i = 0; i < FRAME_W * FRAME_H; ++i) {
            uint8_t r = pRgb[i * 3 + 0];
            uint8_t g = pRgb[i * 3 + 1];
            uint8_t b = pRgb[i * 3 + 2];
            cam.rgb32_buffer[i] = (static_cast<uint32_t>(r) << 16) |
                                  (static_cast<uint32_t>(g) << 8)  |
                                  b;
        }
        cam.current_frame_id = hdr.frame_id;
        cam.has_frame = true;

        cam.frame_count++;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - cam.fps_start).count();
        if (elapsed >= 1.0) {
            cam.stream_fps = cam.frame_count / elapsed;
            cam.frame_count = 0;
            cam.fps_start = now;
        }

        return true;
    }

    return false;
}

CameraFeed& CameraManager::getCamera(int slot_idx) {
    return m_cams[slot_idx];
}

const CameraFeed& CameraManager::getCamera(int slot_idx) const {
    return m_cams[slot_idx];
}

void CameraManager::resetAllTracks() {
    for (int i = 0; i < MAX_CAMERAS; ++i) {
        m_cams[i].tracks.clear();
        m_cams[i].next_track_id = 1;
    }
}
