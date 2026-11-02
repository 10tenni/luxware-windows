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
  double delayedBaselineError = 0, delayedCompensatedError = 0;
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
    check("resetStopBindingsReserved", reservedBinding(0, 'R', 3) && !reservedBinding(0, 'L', 3) &&
                                           reservedBinding(0, VK_ESCAPE, 5) && !reservedBinding(0, 'E', 2) &&
                                           !reservedBinding(1, 3, 3));
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
    // Exercise detection loss using the real dispatch loop and an injected event sink.
    // Observations here are explicit controlled inputs, not claimed neural detections.
    Shared lossState;
    lossState.scan = lossState.armed = true;
    lossState.source = reinterpret_cast<HWND>(1);
    lossState.settings.aim = lossState.settings.clicks = true;
    lossState.settings.level = true; // Legacy saved setting must never cause movement on loss.
    lossState.settings.smoothing = .5;
    std::atomic<int> movementCount{0}, clickCount{0}, upCount{0};
    std::thread lossThread([&] {
      inputLoop(
          lossState,
          [&](INPUT &i) {
            if (i.mi.dwFlags & MOUSEEVENTF_MOVE)
              ++movementCount;
            if (i.mi.dwFlags & MOUSEEVENTF_LEFTDOWN)
              ++clickCount;
            if (i.mi.dwFlags & MOUSEEVENTF_LEFTUP)
              ++upCount;
            return UINT(1);
          },
          [](HWND) { return true; }, [] { return POINT{500, 400}; });
    });
    auto lossPump = [&](double duration, std::optional<Detection> observation, double age = 0.) {
      double begin = now();
      while (now() - begin < duration) {
        double t = now();
        lossState.heartbeat = t;
        {
          std::lock_guard<std::mutex> lock(lossState.mutex);
          lossState.view.received = t - age;
          lossState.view.client = {0, 0, 1000, 800};
          lossState.view.target = observation;
        }
        Sleep(2);
      }
    };
    lossPump(.12, {});
    bool initiallyStill = movementCount == 0 && clickCount > 0;
    Detection observed{{.7, .7, .1, .1}, .9f, 7, false, {}};
    lossPump(.10, observed);
    bool movesWhenObserved = movementCount > 0;
    {
      std::lock_guard<std::mutex> lock(lossState.mutex);
      lossState.view.target.reset();
    }
    // Let any already-dispatched event finish before observing the no-target interval.
    lossPump(.02, {});
    int beforeLossMoves = movementCount, beforeLossClicks = clickCount;
    lossPump(.65, {});
    bool stillAfterLoss = movementCount == beforeLossMoves;
    bool clicksAfterLoss = clickCount - beforeLossClicks >= 8;
    observed.visual = true;
    lossPump(.10, observed);
    bool templateCannotMove = movementCount == beforeLossMoves;
    observed.visual = false;
    observed.score = .05f;
    lossPump(.10, observed);
    bool weakCannotMove = movementCount == beforeLossMoves;
    observed.score = .9f;
    lossPump(.10, observed, .3);
    bool staleCannotMove = movementCount == beforeLossMoves;
    lossState.armed = false;
    lossPump(.04, {});
    lossState.quit = true;
    lossThread.join();
    check("noPlayerStartsAsOrdinaryClicker", initiallyStill);
    check("currentPlayerEnablesMovement", movesWhenObserved);
    check("playerLossStopsMovementDespiteLegacyLevelSetting", stillAfterLoss);
    check("playerLossKeepsAutoClicksRunning", clicksAfterLoss);
    check("templateOnlyObservationCannotMoveMouse", templateCannotMove);
    check("belowConfidenceObservationCannotMoveMouse", weakCannotMove);
    check("staleObservationCannotMoveMouse", staleCannotMove);
    check("noPlayerClickPairsRemainBalanced", clickCount == upCount);
    // Real dispatch path with a fixed producer frame: Snap must not tween/reapply that frame.
    Shared snapState;
    snapState.scan = snapState.armed = true;
    snapState.source = reinterpret_cast<HWND>(1);
    snapState.settings.snap = true;
    snapState.settings.smoothing = .9;
    snapState.settings.strength = 1;
    snapState.settings.hz = 1000;
    snapState.settings.sensitivity = 1;
    snapState.view.client = {0, 0, 1000, 800};
    snapState.view.received = now();
    snapState.view.target = Detection{{.8, .7, .1, .1}, .9f, 1, false, {}};
    std::atomic<int> snapMoves{0}, firstX{0}, firstY{0};
    std::thread snapThread([&] {
      inputLoop(
          snapState,
          [&](INPUT &input) {
            if (input.mi.dwFlags & MOUSEEVENTF_MOVE) {
              if (snapMoves.fetch_add(1) == 0) {
                firstX = input.mi.dx;
                firstY = input.mi.dy;
              }
            }
            return UINT(1);
          },
          [](HWND) { return true; }, [] { return POINT{500, 400}; });
    });
    auto snapPump = [&](double seconds) {
      double begin = now();
      while (now() - begin < seconds) {
        snapState.heartbeat = now();
        Sleep(2);
      }
    };
    snapPump(.10);
    bool directSnap = snapMoves == 1 && firstX == 350 && firstY == 200;
    {
      std::lock_guard<std::mutex> lock(snapState.mutex);
      snapState.view.received = now();
      snapState.view.target = Detection{{.1, .2, .1, .1}, .9f, 1, false, {}};
    }
    snapPump(.08);
    bool followsNewFrame = snapMoves == 2;
    {
      std::lock_guard<std::mutex> lock(snapState.mutex);
      snapState.view.received = now();
      snapState.view.target.reset();
    }
    snapPump(.10);
    bool stopsOnLoss = snapMoves == 2;
    snapState.armed = false;
    snapPump(.02);
    snapState.quit = true;
    snapThread.join();
    check("snapUsesOneDirectCorrectionWithoutSmoothing", directSnap);
    check("snapWaitsForNewFrameBeforeNextCorrection", followsNewFrame);
    check("snapStillStopsMovementWhenPlayerDisappears", stopsOnLoss);
    auto primeLock = [](Tracker &tracker) {
      Feature f{};
      Detection body{{.8, .4, .1, .2}, .9f, 0, false, f};
      tracker.update({body}, 1, true);
      auto selected = tracker.choose(tracker.update({body}, 1.03, true), 1.03, .71, true);
      tracker.choose(tracker.update({}, 1.06, true), 1.06, .71, true);
      return selected->d.id;
    };
    Tracker acrossScreen;
    int remembered = primeLock(acrossScreen);
    Feature signature{};
    Detection reappeared{{0, .4, .1, .2}, .9f, 0, false, signature};
    auto firstReturn = acrossScreen.choose(acrossScreen.update({reappeared}, 1.2, true), 1.2, .71, true);
    auto secondReturn = acrossScreen.choose(acrossScreen.update({reappeared}, 1.23, true), 1.23, .71, true);
    check("oneEntityReacquiresAcrossViewportAfterConfirmation",
          !firstReturn && secondReturn && secondReturn->d.id == remembered);
    Tracker expiredLock;
    primeLock(expiredLock);
    expiredLock.choose(expiredLock.update({reappeared}, 8, true), 8, .71, true);
    check("expiredIdentityDoesNotSilentlySwitchToNewPlayer",
          !expiredLock.choose(expiredLock.update({reappeared}, 8.03, true), 8.03, .71, true));
    Tracker ambiguousLock;
    primeLock(ambiguousLock);
    Detection lookalike = reappeared;
    lookalike.box.x = .15;
    ambiguousLock.choose(ambiguousLock.update({reappeared, lookalike}, 1.2, true), 1.2, .71, true);
    check("acrossViewportLookalikesKeepLockPaused",
          !ambiguousLock.choose(ambiguousLock.update({reappeared, lookalike}, 1.23, true), 1.23, .71, true));
    MotionHistory history;
    history.record(1.01, 20, -10);
    history.record(1.04, 30, 5);
    auto afterCapture = history.after(1.02, 1.05);
    check("cameraHistoryCountsOnlyInputNewerThanPixels",
          afterCapture.first == 30 && afterCapture.second == 5);
    check("cameraHistoryExpiresOldInput", history.after(1.02, 1.7).first == 0);
    history.record(2, 12, 4);
    history.clear();
    check("cameraHistoryClearRemovesPriorContext", history.after(1.9, 2.1).first == 0);
    MotionHistory cameraSamples;
    FrameLead velocity;
    velocity.observe(1, true, 1, 100, 0, cameraSamples);
    cameraSamples.record(1.01, 20, 0);
    velocity.observe(1, true, 1.05, 90, 0, cameraSamples);
    auto predicted = velocity.offset(.05, .001, 60, 60);
    check("leadSeparatesTargetMotionFromAcceptedCameraInput", std::abs(predicted.first - 10.2) < 1e-6);
    check("zeroPredictionSettingDisablesOptionalLead", velocity.offset(.05, 0, 60, 60).first == 0);
    FrameLead cursorLead;
    cursorLead.observe(1, false, 1, 100, 0, cameraSamples);
    cursorLead.observe(1, false, 1.05, 110, 0, cameraSamples);
    check("freeCursorLeadUsesScreenMotionWithoutCameraDelta",
          std::abs(cursorLead.offset(.05, .001, 60, 60).first - 10.2) < 1e-6);
    velocity.observe(2, true, 1.07, -80, 0, cameraSamples);
    check("leadResetsForDifferentTarget", velocity.offset(.05, .01, 60, 60).first == 0);
    velocity.observe(2, true, 1.08, 1000, 0, cameraSamples);
    check("leadRejectsDiscontinuousImageJumps", velocity.offset(.05, .01, 60, 60).first == 0);
    // Explicit idealized LINEAR camera with SERIAL capture/inference and smoothing.
    // It uses the production input budget cadence; it is not a live-game accuracy benchmark.
    auto delayedScene = [&](bool compensated, double latency, bool moving) {
      struct Frame {
        double captured, ready, error;
      };
      std::deque<Frame> queue;
      MotionHistory inFlight;
      FrameLead targetLead;
      double camera = 0, observedError = 0, captured = 0, oldApplied = 0, errorSum = 0, nextCapture = 0,
             filtered = 0;
      int count = 0;
      bool available = false;
      for (int tick = 0; tick < 960; ++tick) {
        double t = tick / 240.;
        double target = 600 + (moving ? 80 * std::sin(6.28318530718 * t) : 0);
        if (queue.empty() && t >= nextCapture)
          queue.push_back({t, t + latency, target - camera});
        if (!queue.empty() && queue.front().ready <= t) {
          auto f = queue.front();
          queue.pop_front();
          observedError = f.error;
          captured = f.captured;
          oldApplied = 0;
          available = true;
          nextCapture = t + analysisDelay(latency, 60, true, true);
          if (compensated)
            targetLead.observe(1, true, captured, observedError, 0, inFlight);
        }
        if (available && t - captured < .2) {
          double correction = observedError - (compensated ? inFlight.after(captured, t).first : oldApplied);
          if (compensated && moving)
            correction += targetLead.offset(t - captured, .001, 60, 60).first;
          filtered += .08 * (correction - filtered);
          double movement =
              filtered * correction > 0
                  ? std::copysign(std::min({180., std::abs(filtered), std::abs(correction)}), correction)
                  : 0;
          if (std::abs(filtered) > 3) {
            movement = std::round(movement);
            camera += movement;
            oldApplied += movement;
            inFlight.record(t + 1e-7, movement, 0);
          }
        }
        if (t > .5) {
          errorSum += std::abs(target - camera);
          ++count;
        }
      }
      return errorSum / std::max(1, count);
    };
    bool staticSettles = true;
    for (double latency : {1. / 60, .05, .10})
      staticSettles = delayedScene(true, latency, false) <= 3 && staticSettles;
    check("compensatedStaticAimSettlesAcrossSerialInferenceDelays", staticSettles);
    delayedBaselineError = delayedScene(false, .05, true);
    delayedCompensatedError = delayedScene(true, .05, true);
    check("optionalLeadReducesMovingErrorInSerialLinearModel",
          delayedCompensatedError < delayedBaselineError * .85);
    SelfGuard ownGuard;
    Feature ownSignature{};
    Detection ownProposal{{.4, .5, .15, .3}, .9f, 0, false, ownSignature};
    bool ownContinues = true;
    for (int frame = 0; frame < 7; ++frame) {
      Feature variant{};
      for (int i = 0; i < 36; ++i)
        variant[i] = frame * .1f;
      ownProposal.feature = variant;
      ownContinues = ownGuard.filter({ownProposal}, {ownSignature}, 1 + frame * .02).empty() && ownContinues;
    }
    check("selfExclusionContinuesAcrossSmallCurrentAppearanceChanges", ownContinues);
    Detection separateBody = ownProposal;
    separateBody.box = {.05, .1, .15, .3};
    check("selfExclusionDoesNotMaskDistantDifferentBody",
          ownGuard.filter({separateBody}, {ownSignature}, 1.14).size() == 1);
    check("selfExclusionContinuationExpires",
          ownGuard.filter({ownProposal}, {ownSignature}, 1.7).size() == 1);
    AimMemory aimMemory;
    aimMemory.observe(3, false, 1);
    aimMemory.filterX = 150;
    aimMemory.filterY = -80;
    aimMemory.appliedX = 30;
    aimMemory.observe(4, false, 1.02);
    check("targetSwitchClearsPriorCorrection",
          aimMemory.filterX == 0 && aimMemory.filterY == 0 && aimMemory.appliedX == 0);
    aimMemory.filterX = 80;
    aimMemory.observe(4, true, 1.04);
    check("perspectiveSwitchClearsPriorCorrection", aimMemory.filterX == 0);
    Tracker stableTracker;
    Detection proposal{{.45, .4, .1, .2}, .9f, 0, false, {}};
    check("singleFrameProposalDoesNotAcquire",
          !stableTracker.choose(stableTracker.update({proposal}, 1, true), 1, .71, true));
    auto stablePlayer = stableTracker.choose(stableTracker.update({proposal}, 1.03, true), 1.03, .71, true);
    check("consecutiveObservationsAcquirePlayer", stablePlayer.has_value());
    check("missingObservationDoesNotReturnRememberedBox",
          !stableTracker.choose(stableTracker.update({}, 1.06, true), 1.06, .71, true));
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
        auto chosen = tracker.choose(tracker.update({d(.45, .5, g)}, 1.02, true), 1.02, .71, true);
        auto id = chosen->d.id;
        tracker.choose(tracker.update({}, 1.2, true), 1.2, .71, true);
        tracker.choose(tracker.update(next, 1.8, true), 1.8, .71, true);
        auto recovered = tracker.choose(tracker.update(next, 1.83, true), 1.83, .71, true);
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
  out << "},\n\"linearDelayBaselineMeanPixelError\":" << delayedBaselineError
      << ",\n\"linearDelayCompensatedMeanPixelError\":" << delayedCompensatedError
      << ",\n\"preprocessReferenceMS\":" << referenceMS << ",\n\"preprocessOptimizedMS\":" << optimizedMS
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
