/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "HttpGameStateServer.h"
#include "GameStateAPI.h"
#include "AccountMgr.h"
#include "Log.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <exception>
#include <map>

using json = nlohmann::json;

namespace
{
constexpr std::size_t HttpWorkerCount = 4;
constexpr std::size_t HttpWorkerQueue = 8;
constexpr std::size_t MaxRequestBodyBytes = 4 * 1024;
constexpr std::size_t MaxPlayerNameBytes = 48;
constexpr std::chrono::milliseconds SnapshotWait{1000};
constexpr std::chrono::milliseconds ListenerStartupWait{2000};
// Server::stop closes the accept socket, but the vendored cpp-httplib
// ThreadPool::shutdown drains queued jobs and joins every worker. This is only
// the interval for observing completion, not a claim of bounded shutdown.
constexpr std::chrono::milliseconds ListenerDrainObservation{2000};

bool ConstantTimeEquals(std::string const& provided, std::string const& expected)
{
    std::size_t length = std::max(provided.size(), expected.size());
    std::size_t difference = provided.size() ^ expected.size();
    for (std::size_t index = 0; index < length; ++index)
    {
        unsigned char left = index < provided.size() ? static_cast<unsigned char>(provided[index]) : 0;
        unsigned char right = index < expected.size() ? static_cast<unsigned char>(expected[index]) : 0;
        difference |= static_cast<std::size_t>(left ^ right);
    }
    return difference == 0;
}
}

HttpGameStateServer::HttpGameStateServer(std::string const& host, uint16 port, std::string const& allowedOrigin,
    std::string const& authToken, std::uint64_t generation)
    : _host(host), _port(port), _allowedOrigin(GameStateSecurity::NormalizeOrigin(allowedOrigin)), _authToken(authToken), _generation(generation)
{
    _server = std::make_unique<httplib::Server>();
    _server->new_task_queue = [] { return new httplib::ThreadPool(HttpWorkerCount, HttpWorkerQueue); };
    _server->set_payload_max_length(MaxRequestBodyBytes);
    _server->set_read_timeout(std::chrono::seconds(2));
    _server->set_write_timeout(std::chrono::seconds(2));
    _server->set_keep_alive_timeout(2);
    _server->set_keep_alive_max_count(4);

    _server->set_pre_routing_handler([this](httplib::Request const& req, httplib::Response& res) {
        if (!_accepting.load() || !IsGameStateGenerationAccepted(_generation))
        {
            SendErrorResponse(res, "Game State API is unavailable", 503);
            return httplib::Server::HandlerResponse::Handled;
        }

        SetCorsHeaders(res);
        if (!IsAllowedOrigin(req))
        {
            SendErrorResponse(res, "Origin not allowed", 403);
            return httplib::Server::HandlerResponse::Handled;
        }
        if (req.method != "GET" && req.method != "OPTIONS")
        {
            res.set_header("Allow", "GET, OPTIONS");
            SendErrorResponse(res, "Method not allowed", 405);
            return httplib::Server::HandlerResponse::Handled;
        }

        if (req.method != "OPTIONS" && !IsAuthorized(req))
        {
            res.set_header("WWW-Authenticate", "Bearer");
            SendErrorResponse(res, "Unauthorized", 401);
            return httplib::Server::HandlerResponse::Handled;
        }

        return httplib::Server::HandlerResponse::Unhandled;
    });

    _server->set_error_handler([this](httplib::Request const& /*req*/, httplib::Response& res) {
        if (res.body.empty())
            SendErrorResponse(res, "HTTP request failed", res.status);
    });

    _server->Options(".*", [this](httplib::Request const& /*req*/, httplib::Response& res) {
        SetCorsHeaders(res);
        res.status = 204;
    });

    _server->Get("/api/health", [this](httplib::Request const& req, httplib::Response& res) {
        HandleHealthCheck(req, res);
    });
    _server->Get("/api/server", [this](httplib::Request const& req, httplib::Response& res) {
        HandleServerInfo(req, res);
    });
    _server->Get("/api/players", [this](httplib::Request const& req, httplib::Response& res) {
        HandleOnlinePlayers(req, res);
    });
    _server->Get("/api/player/([^/]+)", [this](httplib::Request const& req, httplib::Response& res) {
        HandlePlayerInfo(req, res);
    });
    _server->Get("/api/player/([^/]+)/stats", [this](httplib::Request const& req, httplib::Response& res) {
        HandlePlayerStats(req, res);
    });
    _server->Get("/api/player/([^/]+)/equipment", [this](httplib::Request const& req, httplib::Response& res) {
        HandlePlayerEquipment(req, res);
    });
    _server->Get("/api/player/([^/]+)/inventory", [this](httplib::Request const& req, httplib::Response& res) {
        HandlePlayerInventory(req, res);
    });
    _server->Get("/api/player/([^/]+)/skills", [this](httplib::Request const& req, httplib::Response& res) {
        HandlePlayerSkills(req, res);
    });
    _server->Get("/api/player/([^/]+)/skills-full", [this](httplib::Request const& req, httplib::Response& res) {
        HandlePlayerSkillsFull(req, res);
    });
    _server->Get("/api/player/([^/]+)/quests", [this](httplib::Request const& req, httplib::Response& res) {
        HandlePlayerQuests(req, res);
    });
}

HttpGameStateServer::~HttpGameStateServer()
{
    if (Stop() == HttpGameStateStopResult::DrainTimedOut)
    {
        LOG_FATAL("module.gamestate_api", "Destroying a Game State API listener that did not stop would be unsafe");
        std::terminate();
    }
    EraseSecret(_authToken);
    EraseSecret(_allowedOrigin);
    EraseSecret(_host);
}

bool HttpGameStateServer::Start()
{
    if (_serverThread)
    {
        LOG_ERROR("module.gamestate_api", "Refusing to start the same HTTP server object twice");
        return false;
    }
    if (!GameStateSecurity::IsValidHost(_host) || !GameStateSecurity::IsValidOrigin(_allowedOrigin) ||
        !GameStateSecurity::IsValidAuthToken(_authToken))
    {
        LOG_ERROR("module.gamestate_api", "Refusing HTTP server startup because a security invariant is invalid");
        return false;
    }
    if (!_server->bind_to_port(_host, _port))
    {
        LOG_ERROR("module.gamestate_api", "Failed to bind HTTP server on {}:{}", _host, _port);
        return false;
    }

    {
        std::lock_guard lock(_listenerMutex);
        _listenerExited = false;
    }
    _serverThread = std::make_unique<std::thread>([this]() {
        bool listened = _server->listen_after_bind();
        if (!listened && _accepting.load())
            LOG_ERROR("module.gamestate_api", "HTTP server failed while listening on {}:{}", _host, _port);

        _accepting.store(false);
        _running.store(false);
        {
            std::lock_guard lock(_listenerMutex);
            _listenerExited = true;
        }
        _listenerStopped.notify_all();
    });

    auto const deadline = std::chrono::steady_clock::now() + ListenerStartupWait;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (_server->is_running())
        {
            _running.store(true);
            _accepting.store(true);
            return true;
        }
        {
            std::lock_guard lock(_listenerMutex);
            if (_listenerExited)
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LOG_ERROR("module.gamestate_api", "HTTP server did not enter its listen loop within the startup observation interval");
    _server->stop();
    if (Stop() == HttpGameStateStopResult::DrainTimedOut)
        LOG_FATAL("module.gamestate_api", "HTTP server worker drain remained incomplete after startup failure");
    return false;
}

HttpGameStateStopResult HttpGameStateServer::Stop()
{
    _accepting.store(false);
    _running.store(false);

    if (!_serverThread)
        return HttpGameStateStopResult::AlreadyStopped;

    if (_server)
        _server->stop();

    std::unique_lock lock(_listenerMutex);
    if (!_listenerStopped.wait_for(lock, ListenerDrainObservation, [this] { return _listenerExited; }))
    {
        _drainTimedOut.store(true);
        LOG_FATAL("module.gamestate_api", "HTTP accept socket was closed, but worker drain did not finish within the {} ms observation interval", ListenerDrainObservation.count());
        return HttpGameStateStopResult::DrainTimedOut;
    }
    lock.unlock();

    if (_serverThread->joinable())
        _serverThread->join();
    _serverThread.reset();
    _drainTimedOut.store(false);
    return HttpGameStateStopResult::Stopped;
}

void HttpGameStateServer::HandleHealthCheck(httplib::Request const& /*req*/, httplib::Response& res)
{
    SendGameStateResponse(res, GameStateRequestType::Health);
}

void HttpGameStateServer::HandleServerInfo(httplib::Request const& /*req*/, httplib::Response& res)
{
    SendGameStateResponse(res, GameStateRequestType::Server);
}

void HttpGameStateServer::HandleOnlinePlayers(httplib::Request const& req, httplib::Response& res)
{
    SendGameStateResponse(res, GameStateRequestType::Players, {}, req.get_param_value("equipment") == "true");
}

void HttpGameStateServer::HandlePlayerInfo(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];
    if (!IsValidPlayerName(playerName))
    {
        SendErrorResponse(res, "Invalid player name", 400);
        return;
    }
    SendGameStateResponse(res, GameStateRequestType::Player, std::move(playerName),
        req.get_param_value("include").find("equipment") != std::string::npos);
}

void HttpGameStateServer::HandlePlayerStats(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];
    if (!IsValidPlayerName(playerName))
    {
        SendErrorResponse(res, "Invalid player name", 400);
        return;
    }
    SendGameStateResponse(res, GameStateRequestType::Stats, std::move(playerName));
}

void HttpGameStateServer::HandlePlayerEquipment(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];
    if (!IsValidPlayerName(playerName))
    {
        SendErrorResponse(res, "Invalid player name", 400);
        return;
    }
    SendGameStateResponse(res, GameStateRequestType::Equipment, std::move(playerName));
}

void HttpGameStateServer::HandlePlayerInventory(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];
    if (!IsValidPlayerName(playerName))
    {
        SendErrorResponse(res, "Invalid player name", 400);
        return;
    }
    SendGameStateResponse(res, GameStateRequestType::Inventory, std::move(playerName));
}

void HttpGameStateServer::HandlePlayerSkills(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];
    if (!IsValidPlayerName(playerName))
    {
        SendErrorResponse(res, "Invalid player name", 400);
        return;
    }
    SendGameStateResponse(res, GameStateRequestType::Skills, std::move(playerName));
}

void HttpGameStateServer::HandlePlayerSkillsFull(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];
    if (!IsValidPlayerName(playerName))
    {
        SendErrorResponse(res, "Invalid player name", 400);
        return;
    }
    SendGameStateResponse(res, GameStateRequestType::SkillsFull, std::move(playerName));
}

void HttpGameStateServer::HandlePlayerQuests(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];
    if (!IsValidPlayerName(playerName))
    {
        SendErrorResponse(res, "Invalid player name", 400);
        return;
    }
    SendGameStateResponse(res, GameStateRequestType::Quests, std::move(playerName));
}

void HttpGameStateServer::SetCorsHeaders(httplib::Response& res) const
{
    res.set_header("Access-Control-Allow-Origin", _allowedOrigin);
    res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
    res.set_header("Access-Control-Max-Age", "600");
    res.set_header("Vary", "Origin");
}

bool HttpGameStateServer::IsAuthorized(httplib::Request const& req) const
{
    std::string provided = req.get_header_value("Authorization");
    if (provided.size() > 263)
        return false;
    return ConstantTimeEquals(provided, "Bearer " + _authToken);
}

bool HttpGameStateServer::IsAllowedOrigin(httplib::Request const& req) const
{
    std::string provided = req.get_header_value("Origin");
    if (provided.empty())
        return true;
    if (provided.size() > 256)
        return false;
    return _allowedOrigin == "*" || ConstantTimeEquals(GameStateSecurity::NormalizeOrigin(provided), _allowedOrigin);
}

bool HttpGameStateServer::IsValidPlayerName(std::string const& name) const
{
    if (name.empty() || name.size() > MaxPlayerNameBytes)
        return false;
    return std::none_of(name.begin(), name.end(), [](char character) {
        unsigned char value = static_cast<unsigned char>(character);
        return std::iscntrl(value) || character == '/' || character == '\\';
    });
}

void HttpGameStateServer::SendGameStateResponse(httplib::Response& res, GameStateRequestType type,
    std::string name, bool includeEquipment)
{
    QueuedGameStateRequest request = QueueGameStateRequest(_generation, type, std::move(name), includeEquipment);
    if (request.Future.wait_for(SnapshotWait) != std::future_status::ready)
    {
        CancelGameStateRequest(request.Control);
        SendErrorResponse(res, "World-thread snapshot timed out and was cancelled", 504);
        return;
    }

    GameStateResult result = request.Future.get();
    if (!CanTransmitSnapshot())
    {
        SendErrorResponse(res, "Game State API configuration changed", 503);
        return;
    }

    std::string serialized;
    try
    {
        // A session without cached RBAC metadata needs a database lookup. Keep
        // that blocking I/O on the HTTP worker, after native state is copied.
        std::map<uint32, json> accountNames;
        auto resolveAccount = [this, &accountNames](json& player) {
            if (!player.contains("account_id") || !CanTransmitSnapshot())
                return;
            uint32 accountId = player["account_id"].get<uint32>();
            if (!player["account_name"].is_null())
                accountNames[accountId] = player["account_name"];
            else
            {
                auto entry = accountNames.find(accountId);
                if (entry == accountNames.end())
                {
                    std::string accountName;
                    json name = AccountMgr::GetName(accountId, accountName) ? json(accountName) : json(nullptr);
                    entry = accountNames.emplace(accountId, std::move(name)).first;
                }
                player["account_name"] = entry->second;
            }
        };
        if (type == GameStateRequestType::Players && result.Status == 200)
            for (json& player : result.Payload["players"])
            {
                if (!CanTransmitSnapshot())
                    break;
                resolveAccount(player);
            }
        else if (type == GameStateRequestType::Player && result.Status == 200)
            resolveAccount(result.Payload);
        serialized = result.Payload.dump();
    }
    catch (std::exception const& exception)
    {
        LOG_ERROR("module.gamestate_api", "Snapshot serialization failed: {}", exception.what());
        SendErrorResponse(res, "Internal server error", 500);
        return;
    }

    // Retirement and response commitment share a lock. A response committed
    // before retirement can finish transmission on its already accepted socket.
    auto publication = LockGameStatePublication();
    if (!CanTransmitSnapshot())
    {
        SendErrorResponse(res, "Game State API configuration changed", 503);
        return;
    }
    SendJsonResponse(res, serialized, result.Status);
}

bool HttpGameStateServer::CanTransmitSnapshot() const
{
    return _accepting.load() && _running.load() && IsGameStateGenerationAccepted(_generation);
}

void HttpGameStateServer::EraseSecret(std::string& secret)
{
    volatile char* bytes = secret.empty() ? nullptr : &secret[0];
    for (std::size_t index = 0; index < secret.size(); ++index)
        bytes[index] = 0;
    secret.clear();
}

void HttpGameStateServer::SendJsonResponse(httplib::Response& res, std::string const& json, int status) const
{
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_content(json, "application/json");
}

void HttpGameStateServer::SendErrorResponse(httplib::Response& res, std::string const& message, int status) const
{
    json error = {{"error", message}, {"status", status}};
    SendJsonResponse(res, error.dump(), status);
}
