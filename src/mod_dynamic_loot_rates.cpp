#include "ScriptMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Config.h"
#include "Map.h"
#include "LootMgr.h"

#include <cmath>
#include <unordered_map>
#include <algorithm>
#include <limits>

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

struct ReferenceBaseCounts
{
    uint32 mincount = 0;
    uint32 maxcount = 0;
};

// Cache the original (DB) reference counts so we always scale from the base values
// and avoid compounding across rolls.
std::unordered_map<LootStoreItem const*, ReferenceBaseCounts> referenceBaseCounts;

class DynamicLootRates_WorldScript : public WorldScript {
public:
    DynamicLootRates_WorldScript()
            : WorldScript("DynamicLootRates_WorldScript") {
    }

    void OnAfterConfigLoad(bool /*reload*/) override {
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
        if (!config.enabled || !player) {
            return;
        }

        Map* map = player->GetMap();
        if (!map) {
            return;
        }

        if (isDungeon(map)) {
            groupAmount = AdjustInstanceAmount(groupAmount, config.worldLootGroupRate, config.dungeonLootGroupRate);
            LOG_DEBUG("module", "mod_dynamic_loot_rates: In dungeon: Applying loot group multiplier of {} (world rate {}), resulting in {}", config.dungeonLootGroupRate, config.worldLootGroupRate, groupAmount);
            return;
        }

        if (isRaid(map)) {
            groupAmount = AdjustInstanceAmount(groupAmount, config.worldLootGroupRate, config.raidLootGroupRate);
            LOG_DEBUG("module", "mod_dynamic_loot_rates: In raid: Applying loot group multiplier of {} (world rate {}), resulting in {}", config.raidLootGroupRate, config.worldLootGroupRate, groupAmount);
            return;
        }
    }

    void OnAfterRefCount(Player const *player, LootStoreItem *lootStoreItem, Loot & /*loot*/, bool /*canRate*/, uint16 /*lootMode*/, uint32 &maxcount, LootStore const & /*store*/) override
    {
        if (!config.enabled || !player) {
            return;
        }

        Map* map = player->GetMap();
        if (!map) {
            return;
        }

        if (isDungeon(map)) {
            AdjustReferenceCounts(lootStoreItem, maxcount, config.worldLootReferenceRate, config.dungeonLootReferenceRate);
            LOG_DEBUG("module", "mod_dynamic_loot_rates: In dungeon: Applying loot reference multiplier of {} (world rate {}), resulting in {}", config.dungeonLootReferenceRate, config.worldLootReferenceRate, maxcount);
            return;
        }

        if (isRaid(map)) {
            AdjustReferenceCounts(lootStoreItem, maxcount, config.worldLootReferenceRate, config.raidLootReferenceRate);
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
    ReferenceBaseCounts const& GetReferenceBaseCounts(LootStoreItem const* item) const
    {
        auto const it = referenceBaseCounts.find(item);
        if (it != referenceBaseCounts.end())
            return it->second;

        ReferenceBaseCounts baseCounts;
        baseCounts.mincount = item ? item->mincountOrRef : 0;
        baseCounts.maxcount = item ? item->maxcount : 0;

        return referenceBaseCounts.emplace(item, baseCounts).first->second;
    }

    uint32 ApplyInstanceRate(uint32 baseAmount, float worldRate, uint32 instanceRate) const
    {
        double normalizedAmount = static_cast<double>(baseAmount);
        if (worldRate > 0.0f)
            normalizedAmount /= static_cast<double>(worldRate);

        double scaledAmount = normalizedAmount * static_cast<double>(instanceRate);

        if (scaledAmount < 0.0) {
            return 0;
        }

        return static_cast<uint32>(std::round(scaledAmount));
    }

    uint32 AdjustInstanceAmount(uint32 currentAmount, float worldRate, uint32 instanceRate) const
    {
        uint32 scaled = ApplyInstanceRate(currentAmount, worldRate, instanceRate);
        return scaled;
    }

    void AdjustReferenceCounts(LootStoreItem* item, uint32& maxcount, float worldRate, uint32 instanceRate) const
    {
        if (!item)
            return;

        ReferenceBaseCounts const& baseCounts = GetReferenceBaseCounts(item);

        uint32 const scaledMax = ApplyInstanceRate(baseCounts.maxcount, worldRate, instanceRate);

        uint32 const clampedMax = scaledMax;

        // Reference tables: ignore the base min and force both min and max to the scaled max
        item->mincountOrRef = static_cast<uint8>(std::min<uint32>(clampedMax, std::numeric_limits<uint8>::max()));

        maxcount = clampedMax;
    }
};

// Add all scripts in one
void AddDynamicLootRateScripts()
{
    new DynamicLootRates_WorldScript();
    new DynamicLootRates_GlobalScript();
}
