#!/usr/bin/env python3
"""Mutation tests for static world-thread, authentication and lifecycle checks."""

from __future__ import annotations

import shutil
import tempfile
import unittest
from pathlib import Path

from source_contract import FILES, analyze


ROOT = Path(__file__).resolve().parents[1]


class SourceContractTests(unittest.TestCase):
    def scratch(self) -> Path:
        temporary = tempfile.TemporaryDirectory(prefix="source-mutation-", dir=ROOT / "tests")
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        for relative in FILES.values():
            source = ROOT / relative
            destination = root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
        return root

    def mutate(self, relative: str, old: str, new: str, *, last: bool = False) -> list[str]:
        root = self.scratch()
        path = root / relative
        text = path.read_text(encoding="utf-8")
        self.assertIn(old, text)
        if last:
            position = text.rfind(old)
            text = text[:position] + new + text[position + len(old) :]
        else:
            self.assertEqual(text.count(old), 1, f"mutation anchor must be unique: {old}")
            text = text.replace(old, new, 1)
        path.write_text(text, encoding="utf-8")
        failures = analyze(root)
        self.assertTrue(failures, "adversarial C++ mutation was accepted")
        return failures

    @staticmethod
    def assert_prefix(failures: list[str], prefix: str) -> None:
        if not any(failure.startswith(prefix) for failure in failures):
            raise AssertionError(f"expected {prefix!r} failure, got {failures}")

    def test_01_current_cpp_contract_passes(self) -> None:
        self.assertEqual(analyze(ROOT), [])

    def test_02_config_validation_cannot_be_bypassed(self) -> None:
        failures = self.mutate("src/HttpGameStateServer.cpp",
            "!GameStateSecurity::IsValidHost(_host)", "false")
        self.assert_prefix(failures, "config:")

    def test_03_failed_stop_must_block_publication_and_replacement(self) -> None:
        failures = self.mutate("src/GameStateAPI.cpp",
            'LOG_FATAL("module.gamestate_api", "Game State API reload rejected: prior listener drain is incomplete; process restart is required");\n        return;',
            'LOG_FATAL("module.gamestate_api", "Game State API reload rejected: prior listener drain is incomplete; process restart is required");')
        self.assertIn("lifecycle:failed-stop-does-not-return", failures)

    def test_04_listener_ownership_release_or_leak_is_rejected(self) -> None:
        failures = self.mutate("src/GameStateAPI.cpp",
            'GameStateStopResult stopResult = StopServer("Game State API configuration reload");',
            '(void)_httpServer.release();\n    GameStateStopResult stopResult = StopServer("Game State API configuration reload");')
        self.assertIn("lifecycle:owned-server-released", failures)

    def test_05_final_generation_transmission_gate_is_required(self) -> None:
        failures = self.mutate("src/HttpGameStateServer.cpp", "if (!CanTransmitSnapshot())", "if (false)", last=True)
        self.assert_prefix(failures, "generation:")

    def test_06_http_cannot_borrow_native_player_objects(self) -> None:
        root = self.scratch()
        path = root / "src/HttpGameStateServer.cpp"
        path.write_text(path.read_text() + "\nPlayer* borrowed;\n")
        self.assertIn("world:http-accesses-core-object", analyze(root))

    def test_07_traversal_requires_active_request(self) -> None:
        failures = self.mutate("src/GameStateAPI.cpp",
            "return IsGameStateRequestActive(control);", "return true;")
        self.assertIn("cancel:traversal-not-active-checked", failures)

    def test_08_world_update_must_process_requests(self) -> None:
        failures = self.mutate("src/GameStateAPI.cpp", "    ProcessGameStateRequests();", "    ;")
        self.assertIn("world:dispatch-not-in-world-update", failures)

    def test_09_equipment_option_survives_world_dispatch(self) -> None:
        root = self.scratch()
        path = root / "src/GameStateAPI.cpp"
        path.write_text(path.read_text().replace("request.IncludeEquipment", "false"))
        self.assertIn("options:world-request-loses-equipment-option", analyze(root))

    def test_10_write_routes_and_gameplay_mutations_are_rejected(self) -> None:
        for insertion in ('_server->Post("/api/write", [](auto const&, auto&) {});',
                          'void UnsafeMutation(Player* player) { player->SetLevel(80); }'):
            with self.subTest(insertion=insertion):
                root = self.scratch()
                path = root / "src/HttpGameStateServer.cpp"
                path.write_text(path.read_text() + "\n" + insertion)
                self.assertTrue(analyze(root))

    def test_11_authentication_cannot_be_bypassed(self) -> None:
        failures = self.mutate("src/HttpGameStateServer.cpp", "!IsAuthorized(req)", "false")
        self.assertIn("surface:authenticated-get-gate", failures)

    def test_inventory_route_is_required(self) -> None:
        failures = self.mutate("src/HttpGameStateServer.cpp",
            '"/api/player/([^/]+)/inventory"', '"/api/player/([^/]+)/other"')
        self.assertIn("surface:inventory-route-missing", failures)

    def test_extra_get_route_is_rejected(self) -> None:
        root = self.scratch()
        path = root / "src/HttpGameStateServer.cpp"
        path.write_text(path.read_text() + '\n_server->Get("/unexpected", [](auto const&, auto&) {});\n')
        self.assertIn("surface:get-route-count", analyze(root))

    def test_12_cpp_httplib_drain_semantics_must_remain_audited(self) -> None:
        failures = self.mutate("include/yhirose/httplib.h",
            "if (pool_.shutdown_ && pool_.jobs_.empty()) { break; }",
            "if (pool_.shutdown_) { break; }")
        self.assertIn("httplib:queue-drain-semantics-missing", failures)


if __name__ == "__main__":
    unittest.main()
