// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/malloc.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/mmo.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"

#include "log.h"
#include "clif.h"
#include "chrif.h"
#include "intif.h"
#include "itemdb.h"
#include "map.h"
#include "pc.h"
#include "status.h"
#include "skill.h"
#include "mob.h"
#include "pet.h"
#include "battle.h"
#include "party.h"
#include "guild.h"
#include "atcommand.h"
#include "script.h"
#include "npc.h"
#include "trade.h"
#include "unit.h"
#include "elemental.h"
#include "homunculus.h"
#include "mercenary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ACTIVE_ELEM_AI_RANGE 2	//Distance added on top of 'AREA_SIZE' at which mobs enter active AI mode.

#define ELEM_IDLE_SKILL_INTERVAL 10	//Active idle skills should be triggered every 1 second (1000/MIN_ELEMTHINKTIME)

#define MAX_ELEM_MINCHASE 30	//Max minimum chase value to use for mobs.
#define ELEM_RUDE_ATTACKED_COUNT 2	//After how many rude-attacks should the skill be used?

struct s_elemental_db elemental_db[MAX_ELEMENTAL_CLASS]; // Elemental Database

int elem_search_index(int class_) {
	int i;
	ARR_FIND(0, MAX_ELEMENTAL_CLASS, i, elemental_db[i].class_ == class_);
	return (i == MAX_ELEMENTAL_CLASS)?-1:i;
}

bool elem_class(int class_)
{
	return (bool)(elem_search_index(class_) > -1);
}

struct view_data * elem_get_viewdata(int class_)
{
	int i = elem_search_index(class_);
	if( i < 0 )
		return 0;

	return &elemental_db[i].vd;
}

int elem_create(struct map_session_data *sd, int class_, unsigned int lifetime) {
	struct s_elemental elem;
	struct s_elemental_db *db;
	struct status_data *mstatus;
	unsigned char elem_size;
	int i, skill, amotion;

	nullpo_retr(1,sd);

	if( (i = elem_search_index(class_)) < 0 )
		return 0;

	mstatus = &sd->battle_status;

	db = &elemental_db[i];
	memset(&elem,0,sizeof(struct s_elemental));

	elem.char_id = sd->status.char_id;
	elem.class_ = class_;

	// Sub-stats are affected by the elemental's summon level but each level has a different size.
	// So we use this size to affect the formula's. This is also true in official.
	elem_size = 1 + db->status.size;

	// MaxHP = (10 * (Master's INT + 2 * Master's JobLV)) * ((ElemLV + 2) / 3.0) + (Master's MaxHP / 3)
	// MaxSP = Master's MaxSP / 4
	elem.max_hp = (10 * (mstatus->int_ + 2 * status_get_job_lv_effect(&sd->bl))) * ((elem_size + 2) / 3) + mstatus->max_hp / 3;
	elem.max_sp = mstatus->max_sp / 4;

	// MaxHP/MaxSP + 5% * SkillLV
	if((skill=pc_checkskill(sd,SO_EL_SYMPATHY)) > 0)
	{
		elem.max_hp += elem.max_hp * (5 * skill) / 100;
		elem.max_sp += elem.max_sp * (5 * skill) / 100;
	}

	if( elem.max_hp > battle_config.max_elemental_hp )
		elem.max_hp = battle_config.max_elemental_hp;

	if( elem.max_sp > battle_config.max_elemental_sp )
		elem.max_sp = battle_config.max_elemental_sp;

	elem.hp = elem.max_hp;
	elem.sp = elem.max_sp;

	// ATK = Owner's MaxSP / (18 / ElemLV)
	// MATK (Official) = ElemLV * (Owner's INT / 2 + Owner's DEX / 4)
	// MATK (Custom) = ElemLV * (Master's INT + (Master's INT / 5) * (Master's DEX / 5)) / 3
	// Custom formula used for MATK since renewals MATK formula is greatly different from pre-re.
	elem.batk = mstatus->max_sp / (18 / elem_size);
	elem.matk = elem_size * (mstatus->int_ + (mstatus->int_ / 5) * (mstatus->dex / 5)) / 3;

	// ATK/MATK + 25 * SkillLV
	if((skill=pc_checkskill(sd,SO_EL_SYMPATHY)) > 0)
	{
		elem.batk += 25 * skill;
		elem.matk += 25 * skill;
	}

	// DEF (Official) = Master's DEF + Master's BaseLV / (5 - ElemLV)
	// DEF (Custom) = Master's DEF + Master's BaseLV / (5 - ElemLV) / 10
	// Custom formula used to balance the bonus DEF part for pre-re.
	skill = mstatus->def + status_get_base_lv_effect(&sd->bl) / (5 - elem_size) / 10;
	elem.def = cap_value(skill, 0, battle_config.max_elemental_def_mdef);

	// MDEF (Official) = Master's MDEF + Master's INT / (5 - ElemLV)
	// MDEF (Custom) = Master's MDEF + Master's INT / (5 - ElemLV) / 10
	// Custom formula used to balance the bonus MDEF part for pre-re.
	skill = mstatus->mdef + mstatus->int_ / (5 - elem_size) / 10;
	elem.mdef = cap_value(skill, 0, battle_config.max_elemental_def_mdef);

	// HIT = Master's HIT + Master's JobLV
	// 2011 document made a mistake saying the master's BaseLV affects this. Its acturally JobLV.
	elem.hit = mstatus->hit + status_get_job_lv_effect(&sd->bl);

	// FLEE = Master's FLEE + Master's BaseLV / (5 - ElemLV)
	elem.flee = mstatus->flee + status_get_base_lv_effect(&sd->bl) / (5 - elem_size);

	// ASPD (aMotion) = 750 - 45 / ElemLV - Master's BaseLV - Master's DEX
	// 2011 balance document says the formula is "ASPD 150 + Master's DEX / 10 + ElemLV * 3".
	// But im seeing a completely different formula for this and a cap for it too.
	// Seriously, where did they get that formula from? I can't find it anywhere. [Rytech]
	amotion = 750 - 45 / elem_size - status_get_base_lv_effect(&sd->bl) - mstatus->dex;
	if ( amotion < 400 )// ASPD capped at 160.
		amotion = 400;
	elem.amotion = cap_value(amotion,battle_config.max_aspd,2000);

	// Bonus sub-stats given depending on the elemental type and its summon level.
	if ( class_ >= MOBID_EL_AGNI_S && class_ <= MOBID_EL_AGNI_L )
	{// Agni - Bonus ATK/HIT
		elem.batk += 20 * elem_size;
		elem.hit += 10 * elem_size;
	}
	if ( class_ >= MOBID_EL_AQUA_S && class_ <= MOBID_EL_AQUA_L )
	{// Aqua - Bonus MATK/MDEF
		elem.matk += 20 * elem_size;
		elem.mdef += 10 * elem_size / 10;
	}
	if ( class_ >= MOBID_EL_VENTUS_S && class_ <= MOBID_EL_VENTUS_L )
	{// Ventus - Bonus MATK/FLEE
		elem.matk += 10 * elem_size;
		elem.flee += 20 * elem_size;
	}
	if ( class_ >= MOBID_EL_TERA_S && class_ <= MOBID_EL_TERA_L )
	{// Tera - Bonus ATK/DEF
		elem.batk += 5 * elem_size;
		elem.def += 25 * elem_size / 10;
	}

	elem.life_time = lifetime;

#ifdef TXT_ONLY// Bypass the char server for TXT.
	elem_data_received(&elem,1);
#else
	// Request Char Server to create this elemental
	intif_elemental_create(&elem);
#endif

	return 1;
}

int elemental_get_lifetime(struct elemental_data *ed) {
	const struct TimerData * td;
	if( ed == NULL || ed->summon_timer == INVALID_TIMER )
		return 0;

	td = get_timer(ed->summon_timer);
	return (td != NULL) ? DIFF_TICK32(td->tick, gettick()) : 0;
}

int elemental_get_type(struct elemental_data *ed)
{
	int class_;

	if( ed == NULL || ed->db == NULL )
		return -1;

	class_ = ed->db->class_;

	if( class_ >= MOBID_EL_AGNI_S && class_ <= MOBID_EL_AGNI_L )
		return ELEMTYPE_AGNI;
	if( class_ >= MOBID_EL_AQUA_S && class_ <= MOBID_EL_AQUA_L )
		return ELEMTYPE_AQUA;
	if( class_ >= MOBID_EL_VENTUS_S && class_ <= MOBID_EL_VENTUS_L )
		return ELEMTYPE_VENTUS;
	if( class_ >= MOBID_EL_TERA_S && class_ <= MOBID_EL_TERA_L )
		return ELEMTYPE_TERA;

	return -1;
}

// Send only HP/SP/Life_Time since these are the only variables that change.
int elemental_save(struct elemental_data *ed) {
	ed->elemental.hp = ed->battle_status.hp;
	ed->elemental.sp = ed->battle_status.sp;
	ed->elemental.life_time = elemental_get_lifetime(ed);

#ifndef TXT_ONLY
	intif_elemental_save(&ed->elemental);
#endif
	return 1;
}

static int elem_summon_end(int tid, int64 tick, int id, intptr_t data) {
	struct map_session_data *sd;
	struct elemental_data *ed;

	if( (sd = map_id2sd(id)) == NULL )
		return 1;
	if( (ed = sd->ed) == NULL )
		return 1;

	if( ed->summon_timer != tid ) {
		ShowError("elem_summon_end %d != %d.\n", ed->summon_timer, tid);
		return 0;
	}

	ed->summon_timer = INVALID_TIMER;
	elem_delete(ed, 0); // Elemental summon time is over.

	return 0;
}

int elem_delete(struct elemental_data *ed, int reply)
{
	struct map_session_data *sd = ed->master;
	ed->elemental.life_time = 0;

	elem_summon_stop(ed);

	if( !sd )
		return unit_free(&ed->bl, CLR_OUTSIGHT);

	if( ed->water_screen_flag )
	{
		ed->water_screen_flag = 0;
		status_change_end(&sd->bl, SC_WATER_SCREEN, INVALID_TIMER);
	}

	status_change_end(&sd->bl, SC_EL_COST, INVALID_TIMER);

	return unit_remove_map(&ed->bl, CLR_OUTSIGHT);
}

void elem_summon_stop(struct elemental_data *ed)
{
	nullpo_retv(ed);
	ed->state.alive = 0;
	if( ed->summon_timer != INVALID_TIMER )
		delete_timer(ed->summon_timer, elem_summon_end);
	ed->summon_timer = INVALID_TIMER;
}

void elem_summon_init(struct elemental_data *ed) {
	if( ed->summon_timer == INVALID_TIMER )
		ed->summon_timer = add_timer(gettick() + ed->elemental.life_time, elem_summon_end, ed->master->bl.id, 0);

	ed->regen.state.block = 0;
}


/**
 * Inter-serv has sent us the elemental data from sql, fill it in map-serv memory
 * @param ele : The elemental data received from char-serv
 * @param flag : 0:not created, 1:was saved/loaded
 * @return 0:failed, 1:sucess
 */
int elem_data_received(struct s_elemental *elem, bool flag) {
	struct map_session_data *sd;
	struct elemental_data *ed;
	struct s_elemental_db *db;

	int i = elem_search_index(elem->class_);

	if( (sd = map_charid2sd(elem->char_id)) == NULL )
		return 0;

	if( !flag || i < 0 )
	{ // Not created - loaded - DB info
		sd->status.ele_id = 0;
		return 0;
	}

	db = &elemental_db[i];
	if( !sd->ed )
	{
		sd->ed = ed = (struct elemental_data*)aCalloc(1,sizeof(struct elemental_data));
		ed->bl.type = BL_ELEM;
		ed->bl.id = npc_get_new_npc_id();
		ed->water_screen_flag = 0;
		ed->state.alive = 1;
		ed->master = sd;
		ed->db = db;
		memcpy(&ed->elemental, elem, sizeof(struct s_elemental));
		status_set_viewdata(&ed->bl, ed->elemental.class_);
		status_change_init(&ed->bl);
		unit_dataset(&ed->bl);
		ed->ud.dir = sd->ud.dir;

		ed->bl.m = sd->bl.m;
		ed->bl.x = sd->bl.x;
		ed->bl.y = sd->bl.y;
		unit_calc_pos(&ed->bl, sd->bl.x, sd->bl.y, sd->ud.dir);
		ed->bl.x = ed->ud.to_x;
		ed->bl.y = ed->ud.to_y;

		map_addiddb(&ed->bl);
		status_calc_elemental(ed, SCO_FIRST);
		ed->summon_timer = INVALID_TIMER;
		ed->masterteleport_timer = INVALID_TIMER;
		elem_summon_init(ed);
	} else {
		memcpy(&sd->ed->elemental, elem, sizeof(struct s_elemental));
		ed = sd->ed;
	}

	sd->status.ele_id = elem->elemental_id;

	if( ed && ed->bl.prev == NULL && sd->bl.prev != NULL )
	{
		if (map_addblock(&ed->bl))
			return 0;

		clif_spawn(&ed->bl);
		clif_elemental_info(sd);
	}

	sc_start(&ed->bl, SC_EL_WAIT, 100, 1, 0);
	sc_start(&sd->bl, SC_EL_COST, 100, 1 + ed->battle_status.size, 0);

	return 1;
}

/*==========================================
 * Reachability to a Specification ID existence place
 * state indicates type of 'seek' mob should do:
 * - MSS_LOOT: Looking for item, path must be easy.
 * - MSS_RUSH: Chasing attacking player, path is complex
 * - MSS_FOLLOW: Initiative/support seek, path is complex
 *------------------------------------------*/
int elem_can_reach(struct elemental_data *ed,struct block_list *bl,int range, int state)
{
	int easy = 0;

	nullpo_ret(ed);
	nullpo_ret(bl);
	switch (state) {
		case MSS_RUSH:
		case MSS_FOLLOW:
			easy = 0; //(battle_config.elem_ai&0x1?0:1);
			break;
		case MSS_LOOT:
		default:
			easy = 1;
			break;
	}
	return unit_can_reach_bl(&ed->bl, bl, range, easy, NULL, NULL);
}

/*==========================================
 * Determines if the elem can change target.
 *------------------------------------------*/
static int elem_can_changetarget(struct elemental_data* ed, struct block_list* target, int mode)
{
	// if the monster was provoked ignore the above rule
	if(ed->state.provoke_flag)
	{	
		if (ed->state.provoke_flag == target->id)
			return 1;
		else if (!(battle_config.elem_ai&0x4))
			return 0;
	}
	
	switch (ed->state.skillstate) {
		case MSS_BERSERK:
			if (!(mode&MD_CHANGETARGET_MELEE))
				return 0;
			return (battle_config.elem_ai&0x4 || check_distance_bl(&ed->bl, target, 3));
		case MSS_RUSH:
			return (mode&MD_CHANGETARGET_CHASE);
		case MSS_FOLLOW:
		case MSS_ANGRY:
		case MSS_IDLE:
		case MSS_WALK:
		case MSS_LOOT:
			return 1;
		default:
			return 0;
	}
}

/*==========================================
 * Determination for an attack of a elemental
 *------------------------------------------*/
int elem_target(struct elemental_data *ed,struct block_list *bl,int dist)
{
	nullpo_ret(ed);
	nullpo_ret(bl);

	// Nothing will be carried out if there is no mind of changing TAGE by TAGE ending.
	if(ed->target_id && !elem_can_changetarget(ed, bl, status_get_mode(&ed->bl)))
		return 0;

	if( !status_check_skilluse(&ed->bl, bl, 0, 0, 0) )
		return 0;

	ed->target_id = bl->id;	// Since there was no disturbance, it locks on to target.
	if (ed->state.provoke_flag && bl->id != ed->state.provoke_flag)
		ed->state.provoke_flag = 0;
	ed->min_chase=dist+ed->db->range3;
	if(ed->min_chase>MAX_ELEM_MINCHASE)
		ed->min_chase=MAX_ELEM_MINCHASE;
	return 0;
}

/*==========================================
 * The ?? routine of an active monster
 *------------------------------------------*/
static int elem_ai_sub_hard_activesearch(struct block_list *bl,va_list ap)
{
	struct elemental_data *ed;
	struct block_list **target;
	int mode;
	int dist;

	nullpo_ret(bl);
	ed=va_arg(ap,struct elemental_data *);
	target= va_arg(ap,struct block_list**);
	mode= va_arg(ap,int);

	//If can't seek yet, not an enemy, or you can't attack it, skip.
	if ((*target) == bl || !status_check_skilluse(&ed->bl, bl, 0, 0, 0))
		return 0;

	if ((mode&MD_TARGETWEAK) && status_get_lv(bl) >= ed->db->lv-5)
		return 0;

	if(battle_check_target(&ed->bl,bl,BCT_ENEMY)<=0)
		return 0;

	switch (bl->type)
	{
	case BL_PC:
		if (((TBL_PC*)bl)->state.gangsterparadise &&
			!(status_get_mode(&ed->bl)&MD_BOSS))
			return 0; //Gangster paradise protection.
	default:
		if (battle_config.hom_setting&0x4 &&
			(*target) && (*target)->type == BL_HOM && bl->type != BL_HOM)
			return 0; //For some reason Homun targets are never overriden.

		dist = distance_bl(&ed->bl, bl);
		if(
			((*target) == NULL || !check_distance_bl(&ed->bl, *target, dist)) &&
			battle_check_range(&ed->bl,bl,ed->db->range2)
		) { //Pick closest target?
			(*target) = bl;
			ed->target_id=bl->id;
			ed->min_chase= dist + ed->db->range3;
			if(ed->min_chase>MAX_ELEM_MINCHASE)
				ed->min_chase=MAX_ELEM_MINCHASE;
			return 1;
		}
		break;
	}
	return 0;
}

/*==========================================
 * chase target-change routine.
 *------------------------------------------*/
static int elem_ai_sub_hard_changechase(struct block_list *bl,va_list ap)
{
	struct elemental_data *ed;
	struct block_list **target;

	nullpo_ret(bl);
	ed=va_arg(ap,struct elemental_data *);
	target= va_arg(ap,struct block_list**);

	//If can't seek yet, not an enemy, or you can't attack it, skip.
	if( (*target) == bl || battle_check_target(&ed->bl,bl,BCT_ENEMY) <= 0 ||
	  	!status_check_skilluse(&ed->bl, bl, 0, 0, 0) )
		return 0;

	if(battle_check_range (&ed->bl, bl, ed->battle_status.rhw.range))
	{
		(*target) = bl;
		ed->target_id=bl->id;
		ed->min_chase= ed->db->range3;
	}
	return 1;
}

/*==========================================
 * Processing of slave elemental
 *------------------------------------------*/
static int elem_ai_sub_hard_slaveelem(struct elemental_data *ed,unsigned int tick)
{
	struct block_list *bl;
	int old_dist;

	bl=map_id2bl(ed->master->bl.id);

	if (!bl || status_isdead(bl)) {
		status_kill(&ed->bl);
		return 1;
	}
	if (bl->prev == NULL)
		return 0; //Master not on a map? Could be warping, do not process.

	if(status_get_mode(&ed->bl)&MD_CANMOVE)
	{	//If the elem can move, follow around.
		
		// Distance with between slave and master is measured.
		old_dist=ed->master_dist;
		ed->master_dist=distance_bl(&ed->bl, bl);

		// Since the master was in near immediately before, teleport is carried out and it pursues.
		if(bl->m != ed->bl.m || 
			(old_dist<10 && ed->master_dist>18) ||
			ed->master_dist > MAX_ELEM_MINCHASE
		){
			ed->master_dist = 0;
			unit_warp(&ed->bl,bl->m,bl->x,bl->y,CLR_TELEPORT);
			return 1;
		}

		if(ed->target_id) //Slave is busy with a target.
			return 0;

		// Approach master if within view range, chase back to Master's area also if standing on top of the master.
		if((ed->master_dist>ELEM_SLAVEDISTANCE || ed->master_dist == 0) &&
			unit_can_move(&ed->bl))
		{
			short x = bl->x, y = bl->y;
			elem_stop_attack(ed);
			if(map_search_freecell(&ed->bl, bl->m, &x, &y, ELEM_SLAVEDISTANCE, ELEM_SLAVEDISTANCE, 1)
				&& unit_walktoxy(&ed->bl, x, y, 0))
				return 1;
		}	
	} else if (bl->m != ed->bl.m && map_flag_gvg(ed->bl.m)) {
		//Delete the summoned elem if it's in a gvg ground and the master is elsewhere.
		status_kill(&ed->bl);
		return 1;
	}
	
	//Avoid attempting to lock the master's target too often to avoid unnecessary overload.
	if (DIFF_TICK(ed->last_linktime, tick) < MIN_ELEMLINKTIME && !ed->target_id)
  	{
		struct unit_data *ud = unit_bl2ud(bl);
		ed->last_linktime = tick;
		
		if (ud) {
			struct block_list *tbl=NULL;
			if (ud->target && ud->state.attack_continue)
				tbl=map_id2bl(ud->target);
			else if (ud->skilltarget) {
				tbl = map_id2bl(ud->skilltarget);
				//Required check as skilltarget is not always an enemy.
				if (tbl && battle_check_target(&ed->bl, tbl, BCT_ENEMY) <= 0)
					tbl = NULL;
			}
			if( tbl && status_check_skilluse(&ed->bl, tbl, 0, 0, 0) )
			{
				ed->target_id=tbl->id;
				ed->min_chase=ed->db->range3+distance_bl(&ed->bl, tbl);
				if(ed->min_chase>MAX_ELEM_MINCHASE)
					ed->min_chase=MAX_ELEM_MINCHASE;
				return 1;
			}
		}
	}
	return 0;
}

/*==========================================
 * A lock of target is stopped and elem moves to a standby state.
 * This also triggers idle skill/movement since the AI can get stuck
 * when trying to pick new targets when the current chosen target is
 * unreachable.
 *------------------------------------------*/
int elem_unlocktarget(struct elemental_data *ed, int64 tick)
{
	nullpo_ret(ed);

	switch (ed->state.skillstate) {
	case MSS_WALK:
		if (ed->ud.walktimer != INVALID_TIMER)
			break;
		//Because it is not unset when the elem finishes walking.
		ed->state.skillstate = MSS_IDLE;
	case MSS_IDLE:
		// Idle skill.
		if ((ed->target_id || !(++ed->ud.walk_count%ELEM_IDLE_SKILL_INTERVAL)) &&
			elemskill_use(ed, tick, -1))
			break;
		//Random walk.
		/*if (!ed->master->bl.id &&
			DIFF_TICK(ed->next_walktime, tick) <= 0 &&
			!elem_randomwalk(ed,tick))
			//Delay next random walk when this one failed.
			ed->next_walktime=tick+rnd()%3000;
		break;*/
	default:
		elem_stop_attack(ed);
		if (battle_config.elem_ai&0x8)
			elem_stop_walking(ed,1); //Immediately stop chasing.
		ed->state.skillstate = MSS_IDLE;
		ed->next_walktime=tick+rnd()%3000+3000;
		break;
	}
	if (ed->target_id) {
		ed->target_id=0;
		ed->ud.target = 0;
	}
	return 0;
}

/*==========================================
 * AI of ELEM whose is near a Player
 *------------------------------------------*/
static bool elem_ai_sub_hard(struct elemental_data *ed, unsigned int tick)
{
	struct block_list *tbl = NULL, *abl = NULL;
	int dist;
	int mode;
	int search_size;
	int view_range, can_move;

	if( ed->bl.prev == NULL || ed->battle_status.hp <= 0 )
		return false;

	if( DIFF_TICK(tick, ed->last_thinktime) < MIN_ELEMTHINKTIME )
		return false;

	ed->last_thinktime = tick;

	if (ed->ud.skilltimer != INVALID_TIMER)
		return false;

	if(ed->ud.walktimer != INVALID_TIMER && ed->ud.walkpath.path_pos <= 3)
		return false;

	// Abnormalities
	if( (ed->sc.opt1 > 0 && ed->sc.opt1 != OPT1_STONEWAIT && ed->sc.opt1 != OPT1_BURNING) ||
		ed->sc.data[SC_BLADESTOP] ||
		ed->sc.data[SC_DEEPSLEEP] ||
		ed->sc.data[SC_CRYSTALIZE] ||
		ed->sc.data[SC__MANHOLE] ||
		ed->sc.data[SC_FALLENEMPIRE] )
  	{	//Should reset targets.
		ed->target_id = ed->attacked_id = 0;
		return false;
	}

	if (ed->sc.count && ed->sc.data[SC_BLIND])
		view_range = 3;
	else
		view_range = ed->db->range2;
	mode = status_get_mode(&ed->bl);

	can_move = (mode&MD_CANMOVE)&&unit_can_move(&ed->bl);

	if( ed->target_id )
	{	//Check validity of current target.
		tbl = map_id2bl(ed->target_id);
		if( !tbl || tbl->m != ed->bl.m ||
			(ed->ud.attacktimer == INVALID_TIMER && !status_check_skilluse(&ed->bl, tbl, 0, 0, 0)) ||
			(ed->ud.walktimer != INVALID_TIMER && !(battle_config.elem_ai&0x1) && !check_distance_bl(&ed->bl, tbl, ed->min_chase)) ||
			(
				tbl->type == BL_PC &&
				((((TBL_PC*)tbl)->state.gangsterparadise && !(mode&MD_BOSS)) ||
				((TBL_PC*)tbl)->invincible_timer != INVALID_TIMER)
		) )
		{	//Unlock current target.
			//if (elem_warpchase(ed, tbl))
			//	return true; //Chasing this target.
			elem_unlocktarget(ed, tick-(battle_config.elem_ai&0x8?3000:0)); //Imediately do random walk.
			tbl = NULL;
		}
	}

	// Check for target change.
	if( ed->attacked_id && mode&MD_CANATTACK )
	{
		if( ed->attacked_id == ed->target_id )
		{	//Rude attacked check.
			if( !battle_check_range(&ed->bl, tbl, ed->battle_status.rhw.range)
			&&  ( //Can't attack back and can't reach back.
			      (!can_move && DIFF_TICK(tick, ed->ud.canmove_tick) > 0 && (battle_config.elem_ai&0x2 || (ed->sc.data[SC_SPIDERWEB] && ed->sc.data[SC_SPIDERWEB]->val1) ||
					ed->sc.data[SC_DEEPSLEEP] || ed->sc.data[SC_CRYSTALIZE] || ed->sc.data[SC_WUGBITE] || ed->sc.data[SC__MANHOLE] || ed->sc.data[SC_VACUUM_EXTREME] || ed->sc.data[SC_THORNS_TRAP] ))
			      || !elem_can_reach(ed, tbl, ed->min_chase, MSS_RUSH)
			    )
			&&  ed->state.attacked_count++ >= ELEM_RUDE_ATTACKED_COUNT
			&&  !elemskill_use(ed, tick, MSC_RUDEATTACKED) // If can't rude Attack
			&&  can_move && unit_escape(&ed->bl, tbl, rnd()%10 +1)) // Attempt escape
			{	//Escaped
				ed->attacked_id = 0;
				return true;
			}
		}
		else
		if( (abl = map_id2bl(ed->attacked_id)) && (!tbl || elem_can_changetarget(ed, abl, mode)) )
		{
			if( ed->bl.m != abl->m || abl->prev == NULL
				|| (dist = distance_bl(&ed->bl, abl)) >= MAX_ELEM_MINCHASE // Attacker longer than visual area
				|| battle_check_target(&ed->bl, abl, BCT_ENEMY) <= 0 // Attacker is not enemy of elem
				|| status_isdead(abl) // Attacker is Dead (Reflecting Damage?)
				|| (battle_config.elem_ai&0x2 && !status_check_skilluse(&ed->bl, abl, 0, 0, 0)) // Cannot normal attack back to Attacker
				|| (!battle_check_range(&ed->bl, abl, ed->battle_status.rhw.range) // Not on Melee Range and ...
				&& ( // Reach check
					(!can_move && DIFF_TICK(tick, ed->ud.canmove_tick) > 0 && (battle_config.elem_ai&0x2 || (ed->sc.data[SC_SPIDERWEB] && ed->sc.data[SC_SPIDERWEB]->val1) ||
					ed->sc.data[SC_DEEPSLEEP] || ed->sc.data[SC_CRYSTALIZE] || ed->sc.data[SC_WUGBITE] || ed->sc.data[SC__MANHOLE] || ed->sc.data[SC_VACUUM_EXTREME] || ed->sc.data[SC_THORNS_TRAP] ))
					|| !elem_can_reach(ed, abl, dist+ed->db->range3, MSS_RUSH)
				)
				) )
			{ // Rude attacked
				if (ed->state.attacked_count++ >= ELEM_RUDE_ATTACKED_COUNT
				&& !elemskill_use(ed, tick, MSC_RUDEATTACKED) && can_move
				&& !tbl && unit_escape(&ed->bl, abl, rnd()%10 +1))
				{	//Escaped.
					//TODO: Maybe it shouldn't attempt to run if it has another, valid target?
					ed->attacked_id = 0;
					return true;
				}
			}
			else
			if (!(battle_config.elem_ai&0x2) && !status_check_skilluse(&ed->bl, abl, 0, 0, 0))
			{
				//Can't attack back, but didn't invoke a rude attacked skill...
			}
			else
			{ //Attackable
				if (!tbl || dist < ed->battle_status.rhw.range || !check_distance_bl(&ed->bl, tbl, dist)
					|| battle_gettarget(tbl) != ed->bl.id)
				{	//Change if the new target is closer than the actual one
					//or if the previous target is not attacking the elem.
					ed->target_id = ed->attacked_id; // set target
					if (ed->state.attacked_count)
					  ed->state.attacked_count--; //Should we reset rude attack count?
					ed->min_chase = dist+ed->db->range3;
					if(ed->min_chase>MAX_ELEM_MINCHASE)
						ed->min_chase=MAX_ELEM_MINCHASE;
					tbl = abl; //Set the new target
				}
			}
		}
	
		//Clear it since it's been checked for already.
		ed->attacked_id = 0;
	}

	// Processing of slave monster
	if (ed->master->bl.id > 0 && elem_ai_sub_hard_slaveelem(ed, tick))
		return true;

	if ((!tbl && mode&MD_AGGRESSIVE) || ed->state.skillstate == MSS_FOLLOW)
	{
		map_foreachinrange (elem_ai_sub_hard_activesearch, &ed->bl, view_range, DEFAULT_ELEM_ENEMY_TYPE(ed), ed, &tbl, mode);
	}
	else
	if (mode&MD_CHANGECHASE && (ed->state.skillstate == MSS_RUSH || ed->state.skillstate == MSS_FOLLOW || (ed->sc.count && ed->sc.data[SC_CONFUSION])))
	{
		search_size = view_range<ed->battle_status.rhw.range ? view_range:ed->battle_status.rhw.range;
		map_foreachinrange (elem_ai_sub_hard_changechase, &ed->bl, search_size, DEFAULT_ELEM_ENEMY_TYPE(ed), ed, &tbl);
	}

	if (!tbl) { //No targets available.
		if (mode&MD_ANGRY && !ed->state.aggressive)
			ed->state.aggressive = 1; //Restore angry state when no targets are available.
		//This handles triggering idle walk/skill.
		elem_unlocktarget(ed, tick);
		return true;
	}
	
	//Target exists, attack as applicable.
	//Attempt to attack.
	//At this point we know the target is attackable, we just gotta check if the range matches.
	if (ed->ud.target == tbl->id && ed->ud.attacktimer != INVALID_TIMER) //Already locked.
		return true;
	
	if (battle_check_range (&ed->bl, tbl, ed->battle_status.rhw.range))
	{	//Target within range, engage
		if(tbl->type == BL_PC)
			elem_log_damage(ed, tbl, 0); //Log interaction (counts as 'attacker' for the exp bonus)
		unit_attack(&ed->bl,tbl->id,1);
		return true;
	}

	//Out of range...
	if (!(mode&MD_CANMOVE))
	{	//Can't chase. Attempt an idle skill before unlocking.
		ed->state.skillstate = MSS_IDLE;
		if (!elemskill_use(ed, tick, -1))
			elem_unlocktarget(ed,tick);
		return true;
	}

	if (!can_move)
	{	//Stuck. Attempt an idle skill
		ed->state.skillstate = MSS_IDLE;
		if (!(++ed->ud.walk_count%ELEM_IDLE_SKILL_INTERVAL))
			elemskill_use(ed, tick, -1);
		return true;
	}

	if (ed->ud.walktimer != INVALID_TIMER && ed->ud.target == tbl->id &&
		(
			!(battle_config.elem_ai&0x1) ||
			check_distance_blxy(tbl, ed->ud.to_x, ed->ud.to_y, ed->battle_status.rhw.range)
	)) //Current target tile is still within attack range.
		return true;

	//Follow up if possible.
	if(!elem_can_reach(ed, tbl, ed->min_chase, MSS_RUSH) ||
		!unit_walktobl(&ed->bl, tbl, ed->battle_status.rhw.range, 2))
		elem_unlocktarget(ed,tick);

	return true;
}

static int elem_ai_sub_hard_timer(struct block_list *bl,va_list ap)
{
	struct elemental_data *ed = (struct elemental_data*)bl;
	unsigned int tick = va_arg(ap, unsigned int);
	if (elem_ai_sub_hard(ed, tick)) 
	{	//Hard AI triggered.
		if(!ed->state.spotted)
			ed->state.spotted = 1;
		ed->last_pcneartime = tick;
	}
	return 0;
}

/*==========================================
 * Serious processing for elem in PC field of view (foreachclient)
 *------------------------------------------*/
static int elem_ai_sub_foreachclient(struct map_session_data *sd,va_list ap)
{
	unsigned int tick;
	tick=va_arg(ap,unsigned int);
	map_foreachinrange(elem_ai_sub_hard_timer,&sd->bl, AREA_SIZE+ACTIVE_ELEM_AI_RANGE, BL_ELEM,tick);

	return 0;
}

/*==========================================
 * Serious processing for elem in PC field of view   (interval timer function)
 *------------------------------------------*/
static int elem_ai_hard(int tid, int64 tick, int id, intptr data)
{
	map_foreachpc(elem_ai_sub_foreachclient,tick);

	return 0;
}

void elem_log_damage(struct elemental_data *ed, struct block_list *src, int damage)
{
	int char_id = 0, flag = MDLF_NORMAL;

	if( damage < 0 )
		return; //Do nothing for absorbed damage.
	if( !damage && !(src->type&DEFAULT_ELEM_ENEMY_TYPE(ed)) )
		return; //Do not log non-damaging effects from non-enemies.
	if( src->id == ed->bl.id )
		return; //Do not log self-damage.

	switch( src->type )
	{
		case BL_PC: 
		{
			struct map_session_data *sd = (TBL_PC*)src;
			char_id = sd->status.char_id;
			if( damage )
				ed->attacked_id = src->id;
			break;
		}
		case BL_HOM:
		{
			struct homun_data *hd = (TBL_HOM*)src;
			flag = MDLF_HOMUN;
			if( hd->master )
				char_id = hd->master->status.char_id;
			if( damage )
				ed->attacked_id = src->id;
			break;
		}
		case BL_MER:
		{
			struct mercenary_data *mer = (TBL_MER*)src;
			if( mer->master )
				char_id = mer->master->status.char_id;
			if( damage )
				ed->attacked_id = src->id;
			break;
		}
		case BL_ELEM:
		{
			struct elemental_data *ed2 = (TBL_ELEM*)src;
			if( ed2->master )
				char_id = ed2->master->status.char_id;
			if( damage )
				ed->attacked_id = src->id;
			break;
		}
		case BL_PET:
		{
			struct pet_data *pd = (TBL_PET*)src;
			flag = MDLF_PET;
			if( pd->master )
			{
				char_id = pd->master->status.char_id;
				if( damage ) //Let mobs retaliate against the pet's master
					ed->attacked_id = pd->master->bl.id;
			}
			break;
		}
		case BL_MOB:
		{
			struct mob_data* md = (TBL_MOB*)src;
			if( md->special_state.ai && md->master_id )
			{
				struct map_session_data* msd = map_id2sd(md->master_id);
				if( msd )
					char_id = msd->status.char_id;
			}
			if( !damage )
				break;
			//Let players decide whether to retaliate versus the master or the mob.
			if( md->master_id && battle_config.retaliate_to_master )
				ed->attacked_id = md->master_id;
			else
				ed->attacked_id = src->id;
			break;
		}
		default: //For all unhandled types.
			ed->attacked_id = src->id;
	}
	
	if( char_id )
	{ //Log damage...
		int i,minpos;
		unsigned int mindmg;
		for(i=0,minpos=DAMAGELOG_SIZE-1,mindmg=UINT_MAX;i<DAMAGELOG_SIZE;i++){
			if(ed->dmglog[i].id==char_id &&
				ed->dmglog[i].flag==flag)
				break;
			if(ed->dmglog[i].id==0) {	//Store data in first empty slot.
				ed->dmglog[i].id  = char_id;
				ed->dmglog[i].flag= flag;
				break;
			}
			if(ed->dmglog[i].dmg<mindmg && i)
			{	//Never overwrite first hit slot (he gets double exp bonus)
				minpos=i;
				mindmg=ed->dmglog[i].dmg;
			}
		}
		if(i<DAMAGELOG_SIZE)
			ed->dmglog[i].dmg+=damage;
		else {
			ed->dmglog[minpos].id  = char_id;
			ed->dmglog[minpos].flag= flag;
			ed->dmglog[minpos].dmg = damage;
		}
	}
	return;
}

void elemental_damage(struct elemental_data *ed, struct block_list *src, int hp, int sp) {
	int damage = hp;

	if (damage > 0)
	{	//Store total damage...
		if (UINT_MAX - (unsigned int)damage > ed->tdmg)
			ed->tdmg+=damage;
		else if (ed->tdmg == UINT_MAX)
			damage = 0; //Stop recording damage once the cap has been reached.
		else { //Cap damage log...
			damage = (int)(UINT_MAX - ed->tdmg);
			ed->tdmg = UINT_MAX;
		}
		if (ed->state.aggressive)
		{	//No longer aggressive, change to retaliate AI.
			ed->state.aggressive = 0;
			if(ed->state.skillstate== MSS_ANGRY)
				ed->state.skillstate = MSS_BERSERK;
			if(ed->state.skillstate== MSS_FOLLOW)
				ed->state.skillstate = MSS_RUSH;
		}
		//Log damage
		if (src) elem_log_damage(ed, src, damage);
	}

	if (!src)
		return;

	if( hp )
		clif_elemental_updatestatus(ed->master, SP_HP);
	if( sp )
		clif_elemental_updatestatus(ed->master, SP_SP);
}

void elemental_heal(struct elemental_data *ed, int hp, int sp) {
	if( hp )
		clif_elemental_updatestatus(ed->master, SP_HP);
	if( sp )
		clif_elemental_updatestatus(ed->master, SP_SP);
}

int elemental_dead(struct elemental_data *ed, struct block_list *src) {
	// I might need to put something more here. [Rytech]
	elem_delete(ed, 1);
	return 0;
}

int elemental_passive_skill(struct elemental_data *ed)
{
	nullpo_ret(ed);

	switch ( ed->db->class_ )
	{// Each elemental has its own unique passive skill.
		case MOBID_EL_AGNI_S:	return EL_PYROTECHNIC;
		case MOBID_EL_AGNI_M:	return EL_HEATER;
		case MOBID_EL_AGNI_L:	return EL_TROPIC;
		case MOBID_EL_AQUA_S:	return EL_AQUAPLAY;
		case MOBID_EL_AQUA_M:	return EL_COOLER;
		case MOBID_EL_AQUA_L:	return EL_CHILLY_AIR;
		case MOBID_EL_VENTUS_S:	return EL_GUST;
		case MOBID_EL_VENTUS_M:	return EL_BLAST;
		case MOBID_EL_VENTUS_L:	return EL_WILD_STORM;
		case MOBID_EL_TERA_S:	return EL_PETROLOGY;
		case MOBID_EL_TERA_M:	return EL_CURSED_SOIL;
		case MOBID_EL_TERA_L:	return EL_UPHEAVAL;
	}

	// Give it bash if no skill was selected to avoid issues.
	return SM_BASH;
}

int elemental_defensive_skill(struct elemental_data *ed)
{
	nullpo_ret(ed);

	switch ( ed->db->class_ )
	{// Each elemental has its own unique defensive skill.
		case MOBID_EL_AGNI_S:	return EL_CIRCLE_OF_FIRE;
		case MOBID_EL_AGNI_M:	return EL_FIRE_CLOAK;
		case MOBID_EL_AGNI_L:	return EL_FIRE_MANTLE;
		case MOBID_EL_AQUA_S:	return EL_WATER_SCREEN;
		case MOBID_EL_AQUA_M:	return EL_WATER_DROP;
		case MOBID_EL_AQUA_L:	return EL_WATER_BARRIER;
		case MOBID_EL_VENTUS_S:	return EL_WIND_STEP;
		case MOBID_EL_VENTUS_M:	return EL_WIND_CURTAIN;
		case MOBID_EL_VENTUS_L:	return EL_ZEPHYR;
		case MOBID_EL_TERA_S:	return EL_SOLID_SKIN;
		case MOBID_EL_TERA_M:	return EL_STONE_SHIELD;
		case MOBID_EL_TERA_L:	return EL_POWER_OF_GAIA;
	}

	// Give it bash if no skill was selected to avoid issues.
	return SM_BASH;
}

int elemental_offensive_skill(struct elemental_data *ed)
{
	nullpo_ret(ed);

	switch ( ed->db->class_ )
	{// Each elemental has its own unique offensive skill.
		case MOBID_EL_AGNI_S:	return EL_FIRE_ARROW;
		case MOBID_EL_AGNI_M:	return EL_FIRE_BOMB;
		case MOBID_EL_AGNI_L:	return EL_FIRE_WAVE;
		case MOBID_EL_AQUA_S:	return EL_ICE_NEEDLE;
		case MOBID_EL_AQUA_M:	return EL_WATER_SCREW;
		case MOBID_EL_AQUA_L:	return EL_TIDAL_WEAPON;
		case MOBID_EL_VENTUS_S:	return EL_WIND_SLASH;
		case MOBID_EL_VENTUS_M:	return EL_HURRICANE;
		case MOBID_EL_VENTUS_L:	return EL_TYPOON_MIS;
		case MOBID_EL_TERA_S:	return EL_STONE_HAMMER;
		case MOBID_EL_TERA_M:	return EL_ROCK_CRUSHER;
		case MOBID_EL_TERA_L:	return EL_STONE_RAIN;
	}

	// Give it bash if no skill was selected to avoid issues.
	return SM_BASH;
}

/*==========================================
 * Skill use judging
 *------------------------------------------*/
int elemskill_use(struct elemental_data *ed, int64 tick, int bypass)
{
	struct block_list *fbl = NULL;
	struct block_list *bl;
	struct elemental_data *fed = NULL;
	short skill_id;
	int cast_time = 0;

	nullpo_ret(ed);

	if (ed->ud.skilltimer != INVALID_TIMER)
		return 0;

	// Only allow autocasting offensive skills in offensive mode unless config allows in defensive mode.
	if ( !(ed->sc.data[SC_EL_OFFENSIVE] || battle_config.elem_defensive_attack_skill) )
		return 0;

	// Skill act delay only affects non-event skills.
	if (bypass == -1 && DIFF_TICK(ed->ud.canact_tick, tick) > 0)
		return 0;

	// Aftercast delay check.
	if (DIFF_TICK(tick, ed->skilldelay) < battle_config.elem_offensive_skill_aftercast)
		return 0;

	// Cast chance.
	if (rnd() % 100 > battle_config.elem_offensive_skill_chance)
		return 0;

	// Cast time should only be applied when
	// the skill is naturally autocasted.
	if ( bypass == -1 )
		cast_time = battle_config.elem_offensive_skill_casttime;

	// Check which skill should be casted depending on the elemental.
	skill_id = elemental_offensive_skill(ed);

	// Elementals have no ground targeted skills but
	// best to have code for it anyway just in case.
	if (skill_get_casttype(skill_id) == CAST_GROUND)
	{// Ground targeted skills. Target the ground where the enemy stands.
		short x, y;

		bl = map_id2bl(ed->target_id);

		if (!bl)
			return 0;

		x = bl->x;
		y = bl->y;

		map_freeblock_lock();

		if( !battle_check_range(&ed->bl,bl,skill_get_range2(&ed->bl, skill_id, 1)) ||
			!unit_skilluse_pos2(&ed->bl, x, y, skill_id, 1, cast_time, 0) )
		{
			map_freeblock_unlock();
			return 0;
		}
	}
	else
	{// Entity targeted skills (Enemy/All types)
		bl = map_id2bl(ed->target_id);

		if (!bl)
			return 0;

		map_freeblock_lock();

		if( !battle_check_range(&ed->bl,bl,skill_get_range2(&ed->bl, skill_id, 1)) ||
			!unit_skilluse_id2(&ed->bl, bl->id, skill_id, 1, cast_time, 0) )
		{
			map_freeblock_unlock();
			return 0;
		}
	}

	// Aftercast delay should only be applied
	// when the skill is naturally autocasted.
	if ( bypass == -1 )
		ed->skilldelay = tick;

	map_freeblock_unlock();

	return 1;
}

int elemental_set_control_mode(struct elemental_data *ed, short control_mode)
{
	if( ed == NULL || ed->db == NULL )
		return -1;

	if ( control_mode < CONTROL_WAIT || control_mode > CONTROL_OFFENSIVE )
	{
		ShowError("elemental_set_control_state : Invalid control state %d given.\n", control_mode);
		return -1;
	}

	// Attempting to set a control mode thats already active? Set it to wait.
	if (control_mode == CONTROL_PASSIVE && ed->sc.data[SC_EL_PASSIVE] || 
		control_mode == CONTROL_DEFENSIVE && ed->sc.data[SC_EL_DEFENSIVE] || 
		control_mode == CONTROL_OFFENSIVE && ed->sc.data[SC_EL_OFFENSIVE])
		control_mode = CONTROL_WAIT;

	// Remove all control status's before setting a new one.
	status_change_end(&ed->bl, SC_EL_WAIT, INVALID_TIMER);
	status_change_end(&ed->bl, SC_EL_PASSIVE, INVALID_TIMER);
	status_change_end(&ed->bl, SC_EL_DEFENSIVE, INVALID_TIMER);
	status_change_end(&ed->bl, SC_EL_OFFENSIVE, INVALID_TIMER);

	// Unlock target to stop attacking after mode status removal.
	elem_unlocktarget(ed, gettick());

	// Ready to set the new mode.
	switch ( control_mode )
	{
		case CONTROL_WAIT:
			sc_start(&ed->bl, SC_EL_WAIT, 100, 1, 0);
			break;

		case CONTROL_PASSIVE:
			sc_start(&ed->bl, SC_EL_PASSIVE, 100, ed->battle_status.size + 1, 0);// SP Cost depends on summon LV.
			unit_skilluse_id(&ed->bl, ed->bl.id, elemental_passive_skill(ed), 1);
			break;

		case CONTROL_DEFENSIVE:
			if (ed->battle_status.size != 2)// Lv 3 summons don't enter defensive mode. They only cast a AoE.
				sc_start(&ed->bl, SC_EL_DEFENSIVE, 100, 1, 0);
			unit_skilluse_id(&ed->bl, ed->bl.id, elemental_defensive_skill(ed), 1);
			break;

		case CONTROL_OFFENSIVE:
			sc_start(&ed->bl, SC_EL_OFFENSIVE, 100, 1, 0);
			break;
	}

	return -1;
}

static bool read_elementaldb_sub(char* str[], int columns, int current)
{
	int ele;
	struct s_elemental_db *db;
	struct status_data *status;

	db = &elemental_db[current];
	db->class_ = atoi(str[0]);
	strncpy(db->sprite, str[1], NAME_LENGTH);
	strncpy(db->name, str[2], NAME_LENGTH);
	db->lv = atoi(str[3]);

	status = &db->status;
	db->vd.class_ = db->class_;

	status->rhw.range = atoi(str[4]);
	db->range2 = atoi(str[5]);
	db->range3 = atoi(str[6]);
	status->size = atoi(str[7]);
	status->race = atoi(str[8]);
	
	ele = atoi(str[9]);
	status->def_ele = ele%10;
	status->ele_lv = ele/20;
	if( status->def_ele >= ELE_MAX )
	{
		ShowWarning("Elemental %d has invalid element type %d (max element is %d)\n", db->class_, status->def_ele, ELE_MAX - 1);
		status->def_ele = ELE_NEUTRAL;
	}
	if( status->ele_lv < 1 || status->ele_lv > 4 )
	{
		ShowWarning("Elemental %d has invalid element level %d (max is 4)\n", db->class_, status->ele_lv);
		status->ele_lv = 1;
	}

	status->aspd_amount = 0;
	status->aspd_rate = 1000;
	status->speed = atoi(str[10]);
	status->dmotion = atoi(str[11]);

	return true;
}

int read_elementaldb(void)
{
	memset(elemental_db,0,sizeof(elemental_db));
	sv_readdb(db_path, "elemental_db.txt", ',', 12, 12, MAX_ELEMENTAL_CLASS, &read_elementaldb_sub);

	return 0;
}

int do_init_elemental(void)
{
	read_elementaldb();

	//add_timer_func_list(elemental_summon, "elemental_summon");
	add_timer_func_list(elem_ai_hard, "elem_ai_hard");
	add_timer_interval(gettick() + MIN_ELEMTHINKTIME, elem_ai_hard, 0, 0, MIN_ELEMTHINKTIME);
	return 0;
}

int do_final_elemental(void);
