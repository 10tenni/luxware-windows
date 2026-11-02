#include "engine.hpp"
#include "update.hpp"
#include <iomanip>
#include <sstream>

using namespace lux;
enum {
  SOURCE = 100,
  REFRESH,
  SCAN,
  STOP,
  VIEW,
  MODE,
  BIND,
  AIM,
  ARM,
  SINGLE,
  RESET,
  LEVEL,
  SETLEVEL,
  SELF,
  IMPORT,
  SETTINGS,
  CLICKS,
  BUTTON,
  CPS,
  DUTY,
  GPU,
  BUNDLED,
  DONE,
  UPDATES,
  CHECKUPDATE,
  AUTOUPDATE,
  GAMEFRIENDLY,
  SLIDER = 300
};
constexpr UINT APP_BIND = WM_APP + 1, APP_SAVE = WM_APP + 2;
Shared state;
Updates updates;
struct UI {
  HWND window = nullptr, settings = nullptr, source = nullptr, view = nullptr, mode = nullptr,
       button = nullptr, cps = nullptr, duty = nullptr;
  HFONT font = nullptr, big = nullptr, mono = nullptr;
  HBRUSH background = nullptr, editBrush = nullptr;
  PaintBuffer canvas;
  double nextVisual = 0;
  std::vector<HWND> controls, sliders;
  std::vector<HWND> windows;
  RECT preview{};
  double scale = 1;
  bool recording = false, marking = false, dragging = false, raw = false;
  double recordUntil = 0, messageUntil = 0, updateAfter = 0;
  POINT dragStart{}, dragEnd{}, lastPhysical{};
  bool lastPhysicalValid = false;
  std::shared_ptr<Image> frozen;
  std::string message;
  HHOOK keyboardHook = nullptr, mouseHook = nullptr;
  std::array<bool, 256> keys{};
  bool bindingDown = false;
  int physicalMods = 0;
  std::thread work, input;
  bool testing = false;
} ui;
int dip(int x) {
  return int(std::lround(x * ui.scale));
}
std::wstring number(double v, int digits = 2) {
  std::wostringstream s;
  s << std::fixed << std::setprecision(digits) << v;
  return s.str();
}
void status(const std::string &text) {
  ui.message = text;
  ui.messageUntil = now() + 8;
  if (ui.window)
    InvalidateRect(ui.window, nullptr, FALSE);
}
void save() {
  saveSettings(state.config());
}
void resetSelection() {
  state.reset++;
  state.clearTarget();
}
void stop() {
  state.stop();
  ui.marking = ui.dragging = false;
  ui.frozen.reset();
  status("Stopped");
}
std::wstring bindingName(const Settings &s) {
  std::wstring name;
  if (s.bindMods & 1)
    name += L"Ctrl+";
  if (s.bindMods & 2)
    name += L"Shift+";
  if (s.bindMods & 4)
    name += L"Alt+";
  if (s.bindMods & 8)
    name += L"Win+";
  if (s.bindKind == 1) {
    static const wchar_t *names[] = {L"Left mouse", L"Right mouse", L"Middle mouse", L"Mouse 4", L"Mouse 5"};
    name += names[std::clamp(s.bindCode, 0, 4)];
  } else if (s.bindCode >= VK_F1 && s.bindCode <= VK_F24)
    name += L"F" + std::to_wstring(s.bindCode - VK_F1 + 1);
  else {
    wchar_t text[80]{};
    LONG code = LONG(MapVirtualKeyW(s.bindCode, MAPVK_VK_TO_VSC) << 16);
    if (s.bindCode == VK_LEFT || s.bindCode == VK_RIGHT || s.bindCode == VK_UP || s.bindCode == VK_DOWN ||
        s.bindCode == VK_INSERT || s.bindCode == VK_DELETE)
      code |= 1 << 24;
    if (GetKeyNameTextW(code, text, 80))
      name += text;
    else
      name += L"Key " + std::to_wstring(s.bindCode);
  }
  return name;
}
void applyEdits() {
  auto read = [](HWND w, double old, double lo, double hi) {
    wchar_t b[80]{};
    GetWindowTextW(w, b, 80);
    std::wstring s = b;
    std::replace(s.begin(), s.end(), L',', L'.');
    try {
      size_t n = 0;
      double v = std::stod(s, &n);
      if (n != s.size() || !std::isfinite(v))
        return old;
      return std::clamp(v, lo, hi);
    } catch (...) {
      return old;
    }
  };
  auto s = state.config();
  s.cps = read(ui.cps, s.cps, .1, 200);
  s.duty = read(ui.duty, s.duty * 100, 1, 99) / 100;
  {
    std::lock_guard<std::mutex> l(state.mutex);
    state.settings = s;
  }
  SetWindowTextW(ui.cps, number(s.cps).c_str());
  SetWindowTextW(ui.duty, number(s.duty * 100).c_str());
  save();
}
void arm() {
  if (state.armed) {
    state.armed = false;
    status("Input stopped");
    return;
  }
  applyEdits();
  auto s = state.config();
  if (!state.scan) {
    status("Start scan first");
    return;
  }
  if (s.aim && !state.modelReady) {
    status("Aim cannot start: the model is not ready. See model status below.");
    return;
  }
  bool hasOwn;
  {
    std::lock_guard<std::mutex> l(state.mutex);
    hasOwn = !state.own.empty();
  }
  if (s.aim && !hasOwn) {
    status("Mark your own avatar once in third person before enabling aim");
    return;
  }
  if (ui.marking) {
    status("Finish or cancel the avatar selection first");
    return;
  }
  state.armed = true;
  if (s.camera && s.level)
    state.calibrate++;
  status("Input armed: focus the selected game window");
}
void captureBinding(int kind, int code) {
  if (reservedBinding(kind, code, ui.physicalMods)) {
    status("That chord is reserved for Reset, Set level or Stop. Choose another binding.");
    return;
  }
  auto s = state.config();
  s.bindKind = kind;
  s.bindCode = code;
  s.bindMods = ui.physicalMods;
  {
    std::lock_guard<std::mutex> l(state.mutex);
    state.settings = s;
  }
  ui.recording = false;
  ui.bindingDown = false;
  state.bindingHeld = false;
  PostMessageW(ui.window, APP_SAVE, 0, 0);
  status("Binding saved. Release it, then activate in the selected game.");
}
int modifier(int vk) {
  if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL)
    return 1;
  if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT)
    return 2;
  if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU)
    return 4;
  if (vk == VK_LWIN || vk == VK_RWIN)
    return 8;
  return 0;
}
LRESULT CALLBACK keyboardHook(int n, WPARAM message, LPARAM data) {
  if (n >= 0) {
    auto k = reinterpret_cast<KBDLLHOOKSTRUCT *>(data);
    bool injected = (k->flags & LLKHF_INJECTED) || k->dwExtraInfo == eventTag;
    if (!injected && k->vkCode < 256) {
      bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
      int mod = modifier(k->vkCode);
      bool repeated = ui.keys[k->vkCode];
      ui.keys[k->vkCode] = down;
      if (mod) {
        ui.physicalMods = ((ui.keys[VK_CONTROL] || ui.keys[VK_LCONTROL] || ui.keys[VK_RCONTROL]) ? 1 : 0) |
                          ((ui.keys[VK_SHIFT] || ui.keys[VK_LSHIFT] || ui.keys[VK_RSHIFT]) ? 2 : 0) |
                          ((ui.keys[VK_MENU] || ui.keys[VK_LMENU] || ui.keys[VK_RMENU]) ? 4 : 0) |
                          ((ui.keys[VK_LWIN] || ui.keys[VK_RWIN]) ? 8 : 0);
        auto binding = state.config();
        if (!down && binding.hold && ui.bindingDown &&
            (ui.physicalMods & binding.bindMods) != binding.bindMods) {
          ui.bindingDown = false;
          state.bindingHeld = false;
          state.armed = false;
        }
      }
      if (down && !repeated && k->vkCode == VK_ESCAPE && (ui.physicalMods & 5) == 5) {
        stop();
        ui.recording = false;
        return CallNextHookEx(nullptr, n, message, data);
      }
      if (ui.recording && GetForegroundWindow() == ui.window && down && !mod && !repeated) {
        if (k->vkCode == VK_ESCAPE) {
          ui.recording = false;
          status("Binding cancelled");
        } else
          captureBinding(0, k->vkCode);
        return 1;
      }
      if (!ui.recording && down && !repeated && (ui.physicalMods & 3) == 3 && k->vkCode == 'R') {
        state.armed = false;
        resetSelection();
        status("Target reset: activate again to choose a new body");
      } else if (!ui.recording && down && !repeated && (ui.physicalMods & 3) == 3 && k->vkCode == 'L') {
        state.calibrate++;
        status("Current view is the level reference");
      } else {
        auto s = state.config();
        if (!ui.recording && s.bindKind == 0 && int(k->vkCode) == s.bindCode) {
          if (!down && ui.bindingDown) {
            ui.bindingDown = false;
            state.bindingHeld = false;
            if (s.hold)
              state.armed = false;
          } else if (down && !repeated && !ui.bindingDown && ui.physicalMods == s.bindMods &&
                     front(state.source)) {
            ui.bindingDown = true;
            state.bindingHeld = true;
            PostMessageW(ui.window, APP_BIND, 1, 0);
          }
        }
      }
    }
  }
  return CallNextHookEx(nullptr, n, message, data);
}
LRESULT CALLBACK mouseHook(int n, WPARAM message, LPARAM data) {
  if (n >= 0) {
    auto m = reinterpret_cast<MSLLHOOKSTRUCT *>(data);
    if (!(m->flags & LLMHF_INJECTED) && m->dwExtraInfo != eventTag) {
      int button = -1;
      bool down = false;
      if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
        button = 0;
        down = message == WM_LBUTTONDOWN;
      }
      if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) {
        button = 1;
        down = message == WM_RBUTTONDOWN;
      }
      if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP) {
        button = 2;
        down = message == WM_MBUTTONDOWN;
      }
      if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP) {
        button = HIWORD(m->mouseData) == XBUTTON1 ? 3 : 4;
        down = message == WM_XBUTTONDOWN;
      }
      if (ui.recording && GetForegroundWindow() == ui.window && button >= 0 && down) {
        captureBinding(1, button);
        return 1;
      }
      auto s = state.config();
      if (!ui.recording && s.bindKind == 1 && button == s.bindCode) {
        if (!down && ui.bindingDown) {
          ui.bindingDown = false;
          state.bindingHeld = false;
          if (s.hold)
            state.armed = false;
        } else if (down && !ui.bindingDown && ui.physicalMods == s.bindMods && front(state.source)) {
          ui.bindingDown = true;
          state.bindingHeld = true;
          PostMessageW(ui.window, APP_BIND, 1, 0);
        }
      }
      if (!ui.raw && message == WM_MOUSEMOVE && state.armed && s.camera && front(state.source)) {
        if (ui.lastPhysicalValid)
          state.physical(m->pt.y - ui.lastPhysical.y);
        ui.lastPhysical = m->pt;
        ui.lastPhysicalValid = true;
      }
    }
  }
  return CallNextHookEx(nullptr, n, message, data);
}
HWND control(const wchar_t *cls, const wchar_t *text, int id, DWORD style = 0, HWND parent = nullptr) {
  if (!parent)
    parent = ui.window;
  HWND w = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 100, 28, parent,
                           reinterpret_cast<HMENU>(INT_PTR(id)), GetModuleHandleW(nullptr), nullptr);
  SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(ui.font), TRUE);
  if (parent == ui.window)
    ui.controls.push_back(w);
  return w;
}
HWND button(const wchar_t *text, int id, HWND parent = nullptr) {
  return control(L"BUTTON", text, id, BS_OWNERDRAW, parent);
}
void move(int id, int x, int y, int w, int h = 30) {
  MoveWindow(GetDlgItem(ui.window, id), dip(x), dip(y), dip(w), dip(h), TRUE);
}
void layout() {
  RECT r{};
  GetClientRect(ui.window, &r);
  int w = int((r.right - r.left) / ui.scale), h = int((r.bottom - r.top) / ui.scale);
  move(STOP, w - 132, 20, 110);
  move(SOURCE, 88, 66, std::max(300, w - 425), 240);
  move(REFRESH, w - 325, 66, 88);
  move(SCAN, w - 225, 66, 118);
  move(VIEW, 64, 108, 160, 120);
  move(MODE, 282, 108, 94, 120);
  move(BIND, 388, 108, 190);
  move(AIM, 590, 108, 120);
  move(ARM, w - 180, 108, 158);
  move(SINGLE, 22, 150, 112);
  move(RESET, 144, 150, 143);
  move(LEVEL, 297, 150, 117);
  move(SETLEVEL, 424, 150, 170);
  move(SELF, w - 380, 150, 150);
  move(IMPORT, w - 220, 150, 100);
  move(SETTINGS, w - 110, 150, 88);
  move(UPDATES, w - 438, 20, 130);
  move(CLICKS, 28, h - 115, 125);
  move(BUTTON, 204, h - 115, 85, 120);
  move(CPS, 333, h - 115, 85);
  move(DUTY, 490, h - 115, 78);
  ui.preview = {dip(22), dip(210), dip(w - 22), dip(h - 158)};
  InvalidateRect(ui.window, nullptr, TRUE);
}
void refreshWindows() {
  HWND previous = state.source;
  ui.windows.clear();
  SendMessageW(ui.source, CB_RESETCONTENT, 0, 0);
  EnumWindows(
      [](HWND w, LPARAM) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(w, &pid);
        RECT r{};
        GetClientRect(w, &r);
        if (IsWindowVisible(w) && pid != GetCurrentProcessId() && GetWindowTextLengthW(w) > 0 &&
            r.right >= 200 && r.bottom >= 150 && !(GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) {
          ui.windows.push_back(w);
          auto title = label(w);
          SendMessageW(ui.source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
        }
        return TRUE;
      },
      0);
  int selected = -1;
  for (int i = 0; i < int(ui.windows.size()); i++) {
    if (ui.windows[i] == previous)
      selected = i;
  }
  if (selected < 0)
    for (int i = 0; i < int(ui.windows.size()); i++)
      if (label(ui.windows[i]).find(L"Roblox") != std::wstring::npos) {
        selected = i;
        break;
      }
  if (selected < 0 && !ui.windows.empty())
    selected = 0;
  if (selected >= 0) {
    SendMessageW(ui.source, CB_SETCURSEL, selected, 0);
    state.source = ui.windows[selected];
  } else
    state.source = nullptr;
  status(ui.windows.empty() ? "No visible windows found" : "Choose the game window, then Start scan");
}
void text(HDC dc, int x, int y, const std::wstring &s, COLORREF color = RGB(220, 225, 235),
          HFONT font = nullptr) {
  auto old = SelectObject(dc, font ? font : ui.font);
  SetTextColor(dc, color);
  SetBkMode(dc, TRANSPARENT);
  TextOutW(dc, dip(x), dip(y), s.c_str(), int(s.size()));
  SelectObject(dc, old);
}
RECT imageRect(const Image &image) {
  int w = ui.preview.right - ui.preview.left, h = ui.preview.bottom - ui.preview.top;
  double scale = std::min(double(w) / image.w, double(h) / image.h);
  int iw = int(image.w * scale), ih = int(image.h * scale);
  return {ui.preview.left + (w - iw) / 2, ui.preview.top + (h - ih) / 2, ui.preview.left + (w + iw) / 2,
          ui.preview.top + (h + ih) / 2};
}
void drawImage(HDC dc, const Image &im, RECT r) {
  BITMAPINFO b{};
  b.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  b.bmiHeader.biWidth = im.w;
  b.bmiHeader.biHeight = -im.h;
  b.bmiHeader.biPlanes = 1;
  b.bmiHeader.biBitCount = 32;
  b.bmiHeader.biCompression = BI_RGB;
  SetStretchBltMode(dc, HALFTONE);
  StretchDIBits(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, 0, im.w, im.h, im.bgra.data(), &b,
                DIB_RGB_COLORS, SRCCOPY);
}
void box(HDC dc, Rect b, RECT image, COLORREF color, int thickness = 2) {
  HPEN pen = CreatePen(PS_SOLID, thickness, color);
  auto oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
  int w = image.right - image.left, h = image.bottom - image.top;
  Rectangle(dc, image.left + int(b.x * w), image.top + int(b.y * h), image.left + int((b.x + b.w) * w),
            image.top + int((b.y + b.h) * h));
  SelectObject(dc, oldPen);
  SelectObject(dc, oldBrush);
  DeleteObject(pen);
}
double cpuUsage() {
  static double last = 0, previous = 0, value = 0;
  double t = now();
  if (t - last < 1)
    return value;
  FILETIME c, e, k, u;
  if (GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) {
    auto n = [](FILETIME f) {
      ULARGE_INTEGER x;
      x.LowPart = f.dwLowDateTime;
      x.HighPart = f.dwHighDateTime;
      return double(x.QuadPart) / 1e7;
    };
    double total = n(k) + n(u);
    if (last)
      value = std::max(0., (total - previous) / (t - last) /
                               std::max(1u, std::thread::hardware_concurrency()) * 100);
    previous = total;
    last = t;
  }
  return value;
}
void paint(HWND window, HDC dc) {
  RECT bounds{};
  GetClientRect(window, &bounds);
  FillRect(dc, &bounds, ui.background);
  int w = int(bounds.right / ui.scale), h = int(bounds.bottom / ui.scale);
  Settings s;
  ViewState v;
  std::string provider, error, inputError, method, level;
  double cps, moves;
  bool own;
  {
    std::lock_guard<std::mutex> l(state.mutex);
    s = state.settings;
    v = state.view;
    provider = state.provider;
    error = state.modelError;
    inputError = state.inputError;
    method = state.method;
    level = state.levelStatus;
    cps = state.cps;
    moves = state.moves;
    own = !state.own.empty();
  }
  text(dc, 22, 20, L"L U X W A R E", RGB(48, 154, 255), ui.big);
  text(dc, 270, 27, L"WINDOWS " + std::wstring(appVersion), RGB(130, 145, 165));
  text(dc, w - 292, 27, state.armed ? L"INPUT ARMED" : L"INPUT OFF",
       state.armed ? RGB(66, 220, 155) : RGB(140, 150, 165));
  text(dc, 22, 73, L"Window");
  text(dc, 22, 115, L"View");
  text(dc, 236, 115, L"Mode");
  text(dc, 22, 190, wide(error.empty() ? "Detector: " + provider + " | " + method : error),
       error.empty() ? RGB(125, 190, 220) : RGB(255, 165, 90));
  HBRUSH black = CreateSolidBrush(RGB(3, 5, 8));
  FillRect(dc, &ui.preview, black);
  DeleteObject(black);
  auto image = ui.marking ? ui.frozen : v.image;
  if (image && image->valid()) {
    RECT imageBounds = imageRect(*image);
    drawImage(dc, *image, imageBounds);
    if (!ui.marking) {
      for (auto &d : v.detections)
        if (!s.single || (v.target && d.id == v.target->id)) {
          COLORREF color = d.visual ? RGB(255, 170, 50) : RGB(255, 65, 85);
          box(dc, d.box, imageBounds, color);
          std::wstring label = std::wstring(d.visual ? L"CV #" : L"ML #") + std::to_wstring(d.id) + L" " +
                               number(d.score * 100, 0) + L"%";
          SetBkMode(dc, OPAQUE);
          SetBkColor(dc, RGB(10, 12, 16));
          SetTextColor(dc, color);
          TextOutW(dc, imageBounds.left + int(d.box.x * (imageBounds.right - imageBounds.left)),
                   imageBounds.top + int(d.box.y * (imageBounds.bottom - imageBounds.top)) - dip(16),
                   label.c_str(), int(label.size()));
          SetBkMode(dc, TRANSPARENT);
        }
      if (v.target) {
        box(dc, v.target->box, imageBounds, RGB(255, 220, 70), 3);
        HPEN p = CreatePen(PS_SOLID, 1, RGB(0, 210, 255));
        auto old = SelectObject(dc, p);
        POINT cursor{};
        GetCursorPos(&cursor);
        double ax = s.camera ? .5 : (cursor.x - v.client.x) / std::max(1., v.client.w),
               ay = s.camera ? .5 : (cursor.y - v.client.y) / std::max(1., v.client.h);
        MoveToEx(dc, imageBounds.left + int(ax * (imageBounds.right - imageBounds.left)),
                 imageBounds.top + int(ay * (imageBounds.bottom - imageBounds.top)), nullptr);
        LineTo(dc, imageBounds.left + int(v.target->box.cx() * (imageBounds.right - imageBounds.left)),
               imageBounds.top + int(v.target->box.cy() * (imageBounds.bottom - imageBounds.top)));
        SelectObject(dc, old);
        DeleteObject(p);
      }
    }
    if (ui.dragging) {
      RECT d{std::min(ui.dragStart.x, ui.dragEnd.x), std::min(ui.dragStart.y, ui.dragEnd.y),
             std::max(ui.dragStart.x, ui.dragEnd.x), std::max(ui.dragStart.y, ui.dragEnd.y)};
      HPEN p = CreatePen(PS_SOLID, 2, RGB(0, 220, 255));
      auto a = SelectObject(dc, p), b = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
      Rectangle(dc, d.left, d.top, d.right, d.bottom);
      SelectObject(dc, a);
      SelectObject(dc, b);
      DeleteObject(p);
    }
  } else
    text(dc, 45, 270, L"Start scanning, then focus the selected game for live screen capture",
         RGB(140, 155, 175));
  std::string message =
      ui.marking                ? "PREVIEW FROZEN: drag tightly around your own visible body. Escape cancels."
      : now() < ui.messageUntil ? ui.message
      : state.scan
          ? (front(state.source) ? v.note : "Capture paused: focus the selected game window (Alt+Tab)")
          : ui.message;
  text(dc, 22, h - 149, wide(message), RGB(60, 208, 240));
  text(dc, 160, h - 108, L"Button");
  text(dc, 300, h - 108, L"CPS");
  text(dc, 430, h - 108, L"Duty %");
  std::wstring input = L"Input off";
  POINT cursor{};
  GetCursorPos(&cursor);
  if (state.armed) {
    if (s.hold && !state.bindingHeld)
      input = L"Armed: hold your binding in the selected game";
    else if (!front(state.source))
      input = L"Paused: focus selected game";
    else if (now() - v.received > .5)
      input = L"Paused: no fresh captured frame";
    else if (!contains(v.client, cursor))
      input = L"Paused: pointer outside game client";
    else if (!s.aim || s.strength <= 0)
      input = L"Active: clicker only";
    else if (!own)
      input = L"Aim requires your saved avatar reference";
    else if (!v.target)
      input =
          wide(level == "Returning to calibrated level" ? level : "Active: waiting for a detected target");
    else
      input = L"Active: following detected target";
  }
  text(dc, 22, h - 70, input, RGB(55, 155, 255));
  auto measured = std::wstring(s.aim ? L"Vision " : L"Capture ") +
                  number(now() - v.received > .5 ? 0 : v.fps, 0) + L" FPS  |  " + number(v.ms, 1) +
                  L" ms  |  " + number(cps) + L" CPS  |  " + number(moves, 0) + L" moves/s  |  CPU " +
                  number(cpuUsage(), 1) + L"%";
  text(dc, std::max(400, w - 690), h - 70, measured, RGB(190, 200, 215), ui.mono);
  text(dc, 22, h - 39,
       L"Ctrl+Alt+Esc stops all. Ctrl+Shift+R resets target. First person: relative camera input. Third "
       L"person: free cursor.",
       RGB(130, 145, 160));
  if (!inputError.empty())
    text(dc, 22, h - 20, wide(inputError), RGB(255, 130, 100));
}
void drawButton(DRAWITEMSTRUCT *d) {
  if (d->CtlType == ODT_COMBOBOX) {
    RECT r = d->rcItem;
    HBRUSH fill = CreateSolidBrush(d->itemState & ODS_SELECTED ? RGB(23, 65, 110) : RGB(28, 34, 44));
    FillRect(d->hDC, &r, fill);
    DeleteObject(fill);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, RGB(230, 235, 245));
    SelectObject(d->hDC, ui.font);
    std::wstring title = L"Choose a window";
    if (d->itemID != UINT(-1)) {
      LRESULT count = SendMessageW(d->hwndItem, CB_GETLBTEXTLEN, d->itemID, 0);
      if (count >= 0 && count < 32768) {
        std::vector<wchar_t> buffer(size_t(count) + 1);
        SendMessageW(d->hwndItem, CB_GETLBTEXT, d->itemID, reinterpret_cast<LPARAM>(buffer.data()));
        title = buffer.data();
      }
    }
    r.left += dip(6);
    DrawTextW(d->hDC, title.c_str(), int(title.size()), &r, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return;
  }
  auto s = state.config();
  bool checkbox = d->CtlID == AIM || d->CtlID == SINGLE || d->CtlID == LEVEL || d->CtlID == CLICKS ||
                  d->CtlID == GPU || d->CtlID == AUTOUPDATE || d->CtlID == GAMEFRIENDLY;
  bool checked = (d->CtlID == AIM && s.aim) || (d->CtlID == SINGLE && s.single) ||
                 (d->CtlID == LEVEL && s.level) || (d->CtlID == CLICKS && s.clicks) ||
                 (d->CtlID == GPU && s.gpu) || (d->CtlID == AUTOUPDATE && updates.automatic) ||
                 (d->CtlID == GAMEFRIENDLY && s.gameFriendly);
  RECT r = d->rcItem;
  bool primary = d->CtlID == SCAN || d->CtlID == ARM;
  COLORREF color = primary ? RGB(0, 115, 245) : RGB(32, 38, 49);
  if (d->itemState & ODS_SELECTED)
    color = RGB(45, 70, 100);
  HBRUSH b = CreateSolidBrush(checkbox ? RGB(9, 13, 19) : color);
  FillRect(d->hDC, &r, b);
  DeleteObject(b);
  SetBkMode(d->hDC, TRANSPARENT);
  SetTextColor(d->hDC, d->CtlID == STOP ? RGB(255, 80, 90) : RGB(225, 232, 243));
  SelectObject(d->hDC, ui.font);
  if (checkbox) {
    RECT c{r.left + 3, r.top + dip(7), r.left + dip(19), r.top + dip(23)};
    HBRUSH fill = CreateSolidBrush(checked ? RGB(0, 125, 255) : RGB(50, 60, 75));
    FillRect(d->hDC, &c, fill);
    DeleteObject(fill);
    if (checked) {
      HPEN p = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
      auto old = SelectObject(d->hDC, p);
      MoveToEx(d->hDC, c.left + 3, c.top + 8, nullptr);
      LineTo(d->hDC, c.left + 7, c.top + 12);
      LineTo(d->hDC, c.left + 13, c.top + 3);
      SelectObject(d->hDC, old);
      DeleteObject(p);
    }
    r.left += dip(25);
  }
  wchar_t label[256]{};
  GetWindowTextW(d->hwndItem, label, 256);
  DrawTextW(d->hDC, label, -1, &r, DT_VCENTER | DT_SINGLELINE | (checkbox ? DT_LEFT : DT_CENTER));
  if (d->itemState & ODS_FOCUS)
    DrawFocusRect(d->hDC, &d->rcItem);
}
LRESULT CALLBACK settingsProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  if (m == WM_CREATE) {
    const wchar_t *names[] = {
        L"Aim strength",         L"Smoothing",       L"Camera sensitivity", L"Acquire FOV",
        L"Detection confidence", L"Prediction (ms)", L"Analysis FPS limit", L"Movement Hz limit"};
    auto s = state.config();
    int lows[] = {0, 0, 10, 5, 10, 0, 5, 5}, highs[] = {100, 95, 300, 71, 99, 100, 240, 240};
    int values[] = {int(s.strength * 100),
                    int(s.smoothing * 100),
                    int(s.sensitivity * 100),
                    int(s.fov * 100),
                    int(s.confidence * 100),
                    int(s.prediction * 1000),
                    int(s.fps),
                    int(s.hz)};
    ui.sliders.clear();
    for (int i = 0; i < 8; i++) {
      HWND label = control(L"STATIC", names[i], 700 + i, 0, w);
      MoveWindow(label, 20, 25 + i * 43, 170, 25, TRUE);
      HWND slider = control(TRACKBAR_CLASSW, L"", SLIDER + i, TBS_AUTOTICKS, w);
      MoveWindow(slider, 200, 20 + i * 43, 310, 35, TRUE);
      SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(lows[i], highs[i]));
      SendMessageW(slider, TBM_SETPOS, TRUE, values[i]);
      ui.sliders.push_back(slider);
    }
    MoveWindow(button(L"GPU (DirectML)", GPU, w), 20, 378, 190, 30, TRUE);
    MoveWindow(button(L"Use bundled model", BUNDLED, w), 240, 378, 200, 30, TRUE);
    MoveWindow(button(L"Game-friendly: 60 FPS GPU / 30 FPS CPU", GAMEFRIENDLY, w), 20, 414, 500, 30, TRUE);
    HWND info =
        control(L"STATIC",
                L"Actual rates appear below the preview. Game-friendly processing yields time to the game.\n"
                L"Level return estimates mouse Y, not camera angle. Manual motion cancels it.\n"
                L"Player recognition and self exclusion can still make mistakes.",
                780, 0, w);
    MoveWindow(info, 20, 453, 500, 65, TRUE);
    MoveWindow(button(L"Check for updates", CHECKUPDATE, w), 20, 530, 210, 32, TRUE);
    MoveWindow(button(L"Automatic updates", AUTOUPDATE, w), 245, 530, 275, 32, TRUE);
    HWND updateText = control(L"STATIC", updates.message.c_str(), 790, 0, w);
    MoveWindow(updateText, 20, 575, 500, 65, TRUE);
    MoveWindow(button(L"Done", DONE, w), 390, 650, 130, 32, TRUE);
    return 0;
  }
  if (m == WM_HSCROLL) {
    if (ui.sliders.size() == 8) {
      auto s = state.config();
      auto v = [&](int i) { return int(SendMessageW(ui.sliders[i], TBM_GETPOS, 0, 0)); };
      s.strength = v(0) / 100.;
      s.smoothing = v(1) / 100.;
      s.sensitivity = v(2) / 100.;
      s.fov = v(3) / 100.;
      s.confidence = v(4) / 100.;
      s.prediction = v(5) / 1000.;
      s.fps = v(6);
      s.hz = v(7);
      {
        std::lock_guard<std::mutex> l(state.mutex);
        state.settings = s;
      }
      save();
    }
    return 0;
  }
  if (m == WM_COMMAND) {
    int id = LOWORD(wp);
    if (id == CHECKUPDATE) {
      if (updates.ready) {
        if (MessageBoxW(w,
                        L"Install the downloaded update? Luxware will stop input, close and restart. Your "
                        L"saved settings stay in place.",
                        L"Luxware update", MB_OKCANCEL | MB_ICONINFORMATION) == IDOK) {
          stop();
          if (updates.start(true))
            PostMessageW(ui.window, WM_CLOSE, 0, 0);
        }
      } else
        updates.start();
      return 0;
    }
    if (id == GAMEFRIENDLY) {
      {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.settings.gameFriendly = !state.settings.gameFriendly;
      }
      save();
      InvalidateRect(w, nullptr, FALSE);
      return 0;
    }
    if (id == AUTOUPDATE) {
      updates.setAutomatic(!updates.automatic);
      InvalidateRect(w, nullptr, FALSE);
      return 0;
    }
    if (id == DONE) {
      DestroyWindow(w);
      return 0;
    }
    if (id == GPU || id == BUNDLED) {
      state.armed = false;
      {
        std::lock_guard<std::mutex> l(state.mutex);
        if (id == GPU)
          state.settings.gpu = !state.settings.gpu;
        else
          state.settings.modelPath.clear();
      }
      state.modelReady = false;
      state.reload++;
      resetSelection();
      save();
      InvalidateRect(w, nullptr, TRUE);
      return 0;
    }
  }
  if (m == WM_DRAWITEM) {
    drawButton(reinterpret_cast<DRAWITEMSTRUCT *>(lp));
    return TRUE;
  }
  if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLORDLG) {
    SetTextColor(reinterpret_cast<HDC>(wp), RGB(220, 230, 240));
    SetBkColor(reinterpret_cast<HDC>(wp), RGB(9, 13, 19));
    return reinterpret_cast<LRESULT>(ui.background);
  }
  if (m == WM_ERASEBKGND) {
    RECT r;
    GetClientRect(w, &r);
    FillRect(reinterpret_cast<HDC>(wp), &r, ui.background);
    return 1;
  }
  if (m == WM_DESTROY) {
    ui.settings = nullptr;
    ui.sliders.clear();
    save();
    return 0;
  }
  return DefWindowProcW(w, m, wp, lp);
}
void showSettings() {
  if (ui.settings) {
    SetForegroundWindow(ui.settings);
    return;
  }
  ui.settings = CreateWindowExW(WS_EX_TOOLWINDOW, L"LuxwareSettings", L"Luxware settings",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, 200, 150, 565, 740, ui.window,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
  ShowWindow(ui.settings, SW_SHOW);
}
void markOwn() {
  if (ui.marking) {
    ui.marking = ui.dragging = false;
    ui.frozen.reset();
    status("Selection cancelled");
    return;
  }
  {
    std::lock_guard<std::mutex> l(state.mutex);
    ui.frozen = state.view.image;
  }
  if (!ui.frozen) {
    status("Scan the game in third person first; then return here to mark your avatar");
    return;
  }
  state.armed = false;
  ui.marking = true;
  ui.dragging = false;
  status("Drag around your own visible body in the frozen preview");
}
void importModel() {
  state.armed = false;
  wchar_t path[32768]{};
  OPENFILENAMEW f{};
  f.lStructSize = sizeof(f);
  f.hwndOwner = ui.window;
  f.lpstrFilter = L"ONNX avatar detector\0*.onnx\0";
  f.lpstrFile = path;
  f.nMaxFile = 32768;
  f.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (GetOpenFileNameW(&f)) {
    std::lock_guard<std::mutex> l(state.mutex);
    state.settings.modelPath = path;
    state.modelReady = false;
    state.reload++;
    state.reset++;
    state.view.target.reset();
  }
  save();
}
void renderOwnWindow(const std::filesystem::path &path) {
  RECT bounds{};
  GetClientRect(ui.window, &bounds);
  HDC dc = CreateCompatibleDC(nullptr);
  BITMAPINFO b{};
  b.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  b.bmiHeader.biWidth = bounds.right;
  b.bmiHeader.biHeight = -bounds.bottom;
  b.bmiHeader.biPlanes = 1;
  b.bmiHeader.biBitCount = 32;
  b.bmiHeader.biCompression = BI_RGB;
  void *pixels = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &b, DIB_RGB_COLORS, &pixels, nullptr, 0);
  auto old = SelectObject(dc, bitmap);
  paint(ui.window, dc);
  for (HWND child : ui.controls) {
    RECT r{};
    GetWindowRect(child, &r);
    MapWindowPoints(nullptr, ui.window, reinterpret_cast<POINT *>(&r), 2);
    int saved = SaveDC(dc);
    SetViewportOrgEx(dc, r.left, r.top, nullptr);
    wchar_t cls[64]{};
    GetClassNameW(child, cls, 64);
    if (std::wstring(cls) == L"Button") {
      DRAWITEMSTRUCT item{};
      item.CtlType = ODT_BUTTON;
      item.CtlID = GetDlgCtrlID(child);
      item.hwndItem = child;
      item.hDC = dc;
      item.rcItem = {0, 0, r.right - r.left, r.bottom - r.top};
      drawButton(&item);
    } else if (std::wstring(cls) == L"ComboBox") {
      DRAWITEMSTRUCT item{};
      item.CtlType = ODT_COMBOBOX;
      item.CtlID = GetDlgCtrlID(child);
      item.hwndItem = child;
      item.hDC = dc;
      item.itemID = UINT(SendMessageW(child, CB_GETCURSEL, 0, 0));
      item.rcItem = {0, 0, r.right - r.left, r.bottom - r.top};
      drawButton(&item);
    } else
      SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    RestoreDC(dc, saved);
  }
  Image image;
  image.w = bounds.right;
  image.h = bounds.bottom;
  image.bgra.resize(size_t(image.w) * image.h * 4);
  std::memcpy(image.bgra.data(), pixels, image.bgra.size());
  saveBMP(path, image);
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
}
void command(int id, int notification) {
  auto s = state.config();
  if (id == STOP) {
    stop();
    return;
  }
  if (id == REFRESH) {
    stop();
    refreshWindows();
    return;
  }
  if (id == SCAN) {
    if (state.scan)
      stop();
    else if (IsWindow(state.source)) {
      state.scan = true;
      resetSelection();
      status("Scanning: focus the selected game window");
    } else
      status("Select a visible window first");
    return;
  }
  if (id == ARM) {
    arm();
    return;
  }
  if (id == RESET) {
    state.armed = false;
    resetSelection();
    status("Target reset. Activate to acquire a new entity");
    return;
  }
  if (id == SETLEVEL) {
    if (s.camera && state.scan) {
      state.calibrate++;
      status("Current game view is the level reference");
    } else
      status("Start scanning in First person, look level, then Ctrl+Shift+L");
    return;
  }
  if (id == BIND) {
    state.armed = false;
    ui.recording = !ui.recording;
    ui.recordUntil = now() + 15;
    status(ui.recording ? "Press a key or mouse button while Luxware is focused. Escape cancels."
                        : "Binding cancelled");
    return;
  }
  if (id == SELF) {
    markOwn();
    return;
  }
  if (id == SETTINGS || id == UPDATES) {
    showSettings();
    return;
  }
  if (id == IMPORT) {
    importModel();
    return;
  }
  if (id == SOURCE && notification == CBN_SELCHANGE) {
    int i = int(SendMessageW(ui.source, CB_GETCURSEL, 0, 0));
    stop();
    if (i >= 0 && i < int(ui.windows.size()))
      state.source = ui.windows[i];
    return;
  }
  if ((id == CPS || id == DUTY) && notification == EN_KILLFOCUS) {
    applyEdits();
    return;
  }
  if (id == VIEW && notification == CBN_SELCHANGE) {
    s.camera = SendMessageW(ui.view, CB_GETCURSEL, 0, 0) == 0;
    state.armed = false;
    resetSelection();
  } else if (id == MODE && notification == CBN_SELCHANGE)
    s.hold = SendMessageW(ui.mode, CB_GETCURSEL, 0, 0) == 1;
  else if (id == BUTTON && notification == CBN_SELCHANGE)
    s.right = SendMessageW(ui.button, CB_GETCURSEL, 0, 0) == 1;
  else if (id == AIM) {
    s.aim = !s.aim;
    resetSelection();
  } else if (id == CLICKS)
    s.clicks = !s.clicks;
  else if (id == SINGLE) {
    s.single = !s.single;
    resetSelection();
  } else if (id == LEVEL)
    s.level = !s.level;
  else
    return;
  {
    std::lock_guard<std::mutex> l(state.mutex);
    state.settings = s;
  }
  save();
  InvalidateRect(ui.window, nullptr, FALSE);
}
LRESULT CALLBACK windowProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
  switch (m) {
  case WM_CREATE: {
    ui.window = w;
    ui.scale = GetDpiForWindow(w) / 96.;
    ui.font =
        CreateFontW(-dip(15), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    ui.big = CreateFontW(-dip(24), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Segoe UI");
    ui.mono = CreateFontW(-dip(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                          CLEARTYPE_QUALITY, 0, L"Consolas");
    ui.background = CreateSolidBrush(RGB(9, 13, 19));
    ui.editBrush = CreateSolidBrush(RGB(28, 34, 44));
    DWORD combo = CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
    ui.source = control(L"COMBOBOX", L"", SOURCE, combo | WS_VSCROLL);
    ui.view = control(L"COMBOBOX", L"", VIEW, combo);
    ui.mode = control(L"COMBOBOX", L"", MODE, combo);
    ui.button = control(L"COMBOBOX", L"", BUTTON, combo);
    for (auto pair : {std::pair<HWND, const wchar_t *>{ui.view, L"First person"},
                      {ui.view, L"Third person"},
                      {ui.mode, L"Toggle"},
                      {ui.mode, L"Hold"},
                      {ui.button, L"Left"},
                      {ui.button, L"Right"}})
      SendMessageW(pair.first, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pair.second));
    auto s = state.config();
    SendMessageW(ui.view, CB_SETCURSEL, s.camera ? 0 : 1, 0);
    SendMessageW(ui.mode, CB_SETCURSEL, s.hold ? 1 : 0, 0);
    SendMessageW(ui.button, CB_SETCURSEL, s.right ? 1 : 0, 0);
    for (auto pair : {std::pair<int, const wchar_t *>{REFRESH, L"Refresh"},
                      {SCAN, L"Start scan"},
                      {STOP, L"STOP ALL"},
                      {BIND, L"Bind"},
                      {AIM, L"Aim assist"},
                      {ARM, L"Activate input"},
                      {SINGLE, L"One entity"},
                      {RESET, L"Reset Ctrl+Shift+R"},
                      {LEVEL, L"Level return"},
                      {SETLEVEL, L"Set level Ctrl+Shift+L"},
                      {SELF, L"Mark myself"},
                      {IMPORT, L"Model..."},
                      {SETTINGS, L"Settings"},
                      {UPDATES, L"Updates"},
                      {CLICKS, L"Auto clicker"}})
      button(pair.second, pair.first);
    ui.cps = control(L"EDIT", number(s.cps).c_str(), CPS, ES_AUTOHSCROLL | WS_BORDER);
    ui.duty = control(L"EDIT", number(s.duty * 100).c_str(), DUTY, ES_AUTOHSCROLL | WS_BORDER);
    if (!ui.testing) {
      RAWINPUTDEVICE raw{1, 2, RIDEV_INPUTSINK, w};
      ui.raw = RegisterRawInputDevices(&raw, 1, sizeof(raw)) != FALSE;
      ui.keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHook, GetModuleHandleW(nullptr), 0);
      ui.mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseHook, GetModuleHandleW(nullptr), 0);
    }
    BOOL dark = TRUE;
    DwmSetWindowAttribute(w, 20, &dark, sizeof(dark));
    SetWindowDisplayAffinity(w, 0x11);
    SetTimer(w, 1, 50, nullptr);
    state.heartbeat = now();
    layout();
    if (!ui.testing)
      refreshWindows();
    if (!ui.testing && (!ui.keyboardHook || !ui.mouseHook))
      status("Global input monitor failed. Restart Luxware; STOP ALL remains available.");
    return 0;
  }
  case WM_SIZE:
    layout();
    return 0;
  case WM_GETMINMAXINFO: {
    auto p = reinterpret_cast<MINMAXINFO *>(lp);
    p->ptMinTrackSize = {dip(1080), dip(760)};
    return 0;
  }
  case WM_DPICHANGED: {
    ui.scale = HIWORD(wp) / 96.;
    auto r = reinterpret_cast<RECT *>(lp);
    SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    layout();
    return 0;
  }
  case WM_COMMAND:
    command(LOWORD(wp), HIWORD(wp));
    return 0;
  case APP_BIND: {
    auto s = state.config();
    if (!s.hold || !state.armed)
      arm();
    return 0;
  }
  case APP_SAVE:
    save();
    return 0;
  case WM_TIMER: {
    state.heartbeat = now();
    // Keep the input safety heartbeat at 20 Hz, while limiting background preview work.
    if (now() < ui.nextVisual)
      return 0;
    HWND foreground = GetForegroundWindow();
    ui.nextVisual = now() + ((foreground == w || foreground == ui.settings) ? .05 : .2);
    if (!ui.testing) {
      if (ui.updateAfter > 0 && now() >= ui.updateAfter) {
        ui.updateAfter = 0;
        if (updates.automatic)
          updates.start();
      }
      updates.poll();
      SetWindowTextW(GetDlgItem(w, UPDATES), updates.ready ? L"Update ready" : L"Updates");
      if (ui.settings) {
        SetWindowTextW(GetDlgItem(ui.settings, 790), updates.message.c_str());
        SetWindowTextW(GetDlgItem(ui.settings, CHECKUPDATE),
                       updates.ready ? L"Install update" : L"Check for updates");
        EnableWindow(GetDlgItem(ui.settings, CHECKUPDATE), !updates.busy());
      }
    }
    if (ui.recording && now() > ui.recordUntil) {
      ui.recording = false;
      status("Binding recording timed out");
    }
    auto s = state.config();
    bool hasOwn;
    {
      std::lock_guard<std::mutex> l(state.mutex);
      hasOwn = !state.own.empty();
    }
    SetWindowTextW(GetDlgItem(w, SELF), ui.marking ? L"Cancel avatar box"
                                        : hasOwn   ? L"Update my avatar"
                                                   : L"Mark myself");
    SetWindowTextW(GetDlgItem(w, BIND),
                   (ui.recording ? L"Cancel binding" : (L"Bind: " + bindingName(s))).c_str());
    SetWindowTextW(GetDlgItem(w, SCAN), state.scan ? L"Stop scan" : L"Start scan");
    SetWindowTextW(GetDlgItem(w, ARM), state.armed ? L"Stop input" : L"Activate input");
    EnableWindow(ui.source, !state.scan);
    ShowWindow(GetDlgItem(w, LEVEL), s.camera ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(w, SETLEVEL), s.camera ? SW_SHOW : SW_HIDE);
    if (!IsIconic(w))
      InvalidateRect(w, nullptr, FALSE);
    return 0;
  }
  case WM_INPUT: {
    UINT bytes = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, nullptr, &bytes, sizeof(RAWINPUTHEADER));
    std::vector<uint8_t> buffer(bytes);
    if (bytes && GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, buffer.data(), &bytes,
                                 sizeof(RAWINPUTHEADER)) == bytes) {
      auto raw = reinterpret_cast<RAWINPUT *>(buffer.data());
      if (raw->header.dwType == RIM_TYPEMOUSE && raw->header.hDevice && state.armed && front(state.source) &&
          state.config().camera && (raw->data.mouse.lLastX || raw->data.mouse.lLastY))
        state.physical(raw->data.mouse.lLastY);
    }
    return DefWindowProcW(w, m, wp, lp);
  }
  case WM_KEYDOWN:
    if (wp == VK_ESCAPE && ui.marking) {
      ui.marking = ui.dragging = false;
      ui.frozen.reset();
      status("Avatar selection cancelled");
    }
    return 0;
  case WM_LBUTTONDOWN: {
    POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    if (ui.marking && ui.frozen && PtInRect(&ui.preview, p)) {
      ui.dragging = true;
      ui.dragStart = ui.dragEnd = p;
      SetCapture(w);
    }
    return 0;
  }
  case WM_MOUSEMOVE:
    if (ui.dragging) {
      ui.dragEnd = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      InvalidateRect(w, nullptr, FALSE);
    }
    return 0;
  case WM_LBUTTONUP: {
    if (ui.dragging && ui.frozen) {
      ui.dragEnd = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ReleaseCapture();
      RECT r = imageRect(*ui.frozen);
      double iw = r.right - r.left, ih = r.bottom - r.top;
      Rect b{(std::min(ui.dragStart.x, ui.dragEnd.x) - r.left) / iw,
             (std::min(ui.dragStart.y, ui.dragEnd.y) - r.top) / ih,
             std::abs(ui.dragStart.x - ui.dragEnd.x) / iw, std::abs(ui.dragStart.y - ui.dragEnd.y) / ih};
      auto v = visible(b);
      if (v && v->w >= .015 && v->h >= .025 && v->area() < .5) {
        try {
          Image own = crop(*ui.frozen, *v);
          auto folder = dataFolder() / L"AvatarViews";
          std::filesystem::create_directories(folder);
          auto config = state.config();
          size_t index = config.avatarNext % 6;
          saveBMP(folder / (L"view" + std::to_wstring(index) + L".bmp"), own);
          std::vector<Feature> refs;
          for (int i = 0; i < 6; i++) {
            auto path = folder / (L"view" + std::to_wstring(i) + L".bmp");
            if (std::filesystem::exists(path)) {
              auto im = loadBMP(path);
              refs.push_back(describe(im, {0, 0, 1, 1}));
            }
          }
          config.avatarNext = (index + 1) % 6;
          {
            std::lock_guard<std::mutex> l(state.mutex);
            state.own = std::move(refs);
            state.settings = config;
          }
          save();
          ui.marking = false;
          ui.frozen.reset();
          resetSelection();
          status("Own-avatar view saved. Automatic scan needs no target selection.");
        } catch (const std::exception &e) {
          status(e.what());
        }
      } else
        status("Draw a tight body box fully inside the preview");
      ui.dragging = false;
      InvalidateRect(w, nullptr, FALSE);
    }
    return 0;
  }
  case WM_DRAWITEM:
    drawButton(reinterpret_cast<DRAWITEMSTRUCT *>(lp));
    return TRUE;
  case WM_MEASUREITEM: {
    auto item = reinterpret_cast<MEASUREITEMSTRUCT *>(lp);
    if (item->CtlType == ODT_COMBOBOX) {
      item->itemHeight = dip(24);
      return TRUE;
    }
    break;
  }
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX:
  case WM_CTLCOLORSTATIC:
    SetTextColor(reinterpret_cast<HDC>(wp), RGB(230, 235, 245));
    SetBkColor(reinterpret_cast<HDC>(wp), RGB(28, 34, 44));
    return reinterpret_cast<LRESULT>(ui.editBrush);
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    RECT r;
    GetClientRect(w, &r);
    if (HDC memory = ui.canvas.get(dc, r.right, r.bottom)) {
      paint(w, memory);
      BitBlt(dc, 0, 0, r.right, r.bottom, memory, 0, 0, SRCCOPY);
    } else
      paint(w, dc);
    EndPaint(w, &ps);
    return 0;
  }
  case WM_CLOSE:
    stop();
    DestroyWindow(w);
    return 0;
  case WM_DESTROY:
    ui.canvas.reset();
    state.quit = true;
    KillTimer(w, 1);
    if (ui.keyboardHook)
      UnhookWindowsHookEx(ui.keyboardHook);
    if (ui.mouseHook)
      UnhookWindowsHookEx(ui.mouseHook);
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(w, m, wp, lp);
}

int runSelfTests(const std::filesystem::path &report,
                 bool recorded); // Implemented in tests.hpp; no OS input is generated.
#include "tests.hpp"
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc == 3 && (std::wstring(argv[1]) == L"--self-test" || std::wstring(argv[1]) == L"--core-test")) {
    int result = runSelfTests(argv[2], std::wstring(argv[1]) == L"--self-test");
    LocalFree(argv);
    return result;
  }
  std::filesystem::path renderPath;
  if (argc == 3 && std::wstring(argv[1]) == L"--render-test") {
    ui.testing = true;
    renderPath = argv[2];
  }
  LocalFree(argv);
  HANDLE instanceMutex = nullptr;
  if (!ui.testing) {
    instanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\Luxware.Windows.Desktop");
    if (!instanceMutex)
      return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
      if (auto previous = FindWindowW(L"LuxwareWindows", nullptr)) {
        ShowWindow(previous, SW_RESTORE);
        SetForegroundWindow(previous);
      }
      CloseHandle(instanceMutex);
      return 0;
    }
    updates.load();
    ui.updateAfter = now() + 3;
  }
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
  InitCommonControlsEx(&common);
  if (!ui.testing)
    try {
      importPreset();
    } catch (...) {
      status("Preset import failed; settings will use defaults");
    }
  state.settings = ui.testing ? Settings{} : loadSettings();
  try {
    auto folder = dataFolder() / L"AvatarViews";
    for (int i = 0; i < 6; i++) {
      auto file = folder / (L"view" + std::to_wstring(i) + L".bmp");
      if (std::filesystem::exists(file)) {
        auto im = loadBMP(file);
        state.own.push_back(describe(im, {0, 0, 1, 1}));
      }
    }
  } catch (...) {
    status("Could not load an avatar reference; mark yourself again");
  }
  WNDCLASSEXW cls{};
  cls.cbSize = sizeof(cls);
  cls.lpfnWndProc = windowProc;
  cls.hInstance = instance;
  cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
  cls.lpszClassName = L"LuxwareWindows";
  RegisterClassExW(&cls);
  cls.lpfnWndProc = settingsProc;
  cls.lpszClassName = L"LuxwareSettings";
  RegisterClassExW(&cls);
  HWND window = CreateWindowExW(0, L"LuxwareWindows", L"Luxware", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, 1240, 930, nullptr, nullptr, instance, nullptr);
  if (!window) {
    MessageBoxW(nullptr, L"Luxware could not create its window.", L"Luxware", MB_ICONERROR);
    return 1;
  }
  if (ui.testing) {
    try {
      Model model;
      model.load(appFolder() / L"Models/LuxwarePlayers.onnx", false);
      auto image = std::make_shared<Image>(loadBMP(appFolder() / L"Tests/Fixtures/frame16.bmp"));
      double start = now();
      auto detections = model.run(*image, .4);
      Tracker tracker;
      for (auto &d : detections)
        d.feature = describe(*image, d.box);
      auto tracks = tracker.update(detections, start, false);
      auto target = tracker.choose(tracks, start, .71, false);
      std::optional<Detection> selected;
      if (target)
        selected = target->d;
      detections.clear();
      for (auto &track : tracks)
        detections.push_back(track.d);
      state.view = {image,    detections,
                    selected, {0, 0, double(image->w), double(image->h)},
                    start,    (now() - start) * 1000,
                    0,        "Actual recorded-image model check"};
      state.provider = model.provider;
      state.method = "Recorded test image; not live screen capture";
    } catch (const std::exception &e) {
      state.modelError = e.what();
    }
    SetWindowTextW(GetDlgItem(window, BIND), (L"Bind: " + bindingName(state.settings)).c_str());
    status("Owned UI render: actual recorded-image inference; no capture, input or hooks");
    renderOwnWindow(renderPath);
    DestroyWindow(window);
    return 0;
  }
  ui.work = std::thread(worker, std::ref(state));
  ui.input = std::thread([] { inputLoop(state); });
  ShowWindow(window, show);
  UpdateWindow(window);
  MSG message;
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  state.quit = true;
  state.armed = false;
  if (ui.input.joinable())
    ui.input.join();
  if (ui.work.joinable())
    ui.work.join();
  save();
  DeleteObject(ui.font);
  DeleteObject(ui.big);
  DeleteObject(ui.mono);
  DeleteObject(ui.background);
  DeleteObject(ui.editBrush);
  CoUninitialize();
  if (instanceMutex)
    CloseHandle(instanceMutex);
  return 0;
}
