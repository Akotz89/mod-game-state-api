/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "GameStateAPI.h"
#include "GameStateUtilities.h"
#include "HttpGameStateServer.h"
#include "Config.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"

#include <deque>
#include <mutex>

namespace
{
constexpr std::size_t MaxPendingRequests = 64;
constexpr std::size_t MaxRequestsPerWorldUpdate = 8;

struct GameStateRequest
{
    GameStateRequestType Type;
    std::string Name;
    bool IncludeEquipment;
    std::promise<GameStateResult> Promise;
};

std::mutex RequestMutex;
std::deque<std::unique_ptr<GameStateRequest>> Requests;
bool AcceptingRequests = false;

GameStateResult ErrorResult(std::string const& message, int status)
{
    return {{{"error", message}, {"status", status}}, status};
}

GameStateResult BuildResult(GameStateRequest const& request)
{
    try
    {
        if (request.Type == GameStateRequestType::Health)
        {
            nlohmann::json serverData = GameStateUtilities::GetServerData();
            return {{{"status", "ok"},
                {"timestamp", GameTime::GetGameTime().count()},
                {"uptime_seconds", serverData["uptime_seconds"]}}, 200};
        }

        if (request.Type == GameStateRequestType::Server)
            return {GameStateUtilities::GetServerData(), 200};

        if (request.Type == GameStateRequestType::Players)
        {
            nlohmann::json players = GameStateUtilities::GetAllPlayersData(request.IncludeEquipment);
            return {{{"count", players.size()}, {"players", std::move(players)}}, 200};
        }

        Player* player = GameStateUtilities::FindPlayerByName(request.Name);
        if (!player || !player->IsInWorld())
            return ErrorResult("Player not found or not online", 404);

        switch (request.Type)
        {
            case GameStateRequestType::Player:
                return {GameStateUtilities::GetPlayerData(player, request.IncludeEquipment), 200};
            case GameStateRequestType::Stats:
                return {GameStateUtilities::GetPlayerStats(player), 200};
            case GameStateRequestType::Equipment:
                return {GameStateUtilities::GetPlayerEquipment(player), 200};
            case GameStateRequestType::Skills:
                return {GameStateUtilities::GetPlayerSkills(player), 200};
            case GameStateRequestType::SkillsFull:
                return {GameStateUtilities::GetPlayerSkillsFull(player), 200};
            case GameStateRequestType::Quests:
                return {GameStateUtilities::GetPlayerQuests(player), 200};
            default:
                return ErrorResult("Unsupported request", 400);
        }
    }
    catch (std::exception const& exception)
    {
        LOG_ERROR("module.gamestate_api", "World-thread snapshot request failed: {}", exception.what());
        return ErrorResult("Internal server error", 500);
    }
}
}

std::future<GameStateResult> QueueGameStateRequest(GameStateRequestType type, std::string name,
    bool includeEquipment)
{
    auto request = std::make_unique<GameStateRequest>();
    request->Type = type;
    request->Name = std::move(name);
    request->IncludeEquipment = includeEquipment;
    std::future<GameStateResult> future = request->Promise.get_future();

    std::lock_guard lock(RequestMutex);
    if (!AcceptingRequests)
    {
        request->Promise.set_value(ErrorResult("Game State API is unavailable", 503));
        return future;
    }

    if (Requests.size() >= MaxPendingRequests)
    {
        request->Promise.set_value(ErrorResult("Game State API request queue is full", 503));
        return future;
    }

    Requests.push_back(std::move(request));
    return future;
}

void ProcessGameStateRequests()
{
    for (std::size_t processed = 0; processed < MaxRequestsPerWorldUpdate; ++processed)
    {
        std::unique_ptr<GameStateRequest> request;
        {
            std::lock_guard lock(RequestMutex);
            if (Requests.empty())
                return;

            request = std::move(Requests.front());
            Requests.pop_front();
        }

        request->Promise.set_value(BuildResult(*request));
    }
}

void SetGameStateRequestAcceptance(bool accepting)
{
    std::lock_guard lock(RequestMutex);
    AcceptingRequests = accepting;
}

void CancelGameStateRequests()
{
    std::deque<std::unique_ptr<GameStateRequest>> requests;
    {
        std::lock_guard lock(RequestMutex);
        AcceptingRequests = false;
        requests.swap(Requests);
    }

    for (std::unique_ptr<GameStateRequest>& request : requests)
        request->Promise.set_value(ErrorResult("Game State API is shutting down", 503));
}

GameStateAPI::GameStateAPI() : WorldScript("GameStateAPI"), _enabled(false), _port(8080)
{
}

GameStateAPI::~GameStateAPI()
{
    CancelGameStateRequests();
    if (_httpServer)
        _httpServer->Stop();
}

void GameStateAPI::OnAfterConfigLoad(bool reload)
{
    _enabled = sConfigMgr->GetOption<bool>("GameStateAPI.Enable", false);
    _host = sConfigMgr->GetOption<std::string>("GameStateAPI.Host", "127.0.0.1");
    _port = static_cast<uint16>(sConfigMgr->GetOption<int32>("GameStateAPI.Port", 8080));
    _allowedOrigin = sConfigMgr->GetOption<std::string>("GameStateAPI.AllowedOrigin", "http://127.0.0.1");
    _authToken = sConfigMgr->GetOption<std::string>("GameStateAPI.AuthToken", "");

    LOG_INFO("module.gamestate_api", "Game State API Module Configuration:");
    LOG_INFO("module.gamestate_api", "  Enabled: {}", _enabled ? "Yes" : "No");
    if (_enabled)
    {
        LOG_INFO("module.gamestate_api", "  Host: {}", _host);
        LOG_INFO("module.gamestate_api", "  Port: {}", _port);
        LOG_INFO("module.gamestate_api", "  Allowed Origin: {}", _allowedOrigin);
        LOG_INFO("module.gamestate_api", "  Authentication: {}", _authToken.empty() ? "Missing" : "Configured");
        if (reload && _httpServer)
            LOG_WARN("module.gamestate_api", "Listener changes require a worldserver restart");
    }
}

void GameStateAPI::OnStartup()
{
    if (!_enabled)
    {
        LOG_INFO("module.gamestate_api", "Game State API Module is disabled");
        return;
    }

    if (_authToken.empty())
    {
        LOG_ERROR("module.gamestate_api", "Refusing to start without GameStateAPI.AuthToken");
        return;
    }

    LOG_INFO("module.gamestate_api", "Starting Game State API HTTP Server...");

    SetGameStateRequestAcceptance(true);
    _httpServer = std::make_unique<HttpGameStateServer>(_host, _port, _allowedOrigin, _authToken);

    if (_httpServer->Start())
    {
        LOG_INFO("module.gamestate_api", "Game State API HTTP Server started successfully on {}:{}", _host, _port);
    }
    else
    {
        LOG_ERROR("module.gamestate_api", "Failed to start Game State API HTTP Server");
        CancelGameStateRequests();
        _httpServer.reset();
    }
}

void GameStateAPI::OnShutdown()
{
    CancelGameStateRequests();
    if (_httpServer)
    {
        LOG_INFO("module.gamestate_api", "Stopping Game State API HTTP Server...");
        _httpServer->Stop();
        _httpServer.reset();
        LOG_INFO("module.gamestate_api", "Game State API HTTP Server stopped");
    }
}

void GameStateAPI::OnUpdate(uint32 /*diff*/)
{
    if (_httpServer)
        ProcessGameStateRequests();
}

// Register the script
void AddGameStateAPIScripts()
{
    new GameStateAPI();
}
