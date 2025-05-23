// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef	_MMO_H_
#define	_MMO_H_

#include "cbasetypes.h"
#include "../common/db.h" // VECTORS
#include <time.h>

// server->client protocol version
//        0 - pre-?
//        1 - ?                    - 0x196
//        2 - ?                    - 0x78, 0x79
//        3 - ?                    - 0x1c8, 0x1c9, 0x1de
//        4 - ?                    - 0x1d7, 0x1d8, 0x1d9, 0x1da
//        5 - 2003-12-18aSakexe+   - 0x1ee, 0x1ef, 0x1f0, ?0x1c4, 0x1c5?
//        6 - 2004-03-02aSakexe+   - 0x1f4, 0x1f5
//        7 - 2005-04-11aSakexe+   - 0x229, 0x22a, 0x22b, 0x22c
// 20061023 - 2006-10-23aSakexe+   - 0x6b, 0x6d
// 20070521 - 2007-05-21aSakexe+   - 0x283
// 20070821 - 2007-08-21aSakexe+   - 0x2c5, 0x2c6
// 20070918 - 2007-09-18aSakexe+   - 0x2d7, 0x2d9, 0x2da
// 20071106 - 2007-11-06aSakexe+   - 0x78, 0x7c, 0x22c
// 20080102 - 2008-01-02aSakexe+   - 0x2ec, 0x2ed , 0x2ee
// 20081126 - 2008-11-26aSakexe+   - 0x1a2
// 20090408 - 2009-04-08aSakexe+   - 0x44a (dont use as it overlaps with RE client packets)
// 20080827 - 2008-08-27aRagexeRE+ - First RE Client
// 20081126 - 2008-11-26aSakexe+   - 0x1a2, 0x441
// 20081210 - 2008-12-10aSakexe+   - 0x442
// 20081217 - 2008-12-17aRagexeRE+ - 0x6d (Note: This one still use old Char Info Packet Structure)
// 20081218 - 2008-12-17bRagexeRE+ - 0x6d (Note: From this one client use new Char Info Packet Structure)
// 20090603 - 2009-06-03aRagexeRE+ - 0x7d7, 0x7d8, 0x7d9, 0x7da
// 20090617 - 2009-06-17aRagexeRE+ - 0x7d9
// 20090715 - 2009-07-15aRagexeRE+ - 0x7e1
// 20090922 - 2009-09-22aRagexeRE+ - 0x7e5, 0x7e7, 0x7e8, 0x7e9
// 20091103 - 2009-11-03aRagexeRE+ - 0x7f7, 0x7f8, 0x7f9
// 20100105 - 2010-01-05aRagexeRE+ - 0x133, 0x800, 0x801
// 20100126 - 2010-01-26aRagexeRE+ - 0x80e
// 20100223 - 2010-02-23aRagexeRE+ - 0x80f
// 20100413 - 2010-04-13aRagexeRE+ - 0x6b
// 20100629 - 2010-06-29aRagexeRE+ - 0x2d0, 0xaa, 0x2d1, 0x2d2
// 20100721 - 2010-07-21aRagexeRE+ - 0x6b, 0x6d
// 20100727 - 2010-07-27aRagexeRE+ - 0x6b, 0x6d
// 20100803 - 2010-08-03aRagexeRE+ - 0x6b, 0x6d, 0x827, 0x828, 0x829, 0x82a, 0x82b, 0x82c, 0x842, 0x843
// 20101124 - 2010-11-24aRagexeRE+ - 0x856, 0x857, 0x858
// 20110111 - 2011-01-11aRagexeRE+ - 0x6b, 0x6d
// 20110928 - 2011-09-28aRagexeRE+ - 0x6b, 0x6d
// 20111025 - 2011-10-25aRagexeRE+ - 0x6b, 0x6d
// 20120307 - 2012-03-07aRagexeRE+ - 0x970

#ifndef PACKETVER
	//#define PACKETVER 20180919 //stable client [15peaces]
	#define PACKETVER 20181121 //stable client [15peaces]
	//#define PACKETVER 20190530 //next experimental client [15peaces]
#endif

// backward compatible PACKETVER 8 and 9
#if PACKETVER == 8
#undef PACKETVER
#define PACKETVER 20070521
#endif
#if PACKETVER == 9
#undef PACKETVER
#define PACKETVER 20071106
#endif

/// Check if the client needs delete_date as remaining time and not the actual delete_date (actually it was tested for clients since 2013)
#define PACKETVER_CHAR_DELETEDATE (PACKETVER > 20130000 && PACKETVER < 20141022) || PACKETVER >= 20150513

//Remove/Comment this line to disable sc_data saving. [Skotlex]
#define ENABLE_SC_SAVING 
//Remove/Comment this line to disable server-side hot-key saving support [Skotlex]
//Note that newer clients no longer save hotkeys in the registry!
#define HOTKEY_SAVING

//The number is the max number of hotkeys to save
#if PACKETVER < 20090603
	// (27 = 9 skills x 3 bars)               (0x02b9,191)
	#define MAX_HOTKEYS 27
#elif PACKETVER < 20090617
	// (36 = 9 skills x 4 bars)               (0x07d9,254)
	#define MAX_HOTKEYS 36
#else
	// (38 = 9 skills x 4 bars & 2 Quickslots)(0x07d9,268)
	#define MAX_HOTKEYS 38
#endif

#define MAX_MAP_PER_SERVER 1500 // Increased to allow creation of Instance Maps

#ifndef INVENTORY_BASE_SIZE
	#define INVENTORY_BASE_SIZE 100 // Amount of inventory slots each player has
#endif

#ifndef INVENTORY_EXPANSION_SIZE
#if PACKETVER >= 20181031
	#define INVENTORY_EXPANSION_SIZE 100 // Amount of additional inventory slots a player can have
#else
	#define INVENTORY_EXPANSION_SIZE 0
#endif
#endif

#define MAX_INVENTORY ( INVENTORY_BASE_SIZE + INVENTORY_EXPANSION_SIZE ) // Maximum items in player inventory (in total)

//Max number of characters per account. Note that changing this setting alone is not enough if the client is not hexed to support more characters as well.
//April 2010 and newer clients don't need to be hexed. Setting server side is enough.
//Keep this setting at a multiple of 3.
#if PACKETVER >= 20180620
	//Default is 15 here(?)
	#define MAX_CHARS 15
#elif PACKETVER >= 20100401
	//Default was 12 here.
	#define MAX_CHARS 12
#else
	//Default was 9 here, can't go higher without client hexing!
	#define MAX_CHARS 9
#endif

// Allow players to create more then just human characters?
// Current Races Supported: Human / Doram.
// Setting is 1 for yes and 0 for no.
#define ALLOW_OTHER_RACES 1

typedef uint32 t_itemid;

//Number of slots carded equipment can have. Never set to less than 4 as they are also used to keep the data of forged items/equipment. [Skotlex]
//Note: The client seems unable to receive data for more than 4 slots due to all related packets having a fixed size.
#define MAX_SLOTS 4
//Max amount of a single stacked item
#define MAX_AMOUNT 30000
#define MAX_ZENY 1000000000
#define MAX_BANK_ZENY SINT32_MAX
#define MAX_FAME 1000000000
#define MAX_CART 100
#define MAX_SKILL 5079
#define GLOBAL_REG_NUM 256
#define ACCOUNT_REG_NUM 64
#define ACCOUNT_REG2_NUM 16
//Should hold the max of GLOBAL/ACCOUNT/ACCOUNT2 (needed for some arrays that hold all three)
#define MAX_REG_NUM 256
#define DEFAULT_WALK_SPEED 150
#define MIN_WALK_SPEED 20
#define MAX_WALK_SPEED 1000
#define MAX_STORAGE 600 // Max number of storage slots a player can have, cannot be higher than ~700!
#define MAX_GUILD_STORAGE  100*5	// Max storage is 100 * GD_GUILD_STORAGE level. [Rytech]
#define MAX_PARTY 12
#define MAX_GUILD (16+10*6)	// increased max guild members +6 per 1 extension levels [Lupus]
#define MAX_GUILDPOSITION 20	// increased max guild positions to accomodate for all members [Valaris] (removed) [PoW]
#define MAX_GUILDEXPULSION 32
#define MAX_GUILDALLIANCE 16
#define MAX_GUILDSKILL	20 // increased max guild skills because of new skills [Sara-chan] [15peaces]
#define MAX_GUILDLEVEL 50
#define MAX_GUARDIANS 8	//Local max per castle. [Skotlex]
#define MAX_QUEST_OBJECTIVES 3 //Max quest objectives for a quest
#define MAX_QUEST_DROPS 3 ///Max quest drops for a quest
#define MAX_PC_BONUS_SCRIPT 50 ///Max bonus script can be fetched from `bonus_script` table on player load [Cydh]
#define MAX_ITEM_RDM_OPT 5	 /// Max item random option [Napster] 
#define MAX_CLAN 500
#define MAX_CLANALLIANCE 6

// for produce
#define MIN_ATTRIBUTE 0
#define MAX_ATTRIBUTE 4
#define ATTRIBUTE_NORMAL 0
#define MIN_STAR 0
#define MAX_STAR 3

#define MAX_STATUS_TYPE 5

#define WEDDING_RING_M 2634
#define WEDDING_RING_F 2635

//For character names, title names, guilds, maps, etc.
//Includes null-terminator as it is the length of the array.
#define NAME_LENGTH (23 + 1)
//NPC names can be longer than it's displayed on client (NAME_LENGTH).
#define NPC_NAME_LENGTH 50
//For item names, which tend to have much longer names.
#define ITEM_NAME_LENGTH 50
//For Map Names, which the client considers to be 16 in length including the .gat extension
// FIXME: These are used wrong, they are supposed to be character strings of lengths 12/16,
//        where only lengths of 11/15 and lower are zero-terminated, not always zero-
//        terminated character strings of lengths 11+1/15+1! This is why mjolnir_04_1 cannot
//        be used on eAthena. [Ai4rei]
#define MAP_NAME_LENGTH (11 + 1)
#define MAP_NAME_LENGTH_EXT (MAP_NAME_LENGTH + 4)

#define PINCODE_LENGTH 4

#define MAX_FRIENDS 40
#define MAX_MEMOPOINTS 3
#define MAX_SKILLCOOLDOWN 20

//Size of the fame list arrays.
#define MAX_FAME_LIST 10

//Limits to avoid ID collision with other game objects
#define START_ACCOUNT_NUM 2000000
#define END_ACCOUNT_NUM 100000000
#define START_CHAR_NUM 150000

//Guilds
#define MAX_GUILDMES1 60
#define MAX_GUILDMES2 120

//Base Homun skill.
#define HM_SKILLBASE 8001
#define MAX_HOMUNSKILL 43//Increased to 43 to add the mutated homunculus skills.
#define MAX_HOMUNCULUS_CLASS 16	//[orn]
#define HM_CLASS_BASE 6001
#define HM_CLASS_MAX (HM_CLASS_BASE+MAX_HOMUNCULUS_CLASS-1)

//Mutated Homunculus System
#define MAX_MUTATE_HOMUNCULUS_CLASS	5
#define MH_CLASS_BASE 6048
#define MH_CLASS_MAX (MH_CLASS_BASE+MAX_MUTATE_HOMUNCULUS_CLASS-1)

//Mail System
#define MAIL_MAX_INBOX 30
#define MAIL_TITLE_LENGTH 40
#if PACKETVER < 20150513
#define MAIL_BODY_LENGTH 200
#define MAIL_MAX_ITEM 1
#else
#define MAIL_BODY_LENGTH 500
#define MAIL_MAX_ITEM 5
#define MAIL_PAGE_SIZE 7
#endif

//Mercenary System
#define MC_SKILLBASE 8201
#define MAX_MERCSKILL 41
#define MAX_MERCENARY_CLASS 61

//Elemental System
#define EL_SKILLBASE 8401
#define MAX_ELEMSKILL 42
#define MAX_ELEMENTAL_CLASS 12

// Achievements [Smokexyz/Hercules]
#define MAX_ACHIEVEMENT_DB 360          // Maximum number of achievements
#define MAX_ACHIEVEMENT_OBJECTIVES 10   // Maximum number of achievement objectives
#define MAX_ACHIEVEMENT_RANKS 20 // Achievement Ranks
#define MAX_ACHIEVEMENT_ITEM_REWARDS 10 // Achievement Rewards

//15-3athena
//Will be needed in the future for keeping track of and saving cooldown times for skills. [15peaces]
//#define MAX_SKILLCOOLDOWN 20

enum item_types {
	IT_HEALING = 0,			//IT_HEAL		= 0x00
	IT_UNKNOWN,		//1		//IT_SCHANGE	= 0x01
	IT_USABLE,		//2		//IT_SPECIAL	= 0x02
	IT_ETC,			//3		//IT_EVENT		= 0x03
	IT_ARMOR,		//4		//IT_ARMOR		= 0x04
	IT_WEAPON,		//5		//IT_WEAPON		= 0x05
	IT_CARD,		//6		//IT_CARD		= 0x06
	IT_PETEGG,		//7		//IT_QUEST		= 0x07
	IT_PETARMOR,	//8		//IT_BOW		= 0x08
	IT_UNKNOWN2,	//9		//IT_BOTHHAND	= 0x09
	IT_AMMO,		//10	//IT_ARROW		= 0x0a
	IT_DELAYCONSUME,//11	//IT_ARMORTM	= 0x0b
	IT_SHADOWGEAR,	//12	//IT_ARMORTB	= 0x0c
							//IT_ARMORMB	= 0x0d
							//IT_ARMORTMB	= 0x0e
							//IT_GUN		= 0x0f
							//IT_AMMO		= 0x10
							//IT_THROWWEAPON= 0x11
	IT_CASH = 18,			//IT_CASH_POINT_ITEM= 0x12
							//IT_CANNONBALL	= 0x13
	IT_MAX
};


// Questlog states
enum quest_state {
	Q_INACTIVE, ///< Inactive quest (the user can toggle between active and inactive quests)
	Q_ACTIVE,   ///< Active quest
	Q_COMPLETE, ///< Completed quest
};

struct quest {
	int quest_id;                    ///< Quest ID
	unsigned int time;               ///< Expiration time
	int count[MAX_QUEST_OBJECTIVES]; ///< Kill counters of each quest objective
	enum quest_state state;          ///< Current quest state
};

/// Achievement log entry
struct achievement {
	int id;                    ///< Achievement ID
	int objective[MAX_ACHIEVEMENT_OBJECTIVES]; ///< Counters of each achievement objective
	time_t completed;                      ///< Date completed
	time_t rewarded;                       ///< Received reward?
};

VECTOR_STRUCT_DECL(char_achievements, struct achievement);

struct item_option {
	int16 id;
	int16 value;
	uint8 param;
};

struct item {
	int id;
	t_itemid nameid;
	short amount;
	unsigned int equip; // location(s) where item is equipped (using enum equip_pos for bitmasking)
	char identify;
	char refine;
	char attribute;
	t_itemid card[MAX_SLOTS];
	struct item_option option[MAX_ITEM_RDM_OPT];		// max of 5 random options can be supported.
	unsigned int expire_time;
	unsigned char favorite;
	unsigned char bound;
	unsigned int equipSwitch; // location(s) where item is equipped for equip switching (using enum equip_pos for bitmasking)
	uint64 unique_id;
};

//Equip position constants
enum equip_pos {
	EQP_HEAD_LOW = 0x0001,
	EQP_HEAD_MID = 0x0200,
	EQP_HEAD_TOP = 0x0100,
	EQP_HAND_R = 0x0002,
	EQP_HAND_L = 0x0020,
	EQP_ARMOR = 0x0010,
	EQP_SHOES = 0x0040,
	EQP_GARMENT = 0x0004,
	EQP_ACC_L = 0x0008,
	EQP_ACC_R = 0x0080,
	EQP_AMMO = 0x8000,
	EQP_COSTUME_HEAD_TOP = 0x0400,
	EQP_COSTUME_HEAD_MID = 0x0800,
	EQP_COSTUME_HEAD_LOW = 0x1000,
	EQP_COSTUME_GARMENT = 0x2000,
	EQP_COSTUME_FLOOR = 0x4000,
	EQP_SHADOW_ARMOR = 0x10000,
	EQP_SHADOW_WEAPON = 0x20000,
	EQP_SHADOW_SHIELD = 0x40000,
	EQP_SHADOW_SHOES = 0x80000,
	EQP_SHADOW_ACC_R = 0x100000,
	EQP_SHADOW_ACC_L = 0x200000,
};

//Equip indexes constants. (eg: sd->equip_index[EQI_AMMO] returns the index
//where the arrows are equipped)
enum equip_index {
	EQI_COMPOUND_ON = -1,
	EQI_ACC_L = 0,
	EQI_ACC_R,
	EQI_SHOES,
	EQI_GARMENT,
	EQI_HEAD_LOW,
	EQI_HEAD_MID,
	EQI_HEAD_TOP,
	EQI_ARMOR,
	EQI_HAND_L,
	EQI_HAND_R,
	EQI_COSTUME_HEAD_TOP,
	EQI_COSTUME_HEAD_MID,
	EQI_COSTUME_HEAD_LOW,
	EQI_COSTUME_GARMENT,
	EQI_AMMO,
	EQI_SHADOW_ARMOR,
	EQI_SHADOW_WEAPON,
	EQI_SHADOW_SHIELD,
	EQI_SHADOW_SHOES,
	EQI_SHADOW_ACC_R,
	EQI_SHADOW_ACC_L,
	EQI_MAX
};

struct point {
	unsigned short map;
	short x,y;
};

enum e_skill_flag
{
	SKILL_FLAG_PERMANENT,
	SKILL_FLAG_TEMPORARY,
	SKILL_FLAG_PLAGIARIZED,
	SKILL_FLAG_PERM_GRANTED, // Permanent, granted through someway e.g. script
	SKILL_FLAG_TMP_COMBO, //@FIXME for homunculus combo atm

	//! NOTE: This flag be the last flag, and don't change the value if not needed!
	SKILL_FLAG_REPLACED_LV_0 = 10, // temporary skill overshadowing permanent skill of level 'N - SKILL_FLAG_REPLACED_LV_0',
};

//OPTION: (EFFECTSTATE_)
enum {
	OPTION_NOTHING		= 0x00000000,
	OPTION_SIGHT		= 0x00000001,
	OPTION_HIDE			= 0x00000002,
	OPTION_CLOAK		= 0x00000004,
	OPTION_CART1		= 0x00000008,
	OPTION_FALCON		= 0x00000010,
	OPTION_RIDING		= 0x00000020,
	OPTION_INVISIBLE	= 0x00000040,
	OPTION_CART2		= 0x00000080,
	OPTION_CART3		= 0x00000100,
	OPTION_CART4		= 0x00000200,
	OPTION_CART5		= 0x00000400,
	OPTION_ORCISH		= 0x00000800,
	OPTION_WEDDING		= 0x00001000,
	OPTION_RUWACH		= 0x00002000,
	OPTION_CHASEWALK	= 0x00004000,
	OPTION_FLYING		= 0x00008000, //Note that clientside Flying and Xmas are 0x8000 for clients prior to 2007.
	OPTION_XMAS			= 0x00010000,
	OPTION_TRANSFORM	= 0x00020000,
	OPTION_SUMMER		= 0x00040000,
	OPTION_DRAGON1		= 0x00080000,
	OPTION_WUG			= 0x00100000,
	OPTION_WUGRIDER		= 0x00200000,
	OPTION_MADOGEAR		= 0x00400000,
	OPTION_DRAGON2		= 0x00800000,
	OPTION_DRAGON3		= 0x01000000,
	OPTION_DRAGON4		= 0x02000000,
	OPTION_DRAGON5		= 0x04000000,
	OPTION_HANBOK		= 0x08000000,
	OPTION_OKTOBERFEST	= 0x10000000,
	OPTION_SUMMER2		= 0x20000000,// Fix me. I exist, but not like this.
	// compound constants
	OPTION_CART		= OPTION_CART1|OPTION_CART2|OPTION_CART3|OPTION_CART4|OPTION_CART5,
	OPTION_DRAGON	= OPTION_DRAGON1|OPTION_DRAGON2|OPTION_DRAGON3|OPTION_DRAGON4|OPTION_DRAGON5,
	OPTION_COSTUME	= OPTION_WEDDING|OPTION_XMAS|OPTION_SUMMER|OPTION_HANBOK|OPTION_OKTOBERFEST|OPTION_SUMMER2,
	OPTION_MASK		= ~OPTION_INVISIBLE,
};

struct s_skill {
	unsigned short id;
	unsigned char lv;
	unsigned char flag; // see enum e_skill_flag
};

struct global_reg {
	char str[32];
	char value[256];
};

//Holds array of global registries, used by the char server and converter.
struct accreg {
	uint32 account_id, char_id;
	int reg_num;
	struct global_reg reg[MAX_REG_NUM];
};

//For saving status changes across sessions. [Skotlex]
struct status_change_data {
	unsigned short type; //SC_type
	long val1, val2, val3, val4, tick; //Remaining duration.
};

struct skill_cooldown_data {
	unsigned short skill_id;
	long tick;
};

#define MAX_BONUS_SCRIPT_LENGTH 512
struct bonus_script_data {
	char script_str[MAX_BONUS_SCRIPT_LENGTH]; //< Script string
	uint32 tick; ///< Tick
	uint16 flag; ///< Flags @see enum e_bonus_script_flags
	int16 icon; ///< Icon SI
	uint8 type; ///< 0 - None, 1 - Buff, 2 - Debuff
};

enum storage_type {
	TABLE_INVENTORY = 1,
	TABLE_CART,
	TABLE_STORAGE,
	TABLE_GUILD_STORAGE,
};

struct s_storage {
	bool dirty; ///< Dirty status, data needs to be saved
	bool status; ///< Current status of storage (opened or closed)
	uint16 amount; ///< Amount of items in storage
	bool lock; ///< If locked, can't use storage when item bound retrieval
	uint32 id; ///< Account ID / Character ID / Guild ID (owner of storage)
	enum storage_type type; ///< Type of storage (inventory, cart, storage, guild storage)
	uint8 stor_id; ///< Storage ID
	union { // Max for inventory, storage, cart, and guild storage are 1637 each without changing this struct and struct item [2014/10/27]
		struct item items_inventory[MAX_INVENTORY];
		struct item items_storage[MAX_STORAGE];
		struct item items_cart[MAX_CART];
		struct item items_guild[MAX_GUILD_STORAGE];
	} u;
};

struct s_storage_table {
	char name[NAME_LENGTH];
	char table[256];
	uint8 id;
};

struct s_pet {
	uint32 account_id;
	uint32 char_id;
	int pet_id;
	short class_;
	short level;
	t_itemid egg_id;//pet egg id
	t_itemid equip;//pet equip name_id
	short intimate;//pet friendly
	short hungry;//pet hungry
	char name[NAME_LENGTH];
	char rename_flag;
	char incuvate;
	int autofeed;
};

struct s_homunculus {	//[orn]
	char name[NAME_LENGTH];
	int hom_id;
	uint32 char_id;
	short class_;
	int hp,max_hp,sp,max_sp;
	unsigned int intimacy;	//[orn]
	short hunger;
	struct s_skill hskill[MAX_HOMUNSKILL]; //albator
	short skillpts;
	short level;
	unsigned int exp;
	short rename_flag;
	short vaporize; //albator
	int str ;
	int agi ;
	int vit ;
	int int_ ;
	int dex ;
	int luk ;

	int autofeed; // TODO
};

struct s_mercenary {
	int mercenary_id;
	uint32 char_id;
	short class_;
	int hp, sp;
	unsigned int kill_count;
	unsigned int life_time;
};

struct s_elemental {
	int elemental_id;
	uint32 char_id;
	short class_;
	int hp, max_hp, sp, max_sp;
	unsigned short batk, matk, amotion;
	short def, mdef, hit, flee;
	unsigned int life_time;
};

struct s_friend {
	uint32 account_id;
	uint32 char_id;
	char name[NAME_LENGTH];
};

#ifdef HOTKEY_SAVING
struct hotkey {
	unsigned int id;
	unsigned short lv;
	unsigned char type; // 0: item, 1: skill
};
#endif

struct mmo_charstatus {
	uint32 char_id;
	uint32 account_id;
	uint32 partner_id;
	uint32 father;
	uint32 mother;
	uint32 child;

	unsigned int base_exp,job_exp;
	int zeny;

	short class_;
	unsigned int status_point,skill_point;
	int hp,max_hp,sp,max_sp;
	unsigned int option;
	short manner; // Defines how many minutes a char will be muted, each negative point is equivalent to a minute.
	unsigned char karma;
	short hair,hair_color,clothes_color,body;
	int party_id, guild_id, clan_id, pet_id, hom_id, mer_id, ele_id;
	int fame;

	// Mercenary Guilds Rank
	int arch_faith, arch_calls;
	int spear_faith, spear_calls;
	int sword_faith, sword_calls;

	short weapon; // enum weapon_type
	short shield; // view-id
	short head_top, head_mid, head_bottom;
	short robe;

	char name[NAME_LENGTH];
	unsigned int base_level, job_level;
	short str, agi, vit, int_, dex, luk;
	unsigned char slot, sex;

	uint32 mapip;
	uint16 mapport;

	struct point last_point, save_point, memo_point[MAX_MEMOPOINTS];
	struct s_skill skill[MAX_SKILL];

	struct s_friend friends[MAX_FRIENDS]; //New friend system [Skotlex]
#ifdef HOTKEY_SAVING
	struct hotkey hotkeys[MAX_HOTKEYS];
#endif
	bool show_equip, allow_party;
	short rename;
	unsigned int character_moves;

	time_t delete_date;

	unsigned char hotkey_rowshift;
	unsigned long title_id;

	uint32 uniqueitem_counter;

	time_t unban_time;

	unsigned char font;
};

typedef enum mail_status {
	MAIL_NEW,
	MAIL_UNREAD,
	MAIL_READ,
} mail_status;

enum mail_inbox_type {
	MAIL_INBOX_NORMAL = 0,
	MAIL_INBOX_ACCOUNT,
	MAIL_INBOX_RETURNED
};

enum mail_attachment_type {
	MAIL_ATT_NONE = 0,
	MAIL_ATT_ZENY = 1,
	MAIL_ATT_ITEM = 2,
	MAIL_ATT_ALL = MAIL_ATT_ZENY | MAIL_ATT_ITEM
};

struct mail_message {
	int id;
	int send_id;
	char send_name[NAME_LENGTH];
	int dest_id;
	char dest_name[NAME_LENGTH];
	char title[MAIL_TITLE_LENGTH];
	char body[MAIL_BODY_LENGTH];
	int type; // enum mail_inbox_type
	time_t scheduled_deletion;

	mail_status status;
	time_t timestamp; // marks when the message was sent

	int zeny;
	struct item item[MAIL_MAX_ITEM];
};

struct mail_data {
	short amount;
	bool full;
	short unchecked, unread;
	struct mail_message msg[MAIL_MAX_INBOX];
};

struct auction_data {
	unsigned int auction_id;
	int seller_id;
	char seller_name[NAME_LENGTH];
	int buyer_id;
	char buyer_name[NAME_LENGTH];
	
	struct item item;
	// This data is required for searching, as itemdb is not read by char server
	char item_name[ITEM_NAME_LENGTH];
	short type;

	unsigned short hours;
	int price, buynow;
	time_t timestamp; // auction's end time
	int auction_end_timer;
};

struct registry {
	int global_num;
	struct global_reg global[GLOBAL_REG_NUM];
	int account_num;
	struct global_reg account[ACCOUNT_REG_NUM];
	int account2_num;
	struct global_reg account2[ACCOUNT_REG2_NUM];
};

struct party_member {
	uint32 account_id;
	uint32 char_id;
	char name[NAME_LENGTH];
	unsigned short class_;
	unsigned short map;
	unsigned short lv;
	unsigned leader : 1,
	         online : 1;
};

struct party {
	int party_id;
	char name[NAME_LENGTH];
	unsigned char count; //Count of online characters.
	unsigned exp : 1,
				item : 2; //&1: Party-Share (round-robin), &2: pickup style: shared.
	struct party_member member[MAX_PARTY];
};

struct map_session_data;
struct guild_member {
	uint32 account_id, char_id;
	short hair,hair_color,gender,class_,lv;
	uint64 exp;
	int exp_payper;
	short online,position;
	int last_login;
	char name[NAME_LENGTH];
	struct map_session_data *sd;
	unsigned char modified;
};

struct guild_position {
	char name[NAME_LENGTH];
	int mode;
	int exp_mode;
	unsigned char modified;
};

struct guild_alliance {
	int opposition;
	int guild_id;
	char name[NAME_LENGTH];
};

struct guild_expulsion {
	char name[NAME_LENGTH];
	char mes[40];
	uint32 account_id;
};

struct guild_skill {
	int id,lv;
};

struct guild {
	int guild_id;
	short guild_lv, connect_member, max_member, average_lv;
	uint64 exp;
	unsigned int next_exp;
	int skill_point;
	char name[NAME_LENGTH],master[NAME_LENGTH];
	struct guild_member member[MAX_GUILD];
	struct guild_position position[MAX_GUILDPOSITION];
	char mes1[MAX_GUILDMES1],mes2[MAX_GUILDMES2];
	int emblem_len,emblem_id;
	char emblem_data[2048];
	struct guild_alliance alliance[MAX_GUILDALLIANCE];
	struct guild_expulsion expulsion[MAX_GUILDEXPULSION];
	struct guild_skill skill[MAX_GUILDSKILL];
	time_t last_leader_change;

	unsigned short save_flag; // for TXT saving
};

struct guild_castle {
	int castle_id;
	int mapindex;
	char castle_name[NAME_LENGTH];
	char castle_event[NAME_LENGTH];
	int guild_id;
	int economy;
	int defense;
	int triggerE;
	int triggerD;
	int nextTime;
	int payTime;
	int createTime;
	int visibleC;
	struct {
		unsigned visible : 1;
		int id; // object id
	} guardian[MAX_GUARDIANS];
	int* temp_guardians; // ids of temporary guardians (mobs)
	int temp_guardians_max;
};

struct fame_list {
	int id;
	int fame;
	char name[NAME_LENGTH];
};

enum {
	GBI_EXP	=1,		// ギルドのEXP
	GBI_GUILDLV,		// ギルドのLv
	GBI_SKILLPOINT,		// ギルドのスキルポイント
	GBI_SKILLLV,		// ギルドスキルLv
};

enum {
	GMI_POSITION	=0,		// メンバーの役職変更
	GMI_EXP,
	GMI_HAIR,
	GMI_HAIR_COLOR,
	GMI_GENDER,
	GMI_CLASS,
	GMI_LEVEL,
	GMI_LAST_LOGIN,
};

enum  e_guild_skill{
	GD_SKILLBASE=10000,
	GD_APPROVAL=10000,
	GD_KAFRACONTRACT=10001,
	GD_GUARDRESEARCH=10002,
	GD_GUARDUP=10003,
	GD_EXTENSION=10004,
	GD_GLORYGUILD=10005,
	GD_LEADERSHIP=10006,
	GD_GLORYWOUNDS=10007,
	GD_SOULCOLD=10008,
	GD_HAWKEYES=10009,
	GD_BATTLEORDER=10010,
	GD_REGENERATION=10011,
	GD_RESTORE=10012,
	GD_EMERGENCYCALL=10013,
	GD_DEVELOPMENT=10014,
	GD_ITEMEMERGENCYCALL=10015,
	GD_GUILD_STORAGE=10016,
	GD_CHARGESHOUT_FLAG = 10017,
	GD_CHARGESHOUT_BEATING = 10018,
	GD_EMERGENCY_MOVE = 10019,
	GD_MAX,
};


//These mark the ID of the jobs, as expected by the client. [Skotlex]
enum e_job {
	JOB_NOVICE = 0,
	JOB_SWORDMAN,
	JOB_MAGE,
	JOB_ARCHER,
	JOB_ACOLYTE,
	JOB_MERCHANT,
	JOB_THIEF,
	JOB_KNIGHT,
	JOB_PRIEST,
	JOB_WIZARD,
	JOB_BLACKSMITH,
	JOB_HUNTER,
	JOB_ASSASSIN,
	JOB_KNIGHT2,
	JOB_CRUSADER,
	JOB_MONK,
	JOB_SAGE,
	JOB_ROGUE,
	JOB_ALCHEMIST,
	JOB_BARD,
	JOB_DANCER,
	JOB_CRUSADER2,
	JOB_WEDDING,
	JOB_SUPER_NOVICE,
	JOB_GUNSLINGER,
	JOB_NINJA,
	JOB_XMAS,
	JOB_SUMMER,
	JOB_HANBOK,
	JOB_OKTOBERFEST,
	JOB_SUMMER2,
	JOB_MAX_BASIC,

	JOB_NOVICE_HIGH = 4001,
	JOB_SWORDMAN_HIGH,
	JOB_MAGE_HIGH,
	JOB_ARCHER_HIGH,
	JOB_ACOLYTE_HIGH,
	JOB_MERCHANT_HIGH,
	JOB_THIEF_HIGH,
	JOB_LORD_KNIGHT,
	JOB_HIGH_PRIEST,
	JOB_HIGH_WIZARD,
	JOB_WHITESMITH,
	JOB_SNIPER,
	JOB_ASSASSIN_CROSS,
	JOB_LORD_KNIGHT2,
	JOB_PALADIN,
	JOB_CHAMPION,
	JOB_PROFESSOR,
	JOB_STALKER,
	JOB_CREATOR,
	JOB_CLOWN,
	JOB_GYPSY,
	JOB_PALADIN2,

	JOB_BABY,
	JOB_BABY_SWORDMAN,
	JOB_BABY_MAGE,
	JOB_BABY_ARCHER,
	JOB_BABY_ACOLYTE,
	JOB_BABY_MERCHANT,
	JOB_BABY_THIEF,
	JOB_BABY_KNIGHT,
	JOB_BABY_PRIEST,
	JOB_BABY_WIZARD,
	JOB_BABY_BLACKSMITH,
	JOB_BABY_HUNTER,
	JOB_BABY_ASSASSIN,
	JOB_BABY_KNIGHT2,
	JOB_BABY_CRUSADER,
	JOB_BABY_MONK,
	JOB_BABY_SAGE,
	JOB_BABY_ROGUE,
	JOB_BABY_ALCHEMIST,
	JOB_BABY_BARD,
	JOB_BABY_DANCER,
	JOB_BABY_CRUSADER2,
	JOB_SUPER_BABY,

	JOB_TAEKWON,
	JOB_STAR_GLADIATOR,
	JOB_STAR_GLADIATOR2,
	JOB_SOUL_LINKER,
	
	JOB_GANGSI,
	JOB_DEATH_KNIGHT,
	JOB_DARK_COLLECTOR,

	JOB_RUNE_KNIGHT = 4054,
	JOB_WARLOCK,
	JOB_RANGER,
	JOB_ARCH_BISHOP,
	JOB_MECHANIC,
	JOB_GUILLOTINE_CROSS,
	
	JOB_RUNE_KNIGHT_T,
	JOB_WARLOCK_T,
	JOB_RANGER_T,
	JOB_ARCH_BISHOP_T,
	JOB_MECHANIC_T,
	JOB_GUILLOTINE_CROSS_T,

	JOB_ROYAL_GUARD,
	JOB_SORCERER,
	JOB_MINSTREL,
	JOB_WANDERER,
	JOB_SURA,
	JOB_GENETIC,
	JOB_SHADOW_CHASER,
	
	JOB_ROYAL_GUARD_T,
	JOB_SORCERER_T,
	JOB_MINSTREL_T,
	JOB_WANDERER_T,
	JOB_SURA_T,
	JOB_GENETIC_T,
	JOB_SHADOW_CHASER_T,
	
	JOB_RUNE_KNIGHT2,
	JOB_RUNE_KNIGHT_T2,
	JOB_ROYAL_GUARD2,
	JOB_ROYAL_GUARD_T2,
	JOB_RANGER2,
	JOB_RANGER_T2,
	JOB_MECHANIC2,
	JOB_MECHANIC_T2,
	JOB_RUNE_KNIGHT3,
	JOB_RUNE_KNIGHT_T3,
	JOB_RUNE_KNIGHT4,
	JOB_RUNE_KNIGHT_T4,
	JOB_RUNE_KNIGHT5,
	JOB_RUNE_KNIGHT_T5,
	JOB_RUNE_KNIGHT6,
	JOB_RUNE_KNIGHT_T6,
	
	JOB_BABY_RUNE_KNIGHT,
	JOB_BABY_WARLOCK,
	JOB_BABY_RANGER,
	JOB_BABY_ARCH_BISHOP,
	JOB_BABY_MECHANIC,
	JOB_BABY_GUILLOTINE_CROSS,
	JOB_BABY_ROYAL_GUARD,
	JOB_BABY_SORCERER,
	JOB_BABY_MINSTREL,
	JOB_BABY_WANDERER,
	JOB_BABY_SURA,
	JOB_BABY_GENETIC,
	JOB_BABY_SHADOW_CHASER,
	
	JOB_BABY_RUNE_KNIGHT2,
	JOB_BABY_ROYAL_GUARD2,
	JOB_BABY_RANGER2,
	JOB_BABY_MECHANIC2,
 	
	JOB_SUPER_NOVICE_E = 4190,
	JOB_SUPER_BABY_E,
	
	JOB_KAGEROU = 4211,
	JOB_OBORO,

	JOB_REBELLION = 4215,

	JOB_SUMMONER = 4218,

	JOB_BABY_SUMMONER = 4220,

	JOB_BABY_NINJA = 4222,
	JOB_BABY_KAGEROU,
	JOB_BABY_OBORO,
	JOB_BABY_TAEKWON,
	JOB_BABY_STAR_GLADIATOR,
	JOB_BABY_SOUL_LINKER,
	JOB_BABY_GUNSLINGER,
	JOB_BABY_REBELLION,

	JOB_BABY_STAR_GLADIATOR2 = 4238,

	JOB_STAR_EMPEROR,
	JOB_SOUL_REAPER,
	JOB_BABY_STAR_EMPEROR,
	JOB_BABY_SOUL_REAPER,
	JOB_STAR_EMPEROR2,
	JOB_BABY_STAR_EMPEROR2,

	JOB_MAX,
};

enum {
	SEX_FEMALE = 0,
	SEX_MALE,
	SEX_SERVER,
	SEX_ACCOUNT = 99
};

/// Item Bound Type
enum bound_type {
	BOUND_NONE = 0, /// No bound
	BOUND_ACCOUNT, /// 1- Account Bound
	BOUND_GUILD, /// 2 - Guild Bound
	BOUND_PARTY, /// 3 - Party Bound
	BOUND_CHAR, /// 4 - Character Bound
	BOUND_MAX,

	BOUND_ONEQUIP = 1, ///< Show notification when item will be bound on equip
	BOUND_DISPYELLOW = 2, /// Shows the item name in yellow color
};

enum e_party_member_withdraw {
	PARTY_MEMBER_WITHDRAW_LEAVE,	  ///< /leave
	PARTY_MEMBER_WITHDRAW_EXPEL,	  ///< Kicked
	PARTY_MEMBER_WITHDRAW_CANT_LEAVE, ///< TODO: Cannot /leave
	PARTY_MEMBER_WITHDRAW_CANT_EXPEL, ///< TODO: Cannot be kicked
};

struct clan_alliance {
	int opposition;
	int clan_id;
	char name[NAME_LENGTH];
};

struct clan {
	int id;
	char name[NAME_LENGTH];
	char master[NAME_LENGTH];
	char map[MAP_NAME_LENGTH_EXT];
	short max_member, connect_member;
	struct map_session_data *members[MAX_CLAN];
	struct clan_alliance alliance[MAX_CLANALLIANCE];
};

// Race values sent by client for character creation.
enum {
	RACE_HUMAN = 0,
	RACE_DORAM = 4218
};

// sanity checks...
#if MAX_ZENY > INT_MAX
#error MAX_ZENY is too big
#endif

#ifdef PACKET_OBFUSCATION
	#if PACKETVER < 20110817
		#undef PACKET_OBFUSCATION
	#endif
#endif

#endif /* _MMO_H_ */
