#ifndef ZERO_HOCKEYZONE_H
#define ZERO_HOCKEYZONE_H

#include <zero/BotController.h>
#include <zero/ZeroBot.h>
#include <zero/zones/ZoneController.h>
#include <zero/game/Game.h>
#include <vector>
#include <memory>

namespace zero {
namespace hockeyzone {

struct Rink {
  RegionIndex region_index = kUndefinedRegion;
  Rectangle east_goal_rect;
  Rectangle west_goal_rect;
  Vector2f center;
  u16 east_door_x = 0;
  u16 west_door_x = 0;

  Rink(RegionIndex index) : region_index(index) {}
};

struct HockeyZone {
  ZeroBot* bot;
  std::vector<Rink> rinks;

  HockeyZone(ZeroBot* bot) : bot(bot) {}

  void Build();
  Rink* GetRinkFromRegionIndex(RegionIndex region_index);
  Rink* GetRinkFromTile(u16 x, u16 y);
};

struct HockeyZoneController : ZoneController {
  bool IsZone(Zone zone) override {
    hz = nullptr;
    return true; // Bypasses the missing core enum!
  }

  void CreateBehaviors(const char* arena_name) override;

  std::unique_ptr<HockeyZone> hz;
};

} // namespace hockeyzone
} // namespace zero

#endif
