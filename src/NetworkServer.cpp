#include "NetworkServer.hpp"
#include <iostream>
#include <vector>

NetworkServer::NetworkServer(CameraManager& camera_mgr,
                             ReidMatcher& matcher,
                             std::vector<PersonProfile>& gallery,
                             std::vector<int8_t>& raw_latest_emb,
                             uint32_t& latest_box,
                             uint32_t& latest_frame,
                             int& latest_cam_id,
                             uint32_t& total_reid_count,
                             CRITICAL_SECTION& cs,
                             HWND& hwnd,
                             std::atomic<bool>& running)
    : m_camera_mgr(camera_mgr),
      m_matcher(matcher),
      m_gallery(gallery),
      m_raw_latest_emb(raw_latest_emb),
      m_latest_box(latest_box),
      m_latest_frame(latest_frame),
      m_latest_cam_id(latest_cam_id),
      m_total_reid_count(total_reid_count),
      m_cs(cs),
      m_hwnd(hwnd),
      m_running(running) {}

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::start(uint16_t port) {
    m_port = port;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[!] WSAStartup failed\n";
        return false;
    }

    m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_sock == INVALID_SOCKET) {
        std::cerr << "[!] Socket creation failed\n";
        WSACleanup();
        return false;
    }

    int rcvbuf = 8 * 1024 * 1024; // 8MB buffer for multi-camera streaming
    setsockopt(m_sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rcvbuf), sizeof(rcvbuf));

    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(m_port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m_sock, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "[!] Bind failed on port " << m_port << "\n";
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    std::cout << "=========================================================\n";
    std::cout << "  Project Argus Dual-Camera Split-Screen ReID Server     \n";
    std::cout << "=========================================================\n";
    std::cout << "[+] UDP Server listening on port " << m_port << "...\n";
    std::cout << "[+] Shared ReID Matching Threshold: " << static_cast<int>(m_matcher.getThreshold() * 100.0f) << "%\n";
    std::cout << "    Split screen active: Left=Cam#1, Right=Cam#2, Shared ReID Pool\n";

    m_hThread = CreateThread(NULL, 0, ThreadEntry, this, 0, NULL);
    if (!m_hThread) {
        std::cerr << "[!] Failed to start network thread\n";
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    return true;
}

void NetworkServer::stop() {
    if (m_sock != INVALID_SOCKET) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }

    if (m_hThread != NULL) {
        WaitForSingleObject(m_hThread, 1000);
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }

    WSACleanup();
}

DWORD WINAPI NetworkServer::ThreadEntry(LPVOID lpParam) {
    auto* self = reinterpret_cast<NetworkServer*>(lpParam);
    self->run();
    return 0;
}

void NetworkServer::run() {
    std::vector<uint8_t> recv_buf(2048);

    while (m_running.load()) {
        sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        int bytes = recvfrom(m_sock, reinterpret_cast<char*>(recv_buf.data()), static_cast<int>(recv_buf.size()), 0,
                             reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (bytes < 4) continue;

        uint32_t magic;
        memcpy(&magic, recv_buf.data(), sizeof(uint32_t));
        if (magic != STREAM_MAGIC) continue;

        std::string client_ip_str = inet_ntoa(client_addr.sin_addr);
        int cam_idx = m_camera_mgr.getOrRegisterSlot(client_ip_str);
        if (cam_idx < 0 || cam_idx >= MAX_CAMERAS) continue;

        uint8_t pkt_type = recv_buf[4];
        auto now_t = std::chrono::steady_clock::now();
        m_camera_mgr.getCamera(cam_idx).last_packet_time = now_t;

        // 1. ReID Metadata Packet
        if (pkt_type == PKT_TYPE_REID_METADATA &&
            bytes >= static_cast<int>(sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint32_t))) {
            ReidMetadataPacket_t reid_pkt = {};
            memcpy(&reid_pkt, recv_buf.data(), std::min(static_cast<size_t>(bytes), sizeof(ReidMetadataPacket_t)));

            EnterCriticalSection(&m_cs);
            m_matcher.processPacket(reid_pkt,
                                   cam_idx,
                                   m_camera_mgr.getCamera(cam_idx),
                                   m_gallery,
                                   m_raw_latest_emb,
                                   m_latest_box,
                                   m_latest_frame,
                                   m_latest_cam_id,
                                   m_total_reid_count);
            LeaveCriticalSection(&m_cs);

            if (m_hwnd) {
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
        }
        // 2. OD Metadata Packet
        else if (pkt_type == PKT_TYPE_OD_METADATA && bytes >= static_cast<int>(sizeof(OdMetadataPacket_t))) {
            OdMetadataPacket_t meta;
            memcpy(&meta, recv_buf.data(), sizeof(OdMetadataPacket_t));

            EnterCriticalSection(&m_cs);
            m_camera_mgr.processOdPacket(cam_idx, meta);
            LeaveCriticalSection(&m_cs);
        }
        // 3. Video Chunk Packet
        else if (pkt_type == PKT_TYPE_VIDEO_CHUNK && bytes >= static_cast<int>(sizeof(VideoChunkHeader_t))) {
            EnterCriticalSection(&m_cs);
            bool frame_complete = m_camera_mgr.processVideoChunk(cam_idx, recv_buf.data(), bytes);
            LeaveCriticalSection(&m_cs);

            if (frame_complete && m_hwnd) {
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
        }
    }
}
