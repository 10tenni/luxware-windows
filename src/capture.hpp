#pragma once
#include "platform.hpp"

namespace lux {
class Capture {
  Com<IDXGIOutputDuplication> duplicate;
  Com<ID3D11Device> device;
  Com<ID3D11DeviceContext> context;
  Com<ID3D11Texture2D> staging;
  HMONITOR monitor = nullptr;
  RECT monitorRect{};
  int textureW = 0, textureH = 0;
  bool attempted = false;
  void initialize(HMONITOR wanted) {
    duplicate.reset();
    staging.reset();
    context.reset();
    device.reset();
    monitor = wanted;
    attempted = true;
    Com<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory.put()))))
      return;
    for (UINT a = 0;; a++) {
      Com<IDXGIAdapter1> adapter;
      if (FAILED(factory->EnumAdapters1(a, adapter.put())))
        break;
      for (UINT o = 0;; o++) {
        Com<IDXGIOutput> output;
        if (FAILED(adapter->EnumOutputs(o, output.put())))
          break;
        DXGI_OUTPUT_DESC desc{};
        if (FAILED(output->GetDesc(&desc)) || desc.Monitor != wanted)
          continue;
        if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED)
          return;
        D3D_FEATURE_LEVEL level;
        if (FAILED(D3D11CreateDevice(adapter.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                     device.put(), &level, context.put())))
          return;
        Com<IDXGIOutput1> newer;
        if (FAILED(output->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void **>(newer.put()))))
          return;
        if (FAILED(newer->DuplicateOutput(device.p, duplicate.put())))
          return;
        monitorRect = desc.DesktopCoordinates;
        return;
      }
    }
  }
  std::shared_ptr<Image> gdi(Rect r) {
    int w = int(r.w), h = int(r.h);
    if (w <= 0 || h <= 0 || w > 10000 || h > 10000)
      return {};
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen);
    void *pixels = nullptr;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap) {
      DeleteDC(memory);
      ReleaseDC(nullptr, screen);
      return {};
    }
    auto previous = SelectObject(memory, bitmap);
    BOOL ok = BitBlt(memory, 0, 0, w, h, screen, int(r.x), int(r.y), SRCCOPY | CAPTUREBLT);
    std::shared_ptr<Image> out;
    if (ok) {
      out = std::make_shared<Image>();
      out->w = w;
      out->h = h;
      out->bgra.resize(size_t(w) * h * 4);
      std::memcpy(out->bgra.data(), pixels, out->bgra.size());
    }
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return out;
  }

public:
  std::string method = "Waiting for capture";
  std::shared_ptr<Image> grab(HWND source, Rect r) {
    // Desktop capture contains visible pixels. Never scan an occluding app/window.
    if (!front(source))
      return {};
    HMONITOR wanted = MonitorFromWindow(source, MONITOR_DEFAULTTONEAREST);
    if (!attempted || monitor != wanted)
      initialize(wanted);
    if (duplicate && r.x >= monitorRect.left && r.y >= monitorRect.top && r.x + r.w <= monitorRect.right &&
        r.y + r.h <= monitorRect.bottom) {
      DXGI_OUTDUPL_FRAME_INFO frame{};
      Com<IDXGIResource> resource;
      HRESULT hr = duplicate->AcquireNextFrame(10, &frame, resource.put());
      if (SUCCEEDED(hr)) {
        struct Release {
          IDXGIOutputDuplication *d;
          ~Release() {
            d->ReleaseFrame();
          }
        } release{duplicate.p};
        Com<ID3D11Texture2D> texture;
        if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
                                               reinterpret_cast<void **>(texture.put())))) {
          D3D11_TEXTURE2D_DESC desc{};
          texture->GetDesc(&desc);
          if (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM && r.x - monitorRect.left + r.w <= desc.Width &&
              r.y - monitorRect.top + r.h <= desc.Height) {
            if (!staging || textureW != int(r.w) || textureH != int(r.h)) {
              desc.Width = UINT(r.w);
              desc.Height = UINT(r.h);
              desc.Usage = D3D11_USAGE_STAGING;
              desc.BindFlags = 0;
              desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
              desc.MiscFlags = 0;
              staging.reset();
              if (SUCCEEDED(device->CreateTexture2D(&desc, nullptr, staging.put()))) {
                textureW = desc.Width;
                textureH = desc.Height;
              }
            }
            if (staging) {
              const UINT x = UINT(r.x - monitorRect.left), y = UINT(r.y - monitorRect.top);
              D3D11_BOX region{x, y, 0, x + UINT(r.w), y + UINT(r.h), 1};
              context->CopySubresourceRegion(staging.p, 0, 0, 0, 0, texture.p, 0, &region);
              D3D11_MAPPED_SUBRESOURCE map{};
              if (SUCCEEDED(context->Map(staging.p, 0, D3D11_MAP_READ, 0, &map))) {
                auto out = std::make_shared<Image>();
                out->w = int(r.w);
                out->h = int(r.h);
                out->bgra.resize(size_t(out->w) * out->h * 4);
                for (int row = 0; row < out->h; row++)
                  std::memcpy(out->bgra.data() + size_t(row) * out->w * 4,
                              static_cast<uint8_t *>(map.pData) + size_t(row) * map.RowPitch,
                              size_t(out->w) * 4);
                context->Unmap(staging.p, 0);
                method = "DXGI Desktop Duplication";
                return out;
              }
            }
          }
        }
      } else if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
        duplicate.reset();
        attempted = false;
      }
    }
    // Also reads actual current pixels when the desktop has not presented a new DXGI frame.
    auto result = gdi(r);
    if (result)
      method = "GDI screen capture";
    return result;
  }
};
} // namespace lux
