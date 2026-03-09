#!/usr/bin/env python3
"""
prepare2.py - Fast preparation of replay data for ML training

Input:  batch_positions.csv (from convert.py)
Output: ml_ready_{Ship}.csv, scaler_{Ship}.txt

Designed to handle millions of rows efficiently using vectorized pandas ops.
"""

import os
import numpy as np
import pandas as pd
from sklearn.preprocessing import StandardScaler

# =============================================================================
# Config
# =============================================================================

SHIP_NAMES = ['Warbird', 'Javelin', 'Spider', 'Leviathan', 
              'Terrier', 'Weasel', 'Lancaster', 'Shark']

MIN_SAMPLES = 500
COORD_SCALE = 16.0  # Raw pixels to tiles

# =============================================================================
# Main
# =============================================================================

def main():
    print("=" * 60)
    print("prepare2.py - Fast ML Data Preparation")
    print("=" * 60)
    
    # Load data
    input_file = "batch_positions.csv"
    if not os.path.exists(input_file):
        # Try combined_data.csv as fallback
        input_file = "combined_data.csv"
        if not os.path.exists(input_file):
            print(f"ERROR: No input file found!")
            print("Run convert.py first to create batch_positions.csv")
            return
    
    print(f"\nLoading {input_file}...")
    df = pd.read_csv(input_file)
    print(f"  Loaded {len(df):,} rows")
    print(f"  Columns: {list(df.columns)}")
    
    # Standardize column names
    col_map = {
        'VelX': 'VX', 'VelY': 'VY', 'Rot': 'Rotation'
    }
    df = df.rename(columns={k: v for k, v in col_map.items() if k in df.columns})
    
    # Check required columns
    required = ['X', 'Y', 'VX', 'VY', 'Ship', 'BX', 'BY']
    missing = [c for c in required if c not in df.columns]
    if missing:
        print(f"ERROR: Missing columns: {missing}")
        return
    
    # Filter invalid data
    print("\nFiltering data...")
    n_before = len(df)
    
    # Remove spectators
    df = df[df['Ship'] < 8]
    
    # Remove rows with no ball position
    df = df[(df['BX'] != 0) | (df['BY'] != 0)]
    
    # Remove NaN
    df = df.dropna(subset=['X', 'Y', 'VX', 'VY', 'BX', 'BY'])
    
    print(f"  Filtered: {n_before:,} -> {len(df):,} rows")
    
    # Detect coordinate system
    max_coord = max(df['X'].max(), df['Y'].max())
    in_pixels = max_coord > 2000
    print(f"  Coordinate system: {'RAW PIXELS' if in_pixels else 'TILES'} (max={max_coord:.0f})")
    
    # Convert to tiles if needed (vectorized - fast!)
    print("\nComputing features (vectorized)...")
    
    if in_pixels:
        df['X'] = df['X'] / COORD_SCALE
        df['Y'] = df['Y'] / COORD_SCALE
        df['BX'] = df['BX'] / COORD_SCALE
        df['BY'] = df['BY'] / COORD_SCALE
        df['VX'] = df['VX'] / COORD_SCALE
        df['VY'] = df['VY'] / COORD_SCALE
    
    # Compute relative ball position
    df['rel_bx'] = df['BX'] - df['X']
    df['rel_by'] = df['BY'] - df['Y']
    
    # Distance
    df['dist'] = np.sqrt(df['rel_bx']**2 + df['rel_by']**2)
    df['dist'] = df['dist'].clip(lower=0.01)
    
    # Heading from rotation
    if 'Rotation' in df.columns:
        angle = df['Rotation'] * (2.0 * np.pi / 40.0)
        df['heading_x'] = np.sin(angle)
        df['heading_y'] = -np.cos(angle)
    else:
        df['heading_x'] = 0.0
        df['heading_y'] = -1.0
    
    # Rename velocity columns
    df['pvx'] = df['VX']
    df['pvy'] = df['VY']
    
    # Direction toward ball (labels)
    df['dir_x'] = df['rel_bx'] / df['dist']
    df['dir_y'] = df['rel_by'] / df['dist']
    
    # Filter outliers
    df = df[df['dist'] > 0.5]      # Not on top of ball
    df = df[df['dist'] < 150]      # Not too far
    df = df[df['pvx'].abs() < 30]  # Reasonable velocity
    df = df[df['pvy'].abs() < 30]
    
    print(f"  After outlier removal: {len(df):,} rows")
    
    # Behavior labeling (vectorized)
    print("\nLabeling behaviors...")
    
    vel_mag = np.sqrt(df['pvx']**2 + df['pvy']**2)
    vel_dir_x = np.where(vel_mag > 0.3, df['pvx'] / vel_mag, 0)
    vel_dir_y = np.where(vel_mag > 0.3, df['pvy'] / vel_mag, 0)
    dot = vel_dir_x * df['dir_x'] + vel_dir_y * df['dir_y']
    
    df['behavior'] = 'IDLE'
    df.loc[df['dist'] < 2.0, 'behavior'] = 'CARRY'
    df.loc[(df['dist'] >= 2.0) & (vel_mag > 0.3) & (dot > 0.5), 'behavior'] = 'CHASE'
    df.loc[(df['dist'] >= 2.0) & (vel_mag > 0.3) & (dot < -0.2), 'behavior'] = 'DEFEND'
    
    print("  Behavior distribution:")
    print(df['behavior'].value_counts().to_string())
    
    # Process per ship
    print("\n" + "=" * 60)
    print("Generating per-ship training data")
    print("=" * 60)
    
    feature_cols = ['rel_bx', 'rel_by', 'pvx', 'pvy', 'heading_x', 'heading_y', 'dist']
    
    for ship_id in range(8):
        ship_name = SHIP_NAMES[ship_id]
        ship_df = df[df['Ship'] == ship_id].copy()
        
        if len(ship_df) < MIN_SAMPLES:
            print(f"\n{ship_name}: {len(ship_df)} samples (need {MIN_SAMPLES}) - SKIP")
            continue
        
        print(f"\n{ship_name}: {len(ship_df):,} samples")
        
        # Save ml_ready file (what train2.py expects)
        output_cols = feature_cols + ['dir_x', 'dir_y', 'behavior']
        out_df = ship_df[output_cols].copy()
        
        out_file = f"ml_ready_{ship_name}.csv"
        out_df.to_csv(out_file, index=False)
        print(f"  -> {out_file}")
        
        # Compute and save scaler
        X = ship_df[feature_cols].values.astype(np.float32)
        scaler = StandardScaler()
        scaler.fit(X)
        
        scaler_file = f"scaler_{ship_name}.txt"
        with open(scaler_file, 'w') as f:
            f.write("# StandardScaler for ShipBrains.h\n")
            f.write("# Copy these values to WARBIRD_SCALER (or appropriate ship)\n")
            f.write("#\n")
            f.write("# Feature order: rel_bx, rel_by, pvx, pvy, heading_x, heading_y, dist\n")
            f.write("#\n")
            f.write(f"mean = {', '.join(f'{v:.5f}' for v in scaler.mean_)}\n")
            f.write(f"scale = {', '.join(f'{v:.5f}' for v in scaler.scale_)}\n")
        print(f"  -> {scaler_file}")
        
        # Stats
        print(f"  rel_bx: mean={X[:,0].mean():+.2f}, std={X[:,0].std():.2f}")
        print(f"  rel_by: mean={X[:,1].mean():+.2f}, std={X[:,1].std():.2f}")  
        print(f"  dist:   mean={X[:,6].mean():.2f}, std={X[:,6].std():.2f}")
    
    print("\n" + "=" * 60)
    print("Done! Next: python train2.py")
    print("=" * 60)


if __name__ == "__main__":
    main()
