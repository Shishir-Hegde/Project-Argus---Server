#pragma once

// Protocol.hpp
// Binary protocol definition matching STM32N6 Ethernet streamer byte-for-byte.

#include <cstdint>

#define STREAM_MAGIC          0x54524F4Eu  // "TRON"
#define PKT_TYPE_VIDEO_CHUNK   0x01u
#define PKT_TYPE_OD_METADATA   0x02u
#define PKT_TYPE_REID_METADATA 0x03u

#define LISTEN_PORT            5000
#define OD_MAX_BOXES           10
#define REID_EMBEDDING_DIM     128
#define MAX_CAMERAS            2

// Video & UI Dimensions
#define FRAME_W                256
#define FRAME_H                256
#define DISP_W                 512
#define DISP_H                 512
#define TOTAL_DISP_W           (DISP_W * MAX_CAMERAS) // 1024
#define HEADER_H               36
#define SIDEBAR_W              320
#define TOTAL_W                (TOTAL_DISP_W + SIDEBAR_W) // 1344
#define TOTAL_H                (DISP_H + HEADER_H)        // 548

#pragma pack(push, 1)

// 24 bytes: 5 floats + 2 uint16s
struct DetectionBox_t {
    float    cx;               // Center X (0.0 to 1.0)
    float    cy;               // Center Y (0.0 to 1.0)
    float    w;                // Width    (0.0 to 1.0)
    float    h;                // Height   (0.0 to 1.0)
    float    conf;             // Confidence (0.0 to 1.0)
    uint16_t class_id;         // Object Class
    uint16_t reserved;         // Padding
};

// 0x02 -- OD metadata packet
struct OdMetadataPacket_t {
    uint32_t       magic;         // STREAM_MAGIC (0x54524F4E)
    uint8_t        pkt_type;      // PKT_TYPE_OD_METADATA (2)
    uint8_t        num_boxes;     // Number of detected boxes (0 to 10)
    uint16_t       inference_ms;  // Model inference time in ms
    uint32_t       frame_id;      // Synchronized with video frame_id
    uint16_t       img_width;     // 256
    uint16_t       img_height;    // 256
    DetectionBox_t boxes[OD_MAX_BOXES];
};

// 0x01 -- Video chunk header
struct VideoChunkHeader_t {
    uint32_t magic;           // STREAM_MAGIC
    uint8_t  pkt_type;        // PKT_TYPE_VIDEO_CHUNK (1)
    uint8_t  reserved;
    uint16_t chunk_idx;       // Chunk index (0, 1, ...)
    uint16_t total_chunks;    // Total chunks for this frame
    uint16_t payload_len;     // Length of chunk payload
    uint32_t frame_id;        // Synchronized frame_id
};

// 0x03 -- ReID metadata packet
struct ReidMetadataPacket_t {
    uint32_t       magic;          // STREAM_MAGIC (0x54524F4E)
    uint8_t        pkt_type;       // PKT_TYPE_REID_METADATA (3)
    uint8_t        box_index;      // Index of detection crop
    uint16_t       embedding_len;  // 128
    uint32_t       frame_id;       // Synchronized frame_id
    DetectionBox_t box;            // Bounding box for crop (24 bytes)
    int8_t         embedding[REID_EMBEDDING_DIM]; // 128 bytes INT8
};

#pragma pack(pop)
