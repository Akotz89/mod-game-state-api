/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef GAME_STATE_API_H
#define GAME_STATE_API_H

#include "Config.h"
#include "ScriptMgr.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

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

enum class GameStateRequestState
{
    Pending,
    Processing,
    Completed,
    Cancelled,
    Expired
};

struct GameStateResult
{
    nlohmann::json Payload;
    int Status;
};

struct GameStateRequestControl
{
    std::atomic<GameStateRequestState> State{GameStateRequestState::Pending};
    std::chrono::steady_clock::time_point Deadline;
    std::uint64_t Generation = 0;
};

struct QueuedGameStateRequest
{
    std::future<GameStateResult> Future;
    std::shared_ptr<GameStateRequestControl> Control;
};

namespace GameStateSecurity
{
    bool IsValidHost(std::string_view host);
    bool IsValidOrigin(std::string_view origin);
    bool IsValidAuthToken(std::string_view token);
    std::string NormalizeOrigin(std::string origin);
}

QueuedGameStateRequest QueueGameStateRequest(std::uint64_t generation, GameStateRequestType type,
    std::string name = {}, bool includeEquipment = false);
void CancelGameStateRequest(std::shared_ptr<GameStateRequestControl> const& control);
bool IsGameStateRequestActive(std::shared_ptr<GameStateRequestControl> const& control);
bool ConsumeGameStateRequestWork(std::shared_ptr<GameStateRequestControl> const& control,
    std::size_t units = 1);
void ProcessGameStateRequests();
std::unique_lock<std::mutex> LockGameStatePublication();
void SetGameStateRequestAcceptance(std::uint64_t generation);
bool IsGameStateGenerationAccepted(std::uint64_t generation);
void CancelGameStateRequests(std::string const& reason);

struct GameStateConfig
{
    bool Enabled = false;
    std::string Host = "127.0.0.1";
    int32 Port = 8080;
    std::string AllowedOrigin = "http://127.0.0.1";
    std::string AuthToken;

    bool operator==(GameStateConfig const& other) const = default;
};

enum class GameStateLifecycle
{
    Stopped,
    Starting,
    Running,
    Stopping,
    Failed,
    RestartRequired
};

enum class GameStateStopResult
{
    AlreadyStopped,
    Stopped,
    RestartRequired
};

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
    static GameStateConfig ReadConfiguration();
    static bool ValidateConfiguration(GameStateConfig const& config);
    static void EraseSecret(std::string& secret);

    bool StartServer();
    GameStateStopResult StopServer(std::string const& reason);

    std::unique_ptr<HttpGameStateServer> _httpServer;
    GameStateConfig _config;
    GameStateLifecycle _lifecycle = GameStateLifecycle::Stopped;
    std::uint64_t _nextGeneration = 0;
    bool _configValid = true;
    bool _worldStarted = false;
};

#endif // GAME_STATE_API_H
