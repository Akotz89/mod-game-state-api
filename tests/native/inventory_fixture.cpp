#define main baseline_main
#include "api_fixture.cpp"
#undef main
#include <fstream>

int main(int argc, char** argv)
{
    CHECK(argc == 2);
    CHECK(baseline_main() == 0);
    Sessions.sessions.clear();
    FixturePlayerRegistry.clear();
    FixtureWorldThread = std::this_thread::get_id();
    SetGameStateRequestAcceptance(1);
    Player player;
    FixturePlayerRegistry[1] = &player;
    ItemTemplate proto;
    std::array<Item, PLAYER_SLOT_END> base;
    std::array<Bag, 11> bags;
    std::array<Item, 11 * MAX_BAG_SIZE> children;
    unsigned nextBag = 0;
    for (unsigned slot = 0; slot < PLAYER_SLOT_END; ++slot)
    {
        base[slot].proto = &proto;
        base[slot].guid.value = 0x4000000000000000ULL + 1000 + slot;
        base[slot].slot = slot;
        player.equipment[slot] = &base[slot];
        if ((slot >= 19 && slot < 23) || (slot >= 67 && slot < 74))
        {
            auto& bag = bags[nextBag];
            static_cast<Item&>(bag) = base[slot];
            player.equipment[slot] = &bag;
            player.bags[slot] = &bag;
            for (unsigned child = 0; child < MAX_BAG_SIZE; ++child)
            {
                auto& item = children[nextBag * MAX_BAG_SIZE + child];
                item.proto = &proto;
                item.guid.value = 0x4000000000000000ULL + 2000 + nextBag * MAX_BAG_SIZE + child;
                item.container = &bag;
                item.slot = child;
                item.property = -7;
                item.suffix = 55;
                item.count = 3;
                bag.contents[child] = &item;
            }
            ++nextBag;
        }
    }
    base[0].suffix = 88; // Actual seed remains distinct from nonnegative loader-zero.
    auto full = Snapshot(GameStateRequestType::Inventory);
    CHECK(full.Status == 200);
    CHECK(full.Payload["complete"] == true);
    CHECK(full.Payload["guid"] == "77");
    CHECK(full.Payload["money"] == 123456);
    CHECK(full.Payload["items"].size() == 534);
    CHECK(full.Payload["load_suffix_evidence"].size() == 2);
    CHECK(full.Payload["container_evidence"].size() == 11);
    CHECK(full.Payload["items"][0]["suffix"] == 88);
    CHECK(full.Payload["items"][0]["item_guid"] == "1000");
    CHECK(full.Payload["load_suffix_evidence"][0]["load_suffix"] == 0);
    CHECK(full.Payload["load_suffix_evidence"][1]["load_suffix"] == 12445);
    std::set<unsigned> baseSlots;
    for (auto const& row : full.Payload["items"])
    {
        if (row["bag_guid"] == "0") baseSlots.insert(row["slot"].get<unsigned>());
        else { CHECK(row["count"] == 3); CHECK(row["property"] == -7); CHECK(row["suffix"] == 55); }
    }
    CHECK(baseSlots.size() == 138);
    for (unsigned slot = 0; slot < PLAYER_SLOT_END; ++slot)
        CHECK(baseSlots.contains(slot) == !(slot >= 74 && slot < 86));

    std::map<std::string, json> actualItems;
    for (auto const& row : full.Payload["items"])
        CHECK(actualItems.emplace(row["item_guid"].get<std::string>(), row).second);
    std::map<std::string, json> actualContainers;
    for (auto const& witness : full.Payload["container_evidence"])
        CHECK(actualContainers.emplace(witness["item_guid"].get<std::string>(), witness).second);
    for (unsigned b = 0; b < bags.size(); ++b)
    {
        std::string parent = std::to_string(1000 + bags[b].slot);
        CHECK(actualContainers.at(parent)["entry"] == 12345);
        CHECK(actualContainers.at(parent)["slots"] == 36);
        for (unsigned child = 0; child < MAX_BAG_SIZE; ++child)
        {
            std::string expected = std::to_string(2000 + b * MAX_BAG_SIZE + child);
            auto const& row = actualItems.at(expected);
            CHECK(row["item_guid"] == expected);
            CHECK(row["bag_guid"] == parent);
            CHECK(row["slot"] == child);
        }
    }
    bags[0].capacity = 4;
    auto fourSlot = Snapshot(GameStateRequestType::Inventory);
    CHECK(fourSlot.Status == 200);
    CHECK(fourSlot.Payload["items"].size() == 502);
    CHECK(fourSlot.Payload["container_evidence"][0]["slots"] == 4);
    for (auto const& row : fourSlot.Payload["items"])
        if (row["bag_guid"] == "1019") CHECK(row["slot"].get<unsigned>() < 4);
    bags[0].capacity = 0;
    auto zeroSlot = Snapshot(GameStateRequestType::Inventory);
    CHECK(zeroSlot.Status == 200);
    CHECK(zeroSlot.Payload["items"].size() == 498);
    CHECK(zeroSlot.Payload["container_evidence"][0]["slots"] == 0);
    bags[0].capacity = MAX_BAG_SIZE;

    auto unavailable = [&] {
        auto result = Snapshot(GameStateRequestType::Inventory);
        CHECK(result.Status == 503);
        CHECK(!result.Payload.contains("items"));
        CHECK(!result.Payload.contains("complete"));
    };
    base[0].guid.value = 0; unavailable(); base[0].guid.value = 0x40000000000003e8ULL;
    base[0].entry = 0; unavailable(); base[0].entry = 12345;
    base[0].owner.value = 78; unavailable(); base[0].owner.value = 77;
    base[0].proto = nullptr; unavailable(); base[0].proto = &proto;
    base[0].guid.value = 0x40000000000003e9ULL; unavailable(); base[0].guid.value = 0x40000000000003e8ULL;
    base[0].slot = 1; unavailable(); base[0].slot = 0;
    base[0].container = &bags[0]; unavailable(); base[0].container = nullptr;
    base[0].count = 0; unavailable(); base[0].count = 1;
    bags[0].capacity = MAX_BAG_SIZE + 1; unavailable(); bags[0].capacity = MAX_BAG_SIZE;
    player.bags.erase(19); unavailable();
    player.equipment[19] = &base[19]; unavailable(); // Ordinary item in an equipped bag slot.
    player.equipment[19] = &bags[0]; player.bags[19] = &bags[0];
    children[0].container = nullptr; unavailable(); children[0].container = &bags[0];

    // End processing during the traversal; the queue must replace any in-progress value with an error.
    auto pending = QueueGameStateRequest(1, GameStateRequestType::Inventory, "Fixture");
    unsigned reads = 0;
    FixtureReadHook = [&] { if (++reads == 45) CancelGameStateRequest(pending.Control); };
    ProcessGameStateRequests(); FixtureReadHook = {};
    auto cancelled = pending.Future.get();
    CHECK(cancelled.Status == 504); CHECK(!cancelled.Payload.contains("items")); CHECK(reads < 60);
    pending = QueueGameStateRequest(1, GameStateRequestType::Inventory, "Fixture");
    reads = 0;
    FixtureReadHook = [&] { if (++reads == 45) pending.Control->Deadline = std::chrono::steady_clock::now() - 1s; };
    ProcessGameStateRequests(); FixtureReadHook = {};
    CHECK(pending.Future.get().Status == 504);
    pending = QueueGameStateRequest(1, GameStateRequestType::Inventory, "Fixture");
    reads = 0;
    FixtureReadHook = [&] { if (++reads == 45) SetGameStateRequestAcceptance(2); };
    ProcessGameStateRequests(); FixtureReadHook = {};
    CHECK(pending.Future.get().Status == 504); SetGameStateRequestAcceptance(1);
    player.online = false; CHECK(Snapshot(GameStateRequestType::Inventory).Status == 404); player.online = true;
    CHECK(Snapshot(GameStateRequestType::Inventory, "Missing").Status == 404);
    CHECK(QueueGameStateRequest(2, GameStateRequestType::Inventory, "Fixture").Future.get().Status == 503);

    int port = FreePort();
    HttpGameStateServer http("127.0.0.1", port, "http://127.0.0.1", Token, 1);
    CHECK(http.Start());
    std::string path = "/api/player/Fixture/inventory";
    CHECK(Body(Request(port, path))["items"].size() == 534);
    CHECK(Body(Request(port, path, ""), 401)["status"] == 401);
    CHECK(Body(Request(port, path, NewToken), 401)["status"] == 401);
    CHECK(Body(Request(port, path, Token, "GET", "http://other.test"), 403)["status"] == 403);
    CHECK(Body(Request(port, path, Token, "POST"), 405)["status"] == 405);
    CHECK(Body(Request(port, "/api/player/Bad%5CName/inventory"), 400)["status"] == 400);
    CHECK(Body(Request(port, "/api/player/Missing/inventory"), 404)["status"] == 404);
    CHECK(Body(Request(port, path, Token, "GET", "", []{}), 504)["status"] == 504);
    ProcessGameStateRequests();
    SetGameStateRequestAcceptance(2); CHECK(Body(Request(port, path), 503)["status"] == 503);
    SetGameStateRequestAcceptance(1); CHECK(http.Stop() == HttpGameStateStopResult::Stopped);

    player.equipment.fill(nullptr); player.bags.clear();
    auto empty = Snapshot(GameStateRequestType::Inventory);
    CHECK(empty.Status == 200); CHECK(empty.Payload["complete"] == true); CHECK(empty.Payload["items"].empty());
    CHECK(empty.Payload["container_evidence"].empty());
    FixturePlayerRegistry.clear();
    player.throwOnRead = true;
    base.fill(Item{}); children.fill(Item{}); bags.fill(Bag{});
    CHECK(full.Payload["items"].size() == 534); CHECK(json::parse(full.Payload.dump())["guid"] == "77");
    CHECK(full.Payload["items"][20]["count"] == 3);
    CHECK(full.Payload["container_evidence"][0]["slots"] == 36);
    json detached;
    std::string rawOwner;
    {
        Player temporaryPlayer;
        temporaryPlayer.guid.value = 0x123400000000004dULL;
        rawOwner = std::to_string(temporaryPlayer.guid.value);
        Item temporaryItem;
        temporaryItem.proto = &proto;
        temporaryItem.guid.value = 0x4000000000000009ULL;
        temporaryItem.owner = temporaryPlayer.guid;
        temporaryPlayer.equipment[0] = &temporaryItem;
        Bag temporaryBag;
        temporaryBag.proto = &proto;
        temporaryBag.guid.value = 0x400000000000000aULL;
        temporaryBag.owner = temporaryPlayer.guid;
        temporaryBag.slot = 19;
        temporaryBag.capacity = 4;
        temporaryPlayer.equipment[19] = &temporaryBag;
        temporaryPlayer.bags[19] = &temporaryBag;
        FixturePlayerRegistry[1] = &temporaryPlayer;
        auto result = Snapshot(GameStateRequestType::Inventory);
        CHECK(result.Status == 200);
        detached = std::move(result.Payload);
        FixturePlayerRegistry.clear();
    }
    CHECK(json::parse(detached.dump())["guid"] == rawOwner);
    CHECK(detached["items"][0]["item_guid"] == "9");
    CHECK(detached["items"][0]["owner_guid"] == rawOwner);
    CHECK(detached["container_evidence"][0]["item_guid"] == "10");
    CHECK(detached["container_evidence"][0]["slots"] == 4);
    json nativeContract = {{"schema", "cpi225-synthetic-native-contract-v1"}, {"maximal", json::object()}};
    {
        Player synthetic;
        synthetic.guid.value = 1121;
        Bag four;
        four.proto = &proto;
        four.guid.value = 0x4000000000000000ULL | 4000000000ULL;
        four.owner = synthetic.guid;
        four.entry = 500;
        four.slot = 19;
        four.capacity = 4;
        Item child;
        child.proto = &proto;
        child.guid.value = 0x4000000000000000ULL | 4000000001ULL;
        child.owner = synthetic.guid;
        child.container = &four;
        child.slot = 3;
        child.property = -7;
        child.suffix = GenerateEnchSuffixFactor(child.entry);
        child.count = 2;
        four.contents[3] = &child;
        synthetic.equipment[19] = &four;
        synthetic.bags[19] = &four;
        FixturePlayerRegistry[1] = &synthetic;
        auto observed = Snapshot(GameStateRequestType::Inventory);
        CHECK(observed.Status == 200);
        CHECK(observed.Payload["guid"] == "1121");
        nativeContract["four_slot"] = std::move(observed.Payload);
        FixturePlayerRegistry.clear();
    }
    for (unsigned actor = 0; actor < 2; ++actor)
    {
        Player synthetic;
        synthetic.guid.value = 1121 + actor;
        std::array<Item, PLAYER_SLOT_END> originals;
        std::array<Bag, 11> equipped;
        std::array<Item, 11 * MAX_BAG_SIZE> contents;
        unsigned bagIndex = 0;
        for (unsigned slot = 0; slot < PLAYER_SLOT_END; ++slot)
        {
            Item& item = originals[slot];
            item.proto = &proto;
            item.guid.value = 0x4000000000000000ULL | (1000000 + actor * 2000000 + slot);
            item.owner = synthetic.guid;
            item.slot = slot;
            synthetic.equipment[slot] = &item;
            if ((slot >= 19 && slot < 23) || (slot >= 67 && slot < 74))
            {
                Bag& bag = equipped[bagIndex];
                static_cast<Item&>(bag) = item;
                synthetic.equipment[slot] = &bag;
                synthetic.bags[slot] = &bag;
                for (unsigned slotInBag = 0; slotInBag < MAX_BAG_SIZE; ++slotInBag)
                {
                    Item& child = contents[bagIndex * MAX_BAG_SIZE + slotInBag];
                    child.proto = &proto;
                    child.guid.value = 0x4000000000000000ULL | (2000000 + actor * 2000000 + bagIndex * MAX_BAG_SIZE + slotInBag);
                    child.owner = synthetic.guid;
                    child.container = &bag;
                    child.slot = slotInBag;
                    child.property = -7;
                    child.suffix = GenerateEnchSuffixFactor(child.entry);
                    child.count = 3;
                    bag.contents[slotInBag] = &child;
                }
                ++bagIndex;
            }
        }
        FixturePlayerRegistry[1] = &synthetic;
        auto observed = Snapshot(GameStateRequestType::Inventory);
        CHECK(observed.Status == 200);
        CHECK(observed.Payload["guid"] == std::to_string(1121 + actor));
        CHECK(observed.Payload["items"].size() == 534);
        CHECK(observed.Payload["container_evidence"].size() == 11);
        nativeContract["maximal"][std::to_string(1121 + actor)] = std::move(observed.Payload);
        FixturePlayerRegistry.clear();
    }
    std::ofstream contractFile(argv[1], std::ios::out | std::ios::trunc);
    CHECK(contractFile.is_open());
    contractFile << nativeContract.dump(2) << '\n';
    contractFile.close();
    CHECK(!contractFile.fail());
    std::cout << "PASS complete534 native inventory, exact396 child identities/parents/slots and11 bag witnesses, four/zero-slot bags, copied lifetime, corruption, queue cancellation/expiry/generation, actual inventory HTTP auth/method/name/offline/timeout\n";
}
