# shared-core

Canonical C++17 basketball simulation engine shared across three projects:

- **BballTactics** (Vue 3 auto-battler) — consumes via WebAssembly/Emscripten
- **BasketballTactics** (SwiftUI iOS app) — consumes via Swift/C++ interop
- **NbaApi** (Flask analytics API) — consumes via pybind11

## Structure

```
shared-core/
├── include/            # C++ headers (source of truth for all shared types)
├── src/                # C++ implementations
├── schema/             # JSON schema for data contracts (Python → C++)
├── bindings/
│   ├── wasm/           # Emscripten embind (Bindings.cpp)
│   └── python/         # pybind11 bindings (GameEconomy, StatNormalizer)
├── tests/              # Native C++ test suite
├── CMakeLists.txt      # Builds: static lib, WASM, pybind11, test runner
└── dist/               # Build outputs (gitignored)
```

## Shared Types

| Type | Header | Description |
|------|--------|-------------|
| `Vector2D`, `Vector3D` | `Vector.h` | 2D/3D math with operators, magnitude, normalize, distance |
| `PlayerStats` | `PlayerEntity.h` | shooting, defense, speed, height, weight, stamina |
| `PlayerEntity` | `PlayerEntity.h` | Full player model with spatial state, actions, formations |
| `Position`, `ActionType`, `PlayerState` | `PlayerEntity.h` | Game enums |
| `Basketball` | `Basketball.h` | Ball with 3D physics (gravity, bounce damping) |
| `Court` | `Court.h` | 800x400 simulation with teams, scoring, possession |
| `SynergyEngine` | `SynergyEngine.h` | Roster buff system (Franchise, Twin Towers, Splash Family, 7SOL) |
| `ShotProbability` | `ShotProbability.h` | Contest-aware shot math with exponential decay |
| `GameEconomy` | `GameEconomy.h` | Salary cap → 1-5 cost tiers, z-score normalization |
| `GameSeason` | `GameSeason.h` | Round state machine, draft lottery |
| `GameManager` | `GameManager.h` | Top-level orchestration, JSON state export |

## Build

### Prerequisites

- CMake 3.15+
- C++17 compiler (AppleClang, GCC, or Clang)
- Emscripten 5.x (for WASM target)
- pybind11 (for Python target, optional)

### Native (static library + tests)

```bash
mkdir -p build && cd build
cmake ..
make
./test_runner
```

### WebAssembly

```bash
mkdir -p build-wasm && cd build-wasm
emcmake cmake ..
emmake make
# Output: dist/wasm/engine.js + engine.wasm
```

### Python bindings

```bash
pip install pybind11
mkdir -p build-py && cd build-py
cmake -Dpybind11_DIR=$(python -m pybind11 --cmakedir) ..
make
# Output: dist/python/bball_py.so
```

## Data Contract

The JSON schema at `schema/engine_roster.schema.json` defines the data contract between NbaApi (producer) and the game engine (consumer). The `scraper.py` in BballTactics transforms raw NBA stats into this format using z-score normalization.

### Roster payload format

```json
[
  {
    "id": 1,
    "name": "Steph Curry",
    "team": "GSW",
    "cost": 5,
    "stats": { "shooting": 90, "speed": 75, "defense": 60 }
  }
]
```

## Integration Guide

### BballTactics (WASM)

Copy `dist/wasm/engine.js` and `engine.wasm` to `BballTactics/public/`. The Vue 3 client loads them via `<script src="/engine.js">` and calls `await Module()`.

### BasketballTactics (Swift)

Add this repo as a Swift Package dependency or git submodule. Enable C++ interop in your Xcode build settings (`SWIFT_OBJC_INTEROP_MODE = objcxx`). Import the headers via a bridging header or module map. The Swift-native PlayerEntity, SynergyEngine, etc. can then be replaced with calls to the C++ implementations.

### NbaApi (Python)

Build the pybind11 module and import it in scraper.py to use the canonical `StatNormalizer` and `GameEconomy` instead of the Python reimplementation.
