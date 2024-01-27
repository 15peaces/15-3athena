// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/malloc.h"

#include "../common/nullpo.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"

#include "itemdb.h"
#include "map.h"
#include "pc.h"
#include "npc.h"
#include "itemdb.h"
#include "script.h"
#include "intif.h"
#include "battle.h"
#include "mob.h"
#include "party.h"
#include "unit.h"
#include "log.h"
#include "clif.h"
#include "quest.h"
#include "intif.h"
#include "chrif.h"
#include "intif.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

/**
 * Searches a quest by ID.
 *
 * @param quest_id ID to lookup
 * @return Quest entry (equals to &quest_dummy if the ID is invalid)
 */
struct quest_db *quest_db(int quest_id) {
	if (quest_id < 0 || quest_id > MAX_QUEST_DB || quest_db_data[quest_id] == NULL)
		return &quest_dummy;
	return quest_db_data[quest_id];
}

/**
 * Sends quest info to the player on login.
 *
 * @param sd Player's data
 * @return 0 in case of success, nonzero otherwise (i.e. the player has no quests)
 */
int quest_pc_login(TBL_PC *sd) {
	int i;

	if(sd->avail_quests == 0)
		return 1;

	clif_quest_send_list(sd);
#if PACKETVER < 20141022
	clif_quest_send_mission(sd);
#endif

	//@TODO: Is this necessary? Does quest_send_mission not take care of this?
	for (i = 0; i < sd->avail_quests; i++)
		clif_quest_update_objective(sd, &sd->quest_log[i]);

	return 0;
}

/**
 * Adds a quest to the player's list.
 *
 * New quest will be added as Q_ACTIVE.
 *
 * @param sd       Player's data
 * @param quest_id ID of the quest to add.
 * @return 0 in case of success, nonzero otherwise
 */
int quest_add(TBL_PC *sd, int quest_id) {
	int n;
	struct quest_db *qi = quest_db(quest_id);

	if (qi == &quest_dummy) {
		ShowError("quest_add: quest %d not found in DB.\n", quest_id);
		return -1;
	}

	if (quest_check(sd, quest_id, HAVEQUEST) >= 0) {
		ShowError("quest_add: Character %d already has quest %d.\n", sd->status.char_id, quest_id);
		return -1;
	}

	n = sd->avail_quests; //Insertion point

	sd->num_quests++;
	sd->avail_quests++;
	RECREATE(sd->quest_log, struct quest, sd->num_quests);

	//The character has some completed quests, make room before them so that they will stay at the end of the array
	if (sd->avail_quests != sd->num_quests)
		memmove(&sd->quest_log[n + 1], &sd->quest_log[n], sizeof(struct quest) * (sd->num_quests - sd->avail_quests));

	memset(&sd->quest_log[n], 0, sizeof(struct quest));

	sd->quest_log[n].quest_id = qi->id;
	if (qi->time)
		sd->quest_log[n].time = (unsigned int)(time(NULL) + qi->time);
	sd->quest_log[n].state = Q_ACTIVE;

	sd->save_quest = true;

	clif_quest_add(sd, &sd->quest_log[n]);
	clif_quest_update_objective(sd, &sd->quest_log[n]);

	if( save_settings&64 )
		chrif_save(sd, CSAVE_NORMAL);

	return 0;
}

/**
 * Replaces a quest in a player's list with another one.
 *
 * @param sd   Player's data
 * @param qid1 Current quest to replace
 * @param qid2 New quest to add
 * @return 0 in case of success, nonzero otherwise
 */
int quest_change(TBL_PC *sd, int qid1, int qid2) {
	int i;
	struct quest_db *qi = quest_db(qid2);

	if (qi == &quest_dummy) {
		ShowError("quest_change: quest %d not found in DB.\n", qid2);
		return -1;
	}

	if (quest_check(sd, qid2, HAVEQUEST) >= 0) {
		ShowError("quest_change: Character %d already has quest %d.\n", sd->status.char_id, qid2);
		return -1;
	}

	if (quest_check(sd, qid1, HAVEQUEST) < 0) {
		ShowError("quest_change: Character %d doesn't have quest %d.\n", sd->status.char_id, qid1);
		return -1;
	}

	ARR_FIND(0, sd->avail_quests, i, sd->quest_log[i].quest_id == qid1);
	if (i == sd->avail_quests) {
		ShowError("quest_change: Character %d has completed quest %d.\n", sd->status.char_id, qid1);
		return -1;
	}

	memset(&sd->quest_log[i], 0, sizeof(struct quest));
	sd->quest_log[i].quest_id = qi->id;
	if (qi->time)
		sd->quest_log[i].time = (unsigned int)(time(NULL) + qi->time);
	sd->quest_log[i].state = Q_ACTIVE;

	sd->save_quest = true;

	clif_quest_delete(sd, qid1);
	clif_quest_add(sd, &sd->quest_log[i]);
	clif_quest_update_objective(sd, &sd->quest_log[i]);

	if( save_settings&64 )
		chrif_save(sd, CSAVE_NORMAL);

	return 0;
}

/**
 * Removes a quest from a player's list
 *
 * @param sd       Player's data
 * @param quest_id ID of the quest to remove
 * @return 0 in case of success, nonzero otherwise
 */
int quest_delete(TBL_PC *sd, int quest_id) {
	int i;

	//Search for quest
	ARR_FIND(0, sd->num_quests, i, sd->quest_log[i].quest_id == quest_id);
	if(i == sd->num_quests)
	{
		ShowError("quest_delete: Character %d doesn't have quest %d.\n", sd->status.char_id, quest_id);
		return -1;
	}

	if( sd->quest_log[i].state != Q_COMPLETE )
		sd->avail_quests--;
	if (i < --sd->num_quests) //Compact the array
		memmove(&sd->quest_log[i], &sd->quest_log[i + 1], sizeof(struct quest) * (sd->num_quests - i));
	if (sd->num_quests == 0) {
		aFree(sd->quest_log);
		sd->quest_log = NULL;
	}
	else
		RECREATE(sd->quest_log, struct quest, sd->num_quests);
	sd->save_quest = true;

	clif_quest_delete(sd, quest_id);

	if( save_settings&64 )
		chrif_save(sd, CSAVE_NORMAL);

	return 0;
}

int quest_update_objective_sub(struct block_list *bl, va_list ap)
{
	struct map_session_data * sd;
	int mob_id, party_id;

	nullpo_ret(bl);
	nullpo_ret(sd = (struct map_session_data *)bl);

	party_id = va_arg(ap, int);
	mob_id = va_arg(ap, int);

	if( !sd->avail_quests )
		return 0;
	if( sd->status.party_id != party_id )
		return 0;

	quest_update_objective(sd, mob_id);

	return 1;
}

/**
* Updates the quest objectives for a character after killing a monster, including the handling of quest-granted drops.
* @param sd : Character's data
* @param mob_id : Monster ID
*/
void quest_update_objective(TBL_PC * sd, int mob_id)
{
	int i,j;

	for( i = 0; i < sd->avail_quests; i++ )
	{
		struct quest_db *qi = NULL;

		if (sd->quest_log[i].state != Q_ACTIVE) // Skip inactive quests
			continue;
		
		qi = quest_db(sd->quest_log[i].quest_id);

		for( j = 0; j < qi->objectives_count; j++ )
			if(qi->objectives[j].mob == mob_id && sd->quest_log[i].count[j] < qi->objectives[j].count)
			{
				sd->quest_log[i].count[j]++;
				sd->save_quest = true;
				clif_quest_update_objective(sd,&sd->quest_log[i]);
			}

		// process quest-granted extra drop bonuses
		for (j = 0; j < qi->dropitem_count; j++) {
			struct quest_dropitem *dropitem = &qi->dropitem[j];
			struct item item;
			int temp;

			if (dropitem->mob_id != 0 && dropitem->mob_id != mob_id)
				continue;
			// TODO: Should this be affected by server rates?
			if (dropitem->rate < 10000 && rnd() % 10000 >= dropitem->rate)
				continue;
			if (!itemdb_exists(dropitem->nameid))
				continue;

			memset(&item, 0, sizeof(item));
			item.nameid = dropitem->nameid;
			item.identify = itemdb_isidentified(dropitem->nameid);
			item.amount = dropitem->count;
#ifdef BOUND_ITEMS
			item.bound = dropitem->bound;
#endif
			temp = pc_additem(sd, &item, 1, LOG_TYPE_QUEST);
			if (temp != 0) // Failed to obtain the item
				clif_additem(sd, 0, 0, temp);
			else if (dropitem->isAnnounced) {
				char output[CHAT_SIZE_MAX];

				sprintf(output, msg_txt(sd,717), sd->status.name, itemdb_jname(item.nameid), StringBuf_Value(&qi->name));
				intif_broadcast(output, strlen(output) + 1, BC_DEFAULT);
			}
		}
	}
}

/**
 * Updates a quest's state.
 *
 * Only status of active and inactive quests can be updated. Completed quests can't (for now). [Inkfish]
 *
 * @param sd       Character's data
 * @param quest_id Quest ID to update
 * @param qs       New quest state
 * @return 0 in case of success, nonzero otherwise
 */
int quest_update_status(TBL_PC *sd, int quest_id, enum quest_state status) {
	int i;

	//Only status of active and inactive quests can be updated. Completed quests can't (for now). [Inkfish]
	ARR_FIND(0, sd->avail_quests, i, sd->quest_log[i].quest_id == quest_id);
	if(i == sd->avail_quests)
	{
		ShowError("quest_update_status: Character %d doesn't have quest %d.\n", sd->status.char_id, quest_id);
		return -1;
	}

	sd->quest_log[i].state = status;
	sd->save_quest = true;

	if( status < Q_COMPLETE )
	{
		clif_quest_update_status(sd, quest_id, status == Q_ACTIVE ? true : false);
		return 0;
	}

	if( i != (--sd->avail_quests) )
	{
		struct quest tmp_quest;
		memcpy(&tmp_quest, &sd->quest_log[i],sizeof(struct quest));
		memcpy(&sd->quest_log[i], &sd->quest_log[sd->avail_quests],sizeof(struct quest));
		memcpy(&sd->quest_log[sd->avail_quests], &tmp_quest,sizeof(struct quest));
	}

	clif_quest_delete(sd, quest_id);

	if( save_settings&64 )
		chrif_save(sd, CSAVE_NORMAL);

	return 0;
}

int quest_check(TBL_PC * sd, int quest_id, enum quest_check_type type)
{
	int i;

	ARR_FIND(0, sd->num_quests, i, sd->quest_log[i].quest_id == quest_id);
	if(i == sd->num_quests)
		return -1;

	switch( type )
	{
	case HAVEQUEST:		
		return sd->quest_log[i].state;
	case PLAYTIME:
		return (sd->quest_log[i].time < (unsigned int)time(NULL) ? 2 : sd->quest_log[i].state == Q_COMPLETE ? 1 : 0);
	case HUNTING:
		if (sd->quest_log[i].state == Q_INACTIVE || sd->quest_log[i].state == Q_ACTIVE) {
			int j;
			struct quest_db *qi = quest_db(sd->quest_log[i].quest_id);

			ARR_FIND(0, qi->objectives_count, j, sd->quest_log[i].count[j] < qi->objectives[j].count);
			if (j == qi->objectives_count)
				return 2;
			if (sd->quest_log[i].time < (unsigned int)time(NULL))
				return 1;
		}
		return 0;
	default:
		ShowError("quest_check_quest: Unknown parameter %d",type);
		break;
	}

	return -1;
}

/**
* Loads quests from the quest db.txt
* @return Number of loaded quests, or -1 if the file couldn't be read.
*/
int quest_read_db(void)
{
	FILE *fp;
	char line[1024];
	uint32 count = 0;
	uint32 ln = 0;
	struct quest_db entry;

	sprintf(line, "%s/quest_db.txt", db_path);
	if( (fp=fopen(line,"r"))==NULL ){
		ShowError("can't read %s\n", line);
		return -1;
	}
	
	while(fgets(line, sizeof(line), fp))
	{
		char *str[19], *p;
		uint16 quest_id = 0;
		uint8 i;
		
		++ln;
		if (line[0] == '\0' || (line[0] == '/' && line[1] == '/'))
			continue;
		
		p = trim(line);

		if (*p == '\0')
			continue; // empty line

		memset(str, 0, sizeof(str));

		for( i = 0, p = line; i < 18 && p; i++)
		{
			str[i] = p;
			p = strchr(p, ',');
			if (p)
				*p++ = 0;
		}
		if (str[0] == NULL)
			continue;
		if (i < 18) 
		{
			ShowError("quest_read_db: Insufficient columns in line %d (%d of %d)\n", ln, i, 18);
			continue;
		}

		memset(&entry, 0, sizeof(entry));

		entry.id = atoi(str[0]);

		if (entry.id < 0 || entry.id >= INT_MAX) {
			ShowError("quest_read_db: Invalid quest ID '%d' in line '%s' (min: 0, max: %d.)\n", entry.id, ln, INT_MAX);
			continue;
		}

		entry.time = atoi(str[1]);

		for( i = 0; i < MAX_QUEST_OBJECTIVES; i++ )
		{
			uint16 mob_id = (uint16)mobdb_checkid(atoi(str[2 * i + 2]));

			memset(&entry.objectives[i], 0, sizeof(entry.objectives[0]));
			entry.objectives[i].mob = mob_id;
			entry.objectives[i].count = atoi(str[2 * i + 3]);
			entry.objectives[i].min_level = 0;
			entry.objectives[i].max_level = 0;
			// These values are dummy for now...
			entry.objectives[i].race = RC_ALL;
			entry.objectives[i].size = 3;
			entry.objectives[i].element = ELE_ALL;
			
			if (mob_id) // Only count valid objectives.
				entry.objectives_count++;
		}

		for (i = 0; i < MAX_QUEST_DROPS; i++)
		{
			uint16 mob_id = (uint16)mobdb_checkid(atoi(str[3 * i + (2 * MAX_QUEST_OBJECTIVES + 2)]));
			uint16 nameid = (uint16)atoi(str[3 * i + (2 * MAX_QUEST_OBJECTIVES + 3)]);
			if (!nameid)
				nameid = 0;
			else if (!itemdb_exists(nameid)) {
				ShowWarning("quest_read_db: Invalid item reward '%d' (mob %d, optional) in line %d.\n", nameid, mob_id, ln);
			}

			memset(&entry.dropitem[i], 0, sizeof(entry.dropitem[0]));
			entry.dropitem[i].mob_id = mob_id;
			entry.dropitem[i].nameid = nameid;
			entry.dropitem[i].rate = atoi(str[3 * i + (2 * MAX_QUEST_OBJECTIVES + 4)]);
			
			if (nameid) // Only count valid items.
				entry.dropitem_count++;
		}

		if (quest_db_data[entry.id] == NULL)
			quest_db_data[entry.id] = aMalloc(sizeof(struct quest_db));

		memcpy(quest_db_data[entry.id], &entry, sizeof(struct quest_db));
		count++;
	}
	fclose(fp);
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, "quest_db.txt");
	return 0;
}

/**
 * Map iterator to ensures a player has no invalid quest log entries.
 *
 * Any entries that are no longer in the db are removed.
 *
 * @see map_foreachpc
 * @param ap Ignored
 */
int quest_reload_check_sub(struct map_session_data *sd, va_list ap) {
	int i, j;

	nullpo_ret(sd);

	j = 0;
	for (i = 0; i < sd->num_quests; i++) {
		struct quest_db *qi = quest_db(sd->quest_log[i].quest_id);

		if (qi == &quest_dummy) { //Remove no longer existing entries
			if (sd->quest_log[i].state != Q_COMPLETE) //And inform the client if necessary
				clif_quest_delete(sd, sd->quest_log[i].quest_id);
			continue;
		}
		if (i != j) {
			//Move entries if there's a gap to fill
			memcpy(&sd->quest_log[j], &sd->quest_log[i], sizeof(struct quest));
		}
		j++;
	}
	sd->num_quests = j;
	ARR_FIND(0, sd->num_quests, i, sd->quest_log[i].state == Q_COMPLETE);
	sd->avail_quests = i;

	return 1;
}

/**
 * Clears the quest database for shutdown or reload.
 */
void quest_clear_db(void) {
	int i;

	for (i = 0; i < MAX_QUEST_DB; i++) {
		if (quest_db_data[i]) {
			aFree(quest_db_data[i]);
			quest_db_data[i] = NULL;
		}
	}
}

/**
 * Initializes the quest interface.
 */
void do_init_quest(void)
{
	quest_read_db();
}

/**
 * Finalizes the quest interface before shutdown.
 */
void do_final_quest(void) {
	memset(&quest_dummy, 0, sizeof(quest_dummy));

	quest_clear_db();
}

/**
 * Reloads the quest database.
 */
void do_reload_quest(void) {
	memset(&quest_dummy, 0, sizeof(quest_dummy));

	quest_clear_db();

	quest_read_db();

	//Update quest data for players, to ensure no entries about removed quests are left over.
	map_foreachpc(&quest_reload_check_sub);
}
