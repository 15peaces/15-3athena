// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _NPC_H_
#define _NPC_H_

#include "map.h" // struct block_list
#include "status.h" // struct status_change
#include "unit.h" // struct unit_data
struct block_list;
struct npc_data;
struct view_data;

/// pointer size fix which fixes several gcc warnings
/// currently only used by bindatcmd script, so best to place here [15peaces]
#ifdef __64BIT__
	#define __64BPRTSIZE(y) (intptr)y
#else
	#define __64BPRTSIZE(y) y
#endif
DBMap* npcname_db; // const char* npc_name -> struct npc_data*

struct npc_timerevent_list {
	int timer,pos;
};

struct npc_label_list {
	char name[NAME_LENGTH];
	int pos;
};

/// Item list for NPC sell/buy list
struct npc_item_list {
	unsigned short nameid;
	unsigned int value;
#if PACKETVER >= 20131223
	unsigned short qty; ///< Stock counter (Market shop)
	uint8 flag; ///< 1: Item added by npcshopitem/npcshopadditem, force load! (Market shop)
#endif
};

/// List of bought/sold item for NPC shops
struct s_npc_buy_list {
	unsigned short qty;		///< Amount of item will be bought
	unsigned short nameid;	///< ID of item will be bought
};

struct npc_data {
	struct block_list bl;
	struct unit_data  ud; //Because they need to be able to move....
	struct view_data *vd;
	struct status_change sc; //They can't have status changes, but.. they want the visual opt values.
	struct npc_data *master_nd;
	short class_;
	short speed;
	char name[NPC_NAME_LENGTH+1];// display name
	char exname[NPC_NAME_LENGTH+1];// unique npc name
	int chat_id;
	int touching_id;
	int64 next_walktime;

	unsigned size : 2;

	struct status_data status;
	unsigned int level,stat_point;

	void* chatdb; // pointer to a npc_parse struct (see npc_chat.c)
	enum npc_subtype subtype;
	bool trigger_on_hidden;
	int src_id;
	union {
		struct {
			struct script_code *script;
			short xs,ys; // OnTouch area radius
			short ep_min,ep_max; //Episode System [15peaces]
			int guild_id;
			int timer,timerid,timeramount,rid;
			int64 timertick;
			struct npc_timerevent_list *timer_event;
			int label_list_num;
			struct npc_label_list *label_list;
		} scr;
		struct {
			struct npc_item_list *shop_item;
			uint16 count;
			bool discount;
		} shop;
		struct {
			short xs,ys; // OnTouch area radius
			short x,y; // destination coords
			unsigned short mapindex; // destination map
		} warp;
		struct {
			struct mob_data *md;
			time_t kill_time;
			char killer_name[NAME_LENGTH];
		} tomb;
	} u;
};

struct event_data {
	struct npc_data *nd;
	int pos;
};
static DBMap* ev_db; // const char* event_name -> struct event_data*

#define START_NPC_NUM 110000000

enum actor_classes
{
	WARP_CLASS = 45,
	HIDDEN_WARP_CLASS = 139,
	WARP_DEBUG_CLASS = 722,
	FLAG_CLASS = 722,
	INVISIBLE_CLASS = 32767,
};

//1st NPC ID Range ( 45 - 125 Including 139)
//2nd NPC ID Range ( 400 - 999 )
#define MAX_NPC_CLASS 1000

//3nd NPC ID Range ( 10001 - 19999 )
//Officially the 3nd NPC range is 10000 - 19999, but we dont need a range that big.
//Having space for 1000 NPC's is good enough until we need to expand the range. [Rytech]
#define MAX_NPC_CLASS_2 1000 //Increase this as needed. Current setting allows ID's 10001 - 11000.
#define NPC_CLASS_BASE_2 10001
#define NPC_CLASS_MAX_2 (NPC_CLASS_BASE_2+MAX_NPC_CLASS_2-1)

//Checks if a given id is a valid npc id. [Skotlex]
//Since new npcs are added all the time, the max valid value is the one before the first mob (Scorpion = 1001)
#define npcdb_checkid(id) ( ( (id) >= 46 && (id) <= 125) || (id) == 139 || ( (id) > 400 && (id) < MAX_NPC_CLASS )  || ( (id) >= NPC_CLASS_BASE_2 && (id) < NPC_CLASS_MAX_2 ) || (id) == INVISIBLE_CLASS )

#ifdef PCRE_SUPPORT
void npc_chat_finalize(struct npc_data* nd);
#endif

//Script NPC events.
enum npce_event {
	NPCE_LOGIN,
	NPCE_LOGOUT,
	NPCE_LOADMAP,
	NPCE_BASELVUP,
	NPCE_JOBLVUP,
	NPCE_DIE,
	NPCE_KILLPC,
	NPCE_KILLNPC,
	NPCE_MAX
};

struct view_data* npc_get_viewdata(int class_);
int npc_chat_sub(struct block_list* bl, va_list ap);
int npc_event_dequeue(struct map_session_data* sd);
int npc_event(struct map_session_data* sd, const char* eventname, int ontouch);
int npc_touch_areanpc(struct map_session_data* sd, int m, int x, int y);
int npc_touch_areanpc2(struct mob_data *md); // [Skotlex]
int npc_check_areanpc(int flag, int m, int x, int y, int range);
int npc_touchnext_areanpc(struct map_session_data* sd,bool leavemap);
int npc_click(struct map_session_data* sd, struct npc_data* nd);
int npc_scriptcont(struct map_session_data* sd, int id);
struct npc_data* npc_checknear(struct map_session_data* sd, struct block_list* bl);
int npc_buysellsel(struct map_session_data* sd, int id, int type);
uint8 npc_buylist(struct map_session_data* sd, uint16 n, struct s_npc_buy_list *item_list);
int npc_selllist(struct map_session_data* sd, int n, unsigned short* item_list);
void npc_parse_mob2(struct spawn_data* mob);
struct npc_data* npc_add_warp(char* name, short from_mapid, short from_x, short from_y, short xs, short ys, unsigned short to_mapindex, short to_x, short to_y);
int npc_globalmessage(const char* name,const char* mes);

void npc_setcells(struct npc_data* nd);
void npc_unsetcells(struct npc_data* nd);
void npc_movenpc(struct npc_data* nd, int x, int y);
bool npc_enable(struct npc_data* nd, int flag);
void npc_setdisplayname(struct npc_data* nd, const char* newname);
void npc_setclass(struct npc_data* nd, short class_);
struct npc_data* npc_name2id(const char* name);

int npc_get_new_npc_id(void);

void npc_addsrcfile(const char* name);
void npc_delsrcfile(const char* name);
void npc_parsesrcfile(const char* filepath);
int do_final_npc(void);
int do_init_npc(void);
void npc_event_do_oninit(void);
int npc_do_ontimer(int npc_id, int option);

int npc_event_do(const char* name);
int npc_event_doall(const char* name);
int npc_event_doall_id(const char* name, int rid);
bool npc_event_isspecial(const char* eventname);

int npc_timerevent_start(struct npc_data* nd, int rid);
int npc_timerevent_stop(struct npc_data* nd);
void npc_timerevent_quit(struct map_session_data* sd);
int64 npc_gettimerevent_tick(struct npc_data* nd);
int npc_settimerevent_tick(struct npc_data* nd, int newtimer);
int npc_remove_map(struct npc_data* nd);
void npc_unload_duplicates (struct npc_data* nd);
int npc_unload(struct npc_data* nd);
int npc_reload(void);
void npc_read_event_script(void);
int npc_script_event(struct map_session_data* sd, enum npce_event type);

int npc_duplicate4instance(struct npc_data *snd, int m);
int npc_instanceinit(struct npc_data* nd);
int npc_cashshop_buy(struct map_session_data* sd, unsigned short nameid, int amount, int points);
int npc_cashshop_buylist(struct map_session_data* sd, int n, struct s_npc_buy_list *item_list, int points);

#if PACKETVER >= 20131223
void npc_market_tosql(const char *exname, struct npc_item_list *list);
void npc_market_delfromsql_(const char *exname, unsigned short nameid, bool clear);
#endif

extern struct npc_data* fake_nd;

// @commands (script-based) 
int npc_do_atcmd_event(struct map_session_data* sd, const char* command, const char* message, const char* eventname);

#endif /* _NPC_H_ */
