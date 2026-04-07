#pragma once
#include <vector>
#include <memory>
#include <random>
#include "PlayerEntity.h"
#include "Basketball.h"

class Court {
public:
    int homeScore = 0;
    int awayScore = 0;
    Basketball ball;

    void AddPlayer(std::shared_ptr<PlayerEntity> p, bool isHome);
    void Clear();
    void Reseed(uint32_t seed) { rng.seed(seed); }
    void InitPossession(); // Give ball to home player with highest shooting
    void UpdateSimulationStep(float dt);

    const std::vector<std::shared_ptr<PlayerEntity>>& GetHomeTeam() const { return homeTeam; }
    const std::vector<std::shared_ptr<PlayerEntity>>& GetAwayTeam()  const { return awayTeam; }

private:
    std::vector<std::shared_ptr<PlayerEntity>> homeTeam;
    std::vector<std::shared_ptr<PlayerEntity>> awayTeam;
    std::mt19937 rng{42};

    void MovePlayerToward(PlayerEntity& p, Vector2D target, float dt);
    void AttemptShot(std::shared_ptr<PlayerEntity>& shooter, bool isHomeTeam);
    void AssignRebound(float dt);
    std::shared_ptr<PlayerEntity> FindNearestDefender(
        const std::shared_ptr<PlayerEntity>& attacker, bool isHomeAttacker);
};
