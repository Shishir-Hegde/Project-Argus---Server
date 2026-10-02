#include "GuiRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

GuiRenderer::GuiRenderer(CameraManager &camera_mgr, ReidMatcher &matcher,
                         std::vector<PersonProfile> &gallery,
                         std::vector<int8_t> &raw_latest_emb,
                         uint32_t &latest_box, uint32_t &latest_frame,
                         int &latest_cam_id, uint32_t &total_reid_count,
                         CRITICAL_SECTION &cs, std::atomic<bool> &running)
    : m_camera_mgr(camera_mgr), m_matcher(matcher), m_gallery(gallery),
      m_raw_latest_emb(raw_latest_emb), m_latest_box(latest_box),
      m_latest_frame(latest_frame), m_latest_cam_id(latest_cam_id),
      m_total_reid_count(total_reid_count), m_cs(cs), m_running(running) {}

GuiRenderer::~GuiRenderer() {
  if (m_hwnd) {
    DestroyWindow(m_hwnd);
    m_hwnd = NULL;
  }
}

bool GuiRenderer::init(HINSTANCE hInstance, const std::string &title) {
  WNDCLASSEXA wc = {};
  wc.cbSize = sizeof(WNDCLASSEXA);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = "Project_Argus_Server_Viewer_Class";
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

  if (!RegisterClassExA(&wc)) {
    std::cerr << "[!] RegisterClassEx failed\n";
    return false;
  }

  RECT wr = {0, 0, TOTAL_W, TOTAL_H};
  AdjustWindowRect(
      &wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

  m_hwnd = CreateWindowExA(
      0, "Project_Argus_Server_Viewer_Class", title.c_str(),
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
      NULL, NULL, hInstance, this);

  if (!m_hwnd) {
    std::cerr << "[!] CreateWindowEx failed\n";
    return false;
  }

  SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  ShowWindow(m_hwnd, SW_SHOW);
  UpdateWindow(m_hwnd);

  std::cout << "[+] Window created successfully (" << TOTAL_W << "x" << TOTAL_H
            << ").\n";
  std::cout << "    [R]     Reset Shared ReID Gallery & Camera Tracks\n";
  std::cout << "    [0]     Reset Threshold to 82%\n";
  std::cout << "    [ESC/Q] Exit Application\n";

  return true;
}

void GuiRenderer::runMessageLoop() {
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

LRESULT CALLBACK GuiRenderer::WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam) {
  auto *self =
      reinterpret_cast<GuiRenderer *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  switch (msg) {
  case WM_PAINT:
    if (self)
      self->onPaint(hwnd);
    return 0;

  case WM_LBUTTONDOWN:
    if (self)
      self->onLButtonDown(hwnd, LOWORD(lParam), HIWORD(lParam));
    return 0;

  case WM_MOUSEWHEEL:
    if (self)
      self->onMouseWheel(hwnd, GET_WHEEL_DELTA_WPARAM(wParam));
    return 0;

  case WM_KEYDOWN:
    if (self)
      self->onKeyDown(hwnd, wParam);
    return 0;

  case WM_DESTROY:
    if (self)
      self->m_running = false;
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void GuiRenderer::onPaint(HWND hwnd) {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);

  EnterCriticalSection(&m_cs);
  CameraFeed cams_copy[MAX_CAMERAS];
  for (int i = 0; i < MAX_CAMERAS; ++i) {
    cams_copy[i] = m_camera_mgr.getCamera(i);
  }
  std::vector<PersonProfile> gallery_copy = m_gallery;
  std::vector<int8_t> latest_emb = m_raw_latest_emb;
  uint32_t last_box = m_latest_box;
  uint32_t last_reid_frame = m_latest_frame;
  int last_reid_cam = m_latest_cam_id;
  uint32_t total_reid = m_total_reid_count;
  float cur_thresh = m_matcher.getThreshold();
  LeaveCriticalSection(&m_cs);

  auto now = std::chrono::steady_clock::now();

  // Double buffering memory DC
  HDC memDC = CreateCompatibleDC(hdc);
  HBITMAP memBmp = CreateCompatibleBitmap(hdc, TOTAL_W, TOTAL_H);
  HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, memBmp));

  // Clear background
  RECT fullRect = {0, 0, TOTAL_W, TOTAL_H};
  HBRUSH bgBrush = CreateSolidBrush(RGB(15, 17, 23));
  FillRect(memDC, &fullRect, bgBrush);
  DeleteObject(bgBrush);

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = FRAME_W;
  bmi.bmiHeader.biHeight = -FRAME_H; // Top-down
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  // 1. Top Header Banner
  RECT bannerRect = {0, 0, TOTAL_W, HEADER_H};
  HBRUSH bannerBrush = CreateSolidBrush(RGB(22, 27, 34));
  FillRect(memDC, &bannerRect, bannerBrush);
  DeleteObject(bannerBrush);

  HPEN divPen = CreatePen(PS_SOLID, 1, RGB(48, 54, 61));
  HPEN oldPen = static_cast<HPEN>(SelectObject(memDC, divPen));
  MoveToEx(memDC, 0, HEADER_H - 1, NULL);
  LineTo(memDC, TOTAL_W, HEADER_H - 1);

  // Banner Segment 1: CAM - 1 (Left 512px)
  RECT bRect1 = {10, 0, DISP_W - 10, HEADER_H};
  char bText1[128];
  if (cams_copy[0].active) {
    snprintf(bText1, sizeof(bText1), "CAM - 1 [%s] | %.1f FPS",
             cams_copy[0].ip.c_str(), cams_copy[0].stream_fps);
    SetTextColor(memDC, RGB(0, 240, 255));
  } else {
    snprintf(bText1, sizeof(bText1), "CAM - 1 [Awaiting IP Stream...]");
    SetTextColor(memDC, RGB(120, 130, 145));
  }
  SetBkMode(memDC, TRANSPARENT);
  DrawTextA(memDC, bText1, -1, &bRect1, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

  MoveToEx(memDC, DISP_W, 0, NULL);
  LineTo(memDC, DISP_W, HEADER_H);

  // Banner Segment 2: CAM - 2 (Middle 512px)
  RECT bRect2 = {DISP_W + 10, 0, TOTAL_DISP_W - 10, HEADER_H};
  char bText2[128];
  if (cams_copy[1].active) {
    snprintf(bText2, sizeof(bText2), "CAM - 2 [%s] | %.1f FPS",
             cams_copy[1].ip.c_str(), cams_copy[1].stream_fps);
    SetTextColor(memDC, RGB(50, 255, 120));
  } else {
    snprintf(bText2, sizeof(bText2), "CAM - 2 [Standby / Awaiting 2nd IP...]");
    SetTextColor(memDC, RGB(120, 130, 145));
  }
  DrawTextA(memDC, bText2, -1, &bRect2, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

  MoveToEx(memDC, TOTAL_DISP_W, 0, NULL);
  LineTo(memDC, TOTAL_DISP_W, HEADER_H);

  // Banner Segment 3: Shared Pool (Right Sidebar header)
  RECT bRectPool = {TOTAL_DISP_W + 10, 0, TOTAL_W - 10, HEADER_H};
  char bTextPool[128];
  snprintf(bTextPool, sizeof(bTextPool),
           "SHARED POOL | Thresh: %.0f%% | IDs: %u", cur_thresh * 100.0f,
           static_cast<unsigned int>(gallery_copy.size()));
  SetTextColor(memDC, RGB(255, 200, 50));
  DrawTextA(memDC, bTextPool, -1, &bRectPool,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT);

  // 2. Render Both Camera Viewports (Split Screen)
  SetStretchBltMode(memDC, COLORONCOLOR);

  for (int c = 0; c < MAX_CAMERAS; ++c) {
    int view_x = c * DISP_W;
    int view_y = HEADER_H;
    const auto &cam = cams_copy[c];

    if (cam.has_frame && !cam.rgb32_buffer.empty()) {
      StretchDIBits(memDC, view_x, view_y, DISP_W, DISP_H, 0, 0, FRAME_W,
                    FRAME_H, cam.rgb32_buffer.data(), &bmi, DIB_RGB_COLORS,
                    SRCCOPY);

      const auto &meta = cam.latest_meta;
      const auto &trks = cam.tracks;

      std::vector<int> box_to_track(meta.num_boxes, -1);
      std::vector<bool> track_used(trks.size(), false);

      for (uint8_t i = 0; i < meta.num_boxes && i < OD_MAX_BOXES; ++i) {
        int best_t = -1;
        float best_overlap = 0.15f;
        for (size_t t = 0; t < trks.size(); ++t) {
          if (track_used[t])
            continue;
          float iou = ReidMatcher::computeIoU(meta.boxes[i], trks[t].box);
          float dx = meta.boxes[i].cx - trks[t].box.cx;
          float dy = meta.boxes[i].cy - trks[t].box.cy;
          float dist = std::sqrt(dx * dx + dy * dy);
          float score = iou + (dist < 0.15f ? (0.20f - dist) : 0.0f);

          if (score > best_overlap) {
            best_overlap = score;
            best_t = static_cast<int>(t);
          }
        }
        if (best_t >= 0) {
          box_to_track[i] = best_t;
          track_used[best_t] = true;
        }
      }

      for (uint8_t i = 0; i < meta.num_boxes && i < OD_MAX_BOXES; ++i) {
        int cx = view_x + static_cast<int>(meta.boxes[i].cx * DISP_W);
        int cy = view_y + static_cast<int>(meta.boxes[i].cy * DISP_H);
        int bw = static_cast<int>(meta.boxes[i].w * DISP_W);
        int bh = static_cast<int>(meta.boxes[i].h * DISP_H);

        int x1 = (std::max)(view_x, cx - bw / 2);
        int y1 = (std::max)(view_y, cy - bh / 2);
        int x2 = (std::min)(view_x + DISP_W - 1, cx + bw / 2);
        int y2 = (std::min)(view_y + DISP_H - 1, cy + bh / 2);

        int assigned_track = box_to_track[i];
        COLORREF box_color = RGB(160, 160, 160);
        char label[64];

        if (assigned_track >= 0 &&
            assigned_track < static_cast<int>(trks.size())) {
          const auto &trk = trks[assigned_track];
          box_color = trk.color;
          if (trk.person_id > 0) {
            snprintf(label, sizeof(label), " Person - %d (%d%%) ",
                     trk.person_id, static_cast<int>(trk.sim * 100.0f));
          } else {
            snprintf(label, sizeof(label), " Track - %d [Scanning...] ",
                     trk.track_id);
          }
        } else {
          snprintf(label, sizeof(label), " Box - %u ", i);
        }

        HPEN boxPen = CreatePen(PS_SOLID, 3, box_color);
        SelectObject(memDC, boxPen);
        HBRUSH oldBrush = static_cast<HBRUSH>(
            SelectObject(memDC, GetStockObject(HOLLOW_BRUSH)));

        Rectangle(memDC, x1, y1, x2, y2);

        RECT labelRect = {x1, (std::max)(view_y, y1 - 20), x1 + 155, y1};
        HBRUSH labelBg = CreateSolidBrush(box_color);
        FillRect(memDC, &labelRect, labelBg);
        DeleteObject(labelBg);

        SetTextColor(memDC, RGB(0, 0, 0));
        DrawTextA(memDC, label, -1, &labelRect,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT);

        // Corner Badge: Similarity scores against gallery persons (compact smaller font)
        if (assigned_track >= 0 &&
            assigned_track < static_cast<int>(trks.size())) {
          const auto &trk = trks[assigned_track];
          if (!trk.all_scores.empty()) {
            HFONT hSmallFont = CreateFontA(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
            HFONT hPrevFont = static_cast<HFONT>(SelectObject(memDC, hSmallFont));

            int num_scores = static_cast<int>(trk.all_scores.size());
            int badge_w = 58;
            int row_h = 13;
            int badge_h = num_scores * row_h + 4;
            int badge_x =
                (x2 - badge_w - 4 >= x1) ? (x2 - badge_w - 4) : (x1 + 4);
            int badge_y = y1 + 4;

            RECT badgeRect = {badge_x, badge_y, badge_x + badge_w,
                              badge_y + badge_h};
            HBRUSH badgeBg = CreateSolidBrush(RGB(15, 20, 28));
            FillRect(memDC, &badgeRect, badgeBg);
            DeleteObject(badgeBg);

            HPEN badgePen = CreatePen(PS_SOLID, 1, RGB(48, 54, 61));
            SelectObject(memDC, badgePen);
            SelectObject(memDC, GetStockObject(HOLLOW_BRUSH));
            Rectangle(memDC, badgeRect.left, badgeRect.top, badgeRect.right,
                      badgeRect.bottom);
            SelectObject(memDC, oldPen);
            DeleteObject(badgePen);

            int row_y = badge_y + 2;
            for (const auto &ms : trk.all_scores) {
              char scoreStr[24];
              snprintf(scoreStr, sizeof(scoreStr), "p%d: %d%%", ms.person_id,
                       static_cast<int>(ms.sim * 100.0f));
              RECT scoreRowRect = {badge_x + 4, row_y, badge_x + badge_w - 2,
                                   row_y + row_h};
              COLORREF textColor = (ms.sim >= cur_thresh) ? RGB(50, 255, 120)
                                                          : RGB(180, 190, 200);
              SetTextColor(memDC, textColor);
              DrawTextA(memDC, scoreStr, -1, &scoreRowRect,
                        DT_SINGLELINE | DT_LEFT | DT_VCENTER);
              row_y += row_h;
            }

            SelectObject(memDC, hPrevFont);
            DeleteObject(hSmallFont);
          }
        }

        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(boxPen);
      }
    } else {
      RECT standbyRect = {view_x, view_y, view_x + DISP_W, view_y + DISP_H};
      HBRUSH standbyBrush = CreateSolidBrush(RGB(18, 21, 28));
      FillRect(memDC, &standbyRect, standbyBrush);
      DeleteObject(standbyBrush);

      HPEN gridPen = CreatePen(PS_DOT, 1, RGB(35, 42, 54));
      SelectObject(memDC, gridPen);
      MoveToEx(memDC, view_x, view_y + DISP_H / 2, NULL);
      LineTo(memDC, view_x + DISP_W, view_y + DISP_H / 2);
      MoveToEx(memDC, view_x + DISP_W / 2, view_y, NULL);
      LineTo(memDC, view_x + DISP_W / 2, view_y + DISP_H);
      SelectObject(memDC, oldPen);
      DeleteObject(gridPen);

      char standbyText[256];
      if (cam.active) {
        snprintf(standbyText, sizeof(standbyText),
                 "[ CAM - %d CONNECTED ]\nIP: %s\nReceiving initial video "
                 "chunks...",
                 c + 1, cam.ip.c_str());
      } else {
        snprintf(standbyText, sizeof(standbyText),
                 "[ CAM - %d STANDBY ]\nListening on UDP Port %d\nAwaiting "
                 "stream from STM32 IP...",
                 c + 1, LISTEN_PORT);
      }

      RECT txtRect = {view_x + 20, view_y + DISP_H / 2 - 40,
                      view_x + DISP_W - 20, view_y + DISP_H / 2 + 50};
      SetTextColor(memDC, RGB(110, 120, 135));
      DrawTextA(memDC, standbyText, -1, &txtRect, DT_CENTER);
    }

    if (c == 0) {
      MoveToEx(memDC, DISP_W, HEADER_H, NULL);
      LineTo(memDC, DISP_W, TOTAL_H);
    }
  }

  // 3. Right Sidebar (X = 1024 to 1344)
  int sbX = TOTAL_DISP_W;
  RECT sidebarRect = {sbX, HEADER_H, TOTAL_W, TOTAL_H};
  HBRUSH sideBrush = CreateSolidBrush(RGB(18, 22, 30));
  FillRect(memDC, &sidebarRect, sideBrush);
  DeleteObject(sideBrush);

  MoveToEx(memDC, sbX, HEADER_H, NULL);
  LineTo(memDC, sbX, TOTAL_H);

  // Sidebar Section 1: Shared Gallery
  RECT titleRect = {sbX + 12, HEADER_H + 10, TOTAL_W - 12, HEADER_H + 28};
  SetTextColor(memDC, RGB(255, 255, 255));
  DrawTextA(memDC, "SHARED REID GALLERY (BOTH CAMS)", -1, &titleRect,
            DT_SINGLELINE | DT_LEFT);

  MoveToEx(memDC, sbX + 10, HEADER_H + 32, NULL);
  LineTo(memDC, TOTAL_W - 10, HEADER_H + 32);

  int cardY = HEADER_H + 38;
  for (size_t i = 0; i < gallery_copy.size() && i < 3; ++i) {
    const auto &p = gallery_copy[i];
    double age = std::chrono::duration<double>(now - p.last_seen_time).count();
    bool is_active = (age < 3.0);

    RECT cardRect = {sbX + 12, cardY, TOTAL_W - 12, cardY + 44};
    HBRUSH cardBg =
        CreateSolidBrush(is_active ? RGB(26, 33, 44) : RGB(22, 27, 34));
    FillRect(memDC, &cardRect, cardBg);
    DeleteObject(cardBg);

    HPEN borderPen =
        CreatePen(PS_SOLID, 1, is_active ? p.color : RGB(48, 54, 61));
    SelectObject(memDC, borderPen);
    SelectObject(memDC, GetStockObject(HOLLOW_BRUSH));
    Rectangle(memDC, cardRect.left, cardRect.top, cardRect.right,
              cardRect.bottom);
    SelectObject(memDC, oldPen);
    DeleteObject(borderPen);

    RECT pillRect = {cardRect.left + 8, cardRect.top + 6, cardRect.left + 14,
                     cardRect.bottom - 6};
    HBRUSH pillBrush = CreateSolidBrush(p.color);
    FillRect(memDC, &pillRect, pillBrush);
    DeleteObject(pillBrush);

    char idHeader[64];
    snprintf(idHeader, sizeof(idHeader), "Person - %d  (CAM - %d)", p.id, p.last_cam_id);
    RECT idTextRect = {cardRect.left + 22, cardRect.top + 4, cardRect.right - 8,
                       cardRect.top + 20};
    SetTextColor(memDC, p.color);
    DrawTextA(memDC, idHeader, -1, &idTextRect, DT_SINGLELINE | DT_LEFT);

    char statsText[128];
    snprintf(statsText, sizeof(statsText), "Sim: %d%%  |  %d matches",
             static_cast<int>(p.last_similarity * 100.0f), p.match_count);
    RECT statsRect = {cardRect.left + 22, cardRect.top + 22, cardRect.right - 8,
                      cardRect.bottom - 4};
    SetTextColor(memDC, RGB(139, 148, 158));
    DrawTextA(memDC, statsText, -1, &statsRect, DT_SINGLELINE | DT_LEFT);

    cardY += 48;
  }

  if (gallery_copy.empty()) {
    RECT noGalleryRect = {sbX + 12, cardY + 4, TOTAL_W - 12, cardY + 28};
    SetTextColor(memDC, RGB(110, 118, 129));
    DrawTextA(memDC, "Awaiting person detections from IP streams...", -1,
              &noGalleryRect, DT_LEFT);
    cardY += 32;
  } else if (gallery_copy.size() > 3) {
    char moreTxt[64];
    snprintf(moreTxt, sizeof(moreTxt),
             "+%u more identities in shared gallery pool",
             static_cast<unsigned int>(gallery_copy.size() - 3));
    RECT moreRect = {sbX + 12, cardY + 2, TOTAL_W - 12, cardY + 18};
    SetTextColor(memDC, RGB(0, 180, 216));
    DrawTextA(memDC, moreTxt, -1, &moreRect, DT_LEFT);
    cardY += 22;
  }

  // Sidebar Section 2: Interactive Threshold Controls (Default 82%)
  int ctrlY = (std::max)(cardY + 8, HEADER_H + 180);
  RECT ctrlCard = {sbX + 12, ctrlY, TOTAL_W - 12, ctrlY + 100};
  HBRUSH ctrlBg = CreateSolidBrush(RGB(22, 27, 34));
  FillRect(memDC, &ctrlCard, ctrlBg);
  DeleteObject(ctrlBg);

  HPEN ctrlBorder = CreatePen(PS_SOLID, 1, RGB(0, 180, 216));
  SelectObject(memDC, ctrlBorder);
  SelectObject(memDC, GetStockObject(HOLLOW_BRUSH));
  Rectangle(memDC, ctrlCard.left, ctrlCard.top, ctrlCard.right,
            ctrlCard.bottom);
  SelectObject(memDC, oldPen);
  DeleteObject(ctrlBorder);

  char threshTitle[128];
  snprintf(threshTitle, sizeof(threshTitle), "MATCH THRESHOLD: %d%%",
           static_cast<int>(cur_thresh * 100.0f));
  RECT threshTitleRect = {ctrlCard.left + 10, ctrlY + 8, ctrlCard.right - 10,
                          ctrlY + 26};
  SetTextColor(memDC, RGB(0, 240, 255));
  DrawTextA(memDC, threshTitle, -1, &threshTitleRect, DT_SINGLELINE | DT_LEFT);

  RECT hintRect = {ctrlCard.left + 10, ctrlY + 24, ctrlCard.right - 10,
                   ctrlY + 38};
  SetTextColor(memDC, RGB(139, 148, 158));
  DrawTextA(memDC, "Scroll wheel / click buttons / drag", -1, &hintRect,
            DT_SINGLELINE | DT_LEFT);

  // [-] Button
  RECT btnMinus = {ctrlCard.left + 10, ctrlY + 42, ctrlCard.left + 42,
                   ctrlY + 66};
  HBRUSH btnMinusBg = CreateSolidBrush(RGB(35, 42, 54));
  FillRect(memDC, &btnMinus, btnMinusBg);
  DeleteObject(btnMinusBg);
  HPEN btnPen = CreatePen(PS_SOLID, 1, RGB(68, 76, 86));
  SelectObject(memDC, btnPen);
  SelectObject(memDC, GetStockObject(HOLLOW_BRUSH));
  Rectangle(memDC, btnMinus.left, btnMinus.top, btnMinus.right,
            btnMinus.bottom);
  SetTextColor(memDC, RGB(255, 255, 255));
  DrawTextA(memDC, "-", -1, &btnMinus, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

  // Slider Track
  int trackLeft = ctrlCard.left + 50;
  int trackRight = ctrlCard.right - 50;
  int trackWidth = trackRight - trackLeft;
  RECT sliderTrack = {trackLeft, ctrlY + 50, trackRight, ctrlY + 58};
  HBRUSH trackBg = CreateSolidBrush(RGB(40, 48, 60));
  FillRect(memDC, &sliderTrack, trackBg);
  DeleteObject(trackBg);

  float fillRatio = (cur_thresh - 0.40f) / (0.95f - 0.40f);
  if (fillRatio < 0.0f)
    fillRatio = 0.0f;
  if (fillRatio > 1.0f)
    fillRatio = 1.0f;
  int fillW = static_cast<int>(fillRatio * trackWidth);
  RECT sliderFill = {trackLeft, ctrlY + 50, trackLeft + fillW, ctrlY + 58};
  HBRUSH fillBg = CreateSolidBrush(RGB(0, 200, 240));
  FillRect(memDC, &sliderFill, fillBg);
  DeleteObject(fillBg);

  RECT knob = {trackLeft + fillW - 4, ctrlY + 46, trackLeft + fillW + 4,
               ctrlY + 62};
  HBRUSH knobBrush = CreateSolidBrush(RGB(255, 255, 255));
  FillRect(memDC, &knob, knobBrush);
  DeleteObject(knobBrush);

  // [+] Button
  RECT btnPlus = {ctrlCard.right - 42, ctrlY + 42, ctrlCard.right - 10,
                  ctrlY + 66};
  HBRUSH btnPlusBg = CreateSolidBrush(RGB(35, 42, 54));
  FillRect(memDC, &btnPlus, btnPlusBg);
  DeleteObject(btnPlusBg);
  Rectangle(memDC, btnPlus.left, btnPlus.top, btnPlus.right, btnPlus.bottom);
  SetTextColor(memDC, RGB(255, 255, 255));
  DrawTextA(memDC, "+", -1, &btnPlus, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
  SelectObject(memDC, oldPen);
  DeleteObject(btnPen);

  // Preset Buttons: [60%] [70%] [75%] [80%] [82%] [88%]
  const int presets[] = {60, 70, 75, 80, 82, 88};
  int pStartX = ctrlCard.left + 10;
  int pW = 42;
  int pGap = 5;
  for (int pi = 0; pi < 6; ++pi) {
    RECT pRect = {pStartX + pi * (pW + pGap), ctrlY + 72,
                  pStartX + pi * (pW + pGap) + pW, ctrlY + 92};
    bool is_selected = (std::abs(cur_thresh * 100.0f - presets[pi]) < 1.0f);
    HBRUSH pBg =
        CreateSolidBrush(is_selected ? RGB(0, 119, 182) : RGB(30, 36, 46));
    FillRect(memDC, &pRect, pBg);
    DeleteObject(pBg);
    char pTxt[16];
    snprintf(pTxt, sizeof(pTxt), "%d%%", presets[pi]);
    SetTextColor(memDC, is_selected ? RGB(255, 255, 255) : RGB(170, 180, 195));
    DrawTextA(memDC, pTxt, -1, &pRect, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
  }

  // Sidebar Section 3: Live 128-Byte Embedding Inspector
  int embY = ctrlY + 108;
  RECT embTitleRect = {sbX + 12, embY, TOTAL_W - 12, embY + 18};
  SetTextColor(memDC, RGB(0, 240, 255));
  char embHeader[64];
  snprintf(embHeader, sizeof(embHeader), "LATEST EMBEDDING (CAM - %d, Box - %u)",
           last_reid_cam, last_box);
  DrawTextA(memDC, embHeader, -1, &embTitleRect, DT_SINGLELINE | DT_LEFT);

  MoveToEx(memDC, sbX + 10, embY + 22, NULL);
  LineTo(memDC, TOTAL_W - 10, embY + 22);

  int rowY = embY + 26;
  for (int row = 0; row < 5 && row * 16 < REID_EMBEDDING_DIM; ++row) {
    char rowStr[128] = {};
    int offset = 0;
    offset += snprintf(rowStr + offset, sizeof(rowStr) - offset,
                       "[%02d..%02d] ", row * 16, row * 16 + 15);
    for (int col = 0; col < 8; ++col) {
      int idx = row * 16 + col;
      offset += snprintf(rowStr + offset, sizeof(rowStr) - offset, "%4d",
                         static_cast<int>(latest_emb[idx]));
    }
    RECT rowRect = {sbX + 12, rowY, TOTAL_W - 12, rowY + 14};
    SetTextColor(memDC, RGB(180, 190, 205));
    DrawTextA(memDC, rowStr, -1, &rowRect, DT_SINGLELINE | DT_LEFT);
    rowY += 14;
  }

  // Section 4: Hotkeys Diagnostics footer
  RECT diagRect = {sbX + 12, TOTAL_H - 46, TOTAL_W - 12, TOTAL_H - 6};
  HBRUSH diagBg = CreateSolidBrush(RGB(13, 17, 23));
  FillRect(memDC, &diagRect, diagBg);
  DeleteObject(diagBg);

  char diagStr[128];
  snprintf(diagStr, sizeof(diagStr),
           "[UP/DN/Wheel] Thresh  [0] Reset (82%%)\n[R] Reset Shared Pool "
           "[ESC/Q] Exit");
  SetTextColor(memDC, RGB(110, 118, 129));
  DrawTextA(memDC, diagStr, -1, &diagRect, DT_LEFT);

  SelectObject(memDC, oldPen);
  DeleteObject(divPen);

  BitBlt(hdc, 0, 0, TOTAL_W, TOTAL_H, memDC, 0, 0, SRCCOPY);

  SelectObject(memDC, oldBmp);
  DeleteObject(memBmp);
  DeleteDC(memDC);

  EndPaint(hwnd, &ps);
}

void GuiRenderer::onLButtonDown(HWND hwnd, int mx, int my) {
  int sbX = TOTAL_DISP_W;

  if (mx >= sbX + 12 && mx <= TOTAL_W - 12 && my >= 180 && my <= 320) {
    float cur = m_matcher.getThreshold();

    // 1. [-] Button
    if (mx >= sbX + 22 && mx <= sbX + 54 && my >= 220 && my <= 250) {
      cur -= 0.02f;
    }
    // 2. [+] Button
    else if (mx >= TOTAL_W - 54 && mx <= TOTAL_W - 22 && my >= 220 &&
             my <= 250) {
      cur += 0.02f;
    }
    // 3. Slider Track
    else if (mx >= sbX + 62 && mx <= TOTAL_W - 62 && my >= 220 && my <= 250) {
      float ratio = static_cast<float>(mx - (sbX + 62)) /
                    static_cast<float>((TOTAL_W - 62) - (sbX + 62));
      cur = 0.40f + ratio * (0.95f - 0.40f);
    }
    // 4. Presets: [60%] [70%] [75%] [80%] [82%] [88%]
    else if (my >= 252 && my <= 276) {
      int pStartX = sbX + 22;
      int pW = 42;
      int pGap = 5;
      const float preset_vals[] = {0.60f, 0.70f, 0.75f, 0.80f, 0.82f, 0.88f};
      for (int pi = 0; pi < 6; ++pi) {
        int bx = pStartX + pi * (pW + pGap);
        if (mx >= bx && mx <= bx + pW) {
          cur = preset_vals[pi];
          break;
        }
      }
    }

    m_matcher.setThreshold(cur);
    std::cout << "[CONFIG] ReID Threshold changed to "
              << static_cast<int>(cur * 100.0f) << "%\n";
    InvalidateRect(hwnd, NULL, FALSE);
  }
}

void GuiRenderer::onMouseWheel(HWND hwnd, short delta) {
  float cur = m_matcher.getThreshold();
  if (delta > 0)
    cur += 0.01f;
  else if (delta < 0)
    cur -= 0.01f;
  m_matcher.setThreshold(cur);
  std::cout << "[CONFIG] ReID Threshold changed to "
            << static_cast<int>(cur * 100.0f) << "%\n";
  InvalidateRect(hwnd, NULL, FALSE);
}

void GuiRenderer::onKeyDown(HWND hwnd, WPARAM wParam) {
  if (wParam == VK_UP || wParam == VK_RIGHT || wParam == VK_OEM_PLUS ||
      wParam == 0xBB) {
    float cur = m_matcher.getThreshold() + 0.01f;
    m_matcher.setThreshold(cur);
    std::cout << "[CONFIG] ReID Threshold set to "
              << static_cast<int>(cur * 100.0f) << "%\n";
    InvalidateRect(hwnd, NULL, FALSE);
  } else if (wParam == VK_DOWN || wParam == VK_LEFT || wParam == VK_OEM_MINUS ||
             wParam == 0xBD) {
    float cur = m_matcher.getThreshold() - 0.01f;
    m_matcher.setThreshold(cur);
    std::cout << "[CONFIG] ReID Threshold set to "
              << static_cast<int>(cur * 100.0f) << "%\n";
    InvalidateRect(hwnd, NULL, FALSE);
  } else if (wParam == VK_PRIOR) {
    float cur = m_matcher.getThreshold() + 0.05f;
    m_matcher.setThreshold(cur);
    std::cout << "[CONFIG] ReID Threshold set to "
              << static_cast<int>(cur * 100.0f) << "%\n";
    InvalidateRect(hwnd, NULL, FALSE);
  } else if (wParam == VK_NEXT) {
    float cur = m_matcher.getThreshold() - 0.05f;
    m_matcher.setThreshold(cur);
    std::cout << "[CONFIG] ReID Threshold set to "
              << static_cast<int>(cur * 100.0f) << "%\n";
    InvalidateRect(hwnd, NULL, FALSE);
  } else if (wParam == '0') {
    m_matcher.resetThreshold(); // Resets to 0.82f (82%)!
    std::cout << "[CONFIG] ReID Threshold reset to default 82%\n";
    InvalidateRect(hwnd, NULL, FALSE);
  } else if (wParam == 'R' || wParam == 'r') {
    EnterCriticalSection(&m_cs);
    m_gallery.clear();
    m_camera_mgr.resetAllTracks();
    LeaveCriticalSection(&m_cs);
    std::cout << "[*] Shared ReID Gallery & All Camera Tracks Reset.\n";
    InvalidateRect(hwnd, NULL, FALSE);
  } else if (wParam == VK_ESCAPE || wParam == 'Q' || wParam == 'q') {
    m_running = false;
    PostQuitMessage(0);
  }
}
