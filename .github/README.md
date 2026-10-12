# Game State API

Read-only, authenticated AzerothCore realm and player observability with complete
equipment, statistics, skills, talents and quest history.

See the maintained [module README](../README.md) for the nine GET routes,
equipment query options, data semantics, configuration and verification commands.
This implementation captures native state on the world thread and returns owned
JSON snapshots to HTTP workers.

The previous aggregate-only minimal profile and field/collection restrictions
are superseded. Full records are available to authorized consumers; cancellation
and expiration return explicit errors rather than partial successful results.

Original author: [abutbul/mod-game-state-api](https://github.com/abutbul/mod-game-state-api).
Maintained fork: [Akotz89/mod-game-state-api](https://github.com/Akotz89/mod-game-state-api).
Licensed under AGPL-3.0.
