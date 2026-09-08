#include "SynergyEngine.h"
#include <map>
#include <cmath>
#include <algorithm>
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

    // Balanced Roster synergy — rewards depth builds (OKC-style)
    // A player is a "contributor" if their overall rating
    // (avg of shooting, defense, speed) >= 55. Depth is rewarded continuously:
    // every contributor beyond the 2 that a single-star build can field adds to
    // the buff, so a 5-deep roster out-scales a one-star-plus-fillers lineup
    // (Finding #7: depth builds were undervalued vs single-star builds).
    int solidCount = 0;
    for (const auto& player : activeFloor) {
        float overall = (player->stats.shooting + player->stats.defense + player->stats.speed) / 3.0f;
        if (overall >= 55.0f) solidCount++;
    }
    if (solidCount >= 4) {
        // Contributor bonus: scale with contributors above a baseline of 2
        // (mirrors the design draft's `max(0, len(contributors) - 2) * 3`).
        int tier = std::max(0, solidCount - 2);   // 2 at 4 solid, 3 at 5 solid
        float shootBuff = 3.0f * tier;
        float defBuff   = 3.0f * tier;
        float spdBuff   = 2.0f * tier;
        ActiveSynergy depthBonus{"Balanced Roster", tier, spdBuff, shootBuff, defBuff};
        currentBuffs.push_back(depthBonus);
        std::cout << "Synergy Activated: Balanced Roster (Tier " << tier
                  << ")! Depth rewarded across the board.\n";
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
