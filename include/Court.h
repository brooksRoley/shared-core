#pragma once
#include <vector>
#include <memory>
#include <random>
#include <map>
#include "PlayerEntity.h"
#include "Basketball.h"

class Court {
public:
    int homeScore = 0;
    int awayScore = 0;
    int lastScorerId = -1;  // id of the player who made the most recent basket
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
    float passCooldown  = 0.0f;   // catch-and-go: no pass right after a catch
    std::map<int, size_t> spotOf;  // player id -> offensive spot slot (0-4)

    void AssignSpots();

    // A shot is decided at release and resolved when the ball reaches the rim:
    // the points (or the carom) happen on arrival, so the score, the rim
    // effects and the ball all land together.
    struct ShotFlight { bool made = false; bool home = false; int points = 0; int shooterId = -1;
                        float timeLeft = 0.0f; Vector2D hoop{0, 0}; };
    ShotFlight shot;
    // A pass travels; the receiver is chased, since he keeps moving.
    struct PassFlight { int receiverId = -1; int passerId = -1; bool home = false; float t = 0.0f;
                        float duration = 0.0f; Vector2D from{0, 0}; };
    PassFlight pass;

    void StepPlay(float dt);
    void UpdateShotFlight(float dt);
    void UpdatePassFlight(float dt);
    void PositionTeams(std::vector<std::shared_ptr<PlayerEntity>>& team,
                       std::vector<std::shared_ptr<PlayerEntity>>& opponents,
                       const std::shared_ptr<PlayerEntity>& focus, Vector2D targetHoop, float dt);
    void ResolveOverlaps();
    std::shared_ptr<PlayerEntity> FindPlayer(int id, bool* isHome = nullptr);

    void MovePlayerToward(PlayerEntity& p, Vector2D target, float dt);
    void AttemptShot(std::shared_ptr<PlayerEntity>& shooter, bool isHomeTeam);
    void AssignRebound(float dt);
    std::shared_ptr<PlayerEntity> FindNearestDefender(
        const std::shared_ptr<PlayerEntity>& attacker, bool isHomeAttacker);
};
