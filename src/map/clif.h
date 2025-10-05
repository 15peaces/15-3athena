// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _CLIF_H_
#define _CLIF_H_

#include "../common/cbasetypes.h"
#include "../common/mmo.h"
#include "packets.h"

struct item;
struct s_storage;
struct block_list;
struct unit_data;
struct map_session_data;
struct homun_data;
struct pet_data;
struct mob_data;
struct npc_data;
struct chat_data;
struct flooritem_data;
struct skill_unit;
struct s_vending;
struct party;
struct party_data;
struct guild;
struct clan;
struct battleground_data;
struct quest;
struct party_booking_ad_info;
struct achievement;

#define P2PTR(fd) RFIFO2PTR(fd)

#include <stdarg.h>

#define RGB2BGR(c) ((c & 0x0000FF) << 16 | (c & 0x00FF00) | (c & 0xFF0000) >> 16)

#define COLOR_RED     0xff0000U
#define COLOR_GREEN   0x00ff00U
#define COLOR_CYAN    0x00FFFFU
#define COLOR_WHITE   0xffffffU
#define COLOR_YELLOW  0xffff00U
#define COLOR_DEFAULT COLOR_GREEN

enum
{// packet DB
	MIN_PACKET_DB  = 0x064,
	MAX_PACKET_DB  = 0xC00,
	MAX_PACKET_VER = 46,
	MAX_PACKET_POS = 20,
};

enum e_packet_ack {
	ZC_ACK_OPEN_BANKING = 0,
	ZC_ACK_BANKING_DEPOSIT,
	ZC_ACK_BANKING_WITHDRAW,
	ZC_BANKING_CHECK,
	ZC_WEAR_EQUIP_ACK,
	ZC_BROADCASTING_SPECIAL_ITEM_OBTAIN,
	ZC_CLEAR_DIALOG,
	ZC_NOTIFY_BIND_ON_EQUIP,
	ZC_MERGE_ITEM_OPEN,
	ZC_ACK_MERGE_ITEM,
	MAX_ACK_FUNC //auto upd len
};

struct s_packet_db {
	short len;
	void (*func)(int, struct map_session_data *);
	short pos[MAX_PACKET_POS];
};

#ifdef PACKET_OBFUSCATION
/// Keys based on packet versions
struct s_packet_keys {
	unsigned int keys[3]; ///< 3-Keys
};
#endif

enum BATTLEGROUNDS_QUEUE_NOTICE_DELETED {
	BGQND_CLOSEWINDOW = 1,
	BGQND_FAIL_BGNAME_WRONG = 3,
	BGQND_FAIL_NOT_QUEUING = 11,
};

enum e_BANKING_DEPOSIT_ACK {
	BDA_SUCCESS  = 0x0,
	BDA_ERROR    = 0x1,
	BDA_NO_MONEY = 0x2,
	BDA_OVERFLOW = 0x3,
};
 
enum e_BANKING_WITHDRAW_ACK {
	BWA_SUCCESS       = 0x0,
	BWA_NO_MONEY      = 0x1,
	BWA_UNKNOWN_ERROR = 0x2,
};

enum RECV_ROULETTE_ITEM_REQ {
	RECV_ITEM_SUCCESS    = 0x0,
	RECV_ITEM_FAILED     = 0x1,
	RECV_ITEM_OVERCOUNT  = 0x2,
	RECV_ITEM_OVERWEIGHT = 0x3,
};

enum RECV_ROULETTE_ITEM_ACK {
	RECV_ITEM_NORMAL =  0x0,
	RECV_ITEM_LOSING =  0x1,
};

enum GENERATE_ROULETTE_ACK {
	GENERATE_ROULETTE_SUCCESS         = 0x0,
	GENERATE_ROULETTE_FAILED          = 0x1,
	GENERATE_ROULETTE_NO_ENOUGH_POINT = 0x2,
	GENERATE_ROULETTE_LOSING          = 0x3,
};

enum OPEN_ROULETTE_ACK {
	OPEN_ROULETTE_SUCCESS = 0x0,
	OPEN_ROULETTE_FAILED  = 0x1,
};

enum CLOSE_ROULETTE_ACK {
	CLOSE_ROULETTE_SUCCESS = 0x0,
	CLOSE_ROULETTE_FAILED  = 0x1,
};

enum MERGE_ITEM_ACK {
	MERGE_ITEM_SUCCESS = 0x0,
	MERGE_ITEM_FAILED_NOT_MERGE = 0x1,
	MERGE_ITEM_FAILED_MAX_COUNT = 0x2,
};

enum e_wip_block {
	WIP_DISABLE_NONE = 0x0,
	WIP_DISABLE_SKILLITEM = 0x1,
	WIP_DISABLE_NPC = 0x2,
	WIP_DISABLE_ALL = 0x3,
};

enum BROADCASTING_SPECIAL_ITEM_OBTAIN {
	ITEMOBTAIN_TYPE_BOXITEM =  0x0,
	ITEMOBTAIN_TYPE_MONSTER_ITEM =  0x1,
	ITEMOBTAIN_TYPE_NPC =  0x2,
};

typedef enum broadcast_flags {
	BC_ALL = 0,
	BC_MAP = 1,
	BC_AREA = 2,
	BC_SELF = 3,
	BC_TARGET_MASK = 0x07,

	BC_PC = 0x00,
	BC_NPC = 0x08,
	BC_SOURCE_MASK = 0x08, // BC_PC|BC_NPC

	BC_YELLOW = 0x00,
	BC_BLUE = 0x10,
	BC_WOE = 0x20,
	BC_COLOR_MASK = 0x30, // BC_YELLOW|BC_BLUE|BC_WOE

	BC_MEGAPHONE = 0x40,

	BC_DEFAULT = BC_ALL | BC_PC | BC_YELLOW
} broadcast_flags;

enum e_adopt_reply {
	ADOPT_REPLY_MORE_CHILDREN = 0,
	ADOPT_REPLY_LEVEL_70,
	ADOPT_REPLY_MARRIED,
};

/**
* Private Airship Responds
**/
enum private_airship {
	P_AIRSHIP_NONE,
	P_AIRSHIP_RETRY,
	P_AIRSHIP_ITEM_NOT_ENOUGH,
	P_AIRSHIP_INVALID_END_MAP,
	P_AIRSHIP_INVALID_START_MAP,
	P_AIRSHIP_ITEM_INVALID
};

/** Pet Evolution Results */
enum pet_evolution_result {
	PET_EVOL_UNKNOWN = 0x0,
	PET_EVOL_NO_CALLPET = 0x1,
	PET_EVOL_NO_PETEGG = 0x2,
	PET_EVOL_NO_RECIPE = 0x3,
	PET_EVOL_NO_MATERIAL = 0x4,
	PET_EVOL_RG_FAMILIAR = 0x5,
	PET_EVOL_SUCCESS = 0x6,
};

// packet_db[SERVER] is reserved for server use
#define SERVER 0
#define packet_len(cmd) packet_db[SERVER][cmd].len
extern struct s_packet_db packet_db[MAX_PACKET_VER+1][MAX_PACKET_DB+1];
extern int packet_db_ack[MAX_PACKET_VER + 1][MAX_ACK_FUNC + 1];

// local define
typedef enum send_target {
	ALL_CLIENT,
	ALL_SAMEMAP,
	AREA,				// area
	AREA_WOS,			// area, without self
	AREA_WOC,			// area, without chatrooms
	AREA_WOSC,			// area, without own chatroom
	AREA_CHAT_WOC,		// hearable area, without chatrooms
	CHAT,				// current chatroom
	CHAT_WOS,			// current chatroom, without self
	PARTY,
	PARTY_WOS,
	PARTY_SAMEMAP,
	PARTY_SAMEMAP_WOS,
	PARTY_AREA,
	PARTY_AREA_WOS,
	GUILD,
	GUILD_WOS,
	GUILD_SAMEMAP,
	GUILD_SAMEMAP_WOS,
	GUILD_AREA,
	GUILD_AREA_WOS,
	GUILD_NOBG,
	DUEL,
	DUEL_WOS,
	CHAT_MAINCHAT,		// everyone on main chat
	SELF,
	BG,					// BattleGround System
	BG_WOS,
	BG_SAMEMAP,
	BG_SAMEMAP_WOS,
	BG_AREA,
	BG_AREA_WOS,
	BG_QUEUE,
	CLAN,
} send_target;

typedef enum emotion_type
{
	E_GASP = 0,     // /!
	E_WHAT,         // /?
	E_HO,
	E_LV,
	E_SWT,
	E_IC,
	E_AN,			// ET_FRET
	E_AG,
	E_CASH,         // /$
	E_DOTS,         // /...
	E_SCISSORS,     // /gawi --- 10
	E_ROCK,         // /bawi
	E_PAPER,        // /bo
	E_KOREA,
	E_LV2,
	E_THX,
	E_WAH,
	E_SRY,
	E_HEH,
	E_SWT2,
	E_HMM,          // --- 20
	E_NO1,
	E_NO,           // /??
	E_OMG,
	E_OH,
	E_X,
	E_HLP,
	E_GO,
	E_SOB,
	E_GG,
	E_KIS,          // --- 30
	E_KIS2,
	E_PIF,
	E_OK,
	E_MUTE,         // red /... used for muted characters
	E_INDONESIA,
	E_BZZ,          // /bzz, /stare
	E_RICE,
	E_AWSM,         // /awsm, /cool
	E_MEH,
	E_SHY,          // --- 40
	E_PAT,          // /pat, /goodboy
	E_MP,           // /mp, /sptime
	E_SLUR,
	E_COM,          // /com, /comeon
	E_YAWN,         // /yawn, /sleepy
	E_GRAT,         // /grat, /congrats
	E_HP,           // /hp, /hptime
	E_PHILIPPINES,
	E_MALAYSIA,
	E_SINGAPORE,    // --- 50
	E_BRAZIL,
	E_FLASH,        // /fsh
	E_SPIN,         // /spin
	E_SIGH,
	E_DUM,          // /dum
	E_LOUD,         // /crwd
	E_OTL,          // /otl, /desp
	E_DICE1,
	E_DICE2,
	E_DICE3,        // --- 60
	E_DICE4,
	E_DICE5,
	E_DICE6,
	E_INDIA,
	E_LUV,          // /love
	E_RUSSIA,
	E_VIRGIN,
	E_MOBILE,
	E_MAIL,
	E_CHINESE,      // --- 70
	E_ANTENNA1,
	E_ANTENNA2,
	E_ANTENNA3,
	E_HUM,
	E_ABS,
	E_OOPS,
	E_SPIT,
	E_ENE,
	E_PANIC,
	E_WHISP,        // --- 80
	E_YUT1, // /dbc
	E_YUT2,
	E_YUT3,
	E_YUT4,
	E_YUT5,
	E_YUT6,
	E_YUT7,
	//
	E_MAX
} emotion_type;

typedef enum clr_type
{
	CLR_OUTSIGHT = 0,
	CLR_DEAD,
	CLR_RESPAWN,
	CLR_TELEPORT,
	CLR_TRICKDEAD,
} clr_type;

enum map_property
{// clif_map_property
	MAPPROPERTY_NOTHING       = 0,
	MAPPROPERTY_FREEPVPZONE   = 1,
	MAPPROPERTY_EVENTPVPZONE  = 2,
	MAPPROPERTY_AGITZONE      = 3,
	MAPPROPERTY_PKSERVERZONE  = 4, // message "You are in a PK area. Please beware of sudden attacks." in color 0x9B9BFF (light red)
	MAPPROPERTY_PVPSERVERZONE = 5,
	MAPPROPERTY_DENYSKILLZONE = 6,
};

enum map_type
{// clif_map_type
	MAPTYPE_VILLAGE					= 0,
	MAPTYPE_VILLAGE_IN				= 1,
	MAPTYPE_FIELD					= 2,
	MAPTYPE_DUNGEON					= 3,
	MAPTYPE_ARENA					= 4,
	MAPTYPE_PENALTY_FREEPKZONE		= 5,
	MAPTYPE_NOPENALTY_FREEPKZONE	= 6,
	MAPTYPE_EVENT_GUILDWAR			= 7,
	MAPTYPE_AGIT					= 8,
	MAPTYPE_DUNGEON2				= 9,
	MAPTYPE_DUNGEON3				= 10,
	MAPTYPE_PKSERVER				= 11,
	MAPTYPE_PVPSERVER				= 12,
	MAPTYPE_DENYSKILL				= 13,
	MAPTYPE_TURBOTRACK				= 14,
	MAPTYPE_JAIL					= 15,
	MAPTYPE_MONSTERTRACK			= 16,
	MAPTYPE_PORINGBATTLE			= 17,
	MAPTYPE_AGIT_SIEGEV15			= 18,
	MAPTYPE_BATTLEFIELD				= 19,
	MAPTYPE_PVP_TOURNAMENT			= 20,
	//Map type ID's 21 - 24 are unknown or unused.
	MAPTYPE_SIEGE_LOWLEVEL			= 25,
	MAPTYPE_2012_RWC_BATTLE_FIELD	= 26,
	//Map type ID's 27 - 28 are unknown or unused.
	MAPTYPE_UNUSED					= 29,
};

enum useskill_fail_cause
{// clif_skill_fail
	USESKILL_FAIL_LEVEL = 0,
	USESKILL_FAIL_SP_INSUFFICIENT = 1,
	USESKILL_FAIL_HP_INSUFFICIENT = 2,
	USESKILL_FAIL_STUFF_INSUFFICIENT = 3,
	USESKILL_FAIL_SKILLINTERVAL = 4,
	USESKILL_FAIL_MONEY = 5,
	USESKILL_FAIL_THIS_WEAPON = 6,
	USESKILL_FAIL_REDJAMSTONE = 7,
	USESKILL_FAIL_BLUEJAMSTONE = 8,
	USESKILL_FAIL_WEIGHTOVER = 9,
	USESKILL_FAIL = 10,
	USESKILL_FAIL_TOTARGET = 11,
	USESKILL_FAIL_ANCILLA_NUMOVER = 12,
	USESKILL_FAIL_HOLYWATER = 13,
	USESKILL_FAIL_ANCILLA = 14,
	USESKILL_FAIL_DUPLICATE_RANGEIN = 15,
	USESKILL_FAIL_NEED_OTHER_SKILL = 16,
	USESKILL_FAIL_NEED_HELPER = 17,
	USESKILL_FAIL_INVALID_DIR = 18,
	USESKILL_FAIL_SUMMON = 19,
	USESKILL_FAIL_SUMMON_NONE = 20,
	USESKILL_FAIL_IMITATION_SKILL_NONE = 21,
	USESKILL_FAIL_DUPLICATE = 22,
	USESKILL_FAIL_CONDITION = 23,
	USESKILL_FAIL_PAINTBRUSH = 24,
	USESKILL_FAIL_DRAGON = 25,
	USESKILL_FAIL_POS = 26,
	USESKILL_FAIL_HELPER_SP_INSUFFICIENT = 27,
	USESKILL_FAIL_NEER_WALL = 28,
	USESKILL_FAIL_NEED_EXP_1PERCENT = 29,
	USESKILL_FAIL_CHORUS_SP_INSUFFICIENT = 30,
	USESKILL_FAIL_GC_WEAPONBLOCKING = 31,
	USESKILL_FAIL_GC_POISONINGWEAPON = 32,
	USESKILL_FAIL_MADOGEAR = 33,
	USESKILL_FAIL_NEED_EQUIPMENT_KUNAI = 34,
	USESKILL_FAIL_TOTARGET_PLAYER = 35,
	USESKILL_FAIL_SIZE = 36,
	USESKILL_FAIL_CANONBALL = 37,
	//XXX_USESKILL_FAIL_II_MADOGEAR_ACCELERATION = 38,
	//XXX_USESKILL_FAIL_II_MADOGEAR_HOVERING_BOOSTER = 39,
	USESKILL_FAIL_MADOGEAR_HOVERING = 40,
	//XXX_USESKILL_FAIL_II_MADOGEAR_SELFDESTRUCTION_DEVICE = 41,
	//XXX_USESKILL_FAIL_II_MADOGEAR_SHAPESHIFTER = 42,
	USESKILL_FAIL_GUILLONTINE_POISON = 43,
	//XXX_USESKILL_FAIL_II_MADOGEAR_COOLING_DEVICE = 44,
	//XXX_USESKILL_FAIL_II_MADOGEAR_MAGNETICFIELD_GENERATOR = 45,
	//XXX_USESKILL_FAIL_II_MADOGEAR_BARRIER_GENERATOR = 46,
	//XXX_USESKILL_FAIL_II_MADOGEAR_OPTICALCAMOUFLAGE_GENERATOR = 47,
	//XXX_USESKILL_FAIL_II_MADOGEAR_REPAIRKIT = 48,
	//XXX_USESKILL_FAIL_II_MONKEY_SPANNER = 49,
	USESKILL_FAIL_MADOGEAR_RIDE = 50,
	USESKILL_FAIL_SPELLBOOK = 51,
	USESKILL_FAIL_SPELLBOOK_DIFFICULT_SLEEP = 52,
	USESKILL_FAIL_SPELLBOOK_PRESERVATION_POINT = 53,
	USESKILL_FAIL_SPELLBOOK_READING = 54,
	//XXX_USESKILL_FAIL_II_FACE_PAINTS = 55,
	//XXX_USESKILL_FAIL_II_MAKEUP_BRUSH = 56,
	USESKILL_FAIL_CART = 57,
	//XXX_USESKILL_FAIL_II_THORNS_SEED = 58,
	//XXX_USESKILL_FAIL_II_BLOOD_SUCKER_SEED = 59,
	USESKILL_FAIL_NO_MORE_SPELL = 60,
	//XXX_USESKILL_FAIL_II_BOMB_MUSHROOM_SPORE = 61,
	//XXX_USESKILL_FAIL_II_GASOLINE_BOOMB = 62,
	//XXX_USESKILL_FAIL_II_OIL_BOTTLE = 63,
	//XXX_USESKILL_FAIL_II_EXPLOSION_POWDER = 64,
	//XXX_USESKILL_FAIL_II_SMOKE_POWDER = 65,
	//XXX_USESKILL_FAIL_II_TEAR_GAS = 66,
	//XXX_USESKILL_FAIL_II_HYDROCHLORIC_ACID_BOTTLE = 67,
	//XXX_USESKILL_FAIL_II_HELLS_PLANT_BOTTLE = 68,
	//XXX_USESKILL_FAIL_II_MANDRAGORA_FLOWERPOT = 69,
	USESKILL_FAIL_MANUAL_NOTIFY = 70,
	USESKILL_FAIL_NEED_ITEM = 71,
	USESKILL_FAIL_NEED_EQUIPMENT = 72,
	USESKILL_FAIL_COMBOSKILL = 73,
	USESKILL_FAIL_SPIRITS = 74,
	USESKILL_FAIL_EXPLOSIONSPIRITS = 75,
	USESKILL_FAIL_HP_TOOMANY = 76,
	USESKILL_FAIL_NEED_ROYAL_GUARD_BANDING = 77,
	USESKILL_FAIL_NEED_EQUIPPED_WEAPON_CLASS = 78,
	USESKILL_FAIL_EL_SUMMON = 79,
	USESKILL_FAIL_RELATIONGRADE = 80,
	USESKILL_FAIL_STYLE_CHANGE_FIGHTER = 81,
	USESKILL_FAIL_STYLE_CHANGE_GRAPPLER = 82,
	USESKILL_FAIL_THERE_ARE_NPC_AROUND = 83,
	USESKILL_FAIL_NEED_MORE_BULLET = 84,
	USESKILL_FAIL_COINS = 85,
	USESKILL_FAIL_MSG = 86,
	USESKILL_FAIL_MAP = 87,
	USESKILL_FAIL_SUMMON_SP_INSUFFICIENT = 88,

	USESKILL_FAIL_MAX
};

enum clif_messages {
	/* Constant values */
	// clif_cart_additem_ack flags
	ADDITEM_TO_CART_FAIL_WEIGHT = 0x0,
	ADDITEM_TO_CART_FAIL_COUNT = 0x1,

	// clif_equipitemack flags
#if PACKETVER >= 20121205
	ITEM_EQUIP_ACK_OK = 0,
	ITEM_EQUIP_ACK_FAIL = 2,
	ITEM_EQUIP_ACK_FAILLEVEL = 1,
#else
	ITEM_EQUIP_ACK_OK = 1,
	ITEM_EQUIP_ACK_FAIL = 0,
	ITEM_EQUIP_ACK_FAILLEVEL = 0,
#endif
	/* -end- */

	//! NOTE: These values below need client version validation
	ITEM_CANT_OBTAIN_WEIGHT = 0x34, /* You cannot carry more items because you are overweight. */
	ITEM_NOUSE_SITTING = 0x297,
	ITEM_PARTY_MEMBER_NOT_SUMMONED = 0x4c5, ///< "The party member was not summoned because you are not the party leader."
	ITEM_PARTY_NO_MEMBER_IN_MAP = 0x4c6, ///< "There is no party member to summon in the current map."
	SKILL_CANT_USE_AREA = 0x536,
	ITEM_CANT_USE_AREA = 0x537,
	VIEW_EQUIP_FAIL = 0x54d,
	RUNE_CANT_CREATE = 0x61b,
	ITEM_CANT_COMBINE = 0x623,
	INVENTORY_SPACE_FULL = 0x625,
	ITEM_PRODUCE_SUCCESS = 0x627,
	ITEM_PRODUCE_FAIL = 0x628,
	ITEM_UNIDENTIFIED = 0x62d,
	ITEM_REUSE_LIMIT = 0x746,
	WORK_IN_PROGRESS = 0x783,
	NEED_REINS_OF_MOUNT = 0x78c,
	MERGE_ITEM_NOT_AVAILABLE = 0x887,

	SKILL_NEED_GATLING = 0x9fa,
	SKILL_NEED_SHOTGUN = 0x9fb,
	SKILL_NEED_RIFLE = 0x9fc,
	SKILL_NEED_REVOLVER = 0x9fd,
	SKILL_NEED_HOLY_BULLET = 0x9fe,
	SKILL_NEED_GRENADE = 0xa01,

	GUILD_MASTER_WOE = 0xb93, /// <"Currently in WoE hours, unable to delegate Guild leader"
	GUILD_MASTER_DELAY = 0xb94, /// <"You have to wait for one day before delegating a new Guild leader"

	MSG_ATTENDANCE_UNAVAILABLE = 0xd92, ///< Attendance Check failed. Please try again later.

	// Unofficial names
	C_ITEM_EQUIP_SWITCH = 0xbc7,
};

enum CASH_SHOP_TABS {
	CASHSHOP_TAB_NEW = 0,
	CASHSHOP_TAB_POPULAR = 1,
	CASHSHOP_TAB_LIMITED = 2,
	CASHSHOP_TAB_RENTAL = 3,
	CASHSHOP_TAB_PERPETUITY = 4,
	CASHSHOP_TAB_BUFF = 5,
	CASHSHOP_TAB_RECOVERY = 6,
	CASHSHOP_TAB_ETC = 7,
	CASHSHOP_TAB_MAX,
};

enum CASH_SHOP_BUY_RESULT {
	CSBR_SUCCESS = 0x0,
	CSBR_SHORTTAGE_CASH = 0x2,
	CSBR_UNKONWN_ITEM = 0x3,
	CSBR_INVENTORY_WEIGHT = 0x4,
	CSBR_INVENTORY_ITEMCNT = 0x5,
	CSBR_RUNE_OVERCOUNT = 0x9,
	CSBR_EACHITEM_OVERCOUNT = 0xa,
	CSBR_UNKNOWN = 0xb,
};

enum BATTLEGROUNDS_QUEUE_ACK {
	BGQA_SUCCESS = 1,
	BGQA_FAIL_QUEUING_FINISHED,
	BGQA_FAIL_BGNAME_INVALID,
	BGQA_FAIL_TYPE_INVALID,
	BGQA_FAIL_PPL_OVERAMOUNT,
	BGQA_FAIL_LEVEL_INCORRECT,
	BGQA_DUPLICATE_REQUEST,
	BGQA_PLEASE_RELOGIN,
	BGQA_NOT_PARTY_GUILD_LEADER,
	BGQA_FAIL_CLASS_INVALID,
	/* not official way to respond (gotta find packet?) */
	BGQA_FAIL_DESERTER,
	BGQA_FAIL_COOLDOWN,
	BGQA_FAIL_TEAM_COUNT,
};

enum ranking_type
{
	RANKING_BLACKSMITH = 0,
	RANKING_ALCHEMIST,
	RANKING_TAEKWON,
	RANKING_KILLER,
	RANKING_BATTLE_1VS1_ALL,
	RANKING_BATTLE_7VS7_ALL,
	RANKING_RUNE_KNIGHT,
	RANKING_WARLOCK,
	RANKING_RANGER,
	RANKING_MECHANIC,
	RANKING_GUILLOTINE_CROSS,
	RANKING_ARCHBISHOP,
	RANKING_ROYAL_GUARD,
	RANKING_SORCERER,
	RANKING_MINSTREL,
	RANKING_WANDERER,
	RANKING_GENETIC,
	RANKING_SHADOW_CHASER,
	RANKING_SURA,
	RANKING_KAGEROU,
	RANKING_OBORO,
	// Rebellion isnt confirmed on the ranking list yet,
	// but if Kagerou and Oboro are on here, Rebellion
	// should be too.
	RANKING_REBELLION,
};

enum e_damage_type {
	DMG_NORMAL = 0,			/// damage [ damage: total damage, div: amount of hits, damage2: assassin dual-wield damage ]
	DMG_PICKUP_ITEM,		/// pick up item
	DMG_SIT_DOWN,			/// sit down
	DMG_STAND_UP,			/// stand up
	DMG_ENDURE,				/// damage (endure)
	DMG_SPLASH,				/// (splash?)
	DMG_SINGLE,				/// (skill?)
	DMG_REPEAT,				/// (repeat damage?)
	DMG_MULTI_HIT,			/// multi-hit damage
	DMG_MULTI_HIT_ENDURE,	/// multi-hit damage (endure)
	DMG_CRITICAL,			/// critical hit
	DMG_LUCY_DODGE,			/// lucky dodge
	DMG_TOUCH,				/// (touch skill?)
	DMG_MULTI_HIT_CRITICAL  /// multi-hit with critical
};

/**
* Receive configuration types
**/
enum CZ_CONFIG {
	CZ_CONFIG_OPEN_EQUIPMENT_WINDOW = 0,
	CZ_CONFIG_CALL = 1,
	CZ_CONFIG_PET_AUTOFEEDING = 2,
	CZ_CONFIG_HOMUNCULUS_AUTOFEEDING = 3,
};

enum lapineddukddak_result {
	LAPINEDDKUKDDAK_SUCCESS = 0,
	LAPINEDDKUKDDAK_INSUFFICIENT_AMOUNT = 5,
	LAPINEDDKUKDDAK_INVALID_ITEM = 7,
};

/// Attendance System
enum in_ui_type {
	IN_UI_ATTENDANCE = 5
};

enum out_ui_type {
	OUT_UI_ATTENDANCE = 7
};

/**
 * Convex Mirror (ZC_BOSS_INFO)
 **/
enum bossmap_info_type {
	BOSS_INFO_NONE = 0,      // No Boss within the map
	BOSS_INFO_ALIVE,         // Boss is still alive
	BOSS_INFO_ALIVE_WITHMSG, // Boss is alive (on item use)
	BOSS_INFO_DEAD,          // Boss is dead
};

enum memorial_dungeon_command {
	COMMAND_MEMORIALDUNGEON_DESTROY_FORCE = 0x3,
};

struct cash_item_data {
	t_itemid id;
	uint32 price;
};

struct cash_item_db {
	struct cash_item_data** item;
	uint32 count;
};

struct ach_list_info {
	uint32 ach_id;
	uint8 completed;
	uint32 objective[MAX_ACHIEVEMENT_OBJECTIVES];
	uint32 completed_at;
	uint8 reward;
};

//CashShop
struct cash_item_db cash_shop_items[CASHSHOP_TAB_MAX];

int clif_setip(const char* ip);
void clif_setbindip(const char* ip);
void clif_setport(uint16 port);

uint32 clif_getip(void);
uint32 clif_refresh_ip(void);
uint16 clif_getport(void);
void packetdb_readdb(void);

void clif_authok(struct map_session_data *sd);
void clif_authrefuse(int fd, uint8 error_code);
void clif_authfail_fd(int fd, int type);
void clif_charselectok(int id, uint8 ok);
void clif_dropflooritem(struct flooritem_data* fitem, bool canShowEffect);
void clif_clearflooritem(struct flooritem_data *fitem, int fd);

void clif_clearunit_single(int id, clr_type type, int fd);
void clif_clearunit_area(struct block_list* bl, clr_type type);
void clif_clearunit_delayed(struct block_list* bl, clr_type type, int64 tick);
int clif_spawn(struct block_list *bl);	//area
void clif_walkok(struct map_session_data *sd);	// self
void clif_move(struct unit_data *ud); //area
void clif_changemap(struct map_session_data *sd, short m, int x, int y);	//self
void clif_changemapserver(struct map_session_data* sd, unsigned short map_index, int x, int y, uint32 ip, uint16 port);	//self
void clif_blown(struct block_list *bl); // area
void clif_slide(struct block_list *bl, int x, int y); // area
void clif_fixpos(struct block_list *bl);	// area
void clif_npcbuysell(struct map_session_data* sd, int id);	//self
void clif_buylist(struct map_session_data *sd, struct npc_data *nd);	//self
void clif_selllist(struct map_session_data *sd);	//self
void clif_scriptmes(struct map_session_data *sd, int npcid, const char *mes);	//self
void clif_scriptnext(struct map_session_data *sd,int npcid);	//self
void clif_scriptclose(struct map_session_data *sd, int npcid);	//self
void clif_scriptclear(struct map_session_data *sd, int npcid);	//self
void clif_scriptmenu(struct map_session_data* sd, int npcid, const char* mes);	//self
void clif_scriptinput(struct map_session_data *sd, int npcid);	//self
void clif_scriptinputstr(struct map_session_data *sd, int npcid);	// self
void clif_cutin(struct map_session_data* sd, const char* image, int type);	//self
void clif_viewpoint(struct map_session_data *sd, int npc_id, int type, int x, int y, int id, int color);	//self
void clif_additem(struct map_session_data *sd, int n, int amount, unsigned char fail); // self
void clif_dropitem(struct map_session_data *sd,int n,int amount);	//self
void clif_delitem(struct map_session_data *sd,int n,int amount, short reason); //self
int clif_updatestatus(struct map_session_data*,int); //self
void clif_changestatus(struct map_session_data* sd, int type, int val);	//area
void clif_updatelongparam(struct map_session_data* sd, short type, int value);	//self
void clif_updatestatuspointsneeded(struct map_session_data* sd, short type, unsigned char value);	//self
void clif_updatecartinfo(struct map_session_data* sd, short count, short maxcount, int weight, int maxweight);	//self
void clif_updateattackrange(struct map_session_data* sd, short range);	//self
void clif_updatestat(struct map_session_data* sd, int type, int value, int plusvalue); //self
void clif_updateparam_area(struct map_session_data* sd, short type, int value);	//area
int clif_damage(struct block_list* src, struct block_list* dst, int64 tick, int sdelay, int ddelay, int64 sdamage, int div, int type, int64 sdamage2, bool spdamage); // area
void clif_takeitem(struct block_list* src, struct block_list* dst);
void clif_sitting(struct block_list* bl, bool area);
void clif_standing(struct block_list* bl, bool area);
void clif_changelook(struct block_list *bl,int type,int val);	// area
void clif_changetraplook(struct block_list *bl,int val); // area
void clif_refreshlook(struct block_list *bl,int id,int type,int val,enum send_target target); //area specified in 'target'
void clif_arrowequip(struct map_session_data *sd,int val); //self
void clif_arrow_fail(struct map_session_data *sd,int type); //self
void clif_arrow_create_list(struct map_session_data *sd);	//self
void clif_elementalconverter_list(struct map_session_data *sd);
void clif_poison_list(struct map_session_data *sd, uint16 skill_lv);
void clif_magicdecoy_list(struct map_session_data *sd, uint16 skill_lv, short x, short y);
void clif_spellbook_list(struct map_session_data *sd);	//self
int clif_skill_select_request( struct map_session_data *sd ); //self
int clif_skill_itemlistwindow( struct map_session_data *sd, int skill_id, int skill_lv ); //self
void clif_statusupack(struct map_session_data *sd,int type,int ok,int val);	// self
void clif_equipitemack(struct map_session_data *sd,int n,int pos,uint8 flag);	// self
void clif_unequipitemack(struct map_session_data *sd,int n,int pos,int ok);	// self
void clif_misceffect(struct block_list* bl,int type);	// area
void clif_changeoption(struct block_list* bl);	// area
void clif_changeoption_target(struct block_list *bl, struct block_list *target_bl, enum send_target target);
void clif_changeoption2(struct block_list* bl);	// area
void clif_useitemack(struct map_session_data *sd,int index,int amount,bool ok);	// self
void clif_GlobalMessage(struct block_list* bl, const char* message);
void clif_createchat(struct map_session_data* sd, int flag);	// self
void clif_dispchat(struct chat_data* cd, int fd);	// area or fd
void clif_joinchatfail(struct map_session_data *sd,int flag);	// self
void clif_joinchatok(struct map_session_data *sd,struct chat_data* cd);	// self
void clif_addchat(struct chat_data* cd,struct map_session_data *sd);	// chat
void clif_changechatowner(struct chat_data* cd, struct map_session_data* sd);	// chat
void clif_clearchat(struct chat_data *cd,int fd);	// area or fd
void clif_leavechat(struct chat_data* cd, struct map_session_data* sd, bool flag);	// chat
void clif_changechatstatus(struct chat_data* cd);	// chat
void clif_refresh_storagewindow(struct map_session_data *sd);
void clif_refresh(struct map_session_data *sd);	// self

// New Ranking Packets
void clif_ranklist_sub(unsigned char *buf, int type);
void clif_ranklist(struct map_session_data *sd, int type);
void clif_parse_ranklist(int fd, struct map_session_data *sd);
void clif_update_rankingpoint(struct map_session_data *sd, int type, int points);
void clif_fame_blacksmith(struct map_session_data *sd, int points);
void clif_fame_alchemist(struct map_session_data *sd, int points);
void clif_fame_taekwon(struct map_session_data *sd, int points);

void clif_emotion(struct block_list *bl,int type);
void clif_talkiebox(struct block_list* bl, const char* talkie);
void clif_wedding_effect(struct block_list *bl);
void clif_divorced(struct map_session_data* sd, const char* name);
void clif_callpartner(struct map_session_data *sd);
void clif_playBGM(struct map_session_data* sd, const char* name);
void clif_soundeffect(struct map_session_data* sd, struct block_list* bl, const char* name, int type);
void clif_soundeffectall(struct block_list* bl, const char* name, int type, enum send_target coverage);
void clif_parse_ActionRequest_sub(struct map_session_data *sd, int action_type, int target_id, int64 tick);
void clif_parse_LoadEndAck(int fd,struct map_session_data *sd);
void clif_hotkeys_send(struct map_session_data *sd);

// trade
void clif_traderequest(struct map_session_data* sd, const char* name);
void clif_tradestart(struct map_session_data* sd, uint8 type);
void clif_tradeadditem(struct map_session_data* sd, struct map_session_data* tsd, int index, int amount);
void clif_tradeitemok(struct map_session_data* sd, int index, int fail);
void clif_tradedeal_lock(struct map_session_data* sd, int fail);
void clif_tradecancelled(struct map_session_data* sd);
void clif_tradecompleted(struct map_session_data* sd, int fail);
void clif_tradeundo(struct map_session_data* sd);

// storage
void clif_storagelist(struct map_session_data* sd, struct item* items, int items_length, const char *storename);
void clif_updatestorageamount(struct map_session_data* sd, int amount, int max_amount);
void clif_storageitemadded(struct map_session_data* sd, struct item* i, int index, int amount);
void clif_storageitemremoved(struct map_session_data* sd, int index, int amount);
void clif_storageclose(struct map_session_data* sd);

int clif_insight(struct block_list *bl,va_list ap);	// map_forallinmovearea callback
int clif_outsight(struct block_list *bl,va_list ap);	// map_forallinmovearea callback

void clif_class_change_target(struct block_list *bl, int class_, int type, enum send_target target, struct map_session_data *sd);
#define clif_class_change(bl, class_, type) clif_class_change_target(bl, class_, type, AREA, NULL)
#define clif_mob_class_change(md, class_) clif_class_change(&md->bl, class_, 1)

void clif_skillupdateinfoblock(struct map_session_data *sd);
void clif_skillup(struct map_session_data *sd, uint16 skill_id, int lv, int range, int upgradable);
void clif_skillupdateinfo(struct map_session_data *sd,int skill_id,int type,int range);
int clif_hom_skillupdateinfo(struct map_session_data *sd, int skillid, int type, int range);
void clif_addskill(struct map_session_data *sd, int id);
void clif_deleteskill(struct map_session_data *sd, int id);

void clif_skillcasting(struct block_list* bl, int src_id, int dst_id, int dst_x, int dst_y, int skill_id, int property, int casttime);
void clif_skillcastcancel(struct block_list* bl);
void clif_skill_fail(struct map_session_data *sd,int skill_id, enum useskill_fail_cause type,int btype, t_itemid itemId);
void clif_skill_cooldown(struct map_session_data *sd, int skill_id, unsigned int duration);
int clif_skill_damage(struct block_list *src,struct block_list *dst,int64 tick,int sdelay,int ddelay,int64 sdamage,int div,int skill_id,int skill_lv,int type);
//int clif_skill_damage2(struct block_list *src,struct block_list *dst,int64 tick,int sdelay,int ddelay,int damage,int div,int skill_id,int skill_lv,int type);
int clif_skill_nodamage(struct block_list *src,struct block_list *dst,int skill_id,int heal, bool success);
void clif_skill_poseffect(struct block_list *src,int skill_id,int val,int x,int y,int64 tick);
void clif_skill_estimation(struct map_session_data *sd,struct block_list *dst);
void clif_skill_warppoint(struct map_session_data* sd, short skill_id, short skill_lv, unsigned short map1, unsigned short map2, unsigned short map3, unsigned short map4);
void clif_skill_memomessage(struct map_session_data* sd, int type);
void clif_skill_teleportmessage(struct map_session_data *sd, int type);
void clif_skill_produce_mix_list(struct map_session_data *sd, int skill_id, int trigger);
void clif_cooking_list(struct map_session_data *sd, int trigger, uint16 skill_id, int qty, int list_type);

void clif_produceeffect(struct map_session_data* sd,int flag, t_itemid nameid);

void clif_getareachar_skillunit(struct block_list *bl, struct skill_unit *unit, enum send_target target, bool visible);
void clif_skill_delunit(struct skill_unit *unit);

void clif_skillunit_update(struct block_list* bl);

void clif_autospell(struct map_session_data *sd,int skill_lv);
void clif_devotion(struct block_list *src, struct map_session_data *tsd);
void clif_spiritball(struct map_session_data *sd);
void clif_spiritball_sub(struct block_list *bl, struct block_list* target, enum send_target send_target);
int clif_spiritball_attribute(struct map_session_data *sd);
int clif_soulball(struct map_session_data *sd);
void clif_combo_delay(struct block_list *bl,int wait);
void clif_bladestop(struct block_list *src, int dst_id, int active);
void clif_changemapcell(int fd, int m, int x, int y, int type, enum send_target target);

#define clif_status_load(bl, type, flag) clif_status_change((bl), (type), (flag), 0, 0, 0, 0)

void clif_wis_message(int fd, const char* nick, const char* mes, int mes_len);
void clif_wis_end(int fd, int flag);

void clif_solved_charname(int fd, int charid, const char* name);
void clif_name(struct block_list* src, struct block_list *bl, send_target target);
#define clif_name_self(bl) clif_name( (bl), (bl), SELF )
#define clif_name_area(bl) clif_name( (bl), (bl), AREA )

void clif_use_card(struct map_session_data *sd,int idx);
void clif_insert_card(struct map_session_data *sd,int idx_equip,int idx_card,int flag);

void clif_inventorylist(struct map_session_data *sd);
void clif_equiplist(struct map_session_data *sd);

void clif_cart_additem(struct map_session_data *sd,int n,int amount);
void clif_cart_delitem(struct map_session_data *sd,int n,int amount);
void clif_cart_additem_ack(struct map_session_data *sd, uint8 flag);
void clif_cartlist(struct map_session_data *sd);
void clif_clearcart(int fd);

void clif_item_identify_list(struct map_session_data *sd);
void clif_item_identified(struct map_session_data *sd,int idx,int flag);
void clif_item_repair_list(struct map_session_data *sd, struct map_session_data *dstsd, int lv);
void clif_item_repaireffect(struct map_session_data *sd, int idx, int flag);
void clif_item_damaged(struct map_session_data* sd, unsigned short position);
void clif_item_refine_list(struct map_session_data *sd);
void clif_hat_effects(struct map_session_data* sd, struct block_list* bl, enum send_target target);
void clif_hat_effect_single(struct map_session_data* sd, uint16 effectId, bool enable);

void clif_item_skill(struct map_session_data *sd,int skill_id,int skill_lv);

void clif_mvp_effect(struct map_session_data *sd);
void clif_mvp_item(struct map_session_data *sd, t_itemid nameid);
void clif_mvp_exp(struct map_session_data *sd, unsigned int exp);
void clif_mvp_noitem(struct map_session_data* sd);
void clif_changed_dir(struct block_list *bl, enum send_target target);

// vending
void clif_openvendingreq(struct map_session_data* sd, int num);
void clif_showvendingboard(struct block_list* bl, const char* message, int fd);
void clif_closevendingboard(struct block_list* bl, int fd);
void clif_vendinglist(struct map_session_data* sd, struct map_session_data* vsd);
void clif_buyvending(struct map_session_data* sd, int index, int amount, int fail);
void clif_openvending(struct map_session_data* sd, int id, struct s_vending* vending);
void clif_vendingreport(struct map_session_data* sd, int index, int amount, uint32 char_id, int zeny);

void clif_movetoattack(struct map_session_data *sd,struct block_list *bl);

// party
void clif_party_created(struct map_session_data *sd,int result);
void clif_party_member_info(struct party_data *p, int member_id, send_target type);
void clif_party_info(struct party_data* p, struct map_session_data *sd);
void clif_party_invite(struct map_session_data *sd,struct map_session_data *tsd);
void clif_party_inviteack(struct map_session_data* sd, const char* nick, int result);
void clif_party_option(struct party_data* p, int member_id, send_target type);
void clif_party_option_failexp(struct map_session_data* sd);
void clif_party_withdraw(struct map_session_data *sd, uint32 account_id, const char* name, enum e_party_member_withdraw result, enum send_target target); 
void clif_party_message(struct party_data* p, uint32 account_id, const char* mes, int len);
void clif_party_xy(struct map_session_data *sd);
void clif_party_xy_single(int fd, struct map_session_data *sd);
void clif_party_hp(struct map_session_data *sd);
void clif_hpmeter_single(int fd, int id, unsigned int hp, unsigned int maxhp);
int clif_hpmeter(struct map_session_data *sd);
int clif_hpmeter_sub(struct block_list *bl, va_list ap);
void clif_party_dead(struct map_session_data *sd);
int clif_party_job_and_level(struct map_session_data *sd);

// guild
void clif_guild_created(struct map_session_data *sd,int flag);
void clif_guild_belonginfo(struct map_session_data *sd);
void clif_guild_masterormember(struct map_session_data *sd);
void clif_guild_basicinfo(struct map_session_data *sd);
void clif_guild_allianceinfo(struct map_session_data *sd);
void clif_guild_memberlist(struct map_session_data *sd);
void clif_guild_skillinfo(struct map_session_data* sd);
void clif_guild_send_onlineinfo(struct map_session_data *sd); //[LuzZza]
void clif_guild_memberlogin_notice(struct guild *g,int idx,int flag);
void clif_guild_invite(struct map_session_data *sd,struct guild *g);
void clif_guild_inviteack(struct map_session_data *sd,int flag);
void clif_guild_leave(struct map_session_data *sd,const char *name,const char *mes);
void clif_guild_expulsion(struct map_session_data* sd, const char* name, const char* mes, uint32 account_id);
void clif_guild_positionchanged(struct guild *g,int idx);
void clif_guild_memberpositionchanged(struct guild *g,int idx);
void clif_guild_emblem(struct map_session_data *sd,struct guild *g);
void clif_guild_emblem_area(struct block_list* bl);
void clif_guild_notice(struct map_session_data* sd);
void clif_guild_message(struct guild *g,uint32 account_id,const char *mes,int len);
void clif_guild_reqalliance(struct map_session_data *sd,uint32 account_id,const char *name);
void clif_guild_allianceack(struct map_session_data *sd,int flag);
void clif_guild_delalliance(struct map_session_data *sd,int guild_id,int flag);
void clif_guild_oppositionack(struct map_session_data *sd,int flag);
void clif_guild_broken(struct map_session_data *sd,int flag);
void clif_guild_xy(struct map_session_data *sd);
void clif_guild_xy_single(int fd, struct map_session_data *sd);
void clif_guild_xy_remove(struct map_session_data *sd);

// Clan System
void clif_clan_basicinfo( struct map_session_data *sd );
void clif_clan_message(struct clan *clan,const char *mes,int len);
void clif_clan_onlinecount( struct clan* clan );
void clif_clan_leave( struct map_session_data* sd );

/// Achievement System
void clif_achievement_list_all(struct map_session_data *sd);
void clif_achievement_update(struct map_session_data *sd, const struct achievement_data *ad);
void clif_achievement_reward_ack(int fd, unsigned char result, int ach_id);

// Battleground
void clif_bg_hp(struct map_session_data *sd);
void clif_bg_xy(struct map_session_data *sd);
void clif_bg_xy_remove(struct map_session_data *sd);
void clif_bg_message(struct battleground_data *bg, int src_id, const char *name, const char *mes, int len);
void clif_bg_updatescore(int m);
void clif_bg_updatescore_single(struct map_session_data *sd);
void clif_sendbgemblem_area(struct map_session_data *sd);
void clif_sendbgemblem_single(int fd, struct map_session_data *sd);
void clif_bgqueue_notice_delete(struct map_session_data *sd, enum BATTLEGROUNDS_QUEUE_NOTICE_DELETED response, const char *name);
void clif_bgqueue_battlebegins(struct map_session_data *sd, unsigned char arena_id, enum send_target target);
void clif_bgqueue_ack(struct map_session_data *sd, enum BATTLEGROUNDS_QUEUE_ACK response, unsigned char arena_id);
void clif_bgqueue_joined(struct map_session_data *sd, int pos);
void clif_bgqueue_update_info(struct map_session_data *sd, unsigned char arena_id, int position);

// Instancing
int clif_instance(int instance_id, int type, int flag);
void clif_instance_join(int fd, int instance_id);
void clif_instance_leave(int fd);

// Custom Fonts
void clif_font(struct map_session_data *sd);

// atcommand
void clif_notify_chat(struct block_list* bl, const char* message, send_target target);
void clif_notify_playerchat(struct map_session_data* sd, const char* message);
void clif_displaymessage(const int fd, const char* mes);
void clif_displaymessagecolor_target(struct block_list *bl, unsigned long color, const char *msg, bool rgb2bgr, enum send_target type, struct map_session_data *sd);
void clif_displaymessagecolor(struct map_session_data *sd, const char* msg, unsigned long color);
void clif_displayformatted(struct map_session_data* sd, const char* fmt, ...);
void clif_disp_onlyself(struct map_session_data *sd, const char *mes, int len);
void clif_disp_message(struct block_list* src, const char* mes, int len, enum send_target target);
void clif_broadcast(struct block_list* bl, const char* mes, int len, int type, enum send_target target);
void clif_MainChatMessage(const char* message); //luzza
void clif_broadcast2(struct block_list* bl, const char* mes, int len, unsigned long fontColor, short fontType, short fontSize, short fontAlign, short fontY, enum send_target target);
void clif_heal(int fd,int type,int val);
void clif_resurrection(struct block_list *bl,int type);
void clif_map_property(struct block_list *bl, enum map_property property, enum send_target target);
void clif_pvpset(struct map_session_data *sd, int pvprank, int pvpnum,int type);
void clif_map_property_mapall(int map, enum map_property property);
void clif_refine(int fd, int fail, int index, int val);
void clif_upgrademessage(int fd, int result, t_itemid item_id);

//petsystem
void clif_catch_process(struct map_session_data *sd);
void clif_pet_roulette(struct map_session_data *sd,int data);
void clif_sendegg(struct map_session_data *sd);
void clif_send_petstatus(struct map_session_data *sd);
void clif_send_petdata(struct map_session_data* sd, struct pet_data* pd, int type, int param);
#define clif_pet_equip(sd, pd) clif_send_petdata(sd, pd, 3, (pd)->vd.head_bottom)
#define clif_pet_equip_area(pd) clif_send_petdata(NULL, pd, 3, (pd)->vd.head_bottom)
#define clif_pet_performance(pd, param) clif_send_petdata(NULL, pd, 4, param)
void clif_pet_emotion(struct pet_data *pd,int param);
void clif_pet_food(struct map_session_data *sd,int foodid,int fail);
void clif_pet_evolution_result(int fd, enum pet_evolution_result result);

//friends list
int clif_friendslist_toggle_sub(struct map_session_data *sd,va_list ap);
void clif_friendslist_send(struct map_session_data *sd);
void clif_friendslist_reqack(struct map_session_data *sd, struct map_session_data *f_sd, int type);

void clif_weather(int m); // [Valaris]
void clif_specialeffect(struct block_list* bl, int type, enum send_target target); // special effects [Valaris]
void clif_specialeffect_single(struct block_list* bl, int type, int fd);
void clif_specialeffect_value(struct block_list* bl, int effect_id, int num, send_target target);

void clif_GM_kickack(struct map_session_data *sd, int id);
void clif_GM_kick(struct map_session_data *sd,struct map_session_data *tsd);
void clif_manner_message(struct map_session_data* sd, uint32 type);
void clif_GM_silence(struct map_session_data* sd, struct map_session_data* tsd, uint8 type);

void clif_disp_overhead_(struct block_list *bl, const char* mes, enum send_target flag);
#define clif_disp_overhead(bl, mes) clif_disp_overhead_(bl, mes, AREA)

void clif_get_weapon_view(struct map_session_data* sd, t_itemid *rhand, t_itemid *lhand);

void clif_party_xy_remove(struct map_session_data *sd); //Fix for minimap [Kevin]
void clif_gospel_info(struct map_session_data *sd, int type);
void clif_feel_req(int fd, struct map_session_data *sd, int skill_lv);
void clif_starskill(struct map_session_data* sd, const char* mapname, int monster_id, unsigned char star, unsigned char result);
void clif_feel_info(struct map_session_data* sd, unsigned char feel_level, unsigned char type);
void clif_hate_info(struct map_session_data *sd, unsigned char hate_level,int class_, unsigned char type);
void clif_mission_info(struct map_session_data *sd, int mob_id, unsigned char progress);
void clif_feel_hate_reset(struct map_session_data *sd);

// [blackhole89]
void clif_hominfo(struct map_session_data *sd, struct homun_data *hd, int flag);
int clif_homskillinfoblock(struct map_session_data *sd);
void clif_homskillup(struct map_session_data *sd, int skill_id);	//[orn]
void clif_hom_food(struct map_session_data *sd,int foodid,int fail);	//[orn]
void clif_send_homdata(struct map_session_data *sd, int state, int param);	//[orn]

void clif_zc_config(struct map_session_data* sd, enum CZ_CONFIG type, int flag);
void clif_viewequip_ack(struct map_session_data* sd, struct map_session_data* tsd);
void clif_equipcheckbox(struct map_session_data* sd);

void clif_msg(struct map_session_data* sd, unsigned short id);
void clif_msg_value(struct map_session_data* sd, unsigned short id, int value);
void clif_msg_skill(struct map_session_data* sd, unsigned short skill_id, int msg_id);

//quest system [Kevin] [Inkfish]
void clif_quest_send_list(struct map_session_data * sd);
void clif_quest_send_mission(struct map_session_data * sd);
void clif_quest_add(struct map_session_data * sd, struct quest * qd);
void clif_quest_delete(struct map_session_data * sd, int quest_id);
void clif_quest_update_status(struct map_session_data * sd, int quest_id, bool active);
void clif_quest_update_objective(struct map_session_data * sd, struct quest * qd);
void clif_quest_show_event(struct map_session_data *sd, struct block_list *bl, short state, short color);
void clif_displayexp(struct map_session_data *sd, unsigned int exp, char type, bool quest, bool lost);

int clif_send(const void* buf, int len, struct block_list* bl, enum send_target type);
void do_init_clif(void);
void do_final_clif(void);

#ifndef TXT_ONLY
// MAIL SYSTEM
enum mail_send_result{
	WRITE_MAIL_SUCCESS = 0x0,
	WRITE_MAIL_FAILED = 0x1,
	WRITE_MAIL_FAILED_CNT = 0x2,
	WRITE_MAIL_FAILED_ITEM = 0x3,
	WRITE_MAIL_FAILED_CHECK_CHARACTER_NAME = 0x4,
	WRITE_MAIL_FAILED_WHISPEREXREGISTER = 0x5,
};

void clif_Mail_window(int fd, int flag);
void clif_Mail_read(struct map_session_data *sd, int mail_id);
void clif_mail_delete(struct map_session_data* sd, struct mail_message *msg, bool success); 
void clif_Mail_return(int fd, int mail_id, short fail);
void clif_Mail_send(struct map_session_data* sd, enum mail_send_result result);
void clif_Mail_new(struct map_session_data* sd, int mail_id, const char *sender, const char *title);
void clif_Mail_refreshinbox(struct map_session_data *sd,enum mail_inbox_type type,int64 mailID);
void clif_mail_getattachment(struct map_session_data* sd, struct mail_message *msg, uint8 result, enum mail_attachment_type type);
void clif_Mail_Receiver_Ack(struct map_session_data* sd, uint32 char_id, short class_, uint32 level, const char* name);
void clif_mail_removeitem(struct map_session_data* sd, bool success, int index, int amount);
// AUCTION SYSTEM
void clif_Auction_openwindow(struct map_session_data *sd);
void clif_Auction_results(struct map_session_data *sd, short count, short pages, uint8 *buf);
void clif_Auction_message(int fd, unsigned char flag);
void clif_Auction_close(int fd, unsigned char flag);
void clif_parse_Auction_cancelreg(int fd, struct map_session_data *sd);
#endif

void clif_bossmapinfo(int fd, struct mob_data *md, enum bossmap_info_type flag);
void clif_cashshop_show(struct map_session_data *sd, struct npc_data *nd);

// ADOPTION
void clif_Adopt_reply(struct map_session_data *sd, int type);
void clif_Adopt_request(struct map_session_data *sd, struct map_session_data *src, int p_id);

// MERCENARIES
void clif_mercenary_info(struct map_session_data *sd);
void clif_mercenary_skillblock(struct map_session_data *sd);
void clif_mercenary_message(struct map_session_data* sd, int message);
void clif_mercenary_updatestatus(struct map_session_data *sd, int type);

// Elementals
void clif_elemental_info(struct map_session_data *sd);
void clif_elemental_updatestatus(struct map_session_data *sd, int type);

// RENTAL SYSTEM
void clif_rental_time(struct map_session_data* sd, t_itemid nameid, int seconds);
void clif_rental_expired(struct map_session_data* sd, int index, t_itemid nameid);

// BOOK READING
void clif_readbook(int fd, int book_id, int page);

// Show Picker
void clif_party_show_picker(struct map_session_data * sd, struct item * item_data);

// Progress Bar [Inkfish]
void clif_progressbar(struct map_session_data * sd, unsigned long color, unsigned int second);
void clif_progressbar_abort(struct map_session_data * sd);

void clif_PartyBookingRegisterAck(struct map_session_data *sd, int flag);
void clif_PartyBookingDeleteAck(struct map_session_data* sd, int flag);
void clif_PartyBookingSearchAck(int fd, struct party_booking_ad_info** results, int count, bool more_result);
void clif_PartyBookingUpdateNotify(struct map_session_data* sd, struct party_booking_ad_info* pb_ad);
void clif_PartyBookingDeleteNotify(struct map_session_data* sd, int index);
void clif_PartyBookingInsertNotify(struct map_session_data* sd, struct party_booking_ad_info* pb_ad);

/* Bank System [Yommy/Hercules] */
void clif_bank_deposit (struct map_session_data *sd, enum e_BANKING_DEPOSIT_ACK reason);
void clif_bank_withdraw (struct map_session_data *sd,enum e_BANKING_WITHDRAW_ACK reason);
void clif_parse_BankDeposit (int fd, struct map_session_data *sd);
void clif_parse_BankWithdraw (int fd, struct map_session_data *sd);
void clif_parse_BankCheck (int fd, struct map_session_data *sd);
void clif_parse_BankOpen (int fd, struct map_session_data *sd);
void clif_parse_BankClose (int fd, struct map_session_data *sd);

void clif_showdigit(struct map_session_data* sd, unsigned char type, int value);

/// Buying Store System
void clif_buyingstore_open(struct map_session_data* sd);
void clif_buyingstore_open_failed(struct map_session_data* sd, unsigned short result, unsigned int weight);
void clif_buyingstore_myitemlist(struct map_session_data* sd);
void clif_buyingstore_entry(struct map_session_data* sd);
void clif_buyingstore_entry_single(struct map_session_data* sd, struct map_session_data* pl_sd);
void clif_buyingstore_disappear_entry(struct map_session_data* sd);
void clif_buyingstore_disappear_entry_single(struct map_session_data* sd, struct map_session_data* pl_sd);
void clif_buyingstore_itemlist(struct map_session_data* sd, struct map_session_data* pl_sd);
void clif_buyingstore_trade_failed_buyer(struct map_session_data* sd, short result);
void clif_buyingstore_update_item(struct map_session_data* sd, t_itemid nameid, unsigned short amount, uint32 char_id, int zeny);
void clif_buyingstore_delete_item(struct map_session_data* sd, short index, unsigned short amount, int price);
void clif_buyingstore_trade_failed_seller(struct map_session_data* sd, short result, t_itemid nameid);

/// Search Store System
void clif_search_store_info_ack(struct map_session_data* sd);
void clif_search_store_info_failed(struct map_session_data* sd, unsigned char reason);
void clif_open_search_store_info(struct map_session_data* sd);
void clif_search_store_info_click_ack(struct map_session_data* sd, short x, short y);

/// 15-3athena Added
void clif_monster_hp_bar( struct mob_data* md, int fd );
void clif_fast_movement(struct block_list *bl, short x, short y);
void clif_showscript(struct block_list* bl, const char* message);
void clif_equip_damaged(struct map_session_data *sd, int equip_index);
void clif_status_change(struct block_list *bl, int type, int flag, int64 tick, int val1, int val2, int val3);
void clif_status_change_sub(struct block_list *bl, int id, int type, int flag, int64 tick, int val1, int val2, int val3, enum send_target target_type);
void clif_efst_status_change(struct block_list *bl, int tid, enum send_target target, int type, int64 tick, int val1, int val2, int val3);
void clif_efst_status_change_sub(struct block_list *tbl, struct block_list *bl, enum send_target target);

/// Trade NPC
void clif_npc_market_open(struct map_session_data *sd, struct npc_data *nd);
void clif_parse_NPCShopClosed(int fd, struct map_session_data *sd);
void clif_parse_NPCMarketClosed(int fd, struct map_session_data *sd);
void clif_parse_NPCMarketPurchase(int fd, struct map_session_data *sd);
/// CashShop
void clif_parse_CashShopOpen(int fd, struct map_session_data *sd);
void clif_parse_CashShopClose(int fd, struct map_session_data *sd);
void clif_parse_CashShopBuy(int fd, struct map_session_data *sd);
void clif_cashshop_result(struct map_session_data *sd, t_itemid item_id, uint16 result);
void do_final_cashshop(void);

/// Roulette
void clif_roulette_generate_ack(struct map_session_data *sd, unsigned char result, short stage, short prizeIdx, t_itemid bonusItemID);
void clif_parse_RouletteOpen(int fd, struct map_session_data *sd);
void clif_parse_RouletteInfo(int fd, struct map_session_data *sd);
void clif_parse_RouletteClose(int fd, struct map_session_data *sd);
void clif_parse_RouletteGenerate(int fd, struct map_session_data *sd);
void clif_parse_RouletteRecvItem(int fd, struct map_session_data *sd);

void clif_disp_overheadcolor_self(int fd, uint32 color, const char *msg);
void clif_disp_overheadcolor(struct block_list* bl, uint32 color, const char *msg);

void clif_broadcast_obtain_special_item(const char *char_name, t_itemid nameid, t_itemid container, enum BROADCASTING_SPECIAL_ITEM_OBTAIN type);

void clif_dressing_room(struct map_session_data *sd, int view);

void clif_navigateTo(struct map_session_data *sd, const char* map, uint16 x, uint16 y, uint8 flag, bool hideWindow, uint16 mob_id);

/// Equip Switch System
void clif_equipswitch_list(struct map_session_data* sd);
void clif_equipswitch_add(struct map_session_data* sd, uint16 index, uint32 pos, uint8 flag);
void clif_equipswitch_remove(struct map_session_data* sd, uint16 index, uint32 pos, bool failed);
void clif_equipswitch_reply(struct map_session_data* sd, bool failed);

void clif_crimson_marker_xy(struct map_session_data *sd);
void clif_crimson_marker_xy_single(int fd, struct map_session_data *sd);
void clif_crimson_marker_xy_all(struct map_session_data *sd, struct block_list *bl, short mark);
void clif_crimson_marker_xy_remove(struct map_session_data *sd);

void clif_private_airship_response(struct map_session_data *sd, uint32 flag);

void clif_open_ui_req_sub(int fd, struct map_session_data* sd, enum out_ui_type ui_type);
bool clif_attendance_timediff(struct map_session_data *sd);

void clif_hom_spiritball(struct homun_data *hd);
int clif_millenniumshield(struct block_list* bl, short shield_count);

void clif_notify_bindOnEquip(struct map_session_data *sd, int n);

void clif_merge_item_open(struct map_session_data *sd);

void clif_partyinvitationstate(struct map_session_data* sd, bool flag);
void clif_party_leaderchanged(struct map_session_data *sd, int prev_leader_aid, int new_leader_aid);

void clif_camerainfo(struct map_session_data* sd, bool show, float range, float rotation, float latitude);

bool clif_lapineDdukDdak_open(struct map_session_data *sd, int item_id);
bool clif_lapineUpgrade_open(struct map_session_data *sd, int item_id);

void clif_parse_skill_toid(struct map_session_data* sd, uint16 skill_id, uint16 skill_lv, int target_id);

#endif /* _CLIF_H_ */
