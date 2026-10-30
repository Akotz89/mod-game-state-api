# API verification

The checks serve different purposes. A static pass is not populated API behavior
or deployment proof.

## Static architecture regression checks

`source_contract.py` inspects real C++ function bodies and ordering. Its mutation
suite checks configuration/authentication validation, listener ownership, failed
retirement before replacement, generation checks around response publication,
world-callback dispatch, no HTTP-worker native object access, cancellation and
equipment-option forwarding. It also verifies the vendored worker queue drains
before joining. It does not ban rich response fields or require arbitrary
collection/output caps.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_source_contract.py' -v
```

Scratch copies are created under `tests/` and automatically removed. The static
analyzer is deliberately limited: it does not prove C++ execution, object
lifetime, populated field values, socket behavior or absence of every possible
mutation. Native fixtures and exact-core compilation provide stronger evidence.

## Candidate-core API presence

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/check_exact_core_api.py /path/to/candidate/core
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -p 'test_exact_core_api.py' -v
```

This reads the actual candidate core and confirms the required script hooks,
session/player/quest/skill and inventory/bag APIs, world-config access, and vendored HTTP interfaces. It reports the core
commit and committed tree; it does not demand an obsolete clean revision. It
neither compiles nor identifies uncommitted source content. Candidate source
hashes and native compilation must be recorded separately.

## Populated native execution and release acceptance

Run from the module directory:

```sh
WOW_OPERATIONS_ROOT=/path/to/operations-repo tests/run_native_fixture.sh /path/to/candidate/core /path/to/operations-repo/work/task/native-build
```

Supply the operations root, candidate core and a task-owned output directory as
shown above. The runner invokes the repository build-host guard before compiling.
The output must be inside the operations checkout `work/` directory. It receives
the fixture binary, synthetic contract and hash list; the output directory must not already exist to protect other runs' artifacts.

The native suite compiles and executes the actual module serializers, queue and
HTTP implementation with representative core fixtures and vendored JSON/HTTP.
It covers equipment options, rich player/stat/skill/talent/quest data, separate
rewarded history, missing metadata, large collections, cancellation and offline
objects. Handler/lifecycle cases establish auth/errors and listener behavior.
The inventory fixture also checks full bag traversal, failure and cancellation, HTTP authorization, and copied values using synthetic players. Fixtures are backend behavior evidence; they are not real earned-player state.

Every native build entrypoint must run the repository's build-host guard before
compilation. Exact-core translation-unit compilation and the guarded incremental
worldserver build establish compatibility beyond fixture interfaces. Same-realm
activation then needs executable identity, initialized services, configured
listener and authenticated requests. Live populated-player responses, when
available, remain separate evidence from fixture execution. No protected client
interaction or earned-state mutation is needed for this suite.
