﻿// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/core.h"
#include "../common/timer.h"
#include "../common/grfio.h"
#include "../common/malloc.h"
#include "../common/random.h"
#include "../common/socket.h" // WFIFO*()
#include "../common/showmsg.h"

#include "../common/nullpo.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "../common/ers.h"

#include "map.h"
#include "path.h"
#include "chrif.h"
#include "clan.h"
#include "clif.h"
#include "duel.h"
#include "intif.h"
#include "npc.h"
#include "pc.h"
#include "status.h"
#include "mob.h"
#include "npc.h" // npc_setcells(), npc_unsetcells()
#include "chat.h"
#include "itemdb.h"
#include "storage.h"
#include "skill.h"
#include "trade.h"
#include "party.h"
#include "unit.h"
#include "battle.h"
#include "battleground.h"
#include "quest.h"
#include "script.h"
#include "mapreg.h"
#include "guild.h"
#include "pet.h"
#include "homunculus.h"
#include "instance.h"
#include "mercenary.h"
#include "elemental.h"
#include "atcommand.h"
#include "log.h"
#ifndef TXT_ONLY
#include "mail.h"
#endif
#include "achievement.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifndef TXT_ONLY
char default_codepage[32] = "";

int map_server_port = 3306;
char map_server_ip[32] = "127.0.0.1";
char map_server_id[32] = "ragnarok";
char map_server_pw[32] = "ragnarok";
char map_server_db[32] = "ragnarok";
Sql* mmysql_handle;

int db_use_sqldbs = 0;
char item_db_db[32] = "item_db";
char item_db2_db[32] = "item_db2";
char mob_db_db[32] = "mob_db";
char mob_db2_db[32] = "mob_db2";
char market_table[32] = "npc_market_data";
char db_roulette_table[32] = "db_roulette";
char vendings_db[32] = "vendings";
char vending_items_db[32] = "vending_items";

// log database
char log_db_ip[32] = "127.0.0.1";
int log_db_port = 3306;
char log_db_id[32] = "ragnarok";
char log_db_pw[32] = "ragnarok";
char log_db_db[32] = "log";
Sql* logmysql_handle;

#endif /* not TXT_ONLY */

// This param using for sending mainchat
// messages like whispers to this nick. [LuzZza]
char main_chat_nick[16] = "Main";

char *INTER_CONF_NAME;
char *LOG_CONF_NAME;
char *MAP_CONF_NAME;
char *BATTLE_CONF_FILENAME;
char *ATCOMMAND_CONF_FILENAME;
char *SCRIPT_CONF_NAME;
char *MSG_CONF_NAME_EN;
char *GRF_PATH_FILENAME;

static int map_users=0;

#define block_free_max 1048576
struct block_list *block_free[block_free_max];
static int block_free_count = 0, block_free_lock = 0;

#define BL_LIST_MAX 1048576
static struct block_list *bl_list[BL_LIST_MAX];
static int bl_list_count = 0;

#define MAP_MAX_MSG 1000

struct map_data map[MAX_MAP_PER_SERVER];
int map_num = 0;
int map_port=0;

int autosave_interval = DEFAULT_AUTOSAVE_INTERVAL;
int minsave_interval = 100;
int save_settings = 0xFFFF;
bool agit_flag = false;
bool agit2_flag = false;
bool agit3_flag = false;
int night_flag = 0; // 0=day, 1=night [Yor]

struct charid_request {
	struct charid_request* next;
	int charid;// who want to be notified of the nick
};
struct charid2nick {
	char nick[NAME_LENGTH];
	struct charid_request* requests;// requests of notification on this nick
};

// This is the main header found at the very beginning of the map cache
struct map_cache_main_header {
	uint32 file_size;
	uint16 map_count;
};

// This is the header appended before every compressed map cells info in the map cache
struct map_cache_map_info {
	char name[MAP_NAME_LENGTH];
	int16 xs;
	int16 ys;
	int32 len;
};

char map_cache_file[256]="db/map_cache.dat";
char db_path[256] = "db";
char motd_txt[256] = "conf/motd.txt";
char help_txt[256] = "conf/help.txt";
char help2_txt[256] = "conf/help2.txt";
char charhelp_txt[256] = "conf/charhelp.txt";

char wisp_server_name[NAME_LENGTH] = "Server"; // can be modified in char-server configuration file

int console = 0;
int enable_spy = 0; //To enable/disable @spy commands, which consume too much cpu time when sending packets. [Skotlex]
int enable_grf = 0;	//To enable/disable reading maps from GRF files, bypassing mapcache [blackhole89]

/*==========================================
 * server player count (of all mapservers)
 *------------------------------------------*/
void map_setusers(int users)
{
	map_users = users;
}

int map_getusers(void)
{
	return map_users;
}

/*==========================================
 * server player count (this mapserver only)
 *------------------------------------------*/
int map_usercount(void)
{
	return pc_db->size(pc_db);
}

//
// blockíœ‚ÌˆÀ‘S«Šm•Û?—
//

/*==========================================
 * block‚ðfree‚·‚é‚Æ‚«free‚Ì?‚í‚è‚ÉŒÄ‚Ô
 * ƒƒbƒN‚³‚ê‚Ä‚¢‚é‚Æ‚«‚Íƒoƒbƒtƒ@‚É‚½‚ß‚é
 *------------------------------------------*/
int map_freeblock (struct block_list *bl)
{
	nullpo_retr(block_free_lock, bl);
	if (block_free_lock == 0 || block_free_count >= block_free_max)
	{
		aFree(bl);
		bl = NULL;
		if (block_free_count >= block_free_max)
			ShowWarning("map_freeblock: too many free block! %d %d\n", block_free_count, block_free_lock);
	} else
		block_free[block_free_count++] = bl;

	return block_free_lock;
}
/*==========================================
 * block‚Ìfree‚ðˆêŽsI‚É‹ÖŽ~‚·‚é
 *------------------------------------------*/
int map_freeblock_lock (void)
{
	return ++block_free_lock;
}

/*==========================================
 * block‚Ìfree‚ÌƒƒbƒN‚ð‰ðœ‚·‚é
 * ‚±‚Ì‚Æ‚«AƒƒbƒN‚ªŠ®‘S‚É‚È‚­‚È‚é‚Æ
 * ƒoƒbƒtƒ@‚É‚½‚Ü‚Á‚Ä‚¢‚½block‚ð‘S•”íœ
 *------------------------------------------*/
int map_freeblock_unlock (void)
{
	if ((--block_free_lock) == 0) {
		int i;
		for (i = 0; i < block_free_count; i++)
		{
			aFree(block_free[i]);
			block_free[i] = NULL;
		}
		block_free_count = 0;
	} else if (block_free_lock < 0) {
		ShowError("map_freeblock_unlock: lock count < 0 !\n");
		block_free_lock = 0;
	}

	return block_free_lock;
}

// Timer function to check if there some remaining lock and remove them if so.
// Called each 1s
int map_freeblock_timer(int tid, int64 tick, int id, intptr_t data)
{
	if (block_free_lock > 0) {
		ShowError("map_freeblock_timer: block_free_lock(%d) is invalid.\n", block_free_lock);
		block_free_lock = 1;
		map_freeblock_unlock();
	}

	return 0;
}

//
// block‰»?—
//
/*==========================================
 * map[]‚Ìblock_list‚©‚ç?‚ª‚Á‚Ä‚¢‚éê‡‚É
 * bl->prev‚Ébl_head‚ÌƒAƒhƒŒƒX‚ð“ü‚ê‚Ä‚¨‚­
 *------------------------------------------*/
static struct block_list bl_head;

#ifdef CELL_NOSTACK
/*==========================================
 * These pair of functions update the counter of how many objects
 * lie on a tile.
 *------------------------------------------*/
static void map_addblcell(struct block_list *bl)
{
	if( bl->m<0 || bl->x<0 || bl->x>=map[bl->m].xs || bl->y<0 || bl->y>=map[bl->m].ys || !(bl->type&BL_CHAR) )
		return;
	map[bl->m].cell[bl->x+bl->y*map[bl->m].xs].cell_bl++;
	return;
}

static void map_delblcell(struct block_list *bl)
{
	if( bl->m <0 || bl->x<0 || bl->x>=map[bl->m].xs || bl->y<0 || bl->y>=map[bl->m].ys || !(bl->type&BL_CHAR) )
		return;
	map[bl->m].cell[bl->x+bl->y*map[bl->m].xs].cell_bl--;
}
#endif

/*==========================================
 * Adds a block to the map.
 * Returns 0 on success, 1 on failure (illegal coordinates).
 *------------------------------------------*/
int map_addblock(struct block_list* bl)
{
	int m, x, y, pos;

	nullpo_ret(bl);

	if (bl->prev != NULL) {
		ShowError("map_addblock: bl->prev != NULL\n");
		return 1;
	}

	m = bl->m;
	x = bl->x;
	y = bl->y;
	if( m < 0 || m >= map_num )
	{
		ShowError("map_addblock: invalid map id (%d), only %d are loaded.\n", m, map_num);
		return 1;
	}
	if( x < 0 || x >= map[m].xs || y < 0 || y >= map[m].ys )
	{
		ShowError("map_addblock: out-of-bounds coordinates (\"%s\",%d,%d), map is %dx%d\n", map[m].name, x, y, map[m].xs, map[m].ys);
		return 1;
	}

	pos = x/BLOCK_SIZE+(y/BLOCK_SIZE)*map[m].bxs;

	if (bl->type == BL_MOB) {
		bl->next = map[m].block_mob[pos];
		bl->prev = &bl_head;
		if (bl->next) bl->next->prev = bl;
		map[m].block_mob[pos] = bl;
	} else {
		bl->next = map[m].block[pos];
		bl->prev = &bl_head;
		if (bl->next) bl->next->prev = bl;
		map[m].block[pos] = bl;
	}

#ifdef CELL_NOSTACK
	map_addblcell(bl);
#endif
	
	return 0;
}

/*==========================================
 * Removes a block from the map.
 *------------------------------------------*/
int map_delblock(struct block_list* bl)
{
	int pos;
	nullpo_ret(bl);

	// ?‚Éblocklist‚©‚ç?‚¯‚Ä‚¢‚é
	if (bl->prev == NULL) {
		if (bl->next != NULL) {
			// prev‚ªNULL‚Ånext‚ªNULL‚Å‚È‚¢‚Ì‚Í—L‚Á‚Ä‚Í‚È‚ç‚È‚¢
			ShowError("map_delblock error : bl->next!=NULL\n");
		}
		return 0;
	}

#ifdef CELL_NOSTACK
	map_delblcell(bl);
#endif
	
	pos = bl->x/BLOCK_SIZE+(bl->y/BLOCK_SIZE)*map[bl->m].bxs;

	if (bl->next)
		bl->next->prev = bl->prev;
	if (bl->prev == &bl_head) {
		// ƒŠƒXƒg‚Ì“ª‚È‚Ì‚ÅAmap[]‚Ìblock_list‚ðXV‚·‚é
		if (bl->type == BL_MOB) {
			map[bl->m].block_mob[pos] = bl->next;
		} else {
			map[bl->m].block[pos] = bl->next;
		}
	} else {
		bl->prev->next = bl->next;
	}
	bl->next = NULL;
	bl->prev = NULL;

	return 0;
}

/*==========================================
 * Moves a block a x/y target position. [Skotlex]
 * Pass flag as 1 to prevent doing skill_unit_move checks
 * (which are executed by default on BL_CHAR types)
 * @param bl : block(object) to move
 * @param x1 : new x position
 * @param y1 : new y position
 * @param tick : when this was scheduled
 * @return 0:sucess, 1:fail
 *------------------------------------------*/
int map_moveblock(struct block_list *bl, int x1, int y1, int64 tick)
{
	int x0 = bl->x, y0 = bl->y;
	struct status_change *sc = NULL;
	int moveblock = ( x0/BLOCK_SIZE != x1/BLOCK_SIZE || y0/BLOCK_SIZE != y1/BLOCK_SIZE);
	struct map_session_data *sd = BL_CAST(BL_PC,bl);

	if (!bl->prev) {
		//Block not in map, just update coordinates, but do naught else.
		bl->x = x1;
		bl->y = y1;
		return 0;	
	}

	//TODO: Perhaps some outs of bounds checking should be placed here?
	if (bl->type&BL_CHAR) {
		sc = status_get_sc(bl);

		skill_unit_move(bl,tick,2);
		status_change_end(bl, SC_CLOSECONFINE, INVALID_TIMER);
		status_change_end(bl, SC_CLOSECONFINE2, INVALID_TIMER);
//		status_change_end(bl, SC_BLADESTOP, INVALID_TIMER); //Won't stop when you are knocked away, go figure...
		status_change_end(bl, SC_TATAMIGAESHI, INVALID_TIMER);
		status_change_end(bl, SC_MAGICROD, INVALID_TIMER);
		status_change_end(bl, SC_ROLLINGCUTTER, INVALID_TIMER);
		status_change_end(bl, SC_SU_STOOP, INVALID_TIMER);
		if (sc->data[SC_PROPERTYWALK] && sc->data[SC_PROPERTYWALK]->val3 >= skill_get_maxcount(sc->data[SC_PROPERTYWALK]->val1, sc->data[SC_PROPERTYWALK]->val2))
			status_change_end(bl, SC_PROPERTYWALK, INVALID_TIMER);

	} else
	if (bl->type == BL_NPC)
		npc_unsetcells((TBL_NPC*)bl);

	if (moveblock) map_delblock(bl);
#ifdef CELL_NOSTACK
	else map_delblcell(bl);
#endif
	bl->x = x1;
	bl->y = y1;
	if (moveblock) {
		if (map_addblock(bl))
			return 1;
	}
#ifdef CELL_NOSTACK
	else map_addblcell(bl);
#endif

	if( bl->type&BL_CHAR ){
		if( sd )
		{ // Shadow Form distances
			struct block_list *d_bl;
			if (sc && sc->data[SC__SHADOWFORM] && ((d_bl = map_id2bl(sc->data[SC__SHADOWFORM]->val2)) == NULL || bl->m != d_bl->m || !check_distance_bl(bl, d_bl, 10)))
				status_change_end(bl,SC__SHADOWFORM,-1);
			if (sd->shadowform_id && ((d_bl = map_id2bl(sd->shadowform_id)) == NULL || !check_distance_bl(bl, d_bl, 10))) {
				if (d_bl) status_change_end(d_bl, SC__SHADOWFORM, -1);
				sd->shadowform_id = 0;
			}
		}

		skill_unit_move(bl,tick,3);
		if (sc && sc->count){
			if (sc->data[SC_CLOAKING])
				skill_check_cloaking(bl, sc->data[SC_CLOAKING]);
			if (sc->data[SC_CAMOUFLAGE])
				skill_check_camouflage(bl, sc->data[SC_CAMOUFLAGE]);
			if (sc->data[SC_DANCING])
				skill_unit_move_unit_group(skill_id2group(sc->data[SC_DANCING]->val2), bl->m, x1-x0, y1-y0);
			if (sc->data[SC_WARM])
				skill_unit_move_unit_group(skill_id2group(sc->data[SC_WARM]->val4), bl->m, x1-x0, y1-y0);
			if (sc->data[SC_NEUTRALBARRIER_MASTER])
				skill_unit_move_unit_group(skill_id2group(sc->data[SC_NEUTRALBARRIER_MASTER]->val2), bl->m, x1-x0, y1-y0);
			if (sc->data[SC_STEALTHFIELD_MASTER])
				skill_unit_move_unit_group(skill_id2group(sc->data[SC_STEALTHFIELD_MASTER]->val2), bl->m, x1-x0, y1-y0);
			if (sc->data[SC_BANDING])
				skill_unit_move_unit_group(skill_id2group(sc->data[SC_BANDING]->val4), bl->m, x1-x0, y1-y0);
			if (sc->data[SC_PROPERTYWALK]){
				if( sc->data[SC_PROPERTYWALK]->val3 < skill_get_maxcount(sc->data[SC_PROPERTYWALK]->val1,sc->data[SC_PROPERTYWALK]->val2) ){
					if( map_find_skill_unit_oncell(bl,bl->x,bl->y,SO_ELECTRICWALK,NULL,0) == NULL && map_find_skill_unit_oncell(bl,bl->x,bl->y,SO_FIREWALK,NULL,0) == NULL )
					{	// These aren't stackable in the same cell.
						if( skill_unitsetting(bl,sc->data[SC_PROPERTYWALK]->val1,sc->data[SC_PROPERTYWALK]->val2,x0, y0,0) )
							sc->data[SC_PROPERTYWALK]->val3++;
					}
				}
			}

			/* Guild Aura Moving */
			if (bl->type == BL_PC && ((TBL_PC*)bl)->state.gmaster_flag) {
				if (sc->data[SC_LEADERSHIP])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_LEADERSHIP]->val4), bl->m, x1 - x0, y1 - y0);
				if (sc->data[SC_GLORYWOUNDS])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_GLORYWOUNDS]->val4), bl->m, x1 - x0, y1 - y0);
				if (sc->data[SC_SOULCOLD])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_SOULCOLD]->val4), bl->m, x1 - x0, y1 - y0);
				if (sc->data[SC_HAWKEYES])
					skill_unit_move_unit_group(skill_id2group(sc->data[SC_HAWKEYES]->val4), bl->m, x1 - x0, y1 - y0);
			}
		}
	} else if (bl->type == BL_NPC)
		npc_setcells((TBL_NPC*)bl);

	return 0;
}
	
/*==========================================
 * Counts specified number of objects on given cell.
 * flag:
 *		0x1 - only count standing units
 *------------------------------------------*/
int map_count_oncell(int m, int x, int y, int type, int flag)
{
	int bx,by;
	struct block_list *bl;
	int count = 0;

	if (x < 0 || y < 0 || (x >= map[m].xs) || (y >= map[m].ys))
		return 0;

	bx = x/BLOCK_SIZE;
	by = y/BLOCK_SIZE;

	if (type&~BL_MOB)
		for( bl = map[m].block[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
			if (bl->x == x && bl->y == y && bl->type&type) {
				if (flag & 1) {
					struct unit_data *ud = unit_bl2ud(bl);
					if (!ud || ud->walktimer == INVALID_TIMER)
						count++;
				}
				else {
					count++;
				}
			}
	
	if (type&BL_MOB)
		for( bl = map[m].block_mob[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
			if (bl->x == x && bl->y == y) {
				if (flag & 1) {
					struct unit_data *ud = unit_bl2ud(bl);
					if (!ud || ud->walktimer == INVALID_TIMER)
						count++;
				}
				else {
					count++;
				}
			}

	return count;
}
/*======================================
 * Look for a skill unit on the given cell.
 * flag&1: check for rivalry.
 *--------------------------------------*/
struct skill_unit* map_find_skill_unit_oncell(struct block_list* target,int x,int y,int skill_id,struct skill_unit* out_unit,int flag) {
	int m,bx,by;
	struct block_list *bl;
	struct skill_unit *unit;
	m = target->m;

	if (x < 0 || y < 0 || (x >= map[m].xs) || (y >= map[m].ys))
		return NULL;

	bx = x/BLOCK_SIZE;
	by = y/BLOCK_SIZE;

	for( bl = map[m].block[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
	{
		if (bl->x != x || bl->y != y || bl->type != BL_SKILL)
			continue;

		unit = (struct skill_unit *) bl;
		if( unit == out_unit || !unit->alive || !unit->group || unit->group->skill_id != skill_id )
			continue;
		if( !(flag&1) || battle_check_target(&unit->bl,target,unit->group->target_flag) > 0 )
			return unit;
	}
	return NULL;
}

/*==========================================
 * Adapted from foreachinarea for an easier invocation. [Skotlex]
 *------------------------------------------*/
int map_foreachinrange(int (*func)(struct block_list*,va_list), struct block_list* center, int range, int type, ...)
{
	int bx,by,m;
	int returnCount =0;	//total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount=bl_list_count,i;
	int x0,x1,y0,y1;

	m = center->m;
	x0 = max(center->x-range, 0);
	y0 = max(center->y-range, 0);
	x1 = min(center->x+range, map[m].xs-1);
	y1 = min(center->y+range, map[m].ys-1);
	
	if (type&~BL_MOB)
		for (by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++) {
			for(bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++) {
				for( bl = map[m].block[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
				{
					if( bl->type&type
						&& bl->x>=x0 && bl->x<=x1 && bl->y>=y0 && bl->y<=y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
					  	&& bl_list_count<BL_LIST_MAX)
						bl_list[bl_list_count++]=bl;
				}
			}
		}
	if(type&BL_MOB)
		for(by=y0/BLOCK_SIZE;by<=y1/BLOCK_SIZE;by++){
			for(bx=x0/BLOCK_SIZE;bx<=x1/BLOCK_SIZE;bx++){
				for( bl = map[m].block_mob[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
				{
					if( bl->x>=x0 && bl->x<=x1 && bl->y>=y0 && bl->y<=y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
						&& bl_list_count<BL_LIST_MAX)
						bl_list[bl_list_count++]=bl;
				}
			}
		}

	if(bl_list_count>=BL_LIST_MAX)
		ShowWarning("map_foreachinrange: block count too many!\n");

	map_freeblock_lock();	// ƒƒ‚ƒŠ‚©‚ç‚Ì‰ð•ú‚ð‹ÖŽ~‚·‚é

	for(i=blockcount;i<bl_list_count;i++)
		if(bl_list[i]->prev)	// —L?‚©‚Ç‚¤‚©ƒ`ƒFƒbƒN
		{
			va_list ap;
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}

	map_freeblock_unlock();	// ‰ð•ú‚ð‹–‰Â‚·‚é

	bl_list_count = blockcount;
	return returnCount;	//[Skotlex]
}

/*==========================================
 * Same as foreachinrange, but there must be a shoot-able range between center and target to be counted in. [Skotlex]
 *------------------------------------------*/
int map_foreachinshootrange(int (*func)(struct block_list*,va_list),struct block_list* center, int range, int type,...)
{
	int bx,by,m;
	int returnCount =0;	//total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount=bl_list_count,i;
	int x0,x1,y0,y1;

	m = center->m;
	if (m < 0)
		return 0;

	x0 = max(center->x-range, 0);
	y0 = max(center->y-range, 0);
	x1 = min(center->x+range, map[m].xs-1);
	y1 = min(center->y+range, map[m].ys-1);

	if (type&~BL_MOB)
		for(by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++) {
			for(bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++) {
				for( bl = map[m].block[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
				{
					if( bl->type&type
						&& bl->x>=x0 && bl->x<=x1 && bl->y>=y0 && bl->y<=y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
						&& path_search_long(NULL,center->m,center->x,center->y,bl->x,bl->y,CELL_CHKWALL)
					  	&& bl_list_count<BL_LIST_MAX)
						bl_list[bl_list_count++]=bl;
				}
			}
		}
	if(type&BL_MOB)
		for(by=y0/BLOCK_SIZE;by<=y1/BLOCK_SIZE;by++){
			for(bx=x0/BLOCK_SIZE;bx<=x1/BLOCK_SIZE;bx++){
				for( bl = map[m].block_mob[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
				{
					if( bl->x>=x0 && bl->x<=x1 && bl->y>=y0 && bl->y<=y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
						&& path_search_long(NULL,center->m,center->x,center->y,bl->x,bl->y,CELL_CHKWALL)
						&& bl_list_count<BL_LIST_MAX)
						bl_list[bl_list_count++]=bl;
				}
			}
		}

	if(bl_list_count>=BL_LIST_MAX)
			ShowWarning("map_foreachinrange: block count too many!\n");

	map_freeblock_lock();	// ƒƒ‚ƒŠ‚©‚ç‚Ì‰ð•ú‚ð‹ÖŽ~‚·‚é

	for(i=blockcount;i<bl_list_count;i++)
		if(bl_list[i]->prev)	// —L?‚©‚Ç‚¤‚©ƒ`ƒFƒbƒN
		{
			va_list ap;
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}

	map_freeblock_unlock();	// ‰ð•ú‚ð‹–‰Â‚·‚é

	bl_list_count = blockcount;
	return returnCount;	//[Skotlex]
}

/*========================================== [Playtester]
* Same as foreachinarea, but there must be a shoot-able range between area center and target.
* @param m: ID of map
* @param x0: West end of area
* @param y0: South end of area
* @param x1: East end of area
* @param y1: North end of area
* @param type: Type of bl to search for
*------------------------------------------*/
int map_foreachinshootarea(int(*func)(struct block_list*, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int type, ...)
{
	int bx, by, cx, cy;
	int returnCount = 0;	//total sum of returned values of func()
	struct block_list *bl;
	int blockcount = bl_list_count, i;
	va_list ap;

	if (m < 0 || m >= map_num)
		return 0;

	if (x1 < x0)
		swap(x0, x1);
	if (y1 < y0)
		swap(y0, y1);

	x0 = i16max(x0, 0);
	y0 = i16max(y0, 0);
	x1 = i16min(x1, map[m].xs - 1);
	y1 = i16min(y1, map[m].ys - 1);

	cx = x0 + (x1 - x0) / 2;
	cy = y0 + (y1 - y0) / 2;

	if (type&~BL_MOB)
		for (by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++)
			for (bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++)
				for (bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next)
					if (bl->type&type && bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1
						&& path_search_long(NULL, m, cx, cy, bl->x, bl->y, CELL_CHKWALL)
						&& bl_list_count < BL_LIST_MAX)
						bl_list[bl_list_count++] = bl;

	if (type&BL_MOB)
		for (by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++)
			for (bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++)
				for (bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next)
					if (bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1
						&& path_search_long(NULL, m, cx, cy, bl->x, bl->y, CELL_CHKWALL)
						&& bl_list_count < BL_LIST_MAX)
						bl_list[bl_list_count++] = bl;

	if (bl_list_count >= BL_LIST_MAX)
		ShowWarning("map_foreachinshootarea: block count too many!\n");

	map_freeblock_lock();

	for (i = blockcount; i < bl_list_count; i++)
		if (bl_list[i]->prev) { //func() may delete this bl_list[] slot, checking for prev ensures it wasn't queued for deletion.
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}

	map_freeblock_unlock();

	bl_list_count = blockcount;
	return returnCount;
}

/*==========================================
 * Adapted from forcountinarea for an easier invocation. [pakpil]
 *------------------------------------------*/
int map_forcountinrange(int (*func)(struct block_list*,va_list), struct block_list* center, int range, int count, int type, ...)
{
	int bx,by,m;
	int returnCount =0;	//total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount=bl_list_count,i;
	int x0,x1,y0,y1;

	m = center->m;
	x0 = max(center->x-range, 0);
	y0 = max(center->y-range, 0);
	x1 = min(center->x+range, map[m].xs-1);
	y1 = min(center->y+range, map[m].ys-1);
	
	if (type&~BL_MOB)
		for (by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++) {
			for(bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++) {
				for( bl = map[m].block[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
				{
					if( bl->type&type
						&& bl->x>=x0 && bl->x<=x1 && bl->y>=y0 && bl->y<=y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
					  	&& bl_list_count<BL_LIST_MAX)
						bl_list[bl_list_count++]=bl;
				}
			}
		}
	if(type&BL_MOB)
		for(by=y0/BLOCK_SIZE;by<=y1/BLOCK_SIZE;by++){
			for(bx=x0/BLOCK_SIZE;bx<=x1/BLOCK_SIZE;bx++){
				for( bl = map[m].block_mob[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
				{
					if( bl->x>=x0 && bl->x<=x1 && bl->y>=y0 && bl->y<=y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
						&& bl_list_count<BL_LIST_MAX)
						bl_list[bl_list_count++]=bl;
				}
			}
		}

	if(bl_list_count>=BL_LIST_MAX)
		ShowWarning("map_forcountinrange: block count too many!\n");

	map_freeblock_lock();	// ���������������������~����

	for(i=blockcount;i<bl_list_count;i++)
		if(bl_list[i]->prev)	// �L?���������`�F�b�N
		{
			va_list ap;
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
			if( count && returnCount >= count )
				break;
		}

	map_freeblock_unlock();	// ��������������

	bl_list_count = blockcount;
	return returnCount;	//[Skotlex]
}

/*==========================================
 * map m (x0,y0)-(x1,y1)?‚Ì‘Sobj‚É?‚µ‚Ä
 * func‚ðŒÄ‚Ô
 * type!=0 ‚È‚ç‚»‚ÌŽí—Þ‚Ì‚Ý
 *------------------------------------------*/
int map_foreachinarea(int (*func)(struct block_list*,va_list), int m, int x0, int y0, int x1, int y1, int type, ...)
{
	int bx,by;
	int returnCount =0;	//total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount=bl_list_count,i;

	if (m < 0 || m >= map_num)
		return 0;
	if (x1 < x0)
	{	//Swap range
		swap(x0, x1);
	}
	if (y1 < y0)
	{
		swap(y0, y1);
	}
	x0 = max(x0, 0);
	y0 = max(y0, 0);
	x1 = min(x1, map[m].xs-1);
	y1 = min(y1, map[m].ys-1);
	if (type&~BL_MOB)
		for(by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++)
			for(bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++)
				for( bl = map[m].block[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
					if(bl->type&type && bl->x>=x0 && bl->x<=x1 && bl->y>=y0 && bl->y<=y1 && bl_list_count<BL_LIST_MAX)
						bl_list[bl_list_count++]=bl;

	if(type&BL_MOB)
		for(by=y0/BLOCK_SIZE;by<=y1/BLOCK_SIZE;by++)
			for(bx=x0/BLOCK_SIZE;bx<=x1/BLOCK_SIZE;bx++)
				for( bl = map[m].block_mob[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
					if(bl->x>=x0 && bl->x<=x1 && bl->y>=y0 && bl->y<=y1 && bl_list_count<BL_LIST_MAX)
						bl_list[bl_list_count++]=bl;

	if(bl_list_count>=BL_LIST_MAX)
		ShowWarning("map_foreachinarea: block count too many!\n");

	map_freeblock_lock();	// ƒƒ‚ƒŠ‚©‚ç‚Ì‰ð•ú‚ð‹ÖŽ~‚·‚é

	for(i=blockcount;i<bl_list_count;i++)
		if(bl_list[i]->prev)	// —L?‚©‚Ç‚¤‚©ƒ`ƒFƒbƒN
		{
			va_list ap;
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}

	map_freeblock_unlock();	// ‰ð•ú‚ð‹–‰Â‚·‚é

	bl_list_count = blockcount;
	return returnCount;	//[Skotlex]
}

int map_forcountinarea(int (*func)(struct block_list*,va_list), int m, int x0, int y0, int x1, int y1, int count, int type, ...)
{
	int bx,by;
	int returnCount =0;	//total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount=bl_list_count,i;

	if (m < 0)
		return 0;
	if (x1 < x0)
	{	//Swap range
		swap(x0, x1);
	}
	if (y1 < y0)
	{
		swap(y0, y1);
	}
	x0 = max(x0, 0);
	y0 = max(y0, 0);
	x1 = min(x1, map[m].xs-1);
	y1 = min(y1, map[m].ys-1);

	if (type&~BL_MOB)
		for(by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by++)
			for(bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx++)
				for( bl = map[m].block[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
					if(bl->type&type && bl->x>=x0 && bl->x<=x1 && bl->y>=y0 && bl->y<=y1 && bl_list_count<BL_LIST_MAX)
						bl_list[bl_list_count++]=bl;

	if(type&BL_MOB)
		for(by=y0/BLOCK_SIZE;by<=y1/BLOCK_SIZE;by++)
			for(bx=x0/BLOCK_SIZE;bx<=x1/BLOCK_SIZE;bx++)
				for( bl = map[m].block_mob[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
					if(bl->x>=x0 && bl->x<=x1 && bl->y>=y0 && bl->y<=y1 && bl_list_count<BL_LIST_MAX)
						bl_list[bl_list_count++]=bl;

	if(bl_list_count>=BL_LIST_MAX)
		ShowWarning("map_foreachinarea: block count too many!\n");

	map_freeblock_lock();	// ƒƒ‚ƒŠ‚©‚ç‚Ì‰ð•ú‚ð‹ÖŽ~‚·‚é

	for(i=blockcount;i<bl_list_count;i++)
		if(bl_list[i]->prev)	// —L?‚©‚Ç‚¤‚©ƒ`ƒFƒbƒN
		{
			va_list ap;
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
			if( count && returnCount >= count )
				break;
		}

	map_freeblock_unlock();	// ‰ð•ú‚ð‹–‰Â‚·‚é

	bl_list_count = blockcount;
	return returnCount;	//[Skotlex]
}

/*==========================================
 * ‹éŒ`(x0,y0)-(x1,y1)‚ª(dx,dy)ˆÚ“®‚µ‚½Žb?
 * —ÌˆæŠO‚É‚È‚é—Ìˆæ(‹éŒ`‚©LŽšŒ`)?‚Ìobj‚É
 * ?‚µ‚Äfunc‚ðŒÄ‚Ô
 *
 * dx,dy‚Í-1,0,1‚Ì‚Ý‚Æ‚·‚éi‚Ç‚ñ‚È’l‚Å‚à‚¢‚¢‚Á‚Û‚¢Hj
 *------------------------------------------*/
int map_foreachinmovearea(int (*func)(struct block_list*,va_list), struct block_list* center, int range, int dx, int dy, int type, ...)
{
	int bx,by,m;
	int returnCount =0;  //total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount=bl_list_count,i;
	int x0, x1, y0, y1;

	if (!range) return 0;
	if (!dx && !dy) return 0; //No movement.
	m = center->m;

	x0 = center->x-range;
	x1 = center->x+range;
	y0 = center->y-range;
	y1 = center->y+range;

	if (x1 < x0)
	{	//Swap range
		swap(x0, x1);
	}
	if (y1 < y0)
	{
		swap(y0, y1);
	}
	if(dx==0 || dy==0){
		//Movement along one axis only.
		if(dx==0){
			if(dy<0) //Moving south
				y0=y1+dy+1;
			else //North
				y1=y0+dy-1;
		} else { //dy == 0
			if(dx<0) //West
				x0=x1+dx+1;
			else //East
				x1=x0+dx-1;
		}
		x0 = max(x0, 0);
		y0 = max(y0, 0);
		x1 = min(x1, map[m].xs-1);
		y1 = min(y1, map[m].ys-1);
		for(by=y0/BLOCK_SIZE;by<=y1/BLOCK_SIZE;by++){
			for(bx=x0/BLOCK_SIZE;bx<=x1/BLOCK_SIZE;bx++){
				if (type&~BL_MOB) {
					for( bl = map[m].block[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
					{
						if(bl->type&type &&
							bl->x>=x0 && bl->x<=x1 &&
							bl->y>=y0 && bl->y<=y1 &&
							bl_list_count<BL_LIST_MAX)
							bl_list[bl_list_count++]=bl;
					}
				}
				if (type&BL_MOB) {
					for( bl = map[m].block_mob[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
					{
						if(bl->x>=x0 && bl->x<=x1 &&
							bl->y>=y0 && bl->y<=y1 &&
							bl_list_count<BL_LIST_MAX)
							bl_list[bl_list_count++]=bl;
					}
				}
			}
		}
	}else{
		// Diagonal movement
		x0 = max(x0, 0);
		y0 = max(y0, 0);
		x1 = min(x1, map[m].xs-1);
		y1 = min(y1, map[m].ys-1);
		for(by=y0/BLOCK_SIZE;by<=y1/BLOCK_SIZE;by++){
			for(bx=x0/BLOCK_SIZE;bx<=x1/BLOCK_SIZE;bx++){
				if (type & ~BL_MOB) {
					for( bl = map[m].block[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
					{
						if( bl->type&type &&
							bl->x>=x0 && bl->x<=x1 &&
							bl->y>=y0 && bl->y<=y1 &&
							bl_list_count<BL_LIST_MAX )
						if((dx>0 && bl->x<x0+dx) ||
							(dx<0 && bl->x>x1+dx) ||
							(dy>0 && bl->y<y0+dy) ||
							(dy<0 && bl->y>y1+dy))
							bl_list[bl_list_count++]=bl;
					}
				}
				if (type & BL_MOB) {
					for( bl = map[m].block_mob[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
					{
						if( bl->x>=x0 && bl->x<=x1 &&
							bl->y>=y0 && bl->y<=y1 &&
							bl_list_count<BL_LIST_MAX)
						if((dx>0 && bl->x<x0+dx) ||
							(dx<0 && bl->x>x1+dx) ||
							(dy>0 && bl->y<y0+dy) ||
							(dy<0 && bl->y>y1+dy))
							bl_list[bl_list_count++]=bl;
					}
				}
			}
		}

	}

	if(bl_list_count>=BL_LIST_MAX)
		ShowWarning("map_foreachinmovearea: block count too many!\n");

	map_freeblock_lock();	// ƒƒ‚ƒŠ‚©‚ç‚Ì‰ð•ú‚ð‹ÖŽ~‚·‚é

	for(i=blockcount;i<bl_list_count;i++)
		if(bl_list[i]->prev)
		{
			va_list ap;
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}

	map_freeblock_unlock();	// ‰ð•ú‚ð‹–‰Â‚·‚é

	bl_list_count = blockcount;
	return returnCount;
}

// -- moonsoul	(added map_foreachincell which is a rework of map_foreachinarea but
//			 which only checks the exact single x/y passed to it rather than an
//			 area radius - may be more useful in some instances)
//
int map_foreachincell(int (*func)(struct block_list*,va_list), int m, int x, int y, int type, ...)
{
	int bx,by;
	int returnCount =0;  //total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount=bl_list_count,i;

	if (x < 0 || y < 0 || x >= map[m].xs || y >= map[m].ys) return 0;

	by=y/BLOCK_SIZE;
	bx=x/BLOCK_SIZE;

	if(type&~BL_MOB)
		for( bl = map[m].block[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
			if(bl->type&type && bl->x==x && bl->y==y && bl_list_count<BL_LIST_MAX)
				bl_list[bl_list_count++]=bl;

	if(type&BL_MOB)
		for( bl = map[m].block_mob[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
			if(bl->x==x && bl->y==y && bl_list_count<BL_LIST_MAX)
				bl_list[bl_list_count++]=bl;

	if(bl_list_count>=BL_LIST_MAX)
		ShowWarning("map_foreachincell: block count too many!\n");

	map_freeblock_lock();	// ƒƒ‚ƒŠ‚©‚ç‚Ì‰ð•ú‚ð‹ÖŽ~‚·‚é

	for(i=blockcount;i<bl_list_count;i++)
		if(bl_list[i]->prev)	// —L?‚©‚Ç‚¤‚©ƒ`ƒFƒbƒN
		{
			va_list ap;
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}

	map_freeblock_unlock();	// ‰ð•ú‚ð‹–‰Â‚·‚é

	bl_list_count = blockcount;
	return returnCount;
}

/*============================================================
* For checking a path between two points (x0, y0) and (x1, y1)
*------------------------------------------------------------*/
int map_foreachinpath(int (*func)(struct block_list*,va_list),int m,int x0,int y0,int x1,int y1,int range,int length, int type,...)
{
	int returnCount =0;  //total sum of returned values of func() [Skotlex]
//////////////////////////////////////////////////////////////
//
// sharp shooting 3 [Skotlex]
//
//////////////////////////////////////////////////////////////
// problem:
// Same as Sharp Shooting 1. Hits all targets within range of
// the line.
// (t1,t2 t3 and t4 get hit)
//
//     target 1
//      x t4
//     t2
// t3 x
//   x
//  S
//////////////////////////////////////////////////////////////
// Methodology: 
// My trigonometrics and math are a little rusty... so the approach I am writing 
// here is basicly do a double for to check for all targets in the square that 
// contains the initial and final positions (area range increased to match the 
// radius given), then for each object to test, calculate the distance to the 
// path and include it if the range fits and the target is in the line (0<k<1,
// as they call it).
// The implementation I took as reference is found at 
// http://astronomy.swin.edu.au/~pbourke/geometry/pointline/ 
// (they have a link to a C implementation, too)
// This approach is a lot like #2 commented on this function, which I have no 
// idea why it was commented. I won't use doubles/floats, but pure int math for
// speed purposes. The range considered is always the same no matter how 
// close/far the target is because that's how SharpShooting works currently in
// kRO.

	//Generic map_foreach* variables.
	int i, blockcount = bl_list_count;
	struct block_list *bl;
	int bx, by;
	//method specific variables
	int magnitude2, len_limit; //The square of the magnitude
	int k, xi, yi, xu, yu;
	int mx0 = x0, mx1 = x1, my0 = y0, my1 = y1;
	
	//Avoid needless calculations by not getting the sqrt right away.
	#define MAGNITUDE2(x0, y0, x1, y1) (((x1)-(x0))*((x1)-(x0)) + ((y1)-(y0))*((y1)-(y0)))
	
	if (m < 0)
		return 0;

	len_limit = magnitude2 = MAGNITUDE2(x0,y0, x1,y1);
	if (magnitude2 < 1) //Same begin and ending point, can't trace path.
		return 0;

	if (length)
	{	//Adjust final position to fit in the given area.
		//TODO: Find an alternate method which does not requires a square root calculation.
		k = (int)sqrt((float)magnitude2);
		mx1 = x0 + (x1 - x0)*length/k;
		my1 = y0 + (y1 - y0)*length/k;
		len_limit = MAGNITUDE2(x0,y0, mx1,my1);
	}
	//Expand target area to cover range.
	if (mx0 > mx1)
	{
		mx0+=range;
		mx1-=range;
	} else {
		mx0-=range;
		mx1+=range;
	}
	if (my0 > my1)
	{
		my0+=range;
		my1-=range;
	} else {
		my0-=range;
		my1+=range;
	}

	//The two fors assume mx0 < mx1 && my0 < my1
	if (mx0 > mx1)
	{
		swap(mx0, mx1);
	}
	if (my0 > my1)
	{
		swap(my0, my1);
	}
	
	mx0 = max(mx0, 0);
	my0 = max(my0, 0);
	mx1 = min(mx1, map[m].xs-1);
	my1 = min(my1, map[m].ys-1);
	
	range*=range<<8; //Values are shifted later on for higher precision using int math.
	
	if (type & ~BL_MOB)
		for (by = my0 / BLOCK_SIZE; by <= my1 / BLOCK_SIZE; by++) {
			for(bx=mx0/BLOCK_SIZE;bx<=mx1/BLOCK_SIZE;bx++){
				for( bl = map[m].block[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
				{
					if(bl->prev && bl->type&type && bl_list_count<BL_LIST_MAX)
					{
						xi = bl->x;
						yi = bl->y;
					
						k = (xi-x0)*(x1-x0) + (yi-y0)*(y1-y0);
						if (k < 0 || k > len_limit) //Since more skills use this, check for ending point as well.
							continue;
						
						if (k > magnitude2 && !path_search_long(NULL,m,x0,y0,xi,yi,CELL_CHKWALL))
							continue; //Targets beyond the initial ending point need the wall check.

						//All these shifts are to increase the precision of the intersection point and distance considering how it's
						//int math.
						k = (k<<4)/magnitude2; //k will be between 1~16 instead of 0~1
						xi<<=4;
						yi<<=4;
						xu= (x0<<4) +k*(x1-x0);
						yu= (y0<<4) +k*(y1-y0);
						k = MAGNITUDE2(xi, yi, xu, yu);
						
						//If all dot coordinates were <<4 the square of the magnitude is <<8
						if (k > range)
							continue;

						bl_list[bl_list_count++]=bl;
					}
				}
			}
		}

	if(type&BL_MOB)
		for(by=my0/BLOCK_SIZE;by<=my1/BLOCK_SIZE;by++){
			for(bx=mx0/BLOCK_SIZE;bx<=mx1/BLOCK_SIZE;bx++){
				for( bl = map[m].block_mob[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
				{
					if(bl->prev && bl_list_count<BL_LIST_MAX)
					{
						xi = bl->x;
						yi = bl->y;
						k = (xi-x0)*(x1-x0) + (yi-y0)*(y1-y0);
						if (k < 0 || k > len_limit)
							continue;
				
						if (k > magnitude2 && !path_search_long(NULL,m,x0,y0,xi,yi,CELL_CHKWALL))
							continue; //Targets beyond the initial ending point need the wall check.
	
						k = (k<<4)/magnitude2; //k will be between 1~16 instead of 0~1
						xi<<=4;
						yi<<=4;
						xu= (x0<<4) +k*(x1-x0);
						yu= (y0<<4) +k*(y1-y0);
						k = MAGNITUDE2(xi, yi, xu, yu);
						
						//If all dot coordinates were <<4 the square of the magnitude is <<8
						if (k > range)
							continue;

						bl_list[bl_list_count++]=bl;
					}
				}
			}
		}

	if(bl_list_count>=BL_LIST_MAX)
		ShowWarning("map_foreachinpath: block count too many!\n");

	map_freeblock_lock();

	for(i=blockcount;i<bl_list_count;i++)
		if(bl_list[i]->prev)	//This check is done in case some object gets killed due to further skill processing.
		{
			va_list ap;
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}

	map_freeblock_unlock();

	bl_list_count = blockcount;
	return returnCount;	//[Skotlex]

}

/*========================================== [Playtester]
* Calls the given function for every object of a type that is on a path.
* The path goes into one of the eight directions and the direction is determined by the given coordinates.
* The path has a length, a width and an offset.
* The cost for diagonal movement is the same as for horizontal/vertical movement.
* @param m: ID of map
* @param x0: Start X
* @param y0: Start Y
* @param x1: X to calculate direction against
* @param y1: Y to calculate direction against
* @param range: Determines width of the path (width = range*2+1 cells)
* @param length: Length of the path
* @param offset: Moves the whole path, half-length for diagonal paths
* @param type: Type of bl to search for
*------------------------------------------*/
int map_foreachindir(int(*func)(struct block_list*, va_list), int16 m, int16 x0, int16 y0, int16 x1, int16 y1, int16 range, int length, int offset, int type, ...)
{
	int returnCount = 0;  //Total sum of returned values of func()

	int i, blockcount = bl_list_count;
	struct block_list *bl;
	int bx, by;
	int mx0, mx1, my0, my1, rx, ry;
	uint8 dir = map_calc_dir_xy(x0, y0, x1, y1, 6);
	short dx = dirx[dir];
	short dy = diry[dir];
	va_list ap;

	if (m < 0)
		return 0;
	if (range < 0)
		return 0;
	if (length < 1)
		return 0;
	if (offset < 0)
		return 0;

	//Special offset handling for diagonal paths
	if (offset && (dir % 2)) {
		//So that diagonal paths can attach to each other, we have to work with half-tile offsets
		offset = (2 * offset) - 1;
		//To get the half-tiles we need to increase length by one
		length++;
	}

	//Get area that needs to be checked
	mx0 = x0 + dx * (offset / ((dir % 2) + 1));
	my0 = y0 + dy * (offset / ((dir % 2) + 1));
	mx1 = x0 + dx * (offset / ((dir % 2) + 1) + length - 1);
	my1 = y0 + dy * (offset / ((dir % 2) + 1) + length - 1);

	//The following assumes mx0 < mx1 && my0 < my1
	if (mx0 > mx1)
		swap(mx0, mx1);
	if (my0 > my1)
		swap(my0, my1);

	//Apply width to the path by turning 90 degrees
	mx0 -= abs(range*dirx[(dir + 2) % 8]);
	my0 -= abs(range*diry[(dir + 2) % 8]);
	mx1 += abs(range*dirx[(dir + 2) % 8]);
	my1 += abs(range*diry[(dir + 2) % 8]);

	mx0 = max(mx0, 0);
	my0 = max(my0, 0);
	mx1 = min(mx1, map[m].xs - 1);
	my1 = min(my1, map[m].ys - 1);

	if (type&~BL_MOB) {
		for (by = my0 / BLOCK_SIZE; by <= my1 / BLOCK_SIZE; by++) {
			for (bx = mx0 / BLOCK_SIZE; bx <= mx1 / BLOCK_SIZE; bx++) {
				for (bl = map[m].block[bx + by * map[m].bxs]; bl != NULL; bl = bl->next) {
					if (bl->prev && bl->type&type && bl_list_count < BL_LIST_MAX) {
						//Check if inside search area
						if (bl->x < mx0 || bl->x > mx1 || bl->y < my0 || bl->y > my1)
							continue;
						//What matters now is the relative x and y from the start point
						rx = (bl->x - x0);
						ry = (bl->y - y0);
						//Do not hit source cell
						if (rx == 0 && ry == 0)
							continue;
						//This turns it so that the area that is hit is always with positive rx and ry
						rx *= dx;
						ry *= dy;
						//These checks only need to be done for diagonal paths
						if (dir % 2) {
							//Check for length
							if ((rx + ry < offset) || (rx + ry > 2 * (length + (offset / 2) - 1)))
								continue;
							//Check for width
							if (abs(rx - ry) > 2 * range)
								continue;
						}
						//Everything else ok, check for line of sight from source
						if (!path_search_long(NULL, m, x0, y0, bl->x, bl->y, CELL_CHKWALL))
							continue;
						//All checks passed, add to list
						bl_list[bl_list_count++] = bl;
					}
				}
			}
		}
	}
	if (type&BL_MOB) {
		for (by = my0 / BLOCK_SIZE; by <= my1 / BLOCK_SIZE; by++) {
			for (bx = mx0 / BLOCK_SIZE; bx <= mx1 / BLOCK_SIZE; bx++) {
				for (bl = map[m].block_mob[bx + by * map[m].bxs]; bl != NULL; bl = bl->next) {
					if (bl->prev && bl_list_count < BL_LIST_MAX) {
						//Check if inside search area
						if (bl->x < mx0 || bl->x > mx1 || bl->y < my0 || bl->y > my1)
							continue;
						//What matters now is the relative x and y from the start point
						rx = (bl->x - x0);
						ry = (bl->y - y0);
						//Do not hit source cell
						if (rx == 0 && ry == 0)
							continue;
						//This turns it so that the area that is hit is always with positive rx and ry
						rx *= dx;
						ry *= dy;
						//These checks only need to be done for diagonal paths
						if (dir % 2) {
							//Check for length
							if ((rx + ry < offset) || (rx + ry > 2 * (length + (offset / 2) - 1)))
								continue;
							//Check for width
							if (abs(rx - ry) > 2 * range)
								continue;
						}
						//Everything else ok, check for line of sight from source
						if (!path_search_long(NULL, m, x0, y0, bl->x, bl->y, CELL_CHKWALL))
							continue;
						//All checks passed, add to list
						bl_list[bl_list_count++] = bl;
					}
				}
			}
		}
	}

	if (bl_list_count >= BL_LIST_MAX)
		ShowWarning("map_foreachindir: block count too many!\n");

	map_freeblock_lock();

	for (i = blockcount; i < bl_list_count; i++)
		if (bl_list[i]->prev) { //func() may delete this bl_list[] slot, checking for prev ensures it wasn't queued for deletion.
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}

	map_freeblock_unlock();

	bl_list_count = blockcount;
	return returnCount;
}

// Copy of map_foreachincell, but applied to the whole map. [Skotlex]
int map_foreachinmap(int (*func)(struct block_list*,va_list), int m, int type,...)
{
	int b, bsize;
	int returnCount =0;  //total sum of returned values of func() [Skotlex]
	struct block_list *bl;
	int blockcount=bl_list_count,i;

	bsize = map[m].bxs * map[m].bys;

	if(type&~BL_MOB)
		for(b=0;b<bsize;b++)
			for( bl = map[m].block[b] ; bl != NULL ; bl = bl->next )
				if(bl->type&type && bl_list_count<BL_LIST_MAX)
					bl_list[bl_list_count++]=bl;

	if(type&BL_MOB)
		for(b=0;b<bsize;b++)
			for( bl = map[m].block_mob[b] ; bl != NULL ; bl = bl->next )
				if(bl_list_count<BL_LIST_MAX)
					bl_list[bl_list_count++]=bl;

	if(bl_list_count>=BL_LIST_MAX)
		ShowWarning("map_foreachinmap: block count too many!\n");

	map_freeblock_lock();	// ƒƒ‚ƒŠ‚©‚ç‚Ì‰ð•ú‚ð‹ÖŽ~‚·‚é

	for(i=blockcount;i<bl_list_count;i++)
		if(bl_list[i]->prev)	// —L?‚©‚Ç‚¤‚©ƒ`ƒFƒbƒN
		{
			va_list ap;
			va_start(ap, type);
			returnCount += func(bl_list[i], ap);
			va_end(ap);
		}

	map_freeblock_unlock();	// ‰ð•ú‚ð‹–‰Â‚·‚é

	bl_list_count = blockcount;
	return returnCount;
}

/**
 * Applies func to every block_list object of bl_type type on all maps
 * of instance instance_id.
 * Returns the sum of values returned by func.
 * @param func Function to be applied
 * @param m Map id
 * @param type enum bl_type
 * @param ... Extra arguments for func
 * @return Sum of the values returned by func
 */
int map_foreachininstance(int (*func)(struct block_list*, va_list), int16 instance_id, int type, ...) {
	int i, returnCount=0;
	va_list ap;

	va_start(ap, type);

	for (i = 0; i < instance[instance_id].num_map; i++) {
		int m = instance[instance_id].map[i];
		va_list apcopy;
		va_copy(apcopy, ap);
		returnCount += map_foreachinmap(func, m, type, apcopy);
		va_end(apcopy);
	}

	va_end(ap);

	return returnCount;
}

/*==========================================
* Adapted from foreachinrange [LimitLine]
* Use it to pick 'max' random targets.
* Unlike other map_foreach* functions, this
* will return the ID of the last bl processed.
* max = max number of bl's it'll operate on.
* ignore_id = if set, the bl with the given ID will be ignored.
*------------------------------------------*/
int map_pickrandominrange(int (*func)(struct block_list*,va_list), struct block_list* center, int range, int max, int ignore_id, int type, ...)
{
	int bx,by,m;
	struct block_list *bl;
	int blockcount=bl_list_count,i;
	int x0,x1,y0,y1;
	int count = 0;

	m = center->m;
	x0 = max(center->x-range, 0);
	y0 = max(center->y-range, 0);
	x1 = min(center->x+range, map[m].xs-1);
	y1 = min(center->y+range, map[m].ys-1);

	if( type & ~BL_MOB )
		for( by = y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by ++ )
		{
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx ++ )
			{
				for( bl = map[m].block[bx+by*map[m].bxs]; bl != NULL ; bl = bl->next )
				{
					if( bl->type & type
						&& bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
						&& bl_list_count < BL_LIST_MAX )
						bl_list[bl_list_count ++] = bl;
				}
			}
		}

	if( type & BL_MOB )
		for( by=y0 / BLOCK_SIZE; by <= y1 / BLOCK_SIZE; by ++ )
		{
			for( bx = x0 / BLOCK_SIZE; bx <= x1 / BLOCK_SIZE; bx ++ )
			{
				for( bl = map[m].block_mob[bx+by*map[m].bxs]; bl != NULL; bl = bl->next )
				{
					if( bl->x >= x0 && bl->x <= x1 && bl->y >= y0 && bl->y <= y1
#ifdef CIRCULAR_AREA
						&& check_distance_bl(center, bl, range)
#endif
						&& bl_list_count < BL_LIST_MAX )
						bl_list[bl_list_count ++] = bl;
				}
			}
		}

		if( bl_list_count >= BL_LIST_MAX )
			ShowWarning("map_pickrandominrange: block count too many!\n");

		for( i = 0; i < 2 * bl_list_count; i++ )
		{ // Randomize indexes of array. Thanks to vallcrist. [LimitLine]
			int size = bl_list_count, index1 = rnd() % size, index2 = 0, tries = 0;
			struct block_list *temp;
			do
			{
				tries ++; // Prevent infinite loop. [LimitLine]
				index2 = rnd()% size;
			} while ( index2 == index1 && bl_list_count > 1 && tries < 10 );
			temp = bl_list[index1];
			bl_list[index1] = bl_list[index2];
			bl_list[index2] = temp;
		}
		
		map_freeblock_lock(); // ©çÌðúðÖ~·é

		for( i = blockcount; i < bl_list_count; i ++ )
		{
			if( bl_list[i] && bl_list[i]->prev && bl_list[i]->id != ignore_id )
			{
				va_list ap;
				va_start(ap, type);
				func(bl_list[i], ap);
				va_end(ap);
				count ++;
			}
			if( max && count && count >= max )
				break; // Limit the number of targets processed. [LimitLine]
		}
		map_freeblock_unlock();
		
		bl_list_count = blockcount;
		return bl_list[i]?bl_list[i]->id:0; // Return last target hit's ID for Chain Lightning. [LimitLine]
}

/// Generates a new flooritem object id from the interval [MIN_FLOORITEM, MAX_FLOORITEM).
/// Used for floor items, skill units and chatroom objects.
/// @return The new object id
int map_get_new_object_id(void)
{
	static int last_object_id = MIN_FLOORITEM - 1;
	int i;

	// find a free id
	i = last_object_id + 1;
	while( i != last_object_id )
	{
		if( i == MAX_FLOORITEM )
			i = MIN_FLOORITEM;

		if (!idb_exists(id_db, i))
			break;

		++i;
	}

	if( i == last_object_id )
	{
		ShowError("map_addobject: no free object id!\n");
		return 0;
	}

	// update cursor
	last_object_id = i;

	return i;
}

/*==========================================
 * °ƒAƒCƒeƒ€‚ðÁ‚·
 *
 * data==0‚ÌŽbÍtimer‚ÅÁ‚¦‚½Žê * data!=0‚ÌŽbÍE‚¤“™‚ÅÁ‚¦‚½ŽbÆ‚µ‚Ä“®?
 *
 * ŒãŽÒ‚ÍAmap_clearflooritem(id)‚Ö
 * map.h?‚Å#define‚µ‚Ä‚ ‚é
 *------------------------------------------*/
int map_clearflooritem_timer(int tid, int64 tick, int id, intptr_t data)
{
	struct flooritem_data* fitem = (struct flooritem_data*)idb_get(id_db, id);
	if( fitem==NULL || fitem->bl.type!=BL_ITEM || (fitem->cleartimer != tid) )
	{
		ShowError("map_clearflooritem_timer : error\n");
		return 1;
	}

	if (search_petDB_index(fitem->item_data.nameid, PET_EGG) >= 0)
		intif_delete_petdata(MakeDWord(fitem->item_data.card[1], fitem->item_data.card[2]));

	clif_clearflooritem(fitem,0);
	map_deliddb(&fitem->bl);
	map_delblock(&fitem->bl);
	map_freeblock(&fitem->bl);

	return 0;
}

/*==========================================
 * clears a single bl item out of the bazooonga.
 *------------------------------------------*/
void map_clearflooritem(struct block_list *bl) {
	struct flooritem_data* fitem = (struct flooritem_data*)bl;

	if (fitem->cleartimer)
		delete_timer(fitem->cleartimer, map_clearflooritem_timer);

	clif_clearflooritem(fitem, 0);
	map_deliddb(&fitem->bl);
	map_delblock(&fitem->bl);
	map_freeblock(&fitem->bl);
}

/*==========================================
 * (m,x,y) locates a random available free cell around the given coordinates
 * to place an BL_ITEM object. Scan area is 9x9, returns 1 on success.
 * x and y are modified with the target cell when successful.
 *------------------------------------------*/
int map_searchrandfreecell(int m,int *x,int *y,int stack)
{
	int free_cell,i,j;
	int free_cells[9][2];

	for(free_cell=0,i=-1;i<=1;i++){
		if(i+*y<0 || i+*y>=map[m].ys)
			continue;
		for(j=-1;j<=1;j++){
			if(j+*x<0 || j+*x>=map[m].xs)
				continue;
			if(map_getcell(m,j+*x,i+*y,CELL_CHKNOPASS) && !map_getcell(m, j + *x, i + *y, CELL_CHKICEWALL))
				continue;
			//Avoid item stacking to prevent against exploits. [Skotlex]
			if(stack && map_count_oncell(m,j+*x,i+*y, BL_ITEM, 0) > stack)
				continue;
			free_cells[free_cell][0] = j+*x;
			free_cells[free_cell++][1] = i+*y;
		}
	}
	if(free_cell==0)
		return 0;
	free_cell = rnd()%free_cell;
	*x = free_cells[free_cell][0];
	*y = free_cells[free_cell][1];
	return 1;
}


static int map_count_sub(struct block_list *bl,va_list ap)
{
	return 1;
}

/*==========================================
 * Locates a random spare cell around the object given, using range as max 
 * distance from that spot. Used for warping functions. Use range < 0 for 
 * whole map range.
 * Returns 1 on success. when it fails and src is available, x/y are set to src's
 * src can be null as long as flag&1
 * when ~flag&1, m is not needed.
 * Flag values:
 * &1 = random cell must be around given m,x,y, not around src
 * &2 = the target should be able to walk to the target tile.
 * &4 = there shouldn't be any players around the target tile (use the no_spawn_on_player setting)
 *------------------------------------------*/
int map_search_freecell(struct block_list *src, int m, short *x,short *y, int rx, int ry, int flag)
{
	int tries, spawn=0;
	int bx, by;
	int rx2 = 2*rx+1;
	int ry2 = 2*ry+1;

	if( !src && (!(flag&1) || flag&2) )
	{
		ShowDebug("map_search_freecell: Incorrect usage! When src is NULL, flag has to be &1 and can't have &2\n");
		return 0;
	}

	if (flag&1) {
		bx = *x;
		by = *y;
	} else {
		bx = src->x;
		by = src->y;
		m = src->m;
	}
	if (!rx && !ry) {
		//No range? Return the target cell then....
		*x = bx;
		*y = by;
		return map_getcell(m,*x,*y,CELL_CHKREACH);
	}
	
	if (rx >= 0 && ry >= 0) {
		tries = rx2*ry2;
		if (tries > 100) tries = 100;
	} else {
		tries = map[m].xs*map[m].ys;
		if (tries > 500) tries = 500;
	}
	
	while(tries--) {
		*x = (rx >= 0)?(rnd()%rx2-rx+bx):(rnd()%(map[m].xs-2)+1);
		*y = (ry >= 0)?(rnd()%ry2-ry+by):(rnd()%(map[m].ys-2)+1);
		
		if (*x == bx && *y == by)
			continue; //Avoid picking the same target tile.
		
		if (map_getcell(m,*x,*y,CELL_CHKREACH))
		{
			if(flag&2 && !unit_can_reach_pos(src, *x, *y, 1))
				continue;
			if(flag&4) {
				if (spawn >= 100) return 0; //Limit of retries reached.
				if (spawn++ < battle_config.no_spawn_on_player &&
					map_foreachinarea(map_count_sub, m,
						*x-AREA_SIZE, *y-AREA_SIZE,
					  	*x+AREA_SIZE, *y+AREA_SIZE, BL_PC)
				)
				continue;
			}
			return 1;
		}
	}
	*x = bx;
	*y = by;
	return 0;
}

/*==========================================
 * Locates the closest, walkable cell with no blocks of a certain type on it
 * Returns true on success and sets x and y to cell found.
 * Otherwise returns false and x and y are not changed.
 * type: Types of block to count
 * flag:
 *		0x1 - only count standing units
 *------------------------------------------*/
bool map_closest_freecell(int16 m, int16 *x, int16 *y, int type, int flag)
{
	uint8 dir = 6;
	int16 tx = *x;
	int16 ty = *y;
	int costrange = 10;
	if (!map_count_oncell(m, tx, ty, type, flag))
		return true; //Current cell is free
	//Algorithm only works up to costrange of 34
	while (costrange <= 34) {
		short dx = dirx[dir];
		short dy = diry[dir];
		//Linear search
		if (dir % 2 == 0 && costrange%10 == 0) {
			tx = *x + dx * (costrange / 10);
			ty = *y + dy * (costrange / 10);
			if (!map_count_oncell(m, tx, ty, type, flag) && map_getcell(m, tx, ty, CELL_CHKPASS)) {
				*x = tx;
				*y = ty;
				return true;
			}
		}
		//Full diagonal search
		else if (dir % 2 == 1 && costrange%14 == 0) {
			tx = *x + dx * (costrange / 14);
			ty = *y + dy * (costrange / 14);
			if (!map_count_oncell(m, tx, ty, type, flag) && map_getcell(m, tx, ty, CELL_CHKPASS)) {
				*x = tx;
				*y = ty;
				return true;
			}
		}
		//One cell diagonal, rest linear (TODO: Find a better algorithm for this)
		else if (dir % 2 == 1 && costrange%10 == 4) {
			tx = *x + dx * ((dir % 4 == 3) ? (costrange / 10) : 1);
			ty = *y + dy * ((dir % 4 == 1) ? (costrange / 10) : 1);
			if (!map_count_oncell(m, tx, ty, type, flag) && map_getcell(m, tx, ty, CELL_CHKPASS)) {
				*x = tx;
				*y = ty;
				return true;
			}
			tx = *x + dx * ((dir % 4 == 1) ? (costrange / 10) : 1);
			ty = *y + dy * ((dir % 4 == 3) ? (costrange / 10) : 1);
			if (!map_count_oncell(m, tx, ty, type, flag) && map_getcell(m, tx, ty, CELL_CHKPASS)) {
				*x = tx;
				*y = ty;
				return true;
			}
		}
		//Get next direction
		if (dir == 5) {
			//Diagonal search complete, repeat with higher cost range
			if (costrange == 14) costrange += 6;
			else if (costrange == 28 || costrange >= 38) costrange += 2;
			else costrange += 4;
			dir = 6;
		}
		else if (dir == 4) {
			//Linear search complete, switch to diagonal directions
			dir = 7;
		}
		else {
			dir = (dir + 2) % 8;
		}
	}
	return false;
}

/*==========================================
 * Add an item in floor to location (m,x,y) and add restriction for those who could pickup later
 * NB : If charids are null their no restriction for pickup
 * @param item_data : item attributes
 * @param amount : items quantity
 * @param m : mapid
 * @param x : x coordinates
 * @param y : y coordinates
 * @param first_charid : 1st player that could loot the item (only charid that could loot for first_get_tick duration)
 * @param second_charid :  2nd player that could loot the item (2nd charid that could loot for second_get_charid duration)
 * @param third_charid : 3rd player that could loot the item (3rd charid that could loot for third_get_charid duration)
 * @param flag: &1 MVP item. &2 do stacking check. &4 bypass droppable check.
 * @param mob_id: Monster ID if dropped by monster
 * @return 0:failure, x:item_gid [MIN_FLOORITEM;MAX_FLOORITEM]==[2;START_ACCOUNT_NUM]
 *------------------------------------------*/
int map_addflooritem(struct item *item_data,int amount,int m,int x,int y,int first_charid,int second_charid,int third_charid,int flags, unsigned short mob_id, bool canShowEffect)
{
	int r;
	struct flooritem_data *fitem=NULL;

	nullpo_ret(item_data);

	if (!(flags&4) && battle_config.item_onfloor && (itemdb_traderight(item_data->nameid) & 1))
		return 0; //can't be dropped

	if(!map_searchrandfreecell(m,&x,&y,flags&2?1:0))
		return 0;
	r=rnd();

	CREATE(fitem, struct flooritem_data, 1);
	fitem->bl.type=BL_ITEM;
	fitem->bl.prev = fitem->bl.next = NULL;
	fitem->bl.m=m;
	fitem->bl.x=x;
	fitem->bl.y=y;
	fitem->bl.id = map_get_new_object_id();
	if(fitem->bl.id==0){
		aFree(fitem);
		return 0;
	}

	fitem->first_get_charid = first_charid;
	fitem->first_get_tick = gettick() + (flags&1 ? battle_config.mvp_item_first_get_time : battle_config.item_first_get_time);
	fitem->second_get_charid = second_charid;
	fitem->second_get_tick = fitem->first_get_tick + (flags&1 ? battle_config.mvp_item_second_get_time : battle_config.item_second_get_time);
	fitem->third_get_charid = third_charid;
	fitem->third_get_tick = fitem->second_get_tick + (flags&1 ? battle_config.mvp_item_third_get_time : battle_config.item_third_get_time);
	fitem->mob_id = mob_id;

	memcpy(&fitem->item_data,item_data,sizeof(*item_data));
	fitem->item_data.amount=amount;
	fitem->subx=(r&3)*3+3;
	fitem->suby=((r>>2)&3)*3+3;
	fitem->cleartimer=add_timer(gettick()+battle_config.flooritem_lifetime,map_clearflooritem_timer,fitem->bl.id,0);

	map_addiddb(&fitem->bl);

	if (map_addblock(&fitem->bl))
		return 0;

	clif_dropflooritem(fitem,canShowEffect);

	return fitem->bl.id;
}

static DBData create_charid2nick(DBKey key, va_list args)
{
	struct charid2nick *p;
	CREATE(p, struct charid2nick, 1);
	return db_ptr2data(p);
}

/// Adds(or replaces) the nick of charid to nick_db and fullfils pending requests.
/// Does nothing if the character is online.
void map_addnickdb(int charid, const char* nick)
{
	struct charid2nick* p;

	if( map_charid2sd(charid) )
		return;// already online

	p = idb_ensure(nick_db, charid, create_charid2nick);
	safestrncpy(p->nick, nick, sizeof(p->nick));

	while( p->requests )
	{
		struct map_session_data* sd;
		struct charid_request* req;
		req = p->requests;
		p->requests = req->next;
		sd = map_charid2sd(req->charid);
		if( sd )
			clif_solved_charname(sd->fd, charid, p->nick);
		aFree(req);
	}
}

/// Removes the nick of charid from nick_db.
/// Sends name to all pending requests on charid.
void map_delnickdb(int charid, const char* name)
{
	struct charid2nick* p;
	DBData data;

	if (!nick_db->remove(nick_db, db_i2key(charid), &data) || (p = db_data2ptr(&data)) == NULL)
		return;

	while( p->requests )
	{
		struct charid_request* req;
		struct map_session_data* sd;
		req = p->requests;
		p->requests = req->next;
		sd = map_charid2sd(req->charid);
		if( sd )
			clif_solved_charname(sd->fd, charid, name);
		aFree(req);
	}
	aFree(p);
}

/// Notifies sd of the nick of charid.
/// Uses the name in the character if online.
/// Uses the name in nick_db if offline.
void map_reqnickdb(struct map_session_data * sd, int charid)
{
	struct charid2nick* p;
	struct charid_request* req;
	struct map_session_data* tsd;

	nullpo_retv(sd);

	tsd = map_charid2sd(charid);
	if( tsd )
	{
		clif_solved_charname(sd->fd, charid, tsd->status.name);
		return;
	}

	p = idb_ensure(nick_db, charid, create_charid2nick);
	if( *p->nick )
	{
		clif_solved_charname(sd->fd, charid, p->nick);
		return;
	}
	// not in cache, request it
	CREATE(req, struct charid_request, 1);
	req->next = p->requests;
	p->requests = req;
	chrif_searchcharid(charid);
}

/*==========================================
 * add bl to id_db
 *------------------------------------------*/
void map_addiddb(struct block_list *bl)
{
	nullpo_retv(bl);

	if( bl->type == BL_PC )
	{
		TBL_PC* sd = (TBL_PC*)bl;
		idb_put(pc_db,sd->bl.id,sd);
		uidb_put(charid_db,sd->status.char_id,sd);
	}
	else if( bl->type == BL_MOB )
	{
		TBL_MOB* md = (TBL_MOB*)bl;
		idb_put(mobid_db,bl->id,bl);

		if( md->state.boss )
			idb_put(bossid_db, bl->id, bl);
	}

	if( bl->type & BL_REGEN )
		idb_put(regen_db, bl->id, bl);

	idb_put(id_db,bl->id,bl);
}

/*==========================================
 * id_db‚©‚çbl‚ðíœ
 *------------------------------------------*/
void map_deliddb(struct block_list *bl)
{
	nullpo_retv(bl);

	if( bl->type == BL_PC )
	{
		TBL_PC* sd = (TBL_PC*)bl;
		idb_remove(pc_db,sd->bl.id);
		idb_remove(charid_db,sd->status.char_id);
	}
	else if( bl->type == BL_MOB )
	{
		idb_remove(mobid_db,bl->id);
		idb_remove(bossid_db,bl->id);
	}

	if( bl->type & BL_REGEN )
		idb_remove(regen_db,bl->id);

	idb_remove(id_db,bl->id);
}

/*==========================================
 * Standard call when a player connection is closed.
 *------------------------------------------*/
int map_quit(struct map_session_data *sd)
{
	int i;

	if (sd->state.keepshop == false) 
	{ // Close vending/buyingstore
		if (sd->state.vending)
			vending_closevending(sd);
		else if (sd->state.buyingstore)
			buyingstore_close(sd);
	}

	if (!sd->state.active) 
	{ //Removing a player that is not active.
		struct auth_node *node = chrif_search(sd->status.account_id);
		if (node && node->char_id == sd->status.char_id &&
			node->state != ST_LOGOUT)
			//Except when logging out, clear the auth-connect data immediately.
			chrif_auth_delete(node->account_id, node->char_id, node->state);
		//Non-active players should not have loaded any data yet (or it was cleared already) so no additional cleanups are needed.
		return 0;
	}

	if (sd->npc_timer_id != INVALID_TIMER) //Cancel the event timer.
		npc_timerevent_quit(sd);

	if (sd->npc_id)
		npc_event_dequeue(sd);

	if (sd->status.clan_id)
		clan_member_left(sd);

	for (i = 0; i < VECTOR_LENGTH(sd->script_queues); i++) {
		struct script_queue *queue = script_queue_get(VECTOR_INDEX(sd->script_queues, i));
		if (queue && queue->event_logout[0] != '\0') {
			npc_event(sd, queue->event_logout, 0);
		}
	}
	/* two times, the npc event above may assign a new one or delete others */
	while (VECTOR_LENGTH(sd->script_queues)) {
		int qid = VECTOR_LAST(sd->script_queues);
		script_queue_remove(qid, sd->status.account_id);
	}

	npc_script_event(sd, NPCE_LOGOUT);

	pc_itemcd_do(sd, false);

	//Unit_free handles clearing the player related data, 
	//map_quit handles extra specific data which is related to quitting normally
	//(changing map-servers invokes unit_free but bypasses map_quit)
	if (sd->sc.count)
	{
		//Status that are not saved...
		status_change_end(&sd->bl, SC_BOSSMAPINFO, INVALID_TIMER);
		status_change_end(&sd->bl, SC_AUTOTRADE, INVALID_TIMER);
		status_change_end(&sd->bl, SC_SPURT, INVALID_TIMER);
		status_change_end(&sd->bl, SC_BERSERK, INVALID_TIMER);
		status_change_end(&sd->bl, SC_TRICKDEAD, INVALID_TIMER);
		status_change_end(&sd->bl, SC_LEADERSHIP, INVALID_TIMER);
		status_change_end(&sd->bl, SC_GLORYWOUNDS, INVALID_TIMER);
		status_change_end(&sd->bl, SC_SOULCOLD, INVALID_TIMER);
		status_change_end(&sd->bl, SC_HAWKEYES, INVALID_TIMER);
		if(sd->sc.data[SC_ENDURE] && sd->sc.data[SC_ENDURE]->val4)
			status_change_end(&sd->bl, SC_ENDURE, INVALID_TIMER); //No need to save infinite endure.
		if (sd->sc.data[SC_PROVOKE] && sd->sc.data[SC_PROVOKE]->timer == INVALID_TIMER)
			status_change_end(&sd->bl, SC_PROVOKE, INVALID_TIMER); //Infinite provoke ends on logout
		status_change_end(&sd->bl, SC_WEIGHT50, INVALID_TIMER);
		status_change_end(&sd->bl, SC_WEIGHT90, INVALID_TIMER);
		status_change_end(&sd->bl, SC_ALL_RIDING, INVALID_TIMER);
		status_change_end(&sd->bl, SC_MILLENNIUMSHIELD, INVALID_TIMER);
		status_change_end(&sd->bl, SC_READING_SB, INVALID_TIMER);
		status_change_end(&sd->bl, SC_OVERHEAT_LIMITPOINT, INVALID_TIMER);
		status_change_end(&sd->bl, SC_RAISINGDRAGON, INVALID_TIMER);
		status_change_end(&sd->bl, SC_OVERHEAT, INVALID_TIMER);
		status_change_end(&sd->bl, SC_SOULCOLLECT, INVALID_TIMER);
		status_change_end(&sd->bl, SC_KYOUGAKU, INVALID_TIMER);//Not official, but needed since logging back in crashes the client. Will fix later. [Rytech]
		status_change_end(&sd->bl, SC_SPRITEMABLE, INVALID_TIMER);
		status_change_end(&sd->bl, SC_SOULATTACK, INVALID_TIMER);
		status_change_end(&sd->bl, SC_EL_COST, INVALID_TIMER);
		status_change_end(&sd->bl, SC_CIRCLE_OF_FIRE_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_FIRE_CLOAK_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_WATER_SCREEN_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_WATER_DROP_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_WIND_STEP_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_WIND_CURTAIN_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_SOLID_SKIN_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_STONE_SHIELD_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_PYROTECHNIC_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_HEATER_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_TROPIC_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_AQUAPLAY_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_COOLER_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_CHILLY_AIR_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_GUST_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_BLAST_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_WILD_STORM_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_PETROLOGY_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_CURSED_SOIL_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_UPHEAVAL_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_TIDAL_WEAPON_OPTION, INVALID_TIMER);
		status_change_end(&sd->bl, SC_READYSTORM, INVALID_TIMER);
		status_change_end(&sd->bl, SC_READYDOWN, INVALID_TIMER);
		status_change_end(&sd->bl, SC_READYTURN, INVALID_TIMER);
		status_change_end(&sd->bl, SC_READYCOUNTER, INVALID_TIMER);
		status_change_end(&sd->bl, SC_DODGE, INVALID_TIMER);
		status_change_end(&sd->bl, SC_CBC, INVALID_TIMER);
		status_change_end(&sd->bl, SC_EQC, INVALID_TIMER);
		if (battle_config.debuff_on_logout&1) 
		{
			status_change_end(&sd->bl, SC_ORCISH, INVALID_TIMER);
			status_change_end(&sd->bl, SC_STRIPWEAPON, INVALID_TIMER);
			status_change_end(&sd->bl, SC_STRIPARMOR, INVALID_TIMER);
			status_change_end(&sd->bl, SC_STRIPSHIELD, INVALID_TIMER);
			status_change_end(&sd->bl, SC_STRIPHELM, INVALID_TIMER);
			status_change_end(&sd->bl,SC__STRIPACCESSARY,INVALID_TIMER);
			status_change_end(&sd->bl, SC_EXTREMITYFIST, INVALID_TIMER);
			status_change_end(&sd->bl, SC_EXPLOSIONSPIRITS, INVALID_TIMER);
			if(sd->sc.data[SC_REGENERATION] && sd->sc.data[SC_REGENERATION]->val4)
				status_change_end(&sd->bl, SC_REGENERATION, INVALID_TIMER);
			//TO-DO Probably there are way more NPC_type negative status that are removed
			status_change_end(&sd->bl, SC_CHANGEUNDEAD, INVALID_TIMER);
			// Both these statuses are removed on logout. [L0ne_W0lf]
			status_change_end(&sd->bl, SC_SLOWCAST, INVALID_TIMER);
			status_change_end(&sd->bl, SC_CRITICALWOUND, INVALID_TIMER);
		}
		if (battle_config.debuff_on_logout&2)
		{
			status_change_end(&sd->bl, SC_MAXIMIZEPOWER, INVALID_TIMER);
			status_change_end(&sd->bl, SC_MAXOVERTHRUST, INVALID_TIMER);
			status_change_end(&sd->bl, SC_STEELBODY, INVALID_TIMER);
			status_change_end(&sd->bl, SC_PRESERVE, INVALID_TIMER);
			status_change_end(&sd->bl, SC_KAAHI, INVALID_TIMER);
			status_change_end(&sd->bl, SC_SPIRIT, INVALID_TIMER);
			status_change_end(&sd->bl, SC__REPRODUCE, INVALID_TIMER);
			status_change_end(&sd->bl, SC__INVISIBILITY, INVALID_TIMER);
			status_change_end(&sd->bl, SC_SOULGOLEM, INVALID_TIMER);
			status_change_end(&sd->bl, SC_SOULSHADOW, INVALID_TIMER);
			status_change_end(&sd->bl, SC_SOULFALCON, INVALID_TIMER);
			status_change_end(&sd->bl, SC_SOULFAIRY, INVALID_TIMER);
			status_change_end(&sd->bl, SC_SIGHTBLASTER, INVALID_TIMER);
		}
	}

	for (i = 0; i < EQI_MAX; i++) {
		if (sd->equip_index[i] >= 0)
			if (pc_isequip(sd, sd->equip_index[i]) != ITEM_EQUIP_ACK_OK)
				pc_unequipitem(sd, sd->equip_index[i], ITEM_EQUIP_ACK_FAIL);
	}
	
	// Return loot to owner
	if( sd->pd ) pet_lootitem_drop(sd->pd, sd);

	unit_remove_map_pc(sd,CLR_TELEPORT);
	
	if( map[sd->bl.m].instance_id )
	{ // Avoid map conflicts and warnings on next login
		int m;
		struct point *pt;
		if( map[sd->bl.m].save.map )
			pt = &map[sd->bl.m].save;
		else
			pt = &sd->status.save_point;

		if( (m=map_mapindex2mapid(pt->map)) >= 0 )
		{
			sd->bl.m = m;
			sd->bl.x = pt->x;
			sd->bl.y = pt->y;
			sd->mapindex = pt->map;
		}
	}	

	// Cell PVP [Napster]
	if( sd->state.pvp && map_getcell( sd->bl.m, sd->bl.x, sd->bl.y, CELL_CHKPVP ) )
		map_pvp_area(sd, 0);

	if (sd->state.vending)
		idb_remove(vending_getdb(), sd->status.char_id);

	if (sd->state.buyingstore)
		idb_remove(buyingstore_getdb(), sd->status.char_id);

	pc_damage_log_clear(sd, 0);
	party_booking_delete(sd); // Party Booking [Spiria]
	pc_makesavestatus(sd);
	pc_clean_skilltree(sd);
	chrif_save(sd, CSAVE_QUIT | CSAVE_INVENTORY | CSAVE_CART);
	unit_free_pc(sd);
	return 0;
}

/*==========================================
 * id”Ô?‚ÌPC‚ð’T‚·B‹‚È‚¯‚ê‚ÎNULL
 *------------------------------------------*/
struct map_session_data * map_id2sd(int id)
{
	if (id <= 0) return NULL;
	return (struct map_session_data*)idb_get(pc_db,id);
}

struct mob_data * map_id2md(int id)
{
	if (id <= 0) return NULL;
	return (struct mob_data*)idb_get(mobid_db,id);
}

struct npc_data * map_id2nd(int id)
{// just a id2bl lookup because there's no npc_db
	struct block_list* bl = map_id2bl(id);

	return BL_CAST(BL_NPC, bl);
}

struct homun_data* map_id2hd(int id)
{
	struct block_list* bl = map_id2bl(id);

	return BL_CAST(BL_HOM, bl);
}

struct mercenary_data* map_id2mc(int id)
{
	struct block_list* bl = map_id2bl(id);

	return BL_CAST(BL_MER, bl);
}

struct pet_data* map_id2pd(int id){
	struct block_list* bl = map_id2bl(id);
	return BL_CAST(BL_PET, bl);
}

struct elemental_data* map_id2ed(int id) {
	struct block_list* bl = map_id2bl(id);
	return BL_CAST(BL_ELEM, bl);
}

struct chat_data* map_id2cd(int id)
{
	struct block_list* bl = map_id2bl(id);

	return BL_CAST(BL_CHAT, bl);
}

/// Returns the nick of the target charid or NULL if unknown (requests the nick to the char server).
const char* map_charid2nick(int charid)
{
	struct charid2nick *p;
	struct map_session_data* sd;

	sd = map_charid2sd(charid);
	if( sd )
		return sd->status.name;// character is online, return it's name

	p = idb_ensure(nick_db, charid, create_charid2nick);
	if( *p->nick )
		return p->nick;// name in nick_db

	chrif_searchcharid(charid);// request the name
	return NULL;
}

/// Returns the struct map_session_data of the charid or NULL if the char is not online.
struct map_session_data* map_charid2sd(int charid)
{
	return (struct map_session_data*)idb_get(charid_db, charid);
}

/*==========================================
 * Search session data from a nick name
 * (without sensitive case if necessary)
 * return map_session_data pointer or NULL
 *------------------------------------------*/
struct map_session_data * map_nick2sd(const char *nick)
{
	struct map_session_data* sd;
	struct map_session_data* found_sd;
	struct s_mapiterator* iter;
	size_t nicklen;
	int qty = 0;

	if( nick == NULL )
		return NULL;

	nicklen = strlen(nick);
	iter = mapit_getallusers();

	found_sd = NULL;
	for( sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); sd = (TBL_PC*)mapit_next(iter) )
	{
		if( battle_config.partial_name_scan )
		{// partial name search
			if( strnicmp(sd->status.name, nick, nicklen) == 0 )
			{
				found_sd = sd;

				if( strcmp(sd->status.name, nick) == 0 )
				{// Perfect Match
					qty = 1;
					break;
				}

				qty++;
			}
		}
		else if( strcasecmp(sd->status.name, nick) == 0 )
		{// exact search only
			found_sd = sd;
			break;
		}
	}
	mapit_free(iter);

	if( battle_config.partial_name_scan && qty != 1 )
		found_sd = NULL;

	return found_sd;
}

/*==========================================
 * id”Ô?‚Ì•¨‚ð’T‚·
 * ˆêŽObject‚Ìê‡‚Í”z—ñ‚ðˆø‚­‚Ì‚Ý
 *------------------------------------------*/
struct block_list * map_id2bl(int id)
{
	return (struct block_list*)idb_get(id_db,id);
}

/**
 * Same as map_id2bl except it only checks for its existence
 **/
bool map_blid_exists(int id) {
	return (idb_exists(id_db, id));
}

/*==========================================
 * Convex Mirror
 *------------------------------------------*/
struct mob_data * map_getmob_boss(int m)
{
	DBIterator* iter;
	struct mob_data *md = NULL;
	bool found = false;

	iter = db_iterator(bossid_db);
	for( md = (struct mob_data*)dbi_first(iter); dbi_exists(iter); md = (struct mob_data*)dbi_next(iter) )
	{
		if( md->bl.m == m )
		{
			found = true;		
			break;
		}
	}
	dbi_destroy(iter);

	return (found)? md : NULL;
}

struct mob_data * map_id2boss(int id)
{
	if (id <= 0) return NULL;
	return (struct mob_data*)idb_get(bossid_db,id);
}

/// Applies func to all the players in the db.
/// Stops iterating if func returns -1.
void map_foreachpc(int (*func)(struct map_session_data* sd, va_list args), ...)
{
	DBIterator* iter;
	struct map_session_data* sd;

	iter = db_iterator(pc_db);
	for (sd = dbi_first(iter); dbi_exists(iter); sd = dbi_next(iter))
	{
		va_list args;
		int ret;

		va_start(args, func);
		ret = func(sd, args);
		va_end(args);
		if( ret == -1 )
			break;// stop iterating
	}
	dbi_destroy(iter);
}

/// Applies func to all the mobs in the db.
/// Stops iterating if func returns -1.
void map_foreachmob(int (*func)(struct mob_data* md, va_list args), ...)
{
	DBIterator* iter;
	struct mob_data* md;

	iter = db_iterator(mobid_db);
	for( md = (struct mob_data*)dbi_first(iter); dbi_exists(iter); md = (struct mob_data*)dbi_next(iter) )
	{
		va_list args;
		int ret;

		va_start(args, func);
		ret = func(md, args);
		va_end(args);
		if( ret == -1 )
			break;// stop iterating
	}
	dbi_destroy(iter);
}

/// Applies func to all the npcs in the db.
/// Stops iterating if func returns -1.
void map_foreachnpc(int (*func)(struct npc_data* nd, va_list args), ...)
{
	DBIterator* iter;
	struct block_list* bl;

	iter = db_iterator(id_db);
	for( bl = (struct block_list*)dbi_first(iter); dbi_exists(iter); bl = (struct block_list*)dbi_next(iter) )
	{
		if( bl->type == BL_NPC )
		{
			struct npc_data* nd = (struct npc_data*)bl;
			va_list args;
			int ret;

			va_start(args, func);
			ret = func(nd, args);
			va_end(args);
			if( ret == -1 )
				break;// stop iterating
		}
	}
	dbi_destroy(iter);
}

/// Applies func to everything in the db.
/// Stops iteratin gif func returns -1.
void map_foreachregen(int (*func)(struct block_list* bl, va_list args), ...)
{
	DBIterator* iter;
	struct block_list* bl;

	iter = db_iterator(regen_db);
	for( bl = (struct block_list*)dbi_first(iter); dbi_exists(iter); bl = (struct block_list*)dbi_next(iter) )
	{
		va_list args;
		int ret;

		va_start(args, func);
		ret = func(bl, args);
		va_end(args);
		if( ret == -1 )
			break;// stop iterating
	}
	dbi_destroy(iter);
}

/// Applies func to everything in the db.
/// Stops iterating if func returns -1.
void map_foreachiddb(int (*func)(struct block_list* bl, va_list args), ...)
{
	DBIterator* iter;
	struct block_list* bl;

	iter = db_iterator(id_db);
	for( bl = (struct block_list*)dbi_first(iter); dbi_exists(iter); bl = (struct block_list*)dbi_next(iter) )
	{
		va_list args;
		int ret;

		va_start(args, func);
		ret = func(bl, args);
		va_end(args);
		if( ret == -1 )
			break;// stop iterating
	}
	dbi_destroy(iter);
}

/**
* Returns the equivalent bitmask to the given race ID.
*
* @param race A race identifier (@see enum Race)
*
* @return The equivalent race bitmask.
*/
uint32 map_race_id2mask(int race)
{
	if (race >= RC_FORMLESS && race < RC_MAX)
		return 1 << race;

	if (race == RC_ALL)
		return RCMASK_ALL;

	ShowWarning("map_race_id2mask: Invalid race: %d\n", race);

	return RCMASK_NONE;
}

/// Iterator.
/// Can filter by bl type.
struct s_mapiterator
{
	enum e_mapitflags flags;// flags for special behaviour
	enum bl_type types;// what bl types to return
	DBIterator* dbi;// database iterator
};

/// Returns true if the block_list matches the description in the iterator.
///
/// @param _mapit_ Iterator
/// @param _bl_ block_list
/// @return true if it matches
#define MAPIT_MATCHES(_mapit_,_bl_) \
	( \
		( (_bl_)->type & (_mapit_)->types /* type matches */ ) \
	)

/// Allocates a new iterator.
/// Returns the new iterator.
/// types can represent several BL's as a bit field.
/// TODO should this be expanded to allow filtering of map/guild/party/chat/cell/area/...?
///
/// @param flags Flags of the iterator
/// @param type Target types
/// @return Iterator
struct s_mapiterator* mapit_alloc(enum e_mapitflags flags, enum bl_type types)
{
	struct s_mapiterator* mapit;

	CREATE(mapit, struct s_mapiterator, 1);
	mapit->flags = flags;
	mapit->types = types;
	if( types == BL_PC )       mapit->dbi = db_iterator(pc_db);
	else if( types == BL_MOB ) mapit->dbi = db_iterator(mobid_db);
	else                       mapit->dbi = db_iterator(id_db);
	return mapit;
}

/// Frees the iterator.
///
/// @param mapit Iterator
void mapit_free(struct s_mapiterator* mapit)
{
	nullpo_retv(mapit);

	dbi_destroy(mapit->dbi);
	aFree(mapit);
}

/// Returns the first block_list that matches the description.
/// Returns NULL if not found.
///
/// @param mapit Iterator
/// @return first block_list or NULL
struct block_list* mapit_first(struct s_mapiterator* mapit)
{
	struct block_list* bl;

	nullpo_retr(NULL,mapit);

	for( bl = (struct block_list*)dbi_first(mapit->dbi); bl != NULL; bl = (struct block_list*)dbi_next(mapit->dbi) )
	{
		if( MAPIT_MATCHES(mapit,bl) )
			break;// found match
	}
	return bl;
}

/// Returns the last block_list that matches the description.
/// Returns NULL if not found.
///
/// @param mapit Iterator
/// @return last block_list or NULL
struct block_list* mapit_last(struct s_mapiterator* mapit)
{
	struct block_list* bl;

	nullpo_retr(NULL,mapit);

	for( bl = (struct block_list*)dbi_last(mapit->dbi); bl != NULL; bl = (struct block_list*)dbi_prev(mapit->dbi) )
	{
		if( MAPIT_MATCHES(mapit,bl) )
			break;// found match
	}
	return bl;
}

/// Returns the next block_list that matches the description.
/// Returns NULL if not found.
///
/// @param mapit Iterator
/// @return next block_list or NULL
struct block_list* mapit_next(struct s_mapiterator* mapit)
{
	struct block_list* bl;

	nullpo_retr(NULL,mapit);

	for( ; ; )
	{
		bl = (struct block_list*)dbi_next(mapit->dbi);
		if( bl == NULL )
			break;// end
		if( MAPIT_MATCHES(mapit,bl) )
			break;// found a match
		// try next
	}
	return bl;
}

/// Returns the previous block_list that matches the description.
/// Returns NULL if not found.
///
/// @param mapit Iterator
/// @return previous block_list or NULL
struct block_list* mapit_prev(struct s_mapiterator* mapit)
{
	struct block_list* bl;

	nullpo_retr(NULL,mapit);

	for( ; ; )
	{
		bl = (struct block_list*)dbi_prev(mapit->dbi);
		if( bl == NULL )
			break;// end
		if( MAPIT_MATCHES(mapit,bl) )
			break;// found a match
		// try prev
	}
	return bl;
}

/// Returns true if the current block_list exists in the database.
///
/// @param mapit Iterator
/// @return true if it exists
bool mapit_exists(struct s_mapiterator* mapit)
{
	nullpo_retr(false,mapit);

	return dbi_exists(mapit->dbi);
}

/*==========================================
 * map.npc‚Ö’Ç‰Á (warp“™‚Ì—ÌˆæŽ‚¿‚Ì‚Ý)
 *------------------------------------------*/
bool map_addnpc(int m,struct npc_data *nd)
{
	nullpo_ret(nd);

	if( m < 0 || m >= map_num )
		return false;

	if( map[m].npc_num == MAX_NPC_PER_MAP )
	{
		ShowWarning("too many NPCs in one map %s\n",map[m].name);
		return false;
	}

	map[m].npc[map[m].npc_num]=nd;
	map[m].npc_num++;
	idb_put(id_db,nd->bl.id,nd);
	return true;
}

/*=========================================
 * Dynamic Mobs [Wizputer]
 *-----------------------------------------*/
// Stores the spawn data entry in the mob list.
// Returns the index of successful, or -1 if the list was full.
int map_addmobtolist(unsigned short m, struct spawn_data *spawn)
{
	size_t i;
	ARR_FIND( 0, MAX_MOB_LIST_PER_MAP, i, map[m].moblist[i] == NULL );
	if( i < MAX_MOB_LIST_PER_MAP )
	{
		map[m].moblist[i] = spawn;
		return i;
	}
	return -1;
}

void map_spawnmobs(int m)
{
	int i, k=0;
	if (map[m].mob_delete_timer != INVALID_TIMER)
	{	//Mobs have not been removed yet [Skotlex]
		delete_timer(map[m].mob_delete_timer, map_removemobs_timer);
		map[m].mob_delete_timer = INVALID_TIMER;
		return;
	}
	for(i=0; i<MAX_MOB_LIST_PER_MAP; i++)
		if(map[m].moblist[i]!=NULL)
		{
			k+=map[m].moblist[i]->num;
			npc_parse_mob2(map[m].moblist[i]);
		}

	if (battle_config.etc_log && k > 0)
	{
		ShowStatus("Map %s: Spawned '"CL_WHITE"%d"CL_RESET"' mobs.\n",map[m].name, k);
	}
}

int map_removemobs_sub(struct block_list *bl, va_list ap)
{
	struct mob_data *md = (struct mob_data *)bl;
	nullpo_ret(md);

	//When not to remove mob:
	// doesn't respawn and is not a slave
	if( !md->spawn && !md->master_id )
		return 0;
	// respawn data is not in cache
	if( md->spawn && !md->spawn->state.dynamic )
		return 0;
	// hasn't spawned yet
	if( md->spawn_timer != INVALID_TIMER )
		return 0;
	// is damaged and mob_remove_damaged is off
	if( !battle_config.mob_remove_damaged && md->status.hp < md->status.max_hp )
		return 0;
	// is a mvp
	if( md->db->mexp > 0 )
		return 0;
	
	unit_free(&md->bl,CLR_OUTSIGHT);

	return 1;
}

int map_removemobs_timer(int tid, int64 tick, int id, intptr_t data)
{
	int count;
	const int m = id;

	if (m < 0 || m >= MAX_MAP_PER_SERVER)
	{	//Incorrect map id!
		ShowError("map_removemobs_timer error: timer %d points to invalid map %d\n",tid, m);
		return 0;
	}
	if (map[m].mob_delete_timer != tid)
	{	//Incorrect timer call!
		ShowError("map_removemobs_timer mismatch: %d != %d (map %s)\n",map[m].mob_delete_timer, tid, map[m].name);
		return 0;
	}
	map[m].mob_delete_timer = INVALID_TIMER;
	if (map[m].users > 0) //Map not empty!
		return 1;

	count = map_foreachinmap(map_removemobs_sub, m, BL_MOB);

	if (battle_config.etc_log && count > 0)
		ShowStatus("Map %s: Removed '"CL_WHITE"%d"CL_RESET"' mobs.\n",map[m].name, count);
	
	return 1;
}

void map_removemobs(int m)
{
	if (map[m].mob_delete_timer != INVALID_TIMER) // should never happen
		return; //Mobs are already scheduled for removal

	map[m].mob_delete_timer = add_timer(gettick()+battle_config.mob_remove_delay, map_removemobs_timer, m, 0);
}

/*==========================================
 * Hookup, get map_id from map_name
 *------------------------------------------*/
int map_mapname2mapid(const char* name)
{
	unsigned short map_index;
	map_index = mapindex_name2id(name);
	if (!map_index)
		return -1;
	return map_mapindex2mapid(map_index);
}

/*==========================================
 * Returns the map of the given mapindex. [Skotlex]
 *------------------------------------------*/
int map_mapindex2mapid(unsigned short mapindex)
{
	struct map_data *md=NULL;
	
	if (!mapindex)
		return -1;
	
	md = (struct map_data*)uidb_get(map_db,(unsigned int)mapindex);
	if(md==NULL || md->cell==NULL)
		return -1;
	return md->m;
}

/*==========================================
 * ‘¼ŽImap–¼‚©‚çip,port?Š·
 *------------------------------------------*/
int map_mapname2ipport(unsigned short name, uint32* ip, uint16* port)
{
	struct map_data_other_server *mdos;

	mdos = (struct map_data_other_server*)uidb_get(map_db,(unsigned int)name);
	if(mdos==NULL || mdos->cell) //If gat isn't null, this is a local map.
		return -1;
	*ip=mdos->ip;
	*port=mdos->port;
	return 0;
}

/*==========================================
 * Checks if both dirs point in the same direction.
 *------------------------------------------*/
int map_check_dir(int s_dir,int t_dir)
{
	if(s_dir == t_dir)
		return 0;
	switch(s_dir) {
		case 0: if(t_dir == 7 || t_dir == 1 || t_dir == 0) return 0; break;
		case 1: if(t_dir == 0 || t_dir == 2 || t_dir == 1) return 0; break;
		case 2: if(t_dir == 1 || t_dir == 3 || t_dir == 2) return 0; break;
		case 3: if(t_dir == 2 || t_dir == 4 || t_dir == 3) return 0; break;
		case 4: if(t_dir == 3 || t_dir == 5 || t_dir == 4) return 0; break;
		case 5: if(t_dir == 4 || t_dir == 6 || t_dir == 5) return 0; break;
		case 6: if(t_dir == 5 || t_dir == 7 || t_dir == 6) return 0; break;
		case 7: if(t_dir == 6 || t_dir == 0 || t_dir == 7) return 0; break;
	}
	return 1;
}

/*==========================================
 * Returns the direction of the given cell, relative to 'src'
 *------------------------------------------*/
uint8 map_calc_dir(struct block_list *src, int16 x, int16 y)
{
	unsigned char dir = 0;
	
	nullpo_ret(src);
	
	dir = map_calc_dir_xy(src->x, src->y, x, y, unit_getdir(src));

	return dir;
}

/*==========================================
 * Returns the direction of the given cell, relative to source cell
 * Use this if you don't have a block list available to check against
 *------------------------------------------*/
uint8 map_calc_dir_xy(int16 srcx, int16 srcy, int16 x, int16 y, uint8 srcdir) {
	uint8 dir = 0;
	int dx, dy;

	dx = x - srcx;
	dy = y - srcy;
	if( dx == 0 && dy == 0 )
	{	// both are standing on the same spot
		// aegis-style, makes knockback default to the left
		// athena-style, makes knockback default to behind 'src'
		dir = (battle_config.knockback_left ? 6 : srcdir);
	}
	else if( dx >= 0 && dy >=0 )
	{	// upper-right
		if (dx >= dy * 3)      dir = 6;	// right
		else if (dx * 3 < dy)  dir = 0;	// up
		else                  dir = 7;	// up-right
	}
	else if( dx >= 0 && dy <= 0 )
	{	// lower-right
		if (dx >= -dy * 3)     dir = 6;	// right
		else if (dx * 3 < -dy) dir = 4;	// down
		else                  dir = 5;	// down-right
	}
	else if( dx <= 0 && dy <= 0 )
	{	// lower-left
		if (dx * 3 >= dy)      dir = 4;	// down
		else if (dx < dy * 3)  dir = 2;	// left
		else                  dir = 3;	// down-left
	}
	else
	{	// upper-left
		if (-dx * 3 <= dy)     dir = 0;	// up
		else if (-dx > dy * 3) dir = 2;	// left
		else                  dir = 1;	// up-left
	
	}
	return dir;
}

/*==========================================
 * Randomizes target cell x,y to a random walkable cell that 
 * has the same distance from object as given coordinates do. [Skotlex]
 *------------------------------------------*/
int map_random_dir(struct block_list *bl, short *x, short *y)
{
	short xi = *x-bl->x;
	short yi = *y-bl->y;
	short i=0;
	int dist2 = xi*xi + yi*yi;
	short dist = (short)sqrt((float)dist2);
	
	if (dist < 1) dist =1;
	
	do {
		short j = 1 + 2 * (rnd() % 4); //Pick a random diagonal direction
		short segment = 1 + (rnd() % dist); //Pick a random interval from the whole vector in that direction
		xi = bl->x + segment*dirx[j];
		segment = (short)sqrt((float)(dist2 - segment*segment)); //The complement of the previously picked segment
		yi = bl->y + segment*diry[j];
	} while (
		(map_getcell(bl->m,xi,yi,CELL_CHKNOPASS) || !path_search(NULL,bl->m,bl->x,bl->y,xi,yi,1,CELL_CHKNOREACH))
		&& (++i)<100 );
	
	if (i < 100) {
		*x = xi;
		*y = yi;
		return 1;
	}
	return 0;
}

// gat system
inline static struct mapcell map_gat2cell(int gat)
{
	struct mapcell cell;

	memset(&cell, 0, sizeof(struct mapcell));

	switch( gat )
	{
		case 0: cell.walkable = 1; cell.shootable = 1; cell.water = 0; break; // walkable ground
		case 1: cell.walkable = 0; cell.shootable = 0; cell.water = 0; break; // non-walkable ground
		case 2: cell.walkable = 1; cell.shootable = 1; cell.water = 0; break; // ???
		case 3: cell.walkable = 1; cell.shootable = 1; cell.water = 1; break; // walkable water
		case 4: cell.walkable = 1; cell.shootable = 1; cell.water = 0; break; // ???
		case 5: cell.walkable = 0; cell.shootable = 1; cell.water = 0; break; // gap (snipable)
		case 6: cell.walkable = 1; cell.shootable = 1; cell.water = 0; break; // ???
		default:
			ShowWarning("map_gat2cell: unrecognized gat type '%d'\n", gat);
			break;
	}

	return cell;
}

static int map_cell2gat(struct mapcell cell)
{
	if( cell.walkable == 1 && cell.shootable == 1 && cell.water == 0 ) return 0;
	if( cell.walkable == 0 && cell.shootable == 0 && cell.water == 0 ) return 1;
	if( cell.walkable == 1 && cell.shootable == 1 && cell.water == 1 ) return 3;
	if( cell.walkable == 0 && cell.shootable == 1 && cell.water == 0 ) return 5;

	ShowWarning("map_cell2gat: cell has no matching gat type\n");
	return 1; // default to 'wall'
}

/*==========================================
 * (m,x,y)‚Ìó‘Ô‚ð’²‚×‚é
 *------------------------------------------*/
int map_getcell(int m,int x,int y,cell_chk cellchk)
{
	return (m < 0 || m >= MAX_MAP_PER_SERVER) ? 0 : map_getcellp(&map[m],x,y,cellchk);
}

int map_getcellp(struct map_data* m,int x,int y,cell_chk cellchk)
{
	struct mapcell cell;

	nullpo_ret(m);

	//NOTE: this intentionally overrides the last row and column
	if(x<0 || x>=m->xs-1 || y<0 || y>=m->ys-1)
		return( cellchk == CELL_CHKNOPASS );

	cell = m->cell[x + y*m->xs];

	switch(cellchk)
	{
		// gat type retrieval
		case CELL_GETTYPE:
			return map_cell2gat(cell);

		// base gat type checks
		case CELL_CHKWALL:
			return (!cell.walkable && !cell.shootable);
		case CELL_CHKWATER:
			return (cell.water);
		case CELL_CHKCLIFF:
			return (!cell.walkable && cell.shootable);

		// base cell type checks
		case CELL_CHKNPC:
			return (cell.npc);
		case CELL_CHKBASILICA:
			return (cell.basilica);
		case CELL_CHKLANDPROTECTOR:
			return (cell.landprotector);
		case CELL_CHKNOVENDING:
			return (cell.novending);
		case CELL_CHKNOCHAT:
			return (cell.nochat);
		case CELL_CHKPVP:	// Cell PVP [Napster]
			return (cell.pvp);
		case CELL_CHKMAELSTROM:
			return (cell.maelstrom);
		case CELL_CHKICEWALL:
			return (cell.icewall);

		// special checks
		case CELL_CHKPASS:
#ifdef CELL_NOSTACK
			if (cell.cell_bl >= battle_config.custom_cell_stack_limit) return 0;
#endif
		case CELL_CHKREACH:
			return (cell.walkable);

		case CELL_CHKNOPASS:
#ifdef CELL_NOSTACK
			if (cell.cell_bl >= battle_config.custom_cell_stack_limit) return 1;
#endif
		case CELL_CHKNOREACH:
			return (!cell.walkable);

		case CELL_CHKSTACK:
#ifdef CELL_NOSTACK
			return (cell.cell_bl >= battle_config.custom_cell_stack_limit);
#else
			return 0;
#endif

		default:
			return 0;
	}
}

/*==========================================
 * Change the type/flags of a map cell
 * 'cell' - which flag to modify
 * 'flag' - true = on, false = off
 *------------------------------------------*/
void map_setcell(int m, int x, int y, cell_t cell, bool flag)
{
	int j;

	if( m < 0 || m >= map_num || x < 0 || x >= map[m].xs || y < 0 || y >= map[m].ys )
		return;

	j = x + y*map[m].xs;

	switch( cell ) {
		case CELL_WALKABLE:      map[m].cell[j].walkable = flag;      break;
		case CELL_SHOOTABLE:     map[m].cell[j].shootable = flag;     break;
		case CELL_WATER:         map[m].cell[j].water = flag;         break;

		case CELL_NPC:				map[m].cell[j].npc = flag;				break;
		case CELL_BASILICA:			map[m].cell[j].basilica = flag;			break;
		case CELL_LANDPROTECTOR:	map[m].cell[j].landprotector = flag;	break;
		case CELL_NOVENDING:		map[m].cell[j].novending = flag;		break;
		case CELL_NOCHAT:			map[m].cell[j].nochat = flag;			break;
		case CELL_PVP:				map[m].cell[j].pvp = flag;				break; // Cell PVP [Napster]
		case CELL_MAELSTROM:		map[m].cell[j].maelstrom = flag;		break;
		case CELL_ICEWALL:			map[m].cell[j].icewall = flag;			break;
		default:
			ShowWarning("map_setcell: invalid cell type '%d'\n", (int)cell);
			break;
	}
}

void map_setgatcell(int m, int x, int y, int gat)
{
	int j;
	struct mapcell cell;

	if( m < 0 || m >= map_num || x < 0 || x >= map[m].xs || y < 0 || y >= map[m].ys )
		return;

	j = x + y*map[m].xs;

	cell = map_gat2cell(gat);
	map[m].cell[j].walkable = cell.walkable;
	map[m].cell[j].shootable = cell.shootable;
	map[m].cell[j].water = cell.water;
}

/*==========================================
 * Invisible Walls
 *------------------------------------------*/
static DBMap* iwall_db;

void map_iwall_nextxy(int x, int y, int dir, int pos, int *x1, int *y1)
{
	if( dir == 0 || dir == 4 )
		*x1 = x; // Keep X
	else if( dir > 0 && dir < 4 )
		*x1 = x - pos; // Going left
	else
		*x1 = x + pos; // Going right

	if( dir == 2 || dir == 6 )
		*y1 = y;
	else if( dir > 2 && dir < 6 )
		*y1 = y - pos;
	else
		*y1 = y + pos;
}

bool map_iwall_set(int m, int x, int y, int size, int dir, bool shootable, const char* wall_name)
{
	struct iwall_data *iwall;
	int i, x1 = 0, y1 = 0;

	if( size < 1 || !wall_name )
		return false;

	if( (iwall = (struct iwall_data *)strdb_get(iwall_db, wall_name)) != NULL )
		return false; // Already Exists

	if( map_getcell(m, x, y, CELL_CHKNOREACH) )
		return false; // Starting cell problem

	CREATE(iwall, struct iwall_data, 1);
	iwall->m = m;
	iwall->x = x;
	iwall->y = y;
	iwall->size = size;
	iwall->dir = dir;
	iwall->shootable = shootable;
	safestrncpy(iwall->wall_name, wall_name, sizeof(iwall->wall_name));

	for( i = 0; i < size; i++ )
	{
		map_iwall_nextxy(x, y, dir, i, &x1, &y1);

		if( map_getcell(m, x1, y1, CELL_CHKNOREACH) )
			break; // Collision

		map_setcell(m, x1, y1, CELL_WALKABLE, false);
		map_setcell(m, x1, y1, CELL_SHOOTABLE, shootable);

		clif_changemapcell(0, m, x1, y1, map_getcell(m, x1, y1, CELL_GETTYPE), ALL_SAMEMAP);
	}

	iwall->size = i;

	strdb_put(iwall_db, iwall->wall_name, iwall);
	map[m].iwall_num++;

	return true;
}

void map_iwall_get(struct map_session_data *sd)
{
	struct iwall_data *iwall;
	DBIterator* iter;
	int x1, y1;
	int i;

	if( map[sd->bl.m].iwall_num < 1 )
		return;

	iter = db_iterator(iwall_db);
	for (iwall = dbi_first(iter); dbi_exists(iter); iwall = dbi_next(iter))
	{
		if( iwall->m != sd->bl.m )
			continue;

		for( i = 0; i < iwall->size; i++ )
		{
			map_iwall_nextxy(iwall->x, iwall->y, iwall->dir, i, &x1, &y1);
			clif_changemapcell(sd->fd, iwall->m, x1, y1, map_getcell(iwall->m, x1, y1, CELL_GETTYPE), SELF);
		}
	}
	dbi_destroy(iter);
}

void map_iwall_remove(const char *wall_name)
{
	struct iwall_data *iwall;
	int i, x1, y1;

	if( (iwall = (struct iwall_data *)strdb_get(iwall_db, wall_name)) == NULL )
		return; // Nothing to do

	for( i = 0; i < iwall->size; i++ )
	{
		map_iwall_nextxy(iwall->x, iwall->y, iwall->dir, i, &x1, &y1);

		map_setcell(iwall->m, x1, y1, CELL_SHOOTABLE, true);
		map_setcell(iwall->m, x1, y1, CELL_WALKABLE, true);

		clif_changemapcell(0, iwall->m, x1, y1, map_getcell(iwall->m, x1, y1, CELL_GETTYPE), ALL_SAMEMAP);
	}

	map[iwall->m].iwall_num--;
	strdb_remove(iwall_db, iwall->wall_name);
}

static DBData create_map_data_other_server(DBKey key, va_list args)
{
	struct map_data_other_server *mdos;
	unsigned short mapindex = (unsigned short)key.ui;
	mdos=(struct map_data_other_server *)aCalloc(1,sizeof(struct map_data_other_server));
	mdos->index = mapindex;
	memcpy(mdos->name, mapindex_id2name(mapindex), MAP_NAME_LENGTH);
	return db_ptr2data(mdos);
}

/*==========================================
 * ‘¼ŽIŠÇ—‚Ìƒ}ƒbƒv‚ðdb‚É’Ç‰Á
 *------------------------------------------*/
int map_setipport(unsigned short mapindex, uint32 ip, uint16 port)
{
	struct map_data_other_server *mdos;

	mdos=uidb_ensure(map_db,(unsigned int)mapindex, create_map_data_other_server);
	
	if(mdos->cell) //Local map,Do nothing. Give priority to our own local maps over ones from another server. [Skotlex]
		return 0;
	if(ip == clif_getip() && port == clif_getport()) {
		//That's odd, we received info that we are the ones with this map, but... we don't have it.
		ShowFatalError("map_setipport : received info that this map-server SHOULD have map '%s', but it is not loaded.\n",mapindex_id2name(mapindex));
		exit(EXIT_FAILURE);
	}
	mdos->ip   = ip;
	mdos->port = port;
	return 1;
}

/*==========================================
 * ‘¼ŽIŠÇ—‚Ìƒ}ƒbƒv‚ð‘S‚Äíœ
 *------------------------------------------*/
int map_eraseallipport_sub(DBKey key, DBData *data, va_list va)
{
	struct map_data_other_server *mdos = db_data2ptr(data);
	if(mdos->cell == NULL) {
		db_remove(map_db,key);
		aFree(mdos);
	}
	return 0;
}

int map_eraseallipport(void)
{
	map_db->foreach(map_db,map_eraseallipport_sub);
	return 1;
}

/*==========================================
 * ‘¼ŽIŠÇ—‚Ìƒ}ƒbƒv‚ðdb‚©‚çíœ
 *------------------------------------------*/
int map_eraseipport(unsigned short mapindex, uint32 ip, uint16 port)
{
	struct map_data_other_server *mdos;

	mdos = (struct map_data_other_server*)uidb_get(map_db,(unsigned int)mapindex);
	if(!mdos || mdos->cell) //Map either does not exists or is a local map.
		return 0;

	if(mdos->ip==ip && mdos->port == port) {
		uidb_remove(map_db,(unsigned int)mapindex);
		aFree(mdos);
		return 1;
	}
	return 0;
}

/*==========================================
 * [Shinryo]: Init the mapcache
 *------------------------------------------*/
static char *map_init_mapcache(FILE *fp)
{
	size_t size = 0;
	char *buffer;

	// No file open? Return..
	nullpo_ret(fp);

	// Get file size
	size = filesize(fp);

	// Allocate enough space
	CREATE(buffer, char, size);

	// No memory? Return..
	nullpo_ret(buffer);

	// Read file into buffer..
	if(fread(buffer, sizeof(char), size, fp) != size) {
		ShowError("map_init_mapcache: Could not read entire mapcache file\n");
		return NULL;
	}

	return buffer;
}

/*==========================================
 * Map cache reading
 * [Shinryo]: Optimized some behaviour to speed this up
 *==========================================*/
int map_readfromcache(struct map_data *m, char *buffer, char *decode_buffer)
{
	int i;
	struct map_cache_main_header *header = (struct map_cache_main_header *)buffer;
	struct map_cache_map_info *info = NULL;
	char *p = buffer + sizeof(struct map_cache_main_header);

	for(i = 0; i < header->map_count; i++) {
		info = (struct map_cache_map_info *)p;

		if( strcmp(m->name, info->name) == 0 )
			break; // Map found

		// Jump to next entry..
		p += sizeof(struct map_cache_map_info) + info->len;
	}

	if( info && i < header->map_count ) {
		unsigned long size, xy;

		if( info->xs <= 0 || info->ys <= 0 )
			return 0;// Invalid

		m->xs = info->xs;
		m->ys = info->ys;
		size = (unsigned long)info->xs*(unsigned long)info->ys;

		if(size > MAX_MAP_SIZE) {
			ShowWarning("map_readfromcache: %s exceeded MAX_MAP_SIZE of %d\n", info->name, MAX_MAP_SIZE);
			return 0; // Say not found to remove it from list.. [Shinryo]
		}

		// TO-DO: Maybe handle the scenario, if the decoded buffer isn't the same size as expected? [Shinryo]
		decode_zip(decode_buffer, &size, p+sizeof(struct map_cache_map_info), info->len);

		CREATE(m->cell, struct mapcell, size);


		for( xy = 0; xy < size; ++xy )
			m->cell[xy] = map_gat2cell(decode_buffer[xy]);

		return 1;
	}

	return 0; // Not found
}

int map_addmap(char* mapname)
{
	if( strcmpi(mapname,"clear")==0 )
	{
		map_num = 0;
		instance_start = 0;
		return 0;
	}

	if( map_num >= MAX_MAP_PER_SERVER - 1 )
	{
		ShowError("Could not add map '"CL_WHITE"%s"CL_RESET"', the limit of maps has been reached.\n",mapname);
		return 1;
	}

	mapindex_getmapname(mapname, map[map_num].name);
	map_num++;
	return 0;
}

static void map_delmapid(int id)
{
	ShowNotice("Removing map [ %s ] from maplist"CL_CLL"\n",map[id].name);
	memmove(map+id, map+id+1, sizeof(map[0])*(map_num-id-1));
	map_num--;
}

int map_delmap(char* mapname)
{
	int i;
	char map_name[MAP_NAME_LENGTH];

	if (strcmpi(mapname, "all") == 0) {
		map_num = 0;
		return 0;
	}

	mapindex_getmapname(mapname, map_name);
	for(i = 0; i < map_num; i++) {
		if (strcmp(map[i].name, map_name) == 0) {
			map_delmapid(i);
			return 1;
		}
	}
	return 0;
}

/// Initializes map flags and adjusts them depending on configuration.
void map_flags_init(void)
{
	int i;

	for( i = 0; i < map_num; i++ )
	{
		// mapflags
		memset(&map[i].flag, 0, sizeof(map[i].flag));

		// additional mapflag data
		map[i].zone      = 0;  // restricted mapflag zone
		map[i].nocommand = 0;  // nocommand mapflag level
		map[i].bexp      = 100;  // per map base exp multiplicator
		map[i].jexp      = 100;  // per map job exp multiplicator
		memset(map[i].drop_list, 0, sizeof(map[i].drop_list));  // pvp nightmare drop list

		// adjustments
		if( battle_config.pk_mode )
			map[i].flag.pvp = 1; // make all maps pvp for pk_mode [Valaris]

		map_free_questinfo(i);
	}
}

#define NO_WATER 1000000

/*
 * Reads from the .rsw for each map
 * Returns water height (or NO_WATER if file doesn't exist) or other error is encountered.
 * Assumed path for file is data/mapname.rsw
 * Credits to LittleWolf
 */
int map_waterheight(char* mapname)
{
	char fn[256];
 	char *rsw, *found;

	//Look up for the rsw
	sprintf(fn, "data\\%s.rsw", mapname);

	found = grfio_find_file(fn);
	if (found) strcpy(fn, found); // replace with real name
	
	// read & convert fn
	rsw = (char *) grfio_read (fn);
	if (rsw)
	{	
		if (memcmp(rsw, "GRSW", 4) != 0) {
			ShowWarning("Failed to find water level for %s (%s)\n", mapname, fn);
			aFree(rsw);
			return NO_WATER;
		}
		int major_version = rsw[4];
		int minor_version = rsw[5];
		if (major_version > 2 || (major_version == 2 && minor_version > 5)) {
			ShowWarning("Failed to find water level for %s (%s)\n", mapname, fn);
			aFree(rsw);
			return NO_WATER;
		}
		if (major_version < 1 || (major_version == 1 && minor_version <= 4)) {
			ShowWarning("Failed to find water level for %s (%s)\n", mapname, fn);
			aFree(rsw);
			return NO_WATER;
		}
		int offset = 166;
		if (major_version == 2 && minor_version >= 5) {
			offset += 4;
		}
		if (major_version == 2 && minor_version >= 2) {
			offset += 1;
		}

		//Load water height from file
		int wh = (int) *(float*)(rsw + offset);
		aFree(rsw);
		return wh;
	}
	ShowWarning("Failed to find water level for (%s)\n", mapname, fn);
	return NO_WATER;
}

/*==================================
 * .GAT format
 *----------------------------------*/
int map_readgat (struct map_data* m)
{
	char filename[256];
	uint8* gat;
	int water_height;
	size_t xy, off, num_cells;

	sprintf(filename, "data\\%s.gat", m->name);

	gat = (uint8 *) grfio_read(filename);
	if (gat == NULL)
		return 0;

	m->xs = *(int32*)(gat+6);
	m->ys = *(int32*)(gat+10);
	num_cells = m->xs * m->ys;
	CREATE(m->cell, struct mapcell, num_cells);

	water_height = map_waterheight(m->name);

	// Set cell properties
	off = 14;
	for( xy = 0; xy < num_cells; ++xy )
	{
		// read cell data
		float height = *(float*)( gat + off      );
		uint32 type = *(uint32*)( gat + off + 16 );
		off += 20;

		if( type == 0 && water_height != NO_WATER && height > water_height )
			type = 3; // Cell is 0 (walkable) but under water level, set to 3 (walkable water)

		m->cell[xy] = map_gat2cell(type);
	}
	
	aFree(gat);

	return 1;
}

/*======================================
 * Add/Remove map to the map_db
 *--------------------------------------*/
void map_addmap2db(struct map_data *m)
{
	uidb_put(map_db, (unsigned int)m->index, m);
}

void map_removemapdb(struct map_data *m)
{
	uidb_remove(map_db, (unsigned int)m->index);
}

/*======================================
 * Initiate maps loading stage
 *--------------------------------------*/
int map_readallmaps (void)
{
	int i;
	FILE* fp=NULL;
	int maps_removed = 0;
	char *map_cache_buffer = NULL; // Has the uncompressed gat data of all maps, so just one allocation has to be made
	char map_cache_decode_buffer[MAX_MAP_SIZE];

	if( enable_grf )
		ShowStatus("Loading maps (using GRF files)...\n");
	else
	{
		ShowStatus("Loading maps (using %s as map cache)...\n", map_cache_file);
		if( (fp = fopen(map_cache_file, "rb")) == NULL )
		{
			ShowFatalError("Unable to open map cache file "CL_WHITE"%s"CL_RESET"\n", map_cache_file);
			exit(EXIT_FAILURE); //No use launching server if maps can't be read.
		}

		// Init mapcache data.. [Shinryo]
		map_cache_buffer = map_init_mapcache(fp);
		if(!map_cache_buffer) {
			ShowFatalError("Failed to initialize mapcache data (%s)..\n", map_cache_file);
			exit(EXIT_FAILURE);
		}
	}

	// Mapcache reading is now fast enough, the progress info will just slow it down so don't use it anymore [Shinryo]
	if(!enable_grf)
		ShowStatus("Loading maps (%d)..\n", map_num);

	for(i = 0; i < map_num; i++)
	{
		size_t size;

		// show progress
		if(enable_grf)
			ShowStatus("Loading maps [%i/%i]: %s"CL_CLL"\r", i, map_num, map[i].name);

		// try to load the map
		if( !
			(enable_grf?
				 map_readgat(&map[i])
				:map_readfromcache(&map[i], map_cache_buffer, map_cache_decode_buffer))
			) {
			map_delmapid(i);
			maps_removed++;
			i--;
			continue;
		}

		map[i].index = mapindex_name2id(map[i].name);

		if (uidb_get(map_db,(unsigned int)map[i].index) != NULL)
		{
			ShowWarning("Map %s already loaded!"CL_CLL"\n", map[i].name);
			if (map[i].cell) {
				aFree(map[i].cell);
				map[i].cell = NULL;	
			}
			map_delmapid(i);
			maps_removed++;
			i--;
			continue;
		}

		map_addmap2db(&map[i]);

		map[i].m = i;
		memset(map[i].moblist, 0, sizeof(map[i].moblist));	//Initialize moblist [Skotlex]
		map[i].mob_delete_timer = INVALID_TIMER;	//Initialize timer [Skotlex]

		map[i].bxs = (map[i].xs + BLOCK_SIZE - 1) / BLOCK_SIZE;
		map[i].bys = (map[i].ys + BLOCK_SIZE - 1) / BLOCK_SIZE;

		size = map[i].bxs * map[i].bys * sizeof(struct block_list*);
		map[i].block = (struct block_list**)aCalloc(size, 1);
		map[i].block_mob = (struct block_list**)aCalloc(size, 1);
	}

	// intialization and configuration-dependent adjustments of mapflags
	map_flags_init();

	if( !enable_grf ) {
		fclose(fp);

		// The cache isn't needed anymore, so free it.. [Shinryo]
		aFree(map_cache_buffer);
	}

	// finished map loading
	ShowInfo("Successfully loaded '"CL_WHITE"%d"CL_RESET"' maps."CL_CLL"\n",map_num);
	instance_start = map_num; // Next Map Index will be instances

	if (maps_removed)
		ShowNotice("Maps removed: '"CL_WHITE"%d"CL_RESET"'\n",maps_removed);

	return 0;
}

////////////////////////////////////////////////////////////////////////
static int map_ip_set = 0;
static int char_ip_set = 0;

/*==========================================
 * Console Command Parser [Wizputer]
 *------------------------------------------*/
int parse_console(const char* buf)
{
	char type[64];
	char command[64];
	char map[64];
	int x = 0;
	int y = 0;
	int m;
	int n;
	struct map_session_data sd;

	memset(&sd, 0, sizeof(struct map_session_data));
	strcpy(sd.status.name, "console");

	if( ( n = sscanf(buf, "%63[^:]:%63[^:]:%63s %d %d[^\n]", type, command, map, &x, &y) ) < 5 )
	{
		if( ( n = sscanf(buf, "%63[^:]:%63[^\n]", type, command) ) < 2 )
		{
			n = sscanf(buf, "%63[^\n]", type);
		}
	}

	if( n == 5 )
	{
		m = map_mapname2mapid(map);
		if( m < 0 )
		{
			ShowWarning("Console: Unknown map.\n");
			return 0;
		}
		sd.bl.m = m;
		map_search_freecell(&sd.bl, m, &sd.bl.x, &sd.bl.y, -1, -1, 0); 
		if( x > 0 )
			sd.bl.x = x;
		if( y > 0 )
			sd.bl.y = y;
	}
	else
	{
		map[0] = '\0';
		if( n < 2 )
			command[0] = '\0';
		if( n < 1 )
			type[0] = '\0';
	}

	ShowNotice("Type of command: '%s' || Command: '%s' || Map: '%s' Coords: %d %d\n", type, command, map, x, y);

	if( n == 5 && strcmpi("admin",type) == 0 )
	{
		if( !is_atcommand(sd.fd, &sd, command, 0) )
			ShowInfo("Console: not atcommand\n");
	}
	else if( n == 2 && strcmpi("server", type) == 0 )
	{
		if( strcmpi("shutdown", command) == 0 || strcmpi("exit", command) == 0 || strcmpi("quit", command) == 0 )
		{
			runflag = SERVER_STATE_STOP;
		}
	}
	else if (strcmpi("ers_report", type) == 0)
	{
		ers_report();
	}
	else if( strcmpi("help", type) == 0 )
	{
		ShowInfo("To use GM commands:\n");
		ShowInfo("  admin:<gm command>:<map of \"gm\"> <x> <y>\n");
		ShowInfo("You can use any GM command that doesn't require the GM.\n");
		ShowInfo("No using @item or @warp however you can use @charwarp\n");
		ShowInfo("The <map of \"gm\"> <x> <y> is for commands that need coords of the GM\n");
		ShowInfo("IE: @spawn\n");
		ShowInfo("To shutdown the server:\n");
		ShowInfo("  server:shutdown\n");
	}

	return 0;
}

/*==========================================
 * Read map server configuration files (conf/map_athena.conf...)
 *------------------------------------------*/
int map_config_read(char *cfgName)
{
	char line[1024], w1[1024], w2[1024];
	FILE *fp;

	fp = fopen(cfgName,"r");
	if( fp == NULL )
	{
		ShowError("Map configuration file not found at: %s\n", cfgName);
		return 1;
	}

	while( fgets(line, sizeof(line), fp) )
	{
		char* ptr;

		if( line[0] == '/' && line[1] == '/' )
			continue;
		if( (ptr = strstr(line, "//")) != NULL )
			*ptr = '\n'; //Strip comments
		if( sscanf(line, "%1023[^:]: %1023[^\t\r\n]", w1, w2) < 2 )
			continue;

		//Strip trailing spaces
		ptr = w2 + strlen(w2);
		while (--ptr >= w2 && *ptr == ' ');
		ptr++;
		*ptr = '\0';
			
		if(strcmpi(w1,"timestamp_format")==0)
			safestrncpy(timestamp_format, w2, 20);
		else 
		if(strcmpi(w1,"stdout_with_ansisequence")==0)
			stdout_with_ansisequence = config_switch(w2);
		else
		if(strcmpi(w1,"console_silent")==0) {
			ShowInfo("Console Silent Setting: %d\n", atoi(w2));
			msg_silent = atoi(w2);
		} else
		if (strcmpi(w1, "userid")==0)
			chrif_setuserid(w2);
		else
		if (strcmpi(w1, "passwd") == 0)
			chrif_setpasswd(w2);
		else
		if (strcmpi(w1, "char_ip") == 0)
			char_ip_set = chrif_setip(w2);
		else
		if (strcmpi(w1, "char_port") == 0)
			chrif_setport(atoi(w2));
		else
		if (strcmpi(w1, "map_ip") == 0)
			map_ip_set = clif_setip(w2);
		else
		if (strcmpi(w1, "bind_ip") == 0)
			clif_setbindip(w2);
		else
		if (strcmpi(w1, "map_port") == 0) {
			clif_setport(atoi(w2));
			map_port = (atoi(w2));
		} else
		if (strcmpi(w1, "map") == 0)
			map_addmap(w2);
		else
		if (strcmpi(w1, "delmap") == 0)
			map_delmap(w2);
		else
		if (strcmpi(w1, "npc") == 0)
			npc_addsrcfile(w2);
		else
		if (strcmpi(w1, "delnpc") == 0)
			npc_delsrcfile(w2);
		else if (strcmpi(w1, "autosave_time") == 0) {
			autosave_interval = atoi(w2);
			if (autosave_interval < 1) //Revert to default saving.
				autosave_interval = DEFAULT_AUTOSAVE_INTERVAL;
			else
				autosave_interval *= 1000; //Pass from sec to ms
		} else
		if (strcmpi(w1, "minsave_time") == 0) {
			minsave_interval= atoi(w2);
			if (minsave_interval < 1)
				minsave_interval = 1;
		} else
		if (strcmpi(w1, "save_settings") == 0)
			save_settings = atoi(w2);
		else
		if (strcmpi(w1, "motd_txt") == 0)
			strcpy(motd_txt, w2);
		else
		if (strcmpi(w1, "help_txt") == 0)
			strcpy(help_txt, w2);
		else
		if (strcmpi(w1, "help2_txt") == 0)
			strcpy(help2_txt, w2);
		else
		if (strcmpi(w1, "charhelp_txt") == 0)
			strcpy(charhelp_txt, w2);
		else
		if(strcmpi(w1,"map_cache_file") == 0)
			safestrncpy(map_cache_file,w2,255);
		else
		if(strcmpi(w1,"db_path") == 0)
			safestrncpy(db_path,w2,255);
		else
		if (strcmpi(w1, "console") == 0) {
			console = config_switch(w2);
			if (console)
				ShowNotice("Console Commands are enabled.\n");
		} else
		if (strcmpi(w1, "enable_spy") == 0)
			enable_spy = config_switch(w2);
		else
		if (strcmpi(w1, "use_grf") == 0)
			enable_grf = config_switch(w2);
		else
		if (strcmpi(w1, "console_msg_log") == 0)
			console_msg_log = atoi(w2);//[Ind]
		else 
		if (strcmpi(w1, "console_log_filepath") == 0)
			safestrncpy(console_log_filepath, w2, sizeof(console_log_filepath));
		else
		if (strcmpi(w1, "import") == 0)
			map_config_read(w2);
		else
			ShowWarning("Unknown setting '%s' in file %s\n", w1, cfgName);
	}

	fclose(fp);
	return 0;
}

int inter_config_read(char *cfgName)
{
	char line[1024],w1[1024],w2[1024];
	FILE *fp;

	fp=fopen(cfgName,"r");
	if(fp==NULL){
		ShowError("File not found: '%s'.\n",cfgName);
		return 1;
	}
	while(fgets(line, sizeof(line), fp))
	{
		if(line[0] == '/' && line[1] == '/')
			continue;
		if( sscanf(line,"%1023[^:]: %1023[^\r\n]",w1,w2) < 2 )
			continue;

		if(strcmpi(w1, "main_chat_nick")==0)
			safestrncpy(main_chat_nick, w2, sizeof(main_chat_nick));
			
	#ifndef TXT_ONLY
		else if(strcmpi(w1,"item_db_db")==0)
			strcpy(item_db_db,w2);
		else if(strcmpi(w1,"mob_db_db")==0)
			strcpy(mob_db_db,w2);
		else if(strcmpi(w1,"item_db2_db")==0)
			strcpy(item_db2_db,w2);
		else if(strcmpi(w1,"mob_db2_db")==0)
			strcpy(mob_db2_db,w2);
		else if (strcmpi(w1, "market_table") == 0)
			strcpy(market_table, w2); 
		else if( strcmpi(w1, "db_roulette_table") == 0)
			strcpy(db_roulette_table, w2);
		else if (strcmpi(w1, "vending_db") == 0)
			strcpy(vendings_db, w2);
		else if (strcmpi(w1, "vending_items_db") == 0)
			strcpy(vending_items_db, w2);
		else
		//Map Server SQL DB
		if(strcmpi(w1,"map_server_ip")==0)
			strcpy(map_server_ip, w2);
		else
		if(strcmpi(w1,"map_server_port")==0)
			map_server_port=atoi(w2);
		else
		if(strcmpi(w1,"map_server_id")==0)
			strcpy(map_server_id, w2);
		else
		if(strcmpi(w1,"map_server_pw")==0)
			strcpy(map_server_pw, w2);
		else
		if(strcmpi(w1,"map_server_db")==0)
			strcpy(map_server_db, w2);
		else
		if(strcmpi(w1,"default_codepage")==0)
			strcpy(default_codepage, w2);
		else
		if(strcmpi(w1,"use_sql_db")==0) {
			db_use_sqldbs = config_switch(w2);
			ShowStatus ("Using SQL dbs: %s\n",w2);
		} else
		if(strcmpi(w1,"log_db_ip")==0)
			strcpy(log_db_ip, w2);
		else
		if(strcmpi(w1,"log_db_id")==0)
			strcpy(log_db_id, w2);
		else
		if(strcmpi(w1,"log_db_pw")==0)
			strcpy(log_db_pw, w2);
		else
		if(strcmpi(w1,"log_db_port")==0)
			log_db_port = atoi(w2);
		else
		if(strcmpi(w1,"log_db_db")==0)
			strcpy(log_db_db, w2);
	#endif
		else
		if( mapreg_config_read(w1,w2) )
			continue;
		//support the import command, just like any other config
		else
		if(strcmpi(w1,"import")==0)
			inter_config_read(w2);
	}
	fclose(fp);

	return 0;
}

#ifndef TXT_ONLY
/*=======================================
 *  MySQL Init
 *---------------------------------------*/
int map_sql_init(void)
{
	// main db connection
	mmysql_handle = Sql_Malloc();

	ShowInfo("Connecting to the Map DB Server....\n");
	if( SQL_ERROR == Sql_Connect(mmysql_handle, map_server_id, map_server_pw, map_server_ip, map_server_port, map_server_db) )
		exit(EXIT_FAILURE);

	if( strlen(default_codepage) > 0 )
		if ( SQL_ERROR == Sql_SetEncoding(mmysql_handle, default_codepage) )
			Sql_ShowDebug(mmysql_handle);

	ShowStatus("Connected to main database '%s'.\n", map_server_db);
	Sql_PrintExtendedInfo(mmysql_handle);

	return 0;
}

int map_sql_close(void)
{
	ShowStatus("Close Map DB Connection....\n");
	Sql_Free(mmysql_handle);
	mmysql_handle = NULL;

	if (log_config.sql_logs)
	{
		ShowStatus("Close Log DB Connection....\n");
		Sql_Free(logmysql_handle);
		logmysql_handle = NULL;
	}

	return 0;
}

int log_sql_init(void)
{
	// log db connection
	logmysql_handle = Sql_Malloc();

	ShowInfo("Connecting to the Log Database...\n");
	if ( SQL_ERROR == Sql_Connect(logmysql_handle, log_db_id, log_db_pw, log_db_ip, log_db_port, log_db_db) )
		exit(EXIT_FAILURE);

	if( strlen(default_codepage) > 0 )
		if ( SQL_ERROR == Sql_SetEncoding(logmysql_handle, default_codepage) )
			Sql_ShowDebug(logmysql_handle);

	ShowStatus("Connected to log database '%s'.\n", log_db_db);
	Sql_PrintExtendedInfo(logmysql_handle);

	return 0;
}

struct questinfo *map_add_questinfo(int m, struct questinfo *qi) {
	unsigned short i;

	/* duplicate, override */
	for(i = 0; i < map[m].qi_count; i++) {
		if( &map[m].qi_data[i] && map[m].qi_data[i].nd == qi->nd && map[m].qi_data[i].quest_id == qi->quest_id)
			break;
	}

	if( i == map[m].qi_count )
		RECREATE(map[m].qi_data, struct questinfo, ++map[m].qi_count);
	else { // clear previous criteria on override
		if (map[m].qi_data[i].jobid)
			aFree(map[m].qi_data[i].jobid);
		map[m].qi_data[i].jobid = NULL;
		map[m].qi_data[i].jobid_count = 0;
		if (map[m].qi_data[i].req)
			aFree(map[m].qi_data[i].req);
		map[m].qi_data[i].req = NULL;
		map[m].qi_data[i].req_count = 0;
	}

	memcpy(&map[m].qi_data[i], qi, sizeof(struct questinfo));
	return &map[m].qi_data[i];
}

static void map_free_questinfo(int m) {
	unsigned short i;

	for (i = 0; i < map[m].qi_count; i++) {
		if (map[m].qi_data[i].jobid)
			aFree(map[m].qi_data[i].jobid);
		map[m].qi_data[i].jobid = NULL;
		map[m].qi_data[i].jobid_count = 0;
		if (map[m].qi_data[i].req)
			aFree(map[m].qi_data[i].req);
		map[m].qi_data[i].req = NULL;
		map[m].qi_data[i].req_count = 0;
	}
	aFree(map[m].qi_data);
	map[m].qi_data = NULL;
	map[m].qi_count = 0;
}

struct questinfo *map_has_questinfo(int m, struct npc_data *nd, int quest_id) {
	unsigned short i;

	for (i = 0; i < map[m].qi_count; i++) {
		struct questinfo *qi = &map[m].qi_data[i];
		if (qi->nd == nd && qi->quest_id == quest_id) {
			return qi;
		}
	}

	return NULL;
}

#endif /* not TXT_ONLY */

int map_db_final(DBKey key, DBData *data, va_list ap)
{
	struct map_data_other_server *mdos = db_data2ptr(data);
	if(mdos && mdos->cell == NULL)
		aFree(mdos);
	return 0;
}

int nick_db_final(DBKey key, DBData *data, va_list args)
{
	struct charid2nick* p = db_data2ptr(data);
	struct charid_request* req;

	if( p == NULL )
		return 0;
	while( p->requests )
	{
		req = p->requests;
		p->requests = req->next;
		aFree(req);
	}
	aFree(p);
	return 0;
}

int cleanup_sub(struct block_list *bl, va_list ap)
{
	nullpo_ret(bl);

	switch(bl->type) {
		case BL_PC:
			map_quit((struct map_session_data *) bl);
			break;
		case BL_NPC:
			npc_unload((struct npc_data *)bl,false);
			break;
		case BL_MOB:
			unit_free(bl,CLR_OUTSIGHT);
			break;
		case BL_PET:
		//There is no need for this, the pet is removed together with the player. [Skotlex]
			break;
		case BL_ITEM:
			map_clearflooritem(bl);
			break;
		case BL_SKILL:
			skill_delunit((struct skill_unit *) bl);
			break;
	}

	return 1;
}

static int cleanup_db_sub(DBKey key, DBData *data, va_list va)
{
	return cleanup_sub(db_data2ptr(data), va);
}

/*==========================================
 * mapŽII—¹E—
 *------------------------------------------*/
void do_final(void)
{
	int i, j;
	struct map_session_data* sd;
	struct s_mapiterator* iter;

	ShowStatus("Terminating...\n");

	//Scan any remaining players (between maps?) to kick them out. [Skotlex]
	iter = mapit_getallusers();
	for (sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); sd = (TBL_PC*)mapit_next(iter))
		map_quit(sd);
	mapit_free(iter);

	/* prepares npcs for a faster shutdown process */
	do_clear_npc();

	// remove all objects on maps
	for (i = 0; i < map_num; i++)
	{
		ShowStatus("Cleaning up maps [%d/%d]: %s..."CL_CLL"\r", i+1, map_num, map[i].name);
		if (map[i].m >= 0)
			map_foreachinmap(cleanup_sub, i, BL_ALL);
	}
	ShowStatus("Cleaned up %d maps."CL_CLL"\n", map_num);

	id_db->foreach(id_db,cleanup_db_sub);
	chrif_char_reset_offline();
	chrif_flush_fifo();

	do_final_atcommand();
	do_final_battle();
	do_final_chrif();
	do_final_clif();
	do_final_npc();
	do_final_quest();
	do_final_achievement();
	do_final_script();
	do_final_instance();
	do_final_itemdb();
	do_final_storage();
	do_final_guild();
	do_final_party();
	do_final_pc();
	do_final_pet();
	do_final_homunculus();
	do_final_mercenary();
	do_final_mob();
	do_final_msg();
	do_final_skill();
	do_final_status();
	do_final_unit();
	do_final_battleground();
	do_final_duel();
	do_final_cashshop();
	do_final_clan();
	do_final_vending();
	do_final_buyingstore();
	
	map_db->destroy(map_db, map_db_final);
	
	for (i=0; i<map_num; i++) {
		if(map[i].cell) aFree(map[i].cell);
		if(map[i].block) aFree(map[i].block);
		if(map[i].block_mob) aFree(map[i].block_mob);
		if(battle_config.dynamic_mobs) { //Dynamic mobs flag by [random]
			for (j=0; j<MAX_MOB_LIST_PER_MAP; j++)
				if (map[i].moblist[j]) aFree(map[i].moblist[j]);
		}
		map_free_questinfo(i);
	}

	mapindex_final();
	if(enable_grf)
		grfio_final();

	id_db->destroy(id_db, NULL);
	pc_db->destroy(pc_db, NULL);
	mobid_db->destroy(mobid_db, NULL);
	bossid_db->destroy(bossid_db, NULL);
	nick_db->destroy(nick_db, nick_db_final);
	charid_db->destroy(charid_db, NULL);
	iwall_db->destroy(iwall_db, NULL);
	regen_db->destroy(regen_db, NULL);

#ifndef TXT_ONLY
    map_sql_close();
#endif /* not TXT_ONLY */
	ShowStatus("Finished.\n");
}

static int map_abort_sub(struct map_session_data* sd, va_list ap)
{
	chrif_save(sd, CSAVE_QUIT | CSAVE_INVENTORY | CSAVE_CART);
	return 1;
}


//------------------------------
// Function called when the server
// has received a crash signal.
//------------------------------
void do_abort(void)
{
	static int run = 0;
	//Save all characters and then flush the inter-connection.
	if (run) {
		ShowFatalError("Server has crashed while trying to save characters. Character data can't be saved!\n");
		return;
	}
	run = 1;
	if (!chrif_isconnected())
	{
		if (pc_db->size(pc_db))
			ShowFatalError("Server has crashed without a connection to the char-server, %u characters can't be saved!\n", pc_db->size(pc_db));
		return;
	}
	ShowError("Server received crash signal! Attempting to save all online characters!\n");
	map_foreachpc(map_abort_sub);
	chrif_flush_fifo();
}

/*======================================================
 * Map-Server Version Screen [MC Cameri]
 *------------------------------------------------------*/
static void map_helpscreen(bool do_exit)
{
	ShowInfo("Usage: %s [options]\n", SERVER_NAME);
	ShowInfo("\n");
	ShowInfo("Options:\n");
	ShowInfo("  -?, -h [--help]\t\tDisplays this help screen.\n");
	ShowInfo("  -v [--version]\t\tDisplays the server's version.\n");
	ShowInfo("  --run-once\t\t\tCloses server after loading (testing).\n");
	ShowInfo("  --map-config <file>\t\tAlternative map-server configuration.\n");
	ShowInfo("  --battle-config <file>\tAlternative battle configuration.\n");
	ShowInfo("  --atcommand-config <file>\tAlternative atcommand configuration.\n");
	ShowInfo("  --script-config <file>\tAlternative script configuration.\n");
	ShowInfo("  --msg-config <file>\t\tAlternative message configuration.\n");
	ShowInfo("  --grf-path <file>\t\tAlternative GRF path configuration.\n");
	ShowInfo("  --inter-config <file>\t\tAlternative inter-server configuration.\n");
	ShowInfo("  --log-config <file>\t\tAlternative logging configuration.\n");
	if( do_exit )
		exit(EXIT_SUCCESS);
}

/*======================================================
 * Map-Server Version Screen [MC Cameri]
 *------------------------------------------------------*/
static void map_versionscreen(bool do_exit)
{
	ShowInfo(CL_WHITE"15-3athena GIT hash: %s" CL_RESET"\n", get_git_hash());
	ShowInfo(CL_GREEN"Project page:"CL_RESET"\thttps://15peaces.com/blog/index.php/15-3athena/\n");
	ShowInfo(CL_GREEN"GitHub:"CL_RESET"\thttps://github.com/15peaces/15-3athena/\n");
	ShowInfo(CL_GREEN"Facebook group:"CL_RESET"\thttps://www.facebook.com/groups/153athena/\n");
	ShowInfo("Open "CL_WHITE"readme.html"CL_RESET" for more information.\n");
	if( do_exit )
		exit(EXIT_SUCCESS);
}

/*======================================================
 * Map-Server Init and Command-line Arguments [Valaris]
 *------------------------------------------------------*/
void set_server_type(void)
{
	SERVER_TYPE = ATHENA_SERVER_MAP;
}

/*======================================================
 * Message System
 *------------------------------------------------------*/
struct msg_data {
	char* msg[MAP_MAX_MSG];
};
struct msg_data *map_lang2msgdb(uint8 lang) {
	return (struct msg_data*)idb_get(map_msg_db, lang);
}

void map_do_init_msg(void) {
	int test = 0, i = 0, size;
	char * listelang[] = {
		MSG_CONF_NAME_EN,	//default
		MSG_CONF_NAME_RUS,
		MSG_CONF_NAME_SPN,
		MSG_CONF_NAME_GER,
		MSG_CONF_NAME_CHN,
		MSG_CONF_NAME_MAL,
		MSG_CONF_NAME_IDN,
		MSG_CONF_NAME_FRN,
		MSG_CONF_NAME_POR
	};

	map_msg_db = idb_alloc(DB_OPT_BASE);
	size = ARRAYLENGTH(listelang); //avoid recalc
	while (test != -1 && size > i) { //for all enable lang +(English default)
		test = msg_checklangtype(i, false);
		if (test == 1) msg_config_read(listelang[i], i); //if enable read it and assign i to langtype
		i++;
	}
}
void map_do_final_msg(void) {
	DBIterator *iter = db_iterator(map_msg_db);
	struct msg_data *mdb;

	for (mdb = dbi_first(iter); dbi_exists(iter); mdb = dbi_next(iter)) {
		_do_final_msg(MAP_MAX_MSG, mdb->msg);
		aFree(mdb);
	}
	dbi_destroy(iter);
	map_msg_db->destroy(map_msg_db, NULL);
}
void map_msg_reload(void) {
	map_do_final_msg(); //clear data
	map_do_init_msg();
}
int map_msg_config_read(char *cfgName, int lang) {
	struct msg_data *mdb;

	if ((mdb = map_lang2msgdb(lang)) == NULL)
		CREATE(mdb, struct msg_data, 1);
	else
		idb_remove(map_msg_db, lang);
	idb_put(map_msg_db, lang, mdb);

	if (_msg_config_read(cfgName, MAP_MAX_MSG, mdb->msg) != 0) { //an error occur
		idb_remove(map_msg_db, lang); //@TRYME
		aFree(mdb);
	}
	return 0;
}
const char* map_msg_txt(struct map_session_data *sd, int msg_number) {
	struct msg_data *mdb;
	uint8 lang = 0; //default
	const char *tmp; //to verify result
	if (sd && sd->langtype) lang = sd->langtype;

	if ((mdb = map_lang2msgdb(lang)) != NULL) {
		tmp = _msg_txt(msg_number, MAP_MAX_MSG, mdb->msg);
		if (strcmp(tmp, "??"))
			return tmp;
		ShowDebug("Message #%d not found for langtype %d.\n", msg_number, lang);
	}
	ShowDebug("Selected langtype %d not loaded, trying fallback...\n", lang);
	if (lang != 0 && (mdb = map_lang2msgdb(0)) != NULL) //fallback
		return _msg_txt(msg_number, MAP_MAX_MSG, mdb->msg);
	return "??";
}

/// Called when a terminate signal is received.
void do_shutdown(void)
{
	if( runflag != SERVER_STATE_SHUTDOWN )
	{
		runflag = SERVER_STATE_SHUTDOWN;
		ShowStatus("Shutting down...\n");
		{
			struct map_session_data* sd;
			struct s_mapiterator* iter = mapit_getallusers();
			for( sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); sd = (TBL_PC*)mapit_next(iter) )
				clif_GM_kick(NULL, sd);
			mapit_free(iter);
			flush_fifos();
		}
		chrif_check_shutdown();
	}
}


static bool map_arg_next_value(const char* option, int i, int argc)
{
	if( i >= argc-1 )
	{
		ShowWarning("Missing value for option '%s'.\n", option);
		return false;
	}

	return true;
}


int do_init(int argc, char *argv[])
{
	int i;

#ifdef GCOLLECT
	GC_enable_incremental();
#endif

	INTER_CONF_NAME="conf/inter_athena.conf";
	LOG_CONF_NAME="conf/log_athena.conf";
	MAP_CONF_NAME = "conf/map_athena.conf";
	BATTLE_CONF_FILENAME = "conf/battle_athena.conf";
	ATCOMMAND_CONF_FILENAME = "conf/atcommand_athena.conf";
	SCRIPT_CONF_NAME = "conf/script_athena.conf";
	GRF_PATH_FILENAME = "conf/grf-files.txt";
	safestrncpy(console_log_filepath, "./log/map-msg_log.log", sizeof(console_log_filepath));

	/* Multilanguage */
	MSG_CONF_NAME_EN = "conf/msg_conf/map_msg.conf"; 		// English (default)
	MSG_CONF_NAME_RUS = "conf/msg_conf/map_msg_rus.conf";	// Russian
	MSG_CONF_NAME_SPN = "conf/msg_conf/map_msg_spn.conf";	// Spanish
	MSG_CONF_NAME_GER = "conf/msg_conf/map_msg_ger.conf";	// German
	MSG_CONF_NAME_CHN = "conf/msg_conf/map_msg_chn.conf";	// Chinese
	MSG_CONF_NAME_MAL = "conf/msg_conf/map_msg_mal.conf";	// Malaysian
	MSG_CONF_NAME_IDN = "conf/msg_conf/map_msg_idn.conf";	// Indonesian
	MSG_CONF_NAME_FRN = "conf/msg_conf/map_msg_frn.conf";	// French
	MSG_CONF_NAME_POR = "conf/msg_conf/map_msg_por.conf";	// Brazilian Portuguese
	/* Multilanguage */

	rnd_init();

	for( i = 1; i < argc ; i++ )
	{
		const char* arg = argv[i];

		if( arg[0] != '-' && ( arg[0] != '/' || arg[1] == '-' ) )
		{// -, -- and /
			ShowError("Unknown option '%s'.\n", argv[i]);
			exit(EXIT_FAILURE);
		}
		else if( (++arg)[0] == '-' )
		{// long option
			arg++;

			if( strcmp(arg, "help") == 0 )
			{
				map_helpscreen(true);
			}
			else if( strcmp(arg, "version") == 0 )
			{
				map_versionscreen(true);
			}
			else if( strcmp(arg, "map-config") == 0 )
			{
				if( map_arg_next_value(arg, i, argc) )
					MAP_CONF_NAME = argv[++i];
			}
			else if( strcmp(arg, "battle-config") == 0 )
			{
				if( map_arg_next_value(arg, i, argc) )
					BATTLE_CONF_FILENAME = argv[++i];
			}
			else if( strcmp(arg, "atcommand-config") == 0 )
			{
				if( map_arg_next_value(arg, i, argc) )
					ATCOMMAND_CONF_FILENAME = argv[++i];
			}
			else if( strcmp(arg, "script-config") == 0 )
			{
				if( map_arg_next_value(arg, i, argc) )
					SCRIPT_CONF_NAME = argv[++i];
			}
			else if( strcmp(arg, "msg-config") == 0 )
			{
				if( map_arg_next_value(arg, i, argc) )
					MSG_CONF_NAME_EN = argv[++i];
			}
			else if( strcmp(arg, "grf-path-file") == 0 )
			{
				if( map_arg_next_value(arg, i, argc) )
					GRF_PATH_FILENAME = argv[++i];
			}
			else if( strcmp(arg, "inter-config") == 0 )
			{
				if( map_arg_next_value(arg, i, argc) )
					INTER_CONF_NAME = argv[++i];
			}
			else if( strcmp(arg, "log-config") == 0 )
			{
				if( map_arg_next_value(arg, i, argc) )
					LOG_CONF_NAME = argv[++i];
			}
			else if( strcmp(arg, "run-once") == 0 ) // close the map-server as soon as its done.. for testing [Celest]
			{
				runflag = SERVER_STATE_STOP;
			}
			else
			{
				ShowError("Unknown option '%s'.\n", argv[i]);
				exit(EXIT_FAILURE);
			}
		}
		else switch( arg[0] )
		{// short option
			case '?':
			case 'h':
				map_helpscreen(true);
				break;
			case 'v':
				map_versionscreen(true);
				break;
			default:
				ShowError("Unknown option '%s'.\n", argv[i]);
				exit(EXIT_FAILURE);
		}
	}

	map_config_read(MAP_CONF_NAME);
	chrif_checkdefaultlogin();

	if (save_settings == 0)
		ShowWarning("Value of 'save_settings' is not set, player's data only will be saved every 'autosave_time' (%d seconds).\n", autosave_interval/1000);

	if (!map_ip_set || !char_ip_set) {
		char ip_str[16];
		ip2str(addr_[0], ip_str);

		ShowWarning("CONFIGURATION WARNING - Not all IP addresses in map_athena.conf are configured, autodetecting... (can pick the wrong ip address)\n");

		if (naddr_ == 0)
			ShowError("Unable to determine your IP address...\n");
		else if (naddr_ > 1)
			ShowNotice("Multiple interfaces detected...\n");

		ShowInfo("Defaulting to %s as our IP address\n", ip_str);

		if (!map_ip_set)
			clif_setip(ip_str);
		if (!char_ip_set)
			chrif_setip(ip_str);
	}

	battle_config_read(BATTLE_CONF_FILENAME);
	atcommand_config_read(ATCOMMAND_CONF_FILENAME);
	script_config_read(SCRIPT_CONF_NAME);
	inter_config_read(INTER_CONF_NAME);
	log_config_read(LOG_CONF_NAME);

	id_db = idb_alloc(DB_OPT_BASE);
	pc_db = idb_alloc(DB_OPT_BASE);	//Added for reliable map_id2sd() use. [Skotlex]
	mobid_db = idb_alloc(DB_OPT_BASE);	//Added to lower the load of the lazy mob ai. [Skotlex]
	bossid_db = idb_alloc(DB_OPT_BASE); // Used for Convex Mirror quick MVP search
	map_db = uidb_alloc(DB_OPT_BASE);
	nick_db = idb_alloc(DB_OPT_BASE);
	charid_db = idb_alloc(DB_OPT_BASE);
	regen_db = idb_alloc(DB_OPT_BASE); // efficient status_natural_heal processing
	iwall_db = strdb_alloc(DB_OPT_RELEASE_DATA,2*NAME_LENGTH+2+1); // [Zephyrus] Invisible Walls

#ifndef TXT_ONLY
	map_sql_init();
	if (log_config.sql_logs)
		log_sql_init();
#endif /* not TXT_ONLY */

	mapindex_init();
	if(enable_grf)
		grfio_init(GRF_PATH_FILENAME);

	map_readallmaps();

	add_timer_func_list(map_freeblock_timer, "map_freeblock_timer");
	add_timer_func_list(map_clearflooritem_timer, "map_clearflooritem_timer");
	add_timer_func_list(map_removemobs_timer, "map_removemobs_timer");
	add_timer_interval(gettick()+1000, map_freeblock_timer, 0, 0, 60*1000);

	map_do_init_msg();
	do_init_atcommand();
	do_init_battle();
	do_init_instance();
	do_init_chrif();
	do_init_clan();
	do_init_clif();
	do_init_script();
	do_init_itemdb();
	do_init_skill();
	do_init_mob();
	do_init_pc();
	do_init_status();
	do_init_party();
	do_init_guild();
	do_init_storage();
	do_init_pet();
	do_init_homunculus();
	do_init_mercenary();
	do_init_elemental();
	do_init_quest();
	do_init_achievement();
	do_init_npc();
	do_init_unit();
	do_init_battleground();
	do_init_duel();
	do_init_vending();
	do_init_buyingstore();

	npc_event_do_oninit();	// Init npcs (OnInit)

	if( console )
	{
		//##TODO invoke a CONSOLE_START plugin event
	}

	if (battle_config.pk_mode)
		ShowNotice("Server is running on '"CL_WHITE"PK Mode"CL_RESET"'.\n");

	ShowStatus("Server is '"CL_GREEN"ready"CL_RESET"' and listening on port '"CL_WHITE"%d"CL_RESET"'.\n\n", map_port);

	return 0;
}

// Cell PVP [Napster]
int map_pvp_area(struct map_session_data* sd, bool flag)
{
	switch(flag) 
	{
		case 1:
			clif_map_property(&sd->bl, MAPPROPERTY_FREEPVPZONE, SELF);
			if (sd->pvp_timer == INVALID_TIMER) {
				map[sd->bl.m].cell_pvpuser++;
 
				sd->pvp_timer  = add_timer(gettick()+200, pc_calc_pvprank_timer, sd->bl.id, 0);
				sd->pvp_rank  = 0;
				sd->pvp_lastusers = 0;
				sd->pvp_point  = 5;
				sd->pvp_won   = 0;
				sd->pvp_lost  = 0;
				sd->state.pvp  = 1;
				sd->pvpcan_walkout_tick = gettick();
			}
			break;
		default:
			clif_pvpset(sd, 0, 0, 2);
			map[sd->bl.m].cell_pvpuser--;

			if( sd->pvp_timer != INVALID_TIMER )
				delete_timer(sd->pvp_timer, pc_calc_pvprank_timer);

			sd->pvp_timer  = INVALID_TIMER;
			sd->state.pvp  = 0;
			break;
	}

	return 0;
}