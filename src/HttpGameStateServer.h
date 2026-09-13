/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef HTTP_GAME_STATE_SERVER_H
#define HTTP_GAME_STATE_SERVER_H

#include "Define.h"
#include <yhirose/httplib.h>
#include <string>
#include <memory>
#include <thread>
#include <atomic>

enum class GameStateRequestType;

// Modern HTTP server using httplib.h
class HttpGameStateServer
{
public:
    HttpGameStateServer(std::string const& host, uint16 port, std::string const& allowedOrigin, std::string const& authToken);
    ~HttpGameStateServer();

    bool Start();
    void Stop();
    bool IsRunning() const { return _running.load(); }

private:
    // REST API endpoint handlers
    void HandlePlayerInfo(httplib::Request const& req, httplib::Response& res);
    void HandlePlayerStats(httplib::Request const& req, httplib::Response& res);
    void HandlePlayerEquipment(httplib::Request const& req, httplib::Response& res);
    void HandlePlayerSkills(httplib::Request const& req, httplib::Response& res);
    void HandlePlayerSkillsFull(httplib::Request const& req, httplib::Response& res);
    void HandlePlayerQuests(httplib::Request const& req, httplib::Response& res);
    void HandleServerInfo(httplib::Request const& req, httplib::Response& res);
    void HandleOnlinePlayers(httplib::Request const& req, httplib::Response& res);
    void HandleHealthCheck(httplib::Request const& req, httplib::Response& res);

    // Utility methods
    void SetCorsHeaders(httplib::Response& res);
    bool IsAuthorized(httplib::Request const& req) const;
    void SendGameStateResponse(httplib::Response& res, GameStateRequestType type, std::string name = {},
        bool includeEquipment = false);
    void SendJsonResponse(httplib::Response& res, std::string const& json, int status = 200);
    void SendErrorResponse(httplib::Response& res, std::string const& message, int status = 400);

    std::string _host;
    uint16 _port;
    std::string _allowedOrigin;
    std::string _authToken;

    std::unique_ptr<httplib::Server> _server;
    std::unique_ptr<std::thread> _serverThread;
    std::atomic<bool> _running;
};

#endif // HTTP_GAME_STATE_SERVER_H
