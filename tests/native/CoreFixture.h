#pragma once
// Data-only substitutes for core interfaces; API serialization/queue/HTTP remain production code.
#include <algorithm>
#include <atomic>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <map>
#include <functional>
#include <set>
#include <shared_mutex>
#include <string>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>
using uint8=std::uint8_t; using uint16=std::uint16_t; using uint32=std::uint32_t; using uint64=std::uint64_t; using int16=std::int16_t; using int32=std::int32_t;
using QuestStatus=uint32;
using Seconds=std::chrono::seconds;
enum Powers { POWER_MANA,POWER_RAGE,POWER_FOCUS,POWER_ENERGY,POWER_HAPPINESS,POWER_RUNE,POWER_RUNIC_POWER };
enum { EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_NECK,EQUIPMENT_SLOT_SHOULDERS,EQUIPMENT_SLOT_BODY,EQUIPMENT_SLOT_CHEST,EQUIPMENT_SLOT_WAIST,EQUIPMENT_SLOT_LEGS,EQUIPMENT_SLOT_FEET,EQUIPMENT_SLOT_WRISTS,EQUIPMENT_SLOT_HANDS,EQUIPMENT_SLOT_FINGER1,EQUIPMENT_SLOT_FINGER2,EQUIPMENT_SLOT_TRINKET1,EQUIPMENT_SLOT_TRINKET2,EQUIPMENT_SLOT_BACK,EQUIPMENT_SLOT_MAINHAND,EQUIPMENT_SLOT_OFFHAND,EQUIPMENT_SLOT_RANGED,EQUIPMENT_SLOT_TABARD };
enum { PLAYER_SLOT_END=150, BUYBACK_SLOT_START=74, BUYBACK_SLOT_END=86, INVENTORY_SLOT_BAG_START=19, INVENTORY_SLOT_BAG_END=23, BANK_SLOT_BAG_START=67, BANK_SLOT_BAG_END=74, MAX_BAG_SIZE=36 };
enum { INVENTORY_SLOT_BAG_0=255, ITEM_FIELD_DURABILITY=1,ITEM_FIELD_MAXDURABILITY=2,ITEM_CLASS_WEAPON=2,MAX_ITEM_PROTO_STATS=10,MAX_ITEM_PROTO_DAMAGES=2,MAX_ITEM_PROTO_SOCKETS=3,MAX_ITEM_PROTO_SPELLS=5 };
enum { STAT_STRENGTH,STAT_AGILITY,STAT_STAMINA,STAT_INTELLECT,STAT_SPIRIT,BASE_ATTACK=0,RANGED_ATTACK=2 };
enum { PLAYER_XP=10,PLAYER_NEXT_LEVEL_XP=11,PLAYER_CRIT_PERCENTAGE=12,PLAYER_RANGED_CRIT_PERCENTAGE=13,PLAYER_SPELL_CRIT_PERCENTAGE1=14,PLAYER_FIELD_MOD_TARGET_PHYSICAL_RESISTANCE=15,PLAYER_FIELD_MOD_TARGET_RESISTANCE=16,PLAYER_FLAGS=17,PLAYER_FLAGS_RESTING=1,PLAYER_FLAGS_GHOST=2,PLAYER_FLAGS_PVP_TIMER=4,PLAYER_MAX_SKILLS=128,PLAYER_SKILL_INFO_1_1=100 };
inline constexpr uint32 PLAYER_FIELD_MOD_HEALING_DONE_POS=500,PLAYER_FIELD_MOD_DAMAGE_DONE_POS=600;
enum { SPELL_SCHOOL_NORMAL,SPELL_SCHOOL_HOLY,SPELL_SCHOOL_FIRE,SPELL_SCHOOL_NATURE,SPELL_SCHOOL_FROST,SPELL_SCHOOL_SHADOW,SPELL_SCHOOL_ARCANE };
enum { PLAYERSPELL_UNCHANGED,PLAYERSPELL_REMOVED,QUEST_STATUS_NONE=0,QUEST_STATUS_COMPLETE=1,QUEST_STATUS_INCOMPLETE=3,QUEST_STATUS_REWARDED=6,QUEST_ITEM_OBJECTIVES_COUNT=6,QUEST_OBJECTIVES_COUNT=4 };
#define SKILL_VALUE(v) ((v)&0xffff)
#define SKILL_MAX(v) ((v)>>16)
struct ObjectGuid { uint64 value=77; uint32 GetCounter() const {return value;} uint64 GetRawValue() const {return value;} };
struct ItemTemplate {
 std::string Name1="Fixture sword"; uint32 Quality=4,ItemLevel=80,RequiredLevel=70,Class=ITEM_CLASS_WEAPON,SubClass=7,InventoryType=13,StatsCount=1;
 struct {uint32 ItemStatType=4; int32 ItemStatValue=12;} ItemStat[MAX_ITEM_PROTO_STATS];
 uint32 Armor=50,HolyRes=1,FireRes=2,NatureRes=3,FrostRes=4,ShadowRes=5,ArcaneRes=6,Delay=2000;
 struct {float DamageMin=10,DamageMax=20;uint32 DamageType=0;} Damage[MAX_ITEM_PROTO_DAMAGES];
 struct {uint32 Color=0,Content=0;} Socket[MAX_ITEM_PROTO_SOCKETS];
 struct {int32 SpellId=0,SpellTrigger=0,SpellCharges=0,SpellCooldown=0;} Spells[MAX_ITEM_PROTO_SPELLS];
 uint32 socketBonus=100,ItemSet=7,Bonding=1,SellPrice=200,BuyPrice=1000;
 float getDPS() const {return 7.5f;} uint32 GetMaxStackSize() const {return 1;}
};
struct Bag;
struct Item {
 ItemTemplate* proto=nullptr; uint32 entry=12345,count=1,durability=35;
 ObjectGuid guid{1},owner{77}; Bag* container=nullptr; uint8 slot=0; uint32 suffix=0; int32 property=0;
 ItemTemplate const* GetTemplate() const {return proto;} uint32 GetEntry()const{return entry;} uint32 GetCount()const{return count;}
 uint32 GetUInt32Value(uint32 field)const{return field==ITEM_FIELD_DURABILITY?durability:50;}
 ObjectGuid GetGUID()const{return guid;} ObjectGuid GetOwnerGUID()const{return owner;} Bag* GetContainer()const{return container;}
 uint8 GetSlot()const{return slot;} uint32 GetItemSuffixFactor()const{return suffix;} int32 GetItemRandomPropertyId()const{return property;}
};
struct Bag : Item { uint32 capacity=MAX_BAG_SIZE; std::array<Item*,MAX_BAG_SIZE> contents{}; uint32 GetBagSize()const{return capacity;} Item* GetItemByPos(uint8 pos)const{return contents.at(pos);} };
inline uint32 GenerateEnchSuffixFactor(uint32 entry) {return entry+100;}

struct PlayerTalent {uint8 State=PLAYERSPELL_UNCHANGED,specMask=3;uint32 talentID=900;bool inSpellBook=true;};
using PlayerTalentMap=std::map<uint32,PlayerTalent*>;
struct TalentSpellPos {uint16 talent_id=900;uint8 rank=1;};
inline std::map<uint32,TalentSpellPos> TalentPositions;
inline TalentSpellPos const* GetTalentSpellPos(uint32 id){auto i=TalentPositions.find(id);return i==TalentPositions.end()?nullptr:&i->second;}
struct PlayerSpell {uint8 State=PLAYERSPELL_UNCHANGED;bool Active=true;uint8 specMask=3;};
using PlayerSpellMap=std::map<uint32,PlayerSpell*>;
struct QuestStatusData {uint32 Status=QUEST_STATUS_INCOMPLETE; uint16 ItemCount[6]={2};uint16 CreatureOrGOCount[4]={1};bool Explored=true;uint32 Timer=42;};
using QuestStatusMap=std::map<uint32,QuestStatusData>;
struct Quest {
 uint32 RequiredItemId[6]={55},RequiredItemCount[6]={5}; int32 RequiredNpcOrGo[4]={-66};uint32 RequiredNpcOrGoCount[4]={3};
 std::string title="Fixture quest",details="Detailed quest description";
 std::string const& GetTitle()const{return title;}std::string const& GetDetails()const{return details;}
 int32 GetQuestLevel()const{return 12;}uint32 GetMinLevel()const{return 10;}uint32 GetType()const{return 1;}uint32 GetSuggestedPlayers()const{return 2;}uint32 GetTimeAllowed()const{return 600;}bool IsDaily()const{return false;}bool IsWeekly()const{return true;}bool IsRepeatable()const{return false;}
};
struct Guild {std::string GetName()const{return "Fixture guild";}};
struct GuildMgr {Guild guild; Guild* GetGuildById(uint32 id){return id?&guild:nullptr;}};
inline GuildMgr Guilds;inline GuildMgr* sGuildMgr=&Guilds;
struct Group {ObjectGuid GetGUID()const{return {88};}ObjectGuid GetLeaderGUID()const{return {77};}uint32 GetMembersCount()const{return 5;}bool IsLeader(ObjectGuid g)const{return g.value==77;}bool IsAssistant(ObjectGuid)const{return false;}uint32 GetLootMethod()const{return 2;}bool isRaidGroup()const{return false;}bool isBGGroup()const{return false;}bool isLFGGroup()const{return true;}};
struct WorldSession;
inline std::thread::id FixtureWorldThread;
inline bool FixtureCheckThread=false;
inline std::size_t FixturePlayerReads=0;
inline std::function<void()> FixtureReadHook;
inline void PlayerRead() {if(FixtureCheckThread)assert(std::this_thread::get_id()==FixtureWorldThread);++FixturePlayerReads;if(FixtureReadHook)FixtureReadHook();}
struct Player {
 std::string name="Fixture";bool online=true,throwOnRead=false,pvp=true,pvpTimer=false;uint32 guildId=1;Group* group=nullptr;WorldSession* session=nullptr;
 ObjectGuid guid{77}; std::array<Item*,PLAYER_SLOT_END> equipment{}; std::map<uint8,Bag*> bags; PlayerSpellMap spells;PlayerTalentMap talents;float m_modMeleeHitChance=2.25f,m_modRangedHitChance=3.5f,m_modSpellHitChance=4.75f;QuestStatusMap statuses;std::set<uint32> rewarded;
 std::map<uint32,uint32> values;std::map<uint32,float> floatValues;uint8 activeSpec=0,specsCount=2;uint32 freeTalentPoints=4,totalTalentPoints=20;
 void check()const {PlayerRead();if(throwOnRead)throw std::runtime_error("fixture read failure");}
 std::string GetName()const{check();return name;}bool IsInWorld()const{check();return online;}
 uint32 GetLevel()const{check();return 19;}uint32 getClass()const{return 1;}uint32 getRace()const{return 1;}uint32 getGender()const{return 0;}
 ObjectGuid GetGUID()const{check();return guid;}uint32 GetZoneId()const{return 12;}uint32 GetAreaId()const{return 34;}uint32 GetMapId()const{return 0;}
 Item* GetItemByPos(uint8,uint8 slot){check();return equipment.at(slot);}
 Bag* GetBagByPos(uint8 slot){check();auto i=bags.find(slot);return i==bags.end()?nullptr:i->second;}
 uint32 GetHealth()const{return 900;}uint32 GetMaxHealth()const{return 1000;}Powers getPowerType()const{return POWER_MANA;}uint32 GetPower(Powers p)const{return 10+p;}uint32 GetMaxPower(Powers p)const{return 100+p;}
 bool IsAlive()const{return true;}bool IsInCombat()const{return false;}bool HasPlayerFlag(uint32 f)const{return f==PLAYER_FLAGS_RESTING;}bool IsPvP()const{return pvp;}bool HasFlag(uint32,uint32 f)const{return f==PLAYER_FLAGS_PVP_TIMER&&pvpTimer;}bool isAFK()const{return false;}bool isDND()const{return true;}bool IsGameMaster()const{return false;}
 uint32 GetUInt32Value(uint32 field)const {check();auto it=values.find(field);return it==values.end()?0:it->second;}
 int32 GetInt32Value(uint32 field)const{return static_cast<int32>(GetUInt32Value(field));}
 float GetFloatValue(uint32 field)const{auto i=floatValues.find(field);return i==floatValues.end()?field/10.0f:i->second;}float GetStat(uint32 stat)const{return 20.0f+stat;}float GetTotalAttackPowerValue(uint32 attack)const{return 200.0f+attack;}uint32 GetBaseSpellPowerBonus()const{return 150;}uint32 GetArmor()const{return 500;}uint32 GetResistance(uint32 school)const{return school*10;}float GetAverageItemLevel()const{return 79.5f;}
 WorldSession* GetSession()const{return session;}uint32 GetGuildId()const{return guildId;}uint32 GetRank()const{return 2;}Group* GetGroup()const{return group;}
 uint32 GetMoney()const{return 123456;}uint32 GetTotalPlayedTime()const{return 12000;}uint32 GetLevelPlayedTime()const{return 500;}uint32 GetHonorPoints()const{return 100;}uint32 GetArenaPoints()const{return 200;}
 float GetPositionX()const{return 1.25f;}float GetPositionY()const{return 2.5f;}float GetPositionZ()const{return 3.75f;}float GetOrientation()const{return 0.5f;}
 PlayerSpellMap const& GetSpellMap()const{check();return spells;}QuestStatusMap& getQuestStatusMap(){check();return statuses;}
 PlayerTalentMap const& GetTalentMap()const{check();return talents;}
 std::set<uint32> const& getRewardedQuests()const{return rewarded;}
 std::set<uint32> const& GetRewardedQuestSet()const{return rewarded;}std::size_t GetRewardedQuestCount()const{return rewarded.size();}
 uint16 GetSkillValue(uint16)const{return 75;}uint16 GetMaxSkillValue(uint16)const{return 150;}uint16 GetPureSkillValue(uint16)const{return 70;}int16 GetSkillPermBonusValue(uint16)const{return 3;}int16 GetSkillTempBonusValue(uint16)const{return 2;}
 uint8 GetActiveSpec()const{return activeSpec;}uint8 GetSpecsCount()const{return specsCount;}uint32 GetFreeTalentPoints()const{return freeTalentPoints;}uint32 CalculateTalentsPoints()const{return totalTalentPoints;}
};
namespace rbac {struct RBACData {std::string GetName()const{return "CachedFixtureAccount";}};}
struct WorldSession {Player* player=nullptr;rbac::RBACData* permissions=nullptr;rbac::RBACData const* GetRBACData()const{return permissions;}Player* GetPlayer()const{return player;}uint32 GetAccountId()const{return 1;}std::string GetPlayerName()const{return "FixtureAccount";}uint32 GetLatency()const{return 25;}uint32 GetSecurity()const{return 0;}};
struct WorldSessionMgr {
 std::map<uint32,WorldSession*> sessions;
 auto const& GetAllSessions()const{return sessions;}uint32 GetPlayerCount()const{uint32 count=0;for(auto const& [id,session]:sessions)if(session&&session->player&&session->player->online)++count;return count;}uint32 GetMaxPlayerCount()const{return 1000;}uint32 GetActiveSessionCount()const{return sessions.size();}uint32 GetQueuedSessionCount()const{return 2;}uint32 GetActiveAndQueuedSessionCount()const{return sessions.size()+2;}
};
inline WorldSessionMgr Sessions;inline WorldSessionMgr* sWorldSessionMgr=&Sessions;
enum ServerConfigs { CONFIG_LEAVE_GROUP_ON_LOGOUT };
struct World {
 bool leaveGroupOnLogout=false;
 bool getBoolConfig(ServerConfigs index)const {assert(index==CONFIG_LEAVE_GROUP_ON_LOGOUT);if(FixtureCheckThread)assert(std::this_thread::get_id()==FixtureWorldThread);return leaveGroupOnLogout;}
};
inline World FixtureWorld;inline World* sWorld=&FixtureWorld;
inline std::map<uint32,Player*> FixturePlayerRegistry;
inline std::shared_mutex FixturePlayerRegistryMutex;
template<class T>struct HashMapHolder {static std::shared_mutex* GetLock(){return &FixturePlayerRegistryMutex;}};
namespace ObjectAccessor {inline auto const& GetPlayers(){return FixturePlayerRegistry;}inline Player* FindPlayerByName(std::string const& name){std::shared_lock lock(FixturePlayerRegistryMutex);for(auto const& [id,player]:FixturePlayerRegistry)if(player&&player->name==name)return player;return nullptr;}}
namespace GameTime {inline Seconds GetUptime(){return Seconds(1234);}inline Seconds GetGameTime(){return Seconds(5678);}inline Seconds GetStartTime(){return Seconds(4444);}}
struct SpellCastTimesEntry {int32 CastTime=1500;};struct SpellRangeEntry {float RangeMax[2]={30,30};};
struct SpellInfo {bool passive=false;char const* SpellName[1]={"Fixture spell"};char const* Rank[1]={"Rank 2"};uint32 SchoolMask=4,RecoveryTime=1000;SpellCastTimesEntry* CastTimeEntry=nullptr;SpellRangeEntry* RangeEntry=nullptr;bool IsPassive()const{return passive;}};
struct SpellMgr {std::map<uint32,SpellInfo> spells;SpellInfo const* GetSpellInfo(uint32 id)const{auto i=spells.find(id);return i==spells.end()?nullptr:&i->second;}};
inline SpellMgr Spells;inline SpellMgr* sSpellMgr=&Spells;
struct SkillLineEntry {char const* name[1]={"Fixture skill"};};
struct SkillLineStore {SkillLineEntry skill;SkillLineEntry const* LookupEntry(uint32)const{return &skill;}};
inline SkillLineStore sSkillLineStore;
struct ObjectMgr {std::map<uint32,Quest> quests;Quest const* GetQuestTemplate(uint32 id)const{auto i=quests.find(id);return i==quests.end()?nullptr:&i->second;}};
inline ObjectMgr Quests;inline ObjectMgr* sObjectMgr=&Quests;
struct ConfigMgr {std::map<std::string,std::variant<bool,int32,std::string>> options;template<class T>T GetOption(std::string const& key,T fallback){auto i=options.find(key);return i==options.end()?fallback:std::get<T>(i->second);}};
inline ConfigMgr Config;inline ConfigMgr* sConfigMgr=&Config;
struct WorldScript {explicit WorldScript(char const*){}virtual ~WorldScript()=default;virtual void OnAfterConfigLoad(bool){}virtual void OnStartup(){}virtual void OnShutdown(){}virtual void OnUpdate(uint32){}};
#define LOG_ERROR(...) do{}while(false)
#define LOG_INFO(...) do{}while(false)
#define LOG_FATAL(...) do{}while(false)

inline std::atomic<unsigned> FixtureAccountLookups{0};
inline std::function<void()> FixtureAccountLookupHook;
namespace AccountMgr {inline bool GetName(uint32,std::string& name){assert(std::this_thread::get_id()!=FixtureWorldThread);++FixtureAccountLookups;if(FixtureAccountLookupHook)FixtureAccountLookupHook();name="FixtureAccount";return true;}}
