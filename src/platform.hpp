#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "core.hpp"
#include <atomic>
#include <commctrl.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <functional>
#include <gdiplus.h>
#include <memory>
#include <mutex>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>
#include <windowsx.h>

namespace lux {
inline std::wstring wide(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
  std::wstring out(n, L' ');
  if (n)
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
  return out;
}
inline std::string utf8(const std::wstring &s) {
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(n, ' ');
  if (n)
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}
inline std::filesystem::path appFolder() {
  std::vector<wchar_t> p(32768);
  DWORD n = GetModuleFileNameW(nullptr, p.data(), DWORD(p.size()));
  return std::filesystem::path(std::wstring(p.data(), n)).parent_path();
}
inline std::filesystem::path dataFolder() {
  PWSTR p = nullptr;
  std::filesystem::path out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
    out = p;
    CoTaskMemFree(p);
  } else
    out = appFolder();
  out /= L"Luxware";
  std::filesystem::create_directories(out);
  return out;
}
inline void importPreset() {
  auto user = dataFolder(), preset = appFolder() / L"Profiles";
  if (std::filesystem::exists(user / L"settings.ini"))
    return;
  if (std::filesystem::exists(preset / L"settings.ini"))
    std::filesystem::copy_file(preset / L"settings.ini", user / L"settings.ini");
  std::filesystem::create_directories(user / L"AvatarViews");
  for (int i = 0; i < 6; i++) {
    auto name = L"view" + std::to_wstring(i) + L".bmp";
    auto from = preset / L"AvatarViews" / name, to = user / L"AvatarViews" / name;
    if (std::filesystem::exists(from) && !std::filesystem::exists(to))
      std::filesystem::copy_file(from, to);
  }
}
inline Rect clientRect(HWND window) {
  RECT r{};
  POINT p{};
  if (!IsWindow(window) || !GetClientRect(window, &r) || !ClientToScreen(window, &p))
    return {};
  return {double(p.x), double(p.y), double(r.right), double(r.bottom)};
}
inline bool front(HWND window) {
  return window && GetAncestor(GetForegroundWindow(), GA_ROOT) == window && !IsIconic(window);
}
inline std::wstring label(HWND window) {
  wchar_t text[512]{};
  GetWindowTextW(window, text, 512);
  return text;
}
inline bool contains(Rect r, POINT p) {
  return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h;
}
template <class T> struct Com {
  T *p = nullptr;
  ~Com() {
    reset();
  }
  Com() = default;
  Com(const Com &) = delete;
  Com &operator=(const Com &) = delete;
  void reset() {
    if (p) {
      p->Release();
      p = nullptr;
    }
  }
  T **put() {
    reset();
    return &p;
  }
  T *operator->() const {
    return p;
  }
  operator bool() const {
    return p != nullptr;
  }
};
class PaintBuffer {
  HDC dc = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ original = nullptr;
  int width = 0, height = 0;

public:
  PaintBuffer() = default;
  PaintBuffer(const PaintBuffer &) = delete;
  PaintBuffer &operator=(const PaintBuffer &) = delete;
  ~PaintBuffer() {
    reset();
  }
  void reset() {
    if (dc && original)
      SelectObject(dc, original);
    if (bitmap)
      DeleteObject(bitmap);
    if (dc)
      DeleteDC(dc);
    dc = nullptr;
    bitmap = nullptr;
    original = nullptr;
    width = height = 0;
  }
  HDC get(HDC target, int w, int h) {
    w = std::max(1, w);
    h = std::max(1, h);
    if (dc && width == w && height == h)
      return dc;
    reset();
    dc = CreateCompatibleDC(target);
    if (!dc)
      return nullptr;
    bitmap = CreateCompatibleBitmap(target, w, h);
    if (!bitmap) {
      reset();
      return nullptr;
    }
    original = SelectObject(dc, bitmap);
    width = w;
    height = h;
    return dc;
  }
};
struct Settings {
  bool camera = true, hold = false, aim = true, clicks = true, right = false, single = false, level = true,
       gpu = true, gameFriendly = true;
  double cps = 23.43, duty = .4, strength = 1, smoothing = 0, sensitivity = 1, fov = .5, confidence = .4,
         prediction = 0, fps = 120, hz = 240;
  int bindKind = 0, bindCode = VK_F6, bindMods = 0;
  int avatarNext = 0;
  std::wstring modelPath;
};
inline bool reservedBinding(int kind, int code, int mods) {
  return kind == 0 && (((mods & 3) == 3 && code == 'R') || ((mods & 5) == 5 && code == VK_ESCAPE));
}
inline Settings loadSettings() {
  Settings s;
  auto file = (dataFolder() / L"settings.ini").wstring();
  auto num = [&](const wchar_t *key, double value) {
    wchar_t b[100]{};
    GetPrivateProfileStringW(L"Luxware", key, std::to_wstring(value).c_str(), b, 100, file.c_str());
    try {
      double v = std::stod(b);
      return std::isfinite(v) ? v : value;
    } catch (...) {
      return value;
    }
  };
  s.camera = num(L"camera", 1) != 0;
  s.hold = num(L"hold", 0) != 0;
  s.aim = num(L"aim", 1) != 0;
  s.clicks = num(L"clicks", 1) != 0;
  s.right = num(L"right", 0) != 0;
  s.single = num(L"single", 0) != 0;
  s.level = num(L"level", 1) != 0;
  s.gpu = num(L"gpu", 1) != 0;
  s.gameFriendly = num(L"gameFriendly", 1) != 0;
  s.cps = std::clamp(num(L"cps", 23.43), .1, 200.);
  s.duty = std::clamp(num(L"duty", .4), .01, .99);
  s.strength = std::clamp(num(L"strength", 1), 0., 1.);
  s.smoothing = std::clamp(num(L"smoothing", 0), 0., .95);
  s.sensitivity = std::clamp(num(L"sensitivity", 1), .1, 3.);
  s.fov = std::clamp(num(L"fov", .5), .05, .71);
  s.confidence = std::clamp(num(L"confidence", .4), .1, .99);
  s.prediction = std::clamp(num(L"prediction", 0), 0., .1);
  s.fps = std::clamp(num(L"fps", 120), 5., 240.);
  s.hz = std::clamp(num(L"hz", 240), 5., 240.);
  s.bindKind = int(num(L"bindKind", 0));
  s.bindCode = int(num(L"bindCode", VK_F6));
  s.bindMods = int(num(L"bindMods", 0)) & 15;
  s.avatarNext = int(num(L"avatarNext", 0)) % 6;
  if ((s.bindKind != 0 && s.bindKind != 1) || (s.bindKind == 0 && (s.bindCode < 1 || s.bindCode > 255)) ||
      (s.bindKind == 1 && (s.bindCode < 0 || s.bindCode > 4))) {
    s.bindKind = 0;
    s.bindCode = VK_F6;
    s.bindMods = 0;
  }
  wchar_t model[32768]{};
  GetPrivateProfileStringW(L"Luxware", L"model", L"", model, 32768, file.c_str());
  s.modelPath = model;
  return s;
}
inline void saveSettings(const Settings &s) {
  auto file = (dataFolder() / L"settings.ini").wstring();
  auto put = [&](const wchar_t *k, double v) {
    WritePrivateProfileStringW(L"Luxware", k, std::to_wstring(v).c_str(), file.c_str());
  };
  put(L"camera", s.camera);
  put(L"hold", s.hold);
  put(L"aim", s.aim);
  put(L"clicks", s.clicks);
  put(L"right", s.right);
  put(L"single", s.single);
  put(L"level", s.level);
  put(L"gpu", s.gpu);
  put(L"gameFriendly", s.gameFriendly);
  put(L"cps", s.cps);
  put(L"duty", s.duty);
  put(L"strength", s.strength);
  put(L"smoothing", s.smoothing);
  put(L"sensitivity", s.sensitivity);
  put(L"fov", s.fov);
  put(L"confidence", s.confidence);
  put(L"prediction", s.prediction);
  put(L"fps", s.fps);
  put(L"hz", s.hz);
  put(L"bindKind", s.bindKind);
  put(L"bindCode", s.bindCode);
  put(L"bindMods", s.bindMods);
  put(L"avatarNext", s.avatarNext);
  WritePrivateProfileStringW(L"Luxware", L"model", s.modelPath.c_str(), file.c_str());
}
} // namespace lux
