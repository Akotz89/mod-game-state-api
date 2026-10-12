/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef GAMESTATEAPI_GAMESTATESUTILITIES_H
#define GAMESTATEAPI_GAMESTATESUTILITIES_H

#include <nlohmann/json.hpp>
#include <memory>
#include <string>

class Player;
struct GameStateRequestControl;

namespace GameStateUtilities
{
    nlohmann::json GetPlayerEquipment(Player* player, std::shared_ptr<GameStateRequestControl> const& control);
    nlohmann::json GetPlayerStats(Player* player, std::shared_ptr<GameStateRequestControl> const& control);
    nlohmann::json GetPlayerData(Player* player, std::shared_ptr<GameStateRequestControl> const& control, bool includeEquipment = false);
    nlohmann::json GetServerData(std::shared_ptr<GameStateRequestControl> const& control);
    nlohmann::json GetAllPlayersData(std::shared_ptr<GameStateRequestControl> const& control, bool includeEquipment = false);
    Player* FindPlayerByName(std::string const& name, std::shared_ptr<GameStateRequestControl> const& control);
    nlohmann::json GetPlayerSkills(Player* player, std::shared_ptr<GameStateRequestControl> const& control);
    nlohmann::json GetPlayerSkillsFull(Player* player, std::shared_ptr<GameStateRequestControl> const& control);
    nlohmann::json GetPlayerQuests(Player* player, std::shared_ptr<GameStateRequestControl> const& control);
}

#endif // GAMESTATEAPI_GAMESTATESUTILITIES_H
