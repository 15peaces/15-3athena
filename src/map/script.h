// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _SCRIPT_H_
#define _SCRIPT_H_

#define NUM_WHISPER_VAR 10

/// Composes the uid of a reference from the id and the index
#define reference_uid(id,idx) ( (int32)((((uint32)(id)) & 0x00ffffff) | (((uint32)(idx)) << 24)) )

#define script_getvarid(var) ( (int32)(int64)(var & 0xFFFFFFFF) )

struct map_session_data;

extern int potion_flag; //For use on Alchemist improved potions/Potion Pitcher. [Skotlex]
extern int potion_hp, potion_per_hp, potion_sp, potion_per_sp;
extern int potion_target;

extern struct Script_Config {
	unsigned warn_func_mismatch_argtypes : 1;
	unsigned warn_func_mismatch_paramnum : 1;
	int check_cmdcount;
	int check_gotocount;
	int input_min_value;
	int input_max_value;

	const char *die_event_name;
	const char *kill_pc_event_name;
	const char *kill_mob_event_name;
	const char *login_event_name;
	const char *logout_event_name;
	const char *loadmap_event_name;
	const char *baselvup_event_name;
	const char *joblvup_event_name;

	const char* ontouch_name;
	const char* ontouch2_name;
} script_config;

typedef enum c_op {
	C_NOP, // end of script/no value (nil)
	C_POS,
	C_INT, // number
	C_PARAM, // parameter variable (see pc_readparam/pc_setparam)
	C_FUNC, // buildin function call
	C_STR, // string (free'd automatically)
	C_CONSTSTR, // string (not free'd)
	C_ARG, // start of argument list
	C_NAME,
	C_EOL, // end of line (extra stack values are cleared)
	C_RETINFO,
	C_USERFUNC, // internal script function
	C_USERFUNC_POS, // internal script function label

	// operators
	C_OP3, // a ? b : c
	C_LOR, // a || b
	C_LAND, // a && b
	C_LE, // a <= b
	C_LT, // a < b
	C_GE, // a >= b
	C_GT, // a > b
	C_EQ, // a == b
	C_NE, // a != b
	C_XOR, // a ^ b
	C_OR, // a | b
	C_AND, // a & b
	C_ADD, // a + b
	C_SUB, // a - b
	C_MUL, // a * b
	C_DIV, // a / b
	C_MOD, // a % b
	C_NEG, // - a
	C_LNOT, // ! a
	C_NOT, // ~ a
	C_R_SHIFT, // a >> b
	C_L_SHIFT // a << b
} c_op;

struct script_retinfo {
	struct linkdb_node** var_function;// scope variables
	struct script_code* script;// script code
	int pos;// script location
	int nargs;// argument count
	int defsp;// default stack pointer
};

struct script_data {
	enum c_op type;
	union script_data_val {
		int64 num;
		char *str;
		struct script_retinfo* ri;
	} u;
	struct linkdb_node** ref;
};

// Moved defsp from script_state to script_stack since
// it must be saved when script state is RERUNLINE. [Eoe / jA 1094]
struct script_code {
	int script_size;
	unsigned char* script_buf;
	struct linkdb_node* script_vars;
};

struct script_stack {
	int sp;// number of entries in the stack
	int sp_max;// capacity of the stack
	int defsp;
	struct script_data *stack_data;// stack
	struct linkdb_node** var_function;// scope variables
};

/**
 * Data structure to represent a script queue.
 * Thanks to hercules.
 */
struct script_queue
{
	int id;                              ///< Queue identifier
	VECTOR_DECL(int) entries;            ///< Items in the queue.
	bool valid;                          ///< Whether the queue is valid.
	char event_logout[EVENT_NAME_LENGTH];    ///< Logout event
};

VECTOR_DECL(struct script_queue) sc_queue;

//
// Script state
//
enum e_script_state { RUN,STOP,END,RERUNLINE,GOTO,RETFUNC };

struct script_state {
	struct script_stack* stack;
	int start,end;
	int pos;
	enum e_script_state state;
	int rid,oid;
	struct script_code *script, *scriptroot;
	struct sleep_data {
		int tick,timer,charid;
	} sleep;
	int instance_id;
	//For backing up purposes
	struct script_state *bk_st;
	int bk_npcid;
	unsigned freeloop : 1;// used by buildin_freeloop
	char* funcname; // Stores the current running function name
};

struct script_reg {
	int64 index;
	int64 data;
};

struct script_regstr {
	int64 index;
	char* data;
};

enum script_parse_options {
	SCRIPT_USE_LABEL_DB = 0x1,// records labels in scriptlabel_db
	SCRIPT_IGNORE_EXTERNAL_BRACKETS = 0x2,// ignores the check for {} brackets around the script
	SCRIPT_RETURN_EMPTY_SCRIPT = 0x4// returns the script object instead of NULL for empty scripts
};

enum questinfo_types {
	QTYPE_QUEST = 0,
	QTYPE_QUEST2,
	QTYPE_JOB,
	QTYPE_JOB2,
	QTYPE_EVENT,
	QTYPE_EVENT2,
	QTYPE_WARG,
	// 7 = free
	QTYPE_WARG2 = 8,
	// 9 - 9998 = free
	QTYPE_NONE = 9999
};

enum unitdata_mobtypes {
	UMOB_SIZE = 0,
	UMOB_LEVEL,
	UMOB_HP,
	UMOB_MAXHP,
	UMOB_MASTERAID,
	UMOB_MAPID,
	UMOB_X,
	UMOB_Y,
	UMOB_SPEED,
	UMOB_MODE,
	UMOB_AI,
	UMOB_SCOPTION,
	UMOB_SEX,
	UMOB_CLASS,
	UMOB_HAIRSTYLE,
	UMOB_HAIRCOLOR,
	UMOB_HEADBOTTOM,
	UMOB_HEADMIDDLE,
	UMOB_HEADTOP,
	UMOB_CLOTHCOLOR,
	UMOB_SHIELD,
	UMOB_WEAPON,
	UMOB_LOOKDIR,
	UMOB_STR,
	UMOB_AGI,
	UMOB_VIT,
	UMOB_INT,
	UMOB_DEX,
	UMOB_LUK,
	UMOB_SLAVECPYMSTRMD,
	UMOB_DMGIMMUNE,
	UMOB_ATKRANGE,
	UMOB_ATK,
	UMOB_MATK,
	UMOB_DEF,
	UMOB_MDEF,
	UMOB_HIT,
	UMOB_FLEE,
	UMOB_PDODGE,
	UMOB_CRIT,
	UMOB_RACE,
	UMOB_ELETYPE,
	UMOB_ELELEVEL,
	UMOB_AMOTION,
	UMOB_ADELAY,
	UMOB_DMOTION,
};

enum unitdata_homuntypes {
	UHOM_SIZE = 0,
	UHOM_LEVEL,
	UHOM_HP,
	UHOM_MAXHP,
	UHOM_SP,
	UHOM_MAXSP,
	UHOM_MASTERCID,
	UHOM_MAPID,
	UHOM_X,
	UHOM_Y,
	UHOM_HUNGER,
	UHOM_INTIMACY,
	UHOM_SPEED,
	UHOM_LOOKDIR,
	UHOM_CANMOVETICK,
	UHOM_STR,
	UHOM_AGI,
	UHOM_VIT,
	UHOM_INT,
	UHOM_DEX,
	UHOM_LUK,
	UHOM_DMGIMMUNE,
	UHOM_ATKRANGE,
	UHOM_ATK,
	UHOM_MATK,
	UHOM_DEF,
	UHOM_MDEF,
	UHOM_HIT,
	UHOM_FLEE,
	UHOM_PDODGE,
	UHOM_CRIT,
	UHOM_RACE,
	UHOM_ELETYPE,
	UHOM_ELELEVEL,
	UHOM_AMOTION,
	UHOM_ADELAY,
	UHOM_DMOTION,
};

enum unitdata_pettypes {
	UPET_SIZE = 0,
	UPET_LEVEL,
	UPET_HP,
	UPET_MAXHP,
	UPET_MASTERAID,
	UPET_MAPID,
	UPET_X,
	UPET_Y,
	UPET_HUNGER,
	UPET_INTIMACY,
	UPET_SPEED,
	UPET_LOOKDIR,
	UPET_CANMOVETICK,
	UPET_STR,
	UPET_AGI,
	UPET_VIT,
	UPET_INT,
	UPET_DEX,
	UPET_LUK,
	UPET_DMGIMMUNE,
	UPET_ATKRANGE,
	UPET_ATK,
	UPET_MATK,
	UPET_DEF,
	UPET_MDEF,
	UPET_HIT,
	UPET_FLEE,
	UPET_PDODGE,
	UPET_CRIT,
	UPET_RACE,
	UPET_ELETYPE,
	UPET_ELELEVEL,
	UPET_AMOTION,
	UPET_ADELAY,
	UPET_DMOTION,
};

enum unitdata_merctypes {
	UMER_SIZE = 0,
	UMER_HP,
	UMER_MAXHP,
	UMER_MASTERCID,
	UMER_MAPID,
	UMER_X,
	UMER_Y,
	UMER_KILLCOUNT,
	UMER_LIFETIME,
	UMER_SPEED,
	UMER_LOOKDIR,
	UMER_CANMOVETICK,
	UMER_STR,
	UMER_AGI,
	UMER_VIT,
	UMER_INT,
	UMER_DEX,
	UMER_LUK,
	UMER_DMGIMMUNE,
	UMER_ATKRANGE,
	UMER_ATK,
	UMER_MATK,
	UMER_DEF,
	UMER_MDEF,
	UMER_HIT,
	UMER_FLEE,
	UMER_PDODGE,
	UMER_CRIT,
	UMER_RACE,
	UMER_ELETYPE,
	UMER_ELELEVEL,
	UMER_AMOTION,
	UMER_ADELAY,
	UMER_DMOTION,
};

enum unitdata_elemtypes {
	UELE_SIZE = 0,
	UELE_HP,
	UELE_MAXHP,
	UELE_SP,
	UELE_MAXSP,
	UELE_MASTERCID,
	UELE_MAPID,
	UELE_X,
	UELE_Y,
	UELE_LIFETIME,
	UELE_MODE,
	UELE_SPEED,
	UELE_LOOKDIR,
	UELE_CANMOVETICK,
	UELE_STR,
	UELE_AGI,
	UELE_VIT,
	UELE_INT,
	UELE_DEX,
	UELE_LUK,
	UELE_DMGIMMUNE,
	UELE_ATKRANGE,
	UELE_ATK,
	UELE_MATK,
	UELE_DEF,
	UELE_MDEF,
	UELE_HIT,
	UELE_FLEE,
	UELE_PDODGE,
	UELE_CRIT,
	UELE_RACE,
	UELE_ELETYPE,
	UELE_ELELEVEL,
	UELE_AMOTION,
	UELE_ADELAY,
	UELE_DMOTION,
};

enum unitdata_npctypes {
	UNPC_DISPLAY = 0,
	UNPC_LEVEL,
	UNPC_HP,
	UNPC_MAXHP,
	UNPC_MAPID,
	UNPC_X,
	UNPC_Y,
	UNPC_LOOKDIR,
	UNPC_STR,
	UNPC_AGI,
	UNPC_VIT,
	UNPC_INT,
	UNPC_DEX,
	UNPC_LUK,
	UNPC_PLUSALLSTAT,
	UNPC_DMGIMMUNE,
	UNPC_ATKRANGE,
	UNPC_ATK,
	UNPC_MATK,
	UNPC_DEF,
	UNPC_MDEF,
	UNPC_HIT,
	UNPC_FLEE,
	UNPC_PDODGE,
	UNPC_CRIT,
	UNPC_RACE,
	UNPC_ELETYPE,
	UNPC_ELELEVEL,
	UNPC_AMOTION,
	UNPC_ADELAY,
	UNPC_DMOTION,
};

enum navigation_service {
	NAV_NONE = 0, ///< 0
	NAV_AIRSHIP_ONLY = 1, ///< 1 (actually 1-9)
	NAV_SCROLL_ONLY = 10, ///< 10
	NAV_AIRSHIP_AND_SCROLL = NAV_AIRSHIP_ONLY + NAV_SCROLL_ONLY, ///< 11 (actually 11-99)
	NAV_KAFRA_ONLY = 100, ///< 100
	NAV_KAFRA_AND_AIRSHIP = NAV_KAFRA_ONLY + NAV_AIRSHIP_ONLY, ///< 101 (actually 101-109)
	NAV_KAFRA_AND_SCROLL = NAV_KAFRA_ONLY + NAV_SCROLL_ONLY, ///< 110
	NAV_ALL = NAV_AIRSHIP_ONLY + NAV_SCROLL_ONLY + NAV_KAFRA_ONLY ///< 111 (actually 111-255)
};

enum random_option_attribute {
	ROA_ID = 0,
	ROA_VALUE,
	ROA_PARAM,
};

enum e_hat_effects {
	HAT_EF_MIN = 0,
	HAT_EF_BLOSSOM_FLUTTERING,
	HAT_EF_MERMAID_LONGING,
	HAT_EF_RL_BANISHING_BUSTER,
	HAT_EF_LJOSALFAR,
	HAT_EF_CLOCKING,
	HAT_EF_SNOW,
	HAT_EF_MAKEBLUR,
	HAT_EF_SLEEPATTACK,
	HAT_EF_GUMGANG,
	HAT_EF_TALK_FROSTJOKE,
	HAT_EF_DEMONSTRATION,
	HAT_EF_FLUTTER_BUTTERFLY,
	HAT_EF_ANGEL_FLUTTERING,
	HAT_EF_BLESSING_OF_ANGELS,
	HAT_EF_ELECTRIC,
	HAT_EF_GREEN_FLOOR,
	HAT_EF_SHRINK,
	HAT_EF_VALHALLA_IDOL,
	HAT_EF_ANGEL_STAIRS,
	HAT_EF_GLOW_OF_NEW_YEAR,
	HAT_EF_BOTTOM_FORTUNEKISS,
	HAT_EF_PINKBODY,
	HAT_EF_DOUBLEGUMGANG,
	HAT_EF_GIANTBODY,
	HAT_EF_GREEN99_6,
	HAT_EF_CIRCLEPOWER,
	HAT_EF_BOTTOM_BLOODYLUST,
	HAT_EF_WATER_BELOW,
	HAT_EF_LEVEL99_150,
	HAT_EF_YELLOWFLY3,
	HAT_EF_KAGEMUSYA,
	HAT_EF_CHERRYBLOSSOM,
	HAT_EF_STRANGELIGHTS,
	HAT_EF_WL_TELEKINESIS_INTENSE,
	HAT_EF_AB_OFFERTORIUM_RING,
	HAT_EF_WHITEBODY2,
	HAT_EF_SAKURA,
	HAT_EF_CLOUD2,
	HAT_EF_FEATHER_FLUTTERING,
	HAT_EF_CAMELLIA_HAIR_PIN,
	HAT_EF_JP_EV_EFFECT01,
	HAT_EF_JP_EV_EFFECT02,
	HAT_EF_JP_EV_EFFECT03,
	HAT_EF_FLORAL_WALTZ,
	HAT_EF_MAGICAL_FEATHER,
	HAT_EF_HAT_EFFECT,
	HAT_EF_BAKURETSU_HADOU,
	HAT_EF_GOLD_SHOWER,
	HAT_EF_WHITEBODY,
	HAT_EF_WATER_BELOW2,
	HAT_EF_FIREWORK,
	HAT_EF_RETURN_TW_1ST_HAT,
	HAT_EF_C_FLUTTERBUTTERFLY_BL,
	HAT_EF_QSCARABA,
	HAT_EF_FSTONE,
	HAT_EF_MAGICCIRCLE,
	HAT_EF_GODCLASS,
	HAT_EF_GODCLASS2,
	HAT_EF_LEVEL99_RED,
	HAT_EF_LEVEL99_ULTRAMARINE,
	HAT_EF_LEVEL99_CYAN,
	HAT_EF_LEVEL99_LIME,
	HAT_EF_LEVEL99_VIOLET,
	HAT_EF_LEVEL99_LILAC,
	HAT_EF_LEVEL99_SUN_ORANGE,
	HAT_EF_LEVEL99_DEEP_PINK,
	HAT_EF_LEVEL99_BLACK,
	HAT_EF_LEVEL99_WHITE,
	HAT_EF_LEVEL160_RED,
	HAT_EF_LEVEL160_ULTRAMARINE,
	HAT_EF_LEVEL160_CYAN,
	HAT_EF_LEVEL160_LIME,
	HAT_EF_LEVEL160_VIOLET,
	HAT_EF_LEVEL160_LILAC,
	HAT_EF_LEVEL160_SUN_ORANGE,
	HAT_EF_LEVEL160_DEEP_PINK,
	HAT_EF_LEVEL160_BLACK,
	HAT_EF_LEVEL160_WHITE,
	HAT_EF_FULL_BLOOMCHERRY_TREE,
	HAT_EF_C_BLESSINGS_OF_SOUL,
	HAT_EF_MANYSTARS,
	HAT_EF_SUBJECT_AURA_GOLD,
	HAT_EF_SUBJECT_AURA_WHITE,
	HAT_EF_SUBJECT_AURA_RED,
	HAT_EF_C_SHINING_ANGEL_WING,
	HAT_EF_MAGIC_STAR_TW,
	HAT_EF_DIGITAL_SPACE,
	HAT_EF_SLEIPNIR,
	HAT_EF_C_MAPLE_WHICH_FALLS_RD,
	HAT_EF_MAGICCIRCLERAINBOW,
	HAT_EF_SNOWFLAKE_TIARA,
	HAT_EF_MIDGARTS_GLORY,
	HAT_EF_LEVEL99_TIGER,
	HAT_EF_LEVEL160_TIGER,
	HAT_EF_FLUFFYWING,
	HAT_EF_C_GHOST_EFFECT,
	HAT_EF_C_POPPING_PORING_AURA,
	HAT_EF_RESONATETAEGO,
	HAT_EF_99LV_RUNE_RED,
	HAT_EF_99LV_ROYAL_GUARD_BLUE,
	HAT_EF_99LV_WARLOCK_VIOLET,
	HAT_EF_99LV_SORCERER_LBLUE,
	HAT_EF_99LV_RANGER_GREEN,
	HAT_EF_99LV_MINSTREL_PINK,
	HAT_EF_99LV_ARCHBISHOP_WHITE,
	HAT_EF_99LV_GUILL_SILVER,
	HAT_EF_99LV_SHADOWC_BLACK,
	HAT_EF_99LV_MECHANIC_GOLD,
	HAT_EF_99LV_GENETIC_YGREEN,
	HAT_EF_160LV_RUNE_RED,
	HAT_EF_160LV_ROYAL_G_BLUE,
	HAT_EF_160LV_WARLOCK_VIOLET,
	HAT_EF_160LV_SORCERER_LBLUE,
	HAT_EF_160LV_RANGER_GREEN,
	HAT_EF_160LV_MINSTREL_PINK,
	HAT_EF_160LV_ARCHB_WHITE,
	HAT_EF_160LV_GUILL_SILVER,
	HAT_EF_160LV_SHADOWC_BLACK,
	HAT_EF_160LV_MECHANIC_GOLD,
	HAT_EF_160LV_GENETIC_YGREEN,
	HAT_EF_WATER_BELOW3,
	HAT_EF_WATER_BELOW4,
	HAT_EF_C_VALKYRIE_WING,
	HAT_EF_MAX
};

const char* skip_space(const char* p);
void script_error(const char* src, const char* file, int start_line, const char* error_msg, const char* error_pos);

bool is_number(const char *p);
struct script_code* parse_script(const char* src,const char* file,int line,int options);
void run_script_sub(struct script_code *rootscript,int pos,int rid,int oid, char* file, int lineno);
void run_script(struct script_code*,int,int,int);

int set_var(struct map_session_data *sd, char *name, void *val);
int conv_num(struct script_state *st,struct script_data *data);
const char* conv_str(struct script_state *st,struct script_data *data);
int run_script_timer(int tid, int64 tick, int id, intptr_t data);
void run_script_main(struct script_state *st);

void script_stop_sleeptimers(int id);
struct linkdb_node* script_erase_sleepdb(struct linkdb_node *n);
void script_free_code(struct script_code* code);
void script_free_vars(struct linkdb_node **node);
struct script_state* script_alloc_state(struct script_code* script, int pos, int rid, int oid);
void script_free_state(struct script_state* st);

struct DBMap* script_get_label_db(void);
struct DBMap* script_get_userfunc_db(void);
void script_run_autobonus(const char *autobonus,int id, int pos);

bool script_get_parameter(const char* name, int* value);
bool script_get_constant(const char* name, int* value);
void script_set_constant_(const char* name, int value, const char* constant_name, bool isparameter);
#define script_set_constant(name, value, isparameter) script_set_constant_(name, value, NULL, isparameter)

void script_cleararray_pc(struct map_session_data* sd, const char* varname, void* value);
void script_setarray_pc(struct map_session_data* sd, const char* varname, uint8 idx, void* value, int* refcache);

int script_config_read(char *cfgName);
int do_init_script(void);
int do_final_script(void);
int add_str(const char* p);
const char* get_str(int id);
int script_reload(void);

// @commands (script based) 
void setd_sub(struct script_state *st, TBL_PC *sd, const char *varname, int elem, void *value, struct linkdb_node **ref); 

// Script Queue
struct script_queue *script_queue_get(int idx);
bool script_queue_add(int idx, int var);
bool script_queue_remove(int idx, int var);
int script_queue_create(void);
bool script_queue_clear(int idx);

#endif /* _SCRIPT_H_ */
