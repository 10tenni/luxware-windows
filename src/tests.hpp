#pragma once
inline int runSelfTests(const std::filesystem::path &report, bool recorded) {
  std::vector<std::pair<std::string, bool>> checks;
  std::vector<double> scores;
  std::string error;
  double inferenceMS = 0, poseDistance = 0, otherDistance = 0;
  auto check = [&](const std::string &name, bool value) { checks.push_back({name, value}); };
  struct TimerResolution {
    bool requested = timeBeginPeriod(1) == TIMERR_NOERROR;
    ~TimerResolution() {
      if (requested)
        timeEndPeriod(1);
    }
  } resolution; // Match the production loop's requested scheduler resolution.
  double clockInterval = 0, clockHold = 0, referenceMS = 0, optimizedMS = 0, maxPixelError = 0,
         pixelChecksum = 0;
  try {
    Tensorizer reusable;
    const float *storage = nullptr;
    bool stable = true;
    Image benchmark;
    for (auto size : {std::pair<int, int>{1, 1}, {17, 13}, {640, 400}, {1600, 900}}) {
      Image image;
      image.w = size.first;
      image.h = size.second;
      image.bgra.resize(size_t(image.w) * image.h * 4);
      for (size_t i = 0; i < image.bgra.size(); ++i)
        image.bgra[i] = uint8_t((i * 37 + i / 17) % 256);
      auto reference = tensor(image);
      const auto &fast = reusable.prepare(image);
      if (storage && storage != fast.data())
        stable = false;
      storage = fast.data();
      for (size_t i = 0; i < fast.size(); ++i)
        maxPixelError = std::max(maxPixelError, std::abs(double(fast[i]) - reference[i]));
      benchmark = std::move(image);
    }
    check("reusedPreprocessingMatchesScalarPixels", maxPixelError < 1e-6);
    check("preprocessingStorageStableAcrossResizes", stable);
    constexpr int repetitions = 8;
    double begin = now();
    for (int i = 0; i < repetitions; ++i) {
      auto v = tensor(benchmark);
      pixelChecksum += v[size_t(i) * 997];
    }
    referenceMS = (now() - begin) * 1000 / repetitions;
    begin = now();
    for (int i = 0; i < repetitions; ++i) {
      const auto &v = reusable.prepare(benchmark);
      pixelChecksum += v[size_t(i) * 997];
    }
    optimizedMS = (now() - begin) * 1000 / repetitions;
    check("processingBudgetLeavesIdleTime",
          std::abs(analysisDelay(.04, 120, true, true) - .01) < 1e-9 &&
              std::abs(analysisDelay(.005, 120, true, true) - (1. / 30 - .005)) < 1e-9 &&
              std::abs(analysisDelay(.005, 120, false, true) - (1. / 60 - .005)) < 1e-9 &&
              analysisDelay(.04, 120, false, false) == 0);
    HDC screen = GetDC(nullptr);
    bool reuseOK = false;
    if (screen) {
      DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
      {
        PaintBuffer canvas;
        auto first = canvas.get(screen, 320, 180);
        auto bitmap = first ? GetCurrentObject(first, OBJ_BITMAP) : nullptr;
        reuseOK =
            first && first == canvas.get(screen, 320, 180) && bitmap == GetCurrentObject(first, OBJ_BITMAP);
        for (int i = 0; i < 20; ++i)
          reuseOK = canvas.get(screen, 320 + i, 180 + i) && reuseOK;
      }
      DWORD after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
      check("paintBufferReusesStorageAndReleasesObjects", reuseOK && after <= before + 1);
      ReleaseDC(nullptr, screen);
    } else
      check("paintBufferReusesStorageAndReleasesObjects", false);
    check("resetLevelStopBindingsReserved", reservedBinding(0, 'R', 3) && reservedBinding(0, 'L', 3) &&
                                                reservedBinding(0, VK_ESCAPE, 5) &&
                                                !reservedBinding(0, 'E', 2) && !reservedBinding(1, 3, 3));
    ClickClock clock;
    std::vector<double> downs, ups;
    double start = now();
    while (now() - start < 1.3) {
      double t = now();
      auto action = clock.tick(t, true, 23.43, .4);
      if (action == ClickClock::down)
        downs.push_back(t);
      if (action == ClickClock::up)
        ups.push_back(t);
      Sleep(1);
    }
    auto final = clock.tick(now(), false, 23.43, .4);
    if (final == ClickClock::up)
      ups.push_back(now());
    double interval = 0, hold = 0;
    for (size_t i = 1; i < downs.size(); i++)
      interval += downs[i] - downs[i - 1];
    interval /= std::max(size_t(1), downs.size() - 1);
    for (size_t i = 0; i < std::min(downs.size(), ups.size()); i++)
      hold += ups[i] - downs[i];
    hold /= std::max(size_t(1), ups.size());
    clockInterval = interval;
    clockHold = hold;
    check("monotonicDecimalClickTiming", downs.size() == ups.size() && downs.size() >= 27 &&
                                             downs.size() <= 33 && std::abs(interval - 1 / 23.43) < .006 &&
                                             std::abs(hold - .4 / 23.43) < .006);
    check("zeroCatchupBurstAfterStall", clock.tick(now() + 10, true, 23.43, .4) == ClickClock::down &&
                                            clock.tick(now() + 10.0001, true, 23.43, .4) == ClickClock::none);
    // Exercise the actual Windows INPUT loop through an injected sink, never SendInput.
    Shared inputState;
    inputState.scan = true;
    inputState.armed = true;
    inputState.source = reinterpret_cast<HWND>(1);
    inputState.settings.hold = true;
    inputState.settings.aim = false;
    inputState.settings.clicks = true;
    std::mutex inputMutex;
    std::vector<DWORD> events;
    bool allTagged = true;
    std::thread inputThread([&] {
      inputLoop(
          inputState,
          [&](INPUT &i) {
            std::lock_guard<std::mutex> lock(inputMutex);
            events.push_back(i.mi.dwFlags);
            allTagged = allTagged && i.mi.dwExtraInfo == eventTag;
            return UINT(1);
          },
          [](HWND) { return true; }, [] { return POINT{500, 400}; });
    });
    auto pump = [&](double seconds) {
      double begin = now();
      while (now() - begin < seconds) {
        double t = now();
        inputState.heartbeat = t;
        {
          std::lock_guard<std::mutex> l(inputState.mutex);
          inputState.view.received = t;
          inputState.view.client = {0, 0, 1000, 800};
        }
        Sleep(2);
      }
    };
    pump(.08);
    bool idle;
    {
      std::lock_guard<std::mutex> l(inputMutex);
      idle = events.empty();
    }
    inputState.bindingHeld = true;
    pump(.45);
    inputState.bindingHeld = false;
    pump(.02);
    size_t releasedCount;
    {
      std::lock_guard<std::mutex> l(inputMutex);
      releasedCount = events.size();
    }
    pump(.04);
    inputState.quit = true;
    inputThread.join();
    size_t inputDown = 0, inputUp = 0;
    for (auto flags : events) {
      if (flags & MOUSEEVENTF_LEFTDOWN)
        inputDown++;
      if (flags & MOUSEEVENTF_LEFTUP)
        inputUp++;
    }
    check("holdDoesNotEmitBeforePhysicalPress", idle);
    check("productionDispatchBalancedClickPairs", inputDown >= 8 && inputDown == inputUp);
    check("holdReleaseStopsProductionDispatch", events.size() == releasedCount);
    check("productionEventsTaggedAgainstBindingFeedback", allTagged);
    LevelReturn level;
    level.calibrate(1);
    level.record(180);
    level.seen(1);
    check("levelLossGracePeriod", level.step(1.2, .02) == 0);
    double sum = 0;
    for (int i = 0; i < 60; i++)
      sum += level.step(1.36 + i * .02, .02);
    check("boundedLevelReturn", sum == -180 && level.offset == 0);
    level.calibrate(3);
    level.record(100);
    level.step(3.4, .02);
    level.manual(1);
    check("manualMotionCancelsReturn", !level.calibrated);
    check("visibleEdgeClipping",
          visible({.4, -.05, .1, .2}).has_value() && !visible({.4, -.3, .1, .2}).has_value());
    const float raw[] = {320, 321, 60, 61, .9f, .8f};
    check("malformedModelOutputRejected", [&]() {
      try {
        decode(raw, {1, 6, 1}, .4);
        return false;
      } catch (...) {
        return true;
      }
    }());
    if (!recorded) {
      Model model;
      model.load(appFolder() / L"Models/LuxwarePlayers.onnx", false);
      Image image;
      image.w = image.h = 640;
      image.bgra.resize(640 * 640 * 4, 127);
      auto observations = model.run(image, .4);
      check("nativeBundledModelLoadsAndProcessesPixels",
            std::all_of(observations.begin(), observations.end(), [](const Detection &d) {
              return std::isfinite(d.score) && d.score >= .4 && d.score <= 1 && d.box.w > 0 && d.box.h > 0;
            }));
    }
    if (recorded) {
      auto fixtures = appFolder() / L"Tests/Fixtures";
      auto ground = loadBMP(fixtures / L"frame17.bmp"), air = loadBMP(fixtures / L"frame16.bmp"),
           self = loadBMP(fixtures / L"own.bmp");
      auto g = describe(ground, {245. / 640, 208. / 400, 40. / 640, 40. / 400}),
           a = describe(air, {238. / 640, 39. / 400, 49. / 640, 101. / 400}),
           me = describe(self, {0, 0, 1, 1});
      poseDistance = distance(g, a);
      otherDistance = distance(g, me);
      check("savedOwnCropRejected", ownBody(me, {me}));
      check("recordedGreenBodyNotExcludedAsBlueOwn", !ownBody(g, {me}));
      auto d = [&](double x, double y, Feature f) { return Detection{{x, y, .1, .16}, .9f, 0, false, f}; };
      auto reacquire = [&](std::vector<Detection> next) {
        Tracker tracker;
        auto first = tracker.update({d(.45, .5, g)}, 1, true);
        auto chosen = tracker.choose(first, 1, .71, true);
        auto id = chosen->d.id;
        tracker.choose(tracker.update({}, 1.2, true), 1.2, .71, true);
        auto recovered = tracker.choose(tracker.update(next, 1.8, true), 1.8, .71, true);
        return recovered && recovered->d.id == id;
      };
      check("sameAppearanceRecoversSingleEntityID", reacquire({d(.45, .25, a)}));
      check("differentAppearanceDoesNotStealLock", !reacquire({d(.45, .25, me)}));
      check("ambiguousLookalikesPause", !reacquire({d(.35, .25, a), d(.55, .25, a)}));
      Model model;
      model.load(appFolder() / L"Models/LuxwarePlayers.onnx", false);
      std::vector<Feature> refs{me};
      for (int i = 0; i < 6; i++) {
        auto p = appFolder() / L"Profiles/AvatarViews" / (L"view" + std::to_wstring(i) + L".bmp");
        if (std::filesystem::exists(p)) {
          auto image = loadBMP(p);
          refs.push_back(describe(image, {0, 0, 1, 1}));
        }
      }
      std::ifstream truth(fixtures / L"truth.tsv");
      std::string name;
      double x, y, w, h;
      int hits = 0, afterOwn = 0, count = 0;
      while (truth >> name >> x >> y >> w >> h) {
        auto im = loadBMP(fixtures / (wide(name)));
        double begin = now();
        auto found = model.run(im, .4);
        inferenceMS += 1000 * (now() - begin);
        bool hit = false, filteredHit = false;
        for (auto &box : found) {
          if (box.box.cx() >= x && box.box.cx() <= x + w && box.box.cy() >= y && box.box.cy() <= y + h) {
            hit = true;
            scores.push_back(box.score);
            if (!ownBody(describe(im, box.box), refs))
              filteredHit = true;
          }
        }
        hits += hit;
        afterOwn += filteredHit;
        count++;
      }
      check("actualBundledModelSixRecordedBodyCenters", count == 6 && hits == 6);
      if (count)
        inferenceMS /= count;
      check("sixBodiesRemainAfterMigratedAvatarFiltering", count == 6 && afterOwn == 6);
      auto empty = loadBMP(fixtures / L"empty.bmp");
      check("actualEmptySceneHasNoBodyProposal", model.run(empty, .4).empty());
      auto actual = model.run(ground, .4);
      if (!actual.empty()) {
        auto body = actual.front();
        body.feature = describe(ground, body.box);
        VisualBridge bridge;
        bridge.confirm(ground, body, 1);
        check("templateDoesNotInventBodyOnEmptyScene", !bridge.follow(empty, 1.08, {me}));
        check("templateContinuationExpires", !bridge.follow(ground, 1.5, {me}));
      } else
        check("templateSetupFromActualModel", false);
      check("realObjectScoresNotConstant100Percent",
            !scores.empty() &&
                std::any_of(scores.begin(), scores.end(), [](double s) { return s > .4 && s < .999; }));
    }
  } catch (const std::exception &e) {
    error = e.what();
    check("noRuntimeException", false);
  }
  bool passed =
      !checks.empty() && std::all_of(checks.begin(), checks.end(), [](auto &c) { return c.second; });
  std::ofstream out(report);
  out << "{\n\"passed\":" << (passed ? "true" : "false") << ",\n\"checks\":{";
  for (size_t i = 0; i < checks.size(); i++) {
    if (i)
      out << ",";
    out << "\n\"" << checks[i].first << "\":" << (checks[i].second ? "true" : "false");
  }
  out << "},\n\"preprocessReferenceMS\":" << referenceMS << ",\n\"preprocessOptimizedMS\":" << optimizedMS
      << ",\n\"preprocessMaximumPixelError\":" << maxPixelError
      << ",\n\"preprocessChecksum\":" << pixelChecksum << ",\n\"clockIntervalMS\":" << clockInterval * 1000
      << ",\n\"clockHoldMS\":" << clockHold * 1000
      << ",\n\"recordedImageChecksRun\":" << (recorded ? "true" : "false")
      << ",\n\"meanModelMS\":" << inferenceMS << ",\"sameBodyFeatureDistance\":" << poseDistance
      << ",\"otherBodyFeatureDistance\":" << otherDistance
      << ",\n\"scope\":\"Controlled Windows core/input-sink regressions. Optional recorded checks use actual "
         "ONNX Runtime on development "
         "images; portable tracker, pixel appearance, timing and level-return regression. No SendInput "
         "calls, screen capture, live game input or physical hotkeys tested.\"";
  if (!error.empty()) {
    std::replace(error.begin(), error.end(), '"', '\'');
    std::replace(error.begin(), error.end(), '\\', '/');
    std::replace(error.begin(), error.end(), '\n', ' ');
    out << ",\n\"error\":\"" << error << "\"";
  }
  out << "\n}";
  return passed ? 0 : 1;
}
