#include "ReidMatcher.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

ReidMatcher::ReidMatcher() : m_threshold(kDefaultThreshold) {}

std::vector<float> ReidMatcher::normalizeInt8Embedding(const int8_t* raw, size_t len) {
    if (len == 0 || len > REID_EMBEDDING_DIM) len = REID_EMBEDDING_DIM;
    const uint8_t* u8_raw = reinterpret_cast<const uint8_t*>(raw);
    std::vector<float> vec(len);
    float sum_sq = 0.0f;
    for (size_t i = 0; i < len; ++i) {
        float val = static_cast<float>(u8_raw[i]);
        vec[i] = val;
        sum_sq += val * val;
    }
    float norm = std::sqrt(sum_sq);
    if (norm > 1e-6f) {
        for (size_t i = 0; i < len; ++i) {
            vec[i] /= norm;
        }
    }
    return vec;
}

float ReidMatcher::computeCosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float dot = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
    }
    return dot;
}

float ReidMatcher::computeIoU(const DetectionBox_t& a, const DetectionBox_t& b) {
    float a_x1 = a.cx - a.w * 0.5f;
    float a_y1 = a.cy - a.h * 0.5f;
    float a_x2 = a.cx + a.w * 0.5f;
    float a_y2 = a.cy + a.h * 0.5f;

    float b_x1 = b.cx - b.w * 0.5f;
    float b_y1 = b.cy - b.h * 0.5f;
    float b_x2 = b.cx + b.w * 0.5f;
    float b_y2 = b.cy + b.h * 0.5f;

    float inter_x1 = (std::max)(a_x1, b_x1);
    float inter_y1 = (std::max)(a_y1, b_y1);
    float inter_x2 = (std::min)(a_x2, b_x2);
    float inter_y2 = (std::min)(a_y2, b_y2);

    float inter_w = (std::max)(0.0f, inter_x2 - inter_x1);
    float inter_h = (std::max)(0.0f, inter_y2 - inter_y1);
    float inter_area = inter_w * inter_h;

    float a_area = a.w * a.h;
    float b_area = b.w * b.h;
    float union_area = a_area + b_area - inter_area;

    if (union_area <= 1e-6f) return 0.0f;
    return inter_area / union_area;
}

void ReidMatcher::processPacket(const ReidMetadataPacket_t& reid_pkt,
                                int cam_idx,
                                CameraFeed& cam,
                                std::vector<PersonProfile>& gallery,
                                std::vector<int8_t>& raw_latest_emb,
                                uint32_t& latest_box,
                                uint32_t& latest_frame,
                                int& latest_cam_id,
                                uint32_t& total_reid_count) {
    size_t emb_len = reid_pkt.embedding_len;
    if (emb_len == 0 || emb_len > REID_EMBEDDING_DIM) emb_len = REID_EMBEDDING_DIM;
    std::vector<float> norm_emb = normalizeInt8Embedding(reid_pkt.embedding, emb_len);

    int box_idx = static_cast<int>(reid_pkt.box_index);
    auto now_t = std::chrono::steady_clock::now();

    // 1. Match to local spatial ActiveTrack on this camera by IoU / proximity
    int matched_track_idx = -1;
    float best_match_score = 0.10f;

    for (size_t t = 0; t < cam.tracks.size(); ++t) {
        float iou = computeIoU(reid_pkt.box, cam.tracks[t].box);
        float dx = reid_pkt.box.cx - cam.tracks[t].box.cx;
        float dy = reid_pkt.box.cy - cam.tracks[t].box.cy;
        float dist = std::sqrt(dx * dx + dy * dy);
        float score = iou + (dist < 0.20f ? (0.25f - dist) : 0.0f);
        if (score > best_match_score) {
            best_match_score = score;
            matched_track_idx = static_cast<int>(t);
        }
    }

    if (matched_track_idx < 0) {
        ActiveTrack new_t = {};
        new_t.track_id = cam.next_track_id++;
        new_t.person_id = 0;
        new_t.sim = 0.0f;
        new_t.color = RGB(160, 160, 160);
        new_t.box = reid_pkt.box;
        new_t.last_seen = now_t;
        cam.tracks.push_back(new_t);
        matched_track_idx = static_cast<int>(cam.tracks.size()) - 1;
    }

    ActiveTrack& trk = cam.tracks[matched_track_idx];
    trk.box = reid_pkt.box;
    trk.last_seen = now_t;

    // 2. Match against SHARED GALLERY POOL across BOTH camera streams
    float live_threshold = m_threshold.load();
    int best_gallery_id = -1;
    float best_gallery_sim = -1.0f;
    int best_gallery_idx = -1;

    trk.all_scores.clear();
    std::string scores_log = "[";

    for (size_t i = 0; i < gallery.size(); ++i) {
        float sim = computeCosineSimilarity(gallery[i].feature, norm_emb);
        trk.all_scores.push_back({gallery[i].id, sim});

        char scBuf[32];
        snprintf(scBuf, sizeof(scBuf), "p%d:%d%%%s", gallery[i].id, static_cast<int>(sim * 100.0f), (i + 1 < gallery.size()) ? ", " : "");
        scores_log += scBuf;

        // Intra-camera mutual exclusion: Ensure two tracks on SAME camera don't claim same person
        bool in_use_by_other_on_same_cam = false;
        for (size_t other_t = 0; other_t < cam.tracks.size(); ++other_t) {
            if (static_cast<int>(other_t) != matched_track_idx && cam.tracks[other_t].person_id == gallery[i].id) {
                double age = std::chrono::duration<double>(now_t - cam.tracks[other_t].last_seen).count();
                if (age < 1.0) {
                    in_use_by_other_on_same_cam = true;
                    break;
                }
            }
        }
        if (in_use_by_other_on_same_cam) continue;

        if (sim > best_gallery_sim) {
            best_gallery_sim = sim;
            best_gallery_id = gallery[i].id;
            best_gallery_idx = static_cast<int>(i);
        }
    }
    scores_log += "]";

    if (best_gallery_sim >= live_threshold && best_gallery_idx >= 0) {
        // Identity matched in the unified pool! Update shared template feature (EMA)
        for (size_t i = 0; i < norm_emb.size(); ++i) {
            gallery[best_gallery_idx].feature[i] = 0.85f * gallery[best_gallery_idx].feature[i] + 0.15f * norm_emb[i];
        }
        float s_sq = 0.0f;
        for (float v : gallery[best_gallery_idx].feature) s_sq += v * v;
        float n_val = std::sqrt(s_sq);
        if (n_val > 1e-6f) {
            for (float &v : gallery[best_gallery_idx].feature) v /= n_val;
        }

        gallery[best_gallery_idx].match_count++;
        gallery[best_gallery_idx].last_seen_frame = reid_pkt.frame_id;
        gallery[best_gallery_idx].last_seen_time = now_t;
        gallery[best_gallery_idx].last_cam_id = cam.cam_id;
        gallery[best_gallery_idx].last_similarity = best_gallery_sim;

        trk.person_id = best_gallery_id;
        trk.sim = best_gallery_sim;
        trk.color = gallery[best_gallery_idx].color;

        std::cout << "[CROSS-CAM REID Cam #" << cam.cam_id << " (" << cam.ip << ")] Track #" << trk.track_id
                  << " (crop box " << box_idx << ") | frame #" << reid_pkt.frame_id
                  << " " << scores_log << " ==> [MATCH POOL] Person #" << best_gallery_id
                  << " (sim=" << static_cast<int>(best_gallery_sim * 100.0f) << "% >= "
                  << static_cast<int>(live_threshold * 100.0f) << "%)\n";
    } else {
        // Add new Person identity to shared pool
        int new_id = static_cast<int>(gallery.size()) + 1;
        COLORREF color = ID_COLORS[(new_id - 1) % NUM_ID_COLORS];
        PersonProfile p;
        p.id = new_id;
        p.feature = norm_emb;
        p.color = color;
        p.last_seen_frame = reid_pkt.frame_id;
        p.last_cam_id = cam.cam_id;
        p.match_count = 1;
        p.last_similarity = (best_gallery_sim > 0.0f) ? best_gallery_sim : 1.0f;
        p.last_seen_time = now_t;
        gallery.push_back(p);

        trk.person_id = new_id;
        trk.sim = 1.0f;
        trk.color = color;

        std::cout << "[CROSS-CAM REID Cam #" << cam.cam_id << " (" << cam.ip << ")] Track #" << trk.track_id
                  << " (crop box " << box_idx << ") | frame #" << reid_pkt.frame_id
                  << " " << scores_log << " ==> [NEW POOL PERSON #" << new_id << "] (best_sim="
                  << static_cast<int>(best_gallery_sim * 100.0f) << "% < "
                  << static_cast<int>(live_threshold * 100.0f) << "%)\n";
    }

    raw_latest_emb.assign(reid_pkt.embedding, reid_pkt.embedding + emb_len);
    latest_box = box_idx;
    latest_frame = reid_pkt.frame_id;
    latest_cam_id = cam.cam_id;
    total_reid_count++;
}

float ReidMatcher::getThreshold() const {
    return m_threshold.load();
}

void ReidMatcher::setThreshold(float val) {
    m_threshold.store(std::max(0.40f, std::min(0.98f, val)));
}

void ReidMatcher::resetThreshold() {
    m_threshold.store(kDefaultThreshold);
}

void ReidMatcher::clearGallery(std::vector<PersonProfile>& gallery) {
    gallery.clear();
}
