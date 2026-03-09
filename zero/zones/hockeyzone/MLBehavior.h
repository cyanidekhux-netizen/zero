#pragma once
#include <zero/Math.h>
#include <zero/behavior/Behavior.h>
#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/nodes/BehaviorNode.h>
#include <zero/game/Clock.h>  // For GetCurrentTick()

namespace zero {
namespace hockeyzone {

// ML-driven steering node
// Takes ball position from blackboard, runs neural network, applies steering
// Includes three anti-oscillation fixes:
//   1. Close-range damping factor
//   2. Velocity-based approach quality check
//   3. Post-fire steering pause
struct MLSteerNode : public behavior::BehaviorNode {
  MLSteerNode(const char* ball_pos_key) 
      : ball_pos_key(ball_pos_key), last_fire_time(0), post_fire_pause_until(0) {}
  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override;
  
  const char* ball_pos_key;
  mutable u32 last_fire_time;         // For shooting cooldown
  mutable u32 post_fire_pause_until;  // FIX 3: Pause steering briefly after firing
};

// ML-first hockey behavior
// Uses neural network for movement decisions, with hardcoded fallbacks
// NOTE: Currently only Warbird has a trained brain (warbird_brain.c)
//       Other ships should use the hardcoded "hockeyzone" behavior
struct MLHockeyBehavior : public behavior::Behavior {
  void OnInitialize(behavior::ExecuteContext& ctx) override {
    // Request Warbird (ship 0) since that's the only trained brain we have
    ctx.blackboard.Set("request_ship", 0);
    ctx.blackboard.Set("leash_distance", 35.0f);
  }
  std::unique_ptr<behavior::BehaviorNode> CreateTree(behavior::ExecuteContext& ctx) override;
};

}  // namespace hockeyzone
}  // namespace zero
