// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/showmsg.h"
#include "../common/random.h"
#include "../common/malloc.h"
#include "../common/utils.h"
#include "../common/ers.h"
#include "../common/strlib.h"

#include "map.h"
#include "path.h"
#include "pc.h"
#include "pet.h"
#include "npc.h"
#include "mob.h"
#include "clif.h"
#include "guild.h"
#include "skill.h"
#include "itemdb.h"
#include "battle.h"
#include "battleground.h"
#include "chrif.h"
#include "skill.h"
#include "status.h"
#include "script.h"
#include "unit.h"
#include "homunculus.h"
#include "mercenary.h"
#include "elemental.h"
#include "vending.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <math.h>

//Regen related flags.
enum e_regen
{
	RGN_NONE = 0x00,
	RGN_HP   = 0x01,
	RGN_SP   = 0x02,
	RGN_SHP  = 0x04,
	RGN_SSP  = 0x08,
};

static int refinebonus[MAX_REFINE_BONUS][3];	// 精錬ボーナステーブル(refine_db.txt)
int percentrefinery[REFINE_TYPE_MAX][MAX_REFINE+1];	// 精錬成功率(refine_db.txt)
static int atkmods[3][MAX_WEAPON_TYPE];	// 武器ATKサイズ修正(size_fix.txt)

// TODO: rewrite refine system later to use this struct... [15peaces]
static struct {
	struct refine_cost cost[REFINE_COST_MAX];
} refine_info[REFINE_TYPE_MAX];

static struct eri *sc_data_ers; //For sc_data entries
static struct status_data dummy_status;

int current_equip_item_index; //Contains inventory index of an equipped item. To pass it into the EQUP_SCRIPT [Lupus]
int current_equip_card_id; //To prevent card-stacking (from jA) [Skotlex]
//we need it for new cards 15 Feb 2005, to check if the combo cards are insrerted into the CURRENT weapon only
//to avoid cards exploits
short current_equip_opt_index; /// Contains random option index of an equipped item. [Secret]

unsigned int SCDisabled[SC_MAX]; /// List of disabled SC on map zones. [Cydh]
static bool status_change_isDisabledOnMap_(sc_type type, bool mapIsVS, bool mapIsPVP, bool mapIsGVG, bool mapIsBG, unsigned int mapZone, bool mapIsTE);
#define status_change_isDisabledOnMap(type, m) ( status_change_isDisabledOnMap_((type), map_flag_vs2((m)), map[(m)].flag.pvp, map_flag_gvg2_no_te((m)), map[(m)].flag.battleground, map[(m)].zone << 3, map_flag_gvg2_te((m))) )


static unsigned short status_calc_str(struct block_list *, struct status_change *, int);
static unsigned short status_calc_agi(struct block_list *, struct status_change *, int);
static unsigned short status_calc_vit(struct block_list *, struct status_change *, int);
static unsigned short status_calc_int(struct block_list *, struct status_change *, int);
static unsigned short status_calc_dex(struct block_list *, struct status_change *, int);
static unsigned short status_calc_luk(struct block_list *, struct status_change *, int);
static unsigned short status_calc_batk(struct block_list *, struct status_change *, int);
static unsigned short status_calc_watk(struct block_list *, struct status_change *, int);
static unsigned short status_calc_matk(struct block_list *, struct status_change *, int);
static signed short status_calc_hit(struct block_list *, struct status_change *, int);
static signed short status_calc_critical(struct block_list *, struct status_change *, int);
static signed short status_calc_flee(struct block_list *, struct status_change *, int);
static signed short status_calc_flee2(struct block_list *, struct status_change *, int);
static signed char status_calc_def(struct block_list *, struct status_change *, int);
static signed short status_calc_def2(struct block_list *, struct status_change *, int);
static signed char status_calc_mdef(struct block_list *, struct status_change *, int);
static signed short status_calc_mdef2(struct block_list *, struct status_change *, int);
static unsigned short status_calc_speed(struct block_list *, struct status_change *, int);
static short status_calc_aspd_amount(struct block_list *, struct status_change *, int);
static short status_calc_aspd_rate(struct block_list *, struct status_change *, int);
static unsigned short status_calc_dmotion(struct block_list *bl, struct status_change *sc, int dmotion);
static unsigned int status_calc_maxhp(struct block_list *bl, uint64 maxhp);
static unsigned int status_calc_maxsp(struct block_list *bl, uint64 maxsp);
static unsigned char status_calc_element(struct block_list *bl, struct status_change *sc, int element);
static unsigned char status_calc_element_lv(struct block_list *bl, struct status_change *sc, int lv);
static enum e_mode status_calc_mode(struct block_list *bl, struct status_change *sc, enum e_mode mode);
static int status_get_sc_interval(enum sc_type type);

/**
 * Returns the status change associated with a skill.
 * @param skill The skill to look up
 * @return The status registered for this skill
 **/
sc_type status_skill2sc(int skill)
{
	int sk = skill_get_index(skill);
	if( sk == 0 ) {
		ShowError("status_skill2sc: Unsupported skill id %d\n", skill);
		return SC_NONE;
	}
	return SkillStatusChangeTable[sk];
}

/**
 * Returns the FIRST skill (in order of definition in initChangeTables) to use a given status change.
 * Utilized for various duration lookups. Use with caution!
 * @param sc The status to look up
 * @return A skill associated with the status
 **/
int status_sc2skill(sc_type sc)
{
	if( sc < 0 || sc >= SC_MAX ) {
		ShowError("status_sc2skill: Unsupported status change id %d\n", sc);
		return 0;
	}

	return StatusSkillChangeTable[sc];
}

int status_sc2icon(sc_type sc)
{
	if( sc < 0 || sc >= SC_MAX ) {
		ShowError("status_sc2icon: Unsupported status change id %d\n", sc);
		return 0;
	}

	return StatusIconChangeTable[sc];
}

/**
 * Returns the status calculation flag associated with a given status change.
 * @param sc The status to look up
 * @return The scb_flag registered for this status (see enum scb_flag)
 **/
unsigned int status_sc2scb_flag(sc_type sc)
{
	if (sc < 0 || sc >= SC_MAX) {
		ShowError("status_sc2scb_flag: Unsupported status change id %d\n", sc);
		return SCB_NONE;
	}

	return StatusChangeFlagTable[sc];
}

/**
 * Returns the bl types which require a status change packet to be sent for a given client status identifier.
 * @param type The client-side status identifier to look up (see enum si_type)
 * @return The bl types relevant to the type (see enum bl_type)
 **/
int status_type2relevant_bl_types(int type)
{
	if (type < 0 || type >= SI_MAX) {
		ShowError("status_type2relevant_bl_types: Unsupported type %d\n", type);
		return SI_BLANK;
	}

	return StatusRelevantBLTypes[type];
}

#define add_sc(skill,sc) set_sc(skill,sc,SI_BLANK,SCB_NONE)
// indicates that the status displays a visual effect for the affected unit, and should be sent to the client for all supported units
#define set_sc_with_vfx(skill, sc, icon, flag) set_sc((skill), (sc), (icon), (flag)); if((icon) < SI_MAX) StatusRelevantBLTypes[(icon)] |= BL_SCEFFECT

static void set_sc(int skill, sc_type sc, int icon, unsigned int flag)
{
	int sk = skill_get_index(skill);
	if( sk == 0 ) {
		ShowError("set_sc: Unsupported skill id %d\n", skill);
		return;
	}
	if( sc < 0 || sc >= SC_MAX ) {
		ShowError("set_sc: Unsupported status change id %d\n", sc);
		return;
	}

	if( StatusSkillChangeTable[sc] == 0 )
  		StatusSkillChangeTable[sc] = skill;
	if( StatusIconChangeTable[sc] == SI_BLANK )
  		StatusIconChangeTable[sc] = icon;
	StatusChangeFlagTable[sc] |= flag;

	if( SkillStatusChangeTable[sk] == SC_NONE )
		SkillStatusChangeTable[sk] = sc;
}

void initChangeTables(void)
{
	int i;
	for (i = 0; i < SC_MAX; i++)
		StatusIconChangeTable[i] = SI_BLANK;
	for (i = 0; i < MAX_SKILL; i++)
		SkillStatusChangeTable[i] = SC_NONE;
	for (i = 0; i < SI_MAX; i++)
		StatusRelevantBLTypes[i] = BL_PC;


	memset(StatusSkillChangeTable, 0, sizeof(StatusSkillChangeTable));
	memset(StatusChangeFlagTable, 0, sizeof(StatusChangeFlagTable));
	memset(StatusDisplayType, 0, sizeof(StatusDisplayType));
	memset(SCDisabled, 0, sizeof(SCDisabled));

	//First we define the skill for common ailments. These are used in skill_additional_effect through sc cards. [Skotlex]
	set_sc( NPC_PETRIFYATTACK		, SC_STONE		, SI_BLANK		, SCB_DEF_ELE|SCB_DEF|SCB_MDEF );
	set_sc( NPC_WIDEFREEZE			, SC_FREEZE		, SI_BLANK		, SCB_DEF_ELE|SCB_DEF|SCB_MDEF );
	set_sc( NPC_STUNATTACK			, SC_STUN		, SI_BLANK		, SCB_NONE );
	set_sc( NPC_SLEEPATTACK			, SC_SLEEP		, SI_BLANK		, SCB_NONE );
	set_sc( NPC_POISON				, SC_POISON		, SI_BLANK		, SCB_DEF2|SCB_REGEN );
	set_sc(NPC_WIDECURSE			, SC_CURSE		, SI_BLANK		, SCB_LUK|SCB_BATK|SCB_WATK|SCB_SPEED );
	set_sc( NPC_SILENCEATTACK		, SC_SILENCE	, SI_BLANK		, SCB_NONE );
	set_sc( NPC_WIDECONFUSE			, SC_CONFUSION	, SI_BLANK		, SCB_NONE );
	set_sc( NPC_BLINDATTACK			, SC_BLIND		, SI_BLANK		, SCB_HIT|SCB_FLEE );
	set_sc( NPC_BLEEDING			, SC_BLEEDING	, SI_BLEEDING	, SCB_REGEN );
	set_sc( NPC_POISON				, SC_DPOISON	, SI_BLANK		, SCB_DEF2|SCB_REGEN );
	add_sc( ALL_REVERSEORCISH		, SC_ORCISH		);
	set_sc( NPC_WIDEHEALTHFEAR		, SC_FEAR		, SI_BLANK		, SCB_HIT|SCB_FLEE );
	set_sc( NPC_WIDEBODYBURNNING	, SC_BURNING	, SI_BLANK		, SCB_MDEF );
	//set_sc( WL_WHITEIMPRISON		, SC_IMPRISON	, SI_BLANK		, SCB_NONE );// No imprison skill for NPC's....yet.
	set_sc( NPC_WIDE_DEEP_SLEEP		, SC_DEEPSLEEP	, SI_DEEP_SLEEP	, SCB_NONE );
	set_sc( NPC_WIDEFROSTMISTY		, SC_FROST		, SI_FROSTMISTY	, SCB_DEF|SCB_SPEED|SCB_ASPD );
	set_sc( NPC_WIDECOLD			, SC_CRYSTALIZE	, SI_COLD		, SCB_NONE );

	//The main status definitions
	add_sc( SM_BASH              , SC_STUN            );
	set_sc( SM_PROVOKE           , SC_PROVOKE         , SI_PROVOKE         , SCB_DEF|SCB_DEF2|SCB_BATK|SCB_WATK );
	add_sc( SM_MAGNUM            , SC_WATK_ELEMENT    );
	set_sc( SM_ENDURE            , SC_ENDURE          , SI_ENDURE          , SCB_MDEF|SCB_DSPD );
	add_sc( MG_SIGHT             , SC_SIGHT           );
	add_sc( MG_SAFETYWALL        , SC_SAFETYWALL      );
	add_sc( MG_FROSTDIVER        , SC_FREEZE          );
	add_sc( MG_STONECURSE        , SC_STONE           );
	add_sc( AL_RUWACH            , SC_RUWACH          );
	add_sc( AL_PNEUMA            , SC_PNEUMA          );
	set_sc( AL_INCAGI            , SC_INCREASEAGI     , SI_INCREASEAGI     , SCB_AGI|SCB_SPEED );
	set_sc( AL_DECAGI            , SC_DECREASEAGI     , SI_DECREASEAGI     , SCB_AGI|SCB_SPEED );
	set_sc( AL_CRUCIS            , SC_SIGNUMCRUCIS    , SI_SIGNUMCRUCIS    , SCB_DEF );
	set_sc( AL_ANGELUS           , SC_ANGELUS         , SI_ANGELUS         , SCB_DEF2 );
	set_sc( AL_BLESSING          , SC_BLESSING        , SI_BLESSING        , SCB_STR|SCB_INT|SCB_DEX );
	set_sc( AC_CONCENTRATION     , SC_CONCENTRATE     , SI_CONCENTRATE     , SCB_AGI|SCB_DEX );
	set_sc( TF_HIDING            , SC_HIDING          , SI_HIDING          , SCB_SPEED );
	add_sc( TF_POISON            , SC_POISON          );
	
	// Knight
	set_sc( KN_TWOHANDQUICKEN	, SC_TWOHANDQUICKEN	, SI_TWOHANDQUICKEN	, SCB_ASPD );
	set_sc( KN_AUTOCOUNTER		, SC_AUTOCOUNTER	, SI_AUTOCOUNTER	, SCB_NONE );
	
	set_sc( PR_IMPOSITIO		, SC_IMPOSITIO			, SI_IMPOSITIO       , SCB_WATK );
	set_sc( PR_SUFFRAGIUM		, SC_SUFFRAGIUM			, SI_SUFFRAGIUM      , SCB_NONE );
	set_sc( PR_ASPERSIO			, SC_ASPERSIO			, SI_ASPERSIO        , SCB_ATK_ELE );
	set_sc( PR_BENEDICTIO		, SC_BENEDICTIO			, SI_BENEDICTIO      , SCB_DEF_ELE );
	set_sc( PR_SLOWPOISON		, SC_SLOWPOISON			, SI_SLOWPOISON      , SCB_REGEN );
	set_sc( PR_KYRIE			, SC_KYRIE				, SI_KYRIE           , SCB_NONE );
	set_sc( PR_MAGNIFICAT		, SC_MAGNIFICAT			, SI_MAGNIFICAT      , SCB_REGEN );
	set_sc( PR_GLORIA			, SC_GLORIA				, SI_GLORIA          , SCB_LUK );
	add_sc( PR_STRECOVERY		, SC_BLIND				);
	add_sc( PR_LEXDIVINA		, SC_SILENCE			);
	set_sc( PR_LEXAETERNA		, SC_AETERNA			, SI_AETERNA         , SCB_NONE );
	add_sc( WZ_METEOR			, SC_STUN				);
	add_sc( WZ_VERMILION		, SC_BLIND				);
	add_sc( WZ_FROSTNOVA		, SC_FREEZE				);
	add_sc( WZ_STORMGUST		, SC_FREEZE				);
	set_sc( WZ_QUAGMIRE			, SC_QUAGMIRE			, SI_QUAGMIRE        , SCB_AGI|SCB_DEX|SCB_ASPD|SCB_SPEED );
	add_sc( BS_HAMMERFALL		, SC_STUN				);
	set_sc( BS_ADRENALINE		, SC_ADRENALINE			, SI_ADRENALINE      , SCB_ASPD );
	set_sc( BS_WEAPONPERFECT	, SC_WEAPONPERFECTION	, SI_WEAPONPERFECTION, SCB_NONE );
	set_sc( BS_OVERTHRUST		, SC_OVERTHRUST			, SI_OVERTHRUST      , SCB_NONE );
	set_sc( BS_MAXIMIZE			, SC_MAXIMIZEPOWER		, SI_MAXIMIZEPOWER   , SCB_REGEN );
	
	// Hunter
	add_sc( HT_LANDMINE		, SC_STUN	);
	set_sc( HT_ANKLESNARE	, SC_ANKLE	, SI_ANKLESNARE	,SCB_NONE);
	add_sc( HT_SANDMAN		, SC_SLEEP	);
	add_sc( HT_FLASHER		, SC_BLIND	);
	add_sc( HT_FREEZINGTRAP	, SC_FREEZE	);

	// Assassin
	set_sc( AS_CLOAKING			, SC_CLOAKING		, SI_CLOAKING		, SCB_CRI|SCB_SPEED );
	add_sc( AS_SONICBLOW		, SC_STUN			);
	set_sc( AS_ENCHANTPOISON	, SC_ENCPOISON		, SI_ENCPOISON		, SCB_ATK_ELE );
	set_sc( AS_POISONREACT		, SC_POISONREACT	, SI_POISONREACT	, SCB_NONE );
	add_sc( AS_VENOMDUST		, SC_POISON			);
	set_sc( AS_SPLASHER			, SC_SPLASHER		, SI_SPLASHER		, SCB_NONE);

	set_sc( NV_TRICKDEAD         , SC_TRICKDEAD       , SI_TRICKDEAD       , SCB_REGEN );
	set_sc( SM_AUTOBERSERK       , SC_AUTOBERSERK     , SI_AUTOBERSERK     , SCB_NONE );
	add_sc( TF_SPRINKLESAND      , SC_BLIND           );
	add_sc( TF_THROWSTONE        , SC_STUN            );
	set_sc( MC_LOUD              , SC_LOUD            , SI_LOUD            , SCB_STR );
	set_sc( MG_ENERGYCOAT        , SC_ENERGYCOAT      , SI_ENERGYCOAT      , SCB_NONE );
	
	// NPC Skills
	set_sc( NPC_EMOTION          , SC_MODECHANGE      , SI_BLANK           , SCB_MODE );
	add_sc( NPC_EMOTION_ON       , SC_MODECHANGE      );
	set_sc( NPC_ATTRICHANGE      , SC_ELEMENTALCHANGE , SI_ARMOR_PROPERTY  , SCB_DEF_ELE );
	add_sc( NPC_CHANGEWATER      , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEGROUND     , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEFIRE       , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEWIND       , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEPOISON     , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEHOLY       , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGEDARKNESS   , SC_ELEMENTALCHANGE );
	add_sc( NPC_CHANGETELEKINESIS, SC_ELEMENTALCHANGE );
	add_sc( NPC_POISON           , SC_POISON          );
	add_sc( NPC_BLINDATTACK      , SC_BLIND           );
	add_sc( NPC_SILENCEATTACK    , SC_SILENCE         );
	add_sc( NPC_STUNATTACK       , SC_STUN            );
	add_sc( NPC_PETRIFYATTACK    , SC_STONE           );
	add_sc( NPC_CURSEATTACK      , SC_CURSE           );
	add_sc( NPC_SLEEPATTACK      , SC_SLEEP           );
	add_sc( NPC_MAGICALATTACK    , SC_MAGICALATTACK   );
	set_sc( NPC_KEEPING          , SC_KEEPING         , SI_BLANK           , SCB_DEF );
	add_sc( NPC_DARKBLESSING     , SC_COMA            );
	set_sc( NPC_BARRIER          , SC_BARRIER         , SI_BARRIER		, SCB_MDEF|SCB_DEF );
	add_sc( NPC_DEFENDER         , SC_ARMOR           );
	add_sc( NPC_LICK             , SC_STUN            );
	set_sc( NPC_HALLUCINATION    , SC_HALLUCINATION   , SI_HALLUCINATION   , SCB_NONE );
	add_sc( NPC_REBIRTH          , SC_REBIRTH         );
	add_sc( RG_RAID              , SC_STUN            );
	set_sc( RG_STRIPWEAPON       , SC_STRIPWEAPON     , SI_STRIPWEAPON     , SCB_WATK );
	set_sc( RG_STRIPSHIELD       , SC_STRIPSHIELD     , SI_STRIPSHIELD     , SCB_DEF );
	set_sc( RG_STRIPARMOR        , SC_STRIPARMOR      , SI_STRIPARMOR      , SCB_VIT );
	set_sc( RG_STRIPHELM         , SC_STRIPHELM       , SI_STRIPHELM       , SCB_INT );
	add_sc( AM_ACIDTERROR        , SC_BLEEDING        );
	set_sc( AM_CP_WEAPON         , SC_CP_WEAPON       , SI_CP_WEAPON       , SCB_NONE );
	set_sc( AM_CP_SHIELD         , SC_CP_SHIELD       , SI_CP_SHIELD       , SCB_NONE );
	set_sc( AM_CP_ARMOR          , SC_CP_ARMOR        , SI_CP_ARMOR        , SCB_NONE );
	set_sc( AM_CP_HELM           , SC_CP_HELM         , SI_CP_HELM         , SCB_NONE );
	
	// Crusader
	set_sc( CR_AUTOGUARD		, SC_AUTOGUARD		, SI_AUTOGUARD		, SCB_NONE );
	add_sc( CR_SHIELDCHARGE		, SC_STUN			);
	set_sc( CR_REFLECTSHIELD	, SC_REFLECTSHIELD	, SI_REFLECTSHIELD	, SCB_NONE );
	add_sc( CR_HOLYCROSS		, SC_BLIND			);
	add_sc( CR_GRANDCROSS		, SC_BLIND			);
	set_sc( CR_DEVOTION			, SC_DEVOTION		, SI_DEVOTION		, SCB_NONE);
	set_sc( CR_PROVIDENCE		, SC_PROVIDENCE		, SI_PROVIDENCE		, SCB_ALL );
	set_sc( CR_DEFENDER			, SC_DEFENDER		, SI_DEFENDER		, SCB_SPEED|SCB_ASPD );
	set_sc( CR_SPEARQUICKEN		, SC_SPEARQUICKEN	, SI_SPEARQUICKEN	, SCB_ASPD );
	
	// Monk
	set_sc( MO_STEELBODY		, SC_STEELBODY			, SI_STEELBODY			, SCB_DEF|SCB_MDEF|SCB_ASPD|SCB_SPEED );
	add_sc( MO_BLADESTOP		, SC_BLADESTOP_WAIT		);
	set_sc( MO_BLADESTOP		, SC_BLADESTOP			, SI_BLADESTOP			, SCB_NONE);
	set_sc( MO_EXPLOSIONSPIRITS	, SC_EXPLOSIONSPIRITS	, SI_EXPLOSIONSPIRITS	, SCB_CRI|SCB_REGEN );
	set_sc( MO_EXTREMITYFIST	, SC_EXTREMITYFIST		, SI_EXTREMITYFIST		, SCB_REGEN );
	
	// Sage
	set_sc( SA_MAGICROD			, SC_MAGICROD		, SI_MAGICROD		, SCB_NONE);
	set_sc( SA_AUTOSPELL		, SC_AUTOSPELL		, SI_AUTOSPELL		, SCB_NONE );
	set_sc( SA_FLAMELAUNCHER	, SC_FIREWEAPON		, SI_FIREWEAPON		, SCB_ATK_ELE );
	set_sc( SA_FROSTWEAPON		, SC_WATERWEAPON	, SI_WATERWEAPON	, SCB_ATK_ELE );
	set_sc( SA_LIGHTNINGLOADER	, SC_WINDWEAPON		, SI_WINDWEAPON		, SCB_ATK_ELE );
	set_sc( SA_SEISMICWEAPON	, SC_EARTHWEAPON	, SI_EARTHWEAPON	, SCB_ATK_ELE );
	set_sc( SA_VOLCANO			, SC_VOLCANO		, SI_LANDENDOW		, SCB_WATK );
	set_sc( SA_DELUGE			, SC_DELUGE			, SI_LANDENDOW		, SCB_MAXHP );
	set_sc( SA_VIOLENTGALE		, SC_VIOLENTGALE	, SI_LANDENDOW		, SCB_FLEE );
	add_sc( SA_REVERSEORCISH	, SC_ORCISH			);
	add_sc( SA_COMA				, SC_COMA			);
	
	// Bard & Dancer
	set_sc( BD_ENCORE			, SC_DANCING		, SI_BDPLAYING		, SCB_SPEED | SCB_REGEN);
	set_sc( BD_RICHMANKIM		, SC_RICHMANKIM		, SI_RICHMANKIM		, SCB_NONE);
	set_sc( BD_ETERNALCHAOS		, SC_ETERNALCHAOS	, SI_ETERNALCHAOS	, SCB_DEF2);
	set_sc( BD_DRUMBATTLEFIELD	, SC_DRUMBATTLE		, SI_DRUMBATTLEFIELD, SCB_WATK | SCB_DEF);
	set_sc( BD_RINGNIBELUNGEN	, SC_NIBELUNGEN		, SI_RINGNIBELUNGEN	, SCB_WATK);
	set_sc( BD_ROKISWEIL		, SC_ROKISWEIL		, SI_ROKISWEIL		, SCB_NONE);
	set_sc( BD_INTOABYSS		, SC_INTOABYSS		, SI_INTOABYSS		, SCB_NONE);
	set_sc( BD_SIEGFRIED		, SC_SIEGFRIED		, SI_SIEGFRIED		, SCB_DEF_ELE);
	
	// Bard
	add_sc( BA_FROSTJOKER	, SC_FREEZE		);
	set_sc( BA_WHISTLE		, SC_WHISTLE	, SI_WHISTLE		, SCB_FLEE | SCB_FLEE2);
	set_sc( BA_ASSASSINCROSS, SC_ASSNCROS	, SI_ASSASSINCROSS	, SCB_ASPD);
	set_sc( BA_POEMBRAGI	, SC_POEMBRAGI	, SI_POEMBRAGI		, SCB_NONE);
	set_sc( BA_APPLEIDUN	, SC_APPLEIDUN	, SI_APPLEIDUN		, SCB_MAXHP);
	
	// Dancer
	add_sc( DC_SCREAM		, SC_STUN			);
	set_sc( DC_HUMMING		, SC_HUMMING		, SI_HUMMING		, SCB_HIT);
	set_sc( DC_DONTFORGETME	, SC_DONTFORGETME	, SI_DONTFORGETME	, SCB_SPEED | SCB_ASPD);
	set_sc( DC_FORTUNEKISS	, SC_FORTUNE		, SI_FORTUNEKISS	, SCB_CRI);
	set_sc( DC_SERVICEFORYOU, SC_SERVICE4U		, SI_SERVICEFORYOU	, SCB_ALL);

	add_sc( NPC_DARKCROSS        , SC_BLIND           );
	add_sc( NPC_GRANDDARKNESS    , SC_BLIND           );
	set_sc( NPC_STOP             , SC_STOP            , SI_STOP            , SCB_NONE );
	set_sc( NPC_WEAPONBRAKER     , SC_BROKENWEAPON    , SI_BROKENWEAPON    , SCB_NONE );
	set_sc( NPC_ARMORBRAKE       , SC_BROKENARMOR     , SI_BROKENARMOR     , SCB_NONE );
	set_sc( NPC_CHANGEUNDEAD     , SC_CHANGEUNDEAD    , SI_UNDEAD          , SCB_DEF_ELE );
	set_sc( NPC_POWERUP          , SC_INCHITRATE      , SI_BLANK           , SCB_HIT );
	set_sc( NPC_AGIUP            , SC_INCFLEERATE     , SI_BLANK           , SCB_FLEE );
	add_sc( NPC_INVISIBLE        , SC_CLOAKING        );
	set_sc( LK_AURABLADE         , SC_AURABLADE       , SI_AURABLADE       , SCB_NONE );
	set_sc( LK_PARRYING          , SC_PARRYING        , SI_PARRYING        , SCB_NONE );
	set_sc( LK_CONCENTRATION     , SC_CONCENTRATION   , SI_CONCENTRATION   , SCB_BATK|SCB_WATK|SCB_HIT|SCB_DEF|SCB_DEF2|SCB_MDEF|SCB_DSPD );
	set_sc( LK_TENSIONRELAX      , SC_TENSIONRELAX    , SI_TENSIONRELAX    , SCB_REGEN );
	set_sc( LK_BERSERK           , SC_BERSERK         , SI_BERSERK         , SCB_DEF|SCB_DEF2|SCB_MDEF|SCB_MDEF2|SCB_FLEE|SCB_SPEED|SCB_ASPD|SCB_MAXHP|SCB_REGEN );
	set_sc( HP_ASSUMPTIO         , SC_ASSUMPTIO       , SI_ASSUMPTIO       , SCB_NONE );
	add_sc( HP_BASILICA          , SC_BASILICA        );
	set_sc( HW_MAGICPOWER        , SC_MAGICPOWER      , SI_MAGICPOWER      , SCB_MATK );
	
	// Paladin
	add_sc( PA_SACRIFICE, SC_SACRIFICE	);
	set_sc( PA_GOSPEL	, SC_GOSPEL		, SI_GOSPEL, SCB_SPEED | SCB_ASPD);
	add_sc( PA_GOSPEL	, SC_SCRESIST	);
	
	add_sc( CH_TIGERFIST         , SC_STOP            );
	set_sc( ASC_EDP              , SC_EDP             , SI_EDP             , SCB_NONE );
	set_sc( SN_SIGHT             , SC_TRUESIGHT       , SI_TRUESIGHT       , SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK|SCB_CRI|SCB_HIT );
	set_sc( SN_WINDWALK          , SC_WINDWALK        , SI_WINDWALK        , SCB_FLEE|SCB_SPEED );
	set_sc( WS_MELTDOWN          , SC_MELTDOWN        , SI_MELTDOWN        , SCB_NONE );
	set_sc( WS_CARTBOOST         , SC_CARTBOOST       , SI_CARTBOOST       , SCB_SPEED );
	
	// Stalker
	set_sc( ST_CHASEWALK	, SC_CHASEWALK	, SI_CHASEWALK	, SCB_SPEED);
	set_sc( ST_REJECTSWORD	, SC_REJECTSWORD, SI_REJECTSWORD, SCB_NONE );
	add_sc( ST_REJECTSWORD	, SC_AUTOCOUNTER);
	
	set_sc( CG_MARIONETTE        , SC_MARIONETTE      , SI_MARIONETTE      , SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK );
	set_sc( CG_MARIONETTE        , SC_MARIONETTE2     , SI_MARIONETTE2     , SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK );
	add_sc( LK_SPIRALPIERCE      , SC_STOP            );
	add_sc( LK_HEADCRUSH         , SC_BLEEDING        );
	set_sc( LK_JOINTBEAT         , SC_JOINTBEAT       , SI_JOINTBEAT       , SCB_BATK|SCB_DEF2|SCB_SPEED|SCB_ASPD );
	add_sc( HW_NAPALMVULCAN      , SC_CURSE           );
	
	// Professor
	set_sc( PF_MINDBREAKER	, SC_MINDBREAKER, SI_MINDBREAKER, SCB_MATK | SCB_MDEF2);
	set_sc( PF_MEMORIZE		, SC_MEMORIZE	, SI_MEMORIZE	, SCB_NONE);
	set_sc( PF_FOGWALL		, SC_FOGWALL	, SI_FOGWALL	, SCB_NONE);
	set_sc( PF_SPIDERWEB	, SC_SPIDERWEB	, SI_SPIDERWEB	, SCB_FLEE);
	
	set_sc( WE_BABY              , SC_BABY            , SI_BABY            , SCB_NONE );
	set_sc( TK_RUN               , SC_RUN             , SI_RUN             , SCB_SPEED|SCB_DSPD );
	set_sc( TK_RUN               , SC_SPURT           , SI_SPURT           , SCB_STR );
	set_sc( TK_READYSTORM        , SC_READYSTORM      , SI_READYSTORM      , SCB_NONE );
	set_sc( TK_READYDOWN         , SC_READYDOWN       , SI_READYDOWN       , SCB_NONE );
	add_sc( TK_DOWNKICK          , SC_STUN            );
	set_sc( TK_READYTURN         , SC_READYTURN       , SI_READYTURN       , SCB_NONE );
	set_sc( TK_READYCOUNTER      , SC_READYCOUNTER    , SI_READYCOUNTER    , SCB_NONE );
	set_sc( TK_DODGE             , SC_DODGE           , SI_DODGE           , SCB_NONE );
	set_sc( TK_SPTIME            , SC_EARTHSCROLL     , SI_EARTHSCROLL     , SCB_NONE );
	add_sc( TK_SEVENWIND         , SC_SEVENWIND ); 
	set_sc( TK_SEVENWIND         , SC_GHOSTWEAPON     , SI_GHOSTWEAPON     , SCB_ATK_ELE ); 
	set_sc( TK_SEVENWIND         , SC_SHADOWWEAPON    , SI_SHADOWWEAPON    , SCB_ATK_ELE ); 
	set_sc( SG_SUN_WARM          , SC_WARM            , SI_WARM            , SCB_NONE );
	add_sc( SG_MOON_WARM         , SC_WARM            );
	add_sc( SG_STAR_WARM         , SC_WARM            );
	set_sc( SG_SUN_COMFORT       , SC_SUN_COMFORT     , SI_SUN_COMFORT     , SCB_DEF2 );
	set_sc( SG_MOON_COMFORT      , SC_MOON_COMFORT    , SI_MOON_COMFORT    , SCB_FLEE );
	set_sc( SG_STAR_COMFORT      , SC_STAR_COMFORT    , SI_STAR_COMFORT    , SCB_ASPD );
	add_sc( SG_FRIEND            , SC_SKILLRATE_UP    );
	set_sc( SG_KNOWLEDGE         , SC_KNOWLEDGE       , SI_BLANK           , SCB_ALL );
	set_sc( SG_FUSION            , SC_FUSION          , SI_BLANK           , SCB_SPEED );
	set_sc( BS_ADRENALINE2       , SC_ADRENALINE2     , SI_ADRENALINE2     , SCB_ASPD );
	
	// Soul Linker
	set_sc( SL_KAIZEL	, SC_KAIZEL	, SI_KAIZEL	, SCB_NONE );
	set_sc( SL_KAAHI	, SC_KAAHI	, SI_KAAHI	, SCB_NONE );
	set_sc( SL_KAUPE	, SC_KAUPE	, SI_KAUPE	, SCB_NONE );
	set_sc( SL_KAITE	, SC_KAITE	, SI_KAITE	, SCB_NONE );
	add_sc( SL_STUN		, SC_STUN	);
	set_sc( SL_SWOO		, SC_SWOO	, SI_SWOO	, SCB_SPEED);
	set_sc( SL_SKE		, SC_SKE	, SI_BLANK	, SCB_BATK|SCB_WATK|SCB_DEF|SCB_DEF2 );
	set_sc( SL_SKA		, SC_SKA	, SI_BLANK	, SCB_DEF2|SCB_MDEF2|SCB_SPEED|SCB_ASPD );
	set_sc( SL_SMA		, SC_SMA	, SI_SMA	, SCB_NONE );

	// 2nd Job And Other Skills
	set_sc( SM_SELFPROVOKE		, SC_PROVOKE		, SI_PROVOKE		, SCB_DEF|SCB_DEF2|SCB_BATK|SCB_WATK );
	set_sc( ST_PRESERVE			, SC_PRESERVE		, SI_PRESERVE		, SCB_NONE );
	set_sc( PF_DOUBLECASTING	, SC_DOUBLECAST		, SI_DOUBLECAST		, SCB_NONE );
	set_sc( HW_GRAVITATION		, SC_GRAVITATION	, SI_GRAVITATION	, SCB_ASPD );
	add_sc( WS_CARTTERMINATION	, SC_STUN			);
	set_sc( WS_OVERTHRUSTMAX	, SC_MAXOVERTHRUST	, SI_MAXOVERTHRUST	, SCB_NONE );
	set_sc( CG_LONGINGFREEDOM	, SC_LONGING		, SI_LONGING		, SCB_SPEED | SCB_ASPD);
	set_sc( CG_HERMODE			, SC_HERMODE		, SI_HERMODE		, SCB_NONE);
	set_sc(CG_TAROTCARD			, SC_TAROTCARD		, SI_TAROT			, SCB_NONE);
	set_sc( ITEM_ENCHANTARMS	, SC_ENCHANTARMS	, SI_BLANK			, SCB_ATK_ELE );
	set_sc( SL_HIGH				, SC_SPIRIT			, SI_SPIRIT			, SCB_ALL );
	set_sc( KN_ONEHAND			, SC_ONEHAND		, SI_ONEHAND		, SCB_ASPD );
	set_sc( GS_FLING			, SC_FLING			, SI_BLANK			, SCB_DEF|SCB_DEF2 );
	add_sc( GS_CRACKER			, SC_STUN			);
	add_sc( GS_DISARM			, SC_STRIPWEAPON	);
	add_sc( GS_PIERCINGSHOT		, SC_BLEEDING		);
	set_sc( GS_MADNESSCANCEL	, SC_MADNESSCANCEL	, SI_MADNESSCANCEL	, SCB_BATK|SCB_ASPD );
	set_sc( GS_ADJUSTMENT		, SC_ADJUSTMENT		, SI_ADJUSTMENT		, SCB_HIT|SCB_FLEE );
	set_sc( GS_INCREASING		, SC_INCREASING		, SI_ACCURACY		, SCB_AGI|SCB_DEX|SCB_HIT );
	set_sc( GS_GATLINGFEVER		, SC_GATLINGFEVER	, SI_GATLINGFEVER	, SCB_BATK|SCB_FLEE|SCB_SPEED|SCB_ASPD );
	set_sc( NJ_TATAMIGAESHI		, SC_TATAMIGAESHI	, SI_BLANK			, SCB_NONE );
	set_sc( NJ_SUITON			, SC_SUITON			, SI_BLANK			, SCB_AGI|SCB_SPEED );
	add_sc( NJ_HYOUSYOURAKU		, SC_FREEZE			);
	set_sc( NJ_NEN				, SC_NEN			, SI_NEN			, SCB_STR|SCB_INT );
	set_sc( NJ_UTSUSEMI			, SC_UTSUSEMI		, SI_UTSUSEMI		, SCB_NONE );
	set_sc( NJ_BUNSINJYUTSU		, SC_BUNSINJYUTSU	, SI_BUNSINJYUTSU	, SCB_DYE );

	add_sc( NPC_ICEBREATH        , SC_FREEZE          );
	add_sc( NPC_ACIDBREATH       , SC_POISON          );
	add_sc( NPC_HELLJUDGEMENT    , SC_CURSE           );
	add_sc( NPC_WIDESILENCE      , SC_SILENCE         );
	add_sc( NPC_WIDEFREEZE       , SC_FREEZE          );
	add_sc( NPC_WIDEBLEEDING     , SC_BLEEDING        );
	add_sc( NPC_WIDESTONE        , SC_STONE           );
	add_sc( NPC_WIDECONFUSE      , SC_CONFUSION       );
	add_sc( NPC_WIDESLEEP        , SC_SLEEP           );
	add_sc( NPC_WIDESIGHT        , SC_SIGHT           );
	add_sc( NPC_EVILLAND         , SC_BLIND           );
	add_sc( NPC_MAGICMIRROR      , SC_MAGICMIRROR     );
	set_sc( NPC_SLOWCAST         , SC_SLOWCAST        , SI_SLOWCAST        , SCB_NONE );
	set_sc( NPC_CRITICALWOUND    , SC_CRITICALWOUND   , SI_CRITICALWOUND   , SCB_NONE );
	set_sc( NPC_STONESKIN        , SC_ARMORCHANGE     , SI_BLANK           , SCB_NONE);
	add_sc( NPC_ANTIMAGIC        , SC_ARMORCHANGE     );
	add_sc( NPC_WIDECURSE        , SC_CURSE           );
	add_sc( NPC_WIDESTUN         , SC_STUN            );

	set_sc( NPC_HELLPOWER        , SC_HELLPOWER       , SI_HELLPOWER       , SCB_NONE );
	set_sc( NPC_WIDEHELLDIGNITY  , SC_HELLPOWER       , SI_HELLPOWER       , SCB_NONE );
	set_sc( NPC_INVINCIBLE       , SC_INVINCIBLE      , SI_INVINCIBLE      , SCB_SPEED );
	set_sc( NPC_INVINCIBLEOFF    , SC_INVINCIBLEOFF   , SI_BLANK           , SCB_SPEED );

	set_sc( CASH_BLESSING        , SC_BLESSING        , SI_BLESSING        , SCB_STR|SCB_INT|SCB_DEX );
	set_sc( CASH_INCAGI          , SC_INCREASEAGI     , SI_INCREASEAGI     , SCB_AGI|SCB_SPEED );
	set_sc( CASH_ASSUMPTIO       , SC_ASSUMPTIO       , SI_ASSUMPTIO       , SCB_NONE );

	set_sc(ALL_PARTYFLEE, SC_PARTYFLEE, SI_PARTYFLEE, SCB_NONE);

	set_sc( CR_SHRINK            , SC_SHRINK          , SI_SHRINK          , SCB_NONE );
	set_sc( RG_CLOSECONFINE      , SC_CLOSECONFINE2   , SI_CLOSECONFINE2   , SCB_NONE );
	set_sc( RG_CLOSECONFINE      , SC_CLOSECONFINE    , SI_CLOSECONFINE    , SCB_FLEE );
	set_sc( WZ_SIGHTBLASTER      , SC_SIGHTBLASTER    , SI_SIGHTBLASTER    , SCB_NONE );
	set_sc( DC_WINKCHARM         , SC_WINKCHARM       , SI_WINKCHARM       , SCB_NONE );
	add_sc( MO_BALKYOUNG         , SC_STUN            );
	add_sc( SA_ELEMENTWATER      , SC_ELEMENTALCHANGE );
	add_sc( SA_ELEMENTFIRE       , SC_ELEMENTALCHANGE );
	add_sc( SA_ELEMENTGROUND     , SC_ELEMENTALCHANGE );
	add_sc( SA_ELEMENTWIND       , SC_ELEMENTALCHANGE );
	
	// Rune Knight
	set_sc( RK_ENCHANTBLADE			, SC_ENCHANTBLADE		, SI_ENCHANTBLADE		, SCB_NONE );
	set_sc( RK_DEATHBOUND			, SC_DEATHBOUND			, SI_DEATHBOUND			, SCB_NONE );
	add_sc( RK_WINDCUTTER			, SC_FEAR				);
	add_sc( RK_DRAGONBREATH			, SC_BURNING			);
	add_sc( RK_DRAGONHOWLING		, SC_FEAR				);
	set_sc( RK_CRUSHSTRIKE			, SC_CRUSHSTRIKE		, SI_CRUSHSTRIKE		, SCB_NONE );
	set_sc( RK_REFRESH				, SC_REFRESH			, SI_REFRESH			, SCB_NONE );
	set_sc( RK_GIANTGROWTH			, SC_GIANTGROWTH		, SI_GIANTGROWTH		, SCB_STR );
	set_sc( RK_STONEHARDSKIN		, SC_STONEHARDSKIN		, SI_STONEHARDSKIN		, SCB_DEF | SCB_MDEF);
	set_sc( RK_VITALITYACTIVATION	, SC_VITALITYACTIVATION	, SI_VITALITYACTIVATION	, SCB_REGEN );
	set_sc( RK_FIGHTINGSPIRIT		, SC_FIGHTINGSPIRIT		, SI_FIGHTINGSPIRIT		, SCB_WATK|SCB_ASPD );
	set_sc( RK_ABUNDANCE			, SC_ABUNDANCE			, SI_ABUNDANCE			, SCB_NONE );

	// Guillotine Cross
	set_sc_with_vfx( GC_VENOMIMPRESS      , SC_VENOMIMPRESS		, SI_VENOMIMPRESS       , SCB_NONE );
	set_sc( GC_POISONINGWEAPON   , SC_POISONINGWEAPON	, SI_POISONINGWEAPON    , SCB_NONE );
	set_sc( GC_WEAPONBLOCKING    , SC_WEAPONBLOCKING	, SI_WEAPONBLOCKING     , SCB_NONE );
	set_sc( GC_CLOAKINGEXCEED    , SC_CLOAKINGEXCEED	, SI_CLOAKINGEXCEED     , SCB_SPEED );
	set_sc( GC_HALLUCINATIONWALK , SC_HALLUCINATIONWALK	, SI_HALLUCINATIONWALK	, SCB_FLEE );
	set_sc( GC_ROLLINGCUTTER     , SC_ROLLINGCUTTER		, SI_ROLLINGCUTTER      , SCB_NONE );

	// Arch Bishop
	set_sc( AB_ADORAMUS     , SC_ADORAMUS       , SI_ADORAMUS       , SCB_AGI|SCB_SPEED );
	set_sc(AB_CLEMENTIA		, SC_BLESSING		, SI_BLESSING		, SCB_STR | SCB_INT | SCB_DEX);
	set_sc(AB_CANTO			, SC_INCREASEAGI	, SI_INCREASEAGI	, SCB_AGI | SCB_SPEED);
	set_sc( AB_EPICLESIS	, SC_EPICLESIS		, SI_EPICLESIS		, SCB_MAXHP );
	set_sc(AB_PRAEFATIO		, SC_KYRIE			, SI_KYRIE			, SCB_NONE);
	set_sc_with_vfx(AB_ORATIO		, SC_ORATIO			, SI_ORATIO			, SCB_NONE);
	set_sc(AB_LAUDAAGNUS	, SC_LAUDAAGNUS		, SI_LAUDAAGNUS		, SCB_MAXHP);
	set_sc(AB_LAUDARAMUS	, SC_LAUDARAMUS		, SI_LAUDARAMUS		, SCB_NONE);
	set_sc(AB_RENOVATIO		, SC_RENOVATIO		, SI_RENOVATIO		, SCB_REGEN);
	set_sc(AB_EXPIATIO		, SC_EXPIATIO		, SI_EXPIATIO		, SCB_NONE);
	set_sc( AB_DUPLELIGHT	, SC_DUPLELIGHT		, SI_DUPLELIGHT		, SCB_NONE );
	set_sc( AB_SECRAMENT	, SC_AB_SECRAMENT	, SI_AB_SECRAMENT	, SCB_NONE );

	// Warlock
	set_sc( WL_WHITEIMPRISON	, SC_IMPRISON		, SI_BLANK				, SCB_NONE);
	add_sc( WL_FROSTMISTY		, SC_FROST			);
	add_sc( WL_JACKFROST        , SC_FREEZE			);
	set_sc( WL_MARSHOFABYSS		, SC_MARSHOFABYSS	, SI_MARSHOFABYSS		, SCB_AGI | SCB_DEX | SCB_SPEED );
	set_sc( WL_RECOGNIZEDSPELL  , SC_RECOGNIZEDSPELL, SI_RECOGNIZEDSPELL	, SCB_NONE );
	add_sc( WL_SIENNAEXECRATE   , SC_STONE			);
	set_sc( WL_STASIS           , SC_STASIS         , SI_STASIS				, SCB_NONE );
	add_sc( WL_CRIMSONROCK      , SC_STUN			);
	add_sc( WL_HELLINFERNO      , SC_BURNING		);
	add_sc( WL_COMET            , SC_BURNING		);

	// Ranger
	set_sc(RA_FEARBREEZE		, SC_FEARBREEZE			, SI_FEARBREEZE		, SCB_NONE);
	set_sc(RA_ELECTRICSHOCKER	, SC_ELECTRICSHOCKER	, SI_ELECTRICSHOCKER, SCB_NONE);
	set_sc(RA_WUGDASH			, SC_WUGDASH			, SI_WUGDASH		, SCB_SPEED);
	set_sc( RA_CAMOUFLAGE		, SC_CAMOUFLAGE			, SI_CAMOUFLAGE		, SCB_WATK|SCB_DEF|SCB_SPEED|SCB_CRI );
	add_sc(RA_MAGENTATRAP		, SC_ELEMENTALCHANGE	);
	add_sc(RA_COBALTTRAP		, SC_ELEMENTALCHANGE	);
	add_sc(RA_MAIZETRAP			, SC_ELEMENTALCHANGE	);
	add_sc(RA_ELECTRICSHOCKER	, SC_ELECTRICSHOCKER	);
	add_sc(RA_VERDURETRAP		, SC_ELEMENTALCHANGE	);
	add_sc(RA_FIRINGTRAP		, SC_BURNING			);
	add_sc(RA_ICEBOUNDTRAP      , SC_FROST				);

	// Mechanic
	add_sc( NC_FLAMELAUNCHER    , SC_BURNING        );
	add_sc( NC_COLDSLOWER       , SC_FREEZE         );
	add_sc( NC_COLDSLOWER       , SC_FROST          );
	set_sc( NC_ACCELERATION		, SC_ACCELERATION	, SI_ACCELERATION	, SCB_SPEED );
	set_sc( NC_HOVERING			, SC_HOVERING		, SI_HOVERING		, SCB_SPEED );
	set_sc( NC_SHAPESHIFT		, SC_SHAPESHIFT     , SI_SHAPESHIFT     , SCB_DEF_ELE );
	set_sc( NC_INFRAREDSCAN		, SC_INFRAREDSCAN	, SI_INFRAREDSCAN	, SCB_FLEE );
	set_sc( NC_ANALYZE			, SC_ANALYZE		, SI_ANALYZE		, SCB_DEF|SCB_DEF2|SCB_MDEF|SCB_MDEF2 );
	set_sc( NC_MAGNETICFIELD	, SC_MAGNETICFIELD	, SI_MAGNETICFIELD	, SCB_NONE );
	set_sc( NC_NEUTRALBARRIER	, SC_NEUTRALBARRIER	, SI_NEUTRALBARRIER	, SCB_DEF | SCB_MDEF);
	set_sc( NC_STEALTHFIELD     , SC_STEALTHFIELD	, SI_STEALTHFIELD	, SCB_NONE );

	// Shadow Chaser
	set_sc( SC_REPRODUCE		, SC__REPRODUCE         , SI_REPRODUCE      , SCB_NONE );
	set_sc( SC_AUTOSHADOWSPELL	, SC__AUTOSHADOWSPELL   , SI_AUTOSHADOWSPELL, SCB_NONE );
	set_sc( SC_SHADOWFORM		, SC__SHADOWFORM		, SI_SHADOWFORM		, SCB_NONE );
	set_sc( SC_BODYPAINT		, SC__BODYPAINT			, SI_BODYPAINT		, SCB_ASPD );
	set_sc( SC_INVISIBILITY		, SC__INVISIBILITY		, SI_INVISIBILITY	, SCB_CRI | SCB_ASPD | SCB_ATK_ELE);
	set_sc( SC_DEADLYINFECT		, SC__DEADLYINFECT		, SI_DEADLYINFECT	, SCB_NONE);
	set_sc( SC_ENERVATION		, SC__ENERVATION		, SI_ENERVATION		, SCB_BATK | SCB_WATK);
	set_sc( SC_GROOMY			, SC__GROOMY			, SI_GROOMY			, SCB_HIT | SCB_ASPD);
	set_sc( SC_IGNORANCE		, SC__IGNORANCE			, SI_IGNORANCE		, SCB_NONE );
	set_sc( SC_LAZINESS			, SC__LAZINESS			, SI_LAZINESS		, SCB_FLEE | SCB_SPEED);
	set_sc( SC_UNLUCKY			, SC__UNLUCKY			, SI_UNLUCKY		, SCB_CRI|SCB_FLEE2 );
	set_sc( SC_WEAKNESS			, SC__WEAKNESS			, SI_WEAKNESS		, SCB_MAXHP);
	set_sc( SC_STRIPACCESSARY	, SC__STRIPACCESSARY	, SI_STRIPACCESSARY	, SCB_INT | SCB_DEX | SCB_LUK);
	set_sc_with_vfx( SC_MANHOLE			, SC__MANHOLE			, SI_MANHOLE		, SCB_NONE );
	add_sc( SC_CONFUSIONPANIC	, SC_CONFUSION			);
	// Old Bloody Lust data is left here in case old behavior is wanted.
	//set_sc( SC_BLOODYLUST		, SC__BLOODYLUST		, SI_BLOODYLUST		, SCB_DEF|SCB_DEF2|SCB_BATK|SCB_WATK );
	set_sc( SC_BLOODYLUST		, SC__BLOODYLUST_BK		, SI_BLOODYLUST		, SCB_NONE );

	// Royal Guard
	set_sc( LG_REFLECTDAMAGE	, SC_LG_REFLECTDAMAGE	, SI_LG_REFLECTDAMAGE	, SCB_NONE );
	set_sc( LG_FORCEOFVANGUARD  , SC_FORCEOFVANGUARD    , SI_FORCEOFVANGUARD    , SCB_MAXHP | SCB_DEF);
	set_sc( LG_EXEEDBREAK       , SC_EXEEDBREAK			, SI_EXEEDBREAK			, SCB_NONE );
	set_sc( LG_PRESTIGE			, SC_PRESTIGE			, SI_PRESTIGE			, SCB_DEF );
	set_sc( LG_BANDING			, SC_BANDING_DEFENCE	, SI_BANDING_DEFENCE	, SCB_SPEED);
	set_sc( LG_PIETY			, SC_BENEDICTIO			, SI_BENEDICTIO			, SCB_DEF_ELE );
	set_sc( LG_EARTHDRIVE       , SC_EARTHDRIVE			, SI_EARTHDRIVE			, SCB_DEF|SCB_ASPD );
	set_sc( LG_INSPIRATION		, SC_INSPIRATION		, SI_INSPIRATION		, SCB_STR | SCB_AGI | SCB_VIT | SCB_INT | SCB_DEX | SCB_LUK | SCB_WATK | SCB_HIT | SCB_MAXHP );

	// Sura
	add_sc( SR_DRAGONCOMBO				, SC_STUN					);
	add_sc( SR_EARTHSHAKER				, SC_STUN					);
	set_sc( SR_FALLENEMPIRE				, SC_STOP					, SI_FALLENEMPIRE			, SCB_NONE );
	set_sc( SR_CRESCENTELBOW			, SC_CRESCENTELBOW			, SI_CRESCENTELBOW			, SCB_NONE );
	set_sc( SR_CURSEDCIRCLE				, SC_CURSEDCIRCLE_TARGET	, SI_CURSEDCIRCLE_TARGET	, SCB_NONE );
	set_sc( SR_LIGHTNINGWALK			, SC_LIGHTNINGWALK			, SI_LIGHTNINGWALK			, SCB_NONE );
	set_sc( SR_RAISINGDRAGON			, SC_RAISINGDRAGON			, SI_RAISINGDRAGON			, SCB_REGEN | SCB_MAXHP | SCB_MAXSP );
	set_sc( SR_GENTLETOUCH_ENERGYGAIN	, SC_GENTLETOUCH_ENERGYGAIN	, SI_GENTLETOUCH_ENERGYGAIN	, SCB_NONE );
	set_sc( SR_GENTLETOUCH_CHANGE		, SC_GENTLETOUCH_CHANGE		, SI_GENTLETOUCH_CHANGE		, SCB_WATK|SCB_MDEF|SCB_ASPD|SCB_MAXHP );
	set_sc( SR_GENTLETOUCH_REVITALIZE	, SC_GENTLETOUCH_REVITALIZE	, SI_GENTLETOUCH_REVITALIZE	, SCB_MAXHP | SCB_DEF2 | SCB_REGEN );
	add_sc( SR_HOWLINGOFLION			, SC_FEAR					);

	// Minstrel/Wanderer
	set_sc( WA_SWING_DANCE				, SC_SWING					, SI_SWING					, SCB_SPEED|SCB_ASPD );
	set_sc( WA_SYMPHONY_OF_LOVER		, SC_SYMPHONY_LOVE			, SI_SYMPHONY_LOVE			, SCB_MDEF );
	set_sc( WA_MOONLIT_SERENADE			, SC_MOONLIT_SERENADE		, SI_MOONLIT_SERENADE		, SCB_MATK );
	set_sc( MI_RUSH_WINDMILL			, SC_RUSH_WINDMILL			, SI_RUSH_WINDMILL			, SCB_WATK );
	set_sc( MI_ECHOSONG					, SC_ECHOSONG				, SI_ECHOSONG				, SCB_DEF );
	set_sc( MI_HARMONIZE				, SC_HARMONIZE				, SI_HARMONIZE				, SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK );
	set_sc( WM_POEMOFNETHERWORLD		, SC_NETHERWORLD			, SI_NETHERWORLD			, SCB_NONE );
	set_sc( WM_VOICEOFSIREN				, SC_SIREN					, SI_SIREN					, SCB_NONE );
	add_sc( WM_LULLABY_DEEPSLEEP		, SC_DEEPSLEEP				);
	set_sc( WM_SIRCLEOFNATURE			, SC_SIRCLEOFNATURE			, SI_SIRCLEOFNATURE			, SCB_NONE );
	set_sc( WM_GLOOMYDAY				, SC_GLOOMYDAY				, SI_GLOOMYDAY				, SCB_FLEE | SCB_SPEED | SCB_ASPD);
	set_sc( WM_SONG_OF_MANA				, SC_SONG_OF_MANA			, SI_SONG_OF_MANA			, SCB_NONE );
	set_sc( WM_DANCE_WITH_WUG			, SC_DANCE_WITH_WUG			, SI_DANCE_WITH_WUG			, SCB_WATK | SCB_ASPD);
	set_sc( WM_SATURDAY_NIGHT_FEVER		, SC_SATURDAY_NIGHT_FEVER	, SI_SATURDAY_NIGHT_FEVER	, SCB_WATK | SCB_FLEE | SCB_DEF | SCB_REGEN );
	set_sc( WM_LERADS_DEW				, SC_LERADS_DEW				, SI_LERADS_DEW				, SCB_MAXHP );
	set_sc( WM_MELODYOFSINK				, SC_MELODYOFSINK			, SI_MELODYOFSINK			, SCB_INT );
	set_sc( WM_BEYOND_OF_WARCRY			, SC_BEYOND_OF_WARCRY		, SI_BEYOND_OF_WARCRY		, SCB_STR | SCB_CRI | SCB_MAXHP);
	set_sc( WM_UNLIMITED_HUMMING_VOICE	, SC_UNLIMITED_HUMMING_VOICE, SI_UNLIMITED_HUMMING_VOICE, SCB_NONE );

	// Sorcerer
	set_sc( SO_FIREWALK         , SC_PROPERTYWALK	, SI_PROPERTYWALK	, SCB_NONE );
	set_sc( SO_ELECTRICWALK     , SC_PROPERTYWALK	, SI_PROPERTYWALK	, SCB_NONE );
	set_sc( SO_SPELLFIST        , SC_SPELLFIST      , SI_SPELLFIST      , SCB_NONE );
	add_sc( SO_DIAMONDDUST		, SC_CRYSTALIZE		);
	set_sc( SO_CLOUD_KILL       , SC_POISON			, SI_CLOUD_KILL		, SCB_NONE );
	set_sc( SO_STRIKING         , SC_STRIKING       , SI_STRIKING       , SCB_WATK|SCB_CRI );
	set_sc( SO_WARMER           , SC_WARMER         , SI_WARMER         , SCB_NONE );
	set_sc( SO_VACUUM_EXTREME	, SC_VACUUM_EXTREME	, SI_VACUUM_EXTREME	, SCB_NONE );
	add_sc( SO_ARRULLO			, SC_DEEPSLEEP		);
	set_sc( SO_FIRE_INSIGNIA    , SC_FIRE_INSIGNIA  , SI_FIRE_INSIGNIA  , SCB_BATK|SCB_WATK|SCB_MATK|SCB_ATK_ELE );
	set_sc( SO_WATER_INSIGNIA   , SC_WATER_INSIGNIA , SI_WATER_INSIGNIA , SCB_BATK|SCB_WATK|SCB_ATK_ELE );
	set_sc( SO_WIND_INSIGNIA    , SC_WIND_INSIGNIA  , SI_WIND_INSIGNIA  , SCB_BATK|SCB_WATK|SCB_ASPD|SCB_ATK_ELE );
	set_sc( SO_EARTH_INSIGNIA   , SC_EARTH_INSIGNIA , SI_EARTH_INSIGNIA , SCB_MAXHP|SCB_MAXSP|SCB_BATK|SCB_WATK|SCB_DEF|SCB_MDEF|SCB_ATK_ELE );

	// Genetic
	set_sc( GN_CARTBOOST					, SC_GN_CARTBOOST				, SI_GN_CARTBOOST				, SCB_SPEED );
	set_sc( GN_THORNS_TRAP					, SC_THORNS_TRAP					, SI_THORNS_TRAP				, SCB_NONE );
	set_sc( GN_BLOOD_SUCKER					, SC_BLOOD_SUCKER				, SI_BLOOD_SUCKER				, SCB_NONE );
	set_sc( GN_FIRE_EXPANSION_SMOKE_POWDER	, SC_FIRE_EXPANSION_SMOKE_POWDER, SI_FIRE_EXPANSION_SMOKE_POWDER, SCB_FLEE );
	set_sc( GN_FIRE_EXPANSION_TEAR_GAS		, SC_FIRE_EXPANSION_TEAR_GAS	, SI_FIRE_EXPANSION_TEAR_GAS    , SCB_HIT|SCB_FLEE );
	set_sc( GN_MANDRAGORA					, SC_MANDRAGORA					, SI_MANDRAGORA					, SCB_INT );

	set_sc(ALL_ODINS_POWER, SC_ODINS_POWER, SI_ODINS_POWER, SCB_WATK | SCB_MATK | SCB_DEF | SCB_MDEF);

	// Rebellion
	add_sc( RL_MASS_SPIRAL	, SC_BLEEDING			);
	set_sc( RL_B_TRAP		, SC_B_TRAP				, SI_B_TRAP				, SCB_SPEED );
	set_sc( RL_E_CHAIN		, SC_E_CHAIN			, SI_E_CHAIN			, SCB_NONE );
	//set_sc( RL_E_CHAIN	, SC_E_QD_SHOT_READY	, SI_E_QD_SHOT_READY	, SCB_NONE );
	set_sc( RL_C_MARKER		, SC_C_MARKER			, SI_C_MARKER			, SCB_FLEE );
	set_sc( RL_H_MINE		, SC_H_MINE				, SI_H_MINE				, SCB_NONE );
	//set_sc( RL_H_MINE		, SC_H_MINE_SPLASH		, SI_H_MINE_SPLASH		, SCB_NONE );
	set_sc( RL_P_ALTER		, SC_P_ALTER			, SI_P_ALTER			, SCB_WATK );
	set_sc( RL_FALLEN_ANGEL	, SC_FALLEN_ANGEL		, SI_FALLEN_ANGEL		, SCB_NONE );
	set_sc( RL_HEAT_BARREL	, SC_HEAT_BARREL		, SI_HEAT_BARREL		, SCB_HIT|SCB_ASPD );
	//set_sc( RL_HEAT_BARREL, SC_HEAT_BARREL_AFTER	, SI_HEAT_BARREL_AFTER	, SCB_NONE );
	set_sc( RL_AM_BLAST		, SC_ANTI_M_BLAST		, SI_ANTI_M_BLAST		, SCB_NONE );
	set_sc( RL_SLUGSHOT		, SC_SLUGSHOT			, SI_SLUGSHOT			, SCB_NONE );
	add_sc( RL_HAMMER_OF_GOD, SC_STUN );

	// Star Emperor
	set_sc( SJ_LIGHTOFMOON		, SC_LIGHTOFMOON	, SI_LIGHTOFMOON	, SCB_NONE );
	set_sc( SJ_LUNARSTANCE		, SC_LUNARSTANCE	, SI_LUNARSTANCE	, SCB_MAXHP );
	add_sc( SJ_FULLMOONKICK		, SC_BLIND			);
	set_sc( SJ_LIGHTOFSTAR		, SC_LIGHTOFSTAR	, SI_LIGHTOFSTAR	, SCB_NONE );
	set_sc( SJ_STARSTANCE		, SC_STARSTANCE		, SI_STARSTANCE		, SCB_ASPD );
	set_sc( SJ_NEWMOONKICK		, SC_NEWMOON		, SI_NEWMOON		, SCB_NONE);
	set_sc( SJ_FLASHKICK		, SC_FLASHKICK		, SI_FLASHKICK		, SCB_NONE );
	add_sc( SJ_STAREMPEROR		, SC_SILENCE		);
	set_sc( SJ_NOVAEXPLOSING	, SC_NOVAEXPLOSING	, SI_NOVAEXPLOSING	, SCB_NONE );
	set_sc( SJ_UNIVERSESTANCE	, SC_UNIVERSESTANCE	, SI_UNIVERSESTANCE	, SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK );
	set_sc( SJ_FALLINGSTAR		, SC_FALLINGSTAR	, SI_FALLINGSTAR	, SCB_NONE );
	set_sc( SJ_GRAVITYCONTROL	, SC_GRAVITYCONTROL	, SI_GRAVITYCONTROL	, SCB_NONE );
	set_sc( SJ_BOOKOFDIMENSION	, SC_DIMENSION		, SI_DIMENSION		, SCB_NONE );
	set_sc( SJ_BOOKOFCREATINGSTAR, SC_CREATINGSTAR	, SI_CREATINGSTAR	, SCB_SPEED );
	set_sc( SJ_LIGHTOFSUN		, SC_LIGHTOFSUN		, SI_LIGHTOFSUN		, SCB_NONE );
	set_sc( SJ_SUNSTANCE		, SC_SUNSTANCE		, SI_SUNSTANCE		, SCB_BATK|SCB_WATK );

	// Soul Reaper
	set_sc( SP_SOULGOLEM   , SC_SOULGOLEM   , SI_SOULGOLEM   , SCB_DEF|SCB_MDEF );
	set_sc( SP_SOULSHADOW  , SC_SOULSHADOW  , SI_SOULSHADOW  , SCB_ASPD|SCB_CRI );
	set_sc( SP_SOULFALCON  , SC_SOULFALCON  , SI_SOULFALCON  , SCB_WATK|SCB_HIT );
	set_sc( SP_SOULFAIRY   , SC_SOULFAIRY   , SI_SOULFAIRY   , SCB_MATK );
	add_sc( SP_SOULCURSE   , SC_CURSE );
	set_sc( SP_SHA         , SC_SP_SHA      , SI_SP_SHA      , SCB_SPEED );
	set_sc( SP_SOULUNITY   , SC_SOULUNITY   , SI_SOULUNITY   , SCB_NONE );
	set_sc( SP_SOULDIVISION, SC_SOULDIVISION, SI_SOULDIVISION, SCB_NONE );
	set_sc( SP_SOULREAPER  , SC_SOULREAPER  , SI_SOULREAPER  , SCB_NONE );
	set_sc( SP_SOULCOLLECT , SC_SOULCOLLECT , SI_SOULCOLLECT , SCB_NONE );

	add_sc( KO_YAMIKUMO		, SC_HIDING				);
	set_sc( KO_JYUMONJIKIRI	, SC_KO_JYUMONJIKIRI	, SI_KO_JYUMONJIKIRI	, SCB_NONE );
	set_sc( KO_MEIKYOUSISUI	, SC_MEIKYOUSISUI		, SI_MEIKYOUSISUI		, SCB_NONE );
	set_sc( KO_KYOUGAKU		, SC_KYOUGAKU			, SI_KYOUGAKU			, SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK );
	set_sc( KO_ZENKAI		, SC_ZENKAI				, SI_ZENKAI				, SCB_WATK );
	set_sc( KO_IZAYOI		, SC_IZAYOI				, SI_IZAYOI				, SCB_MATK );
	set_sc( KG_KAGEHUMI		, SC_KG_KAGEHUMI		, SI_KG_KAGEHUMI		, SCB_NONE );
	set_sc( KG_KYOMU		, SC_KYOMU				, SI_KYOMU				, SCB_NONE );
	set_sc( KG_KAGEMUSYA	, SC_KAGEMUSYA			, SI_KAGEMUSYA			, SCB_NONE );
	set_sc( OB_ZANGETSU		, SC_ZANGETSU			, SI_ZANGETSU			, SCB_WATK | SCB_MATK );
	set_sc( OB_OBOROGENSOU	, SC_GENSOU				, SI_GENSOU				, SCB_NONE );
	set_sc( OB_AKAITSUKI	, SC_AKAITSUKI			, SI_AKAITSUKI			, SCB_NONE );

	// EP 14.3 Part 2 3rd Job Skills
	set_sc(GC_DARKCROW					, SC_DARKCROW			, SI_DARKCROW			, SCB_NONE);
	set_sc(RA_UNLIMIT					, SC_UNLIMIT			, SI_UNLIMIT			, SCB_DEF|SCB_DEF2|SCB_MDEF|SCB_MDEF2);
	set_sc(GN_ILLUSIONDOPING			, SC_ILLUSIONDOPING		, SI_ILLUSIONDOPING		, SCB_HIT);
	add_sc(RK_DRAGONBREATH_WATER		, SC_FROST				);
	add_sc(NC_MAGMA_ERUPTION			, SC_STUN				);
	set_sc(WM_FRIGG_SONG				, SC_FRIGG_SONG			, SI_FRIGG_SONG			, SCB_MAXHP);
	set_sc(SR_FLASHCOMBO				, SC_FLASHCOMBO         , SI_FLASHCOMBO         , SCB_WATK);
	add_sc(SC_ESCAPE					, SC_ANKLE				);
	set_sc(AB_OFFERTORIUM				, SC_OFFERTORIUM		, SI_OFFERTORIUM		, SCB_NONE);
	set_sc(WL_TELEKINESIS_INTENSE		, SC_TELEKINESIS_INTENSE, SI_TELEKINESIS_INTENSE, SCB_NONE);
	set_sc(LG_KINGS_GRACE				, SC_KINGS_GRACE		, SI_KINGS_GRACE		, SCB_NONE);
	set_sc(ALL_FULL_THROTTLE			, SC_FULL_THROTTLE		, SI_FULL_THROTTLE		, SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK|SCB_SPEED);
	add_sc(NC_MAGMA_ERUPTION_DOTDAMAGE	, SC_BURNING			);

	set_sc(SU_HIDE				, SC_SUHIDE			, SI_SUHIDE			, SCB_SPEED);
	add_sc(SU_SCRATCH			, SC_BLEEDING		);
	set_sc(SU_STOOP				, SC_SU_STOOP		, SI_SU_STOOP		, SCB_NONE);
	add_sc(SU_SV_STEMSPEAR		, SC_BLEEDING		);
	set_sc(SU_SV_ROOTTWIST		, SC_SV_ROOTTWIST	, SI_SV_ROOTTWIST	, SCB_NONE);
	set_sc(SU_CN_POWDERING		, SC_CATNIPPOWDER	, SI_CATNIPPOWDER	, SCB_BATK | SCB_WATK | SCB_MATK | SCB_REGEN);
	set_sc(SU_SCAROFTAROU		, SC_BITESCAR		, SI_BITESCAR		, SCB_NONE);
	set_sc(SU_ARCLOUSEDASH		, SC_ARCLOUSEDASH	, SI_ARCLOUSEDASH	, SCB_AGI | SCB_SPEED);
	set_sc(SU_TUNAPARTY			, SC_TUNAPARTY		, SI_TUNAPARTY		, SCB_NONE);
	set_sc(SU_BUNCHOFSHRIMP		, SC_SHRIMP			, SI_SHRIMP			, SCB_BATK | SCB_WATK | SCB_MATK);
	set_sc(SU_FRESHSHRIMP		, SC_FRESHSHRIMP	, SI_FRESHSHRIMP	, SCB_NONE);
	add_sc(SU_CN_METEOR2		, SC_CURSE);
	add_sc(SU_LUNATICCARROTBEAT2, SC_STUN		);
	set_sc(SU_NYANGGRASS		, SC_NYANGGRASS		, SI_NYANGGRASS		, SCB_DEF | SCB_MDEF);
	add_sc(SU_GROOMING			, SC_GROOMING	);

	set_sc(WE_CHEERUP, SC_CHEERUP, SI_CHEERUP, SCB_STR | SCB_AGI | SCB_VIT | SCB_INT | SCB_DEX | SCB_LUK);

	add_sc(AB_VITUPERATUM, SC_AETERNA);

	set_sc(NV_HELPANGEL, SC_HELPANGEL, SI_HELPANGEL, SCB_NONE);

	set_sc( HLIF_AVOID           , SC_AVOID           , SI_BLANK           , SCB_SPEED );
	set_sc( HLIF_CHANGE          , SC_CHANGE          , SI_BLANK           , SCB_VIT|SCB_INT );
	set_sc( HFLI_FLEET           , SC_FLEET           , SI_BLANK           , SCB_ASPD|SCB_BATK|SCB_WATK );
	set_sc( HFLI_SPEED           , SC_SPEED           , SI_BLANK           , SCB_FLEE );
	set_sc( HAMI_DEFENCE         , SC_DEFENCE         , SI_BLANK           , SCB_DEF );
	set_sc( HAMI_BLOODLUST       , SC_BLOODLUST       , SI_BLANK           , SCB_BATK|SCB_WATK );

	// Mutanted Homunculus
	set_sc( MH_NEEDLE_OF_PARALYZE	, SC_NEEDLE_OF_PARALYZE	, SI_NEEDLE_OF_PARALYZE	, SCB_DEF | SCB_MDEF);
	add_sc( MH_POISON_MIST			, SC_BLIND				);
	set_sc( MH_PAIN_KILLER			, SC_PAIN_KILLER		, SI_PAIN_KILLER		, SCB_ASPD);
	set_sc( MH_LIGHT_OF_REGENE		, SC_LIGHT_OF_REGENE	, SI_LIGHT_OF_REGENE	, SCB_NONE );
	set_sc( MH_OVERED_BOOST			, SC_OVERED_BOOST		, SI_OVERED_BOOST		, SCB_FLEE | SCB_DEF | SCB_ASPD);
	add_sc( MH_XENO_SLASHER			, SC_BLEEDING			);
	set_sc( MH_SILENT_BREEZE		, SC_SILENT_BREEZE		, SI_SILENT_BREEZE		, SCB_NONE );
	set_sc( MH_STYLE_CHANGE			, SC_STYLE_CHANGE		, SI_STYLE_CHANGE		, SCB_NONE );
	add_sc( MH_SILVERVEIN_RUSH		, SC_STUN				);
	add_sc( MH_MIDNIGHT_FRENZY		, SC_FEAR				);
	set_sc( MH_TINDER_BREAKER		, SC_TINDER_BREAKER		, SI_TINDER_BREAKER		, SCB_FLEE);
	set_sc( MH_CBC					, SC_CBC				, SI_CBC				, SCB_NONE);
	set_sc( MH_EQC					, SC_EQC				, SI_EQC				, SCB_MAXHP | SCB_BATK | SCB_WATK | SCB_DEF | SCB_DEF2);
	add_sc( MH_STAHL_HORN			, SC_STUN				);
	set_sc( MH_GOLDENE_FERSE		, SC_GOLDENE_FERSE		, SI_GOLDENE_FERSE		, SCB_FLEE | SCB_ASPD);
	add_sc( MH_STEINWAND			, SC_SAFETYWALL			);
	set_sc( MH_ANGRIFFS_MODUS		, SC_ANGRIFFS_MODUS, SI_ANGRIFFS_MODUS, SCB_MAXHP | SCB_BATK | SCB_WATK | SCB_FLEE | SCB_DEF);
	set_sc( MH_MAGMA_FLOW			, SC_MAGMA_FLOW			, SI_MAGMA_FLOW			, SCB_NONE );
	set_sc( MH_GRANITIC_ARMOR		, SC_GRANITIC_ARMOR		, SI_GRANITIC_ARMOR		, SCB_NONE );
	add_sc( MH_LAVA_SLIDE			, SC_BURNING			);
	set_sc( MH_PYROCLASTIC			, SC_PYROCLASTIC		, SI_PYROCLASTIC		, SCB_WATK | SCB_ATK_ELE);
	set_sc( MH_VOLCANIC_ASH			, SC_VOLCANIC_ASH		, SI_VOLCANIC_ASH		, SCB_BATK | SCB_WATK | SCB_HIT | SCB_FLEE | SCB_DEF);

	add_sc( MER_CRASH            , SC_STUN            );
	set_sc( MER_PROVOKE          , SC_PROVOKE         , SI_PROVOKE         , SCB_DEF|SCB_DEF2|SCB_BATK|SCB_WATK );
	add_sc( MS_MAGNUM            , SC_WATK_ELEMENT    );
	add_sc( MER_SIGHT            , SC_SIGHT           );
	set_sc( MER_DECAGI           , SC_DECREASEAGI     , SI_DECREASEAGI     , SCB_AGI|SCB_SPEED );
	set_sc( MER_MAGNIFICAT       , SC_MAGNIFICAT      , SI_MAGNIFICAT      , SCB_REGEN );
	add_sc( MER_LEXDIVINA        , SC_SILENCE         );
	add_sc( MA_LANDMINE          , SC_STUN            );
	add_sc( MA_SANDMAN           , SC_SLEEP           );
	add_sc( MA_FREEZINGTRAP      , SC_FREEZE          );
	set_sc( MER_AUTOBERSERK      , SC_AUTOBERSERK     , SI_AUTOBERSERK     , SCB_NONE );
	set_sc( ML_AUTOGUARD         , SC_AUTOGUARD       , SI_AUTOGUARD       , SCB_NONE );
	set_sc( MS_REFLECTSHIELD     , SC_REFLECTSHIELD   , SI_REFLECTSHIELD   , SCB_NONE );
	set_sc( ML_DEFENDER          , SC_DEFENDER        , SI_DEFENDER        , SCB_SPEED|SCB_ASPD );
	set_sc( MS_PARRYING          , SC_PARRYING        , SI_PARRYING        , SCB_NONE );
	set_sc( MS_BERSERK           , SC_BERSERK         , SI_BERSERK         , SCB_DEF|SCB_DEF2|SCB_MDEF|SCB_MDEF2|SCB_FLEE|SCB_SPEED|SCB_ASPD|SCB_MAXHP|SCB_REGEN );
	add_sc( ML_SPIRALPIERCE      , SC_STOP            );
	set_sc( MER_QUICKEN          , SC_MERC_QUICKEN    , SI_BLANK           , SCB_ASPD );
	add_sc( ML_DEVOTION          , SC_DEVOTION        );
	set_sc( MER_KYRIE            , SC_KYRIE           , SI_KYRIE           , SCB_NONE );
	set_sc( MER_BLESSING         , SC_BLESSING        , SI_BLESSING        , SCB_STR|SCB_INT|SCB_DEX );
	set_sc( MER_INCAGI           , SC_INCREASEAGI     , SI_INCREASEAGI     , SCB_AGI|SCB_SPEED );

	// Elementals
	set_sc( EL_CIRCLE_OF_FIRE    , SC_CIRCLE_OF_FIRE  , SI_CIRCLE_OF_FIRE  , SCB_NONE );
	set_sc( EL_FIRE_CLOAK        , SC_FIRE_CLOAK      , SI_FIRE_CLOAK      , SCB_DEF_ELE );
	add_sc( EL_FIRE_MANTLE       , SC_BURNING         );
	set_sc( EL_WATER_SCREEN      , SC_WATER_SCREEN    , SI_WATER_SCREEN    , SCB_NONE );
	set_sc( EL_WATER_DROP        , SC_WATER_DROP      , SI_WATER_DROP      , SCB_DEF_ELE );
	set_sc( EL_WATER_BARRIER     , SC_WATER_BARRIER   , SI_WATER_BARRIER   , SCB_BATK|SCB_WATK|SCB_FLEE|SCB_DEF|SCB_MDEF );
	set_sc( EL_WIND_STEP         , SC_WIND_STEP       , SI_WIND_STEP       , SCB_NONE );
	set_sc( EL_WIND_CURTAIN      , SC_WIND_CURTAIN    , SI_WIND_CURTAIN    , SCB_DEF_ELE );
	set_sc( EL_ZEPHYR            , SC_ZEPHYR          , SI_ZEPHYR          , SCB_FLEE );
	set_sc( EL_SOLID_SKIN        , SC_SOLID_SKIN      , SI_SOLID_SKIN      , SCB_NONE );
	set_sc( EL_STONE_SHIELD      , SC_STONE_SHIELD    , SI_STONE_SHIELD    , SCB_DEF_ELE );
	set_sc( EL_POWER_OF_GAIA     , SC_POWER_OF_GAIA   , SI_POWER_OF_GAIA   , SCB_MAXHP|SCB_DEF|SCB_SPEED );
	set_sc( EL_PYROTECHNIC       , SC_PYROTECHNIC     , SI_PYROTECHNIC     , SCB_NONE );
	set_sc( EL_HEATER            , SC_HEATER          , SI_HEATER          , SCB_NONE );
	set_sc( EL_TROPIC            , SC_TROPIC          , SI_TROPIC          , SCB_NONE );
	set_sc( EL_AQUAPLAY          , SC_AQUAPLAY        , SI_AQUAPLAY        , SCB_NONE );
	set_sc( EL_COOLER            , SC_COOLER          , SI_COOLER          , SCB_NONE );
	set_sc( EL_CHILLY_AIR        , SC_CHILLY_AIR      , SI_CHILLY_AIR      , SCB_NONE );
	set_sc( EL_GUST              , SC_GUST            , SI_GUST            , SCB_NONE );
	set_sc( EL_BLAST             , SC_BLAST           , SI_BLAST           , SCB_NONE );
	set_sc( EL_WILD_STORM        , SC_WILD_STORM      , SI_WILD_STORM      , SCB_NONE );
	set_sc( EL_PETROLOGY         , SC_PETROLOGY       , SI_PETROLOGY       , SCB_NONE );
	set_sc( EL_CURSED_SOIL       , SC_CURSED_SOIL     , SI_CURSED_SOIL     , SCB_NONE );
	set_sc( EL_UPHEAVAL          , SC_UPHEAVAL        , SI_UPHEAVAL        , SCB_NONE );
	set_sc( EL_TIDAL_WEAPON      , SC_TIDAL_WEAPON    , SI_TIDAL_WEAPON    , SCB_WATK|SCB_ATK_ELE );
	add_sc( EL_WIND_SLASH        , SC_BLEEDING        );
	add_sc( EL_TYPOON_MIS_ATK    , SC_SILENCE         );
	add_sc( EL_STONE_HAMMER      , SC_STUN            );
	set_sc( EL_ROCK_CRUSHER      , SC_ROCK_CRUSHER    , SI_ROCK_CRUSHER    , SCB_DEF );
	set_sc( EL_ROCK_CRUSHER_ATK  , SC_ROCK_CRUSHER_ATK, SI_ROCK_CRUSHER_ATK, SCB_SPEED );
	add_sc( EL_STONE_RAIN        , SC_STUN            );

	// Guild Skills
	set_sc( GD_LEADERSHIP	, SC_LEADERSHIP		, SI_BLANK					, SCB_STR );
	set_sc( GD_GLORYWOUNDS	, SC_GLORYWOUNDS	, SI_BLANK					, SCB_VIT );
	set_sc( GD_SOULCOLD		, SC_SOULCOLD		, SI_BLANK					, SCB_AGI );
	set_sc( GD_HAWKEYES		, SC_HAWKEYES		, SI_BLANK					, SCB_DEX );
	set_sc( GD_BATTLEORDER	, SC_BATTLEORDERS	, SI_GDSKILL_BATTLEORDER	, SCB_STR | SCB_INT | SCB_DEX);
	set_sc( GD_REGENERATION	, SC_REGENERATION	, SI_GDSKILL_REGENERATION	, SCB_REGEN);

	// Storing the target job rather than simply SC_SPIRIT simplifies code later on.
	SkillStatusChangeTable[SL_ALCHEMIST]	= (sc_type)MAPID_ALCHEMIST,
	SkillStatusChangeTable[SL_MONK]			= (sc_type)MAPID_MONK,
	SkillStatusChangeTable[SL_STAR]			= (sc_type)MAPID_STAR_GLADIATOR,
	SkillStatusChangeTable[SL_SAGE]			= (sc_type)MAPID_SAGE,
	SkillStatusChangeTable[SL_CRUSADER]		= (sc_type)MAPID_CRUSADER,
	SkillStatusChangeTable[SL_SUPERNOVICE]	= (sc_type)MAPID_SUPER_NOVICE,
	SkillStatusChangeTable[SL_KNIGHT]		= (sc_type)MAPID_KNIGHT,
	SkillStatusChangeTable[SL_WIZARD]		= (sc_type)MAPID_WIZARD,
	SkillStatusChangeTable[SL_PRIEST]		= (sc_type)MAPID_PRIEST,
	SkillStatusChangeTable[SL_BARDDANCER]	= (sc_type)MAPID_BARDDANCER,
	SkillStatusChangeTable[SL_ROGUE]		= (sc_type)MAPID_ROGUE,
	SkillStatusChangeTable[SL_ASSASIN]		= (sc_type)MAPID_ASSASSIN,
	SkillStatusChangeTable[SL_BLACKSMITH]	= (sc_type)MAPID_BLACKSMITH,
	SkillStatusChangeTable[SL_HUNTER]		= (sc_type)MAPID_HUNTER,
	SkillStatusChangeTable[SL_SOULLINKER]	= (sc_type)MAPID_SOUL_LINKER,
	SkillStatusChangeTable[SL_DEATHKNIGHT]	= (sc_type)MAPID_DEATH_KNIGHT,
	SkillStatusChangeTable[SL_COLLECTOR]	= (sc_type)MAPID_DARK_COLLECTOR,
	SkillStatusChangeTable[SL_NINJA]		= (sc_type)MAPID_NINJA,
	SkillStatusChangeTable[SL_GUNNER]		= (sc_type)MAPID_GUNSLINGER,

	//Status that don't have a skill associated.
	StatusIconChangeTable[SC_WEIGHT50] = SI_WEIGHT50;
	StatusIconChangeTable[SC_WEIGHT90] = SI_WEIGHT90;
	StatusIconChangeTable[SC_ASPDPOTION0] = SI_ASPDPOTION0;
	StatusIconChangeTable[SC_ASPDPOTION1] = SI_ASPDPOTION1;
	StatusIconChangeTable[SC_ASPDPOTION2] = SI_ASPDPOTION2;
	StatusIconChangeTable[SC_ASPDPOTION3] = SI_ASPDPOTIONINFINITY;
	StatusIconChangeTable[SC_SPEEDUP0] = SI_MOVHASTE_HORSE;
	StatusIconChangeTable[SC_SPEEDUP1] = SI_SPEEDPOTION1;
	StatusIconChangeTable[SC_COMBO] = SI_COMBOATTACK;
	StatusIconChangeTable[SC_INCSTR] = SI_INCSTR;
	StatusIconChangeTable[SC_MIRACLE] = SI_SPIRIT;
	StatusIconChangeTable[SC_INTRAVISION] = SI_INTRAVISION;
	StatusIconChangeTable[SC_STRFOOD] = SI_FOODSTR;
	StatusIconChangeTable[SC_AGIFOOD] = SI_FOODAGI;
	StatusIconChangeTable[SC_VITFOOD] = SI_FOODVIT;
	StatusIconChangeTable[SC_INTFOOD] = SI_FOODINT;
	StatusIconChangeTable[SC_DEXFOOD] = SI_FOODDEX;
	StatusIconChangeTable[SC_LUKFOOD] = SI_FOODLUK;
	StatusIconChangeTable[SC_FLEEFOOD]= SI_FOODFLEE;
	StatusIconChangeTable[SC_HITFOOD] = SI_FOODHIT;
	StatusIconChangeTable[SC_CRIFOOD] = SI_FOODCRI;
	StatusIconChangeTable[SC_MANU_ATK] = SI_MANU_ATK;
	StatusIconChangeTable[SC_MANU_DEF] = SI_MANU_DEF;
	StatusIconChangeTable[SC_SPL_ATK] = SI_SPL_ATK;
	StatusIconChangeTable[SC_SPL_DEF] = SI_SPL_DEF;
	StatusIconChangeTable[SC_MANU_MATK] = SI_MANU_MATK;
	StatusIconChangeTable[SC_SPL_MATK] = SI_SPL_MATK;
	StatusIconChangeTable[SC_ATKPOTION] = SI_PLUSATTACKPOWER;
	StatusIconChangeTable[SC_MATKPOTION] = SI_PLUSMAGICPOWER;
	//Cash Items
	StatusIconChangeTable[SC_FOOD_STR_CASH] = SI_FOOD_STR_CASH;
	StatusIconChangeTable[SC_FOOD_AGI_CASH] = SI_FOOD_AGI_CASH;
	StatusIconChangeTable[SC_FOOD_VIT_CASH] = SI_FOOD_VIT_CASH;
	StatusIconChangeTable[SC_FOOD_DEX_CASH] = SI_FOOD_DEX_CASH;
	StatusIconChangeTable[SC_FOOD_INT_CASH] = SI_FOOD_INT_CASH;
	StatusIconChangeTable[SC_FOOD_LUK_CASH] = SI_FOOD_LUK_CASH;
	StatusIconChangeTable[SC_EXPBOOST] = SI_EXPBOOST;
	StatusIconChangeTable[SC_ITEMBOOST] = SI_ITEMBOOST;
	StatusIconChangeTable[SC_JEXPBOOST] = SI_CASH_PLUSONLYJOBEXP;
	StatusIconChangeTable[SC_LIFEINSURANCE] = SI_LIFEINSURANCE;
	StatusIconChangeTable[SC_BOSSMAPINFO] = SI_BOSSMAPINFO;
	StatusIconChangeTable[SC_DEF_RATE] = SI_DEF_RATE;
	StatusIconChangeTable[SC_MDEF_RATE] = SI_MDEF_RATE;
	StatusIconChangeTable[SC_INCCRI] = SI_INCCRI;
	StatusIconChangeTable[SC_INCFLEE2] = SI_PLUSAVOIDVALUE;
	StatusIconChangeTable[SC_INCHEALRATE] = SI_INCHEALRATE;
	StatusIconChangeTable[SC_S_LIFEPOTION] = SI_S_LIFEPOTION;
	StatusIconChangeTable[SC_L_LIFEPOTION] = SI_L_LIFEPOTION;
	StatusIconChangeTable[SC_SPCOST_RATE] = SI_ATKER_BLOOD;
	StatusIconChangeTable[SC_COMMONSC_RESIST] = SI_TARGET_BLOOD;
	StatusIconChangeTable[SC_MOVESLOW_POTION] = SI_MOVESLOW_POTION;
	StatusIconChangeTable[SC_ATTHASTE_CASH] = SI_ATTHASTE_CASH;
	// Mercenary Bonus Effects
	StatusIconChangeTable[SC_MERC_FLEEUP] = SI_MERC_FLEEUP;
	StatusIconChangeTable[SC_MERC_ATKUP] = SI_MERC_ATKUP;
	StatusIconChangeTable[SC_MERC_HPUP] = SI_MERC_HPUP;
	StatusIconChangeTable[SC_MERC_SPUP] = SI_MERC_SPUP;
	StatusIconChangeTable[SC_MERC_HITUP] = SI_MERC_HITUP;

	// Elementals
	StatusIconChangeTable[SC_EL_WAIT] = SI_EL_WAIT;
	StatusIconChangeTable[SC_EL_PASSIVE] = SI_EL_PASSIVE;
	StatusIconChangeTable[SC_EL_DEFENSIVE] = SI_EL_DEFENSIVE;
	StatusIconChangeTable[SC_EL_OFFENSIVE] = SI_EL_OFFENSIVE;
	StatusIconChangeTable[SC_EL_COST] = SI_EL_COST;

	StatusIconChangeTable[SC_CIRCLE_OF_FIRE_OPTION] = SI_CIRCLE_OF_FIRE_OPTION;
	StatusIconChangeTable[SC_FIRE_CLOAK_OPTION] = SI_FIRE_CLOAK_OPTION;
	StatusIconChangeTable[SC_WATER_SCREEN_OPTION] = SI_WATER_SCREEN_OPTION;
	StatusIconChangeTable[SC_WATER_DROP_OPTION] = SI_WATER_DROP_OPTION;
	StatusIconChangeTable[SC_WIND_STEP_OPTION] = SI_WIND_STEP_OPTION;
	StatusIconChangeTable[SC_WIND_CURTAIN_OPTION] = SI_WIND_CURTAIN_OPTION;
	StatusIconChangeTable[SC_SOLID_SKIN_OPTION] = SI_SOLID_SKIN_OPTION;
	StatusIconChangeTable[SC_STONE_SHIELD_OPTION] = SI_STONE_SHIELD_OPTION;
	StatusIconChangeTable[SC_PYROTECHNIC_OPTION] = SI_PYROTECHNIC_OPTION;
	StatusIconChangeTable[SC_HEATER_OPTION] = SI_HEATER_OPTION;
	StatusIconChangeTable[SC_TROPIC_OPTION] = SI_TROPIC_OPTION;
	StatusIconChangeTable[SC_AQUAPLAY_OPTION] = SI_AQUAPLAY_OPTION;
	StatusIconChangeTable[SC_COOLER_OPTION] = SI_COOLER_OPTION;
	StatusIconChangeTable[SC_CHILLY_AIR_OPTION] = SI_CHILLY_AIR_OPTION;
	StatusIconChangeTable[SC_GUST_OPTION] = SI_GUST_OPTION;
	StatusIconChangeTable[SC_BLAST_OPTION] = SI_BLAST_OPTION;
	StatusIconChangeTable[SC_WILD_STORM_OPTION] = SI_WILD_STORM_OPTION;
	StatusIconChangeTable[SC_PETROLOGY_OPTION] = SI_PETROLOGY_OPTION;
	StatusIconChangeTable[SC_CURSED_SOIL_OPTION] = SI_CURSED_SOIL_OPTION;
	StatusIconChangeTable[SC_UPHEAVAL_OPTION] = SI_UPHEAVAL_OPTION;
	StatusIconChangeTable[SC_TIDAL_WEAPON_OPTION] = SC_TIDAL_WEAPON_OPTION;

	// Rental Mounts, Push Carts, and Transformation Scrolls
	StatusIconChangeTable[SC_ALL_RIDING] = SI_ALL_RIDING;
	StatusIconChangeTable[SC_ON_PUSH_CART] = SI_ON_PUSH_CART;
	StatusIconChangeTable[SC_MONSTER_TRANSFORM] = SI_MONSTER_TRANSFORM;
	StatusIconChangeTable[SC_ACTIVE_MONSTER_TRANSFORM] = SI_ACTIVE_MONSTER_TRANSFORM;
	StatusIconChangeTable[SC_MTF_ASPD] = SI_MTF_ASPD;
	StatusIconChangeTable[SC_MTF_RANGEATK] = SI_MTF_RANGEATK;
	StatusIconChangeTable[SC_MTF_MATK] = SI_MTF_MATK;
	StatusIconChangeTable[SC_MTF_MLEATKED] = SI_MTF_MLEATKED;
	StatusIconChangeTable[SC_MTF_CRIDAMAGE] = SI_MTF_CRIDAMAGE;
	StatusIconChangeTable[SC_MTF_ASPD2] = SI_MTF_ASPD2;
	StatusIconChangeTable[SC_MTF_RANGEATK2] = SI_MTF_RANGEATK2;
	StatusIconChangeTable[SC_MTF_MATK2] = SI_MTF_MATK2;
	StatusIconChangeTable[SC_MTF_HITFLEE] = SI_MTF_HITFLEE;
	StatusIconChangeTable[SC_MTF_MHP] = SI_MTF_MHP;
	StatusIconChangeTable[SC_MTF_MSP] = SI_MTF_MSP;
	StatusIconChangeTable[SC_MTF_PUMPKIN] = SI_MTF_PUMPKIN;

	// Headgears with special animations through status
	StatusIconChangeTable[SC_MOONSTAR] = SI_MOONSTAR;
	StatusIconChangeTable[SC_STRANGELIGHTS] = SI_STRANGELIGHTS;
	StatusIconChangeTable[SC_SUPER_STAR] = SI_SUPER_STAR;
	StatusIconChangeTable[SC_DECORATION_OF_MUSIC] = SI_DECORATION_OF_MUSIC;
	StatusIconChangeTable[SC_LJOSALFAR] = SI_LJOSALFAR;
	StatusIconChangeTable[SC_MERMAID_LONGING] = SI_MERMAID_LONGING;
	StatusIconChangeTable[SC_HAT_EFFECT] = SI_HAT_EFFECT;
	StatusIconChangeTable[SC_FLOWERSMOKE] = SI_FLOWERSMOKE;
	StatusIconChangeTable[SC_FSTONE] = SI_FSTONE;
	StatusIconChangeTable[SC_HAPPINESS_STAR] = SI_HAPPINESS_STAR;
	StatusIconChangeTable[SC_MAPLE_FALLS] = SI_MAPLE_FALLS;
	StatusIconChangeTable[SC_TIME_ACCESSORY] = SI_TIME_ACCESSORY;
	StatusIconChangeTable[SC_MAGICAL_FEATHER] = SI_MAGICAL_FEATHER;

	// Elemental Resist Potions
	StatusIconChangeTable[SC_ARMOR_ELEMENT_WATER] = SI_RESIST_PROPERTY_WATER;
	StatusIconChangeTable[SC_ARMOR_ELEMENT_EARTH] = SI_RESIST_PROPERTY_GROUND;
	StatusIconChangeTable[SC_ARMOR_ELEMENT_FIRE] = SI_RESIST_PROPERTY_FIRE;
	StatusIconChangeTable[SC_ARMOR_ELEMENT_WIND] = SI_RESIST_PROPERTY_WIND;

	// Mechanic status change icons
	StatusIconChangeTable[SC_NEUTRALBARRIER_MASTER] = SI_NEUTRALBARRIER_MASTER;
	StatusIconChangeTable[SC_STEALTHFIELD_MASTER] = SI_STEALTHFIELD_MASTER;
	StatusIconChangeTable[SC_OVERHEAT_LIMITPOINT] = SI_OVERHEAT_LIMITPOINT;
	StatusIconChangeTable[SC_OVERHEAT] = SI_OVERHEAT;

	// Guillotine Cross status change icons
	StatusIconChangeTable[SC_TOXIN] = SI_TOXIN;
	StatusIconChangeTable[SC_PARALYSE] = SI_PARALYSE;
	StatusIconChangeTable[SC_VENOMBLEED] = SI_VENOMBLEED;
	StatusIconChangeTable[SC_MAGICMUSHROOM] = SI_MAGICMUSHROOM;
	StatusIconChangeTable[SC_DEATHHURT] = SI_DEATHHURT;
	StatusIconChangeTable[SC_PYREXIA] = SI_PYREXIA;
	StatusIconChangeTable[SC_OBLIVIONCURSE] = SI_OBLIVIONCURSE;
	StatusIconChangeTable[SC_LEECHESEND] = SI_LEECHESEND;
	StatusIconChangeTable[SC_HALLUCINATIONWALK_POSTDELAY] = SI_HALLUCINATIONWALK_POSTDELAY;

	// Royal Guard status change icons
	StatusIconChangeTable[SC_SHIELDSPELL_DEF] = SI_SHIELDSPELL_DEF;
	StatusIconChangeTable[SC_SHIELDSPELL_MDEF] = SI_SHIELDSPELL_MDEF;
	StatusIconChangeTable[SC_SHIELDSPELL_REF] = SI_SHIELDSPELL_REF;
	StatusIconChangeTable[SC_BANDING] = SI_BANDING;

	// Sura status change icons
	StatusIconChangeTable[SC_CURSEDCIRCLE_ATKER] = SI_CURSEDCIRCLE_ATKER;
	
	// Genetic & Genetics New Food Items status change icons
	StatusIconChangeTable[SC_MELON_BOMB] = SI_MELON_BOMB;
	StatusIconChangeTable[SC_BANANA_BOMB] = SI_BANANA_BOMB;
	StatusIconChangeTable[SC_BANANA_BOMB_SITDOWN_POSTDELAY] = SI_BANANA_BOMB_SITDOWN_POSTDELAY;
	StatusIconChangeTable[SC_MYSTERIOUS_POWDER] = SI_MYSTERIOUS_POWDER;
	StatusIconChangeTable[SC_PROMOTE_HEALTH_RESERCH] = SI_PROMOTE_HEALTH_RESERCH;
	StatusIconChangeTable[SC_ENERGY_DRINK_RESERCH] = SI_ENERGY_DRINK_RESERCH;
	StatusIconChangeTable[SC_EXTRACT_WHITE_POTION_Z] = SI_EXTRACT_WHITE_POTION_Z;
	StatusIconChangeTable[SC_SAVAGE_STEAK] = SI_SAVAGE_STEAK;
	StatusIconChangeTable[SC_COCKTAIL_WARG_BLOOD] = SI_COCKTAIL_WARG_BLOOD;
	StatusIconChangeTable[SC_MINOR_BBQ] = SI_MINOR_BBQ;
	StatusIconChangeTable[SC_SIROMA_ICE_TEA] = SI_SIROMA_ICE_TEA;
	StatusIconChangeTable[SC_DROCERA_HERB_STEAMED] = SI_DROCERA_HERB_STEAMED;
	StatusIconChangeTable[SC_PUTTI_TAILS_NOODLES] = SI_PUTTI_TAILS_NOODLES;
	StatusIconChangeTable[SC_STOMACHACHE] = SI_STOMACHACHE;
	StatusIconChangeTable[SC_VITATA_500] = SI_VITATA_500;
	StatusIconChangeTable[SC_EXTRACT_SALAMINE_JUICE] = SI_EXTRACT_SALAMINE_JUICE;
	StatusIconChangeTable[SC_BOOST500] = SI_BOOST500;
	StatusIconChangeTable[SC_FULL_SWING_K] = SI_FULL_SWING_K;
	StatusIconChangeTable[SC_MANA_PLUS] = SI_MANA_PLUS;
	StatusIconChangeTable[SC_MUSTLE_M] |= SI_MUSTLE_M;
	StatusIconChangeTable[SC_LIFE_FORCE_F] = SI_LIFE_FORCE_F;

	// Warlock Spheres
	StatusIconChangeTable[SC_SUMMON1] = SI_SUMMON1;
	StatusIconChangeTable[SC_SUMMON2] = SI_SUMMON2;
	StatusIconChangeTable[SC_SUMMON3] = SI_SUMMON3;
	StatusIconChangeTable[SC_SUMMON4] = SI_SUMMON4;
	StatusIconChangeTable[SC_SUMMON5] = SI_SUMMON5;

	StatusIconChangeTable[SC_REBOUND] = SI_REBOUND;
	StatusIconChangeTable[SC_H_MINE_SPLASH] = SI_H_MINE_SPLASH;
	StatusIconChangeTable[SC_HEAT_BARREL_AFTER] = SI_HEAT_BARREL_AFTER;
	StatusIconChangeTable[SC_ANCILLA] = SI_ANCILLA;

	// Star Emperor / Soul Reaper
	StatusIconChangeTable[SC_USE_SKILL_SP_SPA] = SI_USE_SKILL_SP_SPA;
	StatusIconChangeTable[SC_USE_SKILL_SP_SHA] = SI_USE_SKILL_SP_SHA;

	// Summoner
	StatusIconChangeTable[SC_SPRITEMABLE] = SI_SPRITEMABLE;
	StatusIconChangeTable[SC_SOULATTACK] = SI_SOULATTACK;
	StatusIconChangeTable[SC_DORAM_BUF_01] = SI_DORAM_BUF_01;
	StatusIconChangeTable[SC_DORAM_BUF_02] = SI_DORAM_BUF_02;

	// Mutanted Homunculus
	StatusIconChangeTable[SC_SONIC_CLAW_POSTDELAY] = SI_SONIC_CLAW_POSTDELAY;
	StatusIconChangeTable[SC_SILVERVEIN_RUSH_POSTDELAY] = SI_SILVERVEIN_RUSH_POSTDELAY;
	StatusIconChangeTable[SC_MIDNIGHT_FRENZY_POSTDELAY] = SI_MIDNIGHT_FRENZY_POSTDELAY;
	StatusIconChangeTable[SC_TINDER_BREAKER_POSTDELAY] = SI_TINDER_BREAKER_POSTDELAY;
	StatusIconChangeTable[SC_CBC_POSTDELAY] = SI_CBC_POSTDELAY;

	// Clan System
	StatusIconChangeTable[SC_CLAN_INFO] = SI_CLAN_INFO;
	StatusIconChangeTable[SC_SWORDCLAN] = SI_SWORDCLAN;
	StatusIconChangeTable[SC_ARCWANDCLAN] = SI_ARCWANDCLAN;
	StatusIconChangeTable[SC_GOLDENMACECLAN] = SI_GOLDENMACECLAN;
	StatusIconChangeTable[SC_CROSSBOWCLAN] = SI_CROSSBOWCLAN;
	StatusIconChangeTable[SC_JUMPINGCLAN] = SI_JUMPINGCLAN;

	// RODEX
	StatusIconChangeTable[SC_DAILYSENDMAILCNT] = SI_DAILYSENDMAILCNT;

	// Item Reuse Limits
	StatusIconChangeTable[SC_REUSE_REFRESH] = SI_REUSE_REFRESH;
	StatusIconChangeTable[SC_REUSE_LIMIT_A] = SI_REUSE_LIMIT_A;
	StatusIconChangeTable[SC_REUSE_LIMIT_B] = SI_REUSE_LIMIT_B;
	StatusIconChangeTable[SC_REUSE_LIMIT_C] = SI_REUSE_LIMIT_C;
	StatusIconChangeTable[SC_REUSE_LIMIT_D] = SI_REUSE_LIMIT_D;
	StatusIconChangeTable[SC_REUSE_LIMIT_E] = SI_REUSE_LIMIT_E;
	StatusIconChangeTable[SC_REUSE_LIMIT_F] = SI_REUSE_LIMIT_F;
	StatusIconChangeTable[SC_REUSE_LIMIT_G] = SI_REUSE_LIMIT_G;
	StatusIconChangeTable[SC_REUSE_LIMIT_H] = SI_REUSE_LIMIT_H;
	StatusIconChangeTable[SC_REUSE_LIMIT_MTF] = SI_REUSE_LIMIT_MTF;
	StatusIconChangeTable[SC_REUSE_LIMIT_ECL] = SI_REUSE_LIMIT_ECL;
	StatusIconChangeTable[SC_REUSE_LIMIT_RECALL] = SI_REUSE_LIMIT_RECALL;
	StatusIconChangeTable[SC_REUSE_LIMIT_ASPD_POTION] = SI_REUSE_LIMIT_ASPD_POTION;
	StatusIconChangeTable[SC_REUSE_MILLENNIUMSHIELD] = SI_MILLENNIUMSHIELD;
	StatusIconChangeTable[SC_REUSE_CRUSHSTRIKE] = SI_REUSE_CRUSHSTRIKE;
	StatusIconChangeTable[SC_REUSE_STORMBLAST] = SI_REUSE_STORMBLAST;
	StatusIconChangeTable[SC_ALL_RIDING_REUSE_LIMIT] = SI_ALL_RIDING_REUSE_LIMIT;

	// misc
	StatusIconChangeTable[SC_DEFSET] = SI_SET_NUM_DEF;
	StatusIconChangeTable[SC_MDEFSET] = SI_SET_NUM_MDEF;
	StatusIconChangeTable[SC_2011RWC_SCROLL] = SI_2011RWC_SCROLL;
	StatusIconChangeTable[SC_JP_EVENT04] = SI_JP_EVENT04;

	// Item Status Changes
	StatusIconChangeTable[SC_GVG_GIANT] = SI_GVG_GIANT;
	StatusIconChangeTable[SC_GVG_GOLEM] = SI_GVG_GOLEM;
	StatusIconChangeTable[SC_GVG_STUN] = SI_GVG_STUN;
	StatusIconChangeTable[SC_GVG_STONE] = SI_GVG_STONE;
	StatusIconChangeTable[SC_GVG_FREEZ] = SI_GVG_FREEZ;
	StatusIconChangeTable[SC_GVG_SLEEP] = SI_GVG_SLEEP;
	StatusIconChangeTable[SC_GVG_CURSE] = SI_GVG_CURSE;
	StatusIconChangeTable[SC_GVG_SILENCE] = SI_GVG_SILENCE;
	StatusIconChangeTable[SC_GVG_BLIND] = SI_GVG_BLIND;

	//Other SC which are not necessarily associated to skills.
	StatusChangeFlagTable[SC_ASPDPOTION0] |= SCB_ASPD;
	StatusChangeFlagTable[SC_ASPDPOTION1] |= SCB_ASPD;
	StatusChangeFlagTable[SC_ASPDPOTION2] |= SCB_ASPD;
	StatusChangeFlagTable[SC_ASPDPOTION3] |= SCB_ASPD;
	StatusChangeFlagTable[SC_SPEEDUP0] |= SCB_SPEED;
	StatusChangeFlagTable[SC_SPEEDUP1] |= SCB_SPEED;
	StatusChangeFlagTable[SC_MOVESLOW_POTION] |= SCB_SPEED;
	StatusChangeFlagTable[SC_ATKPOTION] |= SCB_BATK;
	StatusChangeFlagTable[SC_MATKPOTION] |= SCB_MATK;
	StatusChangeFlagTable[SC_INCALLSTATUS] |= SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK;
	StatusChangeFlagTable[SC_INCSTR] |= SCB_STR;
	StatusChangeFlagTable[SC_INCAGI] |= SCB_AGI;
	StatusChangeFlagTable[SC_INCVIT] |= SCB_VIT;
	StatusChangeFlagTable[SC_INCINT] |= SCB_INT;
	StatusChangeFlagTable[SC_INCDEX] |= SCB_DEX;
	StatusChangeFlagTable[SC_INCLUK] |= SCB_LUK;
	StatusChangeFlagTable[SC_INCHIT] |= SCB_HIT;
	StatusChangeFlagTable[SC_INCHITRATE] |= SCB_HIT;
	StatusChangeFlagTable[SC_INCFLEE] |= SCB_FLEE;
	StatusChangeFlagTable[SC_INCFLEERATE] |= SCB_FLEE;
	StatusChangeFlagTable[SC_INCCRI] |= SCB_CRI;
	StatusChangeFlagTable[SC_INCFLEE2] |= SCB_FLEE2;
	StatusChangeFlagTable[SC_INCMHPRATE] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_INCMSPRATE] |= SCB_MAXSP;
	StatusChangeFlagTable[SC_INCATKRATE] |= SCB_BATK|SCB_WATK;
	StatusChangeFlagTable[SC_INCMATKRATE] |= SCB_MATK;
	StatusChangeFlagTable[SC_INCDEFRATE] |= SCB_DEF;
	StatusChangeFlagTable[SC_STRFOOD] |= SCB_STR;
	StatusChangeFlagTable[SC_AGIFOOD] |= SCB_AGI;
	StatusChangeFlagTable[SC_VITFOOD] |= SCB_VIT;
	StatusChangeFlagTable[SC_INTFOOD] |= SCB_INT;
	StatusChangeFlagTable[SC_DEXFOOD] |= SCB_DEX;
	StatusChangeFlagTable[SC_LUKFOOD] |= SCB_LUK;
	StatusChangeFlagTable[SC_HITFOOD] |= SCB_HIT;
	StatusChangeFlagTable[SC_CRIFOOD] |= SCB_CRI;
	StatusChangeFlagTable[SC_FLEEFOOD] |= SCB_FLEE;
	StatusChangeFlagTable[SC_BATKFOOD] |= SCB_BATK;
	StatusChangeFlagTable[SC_WATKFOOD] |= SCB_WATK;
	StatusChangeFlagTable[SC_MATKFOOD] |= SCB_MATK;
	StatusChangeFlagTable[SC_ARMOR_ELEMENT_WATER] |= SCB_ALL;
	StatusChangeFlagTable[SC_ARMOR_ELEMENT_EARTH] |= SCB_ALL;
	StatusChangeFlagTable[SC_ARMOR_ELEMENT_FIRE] |= SCB_ALL;
	StatusChangeFlagTable[SC_ARMOR_ELEMENT_WIND] |= SCB_ALL;
	StatusChangeFlagTable[SC_ARMOR_RESIST] |= SCB_ALL;
	StatusChangeFlagTable[SC_SPCOST_RATE] |= SCB_ALL;
	StatusChangeFlagTable[SC_WALKSPEED] |= SCB_SPEED;
	StatusChangeFlagTable[SC_ITEMSCRIPT] |= SCB_ALL;
	StatusChangeFlagTable[SC_SLOWDOWN] |= SCB_SPEED;
	// Cash Items
	StatusChangeFlagTable[SC_FOOD_STR_CASH] |= SCB_STR;
	StatusChangeFlagTable[SC_FOOD_AGI_CASH] |= SCB_AGI;
	StatusChangeFlagTable[SC_FOOD_VIT_CASH] |= SCB_VIT;
	StatusChangeFlagTable[SC_FOOD_DEX_CASH] |= SCB_DEX;
	StatusChangeFlagTable[SC_FOOD_INT_CASH] |= SCB_INT;
	StatusChangeFlagTable[SC_FOOD_LUK_CASH] |= SCB_LUK;
	StatusChangeFlagTable[SC_ATTHASTE_CASH] |= SCB_ASPD;
	// Mercenary Bonus Effects
	StatusChangeFlagTable[SC_MERC_FLEEUP] |= SCB_FLEE;
	StatusChangeFlagTable[SC_MERC_ATKUP] |= SCB_WATK;
	StatusChangeFlagTable[SC_MERC_HPUP] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_MERC_SPUP] |= SCB_MAXSP;
	StatusChangeFlagTable[SC_MERC_HITUP] |= SCB_HIT;

	// Rental Mounts, Push Carts, and Transformation Scrolls
	StatusChangeFlagTable[SC_ALL_RIDING] |= SCB_SPEED;
	StatusChangeFlagTable[SC_ON_PUSH_CART] |= SCB_SPEED;
	StatusChangeFlagTable[SC_REBOUND] |= SCB_SPEED|SCB_REGEN;//Recheck later.
	StatusChangeFlagTable[SC_MONSTER_TRANSFORM] |= SCB_NONE;

	// Headgears with special animations through status.
	StatusChangeFlagTable[SC_MOONSTAR] |= SCB_NONE;
	StatusChangeFlagTable[SC_STRANGELIGHTS] |= SCB_NONE;
	StatusChangeFlagTable[SC_SUPER_STAR] |= SCB_NONE;
	StatusChangeFlagTable[SC_DECORATION_OF_MUSIC] |= SCB_NONE;
	StatusChangeFlagTable[SC_LJOSALFAR] |= SCB_NONE;
	StatusChangeFlagTable[SC_MERMAID_LONGING] |= SCB_NONE;
	StatusChangeFlagTable[SC_HAT_EFFECT] |= SCB_NONE;
	StatusChangeFlagTable[SC_FLOWERSMOKE] |= SCB_NONE;
	StatusChangeFlagTable[SC_FSTONE] |= SCB_NONE;
	StatusChangeFlagTable[SC_HAPPINESS_STAR] |= SCB_NONE;
	StatusChangeFlagTable[SC_MAPLE_FALLS] |= SCB_NONE;
	StatusChangeFlagTable[SC_TIME_ACCESSORY] |= SCB_NONE;
	StatusChangeFlagTable[SC_MAGICAL_FEATHER] |= SCB_NONE;

	// Mechanic
	StatusChangeFlagTable[SC_STEALTHFIELD_MASTER] |= SCB_SPEED;

	// Guillotine Cross
	StatusChangeFlagTable[SC_PARALYSE] |= SCB_FLEE | SCB_SPEED|SCB_ASPD;
	StatusChangeFlagTable[SC_OBLIVIONCURSE] |= SCB_REGEN;
	StatusChangeFlagTable[SC_DEATHHURT] |= SCB_REGEN;
	StatusChangeFlagTable[SC_PYREXIA] |= SCB_HIT | SCB_FLEE;
	StatusChangeFlagTable[SC_MAGICMUSHROOM] |= SCB_REGEN;
	StatusChangeFlagTable[SC_VENOMBLEED] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_HALLUCINATIONWALK_POSTDELAY] |= SCB_ASPD|SCB_SPEED;

	// Royal Guard
	StatusChangeFlagTable[SC_SHIELDSPELL_DEF] |= SCB_WATK;
	StatusChangeFlagTable[SC_SHIELDSPELL_REF] |= SCB_DEF;
	StatusChangeFlagTable[SC_BANDING] |= SCB_WATK | SCB_DEF | SCB_REGEN;

	// Genetic & Genetics New Food Items
	StatusChangeFlagTable[SC_MELON_BOMB] |= SCB_SPEED|SCB_ASPD;
	StatusChangeFlagTable[SC_BANANA_BOMB] |= SCB_LUK;
	StatusChangeFlagTable[SC_MYSTERIOUS_POWDER] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_BOOST500] |= SCB_ASPD;
	StatusChangeFlagTable[SC_FULL_SWING_K] |= SCB_WATK;
	StatusChangeFlagTable[SC_MANA_PLUS] |= SCB_MATK;
	StatusChangeFlagTable[SC_MUSTLE_M] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_LIFE_FORCE_F] |= SCB_MAXSP;
	StatusChangeFlagTable[SC_PROMOTE_HEALTH_RESERCH] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_ENERGY_DRINK_RESERCH] |= SCB_MAXSP;
	StatusChangeFlagTable[SC_EXTRACT_WHITE_POTION_Z] |= SCB_REGEN;
	StatusChangeFlagTable[SC_VITATA_500] |= SCB_MAXSP | SCB_REGEN;
	StatusChangeFlagTable[SC_SAVAGE_STEAK] |= SCB_STR;
	StatusChangeFlagTable[SC_COCKTAIL_WARG_BLOOD] |= SCB_INT;
	StatusChangeFlagTable[SC_MINOR_BBQ] |= SCB_VIT;
	StatusChangeFlagTable[SC_SIROMA_ICE_TEA] |= SCB_DEX;
	StatusChangeFlagTable[SC_DROCERA_HERB_STEAMED] |= SCB_AGI;
	StatusChangeFlagTable[SC_PUTTI_TAILS_NOODLES] |= SCB_LUK;
	StatusChangeFlagTable[SC_STOMACHACHE] |= SCB_STR | SCB_AGI | SCB_VIT | SCB_INT | SCB_DEX | SCB_LUK;
	StatusChangeFlagTable[SC_EXTRACT_SALAMINE_JUICE] |= SCB_ASPD;

	// Summoner
	StatusChangeFlagTable[SC_SPIRITOFLAND_SPEED] = SCB_SPEED;
	StatusChangeFlagTable[SC_SPIRITOFLAND_MATK] = SCB_MATK;
	StatusChangeFlagTable[SC_SPIRITOFLAND_PERFECTDODGE] = SCB_FLEE2;
	StatusChangeFlagTable[SC_DORAM_BUF_01] |= SCB_REGEN;
	StatusChangeFlagTable[SC_DORAM_BUF_02] |= SCB_REGEN;

	//StatusChangeFlagTable[SC_CIRCLE_OF_FIRE_OPTION] |= SCB_NONE;
	StatusChangeFlagTable[SC_FIRE_CLOAK_OPTION] |= SCB_ALL;
	//StatusChangeFlagTable[SC_WATER_SCREEN_OPTION] |= SCB_NONE;
	StatusChangeFlagTable[SC_WATER_DROP_OPTION] |= SCB_ALL;
	StatusChangeFlagTable[SC_WIND_STEP_OPTION] |= SCB_FLEE | SCB_SPEED;
	StatusChangeFlagTable[SC_WIND_CURTAIN_OPTION] |= SCB_ALL;
	StatusChangeFlagTable[SC_SOLID_SKIN_OPTION] |= SCB_MAXHP | SCB_DEF;
	StatusChangeFlagTable[SC_STONE_SHIELD_OPTION] |= SCB_ALL;
	StatusChangeFlagTable[SC_PYROTECHNIC_OPTION] |= SCB_WATK;
	StatusChangeFlagTable[SC_HEATER_OPTION] |= SCB_WATK;
	StatusChangeFlagTable[SC_TROPIC_OPTION] |= SCB_WATK;
	StatusChangeFlagTable[SC_AQUAPLAY_OPTION] |= SCB_MATK;
	StatusChangeFlagTable[SC_COOLER_OPTION] |= SCB_MATK;
	StatusChangeFlagTable[SC_CHILLY_AIR_OPTION] |= SCB_MATK;
	StatusChangeFlagTable[SC_GUST_OPTION] |= SCB_ASPD;
	StatusChangeFlagTable[SC_BLAST_OPTION] |= SCB_ASPD;
	StatusChangeFlagTable[SC_WILD_STORM_OPTION] |= SCB_ASPD;
	StatusChangeFlagTable[SC_PETROLOGY_OPTION] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_CURSED_SOIL_OPTION] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_UPHEAVAL_OPTION] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_TIDAL_WEAPON_OPTION] |= SCB_ATK_ELE;

	// Clan System
	StatusChangeFlagTable[SC_CLAN_INFO] |= SCB_NONE;
	StatusChangeFlagTable[SC_SWORDCLAN] |= SCB_STR|SCB_VIT|SCB_MAXHP|SCB_MAXSP;
	StatusChangeFlagTable[SC_ARCWANDCLAN] |= SCB_INT|SCB_DEX|SCB_MAXHP|SCB_MAXSP;
	StatusChangeFlagTable[SC_GOLDENMACECLAN] |= SCB_LUK|SCB_INT|SCB_MAXHP|SCB_MAXSP;
	StatusChangeFlagTable[SC_CROSSBOWCLAN] |= SCB_DEX|SCB_AGI|SCB_MAXHP|SCB_MAXSP;
	StatusChangeFlagTable[SC_JUMPINGCLAN] |= SCB_STR|SCB_AGI|SCB_VIT|SCB_INT|SCB_DEX|SCB_LUK;

	// RODEX
	StatusChangeFlagTable[SC_DAILYSENDMAILCNT] |= SCB_NONE;

	// misc
	StatusChangeFlagTable[SC_DEFSET] |= SCB_DEF|SCB_DEF2;
	StatusChangeFlagTable[SC_MDEFSET] |= SCB_MDEF|SCB_MDEF2;

	// Monster Transformation
	StatusChangeFlagTable[SC_MTF_ASPD] = SCB_ASPD | SCB_HIT;
	StatusChangeFlagTable[SC_MTF_MATK] = SCB_MATK;
	StatusChangeFlagTable[SC_MTF_MLEATKED] |= SCB_ALL;
	StatusChangeFlagTable[SC_MTF_ASPD2] |= SCB_ASPD | SCB_HIT;
	StatusChangeFlagTable[SC_MTF_MATK2] |= SCB_MATK;
	StatusChangeFlagTable[SC_2011RWC_SCROLL] |= SCB_BATK | SCB_MATK | SCB_STR | SCB_AGI | SCB_VIT | SCB_INT | SCB_DEX | SCB_LUK;
	StatusChangeFlagTable[SC_MTF_HITFLEE] |= SCB_HIT | SCB_FLEE;
	StatusChangeFlagTable[SC_MTF_MHP] |= SCB_MAXHP;
	StatusChangeFlagTable[SC_MTF_MSP] |= SCB_MAXSP;

	/* StatusDisplayType Table [Ind] */
	StatusDisplayType[SC_ALL_RIDING]  = BL_PC;
	StatusDisplayType[SC_ON_PUSH_CART]  = BL_PC;
	StatusDisplayType[SC_CAMOUFLAGE]  = BL_PC;
	StatusDisplayType[SC_DUPLELIGHT]  = BL_PC;
	StatusDisplayType[SC_ORATIO]  = BL_PC;
	StatusDisplayType[SC_FROST]  = BL_PC;
	StatusDisplayType[SC_VENOMIMPRESS]  = BL_PC;
	StatusDisplayType[SC_HALLUCINATIONWALK]  = BL_PC;
	StatusDisplayType[SC_ROLLINGCUTTER]  = BL_PC;
	StatusDisplayType[SC_BANDING]  = BL_PC;
	StatusDisplayType[SC_CRYSTALIZE]  = BL_PC;
	StatusDisplayType[SC_DEEPSLEEP]  = BL_PC;
	StatusDisplayType[SC_CURSEDCIRCLE_ATKER]  = BL_PC;
	StatusDisplayType[SC_CURSEDCIRCLE_TARGET]  = BL_PC;
	StatusDisplayType[SC_NETHERWORLD]  = BL_PC;
	StatusDisplayType[SC_SIREN]  = BL_PC;
	StatusDisplayType[SC_BLOOD_SUCKER]  = BL_PC;
	StatusDisplayType[SC__SHADOWFORM]  = BL_PC;
	StatusDisplayType[SC__MANHOLE]  = BL_PC;
	StatusDisplayType[SC_KO_JYUMONJIKIRI]  = BL_PC;
	StatusDisplayType[SC_AKAITSUKI]  = BL_PC;
	StatusDisplayType[SC_MONSTER_TRANSFORM]  = BL_PC;
	StatusDisplayType[SC_DARKCROW]  = BL_PC;
	StatusDisplayType[SC_OFFERTORIUM]  = BL_PC;
	StatusDisplayType[SC_TELEKINESIS_INTENSE]  = BL_PC;
	StatusDisplayType[SC_UNLIMIT]  = BL_PC;
	StatusDisplayType[SC_ILLUSIONDOPING]  = BL_PC;
	StatusDisplayType[SC_C_MARKER]  = BL_PC;
	StatusDisplayType[SC_ANTI_M_BLAST]  = BL_PC;
	StatusDisplayType[SC_MOONSTAR]  = BL_PC;
	StatusDisplayType[SC_SUPER_STAR]  = BL_PC;
	StatusDisplayType[SC_STRANGELIGHTS]  = BL_PC;
	StatusDisplayType[SC_DECORATION_OF_MUSIC]  = BL_PC;
	StatusDisplayType[SC_LJOSALFAR]  = BL_PC;
	StatusDisplayType[SC_MERMAID_LONGING]  = BL_PC;
	StatusDisplayType[SC_HAT_EFFECT]  = BL_PC;
	StatusDisplayType[SC_FLOWERSMOKE]  = BL_PC;
	StatusDisplayType[SC_FSTONE]  = BL_PC;
	StatusDisplayType[SC_HAPPINESS_STAR]  = BL_PC;
	StatusDisplayType[SC_MAPLE_FALLS]  = BL_PC;
	StatusDisplayType[SC_TIME_ACCESSORY]  = BL_PC;
	StatusDisplayType[SC_MAGICAL_FEATHER]  = BL_PC;

	// Clans
	StatusDisplayType[SC_CLAN_INFO] = BL_PC | BL_NPC;
}

static void initDummyData(void)
{
	memset(&dummy_status, 0, sizeof(dummy_status));
	dummy_status.hp = 
	dummy_status.max_hp = 
	dummy_status.max_sp = 
	dummy_status.str =
	dummy_status.agi =
	dummy_status.vit =
	dummy_status.int_ =
	dummy_status.dex =
	dummy_status.luk =
	dummy_status.hit = 1;
	dummy_status.speed = 2000;
	dummy_status.adelay = 4000;
	dummy_status.amotion = 2000;
	dummy_status.dmotion = 2000;
	dummy_status.ele_lv = 1; //Min elemental level.
	dummy_status.mode = MD_CANMOVE;
}


//For copying a status_data structure from b to a, without overwriting current Hp and Sp
static inline void status_cpy(struct status_data* a, const struct status_data* b)
{
	memcpy((void*)&a->max_hp, (const void*)&b->max_hp, sizeof(struct status_data)-(sizeof(a->hp)+sizeof(a->sp)));
}


/*==========================================
 * 精錬ボーナス
 *------------------------------------------*/
int status_getrefinebonus(int lv,int type)
{
	if (lv >= 0 && lv < 5 && type >= 0 && type < 3)
		return refinebonus[lv][type];
	return 0;
}

//Sets HP to given value. Flag is the flag passed to status_heal in case
//final value is higher than current (use 2 to make a healing effect display 
//on players) It will always succeed (overrides Berserk block), but it can't kill.
int status_set_hp(struct block_list *bl, unsigned int hp, int flag)
{
	struct status_data *status;
	if (hp < 1) return 0;
	status = status_get_status_data(bl);
	if (status == &dummy_status)
		return 0;

	if (hp > status->max_hp) hp = status->max_hp;
	if (hp == status->hp) return 0;
	if (hp > status->hp)
		return status_heal(bl, hp - status->hp, 0, 1|flag);
	return status_zap(bl, status->hp - hp, 0);
}

/**
 * Sets Max HP to a given value
 * @param bl: Object whose Max HP will be set [PC|MOB|HOM|MER|ELEM|NPC]
 * @param maxhp: What the Max HP is to be set as
 * @param flag: Used in case final value is higher than current
 *		Use 2 to display healing effect
 * @return heal or zapped HP if valid
 */
int status_set_maxhp(struct block_list *bl, unsigned int maxhp, int flag)
{
	struct status_data *status;
	int64 heal;

	if (maxhp < 1)
		return 0;

	status = status_get_status_data(bl);

	if (status == &dummy_status)
		return 0;

	if (maxhp == status->max_hp)
		return 0;

	heal = maxhp - status->max_hp;
	status->max_hp = maxhp;

	if (heal > 0)
		status_heal(bl, heal, 0, 1 | flag);
	else
		status_zap(bl, -heal, 0);

	return maxhp;
}

//Sets SP to given value. Flag is the flag passed to status_heal in case
//final value is higher than current (use 2 to make a healing effect display 
//on players)
int status_set_sp(struct block_list *bl, unsigned int sp, int flag)
{
	struct status_data *status;

	status = status_get_status_data(bl);
	if (status == &dummy_status)
		return 0;

	if (sp > status->max_sp) sp = status->max_sp;
	if (sp == status->sp) return 0;
	if (sp > status->sp)
		return status_heal(bl, 0, sp - status->sp, 1|flag);
	return status_zap(bl, 0, status->sp - sp);
}

/**
 * Sets Max SP to a given value
 * @param bl: Object whose Max SP will be set [PC|HOM|MER|ELEM]
 * @param maxsp: What the Max SP is to be set as
 * @param flag: Used in case final value is higher than current
 *		Use 2 to display healing effect
 * @return heal or zapped HP if valid
 */
int status_set_maxsp(struct block_list *bl, unsigned int maxsp, int flag)
{
	struct status_data *status;

	if (maxsp < 1)
		return 0;

	status = status_get_status_data(bl);

	if (status == &dummy_status)
		return 0;

	if (maxsp == status->max_sp)
		return 0;

	if (maxsp > status->max_sp) {
		status_heal(bl, maxsp - status->max_sp, 0, 1 | flag);
	}
	else
		status_zap(bl, status->max_sp - maxsp, 0);

	status->max_sp = maxsp;

	return maxsp;
}

int64 status_charge(struct block_list* bl, int64 hp, int64 sp)
{
	if(!(bl->type&BL_CONSUME))
		return hp+sp; //Assume all was charged so there are no 'not enough' fails.
	return status_damage(NULL, bl, hp, sp, 0, 3);
}

//Inflicts damage on the target with the according walkdelay.
//If flag&1, damage is passive and does not triggers cancelling status changes.
//If flag&2, fail if target does not has enough to substract.
//If flag&4, if killed, mob must not give exp/loot.
//If flag&8, sp loss on dead target.
//If flag&16, cancel casting on damage
//If flag&32, damage redirected by ShadowForm.
int status_damage(struct block_list *src,struct block_list *target,int64 dhp, int64 dsp, int walkdelay, int flag)
{
	struct status_data *status;
	struct status_change *sc;

	int hp = (int)cap_value(dhp, INT_MIN, INT_MAX);
	int sp = (int)cap_value(dsp, INT_MIN, INT_MAX);

	nullpo_ret(target);

	if(sp && !(target->type&BL_CONSUME))
		sp = 0; //Not a valid SP target.

	if (hp < 0) { //Assume absorbed damage.
		status_heal(target, -hp, 0, 1);
		hp = 0;
	}

	if (sp < 0) {
		status_heal(target, 0, -sp, 1);
		sp = 0;
	}

	if (target->type == BL_SKILL) {
		if (!src || src->type&battle_config.can_damage_skill)
			return (int)skill_unit_ondamaged((struct skill_unit *)target, src, hp);
		return 0;
	}

	status = status_get_status_data(target);
	if(!status || status == &dummy_status)
		return 0;

	if ((unsigned int)hp >= status->hp) {
		if (flag&2) return 0;
		hp = status->hp;
	}

	if ((unsigned int)sp > status->sp) {
		if (flag&2) return 0;
		sp = status->sp;
	}

	if (!hp && !sp)
		return 0;

	if( !status->hp )
		flag |= 8;

// Let through. battle.c/skill.c have the whole logic of when it's possible or
// not to hurt someone (and this check breaks pet catching) [Skotlex]
//	if (!target->prev && !(flag&2))
//		return 0; //Cannot damage a bl not on a map, except when "charging" hp/sp

	sc = status_get_sc(target);
	if( hp && battle_config.invincible_nodamage && src && sc && sc->data[SC_INVINCIBLE] && !sc->data[SC_INVINCIBLEOFF] )
		hp = 1;

	if( hp && flag&32 ){
		if( sc && sc->data[SC_LG_REFLECTDAMAGE] ){
			int64 thp = (int64)hp;
			int64 rdamage = battle_calc_return_damage(src,target,&thp,BF_SHORT,0,1);
			hp = (int)cap_value(thp, INT32_MIN, INT32_MAX);
			if( src != target )
				map_foreachinrange(battle_damage_area,target,skill_get_splash(LG_REFLECTDAMAGE,1),BL_CHAR,gettick(),target,status_get_amotion(src),status_get_dmotion(src),rdamage,status->race);
		}
	}

	if( hp && !(flag&1) ) {
		if( sc ) {
			struct status_change_entry *sce;
			if (sc->data[SC_STONE] && sc->opt1 == OPT1_STONE)
				status_change_end(target, SC_STONE, INVALID_TIMER);
			status_change_end(target, SC_FREEZE, INVALID_TIMER);
			status_change_end(target, SC_SLEEP, INVALID_TIMER);
			status_change_end(target, SC_DEEPSLEEP, INVALID_TIMER);
			status_change_end(target, SC_WINKCHARM, INVALID_TIMER);
			status_change_end(target, SC_CONFUSION, INVALID_TIMER);
			status_change_end(target, SC_TRICKDEAD, INVALID_TIMER);
			status_change_end(target, SC_HIDING, INVALID_TIMER);
			status_change_end(target, SC_CLOAKING, INVALID_TIMER);
			status_change_end(target, SC_CHASEWALK, INVALID_TIMER);
			status_change_end(target, SC_CAMOUFLAGE, INVALID_TIMER);
			status_change_end(target, SC_SIREN,INVALID_TIMER);
			//status_change_end(target, SC_MAGNETICFIELD,INVALID_TIMER);// Skill description says it ends of you take damage.
			status_change_end(target, SC_NEWMOON, INVALID_TIMER);
			status_change_end(target, SC_SUHIDE, INVALID_TIMER);
			if ((sce=sc->data[SC_ENDURE]) && !sce->val4 && !sc->data[SC_CONCENTRATION]) {
				//Endure count is only reduced by non-players on non-gvg maps.
				//val4 signals infinite endure. [Skotlex]
				if (src && src->type != BL_PC && !map_flag_gvg2(target->m) && !map[target->m].flag.battleground && --(sce->val2) < 0)
					status_change_end(target, SC_ENDURE, INVALID_TIMER);
			}
			if ((sce=sc->data[SC_GRAVITATION]) && sce->val3 == BCT_SELF)
			{
				struct skill_unit_group* sg = skill_id2group(sce->val4);
				if (sg) {
					skill_delunitgroup(sg);
					sce->val4 = 0;
					status_change_end(target, SC_GRAVITATION, INVALID_TIMER);
				}
			}
			if(sc->data[SC_DANCING] && (unsigned int)hp > status->max_hp>>2)
				status_change_end(target, SC_DANCING, INVALID_TIMER);
			if (sc->data[SC_CLOAKINGEXCEED] && (--sc->data[SC_CLOAKINGEXCEED]->val2) <= 0)
				status_change_end(target, SC_CLOAKINGEXCEED, INVALID_TIMER);
		}
		if (target->type == BL_PC)
			pc_bonus_script_clear(BL_CAST(BL_PC,target),BSF_REM_ON_DAMAGED);
		unit_skillcastcancel(target,(flag&16)?0:2);
	}

	status->hp-= hp;
	status->sp-= sp;

	if (sc && hp && status->hp) {
		if (sc->data[SC_AUTOBERSERK] &&
			(!sc->data[SC_PROVOKE] || !sc->data[SC_PROVOKE]->val2) &&
			status->hp < status->max_hp>>2)
			sc_start4(target,SC_PROVOKE,100,10,1,0,0,0);
		if (sc->data[SC_BERSERK] && status->hp <= 100)
			status_change_end(target, SC_BERSERK, INVALID_TIMER);
		if( sc->data[SC_RAISINGDRAGON] && status->hp <= 1000 )
			status_change_end(target, SC_RAISINGDRAGON, INVALID_TIMER);
		if (sc->data[SC_SATURDAY_NIGHT_FEVER] && status->hp <= 100)
			status_change_end(target, SC_SATURDAY_NIGHT_FEVER, INVALID_TIMER);
	}

	switch (target->type)
	{
		case BL_PC:  pc_damage((TBL_PC*)target,src,hp,sp); break;
		case BL_MOB: mob_damage((TBL_MOB*)target, src, hp); break;
		case BL_HOM: hom_damage((TBL_HOM*)target); break;
		case BL_MER: mercenary_damage((TBL_MER*)target,hp,sp); break;
		case BL_ELEM: elemental_damage((TBL_ELEM*)target,src,hp,sp); break;
	}

	if (target->type == BL_PC && ((TBL_PC*)target)->disguise && src)
	{// stop walking when attacked in disguise to prevent walk-delay bug
		unit_stop_walking(target, 1);
	}

	if( status->hp || (flag&8) )
  	{	//Still lives or has been dead before this damage.
		if (walkdelay)
			unit_set_walkdelay(target, gettick(), walkdelay, 0);
		return hp+sp;
	}

	status->hp = 0;
	//NOTE: These dead functions should return: [Skotlex]
	//0: Death cancelled, auto-revived.
	//Non-zero: Standard death. Clear status, cancel move/attack, etc
	//&2: Also remove object from map.
	//&4: Also delete object from memory.
	switch (target->type) {
		case BL_PC:  flag = pc_dead((TBL_PC*)target,src); break;
		case BL_MOB: flag = mob_dead((TBL_MOB*)target, src, flag&4?3:0); break;
		case BL_HOM: flag = hom_dead((TBL_HOM*)target); break;
		case BL_MER: flag = mercenary_dead((TBL_MER*)target); break;
		case BL_ELEM: flag = elemental_dead((TBL_ELEM*)target); break;
		default:	//Unhandled case, do nothing to object.
			flag = 0;
			break;
	}

	if(!flag) //Death cancelled.
		return hp+sp;

	//Normal death
	if (battle_config.clear_unit_ondeath &&
		battle_config.clear_unit_ondeath&target->type)
		skill_clear_unitgroup(target);

	if(target->type&BL_REGEN)
	{	//Reset regen ticks.
		struct regen_data *regen = status_get_regen_data(target);
		if (regen) {
			memset(&regen->tick, 0, sizeof(regen->tick));
			if (regen->sregen)
				memset(&regen->sregen->tick, 0, sizeof(regen->sregen->tick));
			if (regen->ssregen)
				memset(&regen->ssregen->tick, 0, sizeof(regen->ssregen->tick));
		}
	}
   
	if( sc && sc->data[SC_KAIZEL] && !map_flag_gvg2(target->m) )
	{ //flag&8 = disable Kaizel
		int time = skill_get_time2(SL_KAIZEL,sc->data[SC_KAIZEL]->val1);
		//Look for Osiris Card's bonus effect on the character and revive 100% or revive normally
		if ( target->type == BL_PC && BL_CAST(BL_PC,target)->special_state.restart_full_recover )
			status_revive(target, 100, 100);
		else
			status_revive(target, sc->data[SC_KAIZEL]->val2, 0);
		status_change_clear(target,0);
		clif_skill_nodamage(target,target,ALL_RESURRECTION,1,1);
		sc_start(target,status_skill2sc(PR_KYRIE),100,10,time);

		if( target->type == BL_MOB ) 
			((TBL_MOB*)target)->state.rebirth = 1;

		return hp+sp;
	}

	if( target->type == BL_MOB && sc && sc->data[SC_REBIRTH] && !((TBL_MOB*)target)->state.rebirth )
	{// Ensure the monster has not already rebirthed before doing so.
		status_revive(target, sc->data[SC_REBIRTH]->val2, 0);
		status_change_clear(target,0);
		((TBL_MOB*)target)->state.rebirth = 1;

		return hp+sp;
	}

	status_change_clear(target,0);

	if(flag&4) //Delete from memory. (also invokes map removal code)
		unit_free(target,CLR_DEAD);
	else
	if(flag&2) //remove from map
		unit_remove_map(target,CLR_DEAD);
	else
	{ //Some death states that would normally be handled by unit_remove_map
		unit_stop_attack(target);
		unit_stop_walking(target,1);
		unit_skillcastcancel(target,0);
		clif_clearunit_area(target,CLR_DEAD);
		skill_unit_move(target,gettick(),4);
		skill_cleartimerskill(target);
	}

	// Always run NPC scripts for players last
	if (target->type == BL_PC) {
		TBL_PC *sd = BL_CAST(BL_PC, target);
		if (sd->bg_id) {
			struct battleground_data *bg;
			if ((bg = bg_team_search(sd->bg_id)) != NULL && bg->die_event[0])
				npc_event(sd, bg->die_event, 0);
		}

		npc_script_event(sd, NPCE_DIE);
	}

	return hp+sp;
}

//Heals a character. If flag&1, this is forced healing (otherwise stuff like Berserk can block it)
//If flag&2, when the player is healed, show the HP/SP heal effect.
int status_heal(struct block_list *bl,int64 hhp,int64 hsp, int flag)
{
	struct status_data *status;
	struct status_change *sc;

	int hp = (int)cap_value(hhp, INT_MIN, INT_MAX);
	int sp = (int)cap_value(hsp, INT_MIN, INT_MAX);

	status = status_get_status_data(bl);

	if (status == &dummy_status || !status->hp)
		return 0;

	sc = status_get_sc(bl);
	if (sc && !sc->count)
		sc = NULL;

	if (hp < 0) {
		if (hp == INT_MIN) hp++; //-INT_MIN == INT_MIN in some architectures!
		status_damage(NULL, bl, -hp, 0, 0, 1);
		hp = 0;
	}

	if (hp)
	{
		if (sc && sc->data[SC_BERSERK]) {
			if (flag & 1)
				flag &= ~2;
			else
				hp = 0;
		}

		if ((unsigned int)hp > status->max_hp - status->hp)
			hp = status->max_hp - status->hp;
	}

	if(sp < 0) {
		if (sp==INT_MIN) sp++;
		status_damage(NULL, bl, 0, -sp, 0, 1);
		sp = 0;
	}

	if(sp) {
		if((unsigned int)sp > status->max_sp - status->sp)
			sp = status->max_sp - status->sp;
	}

	if(!sp && !hp) return 0;

	status->hp+= hp;
	status->sp+= sp;

	if(hp && sc &&
		sc->data[SC_AUTOBERSERK] &&
		sc->data[SC_PROVOKE] &&
		sc->data[SC_PROVOKE]->val2==1 &&
		status->hp>=status->max_hp>>2
	)	//End auto berserk.
		status_change_end(bl, SC_PROVOKE, INVALID_TIMER);

	// send hp update to client
	switch(bl->type) {
	case BL_PC:  pc_heal((TBL_PC*)bl,hp,sp,flag&2?1:0); break;
	case BL_MOB: mob_heal((TBL_MOB*)bl,hp); break;
	case BL_HOM: hom_heal((TBL_HOM*)bl); break;
	case BL_MER: mercenary_heal((TBL_MER*)bl,hp,sp); break;
	case BL_ELEM: elemental_heal((TBL_ELEM*)bl,hp,sp); break;
	}

	return hp+sp;
}

//Does percentual non-flinching damage/heal. If mob is killed this way,
//no exp/drops will be awarded if there is no src (or src is target)
//If rates are > 0, percent is of current HP/SP
//If rates are < 0, percent is of max HP/SP
//If !flag, this is heal, otherwise it is damage.
//Furthermore, if flag==2, then the target must not die from the substraction.
int status_percent_change(struct block_list *src, struct block_list *target, int8 hp_rate, int8 sp_rate, uint8 flag)
{
	struct status_data *status;
	unsigned int hp  =0, sp = 0;

	status = status_get_status_data(target);

	if (hp_rate > 99)
		hp = status->hp;
	else if (hp_rate > 0)
		hp = apply_rate(status->hp, hp_rate);
	else if (hp_rate < -99)
		hp = status->max_hp;
	else if (hp_rate < 0)
		hp = (apply_rate(status->max_hp, -hp_rate));
	if (hp_rate && !hp)
		hp = 1;

	if (flag == 2 && hp >= status->hp)
		hp = status->hp-1; //Must not kill target.

	if (sp_rate > 99)
		sp = status->sp;
	else if (sp_rate > 0)
		sp = apply_rate(status->sp, sp_rate);
	else if (sp_rate < -99)
		sp = status->max_sp;
	else if (sp_rate < 0)
		sp = (apply_rate(status->max_sp, -sp_rate));
	if (sp_rate && !sp)
		sp = 1;

	//Ugly check in case damage dealt is too much for the received args of
	//status_heal / status_damage. [Skotlex]
	if (hp > INT_MAX) {
	  	hp -= INT_MAX;
		if (flag)
			status_damage(src, target, INT_MAX, 0, 0, (!src||src==target?5:1));
		else
		  	status_heal(target, INT_MAX, 0, 0);
	}
  	if (sp > INT_MAX) {
		sp -= INT_MAX;
		if (flag)
			status_damage(src, target, 0, INT_MAX, 0, (!src||src==target?5:1));
		else
		  	status_heal(target, 0, INT_MAX, 0);
	}	
	if (flag)
		return status_damage(src, target, hp, sp, 0, (!src||src==target?5:1));
	return status_heal(target, hp, sp, 0);
}

int status_revive(struct block_list *bl, unsigned char per_hp, unsigned char per_sp)
{
	struct status_data *status;
	unsigned int hp, sp;
	if (!status_isdead(bl)) return 0;

	status = status_get_status_data(bl);
	if (status == &dummy_status)
		return 0; //Invalid target.

	hp = (int64)status->max_hp * per_hp/100;
	sp = (int64)status->max_sp * per_sp/100;

	if(hp > status->max_hp - status->hp)
		hp = status->max_hp - status->hp;
	else if (per_hp && !hp)
		hp = 1;
		
	if(sp > status->max_sp - status->sp)
		sp = status->max_sp - status->sp;
	else if (per_sp && !sp)
		sp = 1;

	status->hp += hp;
	status->sp += sp;

	if (bl->prev) //Animation only if character is already on a map.
		clif_resurrection(bl, 1);
	switch (bl->type) {
		case BL_PC:  pc_revive((TBL_PC*)bl, hp, sp); break;
		case BL_MOB: mob_revive((TBL_MOB*)bl, hp); break;
		case BL_HOM: hom_revive((TBL_HOM*)bl, hp, sp); break;
	}
	return 1;
}

int status_fixed_revive(struct block_list *bl, unsigned int per_hp, unsigned int per_sp)
{
	struct status_data *status;
	unsigned int hp, sp;
	if (!status_isdead(bl)) return 0;

	status = status_get_status_data(bl);
	if (status == &dummy_status)
		return 0; //Invalid target.

	hp = per_hp;
	sp = per_sp;

	if(hp > status->max_hp - status->hp)
		hp = status->max_hp - status->hp;
	else if (per_hp && !hp)
		hp = 1;
		
	if(sp > status->max_sp - status->sp)
		sp = status->max_sp - status->sp;
	else if (per_sp && !sp)
		sp = 1;

	status->hp += hp;
	status->sp += sp;

	if (bl->prev) //Animation only if character is already on a map.
		clif_resurrection(bl, 1);
	switch (bl->type) {
		case BL_PC:  pc_revive((TBL_PC*)bl, hp, sp); break;
		case BL_MOB: mob_revive((TBL_MOB*)bl, hp); break;
		case BL_HOM: hom_revive((TBL_HOM*)bl, hp, sp); break;
	}
	return 1;
}

/*==========================================
 * Checks whether the src can use the skill on the target,
 * taking into account status/option of both source/target. [Skotlex]
 * flag:
 * 	0 - Trying to use skill on target.
 * 	1 - Cast bar is done.
 * 	2 - Skill already pulled off, check is due to ground-based skills or splash-damage ones.
 * src MAY be null to indicate we shouldn't check it, this is a ground-based skill attack.
 * target MAY Be null, in which case the checks are only to see 
 * whether the source can cast or not the skill on the ground.
 *------------------------------------------*/
int status_check_skilluse(struct block_list *src, struct block_list *target, int skill_id, int skill_lv, int flag)
{
	struct status_data *status;
	struct status_change *sc=NULL, *tsc;
	int hide_flag;

	status = src?status_get_status_data(src):&dummy_status;

	if (src && src->type != BL_PC && status_isdead(src))
		return 0;

	if (!skill_id) { //Normal attack checks.
		if (!(status->mode&MD_CANATTACK))
			return 0; //This mode is only needed for melee attacking.
		//Dead state is not checked for skills as some skills can be used 
		//on dead characters, said checks are left to skill.c [Skotlex]
		if (target && status_isdead(target))
			return 0;
		if( (sc = status_get_sc(src)) && sc->data[SC_CRYSTALIZE] )
			return 0;
	}

	if (skill_id == PA_PRESSURE && flag && target) {
		//Gloria Avoids pretty much everything....
		tsc = status_get_sc(target);
		if(tsc && tsc->option&OPTION_HIDE)
			return 0;
		return 1;
	}

	if( skill_id == GN_WALLOFTHORN && target && status_isdead(target) )
		return 0;

	//Should fail when used on top of Land Protector [Skotlex]
	if (src && (skill_id == AL_TELEPORT || skill_id == ALL_ODINS_RECALL) && map_getcell(src->m, src->x, src->y, CELL_CHKLANDPROTECTOR)
		&& !status_has_mode(status, MD_STATUS_IMMUNE) && (src->type != BL_PC || ((TBL_PC*)src)->skillitem != skill_id) )
 		return 0;

	if (src) sc = status_get_sc(src);

	if(sc && sc->count)
	{
		if (sc->data[SC_ALL_RIDING])
			return 0; //You can't use skills while in the new mounts (The client doesn't let you, this is to make cheat-safe)

		if (sc->opt1 > 0 && sc->opt1 != OPT1_BURNING && skill_id != RK_REFRESH && skill_id != SR_GENTLETOUCH_CURE && skill_id != SU_GROOMING)
		{	//Stuned/Frozen/etc
			if (flag != 1) //Can't cast, casted stuff can't damage. 
				return 0;
			if (!(skill_get_inf(skill_id)&INF_GROUND_SKILL))
				return 0; //Targetted spells can't come off.
		}

		if (
			(sc->data[SC_TRICKDEAD] && skill_id != NV_TRICKDEAD)
			|| ((sc->data[SC_AUTOCOUNTER] || sc->data[SC_DEATHBOUND]) && !flag)
			|| (sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_SELF && skill_id != PA_GOSPEL)
			|| (sc->data[SC_SUHIDE] && skill_id != SU_HIDE)
		)
			return 0;

		// Shadow Hold prevent's use of skills that hides the player. This doesn't apply to Chase Walk due to its immunity to detection.
		// Also prevent's the use of fly wings and butterfly wings (teleporting).
		if ( sc->data[SC_KG_KAGEHUMI] && (skill_id == AL_TELEPORT || 
			skill_id == TF_HIDING || skill_id == AS_CLOAKING || skill_id == GC_CLOAKINGEXCEED || 
			skill_id == RA_CAMOUFLAGE || skill_id == SC_SHADOWFORM || skill_id == KO_YAMIKUMO) )
			return 0;

		if (sc->data[SC_WINKCHARM] && target && !flag) { //Prevents skill usage
			if (unit_bl2ud(src) && (unit_bl2ud(src))->walktimer == INVALID_TIMER)
				unit_walktobl(src, map_id2bl(sc->data[SC_WINKCHARM]->val2), 3, 1);
			clif_emotion(src, E_LV);
			return 0;
		}

		if (sc->data[SC_BLADESTOP]) {
			switch (sc->data[SC_BLADESTOP]->val1)
			{
				case 5: if (skill_id == MO_EXTREMITYFIST) break;
				case 4: if (skill_id == MO_CHAINCOMBO) break;
				case 3: if (skill_id == MO_INVESTIGATE) break;
				case 2: if (skill_id == MO_FINGEROFFENSIVE) break;
				default: return 0;
			}
		}

		if (sc->data[SC_DANCING] && flag!=2)
		{
			if( src->type == BL_PC && skill_id >= WA_SWING_DANCE && skill_id <= WM_UNLIMITED_HUMMING_VOICE )
			{ // Lvl 5 Lesson or higher allow you use 3rd job skills while dancing.v
				if( pc_checkskill((TBL_PC*)src,WM_LESSON) < 5 )
					return 0;
			}
			else if(sc->data[SC_LONGING])
			{	//Allow everything except dancing/re-dancing. [Skotlex]
				if (skill_id == BD_ENCORE ||
					skill_get_inf2(skill_id)&(INF2_SONG_DANCE|INF2_ENSEMBLE_SKILL)
				)
					return 0;
			} else
			switch (skill_id) {
			case BD_ADAPTATION:
			case CG_LONGINGFREEDOM:
			case BA_MUSICALSTRIKE:
			case DC_THROWARROW:
				break;
			default:
				return 0;
			}
			if ((sc->data[SC_DANCING]->val1&0xFFFF) == CG_HERMODE && skill_id == BD_ADAPTATION)
				return 0;	//Can't amp out of Wand of Hermode :/ [Skotlex]
		}

		if (skill_id && //Do not block item-casted skills.
			(src->type != BL_PC || ((TBL_PC*)src)->skillitem != skill_id)
		) {	//Skills blocked through status changes...
			if (!flag && ( //Blocked only from using the skill (stuff like autospell may still go through
				sc->data[SC_SILENCE] ||
				sc->data[SC_DEEPSLEEP] ||
				sc->data[SC_CRYSTALIZE] ||
				(sc->data[SC_BASILICA] && (sc->data[SC_BASILICA]->val4 != src->id || skill_id != HP_BASILICA)) || // Only Basilica caster that can cast, and only Basilica to cancel it
				(sc->data[SC_MARIONETTE] && skill_id != CG_MARIONETTE) || //Only skill you can use is marionette again to cancel it
				(sc->data[SC_MARIONETTE2] && skill_id == CG_MARIONETTE) || //Cannot use marionette if you are being buffed by another
				(sc->data[SC_ANKLE] && skill_id == AL_TELEPORT) ||
				sc->data[SC_STEELBODY] ||
				sc->data[SC_BERSERK] ||
				sc->data[SC_OBLIVIONCURSE] ||
				sc->data[SC_STASIS] && skill_stasis_check(src, skill_id) ||
				sc->data[SC__INVISIBILITY] ||
				sc->data[SC__IGNORANCE] ||
				sc->data[SC_CURSEDCIRCLE_TARGET] ||
				sc->data[SC__SHADOWFORM] ||
				sc->data[SC_HEAT_BARREL_AFTER] ||
				sc->data[SC_FLASHCOMBO] ||
				sc->data[SC_KINGS_GRACE] ||
				sc->data[SC_ALL_RIDING] != NULL
			))
				return 0;

			//Skill blocking.
			if (
				(sc->data[SC_VOLCANO] && skill_id == WZ_ICEWALL) ||
				(sc->data[SC_ROKISWEIL] && skill_id != BD_ADAPTATION) ||
				(sc->data[SC_HERMODE] && skill_get_inf(skill_id) & INF_SUPPORT_SKILL) ||
				(sc->data[SC_NOCHAT] && sc->data[SC_NOCHAT]->val1&MANNER_NOSKILL)
			)
				return 0;
		}
	}

	// Check for src's status changes
	if (sc && sc->option)
	{
		if (sc->option&OPTION_HIDE)
		switch (skill_id) { //Usable skills while hiding.
			case TF_HIDING:
			case AS_GRIMTOOTH:
			case RG_BACKSTAP:
			case RG_RAID:
			case NJ_SHADOWJUMP:
			case NJ_KIRIKAGE:
			case KO_YAMIKUMO:
				break;
			default:
				//Non players can use all skills while hidden.
				if (!skill_id || src->type == BL_PC)
					return 0;
		}
		if (sc->option&OPTION_CHASEWALK && skill_id != ST_CHASEWALK)
			return 0;
		if (sc->option&OPTION_WUGRIDER && ((TBL_PC*)src)->skillitem != skill_id)
			switch (skill_id)
			{//List of skills usable while mounted on a warg.
				case HT_SKIDTRAP:		case HT_LANDMINE:
				case HT_ANKLESNARE:		case HT_SHOCKWAVE:
				case HT_SANDMAN:		case HT_FLASHER:
				case HT_FREEZINGTRAP:	case HT_BLASTMINE:
				case HT_CLAYMORETRAP:	case HT_TALKIEBOX:
				case BS_GREED:			case RA_DETONATOR:
				case RA_ELECTRICSHOCKER:case RA_CLUSTERBOMB:
				case RA_WUGRIDER:		case RA_WUGDASH:
				case RA_WUGSTRIKE:		case RA_MAGENTATRAP:
				case RA_COBALTTRAP:		case RA_MAIZETRAP:
				case RA_VERDURETRAP:	case RA_FIRINGTRAP:
				case RA_ICEBOUNDTRAP:	case ALL_FULL_THROTTLE:
					break;
				default:
					return 0;
			}
		if ( battle_config.mado_skill_limit == 0 && sc->option&OPTION_MADOGEAR && ((TBL_PC*)src)->skillitem != skill_id )
			switch ( skill_id )
			{//List of skills usable while mounted on a mado.
				case AL_TELEPORT:		case BS_GREED:
				case NC_BOOSTKNUCKLE:	case NC_PILEBUNKER:
				case NC_VULCANARM:		case NC_FLAMELAUNCHER:
				case NC_COLDSLOWER:		case NC_ARMSCANNON:
				case NC_ACCELERATION:	case NC_HOVERING:
				case NC_F_SIDESLIDE:	case NC_B_SIDESLIDE:
				case NC_SELFDESTRUCTION:case NC_SHAPESHIFT:
				case NC_EMERGENCYCOOL:	case NC_INFRAREDSCAN:
				case NC_ANALYZE:		case NC_MAGNETICFIELD:
				case NC_NEUTRALBARRIER:	case NC_STEALTHFIELD:
				case NC_REPAIR:			case NC_AXEBOOMERANG:
				case NC_POWERSWING:		case NC_AXETORNADO:
				case NC_SILVERSNIPER:	case NC_MAGICDECOY:
				case NC_DISJOINT:		case NC_MAGMA_ERUPTION:
				case ALL_FULL_THROTTLE:	case NC_MAGMA_ERUPTION_DOTDAMAGE:
					break;
				default:
					return 0;
			}
	}
	if (target == NULL || target == src) //No further checking needed.
		return 1;

	tsc = status_get_sc(target);
	
	// Check for target's status changes.
	if(tsc && tsc->count)
	{	
		if (tsc->data[SC_INVINCIBLE])
			return 0;
		if(!skill_id && tsc->data[SC_TRICKDEAD])
			return 0;
		if((skill_id == WZ_STORMGUST || skill_id == WZ_FROSTNOVA || skill_id == NJ_HYOUSYOURAKU)
			&& tsc->data[SC_FREEZE])
			return 0;
		if(skill_id == PR_LEXAETERNA && (tsc->data[SC_FREEZE] || (tsc->data[SC_STONE] && tsc->opt1 == OPT1_STONE)))
			return 0;
		if((skill_get_inf(skill_id)&INF_ATTACK_SKILL || skill_get_inf(skill_id)&INF_SUPPORT_SKILL) && tsc->data[SC_STEALTHFIELD])
			return 0;// Its blocking splash damage from other's targeted with INF_ATK_SKILL. Need to correct this later. [Rytech]
	}

	if( tsc && tsc->option )
	{
		if (battle_config.mado_cast_skill_on_limit == 0 && tsc->option&OPTION_MADOGEAR)
			switch (skill_id)
			{// List of skills not castable on player's mounted on a mado.
				case AL_HEAL:
				case AL_INCAGI:
				case AL_DECAGI:
				case AB_RENOVATIO:
				case AB_HIGHNESSHEAL:
				// Not confirmed, but if all heal skills
				// fail on mado's, these should too right? [Rytech]
				case SU_TUNABELLY:
				case SU_FRESHSHRIMP:
					return 0;
				default:
					break;
			}
	}

	//If targetting, cloak+hide protect you, otherwise only hiding does.
	hide_flag = flag?OPTION_HIDE:(OPTION_HIDE|OPTION_CLOAK|OPTION_CHASEWALK);
		
 	//You cannot hide from ground skills.
	if( skill_get_ele(skill_id, (skill_lv) ? skill_lv : 1) == ELE_EARTH )
		hide_flag &= ~OPTION_HIDE;

	switch( target->type ) {
		case BL_PC:
			{
				struct map_session_data *sd = (TBL_PC*) target;
				bool is_boss = (src && status_get_class_(src) == CLASS_BOSS);
				bool is_detect = status_has_mode(status, MD_DETECTOR);
				if (pc_isinvisible(sd))
					return 0;
				if ((tsc->data[SC_CLOAKINGEXCEED] || tsc->data[SC_NEWMOON]) && !is_boss && (((TBL_PC*)target)->special_state.perfect_hiding || is_detect))
					return 0;
				if( tsc->option&hide_flag && !is_boss && (sd->special_state.perfect_hiding || !is_detect))
					return 0;
				if( tsc->data[SC_CAMOUFLAGE] && !(is_boss || is_detect) && !skill_id )
					return 0;
			}
			break;
		case BL_ITEM:	//Allow targetting of items to pick'em up (or in the case of mobs, to loot them).
			//TODO: Would be nice if this could be used to judge whether the player can or not pick up the item it targets. [Skotlex]
			if (status->mode&MD_LOOTER)
				return 1;
			return 0;
		case BL_HOM: 
		case BL_MER:
		case BL_ELEM:
			if( target->type == BL_HOM && skill_id && battle_config.hom_setting&0x1 && skill_get_inf(skill_id)&INF_SUPPORT_SKILL && battle_get_master(target) != src )
				return 0; // Can't use support skills on Homunculus (only Master/Self)
			if( target->type == BL_MER && (skill_id == PR_ASPERSIO || (skill_id >= SA_FLAMELAUNCHER && skill_id <= SA_SEISMICWEAPON)) && battle_get_master(target) != src )
				return 0; // Can't use Weapon endow skills on Mercenary (only Master)
			if( (target->type == BL_MER || target->type == BL_ELEM) && skill_id == AM_POTIONPITCHER )
				return 0; // Can't use Potion Pitcher on Mercenaries
		default:
			//Check for chase-walk/hiding/cloaking opponents.
			if( tsc ){
				if( tsc->option&hide_flag && !(status->mode&(MD_DETECTOR)) )
					return 0;
			}
	}
	return 1;
}

//Checks whether the source can see and chase target.
int status_check_visibility(struct block_list *src, struct block_list *target)
{
	int view_range;
	struct status_data* status = status_get_status_data(src);
	struct status_change* tsc = status_get_sc(target);
	switch (src->type) {
	case BL_MOB:
		view_range = ((TBL_MOB*)src)->min_chase;
		break;
	case BL_PET:
		view_range = ((TBL_PET*)src)->db->range2;
		break;
	default:
		view_range = AREA_SIZE;
	}

	if (src->m != target->m || !check_distance_bl(src, target, view_range))
		return 0;

	if (tsc) {
		bool is_boss = (status_get_class_(src) == CLASS_BOSS);
		bool is_detector = status_has_mode(status, MD_DETECTOR);

		switch (target->type)
		{	//Check for chase-walk/hiding/cloaking opponents.
			case BL_PC:
				// Perfect hiding. Nothing can see or detect you, including insect and demon monsters.
				if (((tsc->data[SC_CLOAKINGEXCEED] || tsc->data[SC_NEWMOON])) && !is_boss &&
					(((TBL_PC*)target)->special_state.perfect_hiding || is_detector))
					return 0;
				// Normal hiding. Insects and demon monsters can detect you.
				if (((tsc->option&(OPTION_HIDE | OPTION_CLOAK | OPTION_CHASEWALK) || tsc->data[SC_CAMOUFLAGE])) && !is_boss &&
					( ((TBL_PC*)target)->special_state.perfect_hiding || !is_detector) )
 					return 0;
				break;
			default:
				if((tsc->option&(OPTION_HIDE|OPTION_CLOAK|OPTION_CHASEWALK) || tsc->data[SC_CAMOUFLAGE]) && !is_boss && !is_detector)
					return 0;
		}
	}

	return 1;
}

// Basic ASPD value
int status_base_amotion_pc(struct map_session_data* sd, struct status_data* status)
{
	int amotion;
	int classidx = pc_class2idx(sd->status.class_);
	
	// base weapon delay
	amotion = (sd->status.weapon < MAX_WEAPON_TYPE)
	 ? (job_info[classidx].aspd_base[sd->status.weapon]) // Single weapon
	 : (job_info[classidx].aspd_base[sd->weapontype1] + job_info[classidx].aspd_base[sd->weapontype2])*7/10; // Dual-wield
	
	// percentual delay reduction from stats
	amotion-= amotion * (4*status->agi + status->dex)/1000;
	
	// raw delay adjustment from bAspd bonus
	amotion+= sd->bonus.aspd_add;
	
 	return amotion;
}

static unsigned short status_base_atk(const struct block_list *bl, const struct status_data *status)
{
	int flag = 0, str, dex, dstr;

	if(!(bl->type&battle_config.enable_baseatk))
		return 0;

	if (bl->type == BL_PC)
	switch(((TBL_PC*)bl)->status.weapon){
		case W_BOW:
		case W_MUSICAL: 
		case W_WHIP:
		case W_REVOLVER:
		case W_RIFLE:
		case W_GATLING:
		case W_SHOTGUN:
		case W_GRENADE:
			flag = 1;
	}
	if (flag) {
		str = status->dex;
		dex = status->str;
	} else {
		str = status->str;
		dex = status->dex;
	}
	//Normally only players have base-atk, but homunc have a different batk
	// equation, hinting that perhaps non-players should use this for batk.
	// [Skotlex]
	dstr = str/10;
	str += dstr*dstr;
	if (bl->type == BL_PC)
		str+= dex/5 + status->luk/5;
	return cap_value(str, 0, USHRT_MAX);
}


static inline unsigned short status_base_matk_max(const struct status_data* status)
{
	return status->int_+(status->int_/5)*(status->int_/5);
}


static inline unsigned short status_base_matk_min(const struct status_data* status)
{
	return status->int_+(status->int_/7)*(status->int_/7);
}


//Fills in the misc data that can be calculated from the other status info (except for level)
void status_calc_misc(struct block_list *bl, struct status_data *status, int level)
{
	int stat;

	//Non players get the value set, players need to stack with previous bonuses.
	if( bl->type != BL_PC )
		status->batk = 
		status->hit = status->flee =
		status->def2 = status->mdef2 =
		status->cri = status->flee2 = 0;

	status->matk_min = status_base_matk_min(status);
	status->matk_max = status_base_matk_max(status);

	//Hit
	stat = status->hit;
	stat += level + status->dex;
	status->hit = cap_value(stat, 1, SHRT_MAX);
	//Flee
	stat = status->flee;
	stat += level + status->agi;
	status->flee = cap_value(stat, 1, SHRT_MAX);
	//Def2
	stat = status->def2;
	stat += status->vit;
	status->def2 = cap_value(stat, 0, SHRT_MAX);
	//MDef2
	stat = status->mdef2;
	stat += status->int_ + (status->vit >> 1);
	status->mdef2 = cap_value(stat, 0, SHRT_MAX);

	//Critical
	if (bl->type&battle_config.enable_critical) {
		stat = status->cri;
		stat += 10 + (status->luk * 10 / 3); // (every 1 luk = +0.3 critical)
		status->cri = cap_value(stat, 1, SHRT_MAX);
	}
	else
		status->cri = 0;

	if (bl->type&battle_config.enable_perfect_flee) {
		stat = status->flee2;
		stat += status->luk + 10; // (every 10 luk = +1 perfect flee)
		status->flee2 = cap_value(stat, 0, SHRT_MAX);
	}
	else
		status->flee2 = 0;

	if (status->batk) {
		int temp = status->batk + status_base_atk(bl, status);
		status->batk = cap_value(temp, 0, USHRT_MAX);
	} else
		status->batk = status_base_atk(bl, status);
	if (status->cri)
	switch (bl->type) {
	case BL_MOB:
		if(battle_config.mob_critical_rate != 100)
			status->cri = cap_value(status->cri*battle_config.mob_critical_rate / 100, 1, SHRT_MAX);
		if(!status->cri && battle_config.mob_critical_rate)
		  	status->cri = 10;
		break;
	case BL_PC:
		//Players don't have a critical adjustment setting as of yet.
		break;
	default:
		if(battle_config.critical_rate != 100)
			status->cri = cap_value(status->cri*battle_config.critical_rate / 100, 1, SHRT_MAX);
		if (!status->cri && battle_config.critical_rate)
			status->cri = 10;
	}
	if(bl->type&BL_REGEN)
		status_calc_regen(bl, status, status_get_regen_data(bl));
}

//Skotlex: Calculates the initial status for the given mob
//first will only be false when the mob leveled up or got a GuardUp level.
int status_calc_mob_(struct mob_data* md, enum e_status_calc_opt opt)
{
	struct status_data *status;
	struct block_list *mbl = NULL;
	int flag=0;

	if(opt&SCO_FIRST)
	{	//Set basic level on respawn.
		md->level = md->db->lv;
	}

	//Check if we need custom base-status
	if (battle_config.mobs_level_up && md->level > md->db->lv)
		flag|=1;
	
	if (md->special_state.size)
		flag|=2;

	if (md->guardian_data && md->guardian_data->guardup_lv)
		flag|=4;
	if (md->mob_id == MOBID_EMPERIUM)
		flag|=4;

	if (battle_config.slaves_inherit_speed && md->master_id)
		flag|=8;

	if (md->master_id && md->special_state.ai>1)
		flag|=16;

	if (md->master_id && battle_config.slaves_inherit_mode)
		flag |= 32;

	if (!flag)
	{ //No special status required.
		if (md->base_status) {
			aFree(md->base_status);
			md->base_status = NULL;
		}
		if(opt&SCO_FIRST)
			memcpy(&md->status, &md->db->status, sizeof(struct status_data));
		return 0;
	}
	if (!md->base_status)
		md->base_status = (struct status_data*)aCalloc(1, sizeof(struct status_data));

	status = md->base_status;
	memcpy(status, &md->db->status, sizeof(struct status_data));

	if (flag&(8|16))
		mbl = map_id2bl(md->master_id);

	if (flag&8 && mbl) {
		struct status_data *mstatus = status_get_base_status(mbl);
		if (mstatus &&
			battle_config.slaves_inherit_speed&(mstatus->mode&MD_CANMOVE?1:2))
			status->speed = mstatus->speed;
	}

	if (flag&16 && mbl)
	{	//Max HP setting from Summon Flora/marine Sphere
		struct unit_data *ud = unit_bl2ud(mbl);
		//Remove special AI when this is used by regular mobs.
		if (mbl->type == BL_MOB && !((TBL_MOB*)mbl)->special_state.ai)
			md->special_state.ai = 0;
		if (ud)
		{	// different levels of HP according to skill level
			if (ud->skill_id == AM_SPHEREMINE) {
				status->max_hp = 2000 + 400*ud->skill_lv;
			}
			else if (ud->skill_id == AM_CANNIBALIZE) {
				status->max_hp = 1500 + 200*ud->skill_lv + 10*status_get_lv(mbl);
				status->mode|= MD_CANATTACK|MD_AGGRESSIVE;
			}
			else if( ud->skill_id == NC_SILVERSNIPER )
			{
				short atkbonus[5] = { 0, 200, 400, 800, 1000 };

				status->rhw.atk = status->rhw.atk2 += atkbonus[ud->skill_lv-1];
				status->max_hp = 1000 * ud->skill_lv + 12 * status_get_lv(mbl) + status_get_max_hp(mbl) / 3;
			}
			// Disabled since MaxHP and MATK values are set elsewhere since it won't work here.
			//else if ( ud->skill_id == NC_MAGICDECOY )
			//{
			//	status->matk_min = status->matk_max = 250 + 50 * ud->skill_lv;
			//	status->max_hp = 1000 * ud->skill_lv + 12 * status_get_lv(mbl) + 4 * status_get_max_sp(mbl);
			//}
			else if ( ud->skill_id == KO_ZANZOU )
				status->max_hp = 3000 + 3000 * ud->skill_lv + status_get_max_sp(mbl);
			else if (ud->skill_id == MH_SUMMON_LEGION)
			{
				const int summon_def[10] = { 15, 20, 25, 30, 33, 37, 40, 42, 45, 47 };// DEF - Future proofed for 10 levels.
				short summon_amotion = 10 * (2 * (20 - ud->skill_lv) - status_get_lv(mbl) / 10);

				status->max_hp = 10 * (100 * (2 + ud->skill_lv) + status_get_lv(mbl));
				status->rhw.atk2 = 100 * (ud->skill_lv + 5) / 2;
				status->def = summon_def[ud->skill_lv-1];

				// ASPD formula is correct but feels OP. Need to see official in-game behavior. [Rytech]
				if ( summon_amotion < 100 )
					summon_amotion = 100;

				status->amotion = summon_amotion;
				status->adelay = 2 * status->amotion;
			}
			status->hp = status->max_hp;
		}
	}

	if (flag&32)
		status_calc_slave_mode(md, map_id2md(md->master_id));

	if (flag&1)
	{	// increase from mobs leveling up [Valaris]
		int diff = md->level - md->db->lv;
		status->str+= diff;
		status->agi+= diff;
		status->vit+= diff;
		status->int_+= diff;
		status->dex+= diff;
		status->luk+= diff;
		status->max_hp += diff*status->vit;
		status->max_sp += diff*status->int_;
		status->hp = status->max_hp;
		status->sp = status->max_sp;
		status->speed -= cap_value(diff, 0, status->speed - 10);
	}


	if (flag&2)
	{	// change for sized monsters [Valaris]
		if (md->special_state.size==1) {
			status->max_hp>>=1;
			status->max_sp>>=1;
			if (!status->max_hp) status->max_hp = 1;
			if (!status->max_sp) status->max_sp = 1;
			status->hp=status->max_hp;
			status->sp=status->max_sp;
			status->str>>=1;
			status->agi>>=1;
			status->vit>>=1;
			status->int_>>=1;
			status->dex>>=1;
			status->luk>>=1;
			if (!status->str) status->str = 1;
			if (!status->agi) status->agi = 1;
			if (!status->vit) status->vit = 1;
			if (!status->int_) status->int_ = 1;
			if (!status->dex) status->dex = 1;
			if (!status->luk) status->luk = 1;
		} else if (md->special_state.size==2) {
			status->max_hp<<=1;
			status->max_sp<<=1;
			status->hp=status->max_hp;
			status->sp=status->max_sp;
			status->str<<=1;
			status->agi<<=1;
			status->vit<<=1;
			status->int_<<=1;
			status->dex<<=1;
			status->luk<<=1;
		}
	}

	status_calc_misc(&md->bl, status, md->level);

	if(flag&4)
	{	// Strengthen Guardians - custom value +10% / lv
		struct guild_castle *gc;
		gc=guild_mapname2gc(map[md->bl.m].name);
		if (!gc)
			ShowError("status_calc_mob: No castle set at map %s\n", map[md->bl.m].name);
		else
		if(gc->castle_id < 24 || md->mob_id == MOBID_EMPERIUM) {
			status->max_hp += 1000 * gc->defense;
			status->max_sp += 200 * gc->defense;
			status->hp = status->max_hp;
			status->sp = status->max_sp;
			status->def += (gc->defense+2)/3;
			status->mdef += (gc->defense+2)/3;
		}
		if(md->mob_id != MOBID_EMPERIUM) {
			status->batk += status->batk * 10*md->guardian_data->guardup_lv/100;
			status->rhw.atk += status->rhw.atk * 10*md->guardian_data->guardup_lv/100;
			status->rhw.atk2 += status->rhw.atk2 * 10*md->guardian_data->guardup_lv/100;
			status->aspd_rate -= 100*md->guardian_data->guardup_lv;
		}
	}

	if(opt&SCO_FIRST) //Initial battle status
		memcpy(&md->status, status, sizeof(struct status_data));

	return 1;
}

//Skotlex: Calculates the stats of the given pet.
void status_calc_pet_(struct pet_data *pd, enum e_status_calc_opt opt)
{
	nullpo_retv(pd);

	if (opt&SCO_FIRST) {
		memcpy(&pd->status, &pd->db->status, sizeof(struct status_data));
		pd->status.mode = MD_CANMOVE; // pets discard all modes, except walking
		pd->status.class_ = CLASS_NORMAL;
		pd->status.speed = pd->petDB->speed;

		if(battle_config.pet_attack_support || battle_config.pet_damage_support)
		{// attack support requires the pet to be able to attack
			pd->status.mode|= MD_CANATTACK;
		}
	}

	if (battle_config.pet_lv_rate && pd->master)
	{
		struct map_session_data *sd = pd->master;
		int lv;

		lv =sd->status.base_level*battle_config.pet_lv_rate/100;
		if (lv < 0)
			lv = 1;
		if (lv != pd->pet.level || opt & SCO_FIRST)
		{
			struct status_data *bstat = &pd->db->status, *status = &pd->status;
			pd->pet.level = lv;
			if (!opt&SCO_FIRST) //Lv Up animation
				clif_misceffect(&pd->bl, 0);
			status->rhw.atk = (bstat->rhw.atk*lv)/pd->db->lv;
			status->rhw.atk2 = (bstat->rhw.atk2*lv)/pd->db->lv;
			status->str = (bstat->str*lv)/pd->db->lv;
			status->agi = (bstat->agi*lv)/pd->db->lv;
			status->vit = (bstat->vit*lv)/pd->db->lv;
			status->int_ = (bstat->int_*lv)/pd->db->lv;
			status->dex = (bstat->dex*lv)/pd->db->lv;
			status->luk = (bstat->luk*lv)/pd->db->lv;

			status->rhw.atk = cap_value(status->rhw.atk, 1, battle_config.pet_max_atk1);
			status->rhw.atk2 = cap_value(status->rhw.atk2, 2, battle_config.pet_max_atk2);
			status->str = cap_value(status->str,1,battle_config.pet_max_stats);
			status->agi = cap_value(status->agi,1,battle_config.pet_max_stats);
			status->vit = cap_value(status->vit,1,battle_config.pet_max_stats);
			status->int_= cap_value(status->int_,1,battle_config.pet_max_stats);
			status->dex = cap_value(status->dex,1,battle_config.pet_max_stats);
			status->luk = cap_value(status->luk,1,battle_config.pet_max_stats);

			status_calc_misc(&pd->bl, &pd->status, lv);

			if (!opt&SCO_FIRST)	//Not done the first time because the pet is not visible yet
				clif_send_petstatus(sd);
		}
	} else if (opt&SCO_FIRST) {
		status_calc_misc(&pd->bl, &pd->status, pd->db->lv);
		if (!battle_config.pet_lv_rate && pd->pet.level != pd->db->lv)
			pd->pet.level = pd->db->lv;
	}
	
	//Support rate modifier (1000 = 100%)
	pd->rate_fix = min(1000 * (pd->pet.intimate - battle_config.pet_support_min_friendly) / (1000 - battle_config.pet_support_min_friendly) + 500, USHRT_MAX);
	pd->rate_fix = min(apply_rate(pd->rate_fix, battle_config.pet_support_rate), USHRT_MAX);
}

/** [Cydh]
* Get HP bonus modifiers
* @param bl: block_list that will be checked
* @param type: type of e_status_bonus (STATUS_BONUS_FIX or STATUS_BONUS_RATE)
* @return bonus: total bonus for HP
*/
static int status_get_hpbonus(struct block_list *bl, enum e_status_bonus type) {
	int bonus = 0;

	if (type == STATUS_BONUS_FIX) {
		struct status_change *sc = status_get_sc(bl);

		//Only for BL_PC
		if (bl->type == BL_PC) {
			struct map_session_data *sd = map_id2sd(bl->id);
			int i;

			bonus += sd->bonus.hp;
			if ((i = pc_checkskill(sd, CR_TRUST)) > 0)
				bonus += 200 * i;
			if (pc_checkskill(sd, SU_SPRITEMABLE) > 0)
				bonus += 1000;
			if (pc_checkskill(sd, SU_POWEROFSEA) > 0)
			{
				bonus += 1000;

				if (skill_summoner_power(sd, POWER_OF_SEA) == 1)
					bonus += 3000;
			}
			if( (i = pc_checkskill(sd,NV_BREAKTHROUGH)) > 0 )
			{
				bonus += 350 * i;
		
				if ( i >= 5 )
					bonus += 250;
			}
			if( (i = pc_checkskill(sd,NV_TRANSCENDENCE)) > 0 )
			{
				bonus += 350 * i;
		
				if ( i >= 5 )
					bonus += 250;
			}

			if ((sd->class_&MAPID_UPPERMASK) == MAPID_SUPER_NOVICE && sd->status.base_level >= 99)
				bonus += 2000; // Supernovice lvl99 hp bonus.
			if ((sd->class_&MAPID_UPPERMASK) == MAPID_SUPER_NOVICE && sd->status.base_level >= 150)
				bonus += 2000; // Supernovice lvl150 hp bonus.
		}

		//Bonus by SC
		if (sc) {
			//Increasing
			if (sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 2)
				bonus += 500;
			if (sc->data[SC_SOLID_SKIN_OPTION])
				bonus += 2000;
			if (sc->data[SC_LERADS_DEW])
				bonus += sc->data[SC_LERADS_DEW]->val3;
			if (sc->data[SC_PROMOTE_HEALTH_RESERCH])
				bonus += sc->data[SC_PROMOTE_HEALTH_RESERCH]->val4;
			if (sc->data[SC_SWORDCLAN])
				bonus += 30;
			if (sc->data[SC_ARCWANDCLAN])
				bonus += 30;
			if (sc->data[SC_GOLDENMACECLAN])
				bonus += 30;
			if (sc->data[SC_CROSSBOWCLAN])
				bonus += 30;
			if (sc->data[SC_INSPIRATION])
				bonus += (600 * sc->data[SC_INSPIRATION]->val1);
			if (sc->data[SC_MTF_MHP])
				bonus += sc->data[SC_MTF_MHP]->val1;

			//Decreasing
			if (sc->data[SC_MARIONETTE])
				bonus -= 1000;
		}
	}
	else if (type == STATUS_BONUS_RATE) {
		struct status_change *sc = status_get_sc(bl);

		//Bonus by SC
		if (sc) {
			//Increasing
			if (sc->data[SC_INCMHPRATE])
				bonus += sc->data[SC_INCMHPRATE]->val1;
			if (sc->data[SC_APPLEIDUN])
				bonus += sc->data[SC_APPLEIDUN]->val2;
			if (sc->data[SC_DELUGE])
				bonus += sc->data[SC_DELUGE]->val2;
			if (sc->data[SC_BERSERK])
				bonus += 200; //+200%
			if (sc->data[SC_MERC_HPUP])
				bonus += sc->data[SC_MERC_HPUP]->val2;
			if (sc->data[SC_EPICLESIS])
				bonus += sc->data[SC_EPICLESIS]->val2;
			if (sc->data[SC_LAUDAAGNUS])
				bonus += sc->data[SC_LAUDAAGNUS]->val2;
			if (sc->data[SC_FORCEOFVANGUARD])
				bonus += (3 * sc->data[SC_FORCEOFVANGUARD]->val1);
			if (sc->data[SC_INSPIRATION])
				bonus += (5 * sc->data[SC_INSPIRATION]->val1);
			if (sc->data[SC_RAISINGDRAGON])
				bonus += (2 + sc->data[SC_RAISINGDRAGON]->val1);
			if (sc->data[SC_GENTLETOUCH_REVITALIZE])
				bonus += sc->data[SC_GENTLETOUCH_REVITALIZE]->val2;
			if (sc->data[SC_LUNARSTANCE])
				bonus += sc->data[SC_LUNARSTANCE]->val2;
			if (sc->data[SC_FRIGG_SONG])
				bonus += sc->data[SC_FRIGG_SONG]->val2;
			if (sc->data[SC_MUSTLE_M])
				bonus += sc->data[SC_MUSTLE_M]->val1;
			if (sc->data[SC_ANGRIFFS_MODUS])
				bonus += (5 * sc->data[SC_ANGRIFFS_MODUS]->val1);
			if(sc->data[SC_POWER_OF_GAIA])
				bonus += 20;
			if(sc->data[SC_PETROLOGY_OPTION])
				bonus += 5;
			if(sc->data[SC_CURSED_SOIL_OPTION])
				bonus += 10;
			if(sc->data[SC_UPHEAVAL_OPTION])
				bonus += 15;

			//Decreasing
			if (sc->data[SC_VENOMBLEED])
				bonus -= 15;
			if (sc->data[SC__WEAKNESS])
				bonus -= sc->data[SC__WEAKNESS]->val2;
			if (sc->data[SC_MYSTERIOUS_POWDER])
				bonus -= sc->data[SC_MYSTERIOUS_POWDER]->val1;
			if (sc->data[SC_GENTLETOUCH_CHANGE]) // Max HP decrease: [Skill Level x 4] %
				bonus -= (4 * sc->data[SC_GENTLETOUCH_CHANGE]->val1);
			if (sc->data[SC_BEYOND_OF_WARCRY])
				bonus -= sc->data[SC_BEYOND_OF_WARCRY]->val4;
			if (sc->data[SC_EQC])
				bonus -= sc->data[SC_EQC]->val4;
		}
		// Max rate reduce is -100%
		bonus = cap_value(bonus, -100, INT_MAX);
	}

	return min(bonus, INT_MAX);
}

/**
* HP bonus rate from usable items
*/
static int status_get_hpbonus_item(struct block_list *bl) {
	int bonus = 0;

	struct status_change *sc = status_get_sc(bl);

	//Bonus by SC
	if (sc) {
		if (sc->data[SC_MUSTLE_M])
			bonus += sc->data[SC_MUSTLE_M]->val1;

		if (sc->data[SC_MYSTERIOUS_POWDER])
			bonus -= sc->data[SC_MYSTERIOUS_POWDER]->val1;
	}

	// Max rate reduce is -100%
	return cap_value(bonus, -100, INT_MAX);
}

/** [Cydh]
* Get SP bonus modifiers
* @param bl: block_list that will be checked
* @param type: type of e_status_bonus (STATUS_BONUS_FIX or STATUS_BONUS_RATE)
* @return bonus: total bonus for SP
*/
static int status_get_spbonus(struct block_list *bl, enum e_status_bonus type) {
	int bonus = 0;

	if (type == STATUS_BONUS_FIX) {
		struct status_change *sc = status_get_sc(bl);

		//Only for BL_PC
		if (bl->type == BL_PC) {
			struct map_session_data *sd = map_id2sd(bl->id);
			int i;

			bonus += sd->bonus.sp;
			if ((i = pc_checkskill(sd, SL_KAINA)) > 0)
				bonus += 30 * i;
			if ((i = pc_checkskill(sd, HP_MEDITATIO)) > 0)
				bonus += bonus * i / 100;
			if ((i = pc_checkskill(sd, HW_SOULDRAIN)) > 0)
				bonus += bonus * 2 * i / 100;
			if ((i = pc_checkskill(sd, RA_RESEARCHTRAP)) > 0)
				bonus += 200 + 20 * i;
			if ((i = pc_checkskill(sd, WM_LESSON)) > 0)
				bonus += 30 * i;
			if (pc_checkskill(sd, SU_SPRITEMABLE) > 0)
				bonus += 100;
			if (pc_checkskill(sd, SU_POWEROFSEA) > 0)
			{
				bonus += 100;

				if (skill_summoner_power(sd, POWER_OF_SEA) == 1)
					bonus += 300;
			}
			if( (i = pc_checkskill(sd,NV_BREAKTHROUGH)) > 0 )
			{
				bonus += 30 * i;
		
				if ( i >= 5 )
					bonus += 50;
			}
			if( (i = pc_checkskill(sd,NV_TRANSCENDENCE)) > 0 )
			{
				bonus += 30 * i;
		
				if ( i >= 5 )
					bonus += 50;
			}
		}

		//Bonus by SC
		if (sc) {
			if (sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 3)
				bonus += 50;
			if (sc->data[SC_MTF_MSP])
				bonus += sc->data[SC_MTF_MSP]->val1;
		}
	}
	else if (type == STATUS_BONUS_RATE) {
		struct status_change *sc = status_get_sc(bl);

		//Only for BL_PC
		if (bl->type == BL_PC) {
			struct map_session_data *sd = map_id2sd(bl->id);
			int i;

			if ((i = pc_checkskill(sd, HP_MEDITATIO)) > 0)
				bonus += i;
			if ((i = pc_checkskill(sd, WM_LESSON)) > 0)
				bonus += 30 * i;
			if ((i = pc_checkskill(sd, HW_SOULDRAIN)) > 0)
				bonus += 2 * i;
		}

		//Bonus by SC
		if (sc) {
			//Increasing
			if (sc->data[SC_INCMSPRATE])
				bonus += sc->data[SC_INCMSPRATE]->val1;
			if (sc->data[SC_RAISINGDRAGON])
				bonus += (2 + sc->data[SC_RAISINGDRAGON]->val1);
			if (sc->data[SC_SERVICE4U])
				bonus += sc->data[SC_SERVICE4U]->val2;
			if (sc->data[SC_MERC_SPUP])
				bonus += sc->data[SC_MERC_SPUP]->val2;
			if (sc->data[SC_RAISINGDRAGON])
				bonus += (2 + sc->data[SC_RAISINGDRAGON]->val1);
			if (sc->data[SC_LIFE_FORCE_F])
				bonus += sc->data[SC_LIFE_FORCE_F]->val1;
			if (sc->data[SC_ENERGY_DRINK_RESERCH])
				bonus += sc->data[SC_ENERGY_DRINK_RESERCH]->val4;
			if (sc->data[SC_VITATA_500])
				bonus += sc->data[SC_VITATA_500]->val2;

			//Decreasing
		}
		// Max rate reduce is -100%
		bonus = cap_value(bonus, -100, INT_MAX);
	}

	return min(bonus, INT_MAX);
}

/**
* SP bonus rate from usable items
*/
static int status_get_spbonus_item(struct block_list *bl) {
	int bonus = 0;

	struct status_change *sc = status_get_sc(bl);

	//Bonus by SC
	if (sc) {
		if (sc->data[SC_LIFE_FORCE_F])
			bonus += sc->data[SC_LIFE_FORCE_F]->val1;
		if (sc->data[SC_VITATA_500])
			bonus += sc->data[SC_VITATA_500]->val2;
		if (sc->data[SC_ENERGY_DRINK_RESERCH])
			bonus += sc->data[SC_ENERGY_DRINK_RESERCH]->val3;
	}

	// Max rate reduce is -100%
	return cap_value(bonus, -100, INT_MAX);
}

// Get final MaxHP or MaxSP for player. References: http://irowiki.org/wiki/Max_HP and http://irowiki.org/wiki/Max_SP
// The calculation needs base_level, base_status/battle_status (vit or int), additive modifier, and multiplicative modifier
// @param sd Player
// @param stat Vit/Int of player as param modifier
// @param isHP true - calculates Max HP, false - calculated Max SP
// @return max The max value of HP or SP
static unsigned int status_calc_maxhpsp_pc(struct map_session_data* sd, unsigned int stat, bool isHP) 
{
	double dmax = 0;
	uint16 idx, level, job_id;

	nullpo_ret(sd);

	job_id = pc_mapid2jobid(sd->class_,sd->status.sex);
	idx = pc_class2idx(job_id);
	level = umax(sd->status.base_level,1);

	if (isHP) {//Calculates MaxHP
		dmax = job_info[idx].base_hp[level - 1] * (1 + (umax(stat, 1) * 0.01)) * ((sd->class_&JOBL_UPPER) ? 1.25 : (pc_is_taekwon_ranker(sd)) ? 3 : 1);
		dmax += status_get_hpbonus(&sd->bl, STATUS_BONUS_FIX);
		dmax += (dmax * (sd->hprate - 100) / 100); // HP bonus rate from equipment
		dmax += (dmax * status_get_hpbonus_item(&sd->bl) / 100);
		dmax += (int64)(dmax * status_get_hpbonus(&sd->bl, STATUS_BONUS_RATE) / 100); //Aegis accuracy
	}
	else {//Calculates MaxSP
		dmax = job_info[idx].base_sp[level - 1] * (1 + (umax(stat, 1) * 0.01)) * ((sd->class_&JOBL_UPPER) ? 1.25 : (pc_is_taekwon_ranker(sd)) ? 3 : 1);
		dmax += status_get_spbonus(&sd->bl, STATUS_BONUS_FIX);
		dmax += (dmax * (sd->sprate - 100) / 100); // SP bonus rate from equipment
		dmax += (dmax * status_get_spbonus_item(&sd->bl) / 100);
		dmax += (int64)(dmax * status_get_spbonus(&sd->bl, STATUS_BONUS_RATE) / 100); //Aegis accuracy
	}

	// Baby classes get a 30% hp/sp penalty
	if (battle_config.baby_hp_sp_penalty == 1 && sd->class_&JOBL_BABY)
		dmax -= dmax * 30 / 100;

	//Make sure it's not negative before casting to unsigned int
	if(dmax < 1)
		dmax = 1;

	return cap_value((unsigned int)dmax,1,UINT_MAX);
}

/**
 * Calculates player's weight
 * @param sd: Player object
 * @param flag: Calculation type
 *   1 - Item weight
 *   2 - Skill/Status/Configuration max weight bonus
 * @return false - failed, true - success
 */
bool status_calc_weight(struct map_session_data *sd, uint8 flag)
{
	int b_weight, b_max_weight, skill, i;
	struct status_change *sc;

	nullpo_retr(false, sd);

	sc = &sd->sc;
	b_max_weight = sd->max_weight; // Store max weight for later comparison
	b_weight = sd->weight; // Store current weight for later comparison
	sd->max_weight = job_info[pc_class2idx(sd->status.class_)].max_weight_base + sd->status.str * 300; // Recalculate max weight

	if (flag&1) {
		sd->weight = 0; // Reset current weight

		for(i = 0; i < MAX_INVENTORY; i++) {
			if (!sd->inventory.u.items_inventory[i].nameid || sd->inventory_data[i] == NULL)
				continue;
			sd->weight += sd->inventory_data[i]->weight * sd->inventory.u.items_inventory[i].amount;
		}
	}

	if (flag&2) {
		// Skill/Status bonus weight increases
		if((skill=pc_checkskill(sd,MC_INCCARRY))>0)
			sd->max_weight += 2000*skill;
		if(pc_isriding(sd) && pc_checkskill(sd,KN_RIDING)>0)
			sd->max_weight += 10000;
		if (pc_ismadogear(sd) && pc_checkskill(sd, NC_MADOLICENCE) > 0)
			sd->max_weight += 15000;
		if(sc->data[SC_KNOWLEDGE])
			sd->max_weight += sd->max_weight*sc->data[SC_KNOWLEDGE]->val1/10;
		if(pc_isdragon(sd) && (skill=pc_checkskill(sd,RK_DRAGONTRAINING))>0)
			sd->max_weight += 5000+2000*skill;
		if((skill=pc_checkskill(sd,ALL_INCCARRY))>0)
			sd->max_weight += 2000*skill;
	}

	// Update the client if the new weight calculations don't match
	if (b_weight != sd->weight)
		clif_updatestatus(sd, SP_WEIGHT);
	if (b_max_weight != sd->max_weight) {
		clif_updatestatus(sd, SP_MAXWEIGHT);
		pc_updateweightstatus(sd);
	}

	return true;
}

//Calculates player data from scratch without counting SC adjustments.
//Should be invoked whenever players raise stats, learn passive skills or change equipment.
int status_calc_pc_(struct map_session_data* sd, enum e_status_calc_opt opt)
{
	static int calculating = 0; //Check for recursive call preemption. [Skotlex]
	struct status_data *status; // pointer to the player's base status
	const struct status_change *sc = &sd->sc;
	struct s_skill b_skill[MAX_SKILL]; // previous skill tree
	int i, index, skill, refinedef = 0;
	int b_cart_weight_max;
	short passive_add_matk = 0;
	short passive_matk_rate = 0;

	if (++calculating > 10) //Too many recursive calls!
		return -1;

	// remember player-specific values that are currently being shown to the client (for refresh purposes)
	memcpy(b_skill, &sd->status.skill, sizeof(b_skill));

	b_cart_weight_max = sd->cart_weight_max;

	pc_calc_skilltree(sd);	// スキルツリ?の計算

	if(opt&SCO_FIRST) {
		//Load Hp/SP from char-received data.
		sd->battle_status.hp = sd->status.hp;
		sd->battle_status.sp = sd->status.sp;
		sd->regen.sregen = &sd->sregen;
		sd->regen.ssregen = &sd->ssregen;
	}

	status = &sd->base_status;
	// these are not zeroed. [zzo]
	sd->hprate=100;
	sd->sprate=100;
	sd->castrate=100;
	sd->fixedcastrate=100;
	sd->delayrate=100;
	sd->cooldownrate = 100;
	sd->dsprate=100;
	sd->hprecov_rate = 100;
	sd->sprecov_rate = 100;
	sd->matk_rate = 100;
	sd->critical_rate = sd->hit_rate = sd->flee_rate = sd->flee2_rate = 100;
	sd->def_rate = sd->def2_rate = sd->mdef_rate = sd->mdef2_rate = 100;
	sd->regen.state.block = 0;

	// zeroed arrays, order follows the order in pc.h.
	// add new arrays to the end of zeroed area in pc.h (see comments) and size here. [zzo]
	memset (sd->param_bonus, 0, sizeof(sd->param_bonus)
		+ sizeof(sd->param_equip)
		+ sizeof(sd->subele)
		+ sizeof(sd->subele_script)
		+ sizeof(sd->subdefele)
		+ sizeof(sd->subrace)
		+ sizeof(sd->subclass)
		+ sizeof(sd->subrace2)
		+ sizeof(sd->subsize)
		+ sizeof(sd->reseff)
		+ sizeof(sd->coma_class)
		+ sizeof(sd->coma_race)
		+ sizeof(sd->weapon_coma_ele)
		+ sizeof(sd->weapon_coma_race)
		+ sizeof(sd->weapon_coma_class)
		+ sizeof(sd->weapon_atk)
		+ sizeof(sd->weapon_atk_rate)
		+ sizeof(sd->arrow_addele) 
		+ sizeof(sd->arrow_addrace)
		+ sizeof(sd->arrow_addclass)
		+ sizeof(sd->arrow_addsize)
		+ sizeof(sd->magic_addele)
		+ sizeof(sd->magic_addele_script)
		+ sizeof(sd->magic_addrace)
		+ sizeof(sd->magic_addclass)
		+ sizeof(sd->magic_addsize)
		+ sizeof(sd->magic_atk_ele)
		+ sizeof(sd->critaddrace)
		+ sizeof(sd->expaddrace)
		+ sizeof(sd->ignore_def_by_race)
		+ sizeof(sd->ignore_mdef_by_race)
		+ sizeof(sd->ignore_mdef_by_class)
		+ sizeof(sd->sp_gain_race)
		+ sizeof(sd->dropaddrace)
		+ sizeof(sd->dropaddclass)
		);

	memset (&sd->right_weapon.overrefine, 0, sizeof(sd->right_weapon) - sizeof(sd->right_weapon.atkmods));
	memset (&sd->left_weapon.overrefine, 0, sizeof(sd->left_weapon) - sizeof(sd->left_weapon.atkmods));

	if (sd->special_state.intravision) //Clear status change.
		clif_status_load(&sd->bl, SI_INTRAVISION, 0);

	memset(&sd->special_state,0,sizeof(sd->special_state));
	memset(&status->max_hp, 0, sizeof(struct status_data) - (sizeof(status->hp) + sizeof(status->sp)));

	//FIXME: Most of these stuff should be calculated once, but how do I fix the memset above to do that? [Skotlex]
	status->speed = DEFAULT_WALK_SPEED;
	//Give them all modes except these (useful for clones)
	status->mode = MD_MASK&~(MD_STATUS_IMMUNE|MD_IGNOREMELEE|MD_IGNOREMAGIC|MD_IGNORERANGED|MD_IGNOREMISC|MD_DETECTOR|MD_ANGRY|MD_TARGETWEAK);

	status->size = (sd->class_&JOBL_BABY || (sd->class_&MAPID_BASEMASK) == MAPID_SUMMONER) ? 0 : 1;
	if (battle_config.character_size && (pc_isriding(sd)||pc_isdragon(sd)||pc_iswugrider(sd)||pc_ismadogear(sd))) { //[Lupus]
		if (sd->class_&JOBL_BABY) {
			if (battle_config.character_size&2)
				status->size++;
		} else
		if(battle_config.character_size&1)
			status->size++;
	}
	status->aspd_amount = 0;
	status->aspd_rate = 1000;
	status->ele_lv = 1;
	status->race = ((sd->class_&MAPID_BASEMASK) == MAPID_SUMMONER) ? RC_BRUTE : RC_PLAYER;
	status->class_ = CLASS_NORMAL;

	//zero up structures...
	memset(&sd->autospell,0,sizeof(sd->autospell)
		+ sizeof(sd->autospell2)
		+ sizeof(sd->autospell3)
		+ sizeof(sd->addeff)
		+ sizeof(sd->addeff_atked)
		+ sizeof(sd->addeff_onskill)
		+ sizeof(sd->skillatk)
		+ sizeof(sd->skillheal)
		+ sizeof(sd->skillheal2)
		+ sizeof(sd->hp_loss)
		+ sizeof(sd->sp_loss)
		+ sizeof(sd->hp_regen)
		+ sizeof(sd->sp_regen)
		+ sizeof(sd->skillblown)
		+ sizeof(sd->skillcast)
		+ sizeof(sd->fixedskillcast)
		+ sizeof(sd->skillsprate)
		+ sizeof(sd->skillcooldown)
		+ sizeof(sd->add_def)
		+ sizeof(sd->add_mdef)
		+ sizeof(sd->add_mdmg)
		+ sizeof(sd->add_drop)
		+ sizeof(sd->itemhealrate)
		+ sizeof(sd->subele2)
		+ sizeof(sd->def_set_race)
		+ sizeof(sd->mdef_set_race)
		+ sizeof(sd->hp_vanish_race)
		+ sizeof(sd->sp_vanish_race)
	);

	memset(&sd->bonus, 0, sizeof(sd->bonus));

	// Autobonus
	pc_delautobonus(sd,sd->autobonus,ARRAYLENGTH(sd->autobonus),true);
	pc_delautobonus(sd,sd->autobonus2,ARRAYLENGTH(sd->autobonus2),true);
	pc_delautobonus(sd,sd->autobonus3,ARRAYLENGTH(sd->autobonus3),true);

	pc_itemgrouphealrate_clear(sd);

	// Parse equipment.
	for(i=0;i<EQI_MAX;i++) {
		current_equip_item_index = index = sd->equip_index[i]; //We pass INDEX to current_equip_item_index - for EQUIP_SCRIPT (new cards solution) [Lupus]
		if(index < 0)
			continue;
		if (i == EQI_AMMO)
			continue;
		if (pc_is_same_equip_index((enum equip_index)i, sd->equip_index, index))
			continue;
		if(!sd->inventory_data[index])
			continue;

		status->def += sd->inventory_data[index]->def;

		// Items may be equipped, their effects however are nullified.
		if(opt&SCO_FIRST && sd->inventory_data[index]->equip_script && (!itemdb_isNoEquip(sd->inventory_data[index], sd->bl.m)))
	  	{	//Execute equip-script on login
			run_script(sd->inventory_data[index]->equip_script,0,sd->bl.id,0);
			if (!calculating)
				return 1;
		}

		if(sd->inventory_data[index]->type == IT_WEAPON) {
			int r,wlv = sd->inventory_data[index]->wlv;
			struct weapon_data *wd;
			struct weapon_atk *wa;
			
			if (wlv >= MAX_REFINE_BONUS) 
				wlv = MAX_REFINE_BONUS - 1;
			if(i == EQI_HAND_L && sd->inventory.u.items_inventory[index].equip == EQP_HAND_L) {
				wd = &sd->left_weapon; // Left-hand weapon
				wa = &status->lhw;
			} else {
				wd = &sd->right_weapon;
				wa = &status->rhw;
			}
			wa->atk += sd->inventory_data[index]->atk;
			wa->atk2 = (r=sd->inventory.u.items_inventory[index].refine)*refinebonus[wlv][0];
			if((r-=refinebonus[wlv][2])>0) //Overrefine bonus.
				wd->overrefine = r*refinebonus[wlv][1];

			wa->range += sd->inventory_data[index]->range;
			if(sd->inventory_data[index]->script && (!itemdb_isNoEquip(sd->inventory_data[index], sd->bl.m))) {
				if (wd == &sd->left_weapon) {
					sd->state.lr_flag = 1;
					run_script(sd->inventory_data[index]->script,0,sd->bl.id,0);
					sd->state.lr_flag = 0;
				} else
					run_script(sd->inventory_data[index]->script,0,sd->bl.id,0);
				if (!calculating) //Abort, run_script retriggered this. [Skotlex]
					return 1;
			}

			if(sd->inventory.u.items_inventory[index].card[0]==CARD0_FORGE)
			{	// Forged weapon
				wd->star += (sd->inventory.u.items_inventory[index].card[1]>>8);
				if(wd->star >= 15) wd->star = 40; // 3 Star Crumbs now give +40 dmg
				if(pc_famerank(MakeDWord(sd->inventory.u.items_inventory[index].card[2],sd->inventory.u.items_inventory[index].card[3]) ,MAPID_BLACKSMITH))
					wd->star += 10;
				
				if (!wa->ele) //Do not overwrite element from previous bonuses.
					wa->ele = (sd->inventory.u.items_inventory[index].card[1]&0x0f);
			}
		}
		else if(sd->inventory_data[index]->type == IT_ARMOR) {
			if (!((battle_config.costume_refine_def != 1 && i >= EQI_COSTUME_HEAD_TOP && i <= EQI_COSTUME_GARMENT) ||
				(battle_config.shadow_refine_def != 1 && i >= EQI_SHADOW_ARMOR && i <= EQI_SHADOW_ACC_L)))
				refinedef += sd->inventory.u.items_inventory[index].refine * refinebonus[0][0];
			if(sd->inventory_data[index]->script && (!itemdb_isNoEquip(sd->inventory_data[index], sd->bl.m))) {
				if (i == EQI_HAND_L) //Shield
					sd->state.lr_flag = 3;
				run_script(sd->inventory_data[index]->script,0,sd->bl.id,0);
				if (i == EQI_HAND_L) //Shield
					sd->state.lr_flag = 0;
				if (!calculating) //Abort, run_script retriggered this. [Skotlex]
					return 1;
			}
		}
		else if (sd->inventory_data[index]->type == IT_SHADOWGEAR) { // Shadow System
			if (!((battle_config.shadow_refine_def != 1 && i >= EQI_SHADOW_ARMOR && i <= EQI_SHADOW_ACC_L)))
				refinedef += sd->inventory.u.items_inventory[index].refine * refinebonus[0][0];
			if (sd->inventory_data[index]->script && !itemdb_isNoEquip(sd->inventory_data[index], sd->bl.m)) {
				run_script(sd->inventory_data[index]->script, 0, sd->bl.id, 0);
				if (!calculating)
					return 1;
			}
		}
	}

	if(sd->equip_index[EQI_AMMO] >= 0){
		index = sd->equip_index[EQI_AMMO];
		if(sd->inventory_data[index]){		// Arrows
			sd->bonus.arrow_atk += sd->inventory_data[index]->atk;
			sd->state.lr_flag = 2;
			if( sd->inventory_data[index]->look != A_THROWWEAPON )
				run_script(sd->inventory_data[index]->script,0,sd->bl.id,0);
			sd->state.lr_flag = 0;
			if (!calculating) //Abort, run_script retriggered status_calc_pc. [Skotlex]
				return 1;
		}
	}

	//Store equipment script bonuses 
	memcpy(sd->param_equip,sd->param_bonus,sizeof(sd->param_equip));
	memset(sd->param_bonus, 0, sizeof(sd->param_bonus));

	status->def += (refinedef+50)/100;

	//Parse Cards
	for(i=0;i<EQI_MAX;i++) {
		current_equip_item_index = index = sd->equip_index[i]; //We pass INDEX to current_equip_item_index - for EQUIP_SCRIPT (new cards solution) [Lupus]
		if(index < 0)
			continue;
		if (i == EQI_AMMO)
			continue;
		if (pc_is_same_equip_index((enum equip_index)i, sd->equip_index, index))
			continue;

		if(sd->inventory_data[index]) {
			int j;
			struct item_data *data;

			//Card script execution.
			if(itemdb_isspecial(sd->inventory.u.items_inventory[index].card[0]))
				continue;
			for(j=0;j<MAX_SLOTS;j++){ // Uses MAX_SLOTS to support Soul Bound system [Inkfish]
				int c = sd->inventory.u.items_inventory[index].card[j];
				current_equip_card_id = c;
				if(!c)
					continue;
				data = itemdb_exists(c);
				if(!data)
					continue;
				if(opt&SCO_FIRST && data->equip_script && (!itemdb_isNoEquip(data, sd->bl.m)))
			  	{	//Execute equip-script on login
					run_script(data->equip_script,0,sd->bl.id,0);
					if (!calculating)
						return 1;
				}
				if(!data->script)
					continue;
				if (itemdb_isNoEquip(data, sd->bl.m)) //Card restriction checks.
					continue;
				if(i == EQI_HAND_L && sd->inventory.u.items_inventory[index].equip == EQP_HAND_L)
				{	//Left hand status.
					sd->state.lr_flag = 1;
					run_script(data->script,0,sd->bl.id,0);
					sd->state.lr_flag = 0;
				} else
					run_script(data->script,0,sd->bl.id,0);
				if (!calculating) //Abort, run_script his function. [Skotlex]
					return 1;
			}
		}
	}
	current_equip_card_id = 0; // Clear stored card ID [Secret]

	// Parse random options
	for (i = 0; i < EQI_MAX; i++) {
		current_equip_item_index = index = sd->equip_index[i];
		current_equip_opt_index = -1;
		if (index < 0)
			continue;
		if (i == EQI_AMMO)
			continue;
		if (pc_is_same_equip_index((enum equip_index)i, sd->equip_index, index))
			continue;
		
		if (sd->inventory_data[index]) {
			int j;
			struct s_random_opt_data *data;
			for (j = 0; j < MAX_ITEM_RDM_OPT; j++) {
				short opt_id = sd->inventory.u.items_inventory[index].option[j].id;

				if (!opt_id)
					continue;
				current_equip_opt_index = j;
				data = itemdb_randomopt_exists(opt_id);
				if (!data || !data->script)
					continue;
				if (pc_isGM(sd) < battle_config.gm_allequip)
					continue;
				if (i == EQI_HAND_L && sd->inventory.u.items_inventory[index].equip == EQP_HAND_L) { // Left hand status.
					sd->state.lr_flag = 1;
					run_script(data->script, 0, sd->bl.id, 0);
					sd->state.lr_flag = 0;
				}
				else
					run_script(data->script, 0, sd->bl.id, 0);
				if (!calculating)
					return 1;
			}
		}
		current_equip_opt_index = -1;
	}

	if (sc->count && sc->data[SC_ITEMSCRIPT])
	{
		struct item_data *data = itemdb_exists(sc->data[SC_ITEMSCRIPT]->val1);
		if( data && data->script )
			run_script(data->script,0,sd->bl.id,0);
	}

	pc_bonus_script(sd);

	if( sd->pd )
	{ // Pet Bonus
		struct pet_data *pd = sd->pd;
		if( pd && pd->petDB && pd->petDB->pet_loyal_script && pd->pet.intimate >= battle_config.pet_equip_min_friendly )
			run_script(pd->petDB->pet_loyal_script,0,sd->bl.id,0);
		if( pd && pd->pet.intimate > 0 && (!battle_config.pet_equip_required || pd->pet.equip > 0) && pd->state.skillbonus == 1 && pd->bonus )
			pc_bonus(sd,pd->bonus->type, pd->bonus->val);
	}

	//param_bonus now holds card bonuses.
	if(status->rhw.range < 1) status->rhw.range = 1;
	if(status->lhw.range < 1) status->lhw.range = 1;
	if(status->rhw.range < status->lhw.range)
		status->rhw.range = status->lhw.range;

	sd->bonus.double_rate += sd->bonus.double_add_rate;
	sd->bonus.perfect_hit += sd->bonus.perfect_hit_add;
	sd->bonus.splash_range += sd->bonus.splash_add_range;

	// Damage modifiers from weapon type
	sd->right_weapon.atkmods[0] = atkmods[0][sd->weapontype1];
	sd->right_weapon.atkmods[1] = atkmods[1][sd->weapontype1];
	sd->right_weapon.atkmods[2] = atkmods[2][sd->weapontype1];
	sd->left_weapon.atkmods[0] = atkmods[0][sd->weapontype2];
	sd->left_weapon.atkmods[1] = atkmods[1][sd->weapontype2];
	sd->left_weapon.atkmods[2] = atkmods[2][sd->weapontype2];

	if ((pc_isriding(sd) || pc_isdragon(sd)) && (sd->status.weapon == W_1HSPEAR || sd->status.weapon == W_2HSPEAR))
	{	//When Riding with spear, damage modifier to mid-class becomes 
		//same as versus large size.
		sd->right_weapon.atkmods[1] = sd->right_weapon.atkmods[2];
		sd->left_weapon.atkmods[1] = sd->left_weapon.atkmods[2];
		//Damage from players mounted on a dragon will have no penaltys on all sizes.
		if (pc_isdragon(sd))
		{//Makes damage to small size become the same as large size.
			sd->right_weapon.atkmods[0] = sd->right_weapon.atkmods[2];
			sd->left_weapon.atkmods[0] = sd->left_weapon.atkmods[2];
		}
	}

// ----- STATS CALCULATION -----

	// Job bonuses
	index = pc_class2idx(sd->status.class_);
	for(i=0;i<(int)sd->status.job_level && i<MAX_LEVEL;i++)
	{
		if(!job_info[index].job_bonus[i])
			continue;
		switch(job_info[index].job_bonus[i])
		{
			case 1: status->str++; break;
			case 2: status->agi++; break;
			case 3: status->vit++; break;
			case 4: status->int_++; break;
			case 5: status->dex++; break;
			case 6: status->luk++; break;
		}
	}

	// If a Super Novice has never died and is at least joblv 70, he gets all stats +10
	if (((sd->class_&MAPID_UPPERMASK) == MAPID_SUPER_NOVICE && sd->status.job_level >= 70 || (sd->class_&MAPID_THIRDMASK) == MAPID_SUPER_NOVICE_E) && sd->die_counter == 0) {
		status->str += 10;
		status->agi += 10;
		status->vit += 10;
		status->int_+= 10;
		status->dex += 10;
		status->luk += 10;
	}

	// Absolute modifiers from passive skills
	if(pc_checkskill(sd,BS_HILTBINDING)>0)
		status->str++;
	if((skill=pc_checkskill(sd,SA_DRAGONOLOGY))>0)
		status->int_ += (skill+1)/2; // +1 INT / 2 lv
	if((skill=pc_checkskill(sd,AC_OWL))>0)
		status->dex += skill;
	if((skill=pc_checkskill(sd,RA_RESEARCHTRAP))>0)
		status->int_ += skill;
	if (pc_checkskill(sd, SU_POWEROFLAND) > 0)
		status->int_ += 20;

	// Bonuses from cards and equipment as well as base stat, remember to avoid overflows.
	i = status->str + sd->status.str + sd->param_bonus[0] + sd->param_equip[0];
	status->str = cap_value(i,0,USHRT_MAX);
	i = status->agi + sd->status.agi + sd->param_bonus[1] + sd->param_equip[1];
	status->agi = cap_value(i,0,USHRT_MAX);
	i = status->vit + sd->status.vit + sd->param_bonus[2] + sd->param_equip[2];
	status->vit = cap_value(i,0,USHRT_MAX);
	i = status->int_+ sd->status.int_+ sd->param_bonus[3] + sd->param_equip[3];
	status->int_ = cap_value(i,0,USHRT_MAX);
	i = status->dex + sd->status.dex + sd->param_bonus[4] + sd->param_equip[4];
	status->dex = cap_value(i,0,USHRT_MAX);
	i = status->luk + sd->status.luk + sd->param_bonus[5] + sd->param_equip[5];
	status->luk = cap_value(i,0,USHRT_MAX);

// ------ BASE ATTACK CALCULATION ------

	// Base batk value is set on status_calc_misc
	// weapon-type bonus (FIXME: Why is the weapon_atk bonus applied to base attack?)
	if (sd->status.weapon < MAX_WEAPON_TYPE && sd->weapon_atk[sd->status.weapon])
		status->batk += sd->weapon_atk[sd->status.weapon];
	// Absolute modifiers from passive skills
	if((skill=pc_checkskill(sd,BS_HILTBINDING))>0)
		status->batk += 4;
	if( (skill = pc_checkskill(sd,NV_BREAKTHROUGH)) > 0 )
	{
		status->batk += 15 * skill;

		if ( skill >= 5 )
			status->batk += 25;
	}

// ----- HP MAX CALCULATION -----

	status->max_hp = status_calc_maxhpsp_pc(sd, sd->battle_status.vit, true);

	if(battle_config.hp_rate != 100)
		status->max_hp = (unsigned int)(battle_config.hp_rate * (status->max_hp / 100.));

	if (sd->status.base_level < 100)
		status->max_hp = cap_value(status->max_hp, 1, (unsigned int)battle_config.max_hp_lv99);
	else if (sd->status.base_level < 151)
		status->max_hp = cap_value(status->max_hp, 1, (unsigned int)battle_config.max_hp_lv150);
	else
		status->max_hp = cap_value(status->max_hp, 1, (unsigned int)battle_config.max_hp);

// ----- SP MAX CALCULATION -----
	status->max_sp = status_calc_maxhpsp_pc(sd, sd->battle_status.int_, false);

	if(battle_config.sp_rate != 100)
		status->max_sp = (unsigned int)(battle_config.sp_rate * (status->max_sp / 100.));

	status->max_sp = cap_value(status->max_sp, 1, (unsigned int)battle_config.max_sp);

// ----- RESPAWN HP/SP -----
// 
	//Calc respawn hp and store it on base_status
	if (sd->special_state.restart_full_recover)
	{
		status->hp = status->max_hp;
		status->sp = status->max_sp;
	} else {
		if((sd->class_&MAPID_BASEMASK) == MAPID_NOVICE && !(sd->class_&JOBL_2) 
			&& battle_config.restart_hp_rate < 50) 
			status->hp=status->max_hp>>1;
		else 
			status->hp=(int64)status->max_hp * battle_config.restart_hp_rate/100;
		if(!status->hp)
			status->hp = 1;

		status->sp = (int64)status->max_sp * battle_config.restart_sp_rate /100;

		if (!status->sp) // The minimum for the respawn setting is SP:1
			status->sp = 1;
	}

// ----- MISC CALCULATION -----
	status_calc_misc(&sd->bl, status, sd->status.base_level);

	// Passive skills that increase matk.
	if( (skill = pc_checkskill(sd,NV_TRANSCENDENCE)) > 0 )
	{
		passive_add_matk += 15 * skill;

		if ( skill >= 5 )
			passive_add_matk += 25;
	}

	//Equipment modifiers for misc settings
	if (sd->bonus.matk + passive_add_matk > 0)
	{	//Increases MATK by a fixed amount. [15peaces]
		status->matk_min += sd->bonus.matk + passive_add_matk;
		status->matk_max += sd->bonus.matk + passive_add_matk;
	}

	if(sd->matk_rate < 0)
		sd->matk_rate = 0;
	// Passive skills that increase matk rate.
	if ( skill_summoner_power(sd, POWER_OF_LAND) == 1 )
		passive_matk_rate += 20;
	if( (sd->matk_rate+passive_matk_rate) != 100 )
	{
		status->matk_max = status->matk_max * (sd->matk_rate + passive_matk_rate) / 100;
		status->matk_min = status->matk_min * (sd->matk_rate + passive_matk_rate) / 100;
	}

	if(sd->hit_rate < 0)
		sd->hit_rate = 0;
	if(sd->hit_rate != 100)
		status->hit = status->hit * sd->hit_rate/100;

	if(sd->flee_rate < 0)
		sd->flee_rate = 0;
	if(sd->flee_rate != 100)
		status->flee = status->flee * sd->flee_rate/100;

	if(sd->def2_rate < 0)
		sd->def2_rate = 0;
	if(sd->def2_rate != 100)
		status->def2 = status->def2 * sd->def2_rate/100;

	if(sd->mdef2_rate < 0)
		sd->mdef2_rate = 0;
	if(sd->mdef2_rate != 100)
		status->mdef2 = status->mdef2 * sd->mdef2_rate/100;

	if(sd->critical_rate < 0) 
		sd->critical_rate = 0;
	if(sd->critical_rate != 100)
		status->cri = cap_value(status->cri * sd->critical_rate / 100, SHRT_MIN, SHRT_MAX);

	if(sd->flee2_rate < 0)
		sd->flee2_rate = 0;
	if(sd->flee2_rate != 100)
		status->flee2 = status->flee2 * sd->flee2_rate/100;

// ----- HIT CALCULATION -----

	// Absolute modifiers from passive skills
	if((skill=pc_checkskill(sd,BS_WEAPONRESEARCH))>0)
		status->hit += skill*2;
	if((skill=pc_checkskill(sd,AC_VULTURE))>0){
		status->hit += skill;
		if(sd->status.weapon == W_BOW)
			status->rhw.range += skill;
	}
	if(sd->status.weapon >= W_REVOLVER && sd->status.weapon <= W_GRENADE)
  	{
		if((skill=pc_checkskill(sd,GS_SINGLEACTION))>0)
			status->hit += 2*skill;
		if((skill=pc_checkskill(sd,GS_SNAKEEYE))>0) {
			status->hit += skill;
			status->rhw.range += skill;
		}
	}
	if( (skill = pc_checkskill(sd,NC_TRAININGAXE)) > 0 )
	{
		if ( sd->status.weapon == W_1HAXE || sd->status.weapon == W_2HAXE )
			status->hit += skill * 3;
		else if ( sd->status.weapon == W_MACE || sd->status.weapon == W_2HMACE )
			status->hit += skill * 2;
	}
	if( pc_checkskill(sd,SU_POWEROFLIFE) > 0 )
		status->hit += 20;
	if( (pc_checkskill(sd,SU_SOULATTACK)) > 0 )
	{// Range with rod type weapons increased to a fixed 14.
		if( sd->status.weapon == W_STAFF )
			status->rhw.range = 14;
	}

// ----- FLEE CALCULATION -----

	// Absolute modifiers from passive skills
	if((skill=pc_checkskill(sd,TF_MISS))>0)
		status->flee += skill*(sd->class_&JOBL_2 && (sd->class_&MAPID_BASEMASK) == MAPID_THIEF? 4 : 3);
	if((skill=pc_checkskill(sd,MO_DODGE))>0)
		status->flee += (skill*3)>>1;
	if( pc_checkskill(sd,SU_POWEROFLIFE)>0)
		status->flee += 20;

// ----- CRIT CALCULATION -----

	// Absolute modifiers from passive skills
	// 10 = 1 crit or 1.0 crit.
	if(pc_checkskill(sd,SU_POWEROFLIFE)>0)
		status->cri += 200;

// ----- EQUIPMENT-DEF CALCULATION -----

	// Apply relative modifiers from equipment
	if(sd->def_rate < 0)
		sd->def_rate = 0;
	if(sd->def_rate != 100) {
		i =  status->def * sd->def_rate/100;
		status->def = cap_value(i, CHAR_MIN, CHAR_MAX);
	}

	if( pc_ismadogear(sd) && (skill = pc_checkskill(sd,NC_MAINFRAME)) > 0 )
		status->def += 2 + 2 * skill;

	if (!battle_config.weapon_defense_type && status->def > battle_config.max_def)
	{
		status->def2 += battle_config.over_def_bonus*(status->def -battle_config.max_def);
		status->def = (unsigned char)battle_config.max_def;
	}

// ----- EQUIPMENT-MDEF CALCULATION -----

	// Apply relative modifiers from equipment
	if(sd->mdef_rate < 0)
		sd->mdef_rate = 0;
	if(sd->mdef_rate != 100) {
		i =  status->mdef * sd->mdef_rate/100;
		status->mdef = cap_value(i, CHAR_MIN, CHAR_MAX);
	}

	if (!battle_config.magic_defense_type && status->mdef > battle_config.max_def)
	{
		status->mdef2 += battle_config.over_def_bonus*(status->mdef -battle_config.max_def);
		status->mdef = (signed char)battle_config.max_def;
	}

// ----- ASPD CALCULATION -----
// Unlike other stats, ASPD rate modifiers from skills/SCs/items/etc are first all added together, then the final modifier is applied

	// Basic ASPD value
	i = status_base_amotion_pc(sd,status);
	status->amotion = cap_value(i,battle_config.max_aspd,2000);

	// Config for setting seprate ASPD cap for 3rd jobs and other jobs released in renewal.
	if ( sd && ((sd->class_&MAPID_THIRDMASK) >= MAPID_SUPER_NOVICE_E && (sd->class_&MAPID_THIRDMASK) <= MAPID_SOUL_REAPER || (sd->class_&MAPID_UPPERMASK) == MAPID_KAGEROUOBORO || (sd->class_&MAPID_UPPERMASK) == MAPID_REBELLION || (sd->class_&MAPID_BASEMASK) == MAPID_SUMMONER))
		status->amotion = cap_value(i,battle_config.max_aspd_renewal_jobs,2000);

	// Relative modifiers from passive skills
	if((skill=pc_checkskill(sd,SA_ADVANCEDBOOK))>0 && sd->status.weapon == W_BOOK)
		status->aspd_rate -= 5*skill;
	if ((skill = pc_checkskill(sd, SG_DEVIL)) > 0 && ((sd->class_&MAPID_THIRDMASK) == MAPID_STAR_EMPEROR || sd->status.job_level >= 50))
		status->aspd_rate -= 30*skill;
	if((skill=pc_checkskill(sd,GS_SINGLEACTION))>0 &&
		(sd->status.weapon >= W_REVOLVER && sd->status.weapon <= W_GRENADE))
		status->aspd_rate -= ((skill+1)/2) * 10;
	if(pc_isriding(sd))
		status->aspd_rate += 500-100*pc_checkskill(sd,KN_CAVALIERMASTERY);
	if(pc_isdragon(sd))
		status->aspd_rate += 250-50*pc_checkskill(sd,RK_DRAGONTRAINING);

	if (sc->data[SC_OVERED_BOOST])// Set to a fixed ASPD value.
		status->amotion = sc->data[SC_OVERED_BOOST]->val3;

	status->adelay = 2*status->amotion;


// ----- DMOTION -----
//
	i =  800-status->agi*4;
	status->dmotion = cap_value(i, 400, 800);
	if(battle_config.pc_damage_delay_rate != 100)
		status->dmotion = status->dmotion*battle_config.pc_damage_delay_rate/100;

// ----- MISC CALCULATIONS -----

	// Weight
	status_calc_weight(sd, 2);

	sd->cart_weight_max = battle_config.max_cart_weight + (pc_checkskill(sd, GN_REMODELING_CART) * 5000);

	if (pc_checkskill(sd,SM_MOVINGRECOVERY)>0)
		sd->regen.state.walk = 1;
	else
		sd->regen.state.walk = 0;

	// Skill SP cost
	if((skill=pc_checkskill(sd,HP_MANARECHARGE))>0 )
		sd->dsprate -= 4*skill;

	if(sc->data[SC_SERVICE4U])
		sd->dsprate -= sc->data[SC_SERVICE4U]->val3;

	if(sc->data[SC_SPCOST_RATE])
		sd->dsprate -= sc->data[SC_SPCOST_RATE]->val1;

	//Underflow protections.
	if(sd->dsprate < 0)
		sd->dsprate = 0;
	if(sd->castrate < 0)
		sd->castrate = 0;
	if(sd->fixedcastrate < 0)
		sd->fixedcastrate = 0;
	if(sd->delayrate < 0)
		sd->delayrate = 0;
	if (sd->cooldownrate < 0)
		sd->cooldownrate = 0;
	if(sd->hprecov_rate < 0)
		sd->hprecov_rate = 0;
	if(sd->sprecov_rate < 0)
		sd->sprecov_rate = 0;

	if((skill=pc_checkskill(sd,SA_DRAGONOLOGY))>0 ){
		skill = skill*4;
		sd->right_weapon.addrace[RC_DRAGON]+=skill;
		sd->left_weapon.addrace[RC_DRAGON]+=skill;
		sd->magic_addrace[RC_DRAGON]+=skill;
		sd->subrace[RC_DRAGON]+=skill;
	}

	if ((skill = pc_checkskill(sd, AB_EUCHARISTICA)) > 0)
	{
		sd->right_weapon.addrace[RC_DEMON] += skill;
		sd->left_weapon.addrace[RC_DEMON] += skill;
		sd->magic_addrace[RC_DEMON] += skill;
		sd->subrace[RC_DEMON] += skill;
	}

	if(sc->count) {
     	if(sc->data[SC_CONCENTRATE])
		{	//Update the card-bonus data
			sc->data[SC_CONCENTRATE]->val3 = sd->param_bonus[1]; //Agi
			sc->data[SC_CONCENTRATE]->val4 = sd->param_bonus[4]; //Dex
		}
		if(sc->data[SC_PROVIDENCE]){
			sd->subrace[RC_DEMON] += sc->data[SC_PROVIDENCE]->val2;
		}
		if(sc->data[SC_ARMOR_RESIST])
		{ // Undead Scroll
			sd->subele[ELE_WATER] += sc->data[SC_ARMOR_RESIST]->val1;
			sd->subele[ELE_EARTH] += sc->data[SC_ARMOR_RESIST]->val2;
			sd->subele[ELE_FIRE] += sc->data[SC_ARMOR_RESIST]->val3;
			sd->subele[ELE_WIND] += sc->data[SC_ARMOR_RESIST]->val4;
		}
		if (sc->data[SC_MTF_CRIDAMAGE])
			sd->bonus.crit_atk_rate += sc->data[SC_MTF_CRIDAMAGE]->val1;
	}

	status_cpy(&sd->battle_status, status);

// ----- CLIENT-SIDE REFRESH -----
	if(!sd->bl.prev) {
		//Will update on LoadEndAck
		calculating = 0;
		return 0;
	}
	if(memcmp(b_skill,sd->status.skill,sizeof(sd->status.skill)))
		clif_skillupdateinfoblock(sd);

	// Spirit Marble status activates automatically for a infinite
	// amount of time when the skill is learned. Felt this was the
	// best place to put this. [Rytech]
	if ((skill = pc_checkskill(sd, SU_SPRITEMABLE)) > 0)
		sc_start(&sd->bl, SC_SPRITEMABLE, 100, 1, -1);

	// Same as above, but for Soul Attack.
	if( (skill = pc_checkskill(sd,SU_SOULATTACK)) > 0 )
		sc_start(&sd->bl,SC_SOULATTACK,100,1,-1);

	if (b_cart_weight_max != sd->cart_weight_max) {
		clif_updatestatus(sd, SP_CARTINFO);
	}

	calculating = 0;

	return 0;
}

/**
 * Calculate attack bonus of element attack for BL_PC.
 * Any SC that listed here, has minimal SCB_ATK_ELE flag.
 * @param sd
 * @param sc
 **/
void status_calc_atk_ele_pc(struct map_session_data *sd, struct status_change *sc) {
	int i = 0;
	nullpo_retv(sd);
	memset(sd->magic_addele, 0, sizeof(sd->magic_addele));
	memset(sd->right_weapon.addele, 0, sizeof(sd->right_weapon.addele));
	memset(sd->left_weapon.addele, 0, sizeof(sd->left_weapon.addele));
	if ((i = pc_checkskill(sd, AB_EUCHARISTICA)) > 0) {
		sd->right_weapon.addele[ELE_DARK] += i;
		sd->left_weapon.addele[ELE_DARK] += i;
		sd->magic_addele[ELE_DARK] += i;
	}
	if (!sc || !sc->count)
		return;
	if (sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 3)
		sd->magic_addele[ELE_FIRE] += 25;
	if (sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 3)
		sd->magic_addele[ELE_WATER] += 25;
	if (sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 3)
		sd->magic_addele[ELE_WIND] += 25;
	if (sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 3)
		sd->magic_addele[ELE_EARTH] += 25;
}
/**
 * Calculate defense bonus againts element attack for BL_PC.
 * Any SC that listed here, has minimal SCB_DEF_ELE flag.
 * @param sd
 * @param sc
 **/
void status_calc_def_ele_pc(struct map_session_data *sd, struct status_change *sc) {
	int i = 0;
	nullpo_retv(sd);
	memset(sd->subele, 0, sizeof(sd->subele));
	if ((i = pc_checkskill(sd, CR_TRUST)) > 0)
		sd->subele[ELE_HOLY] += i * 5;
	if ((i = pc_checkskill(sd, BS_SKINTEMPER)) > 0) {
		sd->subele[ELE_NEUTRAL] += i;
		sd->subele[ELE_FIRE] += i * 4;
	}
	if ((i = pc_checkskill(sd, AB_EUCHARISTICA)) > 0)
		sd->subele[ELE_DARK] += i;
	if (!sc || !sc->count)
		return;
	if (sc->data[SC_SIEGFRIED]) {
		i = sc->data[SC_SIEGFRIED]->val2;
		sd->subele[ELE_WATER] += i;
		sd->subele[ELE_EARTH] += i;
		sd->subele[ELE_FIRE] += i;
		sd->subele[ELE_WIND] += i;
		sd->subele[ELE_POISON] += i;
		sd->subele[ELE_HOLY] += i;
		sd->subele[ELE_DARK] += i;
		sd->subele[ELE_GHOST] += i;
		sd->subele[ELE_UNDEAD] += i;
	}
	if (sc->data[SC_PROVIDENCE])
		sd->subele[ELE_HOLY] += sc->data[SC_PROVIDENCE]->val2;
	if (sc->data[SC_ARMOR_ELEMENT_WATER])
	{	// This status change should grant card-type elemental resist.
		sd->subele[ELE_WATER] += sc->data[SC_ARMOR_ELEMENT_WATER]->val1;
		sd->subele[ELE_EARTH] += sc->data[SC_ARMOR_ELEMENT_WATER]->val2;
		sd->subele[ELE_FIRE] += sc->data[SC_ARMOR_ELEMENT_WATER]->val3;
		sd->subele[ELE_WIND] += sc->data[SC_ARMOR_ELEMENT_WATER]->val4;
	}
	if (sc->data[SC_ARMOR_ELEMENT_EARTH])
	{	// This status change should grant card-type elemental resist.
		sd->subele[ELE_WATER] += sc->data[SC_ARMOR_ELEMENT_EARTH]->val1;
		sd->subele[ELE_EARTH] += sc->data[SC_ARMOR_ELEMENT_EARTH]->val2;
		sd->subele[ELE_FIRE] += sc->data[SC_ARMOR_ELEMENT_EARTH]->val3;
		sd->subele[ELE_WIND] += sc->data[SC_ARMOR_ELEMENT_EARTH]->val4;
	}
	if (sc->data[SC_ARMOR_ELEMENT_FIRE])
	{	// This status change should grant card-type elemental resist.
		sd->subele[ELE_WATER] += sc->data[SC_ARMOR_ELEMENT_FIRE]->val1;
		sd->subele[ELE_EARTH] += sc->data[SC_ARMOR_ELEMENT_FIRE]->val2;
		sd->subele[ELE_FIRE] += sc->data[SC_ARMOR_ELEMENT_FIRE]->val3;
		sd->subele[ELE_WIND] += sc->data[SC_ARMOR_ELEMENT_FIRE]->val4;
	}
	if (sc->data[SC_ARMOR_ELEMENT_WIND])
	{	// This status change should grant card-type elemental resist.
		sd->subele[ELE_WATER] += sc->data[SC_ARMOR_ELEMENT_WIND]->val1;
		sd->subele[ELE_EARTH] += sc->data[SC_ARMOR_ELEMENT_WIND]->val2;
		sd->subele[ELE_FIRE] += sc->data[SC_ARMOR_ELEMENT_WIND]->val3;
		sd->subele[ELE_WIND] += sc->data[SC_ARMOR_ELEMENT_WIND]->val4;
	}
	if (sc->data[SC_ARMOR_RESIST]) { // Undead Scroll
		sd->subele[ELE_WATER] += sc->data[SC_ARMOR_RESIST]->val1;
		sd->subele[ELE_EARTH] += sc->data[SC_ARMOR_RESIST]->val2;
		sd->subele[ELE_FIRE] += sc->data[SC_ARMOR_RESIST]->val3;
		sd->subele[ELE_WIND] += sc->data[SC_ARMOR_RESIST]->val4;
	}
	if (sc->data[SC_FIRE_CLOAK_OPTION])
	{
		sd->subele[ELE_FIRE] += 100;
		sd->subele[ELE_WATER] -= 100;
	}
	if (sc->data[SC_WATER_DROP_OPTION])
	{
		sd->subele[ELE_WATER] += 100;
		sd->subele[ELE_WIND] -= 100;
	}
	if (sc->data[SC_WIND_CURTAIN_OPTION])
	{
		sd->subele[ELE_WIND] += 100;
		sd->subele[ELE_EARTH] -= 100;
	}
	if (sc->data[SC_STONE_SHIELD_OPTION])
	{
		sd->subele[ELE_EARTH] += 100;
		sd->subele[ELE_FIRE] -= 100;
	}
	if (sc->data[SC_MTF_MLEATKED])
		sd->subele[ELE_NEUTRAL] += sc->data[SC_MTF_MLEATKED]->val3;
}

int status_calc_mercenary_(struct mercenary_data *md, enum e_status_calc_opt opt)
{
	struct status_data *status = &md->base_status;
	struct s_mercenary *merc = &md->mercenary;

	if(opt&SCO_FIRST)
	{
		memcpy(status, &md->db->status, sizeof(struct status_data));
		status->class_ = CLASS_NORMAL;
		status->mode = MD_CANMOVE|MD_CANATTACK;
		status->hp = status->max_hp;
		status->sp = status->max_sp;
		md->battle_status.hp = merc->hp;
		md->battle_status.sp = merc->sp;

		if (md->master)
			status->speed = status_get_speed(&md->master->bl);
	}

	status_calc_misc(&md->bl, status, md->db->lv);
	status_cpy(&md->battle_status, status);

	return 0;
}

int status_calc_elemental_(struct elemental_data *ed, enum e_status_calc_opt opt)
{
	struct status_data *status = &ed->base_status;
	struct s_elemental *elem = &ed->elemental;

	if(opt&SCO_FIRST)
	{
		memcpy(status, &ed->db->status, sizeof(struct status_data));
		//status->def_ele =  db->element;
		//status->ele_lv = 1;
		//status->race = db->race;
		//status->size = db->size;
		//status->rhw.range = db->range;
		status->class_ = CLASS_NORMAL;
		status->mode = MD_CANMOVE;
		status->speed = DEFAULT_WALK_SPEED;

		if (battle_config.elemental_masters_walk_speed && ed->master)
			status->speed = status_get_speed(&ed->master->bl);
	}

	// Elementals don't have stats but as with monsters we must give them at least 1 of each
	// to avoid any possible divisions by 0. These won't affect their sub-stats.
	// Note: A confirm on this would be nice. How can they have any natural immunity to common status's without stats??? [Rytech]
	status->str = status->agi = status->vit = status->int_ = status->dex = status->luk = 1;

	// Zero out the sub-stats elementals arn't affected by.
	status->batk = status->def2 = status->mdef2 = status->cri = status->flee2 = 0;

	// Set sub-stats to what was given on creation/load.
	status->max_hp = elem->max_hp;
	status->max_sp = elem->max_sp;
	status->rhw.atk = status->rhw.atk2 = elem->batk;
	status->matk_min = status->matk_max = elem->matk;
	status->def = elem->def;
	status->mdef = elem->mdef;
	status->hit = elem->hit;
	status->flee = elem->flee;
	status->amotion = elem->amotion;

	if (opt&SCO_FIRST)
	{// Set current HP after Max HP/SP is set on creation/load.
		ed->battle_status.hp = elem->hp;
		ed->battle_status.sp = elem->sp;
	}

	// Set other things needed.
	status->aspd_amount = 0;
	status->aspd_rate = 1000;
	status->adelay = 2 * status->amotion;

	status_cpy(&ed->battle_status, status);

	return 0;
}

int status_calc_homunculus_(struct homun_data *hd, enum e_status_calc_opt opt)
{
	const struct status_change *sc = &hd->sc;
	struct status_data *status = &hd->base_status;
	struct s_homunculus *hom = &hd->homunculus;
	int skill;
	int amotion;

	status->str = hom->str / 10;
	status->agi = hom->agi / 10;
	status->vit = hom->vit / 10;
	status->dex = hom->dex / 10;
	status->int_ = hom->int_ / 10;
	status->luk = hom->luk / 10;

	if (opt&SCO_FIRST) {	//[orn]
		const struct s_homunculus_db *db = hd->homunculusDB;
		status->def_ele =  db->element;
		status->ele_lv = 1;
		status->race = db->race;
		status->class_ = CLASS_NORMAL;
		status->size = (hom->class_ == db->evo_class)?db->evo_size:db->base_size;
		status->rhw.range = 1 + status->size;
		status->mode = MD_CANMOVE|MD_CANATTACK;
		status->speed = DEFAULT_WALK_SPEED;
		if (battle_config.hom_setting&0x8 && hd->master)
			status->speed = status_get_speed(&hd->master->bl);

		status->hp = 1;
		status->sp = 1;
	}
	skill = hom->level/10 + status->vit/5;
	status->def = cap_value(skill, 0, 99);

	skill = hom->level/10 + status->int_/5;
	status->mdef = cap_value(skill, 0, 99);

	status->max_hp = hom->max_hp ;
	status->max_sp = hom->max_sp ;

	hom_calc_skilltree(hd, 0);

	if((skill=hom_checkskill(hd,HAMI_SKIN)) > 0)
		status->def +=	skill * 4;

	if((skill = hom_checkskill(hd,HVAN_INSTRUCT)) > 0)
	{
		status->int_ += 1 +skill/2 +skill/4 +skill/5;
		status->str  += 1 +skill/3 +skill/3 +skill/4;
	}

	if((skill=hom_checkskill(hd,HAMI_SKIN)) > 0)
		status->max_hp += skill * 2 * status->max_hp / 100;

	if((skill = hom_checkskill(hd,HLIF_BRAIN)) > 0)
		status->max_sp += (1 +skill/2 -skill/4 +skill/5) * status->max_sp / 100 ;

	if (opt&SCO_FIRST) {
		hd->battle_status.hp = hom->hp ;
		hd->battle_status.sp = hom->sp ;
	}

	status->rhw.atk = status->dex;
	status->rhw.atk2 = status->str + hom->level;

	status->aspd_amount = 0;
	status->aspd_rate = 1000;

	amotion = hd->homunculusDB->baseASPD;
	amotion -= amotion * (4 * status->agi + status->dex) / 1000;
	status->amotion = cap_value(amotion,battle_config.max_aspd,2000);

	if (sc->data[SC_OVERED_BOOST])// Set to a fixed ASPD value.
		status->amotion = sc->data[SC_OVERED_BOOST]->val3;
	
	status->adelay = 2 * status->amotion;

	status_calc_misc(&hd->bl, status, hom->level);
	status_cpy(&hd->battle_status, status);

	return 1;
}

/**
 * Calculates NPC data
 * @param nd: NPC object
 * @param opt: Whether it is first calc or not (what?)
 * @return 0
 */
int status_calc_npc_(struct npc_data *nd, enum e_status_calc_opt opt)
{
	struct status_data *status = &nd->status;

	if (!nd)
		return 0;

	if (opt&SCO_FIRST) {
		status->hp = 1;
		status->sp = 1;
		status->max_hp = 1;
		status->max_sp = 1;

		status->def_ele = ELE_NEUTRAL;
		status->ele_lv = 1;
		status->race = RC_DEMIHUMAN;
		status->class_ = CLASS_NORMAL;
		status->size = nd->size;
		status->rhw.range = 1 + status->size;
		status->mode = (MD_CANMOVE|MD_CANATTACK);
		status->speed = nd->speed;
	}

	status->str = nd->stat_point + nd->params.str;
	status->agi = nd->stat_point + nd->params.agi;
	status->vit = nd->stat_point + nd->params.vit;
	status->int_ = nd->stat_point + nd->params.int_;
	status->dex = nd->stat_point + nd->params.dex;
	status->luk = nd->stat_point + nd->params.luk;

	status_calc_misc(&nd->bl, status, nd->level);
	status_cpy(&nd->status, status);

	return 0;
}

//Calculates base regen values.
void status_calc_regen(struct block_list *bl, struct status_data *status, struct regen_data *regen)
{
	struct map_session_data *sd;
	int val, skill;

	if( !(bl->type&BL_REGEN) || !regen )
		return;

	sd = BL_CAST(BL_PC,bl);
	val = 1 + (status->vit/5) + (status->max_hp/200);

	if( sd && sd->hprecov_rate != 100 )
		val = val*sd->hprecov_rate/100;

	regen->hp = cap_value(val, 1, SHRT_MAX);

	val = 1 + (status->int_/6) + (status->max_sp/100);
	if( status->int_ >= 120 )
		val += ((status->int_-120)>>1) + 4;

	if( sd && sd->sprecov_rate != 100 )
		val = val*sd->sprecov_rate/100;

	regen->sp = cap_value(val, 1, SHRT_MAX);

	if( sd )
	{
		struct regen_data_sub *sregen;
		if (pc_ismadogear(sd))
		{	// Mado's get a increased HP regen but I don't know the official HP regen increase.
			// Lets just double it for now. Won't hurt anything. [Rytech]
			val = regen->hp*2;
			regen->hp = cap_value(val, 1, SHRT_MAX);
		}
		if( (skill=pc_checkskill(sd,HP_MEDITATIO)) > 0 )
		{
			val = regen->sp*(100+3*skill)/100;
			regen->sp = cap_value(val, 1, SHRT_MAX);
		}
		//Only players have skill/sitting skill regen for now.
		sregen = regen->sregen;

		val = 0;
		if( (skill=pc_checkskill(sd,SM_RECOVERY)) > 0 )
			val += skill*5 + skill*status->max_hp/500;
		sregen->hp = cap_value(val, 0, SHRT_MAX);

		val = 0;
		if( (skill=pc_checkskill(sd,MG_SRECOVERY)) > 0 )
			val += skill*3 + skill*status->max_sp/500;
		if( (skill=pc_checkskill(sd,NJ_NINPOU)) > 0 )
			val += skill*3 + skill*status->max_sp/500;
		if( (skill=pc_checkskill(sd,WM_LESSON)) > 0 )
			val += skill * 3 + skill*status->max_sp / 500;
		sregen->sp = cap_value(val, 0, SHRT_MAX);

		// Skill-related recovery (only when sit)
		sregen = regen->ssregen;

		val = 0;
		if( (skill=pc_checkskill(sd,MO_SPIRITSRECOVERY)) > 0 )
			val += skill*4 + skill*status->max_hp/500;

		if( (skill=pc_checkskill(sd,TK_HPTIME)) > 0 && sd->state.rest )
			val += skill*30 + skill*status->max_hp/500;
		sregen->hp = cap_value(val, 0, SHRT_MAX);

		val = 0;
		if( (skill=pc_checkskill(sd,TK_SPTIME)) > 0 && sd->state.rest )
		{
			val += skill*3 + skill*status->max_sp/500;
			if ((skill=pc_checkskill(sd,SL_KAINA)) > 0) //Power up Enjoyable Rest
				val += (30+10*skill)*val/100;
		}
		if( (skill=pc_checkskill(sd,MO_SPIRITSRECOVERY)) > 0 )
			val += skill*2 + skill*status->max_sp/500;
		sregen->sp = cap_value(val, 0, SHRT_MAX);
	}

	if( bl->type == BL_HOM )
	{
		struct homun_data *hd = (TBL_HOM*)bl;
		if( (skill = hom_checkskill(hd,HAMI_SKIN)) > 0 )
		{
			val = regen->hp*(100+5*skill)/100;
			regen->hp = cap_value(val, 1, SHRT_MAX);
		}
		if( (skill = hom_checkskill(hd,HLIF_BRAIN)) > 0 )
		{
			val = regen->sp*(100+3*skill)/100;
			regen->sp = cap_value(val, 1, SHRT_MAX);
		}
	} else if( bl->type == BL_MER ) {
		val = (status->max_hp * status->vit / 10000 + 1) * 6;
		regen->hp = cap_value(val, 1, SHRT_MAX);

		val = (status->max_sp * (status->int_ + 10) / 750) + 1;
		regen->sp = cap_value(val, 1, SHRT_MAX);
	}
}

//Calculates SC related regen rates.
void status_calc_regen_rate(struct block_list *bl, struct regen_data *regen, struct status_change *sc)
{
	if (!(bl->type&BL_REGEN) || !regen)
		return;

	regen->flag = RGN_HP|RGN_SP;
	if(regen->sregen)
	{
		if (regen->sregen->hp)
			regen->flag|=RGN_SHP;

		if (regen->sregen->sp)
			regen->flag|=RGN_SSP;
		regen->sregen->rate.hp = regen->sregen->rate.sp = 100;
	}
	if (regen->ssregen)
	{
		if (regen->ssregen->hp)
			regen->flag|=RGN_SHP;

		if (regen->ssregen->sp)
			regen->flag|=RGN_SSP;
		regen->ssregen->rate.hp = regen->ssregen->rate.sp = 100;
	}
	regen->rate.hp = regen->rate.sp = 100;

	if (!sc || !sc->count)
		return;

	if (
		(sc->data[SC_POISON] && !sc->data[SC_SLOWPOISON])
		|| (sc->data[SC_DPOISON] && !sc->data[SC_SLOWPOISON])
		|| sc->data[SC_BERSERK]
		|| sc->data[SC_TRICKDEAD]
		|| sc->data[SC_BLEEDING]
		|| sc->data[SC_MAGICMUSHROOM]
		|| sc->data[SC_RAISINGDRAGON]
		|| sc->data[SC_SATURDAY_NIGHT_FEVER]
		|| sc->data[SC_AKAITSUKI]
		|| sc->data[SC_REBOUND]
	)	//No regen
		regen->flag = 0;

	if (
		sc->data[SC_DANCING]
		|| (
			bl->type == BL_PC && (((TBL_PC*)bl)->class_&MAPID_UPPERMASK) == MAPID_MONK &&
			(sc->data[SC_EXTREMITYFIST] || (sc->data[SC_EXPLOSIONSPIRITS] && (!sc->data[SC_SPIRIT] || sc->data[SC_SPIRIT]->val2 != SL_MONK)))
			)
		|| sc->data[SC_VITALITYACTIVATION]
		|| sc->data[SC_OBLIVIONCURSE]
	)	//No natural SP regen
		regen->flag &=~RGN_SP;

	if(
		sc->data[SC_TENSIONRELAX]
	  ) {
		regen->rate.hp += 200;
		if (regen->sregen)
			regen->sregen->rate.hp += 300;
	}
	if (sc->data[SC_MAGNIFICAT])
		regen->rate.sp += 100;

	if (sc->data[SC_CATNIPPOWDER])
	{// Rate increase is unknown. Also not sure if this stacks with other increases. [Rytech]
		regen->rate.hp += 100;
		regen->rate.sp += 100;
	}

	// iRO document says it increase HP recovery by 50% but I don't see that in aegis. Needs testing. [Rytech]
	if (sc->data[SC_BANDING])
		regen->rate.hp += 50;

	if( sc->data[SC_GENTLETOUCH_REVITALIZE] )// 50 + 30 * SkillLV
		regen->rate.hp += sc->data[SC_GENTLETOUCH_REVITALIZE]->val3;

	if( sc->data[SC_EXTRACT_WHITE_POTION_Z] )// 20
		regen->rate.hp += sc->data[SC_EXTRACT_WHITE_POTION_Z]->val1;

	if( sc->data[SC_VITATA_500] )// 20
		regen->rate.sp += sc->data[SC_VITATA_500]->val1;

	if (sc->data[SC_REGENERATION])
	{
		const struct status_change_entry *sce = sc->data[SC_REGENERATION];
		if (!sce->val4)
		{
			regen->rate.hp += 100 * sce->val2;
			regen->rate.sp += 100 * sce->val3;
		} else
			regen->flag&=~sce->val4; //Remove regen as specified by val4
	}

	// Should not be here, I guess... [15peaces]
	//if (sc->data[SC_EPICLESIS]) {
	//	regen->rate.hp += sc->data[SC_EPICLESIS]->val3;
	//	regen->rate.sp += sc->data[SC_EPICLESIS]->val4;
	//}
}

/// Recalculates parts of an object's battle status according to the specified flags.
/// @param flag bitfield of values from enum scb_flag
void status_calc_bl_main(struct block_list *bl, /*enum scb_flag*/int flag)
{
	const struct status_data *b_status = status_get_base_status(bl); // Base Status
	struct status_data *status = status_get_status_data(bl); // Battle Status
	struct status_change *sc = status_get_sc(bl);
	TBL_PC *sd = BL_CAST(BL_PC,bl);
	int temp;

	if (!b_status || !status)
		return;

	/** [Playtester]
	* This needs to be done even if there is currently no status change active, because
	* we need to update the speed on the client when the last status change ends.
	**/
	if (flag&SCB_SPEED) {
		struct unit_data *ud = unit_bl2ud(bl);
		/** [Skotlex]
		* Re-walk to adjust speed (we do not check if walktimer != INVALID_TIMER
		* because if you step on something while walking, the moment this
		* piece of code triggers the walk-timer is set on INVALID_TIMER)
		**/
		if (ud)
			ud->state.change_walk_target = ud->state.speed_changed = 1;
	}

	if ((!(bl->type&BL_REGEN)) && (!sc || !sc->count)) 
	{ //No difference.
		status_cpy(status, b_status);
		return;
	}

	if (flag&SCB_STR)
	{
		status->str = status_calc_str(bl, sc, b_status->str);
		flag|=SCB_BATK;
		if (bl->type&BL_HOM)
			flag |= SCB_WATK;
	}

	if (flag&SCB_AGI)
	{
		status->agi = status_calc_agi(bl, sc, b_status->agi);
		flag|=SCB_FLEE;
		if( bl->type&(BL_PC|BL_HOM) )
			flag |= SCB_ASPD|SCB_DSPD;
	}

	if(flag&SCB_VIT) {
		status->vit = status_calc_vit(bl, sc, b_status->vit);
		flag|=SCB_DEF2|SCB_MDEF2;
		if( bl->type&(BL_PC|BL_HOM|BL_MER|BL_ELEM) )
			flag |= SCB_MAXHP;
		if( bl->type&BL_HOM )
			flag |= SCB_DEF;
	}

	if(flag&SCB_INT) {
		status->int_ = status_calc_int(bl, sc, b_status->int_);
		flag|=SCB_MATK|SCB_MDEF2;
		if( bl->type&(BL_PC|BL_HOM|BL_MER|BL_ELEM) )
			flag |= SCB_MAXSP;
		if( bl->type&BL_HOM )
			flag |= SCB_MDEF;
	}

	if(flag&SCB_DEX) {
		status->dex = status_calc_dex(bl, sc, b_status->dex);
		flag|=SCB_BATK|SCB_HIT;
		if( bl->type&(BL_PC|BL_HOM) )
			flag |= SCB_ASPD;
		if( bl->type&BL_HOM )
			flag |= SCB_WATK;
	}

	if(flag&SCB_LUK) {
		status->luk = status_calc_luk(bl, sc, b_status->luk);
		flag|=SCB_BATK|SCB_CRI|SCB_FLEE2;
	}

	if(flag&SCB_BATK && b_status->batk) {
		status->batk = status_base_atk(bl,status);
		temp = b_status->batk - status_base_atk(bl,b_status);
		if (temp)
		{
			temp += status->batk;
			status->batk = cap_value(temp, 0, USHRT_MAX);
		}
		status->batk = status_calc_batk(bl, sc, status->batk);
	}

	if(flag&SCB_WATK) {

		status->rhw.atk = status_calc_watk(bl, sc, b_status->rhw.atk);
		if (!sd) //Should not affect weapon refine bonus
			status->rhw.atk2 = status_calc_watk(bl, sc, b_status->rhw.atk2);

		if(b_status->lhw.atk) {
			if (sd) {
				sd->state.lr_flag = 1;
				status->lhw.atk = status_calc_watk(bl, sc, b_status->lhw.atk);
				sd->state.lr_flag = 0;
			} else {
				status->lhw.atk = status_calc_watk(bl, sc, b_status->lhw.atk);
				status->lhw.atk2= status_calc_watk(bl, sc, b_status->lhw.atk2);
			}
		}
	}

	if(flag&SCB_HIT) {
		if (status->dex == b_status->dex)
			status->hit = status_calc_hit(bl, sc, b_status->hit);
		else
			status->hit = status_calc_hit(bl, sc, b_status->hit +(status->dex - b_status->dex));
	}

	if(flag&SCB_FLEE) {
		if (status->agi == b_status->agi)
			status->flee = status_calc_flee(bl, sc, b_status->flee);
		else
			status->flee = status_calc_flee(bl, sc, b_status->flee +(status->agi - b_status->agi));
	}

	if(flag&SCB_DEF)
	{
		status->def = status_calc_def(bl, sc, b_status->def);

		if( bl->type&BL_HOM )
			status->def += (status->vit/5 - b_status->vit/5);
	}

	if(flag&SCB_DEF2) {
		if (status->vit == b_status->vit)
			status->def2 = status_calc_def2(bl, sc, b_status->def2);
		else
			status->def2 = status_calc_def2(bl, sc, b_status->def2 + (status->vit - b_status->vit));
	}

	if(flag&SCB_MDEF)
	{
		status->mdef = status_calc_mdef(bl, sc, b_status->mdef);
	
		if( bl->type&BL_HOM )
			status->mdef += (status->int_/5 - b_status->int_/5);
	}
		
	if(flag&SCB_MDEF2) {
		if (status->int_ == b_status->int_ && status->vit == b_status->vit)
			status->mdef2 = status_calc_mdef2(bl, sc, b_status->mdef2);
		else
			status->mdef2 = status_calc_mdef2(bl, sc, b_status->mdef2 +(status->int_ - b_status->int_) +((status->vit - b_status->vit)>>1));
	}

	if(flag&SCB_SPEED) {
		status->speed = status_calc_speed(bl, sc, b_status->speed);

		if( bl->type&BL_PC && status->speed < battle_config.max_walk_speed )
			status->speed = battle_config.max_walk_speed;

		if( bl->type&BL_HOM && battle_config.hom_setting&0x8 && ((TBL_HOM*)bl)->master)
			status->speed = status_get_speed(&((TBL_HOM*)bl)->master->bl);

		if (bl->type&BL_MER && ((TBL_MER*)bl)->master)
			status->speed = status_get_speed(&((TBL_MER*)bl)->master->bl);

		if (bl->type&BL_ELEM && ((TBL_ELEM*)bl)->master)
			status->speed = status_get_speed(&((TBL_ELEM*)bl)->master->bl);
	}

	if(flag&SCB_CRI && b_status->cri) {
		if (status->luk == b_status->luk)
			status->cri = status_calc_critical(bl, sc, b_status->cri);
		else
			status->cri = status_calc_critical(bl, sc, b_status->cri + 3*(status->luk - b_status->luk));
		if (bl->type == BL_PC && ((TBL_PC*)bl)->status.weapon == W_KATAR)
			status->cri <<= 1;
	}

	if(flag&SCB_FLEE2 && b_status->flee2) {
		if (status->luk == b_status->luk)
			status->flee2 = status_calc_flee2(bl, sc, b_status->flee2);
		else
			status->flee2 = status_calc_flee2(bl, sc, b_status->flee2 +(status->luk - b_status->luk));
	}

	if(flag&SCB_ATK_ELE) {
		status->rhw.ele = status_calc_attack_element(bl, sc, b_status->rhw.ele);
		if (sd)
			sd->state.lr_flag = 1;
		status->lhw.ele = status_calc_attack_element(bl, sc, b_status->lhw.ele);
		if (sd) {
			sd->state.lr_flag = 0;
			status_calc_atk_ele_pc(sd, sc);
		}
	}

	if(flag&SCB_DEF_ELE) {
		status->def_ele = status_calc_element(bl, sc, b_status->def_ele);
		status->ele_lv = status_calc_element_lv(bl, sc, b_status->ele_lv);
		if (sd)
			status_calc_def_ele_pc(sd, sc);
	}

	if(flag&SCB_MODE)
	{
		status->mode = status_calc_mode(bl, sc, b_status->mode);
		//Since mode changed, reset their state.
		if (!(status->mode&MD_CANATTACK))
			unit_stop_attack(bl);
		if (!(status->mode&MD_CANMOVE))
			unit_stop_walking(bl,1);
	}

// No status changes alter these yet.
//	if(flag&SCB_SIZE)
// if(flag&SCB_RACE)
// if(flag&SCB_RANGE)

	if(flag&SCB_MAXHP) {
		if( bl->type&BL_PC )
		{
			status->max_hp = status_calc_maxhpsp_pc(sd, sd->battle_status.vit, true);

			if (battle_config.hp_rate != 100)
				status->max_hp = (unsigned int)(battle_config.hp_rate * (status->max_hp / 100.));

			if (sd->status.base_level < 100)
				status->max_hp = umin(status->max_hp, (unsigned int)battle_config.max_hp_lv99);
			else if (sd->status.base_level < 151)
				status->max_hp = umin(status->max_hp, (unsigned int)battle_config.max_hp_lv150);
			else
				status->max_hp = umin(status->max_hp, (unsigned int)battle_config.max_hp);
		}
		else
		{
			status->max_hp = status_calc_maxhp(bl, b_status->max_hp);
		}

		if( status->hp > status->max_hp ) //FIXME: Should perhaps a status_zap should be issued?
		{
			status->hp = status->max_hp;
			if( sd ) clif_updatestatus(sd,SP_HP);
		}
	}

	if(flag&SCB_MAXSP) {
		if( bl->type&BL_PC )
		{
			status->max_sp = status_calc_maxhpsp_pc(sd, sd->battle_status.int_, false);

			if (battle_config.sp_rate != 100)
				status->max_sp = (unsigned int)(battle_config.sp_rate * (status->max_sp / 100.));

			status->max_sp = min(status->max_sp, (unsigned int)battle_config.max_sp);
		}
		else
		{
			status->max_sp = status_calc_maxsp(bl, b_status->max_sp);
		}

		if( status->sp > status->max_sp )
		{
			status->sp = status->max_sp;
			if( sd ) clif_updatestatus(sd,SP_SP);
		}
	}

	if(flag&SCB_MATK) {
		//New matk
		status->matk_min = status_base_matk_min(status);
		status->matk_max = status_base_matk_max(status);

		if( bl->type&BL_PC )
		{
			short skill = 0;
			short passive_add_matk = 0;
			short passive_matk_rate = 0;

			// Passive skills that increase matk.
			if( (skill = pc_checkskill(sd,NV_TRANSCENDENCE)) > 0 )
			{
				passive_add_matk += 15 * skill;

				if ( skill >= 5 )
					passive_add_matk += 25;
			}

			if ( sd->bonus.matk+passive_add_matk > 0 )
			{	//Increases MATK by a fixed amount. [15peaces]
				status->matk_min += sd->bonus.matk + passive_add_matk;
				status->matk_max += sd->bonus.matk + passive_add_matk;
			}

			if ( skill_summoner_power(sd, POWER_OF_LAND) == 1 )
				passive_matk_rate += 20;

			if ( (sd->matk_rate+passive_matk_rate) != 100 )
			{
				//Bonuses from previous matk
				status->matk_max = status->matk_max * (sd->matk_rate + passive_matk_rate) / 100;
				status->matk_min = status->matk_min * (sd->matk_rate + passive_matk_rate) / 100;
			}
		}
			
		status->matk_min = status_calc_matk(bl, sc, status->matk_min);
		status->matk_max = status_calc_matk(bl, sc, status->matk_max);

		if( bl->type&BL_HOM && battle_config.hom_setting&0x20 ) //Hom Min Matk is always the same as Max Matk
			status->matk_min = status->matk_max;

	}

	if(flag&SCB_ASPD) {
		int amotion;
		if( bl->type&BL_PC ) {
			amotion = status_base_amotion_pc(sd,status);
			status->aspd_amount = status_calc_aspd_amount(bl, sc, b_status->aspd_amount);
			status->aspd_rate = status_calc_aspd_rate(bl, sc, b_status->aspd_rate);

			if(status->aspd_amount != 0)
				amotion -= status->aspd_amount;
			
			if(status->aspd_rate != 1000)
				amotion = amotion*status->aspd_rate/1000;
			
			// Config for setting seprate ASPD cap for 3rd jobs and other jobs released in renewal.
			if ( sd && ((sd->class_&MAPID_THIRDMASK) >= MAPID_SUPER_NOVICE_E && (sd->class_&MAPID_THIRDMASK) <= MAPID_SOUL_REAPER || (sd->class_&MAPID_UPPERMASK) == MAPID_KAGEROUOBORO || (sd->class_&MAPID_UPPERMASK) == MAPID_REBELLION || (sd->class_&MAPID_BASEMASK) == MAPID_SUMMONER))
				status->amotion = cap_value(amotion,battle_config.max_aspd_renewal_jobs,2000);
			else
				status->amotion = cap_value(amotion,battle_config.max_aspd,2000);

			if (sc->data[SC_OVERED_BOOST])
				status->amotion = sc->data[SC_OVERED_BOOST]->val3;
			
			status->adelay = 2*status->amotion;
		}
		else
		if( bl->type&BL_HOM )
		{
			amotion = ((TBL_HOM*)bl)->homunculusDB->baseASPD;
			amotion -= amotion * (4 * status->agi + status->dex) / 1000;
			
			status->aspd_amount = status_calc_aspd_amount(bl, sc, b_status->aspd_amount);
			status->aspd_rate = status_calc_aspd_rate(bl, sc, b_status->aspd_rate);

			if(status->aspd_amount != 0)
				amotion -= status->aspd_amount;
			
			if(status->aspd_rate != 1000)
				amotion = amotion*status->aspd_rate/1000;
			
			status->amotion = cap_value(amotion,battle_config.max_aspd,2000);
			
			if (sc->data[SC_OVERED_BOOST])
				status->amotion = sc->data[SC_OVERED_BOOST]->val3;

			status->adelay = 2 * status->amotion;
		}
		else // mercenary, elemental and mobs
		{
			amotion = b_status->amotion;
			status->aspd_amount = status_calc_aspd_amount(bl, sc, b_status->aspd_amount);
			status->aspd_rate = status_calc_aspd_rate(bl, sc, b_status->aspd_rate);

			if(status->aspd_amount != 0)
				amotion -= status->aspd_amount;
			
			if(status->aspd_rate != 1000)
				amotion = amotion*status->aspd_rate/1000;
			
			status->amotion = cap_value(amotion, battle_config.monster_max_aspd, 2000);	
			
			temp = b_status->adelay*status->aspd_rate/1000;
			status->adelay = cap_value(temp, battle_config.monster_max_aspd*2, 4000);
		}
	}

	if(flag&SCB_DSPD) {
		int dmotion;
		if( bl->type&BL_PC )
		{
			if (b_status->agi == status->agi)
				status->dmotion = status_calc_dmotion(bl, sc, b_status->dmotion);
			else {
				dmotion = 800-status->agi*4;
				status->dmotion = cap_value(dmotion, 400, 800);
				if(battle_config.pc_damage_delay_rate != 100)
					status->dmotion = status->dmotion*battle_config.pc_damage_delay_rate/100;
				//It's safe to ignore b_status->dmotion since no bonus affects it.
				status->dmotion = status_calc_dmotion(bl, sc, status->dmotion);
			}
		}
		else
		if( bl->type&BL_HOM )
		{
			dmotion = 800-status->agi*4;
			status->dmotion = cap_value(dmotion, 400, 800);
			status->dmotion = status_calc_dmotion(bl, sc, b_status->dmotion);
		}
		else // mercenary and mobs
		{
			status->dmotion = status_calc_dmotion(bl, sc, b_status->dmotion);
		}
	}

	if(flag&(SCB_VIT|SCB_MAXHP|SCB_INT|SCB_MAXSP) && bl->type&BL_REGEN)
		status_calc_regen(bl, status, status_get_regen_data(bl));

	if(flag&SCB_REGEN && bl->type&BL_REGEN)
		status_calc_regen_rate(bl, status_get_regen_data(bl), sc);
}

/// Recalculates parts of an elementals battle status according to the specified flags.
/// @param flag bitfield of values from enum scb_flag
void status_calc_bl_elem(struct block_list *bl, enum scb_flag flag)
{
	const struct status_data *b_status = status_get_base_status(bl);
	struct status_data *status = status_get_status_data(bl);
	struct status_change *sc = status_get_sc(bl);

	if (!b_status || !status)
		return;

	if((!(bl->type&BL_REGEN)) && (!sc || !sc->count)) {
		status_cpy(status, b_status);
		return;
	}

	if(flag&SCB_MAXHP)
	{
		status->max_hp = status_calc_maxhp(bl, b_status->max_hp);

		if( status->max_hp > battle_config.max_elemental_hp )
			status->max_hp = battle_config.max_elemental_hp;

		if( status->hp > status->max_hp )
			status->hp = status->max_hp;
	}

	if(flag&SCB_MAXSP)
	{
		status->max_sp = status_calc_maxsp(bl, b_status->max_sp);

		if( status->max_sp > battle_config.max_elemental_sp )
			status->max_sp = battle_config.max_elemental_sp;

		if( status->sp > status->max_sp )
			status->sp = status->max_sp;
	}

	if(flag&SCB_WATK)
	{
		status->rhw.atk = status_calc_watk(bl, sc, b_status->rhw.atk);
		status->rhw.atk2 = status_calc_watk(bl, sc, b_status->rhw.atk2);
	}

	if(flag&SCB_MATK)
	{
		status->matk_min = status_calc_matk(bl, sc, status->matk_min);
		status->matk_max = status_calc_matk(bl, sc, status->matk_max);
	}

	if(flag&SCB_HIT)
		status->hit = status_calc_hit(bl, sc, b_status->hit);

	if(flag&SCB_FLEE)
		status->flee = status_calc_flee(bl, sc, b_status->flee);

	if(flag&SCB_DEF)
		status->def = status_calc_def(bl, sc, b_status->def);

	if(flag&SCB_MDEF)
		status->mdef = status_calc_mdef(bl, sc, b_status->mdef);

	if(flag&SCB_SPEED)
	{
		struct unit_data *ud = unit_bl2ud(bl);
		status->speed = status_calc_speed(bl, sc, b_status->speed);

	  	if (ud)
			ud->state.change_walk_target = ud->state.speed_changed = 1;

		if( bl->type&BL_ELEM && battle_config.elemental_masters_walk_speed && ((TBL_ELEM*)bl)->master)
			status->speed = status_get_speed(&((TBL_ELEM*)bl)->master->bl);
	}

	if(flag&SCB_ATK_ELE)
		status->rhw.ele = status_calc_attack_element(bl, sc, b_status->rhw.ele);

	if(flag&SCB_DEF_ELE)
	{
		status->def_ele = status_calc_element(bl, sc, b_status->def_ele);
		status->ele_lv = status_calc_element_lv(bl, sc, b_status->ele_lv);
	}

	if(flag&SCB_MODE)
	{
		status->mode = status_calc_mode(bl, sc, b_status->mode);
		//Since mode changed, reset their state.
		if (!(status->mode&MD_CANATTACK))
			unit_stop_attack(bl);
		if (!(status->mode&MD_CANMOVE))
			unit_stop_walking(bl,1);
	}

// No status changes alter these yet.
//	if(flag&SCB_SIZE)
//	if(flag&SCB_RACE)
//	if(flag&SCB_RANGE)

	if(flag&SCB_ASPD)
	{
		short amotion;

		amotion = b_status->amotion;

		status->aspd_rate = status_calc_aspd_rate(bl, sc, b_status->aspd_rate);

		if(status->aspd_rate != 1000)
			amotion = amotion*status->aspd_rate/1000;

		status->amotion = cap_value(amotion,battle_config.max_aspd,2000);
		status->adelay = 2 * status->amotion;
	}

	// Elementals have a dMotion of 0 in aegis database but I don't see any formula for this in the code.
	// And with no stats I have no idea what defines it. For now its set to 300 in our database.
	if(flag&SCB_DSPD)
		status->dmotion = status_calc_dmotion(bl, sc, b_status->dmotion);
}

/// Recalculates parts of an object's base status and battle status according to the specified flags.
/// Also sends updates to the client wherever applicable.
/// @param flag bitfield of values from enum scb_flag
/// @param first if true, will cause status_calc_* functions to run their base status initialization code
void status_calc_bl_(struct block_list* bl, enum scb_flag flag, enum e_status_calc_opt opt)
{
	struct status_data b_status; // previous battle status
	struct status_data* status; // pointer to current battle status

	if (bl->type == BL_PC && ((TBL_PC*)bl)->delayed_damage != 0) {
		if (opt&SCO_FORCE)
			((TBL_PC*)bl)->state.hold_recalc = 0; /* Clear and move on */
		else {
			((TBL_PC*)bl)->state.hold_recalc = 1; /* Flag and stop */
			return;
		}
	}

	// remember previous values
	status = status_get_status_data(bl);
	memcpy(&b_status, status, sizeof(struct status_data));

	if( flag&SCB_BASE )
	{// calculate the object's base status too
		switch( bl->type )
		{
			case BL_PC:		status_calc_pc_(BL_CAST(BL_PC,bl), opt);			break;
			case BL_MOB:	status_calc_mob_(BL_CAST(BL_MOB,bl), opt);			break;
			case BL_PET:	status_calc_pet_(BL_CAST(BL_PET,bl), opt);			break;
			case BL_HOM:	status_calc_homunculus_(BL_CAST(BL_HOM,bl), opt);	break;
			case BL_MER:	status_calc_mercenary_(BL_CAST(BL_MER,bl), opt);	break;
			case BL_ELEM:	status_calc_elemental_(BL_CAST(BL_ELEM,bl), opt);	break;
			case BL_NPC:	status_calc_npc_(BL_CAST(BL_NPC, bl), opt);			break;
		}
	}

	if( bl->type == BL_PET )
		return; // pets are not affected by statuses

	if(opt&SCO_FIRST && bl->type == BL_MOB )
		return; // assume there will be no statuses active

	if ( bl->type == BL_ELEM )// Elemental calculations are based on the summoner and not raw stats.
		status_calc_bl_elem(bl, flag);
	else
		status_calc_bl_main(bl, flag);

	if(opt&SCO_FIRST && bl->type == BL_HOM )
		return; // client update handled by caller

	// compare against new values and send client updates
	if( bl->type == BL_PC )
	{
		TBL_PC* sd = BL_CAST(BL_PC, bl);
		if(b_status.str != status->str)
			clif_updatestatus(sd,SP_STR);
		if(b_status.agi != status->agi)
			clif_updatestatus(sd,SP_AGI);
		if(b_status.vit != status->vit)
			clif_updatestatus(sd,SP_VIT);
		if(b_status.int_ != status->int_)
			clif_updatestatus(sd,SP_INT);
		if(b_status.dex != status->dex)
			clif_updatestatus(sd,SP_DEX);
		if(b_status.luk != status->luk)
			clif_updatestatus(sd,SP_LUK);
		if(b_status.hit != status->hit)
			clif_updatestatus(sd,SP_HIT);
		if(b_status.flee != status->flee)
			clif_updatestatus(sd,SP_FLEE1);
		if(b_status.amotion != status->amotion)
			clif_updatestatus(sd,SP_ASPD);
		if(b_status.speed != status->speed)
			clif_updatestatus(sd,SP_SPEED);
		if(b_status.rhw.atk != status->rhw.atk || b_status.lhw.atk != status->lhw.atk || b_status.batk != status->batk)
			clif_updatestatus(sd,SP_ATK1);
		if(b_status.def != status->def)
			clif_updatestatus(sd,SP_DEF1);
		if(b_status.rhw.atk2 != status->rhw.atk2 || b_status.lhw.atk2 != status->lhw.atk2)
			clif_updatestatus(sd,SP_ATK2);
		if(b_status.def2 != status->def2)
			clif_updatestatus(sd,SP_DEF2);
		if(b_status.flee2 != status->flee2)
			clif_updatestatus(sd,SP_FLEE2);
		if(b_status.cri != status->cri)
			clif_updatestatus(sd,SP_CRITICAL);
		if(b_status.matk_max != status->matk_max)
			clif_updatestatus(sd,SP_MATK1);
		if(b_status.matk_min != status->matk_min)
			clif_updatestatus(sd,SP_MATK2);
		if(b_status.mdef != status->mdef)
			clif_updatestatus(sd,SP_MDEF1);
		if(b_status.mdef2 != status->mdef2)
			clif_updatestatus(sd,SP_MDEF2);
		if(b_status.rhw.range != status->rhw.range)
			clif_updatestatus(sd,SP_ATTACKRANGE);
		if(b_status.max_hp != status->max_hp)
			clif_updatestatus(sd,SP_MAXHP);
		if(b_status.max_sp != status->max_sp)
			clif_updatestatus(sd,SP_MAXSP);
		if(b_status.hp != status->hp)
			clif_updatestatus(sd,SP_HP);
		if(b_status.sp != status->sp)
			clif_updatestatus(sd,SP_SP);
	}
	else
	if( bl->type == BL_HOM )
	{
		TBL_HOM* hd = BL_CAST(BL_HOM, bl);
		if( hd->master && memcmp(&b_status, status, sizeof(struct status_data)) != 0 )
			clif_hominfo(hd->master,hd,0);
	}
	else
	if( bl->type == BL_MER ) {
		TBL_MER* md = BL_CAST(BL_MER, bl);

		if (!md->master)
			return;

		if( b_status.rhw.atk != status->rhw.atk || b_status.rhw.atk2 != status->rhw.atk2 )
			clif_mercenary_updatestatus(md->master, SP_ATK1);
		if( b_status.matk_max != status->matk_max )
			clif_mercenary_updatestatus(md->master, SP_MATK1);
		if( b_status.hit != status->hit )
			clif_mercenary_updatestatus(md->master, SP_HIT);
		if( b_status.cri != status->cri )
			clif_mercenary_updatestatus(md->master, SP_CRITICAL);
		if( b_status.def != status->def )
			clif_mercenary_updatestatus(md->master, SP_DEF1);
		if( b_status.mdef != status->mdef )
			clif_mercenary_updatestatus(md->master, SP_MDEF1);
		if( b_status.flee != status->flee )
			clif_mercenary_updatestatus(md->master, SP_MERCFLEE);
		if( b_status.amotion != status->amotion )
			clif_mercenary_updatestatus(md->master, SP_ASPD);
		if( b_status.max_hp != status->max_hp )
			clif_mercenary_updatestatus(md->master, SP_MAXHP);
		if( b_status.max_sp != status->max_sp )
			clif_mercenary_updatestatus(md->master, SP_MAXSP);
		if( b_status.hp != status->hp )
			clif_mercenary_updatestatus(md->master, SP_HP);
		if( b_status.sp != status->sp )
			clif_mercenary_updatestatus(md->master, SP_SP);
	}
	else
	if( bl->type == BL_ELEM ) {
		TBL_ELEM* ed = BL_CAST(BL_ELEM, bl);

		if (!ed->master)
			return;

		if( b_status.max_hp != status->max_hp )
			clif_elemental_updatestatus(ed->master, SP_MAXHP);
		if( b_status.max_sp != status->max_sp )
			clif_elemental_updatestatus(ed->master, SP_MAXSP);
		if( b_status.hp != status->hp )
			clif_elemental_updatestatus(ed->master, SP_HP);
		if( b_status.sp != status->sp )
			clif_mercenary_updatestatus(ed->master, SP_SP);
	}
}

/*==========================================
 * Apply shared stat mods from status changes [DracoRPG]
 *------------------------------------------*/
static unsigned short status_calc_str(struct block_list *bl, struct status_change *sc, int str)
{
	if(!sc || !sc->count)
		return cap_value(str,0,USHRT_MAX);

	if (sc->data[SC_FULL_THROTTLE])
		str += str * 20 / 100;
	if(sc->data[SC_INCALLSTATUS])
		str += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCSTR])
		str += sc->data[SC_INCSTR]->val1;
	if(sc->data[SC_STRFOOD])
		str += sc->data[SC_STRFOOD]->val1;
	if(sc->data[SC_FOOD_STR_CASH])
		str += sc->data[SC_FOOD_STR_CASH]->val1;
	if(sc->data[SC_BATTLEORDERS])
		str += 5;
	if (sc->data[SC_LEADERSHIP])
		str += sc->data[SC_LEADERSHIP]->val1;
	if(sc->data[SC_LOUD])
		str += 4;
	if(sc->data[SC_TRUESIGHT])
		str += 5;
	if(sc->data[SC_SPURT])
		str += 10;
	if(sc->data[SC_NEN])
		str += sc->data[SC_NEN]->val1;
	if(sc->data[SC_BLESSING]){
		if(sc->data[SC_BLESSING]->val2)
			str += sc->data[SC_BLESSING]->val2;
		else
			str >>= 1;
	}
	if(sc->data[SC_GIANTGROWTH])
		str += 30;
	if(sc->data[SC_HARMONIZE])
		str += sc->data[SC_HARMONIZE]->val3;
	if (sc->data[SC_BEYOND_OF_WARCRY])
		str += sc->data[SC_BEYOND_OF_WARCRY]->val3;
	if (sc->data[SC_SAVAGE_STEAK])
		str += sc->data[SC_SAVAGE_STEAK]->val1;
	if (sc->data[SC_INSPIRATION])
		str += sc->data[SC_INSPIRATION]->val3;
	if (sc->data[SC_2011RWC_SCROLL])
		str += sc->data[SC_2011RWC_SCROLL]->val1;
	if (sc->data[SC_UNIVERSESTANCE])
		str += sc->data[SC_UNIVERSESTANCE]->val2;
	if (sc->data[SC_CHEERUP])
		str += 3;
	if (sc->data[SC_STOMACHACHE])
		str -= sc->data[SC_STOMACHACHE]->val1;
	if (sc->data[SC_KYOUGAKU])
		str -= sc->data[SC_KYOUGAKU]->val2;
	if (sc->data[SC_MARIONETTE])
		str -= ((sc->data[SC_MARIONETTE]->val3) >> 16) & 0xFF;
	if (sc->data[SC_MARIONETTE2])
		str += ((sc->data[SC_MARIONETTE2]->val3) >> 16) & 0xFF;
	if (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		str += ((sc->data[SC_SPIRIT]->val3) >> 16) & 0xFF;
	if (sc->data[SC_SWORDCLAN])
		str += 1;
	if (sc->data[SC_JUMPINGCLAN])
		str += 1;


	return (unsigned short)cap_value(str,0,USHRT_MAX);
}

static unsigned short status_calc_agi(struct block_list *bl, struct status_change *sc, int agi)
{
	if(!sc || !sc->count)
		return cap_value(agi,0,USHRT_MAX);

	if (sc->data[SC_FULL_THROTTLE])
		agi += agi * 20 / 100;
	if(sc->data[SC_CONCENTRATE] && !sc->data[SC_QUAGMIRE])
		agi += (agi-sc->data[SC_CONCENTRATE]->val3)*sc->data[SC_CONCENTRATE]->val2/100;
	if(sc->data[SC_INCALLSTATUS])
		agi += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCAGI])
		agi += sc->data[SC_INCAGI]->val1;
	if(sc->data[SC_AGIFOOD])
		agi += sc->data[SC_AGIFOOD]->val1;
	if(sc->data[SC_FOOD_AGI_CASH])
		agi += sc->data[SC_FOOD_AGI_CASH]->val1;
	if (sc->data[SC_SOULCOLD])
		agi += sc->data[SC_SOULCOLD]->val1;
	if(sc->data[SC_TRUESIGHT])
		agi += 5;
	if(sc->data[SC_INCREASEAGI])
		agi += sc->data[SC_INCREASEAGI]->val2;
	if(sc->data[SC_INCREASING])
		agi += 4;	// added based on skill updates [Reddozen]
	if (sc->data[SC_2011RWC_SCROLL])
		agi += sc->data[SC_2011RWC_SCROLL]->val1;
	if(sc->data[SC_HARMONIZE])
		agi += sc->data[SC_HARMONIZE]->val3;
	if(sc->data[SC_DROCERA_HERB_STEAMED])
		agi += sc->data[SC_DROCERA_HERB_STEAMED]->val1;
	if(sc->data[SC_INSPIRATION])
		agi += sc->data[SC_INSPIRATION]->val3;
	if(sc->data[SC_UNIVERSESTANCE])
		agi += sc->data[SC_UNIVERSESTANCE]->val2;
	if(sc->data[SC_ARCLOUSEDASH])
		agi += sc->data[SC_ARCLOUSEDASH]->val2;
	if(sc->data[SC_CHEERUP])
		agi += 3;
	if(sc->data[SC_MARSHOFABYSS])// Must confirm if stat reductions are done by percentage first. [Rytech]
		agi -= agi * sc->data[SC_MARSHOFABYSS]->val2 / 100;
	if(sc->data[SC_DECREASEAGI])
		agi -= sc->data[SC_DECREASEAGI]->val2;
	if(sc->data[SC_QUAGMIRE])
		agi -= sc->data[SC_QUAGMIRE]->val2;
	if(sc->data[SC_SUITON] && sc->data[SC_SUITON]->val3)
		agi -= sc->data[SC_SUITON]->val2;
	if(sc->data[SC_ADORAMUS])
		agi -= sc->data[SC_ADORAMUS]->val2;
	if(sc->data[SC_STOMACHACHE])
		agi -= sc->data[SC_STOMACHACHE]->val1;
	if(sc->data[SC_KYOUGAKU])
		agi -= sc->data[SC_KYOUGAKU]->val2;
	if(sc->data[SC_MARIONETTE])
		agi -= ((sc->data[SC_MARIONETTE]->val3)>>8)&0xFF;
	if(sc->data[SC_MARIONETTE2])
		agi += ((sc->data[SC_MARIONETTE2]->val3)>>8)&0xFF;
	if (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		agi += ((sc->data[SC_SPIRIT]->val3) >> 8) & 0xFF;
	if(sc->data[SC_CROSSBOWCLAN])
		agi += 1;
	if(sc->data[SC_JUMPINGCLAN])
		agi += 1;

	return (unsigned short)cap_value(agi,0,USHRT_MAX);
}

static unsigned short status_calc_vit(struct block_list *bl, struct status_change *sc, int vit)
{
	if(!sc || !sc->count)
		return cap_value(vit,0,USHRT_MAX);
	
	if(sc->data[SC_FULL_THROTTLE])
		vit += vit * 20 / 100;
	if(sc->data[SC_INCALLSTATUS])
		vit += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCVIT])
		vit += sc->data[SC_INCVIT]->val1;
	if(sc->data[SC_VITFOOD])
		vit += sc->data[SC_VITFOOD]->val1;
	if(sc->data[SC_FOOD_VIT_CASH])
		vit += sc->data[SC_FOOD_VIT_CASH]->val1;
	if(sc->data[SC_CHANGE])
		vit += sc->data[SC_CHANGE]->val2;
	if (sc->data[SC_GLORYWOUNDS])
		vit += sc->data[SC_GLORYWOUNDS]->val1;
	if(sc->data[SC_TRUESIGHT])
		vit += 5;
	if(sc->data[SC_HARMONIZE])
		vit += sc->data[SC_HARMONIZE]->val3;
	if(sc->data[SC_MINOR_BBQ])
		vit += sc->data[SC_MINOR_BBQ]->val1;
	if(sc->data[SC_INSPIRATION])
		vit += sc->data[SC_INSPIRATION]->val3;
	if (sc->data[SC_2011RWC_SCROLL])
		vit += sc->data[SC_2011RWC_SCROLL]->val1;
	if(sc->data[SC_UNIVERSESTANCE])
		vit += sc->data[SC_UNIVERSESTANCE]->val2;
	if(sc->data[SC_CHEERUP])
		vit += 3;
	if(sc->data[SC_STRIPARMOR] && bl->type != BL_PC)
		vit -= vit * sc->data[SC_STRIPARMOR]->val2 / 100;
	if(sc->data[SC_STOMACHACHE])
		vit -= sc->data[SC_STOMACHACHE]->val1;
	if(sc->data[SC_KYOUGAKU])
		vit -= sc->data[SC_KYOUGAKU]->val2;
	if(sc->data[SC_MARIONETTE])
		vit -= sc->data[SC_MARIONETTE]->val3&0xFF;
	if(sc->data[SC_MARIONETTE2])
		vit += sc->data[SC_MARIONETTE2]->val3&0xFF;
	if (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		vit += sc->data[SC_SPIRIT]->val3 & 0xFF;
	if(sc->data[SC_SWORDCLAN])
		vit += 1;
	if(sc->data[SC_JUMPINGCLAN])
		vit += 1;

	return (unsigned short)cap_value(vit,0,USHRT_MAX);
}

static unsigned short status_calc_int(struct block_list *bl, struct status_change *sc, int int_)
{
	if(!sc || !sc->count)
		return cap_value(int_,0,USHRT_MAX);

	if (sc->data[SC_FULL_THROTTLE])
		int_ += int_ * 20 / 100;
	if(sc->data[SC_INCALLSTATUS])
		int_ += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCINT])
		int_ += sc->data[SC_INCINT]->val1;
	if(sc->data[SC_INTFOOD])
		int_ += sc->data[SC_INTFOOD]->val1;
	if(sc->data[SC_FOOD_INT_CASH])
		int_ += sc->data[SC_FOOD_INT_CASH]->val1;
	if(sc->data[SC_CHANGE])
		int_ += sc->data[SC_CHANGE]->val3;
	if(sc->data[SC_BATTLEORDERS])
		int_ += 5;
	if(sc->data[SC_TRUESIGHT])
		int_ += 5;
	if(sc->data[SC_BLESSING]){
		if (sc->data[SC_BLESSING]->val2)
			int_ += sc->data[SC_BLESSING]->val2;
		else
			int_ >>= 1;
	}
	if(sc->data[SC_NEN])
		int_ += sc->data[SC_NEN]->val1;
	if(sc->data[SC_INSPIRATION])
		int_ += sc->data[SC_INSPIRATION]->val3;
	if(sc->data[SC_UNIVERSESTANCE])
		int_ += sc->data[SC_UNIVERSESTANCE]->val2;
	if(sc->data[SC_CHEERUP])
		int_ += 3;
	if(sc->data[SC_HARMONIZE])
		int_ += sc->data[SC_HARMONIZE]->val3;
	if(sc->data[SC_COCKTAIL_WARG_BLOOD])
		int_ += sc->data[SC_COCKTAIL_WARG_BLOOD]->val1;
	if (bl->type != BL_PC) {
		if (sc->data[SC_STRIPHELM])
			int_ -= int_ * sc->data[SC_STRIPHELM]->val2 / 100;
		if (sc->data[SC__STRIPACCESSARY])// What happens when 2 different reductions by percentages exists? [Rytech]
			int_ -= int_ * sc->data[SC__STRIPACCESSARY]->val2 / 100;
	}
	if(sc->data[SC_MELODYOFSINK])
		int_ -= sc->data[SC_MELODYOFSINK]->val3;
	if(sc->data[SC_MANDRAGORA])
		int_ -= 4 * sc->data[SC_MANDRAGORA]->val1;
	if(sc->data[SC_STOMACHACHE])
		int_ -= sc->data[SC_STOMACHACHE]->val1;
	if(sc->data[SC_KYOUGAKU])
		int_ -= sc->data[SC_KYOUGAKU]->val2;
	if(sc->data[SC_MARIONETTE])
		int_ -= ((sc->data[SC_MARIONETTE]->val4)>>16)&0xFF;
	if (sc->data[SC_2011RWC_SCROLL])
		int_ += sc->data[SC_2011RWC_SCROLL]->val1;
	if(sc->data[SC_MARIONETTE2])
		int_ += ((sc->data[SC_MARIONETTE2]->val4)>>16)&0xFF;
	if (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		int_ += ((sc->data[SC_SPIRIT]->val4) >> 16) & 0xFF;
	if(sc->data[SC_ARCWANDCLAN])
		int_ += 1;
	if(sc->data[SC_GOLDENMACECLAN])
		int_ += 1;
	if(sc->data[SC_JUMPINGCLAN])
		int_ += 1;

	return (unsigned short)cap_value(int_,0,USHRT_MAX);
}

static unsigned short status_calc_dex(struct block_list *bl, struct status_change *sc, int dex)
{
	if(!sc || !sc->count)
		return cap_value(dex,0,USHRT_MAX);

	if (sc->data[SC_FULL_THROTTLE])
		dex += dex * 20 / 100;
	if(sc->data[SC_CONCENTRATE] && !sc->data[SC_QUAGMIRE])
		dex += (dex-sc->data[SC_CONCENTRATE]->val4)*sc->data[SC_CONCENTRATE]->val2/100;
	if(sc->data[SC_INCALLSTATUS])
		dex += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCDEX])
		dex += sc->data[SC_INCDEX]->val1;
	if(sc->data[SC_DEXFOOD])
		dex += sc->data[SC_DEXFOOD]->val1;
	if(sc->data[SC_FOOD_DEX_CASH])
		dex += sc->data[SC_FOOD_DEX_CASH]->val1;
	if(sc->data[SC_BATTLEORDERS])
		dex += 5;
	if (sc->data[SC_HAWKEYES])
		dex += sc->data[SC_HAWKEYES]->val1;
	if(sc->data[SC_TRUESIGHT])
		dex += 5;
	if(sc->data[SC_BLESSING]){
		if (sc->data[SC_BLESSING]->val2)
			dex += sc->data[SC_BLESSING]->val2;
		else
			dex >>= 1;
	}
	if(sc->data[SC_INCREASING])
		dex += 4;	// added based on skill updates [Reddozen]
	if(sc->data[SC_HARMONIZE])
		dex += sc->data[SC_HARMONIZE]->val3;
	if(sc->data[SC_SIROMA_ICE_TEA])
		dex += sc->data[SC_SIROMA_ICE_TEA]->val1;
	if(sc->data[SC_INSPIRATION])
		dex += sc->data[SC_INSPIRATION]->val3;
	if(sc->data[SC_UNIVERSESTANCE])
		dex += sc->data[SC_UNIVERSESTANCE]->val2;
	if(sc->data[SC_CHEERUP])
		dex += 3;
	if(sc->data[SC_MARSHOFABYSS])
		dex -= dex * sc->data[SC_MARSHOFABYSS]->val2 / 100;
	if(sc->data[SC__STRIPACCESSARY] && bl->type != BL_PC)
		dex -= dex * sc->data[SC__STRIPACCESSARY]->val2 / 100;
	if(sc->data[SC_QUAGMIRE])
		dex -= sc->data[SC_QUAGMIRE]->val2;
	if(sc->data[SC_STOMACHACHE])
		dex -= sc->data[SC_STOMACHACHE]->val1;
	if(sc->data[SC_KYOUGAKU])
		dex -= sc->data[SC_KYOUGAKU]->val2;
	if(sc->data[SC_MARIONETTE])
		dex -= ((sc->data[SC_MARIONETTE]->val4)>>8)&0xFF;
	if (sc->data[SC_2011RWC_SCROLL])
		dex += sc->data[SC_2011RWC_SCROLL]->val1;
	if(sc->data[SC_MARIONETTE2])
		dex += ((sc->data[SC_MARIONETTE2]->val4)>>8)&0xFF;
	if (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		dex += ((sc->data[SC_SPIRIT]->val4) >> 8) & 0xFF;
	if(sc->data[SC_ARCWANDCLAN])
		dex += 1;
	if(sc->data[SC_CROSSBOWCLAN])
		dex += 1;
	if(sc->data[SC_JUMPINGCLAN])
		dex += 1;

	return (unsigned short)cap_value(dex,0,USHRT_MAX);
}

// Calculation order (for all except SPEED and ASPD)
// - 1. Calculate increases by a fixed amount.
// - 2. Calculate increases by a percentage.
// - 3. Calculate decreases by a fixed amount.
// - 4. Calculate decreases by a percentage.
static unsigned short status_calc_luk(struct block_list *bl, struct status_change *sc, int luk)
{
	if(!sc || !sc->count)
		return cap_value(luk,0,USHRT_MAX);

	if(sc->data[SC_CURSE])
		return 0;
	if (sc->data[SC_FULL_THROTTLE])
		luk += luk * 20 / 100;
	if(sc->data[SC_INCALLSTATUS])
		luk += sc->data[SC_INCALLSTATUS]->val1;
	if(sc->data[SC_INCLUK])
		luk += sc->data[SC_INCLUK]->val1;
	if(sc->data[SC_LUKFOOD])
		luk += sc->data[SC_LUKFOOD]->val1;
	if(sc->data[SC_FOOD_LUK_CASH])
		luk += sc->data[SC_FOOD_LUK_CASH]->val1;
	if(sc->data[SC_TRUESIGHT])
		luk += 5;
	if(sc->data[SC_GLORIA])
		luk += 30;
	if(sc->data[SC_HARMONIZE])
		luk += sc->data[SC_HARMONIZE]->val3;
	if(sc->data[SC_PUTTI_TAILS_NOODLES])
		luk += sc->data[SC_PUTTI_TAILS_NOODLES]->val1;
	if(sc->data[SC_INSPIRATION])
		luk += sc->data[SC_INSPIRATION]->val3;
	if(sc->data[SC_UNIVERSESTANCE])
		luk += sc->data[SC_UNIVERSESTANCE]->val2;
	if(sc->data[SC_CHEERUP])
		luk += 3;
	if(sc->data[SC_GOLDENMACECLAN])
		luk += 1;
	if(sc->data[SC_JUMPINGCLAN])
		luk += 1;
	if (sc->data[SC_2011RWC_SCROLL])
		luk += sc->data[SC_2011RWC_SCROLL]->val1;
	if(sc->data[SC__STRIPACCESSARY] && bl->type != BL_PC)
		luk -= luk * sc->data[SC__STRIPACCESSARY]->val2 / 100;
	if(sc->data[SC_BANANA_BOMB])
		luk -= 77;
	if(sc->data[SC_STOMACHACHE])
		luk -= sc->data[SC_STOMACHACHE]->val1;
	if(sc->data[SC_KYOUGAKU])
		luk -= sc->data[SC_KYOUGAKU]->val2;
	if(sc->data[SC_MARIONETTE])
		luk -= sc->data[SC_MARIONETTE]->val4&0xFF;
	if(sc->data[SC_MARIONETTE2])
		luk += sc->data[SC_MARIONETTE2]->val4&0xFF;
	if (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
		luk += sc->data[SC_SPIRIT]->val4 & 0xFF;

	return (unsigned short)cap_value(luk,0,USHRT_MAX);
}

// Calculation order (for all except SPEED and ASPD)
// - 1. Calculate increases by a fixed amount.
// - 2. Calculate increases by a percentage.
// - 3. Calculate decreases by a fixed amount.
// - 4. Calculate decreases by a percentage.
static unsigned short status_calc_batk(struct block_list *bl, struct status_change *sc, int batk)
{
	if(!sc || !sc->count)
		return cap_value(batk,0,USHRT_MAX);

	if(sc->data[SC_ATKPOTION])
		batk += sc->data[SC_ATKPOTION]->val1;
	if(sc->data[SC_BATKFOOD])
		batk += sc->data[SC_BATKFOOD]->val1;
	if(sc->data[SC_GATLINGFEVER])
		batk += sc->data[SC_GATLINGFEVER]->val3;
	if(sc->data[SC_MADNESSCANCEL])
		batk += 100;
	if (sc->data[SC_2011RWC_SCROLL])
		batk += 30;
	if(sc->data[SC_INCATKRATE])
		batk += batk * sc->data[SC_INCATKRATE]->val1/100;
	if(sc->data[SC_PROVOKE])
		batk += batk * sc->data[SC_PROVOKE]->val3/100;
	if(sc->data[SC_CONCENTRATION])
		batk += batk * sc->data[SC_CONCENTRATION]->val2/100;
	if(sc->data[SC_SKE])
		batk += batk * 3;
	if(sc->data[SC_BLOODLUST])
		batk += batk * sc->data[SC_BLOODLUST]->val2/100;
	if(sc->data[SC_FLEET])
		batk += batk * sc->data[SC_FLEET]->val3/100;
	if(sc->data[SC__BLOODYLUST])
		batk += batk * 32 / 100;
	if((sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 2) ||
		(sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 2) ||
		(sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 2) ||
		(sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 2))
		batk += batk * 10 / 100;
	if(sc->data[SC_SUNSTANCE])
		batk += batk * sc->data[SC_SUNSTANCE]->val2 / 100;
	if(sc->data[SC_SHRIMP])
		batk += batk * 10 / 100;
	if(sc->data[SC_JOINTBEAT] && sc->data[SC_JOINTBEAT]->val2&BREAK_WAIST)
		batk -= batk * 25/100;
	if(sc->data[SC_CURSE])
		batk -= batk * 25/100;
//Curse shouldn't effect on this?  <- Curse OR Bleeding??
//	if(sc->data[SC_BLEEDING])
//		batk -= batk * 25/100;
	if(sc->data[SC__ENERVATION])
		batk -= batk * sc->data[SC__ENERVATION]->val2 / 100;
	if (sc->data[SC_CATNIPPOWDER])
		batk -= batk * 50 / 100;
	if(sc->data[SC_EQC])
		batk -= batk * sc->data[SC_EQC]->val3 / 100;
	if (sc->data[SC_VOLCANIC_ASH] && bl->type == BL_MOB && status_get_element(bl) == ELE_WATER)
		batk -= batk * 50 / 100;
	if (sc->data[SC_WATER_BARRIER])
		batk -= batk * 30 / 100;

	return (unsigned short)cap_value(batk,0,USHRT_MAX);
}

// Calculation order (for all except SPEED and ASPD)
// - 1. Calculate increases by a fixed amount.
// - 2. Calculate increases by a percentage.
// - 3. Calculate decreases by a fixed amount.
// - 4. Calculate decreases by a percentage.
static unsigned short status_calc_watk(struct block_list *bl, struct status_change *sc, int watk)
{
	TBL_PC *sd = BL_CAST(BL_PC, bl);

	if(!sc || !sc->count)
		return cap_value(watk,0,USHRT_MAX);

	if(sc->data[SC_IMPOSITIO])
		watk += sc->data[SC_IMPOSITIO]->val2;
	if(sc->data[SC_WATKFOOD])
		watk += sc->data[SC_WATKFOOD]->val1;
	if(sc->data[SC_DRUMBATTLE])
		watk += sc->data[SC_DRUMBATTLE]->val2;
	if(sc->data[SC_VOLCANO])
		watk += sc->data[SC_VOLCANO]->val2;
	if(sc->data[SC_MERC_ATKUP])
		watk += sc->data[SC_MERC_ATKUP]->val2;
	if(sc->data[SC_FIGHTINGSPIRIT])
		watk += sc->data[SC_FIGHTINGSPIRIT]->val1;
	if(sc->data[SC_CAMOUFLAGE])
		watk += 30 * sc->data[SC_CAMOUFLAGE]->val2;
	if(sc->data[SC_STRIKING])
		watk += sc->data[SC_STRIKING]->val2;
	if(sc->data[SC_SHIELDSPELL_DEF] && sc->data[SC_SHIELDSPELL_DEF]->val1 == 3)
		watk += sc->data[SC_SHIELDSPELL_DEF]->val2;
	if( sc->data[SC_BANDING] && sc->data[SC_BANDING]->val2 > 1 )
		watk += (10 + 10 * sc->data[SC_BANDING]->val1) * sc->data[SC_BANDING]->val2;
	if(sc->data[SC_INSPIRATION])
		watk += sc->data[SC_INSPIRATION]->val2;
	if(sc->data[SC_GENTLETOUCH_CHANGE])
		watk += sc->data[SC_GENTLETOUCH_CHANGE]->val2;
	if(sc->data[SC_RUSH_WINDMILL])
		watk += sc->data[SC_RUSH_WINDMILL]->val4;
	if(sc->data[SC_SATURDAY_NIGHT_FEVER])
		watk += 100 * sc->data[SC_SATURDAY_NIGHT_FEVER]->val1;
	if (sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 2)
		watk += 50;
	if(sc->data[SC_ODINS_POWER])
		watk += 40 + 30 * sc->data[SC_ODINS_POWER]->val1;
	if (sc->data[SC_P_ALTER])
		watk += sc->data[SC_P_ALTER]->val2;
	if (sc->data[SC_SOULFALCON])
		watk += sc->data[SC_SOULFALCON]->val2;
	if (sc->data[SC_ZENKAI])
		 watk += sc->data[SC_ZENKAI]->val2;
	if (sc->data[SC_ZANGETSU] && sc->data[SC_ZANGETSU]->val3 == 1)
		watk += 20 * sc->data[SC_ZANGETSU]->val1 + sc->data[SC_ZANGETSU]->val2;
	if (sc->data[SC_FLASHCOMBO])
		watk += sc->data[SC_FLASHCOMBO]->val2;
	if (sc->data[SC_ANGRIFFS_MODUS])
		watk += 50 + 20 * sc->data[SC_ANGRIFFS_MODUS]->val1;
	if (sc->data[SC_PYROCLASTIC])
		watk += sc->data[SC_PYROCLASTIC]->val2;
	if(sc->data[SC_PYROTECHNIC_OPTION])
		watk += 60;
	if(sc->data[SC_HEATER_OPTION])
		watk += 120;
	if(sc->data[SC_TROPIC_OPTION])
		watk += 180;
	if (sc->data[SC_TIDAL_WEAPON])
		watk += sc->data[SC_TIDAL_WEAPON]->val2;
	if(sc->data[SC_FULL_SWING_K])
		watk += sc->data[SC_FULL_SWING_K]->val1;
	if(sc->data[SC_INCATKRATE])
		watk += watk * sc->data[SC_INCATKRATE]->val1/100;
	if(sc->data[SC_PROVOKE])
		watk += watk * sc->data[SC_PROVOKE]->val3/100;
	if(sc->data[SC_CONCENTRATION])
		watk += watk * sc->data[SC_CONCENTRATION]->val2/100;
	if(sc->data[SC_SKE])
		watk += watk * 3;
	if(sc->data[SC_NIBELUNGEN]) {
		if (bl->type != BL_PC)
			watk += sc->data[SC_NIBELUNGEN]->val2;
		else {
			TBL_PC *sd = (TBL_PC*)bl;
			int index = sd->equip_index[sd->state.lr_flag?EQI_HAND_L:EQI_HAND_R];
			if(index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->wlv == 4)
				watk += sc->data[SC_NIBELUNGEN]->val2;
		}
	}
	if(sc->data[SC_BLOODLUST])
		watk += watk * sc->data[SC_BLOODLUST]->val2/100;
	if(sc->data[SC_FLEET])
		watk += watk * sc->data[SC_FLEET]->val3/100;
	if(sc->data[SC__BLOODYLUST])
		watk += watk * 32 / 100;
	if ( bl->type == BL_ELEM )
	{
		unsigned char elem_type;

		TBL_ELEM *ed = BL_CAST(BL_ELEM,bl);

		elem_type = elemental_get_type(ed);

		if ((sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 1 && elem_type == ELEMTYPE_AGNI) || 
			(sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 1 && elem_type == ELEMTYPE_AQUA) || 
			(sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 1 && elem_type == ELEMTYPE_VENTUS) || 
			(sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 1 && elem_type == ELEMTYPE_TERA))
			watk += watk * 20 / 100;
	}
	else if ((sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 2) ||
		(sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 2) ||
		(sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 2) ||
		(sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 2))
		watk += watk * 10 / 100;

	if(sc->data[SC_SUNSTANCE])
		watk += watk * sc->data[SC_SUNSTANCE]->val2 / 100;
	if(sd && sd->charmball > 0 && sd->charmball_type == CHARM_EARTH)
		watk += watk * (10 * sd->charmball) / 100;
	if(sc->data[SC_SHRIMP])
		watk += watk * 10 / 100;
	if (sc->data[SC_ZANGETSU] && sc->data[SC_ZANGETSU]->val3 == 2)
		watk -= 30 * sc->data[SC_ZANGETSU]->val1 + sc->data[SC_ZANGETSU]->val2;
	if(sc->data[SC_CURSE])
		watk -= watk * 25/100;
	if(sc->data[SC_STRIPWEAPON] && bl->type != BL_PC)
		watk -= watk * sc->data[SC_STRIPWEAPON]->val2/100;
	if(sc->data[SC__ENERVATION])
		watk -= watk * sc->data[SC__ENERVATION]->val2 / 100;
	if (sc->data[SC_CATNIPPOWDER])
		watk -= watk * 50 / 100;
	if(sc->data[SC_EQC])
		watk -= watk * sc->data[SC_EQC]->val3 / 100;
	if (sc->data[SC_VOLCANIC_ASH] && bl->type == BL_MOB && status_get_element(bl) == ELE_WATER)
		watk -= watk * 50 / 100;
	if (sc->data[SC_WATER_BARRIER])
		watk -= watk * 30 / 100;

	return (unsigned short)cap_value(watk,0,USHRT_MAX);
}

// Calculation order (for all except SPEED and ASPD)
// - 1. Calculate increases by a fixed amount.
// - 2. Calculate increases by a percentage.
// - 3. Calculate decreases by a fixed amount.
// - 4. Calculate decreases by a percentage.
static unsigned short status_calc_matk(struct block_list *bl, struct status_change *sc, int matk)
{
	if(!sc || !sc->count)
		return cap_value(matk,0,USHRT_MAX);

	if(sc->data[SC_MATKPOTION])
		matk += sc->data[SC_MATKPOTION]->val1;
	if(sc->data[SC_MATKFOOD])
		matk += sc->data[SC_MATKFOOD]->val1;
	if(sc->data[SC_MOONLIT_SERENADE])
		matk += sc->data[SC_MOONLIT_SERENADE]->val4;
	if(sc->data[SC_MANA_PLUS])
		matk += sc->data[SC_MANA_PLUS]->val1;
	if (sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 3)
		matk += 50;
	if (sc->data[SC_ODINS_POWER])
		matk += 40 + 30 * sc->data[SC_ODINS_POWER]->val1;
	if (sc->data[SC_SOULFAIRY])
		matk += sc->data[SC_SOULFAIRY]->val2;
	if(sc->data[SC_IZAYOI])
		matk += sc->data[SC_IZAYOI]->val2;
	if (sc->data[SC_ZANGETSU] && sc->data[SC_ZANGETSU]->val4 == 1)
		matk += 20 * sc->data[SC_ZANGETSU]->val1 + sc->data[SC_ZANGETSU]->val2;
	if (sc->data[SC_SPIRITOFLAND_MATK])
		matk += sc->data[SC_SPIRITOFLAND_MATK]->val2;
	if(sc->data[SC_AQUAPLAY_OPTION])
		matk += 40;
	if(sc->data[SC_COOLER_OPTION])
		matk += 80;
	if(sc->data[SC_CHILLY_AIR_OPTION])
		matk += 120;
	if (sc->data[SC_MTF_MATK2])
		matk += sc->data[SC_MTF_MATK2]->val1;
	if (sc->data[SC_2011RWC_SCROLL])
		matk += 30;
	if(sc->data[SC_MAGICPOWER] && sc->data[SC_MAGICPOWER]->val4)
		matk += matk * sc->data[SC_MAGICPOWER]->val3/100;
	if(sc->data[SC_MINDBREAKER])
		matk += matk * sc->data[SC_MINDBREAKER]->val2/100;
	if(sc->data[SC_INCMATKRATE])
		matk += matk * sc->data[SC_INCMATKRATE]->val1/100;
	if (sc->data[SC_SHRIMP])
		matk += matk * 10 / 100;
	if (sc->data[SC_MTF_MATK])
		matk += matk * sc->data[SC_MTF_MATK]->val1 / 100;
	if (sc->data[SC_ZANGETSU] && sc->data[SC_ZANGETSU]->val4 == 2)
		matk -= 30 * sc->data[SC_ZANGETSU]->val1 + sc->data[SC_ZANGETSU]->val2;
	if (sc->data[SC_CATNIPPOWDER])
		matk -= matk * 50 / 100;

	return (unsigned short)cap_value(matk,0,USHRT_MAX);
}

// Calculation order (for all except SPEED and ASPD)
// - 1. Calculate increases by a fixed amount.
// - 2. Calculate increases by a percentage.
// - 3. Calculate decreases by a fixed amount.
// - 4. Calculate decreases by a percentage.
static signed short status_calc_critical(struct block_list *bl, struct status_change *sc, int critical)
{
	if(!sc || !sc->count)
		return cap_value(critical, 10, SHRT_MAX);

	if (sc->data[SC_INCCRI])
		critical += sc->data[SC_INCCRI]->val2;
	if (sc->data[SC_CRIFOOD])
		critical += sc->data[SC_CRIFOOD]->val1;
	if (sc->data[SC_EXPLOSIONSPIRITS])
		critical += sc->data[SC_EXPLOSIONSPIRITS]->val2;
	if (sc->data[SC_FORTUNE])
		critical += sc->data[SC_FORTUNE]->val2;
	if (sc->data[SC_TRUESIGHT])
		critical += sc->data[SC_TRUESIGHT]->val2;
	if (sc->data[SC_CLOAKING])
		critical += critical;
	if (sc->data[SC_BEYOND_OF_WARCRY])// Recheck. May be incorrect.
		critical += 10 * sc->data[SC_BEYOND_OF_WARCRY]->val3;
	if (sc->data[SC_SOULSHADOW])
		critical += 10 * sc->data[SC_SOULSHADOW]->val3;
	if(sc->data[SC_CAMOUFLAGE])
		critical += critical * (10 * sc->data[SC_CAMOUFLAGE]->val2) / 100;
	if(sc->data[SC__INVISIBILITY])
		critical += critical * sc->data[SC__INVISIBILITY]->val2 / 100;
	if(sc->data[SC__UNLUCKY])
		critical -= critical * sc->data[SC__UNLUCKY]->val2 / 100;
	if (sc->data[SC_STRIKING])
		critical += critical * sc->data[SC_STRIKING]->val1 / 100;

	return (short)cap_value(critical, 10, SHRT_MAX);
}

// Calculation order (for all except SPEED and ASPD)
// - 1. Calculate increases by a fixed amount.
// - 2. Calculate increases by a percentage.
// - 3. Calculate decreases by a fixed amount.
// - 4. Calculate decreases by a percentage.
static signed short status_calc_hit(struct block_list *bl, struct status_change *sc, int hit)
{
	
	if(!sc || !sc->count)
		return cap_value(hit,1,SHRT_MAX);

	if(sc->data[SC_INCHIT])
		hit += sc->data[SC_INCHIT]->val1;
	if(sc->data[SC_HITFOOD])
		hit += sc->data[SC_HITFOOD]->val1;
	if(sc->data[SC_TRUESIGHT])
		hit += sc->data[SC_TRUESIGHT]->val3;
	if(sc->data[SC_HUMMING])
		hit += sc->data[SC_HUMMING]->val2;
	if(sc->data[SC_CONCENTRATION])
		hit += sc->data[SC_CONCENTRATION]->val3;
	if(sc->data[SC_INCREASING])
		hit += 20; // RockmanEXE; changed based on updated [Reddozen]
	if(sc->data[SC_MERC_HITUP])
		hit += sc->data[SC_MERC_HITUP]->val2;
	if(sc->data[SC_INSPIRATION])//Unable to add job level check at this time with current coding. Will add later. [Rytech]
		hit += 5 * sc->data[SC_INSPIRATION]->val1 + 25;
	if(sc->data[SC_SOULFALCON])
		hit += sc->data[SC_SOULFALCON]->val3;
	if (sc->data[SC_MTF_HITFLEE])
		hit += sc->data[SC_MTF_HITFLEE]->val1;
	if (sc->data[SC_MTF_ASPD])
		hit += sc->data[SC_MTF_ASPD]->val2;
	if(sc->data[SC_INCHITRATE])
		hit += hit * sc->data[SC_INCHITRATE]->val1/100;
	if(sc->data[SC_ADJUSTMENT])
		hit -= 30;
	if(sc->data[SC_HEAT_BARREL])
		hit -= sc->data[SC_HEAT_BARREL]->val4;
	if (sc->data[SC_ILLUSIONDOPING])
		hit -= 50;
	if(sc->data[SC_BLIND])
		hit -= hit * 25/100;
	if (sc->data[SC_FEAR])
		hit -= hit * 20 / 100;
	if(sc->data[SC_PYREXIA])
		hit -= hit * 25/100;
	if(sc->data[SC__GROOMY])
		hit -= hit * sc->data[SC__GROOMY]->val2 / 100;
	if (sc->data[SC_FIRE_EXPANSION_TEAR_GAS])
		hit -= hit * 50 / 100;
	if (sc->data[SC_VOLCANIC_ASH])
		hit -= hit * 50 / 100;

	return (short)cap_value(hit,1,SHRT_MAX);
}

// Calculation order (for all except SPEED and ASPD)
// - 1. Calculate increases by a fixed amount.
// - 2. Calculate increases by a percentage.
// - 3. Calculate decreases by a fixed amount.
// - 4. Calculate decreases by a percentage.
static signed short status_calc_flee(struct block_list *bl, struct status_change *sc, int flee)
{
	if( bl->type == BL_PC )
	{
		if( map_flag_gvg(bl->m) )
			flee -= flee * battle_config.gvg_flee_penalty/100;
		else if( map[bl->m].flag.battleground )
			flee -= flee * battle_config.bg_flee_penalty/100;
	}

	if(!sc || !sc->count)
		return cap_value(flee,1,SHRT_MAX);

	if (sc->data[SC_TINDER_BREAKER])
		return 1; //1 = min flee
	if (sc->data[SC_OVERED_BOOST])
		return sc->data[SC_OVERED_BOOST]->val2;
	if(sc->data[SC_INCFLEE])
		flee += sc->data[SC_INCFLEE]->val1;
	if(sc->data[SC_FLEEFOOD])
		flee += sc->data[SC_FLEEFOOD]->val1;
	if(sc->data[SC_WHISTLE])
		flee += sc->data[SC_WHISTLE]->val2;
	if(sc->data[SC_WINDWALK])
		flee += sc->data[SC_WINDWALK]->val2;
	if(sc->data[SC_VIOLENTGALE])
		flee += sc->data[SC_VIOLENTGALE]->val2;
	if(sc->data[SC_MOON_COMFORT]) //SG skill [Komurka]
		flee += sc->data[SC_MOON_COMFORT]->val2;
	if(sc->data[SC_CLOSECONFINE])
		flee += 10;
	if(sc->data[SC_ADJUSTMENT])
		flee += 30;
	if(sc->data[SC_SPEED])
		flee += 10 + sc->data[SC_SPEED]->val1 * 10;
	if (sc->data[SC_PARTYFLEE])
		flee += sc->data[SC_PARTYFLEE]->val1 * 10;
	if(sc->data[SC_MERC_FLEEUP])
		flee += sc->data[SC_MERC_FLEEUP]->val2;
	if( sc->data[SC_HALLUCINATIONWALK] )
		flee += sc->data[SC_HALLUCINATIONWALK]->val2;
	if (sc->data[SC_GOLDENE_FERSE])
		flee += sc->data[SC_GOLDENE_FERSE]->val2;
	if ( sc->data[SC_WIND_STEP_OPTION] )
		flee += flee * 10 / 100;
	if ( sc->data[SC_ZEPHYR] )
		flee += flee * 25 / 100;
	if(sc->data[SC_INCFLEERATE])
		flee += flee * sc->data[SC_INCFLEERATE]->val1/100;
	if ( sc->data[SC_FIRE_EXPANSION_SMOKE_POWDER] )
		flee += flee * 20 / 100;
	if (sc->data[SC_GROOMING])
		flee += sc->data[SC_GROOMING]->val2;
	if (sc->data[SC_MTF_HITFLEE])
		flee += sc->data[SC_MTF_HITFLEE]->val2;
	if(sc->data[SC_GATLINGFEVER])
		flee -= sc->data[SC_GATLINGFEVER]->val4;
	if( sc->data[SC_GLOOMYDAY] )
		flee -= sc->data[SC_GLOOMYDAY]->val2;
	if ( sc->data[SC_C_MARKER] )
		flee -= 10;
	if (sc->data[SC_ANGRIFFS_MODUS])
		flee -= 40 + 20 * sc->data[SC_ANGRIFFS_MODUS]->val1;
	if(sc->data[SC_SPIDERWEB])
		flee -= flee * 50/100;
	if(sc->data[SC_BERSERK])
		flee -= flee * 50/100;
	if (sc->data[SC_BLIND])
		flee -= flee * 25/100;
	if (sc->data[SC_FEAR])
		flee -= flee * 20 / 100;
	if (sc->data[SC_PYREXIA])
		flee -= flee * 25 / 100;
	if (sc->data[SC_PARALYSE])
		flee -= flee / 10; // 10% Flee reduction
	if (sc->data[SC_INFRAREDSCAN])
		flee -= flee * 30 / 100;
	if( sc->data[SC__LAZINESS] )
		flee -= flee * sc->data[SC__LAZINESS]->val2 / 100;
	if(sc->data[SC_SATURDAY_NIGHT_FEVER])
		flee -= flee * (40 + 10 * sc->data[SC_SATURDAY_NIGHT_FEVER]->val1) / 100;
	if (sc->data[SC_FIRE_EXPANSION_TEAR_GAS])
		flee -= flee * 50 / 100;
	if (sc->data[SC_VOLCANIC_ASH] && bl->type == BL_MOB && status_get_element(bl) == ELE_WATER)
		flee -= flee * 50 / 100;
	if (sc->data[SC_WATER_BARRIER])
		flee -= flee * 30 / 100;

	return (short)cap_value(flee,1,SHRT_MAX);
}

// Calculation order (for all except SPEED and ASPD)
// - 1. Calculate increases by a fixed amount.
// - 2. Calculate increases by a percentage.
// - 3. Calculate decreases by a fixed amount.
// - 4. Calculate decreases by a percentage.
static signed short status_calc_flee2(struct block_list *bl, struct status_change *sc, int flee2)
{
	if(!sc || !sc->count)
		return cap_value(flee2,10,SHRT_MAX);

	if(sc->data[SC_INCFLEE2])
		flee2 += sc->data[SC_INCFLEE2]->val2;
	if(sc->data[SC_WHISTLE])
		flee2 += sc->data[SC_WHISTLE]->val3*10;
	if (sc->data[SC_SPIRITOFLAND_PERFECTDODGE])
		flee2 += sc->data[SC_SPIRITOFLAND_PERFECTDODGE]->val2;
	if(sc->data[SC__UNLUCKY])
		flee2 -= flee2 * sc->data[SC__UNLUCKY]->val2 / 100;

	return (short)cap_value(flee2,10,SHRT_MAX);
}

// Calculation order (for all except SPEED and ASPD)
// - 1. Calculate increases by a fixed amount.
// - 2. Calculate increases by a percentage.
// - 3. Calculate decreases by a fixed amount.
// - 4. Calculate decreases by a percentage.
static signed char status_calc_def(struct block_list *bl, struct status_change *sc, int def)
{
	TBL_PC *sd = BL_CAST(BL_PC, bl);

	if(!sc || !sc->count)
		return (signed char)cap_value(def, CHAR_MIN, battle_config.max_def);

	if (sc->data[SC_DEFSET]) //FIXME: Find out if this really overrides all other SCs
		return sc->data[SC_DEFSET]->val1;

	if(sc->data[SC_BERSERK])
		return 0;
	if(sc->data[SC_BARRIER])
		return 100;
	if(sc->data[SC_KEEPING])
		return 90;
	if(sc->data[SC_STEELBODY])
		return 90;
	if (sc->data[SC_UNLIMIT])
		return 1;
	if (sc->data[SC_NYANGGRASS] && sc->data[SC_NYANGGRASS]->val2 == 1)
		return 0;
	if (sc->data[SC_DEFSET])
		return sc->data[SC_DEFSET]->val1;
	if(sc->data[SC_DRUMBATTLE])
		def += sc->data[SC_DRUMBATTLE]->val3;
	if(sc->data[SC_DEFENCE])	//[orn]
		def += sc->data[SC_DEFENCE]->val2;
	if (sc->data[SC_STONEHARDSKIN])
		def += sc->data[SC_STONEHARDSKIN]->val2;
	if( sc->data[SC_SHIELDSPELL_REF] && sc->data[SC_SHIELDSPELL_REF]->val1 == 2 )
		def += sc->data[SC_SHIELDSPELL_REF]->val2;
	if (sc->data[SC_PRESTIGE])
		def += sc->data[SC_PRESTIGE]->val2;
	if( sc->data[SC_BANDING] && sc->data[SC_BANDING]->val2 > 1 )//DEF formula divided by 10 to balance it for us on pre_renewal mechanics. [Rytech]
		def += (5 + sc->data[SC_BANDING]->val1) * sc->data[SC_BANDING]->val2 / 10;
	if(sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 2)
		def += 5;
	if(sc->data[SC_SOULGOLEM])
		def += sc->data[SC_SOULGOLEM]->val2;
	if(sc->data[SC_INCDEFRATE])
		def += def * sc->data[SC_INCDEFRATE]->val1/100;
	if( sc->data[SC_NEUTRALBARRIER] )
		def += def * ( 10 + 5 * sc->data[SC_NEUTRALBARRIER]->val1 ) / 100;
	if (sc->data[SC_FORCEOFVANGUARD])
		def += def * (2 * sc->data[SC_FORCEOFVANGUARD]->val1) / 100;
	if( sc->data[SC_ECHOSONG] )
		def += def * sc->data[SC_ECHOSONG]->val4 / 100;
	// Official increase is 10% per charm for a max of 100%, but is clearly overpowered for pre-re. Setting to 5% per charm to keep it balanced. [Rytech]
	if (sd && sd->charmball > 0 && sd->charmball_type == CHARM_EARTH)
		def += def * (5 * sd->charmball) / 100;
	if (sc->data[SC_WATER_BARRIER])
		def += def * 30 / 100;
	if( sc->data[SC_SOLID_SKIN_OPTION] )
		def += def * 50 / 100;// Official increase is 100%, but is overpowered for pre-re. Setting to 50%.
	if( sc->data[SC_POWER_OF_GAIA] )
		def += def * 50 / 100;// Official increase is 100%, but is overpowered for pre-re. Setting to 50%.
	if (sc->data[SC_ANGRIFFS_MODUS])// Fixed decrease. Divided by 10 to keep it balanced for classic mechanics.
		def -= (30 + 20 * sc->data[SC_ANGRIFFS_MODUS]->val1) / 10;
	if(sc->data[SC_ODINS_POWER])
		def -= 2 * sc->data[SC_ODINS_POWER]->val1;
	if(sc->data[SC_STONE] && sc->opt1 == OPT1_STONE)
		def >>=1;
	if(sc->data[SC_FREEZE])
		def >>=1;
	if(sc->data[SC_SIGNUMCRUCIS])
		def -= def * sc->data[SC_SIGNUMCRUCIS]->val2/100;
	if(sc->data[SC_CONCENTRATION])
		def -= def * sc->data[SC_CONCENTRATION]->val4/100;
	if(sc->data[SC_SKE])
		def >>=1;
	if (bl->type != BL_PC) {
		if (sc->data[SC_PROVOKE]) // Provoke doesn't alter player defense->
			def -= def * sc->data[SC_PROVOKE]->val4 / 100;
		if (sc->data[SC_STRIPSHIELD]) // Player doesn't have def reduction, only equip is removed.
			def -= def * sc->data[SC_STRIPSHIELD]->val2 / 100;
	}
	if (sc->data[SC_FLING])
		def -= def * (sc->data[SC_FLING]->val2)/100;
	if( sc->data[SC_FROST] )
		if (bl->type == BL_MOB)
			def -= def * 10 / 100;
		else
			def -= def * 30 / 100;
	if (sc->data[SC_CAMOUFLAGE])
		def -= def * (5 * sc->data[SC_CAMOUFLAGE]->val2) / 100;
 	if(sc->data[SC_ANALYZE])
		def -= def * ( 14 * sc->data[SC_ANALYZE]->val1 ) / 100;
	if( sc->data[SC__BLOODYLUST] )
		def -= def * 55 / 100;
	if(sc->data[SC_SATURDAY_NIGHT_FEVER])
		def -= def * (10 + 10 * sc->data[SC_SATURDAY_NIGHT_FEVER]->val1) / 100;
	if(sc->data[SC_EARTHDRIVE])
		def -= def * 25 / 100;
	if (sc->data[SC_NYANGGRASS] && sc->data[SC_NYANGGRASS]->val2 == 2)
		def -= def * 50 / 100;
	if (sc->data[SC_NEEDLE_OF_PARALYZE])
		def -= def * sc->data[SC_NEEDLE_OF_PARALYZE]->val2 / 100;
	if(sc->data[SC_OVERED_BOOST])
		def -= def * 50 / 100;
	if(sc->data[SC_EQC])
		def -= def * sc->data[SC_EQC]->val3 / 100;
	if (sc->data[SC_VOLCANIC_ASH] && bl->type == BL_MOB && status_get_race(bl) == RC_PLANT)
		def -= def * 50 / 100;
	if(sc->data[SC_ROCK_CRUSHER])
	{
		if ( bl->type == BL_PC )
			def -= def * 50 / 100;
		else
			def -= def * 30 / 100;
	}

	return (signed char)cap_value(def, CHAR_MIN, battle_config.max_def);
}

// Calculation order (for all except SPEED and ASPD)
// - 1. Calculate increases by a fixed amount.
// - 2. Calculate increases by a percentage.
// - 3. Calculate decreases by a fixed amount.
// - 4. Calculate decreases by a percentage.
static signed short status_calc_def2(struct block_list *bl, struct status_change *sc, int def2)
{
	if(!sc || !sc->count)
		return cap_value(def2,1,SHRT_MAX);
	
	if(sc->data[SC_BERSERK])
		return 0;
	if(sc->data[SC_ETERNALCHAOS])
		return 0;
	if (sc->data[SC_UNLIMIT])
		return 1;
	if (sc->data[SC_DEFSET])
		return sc->data[SC_DEFSET]->val1;
	if(sc->data[SC_SUN_COMFORT])
		def2 += sc->data[SC_SUN_COMFORT]->val2;
	if (sc->data[SC_GENTLETOUCH_REVITALIZE])
		def2 += sc->data[SC_GENTLETOUCH_REVITALIZE]->val4;
	if(sc->data[SC_ANGELUS])
		def2 += def2 * sc->data[SC_ANGELUS]->val2/100;
	if(sc->data[SC_CONCENTRATION])
		def2 -= def2 * sc->data[SC_CONCENTRATION]->val4/100;
	if(sc->data[SC_POISON])
		def2 -= def2 * 25/100;
	if(sc->data[SC_DPOISON])
		def2 -= def2 * 25/100;
	if(sc->data[SC_SKE])
		def2 -= def2 * 50/100;
	if(sc->data[SC_PROVOKE])
		def2 -= def2 * sc->data[SC_PROVOKE]->val4/100;
	if(sc->data[SC_JOINTBEAT])
		def2 -= def2 * ( sc->data[SC_JOINTBEAT]->val2&BREAK_SHOULDER ? 50 : 0 ) / 100
			  + def2 * ( sc->data[SC_JOINTBEAT]->val2&BREAK_WAIST ? 25 : 0 ) / 100;
	if(sc->data[SC_FLING])
		def2 -= def2 * (sc->data[SC_FLING]->val3)/100;
	if(sc->data[SC_ANALYZE])
		def2 -= def2 * ( 14 * sc->data[SC_ANALYZE]->val1 ) / 100;
	if(sc->data[SC_EQC])
		def2 -= def2 * sc->data[SC_EQC]->val3 / 100;

	return (short)cap_value(def2,1,SHRT_MAX);
}

// Calculation order (for all except SPEED and ASPD)
// - 1. Calculate increases by a fixed amount.
// - 2. Calculate increases by a percentage.
// - 3. Calculate decreases by a fixed amount.
// - 4. Calculate decreases by a percentage.
static signed char status_calc_mdef(struct block_list *bl, struct status_change *sc, int mdef)
{
	if(!sc || !sc->count)
		return (signed char)cap_value(mdef, CHAR_MIN, battle_config.max_def);

	if (sc->data[SC_MDEFSET]) //FIXME: Find out if this really overrides all other SCs
		return sc->data[SC_MDEFSET]->val1;

	if(sc->data[SC_BERSERK])
		return 0;
	if(sc->data[SC_BARRIER])
		return 100;
	if(sc->data[SC_STEELBODY])
		return 90;
	if(sc->data[SC_SKA])
		return 90;
	if (sc->data[SC_UNLIMIT])
		return 1;
	if (sc->data[SC_NYANGGRASS] && sc->data[SC_NYANGGRASS]->val2 == 1)
		return 0;
	if (sc->data[SC_MDEFSET])
		return sc->data[SC_MDEFSET]->val1;
	if (sc->data[SC_ENDURE])// It has been confirmed that eddga card grants 1 MDEF, not 0, not 10, but 1.
		mdef += (sc->data[SC_ENDURE]->val4 == 0) ? sc->data[SC_ENDURE]->val1 : 1;
	if (sc->data[SC_CONCENTRATION])
		mdef += 1; //Skill info says it adds a fixed 1 Mdef point.
	if (sc->data[SC_STONEHARDSKIN])
		mdef += sc->data[SC_STONEHARDSKIN]->val2;
	if (sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 3)
		mdef += 5;
	if (sc->data[SC_SOULGOLEM])
		mdef += sc->data[SC_SOULGOLEM]->val3;
	if(sc->data[SC_STONE] && sc->opt1 == OPT1_STONE)
		mdef += 25*mdef/100;
	if(sc->data[SC_FREEZE])
		mdef += 25*mdef/100;
	if (sc->data[SC_NEUTRALBARRIER])
		mdef += mdef * (10 + 5 * sc->data[SC_NEUTRALBARRIER]->val1) / 100;
	if(sc->data[SC_SYMPHONY_LOVE])
		mdef += mdef * sc->data[SC_SYMPHONY_LOVE]->val4 / 100;
	if(sc->data[SC_WATER_BARRIER])
		mdef += mdef * 20 / 100;
	if(sc->data[SC_GENTLETOUCH_CHANGE])
	{
		mdef -= sc->data[SC_GENTLETOUCH_CHANGE]->val4;
		if ( mdef < 0 )
				mdef = 0;
	}
	if (sc->data[SC_ODINS_POWER])
		mdef -= 2 * sc->data[SC_ODINS_POWER]->val1;
	if (sc->data[SC_BURNING])
		mdef -= mdef * 25 / 100;
	if(sc->data[SC_ANALYZE])
		mdef -= mdef * ( 14 * sc->data[SC_ANALYZE]->val1 ) / 100;
	if (sc->data[SC_NYANGGRASS] && sc->data[SC_NYANGGRASS]->val2 == 2)
		mdef -= mdef * 50 / 100;
	if (sc->data[SC_NEEDLE_OF_PARALYZE])
		mdef -= mdef * sc->data[SC_NEEDLE_OF_PARALYZE]->val2 / 100;

	return (signed char)cap_value(mdef, CHAR_MIN, battle_config.max_def);
}

static signed short status_calc_mdef2(struct block_list *bl, struct status_change *sc, int mdef2)
{
	if(!sc || !sc->count)
		return cap_value(mdef2,1,SHRT_MAX);

	if(sc->data[SC_BERSERK])
		return 0;
	if (sc->data[SC_UNLIMIT])
		return 1;
	if (sc->data[SC_MDEFSET])
		return sc->data[SC_MDEFSET]->val1;
	if(sc->data[SC_MINDBREAKER])
		mdef2 -= mdef2 * sc->data[SC_MINDBREAKER]->val3/100;
	if(sc->data[SC_ANALYZE])
		mdef2 -= mdef2 * ( 14 * sc->data[SC_ANALYZE]->val1 ) / 100;

	return (short)cap_value(mdef2,1,SHRT_MAX);
}

static unsigned short status_calc_speed(struct block_list *bl, struct status_change *sc, int speed)
{
	TBL_PC* sd = BL_CAST(BL_PC, bl);
	int speed_rate;

	if( sc == NULL )
		return cap_value(speed,10,USHRT_MAX);

	if( sd && sd->ud.skilltimer != -1 && ( sd->ud.skill_id == LG_EXEEDBREAK || pc_checkskill(sd,SA_FREECAST) > 0) )
	{
		if( sd->ud.skill_id == LG_EXEEDBREAK )
			speed_rate = 160 - 10 * sd->ud.skill_lv;// -50% at skill_lv 1 -> -10% at skill_lv 5
		else
			speed_rate = 175 - 5 * pc_checkskill(sd,SA_FREECAST);
	}
	else
	{
		speed_rate = 100;

		//GetMoveHasteValue2()
		{
			int val = 0;

			if( sc->data[SC_FUSION] )
				val = 25;
			else
			if( sc->data[SC_ALL_RIDING] )
				val = battle_config.all_riding_speed;
			else
			if (sd && (pc_isriding(sd) || pc_isdragon(sd) || pc_ismadogear(sd)))
				val = 25;
			else
			if( sd && pc_iswugrider(sd) )//Formula confirmed from testing and finalized. Do not touch. [Rytech]
				val = 15 + 5 * pc_checkskill(sd, RA_WUGRIDER);

			speed_rate -= val;
		}

		//GetMoveSlowValue()
		{
			int val = 0;

			if( sd && sc->data[SC_HIDING] && pc_checkskill(sd,RG_TUNNELDRIVE) > 0 )
				val = 120 - 6 * pc_checkskill(sd,RG_TUNNELDRIVE);
			else
			if( sd && sc->data[SC_CHASEWALK] && sc->data[SC_CHASEWALK]->val3 < 0 )
				val = sc->data[SC_CHASEWALK]->val3;
			else
			{
				// Longing for Freedom cancels song/dance penalty
				if( sc->data[SC_LONGING] )
					val = max( val, 50 - 10 * sc->data[SC_LONGING]->val1 );
				else
				if( sd && sc->data[SC_DANCING] )
					val = max( val, 500 - (40 + 10 * (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_BARDDANCER)) * pc_checkskill(sd,(sd->status.sex?BA_MUSICALLESSON:DC_DANCINGLESSON)) );

				if (sc->data[SC_DECREASEAGI])
					val = max( val, 25 );
				if (sc->data[SC_QUAGMIRE])
					val = max( val, 50 );
				if( sc->data[SC_DONTFORGETME] )
					val = max( val, sc->data[SC_DONTFORGETME]->val3 );
				if( sc->data[SC_CURSE] )
					val = max( val, 300 );
				if( sc->data[SC_CHASEWALK] )
					val = max( val, sc->data[SC_CHASEWALK]->val3 );
				if( sc->data[SC_WEDDING] )
					val = max( val, 100 );
				if( sc->data[SC_JOINTBEAT] && sc->data[SC_JOINTBEAT]->val2&(BREAK_ANKLE|BREAK_KNEE) )
					val = max( val, (sc->data[SC_JOINTBEAT]->val2&BREAK_ANKLE ? 50 : 0) + (sc->data[SC_JOINTBEAT]->val2&BREAK_KNEE ? 30 : 0) );
				if( sc->data[SC_CLOAKING] && (sc->data[SC_CLOAKING]->val4&1) == 0 )
					val = max( val, sc->data[SC_CLOAKING]->val1 < 3 ? 300 : 30 - 3 * sc->data[SC_CLOAKING]->val1 );
				if( sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_ENEMY )
					val = max( val, 75 );
				if( sc->data[SC_SLOWDOWN] ) // Slow Potion
					val = max( val, 100 );
				if (sc->data[SC_MOVESLOW_POTION]) // Used by Slow_Down_Potion [Frost]
					val = max(val, sc->data[SC_MOVESLOW_POTION]->val1);
				if( sc->data[SC_GATLINGFEVER] )
					val = max( val, 100 );
				if( sc->data[SC_SUITON] )
					val = max( val, sc->data[SC_SUITON]->val3 );
				if( sc->data[SC_SWOO] )
					val = max( val, 300 );
				if (sc->data[SC_SKA])
					val = max(val, 25);
				if (sc->data[SC_FROST])
					val = max(val, 70);
				if( sc->data[SC_ADORAMUS] )
					val = max( val, 25 );
				if( sc->data[SC_HALLUCINATIONWALK_POSTDELAY] )
					val = max( val, 50 );
				if( sc->data[SC_PARALYSE] )
					val = max( val, 50 );
				if( sc->data[SC_MARSHOFABYSS] )
					val = max(val, sc->data[SC_MARSHOFABYSS]->val3);
				if (sc->data[SC_CAMOUFLAGE] && sc->data[SC_CAMOUFLAGE]->val1 > 2)
					val = max(val, 25 * (5 - sc->data[SC_CAMOUFLAGE]->val1));
				if( sc->data[SC_STEALTHFIELD_MASTER] )
					val = max(val, 20);//Description says decreases casters movement speed by 20%. [Rytech]
				if (sc->data[SC__LAZINESS])
					val = max(val, 25);
				if( sc->data[SC_BANDING_DEFENCE] )
					val = max(val, 90);
				if (sc->data[SC_GLOOMYDAY] && sc->data[SC_GLOOMYDAY]->val1 == 2)
					val = max(val, 50);
				if (sc->data[SC_B_TRAP])
					val = max( val, 90 );
				if( sc->data[SC_CREATINGSTAR] )
					val = max(val, 90);
				if (sc->data[SC_SP_SHA])
					val = max(val, 50);
				if( sc->data[SC_POWER_OF_GAIA] )
					val = max( val, sc->data[SC_POWER_OF_GAIA]->val2 );
				if( sc->data[SC_ROCK_CRUSHER_ATK] )
					val = max( val, 50 );
				if( sc->data[SC_MELON_BOMB] )
					val = max( val, sc->data[SC_MELON_BOMB]->val2 );
				if (sc->data[SC_REBOUND])
					val = max(val, 25);

				if( sd && sd->bonus.speed_rate + sd->bonus.speed_add_rate > 0 ) // permanent item-based speedup
					val = max( val, sd->bonus.speed_rate + sd->bonus.speed_add_rate );
			}

			speed_rate += val;
		}

		//GetMoveHasteValue1()
		{
			int val = 0;

			if( sc->data[SC_SPEEDUP1] ) //FIXME: used both by NPC_AGIUP and Speed Potion script
				val = max( val, 50 );
			if( sc->data[SC_INCREASEAGI] )
				val = max( val, 25 );
			if( sc->data[SC_WINDWALK] )
				val = max( val, 2 * sc->data[SC_WINDWALK]->val1 );
			if( sc->data[SC_CARTBOOST] )
				val = max( val, 20 );
			if( sd && (sd->class_&MAPID_UPPERMASK) == MAPID_ASSASSIN && pc_checkskill(sd,TF_MISS) > 0 )
				val = max( val, 1 * pc_checkskill(sd,TF_MISS) );
			if( sc->data[SC_CLOAKING] && (sc->data[SC_CLOAKING]->val4&1) == 1 )
				val = max( val, sc->data[SC_CLOAKING]->val1 >= 10 ? 25 : 3 * sc->data[SC_CLOAKING]->val1 - 3 );
			if( sc->data[SC_BERSERK] )
				val = max( val, 25 );
			if( sc->data[SC_RUN] )
				val = max( val, 55 );
			if( sc->data[SC_AVOID] )
				val = max( val, 10 * sc->data[SC_AVOID]->val1 );
			if( sc->data[SC_INVINCIBLE] && !sc->data[SC_INVINCIBLEOFF] )
				val = max( val, 75 );
			if (sc->data[SC_ACCELERATION])
				val = max(val, 25);
			if( sc->data[SC_CLOAKINGEXCEED] )
				val = max( val, sc->data[SC_CLOAKINGEXCEED]->val3);
			if (sc->data[SC_SWING])
				val = max( val, 25 );
			if( sc->data[SC_GN_CARTBOOST] )
				val = max( val, sc->data[SC_GN_CARTBOOST]->val2 );
			//I can only guess Full Throttles placement and speed increase right now. [Rytech]
			if (sc->data[SC_FULL_THROTTLE])
				val = max( val, 100 );
			if (sc->data[SC_ARCLOUSEDASH])// Speed increase not confirmed but is likely the same as AGI Up. [Rytech]
				val = max(val, 25);
			if (sc->data[SC_SPIRITOFLAND_SPEED])
				val = max(val, 60);
			if (sc->data[SC_WIND_STEP_OPTION])
				val = max(val, 50);

			//FIXME: official items use a single bonus for this [ultramage]
			if( sc->data[SC_SPEEDUP0] ) // temporary item-based speedup
				val = max( val, 25 );
			if( sd && sd->bonus.speed_rate + sd->bonus.speed_add_rate < 0 ) // permanent item-based speedup
				val = max( val, -(sd->bonus.speed_rate + sd->bonus.speed_add_rate) );

			speed_rate -= val;
		}

		if( speed_rate < 40 )
			speed_rate = 40;
	}

	//GetSpeed()
	{
		if( sd && pc_iscarton(sd) )
			speed += speed * (50 - 5 * pc_checkskill(sd,MC_PUSHCART)) / 100;
		if( sd && pc_ismadogear(sd) )
			speed += speed * (50 - 10 * pc_checkskill(sd,NC_MADOLICENCE)) / 100;
		if( speed_rate != 100 )
			speed = speed * speed_rate / 100;
		if( sc->data[SC_STEELBODY] )
			speed = 200;
		if( sc->data[SC_DEFENDER] )
			speed = max(speed, 200);
		if( sc->data[SC_WALKSPEED] && sc->data[SC_WALKSPEED]->val1 > 0 ) // ChangeSpeed
			speed = speed * 100 / sc->data[SC_WALKSPEED]->val1;
	}

	return (short)cap_value(speed,10,USHRT_MAX);
}

/// Calculates an object's ASPD modifier by a fixed amount.
/// Calculation is done before aspd_rate.
/// Note that the scale of aspd_amount is 10 = 1 ASPD.
static short status_calc_aspd_amount(struct block_list *bl, struct status_change *sc, int aspd_amount)
{
	if( !sc || !sc->count )
		return cap_value(aspd_amount,0,SHRT_MAX);

	if (sc->data[SC_FIGHTINGSPIRIT])
		aspd_amount += 4 * sc->data[SC_FIGHTINGSPIRIT]->val2;
	if (sc->data[SC_HEAT_BARREL])
		aspd_amount += 10 * sc->data[SC_HEAT_BARREL]->val1;
	if (sc->data[SC_SOULSHADOW])
		aspd_amount += 10 * sc->data[SC_SOULSHADOW]->val2;
	if (sc->data[SC_GUST_OPTION] || sc->data[SC_BLAST_OPTION] || sc->data[SC_WILD_STORM_OPTION])
		aspd_amount += 10 * 5;
	if (sc->data[SC_EXTRACT_SALAMINE_JUICE]) // Correct way to handle? [15peaces]
		aspd_amount += sc->data[SC_EXTRACT_SALAMINE_JUICE]->val1;
	if (sc->data[SC_MTF_ASPD])
		aspd_amount -= sc->data[SC_MTF_ASPD]->val1;
	if (sc->data[SC_MTF_ASPD2])
		aspd_amount -= sc->data[SC_MTF_ASPD2]->val1;

	return (short)cap_value(aspd_amount,0,SHRT_MAX);
}

/// Calculates an object's ASPD modifier (alters the base amotion value).
/// Note that the scale of aspd_rate is 1000 = 100%.
static short status_calc_aspd_rate(struct block_list *bl, struct status_change *sc, int aspd_rate)
{
	int i;
	if(!sc || !sc->count)
		return cap_value(aspd_rate,0,SHRT_MAX);

	if(!sc->data[SC_QUAGMIRE])
	{
		int max = 0;
		if(sc->data[SC_STAR_COMFORT])
			max = sc->data[SC_STAR_COMFORT]->val2;

		if(sc->data[SC_TWOHANDQUICKEN] &&
			max < sc->data[SC_TWOHANDQUICKEN]->val2)
			max = sc->data[SC_TWOHANDQUICKEN]->val2;

		if(sc->data[SC_ONEHAND] &&
			max < sc->data[SC_ONEHAND]->val2)
			max = sc->data[SC_ONEHAND]->val2;

		if(sc->data[SC_MERC_QUICKEN] &&
			max < sc->data[SC_MERC_QUICKEN]->val2)
			max = sc->data[SC_MERC_QUICKEN]->val2;

		if(sc->data[SC_ADRENALINE2] &&
			max < sc->data[SC_ADRENALINE2]->val3)
			max = sc->data[SC_ADRENALINE2]->val3;
		
		if(sc->data[SC_ADRENALINE] &&
			max < sc->data[SC_ADRENALINE]->val3)
			max = sc->data[SC_ADRENALINE]->val3;
		
		if(sc->data[SC_SPEARQUICKEN] &&
			max < sc->data[SC_SPEARQUICKEN]->val2)
			max = sc->data[SC_SPEARQUICKEN]->val2;

		if(sc->data[SC_GATLINGFEVER] &&
			max < sc->data[SC_GATLINGFEVER]->val2)
			max = sc->data[SC_GATLINGFEVER]->val2;
		
		if(sc->data[SC_FLEET] &&
			max < sc->data[SC_FLEET]->val2)
			max = sc->data[SC_FLEET]->val2;

		if(sc->data[SC_ASSNCROS] &&
			max < sc->data[SC_ASSNCROS]->val2)
		{
			if (bl->type!=BL_PC)
				max = sc->data[SC_ASSNCROS]->val2;
			else
			switch(((TBL_PC*)bl)->status.weapon)
			{
				case W_BOW:
				case W_REVOLVER:
				case W_RIFLE:
				case W_GATLING:
				case W_SHOTGUN:
				case W_GRENADE:
					break;
				default:
					max = sc->data[SC_ASSNCROS]->val2;
			}
		}
		aspd_rate -= max;

	  	//These stack with the rest of bonuses.
		max = 0;
		if (sc->data[SC_BERSERK])
			aspd_rate -= 300;
			max = 300;

		if( sc->data[SC_MADNESSCANCEL] && max < 200 )
			max = 200;

		if( sc->data[SC_SWING] && max < (10 * sc->data[SC_SWING]->val3) )
			max = 10 * sc->data[SC_SWING]->val3;

		if( sc->data[SC_DANCE_WITH_WUG] && max < (10 * sc->data[SC_DANCE_WITH_WUG]->val3) )
			max = 10 * sc->data[SC_DANCE_WITH_WUG]->val3;

		aspd_rate -= max;
	}

	if(sc->data[i=SC_ASPDPOTION3] ||
		sc->data[i=SC_ASPDPOTION2] ||
		sc->data[i=SC_ASPDPOTION1] ||
		sc->data[i=SC_ASPDPOTION0])
		aspd_rate -= sc->data[i]->val2;
	if( sc->data[SC_GENTLETOUCH_CHANGE] )
		aspd_rate -= 10 * sc->data[SC_GENTLETOUCH_CHANGE]->val3;
	if (sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 2)
		aspd_rate -= 100;
	if (sc->data[SC_GOLDENE_FERSE])
		aspd_rate -= sc->data[SC_GOLDENE_FERSE]->val3;
	if( sc->data[SC_BOOST500] )
		aspd_rate -= 10 * sc->data[SC_BOOST500]->val1;
	if(sc->data[SC_EXTRACT_SALAMINE_JUICE])
		aspd_rate -= 10 * sc->data[SC_EXTRACT_SALAMINE_JUICE]->val1;
	if(sc->data[SC_STARSTANCE])
		aspd_rate -= 10 * sc->data[SC_STARSTANCE]->val2;
	if(sc->data[SC_ATTHASTE_CASH])
		aspd_rate -= sc->data[SC_ATTHASTE_CASH]->val2;
	if(sc->data[SC_DONTFORGETME])
		aspd_rate += sc->data[SC_DONTFORGETME]->val2;
	if(sc->data[SC_LONGING])
		aspd_rate += sc->data[SC_LONGING]->val2;
	if(sc->data[SC_STEELBODY])
		aspd_rate += 250;
	if(sc->data[SC_SKA])
		aspd_rate += 250;
	if(sc->data[SC_DEFENDER])
		aspd_rate += sc->data[SC_DEFENDER]->val4;
	if(sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_ENEMY)
		aspd_rate += 250;
	if(sc->data[SC_GRAVITATION])
		aspd_rate += sc->data[SC_GRAVITATION]->val2;
	if(sc->data[SC_JOINTBEAT]) {
		if( sc->data[SC_JOINTBEAT]->val2&BREAK_WRIST )
			aspd_rate += 250;
		if( sc->data[SC_JOINTBEAT]->val2&BREAK_KNEE )
			aspd_rate += 100;
	}
	if (sc->data[SC_FROST])
		aspd_rate += 300;
	if( sc->data[SC_HALLUCINATIONWALK_POSTDELAY] )
		aspd_rate += 500;
	if( sc->data[SC_PARALYSE] )
		aspd_rate += 100;
	if( sc->data[SC__BODYPAINT] )
		aspd_rate += 10 * (5 * sc->data[SC__BODYPAINT]->val1);
	if( sc->data[SC__INVISIBILITY] )
		aspd_rate += 10 * sc->data[SC__INVISIBILITY]->val3;
	if( sc->data[SC__GROOMY] )
		aspd_rate += 10 * sc->data[SC__GROOMY]->val3;
	if( sc->data[SC_GLOOMYDAY] )
		aspd_rate += 10 * sc->data[SC_GLOOMYDAY]->val3;
	if( sc->data[SC_EARTHDRIVE] )
		aspd_rate += 250;
	if (sc->data[SC_PAIN_KILLER])
		aspd_rate += 10 * sc->data[SC_PAIN_KILLER]->val3;
	if( sc->data[SC_MELON_BOMB] )
		aspd_rate += 10 * sc->data[SC_MELON_BOMB]->val3;

	return (short)cap_value(aspd_rate,0,SHRT_MAX);
}

static unsigned short status_calc_dmotion(struct block_list *bl, struct status_change *sc, int dmotion)
{
	/// It has been confirmed on official servers that MvP mobs have no dmotion even without endure
	if (sc->data[SC_ENDURE] || (bl->type == BL_MOB && status_get_class_(bl) == CLASS_BOSS))
		return 0;

	if( !sc || !sc->count || map_flag_gvg2(bl->m) || map[bl->m].flag.battleground )
		return cap_value(dmotion,0,USHRT_MAX);
		
	if( sc->data[SC_ENDURE] )
		return 0;
	if( sc->data[SC_RUN] || sc->data[SC_WUGDASH] )
		return 0;

	return (unsigned short)cap_value(dmotion,0,USHRT_MAX);
}

/**
* Calculates a max HP based on status changes
* Values can either be percentages or fixed, based on how equations are formulated
* Examples: maxhp += maxhp * value; (percentage increase)
* @param bl: Object's block_list data
* @param maxhp: Object's current max HP
* @return modified maxhp
**/
static unsigned int status_calc_maxhp(struct block_list *bl, uint64 maxhp)
{
	int rate = 100;

	maxhp += status_get_hpbonus(bl, STATUS_BONUS_FIX);

	if ((rate += status_get_hpbonus(bl, STATUS_BONUS_RATE)) != 100)
		maxhp = maxhp * rate / 100;

	return (unsigned int)cap_value(maxhp, 1, UINT_MAX);
}

/**
* Calculates a max SP based on status changes
* Values can either be percentages or fixed, bas ed on how equations are formulated
* Examples: maxsp += maxsp * value; (percentage increase)
* @param bl: Object's block_list data
* @param maxsp: Object's current max SP
* @return modified maxsp
**/
static unsigned int status_calc_maxsp(struct block_list *bl, uint64 maxsp)
{
	int rate = 100;

	maxsp += status_get_spbonus(bl, STATUS_BONUS_FIX);

	if ((rate += status_get_spbonus(bl, STATUS_BONUS_RATE)) != 100)
		maxsp = maxsp * rate / 100;

	return (unsigned int)cap_value(maxsp, 1, UINT_MAX);
}

static unsigned char status_calc_element(struct block_list *bl, struct status_change *sc, int element)
{
	if(!sc || !sc->count)
		return cap_value(element, 0, UCHAR_MAX);

	if(sc->data[SC_FREEZE])
		return ELE_WATER;
	if(sc->data[SC_STONE] && sc->opt1 == OPT1_STONE)
		return ELE_EARTH;
	if(sc->data[SC_BENEDICTIO])
		return ELE_HOLY;
	if(sc->data[SC_CHANGEUNDEAD])
		return ELE_UNDEAD;
	if(sc->data[SC_ELEMENTALCHANGE])
		return sc->data[SC_ELEMENTALCHANGE]->val2;
	if(sc->data[SC_SHAPESHIFT])
		return sc->data[SC_SHAPESHIFT]->val2;

	return (unsigned char)cap_value(element,0,UCHAR_MAX);
}

static unsigned char status_calc_element_lv(struct block_list *bl, struct status_change *sc, int lv)
{
	if(!sc || !sc->count)
		return cap_value(lv, 1, 4);

	if(sc->data[SC_FREEZE])	
		return 1;
	if(sc->data[SC_STONE] && sc->opt1 == OPT1_STONE)
		return 1;
	if(sc->data[SC_BENEDICTIO])
		return 1;
	if(sc->data[SC_CHANGEUNDEAD])
		return 1;
	if(sc->data[SC_ELEMENTALCHANGE])
		return sc->data[SC_ELEMENTALCHANGE]->val1;
	if(sc->data[SC_SHAPESHIFT])
		return sc->data[SC_SHAPESHIFT]->val2;

	return (unsigned char)cap_value(lv,1,4);
}


unsigned char status_calc_attack_element(struct block_list *bl, struct status_change *sc, int element)
{
	if(!sc || !sc->count)
		return cap_value(element, 0, UCHAR_MAX);
	if(sc->data[SC_ENCHANTARMS])
		return sc->data[SC_ENCHANTARMS]->val2;
	if(sc->data[SC_WATERWEAPON] || (sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 2) || sc->data[SC_TIDAL_WEAPON] || sc->data[SC_TIDAL_WEAPON_OPTION])
		return ELE_WATER;
	if (sc->data[SC_EARTHWEAPON] || (sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 2))
		return ELE_EARTH;
	if(sc->data[SC_FIREWEAPON] || (sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 2) || sc->data[SC_PYROCLASTIC])
		return ELE_FIRE;
	if(sc->data[SC_WINDWEAPON] || (sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 2))
		return ELE_WIND;
	if(sc->data[SC_ENCPOISON])
		return ELE_POISON;
	if(sc->data[SC_ASPERSIO])
		return ELE_HOLY;
	if(sc->data[SC_SHADOWWEAPON])
		return ELE_DARK;
	if(sc->data[SC_GHOSTWEAPON] || sc->data[SC__INVISIBILITY])
		return ELE_GHOST;
	return (unsigned char)cap_value(element,0,UCHAR_MAX);
}

static enum e_mode status_calc_mode(struct block_list *bl, struct status_change *sc, enum e_mode mode)
{
	if(!sc || !sc->count)
		return cap_value(mode, 0, INT_MAX);
	if(sc->data[SC_MODECHANGE]) {
		if (sc->data[SC_MODECHANGE]->val2)
			mode = (mode&~MD_MASK) | sc->data[SC_MODECHANGE]->val2; //Set mode
		if (sc->data[SC_MODECHANGE]->val3)
			mode|= sc->data[SC_MODECHANGE]->val3; //Add mode
		if (sc->data[SC_MODECHANGE]->val4)
			mode&=~sc->data[SC_MODECHANGE]->val4; //Del mode
	}
	return cap_value(mode,0, INT_MAX);
}

/**
 * Changes the mode of a slave mob
 * @param md: Slave mob whose mode to change
 * @param mmd: Master of slave mob
 */
void status_calc_slave_mode(struct mob_data *md, struct mob_data *mmd)
{
	switch (battle_config.slaves_inherit_mode) {
	case 1: //Always aggressive
		if (!status_has_mode(&md->status, MD_AGGRESSIVE))
			sc_start4(&md->bl, SC_MODECHANGE, 100, 1, 0, MD_AGGRESSIVE, 0, 0);
		break;
	case 2: //Always passive
		if (status_has_mode(&md->status, MD_AGGRESSIVE))
			sc_start4(&md->bl, SC_MODECHANGE, 100, 1, 0, 0, MD_AGGRESSIVE, 0);
		break;
	case 4: // Overwrite with slave mode
		sc_start4(&md->bl, SC_MODECHANGE, 100, 1, MD_CANMOVE | MD_NORANDOM_WALK | MD_CANATTACK, 0, 0, 0);
		break;
	default: //Copy master
		if (status_has_mode(&mmd->status, MD_AGGRESSIVE))
			sc_start4(&md->bl, SC_MODECHANGE, 100, 1, 0, MD_AGGRESSIVE, 0, 0);
		else
			sc_start4(&md->bl, SC_MODECHANGE, 100, 1, 0, 0, MD_AGGRESSIVE, 0);
		break;
	}
}

const char* status_get_name(struct block_list *bl)
{
	nullpo_ret(bl);
	switch (bl->type) {
	case BL_PC:  return ((TBL_PC *)bl)->fakename[0] != '\0' ? ((TBL_PC*)bl)->fakename : ((TBL_PC*)bl)->status.name;
	case BL_MOB: return ((TBL_MOB*)bl)->name;
	case BL_PET: return ((TBL_PET*)bl)->pet.name;
	case BL_HOM: return ((TBL_HOM*)bl)->homunculus.name;
	//case BL_MER: // They only have database names which are global, not specific to GID.
	case BL_NPC: return ((TBL_NPC*)bl)->name;
	//case BL_ELEM: // They only have database names which are global, not specific to GID.
	}
	return "Unknown";
}

/*==========================================
 * 対象のClassを返す(汎用)
 * 戻りは整数で0以上
 *------------------------------------------*/
int status_get_class(struct block_list *bl) {
	nullpo_ret(bl);
	switch( bl->type ) {
		case BL_PC:  return ((TBL_PC*)bl)->status.class_;
		case BL_MOB: return ((TBL_MOB*)bl)->vd->class_; //Class used on all code should be the view class of the mob.
		case BL_PET: return ((TBL_PET*)bl)->pet.class_;
		case BL_HOM: return ((TBL_HOM*)bl)->homunculus.class_;
		case BL_MER: return ((TBL_MER*)bl)->mercenary.class_;
		case BL_NPC: return ((TBL_NPC*)bl)->class_;
		case BL_ELEM: return ((TBL_ELEM*)bl)->elemental.class_;
	}
	return 0;
}
/*==========================================
 * 対象のレベルを返す(汎用)
 * 戻りは整数で0以上
 *------------------------------------------*/
int status_get_lv(struct block_list *bl) {
	nullpo_ret(bl);
	switch (bl->type) {
		case BL_PC:  return ((TBL_PC*)bl)->status.base_level;
		case BL_MOB: return ((TBL_MOB*)bl)->level;
		case BL_PET: return ((TBL_PET*)bl)->pet.level;
		case BL_HOM: return ((TBL_HOM*)bl)->homunculus.level;
		case BL_MER: return ((TBL_MER*)bl)->db->lv;
		case BL_ELEM: return ((TBL_ELEM*)bl)->db->lv;
		case BL_NPC: return ((TBL_NPC*)bl)->level;
	}
	return 1;
}

int status_get_job_lv(struct block_list *bl) {
	nullpo_ret(bl);
	switch (bl->type) {
		case BL_PC:
			return ((TBL_PC*)bl)->status.job_level;
		//Non-Player characters don't have job levels. Well just send the most common max job level.
		//This will allow skills and status's that take job levels into formula's to have max effectiveness
		//for non-player characters using them. [Rytech]
		case BL_MOB:
		case BL_PET:
		case BL_HOM:
		case BL_MER:
		// case BL_ELEM: // Do Elementals need a Joblevel? [15peaces]
			return 50;
	}
	return 1;
}

// Special base level check for use with skill related stuff.
// This sets a limit on the highest a base level will affect the skill.
int status_get_base_lv_effect(struct block_list *bl)
{
	short base_lv = 1;

	nullpo_ret(bl);
	switch (bl->type)
	{
		case BL_PC:
			base_lv = ((TBL_PC*)bl)->status.base_level;
			break;
		case BL_MOB:
			base_lv = ((TBL_MOB*)bl)->level;
			break;
		case BL_PET:
			base_lv = ((TBL_PET*)bl)->pet.level;
			break;
		case BL_HOM:
			base_lv = ((TBL_HOM*)bl)->homunculus.level;
			break;
		case BL_MER:
			base_lv = ((TBL_MER*)bl)->db->lv;
			break;
		case BL_ELEM:
			base_lv = ((TBL_ELEM*)bl)->db->lv;
			break;
	}

	// Base level limiter. Config setting limits how high
	// of a base level is allowed. Anything higher is set
	// to the max allowed.
	if ( base_lv > battle_config.base_lv_skill_effect_limit )
		base_lv = battle_config.base_lv_skill_effect_limit;

	return base_lv;
}

// Special job level check for use with skill related stuff.
// This sets a limit on the highest a job level will affect the skill.
int status_get_job_lv_effect(struct block_list *bl)
{
	short job_lv = 1;

	nullpo_ret(bl);
	switch (bl->type)
	{
		case BL_PC:
			job_lv = ((TBL_PC*)bl)->status.job_level;
			break;
		//Non-Player characters don't have job levels. Well just send the most common max job level.
		//This will allow skills and status's that take job levels into formula's to have max effectiveness
		//for non-player characters using them. [Rytech]
		case BL_MOB:
		case BL_PET:
		case BL_HOM:
		case BL_MER:
		case BL_ELEM:
			job_lv = 50;
			break;
	}

	// Job level limiter. Config setting limits how high
	// of a job level is allowed. Anything higher is set
	// to the max allowed.
	if ( job_lv > battle_config.job_lv_skill_effect_limit )
		job_lv = battle_config.job_lv_skill_effect_limit;

	return job_lv;
}

struct regen_data *status_get_regen_data(struct block_list *bl) {
	nullpo_retr(NULL, bl);
	switch (bl->type) {
		case BL_PC:  return &((TBL_PC*)bl)->regen;
		case BL_HOM: return &((TBL_HOM*)bl)->regen;
		case BL_MER: return &((TBL_MER*)bl)->regen;
		case BL_ELEM: return &((TBL_ELEM*)bl)->regen;
		default:
			return NULL;
	}
}

struct status_data *status_get_status_data(struct block_list *bl) {
	nullpo_retr(&dummy_status, bl);

	switch (bl->type) {
		case BL_PC:  return &((TBL_PC*)bl)->battle_status;
		case BL_MOB: return &((TBL_MOB*)bl)->status;
		case BL_PET: return &((TBL_PET*)bl)->status;
		case BL_HOM: return &((TBL_HOM*)bl)->battle_status;
		case BL_MER: return &((TBL_MER*)bl)->battle_status;
		case BL_ELEM: return &((TBL_ELEM*)bl)->battle_status;
		case BL_NPC: return &((TBL_NPC*)bl)->status;
		default:
			return &dummy_status;
	}
}

struct status_data *status_get_base_status(struct block_list *bl) {
	nullpo_retr(NULL, bl);
	switch (bl->type) {
		case BL_PC:  return &((TBL_PC*)bl)->base_status;
		case BL_MOB: return ((TBL_MOB*)bl)->base_status ? ((TBL_MOB*)bl)->base_status : &((TBL_MOB*)bl)->db->status;
		case BL_PET: return &((TBL_PET*)bl)->db->status;
		case BL_HOM: return &((TBL_HOM*)bl)->base_status;
		case BL_MER: return &((TBL_MER*)bl)->base_status;
		case BL_ELEM: return &((TBL_ELEM*)bl)->base_status;
		case BL_NPC: return &((TBL_NPC*)bl)->status;
		default:
			return NULL;
	}
}

signed char status_get_def(struct block_list *bl)
{
	struct unit_data *ud;
	struct status_data *status = status_get_status_data(bl);
	int def = status?status->def:0;
	ud = unit_bl2ud(bl);
	if (ud && ud->skilltimer != INVALID_TIMER)
		def -= def * skill_get_castdef(ud->skill_id)/100;
	return cap_value(def, CHAR_MIN, CHAR_MAX);
}

unsigned short status_get_speed(struct block_list *bl)
{
	return status_get_status_data(bl)->speed;
}

int status_get_party_id(struct block_list *bl)
{
	nullpo_ret(bl);
	switch (bl->type) {
	case BL_PC:
		return ((TBL_PC*)bl)->status.party_id;
	case BL_PET:
		if (((TBL_PET*)bl)->master)
			return ((TBL_PET*)bl)->master->status.party_id;
		break;
	case BL_MOB:
	{
		struct mob_data *md=(TBL_MOB*)bl;
		if( md->master_id>0 )
		{
			struct map_session_data *msd;
			if (md->special_state.ai && (msd = map_id2sd(md->master_id)) != NULL)
				return msd->status.party_id;
			return -md->master_id;
		}
	}
		break;
	case BL_HOM:
		if (((TBL_HOM*)bl)->master)
			return ((TBL_HOM*)bl)->master->status.party_id;
		break;
	case BL_MER:
		if (((TBL_MER*)bl)->master)
			return ((TBL_MER*)bl)->master->status.party_id;
		break;
	case BL_ELEM:
		if (((TBL_ELEM*)bl)->master)
			return ((TBL_ELEM*)bl)->master->status.party_id;
		break;
	case BL_SKILL:
		if (((TBL_SKILL*)bl)->group)
			return ((TBL_SKILL*)bl)->group->party_id;
		break;
	}
	return 0;
}

int status_get_guild_id(struct block_list *bl)
{
	nullpo_ret(bl);
	switch (bl->type) {
	case BL_PC:
		return ((TBL_PC*)bl)->status.guild_id;
	case BL_PET:
		if (((TBL_PET*)bl)->master)
			return ((TBL_PET*)bl)->master->status.guild_id;
		break;
	case BL_MOB:
	{
		struct map_session_data *msd;
		struct mob_data *md = (struct mob_data *)bl;
		if (md->guardian_data)	//Guardian's guild [Skotlex]
			return md->guardian_data->guild_id;
		if (md->special_state.ai && (msd = map_id2sd(md->master_id)) != NULL)
			return msd->status.guild_id; //Alchemist's mobs [Skotlex]
	}
		break;
	case BL_HOM:
	  	if (((TBL_HOM*)bl)->master)
			return ((TBL_HOM*)bl)->master->status.guild_id;
		break;
	case BL_MER:
		if (((TBL_MER*)bl)->master)
			return ((TBL_MER*)bl)->master->status.guild_id;
		break;
	case BL_ELEM:
		if (((TBL_ELEM*)bl)->master)
			return ((TBL_ELEM*)bl)->master->status.guild_id;
		break;
	case BL_NPC:
	  	if (((TBL_NPC*)bl)->subtype == NPCTYPE_SCRIPT)
			return ((TBL_NPC*)bl)->u.scr.guild_id;
		break;
	case BL_SKILL:
		if (((TBL_SKILL*)bl)->group)
			return ((TBL_SKILL*)bl)->group->guild_id;
		break;
	}
	return 0;
}

int status_get_emblem_id(struct block_list *bl)
{
	nullpo_ret(bl);
	switch (bl->type) {
	case BL_PC:
		return ((TBL_PC*)bl)->guild_emblem_id;
	case BL_PET:
		if (((TBL_PET*)bl)->master)
			return ((TBL_PET*)bl)->master->guild_emblem_id;
		break;
	case BL_MOB:
	{
		struct map_session_data *msd;
		struct mob_data *md = (struct mob_data *)bl;
		if (md->guardian_data)	//Guardian's guild [Skotlex]
			return md->guardian_data->emblem_id;
		if (md->special_state.ai && (msd = map_id2sd(md->master_id)) != NULL)
			return msd->guild_emblem_id; //Alchemist's mobs [Skotlex]
	}
		break;
	case BL_HOM:
	  	if (((TBL_HOM*)bl)->master)
			return ((TBL_HOM*)bl)->master->guild_emblem_id;
		break;
	case BL_MER:
	  	if (((TBL_MER*)bl)->master)
			return ((TBL_MER*)bl)->master->guild_emblem_id;
		break;
	case BL_ELEM:
		if (((TBL_ELEM*)bl)->master)
			return ((TBL_ELEM*)bl)->master->guild_emblem_id;
		break;
	case BL_NPC:
		if (((TBL_NPC*)bl)->subtype == NPCTYPE_SCRIPT && ((TBL_NPC*)bl)->u.scr.guild_id > 0) {
			struct guild *g = guild_search(((TBL_NPC*)bl)->u.scr.guild_id);
			if (g)
				return g->emblem_id;
		}
		break;
	}
	return 0;
}

int status_get_mexp(struct block_list *bl)
{
	nullpo_ret(bl);
	if(bl->type==BL_MOB)
		return ((struct mob_data *)bl)->db->mexp;
	if(bl->type==BL_PET)
		return ((struct pet_data *)bl)->db->mexp;
	return 0;
}
int status_get_race2(struct block_list *bl)
{
	nullpo_ret(bl);
	if(bl->type == BL_MOB)
		return ((struct mob_data *)bl)->db->race2;
	if(bl->type==BL_PET)
		return ((struct pet_data *)bl)->db->race2;
	return 0;
}

int status_isdead(struct block_list *bl)
{
	nullpo_ret(bl);
	return status_get_status_data(bl)->hp == 0;
}

int status_isimmune(struct block_list *bl)
{
	struct status_change *sc =status_get_sc(bl);
	if (sc && sc->data[SC_HERMODE])
		return 100;

	if (bl->type == BL_PC &&
		((TBL_PC*)bl)->special_state.no_magic_damage >= battle_config.gtb_sc_immunity)
		return ((TBL_PC*)bl)->special_state.no_magic_damage;
	return 0;
}

struct view_data* status_get_viewdata(struct block_list *bl) {
	nullpo_retr(NULL, bl);
	switch (bl->type) {
		case BL_PC:  return &((TBL_PC*)bl)->vd;
		case BL_MOB: return ((TBL_MOB*)bl)->vd;
		case BL_PET: return &((TBL_PET*)bl)->vd;
		case BL_NPC: return ((TBL_NPC*)bl)->vd;
		case BL_HOM: return ((TBL_HOM*)bl)->vd;
		case BL_MER: return ((TBL_MER*)bl)->vd;
		case BL_ELEM: return ((TBL_ELEM*)bl)->vd;
	}
	return NULL;
}

void status_set_viewdata(struct block_list *bl, int class_) {
	struct view_data* vd;
	nullpo_retv(bl);
	if (mobdb_checkid(class_) || mob_is_clone(class_))
		vd = mob_get_viewdata(class_);
	else if (npcdb_checkid(class_) || (bl->type == BL_NPC && class_ == WARP_CLASS))
		vd = npc_get_viewdata(class_);
	else if (homdb_checkid(class_))
		vd = hom_get_viewdata(class_);
	else if (mercenary_class(class_))
		vd = mercenary_get_viewdata(class_);
	else if (elem_class(class_))
		vd = elem_get_viewdata(class_);
	else
		vd = NULL;

	switch (bl->type) {
	case BL_PC:
		{
			TBL_PC* sd = (TBL_PC*)bl;
			if (pcdb_checkid(class_)) {
				if (sd->sc.option&OPTION_RIDING)
					switch (class_)
					{	//Adapt class to a Mounted one.
						case JOB_KNIGHT:
							class_ = JOB_KNIGHT2;
							break;
						case JOB_CRUSADER:
							class_ = JOB_CRUSADER2;
							break;
						case JOB_LORD_KNIGHT:
							class_ = JOB_LORD_KNIGHT2;
							break;
						case JOB_PALADIN:
							class_ = JOB_PALADIN2;
							break;
						case JOB_BABY_KNIGHT:
							class_ = JOB_BABY_KNIGHT2;
							break;
						case JOB_BABY_CRUSADER:
							class_ = JOB_BABY_CRUSADER2;
							break;
						case JOB_ROYAL_GUARD:
							class_ = JOB_ROYAL_GUARD2;
							break;
						case JOB_ROYAL_GUARD_T:
							class_ = JOB_ROYAL_GUARD_T2;
							break;
						case JOB_BABY_ROYAL_GUARD:
							class_ = JOB_BABY_ROYAL_GUARD2;
							break;
					}
				else
				if (sd->sc.option&OPTION_DRAGON1)
				switch (class_)
				{
				case JOB_RUNE_KNIGHT:
					class_ = JOB_RUNE_KNIGHT2;
					break;
				case JOB_RUNE_KNIGHT_T:
					class_ = JOB_RUNE_KNIGHT_T2;
					break;
				case JOB_BABY_RUNE_KNIGHT:
					class_ = JOB_BABY_RUNE_KNIGHT2;
					break;
				}
				else
				if (sd->sc.option&OPTION_WUGRIDER)
				switch (class_)
				{
				case JOB_RANGER:
					class_ = JOB_RANGER2;
					break;
				case JOB_RANGER_T:
					class_ = JOB_RANGER_T2;
					break;
				case JOB_BABY_RANGER:
					class_ = JOB_BABY_RANGER2;
					break;
				}
				else
				if (sd->sc.option&OPTION_MADOGEAR)
				switch (class_)
				{
				case JOB_MECHANIC:
					class_ = JOB_MECHANIC2;
					break;
				case JOB_MECHANIC_T:
					class_ = JOB_MECHANIC_T2;
					break;
				case JOB_BABY_MECHANIC:
					class_ = JOB_BABY_MECHANIC2;
					break;
				}
				else
				if (sd->sc.option&OPTION_DRAGON2)
				switch (class_)
				{
				case JOB_RUNE_KNIGHT:
					class_ = JOB_RUNE_KNIGHT3;
					break;
				case JOB_RUNE_KNIGHT_T:
					class_ = JOB_RUNE_KNIGHT_T3;
					break;
				}
				else
				if (sd->sc.option&OPTION_DRAGON3)
				switch (class_)
				{
				case JOB_RUNE_KNIGHT:
					class_ = JOB_RUNE_KNIGHT4;
					break;
				case JOB_RUNE_KNIGHT_T:
					class_ = JOB_RUNE_KNIGHT_T4;
					break;
				}
				else
				if (sd->sc.option&OPTION_DRAGON4)
				switch (class_)
				{
				case JOB_RUNE_KNIGHT:
					class_ = JOB_RUNE_KNIGHT5;
					break;
				case JOB_RUNE_KNIGHT_T:
					class_ = JOB_RUNE_KNIGHT_T5;
					break;
				}
				else
				if (sd->sc.option&OPTION_DRAGON5)
				switch (class_)
				{
				case JOB_RUNE_KNIGHT:
					class_ = JOB_RUNE_KNIGHT6;
					break;
				case JOB_RUNE_KNIGHT_T:
					class_ = JOB_RUNE_KNIGHT_T6;
					break;
				}
				sd->vd.class_ = class_;
				clif_get_weapon_view(sd, &sd->vd.weapon, &sd->vd.shield);
				sd->vd.head_top = sd->status.head_top;
				sd->vd.head_mid = sd->status.head_mid;
				sd->vd.head_bottom = sd->status.head_bottom;
 				sd->vd.hair_style = cap_value(sd->status.hair,0,battle_config.max_hair_style);
 				sd->vd.hair_color = cap_value(sd->status.hair_color,0,battle_config.max_hair_color);
 				sd->vd.cloth_color = cap_value(sd->status.clothes_color,0,battle_config.max_cloth_color);
				sd->vd.body_style = cap_value(sd->status.body,0,battle_config.max_body_style);
				sd->vd.sex = sd->status.sex;
				sd->vd.robe = sd->status.robe;
				sd->vd.body_style = sd->status.body;

				if (sd->vd.cloth_color) {
					if (sd->sc.option&OPTION_WEDDING && battle_config.wedding_ignorepalette)
						sd->vd.cloth_color = 0;
					if (sd->sc.option&OPTION_XMAS && battle_config.xmas_ignorepalette)
						sd->vd.cloth_color = 0;
					if (sd->sc.option&OPTION_SUMMER && battle_config.summer_ignorepalette)
						sd->vd.cloth_color = 0;
					if (sd->sc.option&OPTION_HANBOK && battle_config.hanbok_ignorepalette)
						sd->vd.cloth_color = 0;
					if (sd->sc.option&OPTION_OKTOBERFEST && battle_config.oktoberfest_ignorepalette)
						sd->vd.cloth_color = 0;
					if (sd->sc.option&OPTION_SUMMER2 && battle_config.summer2_ignorepalette)
						sd->vd.cloth_color = 0;
				}
			} else if (vd)
				memcpy(&sd->vd, vd, sizeof(struct view_data));
			else
				ShowError("status_set_viewdata (PC): No view data for class %d\n", class_);
		}
	break;
	case BL_MOB:
		{
			TBL_MOB* md = (TBL_MOB*)bl;
			if (vd)
				md->vd = vd;
			else
				ShowError("status_set_viewdata (MOB): No view data for class %d\n", class_);
		}
	break;
	case BL_PET:
		{
			TBL_PET* pd = (TBL_PET*)bl;
			if (vd) {
				memcpy(&pd->vd, vd, sizeof(struct view_data));
				if (!pcdb_checkid(vd->class_)) {
					pd->vd.hair_style = battle_config.pet_hair_style;
					if(pd->pet.equip) {
						pd->vd.head_bottom = itemdb_viewid(pd->pet.equip);
						if (!pd->vd.head_bottom)
							pd->vd.head_bottom = pd->pet.equip;
					}
				}
			} else
				ShowError("status_set_viewdata (PET): No view data for class %d\n", class_);
		}
	break;
	case BL_NPC:
		{
			TBL_NPC* nd = (TBL_NPC*)bl;
			if (vd)
				nd->vd = vd;
			else {
				ShowError("status_set_viewdata (NPC): No view data for class %d\n", class_);
				if (bl->m >= 0)
					ShowDebug("Source (NPC): %s at %s (%d,%d)\n", nd->name, mapindex_id2name(map_id2index(bl->m)), bl->x, bl->y);
				else
					ShowDebug("Source (NPC): %s (invisible/not on a map)\n", nd->name);
				break;
			}
		}
	break;
	case BL_HOM:		//[blackhole89]
		{
			struct homun_data *hd = (struct homun_data*)bl;
			if (vd)
				hd->vd = vd;
			else
				ShowError("status_set_viewdata (HOMUNCULUS): No view data for class %d\n", class_);
		}
		break;
	case BL_MER:
		{
			struct mercenary_data *md = (struct mercenary_data*)bl;
			if (vd)
				md->vd = vd;
			else
				ShowError("status_set_viewdata (MERCENARY): No view data for class %d\n", class_);
		}
		break;
	case BL_ELEM:
		{
			struct elemental_data *ed = (struct elemental_data*)bl;
			if (vd)
				ed->vd = vd;
			else
				ShowError("status_set_viewdata (ELEMENTAL): No view data for class %d\n", class_);
		}
		break;
	}
}

/// Returns the status_change data of bl or NULL if it doesn't exist.
struct status_change *status_get_sc(struct block_list *bl)
{
	if( bl )
	switch (bl->type) {
	case BL_PC:  return &((TBL_PC*)bl)->sc;
	case BL_MOB: return &((TBL_MOB*)bl)->sc;
	case BL_NPC: return &((TBL_NPC*)bl)->sc;
	case BL_HOM: return &((TBL_HOM*)bl)->sc;
	case BL_MER: return &((TBL_MER*)bl)->sc;
	case BL_ELEM: return &((TBL_ELEM*)bl)->sc;
	}
	return NULL;
}

void status_change_init(struct block_list *bl)
{
	struct status_change *sc = status_get_sc(bl);
	nullpo_retv(sc);
	memset(sc, 0, sizeof (struct status_change));
}

/*========================================== [Playtester]
* Returns the interval for status changes that iterate multiple times
* through the timer (e.g. those that deal damage in regular intervals)
* @param type: Status change (SC_*)
*------------------------------------------*/
static int status_get_sc_interval(enum sc_type type)
{
	switch (type) {
		case SC_POISON:
		case SC_DPOISON:
		case SC_LEECHESEND:
			return 1000;
		case SC_BURNING:
		case SC_PYREXIA:
			return 3000;
		case SC_MAGICMUSHROOM:
			return 4000;
		case SC_STONE:
			return 5000;
		case SC_BLEEDING:
		case SC_TOXIN:
			return 10000;
		default:
			break;
	}
	return 0;
}

//Applies SC defense to a given status change.
//Returns the adjusted duration based on flag values.
//the flag values are the same as in status_change_start.
int64 status_get_sc_def(struct block_list *src, struct block_list *bl, enum sc_type type, int rate, int64 tick, int flag)
{
	bool natural_def = true;
	//Percentual resistance: 10000 = 100% Resist
	//Example: 50% -> sc_def=5000 -> 25%; 5000ms -> tick_def=5000 -> 2500ms
	int sc_def = 0, tick_def = -1; //-1 = use sc_def
	//Linear resistance substracted from rate and tick after percentual resistance was applied
	//Example: 25% -> sc_def2=2000 -> 5%; 2500ms -> tick_def2=2000 -> 500ms
	int sc_def2 = 0, tick_def2 = 0;

	struct status_data *status, *status_src;
	struct status_change *sc;
	struct map_session_data *sd;

	nullpo_ret(bl);
	nullpo_retr(tick ? tick : 1, src); //If no source, it can't be resisted (NPC given)

	if (rate <= 0)
		rate = 0;

	//Status that are blocked by Golden Thief Bug card or Wand of Hermod
	if (status_isimmune(bl))
	switch (type)
	{
	case SC_DECREASEAGI:
	case SC_SILENCE:
	case SC_COMA:
	case SC_INCREASEAGI:
	case SC_BLESSING:
	case SC_SLOWPOISON:
	case SC_IMPOSITIO:
	case SC_AETERNA:
	case SC_SUFFRAGIUM:
	case SC_BENEDICTIO:
	case SC_PROVIDENCE:
	case SC_KYRIE:
	case SC_ASSUMPTIO:
	case SC_ANGELUS:
	case SC_MAGNIFICAT:
	case SC_GLORIA:
	case SC_WINDWALK:
	case SC_MAGICROD:
	case SC_HALLUCINATION:
	case SC_STONE:
	case SC_QUAGMIRE:
	case SC_SUITON:
		return 0;
	}
	
	sd = BL_CAST(BL_PC,bl);
	status = status_get_status_data(bl);
	status_src = status_get_status_data(src);
	sc = status_get_sc(bl);

	switch (type)
	{
	case SC_POISON:
		if (sc && sc->data[SC__UNLUCKY])
			return tick;
	case SC_DPOISON:
		sc_def = status->vit * 100;
		sc_def2 = status->luk * 10 + status_get_lv(bl) * 10 - status_get_lv(src) * 10;
		if (sd) {
			//For players: 60000 - 450*vit - 100*luk
			tick_def = status->vit * 75;
			tick_def2 = status->luk * 100;
		}
		else {
			//For monsters: 30000 - 200*vit
			tick >>= 1;
			tick_def = (status->vit * 200) / 3;
		}
		break;
	case SC_STUN:
		if (sc && sc->data[SC__UNLUCKY])
			return tick;
	case SC_SILENCE:
	case SC_BLEEDING:
		sc_def = status->vit * 100;
		sc_def2 = status->luk * 10 + status_get_lv(bl) * 10 - status_get_lv(src) * 10;
		tick_def2 = status->luk * 10;
		break;
	case SC_SLEEP:
		sc_def = status->int_ * 100;
		sc_def2 = status->luk * 10 + status_get_lv(bl) * 10 - status_get_lv(src) * 10;
		tick_def2 = status->luk * 10;
	case SC_STONE:
		sc_def = status->mdef * 100;
		sc_def2 = status->luk * 10 + status_get_lv(bl) * 10 - status_get_lv(src) * 10;
		tick_def = 0; //No duration reduction
		break;
	case SC_FREEZE:
		sc_def = status->mdef * 100;
		sc_def2 = status->luk * 10 + status_get_lv(bl) * 10 - status_get_lv(src) * 10;
		tick_def2 = status_src->luk*-10; //Caster can increase final duration with luk
		break;
	case SC_CURSE:
		//Special property: immunity when luk is zero
		if (status->luk == 0)
			return 0;
		sc_def = status->luk * 100;
		sc_def2 = status->luk * 10 - status_get_lv(src) * 10; //Curse only has a level penalty and no resistance
		tick_def = status->vit * 100;
		tick_def2 = status->luk * 10;
		break;
	case SC_BLIND:
		sc_def = (status->vit + status->int_) * 50;
		sc_def2 = status->luk * 10 + status_get_lv(bl) * 10 - status_get_lv(src) * 10;
		tick_def2 = status->luk * 10;
		break;
	case SC_CONFUSION:
		sc_def = (status->str + status->int_) * 50;
		sc_def2 = status_get_lv(src) * 10 - status_get_lv(bl) * 10 - status->luk * 10; //Reversed sc_def2
		tick_def2 = status->luk * 10;
		break;
	case SC_DECREASEAGI:
	//case SC_ADORAMUS: //Arch Bishop
		if (sd) tick >>= 1; //Half duration for players.
		sc_def = status->mdef * 100;
		tick_def = 0; //No duration reduction
		break;
	case SC_ANKLE:
		if(status->mode&MD_STATUS_IMMUNE) // Lasts 5 times less on bosses
			tick /= 5;
		sc_def = status->agi * 50;
		break;

	// Fear / Burning / Imprison
	// These status's require 5x the sc_def (500) to gain immunity.
	case SC_FEAR:
		sc_def = (status->int_ + status_get_lv(bl) + status->luk / 2) / 5;
		break;
	case SC_BURNING:
		sc_def = (status->agi + status_get_lv(bl) + status->luk / 2) / 5;
		break;
	case SC_IMPRISON:
		sc_def = (status->str + status_get_lv(bl) + status->luk / 2) / 5;
		break;

	// Deep Sleep / Freezing / Crystalize / Etc.
	// These status's don't have any usual natural defense.
	// Duration is reduceable, but by fixed amounts and not percentage.
	case SC_DEEPSLEEP:
	case SC_FROST:
	case SC_CRYSTALIZE:
	case SC_NORECOVER:
		natural_def = false;
		break;
	default:
		//Effect that cannot be reduced? Likely a buff.
		if (!(rnd()%10000 < rate))
			return 0;
		return tick ? tick : 1;
	}
	
	if ( natural_def == true )
		if (sd)
		{
			if (battle_config.pc_sc_def_rate != 100) {
				sc_def = sc_def * battle_config.pc_sc_def_rate / 100;
				sc_def2 = sc_def2 * battle_config.pc_sc_def_rate / 100;
			}

			sc_def = min(sc_def, battle_config.pc_max_sc_def * 100);
			sc_def2 = min(sc_def2, battle_config.pc_max_sc_def * 100);

			if (tick_def > 0 && battle_config.pc_sc_def_rate != 100) {
				tick_def = tick_def * battle_config.pc_sc_def_rate / 100;
				tick_def2 = tick_def2 * battle_config.pc_sc_def_rate / 100;
			}
		}
		else
		{
			if (battle_config.mob_sc_def_rate != 100) {
				sc_def = sc_def * battle_config.mob_sc_def_rate / 100;
				sc_def2 = sc_def2 * battle_config.mob_sc_def_rate / 100;
			}

			sc_def = min(sc_def, battle_config.mob_max_sc_def * 100);
			sc_def2 = min(sc_def2, battle_config.mob_max_sc_def * 100);

			if (tick_def > 0 && battle_config.mob_sc_def_rate != 100) {
				tick_def = tick_def * battle_config.mob_sc_def_rate / 100;
				tick_def2 = tick_def2 * battle_config.mob_sc_def_rate / 100;
			}
		}
	
	sc = status_get_sc(bl);
	if (sc && sc->count)
	{
		if (sc->data[SC_SCRESIST])
			sc_def += sc->data[SC_SCRESIST]->val1 * 100; //Status resist
		else if (sc->data[SC_SIEGFRIED])
			sc_def += sc->data[SC_SIEGFRIED]->val3 * 100; //Status resistance.
		else if (sc->data[SC_SHIELDSPELL_REF] && sc->data[SC_SHIELDSPELL_REF]->val1 == 1)
			sc_def += sc->data[SC_SHIELDSPELL_REF]->val2 * 100; //Status resistance.
	}

	//When tick def not set, reduction is the same for both.
	if (tick_def == -1)
		tick_def = sc_def;

	//Natural resistance
	if (!(flag & 8) && natural_def == true) {
		rate -= rate * sc_def / 10000;
		rate -= sc_def2;

		//Minimum chances
		switch (type) {
		case SC_WUGBITE:
			rate = max(rate, 5000); //Minimum of 50%
			break;
		}

		//Item resistance (only applies to rate%)
		if(sd && SC_COMMON_MIN <= type && type <= SC_COMMON_MAX)
		{
			if( sd->reseff[type-SC_COMMON_MIN])
				rate -= rate*sd->reseff[type-SC_COMMON_MIN]/10000;
			if (sd->sc.data[SC_COMMONSC_RESIST])
				rate -= rate*sd->sc.data[SC_COMMONSC_RESIST]->val1/100;
		}

		//Aegis accuracy
		if (rate > 0 && rate % 10 != 0) rate += (10 - rate % 10);
	}
	if (!(rnd()%10000 < rate))
		return 0;

	//Rate reduction
 	if (flag&2)
		return max(tick, 1);

	// Status's placed here use special formula's for reducing the duration instead of using tick_def.
	switch (type)
	{
	case SC_DEEPSLEEP:
		tick -= 1000 * (status->int_ / 40 + status_get_lv(bl) / 20);
		break;

	case SC_FROST:
		tick -= 1000 * ((status->vit + status->dex) / 20);
		if (tick < 10000)
			tick = 10000;// Min duration.
		break;

	case SC_CRYSTALIZE:
		tick -= 1000 * status->vit / 10;
		break;

	case SC_NORECOVER:// This is the correct formula but there appears to be a min duration. What is it?
		tick -= 100 * status->luk;
		break;

	default:
		tick -= tick*tick_def/100;
		break;
	}

	if (type == SC_ANKLE)
		if ( sc && sc->count && sc->data[SC_FEAR] && tick <= 2000 )
			tick = 2000;
		else if ( tick < 5000 )// Changed to 5 seconds according to recent tests [Playtester]
			tick = 5000;

	// Fear officially has 2 seconds added to the duration, even if its reduced to 0.
	// Guess this is to work with the "Can't move for 2 seconds." part.
	if (type == SC_FEAR)
		if (tick >= 0)
			tick += 2000;
		else
			tick = 2000;

	// Burning has a minimum duration of 10 seconds.
	if (type == SC_BURNING && tick < 10000)
		tick = 10000;

	return tick<=0?0:tick;
}

/**
* Applies SC effect
* @param bl: Source to apply effect
* @param type: Status change (SC_*)
* @param dval1~3: Depends on type of status change
* Author: Ind
**/
void status_display_add(struct block_list *bl, enum sc_type type, int dval1, int dval2, int dval3) {
	struct eri *eri;
	struct sc_display_entry **sc_display;
	struct sc_display_entry ***sc_display_ptr;
	struct sc_display_entry *entry;
	int i;
	unsigned char sc_display_count;
	unsigned char *sc_display_count_ptr;

	nullpo_retv(bl);
	
	switch (bl->type) {
		case BL_PC: {
			struct map_session_data* sd = (struct map_session_data*)bl;
			sc_display_ptr = &sd->sc_display;
			sc_display_count_ptr = &sd->sc_display_count;
			eri = pc_sc_display_ers;
		}
		break;
		case BL_NPC: {
			struct npc_data* nd = (struct npc_data*)bl;
			sc_display_ptr = &nd->sc_display;
			sc_display_count_ptr = &nd->sc_display_count;
			eri = npc_sc_display_ers;
		}
		break;
		default:
			return;
	}

	sc_display = *sc_display_ptr;
	sc_display_count = *sc_display_count_ptr;
	
	ARR_FIND(0, sc_display_count, i, sc_display[i]->type == type);
	
	if (i != sc_display_count) {
		sc_display[i]->val1 = dval1;
		sc_display[i]->val2 = dval2;
		sc_display[i]->val3 = dval3;
		return;
	}

	entry = ers_alloc(eri, struct sc_display_entry);

	entry->type = type;
	entry->val1 = dval1;
	entry->val2 = dval2;
	entry->val3 = dval3;

	RECREATE(sc_display, struct sc_display_entry *, ++sc_display_count);
	sc_display[sc_display_count - 1] = entry;
	*sc_display_ptr = sc_display;
	*sc_display_count_ptr = sc_display_count;
}

/**
* Removes SC effect
* @param bl: Source to remove effect
* @param type: Status change (SC_*)
* Author: Ind
**/
void status_display_remove(struct block_list *bl, enum sc_type type) {
	struct eri *eri;
	struct sc_display_entry **sc_display;
	struct sc_display_entry ***sc_display_ptr;
	int i;
	unsigned char sc_display_count;
	unsigned char *sc_display_count_ptr;

	nullpo_retv(bl);
	
	switch (bl->type) {
		case BL_PC: {
			struct map_session_data* sd = (struct map_session_data*)bl;
			sc_display_ptr = &sd->sc_display;
			sc_display_count_ptr = &sd->sc_display_count;
			eri = pc_sc_display_ers;
		}
		break;
		case BL_NPC: {
			struct npc_data* nd = (struct npc_data*)bl;
			sc_display_ptr = &nd->sc_display;
			sc_display_count_ptr = &nd->sc_display_count;
			eri = npc_sc_display_ers;
		}
		break;
		default:
			return;
	}

	sc_display = *sc_display_ptr;
	sc_display_count = *sc_display_count_ptr;
	ARR_FIND(0, sc_display_count, i, sc_display[i]->type == type);
	if (i != sc_display_count) {
		int cursor;

		ers_free(eri, sc_display[i]);
		sc_display[i] = NULL;

		/* The all-mighty compact-o-matic */
		for (i = 0, cursor = 0; i < sc_display_count; i++) {
			if (sc_display[i] == NULL)
				continue;

			if (i != cursor)
				sc_display[cursor] = sc_display[i];

			cursor++;
		}

		if (!(sc_display_count = cursor)) {
			aFree(sc_display);
			sc_display = NULL;
		}

		*sc_display_ptr = sc_display;
		*sc_display_count_ptr = sc_display_count;
	}
}

/*==========================================
 * Starts a status change.
 * 'type' = type, 'val1~4' depend on the type.
 * 'rate' = base success rate. 10000 = 100%
 * 'tick' is base duration
 * 'flag':
 *  &1: Cannot be avoided (it has to start)
 *  &2: Tick should not be reduced (by vit, luk, lv, etc)
 *  &4: sc_data loaded, no value has to be altered.
 *  &8: rate should not be reduced
 * &16: don't send SI
 *------------------------------------------*/
int status_change_start(struct block_list* src, struct block_list* bl,enum sc_type type,int rate,int val1,int val2,int val3,int val4,int64 duration,int flag)
{
	struct map_session_data *sd = NULL;
	struct homun_data *hd;
	struct elemental_data *ed;
	struct status_change* sc;
	struct status_change_entry* sce;
	struct status_data *status;
	struct view_data *vd;
	int opt_flag, calc_flag, undead_flag, val_flag = 0, tick_time = 0;

	nullpo_ret(bl);
	sc = status_get_sc(bl);
	status = status_get_status_data(bl);

	if( type <= SC_NONE || type >= SC_MAX )
	{
		ShowError("status_change_start: invalid status change (%d)!\n", type);
		return 0;
	}

	if( !sc )
		return 0; //Unable to receive status changes

	if( sc->data[SC_REFRESH] )
	{// Immune to all common status's as well as Guillotine Cross poisons.
		if( type >= SC_COMMON_MIN && type <= SC_COMMON_MAX || 
			type >= SC_NEW_POISON_MIN && type <= SC_NEW_POISON_MAX )
			return 0;
		switch( type )
		{// Additional status immunity's.
			case SC_MARSHOFABYSS:
			case SC_MANDRAGORA:
				return 0;
		}
	}

	if( sc->data[SC_INSPIRATION] )
	{// Immune to all common status's as well as Guillotine Cross poisons.
		if( type >= SC_COMMON_MIN && type <= SC_COMMON_MAX || 
			type >= SC_NEW_POISON_MIN && type <= SC_NEW_POISON_MAX )
				return 0;

		switch( type )
			{// Additional status immunity's.
			case SC__BODYPAINT:
			case SC__ENERVATION:
			case SC__GROOMY:
			case SC__IGNORANCE:
			case SC__LAZINESS:
			case SC__UNLUCKY:
			case SC__WEAKNESS:
			case SC_SATURDAY_NIGHT_FEVER:
				return 0;
		}
	}
	
	if (bl->type != BL_NPC && status_isdead(bl) && (type != SC_NOCHAT && type != SC_JAILED)) // SC_NOCHAT and SC_JAILED should work even on dead characters
		return 0;

	if (status_change_isDisabledOnMap(type, bl->m))
		return 0;

	//Adjust tick according to status resistances
	if( !(flag&(1|4)) )
	{
		duration = status_get_sc_def(src, bl, type, rate, duration, flag);
		if( !duration)
			return 0;
	}

	int32 tick = (int32)duration;

	sd = BL_CAST(BL_PC, bl);
	hd = BL_CAST(BL_HOM, bl);
	ed = BL_CAST(BL_ELEM, bl);

	undead_flag = battle_check_undead(status->race,status->def_ele);
	//Check for inmunities / sc fails
	switch (type) {
		case SC_STONE:
			if (sc->data[SC_GVG_STONE])
				return 0;
		case SC_FREEZE:
			// Undead are immune to Freeze/Stone
			if (undead_flag && !(flag&SCSTART_NOAVOID))
				return 0;
			if (sc->data[SC_GVG_FREEZ])
				return 0;
			break;
		case SC_SLEEP:
			if (sc->data[SC_GVG_SLEEP])
				return 0;
		case SC_STUN:
			if (sc->data[SC_GVG_STUN])
				return 0;
		case SC_BURNING:
			// Burning can't be given to someone in freezing status.
			if ( type == SC_BURNING && sc->data[SC_FROST] )
				return 0;
		case SC_IMPRISON:
			if (sc->opt1)
				return 0; //Cannot override other opt1 status changes. [Skotlex]
			break;
		case SC_BLIND:// Can't be blinded while fear is active.
			if (sc->data[SC_FEAR])
				return 0;
			break;
		case SC_FROST:
		case SC_CRYSTALIZE:
			if (sc->data[SC_GVG_FREEZ])
				return 0;
			// Burning makes you immune to freezing.
			if ( type == SC_FROST && sc->data[SC_BURNING] || sc->data[SC_WARMER] )
				return 0;
			break;
		case SC_CURSE:
			if (sc->data[SC_GVG_CURSE])
				return 0;
			break;
		case SC_SILENCE:
			if (sc->data[SC_GVG_SILENCE])
				return 0;
			break;
		case SC_SIGNUMCRUCIS:
			//Only affects demons and undead element (but not players)
			if((!undead_flag && status->race!=RC_DEMON) || bl->type == BL_PC)
				return 0;
			break;
		case SC_AETERNA:
			if( (sc->data[SC_STONE] && sc->opt1 == OPT1_STONE) || sc->data[SC_FREEZE] )
				return 0;
			break;
		case SC_KYRIE:
			if (bl->type == BL_MOB)
				return 0;
			break;
		case SC_OVERTHRUST:
			if (sc->data[SC_MAXOVERTHRUST])
				return 0; //Overthrust can't take effect if under Max Overthrust. [Skotlex]
			break;
		case SC_ADRENALINE:
			if (sc->data[SC_QUAGMIRE] ||
				sc->data[SC_DECREASEAGI] ||
				sc->data[SC_ADORAMUS]
			)
				return 0;
			break;
		case SC_ADRENALINE2:
			if (sc->data[SC_QUAGMIRE] ||
				sc->data[SC_DECREASEAGI] ||
				sc->data[SC_ADORAMUS]
			)
				return 0;
			break;
		case SC_ONEHAND:
		case SC_MERC_QUICKEN:
		case SC_TWOHANDQUICKEN:
			if(sc->data[SC_DECREASEAGI] || sc->data[SC_ADORAMUS])
				return 0;
		case SC_INCREASEAGI:
		case SC_SPEARQUICKEN:
		case SC_TRUESIGHT:
		case SC_WINDWALK:
		case SC_CARTBOOST:
		case SC_ASSNCROS:
		case SC_GN_CARTBOOST:
			if (sc->data[SC_QUAGMIRE])
				return 0;
		break;
		case SC_CLOAKING:
			//Avoid cloaking with no wall and low skill level. [Skotlex]
			//Due to the cloaking card, we have to check the wall versus to known
			//skill level rather than the used one. [Skotlex]
			//if (sd && val1 < 3 && skill_check_cloaking(bl,NULL))
			if( sd && pc_checkskill(sd, AS_CLOAKING) < 3 && !skill_check_cloaking(bl,NULL) )
				return 0;
			break;
		case SC_CLOAKINGEXCEED:
		case SC_HIDING:
		case SC_NEWMOON:
			if( sc->data[SC_WUGBITE] )
				return 0; // Prevent Cloaking, Exceed and Hiding
 			break;
		case SC_MODECHANGE:
			{
				int mode;
				struct status_data *bstatus = status_get_base_status(bl);
				if (!bstatus) return 0;
				if (sc->data[type])
				{	//Pile up with previous values.
					if(!val2) val2 = sc->data[type]->val2;
					val3 |= sc->data[type]->val3;
					val4 |= sc->data[type]->val4;
				}
				mode = val2 ? ((val2&~MD_MASK) | val2) : bstatus->mode; // Base mode
				if (val4) mode&=~val4; //Del mode
				if (val3) mode|= val3; //Add mode
				if (mode == bstatus->mode) { //No change.
					if (sc->data[type]) //Abort previous status
						return status_change_end(bl, type, INVALID_TIMER);
					return 0;
				}
			}
			break;
		//Strip skills, need to divest something or it fails.
		case SC_STRIPWEAPON:
			if (sd && !(flag & 4)) { //apply sc anyway if loading saved sc_data
				int i;
				opt_flag = 0; //Reuse to check success condition.
				if(sd->bonus.unstripable_equip&EQP_WEAPON)
					return 0;
				i = sd->equip_index[EQI_HAND_L];
				if (i>=0 && sd->inventory_data[i] &&
					sd->inventory_data[i]->type == IT_WEAPON)
				{
					opt_flag|=1;
					pc_unequipitem(sd,i,3); //L-hand weapon
				}

				i = sd->equip_index[EQI_HAND_R];
				if (i>=0 && sd->inventory_data[i] &&
					sd->inventory_data[i]->type == IT_WEAPON)
				{
					opt_flag|=2;
					pc_unequipitem(sd,i,3);
				}
				if (!opt_flag) return 0;
			}
			if (tick == 1) return 1; //Minimal duration: Only strip without causing the SC
		break;
		case SC_STRIPSHIELD:
			if( val2 == 1 ) val2 = 0; //GX effect. Do not take shield off..		
			else
				if (sd && !(flag & 4)) {
				int i;
				if(sd->bonus.unstripable_equip&EQP_SHIELD)
					return 0;
				i = sd->equip_index[EQI_HAND_L];
				if (i<0 || !sd->inventory_data[i] ||
					sd->inventory_data[i]->type != IT_ARMOR)
					return 0;
				pc_unequipitem(sd,i,3);
			}
			if (tick == 1) return 1; //Minimal duration: Only strip without causing the SC
		break;
		case SC_STRIPARMOR:
			if (sd && !(flag & 4)) {
				int i;
				if(sd->bonus.unstripable_equip&EQP_ARMOR)
					return 0;
				i = sd->equip_index[EQI_ARMOR];
				if (i<0 || !sd->inventory_data[i])
					return 0;
				pc_unequipitem(sd,i,3);
			}
			if (tick == 1) return 1; //Minimal duration: Only strip without causing the SC
		break;
		case SC_STRIPHELM:
			if (sd && !(flag & 4)) {
				int i;
				if(sd->bonus.unstripable_equip&EQP_HELM)
					return 0;
				i = sd->equip_index[EQI_HEAD_TOP];
				if (i<0 || !sd->inventory_data[i])
					return 0;
				pc_unequipitem(sd,i,3);
			}
			if (tick == 1) return 1; //Minimal duration: Only strip without causing the SC
		break;
		case SC_MERC_FLEEUP:
		case SC_MERC_ATKUP:
		case SC_MERC_HPUP:
		case SC_MERC_SPUP:
		case SC_MERC_HITUP:
			if( bl->type != BL_MER )
				return 0; // Stats only for Mercenaries
		break;
		case SC_CAMOUFLAGE:
			if (sd && pc_checkskill(sd, RA_CAMOUFLAGE) < 2 && !skill_check_camouflage(bl, NULL))
				return 0;
			break;
	case SC_TOXIN:
	case SC_PARALYSE:
	case SC_VENOMBLEED:
	case SC_MAGICMUSHROOM:
	case SC_DEATHHURT:
	case SC_PYREXIA:
	case SC_OBLIVIONCURSE://Guillotine Cross poisons do not stack.
	case SC_LEECHESEND://Being inflected with one makes you immune to other poisons until the status wears off. [Rytech]
		if ( sc->data[SC_TOXIN] || sc->data[SC_PARALYSE] || sc->data[SC_VENOMBLEED] || sc->data[SC_MAGICMUSHROOM] || 
			sc->data[SC_DEATHHURT] || sc->data[SC_PYREXIA] || sc->data[SC_OBLIVIONCURSE] || sc->data[SC_LEECHESEND] )
			return 0;
	break;
		case SC_STRFOOD:
			if ((sc->data[SC_FOOD_STR_CASH] && sc->data[SC_FOOD_STR_CASH]->val1 > val1) || sc->data[SC_2011RWC_SCROLL])
				return 0;
		break;
		case SC_AGIFOOD:
			if ((sc->data[SC_FOOD_AGI_CASH] && sc->data[SC_FOOD_AGI_CASH]->val1 > val1) || sc->data[SC_2011RWC_SCROLL])
				return 0;
		break;
		case SC_VITFOOD:
			if ((sc->data[SC_FOOD_VIT_CASH] && sc->data[SC_FOOD_VIT_CASH]->val1 > val1) || sc->data[SC_2011RWC_SCROLL])
				return 0;
		break;
		case SC_INTFOOD:
			if ((sc->data[SC_FOOD_INT_CASH] && sc->data[SC_FOOD_INT_CASH]->val1 > val1) || sc->data[SC_2011RWC_SCROLL])
				return 0;
		break;
		case SC_DEXFOOD:
			if ((sc->data[SC_FOOD_DEX_CASH] && sc->data[SC_FOOD_DEX_CASH]->val1 > val1) || sc->data[SC_2011RWC_SCROLL])
				return 0;
		break;
		case SC_LUKFOOD:
			if ((sc->data[SC_FOOD_LUK_CASH] && sc->data[SC_FOOD_LUK_CASH]->val1 > val1) || sc->data[SC_2011RWC_SCROLL])
				return 0;
		break;
		case SC_ALL_RIDING:
			if ( !sd || pc_isriding(sd) || pc_isdragon(sd) || pc_iswugrider(sd) || pc_ismadogear(sd) )
				return 0;//Do not get on a rental mount if your on a class mount.
			if ( sc->data[type] )
			{
				status_change_end(bl,SC_ALL_RIDING,INVALID_TIMER);
				return 0;
			}
		break;
	
		case SC__STRIPACCESSARY:
			if( sd )
			{
				int i = -1;
				if( !(sd->bonus.unstripable_equip&EQI_ACC_L) )
				{
					i = sd->equip_index[EQI_ACC_L];
					if( i >= 0 && sd->inventory_data[i] && sd->inventory_data[i]->type == IT_ARMOR )
						pc_unequipitem(sd,i,3); //L-Accessory
				}
				if( !(sd->bonus.unstripable_equip&EQI_ACC_R) )
				{
					i = sd->equip_index[EQI_ACC_R];
					if( i >= 0 && sd->inventory_data[i] && sd->inventory_data[i]->type == IT_ARMOR )
						pc_unequipitem(sd,i,3); //R-Accessory
				}
				if( i < 0 )
					return 0;
			}
			if (tick == 1) return 1; //Minimal duration: Only strip without causing the SC
			break;
		case SC_MAGNETICFIELD:
			if(sc->data[SC_HOVERING])
				return 0;
			break;

		case SC_ON_PUSH_CART:
			if (sd)
			{
				int i;

				sd->cart_weight = 0; // Force a client refesh
				sd->cart_num = 0;

				for(i = 0; i < MAX_CART; i++)
				{
					if(sd->cart.u.items_cart[i].nameid==0)
						continue;
					sd->cart_weight+=itemdb_weight(sd->cart.u.items_cart[i].nameid)*sd->cart.u.items_cart[i].amount;
					sd->cart_num++;
				}
			}
			break;
	}

	// Check for resistances
	if (status_has_mode(status, MD_STATUS_IMMUNE) && !(flag&SCSTART_NOAVOID)) {
		if( type >= SC_COMMON_MIN && type <= SC_COMMON_MAX || 
			type >= SC_NEW_POISON_MIN && type <= SC_NEW_POISON_MAX )
			return 0;

		switch( type )
		{
			case SC_BLESSING:
			  if (!undead_flag && status->race!=RC_DEMON)
				  break;
			case SC_DECREASEAGI:
			case SC_PROVOKE:
			case SC_COMA:
			case SC_GRAVITATION:
			case SC_SUITON:
			case SC_STRIPWEAPON:
			case SC_STRIPARMOR:
			case SC_STRIPSHIELD:
			case SC_STRIPHELM:
			case SC_RICHMANKIM:
			case SC_ROKISWEIL:
			case SC_FOGWALL:
			case SC_IMPRISON:
			case SC_FEAR:
			case SC_MARSHOFABYSS:
			case SC_ADORAMUS:
			//case SC_WUGBITE: // Can affect boss monsters but triggers rude attack on hit.
			case SC_ELECTRICSHOCKER:
			case SC_MAGNETICFIELD:
			case SC__MANHOLE:
			//case SC_VACUUM_EXTREME:// Same as SC_WUGBITE with rude attack.
			case SC_CURSEDCIRCLE_TARGET:
			case SC_SILENT_BREEZE:
			case SC_NETHERWORLD:
			case SC_BANDING_DEFENCE:
			case SC_SV_ROOTTWIST:
			case SC_BITESCAR:
			case SC_SP_SHA:
			case SC_CRESCENTELBOW:
			case SC_CATNIPPOWDER:
				return 0;
		}
		if (type == SC__BLOODYLUST_BK && battle_config.allow_bloody_lust_on_boss == 0)
			return 0;
	}

	// Check for mvp resistance // atm only those who OS
	if(status_has_mode(status,MD_MVP) && !(flag&1)) {
		 switch (type) {
		 case SC_COMA:
		// continue list...
		     return 0;
		}
	}

	//Before overlapping fail, one must check for status cured.
	switch (type)
	{
	case SC_ENDURE:
		if (val4)
			status_change_end(bl, SC_CONCENTRATION, INVALID_TIMER);
		break;
	case SC_BLESSING:
		//TO-DO Blessing and Agi up should do 1 damage against players on Undead Status, even on PvM
		//but cannot be plagiarized (this requires aegis investigation on packets and official behavior) [Brainstorm]
		if ((!undead_flag && status->race!=RC_DEMON) || bl->type == BL_PC) {
			if (sc->data[SC_STONE] && sc->opt1 == OPT1_STONE)
				status_change_end(bl, SC_STONE, INVALID_TIMER);
			if (sc->data[SC_CURSE]) {
				status_change_end(bl, SC_CURSE, INVALID_TIMER);
				return 1; // End Curse and do not give stat boost
			}
		}
		if (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
			status_change_end(bl, SC_SPIRIT, INVALID_TIMER);
		break;
	case SC_INCREASEAGI:
		status_change_end(bl, SC_DECREASEAGI, INVALID_TIMER);
		status_change_end(bl, SC_ADORAMUS, INVALID_TIMER);
		if (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_HIGH)
			status_change_end(bl, SC_SPIRIT, INVALID_TIMER);
		break;
	case SC_QUAGMIRE:
		status_change_end(bl, SC_CONCENTRATE, INVALID_TIMER);
		status_change_end(bl, SC_TRUESIGHT, INVALID_TIMER);
		status_change_end(bl, SC_WINDWALK, INVALID_TIMER);
		//Also blocks the ones below...
	case SC_ADORAMUS:
		status_change_end(bl, SC_DECREASEAGI, INVALID_TIMER);
	case SC_DECREASEAGI:
		status_change_end(bl, SC_CARTBOOST, INVALID_TIMER);
		status_change_end(bl, SC_GN_CARTBOOST, INVALID_TIMER);
		//Also blocks the ones below...
	case SC_DONTFORGETME:
		status_change_end(bl, SC_INCREASEAGI, INVALID_TIMER);
		status_change_end(bl, SC_ADRENALINE, INVALID_TIMER);
		status_change_end(bl, SC_ADRENALINE2, INVALID_TIMER);
		status_change_end(bl, SC_SPEARQUICKEN, INVALID_TIMER);
		status_change_end(bl, SC_TWOHANDQUICKEN, INVALID_TIMER);
		status_change_end(bl, SC_ONEHAND, INVALID_TIMER);
		status_change_end(bl, SC_MERC_QUICKEN, INVALID_TIMER);
		status_change_end(bl, SC_ACCELERATION, INVALID_TIMER);
		break;
	case SC_ONEHAND:
	  	//Removes the Aspd potion effect, as reported by Vicious. [Skotlex]
		status_change_end(bl, SC_ASPDPOTION0, INVALID_TIMER);
		status_change_end(bl, SC_ASPDPOTION1, INVALID_TIMER);
		status_change_end(bl, SC_ASPDPOTION2, INVALID_TIMER);
		status_change_end(bl, SC_ASPDPOTION3, INVALID_TIMER);
		break;
	case SC_MAXOVERTHRUST:
	  	//Cancels Normal Overthrust. [Skotlex]
		status_change_end(bl, SC_OVERTHRUST, INVALID_TIMER);
		break;
	case SC_KYRIE:
		//Cancels Assumptio
		status_change_end(bl, SC_ASSUMPTIO, INVALID_TIMER);
		break;
	case SC_DELUGE:
		if (sc->data[SC_FOGWALL] && sc->data[SC_BLIND])
			status_change_end(bl, SC_BLIND, INVALID_TIMER);
		break;
	case SC_SILENCE:
		if (sc->data[SC_GOSPEL] && sc->data[SC_GOSPEL]->val4 == BCT_SELF)
			status_change_end(bl, SC_GOSPEL, INVALID_TIMER);
		break;
	case SC_HIDING:
		status_change_end(bl, SC_CLOSECONFINE, INVALID_TIMER);
		status_change_end(bl, SC_CLOSECONFINE2, INVALID_TIMER);
		break;
	case SC_FREEZE:
		status_change_end(bl, SC_AETERNA, INVALID_TIMER);
		break;
	case SC_BERSERK:
		if(battle_config.berserk_cancels_buffs)
		{
			status_change_end(bl, SC_ONEHAND, INVALID_TIMER);
			status_change_end(bl, SC_TWOHANDQUICKEN, INVALID_TIMER);
			status_change_end(bl, SC_CONCENTRATION, INVALID_TIMER);
			status_change_end(bl, SC_PARRYING, INVALID_TIMER);
			status_change_end(bl, SC_AURABLADE, INVALID_TIMER);
			status_change_end(bl, SC_MERC_QUICKEN, INVALID_TIMER);
		}
		break;
	case SC_ASSUMPTIO:
		status_change_end(bl, SC_KYRIE, INVALID_TIMER);
		status_change_end(bl, SC_KAITE, INVALID_TIMER);
		break;
	case SC_KAITE:
		status_change_end(bl, SC_ASSUMPTIO, INVALID_TIMER);
		break;
	case SC_CARTBOOST:
	case SC_GN_CARTBOOST:
		if(sc->data[SC_DECREASEAGI] || sc->data[SC_ADORAMUS])
		{	//Cancel Decrease Agi, but take no further effect [Skotlex]
			status_change_end(bl, SC_DECREASEAGI, INVALID_TIMER);
			status_change_end(bl, SC_ADORAMUS, INVALID_TIMER);
			return 0;
		}
		break;
	case SC_FUSION:
		status_change_end(bl, SC_SPIRIT, INVALID_TIMER);
		break;
	case SC_ADJUSTMENT:
		status_change_end(bl, SC_MADNESSCANCEL, INVALID_TIMER);
		break;
	case SC_MADNESSCANCEL:
		status_change_end(bl, SC_ADJUSTMENT, INVALID_TIMER);
		break;
	case SC_P_ALTER:
	case SC_HEAT_BARREL:
		if (sc->data[type])
			break;
		status_change_end(bl, SC_MADNESSCANCEL, INVALID_TIMER);
		status_change_end(bl, SC_P_ALTER, INVALID_TIMER);
		status_change_end(bl, SC_HEAT_BARREL, INVALID_TIMER);
		break;
	//NPC_CHANGEUNDEAD will debuff Blessing and Agi Up
	case SC_CHANGEUNDEAD:
		status_change_end(bl, SC_BLESSING, INVALID_TIMER);
		status_change_end(bl, SC_INCREASEAGI, INVALID_TIMER);
		break;
	case SC_STRFOOD:
		status_change_end(bl, SC_STRFOOD, INVALID_TIMER);
		break;
	case SC_AGIFOOD:
		status_change_end(bl, SC_AGIFOOD, INVALID_TIMER);
		break;
	case SC_VITFOOD:
		status_change_end(bl, SC_VITFOOD, INVALID_TIMER);
		break;
	case SC_INTFOOD:
		status_change_end(bl, SC_INTFOOD, INVALID_TIMER);
		break;
	case SC_DEXFOOD:
		status_change_end(bl, SC_DEXFOOD, INVALID_TIMER);
		break;
	case SC_LUKFOOD:
		status_change_end(bl, SC_LUKFOOD, INVALID_TIMER);
		break;
	case SC_FOOD_STR_CASH:
		status_change_end(bl, SC_STRFOOD, INVALID_TIMER);
		status_change_end(bl, SC_FOOD_STR_CASH, INVALID_TIMER);
		break;
	case SC_FOOD_AGI_CASH:
		status_change_end(bl, SC_AGIFOOD, INVALID_TIMER);
		status_change_end(bl, SC_FOOD_AGI_CASH, INVALID_TIMER);
		break;
	case SC_FOOD_VIT_CASH:
		status_change_end(bl, SC_VITFOOD, INVALID_TIMER);
		status_change_end(bl, SC_FOOD_VIT_CASH, INVALID_TIMER);
		break;
	case SC_FOOD_INT_CASH:
		status_change_end(bl, SC_INTFOOD, INVALID_TIMER);
		status_change_end(bl, SC_FOOD_INT_CASH, INVALID_TIMER);
		break;
	case SC_FOOD_DEX_CASH:
		status_change_end(bl, SC_DEXFOOD, INVALID_TIMER);
		status_change_end(bl, SC_FOOD_DEX_CASH, INVALID_TIMER);
		break;
	case SC_FOOD_LUK_CASH:
		status_change_end(bl, SC_LUKFOOD, INVALID_TIMER);
		status_change_end(bl, SC_FOOD_LUK_CASH, INVALID_TIMER);
		break;
	case SC_FEAR:
		status_change_end(bl, SC_BLIND, INVALID_TIMER);
		break;
	//Group A Status
	case SC_SWING:
	case SC_SYMPHONY_LOVE:
	case SC_MOONLIT_SERENADE:
	case SC_RUSH_WINDMILL:
	case SC_ECHOSONG:
	case SC_HARMONIZE:
		if (sc->data[type]) // Don't remove same sc.
			break;
		status_change_end(bl, SC_SWING, INVALID_TIMER);
		status_change_end(bl, SC_SYMPHONY_LOVE, INVALID_TIMER);
		status_change_end(bl, SC_MOONLIT_SERENADE, INVALID_TIMER);
		status_change_end(bl, SC_RUSH_WINDMILL, INVALID_TIMER);
		status_change_end(bl, SC_ECHOSONG, INVALID_TIMER);
		status_change_end(bl, SC_HARMONIZE, INVALID_TIMER);
	//Group B Status
	case SC_SIREN:
	case SC_DEEPSLEEP:
	case SC_SIRCLEOFNATURE:
	case SC_GLOOMYDAY:
	case SC_SONG_OF_MANA:
	case SC_DANCE_WITH_WUG:
	case SC_LERADS_DEW:
	case SC_MELODYOFSINK:
	case SC_BEYOND_OF_WARCRY:
	case SC_UNLIMITED_HUMMING_VOICE:
		if( sc->data[type] )// Don't remove same sc.
			break;
		status_change_end(bl, SC_SIREN, INVALID_TIMER);
		status_change_end(bl, SC_DEEPSLEEP, INVALID_TIMER);
		status_change_end(bl, SC_SIRCLEOFNATURE, INVALID_TIMER);
		status_change_end(bl, SC_GLOOMYDAY, INVALID_TIMER);
		status_change_end(bl, SC_SONG_OF_MANA, INVALID_TIMER);
		status_change_end(bl, SC_DANCE_WITH_WUG, INVALID_TIMER);
		status_change_end(bl, SC_LERADS_DEW, INVALID_TIMER);
		status_change_end(bl, SC_MELODYOFSINK, INVALID_TIMER);
		status_change_end(bl, SC_BEYOND_OF_WARCRY, INVALID_TIMER);
		status_change_end(bl, SC_UNLIMITED_HUMMING_VOICE, INVALID_TIMER);
		break;
	case SC_REFLECTSHIELD:
		status_change_end(bl,SC_LG_REFLECTDAMAGE,INVALID_TIMER);
		break;
	case SC_LG_REFLECTDAMAGE:
		status_change_end(bl,SC_REFLECTSHIELD,INVALID_TIMER);
		break;
	// Remove previous status changes.
	case SC_SHIELDSPELL_DEF:
	case SC_SHIELDSPELL_MDEF:
	case SC_SHIELDSPELL_REF:
		status_change_end(bl,SC_MAGNIFICAT,INVALID_TIMER);
		status_change_end(bl,SC_SHIELDSPELL_DEF,INVALID_TIMER);
		status_change_end(bl,SC_SHIELDSPELL_MDEF,INVALID_TIMER);
		status_change_end(bl,SC_SHIELDSPELL_REF,INVALID_TIMER);
 		break;
	case SC_BANDING:
		status_change_end(bl, SC_PRESTIGE, INVALID_TIMER);
		break;
	case SC_GENTLETOUCH_ENERGYGAIN:
	case SC_GENTLETOUCH_CHANGE:
	case SC_GENTLETOUCH_REVITALIZE:
		if (sc->data[type]) // Don't remove same sc.
			break;
		status_change_end(bl, SC_GENTLETOUCH_ENERGYGAIN, INVALID_TIMER);
		status_change_end(bl, SC_GENTLETOUCH_CHANGE, INVALID_TIMER);
		status_change_end(bl, SC_GENTLETOUCH_REVITALIZE, INVALID_TIMER);
		break;
	case SC_SUNSTANCE:
	case SC_LUNARSTANCE:
	case SC_STARSTANCE:
	case SC_UNIVERSESTANCE:
		if ( sc->data[type] )
			break;
		status_change_end(bl, SC_SUNSTANCE, INVALID_TIMER);
		status_change_end(bl, SC_LUNARSTANCE, INVALID_TIMER);
		status_change_end(bl, SC_STARSTANCE, INVALID_TIMER);
		status_change_end(bl, SC_UNIVERSESTANCE, INVALID_TIMER);
		break;
	case SC_SPIRIT:
	case SC_SOULGOLEM:
	case SC_SOULSHADOW:
	case SC_SOULFALCON:
	case SC_SOULFAIRY:
		if ( sc->data[type] )
			break;
		status_change_end(bl, SC_SPIRIT, INVALID_TIMER);
		status_change_end(bl, SC_SOULGOLEM, INVALID_TIMER);
		status_change_end(bl, SC_SOULSHADOW, INVALID_TIMER);
		status_change_end(bl, SC_SOULFALCON, INVALID_TIMER);
		status_change_end(bl, SC_SOULFAIRY, INVALID_TIMER);
		break;
	case SC_OFFERTORIUM:
	case SC_MAGNIFICAT:
		if ( sc->data[type] )
			break;
		status_change_end(bl, SC_OFFERTORIUM, INVALID_TIMER);
		status_change_end(bl, SC_MAGNIFICAT, INVALID_TIMER);
		break;
	case SC_SONIC_CLAW_POSTDELAY:
	case SC_SILVERVEIN_RUSH_POSTDELAY:
	case SC_MIDNIGHT_FRENZY_POSTDELAY:
	case SC_TINDER_BREAKER_POSTDELAY:
	case SC_CBC_POSTDELAY:
		if( sc->data[type] )// Don't remove status that was just activated.
			break;
		status_change_end(bl, SC_SONIC_CLAW_POSTDELAY, INVALID_TIMER);
		status_change_end(bl, SC_SILVERVEIN_RUSH_POSTDELAY, INVALID_TIMER);
		status_change_end(bl, SC_MIDNIGHT_FRENZY_POSTDELAY, INVALID_TIMER);
		status_change_end(bl, SC_TINDER_BREAKER_POSTDELAY, INVALID_TIMER);
		status_change_end(bl, SC_CBC_POSTDELAY, INVALID_TIMER);
		break;
	case SC_GOLDENE_FERSE:
	case SC_ANGRIFFS_MODUS:
		if ( sc->data[type] )
			break;
		status_change_end(bl, SC_GOLDENE_FERSE, INVALID_TIMER);
		status_change_end(bl, SC_ANGRIFFS_MODUS, INVALID_TIMER);
		break;
	case SC_2011RWC_SCROLL:
		status_change_end(bl, SC_FOOD_STR_CASH, INVALID_TIMER);
		status_change_end(bl, SC_FOOD_AGI_CASH, INVALID_TIMER);
		status_change_end(bl, SC_FOOD_VIT_CASH, INVALID_TIMER);
		status_change_end(bl, SC_FOOD_INT_CASH, INVALID_TIMER);
		status_change_end(bl, SC_FOOD_DEX_CASH, INVALID_TIMER);
		status_change_end(bl, SC_FOOD_LUK_CASH, INVALID_TIMER);
		break;
	case SC_FIGHTINGSPIRIT:
	case SC_IMPOSITIO:
	case SC_KAAHI:
		//These status changes always overwrite themselves even when a lower level is cast
		status_change_end(bl, type, INVALID_TIMER);
		break;
	case SC_INVINCIBLE:
		status_change_end(bl, SC_INVINCIBLEOFF, INVALID_TIMER);
		break;
	case SC_INVINCIBLEOFF:
		status_change_end(bl, SC_INVINCIBLE, INVALID_TIMER);
		break;
	}

	//Check for overlapping fails
	if( (sce = sc->data[type]) )
	{
		switch( type )
		{
			case SC_MERC_FLEEUP:
			case SC_MERC_ATKUP:
			case SC_MERC_HPUP:
			case SC_MERC_SPUP:
			case SC_MERC_HITUP:
				if( sce->val1 > val1 )
					val1 = sce->val1;
				break;
			case SC_ADRENALINE:
			case SC_ADRENALINE2:
			case SC_WEAPONPERFECTION:
			case SC_OVERTHRUST:
				if (sce->val2 > val2)
					return 0;
				break;
			case SC_S_LIFEPOTION:
			case SC_L_LIFEPOTION:
			case SC_BOSSMAPINFO:
			case SC_STUN:
				if (sc->data[SC_DANCING])
					unit_stop_walking(bl, 1);
			case SC_SLEEP:
			case SC_POISON:
			case SC_CURSE:
			case SC_SILENCE:
			case SC_CONFUSION:
			case SC_BLIND:
			case SC_BLEEDING:
			case SC_DPOISON:
			case SC_FEAR:
			case SC_BURNING:
			case SC_IMPRISON:// Can't recast imprison on enemy with this status, but here to be safe.
			case SC_DEEPSLEEP:
			case SC_FROST:
			case SC_CRYSTALIZE:
			case SC_CLOSECONFINE2: //Can't be re-closed in.
			case SC_MARIONETTE:
			case SC_MARIONETTE2:
			case SC_NOCHAT:
			case SC_CHANGE: //Otherwise your Hp/Sp would get refilled while still within effect of the last invocation.
			case SC_ABUNDANCE:
			case SC_TOXIN:
			case SC_PARALYSE:
			case SC_VENOMBLEED:
			case SC_MAGICMUSHROOM:
			case SC_DEATHHURT:
			case SC_PYREXIA:
			case SC_OBLIVIONCURSE:
			case SC_LEECHESEND:
			case SC_WUGBITE:
			case SC_VACUUM_EXTREME:
			case SC_BANDING_DEFENCE:
			//case SC__INVISIBILITY:
			case SC__ENERVATION:
			case SC__GROOMY:
			case SC__IGNORANCE:
			case SC__LAZINESS:
			case SC__WEAKNESS:
			case SC__UNLUCKY:
			case SC__MANHOLE:
			case SC_CONFUSIONPANIC:
			case SC_B_TRAP:
			case SC_SV_ROOTTWIST:
			case SC_REUSE_REFRESH:
			case SC_REUSE_LIMIT_A:
			case SC_REUSE_LIMIT_B:
			case SC_REUSE_LIMIT_C:
			case SC_REUSE_LIMIT_D:
			case SC_REUSE_LIMIT_E:
			case SC_REUSE_LIMIT_F:
			case SC_REUSE_LIMIT_G:
			case SC_REUSE_LIMIT_H:
			case SC_REUSE_LIMIT_MTF:
			case SC_REUSE_LIMIT_ECL:
			case SC_REUSE_LIMIT_RECALL:
			case SC_REUSE_LIMIT_ASPD_POTION:
			case SC_REUSE_MILLENNIUMSHIELD:
			case SC_REUSE_CRUSHSTRIKE:
			case SC_REUSE_STORMBLAST:
			case SC_ALL_RIDING_REUSE_LIMIT:
			case SC_DORAM_BUF_01:
			case SC_DORAM_BUF_02:
				return 0;
			case SC_COMBO: 
			case SC_DANCING:
			case SC_DEVOTION:
			case SC_ASPDPOTION0:
			case SC_ASPDPOTION1:
			case SC_ASPDPOTION2:
			case SC_ASPDPOTION3:
			case SC_ATKPOTION:
			case SC_MATKPOTION:
			case SC_ENCHANTARMS:
			case SC_ARMOR_ELEMENT_WATER:
			case SC_ARMOR_ELEMENT_EARTH:
			case SC_ARMOR_ELEMENT_FIRE:
			case SC_ARMOR_ELEMENT_WIND:
			case SC_ARMOR_RESIST:
			case SC_CURSEDCIRCLE_TARGET:
			case SC_BLOOD_SUCKER:
			case SC_C_MARKER:
			case SC_H_MINE:
			case SC_FLASHKICK:
			case SC_SOULUNITY:
			case SC_ATTHASTE_CASH:
				break;
			case SC_GOSPEL:
				 //Must not override a casting gospel char.
				if(sce->val4 == BCT_SELF)
					return 0;
				if(sce->val1 > val1)
					return 1;
				break;
			case SC_ENDURE:
				if(sce->val4 && !val4)
					return 1; //Don't let you override infinite endure.
				if(sce->val1 > val1)
					return 1;
				break;
			case SC_JAILED:
				//When a player is already jailed, do not edit the jail data.
				val2 = sce->val2;
				val3 = sce->val3;
				val4 = sce->val4;
				break;
			case SC_JOINTBEAT:
				val2 |= sce->val2; // stackable ailments
			case SC_LERADS_DEW:
				if(sc && sc->data[SC_BERSERK])
					return 0;
			case SC_SHAPESHIFT:
			case SC_PROPERTYWALK:
				break;
			case SC_LEADERSHIP:
			case SC_GLORYWOUNDS:
			case SC_SOULCOLD:
			case SC_HAWKEYES:
				if (sce->val4 && !val4)//you cannot override master guild aura
					return 0;
				break;
			default:
				if(sce->val1 > val1)
					return 1; //Return true to not mess up skill animations. [Skotlex]
		}
	}

	vd = status_get_viewdata(bl);
	calc_flag = StatusChangeFlagTable[type];
	if(!(flag&4)) //&4 - Do not parse val settings when loading SCs
	switch(type)
	{
		case SC_DECREASEAGI:
		case SC_INCREASEAGI:
			val2 = 2 + val1; //Agi change
			break;
		case SC_ENDURE:
			val2 = 7; // Hit-count [Celest]
			if( !(flag&1) && (bl->type&(BL_PC|BL_MER)) && !map_flag_gvg2(bl->m) && !map[bl->m].flag.battleground && !val4 )
			{
				struct map_session_data *tsd;
				if( sd )
				{
					int i;
					for( i = 0; i < 5; i++ )
					{
						if( sd->devotion[i] && (tsd = map_id2sd(sd->devotion[i])) )
							status_change_start(src, &tsd->bl, type, 10000, val1, val2, val3, val4, tick, 1);
					}
				}
				else if( bl->type == BL_MER && ((TBL_MER*)bl)->devotion_flag && (tsd = ((TBL_MER*)bl)->master) )
					status_change_start(src, &tsd->bl, type, 10000, val1, val2, val3, val4, tick, 1);
			}
			//val4 signals infinite endure (if val4 == 2 it is infinite endure from Berserk)
			if( val4 )
				tick = -1;
			break;
		case SC_AUTOBERSERK:
			if (status->hp < status->max_hp>>2 &&
				(!sc->data[SC_PROVOKE] || sc->data[SC_PROVOKE]->val2==0))
					sc_start4(bl,SC_PROVOKE,100,10,1,0,0,60000);
			tick = -1;
			break;
		case SC_SIGNUMCRUCIS:
			val2 = 10 + 4*val1; //Def reduction
			tick = -1;
			clif_emotion(bl,E_SWT);
			break;
		case SC_MAXIMIZEPOWER:
			tick_time = val2 = tick > 0 ? tick : 60000;
			tick = -1; // duration sent to the client should be infinite
			break;
		case SC_EDP:	// [Celest]
			val2 = val1 + 2; //Chance to Poison enemies.
			val3 = 50*(val1+1); //Damage increase (+50 +50*lv%)
			break;
		case SC_POISONREACT:
			val2=(val1+1)/2 + val1/10; // Number of counters [Skotlex]
			val3=50; // + 5*val1; //Chance to counter. [Skotlex]
			break;
		case SC_MAGICROD:
			val2 = val1*20; //SP gained
			break;
		case SC_KYRIE:
			if ( val4 == RL_P_ALTER )
			{// Platinum Alter's Formula
				val2 = (int64)status->max_hp * (val1 * 5) / 100; //%Max HP to absorb
				val3 = 3 + val1; //Hits
			}
			else if ( val4 == AB_PRAEFATIO )
			{// Praefatio's Formula
				val2 = (int64)status->max_hp * (val1 * 2 + 16) / 100; //%Max HP to absorb
				val3 = 6 + val1; //Hits
			} else
			{// Kyrie Eleison's Formula
				val2 = (int64)status->max_hp * (val1 * 2 + 10) / 100; //%Max HP to absorb
				val3 = (val1 / 2 + 5); //Hits
			}
			break;
		case SC_MAGICPOWER:
			//val1: Skill lv
			val2 = 1; //Lasts 1 invocation
			val3 = 5*val1; //Matk% increase
			val4 = 0; // 0 = ready to be used, 1 = activated and running
			break;
		case SC_SACRIFICE:
			val2 = 5; //Lasts 5 hits
			tick = -1;
			break;
		case SC_ENCPOISON:
			val2= 250+50*val1;	//Poisoning Chance (2.5+0.5%) in 1/10000 rate
		case SC_ASPERSIO:
		case SC_FIREWEAPON:
		case SC_WATERWEAPON:
		case SC_WINDWEAPON:
		case SC_EARTHWEAPON:
		case SC_SHADOWWEAPON:
		case SC_GHOSTWEAPON:
			skill_enchant_elemental_end(bl,type);
			break;
		case SC_ELEMENTALCHANGE:
			// val1 : Element Lvl (if called by skill lvl 1, takes random value between 1 and 4)
			// val2 : Element (When no element, random one is picked)
			// val3 : 0 = called by skill 1 = called by script (fixed level)
			if( !val2 ) val2 = rnd()% ELE_ALL;

			if( val1 == 1 && val3 == 0 )
				val1 = 1 + rnd()%4;
			else if( val1 > 4 )
				val1 = 4; // Max Level
			val3 = 0; // Not need to keep this info.
			break;
		case SC_PROVIDENCE:
			val2=val1*5; //Race/Ele resist
			break;
		case SC_REFLECTSHIELD:
			val2=10+val1*3; // %Dmg reflected
			if( !(flag&1) && (bl->type&(BL_PC|BL_MER)) )
			{
				struct map_session_data *tsd;
				if( sd )
				{
					int i;
					for( i = 0; i < 5; i++ )
					{
						if( sd->devotion[i] && (tsd = map_id2sd(sd->devotion[i])) )
							status_change_start(src, &tsd->bl, type, 10000, val1, val2, 0, 0, tick, 1);
					}
				}
				else if( bl->type == BL_MER && ((TBL_MER*)bl)->devotion_flag && (tsd = ((TBL_MER*)bl)->master) )
					status_change_start(src, &tsd->bl, type, 10000, val1, val2, 0, 0, tick, 1);
			}
			break;
		case SC_STRIPWEAPON:
			if (!sd) //Watk reduction
				val2 = 25;
			break;
		case SC_STRIPSHIELD:
			if (!sd) //Def reduction
				val2 = 15;
			break;
		case SC_STRIPARMOR:
			if (!sd) //Vit reduction
				val2 = 40;
			break;
		case SC_STRIPHELM:
			if (!sd) //Int reduction
				val2 = 40;
			break;
		case SC_AUTOSPELL:
			//Val1 Skill LV of Autospell
			//Val2 Skill ID to cast
			//Val3 Max Lv to cast
			val4 = 5 + val1*2; //Chance of casting
			break;
		case SC_VOLCANO:
			{
				int8 enchant_eff[] = { 10, 14, 17, 19, 20 }; // Enchant addition
				uint8 i = max((val1 - 1) % 5, 0);

				val2 = val1 * 10; // Watk increase
				if (status->def_ele != ELE_FIRE)
					val2 = 0;
				val3 = enchant_eff[i];
			}
			break;
		case SC_VIOLENTGALE:
			{
				int8 enchant_eff[] = { 10, 14, 17, 19, 20 }; // Enchant addition
				uint8 i = max((val1 - 1) % 5, 0);

				val2 = val1 * 3; // Flee increase
				if (status->def_ele != ELE_WIND)
					val2 = 0;
				val3 = enchant_eff[i];
			}
			break;
		case SC_DELUGE:
			{
				int8 deluge_eff[] = { 5,  9, 12, 14, 15 }; // HP addition rate n/100
				int8 enchant_eff[] = { 10, 14, 17, 19, 20 }; // Enchant addition
				uint8 i = max((val1 - 1) % 5, 0);

				val2 = deluge_eff[i]; // HP increase
				if (status->def_ele != ELE_WATER)
					val2 = 0;
				val3 = enchant_eff[i];
			}
			break;
		case SC_SUITON:
			if (!val2 || (sd && (sd->class_&MAPID_BASEMASK) == MAPID_NINJA)) {
				//No penalties.
				val2 = 0; //Agi penalty
				val3 = 0; //Walk speed penalty
				break;
			}
			val3 = 50;
			val2 = 3*((val1+1)/3);
			if (val1 > 4) val2--;
			//Suiton is a special case, stop effect is forced and only happens when target enters it
			if (!unit_blown_immune(bl, 0x1))
				unit_stop_walking(bl, 9);
			break;
		case SC_ONEHAND:
		case SC_TWOHANDQUICKEN:
			val2 = 300;
			if (val1 > 10) //For boss casted skills [Skotlex]
				val2 += 20*(val1-10);
			break;
		case SC_MERC_QUICKEN:
			val2 = 300;
			break;

		case SC_SPEARQUICKEN:
			val2 = 200+10*val1;
			break;
		case SC_DANCING:
			//val1 : Skill ID + LV
			//val2 : Skill Group of the Dance.
			//val3 : Brings the skill_lv (merged into val1 here)
			//val4 : Partner
			if (val1 == CG_MOONLIT)
				clif_status_change(bl,SI_MOONLIT,1,tick, 0, 0, 0);
			val1|= (val3<<16);
			val3 = tick/1000; //Tick duration
			tick_time = 1000; // [GodLesZ] tick time
			break;
		case SC_LONGING:
			val2 = 500-100*val1; //Aspd penalty.
			break;
		case SC_EXPLOSIONSPIRITS:
			val2 = 75 + 25*val1; //Cri bonus
			break;
		case SC_ASPDPOTION0:
		case SC_ASPDPOTION1:
		case SC_ASPDPOTION2:
		case SC_ASPDPOTION3:
			val2 = 50*(2+type-SC_ASPDPOTION0);
			break;
		case SC_WEDDING:
		case SC_XMAS:
		case SC_SUMMER:
		case SC_HANBOK:
		case SC_OKTOBERFEST:
		case SC_SUMMER2:
			if (!vd) return 0;
			//Store previous values as they could be removed.
			unit_stop_attack(bl);
			break;
		case SC_ATTHASTE_CASH:
			val2 = 50 * val1; // Just custom for pre-re
			break;
		case SC_NOCHAT:
			tick = 60000;
			val1 = battle_config.manner_system; //Mute filters.
			if (sd)
			{
				clif_changestatus(sd, SP_MANNER, sd->status.manner);
				clif_updatestatus(sd,SP_MANNER);
			}
			break;

		case SC_STONE:
			val3 = max(val3, 100); // Incubation time
			val4 = max(tick - val3, 100); // Petrify time
			tick = val3;
			calc_flag = 0; //Actual status changes take effect on petrified state.
			break;

		case SC_DPOISON:
		//Lose 10/15% of your life as long as it doesn't brings life below 25%
		if (status->hp > status->max_hp>>2)
		{
			int diff = status->max_hp*(bl->type==BL_PC?10:15)/100;
			if (status->hp - diff < status->max_hp>>2)
				diff = status->hp - (status->max_hp>>2);
			status_zap(bl, diff, 0);
		}
		// fall through
		case SC_POISON:
		case SC_BLEEDING:
		case SC_BURNING:
		case SC_TOXIN:
		case SC_MAGICMUSHROOM:
		case SC_LEECHESEND:
			tick_time = status_get_sc_interval(type);
			val4 = tick - tick_time; // Remaining time
			break;
		case SC_PYREXIA:
			//Causes blind for duration of pyrexia, unreducable and unavoidable, but can be healed with e.g. green potion
			status_change_start(src, bl, SC_BLIND, 10000, val1, 0, 0, 0, tick, SCSTART_NOAVOID | SCSTART_NOTICKDEF | SCSTART_NORATEDEF);
			tick_time = status_get_sc_interval(type);
			val4 = tick - tick_time; // Remaining time	
			break;
		case SC_CONFUSION:
			clif_emotion(bl,E_WHAT);
			break;
		case SC_S_LIFEPOTION:
		case SC_L_LIFEPOTION:
			if( val1 == 0 ) return 0;
			// val1 = heal percent/amout
			// val2 = seconds between heals
			// val4 = total of heals
			if( val2 < 1 ) val2 = 1;
			if( (val4 = tick/(val2 * 1000)) < 1 )
				val4 = 1;
			tick_time = val2 * 1000; // val2 = Seconds between heals
			break;
		case SC_BOSSMAPINFO:
			if( sd != NULL )
			{
				struct mob_data *boss_md = map_getmob_boss(bl->m); // Search for Boss on this Map
				if( boss_md == NULL || boss_md->bl.prev == NULL )
				{ // No MVP on this map - MVP is dead
					clif_bossmapinfo(sd->fd, boss_md, 1);
					return 0; // No need to start SC
				}
				val1 = boss_md->bl.id;
				if( (val4 = tick/1000) < 1 )
					val4 = 1;
				tick_time = 1000;
			}
			break;
		case SC_HIDING:
			val2 = tick/1000;
			tick_time = 1000;
			val3 = 0; // unused, previously speed adjustment
			val4 = val1+3; //Seconds before SP substraction happen.
			break;
		case SC_CHASEWALK:
			val2 = tick>0?tick:10000; //Interval at which SP is drained.
			val3 = 35 - 5 * val1; //Speed adjustment.
			if (sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_ROGUE)
				val3 -= 40;
			val4 = 10+val1*2; //SP cost.
			if (map_flag_gvg2(bl->m) || map[bl->m].flag.battleground) val4 *= 5;
			break;
		case SC_CLOAKING:
			if (!sd) //Monsters should be able to walk with no penalties. [Skotlex]
				val1 = 10;
			tick_time = val2 = tick > 0 ? tick : 60000; //SP consumption rate.
			tick = -1; // duration sent to the client should be infinite
			//val4&1 signals the presence of a wall.
			//val4&2 makes cloak not end on normal attacks [Skotlex]
			//val4&4 makes cloak not end on using skills
			if (bl->type == BL_PC || (bl->type == BL_MOB && ((TBL_MOB*)bl)->special_state.clone))	//Standard cloaking.
				val4 |= battle_config.pc_cloak_check_type&7;
			else
				val4 |= battle_config.monster_cloak_check_type&7;
			break;
		case SC_SIGHT:			/* splash status */
		case SC_RUWACH:
		case SC_SIGHTBLASTER:
			val3 = skill_get_splash(val2, val1); //Val2 should bring the skill-id.
			val2 = tick/20;
			tick_time = 20;
			break;

		//Permanent effects.
		case SC_AETERNA:
		case SC_MODECHANGE:
		case SC_WEIGHT50:
		case SC_WEIGHT90:
		case SC_BROKENWEAPON:
		case SC_BROKENARMOR:
		case SC_READYSTORM:
		case SC_READYDOWN:
		case SC_READYCOUNTER:
		case SC_READYTURN:
		case SC_DODGE:
		case SC_MILLENNIUMSHIELD:
		case SC_SPRITEMABLE:
		case SC_SOULATTACK:
		case SC_ALL_RIDING:
		case SC_MOONSTAR:
		case SC_STRANGELIGHTS:
		case SC_SUPER_STAR:
		case SC_DECORATION_OF_MUSIC:
		case SC_LJOSALFAR:
		case SC_MERMAID_LONGING:
		case SC_HAT_EFFECT:
		case SC_FLOWERSMOKE:
		case SC_FSTONE:
		case SC_HAPPINESS_STAR:
		case SC_MAPLE_FALLS:
		case SC_TIME_ACCESSORY:
		case SC_MAGICAL_FEATHER:
			tick = -1;
			break;

		case SC_AUTOGUARD:
			if( !(flag&1) )
			{
				struct map_session_data *tsd;
				int i;
				for( i = val2 = 0; i < val1; i++)
				{
					int t = 5 - (i >> 1);
					val2 += (t < 0)? 1:t;
				}

				if( bl->type&(BL_PC|BL_MER) )
				{
					if( sd )
					{
						for( i = 0; i < 5; i++ )
						{
							if( sd->devotion[i] && (tsd = map_id2sd(sd->devotion[i])) )
								status_change_start(src, &tsd->bl, type, 10000, val1, val2, 0, 0, tick, 1|16);
						}
					}
					else if( bl->type == BL_MER && ((TBL_MER*)bl)->devotion_flag && (tsd = ((TBL_MER*)bl)->master) )
						status_change_start(src, &tsd->bl, type, 10000, val1, val2, 0, 0, tick, 1);
				}
			}
			break;

		case SC_DEFENDER:
			if (!(flag&1))
			{	
				val2 = 5 + 15*val1; //Damage reduction
				val3 = 0; // unused, previously speed adjustment
				val4 = 250 - 50*val1; //Aspd adjustment 

				if (sd)
				{
					struct map_session_data *tsd;
					int i;
					for (i = 0; i < 5; i++)
					{	//See if there are devoted characters, and pass the status to them. [Skotlex]
						if (sd->devotion[i] && (tsd = map_id2sd(sd->devotion[i])))
							status_change_start(src, &tsd->bl, type, 10000, val1, val2, val3, val4, tick, 1);
					}
				}
			}
			break;

		case SC_TENSIONRELAX:
			if (sd)
			{
				pc_setsit(sd);
				skill_sit(sd, 1);
				clif_sitting(&sd->bl, true);
			}
			val2 = 12; //SP cost
			val4 = 10000; //Decrease at 10secs intervals.
			val3 = tick/val4;
			tick = -1; // duration sent to the client should be infinite
			tick_time = val4;
			break;
		case SC_PARRYING:
			val2 = 20 + val1*3; //Block Chance
			break;

		case SC_WINDWALK:
			val2 = (val1+1)/2; // Flee bonus is 1/1/2/2/3/3/4/4/5/5
			break;

		case SC_JOINTBEAT:
			if( val2&BREAK_NECK )
				sc_start2(bl,SC_BLEEDING,100,val1,src->id,skill_get_time2(status_sc2skill(type),val1));
			break;

		case SC_BERSERK:
			if (!sc->data[SC_ENDURE] || !sc->data[SC_ENDURE]->val4)
				sc_start4(bl, SC_ENDURE, 100,10,0,0,2, tick);
			//HP healing is performing after the calc_status call.
			//Val2 holds HP penalty
			if (!val4) val4 = skill_get_time2(status_sc2skill(type),val1);
			if (!val4) val4 = 10000; //Val4 holds damage interval
			val3 = tick/val4; //val3 holds skill duration
			tick_time = val4;
			break;

		case SC_GOSPEL:
			if(val4 == BCT_SELF) {	// self effect
				val2 = tick/10000;
				tick_time = 10000;
				status_change_clear_buffs(bl,11); //Remove buffs/debuffs
			}
			break;

		case SC_MARIONETTE:
		{
			int stat;

			val3 = 0;
			val4 = 0;
			stat = ( sd ? sd->status.str : status_get_base_status(bl)->str ) / 2; val3 |= cap_value(stat,0,0xFF)<<16;
			stat = ( sd ? sd->status.agi : status_get_base_status(bl)->agi ) / 2; val3 |= cap_value(stat,0,0xFF)<<8;
			stat = ( sd ? sd->status.vit : status_get_base_status(bl)->vit ) / 2; val3 |= cap_value(stat,0,0xFF);
			stat = ( sd ? sd->status.int_: status_get_base_status(bl)->int_) / 2; val4 |= cap_value(stat,0,0xFF)<<16;
			stat = ( sd ? sd->status.dex : status_get_base_status(bl)->dex ) / 2; val4 |= cap_value(stat,0,0xFF)<<8;
			stat = ( sd ? sd->status.luk : status_get_base_status(bl)->luk ) / 2; val4 |= cap_value(stat,0,0xFF);
			break;
		}
		case SC_MARIONETTE2:
		{
			int stat,max_stat;
			// fetch caster information
			struct block_list *pbl = map_id2bl(val1);
			struct status_change *psc = pbl?status_get_sc(pbl):NULL;
			struct status_change_entry *psce = psc?psc->data[SC_MARIONETTE]:NULL;
			// fetch target's stats
			struct status_data* status = status_get_status_data(bl); // battle status

			if (!psce)
				return 0;

			val3 = 0;
			val4 = 0;
			if ( sd && battle_config.marionette_renewal_jobs == 1 &&
				((sd->class_&MAPID_THIRDMASK) >= MAPID_SUPER_NOVICE_E && (sd->class_&MAPID_THIRDMASK) <= MAPID_SOUL_REAPER || (sd->class_&MAPID_UPPERMASK) == MAPID_KAGEROUOBORO || (sd->class_&MAPID_UPPERMASK) == MAPID_REBELLION || (sd->class_&MAPID_BASEMASK) == MAPID_SUMMONER))
				max_stat = battle_config.max_parameter_renewal_jobs;//Custom cap for renewal jobs.
			else
				max_stat = battle_config.max_parameter; //Cap to 99 (default)
			stat = (psce->val3 >>16)&0xFF; stat = min(stat, max_stat - status->str ); val3 |= cap_value(stat,0,0xFF)<<16;
			stat = (psce->val3 >> 8)&0xFF; stat = min(stat, max_stat - status->agi ); val3 |= cap_value(stat,0,0xFF)<<8;
			stat = (psce->val3 >> 0)&0xFF; stat = min(stat, max_stat - status->vit ); val3 |= cap_value(stat,0,0xFF);
			stat = (psce->val4 >>16)&0xFF; stat = min(stat, max_stat - status->int_); val4 |= cap_value(stat,0,0xFF)<<16;
			stat = (psce->val4 >> 8)&0xFF; stat = min(stat, max_stat - status->dex ); val4 |= cap_value(stat,0,0xFF)<<8;
			stat = (psce->val4 >> 0)&0xFF; stat = min(stat, max_stat - status->luk ); val4 |= cap_value(stat,0,0xFF);
			break;
		}
		case SC_SPIRIT:
			//1st Transcendent Spirit works similar to Marionette Control
			if (sd && val2 == SL_HIGH) {
				int stat, max_stat;
				// Fetch target's stats
				struct status_data* status2 = status_get_base_status(bl); // Battle status
				val3 = 0;
				val4 = 0;
				max_stat = (status_get_lv(bl) - 10 < 50) ? status_get_lv(bl) - 10 : 50;
				stat = max(0, max_stat - status2->str); val3 |= cap_value(stat, 0, 0xFF) << 16;
				stat = max(0, max_stat - status2->agi); val3 |= cap_value(stat, 0, 0xFF) << 8;
				stat = max(0, max_stat - status2->vit); val3 |= cap_value(stat, 0, 0xFF);
				stat = max(0, max_stat - status2->int_); val4 |= cap_value(stat, 0, 0xFF) << 16;
				stat = max(0, max_stat - status2->dex); val4 |= cap_value(stat, 0, 0xFF) << 8;
				stat = max(0, max_stat - status2->luk); val4 |= cap_value(stat, 0, 0xFF);
			}
			break;
		case SC_REJECTSWORD:
			val2 = 15*val1; //Reflect chance
			val3 = 3; //Reflections
			tick = -1;
			break;

		case SC_MEMORIZE:
			val2 = 5; //Memorized casts.
			tick = -1;
			break;

		case SC_GRAVITATION:
			val2 = 50*val1; //aspd reduction
			break;

		case SC_REGENERATION:
			if (val1 == 1)
				val2 = 2;
			else
				val2 = val1; //HP Regerenation rate: 200% 200% 300%
			val3 = val1; //SP Regeneration Rate: 100% 200% 300%
			//if val4 comes set, this blocks regen rather than increase it.
			break;

		case SC_DEVOTION:
		{
			struct block_list *d_bl;
			struct status_change *d_sc;

			if( (d_bl = map_id2bl(val1)) && (d_sc = status_get_sc(d_bl)) && d_sc->count )
			{ // Inherits Status From Source
				const enum sc_type types[] = { SC_AUTOGUARD, SC_DEFENDER, SC_REFLECTSHIELD, SC_ENDURE };
				int i = (map_flag_gvg2(bl->m) || map[bl->m].flag.battleground)?2:3;
				while( i >= 0 )
				{
					enum sc_type type2 = types[i];
					if( d_sc->data[type2] )
						status_change_start(d_bl, bl, type2, 10000, d_sc->data[type2]->val1, 0, 0, (type2 == SC_REFLECTSHIELD ? 1 : 0), skill_get_time(status_sc2skill(type2), d_sc->data[type2]->val1), (type2 == SC_DEFENDER) ? 1 : 1|16);
					i--;
				}
			}
			break;
		}

		case SC_COMA: //Coma. Sends a char to 1HP. If val2, do not zap sp
			status_zap(bl, status->hp-1, val2?0:status->sp);
			return 1;
			break;
		case SC_CLOSECONFINE2:
		{
			struct block_list *src = val2?map_id2bl(val2):NULL;
			struct status_change *sc2 = src?status_get_sc(src):NULL;
			struct status_change_entry *sce2 = sc2?sc2->data[SC_CLOSECONFINE]:NULL;
			if (src && sc2) {
				if (!sce2) //Start lock on caster.
					sc_start4(src,SC_CLOSECONFINE,100,val1,1,0,0,tick+1000);
				else { //Increase count of locked enemies and refresh time.
					(sce2->val2)++;
					delete_timer(sce2->timer, status_change_timer);
					sce2->timer = add_timer(gettick()+tick+1000, status_change_timer, src->id, SC_CLOSECONFINE);
				}
			} else //Status failed.
				return 0;
		}
			break;
		case SC_KAITE:
			val2 = 1+val1/5; //Number of bounces: 1 + skill_lv/5
			break;
		case SC_KAUPE:
			switch (val1) {
				case 3: //33*3 + 1 -> 100%
					val2++;
				case 1:
				case 2: //33, 66%
					val2 += 33*val1;
					val3 = 1; //Dodge 1 attack total.
					break;
				default: //Custom. For high level mob usage, higher level means more blocks. [Skotlex]
					val2 = 100;
					val3 = val1-2;
					break;
			}
			break;

		case SC_COMBO:
		{
			// val1: Skill ID
			// val2: When given, target (for autotargetting skills)
			// val3: When set, this combo time should NOT delay attack/movement
			// val3: If set to 2 this combo will delay ONLY attack
			// val3: TK: Last used kick
			// val4: TK: Combo time
			struct unit_data *ud = unit_bl2ud(bl);
			if (ud && (!val3 || val3 == 2)) {
				tick += 300 * battle_config.combo_delay_rate / 100;
				ud->attackabletime = gettick() + tick;
				if (!val3)
					unit_set_walkdelay(bl, gettick(), tick, 1);
			}
			val3 = 0;
			val4 = tick;
			break;
		}
		case SC_EARTHSCROLL:
			val2 = 11-val1; //Chance to consume: 11-skill_lv%
			break;
		case SC_RUN:
		case SC_WUGDASH:
			{
				//Store time at which you started running.
				int64 currenttick = gettick();
				// Note: this int64 value is stored in two separate int32 variables (FIXME)
				val3 = (int)(currenttick&0x00000000ffffffffLL);
				val4 = (int)((currenttick&0xffffffff00000000LL)>>32);
			}
			tick = -1;
			break;
		case SC_KAAHI:
			val2 = 200*val1; //HP heal
			val3 = 5*val1; //SP cost 
			break;
		case SC_BLESSING:
			if ((!undead_flag && status->race!=RC_DEMON) || bl->type == BL_PC)
				val2 = val1;
			else
				val2 = 0; //0 -> Half stat.
			break;
		case SC_TRICKDEAD:
			if (vd) vd->dead_sit = 1;
			tick = -1;
			break;
		case SC_CONCENTRATE:
			val2 = 2 + val1;
			if (sd) { //Store the card-bonus data that should not count in the %
				val3 = sd->param_bonus[1]; //Agi
				val4 = sd->param_bonus[4]; //Dex
			} else {
				val3 = val4 = 0;
			}
			break;
		case SC_MAXOVERTHRUST:
			val2 = 20*val1; //Power increase
			break;
		case SC_ADRENALINE2:
		case SC_ADRENALINE:
		case SC_WEAPONPERFECTION:
			{
				struct map_session_data * s_sd = BL_CAST(BL_PC, src);
				if (type == SC_OVERTHRUST) {
					// val2 holds if it was casted on self, or is bonus received from others
					val3 = (val2) ? 5 * val1 : 5; // Power increase
				}
				else if (type == SC_ADRENALINE2 || type == SC_ADRENALINE) {
					val3 = (val2) ? 300 : 200; // Aspd increase
				}
				if (s_sd && pc_checkskill(s_sd, BS_HILTBINDING) > 0)
					tick += tick / 10; //If caster has Hilt Binding, duration increases by 10%
			}
			break;
		case SC_CONCENTRATION:
			val2 = 5*val1; //Batk/Watk Increase
			val3 = 10*val1; //Hit Increase
			val4 = 5*val1; //Def reduction
			status_change_start(src, bl, SC_ENDURE, 10000, 1, 0, 0, 0, tick, 0); // Level 1 Endure effect
			break;
		case SC_ANGELUS:
			val2 = 5*val1; //def increase
			break;
		case SC_IMPOSITIO:
			val2 = 5*val1; //watk increase
			break;
		case SC_MELTDOWN:
			val2 = 100*val1; //Chance to break weapon
			val3 = 70*val1; //Change to break armor
			break;
		case SC_TRUESIGHT:
			val2 = 10*val1; //Critical increase
			val3 = 3*val1; //Hit increase
			break;
		case SC_SUN_COMFORT:
			val2 = (status_get_lv(bl) + status->dex + status->luk)/2; //def increase
			break;
		case SC_MOON_COMFORT:
			val2 = (status_get_lv(bl) + status->dex + status->luk)/10; //flee increase
			break;
		case SC_STAR_COMFORT:
			val2 = (status_get_lv(bl) + status->dex + status->luk); //Aspd increase
			break;
		case SC_QUAGMIRE:
			val2 = (sd?5:10)*val1; //Agi/Dex decrease.
			break;

		// gs_something1 [Vicious]
		case SC_GATLINGFEVER:
			val2 = 20*val1; //Aspd increase
			val3 = 20+10*val1; //Batk increase
			val4 = 5*val1; //Flee decrease
			break;

		case SC_FLING:
			if (bl->type == BL_PC)
				val2 = 0; //No armor reduction to players.
			else
				val2 = 5*val1; //Def reduction
			val3 = 5*val1; //Def2 reduction
			break;
		case SC_PROVOKE:
			//val2 signals autoprovoke.
			val3 = 2+3*val1; //Atk increase
			val4 = 5+5*val1; //Def reduction.
			break;
		case SC_AVOID:
			//val2 = 10*val1; //Speed change rate.
			break;
		case SC_DEFENCE:
			val2 = 2*val1; //Def bonus
			break;
		case SC_BLOODLUST:
			val2 = 20+10*val1; //Atk rate change.
			val3 = 3*val1; //Leech chance
			val4 = 20; //Leech percent
			break;
		case SC_FLEET:
			val2 = 30*val1; //Aspd change
			val3 = 5+5*val1; //bAtk/wAtk rate change
			break;
		case SC_MINDBREAKER:
			val2 = 20*val1; //matk increase.
			val3 = 12*val1; //mdef2 reduction.
			break;
		case SC_JAILED:
			//Val1 is duration in minutes. Use INT_MAX to specify 'unlimited' time.
			tick = val1>0?1000:250;
			if (sd)
			{
				if (sd->mapindex != val2)
				{
					int pos =  (bl->x&0xFFFF)|(bl->y<<16), //Current Coordinates
					map =  sd->mapindex; //Current Map
					//1. Place in Jail (val2 -> Jail Map, val3 -> x, val4 -> y
					pc_setpos(sd,(unsigned short)val2,val3,val4, CLR_TELEPORT);
					//2. Set restore point (val3 -> return map, val4 return coords
					val3 = map;
					val4 = pos;
				} else if (!val3 || val3 == sd->mapindex) { //Use save point.
					val3 = sd->status.save_point.map;
					val4 = (sd->status.save_point.x&0xFFFF)
						|(sd->status.save_point.y<<16);
				}
			}
			break;
		case SC_UTSUSEMI:
			val2=(val1+1)/2; // number of hits blocked
			val3=skill_get_blewcount(NJ_UTSUSEMI, val1); //knockback value.
			break;
		case SC_BUNSINJYUTSU:
			val2=(val1+1)/2; // number of hits blocked
			break;
		case SC_CHANGE:
			val2= 30*val1; //Vit increase
			val3= 20*val1; //Int increase
			break;
		case SC_SWOO:
			if(status->mode&MD_STATUS_IMMUNE)
				tick /= 5; //TODO: Reduce skill's duration. But for how long?
			break;
		case SC_ARMOR:
			//NPC_DEFENDER:
			val2 = 80; //Damage reduction
			//Attack requirements to be blocked:
			val3 = BF_LONG; //Range
			val4 = BF_WEAPON|BF_MISC; //Type
			break;
		case SC_ENCHANTARMS:
			//end previous enchants
			skill_enchant_elemental_end(bl,type);
			//Make sure the received element is valid.
			if (val2 >= ELE_ALL)
				val2 = val2% ELE_ALL;
			else if (val2 < 0)
				val2 = rnd()% ELE_ALL;
			break;
		case SC_CRITICALWOUND:
			val2 = 20*val1; //Heal effectiveness decrease
			break;
		case SC_MAGICMIRROR:
			val1 = 1 + ((val1 - 1) % 5);
		case SC_SLOWCAST:
			val2 = 20*val1; //Magic reflection/cast rate
			break;

		case SC_ARMORCHANGE:
			if (val2 == NPC_ANTIMAGIC)
			{	//Boost mdef
				val2 =-20;
				val3 = 20;
			} else { //Boost def
				val2 = 20;
				val3 =-20;
			}
			val1 = 1 + ((val1 - 1) % 5);
			val2*=val1; //20% per level
			val3*=val1;
			break;
		case SC_EXPBOOST:
		case SC_JEXPBOOST:
		case SC_JP_EVENT04:
			if (val1 < 0)
				val1 = 0;
			break;
		case SC_INCFLEE2:
		case SC_INCCRI:
			val2 = val1*10; //Actual boost (since 100% = 1000)
			break;
		case SC_SUFFRAGIUM:
			val2 = 15 * val1; //Speed cast decrease
			break;
		case SC_INCHEALRATE:
			if (val1 < 1)
				val1 = 1;
			break;
		case SC_DOUBLECAST:
			val2 = 30+10*val1; //Trigger rate
			break;
		case SC_KAIZEL:
			val2 = 10*val1; //% of life to be revived with
			break;
		// case SC_ARMOR_ELEMENT_WATER:
		// case SC_ARMOR_ELEMENT_EARTH:
		// case SC_ARMOR_ELEMENT_FIRE:
		// case SC_ARMOR_ELEMENT_WIND:
		// case SC_ARMOR_RESIST:
			// Mod your resistance against elements:
			// val1 = water | val2 = earth | val3 = fire | val4 = wind
			// break;
		//case ????:
			//Place here SCs that have no SCB_* data, no skill associated, no ICON
			//associated, and yet are not wrong/unknown. [Skotlex]
			//break;

		case SC_MERC_FLEEUP:
		case SC_MERC_ATKUP:
		case SC_MERC_HITUP:
			val2 = 15 * val1;
			break;
		case SC_MERC_HPUP:
		case SC_MERC_SPUP:
			val2 = 5 * val1;
			break;
		case SC_REBIRTH:
			val2 = 20*val1; //% of life to be revived with
			break;
		case SC_DEEPSLEEP:
			val4 = tick / 2000;
			tick = 2000;
			break;
		case SC_CRYSTALIZE:
			val4 = tick / 1000;
			tick_time = 1000;
			break;
		case SC_DEATHBOUND: // 3ceam v1.
			val2 = 500 + 100 * val1;
			break;
		case SC_REFRESH:
			{
				short i = 0;

				if ( sc )
				{
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

				status_heal(bl,status_get_max_hp(bl) * 25 / 100,0,1);
			}
			break;
		case SC_STONEHARDSKIN:
			// Final DEF/MDEF increase divided by 10 since were using classic (pre-renewal) mechanics. [Rytech]
			val2 = val2 / 4 / 10;// DEF/MDEF Increase
			val3 = 20 * status_get_hp(bl) / 100;// Stone Skin HP
			status_zap(bl, val3, 0);
			break;
		case SC_FIGHTINGSPIRIT:
			val_flag |= 1 | 2;
			break;
		case SC_ABUNDANCE:
			val4 = tick / 10000;
			tick_time = 10000;
			break;

		case SC_MANU_DEF:
		case SC_MANU_ATK:
		case SC_MANU_MATK:
			val2 = 1; // Manuk group
			break;
		case SC_SPL_DEF:
		case SC_SPL_ATK:
		case SC_SPL_MATK:
			val2 = 2; // Splendide group
			break;
		case SC_ENCHANTBLADE://Added MATK Damage
			val2 = 100 + 20 * val1;
			val2 = val2 * status_get_base_lv_effect(bl) / 150;
			val2 = val2 + status->int_;
			break;
		case SC_GIANTGROWTH:
			val2 = 15; // Triple damage success rate.
			break;
		case SC_VENOMIMPRESS:
			val2 = 10 * val1;
			val_flag |= 1|2;
			break;
		case SC_POISONINGWEAPON:
			val_flag |= 1|2|4;
			break;
		case SC_WEAPONBLOCKING:
			val2 = 10 + 2 * val1; // Chance
			val4 = tick / 5000;
			tick_time = 5000;
			val_flag |= 1|2;
			break;
		case SC_OBLIVIONCURSE:
			val4 = tick / 3000;
			tick_time = 3000;
			break;
		case SC_ROLLINGCUTTER:
			val_flag |= 1;
			break;
		case SC_CLOAKINGEXCEED:
			val2 = ( val1 + 1 ) / 2; // Hits
			val3 = ( val1 - 1 ) * 10; // Walk speed
			val_flag |= 1|2|4;			
			if (bl->type == BL_PC)
				val4 |= battle_config.pc_cloak_check_type&7;
			else
				val4 |= battle_config.monster_cloak_check_type&7;
			tick_time = 1000;
			break;
		case SC_HALLUCINATIONWALK:
			val2 = 50 * val1; // Evasion rate of physical attacks. Flee
			val3 = 10 * val1; // Evasion rate of magical attacks.
			val_flag |= 1|2|4;
			break;
		case SC_ADORAMUS:
			val2 = 2 + val1;// AGI Reduction
			break;
		case SC_ORATIO:
			val2 = 2 * val1;// Holy Element Resistance Reduction
			break;
		case SC_LAUDAAGNUS:
			val2 = 2 + 2 * val1;// MaxHP Increase
			break;
		case SC_LAUDARAMUS:
			val2 = 5 * val1;// Critical Damage Increase
			break;
		case SC_RENOVATIO:
			val4 = tick / 5000;
			tick_time = 5000;
			break;
		case SC_EXPIATIO:
			val2 = 5 * val1;
			break;
		case SC_DUPLELIGHT:
			val2 = 10 + 2 * val1;// Success Chance
			break;
		case SC_MARSHOFABYSS:
			val2 = 3 * val1;//AGI and DEX Reduction
			val3 = 10 * val1;//Movement Speed Reduction
 			break;
		case SC_READING_SB:
			// val2 = sp reduction per second
			tick_time = 10000;
			break;
		case SC_STEALTHFIELD_MASTER:
			val4 = tick / (2000 + 1000 * val1);
			tick = val4;
			break;
		case SC_SHAPESHIFT:
			switch( val1 )
			{
				case 1: val2 = ELE_FIRE; break;
				case 2: val2 = ELE_EARTH; break;
				case 3: val2 = ELE_WIND; break;
				case 4: val2 = ELE_WATER; break;
			}
			break;
		case SC_ELECTRICSHOCKER:
			val4 = tick / 1000;
			if( val4 < 1 )
				val4 = 1;
			tick_time = 1000;
			break;
		case SC_CAMOUFLAGE:
			val4 = tick / 1000;
			tick_time = 1000;
			break;
		case SC__REPRODUCE:
			val4 = tick / 1000;
			tick = 1000;
			break;
		case SC__AUTOSHADOWSPELL:
			//Val1: Auto Shadow Spell LV
			//Val2: Skill ID To Autocast
			//Val3: Skill LV To Autocast
			//Val4: Autocast Chance
			if ( val1 >= 10 )
				val4 = 15;// Autocast fixed to 15 if LV is 10 or higher.
			else
				val4 = 30 - 2 * val1;
			break;
		case SC__SHADOWFORM:
			{
				struct map_session_data * s_sd = map_id2sd(val2);
				if( s_sd )
				s_sd->shadowform_id = bl->id;
				val4 = tick / 1000;
				val_flag |= 1|2|4;
				tick_time = 1000;
			}
			break;
		case SC__INVISIBILITY:
			val2 = 20 * val1;// Critical Amount Increase
			val3 = 50 - 10 * val1;// ASPD Reduction
			val4 = tick / 1000;
			tick = -1; // Duration sent to the client should be infinite
			tick_time = 1000;
			val_flag |= 1|2;
			break;
		case SC__ENERVATION:
			val2 = 20 + 10 * val1;// ATK Reduction
			val_flag |= 1|2;
			if( sd ) pc_delspiritball(sd,sd->spiritball,0);
			break;
		case SC__GROOMY:
			val2 = 20 * val1;//HIT Reduction
			val3 = 20 + 10 * val1;//ASPD Reduction
			val_flag |= 1|2|4;
			if( sd )
			{// Removes Animals
				if (pc_isfalcon(sd)) pc_setoption(sd, sd->sc.option&~OPTION_FALCON);
				if (pc_isriding(sd)) pc_setoption(sd, sd->sc.option&~OPTION_RIDING);
				if (pc_isdragon(sd)) pc_setoption(sd, sd->sc.option&~OPTION_DRAGON);
				if (pc_iswug(sd)) pc_setoption(sd, sd->sc.option&~OPTION_WUG);
				if (pc_iswugrider(sd)) pc_setoption(sd, sd->sc.option&~OPTION_WUGRIDER);
				if (sd->status.pet_id > 0) pet_menu(sd, 3);
				if (hom_is_active(sd->hd)) hom_vaporize(sd, 1);
				//Are rental mounts stripped as well? Well find out once I add them in.
			}
			break;
		case SC__LAZINESS:
			val2 = 10 * val1;//FLEE Reduction
			val3 = 10 + 10 * val1;//Increased Cast Time
			val_flag |= 1|2|4;
			break;
		case SC__UNLUCKY:
			val2 = 10 * val1;// Critical and Perfect Dodge Reduction
			val_flag |= 1|2|4;
			break;
		case SC__WEAKNESS:
			val2 = 10 * val1;//MaxHP Reduction
			val_flag |= 1|2;
			skill_strip_equip(bl,EQP_WEAPON|EQP_SHIELD,100,val1,tick);
			break;
		case SC__STRIPACCESSARY:
			if (!sd)
				val2 = 20;
			break;
		case SC__BLOODYLUST:
			val_flag |= 1|2;
			break;
		case SC__BLOODYLUST_BK:// 200ms added to berserk so the HP penalty can be disabled before berserk ends.
			sc_start4(bl, SC_BERSERK, 100, 1, 0, 0, tick+200, tick+200);
			break;
		case SC_GN_CARTBOOST:
			if( val1 < 3 )
				val2 = 50;
			else if( val1 < 5 )
				val2 = 75;
			else
				val2 = 100;
			val3 = 10 * val1;
			break;
		case SC_PROPERTYWALK:
			val_flag |= 1|2;
			val3 = 0;
 			break;
		case SC_STRIKING:
			val4 = tick / 1000;
			tick_time = 1000;
			break;
		case SC_WARMER:
			val4 = tick / 3000;
			tick = 3000;
			break;
		case SC_BLOOD_SUCKER:
			val4 = tick / 1000;
			tick_time = 1000;
			break;
		case SC_SWING:
			val3 = 5 * val1 + val2;//ASPD Increase
			break;
		case SC_SYMPHONY_LOVE:
			val4 = 12 * val1 + val2 + val3 / 4;//MDEF Increase In %
			break;
		case SC_MOONLIT_SERENADE://MATK Increase
		case SC_RUSH_WINDMILL://ATK Increase
			val4 = 6 * val1 + val2 + val3 / 5;
			break;
		case SC_ECHOSONG:
			val4 = 6 * val1 + val2 + val3 / 4;//DEF Increase In %
			break;		
		case SC_HARMONIZE:
			val3 = val1 + val2 / 2;
			break;
		case SC_SIREN:
			val4 = tick / 2000;
			tick_time = 2000;
			break;
		case SC_SIRCLEOFNATURE:
			val2 = 40 * val1;//HP recovery
			val3 = 4 * val1;//SP drain
			val4 = tick / 1000;
			tick_time = 1000;
			break;
		case SC_GLOOMYDAY:
			val2 = 20 + 5 * val1;//Flee reduction
			val3 = 15 + 5 * val1;//ASPD reduction
			val4 = rnd_value(15, 10 * val1 + 5 * val4);// Damage Increase
			if (rnd() % 100 < val1)// Chance of super gloomy effect.
			{
				if ( sd )
				{// If successful, dismount the player. Only applies to Peco/Gryphin and Dragon.
					if ( pc_isriding(sd) )
						pc_setoption(sd, sd->sc.option&~OPTION_RIDING);
					if ( pc_isdragon(sd) )
						pc_setoption(sd, sd->sc.option&~OPTION_DRAGON);
				}
				val1 = 2;// Use to signal if speed reduction applies.
			}
			else// Signal no speed reduction but still give a value.
				val1 = 1;
			break;
		case SC_SONG_OF_MANA:
			val3 = 10 + 5 * val2;
			val4 = tick/5000;
			tick_time = 5000;
			break;
		case SC_DANCE_WITH_WUG:// Fix Me - Ranger / Minstrel / Wanderer - These jobs get a attack bonus of 2 * Skill LV * Performer Count.
			val3 = 5 + 5 * val2;//ASPD Increase
			val4 = 20 + 10 * val2;//Fixed Cast Time Reduction
			break;
		case SC_SATURDAY_NIGHT_FEVER:
			/*val2 = 12000 - 2000 * val1;//HP/SP Drain Timer
			if (val2 < 1000)
				val2 = 1000;//Added to prevent val3 from dividing by 0 when using level 6 or higher through commands. [Rytech]
			val3 = tick/val2;
			tick = val2;*/
			val3 = tick / 3000;
			tick_time = 3000;
			break;
		case SC_LERADS_DEW:
			val3 = 200 * val1 + 300 * val2;//MaxHP Increase
			break;
		case SC_MELODYOFSINK:
			val3 = val1 * (2 + val2);//INT Reduction. Formula Includes Caster And 2nd Performer.
			val4 = tick/1000;
			tick = 1000;
			break;
		case SC_BEYOND_OF_WARCRY:
			val3 = val1 * (2 + val2);//STR And Crit Reduction. Formula Includes Caster And 2nd Performer.
			val4 = 4 * val1 + 4 * val2;//MaxHP Reduction
			break;
		case SC_UNLIMITED_HUMMING_VOICE:
			val3 = 15 - (3 * val2);//Increased SP Cost.
			break;
		case SC_SITDOWN_FORCE:
		case SC_BANANA_BOMB_SITDOWN_POSTDELAY:
			if (sd && !pc_issit(sd))
			{
				pc_setsit(sd);
				skill_sit(sd, 1);
				clif_sitting(&sd->bl, true);
			}
			break;
		case SC_LG_REFLECTDAMAGE:
			val2 = 15 + 5 * val1;//Reflect Amount
			val3 = 25 + 5 * val1; //Number of Reflects
			val4 = tick/ 10000;
			tick_time = 10000;
			break;
		case SC_FORCEOFVANGUARD:// This is not the official way to handle it but I think we should use it. [pakpil]
			val2 = 8 + 12 * val1;//Chance Of Getting A Rage Counter
			val3 = 5 + 2 * val1;//Max Number of Rage Counter's Possiable
			tick = -1; //endless duration in the client
			tick_time = 10000;
			val_flag |= 1|2|4;
			break;
		case SC_EXEEDBREAK:
			val1 = 150 * val1; // 100 * SkillLv
			if (sd)
			{	// Players
				short index = sd->equip_index[EQI_HAND_R];
				if (index >= 0 && sd->inventory_data[index] && sd->inventory_data[index]->type == IT_WEAPON)
				{
					if( battle_config.renewal_level_effect_skills == 1 )
						val1 += 15 * status_get_job_lv_effect(bl) + sd->inventory_data[index]->weight / 10 * sd->inventory_data[index]->wlv * status_get_base_lv_effect(bl) / 100;
					else
						val1 += 15 * status_get_job_lv_effect(bl) + sd->inventory_data[index]->weight / 10 * sd->inventory_data[index]->wlv;
				}
			}
			else	// Monster Use
				val1 += 500;
			break;

		case SC_PRESTIGE:
			val2 = (15 * val1 + 10 * pc_checkskill(sd,CR_DEFENDER)) / 10;// Defence added. Divided by 10 to balance out for pre-renewal mechanics.
			// No way of making this work out. Hard to explain on balance terms.
			//val2 = val2 * status_get_base_lv_effect(bl) / 100;//This is for use in renewal mechanic's only.
			val3 = ((status->int_ + status->luk) * val1 / 20) * status_get_base_lv_effect(bl) / 200 + val1;// Chance to evade magic damage.
			break;
		case SC_BANDING:// val1 = Skill LV, val4 = Skill Group AoE ID.
			val2 = skill_banding_count(sd);// Royal Guard's In Banding Count
			val3 = tick / 5000;
			tick_time = 5000;
			break;
		case SC_SHIELDSPELL_DEF:
		case SC_SHIELDSPELL_MDEF:
		case SC_SHIELDSPELL_REF:
			val_flag |= 1|2;
			break;
		case SC_MAGNETICFIELD:
			val3 = tick / 1000;
			tick_time = 1000;
 			break;
		case SC_INSPIRATION:
			val2 = 40 * val1 + 3 * status_get_job_lv_effect(bl);// ATK Bonus
			val3 = status_get_base_lv_effect(bl) / 10 + status_get_job_lv_effect(bl) / 5;// All Stats Bonus
			val4 = tick / 5000;
			tick_time = 5000;
			status_change_clear_buffs(bl,3); //Remove buffs/debuffs
			break;
		case SC_SPELLFIST:
			val_flag |= 1|2|4;
			break;
		case SC_CRESCENTELBOW:
			val2 = 50 + 5 * val1 + status_get_job_lv_effect(bl) / 2;
			break;
		case SC_LIGHTNINGWALK:
			val2 = 40 + 5 * val1 + status_get_job_lv_effect(bl) / 2;
			break;
		case SC_RAISINGDRAGON:
			val3 = tick / 5000;
			tick_time = 5000;
			break;
		case SC_GENTLETOUCH_ENERGYGAIN:
			val2 = 10 + 5 * val1;//Sphere gain chance.
			break;
		case SC_GENTLETOUCH_CHANGE:
			{
				int casterint = status->int_;
				if (casterint <= 0)
					casterint = 1;//Prevents dividing by 0 since its possiable to reduce players stats to 0; [Rytech]
				val2 = (status->str / 2 + status->dex / 4) * val1 / 5;//Fixed amount of weapon attack increase.
				val3 = status_get_agi(bl) * val1 / 60;//ASPD increase.
				val4 = 200 / status->int_ * val1;//MDEF decrease.
			}
			break;
		case SC_GENTLETOUCH_REVITALIZE:
			val2 = 2 * val1;//MaxHP Increase
			val3 = 50 + 30 * val1;//HP recovery rate increase
			val4 = status->vit / 4 * val1;//VIT defense increase.
			if (sd)
				sd->regen.state.walk = 1;
			break;
		case SC_FIRE_INSIGNIA:
		case SC_WATER_INSIGNIA:
		case SC_WIND_INSIGNIA:
		case SC_EARTH_INSIGNIA:
			val4 = tick / 5000;
			tick = 5000;
			break;
		case SC_P_ALTER:
			val2 = 10 * val1;// ATK Increase
			if (sd)
				val2 += 10 * sd->spiritball_old;
			else
				val2 += 100;
			break;
		case SC_HEAT_BARREL:
			if (sd)
			{
				val2 = 5 * sd->spiritball_old;// Fixed Cast Reduction
				val3 = 20 * sd->spiritball_old;// ATK Increase.
			}
			else// If monster has this buff.
				val3 = 200;
			val4 = 25 + 5 * val1;// HIT Reduction
			break;
		case SC_E_CHAIN:
			val2 = 10;
			if (sd)
				val2 = sd->spiritball_old;
			break;
		case SC_ANTI_M_BLAST:
			val2 = 10 * val1;// Player Damage Resistance Reduction.
			break;
		case SC_SUNSTANCE:
			val2 = 2 + val1;// ATK Increase
			tick = -1;
			break;
		case SC_LUNARSTANCE:
			val2 = 2+val1;// MaxHP Increase
			tick = -1;
			break;
		case SC_STARSTANCE:
			val2 = 4+2*val1;// ASPD Increase
			tick = -1;
			break;
		case SC_UNIVERSESTANCE:
			val2 = 2+val1;// All Stats Increase
			tick = -1;
			break;
		case SC_NEWMOON:
			val2 = 7;// Number of Regular Attacks Until Reveal
			break;
		case SC_FALLINGSTAR:
			val2 = 8 + 2 * (1 + val1) / 2;// Autocast Chance
			if ( val1 >= 7 )
				val2 += 1;// Make it 15% at level 7.
			break;
		case SC_CREATINGSTAR:
			val4 = tick / 500;
			tick = 10;
			break;
		case SC_LIGHTOFSUN:
		case SC_LIGHTOFMOON:
		case SC_LIGHTOFSTAR:
			val2 = 5 * val1;// Skill Damage Increase.
			break;
		case SC_SOULGOLEM:
			val2 = 60 * val1 / 10;// DEF Increase
			val3 = 15 + 5 * val1;// MDEF Increase
			break;
		case SC_SOULSHADOW:
			val2 = (1 + val1) / 2;// ASPD Increase
			val3 = 10 + 2 * val1;// CRIT Increase
			break;
		case SC_SOULFALCON:
			val2 = 10 * val1;// WATK Increase
			val3 = 10;// HIT Increase
			if ( val1 >= 3 )
				val3 += 3;
			if ( val1 >= 5 )
				val3 += 2;
			if ( val1 >= 6 )// In case someone uses a level higher then 5.
				val3 += val1-5;
			break;
		case SC_SOULFAIRY:
			val2 = 10 * val1;// MATK Increase
			val3 = 5;// Variable Cast Time Reduction
			if ( val1 >= 3 )
				val3 += 2;
			if ( val1 >= 5 )
				val3 += 3;
			if ( val1 >= 6 )// In case someone uses a level higher then 5.
				val3 += 4*(val1-5);
			break;
		case SC_CURSEDCIRCLE_TARGET:
			val4 = tick / 1000;
			tick = 1000;
			break;
		case SC_SOULUNITY:
			val4 = tick / 3000;
			tick = 3000;
			break;
		case SC_SOULDIVISION:
			val2 = 10 * val1;// Skill Aftercast Increase
			break;
		case SC_SOULREAPER:
			val2 = 10 + 5 * val1;// Chance of Getting A Soul Sphere.
			break;
		case SC_SOULCOLLECT:
			val2 = 5 + 3 * val2;// Max Soul Sphere's.
			val3 = tick > 0 ? tick : 60000;
			break;
		case SC_MEIKYOUSISUI:
			if (sd) {
				pc_setsit(sd);
				clif_sitting(&sd->bl,true);
				clif_status_load(&sd->bl, SI_SIT, 1);
			}
			val2 = 10 * val1;// Chance of nulling the attack.
			val4 = tick / 1000;
			tick = 1000;
			break;
		case SC_KYOUGAKU:
			val2 = rnd_value(2 * val1, 3 * val1);// Stats decrease.
			val1 = 1002;
			break;
		case SC_ZENKAI:
			if ( sd )
			{
				struct status_data *sstatus = status_get_status_data(bl);

				if ( sstatus->rhw.ele == val2 )
					val2 = status_get_base_lv_effect(bl) + status_get_job_lv_effect(bl);
				else
					val2 = 0;
			}
			else// Monsters
				val2 = 0;
			break;
		case SC_IZAYOI:
			val2 = val1 * (status_get_job_lv_effect(bl) / 2);// MATK Increase.
			break;
		case SC_KYOMU:
			val2 = 5 * val1;// Skill Fail Chance
			break;
		case SC_KAGEMUSYA:
			val4 = tick / 1000;
			tick = 1000;
			break;
		case SC_ZANGETSU:
			{	// Target HP Check
				if (status_get_hp(bl) % 2 == 0)
					val3 = 1;// Even = Increase Attack
				else
					val3 = 2;// Odd = Decrease Attack

				// Target SP Check
				if (status_get_sp(bl) % 2 == 0)
					val4 = 1;// Even = Increase Magic Attack
				else
					val4 = 2;// Odd = Decrease Magic Attack

				// Bonus From Caster's Base Level
				val2 = val2 / 3;
			}
			break;
		case SC_DARKCROW:
			val2 = 30 * val1;// Melee Damage Increase
			break;
		case SC_UNLIMIT:
			val2 = 50 * val1;// Physical Ranged Damage Increase
			break;
		case SC_ILLUSIONDOPING:
			sc_start(bl, SC_HALLUCINATION, 100, val1, tick);
			break;
		case SC_FRIGG_SONG:
			val2 = 5 * val1;// MaxHP Increase
			val3 = tick / 1000;
			tick_time = 1000;
			break;
		case SC_FLASHCOMBO:
			val2 = 20 + 20 * val1;// ATK Increase
			break;
		case SC_OFFERTORIUM:
			val2 = 30 * val1;// Heal Power Increase
			val3 = 100 + 20 * val1;// SP Requirement Increase
			break;
		case SC_TELEKINESIS_INTENSE:
			val2 = 40 * val1;// Damage Increase To Ghost Element Skills
			val3 = 10 * val1;// Variable Cast Time Reduction
			val4 = 10 * val1;// SP Cost Reduction
			break;
		case SC_KINGS_GRACE:
			val2 = tick / 1000;
			tick_time = 1000;
			break;
		case SC_FULL_THROTTLE:
			val2 = tick/1000;
			tick_time = 1000;
			break;
		case SC_REBOUND:
			val2 = tick/2000;
			tick = 2000;
			clif_emotion(bl,E_SWT);
			break;
		case SC_ARCLOUSEDASH:
			val2 = 15 + 5 * val1;// AGI Increase.
			break;
		case SC_SV_ROOTTWIST:
			val3 = tick / 1000;
			tick = 1000;
			break;
		case SC_TUNAPARTY:
			val2 = val2 * (10 * val1) / 100;// Tuna's HP
			break;
		case SC_FRESHSHRIMP:
			val2 = 11000 - 1000 * val1;// Heal interval.
			if ( val2 < 1000 )
				val2 = 1000;// Best to not let the heal interval go below 1 second.
			val3 = tick / val2;
			tick = val2;
			break;
		case SC_NYANGGRASS:
			if (sd)
				val2 = 1;// Reduce DEF/MDEF to 0 for players.
			else
				val2 = 2;// Reduce DEF/MDEF by 50% on non-players.
			break;
		case SC_GROOMING:
			val2 = 100; // Flee
			break;
		case SC_SPIRITOFLAND_MATK:
			val2 = status_get_base_lv_effect(bl);// MATK Increase
			break;
		case SC_SPIRITOFLAND_PERFECTDODGE:
			val2 = 10 * status_get_base_lv_effect(bl) / 12;// Perfect Dodge Increase. 10 = 1.
			break;
		case SC_HELPANGEL:
			val4 = tick / 1000;
			tick = 1000;
			break;
		case SC_NEEDLE_OF_PARALYZE:
			val2 = 5 * val1;// DEF/MDEF  Reduction
			// Need a confirm on if it increases cast time by variable cast, fixed cast, or overall cast and by how much. [Rytech]
			val3 = 5 * val1;// Overall Cast Time Increase.
			break;
		case SC_PAIN_KILLER:
			val3 = 10 * val1;// ASPD Reduction
			val4 = 200 * val1 * val2 / 150;// Damage Reduction.
			sc_start(bl, SC_ENDURE, 100, 1, tick);
			break;
		case SC_LIGHT_OF_REGENE:
			val2 = 20 * val1;// Percent of HP recovered when resurrected.
			break;
		case SC_OVERED_BOOST:
			// Hunger and SP reduction if skill is recasted while status is active.
			if (sc && sc->data[SC_OVERED_BOOST])
				if (hd)
				{// Homunculus hunger is reduced by 50% of max hunger.
					short hunger = hd->homunculus.hunger - 50;
					if (hunger < 1)// Hunger isnt reduced below 1.
						hunger = 1;
					hd->homunculus.hunger = hunger;
				}
				else if (sd)// Master's SP is reduced by 50% of MaxSP
					status_zap(bl,0,status->max_sp * 50 / 100);
			val2 = 300 + 40 * val1;//Fixed FLEE
			val3 = 10 * (200 - (179 + 2 * val1));//Fixed ASPD
			if ( val3 < 100 )// Don't allow going higher then 190 ASPD.
				val3 = 100;
			break;
		case SC_STYLE_CHANGE:
			if ( val1 == FIGHTER_STYLE )
				val2 = 20 + hd->homunculus.level / 5;// Sphere gain chance when attacking.
			else if ( val1 == GRAPPLER_STYLE )
				val2 = hd->homunculus.level / 2;// Sphere gain chance when getting attacked.
			else
				val2 = 0;
			break;
		case SC_MIDNIGHT_FRENZY_POSTDELAY: // Allows Sonic Claw to auto target in the combo.
			clif_hom_skillupdateinfo(hd->master, MH_SONIC_CRAW, INF_SELF_SKILL, 1);
			break;
		case SC_GOLDENE_FERSE:
			val2 = 10 + 10 * val1;// FLEE Increase
			val3 = 60 + 40 * val1;// ASPD Increase In Percent
			val4 = 2 + 2 * val1;// Chance of Holy Property For Regular Attack
			break;
		case SC_ANGRIFFS_MODUS:
			val2 = tick/1000;
			tick = 1000;
			break;
		case SC_CBC:
			val1 = 0;// Prepares for tracking seconds in timer script.
			val4 = tick/1000;
			tick = 1000;
			break;
		case SC_EQC:
			val2 = 2 * val1;// MaxHP Reduction
			val3 = 5 * val1;// ATK/DEF Reduction
			break;
		case SC_GRANITIC_ARMOR:
			val2 = 5 * val1;// Damage Reduction In % - Skill desc says its 2 * SkillLV, but that appears to be wrong.
			val3 = 6 * val1;// HP Loss On End
			break;
		case SC_PYROCLASTIC:
			val2 = 10 * val1 + val2;// Attack Increase - Val2 = Homunculus's Level.
			val3 = 2 * val1;// Autocast Chance
			break;
		case SC_STOMACHACHE:
			val2 = 8;	// SP consume.
			val4 = tick / 10000;
			tick_time = 10000;
			break;
		case SC_PROMOTE_HEALTH_RESERCH:
			//Val1: 1 = Regular Potion, 2 = Thrown Potion
			//Val2: 1 = Small Potion, 2 = Medium Potion, 3 = Large Potion
			//Val3: BaseLV of Thrower For Thrown Potions
			//Val4: MaxHP Increase By Fixed Amount
			if (val1 == 1)// If potion was normally used, take the user's BaseLv.
				val4 = (1000 * val2 - 500) + (status_get_base_lv_effect(bl) * 10 / 3);
			else if (val1 == 2)// If potion was thrown at someone, take the thrower's BaseLv.
				val4 = (1000 * val2 - 500) + (val3 * 10 / 3);
			if (val4 <= 0)//Prevents a negeative value from happening.
				val4 = 0;
			break;
		case SC_ENERGY_DRINK_RESERCH:
			//Val1: 1 = Regular Potion, 2 = Thrown Potion
			//Val2: 1 = Small Potion, 2 = Medium Potion, 3 = Large Potion
			//Val3: BaseLV of Thrower For Thrown Potions
			//Val4: MaxSP Increase By Percentage Amount
			if (val1 == 1)// If potion was normally used, take the user's BaseLv.
				val4 = status_get_base_lv_effect(bl) / 10 + (5 * val2 - 10);
			else if (val1 == 2)// If potion was thrown at someone, take the thrower's BaseLv.
				val4 = val3 / 10 + (5 * val2 - 10);
			if (val4 <= 0)//Prevents a negeative value from happening.
				val4 = 0;
			break;
		case SC_ANCILLA:
			val2 = 15;// Heal Increase
			val3 = 30;// SP Recovery Increase
			break;
		case SC_CLAN_INFO:
 			val_flag |= 1|2;
			tick = -1;
 			break;
		case SC_SWORDCLAN:
		case SC_ARCWANDCLAN:
		case SC_GOLDENMACECLAN:
		case SC_CROSSBOWCLAN:
		case SC_JUMPINGCLAN:
			val_flag |= 1;
			tick = -1;
			status_change_start(src, bl, SC_CLAN_INFO, 10000, 0, val2, 0, 0, -1, flag);
			break;
		case SC_DAILYSENDMAILCNT:
			val_flag |= 1|2;
			tick = -1;
			break;
		case SC_MONSTER_TRANSFORM:
		case SC_ACTIVE_MONSTER_TRANSFORM:
			if( !mobdb_checkid(val1) )
				val1 = MOBID_PORING; // Default poring
			break;
		case SC_APPLEIDUN:
		{
			struct map_session_data * s_sd = BL_CAST(BL_PC, src);
			val2 = (5 + 2 * val1) + (status_get_vit(bl) / 10); //HP Rate: (5 + 2 * skill_lv) + (vit/10) + (BA_MUSICALLESSON level)
			if (s_sd)
				val2 += pc_checkskill(s_sd, BA_MUSICALLESSON) / 2;
			break;
		}
		case SC_EPICLESIS:
			val2 = 5 * val1; //HP rate bonus
			switch (val1) { //! FIXME, looks so weird!
				//val3: HP regen rate bonus
				//val4: SP regen rate bonus
			case 1:
			case 2:
				val3 = 3; //HP regen rate bonus
				val4 = 2; //SP regen rate bonus
				break;
			case 3:
			case 4:
				val3 = 4; //HP regen rate bonus
				val4 = 3; //SP regen rate bonus
				break;
			case 5:
				val3 = 5; //HP regen rate bonus
				val4 = 4; //SP regen rate bonus
				break;
			}
			break;
		case SC_EL_DEFENSIVE:
			if ( battle_config.elem_defensive_support == 1 )
				sc_start4(bl, SC_MODECHANGE, 100, 1, 0, MD_CANATTACK, 0, 0);
			break;
		case SC_EL_OFFENSIVE:
			sc_start4(bl, SC_MODECHANGE, 100, 1, 0, MD_CANATTACK|MD_AGGRESSIVE, 0, 0);
			tick = -1;// Keep active until requested to end.
			break;

		case SC_CIRCLE_OF_FIRE:
		case SC_FIRE_CLOAK:
		case SC_WATER_SCREEN:
		case SC_WATER_DROP:
		case SC_WIND_STEP:
		case SC_WIND_CURTAIN:
		case SC_SOLID_SKIN:
		case SC_STONE_SHIELD:
		case SC_PYROTECHNIC:
		case SC_HEATER:
		case SC_TROPIC:
		case SC_AQUAPLAY:
		case SC_COOLER:
		case SC_CHILLY_AIR:
		case SC_GUST:
		case SC_BLAST:
		case SC_WILD_STORM:
		case SC_PETROLOGY:
		case SC_CURSED_SOIL:
		case SC_UPHEAVAL:
		case SC_TIDAL_WEAPON:
			if ( ed )
			{
				if ( type == SC_WATER_SCREEN )
					ed->water_screen_flag = 1;
				else if ( type == SC_TIDAL_WEAPON )
					val2 = status_get_base_lv_effect(&ed->master->bl) / 3 + status_get_job_lv_effect(&ed->master->bl);
			}
			sc_start(&ed->master->bl, type+1, 100, 1, 0);
			break;

		case SC_CIRCLE_OF_FIRE_OPTION:
		case SC_FIRE_CLOAK_OPTION:
		case SC_WATER_SCREEN_OPTION:
		case SC_WATER_DROP_OPTION:
		case SC_WIND_STEP_OPTION:
		case SC_WIND_CURTAIN_OPTION:
		case SC_SOLID_SKIN_OPTION:
		case SC_STONE_SHIELD_OPTION:
		case SC_PYROTECHNIC_OPTION:
		case SC_HEATER_OPTION:
		case SC_TROPIC_OPTION:
		case SC_AQUAPLAY_OPTION:
		case SC_COOLER_OPTION:
		case SC_CHILLY_AIR_OPTION:
		case SC_GUST_OPTION:
		case SC_BLAST_OPTION:
		case SC_WILD_STORM_OPTION:
		case SC_PETROLOGY_OPTION:
		case SC_CURSED_SOIL_OPTION:
		case SC_UPHEAVAL_OPTION:
		case SC_TIDAL_WEAPON_OPTION:
			{
				switch ( type )
				{
					case SC_WATER_SCREEN_OPTION:
						if ( sd )
						{
							val1 = sd->ed->bl.id;// Elemental ID
							clif_devotion(&sd->ed->bl, NULL);// Not official but....
						}
						break;
					case SC_PYROTECHNIC_OPTION:
					case SC_AQUAPLAY_OPTION:
					case SC_GUST_OPTION:
					case SC_PETROLOGY_OPTION:
						val2 = status_get_job_lv_effect(bl) / 3;// Skill Damage Increase
						break;
					case SC_HEATER_OPTION:
						val2 = status_get_job_lv_effect(bl) / 2;// Skill Damage Increase
						break;
					case SC_TROPIC_OPTION:
					case SC_CHILLY_AIR_OPTION:
					case SC_WILD_STORM_OPTION:
					case SC_UPHEAVAL_OPTION:
						val2 = status_get_job_lv_effect(bl) / 2;// Autocast Chance
						val3 = status_get_job_lv_effect(bl) / 10;// Autocasted Skill LV
						break;
					case SC_COOLER_OPTION:
						val2 = 5 * status_get_job_lv_effect(bl);// Skill Damage Increase
						val3 = status_get_job_lv_effect(bl) / 5;// Crystalize Chance Increase
						break;
					case SC_BLAST_OPTION:
						val2 = status_get_job_lv_effect(bl) / 2;// Skill Damage Increase
						val3 = 5 * status_get_job_lv_effect(bl);// Skill Damage Increase
						break;
					case SC_CURSED_SOIL_OPTION:
						val2 = 5 * status_get_job_lv_effect(bl);// Skill Damage Increase
						val3 = status_get_job_lv_effect(bl);// Skill Damage Increase
						break;
				}

				if (type == SC_WATER_SCREEN_OPTION)
					tick = 10000;
				else
					tick = -1;
			}
			break;

		case SC_POWER_OF_GAIA:
			val2 = 60 - status_get_job_lv_effect(bl);// Movement Speed Reduction.
			if (val2 < 0)
				val2 = 0;
			break;

		case SC_DORAM_BUF_01:
		case SC_DORAM_BUF_02:
			tick_time = 10000; // every 10 seconds
			if ((val4 = tick / tick_time) < 1)
				val4 = 1;
			break;

		default:
			if( calc_flag == SCB_NONE && StatusSkillChangeTable[type] ==-1 && StatusIconChangeTable[type] == SI_BLANK )
			{	//Status change with no calc, no icon, and no skill associated...? 
				ShowError("UnknownStatusChange [%d]\n", type);
				return 0;
			}
	}
	else //Special considerations when loading SC data.
	switch( type )
	{
		case SC_WEDDING:
		case SC_XMAS:
		case SC_SUMMER:
		case SC_HANBOK:
		case SC_OKTOBERFEST:
		case SC_SUMMER2:
			if (!vd)
				break;
			clif_changelook(bl, LOOK_BASE, vd->class_);
			clif_changelook(bl,LOOK_WEAPON,0);
			clif_changelook(bl,LOOK_SHIELD,0);
			clif_changelook(bl, LOOK_CLOTHES_COLOR, vd->cloth_color);
			break;
		case SC_STONE:
			if (val3 > 0)
				break; //Incubation time still active
			//Fall through
		case SC_POISON:
		case SC_DPOISON:
		case SC_BLEEDING:
		case SC_BURNING:
		case SC_TOXIN:
		case SC_MAGICMUSHROOM:
		case SC_PYREXIA:
		case SC_LEECHESEND:
			tick_time = tick;
			tick = tick_time + max(val4, 0);
			break;
		case SC_SWORDCLAN:
		case SC_ARCWANDCLAN:
		case SC_GOLDENMACECLAN:
		case SC_CROSSBOWCLAN:
		case SC_JUMPINGCLAN:
		case SC_CLAN_INFO:
			// If the player still has a clan status, but was removed from his clan
			if (sd && sd->status.clan_id == 0){
				return 0;
			}
			break;
	}

	// Values that must be set regardless of flag&4 e.g. val_flag [Ind]
	switch(type)
	{
		// Start |1 val_flag setting
		case SC_MONSTER_TRANSFORM:
		case SC_ACTIVE_MONSTER_TRANSFORM:
		case SC_JP_EVENT04:
			val_flag |= 1;
			break;
	}

	if (StatusDisplayType[type] & bl->type) {
		int dval1 = 0, dval2 = 0, dval3 = 0;

		switch (type) {
		case SC_ALL_RIDING:
			dval1 = 1;
			break;
		case SC_CLAN_INFO:
			dval1 = val1;
			dval2 = val2;
			dval3 = val3;
			break;
		default: /* All others: just copy val1 */
			dval1 = val1;
			break;
		}
		status_display_add(bl, type, dval1, dval2, dval3);
		clif_efst_status_change_sub(bl, bl, AREA);
	}
	
	//Those that make you stop attacking/walking....
	switch (type)
	{
		case SC_IMPRISON:
		case SC_DEEPSLEEP:
		case SC_CRYSTALIZE:
			if (sd && pc_issit(sd)) //Avoid sprite sync problems.
				pc_setstand(sd, true);
		case SC_FREEZE:
		case SC_STUN:
			if (sc->data[SC_DANCING])
				unit_stop_walking(bl, 1);
		case SC_TRICKDEAD:
			status_change_end(bl, SC_DANCING, INVALID_TIMER);
		case SC_SLEEP:
		case SC_STONE:
		case SC__MANHOLE:
		case SC_FALLENEMPIRE:
		case SC_CURSEDCIRCLE_ATKER:
		case SC_CURSEDCIRCLE_TARGET:
		case SC_GRAVITYCONTROL:
			unit_stop_attack(bl);
			// Cancel cast when get status [LuzZza]
			if (battle_config.sc_castcancel&bl->type)
				unit_skillcastcancel(bl, 0);
		case SC_STOP:
		case SC_CONFUSION:
		case SC_CLOSECONFINE:
		case SC_CLOSECONFINE2:
		case SC_SPIDERWEB:
		case SC_ELECTRICSHOCKER:
		case SC_WUGBITE:
		case SC_MAGNETICFIELD:
		case SC_CONFUSIONPANIC:
		case SC_NETHERWORLD:
		case SC_VACUUM_EXTREME:
		case SC_THORNS_TRAP:
		case SC_KG_KAGEHUMI:
		case SC_SV_ROOTTWIST:
		case SC_NEEDLE_OF_PARALYZE:
		case SC_TINDER_BREAKER:
		case SC_ANKLE:
			if (!unit_blown_immune(bl, 0x1))
				unit_stop_walking(bl, 1);
			break;
		case SC_HIDING:
		case SC_CLOAKING:
		case SC_CHASEWALK:
		case SC_WEIGHT90:
		case SC_SIREN:
		case SC_CLOAKINGEXCEED:
		case SC_CAMOUFLAGE:
		case SC_HEAT_BARREL_AFTER:
		case SC_ALL_RIDING:
		case SC_NEWMOON:
		case SC_SUHIDE:
			unit_stop_attack(bl);
		break;
		case SC_SILENCE:
			if (battle_config.sc_castcancel&bl->type)
				unit_skillcastcancel(bl, 0);
		break;
		// Stops everything your doing.
		case SC_KINGS_GRACE:
			unit_stop_walking(bl,1);
			unit_stop_attack(bl);
			unit_skillcastcancel(bl, 0);
		break;
		case SC_ITEMSCRIPT: // Shows Buff Icons
			if (sd && val2 != SI_BLANK)
				clif_status_change(bl, (enum si_type)val2, 1, tick, 0, 0, 0);
			break;
	}

	// Set option as needed.
	opt_flag = 1;
	switch(type)
	{
		//OPT1
	case SC_STONE:
		if (val3 > 0)
			sc->opt1 = OPT1_STONEWAIT;
		else
			sc->opt1 = OPT1_STONE;
		break;
		case SC_FREEZE:		sc->opt1 = OPT1_FREEZE;		break;
		case SC_STUN:		sc->opt1 = OPT1_STUN;		break;
		case SC_SLEEP:		sc->opt1 = OPT1_SLEEP;		break;
		case SC_BURNING:	sc->opt1 = OPT1_BURNING;	break; // Burning need this to be showed correctly. [pakpil]
		case SC_IMPRISON:	sc->opt1 = OPT1_IMPRISON;	break;
		//OPT2
		case SC_POISON:									sc->opt2 |= OPT2_POISON;		break;
		case SC_CURSE:									sc->opt2 |= OPT2_CURSE;			break;
		case SC_SILENCE:								sc->opt2 |= OPT2_SILENCE;		break;
		case SC_SIGNUMCRUCIS:	case SC_CONFUSION:			sc->opt2 |= OPT2_SIGNUMCRUCIS;	break;
		case SC_BLIND:									sc->opt2 |= OPT2_BLIND;			break;
		case SC_ANGELUS:		case SC_BENEDICTIO:		sc->opt2 |= OPT2_ANGELUS;		break;// Piety use this, i need confirm if Benedictio do it too. [pakpil]
		case SC_BLEEDING:								sc->opt2 |= OPT2_BLEEDING;		break;
		case SC_DPOISON:								sc->opt2 |= OPT2_DPOISON;		break;
		case SC_FEAR:									sc->opt2 |= OPT2_FEAR;			break;
		//OPT3
		case SC_TWOHANDQUICKEN:
		case SC_ONEHAND:
		case SC_SPEARQUICKEN:
		case SC_CONCENTRATION:
		case SC_MERC_QUICKEN:
			sc->opt3 |= OPT3_QUICKEN;
			opt_flag = 0;
			break;
		case SC_MAXOVERTHRUST:
		case SC_OVERTHRUST:
		case SC_SWOO:	//Why does it shares the same opt as Overthrust? Perhaps we'll never know...
			sc->opt3 |= OPT3_OVERTHRUST;
			opt_flag = 0;
			break;
		case SC_ENERGYCOAT:
		case SC_SKE:
			sc->opt3 |= OPT3_ENERGYCOAT;
			opt_flag = 0;
			break;
		case SC_INCATKRATE:
			//Simulate Explosion Spirits effect for NPC_POWERUP [Skotlex]
			if (bl->type != BL_MOB) {
				opt_flag = 0;
				break;
			}
		case SC_EXPLOSIONSPIRITS:
			sc->opt3 |= OPT3_EXPLOSIONSPIRITS;
			opt_flag = 0;
			break;
		case SC_STEELBODY:
		case SC_SKA:
			sc->opt3 |= OPT3_STEELBODY;
			opt_flag = 0;
			break;
		case SC_BLADESTOP:
			sc->opt3 |= OPT3_BLADESTOP;
			opt_flag = 0;
			break;
		case SC_AURABLADE:
			sc->opt3 |= OPT3_AURABLADE;
			opt_flag = 0;
			break;
		case SC_BERSERK:
			sc->opt3 |= OPT3_BERSERK;
			opt_flag = 0;
			break;
//		case ???: // doesn't seem to do anything
//			sc->opt3 |= OPT3_LIGHTBLADE;
//			opt_flag = 0;
//			break;
		case SC_DANCING:
			if ((val1&0xFFFF) == CG_MOONLIT)
				sc->opt3 |= OPT3_MOONLIT;
			opt_flag = 0;
			break;
		case SC_MARIONETTE:
		case SC_MARIONETTE2:
			sc->opt3 |= OPT3_MARIONETTE;
			opt_flag = 0;
			break;
		case SC_ASSUMPTIO:
			sc->opt3 |= OPT3_ASSUMPTIO;
			opt_flag = 0;
			break;
		case SC_WARM: //SG skills [Komurka]
			sc->opt3 |= OPT3_WARM;
			opt_flag = 0;
			break;
		case SC_KAITE:
			sc->opt3 |= OPT3_KAITE;
			opt_flag = 0;
			break;
		case SC_BUNSINJYUTSU:
			sc->opt3 |= OPT3_BUNSIN;
			opt_flag = 0;
			break;
		case SC_SPIRIT:
			sc->opt3 |= OPT3_SOULLINK;
			opt_flag = 0;
			break;
		case SC_CHANGEUNDEAD:
			sc->opt3 |= OPT3_UNDEAD;
			opt_flag = 0;
			break;
//		case ???: // from DA_CONTRACT (looks like biolab mobs aura)
//			sc->opt3 |= OPT3_CONTRACT;
//			opt_flag = 0;
//			break;
		//OPTION
		case SC_HIDING:
			sc->option |= OPTION_HIDE;
			opt_flag = 2;
			break;
		case SC_CLOAKING:
		case SC_CLOAKINGEXCEED:
		case SC__INVISIBILITY:
		case SC_NEWMOON:
			sc->option |= OPTION_CLOAK;
			opt_flag = 2;
			break;
		case SC_CHASEWALK:
			sc->option |= OPTION_CHASEWALK|OPTION_CLOAK;
			opt_flag = 2;
			break;
		case SC_SIGHT:
			sc->option |= OPTION_SIGHT;
			break;
		case SC_RUWACH:
			sc->option |= OPTION_RUWACH;
			break;
		case SC_WEDDING:
			sc->option |= OPTION_WEDDING;
			opt_flag |= 0x4;
			break;
		case SC_XMAS:
			sc->option |= OPTION_XMAS;
			opt_flag |= 0x4;
			break;
		case SC_SUMMER:
			sc->option |= OPTION_SUMMER;
			opt_flag |= 0x4;
			break;
		case SC_HANBOK:
			sc->option |= OPTION_HANBOK;
			opt_flag |= 0x4;
			break;
		case SC_OKTOBERFEST:
			sc->option |= OPTION_OKTOBERFEST;
			opt_flag |= 0x4;
			break;
		case SC_SUMMER2:
			sc->option |= OPTION_SUMMER2;
			opt_flag |= 0x4;
			break;
		case SC_ORCISH:
			sc->option |= OPTION_ORCISH;
			break;
		case SC_FUSION:
			sc->option |= OPTION_FLYING;
			break;
		default:
			opt_flag = 0;
	}

	//On Aegis, when turning on a status change, first goes the option packet, then the sc packet.
	if(opt_flag) {
		clif_changeoption(bl);

		if (sd && (opt_flag & 0x4)) {
			clif_changelook(bl, LOOK_BASE, vd->class_);
			clif_changelook(bl, LOOK_WEAPON, 0);
			clif_changelook(bl, LOOK_SHIELD, 0);
			clif_changelook(bl, LOOK_CLOTHES_COLOR, vd->cloth_color);
		}
	}

	if (calc_flag&SCB_DYE)
	{	//Reset DYE color
		if (vd && vd->cloth_color)
		{
			val4 = vd->cloth_color;
			clif_changelook(bl,LOOK_CLOTHES_COLOR,0);
		}
		calc_flag&=~SCB_DYE;
	}

	/*if (calc_flag&SCB_BODY)// Might be needed in the future. [Rytech]
	{	//Reset body style
		if (vd && vd->body_style)
		{
			val4 = vd->body_style;
			clif_changelook(bl,LOOK_BODY2,0);
		}
		calc_flag&=~SCB_BODY;
	}*/

	if (!(flag & 16) && !(flag & 4 && StatusDisplayType[type]))
		clif_status_change(bl, StatusIconChangeTable[type], 1, tick, (val_flag & 1) ? val1 : 1, (val_flag & 2) ? val2 : 0, (val_flag & 4) ? val3 : 0);

	/**
	 * used as temporary storage for scs with interval ticks, so that the actual duration is sent to the client first.
	 **/
	if (tick_time)
		tick = tick_time;

	//Don't trust the previous sce assignment, in case the SC ended somewhere between there and here.
	if((sce=sc->data[type]))
	{// reuse old sc
		if( sce->timer != INVALID_TIMER )
			delete_timer(sce->timer, status_change_timer);
	}
	else
	{// new sc
		++(sc->count);
		sce = sc->data[type] = ers_alloc(sc_data_ers, struct status_change_entry);
	}
	sce->val1 = val1;
	sce->val2 = val2;
	sce->val3 = val3;
	sce->val4 = val4;
	if (tick >= 0)
		sce->timer = add_timer(gettick() + tick, status_change_timer, bl->id, type);
	else
		sce->timer = INVALID_TIMER; //Infinite duration

	if (calc_flag)
		status_calc_bl(bl,calc_flag);
	
	if(sd && sd->pd)
		pet_sc_check(sd, type); //Skotlex: Pet Status Effect Healing

	switch( type )
	{
		case SC_BERSERK:
		case SC_SATURDAY_NIGHT_FEVER:
			sce->val2 = 5*status->max_hp/100;
			status_heal(bl, status->max_hp, 0, 1); //Do not use percent_heal as this healing must override BERSERK's block.
			status_set_sp(bl, 0, 0); //Damage all SP
			break;
		case SC_CHANGE:
			status_percent_heal(bl, 100, 100);
			break;
		case SC_RUN:
			{
				struct unit_data *ud = unit_bl2ud(bl);
				if( ud )
					ud->state.running = unit_run(bl);
			}
			break;
		case SC_BOSSMAPINFO:
			if (sd)
				clif_bossmapinfo(sd->fd, map_id2boss(sce->val1), 0); // First Message
			break;
		case SC_MERC_HPUP:
		case SC_FULL_THROTTLE:
			status_percent_heal(bl, 100, 0); // Recover Full HP
			break;
		case SC_MERC_SPUP:
			status_percent_heal(bl, 0, 100); // Recover Full SP
			break;
		case SC_FEAR:
			status_change_start(src, bl, SC_ANKLE, 10000, val1, 0, 0, 0, 2000, SCSTART_NOAVOID | SCSTART_NOTICKDEF | SCSTART_NORATEDEF);
			break;
		case SC_WUGDASH:
			{
				struct unit_data *ud = unit_bl2ud(bl);
				if( ud )
					ud->state.running = unit_wugdash(bl, sd);
			}
			break;
		case SC_RAISINGDRAGON:
			sce->val2 = status->max_hp / 100;// Officially tested its 1%hp drain. [Jobbie]
			break;
		case SC_COMBO:
			switch (sce->val1) {
				case TK_STORMKICK:
					skill_combo_toggle_inf(bl, TK_JUMPKICK, 0);
					clif_skill_nodamage(bl,bl,TK_READYSTORM,1,1);
					break;
				case TK_DOWNKICK:
					skill_combo_toggle_inf(bl, TK_JUMPKICK, 0);
					clif_skill_nodamage(bl,bl,TK_READYDOWN,1,1);
					break;
				case TK_TURNKICK:
					skill_combo_toggle_inf(bl, TK_JUMPKICK, 0);
					clif_skill_nodamage(bl,bl,TK_READYTURN,1,1);
					break;
				case TK_COUNTER:
					skill_combo_toggle_inf(bl, TK_JUMPKICK, 0);
					clif_skill_nodamage(bl,bl,TK_READYCOUNTER,1,1);
					break;
				default: //rest just toggle inf to enable autotarget
					skill_combo_toggle_inf(bl, sce->val1, INF_SELF_SKILL);
					break;
			}
			break;
		case SC_GVG_GIANT:
		case SC_GVG_GOLEM:
		case SC_GVG_STUN:
		case SC_GVG_STONE:
		case SC_GVG_FREEZ:
		case SC_GVG_SLEEP:
		case SC_GVG_CURSE:
		case SC_GVG_SILENCE:
		case SC_GVG_BLIND:
			if (val1 || val2)
				status_zap(bl, val1 ? val1 : 0, val2 ? val2 : 0);
			break;
	}

	if( opt_flag&2 && sd && sd->touching_id )
		npc_touchnext_areanpc(sd,false); // run OnTouch_ on next char in range

	return 1;
}
/*==========================================
 * Ending all status except those listed.
 * @TODO maybe usefull for dispel instead reseting a liste there.
 * type:
 * 0 - PC killed -> Place here statuses that do not dispel on death.
 * 1 - If for some reason status_change_end decides to still keep the status when quitting.
 * 2 - Do clif
 * 3 - Do not remove some permanent/time-independent effects
 *------------------------------------------*/
int status_change_clear(struct block_list* bl, int type)
{
	struct status_change* sc;
	int i;

	sc = status_get_sc(bl);

	if (!sc)
		return 0;

	//cleaning all extras vars
	sc->comet_x = 0;
	sc->comet_y = 0;
	sc->sg_counter = 0;

	if (!sc->count)
		return 0;

	for(i = 0; i < SC_MAX; i++)
	{
		if(!sc->data[i])
		  continue;

		if(type == 0)
			switch(i)
			{	//Type 0: PC killed -> Place here statuses that do not dispel on death.
				case SC_ELEMENTALCHANGE://Only when its Holy or Dark that it doesn't dispell on death
					if (sc->data[i]->val2 != ELE_HOLY && sc->data[i]->val2 != ELE_DARK)
						break;
				case SC_WEIGHT50:
				case SC_WEIGHT90:
				case SC_EDP:
				case SC_MELTDOWN:
				case SC_WEDDING:
				case SC_XMAS:
				case SC_SUMMER:
				case SC_HANBOK:
				case SC_OKTOBERFEST:
				case SC_SUMMER2:
				case SC_NOCHAT:
				case SC_FUSION:
				case SC_EARTHSCROLL:
				case SC_READYSTORM:
				case SC_READYDOWN:
				case SC_READYCOUNTER:
				case SC_READYTURN:
				case SC_DODGE:
				case SC_MIRACLE:
				case SC_JAILED:
				case SC_EXPBOOST:
				case SC_ITEMBOOST:
				case SC_HELLPOWER:
				case SC_JEXPBOOST:
				case SC_AUTOTRADE:
				case SC_WHISTLE:
				case SC_ASSNCROS:
				case SC_POEMBRAGI:
				case SC_APPLEIDUN:
				case SC_HUMMING:
				case SC_DONTFORGETME:
				case SC_FORTUNE:
				case SC_SERVICE4U:
				case SC_FOOD_STR_CASH:
				case SC_FOOD_AGI_CASH:
				case SC_FOOD_VIT_CASH:
				case SC_FOOD_DEX_CASH:
				case SC_FOOD_INT_CASH:
				case SC_FOOD_LUK_CASH:
				case SC_SAVAGE_STEAK:
				case SC_COCKTAIL_WARG_BLOOD:
				case SC_MINOR_BBQ:
				case SC_SIROMA_ICE_TEA:
				case SC_DROCERA_HERB_STEAMED:
				case SC_PUTTI_TAILS_NOODLES:
				case SC_ON_PUSH_CART:
				case SC_MOONSTAR:
				case SC_LIGHT_OF_REGENE:
				case SC_STRANGELIGHTS:
				case SC_SUPER_STAR:
				case SC_DECORATION_OF_MUSIC:
				case SC_LJOSALFAR:
				case SC_MERMAID_LONGING:
				case SC_HAT_EFFECT:
				case SC_FLOWERSMOKE:
				case SC_FSTONE:
				case SC_HAPPINESS_STAR:
				case SC_MAPLE_FALLS:
				case SC_TIME_ACCESSORY:
				case SC_MAGICAL_FEATHER:
				case SC_SPRITEMABLE:
				case SC_DORAM_BUF_01:
				case SC_DORAM_BUF_02:
				case SC_SOULATTACK:
				case SC_CLAN_INFO:
				case SC_SWORDCLAN:
				case SC_ARCWANDCLAN:
				case SC_GOLDENMACECLAN:
				case SC_CROSSBOWCLAN:
				case SC_JUMPINGCLAN:
				case SC_DAILYSENDMAILCNT:
				case SC_ATTHASTE_CASH:
				case SC_REUSE_REFRESH:
				case SC_REUSE_LIMIT_A:
				case SC_REUSE_LIMIT_B:
				case SC_REUSE_LIMIT_C:
				case SC_REUSE_LIMIT_D:
				case SC_REUSE_LIMIT_E:
				case SC_REUSE_LIMIT_F:
				case SC_REUSE_LIMIT_G:
				case SC_REUSE_LIMIT_H:
				case SC_REUSE_LIMIT_MTF:
				case SC_REUSE_LIMIT_ECL:
				case SC_REUSE_LIMIT_RECALL:
				case SC_REUSE_LIMIT_ASPD_POTION:
				case SC_REUSE_MILLENNIUMSHIELD:
				case SC_REUSE_CRUSHSTRIKE:
				case SC_REUSE_STORMBLAST:
				case SC_ALL_RIDING_REUSE_LIMIT:
				case SC_2011RWC_SCROLL:
				case SC_JP_EVENT04:
					continue;
			}

		if(type == 3)
			switch(i)
			{	// Type 3: Do not remove some permanent/time-independent effects
				// TODO: This list may be incomplete
				case SC_WEIGHT50:
				case SC_WEIGHT90:
				case SC_NOCHAT:
				case SC_ON_PUSH_CART:
				case SC_CLAN_INFO:
				case SC_SWORDCLAN:
				case SC_ARCWANDCLAN:
				case SC_GOLDENMACECLAN:
				case SC_CROSSBOWCLAN:
				case SC_JUMPINGCLAN:
				case SC_DAILYSENDMAILCNT:
					continue;
			}

		if ( sc && sc->data[SC_MONSTER_TRANSFORM] && battle_config.transform_end_on_death == 0 && type == 0 )
			continue;//Config if the monster transform status should end on death. [Rytech]
		status_change_end(bl, (sc_type)i, INVALID_TIMER);

		if( type == 1 && sc->data[i] )
		{	//If for some reason status_change_end decides to still keep the status when quitting. [Skotlex]
			(sc->count)--;
			if (sc->data[i]->timer != INVALID_TIMER)
				delete_timer(sc->data[i]->timer, status_change_timer);
			ers_free(sc_data_ers, sc->data[i]);
			sc->data[i] = NULL;
		}
	}

	sc->opt1 = 0;
	sc->opt2 = 0;
	sc->opt3 = 0;
	sc->option &= OPTION_MASK;

	if( type == 0 || type == 2 )
		clif_changeoption(bl);

	return 1;
}

/*==========================================
 * ステータス異常終了
 *------------------------------------------*/
int status_change_end_(struct block_list* bl, enum sc_type type, int tid)
{
	struct map_session_data *sd;
	struct homun_data *hd;
	struct elemental_data *ed;
	struct status_change *sc;
	struct status_change_entry *sce;
	struct status_data *status;
	struct view_data *vd;
	int opt_flag=0, calc_flag;

	nullpo_ret(bl);
	
	sc = status_get_sc(bl);

	if(type < 0 || type >= SC_MAX || !sc || !(sce = sc->data[type]))
		return 0;

	sd = BL_CAST(BL_PC,bl);
	hd = BL_CAST(BL_HOM, bl);
	ed = BL_CAST(BL_ELEM, bl);

	if (sce->timer != tid && tid != INVALID_TIMER)
		return 0;

	status = status_get_status_data(bl);

	if (tid == INVALID_TIMER) {
		if (type == SC_ENDURE && sce->val4)
			//Do not end infinite endure.
			return 0;
		if (type == SC_SPIDERWEB) {
			//Delete the unit group first to expire found in the status change
			struct skill_unit_group *group = NULL, *group2 = NULL;
			int64 tick = gettick();
			int pos = 1;
			if (sce->val2)
				if (!(group = skill_id2group(sce->val2)))
					sce->val2 = 0;
			if (sce->val3) {
				if (!(group2 = skill_id2group(sce->val3)))
					sce->val3 = 0;
				else if (!group || ((group->limit - DIFF_TICK32(tick, group->tick)) > (group2->limit - DIFF_TICK32(tick, group2->tick)))) {
					group = group2;
					pos = 2;
				}
			}
			if (sce->val4) {
				if (!(group2 = skill_id2group(sce->val4)))
					sce->val4 = 0;
				else if (!group || ((group->limit - DIFF_TICK32(tick, group->tick)) > (group2->limit - DIFF_TICK32(tick, group2->tick)))) {
					group = group2;
					pos = 3;
				}
			}
			if (pos == 1)
				sce->val2 = 0;
			else if (pos == 2)
				sce->val3 = 0;
			else if (pos == 3)
				sce->val4 = 0;
			if (group)
				skill_delunitgroup(group);
			if (!status_isdead(bl) && (sce->val2 || sce->val3 || sce->val4))
				return 0; //Don't end the status change yet as there are still unit groups associated with it
		}
		if (sce->timer != INVALID_TIMER) //Could be a SC with infinite duration
			delete_timer(sce->timer,status_change_timer);
		if (sc->opt1)
		switch (type) {
			//"Ugly workaround"  [Skotlex]
			//delays status change ending so that a skill that sets opt1 fails to 
			//trigger when it also removed one
			case SC_STONE:
				sce->val4 = -1; // Petrify time
			case SC_FREEZE:
			case SC_STUN:
			case SC_SLEEP:
			//case SC_BURNING:// Having this enabled causes issues.
			case SC_IMPRISON:
				if (sce->val1) {
			  		//Removing the 'level' shouldn't affect anything in the code
					//since these SC are not affected by it, and it lets us know
					//if we have already delayed this attack or not.
					sce->val1 = 0;
					sce->timer = add_timer(gettick()+10, status_change_timer, bl->id, type);
					return 1;
				}
		}
	}

	sc->data[type] = NULL;
	(sc->count)--;

	if (StatusDisplayType[type] & bl->type)
		status_display_remove(bl, type);

	vd = status_get_viewdata(bl);
	calc_flag = StatusChangeFlagTable[type];
	switch(type){
		case SC_RUN:
		{
			struct unit_data *ud = unit_bl2ud(bl);
			bool begin_spurt = true;
			// Note: this int64 value is stored in two separate int32 variables (FIXME)
			int64 starttick  = (int64)sce->val3&0x00000000ffffffffLL;
			      starttick |= ((int64)sce->val4<<32)&0xffffffff00000000LL;

			if (ud) {
				if(!ud->state.running)
					begin_spurt = false;
				ud->state.running = 0;
				if (ud->walktimer != INVALID_TIMER)
					unit_stop_walking(bl,1);
			}
			if (begin_spurt && sce->val1 >= 7 &&
				DIFF_TICK(gettick(), starttick) <= 1000 &&
				(!sd || (sd->weapontype1 == 0 && sd->weapontype2 == 0))
			)
				sc_start(bl,SC_SPURT,100,sce->val1,skill_get_time2(status_sc2skill(type), sce->val1));
		}
		break;
		case SC_AUTOBERSERK:
			if (sc->data[SC_PROVOKE] && sc->data[SC_PROVOKE]->val2 == 1)
				status_change_end(bl, SC_PROVOKE, INVALID_TIMER);
			break;

		case SC_ENDURE:
		case SC_DEFENDER:
		case SC_REFLECTSHIELD:
		case SC_AUTOGUARD:
			{
				struct map_session_data *tsd;
				if( bl->type == BL_PC )
				{ // Clear Status from others
					int i;
					for( i = 0; i < 5; i++ )
					{
						if( sd->devotion[i] && (tsd = map_id2sd(sd->devotion[i])) && tsd->sc.data[type] )
							status_change_end(&tsd->bl, type, INVALID_TIMER);
					}
				}
				else if( bl->type == BL_MER && ((TBL_MER*)bl)->devotion_flag )
				{ // Clear Status from Master
					tsd = ((TBL_MER*)bl)->master;
					if( tsd && tsd->sc.data[type] )
						status_change_end(&tsd->bl, type, INVALID_TIMER);
				}
			}
			break;
		case SC_DEVOTION:
			{
				struct block_list *d_bl = map_id2bl(sce->val1);
				if( d_bl )
				{
					if( d_bl->type == BL_PC )
						((TBL_PC*)d_bl)->devotion[sce->val2] = 0;
					else if( d_bl->type == BL_MER )
						((TBL_MER*)d_bl)->devotion_flag = 0;
					clif_devotion(d_bl, NULL);
				}

				status_change_end(bl, SC_AUTOGUARD, INVALID_TIMER);
				status_change_end(bl, SC_DEFENDER, INVALID_TIMER);
				status_change_end(bl, SC_REFLECTSHIELD, INVALID_TIMER);
				status_change_end(bl, SC_ENDURE, INVALID_TIMER);
			}
			break;

		case SC_WATER_SCREEN_OPTION:
			{
				struct block_list *d_bl = map_id2bl(sce->val1);
				if( d_bl )
				{
					if( d_bl->type == BL_ELEM )
						((TBL_ELEM*)d_bl)->water_screen_flag = 0;
					clif_devotion(d_bl, NULL);
				}
			}
			break;

		case SC_CURSEDCIRCLE_TARGET:
			{
				struct block_list *d_bl = map_id2bl(sce->val1);
				if( d_bl )
				{
					if( d_bl->type == BL_PC )
						((TBL_PC*)d_bl)->cursed_circle[sce->val2] = 0;
				}
			}
			break;

		case SC_BLOOD_SUCKER:
			{
				struct block_list *d_bl = map_id2bl(sce->val1);
				if( d_bl )
				{
					if( d_bl->type == BL_PC )
						((TBL_PC*)d_bl)->blood_sucker[sce->val2] = 0;
				}
			}
			break;

		case SC_C_MARKER:
			{
				struct block_list *d_bl = map_id2bl(sce->val1);
				if( d_bl )
				{
					if( d_bl->type == BL_PC )
						((TBL_PC*)d_bl)->crimson_mark[sce->val2] = 0;
				}
			}
			break;

		case SC_H_MINE:
			{
				struct block_list *d_bl = map_id2bl(sce->val1);
				if( d_bl )
				{
					if( d_bl->type == BL_PC )
						((TBL_PC*)d_bl)->howl_mine[sce->val2] = 0;
				}
			}
			break;

		case SC_FLASHKICK:
			{
				struct block_list *d_bl = map_id2bl(sce->val1);
				if( d_bl )
				{
					if( d_bl->type == BL_PC )
						((TBL_PC*)d_bl)->stellar_mark[sce->val2] = 0;
				}
			}
			break;

		case SC_SOULUNITY:
			{
				struct block_list *d_bl = map_id2bl(sce->val1);
				if( d_bl )
				{
					if( d_bl->type == BL_PC )
						((TBL_PC*)d_bl)->united_soul[sce->val2] = 0;
				}
			}
			break;

		case SC_BLADESTOP:
			if(sce->val4)
			{
				int tid = sce->val4;
				struct block_list *tbl = map_id2bl(tid);
				struct status_change *tsc = status_get_sc(tbl);
				sce->val4 = 0;
				if(tbl && tsc && tsc->data[SC_BLADESTOP])
				{
					tsc->data[SC_BLADESTOP]->val4 = 0;
					status_change_end(tbl, SC_BLADESTOP, INVALID_TIMER);
				}
				clif_bladestop(bl, tid, 0);
			}
			break;
		case SC_DANCING:
			{
				struct map_session_data *dsd;
				struct status_change_entry *dsc;

				if(sce->val4 && sce->val4 != BCT_SELF && (dsd=map_id2sd(sce->val4)))
				{// end status on partner as well
					dsc = dsd->sc.data[SC_DANCING];
					if(dsc)
					{	//This will prevent recursive loops. 
						dsc->val2 = dsc->val4 = 0;
						status_change_end(&dsd->bl, SC_DANCING, INVALID_TIMER);
					}
				}

				if(sce->val2)
				{// erase associated land skill
					struct skill_unit_group *group;
					group = skill_id2group(sce->val2);

					sce->val2 = 0;
					if (group)
						skill_delunitgroup(group);
				}

				if((sce->val1&0xFFFF) == CG_MOONLIT)
					clif_status_change(bl,SI_MOONLIT,0,0, 0, 0, 0);

				status_change_end(bl, SC_LONGING, INVALID_TIMER);
			}
			break;
		case SC_NOCHAT:
			if (sd && sd->status.manner < 0 && tid != INVALID_TIMER)
				sd->status.manner = 0;
			if (sd && tid == INVALID_TIMER)
			{
				clif_changestatus(sd, SP_MANNER, sd->status.manner);
				clif_updatestatus(sd,SP_MANNER);
			}
			break;
		case SC_SPLASHER:	
			{
				struct block_list *src=map_id2bl(sce->val3);
				if(src && tid != INVALID_TIMER)
					skill_castend_damage_id(src, bl, sce->val2, sce->val1, gettick(), SD_LEVEL );
			}
			break;
		case SC_CLOSECONFINE2:
			{
				struct block_list *src = sce->val2?map_id2bl(sce->val2):NULL;
				struct status_change *sc2 = src?status_get_sc(src):NULL;
				if (src && sc2 && sc2->data[SC_CLOSECONFINE]) {
					//If status was already ended, do nothing.
					//Decrease count
					if (--(sc2->data[SC_CLOSECONFINE]->val1) <= 0) //No more holds, free him up.
						status_change_end(src, SC_CLOSECONFINE, INVALID_TIMER);
				}
			}
		case SC_CLOSECONFINE:
			if (sce->val2 > 0) {
				//Caster has been unlocked... nearby chars need to be unlocked.
				int range = 1
					+ skill_get_range2(bl, status_sc2skill(type), sce->val1, true)
					+ skill_get_range2(bl, TF_BACKSLIDING, 1, true); // Since most people use this to escape the hold....
				map_foreachinarea(status_change_timer_sub, 
					bl->m, bl->x-range, bl->y-range, bl->x+range,bl->y+range,BL_CHAR,bl,sce,type,gettick());
			}
			break;
		case SC_COMBO:
			skill_combo_toggle_inf(bl, sce->val1, 0);
			break;

		case SC_MARIONETTE:
		case SC_MARIONETTE2:	/// Marionette target
			if (sce->val1)
			{	// check for partner and end their marionette status as well
				enum sc_type type2 = (type == SC_MARIONETTE) ? SC_MARIONETTE2 : SC_MARIONETTE;
				struct block_list *pbl = map_id2bl(sce->val1);
				struct status_change* sc2 = pbl?status_get_sc(pbl):NULL;
				
				if (sc2 && sc2->data[type2])
				{
					sc2->data[type2]->val1 = 0;
					status_change_end(pbl, type2, INVALID_TIMER);
				}
			}
			break;

		case SC_CONCENTRATION:
			status_change_end(bl, SC_ENDURE, INVALID_TIMER);
			break;

		case SC_BERSERK:
			//If val2 is removed, no HP penalty (dispelled?) [Skotlex]
			if(status->hp > 100 && sce->val2)
				status_set_hp(bl, 100, 0); 
			if(sc->data[SC_ENDURE] && sc->data[SC_ENDURE]->val4 == 2)
			{
				sc->data[SC_ENDURE]->val4 = 0;
				status_change_end(bl, SC_ENDURE, INVALID_TIMER);
			}
			sc_start4(bl, SC_REGENERATION, 100, 10,0,0,(RGN_HP|RGN_SP), skill_get_time(LK_BERSERK, sce->val1));
			break;
		case SC_GOSPEL:
			if (sce->val3) { //Clear the group.
				struct skill_unit_group* group = skill_id2group(sce->val3);
				sce->val3 = 0;
				if (group)
					skill_delunitgroup(group);
			}
			break;
		case SC_HERMODE: 
			if(sce->val3 == BCT_SELF)
				skill_clear_unitgroup(bl);
			break;
		case SC_BASILICA: //Clear the skill area. [Skotlex]
			if (sce->val3 && sce->val4 == bl->id) {
				struct skill_unit_group* group = skill_id2group(sce->val3);
				sce->val3 = 0;
				if (group)
					skill_delunitgroup(group);
			}
				break;
		case SC_TRICKDEAD:
			if (vd) vd->dead_sit = 0;
			break;
		case SC_WARM:
		case SC_BANDING:
		case SC__MANHOLE:
			if (sce->val4) 
			{ //Clear the group.
				struct skill_unit_group* group = skill_id2group(sce->val4);
				sce->val4 = 0;
				skill_delunitgroup(group);
			}
			break;
		case SC_JAILED:
			if(tid == INVALID_TIMER)
				break;
		  	//natural expiration.
			if(sd && sd->mapindex == sce->val2)
				pc_setpos(sd,(unsigned short)sce->val3,sce->val4&0xFFFF, sce->val4>>16, CLR_TELEPORT);
			break; //guess hes not in jail :P
		case SC_CHANGE:
			if (tid == INVALID_TIMER)
		 		break;
			// "lose almost all their HP and SP" on natural expiration.
			status_set_hp(bl, 10, 0);
			status_set_sp(bl, 10, 0);
			break;
		case SC_AUTOTRADE:
			if (tid == INVALID_TIMER)
				break;
			// Note: vending/buying is closed by unit_remove_map, no
			// need to do it here.
			if (sd) {
				map_quit(sd);
				// Because map_quit calls status_change_end with tid -1
				// from here it's not neccesary to continue
				return 1;
			}
			break;
		case SC_READING_SB:
			if( sd ) memset(sd->rsb,0,sizeof(sd->rsb)); // Clear all Spell Books
 			break;
		case SC_STOP:
			if( sce->val2 )
			{
				struct block_list* tbl = map_id2bl(sce->val2);
				sce->val2 = 0;
				if( tbl && (sc = status_get_sc(tbl)) && sc->data[SC_STOP] && sc->data[SC_STOP]->val2 == bl->id )
					status_change_end(tbl, SC_STOP, INVALID_TIMER);
			}
			break;
		case SC_MONSTER_TRANSFORM:
		case SC_ACTIVE_MONSTER_TRANSFORM:
			if (sce->val2)
				status_change_end(bl, (sc_type)sce->val2, INVALID_TIMER);
			break;
		case SC_HALLUCINATIONWALK:
			sc_start(bl,SC_HALLUCINATIONWALK_POSTDELAY,100,sce->val1,skill_get_time2(GC_HALLUCINATIONWALK,sce->val1));
			break;
		case SC_IMPRISON:
			if( tid == -1 )
				break; // Terminated by Damage
			clif_damage(bl, bl, gettick(), 0, 0, 400 * sce->val1, 0, 0, 0, false);
			status_fix_damage(bl, bl, 400 * sce->val1, 0);
			break;
		case SC_WUGDASH:
			{
				struct unit_data *ud = unit_bl2ud(bl);
				if (ud) {
					ud->state.running = 0;
					if (ud->walktimer != -1)
						unit_stop_walking(bl,1);
				}
			}
			break;
		case SC_NEUTRALBARRIER_MASTER:
		case SC_STEALTHFIELD_MASTER:
			if (sce->val2)
			{
				struct skill_unit_group* group = skill_id2group(sce->val2);
				sce->val2 = 0;
				skill_delunitgroup(group);
			}
			break;
		case SC__SHADOWFORM:
			{
				struct map_session_data *s_sd = map_id2sd(sce->val2);
				if( !s_sd )
					break;
				s_sd->shadowform_id = 0;
			}
			break;
		case SC__BLOODYLUST_BK:
			if ( sc->data[SC_BERSERK] )
			{// Remove HP penalty before ending the status.
				sc->data[SC_BERSERK]->val2 = 0;
				status_change_end(bl, SC_BERSERK, INVALID_TIMER);
			}
			break;
		case SC_CURSEDCIRCLE_ATKER:
			{
				short k = 0;
				for(k = 0; k < MAX_CURSED_CIRCLES; k++)
				if (sd->cursed_circle[k]){
					struct map_session_data *ccirclesd = map_id2sd(sd->cursed_circle[k]);
					struct mob_data *ccirclemd = map_id2md(sd->cursed_circle[k]);
					if (ccirclesd)
						status_change_end(&ccirclesd->bl, SC_CURSEDCIRCLE_TARGET, INVALID_TIMER);
					if (ccirclemd)
						status_change_end(&ccirclemd->bl, SC_CURSEDCIRCLE_TARGET, INVALID_TIMER);
					sd->cursed_circle[k] = 0;
				}
			}
			break;
		case SC_RAISINGDRAGON:
			if (sd && sce->val2 && !pc_isdead(sd))
			{
				int i = min(sd->spiritball, 5);
				pc_delspiritball(sd, sd->spiritball, 0);
				status_change_end(bl, SC_EXPLOSIONSPIRITS, INVALID_TIMER);
				while (i > 0)
				{
					pc_addspiritball(sd, skill_get_time(MO_CALLSPIRITS, pc_checkskill(sd,MO_CALLSPIRITS)), 5);
					--i;
				}
			}
			break;
		case SC_GENTLETOUCH_REVITALIZE:
			if ( sd && pc_checkskill(sd,SM_MOVINGRECOVERY) < 1)
				sd->regen.state.walk = 0;
			break;
		case SC_SATURDAY_NIGHT_FEVER:
			sc_start(bl,SC_SITDOWN_FORCE,100,sce->val1,skill_get_time2(WM_SATURDAY_NIGHT_FEVER,sce->val1));
			break;
		case SC_SITDOWN_FORCE:
		case SC_BANANA_BOMB_SITDOWN_POSTDELAY:
			if (sd && pc_issit(sd) && pc_setstand(sd, false))
				skill_sit(sd, false);
			break;
		case SC_SUNSTANCE:
			status_change_end(bl, SC_LIGHTOFSUN, INVALID_TIMER);
			break;
		case SC_LUNARSTANCE:
			status_change_end(bl, SC_NEWMOON, INVALID_TIMER);
			status_change_end(bl, SC_LIGHTOFMOON, INVALID_TIMER);
			break;
		case SC_STARSTANCE:
			status_change_end(bl, SC_FALLINGSTAR, INVALID_TIMER);
			status_change_end(bl, SC_LIGHTOFSTAR, INVALID_TIMER);
			break;
		case SC_UNIVERSESTANCE:
			status_change_end(bl, SC_LIGHTOFSUN, INVALID_TIMER);
			status_change_end(bl, SC_NEWMOON, INVALID_TIMER);
			status_change_end(bl, SC_LIGHTOFMOON, INVALID_TIMER);
			status_change_end(bl, SC_FALLINGSTAR, INVALID_TIMER);
			status_change_end(bl, SC_LIGHTOFSTAR, INVALID_TIMER);
			status_change_end(bl, SC_DIMENSION, INVALID_TIMER);
			break;
		case SC_GRAVITYCONTROL:
			clif_damage(bl,bl,gettick(),0,0,sce->val2,0,0,0,false);
			status_fix_damage(bl,bl,sce->val2,0);
			clif_specialeffect(bl, 223, AREA);
			clif_specialeffect(bl, 330, AREA);
			break;
		case SC_OVERED_BOOST:
			if (hd)
			{// Homunculus hunger is reduced by 50% of max hunger.
				short hunger = hd->homunculus.hunger - 50;
				if (hunger < 1)// Hunger isnt reduced below 1.
					hunger = 1;
				hd->homunculus.hunger = hunger;
			}
			else if (sd)// Master's SP is reduced by 50% of MaxSP
				status_zap(bl,0,status->max_sp * 50 / 100);
			break;
		case SC_FULL_THROTTLE:
			sc_start(bl,SC_REBOUND,100,sce->val1,skill_get_time2(ALL_FULL_THROTTLE, sce->val1));
			break;
		case SC_MIDNIGHT_FRENZY_POSTDELAY:
			clif_hom_skillupdateinfo(hd->master, MH_SONIC_CRAW, INF_ATTACK_SKILL, 1);
			break;
		//case SC_HEAT_BARREL:
		//	sc_start(bl,SC_HEAT_BARREL_AFTER,100,sce->val1,skill_get_time2(RL_HEAT_BARREL, sce->val1));
		//	break;
		case SC_GRANITIC_ARMOR:
			status_percent_damage(NULL, bl, -sce->val3, 0, false);
			break;
		case SC_PYROCLASTIC:
			if ( sd && battle_config.pyroclastic_breaks_weapon == 1 )
				skill_break_equip(bl, EQP_WEAPON, 10000, BCT_SELF);
			break;
		case SC_EL_PASSIVE:
			status_change_end(bl, SC_PYROTECHNIC, INVALID_TIMER);
			status_change_end(bl, SC_HEATER, INVALID_TIMER);
			status_change_end(bl, SC_TROPIC, INVALID_TIMER);
			status_change_end(bl, SC_AQUAPLAY, INVALID_TIMER);
			status_change_end(bl, SC_COOLER, INVALID_TIMER);
			status_change_end(bl, SC_CHILLY_AIR, INVALID_TIMER);
			status_change_end(bl, SC_GUST, INVALID_TIMER);
			status_change_end(bl, SC_BLAST, INVALID_TIMER);
			status_change_end(bl, SC_WILD_STORM, INVALID_TIMER);
			status_change_end(bl, SC_PETROLOGY, INVALID_TIMER);
			status_change_end(bl, SC_CURSED_SOIL, INVALID_TIMER);
			status_change_end(bl, SC_UPHEAVAL, INVALID_TIMER);
			if ( ed->state.alive == 1 )
				sc_start(bl, SC_EL_WAIT, 100, 1, 0);
			break;
		case SC_EL_DEFENSIVE:
			status_change_end(bl, SC_CIRCLE_OF_FIRE, INVALID_TIMER);
			status_change_end(bl, SC_FIRE_CLOAK, INVALID_TIMER);
			status_change_end(bl, SC_WATER_SCREEN, INVALID_TIMER);
			status_change_end(bl, SC_WATER_DROP, INVALID_TIMER);
			status_change_end(bl, SC_WIND_STEP, INVALID_TIMER);
			status_change_end(bl, SC_WIND_CURTAIN, INVALID_TIMER);
			status_change_end(bl, SC_SOLID_SKIN, INVALID_TIMER);
			status_change_end(bl, SC_STONE_SHIELD, INVALID_TIMER);
			if ( battle_config.elem_defensive_support == 1 )
				status_change_end(bl, SC_MODECHANGE, INVALID_TIMER);
			if ( ed->state.alive == 1 )
				sc_start(bl, SC_EL_WAIT, 100, 1, 0);
			break;
		case SC_EL_OFFENSIVE:
			status_change_end(bl, SC_MODECHANGE, INVALID_TIMER);
			if ( ed->state.alive == 1 )
				sc_start(bl, SC_EL_WAIT, 100, 1, 0);
			break;
		case SC_CIRCLE_OF_FIRE:
		case SC_FIRE_CLOAK:
		case SC_WATER_SCREEN:
		case SC_WATER_DROP:
		case SC_WIND_STEP:
		case SC_WIND_CURTAIN:
		case SC_SOLID_SKIN:
		case SC_STONE_SHIELD:
		case SC_PYROTECHNIC:
		case SC_HEATER:
		case SC_TROPIC:
		case SC_AQUAPLAY:
		case SC_COOLER:
		case SC_CHILLY_AIR:
		case SC_GUST:
		case SC_BLAST:
		case SC_WILD_STORM:
		case SC_PETROLOGY:
		case SC_CURSED_SOIL:
		case SC_UPHEAVAL:
		case SC_TIDAL_WEAPON:
			if (ed && type == SC_WATER_SCREEN)
				ed->water_screen_flag = 0;
			status_change_end(&ed->master->bl, type+1, INVALID_TIMER);
			break;
		case SC_SWORDCLAN:
		case SC_ARCWANDCLAN:
		case SC_GOLDENMACECLAN:
		case SC_CROSSBOWCLAN:
		case SC_JUMPINGCLAN:
			status_change_end(bl, SC_CLAN_INFO, INVALID_TIMER);
			break;
		case SC_ITEMSCRIPT: // Removes Buff Icons
			if (sd && sce->val2 != SI_BLANK)
				clif_status_load(bl, (enum si_type)sce->val2, 0);
			break;
		}


	opt_flag = 1;
	switch(type)
	{
		case SC_STONE:
		case SC_FREEZE:
		case SC_STUN:
		case SC_SLEEP:
		case SC_BURNING:
		case SC_IMPRISON:
			sc->opt1 = 0;
			break;

		case SC_POISON:
		case SC_CURSE:
		case SC_SILENCE:
		case SC_BLIND:
			sc->opt2 &= ~(1<<(type-SC_POISON));
			break;
		case SC_DPOISON:
			sc->opt2 &= ~OPT2_DPOISON;
			break;
		case SC_FEAR:
			sc->opt2 &= ~OPT2_FEAR;
			break;
		case SC_SIGNUMCRUCIS:
		case SC_CONFUSION:
			sc->opt2 &= ~OPT2_SIGNUMCRUCIS;
			break;

		case SC_HIDING:
			sc->option &= ~OPTION_HIDE;
			opt_flag|= 2|4; //Check for warp trigger + AoE trigger
			break;
		case SC_CLOAKING:
		case SC_CLOAKINGEXCEED:
		case SC__INVISIBILITY:
		case SC_NEWMOON:
			sc->option &= ~OPTION_CLOAK;
			opt_flag|= 2;
			break;
		case SC_CHASEWALK:
			sc->option &= ~(OPTION_CHASEWALK|OPTION_CLOAK);
			opt_flag|= 2;
			break;
		case SC_SIGHT:
			sc->option &= ~OPTION_SIGHT;
			break;
		case SC_WEDDING:	
			sc->option &= ~OPTION_WEDDING;
			opt_flag |= 0x4;
			break;
		case SC_XMAS:	
			sc->option &= ~OPTION_XMAS;
			opt_flag |= 0x4;
			break;
		case SC_SUMMER:
			sc->option &= ~OPTION_SUMMER;
			opt_flag |= 0x4;
			break;
		case SC_HANBOK:
			sc->option &= ~OPTION_HANBOK;
			opt_flag |= 0x4;
			break;
		case SC_OKTOBERFEST:
			sc->option &= ~OPTION_OKTOBERFEST;
			opt_flag |= 0x4;
			break;
		case SC_SUMMER2:
			sc->option &= ~OPTION_SUMMER2;
			opt_flag |= 0x4;
			break;
		case SC_ORCISH:
			sc->option &= ~OPTION_ORCISH;
			break;
		case SC_RUWACH:
			sc->option &= ~OPTION_RUWACH;
			break;
		case SC_FUSION:
			sc->option &= ~OPTION_FLYING;
			break;
		//opt3
		case SC_TWOHANDQUICKEN:
		case SC_ONEHAND:
		case SC_SPEARQUICKEN:
		case SC_CONCENTRATION:
		case SC_MERC_QUICKEN:
			sc->opt3 &= ~OPT3_QUICKEN;
			opt_flag = 0;
			break;
		case SC_OVERTHRUST:
		case SC_MAXOVERTHRUST:
		case SC_SWOO:
			sc->opt3 &= ~OPT3_OVERTHRUST;
			if (type == SC_SWOO)
				opt_flag = 8;
			else
				opt_flag = 0;
			break;
		case SC_ENERGYCOAT:
		case SC_SKE:
			sc->opt3 &= ~OPT3_ENERGYCOAT;
			opt_flag = 0;
			break;
		case SC_INCATKRATE: //Simulated Explosion spirits effect.
			if (bl->type != BL_MOB)
			{
				opt_flag = 0;
				break;
			}
		case SC_EXPLOSIONSPIRITS:
			sc->opt3 &= ~OPT3_EXPLOSIONSPIRITS;
			opt_flag = 0;
			break;
		case SC_STEELBODY:
		case SC_SKA:
			sc->opt3 &= ~OPT3_STEELBODY;
			if (type == SC_SKA)
				opt_flag = 8;
			else
				opt_flag = 0;
			break;
		case SC_BLADESTOP:
			sc->opt3 &= ~OPT3_BLADESTOP;
			opt_flag = 0;
			break;
		case SC_AURABLADE:
			sc->opt3 &= ~OPT3_AURABLADE;
			opt_flag = 0;
			break;
		case SC_BERSERK:
			sc->opt3 &= ~OPT3_BERSERK;
			opt_flag = 0;
			break;
	//	case ???: // doesn't seem to do anything
	//		sc->opt3 &= ~OPT3_LIGHTBLADE;
	//		opt_flag = 0;
	//		break;
		case SC_DANCING:
			if ((sce->val1&0xFFFF) == CG_MOONLIT)
				sc->opt3 &= ~OPT3_MOONLIT;
			opt_flag = 0;
			break;
		case SC_MARIONETTE:
		case SC_MARIONETTE2:
			sc->opt3 &= ~OPT3_MARIONETTE;
			opt_flag = 0;
			break;
		case SC_ASSUMPTIO:
			sc->opt3 &= ~OPT3_ASSUMPTIO;
			opt_flag = 0;
			break;
		case SC_WARM: //SG skills [Komurka]
			sc->opt3 &= ~OPT3_WARM;
			opt_flag = 0;
			break;
		case SC_KAITE:
			sc->opt3 &= ~OPT3_KAITE;
			opt_flag = 0;
			break;
		case SC_BUNSINJYUTSU:
			sc->opt3 &= ~OPT3_BUNSIN;
			opt_flag = 0;
			break;
		case SC_SPIRIT:
			sc->opt3 &= ~OPT3_SOULLINK;
			opt_flag = 0;
			break;
		case SC_CHANGEUNDEAD:
			sc->opt3 &= ~OPT3_UNDEAD;
			opt_flag = 0;
			break;
	//	case ???: // from DA_CONTRACT (looks like biolab mobs aura)
	//		sc->opt3 &= ~OPT3_CONTRACT;
	//		opt_flag = 0;
	//		break;
		default:
			opt_flag = 0;
	}

	if (calc_flag&SCB_DYE)
	{	//Restore DYE color
		if (vd && !vd->cloth_color && sce->val4)
			clif_changelook(bl,LOOK_CLOTHES_COLOR,sce->val4);
		calc_flag&=~SCB_DYE;
	}

	/*if (calc_flag&SCB_BODY)// Might be needed in the future. [Rytech]
	{	//Restore body style
		if (vd && !vd->body_style && sce->val4)
			clif_changelook(bl,LOOK_BODY2,sce->val4);
		calc_flag&=~SCB_BODY;
	}*/

	//On Aegis, when turning off a status change, first goes the sc packet, then the option packet.
	clif_status_change(bl, StatusIconChangeTable[type], 0, 0, 0, 0, 0);

	if (opt_flag&8)
		clif_changeoption2(bl);
	else if (opt_flag)
	{
		clif_changeoption(bl);
 		if (sd && (opt_flag&0x4)) {
 			clif_changelook(bl,LOOK_BASE,sd->vd.class_);
 			clif_get_weapon_view(sd,&sd->vd.weapon,&sd->vd.shield);
 			clif_changelook(bl,LOOK_WEAPON,sd->vd.weapon);
 			clif_changelook(bl,LOOK_SHIELD,sd->vd.shield);
 			clif_changelook(bl,LOOK_CLOTHES_COLOR,cap_value(sd->status.clothes_color,0,battle_config.max_cloth_color));
			clif_changelook(bl,LOOK_BODY2,cap_value(sd->status.body,0,battle_config.max_body_style));
		}
	}

	if (calc_flag) {
		switch (type) {
		case SC_MAGICPOWER:
			//If Mystical Amplification ends, MATK is immediately recalculated
			status_calc_bl_(bl, calc_flag, SCO_FORCE);
			break;
		default:
			status_calc_bl(bl, calc_flag);
			break;
		}
	}

	if(opt_flag&4) //Out of hiding, invoke on place.
		skill_unit_move(bl,gettick(),1);

	if(opt_flag&2 && sd && map_getcell(bl->m,bl->x,bl->y,CELL_CHKNPC))
		npc_touch_areanpc(sd,bl->m,bl->x,bl->y); //Trigger on-touch event.

	ers_free(sc_data_ers, sce);
	return 1;
}

/*==========================================
 * ステータス異常終了タイマー
 *------------------------------------------*/
int status_change_timer(int tid, int64 tick, int id, intptr_t data)
{
	enum sc_type type = (sc_type)data;
	struct block_list *bl;
	struct map_session_data *sd;
	struct elemental_data *ed;
	struct status_data *status;
	struct status_change *sc;
	struct status_change_entry *sce;
	int interval = status_get_sc_interval(type);
	bool dounlock = false;

	bl = map_id2bl(id);
	if(!bl)
	{
		ShowDebug("status_change_timer: Null pointer id: %d data: %d\n", id, data);
		return 0;
	}
	sc = status_get_sc(bl);
	status = status_get_status_data(bl);
	
	if(!(sc && (sce = sc->data[type])))
	{
		ShowDebug("status_change_timer: Null pointer id: %d data: %d bl-type: %d\n", id, data, bl->type);
		return 0;
	}

	if( sce->timer != tid )
	{
		ShowError("status_change_timer: Mismatch for type %d: %d != %d (bl id %d)\n",type,tid,sce->timer, bl->id);
		return 0;
	}

	sce->timer = INVALID_TIMER;

	sd = BL_CAST(BL_PC, bl);
	ed = BL_CAST(BL_ELEM, bl);

// set the next timer of the sce (don't assume the status still exists)
#define sc_timer_next(t,f,i,d) \
	if( (sce=sc->data[type]) ) \
		sce->timer = add_timer(t,f,i,d); \
	else \
		ShowError("status_change_timer: Unexpected NULL status change id: %d data: %d\n", id, data)

	switch(type)
	{
	case SC_MAXIMIZEPOWER:
	case SC_CLOAKING:
		if(!status_charge(bl, 0, 1))
			break; //Not enough SP to continue.
		sc_timer_next(sce->val2+tick, status_change_timer, bl->id, data);
		return 0;

	case SC_CHASEWALK:
		if(!status_charge(bl, 0, sce->val4))
			break; //Not enough SP to continue.
			
		if (!sc->data[SC_INCSTR]) {
			sc_start(bl, SC_INCSTR,100,1<<(sce->val1-1),
				(sc->data[SC_SPIRIT] && sc->data[SC_SPIRIT]->val2 == SL_ROGUE?10:1) //SL bonus -> x10 duration
				*skill_get_time2(status_sc2skill(type),sce->val1));
		}
		sc_timer_next(sce->val2+tick, status_change_timer, bl->id, data);
		return 0;

	case SC_HIDING:
		if(--(sce->val2)>0){
			
			if(sce->val2 % sce->val4 == 0 && !status_charge(bl, 0, 1))
				break; //Fail if it's time to substract SP and there isn't.
		
			sc_timer_next(1000+tick, status_change_timer,bl->id, data);
			return 0;
		}
	break;

	case SC_SIGHT:
	case SC_RUWACH:
	case SC_SIGHTBLASTER:
		if (type == SC_SIGHTBLASTER) {
			//Restore trap immunity
			if (sce->val4 % 2)
				sce->val4--;
			map_foreachinrange(status_change_timer_sub, bl, sce->val3, BL_CHAR | BL_SKILL, bl, sce, type, tick);
		}
		else {
			map_foreachinrange(status_change_timer_sub, bl, sce->val3, BL_CHAR, bl, sce, type, tick);
			skill_reveal_trap_inarea(bl, sce->val3, bl->x, bl->y);
		}

		if( --(sce->val2)>0 ){
			sce->val4 += 20; // Use for Shadow Form 2 seconds checking.
			sc_timer_next(20 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;
		
	case SC_PROVOKE:
		if(sce->val2) { //Auto-provoke (it is ended in status_heal)
			sc_timer_next(1000*60+tick,status_change_timer, bl->id, data );
			return 0;
		}
		break;

	case SC_STONE:
		if (sc->opt1 == OPT1_STONEWAIT && sce->val4) {
			sce->val3 = 0; //Incubation time used up
			unit_stop_attack(bl);
			if (sc->data[SC_DANCING]) {
				unit_stop_walking(bl, 1);
				status_change_end(bl, SC_DANCING, INVALID_TIMER);
			}
			status_change_end(bl, SC_AETERNA, INVALID_TIMER);
			sc->opt1 = OPT1_STONE;
			clif_changeoption(bl);
			sc_timer_next(min(sce->val4, interval) + tick, status_change_timer, bl->id, data);
			sce->val4 -= interval; //Remaining time
			status_calc_bl(bl, StatusChangeFlagTable[type]);
			return 0;
		}
		if (sce->val4 >= 0 && !(sce->val3) && status->hp > status->max_hp / 4) {
			status_percent_damage(NULL, bl, 1, 0, false);
		}
		break;

	case SC_POISON:
	case SC_DPOISON:
		if (sce->val4 >= 0 && !sc->data[SC_SLOWPOISON]) {
			int64 damage = 0;
			if (sd)
				damage = (type == SC_DPOISON) ? 2 + status->max_hp / 50 : 2 + status->max_hp * 3 / 200;
			else
				damage = (type == SC_DPOISON) ? 2 + status->max_hp / 100 : 2 + status->max_hp / 200;
			if (status->hp > max(status->max_hp / 4, damage)) // Stop damaging after 25% HP left.
				status_zap(bl, damage, 0);
		}
		break;
	case SC_BLEEDING:
		if (sce->val4 >= 0) {
			int64 damage = rnd() % 600 + 200;
			if (!sd && damage >= status->hp)
				damage = status->hp - 1; // No deadly damage for monsters
			map_freeblock_lock();
			dounlock = true;
			status_zap(bl, damage, 0);
		}
		break;
	case SC_BURNING:
		if (sce->val4 >= 0) {
			int64 damage = 1000 + (3 * status->max_hp) / 100; // Deals fixed (1000 + 3%*MaxHP)
			map_freeblock_lock();
			dounlock = true;
			status_fix_damage(bl, bl, damage, clif_damage(bl, bl, tick, 0, 1, damage, 1, DMG_NORMAL, 0, false));
		}
		break;
	case SC_TOXIN:
		if (sce->val4 >= 0) { // Damage is every 10 seconds including 3%sp drain.
			map_freeblock_lock();
			dounlock = true;
			status_damage(bl, bl, 1, status->max_sp * 3 / 100, clif_damage(bl, bl, tick, status->amotion, status->dmotion + 500, 1, 1, DMG_NORMAL, 0, false), 0);
		}
		break;
	case SC_MAGICMUSHROOM:
		if (sce->val4 >= 0) {
			bool flag = 0;
			int64 damage = status->max_hp * 3 / 100;
			if (status->hp <= damage)
				damage = status->hp - 1; // Cannot Kill
			if (damage > 0) { // 3% Damage each 4 seconds
				map_freeblock_lock();
				status_zap(bl, damage, 0);
				flag = !sc->data[type]; // Killed? Should not
				map_freeblock_unlock();
			}
			if (!flag)
			{ // Random Skill Cast
				map_freeblock_lock();

				if (sd && !pc_issit(sd)) //can't cast if sit
				{
					int mushroom_skillid = 0;
					unit_stop_attack(bl);
					unit_skillcastcancel(bl, 1);
					do
					{
						int i = rnd() % MAX_SKILL_MAGICMUSHROOM_DB;
						mushroom_skillid = skill_magicmushroom_db[i].skill_id;
					} while (mushroom_skillid == 0);
					switch (skill_get_casttype(mushroom_skillid)) { // Magic Mushroom skills are buffs or area damage
					case CAST_GROUND:
						skill_castend_pos2(bl, bl->x, bl->y, mushroom_skillid, 1, tick, 0);
						break;
					case CAST_NODAMAGE:
						skill_castend_nodamage_id(bl, bl, mushroom_skillid, 1, tick, 0);
						break;
					case CAST_DAMAGE:
						skill_castend_damage_id(bl, bl, mushroom_skillid, 1, tick, 0);
						break;
					}
				}
				clif_emotion(bl, E_HEH);
				if (sc->data[type])
					sc_timer_next(4000 + tick, status_change_timer, bl->id, data);

				map_freeblock_unlock();
			}
		}
		break;
	case SC_PYREXIA:
		if (sce->val4 >= 0) {
			map_freeblock_lock();
			dounlock = true;
			status_fix_damage(bl, bl, 100, clif_damage(bl, bl, tick, status->amotion, status->dmotion + 500, 100, 1, DMG_NORMAL, 0, false));
		}
		break;

	case SC_TENSIONRELAX:
		if(status->max_hp > status->hp && --(sce->val3) > 0){
			sc_timer_next(sce->val4+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_KNOWLEDGE:
		if (!sd) break;
		if(bl->m == sd->feel_map[0].m ||
			bl->m == sd->feel_map[1].m ||
			bl->m == sd->feel_map[2].m)
		{	//Timeout will be handled by pc_setpos
			sce->timer = INVALID_TIMER;
			return 0;
		}
		break;

	case SC_S_LIFEPOTION:
	case SC_L_LIFEPOTION:
		if( sd && --(sce->val4) >= 0 )
		{
			// val1 < 0 = per max% | val1 > 0 = exact amount
			int hp = 0;
			if( status->hp < status->max_hp )
				hp = (sce->val1 < 0) ? (int)(sd->status.max_hp * -1 * sce->val1 / 100.) : sce->val1 ;
			status_heal(bl, hp, 0, 2);
			sc_timer_next((sce->val2 * 1000) + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_BOSSMAPINFO:
		if( sd && --(sce->val4) >= 0 )
		{
			struct mob_data *boss_md = map_id2boss(sce->val1);
			if( boss_md && sd->bl.m == boss_md->bl.m )
			{
				clif_bossmapinfo(sd->fd, boss_md, 1); // Update X - Y on minimap
				if (boss_md->bl.prev != NULL) {
					sc_timer_next(5000 + tick, status_change_timer, bl->id, data);
					return 0;
				}
			}
		}
		break;

	case SC_DANCING: //ダンススキルの時間SP消費
		{
			int s = 0;
			int sp = 1;
			if (--sce->val3 <= 0)
				break;
			switch(sce->val1&0xFFFF){
				case BD_RICHMANKIM:
				case BD_DRUMBATTLEFIELD:
				case BD_RINGNIBELUNGEN:
				case BD_SIEGFRIED:
				case BA_DISSONANCE:
				case BA_ASSASSINCROSS:
				case DC_UGLYDANCE:
					s=3;
					break;
				case BD_LULLABY:
				case BD_ETERNALCHAOS:
				case BD_ROKISWEIL:
				case DC_FORTUNEKISS:
					s=4;
					break;
				case CG_HERMODE:
				case BD_INTOABYSS:
				case BA_WHISTLE:
				case DC_HUMMING:
				case BA_POEMBRAGI:
				case DC_SERVICEFORYOU:
					s=5;
					break;
				case BA_APPLEIDUN:
					s=6;
					break;
				case CG_MOONLIT:
					//Moonlit's cost is 4sp*skill_lv [Skotlex]
					sp= 4*(sce->val1>>16);
					//Upkeep is also every 10 secs.
				case DC_DONTFORGETME:
					s=10;
					break;
			}
			if( s != 0 && sce->val3 % s == 0 )
			{
				if (sc->data[SC_LONGING])
					sp*= 3;
				if (!status_charge(bl, 0, sp))
					break;
			}
			sc_timer_next(1000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	// Needed for updating the enemy's position on the mini map.
	// Not up for doing this right now. Code it in later. [Rytech]
	/*case SC_C_MARKER:
		if(--(sce->val4)>0){  
			struct map_session_data *sd = map_id2sd(account_id),*sd;
			clif_crimson_marker_xy(sd);
			sc_timer_next(1000+tick, status_change_timer,bl->id, data);
			return 0;
		}
		break;*/

	case SC_BERSERK:
		// 5% every 10 seconds [DracoRPG]
		if( --( sce->val3 ) > 0 && status_charge(bl, sce->val2, 0) && status->hp > 100 )
		{
			sc_timer_next(sce->val4+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_NOCHAT:
		if(sd){
			sd->status.manner++;
			clif_changestatus(sd, SP_MANNER, sd->status.manner);
			clif_updatestatus(sd,SP_MANNER);
			if (sd->status.manner < 0)
			{	//Every 60 seconds your manner goes up by 1 until it gets back to 0.
				sc_timer_next(60000+tick, status_change_timer, bl->id, data);
				return 0;
			}
		}
		break;

	case SC_SPLASHER:
		// custom Venom Splasher countdown timer
		//if (sce->val4 % 1000 == 0) {
		//	char timer[10];
		//	snprintf (timer, 10, "%d", sce->val4/1000);
		//	clif_disp_overhead(bl, timer);
		//}
		if((sce->val4 -= 500) > 0) {
			sc_timer_next(500 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_MARIONETTE:
	case SC_MARIONETTE2:
		{
			struct block_list *pbl = map_id2bl(sce->val1);
			if( pbl && check_distance_bl(bl, pbl, 7) )
			{
				sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
				return 0;
			}
		}
		break;

	case SC_GOSPEL:
		if(sce->val4 == BCT_SELF && --(sce->val2) > 0)
		{
			int hp, sp;
			hp = (sce->val1 > 5) ? 45 : 30;
			sp = (sce->val1 > 5) ? 35 : 20;
			if(!status_charge(bl, hp, sp))
				break;
			sc_timer_next(10000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_JAILED:
		if(sce->val1 == INT_MAX || --(sce->val1) > 0)
		{
			sc_timer_next(60000+tick, status_change_timer, bl->id,data);
			return 0;
		}
		break;

	case SC_BLIND:
		if(sc->data[SC_FOGWALL]) 
		{	//Blind lasts forever while you are standing on the fog.
			sc_timer_next(5000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;
	case SC_GIANTGROWTH:
		if(--(sce->val4) > 0)
		{
			if(sce->val3 > 0)
				sce->val3 *= -1; // mark it to be consumed.
			else
				sce->val3 = 0; // Consume it.
			sc_timer_next(1000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;
	case SC_DEEPSLEEP:
		if( --(sce->val4) >= 0 )
		{// Recovers 3% of the player's MaxHP/MaxSP every 2 seconds.
			status_heal(bl, status->max_hp * 3 / 100, status->max_sp * 3 / 100, 2);
			sc_timer_next(2000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;
	case SC_CRYSTALIZE:
		if(--(sce->val4) > 0)
		{// 2% HP lost per second.
			bool flag;
			map_freeblock_lock();
			status_zap(bl, 2 * status->max_hp / 100, 0);
			flag = !sc->data[type];
			map_freeblock_unlock();
			if (flag) return 0;

			// 1% SP lost per second. Status ends if less then 1% remains.
			if(sd && !status_charge(bl, 0, status->max_sp / 100))
				break;

			sc_timer_next(1000+tick, status_change_timer,bl->id, data);
			return 0;
		}
		break;
	case SC_ABUNDANCE:
		if(--(sce->val4) > 0) {
			if( !sc->data[SC_BERSERK] )
				status_heal(bl,0,60,0);
				
			sc_timer_next(10000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_OBLIVIONCURSE:
		if( --(sce->val4) >= 0 )
		{
			clif_emotion(bl,1);
			sc_timer_next(3000 + tick, status_change_timer, bl->id, data );
			return 0;
		}
		break;

	case SC_WEAPONBLOCKING:
		if( --(sce->val4) >= 0 )
		{
			if(!status_charge(bl,0,3))
				break;
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_CLOAKINGEXCEED:
		if(!status_charge(bl, 0, 10 - sce->val1))
			break;
		sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
		return 0;

	case SC_RENOVATIO:
		if( --(sce->val4) >= 0 ) {
			map_freeblock_lock();
			if(!battle_check_undead(status->race, status->def_ele))
				status_heal(bl, status->max_hp * (sce->val1 + 4) / 100, 0, 2);
			if (sc->data[type]) {
				sc_timer_next(5000 + tick, status_change_timer, bl->id, data);
			}
			map_freeblock_unlock();
			return 0;
		}
		break;

	case SC_SOULCOLLECT:
		pc_addsoulball(sd, skill_get_time2(SP_SOULCOLLECT, sce->val1), sce->val2);
		sc_timer_next(sce->val3 + tick, status_change_timer, bl->id, data);
		return 0;

	case SC_MEIKYOUSISUI:
		if( --(sce->val4) >= 0 )
		{
			status_heal(bl, status->max_hp * (2 * sce->val1) / 100, status->max_sp * (1 * sce->val1) / 100, 2);
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_SUMMON1:
	case SC_SUMMON2:
	case SC_SUMMON3:
	case SC_SUMMON4:
	case SC_SUMMON5:
		if( --(sce->val4) >= 0 )
		{
			if( !status_charge(bl, 0, 1) )
				break;
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_READING_SB:
		if (!status_charge(bl, 0, sce->val2))
			break;
		sc_timer_next(10000 + tick, status_change_timer, bl->id, data);
		return 0;

	case SC_ELECTRICSHOCKER:
		if( --(sce->val4) >= 0 )
		{
			status_charge(bl, 0, status->max_sp / 100 * sce->val1 );
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_CAMOUFLAGE:
		if( !status_charge(bl, 0, 7 - sce->val1) )
			break;

		if( sce->val2 < 10 ) {
			sce->val2++;
			status_calc_bl(bl, SCB_WATK | SCB_DEF | SCB_CRI);
		}

		sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
		return 0;

	case SC__REPRODUCE:
		if( --(sce->val4) >= 0 )
		{
			if (!status_charge(bl, 0, 9 - (1 + sce->val1) / 2))
				break;
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC__SHADOWFORM:
		if( --(sce->val4) >= 0 )
		{
			if( !status_charge(bl, 0, 11 - sce->val1) )
				break;
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC__INVISIBILITY:
		if (--(sce->val4) >= 0)
		{
			if (!status_charge(bl, 0, status->max_sp * (12 - 2 * sce->val1) / 100))
				break;
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_STRIKING:
		if( --(sce->val4) >= 0 )
		{
			if( !status_charge(bl,0, 6 - sce->val1 ) )
				break;
			sc_timer_next( 1000 + tick, status_change_timer, bl->id, data );
			return 0;
		}
		break;
	case SC_SIREN:
		if( --( sce->val4 ) >= 0 )
		{
			clif_emotion( bl, 3 );
			sc_timer_next( 2000 + tick, status_change_timer, bl->id, data );
			return 0;
		}
		break;
	case SC_SIRCLEOFNATURE:
		if( --(sce->val4) >= 0 )
		{
			if (!status_charge(bl, 0, sce->val3))
				break;
			status_heal(bl, sce->val2, 0, 1);
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;
	case SC_SONG_OF_MANA:
		if( --(sce->val4) >= 0 )
		{
			status_heal(bl,0,sce->val3,3);
			sc_timer_next(5000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;
	case SC_SATURDAY_NIGHT_FEVER:
		if( --(sce->val3) >= 0 )
		{
			if( !status_charge(bl, status->max_hp * 1 / 100, status->max_sp * 1 / 100) )
			break;
			//sc_timer_next(sce->val2+tick, status_change_timer, bl->id, data);
			sc_timer_next(3000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;
	case SC_MELODYOFSINK:
		if( --(sce->val4) >= 0 )
		{
			status_charge(bl, 0, status->max_sp * ( 2 * sce->val1 + 2 * sce->val2 ) / 100);
			sc_timer_next(1000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;
	case SC_FORCEOFVANGUARD:
		{
			if( !status_charge(bl,0,24 - 4 * sce->val1))
				break;

			sc_timer_next(10000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_BANDING:
		if(--(sce->val3)>0){

			if(!status_charge(bl, 0, 7 - sce->val1))
				break;

			sce->val2 = skill_banding_count(sd);// Recheck around you to update the banding count.
			status_calc_bl(bl, SCB_WATK|SCB_DEF);// Update ATK/DEF bonuses after updating the banding count.

			sc_timer_next(5000+tick, status_change_timer,bl->id, data);
			return 0;
		}
		break;

	case SC_LG_REFLECTDAMAGE:
		if( --(sce->val4) > 0 ) {
			if (!status_charge(bl, 0, 20 + 10 * sce->val1))
				break;
			sc_timer_next(10000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_OVERHEAT_LIMITPOINT:
		if( --(sce->val1) > 0 ) // Cooling
			sc_timer_next(30000 + tick, status_change_timer, bl->id, data);
		break;

	case SC_OVERHEAT:
		{
			int damage = status->max_hp / 100; // Suggestion 1% each second
			if( damage >= status->hp ) damage = status->hp - 1; // Do not kill, just keep you with 1 hp minimum
			map_freeblock_lock();
			status_fix_damage(NULL,bl,damage,clif_damage(bl,bl,tick,0,0,damage,0,0,0, false));
			if (sc->data[type])
				sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			map_freeblock_unlock();
			return 0;
		}
		break;

	case SC_MAGNETICFIELD:
		{
			if( --(sce->val3) <= 0 )
				break; // Time out
			if( sce->val2 == bl->id ){
				if (!status_charge(bl, 0, 50))
					break; // No more SP status should end, and in the next second will end for the other affected players
			}else{
				struct block_list *src = map_id2bl(sce->val2);
				struct status_change *ssc;
				if( !src || (ssc = status_get_sc(src)) == NULL || !ssc->data[SC_MAGNETICFIELD] )
					break; // Source no more under Magnetic Field
			}
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
		}
		break;

	case SC_STEALTHFIELD_MASTER:
		if( --(sce->val4) >= 0 ) {
			if( !status_charge(bl,0,status->max_sp / 100) )
				break;
			sc_timer_next(2000 + 1000 * sce->val1 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_INSPIRATION:
		if(--(sce->val4) >= 0)
		{
			int hp = status->max_hp * (35 - 5 * sce->val1) / 1000;
			int sp = status->max_sp * (45 - 5 * sce->val1) / 1000;
			
			if( !status_charge(bl,hp,sp) ) 
				break;

			sc_timer_next(5000+tick,status_change_timer,bl->id, data);
			return 0;
		}
		break;

	case SC_RAISINGDRAGON:
		// 1% every 5 seconds [Jobbie]
		if( --(sce->val3)>0 && status_charge(bl, sce->val2, 0) ){
			if( !sc->data[type] ) return 0;
			sc_timer_next(5000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_WARMER:
	{
		unsigned char regen_percent = sce->val1;

		// Heater triples the heal percent.
		if (sc->data[SC_HEATER_OPTION])
			regen_percent *= 3;

		if (sc->data[SC_AKAITSUKI])
			skill_akaitsuki_damage(bl, bl, status->max_hp * regen_percent / 100, SO_WARMER, sce->val1, tick);
		else
			status_percent_heal(bl, regen_percent, 0);
		sc_timer_next(3000 + tick, status_change_timer, bl->id, data);
		return 0;
	}
	break;


	case SC_FIRE_INSIGNIA:
		if(--(sce->val4) >= 0)
		{
			if (status->def_ele == ELE_FIRE)
				status_heal(bl, status->max_hp / 100, 0, 1);
			else if (status->def_ele == ELE_EARTH)
				status_zap(bl, status->max_hp / 100, 0);
			sc_timer_next(5000+tick, status_change_timer,bl->id, data);
			return 0;
		}
		break;

	case SC_WATER_INSIGNIA:
		if(--(sce->val4) >= 0)
		{
			if (status->def_ele == ELE_WATER)
				status_heal(bl, status->max_hp / 100, 0, 1);
			else if (status->def_ele == ELE_FIRE)
				status_zap(bl, status->max_hp / 100, 0);
			sc_timer_next(5000+tick, status_change_timer,bl->id, data);
			return 0;
		}
		break;

	case SC_WIND_INSIGNIA:
		if(--(sce->val4) >= 0)
		{
			if (status->def_ele == ELE_WIND)
				status_heal(bl, status->max_hp / 100, 0, 1);
			else if (status->def_ele == ELE_WATER)
				status_zap(bl, status->max_hp / 100, 0);
			sc_timer_next(5000+tick, status_change_timer,bl->id, data);
			return 0;
		}
		break;

	case SC_EARTH_INSIGNIA:
		if(--(sce->val4) >= 0)
		{
			if (status->def_ele == ELE_EARTH)
				status_heal(bl, status->max_hp / 100, 0, 1);
			else if (status->def_ele == ELE_WIND)
				status_zap(bl, status->max_hp / 100, 0);
			sc_timer_next(5000+tick, status_change_timer,bl->id, data);
			return 0;
		}
		break;

	case SC_CREATINGSTAR:
		if( --(sce->val4) >= 0 )
		{// Needed to check who the caster is and what AoE is giving the status.
			struct block_list *star_caster = map_id2bl(sce->val2);
			struct skill_unit *star_aoe = (struct skill_unit *)map_id2bl(sce->val3);

			sc_timer_next(500+tick, status_change_timer, bl->id, data);

			// Must be placed after sc_timer_next to prevent null errors.
			skill_attack(BF_WEAPON,star_caster,&star_aoe->bl,bl,SJ_BOOKOFCREATINGSTAR,sce->val1,tick,0);
			return 0;
		}
		break;

	case SC_CURSEDCIRCLE_TARGET:
		if( --(sce->val4) >= 0 )
		{// Needed to check the caster's location for the range check.
			struct block_list *circle_src = map_id2bl(sce->val1);

			// End the status if out of range.
			if ( !check_distance_bl(bl, circle_src, (1+sce->val3)/2) )
				break;

			sc_timer_next(1000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_BLOOD_SUCKER:
		if( --(sce->val4) >= 0 )
		{
			int64 healing = 0;
			struct block_list *sucker_src = map_id2bl(sce->val1);

			// End the status if out of range.
			if ( !check_distance_bl(bl, sucker_src, 12) )
				break;

			map_freeblock_lock();
			healing = skill_attack(BF_MISC, sucker_src, sucker_src, bl, GN_BLOOD_SUCKER, sce->val3, tick, SD_LEVEL|SD_ANIMATION);
			status_heal(sucker_src, healing*(5+5*sce->val3)/100, 0, 0);
			if (sc->data[type]) {
				sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			}
			map_freeblock_unlock();
			return 0;
		}
		break;

	case SC_SOULUNITY:
		if( --(sce->val4) >= 0 )
		{// Needed to check the caster's location for the range check.
			struct block_list *unity_src = map_id2bl(sce->val1);

			// End the status if out of range.
			if ( !check_distance_bl(bl, unity_src, 11) )
				break;

			status_heal(bl, 150*sce->val3, 0, 2);
			sc_timer_next(3000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_STOMACHACHE:
		if (--(sce->val4) > 0)
		{
			status_charge(bl, 0, sce->val2); // Reduce 8 every 10 seconds.
			if (sd && !pc_issit(sd)) // Force to sit every 10 seconds.
			{
				pc_setsit(sd);
				skill_sit(sd, 1);
				clif_sitting(&sd->bl, true);
			}
			sc_timer_next(10000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_KAGEMUSYA:
		if (--(sce->val4) >= 0)
		{
			if (!status_charge(bl, 0, 1))
				break;
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_FRIGG_SONG:
		if( --(sce->val3) >= 0 )
		{
			status_heal(bl, 80 + 20 * sce->val1, 0, 2);
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_KINGS_GRACE:
		if( --(sce->val2) >= 0 )
		{
			status_heal(bl, status->max_hp * (3 + sce->val1) / 100, 0, 2);
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_FULL_THROTTLE:
		if( --(sce->val2) >= 0 )
		{
			if( !status_charge(bl, 0, status->max_sp * ( 6 - sce->val1 ) / 100) )
				break;
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_REBOUND:
		if(--(sce->val2)>0)
		{
			clif_emotion(bl,E_SWT);
			sc_timer_next(2000+tick, status_change_timer,bl->id, data);
			return 0;
		}
		break;
	case SC_SV_ROOTTWIST:
		if (--(sce->val3) > 0)
		{
			bool flag;
			struct block_list* src = map_id2bl(sce->val2);
			map_freeblock_lock();
			skill_attack(BF_MISC,src,src,bl,SU_SV_ROOTTWIST_ATK,sce->val1,tick,SD_LEVEL|SD_ANIMATION);
			flag = !sc->data[type];
			map_freeblock_unlock();
			if (flag) return 0;
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data );
			return 0;
		}
		break;
	case SC_FRESHSHRIMP:
		if( --(sce->val3) >= 0 )
		{
			status_heal(bl, status->max_hp * 4 / 100, 0, 2);
			sc_timer_next(sce->val2 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_DORAM_BUF_01:
		if (sd && --(sce->val4) >= 0) {
			if (status->hp < status->max_hp)
				status_heal(bl, 10, 0, 2);
			sc_timer_next(10000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;
	case SC_DORAM_BUF_02:
		if (sd && --(sce->val4) >= 0) {
			if (status->sp < status->max_sp)
				status_heal(bl, 0, 5, 2);
			sc_timer_next(10000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_HELPANGEL:
		if (--(sce->val4) >= 0)
			{
			status_heal(bl, 1000, 350, 2);
			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_ANGRIFFS_MODUS:
		if( --(sce->val2) >= 0 )
		{// Homunculus can die from this status. Let it drain HP until it does.
			status_charge(bl, 100, 0);

			// The status does end if the homunculus runs out of SP.
			if( !status_charge(bl, 0, 20))
				break;

			sc_timer_next(1000 + tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_CBC:
		if(--(sce->val4)>0)
		{// Val1 is used to count the number of seconds that passed.
			sce->val1 += 1;

			if ( sce->val1 % 2 == 0 )// Loose HP every 2 seconds.
				if ( sce->val3 > 0 )// SPdamage value higher then 0 signals target is not a monster.
				{// HP loss for players and other non monster entitys.
					if( !status_charge(bl, sce->val2, 0))
						break;
				}// If its a monster then use this to remove HP since status_charge won't work.
				else if (!status_damage(NULL, bl, sce->val2, 0, 0, 3))

			if ( sce->val1 % 3 == 0 )// Loose SP every 3 seconds.
				status_charge(bl, 0, sce->val3);

			sc_timer_next(1000+tick, status_change_timer,bl->id, data);
			return 0;
		}
		break;

	case SC_EL_COST:
		{
			if(!status_charge(bl, 0, 2 + 3 * sce->val1))
			{// Not enough SP to continue? Remove the elemental.
				elem_delete(sd->ed, 0);
				break;
			}

			sc_timer_next(10000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_EL_WAIT:
		{
			unsigned char elem_type, regen_percent = 2;

			if ( sc )
			{
				elem_type = elemental_get_type(ed);

				if ((sc->data[SC_FIRE_INSIGNIA] && sc->data[SC_FIRE_INSIGNIA]->val1 == 1 && elem_type == ELEMTYPE_AGNI) || 
					(sc->data[SC_WATER_INSIGNIA] && sc->data[SC_WATER_INSIGNIA]->val1 == 1 && elem_type == ELEMTYPE_AQUA) || 
					(sc->data[SC_WIND_INSIGNIA] && sc->data[SC_WIND_INSIGNIA]->val1 == 1 && elem_type == ELEMTYPE_VENTUS) || 
					(sc->data[SC_EARTH_INSIGNIA] && sc->data[SC_EARTH_INSIGNIA]->val1 == 1 && elem_type == ELEMTYPE_TERA))
					regen_percent = 4;
			}

			status_percent_heal(bl, regen_percent, regen_percent);
			sc_timer_next(battle_config.natural_elem_heal_interval+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_EL_PASSIVE:
		{
			if(!status_charge(bl, 0, 10 * sce->val1))
				break;// Not enough SP. Switch back to Wait mode.

			sc_timer_next(10000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_EL_DEFENSIVE:
		{
			if(!status_charge(bl, 0, 5))
				break;// Not enough SP. Switch back to Wait mode.

			sc_timer_next(1000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_WATER_SCREEN_OPTION:
		{
			status_heal(bl, 1000, 0, 2);
			sc_timer_next(10000+tick, status_change_timer, bl->id, data);
			return 0;
		}
		break;

	case SC_LEADERSHIP:
	case SC_GLORYWOUNDS:
	case SC_SOULCOLD:
	case SC_HAWKEYES:
		/* they only end by status_change_end */
		sc_timer_next(600000 + tick, status_change_timer, bl->id, data);
		return 0;
	}

	// If status has an interval and there is at least 100ms remaining time, wait for next interval
	if (interval > 0 && sc->data[type] && sce->val4 >= 100) {
		sc_timer_next(min(sce->val4, interval) + tick, status_change_timer, bl->id, data);
		sce->val4 -= interval;
		if (dounlock)
			map_freeblock_unlock();
		return 0;
	}

	if (dounlock)
		map_freeblock_unlock();

	// Default for all non-handled control paths is to end the status
	return status_change_end( bl,type,tid );	
#undef sc_timer_next
}

/*==========================================
 * ステータス異常タイマー範囲処理
 *------------------------------------------*/
int status_change_timer_sub(struct block_list* bl, va_list ap)
{
	struct status_change* tsc;

	struct block_list* src = va_arg(ap,struct block_list*);
	struct status_change_entry* sce = va_arg(ap,struct status_change_entry*);
	enum sc_type type = (sc_type)va_arg(ap,int); //gcc: enum args get promoted to int
	int64 tick = va_arg(ap, int64);

	if (status_isdead(bl))
		return 0;

	tsc = status_get_sc(bl);

	switch( type )
	{
	case SC_SIGHT:	/* Reveal hidden ennemy on 3*3 range */
		if (sce && sce->val4 == 2000 && tsc && tsc->data[SC__SHADOWFORM] && rnd() % 100 < 100 - 10 * tsc->data[SC__SHADOWFORM]->val1)
		{//Attempt to remove Shadow Form status by chance every 2 seconds. [Rytech]
			status_change_end(bl, SC__SHADOWFORM, INVALID_TIMER);
			sce->val4 = 0;
		}
		else if (sce && sce->val4 >= 2000)//Reset check to 0 seconds only if above condition fails.
			sce->val4 = 0;//No break after this since other invisiable character status's are removed as well.
	case SC_CONCENTRATE:
		status_change_end(bl, SC_HIDING, INVALID_TIMER);
		status_change_end(bl, SC_CLOAKING, INVALID_TIMER);
		status_change_end(bl, SC_CLOAKINGEXCEED, INVALID_TIMER);
		status_change_end(bl, SC_CAMOUFLAGE, INVALID_TIMER);
		status_change_end(bl, SC_NEWMOON, INVALID_TIMER);
		break;
	case SC_RUWACH:	/* At */
		if (sce && sce->val4 == 2000 && tsc && tsc->data[SC__SHADOWFORM] && rnd() % 100 < 100 - 10 * tsc->data[SC__SHADOWFORM]->val1)
		{//Attempt to remove Shadow Form status by chance every 2 seconds. [Rytech]
			status_change_end(bl, SC__SHADOWFORM, INVALID_TIMER);
			if(battle_check_target( src, bl, BCT_ENEMY ) > 0)
				skill_attack(BF_MAGIC,src,src,bl,AL_RUWACH,1,tick,0);
			sce->val4 = 0;
		}
		else if ( sce && sce->val4 >= 2000 )//Reset check to 0 seconds only if above condition fails.
			sce->val4 = 0;//No break after this since other invisiable character status's are removed as well.
		if (tsc && (tsc->data[SC_HIDING] || tsc->data[SC_CLOAKING] || tsc->data[SC_CLOAKINGEXCEED] ||
			tsc->data[SC_CAMOUFLAGE] || tsc->data[SC_NEWMOON])) {
			status_change_end(bl, SC_HIDING, INVALID_TIMER);
			status_change_end(bl, SC_CLOAKING, INVALID_TIMER);
			status_change_end(bl, SC_CLOAKINGEXCEED, INVALID_TIMER);
			status_change_end(bl, SC_CAMOUFLAGE, INVALID_TIMER);
			status_change_end(bl, SC_NEWMOON, INVALID_TIMER);
			if(battle_check_target( src, bl, BCT_ENEMY ) > 0)
				skill_attack(BF_MAGIC,src,src,bl,AL_RUWACH,1,tick,0);
		}
		break;
	case SC_SIGHTBLASTER:
		if (battle_check_target(src, bl, BCT_ENEMY) > 0 && status_check_skilluse(src, bl, WZ_SIGHTBLASTER, sce->val1, 2))
		{
			if (sce) {
				struct skill_unit *su = NULL;
				if (bl->type == BL_SKILL)
					su = (struct skill_unit *)bl;
				if (skill_attack(BF_MAGIC, src, src, bl, WZ_SIGHTBLASTER, sce->val1, tick, 0x1000000)
					&& (!su || !su->group || !(skill_get_inf2(su->group->skill_id)&INF2_TRAP))) { // The hit is not counted if it's against a trap
					sce->val2 = 0; // This signals it to end.
				}
				else if ((bl->type&BL_SKILL) && sce->val4 % 2 == 0) {
					//Remove trap immunity temporarily so it triggers if you still stand on it
					sce->val4++;
				}
			}
		}
		break;
	case SC_CLOSECONFINE:
		//Lock char has released the hold on everyone...
		if (tsc && tsc->data[SC_CLOSECONFINE2] && tsc->data[SC_CLOSECONFINE2]->val2 == src->id) {
			tsc->data[SC_CLOSECONFINE2]->val2 = 0;
			status_change_end(bl, SC_CLOSECONFINE2, INVALID_TIMER);
		}
		break;
	}
	return 0;
}

/*==========================================
 * Clears buffs/debuffs of a character.
 * @param bl: Object to clear [PC|MOB|HOM|MER|ELEM]
 * @param type: Type to remove
 *  SCCB_BUFFS: Clear Buffs
 *  SCCB_DEBUFFS: Clear Debuffs
 *  SCCB_REFRESH: Clear specific debuffs through RK_REFRESH
 *  SCCB_CHEM_PROTECT: Clear AM_CP_ARMOR/HELM/SHIELD/WEAPON
 *  SCCB_LUXANIMA: Bonus Script removed through RK_LUXANIMA
 *------------------------------------------*/
int status_change_clear_buffs (struct block_list* bl, uint8 type)
{
	int i;
	struct status_change *sc= status_get_sc(bl);

	if (!sc || !sc->count)
		return 0;

	map_freeblock_lock();

	if (type&(SCCB_DEBUFFS | SCCB_REFRESH)) //Debuffs
	for( i = SC_COMMON_MIN; i <= SC_COMMON_MAX; i++ )
	{
		status_change_end(bl, (sc_type)i, INVALID_TIMER);
	}

	for( i = SC_COMMON_MAX+1; i < SC_MAX; i++ )
	{
		if(!sc->data[i])
			continue;
		
		switch (i) {
			//Stuff that cannot be removed
			case SC_WEIGHT50:
			case SC_WEIGHT90:
			case SC_COMBO:
			case SC_SMA:
			case SC_DANCING:
			case SC_LEADERSHIP:
			case SC_GLORYWOUNDS:
			case SC_SOULCOLD:
			case SC_HAWKEYES:
			case SC_SAFETYWALL:
			case SC_PNEUMA:
			case SC_NOCHAT:
			case SC_JAILED:
			case SC_ANKLE:
			case SC_BLADESTOP:
			case SC_STRFOOD:
			case SC_AGIFOOD:
			case SC_VITFOOD:
			case SC_INTFOOD:
			case SC_DEXFOOD:
			case SC_LUKFOOD:
			case SC_HITFOOD:
			case SC_CRIFOOD:
			case SC_FLEEFOOD:
			case SC_BATKFOOD:
			case SC_WATKFOOD:
			case SC_MATKFOOD:
			case SC_FOOD_STR_CASH:
			case SC_FOOD_AGI_CASH:
			case SC_FOOD_VIT_CASH:
			case SC_FOOD_DEX_CASH:
			case SC_FOOD_INT_CASH:
			case SC_FOOD_LUK_CASH:
			case SC_EXPBOOST:
			case SC_JEXPBOOST:
			case SC_ITEMBOOST:
			case SC_ALL_RIDING:
			case SC_ON_PUSH_CART:
			case SC_MONSTER_TRANSFORM:
			case SC_ACTIVE_MONSTER_TRANSFORM:
			case SC_MOONSTAR:
			case SC_STRANGELIGHTS:
			case SC_SUPER_STAR:
			case SC_DECORATION_OF_MUSIC:
			case SC_LJOSALFAR:
			case SC_MERMAID_LONGING:
			case SC_HAT_EFFECT:
			case SC_FLOWERSMOKE:
			case SC_FSTONE:
			case SC_HAPPINESS_STAR:
			case SC_MAPLE_FALLS:
			case SC_TIME_ACCESSORY:
			case SC_MAGICAL_FEATHER:
			case SC_ELECTRICSHOCKER:
			case SC__MANHOLE:
			case SC_MILLENNIUMSHIELD:
			case SC_REFRESH:
			case SC_GIANTGROWTH:
			case SC_STONEHARDSKIN:
			case SC_VITALITYACTIVATION:
			case SC_FIGHTINGSPIRIT:
			case SC_ABUNDANCE:
			case SC_NEUTRALBARRIER_MASTER:
			case SC_NEUTRALBARRIER:
			case SC_STEALTHFIELD_MASTER:
			case SC_STEALTHFIELD:
			case SC_BANDING:
			case SC_ATTHASTE_CASH:
			case SC_REUSE_REFRESH:
			case SC_REUSE_LIMIT_A:
			case SC_REUSE_LIMIT_B:
			case SC_REUSE_LIMIT_C:
			case SC_REUSE_LIMIT_D:
			case SC_REUSE_LIMIT_E:
			case SC_REUSE_LIMIT_F:
			case SC_REUSE_LIMIT_G:
			case SC_REUSE_LIMIT_H:
			case SC_REUSE_LIMIT_MTF:
			case SC_REUSE_LIMIT_ECL:
			case SC_REUSE_LIMIT_RECALL:
			case SC_REUSE_LIMIT_ASPD_POTION:
			case SC_REUSE_MILLENNIUMSHIELD:
			case SC_REUSE_CRUSHSTRIKE:
			case SC_REUSE_STORMBLAST:
			case SC_ALL_RIDING_REUSE_LIMIT:
			case SC_SAVAGE_STEAK:
			case SC_COCKTAIL_WARG_BLOOD:
			case SC_MINOR_BBQ:
			case SC_SIROMA_ICE_TEA:
			case SC_DROCERA_HERB_STEAMED:
			case SC_PUTTI_TAILS_NOODLES:
			case SC_SPRITEMABLE:
			case SC_SOULATTACK:
			case SC_MTF_ASPD2:
			case SC_MTF_RANGEATK2:
			case SC_MTF_MATK2:
			case SC_2011RWC_SCROLL:
			case SC_JP_EVENT04:
			case SC_MTF_MHP:
			case SC_MTF_MSP:
			case SC_MTF_PUMPKIN:
			case SC_MTF_HITFLEE:
				continue;

			// Chemical Protection is only removed by some skills
			case SC_CP_WEAPON:
			case SC_CP_SHIELD:
			case SC_CP_ARMOR:
			case SC_CP_HELM:
				if (!(type&SCCB_CHEM_PROTECT))
					continue;
				break;
				
			//Debuffs that can be removed.
			case SC_HALLUCINATION:
			case SC_QUAGMIRE:
			case SC_SIGNUMCRUCIS:
			case SC_DECREASEAGI:
			case SC_SLOWDOWN:
			case SC_MINDBREAKER:
			case SC_WINKCHARM:
			case SC_STOP:
			case SC_ORCISH:
			case SC_STRIPWEAPON:
			case SC_STRIPSHIELD:
			case SC_STRIPARMOR:
			case SC_STRIPHELM:
			case SC__STRIPACCESSARY:
			case SC_WUGBITE:
			case SC_ADORAMUS:
			case SC_VACUUM_EXTREME:
			case SC_MAGNETICFIELD:
				if (!(type&SCCB_DEBUFFS))
					continue;
				break;
			//The rest are buffs that can be removed.
			case SC_BERSERK:
				if (!(type&SCCB_BUFFS))
					continue;
			  	sc->data[i]->val2 = 0;
				break;
			default:
				if (!(type&SCCB_BUFFS))
					continue;
				break;
		}
		status_change_end(bl, (sc_type)i, INVALID_TIMER);

		//Removes bonus_script
		if (bl->type == BL_PC) {
			i = 0;
			if (type&SCCB_BUFFS) i |= BSF_REM_BUFF;
			if (type&SCCB_DEBUFFS) i |= BSF_REM_DEBUFF;
			if (type&SCCB_REFRESH) i |= BSF_REM_ON_REFRESH;
			if (type&SCCB_LUXANIMA) i |= BSF_REM_ON_LUXANIMA;
			pc_bonus_script_clear(BL_CAST(BL_PC,bl),i);
		}
	}

	//cleaning all extras vars
	sc->comet_x = 0;
	sc->comet_y = 0;
	sc->sg_counter = 0;

	map_freeblock_unlock();

	return 0;
}

/**
 * Infect a user with status effects (SC_DEADLYINFECT)
 * @param src: Object initiating change on bl [PC|MOB|HOM|MER|ELEM]
 * @param bl: Object to change
 * @return 1: Success 0: Fail
 */
int status_change_spread(struct block_list *src, struct block_list *bl)
{
	int i, flag = 0;
	struct status_change *sc = status_get_sc(src);
	const struct TimerData *timer = NULL;
	int64 tick;
	struct status_change_data data;

	if (!sc || !sc->count)
		return 0;

	tick = gettick();

	// Status Immunity resistance
	if (status_bl_has_mode(src, MD_STATUS_IMMUNE) || status_bl_has_mode(bl, MD_STATUS_IMMUNE))
		return 0;

	for (i = SC_COMMON_MIN; i < SC_MAX; i++) {
		if (!sc->data[i] || i == SC_COMMON_MAX)
			continue;
		if (sc->data[i]->timer != INVALID_TIMER) {
			timer = get_timer(sc->data[i]->timer);
			if (timer == NULL || timer->func != status_change_timer || DIFF_TICK(timer->tick, tick) < 0)
				continue;
		}

		switch (i) {
			// Debuffs that can be spread.
			// NOTE: We'll add/delete SCs when we are able to confirm it.
		case SC_DEATHHURT:
		case SC_PARALYSE:
		case SC_CURSE:
		case SC_SILENCE:
		case SC_CONFUSION:
		case SC_BLIND:
		case SC_HALLUCINATION:
		case SC_SIGNUMCRUCIS:
		case SC_DECREASEAGI:
			//case SC_SLOWDOWN:
			//case SC_MINDBREAKER:
			//case SC_WINKCHARM:
			//case SC_STOP:
		case SC_ORCISH:
			//case SC_STRIPWEAPON: // Omg I got infected and had the urge to strip myself physically.
			//case SC_STRIPSHIELD: // No this is stupid and shouldnt be spreadable at all.
			//case SC_STRIPARMOR: // Disabled until I can confirm if it does or not. [Rytech]
			//case SC_STRIPHELM:
			//case SC__STRIPACCESSORY:
			//case SC_BITE:
		case SC_FEAR:
		case SC_FREEZE:
		case SC_VENOMBLEED:
			if (sc->data[i]->timer != INVALID_TIMER)
				data.tick = DIFF_TICK32(timer->tick, tick);
			else
				data.tick = INVALID_TIMER;
			break;
			// Special cases
		case SC_TOXIN:
		case SC_MAGICMUSHROOM:
		case SC_PYREXIA:
		case SC_LEECHESEND:
		case SC_POISON:
		case SC_DPOISON:
		case SC_BLEEDING:
		case SC_BURNING:
			if (sc->data[i]->timer != INVALID_TIMER)
				data.tick = DIFF_TICK32(timer->tick, tick) + sc->data[i]->val4;
			else
				data.tick = INVALID_TIMER;
			break;
		default:
			continue;
		}
		if (i) {
			data.val1 = sc->data[i]->val1;
			data.val2 = sc->data[i]->val2;
			data.val3 = sc->data[i]->val3;
			data.val4 = sc->data[i]->val4;
			status_change_start(src, bl, (sc_type)i, 10000, data.val1, data.val2, data.val3, data.val4, data.tick, SCSTART_NOAVOID | SCSTART_NOTICKDEF | SCSTART_NORATEDEF);
			flag = 1;
		}
	}

	return flag;
}

//Natural regen related stuff.
static int64 natural_heal_prev_tick;
static unsigned int natural_heal_diff_tick;
static int status_natural_heal( struct block_list* bl, va_list args )
{
	struct regen_data *regen;
	struct status_data *status;
	struct status_change *sc;
	struct unit_data *ud;
	struct view_data *vd = NULL;
	struct regen_data_sub *sregen;
	struct map_session_data *sd;
	int val, rate, multi = 1, flag, heal_interval;

	regen = status_get_regen_data( bl );
	if ( !regen ) 
		return 0;

	status = status_get_status_data( bl );
	sc = status_get_sc( bl );
	if ( sc && !sc->count )
		sc = NULL;
	sd = BL_CAST(BL_PC,bl);

	flag = regen->flag;
	if (flag&RGN_HP && (status->hp >= status->max_hp || regen->state.block&1))
		flag&=~(RGN_HP|RGN_SHP);
	if (flag&RGN_SP && (status->sp >= status->max_sp || regen->state.block&2))
		flag&=~(RGN_SP|RGN_SSP);

	if (flag && (
		status_isdead(bl) ||
		(sc && sc->option&(OPTION_HIDE | OPTION_CLOAK | OPTION_CHASEWALK))
	))
		flag=0;

	if (sd) {
		if (sd->hp_loss.value || sd->sp_loss.value)
			pc_bleeding(sd, natural_heal_diff_tick);
		if (sd->hp_regen.value || sd->sp_regen.value)
			pc_regen(sd, natural_heal_diff_tick);
	}

	if(flag&(RGN_SHP|RGN_SSP) && regen->ssregen &&
		(vd = status_get_viewdata(bl)) && vd->dead_sit == 2)
	{	//Apply sitting regen bonus.
		sregen = regen->ssregen;
		if(flag&(RGN_SHP))
		{	//Sitting HP regen
			val = natural_heal_diff_tick * sregen->rate.hp / 100;
			if (regen->state.overweight)
				val>>=1; //Half as fast when overweight.
			sregen->tick.hp += val;
			while(sregen->tick.hp >= (unsigned int)battle_config.natural_heal_skill_interval)
			{
				sregen->tick.hp -= battle_config.natural_heal_skill_interval;
				if(status_heal(bl, sregen->hp, 0, 3) < sregen->hp)
				{	//Full
					flag&=~(RGN_HP|RGN_SHP);
					break;
				}
			}
		}
		if(flag&(RGN_SSP))
		{	//Sitting SP regen
			val = natural_heal_diff_tick * sregen->rate.sp / 100;
			if (regen->state.overweight)
				val>>=1; //Half as fast when overweight.
			sregen->tick.sp += val;
			while(sregen->tick.sp >= (unsigned int)battle_config.natural_heal_skill_interval)
			{
				sregen->tick.sp -= battle_config.natural_heal_skill_interval;
				if(status_heal(bl, 0, sregen->sp, 3) < sregen->sp)
				{	//Full
					flag&=~(RGN_SP|RGN_SSP);
					break;
				}
			}
		}
	}

	if (flag && regen->state.overweight)
		flag=0;

	ud = unit_bl2ud(bl);

	if (flag&(RGN_HP|RGN_SHP|RGN_SSP) && ud && ud->walktimer != INVALID_TIMER)
	{
		flag&=~(RGN_SHP|RGN_SSP);
		if(!regen->state.walk)
			flag&=~RGN_HP;
	}

	if (!flag)
		return 0;

	if (flag&(RGN_HP|RGN_SP))
	{
		if(!vd) vd = status_get_viewdata(bl);
		if(vd && vd->dead_sit == 2)
			multi += 1; //This causes the interval to be halved
		if(regen->state.gc)
			multi += 1; //This causes the interval to be halved
	}

	//Natural Hp regen
	if (flag&RGN_HP)
	{
		rate = (int)(natural_heal_diff_tick * (regen->rate.hp / 100. * multi));
		if (!(sc && sc->data[SC_GENTLETOUCH_REVITALIZE]) && ud && ud->walktimer != INVALID_TIMER)
			rate/=2;

		regen->tick.hp += rate;

		// Different interval's for homunculus, elementals, and everything else.
		if(bl->type==BL_HOM)
			heal_interval = battle_config.natural_homun_healhp_interval;
		else
			heal_interval = battle_config.natural_healhp_interval;

		if(regen->tick.hp >= heal_interval)
		{
			val = 0;
			do
			{
				val += regen->hp;
				regen->tick.hp -= heal_interval;
			} while(regen->tick.hp >= heal_interval);
			if (status_heal(bl, val, 0, 1) < val)
				flag&=~RGN_SHP; //full.
		}
	}

	//Natural SP regen
	if (flag&RGN_SP)
	{
		rate = (int)(natural_heal_diff_tick * (regen->rate.sp/100. * multi));

		regen->tick.sp += rate;

		// Different interval's for homunculus, elementals, and everything else.
		if(bl->type==BL_HOM)
			heal_interval = battle_config.natural_homun_healsp_interval;
		else
			heal_interval = battle_config.natural_healsp_interval;

		if(regen->tick.sp >= heal_interval)
		{
			val = 0;
			do
			{
				val += regen->sp;
				regen->tick.sp -= heal_interval;
			} while(regen->tick.sp >= heal_interval);
			if (status_heal(bl, 0, val, 1) < val)
				flag&=~RGN_SSP; //full.
		}
	}

	if (!regen->sregen)
		return flag;

	//Skill regen
	sregen = regen->sregen;

	if(flag&RGN_SHP)
	{	//Skill HP regen
		sregen->tick.hp += natural_heal_diff_tick * sregen->rate.hp / 100;
		
		while(sregen->tick.hp >= (unsigned int)battle_config.natural_heal_skill_interval)
		{
			sregen->tick.hp -= battle_config.natural_heal_skill_interval;
			if(status_heal(bl, sregen->hp, 0, 3) < sregen->hp)
				break; //Full
		}
	}
	if(flag&RGN_SSP)
	{	//Skill SP regen
		sregen->tick.sp += natural_heal_diff_tick * sregen->rate.sp / 100;
		while(sregen->tick.sp >= (unsigned int)battle_config.natural_heal_skill_interval)
		{
			val = sregen->sp;
			if (sd && sd->state.doridori) {
				val*=2;
				sd->state.doridori = 0;
				if ((rate = pc_checkskill(sd,TK_SPTIME)))
					sc_start(bl,status_skill2sc(TK_SPTIME),
						100,rate,skill_get_time(TK_SPTIME, rate));
				if (
					(sd->class_&MAPID_UPPERMASK) == MAPID_STAR_GLADIATOR &&
					rnd()%10000 < battle_config.sg_angel_skill_ratio
				) { //Angel of the Sun/Moon/Star
					clif_feel_hate_reset(sd);
					pc_resethate(sd);
					pc_resetfeel(sd);
				}
			}
			sregen->tick.sp -= battle_config.natural_heal_skill_interval;
			if(status_heal(bl, 0, val, 3) < val)
				break; //Full
		}
	}
	return flag;
}

//Natural heal main timer.
static int status_natural_heal_timer(int tid, int64 tick, int id, intptr_t data)
{
	// This difference is always positive and lower than UINT_MAX (~24 days)
	natural_heal_diff_tick = (unsigned int)cap_value(DIFF_TICK(tick, natural_heal_prev_tick), 0, UINT_MAX);
	map_foreachregen(status_natural_heal);
	natural_heal_prev_tick = tick;
	return 0;
}

/*==========================================
 * Check if status is disabled on a map
 * @param type: Status Change data
 * @param mapIsVS: If the map is a map_flag_vs type
 * @param mapisPVP: If the map is a PvP type
 * @param mapIsGVG: If the map is a map_flag_gvg type
 * @param mapIsBG: If the map is a Battleground type
 * @param mapZone: Map Zone type
 * @param mapIsTE: If the map us WOE TE 
 * @return True - SC disabled on map; False - SC not disabled on map/Invalid SC
 *------------------------------------------*/
static bool status_change_isDisabledOnMap_(sc_type type, bool mapIsVS, bool mapIsPVP, bool mapIsGVG, bool mapIsBG, unsigned int mapZone, bool mapIsTE) 
{
	if (type <= SC_NONE || type >= SC_MAX)
		return false;

	if ((!mapIsVS && SCDisabled[type]&1) ||
		(mapIsPVP && SCDisabled[type]&2) ||
		(mapIsGVG && SCDisabled[type]&4) ||
		(mapIsBG && SCDisabled[type]&8) ||
		(mapIsTE && SCDisabled[type]&16) ||
		(SCDisabled[type]&(mapZone)))
	{
		return true;
	}

	return false;
}

/*==========================================
 * Clear a status if it is disabled on a map
 * @param bl: Block list data
 * @param sc: Status Change data
 *------------------------------------------*/
void status_change_clear_onChangeMap(struct block_list *bl, struct status_change *sc)
{
	nullpo_retv(bl);

	if (sc && sc->count) {
		unsigned short i;
		bool mapIsVS = map_flag_vs2(bl->m);
		bool mapIsPVP = map[bl->m].flag.pvp;
		bool mapIsGVG = map_flag_gvg2_no_te(bl->m);
		bool mapIsBG = map[bl->m].flag.battleground;
		bool mapIsTE = map_flag_gvg2_te(bl->m);
		unsigned int mapZone = map[bl->m].zone << 3;

		for (i = 0; i < SC_MAX; i++) {
			if (!sc->data[i] || !SCDisabled[i])
				continue;

			if (status_change_isDisabledOnMap_((sc_type)i, mapIsVS, mapIsPVP, mapIsGVG, mapIsBG, mapZone, mapIsTE)) 
				status_change_end(bl, (sc_type)i, INVALID_TIMER);
		}
	}
}

/**
 * Returns refine cost (zeny or item) for a weapon level.
 * @param weapon_lv Weapon level
 * @param type Refine type (can be retrieved from refine_cost_type enum)
 * @param what true = returns zeny, false = returns item id
 * @return Refine cost for a weapon level
 */
int status_get_refine_cost(int weapon_lv, int type, bool what) {
	return what ? refine_info[weapon_lv].cost[type].zeny : refine_info[weapon_lv].cost[type].nameid;
}

/*==========================================
 * Read status_disabled.txt file
 * @param str: Fields passed from sv_readdb
 * @param columns: Columns passed from sv_readdb function call
 * @param current: Current row being read into SCDisabled array
 * @return True - Successfully stored, False - Invalid SC
 *------------------------------------------*/
static bool status_readdb_status_disabled(char **str, int columns, int current)
{
	int type = SC_NONE;

	if (ISDIGIT(str[0][0]))
		type = atoi(str[0]);
	else {
		if (!script_get_constant(str[0],&type))
			type = SC_NONE;
	}

	if (type <= SC_NONE || type >= SC_MAX) {
		ShowError("status_readdb_status_disabled: Invalid SC with type %s.\n", str[0]);
		return false;
	}

	SCDisabled[type] = (unsigned int)atol(str[1]);
	return true;
}

/*==========================================
 * DB reading.
 * size_fix.txt   - size adjustment table for weapons
 * refine_db.txt  - refining data table
 *------------------------------------------*/

static bool status_readdb_sizefix(char* fields[], int columns, int current)
{
	unsigned int i;

	for(i = 0; i < MAX_WEAPON_TYPE; i++)
	{
		atkmods[current][i] = atoi(fields[i]);
	}
	return true;
}

static bool status_readdb_refine(char* fields[], int columns, int current)
{
	int i;

	refinebonus[current][0] = atoi(fields[0]);  // stats per safe-upgrade
	refinebonus[current][1] = atoi(fields[1]);  // stats after safe-limit
	refinebonus[current][2] = atoi(fields[2]);  // safe limit

	for(i = 0; i < MAX_REFINE; i++)
	{
		percentrefinery[current][i] = atoi(fields[3+i]);
	}
	return true;
}

static bool  status_readdb_refine_cost(char* fields[], int columns, int current)
{
	int type, zeny;
	t_itemid nameid;

	current = atoi(fields[0]);

	if (current < 0 || current >= REFINE_TYPE_MAX)
		return false;

	type = atoi(fields[1]);

	if (type < 0 || type >= REFINE_COST_MAX)
		return false;

	zeny = atoi(fields[2]);
	nameid = strtoul(fields[3], NULL, 10);

	refine_info[current].cost[type].zeny = zeny;
	refine_info[current].cost[type].nameid = nameid;

	return true;
}

/**
 * Read attribute fix database for attack calculations
 * Function stores information in the attr_fix_table
 * @return True
 */
static bool status_readdb_attrfix(const char *basedir)
{
	FILE *fp;
	char line[512], path[512], *p;
	int i, j, k, entries = 0;

	// attr_fix.txt
	for (i = 0; i < 4; i++)
		for (j = 0; j < ELE_ALL; j++)
			for (k = 0; k < ELE_ALL; k++)
				attr_fix_table[i][j][k] = 100;

	sprintf(path, "%s/attr_fix.txt", basedir);
	fp = fopen(path,"r");
	if (fp == NULL) {
		ShowError("Can't read %s\n", path);
		return 1;
	}

	while (fgets(line, sizeof(line), fp))
	{
		char *split[10];
		int lv, n;
		if (line[0] == '/' && line[1] == '/')
			continue;
		for (j = 0, p = line; j < 3 && p; j++) {
			split[j] = p;
			p = strchr(p, ',');
			if (p) *p++ = 0;
		}
		if (j < 2)
			continue;

		lv = atoi(split[0]);
		n = atoi(split[1]);

		for (i = 0; i < n && i < ELE_ALL;) {
			if (!fgets(line, sizeof(line), fp))
				break;
			if (line[0] == '/' && line[1] == '/')
				continue;

			for (j = 0, p = line; j < n && j < ELE_ALL && p; j++) {
				while (*p == 32 && *p > 0)
					p++;
				attr_fix_table[lv - 1][i][j] = atoi(p);
				if (battle_config.attr_recover == 0 && attr_fix_table[lv - 1][i][j] < 0)
					attr_fix_table[lv - 1][i][j] = 0;
				p = strchr(p, ',');
				if (p) *p++ = 0;
			}

			i++;
		}
		entries++;
	}

	fclose(fp);
	ShowStatus("Done reading '"CL_WHITE"%d"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", entries, path);
	return true;
}

/**
 * Sets defaults in tables and starts read db functions
 * sv_readdb reads the file, outputting the information line-by-line to
 * previous functions above, separating information by delimiter
 * DBs being read:
 *	attr_fix.txt: Attribute adjustment table for attacks
 *	size_fix.txt: Size adjustment table for weapons
 *	refine_db.txt: Refining data table
 * @return 0
 */
int status_readdb(void)
{
	int i, j;

	memset(SCDisabled, 0, sizeof(SCDisabled));

	// size_fix.txt
	for(i=0;i<ARRAYLENGTH(atkmods);i++)
		for(j=0;j<MAX_WEAPON_TYPE;j++)
			atkmods[i][j]=100;

	// refine_db.txt
	for(i=0;i<ARRAYLENGTH(percentrefinery);i++){
		for(j=0;j<MAX_REFINE; j++)
			percentrefinery[i][j]=100;  // success chance
		percentrefinery[i][j]=0; //Slot MAX+1 always has 0% success chance [Skotlex]
		refinebonus[i][0]=0;  // stats per safe-upgrade
		refinebonus[i][1]=0;  // stats after safe-limit
		refinebonus[i][2]=10;  // safe limit
	}

	// read databases
	//

	status_readdb_attrfix(db_path);
 	sv_readdb(db_path, "status_disabled.txt", ',', 2,                 2,                  -1,                           &status_readdb_status_disabled); 
	sv_readdb(db_path, "size_fix.txt",        ',', MAX_WEAPON_TYPE,   MAX_WEAPON_TYPE,    ARRAYLENGTH(atkmods),         &status_readdb_sizefix);
	sv_readdb(db_path, "refine_db.txt",       ',', 3+MAX_REFINE+1,    3+MAX_REFINE+1,     ARRAYLENGTH(percentrefinery), &status_readdb_refine);
	sv_readdb(db_path, "refine_cost_db.txt",  ',', 4,                 4,                  -1,                           &status_readdb_refine_cost);

	return 0;
}

/*==========================================
 * Status db init and destroy.
 *------------------------------------------*/
int do_init_status(void)
{
	add_timer_func_list(status_change_timer,"status_change_timer");
	add_timer_func_list(status_natural_heal_timer,"status_natural_heal_timer");
	initChangeTables();
	initDummyData();
	status_readdb();
	natural_heal_prev_tick = gettick();
	sc_data_ers = ers_new(sizeof(struct status_change_entry), "status.c::sc_data_ers", ERS_OPT_NONE);
	add_timer_interval(natural_heal_prev_tick + NATURAL_HEAL_INTERVAL, status_natural_heal_timer, 0, 0, NATURAL_HEAL_INTERVAL);
	return 0;
}
void do_final_status(void)
{
	ers_destroy(sc_data_ers);
}
