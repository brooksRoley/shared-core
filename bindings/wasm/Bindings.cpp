#include <emscripten/bind.h>
#include "GameManager.h"

EMSCRIPTEN_BINDINGS(bball_tactics) {
    emscripten::class_<GameManager>("GameManager")
        .constructor<>()
        .function("SpawnPlayer", &GameManager::SpawnPlayer)
        .function("SetPlayerPlay", &GameManager::SetPlayerPlay)
        .function("TickSimulation", &GameManager::TickSimulation)
        .function("GetGameStateJSON", &GameManager::GetGameStateJSON)
        .function("StartRound", &GameManager::StartRound)
        .function("LoadRosterJSON", &GameManager::LoadRosterJSON)
        .function("SetPlayerCoordinates", &GameManager::SetPlayerCoordinates)
        .function("RemovePlayer", &GameManager::RemovePlayer);
}
