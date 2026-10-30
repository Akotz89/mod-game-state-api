# Game State API

Read-only REST observability for AzerothCore players and the realm. This maintained
variant restores the author's detailed data while capturing native state on the
world thread. HTTP workers receive owned JSON snapshots, never borrowed player
objects. Sessions with cached RBAC metadata provide account names during the native
snapshot. Missing cached names are resolved on the HTTP worker after the world
state is copied, so database waits cannot stall the world callback. Every GET requires `Authorization: Bearer <configured token>`; CORS
preflight uses OPTIONS.

Author source: [abutbul/mod-game-state-api](https://github.com/abutbul/mod-game-state-api)
at `ca8a6c84a5fedd460a2011039d90dc4f573e30fd`. Maintained source:
[Akotz89/mod-game-state-api](https://github.com/Akotz89/mod-game-state-api).

## Offline CI checks

CI runs the existing Python source-contract mutation tests and core API
presence-verifier tests. These are static checks; they do not compile a WoW
server or establish deployment, HTTP runtime, or gameplay acceptance.

## Routes

| GET route | Response |
|---|---|
| `/api/health` | Native snapshot readiness status and uptime. |
| `/api/server` | Uptime, current/start times, session/player counts and all in-world characters. |
| `/api/players` | `players` array and `count` for every in-world player, including Playerbots. |
| `/api/player/{name}` | Character identity, level/class/race, location, health/power, guild/group, currency, played time, basic stats and status. |
| `/api/player/{name}/stats` | Experience, attributes, health, all power types, combat values, resistances, status and average item level. |
| `/api/player/{name}/equipment` | Equipped slots with item metadata, stats, resistances, weapon damage, durability, sockets and item spells. |
| `/api/player/{name}/inventory` | Complete live inventory, including bag contents, item ownership and load-suffix evidence. |
| `/api/player/{name}/skills` | Skill lines, learned talents/spec metadata and castable spells with IDs, names, ranks, schools, cast times, cooldowns and ranges. |
| `/api/player/{name}/skills-full` | The same complete skill/talent/spell response, retaining the author route alias. |
| `/api/player/{name}/quests` | Active quests with IDs, descriptions, status and objective progress; separate rewarded history and counts. |

`/api/players?equipment=true` includes each player's equipment.
`/api/player/{name}?include=equipment` includes that player's equipment.
Values are case-sensitive. The author's `include` behavior recognizes `equipment`
within the supplied include value; `equipment=true` requires the exact value
`true`. Omitted/nonmatching parameters omit the optional equipment field. The dedicated equipment
route always returns equipment. Other routes do not have a detail-profile switch.

Collections have no arbitrary player/spell/quest count or response-size cap. A
successful response contains the complete eligible collection. A cancelled or
expired request returns an explicit error, never a partial successful payload.
The locked native player registry includes Playerbots whose sessions are outside
the normal network-session map. `characters_in_world` includes them; retained
`player_count`/`online_players` follow the core network-session counters.
Only online, in-world players are exposed; missing/offline names return 404.

## Data semantics

Both skills routes return `skills` and its compatible `passive_skills` alias,
`talents` with learned records and spec/point metadata, and `castable_spells`
with `spell_count`. Skill values include pure values, maxima and temporary/
permanent bonuses. `castable_spells` is the author's legacy field name for the
nonpassive learned-spell list: passive and removed spells are excluded, while
inactive records retain `active` and `spec_mask` metadata.
The list does not assert every returned spell is currently castable. Missing
spell/talent metadata retains the record ID with
`missing_template: true`; unnamed skill lines retain their IDs and values.

Inventory includes equipped, backpack and bank slots and bag contents, excluding buyback. It returns 503 rather than a partial successful result if item identity, ownership or container data is inconsistent. The response includes current item suffixes and separately derived load-suffix evidence.

Equipment is keyed by the equipped slot name. Empty slots are `null`. When an
item template is unavailable, the item entry, count and durability remain
available. Socket information describes the item template's sockets and socket
bonus; it is not a claim to enumerate every applied enchantment.

The quest response uses `active_quests` for quests in the active status map and
`completed_quests` for the core's separate rewarded set. A completed active quest
ready for turn-in remains active until rewarded. Rewarded history survives its
removal from the active map. A missing quest template retains the quest ID,
so history does not disappear merely because descriptive metadata is unavailable.

`combat.hit_chance` values are core hit bonuses/modifiers, not absolute hit
probabilities against a target. `combat.critical_chance.spell` preserves the first
magical school (Holy) value; `spell_by_school` reports the individual magical
schools. Metadata names use the default locale. Counts describe their accompanying
collections rather than substituting aggregate-only data for the records.

`combat.spell_power` preserves the author's base value, also exposed as
`base_spell_power`. `spell_damage_bonus_by_school` and `healing_bonus` read the
core's effective signed character-sheet bonuses. PvP reads actual PvP state,
not its expiration timer flag. An unavailable account-name cache is resolved
by the HTTP worker; database stalls can occupy that worker and trigger the
existing listener-retention behavior during reload, without blocking native
world snapshot traversal.

## Configuration

Install the module using the normal AzerothCore module build, then configure
`conf/gamestate_api.conf.dist` in the realm's module configuration directory.
The distribution defaults disable the listener and bind to `127.0.0.1:8080`.

| Setting | Meaning |
|---|---|
| `GameStateAPI.Enable` | `0` disables; `1` enables after configuration validation. |
| `GameStateAPI.Host` | Bind hostname or numeric IPv4/IPv6 address. The listener validates resolution/binding. |
| `GameStateAPI.Port` | TCP port from 1 through 65535. |
| `GameStateAPI.AllowedOrigin` | Explicit HTTP(S) origin, optionally with a port, or `*`. Origin-bearing requests must match an explicit configured origin; wildcard accepts origins. |
| `GameStateAPI.AuthToken` | Required token with 32–256 non-whitespace, non-control characters. Keep the actual credential out of source control and logs. |

Host/origin validation supports legitimate configured consumers; it does not
force every configuration to loopback. Host casing and default HTTP(S) ports are normalized when comparing origins.
Authentication remains required for GET with either explicit or wildcard CORS. Enabling this module does not itself
change firewall, network routing or deployment access.

Reload retires the prior listener and invalidates its requests before publishing
new configuration. An unchanged enabled valid configuration retries a failed
listener, allowing recovery after a port conflict clears. If worker drain has
not finished, the listener stays owned and replacement is refused until process
restart. Uncommitted responses are fenced against generation retirement. A response
already committed before retirement may finish transmission on its existing
socket; this is not a guarantee of retracting bytes already sent.

## Errors and verification

Responses are JSON with `Cache-Control: no-store`. Invalid names return 400,
missing/bad credentials 401, disallowed origins 403, offline/missing players 404,
unsupported methods 405, internal failures 500, unavailable/full queues or
configuration changes 503, and expired/cancelled snapshots 504. The HTTP worker
pool and native request admission remain bounded; these are concurrency limits,
not record-redaction policies.

See [tests/README.md](tests/README.md) for static checks, populated native fixtures
and exact-core compatibility checks. Passing a source check or empty health
request alone does not prove populated functionality, native ABI closure or
same-realm deployment acceptance. Release evidence records those results
separately.

Licensed under AGPL-3.0; retain author notices and applicable source obligations.
