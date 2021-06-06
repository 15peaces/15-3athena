// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/malloc.h"
#include "../common/version.h"
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


struct s_quest_db quest_db[MAX_QUEST_DB];

int quest_search_db(int quest_id)
{
	int i;

	ARR_FIND(0, MAX_QUEST_DB,i,quest_id == quest_db[i].id);
	if( i == MAX_QUEST_DB )
		return -1;

	return i;
}

//Send quest info on login
int quest_pc_login(TBL_PC * sd)
{
	if(sd->avail_quests == 0)
		return 1;

	clif_quest_send_list(sd);
#if PACKETVER < 20141022
	clif_quest_send_mission(sd);
#endif

	return 0;
}

int quest_add(TBL_PC * sd, int quest_id)
{

	int i, j;

	if( sd->num_quests >= MAX_QUEST_DB )
	{
		ShowError("quest_add: Character %d has got all the quests.(max quests: %d)\n", sd->status.char_id, MAX_QUEST_DB);
		return 1;
	}

	if( quest_check(sd, quest_id, HAVEQUEST) >= 0 )
	{
		ShowError("quest_add: Character %d already has quest %d.\n", sd->status.char_id, quest_id);
		return -1;
	}

	if( (j = quest_search_db(quest_id)) < 0 )
	{
		ShowError("quest_add: quest %d not found in DB.\n", quest_id);
		return -1;
	}

	i = sd->avail_quests;
	memmove(&sd->quest_log[i+1], &sd->quest_log[i], sizeof(struct quest)*(sd->num_quests-sd->avail_quests));
	memmove(sd->quest_index+i+1, sd->quest_index+i, sizeof(int)*(sd->num_quests-sd->avail_quests));

	memset(&sd->quest_log[i], 0, sizeof(struct quest));
	sd->quest_log[i].quest_id = quest_db[j].id;
	if( quest_db[j].time )
		sd->quest_log[i].time = (unsigned int)(time(NULL) + quest_db[j].time);
	sd->quest_log[i].state = Q_ACTIVE;

	sd->quest_index[i] = j;
	sd->num_quests++;
	sd->avail_quests++;
	sd->save_quest = true;

	clif_quest_add(sd, &sd->quest_log[i], sd->quest_index[i]);
	clif_quest_update_objective(sd, &sd->quest_log[i], sd->quest_index[i]);

	if( save_settings&64 )
		chrif_save(sd,0);

	return 0;
}

int quest_change(TBL_PC * sd, int qid1, int qid2)
{

	int i, j;

	if( quest_check(sd, qid2, HAVEQUEST) >= 0 )
	{
		ShowError("quest_change: Character %d already has quest %d.\n", sd->status.char_id, qid2);
		return -1;
	}

	if( quest_check(sd, qid1, HAVEQUEST) < 0 )
	{
		ShowError("quest_change: Character %d doesn't have quest %d.\n", sd->status.char_id, qid1);
		return -1;
	}

	if( (j = quest_search_db(qid2)) < 0 )
	{
		ShowError("quest_change: quest %d not found in DB.\n",qid2);
		return -1;
	}

	ARR_FIND(0, sd->avail_quests, i, sd->quest_log[i].quest_id == qid1);
	if(i == sd->avail_quests)
	{
		ShowError("quest_change: Character %d has completed quests %d.\n", sd->status.char_id, qid1);
		return -1;
	}

	memset(&sd->quest_log[i], 0, sizeof(struct quest));
	sd->quest_log[i].quest_id = quest_db[j].id;
	if( quest_db[j].time )
		sd->quest_log[i].time = (unsigned int)(time(NULL) + quest_db[j].time);
	sd->quest_log[i].state = Q_ACTIVE;

	sd->quest_index[i] = j;
	sd->save_quest = true;

	clif_quest_delete(sd, qid1);
	clif_quest_add(sd, &sd->quest_log[i], sd->quest_index[i]);

	if( save_settings&64 )
		chrif_save(sd,0);

	return 0;
}

int quest_delete(TBL_PC * sd, int quest_id)
{
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
	if( sd->num_quests-- < MAX_QUEST_DB && sd->quest_log[i+1].quest_id )
	{
		memmove(&sd->quest_log[i], &sd->quest_log[i+1], sizeof(struct quest)*(sd->num_quests-i));
		memmove(sd->quest_index+i, sd->quest_index+i+1, sizeof(int)*(sd->num_quests-i));
	}
	memset(&sd->quest_log[sd->num_quests], 0, sizeof(struct quest));
	sd->quest_index[sd->num_quests] = 0;
	sd->save_quest = true;

	clif_quest_delete(sd, quest_id);

	if( save_settings&64 )
		chrif_save(sd,0);

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
		if( sd->quest_log[i].state != Q_ACTIVE )
			continue;

		for( j = 0; j < quest_db[sd->quest_index[i]].objectives_count; j++ )
			if( quest_db[sd->quest_index[i]].objectives[j].mob == mob_id && sd->quest_log[i].count[j] < quest_db[sd->quest_index[i]].objectives[j].count)
			{
				sd->quest_log[i].count[j]++;
				sd->save_quest = true;
				clif_quest_update_objective(sd,&sd->quest_log[i],sd->quest_index[i]);
			}

		// process quest-granted extra drop bonuses
		for (j = 0; j < quest_db[sd->quest_index[i]].dropitem_count; j++) {
			struct quest_dropitem *dropitem = &quest_db[sd->quest_index[i]].dropitem[j];
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
			temp = pc_additem(sd, &item, 1);
			log_pick(&sd->bl, LOG_TYPE_QUEST, dropitem->nameid, 1, &item);
			if (temp != 0) // Failed to obtain the item
				clif_additem(sd, 0, 0, temp);
			else if (dropitem->isAnnounced) {
				char output[CHAT_SIZE_MAX];

				sprintf(output, msg_txt(717), sd->status.name, itemdb_jname(item.nameid), StringBuf_Value(&quest_db[sd->quest_index[i]].name));
				intif_broadcast(output, strlen(output) + 1, BC_DEFAULT);
			}
		}
	}
}

int quest_update_status(TBL_PC * sd, int quest_id, quest_state status)
{
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
		chrif_save(sd,0);

	return 0;
}

int quest_check(TBL_PC * sd, int quest_id, quest_check_type type)
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
		{
			int j;
			ARR_FIND(0, quest_db[sd->quest_index[i]].objectives_count, j, sd->quest_log[i].count[j] < quest_db[sd->quest_index[i]].objectives[j].count);
			if( j == quest_db[sd->quest_index[i]].objectives_count )
				return 2;
			if( sd->quest_log[i].time < (unsigned int)time(NULL) )
				return 1;
			return 0;
		}
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

		if (count == MAX_QUEST_DB) {
			ShowError("quest_read_db: Too many entries specified in %s/quest_db.txt!\n", db_path);
			break;
		}
		
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

		quest_id = atoi(str[0]);

		if (quest_id < 0 || quest_id >= INT_MAX) {
			ShowError("quest_read_db: Invalid quest ID '%d' in line '%s' (min: 0, max: %d.)\n", quest_id, ln, INT_MAX);
			continue;
		}

		memset(&quest_db[count], 0, sizeof(quest_db[0]));

		quest_db[count].id = quest_id;
		quest_db[count].time = atoi(str[1]);

		for( i = 0; i < MAX_QUEST_OBJECTIVES; i++ )
		{
			uint16 mob_id = (uint16)mobdb_checkid(atoi(str[2 * i + 2]));

			memset(&quest_db[count].objectives[i], 0, sizeof(quest_db[count].objectives[0]));
			quest_db[count].objectives[i].mob = mob_id;
			quest_db[count].objectives[i].count = atoi(str[2*i+3]);
			quest_db[count].objectives[i].min_level = 0;
			quest_db[count].objectives[i].max_level = 0;
			quest_db[count].objectives[i].race = RC_ALL;
			quest_db[count].objectives[i].size = 3;
			quest_db[count].objectives[i].element = ELE_ALL;
			quest_db[count].objectives_count++;
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

			memset(&quest_db[count].dropitem[i], 0, sizeof(quest_db[count].dropitem[0]));
			quest_db[count].dropitem[i].mob_id = mob_id;
			quest_db[count].dropitem[i].nameid = nameid;
			quest_db[count].dropitem[i].rate = atoi(str[3 * i + (2 * MAX_QUEST_OBJECTIVES + 4)]);
			quest_db[count].dropitem_count++;
		}

		//StringBuf_Printf(&quest_db[count].name, "%s", str[17]);
		//ShowDebug("quest_read_db: ID: %d, time: %d, obj1: %d(%d), drop1: %d(%d)\n", quest_db[count].id, quest_db[count].time, quest_db[count].objectives[0].mob, quest_db[count].objectives[0].count, quest_db[count].dropitem[0].nameid, quest_db[count].dropitem[0].rate);
		count++;
	}
	fclose(fp);
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, "quest_db.txt");
	return 0;
}

void do_init_quest(void)
{
	quest_read_db();
}
