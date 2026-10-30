#!/usr/bin/env python3
"""Static architecture checks; native tests establish payload and HTTP behavior."""

from __future__ import annotations

import re
from pathlib import Path


FILES = {
    "api_cpp": "src/GameStateAPI.cpp",
    "api_h": "src/GameStateAPI.h",
    "http_cpp": "src/HttpGameStateServer.cpp",
    "http_h": "src/HttpGameStateServer.h",
    "util_cpp": "src/GameStateUtilities.cpp",
    "util_h": "src/GameStateUtilities.h",
    "config": "conf/gamestate_api.conf.dist",
    "httplib": "include/yhirose/httplib.h",
}


def _balanced_block(text: str, opening: int) -> str:
    depth = 0
    index = opening
    state = "code"
    quote = ""
    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""
        if state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and next_char == "/":
                state = "code"
                index += 1
        elif state == "string":
            if char == "\\":
                index += 1
            elif char == quote:
                state = "code"
        else:
            if char == "/" and next_char == "/":
                state = "line_comment"
                index += 1
            elif char == "/" and next_char == "*":
                state = "block_comment"
                index += 1
            elif char in {'"', "'"}:
                state = "string"
                quote = char
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return text[opening : index + 1]
        index += 1
    raise ValueError("unbalanced C++ block")


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise ValueError(f"missing function signature: {signature}")
    opening = text.find("{", start + len(signature))
    if opening < 0:
        raise ValueError(f"missing function body: {signature}")
    return _balanced_block(text, opening)


def analyze(root: Path) -> list[str]:
    source = {name: (root / relative).read_text(encoding="utf-8") for name, relative in FILES.items()}
    errors: list[str] = []

    def require(condition: bool, code: str) -> None:
        if not condition:
            errors.append(code)

    api = source["api_cpp"]
    api_h = source["api_h"]
    http = source["http_cpp"]
    http_h = source["http_h"]
    util = source["util_cpp"]
    util_h = source["util_h"]
    combined = api + api_h + http + http_h + util + util_h

    # Configuration validation remains independent of the listener and permits
    # the configured host/origin; payload visibility is not a configuration policy.
    try:
        validation = function_body(api, "bool GameStateAPI::ValidateConfiguration")
        startup = function_body(http, "bool HttpGameStateServer::Start()")
        for validator in ("IsValidHost", "IsValidOrigin", "IsValidAuthToken"):
            require(validator in validation and validator in startup,
                f"config:missing-validation:{validator}")
        require("config.Port < 1 || config.Port > 65535" in validation, "config:invalid-port-accepted")
    except ValueError as error:
        errors.append(f"config:{error}")

    # The actual reload control flow must return on failed retirement before
    # publishing any new config or calling StartServer.
    try:
        reload = function_body(api, "void GameStateAPI::OnAfterConfigLoad(bool reload)")
        terminal_at = reload.index("_lifecycle == GameStateLifecycle::RestartRequired")
        stop_at = reload.index("GameStateStopResult stopResult = StopServer(")
        guard_at = reload.index("stopResult == GameStateStopResult::RestartRequired", stop_at)
        publish_at = reload.index("_config = std::move(next)", guard_at)
        start_at = reload.index("StartServer()", publish_at)
        guard_open = reload.find("{", guard_at)
        guard = _balanced_block(reload, guard_open)
        require("return;" in guard, "lifecycle:failed-stop-does-not-return")
        terminal_guard = _balanced_block(reload, reload.find("{", terminal_at))
        require("return;" in terminal_guard and terminal_at < stop_at,
            "lifecycle:restart-required-not-terminal")
        require(stop_at < guard_at < publish_at < start_at, "lifecycle:stop-publication-start-order")
        require("EraseSecret(next.AuthToken)" in guard, "secret:rejected-config-not-erased")
    except (ValueError, IndexError) as error:
        errors.append(f"lifecycle:reload-control-flow:{error}")

    require("GameStateStopResult StopServer" in api_h, "lifecycle:stop-result-not-precise")
    require("GameStateLifecycle::RestartRequired" in api, "lifecycle:no-restart-required-state")
    require("HttpGameStateStopResult::DrainTimedOut" in api, "lifecycle:drain-timeout-not-propagated")
    require(".release()" not in combined, "lifecycle:owned-server-released")
    require("std::unique_ptr<HttpGameStateServer> _httpServer" in api_h, "lifecycle:server-not-owned")

    # Vendored cpp-httplib closes accept separately, then drains queued jobs and
    # joins workers. Source must call the wait an observation, never a bound.
    require("task_queue->shutdown();" in source["httplib"], "httplib:shutdown-call-missing")
    require("pool_.shutdown_ && pool_.jobs_.empty()" in source["httplib"], "httplib:queue-drain-semantics-missing")
    require("t.join();" in source["httplib"], "httplib:worker-join-semantics-missing")
    require("ListenerDrainObservation" in http and "not a claim of bounded shutdown" in http,
        "lifecycle:drain-semantics-overstated")

    # Final generation/state checks must bracket serialization and be adjacent
    # to the final SendJsonResponse publication.
    try:
        send = function_body(http, "void HttpGameStateServer::SendGameStateResponse")
        serialize_at = send.index("result.Payload.dump()")
        publish_at = send.rindex("SendJsonResponse(res, serialized, result.Status)")
        checks = [match.start() for match in re.finditer(r"if\s*\(!CanTransmitSnapshot\(\)\)", send)]
        require(len(checks) >= 2, "generation:missing-final-transmit-check")
        require(any(position < serialize_at for position in checks), "generation:no-pre-serialization-check")
        require(any(serialize_at < position < publish_at for position in checks), "generation:no-pre-transmission-check")
        require(publish_at - max(position for position in checks if position < publish_at) < 260,
            "generation:final-check-not-adjacent")
    except (ValueError, IndexError) as error:
        errors.append(f"generation:send-control-flow:{error}")
    try:
        can_send = function_body(http, "bool HttpGameStateServer::CanTransmitSnapshot() const")
        for term in ("_accepting.load()", "_running.load()", "IsGameStateGenerationAccepted(_generation)"):
            require(term in can_send, f"generation:transmit-state-missing:{term}")
    except ValueError as error:
        errors.append(f"generation:{error}")

    # HTTP workers enqueue value requests; core lookup and snapshot construction
    # belong to the native world callback. Actual cancellation is tested natively.
    require("ProcessGameStateRequests()" in function_body(api, "void GameStateAPI::OnUpdate"),
        "world:dispatch-not-in-world-update")
    require("FindPlayerByName" in function_body(api, "GameStateResult BuildResult"),
        "world:no-native-player-lookup")
    require("player->IsInWorld()" in function_body(api, "GameStateResult BuildResult"),
        "world:player-not-validated")
    require(not re.search(r"\bPlayer\s*\*|ObjectAccessor::|GameStateUtilities::", http),
        "world:http-accesses-core-object")
    consume = function_body(api, "bool ConsumeGameStateRequestWork")
    require("IsGameStateRequestActive(control)" in consume, "cancel:traversal-not-active-checked")
    require("Requests.size() >= MaxPendingRequests" in api, "queue:missing-admission-limit")
    require("new httplib::ThreadPool(HttpWorkerCount, HttpWorkerQueue)" in http,
        "queue:missing-worker-admission-limit")

    # The read-only authenticated route surface does not restrict returned fields.
    require(http.count("_server->Get(") == 10, "surface:get-route-count")
    require('"/api/player/([^/]+)/inventory"' in http, "surface:inventory-route-missing")
    require(http.count("_server->Options(") == 1, "surface:options-route-count")
    for verb in ("Post", "Put", "Patch", "Delete"):
        require(f"_server->{verb}(" not in http, f"surface:{verb.lower()}-route")
    require('req.method != "GET" && req.method != "OPTIONS"' in http, "surface:method-gate")
    pre_route = function_body(http, "_server->set_pre_routing_handler")
    require("!IsAuthorized(req)" in pre_route and 'req.method != "OPTIONS"' in pre_route,
        "surface:authenticated-get-gate")
    require('req.get_header_value("Authorization")' in http and "ConstantTimeEquals" in http,
        "auth:bearer-check-missing")
    require('req.get_header_value("Origin")' in http and "IsAllowedOrigin(req)" in pre_route,
        "origin:request-check-missing")
    require('res.set_header("WWW-Authenticate", "Bearer")' in http, "auth:challenge-missing")
    require('Access-Control-Allow-Methods", "GET, OPTIONS"' in http, "origin:cors-methods")
    for handler, parameter in (("HandleOnlinePlayers", "equipment"), ("HandlePlayerInfo", "include")):
        body = function_body(http, f"void HttpGameStateServer::{handler}")
        require(f'req.get_param_value("{parameter}")' in body, f"options:{handler}-not-forwarded")
    require("bool includeEquipment" in api_h and "request.IncludeEquipment" in api,
        "options:world-request-loses-equipment-option")

    # The module observes native state; it does not execute gameplay mutations.
    for mutator in ("SaveToDB", "ModifyMoney", "SetLevel", "GiveXP", "AddQuest", "CompleteQuest",
                    "TeleportTo", "LogoutPlayer", "Execute(", "CommitTransaction", "DirectExecute"):
        require(mutator not in combined, f"mutator:forbidden:{mutator}")

    # Secrets remain owned in a terminal object until workers drain, then are
    # erased. Failed reloads erase the unpublished candidate and cannot replace.
    require("EraseSecret(_authToken)" in http and "EraseSecret(_config.AuthToken)" in api,
        "secret:explicit-erasure-missing")
    log_lines = "\n".join(line for line in (api + http).splitlines() if "LOG_" in line)
    for secret in ("_authToken", "config.AuthToken", "next.AuthToken"):
        require(secret not in log_lines, f"secret:logged:{secret}")

    require("CancelGameStateRequest(request.Control)" in http, "cancel:http-timeout-not-cancelled")
    require("compare_exchange_weak" in function_body(api, "void CancelGameStateRequest"),
        "cancel:not-idempotent")
    require("ActiveControls" in function_body(api, "void CancelGameStateRequests"),
        "cancel:processing-request-not-invalidated")

    return sorted(set(errors))
