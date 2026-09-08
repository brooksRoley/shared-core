#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "PlayerEntity.h"
#include "SynergyEngine.h"
#include "Court.h"

class GameManager {
public:
    GameManager() = default;

    void SpawnPlayer(int id, std::string name, float speed, float shooting);
    void SetPlayerPlay(int playerId, int actionTypeInt, float targetX, float targetY);
    void TickSimulation(float deltaTime);
    std::string GetGameStateJSON();
    void StartRound();
    void LoadRosterJSON(std::string jsonData);
    void SetPlayerCoordinates(int playerId, float offX, float offY, float defX, float defY);
    void RemovePlayer(int playerId);

    // seriesState: 0=NORMAL, 1=ELIMINATION_GAME, 2=CLOSEOUT_GAME, 3=GAME_7
    // seriesTeam:  0=home, 1=away, -1=none. missingStars: count of OUT star cards.
    void SetSeriesContext(int seriesState, int seriesTeam, int missingStars);

    // Run a full game simulation. Returns JSON: {homeScore, awayScore, simTicks, synergies}
    // seed: RNG seed for determinism. ticks: number of simulation steps to run.
    std::string SimulateGame(uint32_t seed, int ticks);

    // Apply a home court advantage: boosts shooting and speed for home team players.
    void SetHomeCourtBonus(float shootingBonus, float speedBonus);

private:
    std::map<int, std::shared_ptr<PlayerEntity>> activeRoster;
    SynergyEngine synergyEngine;
    Court court;

    std::vector<std::shared_ptr<PlayerEntity>> GetActiveFloorPlayers();
    void SpawnBotOpponents();
};
