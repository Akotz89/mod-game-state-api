/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "GameStateUtilities.h"
#include "GameStateAPI.h"
#include "RBAC.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Item.h"
#include "Bag.h"
#include "ItemEnchantmentMgr.h"
#include <set>
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "World.h"
#include "Group.h"
#include "QuestDef.h"
#include "DBCStores.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <fmt/format.h>
#include <map>
#include <shared_mutex>

namespace GameStateUtilities
{
    nlohmann::json GetItemData(Item* item, std::shared_ptr<GameStateRequestControl> const& control)
    {
        nlohmann::json itemData = nlohmann::json::object();

        if (!item || !ConsumeGameStateRequestWork(control))
            return itemData;

        const ItemTemplate* itemTemplate = item->GetTemplate();
        if (!itemTemplate)
        {
            itemData = {
                {"entry", item->GetEntry()},
                {"count", item->GetCount()},
                {"durability", item->GetUInt32Value(ITEM_FIELD_DURABILITY)},
                {"max_durability", item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY)}
            };
            return itemData;
        }

        itemData["entry"] = item->GetEntry();
        itemData["count"] = item->GetCount();
        itemData["name"] = itemTemplate->Name1;
        itemData["quality"] = itemTemplate->Quality;
        itemData["item_level"] = itemTemplate->ItemLevel;
        itemData["required_level"] = itemTemplate->RequiredLevel;
        itemData["class"] = itemTemplate->Class;
        itemData["subclass"] = itemTemplate->SubClass;
        itemData["inventory_type"] = itemTemplate->InventoryType;
        itemData["durability"] = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
        itemData["max_durability"] = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);

        nlohmann::json stats = nlohmann::json::array();
        for (uint32 i = 0; i < itemTemplate->StatsCount && i < MAX_ITEM_PROTO_STATS; ++i)
        {
            if (!ConsumeGameStateRequestWork(control))
                return nlohmann::json::object();
            if (itemTemplate->ItemStat[i].ItemStatValue != 0)
            {
                stats.push_back({
                    {"type", itemTemplate->ItemStat[i].ItemStatType},
                    {"value", itemTemplate->ItemStat[i].ItemStatValue}
                });
            }
        }
        itemData["stats"] = stats;

        itemData["resistances"] = {
            {"armor", itemTemplate->Armor},
            {"holy", itemTemplate->HolyRes},
            {"fire", itemTemplate->FireRes},
            {"nature", itemTemplate->NatureRes},
            {"frost", itemTemplate->FrostRes},
            {"shadow", itemTemplate->ShadowRes},
            {"arcane", itemTemplate->ArcaneRes}
        };

        if (itemTemplate->Class == ITEM_CLASS_WEAPON)
        {
            nlohmann::json weaponData = nlohmann::json::object();
            weaponData["delay"] = itemTemplate->Delay;
            weaponData["dps"] = itemTemplate->getDPS();

            nlohmann::json damages = nlohmann::json::array();
            for (uint32 i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
            {
                if (!ConsumeGameStateRequestWork(control))
                    return nlohmann::json::object();
                if (itemTemplate->Damage[i].DamageMin > 0 || itemTemplate->Damage[i].DamageMax > 0)
                {
                    damages.push_back({
                        {"min", itemTemplate->Damage[i].DamageMin},
                        {"max", itemTemplate->Damage[i].DamageMax},
                        {"type", itemTemplate->Damage[i].DamageType}
                    });
                }
            }
            weaponData["damages"] = damages;
            itemData["weapon_data"] = weaponData;
        }

        nlohmann::json sockets = nlohmann::json::array();
        for (uint32 i = 0; i < MAX_ITEM_PROTO_SOCKETS; ++i)
        {
            if (!ConsumeGameStateRequestWork(control))
                return nlohmann::json::object();
            if (itemTemplate->Socket[i].Color)
            {
                sockets.push_back({
                    {"color", itemTemplate->Socket[i].Color},
                    {"content", itemTemplate->Socket[i].Content}
                });
            }
        }
        if (!sockets.empty())
        {
            itemData["sockets"] = sockets;
            if (itemTemplate->socketBonus)
            {
                itemData["socket_bonus"] = itemTemplate->socketBonus;
            }
        }

        nlohmann::json spells = nlohmann::json::array();
        for (uint32 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            if (!ConsumeGameStateRequestWork(control))
                return nlohmann::json::object();
            if (itemTemplate->Spells[i].SpellId > 0)
            {
                spells.push_back({
                    {"spell_id", itemTemplate->Spells[i].SpellId},
                    {"trigger", itemTemplate->Spells[i].SpellTrigger},
                    {"charges", itemTemplate->Spells[i].SpellCharges},
                    {"cooldown", itemTemplate->Spells[i].SpellCooldown}
                });
            }
        }
        if (!spells.empty())
        {
            itemData["spells"] = spells;
        }

        itemData["item_set"] = itemTemplate->ItemSet;
        itemData["bonding"] = itemTemplate->Bonding;
        itemData["stack_size"] = itemTemplate->GetMaxStackSize();
        itemData["sell_price"] = itemTemplate->SellPrice;
        itemData["buy_price"] = itemTemplate->BuyPrice;

        return itemData;
    }

    nlohmann::json GetPlayerEquipment(Player* player, std::shared_ptr<GameStateRequestControl> const& control)
    {
        nlohmann::json equipment = nlohmann::json::object();

        if (!player || !ConsumeGameStateRequestWork(control))
            return IsGameStateRequestActive(control) ? equipment : nlohmann::json::object();

        const std::map<uint8, std::string> slotNames = {
            {EQUIPMENT_SLOT_HEAD, "head"},
            {EQUIPMENT_SLOT_NECK, "neck"},
            {EQUIPMENT_SLOT_SHOULDERS, "shoulders"},
            {EQUIPMENT_SLOT_BODY, "body"},
            {EQUIPMENT_SLOT_CHEST, "chest"},
            {EQUIPMENT_SLOT_WAIST, "waist"},
            {EQUIPMENT_SLOT_LEGS, "legs"},
            {EQUIPMENT_SLOT_FEET, "feet"},
            {EQUIPMENT_SLOT_WRISTS, "wrists"},
            {EQUIPMENT_SLOT_HANDS, "hands"},
            {EQUIPMENT_SLOT_FINGER1, "finger1"},
            {EQUIPMENT_SLOT_FINGER2, "finger2"},
            {EQUIPMENT_SLOT_TRINKET1, "trinket1"},
            {EQUIPMENT_SLOT_TRINKET2, "trinket2"},
            {EQUIPMENT_SLOT_BACK, "back"},
            {EQUIPMENT_SLOT_MAINHAND, "mainhand"},
            {EQUIPMENT_SLOT_OFFHAND, "offhand"},
            {EQUIPMENT_SLOT_RANGED, "ranged"},
            {EQUIPMENT_SLOT_TABARD, "tabard"}
        };

        for (const auto& [slot, name] : slotNames)
        {
            if (!ConsumeGameStateRequestWork(control))
                return nlohmann::json::object();
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            {
                equipment[name] = GetItemData(item, control);
            }
            else
            {
                equipment[name] = nullptr;
            }
        }

        return IsGameStateRequestActive(control) ? equipment : nlohmann::json::object();
    }

    nlohmann::json GetPlayerInventory(Player* player, std::shared_ptr<GameStateRequestControl> const& control)
    {
        if (!player || !ConsumeGameStateRequestWork(control))
            return nlohmann::json::object();

        constexpr std::size_t MaxRows = PLAYER_SLOT_END - (BUYBACK_SLOT_END - BUYBACK_SLOT_START)
            + ((INVENTORY_SLOT_BAG_END - INVENTORY_SLOT_BAG_START)
                + (BANK_SLOT_BAG_END - BANK_SLOT_BAG_START)) * MAX_BAG_SIZE;
        auto unavailable = [] { return nlohmann::json{{"error", "Complete inventory unavailable"}, {"status", 503}}; };
        std::string owner = std::to_string(player->GetGUID().GetRawValue());
        nlohmann::json rows = nlohmann::json::array();
        nlohmann::json seeds = nlohmann::json::array();
        nlohmann::json containers = nlohmann::json::array();
        std::set<uint32> identities;
        std::set<std::pair<uint32, int32>> seedKeys;
        auto copyItem = [&](Item* item, Bag* container, uint8 slot) {
            if (!item)
                return true;
            if (!ConsumeGameStateRequestWork(control))
                return false;
            uint32 guid = item->GetGUID().GetCounter();
            if (!guid || !item->GetTemplate() || !item->GetCount() || item->GetSlot() != slot
                || item->GetContainer() != container || std::to_string(item->GetOwnerGUID().GetRawValue()) != owner
                || rows.size() >= MaxRows || !identities.insert(guid).second)
                return false;
            uint32 entry = item->GetEntry();
            if (!entry)
                return false;
            int32 property = item->GetItemRandomPropertyId();
            rows.push_back({{"item_guid", std::to_string(guid)}, {"owner_guid", owner},
                {"bag_guid", container ? std::to_string(container->GetGUID().GetCounter()) : "0"},
                {"slot", slot}, {"entry", entry}, {"count", item->GetCount()},
                {"suffix", item->GetItemSuffixFactor()}, {"property", property}});
            if (seedKeys.emplace(entry, property).second)
                seeds.push_back({{"entry", entry}, {"property", property},
                    {"load_suffix", property < 0 ? GenerateEnchSuffixFactor(entry) : 0},
                    {"derivation", property < 0 ? "GenerateEnchSuffixFactor" : "loader_zero"}});
            return true;
        };
        for (uint16 slot = 0; slot < PLAYER_SLOT_END; ++slot)
        {
            if (slot >= BUYBACK_SLOT_START && slot < BUYBACK_SLOT_END)
                continue;
            if (!ConsumeGameStateRequestWork(control))
                return nlohmann::json::object();
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!copyItem(item, nullptr, slot))
                return unavailable();
            if ((slot >= INVENTORY_SLOT_BAG_START && slot < INVENTORY_SLOT_BAG_END)
                || (slot >= BANK_SLOT_BAG_START && slot < BANK_SLOT_BAG_END))
            {
                Bag* bag = player->GetBagByPos(slot);
                if ((item && !bag) || (bag && static_cast<Item*>(bag) != item))
                    return unavailable();
                if (!bag)
                    continue;
                uint32 slots = bag->GetBagSize();
                if (slots > MAX_BAG_SIZE)
                    return unavailable();
                containers.push_back({{"item_guid", std::to_string(bag->GetGUID().GetCounter())},
                    {"entry", bag->GetEntry()}, {"slots", slots}});
                for (uint32 child = 0; child < slots; ++child)
                {
                    if (!ConsumeGameStateRequestWork(control))
                        return nlohmann::json::object();
                    if (!copyItem(bag->GetItemByPos(child), bag, child))
                        return unavailable();
                }
            }
        }
        if (!IsGameStateRequestActive(control))
            return nlohmann::json::object();
        return {{"complete", true}, {"guid", owner}, {"money", player->GetMoney()},
            {"items", std::move(rows)}, {"load_suffix_evidence", std::move(seeds)},
            {"container_evidence", std::move(containers)}};
    }

    nlohmann::json GetPlayerStats(Player* player, std::shared_ptr<GameStateRequestControl> const& control)
    {
        nlohmann::json stats = nlohmann::json::object();

        if (!player || !ConsumeGameStateRequestWork(control))
            return IsGameStateRequestActive(control) ? stats : nlohmann::json::object();

        stats["level"] = player->GetLevel();
        stats["experience"] = {
            {"current", player->GetUInt32Value(PLAYER_XP)},
            {"next_level", player->GetUInt32Value(PLAYER_NEXT_LEVEL_XP)}
        };

        stats["attributes"] = {
            {"strength", player->GetStat(STAT_STRENGTH)},
            {"agility", player->GetStat(STAT_AGILITY)},
            {"stamina", player->GetStat(STAT_STAMINA)},
            {"intellect", player->GetStat(STAT_INTELLECT)},
            {"spirit", player->GetStat(STAT_SPIRIT)}
        };

        stats["health"] = {
            {"current", player->GetHealth()},
            {"max", player->GetMaxHealth()}
        };

        stats["power"] = {
            {"mana", {
                {"current", player->GetPower(POWER_MANA)},
                {"max", player->GetMaxPower(POWER_MANA)}
            }},
            {"rage", {
                {"current", player->GetPower(POWER_RAGE)},
                {"max", player->GetMaxPower(POWER_RAGE)}
            }},
            {"energy", {
                {"current", player->GetPower(POWER_ENERGY)},
                {"max", player->GetMaxPower(POWER_ENERGY)}
            }},
            {"focus", {
                {"current", player->GetPower(POWER_FOCUS)},
                {"max", player->GetMaxPower(POWER_FOCUS)}
            }},
            {"happiness", {
                {"current", player->GetPower(POWER_HAPPINESS)},
                {"max", player->GetMaxPower(POWER_HAPPINESS)}
            }},
            {"runes", {
                {"current", player->GetPower(POWER_RUNE)},
                {"max", player->GetMaxPower(POWER_RUNE)}
            }},
            {"runic_power", {
                {"current", player->GetPower(POWER_RUNIC_POWER)},
                {"max", player->GetMaxPower(POWER_RUNIC_POWER)}
            }}
        };

        stats["combat"] = {
            {"attack_power", player->GetTotalAttackPowerValue(BASE_ATTACK)},
            {"ranged_attack_power", player->GetTotalAttackPowerValue(RANGED_ATTACK)},
            {"spell_power", player->GetBaseSpellPowerBonus()},
            {"base_spell_power", player->GetBaseSpellPowerBonus()},
            {"healing_bonus", player->GetInt32Value(PLAYER_FIELD_MOD_HEALING_DONE_POS)},
            {"spell_damage_bonus_by_school", {
                {"holy", player->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_HOLY)},
                {"fire", player->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_FIRE)},
                {"nature", player->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_NATURE)},
                {"frost", player->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_FROST)},
                {"shadow", player->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_SHADOW)},
                {"arcane", player->GetInt32Value(PLAYER_FIELD_MOD_DAMAGE_DONE_POS + SPELL_SCHOOL_ARCANE)}
            }},
            {"critical_chance", {
                {"melee", player->GetFloatValue(PLAYER_CRIT_PERCENTAGE)},
                {"ranged", player->GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE)},
                {"spell", player->GetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + static_cast<uint16>(SPELL_SCHOOL_HOLY))},
                {"spell_by_school", {
                    {"holy", player->GetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + static_cast<uint16>(SPELL_SCHOOL_HOLY))},
                    {"fire", player->GetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + static_cast<uint16>(SPELL_SCHOOL_FIRE))},
                    {"nature", player->GetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + static_cast<uint16>(SPELL_SCHOOL_NATURE))},
                    {"frost", player->GetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + static_cast<uint16>(SPELL_SCHOOL_FROST))},
                    {"shadow", player->GetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + static_cast<uint16>(SPELL_SCHOOL_SHADOW))},
                    {"arcane", player->GetFloatValue(PLAYER_SPELL_CRIT_PERCENTAGE1 + static_cast<uint16>(SPELL_SCHOOL_ARCANE))}
                }}
            }},
            {"hit_chance", {
                {"melee", player->m_modMeleeHitChance},
                {"ranged", player->m_modRangedHitChance},
                {"spell", player->m_modSpellHitChance}
            }}
        };

        stats["resistances"] = {
            {"armor", player->GetArmor()},
            {"holy", player->GetResistance(SPELL_SCHOOL_HOLY)},
            {"fire", player->GetResistance(SPELL_SCHOOL_FIRE)},
            {"nature", player->GetResistance(SPELL_SCHOOL_NATURE)},
            {"frost", player->GetResistance(SPELL_SCHOOL_FROST)},
            {"shadow", player->GetResistance(SPELL_SCHOOL_SHADOW)},
            {"arcane", player->GetResistance(SPELL_SCHOOL_ARCANE)}
        };

        stats["status"] = {
            {"alive", player->IsAlive()},
            {"in_combat", player->IsInCombat()},
            {"resting", player->HasPlayerFlag(PLAYER_FLAGS_RESTING)},
            {"ghost", player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST)},
            {"player_vs_player", player->IsPvP()},
            {"away", player->isAFK()},
            {"dnd", player->isDND()}
        };

        stats["average_item_level"] = player->GetAverageItemLevel();

        return IsGameStateRequestActive(control) ? stats : nlohmann::json::object();
    }

    nlohmann::json GetPlayerTalentInfo(Player* player, std::shared_ptr<GameStateRequestControl> const& control)
    {
        nlohmann::json talents = nlohmann::json::object();

        if (!player || !ConsumeGameStateRequestWork(control))
            return IsGameStateRequestActive(control) ? talents : nlohmann::json::object();

        talents["active_spec"] = player->GetActiveSpec();
        talents["specs_count"] = player->GetSpecsCount();

        uint32 totalTalentPoints = player->CalculateTalentsPoints();
        uint32 freeTalentPoints = player->GetFreeTalentPoints();
        uint32 usedTalentPoints = (totalTalentPoints >= freeTalentPoints) ? (totalTalentPoints - freeTalentPoints) : 0;

        talents["talent_points"] = {
            {"available", freeTalentPoints},
            {"used", usedTalentPoints},
            {"total", totalTalentPoints}
        };

        nlohmann::json learned = nlohmann::json::array();
        for (auto const& [spellId, talent] : player->GetTalentMap())
        {
            if (!ConsumeGameStateRequestWork(control))
                return nlohmann::json::object();
            if (!talent || talent->State == PLAYERSPELL_REMOVED)
                continue;
            nlohmann::json entry = {{"spell_id", spellId}, {"talent_id", talent->talentID},
                {"spec_mask", static_cast<uint8>(talent->specMask)}, {"in_spellbook", talent->inSpellBook}};
            if (TalentSpellPos const* position = GetTalentSpellPos(spellId))
            {
                entry["talent_id"] = position->talent_id;
                entry["rank"] = position->rank + 1;
            }
            else
                entry["missing_template"] = true;
            learned.push_back(std::move(entry));
        }
        talents["learned_talents"] = std::move(learned);
        return IsGameStateRequestActive(control) ? talents : nlohmann::json::object();
    }

    nlohmann::json GetPlayerData(Player* player, std::shared_ptr<GameStateRequestControl> const& control, bool includeEquipment)
    {
        nlohmann::json data = nlohmann::json::object();

        if (!player || !ConsumeGameStateRequestWork(control))
            return IsGameStateRequestActive(control) ? data : nlohmann::json::object();

        WorldSession* session = player->GetSession();

        data["name"] = player->GetName();
        data["level"] = player->GetLevel();
        data["class"] = player->getClass();
        data["race"] = player->getRace();
        data["gender"] = player->getGender();
        data["guid"] = player->GetGUID().GetCounter();
        data["zone_id"] = player->GetZoneId();
        data["area_id"] = player->GetAreaId();
        data["map_id"] = player->GetMapId();
        data["online"] = player->IsInWorld();

        if (session)
        {
            data["account_id"] = session->GetAccountId();
            data["account_name"] = session->GetRBACData()
                ? nlohmann::json(session->GetRBACData()->GetName()) : nlohmann::json(nullptr);
            data["latency"] = session->GetLatency();
            data["security_level"] = static_cast<uint32>(session->GetSecurity());
        }

        if (Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId()))
        {
            data["guild"] = {
                {"id", player->GetGuildId()},
                {"name", guild->GetName()},
                {"rank", player->GetRank()}
            };
        }
        else
        {
            data["guild"] = nullptr;
        }

        data["money"] = player->GetMoney();
        data["played_time"] = {
            {"total", player->GetTotalPlayedTime()},
            {"level", player->GetLevelPlayedTime()}
        };
        data["honor_points"] = player->GetHonorPoints();
        data["arena_points"] = player->GetArenaPoints();

        data["position"] = {
            {"x", player->GetPositionX()},
            {"y", player->GetPositionY()},
            {"z", player->GetPositionZ()},
            {"orientation", player->GetOrientation()}
        };

        data["health"] = {
            {"current", player->GetHealth()},
            {"max", player->GetMaxHealth()}
        };

        Powers primaryPower = player->getPowerType();
        data["power"] = {
            {"type", static_cast<uint32>(primaryPower)},
            {"current", player->GetPower(primaryPower)},
            {"max", player->GetMaxPower(primaryPower)}
        };

        if (Group* group = player->GetGroup())
        {
            data["group"] = {
                {"id", group->GetGUID().GetCounter()},
                {"leader_guid", group->GetLeaderGUID().GetCounter()},
                {"members_count", group->GetMembersCount()},
                {"is_leader", group->IsLeader(player->GetGUID())},
                {"is_assistant", group->IsAssistant(player->GetGUID())},
                {"loot_method", static_cast<uint32>(group->GetLootMethod())},
                {"is_raid", group->isRaidGroup()},
                {"is_bg_group", group->isBGGroup()},
                {"is_lfg_group", group->isLFGGroup()}
            };
        }
        else
        {
            data["group"] = nullptr;
        }

        data["stats"] = {
            {"strength", player->GetStat(STAT_STRENGTH)},
            {"agility", player->GetStat(STAT_AGILITY)},
            {"stamina", player->GetStat(STAT_STAMINA)},
            {"intellect", player->GetStat(STAT_INTELLECT)},
            {"spirit", player->GetStat(STAT_SPIRIT)},
            {"average_item_level", player->GetAverageItemLevel()}
        };

        data["status"] = {
            {"alive", player->IsAlive()},
            {"in_combat", player->IsInCombat()},
            {"ghost", player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST)},
            {"resting", player->HasPlayerFlag(PLAYER_FLAGS_RESTING)},
            {"away", player->isAFK()},
            {"dnd", player->isDND()},
            {"gm", player->IsGameMaster()}
        };

        if (includeEquipment)
        {
            data["equipment"] = GetPlayerEquipment(player, control);
        }

        return IsGameStateRequestActive(control) ? data : nlohmann::json::object();
    }

    nlohmann::json GetServerData(std::shared_ptr<GameStateRequestControl> const& control)
    {
        nlohmann::json data = nlohmann::json::object();
        if (!ConsumeGameStateRequestWork(control))
            return data;

        Seconds uptime = GameTime::GetUptime();
        Seconds currentTime = GameTime::GetGameTime();
        Seconds startTime = GameTime::GetStartTime();

        data["uptime_seconds"] = uptime.count();
        data["current_time"] = currentTime.count();
        data["start_time"] = startTime.count();
        data["effective_world_config"] = {
            {"schema", "cpi388-cached-world-config-v1"},
            {"world_config_load_callback_count", GetWorldConfigLoadCallbackCount()},
            {"leave_group_on_logout", sWorld->getBoolConfig(CONFIG_LEAVE_GROUP_ON_LOGOUT)}
        };
        {
            std::shared_lock lock(*HashMapHolder<Player>::GetLock());
            std::size_t characters = 0;
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            {
                if (!ConsumeGameStateRequestWork(control))
                    return nlohmann::json::object();
                if (player && player->IsInWorld())
                    ++characters;
            }
            data["characters_in_world"] = characters;
        }
        data["player_count"] = sWorldSessionMgr->GetPlayerCount();
        data["online_players"] = data["player_count"];
        data["max_player_count"] = sWorldSessionMgr->GetMaxPlayerCount();
        data["active_sessions"] = sWorldSessionMgr->GetActiveSessionCount();
        data["queued_sessions"] = sWorldSessionMgr->GetQueuedSessionCount();
        data["total_sessions"] = sWorldSessionMgr->GetActiveAndQueuedSessionCount();

        uint32 uptimeSeconds = static_cast<uint32>(uptime.count());
        uint32 days = uptimeSeconds / 86400;
        uint32 hours = (uptimeSeconds % 86400) / 3600;
        uint32 minutes = (uptimeSeconds % 3600) / 60;
        uint32 seconds = uptimeSeconds % 60;

        data["uptime_formatted"] = fmt::format("{}d {}h {}m {}s", days, hours, minutes, seconds);

        return IsGameStateRequestActive(control) ? data : nlohmann::json::object();
    }

    nlohmann::json GetAllPlayersData(std::shared_ptr<GameStateRequestControl> const& control, bool includeEquipment)
    {
        nlohmann::json players = nlohmann::json::array();

        // Playerbots own sessions outside WorldSessionMgr; the native registry
        // includes every player and requires its shared membership lock.
        std::shared_lock lock(*HashMapHolder<Player>::GetLock());
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        {
            if (!ConsumeGameStateRequestWork(control))
                return nlohmann::json::object();
            if (player && player->IsInWorld())
                players.push_back(GetPlayerData(player, control, includeEquipment));
        }

        return IsGameStateRequestActive(control) ? players : nlohmann::json::array();
    }

    Player* FindPlayerByName(const std::string& name, std::shared_ptr<GameStateRequestControl> const& control)
    {
        if (!ConsumeGameStateRequestWork(control))
            return nullptr;
        return ObjectAccessor::FindPlayerByName(name);
    }

    nlohmann::json GetPlayerSkills(Player* player, std::shared_ptr<GameStateRequestControl> const& control)
    {
        nlohmann::json skills = nlohmann::json::object();

        if (!player || !ConsumeGameStateRequestWork(control))
            return IsGameStateRequestActive(control) ? skills : nlohmann::json::object();

        nlohmann::json castableSpells = nlohmann::json::array();

        const PlayerSpellMap& spellMap = player->GetSpellMap();
        for (const auto& spellPair : spellMap)
        {
            if (!ConsumeGameStateRequestWork(control))
                return nlohmann::json::object();
            uint32 spellId = spellPair.first;
            const PlayerSpell* playerSpell = spellPair.second;

            if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED)
                continue;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!spellInfo)
            {
                castableSpells.push_back({{"spell_id", spellId}, {"active", static_cast<bool>(playerSpell->Active)},
                    {"spec_mask", static_cast<uint8>(playerSpell->specMask)}, {"missing_template", true}});
                continue;
            }

            if (spellInfo->IsPassive())
                continue;

            nlohmann::json spellData = {
                {"spell_id", spellId},
                {"active", static_cast<bool>(playerSpell->Active)},
                {"spec_mask", static_cast<uint8>(playerSpell->specMask)},
                {"name", spellInfo->SpellName[0] ? spellInfo->SpellName[0] : "Unknown"},
                {"rank", spellInfo->Rank[0] ? spellInfo->Rank[0] : ""},
                {"school", spellInfo->SchoolMask},
                {"cast_time", spellInfo->CastTimeEntry ? spellInfo->CastTimeEntry->CastTime : 0},
                {"cooldown", spellInfo->RecoveryTime},
                {"range", spellInfo->RangeEntry ? spellInfo->RangeEntry->RangeMax[0] : 0.0f}
            };

            castableSpells.push_back(spellData);
        }

        skills["castable_spells"] = castableSpells;
        skills["spell_count"] = castableSpells.size();

        return IsGameStateRequestActive(control) ? skills : nlohmann::json::object();
    }

    nlohmann::json GetPlayerSkillsFull(Player* player, std::shared_ptr<GameStateRequestControl> const& control)
    {
        nlohmann::json skills = nlohmann::json::object();

        if (!player || !ConsumeGameStateRequestWork(control))
            return IsGameStateRequestActive(control) ? skills : nlohmann::json::object();

        nlohmann::json skillsArray = nlohmann::json::array();

        for (uint32 i = 0; i < PLAYER_MAX_SKILLS; ++i)
        {
            if (!ConsumeGameStateRequestWork(control))
                return nlohmann::json::object();
            uint32 skill = player->GetUInt32Value(PLAYER_SKILL_INFO_1_1 + i * 3);
            if (skill == 0)
                continue;

            uint16 skillId = SKILL_VALUE(skill);
            uint16 skillStep = SKILL_MAX(skill);

            if (skillId == 0)
                continue;

            uint16 skillValue = player->GetSkillValue(skillId);
            uint16 maxSkillValue = player->GetMaxSkillValue(skillId);
            uint16 pureSkillValue = player->GetPureSkillValue(skillId);
            int16 permBonus = player->GetSkillPermBonusValue(skillId);
            int16 tempBonus = player->GetSkillTempBonusValue(skillId);

            nlohmann::json skillData = {
                {"skill_id", skillId},
                {"skill_step", skillStep},
                {"current_value", skillValue},
                {"max_value", maxSkillValue},
                {"pure_value", pureSkillValue},
                {"permanent_bonus", permBonus},
                {"temporary_bonus", tempBonus}
            };

            if (SkillLineEntry const* skillLineEntry = sSkillLineStore.LookupEntry(skillId))
            {
                skillData["name"] = skillLineEntry->name[0]; // Default locale
            }

            skillsArray.push_back(skillData);
        }

        skills["passive_skills"] = skillsArray;
        skills["skills"] = std::move(skillsArray);

        nlohmann::json talentsInfo = GetPlayerTalentInfo(player, control);
        skills["talents"] = talentsInfo;

        nlohmann::json castableData = GetPlayerSkills(player, control);
        if (!IsGameStateRequestActive(control))
            return nlohmann::json::object();
        skills["castable_spells"] = castableData["castable_spells"];
        skills["spell_count"] = castableData["spell_count"];

        return IsGameStateRequestActive(control) ? skills : nlohmann::json::object();
    }

    nlohmann::json GetQuestData(uint32 questId, QuestStatus status)
    {
        nlohmann::json data = {{"quest_id", questId}, {"status", static_cast<uint32>(status)}};
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
        {
            data["missing_template"] = true;
            return data;
        }
        data.update({{"title", quest->GetTitle()}, {"description", quest->GetDetails()},
            {"level", quest->GetQuestLevel()}, {"min_level", quest->GetMinLevel()},
            {"quest_type", quest->GetType()}, {"suggested_players", quest->GetSuggestedPlayers()},
            {"time_limit", quest->GetTimeAllowed()}, {"is_daily", quest->IsDaily()},
            {"is_weekly", quest->IsWeekly()}, {"is_repeatable", quest->IsRepeatable()}});
        return data;
    }

    nlohmann::json GetPlayerQuests(Player* player, std::shared_ptr<GameStateRequestControl> const& control)
    {
        if (!player || !ConsumeGameStateRequestWork(control))
            return nlohmann::json::object();
        nlohmann::json active = nlohmann::json::array();
        nlohmann::json completed = nlohmann::json::array();
        for (auto const& [questId, status] : player->getQuestStatusMap())
        {
            if (!ConsumeGameStateRequestWork(control))
                return nlohmann::json::object();
            if (status.Status == QUEST_STATUS_NONE || status.Status == QUEST_STATUS_REWARDED)
                continue;
            nlohmann::json data = GetQuestData(questId, status.Status);
            data["ready_to_turn_in"] = status.Status == QUEST_STATUS_COMPLETE;
            data["explored"] = status.Explored;
            data["timer"] = status.Timer;
            nlohmann::json items = nlohmann::json::array();
            nlohmann::json creatures = nlohmann::json::array();
            if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
            {
                for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                {
                    if (!ConsumeGameStateRequestWork(control))
                        return nlohmann::json::object();
                    if (quest->RequiredItemId[i])
                        items.push_back({{"item_id", quest->RequiredItemId[i]},
                            {"required_count", quest->RequiredItemCount[i]}, {"current_count", status.ItemCount[i]}});
                }
                for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                {
                    if (!ConsumeGameStateRequestWork(control))
                        return nlohmann::json::object();
                    if (quest->RequiredNpcOrGo[i])
                        creatures.push_back({{"npc_or_go_id", quest->RequiredNpcOrGo[i]},
                            {"required_count", quest->RequiredNpcOrGoCount[i]}, {"current_count", status.CreatureOrGOCount[i]}});
                }
            }
            data["item_objectives"] = std::move(items);
            data["creature_objectives"] = std::move(creatures);
            active.push_back(std::move(data));
        }
        // RewardQuest removes the active record; durable history lives in the rewarded set.
        for (uint32 questId : player->getRewardedQuests())
        {
            if (!ConsumeGameStateRequestWork(control))
                return nlohmann::json::object();
            completed.push_back(GetQuestData(questId, QUEST_STATUS_REWARDED));
        }
        if (!IsGameStateRequestActive(control))
            return nlohmann::json::object();
        return {{"active_quests", active}, {"completed_quests", completed},
            {"active_count", active.size()}, {"completed_count", completed.size()},
            {"rewarded_count", player->GetRewardedQuestCount()}};
    }
}
