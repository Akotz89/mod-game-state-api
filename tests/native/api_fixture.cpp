#include "CoreFixture.h"
#include "GameStateAPI.h"
#include "GameStateUtilities.h"
#include "HttpGameStateServer.h"
#include <functional>
#include <condition_variable>
#include <future>
#include <iostream>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
using json=nlohmann::json;
using namespace std::chrono_literals;
static std::atomic<unsigned> checks{0};
#define CHECK(expr) do { ++checks; if (!(expr)) { std::cerr<<"FAIL line "<<__LINE__<<": "<<#expr<<"\n"; std::abort(); } } while(false)
static std::string const Token(40,'x'); // Synthetic fixture credential only.
static std::string const NewToken(40,'y');
static int FreePort() {int fd=socket(AF_INET,SOCK_STREAM,0);CHECK(fd>=0);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);CHECK(bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0);socklen_t n=sizeof(a);CHECK(getsockname(fd,reinterpret_cast<sockaddr*>(&a),&n)==0);int p=ntohs(a.sin_port);close(fd);return p;}
static std::shared_ptr<GameStateRequestControl> Control(std::uint64_t generation=1) {auto c=std::make_shared<GameStateRequestControl>();c->Generation=generation;c->State=GameStateRequestState::Processing;c->Deadline=std::chrono::steady_clock::now()+5s;return c;}
static GameStateResult Snapshot(GameStateRequestType type,std::string name="Fixture",bool gear=false) {auto q=QueueGameStateRequest(1,type,std::move(name),gear);ProcessGameStateRequests();CHECK(q.Future.wait_for(0s)==std::future_status::ready);return q.Future.get();}
static void CheckCachedWorldConfig() {
 auto expectedCount=GetWorldConfigLoadCallbackCount();
 sWorld->leaveGroupOnLogout=false;
 auto before=GameStateUtilities::GetServerData(Control());auto field=before.at("effective_world_config");
 CHECK(field.size()==3);CHECK(field.at("schema")=="cpi388-cached-world-config-v1");
 CHECK(field.at("world_config_load_callback_count").is_number_unsigned());CHECK(field.at("world_config_load_callback_count")==expectedCount);
 CHECK(field.at("leave_group_on_logout").is_boolean());CHECK(field.at("leave_group_on_logout")==false);
 sWorld->leaveGroupOnLogout=true;
 auto after=GameStateUtilities::GetServerData(Control());CHECK(after.at("effective_world_config").at("leave_group_on_logout")==true);
 CHECK(after.at("effective_world_config").at("world_config_load_callback_count")==expectedCount);
 before.erase("effective_world_config");after.erase("effective_world_config");CHECK(before==after);
 sWorld->leaveGroupOnLogout=false;
}
static httplib::Result Request(int port,std::string path,std::string token=Token,std::string method="GET",std::string origin="",std::function<void()> pump=ProcessGameStateRequests) {
 auto future=std::async(std::launch::async,[=]{httplib::Client client("127.0.0.1",port);client.set_connection_timeout(1,0);client.set_read_timeout(3,0);httplib::Headers h;if(!token.empty())h.emplace("Authorization","Bearer "+token);if(!origin.empty())h.emplace("Origin",origin);if(method=="POST")return client.Post(path,h,"","text/plain");if(method=="OPTIONS")return client.Options(path,h);return client.Get(path,h);});
 auto deadline=std::chrono::steady_clock::now()+4s;
 while(future.wait_for(0s)!=std::future_status::ready&&std::chrono::steady_clock::now()<deadline){pump();std::this_thread::sleep_for(1ms);}CHECK(future.wait_for(0s)==std::future_status::ready);return future.get();
}
static json Body(httplib::Result const& response,int status=200) {CHECK(response!=nullptr);CHECK(response->status==status);return json::parse(response->body);}
static json& PlayerArray(json& j){return j.is_array()?j:j.at("players");}
int main() {
 std::cout<<std::unitbuf;
 CHECK(GameStateSecurity::IsValidHost("0.0.0.0"));CHECK(GameStateSecurity::IsValidHost("api.example.test"));CHECK(!GameStateSecurity::IsValidHost("host/path"));CHECK(GameStateSecurity::IsValidOrigin("https://example.test:443"));CHECK(GameStateSecurity::IsValidOrigin("*"));CHECK(!GameStateSecurity::IsValidOrigin("https://example.test/path"));CHECK(!GameStateSecurity::IsValidOrigin("https://example.test:65536"));
 FixtureWorldThread=std::this_thread::get_id();FixtureCheckThread=true;
 Player player;WorldSession session{&player};player.session=&session;Sessions.sessions[1]=&session;FixturePlayerRegistry[1]=&player;
 ItemTemplate sword;sword.Socket[0]={2,99};sword.Spells[0]={456,1,3,2000};Item item{&sword};player.equipment[EQUIPMENT_SLOT_MAINHAND]=&item;
 player.values[PLAYER_FIELD_MOD_HEALING_DONE_POS]=225;player.values[uint32(PLAYER_FIELD_MOD_DAMAGE_DONE_POS)+uint32(SPELL_SCHOOL_HOLY)]=300;player.values[uint32(PLAYER_FIELD_MOD_DAMAGE_DONE_POS)+uint32(SPELL_SCHOOL_FIRE)]=450;
 player.values[PLAYER_XP]=250;player.values[PLAYER_NEXT_LEVEL_XP]=1000;player.values[PLAYER_SKILL_INFO_1_1]=43|(2u<<16);
 player.floatValues[PLAYER_SPELL_CRIT_PERCENTAGE1]=0;player.floatValues[uint32(PLAYER_SPELL_CRIT_PERCENTAGE1)+uint32(SPELL_SCHOOL_HOLY)]=4;player.floatValues[uint32(PLAYER_SPELL_CRIT_PERCENTAGE1)+uint32(SPELL_SCHOOL_FIRE)]=6;
 Group group;player.group=&group;
 std::vector<PlayerSpell> learned(603);for(uint32 i=0;i<600;i++){player.spells[1000+i]=&learned[i];Spells.spells[1000+i]={};}
 SpellCastTimesEntry cast;SpellRangeEntry range;Spells.spells[1000].CastTimeEntry=&cast;Spells.spells[1000].RangeEntry=&range;
 learned[599].Active=false;learned[600].State=PLAYERSPELL_REMOVED;player.spells[2000]=&learned[600];Spells.spells[2000]={};
 player.spells[2001]=&learned[601];Spells.spells[2001].passive=true;player.spells[2002]=&learned[602];player.spells[2003]=nullptr;
 PlayerTalent talent;player.talents[1000]=&talent;TalentPositions[1000]={900,1};PlayerTalent removedTalent;removedTalent.State=PLAYERSPELL_REMOVED;player.talents[2000]=&removedTalent;
 for(uint32 i=0;i<300;i++){player.statuses[3000+i]={};Quests.quests[3000+i]={};player.rewarded.insert(4000+i);Quests.quests[4000+i]={};}
 player.statuses[3001].Status=QUEST_STATUS_COMPLETE;player.rewarded.insert(4999); // No template and never a row in active map.
 SetGameStateRequestAcceptance(1);
 CheckCachedWorldConfig();
 auto c=Control();auto gear=GameStateUtilities::GetPlayerEquipment(&player,c);
 CHECK(gear.size()==19);CHECK(gear["head"].is_null());CHECK(gear["mainhand"]["entry"]==12345);CHECK(gear["mainhand"]["stats"][0]["value"]==12);CHECK(gear["mainhand"]["durability"]==35);CHECK(gear["mainhand"]["max_durability"]==50);CHECK(gear["mainhand"]["sockets"][0]["color"]==2);CHECK(gear["mainhand"]["spells"][0]["spell_id"]==456);CHECK(gear["mainhand"]["weapon_data"]["dps"]==7.5f);
 Item withoutTemplate;player.equipment[EQUIPMENT_SLOT_HEAD]=&withoutTemplate;auto fallback=GameStateUtilities::GetPlayerEquipment(&player,c);CHECK(fallback["head"]["entry"]==12345);CHECK(fallback["head"]["durability"]==35);player.equipment[EQUIPMENT_SLOT_HEAD]=nullptr;
 auto stats=GameStateUtilities::GetPlayerStats(&player,c);CHECK(stats["attributes"]["strength"]==20.0f);CHECK(stats["health"]["current"]==900);CHECK(stats["power"]["runic_power"]["max"]==106);CHECK(stats["experience"]["current"]==250);CHECK(stats["combat"]["attack_power"]==200.0f);CHECK(stats["combat"]["critical_chance"]["spell"]==4.0f);CHECK(stats["combat"]["critical_chance"]["spell_by_school"]["fire"]==6.0f);CHECK(stats["combat"]["hit_chance"]["melee"]==2.25f);CHECK(stats["combat"]["hit_chance"]["ranged"]==3.5f);CHECK(stats["combat"]["hit_chance"]["spell"]==4.75f);CHECK(stats["resistances"]["fire"]==20);CHECK(stats["status"]["dnd"]==true);CHECK(stats["status"]["player_vs_player"]==true);CHECK(stats["combat"]["spell_power"]==150);CHECK(stats["combat"]["base_spell_power"]==150);CHECK(stats["combat"]["healing_bonus"]==225);CHECK(stats["combat"]["spell_damage_bonus_by_school"]["holy"]==300);CHECK(stats["combat"]["spell_damage_bonus_by_school"]["fire"]==450);player.pvp=false;player.pvpTimer=true;CHECK(GameStateUtilities::GetPlayerStats(&player,c)["status"]["player_vs_player"]==false);player.pvp=true;player.pvpTimer=false;
 auto data=GameStateUtilities::GetPlayerData(&player,c);CHECK(!data.contains("equipment"));CHECK(data["position"]["x"]==1.25f);CHECK(data["guid"]==77);CHECK(data["money"]==123456);CHECK(data["account_name"].is_null());CHECK(FixtureAccountLookups==0);CHECK(data["guild"]["name"]=="Fixture guild");CHECK(data["group"]["members_count"]==5);rbac::RBACData permissions;session.permissions=&permissions;CHECK(GameStateUtilities::GetPlayerData(&player,c)["account_name"]=="CachedFixtureAccount");session.permissions=nullptr;
 auto skills=GameStateUtilities::GetPlayerSkills(&player,c);CHECK(skills["castable_spells"].size()==601);CHECK(skills["spell_count"]==601);CHECK(skills["castable_spells"][0]["spell_id"]==1000);CHECK(skills["castable_spells"][0]["cast_time"]==1500);CHECK(skills["castable_spells"][0]["range"]==30.0f);CHECK(skills["castable_spells"].back()["spell_id"]==2002);CHECK(skills["castable_spells"].back()["missing_template"]==true);CHECK(skills["castable_spells"][599]["active"]==false);CHECK(skills["castable_spells"].back()["spec_mask"]==3);
 auto full=GameStateUtilities::GetPlayerSkillsFull(&player,c);CHECK(full["passive_skills"][0]["skill_id"]==43);CHECK(full["passive_skills"][0]["current_value"]==75);CHECK(full["passive_skills"][0]["max_value"]==150);CHECK(full["passive_skills"][0]["permanent_bonus"]==3);CHECK(full["passive_skills"][0]["temporary_bonus"]==2);CHECK(full["talents"]["talent_points"]["used"]==16);CHECK(full["talents"]["learned_talents"].size()==1);CHECK(full["talents"]["learned_talents"][0]["rank"]==2);CHECK(full["talents"]["learned_talents"][0]["spec_mask"]==3);
 auto quests=GameStateUtilities::GetPlayerQuests(&player,c);CHECK(quests["active_quests"].size()==300);CHECK(quests["completed_quests"].size()==301);CHECK(quests["rewarded_count"]==301);CHECK(quests["completed_count"]==301);CHECK(quests["active_quests"][0]["quest_id"]==3000);CHECK(quests["active_quests"][0]["item_objectives"][0]["item_id"]==55);CHECK(quests["active_quests"][0]["item_objectives"][0]["current_count"]==2);CHECK(quests["active_quests"][0]["creature_objectives"][0]["npc_or_go_id"]==-66);CHECK(quests["active_quests"][1]["ready_to_turn_in"]==true);CHECK(quests["completed_quests"].back()["quest_id"]==4999);CHECK(quests["completed_quests"].back()["missing_template"]==true);CHECK(player.statuses.count(4000)==0);CHECK(player.rewarded.size()==301);
 Player empty;auto noHistory=GameStateUtilities::GetPlayerQuests(&empty,c);CHECK(noHistory["active_quests"].empty());CHECK(noHistory["completed_quests"].empty());CHECK(noHistory["rewarded_count"]==0);
 CHECK(GameStateUtilities::GetPlayerEquipment(nullptr,c).empty());CHECK(GameStateUtilities::GetPlayerStats(nullptr,c).empty());CHECK(GameStateUtilities::GetPlayerSkills(nullptr,c).empty());CHECK(GameStateUtilities::GetPlayerSkillsFull(nullptr,c).empty());CHECK(GameStateUtilities::GetPlayerQuests(nullptr,c).empty());CHECK(GameStateUtilities::GetPlayerData(nullptr,c).empty());
 std::vector<Player> players(299);std::vector<WorldSession> sessions(299);for(uint32 i=0;i<299;i++){players[i].name="Player"+std::to_string(i);sessions[i].player=&players[i];players[i].session=&sessions[i];Sessions.sessions[i+2]=&sessions[i];FixturePlayerRegistry[i+2]=&players[i];}
 WorldSession noPlayer;Sessions.sessions[1001]=nullptr;Sessions.sessions[1002]=&noPlayer;Player offline;offline.name="Offline";offline.online=false;WorldSession offSession{&offline};Sessions.sessions[1003]=&offSession;FixturePlayerRegistry[1001]=nullptr;FixturePlayerRegistry[1003]=&offline;
 Player registryBot;registryBot.name="Registrybot";WorldSession botSession{&registryBot};registryBot.session=&botSession;registryBot.equipment[EQUIPMENT_SLOT_MAINHAND]=&item;FixturePlayerRegistry[500]=&registryBot;CHECK(std::none_of(Sessions.sessions.begin(),Sessions.sessions.end(),[&](auto const& entry){return entry.second==&botSession;}));
 auto all=GameStateUtilities::GetAllPlayersData(c);CHECK(PlayerArray(all).size()==301);CHECK(!PlayerArray(all)[0].contains("equipment"));auto allGear=GameStateUtilities::GetAllPlayersData(c,true);CHECK(PlayerArray(allGear).size()==301);CHECK(PlayerArray(allGear)[0]["equipment"]["mainhand"]["entry"]==12345);CHECK(PlayerArray(allGear).back()["name"]=="Registrybot");CHECK(PlayerArray(allGear).back()["equipment"]["mainhand"]["entry"]==12345);auto serverRegistry=GameStateUtilities::GetServerData(c);CHECK(serverRegistry["characters_in_world"]==301);CHECK(serverRegistry["player_count"]==300);CHECK(serverRegistry["online_players"]==300);

 for(auto type:{GameStateRequestType::Players,GameStateRequestType::SkillsFull,GameStateRequestType::Quests}){auto began=std::chrono::steady_clock::now();auto measured=Snapshot(type,"Fixture",type==GameStateRequestType::Players);auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();CHECK(measured.Status==200);std::cout<<"TIMING O0 queued native snapshot type="<<static_cast<int>(type)<<" elapsed_ms="<<elapsed<<" payload_bytes="<<measured.Payload.dump().size()<<" (fixture interfaces, not production capacity proof)\n";}
 auto q=QueueGameStateRequest(1,GameStateRequestType::Players);CancelGameStateRequest(q.Control);ProcessGameStateRequests();CHECK(q.Future.get().Status==504);
 q=QueueGameStateRequest(1,GameStateRequestType::Players);q.Control->Deadline=std::chrono::steady_clock::now()-1s;ProcessGameStateRequests();CHECK(q.Future.get().Status==504);
 q=QueueGameStateRequest(1,GameStateRequestType::Players);SetGameStateRequestAcceptance(2);ProcessGameStateRequests();CHECK(q.Future.get().Status==503);SetGameStateRequestAcceptance(1);
 q=QueueGameStateRequest(1,GameStateRequestType::Players);unsigned reads=0;FixtureReadHook=[&]{if(++reads==50)CancelGameStateRequest(q.Control);};ProcessGameStateRequests();FixtureReadHook={};CHECK(q.Future.get().Status==504);CHECK(reads>=50&&reads<100);
 q=QueueGameStateRequest(1,GameStateRequestType::Players);reads=0;FixtureReadHook=[&]{if(++reads==50)q.Control->Deadline=std::chrono::steady_clock::now()-1s;};ProcessGameStateRequests();FixtureReadHook={};CHECK(q.Future.get().Status==504);
 q=QueueGameStateRequest(1,GameStateRequestType::SkillsFull,"Fixture");reads=0;FixtureReadHook=[&]{if(++reads==16)CancelGameStateRequest(q.Control);};ProcessGameStateRequests();FixtureReadHook={};CHECK(q.Future.get().Status==504);CHECK(reads>=16&&reads<30);
 std::vector<QueuedGameStateRequest> saturated;for(int i=0;i<33;i++)saturated.push_back(QueueGameStateRequest(1,GameStateRequestType::Health));CHECK(saturated.back().Future.wait_for(0s)==std::future_status::ready);CHECK(saturated.back().Future.get().Status==503);CancelGameStateRequests("fixture saturation drain");for(int i=0;i<32;i++)CHECK(saturated[i].Future.get().Status==503);SetGameStateRequestAcceptance(1);
 CHECK(Snapshot(GameStateRequestType::Player,"Missing").Status==404);CHECK(Snapshot(GameStateRequestType::Player,offline.name).Status==404);
 player.throwOnRead=true;CHECK(Snapshot(GameStateRequestType::Player).Status==500);player.throwOnRead=false;
 c->State=GameStateRequestState::Cancelled;CHECK(GameStateUtilities::GetAllPlayersData(c).empty());CHECK(GameStateUtilities::GetPlayerQuests(&player,c).empty());CHECK(GameStateUtilities::GetPlayerSkillsFull(&player,c).empty());c=Control();c->Deadline=std::chrono::steady_clock::now()-1s;CHECK(GameStateUtilities::GetPlayerStats(&player,c).empty());
 std::cout<<"PASS populated serializers: equipment, stats, account/group, 600 castable spells + 1 retained missing-template spell, skills/talents, 300 active + 301 durable rewarded quests, 301 registry players including a non-network-session bot, null/offline/cancellation/expiry/errors\n";
 int port=FreePort();HttpGameStateServer http("127.0.0.1",port,"http://127.0.0.1",Token,1);CHECK(http.Start());CHECK(!http.Start());
 CHECK(Body(Request(port,"/api/health",""),401)["status"]==401);CHECK(Body(Request(port,"/api/health",NewToken),401)["status"]==401);CHECK(Body(Request(port,"/api/health",Token,"GET","http://other.test"),403)["status"]==403);CHECK(Body(Request(port,"/api/health",Token,"POST"),405)["status"]==405);auto preflight=Request(port,"/api/players","","OPTIONS");CHECK(preflight!=nullptr);CHECK(preflight->status==204);auto allowedHeaders=preflight->get_header_value("Access-Control-Allow-Headers");CHECK(allowedHeaders.find("Content-Type")!=std::string::npos);CHECK(allowedHeaders.find("Authorization")!=std::string::npos);CHECK(allowedHeaders.find("X-Requested-With")!=std::string::npos);
 std::vector<std::string> routes={"/api/health","/api/server","/api/players","/api/player/Fixture","/api/player/Fixture/stats","/api/player/Fixture/equipment","/api/player/Fixture/skills","/api/player/Fixture/skills-full","/api/player/Fixture/quests"};for(auto const& path:routes){auto body=Body(Request(port,path));CHECK(!body.empty());}
 auto lookupsBeforeList=FixtureAccountLookups.load();auto list=Body(Request(port,"/api/players?equipment=true"));CHECK(FixtureAccountLookups.load()-lookupsBeforeList==1);CHECK(PlayerArray(list).size()==301);CHECK(PlayerArray(list)[0]["equipment"]["mainhand"]["entry"]==12345);CHECK(list.dump().size()>64*1024);
 auto botResponse=Body(Request(port,"/api/player/Registrybot?include=equipment"));CHECK(botResponse["name"]=="Registrybot");CHECK(botResponse["equipment"]["mainhand"]["entry"]==12345);auto serverHttp=Body(Request(port,"/api/server"));CHECK(serverHttp["characters_in_world"]==301);CHECK(serverHttp["player_count"]==300);CHECK(list["count"]==301);CHECK(PlayerArray(list).back()["name"]=="Registrybot");CHECK(PlayerArray(list).back()["equipment"]["mainhand"]["entry"]==12345);
 auto plain=Body(Request(port,"/api/player/Fixture"));CHECK(!plain.contains("equipment"));CHECK(plain["account_name"]=="FixtureAccount");CHECK(FixtureAccountLookups>0);auto withGear=Body(Request(port,"/api/player/Fixture?include=equipment"));CHECK(withGear["equipment"]["mainhand"]["entry"]==12345);CHECK(Body(Request(port,"/api/player/Fixture?include=stats,equipment"))["equipment"]["mainhand"]["entry"]==12345);CHECK(!Body(Request(port,"/api/player/Fixture?include=invalid")).contains("equipment"));auto lookupsBeforeNextList=FixtureAccountLookups.load();auto falseEquipment=Body(Request(port,"/api/players?equipment=false"));CHECK(FixtureAccountLookups.load()-lookupsBeforeNextList==1);CHECK(!PlayerArray(falseEquipment)[0].contains("equipment"));
 CHECK(Body(Request(port,"/api/health",Token,"GET","",[]{}),504)["status"]==504);ProcessGameStateRequests();
 auto spellsHttp=Body(Request(port,"/api/player/Fixture/skills"));CHECK(spellsHttp["castable_spells"].size()==601);CHECK(spellsHttp["skills"][0]["skill_id"]==43);CHECK(spellsHttp["talents"]["learned_talents"].size()==1);CHECK(spellsHttp.dump().size()>64*1024);auto questsHttp=Body(Request(port,"/api/player/Fixture/quests"));CHECK(questsHttp["completed_quests"].size()==301);
 Quests.quests[3000].details=std::string(1,char(0xff));CHECK(Body(Request(port,"/api/player/Fixture/quests"),500)["status"]==500);Quests.quests[3000].details="Detailed quest description";
 auto unknown=Request(port,"/api/unknown");CHECK(Body(unknown,404)["status"]==404);CHECK(unknown->get_header_value("Cache-Control")=="no-store");
 CHECK(Body(Request(port,"/api/player/Missing"),404)["status"]==404);CHECK(Body(Request(port,"/api/player/Bad%5CName"),400)["status"]==400);

 // A blocked account DB lookup is on a worker: native health snapshots remain usable.
 std::mutex gateMutex;std::condition_variable gateCondition;bool entered=false,released=false;
 auto gate=[&]{std::unique_lock lock(gateMutex);entered=true;gateCondition.notify_all();CHECK(gateCondition.wait_for(lock,5s,[&]{return released;}));};
 auto startPlayerRequest=[&]{return std::async(std::launch::async,[&]{httplib::Client client("127.0.0.1",port);client.set_read_timeout(5,0);return client.Get("/api/player/Fixture",{{"Authorization","Bearer "+Token}});});};
 FixtureAccountLookupHook=gate;auto blocked=startPlayerRequest();auto until=std::chrono::steady_clock::now()+3s;
 while(std::chrono::steady_clock::now()<until){ProcessGameStateRequests();{std::lock_guard lock(gateMutex);if(entered)break;}std::this_thread::sleep_for(1ms);}
 {std::lock_guard lock(gateMutex);CHECK(entered);}CHECK(Snapshot(GameStateRequestType::Health).Status==200);SetGameStateRequestAcceptance(2);
 {std::lock_guard lock(gateMutex);released=true;}gateCondition.notify_all();CHECK(blocked.wait_for(3s)==std::future_status::ready);CHECK(Body(blocked.get(),503)["status"]==503);FixtureAccountLookupHook={};SetGameStateRequestAcceptance(1);
 // Cancellation from the real HTTP timeout interrupts a native collection at a barrier.
 entered=false;released=false;unsigned barrierReads=0;FixtureReadHook=[&]{if(++barrierReads==50)gate();};
 auto timedCollection=std::async(std::launch::async,[&]{httplib::Client client("127.0.0.1",port);client.set_read_timeout(4,0);return client.Get("/api/players",{{"Authorization","Bearer "+Token}});});
 auto releaseAfterTimeout=std::async(std::launch::async,[&]{std::unique_lock lock(gateMutex);CHECK(gateCondition.wait_for(lock,3s,[&]{return entered;}));lock.unlock();CHECK(timedCollection.wait_for(3s)==std::future_status::ready);lock.lock();released=true;lock.unlock();gateCondition.notify_all();});
 until=std::chrono::steady_clock::now()+3s;while(timedCollection.wait_for(0s)!=std::future_status::ready&&std::chrono::steady_clock::now()<until){ProcessGameStateRequests();std::this_thread::sleep_for(1ms);}CHECK(timedCollection.wait_for(0s)==std::future_status::ready);CHECK(Body(timedCollection.get(),504)["status"]==504);releaseAfterTimeout.get();FixtureReadHook={};CHECK(barrierReads>=50&&barrierReads<100);ProcessGameStateRequests();
 // Publication commitment excludes retirement until the same lock is released.
 {auto publication=LockGameStatePublication();std::promise<void> started;auto begun=started.get_future();auto retirement=std::async(std::launch::async,[&]{started.set_value();SetGameStateRequestAcceptance(2);});begun.wait();CHECK(retirement.wait_for(20ms)==std::future_status::timeout);CHECK(IsGameStateGenerationAccepted(1));publication.unlock();CHECK(retirement.wait_for(1s)==std::future_status::ready);retirement.get();CHECK(!IsGameStateGenerationAccepted(1));SetGameStateRequestAcceptance(1);}
 std::cout<<"PASS worker/native barriers: off-world account lookup, healthy world while worker blocked, retirement503, actual HTTP timeout cancellation504, publication exclusion\n";
 SetGameStateRequestAcceptance(2);CHECK(Body(Request(port,"/api/health"),503)["status"]==503);SetGameStateRequestAcceptance(1);CHECK(http.Stop()==HttpGameStateStopResult::Stopped);CHECK(!http.IsRunning());CHECK(http.Stop()==HttpGameStateStopResult::AlreadyStopped);

 {int originPort=FreePort();HttpGameStateServer normalized("127.0.0.1",originPort,"HTTP://Consumer.Example.Test:80",Token,1);CHECK(normalized.Start());auto normalizedResponse=Request(originPort,"/api/health",Token,"GET","http://CONSUMER.example.test");CHECK(Body(normalizedResponse)["status"]=="ok");CHECK(normalizedResponse->get_header_value("Access-Control-Allow-Origin")=="http://consumer.example.test");CHECK(normalized.Stop()==HttpGameStateStopResult::Stopped);}
 // A slow worker retains its listener owner after drain timeout; completion permits safe cleanup.
 {int slowPort=FreePort();HttpGameStateServer slow("127.0.0.1",slowPort,"http://127.0.0.1",Token,1);CHECK(slow.Start());entered=false;released=false;FixtureAccountLookupHook=gate;
 auto pending=std::async(std::launch::async,[&]{httplib::Client client("127.0.0.1",slowPort);client.set_read_timeout(5,0);return client.Get("/api/player/Fixture",{{"Authorization","Bearer "+Token}});});
 until=std::chrono::steady_clock::now()+3s;while(std::chrono::steady_clock::now()<until){ProcessGameStateRequests();{std::lock_guard lock(gateMutex);if(entered)break;}std::this_thread::sleep_for(1ms);}{std::lock_guard lock(gateMutex);CHECK(entered);}
 CHECK(slow.Stop()==HttpGameStateStopResult::DrainTimedOut);CHECK(slow.RequiresRetention());{std::lock_guard lock(gateMutex);released=true;}gateCondition.notify_all();CHECK(pending.wait_for(3s)==std::future_status::ready);CHECK(Body(pending.get(),503)["status"]==503);FixtureAccountLookupHook={};CHECK(slow.Stop()==HttpGameStateStopResult::Stopped);CHECK(!slow.RequiresRetention());}
 std::cout<<"PASS listener retention: real blocked worker causes drain timeout, retained owner safely drains after release\n";
 std::cout<<"PASS real HTTP: all 9 routes, both equipment options, payloads above 64KiB, auth/origin/method/preflight, retired generation, start/stop\n";

 CancelGameStateRequests("isolate WorldScript port fixture");
 for(int32 invalidPort:std::vector<int32>{-1,0,65536,65536+FreePort()}) {
 int validPort=FreePort();Config.options={{"GameStateAPI.Enable",true},{"GameStateAPI.Host",std::string("127.0.0.1")},{"GameStateAPI.Port",invalidPort},{"GameStateAPI.AllowedOrigin",std::string("HTTP://Consumer.Example.Test:80")},{"GameStateAPI.AuthToken",Token}};
 GameStateAPI invalid;invalid.OnAfterConfigLoad(false);invalid.OnStartup();auto rejected=QueueGameStateRequest(1,GameStateRequestType::Health);CHECK(rejected.Future.wait_for(0s)==std::future_status::ready);CHECK(rejected.Future.get().Status==503);
 Config.options["GameStateAPI.Port"]=validPort;invalid.OnAfterConfigLoad(true);auto pump=[&]{invalid.OnUpdate(1);};CHECK(Body(Request(validPort,"/api/health",Token,"GET","http://CONSUMER.example.test",pump))["status"]=="ok");
 Config.options["GameStateAPI.Port"]=invalidPort;invalid.OnAfterConfigLoad(true);CHECK(!Request(validPort,"/api/health",Token,"GET","",pump));rejected=QueueGameStateRequest(1,GameStateRequestType::Health);CHECK(rejected.Future.wait_for(0s)==std::future_status::ready);CHECK(rejected.Future.get().Status==503);
 Config.options["GameStateAPI.Port"]=validPort;invalid.OnAfterConfigLoad(true);CHECK(Body(Request(validPort,"/api/health",Token,"GET","http://consumer.example.test:80",pump))["status"]=="ok");invalid.OnShutdown();}
 std::cout<<"PASS signed port startup/reload rejection and recovery: -1,0,65536 and wrapping validport; configured origin normalization\n";

 // The WorldScript retains the old owner and refuses replacement after slow drain.
 {int retentionPort=FreePort();Config.options={{"GameStateAPI.Enable",true},{"GameStateAPI.Host",std::string("127.0.0.1")},{"GameStateAPI.Port",int32(retentionPort)},{"GameStateAPI.AllowedOrigin",std::string("http://127.0.0.1")},{"GameStateAPI.AuthToken",Token}};
 GameStateAPI retained;retained.OnAfterConfigLoad(false);retained.OnStartup();entered=false;released=false;FixtureAccountLookupHook=gate;
 auto pending=std::async(std::launch::async,[&]{httplib::Client client("127.0.0.1",retentionPort);client.set_read_timeout(5,0);return client.Get("/api/player/Fixture",{{"Authorization","Bearer "+Token}});});until=std::chrono::steady_clock::now()+3s;
 while(std::chrono::steady_clock::now()<until){retained.OnUpdate(1);{std::lock_guard lock(gateMutex);if(entered)break;}std::this_thread::sleep_for(1ms);}{std::lock_guard lock(gateMutex);CHECK(entered);}
 Config.options["GameStateAPI.AuthToken"]=NewToken;retained.OnAfterConfigLoad(true);CHECK(!IsGameStateGenerationAccepted(1));int replacementPort=FreePort();Config.options["GameStateAPI.Port"]=int32(replacementPort);retained.OnAfterConfigLoad(true);auto pump=[&]{retained.OnUpdate(1);};CHECK(!Request(replacementPort,"/api/health",NewToken,"GET","",pump));auto unavailable=QueueGameStateRequest(1,GameStateRequestType::Health);CHECK(unavailable.Future.wait_for(0s)==std::future_status::ready);CHECK(unavailable.Future.get().Status==503);
 {std::lock_guard lock(gateMutex);released=true;}gateCondition.notify_all();CHECK(pending.wait_for(3s)==std::future_status::ready);CHECK(Body(pending.get(),503)["status"]==503);FixtureAccountLookupHook={};retained.OnShutdown();}
 std::cout<<"PASS owner retention: rekey slow-drain timeout disables generation, refuses replacement listener, releases and safely cleans owner\n";
 // Reload exercises the production WorldScript owner, with a competing real listener.
 port=FreePort();Config.options={{"GameStateAPI.Enable",true},{"GameStateAPI.Host",std::string("127.0.0.1")},{"GameStateAPI.Port",int32(port)},{"GameStateAPI.AllowedOrigin",std::string("http://127.0.0.1")},{"GameStateAPI.AuthToken",Token}};
 int occupied=socket(AF_INET,SOCK_STREAM,0);CHECK(occupied>=0);sockaddr_in bound{};bound.sin_family=AF_INET;bound.sin_addr.s_addr=htonl(INADDR_LOOPBACK);bound.sin_port=htons(port);CHECK(bind(occupied,reinterpret_cast<sockaddr*>(&bound),sizeof(bound))==0);CHECK(listen(occupied,8)==0);
 {GameStateAPI api;api.OnAfterConfigLoad(false);api.OnStartup();auto denied=QueueGameStateRequest(1,GameStateRequestType::Health);CHECK(denied.Future.wait_for(0s)==std::future_status::ready);CHECK(denied.Future.get().Status==503);close(occupied);api.OnAfterConfigLoad(true);
 auto pump=[&]{api.OnUpdate(1);};CHECK(Body(Request(port,"/api/health",Token,"GET","",pump))["status"]=="ok");
 Config.options["GameStateAPI.AuthToken"]=NewToken;api.OnAfterConfigLoad(true);CHECK(Body(Request(port,"/api/health",Token,"GET","",pump),401)["status"]==401);CHECK(Body(Request(port,"/api/health",NewToken,"GET","",pump))["status"]=="ok");
 Config.options["GameStateAPI.AllowedOrigin"]=std::string("*");api.OnAfterConfigLoad(true);auto wildcard=Request(port,"/api/health",NewToken,"GET","https://consumer.example.test",pump);CHECK(Body(wildcard)["status"]=="ok");CHECK(wildcard->get_header_value("Access-Control-Allow-Origin")=="*");int oldPort=port;port=FreePort();Config.options["GameStateAPI.Port"]=int32(port);api.OnAfterConfigLoad(true);CHECK(!Request(oldPort,"/api/health",NewToken,"GET","",pump));CHECK(Body(Request(port,"/api/health",NewToken,"GET","",pump))["status"]=="ok");
 Config.options["GameStateAPI.Enable"]=false;api.OnAfterConfigLoad(true);CHECK(!Request(port,"/api/health",NewToken,"GET","",pump));Config.options["GameStateAPI.Enable"]=true;Config.options["GameStateAPI.AuthToken"]=std::string("invalid");api.OnAfterConfigLoad(true);CHECK(!Request(port,"/api/health",NewToken,"GET","",pump));Config.options["GameStateAPI.AuthToken"]=NewToken;api.OnAfterConfigLoad(true);CHECK(Body(Request(port,"/api/health",NewToken,"GET","",pump))["status"]=="ok");api.OnShutdown();CHECK(!Request(port,"/api/health",NewToken,"GET","",pump));}
 CHECK(FixturePlayerReads>1000);std::cout<<"PASS lifecycle: failed bind, unchanged retry, rekey, disable, invalid reload, recovery, shutdown; reads confined to fixture world thread\n";
 std::cout<<"PASS "<<checks.load()<<" assertions; production serializers/queue/HTTP compiled; data interfaces stubbed, no realm or client mutated\n";
 return 0;
}
