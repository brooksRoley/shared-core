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

private:
    std::map<int, std::shared_ptr<PlayerEntity>> activeRoster;
    SynergyEngine synergyEngine;
    Court court;

    std::vector<std::shared_ptr<PlayerEntity>> GetActiveFloorPlayers();
    void SpawnBotOpponents();
};
