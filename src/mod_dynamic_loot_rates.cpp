#include "ScriptMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Config.h"
#include "Map.h"

#include <cmath>

struct DynamicLootRatesConfig {
    bool enabled = true;
    
    uint32 dungeonLootGroupRate = 1;
    uint32 dungeonLootReferenceRate = 1;

    uint32 raidLootGroupRate = 1;
    uint32 raidLootReferenceRate = 1;

    float worldLootGroupRate = 1.0f;
    float worldLootReferenceRate = 1.0f;
};

DynamicLootRatesConfig config;

class DynamicLootRates_WorldScript : public WorldScript {
public:
    DynamicLootRates_WorldScript()
            : WorldScript("DynamicLootRates_WorldScript") {
    }

    void OnBeforeConfigLoad(bool /*reload*/) override {
        config.enabled = sConfigMgr->GetOption<bool>("DynamicLootRates.Enable", true);
        config.dungeonLootGroupRate = sConfigMgr->GetOption<uint32>("DynamicLootRates.Dungeon.Rate.GroupAmount", 1);
        config.dungeonLootReferenceRate = sConfigMgr->GetOption<uint32>("DynamicLootRates.Dungeon.Rate.ReferencedAmount", 1);
        config.raidLootGroupRate = sConfigMgr->GetOption<uint32>("DynamicLootRates.Raid.Rate.GroupAmount", 1);
        config.raidLootReferenceRate = sConfigMgr->GetOption<uint32>("DynamicLootRates.Raid.Rate.ReferencedAmount", 1);
        config.worldLootGroupRate = sConfigMgr->GetOption<float>("Rate.Drop.Item.GroupAmount", 1.0f);
        config.worldLootReferenceRate = sConfigMgr->GetOption<float>("Rate.Drop.Item.ReferencedAmount", 1.0f);
    }
};

class DynamicLootRates_GlobalScript : public GlobalScript
{
public:
    DynamicLootRates_GlobalScript() : GlobalScript("DynamicLootRates_GlobalScript") {}

    void OnAfterCalculateLootGroupAmount(Player const *player, Loot & /*loot*/, uint16 /*lootMode*/, uint32 &groupAmount, LootStore const & /*store*/) override
    {
        if (!config.enabled) {
            return;
        }

        if (isDungeon(player->GetMap())) {
            groupAmount = AdjustInstanceAmount(groupAmount, config.worldLootGroupRate, config.dungeonLootGroupRate);
            LOG_DEBUG("module", "mod_dynamic_loot_rates: In dungeon: Applying loot group multiplier of {} (world rate {}), resulting in {}", config.dungeonLootGroupRate, config.worldLootGroupRate, groupAmount);
            return;
        }

        if (isRaid(player->GetMap())) {
            groupAmount = AdjustInstanceAmount(groupAmount, config.worldLootGroupRate, config.raidLootGroupRate);
            LOG_DEBUG("module", "mod_dynamic_loot_rates: In raid: Applying loot group multiplier of {} (world rate {}), resulting in {}", config.raidLootGroupRate, config.worldLootGroupRate, groupAmount);
            return;
        }
    }

    void OnAfterRefCount(Player const *player, LootStoreItem * /*LootStoreItem*/, Loot & /*loot*/, bool /*canRate*/, uint16 /*lootMode*/, uint32 &maxcount, LootStore const & /*store*/) override
    {
        if (!config.enabled) {
            return;
        }

        if (isDungeon(player->GetMap())) {
            maxcount = AdjustInstanceAmount(maxcount, config.worldLootReferenceRate, config.dungeonLootReferenceRate);
            LOG_DEBUG("module", "mod_dynamic_loot_rates: In dungeon: Applying loot reference multiplier of {} (world rate {}), resulting in {}", config.dungeonLootReferenceRate, config.worldLootReferenceRate, maxcount);
            return;
        }

        if (isRaid(player->GetMap())) {
            maxcount = AdjustInstanceAmount(maxcount, config.worldLootReferenceRate, config.raidLootReferenceRate);
            LOG_DEBUG("module", "mod_dynamic_loot_rates: In raid: Applying loot reference multiplier of {} (world rate {}), resulting in {}", config.raidLootReferenceRate, config.worldLootReferenceRate, maxcount);
            return;
        }
    }

    bool isDungeon(Map* map) {
        return map->IsDungeon() && !map->IsRaid();
    }

    bool isRaid(Map* map) {
        return map->IsRaid();
    }

private:
    uint32 AdjustInstanceAmount(uint32 currentAmount, float worldRate, uint32 instanceRate) const
    {
        if (worldRate <= 0.0f) {
            // Fallback to previous behavior if world rate is invalid.
            return currentAmount * instanceRate;
        }

        double normalizedAmount = static_cast<double>(currentAmount) / static_cast<double>(worldRate);
        double scaledAmount = normalizedAmount * static_cast<double>(instanceRate);

        if (scaledAmount < 0.0) {
            return 0;
        }

        return static_cast<uint32>(std::round(scaledAmount));
    }
};

// Add all scripts in one
void AddDynamicLootRateScripts()
{
    new DynamicLootRates_WorldScript();
    new DynamicLootRates_GlobalScript();
}
