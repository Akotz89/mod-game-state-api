/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef GAME_STATE_API_H
#define GAME_STATE_API_H

#include "ScriptMgr.h"
#include "Config.h"
#include <future>
#include <nlohmann/json.hpp>
#include <string>
#include <memory>

class HttpGameStateServer;

enum class GameStateRequestType
{
    Health,
    Server,
    Players,
    Player,
    Stats,
    Equipment,
    Skills,
    SkillsFull,
    Quests
};

struct GameStateResult
{
    nlohmann::json Payload;
    int Status;
};

std::future<GameStateResult> QueueGameStateRequest(GameStateRequestType type, std::string name = {},
    bool includeEquipment = false);
void ProcessGameStateRequests();
void SetGameStateRequestAcceptance(bool accepting);
void CancelGameStateRequests();

// Game State API Module
class GameStateAPI : public WorldScript
{
public:
    GameStateAPI();
    ~GameStateAPI();

    void OnAfterConfigLoad(bool reload) override;
    void OnStartup() override;
    void OnShutdown() override;
    void OnUpdate(uint32 /*diff*/) override;

private:
    std::unique_ptr<HttpGameStateServer> _httpServer;
    bool _enabled;
    std::string _host;
    uint16 _port;
    std::string _allowedOrigin;
    std::string _authToken;
};

#endif // GAME_STATE_API_H
