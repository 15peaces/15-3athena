// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _ITEMDB_H_
#define _ITEMDB_H_

#include "../common/mmo.h" // ITEM_NAME_LENGTH

///Maximum allowed Item ID (range: 1 ~ 65,534)
#define MAX_ITEMID USHRT_MAX

#define MAX_RANDITEM	11000

// The maximum number of item delays
#define MAX_ITEMDELAYS	40

#define MAX_ROULETTE_LEVEL 7 /** client-defined value **/
#define MAX_ROULETTE_COLUMNS 9 /** client-defined value **/

#define MAX_SEARCH	5  //Designed for search functions, species max number of matches to display.

enum item_itemid
{
	ITEMID_RED_POTION					= 501,
 	ITEMID_YELLOW_POTION				= 503,
 	ITEMID_WHITE_POTION					= 504,
 	ITEMID_BLUE_POTION					= 505,
	ITEMID_APPLE						= 512,
	ITEMID_CARROT						= 515,
	ITEMID_HOLY_WATER					= 523,
	ITEMID_PUMPKIN						= 535,
	ITEMID_RED_SLIM_POTION				= 545,
	ITEMID_YELLOW_SLIM_POTION			= 546,
	ITEMID_WHITE_SLIM_POTION			= 547,
	ITEMID_SHRIMP						= 567,
	ITEMID_WING_OF_FLY					= 601,
	ITEMID_WING_OF_BUTTERFLY			= 602,
	ITEMID_BRANCH_OF_DEAD_TREE			= 604,
	ITEMID_ANODYNE						= 605,
	ITEMID_ALOEBERA						= 606,
	ITEMID_MAGNIFIER					= 611,
	ITEMID_EMPTY_BOTTLE					= 713,
	ITEMID_EMPERIUM						,
	ITEMID_YELLOW_GEMSTONE				,
	ITEMID_RED_GEMSTONE					,
	ITEMID_BLUE_GEMSTONE				,
	ITEMID_ALCOHOL						= 970,
	ITEMID_ORIDECON						= 984,
	ITEMID_ANVIL						= 986,
	ITEMID_ORIDECON_ANVIL				= 987,
	ITEMID_GOLDEN_ANVIL					= 988,
	ITEMID_EMPERIUM_ANVIL				= 989,
	ITEMID_BLOODY_RED					= 990,
	ITEMID_CRYSTAL_BLUE					= 991,
	ITEMID_WIND_OF_VERDURE				= 992,
	ITEMID_YELLOW_LIVE					= 993,
	ITEMID_FLAME_HEART					= 994,
	ITEMID_MISTIC_FROZEN				= 995,
	ITEMID_ROUGH_WIND					= 996,
	ITEMID_GREAT_NATURE					= 997,
	ITEMID_IRON							= 998,
	ITEMID_STEEL						= 999,
	ITEMID_STAR_CRUMB					= 1000,
	ITEMID_PHRACON						= 1010,
	ITEMID_EMVERETARCON					= 1011,
	ITEMID_TRAP							= 1065,
	ITEMID_PILE_BUNKER					= 1549,
	ITEMID_COOLING_DEVICE				= 2804,
	ITEMID_HIGH_QUALITY_COOLER			= 2809,
	ITEMID_SPECIAL_COOLER				,
	ITEMID_FACE_PAINT					= 6120,
	ITEMID_MAKEOVER_BRUSH				,
	ITEMID_PAINT_BRUSH					,
	ITEMID_SURFACE_PAINT				,
	ITEMID_MAGIC_GEAR_FUEL				= 6146,
	ITEMID_MAGICBOOK_FIREBOLT			= 6189,
	ITEMID_MAGICBOOK_COLDBOLT			,
	ITEMID_MAGICBOOK_LIGHTNINGBOLT		,
	ITEMID_MAGICBOOK_STORMGUST			,
	ITEMID_MAGICBOOK_VERMILION			,
	ITEMID_MAGICBOOK_METEOR				,
	ITEMID_MAGICBOOK_COMET				,
	ITEMID_MAGICBOOK_TETRAVORTEX		,
	ITEMID_MAGICBOOK_THUNDERSTORM		,
	ITEMID_MAGICBOOK_JUPITEL			,
	ITEMID_MAGICBOOK_WATERBALL			,
	ITEMID_MAGICBOOK_HEAVENDRIVE		,
	ITEMID_MAGICBOOK_EARTHSPIKE			,
	ITEMID_MAGICBOOK_EARTHSTRAIN		,
	ITEMID_MAGICBOOK_CHAINLIGHTNING		,
	ITEMID_MAGICBOOK_CRIMSONROCK		,
	ITEMID_MAGICBOOK_DRAINLIFE			,
	ITEMID_SEED_OF_HORNY_PLANT			= 6210,
	ITEMID_BLOODSUCK_PLANT_SEED			= 6211,
	ITEMID_BOMB_MUSHROOM_SPORE			= 6212,
	ITEMID_SCARLETT_POINT				= 6360,
	ITEMID_INDIGO_POINT					,
	ITEMID_YELLOW_WISH_POINT			,
	ITEMID_LIME_GREEN_POINT				,
	ITEMID_STRANGE_EMBRYO				= 6415,
	ITEMID_STONE						= 7049,
	ITEMID_FIRE_BOTTLE					= 7135,
	ITEMID_ACID_BOTTLE					= 7136,
	ITEMID_MAN_EATER_BOTTLE				= 7137,
	ITEMID_MINI_BOTTLE					= 7138,
	ITEMID_COATING_BOTTLE				= 7139,
	ITEMID_FRAGMENT_OF_CRYSTAL			= 7321,
	ITEMID_SKULL_						= 7420,
	ITEMID_TOKEN_OF_SIEGFRIED			= 7621,
	ITEMID_SPECIAL_ALLOY_TRAP			= 7940,
	ITEMID_CATNIP_FRUIT					= 11602,
	ITEMID_RED_POUCH_OF_SURPRISE		= 12024,
	ITEMID_BLOODY_DEAD_BRANCH			= 12103,
	ITEMID_PORING_BOX					= 12109,
	// Mercenary Scrolls
	ITEMID_BOW_MERCENARY_SCROLL1		= 12153,
	ITEMID_BOW_MERCENARY_SCROLL2		,
	ITEMID_BOW_MERCENARY_SCROLL3		,
	ITEMID_BOW_MERCENARY_SCROLL4		,
	ITEMID_BOW_MERCENARY_SCROLL5		,
	ITEMID_BOW_MERCENARY_SCROLL6		,
	ITEMID_BOW_MERCENARY_SCROLL7		,
	ITEMID_BOW_MERCENARY_SCROLL8		,
	ITEMID_BOW_MERCENARY_SCROLL9		,
	ITEMID_BOW_MERCENARY_SCROLL10		,
	ITEMID_SWORDMERCENARY_SCROLL1		,
	ITEMID_SWORDMERCENARY_SCROLL2		,
	ITEMID_SWORDMERCENARY_SCROLL3		,
	ITEMID_SWORDMERCENARY_SCROLL4		,
	ITEMID_SWORDMERCENARY_SCROLL5		,
	ITEMID_SWORDMERCENARY_SCROLL6		,
	ITEMID_SWORDMERCENARY_SCROLL7		,
	ITEMID_SWORDMERCENARY_SCROLL8		,
	ITEMID_SWORDMERCENARY_SCROLL9		,
	ITEMID_SWORDMERCENARY_SCROLL10		,
	ITEMID_SPEARMERCENARY_SCROLL1		,
	ITEMID_SPEARMERCENARY_SCROLL2		,
	ITEMID_SPEARMERCENARY_SCROLL3		,
	ITEMID_SPEARMERCENARY_SCROLL4		,
	ITEMID_SPEARMERCENARY_SCROLL5		,
	ITEMID_SPEARMERCENARY_SCROLL6		,
	ITEMID_SPEARMERCENARY_SCROLL7		,
	ITEMID_SPEARMERCENARY_SCROLL8		,
	ITEMID_SPEARMERCENARY_SCROLL9		,
	ITEMID_SPEARMERCENARY_SCROLL10		,
	ITEMID_MERCENARY_RED_POTION			= 12184,
	ITEMID_MERCENARY_BLUE_POTION		= 12185,
	// Cash Food
	ITEMID_STR_DISH10_					= 12202,
	ITEMID_AGI_DISH10_					,
	ITEMID_INT_DISH10_					,
	ITEMID_DEX_DISH10_					,
	ITEMID_LUK_DISH10_					,
	ITEMID_VIT_DISH10_					,
	ITEMID_BATTLE_MANUAL				= 12208,
	ITEMID_BUBBLE_GUM					= 12210,
	ITEMID_GIANT_FLY_WING				= 12212,
	ITEMID_NEURALIZER					= 12213,
	ITEMID_M_CENTER_POTION				= 12241,
	ITEMID_M_AWAKENING_POTION			= 12242,
	ITEMID_M_BERSERK_POTION				= 12243,
	ITEMID_COMP_BATTLE_MANUAL			= 12263,
	ITEMID_COMP_BUBBLE_GUM				= 12264,
	ITEMID_LOVE_ANGEL					= 12287,
	ITEMID_SQUIRREL						= 12288,
	ITEMID_GOGO							= 12289,
	ITEMID_PICTURE_DIARY				= 12304,
	ITEMID_MINI_HEART					= 12305,
	ITEMID_NEWCOMER						= 12306,
	ITEMID_KID							= 12307,
	ITEMID_MAGIC_CASTLE					= 12308,
	ITEMID_BULGING_HEAD					= 12309,
	ITEMID_THICK_BATTLE_MANUAL			= 12312,
	ITEMID_NOVICE_MAGNIFIER				= 12325,
	ITEMID_ANCILLA						= 12333,
	ITEMID_DUN_TELE_SCROLL3				= 12352,
	ITEMID_HP_INCREASE_POTION_SMALL		= 12422,
	ITEMID_HP_INCREASE_POTION_MEDIUM	,
	ITEMID_HP_INCREASE_POTION_LARGE		,
	ITEMID_SP_INCREASE_POTION_SMALL		,
	ITEMID_SP_INCREASE_POTION_MEDIUM	,
	ITEMID_SP_INCREASE_POTION_LARGE		,
	ITEMID_CONCENTRATED_WHITE_POTION_Z	,
	ITEMID_SAVAGE_FULL_ROAST			,
	ITEMID_COCKTAIL_WARG_BLOOD			,
	ITEMID_MINOR_STEW					,
	ITEMID_SIROMA_ICED_TEA				,
	ITEMID_DROSERA_HERB_SALAD			,
	ITEMID_PETITE_TAIL_NOODLES			,
	ITEMID_BLACK_MASS					,
	ITEMID_VITATA500					,
	ITEMID_CONCENTRATED_CEROMAIN_SOUP	,
	ITEMID_CURE_FREE					= 12475,
	ITEMID_BOARDING_HALTER				= 12622,
	ITEMID_NOBLE_NAMEPLATE				= 12705,
	ITEMID_PARALYSIS_POISON				= 12717,
	ITEMID_LEECH_POISON					,
	ITEMID_OBLIVION_POISON				,
	ITEMID_CONTAMINATION_POISON			,
	ITEMID_NUMB_POISON					,
	ITEMID_FEVER_POISON					,
	ITEMID_LAUGHING_POISON				,
	ITEMID_FATIGUE_POISON				,
	ITEMID_NAUTHIZ_RUNE					= 12725,
	ITEMID_RAIDO_RUNE					,
	ITEMID_BERKANA_RUNE					,
	ITEMID_ISA_RUNE						,
	ITEMID_EIHWAZ_RUNE					,
	ITEMID_URUZ_RUNE					,
	ITEMID_THURISAZ_RUNE				,
	ITEMID_PERTHRO_RUNE					,
	ITEMID_HAGALAZ_RUNE					,
	ITEMID_SNOW_FLIP					= 12812,
	ITEMID_PEONY_MOMMY					,
	ITEMID_SLAPPING_HERB				,
	ITEMID_YGGDRASIL_DUST				,
	ITEMID_TREASURE_CHEST_SUMMONED_II	= 12863,
	ITEMID_SILVER_BULLET				= 13201,
	ITEMID_PURIFICATION_BULLET			= 13220,
	// Genetic Created Bombs And Lumps
	ITEMID_APPLE_BOMB					= 13260,
	ITEMID_COCONUT_BOMB					,
	ITEMID_MELON_BOMB					,
	ITEMID_PINEAPPLE_BOMB				,
	ITEMID_BANANA_BOMB					,
	ITEMID_DARK_LUMP					,
	ITEMID_HARD_DARK_LUMP				,
	ITEMID_VERY_HARD_DARK_LUMP			,
	ITEMID_MYSTERIOUS_POWDER			,
	ITEMID_THROW_BOOST500				,
	ITEMID_THROW_FULL_SWING_K			,
	ITEMID_THROW_MANA_PLUS				,
	ITEMID_THROW_CURE_FREE				,
	ITEMID_THROW_MUSTLE_M				,
	ITEMID_THROW_LIFE_FORCE_F			,
	ITEMID_THROW_HP_POTION_SMALL		,
	ITEMID_THROW_HP_POTION_MEDIUM		,
	ITEMID_THROW_HP_POTION_LARGE		,
	ITEMID_THROW_SP_POTION_SMALL		,
	ITEMID_THROW_SP_POTION_MEDIUM		,
	ITEMID_THROW_SP_POTION_LARGE		,
	ITEMID_THROW_EXTRACT_WHITE_POTION_Z	,
	ITEMID_THROW_VITATA_500				,
	ITEMID_THROW_EXTRACT_SALAMINE_JUICE	,
	ITEMID_THROW_SAVAGE_STEAK			,
	ITEMID_THROW_COCKTAIL_WARG_BLOOD	,
	ITEMID_THROW_MINOR_BBQ				,
	ITEMID_THROW_SIROMA_ICE_TEA			,
	ITEMID_THROW_DROCERA_HERB_STEAMED	,
	ITEMID_THROW_PUTTI_TAILS_NOODLES	,
	ITEMID_THROW_OVERDONE_FOOD			,
	ITEMID_PILE_BUNKER_S = 16030		,
	ITEMID_PILE_BUNKER_T				,
	ITEMID_PILE_BUNKER_P				,
	ITEMID_DUN_TELE_SCROLL1				= 14527,
	ITEMID_BATTLE_MANUAL25				= 14532,
	ITEMID_BATTLE_MANUAL100				= 14533,
	ITEMID_BATTLE_MANUAL300				= 14545,
	ITEMID_DUN_TELE_SCROLL2				= 14581,
	ITEMID_WOB_RUNE						= 14582,
	ITEMID_WOB_SCHWALTZ					= 14583,
	ITEMID_WOB_RACHEL					= 14584,
	ITEMID_WOB_LOCAL					= 14585,
	ITEMID_SIEGE_TELEPORT_SCROLL		= 14591,
	ITEMID_JOB_MANUAL50					= 14592,
	// Mechanic's Engine Pile Bunker
	ITEMID_ENGINE_PILE_BUNKER			= 16092,
	ITEMID_PARA_TEAM_MARK				= 22508,
	ITEMID_LUX_ANIMA_RUNE				= 22540,
};

#define itemid_isgemstone(id) ( (id) >= ITEMID_YELLOW_GEMSTONE && (id) <= ITEMID_BLUE_GEMSTONE )
#define itemdb_iscashfood(id) ((id) >= ITEMID_STR_DISH10_ && (id) <= ITEMID_VIT_DISH10_)

#define itemid_is_rune(id) ((id) >= ITEMID_NAUTHIZ_RUNE && (id) <= ITEMID_HAGALAZ_RUNE || (id) == ITEMID_LUX_ANIMA_RUNE)
#define itemid_is_spell_book(id) ((id) >= ITEMID_MAGICBOOK_FIREBOLT && (id) <= ITEMID_MAGICBOOK_DRAINLIFE)
#define itemid_is_pile_bunker(id) ((id) == ITEMID_PILE_BUNKER || (id) >= ITEMID_PILE_BUNKER_S && (id) <= ITEMID_PILE_BUNKER_P || (id) == ITEMID_ENGINE_PILE_BUNKER)
#define itemid_is_cooling_system(id) ((id) == ITEMID_COOLING_DEVICE || (id) == ITEMID_HIGH_QUALITY_COOLER || (id) == ITEMID_SPECIAL_COOLER)
#define itemid_is_guillotine_poison(id) ((id) >= ITEMID_PARALYSIS_POISON && (id) <= ITEMID_FATIGUE_POISON)
#define itemid_is_element_point(id) ((id) >= ITEMID_SCARLETT_POINT && (id) <= ITEMID_LIME_GREEN_POINT)
#define itemid_is_eclage_cures(id) ((id) >= ITEMID_SNOW_FLIP && (id) <= ITEMID_YGGDRASIL_DUST)
#define itemid_is_sling_atk(id) ((id) >= ITEMID_APPLE_BOMB && (id) <= ITEMID_VERY_HARD_DARK_LUMP)
#define itemid_is_sling_buff(id) ((id) >= ITEMID_MYSTERIOUS_POWDER && (id) <= ITEMID_THROW_OVERDONE_FOOD)
#define itemid_is_holy_bullet(id) ( (id) == ITEMID_SILVER_BULLET || (id) == ITEMID_PURIFICATION_BULLET )
#define itemid_is_mado_fuel(id) ( (id) == ITEMID_MAGIC_GEAR_FUEL )

//The only item group required by the code to be known. See const.txt for the full list.
#define IG_FINDINGORE 6
#define IG_POTION 37
#define IG_TOKEN_OF_SIEGFRIED 60

#define MAX_RANDGROUP 4 // For item packages

#define CARD0_FORGE 0x00FF
#define CARD0_CREATE 0x00FE
#define CARD0_PET 0x0100

//Marks if the card0 given is "special" (non-item id used to mark pets/created items. [Skotlex]
#define itemdb_isspecial(i) (i == CARD0_FORGE || i == CARD0_CREATE || i == CARD0_PET)

//Use apple for unknown items.
#define UNKNOWN_ITEM_ID 512

/// Struct of Roulette db
struct s_roulette_db
{
	unsigned short *nameid[MAX_ROULETTE_LEVEL], /// Item ID
		           *qty[MAX_ROULETTE_LEVEL]; /// Amount of Item ID
	int *flag[MAX_ROULETTE_LEVEL]; /// Whether the item is for loss or win
	int items[MAX_ROULETTE_LEVEL]; /// Number of items in the list for each
} rd;

enum {
	NOUSE_SITTING = 0x01,
} item_nouse_list;

struct item_data {
	unsigned short nameid;
	char name[ITEM_NAME_LENGTH],jname[ITEM_NAME_LENGTH];
	//Do not add stuff between value_buy and wlv (see how getiteminfo works)
	int value_buy;
	int value_sell;
	int type;
	int maxchance; //For logs, for external game info, for scripts: Max drop chance of this item (e.g. 0.01% , etc.. if it = 0, then monsters don't drop it, -1 denotes items sold in shops only) [Lupus]
	int sex;
	int equip;
	int weight;
	int atk;
	int def;
	int range;
	int slot;
	int look;
	int elv,elv_max;
	int wlv;
	int view_id;
	int delay;
//Lupus: I rearranged order of these fields due to compatibility with ITEMINFO script command
//		some script commands should be revised as well...
	unsigned int class_base[3];	//Specifies if the base can wear this item (split in 3 indexes per type: 1-1, 2-1, 2-2)
	unsigned class_upper : 4; //Specifies if the upper-type can equip it (bitfield, 1: normal, 2: upper, 3: baby, 4: third)
	struct {
		unsigned short chance;
		int id;
	} mob[MAX_SEARCH]; //Holds the mobs that have the highest drop rate for this item. [Skotlex]
	struct script_code *script;	//Default script for everything.
	struct script_code *equip_script;	//Script executed once when equipping.
	struct script_code *unequip_script;//Script executed once when unequipping.
	struct {
		unsigned available : 1;
		uint32 no_equip;
		unsigned no_refine : 1;	// [celest]
		unsigned delay_consume : 1;	// Signifies items that are not consumed immediately upon double-click [Skotlex]
		unsigned trade_restriction : 9;	//Item restrictions mask [Skotlex]
		unsigned autoequip: 1;
		unsigned buyingstore : 1;
		unsigned dead_branch : 1; // As dead branch item. Logged at `branchlog` table and cannot be used at 'nobranch' mapflag [Cydh]
		unsigned group : 1; // As item group container [Cydh]
		unsigned guid : 1; // This item always be attached with GUID and make it as bound item! [Cydh]
		unsigned broadcast : 1; ///< Will be broadcasted if someone obtain the item [Cydh]
		unsigned fixed_drop : 1;
		bool bindOnEquip; ///< Set item as bound when equipped
		uint8 dropEffect; ///< Drop Effect Mode
	} flag;
	struct {// used by item_nouse.txt
		unsigned int flag;
		unsigned short override;
	} item_usage;
	short gm_lv_trade_override;	//GM-level to override trade_restriction
	enum sc_type delay_sc; ///< Use delay group if any instead using player's item_delay data [Cydh]
};

struct item_group {
	uint16 id;
	uint16 qty;
	unsigned short nameid[MAX_RANDITEM];
};

struct item_package {
	uint16 id;
	struct item_package_entry *entry[MAX_RANDITEM];
	uint16 count; // total count of entry
	unsigned short max_rand;
};

struct item_package_entry {
	unsigned short nameid;
	unsigned short prob; // Rate
	unsigned short amount;
	unsigned short group;
	bool announced;
	bool guid;
	unsigned short duration;
};

// Struct for item random option [Secret]
struct s_random_opt_data
{
	unsigned short id;
	struct script_code *script;
};

/* attendance data */
struct attendance_entry {
	int day;
	int nameid;
	int qty;
};
VECTOR_DECL(struct attendance_entry) attendance_data;

struct item_data* itemdb_searchname(const char *name);
int itemdb_searchname_array(struct item_data** data, int size, const char *str);
struct item_data* itemdb_search(unsigned short nameid);
struct item_data* itemdb_exists(unsigned short nameid);
struct s_random_opt_data* itemdb_randomopt_exists(short id);
#define itemdb_name(n) itemdb_search(n)->name
#define itemdb_jname(n) itemdb_search(n)->jname
#define itemdb_type(n) itemdb_search(n)->type
#define itemdb_atk(n) itemdb_search(n)->atk
#define itemdb_def(n) itemdb_search(n)->def
#define itemdb_look(n) itemdb_search(n)->look
#define itemdb_weight(n) itemdb_search(n)->weight
#define itemdb_equip(n) itemdb_search(n)->equip
#define itemdb_usescript(n) itemdb_search(n)->script
#define itemdb_equipscript(n) itemdb_search(n)->script
#define itemdb_wlv(n) itemdb_search(n)->wlv
#define itemdb_range(n) itemdb_search(n)->range
#define itemdb_slot(n) itemdb_search(n)->slot
#define itemdb_available(n) (itemdb_search(n)->flag.available)
#define itemdb_traderight(n) (itemdb_search(n)->flag.trade_restriction)
#define itemdb_viewid(n) (itemdb_search(n)->view_id)
#define itemdb_autoequip(n) (itemdb_search(n)->flag.autoequip)
#define itemdb_dropeffect(n) (itemdb_search(n)->flag.dropEffect)
const char* itemdb_typename(enum item_types type);
const char* itemdb_weapon_typename(int type);
const char* itemdb_armor_typename(int type);
const char* itemdb_ammo_typename(int type);

int itemdb_searchrandomid(uint16 group_id);

#define itemdb_value_buy(n) itemdb_search(n)->value_buy
#define itemdb_value_sell(n) itemdb_search(n)->value_sell
#define itemdb_canrefine(n) (!itemdb_search(n)->flag.no_refine)
//Item trade restrictions [Skotlex]
bool itemdb_isdropable_sub(struct item_data *, int, int);
bool itemdb_cantrade_sub(struct item_data*, int, int);
bool itemdb_canpartnertrade_sub(struct item_data*, int, int);
bool itemdb_cansell_sub(struct item_data*,int, int);
bool itemdb_cancartstore_sub(struct item_data*, int, int);
bool itemdb_canstore_sub(struct item_data*, int, int);
bool itemdb_canguildstore_sub(struct item_data*, int, int);
bool itemdb_canmail_sub(struct item_data*, int, int);
bool itemdb_canauction_sub(struct item_data*, int, int);
bool itemdb_isrestricted(struct item* item, int gmlv, int gmlv2, bool (*func)(struct item_data*, int, int));
bool itemdb_ishatched_egg(struct item* item);
#define itemdb_isdropable(item, gmlv) itemdb_isrestricted(item, gmlv, 0, itemdb_isdropable_sub)
#define itemdb_cantrade(item, gmlv, gmlv2) itemdb_isrestricted(item, gmlv, gmlv2, itemdb_cantrade_sub)
#define itemdb_canpartnertrade(item, gmlv, gmlv2) itemdb_isrestricted(item, gmlv, gmlv2, itemdb_canpartnertrade_sub)
#define itemdb_cansell(item, gmlv) itemdb_isrestricted(item, gmlv, 0, itemdb_cansell_sub)
#define itemdb_cancartstore(item, gmlv)  itemdb_isrestricted(item, gmlv, 0, itemdb_cancartstore_sub)
#define itemdb_canstore(item, gmlv) itemdb_isrestricted(item, gmlv, 0, itemdb_canstore_sub) 
#define itemdb_canguildstore(item, gmlv) itemdb_isrestricted(item , gmlv, 0, itemdb_canguildstore_sub) 
#define itemdb_canmail(item, gmlv) itemdb_isrestricted(item , gmlv, 0, itemdb_canmail_sub)
#define itemdb_canauction(item, gmlv) itemdb_isrestricted(item , gmlv, 0, itemdb_canauction_sub)

bool itemdb_isequip2(struct item_data *id);
#define itemdb_isequip(nameid) itemdb_isequip2(itemdb_search(nameid))
char itemdb_isidentified(unsigned short);
bool itemdb_isstackable2(struct item_data *id);
#define itemdb_isstackable(nameid) itemdb_isstackable2(itemdb_search(nameid))

bool itemdb_parse_roulette_db(void);

void itemdb_package_item(struct map_session_data *sd, int packageid);

struct item_group *itemdb_group_exists(unsigned short group_id);
struct item_package *itemdb_package_exists(unsigned short package_id);
int itemdb_group_item_exists_pc(struct map_session_data *sd, unsigned short group_id);

bool itemdb_isNoEquip(struct item_data *id, uint16 m);

void itemdb_reload(void);

void do_final_itemdb(void);
void do_init_itemdb(void);

#endif /* _ITEMDB_H_ */
