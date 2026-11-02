#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lux {
inline double now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Rect {
  double x = 0, y = 0, w = 0, h = 0;
  double cx() const {
    return x + w / 2;
  }
  double cy() const {
    return y + h / 2;
  }
  double area() const {
    return std::max(0.0, w) * std::max(0.0, h);
  }
};
inline double overlap(Rect a, Rect b) {
  double area = std::max(0.0, std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x)) *
                std::max(0.0, std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y));
  return area / std::max(1e-9, a.area() + b.area() - area);
}
inline std::optional<Rect> visible(Rect b) {
  if (!std::isfinite(b.x + b.y + b.w + b.h) || b.w <= 0 || b.h <= 0 || b.w > 1.25 || b.h > 1.25)
    return {};
  Rect r{std::max(0.0, b.x), std::max(0.0, b.y), 0, 0};
  r.w = std::min(1.0, b.x + b.w) - r.x;
  r.h = std::min(1.0, b.y + b.h) - r.y;
  if (r.w <= .004 || r.h <= .008 || r.area() / b.area() < .35)
    return {};
  return r;
}
struct Image {
  int w = 0, h = 0;
  std::vector<uint8_t> bgra;
  bool valid() const {
    return w > 0 && h > 0 && bgra.size() == size_t(w) * h * 4;
  }
};
inline std::array<float, 3> pixel(const Image &im, double x, double y) {
  x = std::clamp(x, 0.0, double(im.w - 1));
  y = std::clamp(y, 0.0, double(im.h - 1));
  int a = int(x), b = int(y), c = std::min(a + 1, im.w - 1), d = std::min(b + 1, im.h - 1);
  double fx = x - a, fy = y - b;
  std::array<float, 3> out{};
  for (int ch = 0; ch < 3; ch++) {
    auto p = [&](int u, int v) { return im.bgra[(size_t(v) * im.w + u) * 4 + (2 - ch)] / 255.0; };
    out[ch] =
        float((p(a, b) * (1 - fx) + p(c, b) * fx) * (1 - fy) + (p(a, d) * (1 - fx) + p(c, d) * fx) * fy);
  }
  return out;
}
inline Image crop(const Image &im, Rect r) {
  int x = std::clamp(int(std::floor(r.x * im.w)), 0, im.w - 1),
      y = std::clamp(int(std::floor(r.y * im.h)), 0, im.h - 1);
  int right = std::clamp(int(std::ceil((r.x + r.w) * im.w)), x + 1, im.w),
      bottom = std::clamp(int(std::ceil((r.y + r.h) * im.h)), y + 1, im.h);
  Image out;
  out.w = right - x;
  out.h = bottom - y;
  out.bgra.resize(size_t(out.w) * out.h * 4);
  for (int row = 0; row < out.h; row++)
    std::memcpy(out.bgra.data() + size_t(row) * out.w * 4, im.bgra.data() + (size_t(y + row) * im.w + x) * 4,
                size_t(out.w) * 4);
  return out;
}
inline std::vector<float> tensor(const Image &im) {
  if (!im.valid())
    throw std::runtime_error("Invalid input image");
  std::vector<float> out(3 * 640 * 640);
  for (int y = 0; y < 640; y++)
    for (int x = 0; x < 640; x++) {
      auto p = pixel(im, (x + .5) * im.w / 640.0 - .5, (y + .5) * im.h / 640.0 - .5);
      for (int c = 0; c < 3; c++)
        out[size_t(c) * 640 * 640 + y * 640 + x] = p[c];
    }
  return out;
}

// Same scaleFill bilinear sampling as tensor(), with reusable storage and cached coordinates.
class Tensorizer {
  struct Axis {
    int low, high;
    double fraction;
  };
  std::array<Axis, 640> xs{}, ys{};
  int width = 0, height = 0;
  std::vector<float> values = std::vector<float>(3 * 640 * 640);
  static void axis(std::array<Axis, 640> &out, int size) {
    for (int i = 0; i < 640; ++i) {
      double p = std::clamp((i + .5) * size / 640.0 - .5, 0., double(size - 1));
      int low = int(p);
      out[i] = {low, std::min(low + 1, size - 1), p - low};
    }
  }

public:
  std::vector<float> &prepare(const Image &im) {
    if (!im.valid())
      throw std::runtime_error("Invalid input image");
    if (width != im.w || height != im.h) {
      axis(xs, im.w);
      axis(ys, im.h);
      width = im.w;
      height = im.h;
    }
    constexpr size_t plane = 640 * 640;
    for (int y = 0; y < 640; ++y) {
      auto a = im.bgra.data() + size_t(ys[y].low) * im.w * 4;
      auto b = im.bgra.data() + size_t(ys[y].high) * im.w * 4;
      double fy = ys[y].fraction;
      for (int x = 0; x < 640; ++x) {
        int lo = xs[x].low * 4, hi = xs[x].high * 4;
        double fx = xs[x].fraction;
        size_t pos = size_t(y) * 640 + x;
        for (int c = 0; c < 3; ++c) {
          int ch = 2 - c;
          double top = a[lo + ch] * (1 - fx) + a[hi + ch] * fx;
          double bottom = b[lo + ch] * (1 - fx) + b[hi + ch] * fx;
          values[c * plane + pos] = float((top * (1 - fy) + bottom * fy) / 255.);
        }
      }
    }
    return values;
  }
};

inline double analysisDelay(double workSeconds, double requestedFPS, bool cpu, bool gameFriendly) {
  double cap = std::clamp(requestedFPS, 5., 240.);
  if (gameFriendly)
    cap = std::min(cap, cpu ? 30. : 60.);
  // No frame queue/catch-up. Allow idle time even when one inference exceeds the cap interval.
  return std::max(std::max(0., 1 / cap - workSeconds), gameFriendly ? workSeconds * .25 : 0.);
}

// Appearance is computed from current image pixels, not game state/coordinates.
using Feature = std::array<float, 76>;
inline Feature describe(const Image &im, Rect box) {
  Feature f{};
  constexpr int W = 24, H = 32;
  std::array<float, W * H> gray{};
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      auto p =
          pixel(im, (box.x + (x + .5) / W * box.w) * im.w - .5, (box.y + (y + .5) / H * box.h) * im.h - .5);
      int cell = (y / 8) * 3 + x / 8;
      for (int c = 0; c < 3; c++)
        f[cell * 3 + c] += p[c] / 64;
      float hi = std::max({p[0], p[1], p[2]}), lo = std::min({p[0], p[1], p[2]}), delta = hi - lo;
      float hue = 0, sat = hi > 0 ? delta / hi : 0;
      if (delta > .001f) {
        if (hi == p[0])
          hue = std::fmod((p[1] - p[2]) / delta + 6, 6.f);
        else if (hi == p[1])
          hue = (p[2] - p[0]) / delta + 2;
        else
          hue = (p[0] - p[1]) / delta + 4;
        hue /= 6;
      }
      f[36 + std::min(15, int(hue * 16))] += 1.f / (W * H);
      f[52 + std::min(7, int(sat * 8))] += 1.f / (W * H);
      f[60 + std::min(7, int(hi * 8))] += 1.f / (W * H);
      gray[y * W + x] = p[0] * .299f + p[1] * .587f + p[2] * .114f;
    }
  float total = 0;
  for (int y = 1; y < H - 1; y++)
    for (int x = 1; x < W - 1; x++) {
      float gx = gray[y * W + x + 1] - gray[y * W + x - 1],
            gy = gray[(y + 1) * W + x] - gray[(y - 1) * W + x];
      float mag = std::hypot(gx, gy), angle = std::atan2(gy, gx) + 3.1415927f;
      f[68 + std::min(7, int(angle / (2 * 3.1415927f) * 8))] += mag;
      total += mag;
    }
  for (int i = 68; i < 76; i++)
    f[i] /= std::max(.001f, total);
  return f;
}
inline double distance(const Feature &a, const Feature &b) {
  double spatial = 0, hist = 0, edges = 0;
  for (int i = 0; i < 76; i++) {
    double v = (a[i] - b[i]) * (a[i] - b[i]);
    if (i < 36)
      spatial += v / 36;
    else if (i < 68)
      hist += v;
    else
      edges += v;
  }
  return std::sqrt(spatial) * 1.2 + std::sqrt(hist) * .7 + std::sqrt(edges) * .25;
}
struct Detection {
  Rect box;
  float score = 0;
  int id = 0;
  bool visual = false;
  std::optional<Feature> feature;
};
inline std::vector<Detection> decode(const float *data, const std::vector<int64_t> &shape, double confidence,
                                     double iou = .45) {
  if (shape.size() != 3 || shape[0] != 1)
    throw std::runtime_error("Expected YOLO [1,5,N] output");
  bool channelFirst = shape[1] == 5;
  size_t count = channelFirst ? shape[2] : shape[1];
  if (!channelFirst && shape[2] != 5)
    throw std::runtime_error("Expected single-class decoded [1,5,N] or [1,N,5] output");
  if (count > 100000)
    throw std::runtime_error("Unexpected model proposal count");
  auto value = [&](size_t n, int c) { return data[channelFirst ? size_t(c) * count + n : n * 5 + c]; };
  std::vector<Detection> candidates;
  for (size_t i = 0; i < count; i++) {
    float score = value(i, 4);
    if (!std::isfinite(score) || score < confidence || score > 1.001f)
      continue;
    double w = value(i, 2) / 640.0, h = value(i, 3) / 640.0;
    auto b = visible({value(i, 0) / 640.0 - w / 2, value(i, 1) / 640.0 - h / 2, w, h});
    if (b)
      candidates.push_back({*b, score});
  }
  std::sort(candidates.begin(), candidates.end(), [](auto &a, auto &b) { return a.score > b.score; });
  std::vector<Detection> kept;
  for (auto &d : candidates) {
    bool duplicate = false;
    for (auto &k : kept)
      if (overlap(d.box, k.box) > iou) {
        duplicate = true;
        break;
      }
    if (!duplicate)
      kept.push_back(d);
    if (kept.size() >= 80)
      break;
  }
  return kept;
}
struct Track {
  Detection d;
  double vx = 0, vy = 0, time = 0;
  unsigned confirmations = 1;
};
class Tracker {
  std::vector<Track> memory;
  int serial = 1;
  double lastUpdate = 0;
  std::optional<int> selected;
  std::optional<Track> reference;
  std::optional<Feature> anchor;

public:
  std::string note = "Waiting for detections";
  void reset() {
    memory.clear();
    lastUpdate = 0;
    selected.reset();
    reference.reset();
    anchor.reset();
    note = "Acquiring a fresh entity";
  }
  std::optional<int> selectedID() const {
    return selected;
  }
  std::vector<Track> update(const std::vector<Detection> &ds, double t, bool single) {
    std::vector<Track> old;
    for (auto &m : memory)
      if (t >= m.time && t - m.time < .35)
        old.push_back(m);
    if (single && reference && t - reference->time <= 2 &&
        std::none_of(old.begin(), old.end(), [&](auto &m) { return m.d.id == reference->d.id; }))
      old.push_back(*reference);
    struct Pair {
      double score;
      int i, j;
    };
    std::vector<Pair> pairs;
    std::vector<int> matched(ds.size(), -1);
    std::vector<bool> used(old.size(), false);
    for (int i = 0; i < int(ds.size()); i++)
      for (int j = 0; j < int(old.size()); j++) {
        auto &o = old[j];
        if (t - o.time >= .35)
          continue;
        double dt = std::clamp(t - o.time, 0.0, .1);
        double px = o.d.box.cx() + std::clamp(o.vx * dt, -.1, .1),
               py = o.d.box.cy() + std::clamp(o.vy * dt, -.15, .15);
        double dist = std::hypot(ds[i].box.cx() - px, ds[i].box.cy() - py),
               scale = std::max(ds[i].box.h, o.d.box.h) / std::max(.001, std::min(ds[i].box.h, o.d.box.h));
        double app = ds[i].feature && o.d.feature ? distance(*ds[i].feature, *o.d.feature) : 0;
        bool normal = dist < .15 && app < .95;
        bool jump = selected && o.d.id == *selected && t - o.time <= .18 &&
                    std::abs(ds[i].box.cx() - px) < .16 && std::abs(ds[i].box.cy() - py) < .32 &&
                    ds[i].feature && o.d.feature && app < .85;
        if ((normal || jump) && scale < 2.5)
          pairs.push_back({dist + app * .08, i, j});
      }
    if (single && selected) {
      int j = -1;
      for (int k = 0; k < int(old.size()); k++)
        if (old[k].d.id == *selected)
          j = k;
      if (j >= 0) {
        std::optional<Pair> best;
        for (auto &p : pairs)
          if (p.j == j && (!anchor || (ds[p.i].feature && distance(*anchor, *ds[p.i].feature) < .90)))
            if (!best || p.score < best->score)
              best = p;
        if (best) {
          matched[best->i] = j;
          used[j] = true;
        } else if (reference && reference->d.feature && anchor && t - reference->time <= 2) {
          std::vector<std::pair<double, int>> possibles;
          for (int i = 0; i < int(ds.size()); i++)
            if (ds[i].feature) {
              double recent = distance(*ds[i].feature, *reference->d.feature),
                     initial = distance(*ds[i].feature, *anchor);
              if (recent < .80 && initial < .85 &&
                  std::hypot(ds[i].box.cx() - reference->d.box.cx(), ds[i].box.cy() - reference->d.box.cy()) <
                      .65 &&
                  std::max(ds[i].box.h, reference->d.box.h) /
                          std::max(.001, std::min(ds[i].box.h, reference->d.box.h)) <
                      4)
                possibles.push_back({std::max(recent, initial), i});
            }
          std::sort(possibles.begin(), possibles.end());
          if (!possibles.empty() &&
              (possibles.size() == 1 || possibles[1].first - possibles[0].first >= .12)) {
            matched[possibles[0].second] = j;
            used[j] = true;
          }
        }
        pairs.erase(std::remove_if(pairs.begin(), pairs.end(), [&](auto &p) { return p.j == j; }),
                    pairs.end());
      }
    }
    std::sort(pairs.begin(), pairs.end(), [](auto &a, auto &b) { return a.score < b.score; });
    for (auto &p : pairs)
      if (matched[p.i] < 0 && !used[p.j]) {
        matched[p.i] = p.j;
        used[p.j] = true;
      }
    std::vector<Track> next;
    for (int i = 0; i < int(ds.size()); i++) {
      Track n{ds[i], 0, 0, t};
      int j = matched[i];
      if (j >= 0) {
        auto &o = old[j];
        double dt = std::max(.001, t - o.time);
        n.d.id = o.d.id;
        n.confirmations = dt <= .2 && o.time == lastUpdate ? std::min(100u, o.confirmations + 1) : 1;
        if (dt <= .18) {
          n.vx = (n.d.box.cx() - o.d.box.cx()) / dt * .5 + o.vx * .5;
          n.vy = (n.d.box.cy() - o.d.box.cy()) / dt * .5 + o.vy * .5;
        }
      } else
        n.d.id = serial++;
      next.push_back(n);
    }
    lastUpdate = t;
    memory = next;
    for (int j = 0; j < int(old.size()); j++)
      if (!used[j])
        memory.push_back(old[j]);
    return next;
  }
  std::optional<Track> choose(const std::vector<Track> &values, double t, double fov, bool single) {
    if (selected)
      for (auto &d : values)
        if (d.d.id == *selected && d.confirmations >= 2) {
          if (!d.d.visual) {
            reference = d;
            if (!anchor)
              anchor = d.d.feature;
          }
          note = "Locked entity #" + std::to_string(*selected);
          return d;
        }
    if (single && selected) {
      note = reference && t - reference->time <= 2 ? "Target lost: checking the same appearance"
                                                   : "Target lost: Reset Ctrl+Shift+R to choose again";
      return {};
    }
    const Track *best = nullptr;
    double nearest = 10;
    for (auto &d : values) {
      if (d.confirmations < 2)
        continue;
      double dist = std::hypot(d.d.box.cx() - .5, d.d.box.cy() - .5);
      if (dist <= fov && dist < nearest) {
        best = &d;
        nearest = dist;
      }
    }
    if (best) {
      selected = best->d.id;
      reference = *best;
      anchor = best->d.feature;
      note = "Locked entity #" + std::to_string(*selected);
      return *best;
    }
    selected.reset();
    reference.reset();
    anchor.reset();
    note = values.empty() ? "Waiting for eligible entity detections"
                          : "Confirming current player / checking FOV";
    return {};
  }
};

struct AimMemory {
  double frameTime = 0, appliedX = 0, appliedY = 0, filterX = 0, filterY = 0, lastMotion = 0;
  int targetID = 0;
  bool camera = true;
  void clear() {
    *this = AimMemory{};
  }
  void observe(int id, bool view, double received) {
    if (targetID != id || camera != view) {
      clear();
      targetID = id;
      camera = view;
    }
    if (frameTime != received) {
      frameTime = received;
      appliedX = appliedY = 0;
    }
  }
};
struct LevelReturn {
  bool calibrated = false, returning = false;
  double offset = 0, lastTarget = 0, began = 0;
  void reset() {
    *this = LevelReturn{};
  }
  void calibrate(double t) {
    reset();
    calibrated = true;
    lastTarget = t;
  }
  void record(double y) {
    if (calibrated && std::isfinite(y)) {
      offset += y;
      if (std::abs(offset) > 1200)
        reset();
    }
  }
  void manual(double y) {
    if (returning)
      reset();
    else
      record(y);
  }
  void seen(double t) {
    lastTarget = t;
    returning = false;
    began = 0;
  }
  double step(double t, double dt) {
    if (!calibrated || t - lastTarget < .35 || std::abs(offset) < 1) {
      returning = false;
      return 0;
    }
    if (!returning) {
      returning = true;
      began = t;
    }
    if (t - began > 1.5) {
      reset();
      return 0;
    }
    double limit = std::clamp(800 * dt, 1.0, 24.0), y = std::round(std::clamp(-offset, -limit, limit));
    offset += y;
    if (std::abs(offset) < 1) {
      offset = 0;
      returning = false;
    }
    return y;
  }
};
struct ClickClock {
  bool held = false;
  double nextDown = 0, nextUp = 0;
  enum Action { none, down, up };
  Action tick(double t, bool enabled, double cps, double duty) {
    if (held && (t >= nextUp || !enabled)) {
      held = false;
      return up;
    }
    if (!enabled) {
      nextDown = t;
      return none;
    }
    if (!held && t >= nextDown) {
      double interval = 1 / std::clamp(cps, .1, 200.0);
      double phase = nextDown > 0 ? nextDown : t;
      nextUp = t + interval * std::clamp(duty, .01, .99);
      nextDown = phase + interval;
      if (t - phase > interval * .1 || nextDown <= t)
        nextDown = t + interval;
      held = true;
      return down;
    }
    return none;
  }
};
struct Rates {
  std::deque<double> times;
  void add(double t) {
    times.push_back(t);
    prune(t);
  }
  void prune(double t) {
    while (!times.empty() && t - times.front() >= 2)
      times.pop_front();
  }
  double events(double t) {
    prune(t);
    return times.size() / 2.0;
  }
  double fps(double t) {
    prune(t);
    return times.size() > 1 && t - times.front() >= .5 ? (times.size() - 1) / (t - times.front()) : 0;
  }
};

#pragma pack(push, 1)
struct BMPFile {
  uint16_t magic = 0x4d42;
  uint32_t size = 0;
  uint16_t a = 0, b = 0;
  uint32_t offset = 54;
};
struct BMPInfo {
  uint32_t size = 40;
  int32_t w = 0, h = 0;
  uint16_t planes = 1, bits = 32;
  uint32_t compression = 0, bytes = 0;
  int32_t xppm = 0, yppm = 0;
  uint32_t colors = 0, important = 0;
};
#pragma pack(pop)
inline void saveBMP(const std::filesystem::path &file, const Image &im) {
  BMPFile f;
  BMPInfo h;
  h.w = im.w;
  h.h = -im.h;
  h.bytes = uint32_t(im.bgra.size());
  f.size = 54 + h.bytes;
  std::ofstream out(file, std::ios::binary);
  out.write(reinterpret_cast<char *>(&f), sizeof(f));
  out.write(reinterpret_cast<char *>(&h), sizeof(h));
  out.write(reinterpret_cast<const char *>(im.bgra.data()), im.bgra.size());
  if (!out)
    throw std::runtime_error("Cannot save bitmap");
}
inline Image loadBMP(const std::filesystem::path &file) {
  std::ifstream in(file, std::ios::binary);
  BMPFile f;
  BMPInfo h;
  in.read(reinterpret_cast<char *>(&f), sizeof(f));
  in.read(reinterpret_cast<char *>(&h), sizeof(h));
  if (!in || f.magic != 0x4d42 || h.planes != 1 || (h.bits != 24 && h.bits != 32) || h.compression != 0 ||
      h.w <= 0 || h.w > 10000 || h.h == 0 || std::abs(int64_t(h.h)) > 10000)
    throw std::runtime_error("Expected an uncompressed 24/32 bit BMP");
  Image im;
  im.w = h.w;
  im.h = std::abs(h.h);
  im.bgra.resize(size_t(im.w) * im.h * 4);
  size_t stride = (size_t(im.w) * (h.bits / 8) + 3) & ~size_t(3);
  std::vector<uint8_t> row(stride);
  in.seekg(f.offset);
  for (int y = 0; y < im.h; y++) {
    in.read(reinterpret_cast<char *>(row.data()), row.size());
    int dest = h.h < 0 ? y : im.h - 1 - y;
    for (int x = 0; x < im.w; x++) {
      for (int c = 0; c < 3; c++)
        im.bgra[(size_t(dest) * im.w + x) * 4 + c] = row[size_t(x) * (h.bits / 8) + c];
      im.bgra[(size_t(dest) * im.w + x) * 4 + 3] = 255;
    }
  }
  if (!in)
    throw std::runtime_error("Truncated bitmap");
  return im;
}
} // namespace lux
