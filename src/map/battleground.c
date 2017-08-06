// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/timer.h"
#include "../common/malloc.h"
#include "../common/nullpo.h"
#include "../common/showmsg.h"
#include "../common/socket.h"
#include "../common/strlib.h"
#include "../common/timer.h"

#include "battleground.h"
#include "battle.h"
#include "clif.h"
#include "guild.h" // guild_send_dot_remove()
#include "map.h"
#include "mapreg.h"
#include "npc.h"
#include "party.h"
#include "pc.h"
#include "pet.h"
#include "homunculus.h"
#include "mercenary.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h> // atoi()

static DBMap* bg_team_db; // int bg_id -> struct battleground_data*
static DBMap* bg_arena_db; // int bg_arena_id -> struct battleground_arena*

static unsigned int bg_team_counter = 0; // Next bg_id
static unsigned int bg_arena_counter = 0; // Next bg_arena_id


struct battleground_data* bg_team_search(int bg_id)
{ // Search a BG Team using bg_id
	if( !bg_id ) return NULL;
	return (struct battleground_data *)idb_get(bg_team_db, bg_id);
}

struct battleground_arena* bg_arena_search(int arena_id)
{ // Search a BG Arena using arena_id
	if( !arena_id ) return NULL;
	return (struct battleground_arena *)idb_get(bg_arena_db, arena_id);
}

struct battleground_arena* bg_name2arena(const char *name)
{
	int i;
	struct battleground_arena* arena = NULL;

	nullpo_retr(NULL, name);

	for(i = 0; i < bg_arena_counter; i++) {
		arena = (struct battleground_arena *)idb_get(bg_arena_db, i);
		if( strcmpi(arena->name,name) == 0 )
			return arena;
		arena = NULL;
	}
	return NULL;
}

struct map_session_data* bg_getavailablesd(struct battleground_data *bg)
{
	int i;
	nullpo_retr(NULL, bg);
	ARR_FIND(0, MAX_BG_MEMBERS, i, bg->members[i].sd != NULL);
	return( i < MAX_BG_MEMBERS ) ? bg->members[i].sd : NULL;
}

enum bg_queue_types bg_str2teamtype (const char *str)
{
	char temp[200], *parse;
	enum bg_queue_types type = BGQT_INVALID;

	nullpo_retr(type, str);
	safestrncpy(temp, str, 200);

	parse = strtok(temp,"|");

	while (parse != NULL) {
		normalize_name(parse," ");
		if( strcmpi(parse,"all") == 0 )
			type |= BGQT_INDIVIDUAL|BGQT_PARTY|BGQT_GUILD;
		else if( strcmpi(parse,"party") == 0 )
			type |= BGQT_PARTY;
		else if( strcmpi(parse,"guild") == 0 )
			type |= BGQT_GUILD;
		else if( strcmpi(parse,"solo") == 0 )
			type |= BGQT_INDIVIDUAL;
		else {
			ShowError("bg_str2teamtype: '%s' unknown type, skipping...\n",parse);
		}
		parse = strtok(NULL,"|");
	}

	return type;
}

static bool bg_read_arenadb(char* str[], int columns, int current)
{// ArenaID,Name,Event,minLevel,maxLevel,reward_win,reward_loss,reward_draw,minPlayers,maxPlayers,minTeamPlayers,delay_var,maxDuration,fillDuration,pGameDuration
	struct battleground_arena* bg_arena;
	bg_arena_counter++;
	
	CREATE(bg_arena, struct battleground_arena, 1);
	
	bg_arena->bg_arena_id = atoi(str[0]);
	safestrncpy(bg_arena->name, str[1], sizeof(bg_arena->name));
	safestrncpy(bg_arena->event_, str[2], sizeof(bg_arena->event_));
	bg_arena->allowed_types = bg_str2teamtype(str[3]);
	bg_arena->min_lv = atoi(str[4]);
	bg_arena->max_lv = atoi(str[5]);
	bg_arena->rew_win = atoi(str[6]);
	bg_arena->rew_loss = atoi(str[7]);
	bg_arena->rew_draw = atoi(str[8]);
	bg_arena->min_players = atoi(str[9]);
	bg_arena->max_players = atoi(str[10]);
	bg_arena->min_team_players = atoi(str[11]);
	safestrncpy(bg_arena->delay_var, str[12], sizeof(bg_arena->delay_var));
	bg_arena->duration = atoi(str[13]);
	bg_arena->fillup_duration = atoi(str[14]);
	bg_arena->pregame_duration = atoi(str[15]);

	bg_arena->queue_id = script_queue_create();

	idb_put(bg_arena_db, bg_arena->bg_arena_id, bg_arena);

	return true;
}

int bg_team_delete(int bg_id)
{ // Deletes BG Team from db
	int i;
	struct map_session_data *sd;
	struct battleground_data *bg = bg_team_search(bg_id);

	if( bg == NULL ) return 0;
	for( i = 0; i < MAX_BG_MEMBERS; i++ )
	{
		if( (sd = bg->members[i].sd) == NULL )
			continue;

		bg_send_dot_remove(sd);
		sd->bg_id = 0;
	}
	idb_remove(bg_team_db, bg_id);
	return 1;
}

int bg_team_warp(int bg_id, unsigned short mapindex, short x, short y)
{ // Warps a Team
	int i;
	struct battleground_data *bg = bg_team_search(bg_id);
	if( bg == NULL ) return 0;
	for( i = 0; i < MAX_BG_MEMBERS; i++ )
		if( bg->members[i].sd != NULL ) pc_setpos(bg->members[i].sd, mapindex, x, y, CLR_TELEPORT);
	return 1;
}

int bg_send_dot_remove(struct map_session_data *sd)
{
	if( sd && sd->bg_id )
		clif_bg_xy_remove(sd);
	return 0;
}

int bg_team_join(int bg_id, struct map_session_data *sd)
{ // Player joins team
	int i;
	struct battleground_data *bg = bg_team_search(bg_id);

	if( bg == NULL || sd == NULL || sd->bg_id ) return 0;

	ARR_FIND(0, MAX_BG_MEMBERS, i, bg->members[i].sd == NULL);
	if( i == MAX_BG_MEMBERS ) return 0; // No free slots

	sd->bg_id = bg_id;
	bg->members[i].sd = sd;
	bg->members[i].x = sd->bl.x;
	bg->members[i].y = sd->bl.y;
	/* populate 'where i came from' */
	if(map[sd->bl.m].flag.nosave || map[sd->bl.m].instance_id >= 0) {
		struct map_data *m=&map[sd->bl.m];
		if(m->save.map)
			memcpy(&bg->members[i].source,&m->save,sizeof(struct point));
		else
			memcpy(&bg->members[i].source,&sd->status.save_point,sizeof(struct point));
	} else
		memcpy(&bg->members[i].source,&sd->status.last_point,sizeof(struct point));
	bg->count++;

	guild_send_dot_remove(sd);

	for( i = 0; i < MAX_BG_MEMBERS; i++ )
	{
		struct map_session_data *pl_sd = bg->members[i].sd;
		if( (pl_sd = bg->members[i].sd) != NULL && pl_sd != sd )
			clif_hpmeter_single(sd->fd, pl_sd->bl.id, pl_sd->battle_status.hp, pl_sd->battle_status.max_hp);
	}

	clif_bg_hp(sd);
	clif_bg_xy(sd);
	return 1;
}

int bg_team_leave(struct map_session_data *sd, enum bg_team_leave_type flag)
{ // Single Player leaves team
	int i, bg_id;
	struct battleground_data *bg;
	char output[128];

	if( sd == NULL || !sd->bg_id )
		return 0;

	bg_send_dot_remove(sd);
	bg_id = sd->bg_id;
	sd->bg_id = 0;

	if( (bg = bg_team_search(bg_id)) == NULL )
		return 0;

	ARR_FIND(0, MAX_BG_MEMBERS, i, bg->members[i].sd == sd);
	if( i < MAX_BG_MEMBERS ) 
	{// Removes member from BG
		if( sd->bg_queue.arena ) {
			bg_queue_player_cleanup(sd);
			pc_setpos(sd,bg->members[i].source.map, bg->members[i].source.x, bg->members[i].source.y, CLR_OUTSIGHT);
		}
		memset(&bg->members[i], 0, sizeof(bg->members[0]));
	}
	bg->count--;

	switch (flag)
	{
		default:
		case BGTL_QUIT:
			sprintf(output, "Server : %s has quit the game...", sd->status.name);
			break;
		case BGTL_LEFT:
			sprintf(output, "Server : %s is leaving the battlefield...", sd->status.name);
			break;
		case BGTL_AFK:
			sprintf(output, "Server : %s has been afk-kicked from the battlefield...", sd->status.name);
			break;
	}
	clif_bg_message(bg, 0, "Server", output, strlen(output) + 1);

	if( bg->logout_event[0] && flag )
		npc_event(sd, bg->logout_event, 0);

	if( sd->bg_queue.arena ) {
		bg_queue_player_cleanup(sd);
	}

	return bg->count;
}

int bg_member_respawn(struct map_session_data *sd)
{ // Respawn after killed
	struct battleground_data *bg;
	if( sd == NULL || !pc_isdead(sd) || !sd->bg_id || (bg = bg_team_search(sd->bg_id)) == NULL )
		return 0;
	if( bg->mapindex == 0 )
		return 0; // Respawn not handled by Core
	pc_setpos(sd, bg->mapindex, bg->x, bg->y, CLR_OUTSIGHT);
	status_revive(&sd->bl, 1, 100);

	return 1; // Warped
}

int bg_create(unsigned short mapindex, short rx, short ry, const char *ev, const char *dev)
{
	struct battleground_data *bg;
	bg_team_counter++;

	CREATE(bg, struct battleground_data, 1);
	bg->bg_id = bg_team_counter;
	bg->count = 0;
	bg->mapindex = mapindex;
	bg->x = rx;
	bg->y = ry;
	safestrncpy(bg->logout_event, ev, sizeof(bg->logout_event));
	safestrncpy(bg->die_event, dev, sizeof(bg->die_event));

	memset(&bg->members, 0, sizeof(bg->members));
	idb_put(bg_team_db, bg_team_counter, bg);

	return bg->bg_id;
}

int bg_team_get_id(struct block_list *bl)
{
	nullpo_ret(bl);
	switch( bl->type )
	{
		case BL_PC:
			return ((TBL_PC*)bl)->bg_id;
		case BL_PET:
			if( ((TBL_PET*)bl)->msd )
				return ((TBL_PET*)bl)->msd->bg_id;
			break;
		case BL_MOB:
		{
			struct map_session_data *msd;
			struct mob_data *md = (TBL_MOB*)bl;
			if( md->special_state.ai && (msd = map_id2sd(md->master_id)) != NULL )
				return msd->bg_id;
			return md->bg_id;
		}
		case BL_HOM:
			if( ((TBL_HOM*)bl)->master )
				return ((TBL_HOM*)bl)->master->bg_id;
			break;
		case BL_MER:
			if( ((TBL_MER*)bl)->master )
				return ((TBL_MER*)bl)->master->bg_id;
			break;
		case BL_SKILL:
			return ((TBL_SKILL*)bl)->group->bg_id;
	}

	return 0;
}

bool bg_send_message(struct map_session_data *sd, const char *mes, int len)
{
	struct battleground_data *bg;

	nullpo_ret(sd);
	nullpo_ret(mes);
	if( sd->bg_id == 0 || (bg = bg_team_search(sd->bg_id)) == NULL )
		return false;
	clif_bg_message(bg, sd->bl.id, sd->status.name, mes, len);
	return false;
}

int bg_send_xy_timer_sub(DBKey key, void *data, va_list ap)
{
	struct battleground_data *bg = (struct battleground_data *)data;
	struct map_session_data *sd;
	int i;
	nullpo_ret(bg);
	for( i = 0; i < MAX_BG_MEMBERS; i++ )
	{
		if( (sd = bg->members[i].sd) == NULL )
			continue;
		if( sd->bl.x != bg->members[i].x || sd->bl.y != bg->members[i].y )
		{ // xy update
			bg->members[i].x = sd->bl.x;
			bg->members[i].y = sd->bl.y;
			clif_bg_xy(sd);
		}
	}
	return 0;
}

int bg_send_xy_timer(int tid, int64 tick, int id, intptr_t data)
{
	bg_team_db->foreach(bg_team_db, bg_send_xy_timer_sub, tick);
	return 0;
}

// Battleground Queue

/**
 * Returns a character's position in a battleground's queue.
 *
 * @param queue_id   The ID of the battleground's queue.
 * @param account_id The character's account ID.
 *
 * @return the position (starting at 1).
 * @retval 0 if the queue doesn't exist or the given account ID isn't present in it.
 */
int bg_id2pos(int queue_id, int account_id)
{
	struct script_queue *queue = script_queue_get(queue_id);
	if (queue)
	{
		int i;
		ARR_FIND(0, VECTOR_LENGTH(queue->entries), i, VECTOR_INDEX(queue->entries, i) == account_id);
		if (i != VECTOR_LENGTH(queue->entries))
		{
			return i+1;
		}
	}
	return 0;
}

void bg_queue_ready_ack(struct battleground_arena *arena, struct map_session_data *sd, bool response)
{
	nullpo_retv(arena);
	nullpo_retv(sd);

	if( arena->begin_timer == INVALID_TIMER || !sd->bg_queue.arena || sd->bg_queue.arena != arena ) {
		bg_queue_player_cleanup(sd);
		return;
	}
	if( !response )
		bg_queue_player_cleanup(sd);
	else {
		struct script_queue *queue = script_queue_get(arena->queue_id);
		int i, count = 0;
		sd->bg_queue.ready = 1;

		for (i = 0; i < VECTOR_LENGTH(queue->entries); i++) {
			struct map_session_data *tsd = map_id2sd(VECTOR_INDEX(queue->entries, i));

			if (tsd != NULL && tsd->bg_queue.ready == 1)
				count++;
		}
		/* check if all are ready then cancel timer, and start game  */
		if (count == VECTOR_LENGTH(queue->entries)) {
			delete_timer(arena->begin_timer,(TimerFunc)bg_begin_timer);
			arena->begin_timer = INVALID_TIMER;
			bg_begin(arena);
		}
	}
}

void bg_queue_player_cleanup(struct map_session_data *sd)
{
	struct battleground_arena *arena;
	nullpo_retv(sd);

	if((arena = bg_arena_search(0)) == NULL)
		return;

	if (sd->bg_queue.client_has_bg_data)
	{
		if(sd->bg_queue.arena)
			clif_bgqueue_notice_delete(sd,BGQND_CLOSEWINDOW,sd->bg_queue.arena->name);
		else
			clif_bgqueue_notice_delete(sd,BGQND_FAIL_NOT_QUEUING,arena->name);
	}
	
	if (sd->bg_queue.arena)
		script_queue_remove(sd->bg_queue.arena->queue_id,sd->status.account_id);
	sd->bg_queue.arena = NULL;
	sd->bg_queue.ready = 0;
	sd->bg_queue.client_has_bg_data = 0;
	sd->bg_queue.type = BGQT_INVALID;
}

void bg_match_over(struct battleground_arena *arena, bool canceled)
{
	struct script_queue *queue = script_queue_get(arena->queue_id);
	int i;

	nullpo_retv(arena);
	if( !arena->ongoing )
		return;
	arena->ongoing = false;

	for (i = 0; i < VECTOR_LENGTH(queue->entries); i++)
	{
		struct map_session_data *sd = map_id2sd(VECTOR_INDEX(queue->entries, i));

		if (sd == NULL)
			continue;

		if (sd->bg_queue.arena) {
			bg_team_leave(sd, 0);
			bg_queue_player_cleanup(sd);
		}
		if (canceled)
			clif_disp_overheadcolor_self(sd->fd, COLOR_RED, "BG Match Canceled: not enough players");
		else
			pc_setglobalreg(sd, arena->delay_var, (unsigned int)time(NULL));
	}

	arena->begin_timer = INVALID_TIMER;
	arena->fillup_timer = INVALID_TIMER;
	/* reset queue */
	script_queue_clear(arena->queue_id);
}

int bg_begin_timer(int tid, int64 tick, int id, intptr_t data) {
	struct battleground_arena* arena = bg_arena_search(id);
	bg_begin(arena);
	arena->begin_timer = INVALID_TIMER;
	return 0;
}

void bg_begin(struct battleground_arena *arena)
{
	struct script_queue *queue = script_queue_get(arena->queue_id);
	int i, count = 0;

	nullpo_retv(arena);
	for (i = 0; i < VECTOR_LENGTH(queue->entries); i++)
	{
		struct map_session_data *sd = map_id2sd(VECTOR_INDEX(queue->entries, i));

		if (sd == NULL)
			continue;

		if (sd->bg_queue.ready == 1)
			count++;
		else
			bg_queue_player_cleanup(sd);
	}

	if( count < arena->min_players ) {
		bg_match_over(arena,true);
	} else {
		arena->ongoing = true;

		/* TODO: make this a arena-independent var? or just .@? */
		mapreg_setreg(add_str("$@bg_queue_id"),arena->queue_id);

		count = 0;
		for (i = 0; i < VECTOR_LENGTH(queue->entries); i++) {
			struct map_session_data *sd = map_id2sd(VECTOR_INDEX(queue->entries, i));

			if (sd == NULL || sd->bg_queue.ready != 1)
				continue;

			mapreg_setreg(reference_uid(add_str("$@bg_member"), count), sd->status.account_id);
			mapreg_setreg(reference_uid(add_str("$@bg_member_group"), count),
			               sd->bg_queue.type == BGQT_GUILD ? sd->status.guild_id :
			               sd->bg_queue.type == BGQT_PARTY ? sd->status.party_id :
			               0
			               );
			mapreg_setreg(reference_uid(add_str("$@bg_member_type"), count),
			               sd->bg_queue.type == BGQT_GUILD ? 1 :
			               sd->bg_queue.type == BGQT_PARTY ? 2 :
			               0
			               );
			count++;
		}
		mapreg_setreg(add_str("$@bg_member_size"),count);

		npc_event_do(arena->event_);
	}
}

void bg_queue_pregame(struct battleground_arena *arena)
{
	struct script_queue *queue;

	int i;
	nullpo_retv(arena);

	queue = script_queue_get(arena->queue_id);
	for (i = 0; i < VECTOR_LENGTH(queue->entries); i++)
	{
		struct map_session_data *sd = map_id2sd(VECTOR_INDEX(queue->entries, i));

		if (sd != NULL)
		{
			clif_bgqueue_battlebegins(sd,arena->bg_arena_id,SELF);
		}
	}
	arena->begin_timer = add_timer( gettick() + (arena->pregame_duration*1000), (TimerFunc)bg_begin_timer, arena->bg_arena_id, 0 );
}

int bg_fillup_timer(int tid, int64 tick, int id, intptr_t data) {
	struct battleground_arena *arena;

	if((arena = bg_arena_search(id)) == NULL)
		return 0;	
	
	bg_queue_pregame(arena);
	arena->fillup_timer = INVALID_TIMER;
	return 0;
}

void bg_queue_check(struct battleground_arena *arena) {
	int count;
	struct script_queue *queue;
	nullpo_retv(arena);

	queue = script_queue_get(arena->queue_id);
	count = VECTOR_LENGTH(queue->entries);
	if( count == arena->max_players ) {
		if( arena->fillup_timer != INVALID_TIMER ) {
			delete_timer(arena->fillup_timer,(TimerFunc)bg_fillup_timer);
			arena->fillup_timer = INVALID_TIMER;
		}
		bg_queue_pregame(arena);
	} else if( count >= arena->min_players && arena->fillup_timer == INVALID_TIMER ) {
		arena->fillup_timer = add_timer( gettick() + (arena->fillup_duration*1000), (TimerFunc)bg_fillup_timer, arena->bg_arena_id, 0 );
	}
}

void bg_queue_add(struct map_session_data *sd, struct battleground_arena *arena, enum bg_queue_types type)
{
	struct battleground_data *bg;

	enum BATTLEGROUNDS_QUEUE_ACK result = bg_can_queue(sd,arena,type);
	struct script_queue *queue = NULL;
	int i, count = 0;

	nullpo_retv(sd);
	nullpo_retv(arena);

	if((bg = bg_team_search(sd->bg_id)) == NULL)
		return;

	if( arena->begin_timer != INVALID_TIMER || arena->ongoing ) {
		clif_bgqueue_ack(sd,BGQA_FAIL_QUEUING_FINISHED,arena->bg_arena_id);
		return;
	}

	if( result != BGQA_SUCCESS ) {
		clif_bgqueue_ack(sd,result,arena->bg_arena_id);
		return;
	}

	switch( type ) { /* guild/party already validated in can_queue */
		case BGQT_PARTY: {
			struct party_data *p = party_search(sd->status.party_id);
			for( i = 0; i < MAX_PARTY; i++ ) {
				if( !p->data[i].sd || p->data[i].sd->bg_queue.arena != NULL ) continue;
				count++;
			}
		}
			break;
		case BGQT_GUILD: {
			struct guild *g = guild_search(sd->status.guild_id);
			for ( i=0; i < MAX_GUILD; i++ ) {
				if ( !g->member[i].sd || g->member[i].sd->bg_queue.arena != NULL )
					continue;
				count++;
			}
						 }
			break;
		case BGQT_INDIVIDUAL:
			count = 1;
			break;
	}

	if( !(queue = script_queue_get(arena->queue_id)) || (VECTOR_LENGTH(queue->entries)+count) > arena->max_players ) 
	{
		clif_bgqueue_ack(sd,BGQA_FAIL_PPL_OVERAMOUNT,arena->bg_arena_id);
		return;
	}

	switch( type ) {
		case BGQT_INDIVIDUAL:
			sd->bg_queue.type = type;
			sd->bg_queue.arena = arena;
			sd->bg_queue.ready = 0;
			script_queue_add(arena->queue_id,sd->status.account_id);
			clif_bgqueue_joined(sd, VECTOR_LENGTH(queue->entries));
			clif_bgqueue_update_info(sd,arena->bg_arena_id, VECTOR_LENGTH(queue->entries));
			break;
		case BGQT_PARTY: {
			struct party_data *p = party_search(sd->status.party_id);
			for( i = 0; i < MAX_PARTY; i++ ) {
				if( !p->data[i].sd || p->data[i].sd->bg_queue.arena != NULL ) continue;
				p->data[i].sd->bg_queue.type = type;
				p->data[i].sd->bg_queue.arena = arena;
				p->data[i].sd->bg_queue.ready = 0;
				script_queue_add(arena->queue_id,p->data[i].sd->status.account_id);
				clif_bgqueue_joined(p->data[i].sd, VECTOR_LENGTH(queue->entries));
				clif_bgqueue_update_info(p->data[i].sd,arena->bg_arena_id, VECTOR_LENGTH(queue->entries));
			}
		}
			break;
		case BGQT_GUILD: {
			struct guild *g = guild_search(sd->status.guild_id);
			for ( i=0; i<MAX_GUILD; i++ ) {
				if ( !g->member[i].sd || g->member[i].sd->bg_queue.arena != NULL )
					continue;
				g->member[i].sd->bg_queue.type = type;
				g->member[i].sd->bg_queue.arena = arena;
				g->member[i].sd->bg_queue.ready = 0;
				script_queue_add(arena->queue_id,g->member[i].sd->status.account_id);
				clif_bgqueue_joined(g->member[i].sd, VECTOR_LENGTH(queue->entries));
				clif_bgqueue_update_info(g->member[i].sd,arena->bg_arena_id, VECTOR_LENGTH(queue->entries));
			}
		}
			break;
	}
	clif_bgqueue_ack(sd,BGQA_SUCCESS,arena->bg_arena_id);
	bg_queue_check(arena);
}

enum BATTLEGROUNDS_QUEUE_ACK bg_can_queue(struct map_session_data *sd, struct battleground_arena *arena, enum bg_queue_types type) {
	struct battleground_data *bg;
	int tick;
	unsigned int tsec;

	nullpo_retr(BGQA_FAIL_TYPE_INVALID, sd);
	nullpo_retr(BGQA_FAIL_TYPE_INVALID, arena);
	
	if ((bg = bg_team_search(sd->bg_id)) == NULL)
		return BGQA_FAIL_TYPE_INVALID;
	
	if( !(arena->allowed_types & type) )
		return BGQA_FAIL_TYPE_INVALID;

	if ( sd->status.base_level > arena->max_lv || sd->status.base_level < arena->min_lv )
		return BGQA_FAIL_LEVEL_INCORRECT;

	if ((sd->class_ & JOBL_2) == 0) /* TODO: maybe make this a per-arena setting, so users may make custom arenas like baby-only,whatever. */
		return BGQA_FAIL_CLASS_INVALID;

	tsec = (unsigned int)time(NULL);

	if ( ( tick = pc_readglobalreg(sd, arena->delay_var) ) && tsec < tick ) {
		char response[100];
		if( (tick-tsec) > 60 )
			sprintf(response, "You can't reapply to this arena so fast. Apply to the different arena or wait %u minute(s)", (tick - tsec) / 60);
		else
			sprintf(response, "You can't reapply to this arena so fast. Apply to the different arena or wait %u seconds", (tick - tsec));
		clif_disp_overheadcolor_self(sd->fd, COLOR_RED, response);
		return BGQA_FAIL_COOLDOWN;
	}

	if( sd->bg_queue.arena != NULL )
		return BGQA_DUPLICATE_REQUEST;

	switch(type) {
		case BGQT_GUILD:
			if( !sd->status.guild_id || !sd->state.gmaster_flag )
				return BGQA_NOT_PARTY_GUILD_LEADER;
			else {
				struct guild *g;
				if( (g = guild_search(sd->status.guild_id) ) ) {
				int i, count = 0;
					for ( i=0; i<g->max_member; i++ ) {
						if ( !g->member[i].sd || g->member[i].sd->bg_queue.arena != NULL )
							continue;
						count++;
					}
					if ( count < arena->min_team_players ) {
						char response[121];
						if( count != g->connect_member && g->connect_member >= arena->min_team_players )
							sprintf(response, "Can't apply: not enough members in your team/guild that have not entered the queue in individual mode, minimum is %d", arena->min_team_players);
						else
							sprintf(response, "Can't apply: not enough members in your team/guild, minimum is %d", arena->min_team_players);
						clif_disp_overheadcolor_self(sd->fd, COLOR_RED, response);
						return BGQA_FAIL_TEAM_COUNT;
					}
				}
			}
			break;
		case BGQT_PARTY:
			if( !sd->status.party_id )
				return BGQA_NOT_PARTY_GUILD_LEADER;
			else {
				struct party_data *p;
				if( (p = party_search(sd->status.party_id) ) ) {
					int i, count = 0;
					bool is_leader = false;

					for(i = 0; i < MAX_PARTY; i++) {
						if( !p->data[i].sd )
							continue;
						if( p->party.member[i].leader && sd == p->data[i].sd )
							is_leader = true;
						if( p->data[i].sd->bg_queue.arena == NULL )
							count++;
					}

					if( !is_leader )
						return BGQA_NOT_PARTY_GUILD_LEADER;

					if( count < arena->min_team_players ) {
						char response[121];
						if( count != p->party.count && p->party.count >= arena->min_team_players )
							sprintf(response, "Can't apply: not enough members in your team/party that have not entered the queue in individual mode, minimum is %d", arena->min_team_players);
						else
							sprintf(response, "Can't apply: not enough members in your team/party, minimum is %d",arena->min_team_players);
						clif_disp_overheadcolor_self(sd->fd, COLOR_RED, response);
						return BGQA_FAIL_TEAM_COUNT;
					}
				} else
					return BGQA_NOT_PARTY_GUILD_LEADER;
			}
			break;
		case BGQT_INDIVIDUAL:/* already did */
			break;
		default:
			ShowDebug("bg_can_queue: unknown/unsupported type %u\n", type);
			return BGQA_DUPLICATE_REQUEST;
	}
	return BGQA_SUCCESS;
}

void do_init_battleground(void)
{
	bg_team_db = idb_alloc(DB_OPT_RELEASE_DATA);
	bg_arena_db = idb_alloc(DB_OPT_BASE);

	add_timer_func_list(bg_send_xy_timer, "bg_send_xy_timer");
	add_timer_interval(gettick() + battle_config.bg_update_interval, bg_send_xy_timer, 0, 0, battle_config.bg_update_interval);

	sv_readdb(db_path, "bg_arena_db.txt", ',', 16, 16, -1, &bg_read_arenadb);
}

void do_final_battleground(void)
{
	bg_team_db->destroy(bg_team_db, NULL);
	bg_arena_db->destroy(bg_arena_db, NULL);
}
