#include "SynergyEngine.h"
#include <map>
#include <cmath>
#include <iostream>

float SynergyEngine::CalculateSimilarity(const PlayerEntity& a, const PlayerEntity& b) {
    float hDiff = (a.stats.height_inches - b.stats.height_inches) * 2.0f;
    float sDiff = a.stats.shooting - b.stats.shooting;
    float dDiff = a.stats.defense - b.stats.defense;
    float spdDiff = a.stats.speed - b.stats.speed;

    return std::sqrt((hDiff*hDiff) + (sDiff*sDiff) + (dDiff*dDiff) + (spdDiff*spdDiff));
}

void SynergyEngine::AnalyzeRoster(const std::vector<std::shared_ptr<PlayerEntity>>& activeFloor) {
    currentBuffs.clear();
    if (activeFloor.empty()) return;

    std::map<std::string, int> teamCounts;
    int giantsCount = 0;
    int sharpshootersCount = 0;
    int lockdownCount = 0;
    float totalSpeed = 0.0f;

    for (const auto& player : activeFloor) {
        teamCounts[player->team]++;
        if (player->stats.height_inches >= 80) giantsCount++;
        if (player->stats.shooting >= 72.0f) sharpshootersCount++;
        if (player->stats.defense >= 85.0f) lockdownCount++;
        totalSpeed += player->stats.speed;
    }

    // Franchise Synergies
    for (const auto& [team, count] : teamCounts) {
        if (count >= 2) {
            ActiveSynergy syn;
            syn.name = team + " Franchise";
            syn.tier = count / 2;
            syn.shootingBuff = 5.0f * syn.tier;
            currentBuffs.push_back(syn);
        }
    }

    if (giantsCount >= 2) {
        ActiveSynergy twinTowers{"Twin Towers", giantsCount - 1, -5.0f, 0.0f, 15.0f};
        currentBuffs.push_back(twinTowers);
        std::cout << "Synergy Activated: Twin Towers! Paint defense heavily boosted.\n";
    }

    if (sharpshootersCount >= 3) {
        ActiveSynergy splashFamily{"Splash Family", 1, 5.0f, 20.0f, -5.0f};
        currentBuffs.push_back(splashFamily);
        std::cout << "Synergy Activated: Splash Family! Limitless range unlocked.\n";
    }

    if ((totalSpeed / activeFloor.size()) > 68.0f && activeFloor.size() >= 4) {
        ActiveSynergy runAndGun{"7 Seconds or Less", 2, 25.0f, 10.0f, -10.0f};
        currentBuffs.push_back(runAndGun);
        std::cout << "Synergy Activated: 7 Seconds or Less! Transition speed maximized.\n";
    }
}

std::vector<std::shared_ptr<PlayerEntity>> SynergyEngine::FindSimilarComps(
    const std::shared_ptr<PlayerEntity>& target,
    const std::vector<std::shared_ptr<PlayerEntity>>& shopPool)
{
    std::vector<std::shared_ptr<PlayerEntity>> recommendations;
    for (const auto& shopPlayer : shopPool) {
        if (CalculateSimilarity(*target, *shopPlayer) < 25.0f) {
            recommendations.push_back(shopPlayer);
        }
    }
    return recommendations;
}

const std::vector<ActiveSynergy>& SynergyEngine::GetActiveBuffs() const {
    return currentBuffs;
}
