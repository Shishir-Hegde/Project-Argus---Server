#pragma once

// ReidMatcher.hpp
// Normalization, Cosine Similarity, IoU math, and Unified Cross-Camera Gallery Matching.

#include "Protocol.hpp"
#include "Types.hpp"
#include <vector>
#include <atomic>

class ReidMatcher {
public:
    // Default threshold is 0.82f (82%) as requested
    static constexpr float kDefaultThreshold = 0.82f;

    ReidMatcher();

    // Feature normalization: clean UINT8 [0..255] directly from firmware
    static std::vector<float> normalizeInt8Embedding(const int8_t* raw, size_t len);

    // Cosine similarity between two unit vectors
    static float computeCosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);

    // Intersection-over-Union between two bounding boxes
    static float computeIoU(const DetectionBox_t& a, const DetectionBox_t& b);

    // Process incoming ReidMetadataPacket_t and match against shared gallery
    void processPacket(const ReidMetadataPacket_t& reid_pkt,
                       int cam_idx,
                       CameraFeed& cam,
                       std::vector<PersonProfile>& gallery,
                       std::vector<int8_t>& raw_latest_emb,
                       uint32_t& latest_box,
                       uint32_t& latest_frame,
                       int& latest_cam_id,
                       uint32_t& total_reid_count);

    // Threshold management
    float getThreshold() const;
    void setThreshold(float val);
    void resetThreshold();

    // Clear shared gallery
    void clearGallery(std::vector<PersonProfile>& gallery);

private:
    std::atomic<float> m_threshold;
};
