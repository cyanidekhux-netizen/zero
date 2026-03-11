/*
    ZERO BOT - GOALIE BEHAVIOR
    
    BASE BEHAVIOR FOCUS:
    - Pure Elliptical Arc mirroring
    - "Linix" Broadside Flight (No 180-degree turns, uses reverse AB)
    - Y-Axis Sensitive Deadzone (Calm depth, aggressive lateral tracking)
    - True Shot Tracking (Ignores shooter velocity on slappers)
    - Anti-Tangle Goal Line Repulsor & Y-Axis-Only Dives
    - Goal Scored Metric Logging
*/

#include "GoalieBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/PowerballNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/game/Logger.h>

#include <cmath>
#include <algorithm>

namespace zero {
namespace hockeyzone {

// =============================================================================
// CONFIGURATION (MULTI-RINK SUPPORT)
// =============================================================================
namespace GoalieConfig {
    constexpr float kLeftGoalLineX = 411.0f;
    constexpr float kRightGoalLineX = 613.0f; 
    
    constexpr float kCenterY[2] = { 511.5f, 736.0f }; 

    constexpr float kPostOffset = 13.5f;    
    constexpr float kMinDepth = 2.0f;       
    constexpr float kMaxArcDepth = 7.0f;    
}

// =============================================================================
// THE GOALIE
// =============================================================================
struct GoalieDefendNode : public behavior::BehaviorNode {
    GoalieDefendNode(const char* puck_key) : puck_key(puck_key) {}

    behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
        auto self = ctx.bot->game->player_manager.GetSelf();
        if (!self) return behavior::ExecuteResult::Failure;

        int rink_index = self->frequency / 2; 
        if (rink_index > 1) return behavior::ExecuteResult::Failure;

        auto opt_puck = ctx.blackboard.Value<Vector2f>(puck_key);
        if (!opt_puck) return behavior::ExecuteResult::Failure;
        
        Vector2f puck_pos = *opt_puck;
        float center_y = GoalieConfig::kCenterY[rink_index];

        if (std::abs(puck_pos.y - center_y) > 100.0f) {
            return behavior::ExecuteResult::Failure;
        }

        bool defend_left = (self->frequency % 2 == 0);
        float goal_line_x = defend_left ? GoalieConfig::kLeftGoalLineX : GoalieConfig::kRightGoalLineX;
        
        float top_post_y = center_y - GoalieConfig::kPostOffset;
        float bottom_post_y = center_y + GoalieConfig::kPostOffset;

        // === GOAL DETECTOR METRICS ===
        static bool goal_scored_recently = false;
        bool puck_in_net = false;
        
        if (defend_left && puck_pos.x < GoalieConfig::kLeftGoalLineX && puck_pos.y > top_post_y && puck_pos.y < bottom_post_y) {
            puck_in_net = true;
        } else if (!defend_left && puck_pos.x > GoalieConfig::kRightGoalLineX && puck_pos.y > top_post_y && puck_pos.y < bottom_post_y) {
            puck_in_net = true;
        }

        if (puck_in_net && !goal_scored_recently) {
            Log(LogLevel::Warning, "🚨 GOAL SCORED! 🚨 | Puck:(%.1f, %.1f) | Goalie:(%.1f, %.1f)", 
                puck_pos.x, puck_pos.y, self->position.x, self->position.y);
            goal_scored_recently = true; 
        } else if (!puck_in_net && puck_pos.x > 450.0f && puck_pos.x < 574.0f) {
            goal_scored_recently = false; 
        }

        // === FIND THE THREAT ===
        Vector2f threat_pos = puck_pos;
        Vector2f threat_vel(0, 0);
        
        for (size_t i = 0; i < ctx.bot->game->player_manager.player_count; ++i) {
            Player* p = ctx.bot->game->player_manager.players + i;
            if (p->ship >= 8 || p->frequency == self->frequency) continue;
            if (std::abs(p->position.y - center_y) > 100.0f) continue;

            if (p->position.Distance(puck_pos) < 3.0f) {
                threat_pos = p->position;
                threat_vel = p->velocity;
                break;
            }
        }

        // === DYNAMIC VELOCITY ANTICIPATION ===
        static Vector2f last_puck_pos(0, 0);
        if (last_puck_pos.LengthSq() == 0.0f) last_puck_pos = puck_pos;
        Vector2f puck_vel_tick = puck_pos - last_puck_pos;
        last_puck_pos = puck_pos;
        
        if (puck_vel_tick.LengthSq() > 100.0f) puck_vel_tick = Vector2f(0, 0);

        float prediction_time = 0.10f; 
        float predicted_y = threat_pos.y;
        float puck_speed = puck_vel_tick.Length() * 100.0f; 
        
        bool is_fast_shot = (puck_speed > 35.0f);

        if (is_fast_shot) {
            prediction_time = std::clamp(puck_speed / 100.0f, 0.15f, 0.40f);
            predicted_y = puck_pos.y + (puck_vel_tick.y * 100.0f) * prediction_time;
        } else {
            float threat_speed = threat_vel.Length();
            prediction_time = std::clamp(0.10f + (threat_speed * 0.005f), 0.10f, 0.35f);
            predicted_y = threat_pos.y + threat_vel.y * prediction_time;
        }

        float want_y = std::clamp(predicted_y, top_post_y, bottom_post_y);

        float dy = want_y - center_y;
        float arc_ratio = std::sqrt(std::max(0.0f, 1.0f - (dy * dy) / (GoalieConfig::kPostOffset * GoalieConfig::kPostOffset)));
        float depth = GoalieConfig::kMinDepth + (GoalieConfig::kMaxArcDepth - GoalieConfig::kMinDepth) * arc_ratio;
        float want_x = defend_left ? (goal_line_x + depth) : (goal_line_x - depth);
        Vector2f target(want_x, want_y);

        // === FLIGHT DYNAMICS ===
        Vector2f to_target = target - self->position;
        float dist_to_target = to_target.Length();
        float y_dist_to_target = std::abs(target.y - self->position.y);

        bool is_moving = ctx.blackboard.ValueOr<bool>("defend_is_moving", false);

        if (is_fast_shot || y_dist_to_target > 1.5f || dist_to_target > 3.0f) {
            is_moving = true;  
        } else if (!is_fast_shot && dist_to_target < 0.5f) {
            is_moving = false; 
        }

        ctx.blackboard.Set<bool>("defend_is_moving", is_moving);
        ctx.bot->bot_controller->steering.force = Vector2f(0, 0);

        if (is_moving) {
            float max_thrust = is_fast_shot ? 40.0f : 25.0f;
            float speed_multiplier = is_fast_shot ? 8.0f : 4.0f;
            
            float desired_speed = std::min(max_thrust, dist_to_target * speed_multiplier);
            Vector2f desired_velocity = Normalize(to_target) * desired_speed;
            Vector2f force_needed = desired_velocity - self->velocity;
            
            // BUGFIX 1: ANTI-NET-TANGLE REPULSOR
            // If momentum drags us behind the crease, inject massive outward force
            if (defend_left && self->position.x < goal_line_x + 1.0f) {
                force_needed.x += 40.0f; 
            } else if (!defend_left && self->position.x > goal_line_x - 1.0f) {
                force_needed.x -= 40.0f;
            }

            // Broadside navigation 
            if (force_needed.y <= 0) {
                ctx.bot->bot_controller->steering.Face(*ctx.bot->game, self->position + force_needed);
                ctx.bot->bot_controller->steering.force = force_needed;
            } else {
                ctx.bot->bot_controller->steering.Face(*ctx.bot->game, self->position - force_needed);
                ctx.bot->bot_controller->steering.force = force_needed; 
            }
            
            // BUGFIX 2: LATERAL AFTERBURNER LOCK
            // Only dive for Y-axis emergencies. Never afterburn just to adjust X-axis depth!
            bool use_ab = is_fast_shot && y_dist_to_target > 2.0f && self->energy > 800.0f;
            if (!use_ab) use_ab = (y_dist_to_target > 5.0f && self->energy > 1000.0f);
            
            ctx.bot->bot_controller->input->SetAction(InputAction::Afterburner, use_ab);
        } else {
            Vector2f brake_force = -self->velocity * 4.0f;
            if (brake_force.y <= 0) {
                ctx.bot->bot_controller->steering.Face(*ctx.bot->game, self->position + brake_force);
            } else {
                ctx.bot->bot_controller->steering.Face(*ctx.bot->game, self->position - brake_force);
            }
            ctx.bot->bot_controller->steering.force = brake_force; 
            ctx.bot->bot_controller->input->SetAction(InputAction::Afterburner, false);
        }

        if (ctx.bot->bot_controller->input) {
            ctx.bot->bot_controller->input->SetAction(InputAction::Bullet, false);
        }

        // --- THROTTLED DEBUG LOGGING ---
        static int defend_ticks = 0;
        if (++defend_ticks % 100 == 0) {
            Log(LogLevel::Info, "[Defend] Threat:(%.1f, %.1f) | Target:(%.1f, %.1f) | FastShot:%d | Moving:%d", 
                threat_pos.x, threat_pos.y, want_x, want_y, is_fast_shot, is_moving);
        }

        return behavior::ExecuteResult::Success;
    }

    const char* puck_key;
};

// =============================================================================
// Clear puck when we have it
// =============================================================================
struct GoalieClearNode : public behavior::BehaviorNode {
    behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
        auto self = ctx.bot->game->player_manager.GetSelf();
        if (!self) return behavior::ExecuteResult::Failure;

        int rink_index = self->frequency / 2; 
        if (rink_index > 1) return behavior::ExecuteResult::Failure;

        bool defend_left = (self->frequency % 2 == 0);
        float center_y = GoalieConfig::kCenterY[rink_index];
        
        Vector2f clear_target;
        clear_target.x = defend_left ? 480.0f : 543.0f;
        clear_target.y = self->position.y > center_y 
            ? center_y + 25.0f 
            : center_y - 25.0f;

        ctx.bot->bot_controller->steering.Face(*ctx.bot->game, clear_target);
        
        if (self->velocity.Length() > 2.0f) {
            ctx.bot->bot_controller->steering.force = Normalize(self->velocity) * -10.0f;
        } else {
            ctx.bot->bot_controller->steering.force = Vector2f(0, 0);
        }

        if (ctx.bot->bot_controller->input) {
            ctx.bot->bot_controller->input->SetAction(InputAction::Afterburner, false);
            ctx.bot->bot_controller->input->SetAction(InputAction::Bullet, false);
        }

        Vector2f to_target = Normalize(clear_target - self->position);
        float alignment = self->GetHeading().Dot(to_target);

        if (alignment > 0.95f && self->velocity.Length() < 4.0f) {
            ctx.bot->game->soccer.FireBall(BallFireMethod::Gun);
            return behavior::ExecuteResult::Success;
        }

        return behavior::ExecuteResult::Running;
    }
};

// =============================================================================
// Idle at home
// =============================================================================
struct GoalieIdleNode : public behavior::BehaviorNode {
    behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
        auto self = ctx.bot->game->player_manager.GetSelf();
        if (!self) return behavior::ExecuteResult::Failure;

        int rink_index = self->frequency / 2; 
        if (rink_index > 1) {
            ctx.bot->bot_controller->steering.force = Vector2f(0, 0);
            return behavior::ExecuteResult::Success; 
        }

        bool defend_left = (self->frequency % 2 == 0);
        float goal_line_x = defend_left ? GoalieConfig::kLeftGoalLineX : GoalieConfig::kRightGoalLineX;
        float center_y = GoalieConfig::kCenterY[rink_index];
        
        float home_x = defend_left 
            ? goal_line_x + GoalieConfig::kMaxArcDepth
            : goal_line_x - GoalieConfig::kMaxArcDepth;
            
        Vector2f home(home_x, center_y);
        
        Vector2f to_home = home - self->position;
        float dist_to_home = to_home.Length();

        bool is_moving = ctx.blackboard.ValueOr<bool>("idle_is_moving", false);

        if (dist_to_home > 2.5f) {
            is_moving = true;
        } else if (dist_to_home < 0.5f) {
            is_moving = false;
        }

        ctx.blackboard.Set<bool>("idle_is_moving", is_moving);
        ctx.bot->bot_controller->steering.force = Vector2f(0, 0);

        if (is_moving) {
            float desired_speed = std::min(25.0f, dist_to_home * 4.0f);
            Vector2f desired_velocity = Normalize(to_home) * desired_speed;
            Vector2f force_needed = desired_velocity - self->velocity;
            
            if (force_needed.y <= 0) {
                ctx.bot->bot_controller->steering.Face(*ctx.bot->game, self->position + force_needed);
                ctx.bot->bot_controller->steering.force = force_needed;
            } else {
                ctx.bot->bot_controller->steering.Face(*ctx.bot->game, self->position - force_needed);
                ctx.bot->bot_controller->steering.force = force_needed;
            }
        } else {
            ctx.bot->bot_controller->steering.Face(*ctx.bot->game, self->position + Vector2f(0, -1));
            ctx.bot->bot_controller->steering.force = -self->velocity * 3.0f;
        }

        if (ctx.bot->bot_controller->input) {
            ctx.bot->bot_controller->input->SetAction(InputAction::Afterburner, false);
            ctx.bot->bot_controller->input->SetAction(InputAction::Bullet, false);
        }

        return behavior::ExecuteResult::Success;
    }
};

// =============================================================================
// BEHAVIOR TREE
// =============================================================================
std::unique_ptr<behavior::BehaviorNode> GoalieBehavior::CreateTree(behavior::ExecuteContext& ctx) {
    using namespace behavior;
    BehaviorBuilder builder;

    builder
        .Selector()
            .Sequence()
                .Child<ExecuteNode>([](ExecuteContext& ctx) {
                    auto self = ctx.bot->game->player_manager.GetSelf();
                    if (!self) return ExecuteResult::Failure;
                    return self->ship >= 8 ? ExecuteResult::Success : ExecuteResult::Failure;
                })
                .Child<ShipRequestNode>(7)
                .End()

            .Selector()
                .Sequence()
                    .Child<PowerballCarryQueryNode>()
                    .Child<GoalieClearNode>()
                    .End()

                .Sequence()
                    .InvertChild<PowerballCarryQueryNode>()
                    .Child<PowerballClosestQueryNode>("puck_position", true)
                    .Child<GoalieDefendNode>("puck_position")
                    .End()

                .Child<GoalieIdleNode>()
                .End()
            .End();

    return builder.Build();
}

}  // namespace hockeyzone
}  // namespace zero
