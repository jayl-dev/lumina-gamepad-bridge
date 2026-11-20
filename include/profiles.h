#pragma once

#include <type_traits>

#include "profile_ds4.h"
#include "profile_generic.h"
#include "profile_switch_pro.h"

namespace lumina {

// Wire ids of the controller profiles (MSG_SET_PROFILE / MSG_PROFILE)
enum class ProfileKind : uint8_t {
  Generic = 0,
  Ds4 = 1,
  SwitchPro = 2,
};
static constexpr uint8_t PROFILE_COUNT = 3;

inline bool isValidProfile(uint8_t id) { return id < PROFILE_COUNT; }

inline ProfileKind nextProfile(ProfileKind k) {
  return static_cast<ProfileKind>((static_cast<uint8_t>(k) + 1) % PROFILE_COUNT);
}

inline const char *profileName(ProfileKind k) {
  switch (k) {
    case ProfileKind::Generic: return "Generic";
    case ProfileKind::Ds4: return "DualShock 4";
    case ProfileKind::SwitchPro: return "Switch Pro";
  }
  return "?";
}

struct UsbIdentity {
  uint16_t vid;
  uint16_t pid;
  uint16_t bcd;
  const char *manufacturer;
  const char *product;
};

/**
 * Holds every profile and forwards to the one selected at boot. The USB identity and report
 * descriptor are fixed once USB starts, so switching profiles means restarting the board.
 */
class AnyProfile {
  // Defined first: deduced return types must be known before use
  template <typename F>
  decltype(auto) visit(F &&f) {
    switch (kind_) {
      case ProfileKind::Generic: return f(generic_);
      case ProfileKind::SwitchPro: return f(switchPro_);
      case ProfileKind::Ds4: break;
    }
    return f(ds4_);
  }

  template <typename F>
  decltype(auto) visit(F &&f) const {
    switch (kind_) {
      case ProfileKind::Generic: return f(generic_);
      case ProfileKind::SwitchPro: return f(switchPro_);
      case ProfileKind::Ds4: break;
    }
    return f(ds4_);
  }

public:
  explicit AnyProfile(ProfileKind kind = ProfileKind::Ds4) : kind_(kind) {}

  ProfileKind kind() const { return kind_; }

  // Only before USB starts
  void select(ProfileKind kind) { kind_ = kind; }

  UsbIdentity identity() const {
    return visit([](const auto &p) {
      using P = std::decay_t<decltype(p)>;
      return UsbIdentity {P::VID, P::PID, P::BCD, P::MANUFACTURER, P::PRODUCT};
    });
  }

  const uint8_t *descriptor(uint16_t *len) const {
    return visit([len](const auto &p) { return std::decay_t<decltype(p)>::descriptor(len); });
  }

  void begin(const uint8_t *mac) {
    generic_.begin(mac);
    ds4_.begin(mac);
    switchPro_.begin(mac);
  }

  void setState(const PadState &s) {
    visit([&s](auto &p) { p.setState(s); });
  }

  void onOutput(uint8_t id, const uint8_t *data, uint16_t len) {
    visit([&](auto &p) { p.onOutput(id, data, len); });
  }

  uint16_t getFeature(uint8_t id, uint8_t *buf, uint16_t len) const {
    return visit([&](const auto &p) { return p.getFeature(id, buf, len); });
  }

  bool takeRumble(Rumble &out) {
    return visit([&](auto &p) { return p.takeRumble(out); });
  }

  bool nextReport(uint32_t nowUs, HidReport &out) {
    return visit([&](auto &p) { return p.nextReport(nowUs, out); });
  }

private:
  ProfileKind kind_;
  GenericProfile generic_;
  Ds4Profile ds4_;
  SwitchProProfile switchPro_;
};

}  // namespace lumina
