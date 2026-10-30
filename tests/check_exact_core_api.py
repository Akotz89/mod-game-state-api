#!/usr/bin/env python3
"""Verify exact-core and vendored-header API presence without compiling anything."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def git_value(core: Path, expression: str) -> str:
    return subprocess.run(
        ["git", "-C", str(core), "rev-parse", expression],
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    ).stdout.strip()


def require(path: Path, *needles: str) -> None:
    text = path.read_text(encoding="utf-8")
    missing = [needle for needle in needles if needle not in text]
    if missing:
        raise SystemExit(f"{path}: missing expected API text: {missing}")


def require_inventory_api(core: Path) -> None:
    require(core / "src/server/game/Entities/Player/Player.h",
        "PLAYER_SLOT_END", "BUYBACK_SLOT_START", "BUYBACK_SLOT_END",
        "INVENTORY_SLOT_BAG_START", "INVENTORY_SLOT_BAG_END",
        "BANK_SLOT_BAG_START", "BANK_SLOT_BAG_END", "GetBagByPos(uint8 slot) const")
    require(core / "src/server/game/Entities/Item/Item.h",
        "GetTemplate() const", "GetOwnerGUID() const", "GetCount() const",
        "GetSlot() const", "GetContainer()", "GetItemRandomPropertyId() const",
        "GetItemSuffixFactor() const")
    require(core / "src/server/game/Entities/Item/Container/Bag.h",
        "MAX_BAG_SIZE", "GetItemByPos(uint8 slot) const", "GetBagSize() const")
    require(core / "src/server/game/Entities/Item/ItemEnchantmentMgr.h",
        "GenerateEnchSuffixFactor(uint32")
    require(core / "src/server/game/Entities/Object/Object.h",
        "GetGUID() const", "GetEntry() const")
    require(core / "src/server/game/Entities/Object/ObjectGuid.h",
        "GetRawValue() const", "GetCounter()")
    require(core / "src/server/game/World/World.h", "getBoolConfig(ServerConfigs")
    require(core / "src/server/game/World/WorldConfig.h", "CONFIG_LEAVE_GROUP_ON_LOGOUT")

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("core", type=Path, help="core source used by the candidate build")
    args = parser.parse_args()
    core = args.core.resolve()
    module = Path(__file__).resolve().parents[1]

    revision = git_value(core, "HEAD")
    tree = git_value(core, "HEAD^{tree}")

    require(
        core / "src/server/game/Scripting/ScriptDefines/WorldScript.h",
        "OnAfterConfigLoad(bool",
        "OnUpdate(uint32",
        "OnStartup()",
        "OnShutdown()",
    )
    require(
        core / "src/server/game/Server/WorldSessionMgr.h",
        "GetQueuedSessionCount() const",
        "GetPlayerCount() const",
    )
    require(
        core / "src/server/game/Entities/Player/Player.h",
        "typedef std::unordered_map<uint32, PlayerSpell*> PlayerSpellMap",
        "typedef std::map<uint32, QuestStatusData> QuestStatusMap",
        "GetSpellMap() const",
        "GetTalentMap() const",
        "getRewardedQuests()",
        "GetRewardedQuestCount() const",
        "GetSkillValue(uint32",
        "GetSkillPermBonusValue(uint32",
        "GetSkillTempBonusValue(uint32",
        "getQuestStatusMap()",
        "GetItemByPos(uint8 bag, uint8 slot) const",
    )
    require(
        core / "src/server/game/Globals/ObjectAccessor.h",
        "FindPlayerByName(std::string const& name",
        "MapType const& GetPlayers()",
        "std::shared_mutex* GetLock()",
    )
    require_inventory_api(core)
    require(core / "src/server/game/Time/GameTime.h", "Seconds GetUptime()")
    require(core / "src/cmake/macros/ConfigureBaseTargets.cmake", "set(CMAKE_CXX_STANDARD 20)")
    require(
        module / "include/yhirose/httplib.h",
        "explicit ThreadPool(size_t n, size_t mqr = 0)",
        "Server &set_payload_max_length(size_t length)",
        "Server &set_read_timeout",
        "Server &set_write_timeout",
        "Server &set_keep_alive_max_count(size_t count)",
        "bool bind_to_port",
        "bool listen_after_bind()",
        "bool is_running() const",
        "void stop()",
    )
    print(f"PASS API presence in candidate core: {revision} committed tree {tree}; not a compile or dirty-source identity proof")


if __name__ == "__main__":
    main()
