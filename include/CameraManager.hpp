#pragma once

// CameraManager.hpp
// Manages dual-camera video chunk assembly, OD metadata tracking, and IP registrations.

#include "Protocol.hpp"
#include "Types.hpp"
#include <string>
#include <unordered_map>
#include <vector>

class CameraManager {
public:
    CameraManager();

    // Dynamically maps incoming board IP to Slot 0 (Camera #1) or Slot 1 (Camera #2)
    int getOrRegisterSlot(const std::string& ip);

    // Process incoming OD metadata and update local spatial tracks
    void processOdPacket(int cam_idx, const OdMetadataPacket_t& meta);

    // Process incoming video chunk packet, reassemble, convert to 32-bit XRGB
    bool processVideoChunk(int cam_idx, const uint8_t* buf, int bytes);

    // Camera feed state access
    CameraFeed& getCamera(int slot_idx);
    const CameraFeed& getCamera(int slot_idx) const;

    // Reset tracking state across all cameras
    void resetAllTracks();

private:
    CameraFeed m_cams[MAX_CAMERAS];
    std::unordered_map<std::string, int> m_ip_to_slot;
    int m_num_registered = 0;
};
