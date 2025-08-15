// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/db.h"
#include "../common/malloc.h"
#include "../common/socket.h"

#include "achievement.h"
#include "unit.h"
#include "map.h"
#include "path.h"
#include "pc.h"
#include "mob.h"
#include "pet.h"
#include "homunculus.h"
#include "instance.h"
#include "mercenary.h"
#include "elemental.h"
#include "skill.h"
#include "clif.h"
#include "duel.h"
#include "npc.h"
#include "guild.h"
#include "status.h"
#include "battle.h"
#include "battleground.h"
#include "chat.h"
#include "trade.h"
#include "vending.h"
#include "party.h"
#include "intif.h"
#include "chrif.h"
#include "script.h"
#include "storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Directions values
// 1 0 7
// 2 . 6
// 3 4 5
const short dirx[8] = { 0,-1,-1,-1,0,1,1,1 }; ///lookup to know where will move to x according dir
const short diry[8] = { 1,1,0,-1,-1,-1,0,1 }; ///lookup to know where will move to y according dir

int unit_unattackable(struct block_list *bl);

/*==========================================
 * Get the unit_data related to the bl
 * @param bl : Object to get the unit_data from \n
 *	valid type are : BL_PC|BL_MOB|BL_PET|BL_NPC|BL_HOM|BL_MER|BL_ELEM
 * @return unit_data of bl or NULL
 *------------------------------------------*/
struct unit_data* unit_bl2ud(struct block_list *bl)
{
	switch (bl->type)
	{
		case BL_PC: return &((struct map_session_data*)bl)->ud;
		case BL_MOB: return &((struct mob_data*)bl)->ud;
		case BL_PET: return &((struct pet_data*)bl)->ud;
		case BL_NPC: return &((struct npc_data*)bl)->ud;
		case BL_HOM: return &((struct homun_data*)bl)->ud;
		case BL_MER: return &((struct mercenary_data*)bl)->ud;
		case BL_ELEM: return &((struct elemental_data*)bl)->ud;
		default: return NULL;
	}
}

/*==========================================
 * Tells a unit to walk to a specific coordinate
 * @param bl: Unit to walk [ALL]
 * @return 1: Success 0: Fail
 *------------------------------------------*/
int unit_walktoxy_sub(struct block_list *bl)
{
	int i;
	struct walkpath_data wpd;
	struct unit_data *ud = NULL;

	nullpo_retr(1, bl);
	ud = unit_bl2ud(bl);
	if(ud == NULL) return 0;

	if( !path_search(&wpd,bl->m,bl->x,bl->y,ud->to_x,ud->to_y,ud->state.walk_easy,CELL_CHKNOPASS) )
		return 0;

#ifdef OFFICIAL_WALKPATH
	if (!path_search_long(NULL, bl->m, bl->x, bl->y, ud->to_x, ud->to_y, CELL_CHKNOPASS) // Check if there is an obstacle between
		&& wpd.path_len > 14	// Official number of walkable cells is 14 if and only if there is an obstacle between. [malufett]
		&& (bl->type != BL_NPC)) // If type is a NPC, please disregard.
		return 0;
#endif

	memcpy(&ud->walkpath,&wpd,sizeof(wpd));
	
	if (ud->target_to && ud->chaserange>1) {
		//Generally speaking, the walk path is already to an adjacent tile
		//so we only need to shorten the path if the range is greater than 1.
		int dir;
		//Trim the last part of the path to account for range,
		//but always move at least one cell when requested to move.
		for (i = (ud->chaserange * 10) - 10; i > 0 && ud->walkpath.path_len > 1;) {
		   ud->walkpath.path_len--;
			dir = ud->walkpath.path[ud->walkpath.path_len];
			if(dir&1)
				i -= 10 * 20; //When chasing, units will target a diamond-shaped area in range [Playtester]
			else
				i-=10;
			ud->to_x -= dirx[dir];
			ud->to_y -= diry[dir];
		}
	}

	ud->state.change_walk_target=0;

	if (bl->type == BL_PC)
	{
		((TBL_PC *)bl)->head_dir = 0;
		clif_walkok((TBL_PC*)bl);
	}
	clif_move(ud);

	if(ud->walkpath.path_pos>=ud->walkpath.path_len)
		i = -1;
	else if(ud->walkpath.path[ud->walkpath.path_pos]&1)
		i = status_get_speed(bl)*14/10;
	else
		i = status_get_speed(bl);
	if( i > 0)
		ud->walktimer = add_timer(gettick()+i,unit_walktoxy_timer,bl->id,i);
	return 1;
}

TBL_PC* unit_get_master(struct block_list *bl) {
	if (bl)
		switch (bl->type) {
		case BL_HOM: return (((TBL_HOM *)bl)->master);
		case BL_ELEM: return (((TBL_ELEM *)bl)->master);
		case BL_PET: return (((TBL_PET *)bl)->master);
		case BL_MER: return (((TBL_MER *)bl)->master);
		}
	return NULL;
}

int* unit_get_masterteleport_timer(struct block_list *bl) {
	if (bl)
		switch (bl->type) {
		case BL_HOM: return &(((TBL_HOM *)bl)->masterteleport_timer);
		case BL_ELEM: return &(((TBL_ELEM *)bl)->masterteleport_timer);
		case BL_PET: return &(((TBL_PET *)bl)->masterteleport_timer);
		case BL_MER: return &(((TBL_MER *)bl)->masterteleport_timer);
		}
	return NULL;
}

int unit_teleport_timer(int tid, int64 tick, int id, intptr_t data) {
	struct block_list *bl = map_id2bl(id);
	int *mast_tid = unit_get_masterteleport_timer(bl);

	if (tid == INVALID_TIMER || mast_tid == NULL)
		return 0;
	else if (*mast_tid != tid || bl == NULL)
		return 0;
	else {
		TBL_PC *msd = unit_get_master(bl);

		if (msd && !check_distance_bl(&msd->bl, bl, data)) {
			*mast_tid = INVALID_TIMER;
			unit_warp(bl, msd->bl.m, msd->bl.x, msd->bl.y, CLR_TELEPORT);
		}
		else // No timer needed
			*mast_tid = INVALID_TIMER;
	}
	return 0;
}

/**
 * Checks if a slave unit is outside their max distance from master
 * If so, starts a timer (default: 3 seconds) which will teleport the unit back to master
 * @param sbl: Object with a master [MOB|PET|HOM|MER|ELEM]
 * @return 0
 */
int unit_check_start_teleport_timer(struct block_list *sbl)
{
	TBL_PC *msd = NULL;
	int max_dist = 0;

	switch (sbl->type) {
		case BL_HOM:
		case BL_ELEM:
		case BL_PET:
		case BL_MER:
			msd = unit_get_master(sbl);
			break;
		default:
			return 0;
	}

	switch (sbl->type) {
		case BL_HOM: max_dist = AREA_SIZE; break;
		case BL_ELEM: max_dist = MAX_ELEDISTANCE; break;
		case BL_PET: max_dist = AREA_SIZE; break;
		case BL_MER: max_dist = MAX_MER_DISTANCE; break;
	}

	//If there is a master and it's a valid type
	if (msd && max_dist) {
		int *msd_tid = unit_get_masterteleport_timer(sbl);

		if (msd_tid == NULL)
			return 0;

		if (!check_distance_bl(&msd->bl, sbl, max_dist)) {
			if (*msd_tid == INVALID_TIMER || *msd_tid == 0)
				*msd_tid = add_timer(gettick() + 3000, unit_teleport_timer, sbl->id, max_dist);
		}
		else {
			if (*msd_tid && *msd_tid != INVALID_TIMER)
				delete_timer(*msd_tid, unit_teleport_timer);
			*msd_tid = INVALID_TIMER; //cancel recall
		}
	}
	return 0;
}

/**
 * Triggered on full step if stepaction is true and executes remembered action.
 * @param tid: Timer ID
 * @param tick: Unused
 * @param id: ID of bl to do the action
 * @param data: Not used
 * @return 1: Success 0: Fail (No valid bl)
 */
int unit_step_timer(int tid, int64 tick, int id, intptr_t data)
{
	struct block_list *bl;
	struct unit_data *ud;
	int target_id;

	bl = map_id2bl(id);

	if (!bl || bl->prev == NULL)
		return 0;

	ud = unit_bl2ud(bl);

	if (!ud)
		return 0;

	if (ud->steptimer != tid) {
		ShowError("unit_step_timer mismatch %d != %d\n", ud->steptimer, tid);
		return 0;
	}

	ud->steptimer = INVALID_TIMER;

	if (!ud->stepaction)
		return 0;

	//Set to false here because if an error occurs, it should not be executed again
	ud->stepaction = false;

	if (!ud->target_to)
		return 0;

	//Flush target_to as it might contain map coordinates which should not be used by other functions
	target_id = ud->target_to;
	ud->target_to = 0;

	//If stepaction is set then we remembered a client request that should be executed on the next step
	//Execute request now if target is in attack range
	if (ud->stepskill_id && skill_get_inf(ud->stepskill_id) & INF_GROUND_SKILL) {
		//Execute ground skill
		struct map_data *md = &map[bl->m];
		unit_skilluse_pos(bl, target_id%md->xs, target_id / md->xs, ud->stepskill_id, ud->stepskill_lv);
	}
	else {
		//If a player has target_id set and target is in range, attempt attack
		struct block_list *tbl = map_id2bl(target_id);
		if (!tbl || !status_check_visibility(bl, tbl)) {
			return 0;
		}
		if (ud->stepskill_id == 0) {
			//Execute normal attack
			unit_attack(bl, tbl->id, (ud->state.attack_continue) + 2);
		}
		else {
			//Execute non-ground skill
			unit_skilluse_id(bl, tbl->id, ud->stepskill_id, ud->stepskill_lv);
		}
	}

	return 1;
}

/*==========================================
 * Defines when to refresh the walking character to object and restart the timer if applicable \n
 * Also checks for speed update, target location, and slave teleport timers
 * @param tid: Timer ID
 * @param tick: Current tick to decide next timer update
 * @param data: Data used in timer calls
 * @return 0 or unit_walktoxy_sub() or unit_walktoxy()
 *------------------------------------------*/
static int unit_walktoxy_timer(int tid, int64 tick, int id, intptr_t data)
{
	int i;
	int x,y,dx,dy;
	unsigned char icewall_walk_block;
	uint8 dir;
	struct block_list       *bl;
	struct unit_data        *ud;
	
	TBL_PC *sd = NULL;
	TBL_MOB *md = NULL;
	TBL_ELEM *ed = NULL;

	bl = map_id2bl(id);
	if(bl == NULL)
		return 0;

	switch (bl->type) { //svoid useless cast, we can only be 1 type
	case BL_PC: sd = BL_CAST(BL_PC, bl); break;
	case BL_MOB: md = BL_CAST(BL_MOB, bl); break;
	case BL_ELEM: ed = BL_CAST(BL_ELEM, bl); break;

	}
	
	ud = unit_bl2ud(bl);
	
	if(ud == NULL) return 0;

	if(ud->walktimer != tid){
		ShowError("unit_walk_timer mismatch %d != %d\n",ud->walktimer,tid);
		return 0;
	}
	ud->walktimer = INVALID_TIMER;
	if( bl->prev == NULL ) return 0; // block_list ���甲���Ă���̂ňړ���~����

	if(ud->walkpath.path_pos>=ud->walkpath.path_len)
		return 0;

	if(ud->walkpath.path[ud->walkpath.path_pos]>=8)
		return 1;
	x = bl->x;
	y = bl->y;

	dir = ud->walkpath.path[ud->walkpath.path_pos];
	ud->dir = dir;

	dx = dirx[(int)dir];
	dy = diry[(int)dir];

	// Get icewall walk block depending on Status Immune mode (players can't be trapped)
	if (md && status_has_mode(&md->status, MD_STATUS_IMMUNE))
		icewall_walk_block = battle_config.boss_icewall_walk_block;
	else if (md)
		icewall_walk_block = battle_config.mob_icewall_walk_block;
	else
		icewall_walk_block = 0;

	//Monsters will walk into an icewall from the west and south if they already started walking
	if (map_getcell(bl->m, x + dx, y + dy, CELL_CHKNOPASS)
		&& (icewall_walk_block == 0 || !map_getcell(bl->m, x + dx, y + dy, CELL_CHKICEWALL) || dx < 0 || dy < 0))
		return unit_walktoxy_sub(bl);
	
	//Monsters can only leave icewalls to the west and south
	//But if movement fails more than icewall_walk_block times, they can ignore this rule
	if (md && md->walktoxy_fail_count < icewall_walk_block && map_getcell(bl->m, x, y, CELL_CHKICEWALL) && (dx > 0 || dy > 0)) {
		//Needs to be done here so that rudeattack skills are invoked
		md->walktoxy_fail_count++;
		clif_fixpos(bl);
		//Monsters in this situation first use a chase skill, then unlock target and then use an idle skill
		if (!(++ud->walk_count%WALK_SKILL_INTERVAL))
			mobskill_use(md, tick, -1);
		mob_unlocktarget(md, tick);
		if (!(++ud->walk_count%WALK_SKILL_INTERVAL))
			mobskill_use(md, tick, -1);
		return 0;
	}

	// Refresh view for all those we lose sight
	map_foreachinmovearea(clif_outsight, bl, AREA_SIZE, dx, dy, sd?BL_ALL:BL_PC, bl);

	x += dx;
	y += dy;
	map_moveblock(bl, x, y, tick);
	ud->walk_count++; //walked cell counter, to be used for walk-triggered skills. [Skotlex]

	if (bl->x != x || bl->y != y || ud->walktimer != INVALID_TIMER)
		return 0; //map_moveblock has altered the object beyond what we expected (moved/warped it)

	ud->walktimer = -2; // arbitrary non-INVALID_TIMER value to make the clif code send walking packets
	map_foreachinmovearea(clif_insight, bl, AREA_SIZE, -dx, -dy, sd?BL_ALL:BL_PC, bl);
	ud->walktimer = INVALID_TIMER;
	
	switch (bl->type) {
	case BL_PC: {
		if( sd->touching_id )
			npc_touchnext_areanpc(sd,false);
		if(map_getcell(bl->m,x,y,CELL_CHKNPC)) {
			npc_touch_areanpc(sd,bl->m,x,y);
			if (bl->prev == NULL) //Script could have warped char, abort remaining of the function.
				return 0;
		} else
			sd->areanpc_id=0;

		pc_cell_basilica(sd);

		// Cell PVP [Napster]
		if( !sd->state.pvp && map_getcell( sd->bl.m, sd->bl.x, sd->bl.y, CELL_CHKPVP) )
			map_pvp_area(sd, 1);
		else if( sd->state.pvp && !map_getcell( sd->bl.m, sd->bl.x, sd->bl.y, CELL_CHKPVP) )
			map_pvp_area(sd, 0);

	}
	break;
	case BL_MOB: {
		//Movement was successful, reset walktoxy_fail_count
		md->walktoxy_fail_count = 0;
		if( map_getcell(bl->m,x,y,CELL_CHKNPC) ) {
			if( npc_touch_areanpc2(md) ) return 0; // Warped
		} else
			md->areanpc_id = 0;
		if (md->min_chase > md->db->range3) md->min_chase--;
		//Walk skills are triggered regardless of target due to the idle-walk mob state.
		//But avoid triggering on stop-walk calls.
		if(tid != INVALID_TIMER &&
			!(ud->walk_count%WALK_SKILL_INTERVAL) &&
			map[bl->m].users > 0 &&
			mobskill_use(md, tick, -1))
	  	{
			if (!(ud->skill_id == NPC_SELFDESTRUCTION && ud->skilltimer != INVALID_TIMER)
				&& md->state.skillstate != MSS_WALK) //Walk skills are supposed to be used while walking
			{ // Skill used, abort walking
				clif_fixpos(bl); //Fix position as walk has been cancelled.
				return 0;
			}
			//Resend walk packet for proper Self Destruction display.
			clif_move(ud);
		}
	}
	break;
	case BL_ELEM: {
		unit_check_start_teleport_timer(&ed->bl);

		// Elementals shouldn't need any of this arena stuff but leave here just in case. [Rytech]
		//if( map_getcell(bl->m,x,y,CELL_CHKNPC) ) {
		//	if( npc_touch_areanpc2(ed) ) return 0; // Warped
		//} else
		//	ed->areanpc_id = 0;
		if (ed->min_chase > ed->db->range3) ed->min_chase--;
		//Walk skills are triggered regardless of target due to the idle-walk mob state.
		//But avoid triggering on stop-walk calls.
		if(tid != INVALID_TIMER &&
			!(ud->walk_count%WALK_SKILL_INTERVAL) &&
			elemskill_use(ed, tick, -1))
	  	{
			if (!(ud->skill_id == NPC_SELFDESTRUCTION && ud->skilltimer != INVALID_TIMER))
			{	//Skill used, abort walking
				clif_fixpos(bl); //Fix position as walk has been cancelled.
				return 0;
			}
			//Resend walk packet for proper Self Destruction display.
			clif_move(ud);
		}
	}
	break;
	}

	if(tid == INVALID_TIMER) //A directly invoked timer is from battle_stop_walking, therefore the rest is irrelevant.
		return 0;

	//If stepaction is set then we remembered a client request that should be executed on the next step
	if (ud->stepaction && ud->target_to) {
		//Delete old stepaction even if not executed yet, the latest command is what counts
		if (ud->steptimer != INVALID_TIMER) {
			delete_timer(ud->steptimer, unit_step_timer);
			ud->steptimer = INVALID_TIMER;
		}
		//Delay stepactions by half a step (so they are executed at full step)
		if (ud->walkpath.path[ud->walkpath.path_pos] & 1)
			i = status_get_speed(bl) * 14 / 20;
		else
			i = status_get_speed(bl) / 2;
		ud->steptimer = add_timer(tick + i, unit_step_timer, bl->id, 0);
	}
		
	if (ud->state.change_walk_target) {
		if (unit_walktoxy_sub(bl)) {
			return 1;
		}
		else {
			clif_fixpos(bl);
			return 0;
		}
	}

	ud->walkpath.path_pos++;
	if(ud->walkpath.path_pos>=ud->walkpath.path_len)
		i = -1;
	else if(ud->walkpath.path[ud->walkpath.path_pos]&1)
		i = status_get_speed(bl)*14/10;
	else
		i = status_get_speed(bl);

	if(i > 0)
		ud->walktimer = add_timer(tick+i,unit_walktoxy_timer,id,i);
	else if(ud->state.running) {
		//Keep trying to run.
		if (!unit_run(bl) || unit_wugdash(bl, sd))
			ud->state.running = 0;
	}
	else if (!ud->stepaction && ud->target_to) {
		//Update target trajectory.
		struct block_list *tbl = map_id2bl(ud->target);
		if (!tbl || !status_check_visibility(bl, tbl)) {	//Cancel chase.
			ud->to_x = bl->x;
			ud->to_y = bl->y;
			if (tbl && bl->type == BL_MOB && mob_warpchase((TBL_MOB*)bl, tbl))
				return 0;
			ud->target_to = 0;
			return 0;
		}
		if (tbl->m == bl->m && check_distance_bl(bl, tbl, ud->chaserange))
		{	//Reached destination.
			if (ud->state.attack_continue)
			{	//Aegis uses one before every attack, we should
				//only need this one for syncing purposes. [Skotlex]
				ud->target_to = 0;
				clif_fixpos(bl);
				unit_attack(bl, tbl->id, ud->state.attack_continue);
			}
		} else { //Update chase-path
			unit_walktobl(bl, tbl, ud->chaserange, ud->state.walk_easy|(ud->state.attack_continue?2:0));
			return 0;
		}
	}
	else {	//Stopped walking. Update to_x and to_y to current location [Skotlex]
		ud->to_x = bl->x;
		ud->to_y = bl->y;

		if (battle_config.official_cell_stack_limit > 0
			&& map_count_oncell(bl->m, x, y, BL_CHAR | BL_NPC, 1) > battle_config.official_cell_stack_limit) {
			//Walked on occupied cell, call unit_walktoxy again
			if (ud->steptimer != INVALID_TIMER) {
				//Execute step timer on next step instead
				delete_timer(ud->steptimer, unit_step_timer);
				ud->steptimer = INVALID_TIMER;
			}
			return unit_walktoxy(bl, x, y, 8);
		}
	}
	return 0;
}

/*==========================================
 * Delays an xy timer
 * @param tid: Timer ID
 * @param tick: Unused
 * @param id: ID of bl to delay timer on
 * @param data: Data used in timer calls
 * @return 1: Success 0: Fail (No valid bl)
 *------------------------------------------*/
int unit_delay_walktoxy_timer(int tid, int64 tick, int id, intptr_t data)
{
	struct block_list *bl = map_id2bl(id);

	if (!bl || bl->prev == NULL)
		return 0;
	unit_walktoxy(bl, (short)((data>>16)&0xffff), (short)(data&0xffff), 0);
	return 1;
}

int unit_delay_walktobl_timer(int tid, int64 tick, int id, intptr_t data)
{
	struct block_list *bl = map_id2bl(id), *tbl = map_id2bl(data);

	if (!bl || bl->prev == NULL || tbl == NULL)
		return 0;
	else {
		struct unit_data* ud = unit_bl2ud(bl);
		unit_walktobl(bl, tbl, 0, 0);
		ud->target_to = 0;
	}
	return 1;
}

/*==========================================
 * Begins the function of walking a unit to an x,y location \n
 * This is where the path searches and unit can_move checks are done
 * @param bl: Object to send to x,y coordinate
 * @param x: X coordinate where the object will be walking to
 * @param y: Y coordinate where the object will be walking to
 * @param flag: Parameter to decide how to walk \n
 *	&1: Easy walk (fail if CELL_CHKNOPASS is in direct path) \n
 *	&2: Force walking (override can_move) \n
 *	&4: Delay walking for can_move
 *  &8: Search for an unoccupied cell and cancel if none available
 * @return 1: Success 0: Fail or unit_walktoxy_sub()
 *------------------------------------------*/
int unit_walktoxy( struct block_list *bl, short x, short y, int flag)
{
	struct unit_data* ud = NULL;
	struct status_change* sc = NULL;
	struct walkpath_data wpd;
	TBL_PC *sd = NULL;

	nullpo_ret(bl);
	
	ud = unit_bl2ud(bl);
	
	if( ud == NULL) return 0;


	if (!path_search(&wpd, bl->m, bl->x, bl->y, x, y, flag & 1, CELL_CHKNOPASS)) // Count walk path cells
		return 0;

#ifdef OFFICIAL_WALKPATH
	if (!path_search_long(NULL, bl->m, bl->x, bl->y, x, y, CELL_CHKNOPASS) // Check if there is an obstacle between
		&& wpd.path_len > 14	// Official number of walkable cells is 14 if and only if there is an obstacle between. [malufett]
		&& (bl->type != BL_NPC)) // If type is a NPC, please disregard.
		return 0;
#endif

	if ((wpd.path_len > MAX_WALKPATH) && (bl->type != BL_NPC))
		return 0;

	if (bl->type == BL_PC)
		sd = BL_CAST(BL_PC, bl);

	if ((flag & 8) && !map_closest_freecell(bl->m, &x, &y, BL_CHAR | BL_NPC, 1)) //This might change x and y
		return 0;

	if (flag & 4) {
		unit_unattackable(bl);
		unit_stop_attack(bl);
		if (DIFF_TICK(ud->canmove_tick, gettick()) > 0 && DIFF_TICK(ud->canmove_tick, gettick()) < 2000)
		{	// Delay walking command. [Skotlex]
			add_timer(ud->canmove_tick + 1, unit_delay_walktoxy_timer, bl->id, (x << 16) | (y & 0xFFFF));
			return 1;
		}
	}

	if(!(flag&2) && (!(status_get_mode(bl)&MD_CANMOVE) || !unit_can_move(bl)))
		return 0;

	// Cell PVP [Napster]
	if (bl->type == BL_PC)
	{
		struct map_session_data* sd = (TBL_PC*)bl;
		int64 tick = gettick();

		if (sd && sd->pvpcan_walkout_tick && !map_getcell( sd->bl.m, x, y, CELL_CHKPVP ) ) {
			if ( DIFF_TICK(tick, sd->pvpcan_walkout_tick) < battle_config.cellpvp_walkout_delay )
			{
				int64 e_tick = (battle_config.cellpvp_walkout_delay - DIFF_TICK( tick, sd->pvpcan_walkout_tick))/1000;
				char e_msg[150];
				if( e_tick > 99 )
						sprintf(e_msg, msg_txt(sd,800), sd->status.name, (double)e_tick / 60); 
				else
						sprintf(e_msg, msg_txt(sd,801), sd->status.name, e_tick+1);

				clif_disp_overheadcolor_self(sd->fd,COLOR_WHITE, e_msg);
				return 0;
			}
		}
	}
	
	ud->state.walk_easy = flag&1;
	ud->to_x = x;
	ud->to_y = y;
	unit_stop_attack(bl); //Sets target to 0
	
	sc = status_get_sc(bl);
	if (sc && (sc->data[SC_CONFUSION] || sc->data[SC_CONFUSION]) ) //Randomize the target position
		map_random_dir(bl, &ud->to_x, &ud->to_y);

	if(ud->walktimer != INVALID_TIMER) {
		// When you come to the center of the grid because the change of destination while you're walking right now
		// Call a function from a timer unit_walktoxy_sub
		ud->state.change_walk_target = 1;
		return 1;
	}

	// Start timer to recall summon
	if (sd && sd->md) unit_check_start_teleport_timer(&sd->md->bl);
	if (sd && sd->hd) unit_check_start_teleport_timer(&sd->hd->bl);
	if (sd && sd->pd) unit_check_start_teleport_timer(&sd->pd->bl);

	return unit_walktoxy_sub(bl);
}

/*==========================================
 * Sets a mob's CHASE/FOLLOW state \n
 * This should not be done if there's no path to reach
 * @param bl: Mob to set state on
 * @param flag: Whether to set state or not
 *------------------------------------------*/
static inline void set_mobstate(struct block_list* bl, int flag)
{
	struct mob_data* md = BL_CAST(BL_MOB,bl);

	if( md && flag )
		md->state.skillstate = md->state.aggressive ? MSS_FOLLOW : MSS_RUSH;
}

/*==========================================
 * Timer to walking a unit to another unit's location \n
 * Calls unit_walktoxy_sub once determined the unit can move
 * @param tid: Object's timer ID
 * @param id: Object's ID
 * @param data: Data passed through timer function (target)
 * @return 0
 *------------------------------------------*/
static int unit_walktobl_sub(int tid, int64 tick, int id, intptr_t data)
{
	struct block_list *bl = map_id2bl(id);
	struct unit_data *ud = bl?unit_bl2ud(bl):NULL;

	if (ud && ud->walktimer == INVALID_TIMER && ud->target == data)
	{
		if (DIFF_TICK(ud->canmove_tick, tick) > 0) //Keep waiting?
			add_timer(ud->canmove_tick+1, unit_walktobl_sub, id, data);
		else if (unit_can_move(bl))
		{
			if (unit_walktoxy_sub(bl))
				set_mobstate(bl, ud->state.attack_continue);
		}
	}
	return 0;	
}

/*==========================================
 * Tells a unit to walk to a target's location (chase)
 * @param bl: Object that is walking to target
 * @param tbl: Target object
 * @param range: How close to get to target (or attack range if flag&2)
 * @param flag: Extra behaviour \n
 *	&1: Use easy path seek (obstacles will not be walked around)
 *	&2: Start attacking upon arrival within range, otherwise just walk to target
 * @return 1: Started walking or set timer 0: Failed
 *------------------------------------------*/
int unit_walktobl(struct block_list *bl, struct block_list *tbl, int range, int flag)
{
	struct unit_data        *ud = NULL;
	struct status_change		*sc = NULL;
	nullpo_ret(bl);
	nullpo_ret(tbl);
	
	ud = unit_bl2ud(bl);
	if( ud == NULL) return 0;

	if (!(status_get_mode(bl)&MD_CANMOVE))
		return 0;
	
	if (!unit_can_reach_bl(bl, tbl, distance_bl(bl, tbl)+1, flag&1, &ud->to_x, &ud->to_y)) {
		ud->to_x = bl->x;
		ud->to_y = bl->y;
		ud->target_to = 0;

		return 0;
	}
	else if (range == 0) {
		//Should walk on the same cell as target (for looters)
		ud->to_x = tbl->x;
		ud->to_y = tbl->y;
	}

	ud->state.walk_easy = flag&1;
	ud->target_to = tbl->id;
	ud->chaserange = range; //Note that if flag&2, this SHOULD be attack-range
	ud->state.attack_continue = flag&2?1:0; //Chase to attack.
	unit_stop_attack(bl); //Sets target to 0

	sc = status_get_sc(bl);
	if (sc && sc->data[SC_CONFUSION] ) //Randomize the target position
		map_random_dir(bl, &ud->to_x, &ud->to_y);

	if(ud->walktimer != INVALID_TIMER) {
		ud->state.change_walk_target = 1;
		set_mobstate(bl, flag&2);
		return 1;
	}

	if(DIFF_TICK(ud->canmove_tick, gettick()) > 0)
	{	//Can't move, wait a bit before invoking the movement.
		add_timer(ud->canmove_tick+1, unit_walktobl_sub, bl->id, ud->target);
		return 1;
	}

	if(!unit_can_move(bl))
		return 0;

	if (unit_walktoxy_sub(bl)) {
		set_mobstate(bl, flag&2);
		return 1;
	}
	return 0;
}
#undef set_mobstate

/*==========================================
 * Set a unit to run, checking for obstacles
 * @param bl: Object that is running
 * @return 1: Success 0: Fail
 *------------------------------------------*/
int unit_run(struct block_list *bl)
{
	struct status_change *sc = status_get_sc(bl);
	short to_x,to_y,dir_x,dir_y;
	int lv;
	int i;

	if (!(sc && sc->data[SC_RUN]))
		return 0;
	
	if (!unit_can_move(bl)) {
		status_change_end(bl, SC_RUN, INVALID_TIMER);
		return 0;
	}
	
	lv = sc->data[SC_RUN]->val1;
	dir_x = dirx[sc->data[SC_RUN]->val2];
	dir_y = diry[sc->data[SC_RUN]->val2];

	// determine destination cell
	to_x = bl->x;
	to_y = bl->y;
	for(i=0;i<AREA_SIZE;i++)
	{
		if(!map_getcell(bl->m,to_x+dir_x,to_y+dir_y,CELL_CHKPASS))
			break;

		//if sprinting and there's a PC/Mob/NPC, block the path [Kevin]
		if(sc->data[SC_RUN] && map_count_oncell(bl->m, to_x+dir_x, to_y+dir_y, BL_PC|BL_MOB|BL_NPC,0))
			break;
			
		to_x += dir_x;
		to_y += dir_y;
	}

	if ((to_x == bl->x && to_y == bl->y) || (to_x == (bl->x + 1) || to_y == (bl->y + 1)) || (to_x == (bl->x - 1) || to_y == (bl->y - 1))) {
		//If you can't run forward, you must be next to a wall, so bounce back. [Skotlex]
		clif_status_change(bl, SI_BUMP, 1, 0, 0, 0, 0);

		//Set running to 0 beforehand so status_change_end knows not to enable spurt [Kevin]
		unit_bl2ud(bl)->state.running = 0;
		status_change_end(bl, SC_RUN, INVALID_TIMER);

		skill_blown(bl,bl,skill_get_blewcount(TK_RUN,lv),unit_getdir(bl),0);
		clif_status_change(bl, SI_BUMP, 0, 0, 0, 0, 0);
		return 0;
	}
	if (unit_walktoxy(bl, to_x, to_y, 1))
		return 1;
	//There must be an obstacle nearby. Attempt walking one cell at a time.
	do {
		to_x -= dir_x;
		to_y -= dir_y;
	} while (--i > 0 && !unit_walktoxy(bl, to_x, to_y, 1));
	if (i==0) {
		// copy-paste from above
		clif_status_change(bl, SI_BUMP, 1, 0, 0, 0, 0);

		//Set running to 0 beforehand so status_change_end knows not to enable spurt [Kevin]
		unit_bl2ud(bl)->state.running = 0;
		status_change_end(bl, SC_RUN, INVALID_TIMER);

		skill_blown(bl,bl,skill_get_blewcount(TK_RUN,lv),unit_getdir(bl),0);
		clif_status_change(bl, SI_BUMP, 0, 0, 0, 0, 0);
		return 0;
	}
	return 1;
}

/*==========================================
 * Char movement with wugdash
 * @author [Jobbie/3CeAM]
 * @param bl: Object that is dashing
 * @param sd: Player
 * @return 1: Success 0: Fail
 *------------------------------------------*/
int unit_wugdash(struct block_list *bl, struct map_session_data *sd)
{
	struct status_change *sc = status_get_sc(bl);
	short to_x,to_y,dir_x,dir_y;
	int lv;
	int i;

	if (!(sc && sc->data[SC_WUGDASH]))
		return 0;

	nullpo_retr(0, sd);
	nullpo_retr(0, bl);

	if (!unit_can_move(bl)) {
		status_change_end(bl,SC_WUGDASH,-1);
		return 0;
	}

	lv = sc->data[SC_WUGDASH]->val1;
	dir_x = dirx[sc->data[SC_WUGDASH]->val2];
	dir_y = diry[sc->data[SC_WUGDASH]->val2];
	to_x = bl->x;
	to_y = bl->y;
	for(i=0;i<AREA_SIZE;i++)
	{
		if(!map_getcell(bl->m,to_x+dir_x,to_y+dir_y,CELL_CHKPASS))
			break;
		if(sc->data[SC_WUGDASH] && map_count_oncell(bl->m, to_x+dir_x, to_y+dir_y, BL_PC|BL_MOB|BL_NPC, 0))
			break;
		to_x += dir_x;
		to_y += dir_y;
	}

	if(to_x == bl->x && to_y == bl->y) {
		unit_bl2ud(bl)->state.running = 0;
		status_change_end(bl,SC_WUGDASH,-1);
		if( sd ){
			clif_fixpos(bl);
			skill_castend_damage_id(bl, &sd->bl, RA_WUGDASH, lv, gettick(), SD_LEVEL);
		}
		return 0;
	}

	if (unit_walktoxy(bl, to_x, to_y, 1))
		return 1;
	do {
		to_x -= dir_x;
		to_y -= dir_y;
	} while (--i > 0 && !unit_walktoxy(bl, to_x, to_y, 1));
	if (i==0) {
		unit_bl2ud(bl)->state.running = 0;
		status_change_end(bl,SC_WUGDASH,-1);
		if( sd ){
			clif_fixpos(bl);
			skill_castend_damage_id(bl, &sd->bl, RA_WUGDASH, lv, gettick(), SD_LEVEL);
		}
		return 0;
	}
	return 1;
}

/*==========================================
 * Makes unit attempt to run away from target using hard paths
 * @param bl: Object that is running away from target
 * @param target: Target
 * @param dist: How far bl should run
 * @return 1: Success 0: Fail
 *------------------------------------------*/
int unit_escape(struct block_list *bl, struct block_list *target, short dist)
{
	int dir = map_calc_dir(target, bl->x, bl->y);
	while( dist > 0 && map_getcell(bl->m, bl->x + dist*dirx[dir], bl->y + dist*diry[dir], CELL_CHKNOREACH) )
		dist--;
	return ( dist > 0 && unit_walktoxy(bl, bl->x + dist*dirx[dir], bl->y + dist*diry[dir], 0) );
}

/*==========================================
 * Instant warps a unit to x,y coordinate
 * @param bl: Object to instant warp
 * @param dst_x: X coordinate to warp to
 * @param dst_y: Y coordinate to warp to
 * @param easy:
 *		0: Hard path check (attempt to go around obstacle)
 *		1: Easy path check (no obstacle on movement path)
 *		2: Long path check (no obstacle on line from start to destination)
 * @param checkpath: Whether or not to do a cell and path check for NOPASS and NOREACH
 * @return true: Success false: Fail
 *------------------------------------------*/
bool unit_movepos(struct block_list *bl, short dst_x, short dst_y, int easy, bool checkpath)
{
	short dx,dy;
	uint8 dir;
	struct unit_data        *ud = NULL;
	struct map_session_data *sd = NULL;

	nullpo_retr(false, bl);
	sd = BL_CAST(BL_PC, bl);
	ud = unit_bl2ud(bl);

	if( ud == NULL) return false;

	unit_stop_walking(bl,1);
	unit_stop_attack(bl);

	if( checkpath && (map_getcell(bl->m,dst_x,dst_y,CELL_CHKNOPASS) || !path_search(NULL,bl->m,bl->x,bl->y,dst_x,dst_y,easy,CELL_CHKNOREACH)) )
		return false; // unreachable

	ud->to_x = dst_x;
	ud->to_y = dst_y;

	dir = map_calc_dir(bl, dst_x, dst_y);
	ud->dir = dir;

	dx = dst_x - bl->x;
	dy = dst_y - bl->y;

	map_foreachinmovearea(clif_outsight, bl, AREA_SIZE, dx, dy, sd?BL_ALL:BL_PC, bl);

	map_moveblock(bl, dst_x, dst_y, gettick());
	
	ud->walktimer = -2; // arbitrary non-INVALID_TIMER value to make the clif code send walking packets
	map_foreachinmovearea(clif_insight, bl, AREA_SIZE, -dx, -dy, sd?BL_ALL:BL_PC, bl);
	ud->walktimer = INVALID_TIMER;
		
	if(sd) {
		if( sd->touching_id )
			npc_touchnext_areanpc(sd,false);
		if(map_getcell(bl->m,bl->x,bl->y,CELL_CHKNPC)) {
			npc_touch_areanpc(sd,bl->m,bl->x,bl->y);
			if (bl->prev == NULL) //Script could have warped char, abort remaining of the function.
				return false;
		} else
			sd->areanpc_id=0;

		// Cell PVP [Ize]
		if( !sd->state.pvp && map_getcell( sd->bl.m, sd->bl.x, sd->bl.y, CELL_CHKPVP) )
			map_pvp_area(sd, 1);
		else if( sd->state.pvp && !map_getcell( sd->bl.m, sd->bl.x, sd->bl.y, CELL_CHKPVP) )
			map_pvp_area(sd, 0);

		if( sd->status.pet_id > 0 && sd->pd && sd->pd->pet.intimate > 0 )
		{ // Check if pet needs to be teleported. [Skotlex]
			int flag = 0;
			struct block_list* bl = &sd->pd->bl;
			if( !checkpath && !path_search(NULL,bl->m,bl->x,bl->y,dst_x,dst_y,0,CELL_CHKNOPASS) )
				flag = 1;
			else if (!check_distance_bl(&sd->bl, bl, AREA_SIZE)) //Too far, teleport.
				flag = 2;
			if( flag )
			{
				unit_movepos(bl,sd->bl.x,sd->bl.y, 0, 0);
				clif_slide(bl,bl->x,bl->y);
			}
		}
	}
	return true;
}

/*==========================================
 * Sets direction of a unit
 * @param bl: Object to set direction
 * @param dir: Direction (0-7)
 * @return 0
 *------------------------------------------*/
int unit_setdir(struct block_list *bl,unsigned char dir)
{
	struct unit_data *ud;
	nullpo_ret(bl );
	ud = unit_bl2ud(bl);
	if (!ud) return 0;
	ud->dir = dir;
	if (bl->type == BL_PC) 
		((TBL_PC *)bl)->head_dir = 0;
	clif_changed_dir(bl, AREA);
	return 0;
}

/*==========================================
 * Gets direction of a unit
 * @param bl: Object to get direction
 * @return direction (0-7)
 *------------------------------------------*/
uint8 unit_getdir(struct block_list *bl)
{
	struct unit_data *ud;
	nullpo_ret(bl );
	ud = unit_bl2ud(bl);
	if (!ud) return 0;
	return ud->dir;
}

/*==========================================
 * Pushes a unit in a direction by a given amount of cells
 * There is no path check, only map cell restrictions are respected
 * @param bl: Object to push
 * @param dx: Destination cell X
 * @param dy: Destination cell Y
 * @param count: How many cells to push bl
 * @param flag: Whether or not to send position packet updates
 * @return count (can be modified due to map cell restrictions)
 *------------------------------------------*/
int unit_blown(struct block_list* bl, int dx, int dy, int count, int flag)
{
	if(count)
	{
		int nx, ny, result;
		struct map_session_data* sd;
		struct skill_unit* su = NULL;

		sd = BL_CAST(BL_PC, bl);
		su = BL_CAST(BL_SKILL, bl);

		result = path_blownpos(bl->m, bl->x, bl->y, dx, dy, count);

		nx = result>>16;
		ny = result&0xffff;

		if(!su)
		{
			unit_stop_walking(bl, 0);
		}

		if (sd) {
			unit_stop_stepaction(bl); //Stop stepaction when knocked back
			sd->ud.to_x = nx;
			sd->ud.to_y = ny;
		}

		dx = nx-bl->x;
		dy = ny-bl->y;

		if(dx || dy)
		{
			map_foreachinmovearea(clif_outsight, bl, AREA_SIZE, dx, dy, bl->type == BL_PC ? BL_ALL : BL_PC, bl);

			if(su)
			{
				skill_unit_move_unit_group(su->group, bl->m, dx, dy);
			}
			else
			{
				map_moveblock(bl, nx, ny, gettick());
			}

			map_foreachinmovearea(clif_insight, bl, AREA_SIZE, -dx, -dy, bl->type == BL_PC ? BL_ALL : BL_PC, bl);

			if(!(flag&1))
			{
				clif_blown(bl);
			}

			if(sd)
			{
				if(sd->touching_id)
				{
					npc_touchnext_areanpc(sd, false);
				}
				if(map_getcell(bl->m, bl->x, bl->y, CELL_CHKNPC))
				{
					npc_touch_areanpc(sd, bl->m, bl->x, bl->y);
				}
				else
				{
					sd->areanpc_id = 0;
				}
			}
		}

		count = distance(dx, dy);
	}

	return count;  // return amount of knocked back cells
}

/**
 * Checks if unit can be knocked back / stopped by skills.
 * @param bl: Object to check
 * @param flag
 *		0x1 - Offensive (not set: self skill, e.g. Backslide)
 *		0x2 - Knockback type (not set: Stop type, e.g. Ankle Snare)
 *      0x4 - Boss attack
 * @return reason for immunity
 *		0 - can be knocked back / stopped
 *		1 - at WOE/BG map
 *		2 - target is MD_KNOCKBACK_IMMUNE
 *		3 - target is in Basilica area
 *		4 - target has 'special_state.no_knockback'
 *		5 - target is trap that cannot be knocked back
 */
uint8 unit_blown_immune(struct block_list* bl, uint8 flag)
{
	if ((flag & 0x1) && (map_flag_gvg2(bl->m) || map[bl->m].flag.battleground)
		&& ((flag & 0x2) || !(battle_config.skill_trap_type & 0x1)))
		return 1; // No knocking back in WoE / BG

	switch (bl->type) {
	case BL_MOB: {
		struct mob_data* md = BL_CAST(BL_MOB, bl);
		// Immune can't be knocked back
		if (((flag & 0x1) && status_bl_has_mode(bl, MD_KNOCKBACK_IMMUNE))
			&& ((flag & 0x2) || !(battle_config.skill_trap_type & 0x2)))
			return 2;
	}
				 break;
	case BL_PC: {
		struct map_session_data *sd = BL_CAST(BL_PC, bl);
		// Basilica caster can't be knocked-back by normal monsters.
		if (!(flag & 0x4) && &sd->sc && sd->sc.data[SC_BASILICA] && sd->sc.data[SC_BASILICA]->val4 == sd->bl.id)
			return 3;
		// Target has special_state.no_knockback (equip)
		if ((flag&(0x1 | 0x2)) && sd->special_state.no_knockback)
			return 4;
	}
				break;
	case BL_SKILL: {
		struct skill_unit* su = (struct skill_unit *)bl;
		// Trap cannot be knocked back
		if (su && su->group && skill_get_unit_flag(su->group->skill_id)&UF_NOKNOCKBACK)
			return 5;
	}
				   break;
	}

	//Object can be knocked back / stopped
	return 0;
}

/*==========================================
 * Warps a unit to a map/position
 * pc_setpos is used for player warping
 * This function checks for "no warp" map flags, so it's safe to call without doing nowarpto/nowarp checks
 * @param bl: Object to warp
 * @param m: Map ID from bl structure (NOT index)
 * @param x: Destination cell X
 * @param y: Destination cell Y
 * @param type: Clear type used in clif_clearunit_area()
 * @return Success(0); Failed(1); Error(2); unit_remove_map() Failed(3); map_addblock Failed(4)
 *------------------------------------------*/
int unit_warp(struct block_list *bl,short m,short x,short y,clr_type type)
{
	struct unit_data *ud;
	nullpo_ret(bl);
	ud = unit_bl2ud(bl);
	
	if(bl->prev==NULL || !ud)
		return 1;

	if (type == CLR_DEAD)
		//Type 1 is invalid, since you shouldn't warp a bl with the "death" 
		//animation, it messes up with unit_remove_map! [Skotlex]
		return 1;
	
	if( m<0 ) m=bl->m;
	
	switch (bl->type) {
		case BL_MOB:
			if (map[bl->m].flag.monster_noteleport)
				return 1;
			if (m != bl->m && map[m].flag.nobranch && battle_config.mob_warp&4 && !(((TBL_MOB *)bl)->master_id))
				return 1;
			break;
		case BL_PC:
			if (map[bl->m].flag.noteleport)
				return 1;
			break;
	}
	
	if (x<0 || y<0)
  	{	//Random map position.
		if (!map_search_freecell(NULL, m, &x, &y, -1, -1, 1)) {
			ShowWarning("unit_warp failed. Unit Id:%d/Type:%d, target position map %d (%s) at [%d,%d]\n", bl->id, bl->type, m, map[m].name, x, y);
			return 2;
			
		}
	} else if (map_getcell(m,x,y,CELL_CHKNOREACH))
	{	//Invalid target cell
		ShowWarning("unit_warp: Specified non-walkable target cell: %d (%s) at [%d,%d]\n", m, map[m].name, x,y);
		
		if (!map_search_freecell(NULL, m, &x, &y, 4, 4, 1))
	 	{	//Can't find a nearby cell
			ShowWarning("unit_warp failed. Unit Id:%d/Type:%d, target position map %d (%s) at [%d,%d]\n", bl->id, bl->type, m, map[m].name, x, y);
			return 2;
		}
	}

	if (bl->type == BL_PC) //Use pc_setpos
		return pc_setpos((TBL_PC*)bl, map_id2index(m), x, y, type);
	
	if (!unit_remove_map(bl, type))
		return 3;
	
	if (bl->m != m && battle_config.clear_unit_onwarp &&
		battle_config.clear_unit_onwarp&bl->type)
		skill_clear_unitgroup(bl);

	bl->x=ud->to_x=x;
	bl->y=ud->to_y=y;
	bl->m=m;

	if (map_addblock(bl))
		return 4; //error on adding bl to map

	clif_spawn(bl);
	skill_unit_move(bl,gettick(),1);

	return 0;
}

/*==========================================
* Stops a unit from walking
 * @param bl: Object to stop walking
 * @param type: Options
 *	&0x1: Issue a fixpos packet afterwards
 *	&0x2: Force the unit to move one cell if it hasn't yet
 *	&0x4: Enable moving to the next cell when unit was already half-way there
 *		(may cause on-touch/place side-effects, such as a scripted map change)
 *	&0x8: Force stop moving, even if walktimer is currently INVALID_TIMER
 * @return Success(1); Failed(0);
 *------------------------------------------*/
int unit_stop_walking(struct block_list *bl,int type)
{
	struct unit_data *ud;
	const struct TimerData* td;
	int64 tick;
	nullpo_ret(bl);

	ud = unit_bl2ud(bl);
	if (!ud || (!(type & 0x08) && ud->walktimer == INVALID_TIMER))
		return 0;
	//NOTE: We are using timer data after deleting it because we know the 
	//delete_timer function does not messes with it. If the function's 
	//behaviour changes in the future, this code could break!
	if (ud->walktimer != INVALID_TIMER) {
		td = get_timer(ud->walktimer);
		delete_timer(ud->walktimer, unit_walktoxy_timer);
		ud->walktimer = INVALID_TIMER;
	}
	ud->state.change_walk_target = 0;
	tick = gettick();
	if( (type&0x02 && !ud->walkpath.path_pos) //Force moving at least one cell.
	||  (type&0x04 && td && DIFF_TICK(td->tick, tick) <= td->data/2) //Enough time has passed to cover half-cell
	) {	
		ud->walkpath.path_len = ud->walkpath.path_pos+1;
		unit_walktoxy_timer(INVALID_TIMER, tick, bl->id, ud->walkpath.path_pos);
	}

	if(type&0x01)
		clif_fixpos(bl);
	
	ud->walkpath.path_len = 0;
	ud->walkpath.path_pos = 0;
	ud->to_x = bl->x;
	ud->to_y = bl->y;
	if(bl->type == BL_PET && type&~0xff)
		ud->canmove_tick = gettick() + (type>>8);

	//Readded, the check in unit_set_walkdelay means dmg during running won't fall through to this place in code [Kevin]
	if (ud->state.running)
	{
		status_change_end(bl, SC_RUN, INVALID_TIMER);
		status_change_end(bl, SC_WUGDASH, INVALID_TIMER);
	}
	return 1;
}

/*==========================================
 * Initiates a skill use by a unit
 * @param src: Source object initiating skill use
 * @param target_id: Target ID (bl->id)
 * @param skill_id: Skill ID
 * @param skill_lv: Skill Level
 * @return unit_skilluse_id2()
 *------------------------------------------*/
int unit_skilluse_id(struct block_list *src, int target_id, uint16 skill_id, uint16 skill_lv)
{
	// Cell PVP [Napster]
	struct block_list *bl = map_id2bl(target_id);	

	if( bl && src->type == BL_PC && bl->type == BL_PC && src->id != target_id)
	{
		struct map_session_data *dstsd = (TBL_PC*)bl;

		 if( dstsd && dstsd->state.pvp != ((TBL_PC*)src)->state.pvp )
			 return 0;
	}

	int casttime = skill_castfix(src, skill_id, skill_lv);
	int castcancel = skill_get_castcancel(skill_id);
	int ret = unit_skilluse_id2(src, target_id, skill_id, skill_lv, casttime, castcancel);
	struct map_session_data *sd = BL_CAST(BL_PC, src);

	if (sd != NULL)
		pc_itemskill_clear(sd);

	return ret;
}

/*==========================================
 * Checks if a unit is walking
 * @param bl: Object to check walk status
 * @return Walking(1); Not Walking(0)
 *------------------------------------------*/
int unit_is_walking(struct block_list *bl)
{
	struct unit_data *ud = unit_bl2ud(bl);
	nullpo_ret(bl);
	if(!ud) return 0;
	return (ud->walktimer != INVALID_TIMER);
}

/*==========================================
 * Checks if a unit is able to move based on status changes
 * Some statuses are still checked here due too specific variables
 * @author [Skotlex]
 * @param bl: Object to check
 * @return Can move(1); Can't move(0)
 *------------------------------------------*/
int unit_can_move(struct block_list *bl)
{
	struct map_session_data *sd;
	struct unit_data *ud;
	struct status_change *sc;
	
	nullpo_ret(bl);
	ud = unit_bl2ud(bl);
	sc = status_get_sc(bl);
	sd = BL_CAST(BL_PC, bl);
	
	if (!ud)
		return 0;
	
	if (ud->skilltimer != INVALID_TIMER && ud->skill_id != LG_EXEEDBREAK && (!sd || !pc_checkskill(sd, SA_FREECAST) || skill_get_inf2(ud->skill_id)&INF2_GUILD_SKILL))
		return 0; // prevent moving while casting
	
	if (DIFF_TICK(ud->canmove_tick, gettick()) > 0)
		return 0;
	
	if (sd && (
		pc_issit(sd) ||
		sd->state.vending ||
		sd->state.buyingstore ||
		(sd->state.block_action & PCBLOCK_MOVE) ||
		sd->state.blockedmove
	))
		return 0; //Can't move
	
	if (sc) {
		if (sc->opt1 > 0 && sc->opt1 != OPT1_STONEWAIT && sc->opt1 != OPT1_BURNING)
			return 0;

		if ((sc->option & OPTION_HIDE) && (!sd || pc_checkskill(sd, RG_TUNNELDRIVE) <= 0))
			return 0;

		if (sc->count && (
			sc->data[SC_ANKLE]
			|| sc->data[SC_AUTOCOUNTER]
			|| sc->data[SC_TRICKDEAD]
			|| sc->data[SC_BLADESTOP]
			|| sc->data[SC_BLADESTOP_WAIT]
			|| sc->data[SC_SPIDERWEB]
			|| (sc->data[SC_DANCING] && sc->data[SC_DANCING]->val4 && (
				!sc->data[SC_LONGING] ||
				(sc->data[SC_DANCING]->val1&0xFFFF) == CG_MOONLIT ||
				(sc->data[SC_DANCING]->val1&0xFFFF) == CG_HERMODE
			))
			|| (sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_SELF)	// cannot move while gospel is in effect
			|| (sc->data[SC_BASILICA] && sc->data[SC_BASILICA]->val4 == bl->id) // Basilica caster cannot move
			|| sc->data[SC_STOP]
			|| sc->data[SC_CLOSECONFINE]
			|| sc->data[SC_CLOSECONFINE2]
			|| (sc->data[SC_CLOAKING] && //Need wall at level 1-2
				sc->data[SC_CLOAKING]->val1 < 3 && !(sc->data[SC_CLOAKING]->val4&1))
			|| sc->data[SC_MADNESSCANCEL]
			|| (sc->data[SC_GRAVITATION] && sc->data[SC_GRAVITATION]->val3 == BCT_SELF)
			|| (sc->data[SC_FEAR] && sc->data[SC_FEAR]->val2 > 0)
			|| sc->data[SC_DEEPSLEEP]
			|| sc->data[SC_CRYSTALIZE]
			|| sc->data[SC_ELECTRICSHOCKER]
			|| sc->data[SC_WUGBITE]
			|| (sc->data[SC_CAMOUFLAGE] && //Must be against a wall on level 1 and 2. Cant move on level 1 and 2.
				sc->data[SC_CAMOUFLAGE]->val1 < 3 && !(sc->data[SC_CAMOUFLAGE]->val3 & 1))
			|| sc->data[SC_MAGNETICFIELD]
			|| sc->data[SC__MANHOLE]
			|| sc->data[SC_FALLENEMPIRE]
			|| sc->data[SC_CURSEDCIRCLE_ATKER]
			|| sc->data[SC_CURSEDCIRCLE_TARGET]
			|| sc->data[SC_NETHERWORLD]
			|| sc->data[SC_VACUUM_EXTREME]
			|| sc->data[SC_THORNS_TRAP]
			|| sc->data[SC_GRAVITYCONTROL]
			|| sc->data[SC_KG_KAGEHUMI]
			|| sc->data[SC_KINGS_GRACE]
			|| sc->data[SC_SUHIDE]
			|| sc->data[SC_SV_ROOTTWIST]
			|| sc->data[SC_NEEDLE_OF_PARALYZE]
			|| sc->data[SC_TINDER_BREAKER]
		))
			return 0;
	}

	// Icewall walk block special trapped monster mode
	if (bl->type == BL_MOB) {
		struct mob_data *md = BL_CAST(BL_MOB, bl);
		if (md && ((status_has_mode(&md->status, MD_STATUS_IMMUNE) && battle_config.boss_icewall_walk_block == 1 && map_getcell(bl->m, bl->x, bl->y, CELL_CHKICEWALL))
			|| (!status_has_mode(&md->status, MD_STATUS_IMMUNE) && battle_config.mob_icewall_walk_block == 1 && map_getcell(bl->m, bl->x, bl->y, CELL_CHKICEWALL)))) {
			md->walktoxy_fail_count = 1; //Make sure rudeattacked skills are invoked
			return 0;
		}
	}

	return 1;
}

/*==========================================
 * Resumes running (RA_WUGDASH or TK_RUN) after a walk delay
 * @param tid: Timer ID
 * @param id: Object ID
 * @param data: Data passed through timer function (unit_data)
 * @return 0
 *------------------------------------------*/
int unit_resume_running(int tid, int64 tick, int id, intptr_t data)
{

	struct unit_data *ud = (struct unit_data *)data;
	TBL_PC * sd = map_id2sd(id);

	if(sd && pc_iswugrider(sd))
		clif_skill_nodamage(ud->bl,ud->bl,RA_WUGDASH,ud->skill_lv,
			sc_start4(ud->bl,status_skill2sc(RA_WUGDASH),100,ud->skill_lv,unit_getdir(ud->bl),0,0,1));
	else
		clif_skill_nodamage(ud->bl,ud->bl,TK_RUN,ud->skill_lv,
			sc_start4(ud->bl,status_skill2sc(TK_RUN),100,ud->skill_lv,unit_getdir(ud->bl),0,0,0));

	if (sd) clif_walkok(sd);

	return 0;

}


/*==========================================
 * Applies walk delay to character, considering that 
 * if type is 0, this is a damage induced delay: if previous delay is active, do not change it.
 * if type is 1, this is a skill induced delay: walk-delay may only be increased, not decreased.
 *------------------------------------------*/
int unit_set_walkdelay(struct block_list *bl, int64 tick, int delay, int type)
{
	struct unit_data *ud = unit_bl2ud(bl);
	if (delay <= 0 || !ud) return 0;
	
	if (type) {
		//Bosses can ignore skill induced walkdelay (but not damage induced)
		if (bl->type == BL_MOB && status_has_mode(status_get_status_data(bl), MD_STATUS_IMMUNE))
			return 0;
		//Make sure walk delay is not decreased
		if (DIFF_TICK(ud->canmove_tick, tick+delay) > 0)
			return 0;
	} else {
		//Don't set walk delays when already trapped.
		if (!unit_can_move(bl)) {
			unit_stop_walking(bl, 4); //Unit might still be moving even though it can't move
			return 0;
		}
	}
	ud->canmove_tick = tick + delay;
	if (ud->walktimer != INVALID_TIMER)
	{	//Stop walking, if chasing, readjust timers.
		if (delay == 1)
		{	//Minimal delay (walk-delay) disabled. Just stop walking.
			unit_stop_walking(bl,4);
		} else {
			//Resume running after can move again [Kevin]
			if(ud->state.running)
			{
				add_timer(ud->canmove_tick, unit_resume_running, bl->id, (intptr_t)ud);
			}
			else
			{
				unit_stop_walking(bl,2|4);
				if(ud->target)
					add_timer(ud->canmove_tick+1, unit_walktobl_sub, bl->id, ud->target);
			}
		}
	}
	return 1;
}

/*==========================================
 * Performs checks for a unit using a skill and executes after cast time completion
 * @param src: Object using skill
 * @param target_id: Target ID (bl->id)
 * @param skill_id: Skill ID
 * @param skill_lv: Skill Level
 * @param casttime: Initial cast time before cast time reductions
 * @param castcancel: Whether or not the skill can be cancelled by interuption (hit)
 * @return Success(1); Fail(0);
 *------------------------------------------*/
int unit_skilluse_id2(struct block_list *src, int target_id, uint16 skill_id, uint16 skill_lv, int casttime, int castcancel)
{
	struct unit_data *ud;
	struct status_data *tstatus;
	struct status_change *sc, *tsc;
	struct map_session_data *sd = NULL;
	struct block_list * target = NULL;
	int64 tick = gettick();
	int combo = 0, range;

	nullpo_ret(src);
	if(status_isdead(src))
		return 0; // ����ł��Ȃ���

	sd = BL_CAST(BL_PC, src);
	ud = unit_bl2ud(src);

	if(ud == NULL) return 0;
	sc = status_get_sc(src);	
	if (sc && !sc->count)
		sc = NULL; //Unneeded

	//temp: used to signal combo-skills right now.
	if (sc && sc->data[SC_COMBO] && skill_is_combo(skill_id) && (sc->data[SC_COMBO]->val1 == skill_id || (sd ? skill_check_condition_castbegin(sd, skill_id, skill_lv) : 0))) {
		if (skill_is_combo(skill_id) == 2 && target_id == src->id && ud->target > 0)
			target_id = ud->target;
		else if (sc->data[SC_COMBO]->val2)
			target_id = sc->data[SC_COMBO]->val2;
		else if (target_id == src->id || ud->target > 0)
			target_id = ud->target;

		if (skill_get_inf(skill_id)&INF_SELF_SKILL && skill_get_nk(skill_id)&NK_NO_DAMAGE)// exploit fix
			target_id = src->id;
		combo = 1;
	}
	else if (target_id == src->id &&
		skill_get_inf(skill_id)&INF_SELF_SKILL &&
		skill_get_inf2(skill_id)&INF2_NO_TARGET_SELF ||
	skill_id == SR_DRAGONCOMBO)
	{
		target_id = ud->target; //Auto-select target. [Skotlex]
		combo = 1;
	}

	if (sd) {
		//Target_id checking.
		if(skillnotok(skill_id, sd)) // [MouseJstr]
			return 0;

		switch(skill_id)
		{	//Check for skills that auto-select target
		case MO_CHAINCOMBO:
			if (sc && sc->data[SC_BLADESTOP]){
				if ((target=map_id2bl(sc->data[SC_BLADESTOP]->val4)) == NULL)
					return 0;
			}
			break;
		case RL_QD_SHOT:
			if (sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == skill_id)
				target_id = sc->data[SC_COMBO]->val2;
			break;
		case WE_MALE:
		case WE_FEMALE:
			if (!sd->status.partner_id)
				return 0;
			target = (struct block_list*)map_charid2sd(sd->status.partner_id);
			if (!target) {
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				return 0;
			}
			break;
		case GC_WEAPONCRUSH:
			if( sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == GC_WEAPONBLOCKING ){
				if ((target=map_id2bl(sc->data[SC_COMBO]->val2)) == NULL){
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_GC_WEAPONBLOCKING,0,0);
					return 0;
				}
			}else{
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_GC_WEAPONBLOCKING,0,0);
				return 0;
			}
			break;
		}
		if (target)
			target_id = target->id;
	}
	else if (src->type==BL_HOM)
	switch(skill_id)
	{ //Homun-auto-target skills.
		case HLIF_HEAL:
		case HLIF_AVOID:
		case HAMI_DEFENCE:
		case HAMI_CASTLE:
		case MH_LIGHT_OF_REGENE:
		case MH_OVERED_BOOST:
		case MH_STEINWAND:
		case MH_GRANITIC_ARMOR:
		case MH_PYROCLASTIC:
			target = battle_get_master(src);
			if (!target) return 0;
			target_id = target->id;
			break;
		case MH_SONIC_CRAW:
			if (sc && sc->data[SC_MIDNIGHT_FRENZY_POSTDELAY])
				target_id = sc->data[SC_MIDNIGHT_FRENZY_POSTDELAY]->val2;
			break;
	}

	if( !target ) // choose default target
		target = map_id2bl(target_id);

	if( !target || src->m != target->m || !src->prev || !target->prev )
		return 0;

	if( mob_ksprotected(src, target) )
		return 0;

	tsc = status_get_sc(target);

	if( tsc && tsc->data[SC__MANHOLE] )
		return 0;

	if(ud->skilltimer != -1 && skill_id != SA_CASTCANCEL &&
	!(skill_id == SO_SPELLFIST && (ud->skill_id == MG_FIREBOLT || ud->skill_id == MG_COLDBOLT || ud->skill_id == MG_LIGHTNINGBOLT)) )
 		return 0;

	if(skill_get_inf2(skill_id)&INF2_NO_TARGET_SELF && src->id == target_id)
		return 0;

	if(!status_check_skilluse(src, target, skill_id, skill_lv, 0))
		return 0;

	tstatus = status_get_status_data(target);
	//���O�̃X�L���󋵂̋L�^
	if(sd) {
		if( skill_get_inf2(skill_id)&INF2_CHORUS_SKILL )
		{
			if( skill_check_pc_partner(sd,skill_id,&skill_lv,skill_get_splash(skill_id,skill_lv),0) < 1 )
			{
				clif_skill_fail(sd,skill_id, USESKILL_FAIL, 0,0);
				return 0;
			}
		}
		else
			switch(skill_id){
				case SA_CASTCANCEL:
				case SO_SPELLFIST:
					if(ud->skill_id != skill_id){
						sd->skillid_old = ud->skill_id;
						sd->skilllv_old = ud->skill_lv;
					}
					break;
				case BD_ENCORE:
					//Prevent using the dance skill if you no longer have the skill in your tree. 
					if(!sd->skillid_dance || pc_checkskill(sd,sd->skillid_dance)<=0){
						clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
						return 0;
					}
					sd->skillid_old = skill_id;
					break;
				case CR_DEVOTION:
					if (target->type == BL_PC) {
						uint8 i = 0, count = min(skill_lv, 5);
						ARR_FIND(0, count, i, sd->devotion[i] == target_id);
						if (i == count) {
							ARR_FIND(0, count, i, sd->devotion[i] == 0);
							if (i == count) { // No free slots, skill Fail
								clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
								return 0;
							}
						}
					}
					break;
			}
		if (!skill_check_condition_castbegin(sd, skill_id, skill_lv))
			return 0;
	}

	if( src->type == BL_MOB )
		switch( skill_id )
		{
			case NPC_SUMMONSLAVE:
			case NPC_SUMMONMONSTER:
			case NPC_DEATHSUMMON:
			case AL_TELEPORT:
				if( ((TBL_MOB*)src)->master_id && ((TBL_MOB*)src)->special_state.ai )
					return 0;
		}

	if (src->type == BL_NPC) // NPC-objects can override cast distance
		range = AREA_SIZE; // Maximum visible distance before NPC goes out of sight
	else
		range = skill_get_range2(src, skill_id, skill_lv, true); // Skill cast distance from database

	// New action request received, delete previous action request if not executed yet
	if (ud->stepaction || ud->steptimer != INVALID_TIMER)
		unit_stop_stepaction(src);

	// Remember the skill request from the client while walking to the next cell
	if (src->type == BL_PC && ud->walktimer != INVALID_TIMER && !battle_check_range(src, target, range - 1)) {
		ud->stepaction = true;
		ud->target_to = target_id;
		ud->stepskill_id = skill_id;
		ud->stepskill_lv = skill_lv;
		return 0; // Attacking will be handled by unit_walktoxy_timer in this case
	}

	//Check range when not using skill on yourself or is a combo-skill during attack
	//(these are supposed to always have the same range as your attack)
	if( src->id != target_id && (!combo || ud->attacktimer == INVALID_TIMER) )
	{
		if( skill_get_state(ud->skill_id) == ST_MOVE_ENABLE )
		{
			if( !unit_can_reach_bl(src, target, range + 1, 1, NULL, NULL) )
				return 0; // Walk-path check failed.
		}
		else if( src->type == BL_MER && skill_id == MA_REMOVETRAP )
		{
			if( !battle_check_range(battle_get_master(src), target, range + 1) )
				return 0; // Aegis calc remove trap based on Master position, ignoring mercenary O.O
		}
		else if( !battle_check_range(src, target, range) )
			return 0; // Arrow-path check failed.
	}

	if (!combo && skill_id != SR_DRAGONCOMBO) //Stop attack on non-combo skills [Skotlex]
		unit_stop_attack(src);
	else if(ud->attacktimer != INVALID_TIMER) //Elsewise, delay current attack sequence
		ud->attackabletime = tick + status_get_adelay(src);
	
	ud->state.skillcastcancel = castcancel;

	//temp: Used to signal force cast now.
	combo = 0;
	
	switch(skill_id){
	case ALL_RESURRECTION:
		if(battle_check_undead(tstatus->race,tstatus->def_ele)) {	
			combo = 1;
		} else if (!status_isdead(target))
			return 0; //Can't cast on non-dead characters.
	break;
	case MO_FINGEROFFENSIVE:
		if(sd)
			casttime += casttime * min(skill_lv, sd->spiritball);
	break;
	case MO_EXTREMITYFIST:
		if (sc && sc->data[SC_COMBO] &&
		   (sc->data[SC_COMBO]->val1 == MO_COMBOFINISH ||
			sc->data[SC_COMBO]->val1 == CH_TIGERFIST ||
			sc->data[SC_COMBO]->val1 == CH_CHAINCRUSH))
			casttime = -1;
		combo = 1;
	break;
	case SA_SPELLBREAKER:
		combo = 1;
	break;
	case ST_CHASEWALK:
		if (sc && sc->data[SC_CHASEWALK])
			casttime = -1;
	break;
	case TK_RUN:
		if (sc && sc->data[SC_RUN])
			casttime = -1;
	break;
	case HP_BASILICA:
		if( sc && sc->data[SC_BASILICA] )
			casttime = -1; // No Casting time on basilica cancel
	break;
	case KN_CHARGEATK:
		{
		unsigned int k = (distance_bl(src,target)-1)/3; //+100% every 3 cells of distance
		if( k > 2 ) k = 2; // ...but hard-limited to 300%.
		casttime += casttime * k; 
		}
	break;	
	case GD_EMERGENCYCALL: //Emergency Call double cast when the user has learned Leap [Daegaladh]
		if (sd && (pc_checkskill(sd, TK_HIGHJUMP) || pc_checkskill(sd, SU_LOPE) >= 3))
			casttime *= 2;
		break;
	case RK_ENCHANTBLADE:
		if( battle_check_target(src,target,BCT_ENEMY)>0 )
			return 0;
		break;
	case RA_WUGDASH:
		if (sc && sc->data[SC_WUGDASH])
			casttime = -1;
		break;
	case SR_TIGERCANNON:
	case SR_GATEOFHELL:
		if (sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == SR_FALLENEMPIRE)
			casttime = -1;
		combo = 1;
	break;
	}
  	
	// force to use the random skill effect from magic mushroom. [Jobbie]
	if( sc && sc->data[SC_MAGICMUSHROOM] )
	{
		if( sd && sd->state.magicmushroom_flag && sd->skillitem == skill_id )
			casttime = 0;
	}

	// moved here to prevent Suffragium from ending if skill fails
	//if (!(skill_get_castnodex(skill_id, skill_lv)&2))
	//	casttime = skill_castfix_sc(src, casttime);

	if (src->type == BL_NPC) { // NPC-objects do not have cast time
		casttime = 0;
	}

	if (!ud->state.running) //need TK_RUN or WUGDASH handler to be done before that.
		unit_stop_walking(src, 1);// eventhough this is not how official works but this will do the trick.

	// SC_MAGICPOWER needs to switch states at start of cast
	skill_toggle_magicpower(src, skill_id);

	if (sd != NULL && sd->state.itemskill_no_casttime == 1 && skill_is_item_skill(sd, skill_id, skill_lv))
		casttime = 0;

	// in official this is triggered even if no cast time.
	clif_skillcasting(src, src->id, target_id, 0, 0, skill_id, skill_get_ele(skill_id, skill_lv), casttime);

	if (sd && target->type == BL_MOB) {
		TBL_MOB *md = (TBL_MOB*)target;

		mobskill_event(md, src, tick, -1); // Cast targetted skill event.
		if (tstatus->mode&(MD_CASTSENSOR_IDLE | MD_CASTSENSOR_CHASE) && battle_check_target(target, src, BCT_ENEMY) > 0) {
			switch (md->state.skillstate) {
			case MSS_RUSH:
			case MSS_FOLLOW:
				if (!(tstatus->mode&MD_CASTSENSOR_CHASE))
					break;
				md->target_id = src->id;
				md->state.aggressive = (tstatus->mode&MD_ANGRY) ? 1 : 0;
				md->min_chase = md->db->range3;
				break;
			case MSS_IDLE:
			case MSS_WALK:
				if (!(tstatus->mode&MD_CASTSENSOR_IDLE))
					break;
				md->target_id = src->id;
				md->state.aggressive = (tstatus->mode&MD_ANGRY) ? 1 : 0;
				md->min_chase = md->db->range3;
				break;
			}
		}
	}

	// force to use the random skill effect from magic mushroom. [Jobbie]
	if( sc && sc->data[SC_MAGICMUSHROOM] )
		casttime = 0;


	if( casttime <= 0 )
		ud->state.skillcastcancel = 0;

	if( !sd || sd->skillitem != skill_id || skill_get_cast(skill_id,skill_lv) )
		ud->canact_tick = tick + max(casttime, max(status_get_amotion(src), battle_config.min_skill_delay_limit)) + SECURITY_CASTTIME;
	if( sd )
	{
		switch( skill_id )
		{
		case CG_ARROWVULCAN:
			sd->canequip_tick = tick + casttime;
			break;
		}
	}
	ud->skilltarget  = target_id;
	ud->skillx       = 0;
	ud->skilly       = 0;
	ud->skill_id      = skill_id;
	ud->skill_lv      = skill_lv;

 	if( sc && sc->data[SC_CLOAKING] && !(sc->data[SC_CLOAKING]->val4&4) && skill_id != AS_CLOAKING )
	{ // Need confirm if Cloaking Exceed ends it.
		status_change_end(src, SC_CLOAKING, INVALID_TIMER);
		if (!src->prev) return 0; //Warped away!
	}

	if( sc && sc->data[SC_CLOAKINGEXCEED] && !(sc->data[SC_CLOAKINGEXCEED]->val4&4) && skill_id != GC_CLOAKINGEXCEED )
	{ // Need confirm if Cloaking ends it.
		status_change_end(src,SC_CLOAKINGEXCEED,-1);
		if (!src->prev) return 0;
	}

	if( sc && sc->data[SC_NEWMOON] && skill_id != SJ_NEWMOONKICK )
	{
		status_change_end(src, SC_NEWMOON, INVALID_TIMER);
		if (!src->prev) return 0; //Warped away!
	}

	if( sc && sc->data[SC__MANHOLE] )
	{
		status_change_end(src,SC__MANHOLE, INVALID_TIMER);
		if (!src->prev) return 0; //Warped away!
	}

	if( sc && sc->data[SC_CURSEDCIRCLE_ATKER] )
	{
		status_change_end(src, SC_CURSEDCIRCLE_ATKER, INVALID_TIMER);
		if (!src->prev) return 0; //Warped away!
	}

	if (casttime > 0)
	{
		ud->skilltimer = add_timer(tick + casttime, skill_castend_id, src->id, 0);
		if (sd && (pc_checkskill(sd, SA_FREECAST) > 0 || skill_id == LG_EXEEDBREAK))
			status_calc_bl(&sd->bl, SCB_SPEED);
	}
	else
		skill_castend_id(ud->skilltimer, tick, src->id, 0);


	if (sd && battle_config.prevent_logout_trigger&PLT_SKILL)
		sd->canlog_tick = gettick();

	return 1;
}

/*==========================================
 * Initiates a placement (ground/non-targeted) skill
 * @param src: Object using skill
 * @param skill_x: X coordinate where skill is being casted (center)
 * @param skill_y: Y coordinate where skill is being casted (center)
 * @param skill_id: Skill ID
 * @param skill_lv: Skill Level
 * @return unit_skilluse_pos2()
 *------------------------------------------*/
int unit_skilluse_pos(struct block_list *src, short skill_x, uint16 skill_y, uint16 skill_id, short skill_lv)
{
	int casttime = skill_castfix(src, skill_id, skill_lv);
	int castcancel = skill_get_castcancel(skill_id);
	int ret = unit_skilluse_pos2(src, skill_x, skill_y, skill_id, skill_lv, casttime, castcancel);
	struct map_session_data *sd = BL_CAST(BL_PC, src);

	if (sd != NULL)
		pc_itemskill_clear(sd);

	return ret;
}

/*==========================================
 * Performs checks for a unit using a skill and executes after cast time completion
 * @param src: Object using skill
 * @param skill_x: X coordinate where skill is being casted (center)
 * @param skill_y: Y coordinate where skill is being casted (center)
 * @param skill_id: Skill ID
 * @param skill_lv: Skill Level
 * @param casttime: Initial cast time before cast time reductions
 * @param castcancel: Whether or not the skill can be cancelled by interuption (hit)
 * @return Success(1); Fail(0);
 *------------------------------------------*/
int unit_skilluse_pos2( struct block_list *src, short skill_x, short skill_y, uint16 skill_id, uint16 skill_lv, int casttime, int castcancel)
{
	struct map_session_data *sd = NULL;
	struct unit_data        *ud = NULL;
	struct status_change *sc;
	struct block_list    bl;
	int64 tick = gettick();
	int range;

	nullpo_ret(src);

	if(!src->prev) return 0; // map ��ɑ��݂��邩
	if(status_isdead(src)) return 0;

	sd = BL_CAST(BL_PC, src);
	ud = unit_bl2ud(src);
	if(ud == NULL) return 0;

	if(ud->skilltimer != INVALID_TIMER) //Normally not needed since clif.c checks for it, but at/char/script commands don't! [Skotlex]
		return 0;
	
	sc = status_get_sc(src);
	if (sc && !sc->count)
		sc = NULL;
	
	if( sd )
	{
		if( skillnotok(skill_id, sd) || !skill_check_condition_castbegin(sd, skill_id, skill_lv) )
			return 0;
	}

	if (!status_check_skilluse(src, NULL, skill_id, skill_lv, 0))
		return 0;

	/* �˒��Ə�Q���`�F�b�N */
	bl.type = BL_NUL;
	bl.m = src->m;
	bl.x = skill_x;
	bl.y = skill_y;

	if (src->type == BL_NPC) // NPC-objects can override cast distance
		range = AREA_SIZE; // Maximum visible distance before NPC goes out of sight
	else
		range = skill_get_range2(src, skill_id, skill_lv, true); // Skill cast distance from database

	// New action request received, delete previous action request if not executed yet
	if (ud->stepaction || ud->steptimer != INVALID_TIMER)
		unit_stop_stepaction(src);

	// Remember the skill request from the client while walking to the next cell
	if (src->type == BL_PC && ud->walktimer != INVALID_TIMER && !battle_check_range(src, &bl, range - 1)) {
		struct map_data *md = &map[src->m];
		// Convert coordinates to target_to so we can use it as target later
		ud->stepaction = true;
		ud->target_to = (skill_x + skill_y * md->xs);
		ud->stepskill_id = skill_id;
		ud->stepskill_lv = skill_lv;
		return 0; // Attacking will be handled by unit_walktoxy_timer in this case
	}

	if( skill_get_state(ud->skill_id) == ST_MOVE_ENABLE )
	{
		if( !unit_can_reach_bl(src, &bl, range + 1, 1, NULL, NULL) )
			return 0; //Walk-path check failed.
	}
	else if( !battle_check_range(src, &bl, range) )
		return 0; //Arrow-path check failed.

	unit_stop_attack(src);

	// moved here to prevent Suffragium from ending if skill fails
	//if (!(skill_get_castnodex(skill_id, skill_lv)&2))
	//	casttime = skill_castfix_sc(src, casttime);

	ud->state.skillcastcancel = castcancel&&casttime>0?1:0;
	if( !sd || sd->skillitem != skill_id || skill_get_cast(skill_id,skill_lv) )
		ud->canact_tick = tick + max(casttime, max(status_get_amotion(src), battle_config.min_skill_delay_limit)) + SECURITY_CASTTIME;
//	if( sd )
//	{
//		switch( skill_id )
//		{
//		case ????:
//			sd->canequip_tick = tick + casttime;
//		}
//	}
	ud->skill_id      = skill_id;
	ud->skill_lv      = skill_lv;
	ud->skillx       = skill_x;
	ud->skilly       = skill_y;
	ud->skilltarget  = 0;

	if (sc && sc->data[SC_CLOAKING] && !(sc->data[SC_CLOAKING]->val4&4))
	{ // Need confirm if Cloaking ends it.
		status_change_end(src, SC_CLOAKING, INVALID_TIMER);
		if (!src->prev) return 0; //Warped away!
	}

	if (sc && sc->data[SC_CLOAKINGEXCEED] && !(sc->data[SC_CLOAKINGEXCEED]->val4&4))
	{ // Need confirm if Cloaking ends it.
		status_change_end(src,SC_CLOAKINGEXCEED, INVALID_TIMER);
		if (!src->prev) return 0; //Warped away!
	}

	if (sc && sc->data[SC_NEWMOON])
	{
		status_change_end(src, SC_NEWMOON, INVALID_TIMER);
		if (!src->prev) return 0; //Warped away!
	}

	unit_stop_walking(src, 1);

	// SC_MAGICPOWER needs to switch states at start of cast
	skill_toggle_magicpower(src, skill_id);

	if (sd != NULL && sd->state.itemskill_no_casttime == 1 && skill_is_item_skill(sd, skill_id, skill_lv))
		casttime = 0;

	// in official this is triggered even if no cast time.
	clif_skillcasting(src, src->id, 0, skill_x, skill_y, skill_id, skill_get_ele(skill_id, skill_lv), casttime);

	if( casttime > 0 )
	{
		// SC_MAGICPOWER needs to switch states at start of cast
		skill_toggle_magicpower(src, skill_id);

		ud->skilltimer = add_timer( tick+casttime, skill_castend_pos, src->id, 0 );
		if( (sd && pc_checkskill(sd,SA_FREECAST) > 0) || skill_id == LG_EXEEDBREAK )
			status_calc_bl(&sd->bl, SCB_SPEED);
	}
	else
	{
		ud->skilltimer = INVALID_TIMER;
		skill_castend_pos(ud->skilltimer,tick,src->id,0);
	}

	if (sd && battle_config.prevent_logout_trigger&PLT_SKILL)
		sd->canlog_tick = gettick();

	return 1;
}

/*========================================
 * update a block's attack target
 *----------------------------------------*/
int unit_set_target(struct unit_data* ud, int target_id)
{
	struct unit_data * ux;
	struct block_list* target;

	nullpo_ret(ud);

	if (ud->target != target_id) {
		if (ud->target && (target = map_id2bl(ud->target)) && (ux = unit_bl2ud(target)) && ux->target_count > 0)
			ux->target_count--;
		
		if (target_id && (target = map_id2bl(target_id)) && (ux = unit_bl2ud(target)) && ux->target_count < 255)
			ux->target_count++;
	}

	ud->target = target_id;
	return 0;
}

/*==========================================
 * Stop a unit's attacks
 * @param bl: Object to stop
 *------------------------------------------*/
void unit_stop_attack(struct block_list *bl)
{
	struct unit_data *ud;
	nullpo_retv(bl);
	ud = unit_bl2ud(bl);
	nullpo_retv(ud);

	//Clear target
	unit_set_target(ud, 0);

	if (ud->attacktimer == INVALID_TIMER)
		return;

	//Clear timer
	delete_timer(ud->attacktimer, unit_attack_timer);
	ud->attacktimer = INVALID_TIMER;
}

/**
 * Stop a unit's step action
 * @param bl: Object to stop
 */
void unit_stop_stepaction(struct block_list *bl)
{
	struct unit_data *ud;
	nullpo_retv(bl);
	ud = unit_bl2ud(bl);
	nullpo_retv(ud);

	//Clear remembered step action
	ud->stepaction = false;
	ud->target_to = 0;
	ud->stepskill_id = 0;
	ud->stepskill_lv = 0;

	if (ud->steptimer == INVALID_TIMER)
		return;

	//Clear timer
	delete_timer(ud->steptimer, unit_step_timer);
	ud->steptimer = INVALID_TIMER;
}

//Means current target is unattackable. For now only unlocks mobs.
int unit_unattackable(struct block_list *bl)
{
	struct unit_data *ud = unit_bl2ud(bl);
	if (ud) {
		ud->state.attack_continue = 0;
		ud->state.step_attack = 0;
		ud->target_to = 0;
		unit_set_target(ud, 0);
	}
	
	if(bl->type == BL_MOB)
		mob_unlocktarget((struct mob_data*)bl, gettick());
	else if(bl->type == BL_PET)
		pet_unlocktarget((struct pet_data*)bl);
	else if (bl->type == BL_ELEM)
		elem_unlocktarget((struct elemental_data*)bl, gettick());
	return 0;
}

/*==========================================
 * Requests a unit to attack a target
 * @param src: Object initiating attack
 * @param target_id: Target ID (bl->id)
 * @param continuous:
 *		0x1 - Whether or not the attack is ongoing
 *		0x2 - Whether function was called from unit_step_timer or not
 * @return Success(0); Fail(1);
 *------------------------------------------*/
int unit_attack(struct block_list *src,int target_id,int continuous)
{
	struct block_list *target;
	struct unit_data  *ud;
	int range;

	nullpo_ret(ud = unit_bl2ud(src));

	target = map_id2bl(target_id);
	if( target==NULL || status_isdead(target) )
	{
		unit_unattackable(src);
		return 1;
	}

	if( src->type == BL_PC )
	{
		TBL_PC* sd = (TBL_PC*)src;
		if( target->type == BL_NPC )
		{ // monster npcs [Valaris]
			npc_click(sd,(TBL_NPC*)target); // submitted by leinsirk10 [Celest]
			return 0;
		}
		if( pc_is90overweight(sd) )
		{ // overweight - stop attacking
			unit_stop_attack(src);
			return 0;
		}
		if (!pc_can_attack(sd, target_id)) {
			unit_stop_attack(src);
			return 0;
		}
	}

	if( battle_check_target(src,target,BCT_ENEMY) <= 0 || !status_check_skilluse(src, target, 0, 0, 0) )
	{
		unit_unattackable(src);
		return 1;
	}

	ud->state.attack_continue = (continuous & 1) ? 1 : 0;
	ud->state.step_attack = (continuous & 2) ? 1 : 0;
	unit_set_target(ud, target_id);

	range = status_get_range(src);

	if (continuous) //If you're to attack continously, set to auto-case character
		ud->chaserange = range;

	//Just change target/type. [Skotlex]
	if(ud->attacktimer != INVALID_TIMER)
		return 0;

	// New action request received, delete previous action request if not executed yet
	if (ud->stepaction || ud->steptimer != INVALID_TIMER)
		unit_stop_stepaction(src);

	// Remember the attack request from the client while walking to the next cell
	if (src->type == BL_PC && ud->walktimer != INVALID_TIMER && !battle_check_range(src, target, range - 1)) {
		ud->stepaction = true;
		ud->target_to = ud->target;
		ud->stepskill_id = 0;
		ud->stepskill_lv = 0;
		return 0; // Attacking will be handled by unit_walktoxy_timer in this case
	}

	if(DIFF_TICK(ud->attackabletime, gettick()) > 0)
		//Do attack next time it is possible. [Skotlex]
		ud->attacktimer=add_timer(ud->attackabletime,unit_attack_timer,src->id,0);
	else //Attack NOW.
		unit_attack_timer(INVALID_TIMER, gettick(), src->id, 0);

	return 0;
}

/*==========================================
 * Cancels an ongoing combo, resets attackable time, and restarts the
 * attack timer to resume attack after amotion time
 * @author [Skotlex]
 * @param bl: Object to cancel combo
 * @return Success(1); Fail(0);
 *------------------------------------------*/
int unit_cancel_combo(struct block_list *bl)
{
	struct unit_data  *ud;

	if (!status_change_end(bl, SC_COMBO, INVALID_TIMER))
		return 0; //Combo wasn't active.

	ud = unit_bl2ud(bl);
	nullpo_ret(ud);

	ud->attackabletime = gettick() + status_get_amotion(bl);

	if (ud->attacktimer == INVALID_TIMER)
		return 1; //Nothing more to do.
	
	delete_timer(ud->attacktimer, unit_attack_timer);
	ud->attacktimer=add_timer(ud->attackabletime,unit_attack_timer,bl->id,0);
	return 1;
}
/*==========================================
 * Does a path_search to check if a position can be reached
 * @param bl: Object to check path
 * @param x: X coordinate that will be path searched
 * @param y: Y coordinate that will be path searched
 * @param easy: Easy(1) or Hard(0) path check (hard attempts to go around obstacles)
 * @return true or false
 *------------------------------------------*/
bool unit_can_reach_pos(struct block_list *bl,int x,int y, int easy)
{
	nullpo_retr(false, bl);

	if( bl->x==x && bl->y==y )	// �����}�X
		return true;

	return path_search(NULL,bl->m,bl->x,bl->y,x,y,easy,CELL_CHKNOREACH);
}

/*==========================================
 * Does a path_search to check if a unit can be reached
 * @param bl: Object to check path
 * @param tbl: Target to be checked for available path
 * @param range: The number of cells away from bl that the path should be checked
 * @param easy: Easy(1) or Hard(0) path check (hard attempts to go around obstacles)
 * @param x: Pointer storing a valid X coordinate around tbl that can be reached
 * @param y: Pointer storing a valid Y coordinate around tbl that can be reached
 * @return true or false
 *------------------------------------------*/
bool unit_can_reach_bl(struct block_list *bl,struct block_list *tbl, int range, int easy, short *x, short *y)
{
	int i;
	short dx,dy;
	nullpo_retr(false, bl);
	nullpo_retr(false, tbl);

	if( bl->m != tbl->m)
		return false;
	
	if( bl->x==tbl->x && bl->y==tbl->y )
		return true;

	if(range>0 && !check_distance_bl(bl, tbl, range))
		return false;

	// It judges whether it can adjoin or not.
	dx=tbl->x - bl->x;
	dy=tbl->y - bl->y;
	dx=(dx>0)?1:((dx<0)?-1:0);
	dy=(dy>0)?1:((dy<0)?-1:0);
	
	if (map_getcell(tbl->m,tbl->x-dx,tbl->y-dy,CELL_CHKNOPASS))
	{	//Look for a suitable cell to place in.
		for(i=0;i<8 && map_getcell(tbl->m,tbl->x-dirx[i],tbl->y-diry[i],CELL_CHKNOPASS);i++);
		if (i==8) return false; //No valid cells.
		dx = dirx[i];
		dy = diry[i];
	}

	if (x) *x = tbl->x-dx;
	if (y) *y = tbl->y-dy;
	return path_search(NULL,bl->m,bl->x,bl->y,tbl->x-dx,tbl->y-dy,easy,CELL_CHKNOREACH);
}

/*==========================================
 * Calculates position of Pet/Mercenary/Homunculus/Elemental
 * @param bl: Object to calculate position
 * @param tx: X coordinate to go to
 * @param ty: Y coordinate to go to
 * @param dir: Direction which to be 2 cells from master's position
 * @return Success(0); Fail(1);
 *------------------------------------------*/
int	unit_calc_pos(struct block_list *bl, int tx, int ty, int dir)
{
	int dx, dy, x, y, i, k;
	struct unit_data *ud = unit_bl2ud(bl);
	nullpo_ret(ud);

	if( dir < 0 || dir > 7 )
		return 1;

	ud->to_x = tx;
	ud->to_y = ty;

	// 2 cells from Master Position
	dx = -dirx[dir] * 2;
	dy = -diry[dir] * 2;
	x = tx + dx;
	y = ty + dy;

	if( !unit_can_reach_pos(bl, x, y, 0) )
	{
		if( dx > 0 ) x--; else if( dx < 0 ) x++;
		if( dy > 0 ) y--; else if( dy < 0 ) y++;
		if( !unit_can_reach_pos(bl, x, y, 0) )
		{
			for( i = 0; i < 12; i++ )
			{
				k = rnd()%8; // Pick a Random Dir
				dx = -dirx[k] * 2;
				dy = -diry[k] * 2;
				x = tx + dx;
				y = ty + dy;
				if( unit_can_reach_pos(bl, x, y, 0) )
					break;
				else
				{
					if( dx > 0 ) x--; else if( dx < 0 ) x++;
					if( dy > 0 ) y--; else if( dy < 0 ) y++;
					if( unit_can_reach_pos(bl, x, y, 0) )
						break;
				}
			}
			if( i == 12 )
			{
				x = tx; y = tx; // Exactly Master Position
				if( !unit_can_reach_pos(bl, x, y, 0) )
					return 1;
			}
		}
	}
	ud->to_x = x;
	ud->to_y = y;
	
	return 0;
}

/*==========================================
 * Function timer to continuously attack
 * @param src: Object to continuously attack
 * @param tid: Timer ID
 * @param tick: Current tick
 * @return Attackable(1); Unattackable(0);
 *------------------------------------------*/
static int unit_attack_timer_sub(struct block_list* src, int tid, int64 tick)
{
	struct block_list *target;
	struct unit_data *ud;
	struct status_data *sstatus;
	struct map_session_data *sd = NULL;
	struct mob_data *md = NULL;
	struct elemental_data *ed = NULL;
	int range;
	
	if( (ud=unit_bl2ud(src))==NULL )
		return 0;
	if( ud->attacktimer != tid )
	{
		ShowError("unit_attack_timer %d != %d\n",ud->attacktimer,tid);
		return 0;
	}

	sd = BL_CAST(BL_PC, src);
	md = BL_CAST(BL_MOB, src);
	ed = BL_CAST(BL_ELEM, src);
	ud->attacktimer = INVALID_TIMER;
	target=map_id2bl(ud->target);

	if( src == NULL || src->prev == NULL || target==NULL || target->prev == NULL )
		return 0;

	if( status_isdead(src) || status_isdead(target) || 	battle_check_target(src,target,BCT_ENEMY) <= 0 || !status_check_skilluse(src, target, 0, 0, 0) 
#ifdef OFFICIAL_WALKPATH
		|| !path_search_long(NULL, src->m, src->x, src->y, target->x, target->y, CELL_CHKWALL)
#endif
		|| (sd && !pc_can_attack(sd, target->id)))
		return 0; // can't attack under these conditions

	if( src->m != target->m )
	{
		if( src->type == BL_MOB && mob_warpchase((TBL_MOB*)src, target) )
			return 1; // Follow up.
		return 0;
	}

	if( ud->skilltimer != INVALID_TIMER && !(sd && pc_checkskill(sd,SA_FREECAST) > 0) )
		return 0; // can't attack while casting
	
	if( !battle_config.sdelay_attack_enable && DIFF_TICK(ud->canact_tick,tick) > 0 && !(sd && pc_checkskill(sd,SA_FREECAST) > 0) )
	{ // attacking when under cast delay has restrictions:
		if( tid == INVALID_TIMER )
		{ //requested attack.
			if(sd) clif_skill_fail(sd,1,USESKILL_FAIL_SKILLINTERVAL,0,0);
			return 0;
		}
		//Otherwise, we are in a combo-attack, delay this until your canact time is over. [Skotlex]
		if( ud->state.attack_continue )
		{
			if( DIFF_TICK(ud->canact_tick, ud->attackabletime) > 0 )
				ud->attackabletime = ud->canact_tick;
			ud->attacktimer=add_timer(ud->attackabletime,unit_attack_timer,src->id,0);
		}
		return 1;
	}

	sstatus = status_get_status_data(src);
	range = sstatus->rhw.range;
	
	if( !sd || sd->status.weapon != W_BOW )
		range++; //Dunno why everyone but bows gets this extra range...
	if ((unit_is_walking(target) || ud->state.step_attack)
		&& (target->type == BL_PC || !map_getcell(target->m, target->x, target->y, CELL_CHKICEWALL)))
		range++; // Extra range when chasing (does not apply to mobs locked in an icewall)

	if (sd && !check_distance_client_bl(src, target, range)) {
		// Player tries to attack but target is too far, notify client
		clif_movetoattack(sd, target);
		return 1;
	}
	else if (md && !check_distance_bl(src, target, range)) {
		// Monster: Chase if required
		unit_walktobl(src, target, ud->chaserange, ud->state.walk_easy | 2);
		return 1;
	}
	if( !battle_check_range(src,target,range) )
	{
	  	//Within range, but no direct line of attack
		if( ud->state.attack_continue )
		{
			if(ud->chaserange > 2) ud->chaserange-=2;
			unit_walktobl(src,target,ud->chaserange,ud->state.walk_easy|2);
		}
		return 1;
	}

	//Sync packet only for players.
	//Non-players use the sync packet on the walk timer. [Skotlex]
	if (tid == INVALID_TIMER && sd) clif_fixpos(src);

	if( DIFF_TICK(ud->attackabletime,tick) <= 0 )
	{
		if (battle_config.attack_direction_change && (src->type&battle_config.attack_direction_change)) {
			ud->dir = map_calc_dir(src, target->x,target->y );
		}
		if(ud->walktimer != INVALID_TIMER)
			unit_stop_walking(src,1);
		if(md) {
			//First attack is always a normal attack
			if (md->state.skillstate == MSS_ANGRY || md->state.skillstate == MSS_BERSERK) {
				if (mobskill_use(md, tick, -1))
					return 1;
			}
			else {
				// Set mob's ANGRY/BERSERK states.
				md->state.skillstate = md->state.aggressive ? MSS_ANGRY : MSS_BERSERK;
			}
			if (sstatus->mode&MD_ASSIST && DIFF_TICK(md->last_linktime, tick) < MIN_MOBLINKTIME)
			{	// Link monsters nearby [Skotlex]
				md->last_linktime = tick;
				map_foreachinrange(mob_linksearch, src, md->db->range2, BL_MOB, md->mob_id, target, tick);
			}
		}
		else if(ed) {
			//First attack is always a normal attack
			if (ed->state.skillstate == MSS_ANGRY || ed->state.skillstate == MSS_BERSERK) {
				if (elemskill_use(ed, tick, -1))
					return 1;
			}
			else {
				// Set ANGRY/BERSERK states.
				ed->state.skillstate = ed->state.aggressive ? MSS_ANGRY : MSS_BERSERK;
			}

			//if (sstatus->mode&MD_ASSIST && DIFF_TICK(ed->last_linktime, tick) < MIN_ELEMLINKTIME)
			//{	// Link monsters nearby [Skotlex]
			//	ed->last_linktime = tick;
			//	map_foreachinrange(elem__linksearch, src, ed->db->range2, BL_ELEM, ed->class_, target, tick);
			//}
		}

		if(src->type == BL_PET && pet_attackskill((TBL_PET*)src, target->id))
			return 1;
		
		map_freeblock_lock();
		ud->attacktarget_lv = battle_weapon_attack(src,target,tick,0);

		if(sd && sd->status.pet_id > 0 && sd->pd && battle_config.pet_attack_support)
			pet_target_check(sd->pd,target,0);
		map_freeblock_unlock();

		ud->attackabletime = tick + sstatus->adelay;
//		You can't move if you can't attack neither.
		if (src->type&battle_config.attack_walk_delay)
			unit_set_walkdelay(src, tick, sstatus->amotion, 1);
	}

	if (ud->state.attack_continue)
	{
		if (src->type == BL_PC)
			((TBL_PC*)src)->idletime = last_tick;
		ud->attacktimer = add_timer(ud->attackabletime, unit_attack_timer, src->id, 0);
	}

	if (sd && battle_config.prevent_logout_trigger&PLT_ATTACK)
		sd->canlog_tick = gettick();

	return 1;
}

/*==========================================
 * Timer function to cancel attacking if unit has become unattackable
 * @param tid: Timer ID
 * @param tick: Current tick
 * @param id: Object to cancel attack if applicable
 * @param data: Data passed from timer call
 * @return 0
 *------------------------------------------*/
static int unit_attack_timer(int tid, int64 tick, int id, intptr_t data)
{
	struct block_list *bl;
	bl = map_id2bl(id);
	if(bl && unit_attack_timer_sub(bl, tid, tick) == 0)
		unit_unattackable(bl);
	return 0;
}

/*==========================================
 * Cancels an ongoing skill cast.
 * flag&1: Cast-Cancel invoked.
 * flag&2: Cancel only if skill is cancellable.
 *------------------------------------------*/
int unit_skillcastcancel(struct block_list *bl,char type)
{
	struct map_session_data *sd = NULL;
	struct unit_data *ud = unit_bl2ud( bl);
	struct status_change *sc = status_get_sc(bl);
	int64 tick=gettick();
	int ret=0, skill;
	
	nullpo_ret(bl);
	if (!ud || ud->skilltimer == INVALID_TIMER)
		return 0; //Nothing to cancel.

	sd = BL_CAST(BL_PC, bl);

	if (type&2) {
		//See if it can be cancelled.
		if (!ud->state.skillcastcancel)
			return 0;

		if (sd && (sd->special_state.no_castcancel2 ||
			(sd->special_state.no_castcancel && !map_flag_gvg2(bl->m) && !map[bl->m].flag.battleground))) //fixed flags being read the wrong way around [blackhole89]
			return 0;

		if (sc && sc->count)
		{// Unlimited Humming Voice may prevent cast interruptions, but it doesn't work in WoE or Battlegrounds.
			if (sc->data[SC_UNLIMITED_HUMMING_VOICE] && !map_flag_gvg(bl->m) && !map[bl->m].flag.battleground)
				return 0;
		}
	}
	
	ud->canact_tick = tick;

	if(type&1 && sd)
		skill = sd->skillid_old;
	else
		skill = ud->skill_id;
	
	if (skill_get_inf(skill) & INF_GROUND_SKILL)
		ret=delete_timer( ud->skilltimer, skill_castend_pos );
	else
		ret=delete_timer( ud->skilltimer, skill_castend_id );
	if(ret<0)
		ShowError("delete timer error : skill_id : %d\n",ret);

	ud->skilltimer = INVALID_TIMER;

	if( sd && pc_checkskill(sd,SA_FREECAST) > 0 )
		status_calc_bl(&sd->bl, SCB_SPEED);
	if( skill == LG_EXEEDBREAK )
		status_calc_bl(bl,SCB_SPEED);

	if( sd )
	{
		switch( skill )
		{
		case CG_ARROWVULCAN:
			sd->canequip_tick = tick;
			break;
		}
	}

	if(bl->type==BL_MOB) ((TBL_MOB*)bl)->skillidx  = -1;

	clif_skillcastcancel(bl);
	return 1;
}

/*==========================================
 * Initialized data on a unit
 * @param bl: Object to initialize data on
 *------------------------------------------*/
void unit_dataset(struct block_list *bl)
{
	struct unit_data *ud;
	nullpo_retv(ud = unit_bl2ud(bl));

	memset( ud, 0, sizeof( struct unit_data) );
	ud->bl             = bl;
	ud->walktimer      = INVALID_TIMER;
	ud->skilltimer     = INVALID_TIMER;
	ud->attacktimer    = INVALID_TIMER;
	ud->steptimer      = INVALID_TIMER;
	ud->attackabletime = 
	ud->canact_tick    = 
	ud->canmove_tick   = gettick();
}

/*==========================================
 * Counts the number of units attacking 'bl'
 *------------------------------------------*/
int unit_counttargeted(struct block_list* bl)
{
	struct unit_data* ud;

	if (bl && (ud = unit_bl2ud(bl)))
		return ud->target_count;

	return 0;	
}

/*==========================================
 * Changes the size of a unit
 * @param bl: Object to change size [PC|MOB]
 * @param size: New size of bl
 * @return 0
 *------------------------------------------*/
int unit_changeviewsize(struct block_list *bl,short size)
{
	nullpo_ret(bl);

	size=(size<0)?-1:(size>0)?1:0;

	if(bl->type == BL_PC) {
		((TBL_PC*)bl)->state.size=size;
	} else if(bl->type == BL_MOB) {
		((TBL_MOB*)bl)->special_state.size=size;
	} else
		return 0;
	if(size!=0)
		clif_specialeffect(bl,421+size, AREA);
	return 0;
}

/*==========================================
 * Removes a bl/ud from the map.
 * Returns 1 on success. 0 if it couldn't be removed or the bl was free'd
 * if clrtype is 1 (death), appropiate cleanup is performed. 
 * Otherwise it is assumed bl is being warped.
 * On-Kill specific stuff is not performed here, look at status_damage for that.
 *------------------------------------------*/
int unit_remove_map_(struct block_list *bl, clr_type clrtype, const char* file, int line, const char* func)
{
	struct unit_data *ud = unit_bl2ud(bl);
	struct status_change *sc = status_get_sc(bl);
	nullpo_ret(ud);

	if(bl->prev == NULL)
		return 0; //Already removed?

	map_freeblock_lock();

	if (ud->walktimer != INVALID_TIMER)
		unit_stop_walking(bl,0);

	if (ud->skilltimer != INVALID_TIMER)
		unit_skillcastcancel(bl,0);

	//Clear target even if there is no timer
	if (ud->target || ud->attacktimer != INVALID_TIMER)
		unit_stop_attack(bl);

	//Clear stepaction even if there is no timer
	if (ud->stepaction || ud->steptimer != INVALID_TIMER)
		unit_stop_stepaction(bl);

	// Do not reset can-act delay. [Skotlex]
	ud->attackabletime = ud->canmove_tick /*= ud->canact_tick*/ = gettick();
	
	if(sc && sc->count ) { //map-change/warp dispells.
		status_change_end(bl, SC_BLADESTOP, INVALID_TIMER);
		status_change_end(bl, SC_BASILICA, INVALID_TIMER);
		status_change_end(bl, SC_ANKLE, INVALID_TIMER);
		status_change_end(bl, SC_TRICKDEAD, INVALID_TIMER);
		status_change_end(bl, SC_BLADESTOP_WAIT, INVALID_TIMER);
		status_change_end(bl, SC_RUN, INVALID_TIMER);
		status_change_end(bl, SC_DANCING, INVALID_TIMER);
		status_change_end(bl, SC_WARM, INVALID_TIMER);
		status_change_end(bl, SC_DEVOTION, INVALID_TIMER);
		status_change_end(bl, SC_MARIONETTE, INVALID_TIMER);
		status_change_end(bl, SC_MARIONETTE2, INVALID_TIMER);
		status_change_end(bl, SC_CLOSECONFINE, INVALID_TIMER);
		status_change_end(bl, SC_CLOSECONFINE2, INVALID_TIMER);
		status_change_end(bl, SC_HIDING, INVALID_TIMER);
		// If the bl is a PC, we'll handle the removal of cloaking and cloaking exceed later
		if( bl->type != BL_PC )
		{
			status_change_end(bl, SC_CLOAKING, INVALID_TIMER);
			status_change_end(bl, SC_CLOAKINGEXCEED, INVALID_TIMER);
		}
		status_change_end(bl, SC_CHASEWALK, INVALID_TIMER);
		if (sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_SELF)
			status_change_end(bl, SC_GOSPEL, INVALID_TIMER);
		if (sc->data[SC_PROVOKE] && sc->data[SC_PROVOKE]->timer == INVALID_TIMER)
			status_change_end(bl, SC_PROVOKE, INVALID_TIMER); //End infinite provoke to prevent exploit
		status_change_end(bl, SC_CHANGE, INVALID_TIMER);
		status_change_end(bl, SC_STOP, INVALID_TIMER);
		status_change_end(bl, SC_ELECTRICSHOCKER, INVALID_TIMER);
		status_change_end(bl, SC_ROLLINGCUTTER, INVALID_TIMER);
		status_change_end(bl, SC_WUGBITE, INVALID_TIMER);
		status_change_end(bl, SC_WUGDASH, INVALID_TIMER);
		status_change_end(bl, SC_CAMOUFLAGE, INVALID_TIMER);
		status_change_end(bl, SC_MAGNETICFIELD, INVALID_TIMER);
		status_change_end(bl, SC_NEUTRALBARRIER_MASTER, INVALID_TIMER);
		status_change_end(bl, SC_NEUTRALBARRIER, INVALID_TIMER);
		status_change_end(bl, SC_STEALTHFIELD_MASTER, INVALID_TIMER);
		status_change_end(bl, SC_STEALTHFIELD, INVALID_TIMER);
		status_change_end(bl, SC_BANDING, INVALID_TIMER);
		status_change_end(bl, SC__SHADOWFORM, INVALID_TIMER);
		status_change_end(bl, SC__MANHOLE, INVALID_TIMER);
		status_change_end(bl, SC_CURSEDCIRCLE_ATKER, INVALID_TIMER);
		status_change_end(bl, SC_CURSEDCIRCLE_TARGET, INVALID_TIMER);
		status_change_end(bl, SC_NETHERWORLD, INVALID_TIMER);
		status_change_end(bl, SC_VACUUM_EXTREME, INVALID_TIMER);
		status_change_end(bl, SC_BLOOD_SUCKER, INVALID_TIMER);
		status_change_end(bl, SC_C_MARKER, INVALID_TIMER);
		status_change_end(bl, SC_H_MINE, INVALID_TIMER);
		status_change_end(bl, SC_NEWMOON, INVALID_TIMER);
		status_change_end(bl, SC_FLASHKICK, INVALID_TIMER);
		status_change_end(bl, SC_SOULUNITY, INVALID_TIMER);
		status_change_end(bl, SC_KINGS_GRACE, INVALID_TIMER);
		status_change_end(bl, SC_SUHIDE, INVALID_TIMER);
		status_change_end(bl, SC_SV_ROOTTWIST, INVALID_TIMER);
		status_change_end(bl, SC_TINDER_BREAKER, INVALID_TIMER);
	}

	if (bl->type&BL_CHAR) {
		skill_unit_move(bl,gettick(),4);
		skill_cleartimerskill(bl);
	}

	switch( bl->type )
	{
	case BL_PC:
	{
		struct map_session_data *sd = (struct map_session_data*)bl;

		if (sd->shadowform_id) {
			struct block_list *d_bl = map_id2bl(sd->shadowform_id);
			if (d_bl)
				status_change_end(d_bl, SC__SHADOWFORM, INVALID_TIMER);
		}

		//Leave/reject all invitations.
		if(sd->chatID)
			chat_leavechat(sd,0);
		if(sd->trade_partner)
			trade_tradecancel(sd);
		searchstore_close(sd);
		if (sd->menuskill_id != AL_TELEPORT) {
			if (sd->state.storage_flag == 1)
				storage_storage_quit(sd, 0);
			else if (sd->state.storage_flag == 2)
				storage_guild_storage_quit(sd, 0);
			else if (sd->state.storage_flag == 3)
				storage_storage2_quit(sd);

			sd->state.storage_flag = 0; //Force close it when being warped.
		}
		if(sd->party_invite>0)
			party_reply_invite(sd,sd->party_invite,0);
		if(sd->guild_invite>0)
			guild_reply_invite(sd,sd->guild_invite,0);
		if(sd->guild_alliance>0)
			guild_reply_reqalliance(sd,sd->guild_alliance_account,0);
		if(sd->menuskill_id)
			sd->menuskill_id = sd->menuskill_val = sd->menuskill_val2 = sd->menuskill_itemused = 0;
		if (sd->touching_id)
			npc_touchnext_areanpc(sd,true);

		// Check if warping and not changing the map.
		if ( sd->state.warping && !sd->state.changemap )
		{
			status_change_end(bl, SC_CLOAKING, INVALID_TIMER);
			status_change_end(bl, SC_CLOAKINGEXCEED, INVALID_TIMER);
		}

		sd->npc_shopid = 0;
		sd->adopt_invite = 0;

		if(sd->pvp_timer != INVALID_TIMER) {
			delete_timer(sd->pvp_timer,pc_calc_pvprank_timer);
			sd->pvp_timer = INVALID_TIMER;
			sd->pvp_rank = 0;
		}
		if(sd->duel_group > 0)
			duel_leave(sd->duel_group, sd);

		if (pc_issit(sd) && pc_setstand(sd, false))
			skill_sit(sd, 0);

		party_send_dot_remove(sd);//minimap dot fix [Kevin]
		guild_send_dot_remove(sd);
		bg_send_dot_remove(sd);

		if( map[bl->m].users <= 0 || sd->state.debug_remove_map )
		{// this is only place where map users is decreased, if the mobs were removed too soon then this function was executed too many times [FlavioJS]
			if( sd->debug_file == NULL || !(sd->state.debug_remove_map) )
			{
				sd->debug_file = "";
				sd->debug_line = 0;
				sd->debug_func = "";
			}
			ShowDebug("unit_remove_map: unexpected state when removing player AID/CID:%d/%d"
				" (active=%d connect_new=%d rewarp=%d changemap=%d debug_remove_map=%d)"
				" from map=%s (users=%d)."
				" Previous call from %s:%d(%s), current call from %s:%d(%s)."
				" Please report this!!!\n",
				sd->status.account_id, sd->status.char_id,
				sd->state.active, sd->state.connect_new, sd->state.rewarp, sd->state.changemap, sd->state.debug_remove_map,
				map[bl->m].name, map[bl->m].users,
				sd->debug_file, sd->debug_line, sd->debug_func, file, line, func);
		}
		else
		if (--map[bl->m].users == 0 && battle_config.dynamic_mobs)	//[Skotlex]
			map_removemobs(bl->m);
		if (!(sd->sc.option&OPTION_INVISIBLE))
		{// decrement the number of active pvp players on the map
			--map[bl->m].users_pvp;
		}
		if( map[bl->m].instance_id >= 0 )
		{
			instances[map[bl->m].instance_id].users--;
			instance_check_idle(map[bl->m].instance_id);
		}
		if (sd->state.hpmeter_visible)
		{
			map[bl->m].hpmeter_visible--;
			sd->state.hpmeter_visible = 0;
		}

		sd->state.debug_remove_map = 1; // temporary state to track double remove_map's [FlavioJS]
		sd->debug_file = file;
		sd->debug_line = line;
		sd->debug_func = func;

		break;
	}
	case BL_MOB:
	{
		struct mob_data *md = (struct mob_data*)bl;
		// Drop previous target mob_slave_keep_target: no.
		if (!battle_config.mob_slave_keep_target)
			md->target_id=0;

		md->attacked_id=0;
		md->state.skillstate= MSS_IDLE;

		break;
	}
	case BL_PET:
	{
		struct pet_data *pd = (struct pet_data*)bl;
		if( pd->pet.intimate <= 0 && !(pd->master && !pd->master->state.active) )
		{	//If logging out, this is deleted on unit_free
			clif_clearunit_area(bl,clrtype);
			map_delblock(bl);
			unit_free(bl,CLR_OUTSIGHT);
			map_freeblock_unlock();
			return 0;
		}

		break;
	}
	case BL_HOM:
	{
		struct homun_data *hd = (struct homun_data *)bl;
		ud->canact_tick = ud->canmove_tick; //It appears HOM do reset the can-act tick.
		if( !hd->homunculus.intimacy && !(hd->master && !hd->master->state.active) )
		{	//If logging out, this is deleted on unit_free
			clif_emotion(bl, E_SOB);
			clif_clearunit_area(bl,clrtype);
			map_delblock(bl);
			unit_free(bl,CLR_OUTSIGHT);
			map_freeblock_unlock();
			return 0;
		}
		break;
	}
	case BL_MER:
	{
		struct mercenary_data *md = (struct mercenary_data *)bl;
		ud->canact_tick = ud->canmove_tick;
		if( mercenary_get_lifetime(md) <= 0 && !(md->master && !md->master->state.active) )
		{
			clif_clearunit_area(bl,clrtype);
			map_delblock(bl);
			unit_free(bl,CLR_OUTSIGHT);
			map_freeblock_unlock();
			return 0;
		}
		break;
	}
	case BL_ELEM:
	{
		struct elemental_data *ed = (struct elemental_data *)bl;
		ud->canact_tick = ud->canmove_tick;
		if( elemental_get_lifetime(ed) <= 0 && !(ed->master && !ed->master->state.active) ) {
			clif_clearunit_area(bl,clrtype);
			map_delblock(bl);
			unit_free(bl,CLR_OUTSIGHT);
			map_freeblock_unlock();
			return 0;
		}
		break;
	}
	default: ;// do nothing
	}

	// BL_MOB is handled by mob_dead
	if (bl->type != BL_MOB)
		clif_clearunit_area(bl, clrtype);

	map_delblock(bl);
	map_freeblock_unlock();
	return 1;
}

/**
 * Refresh the area with a change in display of a unit.
 * @bl: Object to update
 */
void unit_refresh(struct block_list *bl) {
	nullpo_retv(bl);

	if (bl->m < 0)
		return;

	// Using CLR_TRICKDEAD because other flags show effects
	// Probably need to use another flag or other way to refresh it
	if (map[bl->m].users) {
		clif_clearunit_area(bl, CLR_TRICKDEAD); // Fade out
		clif_spawn(bl); // Fade in
	}
}

/*==========================================
 * Removes units of a master when the master is removed from map
 * @param sd: Player
 * @param clrtype: How bl is being removed
 *	0: Assume bl is being warped
 *	1: Death, appropriate cleanup performed
 *------------------------------------------*/
void unit_remove_map_pc(struct map_session_data *sd, clr_type clrtype) {
	unit_remove_map(&sd->bl,clrtype);

	//CLR_RESPAWN is the warp from logging out, CLR_TELEPORT is the warp from teleporting, but pets/homunc need to just 'vanish' instead of showing the warping animation.
	if (clrtype == CLR_RESPAWN || clrtype == CLR_TELEPORT) clrtype = CLR_OUTSIGHT;

	if(sd->pd)
		unit_remove_map(&sd->pd->bl, clrtype);
	if(hom_is_active(sd->hd))
		unit_remove_map(&sd->hd->bl, clrtype);
	if(sd->md)
		unit_remove_map(&sd->md->bl, clrtype);
	if(sd->ed)
		unit_remove_map(&sd->ed->bl, clrtype);
}

/*==========================================
 * Frees units of a player when is removed from map
 * Also free his pets/homon/mercenary/elemental/etc if he have any
 * @param sd: Player
 *------------------------------------------*/
void unit_free_pc(struct map_session_data *sd) {
	if (sd->pd) unit_free(&sd->pd->bl,CLR_OUTSIGHT);
	if (sd->hd) unit_free(&sd->hd->bl,CLR_OUTSIGHT);
	if (sd->md) unit_free(&sd->md->bl,CLR_OUTSIGHT);
	if (sd->ed) unit_free(&sd->ed->bl,CLR_OUTSIGHT);
	unit_free(&sd->bl,CLR_TELEPORT);
}

/*==========================================
 * Frees all related resources to the unit
 * @param bl: Object being removed from map
 * @param clrtype: How bl is being removed
 *	0: Assume bl is being warped
 *	1: Death, appropriate cleanup performed
 * @return 0
 *------------------------------------------*/
int unit_free(struct block_list *bl, clr_type clrtype)
{
	struct unit_data *ud = unit_bl2ud( bl );
	nullpo_ret(ud);

	map_freeblock_lock();
	if( bl->prev )	//Players are supposed to logout with a "warp" effect.
		unit_remove_map(bl, clrtype);

	switch( bl->type )
	{
		case BL_PC:
		{
			struct map_session_data *sd = (struct map_session_data*)bl;
			int i;

			if( status_isdead(bl) )
				pc_setrestartvalue(sd,2);

			pc_delinvincibletimer(sd);
			pc_delautobonus(sd,sd->autobonus,ARRAYLENGTH(sd->autobonus),false);
			pc_delautobonus(sd,sd->autobonus2,ARRAYLENGTH(sd->autobonus2),false);
			pc_delautobonus(sd,sd->autobonus3,ARRAYLENGTH(sd->autobonus3),false);
			
			if( sd->followtimer != INVALID_TIMER )
				pc_stop_following(sd);
				
			if( sd->duel_invite > 0 )
				duel_reject(sd->duel_invite, sd);

			skill_blockpc_clear(sd); //clear all skill cooldown related
		
			// Notify friends that this char logged out. [Skotlex]
			map_foreachpc(clif_friendslist_toggle_sub, sd->status.account_id, sd->status.char_id, 0);
			party_send_logout(sd);
			guild_send_memberinfoshort(sd,0);
			pc_cleareventtimer(sd);
			pc_inventory_rental_clear(sd);
			if( sd->bg_id ) bg_team_leave(sd,1);
			pc_delspiritball(sd,sd->spiritball,1);
			pc_delshieldball(sd, sd->shieldball, 1);
			pc_delrageball(sd, sd->rageball, 1);
			pc_delcharmball(sd, sd->charmball, 3);
			pc_delsoulball(sd, sd->soulball, 1);

			if( sd->reg )
			{	//Double logout already freed pointer fix... [Skotlex]
				aFree(sd->reg);
				sd->reg = NULL;
				sd->reg_num = 0;
			}
			if( sd->regstr )
			{
				int i;
				for( i = 0; i < sd->regstr_num; ++i )
					if( sd->regstr[i].data )
						aFree(sd->regstr[i].data);
				aFree(sd->regstr);
				sd->regstr = NULL;
				sd->regstr_num = 0;
			}
			if( sd->st && sd->st->state != RUN )
			{// free attached scripts that are waiting
				script_free_state(sd->st);
				sd->st = NULL;
				sd->npc_id = 0;
			}
			if (sd->sc_display_count) {
				for (i = 0; i < sd->sc_display_count; i++)
					ers_free(pc_sc_display_ers, sd->sc_display[i]);
				sd->sc_display_count = 0;
				aFree(sd->sc_display);
				sd->sc_display = NULL;
			}
			if (sd->quest_log != NULL) {
				aFree(sd->quest_log);
				sd->quest_log = NULL;
				sd->num_quests = sd->avail_quests = 0;
			}
			if (sd->qi_display) {
				aFree(sd->qi_display);
				sd->qi_display = NULL;
			}
			if (sd->instance != NULL) {
				aFree(sd->instance);
				sd->instance = NULL;
			}
			sd->qi_count = 0;
#if PACKETVER >= 20150513
			if (sd->hatEffectCount > 0){
				aFree(sd->hatEffectIDs);
				sd->hatEffectIDs = NULL;
				sd->hatEffectCount = 0;
			}
#endif
			VECTOR_CLEAR(sd->achievement); // Achievement [Smokexyz/Hercules]
			VECTOR_CLEAR(sd->title_ids); // Title [Dastgir/Hercules]
			// Clearing...
			if (sd->bonus_script.head)
				pc_bonus_script_clear(sd, BSF_REM_ALL);

			pc_itemgrouphealrate_clear(sd);
			break;
		}
		case BL_PET:
		{
			struct pet_data *pd = (struct pet_data*)bl;
			struct map_session_data *sd = pd->master;
			pet_hungry_timer_delete(pd);
			if( pd->a_skill )
			{
				aFree(pd->a_skill);
				pd->a_skill = NULL;
			}
			if( pd->s_skill )
			{
				if (pd->s_skill->timer != INVALID_TIMER) {
					if (pd->s_skill->id)
						delete_timer(pd->s_skill->timer, pet_skill_support_timer);
					else
						delete_timer(pd->s_skill->timer, pet_heal_timer);
				}
				aFree(pd->s_skill);
				pd->s_skill = NULL;
			}
			if( pd->recovery )
			{
				if(pd->recovery->timer != INVALID_TIMER)
					delete_timer(pd->recovery->timer, pet_recovery_timer);
				aFree(pd->recovery);
				pd->recovery = NULL;
			}
			if( pd->bonus )
			{
				if (pd->bonus->timer != INVALID_TIMER)
					delete_timer(pd->bonus->timer, pet_skill_bonus_timer);
				aFree(pd->bonus);
				pd->bonus = NULL;
			}
			if( pd->loot )
			{
				pet_lootitem_drop(pd,sd);
				if (pd->loot->item)
					aFree(pd->loot->item);
				aFree (pd->loot);
				pd->loot = NULL;
			}
			if( pd->pet.intimate > 0 )
				intif_save_petdata(pd->pet.account_id,&pd->pet);
			else
			{	//Remove pet.
				intif_delete_petdata(pd->pet.pet_id);
				if (sd) sd->status.pet_id = 0;
			}
			if( sd )
				sd->pd = NULL;
			pd->master = NULL;
			break;
		}
		case BL_MOB:
		{
			struct mob_data *md = (struct mob_data*)bl;
			if( md->spawn_timer != INVALID_TIMER )
			{
				delete_timer(md->spawn_timer,mob_delayspawn);
				md->spawn_timer = INVALID_TIMER;
			}
			if( md->deletetimer != INVALID_TIMER )
			{
				delete_timer(md->deletetimer,mob_timer_delete);
				md->deletetimer = INVALID_TIMER;
			}
			if (md->lootitems) {
				aFree(md->lootitems);
				md->lootitems = NULL;
			}
			if( md->guardian_data )
			{
				struct guild_castle* gc = md->guardian_data->castle;
				if( md->guardian_data->number >= 0 && md->guardian_data->number < MAX_GUARDIANS )
				{
					gc->guardian[md->guardian_data->number].id = 0;
				}
				else
				{
					int i;
					ARR_FIND(0, gc->temp_guardians_max, i, gc->temp_guardians[i] == md->bl.id);
					if( i < gc->temp_guardians_max )
						gc->temp_guardians[i] = 0;
				}
				aFree(md->guardian_data);
				md->guardian_data = NULL;
			}
			if( md->spawn )
			{
				md->spawn->active--;
				if( !md->spawn->state.dynamic )
				{// permanently remove the mob
					if( --md->spawn->num == 0 )
					{// Last freed mob is responsible for deallocating the group's spawn data.
						aFree(md->spawn);
						md->spawn = NULL;
					}
				}
			}
			if( md->base_status)
			{
				aFree(md->base_status);
				md->base_status = NULL;
			}
			if( mob_is_clone(md->mob_id) )
				mob_clone_delete(md);
			if( md->tomb_nid )
				mvptomb_destroy(md);
			break;
		}
		case BL_HOM:
		{
			struct homun_data *hd = (TBL_HOM*)bl;
			struct map_session_data *sd = hd->master;
			hom_hungry_timer_delete(hd);
			if( hd->homunculus.intimacy > 0 )
				hom_save(hd);
			else
			{
				intif_homunculus_requestdelete(hd->homunculus.hom_id);
				if( sd )
					sd->status.hom_id = 0;
			}
			if( sd )
				sd->hd = NULL;
			hd->master = NULL;
			break;
		}
		case BL_MER:
		{
			struct mercenary_data *md = (TBL_MER*)bl;
			struct map_session_data *sd = md->master;
			if( mercenary_get_lifetime(md) > 0 )
				mercenary_save(md);
			else
			{
				intif_mercenary_delete(md->mercenary.mercenary_id);
				if( sd )
					sd->status.mer_id = 0;
			}
			if( sd )
				sd->md = NULL;

			mercenary_contract_stop(md);
			md->master = NULL;
			break;
		}
		case BL_ELEM:
		{
			struct elemental_data *ed = (TBL_ELEM*)bl;
			struct map_session_data *sd = ed->master;
			if( elemental_get_lifetime(ed) > 0 )
				elemental_save(ed);
			else {
#ifndef TXT_ONLY
				intif_elemental_delete(ed->elemental.elemental_id);
#endif
					if( sd )
					sd->status.ele_id = 0;
			}
			if( sd )
				sd->ed = NULL;

			elem_summon_stop(ed);
			ed->master = NULL;
			break;
		}
	}

	skill_clear_unitgroup(bl);
	status_change_clear(bl,1);
	map_deliddb(bl);
	if( bl->type != BL_PC ) //Players are handled by map_quit
		map_freeblock(bl);
	map_freeblock_unlock();
	return 0;
}

/*==========================================
 * Initialization function for unit on map start
 * called in map::do_init
 *------------------------------------------*/
void do_init_unit(void)
{
	add_timer_func_list(unit_attack_timer,  "unit_attack_timer");
	add_timer_func_list(unit_walktoxy_timer,"unit_walktoxy_timer");
	add_timer_func_list(unit_walktobl_sub, "unit_walktobl_sub");
	add_timer_func_list(unit_delay_walktoxy_timer,"unit_delay_walktoxy_timer");
	add_timer_func_list(unit_delay_walktobl_timer, "unit_delay_walktobl_timer");
	add_timer_func_list(unit_teleport_timer, "unit_teleport_timer");
	add_timer_func_list(unit_step_timer, "unit_step_timer");
}

/*==========================================
 * Unit module destructor, (thing to do before closing the module)
 * called in map::do_final
 * @return 0
 *------------------------------------------*/
void do_final_unit(void)
{
	// nothing to do
}
