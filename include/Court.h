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

    float homeShootingBonus = 0.0f;
    float homeSpeedBonus    = 0.0f;

    void SetHomeCourtBonus(float shooting, float speed) {
        homeShootingBonus = shooting;
        homeSpeedBonus    = speed;
    }

    // Series context (Finding #17) + bench-hero escalation (Findings #18 + #44)
    enum class SeriesState { NORMAL, ELIMINATION_GAME, CLOSEOUT_GAME, GAME_7 };
    SeriesState seriesState = SeriesState::NORMAL;
    int seriesTeam   = -1;  // 0 = home, 1 = away, -1 = none (at-risk team in ELIM; closing team in CLOSEOUT)
    int missingStars = 0;   // count of drafter's star cards flagged OUT (supplied externally)

    void AddPlayer(std::shared_ptr<PlayerEntity> p, bool isHome);
    void Clear();
    void Reseed(uint32_t seed) { if (seed == 0) { rng.seed(std::random_device{}()); } else { rng.seed(seed); } }
    void InitPossession(); // Give ball to home player with highest shooting
    void UpdateSimulationStep(float dt);

    const std::vector<std::shared_ptr<PlayerEntity>>& GetHomeTeam() const { return homeTeam; }
    const std::vector<std::shared_ptr<PlayerEntity>>& GetAwayTeam()  const { return awayTeam; }

private:
    std::vector<std::shared_ptr<PlayerEntity>> homeTeam;
    std::vector<std::shared_ptr<PlayerEntity>> awayTeam;
    std::mt19937 rng{std::random_device{}()};
    float stealCooldown = 0.0f;

    void MovePlayerToward(PlayerEntity& p, Vector2D target, float dt);
    void AttemptShot(std::shared_ptr<PlayerEntity>& shooter, bool isHomeTeam);
    void AssignRebound(float dt);
    std::shared_ptr<PlayerEntity> FindNearestDefender(
        const std::shared_ptr<PlayerEntity>& attacker, bool isHomeAttacker);
};
