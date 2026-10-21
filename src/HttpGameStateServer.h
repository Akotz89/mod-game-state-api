/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef HTTP_GAME_STATE_SERVER_H
#define HTTP_GAME_STATE_SERVER_H

#include "Define.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// cpp-httplib passes this exact constant to listen(2). Pin it here so the
// kernel accept backlog cannot silently grow with a vendored-header default.
#ifdef CPPHTTPLIB_LISTEN_BACKLOG
#if CPPHTTPLIB_LISTEN_BACKLOG != 8
#error "Game State API requires CPPHTTPLIB_LISTEN_BACKLOG=8"
#endif
#else
#define CPPHTTPLIB_LISTEN_BACKLOG 8
#endif
#include <yhirose/httplib.h>

enum class GameStateRequestType;

enum class HttpGameStateStopResult
{
    AlreadyStopped,
    Stopped,
    DrainTimedOut
};

// Authenticated read-only HTTP server. If worker drain exceeds the observation interval,
// the owner must retain this object in RestartRequired state. cpp-httplib drains
// queued jobs before ThreadPool::shutdown returns, so the interval is not a
// claim that shutdown itself is bounded.
class HttpGameStateServer
{
public:
    HttpGameStateServer(std::string const& host, uint16 port, std::string const& allowedOrigin,
        std::string const& authToken, std::uint64_t generation);
    ~HttpGameStateServer();

    bool Start();
    HttpGameStateStopResult Stop();
    bool IsRunning() const { return _running.load(); }
    bool RequiresRetention() const { return _drainTimedOut.load(); }

private:
    void HandlePlayerInfo(httplib::Request const& req, httplib::Response& res);
    void HandlePlayerStats(httplib::Request const& req, httplib::Response& res);
    void HandlePlayerInventory(httplib::Request const& req, httplib::Response& res);
    void HandlePlayerEquipment(httplib::Request const& req, httplib::Response& res);
    void HandlePlayerSkills(httplib::Request const& req, httplib::Response& res);
    void HandlePlayerSkillsFull(httplib::Request const& req, httplib::Response& res);
    void HandlePlayerQuests(httplib::Request const& req, httplib::Response& res);
    void HandleServerInfo(httplib::Request const& req, httplib::Response& res);
    void HandleOnlinePlayers(httplib::Request const& req, httplib::Response& res);
    void HandleHealthCheck(httplib::Request const& req, httplib::Response& res);

    void SetCorsHeaders(httplib::Response& res) const;
    bool IsAuthorized(httplib::Request const& req) const;
    bool IsAllowedOrigin(httplib::Request const& req) const;
    bool IsValidPlayerName(std::string const& name) const;
    void SendGameStateResponse(httplib::Response& res, GameStateRequestType type, std::string name = {}, bool includeEquipment = false);
    void SendJsonResponse(httplib::Response& res, std::string const& json, int status = 200) const;
    void SendErrorResponse(httplib::Response& res, std::string const& message, int status = 400) const;
    bool CanTransmitSnapshot() const;
    static void EraseSecret(std::string& secret);

    std::string _host;
    uint16 _port;
    std::string _allowedOrigin;
    std::string _authToken;
    std::uint64_t _generation;

    std::unique_ptr<httplib::Server> _server;
    std::unique_ptr<std::thread> _serverThread;
    std::atomic<bool> _running{false};
    std::atomic<bool> _accepting{false};
    std::atomic<bool> _drainTimedOut{false};
    std::mutex _listenerMutex;
    std::condition_variable _listenerStopped;
    bool _listenerExited = true;
};

#endif // HTTP_GAME_STATE_SERVER_H
