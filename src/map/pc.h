// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _PC_H_
#define _PC_H_

#include "../common/mmo.h" // JOB_*, MAX_FAME_LIST, struct fame_list, struct mmo_charstatus
#include "../common/timer.h" // INVALID_TIMER
#include "../common/strlib.h"// StringBuf
#include "battle.h" // battle_config
#include "buyingstore.h"  // struct s_buyingstore
#include "clan.h" // struct clan
#include "itemdb.h" // MAX_ITEMGROUP
#include "map.h" // RC_MAX
#include "script.h" // struct script_reg, struct script_regstr
#include "searchstore.h"  // struct s_search_store_info
#include "status.h" // OPTION_*, struct weapon_atk
#include "unit.h" // unit_stop_attack(), unit_stop_walking()
#include "vending.h" // struct s_vending
#include "mob.h"

#define MAX_PC_BONUS 10
#define MAX_PC_SKILL_REQUIRE 5
#define MAX_PC_FEELHATE 3

#define BANK_VAULT_VAR "#BANKVAULT"
#define ROULETTE_BRONZE_VAR "RouletteBronze"
#define ROULETTE_SILVER_VAR "RouletteSilver"
#define ROULETTE_GOLD_VAR "RouletteGold"

//15-3athena
//These may be needed in the future. [15peaces]
#define MAX_RUNE 20 //Max number of runes a Rune Knight can carry of each type.
#define MAX_RAGE 15 //Max number of rage balls a Royal Guard can have.
#define MAX_SPELLBOOK 10 //Max number or spells a Warlock can preserve.

#define equip_index_check(i) ( (i) >= EQI_ACC_L && (i) < EQI_MAX )

struct weapon_data {
	int atkmods[3];
	// all the variables except atkmods get zero'ed in each call of status_calc_pc
	// NOTE: if you want to add a non-zeroed variable, you need to update the memset call
	//  in status_calc_pc as well! All the following are automatically zero'ed. [Skotlex]
	int overrefine;
	int star;
	int ignore_def_ele;
	int ignore_def_race;
	int def_ratio_atk_ele;
	int def_ratio_atk_race;
	int addele[ELE_MAX];
	int addrace[RC_MAX];
	int addrace2[RC2_MAX];
	int addsize[3];

	struct drain_data {
		short rate;
		short per;
		short value;
		unsigned type:1;
	} hp_drain[RC_MAX], sp_drain[RC_MAX];

	struct {
		short class_, rate;
	}	add_dmg[MAX_PC_BONUS];

	struct {
		short flag, rate;
		unsigned char ele;
	} addele2[MAX_PC_BONUS];
};

struct s_autospell {
	unsigned short card_id;
	short id, lv, rate, flag;
	bool lock;  // bAutoSpellOnSkill: blocks autospell from triggering again, while being executed
};

struct s_addeffect {
	enum sc_type id;
	short rate, arrow_rate;
	unsigned char flag;
};

struct s_addeffectonskill {
	enum sc_type id;
	short rate, skill;
	unsigned char target;
};

struct s_add_drop {
	unsigned short id;
	short group;
	int race, rate;
};

struct s_autobonus {
	short rate,atk_type;
	unsigned int duration;
	char *bonus_script, *other_script;
	int active;
	unsigned short pos;
};

///Timed bonus 'bonus_script' struct [Cydh]
struct s_bonus_script_entry {
	struct script_code *script;
	StringBuf *script_buf; //Used for comparing and storing on table
	int64 tick;
	uint16 flag;
	enum si_type icon;
	uint8 type; //0 - Ignore; 1 - Buff; 2 - Debuff
	int tid;
};

struct skill_cooldown_entry {
	unsigned short skill_id;
	int timer;
};

struct map_session_data {
	struct block_list bl;
	struct unit_data ud;
	struct view_data vd;
	struct status_data base_status, battle_status;
	struct status_change sc;
	struct regen_data regen;
	struct regen_data_sub sregen, ssregen;
	//NOTE: When deciding to add a flag to state or special_state, take into consideration that state is preserved in
	//status_calc_pc, while special_state is recalculated in each call. [Skotlex]
	struct {
		unsigned int active : 1; //Marks active player (not active is logging in/out, or changing map servers)
		unsigned int menu_or_input : 1;// if a script is waiting for feedback from the player
		unsigned int dead_sit : 2;
		unsigned int lr_flag : 3;
		unsigned int connect_new : 1;
		unsigned int arrow_atk : 1;
		unsigned combo : 2; // 1:Asura, 2:Kick [Inkfish]
		unsigned int gangsterparadise : 1;
		unsigned int rest : 1;
		unsigned int storage_flag : 2; //0: closed, 1: Normal Storage open, 2: guild storage open [Skotlex]
		unsigned int snovice_dead_flag : 1; //Explosion spirits on death: 0 off, 1 used.
		unsigned int abra_flag : 1; // Abracadabra bugfix by Aru
		unsigned int gmaster_flag : 1; // is guildmaster? (caches sd->status.name == g->master)
		unsigned int autocast : 1; // Autospell flag [Inkfish]
		unsigned int autotrade : 1;	//By Fantik
		unsigned int prevend : 1;//used to flag wheather you've spent 40sp to open the vending or not.
		unsigned int reg_dirty : 3; //By Skotlex (marks whether registry variables have been saved or not yet)
		unsigned int showdelay :1;
		unsigned int showexp :1;
		unsigned int showzeny :1;
		unsigned int mainchat :1; //[LuzZza]
		unsigned int noask :1; // [LuzZza]
		unsigned int trading :1; //[Skotlex] is 1 only after a trade has started.
		unsigned int can_tradeack : 1; // client can send a tradeack
		unsigned int deal_locked :2; //1: Clicked on OK. 2: Clicked on TRADE
		unsigned int monster_ignore :1; // for monsters to ignore a character [Valaris] [zzo]
		unsigned int size :2; // for tiny/large types
		unsigned int night :1; //Holds whether or not the player currently has the SI_NIGHT effect on. [Skotlex]
		unsigned int blockedmove :1;
		unsigned int using_fake_npc :1;
		unsigned int rewarp :1; //Signals that a player should warp as soon as he is done loading a map. [Skotlex]
		unsigned int killer : 1;
		unsigned int killable : 1;
		unsigned int doridori : 1;
		unsigned int ignoreAll : 1;
		unsigned int debug_remove_map : 1; // temporary state to track double remove_map's [FlavioJS]
		unsigned int buyingstore : 1;
		unsigned int lesseffect : 1;
		unsigned int vending : 1;
		unsigned int noks : 3; // [Zeph Kill Steal Protection]
		unsigned int changemap : 1;
		unsigned int callshop : 1; // flag to indicate that a script used callshop; on a shop
		short pmap; // Previous map on Map Change
		unsigned short autoloot;
		unsigned short autolootid; // [Zephyrus]
		unsigned short autobonus; //flag to indicate if an autobonus is activated. [Inkfish]
		unsigned improv_flag : 1;
		unsigned magicmushroom_flag : 1;
		unsigned no_gemstone : 1; // If a skill have a partner near, it don't consume gemstone but SP from all (ADORAMUS, COMET)
		unsigned fearbreeze : 4; // Arrows used on SC_FEARBREEZE
		unsigned int warping : 1;//states whether you're in the middle of a warp processing
		unsigned int banking : 1; //1 when we using the banking system 0 when closed
		unsigned int pvp : 1;	// Cell PVP [Napster]
		unsigned int bg_id;
		unsigned int workinprogress : 2; // See clif.h::e_workinprogress
		bool keepshop; // Whether shop data should be removed when the player disconnects
		bool mail_writing; // Whether the player is currently writing a mail in RODEX or not
	} state;
	struct {
		unsigned char no_weapon_damage, no_magic_damage, no_misc_damage;
		unsigned int restart_full_recover : 1;
		unsigned int no_castcancel : 1;
		unsigned int no_castcancel2 : 1;
		unsigned int no_sizefix : 1;
		unsigned int no_gemstone : 1;
		unsigned int intravision : 1; // Maya Purple Card effect [DracoRPG]
		unsigned int perfect_hiding : 1; // [Valaris]
		unsigned int no_knockback : 1;
		unsigned int bonus_coma : 1;
	} special_state;
	int login_id1, login_id2;
	unsigned short class_;	//This is the internal job ID used by the map server to simplify comparisons/queries/etc. [Skotlex]
	int gmlevel;

	int packet_ver;  // 5: old, 6: 7july04, 7: 13july04, 8: 26july04, 9: 9aug04/16aug04/17aug04, 10: 6sept04, 11: 21sept04, 12: 18oct04, 13: 25oct04 ... 18
	struct mmo_charstatus status;
	struct registry save_reg;

	// Item Storages
	struct s_storage storage;
	struct s_storage inventory;
	struct s_storage cart;
	
	struct item_data* inventory_data[MAX_INVENTORY]; // direct pointers to itemdb entries (faster than doing item_id lookups)
	short equip_index[EQI_MAX];
	short equip_switch_index[EQI_MAX];
	unsigned int weight,max_weight;
	int cart_weight,cart_num;
	int fd;
	unsigned short mapindex;
	unsigned char head_dir; //0: Look forward. 1: Look right, 2: Look left.
	unsigned int client_tick;
	int npc_id,areanpc_id,npc_shopid,touching_id;
	int npc_item_flag; //Marks the npc_id with which you can use items during interactions with said npc (see script command enable_itemuse)
	int npc_menu; // internal variable, used in npc menu handling
	int npc_amount;
	struct script_state *st;
	char npc_str[CHATBOX_SIZE]; // for passing npc input box text to script engine
	int npc_timer_id; //For player attached npc timers. [Skotlex]
	unsigned int chatID;
	int64 idletime;

	struct{
		int npc_id;
		int64 timeout;
	} progressbar; //Progress Bar [Inkfish]

	struct{
		char name[NAME_LENGTH];
	} ignore[MAX_IGNORE_LIST];

	int followtimer; // [MouseJstr]
	int followtarget;

	time_t emotionlasttime; // to limit flood with emotion packets

	short skillitem,skillitemlv;
	short skillid_old,skilllv_old;
	short skillid_dance,skilllv_dance;
	short cook_mastery; // range: [0,1999] [Inkfish]
	struct skill_cooldown_entry *scd[MAX_SKILLCOOLDOWN]; // Skill Cooldown
	int cloneskill_id, reproduceskill_id;
	int menuskill_id, menuskill_val, menuskill_itemused;

	int invincible_timer;
	int64 canlog_tick;
	int64 canuseitem_tick;	// [Skotlex]
	int64 canusecashfood_tick;
	int64 canequip_tick;	// [Inkfish]
	int64 cantalk_tick;
	int64 cansendmail_tick; // [Mail System Flood Protection]
	int64 ks_floodprotect_tick; // [Kill Steal Protection]
	int64 pvpcan_walkout_tick; // Cell PVP [Napster]
	int64 equipswitch_tick; // Equip switch
	
	struct {
		unsigned short nameid;
		int64 tick;
	} item_delay[MAX_ITEMDELAYS]; // [Paradox924X]

	short weapontype1,weapontype2;
	short disguise; // [Valaris]

	struct weapon_data right_weapon, left_weapon;
	
	// here start arrays to be globally zeroed at the beginning of status_calc_pc()
	int param_bonus[6],param_equip[6]; //Stores card/equipment bonuses.
	int subele[ELE_MAX];
	int subrace[RC_MAX];
	int subrace2[RC2_MAX];
	int subsize[3];
	int reseff[SC_COMMON_MAX-SC_COMMON_MIN+1];
	int weapon_coma_ele[ELE_MAX];
	int weapon_coma_race[RC_MAX];
	int weapon_atk[16];
	int weapon_atk_rate[16];
	int arrow_addele[ELE_MAX];
	int arrow_addrace[RC_MAX];
	int arrow_addsize[3];
	int magic_addele[ELE_MAX];
	int magic_addrace[RC_MAX];
	int magic_addsize[3];
	int magic_atk_ele[ELE_MAX];
	int critaddrace[RC_MAX];
	int expaddrace[RC_MAX];
	int ignore_mdef[RC_MAX];
	int ignore_def[RC_MAX];
	int itemgrouphealrate[MAX_ITEMGROUP];
	short sp_gain_race[RC_MAX];
	// zeroed arrays end here.
	// zeroed structures start here
	struct s_autospell autospell[15], autospell2[15], autospell3[15];
	struct s_addeffect addeff[MAX_PC_BONUS], addeff2[MAX_PC_BONUS];
	struct s_addeffectonskill addeff3[MAX_PC_BONUS];

	struct s_skill_bonus { //skillatk raises bonus dmg% of skills, skillheal increases heal%, skillblown increases bonus blewcount for some skills.
		unsigned short id;
		short val;
	} skillatk[MAX_PC_BONUS], skillheal[5], skillheal2[5], skillblown[MAX_PC_BONUS]
		, skillusesp[MAX_PC_BONUS];
	struct s_skill_bonus_i32 { // Copy of s_skill_bonus struct but for larger values [15peaces]
		uint16 id;
		int32 val;
	} skillcooldown[MAX_PC_BONUS], skillcast[MAX_PC_BONUS], fixedskillcast[MAX_PC_BONUS];
	struct {
		short value;
		int rate;
		int64 tick;
	} hp_loss, sp_loss, hp_regen, sp_regen;
	struct {
		short class_, rate;
	}	add_def[MAX_PC_BONUS], add_mdef[MAX_PC_BONUS], add_mdmg[MAX_PC_BONUS];
	struct s_add_drop add_drop[MAX_PC_BONUS];
	struct {
		unsigned short nameid;
		int rate;
	} itemhealrate[MAX_PC_BONUS];
	struct {
		short flag, rate;
		unsigned char ele;
	} subele2[MAX_PC_BONUS];
	// zeroed structures end here
	// manually zeroed structures start here.
	struct s_autobonus autobonus[MAX_PC_BONUS], autobonus2[MAX_PC_BONUS], autobonus3[MAX_PC_BONUS]; //Auto script on attack, when attacked, on skill usage
	// manually zeroed structures end here.
	// zeroed vars start here.
	int atk_rate;
	int arrow_atk,arrow_ele,arrow_cri,arrow_hit;
	int nsshealhp,nsshealsp;
	int critical_def,double_rate;
	int long_attack_atk_rate; //Long range atk rate, not weapon based. [Skotlex]
	int near_attack_def_rate,long_attack_def_rate,magic_def_rate,misc_def_rate;
	int ignore_mdef_ele;
	int ignore_mdef_race;
	int perfect_hit;
	int perfect_hit_add;
	int get_zeny_rate;
	int get_zeny_num; //Added Get Zeny Rate [Skotlex]
	int double_add_rate;
	int short_weapon_damage_return,long_weapon_damage_return;
	int magic_damage_return; // AppleGirl Was Here
	int random_attack_increase_add,random_attack_increase_per; // [Valaris]
	int break_weapon_rate,break_armor_rate;
	int crit_atk_rate;
	int classchange; // [Valaris]
	int speed_rate, speed_add_rate, aspd_add;
	int itemhealrate2; // [Epoque] Increase heal rate of all healing items.
	int shieldmdef;
	unsigned int setitem_hash, setitem_hash2; //Split in 2 because shift operations only work on int ranges. [Skotlex]
	int fixedcast; //Renewal fixed cast time adjustment. [15peaces]
	
	short splash_range, splash_add_range;
	short add_steal_rate;
	short add_heal_rate, add_heal2_rate;
	short sp_gain_value, hp_gain_value, magic_sp_gain_value, magic_hp_gain_value;
	short sp_vanish_rate;
	short sp_vanish_per;	
	unsigned short unbreakable;	// chance to prevent ANY equipment breaking [celest]
	unsigned short unbreakable_equip; //100% break resistance on certain equipment
	unsigned short unstripable_equip;

	// zeroed vars end here.

	int castrate,delayrate,hprate,sprate,dsprate;
	int fixedcastrate; //Renewal fixed cast time adjustment, Thanks to Rytech. [15peaces]
	int hprecov_rate,sprecov_rate;
	int matk_rate;
	int critical_rate,hit_rate,flee_rate,flee2_rate,def_rate,def2_rate,mdef_rate,mdef2_rate;

	int itemid;
	short itemindex;	//Used item's index in sd->inventory [Skotlex]

	short catch_target_class; // pet catching, stores a pet class to catch (short now) [zzo]

	short spiritball, spiritball_old;
	int spirit_timer[MAX_SKILL_LEVEL];

	short spiritballtype;
	short spiritballnumber;

	short rageball, rageball_old;
	int rage_timer[MAX_RAGE];

	unsigned char potion_success_counter; //Potion successes in row counter
	unsigned char mission_count; //Stores the bounty kill count for TK_MISSION
	short mission_mobid; //Stores the target mob_id for TK_MISSION
	int die_counter; //Total number of times you've died
	int devotion[5]; //Stores the account IDs of chars devoted to.
	int reg_num; //Number of registries (type numeric)
	int regstr_num; //Number of registries (type string)

	struct script_reg *reg;
	struct script_regstr *regstr;

	int trade_partner;
	struct { 
		struct {
			short index, amount;
		} item[10];
		int zeny, weight;
	} deal;

	bool party_creating; // whether the char is requesting party creation
	bool party_joining; // whether the char is accepting party invitation
	int party_invite, party_invite_account; // for handling party invitation (holds party id and account id)
	int adopt_invite; // Adoption

	int guild_invite,guild_invite_account;
	int guild_emblem_id,guild_alliance,guild_alliance_account;
	short guild_x,guild_y; // For guildmate position display. [Skotlex] should be short [zzo]
	int guildspy; // [Syrus22]
	int partyspy; // [Syrus22]

	struct clan *clan;

	int vended_id;
	int vender_id;
	int vend_num;
	uint16 vend_skill_lv;
	char message[MESSAGE_SIZE];
	struct s_vending vending[MAX_VENDING];

	unsigned int buyer_id;  // uid of open buying store
	struct s_buyingstore buyingstore;

	struct s_search_store_info searchstore;

	struct pet_data *pd;
	struct homun_data *hd;	// [blackhole89]
	struct mercenary_data *md;
	struct elemental_data *ed;

	struct{
		int  m; //-1 - none, other: map index corresponding to map name.
		unsigned short index; //map index
	}feel_map[3];// 0 - Sun; 1 - Moon; 2 - Stars
	short hate_mob[3];

	int pvp_timer;
	short pvp_point;
	unsigned short pvp_rank, pvp_lastusers;
	unsigned short pvp_won, pvp_lost;

	char eventqueue[MAX_EVENTQUEUE][EVENT_NAME_LENGTH];
	int eventtimer[MAX_EVENTTIMER];
	unsigned short eventcount; // [celest]

	unsigned char change_level[2]; // [celest]

	char fakename[NAME_LENGTH]; // fake names [Valaris]

	int duel_group; // duel vars [LuzZza]
	int duel_invite;

	int killerrid, killedrid;

	int cashPoints, kafraPoints;
	int rental_timer;

	// Auction System [Zephyrus]
	struct {
		int index, amount;
	} auction;

	// Mail System [Zephyrus]
	struct s_mail {
		struct {
			unsigned short nameid;
			int index, amount;
		} item[MAIL_MAX_ITEM];
		int zeny;
		struct mail_data inbox;
		bool changed; // if true, should sync with charserver on next mailbox request
	} mail;

	// Reading SpellBook
	struct {
		unsigned short skillid;
		unsigned char level;
		unsigned char points;
	} rsb[MAX_SPELLBOOK];

	//Quest log system [Kevin] [Inkfish]
	int num_quests;
	int avail_quests;
	int quest_index[MAX_QUEST_DB];
	struct quest quest_log[MAX_QUEST_DB];
	bool save_quest;

	// Achievement log system
	struct s_achievement_data {
		int total_score;                  ///< Total achievement points
		int level;                        ///< Achievement level
		bool save;                        ///< Flag to know if achievements need to be saved
		bool sendlist;                    ///< Flag to know if all achievements should be sent to the player (refresh list if an achievement has a title)
		uint16 count;                     ///< Total achievements in log
		uint16 incompleteCount;           ///< Total incomplete achievements in log
		struct achievement *achievements; ///< Achievement log entries
	} achievement_data;

	// Title system
	int *titles;
	uint8 titleCount;

	// Questinfo Cache
	bool *qi_display;
	unsigned short qi_count;

	// temporary debug [flaviojs]
	const char* debug_file;
	int debug_line;
	const char* debug_func;
	int shadowform_id;

	unsigned int bg_id;
	unsigned short user_font;

	/* Possible Thanks to Yommy~ / herc.ws! */
	struct
	{
		unsigned int ready : 1;/* did he accept the 'match is about to start, enter' dialog? */
		unsigned int client_has_bg_data : 1; /* flags whether the client has the "in queue" window (aka the client knows it is in a queue) */
		struct battleground_arena *arena;
		enum bg_queue_types type;
	} bg_queue;

	VECTOR_DECL(int) script_queues;

	// temporary debugging of bug #3504
	const char* delunit_prevfile;
	int delunit_prevline;

	int bank_vault; ///< Bank Vault

	/// Bonus Script [Cydh]
	struct s_bonus_script_list {
		struct linkdb_node *head; ///< Bonus script head node. data: struct s_bonus_script_entry *entry, key: (intptr_t)entry
		uint16 count;
	} bonus_script;

	struct {
		int bronze, silver, gold; ///< Roulette Coin
	} roulette_point;

	struct {
		short stage;
		int8 prizeIdx;
		short prizeStage;
		bool claimPrize;
	} roulette;

#if PACKETVER >= 20150513
	uint32* hatEffectIDs;
	uint8 hatEffectCount;
#endif

#ifdef PACKET_OBFUSCATION
	unsigned int cryptKey; ///< Packet obfuscation key to be used for the next received packet
#endif
};

//Update this max as necessary. 84 is the value needed for the Expanded Super Baby.
#define MAX_SKILL_TREE 84
//Total number of classes (for data storage)
#define CLASS_COUNT (JOB_MAX - JOB_NOVICE_HIGH + JOB_MAX_BASIC)

enum weapon_type {
	W_FIST,	//Bare hands
	W_DAGGER,	//1
	W_1HSWORD,	//2
	W_2HSWORD,	//3
	W_1HSPEAR,	//4
	W_2HSPEAR,	//5
	W_1HAXE,	//6
	W_2HAXE,	//7
	W_MACE,	//8
	W_2HMACE,	//9 (unused)
	W_STAFF,	//10
	W_BOW,	//11
	W_KNUCKLE,	//12	
	W_MUSICAL,	//13
	W_WHIP,	//14
	W_BOOK,	//15
	W_KATAR,	//16
	W_REVOLVER,	//17
	W_RIFLE,	//18
	W_GATLING,	//19
	W_SHOTGUN,	//20
	W_GRENADE,	//21
	W_HUUMA,	//22
	W_2HSTAFF,	//23
	MAX_WEAPON_TYPE,
	// dual-wield constants
	W_DOUBLE_DD, // 2 daggers
	W_DOUBLE_SS, // 2 swords
	W_DOUBLE_AA, // 2 axes
	W_DOUBLE_DS, // dagger + sword
	W_DOUBLE_DA, // dagger + axe
	W_DOUBLE_SA, // sword + axe
};

enum ammo_type {
	A_ARROW = 1,
	A_DAGGER,		//2
	A_BULLET,		//3
	A_SHELL,		//4
	A_GRENADE,		//5
	A_SHURIKEN,		//6
	A_KUNAI,		//7
	A_CANNONBALL,	//8
	A_THROWWEAPON,	//9
};

enum adopt_responses {
	ADOPT_ALLOWED = 0,
	ADOPT_ALREADY_ADOPTED,
	ADOPT_MARRIED_AND_PARTY,
	ADOPT_EQUIP_RINGS,
	ADOPT_NOT_NOVICE,
	ADOPT_CHARACTER_NOT_FOUND,
	ADOPT_MORE_CHILDREN,
	ADOPT_LEVEL_70,
	ADOPT_MARRIED,
};

struct {
	unsigned int base_hp[MAX_LEVEL], base_sp[MAX_LEVEL]; //Storage for the first calculation with hp/sp factor and multiplicator
	int hp_factor, hp_multiplicator, sp_factor;
	int max_weight_base;
	char job_bonus[MAX_LEVEL];
	int aspd_base[MAX_WEAPON_TYPE];
	uint32 exp_table[2][MAX_LEVEL];
	uint32 max_level[2];
	struct s_params {
		uint16 str, agi, vit, int_, dex, luk;
	} max_param;
	struct s_job_noenter_map {
		uint32 zone;
		uint8 gm_lv;
	} noenter_map;
} job_info[CLASS_COUNT];

//Equip position constants
enum equip_pos {
	EQP_HEAD_LOW		= 0x0001, 
	EQP_HEAD_MID		= 0x0200,
	EQP_HEAD_TOP		= 0x0100,
	EQP_HAND_R			= 0x0002,
	EQP_HAND_L			= 0x0020,
	EQP_ARMOR			= 0x0010,
	EQP_SHOES			= 0x0040,
	EQP_GARMENT			= 0x0004,
	EQP_ACC_L			= 0x0008,
	EQP_ACC_R			= 0x0080,
	EQP_AMMO			= 0x8000,
	EQP_COSTUME_HEAD_TOP= 0x0400,
	EQP_COSTUME_HEAD_MID= 0x0800,
	EQP_COSTUME_HEAD_LOW= 0x1000,
	EQP_COSTUME_GARMENT = 0x2000,
	EQP_COSTUME_FLOOR	= 0x4000,
	EQP_SHADOW_ARMOR	= 0x10000,
	EQP_SHADOW_WEAPON	= 0x20000,
	EQP_SHADOW_SHIELD	= 0x40000,
	EQP_SHADOW_SHOES	= 0x80000,
	EQP_SHADOW_ACC_R	= 0x100000,
	EQP_SHADOW_ACC_L	= 0x200000,
};

extern unsigned int equip[EQI_MAX];

#define EQP_WEAPON EQP_HAND_R
#define EQP_SHIELD EQP_HAND_L
#define EQP_ARMS (EQP_HAND_R|EQP_HAND_L)
#define EQP_HELM (EQP_HEAD_LOW|EQP_HEAD_MID|EQP_HEAD_TOP)
#define EQP_ACC (EQP_ACC_L|EQP_ACC_R)
#define EQP_COSTUME_HELM (EQP_COSTUME_HEAD_TOP|EQP_COSTUME_HEAD_MID|EQP_COSTUME_HEAD_LOW)
#define EQP_COSTUME (EQP_COSTUME_HEAD_TOP|EQP_COSTUME_HEAD_MID|EQP_COSTUME_HEAD_LOW|EQP_COSTUME_GARMENT|EQP_COSTUME_FLOOR)
#define EQP_SHADOW_ACC (EQP_SHADOW_ACC_L|EQP_SHADOW_ACC_R)
#define EQP_SHADOW_EQUIPS (EQP_SHADOW_ARMOR|EQP_SHADOW_WEAPON|EQP_SHADOW_SHIELD|EQP_SHADOW_SHOES|EQP_SHADOW_ACC_R|EQP_SHADOW_ACC_L)

/// Equip positions that use a visible sprite
#if PACKETVER < 20110111
	#define EQP_VISIBLE EQP_HELM
#else
	#define EQP_VISIBLE (EQP_HELM|EQP_COSTUME_HELM|EQP_COSTUME_GARMENT)
#endif

#define pc_setdead(sd)        ( (sd)->state.dead_sit = (sd)->vd.dead_sit = 1 )
#define pc_setsit(sd)         ( (sd)->state.dead_sit = (sd)->vd.dead_sit = 2 )
#define pc_isdead(sd)         ( (sd)->state.dead_sit == 1 )
#define pc_issit(sd)          ( (sd)->vd.dead_sit == 2 )
#define pc_isidle(sd)         ( (sd)->chatID || (sd)->state.vending || (sd)->state.buyingstore || DIFF_TICK(last_tick, (sd)->idletime) >= battle_config.idle_no_share )
#define pc_istrading(sd)      ( (sd)->npc_id || (sd)->state.vending || (sd)->state.buyingstore || (sd)->state.trading )
#define pc_cant_act(sd)       ( (sd)->npc_id || (sd)->vender_id || (sd)->chatID || ((sd)->sc.opt1 && (sd)->sc.opt1 != OPT1_BURNING) || (sd)->state.trading || (sd)->state.storage_flag  || (sd)->state.prevend )
#define pc_setdir(sd,b,h)     ( (sd)->ud.dir = (b) ,(sd)->head_dir = (h) )
#define pc_setchatid(sd,n)    ( (sd)->chatID = n )
#define pc_ishiding(sd)       ( (sd)->sc.option&(OPTION_HIDE|OPTION_CLOAK|OPTION_CHASEWALK) )
#define pc_iscloaking(sd)     ( !((sd)->sc.option&OPTION_CHASEWALK) && ((sd)->sc.option&OPTION_CLOAK) )
#define pc_ischasewalk(sd)    ( (sd)->sc.option&OPTION_CHASEWALK )
 #if PACKETVER < 20120410
#define pc_iscarton(sd)       ( (sd)->sc.option&OPTION_CART )
#else
#define pc_iscarton(sd)		  ( (sd)->sc.data[SC_ON_PUSH_CART] )
#endif
#define pc_isfalcon(sd)       ( (sd)->sc.option&OPTION_FALCON )
#define pc_isriding(sd)       ( (sd)->sc.option&OPTION_RIDING )
#define pc_isinvisible(sd)    ( (sd)->sc.option&OPTION_INVISIBLE )
#define pc_isdragon(sd) ( (sd)->sc.option&OPTION_DRAGON )
#define pc_iswug(sd) ( (sd)->sc.option&OPTION_WUG )
#define pc_iswugrider(sd) ( (sd)->sc.option&OPTION_WUGRIDER )
#define pc_ismadogear(sd) ( (sd)->sc.option&OPTION_MADOGEAR )
#define pc_hasmount(sd)       ( (sd)->sc.option&(OPTION_RIDING|OPTION_WUGRIDER|OPTION_DRAGON|OPTION_MADOGEAR) )
#define pc_is50overweight(sd) ( (sd)->weight*100 >= (sd)->max_weight*battle_config.natural_heal_weight_rate )
#define pc_is90overweight(sd) ( (sd)->weight*10 >= (sd)->max_weight*9 )
#define pc_maxparameter(sd) ( \
	(sd)->class_ == MAPID_SUPER_NOVICE_E || (sd)->class_ == MAPID_SUPER_BABY_E || \
	(sd)->class_ == MAPID_KAGEROUOBORO || (sd)->class_ == MAPID_REBELLION || (sd)->class_&JOBL_THIRD ? \
	(sd)->class_&JOBL_BABY ? battle_config.max_baby_parameter_renewal_jobs : battle_config.max_parameter_renewal_jobs : \
	(sd)->class_&JOBL_BABY ? battle_config.max_baby_parameter : battle_config.max_parameter \
)

#define pc_stop_walking(sd, type) unit_stop_walking(&(sd)->bl, type)
#define pc_stop_attack(sd) unit_stop_attack(&(sd)->bl)

//Weapon check considering dual wielding.
#define pc_check_weapontype(sd, type) ((type)&((sd)->status.weapon < MAX_WEAPON_TYPE? \
	1<<(sd)->status.weapon:(1<<(sd)->weapontype1)|(1<<(sd)->weapontype2)))
//Checks if the given class value corresponds to a player class. [Skotlex]
#define pcdb_checkid(class_) \
( \
	( (class_) >= JOB_NOVICE			&& (class_) < JOB_MAX_BASIC ) \
	|| ( (class_) >= JOB_NOVICE_HIGH	&& (class_) <= JOB_DARK_COLLECTOR ) \
	|| ( (class_) >= JOB_RUNE_KNIGHT	&& (class_) <= JOB_BABY_MECHANIC2 ) \
	|| ( (class_) >= JOB_SUPER_NOVICE_E && (class_) <= JOB_SUPER_BABY_E ) \
	|| ( (class_) >= JOB_KAGEROU		&& (class_) <= JOB_OBORO ) \
||	  (class_) == JOB_REBELLION      || (class_) == JOB_SUMMONER         \
||    (class_) == JOB_BABY_SUMMONER \
)

static inline bool pc_hasprogress(struct map_session_data *sd, enum e_wip_block progress) {
	return sd == NULL || (sd->state.workinprogress&progress) == progress;
}

int pc_class2idx(int class_);
int pc_isGM(struct map_session_data *sd);
int pc_getrefinebonus(int lv,int type);
bool pc_can_give_items(int level);
bool pc_can_give_bounded_items(int level);

int pc_setrestartvalue(struct map_session_data *sd,int type);
void pc_makesavestatus(struct map_session_data *);
void pc_respawn(struct map_session_data* sd, clr_type clrtype);
int pc_setnewpc(struct map_session_data* sd, int account_id, int char_id, uint32 login_id1, unsigned int client_tick, char sex, int fd);
bool pc_authok(struct map_session_data *sd, int login_id2, time_t expiration_time, int gmlevel, struct mmo_charstatus *st, bool changing_mapservers);
void pc_authfail(struct map_session_data *);
int pc_reg_received(struct map_session_data *sd);

void pc_setequipindex(struct map_session_data *sd);
int pc_isequip(struct map_session_data *sd,int n);
int pc_equippoint_sub(struct map_session_data *sd, struct item_data* id);
int pc_equippoint(struct map_session_data *sd,int n);
int pc_setinventorydata(struct map_session_data *sd);

int pc_get_skillcooldown(struct map_session_data *sd, uint16 skill_id, uint16 skill_lv);
int pc_checkskill(struct map_session_data *sd,int skill_id);
int pc_checkallowskill(struct map_session_data *sd);
int pc_checkequip(struct map_session_data *sd,int pos, bool checkall);

int pc_calc_skilltree(struct map_session_data *sd);
int pc_calc_skilltree_normalize_job(struct map_session_data *sd);
int pc_clean_skilltree(struct map_session_data *sd);

#define pc_checkoverhp(sd) ((sd)->battle_status.hp == (sd)->battle_status.max_hp)
#define pc_checkoversp(sd) ((sd)->battle_status.sp == (sd)->battle_status.max_sp)

int pc_setpos(struct map_session_data* sd, unsigned short mapindex, int x, int y, clr_type clrtype);
int pc_setsavepoint(struct map_session_data*,short,int,int);
int pc_randomwarp(struct map_session_data *sd,clr_type type);
int pc_warpto(struct map_session_data* sd, struct map_session_data* pl_sd);
int pc_recall(struct map_session_data* sd, struct map_session_data* pl_sd);
int pc_memo(struct map_session_data* sd, int pos);

int pc_checkadditem(struct map_session_data*,unsigned short,int);
int pc_inventoryblank(struct map_session_data*);
int pc_search_inventory(struct map_session_data *sd,unsigned short item_id);
int pc_payzeny(struct map_session_data*,int);
int pc_additem(struct map_session_data*,struct item*,int);
int pc_getzeny(struct map_session_data*,int);
int pc_delitem(struct map_session_data*,int,int,int,short);

//Bound items 
int pc_bound_chk(TBL_PC *sd,int type,int *idxlist);

// Special Shop System
void pc_paycash(struct map_session_data *sd, int price, int points);
void pc_getcash(struct map_session_data *sd, int cash, int points);

int pc_cart_additem(struct map_session_data *sd,struct item *item_data,int amount);
int pc_cart_delitem(struct map_session_data *sd,int n,int amount,int type);
int pc_putitemtocart(struct map_session_data *sd,int idx,int amount);
int pc_getitemfromcart(struct map_session_data *sd,int idx,int amount);
int pc_cartitem_amount(struct map_session_data *sd,int idx,int amount);

int pc_takeitem(struct map_session_data*,struct flooritem_data*);
int pc_dropitem(struct map_session_data*,int,int);

bool pc_isequipped(struct map_session_data *sd, unsigned short nameid);
enum adopt_responses pc_try_adopt(struct map_session_data *p1_sd, struct map_session_data *p2_sd, struct map_session_data *b_sd);
bool pc_adoption(struct map_session_data *p1_sd, struct map_session_data *p2_sd, struct map_session_data *b_sd);

int pc_updateweightstatus(struct map_session_data *sd);

int pc_addautobonus(struct s_autobonus *bonus,char max,const char *script,short rate,unsigned int dur,short atk_type,const char *o_script,unsigned short pos,bool onskill);
int pc_exeautobonus(struct map_session_data* sd,struct s_autobonus *bonus);
int pc_endautobonus(int tid, int64 tick, int id, intptr_t data);
int pc_delautobonus(struct map_session_data* sd,struct s_autobonus *bonus,char max,bool restore);

int pc_bonus(struct map_session_data*,int,int);
int pc_bonus2(struct map_session_data *sd,int,int,int);
int pc_bonus3(struct map_session_data *sd,int,int,int,int);
int pc_bonus4(struct map_session_data *sd,int,int,int,int,int);
int pc_bonus5(struct map_session_data *sd,int,int,int,int,int,int);
int pc_skill(struct map_session_data* sd, int id, int level, int flag);

int pc_insert_card(struct map_session_data *sd,int idx_card,int idx_equip);

int pc_steal_item(struct map_session_data *sd,struct block_list *bl, int skilllv);
int pc_steal_coin(struct map_session_data *sd,struct block_list *bl);

int pc_modifybuyvalue(struct map_session_data*,int);
int pc_modifysellvalue(struct map_session_data*,int);
int pc_modify_cashshop_buy_value(struct map_session_data *sd,int value);// For cash shop items. [Rytech]

int pc_follow(struct map_session_data*, int); // [MouseJstr]
int pc_stop_following(struct map_session_data*);

unsigned int pc_maxbaselv(struct map_session_data *sd);
unsigned int pc_maxjoblv(struct map_session_data *sd);
bool pc_is_maxbaselv(struct map_session_data *sd);
bool pc_is_maxjoblv(struct map_session_data *sd);
int pc_checkbaselevelup(struct map_session_data *sd);
int pc_checkjoblevelup(struct map_session_data *sd);
int pc_gainexp(struct map_session_data *sd, struct block_list *src, unsigned int base_exp, unsigned int job_exp, bool quest); 
unsigned int pc_nextbaseexp(struct map_session_data *sd);
unsigned int pc_nextjobexp(struct map_session_data *sd);
int pc_gets_status_point(int);
int pc_need_status_point(struct map_session_data *,int,int);
bool pc_statusup(struct map_session_data*, int, int);
int pc_statusup2(struct map_session_data*,int,int);
int pc_skillup(struct map_session_data*,int);
int pc_allskillup(struct map_session_data*);
int pc_resetlvl(struct map_session_data*,int type);
int pc_resetstate(struct map_session_data*);
int pc_resetskill(struct map_session_data*, int);
int pc_resetfeel(struct map_session_data*);
int pc_resethate(struct map_session_data*);
int pc_equipitem(struct map_session_data *sd, int n, int req_pos, bool equipswitch);
int pc_unequipitem(struct map_session_data*,int,int);
int pc_equipswitch(struct map_session_data* sd, int index);
void pc_equipswitch_remove(struct map_session_data* sd, int index);
int pc_checkitem(struct map_session_data*);
int pc_useitem(struct map_session_data*,int);

int pc_skillatk_bonus(struct map_session_data *sd, int skill_num);
int pc_skillheal_bonus(struct map_session_data *sd, int skill_num);
int pc_skillheal2_bonus(struct map_session_data *sd, int skill_num);

void pc_damage(struct map_session_data *sd,struct block_list *src,unsigned int hp, unsigned int sp);
int pc_dead(struct map_session_data *sd,struct block_list *src);
void pc_revive(struct map_session_data *sd,unsigned int hp, unsigned int sp);
void pc_heal(struct map_session_data *sd,unsigned int hp,unsigned int sp, int type);
int pc_itemheal(struct map_session_data *sd,int itemid, int hp,int sp);
int pc_percentheal(struct map_session_data *sd,int,int);
int pc_jobchange(struct map_session_data *,int, int);
int pc_setoption(struct map_session_data *,int);
int pc_setcart(struct map_session_data* sd, int type);
int pc_setfalcon(struct map_session_data* sd, int flag);
int pc_setriding(struct map_session_data* sd, int flag);
int pc_setdragon(struct map_session_data* sd, int flag);
int pc_setwug(struct map_session_data* sd, int flag);
int pc_setwugrider(struct map_session_data* sd, int flag);
int pc_setmadogear(struct map_session_data* sd, int flag);
int pc_changelook(struct map_session_data *,int,int);
int pc_equiplookall(struct map_session_data *sd);

int pc_readparam(struct map_session_data*,int);
int pc_setparam(struct map_session_data*,int,int);
int pc_readreg(struct map_session_data*,int);
int pc_setreg(struct map_session_data*,int,int);
char *pc_readregstr(struct map_session_data *sd,int reg);
int pc_setregstr(struct map_session_data *sd,int reg,const char *str);

#define pc_readglobalreg(sd,reg) pc_readregistry(sd,reg,3)
#define pc_setglobalreg(sd,reg,val) pc_setregistry(sd,reg,val,3)
#define pc_readglobalreg_str(sd,reg) pc_readregistry_str(sd,reg,3)
#define pc_setglobalreg_str(sd,reg,val) pc_setregistry_str(sd,reg,val,3)
#define pc_readaccountreg(sd,reg) pc_readregistry(sd,reg,2)
#define pc_setaccountreg(sd,reg,val) pc_setregistry(sd,reg,val,2)
#define pc_readaccountregstr(sd,reg) pc_readregistry_str(sd,reg,2)
#define pc_setaccountregstr(sd,reg,val) pc_setregistry_str(sd,reg,val,2)
#define pc_readaccountreg2(sd,reg) pc_readregistry(sd,reg,1)
#define pc_setaccountreg2(sd,reg,val) pc_setregistry(sd,reg,val,1)
#define pc_readaccountreg2str(sd,reg) pc_readregistry_str(sd,reg,1)
#define pc_setaccountreg2str(sd,reg,val) pc_setregistry_str(sd,reg,val,1)
int pc_readregistry(struct map_session_data*,const char*,int);
int pc_setregistry(struct map_session_data*,const char*,int,int);
char *pc_readregistry_str(struct map_session_data*,const char*,int);
int pc_setregistry_str(struct map_session_data*,const char*,const char*,int);

bool pc_setreg2(struct map_session_data *sd, const char *reg, int val);
int pc_readreg2(struct map_session_data *sd, const char *reg);

int pc_addeventtimer(struct map_session_data *sd,int tick,const char *name);
int pc_deleventtimer(struct map_session_data *sd,const char *name);
int pc_cleareventtimer(struct map_session_data *sd);
int pc_addeventtimercount(struct map_session_data *sd,const char *name,int tick);

int pc_calc_pvprank(struct map_session_data *sd);
int pc_calc_pvprank_timer(int tid, int64 tick, int id, intptr_t data);

int pc_ismarried(struct map_session_data *sd);
int pc_marriage(struct map_session_data *sd,struct map_session_data *dstsd);
int pc_divorce(struct map_session_data *sd);
struct map_session_data *pc_get_partner(struct map_session_data *sd);
struct map_session_data *pc_get_father(struct map_session_data *sd);
struct map_session_data *pc_get_mother(struct map_session_data *sd);
struct map_session_data *pc_get_child(struct map_session_data *sd);

void pc_bleeding (struct map_session_data *sd, int64 diff_tick);
void pc_regen (struct map_session_data *sd, int64 diff_tick);

bool pc_setstand(struct map_session_data *sd, bool force);
int pc_split_atoi(char* str, int* val, char sep, int max);
int pc_candrop(struct map_session_data *sd,struct item *item);

int pc_jobid2mapid(unsigned short b_class);	// Skotlex
int pc_mapid2jobid(unsigned short class_, int sex);	// Skotlex

const char * job_name(int class_);

struct skill_tree_entry {
	short id;
	unsigned char max;
	unsigned char joblv;
	struct {
		short id;
		unsigned char lv;
	} need[MAX_PC_SKILL_REQUIRE];
}; // Celest
extern struct skill_tree_entry skill_tree[CLASS_COUNT][MAX_SKILL_TREE];

struct sg_data {
	short anger_id;
	short bless_id;
	short comfort_id;
	char feel_var[NAME_LENGTH];
	char hate_var[NAME_LENGTH];
	int (*day_func)(void);
};
extern const struct sg_data sg_info[MAX_PC_FEELHATE];

void pc_setinvincibletimer(struct map_session_data* sd, int val);
void pc_delinvincibletimer(struct map_session_data* sd);

int pc_overheat(struct map_session_data *sd, int val);
int pc_addspiritball(struct map_session_data *sd,int,int);
int pc_delspiritball(struct map_session_data *sd,int,int);
int pc_addrageball(struct map_session_data *sd,int interval, int max);
int pc_delrageball(struct map_session_data *sd,int);
void pc_addfame(struct map_session_data *sd,int count);
unsigned char pc_famerank(int char_id, int job);
int pc_set_hate_mob(struct map_session_data *sd, int pos, struct block_list *bl);

extern struct fame_list smith_fame_list[MAX_FAME_LIST];
extern struct fame_list chemist_fame_list[MAX_FAME_LIST];
extern struct fame_list taekwon_fame_list[MAX_FAME_LIST];

extern unsigned int statp[MAX_LEVEL + 1];

void pc_readdb(void);
int do_init_pc(void);
void do_final_pc(void);

enum {
	ADDITEM_EXIST,
	ADDITEM_NEW,
	ADDITEM_OVERAMOUNT
};

// timer for night.day
extern int day_timer_tid;
extern int night_timer_tid;
int map_day_timer(int tid, int64 tick, int id, intptr_t data); // by [yor]
int map_night_timer(int tid, int64 tick, int id, intptr_t data); // by [yor]

// Rental System
void pc_inventory_rentals(struct map_session_data *sd);
int pc_inventory_rental_clear(struct map_session_data *sd);
void pc_inventory_rental_add(struct map_session_data *sd, int seconds);

int pc_read_motd(void); // [Valaris]
int pc_disguise(struct map_session_data *sd, int class_);

bool pc_isSkillFromJob( int job_id, int skill_num );

int pc_banding(struct map_session_data *sd, short skill_lv);

enum pc_msg {
	MSG_UPGRADESKER_FIRSTJOB	=  0x61e,
	MSG_UPGRADESKER_SECONDJOB	=  0x61f
};

enum e_BANKING_DEPOSIT_ACK pc_bank_deposit(struct map_session_data *sd, int money);
enum e_BANKING_WITHDRAW_ACK pc_bank_withdraw(struct map_session_data *sd, int money);

int pc_bonus_script_timer(int tid, int64 tick, int id, intptr_t data);
void pc_bonus_script(struct map_session_data *sd);
struct s_bonus_script_entry *pc_bonus_script_add(struct map_session_data *sd, const char *script_str, uint32 dur, enum si_type icon, uint16 flag, uint8 type);
void pc_bonus_script_clear(struct map_session_data *sd, uint16 flag);

bool pc_is_same_equip_index(enum equip_index eqi, short *equip_index, short index);

void pc_show_questinfo(struct map_session_data *sd);
void pc_show_questinfo_reinit(struct map_session_data *sd);

bool pc_job_can_entermap(enum e_job jobid, int m, int group_lv);

void pc_update_job_and_level(struct map_session_data *sd);

/// Check if player is Taekwon Ranker and the level is >= 90 (battle_config.taekwon_ranker_min_lv)
#define pc_is_taekwon_ranker(sd) (((sd)->class_&MAPID_UPPERMASK) == MAPID_TAEKWON && pc_famerank((sd)->status.char_id,MAPID_TAEKWON))

#endif /* _PC_H_ */
