#pragma once
#include "capture.hpp"
#include "model.hpp"
#include <mmsystem.h>

namespace lux {
constexpr ULONG_PTR eventTag = 0x4c585752;
struct ViewState {
  std::shared_ptr<Image> image;
  std::vector<Detection> detections;
  std::optional<Detection> target;
  Rect client;
  double received = 0, ms = 0, fps = 0;
  std::string note = "Choose a window, then Start scan";
};
struct Shared {
  std::mutex mutex;
  Settings settings;
  ViewState view;
  std::vector<Feature> own;
  std::string provider = "Loading model", modelError, inputError, method = "Waiting",
              levelStatus = "Look level, then Ctrl+Shift+L";
  double cps = 0, moves = 0, manualY = 0;
  bool manualMoved = false;
  std::atomic<bool> quit{false}, scan{false}, armed{false}, modelReady{false};
  std::atomic<bool> bindingHeld{false};
  std::atomic<HWND> source{nullptr};
  std::atomic<unsigned> reset{0}, reload{0}, calibrate{0};
  std::atomic<double> heartbeat{0};
  Settings config() {
    std::lock_guard<std::mutex> l(mutex);
    return settings;
  }
  void clearTarget() {
    std::lock_guard<std::mutex> l(mutex);
    view.target.reset();
  }
  void stop() {
    armed = false;
    bindingHeld = false;
    scan = false;
    reset++;
    clearTarget();
  }
  void physical(double y) {
    std::lock_guard<std::mutex> l(mutex);
    manualMoved = true;
    manualY += y;
  }
};
inline bool ownBody(const Feature &f, const std::vector<Feature> &refs) {
  for (auto &r : refs)
    if (distance(f, r) <= .55)
      return true;
  return false;
}

// Saved-self pixel matches can continue briefly through a small appearance change.
// The continuation only rejects overlapping current proposals; it never creates an aiming target.
class SelfGuard {
  struct Observation {
    Rect box;
    Feature feature;
    double time;
  };
  std::vector<Observation> memory;

public:
  void reset() {
    memory.clear();
  }
  std::vector<Detection> filter(const std::vector<Detection> &proposals, const std::vector<Feature> &refs,
                                double t) {
    if (refs.empty()) {
      reset();
      return proposals;
    }
    std::vector<Observation> previous;
    for (auto &m : memory)
      if (t >= m.time && t - m.time <= .15)
        previous.push_back(m);
    std::vector<Observation> current;
    std::vector<Detection> accepted;
    for (auto &d : proposals) {
      if (!d.feature)
        continue;
      bool own = ownBody(*d.feature, refs);
      if (!own)
        for (auto &m : previous)
          if (overlap(d.box, m.box) >= .5 && distance(*d.feature, m.feature) < .65) {
            own = true;
            break;
          }
      if (own) {
        if (current.size() < 8)
          current.push_back({d.box, *d.feature, t});
      } else
        accepted.push_back(d);
    }
    memory = current.empty() ? previous : current;
    return accepted;
  }
};

// A brief current-image template check, never a predicted box for invisible pixels.
class VisualBridge {
  std::optional<Detection> last;
  double confirmed = 0, lastTime = 0;
  std::array<float, 16 * 24> sample{};
  static std::array<float, 16 * 24> patch(const Image &im, Rect box) {
    std::array<float, 16 * 24> values{};
    for (int y = 0; y < 24; y++)
      for (int x = 0; x < 16; x++) {
        auto c = pixel(im, (box.x + (x + .5) / 16 * box.w) * im.w - .5,
                       (box.y + (y + .5) / 24 * box.h) * im.h - .5);
        values[y * 16 + x] = c[0] * .299f + c[1] * .587f + c[2] * .114f;
      }
    return values;
  }
  static double correlation(const std::array<float, 384> &a, const std::array<float, 384> &b) {
    double ma = 0, mb = 0;
    for (int i = 0; i < 384; i++) {
      ma += a[i] / 384;
      mb += b[i] / 384;
    }
    double xx = 0, yy = 0, xy = 0;
    for (int i = 0; i < 384; i++) {
      double x = a[i] - ma, y = b[i] - mb;
      xx += x * x;
      yy += y * y;
      xy += x * y;
    }
    return xx > 1e-4 && yy > 1e-4 ? xy / std::sqrt(xx * yy) : -1;
  }

public:
  void reset() {
    last.reset();
    confirmed = lastTime = 0;
  }
  void confirm(const Image &im, const Detection &d, double t) {
    last = d;
    sample = patch(im, d.box);
    confirmed = lastTime = t;
  }
  std::optional<Detection> follow(const Image &im, double t, const std::vector<Feature> &own) {
    if (!last || t < confirmed || t - confirmed > .20 || t - lastTime > .125) {
      reset();
      return {};
    }
    double best = -1;
    Rect found;
    for (double scale : {.9, 1., 1.1})
      for (int y = -3; y <= 3; y++)
        for (int x = -3; x <= 3; x++) {
          Rect b{last->box.cx() + x * .018 - last->box.w * scale / 2,
                 last->box.cy() + y * .018 - last->box.h * scale / 2, last->box.w * scale,
                 last->box.h * scale};
          auto v = visible(b);
          if (!v || std::abs(v->area() - b.area()) > 1e-6)
            continue;
          double score = correlation(sample, patch(im, b));
          if (score > best) {
            best = score;
            found = b;
          }
        }
    if (best < .88) {
      reset();
      return {};
    }
    Feature f = describe(im, found);
    if (!last->feature || distance(f, *last->feature) > .25 || ownBody(f, own)) {
      reset();
      return {};
    }
    Detection result{found, float(best), last->id, true, f};
    last = result;
    lastTime = t;
    return result;
  }
};

inline void worker(Shared &shared) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  {
    Capture capture;
    Model model;
    Tracker tracker;
    SelfGuard selfGuard;
    Rates fps;
    unsigned reset = shared.reset.load(), reload = ~0u;
    bool previousSingle = false;
    double nextAnalysis = 0;
    while (!shared.quit) {
      Settings s = shared.config();
      unsigned wanted = shared.reload.load();
      if (wanted != reload) {
        reload = wanted;
        shared.modelReady = false;
        tracker.reset();
        selfGuard.reset();
        try {
          auto path = s.modelPath.empty() ? appFolder() / L"Models/LuxwarePlayers.onnx"
                                          : std::filesystem::path(s.modelPath);
          model.load(path, s.gpu);
          shared.modelReady = true;
          std::lock_guard<std::mutex> l(shared.mutex);
          shared.provider = model.provider;
          shared.modelError.clear();
        } catch (const std::exception &e) {
          std::lock_guard<std::mutex> l(shared.mutex);
          shared.provider = "Model unavailable";
          shared.modelError = e.what();
        }
      }
      if (reset != shared.reset.load() || previousSingle != s.single) {
        reset = shared.reset.load();
        previousSingle = s.single;
        tracker.reset();
        selfGuard.reset();
      }
      HWND source = shared.source.load();
      double started = now();
      if (!shared.scan || !source || !front(source) || started < nextAnalysis) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        continue;
      }
      try {
        Rect client = clientRect(source);
        auto image = capture.grab(source, client);
        if (!image || !image->valid()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(15));
          continue;
        }
        // Capture time is recorded before analysis, not falsely refreshed after a slow inference.
        double received = now();
        std::vector<Feature> own;
        {
          std::lock_guard<std::mutex> l(shared.mutex);
          own = shared.own;
        }
        std::vector<Detection> detections;
        if (shared.modelReady && s.aim) {
          detections = model.run(*image, s.confidence);
          for (auto &d : detections)
            d.feature = describe(*image, d.box);
          detections = selfGuard.filter(detections, own, received);
        }
        auto tracks = tracker.update(detections, received, s.single);
        std::optional<Track> chosen;
        if (!own.empty())
          chosen = tracker.choose(tracks, received, s.fov, s.single);
        std::optional<Detection> target;
        if (chosen)
          target = chosen->d; // Only current neural detections may drive input.
        detections.clear();
        for (auto &t : tracks)
          detections.push_back(t.d);
        if (target && target->visual)
          detections.push_back(*target);
        std::string note = own.empty() ? "Mark your own avatar once before enabling aim" : tracker.note;
        if (!s.aim)
          note = "Clicker only: real screen capture active; detection and aiming paused";
        fps.add(received);
        double finished = now();
        nextAnalysis = finished + analysisDelay(finished - started, s.fps,
                                                s.aim && model.provider.find("CPU") == 0, s.gameFriendly);
        if (shared.reset.load() != reset || shared.source.load() != source || !shared.scan ||
            shared.reload.load() != reload)
          continue;
        {
          std::lock_guard<std::mutex> l(shared.mutex);
          shared.view = {image,
                         std::move(detections),
                         target,
                         client,
                         received,
                         (finished - started) * 1000,
                         fps.fps(received),
                         note};
          shared.method = capture.method;
        }
      } catch (const std::exception &e) {
        std::lock_guard<std::mutex> l(shared.mutex);
        shared.view.target.reset();
        shared.view.note = e.what();
      }
    }
  }
  CoUninitialize();
}

using EventSink = std::function<UINT(INPUT &)>;
inline void inputLoop(
    Shared &shared, EventSink sink = [](INPUT &i) { return SendInput(1, &i, sizeof(i)); },
    std::function<bool(HWND)> foreground = front,
    std::function<POINT()> cursor =
        []() {
          POINT p{};
          GetCursorPos(&p);
          return p;
        }) {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  ClickClock clock;
  bool osHeld = false, heldRight = false;
  Rates clicks, moves;
  AimMemory correction;
  MotionHistory motion;
  FrameLead lead;
  HWND motionSource = nullptr;
  bool motionCamera = true;
  double motionWidth = 0, motionHeight = 0;
  double nextMove = 0, lastHz = 0, lastRates = 0;
  auto &frameTime = correction.frameTime, &appliedX = correction.appliedX, &appliedY = correction.appliedY,
       &filterX = correction.filterX, &filterY = correction.filterY, &lastMotion = correction.lastMotion;
  auto send = [&](DWORD flags, LONG x, LONG y) {
    INPUT i{};
    i.type = INPUT_MOUSE;
    i.mi.dwFlags = flags;
    i.mi.dx = x;
    i.mi.dy = y;
    i.mi.dwExtraInfo = eventTag;
    UINT accepted = sink(i);
    {
      std::lock_guard<std::mutex> l(shared.mutex);
      if (accepted != 1)
        shared.inputError = "SendInput rejected an event (" + std::to_string(GetLastError()) +
                            "). See README for process privilege requirements.";
      else
        shared.inputError.clear();
    }
    return accepted == 1;
  };
  auto release = [&]() {
    if (osHeld && send(heldRight ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP, 0, 0)) {
      osHeld = false;
      clock.held = false;
    }
  };
  timeBeginPeriod(1);
  HANDLE waitable =
      CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (waitable) {
    LARGE_INTEGER due;
    due.QuadPart = -10000;
    if (!SetWaitableTimer(waitable, &due, 0, nullptr, nullptr, FALSE)) {
      CloseHandle(waitable);
      waitable = nullptr;
    }
  }
  while (!shared.quit) {
    double t = now();
    Settings s;
    Rect client;
    std::optional<Detection> target;
    double received;
    {
      std::lock_guard<std::mutex> l(shared.mutex);
      s = shared.settings;
      client = shared.view.client;
      target = shared.view.target;
      received = shared.view.received;
      shared.manualMoved = false;
      shared.manualY = 0;
    }
    POINT pointer = cursor();
    HWND source = shared.source.load();
    if (source != motionSource || client.w != motionWidth || client.h != motionHeight ||
        motionCamera != s.camera) {
      motion.clear();
      lead.clear();
      motionSource = source;
      motionCamera = s.camera;
      motionWidth = client.w;
      motionHeight = client.h;
    }
    t = now(); // Evaluate age after reading the shared frame, not before a possible scheduling delay.
    bool allowed = shared.armed && (!s.hold || shared.bindingHeld) && shared.scan && foreground(source) &&
                   t - shared.heartbeat.load() < .25 && t >= received && t - received < .5 && client.w > 0 &&
                   client.h > 0 && contains(client, pointer);
    if (osHeld && (!allowed || !s.clicks || heldRight != s.right || !clock.held || t >= clock.nextUp))
      release();
    if (!allowed) {
      clock.tick(t, false, s.cps, s.duty);
      nextMove = t;
      correction.clear();
      motion.clear();
      lead.clear();
    } else {
      auto action = osHeld ? ClickClock::none : clock.tick(t, s.clicks, s.cps, s.duty);
      if (action == ClickClock::down && !osHeld) {
        if (send(s.right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN, 0, 0)) {
          osHeld = true;
          heldRight = s.right;
          clicks.add(t);
        } else
          clock.held = false;
      }
      if (action == ClickClock::up)
        release();
      if (s.hz != lastHz) {
        nextMove = t;
        lastHz = s.hz;
      }
      if (t >= nextMove) {
        double interval = 1 / std::clamp(s.hz, 5., 240.);
        nextMove = (nextMove > 0 ? nextMove : t) + interval;
        if (nextMove <= t)
          nextMove = t + interval;
        bool valid = s.aim && target && !target->visual && target->score >= s.confidence &&
                     t - received < .2 && s.strength > 0;
        if (valid) {
          double centerX = client.x + client.w / 2, centerY = client.y + client.h / 2;
          correction.observe(target->id, s.camera, received);
          double ex = client.x + target->box.cx() * client.w - (s.camera ? centerX : pointer.x),
                 ey = client.y + target->box.cy() * client.h - (s.camera ? centerY : pointer.y);
          lead.observe(target->id, s.camera, received, target->box.cx() * client.w - client.w / 2,
                       target->box.cy() * client.h - client.h / 2, motion);
          auto forecast =
              lead.offset(t - received, s.prediction, std::clamp(target->box.w * client.w * .5, 8., 60.),
                          std::clamp(target->box.h * client.h * .35, 8., 60.));
          ex += forecast.first;
          ey += forecast.second;
          if (s.camera) {
            auto inFlight = motion.after(received, t);
            ex -= inFlight.first;
            ey -= inFlight.second;
          }
          double dt = lastMotion ? std::min(.1, t - lastMotion) : interval;
          lastMotion = t;
          double alpha = s.smoothing == 0 ? 1 : 1 - std::exp(-dt / (.002 + s.smoothing * .1));
          filterX += alpha * (ex - filterX);
          filterY += alpha * (ey - filterY);
          double dist = std::hypot(filterX, filterY),
                 scale = std::min(1., 180 / std::max(.001, dist)) * s.strength, dx = filterX * scale,
                 dy = filterY * scale;
          if (s.camera) {
            auto bound = [](double p, double r) {
              return p * r > 0 ? (r > 0 ? std::min(p, r) : std::max(p, r)) : 0;
            };
            dx = bound(dx, ex);
            dy = bound(dy, ey);
          }
          if (dist > 3) {
            if (s.camera) {
              LONG x = LONG(std::lround(dx * s.sensitivity)), y = LONG(std::lround(dy * s.sensitivity));
              if ((x || y) && send(MOUSEEVENTF_MOVE | MOUSEEVENTF_MOVE_NOCOALESCE, x, y)) {
                moves.add(t);
                appliedX += x / s.sensitivity;
                appliedY += y / s.sensitivity;
                motion.record(now(), x / s.sensitivity, y / s.sensitivity);
              }
            } else {
              double x = std::clamp(pointer.x + dx, client.x + 1, client.x + client.w - 1),
                     y = std::clamp(pointer.y + dy, client.y + 1, client.y + client.h - 1);
              int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN),
                  vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
              LONG ax = LONG(std::lround((x - vx) * 65535 / std::max(1, vw - 1))),
                   ay = LONG(std::lround((y - vy) * 65535 / std::max(1, vh - 1)));
              if (send(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK, ax, ay))
                moves.add(t);
            }
          }
        } else {
          correction.clear();
          lead.clear();
          // No detections: release camera/cursor control completely. Click phases above continue.
        }
      }
    }
    if (t - lastRates > .04) {
      std::lock_guard<std::mutex> l(shared.mutex);
      shared.cps = clicks.events(t);
      shared.moves = moves.events(t);
      shared.levelStatus = "No player: mouse free; activated auto clicker continues";
      lastRates = t;
    }
    if (!shared.armed || !shared.scan)
      Sleep(10);
    else if (waitable) {
      LARGE_INTEGER due;
      due.QuadPart = -10000;
      if (SetWaitableTimer(waitable, &due, 0, nullptr, nullptr, FALSE))
        WaitForSingleObject(waitable, 5);
      else
        Sleep(1);
    } else
      Sleep(1);
  }
  for (int i = 0; osHeld && i < 10; i++) {
    release();
    if (osHeld)
      Sleep(1);
  }
  if (waitable)
    CloseHandle(waitable);
  timeEndPeriod(1);
}
} // namespace lux
