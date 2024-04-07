// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _MOB_H_
#define _MOB_H_

#include "../common/mmo.h" // struct item
#include "map.h" // struct status_data, struct view_data, struct mob_skill
#include "status.h" // struct status data, struct status_change
#include "unit.h" // unit_stop_walking(), unit_stop_attack()
#include "npc.h"


#define MAX_RANDOMMONSTER 5

// Be sure to note that IDs 4001 to 5000 are reserved for advanced/baby/expanded classes.
#define MAX_MOB_DB 24000

//The number of drops all mobs have and the max drop-slot that the steal skill will attempt to steal from.
#define MAX_MOB_DROP 10
#define MAX_STEAL_DROP 7

#define MAX_RACE2_MOBS 50

//Min time between AI executions
#define MIN_MOBTHINKTIME 100
//Min time before mobs do a check to call nearby friends for help (or for slaves to support their master)
#define MIN_MOBLINKTIME 1000

//Distance that slaves should keep from their master.
#define MOB_SLAVEDISTANCE 2

// These define the range of available IDs for clones. [Valaris]
#define MOB_CLONE_START MAX_MOB_DB
#define MOB_CLONE_END MAX_MOB_DB+1000

//Used to determine default enemy type of mobs (for use in eachinrange calls)
#define DEFAULT_ENEMY_TYPE(md) (md->special_state.ai?BL_CHAR:BL_PC|BL_HOM|BL_MER|BL_ELEM)

struct mob_db *mob_dummy;	//Dummy mob to be returned when a non-existant one is requested.

enum MOBID {
	MOBID_PORING			= 1002,
	MOBID_RED_PLANT			= 1078,
	MOBID_BLUE_PLANT,
	MOBID_GREEN_PLANT,
	MOBID_YELLOW_PLANT,
	MOBID_WHITE_PLANT,
	MOBID_SHINING_PLANT,
	MOBID_BLACK_MUSHROOM	= 1084,
	MOBID_GOBLIN_1			= 1122,
	MOBID_GOBLIN_2,
	MOBID_GOBLIN_3,
	MOBID_GOBLIN_4,
	MOBID_GOBLIN_5,
	MOBID_MARINE_SPHERE		= 1142,
	MOBID_A_GUARDIAN		= 1285,
	MOBID_K_GUARDIAN,
	MOBID_S_GUARDIAN,
	MOBID_EMPERIUM			= 1288,
	MOBID_TREAS01			= 1324,
	MOBID_TREAS40			= 1363,
	MOBID_G_PARASITE		= 1555,
	MOBID_G_FLORA			= 1575,
	MOBID_G_HYDRA			= 1579,
	MOBID_G_MANDRAGORA		= 1589,
	MOBID_G_GEOGRAPHER		= 1590,
	// WoE:SE objects
	MOBID_S_GUARDIAN_		= 1899,
	MOBID_A_GUARDIAN_,
	MOBID_BARRICADE1		= 1905,
	MOBID_BARRICADE2,		
	MOBID_GUARIDAN_STONE1,	
	MOBID_GUARIDAN_STONE2,
	MOBID_TREAS41			= 1938,
	MOBID_TREAS49			= 1946,
	MOBID_SILVERSNIPER		= 2042,
	MOBID_MAGICDECOY_FIRE,
	MOBID_MAGICDECOY_WATER,
	MOBID_MAGICDECOY_EARTH,
	MOBID_MAGICDECOY_WIND,
	// Sorcerer's Elementals
	MOBID_EL_AGNI_S = 2114,
	MOBID_EL_AGNI_M,
	MOBID_EL_AGNI_L,
	MOBID_EL_AQUA_S,
	MOBID_EL_AQUA_M,
	MOBID_EL_AQUA_L,
	MOBID_EL_VENTUS_S,
	MOBID_EL_VENTUS_M,
	MOBID_EL_VENTUS_L,
	MOBID_EL_TERA_S,
	MOBID_EL_TERA_M,
	MOBID_EL_TERA_L,
	// Sera's Summon Legion
	MOBID_S_HORNET			= 2158,
	MOBID_S_GIANT_HORNET,
	MOBID_S_LUCIOLA_VESPA,
	MOBID_KO_KAGE			= 2308,
};

//Externals for the status effects. [Epoque]
extern const int mob_manuk[8];
extern const int mob_splendide[5];

//Mob skill states.
enum MobSkillState {
	MSS_ANY = -1,
	MSS_IDLE,
	MSS_WALK,
	MSS_LOOT,
	MSS_DEAD,
	MSS_BERSERK, //Aggressive mob attacking
	MSS_ANGRY,   //Mob retaliating from being attacked.
	MSS_RUSH,    //Mob following a player after being attacked.
	MSS_FOLLOW,  //Mob following a player without being attacked.
	MSS_ANYTARGET,
};

enum MobDamageLogFlag
{
	MDLF_NORMAL = 0,
	MDLF_HOMUN,
	MDLF_PET,
};

enum e_size {
	SZ_SMALL = 0,
	SZ_MEDIUM,
	SZ_BIG,
	SZ_ALL,
	SZ_MAX
};

struct mob_skill {
	enum MobSkillState state;
	short skill_id,skill_lv;
	short permillage;
	int casttime,delay;
	short cancel;
	short cond1,cond2;
	short target;
	int val[5];
	short emotion;
	unsigned short msg_id;
};

struct mob_chat {
	unsigned short msg_id;
	unsigned long color;
	char msg[CHAT_SIZE_MAX];
};

struct spawn_info {
	unsigned short mapindex;
	unsigned short qty;
};
 
struct mob_db {
	char sprite[NAME_LENGTH],name[NAME_LENGTH],jname[NAME_LENGTH];
	unsigned int base_exp,job_exp;
	unsigned int mexp,mexpper;
	short range2,range3;
	short race2;	// celest
	unsigned short lv;
	struct { 
		unsigned short nameid; 
		int p; 
	} dropitem[MAX_MOB_DROP];
	struct { 
		unsigned short nameid; 
		int p; 
	} mvpitem[3];
	struct status_data status;
	struct view_data vd;
	int option; // Changed to int to give support to the new options. [pakpil] // 3ceam v1.
	int summonper[MAX_RANDOMMONSTER];
	int maxskill;
	struct mob_skill skill[MAX_MOBSKILL];
	struct spawn_info spawn[10];
};

struct mob_data {
	struct block_list bl;
	struct unit_data  ud;
	struct view_data *vd;
	struct status_data status, *base_status; //Second one is in case of leveling up mobs, or tiny/large mobs.
	struct status_change sc;
	struct mob_db *db;	//For quick data access (saves doing mob_db(md->mob_id) all the time) [Skotlex]
	char name[NAME_LENGTH];
	struct {
		unsigned int size : 2; //Small/Big monsters.
		enum mob_ai ai;
		unsigned int clone : 1;// is clone? 1:0
	} special_state; //Special mob information that does not needs to be zero'ed on mob respawn.
	struct {
		unsigned int aggressive : 1; //Signals whether the mob AI is in aggressive mode or reactive mode. [Skotlex]
		unsigned int steal_coin_flag : 1;
		unsigned int soul_change_flag : 1; // Celest
		unsigned int alchemist: 1;
		unsigned int spotted: 1;
		unsigned int npc_killmonster: 1; //for new killmonster behavior
		unsigned int rebirth: 1; // NPC_Rebirth used
		unsigned int boss : 1;
		unsigned int copy_master_mode : 1; ///< Whether the spawned monster should copy the master's mode. 
		enum MobSkillState skillstate;
		unsigned char steal_flag; //number of steal tries (to prevent steal exploit on mobs with few items) [Lupus]
		unsigned char attacked_count; //For rude attacked.
		int provoke_flag; // Celest
	} state;
	struct guardian_data* guardian_data; 
	struct s_dmglog {
		int id; //char id
		unsigned int dmg;
		unsigned int flag : 2; //0: Normal. 1: Homunc exp. 2: Pet exp
	} dmglog[DAMAGELOG_SIZE];
	struct spawn_data *spawn; //Spawn data.
	int spawn_timer; //Required for Convex Mirror
	struct item *lootitem;
	short mob_id;
	unsigned int tdmg; //Stores total damage given to the mob, for exp calculations. [Skotlex]
	int level;
	int target_id,attacked_id;
	int areanpc_id; //Required in OnTouchNPC (to avoid multiple area touchs)
	unsigned int bg_id; // BattleGround System

	int64 next_walktime,last_thinktime,last_linktime,last_pcneartime;
	short move_fail_count;
	short lootitem_count;
	short min_chase;
	
	int deletetimer;
	int master_id,master_dist;

	short skillidx;
	int64 skilldelay[MAX_MOBSKILL];
	char npc_event[EVENT_NAME_LENGTH];

	//MvP Tombstone NPC ID
	int tomb_nid;
};

enum {
	MST_TARGET =	0,
	MST_RANDOM,	//Random Target!
	MST_SELF,
	MST_FRIEND,
	MST_MASTER,
	MST_AROUND5,
	MST_AROUND6,
	MST_AROUND7,
	MST_AROUND8,
	MST_AROUND1,
	MST_AROUND2,
	MST_AROUND3,
	MST_AROUND4,
	MST_AROUND			=	MST_AROUND4,

	MSC_ALWAYS			=	0x0000,
	MSC_MYHPLTMAXRATE,
	MSC_MYHPINRATE,
	MSC_FRIENDHPLTMAXRATE,
	MSC_FRIENDHPINRATE,
	MSC_MYSTATUSON,
	MSC_MYSTATUSOFF,
	MSC_FRIENDSTATUSON,
	MSC_FRIENDSTATUSOFF,
	MSC_ATTACKPCGT,
	MSC_ATTACKPCGE,
	MSC_SLAVELT,
	MSC_SLAVELE,
	MSC_CLOSEDATTACKED,
	MSC_LONGRANGEATTACKED,
	MSC_AFTERSKILL,
	MSC_SKILLUSED	,
	MSC_CASTTARGETED,
	MSC_RUDEATTACKED,
	MSC_MASTERHPLTMAXRATE,
	MSC_MASTERATTACKED,
	MSC_ALCHEMIST,
	MSC_SPAWN,
};

// The data structures for storing delayed item drops
struct item_drop {
	struct item item_data;
	struct item_drop* next;
};
struct item_drop_list {
	int m, x, y;                       // coordinates
	int first_charid, second_charid, third_charid; // charid's of players with higher pickup priority
	struct item_drop* item;            // linked list of drops
};

struct mob_db* mob_db(int class_);
int mobdb_searchname(const char *str);
int mobdb_searchname_array(struct mob_db** data, int size, const char *str);
int mobdb_checkid(const int id);
struct view_data* mob_get_viewdata(int class_);
struct mob_data *mob_once_spawn_sub(struct block_list *bl, int m, short x, short y, const char *mobname, int class_, const char *event, unsigned int size, enum mob_ai ai);
int mob_once_spawn(struct map_session_data* sd,int m,short x,short y,const char* mobname,int class_,int amount,const char* event, unsigned int size, enum mob_ai ai);
int mob_once_spawn_area(struct map_session_data* sd,int m,int x0,int y0,int x1,int y1,const char* mobname,int class_,int amount,const char* event, unsigned int size, enum mob_ai ai);

bool mob_ksprotected (struct block_list *src, struct block_list *target);

int mob_spawn_guardian(const char* mapname, short x, short y, const char* mobname, int class_, const char* event, int guardian, bool has_index);	// Spawning Guardians [Valaris]
int mob_spawn_bg(const char* mapname, short x, short y, const char* mobname, int class_, const char* event, unsigned int bg_id);
int mob_guardian_guildchange(struct mob_data *md); //Change Guardian's ownership. [Skotlex]

int mob_randomwalk(struct mob_data *md,int64 tick);
int mob_warpchase(struct mob_data *md, struct block_list *target);
int mob_target(struct mob_data *md,struct block_list *bl,int dist);
int mob_unlocktarget(struct mob_data *md, int64 tick);
struct mob_data* mob_spawn_dataset(struct spawn_data *data);
int mob_spawn(struct mob_data *md);
int mob_delayspawn(int tid, int64 tick, int id, intptr_t data);
int mob_setdelayspawn(struct mob_data *md);
int mob_parse_dataset(struct spawn_data *data);
void mob_log_damage(struct mob_data *md, struct block_list *src, int damage);
void mob_damage(struct mob_data *md, struct block_list *src, int damage);
int mob_dead(struct mob_data *md, struct block_list *src, int type);
void mob_revive(struct mob_data *md, unsigned int hp);
void mob_heal(struct mob_data *md,unsigned int heal);

#define mob_stop_walking(md, type) unit_stop_walking(&(md)->bl, type)
#define mob_stop_attack(md) unit_stop_attack(&(md)->bl)
#define mob_is_battleground(md) ( map[(md)->bl.m].flag.battleground && ((md)->mob_id == 1906 || ((md)->mob_id >= 1909 && (md)->mob_id <= 1915)) )
#define mob_is_goblin(md, mid) (((md)->mob_id >= MOBID_GOBLIN_1 && (md)->mob_id <= MOBID_GOBLIN_5) && (mid >= MOBID_GOBLIN_1 && mid <= MOBID_GOBLIN_5))
#define mob_is_samename(md, mid) (strcmp(mob_db((md)->mob_id)->jname, mob_db(mid)->jname) == 0)
#define mob_is_treasure(md) (((md)->mob_id >= MOBID_TREAS01 && (md)->mob_id <= MOBID_TREAS40) || ((md)->mob_id >= MOBID_TREAS41 && (md)->mob_id <= MOBID_TREAS49))
#define mob_is_guardian(mob_id) ((mob_id >= MOBID_A_GUARDIAN && mob_id <= MOBID_S_GUARDIAN) || mob_id == MOBID_S_GUARDIAN_ || mob_id == MOBID_A_GUARDIAN_)

void mob_clear_spawninfo();
void do_init_mob(void);
void do_final_mob(void);

int mob_timer_delete(int tid, int64 tick, int id, intptr_t data);
int mob_deleteslave(struct mob_data *md);

int mob_random_class (int *value, size_t count);
int mob_get_random_id(int type, int flag, int lv);
int mob_class_change(struct mob_data *md,int class_);
int mob_warpslave(struct block_list *bl, int range);
int mob_linksearch(struct block_list *bl,va_list ap);

int mobskill_use(struct mob_data *md,int64 tick,int event);
int mobskill_event(struct mob_data *md,struct block_list *src,int64 tick, int flag);
int mobskill_castend_id( int tid, int64 tick, int id,int data );
int mobskill_castend_pos( int tid, int64 tick, int id,int data );
int mob_summonslave(struct mob_data *md2,int *value,int amount,int skill_id);
int mob_countslave(struct block_list *bl);
int mob_count_sub(struct block_list *bl, va_list ap);

int mob_is_clone(int class_);

int mob_clone_spawn(struct map_session_data *sd, int m, int x, int y, const char *event, int master_id, int mode, int flag, unsigned int duration);
int mob_clone_delete(struct mob_data *md);

// MvP Tomb System
void mvptomb_create(struct mob_data *md, char *killer, time_t time);
void mvptomb_destroy(struct mob_data *md);

void mob_reload(void);

#endif /* _MOB_H_ */
