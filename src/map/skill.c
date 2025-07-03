// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/showmsg.h"
#include "../common/random.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "../common/ers.h"

#include "skill.h"
#include "map.h"
#include "path.h"
#include "clif.h"
#include "pc.h"
#include "status.h"
#include "pet.h"
#include "homunculus.h"
#include "mercenary.h"
#include "elemental.h"
#include "mob.h"
#include "npc.h"
#include "battle.h"
#include "battleground.h"
#include "party.h"
#include "itemdb.h"
#include "script.h"
#include "intif.h"
#include "log.h"
#include "chrif.h"
#include "guild.h"
#include "date.h"
#include "unit.h"
#include "achievement.h"
#include "episode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>


#define SKILLUNITTIMER_INTERVAL	100
#define TIMERSKILL_INTERVAL	150

// ranges reserved for mapping skill ids to skilldb offsets
#define HM_SKILLRANGEMIN 1201// 1201 - 1400
#define HM_SKILLRANGEMAX HM_SKILLRANGEMIN+MAX_HOMUNSKILL
#define MC_SKILLRANGEMIN 1401// 1401 - 1600
#define MC_SKILLRANGEMAX MC_SKILLRANGEMIN+MAX_MERCSKILL
#define EL_SKILLRANGEMIN 1601// 1601 - 1800
#define EL_SKILLRANGEMAX EL_SKILLRANGEMIN+MAX_ELEMSKILL
#define GD_SKILLRANGEMIN 1801// 1801 - 2000
#define GD_SKILLRANGEMAX GD_SKILLRANGEMIN+MAX_GUILDSKILL

static struct eri *skill_unit_ers = NULL; //For handling skill_unit's [Skotlex]
static struct eri *skill_timer_ers = NULL; //For handling skill_timerskills [Skotlex]
static DBMap* bowling_db = NULL; // int mob_id -> struct mob_data*

DBMap* skillunit_db = NULL; // int id -> struct skill_unit*

/**
 * Skill Unit Persistency during endack routes (mostly for songs)
 **/
DBMap* skillusave_db = NULL; // char_id -> struct skill_usave
struct skill_usave {
	int skill_id, skill_lv;
};

struct s_skill_db skill_db[MAX_SKILL_DB];
struct s_skill_produce_db skill_produce_db[MAX_SKILL_PRODUCE_DB];
struct s_skill_arrow_db skill_arrow_db[MAX_SKILL_ARROW_DB];
struct s_skill_abra_db skill_abra_db[MAX_SKILL_ABRA_DB];
struct s_skill_spellbook_db skill_spellbook_db[MAX_SKILL_SPELLBOOK_DB];
struct s_skill_improvise_db skill_improvise_db[MAX_SKILL_IMPROVISE_DB];
struct s_skill_magicmushroom_db skill_magicmushroom_db[MAX_SKILL_MAGICMUSHROOM_DB];

struct s_skill_changematerial_db {
	t_itemid itemid;
	short rate;
	int qty[5];
	short qty_rate[5];
};
struct s_skill_changematerial_db skill_changematerial_db[MAX_SKILL_PRODUCE_DB];

struct s_skill_unit_layout skill_unit_layout[MAX_SKILL_UNIT_LAYOUT];
int firewall_unit_pos;
int icewall_unit_pos;
int earthstrain_unit_pos;
int fire_rain_unit_pos;

struct s_skill_nounit_layout skill_nounit_layout[MAX_SKILL_UNIT_LAYOUT];
int windcutter_nounit_pos;
int overbrand_nounit_pos;
int overbrand_brandish_nounit_pos;

//early declaration
static int skill_check_unit_range(struct block_list *bl, int x, int y, int skill_id, int skill_lv);
static int skill_check_unit_range2(struct block_list *bl, int x, int y, int skill_id, int skill_lv);
static bool skill_check_unit_movepos(uint8 check_flag, struct block_list *bl, short dst_x, short dst_y, int easy, bool checkpath);

//Since only mob-casted splash skills can hit ice-walls
static inline int splash_target(struct block_list* bl)
{
	nullpo_retr(BL_CHAR, bl);
	return ( bl->type == BL_MOB ) ? BL_SKILL|BL_CHAR : BL_CHAR;
}

static int skill_area_temp[8];

/// Returns the id of the skill, or 0 if not found.
int skill_name2id(const char* name)
{
	if( name == NULL )
		return 0;

	return strdb_iget(skilldb_name2id, name);
}

/// Maps skill ids to skill db offsets.
/// Returns the skill's array index, or 0 (Unknown Skill).
int skill_get_index(uint16 id)
{
	// avoid ranges reserved for mapping homunculus/mercenary/elemental/guild skills
	if( (id >= HM_SKILLRANGEMIN && id <= HM_SKILLRANGEMAX)
		|| (id >= MC_SKILLRANGEMIN && id <= MC_SKILLRANGEMAX)
		|| (id >= EL_SKILLRANGEMIN && id <= EL_SKILLRANGEMAX)
		|| (id >= GD_SKILLRANGEMIN && id <= GD_SKILLRANGEMAX) )
		return 0;

	// map skill id to skill db index
	if( id >= GD_SKILLBASE )
		id = GD_SKILLRANGEMIN + id - GD_SKILLBASE;
	else
	if( id >= EL_SKILLBASE )
		id = EL_SKILLRANGEMIN + id - EL_SKILLBASE;
	else
	if( id >= MC_SKILLBASE )
		id = MC_SKILLRANGEMIN + id - MC_SKILLBASE;
	else
	if( id >= HM_SKILLBASE )
		id = HM_SKILLRANGEMIN + id - HM_SKILLBASE;
	else
		; // identity

	// validate result
	if( id <= 0 || id >= MAX_SKILL_DB )
		return 0;

	return id;
}

const char* skill_get_name( int id )
{
	return skill_db[skill_get_index(id)].name;
}

const char* skill_get_desc( int id )
{
	return skill_db[skill_get_index(id)].desc;
}

/// out of bounds error checking [celest]
static void skill_chk(int16 *skill_id) {
	*skill_id = skill_get_index(*skill_id); // checks/adjusts id
}
// checks/adjusts level
static void skill_chk2(int16 *skill_lv) {
	*skill_lv = (*skill_lv < 1) ? 1 : (*skill_lv > MAX_SKILL_LEVEL) ? MAX_SKILL_LEVEL : *skill_lv;
}
// checks/adjusts index. make sure we don't use negative index
static void skill_chk3(int *idx) {
	if (*idx < 0) *idx = 0;
}

#define skill_get(var,id) { skill_chk(&id); if(!id) return 0; return var; }
#define skill_get2(var,id,lv) { skill_chk(&id); if (!id) return 0; skill_chk2(&lv); return var; }
#define skill_get3(var,id,x) { skill_chk(&id); if (!id) return 0; skill_chk3(&x); return var; }

// Skill DB
int skill_get_hit( uint16 skill_id )								{ skill_get (skill_db[skill_id].hit, skill_id); }
int skill_get_inf( uint16 skill_id )								{ skill_get (skill_db[skill_id].inf, skill_id); }
int skill_get_ele( uint16 skill_id , uint16 skill_lv )				{ skill_get2 (skill_db[skill_id].element[skill_lv-1], skill_id, skill_lv); }
int skill_get_nk( uint16 skill_id )									{ skill_get (skill_db[skill_id].nk, skill_id); }
int skill_get_max( uint16 skill_id )								{ skill_get (skill_db[skill_id].max, skill_id); }
int skill_get_range( uint16 skill_id , uint16 skill_lv )			{ skill_get2 (skill_db[skill_id].range[skill_lv-1], skill_id, skill_lv); }
int skill_get_splash( uint16 skill_id , uint16 skill_lv )			{ skill_get2 ( (skill_db[skill_id].splash[skill_lv-1]>=0?skill_db[skill_id].splash[skill_lv-1]:AREA_SIZE), skill_id, skill_lv);  }
int skill_get_num( uint16 skill_id ,uint16 skill_lv )				{ skill_get2 (skill_db[skill_id].num[skill_lv-1], skill_id, skill_lv); }
int skill_get_cast( uint16 skill_id ,uint16 skill_lv )				{ skill_get2 (skill_db[skill_id].cast[skill_lv-1], skill_id, skill_lv); }
int skill_get_delay( uint16 skill_id ,uint16 skill_lv )				{ skill_get2 (skill_db[skill_id].delay[skill_lv-1], skill_id, skill_lv); }
int skill_get_walkdelay( uint16 skill_id ,uint16 skill_lv )			{ skill_get2 (skill_db[skill_id].walkdelay[skill_lv-1], skill_id, skill_lv); }
int skill_get_time( uint16 skill_id ,uint16 skill_lv )				{ skill_get2 (skill_db[skill_id].upkeep_time[skill_lv-1], skill_id, skill_lv); }
int skill_get_time2( uint16 skill_id ,uint16 skill_lv )				{ skill_get2 (skill_db[skill_id].upkeep_time2[skill_lv-1], skill_id, skill_lv); }
int skill_get_castdef( uint16 skill_id )							{ skill_get (skill_db[skill_id].cast_def_rate, skill_id); }
int skill_get_inf2( uint16 skill_id )								{ skill_get (skill_db[skill_id].inf2, skill_id); }
int skill_get_castcancel( uint16 skill_id )							{ skill_get (skill_db[skill_id].castcancel, skill_id); }
int skill_get_maxcount( uint16 skill_id ,uint16 skill_lv )			{ skill_get2 (skill_db[skill_id].maxcount[skill_lv-1], skill_id, skill_lv); }
int skill_get_blewcount( uint16 skill_id ,uint16 skill_lv )			{ skill_get2 (skill_db[skill_id].blewcount[skill_lv-1], skill_id, skill_lv); }
int skill_get_castnodex( uint16 skill_id ,uint16 skill_lv )			{ skill_get2 (skill_db[skill_id].castnodex[skill_lv-1], skill_id, skill_lv); }
int skill_get_delaynodex( uint16 skill_id ,uint16 skill_lv )		{ skill_get2 (skill_db[skill_id].delaynodex[skill_lv-1], skill_id, skill_lv); }
int skill_get_nocast ( uint16 skill_id )							{ skill_get (skill_db[skill_id].nocast, skill_id); }
int skill_get_type( uint16 skill_id )								{ skill_get (skill_db[skill_id].skill_type, skill_id); }
int skill_get_unit_id ( uint16 skill_id, int flag )					{ skill_get3 (skill_db[skill_id].unit_id[flag], skill_id, flag); }
int skill_get_unit_interval( uint16 skill_id )						{ skill_get (skill_db[skill_id].unit_interval, skill_id); }
int skill_get_unit_range( uint16 skill_id, uint16 skill_lv )		{ skill_get2 (skill_db[skill_id].unit_range[skill_lv-1], skill_id, skill_lv); }
int skill_get_unit_target( uint16 skill_id )						{ skill_get (skill_db[skill_id].unit_target&BCT_ALL, skill_id); }
int skill_get_unit_bl_target( uint16 skill_id )						{ skill_get (skill_db[skill_id].unit_target&BL_ALL, skill_id); }
int skill_get_unit_flag( uint16 skill_id )							{ skill_get (skill_db[skill_id].unit_flag, skill_id); }
int skill_get_unit_layout_type( uint16 skill_id ,uint16 skill_lv )	{ skill_get2 (skill_db[skill_id].unit_layout_type[skill_lv-1], skill_id, skill_lv); }
int skill_get_cooldown( uint16 skill_id, uint16 skill_lv )			{ skill_get2 (skill_db[skill_id].cooldown[skill_lv-1], skill_id, skill_lv); }
int skill_get_fixed_cast( uint16 skill_id, uint16 skill_lv )		{ skill_get2 (skill_db[skill_id].fixed_cast[skill_lv - 1], skill_id, skill_lv); }

// Skill requirements
int skill_get_hp( uint16 skill_id ,uint16 skill_lv )        { skill_get2 (skill_db[skill_id].require.hp[skill_lv-1], skill_id, skill_lv); }
int skill_get_mhp( uint16 skill_id ,uint16 skill_lv )       { skill_get2 (skill_db[skill_id].require.mhp[skill_lv-1], skill_id, skill_lv); }
int skill_get_sp( uint16 skill_id ,uint16 skill_lv )        { skill_get2 (skill_db[skill_id].require.sp[skill_lv-1], skill_id, skill_lv); }
int skill_get_hp_rate( uint16 skill_id, uint16 skill_lv )   { skill_get2 (skill_db[skill_id].require.hp_rate[skill_lv-1], skill_id, skill_lv); }
int skill_get_sp_rate( uint16 skill_id, uint16 skill_lv )   { skill_get2 (skill_db[skill_id].require.sp_rate[skill_lv-1], skill_id, skill_lv); }
int skill_get_zeny( uint16 skill_id ,uint16 skill_lv )      { skill_get2 (skill_db[skill_id].require.zeny[skill_lv-1], skill_id, skill_lv); }
int skill_get_weapontype( uint16 skill_id )                 { skill_get (skill_db[skill_id].require.weapon, skill_id); }
int skill_get_ammotype( uint16 skill_id )                   { skill_get (skill_db[skill_id].require.ammo, skill_id); }
int skill_get_ammo_qty( uint16 skill_id, uint16 skill_lv )  { skill_get2 (skill_db[skill_id].require.ammo_qty[skill_lv-1], skill_id, skill_lv); }
int skill_get_state( uint16 skill_id )                      { skill_get (skill_db[skill_id].require.state, skill_id); }
int skill_get_spiritball( uint16 skill_id, uint16 skill_lv ){ skill_get2 (skill_db[skill_id].require.spiritball[skill_lv-1], skill_id, skill_lv); }
int skill_get_itemid( uint16 skill_id, int idx )            { skill_get3 (skill_db[skill_id].require.itemid[idx], skill_id, idx); }
int skill_get_itemqty( uint16 skill_id, int idx )           { skill_get3 (skill_db[skill_id].require.amount[idx], skill_id, idx); }
int skill_get_itemeq( uint16 skill_id, int idx )            { skill_get3 (skill_db[skill_id].require.eqItem[idx], skill_id, idx); }

int skill_tree_get_max(int id, int b_class)
{
	int i;
	b_class = pc_class2idx(b_class);

	ARR_FIND( 0, MAX_SKILL_TREE, i, skill_tree[b_class][i].id == 0 || skill_tree[b_class][i].id == id );
	if( i < MAX_SKILL_TREE && skill_tree[b_class][i].id == id )
		return skill_tree[b_class][i].max;
	else
		return skill_get_max(id);
}

int skill_frostjoke_scream(struct block_list *bl,va_list ap);
int skill_attack_area(struct block_list *bl,va_list ap);
struct skill_unit_group *skill_locate_element_field(struct block_list *bl); // [Skotlex]
int skill_graffitiremover(struct block_list *bl, va_list ap); // [Valaris]
int skill_greed(struct block_list *bl, va_list ap);
int skill_detonator(struct block_list *bl, va_list ap); // [Jobbie]
static int skill_cell_overlap(struct block_list *bl, va_list ap);
int skill_flicker_bind_trap(struct block_list *bl, va_list ap);
static int skill_trap_splash(struct block_list *bl, va_list ap);
struct skill_unit_group_tickset *skill_unitgrouptickset_search(struct block_list *bl,struct skill_unit_group *sg,int64 tick);
static int skill_unit_onplace(struct skill_unit *src,struct block_list *bl,int64 tick);
int skill_unit_onleft(uint16 skill_id, struct block_list *bl,int64 tick);
static int skill_unit_effect(struct block_list *bl,va_list ap);
int skill_blockpc_get(struct map_session_data *sd, int skill_id);

int enchant_eff[5] = { 10, 14, 17, 19, 20 };
int deluge_eff[5] = { 5, 9, 12, 14, 15 };

int skill_get_casttype (int id)
{
	int inf = skill_get_inf(id);
	if (inf&(INF_GROUND_SKILL))
		return CAST_GROUND;
	if (inf&INF_SUPPORT_SKILL)
		return CAST_NODAMAGE;
	if (inf&INF_SELF_SKILL) {
		if(skill_get_inf2(id)&INF2_NO_TARGET_SELF)
			return CAST_DAMAGE; //Combo skill.
		return CAST_NODAMAGE;
	}
	if (skill_get_nk(id)&NK_NO_DAMAGE)
		return CAST_NODAMAGE;
	return CAST_DAMAGE;
}

//Returns actual skill range taking into account attack range and AC_OWL [Skotlex]
int skill_get_range2(struct block_list *bl, uint16 id, uint16 lv, bool isServer)
{
	int range;
	if( bl->type == BL_MOB && battle_config.mob_ai&0x400 )
		return 9; //Mobs have a range of 9 regardless of skill used.

	range = skill_get_range(id, lv);

	if( range < 0 )
	{
		if( battle_config.use_weapon_skill_range&bl->type )
			return status_get_range(bl);
		range *=-1;
	}

	if (isServer && range > 14) {
		range = 14; // Server-sided base range can't be above 14
	}

	if(bl->type == BL_PC && pc_checkskill((TBL_PC*)bl, WL_RADIUS) && id >= WL_WHITEIMPRISON && id <= WL_FREEZE_SP ) // 3ceam v1.
		range = range + (pc_checkskill((TBL_PC*)bl, WL_RADIUS));

	//TODO: Find a way better than hardcoding the list of skills affected by AC_VULTURE
	switch(id)
	{
		case AC_DOUBLE:			case MA_DOUBLE:
		case AC_SHOWER:			case MA_SHOWER:
		case HT_BLITZBEAT:		
		case AC_CHARGEARROW:	case MA_CHARGEARROW:	
		case SN_FALCONASSAULT:
		case HT_POWER:
		case RA_ARROWSTORM:
		case RA_AIMEDBOLT:
		case RA_WUGBITE:
			if( bl->type == BL_PC )
				range += pc_checkskill((TBL_PC*)bl, AC_VULTURE);
			else
				range += battle_config.mob_eye_range_bonus;
			break;
		// added to allow GS skills to be effected by the range of Snake Eyes [Reddozen]
		case GS_RAPIDSHOWER:
		case GS_PIERCINGSHOT:
		case GS_FULLBUSTER:
		case GS_SPREADATTACK:
		case GS_GROUNDDRIFT:
			if (bl->type == BL_PC)
				range += pc_checkskill((TBL_PC*)bl, GS_SNAKEEYE);
			else
				range += battle_config.mob_eye_range_bonus;
			break;
		case NJ_KIRIKAGE:
			if (bl->type == BL_PC)
				range = skill_get_range(NJ_SHADOWJUMP,pc_checkskill((TBL_PC*)bl,NJ_SHADOWJUMP));
			break;
		case WL_WHITEIMPRISON:
		case WL_SOULEXPANSION:
		//case WL_FROSTMISTY://Shows in official data this is needed. But why? [Rytech]
		case WL_MARSHOFABYSS:
		case WL_SIENNAEXECRATE:
		case WL_DRAINLIFE:
		case WL_CRIMSONROCK:
		case WL_HELLINFERNO:
		case WL_COMET:
		case WL_CHAINLIGHTNING:
		case WL_EARTHSTRAIN:
		case WL_TETRAVORTEX:
		case WL_RELEASE:
		//case WL_READING_SB:// Code shows this in here. Why when its self casted?
			if( bl->type == BL_PC )
				range += pc_checkskill((TBL_PC*)bl, WL_RADIUS);
			break;
		//Added to allow increasing traps range
		case HT_LANDMINE:
		case HT_FREEZINGTRAP:
		case HT_BLASTMINE:
		case HT_CLAYMORETRAP:
		case RA_CLUSTERBOMB:
		case RA_FIRINGTRAP:
 		case RA_ICEBOUNDTRAP:
			if( bl->type == BL_PC )
				range += (1 + pc_checkskill((TBL_PC*)bl, RA_RESEARCHTRAP))/2;
			break;
	}

	if( !range && bl->type != BL_PC )
		return 9; // Enable non players to use self skills on others. [Skotlex]
	return range;
}

/** Calculates heal value of skill's effect
 * @param src
 * @param target
 * @param skill_id
 * @param skill_lv
 * @param heal
 * @return modified heal value
 */
int skill_calc_heal(struct block_list *src, struct block_list *target, int skill_id, int skill_lv, bool heal)
{
	int skill, hp = 0;
	struct map_session_data *sd = BL_CAST(BL_PC, src);
	struct map_session_data *tsd = BL_CAST(BL_PC, target);
	struct status_change* sc;

	switch( skill_id )
	{
		case BA_APPLEIDUN:
			hp = 30 + 5 * skill_lv + (status_get_vit(src) / 2); // HP recovery
			if( sd )
				hp += 5*pc_checkskill(sd,BA_MUSICALLESSON);
			break;
		case PR_SANCTUARY:
			hp = (skill_lv>6)?777:skill_lv*100;
			break;
		case NPC_EVILLAND:
			hp = (skill_lv > 6) ? 666 : skill_lv * 100;
			break;
		case SU_TUNABELLY:// Is this affected by heal bonuses?
			hp = status_get_max_hp(target) * ( 20 * skill_lv - 10 ) / 100;
			break;
		default:
			if (skill_lv >= battle_config.max_heal_lv)
				return battle_config.max_heal;

			// 1st half of heal's formula.
			hp = ( status_get_lv(src) + status_get_int(src) ) / 8;

			// 2nd half of heal's formula.
			// Must check for learned level of heal when Highness Heal is used
			// so the highest level of heal usable is taken into the calculations.
			if ( skill_id == AB_HIGHNESSHEAL )
				hp = hp * ( 4 + (sd? pc_checkskill(sd, AL_HEAL) : 10) * 8 );
			else
				hp = hp * ( 4 + skill_lv * 8 );

			// Passive skills that increases healing by a percentage.
			if ( sd )
			{
				if( (skill = pc_checkskill(sd, HP_MEDITATIO)) > 0 )
					hp += hp * skill * 2 / 100;

				if( (pc_checkskill(sd, SU_POWEROFSEA) > 0) )
				{
					unsigned char sea_heal = 10;

					if ( skill_summoner_power(sd, POWER_OF_SEA) == 1 )
						sea_heal += 20;

					hp += hp * sea_heal / 100;
				}

				if( (skill = pc_checkskill(sd, NV_BREAKTHROUGH)) > 0 )
					hp += hp * skill * 2 / 100;

				if( (skill = pc_checkskill(sd, NV_TRANSCENDENCE)) > 0 )
					hp += hp * skill * 3 / 100;
			}
			else if( src->type == BL_HOM && (skill = hom_checkskill(((TBL_HOM*)src), HLIF_BRAIN)) > 0 )
				hp += hp * skill * 2 / 100;
			break;
	}

	// Highness Heal increases healing by a percentage.
	if ( skill_id == AB_HIGHNESSHEAL )
		hp += hp * ( 70 + 30 * skill_lv ) / 100;

	if( (!heal || (target && target->type == BL_MER) ) && skill_id != NPC_EVILLAND )
		hp >>= 1;

	if( sd && (skill = pc_skillheal_bonus(sd, skill_id)) )
		hp += hp * skill / 100;
		
	if( tsd && (skill = pc_skillheal2_bonus(tsd,skill_id)) )
		hp += hp * skill / 100;

	sc = status_get_sc(src);
	if( sc && sc->count )
	{
			if( sc->data[SC_OFFERTORIUM] )
				hp += hp * sc->data[SC_OFFERTORIUM]->val2 / 100;
			if( sc->data[SC_ANCILLA] )
				hp += hp * sc->data[SC_ANCILLA]->val2 / 100;
	}

	sc = status_get_sc(target);

	if( sc && sc->count )
	{
		if ( skill_id != NPC_EVILLAND && skill_id != BA_APPLEIDUN )
		{
			if (sc->data[SC_INCHEALRATE])// Only affects Heal, Sanctuary and PotionPitcher.(like bHealPower) [Inkfish]
				hp += hp * sc->data[SC_INCHEALRATE]->val1 / 100;// Highness Heal too. [Rytech]
			if ( sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 2 )
				hp += hp * 10 / 100;
		}
		if( heal && sc->data[SC_CRITICALWOUND] ) // Critical Wound has no effect on offensive heal. [Inkfish]
			hp -= hp * sc->data[SC_CRITICALWOUND]->val2/100;
		if( heal && sc->data[SC_DEATHHURT] )
			hp -= hp * 20 / 100;
	}

	return hp;
}

/// Making plagiarize check its own function
/// Returns:
///   0 - Cannot be copied
///   1 - Can be copied by Plagiarism
///   2 - Can be copied by Reproduce
static short skill_isCopyable(struct map_session_data *sd, uint16 skill_id) {
	int idx = skill_get_index(skill_id);

	// Only copy skill that player doesn't have or the skill is old clone
	if (sd->status.skill[idx].id != 0 && sd->status.skill[idx].flag != SKILL_FLAG_PLAGIARIZED)
		return 0;

	// Check if the skill is copyable by class
	uint16 job_allowed = skill_db[idx].copyable.joballowed;
	while (1) {
		if (job_allowed & 0x01 && sd->status.class_ == JOB_ROGUE) break;
		if (job_allowed & 0x02 && sd->status.class_ == JOB_STALKER) break;
		if (job_allowed & 0x04 && sd->status.class_ == JOB_SHADOW_CHASER) break;
		if (job_allowed & 0x08 && sd->status.class_ == JOB_SHADOW_CHASER_T) break;
		if (job_allowed & 0x10 && sd->status.class_ == JOB_BABY_ROGUE) break;
		if (job_allowed & 0x20 && sd->status.class_ == JOB_BABY_SHADOW_CHASER) break;
		return 0;
	}

	//Plagiarism only able to copy skill while SC_PRESERVE is not active and skill is copyable by Plagiarism
	if (skill_db[idx].copyable.option&1 && pc_checkskill(sd, RG_PLAGIARISM) && !sd->sc.data[SC_PRESERVE])
		return 1;

	//Reproduce can copy skill if SC__REPRODUCE is active and the skill is copyable by Reproduce
	if (skill_db[idx].copyable.option&2 && pc_checkskill(sd, SC_REPRODUCE) && (&sd->sc && sd->sc.data[SC__REPRODUCE]))
		return 2;

	return 0;
}

// [MouseJstr] - skill ok to cast? and when?
//done before check_condition_begin, requirement
int skillnotok (int skill_id, struct map_session_data *sd)
{
	int i,m;
	nullpo_retr (1, sd);
	m = sd->bl.m;
	i = skill_get_index(skill_id);

	if (i == 0)
		return 1; // invalid skill id

	if (battle_config.gm_skilluncond && pc_isGM(sd) >= battle_config.gm_skilluncond)
		return 0; // GMs can do any damn thing they want

	if( skill_id == AL_TELEPORT && sd->skillitem == skill_id && sd->skillitemlv > 2 )
		return 0; // Teleport lv 3 bypasses this check.[Inkfish]

	if (skill_id == ALL_EQSWITCH)
		return 0;

	if (map[m].flag.noskill)
		return 1;

	// Epoque:
	// This code will compare the player's attack motion value which is influenced by ASPD before
	// allowing a skill to be cast. This is to prevent no-delay ACT files from spamming skills such as
	// AC_DOUBLE which do not have a skill delay and are not regarded in terms of attack motion.
	if(!sd->state.autocast && sd->skillitem != skill_id && sd->canskill_tick && DIFF_TICK(gettick(), sd->canskill_tick) < (sd->battle_status.amotion * (battle_config.skill_amotion_leniency) / 100) )
	{// attempted to cast a skill before the attack motion has finished
		return 1;
	}

	/**
	 * It has been confirmed on a official server (thanks to Yommy) that item-cast skills bypass all the restrictions above
	 * Also, without this check, an exploit where an item casting + healing (or any other kind buff) isn't deleted after used on a restricted map
	 **/
	if (sd->skillitem == skill_id && !sd->skillitem_keep_requirement)
		return 0;

	if( skill_blockpc_get(sd,skill_id) != -1 )
		return 1;

	// Check skill restrictions [Celest]
	if((!map_flag_vs(m) && skill_get_nocast (skill_id) & 1) ||
		(map[m].flag.pvp && skill_get_nocast (skill_id) & 2) ||
		(map_flag_gvg(m) && skill_get_nocast (skill_id) & 4) ||
		(map[m].flag.battleground && skill_get_nocast (skill_id) & 8) ||
		(map[m].flag.restricted && map[m].zone && skill_get_nocast(skill_id) & (8 * map[m].zone)))
		{
			clif_msg(sd, SKILL_CANT_USE_AREA); // This skill cannot be used within this area
			return 1;
		}

	switch (skill_id) {
		case AL_WARP:
		case RETURN_TO_ELDICASTES:
		case ALL_GUARDIAN_RECALL:
		case ECLAGE_RECALL:
			if(map[m].flag.nowarp) {
				clif_skill_teleportmessage(sd,0);
				return 1;
			}
			return 0;
		case AL_TELEPORT:
		case SC_FATALMENACE:
		case SC_DIMENSIONDOOR:
		case ALL_ODINS_RECALL:
		case WE_CALLALLFAMILY:
		case AB_CONVENIO:
			if(map[m].flag.noteleport) {
				clif_skill_teleportmessage(sd,0);
				return 1;
			}
			return 0; // gonna be checked in 'skill_castend_nodamage_id'
		case WE_CALLPARTNER:
		case WE_CALLPARENT:
		case WE_CALLBABY:
			if (map[m].flag.nomemo) {
				clif_skill_teleportmessage(sd,1);
				return 1;
			}
			break;
		case MC_VENDING:
		case ALL_BUYING_STORE:
			if (map[sd->bl.m].flag.novending) {
				clif_displaymessage(sd->fd, msg_txt(sd, 276)); // "You can't open a shop on this map"
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
				return 1;
			}
			if (map_getcell(sd->bl.m, sd->bl.x, sd->bl.y, CELL_CHKNOVENDING)) {
				clif_displaymessage(sd->fd, msg_txt(sd, 204)); // "You can't open a shop on this cell."
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
				return 1;
			}
			if (npc_isnear(&sd->bl)) {
				// uncomment to send msg_txt.
				//char output[150];
				//sprintf(output, msg_txt(sd,725), battle_config.min_npc_vendchat_distance);
				//clif_displaymessage(sd->fd, output);
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_THERE_ARE_NPC_AROUND, 0, 0);
				return 1;
			}
		case MC_IDENTIFY:
			return 0; // always allowed
		case WZ_ICEWALL:
			// noicewall flag [Valaris]
			if (map[m].flag.noicewall) {
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				return 1;
			}
			break;
		//case WM_SIRCLEOFNATURE://Need a confirm if this is still restricted to player vs player maps only. [Rytech]
		case WM_SOUND_OF_DESTRUCTION:
		case WM_SATURDAY_NIGHT_FEVER:
			if( !map_flag_vs(m) )
			{
				clif_skill_teleportmessage(sd,2); // This skill uses this msg instead of skill fails.
				return 1;
			}
			break;
		case GD_EMERGENCYCALL:
		case GD_ITEMEMERGENCYCALL:
			if (
				!(battle_config.emergency_call&((agit_flag || agit2_flag)?2:1)) ||
				!(battle_config.emergency_call&(map[m].flag.gvg || map[m].flag.gvg_castle?8:4)) ||
				(battle_config.emergency_call&16 && map[m].flag.nowarpto && !map[m].flag.gvg_castle)
			)	{
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				return 1;
			}
			break;
	}
	return 0;
}

int skillnotok_homun(int skill_id, struct homun_data *hd)
{
	int i = skill_get_index(skill_id);
	nullpo_retr(1,hd);

	if (i == 0)
		return 1; // invalid skill id

	if (hd->blockskill[i] > 0)
		return 1;

	//Use master's criteria.
	return skillnotok(skill_id, hd->master);
}

int skillnotok_mercenary(int skill_id, struct mercenary_data *md)
{
	int i = skill_get_index(skill_id);
	nullpo_retr(1,md);

	if( i == 0 )
		return 1; // Invalid Skill ID
	if( md->blockskill[i] > 0 )
		return 1;

	return skillnotok(skill_id, md->master);
}

struct s_skill_unit_layout* skill_get_unit_layout (int skill_id, int skill_lv, struct block_list* src, int x, int y, int dir){
	int pos = skill_get_unit_layout_type(skill_id,skill_lv);

	if (pos < -1 || pos >= MAX_SKILL_UNIT_LAYOUT) {
		ShowError("skill_get_unit_layout: unsupported layout type %d for skill %d (level %d)\n", pos, skill_id, skill_lv);
		pos = cap_value(pos, 0, MAX_SQUARE_LAYOUT); // cap to nearest square layout
	}

	nullpo_retr(NULL, src);
	//Monsters sometimes deploy more units on level 10
	if (src->type == BL_MOB && skill_lv >= 10) {
		if (skill_id == WZ_WATERBALL)
			pos = 4; //9x9 Area
	}

	if (pos != -1) // simple single-definition layout
		return &skill_unit_layout[pos];

	if( dir == -1 )
		dir = (src->x == x && src->y == y) ? 6 : map_calc_dir(src,x,y); // 6 - default aegis direction

	if( skill_id == MG_FIREWALL )
		return &skill_unit_layout [firewall_unit_pos + dir];
	else if (skill_id == WZ_ICEWALL)
		return &skill_unit_layout [icewall_unit_pos + dir];
	else if( skill_id == WL_EARTHSTRAIN )
 		return &skill_unit_layout [earthstrain_unit_pos + dir];
	else if (skill_id == RL_FIRE_RAIN)
		return &skill_unit_layout[fire_rain_unit_pos + dir];

	ShowError("skill_get_unit_layout: unknown unit layout for skill %d (level %d)\n", skill_id, skill_lv);
	return &skill_unit_layout[0]; // default 1x1 layout
}

struct s_skill_nounit_layout* skill_get_nounit_layout (int skill_id, int skill_lv, struct block_list* src, int x, int y, int dir)
{
	if( skill_id == RK_WINDCUTTER )
		return &skill_nounit_layout[windcutter_nounit_pos + dir];
	else if( skill_id == LG_OVERBRAND )
		return &skill_nounit_layout[overbrand_nounit_pos + dir];
	else if( skill_id == LG_OVERBRAND_BRANDISH )
		return &skill_nounit_layout[overbrand_brandish_nounit_pos + dir];

	ShowError("skill_get_nounit_layout: unknown no-unit layout for skill %d (level %d)\n", skill_id, skill_lv);
	return &skill_nounit_layout[0];
}

/*==========================================
 *
 *------------------------------------------*/
int skill_additional_effect (struct block_list* src, struct block_list *bl, uint16 skill_id, uint16 skill_lv, int attack_type, int dmg_lv, int64 tick)
{
	struct map_session_data *sd, *dstsd;
	struct mob_data *md, *dstmd;
	struct homun_data *hd;
	struct elemental_data *ed;
	struct status_data *sstatus, *tstatus;
	struct status_change *sc, *tsc;
	int s_job_level = 50;
	bool level_effect_bonus = battle_config.renewal_level_effect_skills;// Base/Job level effect on formula's.

	enum sc_type status;
	int skill;
	int rate;

	nullpo_ret(src);
	nullpo_ret(bl);

	if(skill_id > 0 && !skill_lv) return 0;	// don't forget auto attacks! - celest

	if( dmg_lv < ATK_BLOCK ) // Don't apply effect if miss.
		return 0;

	sd = BL_CAST(BL_PC, src);
	md = BL_CAST(BL_MOB, src);
	hd = BL_CAST(BL_HOM, src);
	ed = BL_CAST(BL_ELEM, src);
	dstsd = BL_CAST(BL_PC, bl);
	dstmd = BL_CAST(BL_MOB, bl);

	sc = status_get_sc(src);
	tsc = status_get_sc(bl);
	sstatus = status_get_status_data(src);
	tstatus = status_get_status_data(bl);

	// Taekwon combos activate on traps, so we need to check them even for targets that don't have status
	if (sd && skill_id == 0 && !(attack_type&BF_SKILL) && sc) {
		// Chance to trigger Taekwon kicks
		if (sc->data[SC_READYSTORM] &&
			sc_start4(src, SC_COMBO, 15, TK_STORMKICK,
				0, 2, 0,
				(2000 - 4 * sstatus->agi - 2 * sstatus->dex)))
			; //Stance triggered
		else if (sc->data[SC_READYDOWN] &&
			sc_start4(src, SC_COMBO, 15, TK_DOWNKICK,
				0, 2, 0,
				(2000 - 4 * sstatus->agi - 2 * sstatus->dex)))
			; //Stance triggered
		else if (sc->data[SC_READYTURN] &&
			sc_start4(src, SC_COMBO, 15, TK_TURNKICK,
				0, 2, 0,
				(2000 - 4 * sstatus->agi - 2 * sstatus->dex)))
			; //Stance triggered
		else if (sc->data[SC_READYCOUNTER]) { //additional chance from SG_FRIEND [Komurka]
			rate = 20;
			if (sc->data[SC_SKILLRATE_UP] && sc->data[SC_SKILLRATE_UP]->val1 == TK_COUNTER) {
				rate += rate * sc->data[SC_SKILLRATE_UP]->val2 / 100;
				status_change_end(src, SC_SKILLRATE_UP, INVALID_TIMER);
			}
			sc_start4(src, SC_COMBO, rate, TK_COUNTER,
				0, 2, 0,
				(2000 - 4 * sstatus->agi - 2 * sstatus->dex))
			; //Stance triggered
		}
	}

	if (!tsc) //skill additional effect is about adding effects to the target...
		//So if the target can't be inflicted with statuses, this is pointless.
		return 0;

	if( sd )
	{ // These statuses would be applied anyway even if the damage was blocked by some skills. [Inkfish]
		if( skill_id != WS_CARTTERMINATION && skill_id != AM_DEMONSTRATION && skill_id != CR_REFLECTSHIELD && skill_id != MS_REFLECTSHIELD && skill_id != ASC_BREAKER )
		{ // Trigger status effects
			enum sc_type type;
			int i;
			unsigned int time = 0;

			for( i = 0; i < ARRAYLENGTH(sd->addeff) && sd->addeff[i].flag; i++ )
			{
				rate = sd->addeff[i].rate;
				if( attack_type&BF_LONG ) // Any ranged physical attack takes status arrows into account (Grimtooth...) [DracoRPG]
					rate += sd->addeff[i].arrow_rate;
				if( !rate ) continue;

				if( (sd->addeff[i].flag&(ATF_WEAPON|ATF_MAGIC|ATF_MISC)) != (ATF_WEAPON|ATF_MAGIC|ATF_MISC) ) 
				{ // Trigger has attack type consideration. 
					if( (sd->addeff[i].flag&ATF_WEAPON && attack_type&BF_WEAPON) ||
						(sd->addeff[i].flag&ATF_MAGIC && attack_type&BF_MAGIC) ||
						(sd->addeff[i].flag&ATF_MISC && attack_type&BF_MISC) ) ;
					else
						continue; 
				}

				if( (sd->addeff[i].flag&(ATF_LONG|ATF_SHORT)) != (ATF_LONG|ATF_SHORT) )
				{ // Trigger has range consideration.
					if((sd->addeff[i].flag&ATF_LONG && !(attack_type&BF_LONG)) ||
						(sd->addeff[i].flag&ATF_SHORT && !(attack_type&BF_SHORT)))
						continue; //Range Failed.
				}

				type = sd->addeff[i].sc;
				time = sd->addeff[i].duration;

				if (sd->addeff[i].flag&ATF_TARGET)
					status_change_start(src,bl,type,rate,7,0,(type == SC_BURNING)?src->id:0,0, time,0);

				if (sd->addeff[i].flag&ATF_SELF)
					status_change_start(src,src,type,rate,7,0,(type == SC_BURNING)?src->id:0,0, time,0);
			}
		}

		if( skill_id )
		{ // Trigger status effects on skills
			enum sc_type type;
			uint8 i;
			unsigned int time = 0;
			for (i = 0; i < ARRAYLENGTH(sd->addeff_onskill) && sd->addeff_onskill[i].skill_id; i++) {
				if (skill_id != sd->addeff_onskill[i].skill_id || !sd->addeff_onskill[i].rate)
					continue;
				type = sd->addeff_onskill[i].sc;
				time = sd->addeff[i].duration;

				if (sd->addeff_onskill[i].target&ATF_TARGET)
					status_change_start(src, bl, type, sd->addeff_onskill[i].rate, 7, 0, 0, 0, time, 0);
				if (sd->addeff_onskill[i].target&ATF_SELF)
					status_change_start(src, src, type, sd->addeff_onskill[i].rate, 7, 0, 0, 0, time, 0);
			}

			//"While the damage can be blocked by Pneuma, the chance to break armor remains", irowiki. [Cydh]
			if (dmg_lv == ATK_BLOCK && skill_id == AM_ACIDTERROR) {
				sc_start2(bl, SC_BLEEDING, (skill_lv * 3), skill_lv, src->id, skill_get_time2(skill_id, skill_lv));
				if (skill_break_equip(bl, EQP_ARMOR, 100 * skill_get_time(skill_id, skill_lv), BCT_ENEMY))
					clif_emotion(bl, E_OMG);
			}
		}
	}

	if( dmg_lv < ATK_DEF ) // no damage, return;
		return 0;

	switch(skill_id)
	{
	case 0: // Normal attacks (no skill used)
	{
		if( attack_type&BF_SKILL )
			break; // If a normal attack is a skill, it's splash damage. [Inkfish]
		if(sd) {
			// Automatic trigger of Blitz Beat
			if( pc_isfalcon(sd) && sd->status.weapon == W_BOW && (skill=pc_checkskill(sd,HT_BLITZBEAT)) > 0 && rnd()%1000 <= sstatus->luk*10/3+1 )
			{
				rate=(sd->status.job_level+9)/10;
				skill_castend_damage_id(src,bl,HT_BLITZBEAT,(skill<rate)?skill:rate,tick,SD_LEVEL);
			}
			// Automatic trigger of Warg Strike [Jobbie]
			if (pc_iswug(sd) && sd->status.weapon == W_BOW && (skill = pc_checkskill(sd, RA_WUGSTRIKE)) > 0 && rnd() % 1000 <= sstatus->luk * 10 / 3 + 1)
				skill_castend_damage_id(src,bl,RA_WUGSTRIKE,skill,tick,SD_LEVEL);
			// Gank
			if(dstmd && sd->status.weapon != W_BOW &&
				(skill=pc_checkskill(sd,RG_SNATCHER)) > 0 &&
				(skill*15 + 55) + pc_checkskill(sd,TF_STEAL)*10 > rnd()%1000) {
				if(pc_steal_item(sd,bl,pc_checkskill(sd,TF_STEAL)))
					clif_skill_nodamage(src,bl,TF_STEAL,skill,1);
				else
					clif_skill_fail(sd,RG_SNATCHER,USESKILL_FAIL_LEVEL,0,0);
			}
		}

		if (sc) {
			struct status_change_entry *sce;
			// Enchant Poison gives a chance to poison attacked enemies
			if((sce=sc->data[SC_ENCPOISON])) //Don't use sc_start since chance comes in 1/10000 rate.
				status_change_start(src,bl,SC_POISON,sce->val2, sce->val1,src->id,0,0,
					skill_get_time2(AS_ENCHANTPOISON,sce->val1),0);
			// Enchant Deadly Poison gives a chance to deadly poison attacked enemies
			if((sce=sc->data[SC_EDP]))
				sc_start4(bl,SC_DPOISON,sce->val2, sce->val1,src->id,0,0,
					skill_get_time2(ASC_EDP,sce->val1));
		}
	}
	break;

	case SM_BASH:
		if( sd && skill_lv > 5 && pc_checkskill(sd,SM_FATALBLOW)>0 ){
			//BaseChance gets multiplied with BaseLevel/50.0; 500/50 simplifies to 10 [Playtester]
			status_change_start(src, bl, SC_STUN, (skill_lv - 5)*sd->status.base_level * 10,
				skill_lv, 0, 0, 0, skill_get_time2(SM_FATALBLOW, skill_lv), 0);
		}
		break;

	case MER_CRASH:
		sc_start(bl,SC_STUN,(6*skill_lv),skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case AS_VENOMKNIFE:
		if (sd) //Poison chance must be that of Envenom. [Skotlex]
			skill_lv = pc_checkskill(sd, TF_POISON);
	case TF_POISON:
	case AS_SPLASHER:
		if(!sc_start2(bl,SC_POISON,(4*skill_lv+10),skill_lv,src->id,skill_get_time2(skill_id,skill_lv))
			&& sd && skill_id==TF_POISON
		)
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
		break;

	case AS_SONICBLOW:
		sc_start(bl,SC_STUN,(2*skill_lv+10),skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case WZ_FIREPILLAR:
		unit_set_walkdelay(bl, tick, skill_get_time2(skill_id, skill_lv), 1);
		break;

	case MG_FROSTDIVER:
		if (!sc_start(bl, SC_FREEZE, min(skill_lv * 3 + 35, skill_lv + 60), skill_lv, skill_get_time2(skill_id, skill_lv)) && sd)
			clif_skill_fail(sd, skill_id, 0, 0, 0);
		break;

	case WZ_FROSTNOVA:
		sc_start(bl,SC_FREEZE,skill_lv*5+33,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case WZ_STORMGUST:
		//On third hit, there is a 150% to freeze the target
		if(tsc->sg_counter >= 3 &&
			sc_start(bl,SC_FREEZE,150,skill_lv,skill_get_time2(skill_id,skill_lv)))
			tsc->sg_counter = 0;
		//being it only resets on success it'd keep stacking and eventually overflowing on mvps, so we reset at a high value
		else if (tsc->sg_counter > 250)
			tsc->sg_counter = 0;
		break;

	case WZ_METEOR:
		sc_start(bl,SC_STUN,3*skill_lv,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case WZ_VERMILION:
		sc_start(bl,SC_BLIND,min(4 * skill_lv, 40),skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case HT_FREEZINGTRAP:
	case MA_FREEZINGTRAP:
		status_change_start (src, bl, SC_FREEZE, 10000, skill_lv, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
		break;

	case HT_FLASHER:
		status_change_start(src, bl, SC_BLIND, 10000, skill_lv, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
		break;

	case HT_LANDMINE:
	case MA_LANDMINE:
		status_change_start(src, bl, SC_STUN, 10000, skill_lv, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
		break;

	case HT_SHOCKWAVE:
		status_percent_damage(src, bl, 0, -(15*skill_lv+5), false);
		break;

	case HT_SANDMAN:
	case MA_SANDMAN:
		sc_start(bl,SC_SLEEP,(10*skill_lv+40),skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case TF_SPRINKLESAND:
		sc_start(bl,SC_BLIND,20,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case TF_THROWSTONE:
		if (!sc_start(bl,SC_STUN,3,skill_lv,skill_get_time(skill_id,skill_lv))) //only blind if success
			sc_start(bl,SC_BLIND,3,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case NPC_DARKCROSS:
	case CR_HOLYCROSS:
		sc_start(bl,SC_BLIND,3*skill_lv,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case NPC_GRANDDARKNESS:
		status_change_start(src, bl, SC_BLIND, 10000, skill_lv, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
		attack_type |= BF_WEAPON;
		break;

	case CR_GRANDCROSS:
		//Chance to cause blind status vs demon and undead element, but not against players
		if(!dstsd && (battle_check_undead(tstatus->race,tstatus->def_ele) || tstatus->race == RC_DEMON))
			sc_start(bl,SC_BLIND,100,skill_lv,skill_get_time2(skill_id,skill_lv));
		attack_type |= BF_WEAPON;
		break;

	case AM_ACIDTERROR:
		sc_start2(bl,SC_BLEEDING,(skill_lv*3),skill_lv,src->id,skill_get_time2(skill_id,skill_lv));
		if (skill_break_equip(bl, EQP_ARMOR, 100*skill_get_time(skill_id,skill_lv), BCT_ENEMY))
			clif_emotion(bl,E_OMG);
		break;

	case AM_DEMONSTRATION:
		skill_break_equip(bl, EQP_WEAPON, 100*skill_lv, BCT_ENEMY);
		break;

	case CR_SHIELDCHARGE:
		sc_start(bl,SC_STUN,(15+skill_lv*5),skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case PA_PRESSURE:
		status_percent_damage(src, bl, 0, 15+5*skill_lv, false);
		break;

	case HW_GRAVITATION:
		//Pressure and Gravitation can trigger physical autospells
		attack_type |= BF_NORMAL;
		attack_type |= BF_WEAPON;
		break;

	case RG_RAID:
		sc_start(bl,SC_STUN,(10+3*skill_lv),skill_lv,skill_get_time(skill_id,skill_lv));
		sc_start(bl,SC_BLIND,(10+3*skill_lv),skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case BA_FROSTJOKER:
		sc_start(bl,SC_FREEZE,(15+5*skill_lv),skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case DC_SCREAM:
		sc_start(bl,SC_STUN,(25+5*skill_lv),skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case BD_LULLABY:
		sc_start(bl, SC_SLEEP, (sstatus->int_ * 2 + rnd_value(100, 300)) * 10, skill_lv, skill_get_time2(skill_id, skill_lv));
		break;

	case DC_UGLYDANCE:
		rate = 5+5*skill_lv;
		if(sd && (skill=pc_checkskill(sd,DC_DANCINGLESSON)))
		    rate += 5+skill;
		status_zap(bl, 0, rate);
  		break;
	case SL_STUN:
		if (tstatus->size==1) //Only stuns mid-sized mobs.
			sc_start(bl,SC_STUN,(30+10*skill_lv),skill_lv,skill_get_time(skill_id,skill_lv));
		break;

	case NPC_PETRIFYATTACK:
		sc_start4(bl,status_skill2sc(skill_id),(20*skill_lv),
			skill_lv,0,0,skill_get_time(skill_id,skill_lv),
			skill_get_time2(skill_id,skill_lv));
		break;
	case NPC_CURSEATTACK:
	case NPC_SLEEPATTACK:
	case NPC_BLINDATTACK:
	case NPC_POISON:
	case NPC_SILENCEATTACK:
	case NPC_STUNATTACK:
	case NPC_BLEEDING:
	case NPC_HELLPOWER:
		sc_start(bl,status_skill2sc(skill_id),(20*skill_lv),skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case NPC_ACIDBREATH:
	case NPC_ICEBREATH:
		sc_start(bl,status_skill2sc(skill_id),70,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case NPC_MENTALBREAKER:
	{	//Based on observations by Tharis, Mental Breaker should do SP damage
	  	//equal to Matk*skLevel.
		rate = sstatus->matk_min;
		if (rate < sstatus->matk_max)
			rate += rnd()%(sstatus->matk_max - sstatus->matk_min);
		rate*=skill_lv;
		status_zap(bl, 0, rate);
		break;
	}
	// Equipment breaking monster skills [Celest]
	case NPC_WEAPONBRAKER:
		skill_break_equip(bl, EQP_WEAPON, 150*skill_lv, BCT_ENEMY);
		break;
	case NPC_ARMORBRAKE:
		skill_break_equip(bl, EQP_ARMOR, 150*skill_lv, BCT_ENEMY);
		break;
	case NPC_HELMBRAKE:
		skill_break_equip(bl, EQP_HELM, 150*skill_lv, BCT_ENEMY);
		break;
	case NPC_SHIELDBRAKE:
		skill_break_equip(bl, EQP_SHIELD, 150*skill_lv, BCT_ENEMY);
		break;

	case CH_TIGERFIST:
		sc_start(bl,SC_STOP,(10+skill_lv*10),0,skill_get_time2(skill_id,skill_lv));
		break;

	case LK_SPIRALPIERCE:
	case ML_SPIRALPIERCE:
		if (dstsd || (dstmd && status_bl_has_mode(bl, MD_STATUS_IMMUNE))) //Does not work on status immune
			sc_start(bl,SC_STOP,(15+skill_lv*5),0,skill_get_time2(skill_id,skill_lv));
		break;

	case ST_REJECTSWORD:
		sc_start(bl,SC_AUTOCOUNTER,(skill_lv*15),skill_lv,skill_get_time(skill_id,skill_lv));
		break;

	case PF_FOGWALL:
		if (src != bl && !tsc->data[SC_DELUGE])
			sc_start(bl, SC_BLIND, 100, skill_lv, skill_get_time2(skill_id, skill_lv));
		break;

	case LK_HEADCRUSH: //Headcrush has chance of causing Bleeding status, except on demon and undead element
		if (!(battle_check_undead(tstatus->race, tstatus->def_ele) || tstatus->race == RC_DEMON))
			sc_start2(bl, SC_BLEEDING,50, skill_lv, src->id, skill_get_time2(skill_id,skill_lv));
		break;

	case LK_JOINTBEAT:
		status = status_skill2sc(skill_id);
		if (tsc->jb_flag) {
			sc_start2(bl,status,(5*skill_lv+5),skill_lv,tsc->jb_flag&BREAK_FLAGS,skill_get_time2(skill_id,skill_lv));
			tsc->jb_flag = 0;
		}
		break;
	case ASC_METEORASSAULT:
		//Any enemies hit by this skill will receive Stun, Darkness, or external bleeding status ailment with a 5%+5*SkillLV% chance.
		switch(rnd()%3) {
			case 0:
				sc_start(bl,SC_BLIND,(5+skill_lv*5),skill_lv,skill_get_time2(skill_id,1));
				break;
			case 1:
				sc_start(bl,SC_STUN,(5+skill_lv*5),skill_lv,skill_get_time2(skill_id,2));
				break;
			default:
				sc_start2(bl,SC_BLEEDING,(5+skill_lv*5),skill_lv,src->id,skill_get_time2(skill_id,3));
  		}
		break;

	case HW_NAPALMVULCAN:
		sc_start(bl,SC_CURSE,5*skill_lv,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case WS_CARTTERMINATION:	// Cart termination
		sc_start(bl,SC_STUN,5*skill_lv,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case CR_ACIDDEMONSTRATION:
		skill_break_equip(bl, EQP_WEAPON|EQP_ARMOR, 100*skill_lv, BCT_ENEMY);
		break;

	case TK_DOWNKICK:
		sc_start(bl,SC_STUN,100,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case TK_JUMPKICK:
		if (dstsd && (dstsd->class_&MAPID_UPPERMASK) != MAPID_SOUL_LINKER && !tsc->data[SC_PRESERVE])
		{// debuff the following statuses
			status_change_end(bl, SC_SPIRIT, INVALID_TIMER);
			status_change_end(bl, SC_ADRENALINE2, INVALID_TIMER);
			status_change_end(bl, SC_KAITE, INVALID_TIMER);
			status_change_end(bl, SC_KAAHI, INVALID_TIMER);
			status_change_end(bl, SC_ONEHAND, INVALID_TIMER);
			status_change_end(bl, SC_ASPDPOTION2, INVALID_TIMER);
			// New soul links confirmed to not dispell with this skill
			// but thats likely a bug since soul links can't stack and
			// soul cutter skill works on them. Leave here in case that changes. [Rytech]
			//status_change_end(bl, SC_SOULGOLEM, INVALID_TIMER);
			//status_change_end(bl, SC_SOULSHADOW, INVALID_TIMER);
			//status_change_end(bl, SC_SOULFALCON, INVALID_TIMER);
			//status_change_end(bl, SC_SOULFAIRY, INVALID_TIMER);
		}
		break;
	case TK_TURNKICK:
	case MO_BALKYOUNG: //Note: attack_type is passed as BF_WEAPON for the actual target, BF_MISC for the splash-affected mobs.
		if(attack_type&BF_MISC) //70% base stun chance...
			sc_start(bl,SC_STUN,70,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case GS_BULLSEYE: //0.1% coma rate.
		if(tstatus->race == RC_BRUTE || tstatus->race == RC_DEMIHUMAN || tstatus->race == RC_PLAYER)
			status_change_start(src,bl,SC_COMA,10,skill_lv,0,src->id,0,0,0);
		break;
	case GS_PIERCINGSHOT:
		sc_start2(bl,SC_BLEEDING,(skill_lv*3),skill_lv,src->id,skill_get_time2(skill_id,skill_lv));
		break;
	case NJ_HYOUSYOURAKU:
		sc_start(bl,SC_FREEZE,(10+10*skill_lv),skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case GS_FLING:
		sc_start(bl,SC_FLING,100, sd?sd->spiritball_old:5,skill_get_time(skill_id,skill_lv));
		break;
	case GS_DISARM:
		rate = sstatus->dex / (4 * (7 - skill_lv)) + sstatus->luk / (4 * (6 - skill_lv));
		rate = rate + status_get_lv(src) - (tstatus->agi * rate / 100) - tstatus->luk - status_get_lv(bl);
		skill_strip_equip(bl, EQP_WEAPON, rate, skill_lv, skill_get_time(skill_id,skill_lv));
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;
	case NPC_EVILLAND:
		sc_start(bl,SC_BLIND,5*skill_lv,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case NPC_HELLJUDGEMENT:
		sc_start(bl,SC_CURSE,100,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case NPC_CRITICALWOUND:
		sc_start(bl,SC_CRITICALWOUND,100,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case RK_HUNDREDSPEAR:
		if ( pc_checkskill(sd,KN_SPEARBOOMERANG) > 0 && rnd()%100 < 10 + 3 * skill_lv ) {
			skill_castend_damage_id(src,bl,KN_SPEARBOOMERANG,pc_checkskill(sd,KN_SPEARBOOMERANG),tick,0);
			skill_blown(src,bl,6,-1,0);//Target gets knocked back 6 cells if Speer Boomerang is autocasted.
		}
		break;
	case RK_WINDCUTTER:
		sc_start(bl,SC_FEAR,3+2*skill_lv,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case RK_DRAGONBREATH:
		sc_start(bl, SC_BURNING, 15, skill_lv, skill_get_time(skill_id, skill_lv));
		break;
	case GC_WEAPONCRUSH:// Rate is handled later.
		skill_castend_nodamage_id(src,bl,skill_id,skill_lv,tick,BCT_ENEMY);
		break;
	case AB_ADORAMUS:
		if (rnd() % 100 < 4 * skill_lv + status_get_job_lv_effect(src) / 2)
		{
			sc_start(bl, SC_ADORAMUS, 100, skill_lv, skill_get_time(skill_id, skill_lv));
			sc_start(bl, SC_BLIND, 100, skill_lv, skill_get_time2(skill_id, skill_lv));
		}
		break;
	case WL_CRIMSONROCK:
		sc_start(bl, SC_STUN, 40, skill_lv, skill_get_time(skill_id, skill_lv));
 		break;
	case WL_COMET:
		sc_start(bl, SC_BURNING, 100, skill_lv, skill_get_time2(skill_id, skill_lv));
		break;
	case WL_EARTHSTRAIN:
		{
			int rate = 0, i;
			const int pos[5] = {EQP_WEAPON, EQP_HELM, EQP_SHIELD, EQP_ARMOR, EQP_ACC};
			rate = (5 + skill_lv) * skill_lv + sstatus->dex / 10 - tstatus->dex / 5;// The tstatus->dex / 5 part is unofficial, but players gotta have some kind of way to have resistance. [Rytech]
			//rate -= rate * tstatus->dex / 200; // Disabled until official resistance is found.

			for( i = 0; i < skill_lv; i++ )
				skill_strip_equip(bl, pos[i], rate, skill_lv, skill_get_time2(skill_id, skill_lv));
		}
		break;
	case WL_FROSTMISTY:
		sc_start(bl, SC_FROST, 25 + 5 * skill_lv, skill_lv, skill_get_time(skill_id, skill_lv));
		break;
	case WL_JACKFROST:
		status_change_start(src, bl, SC_FREEZE, 20000, skill_lv, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
		break;
	case RA_WUGBITE:
		rate = 50 + 10 * skill_lv + 2 * (sd ? pc_checkskill(sd, RA_TOOTHOFWUG) : 10) - tstatus->agi / 4;
		if ( rate < 50 )
			rate = 50;
		sc_start(bl, SC_WUGBITE, rate, skill_lv, skill_get_time(skill_id, skill_lv) + (sd ? pc_checkskill(sd, RA_TOOTHOFWUG) * 500 : 5000));
		break;
	case RA_SENSITIVEKEEN:
		if (rnd()%100 < 8 * skill_lv)
			skill_castend_damage_id(src, bl, RA_WUGBITE, (sd ? pc_checkskill(sd, RA_WUGBITE) : 5), tick, SD_ANIMATION); skill_castend_damage_id(src, bl, RA_WUGBITE, sd ? pc_checkskill(sd, RA_WUGBITE) : skill_lv, tick, SD_ANIMATION);
		break;
	case RA_MAGENTATRAP:
	case RA_COBALTTRAP:
	case RA_MAIZETRAP:
	case RA_VERDURETRAP:
		if( dstmd && !(dstmd->status.mode&MD_STATUS_IMMUNE) )
			sc_start2(bl,SC_ELEMENTALCHANGE,100,skill_lv,skill_get_ele(skill_id,skill_lv),skill_get_time2(skill_id,skill_lv));
		break;
	case RA_FIRINGTRAP:
	case RA_ICEBOUNDTRAP:
		sc_start(bl, (skill_id == RA_FIRINGTRAP) ? SC_BURNING : SC_FROST, 50 + 10 * skill_lv, skill_lv, skill_get_time2(skill_id, skill_lv));
		break;
	case NC_PILEBUNKER:
		if (rnd() % 100 < 25 + 15 * skill_lv)
		{ //Status's Deactivated By Pile Bunker
			status_change_end(bl, SC_KYRIE, INVALID_TIMER);
			status_change_end(bl, SC_AUTOGUARD, INVALID_TIMER);
			status_change_end(bl, SC_REFLECTSHIELD, INVALID_TIMER);
			status_change_end(bl, SC_DEFENDER, INVALID_TIMER);
			status_change_end(bl, SC_STEELBODY, INVALID_TIMER);
			status_change_end(bl, SC_ASSUMPTIO, INVALID_TIMER);
			status_change_end(bl, SC_LG_REFLECTDAMAGE, INVALID_TIMER);
			status_change_end(bl, SC_PRESTIGE, INVALID_TIMER);
			status_change_end(bl, SC_BANDING, INVALID_TIMER);
			status_change_end(bl, SC_GENTLETOUCH_CHANGE, INVALID_TIMER);
			status_change_end(bl, SC_GENTLETOUCH_REVITALIZE, INVALID_TIMER);
			if (dstsd)
				pc_delshieldball(dstsd, dstsd->shieldball, 0);
		}
		break;
	case NC_FLAMELAUNCHER:
		sc_start4(bl, SC_BURNING, 20 + 10 * skill_lv, skill_lv, 1000, src->id, 0, skill_get_time2(skill_id, skill_lv));
		break;
	case NC_COLDSLOWER:
		//Status chances are applied officially through a check.
		//The skill first trys to give the frozen status to targets that are hit.
		sc_start(bl, SC_FREEZE, 10 * skill_lv, skill_lv, skill_get_time(skill_id, skill_lv));
		//If it fails to give the frozen status, it will attempt to give the freezing status.
		if ( tsc && !tsc->data[SC_FREEZE] )
			sc_start(bl, SC_FROST, 20 + 10 * skill_lv, skill_lv, skill_get_time2(skill_id, skill_lv));
		break;
	case NC_POWERSWING:
		sc_start(bl, SC_STUN, 10, skill_lv, skill_get_time(skill_id, skill_lv));
		if ((sd ? pc_checkskill(sd, NC_AXEBOOMERANG) : 5) > 0 && rnd() % 100 < 5 * skill_lv)
			skill_castend_damage_id(src, bl, NC_AXEBOOMERANG, (sd ? pc_checkskill(sd, NC_AXEBOOMERANG) : 5), tick, 1);
		break;
	case LG_SHIELDPRESS:
		sc_start(bl, SC_STUN, 30 + 8 * skill_lv + sstatus->dex / 10 + status_get_job_lv_effect(src) / 4, skill_lv, skill_get_time(skill_id, skill_lv));
 		break;
	case LG_PINPOINTATTACK:
		rate = 5 * (sd ? pc_checkskill(sd, LG_PINPOINTATTACK) : 5) + (sstatus->agi + status_get_base_lv_effect(src)) / 10;

		switch( skill_lv )
		{
			case 1://Gives Bleeding Status
				sc_start2(bl,SC_BLEEDING,rate,skill_lv,src->id,skill_get_time(skill_id,skill_lv));
				break;
			case 2://Breaks Top Headgear
				skill_break_equip(bl, EQP_HELM, 100 * rate, BCT_ENEMY);
				break;
			case 3://Breaks Shield
				skill_break_equip(bl, EQP_SHIELD, 100*rate, BCT_ENEMY);
				break;
			case 4://Breaks Armor
				skill_break_equip(bl, EQP_ARMOR, 100*rate, BCT_ENEMY);
				break;
			case 5://Breaks Weapon
				skill_break_equip(bl, EQP_WEAPON, 100*rate, BCT_ENEMY);
				break;
		}
 		break;
	case LG_MOONSLASHER:
		rate = 32 + 8 * skill_lv;
		if( rnd()%100 < rate && dstsd ) // Uses skill_addtimerskill to avoid damage and setsit packet overlaping. Officially clif_setsit is received about 500 ms after damage packet.
			skill_addtimerskill(src,tick+500,bl->id,0,0,skill_id,skill_lv,BF_WEAPON,0);
		else if( dstmd )
			sc_start(bl,SC_STOP,100,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case LG_RAYOFGENESIS:	// 50% chance to cause Blind on Undead and Demon monsters.
		if (battle_check_undead(tstatus->race, tstatus->def_ele) || tstatus->race == RC_DEMON)
			sc_start(bl, SC_BLIND,50, skill_lv, skill_get_time(skill_id, skill_lv));
		break;
	case LG_EARTHDRIVE:
		skill_break_equip(src, EQP_SHIELD, 100 * skill_lv, BCT_SELF);
		sc_start(bl, SC_EARTHDRIVE, 100, skill_lv, skill_get_time(skill_id, skill_lv));
		break;
	case LG_HESPERUSLIT:
		if (sc && sc->data[SC_BANDING] && (battle_config.hesperuslit_bonus_stack == 1 && sc->data[SC_BANDING]->val2 >= 4 || sc->data[SC_BANDING]->val2 == 4))
			status_change_start(src, bl, SC_STUN, 10000, skill_lv, 0, 0, 0, rnd_value( 4000, 8000), 2);
		if ((sd ? pc_checkskill(sd, LG_PINPOINTATTACK) : 5) > 0 && sc && sc->data[SC_BANDING] && (battle_config.hesperuslit_bonus_stack == 1 && sc->data[SC_BANDING]->val2 >= 6 || sc->data[SC_BANDING]->val2 == 6))
			skill_castend_damage_id(src, bl, LG_PINPOINTATTACK, rnd_value(1, (sd ? pc_checkskill(sd, LG_PINPOINTATTACK) : 5)), tick, 0);
		break;
	case SR_DRAGONCOMBO:
		sc_start(bl, SC_STUN, 1 + skill_lv, skill_lv, skill_get_time(skill_id, skill_lv));
		break;
	case SR_FALLENEMPIRE:// Switch this to SC_FALLENEMPIRE soon.
		sc_start(bl, SC_STOP, 100, skill_lv, skill_get_time(skill_id, skill_lv));
		break;
	case SR_WINDMILL:
		if (dstsd)
			skill_addtimerskill(src, tick + 500, bl->id, 0, 0, skill_id, skill_lv, BF_WEAPON, 0);
		else if (dstmd)
			sc_start(bl, SC_STUN, 100, skill_lv, 1000 * rnd() % 4);
		break;
	case SR_GENTLETOUCH_QUIET:
		sc_start(bl, SC_SILENCE, 5 * skill_lv + (sstatus->dex + status_get_base_lv_effect(src)) / 10, skill_lv, skill_get_time(skill_id, skill_lv));
		break;
	case SR_HOWLINGOFLION:
		status_change_end(bl, SC_SWING, INVALID_TIMER);
		status_change_end(bl, SC_SYMPHONY_LOVE, INVALID_TIMER);
		status_change_end(bl, SC_MOONLIT_SERENADE, INVALID_TIMER);
		status_change_end(bl, SC_RUSH_WINDMILL, INVALID_TIMER);
		status_change_end(bl, SC_ECHOSONG, INVALID_TIMER);
		status_change_end(bl, SC_HARMONIZE, INVALID_TIMER);
		status_change_end(bl, SC_NETHERWORLD, INVALID_TIMER);
		status_change_end(bl, SC_SIREN, INVALID_TIMER);
		status_change_end(bl, SC_DEEPSLEEP, INVALID_TIMER);
		status_change_end(bl, SC_SIRCLEOFNATURE, INVALID_TIMER);
		status_change_end(bl, SC_GLOOMYDAY, INVALID_TIMER);
		status_change_end(bl, SC_SONG_OF_MANA, INVALID_TIMER);
		status_change_end(bl, SC_DANCE_WITH_WUG, INVALID_TIMER);
		status_change_end(bl, SC_SATURDAY_NIGHT_FEVER, INVALID_TIMER);
		status_change_end(bl, SC_LERADS_DEW, INVALID_TIMER);
		status_change_end(bl, SC_MELODYOFSINK, INVALID_TIMER);
		status_change_end(bl, SC_BEYOND_OF_WARCRY, INVALID_TIMER);
		status_change_end(bl, SC_UNLIMITED_HUMMING_VOICE, INVALID_TIMER);
		sc_start(bl, SC_FEAR, 5 + 5 * skill_lv, skill_lv, skill_get_time(skill_id, skill_lv));// Aegis says this applies even if not hit. Keep this here since it might be a bug.
		break;
	case SO_EARTHGRAVE:
		sc_start2(bl, SC_BLEEDING, 5 * skill_lv, skill_lv, src->id, skill_get_time2(skill_id, skill_lv));
		break;
	case SO_DIAMONDDUST:
		rate = 5 + 5 * skill_lv;
		if (sc && sc->data[SC_COOLER_OPTION])
			rate += sc->data[SC_COOLER_OPTION]->val3;
		sc_start(bl, SC_CRYSTALIZE, rate, skill_lv, skill_get_time2(skill_id, skill_lv));
		break;
	case SO_CLOUD_KILL:
		sc_start(bl, SC_POISON, 10 + 10 * skill_lv, skill_lv, skill_get_time2(skill_id, skill_lv)); // Need official rate. [LimitLine]
		if(tstatus->mode&~MD_STATUS_IMMUNE ) // Boss monsters should be immune to elemental change trough Cloud Kill. Confirm this. [LimitLine]
			sc_start2(bl, SC_ELEMENTALCHANGE, 10 + 10 * skill_lv, skill_lv, 5, skill_get_time2(skill_id, skill_lv)); // Need official rate. [LimitLine]
		break;
	case SO_VARETYR_SPEAR:
		sc_start(bl, SC_STUN, 5 + 5 * skill_lv, skill_lv, skill_get_time(skill_id, skill_lv));
		break;
	case WM_POEMOFNETHERWORLD:
		{
			int duration = skill_get_time2(skill_id, skill_lv) - (1000 * status_get_base_lv_effect(bl) / 50 + 1000 * status_get_job_lv_effect(bl) / 10);
			if ( duration < 4000 )
				duration = 4000;// Duration can't be reduced below 4 seconds.
			sc_start(bl, SC_NETHERWORLD, 100, skill_lv, duration);
		}
		break;
	case GN_HELLS_PLANT_ATK:
		sc_start(bl, SC_STUN, 20 + 10 * skill_lv, skill_lv, skill_get_time(skill_id, skill_lv));
		sc_start2(bl, SC_BLEEDING, 5 + 5 * skill_lv, skill_lv, src->id, skill_get_time(skill_id, skill_lv));
		break;
	case GN_SLINGITEM_RANGEMELEEATK:
		if( sd ) {
			short baselv = status_get_base_lv_effect(src);
			short joblv = status_get_job_lv_effect(src);
			short tbaselv = status_get_base_lv_effect(bl);
			
			switch( sd->itemid )
			{
				case ITEMID_COCONUT_BOMB://Causes stun and bleeding.
					sc_start(bl, SC_STUN, 5 + joblv / 2, skill_lv, 1000 * joblv / 3);
					sc_start2(bl, SC_BLEEDING, 3 + joblv / 2, skill_lv, src->id, 1000 * (baselv / 4 + joblv / 3));
					break;
				case ITEMID_MELON_BOMB://Reduces movement and attack speed.
					sc_start4(bl, SC_MELON_BOMB, 100, skill_lv, 20 + joblv, 10 + joblv / 2, 0, 1000 * baselv / 4);
					break;
				case ITEMID_BANANA_BOMB:
					{
						int duration;
						if ( battle_config.banana_bomb_sit_duration == 1 )
							duration = 1000 * joblv / 4;// Official force sit duration.
						else
							duration = 200;// Config to allow player to stand up after forced sit, but small duration needed for sitting to happen.
						// Reduces LUK and chance to force sit. Must do the force sit success chance first before LUK reduction.
						sc_start(bl, SC_BANANA_BOMB_SITDOWN_POSTDELAY, baselv + joblv + sstatus->dex / 6 - tbaselv - tstatus->agi / 4 - tstatus->luk / 5, skill_lv, duration);
						//sc_start(bl, SC_BANANA_BOMB, 100, skill_lv, 77000);//Info says reduces LUK by 77, but doesn't work on official. Bug maybe or disabled for above reason.
					}
					break;
			}
		}
		break;
	case RL_MASS_SPIRAL:
		sc_start2(bl,SC_BLEEDING,30 + 10 * skill_lv,skill_lv,src->id,skill_get_time(skill_id,skill_lv));
		break;
	case RL_BANISHING_BUSTER:
		{
			short i;
			if((dstsd && (dstsd->class_&MAPID_UPPERMASK) == MAPID_SOUL_LINKER)
				|| (tsc && tsc->data[SC_SPIRIT] && tsc->data[SC_SPIRIT]->val2 == SL_ROGUE) //Rogue's spirit defends againt dispel.
				|| rnd()%100 >= 50+10*skill_lv)
			{
				if (sd)
					clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}
			if(status_isimmune(bl) || !tsc || !tsc->count)
				break;
			for(i=0;i<SC_MAX;i++)
			{
				if (!tsc->data[i])
					continue;
				switch (i) {
				case SC_WEIGHT50:		case SC_WEIGHT90:		case SC_HALLUCINATION:
				case SC_STRIPWEAPON:	case SC_STRIPSHIELD:	case SC_STRIPARMOR:
				case SC_STRIPHELM:		case SC_CP_WEAPON:		case SC_CP_SHIELD:
				case SC_CP_ARMOR:		case SC_CP_HELM:		case SC_COMBO:
				case SC_STRFOOD:		case SC_AGIFOOD:		case SC_VITFOOD:
				case SC_INTFOOD:		case SC_DEXFOOD:		case SC_LUKFOOD:
				case SC_HITFOOD:		case SC_FLEEFOOD:		case SC_BATKFOOD:
				case SC_WATKFOOD:		case SC_MATKFOOD:		case SC_DANCING:
				case SC_EDP:			case SC_AUTOBERSERK:
				case SC_CARTBOOST:		case SC_MELTDOWN:		case SC_SAFETYWALL:
				case SC_SMA:			case SC_SPEEDUP0:		case SC_NOCHAT:
				case SC_ANKLE:			case SC_SPIDERWEB:		case SC_JAILED:
				case SC_ITEMBOOST:		case SC_EXPBOOST:		case SC_LIFEINSURANCE:
				case SC_BOSSMAPINFO:	case SC_PNEUMA:			case SC_AUTOSPELL:
				case SC_INCHITRATE:		case SC_INCATKRATE:		case SC_NEN:
				case SC_READYSTORM:		case SC_READYDOWN:		case SC_READYTURN:
				case SC_READYCOUNTER:	case SC_DODGE:			case SC_WARM:
				case SC_SPEEDUP1:		case SC_AUTOTRADE:		case SC_CRITICALWOUND:
				case SC_JEXPBOOST:		case SC_INVINCIBLE:		case SC_INVINCIBLEOFF:
				case SC_HELLPOWER:		case SC_MANU_ATK:		case SC_MANU_DEF:
				case SC_SPL_ATK:		case SC_SPL_DEF:		case SC_MANU_MATK:
				case SC_SPL_MATK:		case SC_RICHMANKIM:		case SC_ETERNALCHAOS:
				case SC_DRUMBATTLE:		case SC_NIBELUNGEN:		case SC_ROKISWEIL:
				case SC_INTOABYSS:		case SC_SIEGFRIED:		case SC_FOOD_STR_CASH:
				case SC_FOOD_AGI_CASH:	case SC_FOOD_VIT_CASH:	case SC_FOOD_DEX_CASH:
				case SC_FOOD_INT_CASH:	case SC_FOOD_LUK_CASH:	case SC_SEVENWIND:
				case SC_MIRACLE:		case SC_S_LIFEPOTION:	case SC_L_LIFEPOTION:
				case SC_INCHEALRATE:
					// 3CeAM
					// 3rd Job Status's
				case SC_DEATHBOUND:				case SC_EPICLESIS:				case SC_CLOAKINGEXCEED:
				case SC_HALLUCINATIONWALK:		case SC_HALLUCINATIONWALK_POSTDELAY:	case SC_WEAPONBLOCKING:
				case SC_WEAPONBLOCKING_POSTDELAY:	case SC_ROLLINGCUTTER:		case SC_POISONINGWEAPON:
				case SC_FEARBREEZE:				case SC_ELECTRICSHOCKER:		case SC_WUGRIDER:
				case SC_WUGDASH:				case SC_WUGBITE:				case SC_CAMOUFLAGE:
				case SC_ACCELERATION:			case SC_HOVERING:				case SC_OVERHEAT_LIMITPOINT:
				case SC_OVERHEAT:				case SC_SHAPESHIFT:				case SC_INFRAREDSCAN:
				case SC_MAGNETICFIELD:			case SC_NEUTRALBARRIER:			case SC_NEUTRALBARRIER_MASTER:
				case SC_STEALTHFIELD_MASTER:	case SC__REPRODUCE:				case SC_FORCEOFVANGUARD:
				case SC__SHADOWFORM:			case SC_EXEEDBREAK:				case SC__INVISIBILITY:
				case SC_BANDING:		case SC_INSPIRATION:	case SC_RAISINGDRAGON:
				case SC_LIGHTNINGWALK:	case SC_CURSEDCIRCLE_ATKER:	case SC_CURSEDCIRCLE_TARGET:
				case SC_CRESCENTELBOW:	case SC_STRIPACCESSARY:	case SC_MANHOLE:
				case SC_GENTLETOUCH_ENERGYGAIN:	case SC_GENTLETOUCH_CHANGE:		case SC_GENTLETOUCH_REVITALIZE:
				case SC_SWING:		case SC_SYMPHONY_LOVE:	case SC_RUSH_WINDMILL:
				case SC_ECHOSONG:		case SC_MOONLIT_SERENADE:	case SC_SITDOWN_FORCE:
				case SC_ANALYZE:		case SC_LERADS_DEW:		case SC_MELODYOFSINK:
				case SC_BEYOND_OF_WARCRY:	case SC_UNLIMITED_HUMMING_VOICE:	case SC_STEALTHFIELD:
				case SC_WARMER:			case SC_READING_SB:		case SC_GN_CARTBOOST:
				case SC_THORNS_TRAP:		case SC_SPORE_EXPLOSION:	case SC_DEMONIC_FIRE:
				case SC_FIRE_EXPANSION_SMOKE_POWDER:	case SC_FIRE_EXPANSION_TEAR_GAS:		case SC_VACUUM_EXTREME:
				case SC_BANDING_DEFENCE:	case SC_LG_REFLECTDAMAGE:	case SC_MILLENNIUMSHIELD:
					// Genetic Potions
				case SC_SAVAGE_STEAK:	case SC_COCKTAIL_WARG_BLOOD:	case SC_MINOR_BBQ:
				case SC_SIROMA_ICE_TEA:	case SC_DROCERA_HERB_STEAMED:	case SC_PUTTI_TAILS_NOODLES:
				case SC_MELON_BOMB:		case SC_BANANA_BOMB_SITDOWN_POSTDELAY:	case SC_BANANA_BOMB:
				case SC_PROMOTE_HEALTH_RESERCH:	case SC_ENERGY_DRINK_RESERCH:	case SC_EXTRACT_WHITE_POTION_Z:
				case SC_VITATA_500:		case SC_EXTRACT_SALAMINE_JUICE:	case SC_BOOST500:
				case SC_FULL_SWING_K:	case SC_MANA_PLUS:		case SC_MUSTLE_M:
				case SC_LIFE_FORCE_F:
					// Elementals
				case SC_EL_WAIT:		case SC_EL_PASSIVE:		case SC_EL_DEFENSIVE:
				case SC_EL_OFFENSIVE:	case SC_EL_COST:		case SC_CIRCLE_OF_FIRE:
				case SC_CIRCLE_OF_FIRE_OPTION:	case SC_FIRE_CLOAK:	case SC_FIRE_CLOAK_OPTION:
				case SC_WATER_SCREEN:	case SC_WATER_SCREEN_OPTION:	case SC_WATER_DROP:
				case SC_WATER_DROP_OPTION:	case SC_WIND_STEP:	case SC_WIND_STEP_OPTION:
				case SC_WIND_CURTAIN:	case SC_WIND_CURTAIN_OPTION:	case SC_SOLID_SKIN:
				case SC_SOLID_SKIN_OPTION:	case SC_STONE_SHIELD:	case SC_STONE_SHIELD_OPTION:
				case SC_PYROTECHNIC:	case SC_PYROTECHNIC_OPTION:	case SC_HEATER:
				case SC_HEATER_OPTION:	case SC_TROPIC:			case SC_TROPIC_OPTION:
				case SC_AQUAPLAY:		case SC_AQUAPLAY_OPTION:	case SC_COOLER:
				case SC_COOLER_OPTION:	case SC_CHILLY_AIR:		case SC_CHILLY_AIR_OPTION:
				case SC_GUST:			case SC_GUST_OPTION:	case SC_BLAST:
				case SC_BLAST_OPTION:	case SC_WILD_STORM:		case SC_WILD_STORM_OPTION:
				case SC_PETROLOGY:		case SC_PETROLOGY_OPTION:	case SC_CURSED_SOIL:
				case SC_CURSED_SOIL_OPTION:	case SC_UPHEAVAL:	case SC_UPHEAVAL_OPTION:
				case SC_TIDAL_WEAPON:	case SC_TIDAL_WEAPON_OPTION:	case SC_ROCK_CRUSHER:
				case SC_ROCK_CRUSHER_ATK:
					// Mutated Homunculus
				case SC_NEEDLE_OF_PARALYZE:	case SC_PAIN_KILLER:	case SC_LIGHT_OF_REGENE:
				case SC_OVERED_BOOST:	case SC_SILENT_BREEZE:	case SC_STYLE_CHANGE:
				case SC_SONIC_CLAW_POSTDELAY:	case SC_SILVERVEIN_RUSH_POSTDELAY:	case SC_MIDNIGHT_FRENZY_POSTDELAY:
				case SC_GOLDENE_FERSE:	case SC_ANGRIFFS_MODUS:	case SC_TINDER_BREAKER:
				case SC_TINDER_BREAKER_POSTDELAY:	case SC_CBC:	case SC_CBC_POSTDELAY:
				case SC_EQC:			case SC_MAGMA_FLOW:		case SC_GRANITIC_ARMOR:
				case SC_PYROCLASTIC:	case SC_VOLCANIC_ASH:
					// Kagerou/Oboro
				case SC_KG_KAGEHUMI:	case SC_KO_JYUMONJIKIRI:	case SC_MEIKYOUSISUI:
				case SC_KYOUGAKU:		case SC_IZAYOI:			case SC_ZENKAI:
					// Summoner
				case SC_SPRITEMABLE:	case SC_BITESCAR:		case SC_SOULATTACK:
					// Rebellion - Need Confirm
				case SC_B_TRAP:			case SC_C_MARKER:		case SC_H_MINE:
				case SC_ANTI_M_BLAST:	case SC_FALLEN_ANGEL:
				// Star Emperor
				case SC_NEWMOON:		case SC_FLASHKICK:		case SC_NOVAEXPLOSING:
				// Soul Reaper
				case SC_SOULUNITY:		case SC_SOULSHADOW:		case SC_SOULFAIRY:
				case SC_SOULFALCON:		case SC_SOULGOLEM:		case SC_USE_SKILL_SP_SPA:
				case SC_USE_SKILL_SP_SHA:	case SC_SP_SHA:
				// 3rd Job Level Expansion
				case SC_FRIGG_SONG:		case SC_OFFERTORIUM:	case SC_TELEKINESIS_INTENSE:
				case SC_KINGS_GRACE:
				// Misc Status's
				case SC_ALL_RIDING:		case SC_MONSTER_TRANSFORM:	case SC_ON_PUSH_CART:
				case SC_FULL_THROTTLE:	case SC_REBOUND:			case SC_ANCILLA:
				// Guild Skills
				case SC_LEADERSHIP:		case SC_GLORYWOUNDS:	case SC_SOULCOLD:
				case SC_HAWKEYES:
				// Only removeable by Clearance
				case SC_CRUSHSTRIKE:	case SC_REFRESH:			case SC_GIANTGROWTH:
				case SC_STONEHARDSKIN:	case SC_VITALITYACTIVATION:	case SC_FIGHTINGSPIRIT:
				case SC_ABUNDANCE:		case SC_ORATIO:				case SC_LAUDAAGNUS:
				case SC_LAUDARAMUS:		case SC_RENOVATIO:			case SC_EXPIATIO:
				case SC_TOXIN:			case SC_PARALYSE:			case SC_VENOMBLEED:
				case SC_MAGICMUSHROOM:	case SC_DEATHHURT:			case SC_PYREXIA:
				case SC_OBLIVIONCURSE:	case SC_LEECHESEND:			case SC_DUPLELIGHT:
				case SC_MARSHOFABYSS:	case SC_RECOGNIZEDSPELL:	case SC__BODYPAINT:
				case SC__DEADLYINFECT:	case SC_EARTHDRIVE:			case SC_VENOMIMPRESS:
				case SC_FROST:			case SC_BLOOD_SUCKER:	case SC_MANDRAGORA:
				case SC_STOMACHACHE:	case SC_MYSTERIOUS_POWDER:
				case SC_DAILYSENDMAILCNT:
				case SC_ATTHASTE_CASH:	case SC_REUSE_REFRESH:
				case SC_REUSE_LIMIT_A:	case SC_REUSE_LIMIT_B:	case SC_REUSE_LIMIT_C:
				case SC_REUSE_LIMIT_D:	case SC_REUSE_LIMIT_E:	case SC_REUSE_LIMIT_F:
				case SC_REUSE_LIMIT_G:	case SC_REUSE_LIMIT_H:	case SC_REUSE_LIMIT_MTF:
				case SC_REUSE_LIMIT_ASPD_POTION:	case SC_REUSE_MILLENNIUMSHIELD:	case SC_REUSE_CRUSHSTRIKE:
				case SC_REUSE_STORMBLAST:	case SC_ALL_RIDING_REUSE_LIMIT:
				case SC_DORAM_BUF_01:	case SC_DORAM_BUF_02:
				case SC_MTF_ASPD:		case SC_MTF_RANGEATK:		case SC_MTF_MATK:
				case SC_MTF_MLEATKED:		case SC_MTF_CRIDAMAGE:
				case SC_MTF_ASPD2:		case SC_MTF_RANGEATK2:	case SC_MTF_MATK2:
				case SC_2011RWC_SCROLL:		case SC_JP_EVENT04:	case SC_MTF_MHP:
				case SC_MTF_MSP:		case SC_MTF_PUMPKIN:	case SC_MTF_HITFLEE:
					continue;
				case SC_WHISTLE:	case SC_ASSNCROS:		case SC_POEMBRAGI:
				case SC_APPLEIDUN:	case SC_HUMMING:		case SC_DONTFORGETME:
				case SC_FORTUNE:	case SC_SERVICE4U:
					if (tsc->data[i]->val4 == 0)
						continue; //if in song-area don't end it
					break;
				case SC_ASSUMPTIO:
					if( bl->type == BL_MOB )
						continue;
					break;
				}
				if( i == SC_BERSERK ) tsc->data[i]->val2=0; //Mark a dispelled berserk to avoid setting hp to 100 by setting hp penalty to 0.
				status_change_end(bl, (sc_type)i, INVALID_TIMER);
			}
			break;
		}
		break;
	case RL_S_STORM:
		{
			short dex_effect = (sstatus->dex - 100) / 4;
			short agi_effect = (tstatus->agi - 100) / 4;
			short added_chance = 0;

			if ( dex_effect < 0 )
				dex_effect = 0;

			if ( agi_effect < 0 )
				agi_effect = 0;

			added_chance = dex_effect - (agi_effect + status_get_lv(bl) / 10);

			if ( added_chance < 0 )
				added_chance = 0;

			skill_break_equip(bl, EQP_HELM, 100*(5*skill_lv+added_chance), BCT_ENEMY);
		}
		break;
	case RL_AM_BLAST:
		sc_start(bl,SC_ANTI_M_BLAST,20 + 10 * skill_lv,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case RL_HAMMER_OF_GOD:
		sc_start(bl, SC_STUN, 100, skill_lv, skill_get_time2(skill_id, skill_lv));
		break;
	case SJ_FULLMOONKICK:
		sc_start(bl,SC_BLIND,15+5*skill_lv,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case SJ_STAREMPEROR:
		sc_start(bl,SC_SILENCE,50+10*skill_lv,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case SP_CURSEEXPLOSION:
		status_change_end(bl, SC_CURSE, INVALID_TIMER);
		break;
	case SP_SHA:
		sc_start(bl,SC_SP_SHA,100,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case KO_JYUMONJIKIRI:
		sc_start(bl,SC_KO_JYUMONJIKIRI,100,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case SP_SOULEXPLOSION:
	case KO_SETSUDAN:// Remove soul link when hit.
		status_change_end(bl, SC_SPIRIT, INVALID_TIMER);
		status_change_end(bl, SC_SOULGOLEM, INVALID_TIMER);
		status_change_end(bl, SC_SOULSHADOW, INVALID_TIMER);
		status_change_end(bl, SC_SOULFALCON, INVALID_TIMER);
		status_change_end(bl, SC_SOULFAIRY, INVALID_TIMER);
		break;
	case KO_MAKIBISHI:
		status_change_start(src, bl, SC_STUN, 1000 * skill_lv, skill_lv, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 2);
		break;
	case GC_DARKCROW:
		sc_start(bl,SC_DARKCROW,100,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case NC_MAGMA_ERUPTION:
		sc_start(bl,SC_STUN,90,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case NC_MAGMA_ERUPTION_DOTDAMAGE:
		sc_start(bl,SC_BURNING,10*skill_lv,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case RK_DRAGONBREATH_WATER://jRO says success chance is 15%. [Rytech]
		sc_start(bl, SC_FROST,15,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case GN_ILLUSIONDOPING:
		sc_start(bl, SC_ILLUSIONDOPING, 100 - 10 * skill_lv, skill_lv, skill_get_time(skill_id, skill_lv));
		break;
	// A number of Summoner skills has a chance of giving a status. kRO doesn't say what the chance is for
	// most of them but jRO does. However, jRO's version is heavly modified. Need to confirm on kRO. [Rytech]
	case SU_SCRATCH:// kRO says there's a chance of bleeding. jRO says its 5 + 5 * SkillLV.
		sc_start2(bl,SC_BLEEDING,5+5*skill_lv,skill_lv,src->id,skill_get_time(skill_id,skill_lv));
		break;
	case SU_SV_STEMSPEAR:
		sc_start2(bl,SC_BLEEDING,10,skill_lv,src->id,skill_get_time(skill_id,skill_lv));
		break;
	case SU_SCAROFTAROU:// Success chance??? No clue for this or the stun chance. Don't even know the HP damage formula.
		sc_start(bl,SC_BITESCAR,100,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case SU_CN_METEOR2:// kRO says there's a chance of curse. jRO says the chance is 20%.
		sc_start(bl,SC_CURSE,20,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case SU_LUNATICCARROTBEAT2:// kRO says there's a chance of stun. jRO says the chance is 50%.
		sc_start(bl,SC_STUN,50,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case MH_NEEDLE_OF_PARALYZE:
		{
			int duration = skill_get_time(skill_id,skill_lv) - 1000 * tstatus->vit / 10;
			if ( duration < 5000 )
				duration = 5000;
			sc_start(bl, SC_NEEDLE_OF_PARALYZE, 40+5*skill_lv-(tstatus->vit+tstatus->luk)/20, skill_lv, duration);
		}
		break;
	case MH_POISON_MIST:
		sc_start(bl, SC_BLIND, 10 + 10 * skill_lv, skill_lv, skill_get_time2(skill_id,skill_lv));
		break;
	case MH_XENO_SLASHER:
		sc_start2(bl, SC_BLEEDING, 1 * skill_lv, skill_lv, src->id, skill_get_time2(skill_id, skill_lv));
		break;
	case MH_SILVERVEIN_RUSH:
		sc_start(bl, SC_STUN, 20 + 5 * skill_lv, skill_lv, skill_get_time(skill_id,skill_lv));
		break;
	case MH_MIDNIGHT_FRENZY:
		sc_start(bl, SC_FEAR, (10 + 2 * skill_lv) * hd->hom_spiritball_old, skill_lv, skill_get_time(skill_id,skill_lv));
		break;
	case MH_STAHL_HORN:
		sc_start(bl, SC_STUN, 16 + 4 * skill_lv, skill_lv, skill_get_time(skill_id,skill_lv));
		break;
	case MH_TINDER_BREAKER:
		sc_start(bl, SC_TINDER_BREAKER, 100, skill_lv, 1000 * (sstatus->str / 7 - tstatus->str / 10));
		break;
	case MH_CBC:
		{
			int HPdamage = 400 * skill_lv + 4 * hd->homunculus.level;
			int SPdamage = 10 * skill_lv + hd->homunculus.level / 5 + hd->homunculus.dex / 10;

			// A bonus is applied to HPdamage using SPdamage
			// formula x10 if entity is a monster.
			if ( !(bl->type&BL_CONSUME) )
			{
				HPdamage += 10 * SPdamage;
				SPdamage = 0;// Signals later that entity is a monster.
			}
			// Officially the Tinder Breaker status is restarted after CBC's use.
			// Why? I don't know. Ask Gravity. Im leaving the code disabled since its pointless.
			//status_change_end(bl, SC_TINDER_BREAKER, INVALID_TIMER);
			//sc_start(bl, SC_TINDER_BREAKER, 100, skill_lv, 1000 * (sstatus->str / 7 - tstatus->str / 10));
			sc_start4(bl, SC_CBC, 100, skill_lv, HPdamage, SPdamage, 0, 1000 * (sstatus->str / 7 - tstatus->str / 10));
		}
		break;
	case MH_EQC:
		sc_start(bl, SC_STUN, 100, skill_lv, 1000 * hd->homunculus.level / 50 + 500 * skill_lv);
		sc_start(bl, SC_EQC, 100, skill_lv, skill_get_time(skill_id,skill_lv));
		status_change_end(bl, SC_TINDER_BREAKER, -1);
		break;
	case MH_LAVA_SLIDE:
		sc_start(bl, SC_BURNING, 10 * skill_lv, skill_lv, skill_get_time2(skill_id,skill_lv));
		break;
	case EL_FIRE_MANTLE:
		sc_start(bl, SC_BURNING, 5, skill_lv, skill_get_time2(skill_id, skill_lv));
		break;
	case EL_WIND_SLASH:
		{
			int duration = 10000;// Default JobLv 50 duration.
			if ( ed )
				duration = skill_get_time(skill_id,skill_lv) * status_get_job_lv_effect(&ed->master->bl) / 5;
			status_change_start(src,bl,SC_BLEEDING,2500,skill_lv,src->id,0,0,duration,2);
		}
		break;
	case EL_TYPOON_MIS_ATK:
		status_change_start(src,bl,SC_SILENCE,10000,skill_lv,0,0,0,skill_get_time(skill_id,skill_lv) * status_get_base_lv_effect(bl) / 10,2);
		break;
	case EL_STONE_HAMMER:
		sc_start(bl,SC_STUN,25,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case EL_ROCK_CRUSHER:
		{
			int duration = 15000;// Default BaseLV duration.
			if ( ed )
				duration = skill_get_time(skill_id,skill_lv) * status_get_base_lv_effect(&ed->master->bl) / 10;
			sc_start(bl,SC_ROCK_CRUSHER,100,skill_lv,duration);
		}
		break;
	case EL_ROCK_CRUSHER_ATK:
		{
			short chance = 100;
			int duration = 15000;// Default BaseLV duration.
			if ( ed )
			{
				chance = status_get_base_lv_effect(&ed->master->bl) + status_get_job_lv_effect(&ed->master->bl) - status_get_lv(bl) - tstatus->vit;
				duration = skill_get_time(skill_id,skill_lv) * status_get_base_lv_effect(&ed->master->bl) / 10;
			}
			sc_start(bl,SC_ROCK_CRUSHER_ATK,chance,skill_lv,duration);
		}
		break;
	case EL_STONE_RAIN:
		if ( attack_type&BF_WEAPON )
		{// Only the physical version can stun.
			int duration = 5000;// Default JobLv 50 duration.
			if ( ed )
				duration = skill_get_time(skill_id,skill_lv) * status_get_job_lv_effect(&ed->master->bl) / 10;
			status_change_start(src,bl,SC_STUN,5000,skill_lv,0,0,0,duration,2);
		}
		break;
	case WL_HELLINFERNO:
		status_change_start(src, bl, SC_BURNING, (55 + 5 * skill_lv)*100, skill_lv, 1000, src->id, 0, skill_get_time(skill_id, skill_lv), 0);
		break;
	}

	if (md && battle_config.summons_trigger_autospells && md->master_id && md->special_state.ai)
	{	//Pass heritage to Master for status causing effects. [Skotlex]
		sd = map_id2sd(md->master_id);
		src = sd?&sd->bl:src;
	}

	// Coma
	if (sd && sd->special_state.bonus_coma && (!md || status_get_race2(&md->bl) != RC2_GVG || status_get_class(&md->bl) != CLASS_BATTLEFIELD)) {
		rate = 0;
		//! TODO: Filter the skills that shouldn't inflict coma bonus, to avoid some non-damage skills inflict coma. [Cydh]
		if (!skill_id || !(skill_get_nk(skill_id)&NK_NO_DAMAGE)) {
			rate += sd->coma_class[tstatus->class_] + sd->coma_class[CLASS_ALL];
			rate += sd->coma_race[tstatus->race] + sd->coma_race[RC_ALL];
		}
		if (attack_type&BF_WEAPON) {
			rate += sd->weapon_coma_ele[tstatus->def_ele] + sd->weapon_coma_ele[ELE_ALL];
			rate += sd->weapon_coma_race[tstatus->race] + sd->weapon_coma_race[RC_ALL];
			rate += sd->weapon_coma_class[tstatus->class_] + sd->weapon_coma_class[CLASS_ALL];
		}
		if (rate > 0)
			status_change_start(src, bl, SC_COMA, rate, 0, 0, src->id, 0, 0, 0);
	}

	if (attack_type&BF_WEAPON)
	{ // Breaking Equipment
		if( sd && battle_config.equip_self_break_rate )
		{	// Self weapon breaking
			rate = battle_config.equip_natural_break_rate;
			if( sc )
			{
				if(sc->data[SC_OVERTHRUST])
					rate += 10;
				if(sc->data[SC_MAXOVERTHRUST])
					rate += 10;
			}
			if( rate )
				skill_break_equip(src, EQP_WEAPON, rate, BCT_SELF);
		}	
		if( battle_config.equip_skill_break_rate && skill_id != WS_CARTTERMINATION && skill_id != ITM_TOMAHAWK )
		{	// Cart Termination/Tomahawk won't trigger breaking data. Why? No idea, go ask Gravity.
			// Target weapon breaking
			rate = 0;
			if( sd )
				rate += sd->bonus.break_weapon_rate;
			if( sc && sc->data[SC_MELTDOWN] )
				rate += sc->data[SC_MELTDOWN]->val2;
			if( rate )
				skill_break_equip(bl, EQP_WEAPON, rate, BCT_ENEMY);

			// Target armor breaking
			rate = 0;
			if( sd )
				rate += sd->bonus.break_armor_rate;
			if( sc && sc->data[SC_MELTDOWN] )
				rate += sc->data[SC_MELTDOWN]->val3;
			if( rate )
				skill_break_equip(bl, EQP_ARMOR, rate, BCT_ENEMY);
		}
		if (sd && !skill_id && bl->type == BL_PC) { // This effect does not work with skills.
			if (sd->def_set_race[tstatus->race].rate)
				status_change_start(src, bl, SC_DEFSET, sd->def_set_race[tstatus->race].rate, sd->def_set_race[tstatus->race].value,
					0, 0, 0, sd->def_set_race[tstatus->race].tick, 2);
			if (sd->mdef_set_race[tstatus->race].rate)
				status_change_start(src, bl, SC_MDEFSET, sd->mdef_set_race[tstatus->race].rate, sd->mdef_set_race[tstatus->race].value,
					0, 0, 0, sd->mdef_set_race[tstatus->race].tick, 2);
		}
	}

	// Autospell when attacking
	if( sd && !status_isdead(bl) && sd->autospell[0].id )
	{
		struct block_list *tbl;
		struct unit_data *ud;
		int i, skill_lv, type;

		for (i = 0; i < ARRAYLENGTH(sd->autospell) && sd->autospell[i].id; i++) {

			if (!(((sd->autospell[i].flag)&attack_type)&BF_WEAPONMASK &&
				((sd->autospell[i].flag)&attack_type)&BF_RANGEMASK &&
				((sd->autospell[i].flag)&attack_type)&BF_SKILLMASK))
				continue; // one or more trigger conditions were not fulfilled

			skill = (sd->autospell[i].id > 0) ? sd->autospell[i].id : -sd->autospell[i].id;

			sd->state.autocast = 1;
			if (skillnotok(skill, sd)) {
				sd->state.autocast = 0;
				continue;
			}
			sd->state.autocast = 0;

			skill_lv = sd->autospell[i].lv?sd->autospell[i].lv:1;
			if (skill_lv < 0) skill_lv = 1+rnd()%(-skill_lv);

			rate = (!sd->state.arrow_atk) ? sd->autospell[i].rate : sd->autospell[i].rate / 2;

			if (rnd()%1000 >= rate)
				continue;

			tbl = (sd->autospell[i].id < 0) ? src : bl;

			if ((type = skill_get_casttype(skill)) == CAST_GROUND) {
				int maxcount = 0;
				if (!(BL_PC&battle_config.skill_reiteration) &&
					skill_get_unit_flag(skill)&UF_NOREITERATION &&
					skill_check_unit_range(src, tbl->x, tbl->y, skill, skill_lv)
					) {
					continue;
				}
				if (BL_PC&battle_config.skill_nofootset &&
					skill_get_unit_flag(skill)&UF_NOFOOTSET &&
					skill_check_unit_range2(src, tbl->x, tbl->y, skill, skill_lv)
					) {
					continue;
				}
				if (BL_PC&battle_config.land_skill_limit &&
					(maxcount = skill_get_maxcount(skill, skill_lv)) > 0
					) {
					int v;
					for (v = 0; v < MAX_SKILLUNITGROUP && sd->ud.skillunit[v] && maxcount; v++) {
						if (sd->ud.skillunit[v]->skill_id == skill)
							maxcount--;
					}
					if (maxcount == 0) {
						continue;
					}
				}
			}

			if (battle_config.autospell_check_range &&
				!battle_check_range(src, tbl, skill_get_range2(src, skill, skill_lv, true)))
				continue;

			if (skill == AS_SONICBLOW)
				pc_stop_attack(sd); //Special case, Sonic Blow autospell should stop the player attacking.
			else if (skill == PF_SPIDERWEB) //Special case, due to its nature of coding.
				type = CAST_GROUND;

			sd->state.autocast = 1;
			skill_consume_requirement(sd,skill,skill_lv,1);
			skill_toggle_magicpower(src, skill);

			switch (type) {
				case CAST_GROUND:
					skill_castend_pos2(src, tbl->x, tbl->y, skill, skill_lv, tick, 0);
					break;
				case CAST_NODAMAGE:
					skill_castend_nodamage_id(src, tbl, skill, skill_lv, tick, 0);
					break;
				case CAST_DAMAGE:
					skill_castend_damage_id(src, tbl, skill, skill_lv, tick, 0);
					break;
			}
			sd->state.autocast = 0;
			//Set canact delay. [Skotlex]
			ud = unit_bl2ud(src);
			if (ud) {
				rate = skill_delayfix(src, skill, skill_lv);
				if (DIFF_TICK(ud->canact_tick, tick + rate) < 0){
					ud->canact_tick = max(tick + rate, ud->canact_tick);
					if ( battle_config.display_status_timers && sd )
						clif_status_change(src, SI_ACTIONDELAY, 1, rate, 0, 0, 0);
				}
				if (skill_get_cooldown(skill,skill_lv))
					skill_blockpc_start(sd, skill, skill_get_cooldown(skill, skill_lv));
			}
		}
	}

	//Autobonus when attacking
	if( sd && sd->autobonus[0].rate )
	{
		int i;
		for( i = 0; i < ARRAYLENGTH(sd->autobonus); i++ )
		{
			if( rnd()%1000 >= sd->autobonus[i].rate )
				continue;
			if (!(((sd->autobonus[i].atk_type)&attack_type)&BF_WEAPONMASK &&
				((sd->autobonus[i].atk_type)&attack_type)&BF_RANGEMASK &&
				((sd->autobonus[i].atk_type)&attack_type)&BF_SKILLMASK))
				continue; // one or more trigger conditions were not fulfilled
			pc_exeautobonus(sd,&sd->autobonus[i]);
		}
	}

	//Polymorph
	if(sd && sd->bonus.classchange && attack_type&BF_WEAPON &&
		dstmd && !(tstatus->mode&MD_STATUS_IMMUNE) &&
		(rnd()%10000 < sd->bonus.classchange))
	{
		int class_ = mob_get_random_id(0, 1, 0);
		if (class_ != 0 && mobdb_checkid(class_))
			mob_class_change(dstmd, class_);
	}

	return 0;
}

int skill_onskillusage(struct map_session_data *sd, struct block_list *bl, int skill_id, int64 tick)
{
	uint8 i;
	struct block_list *tbl;

	if( sd == NULL || skill_id <= 0 )
		return 0;

	for( i = 0; i < ARRAYLENGTH(sd->autospell3) && sd->autospell3[i].flag; i++ )
	{
		int skill, skill_lv, type;

		if( sd->autospell3[i].flag != skill_id )
			continue;

		if( sd->autospell3[i].lock )
			continue;  // autospell already being executed

		skill = sd->autospell3[i].id;
		sd->state.autocast = 1; //set this to bypass sd->canskill_tick check
		
		if (skillnotok((skill > 0) ? skill : skill * -1, sd))
			continue;

		sd->state.autocast = 0;

		if (skill >= 0 && bl == NULL)
			continue; // No target
		if( rnd()%1000 >= sd->autospell3[i].rate )
			continue;
		
		skill_lv = sd->autospell3[i].lv ? sd->autospell3[i].lv : 1;
		if (skill < 0) {
			tbl = &sd->bl;
			skill *= -1;
			skill_lv = 1 + rnd() % (-skill_lv); //random skill_lv
		}
		else
			tbl = bl;

		if ((type = skill_get_casttype(skill)) == CAST_GROUND) {
			int maxcount = 0;
			if (!(BL_PC&battle_config.skill_reiteration) &&
				skill_get_unit_flag(skill)&UF_NOREITERATION &&
				skill_check_unit_range(&sd->bl, tbl->x, tbl->y, skill, skill_lv)
				) {
				continue;
			}
			if (BL_PC&battle_config.skill_nofootset &&
				skill_get_unit_flag(skill)&UF_NOFOOTSET &&
				skill_check_unit_range2(&sd->bl, tbl->x, tbl->y, skill, skill_lv)
				) {
				continue;
			}
			if (BL_PC&battle_config.land_skill_limit &&
				(maxcount = skill_get_maxcount(skill, skill_lv)) > 0
				) {
				int v;
				for (v = 0; v < MAX_SKILLUNITGROUP && sd->ud.skillunit[v] && maxcount; v++) {
					if (sd->ud.skillunit[v]->skill_id == skill)
						maxcount--;
				}
				if (maxcount == 0) {
					continue;
				}
			}
		}

		if (battle_config.autospell_check_range &&
			!battle_check_range(&sd->bl, tbl, skill_get_range2(&sd->bl, skill, skill_lv, true)))
			continue;

		sd->state.autocast = 1;
		sd->autospell3[i].lock = true;
		skill_consume_requirement(sd,skill,skill_lv,1);
		switch( type )
		{
			case CAST_GROUND:   skill_castend_pos2(&sd->bl, tbl->x, tbl->y, skill, skill_lv, tick, 0); break;
			case CAST_NODAMAGE: skill_castend_nodamage_id(&sd->bl, tbl, skill, skill_lv, tick, 0); break;
			case CAST_DAMAGE:   skill_castend_damage_id(&sd->bl, tbl, skill, skill_lv, tick, 0); break;
		}
		sd->autospell3[i].lock = false;
		sd->state.autocast = 0;
	}

	if( sd && sd->autobonus3[0].rate )
	{
		for( i = 0; i < ARRAYLENGTH(sd->autobonus3); i++ )
		{
			if( rnd()%1000 >= sd->autobonus3[i].rate )
				continue;
			if( sd->autobonus3[i].atk_type != skill_id )
				continue;
			pc_exeautobonus(sd,&sd->autobonus3[i]);
		}
	}

	return 1;
}

/* Splitted off from skill_additional_effect, which is never called when the
 * attack skill kills the enemy. Place in this function counter status effects
 * when using skills (eg: Asura's sp regen penalty, or counter-status effects
 * from cards) that will take effect on the source, not the target. [Skotlex]
 * Note: Currently this function only applies to Extremity Fist and BF_WEAPON
 * type of skills, so not every instance of skill_additional_effect needs a call
 * to this one.
 */
int skill_counter_additional_effect (struct block_list* src, struct block_list *bl, uint16 skill_id, uint16 skill_lv, int attack_type, int64 tick)
{
	int rate;
	struct map_session_data *sd=NULL;
	struct map_session_data *dstsd=NULL;
	struct status_data *sstatus, *tstatus;
	struct status_change *sc;

	nullpo_ret(src);
	nullpo_ret(bl);

	if(skill_id > 0 && !skill_lv) return 0;	// don't forget auto attacks! - celest

	sc = status_get_sc(src);

	sd = BL_CAST(BL_PC, src);
	dstsd = BL_CAST(BL_PC, bl);

	sstatus = status_get_status_data(src);
	tstatus = status_get_status_data(bl);

	if(dstsd && attack_type&BF_WEAPON)
	{	//Counter effects.
		enum sc_type type;
		uint8 i;
		unsigned int time = 0;
		for (i = 0; i < ARRAYLENGTH(dstsd->addeff_atked) && dstsd->addeff_atked[i].flag; i++) {
			rate = dstsd->addeff_atked[i].rate;
			if (attack_type&BF_LONG)
				rate += dstsd->addeff_atked[i].arrow_rate;
			if (!rate)
				continue;

			if ((dstsd->addeff_atked[i].flag&(ATF_LONG | ATF_SHORT)) != (ATF_LONG | ATF_SHORT)) {	//Trigger has range consideration.
				if ((dstsd->addeff_atked[i].flag&ATF_LONG && !(attack_type&BF_LONG)) ||
					(dstsd->addeff_atked[i].flag&ATF_SHORT && !(attack_type&BF_SHORT)))
					continue; //Range Failed.
			}
			type = dstsd->addeff_atked[i].sc;
			time = dstsd->addeff_atked[i].duration;

			if (dstsd->addeff_atked[i].flag&ATF_TARGET)
				status_change_start(src,src,type,rate,7,0,0,0,time,0);

			if (dstsd->addeff_atked[i].flag&ATF_SELF && !status_isdead(bl))
				status_change_start(src,bl,type,rate,7,0,0,0,time,0);
		}
	}

	switch(skill_id){
	case MO_EXTREMITYFIST:
		sc_start(src,status_skill2sc(skill_id),100,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case GS_FULLBUSTER:
		sc_start(src,SC_BLIND,2*skill_lv,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;
	case HFLI_SBR44:	//[orn]
	case HVAN_EXPLOSION:
		if(src->type == BL_HOM){
			TBL_HOM *hd = (TBL_HOM*)src;
			hd->homunculus.intimacy = 200;
			if (hd->master)
				clif_send_homdata(hd->master,SP_INTIMATE,hd->homunculus.intimacy/100);
		}
		break;
	case MH_SONIC_CRAW:
		sc_start(src,SC_SONIC_CLAW_POSTDELAY,100,skill_lv,2000);
		break;
	case MH_SILVERVEIN_RUSH:
		sc_start(src,SC_SILVERVEIN_RUSH_POSTDELAY,100,skill_lv,2000);
		break;
	case MH_MIDNIGHT_FRENZY:
		sc_start2(src,SC_MIDNIGHT_FRENZY_POSTDELAY,100,skill_lv,bl->id,2000);
		break;
	case MH_TINDER_BREAKER:
		sc_start(src,SC_TINDER_BREAKER, 100, skill_lv, 1000 * (sstatus->str / 7 - tstatus->str / 10));
		sc_start(src,SC_TINDER_BREAKER_POSTDELAY,100,skill_lv,2000);
		break;
	case MH_CBC:
		// Officially the Tinder Breaker status is restarted after CBC's use.
		// Why? I don't know. Ask Gravity. Im leaving the code disabled since its pointless.
		//status_change_end(src, SC_TINDER_BREAKER, -1);
		//sc_start(src,SC_TINDER_BREAKER, 100, skill_lv, 1000 * (sstatus->str / 7 - tstatus->str / 10));
		sc_start(src,SC_CBC_POSTDELAY,100,skill_lv,2000);
		break;
	case MH_EQC:
		status_change_end(src, SC_CBC_POSTDELAY, INVALID_TIMER);// End of grappler combo as it doesn't loop.
		status_change_end(src, SC_TINDER_BREAKER, INVALID_TIMER);
		break;
	case CR_GRANDCROSS:
	case NPC_GRANDDARKNESS:
		attack_type |= BF_WEAPON;
		break;
	case LG_HESPERUSLIT:// Generates rage spheres for all Royal Guards in banding that has Force of Vanguard active.
		if (sc && sc->data[SC_BANDING] && (battle_config.hesperuslit_bonus_stack == 1 && sc->data[SC_BANDING]->val2 >= 7 || sc->data[SC_BANDING]->val2 == 7))
			party_foreachsamemap(party_sub_count_banding, sd, 3, 2);
		break;
	}

	if(sd && (sd->class_&MAPID_UPPERMASK) == MAPID_STAR_GLADIATOR && map[bl->m].flag.nosunmoonstarmiracle == 0 && 
		rnd()%10000 < battle_config.sg_miracle_skill_ratio)	//SG_MIRACLE [Komurka]
		status_change_start(src, src, SC_MIRACLE, battle_config.sg_miracle_skill_ratio, 1, 0, 0, 0, battle_config.sg_miracle_skill_duration, 0);

	if(sd && skill_id && attack_type&BF_MAGIC && status_isdead(bl) &&
	 	!(skill_get_inf(skill_id)&(INF_GROUND_SKILL|INF_SELF_SKILL)) &&
		(rate=pc_checkskill(sd,HW_SOULDRAIN))>0
	){	//Soul Drain should only work on targetted spells [Skotlex]
		if (pc_issit(sd)) pc_setstand(sd, true); //Character stuck in attacking animation while 'sitting' fix. [Skotlex]
		clif_skill_nodamage(src,bl,HW_SOULDRAIN,rate,1);
		status_heal(src, 0, status_get_lv(bl)*(95+15*rate)/100, 2);
	}

	if( sd && status_isdead(bl) )
	{
		int sp = 0, hp = 0;
		if((attack_type&(BF_WEAPON | BF_SHORT)) == (BF_WEAPON | BF_SHORT))
		{
			sp += sd->bonus.sp_gain_value;
			sp += sd->sp_gain_race[status_get_race(bl)] + sd->sp_gain_race[RC_ALL];
			hp += sd->bonus.hp_gain_value;
		}
		if( attack_type&BF_MAGIC )
		{
			sp += sd->bonus.magic_sp_gain_value;
			hp += sd->bonus.magic_hp_gain_value;
			if (sc && skill_id == WZ_WATERBALL) {
				if (sc->data[SC_SPIRIT] &&
					sc->data[SC_SPIRIT]->val2 == SL_WIZARD &&
					sc->data[SC_SPIRIT]->val3 == WZ_WATERBALL)
					sc->data[SC_SPIRIT]->val3 = 0; //Clear bounced spell check.
			}
		}
		if( hp || sp )
		{// updated to force healing to allow healing through berserk
			status_heal(src, hp, sp, battle_config.show_hp_sp_gain ? 3 : 1);
		}
	}

	// Trigger counter-spells to retaliate against damage causing skills.
	if(dstsd && !status_isdead(bl) && dstsd->autospell2[0].id &&
		!(skill_id && skill_get_nk(skill_id)&NK_NO_DAMAGE))
	{
		struct block_list *tbl;
		struct unit_data *ud;
		int i, skill_id, skill_lv, rate, type;

		for (i = 0; i < ARRAYLENGTH(dstsd->autospell2) && dstsd->autospell2[i].id; i++) {

			if (!(((dstsd->autospell2[i].flag)&attack_type)&BF_WEAPONMASK &&
				((dstsd->autospell2[i].flag)&attack_type)&BF_RANGEMASK &&
				((dstsd->autospell2[i].flag)&attack_type)&BF_SKILLMASK))
				continue; // one or more trigger conditions were not fulfilled

			skill_id = (dstsd->autospell2[i].id > 0) ? dstsd->autospell2[i].id : -dstsd->autospell2[i].id;
			skill_lv = dstsd->autospell2[i].lv?dstsd->autospell2[i].lv:1;
			if (skill_lv < 0) skill_lv = 1+rnd()%(-skill_lv);

			rate = dstsd->autospell2[i].rate;
			//Physical range attacks only trigger autospells half of the time
			if ((attack_type&(BF_WEAPON | BF_LONG)) == (BF_WEAPON | BF_LONG))
				 rate>>=1;

			dstsd->state.autocast = 1;
			if (skillnotok(skill_id, dstsd)) {
				dstsd->state.autocast = 0;
				continue;
			}
			dstsd->state.autocast = 0;

			if (rnd()%1000 >= rate)
				continue;

			tbl = (dstsd->autospell2[i].id < 0) ? bl : src;

			if ((type = skill_get_casttype(skill_id)) == CAST_GROUND) {
				int maxcount = 0;
				if (!(BL_PC&battle_config.skill_reiteration) &&
					skill_get_unit_flag(skill_id)&UF_NOREITERATION &&
					skill_check_unit_range(bl, tbl->x, tbl->y, skill_id, skill_lv)
					) {
					continue;
				}
				if (BL_PC&battle_config.skill_nofootset &&
					skill_get_unit_flag(skill_id)&UF_NOFOOTSET &&
					skill_check_unit_range2(bl, tbl->x, tbl->y, skill_id, skill_lv)
					) {
					continue;
				}
				if (BL_PC&battle_config.land_skill_limit &&
					(maxcount = skill_get_maxcount(skill_id, skill_lv)) > 0
					) {
					int v;
					for (v = 0; v < MAX_SKILLUNITGROUP && dstsd->ud.skillunit[v] && maxcount; v++) {
						if (dstsd->ud.skillunit[v]->skill_id == skill_id)
							maxcount--;
					}
					if (maxcount == 0) {
						continue;
					}
				}
			}

			if (!battle_check_range(src, tbl, skill_get_range2(src, skill_id, skill_lv, true)) && battle_config.autospell_check_range)
				continue;

			dstsd->state.autocast = 1;
			skill_consume_requirement(dstsd,skill_id,skill_lv,1);
			switch (type) {
				case CAST_GROUND:
					skill_castend_pos2(bl, tbl->x, tbl->y, skill_id, skill_lv, tick, 0);
					break;
				case CAST_NODAMAGE:
					skill_castend_nodamage_id(bl, tbl, skill_id, skill_lv, tick, 0);
					break;
				case CAST_DAMAGE:
					skill_castend_damage_id(bl, tbl, skill_id, skill_lv, tick, 0);
					break;
			}
			dstsd->state.autocast = 0;
			//Set canact delay. [Skotlex]
			ud = unit_bl2ud(bl);
			if (ud) {
				rate = skill_delayfix(bl, skill_id, skill_lv);
				if (DIFF_TICK(ud->canact_tick, tick + rate) < 0){
					ud->canact_tick = max(tick + rate, ud->canact_tick);
					if ( battle_config.display_status_timers && dstsd )
						clif_status_change(bl, SI_ACTIONDELAY, 1, rate, 0, 0, 0);
				}
				if (skill_get_cooldown(skill_id,skill_lv))
					skill_blockpc_start(sd, skill_id, skill_get_cooldown(skill_id, skill_lv));
			}
		}
	}

	//Autobonus when attacked
	if( dstsd && !status_isdead(bl) && dstsd->autobonus2[0].rate && !(skill_id && skill_get_nk(skill_id)&NK_NO_DAMAGE) )
	{
		int i;
		for( i = 0; i < ARRAYLENGTH(dstsd->autobonus2); i++ )
		{
			if( rnd()%1000 >= dstsd->autobonus2[i].rate )
				continue;
			if (!(((dstsd->autobonus2[i].atk_type)&attack_type)&BF_WEAPONMASK &&
				((dstsd->autobonus2[i].atk_type)&attack_type)&BF_RANGEMASK &&
				((dstsd->autobonus2[i].atk_type)&attack_type)&BF_SKILLMASK))
				continue; // one or more trigger conditions were not fulfilled
			pc_exeautobonus(dstsd,&dstsd->autobonus2[i]);
		}
	}

	return 0;
}
/*=========================================================================
 Breaks equipment. On-non players causes the corresponding strip effect.
 - rate goes from 0 to 10000 (100.00%)
 - flag is a BCT_ flag to indicate which type of adjustment should be used
   (BCT_ENEMY/BCT_PARTY/BCT_SELF) are the valid values.
--------------------------------------------------------------------------*/
int skill_break_equip (struct block_list *bl, unsigned short where, int rate, int flag)
{
	const int where_list[5]     = {EQP_WEAPON, EQP_ARMOR, EQP_SHIELD, EQP_HELM, EQP_ACC};
	const enum sc_type scatk[5] = {SC_STRIPWEAPON, SC_STRIPARMOR, SC_STRIPSHIELD, SC_STRIPHELM, SC__STRIPACCESSARY};
	const enum sc_type scdef[5] = {SC_CP_WEAPON, SC_CP_ARMOR, SC_CP_SHIELD, SC_CP_HELM, SC_COMMON_MIN};
	struct status_change *sc = status_get_sc(bl);
	int i,j;
	TBL_PC *sd;
	sd = BL_CAST(BL_PC, bl);
	if (sc && !sc->count)
		sc = NULL;

	if (sd) {
		if (sd->bonus.unbreakable_equip)
			where &= ~sd->bonus.unbreakable_equip;
		if (sd->bonus.unbreakable)
			rate -= rate*sd->bonus.unbreakable/100;
		if (where&EQP_WEAPON) {
			switch (sd->status.weapon) {
				case W_FIST:	//Bare fists should not break :P
				case W_1HAXE:
				case W_2HAXE:
				case W_MACE: // Axes and Maces can't be broken [DracoRPG]
				case W_2HMACE:
				case W_STAFF:
				case W_2HSTAFF:
				case W_BOOK: //Rods and Books can't be broken [Skotlex]
				case W_HUUMA:
				case W_DOUBLE_AA:	// Axe usage during dual wield should also prevent breaking [Neutral]
				case W_DOUBLE_DA:
				case W_DOUBLE_SA:
					where &= ~EQP_WEAPON;
			}
		}
	}
	if (flag&BCT_ENEMY) {
		if (battle_config.equip_skill_break_rate != 100)
			rate = rate*battle_config.equip_skill_break_rate/100;
	} else if (flag&(BCT_PARTY|BCT_SELF)) {
		if (battle_config.equip_self_break_rate != 100)
			rate = rate*battle_config.equip_self_break_rate/100;
	}

	for (i = 0; i < 4; i++) {
		if (where&where_list[i]) {
			if (sc && sc->count && sc->data[scdef[i]])
				where&=~where_list[i];
			else if (rnd()%10000 >= rate)
				where&=~where_list[i];
			else if (!sd) //Cause Strip effect.
				sc_start(bl,scatk[i],100,0,skill_get_time(status_sc2skill(scatk[i]),1));
		}
	}
	if (!where) //Nothing to break.
		return 0;
	if (sd) {
		for (i = 0; i < EQI_MAX; i++) {
			j = sd->equip_index[i];
			if (j < 0 || sd->inventory.u.items_inventory[j].attribute == 1 || !sd->inventory_data[j])
				continue;

			switch(i) {
				case EQI_HEAD_TOP: //Upper Head
					flag = (where&EQP_HELM);
					break;
				case EQI_ARMOR: //Body
					flag = (where&EQP_ARMOR);
					break;
				case EQI_HAND_R: //Left/Right hands
				case EQI_HAND_L:
					flag = (
						(where&EQP_WEAPON && sd->inventory_data[j]->type == IT_WEAPON) ||
						(where&EQP_SHIELD && sd->inventory_data[j]->type == IT_ARMOR));
					break;
				case EQI_SHOES:
					flag = (where&EQP_SHOES);
					break;
				case EQI_GARMENT:
					flag = (where&EQP_GARMENT);
					break;
				case EQI_ACC_L:
				case EQI_ACC_R:
					flag = (where&EQP_ACC);
				default:
					continue;
			}
			if (flag) {
				sd->inventory.u.items_inventory[j].attribute = 1;
				pc_unequipitem(sd, j, 3);
			}
		}
#if PACKETVER >= 20070521
		clif_equip_damaged(sd,i);
#endif
		clif_equiplist(sd);
	}

	return where; //Return list of pieces broken.
}

int skill_strip_equip(struct block_list *bl, unsigned short where, int rate, int lv, int time)
{
	struct status_change *sc;
	const int pos[5]             = {EQP_WEAPON, EQP_SHIELD, EQP_ARMOR, EQP_HELM, EQP_ACC};
	const enum sc_type sc_atk[5] = {SC_STRIPWEAPON, SC_STRIPSHIELD, SC_STRIPARMOR, SC_STRIPHELM, SC__STRIPACCESSARY};
	const enum sc_type sc_def[5] = {SC_CP_WEAPON, SC_CP_SHIELD, SC_CP_ARMOR, SC_CP_HELM, SC_NONE};
	int i;
	TBL_PC *sd;
	sd = BL_CAST(BL_PC, bl);

	if (rnd()%100 >= rate)
		return 0;

	sc = status_get_sc(bl);
	if (!sc)
		return 0;

	if (sd)
	{// Mado's are immune to stripping.
		if (pc_ismadogear(sd))
			return 0;
	}

	for (i = 0; i < ARRAYLENGTH(pos); i++) {
		if (where&pos[i] && sc_def[i] > SC_NONE && sc->data[sc_def[i]])
			where&=~pos[i];
	}
	if (!where) return 0;

	for (i = 0; i < ARRAYLENGTH(pos); i++) {
		if (where&pos[i] && !sc_start(bl, sc_atk[i], 100, lv, time))
			where&=~pos[i];
	}
	return where?1:0;
}


/*=========================================================================
 Used to knock back players, monsters, traps, etc
* @param src Object that give knock back
 * @param target Object that receive knock back
 * @param count Number of knock back cell requested
 * @param dir Direction indicates the way OPPOSITE to the knockback direction (or -1 for default behavior)
 * @param flag
		0x01 - position update packets must not be sent;
		0x02 - ignores players' special_state.no_knockback;
		These flags "return 'count' instead of 0 if target is cannot be knocked back":
		0x04 - at WOE/BG map;
		0x08 - if target is MD_KNOCKBACK_IMMUNE;
		0x10 - if target has 'special_state.no_knockback';
		0x20 - if target is in Basilica area;
 * @return Number of knocked back cells done
 -------------------------------------------------------------------------*/
int skill_blown(struct block_list* src, struct block_list* target, int count, int direction, unsigned char flag)
{
	int dx = 0, dy = 0;
	uint8 reason = 0, checkflag = 0;

	nullpo_ret(src);
	nullpo_ret(target);

	if (!count)
		return count; // Actual knockback distance is 0.

// Create flag needed in unit_blown_immune
	if (src != target)
		checkflag |= 0x1; // Offensive
	if (!(flag & 0x2))
		checkflag |= 0x2; // Knockback type
	if (status_get_class_(src) == CLASS_BOSS)
		checkflag |= 0x4; // Boss attack

	// Get reason and check for flags
	reason = unit_blown_immune(target, checkflag);
	switch (reason) {
	case 1: return ((flag & 0x04) ? count : 0); // No knocking back in WoE / BG
	case 2: return ((flag & 0x08) ? count : 0); // Immune can't be knocked back
	case 3: return ((flag & 0x20) ? count : 0); // Basilica caster can't be knocked-back by normal monsters.
	case 4: return ((flag & 0x10) ? count : 0); // Target has special_state.no_knockback (equip)
	case 5: return count; // Trap cannot be knocked back
	}

	if (direction == -1) // <optimized>: do the computation here instead of outside
		direction = map_calc_dir(target, src->x, src->y); // direction from src to target, reversed

	if (direction >= 0 && direction < 8)
	{	// take the reversed 'direction' and reverse it
		dx = -dirx[direction];
		dy = -diry[direction];
	}

	return unit_blown(target, dx, dy, count, flag);	// send over the proper flag
}


//Checks if 'bl' should reflect back a spell cast by 'src'.
//type is the type of magic attack: 0: indirect (aoe), 1: direct (targetted)
static int skill_magic_reflect(struct block_list* src, struct block_list* bl, int type)
{
	struct status_change *sc = status_get_sc(bl);
	struct map_session_data* sd = BL_CAST(BL_PC, bl);

	// item-based reflection
	if( sd && sd->bonus.magic_damage_return && type && rnd()%100 < sd->bonus.magic_damage_return )
		return 1;

	if (status_get_class_(src) == CLASS_BOSS)
		return 0;

	// status-based reflection
	if( !sc || sc->count == 0 )
		return 0;

	if( sc->data[SC_MAGICMIRROR] && rnd()%100 < sc->data[SC_MAGICMIRROR]->val2 )
		return 1;

	if( sc->data[SC_KAITE] && (src->type == BL_PC || status_get_lv(src) <= 80) )
	{// Kaite only works against non-players if they are low-level.
		clif_specialeffect(bl, 438, AREA);
		if( --sc->data[SC_KAITE]->val2 <= 0 )
			status_change_end(bl, SC_KAITE, INVALID_TIMER);
		return 2;
	}

	return 0;
}

/** Copy skill by Plagiarism or Reproduce
* @param src: The caster
* @param bl: The target
* @param skill_id: Skill that casted
* @param skill_lv: Skill level of the casted skill
*/
static void skill_do_copy(struct block_list* src, struct block_list *bl, uint16 skill_id, uint16 skill_lv) {
	TBL_PC *tsd = BL_CAST(BL_PC, bl);

	if (!tsd || (!pc_checkskill(tsd, RG_PLAGIARISM) && !pc_checkskill(tsd, SC_REPRODUCE)))
		return;
	//If SC_PRESERVE is active and SC__REPRODUCE is not active, nothing to do
	else if (&tsd->sc && tsd->sc.data[SC_PRESERVE] && !tsd->sc.data[SC__REPRODUCE])
		return;
	else {
		short idx;
		unsigned char lv;

		// Copy Referal: dummy skills should point to their source upon copying
		switch (skill_id)
		{
		case AB_DUPLELIGHT_MELEE:
		case AB_DUPLELIGHT_MAGIC:
			skill_id = AB_DUPLELIGHT;
			break;
		case WL_CHAINLIGHTNING_ATK:
			skill_id = WL_CHAINLIGHTNING;
			break;
		case WL_TETRAVORTEX_FIRE:
		case WL_TETRAVORTEX_WATER:
		case WL_TETRAVORTEX_WIND:
		case WL_TETRAVORTEX_GROUND:
			skill_id = WL_TETRAVORTEX;
			break;
		case WL_SUMMON_ATK_FIRE:
			skill_id = WL_SUMMONFB;
			break;
		case WL_SUMMON_ATK_WIND:
			skill_id = WL_SUMMONBL;
			break;
		case WL_SUMMON_ATK_WATER:
			skill_id = WL_SUMMONWB;
			break;
		case WL_SUMMON_ATK_GROUND:
			skill_id = WL_SUMMONSTONE;
			break;
		case LG_OVERBRAND_BRANDISH:
		case LG_OVERBRAND_PLUSATK:
			skill_id = LG_OVERBRAND;
			break;
		case SR_CRESCENTELBOW_AUTOSPELL:
			skill_id = SR_CRESCENTELBOW;
			break;
		case WM_REVERBERATION_MELEE:
		case WM_REVERBERATION_MAGIC:
			skill_id = WM_REVERBERATION;
			break;
		case WM_SEVERE_RAINSTORM_MELEE:
			skill_id = WM_SEVERE_RAINSTORM;
			break;
		case GN_CRAZYWEED_ATK:
			skill_id = GN_CRAZYWEED;
			break;
		case GN_FIRE_EXPANSION_SMOKE_POWDER:
		case GN_FIRE_EXPANSION_TEAR_GAS:
		case GN_FIRE_EXPANSION_ACID:
			skill_id = GN_FIRE_EXPANSION;
			break;
		case GN_HELLS_PLANT_ATK:
			skill_id = GN_HELLS_PLANT;
			break;
		case GN_SLINGITEM_RANGEMELEEATK:
			skill_id = GN_SLINGITEM;
			break;
			//case RL_GLITTERING_GREED_ATK:
			//	copy_skillid = RL_GLITTERING_GREED;
			//	break;
		case RL_R_TRIP_PLUSATK:
			skill_id = RL_R_TRIP;
			break;
		case SJ_FALLINGSTAR_ATK:
		case SJ_FALLINGSTAR_ATK2:
			skill_id = SJ_FALLINGSTAR;
			break;
		case OB_OBOROGENSOU_TRANSITION_ATK:
			skill_id = OB_OBOROGENSOU;
			break;
		case NC_MAGMA_ERUPTION_DOTDAMAGE:
			skill_id = NC_MAGMA_ERUPTION;
			break;
		case SU_CN_METEOR2:
			skill_id = SU_CN_METEOR;
			break;
		case SU_SV_ROOTTWIST_ATK:
			skill_id = SU_SV_ROOTTWIST;
			break;
		case SU_PICKYPECK_DOUBLE_ATK:
			skill_id = SU_PICKYPECK;
			break;
		case SU_LUNATICCARROTBEAT2:
			skill_id = SU_LUNATICCARROTBEAT;
			break;
		}

		//Use skill index, avoiding out-of-bound array [Cydh]
		if ((idx = skill_get_index(skill_id)) < 0)
			return;

		switch (skill_isCopyable(tsd, skill_id)) {
			case 1: //Copied by Plagiarism
				{
					if (tsd->cloneskill_idx >= 0 && tsd->status.skill[tsd->cloneskill_idx].flag == SKILL_FLAG_PLAGIARIZED) {
						clif_deleteskill(tsd, tsd->status.skill[tsd->cloneskill_idx].id);
						tsd->status.skill[tsd->cloneskill_idx].id = 0;
						tsd->status.skill[tsd->cloneskill_idx].lv = 0;
						tsd->status.skill[tsd->cloneskill_idx].flag = SKILL_FLAG_PERMANENT;
					}

					lv = min(skill_lv, pc_checkskill(tsd, RG_PLAGIARISM)); //Copied level never be > player's RG_PLAGIARISM level

					tsd->cloneskill_idx = idx;
					pc_setglobalreg(tsd, "CLONE_SKILL", skill_id);
					pc_setglobalreg(tsd, "CLONE_SKILL_LV", lv);
				}
				break;
			case 2: //Copied by Reproduce
				{
					struct status_change *tsc = status_get_sc(bl);
					//Already did SC check
					//Skill level copied depends on Reproduce skill that used
					lv = (tsc) ? tsc->data[SC__REPRODUCE]->val1 : 1;
					if (tsd->reproduceskill_idx >= 0 && tsd->status.skill[tsd->reproduceskill_idx].flag == SKILL_FLAG_PLAGIARIZED) {
						clif_deleteskill(tsd, tsd->status.skill[tsd->reproduceskill_idx].id);
						tsd->status.skill[tsd->reproduceskill_idx].id = 0;
						tsd->status.skill[tsd->reproduceskill_idx].lv = 0;
						tsd->status.skill[tsd->reproduceskill_idx].flag = SKILL_FLAG_PERMANENT;
					}

					//Level dependent and limitation.
					if (src->type == BL_PC) //If player, max skill level is skill_get_max(skill_id)
						lv = min(lv, skill_get_max(skill_id));
					else //Monster might used skill level > allowed player max skill lv. Ex. Drake with Waterball lv. 10
						lv = min(lv, skill_lv);

					tsd->reproduceskill_idx = idx;
					pc_setglobalreg(tsd, "REPRODUCE_SKILL", skill_id);
					pc_setglobalreg(tsd, "REPRODUCE_SKILL_LV", lv);
				}
				break;
			default:
				return;
		}

		tsd->status.skill[idx].id = skill_id;
		tsd->status.skill[idx].lv = lv;
		tsd->status.skill[idx].flag = SKILL_FLAG_PLAGIARIZED;
		clif_addskill(tsd, skill_id);
	}
}

/**
 * Checks whether a skill can be used in combos or not
 * @param skill_id: Target skill
 * @return	0: Skill is not a combo
 *			1: Skill is a normal combo
 *			2: Skill is combo that prioritizes auto-target even if val2 is set
 * @author Panikon
 **/
int skill_is_combo(int skill_id) {
	switch (skill_id) {
	case MO_CHAINCOMBO:
	case MO_COMBOFINISH:
	case CH_TIGERFIST:
	case CH_CHAINCRUSH:
	case MO_EXTREMITYFIST:
	case TK_TURNKICK:
	case TK_STORMKICK:
	case TK_DOWNKICK:
	case TK_COUNTER:
	case TK_JUMPKICK:
	case HT_POWER:
	case GC_COUNTERSLASH:
	case GC_WEAPONCRUSH:
	case SR_DRAGONCOMBO:
		return 1;
	case SR_FALLENEMPIRE:
	case SR_TIGERCANNON:
	case SR_GATEOFHELL:
		return 2;
	}
	return 0;
}

/*
 * Combo handler, start stop combo status
 */
void skill_combo_toggle_inf(struct block_list* bl, uint16 skill_id, int inf) {
	TBL_PC *sd = BL_CAST(BL_PC, bl);
	
	switch (skill_id) {
		case MH_MIDNIGHT_FRENZY:
		case MH_EQC:
			{
				int skill_id2 = ((skill_id == MH_EQC) ? MH_TINDER_BREAKER : MH_SONIC_CRAW);
				int idx = skill_id2 - HM_SKILLBASE;
				int flag = (inf ? SKILL_FLAG_TMP_COMBO : SKILL_FLAG_PERMANENT);
				TBL_HOM *hd = BL_CAST(BL_HOM, bl);
				sd = hd->master;
				hd->homunculus.hskill[idx].flag = flag;
				if (sd)
					clif_homskillinfoblock(sd); //refresh info //@FIXME we only want to refresh one skill
			}
			break;
		case MO_COMBOFINISH:
		case CH_TIGERFIST:
		case CH_CHAINCRUSH:
			if (sd)
				clif_skillupdateinfo(sd, MO_EXTREMITYFIST, inf, 0);
			break;
		case TK_JUMPKICK:
			if (sd)
				clif_skillupdateinfo(sd, TK_JUMPKICK, inf, 0);
			break;
		case MO_TRIPLEATTACK:
			if (sd && pc_checkskill(sd, SR_DRAGONCOMBO) > 0)
				clif_skillupdateinfo(sd, SR_DRAGONCOMBO, inf, 0);
			break;
		case SR_FALLENEMPIRE:
			if (sd) {
				clif_skillupdateinfo(sd, SR_GATEOFHELL, inf, 0);
				clif_skillupdateinfo(sd, SR_TIGERCANNON, inf, 0);
			}
			break;
	}
}

void skill_combo(struct block_list* src, struct block_list *dsrc, struct block_list *bl, uint16 skill_id, uint16 skill_lv, int64 tick) {
	int duration = 0; //Set to duration the user can use a combo skill or 1 for aftercast delay of pre-skill
	int nodelay = 0; //Set to 1 for no walk/attack delay, set to 2 for no walk delay
	int target_id = bl->id; //Set to 0 if combo skill should not autotarget
	struct status_change_entry *sce;
	TBL_PC *sd = BL_CAST(BL_PC, src);
	TBL_HOM *hd = BL_CAST(BL_HOM, src);
	struct status_change *sc = status_get_sc(src);
	
	if (sc == NULL)
		return;
	//End previous combo state after skill is invoked
	if ((sce = sc->data[SC_COMBO]) != NULL) {
		switch (skill_id) {
			case TK_TURNKICK:
			case TK_STORMKICK:
			case TK_DOWNKICK:
			case TK_COUNTER:
				if (sd && pc_famerank(sd->status.char_id, MAPID_TAEKWON)) {//Extend combo time.
					sce->val1 = skill_id; //Update combo-skill
					sce->val3 = skill_id;
					if (sce->timer != INVALID_TIMER)
						delete_timer(sce->timer, status_change_timer);
					sce->timer = add_timer(tick + sce->val4, status_change_timer, src->id, SC_COMBO);
					break;
				}
				unit_cancel_combo(src); // Cancel combo wait
				break;
			default:
				if (src == dsrc) // Ground skills are exceptions. [Inkfish]
					status_change_end(src, SC_COMBO, INVALID_TIMER);
		}
	}
	
	//start new combo
	if (sd) { //player only
		switch (skill_id) {
			case MO_TRIPLEATTACK:
				if (pc_checkskill(sd, MO_CHAINCOMBO) > 0 || pc_checkskill(sd, SR_DRAGONCOMBO) > 0) {
					duration = 1;
					target_id = 0; // Will target current auto-target instead
				}
				break;
			case MO_CHAINCOMBO:
				if (pc_checkskill(sd, MO_COMBOFINISH) > 0 && sd->spiritball > 0) {
					duration = 1;
					target_id = 0; // Will target current auto-target instead
				}
				break;
			case MO_COMBOFINISH:
				if (sd->status.party_id > 0) //bonus from SG_FRIEND [Komurka]
					party_skill_check(sd, sd->status.party_id, MO_COMBOFINISH, skill_lv);
				if (pc_checkskill(sd, CH_TIGERFIST) > 0 && sd->spiritball > 0) {
					duration = 1;
					target_id = 0; // Will target current auto-target instead
				}
			case CH_TIGERFIST:
				if (!duration && pc_checkskill(sd, CH_CHAINCRUSH) > 0 && sd->spiritball > 1) {
					duration = 1;
					target_id = 0; // Will target current auto-target instead
				}
			case CH_CHAINCRUSH:
				if (!duration && pc_checkskill(sd, MO_EXTREMITYFIST) > 0 && sd->spiritball > 0 && sd->sc.data[SC_EXPLOSIONSPIRITS]) {
					duration = 1;
					target_id = 0; // Will target current auto-target instead
				}
				break;
			case AC_DOUBLE:
				if (pc_checkskill(sd, HT_POWER)) {
						duration = 2000;
						nodelay = 1; //Neither gives walk nor attack delay
						target_id = 0; //Does not need to be used on previous target
				}
				break;
			case SR_DRAGONCOMBO:
				if (pc_checkskill(sd, SR_FALLENEMPIRE) > 0)
					duration = 1;
				break;
			case SR_FALLENEMPIRE:
				if (pc_checkskill(sd, SR_TIGERCANNON) > 0 || pc_checkskill(sd, SR_GATEOFHELL) > 0)
					duration = 1;
				break;
		}
	}
	else { //other
		switch (skill_id) {
			case MH_TINDER_BREAKER:
			case MH_CBC:
			case MH_SONIC_CRAW:
			case MH_SILVERVEIN_RUSH:
				if (hd->hom_spiritball > 0) duration = 2000;
				nodelay = 1;
				break;
			case MH_EQC:
			case MH_MIDNIGHT_FRENZY:
				if (hd->hom_spiritball >= 2) duration = 2000;
				nodelay = 1;
				break;
		}
	}
	
	if (duration) { //Possible to chain
		if (sd && duration == 1) duration = DIFF_TICK32(sd->ud.canact_tick, tick); //Auto calc duration
		duration = max(status_get_amotion(src), duration); //Never less than aMotion
		sc_start4(src, SC_COMBO, 100, skill_id, target_id, nodelay, 0, duration);
		clif_combo_delay(src, duration);
	}
}

/*
 * =========================================================================
 * Does a skill attack with the given properties.
 * src is the master behind the attack (player/mob/pet)
 * dsrc is the actual originator of the damage, can be the same as src, or a BL_SKILL
 * bl is the target to be attacked.
 * flag can hold a bunch of information:
 * flag&0xFFF is passed to the underlying battle_calc_attack for processing
 *      (usually holds number of targets, or just 1 for simple splash attacks)
	 flag&0xF000 - Values from enum e_skill_display
 *        flag&0x3F0000 - Values from enum e_battle_check_target
 *
 *        flag&0x1000000 - Return 0 if damage was reflected
 *-------------------------------------------------------------------------*/
int64 skill_attack (int attack_type, struct block_list* src, struct block_list *dsrc, struct block_list *bl, int skill_id, int skill_lv, int64 tick, int flag)
{
	struct Damage dmg;
	struct status_data *sstatus, *tstatus;
	struct status_change *sc, *ssc;
	struct map_session_data *sd, *tsd;
	int type=0;
	int64 damage;
	int8 rmdamage = 0;//magic reflected
	bool additional_effects = true;

	if(skill_id > 0 && skill_lv <= 0) return 0;

	nullpo_ret(src);	//Source is the master behind the attack (player/mob/pet)
	nullpo_ret(dsrc); //dsrc is the actual originator of the damage, can be the same as src, or a skill casted by src.
	nullpo_ret(bl); //Target to be attacked.

	if (status_bl_has_mode(bl, MD_SKILL_IMMUNE) || (status_get_class(bl) == MOBID_EMPERIUM ))
		return 0;

	if (src != dsrc) {
		//When caster is not the src of attack, this is a ground skill, and as such, do the relevant target checking. [Skotlex]
		if( !status_check_skilluse(battle_config.skill_caster_check?src:NULL, bl, skill_id, skill_lv, 2) )
			return 0;
	} else if ((flag&SD_ANIMATION) && skill_get_nk(skill_id)&NK_SPLASH) {
		//Note that splash attacks often only check versus the targetted mob, those around the splash area normally don't get checked for being hidden/cloaked/etc. [Skotlex]
		if( !status_check_skilluse(src, bl, skill_id, skill_lv, 2) )
			return 0;
	}

	sd = BL_CAST(BL_PC, src);
	tsd = BL_CAST(BL_PC, bl);

	sstatus = status_get_status_data(src);
	tstatus = status_get_status_data(bl);
	ssc = status_get_sc(src);
	sc= status_get_sc(bl);
	if (sc && !sc->count) sc = NULL; //Don't need it.

	 //Trick Dead protects you from damage, but not from buffs and the like, hence it's placed here.
	if (sc && sc->data[SC_TRICKDEAD])
		return 0;

	//When Gravitational Field is active, damage can only be dealt by Gravitational Field and Autospells
	if (ssc && ssc->data[SC_GRAVITATION] && ssc->data[SC_GRAVITATION]->val3 == BCT_SELF && skill_id != HW_GRAVITATION && !sd->state.autocast)
		return 0;

	dmg = battle_calc_attack(attack_type,src,bl,skill_id,skill_lv,flag&0xFFF);

	//If the damage source is a unit, the damage is not delayed
	if (src != dsrc && skill_id != GS_GROUNDDRIFT)
		dmg.amotion = 0;

	//Skotlex: Adjusted to the new system
	if(src->type==BL_PET)
	{ // [Valaris]
		struct pet_data *pd = (TBL_PET*)src;
		if (pd->a_skill && pd->a_skill->div_ && pd->a_skill->id == skill_id) { //petskillattack2
			if (!is_infinite_defense(bl, dmg.flag)) {
				int element = skill_get_ele(skill_id, skill_lv);
				/*if (skill_id == -1) Does it ever worked?
					element = sstatus->rhw.ele;*/
				if (element != ELE_NEUTRAL || !(battle_config.attack_attr_none&BL_PET))
					dmg.damage = battle_attr_fix(src, bl, pd->a_skill->damage, element, tstatus->def_ele, tstatus->ele_lv);
				else
					dmg.damage = pd->a_skill->damage; // Fixed damage

			}
			else
				dmg.damage = 1 * pd->a_skill->div_;
			dmg.damage2 = 0;
			dmg.div_= pd->a_skill->div_;
		}
	}

	if( dmg.flag&BF_MAGIC && ( skill_id != NPC_EARTHQUAKE || (battle_config.eq_single_target_reflectable && (flag&0xFFF) == 1) ) )
	{ // Earthquake on multiple targets is not counted as a target skill. [Inkfish]
		if( (dmg.damage || dmg.damage2) && (type = skill_magic_reflect(src, bl, src==dsrc)) )
		{	//Magic reflection, switch caster/target
			struct block_list *tbl = bl;
			rmdamage = 1;
			bl = src;
			src = tbl;
			dsrc = tbl;
			sd = BL_CAST(BL_PC, src);
			tsd = BL_CAST(BL_PC, bl);
			sc = status_get_sc(bl);
			if (sc && !sc->count)
				sc = NULL; //Don't need it.
			/* flag&2 disables double casting trigger */
			flag |= 2;

			//Reflected magic damage will not cause the caster to be knocked back [Playtester]
			flag |= 4;

			//Spirit of Wizard blocks Kaite's reflection
			if( type == 2 && sc && sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_WIZARD )
			{	//Consume one Fragment per hit of the casted skill? [Skotlex]
			  	type = tsd?pc_search_inventory (tsd, ITEMID_FRAGMENT_OF_CRYSTAL):0;
				if (type >= 0) {
					if ( tsd ) pc_delitem(tsd, type, 1, 0, 1, LOG_TYPE_CONSUME);
					dmg.damage = dmg.damage2 = 0;
					dmg.dmg_lv = ATK_MISS;
					sc->data[SC_SPIRIT]->val3 = skill_id;
					sc->data[SC_SPIRIT]->val4 = dsrc->id;
				}
			}
			else if (type != 2) /* Kaite bypasses */
				additional_effects = false;
		}

		if( sc )
		{
			if( sc->data[SC_MAGICROD] && src == dsrc)
			{
				int sp = skill_get_sp(skill_id,skill_lv);
				dmg.damage = dmg.damage2 = 0;
				dmg.dmg_lv = ATK_MISS; //This will prevent skill additional effect from taking effect. [Skotlex]
				sp = sp * sc->data[SC_MAGICROD]->val2 / 100;
				if(skill_id == WZ_WATERBALL && skill_lv > 1)
					sp = sp/((skill_lv|1)*(skill_lv|1)); //Estimate SP cost of a single water-ball
				status_heal(bl, 0, sp, 2);
			}
			if( (dmg.damage || dmg.damage2) && sc->data[SC_HALLUCINATIONWALK] && rnd()%100 < sc->data[SC_HALLUCINATIONWALK]->val3 )
			{
				dmg.damage = dmg.damage2 = 0;
				dmg.dmg_lv = ATK_MISS;
			}
		}
	}

	damage = dmg.damage + dmg.damage2;

	if( (skill_id == AL_INCAGI || skill_id == AL_BLESSING || 
		skill_id == CASH_BLESSING || skill_id == CASH_INCAGI ||
		skill_id == MER_INCAGI || skill_id == MER_BLESSING) && tsd->sc.data[SC_CHANGEUNDEAD] )
		damage = 1;

	//Skill hit type
	type=(skill_id==0)?5:skill_get_hit(skill_id);

	switch (skill_id) {
		case SC_TRIANGLESHOT:
			if (rnd() % 100 > (1 + skill_lv)) dmg.blewcount = 0;
			break;
		default:
			if (damage < dmg.div_ && skill_id != CH_PALMSTRIKE)
				dmg.blewcount = 0; //only pushback when it hit for other
			break;
	}

	switch (skill_id) {
		case CR_GRANDCROSS:
		case NPC_GRANDDARKNESS:
			if(battle_config.gx_disptype) dsrc = src;
			if(src == bl) type = 4;
			else flag|=SD_ANIMATION;
			break;
		case NJ_TATAMIGAESHI: //For correct knockback.
			dsrc = src;
			flag|=SD_ANIMATION;
			break;
		case TK_COUNTER: {	//bonus from SG_FRIEND [Komurka]
			int level;
			if (sd && sd->status.party_id > 0 && (level = pc_checkskill(sd, SG_FRIEND)))
				party_skill_check(sd, sd->status.party_id, TK_COUNTER, level);
			}
			break;
		case SL_STIN:
		case SL_STUN:
			if (skill_lv >= 7) {
				struct status_change *sc_cur = status_get_sc(src);
				if (sc_cur && !sc_cur->data[SC_SMA])
					sc_start(src, SC_SMA, 100, skill_lv, skill_get_time(SL_SMA, skill_lv));
			}
			break;
		case GS_FULLBUSTER:
			if (sd) //Can't attack nor use items until skill's delay expires. [Skotlex]
				sd->ud.attackabletime = sd->canuseitem_tick = sd->ud.canact_tick;
			break;
	}

	//combo handling
	skill_combo(src, dsrc, bl, skill_id, skill_lv, tick);

	//Display damage.
	switch( skill_id )
	{
	case PA_GOSPEL: //Should look like Holy Cross [Skotlex]
		dmg.dmotion = clif_skill_damage(src,bl,tick,dmg.amotion,dmg.dmotion, damage, dmg.div_, CR_HOLYCROSS, -1, 5);
		break;
	//Skills that need be passed as a normal attack for the client to display correctly.
	case HVAN_EXPLOSION:
	case NPC_SELFDESTRUCTION:
		if(src->type==BL_PC)
			dmg.blewcount = 10;
		dmg.amotion = 0; //Disable delay or attack will do no damage since source is dead by the time it takes effect. [Skotlex]
		// fall through
	case KN_AUTOCOUNTER:
	case NPC_CRITICALSLASH:
	case TF_DOUBLE:
	case GS_CHAINACTION:
		dmg.dmotion = clif_damage(src,bl,tick,dmg.amotion,dmg.dmotion,damage,dmg.div_,dmg.type,dmg.damage2, false);
		break;

	case AS_SPLASHER:
		if( flag&SD_ANIMATION ) // the surrounding targets
			dmg.dmotion = clif_skill_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, -1, 5); // needs -1 as skill level
		else // the central target doesn't display an animation
			dmg.dmotion = clif_skill_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, -2, 5); // needs -2(!) as skill level
		break;
#if PACKETVER < 20180620
	case RK_IGNITIONBREAK:
		dmg.dmotion = clif_skill_damage(dsrc,bl,tick,dmg.amotion,dmg.dmotion,damage,dmg.div_,NV_BASIC,-1,5);
		break;
#endif
	case WL_HELLINFERNO:
		if ( flag&4 )// Show animation for fire damage part.
			dmg.dmotion = clif_skill_damage(dsrc,bl,tick,dmg.amotion,dmg.dmotion,damage,dmg.div_,skill_id,skill_lv,type);
		else// Only display damage for the shadow hit.
			dmg.dmotion = clif_skill_damage(dsrc,bl,tick,dmg.amotion,dmg.dmotion,damage,dmg.div_,0,-2,5);
		break;
	case WL_COMET:
		dmg.dmotion = clif_skill_damage(src,bl,tick,dmg.amotion,dmg.dmotion,damage,dmg.div_,skill_id,skill_lv,8);
		break;
	case WL_CHAINLIGHTNING_ATK:
		dmg.dmotion = clif_skill_damage(src,bl,tick,dmg.amotion,dmg.dmotion,damage,1,WL_CHAINLIGHTNING,-2,6);
		break;
	case WL_TETRAVORTEX_FIRE:
	case WL_TETRAVORTEX_WATER:
	case WL_TETRAVORTEX_WIND:
	case WL_TETRAVORTEX_GROUND:
		clif_skill_nodamage(src, bl, skill_id, -2, 1);
		clif_skill_damage(src, bl, tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, -2, 5);
		break;
	case SC_FEINTBOMB:
		dmg.dmotion = clif_skill_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, -1, 5);
		break;
	case WM_REVERBERATION_MELEE:
	case WM_REVERBERATION_MAGIC:
	case WM_SEVERE_RAINSTORM_MELEE:
		dmg.dmotion = clif_skill_damage(dsrc,bl,tick,dmg.amotion,dmg.dmotion,damage,dmg.div_,skill_id,65534,6);
		break;
	case GN_SPORE_EXPLOSION:
		if( flag&SD_ANIMATION )// The surrounding targets show no animaion. Only damage.
			dmg.dmotion = clif_skill_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, 0, -2, 5);
		else// Display animation only on the source.
			dmg.dmotion = clif_skill_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, -2, 5);
		break;
	case GN_SLINGITEM_RANGEMELEEATK://Server sends a skill level of 65534 and type 6. Interesting.
		dmg.dmotion = clif_skill_damage(dsrc, bl, tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, 65534, 6);
		break;
	case RL_QD_SHOT:
	case SJ_NOVAEXPLOSING:
		dmg.dmotion = clif_skill_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, -2, 6);
		break;
	case RL_H_MINE:
		if ( flag&8 )// Client has a glitch where the status animation appears on hit targets, even by exploaded mines. We need this for now to fix it. [Rytech]
			dmg.dmotion = clif_skill_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, 0, -2, 5);
		else
			dmg.dmotion = clif_skill_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, skill_lv, type);
		break;
	case SJ_FALLINGSTAR_ATK:
	case SJ_FALLINGSTAR_ATK2:
	case KO_HUUMARANKA:
		dmg.dmotion = clif_skill_damage(src, bl, tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, -2, 8);
		break;
	// Skills listed here use type 5 for displaying single hit damage even if its not a splash skill unless its multi-hit.
	// This is because the skill animation is handled by the ZC_USE_SKILL instead, but only if damage is dealt.
	case SP_CURSEEXPLOSION:
	case SP_SPA:
	case SP_SHA:
	case SU_BITE:
	case SU_SCRATCH:
	case SU_SV_STEMSPEAR:
	case SU_SCAROFTAROU:
	case SU_PICKYPECK:
	case SU_LUNATICCARROTBEAT:
	case SU_LUNATICCARROTBEAT2:
	//case SU_SVG_SPIRIT:// Animation acturally works through damage packet.
		if( dmg.div_ < 2 )
			type = 5;// Multi-hit skills in database are usually set to 8 which is fine. If hit count is just 1, set to type 5.
		if ( !(flag&SD_ANIMATION) )// Make sure to display the animation only on what you targeted when dealing damage, including splash attacks.
			clif_skill_nodamage(dsrc, bl, skill_id, skill_lv, damage);
		// Since animations arn't handled by the damage packet, we can set the level to -2 to get rid of the flash effect
		// even tho its not official. It just looks and feels much better.
		dmg.dmotion = clif_skill_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, -2, type);
		break;
	case LG_OVERBRAND_BRANDISH:
	case LG_OVERBRAND_PLUSATK:
		dmg.dmotion = clif_skill_damage(src,bl,tick,dmg.amotion,dmg.dmotion,damage,dmg.div_,skill_id,-1,5);
		break;
	case EL_CIRCLE_OF_FIRE:
	case EL_STONE_RAIN:
		dmg.dmotion = clif_skill_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, 0, -2, 5);
		break;
	case HT_CLAYMORETRAP:
	case HT_BLASTMINE:
	case HT_FLASHER:
	case HT_FREEZINGTRAP:
	case RA_CLUSTERBOMB:
	case RA_FIRINGTRAP:
	case RA_ICEBOUNDTRAP:
		dmg.dmotion = clif_skill_damage(src, bl, tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, flag&SD_LEVEL ? -1 : skill_lv, 5);
		if (dsrc != src) // avoid damage display redundancy
			break;
		//Fall through
	case HT_LANDMINE:
		dmg.dmotion = clif_skill_damage(dsrc, bl, tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, -1, type);
		break;
	case WZ_SIGHTBLASTER:
		//Sightblaster should never call clif_skill_damage twice
		dmg.dmotion = clif_skill_damage(src, bl, tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, (flag&SD_LEVEL) ? -1 : skill_lv, 5);
		break;
	default:
		if( flag&SD_ANIMATION && dmg.div_ < 2 ) //Disabling skill animation doesn't works on multi-hit.
			type = 5;
		if (src->type == BL_SKILL) {
			TBL_SKILL *su = (TBL_SKILL*)src;
			if (su->group && skill_get_inf2(su->group->skill_id)&INF2_TRAP) { // show damage on trap targets
				clif_skill_damage(src, bl, tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, flag&SD_LEVEL ? -1 : skill_lv, 5);
				break;
			}
		}
		dmg.dmotion = clif_skill_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, damage, dmg.div_, skill_id, flag&SD_LEVEL?-1:skill_lv, type);
		break;
	}

	map_freeblock_lock();

	if (bl->type == BL_PC && skill_id && skill_get_index(skill_id) >= 0 && skill_db[skill_get_index(skill_id)].copyable.option && //Only copy skill that copyable [Cydh]
		dmg.flag&BF_SKILL && dmg.damage + dmg.damage2 > 0 && damage < status_get_hp(bl)) //Cannot copy skills if the blow will kill you. [Skotlex]
		skill_do_copy(src, bl, skill_id, skill_lv);

	if (dmg.dmg_lv >= ATK_MISS && (type = skill_get_walkdelay(skill_id, skill_lv)) > 0)
	{	//Skills with can't walk delay also stop normal attacking for that
		//duration when the attack connects. [Skotlex]
		struct unit_data *ud = unit_bl2ud(src);
		if (ud && DIFF_TICK(ud->attackabletime, tick + type) < 0)
			ud->attackabletime = tick + type;
	}

	if( !dmg.amotion )
	{ //Instant damage
		if (!sc || !(sc->data[SC_DEVOTION] || sc->data[SC_WATER_SCREEN_OPTION] || skill_id == HW_GRAVITATION || skill_id == NPC_EVILLAND))
			status_fix_damage(src,bl,damage,dmg.dmotion); //Deal damage before knockback to allow stuff like firewall+storm gust combo.
		if( !status_isdead(bl) && additional_effects )
			skill_additional_effect(src,bl,skill_id,skill_lv,dmg.flag,dmg.dmg_lv,tick);
		if( damage > 0 ) //Counter status effects [Skotlex]
			skill_counter_additional_effect(src,bl,skill_id,skill_lv,dmg.flag,tick);
	}

	//Only knockback if it's still alive, otherwise a "ghost" is left behind. [Skotlex]
	//Reflected spells do not bounce back (bl == dsrc since it only happens for direct skills)
	if (dmg.blewcount > 0 && bl!=dsrc && !status_isdead(bl))
	{
		int direction = -1; // default
		switch(skill_id) {
			case MG_FIREWALL:
			case GN_WALLOFTHORN:
			case EL_FIRE_MANTLE:
				direction = unit_getdir(bl); 
				break;
			// This ensures the storm randomly pushes instead of exactly a cell backwards per official mechanics.
			case WZ_STORMGUST:
				if (!battle_config.stormgust_knockback)
					direction = rnd()%8;
				break;
			case MC_CARTREVOLUTION:
				if (battle_config.cart_revo_knockback)
					direction = 6; // Official servers push target to the West
				break;
			case AC_SHOWER:
				// Direction between target to actual attacker location instead of the unit location
				if (!battle_config.arrow_shower_knockback && dsrc != src)
					direction = map_calc_dir(bl, src->x, src->y);
				else
					direction = map_calc_dir(bl, skill_area_temp[4], skill_area_temp[5]);
				break;
		}
		if ( skill_id == SR_KNUCKLEARROW )
		{
			// Main attack has no flag set which allows the knockback code to be processed.
			// If a flag is detected, then it means the knockback code was already ran.
			if ( !flag )
			{
				short x = bl->x, y = bl->y;

				// Ignore knockback damage bonus if in WOE (player cannot be knocked in WOE)
				// Boss & Immune Knockback (mode or from bonus bNoKnockBack) target still remains the damage bonus
				if (skill_blown(dsrc, bl, dmg.blewcount, direction, 0x04) < dmg.blewcount)
					skill_addtimerskill(src, tick + 300 * ((flag & 2) ? 1 : 2), bl->id, 0, 0, skill_id, skill_lv, BF_WEAPON, flag | 4);

				direction = -1;

				// Move attacker to the target position after knocked back
				if ((bl->x != x || bl->y != y) && skill_check_unit_movepos(5, src, bl->x, bl->y, 1, 1))
					clif_blown(src);
			}
		}
		if (skill_id == LG_OVERBRAND_BRANDISH)
		{
			if (skill_blown(dsrc, bl, dmg.blewcount, direction, 0x04|0x08|0x10|0x20))
			{
				short dir_x, dir_y;
				dir_x = dirx[(direction + 4)%8];
				dir_y = diry[(direction + 4)%8];
				if (map_getcell(bl->m, bl->x+dir_x, bl->y+dir_y, CELL_CHKNOPASS) != 0)
					skill_addtimerskill(src, tick + status_get_amotion(src), bl->id, 0, 0, LG_OVERBRAND_PLUSATK, skill_lv, BF_WEAPON, flag);
			}
			else
				skill_addtimerskill(src, tick + status_get_amotion(src), bl->id, 0, 0, LG_OVERBRAND_PLUSATK, skill_lv, BF_WEAPON, flag);
 		}
		else if (skill_id == RL_R_TRIP)
		{
			struct mob_data* tmd = BL_CAST(BL_MOB, bl);
			static int dx[] = { 0, 1, 0, -1, -1,  1, 1, -1 };
			static int dy[] = { -1, 0, 1,  0, -1, -1, 1,  1 };
			bool wall_damage = true;
			int i = 0;

			// Knock back the target first if possible before we do a wall check.
			skill_blown(dsrc, bl, dmg.blewcount, direction, 0);

			// Check if the target will receive wall damage.
			// Targets that can be knocked back will receive wall damage if pushed next to a wall.
			// Player's with anti-knockback and boss monsters will always receive wall damage.
			if (!((tsd && tsd->special_state.no_knockback) || (tmd && status_get_class_(bl) == CLASS_BOSS)))
			{// Is there a wall next to the target?
				ARR_FIND(0, 8, i, map_getcell(bl->m, bl->x + dx[i], bl->y + dy[i], CELL_CHKNOPASS) != 0);
				if (i == 8)// No wall detected.
					wall_damage = false;
			}

			if (wall_damage == true)// Deal wall damage if the above check detected a wall or the target has anti-knockback.
				skill_addtimerskill(src, tick + status_get_amotion(src), bl->id, 0, 0, RL_R_TRIP_PLUSATK, skill_lv, BF_WEAPON, flag);
		}
		else {
			skill_blown(dsrc, bl, dmg.blewcount, direction, 0);
			if (!dmg.blewcount && bl->type == BL_SKILL && damage > 0) {
				TBL_SKILL *su = (TBL_SKILL*)bl;
				if (su->group && su->group->skill_id == HT_BLASTMINE)
					skill_blown(src, bl, 3, -1, 0);
			}
		}
	}

	//Delayed damage must be dealt after the knockback (it needs to know actual position of target)
	if (dmg.amotion)
		battle_delay_damage(tick, dmg.amotion,src,bl,dmg.flag,skill_id,skill_lv,damage,dmg.dmg_lv,dmg.dmotion, additional_effects, false);

	if (sc && sc->data[SC_DEVOTION] && (skill_id != PA_PRESSURE && skill_id != HW_GRAVITATION && skill_id != NPC_EVILLAND && skill_id != SJ_NOVAEXPLOSING && skill_id != SP_SOULEXPLOSION))
	{
		struct status_change_entry *sce = sc->data[SC_DEVOTION];
		struct block_list *d_bl = map_id2bl(sce->val1);

		if( d_bl && (
			(d_bl->type == BL_MER && ((TBL_MER*)d_bl)->master && ((TBL_MER*)d_bl)->master->bl.id == bl->id) ||
			(d_bl->type == BL_PC && ((TBL_PC*)d_bl)->devotion[sce->val2] == bl->id)
			) && check_distance_bl(bl, d_bl, sce->val3) )
		{
			if (!rmdamage) {
				clif_damage(d_bl, d_bl, gettick(), 0, 0, damage, 0, 0, 0, false);
				status_fix_damage(NULL, d_bl, damage, 0);
			}
			else {
				bool isDevotRdamage = false;
				if (battle_config.devotion_rdamage && battle_config.devotion_rdamage > rnd() % 100)
					isDevotRdamage = true;
				// If !isDevotRdamage, reflected magics are done directly on the target not on paladin
				// This check is only for magical skill.
				// For BF_WEAPON skills types track var rdamage and function battle_calc_return_damage
				clif_damage(bl, (!isDevotRdamage) ? bl : d_bl, gettick(), 0, 0, damage, 0, DMG_NORMAL, 0, false);
				status_fix_damage(bl, (!isDevotRdamage) ? bl : d_bl, damage, 0);
			}
		}
		else
			status_change_end(bl, SC_DEVOTION, INVALID_TIMER);
	}

	if( sc && sc->data[SC_WATER_SCREEN_OPTION] && (skill_id != PA_PRESSURE && skill_id != SJ_NOVAEXPLOSING && skill_id != SP_SOULEXPLOSION) )
	{
		struct status_change_entry *sce = sc->data[SC_WATER_SCREEN_OPTION];
		struct block_list *d_bl = map_id2bl(sce->val1);

		if( d_bl && (d_bl->type == BL_ELEM && ((TBL_ELEM*)d_bl)->master && ((TBL_ELEM*)d_bl)->master->bl.id == bl->id) )
		{
			clif_damage(d_bl, d_bl, gettick(), 0, 0, damage, 0, 0, 0, false);
			status_fix_damage(NULL, d_bl, damage, 0);
		}
	}

	if (damage > 0 && !(tstatus->mode&MD_STATUS_IMMUNE))
	{
		if (skill_id == RG_INTIMIDATE && rnd()%100 < (50 + skill_lv * 5 + status_get_lv(src) - status_get_lv(bl)))
			skill_addtimerskill(src, tick + 800, bl->id, 0, 0, skill_id, skill_lv, 0, flag);
		else if (skill_id == SC_FATALMENACE)
			skill_addtimerskill(src, tick + 800, bl->id, skill_area_temp[4], skill_area_temp[5], skill_id, skill_lv, 0, flag);
	}

	if ( skill_id == WM_METALICSOUND && damage > 0 )
	{
		int64 sp_damage = damage / 10 / (11 - (sd?pc_checkskill(sd,WM_LESSON):10)) * battle_config.metallicsound_spburn_rate / 100;

		clif_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, sp_damage, 1, 0, 0, true);
		status_zap(bl, 0, sp_damage);
	}

	if ( skill_id == SR_TIGERCANNON && damage > 0 )
	{
		int64 sp_damage = damage * 10 / 100;
		clif_damage(dsrc,bl,tick, dmg.amotion, dmg.dmotion, sp_damage, 1, 0, 0, true);
		status_zap(bl, 0, sp_damage);
	}

	if(skill_id == CR_GRANDCROSS || skill_id == NPC_GRANDDARKNESS)
		dmg.flag |= BF_WEAPON;

	if (sd && src != bl && damage > 0 && (dmg.flag&BF_WEAPON || (dmg.flag&BF_MISC && (skill_id == RA_CLUSTERBOMB || skill_id == RA_FIRINGTRAP || skill_id == RA_ICEBOUNDTRAP || skill_id == RK_DRAGONBREATH))))
	{
		if (battle_config.left_cardfix_to_right)
			battle_drain(sd, bl, dmg.damage, dmg.damage, tstatus->race, tstatus->class_);
		else
			battle_drain(sd, bl, dmg.damage, dmg.damage2, tstatus->race, tstatus->class_);
	}

	if( damage > 0){ 
		switch (skill_id) {
			case RK_CRUSHSTRIKE: // Your weapon will not be broken if you miss.
				skill_break_equip(src, EQP_WEAPON, 2000, BCT_SELF);
				break;
			case GC_VENOMPRESSURE: {
				struct status_change *ssc = status_get_sc(src);
				short rate = 100;
				if (ssc && ssc->data[SC_POISONINGWEAPON] && rnd() % 100 < 70 + 5 * skill_lv) {
					if (ssc->data[SC_POISONINGWEAPON]->val1 == 9)//Oblivion Curse gives a 2nd success chance after the 1st one passes which is reduceable. [Rytech]
						rate = 100 - tstatus->int_ * 4 / 5;
					sc_start(bl, ssc->data[SC_POISONINGWEAPON]->val2, rate, ssc->data[SC_POISONINGWEAPON]->val1, skill_get_time2(GC_POISONINGWEAPON, 1) - (tstatus->vit + tstatus->luk) / 2 * 1000);
					
					status_change_end(src, SC_POISONINGWEAPON, -1);
					clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
				}
			}
		}
		if (sd)
			skill_onskillusage(sd, bl, skill_id, tick);
	}

	if (!(flag & 2))
	{
		if ((skill_id == MG_COLDBOLT || skill_id == MG_FIREBOLT || skill_id == MG_LIGHTNINGBOLT) &&
			(sc = status_get_sc(src)) && sc->data[SC_DOUBLECAST] && rnd()%100 < sc->data[SC_DOUBLECAST]->val2)
		{
			//skill_addtimerskill(src, tick + dmg.div_*dmg.amotion, bl->id, 0, 0, skill_id, skill_lv, BF_MAGIC, flag|2);
			skill_addtimerskill(src, tick + dmg.amotion, bl->id, 0, 0, skill_id, skill_lv, BF_MAGIC, flag|2);
		}

		if ((skill_id == SU_BITE || skill_id == SU_SCRATCH || skill_id == SU_SV_STEMSPEAR || skill_id == SU_SCAROFTAROU || skill_id == SU_PICKYPECK) && 
			rnd()%100 < 10 * (status_get_base_lv_effect(src) / 30))
		{// There's a 1000ms + amotion delay after the skill does its thing before the double cast effect happens.
			skill_addtimerskill(src, tick + dmg.amotion + 1000, bl->id, 0, 0, skill_id, skill_lv, skill_get_type(skill_id), flag|2);
		}
	}

	map_freeblock_unlock();

	if ((flag & 0x1000000) && rmdamage == 1)
		return 0; //Should return 0 when damage was reflected

	return damage;
}

/*==========================================
 * sub fonction for recursive skill call.
 * Checking bl battle flag and display dammage
 * then call func with source,target,skill_id,skill_lv,tick,flag
 *------------------------------------------*/
typedef int (*SkillFunc)(struct block_list *, struct block_list *, int, int, int64, int);
int skill_area_sub (struct block_list *bl, va_list ap)
{
	struct block_list *src;
	int skill_id,skill_lv,flag;
	int64 tick;
	SkillFunc func;

	nullpo_ret(bl);

	src = va_arg(ap, struct block_list *);
	skill_id = va_arg(ap, int);
	skill_lv = va_arg(ap, int);
	tick = va_arg(ap, int64);
	flag = va_arg(ap, int);
	func = va_arg(ap, SkillFunc);

	if(battle_check_target(src,bl,flag) > 0)
	{
		// several splash skills need this initial dummy packet to display correctly
		if (flag&SD_PREAMBLE && skill_area_temp[2] == 0)
			clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);

		if (flag&(SD_SPLASH|SD_PREAMBLE))
			skill_area_temp[2]++;

		return func(src,bl,skill_id,skill_lv,tick,flag);
	}
	return 0;
}

static int skill_check_unit_range_sub (struct block_list *bl, va_list ap)
{
	struct skill_unit *unit;
	int skill_id,g_skillid;

	unit = (struct skill_unit *)bl;

	if(bl->prev == NULL || bl->type != BL_SKILL)
		return 0;

	if(!unit->alive)
		return 0;

	skill_id = va_arg(ap,int);
	g_skillid = unit->group->skill_id;

	switch (skill_id)
	{
	case AL_PNEUMA: //Pneuma doesn't work even if just one cell overlaps with Land Protector
		if (g_skillid == SA_LANDPROTECTOR)
			break;
		//Fall through
		case MG_SAFETYWALL:
		case SC_MAELSTROM:// Shouldn't be here.
		case MH_STEINWAND:
			if (g_skillid != MG_SAFETYWALL && g_skillid != AL_PNEUMA && g_skillid != SC_MAELSTROM && g_skillid != MH_STEINWAND)
				return 0;
			break;
		case AL_WARP:
		case HT_SKIDTRAP:
		case MA_SKIDTRAP:
		case HT_LANDMINE:
		case MA_LANDMINE:
		case HT_ANKLESNARE:
		case HT_SHOCKWAVE:
		case HT_SANDMAN:
		case MA_SANDMAN:
		case HT_FLASHER:
		case HT_FREEZINGTRAP:
		case MA_FREEZINGTRAP:
		case HT_BLASTMINE:
		case HT_CLAYMORETRAP:
		case HT_TALKIEBOX:
		case HP_BASILICA:
		case RA_ELECTRICSHOCKER:
		case RA_CLUSTERBOMB:
		case RA_MAGENTATRAP:
		case RA_COBALTTRAP:
		case RA_MAIZETRAP:
		case RA_VERDURETRAP:
		case RA_FIRINGTRAP:
		case RA_ICEBOUNDTRAP:
		case SC_DIMENSIONDOOR:
		case WM_REVERBERATION:
		case WM_POEMOFNETHERWORLD:
		case GN_THORNS_TRAP:
		case GN_HELLS_PLANT:
			//Non stackable on themselves and traps (including venom dust which does not has the trap inf2 set)
			if (skill_id != g_skillid && !(skill_get_inf2(g_skillid)&INF2_TRAP) && g_skillid != AS_VENOMDUST && g_skillid != MH_POISON_MIST)
				return 0;
			break;
		default: //Avoid stacking with same kind of trap. [Skotlex]
			if (g_skillid != skill_id)
				return 0;
			break;
	}

	return 1;
}

static int skill_check_unit_range (struct block_list *bl, int x, int y, int skill_id, int skill_lv)
{
	//Non players do not check for the skill's splash-trigger area.
	int range = bl->type==BL_PC?skill_get_unit_range(skill_id, skill_lv):0;
	int layout_type = skill_get_unit_layout_type(skill_id,skill_lv);
	if (layout_type==-1 || layout_type>MAX_SQUARE_LAYOUT) {
		ShowError("skill_check_unit_range: unsupported layout type %d for skill %d\n",layout_type,skill_id);
		return 0;
	}

	range += layout_type;
	return map_foreachinarea(skill_check_unit_range_sub,bl->m,x-range,y-range,x+range,y+range,BL_SKILL,skill_id);
}

static int skill_check_unit_range2_sub (struct block_list *bl, va_list ap)
{
	int skill_id;

	if(bl->prev == NULL)
		return 0;

	skill_id = va_arg(ap,int);

	if( status_isdead(bl) && skill_id != AL_WARP )
		return 0;

	if( skill_id == HP_BASILICA && bl->type == BL_PC )
		return 0;

	if( skill_id == AM_DEMONSTRATION && bl->type == BL_MOB && ((TBL_MOB*)bl)->mob_id == MOBID_EMPERIUM )
		return 0; //Allow casting Bomb/Demonstration Right under emperium [Skotlex]
	return 1;
}

static int skill_check_unit_range2 (struct block_list *bl, int x, int y, int skill_id, int skill_lv)
{
	int range, type;

	switch (skill_id) {	// to be expanded later
	case WZ_ICEWALL:
		range = 2;
		break;
	default:
		{
			int layout_type = skill_get_unit_layout_type(skill_id,skill_lv);
			if (layout_type==-1 || layout_type>MAX_SQUARE_LAYOUT) {
				ShowError("skill_check_unit_range2: unsupported layout type %d for skill %d\n",layout_type,skill_id);
				return 0;
			}
			range = skill_get_unit_range(skill_id,skill_lv) + layout_type;
		}
		break;
	}

	// if the caster is a monster/NPC, only check for players
	// otherwise just check characters
	if (bl->type == BL_PC)
		type = BL_CHAR;
	else
		type = BL_PC;

	return map_foreachinarea(skill_check_unit_range2_sub, bl->m,
		x - range, y - range, x + range, y + range,
		type, skill_id);
}

static int skill_check_condition_mob_master_mer_sub (struct block_list *bl, va_list ap)
{
	int *c,src_id,mob_class,skill;
	struct mob_data *md;

	md=(struct mob_data*)bl;
	src_id=va_arg(ap,int);
	mob_class=va_arg(ap,int);
	skill=va_arg(ap,int);
	c=va_arg(ap,int *);

	if( md->master_id != src_id || md->special_state.ai != 3)
		return 0;

	if( md->mob_id == mob_class )
		(*c)++;

	return 1;
}

/*==========================================
 * Checks that you have the requirements for casting a skill for homunculus/mercenary.
 * Flag:
 * &1: finished casting the skill (invoke hp/sp/item consumption)
 * &2: picked menu entry (Warp Portal, Teleport and other menu based skills)
 *------------------------------------------*/
static int skill_check_condition_mercenary(struct block_list *bl, int skill, int lv, int type)
{
	struct status_data *status;
	struct status_change *sc;
	struct map_session_data *sd = NULL;
	int i, j, hp, sp, hp_rate, sp_rate, state, mhp, spiritball;
	t_itemid itemid[MAX_SKILL_ITEM_REQUIRE];
	int amount[ARRAYLENGTH(itemid)], index[ARRAYLENGTH(itemid)];

	if( lv < 1 || lv > MAX_SKILL_LEVEL )
		return 0;
	nullpo_ret(bl);

	switch( bl->type )
	{
		case BL_HOM: sd = ((TBL_HOM*)bl)->master; break;
		case BL_MER: sd = ((TBL_MER*)bl)->master; break;
		case BL_ELEM: sd = ((TBL_ELEM*)bl)->master; break;
	}

	status = status_get_status_data(bl);
	sc = status_get_sc(bl);

	if( sc && !sc->count )
		sc = NULL;

	if( (j = skill_get_index(skill)) == 0 )
		return 0;

	// Requeriments
	for( i = 0; i < ARRAYLENGTH(itemid); i++ )
	{
		itemid[i] = skill_db[j].require.itemid[i];
		amount[i] = skill_db[j].require.amount[i];
	}
	hp = skill_db[j].require.hp[lv-1];
	sp = skill_db[j].require.sp[lv-1];
	hp_rate = skill_db[j].require.hp_rate[lv-1];
	sp_rate = skill_db[j].require.sp_rate[lv-1];
	state = skill_db[j].require.state;
	spiritball = skill_db[j].require.spiritball[lv - 1];
	if( (mhp = skill_db[j].require.mhp[lv-1]) > 0 )
		hp += (status->max_hp * mhp) / 100;
	if( hp_rate > 0 )
		hp += (status->hp * hp_rate) / 100;
	else
		hp += (status->max_hp * (-hp_rate)) / 100;
	if( sp_rate > 0 )
		sp += (status->sp * sp_rate) / 100;
	else
		sp += (status->max_sp * (-sp_rate)) / 100;

	if( bl->type == BL_HOM )
	{ // Intimacy Requeriments
		struct homun_data *hd = BL_CAST(BL_HOM, bl);
		switch( skill )
		{
			case HFLI_SBR44:
				if( hd->homunculus.intimacy <= 200 )
					return 0;
				break;
			case HVAN_EXPLOSION:
				if( hd->homunculus.intimacy < (unsigned int)battle_config.hvan_explosion_intimate )
					return 0;
				break;
			case MH_SUMMON_LEGION:
				{
					unsigned char count = 0;
					short mobid = 0;

					// Check to see if any Hornet's, Giant Hornet's, or Luciola Vespa's are currently summoned.
					for ( mobid=MOBID_S_HORNET; mobid<=MOBID_S_LUCIOLA_VESPA; mobid++ )
						map_foreachinmap(skill_check_condition_mob_master_mer_sub ,hd->bl.m, BL_MOB, hd->bl.id, mobid, skill, &count);

					// If any of the above 3 summons are found, fail the skill.
					if (count > 0)
					{
						clif_skill_fail(sd,skill,0,0,0);
						return 0;
					}
					break;
				}
			case MH_LIGHT_OF_REGENE:
				// Zone data shows it needs a intimacy 901.
				// But description says it needs to be loyal, which would requie 911.
				// Best to set it to loyal requirements to be accurate to the description.
				if( hd->homunculus.intimacy <= 91000 )
					return 0;
				break;
			case MH_SONIC_CRAW:
				// Requires at least 1 spirit sphere even
				// tho it doesn't take any on skill use.
				if ( hd->hom_spiritball < 1 )
				{
					clif_skill_fail(sd,skill,USESKILL_FAIL_SPIRITS,1,0);
					return 0;
				}
				else
					hd->hom_spiritball_old = hd->hom_spiritball;
				break;
			case MH_SILVERVEIN_RUSH:
				if(!(sc && sc->data[SC_SONIC_CLAW_POSTDELAY]))
				{
					clif_skill_fail(sd,skill,USESKILL_FAIL_COMBOSKILL,MH_SONIC_CRAW,0);
					return 0;
				}
				break;
			case MH_MIDNIGHT_FRENZY:
				if(!(sc && sc->data[SC_SILVERVEIN_RUSH_POSTDELAY]))
				{
					clif_skill_fail(sd,skill,USESKILL_FAIL_COMBOSKILL,MH_SILVERVEIN_RUSH,0);
					return 0;
				}
				break;
			case MH_CBC:
				if(!(sc && sc->data[SC_TINDER_BREAKER_POSTDELAY]))
				{
					clif_skill_fail(sd,skill,USESKILL_FAIL_COMBOSKILL,MH_TINDER_BREAKER,0);
					return 0;
				}
				break;
			case MH_EQC:
				if(!(sc && sc->data[SC_CBC_POSTDELAY]))
				{
					clif_skill_fail(sd,skill,USESKILL_FAIL_COMBOSKILL,MH_CBC,0);
					return 0;
				}
				break;
		}

		// Homunculus Status Checks
		switch ( state )
		{
			case ST_FIGHTER:
				if( !(sc && sc->data[SC_STYLE_CHANGE] && sc->data[SC_STYLE_CHANGE]->val1 == FIGHTER_STYLE) )
				{
					clif_skill_fail(sd,skill,USESKILL_FAIL_STYLE_CHANGE_FIGHTER,0,0);
					return 0;
				}
				break;
			case ST_GRAPPLER:
				if( !(sc && sc->data[SC_STYLE_CHANGE] && sc->data[SC_STYLE_CHANGE]->val1 == GRAPPLER_STYLE) )
				{
					clif_skill_fail(sd,skill,USESKILL_FAIL_STYLE_CHANGE_GRAPPLER,0,0);
					return 0;
				}
				break;
		}

		// Homunculus Spirit Sphere's Check
		if ( spiritball > 0 )
		{
			if ( hd->hom_spiritball < spiritball )
			{
				clif_skill_fail(sd,skill,USESKILL_FAIL_SPIRITS,spiritball,0);
				return 0;
			}
			else
			{
				hd->hom_spiritball_old = hd->hom_spiritball;
				merc_hom_delspiritball(hd,spiritball);
			}
		}
	}

	if( !(type&2) )
	{
		if( hp > 0 && status->hp <= (unsigned int)hp )
		{
			clif_skill_fail(sd, skill, USESKILL_FAIL_HP_INSUFFICIENT, 0,0);
			return 0;
		}
		if( sp > 0 && status->sp <= (unsigned int)sp )
		{
			clif_skill_fail(sd, skill, USESKILL_FAIL_SP_INSUFFICIENT, 0,0);
			return 0;
		}
	}

	if( !type )
		switch( state )
		{
			case ST_MOVE_ENABLE:
				if( !unit_can_move(bl) )
				{
					clif_skill_fail(sd, skill, USESKILL_FAIL_LEVEL, 0,0);
					return 0;
				}
				break;
		}
	if( !(type&1) )
		return 1;

	// Check item existences
	for( i = 0; i < ARRAYLENGTH(itemid); i++ )
	{
		index[i] = -1;
		if( itemid[i] == 0 ) continue; // No item
		index[i] = pc_search_inventory(sd, itemid[i]);
		if( index[i] < 0 || sd->inventory.u.items_inventory[index[i]].amount < amount[i] )
		{
			clif_skill_fail(sd, skill, USESKILL_FAIL_LEVEL, 0,0);
			return 0;
		}
	}

	// Consume items
	for( i = 0; i < ARRAYLENGTH(itemid); i++ )
	{
		if( index[i] >= 0 ) pc_delitem(sd, index[i], amount[i], 0, 1, LOG_TYPE_CONSUME);
	}

	if( type&2 )
		return 1;

	if( sp || hp )
		status_zap(bl, hp, sp);

	return 1;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_area_sub_count (struct block_list *src, struct block_list *target, int skill_id, int skill_lv, int64 tick, int flag)
{
	return 1;
}

/*==========================================
 *
 *------------------------------------------*/
static int skill_timerskill(int tid, int64 tick, int id, intptr_t data)
{
	struct block_list *src = map_id2bl(id),*target;
	struct unit_data *ud = unit_bl2ud(src);
	struct skill_timerskill *skl = NULL;
	struct skill_unit *unit = NULL;
	bool flag = true;
	int range;

	nullpo_ret(src);
	nullpo_ret(ud);
	skl = ud->skilltimerskill[data];
	nullpo_ret(skl);
	ud->skilltimerskill[data] = NULL;

	do
	{
		if( src->prev == NULL )
			break; // Source not on Map
		if( skl->target_id )
		{
			target = map_id2bl(skl->target_id);
			if( (skl->skill_id == RG_INTIMIDATE || skl->skill_id == SC_FATALMENACE) && (!target || target->prev == NULL) )
				target = src; //Required since it has to warp.
			if( target == NULL )
				break; // Target offline?
			if( target->prev == NULL )
				break; // Target not on Map
			if( src->m != target->m )
				break; // Different Maps
			if( status_isdead(src) )
				break; // Caster is Dead
			if( status_isdead(target) && skl->skill_id != RG_INTIMIDATE && skl->skill_id != WZ_WATERBALL )
				break; // Target Killed
			flag = false;

			switch( skl->skill_id )
			{
				case KN_AUTOCOUNTER:
					clif_skill_nodamage(src, target, skl->skill_id, skl->skill_lv, 1);
					break;
				case RG_INTIMIDATE:
					if (unit_warp(src,-1,-1,-1,CLR_TELEPORT) == 0) {
						short x,y;
						map_search_freecell(src, 0, &x, &y, 1, 1, 0);
						if (target != src && !status_isdead(target))
							unit_warp(target, -1, x, y, CLR_TELEPORT);
					}
					break;
				case BA_FROSTJOKER:
				case DC_SCREAM:
					range = skill_get_splash(skl->skill_id, skl->skill_lv);
					map_foreachinarea(skill_frostjoke_scream,skl->map,skl->x-range,skl->y-range,skl->x+range,skl->y+range,BL_CHAR,src,skl->skill_id,skl->skill_lv,tick);
					break;
				case PR_LEXDIVINA:
					if (src->type == BL_MOB) {
						// Monsters use the default duration when casting Lex Divina
						status_change_start(src, target, status_skill2sc(skl->skill_id), skl->type*100, skl->skill_lv, 0, 0, 0, skill_get_time2(status_sc2skill(status_skill2sc(skl->skill_id)), 1), 0);
						break;
					}
					// Fall through
				case PR_STRECOVERY:
				case BS_HAMMERFALL:
					status_change_start(src, target, status_skill2sc(skl->skill_id), skl->type*100, skl->skill_lv, 0, 0, 0, skill_get_time2(skl->skill_id, skl->skill_lv), 0);
					break;
				case NPC_EARTHQUAKE:
					if (skl->type > 1)
						skill_addtimerskill(src, tick + 250, src->id, 0, 0, skl->skill_id, skl->skill_lv, skl->type - 1, skl->flag);
					skill_area_temp[0] = map_foreachinrange(skill_area_sub, src, skill_get_splash(skl->skill_id, skl->skill_lv), BL_CHAR, src, skl->skill_id, skl->skill_lv, tick, BCT_ENEMY, skill_area_sub_count);
					skill_area_temp[1] = src->id;
					skill_area_temp[2] = 0;
					map_foreachinrange(skill_area_sub, src, skill_get_splash(skl->skill_id, skl->skill_lv), splash_target(src), src, skl->skill_id, skl->skill_lv, tick, skl->flag, skill_castend_damage_id);
					break;
				case WZ_WATERBALL:
				{
					//Get the next waterball cell to consume
					struct s_skill_unit_layout *layout;
					layout = skill_get_unit_layout(skl->skill_id, skl->skill_lv, src, skl->x, skl->y, -1);
					for (int i = skl->type; i >= 0 && i < layout->count; i++) {
						int ux = skl->x + layout->dx[i];
						int uy = skl->y + layout->dy[i];
						unit = map_find_skill_unit_oncell(src, ux, uy, WZ_WATERBALL, NULL, 0);
						if (unit)
							break;
					}
				}	// Fall through
				case WZ_JUPITEL:
					// Official behaviour is to hit as long as there is a line of sight, regardless of distance
					if (skl->type > 0 && !status_isdead(target) && path_search_long(NULL, src->m, src->x, src->y, target->x, target->y, CELL_CHKWALL)) {
						// Apply canact delay here to prevent hacks (unlimited casting)
						ud->canact_tick = max(tick + status_get_amotion(src), ud->canact_tick);
						skill_attack(BF_MAGIC, src, src, target, skl->skill_id, skl->skill_lv, tick, skl->flag);
					}
					if (unit && !status_isdead(target) && !status_isdead(src)) {
						if (skl->type > 0)
							skill_toggle_magicpower(src, skl->skill_id); // Only the first hit will be amplified
						skill_delunit(unit); // Consume unit for next waterball
						//Timer will continue and walkdelay set until target is dead, even if there is currently no line of sight
						unit_set_walkdelay(src, tick, TIMERSKILL_INTERVAL, 1);
						skill_addtimerskill(src, tick + TIMERSKILL_INTERVAL, target->id, skl->x, skl->y, skl->skill_id, skl->skill_lv, skl->type + 1, skl->flag);
					} else {
						struct status_change *sc = status_get_sc(src);
						if(sc) {
							if( sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_WIZARD && sc->data[SC_SPIRIT]->val3 == skl->skill_id )
								sc->data[SC_SPIRIT]->val3 = 0; //Clear bounced spell check.
						}
					}
					break;
				case WL_HELLINFERNO:
					{
						struct status_change *sc = status_get_sc(src);
						if( !status_isdead(target) )
							skill_attack(skl->type,src,src,target,skl->skill_id,skl->skill_lv,tick,skl->flag);
					}
					break;
				case WL_CHAINLIGHTNING_ATK:
					{
						skill_toggle_magicpower(src, skl->skill_id); // Only the first hit will be amplified
						struct block_list *nbl = NULL; // Next Target of Chain
						skill_attack(BF_MAGIC,src,src,target,skl->skill_id,skl->skill_lv,tick,skl->flag); // Hit a Lightning on the current Target
						if( skl->type > 1 )
						{ // Remaining Chains Hit
							nbl = battle_getenemyarea(src,target->x,target->y,2,BL_CHAR|BL_SKILL,target->id); // Search for a new Target around current one...
							if( nbl == NULL && skl->x > 1 )
							{
								nbl = target;
								skl->x--;
							}
							else skl->x = 3;
						}

						if( nbl )
							skill_addtimerskill(src, tick + 650, nbl->id, skl->x, 0, WL_CHAINLIGHTNING_ATK, skl->skill_lv, skl->type - 1, skl->flag);
					}
 					break;
				case WL_TETRAVORTEX_FIRE:
				case WL_TETRAVORTEX_WATER:
				case WL_TETRAVORTEX_WIND:
				case WL_TETRAVORTEX_GROUND:
					skill_attack(BF_MAGIC,src,src,target,skl->skill_id,skl->skill_lv,tick,skl->flag);
					if( skl->type >= 3 )
					{ // Final Hit
						if( !status_isdead(target) )
						{ // Final Status Effect
							int effects[4] = { SC_BURNING, SC_FROST, SC_BLEEDING, SC_STUN },
								applyeffects[4] = { 0, 0, 0, 0 },
								i, j = 0, k = 0;
							for( i = 1; i <= 8; i = i + i )
							{
								if( skl->x&i )
								{
									applyeffects[j] = effects[k];
									j++;
								}
								k++;
							}
							if( j )
							{
								int duration = 0;

								i = applyeffects[rnd()%j];
								if ( i == SC_BURNING )
									duration = 20000;
								if ( i == SC_FROST )
									duration = 40000;
								if ( i == SC_BLEEDING )
									duration = 120000;
								if ( i == SC_STUN )
									duration = 5000;

								sc_start(target, i, 100, skl->skill_lv, duration);
							}
						}
					}
					break;
				case SC_FATALMENACE:
					if (src == target) // Casters Part
						unit_warp(src, -1, skl->x, skl->y, CLR_TELEPORT);
					else
					{ // Target's Part
						short x = skl->x, y = skl->y;
						map_search_freecell(NULL, target->m, &x, &y, 2, 2, 1);
						unit_warp(target, -1, x, y, CLR_TELEPORT);
					}
					break;
				case LG_MOONSLASHER:
				case SR_WINDMILL:
					if (target->type == BL_PC)
					{
						struct map_session_data *tsd = BL_CAST(BL_PC, target);
						if (tsd && !pc_issit(tsd))
						{
							pc_setsit(tsd);
							skill_sit(tsd, 1);
							clif_sitting(&tsd->bl, true);
						}
					}
					break;
				case LG_OVERBRAND_BRANDISH:
				case LG_OVERBRAND_PLUSATK:
				//case SR_KNUCKLEARROW://Shouldnt be needed since its set as a weapon attack in another part of the source. Will disable for now. [Rytech]
					skill_attack(BF_WEAPON, src, src, target, skl->skill_id, skl->skill_lv, tick, skl->flag|SD_LEVEL);
					break;
				case CH_PALMSTRIKE:
				{
					struct status_change* tsc = status_get_sc(target);
					struct status_change* sc = status_get_sc(src);
					if ((tsc && tsc->option&OPTION_HIDE) ||
						(sc && sc->option&OPTION_HIDE)) {
						skill_blown(src, target, skill_get_blewcount(skl->skill_id, skl->skill_lv), -1, 0x0);
						break;
					}
					skill_attack(skl->type, src, src, target, skl->skill_id, skl->skill_lv, tick, skl->flag);
					break;
				}
				default:
					skill_attack(skl->type,src,src,target,skl->skill_id,skl->skill_lv,tick,skl->flag);
					break;
			}
		}
		else {
			if(src->m != skl->map)
				break;
			switch( skl->skill_id ){
				case WZ_METEOR:
				case SU_CN_METEOR:
				case SU_CN_METEOR2:
					if( skl->type >= 0 ){
						int x = skl->type>>16, y = skl->type&0xFFFF;
						if( path_search_long(NULL, src->m, src->x, src->y, x, y, CELL_CHKWALL) )
							skill_unitsetting(src,skl->skill_id,skl->skill_lv,x,y,skl->flag);
						if (path_search_long(NULL, src->m, src->x, src->y, skl->x, skl->y, CELL_CHKWALL)
							&& !map_getcell(src->m, skl->x, skl->y, CELL_CHKLANDPROTECTOR))
							clif_skill_poseffect(src,skl->skill_id,skl->skill_lv,skl->x,skl->y,tick);
					}
					else if( path_search_long(NULL, src->m, src->x, src->y, skl->x, skl->y, CELL_CHKWALL) )
						skill_unitsetting(src,skl->skill_id,skl->skill_lv,skl->x,skl->y,skl->flag);
					break;
				case WL_EARTHSTRAIN:
				case RL_FIRE_RAIN:
					// skl->type = original direction, to avoid change it if caster walks in the waves progress.
					skill_unitsetting(src, skl->skill_id, skl->skill_lv, skl->x, skl->y, (skl->type<<16)|skl->flag);
					break;
				case GN_CRAZYWEED:
					if( skl->type >= 0 )
					{
						int x = skl->type>>16, y = skl->type&0xFFFF;
						if( path_search_long(NULL, src->m, src->x, src->y, skl->x, skl->y, CELL_CHKWALL) )
							skill_castend_pos2(src, x, y, GN_CRAZYWEED_ATK, skl->skill_lv, tick, flag);
					}
					else if( path_search_long(NULL, src->m, src->x, src->y, skl->x, skl->y, CELL_CHKWALL) )
						skill_castend_pos2(src, skl->x, skl->y, GN_CRAZYWEED_ATK, skl->skill_lv, tick, flag);
					break;
			}
		}
	} while (0);

	//Free skl now that it is no longer needed.
	ers_free(skill_timer_ers, skl);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_addtimerskill (struct block_list *src, int64 tick, int target, int x,int y, int skill_id, int skill_lv, int type, int flag)
{
	int i;
	struct unit_data *ud;
	nullpo_retr(1, src);

	if (src->prev == NULL)
		return 0;

	ud = unit_bl2ud(src);
	nullpo_retr(1, ud);

	ARR_FIND( 0, MAX_SKILLTIMERSKILL, i, ud->skilltimerskill[i] == 0 );
	if( i == MAX_SKILLTIMERSKILL ) return 1;

	ud->skilltimerskill[i] = ers_alloc(skill_timer_ers, struct skill_timerskill);
	ud->skilltimerskill[i]->timer = add_timer(tick, skill_timerskill, src->id, i);
	ud->skilltimerskill[i]->src_id = src->id;
	ud->skilltimerskill[i]->target_id = target;
	ud->skilltimerskill[i]->skill_id = skill_id;
	ud->skilltimerskill[i]->skill_lv = skill_lv;
	ud->skilltimerskill[i]->map = src->m;
	ud->skilltimerskill[i]->x = x;
	ud->skilltimerskill[i]->y = y;
	ud->skilltimerskill[i]->type = type;
	ud->skilltimerskill[i]->flag = flag;
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_cleartimerskill (struct block_list *src)
{
	int i;
	struct unit_data *ud;
	nullpo_ret(src);
	ud = unit_bl2ud(src);
	nullpo_ret(ud);

	for(i=0;i<MAX_SKILLTIMERSKILL;i++) {
		if(ud->skilltimerskill[i]) {
			delete_timer(ud->skilltimerskill[i]->timer, skill_timerskill);
			ers_free(skill_timer_ers, ud->skilltimerskill[i]);
			ud->skilltimerskill[i]=NULL;
		}
	}
	return 1;
}

static int skill_reveal_trap (struct block_list *bl, va_list ap)
{
	TBL_SKILL *su = (TBL_SKILL*)bl;
	if (su->alive && su->group && skill_get_inf2(su->group->skill_id)&INF2_TRAP)
	{	//Reveal trap.
		//Change look is not good enough, the client ignores it as an actual trap still. [Skotlex]
		//clif_changetraplook(bl, su->group->unit_id);

		su->hidden = false;
		clif_getareachar_skillunit(&su->bl, su, AREA, 0);
		return 1;
	}
	return 0;
}

/**
 * Attempt to reaveal trap in area
 * @param src Skill caster
 * @param range Affected range
 * @param x
 * @param y
 **/
void skill_reveal_trap_inarea(struct block_list *src, int range, int x, int y) {
	if (!battle_config.traps_setting)
		return;
	nullpo_retv(src);
	map_foreachinarea(skill_reveal_trap, src->m, x - range, y - range, x + range, y + range, BL_SKILL);
}

static int skill_trigger_reverberation(struct block_list *bl, va_list ap)
{
	int64 tick = va_arg(ap, int64);
	TBL_SKILL *su = (TBL_SKILL*)bl;

	if (su->alive && su->group && su->group->unit_id == UNT_REVERBERATION)
	{// Trigger all reverberations in the AoE.
		su->group->limit = DIFF_TICK32(tick,su->group->tick) + 1500;
		return 1;
	}
	return 0;
}

static int skill_destroy_trap( struct block_list *bl, va_list ap )
{
	struct skill_unit *su = (struct skill_unit *)bl;
	struct skill_unit_group *sg = NULL;
	int64 tick;
	
	nullpo_retr(0, su);
	tick = va_arg(ap, int64);

	if (su->alive && (sg = su->group) && skill_get_inf2(sg->skill_id)&INF2_TRAP)
	{
		switch (sg->unit_id)
		{
			case UNT_SKIDTRAP:
			case UNT_LANDMINE:
			case UNT_ANKLESNARE:
			case UNT_SHOCKWAVE:
			case UNT_SANDMAN:
			case UNT_FLASHER:
			case UNT_FREEZINGTRAP:
			case UNT_BLASTMINE:
			case UNT_CLAYMORETRAP:
			case UNT_TALKIEBOX:
			case UNT_ELECTRICSHOCKER:
			case UNT_CLUSTERBOMB:
			case UNT_MAGENTATRAP:
			case UNT_COBALTTRAP:
			case UNT_MAIZETRAP:
			case UNT_VERDURETRAP:
			case UNT_FIRINGTRAP:
			case UNT_ICEBOUNDTRAP:
			case UNT_THORNS_TRAP:
				if (battle_config.skill_wall_check)
					map_foreachinshootrange(skill_trap_splash, &su->bl, skill_get_splash(sg->skill_id, sg->skill_lv), sg->bl_flag | BL_SKILL | ~BCT_SELF, &su->bl, tick);
				else
					map_foreachinrange(skill_trap_splash, &su->bl, skill_get_splash(sg->skill_id, sg->skill_lv), sg->bl_flag | BL_SKILL | ~BCT_SELF, &su->bl, tick);
				break;
		}
		// Traps aren't not recovered.
		skill_delunit(su);
	}
	return 0;
}

/*==========================================
 *
 *
 *------------------------------------------*/
int skill_castend_damage_id (struct block_list* src, struct block_list *bl, int skill_id, int skill_lv, int64 tick, int flag)
{
	struct map_session_data *sd = NULL, *tsd = NULL;
	struct mob_data *md = NULL, *tmd = NULL;
	struct elemental_data *ed = NULL;
	struct status_data *sstatus, *tstatus;
	struct status_change *sc, *tsc;

	if (skill_id > 0 && skill_lv <= 0) return 0; // Wrong skill level.

	nullpo_retr(1, src);
	nullpo_retr(1, bl);

	if (src->m != bl->m)
		return 1;

	if (bl->prev == NULL)
		return 1;

	sd = BL_CAST(BL_PC, src);
	md = BL_CAST(BL_MOB, src);
	ed = BL_CAST(BL_ELEM, src);

	tsd = BL_CAST(BL_PC, bl);
	tmd = BL_CAST(BL_MOB, bl);

	if (status_isdead(bl))
		return 1;

	if (skill_id && skill_get_type(skill_id) == BF_MAGIC && status_isimmune(bl) == 100)
	{	//GTB makes all targetted magic display miss with a single bolt.
		sc_type sct = status_skill2sc(skill_id);
		if (sct != SC_NONE)
			status_change_end(bl, sct, INVALID_TIMER);
		clif_skill_damage(src, bl, tick, status_get_amotion(src), status_get_dmotion(bl), 0, 1, skill_id, skill_lv, skill_get_hit(skill_id));
		return 1;
	}

	sc = status_get_sc(src);
	tsc = status_get_sc(bl);
	if (sc && !sc->count)
		sc = NULL; //Unneeded
	if( tsc && !tsc->count )
		tsc = NULL;

	tstatus = status_get_status_data(bl);
	sstatus = status_get_status_data(src);

	map_freeblock_lock();

	switch(skill_id)
	{
	case MER_CRASH:
	case SM_BASH:
	case MS_BASH:
	case MC_MAMMONITE:
	case TF_DOUBLE:
	case AC_DOUBLE:
	case MA_DOUBLE:
	case AS_SONICBLOW:
	case KN_PIERCE:
	case ML_PIERCE:
	case KN_SPEARBOOMERANG:
	case TF_POISON:
	case TF_SPRINKLESAND:
	case AC_CHARGEARROW:
	case MA_CHARGEARROW:
	case RG_INTIMIDATE:
	case AM_ACIDTERROR:
	case BA_MUSICALSTRIKE:
	case DC_THROWARROW:
	case BA_DISSONANCE:
	case CR_HOLYCROSS:
	case NPC_DARKCROSS:
	case CR_SHIELDCHARGE:
	case CR_SHIELDBOOMERANG:
	case NPC_PIERCINGATT:
	case NPC_MENTALBREAKER:
	case NPC_RANGEATTACK:
	case NPC_CRITICALSLASH:
	case NPC_COMBOATTACK:
	case NPC_GUIDEDATTACK:
	case NPC_POISON:
	case NPC_RANDOMATTACK:
	case NPC_WATERATTACK:
	case NPC_GROUNDATTACK:
	case NPC_FIREATTACK:
	case NPC_WINDATTACK:
	case NPC_POISONATTACK:
	case NPC_HOLYATTACK:
	case NPC_DARKNESSATTACK:
	case NPC_TELEKINESISATTACK:
	case NPC_UNDEADATTACK:
	case NPC_ARMORBRAKE:
	case NPC_WEAPONBRAKER:
	case NPC_HELMBRAKE:
	case NPC_SHIELDBRAKE:
	case NPC_BLINDATTACK:
	case NPC_SILENCEATTACK:
	case NPC_STUNATTACK:
	case NPC_PETRIFYATTACK:
	case NPC_CURSEATTACK:
	case NPC_SLEEPATTACK:
	case LK_AURABLADE:
	case LK_SPIRALPIERCE:
	case ML_SPIRALPIERCE:
	case LK_HEADCRUSH:
	case CG_ARROWVULCAN:
	case HW_MAGICCRASHER:
	case ITM_TOMAHAWK:
	case MO_TRIPLEATTACK:
	case CH_CHAINCRUSH:
	case CH_TIGERFIST:
	case PA_SHIELDCHAIN:	// Shield Chain
	case PA_SACRIFICE:
	case WS_CARTTERMINATION:	// Cart Termination
	case AS_VENOMKNIFE:
	case HT_PHANTASMIC:
	case TK_DOWNKICK:
	case TK_COUNTER:
	case GS_CHAINACTION:
	case GS_TRIPLEACTION:
	case GS_MAGICALBULLET:
	case GS_TRACKING:
	case GS_PIERCINGSHOT:
	case GS_RAPIDSHOWER:
	case GS_DUST:
	case GS_DISARM:				// Added disarm. [Reddozen]
	case GS_FULLBUSTER:
	case NJ_SYURIKEN:
	case NJ_KUNAI:
	case ASC_BREAKER:
	case HFLI_MOON:	//[orn]
	case HFLI_SBR44:	//[orn]
	case NPC_BLEEDING:
	case NPC_CRITICALWOUND:
	case NPC_HELLPOWER:
	case RK_SONICWAVE:
	case RK_HUNDREDSPEAR:
	case RK_WINDCUTTER:
	case RK_DRAGONBREATH:
	case GC_CROSSIMPACT:
	case GC_VENOMPRESSURE:
	case AB_DUPLELIGHT_MELEE:
	case RA_AIMEDBOLT:
	case NC_AXEBOOMERANG:
	case NC_POWERSWING:
	case SC_TRIANGLESHOT:
	case SC_FEINTBOMB:
	case LG_BANISHINGPOINT:
	case LG_SHIELDPRESS:
	case LG_RAGEBURST:
	case LG_RAYOFGENESIS:
	case LG_HESPERUSLIT:
	case SR_DRAGONCOMBO:
	case SR_SKYNETBLOW:
	case SR_FALLENEMPIRE:
	case SR_CRESCENTELBOW_AUTOSPELL:
	case SR_GATEOFHELL:
	case SR_GENTLETOUCH_QUIET:
	case SR_HOWLINGOFLION:
	case WM_SEVERE_RAINSTORM_MELEE:
	case WM_GREAT_ECHO:
	case GN_SLINGITEM_RANGEMELEEATK:
	case RL_R_TRIP_PLUSATK:
	case KO_SETSUDAN:
	case KO_BAKURETSU:
	case KO_HUUMARANKA:
	case GC_DARKCROW:
	case RK_DRAGONBREATH_WATER:
	case SU_BITE:
	case SU_SCAROFTAROU:
	case SU_PICKYPECK:
	case MH_CBC:
	case EL_WIND_SLASH:
	case EL_STONE_HAMMER:
		skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
		break;
	case MH_NEEDLE_OF_PARALYZE:
	case MH_SONIC_CRAW:
	case MH_MIDNIGHT_FRENZY:
	case MH_SILVERVEIN_RUSH:
		skill_attack(skill_get_type(skill_id), src, src, bl, skill_id, skill_lv, tick, flag);
		break;
	case NC_MAGMA_ERUPTION:
		skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag | SD_ANIMATION);
		break;
	case GN_CRAZYWEED_ATK:
		skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag|SD_LEVEL|SD_ANIMATION);
		break;
	// Works just like the above except animations are done through ZC_USE_SKILL.
	case RL_MASS_SPIRAL:
	case RL_BANISHING_BUSTER:
	case RL_AM_BLAST:
	case RL_SLUGSHOT:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag|SD_ANIMATION));
		break;
	case RL_H_MINE:
		{// Max allowed targets to be tagged with a mine.
			short count = MAX_HOWL_MINES;
			int i = 0;
			int64 hm_damage = 0;

			if ( flag&1 )
			{// Splash damage from explosion.
				skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag|8);

				if( (tsd && tsd->sc.data[SC_H_MINE] && tsd->sc.data[SC_H_MINE]->val1 == src->id) || 
					(tmd && tmd->sc.data[SC_H_MINE] && tmd->sc.data[SC_H_MINE]->val1 == src->id) )
						status_change_end(bl, SC_H_MINE, INVALID_TIMER);
			}
			else if ( flag&4 )
			{// Triggered by Flicker.
				flag = 0;// Reset flag.
				map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag|BCT_ENEMY|SD_SPLASH|1, skill_castend_damage_id);
			}
			else
			{
				// Only players and monsters can be tagged....I think??? [Rytech]
				// Lets only allow players and monsters to use this skill for safety reasons.
				if( (!tsd && !tmd) || !sd && !md )
				{
					if( sd )
						clif_skill_fail(sd, skill_id, 0, 0, 0);
					break;
				}

				// Check if the target is already tagged by another source.
				if( (tsd && tsd->sc.data[SC_H_MINE] && tsd->sc.data[SC_H_MINE]->val1 != src->id) || // Cant tag a player that was already tagged from another source.
					(tmd && tmd->sc.data[SC_H_MINE] && tmd->sc.data[SC_H_MINE]->val1 != src->id) )// Same as the above check, but for monsters.
				{
					if( sd )
						clif_skill_fail(sd,skill_id,0,0,0);
					map_freeblock_unlock();
					return 1;
				}

				if( sd )
				{// Tagging the target.
					ARR_FIND(0, count, i, sd->howl_mine[i] == bl->id );
					if( i == count )
					{
						ARR_FIND(0, count, i, sd->howl_mine[i] == 0 );
						if( i == count )
						{// Max number of targets tagged. Fail the skill.
							clif_skill_fail(sd, skill_id, 0, 0, 0);
							map_freeblock_unlock();
							return 1;
						}
					}
					// Attack the target and return the damage result for the upcoming check.
					hm_damage = skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);

					// Tag the target only if damage was done. If it deals no damage, it counts as a miss and won't tag.
					// Note: Not sure if it works like this in official but you can't stick a explosive on something you can't
					// hit, right? For now well just use this logic until we can get a confirm on if it does this or not. [Rytech]
					if ( hm_damage > 0 )
					{// Add the ID of the tagged target to the player's tag list and start the status on the target.
						sd->howl_mine[i] = bl->id;

						// Val4 flags if the status was applied by a player or a monster.
						// This will be important for other skills that work together with this one.
						// 1 = Player, 2 = Monster.
						// Note: Because the attacker's ID and the slot number is handled here, we have to
						// apply the status here. We can't pass this data to skill_additional_effect.
						sc_start4(bl, SC_H_MINE, 100, src->id, i, skill_lv, 1, skill_get_time(skill_id, skill_lv));
					}
				}
				else if ( md )// Monster's cant track with this skill. Just give the status.
				{
					hm_damage = skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);

					if ( hm_damage > 0 )
						sc_start4(bl, SC_H_MINE, 100, 0, 0, skill_lv, 2, skill_get_time(skill_id, skill_lv));
				}
			}
		}
		break;

	case SJ_FLASHKICK:
		{// Max allowed targets to be tagged with a stellar mark.
			short count = MAX_STELLAR_MARKS;
			int i = 0;
			int64 fk_damage = 0;

			// Only players and monsters can be tagged....I think??? [Rytech]
			// Lets only allow players and monsters to use this skill for safety reasons.
			if( (!tsd && !tmd) || !sd && !md )
			{
				if( sd )
					clif_skill_fail(sd, skill_id, 0, 0, 0);
				break;
			}

			// Check if the target is already tagged by another source.
			if( (tsd && tsd->sc.data[SC_FLASHKICK] && tsd->sc.data[SC_FLASHKICK]->val1 != src->id) || // Cant tag a player that was already tagged from another source.
				(tmd && tmd->sc.data[SC_FLASHKICK] && tmd->sc.data[SC_FLASHKICK]->val1 != src->id) )// Same as the above check, but for monsters.
			{
				if( sd )
					clif_skill_fail(sd,skill_id,0,0,0);
				map_freeblock_unlock();
				return 1;
			}

			if( sd )
			{// Tagging the target.
				ARR_FIND(0, count, i, sd->stellar_mark[i] == bl->id );
				if( i == count )
				{
					ARR_FIND(0, count, i, sd->stellar_mark[i] == 0 );
					if( i == count )
					{// Max number of targets tagged. Fail the skill.
						clif_skill_fail(sd, skill_id, 0, 0, 0);
						map_freeblock_unlock();
						return 1;
					}
				}
				// Attack the target and return the damage result for the upcoming check.
				fk_damage = skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);

				// Tag the target only if damage was done. If it deals no damage, it counts as a miss and won't tag.
				// Note: Not sure if it works like this in official but you can't mark on something you can't
				// hit, right? For now well just use this logic until we can get a confirm on if it does this or not. [Rytech]
				if ( fk_damage > 0 )
				{// Add the ID of the tagged target to the player's tag list and start the status on the target.
					sd->stellar_mark[i] = bl->id;

					// Val4 flags if the status was applied by a player or a monster.
					// This will be important for other skills that work together with this one.
					// 1 = Player, 2 = Monster.
					// Note: Because the attacker's ID and the slot number is handled here, we have to
					// apply the status here. We can't pass this data to skill_additional_effect.
					sc_start4(bl, SC_FLASHKICK, 100, src->id, i, skill_lv, 1, skill_get_time(skill_id, skill_lv));
				}
			}
			else if ( md )// Monster's cant track with this skill. Just give the status.
			{
				fk_damage = skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);

				if ( fk_damage > 0 )
					sc_start4(bl, SC_FLASHKICK, 100, 0, 0, skill_lv, 2, skill_get_time(skill_id, skill_lv));
			}
		}
		break;

	case NC_BOOSTKNUCKLE:
	case NC_PILEBUNKER:
	//case NC_VULCANARM:
	case NC_COLDSLOWER:
		// Heat of the mado
		if (sd)
			pc_overheat(sd, 1);
		skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
		break;

	case LK_JOINTBEAT: // decide the ailment first (affects attack damage and effect)
		switch( rnd()%6 ){
		case 0: flag |= BREAK_ANKLE; break;
		case 1: flag |= BREAK_WRIST; break;
		case 2: flag |= BREAK_KNEE; break;
		case 3: flag |= BREAK_SHOULDER; break;
		case 4: flag |= BREAK_WAIST; break;
		case 5: flag |= BREAK_NECK; break;
		}
		//TODO: is there really no cleaner way to do this?
		sc = status_get_sc(bl);
		if (sc) sc->jb_flag = flag;
		skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		break;

	case MO_COMBOFINISH:
		if (!(flag&1) && sc && sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_MONK)
		{	//Becomes a splash attack when Soul Linked.
			map_foreachinshootrange(skill_area_sub, bl,
				skill_get_splash(skill_id, skill_lv),BL_CHAR|BL_SKILL,
				src,skill_id,skill_lv,tick, flag|BCT_ENEMY|1,
				skill_castend_damage_id);
		} else
			skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		break;

	case TK_STORMKICK: // Taekwon kicks [Dralnu]
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		skill_area_temp[1] = 0;
		map_foreachinshootrange(skill_attack_area, src,
			skill_get_splash(skill_id, skill_lv), BL_CHAR|BL_SKILL,
			BF_WEAPON, src, src, skill_id, skill_lv, tick, flag, BCT_ENEMY);
		break;

	case KN_CHARGEATK:
		{
		bool path = path_search_long(NULL, src->m, src->x, src->y, bl->x, bl->y,CELL_CHKWALL);
		unsigned int dist = distance_bl(src, bl);
		unsigned int dir = map_calc_dir(bl, src->x, src->y);

		// teleport to target (if not on WoE grounds)
		if (skill_check_unit_movepos(3, src, bl->x, bl->y, 0, 1))
		{
			skill_blown(src, src, 1, (dir + 4) % 8, 0); //Target position is actually one cell next to the target
		}

		// cause damage and knockback if the path to target was a straight one
		if( path )
		{
			if (skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, dist))
				skill_blown(src, bl, dist, dir, 0);
			//HACK: since knockback officially defaults to the left, the client also turns to the left... therefore,
			// make the caster look in the direction of the target
			unit_setdir(src, (dir+4)%8);
		}

		}
		break;

	case NC_FLAMELAUNCHER:
		if (sd) 
			pc_overheat(sd,1);
	case LG_CANNONSPEAR:
		skill_area_temp[1] = bl->id;
		if (battle_config.skill_eightpath_algorithm) {
			//Use official AoE algorithm
			map_foreachindir(skill_attack_area, src->m, src->x, src->y, bl->x, bl->y,
				skill_get_splash(skill_id, skill_lv), skill_get_maxcount(skill_id, skill_lv), 0, splash_target(src),
				skill_get_type(skill_id), src, src, skill_id, skill_lv, tick, flag, BCT_ENEMY);
		}
		else {
			map_foreachinpath(skill_attack_area, src->m, src->x, src->y, bl->x, bl->y,
				skill_get_splash(skill_id, skill_lv), skill_get_maxcount(skill_id, skill_lv), splash_target(src),
				skill_get_type(skill_id), src, src, skill_id, skill_lv, tick, flag, BCT_ENEMY);
		}
		break;

	case LG_PINPOINTATTACK:
		if (skill_check_unit_movepos(5, src, bl->x, bl->y, 1, 1))
			clif_blown(src);
		skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
 		break;

	case SN_SHARPSHOOTING:
	case MA_SHARPSHOOTING:
	case NJ_KAMAITACHI:
	case NPC_ACIDBREATH:
	case NPC_DARKNESSBREATH:
	case NPC_FIREBREATH:
	case NPC_ICEBREATH:
	case NPC_THUNDERBREATH:
	case SU_SVG_SPIRIT:
		skill_area_temp[1] = bl->id;
		map_foreachinpath(skill_attack_area,src->m,src->x,src->y,bl->x,bl->y,
			skill_get_splash(skill_id, skill_lv),skill_get_maxcount(skill_id,skill_lv), splash_target(src),
			skill_get_type(skill_id),src,src,skill_id,skill_lv,tick,flag,BCT_ENEMY);
		break;

	case MO_INVESTIGATE:
		skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		status_change_end(src, SC_BLADESTOP, INVALID_TIMER);
		break;

	case RG_BACKSTAP:
		{
			int dir = map_calc_dir(src, bl->x, bl->y), t_dir = unit_getdir(bl);
			if ((!check_distance_bl(src, bl, 0) && !map_check_dir(dir, t_dir)) || bl->type == BL_SKILL) {
				status_change_end(src, SC_HIDING, INVALID_TIMER);
				skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
				dir = dir < 4 ? dir+4 : dir-4; // change direction [Celest]
				unit_setdir(bl,dir);
			}
			else if (sd)
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
		}
		break;

	case MO_FINGEROFFENSIVE:
		skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		if (battle_config.finger_offensive_type && sd) {
			int i;
			for (i = 1; i < sd->spiritball_old; i++)
				skill_addtimerskill(src, tick + i * 200, bl->id, 0, 0, skill_id, skill_lv, BF_WEAPON, flag);
		}
		status_change_end(src, SC_BLADESTOP, INVALID_TIMER);
		break;

	case MO_CHAINCOMBO:
		skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		status_change_end(src, SC_BLADESTOP, INVALID_TIMER);
		break;

	case NJ_ISSEN:
	case MO_EXTREMITYFIST:
	{
		struct block_list *mbl = bl; // For NJ_ISSEN
		short x, y, i = 2; // Move 2 cells (From target)
		short dir = map_calc_dir(src, bl->x, bl->y);

		skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
		if (skill_id == MO_EXTREMITYFIST) {
			status_set_sp(src, 0, 0);
			status_change_end(src, SC_EXPLOSIONSPIRITS, INVALID_TIMER);
			status_change_end(src, SC_BLADESTOP, INVALID_TIMER);
		}
		else {
			status_set_hp(src, 1, 0);
			status_change_end(src, SC_NEN, INVALID_TIMER);
			status_change_end(src, SC_HIDING, INVALID_TIMER);
		}
		if (skill_id == MO_EXTREMITYFIST) {
			mbl = src; // For MO_EXTREMITYFIST
			i = 3; // Move 3 cells (From caster)
		}
		if (dir > 0 && dir < 4)
			x = -i;
		else if (dir > 4)
			x = i;
		else
			x = 0;
		if (dir > 2 && dir < 6)
			y = -i;
		else if (dir == 7 || dir < 2)
			y = i;
		else
			y = 0;
		// Ashura Strike still has slide effect in GVG
		if ((mbl == src || (!map_flag_gvg2(src->m) && !map[src->m].flag.battleground)) &&
			unit_movepos(src, mbl->x + x, mbl->y + y, 1, 1)) {
			clif_blown(src);
			clif_spiritball_sub(src, NULL, AREA);
		}
	}
	break;

	case HT_POWER:
		if (tstatus->race == RC_BRUTE || tstatus->race == RC_INSECT)
			skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
		break;

	//Splash attack skills.
	case AS_GRIMTOOTH:
	case MC_CARTREVOLUTION:
	case NPC_SPLASHATTACK:
		flag |= SD_PREAMBLE; // a fake packet will be sent for the first target to be hit
	case AS_SPLASHER:
	case HT_BLITZBEAT:
	case AC_SHOWER:
	case MA_SHOWER:
	case MG_NAPALMBEAT:
	case MG_FIREBALL:
	case RG_RAID:
	case HW_NAPALMVULCAN:
	case NJ_HUUMA:
	case ASC_METEORASSAULT:
	case GS_SPREADATTACK:
	case NPC_EARTHQUAKE:
	case NPC_PULSESTRIKE:
	case NPC_HELLJUDGEMENT:
	case NPC_VAMPIRE_GIFT:
	case RK_IGNITIONBREAK:
	case GC_ROLLINGCUTTER:
	case GC_COUNTERSLASH:
	case AB_JUDEX:
	case AB_ADORAMUS:
	case WL_SOULEXPANSION:
	case WL_CRIMSONROCK:
	case WL_JACKFROST:
	case RA_ARROWSTORM:
	case RA_WUGDASH:
	case NC_VULCANARM:
	case NC_ARMSCANNON:
	case NC_SELFDESTRUCTION:
	case NC_AXETORNADO:
	case LG_MOONSLASHER:
	case LG_EARTHDRIVE:
	case SR_RAMPAGEBLASTER:
	case SR_WINDMILL:
	case SR_RIDEINLIGHTNING:
	case SO_VARETYR_SPEAR:
	case GN_CART_TORNADO:
	case GN_CARTCANNON:
	case GN_SPORE_EXPLOSION:
	case RL_S_STORM:
	case RL_FIREDANCE:
	case RL_R_TRIP:
	case KO_HAPPOKUNAI:
	case GN_ILLUSIONDOPING:
	case SJ_FULLMOONKICK:
	case SJ_NEWMOONKICK:
	case SJ_STAREMPEROR:
	case SJ_SOLARBURST:
	case SJ_PROMINENCEKICK:
	case SJ_FALLINGSTAR_ATK2:
	case SP_CURSEEXPLOSION:
	case SP_SHA:
	case SP_SWHOO:
	case SU_SCRATCH:
	case SU_LUNATICCARROTBEAT:
	case SU_LUNATICCARROTBEAT2:
	case MH_HEILIGE_STANGE:
	case MH_MAGMA_FLOW:
	case EL_CIRCLE_OF_FIRE:
	case EL_FIRE_BOMB_ATK:
	case EL_FIRE_WAVE_ATK:
	case EL_WATER_SCREW_ATK:
	case EL_HURRICANE_ATK:
		if (flag&1)
		{	//Recursive invocation
			// skill_area_temp[0] holds number of targets in area
			// skill_area_temp[1] holds the id of the original target
			// skill_area_temp[2] counts how many targets have already been processed
			int sflag = skill_area_temp[0] & 0xFFF;
			int heal = 0;
			if( flag&SD_LEVEL )
				sflag |= SD_LEVEL; // -1 will be used in packets instead of the skill level
			if( skill_area_temp[1] != bl->id && !(skill_get_inf2(skill_id)&INF2_NPC_SKILL) )
				sflag |= SD_ANIMATION; // original target gets no animation (as well as all NPC skills)

			// If a enemy player is standing next to a mob when splash Es- skill is casted, the player won't get hurt.
			if ( ( skill_id == SP_SHA || skill_id == SP_SWHOO ) && !battle_config.allow_es_magic_pc && bl->type != BL_MOB )
				break;
			
			heal = (int)skill_attack(skill_get_type(skill_id), src, src, bl, skill_id, skill_lv, tick, sflag);
			if (skill_id == NPC_VAMPIRE_GIFT && heal > 0)
			{
				clif_skill_nodamage(NULL, src, AL_HEAL, heal, 1);
				status_heal(src, heal, 0, 0);
			}

			if ( skill_id == SJ_PROMINENCEKICK )// Trigger the 2nd hit. (100% fire damage.)
				skill_attack(skill_get_type(skill_id), src, src, bl, skill_id, skill_lv, tick, sflag|8|SD_ANIMATION);
		}
		else
		{
			int n;
			if( sd && skill_id == SU_LUNATICCARROTBEAT && (n = pc_search_inventory(sd,ITEMID_CARROT)) >= 0 )
			{// If carrot is in the caster's inventory, switch to alternate skill ID to give a chance to stun.
				pc_delitem(sd,n,1,0,1,LOG_TYPE_CONSUME);
				skill_id = SU_LUNATICCARROTBEAT2;
			}

			if (sd && ( skill_id == SP_SHA || skill_id == SP_SWHOO ) && !battle_config.allow_es_magic_pc && bl->type != BL_MOB)
			{
				status_change_start(src,src,SC_STUN,10000,skill_lv,0,0,0,500,10);
				clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}
			else if ( skill_id == SP_SWHOO )
				status_change_end(src, SC_USE_SKILL_SP_SPA, INVALID_TIMER);

			if (skill_id == NJ_BAKUENRYU || skill_id == LG_EARTHDRIVE || skill_id == GN_CARTCANNON)
				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			if (skill_id == LG_MOONSLASHER)
				clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
			//FIXME: Isn't EarthQuake a ground skill after all?
			if (skill_id == NPC_EARTHQUAKE)
				skill_addtimerskill(src, tick + 250, src->id, 0, 0, skill_id, skill_lv, 2, flag | BCT_ENEMY | SD_SPLASH | 1);

			skill_area_temp[0] = 0;
			skill_area_temp[1] = bl->id;
			skill_area_temp[2] = 0;

			if (skill_id == NC_VULCANARM )
				if (sd) pc_overheat(sd, 1);

			// if skill damage should be split among targets, count them
			//SD_LEVEL -> Forced splash damage for Auto Blitz-Beat -> count targets
			//special case: Venom Splasher uses a different range for searching than for splashing
			if(flag&SD_LEVEL || skill_get_nk(skill_id)&NK_SPLASHSPLIT)
				skill_area_temp[0] = map_foreachinrange(skill_area_sub, bl, (skill_id == AS_SPLASHER || skill_id == GN_SPORE_EXPLOSION) ? 1 : skill_get_splash(skill_id, skill_lv), BL_CHAR, src, skill_id, skill_lv, tick, BCT_ENEMY, skill_area_sub_count);

			// recursive invocation of skill_castend_damage_id() with flag|1
			if (battle_config.skill_wall_check && skill_id != NPC_EARTHQUAKE)
				map_foreachinshootrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag | BCT_ENEMY | SD_SPLASH | 1, skill_castend_damage_id);
			else
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag | BCT_ENEMY | SD_SPLASH | 1, skill_castend_damage_id);

			if (skill_id == AS_SPLASHER) {
				map_freeblock_unlock(); // Don't consume a second gemstone.
				return 0;
			}

			// Switch back to original skill ID in case there's more to be done beyond here.
			if ( skill_id == SU_LUNATICCARROTBEAT2 )
				skill_id = SU_LUNATICCARROTBEAT;
		}
		break;

	//Place units around target
	case NJ_BAKUENRYU:
		clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		skill_unitsetting(src, skill_id, skill_lv, bl->x, bl->y, 0);
		break;

	case RL_QD_SHOT:
		if( flag&1 )
		{
			if ( sd )
			{// If a player used the skill it will search for targets marked by that player. 
				if (tsc && tsc->data[SC_C_MARKER] && tsc->data[SC_C_MARKER]->val3 == 1)// Mark placed by a player.
				{
					short i = 0;
					ARR_FIND( 0, MAX_CRIMSON_MARKS, i, sd->crimson_mark[i] == bl->id);
					if ( i < MAX_CRIMSON_MARKS )
						clif_skill_nodamage(src,bl,skill_id,skill_lv,skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag));
				}
			}// If a monster used the skill it will search for targets marked by any monster since they can't track their own targets.
			else if (tsc && tsc->data[SC_C_MARKER] && tsc->data[SC_C_MARKER]->val3 == 2)// Mark placed by a monster.
					clif_skill_nodamage(src,bl,skill_id,skill_lv,skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag));
		}
		else
		{// Attack the main target you triggered Chain Action / Eternal Chain on first and then do a area search for other's with a mark.
			clif_skill_nodamage(src,bl,skill_id,skill_lv,skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag));
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag|BCT_ENEMY|SD_SPLASH|1, skill_castend_damage_id);
		}
		break;

	case RL_D_TAIL:
		if ( sd )
		{// If a player used the skill it will search for targets marked by that player. 
			if ( tsc && tsc->data[SC_C_MARKER] && tsc->data[SC_C_MARKER]->val3 == 1 )// Mark placed by a player.
			{
				short i = 0;
				ARR_FIND( 0, MAX_CRIMSON_MARKS, i, sd->crimson_mark[i] == bl->id);
				if ( i < MAX_CRIMSON_MARKS )
					skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
			}
		}// If a monster used the skill it will search for targets marked by any monster since they can't track their own targets.
		else if ( tsc && tsc->data[SC_C_MARKER] && tsc->data[SC_C_MARKER]->val3 == 2 )// Mark placed by a monster.
			skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
		break;

	case SJ_FALLINGSTAR_ATK:
		if ( sd )
		{// If a player used the skill it will search for targets marked by that player. 
			if (tsc && tsc->data[SC_FLASHKICK] && tsc->data[SC_FLASHKICK]->val4 == 1)// Mark placed by a player.
			{
				short i = 0;
				ARR_FIND( 0, MAX_STELLAR_MARKS, i, sd->stellar_mark[i] == bl->id);
				if ( i < MAX_STELLAR_MARKS )
				{
					skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
					skill_castend_damage_id(src, bl, SJ_FALLINGSTAR_ATK2, skill_lv, tick, 0);
				}
			}
		}// If a monster used the skill it will search for targets marked by any monster since they can't track their own targets.
		else if (tsc && tsc->data[SC_FLASHKICK] && tsc->data[SC_FLASHKICK]->val4 == 2)// Mark placed by a monster.
		{
			skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
			skill_castend_damage_id(src, bl, SJ_FALLINGSTAR_ATK2, skill_lv, tick, 0);
		}
		break;

	case SM_MAGNUM:
	case MS_MAGNUM:
		if (flag & 1) {
			// For players, damage depends on distance, so add it to flag if it is > 1
			// Cannot hit hidden targets
			skill_attack(skill_get_type(skill_id), src, src, bl, skill_id, skill_lv, tick, flag | SD_ANIMATION | (sd ? distance_bl(src, bl) : 0));
		}
		break;

	case KN_BRANDISHSPEAR:
	case ML_BRANDISH:
		//Coded apart for it needs the flag passed to the damage calculation.
		if (skill_area_temp[1] != bl->id)
			skill_attack(skill_get_type(skill_id), src, src, bl, skill_id, skill_lv, tick, flag|SD_ANIMATION);
		else
			skill_attack(skill_get_type(skill_id), src, src, bl, skill_id, skill_lv, tick, flag);
		break;

	case KN_BOWLINGBASH:
	case MS_BOWLINGBASH:
	{
		int min_x, max_x, min_y, max_y, i, c, dir, tx, ty;
		// Chain effect and check range gets reduction by recursive depth, as this can reach 0, we don't use blowcount
		c = (skill_lv - (flag & 0xFFF) + 1) / 2;
		// Determine the Bowling Bash area depending on configuration
		
		// Gutter line system
		min_x = ((src->x) - c) - ((src->x) - c) % 40;
		if (min_x < 0) min_x = 0;
		max_x = min_x + 39;
		min_y = ((src->y) - c) - ((src->y) - c) % 40;
		if (min_y < 0) min_y = 0;
		max_y = min_y + 39;
		
		// Initialization, break checks, direction
		if ((flag & 0xFFF) > 0) {
			// Ignore monsters outside area
			if (bl->x < min_x || bl->x > max_x || bl->y < min_y || bl->y > max_y)
				break;
			// Ignore monsters already in list
			if (idb_exists(bowling_db, bl->id))
				break;
			// Random direction
			dir = rnd() % 8;
		}
		else {
			// Create an empty list of already hit targets
			db_clear(bowling_db);
			// Direction is walkpath
			dir = (unit_getdir(src) + 4) % 8;
		}
		// Add current target to the list of already hit targets
		idb_put(bowling_db, bl->id, bl);
		// Keep moving target in direction square by square
		tx = bl->x;
		ty = bl->y;
		for (i = 0; i < c; i++) {
			// Target coordinates (get changed even if knockback fails)
			tx -= dirx[dir];
			ty -= diry[dir];
			// If target cell is a wall then break
			if (map_getcell(bl->m, tx, ty, CELL_CHKWALL))
				break;
			skill_blown(src, bl, 1, dir, 0);
			// Splash around target cell, but only cells inside area; we first have to check the area is not negative
			if ((max(min_x, tx - 1) <= min(max_x, tx + 1)) &&
				(max(min_y, ty - 1) <= min(max_y, ty + 1)) &&
				(map_foreachinarea(skill_area_sub, bl->m, max(min_x, tx - 1), max(min_y, ty - 1), min(max_x, tx + 1), min(max_y, ty + 1), splash_target(src), src, skill_id, skill_lv, tick, flag | BCT_ENEMY, skill_area_sub_count))) {
				// Recursive call
				map_foreachinarea(skill_area_sub, bl->m, max(min_x, tx - 1), max(min_y, ty - 1), min(max_x, tx + 1), min(max_y, ty + 1), splash_target(src), src, skill_id, skill_lv, tick, (flag | BCT_ENEMY) + 1, skill_castend_damage_id);
				// Self-collision
				if (bl->x >= min_x && bl->x <= max_x && bl->y >= min_y && bl->y <= max_y)
					skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, (flag & 0xFFF) > 0 ? SD_ANIMATION : 0);
				break;
			}
		}
		// Original hit or chain hit depending on flag
		skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, (flag & 0xFFF) > 0 ? SD_ANIMATION : 0);
	}
	break;

	case KN_SPEARSTAB:
		if(flag&1) {
			if (bl->id==skill_area_temp[1])
				break;
			if (skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,SD_ANIMATION))
				skill_blown(src,bl,skill_area_temp[2],-1,0);
		} else {
			int x=bl->x,y=bl->y,i,dir;
			dir = map_calc_dir(bl,src->x,src->y);
			skill_area_temp[1] = bl->id;
			skill_area_temp[2] = skill_get_blewcount(skill_id,skill_lv);
			// all the enemies between the caster and the target are hit, as well as the target
			if (skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,0))
				skill_blown(src,bl,skill_area_temp[2],-1,0);
			for (i=0;i<4;i++) {
				map_foreachincell(skill_area_sub,bl->m,x,y,BL_CHAR,
					src,skill_id,skill_lv,tick,flag|BCT_ENEMY|1,skill_castend_damage_id);
				x += dirx[dir];
				y += diry[dir];
			}
		}
		break;

	case TK_TURNKICK:
	case MO_BALKYOUNG: //Active part of the attack. Skill-attack [Skotlex]
	{
		skill_area_temp[1] = bl->id; //NOTE: This is used in skill_castend_nodamage_id to avoid affecting the target.
		if (skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag))
			map_foreachinrange(skill_area_sub,bl,
				skill_get_splash(skill_id, skill_lv),BL_CHAR,
				src,skill_id,skill_lv,tick,flag|BCT_ENEMY|1,
				skill_castend_nodamage_id);
	}
		break;
	case CH_PALMSTRIKE: //	Palm Strike takes effect 1sec after casting. [Skotlex]
	//	clif_skill_nodamage(src,bl,skill_id,skill_lv,0); //Can't make this one display the correct attack animation delay :/
		clif_damage(src,bl,tick,status_get_amotion(src),0,-1,1,4,0, false); //Display an absorbed damage attack.
		skill_addtimerskill(src, tick + (1000 + status_get_amotion(src)), bl->id, 0, 0, skill_id, skill_lv, BF_WEAPON, flag);
		break;

	case PR_TURNUNDEAD:
	case ALL_RESURRECTION:
		if (!battle_check_undead(tstatus->race, tstatus->def_ele))
			break;
		skill_attack(BF_MAGIC,src,src,bl,skill_id,skill_lv,tick,flag);
		break;

	case MG_SOULSTRIKE:
	case NPC_DARKSTRIKE:
	case MG_COLDBOLT:
	case MG_FIREBOLT:
	case MG_LIGHTNINGBOLT:
	case WZ_EARTHSPIKE:
	case AL_HEAL:
	case AL_HOLYLIGHT:
	case NPC_DARKTHUNDER:
	case PR_ASPERSIO:
	case MG_FROSTDIVER:
	case WZ_SIGHTBLASTER:
	case WZ_SIGHTRASHER:
	case NJ_KOUENKA:
	case NJ_HYOUSENSOU:
	case NJ_HUUJIN:
	case AB_HIGHNESSHEAL:
	case AB_DUPLELIGHT_MAGIC:
	case WL_TETRAVORTEX_FIRE:
	case WL_TETRAVORTEX_WATER:
	case WL_TETRAVORTEX_WIND:
	case WL_TETRAVORTEX_GROUND:
	case WM_METALICSOUND:
	case KO_KAIHOU:
	case SU_SV_STEMSPEAR:
	case MH_ERASER_CUTTER:
	case EL_FIRE_ARROW:
	case EL_ICE_NEEDLE:
		skill_attack(BF_MAGIC,src,src,bl,skill_id,skill_lv,tick,flag);
		break;

	case EL_FIRE_BOMB:
	case EL_FIRE_WAVE:
	case EL_WATER_SCREW:
	case EL_TIDAL_WEAPON:
	case EL_HURRICANE:
	case EL_TYPOON_MIS:
	case EL_ROCK_CRUSHER:
		{
			short skill_switch = 0;

			if ( rnd()%100 < 50 )
				skill_attack(skill_get_type(skill_id),src,src,bl,skill_id,skill_lv,tick,flag);
			else
			{
				if (ed && skill_id == EL_TIDAL_WEAPON)
				{
					clif_skill_damage(src, &ed->master->bl, tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 5);
					sc_start(&ed->bl, SC_TIDAL_WEAPON, 100, 1, skill_get_time(skill_id, skill_lv));
				}
				else if (skill_id == EL_TYPOON_MIS)
				{
					clif_skill_damage(src, bl, tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 5);
					skill_attack(skill_get_type(EL_TYPOON_MIS_ATK), src, src, bl, EL_TYPOON_MIS_ATK, skill_lv, tick, flag);
				}
				else if ( skill_id == EL_ROCK_CRUSHER )
				{
					clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 5);
					skill_attack(skill_get_type(EL_ROCK_CRUSHER_ATK),src,src,bl,EL_ROCK_CRUSHER_ATK,skill_lv,tick,flag);
				}
				else
				{
					switch ( skill_id )
					{
						case EL_FIRE_BOMB:
							skill_switch = EL_FIRE_BOMB_ATK;
							break;

						case EL_FIRE_WAVE:
							skill_switch = EL_FIRE_WAVE_ATK;
							break;

						case EL_WATER_SCREW:
							skill_switch = EL_WATER_SCREW_ATK;
							break;

						case EL_HURRICANE:
							skill_switch = EL_HURRICANE_ATK;
							break;
					}
					clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 5);
					skill_castend_damage_id(src, bl, skill_switch, skill_lv, tick, flag);
				}
			}
		}
		break;

	case EL_STONE_RAIN:
		if ( !flag && rnd()%100 < 50 )
		{// Physical Version. Single target.
			clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
			skill_attack(skill_get_type(skill_id),src,src,bl,skill_id,skill_lv,tick,flag);
		}
		else
		{
			if( flag&1 )
			{// Magical version. Does a splash attack.
				skill_attack(BF_MAGIC, src, src, bl, skill_id, skill_lv, tick, flag);
			}
			else
			{
				skill_area_temp[0] = 0;
				clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
				map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag|BCT_ENEMY|SD_SPLASH|1, skill_castend_damage_id);
			}
		}
		break;

	case WL_HELLINFERNO:
		// Flag 4 signals the fire attack. Flag 8 signals the shadow attack.
		skill_attack(BF_MAGIC,src,src,bl,skill_id,skill_lv,tick,flag|4);
		skill_addtimerskill(src, tick+200, bl->id, 0, 0, skill_id, skill_lv, BF_MAGIC, flag|8);
		break;

	case NPC_MAGICALATTACK:
		skill_attack(BF_MAGIC,src,src,bl,skill_id,skill_lv,tick,flag);
		sc_start(src,status_skill2sc(skill_id),100,skill_lv,skill_get_time(skill_id,skill_lv));
		break;

	case HVAN_CAPRICE: //[blackhole89]
		{
			int ran=rnd()%4;
			int sid = 0;
			switch(ran)
			{
			case 0: sid=MG_COLDBOLT; break;
			case 1: sid=MG_FIREBOLT; break;
			case 2: sid=MG_LIGHTNINGBOLT; break;
			case 3: sid=WZ_EARTHSPIKE; break;
			}
			skill_attack(BF_MAGIC,src,src,bl,sid,skill_lv,tick,flag|SD_LEVEL);
		}
		break;

	case WZ_WATERBALL:
		//Deploy waterball cells, these are used and turned into waterballs via the timerskill
		skill_unitsetting(src, skill_id, skill_lv, src->x, src->y, 0);
		skill_addtimerskill(src, tick, bl->id, src->x, src->y, skill_id, skill_lv, 0, flag);
		break;
	case WZ_JUPITEL:
		//Jupitel Thunder is delayed by 150ms, you can cast another spell before the knockback
		skill_addtimerskill(src, tick + TIMERSKILL_INTERVAL, bl->id, 0, 0, skill_id, skill_lv, 1, flag);
		break;

	case WL_CHAINLIGHTNING:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		skill_addtimerskill(src,tick + 150,bl->id,3,0,WL_CHAINLIGHTNING_ATK,skill_lv,4+skill_lv,flag);
		break;

	case PR_BENEDICTIO:
		//Should attack undead and demons. [Skotlex]
		if (battle_check_undead(tstatus->race, tstatus->def_ele) || tstatus->race == RC_DEMON)
			skill_attack(BF_MAGIC, src, src, bl, skill_id, skill_lv, tick, flag);
	break;

	case SL_SMA:
		status_change_end(src, SC_SMA, INVALID_TIMER);
		status_change_end(src, SC_USE_SKILL_SP_SHA, INVALID_TIMER);
	case SL_STIN:
	case SL_STUN:
	case SP_SPA:
		if (sd && !battle_config.allow_es_magic_pc && bl->type != BL_MOB) {
			status_change_start(src,src,SC_STUN,10000,skill_lv,0,0,0,500,10);
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			break;
		}
		skill_attack(BF_MAGIC,src,src,bl,skill_id,skill_lv,tick,flag);
		break;

	case NPC_DARKBREATH:
		clif_emotion(src,E_AG);
	case SN_FALCONASSAULT:
	case PA_PRESSURE:
	case CR_ACIDDEMONSTRATION:
	case TF_THROWSTONE:
	case NPC_SMOKING:
	case GS_FLING:
	case NJ_ZENYNAGE:
	case GN_THORNS_TRAP:
	case GN_HELLS_PLANT_ATK:
	case SU_SV_ROOTTWIST_ATK:
		skill_attack(BF_MISC, src, src, bl, skill_id, skill_lv, tick, flag);
		break;

	case SJ_NOVAEXPLOSING:
		skill_attack(BF_MISC,src,src,bl,skill_id,skill_lv,tick,flag);

		// We can end Dimension here since the cooldown code is processed before this point.
		if ( sc && sc->data[SC_DIMENSION] )
			status_change_end(src, SC_DIMENSION, INVALID_TIMER);
		else// Dimension not active? Activate the 2 second skill block penalty.
			sc_start(src, SC_NOVAEXPLOSING, 100, skill_lv, skill_get_time(skill_id, skill_lv));
		break;

	case SP_SOULEXPLOSION:
		if ( !(tsc && (tsc->data[SC_SPIRIT] || tsc->data[SC_SOULGOLEM] || tsc->data[SC_SOULSHADOW] || 
			tsc->data[SC_SOULFALCON] || tsc->data[SC_SOULFAIRY])) || tstatus->hp < 10 * tstatus->max_hp / 100)
		{// Requires target to have a soul link and more then 10% of MaxHP.
			// With this skill requiring a soul link, and the target to have more then 10% if MaxHP, I wonder
			// if the cooldown still happens after it fails. Need a confirm. [Rytech] 
			if (sd)
				clif_skill_fail(sd,skill_id,0,0,0);
			break;
		}

		skill_attack(BF_MISC,src,src,bl,skill_id,skill_lv,tick,flag);
		break;

	case KO_MUCHANAGE:
		{
			int rate = (1000 - (10000 / (sstatus->dex + sstatus->luk) * 5)) * (skill_lv / 2 + 5) / 100;
			if ( rate < 0 )
				rate = 0;
			if ( rnd()%100 < rate )// Success chance of hitting is applied to each enemy seprate.
				skill_attack(BF_MISC,src,src,bl,skill_id,skill_lv,tick,skill_area_temp[0]&0xFFF);
		}
		break;

	case NPC_SELFDESTRUCTION: {
		struct status_change *tsc = NULL;
		if ((tsc = status_get_sc(bl)) && tsc->data[SC_HIDING])
			break;
	}
	case HVAN_EXPLOSION:
		if (src != bl)
			skill_attack(BF_MISC,src,src,bl,skill_id,skill_lv,tick,flag);
		break;

	// Celest
	case PF_SOULBURN:
		if (rnd()%100 < (skill_lv < 5 ? 30 + skill_lv * 10 : 70)) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			if (skill_lv == 5)
				skill_attack(BF_MAGIC,src,src,bl,skill_id,skill_lv,tick,flag);
			status_percent_damage(src, bl, 0, 100, false);
		} else {
			clif_skill_nodamage(src,src,skill_id,skill_lv,1);
			if (skill_lv == 5)
				skill_attack(BF_MAGIC,src,src,src,skill_id,skill_lv,tick,flag);
			status_percent_damage(src, src, 0, 100, false);
		}
		break;

	case NPC_BLOODDRAIN:
	case NPC_ENERGYDRAIN:
		{
			int heal = (int)skill_attack( (skill_id == NPC_BLOODDRAIN) ? BF_WEAPON : BF_MAGIC,
					src, src, bl, skill_id, skill_lv, tick, flag);
			if (heal > 0){
				clif_skill_nodamage(NULL, src, AL_HEAL, heal, 1);
				status_heal(src, heal, 0, 0);
			}
		}
		break;

	case GS_BULLSEYE:
		skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		break;

	case NJ_KASUMIKIRI:
		if (skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag) > 0)
			sc_start(src,SC_HIDING,100,skill_lv,skill_get_time(skill_id,skill_lv));
		break;

	case KO_JYUMONJIKIRI:
	case NJ_KIRIKAGE:
		if( !map_flag_gvg2(src->m) && !map[src->m].flag.battleground )
		{	//You don't move on GVG grounds.
			short x, y;
			map_search_freecell(bl, 0, &x, &y, 1, 1, 0);
			if (unit_movepos(src, x, y, 0, 0)) {
				clif_blown(src);
			}
		}
		if ( skill_id == NJ_KIRIKAGE )
			status_change_end(src, SC_HIDING, INVALID_TIMER);
		skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		break;

	case RK_PHANTOMTHRUST:
		unit_setdir(src,map_calc_dir(src, bl->x, bl->y));
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);

		skill_blown(src, bl, distance_bl(src, bl) - 1, unit_getdir(src), 0);
		if (sd && tsd && sd->status.party_id && sd->status.party_id && sd->status.party_id == tsd->status.party_id) // Don't damage party members.
			; // No damage to Members
		else
			skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
		break;

	case RK_STORMBLAST:
		if (sd)
			if (pc_checkskill(sd, RK_RUNEMASTERY) >= 3)
				skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
		break;

	case GC_DARKILLUSION:
		{
			short x, y;
			short dir = map_calc_dir(src,bl->x,bl->y);

			if( dir > 4 ) x = -1;
			else if( dir > 0 && dir < 4 ) x = 1;
			else x = 0;
			if( dir < 3 || dir > 5 ) y = -1;
			else if( dir > 3 && dir < 5 ) y = 1;
			else y = 0;

			if( unit_movepos(src, bl->x+x, bl->y+y, 1, 1) )
			{
				clif_blown(src);
				skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
				if( rnd()%100 < 4 * skill_lv )
					skill_castend_damage_id(src,bl,GC_CROSSIMPACT,skill_lv,tick,flag);
			}

		}
		break;

	case GC_WEAPONCRUSH:
		if( sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == GC_WEAPONBLOCKING )
			skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		else if( sd )
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_GC_WEAPONBLOCKING,0,0);
		break;

	case GC_CROSSRIPPERSLASHER:
		if( sd && !(sc && sc->data[SC_ROLLINGCUTTER]) )
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_CONDITION,0,0);
		else
		{
			skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
			status_change_end(src,SC_ROLLINGCUTTER, INVALID_TIMER);
		}
		break;

	case GC_PHANTOMMENACE:
		if( flag&1 ) { // Only Hits Invisible Targets
			sc = status_get_sc(bl);
			if (tsc && (tsc->option&(OPTION_HIDE | OPTION_CLOAK | OPTION_CHASEWALK)))
				skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		}
		break;

	case WL_DRAINLIFE:
	{
						 int heal = (int)skill_attack(skill_get_type(skill_id), src, src, bl, skill_id, skill_lv, tick, flag);
						 int rate = 70 + 5 * skill_lv;

						 heal = heal * (5 + 5 * skill_lv) / 100;

						 if (bl->type == BL_SKILL)
							 heal = 0; // Don't absorb heal from Ice Walls or other skill units.

						 if (heal && rnd() % 100 < rate)
						 {
							 status_heal(src, heal, 0, 0);
							 clif_skill_nodamage(NULL, src, AL_HEAL, heal, 1);
						 }
	}
		break;

	case WL_FROSTMISTY:
		//Don't deal damage to hidden targets but these get Freezing anyway.
		sc = status_get_sc(bl);
		if( sc && (sc->option&(OPTION_HIDE|OPTION_CLOAK|OPTION_CHASEWALK) || sc->data[SC__INVISIBILITY]) )
			sc_start(bl,SC_FROST,20 + 12 * skill_lv,skill_lv,skill_get_time(skill_id,skill_lv));
		else
			skill_attack(BF_MAGIC,src,src,bl,skill_id,skill_lv,tick,flag);
		break;

	case WL_TETRAVORTEX:
		if (sd && sc) { // No SC? No spheres
			int spheres[5] = { 0, 0, 0, 0, 0 },
				positions[5] = {-1,-1,-1,-1,-1 },
				i, j = 0, k, subskill = 0;

			for( i = SC_SUMMON1; i <= SC_SUMMON5; i++ )
				if( sc->data[i] )
				{
					spheres[j] = i;
					positions[j] = sc->data[i]->val2;
					j++;
				}

			if( j < 4 )
			{ // Need 4 spheres minimum
				clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}

			// Sphere Sort, this time from new to old
			for( i = 0; i <= j - 2; i++ )
				for( k = i + 1; k <= j - 1; k++ )
					if( positions[i] < positions[k] )
					{
						swap(positions[i],positions[k]);
						swap(spheres[i],spheres[k]);
					}

			if (j == 5) { // If 5 spheres, remove last one and only do 4 actions
				status_change_end(src, spheres[4], INVALID_TIMER);
				j = 4;
			}

			k = 0;
			for (i = 0; i < j; i++) { // Loop should always be 4 for regular players, but unconditional_skill could be less
				switch (sc->data[spheres[i]]->val1) {
					case WLS_FIRE:  subskill = WL_TETRAVORTEX_FIRE; k |= 1; break;
					case WLS_WIND:  subskill = WL_TETRAVORTEX_WIND; k |= 4; break;
					case WLS_WATER: subskill = WL_TETRAVORTEX_WATER; k |= 2; break;
					case WLS_STONE: subskill = WL_TETRAVORTEX_GROUND; k |= 8; break;

				}
				skill_addtimerskill(src, tick + 250 * i, bl->id, k, 0, subskill, skill_lv, i, flag);
				status_change_end(src,spheres[i],INVALID_TIMER);
			}
		}
		break;

	case WL_RELEASE:
		if( sd )
		{
			int i;
			
			skill_toggle_magicpower(src, skill_id); // No hit will be amplified

			// Priority is to release SpellBook
			ARR_FIND(0,MAX_SPELLBOOK,i,sd->rsb[i].skill_id != 0);
			if( i < MAX_SPELLBOOK )
			{ // SpellBook
				int rsb_skillid, rsb_skilllv, cooldown;
				if (skill_lv > 1)
				{
					ARR_FIND(0, MAX_SPELLBOOK, i, sd->rsb[i].skill_id == 0);
					i--; // At skilllvl 2, Release uses the last learned skill in spellbook
				}

				rsb_skillid = sd->rsb[i].skill_id;
				rsb_skilllv = sd->rsb[i].level;
				
				if (skill_lv > 1)
					sd->rsb[i].skill_id = 0; // Last position - only remove it from list
				else
					memmove(&sd->rsb[0], &sd->rsb[1], sizeof(sd->rsb) - sizeof(sd->rsb[0]));

				if (sd->rsb[0].skill_id == 0)
					status_change_end(&sd->bl, SC_READING_SB, INVALID_TIMER);

				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
				if (!skill_check_condition_castbegin(sd, rsb_skillid, rsb_skilllv))
					break;

				switch (skill_get_casttype(rsb_skillid))
				{
					case CAST_GROUND:
						skill_castend_pos2(src, bl->x, bl->y, rsb_skillid, rsb_skilllv, tick, 0);
						break;
					case CAST_NODAMAGE:
						skill_castend_nodamage_id(src, bl, rsb_skillid, rsb_skilllv, tick, 0);
						break;
					case CAST_DAMAGE:
						skill_castend_damage_id(src, bl, rsb_skillid, rsb_skilllv, tick, 0);
						break;
				}
					cooldown = pc_get_skillcooldown(sd, rsb_skillid, rsb_skilllv);
				if (cooldown)
					skill_blockpc_start(sd, rsb_skillid, cooldown);

				sd->ud.canact_tick = tick + skill_delayfix(src, rsb_skillid, rsb_skilllv);
				clif_status_change(src, SI_ACTIONDELAY, 1, skill_delayfix(src, rsb_skillid, rsb_skilllv), 0, 0, 1);
			}
			else
			{ // Summon Balls
				int j = 0, k, skele;
				int spheres[5] = { 0, 0, 0, 0, 0 },
					positions[5] = {-1,-1,-1,-1,-1 };

				for( i = SC_SUMMON1; i <= SC_SUMMON5; i++ )
					if( sc && sc->data[i] )
					{
						spheres[j] = i;
						positions[j] = sc->data[i]->val2;
						sc->data[i]->val2--; // Prepares for next position
						j++;
					}
				if( j == 0 )
				{ // No Spheres
					clif_skill_fail(sd,skill_id,0x14,0,0);
					break;
				}
				
				// Sphere Sort
				for( i = 0; i <= j - 2; i++ )
					for( k = i + 1; k <= j - 1; k++ )
						if( positions[i] > positions[k] )
						{
							swap(positions[i],positions[k]);
							swap(spheres[i],spheres[k]);
						}

				if( skill_lv == 1 ) j = 1; // Limit only to one ball
				for( i = 0; i < j; i++ ){
					skele = WL_RELEASE - 5 + sc->data[spheres[i]]->val1 - WLS_FIRE; // Convert Ball Element into Skill ATK for balls
					// WL_SUMMON_ATK_FIRE, WL_SUMMON_ATK_WIND, WL_SUMMON_ATK_WATER, WL_SUMMON_ATK_GROUND
					skill_addtimerskill(src,tick+status_get_adelay(src)*i,bl->id,0,0,skele,sc->data[spheres[i]]->val3,BF_MAGIC,flag|SD_LEVEL);
					status_change_end(src, (sc_type)spheres[i], INVALID_TIMER); // Eliminate ball
				}
				clif_skill_nodamage(src,bl,skill_id,0,1);
			}
		}
		break;

	case RA_WUGSTRIKE:
	case RA_WUGBITE:
		if( path_search(NULL,src->m,src->x,src->y,bl->x,bl->y,1,CELL_CHKNOREACH) ){
			skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		}
		break;


	case RA_SENSITIVEKEEN:
		if( bl->type != BL_SKILL )
		{ // Only Hits Invisible Targets
			sc = status_get_sc(bl);
			if (tsc && (tsc->option&(OPTION_HIDE | OPTION_CLOAK | OPTION_CHASEWALK)))
				skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		}else{
			struct skill_unit *su = BL_CAST(BL_SKILL,bl);
			struct skill_unit_group* sg;
			if( su && (sg=su->group) && skill_get_inf2(sg->skill_id)&INF2_TRAP && sg->src_id != src->id &&
				battle_check_target(src, map_id2bl(sg->src_id), BCT_ENEMY) > 0 )
			{
				if( sd && !(sg->unit_id == UNT_USED_TRAPS || (sg->unit_id == UNT_ANKLESNARE && sg->val2 != 0 )) )
				{
					struct item item_tmp;
					memset(&item_tmp,0,sizeof(item_tmp));
					item_tmp.nameid = sg->item_id ? sg->item_id : ITEMID_TRAP;
					item_tmp.identify = 1;
					if( item_tmp.nameid )
						map_addflooritem(&item_tmp, 1, bl->m, bl->x, bl->y, 0, 0, 0, 0, 0, false);
				}
				skill_delunit(su);
			}
		}
		break;

	case NC_INFRAREDSCAN:
		if( flag&1 )
		{ //TODO: Need a confirmation if the other type of hidden status is included to be scanned. [Jobbie]
			sc_start(bl, SC_INFRAREDSCAN, 10000, skill_lv, skill_get_time(skill_id, skill_lv));
			status_change_end(bl, SC_HIDING, INVALID_TIMER);
			status_change_end(bl, SC_CLOAKING, INVALID_TIMER);
			status_change_end(bl, SC_CLOAKINGEXCEED, INVALID_TIMER); // Need confirm it.
			status_change_end(bl, SC_NEWMOON, INVALID_TIMER);
			sc_start(bl, SC_INFRAREDSCAN, 10000, skill_lv, skill_get_time(skill_id, skill_lv));
		}else{
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag|BCT_ENEMY|SD_SPLASH|1, skill_castend_damage_id);
			clif_skill_damage(src,src,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
			if( sd ) pc_overheat(sd,1);
		}
		break;

	case NC_MAGNETICFIELD:
		sc_start2(bl,SC_MAGNETICFIELD,100,skill_lv,src->id,skill_get_time(skill_id,skill_lv));
 		break;
	case SC_FATALMENACE:
		if( flag&1 )
			skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		else{
			short x, y;
			map_search_freecell(src, 0, &x, &y, -1, -1, 0);
			// Destination area
			skill_area_temp[4] = x;
			skill_area_temp[5] = y;
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_damage_id);
			skill_addtimerskill(src,tick + 800,src->id,x,y,skill_id,skill_lv,0,flag); // To teleport Self
			clif_skill_damage(src,src,tick,status_get_amotion(src),0,-30000,1,skill_id,skill_lv,6);
		}
		break;

	case LG_SHIELDSPELL:
		if ( skill_lv == 1 )
			skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		else if ( skill_lv == 2 )
			skill_attack(BF_MAGIC,src,src,bl,skill_id,skill_lv,tick,flag);
 		break;

	case LG_OVERBRAND:
		skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag|SD_LEVEL);
		break;

	case LG_OVERBRAND_BRANDISH:
		skill_addtimerskill(src, tick + status_get_amotion(src), bl->id, 0, 0, skill_id, skill_lv, BF_WEAPON, flag | SD_LEVEL);
		break;

	case SR_KNUCKLEARROW:
		// teleport to target (if not on WoE grounds)
		if (skill_check_unit_movepos(5, src, bl->x, bl->y, 1, 1))
			// Self knock back 1 cell to make it appear you warped
			// next to the enemy you targeted from the direction
			// you attacked from.
			skill_blown(bl,src,1,unit_getdir(src),0);
			skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
		break;

	case SR_EARTHSHAKER:
		if( flag&1 ){
			skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
			sc_start(bl,SC_STUN, 25 + 5 * skill_lv,skill_lv,skill_get_time(skill_id,skill_lv));
		}
		else
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag|BCT_ENEMY|SD_SPLASH|1, skill_castend_damage_id);
			clif_skill_damage(src, src, tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
		break;

	case SR_TIGERCANNON:
		if (flag & 1)
		{
			skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
		}
		else if (sd)
		{
			int hpcost, spcost;
			hpcost = 10 + 2 * skill_lv;
			spcost = 5 + skill_lv;
			if (!status_charge(src, status_get_max_hp(src) * hpcost / 100, status_get_max_sp(src) * spcost / 100))
			{
				if (sd) clif_skill_fail(sd, skill_id, 0, 0, 0);
				break;
			}
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag | BCT_ENEMY | SD_SPLASH | 1, skill_castend_damage_id);
			status_zap(src, hpcost, spcost);
		}
		break;

	case WM_SOUND_OF_DESTRUCTION:
		if (tsc && (tsc->data[SC_SWING] || tsc->data[SC_SYMPHONY_LOVE] || tsc->data[SC_MOONLIT_SERENADE] ||
		tsc->data[SC_RUSH_WINDMILL] || tsc->data[SC_ECHOSONG] || tsc->data[SC_HARMONIZE] || 
		tsc->data[SC_SIREN] || tsc->data[SC_DEEPSLEEP] || tsc->data[SC_SIRCLEOFNATURE] ||
		tsc->data[SC_GLOOMYDAY] || tsc->data[SC_SONG_OF_MANA] || 
		tsc->data[SC_DANCE_WITH_WUG] || tsc->data[SC_SATURDAY_NIGHT_FEVER] || tsc->data[SC_LERADS_DEW] || 
		tsc->data[SC_MELODYOFSINK] || tsc->data[SC_BEYOND_OF_WARCRY] || tsc->data[SC_UNLIMITED_HUMMING_VOICE] || tsc->data[SC_FRIGG_SONG]) &&
		rnd() % 100 < 4 * skill_lv + 2 * (sd ? pc_checkskill(sd, WM_LESSON) : 10) + 10 * skill_chorus_count(sd))
		{
			skill_attack(BF_MISC,src,src,bl,skill_id,skill_lv,tick,flag);
			status_change_start(src,bl,SC_STUN,10000,skill_lv,0,0,0,skill_get_time(skill_id,skill_lv),8);
			status_change_end(bl, SC_SWING, INVALID_TIMER);
			status_change_end(bl, SC_SYMPHONY_LOVE, INVALID_TIMER);
			status_change_end(bl, SC_MOONLIT_SERENADE, INVALID_TIMER);
			status_change_end(bl, SC_RUSH_WINDMILL, INVALID_TIMER);
			status_change_end(bl, SC_ECHOSONG, INVALID_TIMER);
			status_change_end(bl, SC_HARMONIZE, INVALID_TIMER);
			status_change_end(bl, SC_SIREN, INVALID_TIMER);
			status_change_end(bl, SC_DEEPSLEEP, INVALID_TIMER);
			status_change_end(bl, SC_SIRCLEOFNATURE, INVALID_TIMER);
			status_change_end(bl, SC_GLOOMYDAY, INVALID_TIMER);
			status_change_end(bl, SC_SONG_OF_MANA, INVALID_TIMER);
			status_change_end(bl, SC_DANCE_WITH_WUG, INVALID_TIMER);
			status_change_end(bl, SC_SATURDAY_NIGHT_FEVER, INVALID_TIMER);
			status_change_end(bl, SC_LERADS_DEW, INVALID_TIMER);
			status_change_end(bl, SC_MELODYOFSINK, INVALID_TIMER);
			status_change_end(bl, SC_BEYOND_OF_WARCRY, INVALID_TIMER);
			status_change_end(bl, SC_UNLIMITED_HUMMING_VOICE, INVALID_TIMER);
			status_change_end(bl, SC_FRIGG_SONG, INVALID_TIMER);
		}
		break;

	case SO_POISON_BUSTER:
		if ( flag&1 )
		{
			skill_attack(BF_MAGIC, src, src, bl, skill_id, skill_lv, tick, flag);
		}
		else if (sd)
		{
			if (tsc && tsc->data[SC_POISON])
			{
				map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag | BCT_ENEMY | SD_SPLASH | 1, skill_castend_damage_id);
				status_change_end(bl, SC_POISON, INVALID_TIMER);
			}
			else
				clif_skill_fail(sd, skill_id, 0, 0, 0);
		}
		break;

	case MH_STAHL_HORN:
	case MH_TINDER_BREAKER:
		if( unit_movepos(src, bl->x, bl->y, 1, 1) )
		{	// Self knock back 1 cell to make it appear you warped
			// next to the enemy you targeted from the direction
			// you attacked from.
			skill_blown(bl,src,1,unit_getdir(src),0);
			skill_attack(BF_WEAPON,src,src,bl,skill_id,skill_lv,tick,flag);
		}
		break;

	case MH_EQC:
		if (!(tstatus->mode&MD_STATUS_IMMUNE))// Not usable on boss monsters.
			skill_attack(BF_MISC,src,src,bl,skill_id,skill_lv,tick,flag);
		break;

	case 0:
		if(sd) {
			if (flag & 3){
				if (bl->id != skill_area_temp[1])
					skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, SD_LEVEL|flag);
			} else {
				skill_area_temp[1] = bl->id;
				map_foreachinrange(skill_area_sub, bl,
					sd->bonus.splash_range, BL_CHAR,
					src, skill_id, skill_lv, tick, flag | BCT_ENEMY | 1,
					skill_castend_damage_id);
				flag|=1; //Set flag to 1 so ammo is not double-consumed. [Skotlex]
			}
		}
		break;

	default:
		ShowWarning("skill_castend_damage_id: Unknown skill used:%d\n",skill_id);
		clif_skill_damage(src, bl, tick, status_get_amotion(src), tstatus->dmotion,
			0, abs(skill_get_num(skill_id, skill_lv)),
			skill_id, skill_lv, skill_get_hit(skill_id));
		map_freeblock_unlock();
		return 1;
	}

	map_freeblock_unlock();

	if( sd && !(flag&1) )
	{// ensure that the skill last-cast tick is recorded
		sd->canskill_tick = gettick();

		if (sd->state.arrow_atk) // consume arrow on last invocation to this skill.
			battle_consume_ammo(sd, skill_id, skill_lv);

		// perform skill requirement consumption
		skill_consume_requirement(sd,skill_id,skill_lv,2);
	}

	return 0;
}

/*========================================== [Playtester]
* Process tarot card's effects
* @ param src: Source of the tarot card effect
* @ param target: Target of the tartor card effect
* @ param skill_id: ID of the skill used
* @ param skill_lv: Level of the skill used
* @ param tick: Processing tick time
* @ return Card number
*------------------------------------------*/
static int skill_tarotcard(struct block_list* src, struct block_list *target, uint16 skill_id, uint16 skill_lv, int64 tick)
{
	int card = 0;
	if (battle_config.tarotcard_equal_chance) {
		//eAthena equal chances
		card = rand() % 14 + 1;
	}
	else {
		//Official chances
		int rate = rand() % 100;
		if (rate < 10) card = 1; // THE FOOL
		else if (rate < 20) card = 2; // THE MAGICIAN
		else if (rate < 30) card = 3; // THE HIGH PRIESTESS
		else if (rate < 37) card = 4; // THE CHARIOT
		else if (rate < 47) card = 5; // STRENGTH
		else if (rate < 62) card = 6; // THE LOVERS
		else if (rate < 63) card = 7; // WHEEL OF FORTUNE
		else if (rate < 69) card = 8; // THE HANGED MAN
		else if (rate < 74) card = 9; // DEATH
		else if (rate < 82) card = 10; // TEMPERANCE
		else if (rate < 83) card = 11; // THE DEVIL
		else if (rate < 85) card = 12; // THE TOWER
		else if (rate < 90) card = 13; // THE STAR
		else card = 14; // THE SUN
	}
	switch (card) {
		case 1: // THE FOOL - heals SP to 0
		{
			status_percent_damage(src, target, 0, 100, false);
			break;
		}
		case 2:  // THE MAGICIAN - matk halved
		{
			status_change_start(src, target, SC_INCMATKRATE, 10000, -50, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
			break;
		}
		case 3: // THE HIGH PRIESTESS - all buffs removed
		{
			status_change_clear_buffs(target, SCCB_BUFFS | SCCB_CHEM_PROTECT);
			break;
		}
		case 4: // THE CHARIOT - 1000 damage, random armor destroyed
		{
			status_fix_damage(src, target, 1000, 0);
			clif_damage(src, target, tick, 0, 0, 1000, 0, DMG_NORMAL, 0, false);
			if (!status_isdead(target))
			{
				unsigned short where[] = { EQP_ARMOR, EQP_SHIELD, EQP_HELM };
				skill_break_equip(target, where[rnd() % 3], 10000, BCT_ENEMY);
			}
			break;
		}
		case 5: // STRENGTH - atk halved
		{
			status_change_start(src, target, SC_INCATKRATE, 10000, -50, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
			break;
		}
		case 6: // THE LOVERS - 2000HP heal, random teleported
		{
			status_heal(target, 2000, 0, 0);
			if (!map_flag_vs(target->m))
				unit_warp(target, -1, -1, -1, CLR_TELEPORT);
			break;
		}
		case 7: // WHEEL OF FORTUNE - random 2 other effects
		{
			// Recursive call
			skill_tarotcard(src, target, skill_id, skill_lv, tick);
			skill_tarotcard(src, target, skill_id, skill_lv, tick);
			break;
		}
		case 8: // THE HANGED MAN - stop, freeze or stoned
		{
			enum sc_type sc[] = { SC_STOP, SC_FREEZE, SC_STONE };
			uint8 rand_eff = rnd() % 3;
			int time = ((rand_eff == 0) ? skill_get_time2(skill_id, skill_lv) : skill_get_time2(status_sc2skill(sc[rand_eff]), 1));
			status_change_start(src, target, sc[rand_eff], 10000, skill_lv, 0, 0, 0, time, 0);
			break;
		}
		case 9: // DEATH - curse, coma and poison
		{
			status_change_start(src, target, SC_COMA, 10000, skill_lv, 0, src->id, 0, 0, SCSTART_NONE);
			status_change_start(src, target, SC_CURSE, 10000, skill_lv, 0, 0, 0, skill_get_time2(status_sc2skill(SC_CURSE), 1), 0);
			status_change_start(src, target, SC_POISON, 10000, skill_lv, src->id, 0, 0, skill_get_time2(status_sc2skill(SC_POISON), 1), 0);
			break;
		}
		case 10: // TEMPERANCE - confusion
		{
			status_change_start(src, target, SC_CONFUSION, 10000, skill_lv, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
			break;
		}
		case 11: // THE DEVIL - 6666 damage, atk and matk halved, cursed
		{
			status_fix_damage(src, target, 6666, 0);
			clif_damage(src, target, tick, 0, 0, 6666, 0, DMG_NORMAL, 0, false);
			status_change_start(src, target, SC_INCATKRATE, 10000, -50, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
			status_change_start(src, target, SC_INCMATKRATE, 10000, -50, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
			status_change_start(src, target, SC_CURSE, 10000, skill_lv, 0, 0, 0, skill_get_time2(status_sc2skill(SC_CURSE), 1), 0);
			break;
		}
		case 12: // THE TOWER - 4444 damage
		{
			status_fix_damage(src, target, 4444, 0);
			clif_damage(src, target, tick, 0, 0, 4444, 0, DMG_NORMAL, 0, false);
			break;
		}
		case 13: // THE STAR - stun
		{
			status_change_start(src, target, SC_STUN, 10000, skill_lv, 0, 0, 0, skill_get_time2(status_sc2skill(SC_STUN), 1), 0);
			break;
		}
		default: // THE SUN - atk, matk, hit, flee and def reduced, immune to more tarot card effects
		{
			status_change_start(src, target, SC_INCATKRATE, 10000, -20, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
			status_change_start(src, target, SC_INCMATKRATE, 10000, -20, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
			status_change_start(src, target, SC_INCHITRATE, 10000, -20, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
			status_change_start(src, target, SC_INCFLEERATE, 10000, -20, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
			status_change_start(src, target, SC_INCDEFRATE, 10000, -20, 0, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
			return 14; //To make sure a valid number is returned
		}
	}
	return card;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_castend_nodamage_id (struct block_list *src, struct block_list *bl, int skill_id, int skill_lv, int64 tick, int flag) {
	struct map_session_data *sd, *dstsd;
	struct mob_data *md, *dstmd;
	struct homun_data *hd;
	struct mercenary_data *mer;
	struct elemental_data *ed;
	struct status_data *sstatus, *tstatus;
	struct status_change *sc, *tsc;
	struct status_change_entry *tsce;

	int i = 0;
	int rate = 0;
	enum sc_type type;

	if(skill_id > 0 && skill_lv <= 0) return 0;	// celest

	nullpo_retr(1, src);
	nullpo_retr(1, bl);

	if (src->m != bl->m)
		return 1;

	sd = BL_CAST(BL_PC, src);
	hd = BL_CAST(BL_HOM, src);
	md = BL_CAST(BL_MOB, src);
	mer = BL_CAST(BL_MER, src);
	ed = BL_CAST(BL_ELEM, src);

	dstsd = BL_CAST(BL_PC, bl);
	dstmd = BL_CAST(BL_MOB, bl);

	if(bl->prev == NULL)
		return 1;
	if(status_isdead(src))
		return 1;

	if (src != bl && status_isdead(bl) && skill_id != ALL_RESURRECTION && skill_id != PR_REDEMPTIO && skill_id != NPC_WIDESOULDRAIN && skill_id != WM_DEADHILLHERE && skill_id != WE_ONEFOREVER)
		return 1;

	tstatus = status_get_status_data(bl);
	sstatus = status_get_status_data(src);

	//Check for undead skills that convert a no-damage skill into a damage one. [Skotlex]
	switch (skill_id) {
		case HLIF_HEAL:	//[orn]
			if (bl->type != BL_HOM) {
				if (sd) clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
 		case AL_HEAL:
		case ALL_RESURRECTION:
		case PR_ASPERSIO:
 		case AB_HIGHNESSHEAL:
			//Apparently only player casted skills can be offensive like this.
			if (sd && battle_check_undead(tstatus->race,tstatus->def_ele)) {
				if (battle_check_target(src, bl, BCT_ENEMY) < 1) {
				  	//Offensive heal does not works on non-enemies. [Skotlex]
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
					return 0;
				}
				return skill_castend_damage_id(src, bl, skill_id == AB_HIGHNESSHEAL ? AL_HEAL : skill_id, skill_lv, tick, flag);
			}
			break;
		case NPC_SMOKING: //Since it is a self skill, this one ends here rather than in damage_id. [Skotlex]
			return skill_castend_damage_id (src, bl, skill_id, skill_lv, tick, flag);
		default:
			//Skill is actually ground placed.
			if (src == bl && skill_get_unit_id(skill_id,0))
				return skill_castend_pos2(src,bl->x,bl->y,skill_id,skill_lv,tick,0);
	}

	type = status_skill2sc(skill_id);
	sc = status_get_sc(src);
	tsc = status_get_sc(bl);
	tsce = (tsc && type != -1)?tsc->data[type]:NULL;

	if (src!=bl && type > -1 &&
		(i = skill_get_ele(skill_id, skill_lv)) > ELE_NEUTRAL &&
		skill_get_inf(skill_id) != INF_SUPPORT_SKILL &&
		battle_attr_fix(NULL, NULL, 100, i, tstatus->def_ele, tstatus->ele_lv) <= 0)
		return 1; //Skills that cause an status should be blocked if the target element blocks its element.

	map_freeblock_lock();
	switch(skill_id)
	{
	case HLIF_HEAL:	//[orn]
	case AL_HEAL:
	case AB_HIGHNESSHEAL:
	case SU_TUNABELLY:
		{
			int heal = skill_calc_heal(src, bl, skill_id, skill_lv, true);
			int heal_get_jobexp;

			if (status_isimmune(bl) || (dstmd && (status_get_class(bl) == MOBID_EMPERIUM || status_get_class_(bl) == CLASS_BATTLEFIELD)))
				heal=0;

			if (sd && dstsd && sd->status.partner_id == dstsd->status.char_id && (sd->class_&MAPID_UPPERMASK) == MAPID_SUPER_NOVICE && sd->status.sex == 0)
				heal = heal*2;

			if (dstsd && dstsd->sc.option&OPTION_MADOGEAR)
				heal = 0;//Mado's cant be healed. Only Repair can heal them.

			if (skill_id == AL_HEAL || skill_id == AB_HIGHNESSHEAL)
				status_change_end(bl, SC_BITESCAR, INVALID_TIMER);

			if( tsc && tsc->count )
			{
				if( tsc->data[SC_KAITE] && !(sstatus->mode&MD_STATUS_IMMUNE) )
				{ //Bounce back heal
					if (--tsc->data[SC_KAITE]->val2 <= 0)
						status_change_end(bl, SC_KAITE, INVALID_TIMER);
					if (src == bl)
						heal=0; //When you try to heal yourself under Kaite, the heal is voided.
					else {
						bl = src;
						dstsd = sd;
					}
				} else if (tsc->data[SC_BERSERK])
					heal = 0; //Needed so that it actually displays 0 when healing.
				else if (tsc->data[SC_AKAITSUKI] && ( skill_id == AL_HEAL || skill_id == AB_HIGHNESSHEAL ))
				{
					skill_akaitsuki_damage(src, bl, heal, skill_id, skill_lv, tick);
					break;
				}
			}
			heal_get_jobexp = status_heal(bl,heal,0,0);
			clif_skill_nodamage (src, bl, skill_id, heal, 1);

			if(sd && dstsd && heal > 0 && sd != dstsd && battle_config.heal_exp > 0){
				heal_get_jobexp = heal_get_jobexp * battle_config.heal_exp / 100;
				if (heal_get_jobexp <= 0)
					heal_get_jobexp = 1;
				pc_gainexp (sd, bl, 0, heal_get_jobexp, false);
			}
		}
		break;

	case PR_REDEMPTIO:
		if (sd && !(flag&1)) {
			if (sd->status.party_id == 0) {
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
			skill_area_temp[0] = 0;
			party_foreachsamemap(skill_area_sub,
				sd,skill_get_splash(skill_id, skill_lv),
				src,skill_id,skill_lv,tick, flag|BCT_PARTY|1,
				skill_castend_nodamage_id);
			if (skill_area_temp[0] == 0) {
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
			skill_area_temp[0] = 5 - skill_area_temp[0]; // The actual penalty...
			if (skill_area_temp[0] > 0 && !map[src->m].flag.noexppenalty) { //Apply penalty
				//If total penalty is 1% => reduced 0.2% penalty per each revived player
				unsigned int base_penalty = u32min(sd->status.base_exp, (pc_nextbaseexp(sd) * skill_area_temp[0] / 5) / 100);
				sd->status.base_exp -= base_penalty;
				clif_displayexp(sd, base_penalty, SP_BASEEXP, false, true);
				clif_updatestatus(sd,SP_BASEEXP);
				if (sd->state.showexp)
					pc_gainexp_disp(sd, base_penalty, pc_nextbaseexp(sd), 0, pc_nextjobexp(sd), true);
			}
			status_set_hp(src, 1, 0);
			status_set_sp(src, 0, 0);
			break;
		} else if (status_isdead(bl) && flag&1) { //Revive
			skill_area_temp[0]++; //Count it in, then fall-through to the Resurrection code.
			skill_lv = 3; //Resurrection level 3 is used
		} else //Invalid target, skip resurrection.
			break;

	case ALL_RESURRECTION:
		if(sd && (map_flag_gvg2(bl->m) || map[bl->m].flag.battleground))
		{	//No reviving in WoE grounds!
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			break;
		}
		if (!status_isdead(bl))
			break;
		{
			int per = 0, sper = 0;
			if (tsc && tsc->data[SC_HELLPOWER]) {
				clif_skill_nodamage(src, bl, ALL_RESURRECTION, skill_lv, 1);
				break;
			}

			if (map[bl->m].flag.pvp && dstsd && dstsd->pvp_point < 0)
				break;

			switch(skill_lv){
			case 1: per=10; break;
			case 2: per=30; break;
			case 3: per=50; break;
			case 4: per=80; break;
			}
			if(dstsd && dstsd->special_state.restart_full_recover)
				per = sper = 100;
			if (status_revive(bl, per, sper))
			{
				clif_skill_nodamage(src,bl,ALL_RESURRECTION,skill_lv,1); //Both Redemptio and Res show this skill-animation.
				if(sd && dstsd && battle_config.resurrection_exp > 0)
				{
					int exp = 0,jexp = 0;
					int lv = dstsd->status.base_level - sd->status.base_level, jlv = dstsd->status.job_level - sd->status.job_level;
					if(lv > 0 && pc_nextbaseexp(dstsd)) {
						exp = (int)((double)dstsd->status.base_exp * (double)lv * (double)battle_config.resurrection_exp / 1000000.);
						if (exp < 1) exp = 1;
					}
					if(jlv > 0 && pc_nextjobexp(dstsd)) {
						jexp = (int)((double)dstsd->status.job_exp * (double)lv * (double)battle_config.resurrection_exp / 1000000.);
						if (jexp < 1) jexp = 1;
					}
					if(exp > 0 || jexp > 0)
						pc_gainexp (sd, bl, exp, jexp, false);
				}
			}
		}
		break;

	case AL_DECAGI:
	case MER_DECAGI:
		if( tsc && !tsc->data[SC_ADORAMUS] ) //Prevent duplicate agi-down effect.
			clif_skill_nodamage (src, bl, skill_id, skill_lv,
				sc_start(bl, type, (50 + skill_lv * 3 + (status_get_lv(src) + sstatus->int_)/5), skill_lv, skill_get_time(skill_id,skill_lv)));
		break;

	case AL_CRUCIS:
		if (flag&1)
			sc_start(bl,type, 23+skill_lv*4 +status_get_lv(src) -status_get_lv(bl), skill_lv,skill_get_time(skill_id,skill_lv));
		else {
			map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id, skill_lv), BL_CHAR,
				src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
		break;

	case SP_SOULCURSE:
		if (flag&1)
			sc_start(bl, type, 30+10*skill_lv, skill_lv, skill_get_time(skill_id,skill_lv));
		else
		{
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
		break;

	case RL_FLICKER:
	{
		short x = bl->x, y = bl->y;

		i = skill_get_splash(skill_id, skill_lv);

		if (flag & 1)
		{// Only detonate the mines you stuck on others. Not by other player's / monster's.
			if ((dstsd && dstsd->sc.data[SC_H_MINE] && dstsd->sc.data[SC_H_MINE]->val1 == src->id) ||
				(dstmd && dstmd->sc.data[SC_H_MINE] && dstmd->sc.data[SC_H_MINE]->val1 == src->id))
			{
				flag = 0;// Flag reset.
				sc_start(bl, SC_H_MINE_SPLASH, 100, skill_lv, 100);// Explosion animation.

				skill_castend_damage_id(src, bl, RL_H_MINE, tsc->data[SC_H_MINE]->val3, tick, flag | 4);
			}
		}
		else
		{// Search for active howl mines and binding traps.
			map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag | BCT_ENEMY | 1, skill_castend_nodamage_id);
			map_foreachinarea(skill_flicker_bind_trap, src->m, x - i, y - i, x + i, y + i, BL_SKILL, src, tick);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
	}
	break;

	case PR_LEXDIVINA:
	case MER_LEXDIVINA:
		if( tsce )
			status_change_end(bl,type, INVALID_TIMER);
		else
			skill_addtimerskill(src, tick + 1000, bl->id, 0, 0, skill_id, skill_lv, 100, flag);
		clif_skill_nodamage (src, bl, skill_id, skill_lv, 1);
		break;

	case SA_ABRACADABRA:
		{
			int abra_skillid = 0, abra_skilllv;
			do {
				i = rnd() % MAX_SKILL_ABRA_DB;
				abra_skillid = skill_abra_db[i].skill_id;
			} while (abra_skillid == 0 ||
				skill_abra_db[i].req_lv > skill_lv || //Required lv for it to appear
				rnd()%10000 >= skill_abra_db[i].per
			);
			abra_skilllv = min(skill_lv, skill_get_max(abra_skillid));
			clif_skill_nodamage (src, bl, skill_id, skill_lv, 1);

			if( sd )
			{// player-casted
				sd->state.abra_flag = 1;
				sd->skillitem = abra_skillid;
				sd->skillitemlv = abra_skilllv;
				sd->skillitem_keep_requirement = false;
				clif_item_skill(sd, abra_skillid, abra_skilllv);
			}
			else
			{// mob-casted
				struct unit_data *ud = unit_bl2ud(src);
				int inf = skill_get_inf(abra_skillid);
				int target_id = 0;
				if (!ud) break;
				if (inf&INF_SELF_SKILL || inf&INF_SUPPORT_SKILL) {
					if (src->type == BL_PET)
						bl = (struct block_list*)((TBL_PET*)src)->master;
					if (!bl) bl = src;
					unit_skilluse_id(src, bl->id, abra_skillid, abra_skilllv);
				} else {	//Assume offensive skills
					if (ud->target)
						target_id = ud->target;
					else switch (src->type) {
						case BL_MOB: target_id = ((TBL_MOB*)src)->target_id; break;
						case BL_PET: target_id = ((TBL_PET*)src)->target_id; break;
					}
					if (!target_id)
						break;
					if (skill_get_casttype(abra_skillid) == CAST_GROUND) {
						bl = map_id2bl(target_id);
						if (!bl) bl = src;
						unit_skilluse_pos(src, bl->x, bl->y, abra_skillid, abra_skilllv);
					} else
						unit_skilluse_id(src, target_id, abra_skillid, abra_skilllv);
				}
			}
		}
		break;

	case SA_COMA:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,100,skill_lv,skill_get_time2(skill_id,skill_lv)));
		break;
	case SA_FULLRECOVERY:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		if (status_isimmune(bl))
			break;
		status_percent_heal(bl, 100, 100);
		break;
	case NPC_ALLHEAL:
		{
			int heal;
			if( status_isimmune(bl) )
				break;
			heal = status_percent_heal(bl, 100, 0);
			clif_skill_nodamage(NULL, bl, AL_HEAL, heal, 1);
			if( dstmd )
			{ // Reset Damage Logs
				memset(dstmd->dmglog, 0, sizeof(dstmd->dmglog));
				dstmd->tdmg = 0;
			}
		}
		break;
	case SA_SUMMONMONSTER:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		if (sd) mob_once_spawn(sd,src->m,src->x,src->y,"--ja--",-1,1,"",0,AI_NONE);
		break;
	case SA_LEVELUP:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		if (sd && pc_nextbaseexp(sd)) pc_gainexp(sd, NULL, pc_nextbaseexp(sd) * 10 / 100, 0, false);
		break;
	case SA_INSTANTDEATH:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		status_kill(src);
		break;
	case SA_QUESTION:
		clif_emotion(src, E_WHAT);
	case SA_GRAVITY:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;
	case SA_CLASSCHANGE:
	case SA_MONOCELL:
		if (dstmd)
		{
			int class_;
			if (sd && status_has_mode(&dstmd->status, MD_STATUS_IMMUNE))
			{
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
			class_ = skill_id==SA_MONOCELL?1002:mob_get_random_id(4, 1, 0);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			mob_class_change(dstmd,class_);
			if (tsc && status_has_mode(&dstmd->status, MD_STATUS_IMMUNE))
			{
				const enum sc_type scs[] = { SC_QUAGMIRE, SC_PROVOKE, SC_ROKISWEIL, SC_GRAVITATION, SC_SUITON, SC_STRIPWEAPON, SC_STRIPSHIELD, SC_STRIPARMOR, SC_STRIPHELM, SC_BLADESTOP };
				for (i = SC_COMMON_MIN; i <= SC_COMMON_MAX; i++)
					if (tsc->data[i]) status_change_end(bl, (sc_type)i, INVALID_TIMER);
				for (i = 0; i < ARRAYLENGTH(scs); i++)
					if (tsc->data[scs[i]]) status_change_end(bl, scs[i], INVALID_TIMER);
			}
		}
		break;
	case SA_DEATH:
		if (sd && dstmd && status_has_mode(&dstmd->status, MD_STATUS_IMMUNE)) {
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			break;
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		status_kill(bl);
		break;
	case SA_REVERSEORCISH:
	case ALL_REVERSEORCISH:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id, skill_lv)));
		break;
	case SA_FORTUNE:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		if(sd) pc_getzeny(sd,status_get_lv(bl)*100,LOG_TYPE_STEAL,NULL);
		break;
	case SA_TAMINGMONSTER:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		if (sd && dstmd) {
			ARR_FIND( 0, MAX_PET_DB, i, dstmd->mob_id == pet_db[i].class_ );
			if( i < MAX_PET_DB )
				pet_catch_process1(sd, dstmd->mob_id);
		}
		break;

	case CR_PROVIDENCE:
		if(sd && dstsd){ //Check they are not another crusader [Skotlex]
			if ((dstsd->class_&MAPID_UPPERMASK) == MAPID_CRUSADER) {
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				map_freeblock_unlock();
				return 1;
			}
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		break;

	case CG_MARIONETTE:
		{
			struct status_change* sc = status_get_sc(src);

			if( sd && dstsd && (dstsd->class_&MAPID_UPPERMASK) == MAPID_BARDDANCER && dstsd->status.sex == sd->status.sex )
			{// Cannot cast on another bard/dancer-type class of the same gender as caster
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				map_freeblock_unlock();
				return 1;
			}

			if( sc && tsc )
			{
				if( !sc->data[SC_MARIONETTE] && !tsc->data[SC_MARIONETTE2] )
				{
					sc_start(src,SC_MARIONETTE,100,bl->id,skill_get_time(skill_id,skill_lv));
					sc_start(bl,SC_MARIONETTE2,100,src->id,skill_get_time(skill_id,skill_lv));
					clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
				}
				else
				if(  sc->data[SC_MARIONETTE ] &&  sc->data[SC_MARIONETTE ]->val1 == bl->id &&
					tsc->data[SC_MARIONETTE2] && tsc->data[SC_MARIONETTE2]->val1 == src->id )
				{
					status_change_end(src, SC_MARIONETTE, INVALID_TIMER);
					status_change_end(bl, SC_MARIONETTE2, INVALID_TIMER);
				}
				else
				{
					if( sd )
						clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);

					map_freeblock_unlock();
					return 1;
				}
			}
		}
		break;

	case RG_CLOSECONFINE:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start4(bl,type,100,skill_lv,src->id,0,0,skill_get_time(skill_id,skill_lv)));
		break;
	case SA_FLAMELAUNCHER:	// added failure chance and chance to break weapon if turned on [Valaris]
	case SA_FROSTWEAPON:
	case SA_LIGHTNINGLOADER:
	case SA_SEISMICWEAPON:
		if (dstsd) {
			if(dstsd->status.weapon == W_FIST ||
				(dstsd->sc.count && !dstsd->sc.data[type] &&
				(	//Allow re-enchanting to lenghten time. [Skotlex]
					dstsd->sc.data[SC_FIREWEAPON] ||
					dstsd->sc.data[SC_WATERWEAPON] ||
					dstsd->sc.data[SC_WINDWEAPON] ||
					dstsd->sc.data[SC_EARTHWEAPON] ||
					dstsd->sc.data[SC_SHADOWWEAPON] ||
					dstsd->sc.data[SC_GHOSTWEAPON] ||
					dstsd->sc.data[SC_ENCPOISON]
				))
				) {
				if (sd) clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				clif_skill_nodamage(src,bl,skill_id,skill_lv,0);
				break;
			}
		}
		// 100% success rate at lv4 & 5, but lasts longer at lv5
		if(!clif_skill_nodamage(src,bl,skill_id,skill_lv, sc_start(bl,type,(60+skill_lv*10),skill_lv, skill_get_time(skill_id,skill_lv)))) {
			if (sd)
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			if (skill_break_equip(bl, EQP_WEAPON, 10000, BCT_PARTY) && sd && sd != dstsd)
				clif_displaymessage(sd->fd,"You broke target's weapon");
		}
		break;

	case PR_ASPERSIO:
		if (sd && dstmd) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,0);
			break;
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		break;

	case ITEM_ENCHANTARMS:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start2(bl,type,100,skill_lv,
				skill_get_ele(skill_id,skill_lv), skill_get_time(skill_id,skill_lv)));
		break;

	case TK_SEVENWIND:
		switch(skill_get_ele(skill_id,skill_lv)) {
			case ELE_EARTH : type = SC_EARTHWEAPON;  break;
			case ELE_WIND  : type = SC_WINDWEAPON;   break;
			case ELE_WATER : type = SC_WATERWEAPON;  break;
			case ELE_FIRE  : type = SC_FIREWEAPON;   break;
			case ELE_GHOST : type = SC_GHOSTWEAPON;  break;
			case ELE_DARK  : type = SC_SHADOWWEAPON; break;
			case ELE_HOLY  : type = SC_ASPERSIO;     break;
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));

		sc_start(bl,SC_SEVENWIND,100,skill_lv,skill_get_time(skill_id,skill_lv));

		break;

	case PR_KYRIE:
	case MER_KYRIE:
	case SU_GROOMING:
	case SU_CHATTERING:
		clif_skill_nodamage(bl,bl,skill_id,skill_lv,
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		break;
	//Passive Magnum, should had been casted on yourself.
	case SM_MAGNUM:
	case MS_MAGNUM:
		skill_area_temp[1] = 0;
		map_foreachinshootrange(skill_area_sub, src, skill_get_splash(skill_id, skill_lv), BL_SKILL|BL_CHAR,
			src,skill_id,skill_lv,tick, flag|BCT_ENEMY|1, skill_castend_damage_id);
		clif_skill_nodamage (src,src,skill_id,skill_lv,1);
		//Initiate 10% of your damage becomes fire element.
		sc_start4(src,SC_WATK_ELEMENT,100,3,20,0,0,skill_get_time2(skill_id, skill_lv));
		if (sd)
			skill_blockpc_start(sd, skill_id, skill_get_time(skill_id, skill_lv));
		else if (bl->type == BL_MER)
			skill_blockmerc_start((TBL_MER*)bl, skill_id, skill_get_time(skill_id, skill_lv));
		break;

	case TK_JUMPKICK:
		/* Check if the target is an enemy; if not, skill should fail so the character doesn't unit_movepos (exploitable) */
		if (battle_check_target(src, bl, BCT_ENEMY) > 0) {
			if (unit_movepos(src, bl->x, bl->y, 2, 1)) {
				skill_attack(BF_WEAPON, src, src, bl, skill_id, skill_lv, tick, flag);
				clif_blown(src);
			}
		}
		else
			clif_skill_fail(sd, skill_id, USESKILL_FAIL, 0, 0);
		break;

	case AL_INCAGI:
	case AL_BLESSING:
	case MER_INCAGI:
	case MER_BLESSING:
		if (dstsd != NULL && tsc->data[SC_CHANGEUNDEAD]) {
			skill_attack(BF_MISC,src,src,bl,skill_id,skill_lv,tick,flag);
			break;
		}
	case PR_SLOWPOISON:
	case PR_IMPOSITIO:
	case PR_LEXAETERNA:
	case PR_SUFFRAGIUM:
	case PR_BENEDICTIO:
	case LK_BERSERK:
	case MS_BERSERK:
	case KN_TWOHANDQUICKEN:
	case KN_ONEHAND:
	case MER_QUICKEN:
	case CR_SPEARQUICKEN:
	case CR_REFLECTSHIELD:
	case MS_REFLECTSHIELD:
	case AS_POISONREACT:
	case MC_LOUD:
	case MG_ENERGYCOAT:
	case MO_EXPLOSIONSPIRITS:
	case MO_STEELBODY:
	case MO_BLADESTOP:
	case LK_AURABLADE:
	case LK_PARRYING:
	case MS_PARRYING:
	case LK_CONCENTRATION:
	case WS_CARTBOOST:
	case SN_SIGHT:
	case WS_MELTDOWN:
	case WS_OVERTHRUSTMAX:
	case ST_REJECTSWORD:
	case HW_MAGICPOWER:
	case PF_MEMORIZE:
	case PA_SACRIFICE:
	case PF_DOUBLECASTING:
	case SG_SUN_COMFORT:
	case SG_MOON_COMFORT:
	case SG_STAR_COMFORT:
	case GS_MADNESSCANCEL:
	case GS_ADJUSTMENT:
	case GS_INCREASING:
	case NJ_KASUMIKIRI:
	case NJ_UTSUSEMI:
	case NJ_NEN:
	case NPC_DEFENDER:
	case NPC_MAGICMIRROR:
	case ST_PRESERVE:
	case NPC_INVINCIBLE:
	case NPC_INVINCIBLEOFF:
	case RK_DEATHBOUND:
	case GC_VENOMIMPRESS:
	case AB_DUPLELIGHT:
	case AB_SECRAMENT:
	case RA_FEARBREEZE:
	case RA_CAMOUFLAGE:
	case NC_ACCELERATION:
	case NC_HOVERING:
	case NC_SHAPESHIFT:
	case SC_INVISIBILITY:
	case SC_DEADLYINFECT:
	case LG_EXEEDBREAK:
	case LG_PRESTIGE:
	case SR_CRESCENTELBOW:
	case SR_LIGHTNINGWALK:
	case SR_GENTLETOUCH_ENERGYGAIN:
	case SR_GENTLETOUCH_CHANGE:
	case SR_GENTLETOUCH_REVITALIZE:
	case GN_CARTBOOST:
	case WL_RECOGNIZEDSPELL:
	case ALL_ODINS_POWER:
	case RL_E_CHAIN:
	case RL_HEAT_BARREL:
	case SJ_LIGHTOFMOON:
	case SJ_LIGHTOFSTAR:
	case SJ_FALLINGSTAR:
	case SJ_BOOKOFDIMENSION:
	case SJ_LIGHTOFSUN:
	case SP_SOULREAPER:
	case KO_IZAYOI:
	case RA_UNLIMIT:
	case WL_TELEKINESIS_INTENSE:
	case ALL_FULL_THROTTLE:
	case SU_ARCLOUSEDASH:
	case SU_FRESHSHRIMP:
		clif_skill_nodamage( src, bl, skill_id, skill_lv, sc_start( bl, type, 100, skill_lv, skill_get_time( skill_id, skill_lv ) ) );
		break;

	case NPC_HALLUCINATION:
		clif_skill_nodamage(src, bl, skill_id, skill_lv,
			status_change_start(src, bl, type, (skill_lv * 20)*100, skill_lv, 0, 0, 0, skill_get_time(skill_id, skill_lv), 0));
		break;

	case EL_FIRE_CLOAK:
	case EL_WATER_SCREEN:
	case EL_WATER_DROP:
	case EL_WIND_STEP:
	case EL_WIND_CURTAIN:
	case EL_SOLID_SKIN:
	case EL_STONE_SHIELD:
	case EL_PYROTECHNIC:
	case EL_HEATER:
	case EL_TROPIC:
	case EL_AQUAPLAY:
	case EL_COOLER:
	case EL_CHILLY_AIR:
	case EL_GUST:
	case EL_BLAST:
	case EL_WILD_STORM:
	case EL_PETROLOGY:
	case EL_CURSED_SOIL:
	case EL_UPHEAVAL:
		if ( sd )// Show only the animation if casted by player to avoid problems.
			clif_skill_damage(src, bl, tick, status_get_amotion(src), 0, 0, 1, skill_id, skill_lv, 6);
		else
		{// Activate the status only if casted by a elemental since it checks for its master on startup.
			clif_skill_damage(src, &ed->master->bl, tick, status_get_amotion(src), 0, 0, 1, skill_id, skill_lv, 6);
			sc_start(bl, type, 100, skill_lv, skill_get_time(skill_id,skill_lv));

			if( skill_id == EL_WIND_STEP && !map_flag_gvg(src->m) && !map[src->m].flag.battleground )
			{// No position jumping on GvG maps.
				short x, y;
				map_search_freecell(&ed->master->bl, 0, &x, &y, 4, 4, 0);
				if (unit_movepos(&ed->master->bl, x, y, 0, 0))
					clif_slide(&ed->master->bl,ed->master->bl.x,ed->master->bl.y);
			}
		}
		break;

	case SU_SV_ROOTTWIST:
		clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		sc_start2(bl, type, 100, skill_lv, src->id, skill_get_time(skill_id, skill_lv));
		break;

	case SJ_GRAVITYCONTROL:
		{
			int fall_damage = (sstatus->batk+sstatus->rhw.atk) - tstatus->def2;

			if ( bl->type == BL_PC )
				fall_damage += dstsd->weight/10 - tstatus->def;
			else// Monster's don't have weight. Put something in its place.
				fall_damage += 50 * status_get_base_lv_effect(bl) - tstatus->def;

			if ( fall_damage < 1 )
				fall_damage = 1;

			clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start2(bl,type,100,skill_lv,fall_damage,skill_get_time(skill_id,skill_lv)));
		}
		break;

	case KO_MEIKYOUSISUI:
		{
			const enum sc_type scs[] = { SC_POISON, SC_CURSE, SC_SILENCE, SC_BLIND, SC_FEAR, SC_BURNING, SC_FROST, SC_CRYSTALIZE };
			signed char remove_attempt = 100;// Safety to prevent infinite looping in the randomizer.
			bool debuff_active = false;// Flag if debuff is found to be active.
			bool debuff_removed = false;// Flag when a debuff was removed.

			i = 0;

			// Don't send the ZC_USE_SKILL packet or it will lock up the player's sprite when the forced sitting happens.
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));

			// Remove a random debuff.
			if (sc)
			{// Check if any of the listed passable debuffs are active.
				for (i = 0; i < ARRAYLENGTH(scs); i++)
				{
					if (sc->data[scs[i]])
					{// If a debuff is found, mark true.
						debuff_active = true;
						break;// End the check.
					}
				}

				// Debuff found? 1 or more of them are likely active.
				if ( debuff_active )
					while ( debuff_removed == false && remove_attempt > 0 )
					{// Randomly select a possible debuff and see if its active.
						i = rnd()%8;

						// Selected debuff active? If yes then remove it and mark it was removed.
						if (sc->data[scs[i]])
						{
							status_change_end(bl, scs[i], INVALID_TIMER);
							debuff_removed = true;
						}
						else// Failed to remove.
							remove_attempt--;
					}
			}

		}
		break;

	case AB_OFFERTORIUM:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));

		if ( sc )
		{
			const enum sc_type scs[] = { SC_POISON, SC_CURSE, SC_CONFUSION, SC_BLIND, SC_BLEEDING, SC_BURNING, SC_FROST, SC_HALLUCINATION, SC_MANDRAGORA };

			// Checking for Guillotine poisons.
			for (i = SC_NEW_POISON_MIN; i <= SC_NEW_POISON_MAX; i++)
				if (sc->data[i])
					status_change_end(bl, (sc_type)i, INVALID_TIMER);
			// Checking for additional status's.
			for (i = 0; i < ARRAYLENGTH(scs); i++)
				if (sc->data[scs[i]])
					status_change_end(bl, scs[i], INVALID_TIMER);
		}
		break;

	case SC_ESCAPE:
		skill_castend_pos2(src, src->x, src->y, HT_ANKLESNARE, 5, tick, flag);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		skill_blown(src,bl,skill_get_blewcount(skill_id,skill_lv),unit_getdir(bl),0);
		break;

	case SR_FLASHCOMBO:
		// Duration of status is set to 4x the amotion to make it last just long enough to apply ATK boost to all 4 skills.
		clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(src,type,100,skill_lv,4 * status_get_amotion(src)));
		skill_addtimerskill(src, tick, bl->id, 0, 0, SR_DRAGONCOMBO, (sd ? pc_checkskill(sd, SR_DRAGONCOMBO) : 10), BF_WEAPON, flag);
		skill_addtimerskill(src, tick + status_get_amotion(src), bl->id, 0, 0, SR_FALLENEMPIRE, (sd ? pc_checkskill(sd, SR_FALLENEMPIRE) : 10), BF_WEAPON, flag);
		skill_addtimerskill(src, tick + (2 * status_get_amotion(src)), bl->id, 0, 0, SR_TIGERCANNON, (sd ? pc_checkskill(sd, SR_TIGERCANNON) : 10), BF_WEAPON, flag);
		skill_addtimerskill(src, tick + (3 * status_get_amotion(src)), bl->id, 0, 0, SR_SKYNETBLOW, (sd ? pc_checkskill(sd, SR_SKYNETBLOW) : 5), BF_WEAPON, flag);
		break;

	case RL_P_ALTER:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		sc_start4(bl,SC_KYRIE,100,skill_lv,0,0,skill_id,skill_get_time(skill_id,skill_lv));
		break;

	// Works just like the above list of skills, except animation caused by
	// status must trigger AFTER the skill cast animation or it will cancel
	// out the status's animation.
	case SU_STOOP:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case KN_AUTOCOUNTER:
		sc_start(bl, type, 100, skill_lv, skill_get_time(skill_id, skill_lv));
		skill_addtimerskill(src, tick + 100, bl->id, 0, 0, skill_id, skill_lv, BF_WEAPON, flag);
		break;
	case SO_STRIKING:
		{
			int bonus = 0;
			if ( dstsd )
			{
				short index = dstsd->equip_index[EQI_HAND_R];
				if( index >= 0 && dstsd->inventory_data[index] && dstsd->inventory_data[index]->type == IT_WEAPON )
				bonus = (8 + 2 * skill_lv) * dstsd->inventory_data[index]->wlv;
			}
			bonus += 5 * ((sd ? pc_checkskill(sd, SA_FLAMELAUNCHER) : 5) + (sd ? pc_checkskill(sd, SA_FROSTWEAPON) : 5) + (sd ? pc_checkskill(sd, SA_LIGHTNINGLOADER) : 5) + (sd ? pc_checkskill(sd, SA_SEISMICWEAPON) : 5));
			clif_skill_nodamage( src, bl, skill_id, skill_lv, sc_start2( bl, type, 100, skill_lv, bonus, skill_get_time( skill_id, skill_lv ) ) );
		}
		break;
	case NPC_STOP:
		if( clif_skill_nodamage( src, bl, skill_id, skill_lv, sc_start2( bl, type, 100, skill_lv, src->id, skill_get_time( skill_id, skill_lv ) ) ) )
			sc_start2( src, type, 100, skill_lv, bl->id, skill_get_time( skill_id, skill_lv ) );
		break;
	case HP_ASSUMPTIO:
		if( sd && dstmd )
			clif_skill_fail( sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0 );
		else
			clif_skill_nodamage( src, bl, skill_id, skill_lv, sc_start( bl, type, 100, skill_lv, skill_get_time( skill_id, skill_lv ) ) );
		break;
	case MG_SIGHT:
	case MER_SIGHT:
	case AL_RUWACH:
	case WZ_SIGHTBLASTER:
	case NPC_WIDESIGHT:
	case NPC_STONESKIN:
	case NPC_ANTIMAGIC:
		clif_skill_nodamage( src, bl, skill_id, skill_lv, sc_start2( bl, type, 100, skill_lv, skill_id, skill_get_time( skill_id, skill_lv ) ) );
		break;
	case NJ_BUNSINJYUTSU:
		status_change_end(bl, SC_BUNSINJYUTSU, INVALID_TIMER); // on official recasting cancels existing mirror image [helvetica]
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		status_change_end(bl, SC_NEN, INVALID_TIMER);
		break;
/* Was modified to only affect targetted char.	[Skotlex]
	case HP_ASSUMPTIO:
		if (flag&1)
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
		else
		{
			map_foreachinrange(skill_area_sub, bl,
				skill_get_splash(skill_id, skill_lv), BL_PC,
				src, skill_id, skill_lv, tick, flag|BCT_ALL|1,
				skill_castend_nodamage_id);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;
*/
	case SM_ENDURE:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		break;

	case AS_ENCHANTPOISON: // Prevent spamming [Valaris]
		if (sd && dstsd && dstsd->sc.count) {
			if (dstsd->sc.data[SC_FIREWEAPON] ||
				dstsd->sc.data[SC_WATERWEAPON] ||
				dstsd->sc.data[SC_WINDWEAPON] ||
				dstsd->sc.data[SC_EARTHWEAPON] ||
				dstsd->sc.data[SC_SHADOWWEAPON] ||
				dstsd->sc.data[SC_GHOSTWEAPON]
			//	dstsd->sc.data[SC_ENCPOISON] //People say you should be able to recast to lengthen the timer. [Skotlex]
			) {
					clif_skill_nodamage(src,bl,skill_id,skill_lv,0);
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
					break;
			}
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		break;

	case LK_TENSIONRELAX:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start4(bl,type,100,skill_lv,0,0,skill_get_time2(skill_id,skill_lv),
				skill_get_time(skill_id,skill_lv)));
		break;

	case MC_CHANGECART:
	case MC_CARTDECORATE:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;

	case TK_MISSION:
		if (sd) {
			int id;
			if (sd->mission_mobid && (sd->mission_count || rnd()%100)) { //Cannot change target when already have one
				clif_mission_info(sd, sd->mission_mobid, sd->mission_count);
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
			id = mob_get_random_id(0,0xF, sd->status.base_level);
			if (!id) {
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
			sd->mission_mobid = id;
			sd->mission_count = 0;
			pc_setglobalreg(sd,"TK_MISSION_ID", id);
			clif_mission_info(sd, id, 0);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case AC_CONCENTRATION:
		{
			int splash = skill_get_splash(skill_id, skill_lv);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,
				sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
			skill_reveal_trap_inarea(src, splash, src->x, src->y);
			map_foreachinrange( status_change_timer_sub, src,
				splash, BL_CHAR, src, NULL, type, tick);
		}
		break;

	case SM_PROVOKE:
	case SM_SELFPROVOKE:
	case MER_PROVOKE:
		if (status_has_mode(tstatus, MD_STATUS_IMMUNE) || battle_check_undead(tstatus->race, tstatus->def_ele)) {
			map_freeblock_unlock();
			return 1;
		}
		// Official chance is 70% + 3%*skill_lv + srcBaseLevel% - tarBaseLevel%
		if (!(i = status_change_start(src, bl, type, skill_id == SM_SELFPROVOKE ? 10000 : ((70 + 3 * skill_lv + status_get_lv(src) - status_get_lv(bl))*100), skill_lv, 0, 0, 0, skill_get_time(skill_id, skill_lv), 0)))
		{
			if( sd )
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			map_freeblock_unlock();
			return 0;
		}
		clif_skill_nodamage(src, bl, skill_id == SM_SELFPROVOKE ? SM_PROVOKE : skill_id, skill_lv, i);
		unit_skillcastcancel(bl, 2);

		if( tsc && tsc->count )
		{
			status_change_end(bl, SC_FREEZE, INVALID_TIMER);
			if( tsc->data[SC_STONE] && tsc->opt1 == OPT1_STONE )
				status_change_end(bl, SC_STONE, INVALID_TIMER);
			status_change_end(bl, SC_SLEEP, INVALID_TIMER);
			status_change_end(bl, SC_TRICKDEAD, INVALID_TIMER);
		}

		if( dstmd )
		{
			dstmd->state.provoke_flag = src->id;
			mob_target(dstmd, src, skill_get_range2(src,skill_id,skill_lv,true));
		}
		break;

	case ML_DEVOTION:
	case CR_DEVOTION:
		{
			int count, lv;
			if( !dstsd || (!sd && !mer) )
			{ // Only players can be devoted
				if( sd )
					clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0,0);
				break;
			}

			if( (lv = status_get_lv(src) - dstsd->status.base_level) < 0 )
				lv = -lv;
			if( lv > battle_config.devotion_level_difference || // Level difference requeriments
				(dstsd->sc.data[type] && dstsd->sc.data[type]->val1 != src->id) || // Cannot Devote a player devoted from another source
				(skill_id == ML_DEVOTION && (!mer || mer != dstsd->md)) || // Mercenary only can devote owner
				(dstsd->class_&MAPID_UPPERMASK) == MAPID_CRUSADER || // Crusader Cannot be devoted
				(dstsd->sc.data[SC_HELLPOWER])) // Players affected by SC_HELLPOWERR cannot be devoted.
			{
				if( sd )
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				map_freeblock_unlock();
				return 1;
			}

			i = 0;
			count = (sd)? min(skill_lv,5) : 1; // Mercenary only can Devote owner
			if( sd )
			{ // Player Devoting Player
				ARR_FIND(0, count, i, sd->devotion[i] == bl->id );
				if( i == count )
				{
					ARR_FIND(0, count, i, sd->devotion[i] == 0 );
					if( i == count )
					{ // No free slots, skill Fail
						clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0,0);
						map_freeblock_unlock();
						return 1;
					}
				}

				sd->devotion[i] = bl->id;
			}
			else
				mer->devotion_flag = 1; // Mercenary Devoting Owner

			clif_skill_nodamage(src, bl, skill_id, skill_lv,
				sc_start4(bl, type, 100, src->id, i, skill_get_range2(src,skill_id,skill_lv,true),0, skill_get_time2(skill_id, skill_lv)));
			clif_devotion(src, NULL);
		}
		break;

	case SR_CURSEDCIRCLE:
		{// Max enemies that can be locked in the circle.
			short count = min(sd?1+sd->spiritball:15,MAX_CURSED_CIRCLES);
			short spheres_used = 0;

			if ( flag&1 )
			{// Limit to only allowing players and mobs to use this skill. Also affect only players and mobs.
				if( (!dstsd && !dstmd) || !sd && !md )
					break;

				// Boss monster's arn't affected.
				if ( tstatus->mode&MD_STATUS_IMMUNE )
					break;

				if( (dstsd && dstsd->sc.data[type] && dstsd->sc.data[type]->val1 != src->id) || // Cant curse a player that was already cursed from another source.
					(dstmd && dstmd->sc.data[type] && dstmd->sc.data[type]->val1 != src->id) )// Same as the above check, but for monsters.
				{
					map_freeblock_unlock();
					return 1;
				}

				i = 0;
				if( sd )
				{// Curse the enemy.
					ARR_FIND(0, count, i, sd->cursed_circle[i] == bl->id );
					if( i == count )
					{
						ARR_FIND(0, count, i, sd->cursed_circle[i] == 0 );
						if( i == count )
						{// No more free slots? Fail.
							map_freeblock_unlock();
							return 1;
						}
					}

					sd->cursed_circle[i] = bl->id;
				}

				// Duration is set 500ms higher then what the atker version gets.
				// This is to give the atker status time to end the target status on all affected targets when it ends.
				// Should that fail to happen for some reason, the target status ends half a second after.
				sc_start4(bl, type, 100, src->id, i, skill_lv, 0, 500+skill_get_time(skill_id, skill_lv));
			}
			else if ( sd )
			{// Display animation and give self the attacker status.
				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
				sc_start(bl, SC_CURSEDCIRCLE_ATKER, 100, skill_lv, skill_get_time(skill_id, skill_lv));

				// Count the number of enemys in range to know how many spirit sphere's will be used.
				spheres_used = map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id, skill_lv), BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_area_sub_count);

				// Give the cursed circle status to the enemys and then consume the spirit spheres.
				map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id, skill_lv), BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
				pc_delspiritball(sd, spheres_used, 0);
			}
		}
		break;

	case GN_BLOOD_SUCKER:
		{// Max allowed targets to be sucked on.
			short count = MAX_BLOOD_SUCKERS;

			// Only players and monsters can be sucked on....I think??? [Rytech]
			// Lets only allow players and monsters to use this skill for safety reasons.
			if( (!dstsd && !dstmd) || !sd && !md )
			{
				if( sd )
					clif_skill_fail(sd, skill_id, 0, 0, 0);
				break;
			}

			// Check if the target is already marked by another source.
			if( (dstsd && dstsd->sc.data[type] && dstsd->sc.data[type]->val1 != src->id) || // Cant suck on a player that was already marked from another source.
				(dstmd && dstmd->sc.data[type] && dstmd->sc.data[type]->val1 != src->id) )// Same as the above check, but for monsters.
			{
				if( sd )
					clif_skill_fail(sd,skill_id,0,0,0);
				map_freeblock_unlock();
				return 1;
			}

			i = 0;
			if( sd )
			{// Sucking on the target.
				ARR_FIND(0, count, i, sd->blood_sucker[i] == bl->id );
				if( i == count )
				{
					ARR_FIND(0, count, i, sd->blood_sucker[i] == 0 );
					if( i == count )
					{// Max number of targets marked. Fail the skill.
						clif_skill_fail(sd, skill_id, 0, 0, 0);
						map_freeblock_unlock();
						return 1;
					}
				}
				// Add the ID of the marked target to the player's sucker list.
				sd->blood_sucker[i] = bl->id;

				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
				sc_start4(bl, type, 100, src->id, i, skill_lv, 0, skill_get_time(skill_id, skill_lv));
			}
			else if ( md )// Monster's cant track with this skill. Just give the status.
				clif_skill_nodamage(src, bl, skill_id, skill_lv,
					sc_start4(bl, type, 100, 0, 0, skill_lv, 0, skill_get_time(skill_id, skill_lv)));
		}
		break;

	case RL_C_MARKER:
		{// Max allowed targets to be marked.
			short count = MAX_CRIMSON_MARKS;

			// Only players and monsters can be marked....I think??? [Rytech]
			// Lets only allow players and monsters to use this skill for safety reasons.
			if( (!dstsd && !dstmd) || !sd && !md )
			{
				if( sd )
					clif_skill_fail(sd, skill_id, 0, 0, 0);
				break;
			}

			// Check if the target is already marked by another source.
			if( (dstsd && dstsd->sc.data[type] && dstsd->sc.data[type]->val1 != src->id) || // Cant mark a player that was already marked from another source.
				(dstmd && dstmd->sc.data[type] && dstmd->sc.data[type]->val1 != src->id) )// Same as the above check, but for monsters.
			{
				if( sd )
					clif_skill_fail(sd,skill_id,0,0,0);
				map_freeblock_unlock();
				return 1;
			}

			i = 0;
			if( sd )
			{// Marking the target.
				ARR_FIND(0, count, i, sd->crimson_mark[i] == bl->id );
				if( i == count )
				{
					ARR_FIND(0, count, i, sd->crimson_mark[i] == 0 );
					if( i == count )
					{// Max number of targets marked. Fail the skill.
						clif_skill_fail(sd, skill_id, 0, 0, 0);
						map_freeblock_unlock();
						return 1;
					}
				}
				// Add the ID of the marked target to the player's marking list.
				sd->crimson_mark[i] = bl->id;

				// Val3 flags if the status was applied by a player or a monster.
				// This will be important for other skills that work together with this one.
				// 1 = Player, 2 = Monster.
				clif_skill_nodamage(src, bl, skill_id, skill_lv,
					sc_start4(bl, type, 100, src->id, i, 1, 1000, skill_get_time(skill_id, skill_lv)));
			}
			else if ( md )// Monster's cant track with this skill. Just give the status.
				clif_skill_nodamage(src, bl, skill_id, skill_lv,
					sc_start4(bl, type, 100, 0, 0, 2, 0, skill_get_time(skill_id, skill_lv)));
		}
		break;

	case SP_SOULUNITY:
		{// Max allowed targets to be in unity.
			short count = min(5+skill_lv,MAX_UNITED_SOULS);

			if ( sd == NULL || sd->status.party_id == 0 || (flag & 1) )
			{// Only put player's souls in unity.
				if( !dstsd || !sd )
					break;

				if (dstsd->sc.data[type] && dstsd->sc.data[type]->val1 != src->id)
				{// Fail if a player is in unity with another source.
					map_freeblock_unlock();
					return 1;
				}

				i = 0;
				if( sd )
				{// Unite player's soul with caster's soul.
					ARR_FIND(0, count, i, sd->united_soul[i] == bl->id );
					if( i == count )
					{
						ARR_FIND(0, count, i, sd->united_soul[i] == 0 );
						if( i == count )
						{// No more free slots? Fail.
							map_freeblock_unlock();
							return 1;
						}
					}

					sd->united_soul[i] = bl->id;
				}

				clif_skill_nodamage(src, bl, skill_id, skill_lv,
					sc_start4(bl, type, 100, src->id, i, skill_lv, 0, skill_get_time(skill_id, skill_lv)));
			}
			else if (sd)
			{
				party_foreachsamemap(skill_area_sub, sd, skill_get_splash(skill_id, skill_lv), src, skill_id, skill_lv, tick, flag | BCT_PARTY | 1, skill_castend_nodamage_id);
			}
		}
		break;

	case MO_CALLSPIRITS:
		if (sd)
		{
			if (!sd->sc.data[SC_RAISINGDRAGON])
				pc_addspiritball(sd, skill_get_time(skill_id, skill_lv), skill_lv);
			else
			{ // Call Spirit can summon more than its max level if under raising dragon status. [Jobbie]
				short rd_lvl = sd->sc.data[SC_RAISINGDRAGON]->val1;
				pc_addspiritball(sd, skill_get_time(skill_id, skill_lv), skill_lv + rd_lvl);
			}
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
		break;

	case MH_STYLE_CHANGE:
		if(hd)
		{// Fighter <---> Grappler style switch.
			if ( hd->sc.data[SC_STYLE_CHANGE] )
			{
				if ( hd->sc.data[SC_STYLE_CHANGE]->val1 == FIGHTER_STYLE )
				{// Change from fighter to grappler style.
					status_change_end(bl,type,INVALID_TIMER);
					sc_start(bl,type,100,GRAPPLER_STYLE,-1);
				}
				else if ( hd->sc.data[SC_STYLE_CHANGE]->val1 == GRAPPLER_STYLE )
				{// Change from grappler to fighter style.
					status_change_end(bl,type,INVALID_TIMER);
					sc_start(bl,type,100,FIGHTER_STYLE,-1);
				}
			}
			else// If for some reason no style is active, start in fighter style.
				sc_start(bl,type,100,FIGHTER_STYLE,-1);

			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case CH_SOULCOLLECT:
		if (sd)
		{
			if (!sd->sc.data[SC_RAISINGDRAGON])
			{
				for( i = 0; i < 5; i++ )
					pc_addspiritball(sd, skill_get_time(skill_id, skill_lv), 5);
			}
			else
			{ //As Tested in kRO Soul Collect will summon 5 + raising dragon level spirit balls directly. [Jobbie]
				for (i = 0; i < 15; i++)
					pc_addspiritball(sd, skill_get_time(skill_id, skill_lv), 5 + sd->sc.data[SC_RAISINGDRAGON]->val1);
			}
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
		break;

	case MO_KITRANSLATION:
		if (dstsd && ((dstsd->class_&MAPID_BASEMASK) != MAPID_GUNSLINGER || (dstsd->class_&MAPID_UPPERMASK) != MAPID_REBELLION)) {
			pc_addspiritball(dstsd,skill_get_time(skill_id,skill_lv),5);
		}
		break;

	case TK_TURNKICK:
	case MO_BALKYOUNG: //Passive part of the attack. Splash knock-back+stun. [Skotlex]
		if (skill_area_temp[1] != bl->id) {
			skill_blown(src,bl,skill_get_blewcount(skill_id,skill_lv),-1,0);
			skill_additional_effect(src,bl,skill_id,skill_lv,BF_MISC,ATK_DEF,tick); //Use Misc rather than weapon to signal passive pushback
		}
		break;

	case SR_CRESCENTELBOW_AUTOSPELL:
		{
			struct mob_data* tmd = BL_CAST(BL_MOB, bl);
			static int dx[] = { 0, 1, 0, -1, -1,  1, 1, -1};
			static int dy[] = {-1, 0, 1,  0, -1, -1, 1,  1};
			bool wall_damage = true;
			//int i = 0;

			// Knock back the target first if possible before we do a wall check.
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			skill_blown(src,bl,skill_get_blewcount(skill_id,skill_lv),-1,0);

			// Main damage and knockback is complete. End the status to prevent anymore triggers.
			status_change_end(src, SC_CRESCENTELBOW, INVALID_TIMER);

			// Check if the target will receive wall damage.
			// Targets that can be knocked back will receive wall damage if pushed next to a wall.
			// Player's with anti-knockback and boss monsters will always receive wall damage.
			if ( !((dstsd && dstsd->special_state.no_knockback) || (tmd && status_get_class_(src) == CLASS_BOSS)) )
			{// Is there a wall next to the target?
				ARR_FIND( 0, 8, i, map_getcell(bl->m, bl->x+dx[i], bl->y+dy[i], CELL_CHKNOPASS) != 0 );
				if( i == 8 )// No wall detected.
					wall_damage = false;
			}

			if ( wall_damage == true )// Deal wall damage if the above check detected a wall or the target has anti-knockback.
				skill_addtimerskill(src, tick + status_get_amotion(src), bl->id, 0, 0, SR_CRESCENTELBOW_AUTOSPELL, skill_lv, BF_WEAPON, 1);
		}
		break;

	case MO_ABSORBSPIRITS:
		i = 0;
		// Cell PVP [Napster]
		if (dstsd && dstsd->spiritball && (sd == dstsd || map_flag_vs(src->m)) || (map_getcell(src->m,src->x,src->y,CELL_CHKPVP) && map_getcell(bl->m,bl->x,bl->y,CELL_CHKPVP)) && ((dstsd->class_&MAPID_BASEMASK) != MAPID_GUNSLINGER || (dstsd->class_&MAPID_UPPERMASK) != MAPID_REBELLION))
		{	// split the if for readability, and included gunslingers in the check so that their coins cannot be removed [Reddozen]
			i = dstsd->spiritball * 7;
			pc_delspiritball(dstsd,dstsd->spiritball,0);
		} else if (dstmd && !(tstatus->mode&MD_STATUS_IMMUNE) && rnd() % 100 < 20)
		{	// check if target is a monster and not status immune, for the 20% chance to absorb 2 SP per monster's level [Reddozen]
			i = 2 * dstmd->level;
			mob_target(dstmd,src,0);
		}
		if (i) status_heal(src, 0, i, 3);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,i?1:0);
		break;

	case AC_MAKINGARROW:
		if(sd) {
			clif_arrow_create_list(sd);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case AM_PHARMACY:
		if(sd) {
			clif_skill_produce_mix_list(sd,skill_id,22);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case SA_CREATECON:
		if(sd) {
			clif_elementalconverter_list(sd);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case BS_HAMMERFALL:
		skill_addtimerskill(src, tick + 1000, bl->id, 0, 0, skill_id, skill_lv, min(20 + 10 * skill_lv, 50 + 5 * skill_lv), flag);
		break;
	case RG_RAID:
		skill_area_temp[1] = 0;
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		map_foreachinrange(skill_area_sub, bl,
			skill_get_splash(skill_id, skill_lv), splash_target(src),
			src,skill_id,skill_lv,tick, flag|BCT_ENEMY|1,
			skill_castend_damage_id);
		status_change_end(src, SC_HIDING, INVALID_TIMER);
		break;

	case ASC_EDP:
		{
			int time = skill_get_time(skill_id, skill_lv) + 3000 * (sd ? pc_checkskill(sd, GC_RESEARCHNEWPOISON) : 10);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, sc_start(bl, type, 100, skill_lv, time));
		}
		break;

	//List of self skills that give damage around caster
	case ASC_METEORASSAULT:
	case GS_SPREADATTACK:
#if PACKETVER >= 20180620
	case RK_IGNITIONBREAK:
#endif
	case RK_STORMBLAST:
	case WL_FROSTMISTY:
	case NC_AXETORNADO:
	case SR_SKYNETBLOW:
	case SR_RAMPAGEBLASTER:
	case SR_HOWLINGOFLION:
	case RL_FIREDANCE:
	case RL_R_TRIP:
	case RL_D_TAIL:
	case SJ_FULLMOONKICK:
	case SJ_NEWMOONKICK:
	case SJ_STAREMPEROR:
	case SJ_SOLARBURST:
	case SJ_FALLINGSTAR_ATK:
	case KO_HAPPOKUNAI:
	{
		int starget = BL_CHAR | BL_SKILL;
		if (skill_id == SR_HOWLINGOFLION)
			starget = splash_target(src);
		skill_area_temp[1] = 0;
		clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		if (battle_config.skill_wall_check)
			i = map_foreachinshootrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), starget,
				src, skill_id, skill_lv, tick, flag | BCT_ENEMY | SD_SPLASH | 1, skill_castend_damage_id);
		else
			i = map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), starget,
				src, skill_id, skill_lv, tick, flag | BCT_ENEMY | SD_SPLASH | 1, skill_castend_damage_id);
		if (!i && (skill_id == NC_AXETORNADO || skill_id == SR_SKYNETBLOW || skill_id == KO_HAPPOKUNAI))
			clif_skill_damage(src, src, tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
	}
	break;

	case LG_EARTHDRIVE:
		skill_area_temp[1] = 0;
		//clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
		if( skill_id == LG_EARTHDRIVE )
		{
			int dummy = 1;
			i = skill_get_splash(skill_id,skill_lv);
			map_foreachinarea(skill_cell_overlap, src->m, src->x-i, src->y-i, src->x+i, src->y+i, BL_SKILL, LG_EARTHDRIVE, src);
		}
		map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src),
		src, skill_id, skill_lv, tick, flag|BCT_ENEMY|SD_SPLASH|1, skill_castend_damage_id);
		break;

	case NC_EMERGENCYCOOL:
		clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		status_change_end(src, SC_OVERHEAT_LIMITPOINT, INVALID_TIMER);
		status_change_end(src, SC_OVERHEAT, INVALID_TIMER);
		break;
	case GC_COUNTERSLASH:
	case SR_WINDMILL:
	case GN_CART_TORNADO:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
	case NC_INFRAREDSCAN:
	case SR_EARTHSHAKER:
	case NPC_EARTHQUAKE:
	case NPC_VAMPIRE_GIFT:
	case NPC_HELLJUDGEMENT:
	case NPC_PULSESTRIKE:
	case LG_MOONSLASHER:
		skill_castend_damage_id(src, src, skill_id, skill_lv, tick, flag);
		break;

	case KN_BRANDISHSPEAR:
	case ML_BRANDISH:
		{
			skill_area_temp[1] = bl->id;
			if(skill_lv >= 10)
				map_foreachindir(skill_area_sub, src->m, src->x, src->y, bl->x, bl->y,
					skill_get_splash(skill_id, skill_lv), 1, skill_get_maxcount(skill_id, skill_lv)-1, splash_target(src),
					src, skill_id, skill_lv, tick, flag | BCT_ENEMY | (sd ? 3 : 0),
					skill_castend_damage_id);
			if(skill_lv >= 7)
				map_foreachindir(skill_area_sub, src->m, src->x, src->y, bl->x, bl->y,
					skill_get_splash(skill_id, skill_lv), 1, skill_get_maxcount(skill_id, skill_lv)-2, splash_target(src),
					src, skill_id, skill_lv, tick, flag | BCT_ENEMY | (sd ? 2 : 0),
					skill_castend_damage_id);
			if(skill_lv >= 4)
				map_foreachindir(skill_area_sub, src->m, src->x, src->y, bl->x, bl->y,
					skill_get_splash(skill_id, skill_lv), 1, skill_get_maxcount(skill_id, skill_lv)-3, splash_target(src),
					src, skill_id, skill_lv, tick, flag | BCT_ENEMY | (sd ? 1 : 0),
					skill_castend_damage_id);
			map_foreachindir(skill_area_sub, src->m, src->x, src->y, bl->x, bl->y,
				skill_get_splash(skill_id, skill_lv), skill_get_maxcount(skill_id, skill_lv)-3, 0, splash_target(src),
				src, skill_id, skill_lv, tick, flag | BCT_ENEMY | 0,
				skill_castend_damage_id);
		}
		break;

	case WZ_SIGHTRASHER:
		//Passive side of the attack.
		status_change_end(src, SC_SIGHT, INVALID_TIMER);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		map_foreachinshootrange(skill_area_sub,src,
			skill_get_splash(skill_id, skill_lv),BL_CHAR|BL_SKILL,
			src,skill_id,skill_lv,tick, flag|BCT_ENEMY|SD_ANIMATION|1,
			skill_castend_damage_id);
		break;

	case WZ_FROSTNOVA:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		skill_area_temp[1] = 0;
		map_foreachinshootrange(skill_attack_area, src,
			skill_get_splash(skill_id, skill_lv), splash_target(src),
			BF_MAGIC, src, src, skill_id, skill_lv, tick, flag, BCT_ENEMY);
		break;

	case HVAN_EXPLOSION:	//[orn]
	case NPC_SELFDESTRUCTION:
		//Self Destruction hits everyone in range (allies+enemies)
		//Except for Summoned Marine spheres on non-versus maps, where it's just enemy.
		i = ((!md || md->special_state.ai == 2) && !map_flag_vs(src->m))?
			BCT_ENEMY:BCT_ALL;
		clif_skill_nodamage(src, src, skill_id, -1, 1);
		map_delblock(src); //Required to prevent chain-self-destructions hitting back.
		map_foreachinshootrange(skill_area_sub, bl,
			skill_get_splash(skill_id, skill_lv), BL_CHAR|BL_SKILL,
			src, skill_id, skill_lv, tick, flag|i,
			skill_castend_damage_id);
		
		if (map_addblock(src))
			return 1;

		status_damage(src, src, sstatus->max_hp,0,0,1);
		break;

	case AL_ANGELUS:
	case PR_MAGNIFICAT:
	case PR_GLORIA:
	case SN_WINDWALK:
	case CASH_BLESSING:
	case CASH_INCAGI:
	case CASH_ASSUMPTIO:
	case AB_RENOVATIO:
	case SU_BUNCHOFSHRIMP:
	case NV_HELPANGEL:
		if( sd == NULL || sd->status.party_id == 0 || (flag & 1) )
			clif_skill_nodamage(bl, bl, skill_id, skill_lv, sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		else if( sd )
			party_foreachsamemap(skill_area_sub, sd, skill_get_splash(skill_id, skill_lv), src, skill_id, skill_lv, tick, flag|BCT_PARTY|1, skill_castend_nodamage_id);
		break;
	case MER_MAGNIFICAT:
		if( mer != NULL )
		{
			clif_skill_nodamage(bl, bl, skill_id, skill_lv, sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
			if( mer->master && mer->master->status.party_id != 0 && !(flag&1) )
				party_foreachsamemap(skill_area_sub, mer->master, skill_get_splash(skill_id, skill_lv), src, skill_id, skill_lv, tick, flag|BCT_PARTY|1, skill_castend_nodamage_id);
			else if( mer->master && !(flag&1) )
				clif_skill_nodamage(src, &mer->master->bl, skill_id, skill_lv, sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		}
		break;

	case BS_ADRENALINE:
	case BS_ADRENALINE2:
	case BS_WEAPONPERFECT:
	case BS_OVERTHRUST:
		if (sd == NULL || sd->status.party_id == 0 || (flag & 1)) {
			int weapontype = skill_get_weapontype(skill_id);
			if (!weapontype || !dstsd || pc_check_weapontype(dstsd, weapontype)) {
				clif_skill_nodamage(bl, bl, skill_id, skill_lv,
					status_change_start(src, bl, type, 10000, skill_lv, (src == bl) ? 1 : 0, 0, 0, skill_get_time(skill_id, skill_lv),0));
			}
		} else if (sd) {
			party_foreachsamemap(skill_area_sub,
				sd,skill_get_splash(skill_id, skill_lv),
				src,skill_id,skill_lv,tick, flag|BCT_PARTY|1,
				skill_castend_nodamage_id);
		}
		break;

	case BS_MAXIMIZE:
	case NV_TRICKDEAD:
	case CR_DEFENDER:
	case ML_DEFENDER:
	case CR_AUTOGUARD:
	case ML_AUTOGUARD:
	case TK_READYSTORM:
	case TK_READYDOWN:
	case TK_READYTURN:
	case TK_READYCOUNTER:
	case TK_DODGE:
	case CR_SHRINK:
	case SG_FUSION:
	case GS_GATLINGFEVER:
	case SC_REPRODUCE:
	case LG_FORCEOFVANGUARD:
	case SJ_LUNARSTANCE:
	case SJ_STARSTANCE:
	case SJ_UNIVERSESTANCE:
	case SJ_SUNSTANCE:
		if( tsce )
		{
			clif_skill_nodamage(src,bl,skill_id,skill_lv,status_change_end(bl, type, INVALID_TIMER));
			map_freeblock_unlock();
			return 0;
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		break;
	case SP_SOULCOLLECT:
		{
			int soul_gen_timer = skill_get_time(skill_id,skill_lv);

			if ( soul_gen_timer < 1000 )
				soul_gen_timer = 1000;

			if( tsce )
			{
				clif_skill_nodamage(src,bl,skill_id,skill_lv,status_change_end(bl, type, INVALID_TIMER));
				map_freeblock_unlock();
				return 0;
			}
			clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start2(bl,type,100,skill_lv,pc_checkskill(sd,SP_SOULENERGY),soul_gen_timer));
		}
		break;
	case SL_KAITE:
	case SL_KAAHI:
	case SL_KAIZEL:
	case SL_KAUPE:
	case SP_KAUTE:
		if (sd) {
			if (!dstsd || !(
				(sd->sc.data[SC_SPIRIT] && sd->sc.data[SC_SPIRIT]->val2 == SL_SOULLINKER) ||
				(dstsd->class_&MAPID_UPPERMASK) == MAPID_SOUL_LINKER ||
				dstsd->status.char_id == sd->status.char_id ||
				dstsd->status.char_id == sd->status.partner_id ||
				dstsd->status.char_id == sd->status.child ||
				(skill_id == SP_KAUTE && dstsd->sc.data[SC_SOULUNITY])
			)) {
				status_change_start(src,src,SC_STUN,10000,skill_lv,0,0,0,500,8);
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
		}

		if ( skill_id == SP_KAUTE )
		{
			int hp, sp;
			hp = sstatus->max_hp * (10 + 2 * skill_lv) / 100;
			sp = tstatus->max_sp * (10 + 2 * skill_lv) / 100;
			if (!status_charge(src,hp,0))
			{
				if (sd) clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			status_heal(bl,0,sp,2);
		}
		else
			clif_skill_nodamage(src,bl,skill_id,skill_lv,
				sc_start(bl,type,100,skill_lv,skill_get_time(skill_id, skill_lv)));
		break;
	case SM_AUTOBERSERK:
	case MER_AUTOBERSERK:
		if( tsce )
			i = status_change_end(bl, type, INVALID_TIMER);
		else
			i = sc_start(bl,type,100,skill_lv,60000);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,i);
		break;
	case TF_HIDING:
	case ST_CHASEWALK:
	case KO_YAMIKUMO:
		if (tsce)
		{
			clif_skill_nodamage(src,bl,skill_id,-1,status_change_end(bl, type, INVALID_TIMER)); //Hide skill-scream animation.
			map_freeblock_unlock();
			return 0;
		}
		clif_skill_nodamage(src,bl,skill_id,-1,sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));		
		break;
	case TK_RUN:
		if (tsce)
		{
			clif_skill_nodamage(src,bl,skill_id,skill_lv,status_change_end(bl, type, INVALID_TIMER));
			map_freeblock_unlock();
			return 0;
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start4(bl,type,100,skill_lv,unit_getdir(bl),0,0,0));
		if (sd) // If the client receives a skill-use packet inmediately before a walkok packet, it will discard the walk packet! [Skotlex]
			clif_walkok(sd); // So aegis has to resend the walk ok.
		break;
	case AS_CLOAKING:
	case GC_CLOAKINGEXCEED:
		if (tsce){
			i = status_change_end(bl, type, INVALID_TIMER);
			if( i )
				clif_skill_nodamage(src, bl, skill_id, -1, i);
			else if( sd )
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			map_freeblock_unlock();
			return 0;
		}
		i = sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
		if( i )
			clif_skill_nodamage(src, bl, skill_id, -1, i);
		else if( sd )
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
		break;

	case BD_ADAPTATION:
		if(tsc && tsc->data[SC_DANCING]){
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			status_change_end(bl, SC_DANCING, INVALID_TIMER);
		}
		break;

	case BA_FROSTJOKER:
	case DC_SCREAM:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		skill_addtimerskill(src,tick+3000,bl->id,src->x,src->y,skill_id,skill_lv,0,flag);

		if (md) {
			// custom hack to make the mob display the skill, because these skills don't show the skill use text themselves
			//NOTE: mobs don't have the sprite animation that is used when performing this skill (will cause glitches)
			char temp[70];
			snprintf(temp, sizeof(temp), "%s : %s !!",md->name,skill_db[skill_get_index(skill_id)].desc);
			clif_disp_overhead(&md->bl,temp);
		}
		break;

	case BA_PANGVOICE:
		clif_skill_nodamage(src,bl,skill_id,skill_lv, sc_start(bl,SC_CONFUSION,50,7,skill_get_time(skill_id,skill_lv)));
		break;

	case DC_WINKCHARM:
		if( dstsd )
			clif_skill_nodamage(src,bl,skill_id,skill_lv, sc_start(bl,SC_CONFUSION,30,7,skill_get_time2(skill_id,skill_lv)));
		else
		if( dstmd )
		{
			if( status_get_lv(src) > status_get_lv(bl)
			&&  (tstatus->race == RC_DEMON || tstatus->race == RC_DEMIHUMAN || tstatus->race == RC_PLAYER || tstatus->race == RC_ANGEL)
			&&  !(tstatus->mode&MD_STATUS_IMMUNE) )
				clif_skill_nodamage(src, bl, skill_id, skill_lv, sc_start2(bl, type, 70, skill_lv, src->id, skill_get_time(skill_id, skill_lv)));
			else
			{
				clif_skill_nodamage(src,bl,skill_id,skill_lv,0);
				if(sd) clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			}
		}
		break;

	case TF_STEAL:
		if(sd) {
			if(pc_steal_item(sd,bl,skill_lv))
				clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			else
				clif_skill_fail(sd,skill_id,USESKILL_FAIL,0,0);
		}
		break;

	case RG_STEALCOIN:
		if(sd) {
			if(pc_steal_coin(sd,bl))
			{
				dstmd->state.provoke_flag = src->id;
				mob_target(dstmd, src, skill_get_range2(src,skill_id,skill_lv, true));
				clif_skill_nodamage(src,bl,skill_id,skill_lv,1);

			} 
			else
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
		}
		break;

	case MG_STONECURSE:
		{
			short stone_chance = 20 + 4 * skill_lv;

			if (tstatus->mode&MD_STATUS_IMMUNE) {
				if (sd) clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
			if(status_isimmune(bl) || !tsc)
				break;

			if (tsc->data[type]) {
				status_change_end(bl, type, INVALID_TIMER);
				if (sd) clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
			if ( sc && sc->data[SC_PETROLOGY_OPTION] )
				stone_chance += 25;
			if (status_change_start(src,bl,type,stone_chance*100,
				skill_lv, 0, skill_get_time(skill_id, skill_lv), 0,
				skill_get_time2(skill_id,skill_lv),0))
					clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			else if(sd) {
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				// Level 6-10 doesn't consume a red gem if it fails [celest]
				if (skill_lv > 5)
				{ // not to consume items
					map_freeblock_unlock();
					return 0;
				}
			}
		}
		break;

	case NV_FIRSTAID:
		clif_skill_nodamage(src,bl,skill_id,5,1);
		status_heal(bl,5,0,0);
		break;

	case AL_CURE:
		if(status_isimmune(bl)) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,0);
			break;
		}
		status_change_end(bl, SC_SILENCE, INVALID_TIMER);
		status_change_end(bl, SC_BLIND, INVALID_TIMER);
		status_change_end(bl, SC_CONFUSION, INVALID_TIMER);
		status_change_end(bl, SC_BITESCAR, INVALID_TIMER);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;

	case TF_DETOXIFY:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		status_change_end(bl, SC_POISON, INVALID_TIMER);
		status_change_end(bl, SC_DPOISON, INVALID_TIMER);
		break;

	case PR_STRECOVERY:
		if(status_isimmune(bl) != 0) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,0);
			break;
		}
		if (!battle_check_undead(tstatus->race, tstatus->def_ele)) {
			if (tsc != NULL && tsc->opt1 != 0) {
				status_change_end(bl, SC_FREEZE, INVALID_TIMER);
				status_change_end(bl, SC_STONE, INVALID_TIMER);
				status_change_end(bl, SC_SLEEP, INVALID_TIMER);
				status_change_end(bl, SC_STUN, INVALID_TIMER);
				status_change_end(bl, SC_IMPRISON, INVALID_TIMER);
				//Burning is also removed too right? All the other BODYSTATE stuff is removed by this skill. Need confirm. [Rytech]
				//status_change_end(bl, SC_BURNING, INVALID_TIMER);
			}
			//Change in kRO on 6/26/2012
			status_change_end(bl, SC_STASIS, INVALID_TIMER);

			//Other confirms
			status_change_end(bl, SC_NETHERWORLD, INVALID_TIMER);
		}
		else {
			int rate = 100 * (100 - (tstatus->int_ / 2 + tstatus->vit / 3 + tstatus->luk / 10));
			int duration = skill_get_time2(skill_id, skill_lv);

			duration *= (100 - (tstatus->int_ + tstatus->vit) / 2) / 100;
			status_change_start(src, bl, SC_BLIND, rate, 1, 0, 0, 0, duration, 0);
		}

		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);

		if(dstmd != NULL)
			mob_unlocktarget(dstmd,tick);

		break;

	// Mercenary Supportive Skills
	case MER_BENEDICTION:
		status_change_end(bl, SC_CURSE, INVALID_TIMER);
		status_change_end(bl, SC_BLIND, INVALID_TIMER);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;
	case MER_COMPRESS:
		status_change_end(bl, SC_BLEEDING, INVALID_TIMER);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;
	case MER_MENTALCURE:
		status_change_end(bl, SC_CONFUSION, INVALID_TIMER);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;
	case MER_RECUPERATE:
		status_change_end(bl, SC_POISON, INVALID_TIMER);
		status_change_end(bl, SC_SILENCE, INVALID_TIMER);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;
	case MER_REGAIN:
		status_change_end(bl, SC_SLEEP, INVALID_TIMER);
		status_change_end(bl, SC_STUN, INVALID_TIMER);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;
	case MER_TENDER:
		status_change_end(bl, SC_FREEZE, INVALID_TIMER);
		status_change_end(bl, SC_STONE, INVALID_TIMER);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;

	case MER_SCAPEGOAT:
		if( mer && mer->master )
		{
			status_heal(&mer->master->bl, mer->battle_status.hp, 0, 2);
			status_damage(src, src, mer->battle_status.max_hp, 0, 0, 1);
		}
		break;

	case MER_ESTIMATION:
		if( !mer )
			break;
		sd = mer->master;
	case WZ_ESTIMATION:
		if( sd == NULL )
			break;
		if( dstsd )
		{ // Fail on Players
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			break;
		}

		clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		clif_skill_estimation(sd, bl);
		if( skill_id == MER_ESTIMATION )
			sd = NULL;
		break;

	case BS_REPAIRWEAPON:
		if(sd && dstsd)
			clif_item_repair_list(sd,dstsd,skill_lv);
		break;

	case MC_IDENTIFY:
		if(sd)
		{
			clif_item_identify_list(sd);
			if (sd->menuskill_id != MC_IDENTIFY) {/* failed, dont consume anything, return */
				map_freeblock_unlock();
				return 1;
			}
			else { // consume sp only if succeeded
				struct skill_condition req = skill_get_requirement(sd, skill_id, skill_lv);
				status_zap(src, 0, req.sp);
			}
		}
		break;

	// Weapon Refining [Celest]
	case WS_WEAPONREFINE:
		if(sd)
			clif_item_refine_list(sd);
		break;

	case MC_VENDING:
		if(sd)
		{	//Prevent vending of GMs with unnecessary Level to trade/drop. [Skotlex]
			if ( !pc_can_give_items(pc_isGM(sd)) )
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			else {
				sd->state.prevend = 1;
				sd->state.workinprogress = WIP_DISABLE_ALL;
				sd->vend_skill_lv = skill_lv;
				ARR_FIND(0, MAX_CART, i, sd->cart.u.items_cart[i].nameid && sd->cart.u.items_cart[i].id == 0);
				if (i < MAX_CART)
					intif_storage_save(sd, &sd->cart);
				else
					clif_openvendingreq(sd,2+skill_lv);
			}
		}
		break;

	case AL_TELEPORT:
	case ALL_ODINS_RECALL:
		if(sd)
		{
			if (map[bl->m].flag.noteleport && skill_lv <= 2) {
				clif_skill_teleportmessage(sd,0);
				break;
			}
			if(!battle_config.duel_allow_teleport && sd->duel_group && skill_lv <= 2) { // duel restriction [LuzZza]
				char output[128]; sprintf(output, msg_txt(sd,365), skill_get_name(AL_TELEPORT));
				clif_displaymessage(sd->fd, output); //"Duel: Can't use %s in duel."
				break;
			}

			if( sd->state.autocast || ( (sd->skillitem == AL_TELEPORT || battle_config.skip_teleport_lv1_menu) && skill_lv == 1 ) || skill_lv == 3 )
			{
				if( skill_lv == 1 )
					pc_randomwarp(sd,CLR_TELEPORT);
				else
					pc_setpos(sd,sd->status.save_point.map,sd->status.save_point.x,sd->status.save_point.y,CLR_TELEPORT);
				break;
			}

			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			if( skill_lv == 1 && skill_id != ALL_ODINS_RECALL )
				clif_skill_warppoint(sd,skill_id,skill_lv, (unsigned short)-1,0,0,0);
			else
				clif_skill_warppoint(sd,skill_id,skill_lv, (unsigned short)-1,sd->status.save_point.map,0,0);
		} else
			unit_warp(bl,-1,-1,-1,CLR_TELEPORT);
		break;

	case NPC_EXPULSION:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		unit_warp(bl,-1,-1,-1,CLR_TELEPORT);
		break;

	case AL_HOLYWATER:
		if(sd) {
			if (skill_produce_mix(sd, skill_id, 523, 0, 0, 0, 1))
				clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			else
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
		}
		break;

	case TF_PICKSTONE:
		if(sd) {
			int eflag;
			struct item item_tmp;
			struct block_list tbl;
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			memset(&item_tmp,0,sizeof(item_tmp));
			memset(&tbl,0,sizeof(tbl)); // [MouseJstr]
			item_tmp.nameid = ITEMID_STONE;
			item_tmp.identify = 1;
			tbl.id = 0;
			clif_takeitem(&sd->bl,&tbl);
			eflag = pc_additem(sd,&item_tmp,1,LOG_TYPE_OTHER);
			if(eflag) {
				clif_additem(sd,0,0,eflag);
				map_addflooritem(&item_tmp,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0,0,false);
			}
		}
		break;
	case ASC_CDP:
     	if(sd) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			skill_produce_mix(sd, skill_id, 678, 0, 0, 0, 1); //Produce a Deadly Poison Bottle.
		}
		break;

	case RG_STRIPWEAPON:
	case RG_STRIPSHIELD:
	case RG_STRIPARMOR:
	case RG_STRIPHELM:
	case ST_FULLSTRIP:
	case SC_STRIPACCESSARY:
	case GC_WEAPONCRUSH:
	{
		unsigned short location = 0;
		int d = 0;
		
		//Rate in percent
		if( skill_id == ST_FULLSTRIP )
			i = 5 + 2 * skill_lv + (sstatus->dex - tstatus->dex)/5;
		else if( skill_id == SC_STRIPACCESSARY )
			i = 12 + 2 * skill_lv + (sstatus->dex - tstatus->dex)/5;
		else
			i = 5 + 5 * skill_lv + (sstatus->dex - tstatus->dex)/5;

		if (i < 5) i = 5; //Minimum rate 5%

		//Duration in ms
		d = skill_get_time(skill_id,skill_lv) + (sstatus->dex - tstatus->dex)*500;
		if (d < 0) d = 0; //Minimum duration 0ms

		switch (skill_id) {
		case GC_WEAPONCRUSH:
		case RG_STRIPWEAPON:
			location = EQP_WEAPON;
			break;
		case RG_STRIPSHIELD:
			location = EQP_SHIELD;
			break;
		case RG_STRIPARMOR:
			location = EQP_ARMOR;
			break;
		case RG_STRIPHELM:
			location = EQP_HELM;
			break;
		case ST_FULLSTRIP:
			location = EQP_WEAPON|EQP_SHIELD|EQP_ARMOR|EQP_HELM;
			break;
		case SC_STRIPACCESSARY:
			location = EQP_ACC;
			break;
		}

		//Special message when trying to use strip on FCP [Jobbie]
		if( sd && skill_id == ST_FULLSTRIP && tsc && tsc->data[SC_CP_WEAPON] && tsc->data[SC_CP_HELM] && tsc->data[SC_CP_ARMOR] && tsc->data[SC_CP_SHIELD] )
		{
			clif_gospel_info(sd, 0x28);
			break;
		}

		// Attempts to strip at rate i and duration d
		if ((i = skill_strip_equip(bl, location, i, skill_lv, d)) || (skill_id != ST_FULLSTRIP && skill_id != GC_WEAPONCRUSH))
			clif_skill_nodamage(src, bl, skill_id, skill_lv, i); 

		//Nothing stripped.
		if( sd && !i )
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);

		if( skill_id == SC_STRIPACCESSARY && i )
			clif_status_change(src, SI_ACTIONDELAY, 1, 1000, 0, 0, 1);
	}
		break;

	case AM_BERSERKPITCHER:
	case AM_POTIONPITCHER:
		{
			int i,x,hp = 0,sp = 0,bonus=100;
			if( dstmd && dstmd->mob_id == MOBID_EMPERIUM )
			{
				map_freeblock_unlock();
				return 1;
			}
			if( sd )
			{
				x = skill_lv%11 - 1;
				i = pc_search_inventory(sd, skill_get_itemid(skill_id,x+1));
				if(i < 0 || skill_get_itemid(skill_id, x + 1) <= 0)
				{
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
					map_freeblock_unlock();
					return 1;
				}
				if(sd->inventory_data[i] == NULL || sd->inventory.u.items_inventory[i].amount < skill_get_itemqty(skill_id,x+1))
				{
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
					map_freeblock_unlock();
					return 1;
				}
				if( skill_id == AM_BERSERKPITCHER )
				{
					if( dstsd && dstsd->status.base_level < (unsigned int)sd->inventory_data[i]->elv )
					{
						clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
						map_freeblock_unlock();
						return 1;
					}
				}
				potion_flag = 1;
				potion_hp = potion_sp = potion_per_hp = potion_per_sp = 0;
				potion_target = bl->id;
				run_script(sd->inventory_data[i]->script,0,sd->bl.id,0);
				potion_flag = potion_target = 0;
				if( sd->sc.data[SC_SPIRIT] && sd->sc.data[SC_SPIRIT]->val2 == SL_ALCHEMIST )
					bonus += sd->status.base_level;
				if( potion_per_hp > 0 || potion_per_sp > 0 )
				{
					hp = tstatus->max_hp * potion_per_hp / 100;
					hp = hp * (100 + pc_checkskill(sd,AM_POTIONPITCHER)*10 + pc_checkskill(sd,AM_LEARNINGPOTION)*5)*bonus/10000;
					if( dstsd )
					{
						sp = dstsd->status.max_sp * potion_per_sp / 100;
						sp = sp * (100 + pc_checkskill(sd,AM_POTIONPITCHER)*10 + pc_checkskill(sd,AM_LEARNINGPOTION)*5)*bonus/10000;
					}
				}
				else
				{
					if( potion_hp > 0 )
					{
						hp = potion_hp * (100 + pc_checkskill(sd,AM_POTIONPITCHER)*10 + pc_checkskill(sd,AM_LEARNINGPOTION)*5)*bonus/10000;
						hp = hp * (100 + (tstatus->vit<<1)) / 100;
						if( dstsd )
							hp = hp * (100 + pc_checkskill(dstsd,SM_RECOVERY)*10) / 100;
					}
					if( potion_sp > 0 )
					{
						sp = potion_sp * (100 + pc_checkskill(sd,AM_POTIONPITCHER)*10 + pc_checkskill(sd,AM_LEARNINGPOTION)*5)*bonus/10000;
						sp = sp * (100 + (tstatus->int_<<1)) / 100;
						if( dstsd )
							sp = sp * (100 + pc_checkskill(dstsd,MG_SRECOVERY)*10) / 100;
					}
				}

				if ((bonus = pc_get_itemgroup_bonus_group(sd, IG_POTION))) {
					hp += hp * bonus / 100;
					sp += sp * bonus / 100;
				}

				if( (i = pc_skillheal_bonus(sd, skill_id)) )
				{
					hp += hp * i / 100;
					sp += sp * i / 100;
				}
			}
			else
			{
				//Maybe replace with potion_hp, but I'm unsure how that works [Playtester]
				switch (skill_lv) {
				case 1: hp = 45; break;
				case 2: hp = 105; break;
				case 3: hp = 175; break;
				default: hp = 325; break;
				}
				hp = (hp + rnd() % (skill_lv * 20 + 1)) * (150 + skill_lv * 10) / 100;
				hp = hp * (100 + (tstatus->vit<<1)) / 100;
				if( dstsd )
					hp = hp * (100 + pc_checkskill(dstsd,SM_RECOVERY)*10) / 100;
			}
			if( dstsd && (i = pc_skillheal2_bonus(dstsd, skill_id)) )
			{
				hp += hp * i / 100;
				sp += sp * i / 100;
			}
			if( tsc ) {
				if( tsc->data[SC_CRITICALWOUND] ) {
					hp -= hp * tsc->data[SC_CRITICALWOUND]->val2 / 100;
					sp -= sp * tsc->data[SC_CRITICALWOUND]->val2 / 100;
				}
				if( tsc->data[SC_DEATHHURT] )
					hp -= hp * 20 / 100;
			}
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			if( hp > 0 || (skill_id == AM_POTIONPITCHER && sp <= 0) )
				clif_skill_nodamage(NULL,bl,AL_HEAL,hp,1);
			if( sp > 0 )
				clif_skill_nodamage(NULL,bl,MG_SRECOVERY,sp,1);
			status_heal(bl,hp,sp,0);
		}
		break;
	case AM_CP_WEAPON:
	case AM_CP_SHIELD:
	case AM_CP_ARMOR:
	case AM_CP_HELM:
		{
			unsigned int equip[] = { EQP_WEAPON, EQP_SHIELD, EQP_ARMOR, EQP_HEAD_TOP };

			if (sd && (bl->type != BL_PC || (dstsd && pc_checkequip(dstsd, equip[skill_id - AM_CP_WEAPON], false) < 0))) {
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
				map_freeblock_unlock(); // Don't consume item requirements
				return 0;
			}

			clif_skill_nodamage(src,bl,skill_id,skill_lv,
				sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		}
		break;
	case AM_TWILIGHT1:
		if (sd) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			//Prepare 200 White Potions.
			if (!skill_produce_mix(sd, skill_id, ITEMID_WHITE_POTION, 0, 0, 0, 200))
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
		}
		break;
	case AM_TWILIGHT2:
		if (sd) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			//Prepare 200 Slim White Potions.
			if (!skill_produce_mix(sd, skill_id, ITEMID_WHITE_SLIM_POTION, 0, 0, 0, 200))
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
		}
		break;
	case AM_TWILIGHT3:
		if (sd) {
			int ebottle = pc_search_inventory(sd, ITEMID_EMPTY_BOTTLE);
			if (ebottle >= 0)
				ebottle = sd->inventory.u.items_inventory[ebottle].amount;
			//check if you can produce all three, if not, then fail:
			if (!skill_can_produce_mix(sd, ITEMID_ALCOHOL,-1, 100) //100 Alcohol
				|| !skill_can_produce_mix(sd, ITEMID_ACID_BOTTLE,-1, 50) //50 Acid Bottle
				|| !skill_can_produce_mix(sd, ITEMID_FIRE_BOTTLE,-1, 50) //50 Flame Bottle
				|| ebottle < 200 //200 empty bottle are required at total.
			) {
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			skill_produce_mix(sd, skill_id, ITEMID_ALCOHOL, 0, 0, 0, 100);
			skill_produce_mix(sd, skill_id, ITEMID_ACID_BOTTLE, 0, 0, 0, 50);
			skill_produce_mix(sd, skill_id, ITEMID_FIRE_BOTTLE, 0, 0, 0, 50);
		}
		break;
	case SA_DISPELL:
		if (flag & 1 || (i = skill_get_splash(skill_id, skill_lv)) < 1) {
			if (sd && dstsd && !map_flag_vs(sd->bl.m) && (!sd->duel_group || sd->duel_group != dstsd->duel_group) && (!sd->status.party_id || sd->status.party_id != dstsd->status.party_id))
				break; // Outside PvP it should only affect party members and no skill fail message
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			if((dstsd && (dstsd->class_&MAPID_UPPERMASK) == MAPID_SOUL_LINKER)
				|| (tsc && tsc->data[SC_SPIRIT] && tsc->data[SC_SPIRIT]->val2 == SL_ROGUE) //Rogue's spirit defends againt dispel.
				|| rnd()%100 >= 50+10*skill_lv)
			{
				if (sd)
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
			if(status_isimmune(bl) || !tsc || !tsc->count)
				break;

			//Remove bonus_script by Dispell
			if (dstsd)
				pc_bonus_script_clear(dstsd,BSF_REM_ON_DISPELL);

			if(!tsc || !tsc->count)
				break;

			for(i=0;i<SC_MAX;i++)
			{
				if (!tsc->data[i])
					continue;
				switch (i) {
				case SC_WEIGHT50:		case SC_WEIGHT90:		case SC_HALLUCINATION:
				case SC_STRIPWEAPON:	case SC_STRIPSHIELD:	case SC_STRIPARMOR:
				case SC_STRIPHELM:		case SC_CP_WEAPON:		case SC_CP_SHIELD:
				case SC_CP_ARMOR:		case SC_CP_HELM:		case SC_COMBO:
				case SC_STRFOOD:		case SC_AGIFOOD:		case SC_VITFOOD:
				case SC_INTFOOD:		case SC_DEXFOOD:		case SC_LUKFOOD:
				case SC_HITFOOD:		case SC_FLEEFOOD:		case SC_BATKFOOD:
				case SC_WATKFOOD:		case SC_MATKFOOD:		case SC_DANCING:
				case SC_EDP:			case SC_AUTOBERSERK:
				case SC_CARTBOOST:		case SC_MELTDOWN:		case SC_SAFETYWALL:
				case SC_SMA:			case SC_SPEEDUP0:		case SC_NOCHAT:
				case SC_ANKLE:			case SC_SPIDERWEB:		case SC_JAILED:
				case SC_ITEMBOOST:		case SC_EXPBOOST:		case SC_LIFEINSURANCE:
				case SC_BOSSMAPINFO:	case SC_PNEUMA:			case SC_AUTOSPELL:
				case SC_INCHITRATE:		case SC_INCATKRATE:		case SC_NEN:
				case SC_READYSTORM:		case SC_READYDOWN:		case SC_READYTURN:
				case SC_READYCOUNTER:	case SC_DODGE:			case SC_WARM:
				case SC_SPEEDUP1:		case SC_AUTOTRADE:		case SC_CRITICALWOUND:
				case SC_JEXPBOOST:		case SC_INVINCIBLE:		case SC_INVINCIBLEOFF:
				case SC_HELLPOWER:		case SC_MANU_ATK:		case SC_MANU_DEF:
				case SC_SPL_ATK:		case SC_SPL_DEF:		case SC_MANU_MATK:
				case SC_SPL_MATK:		case SC_RICHMANKIM:		case SC_ETERNALCHAOS:
				case SC_DRUMBATTLE:		case SC_NIBELUNGEN:		case SC_ROKISWEIL:
				case SC_INTOABYSS:		case SC_SIEGFRIED:		case SC_FOOD_STR_CASH:	
				case SC_FOOD_AGI_CASH:	case SC_FOOD_VIT_CASH:	case SC_FOOD_DEX_CASH:	
				case SC_FOOD_INT_CASH:	case SC_FOOD_LUK_CASH:	case SC_SEVENWIND:
				case SC_MIRACLE:		case SC_S_LIFEPOTION:	case SC_L_LIFEPOTION:
				case SC_INCHEALRATE:
				// 3CeAM
				// 3rd Job Status's
				case SC_DEATHBOUND:				case SC_EPICLESIS:			case SC_CLOAKINGEXCEED:
				case SC_HALLUCINATIONWALK:		case SC_HALLUCINATIONWALK_POSTDELAY:	case SC_WEAPONBLOCKING:
				case SC_WEAPONBLOCKING_POSTDELAY:	case SC_ROLLINGCUTTER:	case SC_POISONINGWEAPON:
				case SC_FEARBREEZE:				case SC_ELECTRICSHOCKER:	case SC_WUGRIDER:
				case SC_WUGDASH:				case SC_WUGBITE:			case SC_CAMOUFLAGE:
				case SC_ACCELERATION:			case SC_HOVERING:			case SC_OVERHEAT_LIMITPOINT:
				case SC_OVERHEAT:				case SC_SHAPESHIFT:			case SC_INFRAREDSCAN:
				case SC_MAGNETICFIELD:			case SC_NEUTRALBARRIER:		case SC_NEUTRALBARRIER_MASTER:
				case SC_STEALTHFIELD_MASTER:	case SC__REPRODUCE:			case SC_FORCEOFVANGUARD:
				case SC__SHADOWFORM:			case SC_EXEEDBREAK:			case SC__INVISIBILITY:
				case SC_BANDING:				case SC_INSPIRATION:		case SC_RAISINGDRAGON:
				case SC_LIGHTNINGWALK:			case SC_CURSEDCIRCLE_ATKER:	case SC_CURSEDCIRCLE_TARGET:
				case SC_CRESCENTELBOW:			case SC__STRIPACCESSARY:	case SC__MANHOLE:
				case SC_GENTLETOUCH_ENERGYGAIN:	case SC_GENTLETOUCH_CHANGE:	case SC_GENTLETOUCH_REVITALIZE:
				case SC_SWING:		case SC_SYMPHONY_LOVE:	case SC_RUSH_WINDMILL:
				case SC_ECHOSONG:		case SC_MOONLIT_SERENADE:	case SC_SITDOWN_FORCE:
				case SC_ANALYZE:		case SC_LERADS_DEW:		case SC_MELODYOFSINK:
				case SC_BEYOND_OF_WARCRY:	case SC_UNLIMITED_HUMMING_VOICE:	case SC_STEALTHFIELD:
				case SC_WARMER:			case SC_READING_SB:		case SC_GN_CARTBOOST:
				case SC_THORNS_TRAP:		case SC_SPORE_EXPLOSION:	case SC_DEMONIC_FIRE:
				case SC_FIRE_EXPANSION_SMOKE_POWDER:	case SC_FIRE_EXPANSION_TEAR_GAS:		case SC_VACUUM_EXTREME:
				case SC_BANDING_DEFENCE:	case SC_LG_REFLECTDAMAGE:	case SC_MILLENNIUMSHIELD:
				// Genetic Potions
				case SC_SAVAGE_STEAK:	case SC_COCKTAIL_WARG_BLOOD:	case SC_MINOR_BBQ:
				case SC_SIROMA_ICE_TEA:	case SC_DROCERA_HERB_STEAMED:	case SC_PUTTI_TAILS_NOODLES:
				case SC_MELON_BOMB:		case SC_BANANA_BOMB_SITDOWN_POSTDELAY:	case SC_BANANA_BOMB:
				case SC_PROMOTE_HEALTH_RESERCH:	case SC_ENERGY_DRINK_RESERCH:	case SC_EXTRACT_WHITE_POTION_Z:
				case SC_VITATA_500:		case SC_EXTRACT_SALAMINE_JUICE:	case SC_BOOST500:
				case SC_FULL_SWING_K:	case SC_MANA_PLUS:		case SC_MUSTLE_M:
				case SC_LIFE_FORCE_F:
				// Elementals
				case SC_EL_WAIT:		case SC_EL_PASSIVE:		case SC_EL_DEFENSIVE:
				case SC_EL_OFFENSIVE:	case SC_EL_COST:		case SC_CIRCLE_OF_FIRE:
				case SC_CIRCLE_OF_FIRE_OPTION:	case SC_FIRE_CLOAK:	case SC_FIRE_CLOAK_OPTION:
				case SC_WATER_SCREEN:	case SC_WATER_SCREEN_OPTION:	case SC_WATER_DROP:
				case SC_WATER_DROP_OPTION:	case SC_WIND_STEP:	case SC_WIND_STEP_OPTION:
				case SC_WIND_CURTAIN:	case SC_WIND_CURTAIN_OPTION:	case SC_SOLID_SKIN:
				case SC_SOLID_SKIN_OPTION:	case SC_STONE_SHIELD:	case SC_STONE_SHIELD_OPTION:
				case SC_PYROTECHNIC:	case SC_PYROTECHNIC_OPTION:	case SC_HEATER:
				case SC_HEATER_OPTION:	case SC_TROPIC:			case SC_TROPIC_OPTION:
				case SC_AQUAPLAY:		case SC_AQUAPLAY_OPTION:	case SC_COOLER:
				case SC_COOLER_OPTION:	case SC_CHILLY_AIR:		case SC_CHILLY_AIR_OPTION:
				case SC_GUST:			case SC_GUST_OPTION:	case SC_BLAST:
				case SC_BLAST_OPTION:	case SC_WILD_STORM:		case SC_WILD_STORM_OPTION:
				case SC_PETROLOGY:		case SC_PETROLOGY_OPTION:	case SC_CURSED_SOIL:
				case SC_CURSED_SOIL_OPTION:	case SC_UPHEAVAL:	case SC_UPHEAVAL_OPTION:
				case SC_TIDAL_WEAPON:	case SC_TIDAL_WEAPON_OPTION:	case SC_ROCK_CRUSHER:
				case SC_ROCK_CRUSHER_ATK:
				// Mutated Homunculus
				case SC_NEEDLE_OF_PARALYZE:	case SC_PAIN_KILLER:	case SC_LIGHT_OF_REGENE:
				case SC_OVERED_BOOST:	case SC_SILENT_BREEZE:	case SC_STYLE_CHANGE:
				case SC_SONIC_CLAW_POSTDELAY:	case SC_SILVERVEIN_RUSH_POSTDELAY:	case SC_MIDNIGHT_FRENZY_POSTDELAY:
				case SC_GOLDENE_FERSE:	case SC_ANGRIFFS_MODUS:	case SC_TINDER_BREAKER:
				case SC_TINDER_BREAKER_POSTDELAY:	case SC_CBC:	case SC_CBC_POSTDELAY:
				case SC_EQC:			case SC_MAGMA_FLOW:		case SC_GRANITIC_ARMOR:
				case SC_PYROCLASTIC:	case SC_VOLCANIC_ASH:
				// Kagerou/Oboro
				case SC_KG_KAGEHUMI:	case SC_KO_JYUMONJIKIRI:	case SC_MEIKYOUSISUI:
				case SC_KYOUGAKU:		case SC_IZAYOI:			case SC_ZENKAI:
				// Summoner
				case SC_SPRITEMABLE:	case SC_BITESCAR:		case SC_SOULATTACK:
				// Rebellion - Need Confirm
				case SC_B_TRAP:			case SC_C_MARKER:		case SC_H_MINE:
				case SC_ANTI_M_BLAST:	case SC_FALLEN_ANGEL:
				// Star Emperor
				case SC_NEWMOON:		case SC_FLASHKICK:		case SC_NOVAEXPLOSING:
				// Soul Reaper
				case SC_SOULUNITY:		case SC_SOULSHADOW:		case SC_SOULFAIRY:
				case SC_SOULFALCON:		case SC_SOULGOLEM:		case SC_USE_SKILL_SP_SPA:
				case SC_USE_SKILL_SP_SHA:	case SC_SP_SHA:
				// 3rd Job Level Expansion
				case SC_FRIGG_SONG:		case SC_OFFERTORIUM:	case SC_TELEKINESIS_INTENSE:
				case SC_KINGS_GRACE:
				// Misc Status's
				case SC_ALL_RIDING:		case SC_MONSTER_TRANSFORM:	case SC_ON_PUSH_CART:
				case SC_FULL_THROTTLE:	case SC_REBOUND:			case SC_ANCILLA:
				// Guild Skills
				case SC_LEADERSHIP:		case SC_GLORYWOUNDS:	case SC_SOULCOLD:
				case SC_HAWKEYES:
				// Only removeable by Clearance
				case SC_CRUSHSTRIKE:	case SC_REFRESH:			case SC_GIANTGROWTH:
				case SC_STONEHARDSKIN:	case SC_VITALITYACTIVATION:	case SC_FIGHTINGSPIRIT:
				case SC_ABUNDANCE:		case SC_ORATIO:				case SC_LAUDAAGNUS:
				case SC_LAUDARAMUS:		case SC_RENOVATIO:			case SC_EXPIATIO:
				case SC_TOXIN:			case SC_PARALYSE:			case SC_VENOMBLEED:
				case SC_MAGICMUSHROOM:	case SC_DEATHHURT:			case SC_PYREXIA:
				case SC_OBLIVIONCURSE:	case SC_LEECHESEND:			case SC_DUPLELIGHT:
				case SC_MARSHOFABYSS:	case SC_RECOGNIZEDSPELL:	case SC__BODYPAINT:
				case SC__DEADLYINFECT:	case SC_EARTHDRIVE:			case SC_VENOMIMPRESS:
				case SC_FROST:		case SC_BLOOD_SUCKER:	case SC_MANDRAGORA:
				case SC_STOMACHACHE:	case SC_MYSTERIOUS_POWDER:
				case SC_DAILYSENDMAILCNT:
				case SC_ATTHASTE_CASH:	case SC_REUSE_REFRESH:
				case SC_REUSE_LIMIT_A:	case SC_REUSE_LIMIT_B:	case SC_REUSE_LIMIT_C:
				case SC_REUSE_LIMIT_D:	case SC_REUSE_LIMIT_E:	case SC_REUSE_LIMIT_F:
				case SC_REUSE_LIMIT_G:	case SC_REUSE_LIMIT_H:	case SC_REUSE_LIMIT_MTF:
				case SC_REUSE_LIMIT_ASPD_POTION:	case SC_REUSE_MILLENNIUMSHIELD:	case SC_REUSE_CRUSHSTRIKE:
				case SC_REUSE_STORMBLAST:	case SC_ALL_RIDING_REUSE_LIMIT:
				// Monster Transformation
				case SC_MTF_ASPD:		case SC_MTF_RANGEATK:		case SC_MTF_MATK:
				case SC_MTF_MLEATKED:		case SC_MTF_CRIDAMAGE:
				case SC_MTF_ASPD2:		case SC_MTF_RANGEATK2:	case SC_MTF_MATK2:
				case SC_2011RWC_SCROLL:		case SC_JP_EVENT04:	case SC_MTF_MHP:
				case SC_MTF_MSP:		case SC_MTF_PUMPKIN:	case SC_MTF_HITFLEE:
				case SC_DORAM_BUF_01:	case SC_DORAM_BUF_02:
					continue;
				case SC_WHISTLE:	case SC_ASSNCROS:		case SC_POEMBRAGI:
				case SC_APPLEIDUN:	case SC_HUMMING:		case SC_DONTFORGETME:
				case SC_FORTUNE:	case SC_SERVICE4U:
					if (tsc->data[i]->val4 == 0)
						continue; //if in song-area don't end it
					break;
				case SC_ASSUMPTIO:
					if( bl->type == BL_MOB )
						continue;
					break;
				}
				if( i == SC_BERSERK )
					tsc->data[i]->val2 = 0; //Mark a dispelled berserk to avoid setting hp to 100 by setting hp penalty to 0.
				status_change_end( bl, (sc_type)i, INVALID_TIMER);
			}
			break;
		}
		//Affect all targets on splash area.
		map_foreachinrange( skill_area_sub, bl, i, BL_CHAR, src, skill_id, skill_lv, tick, flag|1, skill_castend_damage_id );
		break;

	case TF_BACKSLIDING: //This is the correct implementation as per packet logging information. [Skotlex]
		clif_skill_nodamage( src, bl, skill_id, skill_lv, 1 );
		skill_blown(src, bl, skill_get_blewcount( skill_id, skill_lv ), unit_getdir( bl ), 2);
		break;

	case TK_HIGHJUMP:
		{
			int x,y, dir = unit_getdir(src);

		  	//Fails on noteleport maps, except for GvG and BG maps [Skotlex]
			if( map[src->m].flag.noteleport && !( map[src->m].flag.battleground || map_flag_gvg2( src->m ) ) ) 
			{
				x = src->x;
				y = src->y;
			}
			else if (dir % 2) {
				//Diagonal
				x = src->x + dirx[dir] * (skill_lv * 4) / 3;
				y = src->y + diry[dir] * (skill_lv * 4) / 3;
			} else {
				x = src->x + dirx[dir]*skill_lv*2;
				y = src->y + diry[dir]*skill_lv*2;
			}

			clif_skill_nodamage(src,bl,TK_HIGHJUMP,skill_lv,1);
			if(!map_count_oncell(src->m,x,y,BL_PC|BL_NPC|BL_MOB,0) && map_getcell(src->m,x,y,CELL_CHKREACH)) {
				clif_slide(src,x,y);
				unit_movepos(src, x, y, 1, 0);
			}
		}
		break;

	case SA_CASTCANCEL:
	case SO_SPELLFIST:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		unit_skillcastcancel(src,1);
		if(sd) {
			int sp = skill_get_sp(sd->skillid_old,sd->skilllv_old);
			sp = sp * (90 - (skill_lv-1)*20) / 100;
			if(sp < 0) sp = 0;
			status_zap(src, 0, sp);
			if( skill_id == SO_SPELLFIST )
				sc_start4(src,type,100,skill_lv+1,skill_lv,(sd->skillid_old)?sd->skillid_old:MG_FIREBOLT,(sd->skilllv_old)?sd->skilllv_old:skill_lv,skill_get_time(skill_id,skill_lv));
		}
		break;
	case SA_SPELLBREAKER:
		{
			int sp;
			if(tsc && tsc->data[SC_MAGICROD]) {
				sp = skill_get_sp(skill_id,skill_lv);
				sp = sp * tsc->data[SC_MAGICROD]->val2 / 100;
				if(sp < 1) sp = 1;
				status_heal(bl,0,sp,2);
				status_percent_damage(bl, src, 0, -20, false); //20% max SP damage.
			} else {
				struct unit_data *ud = unit_bl2ud(bl);
				int bl_skillid=0,bl_skilllv=0,hp = 0;
				if (!ud || ud->skilltimer == INVALID_TIMER)
					break; //Nothing to cancel.
				bl_skillid = ud->skill_id;
				bl_skilllv = ud->skill_lv;
				if (status_has_mode(tstatus, MD_STATUS_IMMUNE)) { //Only 10% success chance against status immune. [Skotlex]
					if (rnd()%100 < 90)
					{
						if (sd) clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
						break;
					}
				} else if (!dstsd || map_flag_vs(bl->m)) //HP damage only on pvp-maps when against players.
					hp = tstatus->max_hp/50; //Recover 2% HP [Skotlex]

				clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
				unit_skillcastcancel(bl,0);
				sp = skill_get_sp(bl_skillid,bl_skilllv);
				status_zap(bl, hp, sp);

				if (hp && skill_lv >= 5)
					hp>>=1;	//Recover half damaged HP at level 5 [Skotlex]
				else
					hp = 0;

				if (sp) //Recover some of the SP used
					sp = sp*(25*(skill_lv-1))/100;

				if(hp || sp)
					status_heal(src, hp, sp, 2);
			}
		}
		break;
	case SA_MAGICROD:
		clif_skill_nodamage(src, src, SA_MAGICROD, skill_lv, 1);
		sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
		break;
	case SA_AUTOSPELL:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		if(sd) {
			clif_autospell(sd,skill_lv);
			sd->state.workinprogress = WIP_DISABLE_ALL;
		} else {
			int maxlv=1,spellid=0;
			static const int spellarray[3] = { MG_COLDBOLT,MG_FIREBOLT,MG_LIGHTNINGBOLT };
			if(skill_lv >= 10) {
				spellid = MG_FROSTDIVER;
//				if (tsc && tsc->data[SC_SPIRIT] && tsc->data[SC_SPIRIT]->val2 == SA_SAGE)
//					maxlv = 10;
//				else
					maxlv = skill_lv - 9;
			}
			else if(skill_lv >=8) {
				spellid = MG_FIREBALL;
				maxlv = skill_lv - 7;
			}
			else if(skill_lv >=5) {
				spellid = MG_SOULSTRIKE;
				maxlv = skill_lv - 4;
			}
			else if(skill_lv >=2) {
				int i = rnd()%3;
				spellid = spellarray[i];
				maxlv = skill_lv - 1;
			}
			else if(skill_lv > 0) {
				spellid = MG_NAPALMBEAT;
				maxlv = 3;
			}
			if(spellid > 0)
				sc_start4(src,SC_AUTOSPELL,100,skill_lv,spellid,maxlv,0,
					skill_get_time(SA_AUTOSPELL,skill_lv));
		}
		break;

	case BS_GREED:
		if(sd){
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			map_foreachinrange(skill_greed,bl,
				skill_get_splash(skill_id, skill_lv),BL_ITEM,bl);
		}
		break;

	case SA_ELEMENTWATER:
	case SA_ELEMENTFIRE:
	case SA_ELEMENTGROUND:
	case SA_ELEMENTWIND:
		if (sd && !dstmd && status_has_mode(tstatus, MD_STATUS_IMMUNE)) // Only works on non-immune monsters.
			break;
	case NPC_ATTRICHANGE:
	case NPC_CHANGEWATER:
	case NPC_CHANGEGROUND:
	case NPC_CHANGEFIRE:
	case NPC_CHANGEWIND:
	case NPC_CHANGEPOISON:
	case NPC_CHANGEHOLY:
	case NPC_CHANGEDARKNESS:
	case NPC_CHANGETELEKINESIS:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start2(bl, type, 100, skill_lv, skill_get_ele(skill_id,skill_lv),
				skill_get_time(skill_id, skill_lv)));
		break;
	case NPC_CHANGEUNDEAD:
		//This skill should fail if target is wearing bathory/evil druid card [Brainstorm]
		//TO-DO This is ugly, fix it
		if(tstatus->def_ele==ELE_UNDEAD || tstatus->def_ele==ELE_DARK) break;
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start2(bl, type, 100, skill_lv, skill_get_ele(skill_id,skill_lv),
				skill_get_time(skill_id, skill_lv)));
		break;

	case NPC_PROVOCATION:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		if (md) mob_unlocktarget(md, tick);
		break;

	case NPC_KEEPING:
	case NPC_BARRIER:
		{
			int skill_time = skill_get_time(skill_id,skill_lv);
			struct unit_data *ud = unit_bl2ud(bl);
			if (clif_skill_nodamage(src,bl,skill_id,skill_lv,
					sc_start(bl,type,100,skill_lv,skill_time))
			&& ud) {	//Disable attacking/acting/moving for skill's duration.
				ud->attackabletime =
				ud->canact_tick =
				ud->canmove_tick = tick + skill_time;
			}
		}
		break;

	case NPC_REBIRTH:
		if( md && md->state.rebirth )
			break; // only works once
		sc_start(bl,type,100,skill_lv,-1);
		break;

	case NPC_DARKBLESSING:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start2(bl,type,(50+skill_lv*5),skill_lv,skill_lv,skill_get_time2(skill_id,skill_lv)));
		break;

	case NPC_LICK:
		status_zap(bl, 0, 100);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,(skill_lv*20),skill_lv,skill_get_time2(skill_id,skill_lv)));
		break;

	case NPC_SUICIDE:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		status_kill(src); //When suiciding, neither exp nor drops is given.
		break;

	case NPC_SUMMONSLAVE:
	case NPC_SUMMONMONSTER:
	case NPC_DEATHSUMMON:
		if(md && md->skillidx >= 0)
			mob_summonslave(md,md->db->skill[md->skillidx].val,skill_lv,skill_id);
		break;

	case NPC_CALLSLAVE:
		mob_warpslave(src,MOB_SLAVEDISTANCE);
		break;

	case NPC_RANDOMMOVE:
		if (md) {
			md->next_walktime = tick - 1;
			mob_randomwalk(md,tick);
		}
		break;

	case NPC_SPEEDUP:
		{
			// or does it increase casting rate? just a guess xD
			int i = SC_ASPDPOTION0 + skill_lv - 1;
			if (i > SC_ASPDPOTION3)
				i = SC_ASPDPOTION3;
			clif_skill_nodamage(src,bl,skill_id,skill_lv,
				sc_start(bl,(sc_type)i,100,skill_lv,skill_lv * 60000));
		}
		break;

	case NPC_REVENGE:
		// not really needed... but adding here anyway ^^
		if (md && md->master_id > 0) {
			struct block_list *mbl, *tbl;
			if ((mbl = map_id2bl(md->master_id)) == NULL ||
				(tbl = battle_gettargeted(mbl)) == NULL)
				break;
			md->state.provoke_flag = tbl->id;
			mob_target(md, tbl, sstatus->rhw.range);
		}
		break;

	case NPC_RUN:
		if (md && unit_escape(src, bl, rnd() % 10 + 1))
			mob_unlocktarget(md, tick);
		break;

	case NPC_TRANSFORMATION:
	case NPC_METAMORPHOSIS:
		if(md && md->skillidx >= 0) {
			int class_ = mob_random_class (md->db->skill[md->skillidx].val,0);
			if (skill_lv > 1) //Multiply the rest of mobs. [Skotlex]
				mob_summonslave(md,md->db->skill[md->skillidx].val,skill_lv-1,skill_id);
			if (class_) mob_class_change(md, class_);
		}
		break;

	case NPC_EMOTION_ON:
	case NPC_EMOTION:
		//val[0] is the emotion to use.
		//NPC_EMOTION & NPC_EMOTION_ON can change a mob's mode 'permanently' [Skotlex]
		//val[1] 'sets' the mode
		//val[2] adds to the current mode
		//val[3] removes from the current mode
		//val[4] if set, asks to delete the previous mode change.
		if(md && md->skillidx >= 0 && tsc)
		{
			clif_emotion(bl, md->db->skill[md->skillidx].val[0]);
			if(md->db->skill[md->skillidx].val[4] && tsce)
				status_change_end(bl, type, INVALID_TIMER);

			//If mode gets set by NPC_EMOTION then the target should be reset [Playtester]
			if (skill_id == NPC_EMOTION && md->db->skill[md->skillidx].val[1])
				mob_unlocktarget(md, tick);

			if(md->db->skill[md->skillidx].val[1] || md->db->skill[md->skillidx].val[2])
				sc_start4(src, type, 100, skill_lv,
					md->db->skill[md->skillidx].val[1],
					md->db->skill[md->skillidx].val[2],
					md->db->skill[md->skillidx].val[3],
					skill_get_time(skill_id, skill_lv));

			//Reset aggressive state depending on resulting mode
			md->state.aggressive = md->status.mode&MD_ANGRY ? 1 : 0;
		}
		break;

	case NPC_POWERUP:
		sc_start(bl,SC_INCATKRATE,100,200,skill_get_time(skill_id, skill_lv));
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,100,100,skill_get_time(skill_id, skill_lv)));
		break;

	case NPC_AGIUP:
			sc_start(bl, SC_SPEEDUP1, 100, 100, skill_get_time(skill_id, skill_lv)); // Fix 100% movement speed in all levels. [Frost]
			clif_skill_nodamage(src, bl, skill_id, skill_lv,
				sc_start(bl, type, 100, 100, skill_get_time(skill_id, skill_lv)));
		break;

	case NPC_INVISIBLE:
		//Have val4 passed as 6 is for "infinite cloak" (do not end on attack/skill use).
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start4(bl,type,100,skill_lv,0,0,6,skill_get_time(skill_id,skill_lv)));
		break;

	case NPC_SIEGEMODE:
		// not sure what it does
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;

	case WE_MALE:
		if (status_get_hp(src) > status_get_max_hp(src) / 10) {
			int hp_rate = (!skill_lv) ? 0 : skill_get_hp_rate(skill_id, skill_lv);
			int gain_hp= tstatus->max_hp*abs(hp_rate)/100; // The earned is the same % of the target HP than it costed the caster. [Skotlex]
			clif_skill_nodamage(src,bl,skill_id,status_heal(bl, gain_hp, 0, 0),1);
		}
		break;
	case WE_FEMALE:
		if (status_get_sp(src) > status_get_max_sp(src) / 10) {
			int sp_rate = (!skill_lv) ? 0 : skill_get_sp_rate(skill_id, skill_lv);
			int gain_sp=tstatus->max_sp*abs(sp_rate)/100;// The earned is the same % of the target SP than it costed the caster. [Skotlex]
			clif_skill_nodamage(src,bl,skill_id,status_heal(bl, 0, gain_sp, 0),1);
		}
		break;

	// parent-baby skills
	case WE_BABY:
		if(sd){
			struct map_session_data *f_sd = pc_get_father(sd);
			struct map_session_data *m_sd = pc_get_mother(sd);
			if ((!f_sd && !m_sd) // if neither was found
				|| (sd->status.party_id != 0 && //not in same party
				((!f_sd || sd->status.party_id != f_sd->status.party_id)
					&& (!m_sd || sd->status.party_id != m_sd->status.party_id) //if both are online they should all be in same team
					))
				|| ((!f_sd || !check_distance_bl(&sd->bl, &f_sd->bl, AREA_SIZE)) //not in same screen
					&& (!m_sd || !check_distance_bl(&sd->bl, &m_sd->bl, AREA_SIZE)))
				) {
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				map_freeblock_unlock();
				return 0;
			}
			status_change_start(src,bl,SC_STUN,10000,skill_lv,0,0,0,skill_get_time2(skill_id,skill_lv),8);
			if (f_sd) sc_start(&f_sd->bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
			if (m_sd) sc_start(&m_sd->bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
		}
		break;

	case WE_CALLALLFAMILY:
		if(sd)
		{
			struct map_session_data *p_sd = pc_get_partner(sd);
			struct map_session_data *c_sd = pc_get_child(sd);

			// Fail if no family members are found.
			if(!p_sd && !c_sd)
			{
				clif_skill_fail(sd,skill_id,0,0,0);
				map_freeblock_unlock();
				return 0;
			}

			// Partner must be on the same map and in same party as the caster.
			if ( p_sd  && !status_isdead(&p_sd->bl) && p_sd->mapindex == sd->mapindex && p_sd->status.party_id == sd->status.party_id )
				pc_setpos(p_sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_TELEPORT);

			// Child must be on the same map and in same party as the caster.
			if ( c_sd && !status_isdead(&c_sd->bl) && c_sd->mapindex == sd->mapindex && c_sd->status.party_id == sd->status.party_id )
				pc_setpos(c_sd, sd->mapindex, sd->bl.x, sd->bl.y, CLR_TELEPORT);
		}
		break;

	case WE_ONEFOREVER:
		if (sd)
		{
			struct map_session_data *p_sd = pc_get_partner(sd);
			struct map_session_data *c_sd = pc_get_child(sd);

			// Fail if no family members are found.
			if(!p_sd && !c_sd)
			{
				clif_skill_fail(sd,skill_id,0,0,0);
				map_freeblock_unlock();
				return 0;
			}

			if(sd && (map_flag_gvg(bl->m) || map[bl->m].flag.battleground))
			{	//No reviving in WoE grounds!
				clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}

			if (!status_isdead(bl))
				break;
			{
				int per = 30, sper = 0;

				if (battle_check_undead(tstatus->race,tstatus->def_ele))
					break;

				if (tsc && tsc->data[SC_HELLPOWER])
					break;

				if (map[bl->m].flag.pvp && dstsd && dstsd->pvp_point < 0)
					break;

				if(dstsd && dstsd->special_state.restart_full_recover)
					per = sper = 100;

				// Can only revive family members.
				if ( dstsd && ( dstsd->status.char_id == sd->status.partner_id || dstsd->status.char_id == sd->status.child ) )
					if (status_revive(bl, per, sper))
						clif_skill_nodamage(src,bl,WE_ONEFOREVER,skill_lv,1);
			}
		}
		break;

	case WE_CHEERUP:
		if(sd)
		{
			struct map_session_data *f_sd = pc_get_father(sd);
			struct map_session_data *m_sd = pc_get_mother(sd);

			// Fail if no parents are found.
			if(!f_sd && !m_sd)
			{
				clif_skill_fail(sd,skill_id,0,0,0);
				map_freeblock_unlock();
				return 0;
			}

			if(flag&1)
			{// Buff can only be given to parents in 7x7 AoE around baby.
				if ( dstsd && ( dstsd->status.char_id == sd->status.father || dstsd->status.char_id == sd->status.mother ) )
					clif_skill_nodamage(src, bl, skill_id, skill_lv, sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
			}
			else if( sd )
				map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), BL_PC, src, skill_id, skill_lv, tick, flag|BCT_ALL|1, skill_castend_nodamage_id);
		}
		break;

	case PF_HPCONVERSION:
		{
			int hp, sp;
			hp = sstatus->max_hp/10;
			sp = hp * 10 * skill_lv / 100;
			if (!status_charge(src,hp,0)) {
				if (sd) clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			status_heal(bl,0,sp,2);
		}
		break;

	case MA_REMOVETRAP:
	case HT_REMOVETRAP:
		{
			struct skill_unit* su;
			struct skill_unit_group* sg;
			su = BL_CAST(BL_SKILL, bl);

			// Mercenaries can remove any trap
			// Players can only remove their own traps or traps on Vs maps.
			if( su && (sg = su->group) && (src->type == BL_MER || sg->src_id == src->id || map_flag_vs(bl->m)) && (skill_get_inf2(sg->skill_id)&INF2_TRAP) )
			{
				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
				if( sd && !(sg->unit_id == UNT_USED_TRAPS || (sg->unit_id == UNT_ANKLESNARE && sg->val2 != 0 )) && sg->unit_id != UNT_THORNS_TRAP )
				{ // prevent picking up expired traps
					if( battle_config.skill_removetrap_type )
					{ // get back all items used to deploy the trap
						for( i = 0; i < 10; i++ )
						{
							if(skill_get_itemid(su->group->skill_id, i + 1) > 0 )
							{
								int flag;
								struct item item_tmp;
								memset(&item_tmp,0,sizeof(item_tmp));
								item_tmp.nameid = skill_get_itemid(su->group->skill_id, i + 1);
								item_tmp.amount = skill_get_itemqty(su->group->skill_id, i + 1);
								if (item_tmp.nameid && (flag = pc_additem(sd, &item_tmp, item_tmp.amount, LOG_TYPE_OTHER)))
								{
									clif_additem(sd,0,0,flag);
									map_addflooritem(&item_tmp,item_tmp.amount,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,4,0,false);
								}
							}
						}
					}
					else
					{ // get back 1 trap
						struct item item_tmp;
						memset(&item_tmp,0,sizeof(item_tmp));
						item_tmp.nameid = su->group->item_id ? su->group->item_id : ITEMID_TRAP;
						item_tmp.identify = 1;
						if( item_tmp.nameid && (flag=pc_additem(sd,&item_tmp,1,LOG_TYPE_OTHER)) )
						{
							clif_additem(sd,0,0,flag);
							map_addflooritem(&item_tmp,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,4,0,false);
						}
					}
				}
				skill_delunit(su);
			}
			else if (sd)
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
		}
		break;
	case HT_SPRINGTRAP:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		{
			struct skill_unit *su=NULL;
			if((bl->type==BL_SKILL) && (su=(struct skill_unit *)bl) && (su->group) ){
				switch(su->group->unit_id){
					case UNT_ANKLESNARE:	// ankle snare
						if (su->group->val2 != 0 || su->group->val3 == SC_ESCAPE)
							// if it is already trapping something don't spring it,
							// remove trap should be used instead
							break;
						// otherwise fallthrough to below
					case UNT_BLASTMINE:
					case UNT_SKIDTRAP:
					case UNT_LANDMINE:
					case UNT_SHOCKWAVE:
					case UNT_SANDMAN:
					case UNT_FLASHER:
					case UNT_FREEZINGTRAP:
					case UNT_CLAYMORETRAP:
					case UNT_TALKIEBOX:
						su->group->unit_id = UNT_USED_TRAPS;
						clif_changetraplook(bl, UNT_USED_TRAPS);
						su->group->limit = DIFF_TICK32(tick + 1500, su->group->tick);
						su->limit = DIFF_TICK32(tick + 1500, su->group->tick);
				}
			}
		}
		break;
	case BD_ENCORE:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		if(sd)
			unit_skilluse_id(src,src->id,sd->skillid_dance,sd->skilllv_dance);
		break;

	case AS_SPLASHER:
		if(tstatus->mode&MD_STATUS_IMMUNE || tstatus-> hp > tstatus->max_hp*3/4) {
			if (sd) clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			map_freeblock_unlock();
			return 1;
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start4(bl,type,100,skill_lv,skill_id,src->id,skill_get_time(skill_id,skill_lv),1000));
		if (sd) skill_blockpc_start (sd, skill_id, skill_get_time(skill_id, skill_lv)+3000);
		break;

	case GN_SPORE_EXPLOSION:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start4(bl,type,100,skill_lv,skill_id,src->id,skill_get_time(skill_id,skill_lv),1000));
		break;

	case PF_MINDBREAKER:
		{
			if(status_has_mode(tstatus, MD_STATUS_IMMUNE) || battle_check_undead(tstatus->race,tstatus->def_ele))
			{
				map_freeblock_unlock();
				return 1;
			}

			if (tsce)
			{	//HelloKitty2 (?) explained that this silently fails when target is
				//already inflicted. [Skotlex]
				map_freeblock_unlock();
				return 1;
			}

			//Has a 55% + skill_lv*5% success chance.
			if (!clif_skill_nodamage(src,bl,skill_id,skill_lv,
				sc_start(bl,type,55+5*skill_lv,skill_lv,skill_get_time(skill_id,skill_lv))))
			{
				if (sd) clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				map_freeblock_unlock();
				return 0;
			}

			unit_skillcastcancel(bl,0);

			if(tsc && tsc->count){
				status_change_end(bl, SC_FREEZE, INVALID_TIMER);
				if(tsc->data[SC_STONE] && tsc->opt1 == OPT1_STONE)
					status_change_end(bl, SC_STONE, INVALID_TIMER);
				status_change_end(bl, SC_SLEEP, INVALID_TIMER);
			}

			if(dstmd)
				mob_target(dstmd,src,skill_get_range2(src,skill_id,skill_lv,true));
		}
		break;

	case PF_SOULCHANGE:
		{
			unsigned int sp1 = 0, sp2 = 0;
			if (dstmd) {
				if (dstmd->state.soul_change_flag) {
					if(sd) clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
					break;
				}
				dstmd->state.soul_change_flag = 1;
				sp2 = sstatus->max_sp * 3 /100;
				status_heal(src, 0, sp2, 2);
				clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
				break;
			}
			sp1 = sstatus->sp;
			sp2 = tstatus->sp;
			status_set_sp(src, sp2, 3);
			status_set_sp(bl, sp1, 3);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	// Slim Pitcher
	case CR_SLIMPITCHER:
		// Updated to block Slim Pitcher from working on barricades and guardian stones.
		if( dstmd && (dstmd->mob_id == MOBID_EMPERIUM || status_get_class_(bl) == CLASS_BATTLEFIELD))
			break;
		if (potion_hp || potion_sp) {
			int hp = potion_hp, sp = potion_sp;
			hp = hp * (100 + (tstatus->vit<<1))/100;
			sp = sp * (100 + (tstatus->int_<<1))/100;
			if (dstsd) {
				if (hp)
					hp = hp * (100 + pc_checkskill(dstsd,SM_RECOVERY)*10 + pc_skillheal2_bonus(dstsd, skill_id))/100;
				if (sp)
					sp = sp * (100 + pc_checkskill(dstsd,MG_SRECOVERY)*10 + pc_skillheal2_bonus(dstsd, skill_id))/100;
			}
			if ( tsc ) {
				if( tsc->data[SC_CRITICALWOUND] ) {
					hp -= hp * tsc->data[SC_CRITICALWOUND]->val2 / 100;
					sp -= sp * tsc->data[SC_CRITICALWOUND]->val2 / 100;
				}
				if( tsc->data[SC_DEATHHURT] )
					hp -= hp * 20 / 100;
			}
			if(hp > 0)
				clif_skill_nodamage(NULL,bl,AL_HEAL,hp,1);
			if(sp > 0)
				clif_skill_nodamage(NULL,bl,MG_SRECOVERY,sp,1);
			status_heal(bl,hp,sp,0);
		}
		break;
	// Full Chemical Protection
	case CR_FULLPROTECTION:
		{
		unsigned int equip[] = { EQP_WEAPON, EQP_SHIELD, EQP_ARMOR, EQP_HEAD_TOP };
		int i, s = 0, skilltime = skill_get_time(skill_id, skill_lv);
		for (i = 0; i < 4; i++) {
			if (bl->type != BL_PC || (dstsd && pc_checkequip(dstsd, equip[i], false) < 0))
				continue;
				sc_start(bl,(sc_type)(SC_CP_WEAPON + i),100,skill_lv,skilltime);
				s++;
		}
		if (sd && !s) {
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
			map_freeblock_unlock(); // Don't consume item requirements
			return 0;
			}
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case RG_CLEANER:	//AppleGirl
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;

	case CG_LONGINGFREEDOM:
		{
			if (tsc && !tsce && (tsce=tsc->data[SC_DANCING]) && tsce->val4
				&& (tsce->val1&0xFFFF) != CG_MOONLIT) //Can't use Longing for Freedom while under Moonlight Petals. [Skotlex]
			{
				clif_skill_nodamage(src,bl,skill_id,skill_lv,
					sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
			}
		}
		break;

	case CG_TAROTCARD:
		{
		int card = -1;
		if (tsc && tsc->data[SC_TAROTCARD]) {
			//Target currently has the SUN tarot card effect and is immune to any other effect
			map_freeblock_unlock();
			return 0;
		}
			if (rnd() % 100 > skill_lv * 8 || (tsc && tsc->data[SC_BASILICA]) ||
				(dstmd && ((dstmd->guardian_data && dstmd->mob_id == MOBID_EMPERIUM) || status_get_class_(bl) == CLASS_BATTLEFIELD))) {
				if( sd )
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);

				map_freeblock_unlock();
				return 0;
			}
			status_zap(src,0,skill_get_sp(skill_id, skill_lv)); // consume sp only if succeeded [Inkfish]
			card = skill_tarotcard(src, bl, skill_id, skill_lv, tick); // actual effect is executed here
			if (card == 6)
				clif_specialeffect(src, 522 + card, AREA);
			else
				clif_specialeffect(bl, 522 + card, AREA);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case SL_ALCHEMIST:
	case SL_ASSASIN:
	case SL_BARDDANCER:
	case SL_BLACKSMITH:
	case SL_CRUSADER:
	case SL_HUNTER:
	case SL_KNIGHT:
	case SL_MONK:
	case SL_PRIEST:
	case SL_ROGUE:
	case SL_SAGE:
	case SL_SOULLINKER:
	case SL_STAR:
	case SL_SUPERNOVICE:
	case SL_WIZARD:
	case SL_DEATHKNIGHT:
	case SL_COLLECTOR:
	case SL_NINJA:
	case SL_GUNNER:
		if ( tsc && (tsc->data[SC_SOULGOLEM] || tsc->data[SC_SOULSHADOW] || tsc->data[SC_SOULFALCON] || tsc->data[SC_SOULFAIRY]))
		{// Soul links from Soul Linker and Soul Reaper skills don't stack.
			clif_skill_fail(sd,skill_id,0,0,0);
			break;
		}
		//NOTE: here, 'type' has the value of the associated MAPID, not of the SC_SPIRIT constant.
		if (sd && !(dstsd && ((dstsd->class_&MAPID_UPPERMASK) == type || // Can below code be optomized??? [Rytech]
			(skill_id == SL_NINJA && (dstsd->class_&MAPID_UPPERMASK) == MAPID_KAGEROUOBORO) ||
			(skill_id == SL_GUNNER && (dstsd->class_&MAPID_UPPERMASK) == MAPID_REBELLION)))) {
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			break;
		}
		if (skill_id == SL_SUPERNOVICE && dstsd && dstsd->die_counter && !(rnd()%100))
		{	//Erase death count 1% of the casts
			dstsd->die_counter = 0;
			pc_setglobalreg(dstsd,"PC_DIE_COUNTER", 0);
			clif_specialeffect(bl, 0x152, AREA);
			//SC_SPIRIT invokes status_calc_pc for us.
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start4(bl,SC_SPIRIT,100,skill_lv,skill_id,0,0,skill_get_time(skill_id,skill_lv)));
		sc_start(src,SC_SMA,100,skill_lv,skill_get_time(SL_SMA,skill_lv));
		break;
	case SL_HIGH:
		if ( tsc && (tsc->data[SC_SOULGOLEM] || tsc->data[SC_SOULSHADOW] || tsc->data[SC_SOULFALCON] || tsc->data[SC_SOULFAIRY]))
		{// Soul links from Soul Linker and Soul Reaper skills don't stack.
			clif_skill_fail(sd,skill_id,0,0,0);
			break;
		}
		if (sd && !(dstsd && (dstsd->class_&JOBL_UPPER) && !(dstsd->class_&JOBL_2) && dstsd->status.base_level < 70)) {
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			break;
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start4(bl,type,100,skill_lv,skill_id,0,0,skill_get_time(skill_id,skill_lv)));
		sc_start(src,SC_SMA,100,skill_lv,skill_get_time(SL_SMA,skill_lv));
		break;

	case SP_SOULGOLEM:
	case SP_SOULSHADOW:
	case SP_SOULFALCON:
	case SP_SOULFAIRY:
		if ( !dstsd )
		{// Only player's can be soul linked.
			clif_skill_fail(sd,skill_id,0,0,0);
			break;
		}
		if ( tsc )
		{
			if ( tsc->data[status_skill2sc(skill_id)] )
			{// Allow refreshing a already active soul link.
				clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
				break;
			}
			else if ( tsc->data[SC_SPIRIT] || tsc->data[SC_SOULGOLEM] || tsc->data[SC_SOULSHADOW] || tsc->data[SC_SOULFALCON] || tsc->data[SC_SOULFAIRY] )
			{// Soul links from Soul Linker and Soul Reaper skills don't stack.
				clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		break;

	case SP_SOULREVOLVE:
		if ( !(tsc && (tsc->data[SC_SPIRIT] || tsc->data[SC_SOULGOLEM] || tsc->data[SC_SOULSHADOW] || 
			tsc->data[SC_SOULFALCON] || tsc->data[SC_SOULFAIRY])) )
		{
			clif_skill_fail(sd,skill_id,0,0,0);
			break;
		}
		status_heal(bl,0,50*skill_lv,2);
		status_change_end(bl, SC_SPIRIT, INVALID_TIMER);
		status_change_end(bl, SC_SOULGOLEM, INVALID_TIMER);
		status_change_end(bl, SC_SOULSHADOW, INVALID_TIMER);
		status_change_end(bl, SC_SOULFALCON, INVALID_TIMER);
		status_change_end(bl, SC_SOULFAIRY, INVALID_TIMER);
		break;

	case SL_SWOO:
		if (tsce) {
			if (sd)
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
			status_change_start(src, src, SC_STUN, 10000, skill_lv, 0, 0, 0, 10000, 8);
			status_change_end(bl, SC_SWOO, INVALID_TIMER);
			break;
		}
	case SL_SKA: // [marquis007]
	case SL_SKE:
		if (sd && !battle_config.allow_es_magic_pc && bl->type != BL_MOB) {
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			status_change_start(src,src,SC_STUN,10000,skill_lv,0,0,0,500,10);
			break;
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		if (skill_id == SL_SKE)
			sc_start(src,SC_SMA,100,skill_lv,skill_get_time(SL_SMA,skill_lv));
		break;

	// New guild skills [Celest]
	case GD_BATTLEORDER:
		if(flag&1) {
			if (status_get_guild_id(src) == status_get_guild_id(bl))
				sc_start(bl,type,100,skill_lv,skill_get_time(skill_id, skill_lv));
		} else if (status_get_guild_id(src)) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			map_foreachinrange(skill_area_sub, src,
				skill_get_splash(skill_id, skill_lv), BL_PC,
				src,skill_id,skill_lv,tick, flag|BCT_GUILD|1,
				skill_castend_nodamage_id);
			if (sd)
				guild_block_skill(sd,skill_get_time2(skill_id,skill_lv));
		}
		break;
	case GD_REGENERATION:
		if(flag&1) {
			if (status_get_guild_id(src) == status_get_guild_id(bl))
				sc_start(bl,type,100,skill_lv,skill_get_time(skill_id, skill_lv));
		} else if (status_get_guild_id(src)) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			map_foreachinrange(skill_area_sub, src,
				skill_get_splash(skill_id, skill_lv), BL_PC,
				src,skill_id,skill_lv,tick, flag|BCT_GUILD|1,
				skill_castend_nodamage_id);
			if (sd)
				guild_block_skill(sd,skill_get_time2(skill_id,skill_lv));
		}
		break;
	case GD_RESTORE:
		if(flag&1) {
			if (status_get_guild_id(src) == status_get_guild_id(bl))
				clif_skill_nodamage(src,bl,AL_HEAL,status_percent_heal(bl,90,90),1);
		} else if (status_get_guild_id(src)) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			map_foreachinrange(skill_area_sub, src,
				skill_get_splash(skill_id, skill_lv), BL_PC,
				src,skill_id,skill_lv,tick, flag|BCT_GUILD|1,
				skill_castend_nodamage_id);
			if (sd)
				guild_block_skill(sd,skill_get_time2(skill_id,skill_lv));
		}
		break;
	case GD_EMERGENCYCALL:
	case GD_ITEMEMERGENCYCALL:
		{
			// i don't know if it actually summons in a circle, but oh well. ;P
			int8 dx[9] = { -1, 1, 0, 0,-1, 1,-1, 1, 0 };
			int8 dy[9] = { 0, 0, 1,-1, 1,-1,-1, 1, 0 };
			uint8 j = 0, calls = 0, called = 0;
			struct guild* g;
			g = sd ? sd->guild:guild_search(status_get_guild_id(src));
			if(!g)
				break;

			if (skill_id == GD_ITEMEMERGENCYCALL)
				switch (skill_lv) {
				case 1:	calls = 7; break;
				case 2:	calls = 12; break;
				case 3:	calls = 20; break;
				default: calls = 0;	break;
				}

			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			for (i = 0; i < g->max_member && (!calls || (calls && called < calls)); i++, j++) {
				if (j > 8)
					j = 0;
				if ((dstsd = g->member[i].sd) != NULL && sd != dstsd && !dstsd->state.autotrade && !pc_isdead(dstsd)) {
					if (map[dstsd->bl.m].flag.nowarp && !map_flag_gvg2(dstsd->bl.m))
						continue;
					if (!pc_job_can_entermap((enum e_job)dstsd->status.class_, src->m, dstsd->gmlevel))
						continue;
					if(map_getcell(src->m,src->x+dx[j],src->y+dy[j],CELL_CHKNOREACH))
						dx[j] = dy[j] = 0;
					pc_setpos(dstsd, map_id2index(src->m), src->x+dx[j], src->y+dy[j], CLR_RESPAWN);
					called++;
				}
			}
			if (sd)
				guild_block_skill(sd,skill_get_time2(skill_id,skill_lv));
		}
		break;

	case AB_CONVENIO:
		{
			int dx[9]={-1, 1, 0, 0,-1, 1,-1, 1, 0};
			int dy[9]={ 0, 0, 1,-1, 1,-1,-1, 1, 0};
			int j = 0;
			int mi;
			struct party_data *p;

			// Only allow players to cast this.
			// Party check also needed incase GM casts it.
			if (!sd || !sd->status.party_id) break;

			// Fail on maps that don't allow teleporting.
			if (map[src->m].flag.noteleport)
			{
				clif_skill_teleportmessage(sd,0);
				break;
			}

			// Set party data.
			if ((p = party_search(sd->status.party_id)) == NULL)
				break;

			// Find the caster's position in the party to do a leader check.
			ARR_FIND( 0, MAX_PARTY, mi, p->data[mi].sd == sd );
			if (mi == MAX_PARTY)
				break;

			// Caster must be the leader of the party or else it fails.
			if (!p->party.member[mi].leader)
			{// Need to be a party leader.
				clif_displaymessage(sd->fd, "You need to be the party leader to use this skill.");
				break;
			}

			// Everything passes? Good lets start teleporting other party members.
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			for(i = 0; i < MAX_PARTY; i++, j++)
			{
				if (j>8) j=0;
				if ((dstsd = p->data[i].sd) != NULL && sd != dstsd && !dstsd->state.autotrade)
				{// Must be in the same map as the caster.
					if (sd->mapindex != dstsd->mapindex)
						continue;
					if(map_getcell(src->m,src->x+dx[j],src->y+dy[j],CELL_CHKNOREACH))
						dx[j] = dy[j] = 0;
					pc_setpos(dstsd, map_id2index(src->m), src->x+dx[j], src->y+dy[j], CLR_TELEPORT);
				}
			}
		}
		break;

	case SG_FEEL:
		//AuronX reported you CAN memorize the same map as all three. [Skotlex]
		if (sd) {
			if(!sd->feel_map[skill_lv-1].index)
				clif_feel_req(sd->fd,sd, skill_lv);
			else
				clif_feel_info(sd, skill_lv-1, 1);
		}
		break;

	case SG_HATE:
		if (sd) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			if (!pc_set_hate_mob(sd, skill_lv-1, bl))
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
		}
		break;

	case SJ_DOCUMENT:
		if (sd)
		{
			if ( skill_lv == 1 || skill_lv == 3 )
				pc_resetfeel(sd);

			if ( skill_lv == 2 || skill_lv == 3 )
				pc_resethate(sd);

			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case GS_GLITTERING:
		if(sd) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			if(rnd()%100 < (20+10*skill_lv))
				pc_addspiritball(sd,skill_get_time(skill_id,skill_lv),10);
			//If player knows Rich's Coin, failure will not remove a coin sphere.
			else if(sd->spiritball > 0 && !(pc_checkskill(sd,RL_RICHS_COIN) > 0))
				pc_delspiritball(sd,1,0);
		}
		break;

	case GS_CRACKER:
		/* per official standards, this skill works on players and mobs. */
		if (sd && (dstsd || dstmd))
		{
			i = 65 - 5 * distance_bl(src, bl); //Base rate
			if (i < 30) i = 30;
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			sc_start(bl, SC_STUN, i, skill_lv, skill_get_time2(skill_id, skill_lv));
		}
		break;

	case AM_CALLHOMUN:	//[orn]
		if (sd && !hom_call(sd))
			clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
		break;

	case AM_REST:
		if (sd)
		{
			if (hom_vaporize(sd,1))
				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			else
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
		}
		break;

	case HAMI_CASTLE:	//[orn]
		if(rnd()%100 < 20*skill_lv && src != bl)
		{
			int x,y;
			x = src->x;
			y = src->y;
			if (hd)
				skill_blockhomun_start(hd, skill_id, skill_get_time2(skill_id,skill_lv));

			if (unit_movepos(src,bl->x,bl->y,0,0)) {
				clif_skill_nodamage(src,src,skill_id,skill_lv,1); // Homunc
				clif_slide(src,bl->x,bl->y) ;
				if (unit_movepos(bl,x,y,0,0))
				{
					clif_skill_nodamage(bl,bl,skill_id,skill_lv,1); // Master
					clif_slide(bl,x,y) ;
				}

				//TODO: Shouldn't also players and the like switch targets?
				map_foreachinrange(skill_chastle_mob_changetarget,src,
					AREA_SIZE, BL_MOB, bl, src);
			}
		}
		// Failed
		else if (hd && hd->master)
			clif_skill_fail(hd->master, skill_id, USESKILL_FAIL_LEVEL, 0,0);
		else if (sd)
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0,0);
		break;
	case HVAN_CHAOTIC:	//[orn]
		{
			static const int per[5][2]={{20,50},{50,60},{25,75},{60,64},{34,67}};
			int rnd_ = rnd()%100;
			i = (skill_lv-1)%5;
			if(rnd_<per[i][0]) //Self
				bl = src;
			else if(rnd_<per[i][1]) //Master
				bl = battle_get_master(src);
			else //Enemy
				bl = map_id2bl(battle_gettarget(src));

			if (!bl) bl = src;
			i = skill_calc_heal(src, bl, skill_id, 1+rnd()%skill_lv, true);
			//Eh? why double skill packet?
			clif_skill_nodamage(src,bl,AL_HEAL,i,1);
			clif_skill_nodamage(src,bl,skill_id,i,1);
			status_heal(bl, i, 0, 0);
		}
		break;
	//Homun single-target support skills [orn]
	case HAMI_BLOODLUST:
	case HFLI_FLEET:
	case HFLI_SPEED:
	case HLIF_CHANGE:
	case MH_GOLDENE_FERSE:
	case MH_ANGRIFFS_MODUS:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		if (hd)
			skill_blockhomun_start(hd, skill_id, skill_get_time2(skill_id,skill_lv));
		break;

	//Homunculus buffs that affects both the homunculus and its master.
	case HLIF_AVOID:
	case HAMI_DEFENCE:
	case MH_OVERED_BOOST:
	case MH_GRANITIC_ARMOR:
		i = skill_get_time(skill_id,skill_lv);
		clif_skill_nodamage(bl,bl,skill_id,skill_lv,sc_start(bl,type,100,skill_lv,i)); // Master
		clif_skill_nodamage(src,src,skill_id,skill_lv,sc_start(src,type,100,skill_lv,i)); // Homunc
		break;

	case MH_PAIN_KILLER:
		clif_skill_nodamage(src, bl, skill_id, skill_lv, sc_start2(bl, type, 100, skill_lv, status_get_base_lv_effect(src), skill_get_time(skill_id, skill_lv)));
		break;

	//Homunculus buffs that affects only its master.
	case MH_LIGHT_OF_REGENE:
		if (hd)
		{// Homunculus intimact is set to a random value in the cordial range.
			hd->homunculus.intimacy = rnd_value(75100 , 85000);
			if (hd->master)
				clif_send_homdata(hd->master,SP_INTIMATE,hd->homunculus.intimacy/100);
		}
		clif_skill_nodamage(bl,bl,skill_id,skill_lv,sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		break;

	case MH_SILENT_BREEZE:
		{// If skill is used by anything not a homunculus, hunger value will be treated as max possible.
			short hunger = 100;
			if (hd)// Checks hunger when used by a homunculus.
				hunger = hd->homunculus.hunger;
			clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
			if ( tsc )
			{// Status's the skill cures.
				const enum sc_type scs[] = { SC_DEEPSLEEP, SC_HALLUCINATION, SC_HARMONIZE, SC_SIREN, SC_MANDRAGORA };
				for (i = 0; i < ARRAYLENGTH(scs); i++)
					if (tsc->data[scs[i]])
						status_change_end(bl, scs[i], INVALID_TIMER);
			}
			sc_start(bl,SC_SILENCE,100,skill_lv,skill_get_time(skill_id,skill_lv));
			status_heal(bl, 5 * (hunger + status_get_base_lv_effect(src)), 0, 2);
			//Its said that the homunculus is silenced too, but I need a confirm on that first.
			//clif_skill_nodamage(src,src,skill_id,skill_lv,sc_start(src,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		}
		break;

	// This was to allow placing a safety wall on both the master and the homunculus
	// but because this bypasses skill_castend_pos, important checks are skipped which
	// are supposed to prevent stacking the AoE's, placing them on top of pneuma's,
	// spawning more then a set number of AoE's (1 pair in this case), other things.
	// For now ill leave this disabled until i can find a way to pass this through
	// the correct functions for doing the checks. [Rytech]
	//case MH_STEINWAND:
	//	{
	//		skill_castend_pos2(src, src->x, src->y, skill_id, skill_lv, tick, flag);
	//		skill_castend_pos2(src, bl->x, bl->y, skill_id, skill_lv, tick, flag);
	//	}
	//	break;

	case MH_MAGMA_FLOW:
		{
			if ( flag&2 )
			{// Splash AoE around the homunculus should only trigger by chance when status is active.
				skill_area_temp[1] = 0;
				clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
				map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag|BCT_ENEMY|SD_SPLASH|1, skill_castend_damage_id);
			}
			else if ( !flag )
			{// Using the skill normally only starts the status. It does not trigger a splash AoE attack this way.
				clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
			}
		}
		break;

	case EL_CIRCLE_OF_FIRE:
		{
			if ( flag&2 )
			{// Splash AoE around the master should only trigger by chance when status is active.
				skill_area_temp[1] = 0;
				clif_skill_damage(src, bl, tick, status_get_amotion(src), 0, 0, 1, skill_id, skill_lv, 5);
				map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag|BCT_ENEMY|SD_SPLASH|1, skill_castend_damage_id);
			}
			else if ( !flag )
			{// Using the skill normally only starts the status. It does not trigger a splash AoE attack this way.
				if ( sd )
					clif_skill_damage(src, bl, tick, status_get_amotion(src), 0, 0, 1, skill_id, skill_lv, 6);
				else
				{
					clif_skill_damage(src, &ed->master->bl, tick, status_get_amotion(src), 0, 0, 1, skill_id, skill_lv, 6);
					sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
				}
			}
		}
		break;

	case MH_PYROCLASTIC:
		i = skill_get_time(skill_id,skill_lv);
		clif_skill_nodamage(bl, bl, skill_id, skill_lv, sc_start2(bl, type, 100, skill_lv, status_get_base_lv_effect(src), i)); // Master
		clif_skill_nodamage(src, src, skill_id, skill_lv, sc_start2(src, type, 100, skill_lv, status_get_base_lv_effect(src), i)); // Homunc
		break;

	case NPC_DRAGONFEAR:
		if (flag&1) {
			const enum sc_type sc[] = { SC_STUN, SC_SILENCE, SC_CONFUSION, SC_BLEEDING };
			int j;
			j = i = rnd()%ARRAYLENGTH(sc);
			while ( !sc_start2(bl,sc[i],100,skill_lv,src->id,skill_get_time2(skill_id,i+1)) ) {
				i++;
				if ( i == ARRAYLENGTH(sc) )
					i = 0;
				if (i == j)
					break;
			}
			break;
		}
	case NPC_WIDEBLEEDING:
	case NPC_WIDECONFUSE:
	case NPC_WIDECURSE:
	case NPC_WIDEFREEZE:
	case NPC_WIDESLEEP:
	case NPC_WIDESILENCE:
	case NPC_WIDESTONE:
	case NPC_WIDESTUN:
	case NPC_SLOWCAST:
	case NPC_WIDEHELLDIGNITY:
	case NPC_WIDEHEALTHFEAR:
	case NPC_WIDEBODYBURNNING:
	case NPC_WIDEFROSTMISTY:
	case NPC_WIDECOLD:
	case NPC_WIDE_DEEP_SLEEP:
	case NPC_WIDESIREN:
		if (flag & 1) {
			switch (type) {
			case SC_BURNING:
				status_change_start(src, bl, type, 10000, skill_lv, 1000, src->id, 0, skill_get_time2(skill_id, skill_lv), 0);
				break;
			case SC_SIREN:
				status_change_start(src, bl, type, 10000, skill_lv, src->id, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
				break;
			default:
				status_change_start(src, bl, type, 10000, skill_lv, src->id, 0, 0, skill_get_time2(skill_id, skill_lv), 0);
			}
		}
		else {
			skill_area_temp[2] = 0; //For SD_PREAMBLE
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			map_foreachinrange(skill_area_sub, bl,
				skill_get_splash(skill_id, skill_lv),BL_CHAR,
				src,skill_id,skill_lv,tick, flag|BCT_ENEMY|SD_PREAMBLE|1,
				skill_castend_nodamage_id);
		}
		break;

	case RK_ENCHANTBLADE:
		clif_skill_nodamage(src, bl, skill_id, skill_lv, sc_start2(bl, type, 100, skill_lv, (100 + 20 * skill_lv)*status_get_base_lv_effect(src) / 150 + sstatus->int_, skill_get_time(skill_id, skill_lv)));
		break;

	case RK_DRAGONHOWLING:// 3ceam v1
		if( flag&1 )
			sc_start( bl, type, ( 50 + 6 * skill_lv ), skill_lv, skill_get_time( skill_id, skill_lv ) );
		else
		{
			skill_area_temp[2] = 0;
			clif_skill_nodamage( src, bl, skill_id, skill_lv, 1 );
			map_foreachinrange( skill_area_sub, src, skill_get_splash( skill_id, skill_lv ), BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|SD_PREAMBLE|1, skill_castend_nodamage_id );
		}
		break;
	case RK_MILLENNIUMSHIELD:
	case RK_CRUSHSTRIKE:
	case RK_REFRESH:
	case RK_GIANTGROWTH:
	case RK_STONEHARDSKIN:
	case RK_VITALITYACTIVATION:
	case RK_ABUNDANCE:
		if (sd)
		{
			unsigned char lv = 1;// RK_GIANTGROWTH
			if (skill_id == RK_VITALITYACTIVATION)
				lv = 2;
			else if (skill_id == RK_STONEHARDSKIN)
				lv = 4;
 			else if(skill_id == RK_ABUNDANCE)
 				lv = 6;
 			else if (skill_id == RK_CRUSHSTRIKE)
 				lv = 7;
			else if (skill_id == RK_REFRESH)
				lv = 8;
			else if (skill_id == RK_MILLENNIUMSHIELD)
				lv = 9;
			if (pc_checkskill(sd, RK_RUNEMASTERY) >= lv)
				if ( skill_id == RK_MILLENNIUMSHIELD )
				{
					if ( sd )
					{
						unsigned char generate = rnd()%100 + 1;//Generates a number between 1 - 100 which is used to determine how many shields it will generate.
						unsigned char shieldnumber = 1;

						if ( generate >= 1 && generate <= 50 )//50% chance for 2 shields.
							shieldnumber = 2;
						else if ( generate >= 51 && generate <= 80 )//30% chance for 3 shields.
							shieldnumber = 3;
						else if ( generate >= 81 && generate <= 100 )//20% chance for 4 shields.
							shieldnumber = 4;

						// Remove old shields if any exist.
						pc_delshieldball(sd, sd->shieldball, 1);

						clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
						for (i = 0; i < shieldnumber; i++)
							pc_addshieldball(sd, skill_get_time(skill_id,skill_lv), shieldnumber, battle_config.millennium_shield_health);
					}

				}
				else if ( skill_id == RK_STONEHARDSKIN )
					clif_skill_nodamage(src, bl, skill_id, skill_lv, sc_start2(bl, type, 100, skill_lv, status_get_job_lv_effect(src)*(sd ? pc_checkskill(sd, RK_RUNEMASTERY) : 10), skill_get_time(skill_id, skill_lv)));
				else
					clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		}
		break;

	case RK_FIGHTINGSPIRIT:
		{
			unsigned char atk_bonus = 70 + 7 * party_foreachsamemap(party_sub_count, sd, skill_get_splash(skill_id, skill_lv));

			if( sd == NULL || sd->status.party_id == 0 || (flag&1) )
			{
				if ( src == bl )
					clif_skill_nodamage(src, bl, skill_id, skill_lv, sc_start2(bl, type, 100, atk_bonus, (sd ? pc_checkskill(sd, RK_RUNEMASTERY) : 10), skill_get_time(skill_id, skill_lv)));
				else
					clif_skill_nodamage(bl, bl, skill_id, skill_lv, sc_start(bl, type, 100, atk_bonus / 2, skill_get_time(skill_id, skill_lv)));
			}
			else if( sd && pc_checkskill(sd,RK_RUNEMASTERY) >= 5 )
				party_foreachsamemap(skill_area_sub, sd, skill_get_splash(skill_id, skill_lv), src, skill_id, skill_lv, tick, flag|BCT_PARTY|1, skill_castend_nodamage_id);
			}
		break;

	case RK_LUXANIMA:
		{
			const enum sc_type scs[] = { SC_REFRESH, SC_GIANTGROWTH, SC_STONEHARDSKIN, SC_VITALITYACTIVATION, SC_ABUNDANCE, SC_MILLENNIUMSHIELD };
			short rune_effect[] = { RK_REFRESH, RK_GIANTGROWTH, RK_STONEHARDSKIN, RK_VITALITYACTIVATION, RK_ABUNDANCE, RK_MILLENNIUMSHIELD };
			unsigned char buff_attempt = 100;// Safety to prevent infinite looping in the randomizer.
			short rune_buff = 0;
			bool rune_active = false;

			i = 0;

			if( sd == NULL || sd->status.party_id == 0 || (flag&1) )
			{// Remove the BCT_PARTY and 1 flags so only the flag for the rune_buff is left. 
				i = flag - 262145;

				// Convert from flag to array number.
				if ( i == LUX_REFRESH )
					rune_buff = 0;
				else if ( i == LUX_GIANTGROWTH )
					rune_buff = 1;
				else if ( i == LUX_STONEHARDSKIN )
					rune_buff = 2;
				else if ( i == LUX_VITALITYACTIVATION )
					rune_buff = 3;
				else if ( i == LUX_ABUNDANCE )
					rune_buff = 4;
				else if ( i == LUX_MILLENNIUMSHIELD )
					rune_buff = 5;

				if ( src == bl )// Sacrificed your rune buff.
				{
					if ( i == LUX_MILLENNIUMSHIELD )
						pc_delshieldball(sd, sd->shieldball, 0);
					else
						status_change_end(bl, scs[rune_buff], INVALID_TIMER);
				}
				else
				{// Buff surrounding party members with sacrificed rune buff.
					if ( i == LUX_MILLENNIUMSHIELD )
					{
						if ( dstsd )
						{
							unsigned char generate = rnd()%100 + 1;//Generates a number between 1 - 100 which is used to determine how many shields it will generate.
							unsigned char shieldnumber = 1;

							if ( generate >= 1 && generate <= 50 )//50% chance for 2 shields.
								shieldnumber = 2;
							else if ( generate >= 51 && generate <= 80 )//30% chance for 3 shields.
								shieldnumber = 3;
							else if ( generate >= 81 && generate <= 100 )//20% chance for 4 shields.
								shieldnumber = 4;

							// Remove old shields if any exist.
							pc_delshieldball(dstsd, dstsd->shieldball, 1);

							clif_skill_nodamage(bl,bl,RK_MILLENNIUMSHIELD,1,1);
							for (i = 0; i < shieldnumber; i++)
								pc_addshieldball(dstsd, skill_get_time(skill_id,skill_lv), shieldnumber, battle_config.millennium_shield_health);
						}

					}
					else if ( i == LUX_STONEHARDSKIN )
						clif_skill_nodamage(bl, bl, rune_effect[rune_buff], skill_lv, sc_start2(bl, scs[rune_buff], 100, skill_lv, status_get_job_lv_effect(src)*(sd ? pc_checkskill(sd, RK_RUNEMASTERY) : 10), skill_get_time(skill_id, skill_lv)));
					else
						clif_skill_nodamage(bl, bl, rune_effect[rune_buff], skill_lv, sc_start(bl,scs[rune_buff],100,skill_lv,skill_get_time(skill_id,skill_lv)));
				}
			}
			else if( sd && pc_checkskill(sd,RK_RUNEMASTERY) >= 10 )
			{
				if (sc)
				{// Check if any of the listed passable rune buffs are active.
					for (i = 0; i < ARRAYLENGTH(scs); i++)
					{
						if (sc->data[scs[i]])
						{// If a rune buff is found, mark true.
							rune_active = true;
							break;// End the check.
						}
					}

					// Rune buff found? 1 or more of them are likely active.
					if ( rune_active )
						while ( rune_buff == 0 && buff_attempt > 0 )
						{// Randomly select a possible buff and see if its active.
							i = rnd()%6;

							// Selected rune buff active? If yes then prepare it to give to surrounding players.
							if (sc->data[scs[i]])
							{// Convert to a flag to pass on to the script under flag&1.
								if ( i == 0 )
									rune_buff = LUX_REFRESH;
								else if ( i == 1 )
									rune_buff = LUX_GIANTGROWTH;
								else if ( i == 2 )
									rune_buff = LUX_STONEHARDSKIN;
								else if ( i == 3 )
									rune_buff = LUX_VITALITYACTIVATION;
								else if ( i == 4 )
									rune_buff = LUX_ABUNDANCE;
								else if ( i == 5 )
									rune_buff = LUX_MILLENNIUMSHIELD;
							}
							else
								--buff_attempt;
						}
				}

				if ( rune_buff > 0 )
					party_foreachsamemap(skill_area_sub, sd, skill_get_splash(skill_id, skill_lv), src, skill_id, skill_lv, tick, flag|BCT_PARTY|rune_buff|1, skill_castend_nodamage_id);

				clif_skill_nodamage(src,bl,skill_id,1,1);
			}
		}
		break;

	case GC_ROLLINGCUTTER:
		{
			short count = 1;
			skill_area_temp[2] = 0;
			map_foreachinrange(skill_area_sub,src,skill_get_splash(skill_id,skill_lv),BL_CHAR,src,skill_id,skill_lv,tick,flag|BCT_ENEMY|SD_PREAMBLE|SD_SPLASH|1,skill_castend_damage_id);
			if( tsc && tsc->data[SC_ROLLINGCUTTER] )
			{ // Every time the skill is casted the status change is reseted adding a counter.
				count += (short)tsc->data[SC_ROLLINGCUTTER]->val1;
				if( count > 10 )
					count = 10; // Max coounter
				status_change_end(bl,SC_ROLLINGCUTTER,-1);
			}
			sc_start(bl,SC_ROLLINGCUTTER,100,count,skill_get_time(skill_id,skill_lv));
			clif_skill_nodamage(src,src,skill_id,skill_lv,1);
		}
		break;

	case GC_WEAPONBLOCKING:
		if( tsc && tsc->data[SC_WEAPONBLOCKING] )
			status_change_end(bl,SC_WEAPONBLOCKING, INVALID_TIMER);
		else
			sc_start(bl,SC_WEAPONBLOCKING,100,skill_lv,skill_get_time(skill_id,skill_lv));
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;

	case GC_CREATENEWPOISON:
		if( sd )
		{
			clif_skill_produce_mix_list(sd,skill_id,25);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
		break;

	case GC_POISONINGWEAPON:
		if( sd )
		{
			clif_poison_list(sd,skill_lv);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case GC_ANTIDOTE:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		if( tsc ) {
			status_change_end(bl,SC_PARALYSE,INVALID_TIMER);
			status_change_end(bl,SC_PYREXIA,INVALID_TIMER);
			status_change_end(bl,SC_DEATHHURT,INVALID_TIMER);
			status_change_end(bl,SC_LEECHESEND,INVALID_TIMER);
			status_change_end(bl,SC_VENOMBLEED,INVALID_TIMER);
			status_change_end(bl,SC_MAGICMUSHROOM,INVALID_TIMER);
			status_change_end(bl,SC_TOXIN,INVALID_TIMER);
			status_change_end(bl,SC_OBLIVIONCURSE,INVALID_TIMER);
		}
		break;

	case GC_PHANTOMMENACE:
		clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		map_foreachinrange(skill_area_sub,src,skill_get_splash(skill_id,skill_lv),BL_CHAR,
			src,skill_id,skill_lv,tick,flag|BCT_ENEMY|1,skill_castend_damage_id);
		break;

	case GC_HALLUCINATIONWALK:
		{
			int heal = status_get_max_hp(bl) * (18 - 2 * skill_lv) / 100;
			if( status_get_hp(bl) < heal )
			{ // if you haven't enough HP skill fail.
				if( sd )
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_HP_INSUFFICIENT,0,0);
				break;
			}
			if( !status_charge(bl,heal,0) )
			{
				if( sd )
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_HP_INSUFFICIENT,0,0);
				break;
			}
			clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		}
		break;

	case AB_ANCILLA:
		if( sd )
		{
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			skill_produce_mix(sd, skill_id, ITEMID_ANCILLA, 0, 0, 0, 1);
		}
		break;

	case AB_CLEMENTIA:
	case AB_CANTO:
		{
			short bless_lv = (sd ? pc_checkskill(sd, AL_BLESSING) : 10) + status_get_job_lv_effect(src) / 10;
			short agi_lv = (sd ? pc_checkskill(sd, AL_INCAGI) : 10) + status_get_job_lv_effect(src) / 10;
			
			if( sd == NULL || sd->status.party_id == 0 || (flag & 1) )
				clif_skill_nodamage(bl, bl, skill_id, skill_lv, sc_start(bl,type,100,
					(skill_id == AB_CLEMENTIA)? bless_lv : (skill_id == AB_CANTO)? agi_lv : skill_lv,skill_get_time(skill_id,skill_lv)));
			else if( sd )
				party_foreachsamemap(skill_area_sub, sd, skill_get_splash(skill_id, skill_lv), src, skill_id, skill_lv, tick, flag|BCT_PARTY|1, skill_castend_nodamage_id);
		}
	break;
	case AB_PRAEFATIO:
		if( !sd || sd->status.party_id == 0 || flag & 1 )
			clif_skill_nodamage(bl, bl, skill_id, skill_lv, sc_start4(bl, type, 100, skill_lv, 0, 0, skill_id, skill_get_time(skill_id, skill_lv)));
		else if( sd )
			party_foreachsamemap(skill_area_sub, sd, skill_get_splash(skill_id, skill_lv), src, skill_id, skill_lv, tick, flag|BCT_PARTY|1, skill_castend_nodamage_id);
		break;

	case AB_CHEAL:
		if( !sd || sd->status.party_id == 0 || flag & 1 )
		{
			if( sd && tstatus && !battle_check_undead(tstatus->race, tstatus->def_ele) )
			{
				status_change_end(bl, SC_BITESCAR, INVALID_TIMER);
				i = skill_calc_heal(src, bl, AL_HEAL, (sd ? pc_checkskill(sd, AL_HEAL) : 10), true);
				clif_skill_nodamage(bl, bl, skill_id, status_heal(bl, i, 0, 1), 1);
			}
		}
		else if( sd )
			party_foreachsamemap(skill_area_sub, sd, skill_get_splash(skill_id, skill_lv), src, skill_id, skill_lv, tick, flag|BCT_PARTY|1, skill_castend_nodamage_id);
		break;

	case AB_ORATIO:
		if( flag & 1 )
			sc_start(bl, type, 40 + 5 * skill_lv, skill_lv, skill_get_time(skill_id, skill_lv));
		else
		{
			map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id, skill_lv), BL_CHAR,
			src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
		break;

	case AB_VITUPERATUM:
		if ( flag&1 )
		{
			if ( !(tsc && (tsc->data[SC_FREEZE] || (tsc->data[SC_STONE] && tsc->opt1 == OPT1_STONE))) )
			{
				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
				sc_start(bl, type, 100, skill_lv, skill_get_time(skill_id, skill_lv));
			}
		}
		else
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), BL_CHAR,
				src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
		break;

	case AB_LAUDAAGNUS:
	case AB_LAUDARAMUS:
		if( !sd || sd->status.party_id == 0 || (flag & 1) )
		{
			if ( rnd()%100 < 60+10*skill_lv )
			{
				if ( skill_id == AB_LAUDAAGNUS )
				{// Lauda Agnus status removals
					status_change_end(bl, SC_STONE, INVALID_TIMER);
					status_change_end(bl, SC_FREEZE, INVALID_TIMER);
					status_change_end(bl, SC_BLIND, INVALID_TIMER);
					status_change_end(bl, SC_BURNING, INVALID_TIMER);
					status_change_end(bl, SC_FROST, INVALID_TIMER);
					status_change_end(bl, SC_CRYSTALIZE, INVALID_TIMER);
				}
				else
				{// Lauda Ramus status removals
					status_change_end(bl, SC_STUN, INVALID_TIMER);
					status_change_end(bl, SC_SLEEP, INVALID_TIMER);
					status_change_end(bl, SC_SILENCE, INVALID_TIMER);
					status_change_end(bl, SC_DEEPSLEEP, INVALID_TIMER);
					status_change_end(bl, SC_MANDRAGORA, INVALID_TIMER);
				}
			}

			clif_skill_nodamage(bl, bl, skill_id, skill_lv, sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv)));
		} 
		else if( sd )
			party_foreachsamemap(skill_area_sub, sd, skill_get_splash(skill_id, skill_lv), src, skill_id, skill_lv, tick, flag | BCT_PARTY | 1, skill_castend_nodamage_id);
		break;

	case AB_EPICLESIS:
		if( !status_isdead(bl) )
			return 0;
		if( bl->type != BL_PC )
			return 0;
		if( status_revive(bl,100,100) )
			clif_skill_nodamage(src,bl,skill_id,skill_lv,0);
		break;

	case AB_CLEARANCE:
		if (flag&1 || (i = skill_get_splash(skill_id, skill_lv)) < 1)
		{
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			if (rnd() % 100 >= 60 + 8 * skill_lv)
			{
				if (sd)
					clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}
			if(status_isimmune(bl) || !tsc || !tsc->count)
				break;
			for(i=0;i<SC_MAX;i++)
			{
				if( !tsc->data[i] )
					continue;
				switch (i) {
				case SC_WEIGHT50:		case SC_WEIGHT90:		case SC_HALLUCINATION:
				case SC_STRIPWEAPON:	case SC_STRIPSHIELD:	case SC_STRIPARMOR:
				case SC_STRIPHELM:		case SC_CP_WEAPON:		case SC_CP_SHIELD:
				case SC_CP_ARMOR:		case SC_CP_HELM:		case SC_COMBO:
				case SC_STRFOOD:		case SC_AGIFOOD:		case SC_VITFOOD:
				case SC_INTFOOD:		case SC_DEXFOOD:		case SC_LUKFOOD:
				case SC_HITFOOD:		case SC_FLEEFOOD:		case SC_BATKFOOD:
				case SC_WATKFOOD:		case SC_MATKFOOD:		case SC_DANCING:
				case SC_CARTBOOST:		case SC_MELTDOWN:		case SC_SAFETYWALL:
				case SC_SMA:			case SC_SPEEDUP0:		case SC_NOCHAT:
				case SC_ANKLE:			case SC_SPIDERWEB:		case SC_JAILED:
				case SC_ITEMBOOST:		case SC_EXPBOOST:		case SC_LIFEINSURANCE:
				case SC_BOSSMAPINFO:	case SC_PNEUMA:			case SC_AUTOSPELL:
				case SC_INCHITRATE:		case SC_INCATKRATE:		case SC_NEN:
				case SC_READYSTORM:		case SC_READYDOWN:		case SC_READYTURN:
				case SC_READYCOUNTER:	case SC_DODGE:			case SC_WARM:
				case SC_SPEEDUP1:		case SC_AUTOTRADE:		case SC_CRITICALWOUND:
				case SC_JEXPBOOST:		case SC_INVINCIBLE:		case SC_INVINCIBLEOFF:
				case SC_HELLPOWER:		case SC_MANU_ATK:		case SC_MANU_DEF:
				case SC_SPL_ATK:		case SC_SPL_DEF:		case SC_MANU_MATK:
				case SC_SPL_MATK:		case SC_RICHMANKIM:		case SC_ETERNALCHAOS:
				case SC_DRUMBATTLE:		case SC_NIBELUNGEN:		case SC_ROKISWEIL:
				case SC_INTOABYSS:		case SC_SIEGFRIED:		case SC_FOOD_STR_CASH:
				case SC_FOOD_AGI_CASH:	case SC_FOOD_VIT_CASH:	case SC_FOOD_DEX_CASH:
				case SC_FOOD_INT_CASH:	case SC_FOOD_LUK_CASH:	case SC_SEVENWIND:
				case SC_MIRACLE:		case SC_S_LIFEPOTION:	case SC_L_LIFEPOTION:
				case SC_INCHEALRATE:
				// 3CeAM
				// 3rd Job Status's
				case SC_DEATHBOUND:				case SC_EPICLESIS:			case SC_CLOAKINGEXCEED:
				case SC_HALLUCINATIONWALK:		case SC_HALLUCINATIONWALK_POSTDELAY:	case SC_WEAPONBLOCKING:
				case SC_WEAPONBLOCKING_POSTDELAY:	case SC_ROLLINGCUTTER:	case SC_POISONINGWEAPON:
				case SC_FEARBREEZE:				case SC_ELECTRICSHOCKER:	case SC_WUGRIDER:
				case SC_WUGDASH:				case SC_WUGBITE:			case SC_CAMOUFLAGE:
				case SC_ACCELERATION:			case SC_HOVERING:			case SC_OVERHEAT_LIMITPOINT:
				case SC_OVERHEAT:				case SC_SHAPESHIFT:			case SC_INFRAREDSCAN:
				case SC_MAGNETICFIELD:			case SC_NEUTRALBARRIER:		case SC_NEUTRALBARRIER_MASTER:
				case SC_STEALTHFIELD_MASTER:	case SC__REPRODUCE:			case SC_FORCEOFVANGUARD:
				case SC__SHADOWFORM:			case SC_EXEEDBREAK:			case SC__INVISIBILITY:
				case SC_BANDING:				case SC_INSPIRATION:		case SC_RAISINGDRAGON:
				case SC_LIGHTNINGWALK:			case SC_CURSEDCIRCLE_ATKER:	case SC_CURSEDCIRCLE_TARGET:
				case SC_CRESCENTELBOW:			case SC__STRIPACCESSARY:	case SC__MANHOLE:
				case SC_GENTLETOUCH_ENERGYGAIN:	case SC_GENTLETOUCH_CHANGE:	case SC_GENTLETOUCH_REVITALIZE:
				case SC_SWING:		case SC_SYMPHONY_LOVE:	case SC_RUSH_WINDMILL:
				case SC_ECHOSONG:		case SC_MOONLIT_SERENADE:	case SC_SITDOWN_FORCE:
				case SC_ANALYZE:		case SC_LERADS_DEW:		case SC_MELODYOFSINK:
				case SC_BEYOND_OF_WARCRY:	case SC_UNLIMITED_HUMMING_VOICE:	case SC_STEALTHFIELD:
				case SC_WARMER:			case SC_READING_SB:		case SC_GN_CARTBOOST:
				case SC_THORNS_TRAP:		case SC_SPORE_EXPLOSION:	case SC_DEMONIC_FIRE:
				case SC_FIRE_EXPANSION_SMOKE_POWDER:	case SC_FIRE_EXPANSION_TEAR_GAS:		case SC_VACUUM_EXTREME:
				case SC_BANDING_DEFENCE:	case SC_LG_REFLECTDAMAGE:	case SC_MILLENNIUMSHIELD:
				// Genetic Potions
				case SC_SAVAGE_STEAK:	case SC_COCKTAIL_WARG_BLOOD:	case SC_MINOR_BBQ:
				case SC_SIROMA_ICE_TEA:	case SC_DROCERA_HERB_STEAMED:	case SC_PUTTI_TAILS_NOODLES:
				case SC_MELON_BOMB:		case SC_BANANA_BOMB_SITDOWN_POSTDELAY:	case SC_BANANA_BOMB:
				case SC_PROMOTE_HEALTH_RESERCH:	case SC_ENERGY_DRINK_RESERCH:	case SC_EXTRACT_WHITE_POTION_Z:
				case SC_VITATA_500:		case SC_EXTRACT_SALAMINE_JUICE:	case SC_BOOST500:
				case SC_FULL_SWING_K:	case SC_MANA_PLUS:		case SC_MUSTLE_M:
				case SC_LIFE_FORCE_F:
				// Elementals
				case SC_EL_WAIT:		case SC_EL_PASSIVE:		case SC_EL_DEFENSIVE:
				case SC_EL_OFFENSIVE:	case SC_EL_COST:		case SC_CIRCLE_OF_FIRE:
				case SC_CIRCLE_OF_FIRE_OPTION:	case SC_FIRE_CLOAK:	case SC_FIRE_CLOAK_OPTION:
				case SC_WATER_SCREEN:	case SC_WATER_SCREEN_OPTION:	case SC_WATER_DROP:
				case SC_WATER_DROP_OPTION:	case SC_WIND_STEP:	case SC_WIND_STEP_OPTION:
				case SC_WIND_CURTAIN:	case SC_WIND_CURTAIN_OPTION:	case SC_SOLID_SKIN:
				case SC_SOLID_SKIN_OPTION:	case SC_STONE_SHIELD:	case SC_STONE_SHIELD_OPTION:
				case SC_PYROTECHNIC:	case SC_PYROTECHNIC_OPTION:	case SC_HEATER:
				case SC_HEATER_OPTION:	case SC_TROPIC:			case SC_TROPIC_OPTION:
				case SC_AQUAPLAY:		case SC_AQUAPLAY_OPTION:	case SC_COOLER:
				case SC_COOLER_OPTION:	case SC_CHILLY_AIR:		case SC_CHILLY_AIR_OPTION:
				case SC_GUST:			case SC_GUST_OPTION:	case SC_BLAST:
				case SC_BLAST_OPTION:	case SC_WILD_STORM:		case SC_WILD_STORM_OPTION:
				case SC_PETROLOGY:		case SC_PETROLOGY_OPTION:	case SC_CURSED_SOIL:
				case SC_CURSED_SOIL_OPTION:	case SC_UPHEAVAL:	case SC_UPHEAVAL_OPTION:
				case SC_TIDAL_WEAPON:	case SC_TIDAL_WEAPON_OPTION:	case SC_ROCK_CRUSHER:
				case SC_ROCK_CRUSHER_ATK:
				// Mutated Homunculus
				case SC_NEEDLE_OF_PARALYZE:	case SC_PAIN_KILLER:	case SC_LIGHT_OF_REGENE:
				case SC_OVERED_BOOST:	case SC_SILENT_BREEZE:	case SC_STYLE_CHANGE:
				case SC_SONIC_CLAW_POSTDELAY:	case SC_SILVERVEIN_RUSH_POSTDELAY:	case SC_MIDNIGHT_FRENZY_POSTDELAY:
				case SC_GOLDENE_FERSE:	case SC_ANGRIFFS_MODUS:	case SC_TINDER_BREAKER:
				case SC_TINDER_BREAKER_POSTDELAY:	case SC_CBC:	case SC_CBC_POSTDELAY:
				case SC_EQC:			case SC_MAGMA_FLOW:		case SC_GRANITIC_ARMOR:
				case SC_PYROCLASTIC:	case SC_VOLCANIC_ASH:
				// Kagerou/Oboro
				case SC_KG_KAGEHUMI:	case SC_KO_JYUMONJIKIRI:	case SC_MEIKYOUSISUI:
				case SC_KYOUGAKU:		case SC_IZAYOI:			case SC_ZENKAI:
				// Summer
				case SC_SPRITEMABLE:	case SC_SOULATTACK:
				// Rebellion - Need Confirm
				case SC_B_TRAP:			case SC_C_MARKER:		case SC_H_MINE:
				case SC_ANTI_M_BLAST:	case SC_FALLEN_ANGEL:
				// Star Emperor
				case SC_NEWMOON:		case SC_FLASHKICK:		case SC_DIMENSION:
				case SC_NOVAEXPLOSING:
				// Soul Reaper
				case SC_SOULUNITY:		case SC_SOULSHADOW:		case SC_SOULFAIRY:
				case SC_SOULFALCON:		case SC_SOULGOLEM:		case SC_USE_SKILL_SP_SPA:
				case SC_USE_SKILL_SP_SHA:	case SC_SP_SHA:
				// 3rd Job Level Expansion
				case SC_FRIGG_SONG:		case SC_OFFERTORIUM:	case SC_TELEKINESIS_INTENSE:
				case SC_KINGS_GRACE:
				// Misc Status's
				case SC_ALL_RIDING:		case SC_MONSTER_TRANSFORM:	case SC_ON_PUSH_CART:
				case SC_FULL_THROTTLE:	case SC_REBOUND:			case SC_ANCILLA:
				// Guild Skills
				case SC_LEADERSHIP:		case SC_GLORYWOUNDS:	case SC_SOULCOLD:
				case SC_HAWKEYES:
				// Only removeable by Dispell
				case SC_SUMMON1:		case SC_SUMMON2:		case SC_SUMMON3:		
				case SC_SUMMON4:		case SC_SUMMON5:		/*case SC_SPELLBOOK1:		
				case SC_SPELLBOOK2:		case SC_SPELLBOOK3:		case SC_SPELLBOOK4:		
				case SC_SPELLBOOK5:		case SC_SPELLBOOK6:		case SC_SPELLBOOK7:*/
				case SC_DAILYSENDMAILCNT:
				case SC_ATTHASTE_CASH:	case SC_REUSE_REFRESH:
				case SC_REUSE_LIMIT_A:	case SC_REUSE_LIMIT_B:	case SC_REUSE_LIMIT_C:
				case SC_REUSE_LIMIT_D:	case SC_REUSE_LIMIT_E:	case SC_REUSE_LIMIT_F:
				case SC_REUSE_LIMIT_G:	case SC_REUSE_LIMIT_H:	case SC_REUSE_LIMIT_MTF:
				case SC_REUSE_LIMIT_ASPD_POTION:	case SC_REUSE_MILLENNIUMSHIELD:	case SC_REUSE_CRUSHSTRIKE:
				case SC_REUSE_STORMBLAST:	case SC_ALL_RIDING_REUSE_LIMIT:
				// Monster Transformation
				case SC_MTF_ASPD:		case SC_MTF_RANGEATK:		case SC_MTF_MATK:
				case SC_MTF_MLEATKED:		case SC_MTF_CRIDAMAGE:
				case SC_MTF_ASPD2:		case SC_MTF_RANGEATK2:	case SC_MTF_MATK2:
				case SC_2011RWC_SCROLL:		case SC_JP_EVENT04:	case SC_MTF_MHP:
				case SC_MTF_MSP:		case SC_MTF_PUMPKIN:	case SC_MTF_HITFLEE:
				case SC_DORAM_BUF_01:	case SC_DORAM_BUF_02:	case SC_PARTYFLEE:
					continue;
				case SC_WHISTLE:	case SC_ASSNCROS:		case SC_POEMBRAGI:
				case SC_APPLEIDUN:	case SC_HUMMING:		case SC_DONTFORGETME:
				case SC_FORTUNE:	case SC_SERVICE4U:
					if (tsc->data[i]->val4 == 0)
						continue; //if in song-area don't end it
					break;
				case SC_ASSUMPTIO:
					if( bl->type == BL_MOB )
							continue;
						break;
				}
				if(i==SC_BERSERK) tsc->data[i]->val2=0; //Mark a dispelled berserk to avoid setting hp to 100 by setting hp penalty to 0.
				status_change_end(bl,(sc_type)i,INVALID_TIMER);
			}
			break;
		}
		//Affect all targets on splash area.
		map_foreachinrange(skill_area_sub, bl, i, BL_CHAR, src, skill_id, skill_lv, tick, flag|1, skill_castend_damage_id);
		break;

	case AB_SILENTIUM:
		// Should the level of Lex Divina be equivalent to the level of Silentium or should the highest level learned be used? [LimitLine]
		// FIX ME!!!! Learned level of Lex Divina doesn't affect the duration. [Rytech]
		map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id, skill_lv), BL_CHAR,
			src, PR_LEXDIVINA, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
		clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		break;

	case WL_WHITEIMPRISON:
		{
			int duration = 0;
			short status_flag = 0;

			// Target check. Can only be casted on monsters, enemy players, or yourself. Also fails if target is already imprisoned.
			if (!(bl->type == BL_MOB || bl->type == BL_PC && battle_check_target(src, bl, BCT_ENEMY) > 0 || src == bl) || status_get_class_(bl) == CLASS_BOSS || (tsc && tsc->data[SC_IMPRISON])) 
			{
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_TOTARGET,0,0);
				break;
			}

			// Success chance.
			if ( bl->type == BL_MOB || bl->type == BL_PC && battle_check_target(src,bl,BCT_ENEMY) > 0 )
			{	// Success chance for monsters and enemy players.
				rate = 50 + 3 * skill_lv + status_get_job_lv_effect(src) / 4;
				// Duration is not reduced the same way on monsters like it is on players.
				if ( bl->type == BL_MOB )
					status_flag = 2;
			}
			else
			{
				rate = 100;// Success for caster targeting self.
				status_flag = 10;// Natural immunity and duration reduction does not apply when self casted.
			}

			// Status duration.
			if ( bl->type == BL_MOB )
			{	// Monsters have a resistance.
				duration = 20000 - 1000 * (tstatus->vit + tstatus->luk) / 20;
				if ( duration < 10000 )// Duration cant be reduced below 10 seconds.
					duration = 10000;
			}
			else if ( bl->type == BL_PC && battle_check_target(src,bl,BCT_ENEMY) > 0)
				duration = skill_get_time(skill_id,skill_lv);// Enemy players get a fixed duration set by skill level.
			else
				duration = 5000;// Duration for caster targeting self.

			// Everything checks out and is set. Attempt to give the status.
			clif_skill_nodamage(src,bl,skill_id,skill_lv,status_change_start(src, bl, type, 100*rate, skill_lv, 0, 0, 0, duration, status_flag));
			break;
		}

	case WL_JACKFROST:
		{
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			skill_area_temp[1] = 0;
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			map_foreachinshootrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), BL_CHAR|BL_SKILL, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_damage_id);
		}
		break;

	case WL_MARSHOFABYSS:
		{
		int timereduct = skill_get_time(skill_id, skill_lv) - (tstatus->int_ + tstatus->dex) / 20 * 1000;
		if ( timereduct < 5000 )
			timereduct = 5000;//Duration cant go below 5 seconds.
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		sc_start(bl, type, 100, skill_lv, timereduct);
		}
		break;

	case WL_SIENNAEXECRATE:
		if( status_isimmune(bl) || !tsc )
			break;

		if( flag&1 ){
			if( bl->id == skill_area_temp[1] )
				break; // Already work on this target

			if( tsc && tsc->data[SC_STONE] )
				status_change_end(bl,SC_STONE,-1);
			else
				status_change_start(src,bl,SC_STONE,10000,skill_lv,0,0,1000,(8+2*skill_lv)*1000,2);
		}
		else
		{
			int rate = 40 + 8 * skill_lv + status_get_job_lv_effect(src) / 4;
			// IroWiki says Rate should be reduced by target stats, but currently unknown
			if (rnd()%100 < rate)
			{ // Success on First Target
				rate = 0;
				if (!tsc->data[SC_STONE])
					rate = status_change_start(src,bl,SC_STONE,10000,skill_lv,0,0,1000,(8+2*skill_lv)*1000,2);
				else
				{
					rate = 1;
					status_change_end(bl, SC_STONE, INVALID_TIMER);
				}

				if (rate)
				{
					skill_area_temp[1] = bl->id;
					map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
				}
				// Doesn't send failure packet if it fails on defense.
			}
			else if (sd) // Failure on Rate
				clif_skill_fail(sd, skill_id, 0, 0, 0);
		}
		break;

	case WL_STASIS:
		if( flag&1 )
		{
		int duration = skill_get_time(skill_id, skill_lv) - 1000 * (tstatus->vit + tstatus->dex) / 20;
		if ( duration < 10000 )
			duration = 10000;// Duration cant go below 10 seconds.
			sc_start(bl,type,100,skill_lv,duration);
		}
		else
		{
			map_foreachinrange(skill_area_sub,src,skill_get_splash(skill_id, skill_lv),BL_CHAR,src,skill_id,skill_lv,tick,(map_flag_vs(src->m)?BCT_ALL:BCT_ENEMY|BCT_SELF)|flag|1,skill_castend_nodamage_id);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
		break;

		case WL_SUMMONFB:
		case WL_SUMMONBL:
		case WL_SUMMONWB:
		case WL_SUMMONSTONE:
		{
			short element = 0, sctype = 0, pos = -1;
			struct status_change *sc = status_get_sc(src);
			if( !sc ) 
				break;
	
			for( i = SC_SUMMON1; i <= SC_SUMMON5; i++ )
			{
				if( !sctype && !sc->data[i] )
					sctype = i; // Take the free SC
				if( sc->data[i] )
					pos = max(sc->data[i]->val2,pos);
			}

			if( !sctype )
			{
				if( sd ) // No free slots to put SC
					clif_skill_fail(sd,skill_id,0x13,0,0);
				break;
			}

			pos++; // Used in val2 for SC. Indicates the order of this ball
			switch( skill_id )
			{ // Set val1. The SC element for this ball
			case WL_SUMMONFB:    element = WLS_FIRE;  break;
			case WL_SUMMONBL:    element = WLS_WIND;  break;
			case WL_SUMMONWB:    element = WLS_WATER; break;
			case WL_SUMMONSTONE: element = WLS_STONE; break;
			}

			sc_start4(src,(sc_type)sctype,100,element,pos,skill_lv,0,skill_get_time(skill_id,skill_lv));
			clif_skill_nodamage(src,bl,skill_id,0,0);
		}
		break;

	case WL_READING_SB:
		if( sd )
		{
			int i, preserved = 0;
			short max_preserve = 4 * pc_checkskill(sd, WL_FREEZE_SP) + sstatus->int_ / 10 + status_get_base_lv_effect(src) / 10;
			
			ARR_FIND(0, MAX_SPELLBOOK, i, sd->rsb[i].skill_id == 0); // Search for a Free Slot
			if( i == MAX_SPELLBOOK )
			{
				clif_skill_fail(sd,skill_id,0x04,0,0);
				break;
			}
			for( i = 0; i < MAX_SPELLBOOK && sd->rsb[i].skill_id; i++ )
				preserved += sd->rsb[i].points;

			if( preserved >= max_preserve )
			{
				clif_skill_fail(sd,skill_id,0x04,0,0);
				break;
			}

			sc_start(bl,SC_STOP,100,skill_lv,-1); //Can't move while selecting a spellbook.
			clif_spellbook_list(sd);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
		break;

	case RA_WUGMASTERY:
		if( sd ){
			if( !pc_iswug(sd) )
				pc_setoption(sd,sd->sc.option|OPTION_WUG);
			else
				pc_setoption(sd,sd->sc.option&~OPTION_WUG);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case RA_WUGRIDER:
		if( sd ){
			if( pc_iswugrider(sd) ){
				pc_setoption(sd, sd->sc.option&~OPTION_WUGRIDER);
				pc_setoption(sd, sd->sc.option | OPTION_WUG);
			}
			else if( pc_iswug(sd) ){
				pc_setoption(sd, sd->sc.option&~OPTION_WUG);
				pc_setoption(sd, sd->sc.option | OPTION_WUGRIDER);
			}
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case RA_WUGDASH:
		if( tsce )
		{
			clif_skill_nodamage(src,bl,skill_id,skill_lv,status_change_end(bl, type, INVALID_TIMER));
			map_freeblock_unlock();
			return 0;
		}
		if( sd && pc_isriding(sd) ){
			clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start4(bl,type,100,skill_lv,unit_getdir(bl),0,0,1));
			clif_walkok(sd);
		}
		break;

	case RA_SENSITIVEKEEN:
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		clif_skill_damage(src,src,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
		map_foreachinrange(skill_area_sub,src,skill_get_splash(skill_id,skill_lv),BL_CHAR|BL_SKILL,src,skill_id,skill_lv,tick,flag|BCT_ENEMY,skill_castend_damage_id);
		break;

	case NC_F_SIDESLIDE:
	case NC_B_SIDESLIDE:
		{
			int dir = (skill_id == NC_F_SIDESLIDE) ? (unit_getdir(src)+4)%8 : unit_getdir(src);
			skill_blown(src,bl,skill_get_blewcount(skill_id,skill_lv),dir,0);
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case NC_SELFDESTRUCTION:
		if( sd )
		{
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			skill_castend_damage_id(src, src, skill_id, skill_lv, tick, flag);
			pc_setoption(sd, sd->sc.option&~OPTION_MADOGEAR);
		}
		break;

	case NC_ANALYZE:
		clif_skill_damage(src,src,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
		sc_start(bl, type, 30 + 12 * skill_lv, skill_lv, skill_get_time(skill_id, skill_lv));
		if( sd ) pc_overheat(sd,1);
		break;

	case NC_MAGNETICFIELD:
		if( (i = sc_start2(bl,type,100,skill_lv,src->id,skill_get_time(skill_id,skill_lv))) ){
			map_foreachinrange(skill_area_sub,src,skill_get_splash(skill_id,skill_lv),splash_target(src),src,skill_id,skill_lv,tick,flag|BCT_ENEMY|SD_SPLASH|1,skill_castend_damage_id);
			clif_skill_damage(src,src,tick,status_get_amotion(src),0,-30000,1,skill_id,skill_lv,6);
			if (sd) pc_overheat(sd,1);
		}
		break;

	case NC_REPAIR:
		if( sd ){
			short percent;
			int heal;

			switch (skill_lv)
			{
				case 1: percent = 4;
				case 2: percent = 7;
				case 3: percent = 13;
				case 4: percent = 17;
				case 5: percent = 23;
			}

			if( dstsd && pc_ismadogear(dstsd) )
			{
				heal = dstsd->status.max_hp * percent / 100;
				status_heal(bl,heal,0,2);
			}else{
				heal = sd->status.max_hp * percent / 100;
				status_heal(src,heal,0,2);
			}
			clif_skill_nodamage(src, bl, skill_id, skill_lv, heal);
		}
	break;

	case NC_DISJOINT:
		{
			if( bl->type != BL_MOB ) break;				
			md = map_id2md(bl->id);
			if (md && md->mob_id >= MOBID_SILVERSNIPER && md->mob_id <= MOBID_MAGICDECOY_WIND)
				status_kill(bl);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
		break;
	
	case SC_AUTOSHADOWSPELL:
		if( sd ){
			if ((sd->reproduceskill_idx >= 0 && sd->status.skill[sd->reproduceskill_idx].id) ||
				(sd->cloneskill_idx >= 0 && sd->status.skill[sd->cloneskill_idx].id))
			{
				sc_start(src,SC_STOP,100,skill_lv,-1);
				clif_skill_select_request(sd);
				clif_skill_nodamage(src,bl,skill_id,1,1);
			}else
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
		}
		break;

	case SC_SHADOWFORM:
		if( sd && dstsd && src != bl && !dstsd->shadowform_id ){
			if( clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start4(src,type,100,skill_lv,bl->id,4+skill_lv,0,skill_get_time(skill_id, skill_lv))) )
				dstsd->shadowform_id = src->id;
		}else if( sd )
			clif_skill_fail(sd, skill_id, 0, 0, 0);
		break;

	case SC_BODYPAINT:
		if (flag&1)
		{
			if (tsc && (tsc->data[SC_HIDING] || tsc->data[SC_CLOAKING] || tsc->data[SC_CLOAKINGEXCEED] || tsc->data[SC_NEWMOON]))
			{
				status_change_end(bl, SC_HIDING, INVALID_TIMER);
				status_change_end(bl, SC_CLOAKING, INVALID_TIMER);
				status_change_end(bl, SC_CLOAKINGEXCEED, INVALID_TIMER);
				status_change_end(bl, SC_NEWMOON, INVALID_TIMER);
				sc_start(bl,type,20 + 5 * skill_lv,skill_lv,skill_get_time(skill_id,skill_lv));
			}
				sc_start(bl,SC_BLIND,53 + 2 * skill_lv,skill_lv,skill_get_time2(skill_id,skill_lv));
		}
		else
		{
			clif_skill_nodamage(src, bl, skill_id, 0, 1);
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
		}
		break;

	case SC_ENERVATION:
	case SC_GROOMY:
	case SC_IGNORANCE:
	case SC_LAZINESS:
	case SC_UNLUCKY:
	case SC_WEAKNESS:
		if( !(tsc && tsc->data[type]) )
		{
			if (status_get_class_(bl) == CLASS_BOSS)
				break;

			//First we set the success chance based on the caster's build which increases the chance.
			rate = 10 * skill_lv + rnd_value( sstatus->dex / 12, sstatus->dex / 4 ) + status_get_job_lv_effect(src) + status_get_base_lv_effect(src) / 10 - 
			//We then reduce the success chance based on the target's build.
			rnd_value( tstatus->agi / 6, tstatus->agi / 3 ) - tstatus->luk / 10 - ( dstsd ? (dstsd->max_weight / 10 - dstsd->weight / 10 ) / 100 : 0 ) - status_get_base_lv_effect(bl) / 10;
			//Finally we set the minimum success chance cap based on the caster's skill level and DEX.
			rate = cap_value( rate, skill_lv + sstatus->dex / 20, 100);
				clif_skill_nodamage(src,bl,skill_id,0,sc_start(bl,type,rate,skill_lv,skill_get_time(skill_id,skill_lv)));

			if ( tsc && tsc->data[SC__IGNORANCE] && skill_id == SC_IGNORANCE)//If the target was successfully inflected with the Ignorance status, drain some of the targets SP.

			{
				int sp = 100 * skill_lv;
				if (dstmd) sp = dstmd->level * 2;
				if( status_zap(bl,0,sp) )
					status_heal(src,0,sp/2,3);//What does flag 3 do? [Rytech]
			}

			if ( tsc && tsc->data[SC__UNLUCKY] && skill_id == SC_UNLUCKY)//If the target was successfully inflected with the Unlucky status, give 1 of 3 random status's.
				switch(rnd()%3)
				{//Targets in the Unlucky status will be affected by one of the 3 random status's reguardless of resistance.
					case 0:
						status_change_start(src,bl,SC_POISON,10000,skill_lv,0,0,0,skill_get_time(skill_id,skill_lv),10);
						break;
					case 1:
						status_change_start(src,bl,SC_SILENCE,10000,skill_lv,0,0,0,skill_get_time(skill_id,skill_lv),10);
						break;
					case 2:
						status_change_start(src,bl,SC_BLIND,10000,skill_lv,0,0,0,skill_get_time(skill_id,skill_lv),10);
				}
		}
		else if( sd )
			clif_skill_fail( sd, skill_id, 0, 0, 0 );
		break;

	case LG_TRAMPLE:
		clif_skill_damage( src, bl, tick, status_get_amotion( src ), 0, -30000, 1, skill_id, skill_lv, 6 );
		if ( rnd()%100 < 25 + 25 * skill_lv )
		{
			map_foreachinrange(skill_destroy_trap,bl,skill_get_splash(skill_id,skill_lv),BL_SKILL,tick);
			// Chance of dogging damage from trap that deals damage. Add behavior in later. [Rytech]
			//if ( rnd()%100 < 5 * skill_lv + (sstatus->agi + sstatus->dex) / 4 )
			status_change_end(src, SC_THORNS_TRAP, INVALID_TIMER);
			status_change_end(src, SC_SV_ROOTTWIST, INVALID_TIMER);
		}
		break;

	case LG_REFLECTDAMAGE:
		if( tsc && tsc->data[type] )
			status_change_end( bl, type, INVALID_TIMER );
		else
			sc_start( bl, type, 100, skill_lv, skill_get_time( skill_id, skill_lv ) );
		clif_skill_nodamage( src, bl, skill_id, skill_lv, 1 );
 		break;

	case LG_SHIELDSPELL:
		if (sd)
		{
			signed char refinebonus = 0;
			short effect_number = rnd()%3 + 1;// Effect Number. Each level has 3 unique effects thats randomly picked from.
			short shield_bonus = 0;// Shield Stats. DEF/MDEF/Refine is taken from shield and ran through a formula.
			short splash_range = 0;// Splash AoE. Used for splash AoE ATK/MATK and Lex Divina.
			struct item_data *shield_data = sd->inventory_data[sd->equip_index[EQI_HAND_L]];// Checks DEF of shield.
			struct item *shield = &sd->inventory.u.items_inventory[sd->equip_index[EQI_HAND_L]];// Checks refine of shield.

			// Skill will first check if a shield is equipped. If none is found on the caster the skill will fail.
			if( !shield_data || shield_data->type != IT_ARMOR )
			{
				clif_skill_fail(sd, skill_id, 0, 0, 0);
				break;
			}

			// Set how the bonus from the shield's refine will be handled.
			if ( MAX_REFINE > 10 )// +20 Refine Limit
				refinebonus = shield->refine;
			else// +10 Refine Limit
				refinebonus = 2 * shield->refine;

			switch( skill_lv )
			{
				case 1:
					{
						if ( effect_number == 1 )
						{// Don't bother setting the splash AoE size unless effect 1 is going to trigger.
							if ( shield_data->def >= 0 && shield_data->def <= 4 )
								splash_range = 1;
							else if ( shield_data->def >= 5 && shield_data->def <= 8 )
								splash_range = 2;
							else
								splash_range = 3;
						}

						switch( effect_number )
						{
							case 1://Splash AoE ATK
								sc_start(bl, SC_SHIELDSPELL_DEF, 100, effect_number, -1);
								clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
								map_foreachinrange(skill_area_sub, src, splash_range, BL_CHAR, src, skill_id, skill_lv, tick, flag | BCT_ENEMY | 1, skill_castend_damage_id);
								status_change_end(bl,SC_SHIELDSPELL_DEF,-1);
								break;
							case 2://Damage Reflecting Increase.
								shield_bonus = shield_data->def;
								sc_start2(bl,SC_SHIELDSPELL_DEF,100,effect_number,shield_bonus,shield_data->def * 10 * 1000);
 								break;
							case 3://Weapon Attack Increase.
								shield_bonus = 10 * shield_data->def;
								sc_start2(bl,SC_SHIELDSPELL_DEF,100,effect_number,shield_bonus,shield_data->def * 10 * 3000);
 								break;
						}
					}
					break;

				case 2:
					{
						if ( effect_number != 3 )
						{// Don't bother setting the splash AoE size unless effect 1 or 2 is going to trigger.
							if ( sd->bonus.shieldmdef >= 0 && sd->bonus.shieldmdef <= 3 )//Info says between 1 - 3, but ill make it go as low as 0 for now. [Rytech]
								splash_range = 1;
							else if ( sd->bonus.shieldmdef >= 4 && sd->bonus.shieldmdef <= 5 )
								splash_range = 2;
							else
								splash_range = 3;
						}
						switch( effect_number )
						{
							case 1://Splash AoE MATK
								sc_start(bl,SC_SHIELDSPELL_MDEF,100,effect_number,-1);
								clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
								map_foreachinrange(skill_area_sub,src,splash_range,BL_CHAR,src,skill_id,skill_lv,tick,flag|BCT_ENEMY|2,skill_castend_damage_id);
								status_change_end(bl,SC_SHIELDSPELL_MDEF,-1);
								break;
							case 2://Splash AoE Lex Divina
								shield_bonus = sd->bonus.shieldmdef;// Shield's MDEF = Level of Lex Divina to cast.
								if ( shield_bonus > 10 )
									shield_bonus = 10;// Don't cast any level above 10.
								sc_start(bl,SC_SHIELDSPELL_MDEF,100,effect_number,-1);
								map_foreachinrange(skill_area_sub,src,splash_range,BL_CHAR,src,PR_LEXDIVINA,shield_bonus,tick,flag|BCT_ENEMY|1,skill_castend_nodamage_id);
								status_change_end(bl,SC_SHIELDSPELL_MDEF,-1);
								break;
							case 3://Magnificat
								if (sc_start(bl, SC_SHIELDSPELL_MDEF, 100, effect_number, sd->bonus.shieldmdef * 30000))
									clif_skill_nodamage(src, bl, PR_MAGNIFICAT, skill_lv, sc_start(bl, SC_MAGNIFICAT, 100, 1, sd->bonus.shieldmdef * 30000));
								break;
						}
					}
					break;

				case 3:
					{
						switch (effect_number)
						{
							case 1://Status Resistance Increase. This is for common status's.
								shield_bonus = 2 * refinebonus + sstatus->luk / 10;
								sc_start2(bl, SC_SHIELDSPELL_REF, 100, effect_number, shield_bonus, refinebonus * 30000);
								break;
							case 2://DEF Increase / Using Converted DEF Increase Formula For Pre-renewal Mechanics.
								shield_bonus = refinebonus / 2;// Half the increase amount.
								sc_start2(bl, SC_SHIELDSPELL_REF, 100, effect_number, shield_bonus, refinebonus * 20000);
								break;
							case 3://HP Recovery
								sc_start(bl, SC_SHIELDSPELL_REF, 100, effect_number, -1);
								shield_bonus = sstatus->max_hp * (status_get_base_lv_effect(src) / 10 + refinebonus) / 100;
								status_heal(bl, shield_bonus, 0, 2);
								status_change_end(bl,SC_SHIELDSPELL_REF,INVALID_TIMER);
							break;
						}
					}
					break;
			}
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case LG_PIETY:
		if (flag&1)
			sc_start(bl, type, 100, skill_lv, skill_get_time(skill_id, skill_lv));
		else
		{
			skill_area_temp[2] = 0;
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), BL_PC, src, skill_id, skill_lv, tick, flag|SD_PREAMBLE|BCT_PARTY|BCT_SELF|1, skill_castend_nodamage_id);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
 		break;

	case LG_INSPIRATION:
		if( sd ){
			if( !map[src->m].flag.noexppenalty ){
				sd->status.base_exp -= min(sd->status.base_exp, pc_nextbaseexp(sd) * 1 / 100); //1% penalty.
				sd->status.job_exp -= min(sd->status.job_exp, pc_nextjobexp(sd) * 1 / 100);
				clif_updatestatus(sd,SP_BASEEXP);
				clif_updatestatus(sd,SP_JOBEXP);
			}
			clif_skill_nodamage(bl,src,skill_id,skill_lv,
				sc_start(bl, type, 100, skill_lv, skill_get_time(skill_id, skill_lv)));
		}
		break;

	case SR_RAISINGDRAGON:
		if (sd)
		{
			short max = 5 + skill_lv;
			if ( pc_checkskill(sd,MO_EXPLOSIONSPIRITS) > 0 )//Only starts the status at the highest learned level if you learned it.
			sc_start(bl, SC_EXPLOSIONSPIRITS, 100, pc_checkskill(sd,MO_EXPLOSIONSPIRITS), skill_get_time(skill_id, skill_lv));
			for (i = 0; i < max; i++) // Don't call more than max available spheres.
				pc_addspiritball(sd, skill_get_time(skill_id, skill_lv), max);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, sc_start(bl, type, 100, skill_lv,skill_get_time(skill_id, skill_lv)));
		}
		break;

	case SR_ASSIMILATEPOWER:
		if (flag&1)
		{
			i = 0;
			if (dstsd && dstsd->spiritball && (sd == dstsd || map_flag_vs(src->m)) && (dstsd->class_&MAPID_BASEMASK) != MAPID_GUNSLINGER)
			{
				i = dstsd->spiritball; //1%sp per spiritball.
				pc_delspiritball(dstsd, dstsd->spiritball, 0);
			}
			if (i)
				status_percent_heal(src, 0, i);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, i ? 1:0);
		}
		else
		{
			clif_skill_damage(src, bl, tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), splash_target(src), src, skill_id, skill_lv, tick, flag|BCT_ENEMY|BCT_SELF|SD_SPLASH|1, skill_castend_nodamage_id);
		}
		break;

	case SR_POWERVELOCITY:
		if( !dstsd )
			break;
		if( sd && dstsd->spiritball <= 5 ){
			for(i = 0; i <= 5; i++){
				pc_addspiritball(dstsd, skill_get_time(MO_CALLSPIRITS, pc_checkskill(sd,MO_CALLSPIRITS)), i);
				pc_delspiritball(sd, sd->spiritball, 0);
			}
		}
		clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		break;

	case SR_GENTLETOUCH_CURE:
		status_heal(bl, tstatus->max_hp * skill_lv / 100 + 120 * skill_lv, 0, 0);//It heals the target, but shows no heal animation or numbers.
		rate = (5 * skill_lv + (sstatus->dex + status_get_base_lv_effect(src)) / 4) - rnd_value(1, 10);
		if(rnd()%100 < rate)
		{
			status_change_end(bl, SC_STONE, INVALID_TIMER);
			status_change_end(bl, SC_FREEZE, INVALID_TIMER);
			status_change_end(bl, SC_STUN, INVALID_TIMER);
			status_change_end(bl, SC_POISON, INVALID_TIMER);
			status_change_end(bl, SC_SILENCE, INVALID_TIMER);
			status_change_end(bl, SC_BLIND, INVALID_TIMER);
			status_change_end(bl, SC_HALLUCINATION, INVALID_TIMER);
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;

	case WA_SWING_DANCE:
	case WA_SYMPHONY_OF_LOVER:
	case WA_MOONLIT_SERENADE:
	case MI_RUSH_WINDMILL:
	case MI_ECHOSONG:
		if( flag&1 )
			sc_start4(bl, type, 100, skill_lv, (sd ? pc_checkskill(sd, WM_LESSON) : 10), status_get_job_lv_effect(src), 0, skill_get_time(skill_id, skill_lv));
		else if( sd )
		{
			party_foreachsamemap(skill_area_sub,sd,skill_get_splash(skill_id,skill_lv),src,skill_id,skill_lv,tick,flag|BCT_PARTY|1,skill_castend_nodamage_id);
			sc_start4(bl, type, 100, skill_lv, (sd ? pc_checkskill(sd, WM_LESSON) : 10), status_get_job_lv_effect(src), 0, skill_get_time(skill_id, skill_lv));
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case WM_FRIGG_SONG:
		if( flag&1 )
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
		else if( sd )
		{
			map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id,skill_lv),BL_PC, src, skill_id, skill_lv, tick, flag|BCT_NOENEMY|1, skill_castend_nodamage_id);
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case MI_HARMONIZE:
		clif_skill_nodamage(src, bl, skill_id, skill_lv, sc_start2(bl, type, 100, skill_lv, (sd ? pc_checkskill(sd, WM_LESSON) : 10), skill_get_time(skill_id, skill_lv)));
		break;

	case WM_DEADHILLHERE:
		if( bl->type == BL_PC )
		{
			if( !status_isdead(bl) )
				break;
			if( rnd()%100 < 88 + 2 * skill_lv )
			{
				int heal = 0;
				status_zap(bl, 0, tstatus->sp * (60 - 10 * skill_lv) / 100);
				heal = tstatus->sp;
				if ( heal <= 0 )
					heal = 1;
				status_fixed_revive(bl, heal, 0);
				clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
				status_set_sp(bl, 0, 0);
			}
		}
		break;

	case WM_LULLABY_DEEPSLEEP:
		if ( flag&1 )
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
		else if ( sd )
		{
			rate = 4 * skill_lv + 2 * (sd ? pc_checkskill(sd, WM_LESSON) : 10) + status_get_base_lv_effect(src) / 15 + status_get_job_lv_effect(src) / 5;

			if ( rnd()%100 < rate )
			{
				map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id, skill_lv), BL_CHAR, src, skill_id, skill_lv, tick, flag | BCT_ENEMY | 1, skill_castend_nodamage_id);
				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			}
		}
		break;

	case WM_SIRCLEOFNATURE:// I need to confirm if this skill affects friendly's or enemy's. [Rytech]
		if( flag&1 )
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
		else if ( sd )
		{
			map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id,skill_lv),BL_PC, src, skill_id, skill_lv, tick, flag|BCT_NOENEMY|1, skill_castend_nodamage_id);
			sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		}
		break;

	case WM_VOICEOFSIREN:
		if( flag&1 )
		{
			int duration = 0;
			// Job LV part is ignored on monsters.
			duration = skill_get_time(skill_id,skill_lv) - 1000 * (status_get_base_lv_effect(bl) / 10 + (sd?status_get_job_lv_effect(bl):0) / 5);

			if ( duration < 10000 )// Duration can't be reduced below 10 seconds.
				duration = 10000;
			sc_start2(bl,type,100,skill_lv,src->id,duration);
		}
		else if ( sd )
		{
			rate = 6 * skill_lv + 2 * (sd ? pc_checkskill(sd, WM_LESSON) : 10) + status_get_job_lv_effect(src) / 2;

			if (rnd() % 100 < rate)
			{
				map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id, skill_lv), BL_CHAR, src, skill_id, skill_lv, tick, flag | BCT_ENEMY | 1, skill_castend_nodamage_id);
				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			}
		}
		break;

	case WM_GLOOMYDAY:
		clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		sc_start4(bl, type, 100, skill_lv, 0, 0, (sd ? pc_checkskill(sd, WM_LESSON) : 10), skill_get_time(skill_id, skill_lv));
		break;

	case WM_SONG_OF_MANA:
	case WM_DANCE_WITH_WUG:
	case WM_LERADS_DEW:
	case WM_UNLIMITED_HUMMING_VOICE:
			if( flag&1 )
				sc_start2(bl, type, 100, skill_lv, skill_chorus_count(sd), skill_get_time(skill_id, skill_lv));
			else if( sd )
			{
				party_foreachsamemap(skill_area_sub,sd,skill_get_splash(skill_id,skill_lv),src,skill_id,skill_lv,tick,flag|BCT_PARTY|1,skill_castend_nodamage_id);
				sc_start2(bl, type, 100, skill_lv, skill_chorus_count(sd), skill_get_time(skill_id, skill_lv));
				clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			}
			break;

	case WM_SATURDAY_NIGHT_FEVER:
		{
			int madnesscheck = 0;
			if ( sd )//Required to check if the lord of madness effect will be applied.
				madnesscheck = map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id,skill_lv),BL_PC, src, skill_id, skill_lv, tick, flag|BCT_ENEMY, skill_area_sub_count);
			if( flag&1 )
			{
				sc_start(bl, type, 100, skill_lv,skill_get_time(skill_id, skill_lv));
				if ( madnesscheck >= 8 )//The god of madness deals 9999 fixed unreduceable damage when 8 or more enemy players are affected.
					status_fix_damage(src, bl, 9999, clif_damage(src, bl, tick, 0, 0, 9999, 0, 0, 0, false));
					//skill_attack(BF_MISC,src,src,bl,skill_id,skill_lv,tick,flag);//To renable when I can confirm it deals damage like this. Data shows its dealed as reflected damage which I dont have it coded like that yet. [Rytech]
			}
			else if( sd )
			{
				rate = sstatus->int_ / 6 + status_get_job_lv_effect(src) / 5 + skill_lv * 4;

				if ( rnd()%100 < rate )
				{
					map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id,skill_lv),BL_PC, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
					clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
				}
			}
			break;
		}

	case WM_MELODYOFSINK:
	case WM_BEYOND_OF_WARCRY:
		if( flag&1 )
			sc_start2(bl, type, 100, skill_lv, skill_chorus_count(sd), skill_get_time(skill_id, skill_lv));
		else if( sd )
		{
			if (rnd() % 100 < 15 + 5 * skill_lv + 5 * skill_chorus_count(sd))
			{
				map_foreachinrange(skill_area_sub, src, skill_get_splash(skill_id,skill_lv),BL_PC, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
				clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			}
		}
		break;

	case WM_RANDOMIZESPELL:
		{
			int improv_skillid = 0, improv_skilllv;
			do
			{
				i = rnd() % MAX_SKILL_IMPROVISE_DB;
				improv_skillid = skill_improvise_db[i].skill_id;
			}
			while (improv_skillid == 0 || rnd()%10000 >= skill_improvise_db[i].per);
			improv_skilllv = 4 + skill_lv;
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);

			if (sd)
			{
				sd->state.improv_flag = 1;
				sd->skillitem = improv_skillid;
				sd->skillitemlv = improv_skilllv;
				sd->skillitem_keep_requirement = false;
				clif_item_skill(sd, improv_skillid, improv_skilllv);
			}
			else
			{
				struct unit_data *ud = unit_bl2ud(src);
				int inf = skill_get_inf(improv_skillid);
				int target_id = 0;
				if (!ud)
					break;
				if (inf&INF_SELF_SKILL || inf&INF_SUPPORT_SKILL) {
					if (src->type == BL_PET)
						bl = (struct block_list*)((TBL_PET*)src)->master;
					if (!bl) bl = src;
					unit_skilluse_id(src, bl->id, improv_skillid, improv_skilllv);
				} else {
					if (ud->target)
						target_id = ud->target;
					else switch (src->type) {
						case BL_MOB: target_id = ((TBL_MOB*)src)->target_id; break;
						case BL_PET: target_id = ((TBL_PET*)src)->target_id; break;
					}
					if (!target_id)
						break;
					if (skill_get_casttype(improv_skillid) == CAST_GROUND) {
						bl = map_id2bl(target_id);
						if (!bl) bl = src;
						unit_skilluse_pos(src, bl->x, bl->y, improv_skillid, improv_skilllv);
					} else
						unit_skilluse_id(src, target_id, improv_skillid, improv_skilllv);
				}
			}
		}
		break;

	case SO_ARRULLO:
		rate = 15 + 5 * skill_lv + sstatus->int_ / 5 + status_get_job_lv_effect(src) / 5 - tstatus->int_ / 6 - tstatus->luk / 10;
		clif_skill_nodamage(src, bl, skill_id, skill_lv, sc_start(bl, type, rate, skill_lv, skill_get_time(skill_id, skill_lv)));
		break;

	case SO_EL_CONTROL:
		if ( sd )
		{
			if ( sd->ed )
			{// Lv 1 - Passive, Lv 2 - Defensive, Lv 3 - Offensive
				if ( skill_lv >= 1 && skill_lv <= 3 )
					elemental_set_control_mode(sd->ed, skill_lv);
				// Lv 4 - Ends the summon.
				else if ( skill_lv == 4 )
					elem_delete(sd->ed, 0);
				else// Safe guard in case a lv not supported is used.
					clif_skill_fail(sd,skill_id,0,0,0);

				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			}
			else
			{// Protection for GM's bypassing pre-cast requirements.
				clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}
		}
		break;

	case SO_SUMMON_AGNI:
	case SO_SUMMON_AQUA:
	case SO_SUMMON_VENTUS:
	case SO_SUMMON_TERA:
		if ( sd )
		{
			short summon_id = 0;

			if ( skill_lv < 1 || skill_lv > 3 )
			{// Avoid summoning anything outside of the range.
				clif_skill_fail(sd, skill_id, 0, 0, 0);
				break;
			}

			// Remove current existing elemental if one exists.
			if ( sd->ed )
				elem_delete(sd->ed, 0);

			switch ( skill_id )
			{// Which elemental is being summoned?
				case SO_SUMMON_AGNI: summon_id = MOBID_EL_AGNI_S+skill_lv-1; break;
				case SO_SUMMON_AQUA: summon_id = MOBID_EL_AQUA_S+skill_lv-1; break;
				case SO_SUMMON_VENTUS: summon_id = MOBID_EL_VENTUS_S+skill_lv-1; break;
				case SO_SUMMON_TERA: summon_id = MOBID_EL_TERA_S+skill_lv-1; break;
			}

			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			elem_create(sd, summon_id, skill_get_time(skill_id, skill_lv));
		}
		break;

	case SO_EL_ACTION:
		if ( sd )
		{
			if ( sd->ed )
			{
				unit_skilluse_id(&sd->ed->bl, bl->id, elemental_offensive_skill(sd->ed), 1);
				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
			}
			else
			{// Protection for GM's bypassing pre-cast requirements.
				clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}
		}
		break;

	case SO_EL_CURE:
		if( sd ) {
			int hp_amount = sstatus->max_hp * 10 / 100;
			int sp_amount = sstatus->max_sp * 10 / 100;

			if ( sd->ed && status_charge(src, hp_amount, sp_amount))
			{
				clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
				status_heal(&sd->ed->bl,hp_amount,sp_amount,0);
			}
			else
			{// Protection for GM's bypassing pre-cast requirements.
				clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}
		}
		break;

	case SO_EL_ANALYSIS:
	case GN_CHANGEMATERIAL:
		if( sd ) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			clif_skill_itemlistwindow(sd,skill_id,skill_lv);
		}
		break;

	case GN_MANDRAGORA:
		if( flag & 1 )
		{
			int chance = 25 + 10 * skill_lv - (tstatus->vit + tstatus->luk) / 5;
			if ( chance < 10 )
				chance = 10;//Minimal chance is 10%.
			if ( rnd()%100 < chance )
			{//Coded to both inflect the status and drain the target's SP only when successful. [Rytech]
				sc_start(bl, type, 100, skill_lv, skill_get_time(skill_id, skill_lv));
				status_zap(bl, 0, status_get_max_sp(bl) * (25 + 5 * skill_lv) / 100);
			}
		}
		else if ( sd )
		{
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), BL_CHAR,src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
			clif_skill_nodamage(bl, src, skill_id, skill_lv, 1);
		}
		break;

	case GN_SLINGITEM:
		if( sd ) 
		{
			short baselv;

			// Check if there's any ammo equipped.
			i = sd->equip_index[EQI_AMMO];
			if( i <= 0 )
				break; // No ammo.

			// Check if ammo ID is that of a Genetic throwing item.
			t_itemid ammo_id = sd->inventory_data[i]->nameid;
			if (!(itemid_is_sling_atk(ammo_id) || itemid_is_sling_buff(ammo_id)))
				break;

			// Used to tell other parts of the code which damage formula and status to use.
			sd->itemid = ammo_id;

			//Thrower's BaseLv affects HP and SP increase potions when thrown.
			baselv = status_get_base_lv_effect(src);

			// If thrown item is a bomb or a lump, then its a attack type ammo.
			if( itemid_is_sling_atk(ammo_id) )
			{	// Only allow throwing attacks at enemys.
				if(battle_check_target(src, bl, BCT_ENEMY) > 0)
				{	// Pineapple Bombs deal 5x5 splash damage on targeted enemy.
					if( ammo_id == ITEMID_PINEAPPLE_BOMB )
						map_foreachinrange(skill_area_sub, bl, 2, BL_CHAR, src, GN_SLINGITEM_RANGEMELEEATK, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_damage_id);
					else// All other bombs and lumps hits one enemy.
						skill_attack( BF_WEAPON, src, src, bl, GN_SLINGITEM_RANGEMELEEATK, skill_lv, tick, flag );
				} 
				else //Otherwise, it fails, shows animation and removes items.
					clif_skill_fail(sd,GN_SLINGITEM,0xa,0,0);
			}// If thrown item is a potion, food, powder, or overcooked food, then its a buff type ammo.
			else if ( itemid_is_sling_buff(ammo_id) )
			{
				switch ( ammo_id )
				{
					case ITEMID_MYSTERIOUS_POWDER:// MaxHP -2%
						sc_start(bl, SC_MYSTERIOUS_POWDER, 100, 2, 10000);
						break;
					case ITEMID_THROW_BOOST500:// ASPD +10%
						sc_start(bl, SC_BOOST500, 100, 10, 500000);
						break;
					case ITEMID_THROW_FULL_SWING_K:// WATK +50
						sc_start(bl, SC_FULL_SWING_K, 100, 50, 500000);
						break;
					case ITEMID_THROW_MANA_PLUS:// MATK +50
						sc_start(bl, SC_MANA_PLUS, 100, 50, 500000);
						break;
					case ITEMID_THROW_CURE_FREE:// Cures Poison, Curse, Silence, Bleeding, Undead, and Orcish and heals 500 HP.
						status_change_end(bl, SC_POISON, INVALID_TIMER);
						status_change_end(bl, SC_CURSE, INVALID_TIMER);
						status_change_end(bl, SC_SILENCE, INVALID_TIMER);
						status_change_end(bl, SC_BLEEDING, INVALID_TIMER);
						status_change_end(bl, SC_ORCISH, INVALID_TIMER);
						status_change_end(bl, SC_CHANGEUNDEAD, INVALID_TIMER);
						status_heal(bl, 500, 0, 0);
						break;
					case ITEMID_THROW_MUSTLE_M:// MaxHP +5%
						sc_start(bl, SC_MUSTLE_M, 100, 5, 500000);
						break;
					case ITEMID_THROW_LIFE_FORCE_F:// MaxSP +5%
						sc_start(bl, SC_LIFE_FORCE_F, 100, 5, 500000);
						break;
					case ITEMID_THROW_HP_POTION_SMALL:// MaxHP +(500 + Thrower BaseLv * 10 / 3) and heals 1% MaxHP
						sc_start4(bl, SC_PROMOTE_HEALTH_RESERCH, 100, 2, 1, baselv, 0, 500000);
						status_percent_heal(bl, 1, 0);
						break;
					case ITEMID_THROW_HP_POTION_MEDIUM:// MaxHP +(1500 + Thrower BaseLv * 10 / 3) and heals 2% MaxHP
						sc_start4(bl, SC_PROMOTE_HEALTH_RESERCH, 100, 2, 2, baselv, 0, 500000);
						status_percent_heal(bl, 2, 0);
						break;
					case ITEMID_THROW_HP_POTION_LARGE:// MaxHP +(2500 + Thrower BaseLv * 10 / 3) and heals 5% MaxHP
						sc_start4(bl, SC_PROMOTE_HEALTH_RESERCH, 100, 2, 3, baselv, 0, 500000);
						status_percent_heal(bl, 5, 0);
						break;
					case ITEMID_THROW_SP_POTION_SMALL:// MaxSP +(Thrower BaseLv / 10 - 5)% and recovers 2% MaxSP
						sc_start4(bl, SC_ENERGY_DRINK_RESERCH, 100, 2, 1, baselv, 0, 500000);
						status_percent_heal(bl, 0, 2);
						break;
					case ITEMID_THROW_SP_POTION_MEDIUM:// MaxSP +(Thrower BaseLv / 10)% and recovers 4% MaxSP
						sc_start4(bl, SC_ENERGY_DRINK_RESERCH, 100, 2, 2, baselv, 0, 500000);
						status_percent_heal(bl, 0, 4);
						break;
					case ITEMID_THROW_SP_POTION_LARGE:// MaxSP +(Thrower BaseLv / 10 + 5)% and recovers 8% MaxSP
						sc_start4(bl, SC_ENERGY_DRINK_RESERCH, 100, 2, 3, baselv, 0, 500000);
						status_percent_heal(bl, 0, 8);
						break;
					case ITEMID_THROW_EXTRACT_WHITE_POTION_Z:// Natural HP Recovery +20% and heals 1000 HP
						sc_start(bl, SC_EXTRACT_WHITE_POTION_Z, 100, 20, 500000);
						status_heal(bl, 1000, 0, 0);
						break;
					case ITEMID_THROW_VITATA_500:// Natural SP Recovery +20%, MaxSP +5%, and recovers 200 SP
						sc_start2(bl, SC_VITATA_500, 100, 20, 5, 500000);
						status_heal(bl, 0, 200, 0);
						break;
					case ITEMID_THROW_EXTRACT_SALAMINE_JUICE:// ASPD +10%
						sc_start(bl, SC_EXTRACT_SALAMINE_JUICE, 100, 10, 500000);
						break;
					case ITEMID_THROW_SAVAGE_STEAK:// STR +20
						sc_start(bl, SC_SAVAGE_STEAK, 100, 20, 300000);
						break;
					case ITEMID_THROW_COCKTAIL_WARG_BLOOD:// INT +20
						sc_start(bl, SC_COCKTAIL_WARG_BLOOD, 100, 20, 300000);
						break;
					case ITEMID_THROW_MINOR_BBQ:// VIT +20
						sc_start(bl, SC_MINOR_BBQ, 100, 20, 300000);
						break;
					case ITEMID_THROW_SIROMA_ICE_TEA:// DEX +20
						sc_start(bl, SC_SIROMA_ICE_TEA, 100, 20, 300000);
						break;
					case ITEMID_THROW_DROCERA_HERB_STEAMED:// AGI +20
						sc_start(bl, SC_DROCERA_HERB_STEAMED, 100, 20, 300000);
						break;
					case ITEMID_THROW_PUTTI_TAILS_NOODLES:// LUK +20
						sc_start(bl, SC_PUTTI_TAILS_NOODLES, 100, 20, 300000);
						break;
					case ITEMID_THROW_OVERDONE_FOOD:// Reduces all stats by random 5 - 10
						sc_start(bl, SC_STOMACHACHE, 100, rnd_value(5, 10), 60000);
						break;
				}
			}
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		break;

	case GN_MIX_COOKING:
	case GN_MAKEBOMB:
	case GN_S_PHARMACY:
		if (sd)
		{
			int qty = 1;
			sd->skillid_old = skill_id;
			sd->skilllv_old = skill_lv;
			if (skill_id != GN_S_PHARMACY && skill_lv > 1)
				qty = 10;
			clif_cooking_list(sd, (skill_id - GN_MIX_COOKING) + 27, skill_id, qty, skill_id == GN_MAKEBOMB ? 5 : 6);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
		break;

	case KO_ZANZOU:
		{
			struct mob_data *md;
			md = mob_once_spawn_sub(src, src->m, src->x, src->y, status_get_name(src), MOBID_KO_KAGE, "", 0, AI_NONE);
			if( md )
			{
				md->master_id = src->id;
				md->special_state.ai = AI_ZANZOU;
				if( md->deletetimer != INVALID_TIMER )
					delete_timer(md->deletetimer, mob_timer_delete);
				md->deletetimer = add_timer (gettick() + skill_get_time(skill_id, skill_lv), mob_timer_delete, md->bl.id, 0);
				mob_spawn( md );
			}
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			skill_blown(src,bl,skill_get_blewcount(skill_id,skill_lv),unit_getdir(bl),0);
		}
		break;

	case KO_KYOUGAKU:
		{
			int duration = skill_get_time(skill_id,skill_lv) - 1000 * tstatus->int_ / 20;
			rate = 45 + 5 * skill_lv - tstatus->int_ / 10;

			// Min rate is 5%.
			if ( rate < 5 )
				rate = 5;

			// Min duration is 1 second.
			if ( duration < 1000 )
				duration = 1000;

			// Usable only in siege areas tho its not fully clear if this is also allow in battlegrounds.
			// Also only usable on enemy players not in the this skill status or in monster form.
			if ( (!map_flag_gvg(src->m) && !map[src->m].flag.battleground) || bl->type != BL_PC ||
				(tsc && (tsc->data[SC_KYOUGAKU] || tsc->data[SC_MONSTER_TRANSFORM])) )
			{
				if(sd)
					clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}

			clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(bl,type,rate,skill_lv,duration));
		}
		break;

	case KO_JYUSATSU:
		rate = 45 + 10 * skill_lv - tstatus->int_ / 2;

		if ( rate < 5 )
			rate = 5;

		if ( bl->type != BL_PC )
		{
			if(sd)
				clif_skill_fail(sd,skill_id,0,0,0);
			break;
		}

		// Attempt to give the curse stats and then reduces target's current HP if curse chance is successful.
		if ( clif_skill_nodamage(src,bl,skill_id,skill_lv,sc_start(bl,SC_CURSE,rate,skill_lv,skill_get_time(skill_id,skill_lv))) );
			status_zap(bl, 5 * skill_lv * tstatus->hp / 100, 0);
		if ( status_get_lv(src) >= status_get_lv(bl) )
			status_change_start(src,bl,SC_COMA,10 * skill_lv,skill_lv,0,0,0,0,0);
		break;

	case KO_KAHU_ENTEN:
	case KO_HYOUHU_HUBUKI:
	case KO_KAZEHU_SEIRAN:
	case KO_DOHU_KOUKAI:
		if ( sd )
		{
			short charm_type = 0;

			switch (skill_id)
			{
				case KO_KAHU_ENTEN:
					charm_type = CHARM_FIRE;
					break;
				case KO_HYOUHU_HUBUKI:
					charm_type = CHARM_WATER;
					break;
				case KO_KAZEHU_SEIRAN:
					charm_type = CHARM_WIND;
					break;
				case KO_DOHU_KOUKAI:
					charm_type = CHARM_EARTH;
					break;
			}

			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			pc_addcharmball(sd,skill_get_time(skill_id,skill_lv),10,charm_type);
		}
		break;

	case KO_GENWAKU:
		rate = 45 + 5 * skill_lv - tstatus->int_ / 10;
		if ( rate < 5 )
			rate = 5;
		if ( rnd()%100 < rate )
		{
			int caster_x = src->x;
			int caster_y = src->y;
			int target_x = bl->x;
			int target_y = bl->y;
			if ( !status_get_class_(bl) == CLASS_BOSS)
			{
				clif_skill_nodamage(bl,bl,skill_id,skill_lv,1);// Caster
				unit_movepos(src,target_x,target_y,0,0);
				clif_slide(src,target_x,target_y) ;
				clif_skill_nodamage(src,src,skill_id,skill_lv,1);// Target
				unit_movepos(bl,caster_x,caster_y,0,0);
				clif_slide(bl,caster_x,caster_y) ;
				sc_start(src,SC_CONFUSION,25,skill_lv,skill_get_time(skill_id,skill_lv));
				sc_start(bl,SC_CONFUSION,75,skill_lv,skill_get_time(skill_id,skill_lv));
			}
			else
			{// Caster will warp up to the target if monster is boss type, but the targeted monster will not change position. [Rytech]
				clif_skill_nodamage(bl,bl,skill_id,skill_lv,1);
				unit_movepos(src,target_x,target_y,0,0);
				clif_slide(src,target_x,target_y) ;
				sc_start(src,SC_CONFUSION,25,skill_lv,skill_get_time(skill_id,skill_lv));
			}
		}
		break;

	case SP_SOULDIVISION:
	case KG_KYOMU:
	case KG_KAGEMUSYA:
	case OB_OBOROGENSOU:
	case OB_AKAITSUKI:
		if (skill_id == SP_SOULDIVISION || skill_id == KG_KAGEMUSYA)
		{// Usable only on other players.
			if ( bl->type != BL_PC )
			{
				if(sd)
					clif_skill_fail(sd,skill_id,0,0,0);
				break;
			}
		}

		//clif_skill_nodamage(src,bl,skill_id,skill_lv,1);// Its stupid that the skill animation doesn't trigger on this but it does on the damage one even tho its not a damage skill.
		clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, 0, 1, skill_id, -2, 6);// No flash / No Damage Sound / Displays Miss
		//clif_skill_damage(src, bl, tick, status_get_amotion(src), 0, -30000, 1, skill_id, -2, 6);// No Flash / Damage Sound / No Miss Display
		sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
		break;

	case KG_KAGEHUMI:
		if( flag&1 )
		{// Chase walk is not placed here since its immune to detection skills.
			if( tsc && (tsc->data[SC_HIDING] || tsc->data[SC_CLOAKING] || tsc->data[SC_CAMOUFLAGE] || 
				tsc->data[SC_CLOAKINGEXCEED] || tsc->data[SC__SHADOWFORM] || tsc->data[SC_NEWMOON]))
			{
				status_change_end(bl, SC_HIDING, INVALID_TIMER);
				status_change_end(bl, SC_CLOAKING, INVALID_TIMER);
				status_change_end(bl, SC_CAMOUFLAGE, INVALID_TIMER);
				status_change_end(bl, SC_CLOAKINGEXCEED, INVALID_TIMER);
				status_change_end(bl, SC__SHADOWFORM, INVALID_TIMER);
				status_change_end(bl, SC_NEWMOON, INVALID_TIMER);
				sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
			}
		}
		else
		{
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			map_foreachinrange(skill_area_sub, bl, skill_get_splash(skill_id, skill_lv), BL_CHAR,
				src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_nodamage_id);
		}
		break;

	case OB_ZANGETSU:
		//clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		clif_skill_damage(src, bl, tick, status_get_amotion(src), 0, 0, 1, skill_id, -2, 6);
		//clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, -30000, 1, skill_id, -2, 6);
		sc_start2(bl, type, 100, skill_lv, status_get_base_lv_effect(src), skill_get_time(skill_id, skill_lv));
		break;

	case RL_RICHS_COIN:
		if(sd) {
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			for (i = 0; i < 10; i++)
				pc_addspiritball(sd,skill_get_time(skill_id,skill_lv),10);
		}
		break;

	case RETURN_TO_ELDICASTES:
	case ALL_GUARDIAN_RECALL:
	case ECLAGE_RECALL:
		if( sd )
		{
			short x = 0, y = 0;// Destiny position.
			unsigned short mapindex = 0;
			if( skill_id == RETURN_TO_ELDICASTES)
			{
				x = 198;
				y = 187;
				mapindex  = mapindex_name2id(MAP_DICASTES);
			}
			else if ( skill_id == ALL_GUARDIAN_RECALL )
			{
				x = 44;
				y = 151;
				mapindex  = mapindex_name2id(MAP_MORA);
			}
			else if ( skill_id == ECLAGE_RECALL )
			{
				x = 47;
				y = 31;
				mapindex  = mapindex_name2id("ecl_in01");
			}
			if( !mapindex )
			{// If the map is not found, fail the skill to prevent warping the player to a non existing map.
				clif_skill_fail(sd, skill_id, 0, 0, 0);
				map_freeblock_unlock();
				return 0;
			}
			else
				pc_setpos(sd, mapindex, x, y, CLR_TELEPORT);
		}
		break;

	case ECL_SNOWFLIP:
	case ECL_PEONYMAMY:
	case ECL_SADAGUI:
	case ECL_SEQUOIADUST:
		if ( skill_id == ECL_SNOWFLIP )
		{
			status_change_end(bl, SC_SLEEP, INVALID_TIMER);
			status_change_end(bl, SC_BLEEDING, INVALID_TIMER);
			status_change_end(bl, SC_BURNING, INVALID_TIMER);
			status_change_end(bl, SC_DEEPSLEEP, INVALID_TIMER);
		}
		else if ( skill_id == ECL_PEONYMAMY )
		{
			status_change_end(bl, SC_FREEZE, INVALID_TIMER);
			status_change_end(bl, SC_FROST, INVALID_TIMER);
			status_change_end(bl, SC_CRYSTALIZE, INVALID_TIMER);
		}
		else if ( skill_id == ECL_SADAGUI )
		{
			status_change_end(bl, SC_STUN, INVALID_TIMER);
			status_change_end(bl, SC_CONFUSION, INVALID_TIMER);
			status_change_end(bl, SC_HALLUCINATION, INVALID_TIMER);
			status_change_end(bl, SC_FEAR, INVALID_TIMER);
		}
		else if ( skill_id == ECL_SEQUOIADUST )
		{
			status_change_end(bl, SC_STONE, INVALID_TIMER);
			status_change_end(bl, SC_POISON, INVALID_TIMER);
			status_change_end(bl, SC_CURSE, INVALID_TIMER);
			status_change_end(bl, SC_BLIND, INVALID_TIMER);
			status_change_end(bl, SC_ORCISH, INVALID_TIMER);
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		clif_skill_damage(src,bl,tick, status_get_amotion(src), 0, 0, 1, skill_id, -2, 6);
		break;

	case SU_HIDE:
		if (tsce)
		{
			clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
			status_change_end(bl, type, INVALID_TIMER);
			map_freeblock_unlock();
			return 0;
		}
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		sc_start(bl,type,100,skill_lv,skill_get_time(skill_id,skill_lv));
		break;

	case SU_TUNAPARTY:// MaxHP of the caster affects the tuna's HP.
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		sc_start2(bl,type,100,skill_lv,sstatus->max_hp,skill_get_time(skill_id,skill_lv));
		break;

	case NPC_WIDESOULDRAIN:
		if ( flag&1 )
			status_percent_damage( src, bl, 0, ( (skill_lv - 1)%5 + 1 ) * 20, false );
		else 
		{
			skill_area_temp[2] = 0; //For SD_PREAMBLE
			clif_skill_nodamage( src, bl, skill_id, skill_lv, 1 );
			map_foreachinrange( skill_area_sub, bl, skill_get_splash( skill_id, skill_lv ),BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|SD_PREAMBLE|1, skill_castend_nodamage_id );
		}
		break;
	case ALL_PARTYFLEE:
		if (sd && !(flag & 1))
		{
			if (!sd->status.party_id)
			{
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
				break;
			}
			party_foreachsamemap(skill_area_sub, sd, skill_get_splash(skill_id, skill_lv), src, skill_id, skill_lv, tick, flag | BCT_PARTY | 1, skill_castend_nodamage_id);
		}
		else
			clif_skill_nodamage(src, bl, skill_id, skill_lv, sc_start(bl, type, 100, skill_lv, skill_get_time(skill_id, skill_lv)));
		break;
	case NPC_TALK:
	case ALL_CATCRY:
	case ALL_DREAM_SUMMERNIGHT:
	case ALL_WEWISH:
		clif_skill_nodamage( src, bl, skill_id, skill_lv, 1 );
		break;

	case GM_SANDMAN:
		if (tsc) {
			if (tsc->opt1 == OPT1_SLEEP)
				tsc->opt1 = 0;
			else
				tsc->opt1 = OPT1_SLEEP;
			clif_changeoption(bl);
			clif_skill_nodamage(src, bl, skill_id, skill_lv, 1);
		}
		break;

	case ALL_BUYING_STORE:
		if( sd )
		{// players only, skill allows 5 buying slots
			clif_skill_nodamage(src, bl, skill_id, skill_lv, buyingstore_setup(sd, MAX_BUYINGSTORE_SLOTS));
		}
		break;

	case ALL_EQSWITCH:
		if (sd) {
			clif_equipswitch_reply(sd, false);

			for (int i = 0, position = 0; i < EQI_MAX; i++) {
				if (sd->equip_switch_index[i] >= 0 && !(position & equip_bitmask[i])) {
					position |= pc_equipswitch(sd, sd->equip_switch_index[i]);
				}
			}
		}
		break;

	default:
		ShowWarning("skill_castend_nodamage_id: Unknown skill used:%d\n",skill_id);
		clif_skill_nodamage(src,bl,skill_id,skill_lv,1);
		map_freeblock_unlock();
		return 1;
	}

	if (dstmd) { //Mob skill event for no damage skills (damage ones are handled in battle_calc_damage) [Skotlex]
		mob_log_damage(dstmd, src, 0); //Log interaction (counts as 'attacker' for the exp bonus)
		mobskill_event(dstmd, src, tick, MSC_SKILLUSED|(skill_id<<16));
	}

	if( sd && !(flag&1) )
	{// ensure that the skill last-cast tick is recorded
		sd->canskill_tick = gettick();

		if( sd->state.arrow_atk ) //Consume arrow on last invocation to this skill.
			battle_consume_ammo(sd, skill_id, skill_lv);

		skill_onskillusage(sd, bl, skill_id, tick);

		// perform skill requirement consumption
		skill_consume_requirement(sd,skill_id,skill_lv,2);
	}

	map_freeblock_unlock();
	return 0;
}

/**
 * Checking that causing skill failed
 * @param src Caster
 * @param target Target
 * @param skill_id
 * @param skill_lv
 * @return -1 success, others are failed @see enum useskill_fail_cause.
 **/
static int8 skill_castend_id_check(struct block_list *src, struct block_list *target, uint16 skill_id, uint16 skill_lv) {
	int inf = skill_get_inf(skill_id);
	int inf2 = skill_get_inf2(skill_id);
	struct map_session_data *sd = BL_CAST(BL_PC,  src);
	struct status_change*sc, *tsc = status_get_sc(target);

	if (src != target && (status_bl_has_mode(target, MD_SKILL_IMMUNE) || (status_get_class(target) == MOBID_EMPERIUM && !(skill_id == MO_TRIPLEATTACK || skill_id == HW_GRAVITATION))) && skill_get_casttype(skill_id) == CAST_NODAMAGE)
		return USESKILL_FAIL_MAX; // Don't show a skill fail message (NoDamage type doesn't consume requirements)

	switch (skill_id) {
		case RG_BACKSTAP:
			{
				uint8 dir = map_calc_dir(src,target->x,target->y),t_dir = unit_getdir(target);
				if (check_distance_bl(src, target, 0) || map_check_dir(dir,t_dir))
					return USESKILL_FAIL_MAX;
			}
			break;
		case PR_TURNUNDEAD:
			{
				struct status_data *tstatus = status_get_status_data(target);
				if (!battle_check_undead(tstatus->race, tstatus->def_ele))
					return USESKILL_FAIL_MAX;
			}
			break;
		case PR_LEXDIVINA:
		case MER_LEXDIVINA:
			{
				//If it's not an enemy, and not silenced, you can't use the skill on them. [Skotlex]
				if (battle_check_target(src,target, BCT_ENEMY) <= 0 && (!tsc || !tsc->data[SC_SILENCE])) {
					clif_skill_nodamage (src, target, skill_id, skill_lv, 0);
					return USESKILL_FAIL_MAX;
				}
			}
			break;
		case RA_WUGSTRIKE:
			// Check if path can be reached
			if (!path_search(NULL,src->m,src->x,src->y,target->x,target->y,1,CELL_CHKNOREACH))
				return USESKILL_FAIL_MAX;
			break;
		case MG_NAPALMBEAT:
		case MG_FIREBALL:
		case HT_BLITZBEAT:
		case AS_GRIMTOOTH:
		case MO_COMBOFINISH:
		case NC_VULCANARM:
		case SR_TIGERCANNON:
			// These can damage traps, but can't target traps directly
			if (target->type == BL_SKILL) {
				TBL_SKILL *su = (TBL_SKILL*)target;
				if (!su || !su->group)
					return USESKILL_FAIL_MAX;
				if (skill_get_inf2(su->group->skill_id)&INF2_TRAP)
					return USESKILL_FAIL_MAX;
			}
			break;
	}

	if (inf&INF_ATTACK_SKILL ||
		(inf&INF_SELF_SKILL && inf2&INF2_NO_TARGET_SELF) //Combo skills
		) // Casted through combo.
		inf = BCT_ENEMY; //Offensive skill.
	else if (inf2&INF2_NO_ENEMY)
		inf = BCT_NOENEMY;
	else
		inf = 0;

	if (inf2 & (INF2_PARTY_ONLY|INF2_GUILD_ONLY) && src != target) {
		inf |=
			(inf2&INF2_PARTY_ONLY?BCT_PARTY:0)|
			(inf2&INF2_GUILD_ONLY?BCT_GUILD:0);
		//Remove neutral targets (but allow enemy if skill is designed to be so)
		inf &= ~BCT_NEUTRAL;
	}

	switch (skill_id) {
		// Cannot be casted to Emperium,
		case WZ_ESTIMATION:
		case SL_SKE:
		case SL_SKA:
			if (target->type == BL_MOB && ((TBL_MOB*)target)->mob_id == MOBID_EMPERIUM)
				return USESKILL_FAIL_MAX;
			break;
		// Still can be casted to party member in normal map
		case RK_PHANTOMTHRUST:
		case AB_CLEARANCE:
			if (target->type != BL_MOB && !map_flag_vs(src->m) && battle_check_target(src,target,BCT_PARTY) <= 0)
				return USESKILL_FAIL_MAX;
			inf |= BCT_PARTY;
			break;
	}

	if (sc = status_get_sc(src))
	{
		if (sc && sc->data[SC_KYOMU] && rnd() % 100 < sc->data[SC_KYOMU]->val2)
		{// Shadow Void makes all skills fail by 5 * SkillLV % Chance.
			if (sd) clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
			return USESKILL_FAIL_LEVEL;
		}

		if (sc && sc->data[SC_VOLCANIC_ASH] && rnd() % 100 < 50)
		{// Volcanic Ash makes all skills fail at 50%
			if (sd) clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
			return USESKILL_FAIL_LEVEL;
		}
	}

	if (sc = status_get_sc(target))
	{
		// Fogwall makes all offensive-type targetted skills fail at 75%
		// Jump Kick can still fail even though you can jump to friendly targets.
		if ((inf&BCT_ENEMY || skill_id == TK_JUMPKICK) && tsc && tsc->data[SC_FOGWALL] && rnd() % 100 < 75)
		{
			if (sd) clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
			return USESKILL_FAIL_LEVEL;
		}
	}

	if (inf && battle_check_target(src, target, inf) <= 0)
		return USESKILL_FAIL_LEVEL;

	//Fogwall makes all offensive-type targetted skills fail at 75%
	if (inf&BCT_ENEMY && tsc && tsc->data[SC_FOGWALL] && rnd() % 100 < 75)
		return USESKILL_FAIL_LEVEL;

	return -1;
}

/**
 * Check & process skill to target on castend. Determines if skill is 'damage' or 'nodamage'
 * @param tid
 * @param tick
 * @param data
 **/
int skill_castend_id(int tid, int64 tick, int id, intptr_t data)
{
	struct block_list *target, *src;
	struct map_session_data *sd;
	struct mob_data *md;
	struct homun_data *hd;
	struct unit_data *ud;
	struct status_change *sc = NULL;
	int flag = 0;

	src = map_id2bl(id);
	if( src == NULL )
	{
		ShowDebug("skill_castend_id: src == NULL (tid=%d, id=%d)\n", tid, id);
		return 0;// not found
	}

	ud = unit_bl2ud(src);
	if( ud == NULL )
	{
		ShowDebug("skill_castend_id: ud == NULL (tid=%d, id=%d)\n", tid, id);
		return 0;// ???
	}

	sd = BL_CAST(BL_PC,  src);
	md = BL_CAST(BL_MOB, src);
	hd = BL_CAST(BL_HOM, src);

	if( src->prev == NULL ) {
		ud->skilltimer = INVALID_TIMER;
		return 0;
	}

	if(ud->skill_id != SA_CASTCANCEL  &&
	!(ud->skill_id == SO_SPELLFIST && (sd && (sd->skillid_old == MG_FIREBOLT || sd->skillid_old == MG_COLDBOLT || sd->skillid_old == MG_LIGHTNINGBOLT))) )
	{// otherwise handled in unit_skillcastcancel()
		if( ud->skilltimer != tid ) {
			ShowError("skill_castend_id: Timer mismatch %d!=%d!\n", ud->skilltimer, tid);
			ud->skilltimer = INVALID_TIMER;
			return 0;
		}

		if( sd && ud->skilltimer != -1 && (pc_checkskill(sd,SA_FREECAST) > 0 || ud->skill_id == LG_EXEEDBREAK) )
		{// restore original walk speed
			ud->skilltimer = INVALID_TIMER;
			status_calc_bl(&sd->bl, SCB_SPEED);
		}

		ud->skilltimer = INVALID_TIMER;
	}

	if (ud->skilltarget == id)
		target = src;
	else
		target = map_id2bl(ud->skilltarget);

	// Use a do so that you can break out of it when the skill fails.
	do {
		bool fail = false;
		int8 res = USESKILL_FAIL_LEVEL;
		if (!target || target->prev == NULL)
			break;

		if(src->m != target->m || status_isdead(src))
			break;

		//These should become skill_castend_pos
		switch (ud->skill_id) {
			case WE_CALLPARTNER:
				if(sd)
					clif_callpartner(sd);
			case WE_CALLPARENT:
				if (sd) {
					struct map_session_data *f_sd = pc_get_father(sd);
					struct map_session_data *m_sd = pc_get_mother(sd);

					if ((f_sd && f_sd->state.autotrade) || (m_sd && m_sd->state.autotrade)) {
						fail = true;
						break;
					}
				}
			case WE_CALLBABY:
				if (sd) {
					struct map_session_data *c_sd = pc_get_child(sd);

					if (c_sd && c_sd->state.autotrade) {
						fail = true;
						break;
					}
				}
			case AM_RESURRECTHOMUN:
			case PF_SPIDERWEB:
			{
				//Find a random spot to place the skill. [Skotlex]
				int splash = skill_get_splash(ud->skill_id, ud->skill_lv);
				ud->skillx = target->x + splash;
				ud->skilly = target->y + splash;
				if (splash && !map_random_dir(target, &ud->skillx, &ud->skilly)) {
					ud->skillx = target->x;
					ud->skilly = target->y;
				}
				ud->skilltimer = tid;
				return skill_castend_pos(tid, tick, id, data);
			}
			case GN_WALLOFTHORN:
			case RL_B_TRAP:
			case KO_MAKIBISHI:
			case SU_CN_POWDERING:
			case MH_SUMMON_LEGION:
			case MH_STEINWAND:// FIX ME - I need to spawn 2 AoE's. One on the master and the homunculus.
				ud->skillx = target->x;
				ud->skilly = target->y;
				ud->skilltimer=tid;
				return skill_castend_pos(tid,tick,id,data);

			case RL_HAMMER_OF_GOD:
			{
				short drop_zone = skill_get_splash(ud->skill_id,ud->skill_lv);

				sc = status_get_sc(target);

				if ( sc && sc->data[SC_C_MARKER] )
				{// Strike directly where the target stands if it has a crimson mark on it.
					ud->skillx = target->x;
					ud->skilly = target->y;
				}
				else
				{// Strike a random spot in a 15x15 area around the target if it doesn't have a crimson mark.
					ud->skillx = target->x - drop_zone + rnd()%(drop_zone * 2 + 1);
					ud->skilly = target->y - drop_zone + rnd()%(drop_zone * 2 + 1);
				}

				ud->skilltimer = tid;
				return skill_castend_pos(tid,tick,id,data);
			}
		}

		// Failing
		if (fail || (res = skill_castend_id_check(src, target, ud->skill_id, ud->skill_lv)) >= 0) {
			if (sd && res != USESKILL_FAIL_MAX)
				clif_skill_fail(sd, ud->skill_id, (enum useskill_fail_cause)res, 0, 0);
			break;
		}

		//Avoid doing double checks for instant-cast skills.
		if( tid != INVALID_TIMER && !status_check_skilluse(src, target, ud->skill_id, ud->skill_lv, 1) )
			break;

		if(md) {
			md->last_thinktime=tick +MIN_MOBTHINKTIME;
			if(md->skillidx >= 0 && md->db->skill[md->skillidx].emotion >= 0)
				clif_emotion(src, md->db->skill[md->skillidx].emotion);
		}

		if (src != target && battle_config.skill_add_range &&
			!check_distance_bl(src, target, skill_get_range2(src, ud->skill_id, ud->skill_lv, true) + battle_config.skill_add_range))
		{
			if (sd) {
				clif_skill_fail(sd, ud->skill_id, USESKILL_FAIL_LEVEL, 0, 0);
				if (battle_config.skill_out_range_consume) //Consume items anyway. [Skotlex]
					skill_consume_requirement(sd, ud->skill_id, ud->skill_lv, 3);
			}
			break;
		}

		if( sd )
		{
			if( !skill_check_condition_castend(sd, ud->skill_id, ud->skill_lv) )
				break;
			else {
				skill_consume_requirement(sd, ud->skill_id, ud->skill_lv, 1);
				if (src != target && (status_bl_has_mode(target, MD_SKILL_IMMUNE) || (status_get_class(target) == MOBID_EMPERIUM && !(ud->skill_id == MO_TRIPLEATTACK || ud->skill_id == HW_GRAVITATION))) && skill_get_casttype(ud->skill_id) == CAST_DAMAGE) {
					clif_skill_fail(sd, ud->skill_id, USESKILL_FAIL_LEVEL, 0, 0);
					break; // Show a skill fail message (Damage type consumes requirements)
				}
			}
		}

#ifdef OFFICIAL_WALKPATH
		if (skill_get_casttype(ud->skill_id) != CAST_NODAMAGE && !path_search_long(NULL, src->m, src->x, src->y, target->x, target->y, CELL_CHKWALL))
			break;
#endif

		if ((src->type == BL_HOM || src->type == BL_MER || src->type == BL_ELEM) && !skill_check_condition_mercenary(src, ud->skill_id, ud->skill_lv, 1))
			break;

		if (ud->state.running && ud->skill_id == TK_JUMPKICK)
		{
			ud->state.running = 0;
			status_change_end(src, SC_RUN, INVALID_TIMER);
			flag = 1;
		}

		if (ud->walktimer != INVALID_TIMER && ud->skill_id != TK_RUN && ud->skill_id != RA_WUGDASH)
			unit_stop_walking(src,1);

		if (sd && skill_get_cooldown(ud->skill_id, ud->skill_lv) > 0) // Skill cooldown. [LimitLine]
			skill_blockpc_start(sd, ud->skill_id, skill_cooldownfix(src, ud->skill_id, ud->skill_lv));
		if (hd && skill_get_cooldown(ud->skill_id, ud->skill_lv))
			skill_blockhomun_start(hd, ud->skill_id, skill_get_cooldown(ud->skill_id, ud->skill_lv));
		if( !sd || sd->skillitem != ud->skill_id || skill_get_delay(ud->skill_id,ud->skill_lv) )
			ud->canact_tick = max(tick + skill_delayfix(src, ud->skill_id, ud->skill_lv), ud->canact_tick - SECURITY_CASTTIME);
		if( battle_config.display_status_timers && sd )
			clif_status_change(src, SI_ACTIONDELAY, 1, skill_delayfix(src, ud->skill_id, ud->skill_lv), 0, 0, 0);
		if( sd && (sd->skillitem != ud->skill_id || skill_get_cooldown(ud->skill_id,ud->skill_lv)) )
			skill_blockpc_start(sd, ud->skill_id, skill_get_cooldown(ud->skill_id, ud->skill_lv));
		if( sd )
		{
			switch( ud->skill_id )
			{
			case GS_DESPERADO:
				sd->canequip_tick = tick + skill_get_time(ud->skill_id, ud->skill_lv);
				break;
			case CR_GRANDCROSS:
			case NPC_GRANDDARKNESS:
				if( (sc = status_get_sc(src)) && sc->data[SC_STRIPSHIELD] )
				{
					const struct TimerData *timer = get_timer(sc->data[SC_STRIPSHIELD]->timer);
					if( timer && timer->func == status_change_timer && DIFF_TICK(timer->tick,gettick()+skill_get_time(ud->skill_id, ud->skill_lv)) > 0 )
						break;
				}
				sc_start2(src, SC_STRIPSHIELD, 100, 0, 1, skill_get_time(ud->skill_id, ud->skill_lv));
				break;
			}
		}
		if (skill_get_state(ud->skill_id) != ST_MOVE_ENABLE)
			unit_set_walkdelay(src, tick, battle_config.default_walk_delay+skill_get_walkdelay(ud->skill_id, ud->skill_lv), 1);

		if(battle_config.skill_log && battle_config.skill_log&src->type)
			ShowInfo("Type %d, ID %d skill castend id [id =%d, lv=%d, target ID %d]\n",
				src->type, src->id, ud->skill_id, ud->skill_lv, target->id);

		map_freeblock_lock();

		sc = status_get_sc(src);
		// Cancel status after skill cast timer ends but before skill behavior starts.
		if( sc && sc->data[SC_CAMOUFLAGE] )
			status_change_end(src,SC_CAMOUFLAGE,INVALID_TIMER);

		if (skill_get_casttype(ud->skill_id) == CAST_NODAMAGE)
			skill_castend_nodamage_id(src,target,ud->skill_id,ud->skill_lv,tick,flag);
		else
			skill_castend_damage_id(src,target,ud->skill_id,ud->skill_lv,tick,flag);

		if(sc && sc->count) {
			if(sc->data[SC_SPIRIT] &&
				sc->data[SC_SPIRIT]->val2 == SL_WIZARD &&
				sc->data[SC_SPIRIT]->val3 == ud->skill_id &&
			  	ud->skill_id != WZ_WATERBALL)
				sc->data[SC_SPIRIT]->val3 = 0; //Clear bounced spell check.

			if( sc->data[SC_DANCING] && skill_get_inf2(ud->skill_id)&INF2_SONG_DANCE && sd )
				skill_blockpc_start(sd,BD_ADAPTATION,3000);
		}

		if( sd && ud->skill_id != SA_ABRACADABRA && ud->skill_id != WM_RANDOMIZESPELL ) // Hocus-Pocus has just set the data so leave it as it is.[Inkfish]
			sd->skillitem = sd->skillitemlv = sd->skillitem_keep_requirement = 0;

		if (ud->skilltimer == INVALID_TIMER) {
			if(md) md->skillidx = -1;
			else ud->skill_id = 0; //mobs can't clear this one as it is used for skill condition 'afterskill'
			ud->skill_lv = ud->skilltarget = 0;
		}
		map_freeblock_unlock();
		return 1;
	} while(0);

	//Skill failed.
	if (ud->skill_id == MO_EXTREMITYFIST && sd && !(sc && sc->data[SC_FOGWALL]))
  	{	//When Asura fails... (except when it fails from Fog of Wall)
		//Consume SP/spheres
		skill_consume_requirement(sd,ud->skill_id, ud->skill_lv,1);
		status_set_sp(src, 0, 0);
		sc = &sd->sc;
		if (sc->count)
		{	//End states
			status_change_end(src, SC_EXPLOSIONSPIRITS, INVALID_TIMER);
			status_change_end(src, SC_BLADESTOP, INVALID_TIMER);
		}
		if (target && target->m == src->m)
		{	//Move character to target anyway.
			int dx,dy;
			dx = target->x - src->x;
			dy = target->y - src->y;
			if(dx > 0) dx++;
			else if(dx < 0) dx--;
			if (dy > 0) dy++;
			else if(dy < 0) dy--;

			if (unit_movepos(src, src->x+dx, src->y+dy, 1, 1))
			{	//Display movement + animation.
				clif_slide(src,src->x,src->y);
				clif_skill_damage(src,target,tick,sd->battle_status.amotion,0,0,1,ud->skill_id, ud->skill_lv, 5);
			}
			clif_skill_fail(sd,ud->skill_id,USESKILL_FAIL_LEVEL,0,0);
		}
	}

	ud->skill_id = ud->skilltarget = 0;
	if( !sd || sd->skillitem != ud->skill_id || skill_get_delay(ud->skill_id,ud->skill_lv) )
		ud->canact_tick = tick;
	//You can't place a skill failed packet here because it would be
	//sent in ALL cases, even cases where skill_check_condition fails
	//which would lead to double 'skill failed' messages u.u [Skotlex]
	if(sd)
		sd->skillitem = sd->skillitemlv = sd->skillitem_keep_requirement = 0;
	else if(md)
		md->skillidx = -1;
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_castend_pos(int tid, int64 tick, int id, intptr_t data)
{
	struct block_list* src = map_id2bl(id);
	struct map_session_data *sd;
	struct mob_data *md;
	struct homun_data *hd;
	struct unit_data *ud = unit_bl2ud(src);
	struct status_change *sc = NULL;
	int maxcount;

	nullpo_ret(ud);

	sd = BL_CAST(BL_PC , src);
	md = BL_CAST(BL_MOB, src);
	hd = BL_CAST(BL_HOM, src);

	if( src->prev == NULL ) {
		ud->skilltimer = INVALID_TIMER;
		return 0;
	}

	if( ud->skilltimer != tid )
	{
		ShowError("skill_castend_pos: Timer mismatch %d!=%d\n", ud->skilltimer, tid);
		ud->skilltimer = INVALID_TIMER;
		return 0;
	}

	if( sd && ud->skilltimer != INVALID_TIMER && pc_checkskill(sd,SA_FREECAST) > 0 )
	{// restore original walk speed
		ud->skilltimer = INVALID_TIMER;
		status_calc_bl(&sd->bl, SCB_SPEED);
	}
	ud->skilltimer = INVALID_TIMER;

	do {
		if( status_isdead(src) )
			break;

		if( !(src->type&battle_config.skill_reiteration) &&
			skill_get_unit_flag(ud->skill_id)&UF_NOREITERATION &&
			skill_check_unit_range(src,ud->skillx,ud->skilly,ud->skill_id,ud->skill_lv)
		  )
		{
			if (sd) clif_skill_fail(sd,ud->skill_id,USESKILL_FAIL_LEVEL,0,0);
			break;
		}
		if( src->type&battle_config.skill_nofootset &&
			skill_get_unit_flag(ud->skill_id)&UF_NOFOOTSET &&
			skill_check_unit_range2(src,ud->skillx,ud->skilly,ud->skill_id,ud->skill_lv)
		  )
		{
			if (sd) clif_skill_fail(sd,ud->skill_id,USESKILL_FAIL_LEVEL,0,0);
			break;
		}
		if( src->type&battle_config.land_skill_limit &&
			(maxcount = skill_get_maxcount(ud->skill_id, ud->skill_lv)) > 0
		  ) {
			int i;
			for(i=0;i<MAX_SKILLUNITGROUP && ud->skillunit[i] && maxcount;i++) {
				if(ud->skillunit[i]->skill_id == ud->skill_id)
					maxcount--;
			}
			if( maxcount <= 0 )
			{
				if (sd) clif_skill_fail(sd,ud->skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
		}

		if (sc = status_get_sc(src))
		{
			if(sc && sc->data[SC_KYOMU] && rnd()%100 < sc->data[SC_KYOMU]->val2)
		  	{// Shadow Void makes all skills fail by 5 * SkillLV % Chance.
				if (sd) clif_skill_fail(sd,ud->skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}

			if(sc && sc->data[SC_VOLCANIC_ASH] && rnd()%100 < 50)
		  	{// Volcanic Ash makes all skills fail at 50%
				if (sd) clif_skill_fail(sd,ud->skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
		}

		if(tid != INVALID_TIMER)
		{	//Avoid double checks on instant cast skills. [Skotlex]
			if( !status_check_skilluse(src, NULL, ud->skill_id, ud->skill_lv, 1) )
				break;
			if (battle_config.skill_add_range &&
				!check_distance_blxy(src, ud->skillx, ud->skilly, skill_get_range2(src, ud->skill_id, ud->skill_lv, true) + battle_config.skill_add_range)) {
				if (sd && battle_config.skill_out_range_consume) //Consume items anyway.
					skill_consume_requirement(sd,ud->skill_id,ud->skill_lv,3);
				break;
			}
		}

		if( sd )
		{
			if(ud->skill_id != AL_WARP && !skill_check_condition_castend(sd, ud->skill_id, ud->skill_lv) )
				break;
			else
				skill_consume_requirement(sd,ud->skill_id,ud->skill_lv,1);
		}

		if ((src->type == BL_HOM || src->type == BL_MER || src->type == BL_ELEM) && !skill_check_condition_mercenary(src, ud->skill_id, ud->skill_lv, 1))
			break;

		if(md) {
			md->last_thinktime=tick +MIN_MOBTHINKTIME;
			if(md->skillidx >= 0 && md->db->skill[md->skillidx].emotion >= 0)
				clif_emotion(src, md->db->skill[md->skillidx].emotion);
		}

		if(battle_config.skill_log && battle_config.skill_log&src->type)
			ShowInfo("Type %d, ID %d skill castend pos [id =%d, lv=%d, (%d,%d)]\n",
				src->type, src->id, ud->skill_id, ud->skill_lv, ud->skillx, ud->skilly);

		if (ud->walktimer != INVALID_TIMER)
			unit_stop_walking(src,1);

		if (sd && skill_get_cooldown(ud->skill_id, ud->skill_lv) > 0) // Skill cooldown. [LimitLine]
			skill_blockpc_start(sd, ud->skill_id, skill_cooldownfix(src, ud->skill_id, ud->skill_lv));
		if (hd && skill_get_cooldown(ud->skill_id, ud->skill_lv))
			skill_blockhomun_start(hd, ud->skill_id, skill_get_cooldown(ud->skill_id, ud->skill_lv));
		if( !sd || sd->skillitem != ud->skill_id || skill_get_delay(ud->skill_id,ud->skill_lv) )
			ud->canact_tick = max(tick + skill_delayfix(src, ud->skill_id, ud->skill_lv), ud->canact_tick - SECURITY_CASTTIME);
		if( battle_config.display_status_timers && sd )
			clif_status_change(src, SI_ACTIONDELAY, 1, skill_delayfix(src, ud->skill_id, ud->skill_lv), 0, 0, 0);
		if( sd && (sd->skillitem != ud->skill_id || skill_get_cooldown(ud->skill_id,ud->skill_lv)) )
			skill_blockpc_start(sd, ud->skill_id, skill_get_cooldown(ud->skill_id, ud->skill_lv));
//		if( sd )
//		{
//			switch( ud->skill_id )
//			{
//			case ????:
//				sd->canequip_tick = tick + ????;
//				break;
//			}
//		}
		unit_set_walkdelay(src, tick, battle_config.default_walk_delay+skill_get_walkdelay(ud->skill_id, ud->skill_lv), 1);

		map_freeblock_lock();
		skill_castend_pos2(src,ud->skillx,ud->skilly,ud->skill_id,ud->skill_lv,tick,0);

		if( sd && sd->skillitem != AL_WARP ) // Warp-Portal thru items will clear data in skill_castend_map. [Inkfish]
			sd->skillitem = sd->skillitemlv = sd->skillitem_keep_requirement = 0;

		if (ud->skilltimer == INVALID_TIMER) {
			if (md) md->skillidx = -1;
			else ud->skill_id = 0; //Non mobs can't clear this one as it is used for skill condition 'afterskill'
			ud->skill_lv = ud->skillx = ud->skilly = 0;
		}

		map_freeblock_unlock();
		return 1;
	} while(0);

	if( !sd || sd->skillitem != ud->skill_id || skill_get_delay(ud->skill_id,ud->skill_lv) )
		ud->canact_tick = tick;
	ud->skill_id = ud->skill_lv = 0;
	if(sd)
		sd->skillitem = sd->skillitemlv = sd->skillitem_keep_requirement = 0;
	else if(md)
		md->skillidx  = -1;
	return 0;

}

/* skill count without self */
static int skill_count_wos(struct block_list *bl, va_list ap) {
	struct block_list* src = va_arg(ap, struct block_list*);
	if (src->id != bl->id) {
		return 1;
	}
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_castend_pos2(struct block_list* src, int x, int y, int skill_id, int skill_lv, int64 tick, int flag)
{
	struct map_session_data* sd;
	struct elemental_data* ed;
	struct status_change* sc;
	struct status_change_entry *sce;
	struct skill_unit_group* sg;
	enum sc_type type;
	int i;

	//if(skill_lv <= 0) return 0;
	if(skill_id > 0 && skill_lv <= 0) return 0;	// celest

	nullpo_ret(src);

	if(status_isdead(src))
		return 0;

	sd = BL_CAST(BL_PC, src);
	ed = BL_CAST(BL_ELEM, src);

	sc = status_get_sc(src);
	type = status_skill2sc(skill_id);
	sce = (sc && type != -1)?sc->data[type]:NULL;

	// Cancel status after skill cast timer ends but before skill behavior starts.
	if( sc && sc->data[SC_CAMOUFLAGE] )
		status_change_end(src,SC_CAMOUFLAGE,INVALID_TIMER);

	switch (skill_id) { //Skill effect.
		// Skills listed here will not display their animation.
		// This is for special cases where the animation should
		// only show under certain coditions which is set elsewhere.
		case WZ_METEOR:
		case WZ_ICEWALL:
		case MO_BODYRELOCATION:
		case CR_CULTIVATION:
		case HW_GANBANTEIN:
		case SC_CONFUSIONPANIC:
		case SC_BLOODYLUST:
		case SC_MAELSTROM:
		case LG_EARTHDRIVE:
		case GN_CRAZYWEED_ATK:
		case RL_FIRE_RAIN:
		//case KO_MAKIBISHI:// Enable once I figure out how to prevent movement stopping. [Rytech]
		case SU_CN_METEOR:
			break; //Effect is displayed on respective switch case.
		default:
			if(skill_get_inf(skill_id)&INF_SELF_SKILL)
				clif_skill_nodamage(src,src,skill_id,skill_lv,1);
			else
				clif_skill_poseffect(src,skill_id,skill_lv,x,y,tick);
	}

	switch(skill_id)
	{
	case PR_BENEDICTIO:
		skill_area_temp[1] = src->id;
		i = skill_get_splash(skill_id, skill_lv);
		map_foreachinarea(skill_area_sub,
			src->m, x-i, y-i, x+i, y+i, BL_PC,
			src, skill_id, skill_lv, tick, flag|BCT_ALL|1,
			skill_castend_nodamage_id);
		map_foreachinarea(skill_area_sub,
			src->m, x-i, y-i, x+i, y+i, BL_CHAR,
			src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1,
			skill_castend_damage_id);
		break;

	case BS_HAMMERFALL:
		i = skill_get_splash(skill_id, skill_lv);
		map_foreachinarea (skill_area_sub,
			src->m, x-i, y-i, x+i, y+i, BL_CHAR,
			src, skill_id, skill_lv, tick, flag|BCT_ENEMY|2,
			skill_castend_nodamage_id);
		break;

	case HT_DETECTING:
		i = skill_get_splash(skill_id, skill_lv);
		map_foreachinarea( status_change_timer_sub,
			src->m, x-i, y-i, x+i,y+i,BL_CHAR,
			src,NULL,SC_SIGHT,tick);
		skill_reveal_trap_inarea(src, i, x, y);
		break;

	case SO_ARRULLO:
		i = skill_get_splash(skill_id, skill_lv);
		map_foreachinarea(skill_area_sub, src->m, x - i, y - i, x + i, y + i, BL_CHAR, src, skill_id, skill_lv, tick, flag | BCT_ENEMY | 1, skill_castend_nodamage_id);
		break;

	//Offensive Ground Targeted Splash AoE's.
	case RK_WINDCUTTER:
		{
			int dir = map_calc_dir(src, x, y);
			struct s_skill_nounit_layout *layout = skill_get_nounit_layout(skill_id,skill_lv,src,x,y,dir);
			clif_skill_damage(src,src,tick, status_get_amotion(src), 0, -30000, 1, skill_id, skill_lv, 6);
			for( i = 0; i < layout->count; i++ )
				map_foreachincell(skill_area_sub, src->m, x+layout->dx[i], y+layout->dy[i], BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY,skill_castend_damage_id);
		}
		break;

	case AC_SHOWER:
	case MA_SHOWER:
	case NC_COLDSLOWER:
	case RK_DRAGONBREATH:
	case WM_GREAT_ECHO:
	case WM_SOUND_OF_DESTRUCTION:
	case SR_RIDEINLIGHTNING:
	case KO_BAKURETSU:
	case KO_MUCHANAGE:
	case KO_HUUMARANKA:
	case RK_DRAGONBREATH_WATER:
		// Cast center might be relevant later (e.g. for knockback direction)
		skill_area_temp[4] = x;
		skill_area_temp[5] = y;
		i = skill_get_splash(skill_id, skill_lv);
		if ( skill_id == KO_MUCHANAGE )// Count the number of enemys in the AoE to prepare for the diving of damage.
			skill_area_temp[0] = map_foreachinarea(skill_area_sub,src->m,x-i,y-i,x+i,y+i, BL_CHAR, src, skill_id, skill_lv, tick, BCT_ENEMY, skill_area_sub_count);
		if (battle_config.skill_wall_check)
			map_foreachinshootarea(skill_area_sub, src->m, x - i, y - i, x + i, y + i, splash_target(src), src, skill_id, skill_lv, tick, flag | BCT_ENEMY | 1, skill_castend_damage_id);
		else
			map_foreachinarea(skill_area_sub, src->m, x - i, y - i, x + i, y + i, BL_CHAR, src, skill_id, skill_lv, tick, flag | BCT_ENEMY | 1, skill_castend_damage_id);
		break;

	case GC_POISONSMOKE:
		if( !(sc && sc->data[SC_POISONINGWEAPON]) )
		{
			if( sd )
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_GC_POISONINGWEAPON,0,0);
			return 0;
		}
		clif_skill_damage(src,src,tick,status_get_amotion(src),0,-30000,1,skill_id,skill_lv,6);
		skill_unitsetting(src, skill_id, skill_lv, x, y, flag);
 		break;

	case AB_EPICLESIS:
		if( sg = skill_unitsetting(src, skill_id, skill_lv, x, y, 0) ){
			i = sg->unit->range;
			map_foreachinarea(skill_area_sub, src->m, x - i, y - i, x + i, y + i, BL_CHAR, src, ALL_RESURRECTION, 1, tick, flag|BCT_NOENEMY|1,skill_castend_nodamage_id);
		}
		break;

	case WL_COMET:
		if (sc)
		{
			sc->comet_x = x;
			sc->comet_y = y;
		}//Splash is used to check the distance between the enemy and the center of the AoE to see what damage zone the target is in.
		i = skill_get_splash(skill_id, skill_lv);
		map_foreachinarea(skill_area_sub, src->m, x - i, y - i, x + i, y + i, BCT_ENEMY);
		flag |= 1;
		skill_unitsetting(src, skill_id, skill_lv, x, y, 0);
		break;

	case WL_EARTHSTRAIN:
		{
			int i, wave = skill_lv + 4, dir = map_calc_dir(src,x,y);
			int sx = x, sy = y;

			for( i = 0; i < wave; i++ )
			{
				switch( dir )
				{
					case 0: case 1: case 7: sy = src->y + i; break;
					case 3: case 4: case 5: sy = src->y - i; break;
					case 2: sx = src->x - i; break;
					case 6: sx = src->x + i; break;
				}
				skill_addtimerskill(src,gettick() + (140 * i),0,sx,sy,skill_id,skill_lv,dir,flag&2);
			}
		}
		break;
	case RL_FIRE_RAIN:
		{
			signed char wave = 10;
			short cell_break_chance = 15 + 5 * skill_lv;
			int i, dir = map_calc_dir(src,x,y);
			int sx = x, sy = y;

			if ( rnd()%100 < cell_break_chance )
				flag = flag|8;

			for( i = 0; i < wave; i++ )
			{
				switch( dir )
				{
					case 0: case 1: case 7: sy = src->y + i; break;// North
					case 3: case 4: case 5: sy = src->y - i; break;// South
					case 2: sx = src->x - i; break;// West
					case 6: sx = src->x + i; break;// East
				}

				skill_addtimerskill(src, gettick() + (140 * i), 0, sx, sy, skill_id, skill_lv, dir, flag);
			}

			// For some reason the sound effect only plays on the damage packet.
			// Only the nodamage packet should trigger the amotion.
			clif_skill_damage(src, src, tick, 0, 0, -30000, 1, skill_id, skill_lv, 5);
			clif_skill_nodamage(src,src,skill_id,skill_lv,1);
		}
		break;

	case RA_DETONATOR:
		i = skill_get_splash(skill_id, skill_lv);
		map_foreachinarea(skill_detonator,src->m,x-i,y-i,x+i,y+i,BL_SKILL,src);
		clif_skill_damage(src,src,tick,status_get_amotion(src),0,-30000,1,skill_id,skill_lv,6);
		break;

	case NC_NEUTRALBARRIER:
	case NC_STEALTHFIELD:
		skill_clear_unitgroup(src); // To remove previous skills - cannot used combined
		if ((sg = skill_unitsetting(src, skill_id, skill_lv, src->x, src->y, 0)))
			sc_start2(src,skill_id == NC_NEUTRALBARRIER ? SC_NEUTRALBARRIER_MASTER : SC_STEALTHFIELD_MASTER,100,skill_lv,sg->group_id,skill_get_time(skill_id,skill_lv));
		break;

	case NC_SILVERSNIPER:
		{
			struct mob_data *md;

			md = mob_once_spawn_sub(src, src->m, x, y, status_get_name(src), MOBID_SILVERSNIPER, "", 0, AI_NONE);
			if(md)
			{
				md->master_id = src->id;
				md->special_state.ai = AI_FAW;
				if( md->deletetimer != INVALID_TIMER )
					delete_timer(md->deletetimer, mob_timer_delete);
				md->deletetimer = add_timer(gettick() + skill_get_time(skill_id, skill_lv), mob_timer_delete, md->bl.id, 0);
				mob_spawn(md);
			}
		}
		break;

	case NC_MAGICDECOY:
		if( sd ) clif_magicdecoy_list(sd,skill_lv,x,y);
 		break;

	case SC_FEINTBOMB:
		clif_skill_nodamage(src,src,skill_id,skill_lv,1);
		skill_unitsetting(src,skill_id,skill_lv,x,y,0); // Set bomb on current Position
		skill_blown(src,src,3*skill_lv,unit_getdir(src),0);
		//After back sliding, the player goes into hiding. Hiding level used is throught to be the learned level.
		sc_start(src, SC_HIDING, 100, (sd ? pc_checkskill(sd, TF_HIDING) : 10), skill_get_time(TF_HIDING, (sd ? pc_checkskill(sd, TF_HIDING) : 10)));
		break;

	case LG_OVERBRAND:
		{
			int dir = map_calc_dir(src, x, y);
			struct s_skill_nounit_layout  *layout;

			//The main ID LG_OVERBRAND takes action first to generate its AoE and deal damage.
			layout = skill_get_nounit_layout(skill_id,skill_lv,src,x,y,dir);
			for( i = 0; i < layout->count; i++ )
				map_foreachincell(skill_area_sub, src->m, x+layout->dx[i], y+layout->dy[i], BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY,skill_castend_damage_id);

			//The LG_OVERBRAND_BRANDISH ID is triggered right after LG_OVERBRAND, but is delayed by the casters ASPD value.
			// It then generates its AoE to deal damage and also knock back targets that were hit. Any target hitting a obstacle will receive damage from a third ID.

			layout = skill_get_nounit_layout(LG_OVERBRAND_BRANDISH, skill_lv, src, x, y, dir);
			for (i = 0; i < layout->count; i++)
				map_foreachincell(skill_area_sub, src->m, x+layout->dx[i], y+layout->dy[i], BL_CHAR, src, LG_OVERBRAND_BRANDISH, skill_lv, tick, flag|BCT_ENEMY,skill_castend_damage_id);
 		}
		break;

	case LG_RAYOFGENESIS:
		if (status_charge(src, status_get_max_hp(src) * 3 * skill_lv / 100,0))
		{
			i = skill_get_splash(skill_id, skill_lv);
			map_foreachinarea(skill_area_sub, src->m, x - i, y - i, x + i, y + i, BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_damage_id);
		}
		else if(sd)
			clif_skill_fail(sd, skill_id, USESKILL_FAIL, 0, 0);
		break;

	case WM_DOMINION_IMPULSE:
		i = skill_get_splash(skill_id, skill_lv);
		map_foreachinarea(skill_trigger_reverberation, src->m, x - i, y - i, x + i, y + i, BL_SKILL);
		break;

	case GN_CRAZYWEED:
		{
			int flag = 0, area = skill_get_splash(skill_id, skill_lv);
			short tmpx = 0, tmpy = 0, x1 = 0, y1 = 0;

			for( i = 0; i < 3 + (skill_lv>>1); i++ )
			{
				// Creates a random Cell in the Splash Area
				tmpx = x - area + rnd()%(area * 2 + 1);
				tmpy = y - area + rnd()%(area * 2 + 1);

				if( i > 0 )
					skill_addtimerskill(src,tick+i*250,0,tmpx,tmpy,GN_CRAZYWEED,skill_lv,(x1<<16)|y1,flag);

				x1 = tmpx;
				y1 = tmpy;
			}

			skill_addtimerskill(src,tick+i*250,0,tmpx,tmpy,GN_CRAZYWEED,skill_lv,-1,flag);
		}
		break;

	case GN_CRAZYWEED_ATK:
		{
			int dummy = 1;
			//Enable if any unique animation gets added to this skill ID in the future. [Rytech]
			//clif_skill_poseffect(src,skill_id,skill_lv,x,y,tick);
			i = skill_get_splash(skill_id, skill_lv);
			map_foreachinarea(skill_cell_overlap, src->m, x-i, y-i, x+i, y+i, BL_SKILL, skill_id, src);
			map_foreachinarea(skill_area_sub, src->m, x-i, y-i, x+i, y+i, BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_damage_id);
		}
		break;

	case GN_FIRE_EXPANSION:
		{
			int i;
			int aciddemocast = 5;//If player doesent know Acid Demonstration or knows level 5 or lower, effect 5 will cast level 5 Acid Demo.
			struct unit_data *ud = unit_bl2ud(src);
			if( !ud )
				break;
			for( i = 0; i < MAX_SKILLUNITGROUP && ud->skillunit[i]; i ++ )
			{
				if( ud->skillunit[i]->skill_id == GN_DEMONIC_FIRE &&
					distance_xy(x, y, ud->skillunit[i]->unit->bl.x, ud->skillunit[i]->unit->bl.y) < 3)
				{
					switch( skill_lv )
					{
						case 3:
							ud->skillunit[i]->unit_id = UNT_FIRE_EXPANSION_SMOKE_POWDER;
							clif_changetraplook(&ud->skillunit[i]->unit->bl, UNT_FIRE_EXPANSION_SMOKE_POWDER);
							break;
						case 4:
							ud->skillunit[i]->unit_id = UNT_FIRE_EXPANSION_TEAR_GAS;
							clif_changetraplook(&ud->skillunit[i]->unit->bl, UNT_FIRE_EXPANSION_TEAR_GAS);
							break;
						case 5:// If player knows a level of Acid Demonstration greater then 5, that level will be casted.
							if ( pc_checkskill(sd, CR_ACIDDEMONSTRATION) > 5 )
								aciddemocast = pc_checkskill(sd, CR_ACIDDEMONSTRATION);
							map_foreachinarea(skill_area_sub, src->m,
								ud->skillunit[i]->unit->bl.x - 2, ud->skillunit[i]->unit->bl.y - 2,
								ud->skillunit[i]->unit->bl.x + 2, ud->skillunit[i]->unit->bl.y + 2, BL_CHAR,
								src, CR_ACIDDEMONSTRATION, aciddemocast, tick, flag|BCT_ENEMY|1|SD_LEVEL, skill_castend_damage_id);
							skill_delunit(ud->skillunit[i]->unit);
							break;
						default:
							ud->skillunit[i]->unit->val2 = skill_lv;
							ud->skillunit[i]->unit->group->val2 = skill_lv;
 							break;
					}
				}
			}
		}
		break;

	case SO_FIREWALK:
	case SO_ELECTRICWALK:
		if( sc && sc->data[type] )
			status_change_end(src,type,INVALID_TIMER);
		clif_skill_nodamage(src, src ,skill_id, skill_lv,
			sc_start2(src, type, 100, skill_id, skill_lv, skill_get_time(skill_id, skill_lv)));
		break;

	case SA_VOLCANO:
	case SA_DELUGE:
	case SA_VIOLENTGALE:
	{	//Does not consumes if the skill is already active. [Skotlex]
		struct skill_unit_group *sg;
		if ((sg= skill_locate_element_field(src)) != NULL && ( sg->skill_id == SA_VOLCANO || sg->skill_id == SA_DELUGE || sg->skill_id == SA_VIOLENTGALE ))
		{
			if (sg->limit - DIFF_TICK(gettick(), sg->tick) > 0)
			{
				skill_unitsetting(src,skill_id,skill_lv,x,y,0);
				return 0; // not to consume items
			}
			else
				sg->limit = 0; //Disable it.
		}
		skill_unitsetting(src,skill_id,skill_lv,x,y,0);
		break;
	}
	case MG_SAFETYWALL:
	case MG_FIREWALL:
	case MG_THUNDERSTORM:
	case AL_PNEUMA:
	case WZ_FIREPILLAR:
	case WZ_QUAGMIRE:
	case WZ_VERMILION:
	case WZ_STORMGUST:
	case WZ_HEAVENDRIVE:
	case PR_SANCTUARY:
	case PR_MAGNUS:
	case CR_GRANDCROSS:
	case NPC_GRANDDARKNESS:
	case HT_SKIDTRAP:
	case MA_SKIDTRAP:
	case HT_LANDMINE:
	case MA_LANDMINE:
	case HT_ANKLESNARE:
	case HT_SHOCKWAVE:
	case HT_SANDMAN:
	case MA_SANDMAN:
	case HT_FLASHER:
	case HT_FREEZINGTRAP:
	case MA_FREEZINGTRAP:
	case HT_BLASTMINE:
	case HT_CLAYMORETRAP:
	case AS_VENOMDUST:
	case AM_DEMONSTRATION:
	case PF_FOGWALL:
	case PF_SPIDERWEB:
	case HT_TALKIEBOX:
	case WE_CALLPARTNER:
	case WE_CALLPARENT:
	case WE_CALLBABY:
	case SA_LANDPROTECTOR:
	case BD_LULLABY:
	case BD_RICHMANKIM:
	case BD_ETERNALCHAOS:
	case BD_DRUMBATTLEFIELD:
	case BD_RINGNIBELUNGEN:
	case BD_ROKISWEIL:
	case BD_INTOABYSS:
	case BD_SIEGFRIED:
	case BA_DISSONANCE:
	case BA_POEMBRAGI:
	case BA_WHISTLE:
	case BA_ASSASSINCROSS:
	case BA_APPLEIDUN:
	case DC_UGLYDANCE:
	case DC_HUMMING:
	case DC_DONTFORGETME:
	case DC_FORTUNEKISS:
	case DC_SERVICEFORYOU:
	case CG_MOONLIT:
	case GS_DESPERADO:
	case NJ_KAENSIN:
	case NJ_BAKUENRYU:
	case NJ_SUITON:
	case NJ_HYOUSYOURAKU:
	case NJ_RAIGEKISAI:
	case NJ_KAMAITACHI:
	case NPC_EVILLAND:
	case RA_ELECTRICSHOCKER:
	case RA_CLUSTERBOMB:
	case RA_MAGENTATRAP:
	case RA_COBALTTRAP:
	case RA_MAIZETRAP:
	case RA_VERDURETRAP:
	case RA_FIRINGTRAP:
	case RA_ICEBOUNDTRAP:
	case SC_MANHOLE:
	case SC_DIMENSIONDOOR:
	case WM_REVERBERATION:
	case WM_SEVERE_RAINSTORM:
	case WM_POEMOFNETHERWORLD:
	case SO_PSYCHIC_WAVE:
	case SO_VACUUM_EXTREME:
	case GN_WALLOFTHORN:
	case GN_THORNS_TRAP:
	case GN_DEMONIC_FIRE:
	case GN_FIRE_EXPANSION_SMOKE_POWDER:
	case GN_FIRE_EXPANSION_TEAR_GAS:
	case GN_HELLS_PLANT:
	case SO_EARTHGRAVE:
	case SO_DIAMONDDUST:
	case SO_FIRE_INSIGNIA:
	case SO_WATER_INSIGNIA:
	case SO_WIND_INSIGNIA:
	case SO_EARTH_INSIGNIA:
	case RL_B_TRAP:
	case RL_HAMMER_OF_GOD:
	case SJ_BOOKOFCREATINGSTAR:
	case LG_KINGS_GRACE:
	case KO_ZENKAI:
	case MH_POISON_MIST:
	case MH_XENO_SLASHER:
	case MH_STEINWAND:
	case MH_LAVA_SLIDE:
	case MH_VOLCANIC_ASH:
	case NPC_VENOMFOG:
		flag|=1;//Set flag to 1 to prevent deleting ammo (it will be deleted on group-delete).
	case GS_GROUNDDRIFT: //Ammo should be deleted right away.
		skill_unitsetting(src,skill_id,skill_lv,x,y,0);
		break;
	
	case WZ_ICEWALL:
		flag |= 1;
		if (skill_unitsetting(src, skill_id, skill_lv, x, y, 0))
			clif_skill_poseffect(src, skill_id, skill_lv, x, y, tick);
		break;

	case EL_FIRE_MANTLE:
	case EL_WATER_BARRIER:
	case EL_ZEPHYR:
	case EL_POWER_OF_GAIA:
		flag|=1;
		if ( sd )
			skill_unitsetting(src,skill_id,skill_lv,src->x,src->y,0);
		else
			skill_unitsetting(src,skill_id,skill_lv,ed->master->bl.x,ed->master->bl.y,0);
		break;

	case SU_CN_POWDERING:
	case SU_NYANGGRASS:
		flag|=1;
		skill_unitsetting(src,skill_id,skill_lv,x,y,0);
		if ( sd && pc_checkskill(sd, SU_SPIRITOFLAND) > 0 )
		{
			if ( skill_id == SU_CN_POWDERING )
				sc_start(src,SC_SPIRITOFLAND_PERFECTDODGE,100,skill_lv,skill_get_time(SU_SPIRITOFLAND,1));
			else
				sc_start(src,SC_SPIRITOFLAND_MATK,100,skill_lv,skill_get_time(SU_SPIRITOFLAND,1));
		}
		break;

	case KO_MAKIBISHI:
		{
			short area = skill_get_splash(skill_id, skill_lv);
			short castx = x, casty = y;// Player's location on skill use.
			short placex = 0, placey = 0;// Where to place the makibishi.
			unsigned char attempts = 0;// Number of attempts to place the makibishi. Prevents infinite loops.

			for( i = 0; i < 2 + skill_lv; i++ )
			{
				// Select a random cell.
				placex = x - area + rnd()%(area * 2 + 1);
				placey = y - area + rnd()%(area * 2 + 1);

				// Only place the makibishi if its not on the cell where the player is standing and its not on top of another.
				if ( !((placex == castx && placey == casty) || skill_check_unit_range(src,placex,placey,skill_id,skill_lv)) )
					skill_unitsetting(src,skill_id,skill_lv,placex,placey,0);
				else if ( attempts < 20 )// 20 attempts is enough. Very rarely do we hit this limit.
				{// If it tried to place on a spot where the player was standing or where another makibishi is, make another attempt.
					attempts++;
					i--;// Mark down. The makibishi placement was unsuccessful.
				}
			}
		}
		break;

	case NC_MAGMA_ERUPTION:
		i = skill_get_splash(skill_id, skill_lv);
		map_foreachinarea (skill_area_sub, src->m, x-i, y-i, x+i, y+i, BL_CHAR, src, skill_id, skill_lv, tick, flag|BCT_ENEMY|1, skill_castend_damage_id);
		skill_unitsetting(src,skill_id,skill_lv,x,y,0);
		break;
	case SC_CONFUSIONPANIC:
	case SC_MAELSTROM:
	case SC_BLOODYLUST:
		if((skill_id == SC_CONFUSIONPANIC || skill_id == SC_MAELSTROM || (skill_id == SC_BLOODYLUST && battle_config.allow_bloody_lust_on_warp == 0)) &&
			npc_check_areanpc(1,src->m,x,y,skill_get_splash(skill_id, skill_lv)))
		{// Can't be placed near warp NPC's.
			if (sd)
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_POS,0,0);
			return 0;
		}

		if ( (skill_id == SC_CONFUSIONPANIC && rnd()%100 < (35 + 15 * skill_lv)) || skill_id == SC_MAELSTROM || skill_id == SC_BLOODYLUST )
		{// Chaos Panic Has a success chance for placing the AoE.
			clif_skill_poseffect(src,skill_id,skill_lv,x,y,tick);
			skill_unitsetting(src,skill_id,skill_lv,x,y,0);
		}
		else
		{
			if (sd)
				clif_skill_fail(sd,skill_id,USESKILL_FAIL,0,0);
			return 0;
		}
		break;
	// Group these skills under their own flags so reusing the skill replaces its own field.
	case SO_CLOUD_KILL:
	case SO_WARMER:
		flag|=(skill_id == SO_CLOUD_KILL)?4:8;
		skill_unitsetting(src,skill_id,skill_lv,x,y,0);
		break;
	case RG_GRAFFITI:			/* Graffiti [Valaris] */
		skill_clear_unitgroup(src);
		skill_unitsetting(src,skill_id,skill_lv,x,y,0);
		flag|=1;
		break;
	case HP_BASILICA:
		if (sc->data[SC_BASILICA]) {
			status_change_end(src, SC_BASILICA, INVALID_TIMER); // Cancel Basilica and return so requirement isn't consumed again
			return 0;
		}
		else { // Create Basilica. Start SC on caster. Unit timer start SC on others.
			if (map_getcell(src->m, x, y, CELL_CHKLANDPROTECTOR)) {
				clif_skill_fail(sd, skill_id, USESKILL_FAIL, 0, 0);
				return 0;
			}
			skill_clear_unitgroup(src);
			skill_unitsetting(src, skill_id, skill_lv, x, y, 0);
			flag|=1;
		}
		break;
	case CG_HERMODE:
		skill_clear_unitgroup(src);
		if ((sg = skill_unitsetting(src,skill_id,skill_lv,x,y,0)))
			sc_start4(src,SC_DANCING,100,
				skill_id,0,skill_lv,sg->group_id,skill_get_time(skill_id,skill_lv));
		flag|=1;
		break;
	case RG_CLEANER: // [Valaris]
		i = skill_get_splash(skill_id, skill_lv);
		map_foreachinarea(skill_graffitiremover,src->m,x-i,y-i,x+i,y+i,BL_SKILL);
		break;
	case WZ_METEOR:
	case SU_CN_METEOR:
		{
			int area = skill_get_splash(skill_id, skill_lv), n;
			short tmpx = 0, tmpy = 0, x1 = 0, y1 = 0, drop_timer = 0;
			signed char drop_count = 0;

			if ( skill_id == WZ_METEOR )
			{
				drop_count = 2 + (skill_lv>>1);
				drop_timer = 1000;
			}
			else
			{// SU_CN_METEOR
				drop_count = 2 + skill_lv;
				drop_timer = 700;
				if( sd && (n = pc_search_inventory(sd,ITEMID_CATNIP_FRUIT)) >= 0 )
				{// If catnip is in the caster's inventory, switch to alternate skill ID to give a chance to curse.
					pc_delitem(sd,n,1,0,1,LOG_TYPE_CONSUME);
					skill_id = SU_CN_METEOR2;
				}
			}

			for (i = 0; i < drop_count; i++)
			{
				// Creates a random Cell in the Splash Area
				tmpx = x - area + rnd()%(area * 2 + 1);
				tmpy = y - area + rnd()%(area * 2 + 1);

				if (i == 0 && path_search_long(NULL, src->m, src->x, src->y, tmpx, tmpy, CELL_CHKWALL)
					&& !map_getcell(src->m, tmpx, tmpy, CELL_CHKLANDPROTECTOR))
					clif_skill_poseffect(src,skill_id,skill_lv,tmpx,tmpy,tick);

				if( i > 0 )
					skill_addtimerskill(src, tick + i * drop_timer, 0, tmpx, tmpy, skill_id, skill_lv, (x1 << 16) | y1, flag & 2); //Only pass the Magic Power flag

				x1 = tmpx;
				y1 = tmpy;
			}

			skill_addtimerskill(src,tick+i*drop_timer,0,tmpx,tmpy,skill_id,skill_lv,-1,flag&2);

			// Switch back to original skill ID in case there's more to be done beyond here.
			if ( skill_id == SU_CN_METEOR2 )
				skill_id = SU_CN_METEOR;

			if (skill_id == SU_CN_METEOR && sd && pc_checkskill(sd, SU_SPIRITOFLAND) > 0)
				sc_start(src, SC_SPIRITOFLAND_AUTOCAST, 100, skill_lv, skill_get_time(SU_SPIRITOFLAND, 1));
		}
		break;

	case AL_WARP:
		if(sd)
		{
			clif_skill_warppoint(sd, skill_id, skill_lv, sd->status.save_point.map,
				(skill_lv >= 2) ? sd->status.memo_point[0].map : 0,
				(skill_lv >= 3) ? sd->status.memo_point[1].map : 0,
				(skill_lv >= 4) ? sd->status.memo_point[2].map : 0
			);
		}
		return 0; // not to consume item.

	case MO_BODYRELOCATION:
		if (unit_movepos(src, x, y, 2, 1)) {
			#if PACKETVER >= 20120410
				clif_fast_movement(src, src->x, src->y);
			#else
				clif_skill_poseffect(src,skill_id,skill_lv,src->x,src->y,tick);
			#endif
			if (sd) skill_blockpc_start (sd, MO_EXTREMITYFIST, 2000);
		}
		break;
	case NJ_SHADOWJUMP:
		if (skill_check_unit_movepos(5, src, x, y, 1, 0)) //You don't move on GVG grounds.
			clif_blown(src);
		status_change_end(src, SC_HIDING, INVALID_TIMER);
		break;

	case RL_FALLEN_ANGEL:
	case SU_LOPE:
		{
			if( map[src->m].flag.noteleport && !(map[src->m].flag.battleground || map_flag_gvg2(src->m) ))
			{
				x = src->x;
				y = src->y;
			}

			if ( skill_id == RL_FALLEN_ANGEL )
			{// Note: There's supposed to be a animation with feathers appearing where you land. Can't trigger it. [Rytech]
				clif_skill_nodamage(src,src,RL_FALLEN_ANGEL,skill_lv,1);
				sc_start(src, type, 100, 1, 2000);// Doubles the damage of GS_DESPERADO.
				// Note: Because of how Desperado's mechanics work and how its coded, its
				// not possible to just end it after using it once. Doing so will make the
				// double damage end on the first hit. All I can do is let the status run
				// the full 2 seconds.
			}
			else
				clif_skill_nodamage(src,src,SU_LOPE,skill_lv,1);
			if(!map_count_oncell(src->m,x,y,BL_PC|BL_NPC|BL_MOB,0) && map_getcell(src->m,x,y,CELL_CHKREACH))
			{
				clif_slide(src,x,y);
				unit_movepos(src, x, y, 1, 0);
			}
		}
		break;

	case AM_SPHEREMINE:
	case AM_CANNIBALIZE:
		{
			int summons[5] = { MOBID_G_MANDRAGORA, MOBID_G_HYDRA, MOBID_G_FLORA, MOBID_G_PARASITE, MOBID_G_GEOGRAPHER };
			int class_ = skill_id == AM_SPHEREMINE ? MOBID_MARINE_SPHERE : summons[skill_lv - 1];
			struct mob_data *md;

			// Correct info, don't change any of this! [celest]
			md = mob_once_spawn_sub(src, src->m, x, y, status_get_name(src),class_,"",0,AI_NONE);
			if (md) {
				md->master_id = src->id;
				md->special_state.ai = skill_id==AM_SPHEREMINE?AI_SPHERE:AI_FLORA;
				if( md->deletetimer != INVALID_TIMER )
					delete_timer(md->deletetimer, mob_timer_delete);
				md->deletetimer = add_timer (gettick() + skill_get_time(skill_id,skill_lv), mob_timer_delete, md->bl.id, 0);
				mob_spawn (md); //Now it is ready for spawning.
			}
		}
		break;

	// Slim Pitcher [Celest]
	case CR_SLIMPITCHER:
		if (sd) {
			int i = 0, j = 0;
			struct skill_condition require = skill_get_requirement(sd, skill_id, skill_lv);
			i = skill_lv%11 - 1;
			j = pc_search_inventory(sd, require.itemid[i]);
			if( j < 0 || require.itemid[i] <= 0 || sd->inventory_data[j] == NULL || sd->inventory.u.items_inventory[j].amount < require.amount[i])
			{
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				return 1;
			}
			potion_flag = 1;
			potion_hp = 0;
			potion_sp = 0;
			run_script(sd->inventory_data[j]->script,0,sd->bl.id,0);
			potion_flag = 0;
			//Apply skill bonuses
			i = pc_checkskill(sd,CR_SLIMPITCHER)*10
				+ pc_checkskill(sd,AM_POTIONPITCHER)*10
				+ pc_checkskill(sd,AM_LEARNINGPOTION)*5
				+ pc_skillheal_bonus(sd, skill_id);

			potion_hp = potion_hp * (100+i)/100;
			potion_sp = potion_sp * (100+i)/100;

			if(potion_hp > 0 || potion_sp > 0) {
				i = skill_get_splash(skill_id, skill_lv);
				map_foreachinarea(skill_area_sub,
					src->m,x-i,y-i,x+i,y+i,BL_CHAR,
					src,skill_id,skill_lv,tick,flag|BCT_PARTY|BCT_GUILD|1,
					skill_castend_nodamage_id);
			}
		} else {
			int i = skill_get_itemid(skill_id, skill_lv);
			struct item_data *item;
			item = itemdb_search(i);
			potion_flag = 1;
			potion_hp = 0;
			potion_sp = 0;
			run_script(item->script,0,src->id,0);
			potion_flag = 0;
			i = skill_get_max(CR_SLIMPITCHER)*10;

			potion_hp = potion_hp * (100+i)/100;
			potion_sp = potion_sp * (100+i)/100;

			if(potion_hp > 0 || potion_sp > 0) {
				i = skill_get_splash(skill_id, skill_lv);
				map_foreachinarea(skill_area_sub,
					src->m,x-i,y-i,x+i,y+i,BL_CHAR,
					src,skill_id,skill_lv,tick,flag|BCT_PARTY|BCT_GUILD|1,
						skill_castend_nodamage_id);
			}
		}
		break;

	case HW_GANBANTEIN:
		if (rnd()%100 < 80) {
			int dummy = 1;
			clif_skill_poseffect(src,skill_id,skill_lv,x,y,tick);
			i = skill_get_splash(skill_id, skill_lv);
			map_foreachinarea(skill_cell_overlap, src->m, x-i, y-i, x+i, y+i, BL_SKILL, HW_GANBANTEIN, src);
		} else {
			if (sd) clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 1;
		}
		break;

	case HW_GRAVITATION:
		if ((sg = skill_unitsetting(src,skill_id,skill_lv,x,y,0)))
			sc_start4(src,type,100,skill_lv,0,BCT_SELF,sg->group_id,skill_get_time(skill_id,skill_lv));
		flag|=1;
		break;

	// Plant Cultivation [Celest]
	case CR_CULTIVATION:
		if (sd) {
			if( map_count_oncell(src->m,x,y,BL_CHAR,0) > 0 )
			{
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				return 1;
			}
			clif_skill_poseffect(src,skill_id,skill_lv,x,y,tick);
			if (rnd()%100 < 50) {
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
			} else {
				TBL_MOB* md = NULL;
				int i, mob_id;

				if (skill_lv == 1)
					mob_id = MOBID_BLACK_MUSHROOM + rnd() % 2;
				else {
					int rand_val = rnd() % 100;

					if (rand_val < 30)
						mob_id = MOBID_GREEN_PLANT;
					else if (rand_val < 55)
						mob_id = MOBID_RED_PLANT;
					else if (rand_val < 80)
						mob_id = MOBID_YELLOW_PLANT;
					else if (rand_val < 90)
						mob_id = MOBID_WHITE_PLANT;
					else if (rand_val < 98)
						mob_id = MOBID_BLUE_PLANT;
					else
						mob_id = MOBID_SHINING_PLANT;
				}

				md = mob_once_spawn_sub(src, src->m, x, y, "--ja--", mob_id, "", 0, AI_NONE);
				if (!md)
					break;
				if ((i = skill_get_time(skill_id, skill_lv)) > 0)
				{
					if( md->deletetimer != INVALID_TIMER )
						delete_timer(md->deletetimer, mob_timer_delete);
					md->deletetimer = add_timer (tick + i, mob_timer_delete, md->bl.id, 0);
				}
				mob_spawn (md);
			}
		}
		break;

	case SG_SUN_WARM:
	case SG_MOON_WARM:
	case SG_STAR_WARM:
		skill_clear_unitgroup(src);
		if ((sg = skill_unitsetting(src,skill_id,skill_lv,src->x,src->y,0)))
			sc_start4(src,type,100,skill_lv,0,0,sg->group_id,skill_get_time(skill_id,skill_lv));
		flag|=1;
		break;

	case LG_BANDING:
		if ( sc && sc->data[SC_BANDING] )
			skill_clear_unitgroup(src);
		else if ((sg = skill_unitsetting(src,skill_id,skill_lv,src->x,src->y,0)))
			sc_start4(src,SC_BANDING,100,skill_lv,0,0,sg->group_id,skill_get_time(skill_id,skill_lv));
		break;

	case PA_GOSPEL:
		if (sce && sce->val4 == BCT_SELF)
		{
			status_change_end(src, SC_GOSPEL, INVALID_TIMER);
			return 0;
		}
		else
	  	{
			sg = skill_unitsetting(src,skill_id,skill_lv,src->x,src->y,0);
			if (!sg) break;
			if (sce)
				status_change_end(src, type, INVALID_TIMER); //Was under someone else's Gospel. [Skotlex]
			sc_start4(src,type,100,skill_lv,0,sg->group_id,BCT_SELF,skill_get_time(skill_id,skill_lv));
			clif_skill_poseffect(src, skill_id, skill_lv, 0, 0, tick); // PA_GOSPEL music packet
		}
		break;
	case NJ_TATAMIGAESHI:
		if (skill_unitsetting(src,skill_id,skill_lv,src->x,src->y,0))
			sc_start(src,type,100,skill_lv,skill_get_time2(skill_id,skill_lv));
		break;

	case AM_RESURRECTHOMUN:	//[orn]
		if (sd)
		{
			if (!hom_ressurect(sd, 20*skill_lv, x, y))
			{
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				break;
			}
		}
		break;

	case MH_SUMMON_LEGION:
		{
			short summons[5] = { MOBID_S_HORNET, MOBID_S_GIANT_HORNET, MOBID_S_GIANT_HORNET, MOBID_S_LUCIOLA_VESPA, MOBID_S_LUCIOLA_VESPA };
			short class_ = (skill_lv>5)?MOBID_S_LUCIOLA_VESPA:summons[skill_lv-1];
			unsigned char summon_count = (5+skill_lv)/2;
			struct mob_data *md;

			for (i=0; i<summon_count; i++)
			{
				md = mob_once_spawn_sub(src, src->m, x, y, status_get_name(src),class_,"",0,AI_NONE);
				if (md)
				{
					md->master_id = src->id;
					md->special_state.ai = 3;
					if( md->deletetimer != INVALID_TIMER )
						delete_timer(md->deletetimer, mob_timer_delete);
					md->deletetimer = add_timer (gettick() + skill_get_time(skill_id,skill_lv), mob_timer_delete, md->bl.id, 0);
					mob_spawn (md);
				}
			}
		}

	default:
		ShowWarning("skill_castend_pos2: Unknown skill used:%d\n",skill_id);
		return 1;
	}

	if( sd )
	{// ensure that the skill last-cast tick is recorded
		tick = gettick();
		switch (skill_id) {
			//These skill don't call skill_attack right away and allow to cast a second spell before the first skill deals damage
		case WZ_JUPITEL:
		case WZ_WATERBALL:
			//Only allow the double-cast trick every 2000ms to prevent hacks
			if (DIFF_TICK(tick, sd->canskill_tick) > 2000) {
				sd->ud.canact_tick = tick;
				sd->canskill_tick = tick - 2000 + TIMERSKILL_INTERVAL;
				break;
			}
			//Fall through
		default:
			sd->canskill_tick = tick;
			break;
		}

		if( sd->state.arrow_atk && !(flag&1) ) //Consume arrow if a ground skill was not invoked. [Skotlex]
			battle_consume_ammo(sd, skill_id, skill_lv);

		// perform skill requirement consumption
		skill_consume_requirement(sd,skill_id,skill_lv,2);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_castend_map (struct map_session_data *sd, short skill_id, const char *map)
{
	nullpo_ret(sd);

//Simplify skill_failed code.
#define skill_failed(sd) { sd->menuskill_id = sd->menuskill_val = 0; }
	if(skill_id != sd->menuskill_id)
		return 0;

	if( sd->bl.prev == NULL || pc_isdead(sd) ) {
		skill_failed(sd);
		return 0;
	}

	if( (sd->sc.opt1 && sd->sc.opt1 != OPT1_BURNING) || sd->sc.option&OPTION_HIDE ) {
		skill_failed(sd);
		return 0;
	}
	if (sd->sc.count && (//Note: If any of these status's are active, any skill you use will fail. So be careful when adding new status's here.
		sd->sc.data[SC_SILENCE] ||
		sd->sc.data[SC_ROKISWEIL] ||
		sd->sc.data[SC_AUTOCOUNTER] ||
		sd->sc.data[SC_STEELBODY] ||
		sd->sc.data[SC_DANCING] ||
		sd->sc.data[SC_BERSERK] ||
		sd->sc.data[SC_BASILICA] ||
		sd->sc.data[SC_MARIONETTE] ||
		sd->sc.data[SC_DEATHBOUND] ||
		//sd->sc.data[SC_STASIS] ||//Not sure if this is needed. Does as it should without it, but leaving here for now. [Rytech]
		sd->sc.data[SC_DEEPSLEEP] ||
		sd->sc.data[SC_CRYSTALIZE] ||
		sd->sc.data[SC__MANHOLE] ||
		sd->sc.data[SC_CURSEDCIRCLE_TARGET] ||
		sd->sc.data[SC_HEAT_BARREL_AFTER] ||
		sd->sc.data[SC_NOVAEXPLOSING] ||
		sd->sc.data[SC_GRAVITYCONTROL] ||
		sd->sc.data[SC_FLASHCOMBO] ||
		sd->sc.data[SC_KINGS_GRACE] ||
		sd->sc.data[SC_ALL_RIDING]
	 )) {
		skill_failed(sd);
		return 0;
	}

	pc_stop_attack(sd);

	if(battle_config.skill_log && battle_config.skill_log&BL_PC)
		ShowInfo("PC %d skill castend skill =%d map=%s\n",sd->bl.id,skill_id,map);

	if(strcmp(map,"cancel")==0) {
		skill_failed(sd);
		return 0;
	}

	switch(skill_id)
	{
	case AL_TELEPORT:
	case ALL_ODINS_RECALL:
		//The storage window is closed automatically by the client when there's
		//any kind of map change, so we need to restore it automatically.
		if(strcmp(map,"Random")==0)
			pc_randomwarp(sd,CLR_TELEPORT);
		else if (sd->menuskill_val > 1 || skill_id == ALL_ODINS_RECALL) //Need lv2 to be able to warp here.
			pc_setpos(sd,sd->status.save_point.map,sd->status.save_point.x,sd->status.save_point.y,CLR_TELEPORT);

		clif_refresh_storagewindow(sd);
		break;

	case AL_WARP:
		{
			const struct point *p[4];
			struct skill_unit_group *group;
			int i, lv, wx, wy;
			int maxcount=0;
			int x,y;
			unsigned short mapindex;

			mapindex  = mapindex_name2id((char*)map);
			if(!mapindex) { //Given map not found?
				clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
				map_freeblock_unlock();
				skill_failed(sd);
				return 0;
			}
			p[0] = &sd->status.save_point;
			p[1] = &sd->status.memo_point[0];
			p[2] = &sd->status.memo_point[1];
			p[3] = &sd->status.memo_point[2];

			if((maxcount = skill_get_maxcount(skill_id, sd->menuskill_val)) > 0) {
				for(i=0;i<MAX_SKILLUNITGROUP && sd->ud.skillunit[i] && maxcount;i++) {
					if(sd->ud.skillunit[i]->skill_id == skill_id)
						maxcount--;
				}
				if(!maxcount) {
					clif_skill_fail(sd,skill_id,USESKILL_FAIL_LEVEL,0,0);
					skill_failed(sd);
					return 0;
				}
			}

			lv = sd->skillitem==skill_id?sd->skillitemlv:pc_checkskill(sd,skill_id);
			wx = sd->menuskill_val>>16;
			wy = sd->menuskill_val&0xffff;

			if( lv <= 0 ) return 0;
			if( lv > 4 ) lv = 4; // crash prevention

			// check if the chosen map exists in the memo list
			ARR_FIND( 0, lv, i, mapindex == p[i]->map );
			if( i < lv ) {
				x=p[i]->x;
				y=p[i]->y;
			} else {
				skill_failed(sd);
				return 0;
			}

			if(!skill_check_condition_castend(sd, sd->menuskill_id, lv))
			{  // This checks versus skill_id/skill_lv...
				skill_failed(sd);
				return 0;
			}

			skill_consume_requirement(sd,sd->menuskill_id,lv,2);
			sd->skillitem = sd->skillitemlv = sd->skillitem_keep_requirement = 0; // Clear data that's skipped in 'skill_castend_pos' [Inkfish]

			if((group=skill_unitsetting(&sd->bl,skill_id,lv,wx,wy,0))==NULL) {
				skill_failed(sd);
				return 0;
			}

			group->val1 = (group->val1 << 16) | (short)0;
			// record the destination coordinates
			group->val2 = (x<<16)|y;
			group->val3 = mapindex;
		}
		break;
	}

	sd->menuskill_id = sd->menuskill_val = 0;
	return 0;
#undef skill_failed
}

/// transforms 'target' skill unit into dissonance (if conditions are met)
static int skill_dance_overlap_sub(struct block_list* bl, va_list ap)
{
	struct skill_unit* target = (struct skill_unit*)bl;
	struct skill_unit* src = va_arg(ap, struct skill_unit*);
	int flag = va_arg(ap, int);

	if (src == target)
		return 0;
	if (!target->group || !(target->group->state.song_dance&0x1))
		return 0;
	if (!(target->val2 & src->val2 & ~UF_ENSEMBLE)) //They don't match (song + dance) is valid.
		return 0;

	if (flag) //Set dissonance
		target->val2 |= UF_ENSEMBLE; //Add ensemble to signal this unit is overlapping.
	else //Remove dissonance
		target->val2 &= ~UF_ENSEMBLE;

	skill_getareachar_skillunit_visibilty(target, AREA);

	return 1;
}

//Does the song/dance overlapping -> dissonance check. [Skotlex]
//When flag is 0, this unit is about to be removed, cancel the dissonance effect
//When 1, this unit has been positioned, so start the cancel effect.
int skill_dance_overlap(struct skill_unit* unit, int flag)
{
	if (!unit || !unit->group || !(unit->group->state.song_dance&0x1))
		return 0;
	if (!flag && !(unit->val2&UF_ENSEMBLE))
		return 0; //Nothing to remove, this unit is not overlapped.

	if (unit->val1 != unit->group->skill_id)
	{	//Reset state
		unit->val1 = unit->group->skill_id;
		unit->val2 &= ~UF_ENSEMBLE;
	}

	return map_foreachincell(skill_dance_overlap_sub, unit->bl.m,unit->bl.x,unit->bl.y,BL_SKILL, unit,flag);
}

/*==========================================
 * Converts this group information so that it is handled as a Dissonance or Ugly Dance cell.
 * Flag: 0 - Convert, 1 - Revert.
 * TODO: This should be completely removed later and rewritten
 * 	 The entire execution of the overlapping songs instances is dirty and hacked together
 *	 Overlapping cells should be checked on unit entry, not infinitely loop checked causing 1000's of executions a song/dance
 *------------------------------------------*/
static bool skill_dance_switch(struct skill_unit* unit, int flag)
{
	static int prevflag = 1;  // by default the backup is empty
	static struct skill_unit_group backup;
	struct skill_unit_group* group;

	if (unit == NULL || (group = unit->group) == NULL)
		return false;

	// val2&UF_ENSEMBLE is a hack to indicate dissonance
	if (!((group->state.song_dance & 0x1) && (unit->val2&UF_ENSEMBLE)))
		return false;

	if( flag == prevflag )
	{// protection against attempts to read an empty backup / write to a full backup
		ShowError("skill_dance_switch: Attempted to %s (skill_id=%d, skill_lv=%d, src_id=%d).\n",
			flag ? "read an empty backup" : "write to a full backup",
			group->skill_id, group->skill_lv, group->src_id);
		return false;
	}
	prevflag = flag;

	if( !flag )
	{	//Transform
		int skill_id = unit->val2&UF_SONG ? BA_DISSONANCE : DC_UGLYDANCE;

		// backup
		backup.skill_id    = group->skill_id;
		backup.skill_lv    = group->skill_lv;
		backup.unit_id     = group->unit_id;
		backup.target_flag = group->target_flag;
		backup.bl_flag     = group->bl_flag;
		backup.interval    = group->interval;

		// replace
		group->skill_id    = skill_id;
		group->skill_lv    = 1;
		group->unit_id     = skill_get_unit_id(skill_id,0);
		group->target_flag = skill_get_unit_target(skill_id);
		group->bl_flag     = skill_get_unit_bl_target(skill_id);
		group->interval    = skill_get_unit_interval(skill_id);
	}
	else
	{	//Restore
		group->skill_id    = backup.skill_id;
		group->skill_lv    = backup.skill_lv;
		group->unit_id     = backup.unit_id;
		group->target_flag = backup.target_flag;
		group->bl_flag     = backup.bl_flag;
		group->interval    = backup.interval;
	}

	return true;
}

/*==========================================
 * Initializes and sets a ground skill.
 * flag&1 is used to determine when the skill 'morphs' (Warp portal becomes active, or Fire Pillar becomes active)
 *------------------------------------------*/
struct skill_unit_group* skill_unitsetting (struct block_list *src, short skill_id, short skill_lv, short x, short y, int flag)
{
	struct skill_unit_group *group;
	int i,limit,val1=0,val2=0,val3=0,dir=-1;
	int link_group_id = 0;
	int target,interval,range,unit_flag;
	t_itemid req_item = 0;
	struct s_skill_unit_layout *layout;
	struct map_session_data *sd;
	struct elemental_data *ed;
	struct status_data *status;
	struct status_change *sc;
	int active_flag=1;
	int subunt=0;
	bool hidden = false;

	nullpo_retr(NULL, src);

	limit = skill_get_time(skill_id,skill_lv);
	range = skill_get_unit_range(skill_id,skill_lv);
	interval = skill_get_unit_interval(skill_id);
	target = skill_get_unit_target(skill_id);
	unit_flag = skill_get_unit_flag(skill_id);
	if (skill_id == WL_EARTHSTRAIN || skill_id == RL_FIRE_RAIN)
	{ // flag is the original skill direction
		dir = flag>>16;
		flag = flag&0xFFFF;
	}
	layout = skill_get_unit_layout(skill_id,skill_lv,src,x,y,dir);

	sd = BL_CAST(BL_PC, src);
	ed = BL_CAST(BL_ELEM, src);
	status = status_get_status_data(src);
	sc = status_get_sc(src);	// for traps, firewall and fogwall - celest
	hidden = (unit_flag&UF_HIDDEN_TRAP && (battle_config.traps_setting == 2 || (battle_config.traps_setting == 1 && map_flag_vs(src->m))));

	switch( skill_id )
	{
	case MG_SAFETYWALL:
		val2=skill_lv+1;
		break;
	case MG_FIREWALL:
		if(sc && sc->data[SC_VIOLENTGALE])
			limit = limit*3/2;
		val2=4+skill_lv;
		break;
	case EL_FIRE_MANTLE:
		val2 = 1;// What is the official number of hits a cell can give before vanishing? [Rytech]
		break;
	case AL_WARP:
		val1=skill_lv+6;
		if(!(flag&1))
			limit=2000;
		else // previous implementation (not used anymore)
		{	//Warp Portal morphing to active mode, extract relevant data from src. [Skotlex]
			if( src->type != BL_SKILL ) return NULL;
			group = ((TBL_SKILL*)src)->group;
			src = map_id2bl(group->src_id);
			if( !src ) return NULL;
			val2 = group->val2; //Copy the (x,y) position you warp to
			val3 = group->val3; //as well as the mapindex to warp to.
		}
		break;
	case HP_BASILICA:
		val1 = src->id; // Store caster id.
		break;

	case PR_SANCTUARY:
	case NPC_EVILLAND:
		val1=skill_lv+3;
		break;

	case WZ_FIREPILLAR:
		if (map_getcell(src->m, x, y, CELL_CHKLANDPROTECTOR))
			return NULL;
		if((flag&1)!=0)
			limit=1000;
		val1=skill_lv+2;
		break;
	case WZ_QUAGMIRE:	//The target changes to "all" if used in a gvg map. [Skotlex]
	case AM_DEMONSTRATION:
		if (map_flag_vs(src->m) && battle_config.vs_traps_bctall
			&& (src->type&battle_config.vs_traps_bctall))
			target = BCT_ALL;
		break;
	case HT_ANKLESNARE:
		if (flag & 2) val3 = SC_ESCAPE;
	case HT_SKIDTRAP:
	case MA_SKIDTRAP:
		//Save position of caster
		val1 = ((src->x) << 16) | (src->y);
	case HT_SHOCKWAVE:
	case HT_SANDMAN:
	case MA_SANDMAN:
	case HT_CLAYMORETRAP:
	case HT_LANDMINE:
	case MA_LANDMINE:
	case HT_FLASHER:
	case HT_FREEZINGTRAP:
	case MA_FREEZINGTRAP:
	case HT_BLASTMINE:
	//case RA_ELECTRICSHOCKER:
	//case RA_CLUSTERBOMB:
	//case RA_MAGENTATRAP:
	//case RA_COBALTTRAP:
	//case RA_MAIZETRAP:
	//case RA_VERDURETRAP:
	//case RA_FIRINGTRAP:
	//case RA_ICEBOUNDTRAP:
	//case WM_REVERBERATION:
	case WM_POEMOFNETHERWORLD:// I don't think trap duration is increased in WoE. [Rytech]
	//case GN_THORNS_TRAP:
	//case GN_HELLS_PLANT:
	{
		struct skill_condition req = skill_get_requirement(sd, skill_id, skill_lv);
		ARR_FIND(0, MAX_SKILL_ITEM_REQUIRE, i, req.itemid[i] && (req.itemid[i] == ITEMID_TRAP || req.itemid[i] == ITEMID_SPECIAL_ALLOY_TRAP));
		if (req.itemid[i])
			req_item = req.itemid[i];
		if (map_flag_gvg2(src->m) || map[src->m].flag.battleground)
			limit *= 4; // longer trap times in WOE [celest]
		if (battle_config.vs_traps_bctall && map_flag_vs(src->m) && (src->type&battle_config.vs_traps_bctall))
			target = BCT_ALL;
	}
	break;

	case SA_LANDPROTECTOR:
	case SA_VOLCANO:
	case SA_DELUGE:
	case SA_VIOLENTGALE:
	{
		struct skill_unit_group *old_sg;
		old_sg = skill_locate_element_field(src);

		if( old_sg != NULL )
		{	//HelloKitty confirmed that these are interchangeable,
			//so you can change element and not consume gemstones.
			if( ( old_sg->skill_id == SA_VOLCANO || old_sg->skill_id == SA_DELUGE || old_sg->skill_id == SA_VIOLENTGALE ) && old_sg->limit > 0 )
			{	//Use the previous limit (minus the elapsed time) [Skotlex]
				limit = old_sg->limit - DIFF_TICK32(gettick(), old_sg->tick);
				if (limit < 0)	//This can happen...
					limit = skill_get_time(skill_id,skill_lv);
			}
			/*TODO: Deluge, Volcano, Violentgale should not be casted on top of Warmer skill.
					Warmer also should not be casted on top of Land Protector skill. */
		}
		break;
	}

	case BA_WHISTLE:
		val1 = skill_lv + status->agi / 10; // Flee increase
		val2 = (skill_lv + 1) / 2 + status->luk / 30; // Perfect dodge increase
		if (sd) {
			val1 += pc_checkskill(sd, BA_MUSICALLESSON) / 2;
			val2 += pc_checkskill(sd, BA_MUSICALLESSON) / 5;
		}
		break;
	case DC_HUMMING:
		val1 = 1 + 2 * skill_lv + status->dex / 10; // Hit increase
		if(sd)
			val1 += pc_checkskill(sd,DC_DANCINGLESSON);
		break;
	case BA_POEMBRAGI:
		val1 = 3*skill_lv+status->dex/10; // Casting time reduction
		//For some reason at level 10 the base delay reduction is 50%.
		val2 = (skill_lv<10?3*skill_lv:50)+status->int_/5; // After-cast delay reduction
		if(sd){
			val1 += pc_checkskill(sd,BA_MUSICALLESSON);
			val2 += 2*pc_checkskill(sd,BA_MUSICALLESSON);
		}
		break;
	case DC_DONTFORGETME:
		val1 = 5 + 3 * skill_lv + status->dex / 10; // ASPD decrease
		val2 = 5 + 3 * skill_lv + status->agi / 10; // Movement speed adjustment.
		if (sd) {
			val1 += pc_checkskill(sd, DC_DANCINGLESSON);
			val2 += pc_checkskill(sd, DC_DANCINGLESSON);
		}
		val1 *= 10; //Because 10 is actually 1% aspd
		break;
	case DC_SERVICEFORYOU:
		val1 = 15 + skill_lv + (status->int_ / 10); // MaxSP percent increase
		val2 = 20 + 3 * skill_lv + (status->int_ / 10); // SP cost reduction
		if (sd) {
			val1 += pc_checkskill(sd, DC_DANCINGLESSON) / 2;
			val2 += pc_checkskill(sd, DC_DANCINGLESSON) / 2;
		}
		break;
	case BA_ASSASSINCROSS:
		if (sd)
			val1 = pc_checkskill(sd, BA_MUSICALLESSON) / 2;
		val1 += 5 + skill_lv + (status->agi / 20);
		val1 *= 10; // ASPD works with 1000 as 100%
		break;
	case DC_FORTUNEKISS:
		val1 = 10 + skill_lv + (status->luk / 10); // Critical increase
		val1 *= 10; //Because every 10 crit is an actual cri point.
		if (sd)
			val1 += 5 * pc_checkskill(sd, DC_DANCINGLESSON);
		break;
	case BD_DRUMBATTLEFIELD:
		val1 = (skill_lv+1)*25;	//Watk increase
		val2 = (skill_lv+1)*2;	//Def increase
		break;
	case BD_RINGNIBELUNGEN:
		val1 = (skill_lv+2)*25;	//Watk increase
		break;
	case BD_RICHMANKIM:
		val1 = 25 + 11*skill_lv; //Exp increase bonus.
		break;
	case BD_SIEGFRIED:
		val1 = 55 + skill_lv*5;	//Elemental Resistance
		val2 = skill_lv*10;	//Status ailment resistance
		break;
	case WE_CALLPARTNER:
		if (sd) val1 = sd->status.partner_id;
		break;
	case WE_CALLPARENT:
		if (sd) {
			val1 = sd->status.father;
		 	val2 = sd->status.mother;
		}
		break;
	case WE_CALLBABY:
		if (sd) val1 = sd->status.child;
		break;
	case NJ_KAENSIN:
		skill_clear_group(src, 1); //Delete previous Kaensins/Suitons
		val2 = (skill_lv+1)/2 + 4;
		break;
	case NJ_SUITON:
	case SC_CONFUSIONPANIC:
		skill_clear_group(src, 1);
		break;

	case GS_GROUNDDRIFT:
		{
		// Ground Drift Element is decided when it's placed.
		int ele = skill_get_ele(skill_id, skill_lv);
		int element[5] = { ELE_WIND, ELE_DARK, ELE_POISON, ELE_WATER, ELE_FIRE };
		if (ele == -3)
			val1 = element[rnd() % 5]; // Use random from available unit visual?
		else if (ele == -2)
			val1 = status_get_attack_sc_element(src, sc);
		else if (ele == -1) {
			val1 = status->rhw.ele;
			if (sc && sc->data[SC_ENCHANTARMS])
				val1 = sc->data[SC_ENCHANTARMS]->val2;
		}
		switch (val1) {
		case ELE_FIRE:
			subunt++;
		case ELE_WATER:
			subunt++;
		case ELE_POISON:
			subunt++;
		case ELE_DARK:
			subunt++;
		case ELE_WIND:
			break;
		default:
			subunt = rnd() % 5;
			break;
		}
		break;
		}

	case GC_POISONSMOKE:
		if( !(sc && sc->data[SC_POISONINGWEAPON]) )
			return NULL;
		val1 = sc->data[SC_POISONINGWEAPON]->val1; // Level of Poison, to determine poisoning time
		val2 = sc->data[SC_POISONINGWEAPON]->val2; // Type of Poison
		limit = skill_get_time(skill_id, skill_lv);
		break;

	case SO_CLOUD_KILL:
		skill_clear_group(src, 4);
		break;
	case SO_WARMER:
		skill_clear_group(src, 8);
 		break;

	case GN_WALLOFTHORN:
		if (flag&1)
			limit = 3000;
		val3 = (x<<16)|y;
		break;

	case KO_ZENKAI:
		{
			if (sd)// Charm type affects what AoE will be placed.
				val1 = sd->charmball_type;

			if (!val1 || !(val1 >= CHARM_WATER && val1 <= CHARM_WIND))
				val1 = rnd()%4+1;

			//AoE's duration is 6 seconds * Number of Summoned Charms. [Rytech]
			if (sd->charmball_old > 0)
				limit = limit * sd->charmball_old;// By default its 6 seconds * charm count.
			else
				limit = 60000;

			switch (val1)
			{
				case CHARM_WIND:
					subunt++;
				case CHARM_FIRE:
					subunt++;
				case CHARM_EARTH:
					subunt++;
				case CHARM_WATER:
					break;
				default:
					subunt=rnd()%4+1;
				break;
			}
			break;
		}
	case MH_POISON_MIST:
		interval = 2500 - 200 * skill_lv;
		if (interval < 100)
			interval = 100;
		break;

	case MH_STEINWAND:
		val2=skill_lv+4;
		break;

	case EL_ZEPHYR:
		val2 = status_get_job_lv_effect(&ed->master->bl) / 2;
		break;

	case GD_LEADERSHIP:
	case GD_GLORYWOUNDS:
	case GD_SOULCOLD:
	case GD_HAWKEYES:
		limit = 1000000;//it doesn't matter
		break;

	case HW_GRAVITATION:
		if (sc && sc->data[SC_GRAVITATION] && sc->data[SC_GRAVITATION]->val3 == BCT_SELF)
			link_group_id = sc->data[SC_GRAVITATION]->val4;
	}

	// Init skill unit group
	nullpo_retr(NULL, group=skill_initunitgroup(src,layout->count,skill_id,skill_lv,skill_get_unit_id(skill_id,flag&1)+subunt, limit, interval));
	group->val1=val1;
	group->val2=val2;
	group->val3=val3;
	group->link_group_id = link_group_id;
	group->target_flag=target;
	group->bl_flag= skill_get_unit_bl_target(skill_id);
	group->state.ammo_consume = (sd && sd->state.arrow_atk && skill_id != GS_GROUNDDRIFT); //Store if this skill needs to consume ammo.
	group->state.song_dance = (unit_flag&(UF_DANCE|UF_SONG)?1:0)|(unit_flag&UF_ENSEMBLE?2:0); //Signals if this is a song/dance/duet
	group->state.guildaura = (skill_id >= GD_LEADERSHIP && skill_id <= GD_HAWKEYES) ? 1 : 0;
	group->item_id = req_item;

  	//if tick is greater than current, do not invoke onplace function just yet. [Skotlex]
	if (DIFF_TICK(group->tick, gettick()) > SKILLUNITTIMER_INTERVAL)
		active_flag = 0;

	if(skill_id==HT_TALKIEBOX || skill_id==RG_GRAFFITI){
		group->valstr=(char *) aMalloc(MESSAGE_SIZE*sizeof(char));
		if (sd)
			safestrncpy(group->valstr, sd->message, MESSAGE_SIZE);
		else //Eh... we have to write something here... even though mobs shouldn't use this. [Skotlex]
			safestrncpy(group->valstr, "Boo!", MESSAGE_SIZE);
	}

	if (group->state.song_dance) {
		if(sd){
			sd->skillid_dance = skill_id;
			sd->skilllv_dance = skill_lv;
		}
		if (
			sc_start4(src, SC_DANCING, 100, skill_id, group->group_id, skill_lv,
				(group->state.song_dance&2?BCT_SELF:0), limit+1000) &&
			sd && group->state.song_dance&2 && skill_id != CG_HERMODE //Hermod is a encore with a warp!
		)
			skill_check_pc_partner(sd, skill_id, &skill_lv, 1, 1);
	}

	limit = group->limit;
	for( i = 0; i < layout->count; i++ )
	{
		struct skill_unit *unit;
		int ux = x + layout->dx[i];
		int uy = y + layout->dy[i];
		int val1 = skill_lv;
		int val2 = 0;
		int alive = 1;

		// are the coordinates out of range?
		if (ux <= 0 || uy <= 0 || ux >= map[src->m].xs || uy >= map[src->m].ys) {
			continue;
		}

		if( !group->state.song_dance && !map_getcell(src->m,ux,uy,CELL_CHKREACH) )
			continue; // don't place skill units on walls (except for songs/dances/encores)
		if (battle_config.skill_wall_check && unit_flag&UF_PATHCHECK && !path_search_long(NULL, src->m, ux, uy, src->x, src->y, CELL_CHKWALL))
			continue; // no path between cell and caster

		switch( skill_id )
		{
		case MG_FIREWALL:
		case NJ_KAENSIN:
		case EL_FIRE_MANTLE:
			val2=group->val2;
			break;
		case WZ_ICEWALL:
			val1 = (skill_lv <= 1) ? 500 : 200 + 200*skill_lv;
			val2 = map_getcell(src->m, ux, uy, CELL_GETTYPE);
			break;
		case HT_LANDMINE:
		case MA_LANDMINE:
		case HT_ANKLESNARE:
		case HT_SHOCKWAVE:
		case HT_SANDMAN:
		case MA_SANDMAN:
		case HT_FLASHER:
		case HT_FREEZINGTRAP:
		case MA_FREEZINGTRAP:
		case HT_TALKIEBOX:
		case HT_SKIDTRAP:
		case MA_SKIDTRAP:
		case HT_CLAYMORETRAP:
		case HT_BLASTMINE:
		case RA_ELECTRICSHOCKER:
		case RA_CLUSTERBOMB:
		case RA_MAGENTATRAP:
		case RA_COBALTTRAP:
		case RA_MAIZETRAP:
		case RA_VERDURETRAP:
		case RA_FIRINGTRAP:
		case RA_ICEBOUNDTRAP:
			val1 = 3500;
			break;
		case WZ_WATERBALL:
			//Check if there are cells that can be turned into waterball units
			if (!sd || map_getcell(src->m, ux, uy, CELL_CHKWATER)
				|| (map_find_skill_unit_oncell(src, ux, uy, SA_DELUGE, NULL, 1)) != NULL || (map_find_skill_unit_oncell(src, ux, uy, NJ_SUITON, NULL, 1)) != NULL)
				break; //Turn water, deluge or suiton into waterball cell
			continue;
		case GS_DESPERADO:
			val1 = abs(layout->dx[i]);
			val2 = abs(layout->dy[i]);
			if (val1 < 2 || val2 < 2) { //Nearby cross, linear decrease with no diagonals
				if (val2 > val1) val1 = val2;
				if (val1) val1--;
				val1 = 36 -12*val1;
			} else //Diagonal edges
				val1 = 28 -4*val1 -4*val2;
			if (val1 < 1) val1 = 1;
			val2 = 0;
			break;
		case WM_REVERBERATION:
			val1 = 1 + skill_lv;// Reverberation's HP. It takes 1 damage per hit.
			break;
		case GN_WALLOFTHORN:
			val1 = 2000 + 2000 * skill_lv;//Thorn Walls HP
			val2 = src->id;
			break;
		default:
			if (group->state.song_dance&0x1)
				val2 = unit_flag&(UF_DANCE|UF_SONG); //Store whether this is a song/dance
			break;
		}

		// This animation hack should only work if val2 is not already in use for anything.
		if (skill_get_unit_flag(skill_id)&UF_SINGLEANIMATION && i == (layout->count / 2) && val2 == 0)
			val2 |= UF_SINGLEANIMATION;

		// Check active cell to failing or remove current unit
		if( range <= 0 )
			map_foreachincell(skill_cell_overlap, src->m, ux, uy, BL_SKILL, skill_id, &alive, src);
		if( !alive )
			continue;

		nullpo_retr(NULL, (unit = skill_initunit(group, i, ux, uy, val1, val2, hidden)));
		unit->limit=limit;
		unit->range=range;

		if (skill_id == PF_FOGWALL && alive == 2)
		{	//Double duration of cells on top of Deluge/Suiton
			unit->limit *= 2;
			group->limit = unit->limit;
		}

		// execute on all targets standing on this cell
		if (range==0 && active_flag)
			map_foreachincell(skill_unit_effect,unit->bl.m,unit->bl.x,unit->bl.y,group->bl_flag,&unit->bl,gettick(),1);
	}

	if (!group->alive_count)
	{	//No cells? Something that was blocked completely by Land Protector?
		skill_delunitgroup(group);
		return NULL;
	}


	//success, unit created.
	switch (skill_id) {
		case NJ_TATAMIGAESHI: //Store number of tiles.
			group->val1 = group->alive_count;
			break;
	}

	return group;
}

void ext_skill_unit_onplace(struct skill_unit *src, struct block_list *bl, int64 tick) { skill_unit_onplace(src, bl, tick); }

/*==========================================
 * Triggeres when 'target' (based on skill unit target) is step in unit area.
 * As a follow of skill_unit_effect flag &1
 * @param unit
 * @param bl Target
 * @param tick
 *------------------------------------------*/
static int skill_unit_onplace (struct skill_unit *src, struct block_list *bl, int64 tick)
{
	struct skill_unit_group *sg;
	struct block_list *ss;
	struct status_change *sc;
	struct status_change *ssc;
	struct status_change_entry *sce;
	enum sc_type type;
	int skill_id;

	nullpo_ret(src);
	nullpo_ret(bl);

	if(bl->prev==NULL || !src->alive || status_isdead(bl))
		return 0;

	nullpo_retr(0, sg = src->group);
	nullpo_retr(0, ss = map_id2bl(sg->src_id));

	if( skill_get_type(sg->skill_id) == BF_MAGIC && map_getcell(src->bl.m, src->bl.x, src->bl.y, CELL_CHKLANDPROTECTOR) && sg->skill_id != SA_LANDPROTECTOR )
		return 0; //AoE skills are ineffective. [Skotlex]

	if (skill_get_inf2(sg->skill_id)&(INF2_SONG_DANCE | INF2_ENSEMBLE_SKILL) && map_getcell(bl->m, bl->x, bl->y, CELL_CHKBASILICA))
		return 0; //Songs don't work in Basilica

	sc = status_get_sc(bl);
	ssc = status_get_sc(ss);

	if (sc && sc->option&OPTION_HIDE && sg->skill_id != WZ_HEAVENDRIVE && sg->skill_id != HW_GRAVITATION && sg->skill_id != WL_EARTHSTRAIN && sg->skill_id != SO_EARTHGRAVE)
		return 0; //Hidden characters are immune to AoE skills except Heaven's Drive, Gravitation Field, Earth Strain, and Earth Grave. [Skotlex]

	type = status_skill2sc(sg->skill_id);
	sce = (sc && type != -1)?sc->data[type]:NULL;
	skill_id = sg->skill_id; //In case the group is deleted, we need to return the correct skill id, still.
	switch (sg->unit_id)
	{
	case UNT_SPIDERWEB:
		if (sc) {
			//Duration in PVM is: 1st - 8s, 2nd - 16s, 3rd - 8s
			//Duration in PVP is: 1st - 4s, 2nd - 8s, 3rd - 12s
			int sec = skill_get_time2(sg->skill_id, sg->skill_lv);
			const struct TimerData* td;
			if (map_flag_vs(bl->m))
				sec /= 2;
			if (sc->data[type]) {
				if (sc->data[type]->val2 && sc->data[type]->val3 && sc->data[type]->val4) {
					//Already triple affected, immune
					sg->limit = DIFF_TICK32(tick, sg->tick);
					break;
				}
				//Don't increase val1 here, we need a higher val in status_change_start so it overwrites the old one
				if (map_flag_vs(bl->m) && sc->data[type]->val1 < 3)
					sec *= (sc->data[type]->val1 + 1);
				else if (!map_flag_vs(bl->m) && sc->data[type]->val1 < 2)
					sec *= (sc->data[type]->val1 + 1);
				//Add group id to status change
				if (sc->data[type]->val2 == 0)
					sc->data[type]->val2 = sg->group_id;
				else if (sc->data[type]->val3 == 0)
					sc->data[type]->val3 = sg->group_id;
				else if (sc->data[type]->val4 == 0)
					sc->data[type]->val4 = sg->group_id;
				//Overwrite status change with new duration
				if (td = get_timer(sc->data[type]->timer))
					status_change_start(ss, bl, type, 10000, sc->data[type]->val1 + 1, sc->data[type]->val2, sc->data[type]->val3, sc->data[type]->val4,
						max(DIFF_TICK32(td->tick, tick), sec), SCSTART_NORATEDEF);
			}
			else {
				if (status_change_start(ss, bl, type, 10000, 1, sg->group_id, 0, 0, sec, SCSTART_NORATEDEF)) {
					td = sc->data[type] ? get_timer(sc->data[type]->timer) : NULL;
					if (td)
						sec = DIFF_TICK32(td->tick, tick);
					map_moveblock(bl, src->bl.x, src->bl.y, tick);
					clif_fixpos(bl);
				}
				else
					sec = 3000; //Couldn't trap it?
			}
			sg->val2 = bl->id;
			sg->limit = DIFF_TICK32(tick, sg->tick) + sec;
		}
		break;
	case UNT_SAFETYWALL:
		if (!sce)
			sc_start4(bl,type,100,sg->skill_lv,sg->group_id,sg->group_id,0,sg->limit);
		break;

	case UNT_PNEUMA:
		if (!sce)
			sc_start4(bl,type,100,sg->skill_lv,sg->group_id,0,0,sg->limit);
		break;

	case UNT_WARP_WAITING: {
		int working = sg->val1 & 0xffff;

		if (bl->type == BL_PC && !working) {
			struct map_session_data *sd = (struct map_session_data *)bl;
			if ((!sd->chatID || battle_config.chat_warpportal)
				&& sd->ud.to_x == src->bl.x && sd->ud.to_y == src->bl.y)
			{
				int x = sg->val2 >> 16;
				int y = sg->val2 & 0xffff;
				int count = sg->val1 >> 16;
				unsigned short m = sg->val3;

				if (--count <= 0)
					skill_delunitgroup(sg);

				if (map_mapindex2mapid(sg->val3) == sd->bl.m && x == sd->bl.x && y == sd->bl.y)
					working = 1;/* we break it because officials break it, lovely stuff. */

				sg->val1 = (count << 16) | working;

				if (pc_job_can_entermap((enum e_job)sd->status.class_, map_mapindex2mapid(m), sd->gmlevel)) {
					pc_setpos(sd, m, x, y, CLR_TELEPORT);
					sg = src->group; // avoid dangling pointer (pc_setpos can cause deletion of 'sg')
				}
			}
		}
		else if (bl->type == BL_MOB && battle_config.mob_warp & 2) {
			int m = map_mapindex2mapid(sg->val3);
			if (m < 0) break; //Map not available on this map-server.
			unit_warp(bl, m, sg->val2 >> 16, sg->val2 & 0xffff, CLR_TELEPORT);
		}
	}
		break;

	case UNT_QUAGMIRE:
		if(!sce && battle_check_target(&sg->unit->bl, bl, sg->target_flag) > 0)
			sc_start4(bl,type,100,sg->skill_lv,sg->group_id,0,0,sg->limit);
		break;

	case UNT_VOLCANO:
	case UNT_DELUGE:
	case UNT_VIOLENTGALE:
	case UNT_NEUTRALBARRIER:
	case UNT_STEALTHFIELD:
	case UNT_BLOODYLUST:
	case UNT_FIRE_INSIGNIA:
	case UNT_WATER_INSIGNIA:
	case UNT_WIND_INSIGNIA:
	case UNT_EARTH_INSIGNIA:
	case UNT_CN_POWDERING:
	case UNT_NYANGGRASS:
	case UNT_WATER_BARRIER:
	case UNT_POWER_OF_GAIA:
		if (sg->src_id == bl->id && (sg->unit_id == UNT_STEALTHFIELD || sg->unit_id == UNT_BLOODYLUST))
			return 0;// Can't be affected by your own AoE.
		if(!sce)
			sc_start(bl,type,100,sg->skill_lv,sg->limit);
		break;

	case UNT_ZEPHYR:
		if(!sce)
			sc_start2(bl,type,100,sg->skill_lv,sg->val2,sg->limit);
		break;

	case UNT_WARMER:
		status_change_end(bl, SC_FREEZE, INVALID_TIMER);
		status_change_end(bl, SC_FROST, INVALID_TIMER);
		status_change_end(bl, SC_CRYSTALIZE, INVALID_TIMER);
		if(!sce)
			sc_start(bl,type,100,sg->skill_lv,sg->limit);
		break;

	case UNT_CREATINGSTAR:
		if (!sce)
			sc_start4(bl, type, 100, sg->skill_lv, ss->id, src->bl.id, 0, sg->limit);
		break;

	case UNT_BANDING:
		if(!sce)
			sc_start(bl,type,5*sg->skill_lv+status_get_base_lv_effect(ss)/5-status_get_agi(bl)/10,sg->skill_lv,skill_get_time2(sg->skill_id,sg->skill_lv));
		break;

	case UNT_CHAOSPANIC:
	case UNT_VOLCANIC_ASH:
		if (!sce)
			sc_start(bl,type,100,sg->skill_lv,skill_get_time2(sg->skill_id,sg->skill_lv));
		break;

	case UNT_KINGS_GRACE:
		if(!sce)
			sc_start(bl,type,100,sg->skill_lv,sg->limit);

		// Immunity active. Now to remove status's your immune to.
		if ( sc )
		{
			short i = 0;
			const enum sc_type scs[] = { SC_MARSHOFABYSS, SC_MANDRAGORA };
			// Checking for common status's.
			for (i = SC_COMMON_MIN; i <= SC_COMMON_MAX; i++)
				if (sc->data[i])
					status_change_end(bl, (sc_type)i, INVALID_TIMER);
			// Checking for Guillotine poisons.
			for (i = SC_NEW_POISON_MIN; i <= SC_NEW_POISON_MAX; i++)
				if (sc->data[i])
					status_change_end(bl, (sc_type)i, INVALID_TIMER);
			// Checking for additional status's.
			for (i = 0; i < ARRAYLENGTH(scs); i++)
				if (sc->data[scs[i]])
					status_change_end(bl, scs[i], INVALID_TIMER);
		}
		break;

	case UNT_SUITON:
		if(!sce)
			sc_start4(bl,type,100,sg->skill_lv,
			map_flag_vs(bl->m) || battle_check_target(&src->bl,bl,BCT_ENEMY)>0?1:0, //Send val3 =1 to reduce agi.
			0,0,sg->limit);
		break;

	case UNT_HERMODE:
		if( sg->src_id!=bl->id && battle_check_target(&src->bl,bl,BCT_PARTY|BCT_GUILD) > 0 )
			status_change_clear_buffs(bl, SCCB_BUFFS); //Should dispell only allies.
	case UNT_RICHMANKIM:
	case UNT_ETERNALCHAOS:
	case UNT_DRUMBATTLEFIELD:
	case UNT_RINGNIBELUNGEN:
	case UNT_ROKISWEIL:
	case UNT_INTOABYSS:
	case UNT_SIEGFRIED:
		 //Needed to check when a dancer/bard leaves their ensemble area.
		if( sg->src_id == bl->id && !(sc && sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_BARDDANCER) )
			return skill_id;
		if( !sce )
			sc_start4(bl,type,100,sg->skill_lv,sg->val1,sg->val2,0,sg->limit);
		break;
	case UNT_WHISTLE:
	case UNT_ASSASSINCROSS:
	case UNT_POEMBRAGI:
	case UNT_APPLEIDUN:
	case UNT_HUMMING:
	case UNT_DONTFORGETME:
	case UNT_FORTUNEKISS:
	case UNT_SERVICEFORYOU:
		if (sg->src_id == bl->id && !(sc && sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_BARDDANCER))
			return 0;
		if( !sc )
			return 0;
		if( !sce )
			sc_start4(bl,type,100,sg->skill_lv,sg->val1,sg->val2,0,sg->limit);
		else if (sce->val4 == 1) { //Readjust timers since the effect will not last long.
			sce->val4 = 0; //remove the mark that we stepped out
			delete_timer(sce->timer, status_change_timer);
			sce->timer = add_timer(tick+sg->limit, status_change_timer, bl->id, type); //put duration back to 3min
		}
		break;

	case UNT_FOGWALL:
		if (!sce)
		{
			sc_start4(bl, type, 100, sg->skill_lv, sg->val1, sg->val2, sg->group_id, sg->limit);
			if (battle_check_target(&src->bl,bl,BCT_ENEMY)>0)
				skill_additional_effect (ss, bl, sg->skill_id, sg->skill_lv, BF_MISC, ATK_DEF, tick);
		}
		break;

	case UNT_GRAVITATION:
		if (!sce)
			sc_start4(bl,type,100,sg->skill_lv,0,BCT_ENEMY,sg->group_id,sg->limit);
		break;

	case UNT_BASILICA:
	{
		int i = battle_check_target(bl, bl, BCT_ENEMY);

		if (i > 0) {
			skill_blown(ss, bl, skill_get_blewcount(skill_id, sg->skill_lv), unit_getdir(bl), 0);
			break;
		}
		if (!sce && i <= 0)
			status_change_start(ss, bl, type, 10000, 0, 0, sg->group_id, ss->id, sg->limit, 0);
	}
	break;

// officially, icewall has no problems existing on occupied cells [ultramage]
//	case UNT_ICEWALL: //Destroy the cell. [Skotlex]
//		src->val1 = 0;
//		if(src->limit + sg->tick > tick + 700)
//			src->limit = DIFF_TICK32(tick + 700, sg->tick);
//		break;

	case UNT_MOONLIT:
		//Knockback out of area if affected char isn't in Moonlit effect
		if (sc && sc->data[SC_DANCING] && (sc->data[SC_DANCING]->val1&0xFFFF) == CG_MOONLIT)
			break;
		if (ss == bl) //Also needed to prevent infinite loop crash.
			break;
		skill_blown(ss,bl,skill_get_blewcount(sg->skill_id,sg->skill_lv),unit_getdir(bl),0);
		break;

	case UNT_WALLOFTHORN:
		if( status_get_mode(bl)&MD_STATUS_IMMUNE)
			break;	// iRO Wiki says that this skill don't affect to Boss monsters.
		if( battle_check_target(ss,bl,BCT_ENEMY) <= 0 )
			skill_blown(&src->bl,bl,skill_get_blewcount(sg->skill_id,sg->skill_lv),unit_getdir(bl),0);
		else
			skill_attack(skill_get_type(sg->skill_id), ss, &src->bl, bl, sg->skill_id, sg->skill_lv, tick, 0);
		break;

	case UNT_GD_LEADERSHIP:
	case UNT_GD_GLORYWOUNDS:
	case UNT_GD_SOULCOLD:
	case UNT_GD_HAWKEYES:
		if (!sce && battle_check_target(&sg->unit->bl, bl, sg->target_flag) > 0)
			sc_start4(bl, type, 100, sg->skill_lv, 0, 0, 0, 1000);
		break;
	}
	return skill_id;
}

/*==========================================
 * Process skill unit each interval (sg->interval, see interval field of skill_unit_db.txt)
 * @param src Skill unit
 * @param bl Valid 'target' above the unit, that has been check in skill_unit_timer_sub_onplace
 * @param tick
 *------------------------------------------*/
int skill_unit_onplace_timer (struct skill_unit *src, struct block_list *bl, int64 tick)
{
	struct skill_unit_group *sg;
	struct block_list *ss;
	TBL_PC* sd;
	TBL_PC* tsd;
	struct status_data *tstatus;
	struct status_change *tsc, *ssc;
	struct skill_unit_group_tickset *ts;
	enum sc_type type;
	int skill_id;
	int diff=0;

	nullpo_ret(src);
	nullpo_ret(bl);

	if (bl->prev==NULL || !src->alive || status_isdead(bl))
		return 0;

	nullpo_ret(sg=src->group);
	nullpo_ret(ss=map_id2bl(sg->src_id));
	sd = BL_CAST(BL_PC, ss);
	tsd = BL_CAST(BL_PC, bl);
	tsc = status_get_sc(bl);
	ssc = status_get_sc(ss); // Status Effects for Unit caster.
	tstatus = status_get_status_data(bl);
	type = status_skill2sc(sg->skill_id);
	skill_id = sg->skill_id;

	if ( tsc && tsc->data[SC_HOVERING] )
		return 0; //Under hovering characters are immune to trap and ground target skills.

	if (sg->interval == -1)
	{
		switch (sg->unit_id) 
		{
			case UNT_ANKLESNARE: //These happen when a trap is splash-triggered by multiple targets on the same cell.
			case UNT_FIREPILLAR_ACTIVE:
 			case UNT_ELECTRICSHOCKER:
			case UNT_MANHOLE:
			//case UNT_POEMOFNETHERWORLD:// Likely not needed, but placed here in case.
				return 0;
			default:
				ShowError("skill_unit_onplace_timer: interval error (unit id %x)\n", sg->unit_id);
				return 0;
		}
	}

	if ((ts = skill_unitgrouptickset_search(bl,sg,tick)))
	{	//Not all have it, eg: Traps don't have it even though they can be hit by Heaven's Drive [Skotlex]
		diff = DIFF_TICK32(tick, ts->tick);
		if (diff < 0)
			return 0;
		ts->tick = tick+sg->interval;

		if ((skill_id==CR_GRANDCROSS || skill_id==NPC_GRANDDARKNESS) && !battle_config.gx_allhit)
			ts->tick += sg->interval*(map_count_oncell(bl->m,bl->x,bl->y,BL_CHAR,0)-1);
	}

	switch (sg->unit_id)
	{
		case UNT_FIREWALL:
		case UNT_KAEN:
		case UNT_FIRE_MANTLE:
		{
			int count=0;
			const int x = bl->x, y = bl->y;

			if( sg->skill_id == GN_WALLOFTHORN && !map_flag_vs(bl->m) )
				break;

			//Take into account these hit more times than the timer interval can handle.
			do
				skill_attack(BF_MAGIC,ss,&src->bl,bl,sg->skill_id,sg->skill_lv,tick+count*sg->interval,0);
			while(sg->interval > 0 && --src->val2 && x == bl->x && y == bl->y &&
				++count < SKILLUNITTIMER_INTERVAL/sg->interval && !status_isdead(bl));

			if (src->val2<=0)
				skill_delunit(src);
		}
		break;

		case UNT_SANCTUARY:
			if( battle_check_undead(tstatus->race, tstatus->def_ele) || tstatus->race==RC_DEMON )
			{ //Only damage enemies with offensive Sanctuary. [Skotlex]
				if( battle_check_target(&src->bl,bl,BCT_ENEMY) > 0 && skill_attack(BF_MAGIC, ss, &src->bl, bl, sg->skill_id, sg->skill_lv, tick, 0) )
					sg->val1 -= 1; // reduce healing count if this was meant for damaging [hekate]
			}
			else
			{
				int heal = skill_calc_heal(ss,bl,sg->skill_id,sg->skill_lv,true);
				struct mob_data *md = BL_CAST(BL_MOB, bl);
				if (md && status_get_class_(bl) == CLASS_BATTLEFIELD)
					break;
				if( tstatus->hp >= tstatus->max_hp )
					break;
				if( status_isimmune(bl) )
					heal = 0;
				if( tsc && tsc->data[SC_AKAITSUKI] )
					skill_akaitsuki_damage(&src->bl, bl, heal, sg->skill_id, sg->skill_lv, tick);
				else
				{
					clif_skill_nodamage(&src->bl, bl, AL_HEAL, heal, 1);
					status_heal(bl, heal, 0, 0);
				}
			}
			break;

		case UNT_EVILLAND:
			//Will heal demon and undead element monsters, but not players.
			if ((bl->type == BL_PC) || (!battle_check_undead(tstatus->race, tstatus->def_ele) && tstatus->race!=RC_DEMON))
			{	//Damage enemies
				if(battle_check_target(&src->bl,bl,BCT_ENEMY)>0)
					skill_attack(BF_MISC, ss, &src->bl, bl, sg->skill_id, sg->skill_lv, tick, 0);
			} else {
				int heal = skill_calc_heal(ss,bl,sg->skill_id,sg->skill_lv,true);
				if (tstatus->hp >= tstatus->max_hp)
					break;
				if (status_isimmune(bl))
					heal = 0;
				clif_skill_nodamage(&src->bl, bl, AL_HEAL, heal, 1);
				status_heal(bl, heal, 0, 0);
			}
			break;

		case UNT_MAGNUS:
			if (!battle_check_undead(tstatus->race,tstatus->def_ele) && tstatus->race!=RC_DEMON)
				break;
			skill_attack(BF_MAGIC,ss,&src->bl,bl,sg->skill_id,sg->skill_lv,tick,0);
			break;

		case UNT_DUMMYSKILL:
			switch (sg->skill_id)
			{
				case SG_SUN_WARM: //SG skills [Komurka]
				case SG_MOON_WARM:
				case SG_STAR_WARM:
				{
					int count = 0;
					const int x = bl->x, y = bl->y;

					//If target isn't knocked back it should hit every "interval" ms [Playtester]
					do
					{
						if( bl->type == BL_PC )
							status_zap(bl, 0, 15); // sp damage to players
						else // mobs
						if( status_charge(ss, 0, 2) ) // costs 2 SP per hit
						{
							if( !skill_attack(BF_WEAPON,ss,&src->bl,bl,sg->skill_id,sg->skill_lv,tick+count*sg->interval,0) )
								status_charge(ss, 0, 8); //costs additional 8 SP if miss
						}
						else
						{ //should end when out of sp.
							sg->limit = DIFF_TICK32(tick, sg->tick);
							break;
						}
					} while(sg->interval > 0 && x == bl->x && y == bl->y &&
						++count < SKILLUNITTIMER_INTERVAL/sg->interval && !status_isdead(bl) );
				}
				break;
				case WZ_STORMGUST: //SG counter does not reset per stormgust. IE: One hit from a SG and two hits from another will freeze you.
					if (tsc) 
						tsc->sg_counter++; //SG hit counter.
					if (skill_attack(skill_get_type(sg->skill_id),ss,&src->bl,bl,sg->skill_id,sg->skill_lv,tick,0) <= 0 && tsc)
						tsc->sg_counter=0; //Attack absorbed.
					break;

				case GS_DESPERADO:
					if (rnd()%100 < src->val1)
						skill_attack(BF_WEAPON,ss,&src->bl,bl,sg->skill_id,sg->skill_lv,tick,0);
					break;

				default:
					skill_attack(skill_get_type(sg->skill_id),ss,&src->bl,bl,sg->skill_id,sg->skill_lv,tick,0);
			}
			if (skill_get_unit_interval(sg->skill_id) >= skill_get_time(sg->skill_id, sg->skill_lv))
				sg->unit_id = UNT_USED_TRAPS;
			break;

		case UNT_FIREPILLAR_WAITING:
			skill_unitsetting(ss,sg->skill_id,sg->skill_lv,src->bl.x,src->bl.y,1);
			skill_delunit(src);
			break;

		case UNT_SKIDTRAP:
			{
				//Knockback away from position of user during placement [Playtester]
				skill_blown(&src->bl, bl, skill_get_blewcount(sg->skill_id, sg->skill_lv),
				(map_calc_dir_xy(sg->val1 >> 16, sg->val1 & 0xFFFF, bl->x, bl->y, 6) + 4) % 8, 0);
				sg->unit_id = UNT_USED_TRAPS;
				clif_changetraplook(&src->bl, UNT_USED_TRAPS);
				sg->limit = DIFF_TICK32(tick, sg->tick) + 1500;
				//Target will be stopped for 3 seconds
				status_change_start(ss, bl, SC_STOP, 10000, 0, 0, 0, 0, skill_get_time2(sg->skill_id, sg->skill_lv), 0);
			}
			break;

		case UNT_ANKLESNARE:
		case UNT_MANHOLE:
			if( sg->val2 == 0 && tsc && (sg->unit_id == UNT_ANKLESNARE || bl->id != sg->src_id) ){
				int sec = skill_get_time2(sg->skill_id,sg->skill_lv);
				
				if( status_change_start(ss,bl,type,10000,sg->skill_lv,sg->group_id,0,0,sec, 8) ){
					const struct TimerData* td = tsc->data[type]?get_timer(tsc->data[type]->timer):NULL; 
					
					if( td )
						sec = DIFF_TICK32(td->tick, tick);
					if (sg->unit_id == UNT_MANHOLE || !unit_blown_immune(bl, 0x1)) {
						unit_movepos(bl, src->bl.x, src->bl.y, 0, 0);
						clif_fixpos(bl);
					}
					sg->val2 = bl->id;
				}
				else
					sec = 3000; //Couldn't trap it?
				if( sg->unit_id == UNT_ANKLESNARE )
					clif_skillunit_update(&src->bl);
				sg->limit = DIFF_TICK32(tick, sg->tick) + sec;
				sg->interval = -1;
				src->range = 0;
			}
			break;

		case UNT_ELECTRICSHOCKER:
			if( bl->id != ss->id )
			{
				int sec = skill_get_time2(sg->skill_id,sg->skill_lv);
				if(status_get_mode(bl)&MD_STATUS_IMMUNE)
					break;
				if (status_change_start(ss,bl,type,10000,sg->skill_lv,sg->group_id,0,0,sec, 8))
				{
					const struct TimerData* td = tsc->data[type]?get_timer(tsc->data[type]->timer):NULL;
					if (td)
						sec = DIFF_TICK32(td->tick, tick);
					map_moveblock(bl, src->bl.x, src->bl.y, tick);
					clif_fixpos(bl);
				} else
					sec = 3000; //Couldn't trap it?
				map_foreachinrange(skill_trap_splash,&src->bl, skill_get_splash(sg->skill_id, sg->skill_lv), sg->bl_flag, &src->bl,tick);
				src->range = -1;
			}
			break;

		case UNT_VENOMDUST:
			if(tsc && !tsc->data[type])
				status_change_start(ss,bl,type,10000,sg->skill_lv,sg->src_id,0,0,skill_get_time2(sg->skill_id,sg->skill_lv),0);
			break;

		case UNT_LANDMINE:
			//Land Mine only hits single target
			skill_attack(skill_get_type(sg->skill_id), ss, &src->bl, bl, sg->skill_id, sg->skill_lv, tick, 0);
			sg->unit_id = UNT_USED_TRAPS; //Changed ID so it does not invoke a for each in area again.
			sg->limit = 1500;
			break;

		case UNT_MAGENTATRAP:
		case UNT_COBALTTRAP:
		case UNT_MAIZETRAP:
		case UNT_VERDURETRAP:
			if( bl->type == BL_PC )// it won't work on players
				break;
		case UNT_FIRINGTRAP:
		case UNT_ICEBOUNDTRAP:
		case UNT_CLUSTERBOMB:
			if( bl->id == ss->id )// it won't trigger on caster
				break;
		case UNT_BLASTMINE:
		case UNT_SHOCKWAVE:
		case UNT_SANDMAN:
		case UNT_FLASHER:
		case UNT_FREEZINGTRAP:
		case UNT_FIREPILLAR_ACTIVE:
		case UNT_CLAYMORETRAP:
		{
			int bl_flag = sg->bl_flag;
			if (tsc && tsc->data[SC__MANHOLE])
				break;
			if (sg->unit_id == UNT_FIRINGTRAP || sg->unit_id == UNT_ICEBOUNDTRAP || sg->unit_id == UNT_CLAYMORETRAP)
				bl_flag = bl_flag | BL_SKILL | ~BCT_SELF;
			if (battle_config.skill_wall_check && !(skill_get_nk(skill_id)&NK_NO_DAMAGE))
				map_foreachinshootrange(skill_trap_splash, &src->bl, skill_get_splash(sg->skill_id, sg->skill_lv), bl_flag, &src->bl, tick);
			else
				map_foreachinrange(skill_trap_splash, &src->bl, skill_get_splash(sg->skill_id, sg->skill_lv), bl_flag, &src->bl, tick);
			if (sg->unit_id != UNT_FIREPILLAR_ACTIVE)
				clif_changetraplook(&src->bl, (sg->unit_id == UNT_LANDMINE ? UNT_FIREPILLAR_ACTIVE : UNT_USED_TRAPS));
			sg->limit = DIFF_TICK32(tick, sg->tick) +
				(sg->unit_id == UNT_CLUSTERBOMB || sg->unit_id == UNT_ICEBOUNDTRAP ? 1000 : 0) + // Cluster Bomb/Icebound has 1s to disappear once activated.
				(sg->unit_id == UNT_FIRINGTRAP ? 0 : 1500); // Firing Trap gets removed immediately once activated.
			sg->unit_id = UNT_USED_TRAPS; // Change ID so it does not invoke a for each in area again.
		}
		break;
		case UNT_REVERBERATION:
		case UNT_POEMOFNETHERWORLD:
			map_foreachinrange(skill_trap_splash,&src->bl, skill_get_splash(sg->skill_id, sg->skill_lv), sg->bl_flag, &src->bl,tick);
			clif_changetraplook(&src->bl, UNT_USED_TRAPS);
			src->range = -1; //Disable range so it does not invoke a for each in area again.
			sg->limit = DIFF_TICK32(tick, sg->tick) + 1500;
			break;

		case UNT_TALKIEBOX:
			if (sg->src_id == bl->id)
				break;
			if (sg->val2 == 0){
				clif_talkiebox(&src->bl, sg->valstr);
				sg->unit_id = UNT_USED_TRAPS;
				clif_changetraplook(&src->bl, UNT_USED_TRAPS);
				sg->limit = DIFF_TICK32(tick, sg->tick) + 5000;
				sg->val2 = -1;
			}
			break;

		case UNT_LULLABY:
			if( ss->id == bl->id )
				break;
			skill_additional_effect(ss, bl, sg->skill_id, sg->skill_lv, BF_LONG|BF_SKILL|BF_MISC, ATK_DEF, tick);
			break;

		case UNT_UGLYDANCE:	//Ugly Dance [Skotlex]
			if( ss->id != bl->id )
				skill_additional_effect(ss, bl, sg->skill_id, sg->skill_lv, BF_LONG|BF_SKILL|BF_MISC, ATK_DEF, tick);
			break;

		case UNT_DISSONANCE:
			skill_attack(BF_MISC, ss, &src->bl, bl, sg->skill_id, sg->skill_lv, tick, 0);
			break;

		case UNT_APPLEIDUN: //Apple of Idun [Skotlex]
		{
			int heal;
			if( sg->src_id == bl->id && !(tsc && tsc->data[SC_SPIRIT] && tsc->data[SC_SPIRIT]->val2 == SL_BARDDANCER) )
				break; // affects self only when soullinked
			heal = skill_calc_heal(ss,bl,sg->skill_id, sg->skill_lv, true);
			if( tsc && tsc->data[SC_AKAITSUKI] )
				skill_akaitsuki_damage(&src->bl, bl, heal, sg->skill_id, sg->skill_lv, tick);
			else
			{
				clif_skill_nodamage(&src->bl, bl, AL_HEAL, heal, 1);
				status_heal(bl, heal, 0, 0);
			}
			break;
		}

 		case UNT_TATAMIGAESHI:
		case UNT_DEMONSTRATION:
			skill_attack(BF_WEAPON,ss,&src->bl,bl,sg->skill_id,sg->skill_lv,tick,0);
			break;

		case UNT_GOSPEL:
			if (rnd() % 100 > 50 + sg->skill_lv * 5 || ss == bl)
				break;
			if( battle_check_target(ss,bl,BCT_PARTY) > 0 )
			{ // Support Effect only on party, not guild
				int heal;
				int i = rnd()%13; // Positive buff count
				int time = skill_get_time2(sg->skill_id, sg->skill_lv); //Duration
				switch (i)
				{
				case 0: // Heal 1000~9999 HP
					heal = rnd() % 9000 + 1000;
						clif_skill_nodamage(ss,bl,AL_HEAL,heal,1);
						status_heal(bl,heal,0,0);
						break;
					case 1: // End all negative status
						status_change_clear_buffs(bl, SCCB_DEBUFFS);
						if (tsd) clif_gospel_info(tsd, 0x15);
						break;
					case 2: // Immunity to all status
						sc_start(bl,SC_SCRESIST,100,100,time);
						if (tsd) clif_gospel_info(tsd, 0x16);
						break;
					case 3: // MaxHP +100%
						sc_start(bl,SC_INCMHPRATE,100,100,time);
						if (tsd) clif_gospel_info(tsd, 0x17);
						break;
					case 4: // MaxSP +100%
						sc_start(bl,SC_INCMSPRATE,100,100,time);
						if (tsd) clif_gospel_info(tsd, 0x18);
						break;
					case 5: // All stats +20
						sc_start(bl,SC_INCALLSTATUS,100,20,time);
						if (tsd) clif_gospel_info(tsd, 0x19);
						break;
					case 6: // Level 10 Blessing
						sc_start(bl,SC_BLESSING,100,10, skill_get_time(AL_BLESSING, 10));
						break;
					case 7: // Level 10 Increase AGI
						sc_start(bl,SC_INCREASEAGI,100,10, skill_get_time(AL_INCAGI, 10));
						break;
					case 8: // Enchant weapon with Holy element
						sc_start(bl,SC_ASPERSIO,100,1,time);
						if (tsd) clif_gospel_info(tsd, 0x1c);
						break;
					case 9: // Enchant armor with Holy element
						sc_start(bl,SC_BENEDICTIO,100,1,time);
						if (tsd) clif_gospel_info(tsd, 0x1d);
						break;
					case 10: // DEF +25%
						sc_start(bl,SC_INCDEFRATE,100,25, 10000); //10 seconds
						if (tsd) clif_gospel_info(tsd, 0x1e);
						break;
					case 11: // ATK +100%
						sc_start(bl,SC_INCATKRATE,100,100,time);
						if (tsd) clif_gospel_info(tsd, 0x1f);
						break;
					case 12: // HIT/Flee +50
						sc_start(bl,SC_INCHIT,100,50,time);
						sc_start(bl,SC_INCFLEE,100,50,time);
						if (tsd) clif_gospel_info(tsd, 0x20);
						break;
				}
			}
			else if (battle_check_target(&src->bl,bl,BCT_ENEMY)>0)
			{ // Offensive Effect
				int i = rnd() % 10; // Negative buff count
				int time = skill_get_time2(sg->skill_id, sg->skill_lv);
				switch (i)
				{
					case 0: // Deal 3000~7999 damage reduced by DEF
					case 1: // Deal 1500~5499 damage unreducable
						skill_attack(BF_MISC, ss, &src->bl, bl, sg->skill_id, sg->skill_lv, tick, i);
						break;
					case 2: // Curse
						sc_start(bl,SC_CURSE,100,1,1800000); //30 minutes
						break;
					case 3: // Blind
						sc_start(bl,SC_BLIND,100,1,1800000); //30 minutes
						break;
					case 4: // Poison
						sc_start(bl,SC_POISON,100,1,1800000); //30 minutes
						break;
					case 5: // Level 10 Provoke
						clif_skill_nodamage(NULL, bl, SM_PROVOKE, 10, status_change_start(ss, bl, SC_PROVOKE, 10000, 10, 0, 0, 0, -1, 0)); //Infinite
						break;
					case 6: // DEF -100%
						sc_start(bl,SC_INCDEFRATE,100,-100,20000); //20 seconds
						break;
					case 7: // ATK -100%
						sc_start(bl,SC_INCATKRATE,100,-100,20000); //20 seconds
						break;
					case 8: // Flee -100%
						sc_start(bl,SC_INCFLEERATE,100,-100,20000); //20 seconds
						break;
					case 9: // Speed/ASPD -25%
						sc_start4(bl,SC_GOSPEL,100,1,0,0,BCT_ENEMY,20000); //20 seconds
						break;
				}
			}
			break;

		case UNT_BASILICA:
			{
				int i = battle_check_target(&src->bl, bl, BCT_ENEMY);
				if (i > 0) {
					skill_blown(&src->bl, bl, skill_get_blewcount(skill_id, sg->skill_lv), unit_getdir(bl), 0);
					break;
				}

				if (i <= 0 && (!tsc || !tsc->data[SC_BASILICA]))
					status_change_start(ss, bl, type, 10000, 0, 0, sg->group_id, ss->id, sg->limit, 0);
			}
			break;

		case UNT_GRAVITATION:
		case UNT_EARTHSTRAIN:		
		case UNT_FIREWALK:
		case UNT_ELECTRICWALK:
		case UNT_PSYCHIC_WAVE:
		case UNT_FIRE_RAIN:
		case UNT_VENOMFOG:
			skill_attack(skill_get_type(sg->skill_id),ss,&src->bl,bl,sg->skill_id,sg->skill_lv,tick,0);
			break;

		case UNT_GROUNDDRIFT_WIND:
		case UNT_GROUNDDRIFT_DARK:
		case UNT_GROUNDDRIFT_POISON:
		case UNT_GROUNDDRIFT_WATER:
		case UNT_GROUNDDRIFT_FIRE:
			map_foreachinrange(skill_trap_splash,&src->bl,
				skill_get_splash(sg->skill_id, sg->skill_lv), sg->bl_flag,
				&src->bl,tick);
			sg->unit_id = UNT_USED_TRAPS;
			//clif_changetraplook(&src->bl, UNT_FIREPILLAR_ACTIVE);
			sg->limit = DIFF_TICK32(tick, sg->tick);
			break;

		case UNT_POISONSMOKE:
			{
				short rate = 100;
				if( battle_check_target(ss,bl,BCT_ENEMY) > 0 && !(tsc && tsc->data[sg->val2]) && rnd()%100 < 50 )
				{
					if ( sg->val1 == 9 )//Oblivion Curse gives a 2nd success chance after the 1st one passes which is reduceable. [Rytech]
						rate = 100 - tstatus->int_ * 4 / 5 ;
					sc_start(bl,sg->val2,rate,sg->val1,skill_get_time2(GC_POISONINGWEAPON,1) - (tstatus->vit + tstatus->luk) / 2 * 1000);
				}
			}
			break;

		case UNT_EPICLESIS:
			if( bl->type == BL_PC && !battle_check_undead(tstatus->race, tstatus->def_ele) && tstatus->race != RC_DEMON )
			{
				if (++sg->val2 % 3 == 0) {
					int hp, sp;
					switch (sg->skill_lv)
					{
					case 1: case 2: hp = 3; sp = 2; break;
					case 3: case 4: hp = 4; sp = 3; break;
					case 5: default: hp = 5; sp = 4; break;
					}
					hp = tstatus->max_hp * hp / 100;
					sp = tstatus->max_sp * sp / 100;
					status_heal(bl, hp, sp, 0);
					if (tstatus->hp < tstatus->max_hp)
						clif_skill_nodamage(&src->bl, bl, AL_HEAL, hp, 1);
					if (tstatus->sp < tstatus->max_sp)
						clif_skill_nodamage(&src->bl, bl, MG_SRECOVERY, sp, 1);
					sc_start(bl, type, 100, sg->skill_lv, (sg->interval * 3) + 100);
				}
				// Reveal hidden players every 5 seconds.
				if (sg->val2 % 5 == 0) {
					// Doesn't remove Invisibility or Chase Walk.
					status_change_end(bl,SC_HIDING,INVALID_TIMER);
					status_change_end(bl,SC_CLOAKING,INVALID_TIMER);
					status_change_end(bl, SC_CLOAKINGEXCEED, INVALID_TIMER);
					status_change_end(bl, SC_NEWMOON, INVALID_TIMER);
				}
			}
			/* Enable this if kRO fix the current skill. Currently no damage on undead and demon monster. [Jobbie]
			else if( battle_check_target(ss, bl, BCT_ENEMY) > 0 && battle_check_undead(tstatus->race, tstatus->def_ele) )
				skill_castend_damage_id(&src->bl, bl, sg->skill_id, sg->skill_lv, 0, 0);*/
			break;

		case UNT_DIMENSIONDOOR:
			if (tsd && !map[bl->m].flag.noteleport)
				pc_randomwarp(tsd, CLR_TELEPORT);
			else if (bl->type == BL_MOB && battle_config.mob_warp&8)
				unit_warp(bl, -1, -1, -1, CLR_TELEPORT);
			break;

		case UNT_SEVERE_RAINSTORM:
			if (battle_check_target(&src->bl, bl, BCT_ENEMY))
				skill_attack(BF_WEAPON, ss, &src->bl, bl, WM_SEVERE_RAINSTORM_MELEE, sg->skill_lv, tick, 0);
			break;

		case UNT_THORNS_TRAP:
			if( tsc )
			{
				if( !sg->val2 )
				{
					int sec = skill_get_time2( sg->skill_id, sg->skill_lv );
					if( status_change_start(ss, bl, type, 10000, sg->skill_lv, 0, 0, 0, sec, 0 ) )
					{
						const struct TimerData* td = tsc->data[type] ? get_timer( tsc->data[type]->timer ) : NULL;
						if( td )
							sec = DIFF_TICK32(td->tick, tick);
						map_moveblock( bl, src->bl.x, src->bl.y, tick );
						clif_fixpos( bl );
						sg->val2 = bl->id;
					}
					else
						sec = 3000; // Couldn't trap it?
					sg->limit = DIFF_TICK32(tick, sg->tick) + sec;
				}
				else if( tsc->data[SC_THORNS_TRAP] && bl->id == sg->val2 )
					skill_attack( skill_get_type( GN_THORNS_TRAP ), ss, ss, bl, sg->skill_id, sg->skill_lv, tick, SD_LEVEL|SD_ANIMATION );
			}
			break;

		case UNT_DEMONIC_FIRE:
			{
				switch( sg->val2 )
				{
					case 1:
					case 2:
					default:
						sc_start(bl, SC_BURNING, 4 + 4 * sg->skill_lv, sg->skill_lv,skill_get_time2(sg->skill_id, sg->skill_lv));
						skill_attack(skill_get_type(sg->skill_id), ss, &src->bl, bl,sg->skill_id, sg->skill_lv + 10 * sg->val2, tick, 0);
						break;
					case 3:
						skill_attack(skill_get_type(CR_ACIDDEMONSTRATION), ss, &src->bl, bl,
							CR_ACIDDEMONSTRATION, sd ? pc_checkskill(sd, CR_ACIDDEMONSTRATION) : sg->skill_lv, tick, 0);
						break;
						
				}
			}
			break;

		case UNT_FIRE_EXPANSION_SMOKE_POWDER:
			sc_start(bl, SC_FIRE_EXPANSION_SMOKE_POWDER, 100, sg->skill_lv, 1000);
			break;

		case UNT_FIRE_EXPANSION_TEAR_GAS:
			sc_start(bl, SC_FIRE_EXPANSION_TEAR_GAS, 100, sg->skill_lv, 1000);
			break;

		case UNT_HELLS_PLANT:
			if (battle_check_target(&src->bl, bl, BCT_ENEMY) > 0)
				skill_attack(skill_get_type(GN_HELLS_PLANT_ATK), ss, &src->bl, bl, GN_HELLS_PLANT_ATK, sg->skill_lv, tick, 0);
			sg->limit = DIFF_TICK32(tick, sg->tick) + 100;
			break;

		case UNT_CLOUD_KILL:
			if(tsc && !tsc->data[type])
				status_change_start(ss,bl,type,10000,sg->skill_lv,sg->group_id,0,0,skill_get_time2(sg->skill_id,sg->skill_lv),8);
			skill_attack(skill_get_type(sg->skill_id),ss,&src->bl,bl,sg->skill_id,sg->skill_lv,tick,0);
 			break;

		case UNT_VACUUM_EXTREME:
			if ( tsc )
			{
				if ( !(tsc->data[SC_HALLUCINATIONWALK] || tsc->data[SC_HOVERING] || tsc->data[SC_VACUUM_EXTREME]) )
					sc_start(bl, SC_VACUUM_EXTREME, 100, sg->skill_lv, sg->limit - DIFF_TICK32(gettick(),sg->tick));

				// Move affected enemy's to the center of the AoE and make sure they stay there until status ends.
				if ( tsc->data[SC_VACUUM_EXTREME] && (bl->x != src->bl.x || bl->y != src->bl.y))
				{
					unit_movepos(bl, src->bl.x, src->bl.y, 0, 0);
					clif_fixpos(bl);
				}
			}
			break;

		case UNT_B_TRAP:
			sc_start(bl, SC_B_TRAP, 100, sg->skill_lv, skill_get_time2(sg->skill_id, sg->skill_lv));
			break;

		case UNT_MAKIBISHI:
			skill_attack(BF_WEAPON, ss, &src->bl, bl, sg->skill_id, sg->skill_lv, tick, 0);
			break;

		case UNT_ZENKAI_WATER:
		case UNT_ZENKAI_LAND:
		case UNT_ZENKAI_FIRE:
		case UNT_ZENKAI_WIND:
			if ( battle_check_target(&src->bl,bl,BCT_ENEMY) > 0 )
			{
				switch ( sg->unit_id )
				 {// Success change is 100% with default durations. But success is reduceable with stats.
					case UNT_ZENKAI_WATER:// However, there's no way to reduce success for deep sleep, frost, and crystalize. So its 100% for them always??? [Rytech]
						switch (rnd() % 3 + 1)
						{
							case 1: sc_start(bl, SC_FREEZE, 100, sg->skill_lv, 30000); break;
							case 2: sc_start(bl, SC_FROST, 100, sg->skill_lv, 40000); break;
							case 3: sc_start(bl, SC_CRYSTALIZE, 100, sg->skill_lv, 20000); break;
						}
						break;
					case UNT_ZENKAI_LAND:
						switch ( rnd()%2+1 )
						{
							case 1: sc_start(bl, SC_STONE, 100, sg->skill_lv, 20000); break;
							case 2: sc_start(bl, SC_POISON, 100, sg->skill_lv, 20000); break;
						}
						break;
					case UNT_ZENKAI_FIRE:
						sc_start(bl, SC_BURNING, 100, sg->skill_lv, 20000);
						break;
					case UNT_ZENKAI_WIND:
						switch ( rnd()%3+1 )
						{
							case 1: sc_start(bl, SC_SLEEP, 100, sg->skill_lv, 20000); break;
							case 2: sc_start(bl, SC_SILENCE, 100, sg->skill_lv, 20000); break;
							case 3: sc_start(bl, SC_DEEPSLEEP, 100, sg->skill_lv, 20000); break;
						}
						break;
				}
			}
			else
			{
				signed char element = 0;// For weapon element check.

				switch ( sg->unit_id )
				{
					case UNT_ZENKAI_WATER:
						element = ELE_WATER;
						break;
					case UNT_ZENKAI_LAND:
						element = ELE_EARTH;
						break;
					case UNT_ZENKAI_FIRE:
						element = ELE_FIRE;
						break;
					case UNT_ZENKAI_WIND:
						element = ELE_WIND;
						break;
				}

				sc_start2(bl, type, 100, sg->skill_lv, element, skill_get_time2(sg->skill_id,sg->skill_lv));
			}
			break;

		case UNT_MAGMA_ERUPTION:
			skill_attack(skill_get_type(NC_MAGMA_ERUPTION_DOTDAMAGE), ss, &src->bl, bl, NC_MAGMA_ERUPTION_DOTDAMAGE, sg->skill_lv, tick, 0);
			break;

		case UNT_POISON_MIST:
		case UNT_LAVA_SLIDE:
			skill_attack(skill_get_type(sg->skill_id),ss,&src->bl,bl,sg->skill_id,sg->skill_lv,tick,0);
			break;
		}

	if (bl->type == BL_MOB && ss != bl)
		mobskill_event((TBL_MOB*)bl, ss, tick, MSC_SKILLUSED|(skill_id<<16));

	return skill_id;
}
/*==========================================
 * Triggered when a char steps out of a skill unit
 * @param src Skill unit from char moved out
 * @param bl Char
 * @param tick
 *------------------------------------------*/
int skill_unit_onout (struct skill_unit *src, struct block_list *bl, int64 tick)
{
	struct skill_unit_group *sg;
	struct status_change *sc;
	struct status_change_entry *sce;
	enum sc_type type;

	nullpo_ret(src);
	nullpo_ret(bl);
	nullpo_ret(sg=src->group);
	sc = status_get_sc(bl);
	type = status_skill2sc(sg->skill_id);
	sce = (sc && type != -1)?sc->data[type]:NULL;

	if( bl->prev==NULL ||
		(status_isdead(bl) && sg->unit_id != UNT_ANKLESNARE && sg->unit_id != UNT_ELECTRICSHOCKER && sg->unit_id != UNT_THORNS_TRAP)) //Need to delete the trap if the source died.
		return 0;

	switch(sg->unit_id)
	{
		case UNT_SAFETYWALL:
		case UNT_PNEUMA:
		case UNT_EPICLESIS:
		if (sce)
			status_change_end(bl, type, INVALID_TIMER);
		break;

		case UNT_BASILICA:
			if (sce && sce->val4 != bl->id)
				status_change_end(bl, type, INVALID_TIMER);
			break;

		case UNT_DISSONANCE:
		case UNT_UGLYDANCE: //Used for updating timers in song overlap instances
		{
			short i;
			for (i = BA_WHISTLE; i <= DC_SERVICEFORYOU; i++) {
				if (skill_get_inf2(i)&(INF2_SONG_DANCE)) {
					type = status_skill2sc(i);
					sce = (sc && type != -1) ? sc->data[type] : NULL;
					if (sce)
						return i;
				}
			}
		}

		case UNT_WHISTLE:
		case UNT_ASSASSINCROSS:
		case UNT_POEMBRAGI:
		case UNT_APPLEIDUN:
		case UNT_HUMMING:
		case UNT_DONTFORGETME:
		case UNT_FORTUNEKISS:
		case UNT_SERVICEFORYOU:
			if (sg->src_id == bl->id && !(sc && sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_BARDDANCER))
				return -1;

		case UNT_HERMODE:	//Clear Hermode if the owner moved.
			if (sce && sce->val3 == BCT_SELF && sce->val4 == sg->group_id)
				status_change_end(bl, type, INVALID_TIMER);
			break;

		case UNT_SPIDERWEB:
		case UNT_THORNS_TRAP:
			{
				struct block_list *target = map_id2bl(sg->val2);
				if (target && target==bl)
				{
					if (sce && sce->val3 == sg->group_id)
						status_change_end(bl, type, INVALID_TIMER);
					sg->limit = DIFF_TICK32(tick, sg->tick) + 1000;
				}
				break;
			}
	}
	return sg->skill_id;
}

/*==========================================
 * Triggered when a char steps out of a skill group (entirely) [Skotlex]
 *------------------------------------------*/
int skill_unit_onleft (uint16 skill_id, struct block_list *bl, int64 tick)
{
	struct status_change *sc;
	struct status_change_entry *sce;
	enum sc_type type;

	sc = status_get_sc(bl);
	if (sc && !sc->count)
		sc = NULL;

	type = status_skill2sc(skill_id);
	sce = (sc && type != -1)?sc->data[type]:NULL;

	switch (skill_id)
	{
		case WZ_QUAGMIRE:
			if (bl->type==BL_MOB)
				break;
			if (sce)
				status_change_end(bl, type, INVALID_TIMER);
			break;

		case BD_LULLABY:
		case BD_RICHMANKIM:
		case BD_ETERNALCHAOS:
		case BD_DRUMBATTLEFIELD:
		case BD_RINGNIBELUNGEN:
		case BD_ROKISWEIL:
		case BD_INTOABYSS:
		case BD_SIEGFRIED:
			if(sc && sc->data[SC_DANCING] && (sc->data[SC_DANCING]->val1&0xFFFF) == skill_id)
			{	//Check if you just stepped out of your ensemble skill to cancel dancing. [Skotlex]
				//We don't check for SC_LONGING because someone could always have knocked you back and out of the song/dance.
				//FIXME: This code is not perfect, it doesn't checks for the real ensemble's owner,
				//it only checks if you are doing the same ensemble. So if there's two chars doing an ensemble
				//which overlaps, by stepping outside of the other parther's ensemble will cause you to cancel
				//your own. Let's pray that scenario is pretty unlikely and noone will complain too much about it.
				status_change_end(bl, SC_DANCING, INVALID_TIMER);
			}
		case MG_SAFETYWALL:
		case AL_PNEUMA:
		case SA_VOLCANO:
		case SA_DELUGE:
		case SA_VIOLENTGALE:
		case CG_HERMODE:
		case HW_GRAVITATION:
		case HP_BASILICA:
		case NJ_SUITON:
		case NC_NEUTRALBARRIER:
		case NC_STEALTHFIELD:
		case SC_MAELSTROM:
		case SC_BLOODYLUST:
		case LG_BANDING:
		case SO_WARMER:
		case SO_FIRE_INSIGNIA:
		case SO_WATER_INSIGNIA:
		case SO_WIND_INSIGNIA:
		case SO_EARTH_INSIGNIA:
		case SJ_BOOKOFCREATINGSTAR:
		case SU_CN_POWDERING:
		case SU_NYANGGRASS:
		case MH_STEINWAND:
		case EL_WATER_BARRIER:
		case EL_ZEPHYR:
		case EL_POWER_OF_GAIA:
			if (sce)
				status_change_end(bl, type, INVALID_TIMER);
			break;
		case BA_DISSONANCE:
		case DC_UGLYDANCE: //Used for updating song timers in overlap instances
			{
				short i;
				for(i = BA_WHISTLE; i <= DC_SERVICEFORYOU; i++){
					if(skill_get_inf2(i)&(INF2_SONG_DANCE)){
						type = status_skill2sc(i);
						sce = (sc && type != -1)?sc->data[type]:NULL;
						if(sce && !sce->val4){ //We don't want dissonance updating this anymore
							delete_timer(sce->timer, status_change_timer);
							sce->val4 = 1; //Store the fact that this is a "reduced" duration effect.
							sce->timer = add_timer(tick+skill_get_time2(i,1), status_change_timer, bl->id, type);
						}
					}
				}
			}
			break;
		case BA_POEMBRAGI:
		case BA_WHISTLE:
		case BA_ASSASSINCROSS:
		case BA_APPLEIDUN:
		case DC_HUMMING:
		case DC_DONTFORGETME:
		case DC_FORTUNEKISS:
		case DC_SERVICEFORYOU:
			if (sce)
			{
				delete_timer(sce->timer, status_change_timer);
				//NOTE: It'd be nice if we could get the skill_lv for a more accurate extra time, but alas...
				//not possible on our current implementation.
				sce->val4 = 1; //Store the fact that this is a "reduced" duration effect.
				sce->timer = add_timer(tick + skill_get_time2(skill_id, 1), status_change_timer, bl->id, type);
			}
			break;
		case PF_FOGWALL:
			if (sce)
			{
				status_change_end(bl, type, INVALID_TIMER);
				if ((sce=sc->data[SC_BLIND]))
				{
					if (bl->type == BL_PC) //Players get blind ended inmediately, others have it still for 30 secs. [Skotlex]
						status_change_end(bl, SC_BLIND, INVALID_TIMER);
					else {
						delete_timer(sce->timer, status_change_timer);
						sce->timer = add_timer(30000+tick, status_change_timer, bl->id, SC_BLIND);
					}
				}
			}
			break;
		case GD_LEADERSHIP:
		case GD_GLORYWOUNDS:
		case GD_SOULCOLD:
		case GD_HAWKEYES:
			if (!(sce && sce->val4))
				status_change_end(bl, type, INVALID_TIMER);
			break;
	}

	return skill_id;
}

/*==========================================
 * Invoked when a unit cell has been placed/removed/deleted.
 * flag values:
 * flag&1: Invoke onplace function (otherwise invoke onout)
 * flag&4: Invoke a onleft call (the unit might be scheduled for deletion)
 * flag&8: Recursive
 *------------------------------------------*/
static int skill_unit_effect (struct block_list* bl, va_list ap)
{
	struct skill_unit* unit = va_arg(ap,struct skill_unit*);
	struct skill_unit_group* group = unit->group;
	int64 tick = va_arg(ap, int64);
	unsigned int flag = va_arg(ap,unsigned int);
	int skill_id;
	bool dissonance = false;
	bool isTarget = false;

	if( (!unit->alive && !(flag&4)) || bl->prev == NULL )
		return 0;

	nullpo_ret(group);

	if( !(flag&8) ) {
		dissonance = skill_dance_switch(unit, 0);
		//Target-type check.
		isTarget = group->bl_flag & bl->type && battle_check_target( &unit->bl, bl, group->target_flag ) > 0;
	}

	//Necessary in case the group is deleted after calling on_place/on_out [Skotlex]
	skill_id = group->skill_id;

	//Target-type check.
  	if( isTarget ){
 		if( flag&1 )
 			skill_unit_onplace(unit,bl,tick);
		else {
			if (skill_unit_onout(unit, bl, tick) == -1)
				return 0; // Don't let a Bard/Dancer update their own song timer
		}
 
 		if( flag&4 )
 			skill_unit_onleft(skill_id, bl, tick);
 	}else if( !isTarget && flag&4 && ( group->state.song_dance&0x1 || ( group->src_id == bl->id && group->state.song_dance&0x2 ) ) ){
 		skill_unit_onleft(skill_id, bl, tick);//Ensemble check to terminate it.
 	}

	if( dissonance ) {
 		skill_dance_switch(unit, 1);
		//we placed a dissonance, let's update
		map_foreachincell(skill_unit_effect,unit->bl.m,unit->bl.x,unit->bl.y,group->bl_flag,&unit->bl,gettick(),4|8);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int64 skill_unit_ondamaged (struct skill_unit *src, struct block_list *bl, int64 damage)
{
	struct skill_unit_group *sg;

	nullpo_ret(src);
	nullpo_ret(sg=src->group);

	switch( sg->unit_id )
	{
	case UNT_SKIDTRAP:
	case UNT_LANDMINE:
	case UNT_SHOCKWAVE:
	case UNT_SANDMAN:
	case UNT_FLASHER:
	case UNT_CLAYMORETRAP:
	case UNT_FREEZINGTRAP:
	case UNT_TALKIEBOX:
	case UNT_ANKLESNARE:
	case UNT_ICEWALL:
	case UNT_REVERBERATION:
	//case UNT_POEMOFNETHERWORLD:// Need a confirm on if this can be damaged.
	case UNT_WALLOFTHORN:
		src->val1-= (int)cap_value(damage, INT_MIN, INT_MAX);
		break;
	case UNT_BLASTMINE:
		skill_blown(bl, &src->bl, 3, -1, 0);
		break;
	default:
		damage = 0;
		break;
	}
	return damage;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_check_condition_char_sub (struct block_list *bl, va_list ap)
{
	int *c, skill_id, lv, inf2;
	struct block_list *src;
	struct map_session_data *sd;
	struct map_session_data *tsd;
	int *p_sd;	//Contains the list of characters found.

	nullpo_ret(bl);
	nullpo_ret(tsd=(struct map_session_data*)bl);
	nullpo_ret(src=va_arg(ap,struct block_list *));
	nullpo_ret(sd=(struct map_session_data*)src);

	c=va_arg(ap,int *);
	p_sd = va_arg(ap, int *);
	skill_id = va_arg(ap,int);
	lv = va_arg(ap,int);
	inf2 = skill_get_inf2(skill_id);

	if (skill_id == PR_BENEDICTIO) {
		if (*c >= 2) // Check for two companions for Benedictio. [Skotlex]
			return 0;
	}
	else if ((inf2&INF2_CHORUS_SKILL || skill_id == WL_COMET)) {
		if (*c == MAX_PARTY) // Check for partners for Chorus or Comet; Cap if the entire party is accounted for.
			return 0;
	}
	else if (*c >= 1) // Check for one companion for all other cases.
		return 0;

	if (bl == src)
		return 0;

	if(pc_isdead(tsd))
		return 0;

	if (tsd->sc.data[SC_SILENCE] || tsd->sc.data[SC_DEEPSLEEP] || tsd->sc.data[SC_CRYSTALIZE] || (tsd->sc.opt1 && tsd->sc.opt1 != OPT1_BURNING))
		return 0;

	if(inf2&INF2_CHORUS_SKILL )
	{
		if (tsd->status.party_id && sd->status.party_id &&
			tsd->status.party_id == sd->status.party_id && 
			(tsd->class_&MAPID_THIRDMASK) == MAPID_MINSTRELWANDERER || (tsd->class_&MAPID_THIRDMASK) == MAPID_MINSTRELWANDERER_T )
			p_sd[(*c)++] = tsd->bl.id;
		return 1;
	}
	else
		switch(skill_id)
		{
			case PR_BENEDICTIO:
			{
				int dir = map_calc_dir(&sd->bl,tsd->bl.x,tsd->bl.y);
				dir = (unit_getdir(&sd->bl) + dir)%8; //This adjusts dir to account for the direction the sd is facing.
			if( (tsd->class_&MAPID_BASEMASK) == MAPID_ACOLYTE && (dir == 2 || dir == 6) //Must be standing to the left/right of Priest.
				&& tsd->status.sp >= 10 )
					p_sd[(*c)++]=tsd->bl.id;
				return 1;
			}
		case AB_ADORAMUS:
			{// Does not consume a blue gemstone if a Acolyte type job is standing next to the caster.
				if ((tsd->class_&MAPID_BASEMASK) == MAPID_ACOLYTE && tsd->status.sp >= 2 * lv)
				p_sd[(*c)++] = tsd->bl.id;
			return 1;
		}
		case WL_COMET:
		{// Does not consume a red gemstone if a Warlock is standing next to the caster.
			if ((tsd->class_&MAPID_THIRDMASK) == MAPID_WARLOCK)
				p_sd[(*c)++] = tsd->bl.id;
			return 1;
		}
		case LG_RAYOFGENESIS:
		{
			if (sd->status.party_id == tsd->status.party_id && (tsd->class_&MAPID_THIRDMASK) == MAPID_ROYAL_GUARD && tsd->sc.data[SC_BANDING])
				p_sd[(*c)++] = tsd->bl.id;
			return 1;
		}
			default: //Warning: Assuming Ensemble Dance/Songs for code speed. [Skotlex]
				{
					int skill_lv;
					if(pc_issit(tsd) || !unit_can_move(&tsd->bl))
						return 0;
					if (sd->status.sex != tsd->status.sex &&
							(tsd->class_&MAPID_UPPERMASK) == MAPID_BARDDANCER &&
							(skill_lv = pc_checkskill(tsd, skill_id)) > 0 &&
							(tsd->weapontype1==W_MUSICAL || tsd->weapontype1==W_WHIP) &&
							sd->status.party_id && tsd->status.party_id &&
							sd->status.party_id == tsd->status.party_id &&
							!tsd->sc.data[SC_DANCING])
					{
						p_sd[(*c)++]=tsd->bl.id;
						return skill_lv;
					} else {
						return 0;
					}
				}
				break;
		}
	return 0;
}

/*==========================================
 * Checks and stores partners for ensemble skills [Skotlex]
 *------------------------------------------*/
int skill_check_pc_partner(struct map_session_data *sd, short skill_id, short* skill_lv, int range, int cast_flag){
	static int c=0;
	static int p_sd[MAX_PARTY];
	int i;

	if (!sd)
		return 0;

	if( !battle_config.player_skill_partner_check || (battle_config.gm_skilluncond && pc_isGM(sd) >= battle_config.gm_skilluncond) ) {
		if( skill_get_inf2(skill_id)&INF2_CHORUS_SKILL )
			return MAX_PARTY; // To avoid extremly high effects in skills that use the amount of party members as damage multiplier.
		else
			return 99; //As if there were infinite partners.
	}

	if( cast_flag == 0 || cast_flag == 2 )
	{ // Search for Partners
		c = 0;
		memset(p_sd, 0, sizeof(p_sd));
		i = map_foreachinrange(skill_check_condition_char_sub, &sd->bl, range, BL_PC, &sd->bl, &c, &p_sd, skill_id);

		if( skill_id != PR_BENEDICTIO && skill_id != AB_ADORAMUS && skill_id != WL_COMET ) //Apply the average lv to encore skills.
			*skill_lv = (i+(*skill_lv))/(c+1); //I know c should be one, but this shows how it could be used for the average of n partners.
	}

	if( cast_flag == 1 || cast_flag == 2 )
	{ // Execute the Skill on Partners
		struct map_session_data* tsd;
			switch( skill_id )
			{
				case PR_BENEDICTIO:
					for (i = 0; i < c; i++)
					{
						if ((tsd = map_id2sd(p_sd[i])) != NULL)
							status_charge(&tsd->bl, 0, 10);
					}
					break;
				case AB_ADORAMUS:
					if( c > 0 && (tsd = map_id2sd(p_sd[0])) != NULL ) {
						i = 2 * (*skill_lv);
						status_charge(&tsd->bl, 0, i);
					}
					break;
				default: //Warning: Assuming Ensemble skills here (for speed)
					if( c > 0 && sd->sc.data[SC_DANCING] && (tsd = map_id2sd(p_sd[0])) != NULL )
					{
						sd->sc.data[SC_DANCING]->val4 = tsd->bl.id;
						sc_start4(&tsd->bl,SC_DANCING,100,skill_id,sd->sc.data[SC_DANCING]->val2,*skill_lv,sd->bl.id,skill_get_time(skill_id,*skill_lv)+1000);
						clif_skill_nodamage(&tsd->bl, &sd->bl, skill_id, *skill_lv, 1);
						tsd->skillid_dance = skill_id;
						tsd->skilllv_dance = *skill_lv;
					}
				break;
			}
	}
	return c;
}

/*==========================================
 *
 *------------------------------------------*/
static int skill_check_condition_mob_master_sub (struct block_list *bl, va_list ap)
{
	int *c,src_id,mob_class,skill;
	struct mob_data *md;

	md=(struct mob_data*)bl;
	src_id=va_arg(ap,int);
	mob_class=va_arg(ap,int);
	skill=va_arg(ap,int);
	c=va_arg(ap,int *);

	if( md->master_id != src_id || md->special_state.ai != (unsigned)(skill == AM_SPHEREMINE?2:3) )
		return 0; //Non alchemist summoned mobs have nothing to do here.

	if(md->mob_id==mob_class)
		(*c)++;

	return 1;
}

/*==========================================
 * Determines if a given skill should be made to consume ammo
 * when used by the player. [Skotlex]
 *------------------------------------------*/
int skill_isammotype (struct map_session_data *sd, int skill)
{
	return (
		battle_config.arrow_decrement==2 &&
		(sd->status.weapon == W_BOW || (sd->status.weapon >= W_REVOLVER && sd->status.weapon <= W_GRENADE)) &&
		skill != HT_PHANTASMIC &&
		skill_get_type(skill) == BF_WEAPON &&
	  	!(skill_get_nk(skill)&NK_NO_DAMAGE) &&
		!skill_get_spiritball(skill,1) //Assume spirit spheres are used as ammo instead.
	);
}

int skill_check_condition_castbegin(struct map_session_data* sd, uint16 skill_id, uint16 skill_lv)
{
	struct status_data *status;
	struct status_change *sc;
	struct skill_condition require;
	int i;
	int inf2;

	nullpo_ret(sd);

	if (sd->chatID) return 0;

	if( battle_config.gm_skilluncond && pc_isGM(sd)>= battle_config.gm_skilluncond && sd->skillitem != skill_id )
	{	//GMs don't override the skillItem check, otherwise they can use items without them being consumed! [Skotlex]	
		sd->state.arrow_atk = skill_get_ammotype(skill_id)?1:0; //Need to do arrow state check.
		sd->spiritball_old = sd->spiritball; //Need to do Spiritball check.
		sd->rageball_old = sd->rageball;// Needed for Rageball check.
		sd->charmball_old = sd->charmball;// Needed for Charmball check.
		sd->soulball_old = sd->soulball; //Need to do Soulball check.
		return 1;
	}

	switch( sd->menuskill_id ){
		case AM_PHARMACY:
			switch( skill_id ){
				case AM_PHARMACY:
				case AC_MAKINGARROW:
				case BS_REPAIRWEAPON:
				case AM_TWILIGHT1:
				case AM_TWILIGHT2:
				case AM_TWILIGHT3:
					return 0;
			}
			break;
		case GN_MIX_COOKING:
		case GN_MAKEBOMB:
		case GN_S_PHARMACY:
		case GN_CHANGEMATERIAL:
			if( sd->menuskill_id != skill_id )
				return 0;
			break;
	}

	status = &sd->battle_status;
	sc = &sd->sc;
	if( !sc->count )
		sc = NULL;

	if( sd->skillitem == skill_id )
	{
		if( sd->state.abra_flag ) // Hocus-Pocus was used. [Inkfish]
			sd->state.abra_flag = 0;
		else if( sd->state.improv_flag )
			sd->state.improv_flag = 0;
		else if( sd->state.magicmushroom_flag )
			sd->state.magicmushroom_flag = 0;
		else
		{ // When a target was selected, consume items that were skipped in pc_use_item [Skotlex]
			if( (i = sd->itemindex) == -1 ||
				sd->inventory.u.items_inventory[i].nameid != sd->itemid ||
				sd->inventory_data[i] == NULL ||
				!sd->inventory_data[i]->flag.delay_consume ||
				sd->inventory.u.items_inventory[i].amount < 1
				)
			{	//Something went wrong, item exploit?
				sd->itemid = 0;
				sd->itemindex = -1;
				return 0;
			}
			//Consume
			sd->itemid = 0;
			sd->itemindex = -1;
			if( skill_id == WZ_EARTHSPIKE && sc && sc->data[SC_EARTHSCROLL] && rnd()%100 > sc->data[SC_EARTHSCROLL]->val2 ) // [marquis007]
				; //Do not consume item.
			else if( sd->inventory.u.items_inventory[i].expire_time == 0 )
				pc_delitem(sd,i,1,0,0,LOG_TYPE_CONSUME); // Rental usable items are not consumed until expiration
		}
		if (!sd->skillitem_keep_requirement)
			return 1;
	}

	if( pc_is90overweight(sd) )
	{
		clif_skill_fail(sd,skill_id,USESKILL_FAIL_WEIGHTOVER,0,0);
		return 0;
	}

	if( sc )
	{ 
		if( sc->data[SC_SPELLFIST] )
			status_change_end( &sd->bl, SC_SPELLFIST, INVALID_TIMER );
	}

	switch( skill_id )
	{ // Turn off check.
		case BS_MAXIMIZE:		case NV_TRICKDEAD:	case TF_HIDING:			case AS_CLOAKING:		case CR_AUTOGUARD:
		case ML_AUTOGUARD:		case CR_DEFENDER:	case ML_DEFENDER:		case ST_CHASEWALK:		case PA_GOSPEL:
		case CR_SHRINK:			case TK_RUN:		case GS_GATLINGFEVER:	case TK_READYCOUNTER:	case TK_READYDOWN:
		case TK_READYSTORM:		case TK_READYTURN:	case SG_FUSION:			case SJ_LUNARSTANCE:	case SJ_STARSTANCE:
		case SJ_UNIVERSESTANCE:	case SJ_SUNSTANCE:	case SP_SOULCOLLECT:	case KO_YAMIKUMO:		case SU_HIDE:
			if( sc && sc->data[status_skill2sc(skill_id)] )
				return 1;
	}

	if( skill_lv < 1 || skill_lv > MAX_SKILL_LEVEL )
		return 0;

	require = skill_get_requirement( sd, skill_id, skill_lv );

	//Can only update state when weapon/arrow info is checked.
	sd->state.arrow_atk = require.ammo ? 1 : 0;

	// perform skill-group checks
	inf2 = skill_get_inf2(skill_id);
	if (inf2&INF2_CHORUS_SKILL) {
		if (skill_check_pc_partner(sd, skill_id, &skill_lv, AREA_SIZE, 0) < 1) {
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
			return 0;
		}
	}
	else if (inf2&INF2_ENSEMBLE_SKILL) {
		if (skill_check_pc_partner(sd, skill_id, &skill_lv, 1, 0) < 1) {
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
			return 0;
		}
	}

	// perform skill-specific checks (and actions)
	switch( skill_id )
	{
		case MC_VENDING:
		case ALL_BUYING_STORE:
			if (map[sd->bl.m].flag.novending) {
				clif_displaymessage(sd->fd, msg_txt(sd,276)); // "You can't open a shop on this map"
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
				return 0;
			}
			if (map_getcell(sd->bl.m, sd->bl.x, sd->bl.y, CELL_CHKNOVENDING)) {
				clif_displaymessage(sd->fd, msg_txt(sd,204)); // "You can't open a shop on this cell."
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
				return 0;
			}
			break;
		case SA_CASTCANCEL:
		case SO_SPELLFIST:
			if( sd->ud.skilltimer == INVALID_TIMER) 
			{
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
				return 0;
			}
			break;
		case AS_CLOAKING:
		{
			static int dx[] = { 0, 1, 0, -1, -1,  1, 1, -1 };
			static int dy[] = { -1, 0, 1,  0, -1, -1, 1,  1 };

			if (skill_lv < 3 && ((sd->bl.type == BL_PC && battle_config.pc_cloak_check_type & 1)
				|| (sd->bl.type != BL_PC && battle_config.monster_cloak_check_type & 1))) { //Check for walls.
				int i;
				ARR_FIND(0, 8, i, map_getcell(sd->bl.m, sd->bl.x + dx[i], sd->bl.y + dy[i], CELL_CHKNOPASS) != 0);
				if (i == 8) {
					clif_skill_fail(sd, skill_id, USESKILL_FAIL_LEVEL, 0, 0);
					return 0;
				}
			}
			break;
		}
		case AL_WARP:
			if(!battle_config.duel_allow_teleport && sd->duel_group) { // duel restriction [LuzZza]
				char output[128]; sprintf(output, msg_txt(sd,365), skill_get_name(AL_WARP));
				clif_displaymessage(sd->fd, output); //"Duel: Can't use %s in duel."
				return 0;
			}
			break;
		case AL_HOLYWATER:
			if(pc_search_inventory(sd,ITEMID_EMPTY_BOTTLE) < 0) {
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
				return false;
			}
			break;
		case MO_CALLSPIRITS:
			{
				if (sc && sc->data[SC_RAISINGDRAGON])
					skill_lv = sc->data[SC_RAISINGDRAGON]->val1 + skill_lv;
				if (sd->spiritball >= skill_lv)
				{
					clif_skill_fail(sd, skill_id, 0, 0, 0);
					return 0;
				}
			}
			break;
	case MO_FINGEROFFENSIVE:
	case GS_FLING:
	case SR_RAMPAGEBLASTER:
	case SR_RIDEINLIGHTNING:
	case RL_P_ALTER:
		if ( skill_id == RL_P_ALTER && !itemid_is_holy_bullet(sd->inventory.u.items_inventory[sd->equip_index[EQI_AMMO]].nameid) )
		{
			clif_skill_fail(sd,skill_id,0,0,0);
			return 0;
		}
	case RL_HEAT_BARREL:
	case RL_HAMMER_OF_GOD:
	case RL_E_CHAIN:
		if( sd->spiritball > 0 && sd->spiritball < require.spiritball )
			sd->spiritball_old = require.spiritball = sd->spiritball;
		else
			sd->spiritball_old = require.spiritball;
		break;
	case MO_CHAINCOMBO:
		if(!sc)
			return 0;
		if(sc->data[SC_BLADESTOP])
			break;
		if(sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == MO_TRIPLEATTACK)
			break;
		return 0;
	case MO_COMBOFINISH:
		if(!(sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == MO_CHAINCOMBO))
			return 0;
		break;
	case CH_TIGERFIST:
		if(!(sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == MO_COMBOFINISH))
			return 0;
		break;
	case CH_CHAINCRUSH:
		if(!(sc && sc->data[SC_COMBO]))
			return 0;
		if(sc->data[SC_COMBO]->val1 != MO_COMBOFINISH && sc->data[SC_COMBO]->val1 != CH_TIGERFIST)
			return 0;
		break;
	case SR_DRAGONCOMBO:
		//Dragon Combo can be used normally, but can also be used in a combo.
		//If used in a combo, it must only work if comboed after Triple Attack.
		if(sc && sc->data[SC_COMBO] && !(sc->data[SC_COMBO]->val1 == MO_TRIPLEATTACK))
			return 0;
		break;
	case SR_FALLENEMPIRE:
		if(!(sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == SR_DRAGONCOMBO))
			return 0;
		break;
	case SJ_SOLARBURST:
		if(!(sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == SJ_PROMINENCEKICK))
			return 0;
		break;
	case MO_EXTREMITYFIST:
//		if(sc && sc->data[SC_EXTREMITYFIST]) //To disable Asura during the 5 min skill block uncomment this...
//			return 0;
		if( sc && sc->data[SC_BLADESTOP] )
			break;
		if( sc && sc->data[SC_COMBO] )
		{
			switch(sc->data[SC_COMBO]->val1) {
				case MO_COMBOFINISH:
				case CH_TIGERFIST:
				case CH_CHAINCRUSH:
					break;
				default:
					return 0;
			}
		}
		else if( !unit_can_move(&sd->bl) )
	  	{	//Placed here as ST_MOVE_ENABLE should not apply if rooted or on a combo. [Skotlex]
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;

	case TK_MISSION:
		if( (sd->class_&MAPID_UPPERMASK) != MAPID_TAEKWON )
		{// Cannot be used by Non-Taekwon classes
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;

	case TK_READYCOUNTER:
	case TK_READYDOWN:
	case TK_READYSTORM:
	case TK_READYTURN:
	case TK_JUMPKICK:
		if( (sd->class_&MAPID_UPPERMASK) == MAPID_SOUL_LINKER )
		{// Soul Linkers cannot use this skill
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;

	case TK_TURNKICK:
	case TK_STORMKICK:
	case TK_DOWNKICK:
	case TK_COUNTER:
		if ((sd->class_&MAPID_UPPERMASK) == MAPID_SOUL_LINKER)
			return 0; //Anti-Soul Linker check in case you job-changed with Stances active.
		if(!(sc && sc->data[SC_COMBO]) || sc->data[SC_COMBO]->val1 == TK_JUMPKICK)
			return 0; //Combo needs to be ready

		if (sc->data[SC_COMBO]->val3)
		{	//Kick chain
			//Do not repeat a kick.
			if (sc->data[SC_COMBO]->val3 != skill_id)
				break;
			status_change_end(&sd->bl, SC_COMBO, INVALID_TIMER);
			return 0;
		}
		if (sc->data[SC_COMBO]->val1 != skill_id && !(sd && sd->status.base_level >= 90 && pc_famerank(sd->status.char_id, MAPID_TAEKWON)))	//Cancel combo wait.
		{	//Cancel combo wait.
			unit_cancel_combo(&sd->bl);
			return 0;
		}
		break; //Combo ready.
	case BD_ADAPTATION:
		{
			int time;
			if(!(sc && sc->data[SC_DANCING]))
			{
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
				return 0;
			}
			time = 1000*(sc->data[SC_DANCING]->val3>>16);
			if (skill_get_time(
				(sc->data[SC_DANCING]->val1&0xFFFF), //Dance Skill ID
				(sc->data[SC_DANCING]->val1>>16)) //Dance Skill LV
				- time < skill_get_time2(skill_id,skill_lv))
			{
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
				return 0;
			}
		}
		break;

	case PR_BENEDICTIO:
		if (skill_check_pc_partner(sd, skill_id, &skill_lv, 1, 0) < 2)
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;

	case SL_SMA:
		if(!(sc && (sc->data[SC_SMA] || sc->data[SC_USE_SKILL_SP_SHA])))
			return 0;
		break;

	case SP_SWHOO:
		if(!(sc && sc->data[SC_USE_SKILL_SP_SPA]))
			return 0;
		break;

	case HT_POWER:
	case RL_QD_SHOT:
		if(!(sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == AC_DOUBLE))
			return 0;
		break;

	case CG_HERMODE:
		if(!npc_check_areanpc(1,sd->bl.m,sd->bl.x,sd->bl.y,skill_get_splash(skill_id, skill_lv)))
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case CG_MOONLIT: //Check there's no wall in the range+1 area around the caster. [Skotlex]
		{
			int i,range = skill_get_splash(skill_id, skill_lv)+1;
			int size = range*2+1;
			for (i=0;i<size*size;i++) {
				int x = sd->bl.x+(i%size-range);
				int y = sd->bl.y+(i/size-range);
				if (map_getcell(sd->bl.m,x,y,CELL_CHKWALL)) {
					clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
					return 0;
				}
			}
		}
		break;
	case PR_REDEMPTIO:
		{
			int exp;
			if( ((exp = pc_nextbaseexp(sd)) > 0 && get_percentage(sd->status.base_exp, exp) < 1) ||
				((exp = pc_nextjobexp(sd)) > 0 && get_percentage(sd->status.job_exp, exp) < 1)) {
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0); //Not enough exp.
				return 0;
			}
			break;
		}
	case HP_BASILICA:
		if (!sc || (sc && !sc->data[SC_BASILICA])) {
			if (sd) {
				// When castbegin, needs 7x7 clear area
				int i, x, y, range = skill_get_unit_layout_type(skill_id, skill_lv) + 1;
				int size = range * 2 + 1;
				for (i = 0; i < size*size; i++) {
					x = sd->bl.x + (i%size - range);
					y = sd->bl.y + (i / size - range);
					if (map_getcell(sd->bl.m, x, y, CELL_CHKWALL)) {
						clif_skill_fail(sd, skill_id, USESKILL_FAIL, 0, 0);
						return 0;
					}
				}
				if (map_foreachinrange(skill_count_wos, &sd->bl, range, BL_ALL, &sd->bl)) {
					clif_skill_fail(sd, skill_id, USESKILL_FAIL, 0, 0);
					return 0;
				}
			}
		}
		break;
	case AM_TWILIGHT2:
	case AM_TWILIGHT3:
		if (!party_skill_check(sd, sd->status.party_id, skill_id, skill_lv))
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case SG_SUN_WARM:
	case SG_MOON_WARM:
	case SG_STAR_WARM:
		if (sc && sc->data[SC_MIRACLE])
			break;
		i = skill_id -SG_SUN_WARM;
		if (sd->bl.m == sd->feel_map[i].m)
			break;
		clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
		return 0;
		break;
	case SG_SUN_COMFORT:
	case SG_MOON_COMFORT:
	case SG_STAR_COMFORT:
		if (sc && sc->data[SC_MIRACLE])
			break;
		i = skill_id -SG_SUN_COMFORT;
		if (sd->bl.m == sd->feel_map[i].m &&
			(battle_config.allow_skill_without_day || sg_info[i].day_func()))
			break;
		clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
		return 0;
	case SG_FUSION:
		if (sc && sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_STAR)
			break;
		//Auron insists we should implement SP consumption when you are not Soul Linked. [Skotlex]
		//Only invoke on skill begin cast (instant cast skill). [Kevin]
		if( require.sp > 0 )
		{
			if (status->sp < (unsigned int)require.sp)
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_SP_INSUFFICIENT,0,0);
			else
				status_zap(&sd->bl, 0, require.sp);
		}
		return 0;
	case GD_BATTLEORDER:
	case GD_REGENERATION:
	case GD_RESTORE:
		if (!map_flag_gvg2(sd->bl.m)) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
	case GD_EMERGENCYCALL:
	case GD_ITEMEMERGENCYCALL:
		// other checks were already done in skillnotok()
		if (!sd->status.guild_id || !sd->state.gmaster_flag)
			return 0;
		break;

	case GS_GLITTERING:
		if(sd->spiritball >= 10) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;

	case AB_CONVENIO:
		{// Only castable if in a party. Leader check is done later.
			if (!sd->status.party_id)
			{
				clif_skill_fail(sd, skill_id,0,0,0);
				return 0;
			}
		}
		break;

	case NJ_ISSEN:
		if (status->hp < 2) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
	case NJ_BUNSINJYUTSU:
		if (!(sc && sc->data[SC_NEN])) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
	  	}
		break;

	case NJ_ZENYNAGE:
	case KO_MUCHANAGE:
		if(sd->status.zeny < require.zeny) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_MONEY,0,0);
			return 0;
		}
		break;
	case PF_HPCONVERSION:
		if (status->sp == status->max_sp)
			return 0; //Unusable when at full SP.
		break;
	case SP_KAUTE:// Fail if below 30% MaxHP.
		if (status->hp < 30 * status->max_hp / 100)
		{
			clif_skill_fail(sd, skill_id,0,0,0);
			return 0;
		}
		break;
	case AM_CALLHOMUN: //Can't summon if a hom is already out
		if (sd->status.hom_id && sd->hd && !sd->hd->homunculus.vaporize) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case AM_REST: //Can't vapo homun if you don't have an active homunc or it's hp is < 80%
		if (!hom_is_active(sd->hd) || sd->hd->battle_status.hp < (sd->hd->battle_status.max_hp*80/100))
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case RK_HUNDREDSPEAR:
	case RK_PHANTOMTHRUST://Well make this skill ask for a spear even tho its not official. It does require a spear type.
		if( require.weapon && !pc_check_weapontype(sd,require.weapon) ) 
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_NEED_EQUIPPED_WEAPON_CLASS,4,0);//Spear required.
			return 0;
		}
		break;
	case AB_ANCILLA:
		{
			short count = 0;
			for( i = 0; i < MAX_INVENTORY; i ++ )
				if( sd->inventory.u.items_inventory[i].nameid == ITEMID_ANCILLA )
					count = sd->inventory.u.items_inventory[i].amount;
			if( count >= 3 )
			{
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_ANCILLA_NUMOVER, 0, 0);
				return 0;
			}
		}
		break;
	case WL_COMET:
	case AB_ADORAMUS:
		if( skill_check_pc_partner(sd, skill_id,&skill_lv,1,0) <= 0 && ((i = pc_search_inventory(sd,require.itemid[0])) < 0 || sd->inventory.u.items_inventory[i].amount < require.amount[0]) )
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_NEED_ITEM,require.amount[0],require.itemid[0]);
 			return 0;
 		}
		break;
	case GC_HALLUCINATIONWALK:
		if( sc && (sc->data[SC_HALLUCINATIONWALK] || sc->data[SC_HALLUCINATIONWALK_POSTDELAY]) )
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case WL_SUMMONFB:
	case WL_SUMMONBL:
	case WL_SUMMONWB:
	case WL_SUMMONSTONE:
		if( sc )
		{
			ARR_FIND(SC_SUMMON1,SC_SUMMON5+1,i,!sc->data[i]);
			if( i == SC_SUMMON5+1 )
			{ // No more free slots
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_SPELLBOOK_PRESERVATION_POINT,0,0);
				return 0;
			}
		}
		break;
	case AB_LAUDAAGNUS:
	case AB_LAUDARAMUS:
		if( !sd->status.party_id )
		{
			clif_skill_fail(sd, skill_id,0,0,0);
			return 0;
		}
		break;
	case AB_HIGHNESSHEAL:
		if( sd && pc_checkskill(sd,AL_HEAL) == 0 ) {
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
				return 0;
		}
		break;
	case GC_COUNTERSLASH:
	case GC_WEAPONCRUSH:
		if( !(sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == GC_WEAPONBLOCKING) )	{
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_GC_WEAPONBLOCKING, 0, 0);
			return 0;
		}
		break;
	case GC_CROSSRIPPERSLASHER:
		if( !(sc && sc->data[SC_ROLLINGCUTTER]) )
		{
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_CONDITION, 0, 0);
			return 0;
		}
		break;
	case GC_POISONSMOKE:
	case GC_VENOMPRESSURE:
		if( !(sc && sc->data[SC_POISONINGWEAPON]) )
		{
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_GC_POISONINGWEAPON, 0, 0);
			return 0;
		}
		break;
	case RA_WUGMASTERY:
		if ((pc_isfalcon(sd) && !battle_config.falcon_and_wug) || sc && sc->data[SC__GROOMY])
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL,0,0);
			return 0;
		}
		break;
	case RA_WUGDASH:
		if(!pc_iswugrider(sd))
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL,0,0);
			return 0;
		}
		break;
	case RA_SENSITIVEKEEN:
		if(!pc_iswug(sd))
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_CONDITION,0,0);
			return 0;
		}
		break;
	case RA_WUGRIDER:
		if(pc_isfalcon(sd) || (!pc_iswugrider(sd) && !pc_iswug(sd)))
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_CONDITION,0,0);
			return 0;
		}
		break;
	case LG_BANDING:
		if (sc && sc->data[SC_INSPIRATION])
		{
			clif_skill_fail(sd, skill_id, 0, 0, 0);
			return 0;
		}
		break;
	case LG_PRESTIGE:
		if( sc && (sc->data[SC_BANDING] || sc->data[SC_INSPIRATION]) ){
			clif_skill_fail(sd, skill_id,0,0,0);
			return 0;
		}
		break;
	case LG_RAGEBURST:
		if( sd->rageball > 0 && sd->rageball < require.spiritball )
			sd->rageball_old = require.spiritball = sd->rageball;
		else
			sd->rageball_old = require.spiritball;
		break;
	case LG_RAYOFGENESIS:
		if (sc && sc->data[SC_INSPIRATION])
			return 1;	// Don't check for partner.
		if (!(sc && sc->data[SC_BANDING]))
		{
			clif_skill_fail(sd, skill_id, USESKILL_FAIL, 0, 0);
			return 0;
		}
		else if (skill_check_pc_partner(sd, skill_id, &skill_lv, skill_get_range(skill_id, skill_lv), 0) < 1)
			return 0; // Just fails, no msg here.
		break;
	case LG_HESPERUSLIT:
		if (!sc || !sc->data[SC_BANDING])
		{
			clif_skill_fail(sd, skill_id, 0, 0, 0);
			return 0;
		}
 		break;
	case SR_CRESCENTELBOW:
		if( sc && sc->data[SC_CRESCENTELBOW] ){
			clif_skill_fail(sd, skill_id,0,0,0);
			return 0;
		}
		break;
	case SR_GATEOFHELL:
		if( sd->spiritball > 0 )
			sd->spiritball_old = require.spiritball;
		break;
	case SC_MANHOLE:
	case SC_DIMENSIONDOOR:
		if( sc && sc->data[SC_MAGNETICFIELD] )
		{
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case WM_GREAT_ECHO:
		{
			int count;
			count = skill_check_pc_partner(sd, skill_id, &skill_lv, AREA_SIZE, 0);
			if( count < 1 )
			{
				clif_skill_fail(sd, skill_id,USESKILL_FAIL,0,0);
				return 0;
			}
			else
				require.sp -= require.sp * 20 * count / 100; //  -20% each W/M in the party.
			break;
		}
	case SO_FIREWALK:
	case SO_ELECTRICWALK:	// Can't be casted until you've walked all cells.
		if( sc && sc->data[SC_PROPERTYWALK] &&
			sc->data[SC_PROPERTYWALK]->val3 < skill_get_maxcount(sc->data[SC_PROPERTYWALK]->val1,sc->data[SC_PROPERTYWALK]->val2) ) {
			clif_skill_fail(sd, skill_id,0x0,0,0);
			return 0;
		}
		break;
	case SJ_FULLMOONKICK:
		if(!(sc && sc->data[SC_NEWMOON]))
		{
			clif_skill_fail(sd, skill_id,0,0,0);
			return 0;
		}
		break;
	case SJ_STAREMPEROR:
	case SJ_NOVAEXPLOSING:
	case SJ_GRAVITYCONTROL:
	case SJ_BOOKOFDIMENSION:
	case SJ_BOOKOFCREATINGSTAR:
	case SP_SOULDIVISION:
	case SP_SOULEXPLOSION:
		if (!map_flag_vs(sd->bl.m)) {
			clif_skill_fail(sd, skill_id,0,0,0);
			return 0;
		}
		break;
	case KO_KAHU_ENTEN:
	case KO_HYOUHU_HUBUKI:
	case KO_KAZEHU_SEIRAN:
	case KO_DOHU_KOUKAI:
		{
			short charm_type = 0;

			switch (skill_id)
			{
				case KO_KAHU_ENTEN:
					charm_type = CHARM_FIRE;
					break;
				case KO_HYOUHU_HUBUKI:
					charm_type = CHARM_WATER;
					break;
				case KO_KAZEHU_SEIRAN:
					charm_type = CHARM_WIND;
					break;
				case KO_DOHU_KOUKAI:
					charm_type = CHARM_EARTH;
					break;
			}

			// Summoning a different type resets your charmball count.
			// Can't have more then 10 of the same type.
			if(sd->charmball_type == charm_type && sd->charmball >= 10)
			{
				clif_skill_fail(sd, skill_id,0,0,0);
				return 0;
			}
		}
		break;
	case KO_KAIHOU:
	case KO_ZENKAI:
		if (sd->charmball > 0 && sd->charmball < require.spiritball)
			sd->charmball_old = require.spiritball = sd->charmball;
		else
			sd->charmball_old = require.spiritball;
		break;
	}

	switch(require.state) {
	case ST_HIDING:
		if(!(sc && sc->option&OPTION_HIDE)) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_CLOAKING:
		if(!pc_iscloaking(sd)) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_HIDDEN:
		if(!pc_ishiding(sd)) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_RIDING:
		if(!pc_isriding(sd) && !pc_isdragon(sd)) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_FALCON:
		if(!pc_isfalcon(sd)) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_CARTBOOST:
		if(!(sc && sc->data[SC_CARTBOOST])) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
	case ST_CART:
		if(!pc_iscarton(sd)) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_CART,0,0);
			return 0;
		}
		break;
	case ST_SHIELD:
		if(sd->status.shield <= 0) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_SIGHT:
		if(!(sc && sc->data[SC_SIGHT])) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_EXPLOSIONSPIRITS:
		if(!(sc && sc->data[SC_EXPLOSIONSPIRITS])) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_RECOV_WEIGHT_RATE:
		if(battle_config.natural_heal_weight_rate <= 100 && sd->weight*100/sd->max_weight >= (unsigned int)battle_config.natural_heal_weight_rate) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_MOVE_ENABLE:
		if (sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == skill_id)
			sd->ud.canmove_tick = gettick(); //When using a combo, cancel the can't move delay to enable the skill. [Skotlex]

		if (!unit_can_move(&sd->bl)) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_WATER:
		if (sc && (sc->data[SC_DELUGE] || sc->data[SC_SUITON]))
			break;
		if (map_getcell(sd->bl.m,sd->bl.x,sd->bl.y,CELL_CHKWATER))
			break;
		clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
		return 0;
	case ST_DRAGON:
		if (!pc_isdragon(sd)) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_DRAGON,0,0);
			return 0;
		}
		break;
	case ST_WUG:
		if (!pc_iswug(sd)) {
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_WUGRIDER:
		if(!pc_iswugrider(sd) && !pc_iswug(sd)){
			clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
			return 0;
		}
		break;
	case ST_MADOGEAR:
		if(!pc_ismadogear(sd)) {
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_MADOGEAR,0,0);
			return 0;
		}
		break;
	case ST_ELEMENTAL:
		if(!sd->ed) {
			clif_skill_fail(sd, skill_id, USESKILL_FAIL_EL_SUMMON, 0, 0);
			return 0;
		}
		break;
	case ST_SUNSTANCE:
		if (!(sc && (sc->data[SC_SUNSTANCE] || sc->data[SC_UNIVERSESTANCE]))) {
			clif_skill_fail(sd, skill_id,0,0,0);
			return 0;
		}
		break;
	case ST_MOONSTANCE:
		if (!(sc && (sc->data[SC_LUNARSTANCE] || sc->data[SC_UNIVERSESTANCE]))) {
			clif_skill_fail(sd, skill_id,0,0,0);
			return 0;
		}
		break;
	case ST_STARSTANCE:
		if (!(sc && (sc->data[SC_STARSTANCE] || sc->data[SC_UNIVERSESTANCE]))) {
			clif_skill_fail(sd, skill_id,0,0,0);
			return 0;
		}
		break;
	case ST_UNIVERSESTANCE:
		if(!(sc && sc->data[SC_UNIVERSESTANCE])) {
			clif_skill_fail(sd, skill_id,0,0,0);
			return 0;
		}
		break;
	}

	//check if equiped item
	if (require.eqItem_count) {
		for (i = 0; i < require.eqItem_count; i++) {
			t_itemid reqeqit = require.eqItem[i];
			if (!reqeqit) break; //no more required item get out of here
			if (!pc_checkequip2(sd, reqeqit)) {
				if (i == require.eqItem_count) {
					clif_skill_fail(sd, skill_id, USESKILL_FAIL_THIS_WEAPON, 0, 0);
					return false;
				}
			}
			else
				break; // Wearing an applicable item.
		}
	}

	if(require.mhp > 0 && get_percentage(status->hp, status->max_hp) > (unsigned int)require.mhp) {
		//mhp is the max-hp-requirement, that is,
		//you must have this % or less of HP to cast it.
		clif_skill_fail(sd, skill_id,USESKILL_FAIL_HP_INSUFFICIENT,0,0);
		return 0;
	}

	if (require.weapon && !pc_check_weapontype(sd, require.weapon)) {
		switch (skill_id) {
		case RA_AIMEDBOLT:
			break;
		default:
			switch ((unsigned int)log2(require.weapon)) {
			case W_REVOLVER:
				clif_msg(sd, SKILL_NEED_REVOLVER);
				break;
			case W_RIFLE:
				clif_msg(sd, SKILL_NEED_RIFLE);
				break;
			case W_GATLING:
				clif_msg(sd, SKILL_NEED_GATLING);
				break;
			case W_SHOTGUN:
				clif_msg(sd, SKILL_NEED_SHOTGUN);
				break;
			case W_GRENADE:
				clif_msg(sd, SKILL_NEED_GRENADE);
				break;
			default:
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_THIS_WEAPON, 0, 0);
				break;
			}
			return 0;
		}
	}

	if( require.sp > 0 && status->sp < (unsigned int)require.sp) {
		clif_skill_fail(sd, skill_id,USESKILL_FAIL_SP_INSUFFICIENT,0,0);
		return 0;
	}

	if( require.zeny > 0 && sd->status.zeny < require.zeny ) {
		clif_skill_fail(sd, skill_id,USESKILL_FAIL_MONEY,0,0);
		return 0;
	}

	if( require.spiritball > 0 )
	{// Skills that require certain types of spheres to use.
		switch (skill_id)
		{// Skills that require soul spheres.
			case SP_SOULGOLEM:
			case SP_SOULSHADOW:
			case SP_SOULFALCON:
			case SP_SOULFAIRY:
			case SP_SOULCURSE:
			case SP_SPA:
			case SP_SHA:
			case SP_SWHOO:
			case SP_SOULUNITY:
			case SP_SOULDIVISION:
			case SP_SOULREAPER:
			case SP_SOULEXPLOSION:
			case SP_KAUTE:
				if ( sd->soulball < require.spiritball )
				{
					clif_skill_fail(sd, skill_id,USESKILL_FAIL_SPIRITS,0,0);
					return 0;
				}
				break;

			// Skills that require rage spheres.
			case LG_RAGEBURST:
				if ( sd->rageball < require.spiritball )
				{
					clif_skill_fail(sd, skill_id,USESKILL_FAIL_SPIRITS,0,0);
					return 0;
				}
				break;

			// Skills that require charm spheres.
			case KO_KAIHOU:
			case KO_ZENKAI:
				if ( sd->charmball < require.spiritball )
				{
					clif_skill_fail(sd, skill_id,USESKILL_FAIL_SPIRITS,0,0);
					return 0;
				}
				break;

			default:// Skills that require spirit/coin spheres.
				if (sd->spiritball < require.spiritball) {
					if ((sd->class_&MAPID_BASEMASK) == MAPID_GUNSLINGER || (sd->class_&MAPID_UPPERMASK) == MAPID_REBELLION)
						clif_skill_fail(sd, skill_id, USESKILL_FAIL_COINS, (require.spiritball == -1) ? 1 : require.spiritball, 0);
					else
						clif_skill_fail(sd, skill_id, USESKILL_FAIL_SPIRITS, (require.spiritball == -1) ? 1 : require.spiritball, 0);
					return false;
				}
				break;
		}
	}

	return 1;
}

int skill_check_condition_castend(struct map_session_data* sd, uint16 skill_id, uint16 skill_lv)
{
	struct skill_condition require;
	struct status_data *status;
	int i;
	int index[MAX_SKILL_ITEM_REQUIRE];

	nullpo_ret(sd);

	if( sd->chatID )
		return 0;

	if( battle_config.gm_skilluncond && pc_isGM(sd) >= battle_config.gm_skilluncond && sd->skillitem != skill_id)
	{	//GMs don't override the skillItem check, otherwise they can use items without them being consumed! [Skotlex]	
		sd->state.arrow_atk = skill_get_ammotype(skill_id)?1:0; //Need to do arrow state check.
		sd->spiritball_old = sd->spiritball; //Need to do Spiritball check.
		sd->rageball_old = sd->rageball;// Needed for Rageball check.
		sd->charmball_old = sd->charmball;// Needed for Charmball check.
		sd->soulball_old = sd->soulball; //Need to do Soulball check.
		return 1;
	}

	switch( sd->menuskill_id )
	{ // Cast start or cast end??
		case AM_PHARMACY:
			switch(skill_id){
				case AM_PHARMACY:
				case AC_MAKINGARROW:
				case BS_REPAIRWEAPON:
				case AM_TWILIGHT1:
				case AM_TWILIGHT2:
				case AM_TWILIGHT3:
					return 0;
			}
			break;
		case GN_MIX_COOKING:
		case GN_MAKEBOMB:
		case GN_S_PHARMACY:
		case GN_CHANGEMATERIAL:
			if( sd->menuskill_id != skill_id)
				return 0;
			break;
	}
	
	if (sd->skillitem == skill_id && !sd->skillitem_keep_requirement) // Casting finished (Item skill or Hocus-Pocus)
	{
		if(skill_id == RK_CRUSHSTRIKE )
		{	// If you haven't any weapon equiped, it fails.
			short index = sd->equip_index[EQI_HAND_R];
			if( index < 0 || !sd->inventory_data[index] || sd->inventory_data[index]->type != IT_WEAPON ) {
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0xa);
				return 0;
			}
		}
		return 1;
	}

	if( pc_is90overweight(sd) )
	{
		clif_skill_fail(sd, skill_id,USESKILL_FAIL_WEIGHTOVER,0,0);
		return 0;
	}

	require = skill_get_requirement(sd, skill_id,skill_lv);

	// perform skill-specific checks (and actions)
	switch(skill_id)
	{
		case PR_BENEDICTIO:
			skill_check_pc_partner(sd, skill_id, &skill_lv, 1, 1);
			break;
		case AM_CANNIBALIZE:
		case AM_SPHEREMINE:
		{
			int c=0;
			int summons[5] = { MOBID_G_MANDRAGORA, MOBID_G_HYDRA, MOBID_G_FLORA, MOBID_G_PARASITE, MOBID_G_MANDRAGORA };
			int maxcount = (skill_id ==AM_CANNIBALIZE)? 6-skill_lv : skill_get_maxcount(skill_id,skill_lv);
			int mob_class = (skill_id ==AM_CANNIBALIZE)? summons[skill_lv-1] : MOBID_MARINE_SPHERE;
			if(battle_config.land_skill_limit && maxcount>0 && (battle_config.land_skill_limit&BL_PC)) {
				i = map_foreachinmap(skill_check_condition_mob_master_sub ,sd->bl.m, BL_MOB, sd->bl.id, mob_class, skill_id, &c);
				if(c >= maxcount ||
					(skill_id ==AM_CANNIBALIZE && c != i && battle_config.summon_flora&2))
				{	//Fails when: exceed max limit. There are other plant types already out.
					clif_skill_fail(sd, skill_id,USESKILL_FAIL_LEVEL,0,0);
					return 0;
				}
			}
			break;
		}
		case RK_HUNDREDSPEAR:
		case RK_PHANTOMTHRUST://Well make this skill ask for a spear even tho its not official. It is spear exclusive.
			if( require.weapon && !pc_check_weapontype(sd,require.weapon) )
			{
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_NEED_EQUIPPED_WEAPON_CLASS,4,0);//Spear required.
				return 0;
			}
			break;
		/*case AB_ADORAMUS:
			if( skill_check_pc_partner(sd, skill_id,&skill_lv, 1, 2) )
				sd->state.no_gemstone = 1; // Mark this skill as it don't consume ammo because partners gives SP
			break;*/
		case WL_COMET:
			if( skill_check_pc_partner(sd, skill_id,&skill_lv, 1, 0) )
				sd->state.no_gemstone = 1; // No need to consume 2 Red Gemstones if there are partners near by.
			break;
		case NC_PILEBUNKER:// As of 2018 there's only 5 pile bunkers in existance.
			if ( !((pc_search_inventory(sd,ITEMID_PILE_BUNKER) + pc_search_inventory(sd,ITEMID_PILE_BUNKER_S) + 
				pc_search_inventory(sd, ITEMID_PILE_BUNKER_T) + pc_search_inventory(sd, ITEMID_PILE_BUNKER_P) +
				pc_search_inventory(sd, ITEMID_ENGINE_PILE_BUNKER)) >= 1))
			{
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_NEED_ITEM,0,ITEMID_PILE_BUNKER);
				return 0;
			}
			break;
		case NC_EMERGENCYCOOL:
			if ( !((pc_search_inventory(sd,ITEMID_COOLING_DEVICE) + pc_search_inventory(sd,ITEMID_HIGH_QUALITY_COOLER) + 
				pc_search_inventory(sd,ITEMID_SPECIAL_COOLER)) >= 1) )
			{
				clif_skill_fail(sd, skill_id,USESKILL_FAIL_NEED_ITEM,0,ITEMID_COOLING_DEVICE);
				return 0;
			}
			break;
		case NC_SILVERSNIPER:
		case NC_MAGICDECOY:
			{
				unsigned char maxcount = skill_get_maxcount(skill_id,skill_lv);
				unsigned char count = 0;
				short mobid = 0;

				if (battle_config.land_skill_limit && maxcount>0 && (battle_config.land_skill_limit&BL_PC))
				{
					if (skill_id == NC_SILVERSNIPER)// Check for Silver Sniper's.
						map_foreachinmap(skill_check_condition_mob_master_sub ,sd->bl.m, BL_MOB, sd->bl.id, MOBID_SILVERSNIPER, skill_id, &count);
					else
					{// Check for Magic Decoy Fire/Water/Earth/Wind types.
						for ( mobid=MOBID_MAGICDECOY_FIRE; mobid<=MOBID_MAGICDECOY_WIND; mobid++ )
							map_foreachinmap(skill_check_condition_mob_master_sub ,sd->bl.m, BL_MOB, sd->bl.id, mobid, skill_id, &count);
					}

					if (count >= maxcount)
					{
						clif_skill_fail(sd, skill_id, 0, 0, 0);
						return 0;
					}
				}
				break;
			}
		case MH_SUMMON_LEGION:
			{
				unsigned char count = 0;
				short mobid = 0;

				// Check to see if any Hornet's, Giant Hornet's, or Luciola Vespa's are currently summoned.
				for ( mobid=MOBID_S_HORNET; mobid<=MOBID_S_LUCIOLA_VESPA; mobid++ )
					map_foreachinmap(skill_check_condition_mob_master_sub ,sd->bl.m, BL_MOB, sd->bl.id, mobid, skill_id, &count);

				// If any of the above 3 summons are found, fail the skill.
				if (count > 0)
				{
					clif_skill_fail(sd, skill_id,0,0,0);
					return 0;
				}
				break;
			}
	}

	status = &sd->battle_status;

	if( require.hp > 0 && status->hp <= (unsigned int)require.hp) {
		clif_skill_fail(sd, skill_id,USESKILL_FAIL_HP_INSUFFICIENT,0,0);
		return 0;
	}

	if (require.weapon && !pc_check_weapontype(sd, require.weapon)) {
		clif_skill_fail(sd, skill_id, USESKILL_FAIL_THIS_WEAPON, 0, 0);
		return 0;
	}

	if( require.ammo )
	{ //Skill requires stuff equipped in the arrow slot.
		if ((i = sd->equip_index[EQI_AMMO]) < 0 || !sd->inventory_data[i]) {
			clif_arrow_fail(sd, 0);
			return 0;
		}
		else if (sd->inventory.u.items_inventory[i].amount < require.ammo_qty) {
			if (require.ammo&(1 << A_BULLET | 1 << A_GRENADE | 1 << A_SHELL)) {
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_NEED_MORE_BULLET, 0, 0);
				return 0;
			}
			else if (require.ammo&(1 << A_KUNAI)) {
				clif_skill_fail(sd, skill_id, USESKILL_FAIL_NEED_EQUIPMENT_KUNAI, 0, 0);
				return 0;
			}
		}
		if (!(require.ammo&1<<sd->inventory_data[i]->look))
		{	//Ammo type check. Send the "wrong weapon type" message
			//which is the closest we have to wrong ammo type. [Skotlex]
			clif_arrow_fail(sd,0); //Haplo suggested we just send the equip-arrows message instead. [Skotlex]
			//clif_skill_fail(sd,skill,USESKILL_FAIL_THIS_WEAPON,0,0);
			return 0;
		}
	}

	for( i = 0; i < MAX_SKILL_ITEM_REQUIRE; ++i )
	{
		if( !require.itemid[i] )
			continue;
		index[i] = pc_search_inventory(sd,require.itemid[i]);
		if( index[i] < 0 || sd->inventory.u.items_inventory[index[i]].amount < require.amount[i] )
		{
			unsigned char type = 0x0a;
			int btype = 0, val = 0;
			switch( require.itemid[i] )
			{
				case ITEMID_RED_GEMSTONE: type = USESKILL_FAIL_REDJAMSTONE; break;	// Red gemstone required
				case ITEMID_BLUE_GEMSTONE:  type = USESKILL_FAIL_BLUEJAMSTONE; break;	// Blue gemstone required
				case ITEMID_HOLY_WATER:  type = USESKILL_FAIL_HOLYWATER; break;	// Holy water required.
				case ITEMID_ANCILLA:  type = USESKILL_FAIL_ANCILLA; break;		// Ancilla required.
				case ITEMID_SPECIAL_ALLOY_TRAP: type = 0x03; break;		// Alloy trap required
				// [%itemid] %ammount is required.
				case ITEMID_FACE_PAINT:
				case ITEMID_MAKEOVER_BRUSH:
				case ITEMID_PAINT_BRUSH:
				case ITEMID_SURFACE_PAINT:
					type = USESKILL_FAIL_NEED_ITEM;
					val = require.itemid[i];
					btype = require.amount[i];
					break;
			}
			clif_skill_fail(sd, skill_id, type, btype, val);
			return 0;
		}
	}

	return 1;
}

// type&2: consume items (after skill was used)
// type&1: consume the others (before skill was used)
int skill_consume_requirement( struct map_session_data *sd, short skill, short lv, short type)
{
	struct skill_condition req;

	nullpo_ret(sd);

	req = skill_get_requirement(sd,skill,lv);

	if( type&1 )
	{
		switch (skill) {
			case CG_TAROTCARD: // TarotCard will consume sp in skill_cast_nodamage_id [Inkfish]
			case MC_IDENTIFY:
				req.sp = 0;
				break;
			default:
				if (sd->state.autocast)
					req.sp = 0;
				break;
		}
		if(req.hp || req.sp)
			status_zap(&sd->bl, req.hp, req.sp);

		if(req.spiritball > 0)
		{// Skills that require certain types of spheres to use
			switch (skill)
			{// Skills that require soul spheres.
				case SP_SOULGOLEM:
				case SP_SOULSHADOW:
				case SP_SOULFALCON:
				case SP_SOULFAIRY:
				case SP_SOULCURSE:
				case SP_SPA:
				case SP_SHA:
				case SP_SWHOO:
				case SP_SOULUNITY:
				case SP_SOULDIVISION:
				case SP_SOULREAPER:
				case SP_SOULEXPLOSION:
				case SP_KAUTE:
					pc_delsoulball(sd,req.spiritball,0);
					break;

				// Skills that require rage spheres.
				case LG_RAGEBURST:
					pc_delrageball(sd,req.spiritball,0);
					break;

				// Skills that require charm spheres.
				case KO_KAIHOU:
				case KO_ZENKAI:
					pc_delcharmball(sd,req.spiritball,0);
					break;

				default:// Skills that require spirit/coin spheres.
					pc_delspiritball(sd,req.spiritball,0);
					break;
			}
		}
		else if (req.spiritball == -1) {
			sd->spiritball_old = sd->spiritball;
			pc_delspiritball(sd, sd->spiritball, 0);
		}

		if(req.zeny > 0)
		{
			if( skill == NJ_ZENYNAGE )
				req.zeny = 0; //Zeny is reduced on skill_attack.
			if( sd->status.zeny < req.zeny )
				req.zeny = sd->status.zeny;
			pc_payzeny(sd, req.zeny, LOG_TYPE_CONSUME, NULL);
		}
	}

	if( type&2 )
	{
		struct status_change *sc = &sd->sc;
		int n, i;

		if( !sc->count )
			sc = NULL;

		for( i = 0; i < MAX_SKILL_ITEM_REQUIRE; ++i )
		{
			if( !req.itemid[i] )
				continue;

			if( itemid_isgemstone(req.itemid[i]) && skill != HW_GANBANTEIN && sc && sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_WIZARD )
				continue; //Gemstones are checked, but not substracted from inventory.

			if( (n = pc_search_inventory(sd,req.itemid[i])) >= 0 )
				pc_delitem(sd,n,req.amount[i],0,1,LOG_TYPE_CONSUME);
		}

		sd->state.no_gemstone = 0;
	}

	return 1;
}

/**
* Get skill requirements and return the value after some additional/reduction condition (such item bonus and status change)
* @param sd Player's that will be checked
* @param skill_id Skill that's being used
* @param skill_lv Skill level of used skill
* @return skill_condition Struct 'skill_condition' that store the modified skill requirements
*/
struct skill_condition skill_get_requirement(struct map_session_data* sd, short skill, short lv)
{
	struct skill_condition req;
	struct status_data *status;
	struct status_change *sc;
	int i,j,hp_rate,sp_rate;
	bool level_dependent = false;

	memset(&req,0,sizeof(req));

	if( !sd )
		return req;

	if( sd->skillitem == skill && !sd->skillitem_keep_requirement )
		return req; // Item skills and Hocus-Pocus don't have requirements.[Inkfish]

	sc = &sd->sc;
	if( !sc->count )
		sc = NULL;

	switch( skill )
	{ // Turn off check.
	case BS_MAXIMIZE:		case NV_TRICKDEAD:	case TF_HIDING:			case AS_CLOAKING:		case CR_AUTOGUARD:
	case ML_AUTOGUARD:		case CR_DEFENDER:	case ML_DEFENDER:		case ST_CHASEWALK:		case PA_GOSPEL:
	case CR_SHRINK:			case TK_RUN:		case GS_GATLINGFEVER:	case TK_READYCOUNTER:	case TK_READYDOWN:
	case TK_READYSTORM:		case TK_READYTURN:	case SG_FUSION:			case SJ_LUNARSTANCE:	case SJ_STARSTANCE:
	case SJ_UNIVERSESTANCE:	case SJ_SUNSTANCE:	case SP_SOULCOLLECT:	case KO_YAMIKUMO:		case SU_HIDE:
		if( sc && sc->data[status_skill2sc(skill)] )
			return req;
	}

	j = skill_get_index(skill);
	if( j == 0 ) // invalid skill id
  		return req;
	if( lv < 1 || lv > MAX_SKILL_LEVEL )
		return req;

	status = &sd->battle_status;

	req.hp = skill_db[j].require.hp[lv-1];
	hp_rate = skill_db[j].require.hp_rate[lv-1];
	if(hp_rate > 0)
		req.hp += (status->hp * hp_rate)/100;
	else
		req.hp += (status->max_hp * (-hp_rate))/100;

	req.sp = skill_db[j].require.sp[lv-1];
	if((sd->skillid_old == BD_ENCORE) && skill == sd->skillid_dance)
		req.sp /= 2;
	if (skill == sd->status.skill[sd->reproduceskill_idx].id)
		req.sp += req.sp * 30 / 100;
	sp_rate = skill_db[j].require.sp_rate[lv-1];
	if(sp_rate > 0)
		req.sp += (status->sp * sp_rate)/100;
	else
		req.sp += (status->max_sp * (-sp_rate))/100;
	if( sd->dsprate!=100 )
		req.sp = req.sp * sd->dsprate / 100;

	ARR_FIND(0, ARRAYLENGTH(sd->skillsprate), i, sd->skillsprate[i].id == skill);
	if( i < ARRAYLENGTH(sd->skillsprate))
		req.sp -= req.sp * sd->skillsprate[i].val / 100;

	ARR_FIND(0, ARRAYLENGTH(sd->skillusesp), i, sd->skillusesp[i].id == skill);
	if( i < ARRAYLENGTH(sd->skillusesp) )
		req.sp -= sd->skillusesp[i].val;

	if( sc ){
		if (sc->data[SC_RECOGNIZEDSPELL])
			req.sp += req.sp * 25 / 100;
		if( sc->data[SC__LAZINESS] )
			req.sp += sc->data[SC__LAZINESS]->val1 * 10;
		if( sc->data[SC_UNLIMITED_HUMMING_VOICE] )
			req.sp += req.sp * sc->data[SC_UNLIMITED_HUMMING_VOICE]->val3 / 100;
		if (sc->data[SC_OFFERTORIUM])
			req.sp += req.sp * sc->data[SC_OFFERTORIUM]->val3 / 100;
		if (sc->data[SC_TELEKINESIS_INTENSE] && skill_get_ele(skill, lv) == ELE_GHOST)
			req.sp -= req.sp * sc->data[SC_TELEKINESIS_INTENSE]->val4 / 100;
	}

	req.zeny = skill_db[j].require.zeny[lv-1];

	if(sc && sc->data[SC__UNLUCKY])
		if ( sc->data[SC__UNLUCKY]->val1 == 1 )
			req.zeny += 250;
		else if ( sc->data[SC__UNLUCKY]->val1 == 2 )
			req.zeny += 500;
		else
			req.zeny += 1000;

	req.spiritball = skill_db[j].require.spiritball[lv-1];

	req.state = skill_db[j].require.state;

	req.mhp = skill_db[j].require.mhp[lv-1];

	req.weapon = skill_db[j].require.weapon;

	req.ammo_qty = skill_db[j].require.ammo_qty[lv-1];
	if (req.ammo_qty)
		req.ammo = skill_db[j].require.ammo;

	if (!req.ammo && skill && skill_isammotype(sd, skill))
	{	//Assume this skill is using the weapon, therefore it requires arrows.
		req.ammo = 0xFFFFFFFF; //Enable use on all ammo types.
		req.ammo_qty = 1;
	}

	req.eqItem_count = skill_db[j].require.eqItem_count;
	req.eqItem = skill_db[j].require.eqItem;

	switch (skill) {
		/* Skill level-dependent checks */
	case NC_SHAPESHIFT:
	case NC_REPAIR:
		//NOTE: Please make sure Magic_Gear_Fuel in the last position in skill_require_db.txt
		req.itemid[1] = skill_db[j].require.itemid[MAX_SKILL_ITEM_REQUIRE - 1];
		req.amount[1] = skill_db[j].require.amount[MAX_SKILL_ITEM_REQUIRE - 1];
	case GN_FIRE_EXPANSION:
	case SO_SUMMON_AGNI:
	case SO_SUMMON_AQUA:
	case SO_SUMMON_VENTUS:
	case SO_SUMMON_TERA:
	case SO_WATER_INSIGNIA:
	case SO_FIRE_INSIGNIA:
	case SO_WIND_INSIGNIA:
	case SO_EARTH_INSIGNIA:
	case WZ_FIREPILLAR: // no gems required at level 1-5 [celest]
		req.itemid[0] = skill_db[j].require.itemid[min(skill - 1, MAX_SKILL_ITEM_REQUIRE - 1)];
		req.amount[0] = skill_db[j].require.amount[min(skill - 1, MAX_SKILL_ITEM_REQUIRE - 1)];
		level_dependent = true;

		/* Normal skill requirements and gemstone checks */
	default:
		for (i = 0; i < ((!level_dependent) ? MAX_SKILL_ITEM_REQUIRE : 2); i++) {
			// Skip this for level_dependent requirement, just looking forward for gemstone removal. Assumed if there is gemstone there.
			if (!level_dependent) {
				switch (skill) {
				case AM_POTIONPITCHER:
				case CR_SLIMPITCHER:
				case CR_CULTIVATION:
					if (i != lv % 11 - 1)
						continue;
					break;
				case AM_CALLHOMUN:
					if (sd->status.hom_id) //Don't delete items when hom is already out.
						continue;
					break;
				case AB_ADORAMUS:
					if (itemid_isgemstone(skill_db[j].require.itemid[i]) && (sd->special_state.no_gemstone == 2 || skill_check_pc_partner(sd, skill, &lv, 1, 2)))
						continue;
					break;
				case WL_COMET:
					if (itemid_isgemstone(skill_db[j].require.itemid[i]) && (sd->special_state.no_gemstone == 2 || skill_check_pc_partner(sd, skill, &lv, 1, 0)))
						continue;
					break;
				}

				req.itemid[i] = skill_db[j].require.itemid[i];
				req.amount[i] = skill_db[j].require.amount[i];

				if (skill >= HT_SKIDTRAP && skill <= HT_TALKIEBOX && pc_checkskill(sd, RA_RESEARCHTRAP) > 0) {
					int16 itIndex;
					if ((itIndex = pc_search_inventory(sd, req.itemid[i])) < 0 || (itIndex >= 0 && sd->inventory.u.items_inventory[itIndex].amount < req.amount[i])) {
						req.itemid[i] = ITEMID_SPECIAL_ALLOY_TRAP;
						req.amount[i] = 1;
					}
					break;
				}
			}

			// Check requirement for gemstone.
			if (itemid_isgemstone(req.itemid[i])) {
				if (sd->special_state.no_gemstone == 2) // Remove all Magic Stone required for all skills for VIP.
					req.itemid[i] = req.amount[i] = 0;
				else {
					if (sd->special_state.no_gemstone)
					{	// All gem skills except Hocus Pocus and Ganbantein can cast for free with Mistress card -helvetica
						if (skill != SA_ABRACADABRA)
							req.itemid[i] = req.amount[i] = 0;
						else if (--req.amount[i] < 1)
							req.amount[i] = 1; // Hocus Pocus always use at least 1 gem
					}
					if (sc && sc->data[SC_INTOABYSS])
					{
						if (skill != SA_ABRACADABRA)
							req.itemid[i] = req.amount[i] = 0;
						else if (--req.amount[i] < 1)
							req.amount[i] = 1; // Hocus Pocus always use at least 1 gem
					}
				}
			}

			if (skill >= HT_SKIDTRAP && skill <= HT_TALKIEBOX && pc_checkskill(sd, RA_RESEARCHTRAP) > 0) {
				if ((j = pc_search_inventory(sd, req.itemid[i])) < 0 || (j >= 0 && sd->inventory.u.items_inventory[j].amount < req.amount[i])) {
					req.itemid[i] = ITEMID_SPECIAL_ALLOY_TRAP;
					req.amount[i] = 1;
				}
				break;
			}
		}
		break;
	}

	// Check for cost reductions due to skills & SCs
	switch(skill) {
		case MC_MAMMONITE:
			if(pc_checkskill(sd,BS_UNFAIRLYTRICK)>0)
				req.zeny -= req.zeny*10/100;
			break;
		case AL_HOLYLIGHT:
			if(sc && sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_PRIEST)
				req.sp *= 5;
			break;
		case SL_SMA:
		case SL_STUN:
		case SL_STIN:
		{
			int kaina_lv = sd ? pc_checkskill(sd, SL_KAINA) : skill_get_max(SL_KAINA);

			if(kaina_lv==0 || !sd || sd->status.base_level<70)
				break;
			if(sd->status.base_level>=90)
				req.sp -= req.sp*7*kaina_lv/100;
			else if(sd->status.base_level>=80)
				req.sp -= req.sp*5*kaina_lv/100;
			else if(sd->status.base_level>=70)
				req.sp -= req.sp*3*kaina_lv/100;
		}
			break;
		case MO_CHAINCOMBO:
		case MO_COMBOFINISH:
		case CH_TIGERFIST:
		case CH_CHAINCRUSH:
			if(sc && sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_MONK)
				req.sp = 2; //Monk Spirit makes monk/champion combo skills cost 2 SP regardless of original cost
			break;
		case MO_BODYRELOCATION:
			if( sc && sc->data[SC_EXPLOSIONSPIRITS] )
				req.spiritball = 0;
			break;
		case MO_EXTREMITYFIST:
			if (sc)
			{
				if (sc->data[SC_BLADESTOP])
					req.spiritball--;
				else if (sc->data[SC_COMBO])
				{
					switch (sc->data[SC_COMBO]->val1)
					{
						case MO_COMBOFINISH:
							req.spiritball = 4;
							break;
						case CH_TIGERFIST:
							req.spiritball = 3;
							break;
						case CH_CHAINCRUSH:	//It should consume whatever is left as long as it's at least 1.
							req.spiritball = (sd && sd->spiritball) ? sd->spiritball : 1;
							break;
					}
				}
				else if (sc->data[SC_RAISINGDRAGON]) //Only Asura will consume all remaining balls under RD status. [Jobbie]
					req.spiritball = max((sd) ? sd->spiritball : 15, 5);
			}
			break;
		case SR_RAMPAGEBLASTER:
			req.spiritball = sd->spiritball?sd->spiritball:15;
			break;
		case SR_GATEOFHELL:
			if( sc && sc->data[SC_COMBO] && sc->data[SC_COMBO]->val1 == SR_FALLENEMPIRE )
				req.sp -= req.sp * 10 / 100;
			break;
		case SO_PSYCHIC_WAVE:
			if (sc && (sc->data[SC_HEATER_OPTION] || sc->data[SC_COOLER_OPTION] || sc->data[SC_GUST_OPTION] || sc->data[SC_CURSED_SOIL_OPTION]))
				req.sp += req.sp * 50 / 100;
			break;
		case SO_SUMMON_AGNI:
		case SO_SUMMON_AQUA:
		case SO_SUMMON_VENTUS:
		case SO_SUMMON_TERA:
			req.sp -= req.sp * (5 + 5 * pc_checkskill(sd,SO_EL_SYMPATHY)) / 100;
			break;
	}

	//Check if player is using the copied skill [Cydh]
	if (sd->status.skill[j].flag == SKILL_FLAG_PLAGIARIZED) {
		uint16 req_opt = skill_db[j].copyable.req_opt;
		if (req_opt & 0x001) req.hp = 0;
		if (req_opt & 0x002) req.mhp = 0;
		if (req_opt & 0x004) req.sp = 0;
		if (req_opt & 0x008) req.hp_rate = 0;
		if (req_opt & 0x010) req.sp_rate = 0;
		if (req_opt & 0x020) req.zeny = 0;
		if (req_opt & 0x040) req.weapon = 0;
		if (req_opt & 0x080) { req.ammo = 0; req.ammo_qty = 0; }
		if (req_opt & 0x100) req.state = ST_NONE;
		if (req_opt & 0x200) req.spiritball = 0;
		if (req_opt & 0x400) { memset(req.itemid, 0, sizeof(req.itemid)); memset(req.amount, 0, sizeof(req.amount)); }
		if (req_opt & 0x800) req.eqItem_count = 0;
	}
	
	return req;
}

/*==========================================
* Recoded cast time system. Mainly a setup like jRO, Thanks to Rytech. [15peaces]
* Does cast-time reductions based on dex, int (for renewal), status's, item bonuses, and config setting.
*------------------------------------------*/
int skill_castfix (struct block_list *bl, int skill_id, int skill_lv)
{
	int time = skill_get_cast(skill_id, skill_lv);//Used for regular and renewal variable cast time.
	int fixed_time = skill_get_fixed_cast(skill_id, skill_lv);//Used for renewal fix time.
	int fixed_cast_rate = 0;//Used for setting the percentage adjustment of fixed cast times.
	int final_time = 0;//Used for finalizing time calculations for pre-re and combining time and fixed_time for renewal.
	int rate = 0;//Used to support the duel dex rates check through castrate_dex_scale and castrate_dex_scale_3rd.
	int scale = 0;//Used to scale the regular and variable cast times.
	struct map_session_data *sd;
	struct status_change *sc;

	nullpo_retr(0, bl);
	sd = BL_CAST(BL_PC, bl);
	sc = status_get_sc(bl);

	// Calculates regular and variable cast time.
	if( !(skill_get_castnodex(skill_id, skill_lv)&1) )
	{ //If renewal casting is enabled, all renewal skills will follow the renewal cast time formula.
		if (battle_config.renewal_casting_renewal_skills == 1 && (skill_id >= RK_ENCHANTBLADE && skill_id <= MAX_SKILL || skill_id >= MH_SUMMON_LEGION && skill_id <= MH_VOLCANIC_ASH))
		{
			if ( battle_config.renewal_casting_square_debug == 1 )
			{
				ShowDebug("Skill ID: %d\n",skill_id);
				ShowDebug("INT: %d / DEX: %d\n",status_get_int(bl) , status_get_dex(bl));
				ShowDebug("Variable Cast %d\n",time);
				ShowDebug("Time With Simple Reduction: %d\n",(time - time * (status_get_int(bl) + 2 * status_get_dex(bl)) / 530));
				time = (int)(time - sqrt((status_get_int(bl) + 2 * status_get_dex(bl)) / (double)530) * time);
				ShowDebug("Time With Square Reduction: %d\n",time);
			}
			else if ( battle_config.renewal_casting_square_calc == 1 )
				time = (int)(time - sqrt((status_get_int(bl) + 2 * status_get_dex(bl)) / (double)530) * time);
			else
				time -= time * (status_get_int(bl) + 2 * status_get_dex(bl)) / 530;
			if ( time < 0 ) time = 0;// No return of 0 since were adding the fixed_time later.
		}
		else
		{
			if (fixed_time < 0)//Prevents negeative values from affecting the variable below.
				fixed_time = 0;
			//Adds variable and fixed cast times together to make a full variable time for renewal skills
			//if renewal_casting_renewal_skills is turned off. Non-renewal skills dont have fixed times,
			//causing a fixed cast value of 0 to be added and not affect the actural cast time.
			time = time + fixed_time;
			if (sd && ((sd->class_&MAPID_THIRDMASK) >= MAPID_SUPER_NOVICE_E && (sd->class_&MAPID_THIRDMASK) <= MAPID_SOUL_REAPER || (sd->class_&MAPID_UPPERMASK) == MAPID_KAGEROUOBORO || (sd->class_&MAPID_UPPERMASK) == MAPID_REBELLION || (sd->class_&MAPID_BASEMASK) == MAPID_SUMMONER))
				rate = battle_config.castrate_dex_scale_renewal_jobs;
			else
				rate = battle_config.castrate_dex_scale;
			scale = rate - status_get_dex(bl);
			if( scale > 0 ) // Not instant cast
				time = time * scale / rate;
			else return 0;// Instant cast
		}
	}
	
	// Calculate cast time reduced by item/card bonuses.
	// Last condition checks if you have a cast or variable time after calculations to avoid additional processing.
	if( sd && time > 0)
	{
		int i;
		if(!(skill_get_castnodex(skill_id, skill_lv) & 4) && sd->castrate != 100 )
			time = time * sd->castrate / 100;
		// Skill-specific reductions work regardless of flag
		for( i = 0; i < ARRAYLENGTH(sd->skillcast) && sd->skillcast[i].id; i++ )
		{
			if( sd->skillcast[i].id == skill_id )
			{
				time += time * sd->skillcast[i].val / 100;
				break;
			}
		}
	}

	//These status's only affect regular and variable cast times.
	if (!(skill_get_castnodex(skill_id, skill_lv)&2) && sc && sc->count && time > 0)
	{
		if (sc->data[SC_SUFFRAGIUM]) {
			time -= time * sc->data[SC_SUFFRAGIUM]->val2 / 100;
			status_change_end(bl, SC_SUFFRAGIUM, INVALID_TIMER);
		}
		if (sc->data[SC_POEMBRAGI])
			time -= time * sc->data[SC_POEMBRAGI]->val2 / 100;
		if (sc->data[SC_MEMORIZE]) {
			time -= time * 50 / 100;
			if ((sc->data[SC_MEMORIZE]->val2) <= 0)
				status_change_end(bl, SC_MEMORIZE, INVALID_TIMER);
		}
		if (sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 3 &&
			skill_get_ele(skill_id, skill_lv) == ELE_WATER && skill_get_type(skill_id) == BF_MAGIC)
			time -= time * 30 / 100;
		if (sc->data[SC_SOULFAIRY])
			time -= time * sc->data[SC_SOULFAIRY]->val3 / 100;
		if (sc->data[SC_IZAYOI])
			time -= time * 50 / 100;
		if (sc->data[SC_TELEKINESIS_INTENSE])
			time -= time * sc->data[SC_TELEKINESIS_INTENSE]->val3 / 100;
		if (sc->data[SC_SLOWCAST])
			time += time * sc->data[SC_SLOWCAST]->val2 / 100;
	}
	
	//These status's adjust the fixed cast time by a fixed amount. Fixed adjustments stack and can increase or decrease the time.
	if (sc && sc->count)
	{
		if (sc->data[SC_GUST_OPTION] || sc->data[SC_BLAST_OPTION] || sc->data[SC_WILD_STORM_OPTION])
			fixed_time -= 1000;
		if( sc->data[SC_MANDRAGORA] )
			fixed_time += 500 * sc->data[SC_MANDRAGORA]->val1;
	}

	if(sd) 
	{
		// Fixed cast time adjustments by a fixed amount trough items. [15peaces]
		fixed_time += sd->bonus.fixedcast;

		//Fixed cast time reductions in a percentage starts here where reductions from any worn equips and cards that give fixed cast
		//reductions are calculated. Percentage reductions do not stack and the highest reduction value found on any worn equip,
		//worn card, skill, or status will be used.
		if (fixed_time > 0)
		{
			short fixed_cast_skill_rate = 0;
			int i;
			if( sd->fixedcastrate != 100 )//Fixed cast reduction on all skills.
				fixed_cast_rate = 100 - sd->fixedcastrate;
			for( i = 0; i < ARRAYLENGTH(sd->fixedskillcast) && sd->fixedskillcast[i].id; i++ )
			{
				if( sd->fixedskillcast[i].id == skill_id )
				{
					fixed_cast_skill_rate = 100 - (100 + sd->fixedskillcast[i].val);

					// Only take the fixed cast rate reduction for a certain
					// skill if bonus2 is giving a higher rate then bonus1.
					if ( fixed_cast_skill_rate > fixed_cast_rate )
						fixed_cast_rate = fixed_cast_skill_rate;
						break;
				}
			}
		}
	}

	//Fixed cast time percentage reduction from radius if learned.
	if( sd && pc_checkskill(sd, WL_RADIUS) > 0 && skill_id >= WL_WHITEIMPRISON && skill_id <= WL_FREEZE_SP && fixed_time > 0 )
	{
		int radiusbonus = 5 + 5 * pc_checkskill(sd, WL_RADIUS);
		if ( radiusbonus > fixed_cast_rate )
			fixed_cast_rate = radiusbonus;
	}
	
	//Fixed cast time percentage reduction from status's.
	if (sc && sc->count && fixed_time > 0)
	{
		if( sc->data[SC_DANCE_WITH_WUG] && sc->data[SC_DANCE_WITH_WUG]->val4 > fixed_cast_rate)
			fixed_cast_rate = sc->data[SC_DANCE_WITH_WUG]->val4;
		if( sc->data[SC_AB_SECRAMENT] && sc->data[SC_AB_SECRAMENT]->val2 > fixed_cast_rate)
			fixed_cast_rate = sc->data[SC_AB_SECRAMENT]->val2;
		if (sc->data[SC_HEAT_BARREL] && sc->data[SC_HEAT_BARREL]->val2 > fixed_cast_rate)
			fixed_cast_rate = sc->data[SC_HEAT_BARREL]->val2;
		if (sc->data[SC_IZAYOI])
			fixed_cast_rate = 100;
		
		// Reductions.
		if (sc->data[SC_FROST])
			fixed_cast_rate -= 50;
	}

	//Finally after checking through many different checks, we finalize how much of a percentage the fixed cast time will be increased or reduced.
	if ( fixed_time > 0 && fixed_cast_rate != 0 )
		fixed_time -= fixed_time * fixed_cast_rate / 100;

	//Check prevents variable and fixed times from going below to a negeative value.
	if (time < 0)
		time = 0;
	if (fixed_time < 0)
		fixed_time = 0;

	//Only add variable and fixed times when renewal casting for renewal skills are on. Without this check,
	//it will add the 2 together during the above phase and then readd the fixed time.
	if (battle_config.renewal_casting_renewal_skills == 1 && (skill_id >= RK_ENCHANTBLADE && skill_id <= MAX_SKILL || skill_id >= MH_SUMMON_LEGION && skill_id <= MH_VOLCANIC_ASH))
		final_time = time + fixed_time;
	else
		final_time = time;

	//Entire cast time is increased if caster has the Laziness status.
	if (sc && sc->data[SC__LAZINESS])
		final_time += final_time * sc->data[SC__LAZINESS]->val3 / 100;

	// Need a confirm on how it increases cast time and by how much. Can't find info anywhere.
	if (sc && sc->data[SC_NEEDLE_OF_PARALYZE])
		final_time += final_time * sc->data[SC_NEEDLE_OF_PARALYZE]->val3 / 100;

	// Config cast time multiplier.
	if (battle_config.cast_rate != 100)
		final_time = final_time * battle_config.cast_rate / 100;

	// Return final cast time.
	return (final_time > 0) ? final_time : 0;
}

/*==========================================
 * Does cast-time reductions based on sc data.
 *------------------------------------------*/
/*int skill_castfix_sc (struct block_list *bl, int time)
{
	struct status_change *sc = status_get_sc(bl);

	if (sc && sc->count) {
		if (sc->data[SC_SLOWCAST])
			time += time * sc->data[SC_SLOWCAST]->val2 / 100;
		if (sc->data[SC_SUFFRAGIUM]) {
			time -= time * sc->data[SC_SUFFRAGIUM]->val2 / 100;
			status_change_end(bl, SC_SUFFRAGIUM, INVALID_TIMER);
		}
		if (sc->data[SC_MEMORIZE]) {
			time>>=1;
			if ((--sc->data[SC_MEMORIZE]->val2) <= 0)
				status_change_end(bl, SC_MEMORIZE, INVALID_TIMER);
		}
		if (sc->data[SC_POEMBRAGI])
			time -= time * sc->data[SC_POEMBRAGI]->val2 / 100;
	}
	return (time > 0) ? time : 0;
}*/

/*==========================================
 * Does delay reductions based on dex/agi, sc data, item bonuses, ...
 *------------------------------------------*/
int skill_delayfix (struct block_list *bl, int skill_id, int skill_lv)
{
	int delaynodex = skill_get_delaynodex(skill_id, skill_lv);
	int time = skill_get_delay(skill_id, skill_lv);
	struct map_session_data *sd;
	struct status_change *sc = status_get_sc(bl);

	nullpo_ret(bl);
	sd = BL_CAST(BL_PC, bl);

	if (skill_id == SA_ABRACADABRA || skill_id == WM_RANDOMIZESPELL)
		return 0; //Will use picked skill's delay.

	if (bl->type&battle_config.no_skill_delay)
		return battle_config.min_skill_delay_limit;

	if (time < 0)
		time = -time + status_get_amotion(bl);	// If set to <0, add to attack motion.

	// Delay reductions
	switch (skill_id)
  	{	//Monk combo skills have their delay reduced by agi/dex.
	case MO_TRIPLEATTACK:
	case MO_CHAINCOMBO:
	case MO_COMBOFINISH:
	case CH_TIGERFIST:
	case CH_CHAINCRUSH:
	case SR_DRAGONCOMBO:
	case SR_FALLENEMPIRE:
	case SJ_PROMINENCEKICK:
		//If delay not specified, it will be 1000 - 4*agi - 2*dex
		if (time == 0)
			time = 1000;
		time -= (4 * status_get_agi(bl) + 2 * status_get_dex(bl));
		break;
	case HP_BASILICA:
		if( sc && !sc->data[SC_BASILICA] )
			time = 0; // There is no Delay on Basilica creation, only on cancel
		break;
	default:
		if (battle_config.delay_dependon_dex && !(delaynodex&1))
		{	// if skill delay is allowed to be reduced by dex
			int scale = battle_config.castrate_dex_scale - status_get_dex(bl);
			if (scale > 0)
				time = time * scale / battle_config.castrate_dex_scale;
			else //To be capped later to minimum.
				time = 0;
		}
		if (battle_config.delay_dependon_agi && !(delaynodex&1))
		{	// if skill delay is allowed to be reduced by agi
			int scale = battle_config.castrate_dex_scale - status_get_agi(bl);
			if (scale > 0)
				time = time * scale / battle_config.castrate_dex_scale;
			else //To be capped later to minimum.
				time = 0;
		}
	}

	if ( sc && sc->data[SC_SPIRIT] )
	{
		switch (skill_id) {
			case CR_SHIELDBOOMERANG:
				if (sc->data[SC_SPIRIT]->val2 == SL_CRUSADER)
					time /= 2;
				break;
			case AS_SONICBLOW:
				if (!map_flag_gvg2(bl->m) && !map[bl->m].flag.battleground && sc->data[SC_SPIRIT]->val2 == SL_ASSASIN)
					time /= 2;
				break;
		}
	}

	if (!(delaynodex&2))
	{
		if (sc && sc->count) {
			if (sc->data[SC_POEMBRAGI])
				time -= time * sc->data[SC_POEMBRAGI]->val3 / 100;
		}
	}

	if (sc && sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 3 &&
		skill_get_ele(skill_id, skill_lv) == ELE_WIND && skill_get_type(skill_id) == BF_MAGIC)
		time -= time * 50 / 100;

	if (sc && sc->data[SC_SOULDIVISION])
		time += time * sc->data[SC_SOULDIVISION]->val2 / 100;

	if( !(delaynodex&4) && sd && sd->delayrate != 100 )
		time = time * sd->delayrate / 100;

	if (battle_config.delay_rate != 100)
		time = time * battle_config.delay_rate / 100;

	return max(time, battle_config.min_skill_delay_limit);
}

int skill_cooldownfix (struct block_list *bl, int skill_id, int skill_lv)
{
	int time = skill_get_cooldown(skill_id, skill_lv);
	struct map_session_data *sd;
	struct status_change *sc = status_get_sc(bl);

	nullpo_ret(bl);
	sd = BL_CAST(BL_PC, bl);

	if (bl->type&battle_config.no_skill_cooldown)
		return battle_config.min_skill_cooldown_limit;

	if ( skill_id == SJ_NOVAEXPLOSING && sc && sc->data[SC_DIMENSION] )
		time = 0;// Dimension removes Nova Explosion's cooldown.

	if (sd && sd->cooldownrate != 100)
		time = time * sd->cooldownrate / 100;

	if (battle_config.cooldown_rate != 100)
		time = time * battle_config.cooldown_rate / 100;

	return max(time, battle_config.min_skill_cooldown_limit);
}

/*==========================================
 * Weapon Repair [Celest/DracoRPG]
 *------------------------------------------*/
void skill_repairweapon (struct map_session_data *sd, int idx)
{
	t_itemid material;
	t_itemid materials[4] = { 1002, 998, 999, 756 };
	struct item *item;
	struct map_session_data *target_sd;

	nullpo_retv(sd);
	
	if (!(target_sd = map_id2sd(sd->menuskill_val))) //Failed....
		return;
	
	if(idx==0xFFFF) // No item selected ('Cancel' clicked)
		return;
	
	if(idx < 0 || idx >= MAX_INVENTORY)
		return; //Invalid index??

   item = &target_sd->inventory.u.items_inventory[idx];
   if (!item->nameid || !item->attribute)
		return; //Again invalid item....

   if (sd != target_sd && !battle_check_range(&sd->bl, &target_sd->bl, skill_get_range2(&sd->bl, sd->menuskill_id, sd->menuskill_val2, true))) {
	   clif_item_repaireffect(sd, idx, 1);
		return;
	}

	if (itemdb_type(item->nameid)==IT_WEAPON)
		material = materials [target_sd->inventory_data[idx]->wlv - 1]; // Lv1/2/3/4 weapons consume 1 Iron Ore/Iron/Steel/Rough Oridecon
	else
		material = materials [2]; // Armors consume 1 Steel
	
	if (pc_search_inventory(sd,material) < 0 ) {
		clif_skill_fail(sd,sd->menuskill_id,USESKILL_FAIL_LEVEL,0,0);
		return;
	}
	
	clif_skill_nodamage(&sd->bl,&target_sd->bl,sd->menuskill_id,1,1);
	item->attribute = 0;/* clear broken state */
	clif_equiplist(target_sd);
	pc_delitem(sd,pc_search_inventory(sd,material),1,0,0,LOG_TYPE_CONSUME);
	clif_item_repaireffect(sd,idx,0);
	
	if(sd!=target_sd)
		clif_item_repaireffect(target_sd,idx,0);
}

/*==========================================
 * Item Appraisal
 *------------------------------------------*/
void skill_identify (struct map_session_data *sd, int idx) {
	int flag=1;

	nullpo_retv(sd);

	sd->state.workinprogress = WIP_DISABLE_NONE;

	if(idx >= 0 && idx < MAX_INVENTORY) {
		if(sd->inventory.u.items_inventory[idx].nameid > 0 && sd->inventory.u.items_inventory[idx].identify == 0 ){
			flag=0;
			sd->inventory.u.items_inventory[idx].identify=1;
		}
	}
	clif_item_identified(sd,idx,flag);
}

/*==========================================
 * Weapon Refine [Celest]
 *------------------------------------------*/
void skill_weaponrefine (struct map_session_data *sd, int idx)
{
	short joblv_bonus = (sd->status.job_level - 50) / 2;
	struct item *item;

	nullpo_retv(sd);

	if (idx >= 0 && idx < MAX_INVENTORY)
	{
		struct item_data *ditem = sd->inventory_data[idx];
		item = &sd->inventory.u.items_inventory[idx];

		if(item->nameid > 0 && ditem->type == IT_WEAPON)
		{
			int i = 0, ep = 0, per;
			t_itemid material[5] = { 0, ITEMID_PHRACON, ITEMID_EMVERETARCON, ITEMID_ORIDECON, ITEMID_ORIDECON, };

			if (ditem->flag.no_refine) { 	// if the item isn't refinable
				clif_skill_fail(sd, sd->menuskill_id, USESKILL_FAIL_LEVEL, 0, 0);
				return;
			}
			if (item->refine >= sd->menuskill_val || item->refine >= 10) {
				clif_upgrademessage(sd->fd, 2, item->nameid);
				return;
			}
			if ((i = pc_search_inventory(sd, material[ditem->wlv])) < 0) {
				clif_upgrademessage(sd->fd, 3, material[ditem->wlv]);
				return;
			}

			per = percentrefinery [ditem->wlv][(int)item->refine];

			// Mechanic's get the full 10% bonus no matter the job level.
			if ( (sd->class_&MAPID_THIRDMASK) == MAPID_MECHANIC )
				per += 10;
			else if ( joblv_bonus > 0 )// JobLV only has effect when above 50.
				per += joblv_bonus;

			pc_delitem(sd, i, 1, 0, 0, LOG_TYPE_OTHER);
			if (per > rnd() % 100) {
				log_pick(&sd->bl, LOG_TYPE_OTHER, item->nameid, -1, item);
				item->refine++;
				log_pick(&sd->bl, LOG_TYPE_OTHER, item->nameid, 1, item);
				if(item->equip) {
					ep = item->equip;
					pc_unequipitem(sd,idx,3);
				}
				clif_delitem(sd,idx,1,3);
				clif_upgrademessage(sd->fd, 0, item->nameid);
				clif_inventorylist(sd);
				clif_refine(sd->fd, 0, idx, item->refine);
				//achievement_update_objective(sd, AG_REFINE_SUCCESS, 2, ditem->wlv, item->refine);
				if (ep)
					pc_equipitem(sd,idx,ep,false);
				clif_misceffect(&sd->bl,3);
				if(item->refine == 10 &&
					item->card[0] == CARD0_FORGE &&
					(int)MakeDWord(item->card[2],item->card[3]) == sd->status.char_id)
				{ // Fame point system [DracoRPG]
					switch(ditem->wlv){
						case 1:
							pc_addfame(sd,1); // Success to refine to +10 a lv1 weapon you forged = +1 fame point
							break;
						case 2:
							pc_addfame(sd,25); // Success to refine to +10 a lv2 weapon you forged = +25 fame point
							break;
						case 3:
							pc_addfame(sd,1000); // Success to refine to +10 a lv3 weapon you forged = +1000 fame point
							break;
					}
				}
			} else {
				item->refine = 0;
				if(item->equip)
					pc_unequipitem(sd,idx,3);
				clif_upgrademessage(sd->fd, 1, item->nameid);
				clif_refine(sd->fd,1,idx,item->refine);
				//achievement_update_objective(sd, AG_REFINE_FAIL, 1, 1);
				pc_delitem(sd,idx,1,0,2,LOG_TYPE_OTHER);
				clif_misceffect(&sd->bl,2);
				clif_emotion(&sd->bl, E_OMG);
			}
		}
	}
}

/*==========================================
 *
 *------------------------------------------*/
int skill_autospell (struct map_session_data *sd, int skill_id)
{
	int skill_lv;
	uint16 idx = 0;
	int maxlv=1,lv;

	nullpo_ret(sd);

	skill_lv = sd->menuskill_val;
	if ((idx = skill_get_index(skill_id)) == 0)
		return 0;

	lv = (sd->status.skill[idx].id == skill_id) ? sd->status.skill[idx].lv : 0;

	if(skill_lv <= 0 || !lv) return 0; // Player must learn the skill before doing auto-spell [Lance]

	if(skill_id==MG_NAPALMBEAT)	maxlv=3;
	else if(skill_id==MG_COLDBOLT || skill_id==MG_FIREBOLT || skill_id==MG_LIGHTNINGBOLT){
		if (sd->sc.data[SC_SPIRIT] && sd->sc.data[SC_SPIRIT]->val2 == SL_SAGE)
			maxlv =10; //Soul Linker bonus. [Skotlex]
		else if(skill_lv==2) maxlv=1;
		else if(skill_lv==3) maxlv=2;
		else if(skill_lv>=4) maxlv=3;
	}
	else if(skill_id==MG_SOULSTRIKE){
		if(skill_lv==5) maxlv=1;
		else if(skill_lv==6) maxlv=2;
		else if(skill_lv>=7) maxlv=3;
	}
	else if(skill_id==MG_FIREBALL){
		if(skill_lv==8) maxlv=1;
		else if(skill_lv>=9) maxlv=2;
	}
	else if(skill_id==MG_FROSTDIVER) maxlv=1;
	else return 0;

	if(maxlv > lv)
		maxlv = lv;

	sc_start4(&sd->bl,SC_AUTOSPELL,100,skill_lv,skill_id,maxlv,0,
		skill_get_time(SA_AUTOSPELL,skill_lv));
	return 0;
}

/**
 * Count the number of players with Gangster Paradise, Peaceful Break, or Happy Break.
 * @param bl: Player object
 * @param ap: va_arg list
 * @return 1 if the player has learned Gangster Paradise, Peaceful Break, or Happy Break otherwise 0
 */
static int skill_sit_count (struct block_list *bl, va_list ap)
{
	struct map_session_data *sd = (struct map_session_data*)bl;
	int flag = va_arg(ap, int);

	if(!pc_issit(sd))
		return 0;

	if(flag&1 && pc_checkskill(sd,RG_GANGSTER) > 0)
		return 1;

	if(flag&2 && (pc_checkskill(sd,TK_HPTIME) > 0 || pc_checkskill(sd,TK_SPTIME) > 0))
		return 1;

	return 0;
}

/**
 * Triggered when a player sits down to activate bonus states.
 * @param bl: Player object
 * @param ap: va_arg list
 * @return 0
 */
static int skill_sit_in (struct block_list *bl, va_list ap)
{
	struct map_session_data *sd = (struct map_session_data*)bl;
	int flag = va_arg(ap, int);

	if(!pc_issit(sd))
		return 0;

	if(flag&1 && pc_checkskill(sd,RG_GANGSTER) > 0)
		sd->state.gangsterparadise=1;

	if(flag&2 && (pc_checkskill(sd,TK_HPTIME) > 0 || pc_checkskill(sd,TK_SPTIME) > 0 ))
	{
		sd->state.rest=1;
		status_calc_regen(bl, &sd->battle_status, &sd->regen);
		status_calc_regen_rate(bl, &sd->regen, &sd->sc);
	}

	return 0;
}

/**
 * Triggered when a player stands up to deactivate bonus states.
 * @param bl: Player object
 * @param ap: va_arg list
 * @return 0
 */
static int skill_sit_out (struct block_list *bl, va_list ap)
{
	struct map_session_data *sd = (struct map_session_data*)bl;
	int flag = va_arg(ap, int), range = va_arg(ap, int);

	if (map_foreachinrange(skill_sit_count, &sd->bl, range, BL_PC, flag) > 1)
		return 0;

	if (flag&1 && sd->state.gangsterparadise)
		sd->state.gangsterparadise=0;
	if (flag&2 && sd->state.rest) {
		sd->state.rest=0;
		status_calc_regen(bl, &sd->battle_status, &sd->regen);
		status_calc_regen_rate(bl, &sd->regen, &sd->sc);
	}

	return 0;
}

/**
 * Toggle Sit icon and player bonuses when sitting/standing.
 * @param sd: Player data
 * @param sitting: True when sitting or false when standing
 * @return 0
 */
int skill_sit(struct map_session_data *sd, bool sitting)
{
	int flag = 0, range = 0, lv;
	nullpo_ret(sd);

	if((lv = pc_checkskill(sd,RG_GANGSTER)) > 0) {
		flag|=1;
		range = skill_get_splash(RG_GANGSTER, lv);
	}
	if((lv = pc_checkskill(sd,TK_HPTIME)) > 0) {
		flag|=2;
		range = skill_get_splash(TK_HPTIME, lv);
	}
	else if ((lv = pc_checkskill(sd,TK_SPTIME)) > 0) {
		flag|=2;
		range = skill_get_splash(TK_SPTIME, lv);
	}

	if (sitting)
		clif_status_load(&sd->bl, SI_SIT, 1);
	else
		clif_status_load(&sd->bl, SI_SIT, 0);

	if (!flag) // No need to count area if no skills are learned.
		return 0;

	if (sitting) {
		if (map_foreachinrange(skill_sit_count, &sd->bl, range, BL_PC, flag) > 1)
			map_foreachinrange(skill_sit_in, &sd->bl, range, BL_PC, flag);
	}
	else
		map_foreachinrange(skill_sit_out, &sd->bl, range, BL_PC, flag, range);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_frostjoke_scream (struct block_list *bl, va_list ap)
{
	struct block_list *src;
	int skill_id,skill_lv;
	int64 tick;

	nullpo_ret(bl);
	nullpo_ret(src=va_arg(ap,struct block_list*));

	skill_id=va_arg(ap,int);
	skill_lv=va_arg(ap,int);
	if(skill_lv <= 0) return 0;
	tick = va_arg(ap, int64);

	if (src == bl || status_isdead(bl))
		return 0;
	if (bl->type == BL_PC) {
		struct map_session_data *sd = (struct map_session_data *)bl;
		if (sd && sd->sc.option&OPTION_INVISIBLE)
			return 0;
	}
	//It has been reported that Scream/Joke works the same regardless of woe-setting. [Skotlex]
	if(battle_check_target(src,bl,BCT_ENEMY) > 0)
		skill_additional_effect(src,bl,skill_id,skill_lv,BF_MISC,ATK_DEF,tick);
	else if(battle_check_target(src,bl,BCT_PARTY) > 0 && rnd()%100 < 10)
		skill_additional_effect(src,bl,skill_id,skill_lv,BF_MISC,ATK_DEF,tick);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static void skill_unitsetmapcell (struct skill_unit *src, int skill_id, int skill_lv, cell_t cell, bool flag)
{
	int range = skill_get_unit_range(skill_id,skill_lv);
	int x,y;

	for( y = src->bl.y - range; y <= src->bl.y + range; ++y )
		for( x = src->bl.x - range; x <= src->bl.x + range; ++x )
			map_setcell(src->bl.m, x, y, cell, flag);
}

/*==========================================
 *
 *------------------------------------------*/
int skill_attack_area (struct block_list *bl, va_list ap)
{
	struct block_list *src,*dsrc;
	int atk_type,skill_id,skill_lv,flag,type;
	int64 tick;

	if(status_isdead(bl))
		return 0;

	atk_type = va_arg(ap,int);
	src = va_arg(ap,struct block_list*);
	dsrc = va_arg(ap,struct block_list*);
	skill_id = va_arg(ap,int);
	skill_lv = va_arg(ap,int);
	tick = va_arg(ap, int64);
	flag = va_arg(ap,int);
	type = va_arg(ap,int);


	if (skill_area_temp[1] == bl->id) //This is the target of the skill, do a full attack and skip target checks.
		return (int)skill_attack(atk_type,src,dsrc,bl,skill_id,skill_lv,tick,flag);

	if( battle_check_target(dsrc,bl,type) <= 0 || !status_check_skilluse(NULL, bl, skill_id, skill_lv, 2) )
		return 0;


	switch (skill_id) {
	case WZ_FROSTNOVA: //Skills that don't require the animation to be removed
	case NPC_ACIDBREATH:
	case NPC_DARKNESSBREATH:
	case NPC_FIREBREATH:
	case NPC_ICEBREATH:
	case NPC_THUNDERBREATH:
		return (int)skill_attack(atk_type,src,dsrc,bl,skill_id,skill_lv,tick,flag);
	default:
		//Area-splash, disable skill animation.
		return (int)skill_attack(atk_type,src,dsrc,bl,skill_id,skill_lv,tick,flag|SD_ANIMATION);
	}
}
/*==========================================
 *
 *------------------------------------------*/
int skill_clear_group (struct block_list *bl, int flag)
{
	struct unit_data *ud = unit_bl2ud(bl);
	struct skill_unit_group *group[MAX_SKILLUNITGROUP];
	int i, count=0;

	nullpo_ret(bl);
	if (!ud) return 0;

	//All groups to be deleted are first stored on an array since the array elements shift around when you delete them. [Skotlex]
	for (i=0;i<MAX_SKILLUNITGROUP && ud->skillunit[i];i++)
	{
		switch (ud->skillunit[i]->skill_id) {
			case SA_DELUGE:
			case SA_VOLCANO:
			case SA_VIOLENTGALE:
			case SA_LANDPROTECTOR:
			case NJ_SUITON:
			case NJ_KAENSIN:
			case SC_CONFUSIONPANIC:
				if( flag&1 )
					group[count++]= ud->skillunit[i];
				break;
			case SO_CLOUD_KILL:
				if( flag&4 )
					group[count++]= ud->skillunit[i];
				break;
			case SO_WARMER:
				if( flag&8 )
					group[count++]= ud->skillunit[i];
				break;
			default:
				if (flag&2 && skill_get_inf2(ud->skillunit[i]->skill_id)&INF2_TRAP)
					group[count++]= ud->skillunit[i];
				break;
		}

	}
	for (i=0;i<count;i++)
		skill_delunitgroup(group[i]);
	return count;
}

/*==========================================
 * Returns the first element field found [Skotlex]
 *------------------------------------------*/
struct skill_unit_group *skill_locate_element_field(struct block_list *bl)
{
	struct unit_data *ud = unit_bl2ud(bl);
	int i;
	nullpo_ret(bl);
	if (!ud) return NULL;

	for (i=0;i<MAX_SKILLUNITGROUP && ud->skillunit[i];i++) {
		switch (ud->skillunit[i]->skill_id) {
			case SA_DELUGE:
			case SA_VOLCANO:
			case SA_VIOLENTGALE:
			case SA_LANDPROTECTOR:
			case NJ_SUITON:
			case SO_CLOUD_KILL:
			case SO_WARMER:
			case SO_FIRE_INSIGNIA:
			case SO_WATER_INSIGNIA:
			case SO_WIND_INSIGNIA:
			case SO_EARTH_INSIGNIA:
				return ud->skillunit[i];
		}
	}
	return NULL;
}

// for graffiti cleaner [Valaris]
int skill_graffitiremover (struct block_list *bl, va_list ap)
{
	struct skill_unit *unit=NULL;

	nullpo_ret(bl);
	nullpo_ret(ap);

	if(bl->type!=BL_SKILL || (unit=(struct skill_unit *)bl) == NULL)
		return 0;

	if((unit->group) && (unit->group->unit_id == UNT_GRAFFITI))
		skill_delunit(unit);

	return 0;
}

int skill_greed (struct block_list *bl, va_list ap)
{
	struct block_list *src;
	struct map_session_data *sd=NULL;
	struct flooritem_data *fitem=NULL;

	nullpo_ret(bl);
	nullpo_ret(src = va_arg(ap, struct block_list *));

	if(src->type == BL_PC && (sd=(struct map_session_data *)src) && bl->type==BL_ITEM && (fitem=(struct flooritem_data *)bl))
		pc_takeitem(sd, fitem);

	return 0;
}

//For Ranger's Detonator [Jobbie]
int skill_detonator (struct block_list *bl, va_list ap)
{
	struct skill_unit *unit=NULL;
	struct block_list *src;
	int unit_id;

	nullpo_retr(0, bl);
	nullpo_retr(0, ap);
	src = va_arg(ap,struct block_list *);

	if( bl->type != BL_SKILL || (unit = (struct skill_unit *)bl) == NULL || !unit->group )
 		return 0;
	if(unit->group->src_id != src->id)
		return 0;

	unit_id = unit->group->unit_id;
	switch( unit_id )
	{ //List of Hunter and Ranger Traps that can be detonate.
		case UNT_BLASTMINE:
		case UNT_SANDMAN:
		case UNT_CLAYMORETRAP:
		case UNT_TALKIEBOX:
		case UNT_CLUSTERBOMB:
		case UNT_FIRINGTRAP:
		case UNT_ICEBOUNDTRAP:
			if( unit_id == UNT_TALKIEBOX ){
				clif_talkiebox(bl,unit->group->valstr);
				unit->group->val2 = -1;
			} else {
				if (battle_config.skill_wall_check)
					map_foreachinshootrange(skill_trap_splash, bl, skill_get_splash(unit->group->skill_id, unit->group->skill_lv), unit->group->bl_flag | BL_SKILL | ~BCT_SELF, bl, unit->group->tick);
				else
					map_foreachinrange(skill_trap_splash, bl, skill_get_splash(unit->group->skill_id, unit->group->skill_lv), unit->group->bl_flag | BL_SKILL | ~BCT_SELF, bl, unit->group->tick);
			}

			clif_changetraplook(bl, UNT_USED_TRAPS);
			unit->group->unit_id = UNT_USED_TRAPS;
			unit->range = -1;
			unit->group->limit = DIFF_TICK32(gettick(), unit->group->tick) + (unit_id == UNT_TALKIEBOX ? 5000 : (unit_id == UNT_CLUSTERBOMB || unit_id == UNT_ICEBOUNDTRAP ? 2500 : 1500));
			break;
	}
	return 0;
}

int skill_flicker_bind_trap(struct block_list *bl, va_list ap)
{
	struct skill_unit *unit = NULL;
	struct block_list *src;
	int tick;

	src = va_arg(ap, struct block_list *);
	tick = va_arg(ap, int);

	nullpo_ret(src);
	nullpo_ret(bl);
	nullpo_ret(ap);

	if (bl->type != BL_SKILL || (unit = (struct skill_unit *)bl) == NULL)
		return 0;

	// Can only detonate your own binding traps.
	if (unit->group->src_id != src->id)
		return 0;

	if ((unit->group) && (unit->group->unit_id == UNT_B_TRAP))
	{
		clif_skill_nodamage(bl, bl, RL_B_FLICKER_ATK, 1, 1);// Explosion animation.
		map_foreachinrange(skill_trap_splash, bl, skill_get_splash(unit->group->skill_id, unit->group->skill_lv), unit->group->bl_flag, bl, tick);
		clif_changetraplook(bl, UNT_USED_TRAPS);
		unit->range = -1;
		unit->group->limit = DIFF_TICK32(tick, unit->group->tick);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static int skill_cell_overlap(struct block_list *bl, va_list ap)
{
	int skill_id;
	int *alive;
	int flag;
	struct skill_unit *unit;

	skill_id = va_arg(ap,int);
	alive = va_arg(ap,int *);
	flag = va_arg(ap, int);
	unit = (struct skill_unit *)bl;

	if (unit == NULL || unit->group == NULL || (*alive) == 0)
		return 0;

	if (unit->group->state.guildaura) /* guild auras are not cancelled! */
		return 0;

	switch (skill_id)
	{
		case SA_LANDPROTECTOR:
			if( unit->group->skill_id == SA_LANDPROTECTOR )
			{	//Check for offensive Land Protector to delete both. [Skotlex]
				(*alive) = 0;
				skill_delunit(unit);
				return 1;
			}
			if( !(skill_get_inf2(unit->group->skill_id)&(INF2_TRAP|INF2_NOLP)) || unit->group->skill_id == WZ_FIREPILLAR )
			{	//It deletes everything except songs/dances and traps
				skill_delunit(unit);
				return 1;
			}
			break;
		case HW_GANBANTEIN:
		case LG_EARTHDRIVE:
			// Officially songs/dances are removed
			skill_delunit(unit);
			return 1;
		case GN_CRAZYWEED_ATK:
			if( !(unit->group->state.song_dance&0x1) )
			{// Don't touch song/dance.
				skill_delunit(unit);
				return 1;
			}
			break;
		case SA_VOLCANO:
		case SA_DELUGE:
		case SA_VIOLENTGALE:
// The official implementation makes them fail to appear when casted on top of ANYTHING
// but I wonder if they didn't actually meant to fail when casted on top of each other?
// hence, I leave the alternate implementation here, commented. [Skotlex]
			if (unit->range <= 0 && skill_get_unit_id(unit->group->skill_id, 0) != UNT_DUMMYSKILL)
			{
				(*alive) = 0;
				return 1;
			}
/*
			switch (unit->group->skill_id)
			{	//These cannot override each other.
				case SA_VOLCANO:
				case SA_DELUGE:
				case SA_VIOLENTGALE:
					(*alive) = 0;
					return 1;
			}
*/
			break;
		case PF_FOGWALL:
			switch(unit->group->skill_id)
			{
				case SA_VOLCANO: //Can't be placed on top of these
				case SA_VIOLENTGALE:
					(*alive) = 0;
					return 1;
				case SA_DELUGE:
				case NJ_SUITON:
				//Cheap 'hack' to notify the calling function that duration should be doubled [Skotlex]
					(*alive) = 2;
					break;
			}
			break;
		case WZ_WATERBALL:
			switch (unit->group->skill_id) {
			case SA_DELUGE:
			case NJ_SUITON:
				//Consumes deluge/suiton
				skill_delunit(unit);
				return 1;
			}
			break;
		case WZ_ICEWALL:
		case HP_BASILICA:
		case HW_GRAVITATION:
			//These can't be placed on top of themselves (duration can't be refreshed)
			if (unit->group->skill_id == skill_id)
			{
				(*alive) = 0;
				return 1;
			}
			break;
		case RL_FIRE_RAIN:
			if (flag & 8)// Destroy skill units only if success chance passes.
				if (!(unit->group->state.song_dance & 0x1))
				{// Don't touch song/dance.
					skill_delunit(unit);
					return 1;
				}
			break;
	}

	if (unit->group->skill_id == SA_LANDPROTECTOR && !(skill_get_inf2(skill_id)&(INF2_TRAP | INF2_NOLP))) 
	{ //It deletes everything except traps and barriers
		(*alive) = 0;
		return 1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_chastle_mob_changetarget(struct block_list *bl,va_list ap)
{
	struct mob_data* md;
	struct unit_data*ud = unit_bl2ud(bl);
	struct block_list *from_bl;
	struct block_list *to_bl;
	md = (struct mob_data*)bl;
	from_bl = va_arg(ap,struct block_list *);
	to_bl = va_arg(ap,struct block_list *);

	if(ud && ud->target == from_bl->id)
		ud->target = to_bl->id;

	if(md->bl.type == BL_MOB && md->target_id == from_bl->id)
		md->target_id = to_bl->id;
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
static int skill_trap_splash (struct block_list *bl, va_list ap)
{
	struct block_list *src = va_arg(ap, struct block_list *);
	int64 tick = va_arg(ap, int64);
	struct skill_unit *unit = NULL;
	struct skill_unit_group *sg;
	struct block_list *ss;

	nullpo_ret(src);

	unit = (struct skill_unit *)src;

	if (!unit || !unit->alive || bl->prev == NULL)
		return 0;

	nullpo_ret(sg = unit->group);
	nullpo_ret(ss = map_id2bl(sg->src_id));

	if(battle_check_target(src,bl,BCT_ENEMY) <= 0)
		return 0;

	switch(sg->unit_id){
		case UNT_SHOCKWAVE:
		case UNT_SANDMAN:
		case UNT_FLASHER:
		case UNT_MAGENTATRAP:
		case UNT_COBALTTRAP:
		case UNT_MAIZETRAP:
		case UNT_VERDURETRAP:
		case UNT_POEMOFNETHERWORLD:
			skill_additional_effect(ss,bl,sg->skill_id,sg->skill_lv,BF_MISC,ATK_DEF,tick);
			break;
		case UNT_GROUNDDRIFT_WIND:
			if (skill_attack(skill_get_type(sg->skill_id), ss, src, bl, sg->skill_id, sg->skill_lv, tick, sg->val1))
				status_change_start(ss, bl, SC_STUN, 5000, sg->skill_lv, 0 ,0, 0, skill_get_time2(sg->skill_id, 1), 0);
			break;
		case UNT_GROUNDDRIFT_DARK:
			if (skill_attack(skill_get_type(sg->skill_id), ss, src, bl, sg->skill_id, sg->skill_lv, tick, sg->val1))
				status_change_start(ss, bl, SC_BLIND, 5000, sg->skill_lv, 0, 0, 0, skill_get_time2(sg->skill_id, 2), 0);
			break;
		case UNT_GROUNDDRIFT_POISON:
			if (skill_attack(skill_get_type(sg->skill_id), ss, src, bl, sg->skill_id, sg->skill_lv, tick, sg->val1))
				status_change_start(ss, bl, SC_POISON, 5000, sg->skill_lv, ss->id, 0, 0, skill_get_time2(sg->skill_id, 3), 0);
			break;
		case UNT_GROUNDDRIFT_WATER:
			if (skill_attack(skill_get_type(sg->skill_id), ss, src, bl, sg->skill_id, sg->skill_lv, tick, sg->val1))
				status_change_start(ss, bl, SC_FREEZE, 5000, sg->skill_lv, 0, 0, 0, skill_get_time2(sg->skill_id, 4), 0);
			break;
		case UNT_GROUNDDRIFT_FIRE:
			if (skill_attack(skill_get_type(sg->skill_id), ss, src, bl, sg->skill_id, sg->skill_lv, tick, sg->val1))
				skill_blown(src,bl,skill_get_blewcount(sg->skill_id,sg->skill_lv),-1,0);
			break;
		case UNT_ELECTRICSHOCKER:
			clif_skill_damage(src,bl,tick,0,0,-30000,1,sg->skill_id,sg->skill_lv,5);
			break;
		case UNT_FIRINGTRAP:
		case UNT_ICEBOUNDTRAP:
		case UNT_CLUSTERBOMB:
			if (ss != bl)
				skill_attack(BF_MISC, ss, src, bl, sg->skill_id, sg->skill_lv, tick, sg->val1 | SD_LEVEL);
			break;
		case UNT_REVERBERATION:
			{// Check for all enemys in a 5x5 range of the reverberation unit so we can divide damage in the following skill attacks.
				short enemy_count = map_foreachinrange(skill_area_sub, src, 2, BL_CHAR, src, sg->skill_id, sg->skill_lv, tick, BCT_ENEMY, skill_area_sub_count);
				skill_addtimerskill(ss, tick+status_get_amotion(ss), bl->id, 0, 0, WM_REVERBERATION_MELEE, sg->skill_lv, BF_WEAPON, enemy_count);
				skill_addtimerskill(ss, tick+2*status_get_amotion(ss), bl->id, 0, 0, WM_REVERBERATION_MAGIC, sg->skill_lv, BF_MAGIC, enemy_count);
			}
			break;
		case UNT_B_TRAP:
			skill_attack(skill_get_type(sg->skill_id), ss, src, bl, sg->skill_id, sg->skill_lv, tick, 0);
			status_change_end(bl, SC_B_TRAP, INVALID_TIMER);
			break;
		default:// ss = Caster / src = Skill Unit / bl = Enemy/Target
			skill_attack(skill_get_type(sg->skill_id), ss, src, bl, sg->skill_id, sg->skill_lv, tick, 0);
			break;
	}
	return 1;
}

/*==========================================
 * Remove current enchanted element for new element
 * @param bl Char
 * @param type New element
 *------------------------------------------*/
void skill_enchant_elemental_end (struct block_list *bl, int type)
{
	struct status_change *sc;
	const enum sc_type scs[] = { SC_ENCPOISON, SC_ASPERSIO, SC_FIREWEAPON, SC_WATERWEAPON, SC_WINDWEAPON, SC_EARTHWEAPON, SC_SHADOWWEAPON, SC_GHOSTWEAPON, SC_ENCHANTARMS };
	int i;
	nullpo_retv(bl);
	nullpo_retv(sc = status_get_sc(bl));

	if (!sc->count)
		return;

	for (i = 0; i < ARRAYLENGTH(scs); i++)
		if (type != scs[i] && sc->data[scs[i]])
			status_change_end(bl, scs[i], INVALID_TIMER);
}

bool skill_check_cloaking(struct block_list *bl, struct status_change_entry *sce)
{
	bool wall = true;

	if( (bl->type == BL_PC && battle_config.pc_cloak_check_type&1)
	||	(bl->type != BL_PC && battle_config.monster_cloak_check_type&1) )
	{	//Check for walls.
		static int dx[] = { 0, 1, 0, -1, -1,  1, 1, -1 };
		static int dy[] = { -1, 0, 1,  0, -1, -1, 1,  1 };
		int i;
		ARR_FIND( 0, 8, i, map_getcell(bl->m, bl->x+dx[i], bl->y+dy[i], CELL_CHKNOPASS) != 0 );
		if( i == 8 )
			wall = false;
	}

	if( sce )
	{
		if( !wall )
		{
			if( sce->val1 < 3 ) //End cloaking.
				status_change_end(bl, SC_CLOAKING, INVALID_TIMER);
			else
			if( sce->val4&1 )
			{	//Remove wall bonus
				sce->val4&=~1;
				status_calc_bl(bl,SCB_SPEED);
			}
		}
		else
		{
			if( !(sce->val4&1) )
			{	//Add wall speed bonus
				sce->val4|=1;
				status_calc_bl(bl,SCB_SPEED);
			}
		}
	}

	return wall;
}

bool skill_check_camouflage(struct block_list *bl, struct status_change_entry *sce)
{
	bool wall = true;

	if( bl->type == BL_PC && battle_config.pc_camouflage_check_type&1 )
	{ //Check for walls.
		static int dx[] = { 0, 1, 0, -1, -1, 1, 1, -1 };
		static int dy[] = { -1, 0, 1, 0, -1, -1, 1, 1 };
		int i;
		ARR_FIND( 0, 8, i, map_getcell(bl->m, bl->x+dx[i], bl->y+dy[i], CELL_CHKNOPASS) != 0 );
		if( i == 8 )
			wall = false;
	}

	if( sce )
	{
		if( !wall )
		{
			if (sce->val1 < 2) //End camoflage.
				status_change_end(bl, SC_CAMOUFLAGE, INVALID_TIMER);
		}
	}

	return wall;
}

/**
 * Process skill unit visibilty for single BL in area
 * @param bl
 * @param ap
 * @author [Cydh]
 **/
int skill_getareachar_skillunit_visibilty_sub(struct block_list *bl, va_list ap) {
	struct skill_unit *su = NULL;
	struct block_list *src = NULL;
	unsigned int party1 = 0;
	bool visible = true;

	nullpo_ret(bl);
	nullpo_ret((su = va_arg(ap, struct skill_unit*)));
	nullpo_ret((src = va_arg(ap, struct block_list*)));
	party1 = va_arg(ap, unsigned int);

	if (src != bl) {
		unsigned int party2 = status_get_party_id(bl);
		if (!party1 || !party2 || party1 != party2)
			visible = false;
	}

	clif_getareachar_skillunit(bl, su, SELF, visible);
	return 1;
}

/**
 * Check for skill unit visibilty in area on
 * - skill first placement
 * - skill moved (knocked back, moved dance)
 * @param su Skill unit
 * @param target Affected target for this visibility @see enum send_target
 * @author [Cydh]
 **/
void skill_getareachar_skillunit_visibilty(struct skill_unit *su, enum send_target target) {
	nullpo_retv(su);

	if (!su->hidden) // It's not hidden, just do this!
		clif_getareachar_skillunit(&su->bl, su, target, true);
	else {
		struct block_list *src = battle_get_master(&su->bl);
		map_foreachinarea(skill_getareachar_skillunit_visibilty_sub, su->bl.m, su->bl.x - AREA_SIZE, su->bl.y - AREA_SIZE,
			su->bl.x + AREA_SIZE, su->bl.y + AREA_SIZE, BL_PC, su, src, status_get_party_id(src));
	}
}

/**
 * Check for skill unit visibilty on single BL on insight/spawn action
 * @param su Skill unit
 * @param bl Block list
 * @author [Cydh]
 **/
void skill_getareachar_skillunit_visibilty_single(struct skill_unit *su, struct block_list *bl) {
	bool visible = true;
	struct block_list *src = NULL;

	nullpo_retv(bl);
	nullpo_retv(su);
	nullpo_retv((src = battle_get_master(&su->bl)));

	if (su->hidden && src != bl) {
		unsigned int party1 = status_get_party_id(src);
		unsigned int party2 = status_get_party_id(bl);
		if (!party1 || !party2 || party1 != party2)
			visible = false;
	}

	clif_getareachar_skillunit(bl, su, SELF, visible);
}

/*==========================================
 *
 *------------------------------------------*/
struct skill_unit *skill_initunit (struct skill_unit_group *group, int idx, int x, int y, int val1, int val2, bool hidden)
{
	struct skill_unit *unit;

	nullpo_retr(NULL, group);
	nullpo_retr(NULL, group->unit); // crash-protection against poor coding
	nullpo_retr(NULL, unit=&group->unit[idx]);

	if(!unit->alive)
		group->alive_count++;

	unit->bl.id=map_get_new_object_id();
	unit->bl.type=BL_SKILL;
	unit->bl.m=group->map;
	unit->bl.x=x;
	unit->bl.y=y;
	unit->group=group;
	unit->alive=1;
	unit->val1=val1;
	unit->val2=val2;
	unit->hidden = hidden;

	idb_put(skillunit_db, unit->bl.id, unit);
	map_addiddb(&unit->bl);
	if (map_addblock(&unit->bl))
		return NULL;

	// perform oninit actions
	switch (group->skill_id) {
	case WZ_ICEWALL:
		map_setgatcell(unit->bl.m,unit->bl.x,unit->bl.y,5);
		clif_changemapcell(0,unit->bl.m,unit->bl.x,unit->bl.y,5,AREA);
		skill_unitsetmapcell(unit, WZ_ICEWALL, group->skill_lv, CELL_ICEWALL, true);
		break;
	case SA_LANDPROTECTOR:
		skill_unitsetmapcell(unit,SA_LANDPROTECTOR,group->skill_lv,CELL_LANDPROTECTOR,true);
		break;
	case HP_BASILICA:
		skill_unitsetmapcell(unit,HP_BASILICA,group->skill_lv,CELL_BASILICA,true);
		// Because of Basilica's range we need to specifically update the players inside when it's cancelled prematurely
		map_foreachincell(skill_unit_effect, unit->bl.m, unit->bl.x, unit->bl.y, group->bl_flag, &unit->bl, 0, 4);
		break;
	case SC_MAELSTROM:
		skill_unitsetmapcell(unit,SC_MAELSTROM,group->skill_lv,CELL_MAELSTROM,true);
		break;
	default:
		if (group->state.song_dance&0x1) //Check for dissonance.
			skill_dance_overlap(unit, 1);
		break;
	}

	skill_getareachar_skillunit_visibilty(unit, AREA);

	return unit;
}

/*==========================================
 * Remove unit
 * @param unit
 *------------------------------------------*/
int skill_delunit (struct skill_unit* unit)
{
	struct skill_unit_group *group;

	nullpo_ret(unit);

	if( !unit->alive )
		return 0;

	unit->alive = 0;

	nullpo_ret(group = unit->group);

	if( group->state.song_dance&0x1 ) //Cancel dissonance effect.
		skill_dance_overlap(unit, 0);

	// invoke onout event
	if( !unit->range )
		map_foreachincell(skill_unit_effect,unit->bl.m,unit->bl.x,unit->bl.y,group->bl_flag,&unit->bl,gettick(),4);

	// perform ondelete actions
	switch (group->skill_id) {
	case HT_ANKLESNARE:
		{
		struct block_list* target = map_id2bl(group->val2);
		if( target )
			status_change_end(target, SC_ANKLE, INVALID_TIMER);
		}
		break;
	case WZ_ICEWALL:
		map_setgatcell(unit->bl.m,unit->bl.x,unit->bl.y,unit->val2);
		clif_changemapcell(0,unit->bl.m,unit->bl.x,unit->bl.y,unit->val2,ALL_SAMEMAP); // hack to avoid clientside cell bug
		skill_unitsetmapcell(unit, WZ_ICEWALL, group->skill_lv, CELL_ICEWALL, false);
		break;
	case SA_LANDPROTECTOR:
		skill_unitsetmapcell(unit,SA_LANDPROTECTOR,group->skill_lv,CELL_LANDPROTECTOR,false);
		break;
	case HP_BASILICA:
		skill_unitsetmapcell(unit,HP_BASILICA,group->skill_lv,CELL_BASILICA,false);
		break;
	case SC_MAELSTROM:
		skill_unitsetmapcell(unit,SC_MAELSTROM,group->skill_lv,CELL_MAELSTROM,false);
		break;
	case RA_ELECTRICSHOCKER:
		{
			struct block_list* target = map_id2bl(group->val2);
			if( target )
				status_change_end(target, SC_ELECTRICSHOCKER, INVALID_TIMER);
		}
		break;
	case SC_MANHOLE: // Note : Removing the unit don't remove the status (official info)
		if( group->val2 )
		{ // Someone Traped
			struct status_change *tsc = status_get_sc( map_id2bl(group->val2));
			if( tsc && tsc->data[SC__MANHOLE] )
				tsc->data[SC__MANHOLE]->val4 = 0; // Remove the Unit ID
 		}
		break;
	}

	clif_skill_delunit(unit);

	unit->group=NULL;
	map_delblock(&unit->bl); // don't free yet
	map_deliddb(&unit->bl);
	idb_remove(skillunit_db, unit->bl.id);
	if(--group->alive_count==0)
		skill_delunitgroup(group);

	return 0;
}
/*==========================================
 *
 *------------------------------------------*/
static DBMap* group_db = NULL;// int group_id -> struct skill_unit_group*

/// Returns the target skill_unit_group or NULL if not found.
struct skill_unit_group* skill_id2group(int group_id)
{
	return (struct skill_unit_group*)idb_get(group_db, group_id);
}


static int skill_unit_group_newid = MAX_SKILL_DB;

/// Returns a new group_id that isn't being used in group_db.
/// Fatal error if nothing is available.
static int skill_get_new_group_id(void)
{
	if( skill_unit_group_newid >= MAX_SKILL_DB && skill_id2group(skill_unit_group_newid) == NULL )
		return skill_unit_group_newid++;// available
	{// find next id
		int base_id = skill_unit_group_newid;
		while( base_id != ++skill_unit_group_newid )
		{
			if( skill_unit_group_newid < MAX_SKILL_DB )
				skill_unit_group_newid = MAX_SKILL_DB;
			if( skill_id2group(skill_unit_group_newid) == NULL )
				return skill_unit_group_newid++;// available
		}
		// full loop, nothing available
		ShowFatalError("skill_get_new_group_id: All ids are taken. Exiting...");
		exit(1);
	}
}

struct skill_unit_group* skill_initunitgroup (struct block_list* src, int count, short skill_id, short skill_lv, int unit_id, int limit, int interval)
{
	struct unit_data* ud = unit_bl2ud( src );
	struct skill_unit_group* group;
	int i;

	if(skill_id <= 0 || skill_lv <= 0) return 0;

	nullpo_retr(NULL, src);
	nullpo_retr(NULL, ud);

	// find a free spot to store the new unit group
	ARR_FIND( 0, MAX_SKILLUNITGROUP, i, ud->skillunit[i] == NULL );
	if(i == MAX_SKILLUNITGROUP)
	{
		// array is full, make room by discarding oldest group
		int j = 0;
		int64 maxdiff = 0, x, tick=gettick();
		for(i = 0; i < MAX_SKILLUNITGROUP && ud->skillunit[i]; i++)
			if((x = DIFF_TICK(tick, ud->skillunit[i]->tick)) > maxdiff)
			{
				maxdiff = x;
				j = i;
			}
		skill_delunitgroup(ud->skillunit[j]);
		//Since elements must have shifted, we use the last slot.
		i = MAX_SKILLUNITGROUP-1;
	}

	group             = ers_alloc(skill_unit_ers, struct skill_unit_group);
	group->src_id     = src->id;
	group->party_id   = status_get_party_id(src);
	group->guild_id   = status_get_guild_id(src);
	group->bg_id      = bg_team_get_id(src);
	group->group_id   = skill_get_new_group_id();
	group->link_group_id = 0;
	group->unit       = (struct skill_unit *)aCalloc(count,sizeof(struct skill_unit));
	group->unit_count = count;
	group->alive_count = 0;
	group->val1       = 0;
	group->val2       = 0;
	group->val3       = 0;
	group->skill_id   = skill_id;
	group->skill_lv   = skill_lv;
	group->unit_id    = unit_id;
	group->map        = src->m;
	group->limit      = limit;
	group->interval   = interval;
	group->tick       = gettick();
	group->valstr     = NULL;

	ud->skillunit[i] = group;

	idb_put(group_db, group->group_id, group);
	return group;
}

/*==========================================
 * Remove skill unit group
 * @param group
 * @param file
 * @param line
 * @param *func
 *------------------------------------------*/
int skill_delunitgroup_(struct skill_unit_group *group, const char* file, int line, const char* func)
{
	struct block_list* src;
	struct unit_data *ud;
	struct status_change *sc;
	int i,j;
	int link_group_id;

	if( group == NULL )
	{
		ShowDebug("skill_delunitgroup: group is NULL (source=%s:%d, %s)! Please report this! (#3504)\n", file, line, func);
		return 0;
	}

	src=map_id2bl(group->src_id);
	ud = unit_bl2ud(src);
	if(!src || !ud) {
		ShowError("skill_delunitgroup: Group's source not found! (src_id: %d skill_id: %d)\n", group->src_id, group->skill_id);
		return 0;
	}

	if (!status_isdead(src) && ((TBL_PC*)src)->state.warping && !((TBL_PC*)src)->state.changemap) {
		switch (group->skill_id) {
		case BA_DISSONANCE:
		case BA_POEMBRAGI:
		case BA_WHISTLE:
		case BA_ASSASSINCROSS:
		case BA_APPLEIDUN:
		case DC_UGLYDANCE:
		case DC_HUMMING:
		case DC_DONTFORGETME:
		case DC_FORTUNEKISS:
		case DC_SERVICEFORYOU:
			skill_usave_add(((TBL_PC*)src), group->skill_id, group->skill_lv);
			break;
		}
	}

	sc = status_get_sc(src);
	if (skill_get_unit_flag(group->skill_id)&(UF_DANCE|UF_SONG|UF_ENSEMBLE))
	{
		struct status_change* sc = status_get_sc(src);
		if (sc && sc->data[SC_DANCING])
		{
			sc->data[SC_DANCING]->val2 = 0 ; //This prevents status_change_end attempting to redelete the group. [Skotlex]
			status_change_end(src, SC_DANCING, INVALID_TIMER);
		}
	}

	// end Gospel's status change on 'src'
	// (needs to be done when the group is deleted by other means than skill deactivation)
	if( group->unit_id == UNT_GOSPEL && sc && sc->data[SC_GOSPEL] )
	{
		sc->data[SC_GOSPEL]->val3 = 0; //Remove reference to this group. [Skotlex]
		status_change_end(src,SC_GOSPEL,-1);
	}

	if (group->unit_id == UNT_BASILICA) {
		struct status_change *sc = status_get_sc(src);
		if (sc && sc->data[SC_BASILICA]) {
			sc->data[SC_BASILICA]->val3 = 0;
			status_change_end(src, SC_BASILICA, INVALID_TIMER);
		}
	}

	switch( group->skill_id ){
		case SG_SUN_WARM:
		case SG_MOON_WARM:
		case SG_STAR_WARM:
			if( sc && sc->data[SC_WARM] )
			{
				sc->data[SC_WARM]->val4 = 0;
				status_change_end(src, SC_WARM, INVALID_TIMER);
			}
			break;
		case NC_NEUTRALBARRIER:
			if( sc && sc->data[SC_NEUTRALBARRIER_MASTER] )
			{
				sc->data[SC_NEUTRALBARRIER_MASTER]->val2 = 0;
				status_change_end(src, SC_NEUTRALBARRIER_MASTER, INVALID_TIMER);
			}
			break;
		case NC_STEALTHFIELD:
			if( sc && sc->data[SC_STEALTHFIELD_MASTER] )
			{
				sc->data[SC_STEALTHFIELD_MASTER]->val2 = 0;
				status_change_end(src, SC_STEALTHFIELD_MASTER, INVALID_TIMER);
			}
			break;
		case LG_BANDING:
			if( sc && sc->data[SC_BANDING] )
			{
				sc->data[SC_BANDING]->val4 = 0;
				status_change_end(src, SC_BANDING, INVALID_TIMER);
			}
			break;
	}

	if (src->type==BL_PC && group->state.ammo_consume)
		battle_consume_ammo((TBL_PC*)src, group->skill_id, group->skill_lv);

	group->alive_count=0;

	// remove all unit cells
	if(group->unit != NULL)
		for( i = 0; i < group->unit_count; i++ )
			skill_delunit(&group->unit[i]);

	// clear Talkie-box string
	if( group->valstr != NULL )
	{
		aFree(group->valstr);
		group->valstr = NULL;
	}

	idb_remove(group_db, group->group_id);
	map_freeblock(&group->unit->bl); // schedules deallocation of whole array (HACK)
	group->unit=NULL;
	group->group_id=0;
	group->unit_count=0;

	link_group_id = group->link_group_id;
	group->link_group_id = 0;

	// locate this group, swap with the last entry and delete it
	ARR_FIND( 0, MAX_SKILLUNITGROUP, i, ud->skillunit[i] == group );
	ARR_FIND( i, MAX_SKILLUNITGROUP, j, ud->skillunit[j] == NULL ); j--;
	if( i < MAX_SKILLUNITGROUP )
	{
		ud->skillunit[i] = ud->skillunit[j];
		ud->skillunit[j] = NULL;
		ers_free(skill_unit_ers, group);
	}
	else
		ShowError("skill_delunitgroup: Group not found! (src_id: %d skill_id: %d)\n", group->src_id, group->skill_id);

	if (link_group_id) {
		struct skill_unit_group* group = skill_id2group(link_group_id);
		if (group)
			skill_delunitgroup(group);
	}

	return 1;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_clear_unitgroup (struct block_list *src)
{
	struct unit_data *ud = unit_bl2ud(src);

	nullpo_ret(ud);

	while (ud->skillunit[0])
		skill_delunitgroup(ud->skillunit[0]);

	return 1;
}

/*==========================================
 *
 *------------------------------------------*/
struct skill_unit_group_tickset *skill_unitgrouptickset_search (struct block_list *bl, struct skill_unit_group *group, int64 tick)
{
	int i,j=-1,s,id;
	struct unit_data *ud;
	struct skill_unit_group_tickset *set;

	nullpo_ret(bl);
	if (group->interval==-1)
		return NULL;

	ud = unit_bl2ud(bl);
	if (!ud) return NULL;

	set = ud->skillunittick;

	if (skill_get_unit_flag(group->skill_id)&UF_NOOVERLAP)
		id = s = group->skill_id;
	else
		id = s = group->group_id;

	for (i=0; i<MAX_SKILLUNITGROUPTICKSET; i++) {
		int k = (i+s) % MAX_SKILLUNITGROUPTICKSET;
		if (set[k].id == id)
			return &set[k];
		else if (j==-1 && (DIFF_TICK(tick,set[k].tick)>0 || set[k].id==0))
			j=k;
	}

	if (j == -1) {
		ShowWarning ("skill_unitgrouptickset_search: tickset is full\n");
		j = id % MAX_SKILLUNITGROUPTICKSET;
	}

	set[j].id = id;
	set[j].tick = tick;
	return &set[j];
}

/*==========================================
 *
 *------------------------------------------*/
int skill_unit_timer_sub_onplace (struct block_list* bl, va_list ap)
{
	struct skill_unit* unit = va_arg(ap,struct skill_unit *);
	struct skill_unit_group* group = NULL;
	int64 tick = va_arg(ap, int64);

	nullpo_ret(unit);

	if( !unit->alive || bl->prev == NULL )
		return 0;

	nullpo_ret(group = unit->group);

	if( !(skill_get_inf2(group->skill_id)&(INF2_TRAP|INF2_NOLP)) && group->skill_id != NC_NEUTRALBARRIER && map_getcell(unit->bl.m, unit->bl.x, unit->bl.y, CELL_CHKLANDPROTECTOR) )
		return 0; //AoE skills are ineffective. [Skotlex]

	if( battle_check_target(&unit->bl,bl,group->target_flag) <= 0 )
		return 0;

	skill_unit_onplace_timer(unit,bl,tick);

	return 1;
}

/*==========================================
 *
 *------------------------------------------*/
static int skill_unit_timer_sub(DBKey key, DBData *data, va_list ap)
{
	struct skill_unit* unit = (struct skill_unit*)db_data2ptr(data);
	struct skill_unit_group* group = NULL;
	int64 tick = va_arg(ap, int64);
  	bool dissonance;
	struct block_list* bl = &unit->bl;

	nullpo_ret(unit);

	if( !unit->alive )
		return 0;

	nullpo_ret(group = unit->group);

	// check for expiration
	if (!group->state.guildaura && (DIFF_TICK(tick, group->tick) >= group->limit || DIFF_TICK(tick, group->tick) >= unit->limit))
	{// skill unit expired (inlined from skill_unit_onlimit())
		switch( group->unit_id )
		{
			case UNT_BLASTMINE:
			case UNT_GROUNDDRIFT_WIND:
			case UNT_GROUNDDRIFT_DARK:
			case UNT_GROUNDDRIFT_POISON:
			case UNT_GROUNDDRIFT_WATER:
			case UNT_GROUNDDRIFT_FIRE:
				group->unit_id = UNT_USED_TRAPS;
				//clif_changetraplook(bl, UNT_FIREPILLAR_ACTIVE);
				group->limit = DIFF_TICK32(tick + 1500, group->tick);
				unit->limit = DIFF_TICK32(tick + 1500, group->tick);
			break;

			case UNT_ANKLESNARE:
			case UNT_ELECTRICSHOCKER:
				if( group->val2 > 0 ) {
					// Used Trap don't returns back to item
					skill_delunit(unit);
					break;
				}
			case UNT_SKIDTRAP:
			case UNT_LANDMINE:
			case UNT_SHOCKWAVE:
			case UNT_SANDMAN:
			case UNT_FLASHER:
			case UNT_FREEZINGTRAP:
			case UNT_CLAYMORETRAP:
			case UNT_TALKIEBOX:
			case UNT_CLUSTERBOMB:
			case UNT_MAGENTATRAP:
			case UNT_COBALTTRAP:
			case UNT_MAIZETRAP:
			case UNT_VERDURETRAP:
			case UNT_FIRINGTRAP:
			case UNT_ICEBOUNDTRAP:
			//case UNT_B_TRAP:// Does binding traps give back the used alloy trap?
				{
					struct block_list* src;
					if( unit->val1 > 0 && (src = map_id2bl(group->src_id)) != NULL && src->type == BL_PC )
					{ // revert unit back into a trap
						struct item item_tmp;
						memset(&item_tmp,0,sizeof(item_tmp));
						item_tmp.nameid = ( group->unit_id >= UNT_MAGENTATRAP && group->unit_id <= UNT_CLUSTERBOMB ) ? ITEMID_SPECIAL_ALLOY_TRAP:ITEMID_TRAP;
						item_tmp.identify = 1;
						map_addflooritem(&item_tmp,1,bl->m,bl->x,bl->y,0,0,0,4,0,false);
					}
					skill_delunit(unit);
				}
				break;

			case UNT_REVERBERATION:
			{
				if( unit->val1 > 0 )
				{
					map_foreachinrange(skill_trap_splash, &unit->bl, skill_get_splash(group->skill_id, group->skill_lv), group->bl_flag, &unit->bl, tick);
					group->unit_id = UNT_USED_TRAPS;
					clif_changelook(&unit->bl, LOOK_BASE, group->unit_id);
					unit->range = -1;
					group->limit=DIFF_TICK32(tick+1500,group->tick);
					unit->limit=DIFF_TICK32(tick+1500,group->tick);
				}
			}
			break;

			case UNT_WARP_ACTIVE:
				// warp portal opens (morph to a UNT_WARP_WAITING cell)
				group->unit_id = skill_get_unit_id(group->skill_id, 1); // UNT_WARP_WAITING
				clif_changelook(&unit->bl, LOOK_BASE, group->unit_id);
				// restart timers
				group->limit = skill_get_time(group->skill_id,group->skill_lv);
				unit->limit = skill_get_time(group->skill_id,group->skill_lv);
				// apply effect to all units standing on it
				map_foreachincell(skill_unit_effect,unit->bl.m,unit->bl.x,unit->bl.y,group->bl_flag,&unit->bl,gettick(),1);
			break;

			case UNT_CALLFAMILY:
			{
				struct map_session_data *sd = NULL;
				if(group->val1) {
		  			sd = map_charid2sd(group->val1);
					group->val1 = 0;
					if (sd && !map[sd->bl.m].flag.nowarp && pc_job_can_entermap((enum e_job)sd->status.class_, unit->bl.m, sd->gmlevel)) 
						pc_setpos(sd,map_id2index(unit->bl.m),unit->bl.x,unit->bl.y,CLR_TELEPORT);
				}
				if(group->val2) {
					sd = map_charid2sd(group->val2);
					group->val2 = 0;
					if (sd && !map[sd->bl.m].flag.nowarp && pc_job_can_entermap((enum e_job)sd->status.class_, unit->bl.m, sd->gmlevel)) 
						pc_setpos(sd,map_id2index(unit->bl.m),unit->bl.x,unit->bl.y,CLR_TELEPORT);
				}
				skill_delunit(unit);
			}
			break;

			case UNT_SPIDERWEB:
			{
				struct block_list* target = map_id2bl(group->val2);
				struct status_change *sc;
				//Clear group id from status change
				if (target && (sc = status_get_sc(target)) != NULL && sc->data[SC_SPIDERWEB]) {
					if (sc->data[SC_SPIDERWEB]->val2 == group->group_id)
						sc->data[SC_SPIDERWEB]->val2 = 0;
					else if (sc->data[SC_SPIDERWEB]->val3 == group->group_id)
						sc->data[SC_SPIDERWEB]->val3 = 0;
					else if (sc->data[SC_SPIDERWEB]->val4 == group->group_id)
						sc->data[SC_SPIDERWEB]->val4 = 0;
				}
				skill_delunit(unit);
			}
			break;

			case UNT_FEINTBOMB:
				{
					struct block_list *src = map_id2bl(group->src_id);
					if (src)
						map_foreachinrange(skill_area_sub, &group->unit->bl, unit->range, splash_target(src), src, SC_FEINTBOMB, group->skill_lv, tick, BCT_ENEMY|1, skill_castend_damage_id);
					skill_delunit(unit);
				}
				break;

			default:
				skill_delunit(unit);
		}
	}
	else
	{// skill unit is still active
		switch( group->unit_id )
		{
			case UNT_ICEWALL:
				// icewall loses 50 hp every second
				unit->val1 -= SKILLUNITTIMER_INTERVAL/20; // trap's hp
				if( unit->val1 <= 0 && unit->limit + group->tick > tick + 700 )
					unit->limit = DIFF_TICK32(tick + 700, group->tick);
				break;
			case UNT_BLASTMINE:
			case UNT_SKIDTRAP:
			case UNT_LANDMINE:
			case UNT_SHOCKWAVE:
			case UNT_SANDMAN:
			case UNT_FLASHER:
			case UNT_CLAYMORETRAP:
			case UNT_FREEZINGTRAP:
			case UNT_TALKIEBOX:
			case UNT_ANKLESNARE:
			case UNT_POEMOFNETHERWORLD:
			case UNT_B_TRAP:
				if( unit->val1 <= 0 ) {
					if( group->unit_id == UNT_ANKLESNARE && group->val2 > 0 )
						skill_delunit(unit);
					else {
						clif_changetraplook(bl, group->unit_id == UNT_LANDMINE ? UNT_FIREPILLAR_ACTIVE : UNT_USED_TRAPS);
						group->limit = DIFF_TICK32(tick, group->tick) + 1500;
						group->unit_id = UNT_USED_TRAPS;
					}
				}
				break;
			case UNT_WALLOFTHORN:
				if( unit->val1 <= 0 ){
					group->unit_id = UNT_USED_TRAPS;
					group->limit = DIFF_TICK32(tick, group->tick) + 1500;
				}
 				break;
			case UNT_SANCTUARY:
				if (group->val1 <= 0) {
					skill_delunitgroup(group);
				}
				break;
		}
	}

	//Don't continue if unit or even group is expired and has been deleted.
	if( !group || !unit->alive )
		return 0;

	dissonance = skill_dance_switch(unit, 0);

	if( unit->range >= 0 && group->interval != -1 )
	{
		if( battle_config.skill_wall_check )
			map_foreachinshootrange(skill_unit_timer_sub_onplace, bl, unit->range, group->bl_flag, bl,tick);
		else
			map_foreachinrange(skill_unit_timer_sub_onplace, bl, unit->range, group->bl_flag, bl,tick);

		if(unit->range == -1) //Unit disabled, but it should not be deleted yet.
			group->unit_id = UNT_USED_TRAPS;

		if( group->unit_id == UNT_TATAMIGAESHI )
		{
			unit->range = -1; //Disable processed cell.
			if (--group->val1 <= 0) // number of live cells
	  		{	//All tiles were processed, disable skill.
				group->target_flag=BCT_NOONE;
				group->bl_flag= BL_NUL;
			}
		}
	}

  	if( dissonance ) skill_dance_switch(unit, 1);

	return 0;
}
/*==========================================
 * Executes on all skill units every SKILLUNITTIMER_INTERVAL miliseconds.
 *------------------------------------------*/
int skill_unit_timer(int tid, int64 tick, int id, intptr_t data)
{
	map_freeblock_lock();

	skillunit_db->foreach(skillunit_db, skill_unit_timer_sub, tick);

	map_freeblock_unlock();

	return 0;
}

static int skill_unit_temp[20];  // temporary storage for tracking skill unit skill ids as players move in/out of them
/*==========================================
 * flag :
 *	1 : store that skill_unit in array
 *	2 : clear that skill_unit
 *	4 : call_on_left
 *------------------------------------------*/
int skill_unit_move_sub (struct block_list* bl, va_list ap)
{
	struct skill_unit* unit = (struct skill_unit *)bl;

	struct block_list* target = va_arg(ap,struct block_list*);
	int64 tick = va_arg(ap, int64);
	int flag = va_arg(ap,int);

	bool dissonance;
	int skill_id;
	int i;

	nullpo_ret(unit);
	nullpo_ret(target);

	if( !unit->alive || target->prev == NULL )
		return 0;

	struct skill_unit_group* group = unit->group;

	if (group == NULL)
		return 0;

	if(flag&1 && (unit->group->skill_id == PF_SPIDERWEB && flag&1 || unit->group->skill_id == GN_THORNS_TRAP) )
		return 0; // Fiberlock is never supposed to trigger on skill_unit_move. [Inkfish]

	dissonance = skill_dance_switch(unit, 0);

	//Necessary in case the group is deleted after calling on_place/on_out [Skotlex]
	skill_id = unit->group->skill_id;

	if( unit->group->interval != -1 && !(skill_get_unit_flag(skill_id)&UF_DUALMODE) && skill_id != BD_LULLABY) //Lullaby is the exception
	{	//Non-dualmode unit skills with a timer don't trigger when walking, so just return
		if( dissonance ) {
			skill_dance_switch(unit, 1);
			skill_unit_onleft(skill_unit_onout(unit,target,tick),target,tick); //we placed a dissonance, let's update
		}
		return 0;
	}

	//Target-type check.
	if( !(group->bl_flag&target->type && battle_check_target(&unit->bl,target,group->target_flag) > 0) )
	{
		if( group->src_id == target->id && group->state.song_dance&0x2 )
		{	//Ensemble check to see if they went out/in of the area [Skotlex]
			if( flag&1 )
			{
				if( flag&2 )
				{	//Clear this skill id.
					ARR_FIND( 0, ARRAYLENGTH(skill_unit_temp), i, skill_unit_temp[i] == skill_id );
					if( i < ARRAYLENGTH(skill_unit_temp) )
						skill_unit_temp[i] = 0;
				}
			}
			else
			{
				if( flag&2 )
				{	//Store this skill id.
					ARR_FIND( 0, ARRAYLENGTH(skill_unit_temp), i, skill_unit_temp[i] == 0 );
					if( i < ARRAYLENGTH(skill_unit_temp) )
						skill_unit_temp[i] = skill_id;
					else
						ShowError("skill_unit_move_sub: Reached limit of unit objects per cell! (skill_id: %hu)\n", skill_id);
				}

			}

			if( flag&4 )
				skill_unit_onleft(skill_id,target,tick);
		}

		if( dissonance ) skill_dance_switch(unit, 1);

		return 0;
	}
	else
	{
		if( flag&1 )
		{
			int result = skill_unit_onplace(unit,target,tick);
			if( flag&2 && result )
			{	//Clear skill ids we have stored in onout.
				ARR_FIND( 0, ARRAYLENGTH(skill_unit_temp), i, skill_unit_temp[i] == result );
				if( i < ARRAYLENGTH(skill_unit_temp) )
					skill_unit_temp[i] = 0;
			}
		}
		else
		{
			int result = skill_unit_onout(unit,target,tick);
			if( flag&2 && result )
			{	//Store this unit id.
				ARR_FIND( 0, ARRAYLENGTH(skill_unit_temp), i, skill_unit_temp[i] == 0 );
				if( i < ARRAYLENGTH(skill_unit_temp) )
					skill_unit_temp[i] = skill_id;
				else
					ShowError("skill_unit_move_sub: Reached limit of unit objects per cell! (skill_id: %hu)\n", skill_id);
			}
		}

		//TODO: Normally, this is dangerous since the unit and group could be freed
		//inside the onout/onplace functions. Currently it is safe because we know song/dance
		//cells do not get deleted within them. [Skotlex]
		if( dissonance ) skill_dance_switch(unit, 1);

		if( flag&4 )
			skill_unit_onleft(skill_id,target,tick);

		return 1;
	}
}

/*==========================================
 * Invoked when a char has moved and unit cells must be invoked (onplace, onout, onleft)
 * Flag values:
 * flag&1: invoke skill_unit_onplace (otherwise invoke skill_unit_onout)
 * flag&2: this function is being invoked twice as a bl moves, store in memory the affected
 * units to figure out when they have left a group.
 * flag&4: Force a onleft event (triggered when the bl is killed, for example)
 *------------------------------------------*/
int skill_unit_move (struct block_list *bl, int64 tick, int flag)
{
	nullpo_ret(bl);

	if( bl->prev == NULL )
		return 0;

	if( flag&2 && !(flag&1) )
	{	//Onout, clear data
		memset(skill_unit_temp, 0, sizeof(skill_unit_temp));
	}

	map_foreachincell(skill_unit_move_sub,bl->m,bl->x,bl->y,BL_SKILL,bl,tick,flag);

	if( flag&2 && flag&1 )
	{	//Onplace, check any skill units you have left.
		int i;
		for( i = 0; i < ARRAYLENGTH(skill_unit_temp); i++ )
			if( skill_unit_temp[i] )
				skill_unit_onleft(skill_unit_temp[i], bl, tick);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_unit_move_unit_group (struct skill_unit_group *group, int m, int dx, int dy)
{
	int i,j;
	int64 tick = gettick();
	int *m_flag;
	struct skill_unit *unit1;
	struct skill_unit *unit2;

	if (group == NULL)
		return 0;
	if (group->unit_count<=0)
		return 0;
	if (group->unit==NULL)
		return 0;

	if (skill_get_unit_flag(group->skill_id)&UF_ENSEMBLE)
		return 0; //Ensembles may not be moved around.

	if( group->unit_id == UNT_ICEWALL || group->unit_id == UNT_WALLOFTHORN )
		return 0; //Icewalls and Wall of Thorns don't get knocked back

	m_flag = (int *) aCalloc(group->unit_count, sizeof(int));
	//    m_flag
	//		0: Neither of the following (skill_unit_onplace & skill_unit_onout are needed)
	//		1: Unit will move to a slot that had another unit of the same group (skill_unit_onplace not needed)
	//		2: Another unit from same group will end up positioned on this unit (skill_unit_onout not needed)
	//		3: Both 1+2.
	for(i=0;i<group->unit_count;i++){
		unit1=&group->unit[i];
		if (!unit1->alive || unit1->bl.m!=m)
			continue;
		for(j=0;j<group->unit_count;j++){
			unit2=&group->unit[j];
			if (!unit2->alive)
				continue;
			if (unit1->bl.x+dx==unit2->bl.x && unit1->bl.y+dy==unit2->bl.y){
				m_flag[i] |= 0x1;
			}
			if (unit1->bl.x-dx==unit2->bl.x && unit1->bl.y-dy==unit2->bl.y){
				m_flag[i] |= 0x2;
			}
		}
	}
	j = 0;
	for (i=0;i<group->unit_count;i++) {
		unit1=&group->unit[i];
		if (!unit1->alive)
			continue;
		if (!(m_flag[i]&0x2)) {
			if (group->state.song_dance&0x1) //Cancel dissonance effect.
				skill_dance_overlap(unit1, 0);
			map_foreachincell(skill_unit_effect,unit1->bl.m,unit1->bl.x,unit1->bl.y,group->bl_flag,&unit1->bl,tick,4);
		}
		//Move Cell using "smart" criteria (avoid useless moving around)
		switch(m_flag[i])
		{
			case 0:
			//Cell moves independently, safely move it.
				map_foreachinmovearea(clif_outsight, &unit1->bl, AREA_SIZE, dx, dy, BL_PC, &unit1->bl);
				map_moveblock(&unit1->bl, unit1->bl.x+dx, unit1->bl.y+dy, tick);
				break;
			case 1:
			//Cell moves unto another cell, look for a replacement cell that won't collide
			//and has no cell moving into it (flag == 2)
				for(;j<group->unit_count;j++)
				{
					int dx2, dy2;
					if(m_flag[j]!=2 || !group->unit[j].alive)
						continue;
					//Move to where this cell would had moved.
					unit2 = &group->unit[j];
					dx2 = unit2->bl.x + dx - unit1->bl.x;
					dy2 = unit2->bl.y + dy - unit1->bl.y;
					map_foreachinmovearea(clif_outsight, &unit1->bl, AREA_SIZE, dx2, dy2, BL_PC, &unit1->bl);
					map_moveblock(&unit1->bl, unit2->bl.x+dx, unit2->bl.y+dy, tick);
					j++; //Skip this cell as we have used it.
					break;
				}
				break;
			case 2:
			case 3:
				break; //Don't move the cell as a cell will end on this tile anyway.
		}
		if (!(m_flag[i]&0x2)) { //We only moved the cell in 0-1
			if (group->state.song_dance&0x1) //Check for dissonance effect.
				skill_dance_overlap(unit1, 1);
			skill_getareachar_skillunit_visibilty(unit1, AREA);
			map_foreachincell(skill_unit_effect,unit1->bl.m,unit1->bl.x,unit1->bl.y,group->bl_flag,&unit1->bl,tick,1);
		}
	}
	aFree(m_flag);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_can_produce_mix (struct map_session_data *sd, t_itemid nameid, int trigger, int qty)
{
	int i,j;

	nullpo_ret(sd);

	if(nameid == 0)
		return 0;

	for(i=0;i<MAX_SKILL_PRODUCE_DB;i++){
		if (skill_produce_db[i].nameid == nameid) {
			if ((j = skill_produce_db[i].req_skill) > 0 && pc_checkskill(sd, j) < skill_produce_db[i].req_skill_lv)
				continue; // must iterate again to check other skills that produce it. [malufett]
			if (j > 0 && sd->skillid_old > 0 && sd->skillid_old != j)
				continue; // special case
			break;
		}
	}
	sd->skillid_old = sd->skilllv_old = 0;
	if( i >= MAX_SKILL_PRODUCE_DB )
		return 0;

	if( pc_checkadditem(sd, nameid, qty) == CHKADDITEM_OVERAMOUNT )
	{// cannot carry the produced stuff
		return 0;
	}

	if(trigger>=0){
		if(trigger>20) { // Non-weapon, non-food item (itemlv must match)
			if(skill_produce_db[i].itemlv!=trigger)
				return 0;
		} else if(trigger>10) { // Food (any item level between 10 and 20 will do)
			if(skill_produce_db[i].itemlv<=10 || skill_produce_db[i].itemlv>20)
				return 0;
		} else { // Weapon (itemlv must be higher or equal)
			if(skill_produce_db[i].itemlv>trigger)
				return 0;
		}
	}

	for(j=0;j<MAX_PRODUCE_RESOURCE;j++){
		t_itemid id;
		if( (id=skill_produce_db[i].mat_id[j]) <= 0 )
			continue;
		if(skill_produce_db[i].mat_amount[j] <= 0) {
			if(pc_search_inventory(sd,id) < 0)
				return 0;
		}
		else {
			int x, y;
			for(y=0,x=0;y<MAX_INVENTORY;y++)
				if( sd->inventory.u.items_inventory[y].nameid == id )
					x+=sd->inventory.u.items_inventory[y].amount;
			if(x<qty*skill_produce_db[i].mat_amount[j])
				return 0;
		}
	}
	return i+1;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_produce_mix (struct map_session_data *sd, int skill_id, t_itemid nameid, int slot1, int slot2, int slot3, int qty)
{
	int slot[3];
	int i,sc,ele,idx,equip,wlv = 0,make_per,flag = 0,skill_lv = 0,temp_qty = 0;
	int num = -1; // exclude the recipe
	struct status_data *status;

	nullpo_ret(sd);
	status = status_get_status_data(&sd->bl);

	if (sd->skillid_old == skill_id)
		skill_lv = sd->skilllv_old;
	else
		sd->skillid_old = skill_id;

	if (!(idx=skill_can_produce_mix(sd,nameid,-1, qty)))
		return 0;
	idx--;

	if (qty < 1)
		qty = 1;

	temp_qty = qty;

	if (!skill_id) //A skill can be specified for some override cases.
		skill_id = skill_produce_db[idx].req_skill;

	if( skill_id == GC_RESEARCHNEWPOISON )
		skill_id = GC_CREATENEWPOISON;

	slot[0]=slot1;
	slot[1]=slot2;
	slot[2]=slot3;

	for(i=0,sc=0,ele=0;i<3;i++){ //Note that qty should always be one if you are using these!
		int j;
		if( slot[i]<=0 )
			continue;
		j = pc_search_inventory(sd,slot[i]);
		if(j < 0)
			continue;
		if(slot[i]== ITEMID_STAR_CRUMB){
			pc_delitem(sd,j,1,1,0,LOG_TYPE_PRODUCE);
			sc++;
		}
		if(slot[i]>=ITEMID_FLAME_HEART && slot[i]<=ITEMID_GREAT_NATURE && ele==0){
			static const int ele_table[4]={3,1,4,2};
			pc_delitem(sd,j,1,1,0,LOG_TYPE_PRODUCE);
			ele=ele_table[slot[i]-ITEMID_FLAME_HEART];
		}
	}

	if( skill_id == RK_RUNEMASTERY )
	{
		skill_lv = pc_checkskill(sd,skill_id);
		if( skill_lv == 10 )
			temp_qty = 1 + rnd()%3;
		else if( skill_lv >= 5 )
			temp_qty = 1 + rnd()%2;
		else
			temp_qty = 1;

		for( i = 0; i < MAX_INVENTORY; i++ )
		{
			if( sd->inventory.u.items_inventory[i].nameid == nameid )
			{
				if( temp_qty > MAX_RUNE - sd->inventory.u.items_inventory[i].amount )
				{
					clif_msg(sd, RUNE_CANT_CREATE);
					return 0;
				}
			}
		}
	}

	for(i=0;i<MAX_PRODUCE_RESOURCE;i++){
		int j,x;
		t_itemid id;
		if( (id=skill_produce_db[idx].mat_id[i]) <= 0 )
			continue;
		num++;
		x=qty*skill_produce_db[idx].mat_amount[i];
		do{
			int y=0;
			j = pc_search_inventory(sd,id);

			if(j >= 0){
				y = sd->inventory.u.items_inventory[j].amount;
				if(y>x)y=x;
				pc_delitem(sd,j,y,0,0,LOG_TYPE_PRODUCE);
			} else {
				ShowError("skill_produce_mix: material item error\n");
				return 0;
			}

			x-=y;
		}while( j>=0 && x>0 );
	}

	if ((equip = (itemdb_isequip(nameid) && skill_id != GN_CHANGEMATERIAL && skill_id != GN_MAKEBOMB )))
		wlv = itemdb_wlv(nameid);
	if (!equip) 
	{
		switch(skill_id)
		{
			case BS_IRON:
			case BS_STEEL:
			case BS_ENCHANTEDSTONE:
				// Ores & Metals Refining - skill bonuses are straight from kRO website [DracoRPG]
				i = pc_checkskill(sd,skill_id);
				make_per = sd->status.job_level * 20 + status->dex * 10 + status->luk * 10; //Base chance
				switch(nameid)
				{
					case ITEMID_IRON:
						make_per += 4000+i*500; // Temper Iron bonus: +26/+32/+38/+44/+50
						break;
					case ITEMID_STEEL:
						make_per += 3000+i*500; // Temper Steel bonus: +35/+40/+45/+50/+55
						break;
					case ITEMID_STAR_CRUMB:
						make_per = 100000; // Star Crumbs are 100% success crafting rate? (made 1000% so it succeeds even after penalties) [Skotlex]
						break;
					default: // Enchanted Stones
						make_per += 1000+i*500; // Enchantedstone Craft bonus: +15/+20/+25/+30/+35
					break;
				}
				break;
			case ASC_CDP:
				make_per = (2000 + 40*status->dex + 20*status->luk);
				break;
			case AL_HOLYWATER:
				make_per = 100000; //100% success
				break;
			case AM_PHARMACY: // Potion Preparation - reviewed with the help of various Ragnainfo sources [DracoRPG]
			case AM_TWILIGHT1:
			case AM_TWILIGHT2:
			case AM_TWILIGHT3:
				make_per = pc_checkskill(sd,AM_LEARNINGPOTION)*50 + pc_checkskill(sd,AM_PHARMACY) * 300 + sd->status.job_level * 20 + (status->int_/2)*10 + status->dex*10+status->luk*10;
				if(hom_is_active(sd->hd)) 
				{//Player got a homun
					int skill;
					if ((skill = hom_checkskill(sd->hd, HVAN_INSTRUCT)) > 0) //His homun is a vanil with instruction change
						make_per += skill*100; //+1% bonus per level
				}
				switch(nameid){
					case ITEMID_RED_POTION:
					case ITEMID_YELLOW_POTION:
					case ITEMID_WHITE_POTION:
						make_per += (1+rnd()%100)*10 + 2000;
						break;
					case ITEMID_ALCOHOL:
						make_per += (1+rnd()%100)*10 + 1000;
						break;
					case ITEMID_FIRE_BOTTLE:
					case ITEMID_ACID_BOTTLE:
					case ITEMID_MAN_EATER_BOTTLE:
					case ITEMID_MINI_BOTTLE:
						make_per += (1+rnd()%100)*10;
						break;
					case ITEMID_YELLOW_SLIM_POTION:
						make_per -= (1+rnd()%50)*10;
						break;
					case ITEMID_WHITE_SLIM_POTION:
					case ITEMID_COATING_BOTTLE:
						make_per -= (1+rnd()%100)*10;
					    break;
					//Common items, recieve no bonus or penalty, listed just because they are commonly produced
					case ITEMID_BLUE_POTION:
					case ITEMID_RED_SLIM_POTION:
					case ITEMID_ANODYNE:
					case ITEMID_ALOEBERA:
					default:
						break;
				}
				if(battle_config.pp_rate != 100)
					make_per = make_per * battle_config.pp_rate / 100;
				break;
			case SA_CREATECON: // Elemental Converter Creation
				make_per = 100000; // should be 100% success rate
				break;
			case RK_RUNEMASTERY://Note: The success rate works on a 100.00 scale. A value of 10000 would equal 100% Remember this. [Rytech]
				make_per = (50 + 2 * pc_checkskill(sd,skill_id)) * 100 // Base success rate and success rate increase from learned Rune Mastery level.
				+ status->dex / 3 * 10 + status->luk * 10 + sd->status.job_level * 10 // Success increase from DEX, LUK, and job level.
				+ sd->menuskill_itemused * 100;// Quality of the rune ore used. Values are 2, 5, 8, 11, and 14.
				switch ( nameid )// Success reduction from rune stone rank. Each rune has a different rank. Values are 5, 10, 15, and 20.
				{
					case ITEMID_BERKANA_RUNE:// Berkana / RK_MILLENNIUMSHIELD
					case ITEMID_LUX_ANIMA_RUNE:// Lux Anima / RK_LUXANIMA
						make_per -= 20 * 100;//S-Rank Reduction
						break;
					case ITEMID_NAUTHIZ_RUNE:// Nauthiz / RK_REFRESH
					case ITEMID_URUZ_RUNE:// Uruz / RK_ABUNDANCE
						make_per -= 15 * 100;//A-Rank Reduction
						break;
					case ITEMID_ISA_RUNE:// Isa / RK_VITALITYACTIVATION
					case ITEMID_PERTHRO_RUNE:// Perthro / RK_STORMBLAST
						make_per -= 10 * 100;//B-Rank Reduction
						break;
					case ITEMID_RAIDO_RUNE:// Raido / RK_CRUSHSTRIKE
					case ITEMID_EIHWAZ_RUNE:// Eihwaz / RK_FIGHTINGSPIRIT
					case ITEMID_THURISAZ_RUNE:// Thurisaz / RK_GIANTGROWTH
					case ITEMID_HAGALAZ_RUNE:// Hagalaz / RK_STONEHARDSKIN
						make_per -= 5 * 100;//C-Rank Reduction
						break;
				}
				qty = temp_qty;
				break;
			case GC_CREATENEWPOISON:
				make_per = 3000 + 500 * pc_checkskill(sd,GC_RESEARCHNEWPOISON) // Base success rate and success rate increase from learned Research New Poison level.
				+ status->dex / 3 * 10 + status->luk * 10 + sd->status.job_level * 10;// Success increase from DEX, LUK, and job level.
				qty = rnd_value( (3 + pc_checkskill(sd,GC_RESEARCHNEWPOISON)) / 2, (8 + pc_checkskill(sd,GC_RESEARCHNEWPOISON)) / 2 );
				break;
			case GN_CHANGEMATERIAL:
				for (i = 0; i < MAX_SKILL_PRODUCE_DB; i++)
					if (skill_changematerial_db[i].itemid == nameid) {
						make_per = skill_changematerial_db[i].rate * 10;
						break;
					}
				break;
			case GN_S_PHARMACY:
				{
					int difficulty = 0;

					difficulty = (620 - 20 * skill_lv);// (620 - 20 * Skill Level)

					make_per = status->int_ + status->dex / 2 + status->luk + sd->status.job_level + (30 + rnd() % 120) + // (Caster�s INT) + (Caster�s DEX / 2) + (Caster�s LUK) + (Caster�s Job Level) + Random number between (30 ~ 150) +
						(sd->status.base_level - 100) + pc_checkskill(sd, AM_LEARNINGPOTION) + pc_checkskill(sd, CR_FULLPROTECTION)*(4 + rnd() % 6); // (Caster�s Base Level - 100) + (Potion Research x 5) + (Full Chemical Protection Skill Level) x (Random number between 4 ~ 10)

					switch (nameid) {// difficulty factor
						case ITEMID_HP_INCREASE_POTION_SMALL:
						case ITEMID_SP_INCREASE_POTION_SMALL:
						case ITEMID_CONCENTRATED_WHITE_POTION_Z:
							difficulty += 10;
							break;
						case ITEMID_BOMB_MUSHROOM_SPORE:
						case ITEMID_SP_INCREASE_POTION_MEDIUM:
							difficulty += 15;
							break;
						case ITEMID_BANANA_BOMB:
						case ITEMID_HP_INCREASE_POTION_MEDIUM:
						case ITEMID_SP_INCREASE_POTION_LARGE:
						case ITEMID_VITATA500:
							difficulty += 20;
							break;
						case ITEMID_SEED_OF_HORNY_PLANT:
						case ITEMID_BLOODSUCK_PLANT_SEED:
						case ITEMID_CONCENTRATED_CEROMAIN_SOUP:
							difficulty += 30;
							break;
						case ITEMID_HP_INCREASE_POTION_LARGE:
						case ITEMID_CURE_FREE:
							difficulty += 40;
							break;
					}

					if (make_per >= 400 && make_per > difficulty)
						qty = 10;
					else if (make_per >= 300 && make_per > difficulty)
						qty = 7;
					else if (make_per >= 100 && make_per > difficulty)
						qty = 6;
					else if (make_per >= 1 && make_per > difficulty)
						qty = 5;
					else
						qty = 4;
					make_per = 10000;
				}
				break;
			case GN_MAKEBOMB:
			case GN_MIX_COOKING:
				{
					int difficulty = 30 + rnd() % 120; // Random number between (30 ~ 150)

					make_per = sd->status.job_level / 4 + status->luk / 2 + status->dex / 3; // (Caster�s Job Level / 4) + (Caster�s LUK / 2) + (Caster�s DEX / 3)
					qty = ~(5 + rnd() % 5) + 1;

					switch (nameid) {// difficulty factor
						case ITEMID_APPLE_BOMB:
							difficulty += 5;
							break;
						case ITEMID_COCONUT_BOMB:
						case ITEMID_MELON_BOMB:
							difficulty += 10;
							break;
						case ITEMID_SAVAGE_FULL_ROAST:	case ITEMID_COCKTAIL_WARG_BLOOD:	case ITEMID_MINOR_STEW:
						case ITEMID_SIROMA_ICED_TEA:	case ITEMID_DROSERA_HERB_SALAD:		case ITEMID_PETITE_TAIL_NOODLES:
						case ITEMID_PINEAPPLE_BOMB:
							difficulty += 15;
							break;
						case ITEMID_BANANA_BOMB:
							difficulty += 20;
							break;
					}

					if (make_per >= 30 && make_per > difficulty)
						qty = 10 + rnd() % 2;
					else if (make_per >= 10 && make_per > difficulty)
						qty = 10;
					else if (make_per == 10 && make_per > difficulty)
						qty = 8;
					else if ((make_per >= 50 || make_per < 30) && make_per < difficulty)
						;// Food/Bomb creation fails.
					else if (make_per >= 30 && make_per < difficulty)
						qty = 5;

					if (qty < 0 || (skill_lv == 1 && make_per < difficulty)) {
						qty = ~qty + 1;
						make_per = 0;
					}
					else
						make_per = 10000;
					qty = (skill_lv > 1 ? qty : 1);
				}
				break;
			default:
				if( sd->menuskill_id == AM_PHARMACY && sd->menuskill_val > 10 && sd->menuskill_val <= 20 )
				{	//Assume Cooking Dish
					if ( sd->menuskill_val >= 15 ) //Legendary Cooking Set.
						make_per = 10000; //100% Success
					else
						make_per = 1200 * ( sd->menuskill_val - 10 )
							+ 20  * ( sd->status.base_level + 1 )
							+ 20  * ( status->dex + 1 )
							+ 100 * ( rnd() % ( 30 + 5 * ( sd->cook_mastery / 400 ) - ( 6 + sd->cook_mastery / 80 ) ) + ( 6 + sd->cook_mastery / 80 ) )
							- 400 * ( skill_produce_db[idx].itemlv - 11 + 1 )
							- 10  * ( 100 - status->luk + 1 )
							- 500 * ( num - 1 )
							- 100 * ( rnd() % 4 + 1 );
					break;
				}
				make_per = 5000;
				break;
		}
	} 
	else 
	{ // Weapon Forging - skill bonuses are straight from kRO website, other things from a jRO calculator [DracoRPG]
		make_per = 5000 + ((sd->class_&JOBL_THIRD) ? 1400 : sd->status.job_level * 20) + status->dex * 10 + status->luk * 10; // Base
		make_per += pc_checkskill (sd, skill_id) * 500; // Smithing skills bonus: +5/+10/+15
		make_per += pc_checkskill (sd, BS_WEAPONRESEARCH) * 100 +((wlv >= 3) ? pc_checkskill(sd, BS_ORIDEOCON) * 100 : 0); // Weaponry Research bonus: +1/+2/+3/+4/+5/+6/+7/+8/+9/+10, Oridecon Research bonus (custom): +1/+2/+3/+4/+5
		make_per -= (ele ? 2000 : 0) + sc * 1500 + (wlv > 1 ? wlv * 1000 : 0); // Element Stone: -20%, Star Crumb: -15% each, Weapon level malus: -0/-20/-30
		if (pc_search_inventory(sd, ITEMID_EMPERIUM_ANVIL) > 0) make_per += 1000; // Emperium Anvil: +10
		else if (pc_search_inventory(sd, ITEMID_GOLDEN_ANVIL) > 0) make_per += 500; // Golden Anvil: +5
		else if (pc_search_inventory(sd, ITEMID_ORIDECON_ANVIL) > 0) make_per += 300; // Oridecon Anvil: +3
		else if (pc_search_inventory(sd, ITEMID_ANVIL) > 0) make_per += 0; // Anvil: +0?
		if(battle_config.wp_rate != 100)
			make_per = make_per * battle_config.wp_rate / 100;
	}

	if (battle_config.baby_crafting_penalty == 1 && sd->class_&JOBL_BABY) //if it's a Baby Class
		make_per = (make_per * 50) / 100; //Baby penalty is 50%

	if(make_per < 1) make_per = 1;


	if(rnd()%10000 < make_per || qty > 1){ //Success, or crafting multiple items.
		struct item tmp_item;
		memset(&tmp_item,0,sizeof(tmp_item));
		tmp_item.nameid=nameid;
		tmp_item.amount=1;
		tmp_item.identify=1;
		if(equip){
			tmp_item.card[0]=CARD0_FORGE;
			tmp_item.card[1]=((sc*5)<<8)+ele;
			tmp_item.card[2]=GetWord(sd->status.char_id,0); // CharId
			tmp_item.card[3]=GetWord(sd->status.char_id,1);
		} else {
			//Flag is only used on the end, so it can be used here. [Skotlex]
			switch (skill_id) {
				case BS_DAGGER:
				case BS_SWORD:
				case BS_TWOHANDSWORD:
				case BS_AXE:
				case BS_MACE:
				case BS_KNUCKLE:
				case BS_SPEAR:
					flag = battle_config.produce_item_name_input&0x1;
					break;
				case AM_PHARMACY:
				case AM_TWILIGHT1:
				case AM_TWILIGHT2:
				case AM_TWILIGHT3:
					flag = battle_config.produce_item_name_input&0x2;
					break;
				case AL_HOLYWATER:
					flag = battle_config.produce_item_name_input&0x8;
					break;
				case ASC_CDP:
					flag = battle_config.produce_item_name_input&0x10;
					break;
				default:
					flag = battle_config.produce_item_name_input&0x80;
					break;
			}
			if (flag) {
				tmp_item.card[0]=CARD0_CREATE;
				tmp_item.card[1]=0;
				tmp_item.card[2]=GetWord(sd->status.char_id,0); // CharId
				tmp_item.card[3]=GetWord(sd->status.char_id,1);
			}
		}

//		if(log_config.produce > 0)
//			log_produce(sd,nameid,slot1,slot2,slot3,1);
//TODO update PICKLOG

		if (equip)
		{
			clif_produceeffect(sd, 0, nameid);
			clif_misceffect(&sd->bl, 3);
			if(itemdb_wlv(nameid) >= 3 && ((ele? 1 : 0) + sc) >= 3) // Fame point system [DracoRPG]
				pc_addfame(sd, 10); // Success to forge a lv3 weapon with 3 additional ingredients = +10 fame point
		} else {
			int fame = 0;
			tmp_item.amount = 0;

			for (i = 0; i < qty; i++) {	//Apply quantity modifiers.
				if ((skill_id == GN_MIX_COOKING || skill_id == GN_MAKEBOMB || skill_id == GN_S_PHARMACY) && make_per > 1) {
					tmp_item.amount = qty;
					break;
				}
				if (rnd() % 10000 < make_per || qty == 1) { //Success
					tmp_item.amount++;
					if (nameid < ITEMID_RED_SLIM_POTION || nameid > ITEMID_WHITE_SLIM_POTION)
						continue;
					if (skill_id != AM_PHARMACY &&
						skill_id != AM_TWILIGHT1 &&
						skill_id != AM_TWILIGHT2 &&
						skill_id != AM_TWILIGHT3)
						continue;
					//Add fame as needed.
					switch (++sd->potion_success_counter) {
					case 3:
						fame += 1; // Success to prepare 3 Condensed Potions in a row
						break;
					case 5:
						fame += 3; // Success to prepare 5 Condensed Potions in a row
						break;
					case 7:
						fame += 10; // Success to prepare 7 Condensed Potions in a row
						break;
					case 10:
						fame += 50; // Success to prepare 10 Condensed Potions in a row
						sd->potion_success_counter = 0;
						break;
					}
				}
				else //Failure
					sd->potion_success_counter = 0;
			}

			if (fame)
				pc_addfame(sd,fame);
			//Visual effects and the like.
			switch (skill_id) {
				case AM_PHARMACY:
				case AM_TWILIGHT1:
				case AM_TWILIGHT2:
				case AM_TWILIGHT3:
				case ASC_CDP:
					clif_produceeffect(sd,2,nameid);
					clif_misceffect(&sd->bl,5);
					break;
				case BS_IRON:
				case BS_STEEL:
				case BS_ENCHANTEDSTONE:
					clif_produceeffect(sd,0,nameid);
					clif_misceffect(&sd->bl,3);
					break;
				case RK_RUNEMASTERY:
				case GC_CREATENEWPOISON:
					clif_produceeffect(sd,2,nameid);
					clif_misceffect(&sd->bl,5);
					break;
				default: //Those that don't require a skill?
					if (skill_produce_db[idx].itemlv > 10 && skill_produce_db[idx].itemlv <= 20)
					{ //Cooking items.
						clif_specialeffect(&sd->bl, 608, AREA);
						if (sd->cook_mastery < 1999)
							pc_setglobalreg(sd, "COOK_MASTERY", sd->cook_mastery + (1 << ((skill_produce_db[idx].itemlv - 11) / 2)) * 5);
					}
					break;
			}
		}

		if (skill_id == GN_CHANGEMATERIAL && tmp_item.amount) { //Success
			int j, k = 0, l;
			bool isStackable = itemdb_isstackable(tmp_item.nameid);

			for (i = 0; i < MAX_SKILL_PRODUCE_DB; i++)
				if (skill_changematerial_db[i].itemid == nameid) {
					for (j = 0; j < 5; j++) {
						if (rnd() % 1000 < skill_changematerial_db[i].qty_rate[j]) {
							uint16 total_qty = qty * skill_changematerial_db[i].qty[j];
							tmp_item.amount = (isStackable ? total_qty : 1);
							for (l = 0; l < total_qty; l += tmp_item.amount) {
								if ((flag = pc_additem(sd, &tmp_item, tmp_item.amount, LOG_TYPE_PRODUCE))) {
									clif_additem(sd, 0, 0, flag);
									map_addflooritem(&tmp_item, tmp_item.amount, sd->bl.m, sd->bl.x, sd->bl.y, 0, 0, 0, 0, 0, false);
								}
							}
							k++;
						}
					}
					break;
				}
			if (k) {
				clif_msg_skill(sd, skill_id, ITEM_PRODUCE_SUCCESS);
				return 1;
			}
		}
		else if (tmp_item.amount) { //Success
			if((flag = pc_additem(sd, &tmp_item, tmp_item.amount, LOG_TYPE_PRODUCE)))
			{
				clif_additem(sd, 0, 0, flag);
				map_addflooritem(&tmp_item, tmp_item.amount, sd->bl.m, sd->bl.x, sd->bl.y, 0, 0, 0, 0, 0, false);
			}
			if (skill_id == GN_MIX_COOKING || skill_id == GN_MAKEBOMB || skill_id == GN_S_PHARMACY)
				clif_msg_skill(sd, skill_id, ITEM_PRODUCE_SUCCESS);
			return 1;
		}
	}
	//Failure
//	if(log_config.produce)
//		log_produce(sd,nameid,slot1,slot2,slot3,0);
//TODO update PICKLOG

	if (equip)
	{
		clif_produceeffect(sd, 1, nameid);
		clif_misceffect(&sd->bl, 2);
	}
	else {
		switch (skill_id) {
			case ASC_CDP: //25% Damage yourself, and display same effect as failed potion.
				status_percent_damage(NULL, &sd->bl, -25, 0, true);
			case AM_PHARMACY:
			case AM_TWILIGHT1:
			case AM_TWILIGHT2:
			case AM_TWILIGHT3:
				clif_produceeffect(sd,3,nameid);
				clif_misceffect(&sd->bl,6);
				sd->potion_success_counter = 0; // Fame point system [DracoRPG]
				break;
			case BS_IRON:
			case BS_STEEL:
			case BS_ENCHANTEDSTONE:
				clif_produceeffect(sd,1,nameid);
				clif_misceffect(&sd->bl,2);
				break;
			case RK_RUNEMASTERY:
			case GC_CREATENEWPOISON:
				clif_produceeffect(sd,3,nameid);
				clif_misceffect(&sd->bl,6);
				break;
			case GN_MIX_COOKING:
				{
					struct item tmp_item;
					const int compensation[5] = { ITEMID_DARK_LUMP, ITEMID_HARD_DARK_LUMP, ITEMID_VERY_HARD_DARK_LUMP, ITEMID_BLACK_MASS, ITEMID_MYSTERIOUS_POWDER };
					int rate = rnd() % 500;
					memset(&tmp_item, 0, sizeof(tmp_item));
					if (rate < 50) i = 4;
					else if (rate < 100) i = 2 + rnd() % 1;
					else if (rate < 250) i = 1;
					else if (rate < 500) i = 0;
					tmp_item.nameid = compensation[i];
					tmp_item.amount = qty;
					tmp_item.identify = 1;
					if (flag = pc_additem(sd, &tmp_item, tmp_item.amount, LOG_TYPE_PRODUCE)) {
						clif_additem(sd, 0, 0, flag);
						map_addflooritem(&tmp_item, tmp_item.amount, sd->bl.m, sd->bl.x, sd->bl.y, 0, 0, 0, 0, 0, false);
					}
					clif_msg_skill(sd, skill_id, ITEM_PRODUCE_FAIL);
				}
				break;
			case GN_MAKEBOMB:
			case GN_S_PHARMACY:
			case GN_CHANGEMATERIAL:
				clif_msg_skill(sd, skill_id, ITEM_PRODUCE_FAIL);
				break;
			default:
				if( skill_produce_db[idx].itemlv > 10 && skill_produce_db[idx].itemlv <= 20 )
				{ //Cooking items.
					clif_specialeffect(&sd->bl, 609, AREA);
					if( sd->cook_mastery > 0 )
						pc_setglobalreg(sd, "COOK_MASTERY", sd->cook_mastery - ( 1 << ((skill_produce_db[idx].itemlv - 11) / 2) ) - ( ( ( 1 << ((skill_produce_db[idx].itemlv - 11) / 2) ) >> 1 ) * 3 ));
				}
		}
	}
	return 0;
}

int skill_arrow_create (struct map_session_data *sd, t_itemid nameid)
{
	int i,j,flag,index=-1;
	struct item tmp_item;

	nullpo_ret(sd);

	if (!nameid || !itemdb_exists(nameid))
		return 1;

	for(i=0;i<MAX_SKILL_ARROW_DB;i++)
		if(nameid == skill_arrow_db[i].nameid) {
			index = i;
			break;
		}

	if (!index || (j = pc_search_inventory(sd,nameid)) < 0)
		return 1;

	pc_delitem(sd,j,1,0,0,LOG_TYPE_PRODUCE);
	for(i=0;i<MAX_ARROW_RESOURCE;i++) {
		memset(&tmp_item,0,sizeof(tmp_item));
		tmp_item.identify = 1;
		tmp_item.nameid = skill_arrow_db[index].cre_id[i];
		tmp_item.amount = skill_arrow_db[index].cre_amount[i];
		if(battle_config.produce_item_name_input&0x4) {
			tmp_item.card[0]=CARD0_CREATE;
			tmp_item.card[1]=0;
			tmp_item.card[2]=GetWord(sd->status.char_id,0); // CharId
			tmp_item.card[3]=GetWord(sd->status.char_id,1);
		}
		if(tmp_item.nameid <= 0 || tmp_item.amount <= 0)
			continue;
		if((flag = pc_additem(sd,&tmp_item,tmp_item.amount,LOG_TYPE_PRODUCE))) {
			clif_additem(sd,0,0,flag);
			map_addflooritem(&tmp_item,tmp_item.amount,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0,0,false);
		}
	}

	return 0;
}

int skill_poisoningweapon( struct map_session_data *sd, t_itemid nameid)
{

	sc_type type;
	int t_lv = 0, chance, i;

	nullpo_retr(0, sd);

	if(!nameid || (i = pc_search_inventory(sd,nameid)) < 0 )
	{
		clif_skill_fail(sd, GC_POISONINGWEAPON, 0, 0, 0);
		return 0;
	}

	if( (i = pc_search_inventory(sd,nameid)) == -1 )
	{ // prevent hacking
		clif_skill_fail(sd, GC_POISONINGWEAPON, 0, 0, 0);
		return 0;
	}

	pc_delitem(sd, i, 1, 0, 1, LOG_TYPE_CONSUME);

	switch( nameid ) { // t_lv used to take duration from skill_get_time2
 		case ITEMID_PARALYSIS_POISON:		type = SC_PARALYSE;      t_lv = 1; break;
		case ITEMID_FEVER_POISON:			type = SC_PYREXIA;       t_lv = 2; break;
		case ITEMID_CONTAMINATION_POISON:   type = SC_DEATHHURT;     t_lv = 3; break;
		case ITEMID_LEECH_POISON:			type = SC_LEECHESEND;    t_lv = 4; break;
		case ITEMID_FATIGUE_POISON:			type = SC_VENOMBLEED;    t_lv = 6; break;
		case ITEMID_NUMB_POISON:			type = SC_TOXIN;         t_lv = 7; break;
		case ITEMID_LAUGHING_POISON:		type = SC_MAGICMUSHROOM; t_lv = 8; break;
 		case ITEMID_OBLIVION_POISON:		type = SC_OBLIVIONCURSE; t_lv = 9; break;
		default:
			clif_skill_fail(sd, GC_POISONINGWEAPON, USESKILL_FAIL_LEVEL, 0, 0);
			return 0;
	}

	status_change_end(&sd->bl, SC_POISONINGWEAPON, INVALID_TIMER);//Status must be forced to end so that a new poison will be applied if a player decides to change poisons. [Rytech]
	chance = 2 + 2 * sd->menuskill_itemused; // 2 + 2 * skill_lv

	sc_start4(&sd->bl,SC_POISONINGWEAPON,100,t_lv,type,chance,0,skill_get_time(GC_POISONINGWEAPON,sd->menuskill_itemused));
	sd->menuskill_itemused = 0;

	return 0;
}

void skill_toggle_magicpower(struct block_list *bl, short skill_id)
{
	struct status_change *sc = status_get_sc(bl);

	// non-offensive and non-magic skills do not affect the status
	if (skill_get_nk(skill_id)&NK_NO_DAMAGE || !(skill_get_type(skill_id)&BF_MAGIC))
		return;

	if (sc && sc->count && sc->data[SC_MAGICPOWER])
	{
		if (sc->data[SC_MAGICPOWER]->val4)
		{
			status_change_end(bl, SC_MAGICPOWER, INVALID_TIMER);
		}
		else
		{
			sc->data[SC_MAGICPOWER]->val4 = 1;
			status_calc_bl(bl, status_sc2scb_flag(SC_MAGICPOWER));
			if (bl->type == BL_PC) {// update current display.
				clif_updatestatus(((TBL_PC *)bl), SP_MATK1);
				clif_updatestatus(((TBL_PC *)bl), SP_MATK2);
			}
		}
	}
}

int skill_magicdecoy(struct map_session_data *sd, t_itemid nameid)
{
	short item = 0;
	short mobid = 0;
	short skill_lv = sd->menuskill_val;
	int x = sd->menuskill_val2 >> 16;
	int y = sd->menuskill_val2 & 0xffff;
	struct mob_data *md;

	nullpo_retr(0, sd);

	if (!nameid || !itemid_is_element_point(nameid) || (item = pc_search_inventory(sd, nameid)) < 0)
		return 1;

	pc_delitem(sd, item, 1, 0, 0, LOG_TYPE_CONSUME);

	switch (nameid)
	{
		case ITEMID_SCARLETT_POINT:
			mobid = MOBID_MAGICDECOY_FIRE;
			break;
		case ITEMID_INDIGO_POINT:
			mobid = MOBID_MAGICDECOY_WATER;
			break;
		case ITEMID_LIME_GREEN_POINT:
			mobid = MOBID_MAGICDECOY_EARTH;
			break;
		case ITEMID_YELLOW_WISH_POINT:
			mobid = MOBID_MAGICDECOY_WIND;
			break;
	}

	md = mob_once_spawn_sub(&sd->bl, sd->bl.m, x, y, status_get_name(&sd->bl),mobid,"",0,AI_NONE);
	if (md)
	{
		md->master_id = sd->bl.id;
		md->special_state.ai = AI_FAW;
		if( md->deletetimer != INVALID_TIMER )
			delete_timer(md->deletetimer, mob_timer_delete);
		md->deletetimer = add_timer (gettick() + skill_get_time(NC_MAGICDECOY,skill_lv), mob_timer_delete, md->bl.id, 0);
		mob_spawn (md);

		// Set MATK and MaxHP
		md->status.matk_min = md->status.matk_max = 250 + 50 * skill_lv;
		md->status.hp = md->status.max_hp = 1000 * skill_lv + 12 * status_get_lv(&sd->bl) + 4 * status_get_max_sp(&sd->bl);
	}

	sd->menuskill_val = sd->menuskill_val2 = 0;

	return 0;
}

// Warlock Spellbooks. [LimitLine]
int skill_spellbook (struct map_session_data *sd, t_itemid nameid)
{
	int i, j, points, skill_id, preserved = 0, max_preserve;
	nullpo_retr(0,sd);
	
	if( sd->sc.data[SC_STOP] ) status_change_end(&sd->bl,SC_STOP,-1);
	if( nameid <= 0 ) return 0;

	if( pc_search_inventory(sd,nameid) < 0 )
	{ // User with no item on inventory
		clif_skill_fail(sd,WL_READING_SB,0x04,0,0);
		return 0;
	}

	ARR_FIND(0,MAX_SPELLBOOK,j,sd->rsb[j].skill_id == 0); // Search for a free slot
	if( j == MAX_SPELLBOOK )
	{ // No more free slots
		clif_skill_fail(sd,WL_READING_SB,0x04,0,0);
		return 0;
	}

	ARR_FIND(0,MAX_SKILL_SPELLBOOK_DB,i,skill_spellbook_db[i].nameid == nameid); // Search for information of this item
	if( i == MAX_SKILL_SPELLBOOK_DB )
	{ // Fake nameid
		clif_skill_fail(sd,WL_READING_SB,0x04,0,0);
		return 0;
	}

	skill_id = skill_spellbook_db[i].skill_id;
	points = skill_spellbook_db[i].points;

	if( !pc_checkskill(sd,skill_id) )
	{ // User don't know the skill
		sc_start(&sd->bl,SC_SLEEP,100,1,skill_get_time(WL_READING_SB,pc_checkskill(sd,WL_READING_SB)));
		clif_skill_fail(sd,WL_READING_SB,0x34,0,0);
		return 0;
	}

	max_preserve = 4 * pc_checkskill(sd, WL_FREEZE_SP) + status_get_int(&sd->bl) / 10 + status_get_base_lv_effect(&sd->bl) / 10;

	for( i = 0; i < MAX_SPELLBOOK && sd->rsb[i].skill_id; i++ )
		preserved += sd->rsb[i].points;

	if( preserved + points >= max_preserve )
	{ // No more free points
		clif_skill_fail(sd,WL_READING_SB,0x04,0,0);
		return 0;
	}

	sd->rsb[j].skill_id = skill_id;
	sd->rsb[j].level = pc_checkskill(sd,skill_id);
	sd->rsb[j].points = points;
	sc_start2(&sd->bl,SC_READING_SB,100,0,preserved+points,-1);

	return 1;
}

int skill_select_menu(struct map_session_data *sd,int flag,int skill_id)
{
	short autoshadowlv;
	short autocastid;
	short autocastlv;
	short maxlv = skill_get_max(skill_id);

	nullpo_ret(sd);

	if (sd->sc.count && sd->sc.data[SC_STOP])
	{
		autoshadowlv = sd->sc.data[SC_STOP]->val1;
		status_change_end(&sd->bl,SC_STOP,INVALID_TIMER);
	}
	else
		autoshadowlv = 10;// Safety.

	// First check to see if skill is a copied skill to protect against forged packets.
	// Then check to see if its a skill that can be autocasted through auto shadow spell.
	if ((skill_id != sd->status.skill[sd->cloneskill_idx].id && skill_id != sd->status.skill[sd->reproduceskill_idx].id) || 
		!(skill_id == MG_NAPALMBEAT || 
		skill_id >= MG_SOULSTRIKE && skill_id <= MG_FROSTDIVER || 
		skill_id >= MG_FIREBALL && skill_id <= MG_THUNDERSTORM || 
		skill_id == AL_HEAL || skill_id == WZ_FIREPILLAR || skill_id == WZ_SIGHTRASHER || 
		skill_id >= WZ_METEOR && skill_id <= WZ_WATERBALL || 
		skill_id >= WZ_FROSTNOVA && skill_id <= WZ_HEAVENDRIVE))
	{
		clif_skill_fail(sd,SC_AUTOSHADOWSPELL,USESKILL_FAIL_LEVEL,0,0);
		return 0;
	}

	autocastid = skill_id;// The skill that will be autocasted.
	autocastlv = (autoshadowlv + 5) / 2;// The level the skill will be autocasted.

	// Don't allow autocasting level's higher then the max possible for players.
	if ( autocastlv > maxlv )
		autocastlv = maxlv;

	sc_start4(&sd->bl,SC__AUTOSHADOWSPELL,100,autoshadowlv,autocastid,autocastlv,0,skill_get_time(SC_AUTOSHADOWSPELL,autoshadowlv));
	return 0;
}

int skill_elementalanalysis(struct map_session_data* sd, int n, int skill_lv, unsigned short* item_list)
{
	int i;

	nullpo_ret(sd);
	nullpo_ret(item_list);

	if( n <= 0 )
		return 1;

	for( i = 0; i < n; i++ )
	{
		int nameid, add_amount, del_amount, idx, product;
		struct item tmp_item;

		idx = item_list[i*2+0]-2;

		if (idx < 0 || idx >= MAX_INVENTORY) {
			return 1;
		}

		del_amount = item_list[i*2+1];

		if( skill_lv == 2 )
			del_amount -= (del_amount % 10);
		add_amount = (skill_lv == 1) ? del_amount * (5 + rnd()%5) : del_amount / 10 ;

		if( (nameid = sd->inventory.u.items_inventory[idx].nameid) <= 0 || del_amount > sd->inventory.u.items_inventory[idx].amount )
		{
			clif_skill_fail(sd,SO_EL_ANALYSIS,USESKILL_FAIL_LEVEL,0,0);
			return 1;
		}

		switch( nameid )
		{
			// Level 1
			case ITEMID_FLAME_HEART:		product = ITEMID_BLOODY_RED;		break;
			case ITEMID_MISTIC_FROZEN:		product = ITEMID_CRYSTAL_BLUE;		break;
			case ITEMID_ROUGH_WIND:			product = ITEMID_WIND_OF_VERDURE;	break;
			case ITEMID_GREAT_NATURE:		product = ITEMID_YELLOW_LIVE;		break;
			// Level 2
			case ITEMID_BLOODY_RED:			product = ITEMID_FLAME_HEART;		break;
			case ITEMID_CRYSTAL_BLUE:		product = ITEMID_MISTIC_FROZEN;		break;
			case ITEMID_WIND_OF_VERDURE:	product = ITEMID_ROUGH_WIND;		break;
			case ITEMID_YELLOW_LIVE:		product = ITEMID_GREAT_NATURE;		break;
			default:
				clif_skill_fail(sd,SO_EL_ANALYSIS,USESKILL_FAIL_LEVEL,0,0);
				return 1;
		}

		if( pc_delitem(sd,idx,del_amount,0,0,LOG_TYPE_CONSUME) )
		{
			clif_skill_fail(sd,SO_EL_ANALYSIS,USESKILL_FAIL_LEVEL,0,0);
			return 1;
		}

		if( skill_lv == 2 && rnd()%100 < 25 )
		{	// At level 2 have a fail chance. You loose your items if it fails.
			clif_skill_fail(sd,SO_EL_ANALYSIS,USESKILL_FAIL_LEVEL,0,0);
			return 1;
		}


		memset(&tmp_item,0,sizeof(tmp_item));
		tmp_item.nameid = product;
		tmp_item.amount = add_amount;
		tmp_item.identify = 1;

		if( tmp_item.amount )
		{
			int flag = pc_additem(sd, &tmp_item, tmp_item.amount, LOG_TYPE_PRODUCE);
			if (flag != 0)
			{
				clif_additem(sd,0,0,flag);
				map_addflooritem(&tmp_item,tmp_item.amount,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0,0,false);
			}
		}

	}

	return 0;
}

int skill_changematerial(struct map_session_data *sd, int n, unsigned short *item_list)
{
	int i, j, k, c, p = 0, amount;
	t_itemid nameid;
	
	nullpo_ret(sd);
	nullpo_ret(item_list);

	// Search for objects that can be created.
	for (i = 0; i < MAX_SKILL_PRODUCE_DB; i++)
	{
		if (skill_produce_db[i].itemlv == 26)
		{
			p = 0;
			do
			{
				c = 0;
				// Verification of overlap between the objects required and the list submitted.
				for (j = 0; j < MAX_PRODUCE_RESOURCE; j++)
				{
					if (skill_produce_db[i].mat_id[j] > 0)
					{
						for (k = 0; k < n; k++)
						{
							int idx = item_list[k * 2 + 0] - 2;

							if (idx < 0 || idx >= MAX_INVENTORY) {
								return 0;
							}

							nameid = sd->inventory.u.items_inventory[idx].nameid;
							amount = item_list[k * 2 + 1];

							if (nameid > 0 && sd->inventory.u.items_inventory[idx].identify == 0) {
								clif_msg_skill(sd, GN_CHANGEMATERIAL, ITEM_UNIDENTIFIED);
								return 0;
							}
							if (nameid == skill_produce_db[i].mat_id[j] && (amount - p * skill_produce_db[i].mat_amount[j]) >= skill_produce_db[i].mat_amount[j]
								&& (amount - p * skill_produce_db[i].mat_amount[j]) % skill_produce_db[i].mat_amount[j] == 0) // must be in exact amount
								c++; // match
						}
					}
					else
						break;	// No more items required
				}
				p++;
			} while(n == j && c == n);
			p--;
			if (p > 0)
			{
				skill_produce_mix(sd, GN_CHANGEMATERIAL, skill_produce_db[i].nameid, 0, 0, 0, p);
				return 1;
			}
		}
	}

	if (p == 0)
		clif_msg_skill(sd, GN_CHANGEMATERIAL, ITEM_CANT_COMBINE);

	return 0;
}

int skill_banding_count (struct map_session_data *sd)
{
	unsigned char count = party_foreachsamemap(party_sub_count_banding, sd, 3, 0);
	unsigned int group_hp = party_foreachsamemap(party_sub_count_banding, sd, 3, 1);

	nullpo_ret(sd);

	// HP is set to the average HP of the banding group.
	if ( count > 1 )
		status_set_hp(&sd->bl, group_hp/count, 0);

	// Royal Guard count check for banding.
	if ( sd && sd->status.party_id )
	{// There's no official max count but its best to limit it to the official max party size.
		if( count > 12)
			return 12;
		else if( count > 1)
			return count;//Effect bonus from additional Royal Guards if not above the max possiable.
	}

	return 0;
}

int skill_chorus_count (struct map_session_data *sd)
{
	unsigned char count = party_foreachsamemap(party_sub_count_chorus, sd, 15);

	nullpo_ret(sd);

	// Minstrel/Wanderer count check for chorus skills.
	// Bonus remains 0 unless 3 or more Minstrel's/Wanderer's are in the party.
	if ( sd && sd->status.party_id )
	{
		if (count > 7)
			return 5;//Maximum effect possiable from 7 or more Minstrel's/Wanderer's
		else if (count > 2)
			return (count - 2);//Effect bonus from additional Minstrel's/Wanderer's if not above the max possiable.
	}

	return 0;
}

int skill_akaitsuki_damage(struct block_list* src, struct block_list *bl, int damage, int skill_id, int skill_lv, int64 tick)
{
	nullpo_ret(src);
	nullpo_ret(bl);

	// Deals damage to the affected target if healed from one of the following skills....
	// AL_HEAL, PR_SANCTUARY, BA_APPLEIDUN, AB_RENOVATIO, AB_HIGHNESSHEAL, SO_WARMER
	damage = damage / 2;// Damage is half of the heal amount.
	clif_skill_damage(src, bl, tick, status_get_amotion(src), status_get_dmotion(bl), damage, 1, skill_id == AB_HIGHNESSHEAL ? AL_HEAL : skill_id, skill_lv, 6);
	status_zap(bl, damage, 0);
	return 0;
}

// Power Types
// 1 = Power of Life
// 2 = Power of Land
// 3 = Power of Sea
int skill_summoner_power(struct map_session_data *sd, unsigned char power_type)
{
	nullpo_ret(sd);

	if (power_type == POWER_OF_LIFE && pc_checkskill(sd, SU_POWEROFLIFE) > 0)
	{// 20 or more points invested in life skills?
		if ((pc_checkskill(sd, SU_PICKYPECK) + pc_checkskill(sd, SU_ARCLOUSEDASH) + pc_checkskill(sd, SU_SCAROFTAROU) +
			pc_checkskill(sd, SU_LUNATICCARROTBEAT) + pc_checkskill(sd, SU_HISS) + pc_checkskill(sd, SU_POWEROFFLOCK) +
			pc_checkskill(sd, SU_SVG_SPIRIT)) >= 20)
			return 1;
	}
	else if (power_type == POWER_OF_LAND && pc_checkskill(sd, SU_POWEROFLAND) > 0)
	{// 20 or more points invested in land skills?
		if ((pc_checkskill(sd, SU_SV_STEMSPEAR) + pc_checkskill(sd, SU_SV_ROOTTWIST) + pc_checkskill(sd, SU_CN_METEOR) +
			pc_checkskill(sd, SU_CN_POWDERING) + pc_checkskill(sd, SU_CHATTERING) + pc_checkskill(sd, SU_MEOWMEOW) +
			pc_checkskill(sd, SU_NYANGGRASS)) >= 20)
			return 1;
	}
	else if (power_type == POWER_OF_SEA && pc_checkskill(sd, SU_POWEROFSEA) > 0)
	{// 20 or more points invested in sea skills?
		if ((pc_checkskill(sd, SU_FRESHSHRIMP) + pc_checkskill(sd, SU_BUNCHOFSHRIMP) + pc_checkskill(sd, SU_TUNABELLY) +
			pc_checkskill(sd, SU_TUNAPARTY) + pc_checkskill(sd, SU_GROOMING) + pc_checkskill(sd, SU_PURRING) +
			pc_checkskill(sd, SU_SHRIMPARTY)) >= 20)
			return 1;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int skill_blockpc_get(struct map_session_data *sd, int skill_id)
{
	int i;
	nullpo_retr(-1,sd);

	ARR_FIND(0, MAX_SKILLCOOLDOWN, i, sd->scd[i] && sd->scd[i]->skill_id == skill_id);
	return (i >= MAX_SKILLCOOLDOWN) ? -1 : i;
}

int skill_blockpc_end(int tid, int64 tick, int id, intptr_t data)
{
	struct map_session_data *sd = map_id2sd(id);
	int i = (int)data;

	if( !sd || data < 0 || data >= MAX_SKILLCOOLDOWN )
		return 0;

	if( !sd->scd[i] || sd->scd[i]->timer != tid )
	{
		ShowWarning("skill_blockpc_end: Invalid Timer or not Skill Cooldown.\n");
		return 0;
	}

	aFree(sd->scd[i]);
	sd->scd[i] = NULL;
	return 1;
}

int skill_blockpc_start(struct map_session_data *sd, int skill_id, int tick)
{
	int i;
	nullpo_retr(-1,sd);
	if(!skill_id || tick < 1 )
		return -1;

	ARR_FIND(0,MAX_SKILLCOOLDOWN,i,sd->scd[i] && sd->scd[i]->skill_id == skill_id);
	if( i < MAX_SKILLCOOLDOWN )
	{ // Skill already with cooldown
		delete_timer(sd->scd[i]->timer,skill_blockpc_end);
		aFree(sd->scd[i]);
		sd->scd[i] = NULL;
	}

	ARR_FIND(0,MAX_SKILLCOOLDOWN,i,!sd->scd[i]);
	if( i < MAX_SKILLCOOLDOWN )
	{ // Free Slot found
		CREATE(sd->scd[i],struct skill_cooldown_entry, 1);
		sd->scd[i]->skill_id = skill_id;
		sd->scd[i]->timer = add_timer(gettick() + tick, skill_blockpc_end, sd->bl.id, i);

		if( battle_config.display_status_timers && tick > 0 )
			clif_skill_cooldown(sd,skill_id,tick);

		return 1;
	}
	else
	{
		ShowWarning("skill_blockpc_start: Too many skillcooldowns, increase MAX_SKILLCOOLDOWN.\n");
		return 0;
	}
}

int skill_blockpc_clear(struct map_session_data *sd)
{
	int i;
	nullpo_retr(0,sd);
	for( i = 0; i < MAX_SKILLCOOLDOWN; i++ )
	{
		if( !sd->scd[i] )
			continue;
		delete_timer(sd->scd[i]->timer,skill_blockpc_end);
		aFree(sd->scd[i]);
		sd->scd[i] = NULL;
	}
	return 1;
}

int skill_blockhomun_end(int tid, int64 tick, int id, intptr_t data)	//[orn]
{
	struct homun_data *hd = (TBL_HOM*) map_id2bl(id);
	if (data <= 0 || data >= MAX_SKILL)
		return 0;
	if (hd) hd->blockskill[data] = 0;

	return 1;
}

int skill_blockhomun_start(struct homun_data *hd, int skill_id, int tick)	//[orn]
{
	nullpo_retr (-1, hd);

	skill_id = skill_get_index(skill_id);
	if (!skill_id)
		return -1;

	if (tick < 1) {
		hd->blockskill[skill_id] = 0;
		return -1;
	}
	hd->blockskill[skill_id] = 1;
	return add_timer(gettick() + tick, skill_blockhomun_end, hd->bl.id, skill_id);
}

int skill_blockmerc_end(int tid, int64 tick, int id, intptr_t data)	//[orn]
{
	struct mercenary_data *md = (TBL_MER*)map_id2bl(id);
	if( data <= 0 || data >= MAX_SKILL )
		return 0;
	if( md ) md->blockskill[data] = 0;

	return 1;
}

int skill_blockmerc_start(struct mercenary_data *md, int skill_id, int tick)
{
	nullpo_retr (-1, md);

	if( (skill_id = skill_get_index(skill_id)) == 0 )
		return -1;
	if( tick < 1 )
	{
		md->blockskill[skill_id] = 0;
		return -1;
	}
	md->blockskill[skill_id] = 1;
	return add_timer(gettick() + tick, skill_blockmerc_end, md->bl.id, skill_id);
}

/**
 * Adds a new skill unit entry for this player to recast after map load
 **/
void skill_usave_add(struct map_session_data * sd, int skill_id, int skill_lv) {
	struct skill_usave * sus = NULL;

	if (idb_exists(skillusave_db, sd->status.char_id)) {
		idb_remove(skillusave_db, sd->status.char_id);
	}

	CREATE(sus, struct skill_usave, 1);
	idb_put(skillusave_db, sd->status.char_id, sus);

	sus->skill_id = skill_id;
	sus->skill_lv = skill_lv;
}

/**
 * Loads saved skill unit entries for this player after map load
 * @param sd: Player
 */
void skill_usave_trigger(struct map_session_data *sd)
{
	struct skill_usave *sus = NULL;
	struct skill_unit_group *group = NULL;

	if (!(sus = (struct skill_usave *)idb_get(skillusave_db, sd->status.char_id)))
		return;

	if ((group = skill_unitsetting(&sd->bl, sus->skill_id, sus->skill_lv, sd->bl.x, sd->bl.y, 0)))
		if (sus->skill_id == NC_NEUTRALBARRIER || sus->skill_id == NC_STEALTHFIELD)
			sc_start2(&sd->bl, (sus->skill_id == NC_NEUTRALBARRIER ? SC_NEUTRALBARRIER_MASTER : SC_STEALTHFIELD_MASTER), 100, sus->skill_lv, group->group_id, skill_get_time(sus->skill_id, sus->skill_lv));
	idb_remove(skillusave_db, sd->status.char_id);
}

/*
 *
 */
int skill_split_str (char *str, char **val, int num)
{
	int i;

	for( i = 0; i < num && str; i++ )
	{
		val[i] = str;
		str = strchr(str,',');
		if( str )
			*str++=0;
	}

	return i;
}

/**
 * Split the string with ':' as separator and put each value for a skill_lv
 * if no more value found put the latest to fill the array
 * @param str : string to split
 * @param val : array of MAX_SKILL_LEVEL to put value into
 * @return 0:error, x:number of value assign (should be MAX_SKILL_LEVEL)
 */
int skill_split_atoi (char *str, int *val)
{
	int i, j, step = 1;

	for (i=0; i<MAX_SKILL_LEVEL; i++) {
		if (!str) break;
		val[i] = atoi(str);
		str = strchr(str,':');
		if (str)
			*str++=0;
	}
	if(i==0) //No data found.
		return 0;
	if(i==1)
	{	//Single value, have the whole range have the same value.
		for (; i < MAX_SKILL_LEVEL; i++)
			val[i] = val[i-1];
		return i;
	}
	//Check for linear change with increasing steps until we reach half of the data acquired.
	for (step = 1; step <= i/2; step++)
	{
		int diff = val[i-1] - val[i-step-1];
		for(j = i-1; j >= step; j--)
			if ((val[j]-val[j-step]) != diff)
				break;

		if (j>=step) //No match, try next step.
			continue;

		for(; i < MAX_SKILL_LEVEL; i++)
		{	//Apply linear increase
			val[i] = val[i-step]+diff;
			if (val[i] < 1 && val[i-1] >=0) //Check if we have switched from + to -, cap the decrease to 0 in said cases.
			{ val[i] = 1; diff = 0; step = 1; }
		}
		return i;
	}
	//Okay.. we can't figure this one out, just fill out the stuff with the previous value.
	for (;i<MAX_SKILL_LEVEL; i++)
		val[i] = val[i-1];
	return i;
}

/*
 *
 */
void skill_init_unit_layout (void)
{
	int i,j,size,pos = 0;

	memset(skill_unit_layout,0,sizeof(skill_unit_layout));

	// standard square layouts go first
	for (i=0; i<=MAX_SQUARE_LAYOUT; i++) {
		size = i*2+1;
		skill_unit_layout[i].count = size*size;
		for (j=0; j<size*size; j++) {
			skill_unit_layout[i].dx[j] = (j%size-i);
			skill_unit_layout[i].dy[j] = (j/size-i);
		}
	}

	// afterwards add special ones
	pos = i;
	for (i=0;i<MAX_SKILL_DB;i++) {
		if (!skill_db[i].unit_id[0] || skill_db[i].unit_layout_type[0] != -1)
			continue;
			switch (i) {
				case MG_FIREWALL:
				case WZ_ICEWALL:
				case WL_EARTHSTRAIN:
				case RL_FIRE_RAIN:
					// these will be handled later
					break;
				case PR_SANCTUARY:
				case NPC_EVILLAND:
				{
					static const int dx[] = {
						-1, 0, 1,-2,-1, 0, 1, 2,-2,-1,
						 0, 1, 2,-2,-1, 0, 1, 2,-1, 0, 1};
					static const int dy[]={
						-2,-2,-2,-1,-1,-1,-1,-1, 0, 0,
						 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2};
					skill_unit_layout[pos].count = 21;
					memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
					memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
					break;
				}
				case PR_MAGNUS:
				{
					static const int dx[] = {
						-1, 0, 1,-1, 0, 1,-3,-2,-1, 0,
						 1, 2, 3,-3,-2,-1, 0, 1, 2, 3,
						-3,-2,-1, 0, 1, 2, 3,-1, 0, 1,-1, 0, 1};
					static const int dy[] = {
						-3,-3,-3,-2,-2,-2,-1,-1,-1,-1,
						-1,-1,-1, 0, 0, 0, 0, 0, 0, 0,
						 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3};
					skill_unit_layout[pos].count = 33;
					memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
					memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
					break;
				}
				case AS_VENOMDUST:
				{
					static const int dx[] = {-1, 0, 0, 0, 1};
					static const int dy[] = { 0,-1, 0, 1, 0};
					skill_unit_layout[pos].count = 5;
					memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
					memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
					break;
				}
				case CR_GRANDCROSS:
				case NPC_GRANDDARKNESS:
				{
					static const int dx[] = {
						 0, 0,-1, 0, 1,-2,-1, 0, 1, 2,
						-4,-3,-2,-1, 0, 1, 2, 3, 4,-2,
						-1, 0, 1, 2,-1, 0, 1, 0, 0};
					static const int dy[] = {
						-4,-3,-2,-2,-2,-1,-1,-1,-1,-1,
						 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
						 1, 1, 1, 1, 2, 2, 2, 3, 4};
					skill_unit_layout[pos].count = 29;
					memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
					memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
					break;
				}
				case PF_FOGWALL:
				{
					static const int dx[] = {
						-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2};
					static const int dy[] = {
						-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1};
					skill_unit_layout[pos].count = 15;
					memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
					memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
					break;
				}
				case PA_GOSPEL:
				{
					static const int dx[] = {
						-1, 0, 1,-1, 0, 1,-3,-2,-1, 0,
						 1, 2, 3,-3,-2,-1, 0, 1, 2, 3,
						-3,-2,-1, 0, 1, 2, 3,-1, 0, 1,
						-1, 0, 1};
					static const int dy[] = {
						-3,-3,-3,-2,-2,-2,-1,-1,-1,-1,
						-1,-1,-1, 0, 0, 0, 0, 0, 0, 0,
						 1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
						 3, 3, 3};
					skill_unit_layout[pos].count = 33;
					memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
					memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
					break;
				}
				case NJ_KAENSIN:
				{
					static const int dx[] = {-2,-1, 0, 1, 2,-2,-1, 0, 1, 2,-2,-1, 1, 2,-2,-1, 0, 1, 2,-2,-1, 0, 1, 2};
					static const int dy[] = { 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0, 0,-1,-1,-1,-1,-1,-2,-2,-2,-2,-2};
					skill_unit_layout[pos].count = 24;
					memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
					memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
					break;
				}
				case NJ_TATAMIGAESHI:
				{
					//Level 1 (count 4, cross of 3x3)
					static const int dx1[] = {-1, 1, 0, 0};
					static const int dy1[] = { 0, 0,-1, 1};
					//Level 2-3 (count 8, cross of 5x5)
					static const int dx2[] = {-2,-1, 1, 2, 0, 0, 0, 0};
					static const int dy2[] = { 0, 0, 0, 0,-2,-1, 1, 2};
					//Level 4-5 (count 12, cross of 7x7
					static const int dx3[] = {-3,-2,-1, 1, 2, 3, 0, 0, 0, 0, 0, 0};
					static const int dy3[] = { 0, 0, 0, 0, 0, 0,-3,-2,-1, 1, 2, 3};
					//lv1
					j = 0;
					skill_unit_layout[pos].count = 4;
					memcpy(skill_unit_layout[pos].dx,dx1,sizeof(dx1));
					memcpy(skill_unit_layout[pos].dy,dy1,sizeof(dy1));
					skill_db[i].unit_layout_type[j] = pos;
					//lv2/3
					j++;
					pos++;
					skill_unit_layout[pos].count = 8;
					memcpy(skill_unit_layout[pos].dx,dx2,sizeof(dx2));
					memcpy(skill_unit_layout[pos].dy,dy2,sizeof(dy2));
					skill_db[i].unit_layout_type[j] = pos;
					skill_db[i].unit_layout_type[++j] = pos;
					//lv4/5
					j++;
					pos++;
					skill_unit_layout[pos].count = 12;
					memcpy(skill_unit_layout[pos].dx,dx3,sizeof(dx3));
					memcpy(skill_unit_layout[pos].dy,dy3,sizeof(dy3));
					skill_db[i].unit_layout_type[j] = pos;
					skill_db[i].unit_layout_type[++j] = pos;
					//Fill in the rest using lv 5.
					for (;j<MAX_SKILL_LEVEL;j++)
						skill_db[i].unit_layout_type[j] = pos;
					//Skip, this way the check below will fail and continue to the next skill.
					pos++;
					break;
				}
				case GN_WALLOFTHORN:
				{
					static const int dx[] = {-1,-2,-2,-2,-2,-2,-1, 0, 1, 2, 2, 2, 2, 2, 1, 0};
					static const int dy[] = { 2, 2, 1, 0,-1,-2,-2,-2,-2,-2,-1, 0, 1, 2, 2, 2};
					skill_unit_layout[pos].count = 16;
					memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
					memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
					break;
				}
				case 1603:// EL_FIRE_MANTLE
				{	// Its pointless to make a 2nd entire skill range check code for 1 elemental skill.
					// Decided to check for the offset ID instead. Should be fine as long as the
					// elemental skills ID offsets don't get changed.
					//
					// Need confirm on if there's no unit placed where the player stands. [Rytech]
					static const int dx[] = {-1,-1,-1, 0, 0, 1, 1, 1};
					static const int dy[] = {-1, 0, 1,-1, 1,-1, 0, 1};
					skill_unit_layout[pos].count = 8;
					memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
					memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
					break;
				}
				default:
					ShowError("unknown unit layout at skill %d\n",i);
					break;
			}
		if (!skill_unit_layout[pos].count)
			continue;
		for (j=0;j<MAX_SKILL_LEVEL;j++)
			skill_db[i].unit_layout_type[j] = pos;
		pos++;
	}

	// firewall and icewall have 8 layouts (direction-dependent)
	firewall_unit_pos = pos;
	for (i=0;i<8;i++) {
		if (i&1) {
			skill_unit_layout[pos].count = 5;
			if (i&0x2) {
				int dx[] = {-7,-5, 0, 5, 7};
				int dy[] = { 0, 0, 0, 0, 0};
				memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
				memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
			} else {
				int dx[] = {-7,-5 ,0, 5, 7};
				int dy[] = { 0, 0, 0, 0, 0};
				memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
				memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
			}
		} else {
			skill_unit_layout[pos].count = 3;
			if (i%4==0) {
				int dx[] = {-7,-5, 0, 5, 7};
				int dy[] = { 0, 0, 0};
				memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
				memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
			} else {
				int dx[] = { 0, 0, 0};
				int dy[] = {-7,-5, 0, 5, 7};
				memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
				memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
			}
		}
		pos++;
	}
	icewall_unit_pos = pos;
	for (i=0;i<8;i++) {
		skill_unit_layout[pos].count = 5;
		if (i&1) {
			if (i&0x2) {
				int dx[] = {-2,-1, 0, 1, 2};
				int dy[] = { 2, 1, 0,-1,-2};
				memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
				memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
			} else {
				int dx[] = { 2, 1 ,0,-1,-2};
				int dy[] = { 2, 1, 0,-1,-2};
				memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
				memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
			}
		} else {
			if (i%4==0) {
				int dx[] = {-2,-1, 0, 1, 2};
				int dy[] = { 0, 0, 0, 0, 0};
				memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
				memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
			} else {
				int dx[] = { 0, 0, 0, 0, 0};
				int dy[] = {-2,-1, 0, 1, 2};
				memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
				memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
			}
		}
		pos++;
	}
	earthstrain_unit_pos = pos;
	for( i = 0; i < 8; i++ )
	{ // For each Direction
		skill_unit_layout[pos].count = 15;
		switch( i )
		{
			case 0: case 1: case 3: case 4: case 5: case 7:
				{
					int dx[] = {-7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7};
					int dy[] = { 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0};
					memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
					memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
				}
				break;
			case 2:
			case 6:
				{
					int dx[] = { 0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0};
					int dy[] = {-7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7};
					memcpy(skill_unit_layout[pos].dx,dx,sizeof(dx));
					memcpy(skill_unit_layout[pos].dy,dy,sizeof(dy));
				}
				break;
		}
		pos++;
	}
	fire_rain_unit_pos = pos;
	for (i = 0; i < 8; i++)
	{// Set for each direction.
		skill_unit_layout[pos].count = 3;
		switch (i)
		{
		case 0: case 1: case 3: case 4: case 5: case 7:// North / South
		{
			int dx[] = { -1, 0, 1 };
			int dy[] = { 0, 0, 0 };
			memcpy(skill_unit_layout[pos].dx, dx, sizeof(dx));
			memcpy(skill_unit_layout[pos].dy, dy, sizeof(dy));
		}
		break;
		case 2: case 6:// West / East
		{
			int dx[] = { 0, 0, 0 };
			int dy[] = { -1, 0, 1 };
			memcpy(skill_unit_layout[pos].dx, dx, sizeof(dx));
			memcpy(skill_unit_layout[pos].dy, dy, sizeof(dy));
		}
		break;
		}
		pos++;
	}

	if (pos >= MAX_SKILL_UNIT_LAYOUT)
		ShowError("skill_init_unit_layout: Max number of unit layouts reached. Set above %d.\n", pos);
}

void skill_init_nounit_layout(void)
{
	int i, pos = 0;

	memset(skill_nounit_layout, 0, sizeof(skill_nounit_layout));

	windcutter_nounit_pos = pos;
	for (i = 0; i < 8; i++)
	{
		if (i & 1)
		{
			skill_nounit_layout[pos].count = 13;
			if (i & 0x2)
			{
				int dx[] = { -2,-2,-1,-1,-1, 0, 0, 0, 1, 1, 1, 2, 2 };
				int dy[] = { 2, 1, 2, 1, 0, 1, 0,-1, 0,-1,-2,-1,-2 };
				memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
				memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
			}
			else
			{
				int dx[] = { 2, 2, 1, 1, 1, 0, 0, 0,-1,-1,-1,-2,-2 };
				int dy[] = { 2, 1, 2, 1, 0, 1, 0,-1, 0,-1,-2,-1,-2 };
				memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
				memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
			}
		}
		else {
			skill_nounit_layout[pos].count = 15;
			if (i % 4 == 0)
			{
				int dx[] = { -2,-2,-2,-1,-1,-1, 0, 0, 0, 1, 1, 1, 2, 2, 2 };
				int dy[] = { 1, 0,-1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1 };
				memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
				memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
			}
			else
			{
				int dx[] = { -1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1 };
				int dy[] = { 2, 1, 0,-1,-2, 2, 1, 0,-1,-2, 2, 1, 0,-1,-2 };
				memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
				memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
			}
		}
		pos++;
	}

	overbrand_nounit_pos = pos;
	for (i = 0; i < 8; i++)
	{
		if (i & 1)
		{
			skill_nounit_layout[pos].count = 33;
			if (i & 2)
			{
				if (i & 4)
				{	// 7
					int dx[] = { 5, 6, 7, 5, 6, 4, 5, 6, 4, 5, 3, 4, 5, 3, 4, 2, 3, 4, 2, 3, 1, 2, 3, 1, 2, 0, 1, 2, 0, 1,-1, 0, 1 };
					int dy[] = { 7, 6, 5, 6, 5, 6, 5, 4, 5, 4, 5, 4, 3, 4, 3, 4, 3, 2, 3, 2, 3, 2, 1, 2, 1, 2, 1, 0, 1, 0, 1, 0,-1 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
				else
				{	// 3
					int dx[] = { -5,-6,-7,-5,-6,-4,-5,-6,-4,-5,-3,-4,-5,-3,-4,-2,-3,-4,-2,-3,-1,-2,-3,-1,-2, 0,-1,-2, 0,-1, 1, 0,-1 };
					int dy[] = { -7,-6,-5,-6,-5,-6,-5,-4,-5,-4,-5,-4,-3,-4,-3,-4,-3,-2,-3,-2,-3,-2,-1,-2,-1,-2,-1, 0,-1, 0,-1, 0, 1 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
			}
			else
			{
				if (i & 4)
				{	// 5
					int dx[] = { 7, 6, 5, 6, 5, 6, 5, 4, 5, 4, 5, 4, 3, 4, 3, 4, 3, 2, 3, 2, 3, 2, 1, 2, 1, 2, 1, 0, 1, 0, 1, 0,-1 };
					int dy[] = { -5,-6,-7,-5,-6,-4,-5,-6,-4,-5,-3,-4,-5,-3,-4,-2,-3,-4,-2,-3,-1,-2,-3,-1,-2, 0,-1,-2, 0,-1, 1, 0,-1 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
				else
				{	// 1
					int dx[] = { -7,-6,-5,-6,-5,-6,-5,-4,-5,-4,-5,-4,-3,-4,-3,-4,-3,-2,-3,-2,-3,-2,-1,-2,-1,-2,-1, 0,-1, 0,-1, 0, 1 };
					int dy[] = { 5, 6, 7, 5, 6, 4, 5, 6, 4, 5, 3, 4, 5, 3, 4, 2, 3, 4, 2, 3, 1, 2, 3, 1, 2, 0, 1, 2, 0, 1,-1, 0, 1 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
			}
		}
		else
		{
			skill_nounit_layout[pos].count = 21;
			if (i & 2)
			{
				if (i & 4)
				{	// 6
					int dx[] = { 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6 };
					int dy[] = { 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1,-1 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
				else
				{	// 2
					int dx[] = { -6,-5,-4,-3,-2,-1, 0,-6,-5,-4,-3,-2,-1, 0,-6,-5,-4,-3,-2,-1, 0 };
					int dy[] = { 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,-1,-1,-1,-1,-1,-1,-1 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
			}
			else
			{
				if (i & 4)
				{	// 4
					int dx[] = { -1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1 };
					int dy[] = { 0, 0, 0,-1,-1,-1,-2,-2,-2,-3,-3,-3,-4,-4,-4,-5,-5,-5,-6,-6,-6 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
				else
				{	// 0
					int dx[] = { -1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1,-1, 0, 1 };
					int dy[] = { 6, 6, 6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 1, 0, 0, 0 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
			}
		}
		pos++;
	}

	overbrand_brandish_nounit_pos = pos;
	for (i = 0; i < 8; i++)
	{
		if (i & 1)
		{
			skill_nounit_layout[pos].count = 74;
			if (i & 2)
			{
				if (i & 4)
				{	// 7
					int dx[] = { -2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 8,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7,
						-3,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7,-3,-2,-1,-0, 1, 2, 3, 4, 5, 6,
						-4,-3,-2,-1, 0, 1, 2, 3, 4, 5, 6,-4,-3,-2,-1,-0, 1, 2, 3, 4, 5,
						-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5 };
					int dy[] = { 8, 7, 6, 5, 4, 3, 2, 1, 0,-1,-2, 7, 6, 5, 4, 3, 2, 1, 0,-1,-2,
						7, 6, 5, 4, 3, 2, 1, 0,-1,-2,-3, 6, 5, 4, 3, 2, 1, 0,-1,-2,-3,
						6, 5, 4, 3, 2, 1, 0,-1,-2,-3,-4, 5, 4, 3, 2, 1, 0,-1,-2,-3,-4,
						5, 4, 3, 2, 1, 0,-1,-2,-3,-4,-5 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
				else
				{	// 3
					int dx[] = { 2, 1, 0,-1,-2,-3,-4,-5,-6,-7,-8, 2, 1, 0,-1,-2,-3,-4,-5,-6,-7,
						3, 2, 1, 0,-1,-2,-3,-4,-5,-6,-7, 3, 2, 1, 0,-1,-2,-3,-4,-5,-6,
						4, 3, 2, 1, 0,-1,-2,-3,-4,-5,-6, 4, 3, 2, 1, 0,-1,-2,-3,-4,-5,
						5, 4, 3, 2, 1, 0,-1,-2,-3,-4,-5 };
					int dy[] = { -8,-7,-6,-5,-4,-3,-2,-1, 0, 1, 2,-7,-6,-5,-4,-3,-2,-1, 0, 1, 2,
						-7,-6,-5,-4,-3,-2,-1, 0, 1, 2, 3,-6,-5,-4,-3,-2,-1, 0, 1, 2, 3,
						-6,-5,-4,-3,-2,-1, 0, 1, 2, 3, 4,-5,-4,-3,-2,-1, 0, 1, 2, 3, 4,
						-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
			}
			else
			{
				if (i & 4)
				{	// 5
					int dx[] = { 8, 7, 6, 5, 4, 3, 2, 1, 0,-1,-2, 7, 6, 5, 4, 3, 2, 1, 0,-1,-2,
						7, 6, 5, 4, 3, 2, 1, 0,-1,-2,-3, 6, 5, 4, 3, 2, 1, 0,-1,-2,-3,
						6, 5, 4, 3, 2, 1, 0,-1,-2,-3,-4, 5, 4, 3, 2, 1, 0,-1,-2,-3,-4,
						5, 4, 3, 2, 1, 0,-1,-2,-3,-4,-5 };
					int dy[] = { 2, 1, 0,-1,-2,-3,-4,-5,-6,-7,-8, 2, 1, 0,-1,-2,-3,-4,-5,-6,-7,
						3, 2, 1, 0,-1,-2,-3,-4,-5,-6,-7, 3, 2, 1, 0,-1,-2,-3,-4,-5,-6,
						4, 3, 2, 1, 0,-1,-2,-3,-4,-5,-6, 4, 3, 2, 1, 0,-1,-2,-3,-4,-5,
						5, 4, 3, 2, 1, 0,-1,-2,-3,-4,-5 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
				else
				{	// 1
					int dx[] = { -8,-7,-6,-5,-4,-3,-2,-1, 0, 1, 2,-7,-6,-5,-4,-3,-2,-1, 0, 1, 2,
						-7,-6,-5,-4,-3,-2,-1, 0, 1, 2, 3,-6,-5,-4,-3,-2,-1, 0, 1, 2, 3,
						-6,-5,-4,-3,-2,-1, 0, 1, 2, 3, 4,-5,-4,-3,-2,-1, 0, 1, 2, 3, 4,
						-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5 };
					int dy[] = { -2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 8,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7,
						-3,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7,-3,-2,-1, 0, 1, 2, 3, 4, 5, 6,
						-4,-3,-2,-1, 0, 1, 2, 3, 4, 5, 6,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5,
						-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
			}
		}
		else
		{
			skill_nounit_layout[pos].count = 44;
			if (i & 2)
			{
				if (i & 4)
				{	// 6
					int dx[] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3 };
					int dy[] = { 5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0,-1,-1,-1,-1,-2,-2,-2,-2,-3,-3,-3,-3,-4,-4,-4,-4,-5,-5,-5,-5 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
				else
				{	// 2
					int dx[] = { -3,-2,-1, 0,-3,-2,-1, 0,-3,-2,-1, 0,-3,-2,-1, 0,-3,-2,-1, 0,-3,-2,-1, 0,-3,-2,-1, 0,-3,-2,-1, 0,-3,-2,-1, 0,-3,-2,-1, 0,-3,-2,-1, 0 };
					int dy[] = { 5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0,-1,-1,-1,-1,-2,-2,-2,-2,-3,-3,-3,-3,-4,-4,-4,-4,-5,-5,-5,-5 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
			}
			else
			{
				if (i & 4)
				{	// 4
					int dx[] = { 5, 4, 3, 2, 1, 0,-1,-2,-3,-4,-5, 5, 4, 3, 2, 1, 0,-1,-2,-3,-4,-5, 5, 4, 3, 2, 1, 0,-1,-2,-3,-4,-5, 5, 4, 3, 2, 1, 0,-1,-2,-3,-4,-5 };
					int dy[] = { -3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
				else
				{	// 0
					int dx[] = { -5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5,-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5,-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5,-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5 };
					int dy[] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
					memcpy(skill_nounit_layout[pos].dx, dx, sizeof(dx));
					memcpy(skill_nounit_layout[pos].dy, dy, sizeof(dy));
				}
			}
		}
		pos++;
	}
}

// Stasis skill usage check. [LimitLine]
int skill_stasis_check(struct block_list *bl, int skill_id)
{
	if( !bl )
		return 0;// Can do it

	//Song, Dance, Ensemble, Chorus, and all magic skills will not work in Stasis status. [Rytech]
	if( skill_get_inf2(skill_id) == INF2_SONG_DANCE || skill_get_inf2(skill_id) == INF2_ENSEMBLE_SKILL || 
		skill_get_inf2(skill_id) == INF2_CHORUS_SKILL || skill_get_type(skill_id) == BF_MAGIC )
		return 1;

	return 0;//Any skills thats not the above will work.
}

/**
 * Check before do `unit_movepos` call
 * @param check_flag Flags: 1:Check for BG maps, 2:Check for GVG maps on WOE times, 4:Check for GVG maps regardless Agit flags
 * @return True:If unit can be moved, False:If check on flags are met or unit cannot be moved.
 **/
static bool skill_check_unit_movepos(uint8 check_flag, struct block_list *bl, short dst_x, short dst_y, int easy, bool checkpath) {
	nullpo_retr(false, bl);
	if (check_flag & 1 && map[bl->m].flag.battleground)
		return false;
	if (check_flag & 2 && map_flag_gvg(bl->m))
		return false;
	if (check_flag & 4 && map_flag_gvg2(bl->m))
		return false;
	return unit_movepos(bl, dst_x, dst_y, easy, checkpath);
}

/*==========================================
 * DB reading.
 * skill_db.txt
 * skill_require_db.txt
 * skill_cast_db.txt
 * skill_castnodex_db.txt
 * skill_nocast_db.txt
 * skill_unit_db.txt
 * produce_db.txt
 * create_arrow_db.txt
 * abra_db.txt
 * spellbook_db.txt
 *------------------------------------------*/

static bool skill_parse_row_skilldb(char* split[], int columns, int current)
{// id,range,hit,inf,element,nk,splash,max,list_num,castcancel,cast_defence_rate,inf2,maxcount,skill_type,blow_count,name,description
	int id = atoi(split[0]);
	int i;
	if( (id >= HM_SKILLRANGEMIN && id <= HM_SKILLRANGEMAX)
		|| (id >= MC_SKILLRANGEMIN && id <= MC_SKILLRANGEMAX)
		|| (id >= EL_SKILLRANGEMIN && id <= EL_SKILLRANGEMAX)
		|| (id >= GD_SKILLRANGEMIN && id <= GD_SKILLRANGEMAX) )
	{
		ShowWarning("skill_parse_row_skilldb: Skill id %d is forbidden (interferes with homunculus/mercenary/elemental/guild skill mapping)!\n", id);
		return false;
	}

	i = skill_get_index(id);
	if( !i ) // invalid skill id
		return false;

	skill_split_atoi(split[1],skill_db[i].range);
	skill_db[i].hit = atoi(split[2]);
	skill_db[i].inf = atoi(split[3]);
	skill_split_atoi(split[4],skill_db[i].element);
	skill_db[i].nk = (int)strtol(split[5], NULL, 0);
	skill_split_atoi(split[6],skill_db[i].splash);
	skill_db[i].max = atoi(split[7]);
	skill_split_atoi(split[8],skill_db[i].num);

	if( strcmpi(split[9],"yes") == 0 )
		skill_db[i].castcancel = 1;
	else
		skill_db[i].castcancel = 0;
	skill_db[i].cast_def_rate = atoi(split[10]);
	skill_db[i].inf2 = (int)strtol(split[11], NULL, 0);
	skill_split_atoi(split[12],skill_db[i].maxcount);
	if( strcmpi(split[13],"weapon") == 0 )
		skill_db[i].skill_type = BF_WEAPON;
	else if( strcmpi(split[13],"magic") == 0 )
		skill_db[i].skill_type = BF_MAGIC;
	else if( strcmpi(split[13],"misc") == 0 )
		skill_db[i].skill_type = BF_MISC;
	else
		skill_db[i].skill_type = 0;
	skill_split_atoi(split[14],skill_db[i].blewcount);
	safestrncpy(skill_db[i].name, trim(split[15]), sizeof(skill_db[i].name));
	safestrncpy(skill_db[i].desc, trim(split[16]), sizeof(skill_db[i].desc));
	strdb_iput(skilldb_name2id, skill_db[i].name, id);

	return true;
}

/** Split string to int or constanta value (const.txt)
* @param *str: String input
* @param *val: Temporary storage
* @param *delim: Delimiter (for multiple value support)
* @param useConst: 'true' uses const.txt as reference, 'false' uses atoi()
* @param min: Min value of each const. Example: SC has min value SC_NONE (-1), so the value that less or equal won't be counted
* @return count: Number of success
*/
uint8 skill_split2(char *str, int *val, const char *delim, bool useConst, short min) {
	uint8 i = 0;
	char *p = strtok(str, delim);

	while (p != NULL) {
		int n = -1;
		if (useConst)
			script_get_constant(trim(p), &n);
		else
			n = atoi(p);
		if (n > min) {
			val[i] = n;
			i++;
		}
		p = strtok(NULL, delim);
	}
	return i;
}

/// Clear status data from skill requirement
static void skill_destroy_requirement(void) {
	uint16 i;
	for (i = 0; i < MAX_SKILL; i++) {
		if (skill_db[i].require.eqItem_count)
			aFree(skill_db[i].require.eqItem);
		skill_db[i].require.eqItem_count = 0;
	}
}

static bool skill_parse_row_requiredb(char* split[], int columns, int current)
{// SkillID,HPCost,MaxHPTrigger,SPCost,HPRateCost,SPRateCost,ZenyCost,RequiredWeapons,RequiredAmmoTypes,RequiredAmmoAmount,RequiredState,SpiritSphereCost,RequiredItemID1,RequiredItemAmount1,RequiredItemID2,RequiredItemAmount2,RequiredItemID3,RequiredItemAmount3,RequiredItemID4,RequiredItemAmount4,RequiredItemID5,RequiredItemAmount5,RequiredItemID6,RequiredItemAmount6,RequiredItemID7,RequiredItemAmount7,RequiredItemID8,RequiredItemAmount8,RequiredItemID9,RequiredItemAmount9,RequiredItemID10,RequiredItemAmount10,RequiredEquip
	char* p;
	int j;

	int i = atoi(split[0]);
	i = skill_get_index(i);
	if( !i ) // invalid skill id
		return false;

	skill_split_atoi(split[1],skill_db[i].require.hp);
	skill_split_atoi(split[2],skill_db[i].require.mhp);
	skill_split_atoi(split[3],skill_db[i].require.sp);
	skill_split_atoi(split[4],skill_db[i].require.hp_rate);
	skill_split_atoi(split[5],skill_db[i].require.sp_rate);
	skill_split_atoi(split[6],skill_db[i].require.zeny);

	//FIXME: document this
	p = split[7];
	while (p) {
		int l = atoi(p);
		if( l == 99 ) // Any weapon
		{
			skill_db[i].require.weapon = 0;
			break;
		}
		else
			skill_db[i].require.weapon |= 1<<l;
		p = strchr(p,':');
		if(!p)
			break;
		p++;
	}

	//FIXME: document this
	p = split[8];
	while (p) {
		int l = atoi(p);
		if( l == 99 ) // Any ammo type
		{
			skill_db[i].require.ammo = 0xFFFFFFFF;
			break;
		}
		else if( l ) // 0 stands for no requirement
			skill_db[i].require.ammo |= 1<<l;
		p = strchr(p,':');
		if( !p )
			break;
		p++;
	}
	skill_split_atoi(split[9],skill_db[i].require.ammo_qty);

	if(      strcmpi(split[10],"hiding")==0 ) skill_db[i].require.state = ST_HIDING;
	else if( strcmpi(split[10],"cloaking")==0 ) skill_db[i].require.state = ST_CLOAKING;
	else if( strcmpi(split[10],"hidden")==0 ) skill_db[i].require.state = ST_HIDDEN;
	else if( strcmpi(split[10],"riding")==0 ) skill_db[i].require.state = ST_RIDING;
	else if( strcmpi(split[10],"falcon")==0 ) skill_db[i].require.state = ST_FALCON;
	else if( strcmpi(split[10],"cart")==0 ) skill_db[i].require.state = ST_CART;
	else if( strcmpi(split[10],"shield")==0 ) skill_db[i].require.state = ST_SHIELD;
	else if( strcmpi(split[10],"sight")==0 ) skill_db[i].require.state = ST_SIGHT;
	else if( strcmpi(split[10],"explosionspirits")==0 ) skill_db[i].require.state = ST_EXPLOSIONSPIRITS;
	else if( strcmpi(split[10],"cartboost")==0 ) skill_db[i].require.state = ST_CARTBOOST;
	else if( strcmpi(split[10],"recover_weight_rate")==0 ) skill_db[i].require.state = ST_RECOV_WEIGHT_RATE;
	else if( strcmpi(split[10],"move_enable")==0 ) skill_db[i].require.state = ST_MOVE_ENABLE;
	else if( strcmpi(split[10],"water")==0 ) skill_db[i].require.state = ST_WATER;
	else if( strcmpi(split[10],"dragon")==0 ) skill_db[i].require.state = ST_DRAGON;
	else if( strcmpi(split[10],"warg")==0 ) skill_db[i].require.state = ST_WUG;
	else if( strcmpi(split[10],"ridingwarg")==0 ) skill_db[i].require.state = ST_WUGRIDER;
	else if( strcmpi(split[10],"mado")==0 ) skill_db[i].require.state = ST_MADOGEAR;
	else if( strcmpi(split[10],"elemental")==0 ) skill_db[i].require.state = ST_ELEMENTAL;
	else if (strcmpi(split[10], "fighter") == 0) skill_db[i].require.state = ST_FIGHTER;
	else if (strcmpi(split[10], "grappler") == 0) skill_db[i].require.state = ST_GRAPPLER;
	else if (strcmpi(split[10], "sunstance") == 0) skill_db[i].require.state = ST_SUNSTANCE;
	else if (strcmpi(split[10], "lunarstance") == 0) skill_db[i].require.state = ST_MOONSTANCE;
	else if (strcmpi(split[10], "starstance") == 0) skill_db[i].require.state = ST_STARSTANCE;
	else if (strcmpi(split[10], "universestance") == 0) skill_db[i].require.state = ST_UNIVERSESTANCE;
	else skill_db[i].require.state = ST_NONE;

	skill_split_atoi(split[11],skill_db[i].require.spiritball);
	for( j = 0; j < MAX_SKILL_ITEM_REQUIRE; j++ ) {
		skill_db[i].require.itemid[j] = atoi(split[12+ 2*j]);
		skill_db[i].require.amount[j] = atoi(split[13+ 2*j]);
	}

	//Equipped Item requirements.
	//NOTE: We don't check the item is exist or not here
	trim(split[32]);
	if (split[32][0] != '\0') {
		int require[MAX_SKILL_EQUIP_REQUIRE];
		if ((skill_db[i].require.eqItem_count = skill_split2(split[32], require, ":", false, 501))) {
			skill_db[i].require.eqItem = aMalloc(skill_db[i].require.eqItem_count * sizeof(short));
			for (j = 0; j < skill_db[i].require.eqItem_count; j++)
				skill_db[i].require.eqItem[j] = require[j];
		}
	}
	return true;
}

static bool skill_parse_row_castdb(char* split[], int columns, int current)
{// SkillID,CastingTime,AfterCastActDelay,AfterCastWalkDelay,Duration1,Duration2
	int i = atoi(split[0]);
	i = skill_get_index(i);
	if( !i ) // invalid skill id
		return false;

	skill_split_atoi(split[1],skill_db[i].cast);
	skill_split_atoi(split[2],skill_db[i].delay);
	skill_split_atoi(split[3],skill_db[i].walkdelay);
	skill_split_atoi(split[4],skill_db[i].upkeep_time);
	skill_split_atoi(split[5],skill_db[i].upkeep_time2);

	return true;
}

static bool skill_parse_row_copyabledb(char* split[], int column, int current) {
	int16 id;
	uint8 option;

	trim(split[0]);
	if (ISDIGIT(split[0][0]))
		id = atoi(split[0]);
	else
		id = skill_name2id(split[0]);

	if ((id = skill_get_index(id)) < 0) {
		ShowError("skill_parse_row_copyabledb: Invalid skill %s\n", split[0]);
		return false;
	}
	if ((option = atoi(split[1])) > 3) {
		ShowError("skill_parse_row_copyabledb: Invalid option '%s'\n", split[1]);
		return false;
	}

	skill_db[id].copyable.option = option;
	skill_db[id].copyable.joballowed = 63;
	if (atoi(split[2]))
		skill_db[id].copyable.joballowed = cap_value(atoi(split[2]), 1, 63);
	skill_db[id].copyable.req_opt = cap_value(atoi(split[3]), 0, 4095);

	return true;
}

static bool skill_parse_row_renewalcastdb(char* split[], int columns, int current)
{// SkillID,VariableCastTime,FixedCastTime,AfterCastActDelay,Cooldown,AfterCastWalkDelay,Duration1,Duration2
	int i = atoi(split[0]);
	i = skill_get_index(i);
	if( !i ) // invalid skill id
		return false;

	skill_split_atoi(split[1],skill_db[i].cast);
	skill_split_atoi(split[2],skill_db[i].fixed_cast);
	skill_split_atoi(split[3],skill_db[i].delay);
	skill_split_atoi(split[4],skill_db[i].cooldown);
	skill_split_atoi(split[5],skill_db[i].walkdelay);
	skill_split_atoi(split[6],skill_db[i].upkeep_time);
	skill_split_atoi(split[7],skill_db[i].upkeep_time2);
	
	return true;
}

static bool skill_parse_row_castnodexdb(char* split[], int columns, int current)
{// Skill id,Cast,Delay (optional)
	int i = atoi(split[0]);
	i = skill_get_index(i);
	if( !i ) // invalid skill id
		return false;

	skill_split_atoi(split[1],skill_db[i].castnodex);
	if( split[2] ) // optional column
		skill_split_atoi(split[2],skill_db[i].delaynodex);

	return true;
}

static bool skill_parse_row_nocastdb(char* split[], int columns, int current)
{// SkillID,Flag
	int i = atoi(split[0]);
	i = skill_get_index(i);
	if( !i ) // invalid skill id
		return false;

	skill_db[i].nocast |= atoi(split[1]);

	return true;
}

static bool skill_parse_row_unitdb(char* split[], int columns, int current)
{// ID,unit ID,unit ID 2,layout,range,interval,target,flag
	int i = atoi(split[0]);
	i = skill_get_index(i);
	if( !i ) // invalid skill id
		return false;

	skill_db[i].unit_id[0] = strtol(split[1],NULL,16);
	skill_db[i].unit_id[1] = strtol(split[2],NULL,16);
	skill_split_atoi(split[3],skill_db[i].unit_layout_type);
	skill_split_atoi(split[4],skill_db[i].unit_range);
	skill_db[i].unit_interval = atoi(split[5]);

	if( strcmpi(split[6],"noenemy")==0 ) skill_db[i].unit_target = BCT_NOENEMY;
	else if( strcmpi(split[6],"friend")==0 ) skill_db[i].unit_target = BCT_NOENEMY;
	else if( strcmpi(split[6],"party")==0 ) skill_db[i].unit_target = BCT_PARTY;
	else if( strcmpi(split[6],"ally")==0 ) skill_db[i].unit_target = BCT_PARTY|BCT_GUILD;
	else if( strcmpi(split[6],"guild") == 0) skill_db[i].unit_target = BCT_GUILD;
	else if( strcmpi(split[6],"all")==0 ) skill_db[i].unit_target = BCT_ALL;
	else if( strcmpi(split[6],"enemy")==0 ) skill_db[i].unit_target = BCT_ENEMY;
	else if( strcmpi(split[6],"self")==0 ) skill_db[i].unit_target = BCT_SELF;
	else if( strcmpi(split[6],"sameguild")==0 ) skill_db[i].unit_target = BCT_GUILD | BCT_SAMEGUILD;
	else if( strcmpi(split[6],"noone")==0 ) skill_db[i].unit_target = BCT_NOONE;
	else skill_db[i].unit_target = strtol(split[6],NULL,16);

	skill_db[i].unit_flag = strtol(split[7],NULL,16);

	if (skill_db[i].unit_flag&UF_DEFNOTENEMY && battle_config.defnotenemy)
		skill_db[i].unit_target = BCT_NOENEMY;

	//By default, target just characters.
	skill_db[i].unit_target |= BL_CHAR;
	if (skill_db[i].unit_flag&UF_NOPC)
		skill_db[i].unit_target &= ~BL_PC;
	if (skill_db[i].unit_flag&UF_NOMOB)
		skill_db[i].unit_target &= ~BL_MOB;
	if (skill_db[i].unit_flag&UF_SKILL)
		skill_db[i].unit_target |= BL_SKILL;

	return true;
}

static bool skill_parse_row_producedb(char* split[], int columns, int current)
{// ProduceItemID,ItemLV,RequireSkill,RequireSkillLv,MaterialID1,MaterialAmount1,......
	int x,y;

	int i = strtoul(split[0], NULL, 10);
	if( !i )
		return false;

	skill_produce_db[current].nameid = i;
	skill_produce_db[current].itemlv = atoi(split[1]);
	skill_produce_db[current].req_skill = atoi(split[2]);
	skill_produce_db[current].req_skill_lv = atoi(split[3]);

	for( x = 4, y = 0; x+1 < columns && split[x] && split[x+1] && y < MAX_PRODUCE_RESOURCE; x += 2, y++ )
	{
		skill_produce_db[current].mat_id[y] = atoi(split[x]);
		skill_produce_db[current].mat_amount[y] = atoi(split[x+1]);
	}

	return true;
}

static bool skill_parse_row_createarrowdb(char* split[], int columns, int current)
{// SourceID,MakeID1,MakeAmount1,...,MakeID5,MakeAmount5
	int x,y;

	int i = strtoul(split[0], NULL, 10);
	if( !i )
		return false;

	skill_arrow_db[current].nameid = i;

	for( x = 1, y = 0; x+1 < columns && split[x] && split[x+1] && y < MAX_ARROW_RESOURCE; x += 2, y++ )
	{
		skill_arrow_db[current].cre_id[y] = atoi(split[x]);
		skill_arrow_db[current].cre_amount[y] = atoi(split[x+1]);
	}

	return true;
}

static bool skill_parse_row_abradb(char* split[], int columns, int current)
{// SkillID,DummyName,RequiredHocusPocusLevel,Rate
	int i = atoi(split[0]);
	if( !skill_get_index(i) || !skill_get_max(i) )
	{
		ShowError("abra_db: Invalid skill ID %d\n", i);
		return false;
	}
	if ( !skill_get_inf(i) )
	{
		ShowError("abra_db: Passive skills cannot be casted (%d/%s)\n", i, skill_get_name(i));
		return false;
	}

	skill_abra_db[current].skill_id = i;
	skill_abra_db[current].req_lv = atoi(split[2]);
	skill_abra_db[current].per = atoi(split[3]);

	return true;
}

static bool skill_parse_row_spellbookdb(char* split[], int columns, int current)
{// SkillID,PreservePoints]
	int skill_id = atoi(split[0]),
		points = atoi(split[1]),
		nameid = strtoul(split[2], NULL, 10);

	if( !skill_get_index(skill_id) || !skill_get_max(skill_id) )
		ShowError("spellbook_db: Invalid skill ID %d\n", skill_id);
	if ( !skill_get_inf(skill_id) )
		ShowError("spellbook_db: Passive skills cannot be memorized (%d/%s)\n", skill_id, skill_get_name(skill_id));
	if( points < 1 )
		ShowError("spellbook_db: PreservePoints have to be 1 or above! (%d/%s)\n", skill_id, skill_get_name(skill_id));
	else
	{
		skill_spellbook_db[current].skill_id = skill_id;
		skill_spellbook_db[current].points = points;
		skill_spellbook_db[current].nameid = nameid;

		return true;
	}

	return false;
}

static bool skill_parse_row_improvisedb(char* split[], int columns, int current)
{// SkillID
	int i = atoi(split[0]);
	int j = atoi(split[1]);

	if( !skill_get_index(i) || !skill_get_max(i) )
	{
		ShowError("improvise_db: Invalid skill ID %d\n", i);
		return false;
	}
	if ( !skill_get_inf(i) )
	{
		ShowError("improvise_db: Passive skills cannot be casted (%d/%s)\n", i, skill_get_name(i));
		return false;
	}
	if( j < 1 )
	{
		ShowError("improvise_db: Chances have to be 1 or above! (%d/%s)\n", i, skill_get_name(i));
		return false;
	}

	skill_improvise_db[current].skill_id = i;
	skill_improvise_db[current].per = j; // Still need confirm it.

	return true;
}

static bool skill_parse_row_magicmushroomdb(char* split[], int column, int current)
{
	int i = atoi(split[0]);

	if( !skill_get_index(i) || !skill_get_max(i) )
	{
		ShowError("magicmushroom_db: Invalid skill ID %d\n", i);
		return false;
	}
	if ( !skill_get_inf(i) )
	{
		ShowError("magicmushroom_db: Passive skills cannot be casted (%d/%s)\n", i, skill_get_name(i));
		return false;
	}

	skill_magicmushroom_db[current].skill_id = i;

	return true;
}

static bool skill_parse_row_changematerialdb(char* split[], int columns, int current)
{// SkillID
	int i = strtoul(split[0], NULL, 10);;
	short j = atoi(split[1]);
	int x, y;

	for (x = 0; x < MAX_SKILL_PRODUCE_DB; x++) {
		if (skill_produce_db[x].nameid == i)
			if (skill_produce_db[x].req_skill == GN_CHANGEMATERIAL)
				break;
	}

	if (x >= MAX_SKILL_PRODUCE_DB) {
		ShowError("changematerial_db: Not supported item ID(%d) for Change Material. \n", i);
		return false;
	}

	if (current >= MAX_SKILL_PRODUCE_DB) {
		ShowError("skill_changematerial_db: Maximum amount of entries reached (%d), increase MAX_SKILL_PRODUCE_DB\n", MAX_SKILL_PRODUCE_DB);
	}

	skill_changematerial_db[current].itemid = i;
	skill_changematerial_db[current].rate = j;

	for (x = 2, y = 0; x + 1 < columns && split[x] && split[x + 1] && y < 5; x += 2, y++)
	{
		skill_changematerial_db[current].qty[y] = atoi(split[x]);
		skill_changematerial_db[current].qty_rate[y] = atoi(split[x + 1]);
	}

	return true;
}

static void skill_readdb(void)
{
	// init skill db structures
	db_clear(skilldb_name2id);
	memset(skill_db,0,sizeof(skill_db));
	memset(skill_produce_db,0,sizeof(skill_produce_db));
	memset(skill_arrow_db,0,sizeof(skill_arrow_db));
	memset(skill_abra_db,0,sizeof(skill_abra_db));
	memset(skill_spellbook_db, 0, sizeof(skill_spellbook_db));
	memset(skill_improvise_db,0,sizeof(skill_improvise_db));
	memset(skill_magicmushroom_db,0,sizeof(skill_magicmushroom_db));
	memset(skill_changematerial_db, 0, sizeof(skill_changematerial_db));

	// load skill databases
	safestrncpy(skill_db[0].name, "UNKNOWN_SKILL", sizeof(skill_db[0].name));
	safestrncpy(skill_db[0].desc, "Unknown Skill", sizeof(skill_db[0].desc));
	sv_readdb(db_path, "skill_db.txt"          , ',',  17, 17, MAX_SKILL_DB, skill_parse_row_skilldb);
	sv_readdb(db_path, "skill_require_db.txt"  , ',',  33, 33, MAX_SKILL_DB, skill_parse_row_requiredb);
	sv_readdb(db_path, "skill_cast_db.txt"     , ',',   6,  6, MAX_SKILL_DB, skill_parse_row_castdb);
	sv_readdb(db_path, "skill_renewal_cast_db.txt", ',',   8,  8, MAX_SKILL_DB, skill_parse_row_renewalcastdb);
	sv_readdb(db_path, "skill_castnodex_db.txt", ',',   2,  3, MAX_SKILL_DB, skill_parse_row_castnodexdb);
	sv_readdb(db_path, "skill_nocast_db.txt"   , ',',   2,  2, MAX_SKILL_DB, skill_parse_row_nocastdb);
	sv_readdb(db_path, "skill_unit_db.txt"     , ',',   8,  8, MAX_SKILL_DB, skill_parse_row_unitdb);
	skill_init_unit_layout();
	skill_init_nounit_layout();
	sv_readdb(db_path, "produce_db.txt"        , ',',   4,  4+2*MAX_PRODUCE_RESOURCE, MAX_SKILL_PRODUCE_DB, skill_parse_row_producedb);
	episode_sv_readdb(db_path, "create_arrow_db", ',', 1+2,  1+2*MAX_ARROW_RESOURCE, MAX_SKILL_ARROW_DB, skill_parse_row_createarrowdb);
	sv_readdb(db_path, "abra_db.txt"           , ',',   4,  4, MAX_SKILL_ABRA_DB, skill_parse_row_abradb);
	sv_readdb(db_path, "spellbook_db.txt"      , ',',   3,  3, MAX_SKILL_SPELLBOOK_DB, skill_parse_row_spellbookdb);
	sv_readdb(db_path, "improvise_db.txt"      , ',',   2,  2, MAX_SKILL_IMPROVISE_DB, skill_parse_row_improvisedb);
	sv_readdb(db_path, "magicmushroom_db.txt"  , ',',   1,  1, MAX_SKILL_MAGICMUSHROOM_DB, skill_parse_row_magicmushroomdb);
	sv_readdb(db_path, "skill_copyable_db.txt", ',',    2,  4, MAX_SKILL_DB, skill_parse_row_copyabledb);
	sv_readdb(db_path, "skill_changematerial_db.txt", ',', 4, 4 + 2 * 5, MAX_SKILL_PRODUCE_DB, skill_parse_row_changematerialdb);
}

void skill_reload (void)
{
	skill_destroy_requirement();
	skill_readdb();
}

/*==========================================
 *
 *------------------------------------------*/
void do_init_skill (void)
{
	skilldb_name2id = strdb_alloc(DB_OPT_DUP_KEY | DB_OPT_RELEASE_DATA, 0);
	skill_readdb();

	group_db = idb_alloc(DB_OPT_BASE);
	skillunit_db = idb_alloc(DB_OPT_BASE);
	skillusave_db = idb_alloc(DB_OPT_RELEASE_DATA);
	bowling_db = idb_alloc(DB_OPT_BASE);
	skill_unit_ers = ers_new(sizeof(struct skill_unit_group), "skill.c::skill_unit_ers", ERS_OPT_NONE);
	skill_timer_ers = ers_new(sizeof(struct skill_timerskill), "skill.c::skill_timer_ers", ERS_OPT_NONE);

	add_timer_func_list(skill_unit_timer,"skill_unit_timer");
	add_timer_func_list(skill_castend_id,"skill_castend_id");
	add_timer_func_list(skill_castend_pos,"skill_castend_pos");
	add_timer_func_list(skill_timerskill,"skill_timerskill");
	add_timer_func_list(skill_blockpc_end, "skill_blockpc_end");

	add_timer_interval(gettick()+SKILLUNITTIMER_INTERVAL,skill_unit_timer,0,0,SKILLUNITTIMER_INTERVAL);
}

void do_final_skill(void)
{
	skill_destroy_requirement();
	db_destroy(skilldb_name2id);
	db_destroy(group_db);
	db_destroy(skillunit_db);
	db_destroy(skillusave_db);
	db_destroy(bowling_db);
	ers_destroy(skill_unit_ers);
	ers_destroy(skill_timer_ers);
}
