// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/malloc.h"

#include "../common/nullpo.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "../common/db.h"

#include "clif.h"
#include "instance.h"
#include "map.h"
#include "npc.h"
#include "party.h"
#include "guild.h"
#include "pc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

unsigned short instance_start_id = 0;
unsigned short instance_count = 0;

/// Checks whether given instance id is valid or not.
bool instance_is_valid(int instance_id) {
	if (instance_id < 0 || instance_id >= instance_count) {// out of range
		return false;
	}

	if (!instance_is_active(instances[instance_id])) { // uninitialized/freed/being freed instance slot
		return false;
	}

	return true;
}


/*--------------------------------------
 * name : instance name
 * Return value could be
 * -4 = already exists | -2 = owner not found | -1 = invalid type
 * On success return instance_id
 *--------------------------------------*/
int instance_create(int owner_id, const char *name, enum instance_owner_type type) {
	struct map_session_data *sd = NULL;
	unsigned short *icptr = NULL;
	struct party_data *p = NULL;
	struct guild *g = NULL;
	short *iptr = NULL;
	int i, j;

	nullpo_retr(-1, name);

	switch (type) {
	case IOT_NONE:
		break;
	case IOT_CHAR:
		if ((sd = map_id2sd(owner_id)) == NULL) {
			ShowError("instance_create: character %d not found for instance '%s'.\n", owner_id, name);
			return -2;
		}
		iptr = sd->instance;
		icptr = &sd->instances;
		break;
	case IOT_PARTY:
		if ((p = party_search(owner_id)) == NULL) {
			ShowError("instance_create: party %d not found for instance '%s'.\n", owner_id, name);
			return -2;
		}
		iptr = p->instance;
		icptr = &p->instances;
		break;
	case IOT_GUILD:
		if ((g = guild_search(owner_id)) == NULL) {
			ShowError("instance_create: guild %d not found for instance '%s'.\n", owner_id, name);
			return -2;
		}
		iptr = g->instance;
		icptr = &g->instances;
		break;
	default:
		ShowError("instance_create: unknown type %d for owner_id %d and name %s.\n", type, owner_id, name);
		return -1;
	}

	if (type != IOT_NONE && *icptr) {
		ARR_FIND(0, *icptr, i, iptr[i] != -1 && strcmp(instances[iptr[i]].name, name) == 0);
		if (i != *icptr)
			return -4;/* already got this instance */
	}

	ARR_FIND(0, instance_count, i, instances[i].state == INSTANCE_FREE);

	if (i == instance_count)
		RECREATE(instances, struct instance_data, ++instance_count);

	instances[i].state = INSTANCE_IDLE;
	instances[i].id = i;
	instances[i].idle_timer = INVALID_TIMER;
	instances[i].idle_timeout = instances[i].idle_timeoutval = 0;
	instances[i].progress_timer = INVALID_TIMER;
	instances[i].progress_timeout = 0;
	instances[i].users = 0;
	instances[i].map = NULL;
	instances[i].num_map = 0;
	instances[i].owner_id = owner_id;
	instances[i].owner_type = type;
	instances[i].vars = idb_alloc(DB_OPT_RELEASE_DATA);
	instances[i].respawn.map = 0;
	instances[i].respawn.y = 0;
	instances[i].respawn.x = 0;

	safestrncpy(instances[i].name, name, sizeof(instances[i].name));

	if (type != IOT_NONE) {
		ARR_FIND(0, *icptr, j, iptr[j] == -1);
		if (j == *icptr) {
			switch (type) {
			case IOT_CHAR:
				RECREATE(sd->instance, short, ++*icptr);
				sd->instance[sd->instances - 1] = i;
				break;
			case IOT_PARTY:
				RECREATE(p->instance, short, ++*icptr);
				p->instance[p->instances - 1] = i;
				break;
			case IOT_GUILD:
				RECREATE(g->instance, short, ++*icptr);
				g->instance[g->instances - 1] = i;
				break;
			}
		}
		else
			iptr[j] = i;
	}

	clif_instance(i, 1, 0); // Start instancing window
	return i;
}

/*--------------------------------------
 * Add a map to the instance using src map "name"
 *--------------------------------------*/
int instance_add_map(const char *name, int instance_id, bool usebasename, const char *map_name)
{
	int m = map_mapname2mapid(name), i, im = -1;
	size_t num_cell, size, j;

	nullpo_retr(-1, name);

	if( m < 0 )
		return -1; // source map not found
		
	if( !instance_is_valid(instance_id) )
	{
		ShowError("instance_add_map: trying to attach '%s' map to non-existing instance %d.\n", name, instance_id);
		return -1;
	}

	if (map_name != NULL && strdb_iget(map_db, map_name)) {
		ShowError("instance_add_map: trying to create instanced map with existent name '%s'\n", map_name);
		return -2;
	}

	if (map[m].instance_id >= 0) { // Source map already belong to a Instance.
		ShowError("instance_add_map: trying to instance already instanced map %s.\n", name);
		return -4;
	}

	ARR_FIND(instance_start_id, map_num, i, map[i].name[0] == 0); // Searching for a Free Map

	if (i < map_num)
		im = i; // Unused map found (old instance)
	else {
		im = map_num; // Using next map index
		RECREATE(map, struct map_data, ++map_num);
	}

	if (map[m].cell == (struct mapcell *)0xdeadbeaf)
		map_cellfromcache(&map[m]);

	memcpy( &map[im], &map[m], sizeof(struct map_data) ); // Copy source map
	if (map_name != NULL) {
		snprintf(map[im].name, MAP_NAME_LENGTH, "%s", map_name);
		map[im].custom_name = true;
	}
	else
		snprintf(map[im].name, MAP_NAME_LENGTH, (usebasename ? "%.3d#%s" : "%.3d%s"), instance_id, name); // Generate Name for Instance Map
	map[im].index = mapindex_addmap(0, map[im].name); // Add map index

	if( !map[im].index )
	{
		map[im].name[0] = '\0';
		ShowError("instance_add_map: no more free map indexes.\n");
		return -3; // No free map index
	}	

	// Reallocate cells
	num_cell = map[im].xs * map[im].ys;
	CREATE( map[im].cell, struct mapcell, num_cell );
	memcpy( map[im].cell, map[m].cell, num_cell * sizeof(struct mapcell) );

	// Appropriately clear cell data
	for (j = 0; j < num_cell; j++) {
		map[im].cell[j].basilica = 0;
		map[im].cell[j].icewall = 0;
		map[im].cell[j].npc = 0;
		map[im].cell[j].landprotector = 0;
		map[im].cell[j].maelstrom = 0;
		map[im].cell[j].pvp = 0;
#ifdef CELL_NOSTACK
		map[im].cell[j].cell_bl = 0; //Holds amount of bls in this cell.
#endif
	}

	size = map[im].bxs * map[im].bys * sizeof(struct block_list*);
	map[im].block = (struct block_list**)aCalloc(1, size);
	map[im].block_mob = (struct block_list**)aCalloc(1, size);

	memset(map[im].npc, 0x00, sizeof(map[i].npc));
	map[im].npc_num = 0;

	memset(map[im].moblist, 0x00, sizeof(map[im].moblist));
	map[im].mob_delete_timer = INVALID_TIMER;

	//Mimic questinfo
	if (map[m].qi_count) {
		map[im].qi_count = map[m].qi_count;
		CREATE(map[im].qi_data, struct questinfo, map[im].qi_count);
		memcpy(map[im].qi_data, map[m].qi_data, map[im].qi_count * sizeof(struct questinfo));
	}

	map[im].m = im;
	map[im].instance_id = instance_id;
	map[im].instance_src_map = m;
	map[im].flag.src4instance = 0; //clear
	map[m].flag.src4instance = 1; // Flag this map as a src map for instances

	RECREATE(instances[instance_id].map, unsigned short, ++instances[instance_id].num_map);

	instances[instance_id].map[instances[instance_id].num_map - 1] = im; // Attach to actual instance
	map_addmap2db(&map[im]);

	return im;
}

/*--------------------------------------
 * m : source map of this instance
 * party_id : source party of this instance
 * type : result (0 = map id | 1 = instance id)
 *--------------------------------------*/
int instance_map2imap(int m, int instance_id)
{
 	int i;

	if( !instance_is_valid(instance_id) )
		return -1;

	for( i = 0; i < instances[instance_id].num_map; i++ ){
		if( instances[instance_id].map[i] && map[instances[instance_id].map[i]].instance_src_map == m )
			return instances[instance_id].map[i];
 	}
 	return -1;
}

int instance_mapname2imap(const char *map_name, int instance_id) {
 	int i;
	
	nullpo_retr(-1, map_name);
	if( !instance_is_valid(instance_id) ) {
		return -1;
	}
	
	for( i = 0; i < instances[instance_id].num_map; i++ ) {
		if( instances[instance_id].map[i] && !strcmpi(map[map[instances[instance_id].map[i]].instance_src_map].name,map_name) )
			return instances[instance_id].map[i];
 	}
 	return -1;
}

/*--------------------------------------
 * m : source map
 * instance_id : where to search
 * result : mapid of map "m" in this instance
 *--------------------------------------*/
int instance_mapid2imapid(int m, int instance_id)
{
	Assert_retr(-1, m >= 0 && m < map_num);

	if( map[m].flag.src4instance == 0 )
		return m; // not instances found for this map
	else if( map[m].instance_id >= 0 )
	{ // This map is a instance, not a src map instance
		ShowError("instance_mapid2imapid: already instanced (%d / %d)\n", m, instance_id);
		return -1;
	}

	if( !instance_is_valid(instance_id) )
		return -1;

	return(instance_map2imap(m, instance_id));
}

/*--------------------------------------
 * map_instance_map_npcsub
 * Used on Init instance. Duplicates each script on source map
 *--------------------------------------*/
int instance_map_npcsub(struct block_list* bl, va_list args)
{
	struct npc_data* nd = (struct npc_data*)bl;
	int m = va_arg(args, int); // Destination Map

	if (npc_duplicate4instance(nd, m))
		ShowDebug("instance_map_npcsub:npc_duplicate4instance failed (%s/%d)\n", nd->name, m);

	return 1;
}

int instance_init_npc(struct block_list* bl, va_list args) {
	struct npc_data* nd;

	nullpo_retr(0, bl);
	nullpo_retr(0, args);
	nullpo_retr(0, nd = (struct npc_data *)bl);

	return npc_instanceinit(nd);
}

/*--------------------------------------
 * Init all map on the instance. Npcs are created here
 *--------------------------------------*/
void instance_init(int instance_id)
{
	int i;

	if( !instance_is_valid(instance_id) )
		return; // nothing to do

	for (i = 0; i < instances[instance_id].num_map; i++)
		map_foreachinmap(instance_map_npcsub, map[instances[instance_id].map[i]].instance_src_map, BL_NPC, instances[instance_id].map[i]);

	/* cant be together with the previous because it will rely on all of them being up */
	map_foreachininstance(instance_init_npc, instance_id, BL_NPC);

	instances[instance_id].state = INSTANCE_BUSY;
}

/*--------------------------------------
 * Used on instance deleting process.
 * Warps all players on each instance map to its save points.
 *--------------------------------------*/
int instance_del_load(struct map_session_data* sd, va_list args)
{
	int m = va_arg(args,int);
	if( !sd || sd->bl.m != m )
		return 0;

	pc_setpos(sd, sd->status.save_point.map, sd->status.save_point.x, sd->status.save_point.y, CLR_OUTSIGHT);
	return 1;
}

/* for npcs behave differently when being unloaded within a instance */
int instance_cleanup_sub(struct block_list *bl, va_list ap) {
	nullpo_ret(bl);

	switch (bl->type) {
	case BL_PC:
		map_quit((struct map_session_data *) bl);
		break;
	case BL_NPC:
		npc_unload((struct npc_data *)bl, true);
		break;
	case BL_MOB:
		unit_free(bl, CLR_OUTSIGHT);
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

/*--------------------------------------
 * Removes a simple instance map
 *--------------------------------------*/
void instance_del_map(int m)
{
	int i;

	if (m <= 0 || map[m].instance_id == -1) {
		ShowError("instance_del_map: tried to remove non-existing instance map (%d)\n", m);
		return;
	}

	map_foreachpc(instance_del_load, m);
	map_foreachinmap(instance_cleanup_sub, m, BL_ALL);

	if( map[m].mob_delete_timer != INVALID_TIMER )
		delete_timer(map[m].mob_delete_timer, map_removemobs_timer);

	mapindex_removemap(map_id2index(m));

	// Free memory
	aFree(map[m].cell);
	aFree(map[m].block);
	aFree(map[m].block_mob);

	if (map[m].qi_data)
		aFree(map[m].qi_data);

	// Remove from instance
	for (i = 0; i < instances[map[m].instance_id].num_map; i++) {
		if (instances[map[m].instance_id].map[i] == m) {
			instances[map[m].instance_id].num_map--;
			for (; i < instances[map[m].instance_id].num_map; i++)
				instances[map[m].instance_id].map[i] = instances[map[m].instance_id].map[i + 1];
			i = -1;
			break;
		}
	}

	if (i == instances[map[m].instance_id].num_map)
		ShowError("map_instance_del: failed to remove %s from instance list (%s): %d\n", map[m].name, instances[map[m].instance_id].name, m);

	map_removemapdb(&map[m]);
	memset(&map[m], 0x00, sizeof(map[0]));
	map[m].m = -1; // Marks this map as unallocated so server doesn't try to clean it up later on.
	map[m].name[0] = 0;
	map[m].instance_id = -1;
	map[m].mob_delete_timer = INVALID_TIMER;
}

/*--------------------------------------
 * Timer to destroy instance by process or idle
 *--------------------------------------*/
int instance_destroy_timer(int tid, int64 tick, int id, intptr_t data)
{
	instance_destroy(id);
	return 0;
}

/*--------------------------------------
 * Removes a instance, all its maps and npcs.
 *--------------------------------------*/
void instance_destroy(int instance_id)
{
	struct map_session_data *sd = NULL;
	unsigned short *icptr = NULL;
	struct party_data *p = NULL;
	struct guild *g = NULL;
	short *iptr = NULL;
	int j, last = 0;
	unsigned int now = (unsigned int)time(NULL);

	if (!instance_is_valid(instance_id))
		return; // nothing to do

	int type = 3; // other reason (default)
	bool idle = (instances[instance_id].users == 0);

	if (!idle && instances[instance_id].progress_timeout && instances[instance_id].progress_timeout <= now)
		type = 1; // progress timeout
	else if(idle && instances[instance_id].idle_timeout && instances[instance_id].idle_timeout <= now)
		type = 2; // idle timeout

	clif_instance(instance_id, 5, type); // Report users this instance has been destroyed

	switch (instances[instance_id].owner_type) {
	case IOT_NONE:
		break;
	case IOT_CHAR:
		if ((sd = map_id2sd(instances[instance_id].owner_id)) == NULL) {
			break;
		}
		iptr = sd->instance;
		icptr = &sd->instances;
		break;
	case IOT_PARTY:
		if ((p = party_search(instances[instance_id].owner_id)) == NULL) {
			break;
		}
		iptr = p->instance;
		icptr = &p->instances;
		break;
	case IOT_GUILD:
		if ((g = guild_search(instances[instance_id].owner_id)) == NULL) {
			break;
		}
		iptr = g->instance;
		icptr = &g->instances;
		break;
	default:
		ShowError("instance_destroy: unknown type %d for owner_id %d and name '%s'.\n", instances[instance_id].owner_type, instances[instance_id].owner_id, instances[instance_id].name);
		break;
	}

	if (iptr != NULL) {
		ARR_FIND(0, *icptr, j, iptr[j] == instance_id);
		if (j != *icptr)
			iptr[j] = -1;
	}

	// mark it as being destroyed so server doesn't try to give players more information about it
	instances[instance_id].state = INSTANCE_DESTROYING;

	if (instances[instance_id].map)
	{
		while (instances[instance_id].num_map && last != instances[instance_id].map[0]) { // Remove all maps from instance
			last = instances[instance_id].map[0];
			instance_del_map(instances[instance_id].map[0]);
		}
	}

	if (instances[instance_id].vars)
		db_destroy(instances[instance_id].vars);

	if (instances[instance_id].progress_timer != INVALID_TIMER)
		delete_timer(instances[instance_id].progress_timer, instance_destroy_timer);
	if (instances[instance_id].idle_timer != INVALID_TIMER)
		delete_timer(instances[instance_id].idle_timer, instance_destroy_timer);

	instances[instance_id].vars = NULL;

	if (instances[instance_id].map)
		aFree(instances[instance_id].map);


	// Clean up remains of the old instance and mark it as available for a new one
	memset(&instances[instance_id], 0x0, sizeof(instances[0]));
	instances[instance_id].map = NULL;
	instances[instance_id].state = INSTANCE_FREE;
}

/*--------------------------------------
 * Checks if there are users in the instance or not to start idle timer
 *--------------------------------------*/
void instance_check_idle(int instance_id)
{
	bool idle = true;
	unsigned int now = (unsigned int)time(NULL);

	if( !instance_is_valid(instance_id) || instances[instance_id].idle_timeoutval == 0 )
		return;

	if( instances[instance_id].users )
		idle = false;

	if (instances[instance_id].idle_timer != INVALID_TIMER && !idle) {
		delete_timer(instances[instance_id].idle_timer, instance_destroy_timer);
		instances[instance_id].idle_timer = INVALID_TIMER;
		instances[instance_id].idle_timeout = 0;
		clif_instance(instance_id, 3, 0); // Notify instance users normal instance expiration
	}
	else if (instances[instance_id].idle_timer == INVALID_TIMER && idle) {
		instances[instance_id].idle_timeout = now + instances[instance_id].idle_timeoutval;
		instances[instance_id].idle_timer = add_timer(gettick() + instances[instance_id].idle_timeoutval * 1000, instance_destroy_timer, instance_id, 0);
		clif_instance(instance_id, 4, 0); // Notify instance users it will be destroyed of no user join it again in "X" time
	}
}

/*--------------------------------------
 * Set instance Timers
 *--------------------------------------*/
void instance_set_timeout(int instance_id, unsigned int progress_timeout, unsigned int idle_timeout)
{
	unsigned int now = (unsigned int)time(NULL);

	if( !instance_is_valid(instance_id) )
		return;
		
	if (instances[instance_id].progress_timer != INVALID_TIMER)
		delete_timer(instances[instance_id].progress_timer, instance_destroy_timer);
	if (instances[instance_id].idle_timer != INVALID_TIMER)
		delete_timer(instances[instance_id].idle_timer, instance_destroy_timer);

	if (progress_timeout) {
		instances[instance_id].progress_timeout = now + progress_timeout;
		instances[instance_id].progress_timer = add_timer(gettick() + progress_timeout * 1000, instance_destroy_timer, instance_id, 0);
		instances[instance_id].original_progress_timeout = progress_timeout;
	}
	else {
		instances[instance_id].progress_timeout = 0;
		instances[instance_id].progress_timer = INVALID_TIMER;
		instances[instance_id].original_progress_timeout = 0;
	}
		
	if (idle_timeout) {
		instances[instance_id].idle_timeoutval = idle_timeout;
		instances[instance_id].idle_timer = INVALID_TIMER;
		instance_check_idle(instance_id);
	}
	else {
		instances[instance_id].idle_timeoutval = 0;
		instances[instance_id].idle_timeout = 0;
		instances[instance_id].idle_timer = INVALID_TIMER;
	}

	if (instances[instance_id].idle_timer == INVALID_TIMER && instances[instance_id].progress_timer != INVALID_TIMER)
		clif_instance(instance_id, 3, 0);
}

/*--------------------------------------
 * Checks if sd in on a instance and should be kicked from it
 *--------------------------------------*/
void instance_check_kick(struct map_session_data *sd)
{
	int m = sd->bl.m;

	nullpo_retv(sd);

	clif_instance_leave(sd->fd);
	if (map[m].instance_id >= 0) { // User was on the instance map
		if( map[m].save.map )
			pc_setpos(sd, map[m].save.map, map[m].save.x, map[m].save.y, CLR_TELEPORT);
		else
			pc_setpos(sd, sd->status.save_point.map, sd->status.save_point.x, sd->status.save_point.y, CLR_TELEPORT);
	}
}

/**
 * Look up existing memorial dungeon of the player and destroy it
 *
 * @param sd session data.
 *
 */
void instance_force_destroy(struct map_session_data *sd)
{
	nullpo_retv(sd);

	for (int i = 0; i < instance_count; ++i) {
		switch (instances[i].owner_type) {
		case IOT_CHAR:
		{
			if (instances[i].owner_id != sd->status.account_id)
				continue;
			break;
		}
		case IOT_PARTY:
		{
			int party_id = sd->status.party_id;
			if (instances[i].owner_id != party_id)
				continue;
			int j = 0;
			struct party_data *pt = party_search(party_id);
			nullpo_retv(pt);

			ARR_FIND(0, MAX_PARTY, j, pt->party.member[j].leader);
			if (j == MAX_PARTY) {
				ShowWarning("clif_parse_memorial_dungeon_command: trying to destroy a party instance, while the party has no leader.");
				return;
			}
			if (pt->party.member[j].char_id != sd->status.char_id) {
				ShowWarning("clif_parse_memorial_dungeon_command: trying to destroy a party instance, from a non party-leader player.");
				return;
			}
			break;
		}
		case IOT_GUILD:
		{
			int guild_id = sd->status.guild_id;
			if (instances[i].owner_id != guild_id)
				continue;
			struct guild *g = guild_search(guild_id);
			nullpo_retv(g);

			if (g->member[0].char_id != sd->status.char_id) {
				ShowWarning("clif_parse_memorial_dungeon_command: trying to destroy a guild instance, from a non guild-master player.");
				return;
			}
			break;
		}
		default:
			continue;
		}
		instance_destroy(instances[i].id);
		return;
	}
}

/**
 * reloads the map flags from the source map
 *
 * @param instance_id
 */
void instance_reload_map_flags(int instance_id)
{
	Assert_retv(instance_is_valid(instance_id));

	const struct instance_data *curInst = &instances[instance_id];

	for (int i = 0; i < curInst->num_map; i++) {
		struct map_data *dstMap = &map[curInst->map[i]];
		const struct map_data *srcMap = &map[dstMap->instance_src_map];

		memcpy(&dstMap->flag, &srcMap->flag, sizeof(struct map_flag));

		dstMap->flag.src4instance = 0;
	}
}

void do_reload_instance(void) {
	struct s_mapiterator *iter;
	struct map_session_data *sd;
	int i, k;

	do_final_instance();
	
	for(i = 0; i < instance_count; i++) {
		if (!instance_is_valid(i))
			continue; // don't try to restart an invalid instance

		for(k = 0; k < instances->num_map; k++) {
			if( !map[map[instances[i].map[k]].instance_src_map].flag.src4instance )
				break;
		}
		
		if( k != instances[i].num_map ) /* any (or all) of them were disabled, we destroy */
			instance_destroy(i);
		else {
			/* populate the instance again */
			instance_init(i);
			instance_reload_map_flags(i);
			/* restart timers */
			instance_set_timeout(i,instances[i].original_progress_timeout,instances[i].idle_timeoutval);
		}
	}
	
	iter = mapit_getallusers();
	for( sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); sd = (TBL_PC*)mapit_next(iter) ) {
		if(sd && map[sd->bl.m].instance_id)
			pc_setpos(sd,instances[map[sd->bl.m].instance_id].respawn.map,instances[map[sd->bl.m].instance_id].respawn.x,instances[map[sd->bl.m].instance_id].respawn.y,CLR_TELEPORT);
	}
	mapit_free(iter);
}

void do_final_instance(void)
{
	int i;

	for( i = 0; i < instance_count; i++ )
		instance_destroy(i);

	if (instances)
		aFree(instances);

	instance_count = 0;
}

void do_init_instance(void)
{
	add_timer_func_list(instance_destroy_timer, "instance_destroy_timer");
}
