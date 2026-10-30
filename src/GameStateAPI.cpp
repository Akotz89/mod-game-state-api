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

#include <algorithm>
#include <cctype>
#include <ctime>
#include <deque>
#include <mutex>
#include <vector>

namespace
{
constexpr std::size_t MaxPendingRequests = 32;
constexpr std::size_t MaxRequestsPerWorldUpdate = 2;
constexpr std::size_t MaxBindHostBytes = 253;
constexpr std::size_t MaxOriginBytes = 256;
constexpr std::chrono::milliseconds RequestLifetime{750};
// Admission is bounded; an active native snapshot checks cancellation between
// records rather than returning a silently truncated collection.
constexpr std::chrono::milliseconds MaxWorldUpdateAdmissionTime{2};

struct GameStateRequest
{
    std::uint64_t Generation;
    GameStateRequestType Type;
    std::string Name;
    bool IncludeEquipment = false;
    std::shared_ptr<GameStateRequestControl> Control;
    std::promise<GameStateResult> Promise;
};

std::mutex RequestMutex;
std::mutex PublicationMutex;
std::deque<std::unique_ptr<GameStateRequest>> Requests;
std::vector<std::weak_ptr<GameStateRequestControl>> ActiveControls;
std::atomic<std::uint64_t> AcceptedGeneration{0};
// World-thread config callbacks; independent of listener generations.
std::uint64_t WorldConfigLoadCallbackCount = 0;

void UntrackRequest(std::shared_ptr<GameStateRequestControl> const& requestControl)
{
    std::lock_guard lock(RequestMutex);
    ActiveControls.erase(std::remove_if(ActiveControls.begin(), ActiveControls.end(),
        [&requestControl](std::weak_ptr<GameStateRequestControl> const& tracked) {
            std::shared_ptr<GameStateRequestControl> control = tracked.lock();
            return !control || control == requestControl;
        }), ActiveControls.end());
}

GameStateResult ErrorResult(std::string const& message, int status)
{
    return {{{"error", message}, {"status", status}}, status};
}

GameStateResult InterruptedResult(GameStateRequestControl const& control)
{
    return control.State.load() == GameStateRequestState::Expired
        ? ErrorResult("World-thread snapshot expired", 504)
        : ErrorResult("World-thread snapshot was cancelled", 504);
}

GameStateResult BuildResult(GameStateRequest const& request)
{
    try
    {
        if (!IsGameStateRequestActive(request.Control))
            return InterruptedResult(*request.Control);

        if (request.Type == GameStateRequestType::Health)
            return {{{"status", "ok"}, {"timestamp", std::time(nullptr)},
                {"uptime_seconds", GameTime::GetUptime().count()}}, 200};

        if (request.Type == GameStateRequestType::Server)
            return {GameStateUtilities::GetServerData(request.Control), 200};

        if (request.Type == GameStateRequestType::Players)
        {
            nlohmann::json players = GameStateUtilities::GetAllPlayersData(request.Control, request.IncludeEquipment);
            return {{{"count", players.size()}, {"players", std::move(players)}}, 200};
        }

        Player* player = GameStateUtilities::FindPlayerByName(request.Name, request.Control);
        if (!IsGameStateRequestActive(request.Control))
            return InterruptedResult(*request.Control);
        if (!player || !player->IsInWorld())
            return ErrorResult("Player not found or not online", 404);

        switch (request.Type)
        {
            case GameStateRequestType::Player:
                return {GameStateUtilities::GetPlayerData(player, request.Control, request.IncludeEquipment), 200};
            case GameStateRequestType::Stats:
                return {GameStateUtilities::GetPlayerStats(player, request.Control), 200};
            case GameStateRequestType::Equipment:
                return {GameStateUtilities::GetPlayerEquipment(player, request.Control), 200};
            case GameStateRequestType::Inventory:
            {
                nlohmann::json inventory = GameStateUtilities::GetPlayerInventory(player, request.Control);
                int status = inventory.contains("error") ? 503 : 200;
                return {std::move(inventory), status};
            }
            case GameStateRequestType::Skills:
                return {GameStateUtilities::GetPlayerSkillsFull(player, request.Control), 200};
            case GameStateRequestType::SkillsFull:
                return {GameStateUtilities::GetPlayerSkillsFull(player, request.Control), 200};
            case GameStateRequestType::Quests:
                return {GameStateUtilities::GetPlayerQuests(player, request.Control), 200};
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

bool IsDecimalPort(std::string_view value)
{
    if (value.empty() || value.size() > 5)
        return false;

    unsigned int port = 0;
    for (char character : value)
    {
        if (!std::isdigit(static_cast<unsigned char>(character)))
            return false;
        port = port * 10 + static_cast<unsigned int>(character - '0');
    }

    return port >= 1 && port <= 65535;
}

}

namespace GameStateSecurity
{
bool IsValidHost(std::string_view host)
{
    if (host.empty() || host.size() > MaxBindHostBytes)
        return false;
    // Binding resolves the address; reject syntax that cannot be a host first.
    return std::all_of(host.begin(), host.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '.' || character == '-' || character == ':';
    });
}

bool IsValidOrigin(std::string_view origin)
{
    if (origin == "*")
        return true;
    if (origin.empty() || origin.size() > MaxOriginBytes)
        return false;
    std::size_t schemeEnd = origin.find("://");
    if (schemeEnd == std::string_view::npos)
        return false;

    std::string_view scheme = origin.substr(0, schemeEnd);
    if (scheme != "http" && scheme != "https")
        return false;

    std::string_view authority = origin.substr(schemeEnd + 3);
    if (authority.empty() || authority.find_first_of("/@?#\r\n") != std::string_view::npos)
        return false;

    std::string_view host;
    std::string_view port;
    bool portSpecified = false;
    if (authority.front() == '[')
    {
        std::size_t closing = authority.find(']');
        if (closing == std::string_view::npos)
            return false;
        host = authority.substr(1, closing - 1);
        if (closing + 1 < authority.size())
        {
            if (authority[closing + 1] != ':')
                return false;
            portSpecified = true;
            port = authority.substr(closing + 2);
        }
    }
    else
    {
        std::size_t separator = authority.rfind(':');
        if (separator != std::string_view::npos)
        {
            portSpecified = true;
            host = authority.substr(0, separator);
            port = authority.substr(separator + 1);
        }
        else
        {
            host = authority;
        }
    }

    return IsValidHost(host) && (!portSpecified || IsDecimalPort(port));
}

std::string NormalizeOrigin(std::string origin)
{
    std::transform(origin.begin(), origin.end(), origin.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if ((origin.starts_with("http://") && origin.ends_with(":80")) ||
        (origin.starts_with("https://") && origin.ends_with(":443")))
        origin.erase(origin.rfind(':'));
    return origin;
}

bool IsValidAuthToken(std::string_view token)
{
    if (token.size() < 32 || token.size() > 256)
        return false;

    return std::none_of(token.begin(), token.end(), [](char character) {
        unsigned char value = static_cast<unsigned char>(character);
        return std::iscntrl(value) || std::isspace(value);
    });
}
}

QueuedGameStateRequest QueueGameStateRequest(std::uint64_t generation, GameStateRequestType type,
    std::string name, bool includeEquipment)
{
    auto request = std::make_unique<GameStateRequest>();
    request->Generation = generation;
    request->Type = type;
    request->Name = std::move(name);
    request->IncludeEquipment = includeEquipment;
    request->Control = std::make_shared<GameStateRequestControl>();
    request->Control->Deadline = std::chrono::steady_clock::now() + RequestLifetime;
    request->Control->Generation = generation;

    QueuedGameStateRequest queued{request->Promise.get_future(), request->Control};

    std::lock_guard lock(RequestMutex);
    if (generation == 0 || generation != AcceptedGeneration.load())
    {
        request->Control->State.store(GameStateRequestState::Completed);
        request->Promise.set_value(ErrorResult("Game State API is unavailable", 503));
        return queued;
    }

    if (Requests.size() >= MaxPendingRequests)
    {
        request->Control->State.store(GameStateRequestState::Completed);
        request->Promise.set_value(ErrorResult("Game State API request queue is full", 503));
        return queued;
    }

    Requests.push_back(std::move(request));
    ActiveControls.push_back(queued.Control);
    return queued;
}

void CancelGameStateRequest(std::shared_ptr<GameStateRequestControl> const& control)
{
    if (!control)
        return;

    GameStateRequestState state = control->State.load();
    while (state != GameStateRequestState::Completed && state != GameStateRequestState::Expired &&
           state != GameStateRequestState::Cancelled &&
           !control->State.compare_exchange_weak(state, GameStateRequestState::Cancelled))
    {
    }
}

bool IsGameStateRequestActive(std::shared_ptr<GameStateRequestControl> const& control)
{
    if (!control || control->State.load() != GameStateRequestState::Processing)
        return false;

    if (control->Generation == 0 || control->Generation != AcceptedGeneration.load())
    {
        GameStateRequestState expected = GameStateRequestState::Processing;
        control->State.compare_exchange_strong(expected, GameStateRequestState::Cancelled);
        return false;
    }

    if (std::chrono::steady_clock::now() >= control->Deadline)
    {
        GameStateRequestState expected = GameStateRequestState::Processing;
        control->State.compare_exchange_strong(expected, GameStateRequestState::Expired);
        return false;
    }

    return control->State.load() == GameStateRequestState::Processing;
}

bool ConsumeGameStateRequestWork(std::shared_ptr<GameStateRequestControl> const& control, std::size_t /*units*/)
{
    return IsGameStateRequestActive(control);
}

void ProcessGameStateRequests()
{
    auto const admissionStart = std::chrono::steady_clock::now();
    for (std::size_t processed = 0; processed < MaxRequestsPerWorldUpdate; ++processed)
    {
        if (std::chrono::steady_clock::now() - admissionStart >= MaxWorldUpdateAdmissionTime)
            return;

        std::unique_ptr<GameStateRequest> request;
        {
            std::lock_guard lock(RequestMutex);
            if (Requests.empty())
                return;

            request = std::move(Requests.front());
            Requests.pop_front();
        }

        auto now = std::chrono::steady_clock::now();
        GameStateRequestState expected = GameStateRequestState::Pending;
        if (now >= request->Control->Deadline)
        {
            request->Control->State.compare_exchange_strong(expected, GameStateRequestState::Expired);
            request->Promise.set_value(ErrorResult("World-thread snapshot expired", 504));
            UntrackRequest(request->Control);
            continue;
        }

        if (request->Generation != AcceptedGeneration.load())
        {
            request->Control->State.store(GameStateRequestState::Cancelled);
            request->Promise.set_value(ErrorResult("Game State API configuration changed", 503));
            UntrackRequest(request->Control);
            continue;
        }

        if (!request->Control->State.compare_exchange_strong(expected, GameStateRequestState::Processing))
        {
            request->Promise.set_value(ErrorResult("World-thread snapshot was cancelled", 504));
            UntrackRequest(request->Control);
            continue;
        }

        GameStateResult result = BuildResult(*request);
        now = std::chrono::steady_clock::now();
        if (request->Generation != AcceptedGeneration.load())
        {
            GameStateRequestState processing = GameStateRequestState::Processing;
            request->Control->State.compare_exchange_strong(processing, GameStateRequestState::Cancelled);
        }
        if (now >= request->Control->Deadline)
        {
            GameStateRequestState processing = GameStateRequestState::Processing;
            request->Control->State.compare_exchange_strong(processing, GameStateRequestState::Expired);
        }

        GameStateRequestState processing = GameStateRequestState::Processing;
        if (!request->Control->State.compare_exchange_strong(processing, GameStateRequestState::Completed))
            result = InterruptedResult(*request->Control);

        request->Promise.set_value(std::move(result));
        UntrackRequest(request->Control);
    }
}

std::unique_lock<std::mutex> LockGameStatePublication()
{
    return std::unique_lock<std::mutex>(PublicationMutex);
}

void SetGameStateRequestAcceptance(std::uint64_t generation)
{
    auto publication = LockGameStatePublication();
    AcceptedGeneration.store(generation);
}

bool IsGameStateGenerationAccepted(std::uint64_t generation)
{
    return generation != 0 && generation == AcceptedGeneration.load();
}

void CancelGameStateRequests(std::string const& reason)
{
    std::deque<std::unique_ptr<GameStateRequest>> requests;
    std::vector<std::shared_ptr<GameStateRequestControl>> controls;
    {
        std::lock_guard lock(RequestMutex);
        SetGameStateRequestAcceptance(0);
        requests.swap(Requests);
        for (std::weak_ptr<GameStateRequestControl> const& tracked : ActiveControls)
        {
            if (std::shared_ptr<GameStateRequestControl> control = tracked.lock())
                controls.push_back(std::move(control));
        }
        ActiveControls.clear();
    }

    for (std::shared_ptr<GameStateRequestControl> const& control : controls)
        CancelGameStateRequest(control);

    for (std::unique_ptr<GameStateRequest>& request : requests)
    {
        request->Promise.set_value(ErrorResult(reason, 503));
    }
}

std::uint64_t GetWorldConfigLoadCallbackCount()
{
    return WorldConfigLoadCallbackCount;
}

GameStateAPI::GameStateAPI() : WorldScript("GameStateAPI")
{
}

GameStateAPI::~GameStateAPI()
{
    _worldStarted = false;
    StopServer("Game State API object destruction");
}

GameStateConfig GameStateAPI::ReadConfiguration()
{
    GameStateConfig config;
    config.Enabled = sConfigMgr->GetOption<bool>("GameStateAPI.Enable", false);
    config.Host = sConfigMgr->GetOption<std::string>("GameStateAPI.Host", "127.0.0.1");
    config.Port = sConfigMgr->GetOption<int32>("GameStateAPI.Port", 8080);
    config.AllowedOrigin = GameStateSecurity::NormalizeOrigin(
        sConfigMgr->GetOption<std::string>("GameStateAPI.AllowedOrigin", "http://127.0.0.1"));
    config.AuthToken = sConfigMgr->GetOption<std::string>("GameStateAPI.AuthToken", "");
    return config;
}

bool GameStateAPI::ValidateConfiguration(GameStateConfig const& config)
{
    bool valid = true;
    if (!GameStateSecurity::IsValidHost(config.Host))
    {
        LOG_ERROR("module.gamestate_api", "Refusing Game State API configuration: Host must be a valid hostname or numeric IPv4/IPv6 address");
        valid = false;
    }
    if (config.Port < 1 || config.Port > 65535)
    {
        LOG_ERROR("module.gamestate_api", "Refusing Game State API configuration: Port must be between 1 and 65535");
        valid = false;
    }
    if (!GameStateSecurity::IsValidOrigin(config.AllowedOrigin))
    {
        LOG_ERROR("module.gamestate_api", "Refusing Game State API configuration: AllowedOrigin must be an HTTP(S) origin or wildcard");
        valid = false;
    }
    if (config.Enabled && !GameStateSecurity::IsValidAuthToken(config.AuthToken))
    {
        LOG_ERROR("module.gamestate_api", "Refusing Game State API configuration: AuthToken must contain 32-256 non-whitespace characters");
        valid = false;
    }
    return valid;
}

void GameStateAPI::EraseSecret(std::string& secret)
{
    volatile char* bytes = secret.empty() ? nullptr : &secret[0];
    for (std::size_t index = 0; index < secret.size(); ++index)
        bytes[index] = 0;
    secret.clear();
}

void GameStateAPI::OnAfterConfigLoad(bool reload)
{
    ++WorldConfigLoadCallbackCount;
    GameStateConfig next = ReadConfiguration();
    bool nextValid = ValidateConfiguration(next);
    if (!nextValid)
    {
        next.Enabled = false;
        EraseSecret(next.AuthToken);
    }
    else if (!next.Enabled)
    {
        // Disabled configuration never needs to retain a credential.
        EraseSecret(next.AuthToken);
    }

    LOG_INFO("module.gamestate_api", "Game State API configuration: enabled={}, validation={}, auth={}",
        next.Enabled ? "yes" : "no", nextValid ? "passed" : "failed",
        next.AuthToken.empty() ? "missing" : "configured");

    if (!reload || !_worldStarted)
    {
        EraseSecret(_config.AuthToken);
        _config = std::move(next);
        _configValid = nextValid;
        return;
    }

    if (_lifecycle == GameStateLifecycle::RestartRequired)
    {
        EraseSecret(next.AuthToken);
        LOG_FATAL("module.gamestate_api", "Game State API reload rejected: listener retirement previously failed; process restart is required");
        return;
    }

    if (next == _config && nextValid == _configValid)
    {
        if (_lifecycle == GameStateLifecycle::Failed && next.Enabled && nextValid)
            StartServer();
        else
            LOG_INFO("module.gamestate_api", "Game State API reload contains no effective configuration change");
        EraseSecret(next.AuthToken);
        return;
    }

    // Stop and invalidate the old generation before publishing any new token,
    // origin, bind address, port, or enabled state.
    GameStateStopResult stopResult = StopServer("Game State API configuration reload");
    if (stopResult == GameStateStopResult::RestartRequired)
    {
        EraseSecret(next.AuthToken);
        LOG_FATAL("module.gamestate_api", "Game State API reload rejected: prior listener drain is incomplete; process restart is required");
        return;
    }

    EraseSecret(_config.AuthToken);
    _config = std::move(next);
    _configValid = nextValid;

    if (_config.Enabled && _configValid)
        StartServer();
    else
        LOG_INFO("module.gamestate_api", "Game State API remains stopped after reload");
}

bool GameStateAPI::StartServer()
{
    if (!_worldStarted || !_config.Enabled || !_configValid || _httpServer ||
        _lifecycle == GameStateLifecycle::RestartRequired)
        return false;

    _lifecycle = GameStateLifecycle::Starting;
    std::uint64_t generation = ++_nextGeneration;
    _httpServer = std::make_unique<HttpGameStateServer>(_config.Host, static_cast<uint16>(_config.Port),
        _config.AllowedOrigin, _config.AuthToken, generation);

    if (!_httpServer->Start())
    {
        CancelGameStateRequests("Game State API failed to start");
        if (_httpServer->RequiresRetention())
        {
            _lifecycle = GameStateLifecycle::RestartRequired;
            LOG_FATAL("module.gamestate_api", "Retaining owned listener after startup cleanup drain timeout; process restart is required");
        }
        else
        {
            _httpServer.reset();
            _lifecycle = GameStateLifecycle::Failed;
            LOG_ERROR("module.gamestate_api", "Game State API listener failed closed during startup");
        }
        return false;
    }

    SetGameStateRequestAcceptance(generation);
    _lifecycle = GameStateLifecycle::Running;
    LOG_INFO("module.gamestate_api", "Game State API started on configured listener generation {}", generation);
    return true;
}

GameStateStopResult GameStateAPI::StopServer(std::string const& reason)
{
    SetGameStateRequestAcceptance(0);
    CancelGameStateRequests(reason);

    if (!_httpServer)
    {
        if (_lifecycle != GameStateLifecycle::Failed && _lifecycle != GameStateLifecycle::RestartRequired)
            _lifecycle = GameStateLifecycle::Stopped;
        return GameStateStopResult::AlreadyStopped;
    }

    _lifecycle = GameStateLifecycle::Stopping;
    HttpGameStateStopResult stopResult = _httpServer->Stop();
    if (stopResult == HttpGameStateStopResult::DrainTimedOut)
    {
        LOG_FATAL("module.gamestate_api", "Game State API accept socket is closed but cpp-httplib worker drain is incomplete; retaining owned listener and requiring process restart");
        _lifecycle = GameStateLifecycle::RestartRequired;
        return GameStateStopResult::RestartRequired;
    }

    _httpServer.reset();
    _lifecycle = GameStateLifecycle::Stopped;
    return stopResult == HttpGameStateStopResult::AlreadyStopped
        ? GameStateStopResult::AlreadyStopped
        : GameStateStopResult::Stopped;
}

void GameStateAPI::OnStartup()
{
    _worldStarted = true;
    if (!_config.Enabled)
    {
        LOG_INFO("module.gamestate_api", "Game State API Module is disabled");
        return;
    }

    if (!_configValid)
    {
        LOG_ERROR("module.gamestate_api", "Game State API startup refused because configuration validation failed");
        return;
    }

    StartServer();
}

void GameStateAPI::OnShutdown()
{
    _worldStarted = false;
    StopServer("Game State API is shutting down");
}

void GameStateAPI::OnUpdate(uint32 /*diff*/)
{
    if (_lifecycle != GameStateLifecycle::Running)
        return;

    if (!_httpServer || !_httpServer->IsRunning())
    {
        LOG_ERROR("module.gamestate_api", "Game State API listener exited unexpectedly; disabling request acceptance");
        if (StopServer("Game State API listener exited unexpectedly") != GameStateStopResult::RestartRequired)
            _lifecycle = GameStateLifecycle::Failed;
        return;
    }

    ProcessGameStateRequests();
}

void AddGameStateAPIScripts()
{
    new GameStateAPI();
}
