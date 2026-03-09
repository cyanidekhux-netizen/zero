#pragma once
// ============================================================================
// ShipBrains.h - Multi-ship neural network dispatcher
// 
// This file includes the onnx2c-generated neural network code directly.
// Currently only Warbird has a trained brain (warbird_brain.c).
// ============================================================================
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// Ship indices (matching Continuum's ship numbering: 0-7)
typedef enum {
    SHIP_WARBIRD = 0,
    SHIP_JAVELIN = 1,
    SHIP_SPIDER = 2,
    SHIP_LEVIATHAN = 3,
    SHIP_TERRIER = 4,
    SHIP_WEASEL = 5,
    SHIP_LANCASTER = 6,
    SHIP_SHARK = 7,
    SHIP_COUNT = 8
} ShipType;

// ============================================================================
// Per-ship scaler values for input normalization
// Formula: normalized = (raw - mean) / scale
//
// Features (in order):
//   [0] rel_bx    - relative ball X (ball.x - player.x) in TILES
//   [1] rel_by    - relative ball Y (ball.y - player.y) in TILES
//   [2] pvx       - player velocity X (tiles/tick)
//   [3] pvy       - player velocity Y (tiles/tick)
//   [4] heading_x - unit heading X (-1 to 1)
//   [5] heading_y - unit heading Y (-1 to 1)
//   [6] dist      - distance to ball (tiles)
// ============================================================================
typedef struct {
    float mean[7];
    float scale[7];
} ShipScaler;

// Warbird scaler - from train2.py on fresh data (2024-03-08)
// All values in TILES - matches bot's coordinate system
static const ShipScaler WARBIRD_SCALER = {
    .mean  = {-0.62654f, 1.25743f, -0.06702f, 0.14392f, -0.08322f, -0.05252f, 22.54487f},
    .scale = {22.08336f, 17.65315f, 15.18085f, 14.52503f, 0.76681f, 0.63429f, 17.11757f}
};

// Placeholder for other ships - copy from scaler_{Ship}.txt after training
// These use Warbird's scaler as fallback but ideally should have their own
static const ShipScaler JAVELIN_SCALER = {
    .mean  = {-0.62654f, 1.25743f, -0.06702f, 0.14392f, -0.08322f, -0.05252f, 22.54487f},
    .scale = {22.08336f, 17.65315f, 15.18085f, 14.52503f, 0.76681f, 0.63429f, 17.11757f}
};

// ============================================================================
// WARBIRD NEURAL NETWORK
// Include the onnx2c-generated code directly
// ============================================================================
#include "brains/warbird_brain.c"

// Wrapper to match our calling convention
static inline void warbird_brain(const float inputs[7], float outputs[2]) {
    entry((const float(*)[7])inputs, (float(*)[2])outputs);
}

// ============================================================================
// Utility functions
// ============================================================================

// Check if a ship has a trained neural network brain
static inline bool HasTrainedBrain(ShipType ship) {
    switch (ship) {
        case SHIP_WARBIRD: return true;   // warbird_brain.c exists
        // Add more cases as brains are trained:
        // case SHIP_JAVELIN: return true;
        // case SHIP_SPIDER: return true;
        default: return false;
    }
}

static inline const ShipScaler* GetShipScaler(ShipType ship) {
    switch (ship) {
        case SHIP_WARBIRD: return &WARBIRD_SCALER;
        case SHIP_JAVELIN: return &JAVELIN_SCALER;
        default:           return &WARBIRD_SCALER;  // Fallback
    }
}

static inline void NormalizeInputs(const float raw[7], float normalized[7], const ShipScaler* scaler) {
    for (int i = 0; i < 7; i++) {
        normalized[i] = (raw[i] - scaler->mean[i]) / scaler->scale[i];
    }
}

// ============================================================================
// Main dispatcher
// Returns false if no brain is trained for this ship
// ============================================================================
static inline bool RunShipBrain(ShipType ship, const float normalized_inputs[7], float outputs[2]) {
    switch (ship) {
        case SHIP_WARBIRD:
            warbird_brain(normalized_inputs, outputs);
            return true;
        // Add more cases as brains are trained:
        // case SHIP_JAVELIN:
        //     javelin_brain(normalized_inputs, outputs);
        //     return true;
        default:
            // No trained brain - caller should use fallback behavior
            outputs[0] = 0.0f;
            outputs[1] = 0.0f;
            return false;
    }
}

static inline bool RunShipBrainRaw(ShipType ship, const float raw_inputs[7], float outputs[2]) {
    if (!HasTrainedBrain(ship)) {
        outputs[0] = 0.0f;
        outputs[1] = 0.0f;
        return false;
    }
    
    const ShipScaler* scaler = GetShipScaler(ship);
    float normalized[7];
    NormalizeInputs(raw_inputs, normalized, scaler);
    return RunShipBrain(ship, normalized, outputs);
}

#ifdef __cplusplus
}
#endif
