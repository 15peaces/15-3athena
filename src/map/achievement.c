// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "achievement.h"

#include "itemdb.h"
#include "mob.h"
#include "party.h"
#include "pc.h"
#include "script.h"

#include "../common/db.h"
#include "../common/malloc.h"
#include "../common/nullpo.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Retrieve an achievement via it's ID.
 * @param aid as the achievement ID.
 * @return NULL or pointer to the achievement data.
 */
const struct achievement_data *achievement_get(int aid)
{
	return (struct achievement_data *) idb_get(achievement_db, aid);
}

/**
 * Searches the provided achievement data for an achievement,
 * optionally creates a new one if no key exists.
 * @param[in] sd       a pointer to map_session_data.
 * @param[in] aid      ID of the achievement provided as key.
 * @param[in] create   new key creation flag.
 * @return pointer to the session's achievement data.
 */
struct achievement *achievement_ensure(struct map_session_data *sd, const struct achievement_data *ad)
{
	struct achievement *s_ad = NULL;
	int i = 0;

	nullpo_retr(NULL, sd);
	nullpo_retr(NULL, ad);

	/* Lookup for achievement entry */
	ARR_FIND(0, VECTOR_LENGTH(sd->achievement), i, (s_ad = &VECTOR_INDEX(sd->achievement, i)) && s_ad->id == ad->id);

	if (i == VECTOR_LENGTH(sd->achievement)) {
		struct achievement ta = { 0 };
		ta.id = ad->id;

		VECTOR_ENSURE(sd->achievement, 1, 1);
		VECTOR_PUSH(sd->achievement, ta);

		s_ad = &VECTOR_LAST(sd->achievement);
	}

	return s_ad;
}

/**
 * Calculates the achievement's totals via reference.
 * @param[in] sd               pointer to map_session_data
 * @param[out] tota_points      pointer to total points var
 * @param[out] completed        pointer to total var
 * @param[out] rank             pointer to completed var
 * @param[out] curr_rank_points pointer to achievement rank var
 */
void achievement_calculate_totals(const struct map_session_data *sd, int *total_points, int *completed, int *rank, int *curr_rank_points)
{
	const struct achievement *a = NULL;
	const struct achievement_data *ad = NULL;
	int tmp_curr_points = 0;
	int tmp_total_points = 0;
	int tmp_total_completed = 0;
	int tmp_rank = 0;
	int i = 0;

	nullpo_retv(sd);

	for (i = 0; i < VECTOR_LENGTH(sd->achievement); i++) {
		a = &VECTOR_INDEX(sd->achievement, i);

		if ((ad = achievement_get(a->id)) == NULL)
			continue;

		if (a->completed != 0) {
			tmp_total_points += ad->points;
			tmp_total_completed++;
		}
	}

	if (tmp_total_points > 0) {
		tmp_curr_points = tmp_total_points;
		for (i = 0; i < MAX_ACHIEVEMENT_RANKS
			&& tmp_curr_points >= VECTOR_INDEX(rank_exp, i)
			&& i < VECTOR_LENGTH(rank_exp); i++) {
			tmp_curr_points -= VECTOR_INDEX(rank_exp, i);
			tmp_rank++;
		}
	}

	if (total_points != NULL)
		*total_points = tmp_total_points;

	if (completed != NULL)
		*completed = tmp_total_completed;

	if (rank != NULL)
		*rank = tmp_rank;

	if (curr_rank_points != NULL)
		*curr_rank_points = tmp_curr_points;
}

/**
 * Checks whether all objectives of the achievement are completed.
 * @param[in] sd       as the map_session_data pointer
 * @param[in] ad       as the achievement_data pointer
 * @return true if complete, false if not.
 */
bool achievement_check_complete(struct map_session_data *sd, const struct achievement_data *ad)
{
	int i;
	struct achievement *ach = NULL;

	nullpo_retr(false, sd);
	nullpo_retr(false, ad);

	if ((ach = achievement_ensure(sd, ad)) == NULL)
		return false;
	for (i = 0; i < VECTOR_LENGTH(ad->objective); i++)
		if (ach->objective[i] < VECTOR_INDEX(ad->objective, i).goal)
			return false;

	return true;
}

/**
 * Compares the progress of an objective against it's goal.
 * Increments the progress of the objective by the specified amount, towards the goal.
 * @param sd         [in] as a pointer to map_session_data
 * @param ad         [in] as a pointer to the achievement_data
 * @param obj_idx    [in] as the index of the objective.
 * @param progress   [in] as the progress of the objective to be added.
 */
static void achievement_progress_add(struct map_session_data *sd, const struct achievement_data *ad, unsigned int obj_idx, int progress)
{
	struct achievement *ach = NULL;

	nullpo_retv(sd);
	nullpo_retv(ad);

	Assert_retv(progress != 0);
	Assert_retv(obj_idx < VECTOR_LENGTH(ad->objective));

	if ((ach = achievement_ensure(sd, ad)) == NULL)
		return;

	if (ach->completed)
		return; // ignore the call if the achievement is completed.

	// Check and increment the objective count.
	if (!ach->objective[obj_idx] || ach->objective[obj_idx] < VECTOR_INDEX(ad->objective, obj_idx).goal) {
		ach->objective[obj_idx] = min(progress + ach->objective[obj_idx], VECTOR_INDEX(ad->objective, obj_idx).goal);

		// Check if the Achievement is complete.
		if (achievement_check_complete(sd, ad)) {
			achievement_validate_achieve(sd, ad->id);
			if ((ach = achievement_ensure(sd, ad)) == NULL)
				return;
			ach->completed = time(NULL);
		}

		// update client.
		clif_achievement_update(sd, ad);
	}
}

/**
 * Compare an absolute progress value against the goal of an objective.
 * Does not add/increase progress.
 * @param sd          [in] pointer to map-server session data.
 * @param ad          [in] pointer to achievement data.
 * @param obj_idx     [in] index of the objective in question.
 * @param progress  progress of the objective in question.
 */
static void achievement_progress_set(struct map_session_data *sd, const struct achievement_data *ad, unsigned int obj_idx, int progress)
{
	struct achievement *ach = NULL;

	nullpo_retv(sd);
	nullpo_retv(ad);

	Assert_retv(progress != 0);
	Assert_retv(obj_idx < VECTOR_LENGTH(ad->objective));

	if (progress >= VECTOR_INDEX(ad->objective, obj_idx).goal) {

		if ((ach = achievement_ensure(sd, ad)) == NULL)
			return;

		if (ach->completed)
			return;

		ach->objective[obj_idx] = VECTOR_INDEX(ad->objective, obj_idx).goal;

		if (achievement_check_complete(sd, ad)) {
			achievement_validate_achieve(sd, ad->id);
			if ((ach = achievement_ensure(sd, ad)) == NULL)
				return;
			ach->completed = time(NULL);
		}

		clif_achievement_update(sd, ad);
	}
}

/**
* Checks if the given criteria satisfies the achievement's objective.
* @param objective   [in] pointer to the achievement's objectives data.
* @param criteria    [in] pointer to the current session's criteria as a comparand.
* @return true if all criteria are satisfied, else false.
*/
static bool achievement_check_criteria(const struct achievement_objective *objective, const struct achievement_objective *criteria)
{
	int i = 0, j = 0;

	nullpo_retr(false, objective);
	nullpo_retr(false, criteria);

	/* Item Id */
	if (objective->unique_type == CRITERIA_UNIQUE_ITEM_ID && objective->unique.itemid != criteria->unique.itemid)
		return false;
	/* Weapon Level */
	else if (objective->unique_type == CRITERIA_UNIQUE_WEAPON_LV && objective->unique.weapon_lv != criteria->unique.weapon_lv)
		return false;
	/* Status Types */
	else if (objective->unique_type == CRITERIA_UNIQUE_STATUS_TYPE && objective->unique.status_type != criteria->unique.status_type)
		return false;
	/* Achievement Id */
	else if (objective->unique_type == CRITERIA_UNIQUE_ACHIEVE_ID && objective->unique.achieve_id != criteria->unique.achieve_id)
		return false;

	/* Monster Id */
	if (objective->mobid > 0 && objective->mobid != criteria->mobid)
		return false;

	/* Item Type */
	if (objective->item_type > 0 && (objective->item_type & (2 << criteria->item_type)) == 0)
		return false;

	/* Job Ids */
	for (i = 0; i < VECTOR_LENGTH(objective->jobid); i++) {
		ARR_FIND(0, VECTOR_LENGTH(criteria->jobid), j, VECTOR_INDEX(criteria->jobid, j) != VECTOR_INDEX(objective->jobid, i));
		if (j < VECTOR_LENGTH(criteria->jobid))
			return false;
	}

	return true;
}

/**
 * Validates an Achievement Objective of similar types.
 * @param[in] sd         as a pointer to the map session data.
 * @param[in] type       as the type of the achievement.
 * @param[in] criteria   as the criteria of the objective (mob id, job id etc.. 0 for no criteria).
 * @param[in] progress   as the current progress of the objective.
 * @return total number of updated achievements on success, 0 on failure.
 */
static int achievement_validate_type(struct map_session_data *sd, enum achievement_types type, const struct achievement_objective *criteria, bool additive)
{
	int i = 0, total = 0;
	struct achievement *ach = NULL;

	nullpo_ret(sd);
	nullpo_ret(criteria);

	Assert_ret(criteria->goal != 0);

	if (battle_config.feature_achievement == 0)
		return 0;

	if (type == ACH_QUEST) {
		ShowError("achievement_validate_type: ACH_QUEST is not handled by this function. (use achievement_validate())\n");
		return 0;
	}
	else if (type >= ACH_TYPE_MAX) {
		ShowError("achievement_validate_type: Invalid Achievement Type %d! (min: %d, max: %d)\n", (int)type, (int)ACH_QUEST, (int)ACH_TYPE_MAX - 1);
		return 0;
	}

	/* Loop through all achievements of the type, checking for possible matches. */
	for (i = 0; i < VECTOR_LENGTH(category[type]); i++) {
		int j = 0;
		bool updated = false;
		const struct achievement_data *ad = NULL;

		if ((ad = achievement_get(VECTOR_INDEX(category[type], i))) == NULL)
			continue;

		for (j = 0; j < VECTOR_LENGTH(ad->objective); j++) {
			// Check if objective criteria matches.
			if (achievement_check_criteria(&VECTOR_INDEX(ad->objective, j), criteria) == false)
				continue;
			// Ensure availability of the achievement.
			if ((ach = achievement_ensure(sd, ad)) == NULL)
				return false;
			// Criteria passed, check if not completed and update progress.
			if ((ach->completed == 0 && ach->objective[j] < VECTOR_INDEX(ad->objective, j).goal)) {
				if (additive == true)
					achievement_progress_add(sd, ad, j, criteria->goal);
				else
					achievement_progress_set(sd, ad, j, criteria->goal);
				updated = true;
			}
		}

		if (updated == true)
			total++;
	}

	return total;
}

/**
 * Validates any achievement's specific objective index.
 * @param[in] sd         pointer to the session data.
 * @param[in] aid        ID of the achievement.
 * @param[in] index      index of the objective.
 * @param[in] progress   progress to be added towards the goal.
 */
bool achievement_validate(struct map_session_data *sd, int aid, unsigned int obj_idx, int progress, bool additive)
{
	const struct achievement_data *ad = NULL;
	struct achievement *ach = NULL;

	nullpo_retr(false, sd);
	Assert_retr(false, progress > 0);
	Assert_retr(false, obj_idx < MAX_ACHIEVEMENT_OBJECTIVES);


	if (battle_config.feature_achievement == 0)
		return false;

	if ((ad = achievement_get(aid)) == NULL) {
		ShowError("achievement_validate: Invalid Achievement %d provided.", aid);
		return false;
	}

	// Ensure availability of the achievement.
	if ((ach = achievement_ensure(sd, ad)) == NULL)
		return false;

	// Check if not completed and update progress.
	if ((!ach->completed && ach->objective[obj_idx] < VECTOR_INDEX(ad->objective, obj_idx).goal)) {
		if (additive == true)
			achievement_progress_add(sd, ad, obj_idx, progress);
		else
			achievement_progress_set(sd, ad, obj_idx, progress);
	}

	return true;
}

/**
 * Validates monster kill type objectives.
 * @type ACH_KILL_MOB_CLASS
 * @param[in] sd          pointer to session data.
 * @param[in] mob_id      (criteria) class of the monster checked for.
 * @param[in] progress    (goal) progress to be added.
 * @see achievement_vaildate_type()
 */
void achievement_validate_mob_kill(struct map_session_data *sd, int mob_id)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);
	Assert_retv(mob_id > 0 && mob_db(mob_id) != NULL);

	if (sd->achievements_received == false)
		return;

	criteria.mobid = mob_id;
	criteria.goal = 1;

	achievement_validate_type(sd, ACH_KILL_MOB_CLASS, &criteria, true);
}

/**
 * Validate monster damage type objectives.
 * @types ACH_DAMAGE_MOB_REC_MAX
 *        ACH_DAMAGE_MOB_REC_TOTAL
 * @param[in] sd       pointer to session data.
 * @param[in] damage   amount of damage received/dealt.
 * @param received received/dealt boolean switch.
 */
void achievement_validate_mob_damage(struct map_session_data *sd, unsigned int damage, bool received)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);
	Assert_retv(damage > 0);

	if (sd->achievements_received == false)
		return;

	criteria.goal = (int)damage;

	if (received) {
		achievement_validate_type(sd, ACH_DAMAGE_MOB_REC_MAX, &criteria, false);
		achievement_validate_type(sd, ACH_DAMAGE_MOB_REC_TOTAL, &criteria, true);
	}
	else {
		achievement_validate_type(sd, ACH_DAMAGE_MOB_MAX, &criteria, false);
		achievement_validate_type(sd, ACH_DAMAGE_MOB_TOTAL, &criteria, true);
	}

}

/**
 * Validate player kill (PVP) achievements.
 * @types ACH_KILL_PC_TOTAL
 *        ACH_KILL_PC_JOB
 *        ACH_KILL_PC_JOBTYPE
 * @param[in] sd         pointer to killed player's session data.
 * @param[in] dstsd      pointer to killer's session data.
 */
void achievement_validate_pc_kill(struct map_session_data *sd, struct map_session_data *dstsd)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);
	nullpo_retv(dstsd);

	if (sd->achievements_received == false)
		return;

	criteria.goal = 1;

	/* */
	achievement_validate_type(sd, ACH_KILL_PC_TOTAL, &criteria, true);

	/* */
	VECTOR_INIT(criteria.jobid);
	VECTOR_ENSURE(criteria.jobid, 1, 1);
	VECTOR_PUSH(criteria.jobid, dstsd->status.class_);

	/* Job class */
	achievement_validate_type(sd, ACH_KILL_PC_JOB, &criteria, true);
	/* Job Type */
	achievement_validate_type(sd, ACH_KILL_PC_JOBTYPE, &criteria, true);

	VECTOR_CLEAR(criteria.jobid);
}

/**
 * Validate player kill (PVP) achievements.
 * @types ACH_DAMAGE_PC_MAX
 *        ACH_DAMAGE_PC_TOTAL
 *        ACH_DAMAGE_PC_REC_MAX
 *        ACH_DAMAGE_PC_REC_TOTAL
 * @param[in] sd         pointer to source player's session data.
 * @param[in] dstsd      pointer to target player's session data.
 * @param[in] damage     amount of damage dealt / received.
 */
void achievement_validate_pc_damage(struct map_session_data *sd, struct map_session_data *dstsd, unsigned int damage)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	if (damage == 0)
		return;

	criteria.goal = (int)damage;

	/* */
	achievement_validate_type(sd, ACH_DAMAGE_PC_MAX, &criteria, false);
	achievement_validate_type(sd, ACH_DAMAGE_PC_TOTAL, &criteria, true);

	/* */
	achievement_validate_type(dstsd, ACH_DAMAGE_PC_REC_MAX, &criteria, false);
	achievement_validate_type(dstsd, ACH_DAMAGE_PC_REC_TOTAL, &criteria, true);
}

/**
 * Validates job change objectives.
 * @type ACH_JOB_CHANGE
 * @param[in] sd         pointer to session data.
 * @see achivement_validate_type()
 */
void achievement_validate_jobchange(struct map_session_data *sd)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	VECTOR_INIT(criteria.jobid);
	VECTOR_ENSURE(criteria.jobid, 1, 1);
	VECTOR_PUSH(criteria.jobid, sd->status.class_);

	criteria.goal = 1;

	achievement_validate_type(sd, ACH_JOB_CHANGE, &criteria, false);

	VECTOR_CLEAR(criteria.jobid);
}

/**
 * Validates stat type objectives.
 * @types ACH_STATUS
 *        ACH_STATUS_BY_JOB
 *        ACH_STATUS_BY_JOBTYPE
 * @param[in] sd         pointer to session data.
 * @param[in] stat_type  (criteria) status point type. (see _sp)
 * @param[in] progress   (goal) amount of absolute progress to check.
 * @see achievement_validate_type()
 */
void achievement_validate_stats(struct map_session_data *sd, enum _sp stat_type, int progress)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);
	Assert_retv(progress > 0);

	if (sd->achievements_received == false)
		return;

	if (!achievement_valid_status_types(stat_type)) {
		ShowError("achievement_validate_stats: Invalid status type %d given.\n", (int)stat_type);
		return;
	}

	criteria.unique.status_type = stat_type;

	criteria.goal = progress;

	achievement_validate_type(sd, ACH_STATUS, &criteria, false);

	VECTOR_INIT(criteria.jobid);
	VECTOR_ENSURE(criteria.jobid, 1, 1);
	VECTOR_PUSH(criteria.jobid, sd->status.class_);

	/* Stat and Job class */
	achievement_validate_type(sd, ACH_STATUS_BY_JOB, &criteria, false);

	/* Stat and Job Type */
	achievement_validate_type(sd, ACH_STATUS_BY_JOBTYPE, &criteria, false);

	VECTOR_CLEAR(criteria.jobid);
}

/**
 * Validates chatroom creation type objectives.
 * @types ACH_CHATROOM_CREATE
 *        ACH_CHATROOM_CREATE_DEAD
 * @param[in] sd    pointer to session data.
 * @see achievement_validate_type()
 */
void achievement_validate_chatroom_create(struct map_session_data *sd)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	criteria.goal = 1;

	if (pc_isdead(sd)) {
		achievement_validate_type(sd, ACH_CHATROOM_CREATE_DEAD, &criteria, true);
		return;
	}

	achievement_validate_type(sd, ACH_CHATROOM_CREATE, &criteria, true);
}

/**
 * Validates chatroom member count type objectives.
 * @type ACH_CHATROOM_MEMBERS
 * @param[in] sd         pointer to session data.
 * @param[in] progress   (goal) amount of progress to be added.
 * @see achievement_validate_type()
 */
void achievement_validate_chatroom_members(struct map_session_data *sd, int progress)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	Assert_retv(progress > 0);

	criteria.goal = progress;

	achievement_validate_type(sd, ACH_CHATROOM_MEMBERS, &criteria, false);
}

/**
 * Validates friend add type objectives.
 * @type ACH_FRIEND_ADD
 * @param[in] sd        pointer to session data.
 * @see achievement_validate_type()
 */
void achievement_validate_friend_add(struct map_session_data *sd)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	criteria.goal = 1;

	achievement_validate_type(sd, ACH_FRIEND_ADD, &criteria, true);
}

/**
 * Validates party creation type objectives.
 * @type ACH_PARTY_CREATE
 * @param[in] sd        pointer to session data.
 * @see achievement_validate_type()
 */
void achievement_validate_party_create(struct map_session_data *sd)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	criteria.goal = 1;
	achievement_validate_type(sd, ACH_PARTY_CREATE, &criteria, true);
}

/**
 * Validates marriage type objectives.
 * @type ACH_MARRY
 * @param[in] sd        pointer to session data.
 * @see achievement_validate_type()
 */
void achievement_validate_marry(struct map_session_data *sd)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	criteria.goal = 1;

	achievement_validate_type(sd, ACH_MARRY, &criteria, true);
}

/**
 * Validates adoption type objectives.
 * @types ACH_ADOPT_PARENT
 *        ACH_ADOPT_BABY
 * @param[in] sd        pointer to session data.
 * @param[in] parent    (type) boolean value to indicate if parent (true) or baby (false).
 * @see achievement_validate_type()
 */
void achievement_validate_adopt(struct map_session_data *sd, bool parent)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	criteria.goal = 1;

	if (parent)
		achievement_validate_type(sd, ACH_ADOPT_PARENT, &criteria, true);
	else
		achievement_validate_type(sd, ACH_ADOPT_BABY, &criteria, true);
}

/**
 * Validates zeny type objectives.
 * @types ACH_ZENY_HOLD
 *        ACH_ZENY_GET_ONCE
 *        ACH_ZENY_GET_TOTAL
 *        ACH_ZENY_SPEND_ONCE
 *        ACH_ZENY_SPEND_TOTAL
 * @param[in] sd        pointer to session data.
 * @param[in] amount    (goal) amount of zeny earned or spent.
 * @see achievement_validate_type()
 */
void achievement_validate_zeny(struct map_session_data *sd, int amount)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	Assert_retv(amount != 0);

	if (amount > 0) {
		criteria.goal = sd->status.zeny;
		achievement_validate_type(sd, ACH_ZENY_HOLD, &criteria, false);
		criteria.goal = amount;
		achievement_validate_type(sd, ACH_ZENY_GET_ONCE, &criteria, false);
		achievement_validate_type(sd, ACH_ZENY_GET_TOTAL, &criteria, true);
	}
	else {
		criteria.goal = -amount;
		achievement_validate_type(sd, ACH_ZENY_SPEND_ONCE, &criteria, false);
		achievement_validate_type(sd, ACH_ZENY_SPEND_TOTAL, &criteria, true);
	}
}

/**
 * Validates equipment refinement type objectives.
 * @types ACH_EQUIP_REFINE_SUCCESS
 *        ACH_EQUIP_REFINE_FAILURE
 *        ACH_EQUIP_REFINE_SUCCESS_TOTAL
 *        ACH_EQUIP_REFINE_FAILURE_TOTAL
 *        ACH_EQUIP_REFINE_SUCCESS_WLV
 *        ACH_EQUIP_REFINE_FAILURE_WLV
 *        ACH_EQUIP_REFINE_SUCCESS_ID
 *        ACH_EQUIP_REFINE_FAILURE_ID
 * @param[in] sd         pointer to session data.
 * @param[in] idx        Inventory index of the item.
 * @param[in] success    (type) boolean switch for failure / success.
 * @see achievement_validate_type()
 */
void achievement_validate_refine(struct map_session_data *sd, unsigned int idx, bool success)
{
	struct achievement_objective criteria = { 0 };
	struct item_data *id = NULL;

	nullpo_retv(sd);
	Assert_retv(idx < MAX_INVENTORY);

	id = itemdb_exists(sd->inventory.u.items_inventory[idx].nameid);

	if (sd->achievements_received == false)
		return;

	Assert_retv(idx < MAX_INVENTORY);
	Assert_retv(id != NULL);

	criteria.goal = sd->inventory.u.items_inventory[idx].refine;

	// achievement should not trigger if refine is 0
	if (criteria.goal == 0)
		return;

	/* Universal */
	achievement_validate_type(sd,
		success ? ACH_EQUIP_REFINE_SUCCESS : ACH_EQUIP_REFINE_FAILURE,
		&criteria, false);

	/* Total */
	criteria.goal = 1;
	achievement_validate_type(sd,
		success ? ACH_EQUIP_REFINE_SUCCESS_TOTAL : ACH_EQUIP_REFINE_FAILURE_TOTAL,
		&criteria, true);

	/* By Weapon Level */
	if (id->type == IT_WEAPON) {
		criteria.item_type = id->type;
		criteria.unique.weapon_lv = id->wlv;
		criteria.goal = sd->inventory.u.items_inventory[idx].refine;
		achievement_validate_type(sd,
			success ? ACH_EQUIP_REFINE_SUCCESS_WLV : ACH_EQUIP_REFINE_FAILURE_WLV,
			&criteria, false);
		criteria.item_type = 0;
		criteria.unique.weapon_lv = 0; // cleanup
	}

	/* By NameId */
	criteria.unique.itemid = id->nameid;
	criteria.goal = sd->inventory.u.items_inventory[idx].refine;
	achievement_validate_type(sd,
		success ? ACH_EQUIP_REFINE_SUCCESS_ID : ACH_EQUIP_REFINE_FAILURE_ID,
		&criteria, false);
	criteria.unique.itemid = 0; // cleanup
}

/**
 * Validates item received type objectives.
 * @types ACH_ITEM_GET_COUNT
 *        ACH_ITEM_GET_WORTH
 *        ACH_ITEM_GET_COUNT_ITEMTYPE
 * @param[in] sd         pointer to session data.
 * @param[in] nameid     (criteria) ID of the item.
 * @param[in] amount     (goal) amount of the item collected.
 */
void achievement_validate_item_get(struct map_session_data *sd, int nameid, int amount)
{
	struct item_data *it = itemdb_exists(nameid);
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	Assert_retv(amount > 0);
	nullpo_retv(it);

	criteria.unique.itemid = it->nameid;
	criteria.goal = amount;
	achievement_validate_type(sd, ACH_ITEM_GET_COUNT, &criteria, false);
	criteria.unique.itemid = 0; // cleanup

	/* Item Buy Value*/
	criteria.goal = max(it->value_buy, 1);
	achievement_validate_type(sd, ACH_ITEM_GET_WORTH, &criteria, false);

	/* Item Type */
	criteria.item_type = it->type;
	criteria.goal = 1;
	achievement_validate_type(sd, ACH_ITEM_GET_COUNT_ITEMTYPE, &criteria, false);
	criteria.item_type = 0; // cleanup
}

/**
 * Validates item sold type objectives.
 * @type ACH_ITEM_SELL_WORTH
 * @param[in] sd         pointer to session data.
 * @param[in] nameid     Item Id in question.
 * @param[in] amount     amount of item in question.
 */
void achievement_validate_item_sell(struct map_session_data *sd, int nameid, int amount)
{
	struct item_data *it = itemdb_exists(nameid);
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	Assert_retv(amount > 0);
	nullpo_retv(it);

	criteria.unique.itemid = it->nameid;

	criteria.goal = max(it->value_sell, 1);

	achievement_validate_type(sd, ACH_ITEM_SELL_WORTH, &criteria, false);
}

/**
 * Validates achievement type objectives.
 * @type ACH_ACHIEVE
 * @param[in] sd        pointer to session data.
 * @param[in] achid     (criteria) achievement id.
 */
void achievement_validate_achieve(struct map_session_data *sd, int achid)
{
	const struct achievement_data *ad = achievement_get(achid);
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);
	nullpo_retv(ad);

	if (sd->achievements_received == false)
		return;

	Assert_retv(achid > 0);

	criteria.unique.achieve_id = ad->id;

	criteria.goal = 1;

	achievement_validate_type(sd, ACH_ACHIEVE, &criteria, false);
}

/**
 * Validates taming type objectives.
 * @type ACH_PET_CREATE
 * @param[in] sd        pointer to session data.
 * @param[in] class     (criteria) class of the monster tamed.
 */
void achievement_validate_taming(struct map_session_data *sd, int class)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	Assert_retv(class > 0);
	Assert_retv(mob_db(class) != mob_dummy);

	criteria.mobid = class;
	criteria.goal = 1;

	achievement_validate_type(sd, ACH_PET_CREATE, &criteria, true);
}

/**
 * Validated achievement rank type objectives.
 * @type ACH_ACHIEVEMENT_RANK
 * @param[in] sd         pointer to session data.
 * @param[in] rank       (goal) rank of earned.
 */
void achievement_validate_achievement_rank(struct map_session_data *sd, int rank)
{
	struct achievement_objective criteria = { 0 };

	nullpo_retv(sd);

	if (sd->achievements_received == false)
		return;

	Assert_retv(rank >= 0 && rank <= VECTOR_LENGTH(rank_exp));

	criteria.goal = 1;

	achievement_validate_type(sd, ACH_ACHIEVEMENT_RANK, &criteria, false);
}

/**
 * Verifies if an achievement type requires a criteria field.
 * @param[in] type       achievement type in question.
 * @return true if required, false if not.
 */
static bool achievement_type_requires_criteria(enum achievement_types type)
{
	if (type == ACH_KILL_PC_JOB
		|| type == ACH_KILL_PC_JOBTYPE
		|| type == ACH_KILL_MOB_CLASS
		|| type == ACH_JOB_CHANGE
		|| type == ACH_STATUS
		|| type == ACH_STATUS_BY_JOB
		|| type == ACH_STATUS_BY_JOBTYPE
		|| type == ACH_EQUIP_REFINE_SUCCESS_WLV
		|| type == ACH_EQUIP_REFINE_FAILURE_WLV
		|| type == ACH_EQUIP_REFINE_SUCCESS_ID
		|| type == ACH_EQUIP_REFINE_FAILURE_ID
		|| type == ACH_ITEM_GET_COUNT
		|| type == ACH_PET_CREATE
		|| type == ACH_ACHIEVE)
		return true;

	return false;
}

/**
 * Stores all the title ID that has been earned by player
 * @param[in] sd        pointer to session data.
 */
void achievement_init_titles(struct map_session_data *sd)
{
	int i;
	nullpo_retv(sd);

	VECTOR_INIT(sd->title_ids);
	/* Browse through the session's achievement list and gather their values. */
	for (i = 0; i < VECTOR_LENGTH(sd->achievement); i++) {
		struct achievement *a = &VECTOR_INDEX(sd->achievement, i);
		const struct achievement_data *ad = NULL;

		/* Sanity check for nonull pointers. */
		if (a == NULL || (ad = achievement_get(a->id)) == NULL)
			continue;

		if (a->completed > 0 && a->rewarded > 0 && ad->rewards.title_id > 0) {
			VECTOR_ENSURE(sd->title_ids, 1, 1);
			VECTOR_PUSH(sd->title_ids, ad->rewards.title_id);
		}
	}
}

/**
 * Validates whether player has earned the title.
 * @param[in]  sd              pointer to session data.
 * @param[in]  title_id        Title ID
 * @return true, if title has been earned, else false
 */
bool achievement_check_title(struct map_session_data *sd, int title_id) {
	int i;

	nullpo_retr(false, sd);

	if (title_id == 0)
		return true;

	for (i = 0; i < VECTOR_LENGTH(sd->title_ids); i++) {
		if (VECTOR_INDEX(sd->title_ids, i) == title_id) {
			return true;
		}
	}

	return false;
}

static void achievement_get_rewards_buffs(struct map_session_data *sd, const struct achievement_data *ad)
{
	nullpo_retv(sd);
	nullpo_retv(ad);

	if (ad->rewards.bonus != NULL)
		run_script(ad->rewards.bonus, 0, sd->bl.id, 0);
}

// TODO: kro send items by rodex
static void achievement_get_rewards_items(struct map_session_data *sd, const struct achievement_data *ad)
{
	nullpo_retv(sd);
	nullpo_retv(ad);

	struct item it = { 0 };
	it.identify = 1;

	for (int i = 0; i < VECTOR_LENGTH(ad->rewards.item); i++) {
		it.nameid = VECTOR_INDEX(ad->rewards.item, i).id;
		int total = VECTOR_INDEX(ad->rewards.item, i).amount;

		//Check if it's stackable.
		if (!itemdb_isstackable(it.nameid)) {
			it.amount = 1;
			for (int j = 0; j < total; ++j)
				pc_additem(sd, &it, 1, LOG_TYPE_OTHER);
		} else {
			it.amount = total;
			pc_additem(sd, &it, total, LOG_TYPE_OTHER);
		}
	}
}

/**
 * Achievement rewards are given to player
 * @param  sd        session data
 * @param  ad        achievement data
 */
bool achievement_get_rewards(struct map_session_data *sd, const struct achievement_data *ad)
{
	nullpo_retr(false, sd);
	nullpo_retr(false, ad);

	struct achievement *ach = achievement_ensure(sd, ad);
	if (ach == NULL)
		return false;

	/* Buff */
	achievement_get_rewards_buffs(sd, ad);

	ach->rewarded = time(NULL);

	if (ad->rewards.title_id > 0) { // Add Title
		VECTOR_ENSURE(sd->title_ids, 1, 1);
		VECTOR_PUSH(sd->title_ids, ad->rewards.title_id);
		clif_achievement_list_all(sd);
	} else {
		clif_achievement_update(sd, ad); // send update.
		clif_achievement_reward_ack(sd->fd, 0, ad->id);
	}

	/* Give Items */
	achievement_get_rewards_items(sd, ad);

	return true;
}

void achievement_removeChar(char *str, char garbage) {

	char *src, *dst;
	for (src = dst = str; *src != '\0'; src++) {
		*dst = *src;
		if (*dst != garbage) dst++;
	}
	*dst = '\0';
}

int split_string(const char *txt, char delim, char ***tokens)
{
	int *tklen, *t, count = 1;
	char **arr, *p = (char *)txt;

	while (*p != '\0') if (*p++ == delim) count += 1;
	t = tklen = calloc(count, sizeof(int));
	for (p = (char *)txt; *p != '\0'; p++) *p == delim ? *t++ : (*t)++;
	*tokens = arr = malloc(count * sizeof(char *));
	t = tklen;
	p = *arr++ = calloc(*(t++) + 1, sizeof(char *));
	while (*txt != '\0')
	{
		if (*txt == delim)
		{
			p = *arr++ = calloc(*(t++) + 1, sizeof(char *));
			txt++;
		}
		else *p++ = *txt++;
	}
	free(tklen);
	return count;
}


/**
 * Parses achievement objective entries of the current achievement db entry being parsed.
 * @param[in]  conf       config pointer.
 * @param[out] entry      pointer to the achievement db entry being parsed.
 * @return false on failure, true on success.
 */
static bool achievement_readdb_objectives(char** str, struct achievement_data *entry)
{
	nullpo_retr(false, entry);

	int c, i, j, val;
	enum unique_criteria_types type[MAX_ACHIEVEMENT_OBJECTIVES];
	int tcount = 0;
	char **arr = NULL;
	char *tmpchar = NULL;
	char *typechar[MAX_ACHIEVEMENT_OBJECTIVES];
	char *descriptions[MAX_ACHIEVEMENT_OBJECTIVES];
	char *criteriaIDs[MAX_ACHIEVEMENT_OBJECTIVES];
	int goals[MAX_ACHIEVEMENT_OBJECTIVES];

	// Initialize the buffer objective vector.
	VECTOR_INIT(entry->objective);

	// get the data, split & count them
	for (i = 0; i < MAX_ACHIEVEMENT_OBJECTIVES; ++i)
	{
		descriptions[i] = str[3];
		str[3] = strchr(str[3], ':');
		if (str[3] == NULL)
			break;// : not found
		*str[3] = '\0';
		++str[3];

		if (descriptions[i] == NULL)
		{
			ShowError("achievement_readdb_objectives: Insufficient values in description of achievement with id %d), skipping.\n", entry->id);
			return false;
		}
		tcount++;
	}
	for (i = 0; i <= tcount; ++i)
	{
		typechar[i] = tmpchar = str[4];
		str[4] = strchr(str[4], ':');
		if (str[4] == NULL) {
			if (i == 0)
				script_get_constant(tmpchar, (int *)&type[0]); // : not found, use the complete string.
			break; // : not found.
		}
		*str[4] = '\0';
		++str[4];

		if (typechar[i] == NULL)
		{
			ShowError("achievement_readdb_objectives: Insufficient values in criterias of achievement with id %d), skipping.\n", entry->id);
			return false;
		}
		script_get_constant(typechar[i], (int *)&type[i]);
	}
	for (i = 0; i <= tcount; ++i)
	{
		criteriaIDs[i] = str[5];
		str[5] = strchr(str[5], ':');
		if (str[5] == NULL)
			break; // : not found
		*str[5] = '\0';
		++str[5];

		if (criteriaIDs[i] == NULL)
		{
			ShowError("achievement_readdb_objectives: Insufficient values in criteria IDs of achievement with id %d), skipping.\n", entry->id);
			return false;
		}
	}
	pc_split_atoi(str[6], goals, ':', tcount+1);


	// put the data into the struct.
	for (i = 0; i <= tcount; ++i)
	{
		struct achievement_objective obj = { 0 };

		safestrncpy(obj.description, descriptions[i], OBJECTIVE_DESCRIPTION_LENGTH);

		if (type[i] < 0 && i>0)
			type[i] = type[i - 1];

		obj.unique_type = type[i]>0?type[i]:CRITERIA_UNIQUE_NONE;

		switch (obj.unique_type)
		{
			case CRITERIA_UNIQUE_ACHIEVE_ID:
				obj.unique.achieve_id = atoi(criteriaIDs[i]);
				break;
			case CRITERIA_UNIQUE_ITEM_ID:
				val = atoi(criteriaIDs[i]);
				if (itemdb_exists(val) == NULL) {
					ShowError("achievement_readdb_objectives: Invalid ItemID %d provided (Achievement: %d, Objective: %d). Skipping...\n", criteriaIDs[i], entry->id, i);
					return false;
				}
				obj.unique.itemid = val;
				break;
			case CRITERIA_UNIQUE_MOB_ID:
				val = atoi(criteriaIDs[i]);
				if (mob_db(val) == NULL) {
					ShowError("achievement_readdb_objectives: Non-existant monster with ID %id provided (Achievement: %d, Objective: %d). Skipping...\n", criteriaIDs[i], entry->id, i);
					return false;
				}
				obj.mobid = val;
				break;
			case CRITERIA_UNIQUE_JOB_ID:
				achievement_removeChar(criteriaIDs[i], '"');
				c = split_string(criteriaIDs[i], '|', &arr);

				for (j = 0; j < c; ++j)
				{
					if (arr[j] == NULL)
					{
						ShowError("achievement_readdb_objectives: Insufficient JobID in defined in achievement with id %d), skipping.\n", entry->id);
						return false;
					}

					if (script_get_constant(arr[j], &val) == false) {
						ShowError("achievement_readdb_objectives: Invalid JobId %d (const: %s) provided (Achievement: %d, Objective: %d). Skipping...\n", val, arr[j], entry->id, i);
						return false;
					}

					VECTOR_INIT(obj.jobid);
					VECTOR_ENSURE(obj.jobid, 1, 1);
					VECTOR_PUSH(obj.jobid, val);
				}
				break;
			case CRITERIA_UNIQUE_STATUS_TYPE:
				achievement_removeChar(criteriaIDs[i], '"');
				if (strcmp(criteriaIDs[i], "SP_STR") == 0)		      val = SP_STR;
				else if (strcmp(criteriaIDs[i], "SP_AGI") == 0)       val = SP_AGI;
				else if (strcmp(criteriaIDs[i], "SP_VIT") == 0)       val = SP_VIT;
				else if (strcmp(criteriaIDs[i], "SP_INT") == 0)       val = SP_INT;
				else if (strcmp(criteriaIDs[i], "SP_DEX") == 0)       val = SP_DEX;
				else if (strcmp(criteriaIDs[i], "SP_LUK") == 0)       val = SP_LUK;
				else if (strcmp(criteriaIDs[i], "SP_BASELEVEL") == 0) val = SP_BASELEVEL;
				else if (strcmp(criteriaIDs[i], "SP_JOBLEVEL") == 0)  val = SP_JOBLEVEL;
				else val = SP_NONE;
				obj.unique.status_type = (enum _sp) val;
				break;
			case CRITERIA_UNIQUE_ITEM_TYPE:
				achievement_removeChar(criteriaIDs[i], '"');
				c = split_string(criteriaIDs[i], '|', &arr);

				for (j = 0; j < c; ++j)
				{
					if (arr[j] == NULL)
					{
						ShowError("achievement_readdb_objectives: Insufficient item type in defined in achievement with id %d), skipping.\n", entry->id);
						return false;
					}

					if (!script_get_constant(arr[j], &val) || val < IT_HEALING || val > IT_MAX) {
						ShowError("achievement_readdb_validate_criteria_itemtype: Invalid ItemType %d provided (Achievement: %d, Objective: %d). Skipping...\n", val, entry->id, i);
						return false;
					}

					if (val == IT_MAX) {
						obj.item_type |= (2 << val) - 1;
					}
					else {
						obj.item_type |= (2 << val);
					}
				}
				break;
			case CRITERIA_UNIQUE_WEAPON_LV:
				val = atoi(criteriaIDs[i]);

				if (val < 1 || val > 4) {
					ShowError("achievement_readdb_validate_criteria_weaponlv: Invalid WeaponLevel %d provided (Achievement: %d, Objective: %d). Skipping...\n", val, entry->id, i);
					return false;
				}
				obj.unique.weapon_lv = val;
				break;
			default:
				if (achievement_type_requires_criteria(entry->type)) {
					ShowError("achievement_readdb_objectives: No criteria field added (Achievement: %d, Type: %d)! Skipping...\n", entry->id, entry->type);
					return false;
				}
				break;
		}

		obj.goal = goals[i]>0 ? goals[i] : 1;

		/* Ensure size and allocation */
		VECTOR_ENSURE(entry->objective, 1, 1);

		/* Push buffer */
		VECTOR_PUSH(entry->objective, obj);
	}

	return true;
}

/**
 * Parses the Achievement Rewards.
 * @read db/achievement_reward_db.txt
 */
static bool achievement_readdb_rewards(char** str, struct achievement_data *entry)
{
	nullpo_retr(false, entry);

	struct achievement_reward_item item = { 0 };
	int amount = 0;
	int val = 0;
	unsigned short nameid;

	/* Title Id */
	// @TODO Check Title ID against title DB!
	entry->rewards.title_id = atoi(str[8]);

	/* Items */
	VECTOR_INIT(entry->rewards.item);

	nameid = atoi(str[9]);
	amount = atoi(str[10]);

	/* Ensure size and allocation */
	VECTOR_ENSURE(entry->rewards.item, 1, 1);

	if (itemdb_exists(nameid)) {
		item.id = nameid;
		item.amount = (amount > 0) ? amount : 1;
	}
	else {
		item.id = 0;
		item.amount = 0;
	}

	/* push buffer */
	VECTOR_PUSH(entry->rewards.item, item);

	/* Buff/Bonus Script Code */
	entry->rewards.bonus = str[11] ? parse_script(str[11], "achievement_db", entry->id, 0) : NULL;

	return true;
}

/*==========================================
* processes one achievement db entry
*------------------------------------------*/
static bool achievement_parse_dbrow(char** str, const char* source, int line, struct achievement_data* tentry)
{
	/*
	+----+------+------+--------------+-----------+-------------+-------+-------+---------+--------+--------+--------+
	| 00 |  01  |  02  |      03      |     04    |      05     |   06  |   07  |    08   |   09   |   10   |   11   |
	+----+------+------+--------------+-----------+-------------+-------+-------+---------+--------+--------+--------+
	| ID | Name | Type | Descriptions | Criterias | CriteriaIDs | Goals | Score | TitleID | ItemID | Amount | Script |
	+----+------+------+--------------+-----------+-------------+-------+-------+---------+--------+--------+--------+
	*/

	char type_char[50];

	/* Achievement ID */
	tentry->id = atoi(str[0]);
	if (tentry->id < 1) {
		ShowWarning("achievement_parse_dbrow: Invalid achievement ID %d in \"%s\", skipping.\n", tentry->id, source);
		return false;
	}

	/* Achievement Name */
	safestrncpy(tentry->name, str[1], ACHIEVEMENT_NAME_LENGTH);

	/* Achievement Type */
	safestrncpy(type_char, str[2], sizeof(type_char));
	script_get_constant(type_char, (int *)&tentry->type);

	if (tentry->type < ACH_QUEST || tentry->type > ACH_TYPE_MAX) {
		ShowWarning("achievement_parse_dbrow: Invalid achievement group %d in \"%s\" (min: %d, max: %d), skipping.\n", tentry->id, source, ACH_QUEST, ACH_TYPE_MAX);
		return false;
	}

	/* Achievement Objectives */
	achievement_readdb_objectives(str, tentry);

	/* Achievement Rewards */
	achievement_readdb_rewards(str, tentry);

	/* Achievement Points */
	tentry->points = atoi(str[7]);

	return true;
}

/**
 * Loads achievements from the achievement db.
 */
static void achievement_readdb()
{
	uint32 lines = 0, count = 0;
	char line[1024];

	VECTOR_DECL(int) duplicate;

	char file[256];
	FILE* fp;

	sprintf(file, "db/achievement_db.txt");
	fp = fopen(file, "r");
	if (fp == NULL)
	{
		ShowWarning("achievement_read_db: File not found \"%s\", skipping.\n", file);
		return;
	}

	VECTOR_INIT(duplicate);

	// process rows one by one
	while (fgets(line, sizeof(line), fp))
	{
		char *str[32], *p;
		int i;

		struct achievement_data t_ad = {0}, *p_ad = NULL;

		lines++;
		if (line[0] == '/' && line[1] == '/')
			continue;
		memset(str, 0, sizeof(str));

		p = line;

		while (ISSPACE(*p))
			++p;

		if (*p == '\0')
			continue;// empty line

		for (i = 0; i < 11; ++i)
		{
			str[i] = p;
			p = strchr(p, ',');
			if (p == NULL)
				break;// comma not found
			*p = '\0';
			++p;

			if (str[i] == NULL)
			{
				ShowError("achievement_read_db: Insufficient columns in line %d of \"%s\" (achievement with id %d), skipping.\n", lines, file, atoi(str[0]));
				continue;
			}
		}

		if (p == NULL)
		{
			ShowError("achievement_read_db: Insufficient columns in line %d of \"%s\" (achievement with id %d), skipping.\n", lines, file, atoi(str[0]));
			continue;
		}

		// Script (last column)
		if (*p != '{')
		{
			ShowError("achievement_read_db: Invalid format (Script column) in line %d of \"%s\" (achievement with id %d), skipping.\n", lines, file, atoi(str[0]));
			continue;
		}
		str[10] = p;

		achievement_parse_dbrow(str, file, lines, &t_ad);

		ARR_FIND(0, VECTOR_LENGTH(duplicate), i, VECTOR_INDEX(duplicate, i) == t_ad.id);
		if (i != VECTOR_LENGTH(duplicate)) {
			ShowError("achievement_read_db: Duplicate Id %d in line %d. Skipping...\n", t_ad.id, lines);
			continue;
		}

		/* Allocate memory for data. */
		CREATE(p_ad, struct achievement_data, 1);
		*p_ad = t_ad;

		/* Place in the database. */
		idb_put(achievement_db, p_ad->id, p_ad);

		/* Put achievement key in categories. */
		VECTOR_ENSURE(category[p_ad->type], 1, 1);
		VECTOR_PUSH(category[p_ad->type], p_ad->id);

		/* Qualify for duplicate Id checks */
		VECTOR_ENSURE(duplicate, 1, 1);
		VECTOR_PUSH(duplicate, t_ad.id);
		count++;
	}

	VECTOR_CLEAR(duplicate);

	fclose(fp);

	ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, file);
}

/**
 * Parses the Achievement Ranks.
 * @read db/achievement_rank_db.txt
 */
static void achievement_readdb_ranks(void)
{
	uint32 lines = 0, entry = 1;
	char line[1024];

	char file[256];
	FILE* fp;

	sprintf(file, "db/achievement_rank_db.txt");
	fp = fopen(file, "r");
	if (fp == NULL)
	{
		ShowWarning("achievement_readdb_ranks: File not found \"%s\", skipping.\n", file);
		return;
	}

	// process rows one by one
	while (fgets(line, sizeof(line), fp))
	{
		char *str[32], *p;
		int i;
		int rank, exp;

		lines++;
		if (line[0] == '/' && line[1] == '/')
			continue;
		memset(str, 0, sizeof(str));

		p = line;

		while (ISSPACE(*p))
			++p;

		if (*p == '\0')
			continue;// empty line

		for (i = 0; i < 2; ++i)
		{
			str[i] = p;
			p = strchr(p, ',');
			if (p == NULL)
				break;// comma not found
			*p = '\0';
			++p;

			if (str[i] == NULL)
			{
				ShowError("achievement_readdb_ranks: Insufficient columns in line %d of \"%s\", skipping.\n", lines, file);
				return;
			}
		}

		if (entry > MAX_ACHIEVEMENT_RANKS)
			ShowWarning("achievement_rankdb_ranks: Maximum number of achievement ranks exceeded. Skipping all after entry %d...\n", entry);

		rank = atoi(str[0]);
		exp = atoi(str[1]);

		if (exp <= 0) {
			ShowError("achievement_readdb_ranks: Invalid value provided for %s in '%s'.\n", rank, file);
			continue;
		}

		if (rank == entry) {
			VECTOR_ENSURE(rank_exp, 1, 1);
			VECTOR_PUSH(rank_exp, exp);
		}
		else {
			ShowWarning("achievement_readdb_ranks: Ranks are not in order! Ignoring all ranks after Rank %d...\n", entry);
			break; // break if elements are not in order.
		}
		entry++;
	}

	if (entry == 1) {
		ShowError("achievement_readdb_ranks: No ranks provided in '%s'!\n", file);
		return;
	}

	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", entry-1, file);
}

/**
 * On server initiation.
 */
void do_init_achievement(void)
{
	int i = 0;

	/* DB Initialization */
	achievement_db = idb_alloc(DB_OPT_RELEASE_DATA);

	for (i = 0; i < ACH_TYPE_MAX; i++)
		VECTOR_INIT(category[i]);

	VECTOR_INIT(rank_exp);

	/* Read database files */
	achievement_readdb();
	achievement_readdb_ranks();
}

/**
 * Cleaning function called through achievement_db->destroy()
 */
static int achievement_db_finalize(union DBKey key, struct DBData *data, va_list args)
{
	struct achievement_data *ad = db_data2ptr(data);

	for (int i = 0; i < VECTOR_LENGTH(ad->objective); i++)
		VECTOR_CLEAR(VECTOR_INDEX(ad->objective, i).jobid);

	VECTOR_CLEAR(ad->objective);
	VECTOR_CLEAR(ad->rewards.item);

	// Free the script
	if (ad->rewards.bonus != NULL) {
		script_free_code(ad->rewards.bonus);
		ad->rewards.bonus = NULL;
	}

	return 0;
}

/**
 * On server finalizing.
 */
void do_final_achievement(void)
{
	int i = 0;

	achievement_db->destroy(achievement_db, achievement_db_finalize);

	for (i = 0; i < ACH_TYPE_MAX; i++)
		VECTOR_CLEAR(category[i]);

	VECTOR_CLEAR(rank_exp);
}

