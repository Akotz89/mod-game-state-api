/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "HttpGameStateServer.h"
#include "GameStateAPI.h"
#include "Log.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>

using json = nlohmann::json;

HttpGameStateServer::HttpGameStateServer(std::string const& host, uint16 port, std::string const& allowedOrigin,
    std::string const& authToken)
    : _host(host), _port(port), _allowedOrigin(allowedOrigin), _authToken(authToken), _running(false)
{
    _server = std::make_unique<httplib::Server>();

    // Set up CORS middleware for all requests
    _server->set_pre_routing_handler([this](httplib::Request const& req, httplib::Response& res) {
        SetCorsHeaders(res);
        if (req.method != "OPTIONS" && !IsAuthorized(req))
        {
            res.set_header("WWW-Authenticate", "Bearer");
            SendErrorResponse(res, "Unauthorized", 401);
            return httplib::Server::HandlerResponse::Handled;
        }

        return httplib::Server::HandlerResponse::Unhandled;
    });

    // Handle OPTIONS requests for CORS preflight
    _server->Options(".*", [this](httplib::Request const& /*req*/, httplib::Response& res) {
        SetCorsHeaders(res);
        res.status = 200;
    });

    // API endpoints
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
    Stop();
}

bool HttpGameStateServer::Start()
{
    if (_running.load())
    {
        LOG_WARN("module.gamestate_api", "HTTP server is already running");
        return false;
    }

    if (!_server->bind_to_port(_host, _port))
    {
        LOG_ERROR("module.gamestate_api", "Failed to bind HTTP server on {}:{}", _host, _port);
        return false;
    }

    _running.store(true);
    _serverThread = std::make_unique<std::thread>([this]() {
        LOG_INFO("module.gamestate_api", "Starting HTTP server on {}:{}", _host, _port);
        if (!_server->listen_after_bind())
        {
            if (_running.load())
                LOG_ERROR("module.gamestate_api", "HTTP server failed while listening on {}:{}", _host, _port);
        }

        _running.store(false);
        LOG_INFO("module.gamestate_api", "HTTP server stopped");
    });

    _server->wait_until_ready();
    if (!_server->is_running())
    {
        _running.store(false);
        if (_serverThread->joinable())
            _serverThread->join();
        LOG_ERROR("module.gamestate_api", "HTTP server failed to enter its listen loop");
        return false;
    }

    LOG_INFO("module.gamestate_api", "Game State API HTTP server started successfully on {}:{}", _host, _port);
    return true;
}

void HttpGameStateServer::Stop()
{
    bool wasRunning = _running.exchange(false);
    if (wasRunning)
        LOG_INFO("module.gamestate_api", "Stopping HTTP server...");

    if (_server)
        _server->stop();

    if (_serverThread && _serverThread->joinable())
        _serverThread->join();

    if (wasRunning)
        LOG_INFO("module.gamestate_api", "HTTP server stopped");
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
    bool includeEquipment = req.has_param("equipment") && req.get_param_value("equipment") == "true";
    SendGameStateResponse(res, GameStateRequestType::Players, {}, includeEquipment);
}

void HttpGameStateServer::HandlePlayerInfo(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];

    if (playerName.empty())
    {
        SendErrorResponse(res, "Player name is required", 400);
        return;
    }

    bool includeEquipment = req.has_param("include") &&
                           req.get_param_value("include").find("equipment") != std::string::npos;
    SendGameStateResponse(res, GameStateRequestType::Player, std::move(playerName), includeEquipment);
}

void HttpGameStateServer::HandlePlayerStats(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];

    if (playerName.empty())
    {
        SendErrorResponse(res, "Player name is required", 400);
        return;
    }

    SendGameStateResponse(res, GameStateRequestType::Stats, std::move(playerName));
}

void HttpGameStateServer::HandlePlayerEquipment(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];

    if (playerName.empty())
    {
        SendErrorResponse(res, "Player name is required", 400);
        return;
    }

    SendGameStateResponse(res, GameStateRequestType::Equipment, std::move(playerName));
}

void HttpGameStateServer::HandlePlayerSkills(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];

    if (playerName.empty())
    {
        SendErrorResponse(res, "Player name is required", 400);
        return;
    }

    SendGameStateResponse(res, GameStateRequestType::Skills, std::move(playerName));
}

void HttpGameStateServer::HandlePlayerSkillsFull(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];

    if (playerName.empty())
    {
        SendErrorResponse(res, "Player name is required", 400);
        return;
    }

    SendGameStateResponse(res, GameStateRequestType::SkillsFull, std::move(playerName));
}

void HttpGameStateServer::HandlePlayerQuests(httplib::Request const& req, httplib::Response& res)
{
    std::string playerName = req.matches[1];

    if (playerName.empty())
    {
        SendErrorResponse(res, "Player name is required", 400);
        return;
    }

    SendGameStateResponse(res, GameStateRequestType::Quests, std::move(playerName));
}

void HttpGameStateServer::SetCorsHeaders(httplib::Response& res)
{
    res.set_header("Access-Control-Allow-Origin", _allowedOrigin);
    res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
    res.set_header("Access-Control-Max-Age", "86400");
}

bool HttpGameStateServer::IsAuthorized(httplib::Request const& req) const
{
    return !_authToken.empty() && req.get_header_value("Authorization") == "Bearer " + _authToken;
}

void HttpGameStateServer::SendGameStateResponse(httplib::Response& res, GameStateRequestType type, std::string name,
    bool includeEquipment)
{
    std::future<GameStateResult> future = QueueGameStateRequest(type, std::move(name), includeEquipment);
    if (future.wait_for(std::chrono::seconds(2)) != std::future_status::ready)
    {
        SendErrorResponse(res, "World-thread snapshot timed out", 504);
        return;
    }

    GameStateResult result = future.get();
    SendJsonResponse(res, result.Payload.dump(2), result.Status);
}

void HttpGameStateServer::SendJsonResponse(httplib::Response& res, std::string const& json, int status)
{
    res.status = status;
    res.set_content(json, "application/json");
}

void HttpGameStateServer::SendErrorResponse(httplib::Response& res, std::string const& message, int status)
{
    json error = {
        {"error", message},
        {"timestamp", std::time(nullptr)}
    };
    SendJsonResponse(res, error.dump(), status);
}
