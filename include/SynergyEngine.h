#pragma once
#include <vector>
#include <string>
#include <memory>
#include "PlayerEntity.h"

struct ActiveSynergy {
    std::string name;
    int tier = 0;
    float speedBuff = 0.0f;
    float shootingBuff = 0.0f;
    float defenseBuff = 0.0f;
};

class SynergyEngine {
public:
    void AnalyzeRoster(const std::vector<std::shared_ptr<PlayerEntity>>& activeFloor);

    std::vector<std::shared_ptr<PlayerEntity>> FindSimilarComps(
        const std::shared_ptr<PlayerEntity>& target,
        const std::vector<std::shared_ptr<PlayerEntity>>& shopPool);

    const std::vector<ActiveSynergy>& GetActiveBuffs() const;

private:
    std::vector<ActiveSynergy> currentBuffs;
    float CalculateSimilarity(const PlayerEntity& a, const PlayerEntity& b);
};
