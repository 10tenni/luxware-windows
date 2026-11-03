#pragma once
namespace lux {
// Inventory keys are single alphanumeric keys; sequences and system shortcuts are not inventory slots.
inline bool slotKey(int key) {
  return (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z');
}
inline bool crossBindingConflict(const Settings &s) {
  if (s.bindMods != 0)
    return false;
  if (s.bindKind == 0)
    return s.bindCode == s.crossSlot || s.bindCode == s.returnSlot ||
           (s.crossTriggerKind == 0 && s.bindCode == s.crossTrigger);
  return s.crossTriggerKind == 1 && s.bindCode == s.crossTrigger;
}
inline bool crossConfigured(const Settings &s) {
  return !crossBindingConflict(s) && (s.crossMode == 1 || s.crossMode == 2) && slotKey(s.crossSlot) &&
         slotKey(s.returnSlot) && s.crossSlot != s.returnSlot && s.crossSlot != s.crossTrigger &&
         s.returnSlot != s.crossTrigger &&
         ((s.crossTriggerKind == 0 && s.crossTrigger >= 1 && s.crossTrigger <= 255 &&
           s.crossTrigger != VK_F6 && s.crossTrigger != VK_ESCAPE && s.crossTrigger != 'R') ||
          (s.crossTriggerKind == 1 && s.crossTrigger >= 2 && s.crossTrigger <= 4));
}
// Monotonic state machine: no catch-up bursts after stalls. Transitions commit only on accepted events.
struct CrossbowCycle {
  enum Action { none, equip, fireDown, fireUp, restore };
  int phase = 0;
  double due = 0;
  bool running = false;
  Action poll(double t, bool enabled) {
    if (!enabled) {
      running = false;
      phase = 0;
      due = 0;
      return none;
    }
    if (!running) {
      running = true;
      phase = 0;
      due = t + 1.10;
    }
    if (t < due)
      return none;
    return phase == 0 ? equip : phase == 1 ? fireDown : phase == 2 ? fireUp : restore;
  }
  void accepted(double t, double cps = 23.43) {
    double interval = 1 / (std::isfinite(cps) ? std::clamp(cps, .1, 200.) : 23.43);
    if (phase == 0) {
      phase = 1;
      due = t + .020;
    } else if (phase == 1) {
      phase = 2;
      due = t + interval * .4;
    } else if (phase == 2) {
      phase = 3;
      due = t + interval * .6;
    } else {
      phase = 0;
      due = t + 1.10;
    }
  }
  bool busy() const {
    return running && phase != 0;
  }
};
} // namespace lux
