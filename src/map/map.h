// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _MAP_H_
#define _MAP_H_

#include "../common/cbasetypes.h"
#include "../common/core.h" // CORE_ST_LAST
#include "../common/mmo.h"
#include "../common/mapindex.h"
#include "../common/db.h"
#include "../common/msg_conf.h"

#include <stdarg.h>

struct npc_data;
struct item_data;

/// Uncomment to enable the Cell Stack Limit mod.
/// It's only config is the battle_config custom_cell_stack_limit.
/// Only chars affected are those defined in BL_CHAR
//#define CELL_NOSTACK

/// Uncomment to enable circular area checks.
/// By default, most server-sided range checks in Aegis are of square shapes, so a monster
/// with a range of 4 can attack anything within a 9x9 area.
/// Client-sided range checks are, however, are always circular.
/// Enabling this changes all checks to circular checks, which is more realistic,
/// but is not the official behaviour.
//#define CIRCULAR_AREA

#define msg_config_read(cfgName,isnew) map_msg_config_read(cfgName,isnew)
#define msg_txt(sd,msg_number) map_msg_txt(sd,msg_number)
#define do_final_msg() map_do_final_msg()
int map_msg_config_read(char *cfgName, int lang);
const char* map_msg_txt(struct map_session_data *sd, int msg_number);
void map_do_final_msg(void);
void map_msg_reload(void);

#define MAX_NPC_PER_MAP 512
#define BLOCK_SIZE 8
#define AREA_SIZE battle_config.area_size
#define DAMAGELOG_SIZE 30
#define LOOTITEM_SIZE 10
#define MAX_MOBSKILL 50
#define MAX_MOB_LIST_PER_MAP 128
#define MAX_EVENTQUEUE 5
#define MAX_EVENTTIMER 32
#define NATURAL_HEAL_INTERVAL 500
#define MIN_FLOORITEM 2
#define MAX_FLOORITEM START_ACCOUNT_NUM
#define MAX_LEVEL 200
#define MAX_DROP_PER_MAP 48
#define MAX_IGNORE_LIST 20 // official is 14
#define MAX_VENDING 12
#define MAX_MAP_SIZE 512*512 // Wasn't there something like this already? Can't find it.. [Shinryo]

//The following system marks a different job ID system used by the map server,
//which makes a lot more sense than the normal one. [Skotlex]
//
//These marks the "level" of the job.
#define JOBL_2_1 0x100 //256
#define JOBL_2_2 0x200 //512
#define JOBL_2 0x300

#define JOBL_UPPER 0x1000 //4096
#define JOBL_BABY 0x2000  //8192
#define JOBL_THIRD 0x4000 //16384

//for filtering and quick checking.
#define MAPID_BASEMASK 0x00ff //Checking 1st Jobs.
#define MAPID_UPPERMASK 0x0fff //Checking 2nd Jobs. Not related to JOBL_UPPER.
#define MAPID_THIRDMASK 0x4fff //Checking 3rd Jobs.

// 極力 staticでロ?カルに?める
static DBMap* id_db=NULL; // int id -> struct block_list*
static DBMap* pc_db=NULL; // int id -> struct map_session_data*
static DBMap* mobid_db=NULL; // int id -> struct mob_data*
static DBMap* bossid_db=NULL; // int id -> struct mob_data* (MVP db)
static DBMap* map_db=NULL; // unsigned int mapindex -> struct map_data*
static DBMap* nick_db=NULL; // uint32 char_id -> struct charid2nick* (requested names of offline characters)
static DBMap* charid_db=NULL; // uint32 char_id -> struct map_session_data*
static DBMap* regen_db=NULL; // int id -> struct block_list* (status_natural_heal processing)
static DBMap* map_msg_db = NULL;

//First Jobs
//Note the oddity of the novice:
//Super Novices are considered the 2-1 version of the novice! Novices are considered a first class type, too...
enum {
	//Novice And 1-1 Jobs
	MAPID_NOVICE = 0x0,
	MAPID_SWORDMAN,
	MAPID_MAGE,
	MAPID_ARCHER,
	MAPID_ACOLYTE,
	MAPID_MERCHANT,
	MAPID_THIEF,
	MAPID_TAEKWON,
	MAPID_WEDDING,
	MAPID_GUNSLINGER,
	MAPID_NINJA,
	MAPID_XMAS,
	MAPID_SUMMER,
	MAPID_GANGSI,
	MAPID_HANBOK,
	MAPID_OKTOBERFEST,
	MAPID_SUMMER2,
	MAPID_SUMMONER,
//2-1 Jobs
	MAPID_SUPER_NOVICE = JOBL_2_1 | 0x0,
	MAPID_KNIGHT,
	MAPID_WIZARD,
	MAPID_HUNTER,
	MAPID_PRIEST,
	MAPID_BLACKSMITH,
	MAPID_ASSASSIN,
	MAPID_STAR_GLADIATOR,
	MAPID_REBELLION = JOBL_2_1 | 0x9,
	MAPID_KAGEROUOBORO,
	MAPID_DEATH_KNIGHT = JOBL_2_1 | 0xD,
//2-2 Jobs
	MAPID_CRUSADER = JOBL_2_2|0x1,
	MAPID_SAGE,
	MAPID_BARDDANCER,
	MAPID_MONK,
	MAPID_ALCHEMIST,
	MAPID_ROGUE,
	MAPID_SOUL_LINKER,
	MAPID_DARK_COLLECTOR = JOBL_2_2 | 0xD,
//Trans Novice And Trans 1-1 Jobs
	MAPID_NOVICE_HIGH = JOBL_UPPER|0x0,
	MAPID_SWORDMAN_HIGH,
	MAPID_MAGE_HIGH,
	MAPID_ARCHER_HIGH,
	MAPID_ACOLYTE_HIGH,
	MAPID_MERCHANT_HIGH,
	MAPID_THIEF_HIGH,
//Trans 2-1 Jobs
	MAPID_LORD_KNIGHT = JOBL_UPPER|JOBL_2_1|0x1,
	MAPID_HIGH_WIZARD,
	MAPID_SNIPER,
	MAPID_HIGH_PRIEST,
	MAPID_WHITESMITH,
	MAPID_ASSASSIN_CROSS,
//Trans 2-2 Jobs
	MAPID_PALADIN = JOBL_UPPER|JOBL_2_2|0x1,
	MAPID_PROFESSOR,
	MAPID_CLOWNGYPSY,
	MAPID_CHAMPION,
	MAPID_CREATOR,
	MAPID_STALKER,
//Baby Novice And Baby 1-1 Jobs
	MAPID_BABY = JOBL_BABY|0x0,
	MAPID_BABY_SWORDMAN,
	MAPID_BABY_MAGE,
	MAPID_BABY_ARCHER,
	MAPID_BABY_ACOLYTE,
	MAPID_BABY_MERCHANT,
	MAPID_BABY_THIEF,
	MAPID_BABY_TAEKWON,
	MAPID_BABY_GUNSLINGER = JOBL_BABY | 0x9,
	MAPID_BABY_NINJA,
	MAPID_BABY_SUMMONER = JOBL_BABY | 0x11,
//Baby 2-1 Jobs
	MAPID_SUPER_BABY = JOBL_BABY | JOBL_2_1 | 0x0,
	MAPID_BABY_KNIGHT,
	MAPID_BABY_WIZARD,
	MAPID_BABY_HUNTER,
	MAPID_BABY_PRIEST,
	MAPID_BABY_BLACKSMITH,
	MAPID_BABY_ASSASSIN,
	MAPID_BABY_STAR_GLADIATOR,
	MAPID_BABY_REBELLION = JOBL_BABY | JOBL_2_1 | 0x9,
	MAPID_BABY_KAGEROUOBORO,
//Baby 2-2 Jobs
	MAPID_BABY_CRUSADER = JOBL_BABY|JOBL_2_2|0x1,
	MAPID_BABY_SAGE,
	MAPID_BABY_BARDDANCER,
	MAPID_BABY_MONK,
	MAPID_BABY_ALCHEMIST,
	MAPID_BABY_ROGUE,
	MAPID_BABY_SOUL_LINKER,
//3-1 Jobs
	MAPID_SUPER_NOVICE_E = JOBL_THIRD | JOBL_2_1 | 0x0,
	MAPID_RUNE_KNIGHT,
	MAPID_WARLOCK,
	MAPID_RANGER,
	MAPID_ARCH_BISHOP,
	MAPID_MECHANIC,
	MAPID_GUILLOTINE_CROSS,
	MAPID_STAR_EMPEROR,
//3-2 Jobs
	MAPID_ROYAL_GUARD = JOBL_THIRD|JOBL_2_2|0x1,
	MAPID_SORCERER,
	MAPID_MINSTRELWANDERER,
	MAPID_SURA,
	MAPID_GENETIC,
	MAPID_SHADOW_CHASER,
	MAPID_SOUL_REAPER,
//Trans 3-1 Jobs
	MAPID_RUNE_KNIGHT_T = JOBL_THIRD|JOBL_UPPER|JOBL_2_1|0x1,
	MAPID_WARLOCK_T,
	MAPID_RANGER_T,
	MAPID_ARCH_BISHOP_T,
	MAPID_MECHANIC_T,
	MAPID_GUILLOTINE_CROSS_T,
//Trans 3-2 Jobs
	MAPID_ROYAL_GUARD_T = JOBL_THIRD|JOBL_UPPER|JOBL_2_2|0x1,
	MAPID_SORCERER_T,
	MAPID_MINSTRELWANDERER_T,
	MAPID_SURA_T,
	MAPID_GENETIC_T,
	MAPID_SHADOW_CHASER_T,
//Baby 3-1 Jobs
	MAPID_SUPER_BABY_E = JOBL_THIRD | JOBL_BABY | JOBL_2_1 | 0x0,
	MAPID_BABY_RUNE_KNIGHT,
	MAPID_BABY_WARLOCK,
	MAPID_BABY_RANGER,
	MAPID_BABY_ARCH_BISHOP,
	MAPID_BABY_MECHANIC,
	MAPID_BABY_GUILLOTINE_CROSS,
	MAPID_BABY_STAR_EMPEROR,
//Baby 3-2 Jobs
	MAPID_BABY_ROYAL_GUARD = JOBL_THIRD|JOBL_BABY|JOBL_2_2|0x1,
	MAPID_BABY_SORCERER,
	MAPID_BABY_MINSTRELWANDERER,
	MAPID_BABY_SURA,
	MAPID_BABY_GENETIC,
	MAPID_BABY_SHADOW_CHASER,
	MAPID_BABY_SOUL_REAPER,
};

//Max size for inputs to Graffiti, Talkie Box and Vending text prompts
#define MESSAGE_SIZE (79 + 1)
//String length you can write in the 'talking box'
#define CHATBOX_SIZE (70 + 1)
//Chatroom-related string sizes
#define CHATROOM_TITLE_SIZE (36 + 1)
#define CHATROOM_PASS_SIZE (8 + 1)
//Max allowed chat text length
#define CHAT_SIZE_MAX (255 + 1)
// <NPC_NAME_LENGTH> for npc name + 2 for a "::" + <NAME_LENGTH> for label + 1 for EOS
#define EVENT_NAME_LENGTH ( NPC_NAME_LENGTH + 2 + NAME_LENGTH + 1 )

#define DEFAULT_AUTOSAVE_INTERVAL 5*60*1000

/// Specifies maps where players may hit each other
#define map_flag_vs(m) (map[m].flag.pvp || map[m].flag.gvg_dungeon || map[m].flag.gvg || ((agit_flag || agit2_flag) && map[m].flag.gvg_castle) || map[m].flag.gvg_te || (agit3_flag && map[m].flag.gvg_te_castle) || map[m].flag.battleground)
/// Versus map: PVP, BG, GVG, GVG Dungeons, and GVG Castles (regardless of agit_flag status)
#define map_flag_vs2(m) (map[m].flag.pvp || map[m].flag.gvg_dungeon || map[m].flag.gvg || map[m].flag.gvg_castle || map[m].flag.gvg_te || map[m].flag.gvg_te_castle || map[m].flag.battleground)
/// Specifies maps that have special GvG/WoE restrictions
#define map_flag_gvg(m) (map[m].flag.gvg || ((agit_flag || agit2_flag) && map[m].flag.gvg_castle) || map[m].flag.gvg_te || (agit3_flag && map[m].flag.gvg_te_castle))
/// Specifies if the map is tagged as GvG/WoE (regardless of agit_flag status)
#define map_flag_gvg2(m) (map[m].flag.gvg || map[m].flag.gvg_te || map[m].flag.gvg_castle || map[m].flag.gvg_te_castle)
/// No Kill Steal Protection
#define map_flag_ks(m) (map[m].flag.town || map[m].flag.pvp || map[m].flag.gvg || map[m].flag.gvg_te || map[m].flag.battleground)

/// WOE:TE Maps (regardless of agit_flag status) [Cydh]
#define map_flag_gvg2_te(m) (map[m].flag.gvg_te || map[m].flag.gvg_te_castle)
/// Check if map is GVG maps exclusion for item, skill, and status restriction check (regardless of agit_flag status) [Cydh]
#define map_flag_gvg2_no_te(m) (map[m].flag.gvg || map[m].flag.gvg_castle)

//This stackable implementation does not means a BL can be more than one type at a time, but it's 
//meant to make it easier to check for multiple types at a time on invocations such as map_foreach* calls [Skotlex]
enum bl_type { 
	BL_NUL   = 0x000,
	BL_PC    = 0x001,
	BL_MOB   = 0x002,
	BL_PET   = 0x004,
	BL_HOM   = 0x008,
	BL_MER   = 0x010,
	BL_ITEM  = 0x020,
	BL_SKILL = 0x040,
	BL_NPC   = 0x080,
	BL_CHAT  = 0x100,
	BL_ELEM  = 0x200,

	BL_ALL   = 0xFFF,
};

//For common mapforeach calls. Since pets cannot be affected, they aren't included here yet.
#define BL_CHAR (BL_PC|BL_MOB|BL_HOM|BL_MER|BL_ELEM)

enum npc_subtype { 
	NPCTYPE_WARP,
	NPCTYPE_SHOP,
	NPCTYPE_SCRIPT,
	NPCTYPE_CASHSHOP,
	NPCTYPE_TOMB,
	NPCTYPE_MARKETSHOP, 
	NPCTYPE_ITEMSHOP,
	NPCTYPE_POINTSHOP,
};

enum e_race {
	RC_NONE_ = -1,		//don't give us bonus
	RC_FORMLESS=0,		//NOTHING
	RC_UNDEAD,			//UNDEAD
	RC_BRUTE,			//ANIMAL
	RC_PLANT,			//PLANT
	RC_INSECT,			//INSECT
	RC_FISH,			//FISHS
	RC_DEMON,			//DEVIL
	RC_DEMIHUMAN,		//HUMAN
	RC_ANGEL,			//ANGEL
	RC_DRAGON,			//DRAGON
	RC_PLAYER,			//PLAYER
	RC_BOSS,			//LAST - Race ID 11 officially marked as LAST to mark the end of the race ID list.
	RC_NONBOSS,
	RC_ALL,
	RC_NONDEMIHUMAN,
	RC_MAX // auto upd enum for array size
};

/**
* Race type bitmasks.
*
* Used by several bonuses internally, to simplify handling of race combinations.
*/
enum RaceMask {
	RCMASK_NONE = 0,
	RCMASK_FORMLESS = 1 << RC_FORMLESS,
	RCMASK_UNDEAD = 1 << RC_UNDEAD,
	RCMASK_BRUTE = 1 << RC_BRUTE,
	RCMASK_PLANT = 1 << RC_PLANT,
	RCMASK_INSECT = 1 << RC_INSECT,
	RCMASK_FISH = 1 << RC_FISH,
	RCMASK_DEMON = 1 << RC_DEMON,
	RCMASK_DEMIHUMAN = 1 << RC_DEMIHUMAN,
	RCMASK_ANGEL = 1 << RC_ANGEL,
	RCMASK_DRAGON = 1 << RC_DRAGON,
	RCMASK_PLAYER = 1 << RC_PLAYER,
	RCMASK_BOSS = 1 << RC_BOSS,
	RCMASK_NONBOSS = 1 << RC_NONBOSS,
	RCMASK_NONDEMIPLAYER = RCMASK_FORMLESS | RCMASK_UNDEAD | RCMASK_BRUTE | RCMASK_PLANT | RCMASK_INSECT | RCMASK_FISH | RCMASK_DEMON | RCMASK_ANGEL | RCMASK_DRAGON,
	RCMASK_NONPLAYER = RCMASK_NONDEMIPLAYER | RCMASK_DEMIHUMAN,
	RCMASK_ALL = RCMASK_BOSS | RCMASK_NONBOSS,
};

enum e_classAE {
	CLASS_NONE = -1, //don't give us bonus
	CLASS_NORMAL = 0,
	CLASS_BOSS,
	CLASS_GUARDIAN,
	CLASS_BATTLEFIELD,
	CLASS_ALL,
	CLASS_MAX //auto upd enum for array len
};

enum e_race2 {
	RC2_NONE = 0,
	RC2_GOBLIN,
	RC2_KOBOLD,
	RC2_ORC,
	RC2_GOLEM,
	RC2_GUARDIAN,
	RC2_NINJA,
	RC2_SCARABA,
	RC2_GVG,
	RC2_BATTLEFIELD,
	RC2_TREASURE,
	RC2_BIOLAB,
	RC2_MAX
};

enum e_elemen {
	ELE_NONE = -1,
	ELE_NEUTRAL=0,	//NOTHING
	ELE_WATER,		//WATER
	ELE_EARTH,		//GROUND
	ELE_FIRE,		//FIRE
	ELE_WIND,		//WIND
	ELE_POISON,		//POISON
	ELE_HOLY,		//SAINT
	ELE_DARK,		//DARKNESS
	ELE_GHOST,		//TELEKINESIS
	ELE_UNDEAD,		//UNDEAD
	ELE_ALL,
	ELE_MAX
};

enum mob_ai {
	AI_NONE = 0,
	AI_ATTACK,
	AI_SPHERE,
	AI_FLORA,
	AI_ZANZOU,
	AI_LEGION,
	AI_FAW,
	AI_MAX
};

enum auto_trigger_flag {
	ATF_SELF=0x01,
	ATF_TARGET=0x02,
	ATF_SHORT=0x04,
	ATF_LONG=0x08,
	ATF_WEAPON=0x10,
	ATF_MAGIC=0x20,
	ATF_MISC=0x40,
};

struct block_list {
	struct block_list *next,*prev;
	int id;
	short m,x,y;
	enum bl_type type;
};


// Mob List Held in memory for Dynamic Mobs [Wizputer]
// Expanded to specify all mob-related spawn data by [Skotlex]
struct spawn_data {
	short class_; //Class, used because a mob can change it's class
	unsigned short m,x,y;	//Spawn information (map, point, spawn-area around point)
	signed short xs,ys;
	unsigned short num; //Number of mobs using this structure
	unsigned short active; //Number of mobs that are already spawned (for mob_remove_damaged: no)
	unsigned int delay1,delay2; //Spawn delay (fixed base + random variance)
	struct {
		unsigned int size :2; //Holds if mob has to be tiny/large
		enum mob_ai ai; //Special ai for summoned monsters.
		unsigned int dynamic :1; //Whether this data is indexed by a map's dynamic mob list
		unsigned int boss : 1;
	} state;
	char name[NAME_LENGTH],eventname[EVENT_NAME_LENGTH]; //Name/event
};




struct flooritem_data {
	struct block_list bl;
	unsigned char subx,suby;
	int cleartimer;
	int first_get_charid,second_get_charid,third_get_charid;
	int64 first_get_tick,second_get_tick,third_get_tick;
	struct item item_data;
	unsigned short mob_id; ///< ID of monster who dropped it. 0 for non-monster who dropped it.
};

enum _sp {
	SP_NONE = -1,
	SP_SPEED,SP_BASEEXP,SP_JOBEXP,SP_KARMA,SP_MANNER,SP_HP,SP_MAXHP,SP_SP,	// 0-7
	SP_MAXSP,SP_STATUSPOINT,SP_0a,SP_BASELEVEL,SP_SKILLPOINT,SP_STR,SP_AGI,SP_VIT,	// 8-15
	SP_INT,SP_DEX,SP_LUK,SP_CLASS,SP_ZENY,SP_SEX,SP_NEXTBASEEXP,SP_NEXTJOBEXP,	// 16-23
	SP_WEIGHT,SP_MAXWEIGHT,SP_1a,SP_1b,SP_1c,SP_1d,SP_1e,SP_1f,	// 24-31
	SP_USTR,SP_UAGI,SP_UVIT,SP_UINT,SP_UDEX,SP_ULUK,SP_26,SP_27,	// 32-39
	SP_28,SP_ATK1,SP_ATK2,SP_MATK1,SP_MATK2,SP_DEF1,SP_DEF2,SP_MDEF1,	// 40-47
	SP_MDEF2,SP_HIT,SP_FLEE1,SP_FLEE2,SP_CRITICAL,SP_ASPD,SP_36,SP_JOBLEVEL,	// 48-55
	SP_UPPER,SP_PARTNER,SP_CART,SP_FAME,SP_UNBREAKABLE,	//56-60
	SP_CARTINFO=99,	// 99

	SP_BASEJOB=119,	// 100+19 - celest
	SP_BASECLASS, SP_KILLERRID, SP_KILLEDRID, SP_BANK_VAULT, SP_ROULETTE_BRONZE, SP_ROULETTE_SILVER, // 120-125
	SP_ROULETTE_GOLD, SP_BASETHIRD, SP_CHARMOVE, SP_CASHPOINTS, SP_KAFRAPOINTS, // 126-130
	SP_PCDIECOUNTER, SP_COOKMASTERY, // 131-132

	// Mercenaries
	SP_MERCFLEE=165, SP_MERCKILLS=189, SP_MERCFAITH=190,
	
	// original 1000-
	SP_ATTACKRANGE=1000,	SP_ATKELE,SP_DEFELE,	// 1000-1002
	SP_CASTRATE, SP_MAXHPRATE, SP_MAXSPRATE, SP_SPRATE, // 1003-1006
	SP_ADDELE, SP_ADDRACE, SP_ADDSIZE, SP_SUBELE, SP_SUBRACE, // 1007-1011
	SP_ADDEFF, SP_RESEFF,	// 1012-1013
	SP_BASE_ATK,SP_ASPD_RATE,SP_HP_RECOV_RATE,SP_SP_RECOV_RATE,SP_SPEED_RATE, // 1014-1018
	SP_CRITICAL_DEF,SP_NEAR_ATK_DEF,SP_LONG_ATK_DEF, // 1019-1021
	SP_DOUBLE_RATE, SP_DOUBLE_ADD_RATE, SP_SKILL_HEAL, SP_MATK_RATE, // 1022-1025
	SP_IGNORE_DEF_ELE,SP_IGNORE_DEF_RACE, // 1026-1027
	SP_ATK_RATE,SP_SPEED_ADDRATE,SP_SP_REGEN_RATE, // 1028-1030
	SP_MAGIC_ATK_DEF,SP_MISC_ATK_DEF, // 1031-1032
	SP_IGNORE_MDEF_ELE,SP_IGNORE_MDEF_RACE, // 1033-1034
	SP_MAGIC_ADDELE,SP_MAGIC_ADDRACE,SP_MAGIC_ADDSIZE, // 1035-1037
	SP_PERFECT_HIT_RATE,SP_PERFECT_HIT_ADD_RATE,SP_CRITICAL_RATE,SP_GET_ZENY_NUM,SP_ADD_GET_ZENY_NUM, // 1038-1042
	SP_ADD_DAMAGE_CLASS,SP_ADD_MAGIC_DAMAGE_CLASS, SP_ADD_DEF_MONSTER, SP_ADD_MDEF_MONSTER, // 1043-1046
	SP_ADD_MONSTER_DROP_ITEM,SP_DEF_RATIO_ATK_ELE,SP_DEF_RATIO_ATK_RACE,SP_UNBREAKABLE_GARMENT, // 1047-1050
	SP_HIT_RATE,SP_FLEE_RATE,SP_FLEE2_RATE,SP_DEF_RATE,SP_DEF2_RATE,SP_MDEF_RATE,SP_MDEF2_RATE, // 1051-1057
	SP_SPLASH_RANGE,SP_SPLASH_ADD_RANGE,SP_AUTOSPELL,SP_HP_DRAIN_RATE,SP_SP_DRAIN_RATE, // 1058-1062
	SP_SHORT_WEAPON_DAMAGE_RETURN,SP_LONG_WEAPON_DAMAGE_RETURN,SP_WEAPON_COMA_ELE,SP_WEAPON_COMA_RACE, // 1063-1066
	SP_ADDEFF2,SP_BREAK_WEAPON_RATE,SP_BREAK_ARMOR_RATE,SP_ADD_STEAL_RATE, // 1067-1070
	SP_MAGIC_DAMAGE_RETURN,SP_RANDOM_ATTACK_INCREASE,SP_ALL_STATS,SP_AGI_VIT,SP_AGI_DEX_STR,SP_PERFECT_HIDE, // 1071-1076
	SP_NO_KNOCKBACK,SP_CLASSCHANGE, // 1077-1078
	SP_HP_DRAIN_VALUE,SP_SP_DRAIN_VALUE, // 1079-1080
	SP_WEAPON_ATK,SP_WEAPON_ATK_RATE, // 1081-1082
	SP_DELAYRATE,SP_HP_DRAIN_VALUE_RACE,SP_SP_DRAIN_VALUE_RACE, // 1083-1085
	SP_IGNORE_MDEF_RACE_RATE, SP_IGNORE_DEF_RACE_RATE,SP_SKILL_HEAL2,SP_ADDEFF_ONSKILL, //1086-1089
	SP_ADD_HEAL_RATE,SP_ADD_HEAL2_RATE, //1090-1091

	SP_RESTART_FULL_RECOVER=2000,SP_NO_CASTCANCEL,SP_NO_SIZEFIX,SP_NO_MAGIC_DAMAGE,SP_NO_WEAPON_DAMAGE,SP_NO_GEMSTONE, // 2000-2005
	SP_NO_CASTCANCEL2,SP_NO_MISC_DAMAGE,SP_UNBREAKABLE_WEAPON,SP_UNBREAKABLE_ARMOR, SP_UNBREAKABLE_HELM, // 2006-2010
	SP_UNBREAKABLE_SHIELD, SP_LONG_ATK_RATE, // 2011-2012

	SP_CRIT_ATK_RATE, SP_CRITICAL_ADDRACE, SP_NO_REGEN, SP_ADDEFF_WHENHIT, SP_AUTOSPELL_WHENHIT, // 2013-2017
	SP_SKILL_ATK, SP_UNSTRIPABLE, SP_AUTOSPELL_ONSKILL, // 2018-2020
	SP_SP_GAIN_VALUE, SP_HP_REGEN_RATE, SP_HP_LOSS_RATE, SP_ADDRACE2, SP_HP_GAIN_VALUE, // 2021-2025
	SP_SUBSIZE, SP_HP_DRAIN_VALUE_CLASS, SP_ADD_ITEM_HEAL_RATE, SP_SP_DRAIN_VALUE_CLASS, SP_EXP_ADDRACE,	// 2026-2030
	SP_SP_GAIN_RACE, SP_SUBRACE2, SP_UNBREAKABLE_SHOES,	// 2031-2033
	SP_UNSTRIPABLE_WEAPON,SP_UNSTRIPABLE_ARMOR,SP_UNSTRIPABLE_HELM,SP_UNSTRIPABLE_SHIELD,  // 2034-2037
	SP_INTRAVISION, SP_ADD_MONSTER_DROP_ITEMGROUP, SP_SP_LOSS_RATE, // 2038-2040
	SP_ADD_SKILL_BLOW, SP_SP_VANISH_RATE, SP_MAGIC_SP_GAIN_VALUE, SP_MAGIC_HP_GAIN_VALUE, SP_ADD_MONSTER_ID_DROP_ITEM, //2041-2045
	
	//15-3athena
	SP_COMA_CLASS = 2047, SP_COMA_RACE, //2047-2048
	SP_SKILL_COOLDOWN = 2050,	//2050
	SP_SKILL_USE_SP = 2055,		//2055
	SP_MAGIC_ATK_ELE, SP_FIXEDCAST,	//2056-2057
	SP_SET_DEF_RACE = 2059, SP_SET_MDEF_RACE,SP_HP_VANISH_RATE,  //2059-2061
	SP_IGNORE_DEF_CLASS = 2062, SP_DEF_RATIO_ATK_CLASS, SP_ADDCLASS, SP_SUBCLASS, SP_MAGIC_ADDCLASS, //2062-2066
	SP_WEAPON_COMA_CLASS, SP_IGNORE_MDEF_CLASS_RATE, //2067-2068
	SP_ADD_CLASS_DROP_ITEM, SP_ADD_CLASS_DROP_ITEMGROUP, //2070-2071
	SP_ADD_ITEMGROUP_HEAL_RATE = 2073, SP_HP_VANISH_RACE_RATE, SP_SP_VANISH_RACE_RATE, //2073-2075
	SP_SUBDEF_ELE = 2078, // 2078
	SP_MAGIC_ADDRACE2 = 2081, SP_IGNORE_MDEF_RACE2_RATE, // 2081-2082
	SP_DROP_ADDRACE = 2085, SP_DROP_ADDCLASS, // 2085-2086
	SP_FIXEDCASTRATE = 2101, SP_COOLDOWNRATE, SP_MATK, SP_NO_MADOFUEL //2101-2105

};

enum _look {
	LOOK_BASE,			//JOB
	LOOK_HAIR,			//HEAD
	LOOK_WEAPON,		//WEAPON
	LOOK_HEAD_BOTTOM,	//ACCESSORY
	LOOK_HEAD_TOP,		//ACCESSORY2
	LOOK_HEAD_MID,		//ACCESSORY3
	LOOK_HAIR_COLOR,	//HEADPALETTE
	LOOK_CLOTHES_COLOR,	//BODYPLAETTE
	LOOK_SHIELD,		//SHIELD
	LOOK_SHOES,			//SHOE
	LOOK_COSTUMEBODY,	//COSTUMEBODY - Used to be BODY and didn't do anything at the time of testing.
	LOOK_RESET_COSTUMES,//RESET_COSTUMES - Makes all headgear sprites on player vanish when activated.
	LOOK_ROBE,			//ROBE
	LOOK_BODY2,			//BODY2 - Changes body appearance.
};

// used by map_setcell()
typedef enum {
	CELL_WALKABLE,
	CELL_SHOOTABLE,
	CELL_WATER,

	CELL_NPC,
	CELL_BASILICA,
	CELL_LANDPROTECTOR,
	CELL_NOVENDING,
	CELL_NOCHAT,
	CELL_PVP,	// Cell PVP [Napster]
	CELL_MAELSTROM,
	CELL_ICEWALL,
} cell_t;

// used by map_getcell()
typedef enum {
	CELL_GETTYPE,		// retrieves a cell's 'gat' type

	CELL_CHKWALL,		// wall (gat type 1)
	CELL_CHKWATER,		// water (gat type 3)
	CELL_CHKCLIFF,		// cliff/gap (gat type 5)

	CELL_CHKPASS,		// passable cell (gat type non-1/5)
	CELL_CHKREACH,		// Same as PASS, but ignores the cell-stacking mod.
	CELL_CHKNOPASS,		// non-passable cell (gat types 1 and 5)
	CELL_CHKNOREACH,	// Same as NOPASS, but ignores the cell-stacking mod.
	CELL_CHKSTACK,		// whether cell is full (reached cell stacking limit) 

	CELL_CHKNPC,
	CELL_CHKBASILICA,
	CELL_CHKLANDPROTECTOR,
	CELL_CHKNOVENDING,
	CELL_CHKNOCHAT,		// Whether the cell denies Player Chat Window
	CELL_CHKPVP,		// Whether the cell has PVP [Napster]
	CELL_CHKMAELSTROM,
	CELL_CHKICEWALL,
} cell_chk;

struct mapcell
{
	// terrain flags
	unsigned char
		walkable : 1,
		shootable : 1,
		water : 1;

	// dynamic flags
	unsigned char
		npc : 1,
		basilica : 1,
		landprotector : 1,
		novending : 1,
		nochat : 1,
		pvp : 1,	// Cell PVP [Napster]
		maelstrom : 1,
		icewall: 1;

#ifdef CELL_NOSTACK
	unsigned char cell_bl; //Holds amount of bls in this cell.
#endif
};

struct iwall_data {
	char wall_name[50];
	short m, x, y, size;
	int8 dir;
	bool shootable;
};

struct questinfo_req {
	unsigned int quest_id;
	unsigned state : 2; // 0: Doesn't have, 1: Inactive, 2: Active, 3: Complete //! TODO: CONFIRM ME!!
};

struct questinfo {
	struct npc_data *nd;
	unsigned short icon;
	unsigned char color;
	int quest_id;
	unsigned short min_level,
		max_level;
	uint8 req_count;
	uint8 jobid_count;
	struct questinfo_req *req;
	unsigned short *jobid;
};

struct map_data {
	char name[MAP_NAME_LENGTH];
	unsigned short index; // The map index used by the mapindex* functions.
	struct mapcell* cell; // Holds the information of each map cell (NULL if the map is not on this map-server).
	struct block_list **block;
	struct block_list **block_mob;
	int m;
	short xs,ys; // map dimensions (in cells)
	short bxs,bys; // map dimensions (in blocks)
	short bgscore_lion, bgscore_eagle; // Battleground ScoreBoard
	int npc_num;
	int users;
	int users_pvp;
	int cell_pvpuser;	// Cell PVP [Napster]
	int iwall_num; // Total of invisible walls in this map
	struct map_flag {
		unsigned town : 1; // [Suggestion to protect Mail System]
		unsigned autotrade : 1;
		unsigned allowks : 1; // [Kill Steal Protection]
		unsigned nomemo : 1;
		unsigned noteleport : 1;
		unsigned noreturn : 1;
		unsigned monster_noteleport : 1;
		unsigned nosave : 1;
		unsigned nobranch : 1;
		unsigned noexppenalty : 1;
		unsigned pvp : 1;
		unsigned pvp_noparty : 1;
		unsigned pvp_noguild : 1;
		unsigned pvp_nightmaredrop :1;
		unsigned pvp_nocalcrank : 1;
		unsigned gvg_castle : 1;
		unsigned gvg : 1; // Now it identifies gvg versus maps that are active 24/7
		unsigned gvg_dungeon : 1; // Celest
		unsigned gvg_noparty : 1;
		unsigned battleground : 2; // [BattleGround System]
		unsigned nozenypenalty : 1;
		unsigned notrade : 1;
		unsigned noskill : 1;
		unsigned nowarp : 1;
		unsigned nowarpto : 1;
		unsigned noicewall : 1; // [Valaris]
		unsigned snow : 1; // [Valaris]
		unsigned clouds : 1;
		unsigned clouds2 : 1; // [Valaris]
		unsigned fog : 1; // [Valaris]
		unsigned fireworks : 1;
		unsigned sakura : 1; // [Valaris]
		unsigned leaves : 1; // [Valaris]
		unsigned rain : 1; // [Valaris]
		unsigned nogo : 1; // [Valaris]
		unsigned nobaseexp	: 1; // [Lorky] added by Lupus
		unsigned nojobexp	: 1; // [Lorky]
		unsigned nomobloot	: 1; // [Lorky]
		unsigned nomvploot	: 1; // [Lorky]
		unsigned nightenabled :1; //For night display. [Skotlex]
		unsigned restricted	: 1; // [Komurka]
		unsigned nodrop : 1;
		unsigned novending : 1;
		unsigned loadevent : 1;
		unsigned nochat :1;
		unsigned partylock :1;
		unsigned guildlock :1;
		unsigned src4instance : 1; // To flag this map when it's used as a src map for instances
		unsigned reset :1; // [Daegaladh]
		unsigned gvg_te : 1; // GVG WOE:TE. This was added as purpose to change 'gvg' for GVG TE, so item_noequp, skill_nocast exlude GVG TE maps from 'gvg' (flag &4)
		unsigned gvg_te_castle : 1; // GVG WOE:TE Castle
		unsigned nosunmoonstarmiracle : 1; // SUNMOONSTAR_MIRACLE
		unsigned pairship_startable : 1;
		unsigned pairship_endable : 1;
		unsigned notomb : 1;
		unsigned nocostume : 1; // Disable costume sprites [Cydh]
		unsigned hidemobhpbar : 1;
	} flag;
	struct point save;
	struct npc_data *npc[MAX_NPC_PER_MAP];
	struct {
		int drop_id;
		int drop_type;
		int drop_per;
	} drop_list[MAX_DROP_PER_MAP];

	struct spawn_data *moblist[MAX_MOB_LIST_PER_MAP]; // [Wizputer]
	int mob_delete_timer;	// [Skotlex]
	uint32 zone;	// zone number (for item/skill restrictions)
	int jexp;	// map experience multiplicator
	int bexp;	// map experience multiplicator
	int nocommand; //Blocks @/# commands for non-gms. [Skotlex]
	// Instance Variables
	int instance_id;
	int instance_src_map;

	// Questinfo Cache
	struct questinfo *qi_data;
	unsigned short qi_count;
	unsigned short hpmeter_visible;
};

/// Stores information about a remote map (for multi-mapserver setups).
/// Beginning of data structure matches 'map_data', to allow typecasting.
struct map_data_other_server {
	char name[MAP_NAME_LENGTH];
	unsigned short index; //Index is the map index used by the mapindex* functions.
	struct mapcell* cell; // If this is NULL, the map is not on this map-server
	uint32 ip;
	uint16 port;
};

int map_getcell(int,int,int,cell_chk);
int map_getcellp(struct map_data*,int,int,cell_chk);
void map_setcell(int m, int x, int y, cell_t cell, bool flag);
void map_setgatcell(int m, int x, int y, int gat);

extern struct map_data map[];
extern int map_num;

extern int autosave_interval;
extern int minsave_interval;
extern int save_settings;
extern int night_flag; // 0=day, 1=night [Yor]
extern int enable_spy; //Determines if @spy commands are active.
extern char db_path[256];

// Agit Flags
extern bool agit_flag;
extern bool agit2_flag;
extern bool agit3_flag;
#define is_agit_start() (agit_flag || agit2_flag || agit3_flag)

extern char motd_txt[];
extern char help_txt[];
extern char help2_txt[];
extern char charhelp_txt[];

extern char wisp_server_name[];

/// Bitfield of flags for the iterator.
enum e_mapitflags
{
	MAPIT_NORMAL = 0,
//	MAPIT_PCISPLAYING = 1,// Unneeded as pc_db/id_db will only hold auth'ed, active players.
};
struct s_mapiterator;
struct s_mapiterator*   mapit_alloc(enum e_mapitflags flags, enum bl_type types);
void                    mapit_free(struct s_mapiterator* mapit);
struct block_list*      mapit_first(struct s_mapiterator* mapit);
struct block_list*      mapit_last(struct s_mapiterator* mapit);
struct block_list*      mapit_next(struct s_mapiterator* mapit);
struct block_list*      mapit_prev(struct s_mapiterator* mapit);
bool                    mapit_exists(struct s_mapiterator* mapit);
#define mapit_getallusers() mapit_alloc(MAPIT_NORMAL,BL_PC)
#define mapit_geteachpc()   mapit_alloc(MAPIT_NORMAL,BL_PC)
#define mapit_geteachmob()  mapit_alloc(MAPIT_NORMAL,BL_MOB)
#define mapit_geteachnpc()  mapit_alloc(MAPIT_NORMAL,BL_NPC)
#define mapit_geteachiddb() mapit_alloc(MAPIT_NORMAL,BL_ALL)

// その他
int map_check_dir(int s_dir,int t_dir);
uint8 map_calc_dir(struct block_list *src, int16 x, int16 y);
uint8 map_calc_dir_xy(int16 srcx, int16 srcy, int16 x, int16 y, uint8 srcdir);
int map_random_dir(struct block_list *bl, short *x, short *y); // [Skotlex]

int cleanup_sub(struct block_list *bl, va_list ap);

int map_delmap(char* mapname);
void map_flags_init(void);

bool map_iwall_set(int m, int x, int y, int size, int dir, bool shootable, const char* wall_name);
void map_iwall_get(struct map_session_data *sd);
void map_iwall_remove(const char *wall_name);

int map_addmobtolist(unsigned short m, struct spawn_data *spawn);	// [Wizputer]
void map_spawnmobs(int); // [Wizputer]
void map_removemobs(int); // [Wizputer]
void do_reconnect_map(void); //Invoked on map-char reconnection [Skotlex]
void map_addmap2db(struct map_data *m);
void map_removemapdb(struct map_data *m);

#define CHK_ELEMENT(ele) ((ele) > ELE_NONE && (ele) < ELE_MAX) /// Check valid Element
#define CHK_RACE(race) ((race) > RC_NONE_ && (race) < RC_MAX) /// Check valid Race
#define CHK_RACE2(race2) ((race2) >= RC2_NONE && (race2) < RC2_MAX) /// Check valid Race2
#define CHK_CLASS(class_) ((class_) > CLASS_NONE && (class_) < CLASS_MAX) /// Check valid Class

extern char *INTER_CONF_NAME;
extern char *LOG_CONF_NAME;
extern char *MAP_CONF_NAME;
extern char *BATTLE_CONF_FILENAME;
extern char *ATCOMMAND_CONF_FILENAME;
extern char *SCRIPT_CONF_NAME;
extern char *MSG_CONF_NAME_EN;
extern char *GRF_PATH_FILENAME;

//Other languages supported
char *MSG_CONF_NAME_RUS;
char *MSG_CONF_NAME_SPN;
char *MSG_CONF_NAME_GER;
char *MSG_CONF_NAME_CHN;
char *MSG_CONF_NAME_MAL;
char *MSG_CONF_NAME_IDN;
char *MSG_CONF_NAME_FRN;
char *MSG_CONF_NAME_POR;

//Useful typedefs from jA [Skotlex]
typedef struct map_session_data TBL_PC;
typedef struct npc_data         TBL_NPC;
typedef struct mob_data         TBL_MOB;
typedef struct flooritem_data   TBL_ITEM;
typedef struct chat_data        TBL_CHAT;
typedef struct skill_unit       TBL_SKILL;
typedef struct pet_data         TBL_PET;
typedef struct homun_data       TBL_HOM;
typedef struct mercenary_data   TBL_MER;
typedef struct elemental_data   TBL_ELEM;

#define BL_CAST(type_, bl) \
	( ((bl) == (struct block_list*)NULL || (bl)->type != (type_)) ? (T ## type_ *)NULL : (T ## type_ *)(bl) )

/**
* Helper function for `BL_UCCAST`.
*
* @warning
*   This function shouldn't be called on it own.
*
* The purpose of this function is to produce a compile-timer error if a non-bl
* object is passed to BL_UCAST. It's declared as static inline to let the
* compiler optimize out the function call overhead.
*/
inline const struct block_list *BL_UCCAST_(const struct block_list *bl) __attribute__((unused));
inline const struct block_list *BL_UCCAST_(const struct block_list *bl)
{
	return bl;
}

/**
 * Helper function for `BL_UCAST`.
 *
 * @warning
 *   This function shouldn't be called on it own.
 *
 * The purpose of this function is to produce a compile-timer error if a non-bl
 * object is passed to BL_UCAST. It's declared as static inline to let the
 * compiler optimize out the function call overhead.
 */
static inline struct block_list *BL_UCAST_(struct block_list *bl) __attribute__((unused));
static inline struct block_list *BL_UCAST_(struct block_list *bl)
{
	return bl;
}

/**
 * Casts a block list to a specific type, without performing any type checks.
 *
 * @remark
 *   The `bl` argument is guaranteed to be evaluated once and only once.
 *
 * @param type_ The block list type (using symbols from enum bl_type).
 * @param bl    The source block list to cast.
 * @return The block list, cast to the correct type.
 */
#define BL_UCAST(type_, bl) \
	((T ## type_ *)BL_UCAST_(bl))

/**
* Casts a const block list to a specific type, without performing any type checks.
*
* @remark
*   The `bl` argument is guaranteed to be evaluated once and only once.
*
* @param type_ The block list type (using symbols from enum bl_type).
* @param bl    The source block list to cast.
* @return The block list, cast to the correct type.
*/
#define BL_UCCAST(type_, bl) \
	((const T ## type_ *)BL_UCCAST_(bl))


extern char main_chat_nick[16];

#ifndef TXT_ONLY

#include "../common/sql.h"

extern int db_use_sqldbs;

extern Sql* mmysql_handle;
extern Sql* logmysql_handle;

extern char item_db_db[32];
extern char item_db2_db[32];
extern char mob_db_db[32];
extern char mob_db2_db[32];
extern char market_table[32];
extern char db_roulette_table[32];
extern char vendings_db[32];
extern char vending_items_db[32];

#endif /* not TXT_ONLY */

/// Type of 'save_settings'
enum save_settings_type {
	CHARSAVE_NONE		= 0x000, /// Never
	CHARSAVE_TRADE		= 0x001, /// After trading
	CHARSAVE_VENDING	= 0x002, /// After vending (open/transaction)
	CHARSAVE_STORAGE	= 0x004, /// After closing storage/guild storage.
	CHARSAVE_PET		= 0x008, /// After hatching/returning to egg a pet.
	CHARSAVE_MAIL		= 0x010, /// After successfully sending a mail with attachment
	CHARSAVE_AUCTION	= 0x020, /// After successfully submitting an item for auction
	CHARSAVE_QUEST		= 0x040, /// After successfully get/delete/complete a quest
	CHARSAVE_BANK		= 0x080, /// After every bank transaction (deposit/withdraw)
	CHARSAVE_ALL		= 0xFFF, /// Always
};

// 鯖全体情報
void map_setusers(int);
int map_getusers(void);
int map_usercount(void);
// block削除関連
int map_freeblock(struct block_list *bl);
int map_freeblock_lock(void);
int map_freeblock_unlock(void);
// block関連
int map_addblock(struct block_list* bl);
int map_delblock(struct block_list* bl);
int map_moveblock(struct block_list *, int, int, int64);
int map_foreachinrange(int(*func)(struct block_list*, va_list), struct block_list* center, int range, int type, ...);
int map_pickrandominrange(int(*func)(struct block_list*, va_list), struct block_list* center, int range, int max, int ignore_id, int type, ...);
int map_foreachinshootrange(int(*func)(struct block_list*, va_list), struct block_list* center, int range, int type, ...);
int map_foreachinarea(int(*func)(struct block_list*, va_list), int m, int x0, int y0, int x1, int y1, int type, ...);
int map_foreachinshootarea(int(*func)(struct block_list*, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int type, ...);
int map_forcountinrange(int(*func)(struct block_list*, va_list), struct block_list* center, int range, int count, int type, ...);
int map_forcountinarea(int(*func)(struct block_list*, va_list), int m, int x0, int y0, int x1, int y1, int count, int type, ...);
int map_foreachinmovearea(int(*func)(struct block_list*, va_list), struct block_list* center, int range, int dx, int dy, int type, ...);
int map_foreachincell(int(*func)(struct block_list*, va_list), int m, int x, int y, int type, ...);
int map_foreachinpath(int(*func)(struct block_list*, va_list), int m, int x0, int y0, int x1, int y1, int range, int length, int type, ...);
int map_foreachindir(int(*func)(struct block_list*, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int16 range, int length, int offset, int type, ...);
int map_foreachinmap(int(*func)(struct block_list*, va_list), int m, int type, ...);
int map_foreachininstance(int(*func)(struct block_list*, va_list), int16 instance_id, int type, ...);
//block関連に追加
int map_count_oncell(int m, int x, int y, int type, int flag);
struct skill_unit *map_find_skill_unit_oncell(struct block_list *, int x, int y, int skill_id, struct skill_unit *, int flag);
// 一時的object関連
int map_get_new_object_id(void);
int map_search_freecell(struct block_list *src, int m, short *x, short *y, int rx, int ry, int flag);
bool map_closest_freecell(int16 m, int16 *x, int16 *y, int type, int flag);
//
int map_quit(struct map_session_data *);
// npc
bool map_addnpc(int, struct npc_data *);

// 床アイテム関連
int map_clearflooritem_timer(int tid, int64 tick, int id, intptr_t data);
int map_removemobs_timer(int tid, int64 tick, int id, intptr_t data);
void map_clearflooritem(struct block_list* bl);
int map_addflooritem(struct item *item_data, int amount, int m, int x, int y, int first_charid, int second_charid, int third_charid, int flags, unsigned short mob_id, bool canShowEffect);

// キャラid＝＞キャラ名 変換関連
void map_addnickdb(int charid, const char* nick);
void map_delnickdb(int charid, const char* nick);
void map_reqnickdb(struct map_session_data* sd, int charid);
const char* map_charid2nick(int charid);
struct map_session_data* map_charid2sd(int charid);

struct map_session_data * map_id2sd(int id);
struct mob_data * map_id2md(int id);
struct npc_data * map_id2nd(int id);
struct homun_data* map_id2hd(int id);
struct mercenary_data* map_id2mc(int id);
struct pet_data* map_id2pd(int id);
struct elemental_data* map_id2ed(int id);
struct chat_data* map_id2cd(int id);
struct block_list * map_id2bl(int id);
bool map_blid_exists(int id);

#define map_id2index(id) map[(id)].index
int map_mapindex2mapid(unsigned short mapindex);
int map_mapname2mapid(const char* name);
int map_mapname2ipport(unsigned short name, uint32* ip, uint16* port);
int map_setipport(unsigned short map, uint32 ip, uint16 port);
int map_eraseipport(unsigned short map, uint32 ip, uint16 port);
int map_eraseallipport(void);
void map_addiddb(struct block_list *);
void map_deliddb(struct block_list *bl);
void map_foreachpc(int(*func)(struct map_session_data* sd, va_list args), ...);
void map_foreachmob(int(*func)(struct mob_data* md, va_list args), ...);
void map_foreachnpc(int(*func)(struct npc_data* nd, va_list args), ...);
void map_foreachregen(int(*func)(struct block_list* bl, va_list args), ...);
void map_foreachiddb(int(*func)(struct block_list* bl, va_list args), ...);
struct map_session_data * map_nick2sd(const char*);
struct mob_data * map_getmob_boss(int m);
struct mob_data * map_id2boss(int id);

static void map_free_questinfo(int m);
struct questinfo *map_add_questinfo(int m, struct questinfo *qi);
struct questinfo *map_has_questinfo(int m, struct npc_data *nd, int quest_id);

uint32 map_race_id2mask(int race);

void do_shutdown(void);

// Cell PVP [Napster]
int map_pvp_area(struct map_session_data* sd, bool flag);

#endif /* _MAP_H_ */
