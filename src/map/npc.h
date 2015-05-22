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

enum npc_parse_options {
	NPO_NONE = 0x0,
	NPO_ONINIT = 0x1,
	NPO_TRADER = 0x2,
};

enum npc_shop_types {
	NST_ZENY,/* default */
	NST_CASH,/* official npc cash shop */
	NST_MARKET,/* official npc market type */
	NST_CUSTOM,
	/* */
	NST_MAX,
};

struct npc_timerevent_list {
	int timer,pos;
};
struct npc_label_list {
	char name[NAME_LENGTH];
	int pos;
};
struct npc_item_list {
	unsigned short nameid;
	unsigned int value;
	unsigned int qty;
};

struct npc_shop_data {
	unsigned char type;/* what am i */
	struct npc_item_list *item;/* list */
	unsigned short items;/* total */
};

struct npc_data {
	struct block_list bl;
	struct unit_data  ud; //Because they need to be able to move....
	struct view_data *vd;
	struct status_change sc; //They can't have status changes, but.. they want the visual opt values.
	struct npc_data *master_nd;
	short class_;
	short speed;
	char name[NAME_LENGTH+1];// display name
	char exname[NAME_LENGTH+1];// unique npc name
	int chat_id;
	int touching_id;
	unsigned int next_walktime;

	unsigned size : 2;

	struct status_data status;
	unsigned int level,stat_point;

	void* chatdb; // pointer to a npc_parse struct (see npc_chat.c)
	enum npc_subtype subtype;
	int src_id;
	union {
		struct {
			struct script_code *script;
			short xs,ys; // OnTouch area radius
			short ep_min,ep_max; //Episode System [15peaces]
			int guild_id;
			int timer,timerid,timeramount,rid;
			unsigned int timertick;
			struct npc_timerevent_list *timer_event;
			int label_list_num;
			struct npc_label_list *label_list;
			struct npc_shop_data *shop;
			bool trader;
		} scr;
		struct {
			struct npc_item_list* shop_item;
			unsigned short count;
		} shop;
		struct {
			short xs,ys; // OnTouch area radius
			short x,y; // destination coords
			unsigned short mapindex; // destination map
		} warp;
	} u;
};



#define START_NPC_NUM 110000000

enum actor_classes
{
	WARP_CLASS = 45,
	HIDDEN_WARP_CLASS = 139,
	WARP_DEBUG_CLASS = 722,
	FLAG_CLASS = 722,
	INVISIBLE_CLASS = 32767,
};

//Checks if a given id is a valid npc id. [Skotlex]
//Since new npcs are added all the time, the max valid value is assumed 11000 for now, and bumped as necessary.
#define npcdb_checkid(id) ( ( (id) > WARP_CLASS && (id) <= 125) || (id) == HIDDEN_WARP_CLASS || ( (id) > 400 && (id) < 1000 ) || (id) == INVISIBLE_CLASS || ( (id) > 10000 && (id) < 11000 ) )

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

/* npc trader global data, for ease of transition between the script, cleared on every usage */
bool trader_ok;
int trader_funds[2];

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
int npc_buylist(struct map_session_data* sd,int n, unsigned short* item_list);
int npc_selllist(struct map_session_data* sd, int n, unsigned short* item_list);
void npc_parse_mob2(struct spawn_data* mob);
struct npc_data* npc_add_warp(short from_mapid, short from_x, short from_y, short xs, short ys, unsigned short to_mapindex, short to_x, short to_y);
int npc_globalmessage(const char* name,const char* mes);

void npc_setcells(struct npc_data* nd);
void npc_unsetcells(struct npc_data* nd);
void npc_movenpc(struct npc_data* nd, int x, int y);
int npc_enable(const char* name, int flag);
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
int npc_gettimerevent_tick(struct npc_data* nd);
int npc_settimerevent_tick(struct npc_data* nd, int newtimer);
int npc_remove_map(struct npc_data* nd);
void npc_unload_duplicates (struct npc_data* nd);
int npc_unload(struct npc_data* nd);
int npc_reload(void);
void npc_read_event_script(void);
int npc_script_event(struct map_session_data* sd, enum npce_event type);

int npc_duplicate4instance(struct npc_data *snd, int m);
int npc_cashshop_buy(struct map_session_data* sd, unsigned short nameid, int amount, int points);

void npc_trader_count_funds(struct npc_data *nd, struct map_session_data *sd);
bool npc_trader_pay(struct npc_data *nd, struct map_session_data *sd, int price, int points);
void npc_trader_update(int master);
int npc_market_buylist(struct map_session_data* sd, unsigned short list_size, struct packet_npc_market_purchase *p);
bool npc_trader_open(struct map_session_data *sd, struct npc_data *nd);
void npc_market_fromsql(void);
void npc_market_tosql(struct npc_data *nd, unsigned short index);
void npc_market_delfromsql(struct npc_data *nd, unsigned short index);

extern struct npc_data* fake_nd;

// @commands (script-based) 
int npc_do_atcmd_event(struct map_session_data* sd, const char* command, const char* message, const char* eventname);

#endif /* _NPC_H_ */
