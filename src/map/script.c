// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

//#define DEBUG_DISP
//#define DEBUG_DISASM
//#define DEBUG_RUN
//#define DEBUG_HASH
//#define DEBUG_DUMP_STACK

#include "../common/cbasetypes.h"
#include "../common/malloc.h"
#include "../common/md5calc.h"
#include "../common/lock.h"
#include "../common/nullpo.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/timer.h"
#include "../common/utils.h"

#include "map.h"
#include "path.h"
#include "clan.h"
#include "clif.h"
#include "chrif.h"
#include "itemdb.h"
#include "pc.h"
#include "status.h"
#include "storage.h"
#include "mob.h"
#include "npc.h"
#include "pet.h"
#include "mapreg.h"
#include "homunculus.h"
#include "instance.h"
#include "mercenary.h"
#include "elemental.h"
#include "intif.h"
#include "skill.h"
#include "status.h"
#include "chat.h"
#include "battle.h"
#include "battleground.h"
#include "party.h"
#include "guild.h"
#include "guild_castle.h"
#include "guild_expcache.h"
#include "atcommand.h"
#include "log.h"
#include "unit.h"
#include "pet.h"
#include "mail.h"
#include "script.h"
#include "quest.h"
#include "achievement.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef WIN32
	#include <sys/time.h>
#endif
#include <time.h>
#include <setjmp.h>
#include <errno.h>


///////////////////////////////////////////////////////////////////////////////
//## TODO possible enhancements: [FlavioJS]
// - 'callfunc' supporting labels in the current npc "::LabelName"
// - 'callfunc' supporting labels in other npcs "NpcName::LabelName"
// - 'function FuncName;' function declarations reverting to global functions 
//   if local label isn't found
// - join callfunc and callsub's functionality
// - remove dynamic allocation in add_word()
// - remove GETVALUE / SETVALUE
// - clean up the set_reg / set_val / setd_sub mess
// - detect invalid label references at parse-time

//
// struct script_state* st;
//

/// Returns the script_data at the target index
#define script_getdata(st,i) ( &((st)->stack->stack_data[(st)->start + (i)]) )
/// Returns if the stack contains data at the target index
#define script_hasdata(st,i) ( (st)->end > (st)->start + (i) )
/// Returns the index of the last data in the stack
#define script_lastdata(st) ( (st)->end - (st)->start - 1 )
/// Pushes an int into the stack
#define script_pushint(st,val) push_val((st)->stack, C_INT, (val))
/// Pushes a string into the stack (script engine frees it automatically)
#define script_pushstr(st,val) push_str((st)->stack, C_STR, (val))
/// Pushes a copy of a string into the stack
#define script_pushstrcopy(st,val) push_str((st)->stack, C_STR, aStrdup(val))
/// Pushes a constant string into the stack (must never change or be freed)
#define script_pushconststr(st,val) push_str((st)->stack, C_CONSTSTR, (val))
/// Pushes a nil into the stack
#define script_pushnil(st) push_val((st)->stack, C_NOP, 0)
/// Pushes a copy of the data in the target index
#define script_pushcopy(st,i) push_copy((st)->stack, (st)->start + (i))

#define script_isstring(st,i) data_isstring(script_getdata(st,i))
#define script_isint(st,i) data_isint(script_getdata(st,i))

#define script_getnum(st,val) conv_num(st, script_getdata(st,val))
#define script_getstr(st,val) conv_str(st, script_getdata(st,val))
#define script_getref(st,val) ( script_getdata(st,val)->ref )

// Returns name of currently running function 
#define script_getfuncname(st) ( st->funcname )

// Note: "top" functions/defines use indexes relative to the top of the stack
//       -1 is the index of the data at the top

/// Returns the script_data at the target index relative to the top of the stack
#define script_getdatatop(st,i) ( &((st)->stack->stack_data[(st)->stack->sp + (i)]) )
/// Pushes a copy of the data in the target index relative to the top of the stack
#define script_pushcopytop(st,i) push_copy((st)->stack, (st)->stack->sp + (i))
/// Removes the range of values [start,end[ relative to the top of the stack
#define script_removetop(st,start,end) ( pop_stack((st), ((st)->stack->sp + (start)), (st)->stack->sp + (end)) )

//
// struct script_data* data;
//

/// Returns if the script data is a string
#define data_isstring(data) ( (data)->type == C_STR || (data)->type == C_CONSTSTR )
/// Returns if the script data is an int
#define data_isint(data) ( (data)->type == C_INT )
/// Returns if the script data is a reference
#define data_isreference(data) ( (data)->type == C_NAME )
/// Returns if the script data is a label
#define data_islabel(data) ( (data)->type == C_POS )
/// Returns if the script data is an internal script function label
#define data_isfunclabel(data) ( (data)->type == C_USERFUNC_POS )

/// Returns if this is a reference to a constant
#define reference_toconstant(data) ( str_data[reference_getid(data)].type == C_INT )
/// Returns if this a reference to a param
#define reference_toparam(data) ( str_data[reference_getid(data)].type == C_PARAM )
/// Returns if this a reference to a variable
//##TODO confirm it's C_NAME [FlavioJS]
#define reference_tovariable(data) ( str_data[reference_getid(data)].type == C_NAME )
/// Returns if this a reference to nil (unused name, is probably supposed to be a variable)
#define reference_tonil(data) ( str_data[reference_getid(data)].type == C_NOP )
/// Returns the unique id of the reference (id and index)
#define reference_getuid(data) ( (data)->u.num )
/// Returns the id of the reference
#define reference_getid(data) ( (int32)(reference_getuid(data) & 0x00ffffff) )
/// Returns the array index of the reference
#define reference_getindex(data) ( (int32)(((uint32)(reference_getuid(data) & 0xff000000)) >> 24) )
/// Returns the name of the reference
#define reference_getname(data) ( str_buf + str_data[reference_getid(data)].str )
/// Returns the linked list of uid-value pairs of the reference (can be NULL)
#define reference_getref(data) ( (data)->ref )
/// Returns the value of the constant
#define reference_getconstant(data) ( str_data[reference_getid(data)].val )
/// Returns the type of param
#define reference_getparamtype(data) ( str_data[reference_getid(data)].val )

#define not_server_variable(prefix) ( (prefix) != '$' && (prefix) != '.' && (prefix) != '\'')
#define not_array_variable(prefix) ( (prefix) != '$' && (prefix) != '@' && (prefix) != '.' && (prefix) != '\'' )
#define is_string_variable(name) ( (name)[strlen(name) - 1] == '$' )

#define FETCH(n, t) \
		if( script_hasdata(st,n) ) \
			(t)=script_getnum(st,n);

/// Maximum amount of elements in script arrays
#define SCRIPT_MAX_ARRAYSIZE 128

#define SCRIPT_BLOCK_SIZE 512
enum { LABEL_NEXTLINE=1,LABEL_START };

TBL_PC *script_rid2sd(struct script_state *st);

/**
 * Get `sd` from a char id in `loc` param instead of attached rid
 * @param st Script
 * @param loc Location to look char id in script parameter
 * @param sd Variable that will be assigned
 * @return True if `sd` is assigned, false otherwise
 **/
static bool script_charid2sd_(struct script_state *st, uint8 loc, struct map_session_data **sd, const char *func) {
	if (script_hasdata(st, loc)) {
		int id_ = script_getnum(st, loc);
		if (!(*sd = map_charid2sd(id_)))
			ShowError("%s: Player with char id '%d' is not found.\n", func, id_);
	}
	else
		*sd = script_rid2sd(st);
	return (*sd) ? true : false;
}

#define script_charid2sd(loc,sd) script_charid2sd_(st,(loc),&(sd),__FUNCTION__)

/// temporary buffer for passing around compiled bytecode
/// @see add_scriptb, set_label, parse_script
static unsigned char* script_buf = NULL;
static int script_pos = 0, script_size = 0;

static inline int GETVALUE(const unsigned char* buf, int i)
{
	return (int)MakeDWord(MakeWord(buf[i], buf[i+1]), MakeWord(buf[i+2], 0));
}
static inline void SETVALUE(unsigned char* buf, int i, int n)
{
	buf[i]   = GetByte(n, 0);
	buf[i+1] = GetByte(n, 1);
	buf[i+2] = GetByte(n, 2);
}

// String buffer structures.
// str_data stores string information
static struct str_data_struct {
	enum c_op type;
	int str;
	int backpatch;
	int label;
	int (*func)(struct script_state *st);
	int val;
	int next;
	const char *name;
} *str_data = NULL;
static int str_data_size = 0; // size of the data
static int str_num = LABEL_START; // next id to be assigned

// str_buf holds the strings themselves
static char *str_buf;
static int str_size = 0; // size of the buffer
static int str_pos = 0; // next position to be assigned


// Using a prime number for SCRIPT_HASH_SIZE should give better distributions
#define SCRIPT_HASH_SIZE 1021
int str_hash[SCRIPT_HASH_SIZE];
// Specifies which string hashing method to use
//#define SCRIPT_HASH_DJB2
//#define SCRIPT_HASH_SDBM
#define SCRIPT_HASH_ELF

static DBMap* scriptlabel_db=NULL; // const char* label_name -> int script_pos
static DBMap* userfunc_db=NULL; // const char* func_name -> struct script_code*
static int parse_options=0;
DBMap* script_get_label_db(){ return scriptlabel_db; }
DBMap* script_get_userfunc_db(){ return userfunc_db; }

// Caches compiled autoscript item code. 
// Note: This is not cleared when reloading itemdb.
static DBMap* autobonus_db=NULL; // char* script -> char* bytecode

struct Script_Config script_config = {
	1, // warn_func_mismatch_argtypes
	1, 65535, 2048, //warn_func_mismatch_paramnum/check_cmdcount/check_gotocount
	0, INT_MAX, // input_min_value/input_max_value
	"OnPCDieEvent", //die_event_name
	"OnPCKillEvent", //kill_pc_event_name
	"OnNPCKillEvent", //kill_mob_event_name
	"OnPCLoginEvent", //login_event_name
	"OnPCLogoutEvent", //logout_event_name
	"OnPCLoadMapEvent", //loadmap_event_name
	"OnPCBaseLvUpEvent", //baselvup_event_name
	"OnPCJobLvUpEvent", //joblvup_event_name
	"OnTouch_",	//ontouch_name (runs on first visible char to enter area, picks another char if the first char leaves)
	"OnTouch",	//ontouch2_name (run whenever a char walks into the OnTouch area)
};

static jmp_buf     error_jump;
static char*       error_msg;
static const char* error_pos;
static int         error_report; // if the error should produce output

// for advanced scripting support ( nested if, switch, while, for, do-while, function, etc )
// [Eoe / jA 1080, 1081, 1094, 1164]
enum curly_type {
	TYPE_NULL = 0,
	TYPE_IF,
	TYPE_SWITCH,
	TYPE_WHILE,
	TYPE_FOR,
	TYPE_DO,
	TYPE_USERFUNC,
	TYPE_ARGLIST // function argument list
};

enum e_arglist
{
	ARGLIST_UNDEFINED = 0,
	ARGLIST_NO_PAREN  = 1,
	ARGLIST_PAREN     = 2,
};

static struct {
	struct {
		enum curly_type type;
		int index;
		int count;
		int flag;
		struct linkdb_node *case_label;
	} curly[256];		// �E�J�b�R�̏��
	int curly_count;	// �E�J�b�R�̐�
	int index;			// �X�N���v�g���Ŏg�p�����\���̐�
} syntax;

const char* parse_curly_close(const char* p);
const char* parse_syntax_close(const char* p);
const char* parse_syntax_close_sub(const char* p,int* flag);
const char* parse_syntax(const char* p);
static int parse_syntax_for_flag = 0;

extern int current_equip_item_index; //for New CARDS Scripts. It contains Inventory Index of the EQUIP_SCRIPT caller item. [Lupus]
int potion_flag=0; //For use on Alchemist improved potions/Potion Pitcher. [Skotlex]
int potion_hp=0, potion_per_hp=0, potion_sp=0, potion_per_sp=0;
int potion_target=0;


c_op get_com(unsigned char *script,int *pos);
int get_num(unsigned char *script,int *pos);

typedef struct script_function {
	int (*func)(struct script_state *st);
	const char *name;
	const char *arg;
} script_function;

extern script_function buildin_func[];

static struct linkdb_node* sleep_db;// int oid -> struct script_state*

/*==========================================
 * ���[�J���v���g�^�C�v�錾 (�K�v�ȕ��̂�)
 *------------------------------------------*/
const char* parse_subexpr(const char* p,int limit);
int run_func(struct script_state *st);

enum {
	MF_NOMEMO,	//0
	MF_NOTELEPORT,
	MF_NOSAVE,
	MF_NOBRANCH,
	MF_NOPENALTY,
	MF_NOZENYPENALTY,
	MF_PVP,
	MF_PVP_NOPARTY,
	MF_PVP_NOGUILD,
	MF_GVG,
	MF_GVG_NOPARTY,	//10
	MF_NOTRADE,
	MF_NOSKILL,
	MF_NOWARP,
	MF_PARTYLOCK,
	MF_NOICEWALL,
	MF_SNOW,
	MF_FOG,
	MF_SAKURA,
	MF_LEAVES,
	MF_RAIN,	//20
	// 21 free
	MF_NOGO = 22,
	MF_CLOUDS,
	MF_CLOUDS2,
	MF_FIREWORKS,
	MF_GVG_CASTLE,
	MF_GVG_DUNGEON,
	MF_NIGHTENABLED,
	MF_NOBASEEXP,
	MF_NOJOBEXP,	//30
	MF_NOMOBLOOT,
	MF_NOMVPLOOT,
	MF_NORETURN,
	MF_NOWARPTO,
	MF_NIGHTMAREDROP,
	MF_RESTRICTED,
	MF_NOCOMMAND,
	MF_NODROP,
	MF_JEXP,
	MF_BEXP,	//40
	MF_NOVENDING,
	MF_LOADEVENT,
	MF_NOCHAT,
	MF_NOEXPPENALTY,
	MF_GUILDLOCK,
	MF_TOWN,
	MF_AUTOTRADE,
	MF_ALLOWKS,
	MF_MONSTER_NOTELEPORT,
	MF_PVP_NOCALCRANK,	//50
	MF_BATTLEGROUND,
	MF_RESET,
	MF_GVG_TE_CASTLE,
	MF_GVG_TE,
	MF_NOSUNMOONSTARMIRACLE,
};

const char* script_op2name(int op)
{
#define RETURN_OP_NAME(type) case type: return #type
	switch( op )
	{
	RETURN_OP_NAME(C_NOP);
	RETURN_OP_NAME(C_POS);
	RETURN_OP_NAME(C_INT);
	RETURN_OP_NAME(C_PARAM);
	RETURN_OP_NAME(C_FUNC);
	RETURN_OP_NAME(C_STR);
	RETURN_OP_NAME(C_CONSTSTR);
	RETURN_OP_NAME(C_ARG);
	RETURN_OP_NAME(C_NAME);
	RETURN_OP_NAME(C_EOL);
	RETURN_OP_NAME(C_RETINFO);
	RETURN_OP_NAME(C_USERFUNC);
	RETURN_OP_NAME(C_USERFUNC_POS);

	// operators
	RETURN_OP_NAME(C_OP3);
	RETURN_OP_NAME(C_LOR);
	RETURN_OP_NAME(C_LAND);
	RETURN_OP_NAME(C_LE);
	RETURN_OP_NAME(C_LT);
	RETURN_OP_NAME(C_GE);
	RETURN_OP_NAME(C_GT);
	RETURN_OP_NAME(C_EQ);
	RETURN_OP_NAME(C_NE);
	RETURN_OP_NAME(C_XOR);
	RETURN_OP_NAME(C_OR);
	RETURN_OP_NAME(C_AND);
	RETURN_OP_NAME(C_ADD);
	RETURN_OP_NAME(C_SUB);
	RETURN_OP_NAME(C_MUL);
	RETURN_OP_NAME(C_DIV);
	RETURN_OP_NAME(C_MOD);
	RETURN_OP_NAME(C_NEG);
	RETURN_OP_NAME(C_LNOT);
	RETURN_OP_NAME(C_NOT);
	RETURN_OP_NAME(C_R_SHIFT);
	RETURN_OP_NAME(C_L_SHIFT);

	default:
		ShowDebug("script_op2name: unexpected op=%d\n", op);
		return "???";
	}
#undef RETURN_OP_NAME
}

#ifdef DEBUG_DUMP_STACK
static void script_dump_stack(struct script_state* st)
{
	int i;
	ShowMessage("\tstart = %d\n", st->start);
	ShowMessage("\tend   = %d\n", st->end);
	ShowMessage("\tdefsp = %d\n", st->stack->defsp);
	ShowMessage("\tsp    = %d\n", st->stack->sp);
	for( i = 0; i < st->stack->sp; ++i )
	{
		struct script_data* data = &st->stack->stack_data[i];
		ShowMessage("\t[%d] %s", i, script_op2name(data->type));
		switch( data->type )
		{
		case C_INT:
		case C_POS:
			ShowMessage(" %d\n", data->u.num);
			break;

		case C_STR:
		case C_CONSTSTR:
			ShowMessage(" \"%s\"\n", data->u.str);
			break;

		case C_NAME:
			ShowMessage(" \"%s\" (id=%d ref=%p subtype=%s)\n", reference_getname(data), data->u.num, data->ref, script_op2name(str_data[data->u.num].type));
			break;

		case C_RETINFO:
			{
				struct script_retinfo* ri = data->u.ri;
				ShowMessage(" %p {var_function=%p, script=%p, pos=%d, nargs=%d, defsp=%d}\n", ri, ri->var_function, ri->script, ri->pos, ri->nargs, ri->defsp);
			}
			break;
		default:
			ShowMessage("\n");
			break;
		}
	}
}
#endif

/// Reports on the console the src of a script error.
static void script_reportsrc(struct script_state *st)
{
	if( st->oid == 0 )
		return; //Can't report source.

	struct block_list* bl = map_id2bl(st->oid);

	if (!bl)
		return;

	switch( bl->type )
	{
	case BL_NPC:
		if( bl->m >= 0 )
			ShowDebug("Source (NPC): %s at %s (%d,%d)\n", ((struct npc_data *)bl)->name, map[bl->m].name, bl->x, bl->y);
		else
			ShowDebug("Source (NPC): %s (invisible/not on a map)\n", ((struct npc_data *)bl)->name);
		break;
	default:
		if( bl->m >= 0 )
			ShowDebug("Source (Non-NPC type %d): name %s at %s (%d,%d)\n", bl->type, status_get_name(bl), map[bl->m].name, bl->x, bl->y);
		else
			ShowDebug("Source (Non-NPC type %d): name %s (invisible/not on a map)\n", bl->type, status_get_name(bl));
		break;
	}
}

/// Reports on the console information about the script data.
static void script_reportdata(struct script_data* data)
{
	if( data == NULL )
		return;
	switch( data->type )
	{
	case C_NOP:// no value
		ShowDebug("Data: nothing (nil)\n");
		break;
	case C_INT:// number
		ShowDebug("Data: number value=%d\n", data->u.num);
		break;
	case C_STR:
	case C_CONSTSTR:// string
		if( data->u.str )
		{
			ShowDebug("Data: string value=\"%s\"\n", data->u.str);
		}
		else
		{
			ShowDebug("Data: string value=NULL\n");
		}
		break;
	case C_NAME:// reference
		if( reference_tovariable(data) )
		{// variable
			const char* name = reference_getname(data);
			if( not_array_variable(*name) )
				ShowDebug("Data: variable name='%s'\n", name);
			else
				ShowDebug("Data: variable name='%s' index=%d\n", name, reference_getindex(data));
		}
		else if( reference_toconstant(data) )
		{// constant
			ShowDebug("Data: constant name='%s' value=%d\n", reference_getname(data), reference_getconstant(data));
		}
		else if( reference_toparam(data) )
		{// param
			ShowDebug("Data: param name='%s' type=%d\n", reference_getname(data), reference_getparamtype(data));
		}
		else
		{// ???
			ShowDebug("Data: reference name='%s' type=%s\n", reference_getname(data), script_op2name(data->type));
			ShowDebug("Please report this!!! - str_data.type=%s\n", script_op2name(str_data[reference_getid(data)].type));
		}
		break;
	case C_POS:// label
		ShowDebug("Data: label pos=%d\n", data->u.num);
		break;
	default:
		ShowDebug("Data: %s\n", script_op2name(data->type));
		break;
	}
}


/// Reports on the console information about the current built-in function.
static void script_reportfunc(struct script_state* st)
{
	int i, params, id;
	struct script_data* data;

	if( !script_hasdata(st,0) )
	{// no stack
		return;
	}

	data = script_getdata(st,0);

	if( !data_isreference(data) || str_data[reference_getid(data)].type != C_FUNC )
	{// script currently not executing a built-in function or corrupt stack
		return;
	}

	id     = reference_getid(data);
	params = script_lastdata(st)-1;

	if( params > 0 )
	{
		ShowDebug("Function: %s (%d parameter%s):\n", get_str(id), params, ( params == 1 ) ? "" : "s");

		for( i = 2; i <= script_lastdata(st); i++ )
		{
			script_reportdata(script_getdata(st,i));
		}
	}
	else
	{
		ShowDebug("Function: %s (no parameters)\n", get_str(id));
	}
}


/*==========================================
 * �G���[���b�Z�[�W�o��
 *------------------------------------------*/
static void disp_error_message2(const char *mes,const char *pos,int report)
{
	error_msg = aStrdup(mes);
	error_pos = pos;
	error_report = report;
	longjmp( error_jump, 1 );
}
#define disp_error_message(mes,pos) disp_error_message2(mes,pos,1)

/// Checks event parameter validity
static void check_event(struct script_state *st, const char *evt)
{
	if( evt && evt[0] && !stristr(evt, "::On") )
	{
		if( npc_event_isspecial(evt) )
		{
			;  // portable small/large monsters or other attributes
		}
		else
		{
			ShowWarning("NPC event parameter deprecated! Please use 'NPCNAME::OnEVENT' instead of '%s'.\n", evt);
			script_reportsrc(st);
		}
	}
}

/*==========================================
 * Hashes the input string
 *------------------------------------------*/
static unsigned int calc_hash(const char* p)
{
	unsigned int h;

#if defined(SCRIPT_HASH_DJB2)
	h = 5381;
	while( *p ) // hash*33 + c
		h = ( h << 5 ) + h + ((unsigned char)TOLOWER(*p++));
#elif defined(SCRIPT_HASH_SDBM)
	h = 0;
	while( *p ) // hash*65599 + c
		h = ( h << 6 ) + ( h << 16 ) - h + ((unsigned char)TOLOWER(*p++));
#elif defined(SCRIPT_HASH_ELF) // UNIX ELF hash
	h = 0;
	while( *p ){
		unsigned int g;
		h = ( h << 4 ) + ((unsigned char)TOLOWER(*p++));
		g = h & 0xF0000000;
		if( g )
		{
			h ^= g >> 24;
			h &= ~g;
		}
	}
#else // athena hash
	h = 0;
	while( *p )
		h = ( h << 1 ) + ( h >> 3 ) + ( h >> 5 ) + ( h >> 8 ) + (unsigned char)TOLOWER(*p++);
#endif

	return h % SCRIPT_HASH_SIZE;
}


/*==========================================
 * str_data manipulation functions
 *------------------------------------------*/

/// Looks up string using the provided id.
const char* get_str(int id)
{
	Assert( id >= LABEL_START && id < str_size );
	return str_buf+str_data[id].str;
}

/// Returns the uid of the string, or -1.
static int search_str(const char* p)
{
	int i;

	for( i = str_hash[calc_hash(p)]; i != 0; i = str_data[i].next )
		if( strcasecmp(get_str(i),p) == 0 )
			return i;

	return -1;
}

/// Stores a copy of the string and returns its id.
/// If an identical string is already present, returns its id instead.
int add_str(const char* p)
{
	int i, h;
	int len;

	h = calc_hash(p);

	if( str_hash[h] == 0 )
	{// empty bucket, add new node here
		str_hash[h] = str_num;
	}
	else
	{// scan for end of list, or occurence of identical string
		for( i = str_hash[h]; ; i = str_data[i].next )
		{
			if( strcasecmp(get_str(i),p) == 0 )
				return i; // string already in list
			if( str_data[i].next == 0 )
				break; // reached the end
		}

		// append node to end of list
		str_data[i].next = str_num;
	}

	// grow list if neccessary
	if( str_num >= str_data_size )
	{
		str_data_size += 128;
		RECREATE(str_data,struct str_data_struct,str_data_size);
		memset(str_data + (str_data_size - 128), '\0', 128);
	}

	len=(int)strlen(p);

	// grow string buffer if neccessary
	while( str_pos+len+1 >= str_size )
	{
		str_size += 256;
		RECREATE(str_buf,char,str_size);
		memset(str_buf + (str_size - 256), '\0', 256);
	}

	safestrncpy(str_buf+str_pos, p, len+1);
	str_data[str_num].type = C_NOP;
	str_data[str_num].str = str_pos;
	str_data[str_num].next = 0;
	str_data[str_num].func = NULL;
	str_data[str_num].backpatch = -1;
	str_data[str_num].label = -1;
	str_pos += len+1;

	return str_num++;
}


/// Appends 1 byte to the script buffer.
static void add_scriptb(int a)
{
	if( script_pos+1 >= script_size )
	{
		script_size += SCRIPT_BLOCK_SIZE;
		RECREATE(script_buf,unsigned char,script_size);
	}
	script_buf[script_pos++] = (uint8)(a);
}

/// Appends a c_op value to the script buffer.
/// The value is variable-length encoded into 8-bit blocks.
/// The encoding scheme is ( 01?????? )* 00??????, LSB first.
/// All blocks but the last hold 7 bits of data, topmost bit is always 1 (carries).
static void add_scriptc(int a)
{
	while( a >= 0x40 )
	{
		add_scriptb((a&0x3f)|0x40);
		a = (a - 0x40) >> 6;
	}

	add_scriptb(a);
}

/// Appends an integer value to the script buffer.
/// The value is variable-length encoded into 8-bit blocks.
/// The encoding scheme is ( 11?????? )* 10??????, LSB first.
/// All blocks but the last hold 7 bits of data, topmost bit is always 1 (carries).
static void add_scripti(int a)
{
	while( a >= 0x40 )
	{
		add_scriptb((a&0x3f)|0xc0);
		a = (a - 0x40) >> 6;
	}
	add_scriptb(a|0x80);
}

/// Appends a str_data object (label/function/variable/integer) to the script buffer.

///
/// @param l The id of the str_data entry
// �ő�16M�܂�
static void add_scriptl(int l)
{
	int backpatch = str_data[l].backpatch;

	switch(str_data[l].type){
	case C_POS:
	case C_USERFUNC_POS:
		add_scriptc(C_POS);
		add_scriptb(str_data[l].label);
		add_scriptb(str_data[l].label>>8);
		add_scriptb(str_data[l].label>>16);
		break;
	case C_NOP:
	case C_USERFUNC:
		// ���x���̉\��������̂�backpatch�p�f�[�^���ߍ���
		add_scriptc(C_NAME);
		str_data[l].backpatch = script_pos;
		add_scriptb(backpatch);
		add_scriptb(backpatch>>8);
		add_scriptb(backpatch>>16);
		break;
	case C_INT:
		add_scripti(abs(str_data[l].val));
		if( str_data[l].val < 0 ) //Notice that this is negative, from jA (Rayce)
			add_scriptc(C_NEG);
		break;
	default: // assume C_NAME
		add_scriptc(C_NAME);
		add_scriptb(l);
		add_scriptb(l>>8);
		add_scriptb(l>>16);
		break;
	}
}

/*==========================================
 * ���x������������
 *------------------------------------------*/
void set_label(int l,int pos, const char* script_pos)
{
	int i,next;

	if(str_data[l].type==C_INT || str_data[l].type==C_PARAM || str_data[l].type==C_FUNC)
	{	//Prevent overwriting constants values, parameters and built-in functions [Skotlex]
		disp_error_message("set_label: invalid label name",script_pos);
		return;
	}
	if(str_data[l].label!=-1){
		disp_error_message("set_label: dup label ",script_pos);
		return;
	}
	str_data[l].type=(str_data[l].type == C_USERFUNC ? C_USERFUNC_POS : C_POS);
	str_data[l].label=pos;
	for(i=str_data[l].backpatch;i>=0 && i!=0x00ffffff;){
		next=GETVALUE(script_buf,i);
		script_buf[i-1]=(str_data[l].type == C_USERFUNC ? C_USERFUNC_POS : C_POS);
		SETVALUE(script_buf,i,pos);
		i=next;
	}
}

/// Skips spaces and/or comments.
const char* skip_space(const char* p)
{
	if( p == NULL )
		return NULL;
	for(;;)
	{
		while( ISSPACE(*p) )
			++p;
		if( *p == '/' && p[1] == '/' )
		{// line comment
			while(*p && *p!='\n')
				++p;
		}
		else if( *p == '/' && p[1] == '*' )
		{// block comment
			p += 2;
			for(;;)
			{
				if( *p == '\0' )
					return p;//disp_error_message("script:skip_space: end of file while parsing block comment. expected "CL_BOLD"*/"CL_NORM, p);
				if( *p == '*' && p[1] == '/' )
				{// end of block comment
					p += 2;
					break;
				}
				++p;
			}
		}
		else
			break;
	}
	return p;
}

/// Skips a word.
/// A word consists of undercores and/or alfanumeric characters,
/// and valid variable prefixes/postfixes.
static
const char* skip_word(const char* p)
{
	// prefix
	switch( *p )
	{
	case '@':// temporary char variable
		++p; break;
	case '#':// account variable
		p += ( p[1] == '#' ? 2 : 1 ); break;
	case '\'':// instance variable
		++p; break;
	case '.':// npc variable
		p += ( p[1] == '@' ? 2 : 1 ); break;
	case '$':// global variable
		p += ( p[1] == '@' ? 2 : 1 ); break;
	}

	while( ISALNUM(*p) || *p == '_' )
		++p;

	// postfix
	if( *p == '$' )// string
		p++;

	return p;
}

/// Adds a word to str_data.
/// @see skip_word
/// @see add_str
static
int add_word(const char* p)
{
	char* word;
	int len;
	int i;

	// Check for a word
	len = skip_word(p) - p;
	if( len == 0 )
		disp_error_message("script:add_word: invalid word. A word consists of undercores and/or alfanumeric characters, and valid variable prefixes/postfixes.", p);

	// Duplicate the word
	word = (char*)aMalloc(len+1);
	memcpy(word, p, len);
	word[len] = 0;
	
	// add the word
	i = add_str(word);
	aFree(word);
	return i;
}

/// Parses a function call.
/// The argument list can have parenthesis or not.
/// The number of arguments is checked.
static
const char* parse_callfunc(const char* p, int require_paren)
{
	const char* p2;
	const char* arg=NULL;
	int func;

	func = add_word(p);
	if( str_data[func].type == C_FUNC ){
		// buildin function
		add_scriptl(func);
		add_scriptc(C_ARG);
		arg = buildin_func[str_data[func].val].arg;
	} else if( str_data[func].type == C_USERFUNC || str_data[func].type == C_USERFUNC_POS ){
		// script defined function
		int callsub = search_str("callsub");
		add_scriptl(callsub);
		add_scriptc(C_ARG);
		add_scriptl(func);
		arg = buildin_func[str_data[callsub].val].arg;
		if( *arg == 0 )
			disp_error_message("parse_callfunc: callsub has no arguments, please review it's definition",p);
		if( *arg != '*' )
			++arg; // count func as argument
	} else
		disp_error_message("parse_line: expect command, missing function name or calling undeclared function",p);

	p = skip_word(p);
	p = skip_space(p);
	syntax.curly[syntax.curly_count].type = TYPE_ARGLIST;
	syntax.curly[syntax.curly_count].count = 0;
	if( *p == ';' )
	{// <func name> ';'
		syntax.curly[syntax.curly_count].flag = ARGLIST_NO_PAREN;
	} else if( *p == '(' && *(p2=skip_space(p+1)) == ')' )
	{// <func name> '(' ')'
		syntax.curly[syntax.curly_count].flag = ARGLIST_PAREN;
		p = p2;
	/*
	} else if( 0 && require_paren && *p != '(' )
	{// <func name>
		syntax.curly[syntax.curly_count].flag = ARGLIST_NO_PAREN;
	*/
	} else
	{// <func name> <arg list>
		if( require_paren ){
			if( *p != '(' )
				disp_error_message("need '('",p);
			++p; // skip '('
			syntax.curly[syntax.curly_count].flag = ARGLIST_PAREN;
		} else if( *p == '(' ){
			syntax.curly[syntax.curly_count].flag = ARGLIST_UNDEFINED;
		} else {
			syntax.curly[syntax.curly_count].flag = ARGLIST_NO_PAREN;
		}
		++syntax.curly_count;
		while( *arg ) {
			p2=parse_subexpr(p,-1);
			if( p == p2 )
				break; // not an argument
			if( *arg != '*' )
				++arg; // next argument

			p=skip_space(p2);
			if( *arg == 0 || *p != ',' )
				break; // no more arguments
			++p; // skip comma
		}
		--syntax.curly_count;
	}
	if( *arg && *arg != '?' && *arg != '*' )
		disp_error_message2("parse_callfunc: not enough arguments, expected ','", p, script_config.warn_func_mismatch_paramnum);
	if( syntax.curly[syntax.curly_count].type != TYPE_ARGLIST )
		disp_error_message("parse_callfunc: DEBUG last curly is not an argument list",p);
	if( syntax.curly[syntax.curly_count].flag == ARGLIST_PAREN ){
		if( *p != ')' )
			disp_error_message("parse_callfunc: expected ')' to close argument list",p);
		++p;
	}
	add_scriptc(C_FUNC);
	return p;
}

/// Processes end of logical script line.
/// @param first When true, only fix up scheduling data is initialized
/// @param p Script position for error reporting in set_label
static void parse_nextline(bool first, const char* p)
{
	if( !first )
	{
		add_scriptc(C_EOL);  // mark end of line for stack cleanup
		set_label(LABEL_NEXTLINE, script_pos, p);  // fix up '-' labels
	}

	// initialize data for new '-' label fix up scheduling
	str_data[LABEL_NEXTLINE].type      = C_NOP;
	str_data[LABEL_NEXTLINE].backpatch = -1;
	str_data[LABEL_NEXTLINE].label     = -1;
}

/*
* Checks whether the gives string is a number literal
*
* Mainly necessary to differentiate between number literals and NPC name
* constants, since several of those start with a digit.
*
* All this does is to check if the string begins with an optional + or - sign,
* followed by a hexadecimal or decimal number literal literal and is NOT
* followed by a underscore or letter.
*
* @author : Hercules.ws
* @param p Pointer to the string to check
* @return Whether the string is a number literal
*/
bool is_number(const char *p) {
	const char *np;
	if (!p)
		return false;
	if (*p == '-' || *p == '+')
		p++;
	np = p;
	if (*p == '0' && p[1] == 'x') {
		p += 2;
		np = p;
		// Hexadecimal
		while (ISXDIGIT(*np))
			np++;
	}
	else {
		// Decimal
		while (ISDIGIT(*np))
			np++;
	}
	if (p != np && *np != '_' && !ISALPHA(*np)) // At least one digit, and next isn't a letter or _
		return true;
	return false;
}

/*==========================================
 * �Analysis section
 *------------------------------------------*/
const char* parse_simpleexpr(const char *p)
{
	long long i;
	p=skip_space(p);

	if(*p==';' || *p==',')
		disp_error_message("parse_simpleexpr: unexpected expr end",p);
	if(*p=='('){
		if( (i=syntax.curly_count-1) >= 0 && syntax.curly[i].type == TYPE_ARGLIST )
			++syntax.curly[i].count;
		p=parse_subexpr(p+1,-1);
		p=skip_space(p);
		if( (i=syntax.curly_count-1) >= 0 && syntax.curly[i].type == TYPE_ARGLIST &&
				syntax.curly[i].flag == ARGLIST_UNDEFINED && --syntax.curly[i].count == 0
		){
			if( *p == ',' ){
				syntax.curly[i].flag = ARGLIST_PAREN;
				return p;
			} else
				syntax.curly[i].flag = ARGLIST_NO_PAREN;
		}
		if( *p != ')' )
			disp_error_message("parse_simpleexpr: unmatch ')'",p);
		++p;
	} else if(ISDIGIT(*p) || ((*p=='-' || *p=='+') && ISDIGIT(p[1]))){
		char *np;
		i=strtoll(p,&np,0);
		if( i < INT_MIN ) {
			i = INT_MIN;
			disp_error_message("parse_simpleexpr: underflow detected, capping value to INT_MIN",p);
		} else if( i > INT_MAX ) {
			i = INT_MAX;
			disp_error_message("parse_simpleexpr: overflow detected, capping value to INT_MAX",p);
		}
		add_scripti((int)i); // Cast is safe, as it's already been checked for overflows
		p=np;
	} else if(*p=='"'){
		add_scriptc(C_STR);
		p++;
		while( *p && *p != '"' ){
			if( (unsigned char)p[-1] <= 0x7e && *p == '\\' )
			{
				char buf[8];
				size_t len = skip_escaped_c(p) - p;
				size_t n = sv_unescape_c(buf, p, len);
				if( n != 1 )
					ShowDebug("parse_simpleexpr: unexpected length %d after unescape (\"%.*s\" -> %.*s)\n", (int)n, (int)len, p, (int)n, buf);
				p += len;
				add_scriptb(*buf);
				continue;
			}
			else if( *p == '\n' )
				disp_error_message("parse_simpleexpr: unexpected newline @ string",p);
			add_scriptb(*p++);
		}
		if(!*p)
			disp_error_message("parse_simpleexpr: unexpected eof @ string",p);
		add_scriptb(0);
		p++;	//'"'
	} else {
		int l;
		// label , register , function etc
		if(skip_word(p)==p)
			disp_error_message("parse_simpleexpr: unexpected character",p);

		l=add_word(p);
		if( str_data[l].type == C_FUNC || str_data[l].type == C_USERFUNC || str_data[l].type == C_USERFUNC_POS)
			return parse_callfunc(p,1);

		p=skip_word(p);
		if( *p == '[' ){
			// array(name[i] => getelementofarray(name,i) )
			add_scriptl(search_str("getelementofarray"));
			add_scriptc(C_ARG);
			add_scriptl(l);
			
			p=parse_subexpr(p+1,-1);
			p=skip_space(p);
			if( *p != ']' )
				disp_error_message("parse_simpleexpr: unmatch ']'",p);
			++p;
			add_scriptc(C_FUNC);
		}else
			add_scriptl(l);

	}

	return p;
}

/*==========================================
 * ���̉��
 *------------------------------------------*/
const char* parse_subexpr(const char* p,int limit)
{
	int op,opl,len;
	const char* tmpp;

	p=skip_space(p);

	if(*p=='-'){
		tmpp=skip_space(p+1);
		if(*tmpp==';' || *tmpp==','){
			add_scriptl(LABEL_NEXTLINE);
			p++;
			return p;
		}
	}
	tmpp=p;
	if((op=C_NEG,*p=='-') || (op=C_LNOT,*p=='!') || (op=C_NOT,*p=='~')){
		p=parse_subexpr(p+1,10);
		add_scriptc(op);
	} else
		p=parse_simpleexpr(p);
	p=skip_space(p);
	while((
			(op=C_OP3,opl=0,len=1,*p=='?') ||
			(op=C_ADD,opl=8,len=1,*p=='+') ||
			(op=C_SUB,opl=8,len=1,*p=='-') ||
			(op=C_MUL,opl=9,len=1,*p=='*') ||
			(op=C_DIV,opl=9,len=1,*p=='/') ||
			(op=C_MOD,opl=9,len=1,*p=='%') ||
			(op=C_LAND,opl=2,len=2,*p=='&' && p[1]=='&') ||
			(op=C_AND,opl=6,len=1,*p=='&') ||
			(op=C_LOR,opl=1,len=2,*p=='|' && p[1]=='|') ||
			(op=C_OR,opl=5,len=1,*p=='|') ||
			(op=C_XOR,opl=4,len=1,*p=='^') ||
			(op=C_EQ,opl=3,len=2,*p=='=' && p[1]=='=') ||
			(op=C_NE,opl=3,len=2,*p=='!' && p[1]=='=') ||
			(op=C_R_SHIFT,opl=7,len=2,*p=='>' && p[1]=='>') ||
			(op=C_GE,opl=3,len=2,*p=='>' && p[1]=='=') ||
			(op=C_GT,opl=3,len=1,*p=='>') ||
			(op=C_L_SHIFT,opl=7,len=2,*p=='<' && p[1]=='<') ||
			(op=C_LE,opl=3,len=2,*p=='<' && p[1]=='=') ||
			(op=C_LT,opl=3,len=1,*p=='<')) && opl>limit){
		p+=len;
		if(op == C_OP3) {
			p=parse_subexpr(p,-1);
			p=skip_space(p);
			if( *(p++) != ':')
				disp_error_message("parse_subexpr: need ':'", p-1);
			p=parse_subexpr(p,-1);
		} else {
			p=parse_subexpr(p,opl);
		}
		add_scriptc(op);
		p=skip_space(p);
	}

	return p;  /* return first untreated operator */
}

/*==========================================
 * ���̕]��
 *------------------------------------------*/
const char* parse_expr(const char *p)
{
	switch(*p){
	case ')': case ';': case ':': case '[': case ']':
	case '}':
		disp_error_message("parse_expr: unexpected char",p);
	}
	p=parse_subexpr(p,-1);
	return p;
}

/*==========================================
 * �s�̉��
 *------------------------------------------*/
const char* parse_line(const char* p)
{
	const char* p2;

	p=skip_space(p);
	if(*p==';') {
		// if(); for(); while(); �̂��߂ɕ�����
		p = parse_syntax_close(p + 1);
		return p;
	}
	if(*p==')' && parse_syntax_for_flag)
		return p+1;

	p = skip_space(p);
	if(p[0] == '{') {
		syntax.curly[syntax.curly_count].type  = TYPE_NULL;
		syntax.curly[syntax.curly_count].count = -1;
		syntax.curly[syntax.curly_count].index = -1;
		syntax.curly_count++;
		return p + 1;
	} else if(p[0] == '}') {
		return parse_curly_close(p);
	}

	// �\���֘A�̏���
	p2 = parse_syntax(p);
	if(p2 != NULL)
		return p2;

	p = parse_callfunc(p,0);
	p = skip_space(p);

	if(parse_syntax_for_flag) {
		if( *p != ')' )
			disp_error_message("parse_line: need ')'",p);
	} else {
		if( *p != ';' )
			disp_error_message("parse_line: need ';'",p);
	}

	// if, for , while �̕�����
	p = parse_syntax_close(p+1);

	return p;
}

// { ... } �̕�����
const char* parse_curly_close(const char* p)
{
	if(syntax.curly_count <= 0) {
		disp_error_message("parse_curly_close: unexpected string",p);
		return p + 1;
	} else if(syntax.curly[syntax.curly_count-1].type == TYPE_NULL) {
		syntax.curly_count--;
		// if, for , while �̕�����
		p = parse_syntax_close(p + 1);
		return p;
	} else if(syntax.curly[syntax.curly_count-1].type == TYPE_SWITCH) {
		// switch() ������
		int pos = syntax.curly_count-1;
		char label[256];
		int l;
		// �ꎞ�ϐ�������
		sprintf(label,"set $@__SW%x_VAL,0;",syntax.curly[pos].index);
		syntax.curly[syntax.curly_count++].type = TYPE_NULL;
		parse_line(label);
		syntax.curly_count--;

		// �������ŏI���|�C���^�Ɉړ�
		sprintf(label,"goto __SW%x_FIN;",syntax.curly[pos].index);
		syntax.curly[syntax.curly_count++].type = TYPE_NULL;
		parse_line(label);
		syntax.curly_count--;

		// ���ݒn�̃��x����t����
		sprintf(label,"__SW%x_%x",syntax.curly[pos].index,syntax.curly[pos].count);
		l=add_str(label);
		set_label(l,script_pos, p);

		if(syntax.curly[pos].flag) {
			// default �����݂���
			sprintf(label,"goto __SW%x_DEF;",syntax.curly[pos].index);
			syntax.curly[syntax.curly_count++].type = TYPE_NULL;
			parse_line(label);
			syntax.curly_count--;
		}

		// �I�����x����t����
		sprintf(label,"__SW%x_FIN",syntax.curly[pos].index);
		l=add_str(label);
		set_label(l,script_pos, p);
		linkdb_final(&syntax.curly[pos].case_label);	// free the list of case label
		syntax.curly_count--;
		// if, for , while �̕�����
		p = parse_syntax_close(p + 1);
		return p;
	} else {
		disp_error_message("parse_curly_close: unexpected string",p);
		return p + 1;
	}
}

// �\���֘A�̏���
//	 break, case, continue, default, do, for, function,
//	 if, switch, while �����̓����ŏ������܂��B
const char* parse_syntax(const char* p)
{
	const char *p2 = skip_word(p);

	switch(*p) {
	case 'B':
	case 'b':
		if(p2 - p == 5 && !strncasecmp(p,"break",5)) {
			// break �̏���
			char label[256];
			int pos = syntax.curly_count - 1;
			while(pos >= 0) {
				if(syntax.curly[pos].type == TYPE_DO) {
					sprintf(label,"goto __DO%x_FIN;",syntax.curly[pos].index);
					break;
				} else if(syntax.curly[pos].type == TYPE_FOR) {
					sprintf(label,"goto __FR%x_FIN;",syntax.curly[pos].index);
					break;
				} else if(syntax.curly[pos].type == TYPE_WHILE) {
					sprintf(label,"goto __WL%x_FIN;",syntax.curly[pos].index);
					break;
				} else if(syntax.curly[pos].type == TYPE_SWITCH) {
					sprintf(label,"goto __SW%x_FIN;",syntax.curly[pos].index);
					break;
				}
				pos--;
			}
			if(pos < 0) {
				disp_error_message("parse_syntax: unexpected 'break'",p);
			} else {
				syntax.curly[syntax.curly_count++].type = TYPE_NULL;
				parse_line(label);
				syntax.curly_count--;
			}
			p = skip_space(p2);
			if(*p != ';')
				disp_error_message("parse_syntax: need ';'",p);
			// if, for , while �̕�����
			p = parse_syntax_close(p + 1);
			return p;
		}
		break;
	case 'c':
	case 'C':
		if(p2 - p == 4 && !strncasecmp(p,"case",4)) {
			// case �̏���
			int pos = syntax.curly_count-1;
			if(pos < 0 || syntax.curly[pos].type != TYPE_SWITCH) {
				disp_error_message("parse_syntax: unexpected 'case' ",p);
				return p+1;
			} else {
				char label[256];
				int  l,v;
				char *np;
				if(syntax.curly[pos].count != 1) {
					// FALLTHRU �p�̃W�����v
					sprintf(label,"goto __SW%x_%xJ;",syntax.curly[pos].index,syntax.curly[pos].count);
					syntax.curly[syntax.curly_count++].type = TYPE_NULL;
					parse_line(label);
					syntax.curly_count--;

					// ���ݒn�̃��x����t����
					sprintf(label,"__SW%x_%x",syntax.curly[pos].index,syntax.curly[pos].count);
					l=add_str(label);
					set_label(l,script_pos, p);
				}
				// switch ���蕶
				p = skip_space(p2);
				if(p == p2) {
					disp_error_message("parse_syntax: expect space ' '",p);
				}
				// check whether case label is integer or not
				v = strtol(p,&np,0);
				if(np == p) { //Check for constants
					p2 = skip_word(p);
					v = p2-p; // length of word at p2
					memcpy(label,p,v);
					label[v]='\0';
					if( !script_get_constant(label, &v) )
						disp_error_message("parse_syntax: 'case' label not integer",p);
					p = skip_word(p);
				} else { //Numeric value
					if((*p == '-' || *p == '+') && ISDIGIT(p[1]))	// pre-skip because '-' can not skip_word
						p++;
					p = skip_word(p);
					if(np != p)
						disp_error_message("parse_syntax: 'case' label not integer",np);
				}
				p = skip_space(p);
				if(*p != ':')
					disp_error_message("parse_syntax: expect ':'",p);
				sprintf(label,"if(%d != $@__SW%x_VAL) goto __SW%x_%x;",
					v,syntax.curly[pos].index,syntax.curly[pos].index,syntax.curly[pos].count+1);
				syntax.curly[syntax.curly_count++].type = TYPE_NULL;
				// �Q��parse ���Ȃ��ƃ_��
				p2 = parse_line(label);
				parse_line(p2);
				syntax.curly_count--;
				if(syntax.curly[pos].count != 1) {
					// FALLTHRU �I����̃��x��
					sprintf(label,"__SW%x_%xJ",syntax.curly[pos].index,syntax.curly[pos].count);
					l=add_str(label);
					set_label(l,script_pos,p);
				}
				// check duplication of case label [Rayce]
				if(linkdb_search(&syntax.curly[pos].case_label, (void*)v) != NULL)
					disp_error_message("parse_syntax: dup 'case'",p);
				linkdb_insert(&syntax.curly[pos].case_label, (void*)v, (void*)1);

				sprintf(label,"set $@__SW%x_VAL,0;",syntax.curly[pos].index);
				syntax.curly[syntax.curly_count++].type = TYPE_NULL;
			
				parse_line(label);
				syntax.curly_count--;
				syntax.curly[pos].count++;
			}
			return p + 1;
		} else if(p2 - p == 8 && !strncasecmp(p,"continue",8)) {
			// continue �̏���
			char label[256];
			int pos = syntax.curly_count - 1;
			while(pos >= 0) {
				if(syntax.curly[pos].type == TYPE_DO) {
					sprintf(label,"goto __DO%x_NXT;",syntax.curly[pos].index);
					syntax.curly[pos].flag = 1; // continue �p�̃����N����t���O
					break;
				} else if(syntax.curly[pos].type == TYPE_FOR) {
					sprintf(label,"goto __FR%x_NXT;",syntax.curly[pos].index);
					break;
				} else if(syntax.curly[pos].type == TYPE_WHILE) {
					sprintf(label,"goto __WL%x_NXT;",syntax.curly[pos].index);
					break;
				}
				pos--;
			}
			if(pos < 0) {
				disp_error_message("parse_syntax: unexpected 'continue'",p);
			} else {
				syntax.curly[syntax.curly_count++].type = TYPE_NULL;
				parse_line(label);
				syntax.curly_count--;
			}
			p = skip_space(p2);
			if(*p != ';')
				disp_error_message("parse_syntax: need ';'",p);
			// if, for , while �̕�����
			p = parse_syntax_close(p + 1);
			return p;
		}
		break;
	case 'd':
	case 'D':
		if(p2 - p == 7 && !strncasecmp(p,"default",7)) {
			// switch - default �̏���
			int pos = syntax.curly_count-1;
			if(pos < 0 || syntax.curly[pos].type != TYPE_SWITCH) {
				disp_error_message("parse_syntax: unexpected 'default'",p);
			} else if(syntax.curly[pos].flag) {
				disp_error_message("parse_syntax: dup 'default'",p);
			} else {
				char label[256];
				int l;
				// ���ݒn�̃��x����t����
				p = skip_space(p2);
				if(*p != ':') {
					disp_error_message("parse_syntax: need ':'",p);
				}
				sprintf(label,"__SW%x_%x",syntax.curly[pos].index,syntax.curly[pos].count);
				l=add_str(label);
				set_label(l,script_pos,p);

				// �������Ŏ��̃����N�ɔ�΂�
				sprintf(label,"goto __SW%x_%x;",syntax.curly[pos].index,syntax.curly[pos].count+1);
				syntax.curly[syntax.curly_count++].type = TYPE_NULL;
				parse_line(label);
				syntax.curly_count--;

				// default �̃��x����t����
				sprintf(label,"__SW%x_DEF",syntax.curly[pos].index);
				l=add_str(label);
				set_label(l,script_pos,p);

				syntax.curly[syntax.curly_count - 1].flag = 1;
				syntax.curly[pos].count++;
			}
			return p + 1;
		} else if(p2 - p == 2 && !strncasecmp(p,"do",2)) {
			int l;
			char label[256];
			p=skip_space(p2);

			syntax.curly[syntax.curly_count].type  = TYPE_DO;
			syntax.curly[syntax.curly_count].count = 1;
			syntax.curly[syntax.curly_count].index = syntax.index++;
			syntax.curly[syntax.curly_count].flag  = 0;
			// ���ݒn�̃��x���`������
			sprintf(label,"__DO%x_BGN",syntax.curly[syntax.curly_count].index);
			l=add_str(label);
			set_label(l,script_pos,p);
			syntax.curly_count++;
			return p;
		}
		break;
	case 'f':
	case 'F':
		if(p2 - p == 3 && !strncasecmp(p,"for",3)) {
			int l;
			char label[256];
			int  pos = syntax.curly_count;
			syntax.curly[syntax.curly_count].type  = TYPE_FOR;
			syntax.curly[syntax.curly_count].count = 1;
			syntax.curly[syntax.curly_count].index = syntax.index++;
			syntax.curly[syntax.curly_count].flag  = 0;
			syntax.curly_count++;

			p=skip_space(p2);

			if(*p != '(')
				disp_error_message("parse_syntax: need '('",p);
			p++;

			// �������������s����
			syntax.curly[syntax.curly_count++].type = TYPE_NULL;
			p=parse_line(p);
			syntax.curly_count--;

			// �������f�J�n�̃��x���`������
			sprintf(label,"__FR%x_J",syntax.curly[pos].index);
			l=add_str(label);
			set_label(l,script_pos,p);

			p=skip_space(p);
			if(*p == ';') {
				// for(;;) �̃p�^�[���Ȃ̂ŕK���^
				;
			} else {
				// �������U�Ȃ�I���n�_�ɔ�΂�
				sprintf(label,"__FR%x_FIN",syntax.curly[pos].index);
				add_scriptl(add_str("jump_zero"));
				add_scriptc(C_ARG);
				p=parse_expr(p);
				p=skip_space(p);
				add_scriptl(add_str(label));
				add_scriptc(C_FUNC);
			}
			if(*p != ';')
				disp_error_message("parse_syntax: need ';'",p);
			p++;

			// ���[�v�J�n�ɔ�΂�
			sprintf(label,"goto __FR%x_BGN;",syntax.curly[pos].index);
			syntax.curly[syntax.curly_count++].type = TYPE_NULL;
			parse_line(label);
			syntax.curly_count--;

			// ���̃��[�v�ւ̃��x���`������
			sprintf(label,"__FR%x_NXT",syntax.curly[pos].index);
			l=add_str(label);
			set_label(l,script_pos,p);

			// ���̃��[�v�ɓ��鎞�̏���
			// for �Ō�� ')' �� ';' �Ƃ��Ĉ����t���O
			parse_syntax_for_flag = 1;
			syntax.curly[syntax.curly_count++].type = TYPE_NULL;
			p=parse_line(p);
			syntax.curly_count--;
			parse_syntax_for_flag = 0;

			// �������菈���ɔ�΂�
			sprintf(label,"goto __FR%x_J;",syntax.curly[pos].index);
			syntax.curly[syntax.curly_count++].type = TYPE_NULL;
			parse_line(label);
			syntax.curly_count--;

			// ���[�v�J�n�̃��x���t��
			sprintf(label,"__FR%x_BGN",syntax.curly[pos].index);
			l=add_str(label);
			set_label(l,script_pos,p);
			return p;
		}
		else if( p2 - p == 8 && strncasecmp(p,"function",8) == 0 )
		{// internal script function
			const char *func_name;

			func_name = skip_space(p2);
			p = skip_word(func_name);
			if( p == func_name )
				disp_error_message("parse_syntax:function: function name is missing or invalid", p);
			p2 = skip_space(p);
			if( *p2 == ';' )
			{// function <name> ;
				// function declaration - just register the name
				int l;
				l = add_word(func_name);
				if( str_data[l].type == C_NOP )// register only, if the name was not used by something else
					str_data[l].type = C_USERFUNC;
				else if( str_data[l].type == C_USERFUNC )
					;  // already registered
				else
					disp_error_message("parse_syntax:function: function name is invalid", func_name);

				// if, for , while �̕�����
				p = parse_syntax_close(p2 + 1);
				return p;
			}
			else if(*p2 == '{')
			{// function <name> <line/block of code>
				char label[256];
				int l;

				syntax.curly[syntax.curly_count].type  = TYPE_USERFUNC;
				syntax.curly[syntax.curly_count].count = 1;
				syntax.curly[syntax.curly_count].index = syntax.index++;
				syntax.curly[syntax.curly_count].flag  = 0;
				++syntax.curly_count;

				// Jump over the function code
				sprintf(label, "goto __FN%x_FIN;", syntax.curly[syntax.curly_count-1].index);
				syntax.curly[syntax.curly_count].type = TYPE_NULL;
				++syntax.curly_count;
				parse_line(label);
				--syntax.curly_count;

				// Set the position of the function (label)
				l=add_word(func_name);
				if( str_data[l].type == C_NOP || str_data[l].type == C_USERFUNC )// register only, if the name was not used by something else
				{
					str_data[l].type = C_USERFUNC;
					set_label(l, script_pos, p);
					if( parse_options&SCRIPT_USE_LABEL_DB )
						strdb_put(scriptlabel_db, get_str(l), (void*)script_pos);
				}
				else
					disp_error_message("parse_syntax:function: function name is invalid", func_name);

				return skip_space(p);
			}
			else
			{
				disp_error_message("expect ';' or '{' at function syntax",p);
			}
		}
		break;
	case 'i':
	case 'I':
		if(p2 - p == 2 && !strncasecmp(p,"if",2)) {
			// if() �̏���
			char label[256];
			p=skip_space(p2);
			if(*p != '(') { //Prevent if this {} non-c syntax. from Rayce (jA)
				disp_error_message("need '('",p);
			}
			syntax.curly[syntax.curly_count].type  = TYPE_IF;
			syntax.curly[syntax.curly_count].count = 1;
			syntax.curly[syntax.curly_count].index = syntax.index++;
			syntax.curly[syntax.curly_count].flag  = 0;
			sprintf(label,"__IF%x_%x",syntax.curly[syntax.curly_count].index,syntax.curly[syntax.curly_count].count);
			syntax.curly_count++;
			add_scriptl(add_str("jump_zero"));
			add_scriptc(C_ARG);
			p=parse_expr(p);
			p=skip_space(p);
			add_scriptl(add_str(label));
			add_scriptc(C_FUNC);
			return p;
		}
		break;
	case 's':
	case 'S':
		if(p2 - p == 6 && !strncasecmp(p,"switch",6)) {
			// switch() �̏���
			char label[256];
			p=skip_space(p2);
			if(*p != '(') {
				disp_error_message("need '('",p);
			}
			syntax.curly[syntax.curly_count].type  = TYPE_SWITCH;
			syntax.curly[syntax.curly_count].count = 1;
			syntax.curly[syntax.curly_count].index = syntax.index++;
			syntax.curly[syntax.curly_count].flag  = 0;
			sprintf(label,"$@__SW%x_VAL",syntax.curly[syntax.curly_count].index);
			syntax.curly_count++;
			add_scriptl(add_str("set"));
			add_scriptc(C_ARG);
			add_scriptl(add_str(label));
			p=parse_expr(p);
			p=skip_space(p);
			if(*p != '{') {
				disp_error_message("parse_syntax: need '{'",p);
			}
			add_scriptc(C_FUNC);
			return p + 1;
		}
		break;
	case 'w':
	case 'W':
		if(p2 - p == 5 && !strncasecmp(p,"while",5)) {
			int l;
			char label[256];
			p=skip_space(p2);
			if(*p != '(') {
				disp_error_message("need '('",p);
			}
			syntax.curly[syntax.curly_count].type  = TYPE_WHILE;
			syntax.curly[syntax.curly_count].count = 1;
			syntax.curly[syntax.curly_count].index = syntax.index++;
			syntax.curly[syntax.curly_count].flag  = 0;
			// �������f�J�n�̃��x���`������
			sprintf(label,"__WL%x_NXT",syntax.curly[syntax.curly_count].index);
			l=add_str(label);
			set_label(l,script_pos,p);

			// �������U�Ȃ�I���n�_�ɔ�΂�
			sprintf(label,"__WL%x_FIN",syntax.curly[syntax.curly_count].index);
			syntax.curly_count++;
			add_scriptl(add_str("jump_zero"));
			add_scriptc(C_ARG);
			p=parse_expr(p);
			p=skip_space(p);
			add_scriptl(add_str(label));
			add_scriptc(C_FUNC);
			return p;
		}
		break;
	}
	return NULL;
}

const char* parse_syntax_close(const char *p) {
	// if(...) for(...) hoge(); �̂悤�ɁA�P�x����ꂽ��ēx�����邩�m�F����
	int flag;

	do {
		p = parse_syntax_close_sub(p,&flag);
	} while(flag);
	return p;
}

// if, for , while , do �̕�����
//	 flag == 1 : ����ꂽ
//	 flag == 0 : �����Ȃ�
const char* parse_syntax_close_sub(const char* p,int* flag)
{
	char label[256];
	int pos = syntax.curly_count - 1;
	int l;
	*flag = 1;

	if(syntax.curly_count <= 0) {
		*flag = 0;
		return p;
	} else if(syntax.curly[pos].type == TYPE_IF) {
		const char *bp = p;
		const char *p2;

		// if-block and else-block end is a new line
		parse_nextline(false, p);

		// if �ŏI�ꏊ�֔�΂�
		sprintf(label,"goto __IF%x_FIN;",syntax.curly[pos].index);
		syntax.curly[syntax.curly_count++].type = TYPE_NULL;
		parse_line(label);
		syntax.curly_count--;

		// ���ݒn�̃��x����t����
		sprintf(label,"__IF%x_%x",syntax.curly[pos].index,syntax.curly[pos].count);
		l=add_str(label);
		set_label(l,script_pos,p);

		syntax.curly[pos].count++;
		p = skip_space(p);
		p2 = skip_word(p);
		if(!syntax.curly[pos].flag && p2 - p == 4 && !strncasecmp(p,"else",4)) {
			// else  or else - if
			p = skip_space(p2);
			p2 = skip_word(p);
			if(p2 - p == 2 && !strncasecmp(p,"if",2)) {
				// else - if
				p=skip_space(p2);
				if(*p != '(') {
					disp_error_message("need '('",p);
				}
				sprintf(label,"__IF%x_%x",syntax.curly[pos].index,syntax.curly[pos].count);
				add_scriptl(add_str("jump_zero"));
				add_scriptc(C_ARG);
				p=parse_expr(p);
				p=skip_space(p);
				add_scriptl(add_str(label));
				add_scriptc(C_FUNC);
				*flag = 0;
				return p;
			} else {
				// else
				if(!syntax.curly[pos].flag) {
					syntax.curly[pos].flag = 1;
					*flag = 0;
					return p;
				}
			}
		}
		// if ��
		syntax.curly_count--;
		// �ŏI�n�̃��x����t����
		sprintf(label,"__IF%x_FIN",syntax.curly[pos].index);
		l=add_str(label);
		set_label(l,script_pos,p);
		if(syntax.curly[pos].flag == 1) {
			// ����if�ɑ΂���else����Ȃ��̂Ń|�C���^�̈ʒu�͓���
			return bp;
		}
		return p;
	} else if(syntax.curly[pos].type == TYPE_DO) {
		int l;
		char label[256];
		const char *p2;

		if(syntax.curly[pos].flag) {
			// ���ݒn�̃��x���`������(continue �ł����ɗ���)
			sprintf(label,"__DO%x_NXT",syntax.curly[pos].index);
			l=add_str(label);
			set_label(l,script_pos,p);
		}

		// �������U�Ȃ�I���n�_�ɔ�΂�
		p = skip_space(p);
		p2 = skip_word(p);
		if(p2 - p != 5 || strncasecmp(p,"while",5))
			disp_error_message("parse_syntax: need 'while'",p);

		p = skip_space(p2);
		if(*p != '(') {
			disp_error_message("need '('",p);
		}

		// do-block end is a new line
		parse_nextline(false, p);

		sprintf(label,"__DO%x_FIN",syntax.curly[pos].index);
		add_scriptl(add_str("jump_zero"));
		add_scriptc(C_ARG);
		p=parse_expr(p);
		p=skip_space(p);
		add_scriptl(add_str(label));
		add_scriptc(C_FUNC);

		// �J�n�n�_�ɔ�΂�
		sprintf(label,"goto __DO%x_BGN;",syntax.curly[pos].index);
		syntax.curly[syntax.curly_count++].type = TYPE_NULL;
		parse_line(label);
		syntax.curly_count--;

		// �����I���n�_�̃��x���`������
		sprintf(label,"__DO%x_FIN",syntax.curly[pos].index);
		l=add_str(label);
		set_label(l,script_pos,p);
		p = skip_space(p);
		if(*p != ';') {
			disp_error_message("parse_syntax: need ';'",p);
			return p+1;
		}
		p++;
		syntax.curly_count--;
		return p;
	} else if(syntax.curly[pos].type == TYPE_FOR) {
		// for-block end is a new line
		parse_nextline(false, p);

		// ���̃��[�v�ɔ�΂�
		sprintf(label,"goto __FR%x_NXT;",syntax.curly[pos].index);
		syntax.curly[syntax.curly_count++].type = TYPE_NULL;
		parse_line(label);
		syntax.curly_count--;

		// for �I���̃��x���t��
		sprintf(label,"__FR%x_FIN",syntax.curly[pos].index);
		l=add_str(label);
		set_label(l,script_pos,p);
		syntax.curly_count--;
		return p;
	} else if(syntax.curly[pos].type == TYPE_WHILE) {
		// while-block end is a new line
		parse_nextline(false, p);

		// while �������f�֔�΂�
		sprintf(label,"goto __WL%x_NXT;",syntax.curly[pos].index);
		syntax.curly[syntax.curly_count++].type = TYPE_NULL;
		parse_line(label);
		syntax.curly_count--;

		// while �I���̃��x���t��
		sprintf(label,"__WL%x_FIN",syntax.curly[pos].index);
		l=add_str(label);
		set_label(l,script_pos,p);
		syntax.curly_count--;
		return p;
	} else if(syntax.curly[syntax.curly_count-1].type == TYPE_USERFUNC) {
		int pos = syntax.curly_count-1;
		char label[256];
		int l;
		// �߂�
		sprintf(label,"return;");
		syntax.curly[syntax.curly_count++].type = TYPE_NULL;
		parse_line(label);
		syntax.curly_count--;

		// ���ݒn�̃��x����t����
		sprintf(label,"__FN%x_FIN",syntax.curly[pos].index);
		l=add_str(label);
		set_label(l,script_pos,p);
		syntax.curly_count--;
		return p;
	} else {
		*flag = 0;
		return p;
	}
}

/*==========================================
 * �g�ݍ��݊֐��̒ǉ�
 *------------------------------------------*/
static void add_buildin_func(void)
{
	int i,n;
	const char* p;
	for( i = 0; buildin_func[i].func; i++ )
	{
		// arg must follow the pattern: (v|s|i|r|l)*\?*\*?
		// 'v' - value (either string or int or reference)
		// 's' - string
		// 'i' - int
		// 'r' - reference (of a variable)
		// 'l' - label
		// '?' - one optional parameter
		// '*' - unknown number of optional parameters
		p = buildin_func[i].arg;
		while( *p == 'v' || *p == 's' || *p == 'i' || *p == 'r' || *p == 'l' ) ++p;
		while( *p == '?' ) ++p;
		if( *p == '*' ) ++p;
		if( *p != 0){
			ShowWarning("add_buildin_func: ignoring function \"%s\" with invalid arg \"%s\".\n", buildin_func[i].name, buildin_func[i].arg);
		} else if( *skip_word(buildin_func[i].name) != 0 ){
			ShowWarning("add_buildin_func: ignoring function with invalid name \"%s\" (must be a word).\n", buildin_func[i].name);
		} else {
			n = add_str(buildin_func[i].name);
			str_data[n].type = C_FUNC;
			str_data[n].val = i;
			str_data[n].func = buildin_func[i].func;
		}
	}
}

/// Retrieves the value of a constant parameter.
bool script_get_parameter(const char* name, int* value)
{
	int n = search_str(name);

	if (n == -1 || str_data[n].type != C_PARAM)
	{// not found or not a parameter
		return false;
	}
	value[0] = str_data[n].val;

	return true;
}

/// Retrieves the value of a constant.
bool script_get_constant(const char* name, int* value)
{
	int n = search_str(name);

	if( n == -1 || str_data[n].type != C_INT )
	{// not found or not a constant
		return false;
	}
	value[0] = str_data[n].val;

	return true;
}

/// Creates new constant or parameter with given value.
void script_set_constant_(const char* name, int value, const char* constant_name, bool isparameter)
{
	int n = add_str(name);

	if( str_data[n].type == C_NOP )
	{// new
		str_data[n].type = isparameter ? C_PARAM : C_INT;
		str_data[n].val  = value;
		str_data[n].name = constant_name;
	}
	else if( str_data[n].type == C_PARAM || str_data[n].type == C_INT )
	{// existing parameter or constant
		ShowError("script_set_constant: Attempted to overwrite existing %s '%s' (old value=%d, new value=%d).\n", ( str_data[n].type == C_PARAM ) ? "parameter" : "constant", name, str_data[n].val, value);
	}
	else
	{// existing name
		ShowError("script_set_constant: Invalid name for %s '%s' (already defined as %s).\n", isparameter ? "parameter" : "constant", name, script_op2name(str_data[n].type));
	}
}

/*==========================================
 * �萔�f�[�^�x�[�X�̓ǂݍ���
 *------------------------------------------*/
static void read_constdb(void)
{
	FILE *fp;
	char line[1024],name[1024],val[1024];
	int type;

	sprintf(line, "%s/const.txt", db_path);
	fp=fopen(line, "r");
	if(fp==NULL){
		ShowError("can't read %s\n", line);
		return ;
	}
	while(fgets(line, sizeof(line), fp))
	{
		if(line[0]=='/' && line[1]=='/')
			continue;
		type=0;
		if(sscanf(line,"%[A-Za-z0-9_],%[-0-9xXA-Fa-f],%d",name,val,&type)>=2 ||
		   sscanf(line,"%[A-Za-z0-9_] %[-0-9xXA-Fa-f] %d",name,val,&type)>=2){
			script_set_constant(name, (int)strtol(val, NULL, 0), (bool)type);
		}
	}
	fclose(fp);
}

/*==========================================
 * �G���[�\��
 *------------------------------------------*/
static const char* script_print_line(StringBuf* buf, const char* p, const char* mark, int line)
{
	int i;
	if( p == NULL || !p[0] ) return NULL;
	if( line < 0 ) 
		StringBuf_Printf(buf, "*% 5d : ", -line);
	else
		StringBuf_Printf(buf, " % 5d : ", line);
	for(i=0;p[i] && p[i] != '\n';i++){
		if(p + i != mark)
			StringBuf_Printf(buf, "%c", p[i]);
		else
			StringBuf_Printf(buf, "\'%c\'", p[i]);
	}
	StringBuf_AppendStr(buf, "\n");
	return p+i+(p[i] == '\n' ? 1 : 0);
}

void script_error(const char* src, const char* file, int start_line, const char* error_msg, const char* error_pos)
{
	// �G���[�����������s�����߂�
	int j;
	int line = start_line;
	const char *p;
	const char *linestart[5] = { NULL, NULL, NULL, NULL, NULL };
	StringBuf buf;

	for(p=src;p && *p;line++){
		const char *lineend=strchr(p,'\n');
		if(lineend==NULL || error_pos<lineend){
			break;
		}
		for( j = 0; j < 4; j++ ) {
			linestart[j] = linestart[j+1];
		}
		linestart[4] = p;
		p=lineend+1;
	}

	StringBuf_Init(&buf);
	StringBuf_AppendStr(&buf, "\a\n");
	StringBuf_Printf(&buf, "script error on %s line %d\n", file, line);
	StringBuf_Printf(&buf, "    %s\n", error_msg);
	for(j = 0; j < 5; j++ ) {
		script_print_line(&buf, linestart[j], NULL, line + j - 5);
	}
	p = script_print_line(&buf, p, error_pos, -line);
	for(j = 0; j < 5; j++) {
		p = script_print_line(&buf, p, NULL, line + j + 1 );
	}
	ShowError("%s", StringBuf_Value(&buf));
	StringBuf_Destroy(&buf);
}

/*==========================================
 * �X�N���v�g�̉��
 *------------------------------------------*/
struct script_code* parse_script(const char *src,const char *file,int line,int options)
{
	const char *p,*tmpp;
	int i;
	struct script_code* code = NULL;
	static int first=1;
	char end;
	bool unresolved_names = false;

	if( src == NULL )
		return NULL;// empty script

	memset(&syntax,0,sizeof(syntax));
	if(first){
		add_buildin_func();
		read_constdb();
		first=0;
	}

	script_buf=(unsigned char *)aMalloc(SCRIPT_BLOCK_SIZE*sizeof(unsigned char));
	script_pos=0;
	script_size=SCRIPT_BLOCK_SIZE;
	parse_nextline(true, NULL);

	// who called parse_script is responsible for clearing the database after using it, but just in case... lets clear it here
	if( options&SCRIPT_USE_LABEL_DB )
		scriptlabel_db->clear(scriptlabel_db, NULL);
	parse_options = options;

	if( setjmp( error_jump ) != 0 ) {
		//Restore program state when script has problems. [from jA]
		int i;
		const int size = ARRAYLENGTH(syntax.curly);
		if( error_report )
			script_error(src,file,line,error_msg,error_pos);
		aFree( error_msg );
		aFree( script_buf );
		script_pos  = 0;
		script_size = 0;
		script_buf  = NULL;
		for(i=LABEL_START;i<str_num;i++)
			if(str_data[i].type == C_NOP) str_data[i].type = C_NAME;
		for(i=0; i<size; i++)
			linkdb_final(&syntax.curly[i].case_label);
		return NULL;
	}

	parse_syntax_for_flag=0;
	p=src;
	p=skip_space(p);
	if( options&SCRIPT_IGNORE_EXTERNAL_BRACKETS )
	{// does not require brackets around the script
		if( *p == '\0' && !(options&SCRIPT_RETURN_EMPTY_SCRIPT) )
		{// empty script and can return NULL
			aFree( script_buf );
			script_pos  = 0;
			script_size = 0;
			script_buf  = NULL;
			return NULL;
		}
		end = '\0';
	}
	else
	{// requires brackets around the script
		if( *p != '{' )
			disp_error_message("not found '{'",p);
		p = skip_space(p+1);
		if( *p == '}' && !(options&SCRIPT_RETURN_EMPTY_SCRIPT) )
		{// empty script and can return NULL
			aFree( script_buf );
			script_pos  = 0;
			script_size = 0;
			script_buf  = NULL;
			return NULL;
		}
		end = '}';
	}

	// clear references of labels, variables and internal functions
	for(i=LABEL_START;i<str_num;i++){
		if(
			str_data[i].type==C_POS || str_data[i].type==C_NAME ||
			str_data[i].type==C_USERFUNC || str_data[i].type == C_USERFUNC_POS
		){
			str_data[i].type=C_NOP;
			str_data[i].backpatch=-1;
			str_data[i].label=-1;
		}
	}

	while( syntax.curly_count != 0 || *p != end )
	{
		if( *p == '\0' )
			disp_error_message("unexpected end of script",p);
		// label�������ꏈ��
		tmpp=skip_space(skip_word(p));
		if(*tmpp==':' && !(!strncasecmp(p,"default:",8) && p + 7 == tmpp)){
			i=add_word(p);
			set_label(i,script_pos,p);
			if( parse_options&SCRIPT_USE_LABEL_DB )
				strdb_put(scriptlabel_db, get_str(i), (void*)script_pos);
			p=tmpp+1;
			p=skip_space(p);
			continue;
		}

		// ���͑S���ꏏ����
		p=parse_line(p);
		p=skip_space(p);

		parse_nextline(false, p);
	}

	add_scriptc(C_NOP);

	// trim code to size
	script_size = script_pos;
	RECREATE(script_buf,unsigned char,script_pos);

	// default unknown references to variables
	for(i=LABEL_START;i<str_num;i++){
		if(str_data[i].type==C_NOP){
			int j,next;
			str_data[i].type=C_NAME;
			str_data[i].label=i;
			for(j=str_data[i].backpatch;j>=0 && j!=0x00ffffff;){
				next=GETVALUE(script_buf,j);
				SETVALUE(script_buf,j,i);
				j=next;
			}
		}
		else if( str_data[i].type == C_USERFUNC )
		{// 'function name;' without follow-up code
			ShowError("parse_script: function '%s' declared but not defined.\n", str_buf+str_data[i].str);
			unresolved_names = true;
		}
	}

	if( unresolved_names )
	{
		disp_error_message("parse_script: unresolved function references", p);
	}

#ifdef DEBUG_DISP
	for(i=0;i<script_pos;i++){
		if((i&15)==0) ShowMessage("%04x : ",i);
		ShowMessage("%02x ",script_buf[i]);
		if((i&15)==15) ShowMessage("\n");
	}
	ShowMessage("\n");
#endif
#ifdef DEBUG_DISASM
	{
		int i = 0,j;
		while(i < script_pos) {
			c_op op = get_com(script_buf,&i);

			ShowMessage("%06x %s", i, script_op2name(op));
			j = i;
			switch(op) {
			case C_INT:
				ShowMessage(" %d", get_num(script_buf,&i));
				break;
			case C_POS:
				ShowMessage(" 0x%06x", *(int*)(script_buf+i)&0xffffff);
				i += 3;
				break;
			case C_NAME:
				j = (*(int*)(script_buf+i)&0xffffff);
				ShowMessage(" %s", ( j == 0xffffff ) ? "?? unknown ??" : get_str(j));
				i += 3;
				break;
			case C_STR:
				j = strlen(script_buf + i);
				ShowMessage(" %s", script_buf + i);
				i += j+1;
				break;
			}
			ShowMessage(CL_CLL"\n");
		}
	}
#endif

	CREATE(code,struct script_code,1);
	code->script_buf  = script_buf;
	code->script_size = script_size;
	code->script_vars = NULL;
	return code;
}

/// Returns the player attached to this script, identified by the rid.
/// If there is no player attached, the script is terminated.
TBL_PC *script_rid2sd(struct script_state *st)
{
	TBL_PC *sd=map_id2sd(st->rid);
	if(!sd){
		ShowError("script_rid2sd: fatal error ! player not attached!\n");
		script_reportfunc(st);
		script_reportsrc(st);
		st->state = END;
	}
	return sd;
}

/// Dereferences a variable/constant, replacing it with a copy of the value.
///
/// @param st Script state
/// @param data Variable/constant
void get_val(struct script_state* st, struct script_data* data)
{
	const char* name;
	char prefix;
	char postfix;
	TBL_PC* sd = NULL;

	if( !data_isreference(data) )
		return;// not a variable/constant

	name = reference_getname(data);
	prefix = name[0];
	postfix = name[strlen(name) - 1];

	//##TODO use reference_tovariable(data) when it's confirmed that it works [FlavioJS]
	if( !reference_toconstant(data) && not_server_variable(prefix) )
	{
		sd = script_rid2sd(st);
		if( sd == NULL )
		{// needs player attached
			if( postfix == '$' )
			{// string variable
				ShowWarning("script:get_val: cannot access player variable '%s', defaulting to \"\"\n", name);
				data->type = C_CONSTSTR;
				data->u.str = "";
			}
			else
			{// integer variable
				ShowWarning("script:get_val: cannot access player variable '%s', defaulting to 0\n", name);
				data->type = C_INT;
				data->u.num = 0;
			}
			return;
		}
	}

	if( postfix == '$' )
	{// string variable

		switch( prefix )
		{
		case '@':
			data->u.str = pc_readregstr(sd, data->u.num);
			break;
		case '$':
			data->u.str = mapreg_readregstr(data->u.num);
			break;
		case '#':
			if( name[1] == '#' )
				data->u.str = pc_readaccountreg2str(sd, name);// global
			else
				data->u.str = pc_readaccountregstr(sd, name);// local
			break;
		case '.':
			{
				struct linkdb_node** n =
					data->ref      ? data->ref:
					name[1] == '@' ? st->stack->var_function:// instance/scope variable
					                 &st->script->script_vars;// npc variable
				data->u.str = (char*)linkdb_search(n, (void*)reference_getuid(data));
			}
			break;
		case '\'':
			{
				struct linkdb_node** n = NULL;
				if( st->instance_id )
					n = &instance[st->instance_id].svar;
				data->u.str = (char*)linkdb_search(n, (void*)reference_getuid(data));
			}
			break;
		default:
			data->u.str = pc_readglobalreg_str(sd, name);
			break;
		}

		if( data->u.str == NULL || data->u.str[0] == '\0' )
		{// empty string
			data->type = C_CONSTSTR;
			data->u.str = "";
		}
		else
		{// duplicate string
			data->type = C_STR;
			data->u.str = aStrdup(data->u.str);
		}

	}
	else
	{// integer variable

		data->type = C_INT;

		if( reference_toconstant(data) )
		{
			data->u.num = reference_getconstant(data);
		}
		else if( reference_toparam(data) )
		{
			data->u.num = pc_readparam(sd, reference_getparamtype(data));
		}
		else
		switch( prefix )
		{
		case '@':
			data->u.num = pc_readreg(sd, data->u.num);
			break;
		case '$':
			data->u.num = mapreg_readreg(data->u.num);
			break;
		case '#':
			if( name[1] == '#' )
				data->u.num = pc_readaccountreg2(sd, name);// global
			else
				data->u.num = pc_readaccountreg(sd, name);// local
			break;
		case '.':
			{
				struct linkdb_node** n =
					data->ref      ? data->ref:
					name[1] == '@' ? st->stack->var_function:// instance/scope variable
					                 &st->script->script_vars;// npc variable
				data->u.num = (int)linkdb_search(n, (void*)reference_getuid(data));
			}
			break;
		case '\'':
			{
				struct linkdb_node** n = NULL;
				if( st->instance_id )
					n = &instance[st->instance_id].ivar;
				data->u.num = (int)linkdb_search(n, (void*)reference_getuid(data));
			}
			break;
		default:
			data->u.num = pc_readglobalreg(sd, name);
			break;
		}

	}

	return;
}

struct script_data* push_val2(struct script_stack* stack, enum c_op type, int val, struct linkdb_node** ref);

/// Retrieves the value of a reference identified by uid (variable, constant, param)
/// The value is left in the top of the stack and needs to be removed manually.
void* get_val2(struct script_state* st, int uid, struct linkdb_node** ref)
{
	struct script_data* data;
	push_val2(st->stack, C_NAME, uid, ref);
	data = script_getdatatop(st, -1);
	get_val(st, data);
	return (data->type == C_INT ? (void*)data->u.num : (void*)data->u.str);
}

/*==========================================
 * Stores the value of a script variable
 * Return value is 0 on fail, 1 on success.
 *------------------------------------------*/
static int set_reg(struct script_state* st, TBL_PC* sd, int num, const char* name, const void* value, struct linkdb_node** ref)
{
	char prefix = name[0];

	if( is_string_variable(name) )
	{// string variable
		const char* str = (const char*)value;
		switch (prefix) {
		case '@':
			return pc_setregstr(sd, num, str);
		case '$':
			return mapreg_setregstr(num, str);
		case '#':
			return (name[1] == '#') ?
				pc_setaccountreg2str(sd, name, str) :
				pc_setaccountregstr(sd, name, str);
		case '.': {
			char* p;
			struct linkdb_node** n;
			n = (ref) ? ref : (name[1] == '@') ? st->stack->var_function : &st->script->script_vars;
			p = (char*)linkdb_erase(n, (void*)num);
			if (p) aFree(p);
			if (str[0]) linkdb_insert(n, (void*)num, aStrdup(str));
			}
			return 1;
		case '\'': {
			char *p;
			struct linkdb_node** n = NULL;
			if( st->instance_id )
				n = &instance[st->instance_id].svar;

			p = (char*)linkdb_erase(n, (void*)num);
			if (p) aFree(p);
			if( str[0] ) linkdb_insert(n, (void*)num, aStrdup(str));
			}
			return 1;
		default:
			return pc_setglobalreg_str(sd, name, str);
		}
	}
	else
	{// integer variable
		int val = (int)value;
		if(str_data[num&0x00ffffff].type == C_PARAM)
		{
			if( pc_setparam(sd, str_data[num&0x00ffffff].val, val) == 0 )
			{
				if( st != NULL )
				{
					ShowError("script:set_reg: failed to set param '%s' to %d.\n", name, val);
					script_reportsrc(st);
					st->state = END;
				}
				return 0;
			}
			return 1;
		}

		switch (prefix) {
		case '@':
			return pc_setreg(sd, num, val);
		case '$':
			return mapreg_setreg(num, val);
		case '#':
			return (name[1] == '#') ?
				pc_setaccountreg2(sd, name, val) :
				pc_setaccountreg(sd, name, val);
		case '.': {
			struct linkdb_node** n;
			n = (ref) ? ref : (name[1] == '@') ? st->stack->var_function : &st->script->script_vars;
			if (val == 0)
				linkdb_erase(n, (void*)num);
			else 
				linkdb_replace(n, (void*)num, (void*)val);
			}
			return 1;
		case '\'':
			{
				struct linkdb_node** n = NULL;
				if( st->instance_id )
					n = &instance[st->instance_id].ivar;

				if( val == 0 )
					linkdb_erase(n, (void*)num);
				else
					linkdb_replace(n, (void*)num, (void*)val);
				return 1;
			}
		default:
			return pc_setglobalreg(sd, name, val);
		}
	}
}

int set_var(TBL_PC* sd, char* name, void* val)
{
    return set_reg(NULL, sd, reference_uid(add_str(name),0), name, val, NULL);
}

void setd_sub(struct script_state *st, TBL_PC *sd, const char *varname, int elem, void *value, struct linkdb_node **ref)
{
	set_reg(st, sd, reference_uid(add_str(varname),elem), varname, value, ref);
}

/// Converts the data to a string
const char* conv_str(struct script_state* st, struct script_data* data)
{
	char* p;

	get_val(st, data);
	if( data_isstring(data) )
	{// nothing to convert
	}
	else if( data_isint(data) )
	{// int -> string
		CREATE(p, char, ITEM_NAME_LENGTH);
		snprintf(p, ITEM_NAME_LENGTH, "%d", data->u.num);
		p[ITEM_NAME_LENGTH-1] = '\0';
		data->type = C_STR;
		data->u.str = p;
	}
	else if( data_isreference(data) )
	{// reference -> string
		//##TODO when does this happen (check get_val) [FlavioJS]
		data->type = C_CONSTSTR;
		data->u.str = reference_getname(data);
	}
	else
	{// unsupported data type
		ShowError("script:conv_str: cannot convert to string, defaulting to \"\"\n");
		script_reportdata(data);
		script_reportsrc(st);
		data->type = C_CONSTSTR;
		data->u.str = "";
	}
	return data->u.str;
}

/// Converts the data to an int
int conv_num(struct script_state* st, struct script_data* data)
{
	char* p;
	long num;

	get_val(st, data);
	if( data_isint(data) )
	{// nothing to convert
	}
	else if( data_isstring(data) )
	{// string -> int
		// the result does not overflow or underflow, it is capped instead
		// ex: 999999999999 is capped to INT_MAX (2147483647)
		p = data->u.str;
		errno = 0;
		num = strtol(data->u.str, NULL, 10);// change radix to 0 to support octal numbers "o377" and hex numbers "0xFF"
		if( errno == ERANGE
#if LONG_MAX > INT_MAX
			|| num < INT_MIN || num > INT_MAX
#endif
			)
		{
			if( num <= INT_MIN )
			{
				num = INT_MIN;
				ShowError("script:conv_num: underflow detected, capping to %ld\n", num);
			}
			else//if( num >= INT_MAX )
			{
				num = INT_MAX;
				ShowError("script:conv_num: overflow detected, capping to %ld\n", num);
			}
			script_reportdata(data);
			script_reportsrc(st);
		}
		if( data->type == C_STR )
			aFree(p);
		data->type = C_INT;
		data->u.num = (int)num;
	}
#if 0
	// FIXME this function is being used to retrieve the position of labels and 
	// probably other stuff [FlavioJS]
	else
	{// unsupported data type
		ShowError("script:conv_num: cannot convert to number, defaulting to 0\n");
		script_reportdata(data);
		script_reportsrc(st);
		data->type = C_INT;
		data->u.num = 0;
	}
#endif
	return data->u.num;
}

//
// Stack operations
//

/// Increases the size of the stack
void stack_expand(struct script_stack* stack)
{
	stack->sp_max += 64;
	stack->stack_data = (struct script_data*)aRealloc(stack->stack_data,
			stack->sp_max * sizeof(stack->stack_data[0]) );
	memset(stack->stack_data + (stack->sp_max - 64), 0,
			64 * sizeof(stack->stack_data[0]) );
}

/// Pushes a value into the stack
#define push_val(stack,type,val) push_val2(stack, type, val, NULL)

/// Pushes a value into the stack (with reference)
struct script_data* push_val2(struct script_stack* stack, enum c_op type, int val, struct linkdb_node** ref)
{
	if( stack->sp >= stack->sp_max )
		stack_expand(stack);
	stack->stack_data[stack->sp].type  = type;
	stack->stack_data[stack->sp].u.num = val;
	stack->stack_data[stack->sp].ref   = ref;
	stack->sp++;
	return &stack->stack_data[stack->sp-1];
}

/// Pushes a string into the stack
struct script_data* push_str(struct script_stack* stack, enum c_op type, char* str)
{
	if( stack->sp >= stack->sp_max )
		stack_expand(stack);
	stack->stack_data[stack->sp].type  = type;
	stack->stack_data[stack->sp].u.str = str;
	stack->stack_data[stack->sp].ref   = NULL;
	stack->sp++;
	return &stack->stack_data[stack->sp-1];
}

/// Pushes a retinfo into the stack
struct script_data* push_retinfo(struct script_stack* stack, struct script_retinfo* ri)
{
	if( stack->sp >= stack->sp_max )
		stack_expand(stack);
	stack->stack_data[stack->sp].type = C_RETINFO;
	stack->stack_data[stack->sp].u.ri = ri;
	stack->stack_data[stack->sp].ref  = NULL;
	stack->sp++;
	return &stack->stack_data[stack->sp-1];
}

/// Pushes a copy of the target position into the stack
struct script_data* push_copy(struct script_stack* stack, int pos)
{
	switch( stack->stack_data[pos].type )
	{
	case C_CONSTSTR:
		return push_str(stack, C_CONSTSTR, stack->stack_data[pos].u.str);
		break;
	case C_STR:
		return push_str(stack, C_STR, aStrdup(stack->stack_data[pos].u.str));
		break;
	case C_RETINFO:
		ShowFatalError("script:push_copy: can't create copies of C_RETINFO. Exiting...\n");
		exit(1);
		break;
	default:
		return push_val2(
			stack,stack->stack_data[pos].type,
			stack->stack_data[pos].u.num,
			stack->stack_data[pos].ref
		);
		break;
	}
}

/// Removes the values in indexes [start,end[ from the stack.
/// Adjusts all stack pointers.
void pop_stack(struct script_state* st, int start, int end)
{
	struct script_stack* stack = st->stack;
	struct script_data* data;
	int i;

	if( start < 0 )
		start = 0;
	if( end > stack->sp )
		end = stack->sp;
	if( start >= end )
		return;// nothing to pop

	// free stack elements
	for( i = start; i < end; i++ )
	{
		data = &stack->stack_data[i];
		if( data->type == C_STR )
			aFree(data->u.str);
		if( data->type == C_RETINFO )
		{
			struct script_retinfo* ri = data->u.ri;
			if( ri->var_function )
			{
				script_free_vars(ri->var_function);
				aFree(ri->var_function);
			}
			aFree(ri);
		}
		data->type = C_NOP;
	}
	// move the rest of the elements
	if( stack->sp > end )
	{
		memmove(&stack->stack_data[start], &stack->stack_data[end], sizeof(stack->stack_data[0])*(stack->sp - end));
		for( i = start + stack->sp - end; i < stack->sp; ++i )
			stack->stack_data[i].type = C_NOP;
	}
	// adjust stack pointers
	     if( st->start > end )   st->start -= end - start;
	else if( st->start > start ) st->start = start;
	     if( st->end > end )   st->end -= end - start;
	else if( st->end > start ) st->end = start;
	     if( stack->defsp > end )   stack->defsp -= end - start;
	else if( stack->defsp > start ) stack->defsp = start;
	stack->sp -= end - start;
}

///
///
///

/*==========================================
 * �X�N���v�g�ˑ��ϐ��A�֐��ˑ��ϐ��̉��
 *------------------------------------------*/
void script_free_vars(struct linkdb_node **node)
{
	struct linkdb_node* n = *node;
	while( n != NULL)
	{
		const char* name = get_str((int)(n->key)&0x00ffffff);
		if( is_string_variable(name) )
			aFree(n->data); // �����^�ϐ��Ȃ̂ŁA�f�[�^�폜
		n = n->next;
	}
	linkdb_final( node );
}

void script_free_code(struct script_code* code)
{
	script_free_vars( &code->script_vars );
	aFree( code->script_buf );
	aFree( code );
}

/// Creates a new script state.
///
/// @param script Script code
/// @param pos Position in the code
/// @param rid Who is running the script (attached player)
/// @param oid Where the code is being run (npc 'object')
/// @return Script state
struct script_state* script_alloc_state(struct script_code* script, int pos, int rid, int oid)
{
	struct script_state* st;
	CREATE(st, struct script_state, 1);
	st->stack = (struct script_stack*)aMalloc(sizeof(struct script_stack));
	st->stack->sp = 0;
	st->stack->sp_max = 64;
	CREATE(st->stack->stack_data, struct script_data, st->stack->sp_max);
	st->stack->defsp = st->stack->sp;
	CREATE(st->stack->var_function, struct linkdb_node*, 1);
	st->state = RUN;
	st->script = script;
	//st->scriptroot = script;
	st->pos = pos;
	st->rid = rid;
	st->oid = oid;
	st->sleep.timer = INVALID_TIMER;
	return st;
}

/// Frees a script state.
///
/// @param st Script state
void script_free_state(struct script_state* st)
{
	if(st->bk_st)
	{// backup was not restored
		ShowDebug("script_free_state: Previous script state lost (rid=%d, oid=%d, state=%d, bk_npcid=%d).\n", st->bk_st->rid, st->bk_st->oid, st->bk_st->state, st->bk_npcid);
	}
	if( st->sleep.timer != INVALID_TIMER )
		delete_timer(st->sleep.timer, run_script_timer);
	script_free_vars(st->stack->var_function);
	aFree(st->stack->var_function);
	pop_stack(st, 0, st->stack->sp);
	aFree(st->stack->stack_data);
	aFree(st->stack);
	st->pos = -1;
	aFree(st);
}

//
// ���s��main
//
/*==========================================
 * �R�}���h�̓ǂݎ��
 *------------------------------------------*/
c_op get_com(unsigned char *script,int *pos)
{
	int i = 0, j = 0;

	if(script[*pos]>=0x80){
		return C_INT;
	}
	while(script[*pos]>=0x40){
		i=script[(*pos)++]<<j;
		j+=6;
	}
	return (c_op)(i+(script[(*pos)++]<<j));
}

/*==========================================
 * ���l�̏���
 *------------------------------------------*/
int get_num(unsigned char *script,int *pos)
{
	int i,j;
	i=0; j=0;
	while(script[*pos]>=0xc0){
		i+=(script[(*pos)++]&0x7f)<<j;
		j+=6;
	}
	return i+((script[(*pos)++]&0x7f)<<j);
}

/*==========================================
 * �X�^�b�N����l�����o��
 *------------------------------------------*/
int pop_val(struct script_state* st)
{
	if(st->stack->sp<=0)
		return 0;
	st->stack->sp--;
	get_val(st,&(st->stack->stack_data[st->stack->sp]));
	if(st->stack->stack_data[st->stack->sp].type==C_INT)
		return st->stack->stack_data[st->stack->sp].u.num;
	return 0;
}

/// Ternary operators
/// test ? if_true : if_false
void op_3(struct script_state* st, int op)
{
	struct script_data* data;
	int flag = 0;

	data = script_getdatatop(st, -3);
	get_val(st, data);

	if( data_isstring(data) )
		flag = data->u.str[0];// "" -> false
	else if( data_isint(data) )
		flag = data->u.num;// 0 -> false
	else
	{
		ShowError("script:op_3: invalid data for the ternary operator test\n");
		script_reportdata(data);
		script_reportsrc(st);
		script_removetop(st, -3, 0);
		script_pushnil(st);
		return;
	}
	if( flag )
		script_pushcopytop(st, -2);
	else
		script_pushcopytop(st, -1);
	script_removetop(st, -4, -1);
}

/// Binary string operators
/// s1 EQ s2 -> i
/// s1 NE s2 -> i
/// s1 GT s2 -> i
/// s1 GE s2 -> i
/// s1 LT s2 -> i
/// s1 LE s2 -> i
/// s1 ADD s2 -> s
void op_2str(struct script_state* st, int op, const char* s1, const char* s2)
{
	int a = 0;

	switch(op){
	case C_EQ: a = (strcmp(s1,s2) == 0); break;
	case C_NE: a = (strcmp(s1,s2) != 0); break;
	case C_GT: a = (strcmp(s1,s2) >  0); break;
	case C_GE: a = (strcmp(s1,s2) >= 0); break;
	case C_LT: a = (strcmp(s1,s2) <  0); break;
	case C_LE: a = (strcmp(s1,s2) <= 0); break;
	case C_ADD:
		{
			char* buf = (char *)aMallocA((strlen(s1)+strlen(s2)+1)*sizeof(char));
			strcpy(buf, s1);
			strcat(buf, s2);
			script_pushstr(st, buf);
			return;
		}
	default:
		ShowError("script:op2_str: unexpected string operator %s\n", script_op2name(op));
		script_reportsrc(st);
		script_pushnil(st);
		st->state = END;
		return;
	}

	script_pushint(st,a);
}

/// Binary number operators
/// i OP i -> i
void op_2num(struct script_state* st, int op, int i1, int i2)
{
	int ret;
	double ret_double;

	switch( op )
	{
	case C_AND:  ret = i1 & i2;		break;
	case C_OR:   ret = i1 | i2;		break;
	case C_XOR:  ret = i1 ^ i2;		break;
	case C_LAND: ret = (i1 && i2);	break;
	case C_LOR:  ret = (i1 || i2);	break;
	case C_EQ:   ret = (i1 == i2);	break;
	case C_NE:   ret = (i1 != i2);	break;
	case C_GT:   ret = (i1 >  i2);	break;
	case C_GE:   ret = (i1 >= i2);	break;
	case C_LT:   ret = (i1 <  i2);	break;
	case C_LE:   ret = (i1 <= i2);	break;
	case C_R_SHIFT: ret = i1>>i2;	break;
	case C_L_SHIFT: ret = i1<<i2;	break;
	case C_DIV:
	case C_MOD:
		if( i2 == 0 )
		{
			ShowError("script:op_2num: division by zero detected op=%s i1=%d i2=%d\n", script_op2name(op), i1, i2);
			script_reportsrc(st);
			script_pushnil(st);
			st->state = END;
			return;
		}
		else if( op == C_DIV )
			ret = i1 / i2;
		else//if( op == C_MOD )
			ret = i1 % i2;
		break;
	default:
		switch( op )
		{// operators that can overflow/underflow
		case C_ADD: ret = i1 + i2; ret_double = (double)i1 + (double)i2; break;
		case C_SUB: ret = i1 - i2; ret_double = (double)i1 - (double)i2; break;
		case C_MUL: ret = i1 * i2; ret_double = (double)i1 * (double)i2; break;
		default:
			ShowError("script:op_2num: unexpected number operator %s i1=%d i2=%d\n", script_op2name(op), i1, i2);
			script_reportsrc(st);
			script_pushnil(st);
			return;
		}
		if( ret_double < (double)INT_MIN )
		{
			ShowWarning("script:op_2num: underflow detected op=%s i1=%d i2=%d\n", script_op2name(op), i1, i2);
			script_reportsrc(st);
			ret = INT_MIN;
		}
		else if( ret_double > (double)INT_MAX )
		{
			ShowWarning("script:op_2num: overflow detected op=%s i1=%d i2=%d\n", script_op2name(op), i1, i2);
			script_reportsrc(st);
			ret = INT_MAX;
		}
	}
	script_pushint(st, ret);
}

/// Binary operators
void op_2(struct script_state *st, int op)
{
	struct script_data* left;
	struct script_data* right;

	left = script_getdatatop(st, -2);
	right = script_getdatatop(st, -1);

	get_val(st, left);
	get_val(st, right);

	// automatic conversions
	switch( op )
	{
	case C_ADD:
		if( data_isint(left) && data_isstring(right) )
		{// convert int-string to string-string
			conv_str(st, left);
		}
		else if( data_isstring(left) && data_isint(right) )
		{// convert string-int to string-string
			conv_str(st, right);
		}
		break;
	}

	if( data_isstring(left) && data_isstring(right) )
	{// ss => op_2str
		op_2str(st, op, left->u.str, right->u.str);
		script_removetop(st, -3, -1);// pop the two values before the top one
	}
	else if( data_isint(left) && data_isint(right) )
	{// ii => op_2num
		int i1 = left->u.num;
		int i2 = right->u.num;
		script_removetop(st, -2, 0);
		op_2num(st, op, i1, i2);
	}
	else
	{// invalid argument
		ShowError("script:op_2: invalid data for operator %s\n", script_op2name(op));
		script_reportdata(left);
		script_reportdata(right);
		script_reportsrc(st);
		script_removetop(st, -2, 0);
		script_pushnil(st);
		st->state = END;
	}
}

/// Unary operators
/// NEG i -> i
/// NOT i -> i
/// LNOT i -> i
void op_1(struct script_state* st, int op)
{
	struct script_data* data;
	int i1;

	data = script_getdatatop(st, -1);
	get_val(st, data);

	if( !data_isint(data) )
	{// not a number
		ShowError("script:op_1: argument is not a number (op=%s)\n", script_op2name(op));
		script_reportdata(data);
		script_reportsrc(st);
		script_pushnil(st);
		st->state = END;
		return;
	}

	i1 = data->u.num;
	script_removetop(st, -1, 0);
	switch( op )
	{
	case C_NEG: i1 = -i1; break;
	case C_NOT: i1 = ~i1; break;
	case C_LNOT: i1 = !i1; break;
	default:
		ShowError("script:op_1: unexpected operator %s i1=%d\n", script_op2name(op), i1);
		script_reportsrc(st);
		script_pushnil(st);
		st->state = END;
		return;
	}
	script_pushint(st, i1);
}


/// Checks the type of all arguments passed to a built-in function.
///
/// @param st Script state whose stack arguments should be inspected.
/// @param func Built-in function for which the arguments are intended.
static void script_check_buildin_argtype(struct script_state* st, int func)
{
	char type;
	int idx, invalid = 0;
	script_function* sf = &buildin_func[str_data[func].val];

	for( idx = 2; script_hasdata(st, idx); idx++ )
	{
		struct script_data* data = script_getdata(st, idx);

		type = sf->arg[idx-2];

		if( type == '?' || type == '*' )
		{// optional argument or unknown number of optional parameters ( no types are after this )
			break;
		}
		else if( type == 0 )
		{// more arguments than necessary ( should not happen, as it is checked before )
			ShowWarning("Found more arguments than necessary.\n");
			invalid++;
			break;
		}
		else
		{
			const char* name = NULL;

			if( data_isreference(data) )
			{// get name for variables to determine the type they refer to
				name = reference_getname(data);
			}

			switch( type )
			{
				case 'v':
					if( !data_isstring(data) && !data_isint(data) && !data_isreference(data) )
					{// variant
						ShowWarning("Unexpected type for argument %d. Expected string, number or variable.\n", idx-1);
						script_reportdata(data);
						invalid++;
					}
					break;
				case 's':
					if( !data_isstring(data) && !( data_isreference(data) && is_string_variable(name) ) )
					{// string
						ShowWarning("Unexpected type for argument %d. Expected string.\n", idx-1);
						script_reportdata(data);
						invalid++;
					}
					break;
				case 'i':
					if( !data_isint(data) && !( data_isreference(data) && ( reference_toparam(data) || reference_toconstant(data) || !is_string_variable(name) ) ) )
					{// int ( params and constants are always int )
						ShowWarning("Unexpected type for argument %d. Expected number.\n", idx-1);
						script_reportdata(data);
						invalid++;
					}
					break;
				case 'r':
					if( !data_isreference(data) )
					{// variables
						ShowWarning("Unexpected type for argument %d. Expected variable.\n", idx-1);
						script_reportdata(data);
						invalid++;
					}
					break;
				case 'l':
					if( !data_islabel(data) && !data_isfunclabel(data) )
					{// label
						ShowWarning("Unexpected type for argument %d. Expected label.\n", idx-1);
						script_reportdata(data);
						invalid++;
					}
					break;
			}
		}
	}

	if(invalid)
	{
		ShowDebug("Function: %s\n", get_str(func));
		script_reportsrc(st);
	}
}


/// Executes a buildin command.
/// Stack: C_NAME(<command>) C_ARG <arg0> <arg1> ... <argN>
int run_func(struct script_state *st)
{
	struct script_data* data;
	int i,start_sp,end_sp,func;

	end_sp = st->stack->sp;// position after the last argument
	for( i = end_sp-1; i > 0 ; --i )
		if( st->stack->stack_data[i].type == C_ARG )
			break;
	if( i == 0 )
	{
		ShowError("script:run_func: C_ARG not found. please report this!!!\n");
		st->state = END;
		script_reportsrc(st);
		return 1;
	}
	start_sp = i-1;// C_NAME of the command
	st->start = start_sp;
	st->end = end_sp;

	data = &st->stack->stack_data[st->start];
	if( data->type == C_NAME && str_data[data->u.num].type == C_FUNC ){
		func = data->u.num;
		st->funcname = reference_getname(data);
	}else{
		ShowError("script:run_func: not a buildin command.\n");
		script_reportdata(data);
		script_reportsrc(st);
		st->state = END;
		return 1;
	}

	if( script_config.warn_func_mismatch_argtypes )
	{
		script_check_buildin_argtype(st, func);
	}

	if(str_data[func].func){
		if (str_data[func].func(st)) //Report error
			script_reportsrc(st);
	} else {
		ShowError("script:run_func: '%s' (id=%d type=%s) has no C function. please report this!!!\n", get_str(func), func, script_op2name(str_data[func].type));
		script_reportsrc(st);
		st->state = END;
	}

	// Stack's datum are used when re-running functions [Eoe]
	if( st->state == RERUNLINE )
		return 0;

	pop_stack(st, st->start, st->end);
	if( st->state == RETFUNC )
	{// return from a user-defined function
		struct script_retinfo* ri;
		int olddefsp = st->stack->defsp;
		int nargs;

		pop_stack(st, st->stack->defsp, st->start);// pop distractions from the stack
		if( st->stack->defsp < 1 || st->stack->stack_data[st->stack->defsp-1].type != C_RETINFO )
		{
			ShowWarning("script:run_func: return without callfunc or callsub!\n");
			script_reportsrc(st);
			st->state = END;
			return 1;
		}
		script_free_vars( st->stack->var_function );
		aFree(st->stack->var_function);

		ri = st->stack->stack_data[st->stack->defsp-1].u.ri;
		nargs = ri->nargs;
		st->pos = ri->pos;
		st->script = ri->script;
		st->stack->var_function = ri->var_function;
		st->stack->defsp = ri->defsp;
		memset(ri, 0, sizeof(struct script_retinfo));

		pop_stack(st, olddefsp-nargs-1, olddefsp);// pop arguments and retinfo

		st->state = GOTO;
	}

	return 0;
}

/*==========================================
 * script execution
 *------------------------------------------*/
void run_script(struct script_code *rootscript,int pos,int rid,int oid)
{
	struct script_state *st;

	if( rootscript == NULL || pos < 0 )
		return;

	// TODO In jAthena, this function can take over the pending script in the player. [FlavioJS]
	//      It is unclear how that can be triggered, so it needs the be traced/checked in more detail.
	// NOTE At the time of this change, this function wasn't capable of taking over the script state because st->scriptroot was never set.
	st = script_alloc_state(rootscript, pos, rid, oid);
	run_script_main(st);
}

void script_stop_sleeptimers(int id)
{
	struct script_state* st;
	for(;;)
	{
		st = (struct script_state*)linkdb_erase(&sleep_db,(void*)id);
		if( st == NULL )
			break; // no more sleep timers
		script_free_state(st);
	}
}

/*==========================================
 * �w��m�[�h��sleep_db����폜
 *------------------------------------------*/
struct linkdb_node* script_erase_sleepdb(struct linkdb_node *n)
{
	struct linkdb_node *retnode;

	if( n == NULL)
		return NULL;
	if( n->prev == NULL )
		sleep_db = n->next;
	else
		n->prev->next = n->next;
	if( n->next )
		n->next->prev = n->prev;
	retnode = n->next;
	aFree( n );
	return retnode;		// ���̃m�[�h��Ԃ�
}

/*==========================================
 * Timer function for sleep
 *------------------------------------------*/
int run_script_timer(int tid, int64 tick, int id, intptr_t data)
{
	struct script_state *st     = (struct script_state *)data;
	struct linkdb_node *node    = (struct linkdb_node *)sleep_db;

	// If it was a player before going to sleep and there is still a unit attached to the script
	if (id != 0 && st->rid)
	{
		struct map_session_data *sd = map_id2sd(st->rid);

		// Attached player is offline or another unit type - should not happen
		if (!sd)
		{
			ShowWarning("Script sleep timer called by an offline character or non player unit.\n");
			script_reportsrc(st);
			st->rid = 0;
			st->state = END;
		// Character mismatch. Cancel execution.
		}
		else if (sd->status.char_id != id)
		{
			ShowWarning("Script sleep timer detected a character mismatch CID %d != %d\n", sd->status.char_id, id);
			script_reportsrc(st);
			st->rid = 0;
			st->state = END;
		}
	}

	while( node && st->sleep.timer != INVALID_TIMER ) {
		if( (int)node->key == st->oid && ((struct script_state *)node->data)->sleep.timer == st->sleep.timer ) {
			script_erase_sleepdb(node);
			st->sleep.timer = INVALID_TIMER;
			break;
		}
		node = node->next;
	}
	if(st->state != RERUNLINE)
		st->sleep.tick = 0;
	run_script_main(st);
	return 0;
}

/// Detaches script state from possibly attached character and restores it's previous script if any.
///
/// @param st Script state to detach.
/// @param dequeue_event Whether to schedule any queued events, when there was no previous script.
static void script_detach_state(struct script_state* st, bool dequeue_event)
{
	struct map_session_data* sd;

	if(st->rid && (sd = map_id2sd(st->rid))!=NULL)
	{
		sd->st = st->bk_st;
		sd->npc_id = st->bk_npcid;

		if(st->bk_st)
		{
			//Remove tag for removal.
			st->bk_st = NULL;
			st->bk_npcid = 0;
		}
		else if(dequeue_event)
		{
			npc_event_dequeue(sd);
		}
	}
	else if(st->bk_st)
	{// rid was set to 0, before detaching the script state
		ShowError("script_detach_state: Found previous script state without attached player (rid=%d, oid=%d, state=%d, bk_npcid=%d)\n", st->bk_st->rid, st->bk_st->oid, st->bk_st->state, st->bk_npcid);
		script_reportsrc(st->bk_st);

		script_free_state(st->bk_st);
		st->bk_st = NULL;
	}
}

/// Attaches script state to possibly attached character and backups it's previous script, if any.
///
/// @param st Script state to attach.
static void script_attach_state(struct script_state* st)
{
	struct map_session_data* sd;

	if(st->rid && (sd = map_id2sd(st->rid))!=NULL)
	{
		if(st!=sd->st)
		{
			if(st->bk_st)
			{// there is already a backup
				ShowDebug("script_free_state: Previous script state lost (rid=%d, oid=%d, state=%d, bk_npcid=%d).\n", st->bk_st->rid, st->bk_st->oid, st->bk_st->state, st->bk_npcid);
			}
			st->bk_st = sd->st;
			st->bk_npcid = sd->npc_id;
		}
		sd->st = st;
		sd->npc_id = st->oid;
	}
}

/*==========================================
 * �X�N���v�g�̎��s���C������
 *------------------------------------------*/
void run_script_main(struct script_state *st)
{
	int cmdcount=script_config.check_cmdcount;
	int gotocount=script_config.check_gotocount;
	TBL_PC *sd;
	struct script_stack *stack=st->stack;
	struct npc_data *nd;

	script_attach_state(st);

	nd = map_id2nd(st->oid);
	if( nd && map[nd->bl.m].instance_id > 0 )
		st->instance_id = map[nd->bl.m].instance_id;

	if(st->state == RERUNLINE) {
		run_func(st);
		if(st->state == GOTO)
			st->state = RUN;
	} else if(st->state != END)
		st->state = RUN;

	while(st->state == RUN)
	{
		enum c_op c = get_com(st->script->script_buf,&st->pos);
		switch(c){
		case C_EOL:
			if( stack->defsp > stack->sp )
				ShowError("script:run_script_main: unexpected stack position (defsp=%d sp=%d). please report this!!!\n", stack->defsp, stack->sp);
			else
				pop_stack(st, stack->defsp, stack->sp);// pop unused stack data. (unused return value)
			break;
		case C_INT:
			push_val(stack,C_INT,get_num(st->script->script_buf,&st->pos));
			break;
		case C_POS:
		case C_NAME:
			push_val(stack,c,GETVALUE(st->script->script_buf,st->pos));
			st->pos+=3;
			break;
		case C_ARG:
			push_val(stack,c,0);
			break;
		case C_STR:
			push_str(stack,C_CONSTSTR,(char*)(st->script->script_buf+st->pos));
			while(st->script->script_buf[st->pos++]);
			break;
		case C_FUNC:
			run_func(st);
			if(st->state==GOTO){
				st->state = RUN;
				if( !st->freeloop && gotocount>0 && (--gotocount)<=0 ){
					ShowError("run_script: infinity loop !\n");
					script_reportsrc(st);
					st->state=END;
				}
			}
			break;

		case C_NEG:
		case C_NOT:
		case C_LNOT:
			op_1(st ,c);
			break;

		case C_ADD:
		case C_SUB:
		case C_MUL:
		case C_DIV:
		case C_MOD:
		case C_EQ:
		case C_NE:
		case C_GT:
		case C_GE:
		case C_LT:
		case C_LE:
		case C_AND:
		case C_OR:
		case C_XOR:
		case C_LAND:
		case C_LOR:
		case C_R_SHIFT:
		case C_L_SHIFT:
			op_2(st, c);
			break;

		case C_OP3:
			op_3(st, c);
			break;

		case C_NOP:
			st->state=END;
			break;

		default:
			ShowError("unknown command : %d @ %d\n",c,st->pos);
			st->state=END;
			break;
		}
		if( !st->freeloop && cmdcount>0 && (--cmdcount)<=0 ){
			ShowError("run_script: infinity loop !\n");
			script_reportsrc(st);
			st->state=END;
		}
	}

	if(st->sleep.tick > 0) {
		//Restore previous script
		script_detach_state(st, false);
		//Delay execution
		sd = map_id2sd(st->rid); // Get sd since script might have attached someone while running. [Inkfish]
		st->sleep.charid = sd?sd->status.char_id:0;
		st->sleep.timer  = add_timer(gettick()+st->sleep.tick,
			run_script_timer, st->sleep.charid, (intptr_t)st);
		linkdb_insert(&sleep_db, (void*)st->oid, st);
	}
	else if(st->state != END && st->rid){
		//Resume later (st is already attached to player).
		if(st->bk_st) {
			ShowWarning("Unable to restore stack! Double continuation!\n");
			//Report BOTH scripts to see if that can help somehow.
			ShowDebug("Previous script (lost):\n");
			script_reportsrc(st->bk_st);
			ShowDebug("Current script:\n");
			script_reportsrc(st);

			script_free_state(st->bk_st);
			st->bk_st = NULL;
		}
	} else {
		//Dispose of script.
		if ((sd = map_id2sd(st->rid))!=NULL)
		{	//Restore previous stack and save char.
			if(sd->state.using_fake_npc){
				clif_clearunit_single(sd->npc_id, CLR_OUTSIGHT, sd->fd);
				sd->state.using_fake_npc = 0;
			}
			//Restore previous script if any.
			script_detach_state(st, true);
			if (sd->state.reg_dirty&2)
				intif_saveregistry(sd,2);
			if (sd->state.reg_dirty&1)
				intif_saveregistry(sd,1);
		}
		script_free_state(st);
		st = NULL;
	}
}

int script_config_read(char *cfgName)
{
	int i;
	char line[1024],w1[1024],w2[1024];
	FILE *fp;


	fp=fopen(cfgName,"r");
	if(fp==NULL){
		ShowError("file not found: [%s]\n", cfgName);
		return 1;
	}
	while(fgets(line, sizeof(line), fp))
	{
		if(line[0] == '/' && line[1] == '/')
			continue;
		i=sscanf(line,"%[^:]: %[^\r\n]",w1,w2);
		if(i!=2)
			continue;

		if(strcmpi(w1,"warn_func_mismatch_paramnum")==0) {
			script_config.warn_func_mismatch_paramnum = config_switch(w2);
		}
		else if(strcmpi(w1,"check_cmdcount")==0) {
			script_config.check_cmdcount = config_switch(w2);
		}
		else if(strcmpi(w1,"check_gotocount")==0) {
			script_config.check_gotocount = config_switch(w2);
		}
		else if(strcmpi(w1,"input_min_value")==0) {
			script_config.input_min_value = config_switch(w2);
		}
		else if(strcmpi(w1,"input_max_value")==0) {
			script_config.input_max_value = config_switch(w2);
		}
		else if(strcmpi(w1,"warn_func_mismatch_argtypes")==0) {
			script_config.warn_func_mismatch_argtypes = config_switch(w2);
		}
		else if(strcmpi(w1,"import")==0){
			script_config_read(w2);
		}
	}
	fclose(fp);

	return 0;
}

static int do_final_userfunc_sub (DBKey key,void *data,va_list ap)
{
	struct script_code *code = (struct script_code *)data;
	if(code){
		script_free_vars( &code->script_vars );
		aFree( code->script_buf );
		aFree( code );
	}
	return 0;
}

static int do_final_autobonus_sub (DBKey key,void *data,va_list ap)
{
	struct script_code *script = (struct script_code *)data;

	if( script )
		script_free_code(script);

	return 0;
}

void script_run_autobonus(const char *autobonus, int id, int pos)
{
	struct script_code *script = (struct script_code *)strdb_get(autobonus_db, autobonus);

	if( script )
	{
		current_equip_item_index = pos;
		run_script(script,0,id,0);
	}
}

void script_add_autobonus(const char *autobonus)
{
	if( strdb_get(autobonus_db, autobonus) == NULL )
	{
		struct script_code *script = parse_script(autobonus, "autobonus", 0, 0);

		if( script )
			strdb_put(autobonus_db, autobonus, script);
	}
}


/// resets a temporary character array variable to given value
void script_cleararray_pc(struct map_session_data* sd, const char* varname, void* value)
{
	int key;
	uint8 idx;

	if( not_array_variable(varname[0]) || !not_server_variable(varname[0]) )
	{
		ShowError("script_cleararray_pc: Variable '%s' has invalid scope (char_id=%d).\n", varname, sd->status.char_id);
		return;
	}

	key = add_str(varname);

	if( is_string_variable(varname) )
	{
		for( idx = 0; idx < SCRIPT_MAX_ARRAYSIZE; idx++ )
		{
			pc_setregstr(sd, reference_uid(key, idx), (const char*)value);
		}
	}
	else
	{
		for( idx = 0; idx < SCRIPT_MAX_ARRAYSIZE; idx++ )
		{
			pc_setreg(sd, reference_uid(key, idx), (int)value);
		}
	}
}


/// sets a temporary character array variable element idx to given value
/// @param refcache Pointer to an int variable, which keeps a copy of the reference to varname and must be initialized to 0. Can be NULL if only one element is set.
void script_setarray_pc(struct map_session_data* sd, const char* varname, uint8 idx, void* value, int* refcache)
{
	int key;

	if( not_array_variable(varname[0]) || !not_server_variable(varname[0]) )
	{
		ShowError("script_setarray_pc: Variable '%s' has invalid scope (char_id=%d).\n", varname, sd->status.char_id);
		return;
	}

	if( idx >= SCRIPT_MAX_ARRAYSIZE )
	{
		ShowError("script_setarray_pc: Variable '%s' has invalid index '%d' (char_id=%d).\n", varname, (int)idx, sd->status.char_id);
		return;
	}

	key = ( refcache && refcache[0] ) ? refcache[0] : add_str(varname);

	if( is_string_variable(varname) )
	{
		pc_setregstr(sd, reference_uid(key, idx), (const char*)value);
	}
	else
	{
		pc_setreg(sd, reference_uid(key, idx), (int)value);
	}

	if( refcache )
	{// save to avoid repeated add_str calls
		refcache[0] = key;
	}
}


/*==========================================
 * �I��
 *------------------------------------------*/
int do_final_script()
{
	int i;

#ifdef DEBUG_HASH
	if (battle_config.etc_log)
	{
		FILE *fp = fopen("hash_dump.txt","wt");
		if(fp) {
			int i,count[SCRIPT_HASH_SIZE];
			int count2[SCRIPT_HASH_SIZE]; // number of buckets with a certain number of items
			int n=0;
			int min=INT_MAX,max=0,zero=0;
			double mean=0.0f;
			double median=0.0f;

			ShowNotice("Dumping script str hash information to hash_dump.txt\n");
			memset(count, 0, sizeof(count));
			fprintf(fp,"num : hash : data_name\n");
			fprintf(fp,"---------------------------------------------------------------\n");
			for(i=LABEL_START; i<str_num; i++) {
				unsigned int h = calc_hash(get_str(i));
				fprintf(fp,"%04d : %4u : %s\n",i,h, get_str(i));
				++count[h];
			}
			fprintf(fp,"--------------------\n\n");
			memset(count2, 0, sizeof(count2));
			for(i=0; i<SCRIPT_HASH_SIZE; i++) {
				fprintf(fp,"  hash %3d = %d\n",i,count[i]);
				if(min > count[i])
					min = count[i];		// minimun count of collision
				if(max < count[i])
					max = count[i];		// maximun count of collision
				if(count[i] == 0)
					zero++;
				++count2[count[i]];
			}
			fprintf(fp,"\n--------------------\n  items : buckets\n--------------------\n");
			for( i=min; i <= max; ++i ){
				fprintf(fp,"  %5d : %7d\n",i,count2[i]);
				mean += 1.0f*i*count2[i]/SCRIPT_HASH_SIZE; // Note: this will always result in <nr labels>/<nr buckets>
			}
			for( i=min; i <= max; ++i ){
				n += count2[i];
				if( n*2 >= SCRIPT_HASH_SIZE )
				{
					if( SCRIPT_HASH_SIZE%2 == 0 && SCRIPT_HASH_SIZE/2 == n )
						median = (i+i+1)/2.0f;
					else
						median = i;
					break;
				}
			}
			fprintf(fp,"--------------------\n    min = %d, max = %d, zero = %d\n    mean = %lf, median = %lf\n",min,max,zero,mean,median);
			fclose(fp);
		}
	}
#endif

	mapreg_final();

	scriptlabel_db->destroy(scriptlabel_db,NULL);
	userfunc_db->destroy(userfunc_db,do_final_userfunc_sub);
	autobonus_db->destroy(autobonus_db, do_final_autobonus_sub);
	if(sleep_db) {
		struct linkdb_node *n = (struct linkdb_node *)sleep_db;
		while(n) {
			struct script_state *st = (struct script_state *)n->data;
			script_free_state(st);
			n = n->next;
		}
		linkdb_final(&sleep_db);
	}

	for (i = 0; i < VECTOR_LENGTH(sc_queue); i++) {
		VECTOR_CLEAR(VECTOR_INDEX(sc_queue, i).entries);
	}
	VECTOR_CLEAR(sc_queue);

	if (str_data)
		aFree(str_data);
	if (str_buf)
		aFree(str_buf);


	return 0;
}
/*==========================================
 * ������
 *------------------------------------------*/
int do_init_script()
{
	userfunc_db=strdb_alloc(DB_OPT_DUP_KEY,0);
	scriptlabel_db=strdb_alloc((DBOptions)(DB_OPT_DUP_KEY|DB_OPT_ALLOW_NULL_DATA),50);
	autobonus_db = strdb_alloc(DB_OPT_DUP_KEY,0);

	mapreg_init();
	
	return 0;
}

int script_reload()
{
	userfunc_db->clear(userfunc_db,do_final_userfunc_sub);
	scriptlabel_db->clear(scriptlabel_db, NULL);
	
	// @commands (script based) 
	// Clear bindings 
	memset(atcmd_binding,0,sizeof(atcmd_binding)); 

	if(sleep_db) {
		struct linkdb_node *n = (struct linkdb_node *)sleep_db;
		while(n) {
			struct script_state *st = (struct script_state *)n->data;
			script_free_state(st);
			n = n->next;
		}
		linkdb_final(&sleep_db);
	}

	mapreg_reload();
	return 0;
}

//-----------------------------------------------------------------------------
// buildin functions
//

#define BUILDIN_DEF(x,args) { buildin_ ## x , #x , args }
#define BUILDIN_DEF2(x,x2,args) { buildin_ ## x , x2 , args }
#define BUILDIN_FUNC(x) int buildin_ ## x (struct script_state* st)

/////////////////////////////////////////////////////////////////////
// NPC interaction
//

/// Appends a message to the npc dialog.
/// If a dialog doesn't exist yet, one is created.
///
/// mes "<message>";
BUILDIN_FUNC(mes)
{
	TBL_PC* sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	clif_scriptmes(sd, st->oid, script_getstr(st, 2));
	return 0;
}

/// Displays the button 'next' in the npc dialog.
/// The dialog text is cleared and the script continues when the button is pressed.
///
/// next;
BUILDIN_FUNC(next)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	st->state = STOP;
	clif_scriptnext(sd, st->oid);
	return 0;
}

/// Ends the script and displays the button 'close' on the npc dialog.
/// The dialog is closed when the button is pressed.
///
/// close;
BUILDIN_FUNC(close)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	st->state = END;
	clif_scriptclose(sd, st->oid);
	return 0;
}

/// Displays the button 'close' on the npc dialog.
/// The dialog is closed and the script continues when the button is pressed.
///
/// close2;
BUILDIN_FUNC(close2)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	st->state = STOP;
	clif_scriptclose(sd, st->oid);
	return 0;
}

/// Counts the number of valid and total number of options in 'str'
/// If max_count > 0 the counting stops when that valid option is reached
/// total is incremented for each option (NULL is supported)
static int menu_countoptions(const char* str, int max_count, int* total)
{
	int count = 0;
	int bogus_total;

	if( total == NULL )
		total = &bogus_total;
	++(*total);

	// initial empty options
	while( *str == ':' )
	{
		++str;
		++(*total);
	}
	// count menu options
	while( *str != '\0' )
	{
		++count;
		--max_count;
		if( max_count == 0 )
			break;
		while( *str != ':' && *str != '\0' )
			++str;
		while( *str == ':' )
		{
			++str;
			++(*total);
		}
	}
	return count;
}

/// Displays a menu with options and goes to the target label.
/// The script is stopped if cancel is pressed.
/// Options with no text are not displayed in the client.
///
/// Options can be grouped together, separated by the character ':' in the text:
///   ex: menu "A:B:C",L_target;
/// All these options go to the specified target label.
///
/// The index of the selected option is put in the variable @menu.
/// Indexes start with 1 and are consistent with grouped and empty options.
///   ex: menu "A::B",-,"",L_Impossible,"C",-;
///       // displays "A", "B" and "C", corresponding to indexes 1, 3 and 5
///
/// NOTE: the client closes the npc dialog when cancel is pressed
///
/// menu "<option_text>",<target_label>{,"<option_text>",<target_label>,...};
BUILDIN_FUNC(menu)
{
	int i;
	const char* text;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	// TODO detect multiple scripts waiting for input at the same time, and what to do when that happens
	if( sd->state.menu_or_input == 0 )
	{
		struct StringBuf buf;
		struct script_data* data;

		if( script_lastdata(st) % 2 == 0 )
		{// argument count is not even (1st argument is at index 2)
			ShowError("script:menu: illegal number of arguments (%d).\n", (script_lastdata(st) - 1));
			st->state = END;
			return 1;
		}

		StringBuf_Init(&buf);
		sd->npc_menu = 0;
		for( i = 2; i < script_lastdata(st); i += 2 )
		{
			// menu options
			text = script_getstr(st, i);

			// target label
			data = script_getdata(st, i+1);
			if( !data_islabel(data) )
			{// not a label
				StringBuf_Destroy(&buf);
				ShowError("script:menu: argument #%d (from 1) is not a label or label not found.\n", i);
				script_reportdata(data);
				st->state = END;
				return 1;
			}

			// append option(s)
			if( text[0] == '\0' )
				continue;// empty string, ignore
			if( sd->npc_menu > 0 )
				StringBuf_AppendStr(&buf, ":");
			StringBuf_AppendStr(&buf, text);
			sd->npc_menu += menu_countoptions(text, 0, NULL);
		}
		st->state = RERUNLINE;
		sd->state.menu_or_input = 1;
		clif_scriptmenu(sd, st->oid, StringBuf_Value(&buf));
		StringBuf_Destroy(&buf);

		if( sd->npc_menu >= 0xff )
		{// client supports only up to 254 entries; 0 is not used and 255 is reserved for cancel; excess entries are displayed but cause 'uint8' overflow
			ShowWarning("buildin_menu: Too many options specified (current=%d, max=254).\n", sd->npc_menu);
			script_reportsrc(st);
		}
	}
	else if( sd->npc_menu == 0xff )
	{// Cancel was pressed
		sd->state.menu_or_input = 0;
		st->state = END;
	}
	else
	{// goto target label
		int menu = 0;

		sd->state.menu_or_input = 0;
		if( sd->npc_menu <= 0 )
		{
			ShowDebug("script:menu: unexpected selection (%d)\n", sd->npc_menu);
			st->state = END;
			return 1;
		}

		// get target label
		for( i = 2; i < script_lastdata(st); i += 2 )
		{
			text = script_getstr(st, i);
			sd->npc_menu -= menu_countoptions(text, sd->npc_menu, &menu);
			if( sd->npc_menu <= 0 )
				break;// entry found
		}
		if( sd->npc_menu > 0 )
		{// Invalid selection
			ShowDebug("script:menu: selection is out of range (%d pairs are missing?) - please report this\n", sd->npc_menu);
			st->state = END;
			return 1;
		}
		if( !data_islabel(script_getdata(st, i + 1)) )
		{// TODO remove this temporary crash-prevention code (fallback for multiple scripts requesting user input)
			ShowError("script:menu: unexpected data in label argument\n");
			script_reportdata(script_getdata(st, i + 1));
			st->state = END;
			return 1;
		}
		pc_setreg(sd, add_str("@menu"), menu);
		st->pos = script_getnum(st, i + 1);
		st->state = GOTO;
	}
	return 0;
}

/// Displays a menu with options and returns the selected option.
/// Behaves like 'menu' without the target labels.
///
/// select(<option_text>{,<option_text>,...}) -> <selected_option>
///
/// @see menu
BUILDIN_FUNC(select)
{
	int i;
	const char* text;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if( sd->state.menu_or_input == 0 )
	{
		struct StringBuf buf;

		StringBuf_Init(&buf);
		sd->npc_menu = 0;
		for( i = 2; i <= script_lastdata(st); ++i )
		{
			text = script_getstr(st, i);
			if( sd->npc_menu > 0 )
				StringBuf_AppendStr(&buf, ":");
			StringBuf_AppendStr(&buf, text);
			sd->npc_menu += menu_countoptions(text, 0, NULL);
		}

		st->state = RERUNLINE;
		sd->state.menu_or_input = 1;
		clif_scriptmenu(sd, st->oid, StringBuf_Value(&buf));
		StringBuf_Destroy(&buf);

		if( sd->npc_menu >= 0xff )
		{
			ShowWarning("buildin_select: Too many options specified (current=%d, max=254).\n", sd->npc_menu);
			script_reportsrc(st);
		}
	}
	else if( sd->npc_menu == 0xff )
	{// Cancel was pressed
		sd->state.menu_or_input = 0;
		st->state = END;
	}
	else
	{// return selected option
		int menu = 0;

		sd->state.menu_or_input = 0;
		for( i = 2; i <= script_lastdata(st); ++i )
		{
			text = script_getstr(st, i);
			sd->npc_menu -= menu_countoptions(text, sd->npc_menu, &menu);
			if( sd->npc_menu <= 0 )
				break;// entry found
		}
		pc_setreg(sd, add_str("@menu"), menu);
		script_pushint(st, menu);
		st->state = RUN;
	}
	return 0;
}

/// Displays a menu with options and returns the selected option.
/// Behaves like 'menu' without the target labels, except when cancel is 
/// pressed.
/// When cancel is pressed, the script continues and 255 is returned.
///
/// prompt(<option_text>{,<option_text>,...}) -> <selected_option>
///
/// @see menu
BUILDIN_FUNC(prompt)
{
	int i;
	const char *text;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if( sd->state.menu_or_input == 0 )
	{
		struct StringBuf buf;

		StringBuf_Init(&buf);
		sd->npc_menu = 0;
		for( i = 2; i <= script_lastdata(st); ++i )
		{
			text = script_getstr(st, i);
			if( sd->npc_menu > 0 )
				StringBuf_AppendStr(&buf, ":");
			StringBuf_AppendStr(&buf, text);
			sd->npc_menu += menu_countoptions(text, 0, NULL);
		}

		st->state = RERUNLINE;
		sd->state.menu_or_input = 1;
		clif_scriptmenu(sd, st->oid, StringBuf_Value(&buf));
		StringBuf_Destroy(&buf);

		if( sd->npc_menu >= 0xff )
		{
			ShowWarning("buildin_prompt: Too many options specified (current=%d, max=254).\n", sd->npc_menu);
			script_reportsrc(st);
		}
	}
	else if( sd->npc_menu == 0xff )
	{// Cancel was pressed
		sd->state.menu_or_input = 0;
		pc_setreg(sd, add_str("@menu"), 0xff);
		script_pushint(st, 0xff);
		st->state = RUN;
	}
	else
	{// return selected option
		int menu = 0;

		sd->state.menu_or_input = 0;
		for( i = 2; i <= script_lastdata(st); ++i )
		{
			text = script_getstr(st, i);
			sd->npc_menu -= menu_countoptions(text, sd->npc_menu, &menu);
			if( sd->npc_menu <= 0 )
				break;// entry found
		}
		pc_setreg(sd, add_str("@menu"), menu);
		script_pushint(st, menu);
		st->state = RUN;
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////
// ...
//

/// Jumps to the target script label.
///
/// goto <label>;
BUILDIN_FUNC(goto)
{
	if( !data_islabel(script_getdata(st,2)) )
	{
		ShowError("script:goto: not a label\n");
		script_reportdata(script_getdata(st,2));
		st->state = END;
		return 1;
	}

	st->pos = script_getnum(st,2);
	st->state = GOTO;
	return 0;
}

/*==========================================
 * user-defined function call
 *------------------------------------------*/
BUILDIN_FUNC(callfunc)
{
	int i, j;
	struct script_retinfo* ri;
	struct script_code* scr;
	const char* str = script_getstr(st,2);

	scr = (struct script_code*)strdb_get(userfunc_db, str);
	if( !scr )
	{
		ShowError("script:callfunc: function not found! [%s]\n", str);
		st->state = END;
		return 1;
	}

	for( i = st->start+3, j = 0; i < st->end; i++, j++ )
	{
		struct script_data* data = push_copy(st->stack,i);
		if( data_isreference(data) && !data->ref )
		{
			const char* name = reference_getname(data);
			if( name[0] == '.' && name[1] == '@' )
				data->ref = st->stack->var_function;
			else if( name[0] == '.' )
				data->ref = &st->script->script_vars;
		}
	}

	CREATE(ri, struct script_retinfo, 1);
	ri->script       = st->script;// script code
	ri->var_function = st->stack->var_function;// scope variables
	ri->pos          = st->pos;// script location
	ri->nargs        = j;// argument count
	ri->defsp        = st->stack->defsp;// default stack pointer
	push_retinfo(st->stack, ri);

	st->pos = 0;
	st->script = scr;
	st->stack->defsp = st->stack->sp;
	st->state = GOTO;
	st->stack->var_function = (struct linkdb_node**)aCalloc(1, sizeof(struct linkdb_node*));

	return 0;
}
/*==========================================
 * subroutine call
 *------------------------------------------*/
BUILDIN_FUNC(callsub)
{
	int i,j;
	struct script_retinfo* ri;
	int pos = script_getnum(st,2);

	if( !data_islabel(script_getdata(st,2)) && !data_isfunclabel(script_getdata(st,2)) )
	{
		ShowError("script:callsub: argument is not a label\n");
		script_reportdata(script_getdata(st,2));
		st->state = END;
		return 1;
	}

	for( i = st->start+3, j = 0; i < st->end; i++, j++ )
	{
		struct script_data* data = push_copy(st->stack,i);
		if( data_isreference(data) && !data->ref )
		{
			const char* name = reference_getname(data);
			if( name[0] == '.' && name[1] == '@' )
				data->ref = st->stack->var_function;
		}
	}

	CREATE(ri, struct script_retinfo, 1);
	ri->script       = st->script;// script code
	ri->var_function = st->stack->var_function;// scope variables
	ri->pos          = st->pos;// script location
	ri->nargs        = j;// argument count
	ri->defsp        = st->stack->defsp;// default stack pointer
	push_retinfo(st->stack, ri);

	st->pos = pos;
	st->stack->defsp = st->stack->sp;
	st->state = GOTO;
	st->stack->var_function = (struct linkdb_node**)aCalloc(1, sizeof(struct linkdb_node*));

	return 0;
}

/// Retrieves an argument provided to callfunc/callsub.
/// If the argument doesn't exist
///
/// getarg(<index>{,<default_value>}) -> <value>
BUILDIN_FUNC(getarg)
{
	struct script_retinfo* ri;
	int idx;

	if( st->stack->defsp < 1 || st->stack->stack_data[st->stack->defsp - 1].type != C_RETINFO )
	{
		ShowError("script:getarg: no callfunc or callsub!\n");
		st->state = END;
		return 1;
	}
	ri = st->stack->stack_data[st->stack->defsp - 1].u.ri;

	idx = script_getnum(st,2);

	if( idx >= 0 && idx < ri->nargs )
		push_copy(st->stack, st->stack->defsp - 1 - ri->nargs + idx);
	else if( script_hasdata(st,3) )
		script_pushcopy(st, 3);
	else
	{
		ShowError("script:getarg: index (idx=%d) out of range (nargs=%d) and no default value found\n", idx, ri->nargs);
		st->state = END;
		return 1;
	}

	return 0;
}

/**
 * Retrieves quantity of arguments provided to callfunc/callsub.
 * getargcount() -> amount of arguments received in a function
 **/
BUILDIN_FUNC(getargcount) {
	struct script_retinfo* ri;

	if( st->stack->defsp < 1 || st->stack->stack_data[st->stack->defsp - 1].type != C_RETINFO ) {
		ShowError("script:getargcount: used out of function or callsub label!\n");
		st->state = END;
		return 1;
	}
	ri = st->stack->stack_data[st->stack->defsp - 1].u.ri;

	script_pushint(st, ri->nargs);
	return 0;
}

/// Returns from the current function, optionaly returning a value from the functions.
/// Don't use outside script functions.
///
/// return;
/// return <value>;
BUILDIN_FUNC(return)
{
	if( script_hasdata(st,2) )
	{// return value
		struct script_data* data;
		script_pushcopy(st, 2);
		data = script_getdatatop(st, -1);
		if( data_isreference(data) )
		{
			const char* name = reference_getname(data);
			if( name[0] == '.' && name[1] == '@' )
			{// scope variable
				if( !data->ref || data->ref == st->stack->var_function )
					get_val(st, data);// current scope, convert to value
			}
			else if( name[0] == '.' && !data->ref )
			{// script variable, link to current script
				data->ref = &st->script->script_vars;
			}
		}
	}
	else
	{// no return value
		script_pushnil(st);
	}
	st->state = RETFUNC;
	return 0;
}

/// Returns a random number from 0 to <range>-1.
/// Or returns a random number from <min> to <max>.
/// If <min> is greater than <max>, their numbers are switched.
/// rand(<range>) -> <int>
/// rand(<min>,<max>) -> <int>
BUILDIN_FUNC(rand)
{
	int range;
	int min;
	int max;

	if( script_hasdata(st,3) )
	{// min,max
		min = script_getnum(st,2);
		max = script_getnum(st,3);
		if( max < min )
			swap(min, max);
		range = max - min + 1;
	}
	else
	{// range
		min = 0;
		range = script_getnum(st,2);
	}
	if( range <= 1 )
		script_pushint(st, min);
	else
		script_pushint(st, rand()%range + min);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(warp)
{
	int ret;
	int x,y;
	const char* str;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	str = script_getstr(st,2);
	x = script_getnum(st,3);
	y = script_getnum(st,4);

	if(strcmp(str,"Random")==0)
		ret = pc_randomwarp(sd,CLR_TELEPORT);
	else if(strcmp(str,"SavePoint")==0 || strcmp(str,"Save")==0)
		ret = pc_setpos(sd,sd->status.save_point.map,sd->status.save_point.x,sd->status.save_point.y,CLR_TELEPORT);
	else
		ret = pc_setpos(sd,mapindex_name2id(str),x,y,CLR_OUTSIGHT);

	if( ret ) {
		ShowError("buildin_warp: moving player '%s' to \"%s\",%d,%d failed.\n", sd->status.name, str, x, y);
		script_reportsrc(st);
	}

	return 0;
}
/*==========================================
 * �G���A�w�胏�[�v
 *------------------------------------------*/
static int buildin_areawarp_sub(struct block_list *bl,va_list ap)
{
	int x,y;
	unsigned int map;
	map=va_arg(ap, unsigned int);
	x=va_arg(ap,int);
	y=va_arg(ap,int);
	if(map == 0)
		pc_randomwarp((TBL_PC *)bl,CLR_TELEPORT);
	else
		pc_setpos((TBL_PC *)bl,map,x,y,CLR_OUTSIGHT);
	return 0;
}
BUILDIN_FUNC(areawarp)
{
	int x,y,m;
	unsigned int index;
	const char *str;
	const char *mapname;
	int x0,y0,x1,y1;

	mapname=script_getstr(st,2);
	x0=script_getnum(st,3);
	y0=script_getnum(st,4);
	x1=script_getnum(st,5);
	y1=script_getnum(st,6);
	str=script_getstr(st,7);
	x=script_getnum(st,8);
	y=script_getnum(st,9);

	if( (m=map_mapname2mapid(mapname))< 0)
		return 0;

	if(strcmp(str,"Random")==0)
		index = 0;
	else if(!(index=mapindex_name2id(str)))
		return 0;

	map_foreachinarea(buildin_areawarp_sub, m,x0,y0,x1,y1,BL_PC, index,x,y);
	return 0;
}

/*==========================================
 * areapercentheal <map>,<x1>,<y1>,<x2>,<y2>,<hp>,<sp>
 *------------------------------------------*/
static int buildin_areapercentheal_sub(struct block_list *bl,va_list ap)
{
	int hp, sp;
	hp = va_arg(ap, int);
	sp = va_arg(ap, int);
	pc_percentheal((TBL_PC *)bl,hp,sp);
	return 0;
}
BUILDIN_FUNC(areapercentheal)
{
	int hp,sp,m;
	const char *mapname;
	int x0,y0,x1,y1;

	mapname=script_getstr(st,2);
	x0=script_getnum(st,3);
	y0=script_getnum(st,4);
	x1=script_getnum(st,5);
	y1=script_getnum(st,6);
	hp=script_getnum(st,7);
	sp=script_getnum(st,8);

	if( (m=map_mapname2mapid(mapname))< 0)
		return 0;

	map_foreachinarea(buildin_areapercentheal_sub,m,x0,y0,x1,y1,BL_PC,hp,sp);
	return 0;
}

/*==========================================
 * warpchar [LuzZza]
 * Useful for warp one player from 
 * another player npc-session.
 * Using: warpchar "mapname",x,y,Char_ID;
 *------------------------------------------*/
BUILDIN_FUNC(warpchar)
{
	int x,y,a;
	const char *str;
	TBL_PC *sd;
	
	str=script_getstr(st,2);
	x=script_getnum(st,3);
	y=script_getnum(st,4);
	a=script_getnum(st,5);

	sd = map_charid2sd(a);
	if( sd == NULL )
		return 0;

	if(strcmp(str, "Random") == 0)
		pc_randomwarp(sd, CLR_TELEPORT);
	else
	if(strcmp(str, "SavePoint") == 0)
		pc_setpos(sd, sd->status.save_point.map,sd->status.save_point.x, sd->status.save_point.y, CLR_TELEPORT);
	else	
		pc_setpos(sd, mapindex_name2id(str), x, y, CLR_TELEPORT);
	
	return 0;
} 
/*==========================================
 * Warpparty - [Fredzilla] [Paradox924X]
 * Syntax: warpparty "to_mapname",x,y,Party_ID,{"from_mapname"};
 * If 'from_mapname' is specified, only the party members on that map will be warped
 *------------------------------------------*/
BUILDIN_FUNC(warpparty)
{
	TBL_PC *sd = NULL;
	TBL_PC *pl_sd;
	struct party_data* p;
	int type;
	int mapindex = 0, m = -1;
	int i;

	const char* str = script_getstr(st,2);
	int x = script_getnum(st,3);
	int y = script_getnum(st,4);
	int p_id = script_getnum(st,5);
	const char* str2 = NULL;
	if ( script_hasdata(st,6) )
		str2 = script_getstr(st,6);

	p = party_search(p_id);
	if(!p)
		return 0;
	
	type = ( strcmp(str,"Random")==0 ) ? 0
	     : ( strcmp(str,"SavePointAll")==0 ) ? 1
		 : ( strcmp(str,"SavePoint")==0 ) ? 2
		 : ( strcmp(str,"Leader")==0 ) ? 3
		 : 4;

	switch (type) {
		case 3:
			for(i = 0; i < MAX_PARTY && !p->party.member[i].leader; i++);
			if (i == MAX_PARTY || !p->data[i].sd) //Leader not found / not online
				return 0;
			pl_sd = p->data[i].sd;
			mapindex = pl_sd->mapindex;
			m = map_mapindex2mapid(mapindex);
			x = pl_sd->bl.x;
			y = pl_sd->bl.y;
			break;
		case 4:
			mapindex = mapindex_name2id(str);
			if (!mapindex) // Invalid map
				return 0;

			m = map_mapindex2mapid(mapindex);
			break;
		case 2:
			//"SavePoint" uses save point of the currently attached player
			if (( sd = script_rid2sd(st) ) == NULL )
				return 0;
			break;
	}

	for (i = 0; i < MAX_PARTY; i++)
	{
		if( !(pl_sd = p->data[i].sd) || pl_sd->status.party_id != p_id )
			continue;

		if( str2 && strcmp(str2, map[pl_sd->bl.m].name) != 0 )
			continue;

		if( pc_isdead(pl_sd) )
			continue;

		switch( type )
		{
		case 0: // Random
			if(!map[pl_sd->bl.m].flag.nowarp)
				pc_randomwarp(pl_sd,CLR_TELEPORT);
		break;
		case 1: // SavePointAll
			if(!map[pl_sd->bl.m].flag.noreturn)
				pc_setpos(pl_sd,pl_sd->status.save_point.map,pl_sd->status.save_point.x,pl_sd->status.save_point.y,CLR_TELEPORT);
		break;
		case 2: // SavePoint
			if(!map[pl_sd->bl.m].flag.noreturn)
				pc_setpos(pl_sd,sd->status.save_point.map,sd->status.save_point.x,sd->status.save_point.y,CLR_TELEPORT);
		break;
		case 3: // Leader
		case 4: // m,x,y
			if(!map[pl_sd->bl.m].flag.noreturn && !map[pl_sd->bl.m].flag.nowarp && pc_job_can_entermap((enum e_job)pl_sd->status.class_, m, pl_sd->gmlevel)) 
				pc_setpos(pl_sd,mapindex,x,y,CLR_TELEPORT);
		break;
		}
	}

	return 0;
}
/*==========================================
 * Warpguild - [Fredzilla]
 * Syntax: warpguild "mapname",x,y,Guild_ID;
 *------------------------------------------*/
BUILDIN_FUNC(warpguild)
{
	TBL_PC *sd = NULL;
	TBL_PC *pl_sd;
	struct guild* g;
	struct s_mapiterator* iter;
	int type, mapindex = 0, m = -1;

	const char* str = script_getstr(st,2);
	int x           = script_getnum(st,3);
	int y           = script_getnum(st,4);
	int gid         = script_getnum(st,5);

	g = guild_search(gid);
	if( g == NULL )
		return 0;
	
	type = ( strcmp(str,"Random")==0 ) ? 0
	     : ( strcmp(str,"SavePointAll")==0 ) ? 1
		 : ( strcmp(str,"SavePoint")==0 ) ? 2
		 : 3;

	if( type == 2 && ( sd = script_rid2sd(st) ) == NULL )
	{// "SavePoint" uses save point of the currently attached player
		return 0;
	}

	if(type == 3) {
		mapindex = mapindex_name2id(str);
		if (!mapindex)
			return 0;
		m = map_mapindex2mapid(mapindex);
	}

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if( pl_sd->status.guild_id != gid )
			continue;

		switch( type ) {
			case 0: // Random
				if(!map[pl_sd->bl.m].flag.nowarp)
					pc_randomwarp(pl_sd,CLR_TELEPORT);
				break;
			case 1: // SavePointAll
				if(!map[pl_sd->bl.m].flag.noreturn)
					pc_setpos(pl_sd,pl_sd->status.save_point.map,pl_sd->status.save_point.x,pl_sd->status.save_point.y,CLR_TELEPORT);
				break;
			case 2: // SavePoint
				if(!map[pl_sd->bl.m].flag.noreturn)
					pc_setpos(pl_sd,sd->status.save_point.map,sd->status.save_point.x,sd->status.save_point.y,CLR_TELEPORT);
				break;
			case 3: // m,x,y
				if(!map[pl_sd->bl.m].flag.noreturn && !map[pl_sd->bl.m].flag.nowarp && pc_job_can_entermap((enum e_job)pl_sd->status.class_, m, pl_sd->gmlevel))
					pc_setpos(pl_sd,mapindex,x,y,CLR_TELEPORT);
				break;
		}
	}
	mapit_free(iter);

	return 0;
}
/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(heal)
{
	TBL_PC *sd;
	int hp,sp;
	
	sd = script_rid2sd(st);
	if (!sd) return 0;
	
	hp=script_getnum(st,2);
	sp=script_getnum(st,3);
	status_heal(&sd->bl, hp, sp, 1);
	return 0;
}
/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(itemheal)
{
	TBL_PC *sd;
	int hp,sp;

	hp=script_getnum(st,2);
	sp=script_getnum(st,3);

	if(potion_flag==1) {
		potion_hp = hp;
		potion_sp = sp;
		return 0;
	}
	
	sd = script_rid2sd(st);
	if (!sd) return 0;
	pc_itemheal(sd,sd->itemid,hp,sp);
	return 0;
}
/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(percentheal)
{
	int hp,sp;
	TBL_PC* sd;

	hp=script_getnum(st,2);
	sp=script_getnum(st,3);

	if(potion_flag==1) {
		potion_per_hp = hp;
		potion_per_sp = sp;
		return 0;
	}

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	pc_percentheal(sd,hp,sp);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(jobchange)
{
	int job, upper=-1;

	job=script_getnum(st,2);
	if( script_hasdata(st,3) )
		upper=script_getnum(st,3);

	if (pcdb_checkid(job))
	{
		TBL_PC* sd;
		
		sd = script_rid2sd(st);
		if( sd == NULL )
			return 0;

		pc_jobchange(sd, job, upper);
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(jobname)
{
	int class_=script_getnum(st,2);
	script_pushconststr(st, (char*)job_name(class_));
	return 0;
}

/// Get input from the player.
/// For numeric inputs the value is capped to the range [min,max]. Returns 1 if 
/// the value was higher than 'max', -1 if lower than 'min' and 0 otherwise.
/// For string inputs it returns 1 if the string was longer than 'max', -1 is 
/// shorter than 'min' and 0 otherwise.
///
/// input(<var>{,<min>{,<max>}}) -> <int>
BUILDIN_FUNC(input)
{
	TBL_PC* sd;
	struct script_data* data;
	int uid;
	const char* name;
	int min;
	int max;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	data = script_getdata(st,2);
	if( !data_isreference(data) ){
		ShowError("script:input: not a variable\n");
		script_reportdata(data);
		st->state = END;
		return 1;
	}
	uid = reference_getuid(data);
	name = reference_getname(data);
	min = (script_hasdata(st,3) ? script_getnum(st,3) : script_config.input_min_value);
	max = (script_hasdata(st,4) ? script_getnum(st,4) : script_config.input_max_value);

	if( !sd->state.menu_or_input )
	{	// first invocation, display npc input box
		sd->state.menu_or_input = 1;
		st->state = RERUNLINE;
		if( is_string_variable(name) )
			clif_scriptinputstr(sd,st->oid);
		else	
			clif_scriptinput(sd,st->oid);
	}
	else
	{	// take received text/value and store it in the designated variable
		sd->state.menu_or_input = 0;
		if( is_string_variable(name) )
		{
			int len = (int)strlen(sd->npc_str);
			set_reg(st, sd, uid, name, (void*)sd->npc_str, script_getref(st,2));
			script_pushint(st, (len > max ? 1 : len < min ? -1 : 0));
		}
		else
		{
			int amount = sd->npc_amount;
			set_reg(st, sd, uid, name, (void*)cap_value(amount,min,max), script_getref(st,2));
			script_pushint(st, (amount > max ? 1 : amount < min ? -1 : 0));
		}
		st->state = RUN;
	}
	return 0;
}

/// Sets the value of a variable.
/// The value is converted to the type of the variable.
///
/// set(<variable>,<value>) -> <variable>
BUILDIN_FUNC(set)
{
	TBL_PC* sd = NULL;
	struct script_data* data;
	int num;
	const char* name;
	char prefix;

	data = script_getdata(st,2);
	if( !data_isreference(data) )
	{
		ShowError("script:set: not a variable\n");
		script_reportdata(script_getdata(st,2));
		st->state = END;
		return 1;
	}

	num = reference_getuid(data);
	name = reference_getname(data);
	prefix = *name;

	if( not_server_variable(prefix) )
	{
		sd = script_rid2sd(st);
		if( sd == NULL )
		{
			ShowError("script:set: no player attached for player variable '%s'\n", name);
			return 0;
		}
	}

	if( is_string_variable(name) )
		set_reg(st,sd,num,name,(void*)script_getstr(st,3),script_getref(st,2));
	else
		set_reg(st,sd,num,name,(void*)script_getnum(st,3),script_getref(st,2));

	// return a copy of the variable reference
	script_pushcopy(st,2);

	return 0;
}

/////////////////////////////////////////////////////////////////////
/// Array variables
///

/// Returns the size of the specified array
static int32 getarraysize(struct script_state* st, int32 id, int32 idx, int isstring, struct linkdb_node** ref)
{
	int32 ret = idx;

	if( isstring )
	{
		for( ; idx < SCRIPT_MAX_ARRAYSIZE; ++idx )
		{
			char* str = (char*)get_val2(st, reference_uid(id, idx), ref);
			if( str && *str )
				ret = idx + 1;
			script_removetop(st, -1, 0);
		}
	}
	else
	{
		for( ; idx < SCRIPT_MAX_ARRAYSIZE; ++idx )
		{
			int32 num = (int32)get_val2(st, reference_uid(id, idx), ref);
			if( num )
				ret = idx + 1;
			script_removetop(st, -1, 0);
		}
	}
	return ret;
}

/// Sets values of an array, from the starting index.
/// ex: setarray arr[1],1,2,3;
///
/// setarray <array variable>,<value1>{,<value2>...};
BUILDIN_FUNC(setarray)
{
	struct script_data* data;
	const char* name;
	int32 start;
	int32 end;
	int32 id;
	int32 i;
	TBL_PC* sd = NULL;

	data = script_getdata(st, 2);
	if( !data_isreference(data) )
	{
		ShowError("script:setarray: not a variable\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not a variable
	}

	id = reference_getid(data);
	start = reference_getindex(data);
	name = reference_getname(data);
	if( not_array_variable(*name) )
	{
		ShowError("script:setarray: illegal scope\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not supported
	}

	if( not_server_variable(*name) )
	{
		sd = script_rid2sd(st);
		if( sd == NULL )
			return 0;// no player attached
	}

	end = start + script_lastdata(st) - 2;
	if( end > SCRIPT_MAX_ARRAYSIZE )
		end = SCRIPT_MAX_ARRAYSIZE;

	if( is_string_variable(name) )
	{// string array
		for( i = 3; start < end; ++start, ++i )
			set_reg(st, sd, reference_uid(id, start), name, (void*)script_getstr(st,i), reference_getref(data));
	}
	else
	{// int array
		for( i = 3; start < end; ++start, ++i )
			set_reg(st, sd, reference_uid(id, start), name, (void*)script_getnum(st,i), reference_getref(data));
	}
	return 0;
}

/// Sets count values of an array, from the starting index.
/// ex: cleararray arr[0],0,1;
///
/// cleararray <array variable>,<value>,<count>;
BUILDIN_FUNC(cleararray)
{
	struct script_data* data;
	const char* name;
	int32 start;
	int32 end;
	int32 id;
	void* v;
	TBL_PC* sd = NULL;

	data = script_getdata(st, 2);
	if( !data_isreference(data) )
	{
		ShowError("script:cleararray: not a variable\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not a variable
	}

	id = reference_getid(data);
	start = reference_getindex(data);
	name = reference_getname(data);
	if( not_array_variable(*name) )
	{
		ShowError("script:cleararray: illegal scope\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not supported
	}

	if( not_server_variable(*name) )
	{
		sd = script_rid2sd(st);
		if( sd == NULL )
			return 0;// no player attached
	}

	if( is_string_variable(name) )
		v = (void*)script_getstr(st, 3);
	else
		v = (void*)script_getnum(st, 3);

	end = start + script_getnum(st, 4);
	if( end > SCRIPT_MAX_ARRAYSIZE )
		end = SCRIPT_MAX_ARRAYSIZE;

	for( ; start < end; ++start )
		set_reg(st, sd, reference_uid(id, start), name, v, script_getref(st,2));
	return 0;
}

/// Copies data from one array to another.
/// ex: copyarray arr[0],arr[2],2;
///
/// copyarray <destination array variable>,<source array variable>,<count>;
BUILDIN_FUNC(copyarray)
{
	struct script_data* data1;
	struct script_data* data2;
	const char* name1;
	const char* name2;
	int32 idx1;
	int32 idx2;
	int32 id1;
	int32 id2;
	void* v;
	int32 i;
	int32 count;
	TBL_PC* sd = NULL;

	data1 = script_getdata(st, 2);
	data2 = script_getdata(st, 3);
	if( !data_isreference(data1) || !data_isreference(data2) )
	{
		ShowError("script:copyarray: not a variable\n");
		script_reportdata(data1);
		script_reportdata(data2);
		st->state = END;
		return 1;// not a variable
	}

	id1 = reference_getid(data1);
	id2 = reference_getid(data2);
	idx1 = reference_getindex(data1);
	idx2 = reference_getindex(data2);
	name1 = reference_getname(data1);
	name2 = reference_getname(data2);
	if( not_array_variable(*name1) || not_array_variable(*name2) )
	{
		ShowError("script:copyarray: illegal scope\n");
		script_reportdata(data1);
		script_reportdata(data2);
		st->state = END;
		return 1;// not supported
	}

	if( is_string_variable(name1) != is_string_variable(name2) )
	{
		ShowError("script:copyarray: type mismatch\n");
		script_reportdata(data1);
		script_reportdata(data2);
		st->state = END;
		return 1;// data type mismatch
	}

	if( not_server_variable(*name1) || not_server_variable(*name2) )
	{
		sd = script_rid2sd(st);
		if( sd == NULL )
			return 0;// no player attached
	}

	count = script_getnum(st, 4);
	if( count > SCRIPT_MAX_ARRAYSIZE - idx1 )
		count = SCRIPT_MAX_ARRAYSIZE - idx1;
	if( count <= 0 || (id1 == id2 && idx1 == idx2) )
		return 0;// nothing to copy

	if( id1 == id2 && idx1 > idx2 )
	{// destination might be overlapping the source - copy in reverse order
		for( i = count - 1; i >= 0; --i )
		{
			v = get_val2(st, reference_uid(id2, idx2 + i), reference_getref(data2));
			set_reg(st, sd, reference_uid(id1, idx1 + i), name1, v, reference_getref(data1));
			script_removetop(st, -1, 0);
		}
	}
	else
	{// normal copy
		for( i = 0; i < count; ++i )
		{
			if( idx2 + i < SCRIPT_MAX_ARRAYSIZE )
			{
				v = get_val2(st, reference_uid(id2, idx2 + i), reference_getref(data2));
				set_reg(st, sd, reference_uid(id1, idx1 + i), name1, v, reference_getref(data1));
				script_removetop(st, -1, 0);
			}
			else// out of range - assume ""/0
				set_reg(st, sd, reference_uid(id1, idx1 + i), name1, (is_string_variable(name1)?(void*)"":(void*)0), reference_getref(data1));
		}
	}
	return 0;
}

/// Returns the size of the array.
/// Assumes that everything before the starting index exists.
/// ex: getarraysize(arr[3])
///
/// getarraysize(<array variable>) -> <int>
BUILDIN_FUNC(getarraysize)
{
	struct script_data* data;
	const char* name;

	data = script_getdata(st, 2);
	if( !data_isreference(data) )
	{
		ShowError("script:getarraysize: not a variable\n");
		script_reportdata(data);
		script_pushnil(st);
		st->state = END;
		return 1;// not a variable
	}

	name = reference_getname(data);
	if( not_array_variable(*name) )
	{
		ShowError("script:getarraysize: illegal scope\n");
		script_reportdata(data);
		script_pushnil(st);
		st->state = END;
		return 1;// not supported
	}

	script_pushint(st, getarraysize(st, reference_getid(data), reference_getindex(data), is_string_variable(name), reference_getref(data)));
	return 0;
}

/// Deletes count or all the elements in an array, from the starting index.
/// ex: deletearray arr[4],2;
///
/// deletearray <array variable>;
/// deletearray <array variable>,<count>;
BUILDIN_FUNC(deletearray)
{
	struct script_data* data;
	const char* name;
	int start;
	int end;
	int id;
	TBL_PC *sd = NULL;

	data = script_getdata(st, 2);
	if( !data_isreference(data) )
	{
		ShowError("script:deletearray: not a variable\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not a variable
	}

	id = reference_getid(data);
	start = reference_getindex(data);
	name = reference_getname(data);
	if( not_array_variable(*name) )
	{
		ShowError("script:deletearray: illegal scope\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not supported
	}

	if( not_server_variable(*name) )
	{
		sd = script_rid2sd(st);
		if( sd == NULL )
			return 0;// no player attached
	}

	end = SCRIPT_MAX_ARRAYSIZE;

	if( start >= end )
		return 0;// nothing to free

	if( script_hasdata(st,3) )
	{
		int count = script_getnum(st, 3);
		if( count > end - start )
			count = end - start;
		if( count <= 0 )
			return 0;// nothing to free

		// move rest of the elements backward
		for( ; start + count < end; ++start )
		{
			void* v = get_val2(st, reference_uid(id, start + count), reference_getref(data));
			set_reg(st, sd, reference_uid(id, start), name, v, reference_getref(data));
			script_removetop(st, -1, 0);
		}
	}

	// clear the rest of the array
	if( is_string_variable(name) )
	{
		for( ; start < end; ++start )
			set_reg(st, sd, reference_uid(id, start), name, (void *)"", reference_getref(data));
	}
	else 
	{
		for( ; start < end; ++start )
			set_reg(st, sd, reference_uid(id, start), name, (void*)0, reference_getref(data));
	}
	return 0;
}

/// Returns a reference to the target index of the array variable.
/// Equivalent to var[index].
///
/// getelementofarray(<array variable>,<index>) -> <variable reference>
BUILDIN_FUNC(getelementofarray)
{
	struct script_data* data;
	const char* name;
	int32 id;
	int i;

	data = script_getdata(st, 2);
	if( !data_isreference(data) )
	{
		ShowError("script:getelementofarray: not a variable\n");
		script_reportdata(data);
		script_pushnil(st);
		st->state = END;
		return 1;// not a variable
	}

	id = reference_getid(data);
	name = reference_getname(data);
	if( not_array_variable(*name) )
	{
		ShowError("script:getelementofarray: illegal scope\n");
		script_reportdata(data);
		script_pushnil(st);
		st->state = END;
		return 1;// not supported
	}

	i = script_getnum(st, 3);
	if( i < 0 || i >= SCRIPT_MAX_ARRAYSIZE )
	{
		ShowWarning("script:getelementofarray: index out of range (%d)\n", i);
		script_reportdata(data);
		script_pushnil(st);
		st->state = END;
		return 1;// out of range
	}

	push_val2(st->stack, C_NAME, reference_uid(id, i), reference_getref(data));
	return 0;
}

/////////////////////////////////////////////////////////////////////
/// ...
///

/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(setlook)
{
	int type,val;
	TBL_PC* sd;

	type=script_getnum(st,2);
	val=script_getnum(st,3);

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	pc_changelook(sd,type,val);

	return 0;
}

BUILDIN_FUNC(changelook)
{ // As setlook but only client side
	int type,val;
	TBL_PC* sd;

	type=script_getnum(st,2);
	val=script_getnum(st,3);

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	clif_changelook(&sd->bl,type,val);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(cutin)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	clif_cutin(sd,script_getstr(st,2),script_getnum(st,3));
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(viewpoint)
{
	int type,x,y,id,color;
	TBL_PC* sd;

	type=script_getnum(st,2);
	x=script_getnum(st,3);
	y=script_getnum(st,4);
	id=script_getnum(st,5);
	color=script_getnum(st,6);
	
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	clif_viewpoint(sd,st->oid,type,x,y,id,color);

	return 0;
}

/// Returns number of items in inventory/cart/storage
/// countitem <nameID>{,<accountID>});
/// countitem2 <nameID>,<Identified>,<Refine>,<Attribute>,<Card0>,<Card1>,<Card2>,<Card3>{,<accountID>}) [Lupus]
/// cartcountitem <nameID>{,<accountID>});
/// cartcountitem2 <nameID>,<Identified>,<Refine>,<Attribute>,<Card0>,<Card1>,<Card2>,<Card3>{,<accountID>})
/// storagecountitem <nameID>{,<accountID>});
/// storagecountitem2 <nameID>,<Identified>,<Refine>,<Attribute>,<Card0>,<Card1>,<Card2>,<Card3>{,<accountID>})
BUILDIN_FUNC(countitem)
{
	int i = 0, aid = 3;
	struct item_data* id = NULL;
	struct script_data* data;
	char *command = (char *)script_getfuncname(st);
	uint8 loc = 0;
	uint16 size, count = 0;
	struct item *items;
	TBL_PC *sd = NULL;
	struct s_storage *gstor = NULL;

	if( command[strlen(command)-1] == '2' ) {
		i = 1;
		aid = 10;
	}
	else if (command[strlen(command)-1] == '3') {
		i = 1;
		aid = 13;
	}

	if( script_hasdata(st,aid) ) {
		if( !(sd = map_id2sd( (aid = script_getnum(st, aid)) )) ) {
			ShowError("buildin_%s: player not found (AID=%d).\n", command, aid);
			st->state = END;
			return 1;
		}
	}
	else {
	
		sd = script_rid2sd(st);
		if (!sd) {
			script_pushint(st,0);
			return 0;
		}
	}

	if( !strncmp(command, "cart", 4) ) {
		loc = TABLE_CART;
		size = MAX_CART;
		items = sd->cart.u.items_cart;
	}
	else if( !strncmp(command, "storage", 7) ) {
		loc = TABLE_STORAGE;
		size = MAX_STORAGE;
		items = sd->storage.u.items_storage;
	}
	else if( !strncmp(command, "guildstorage", 12) ) {
		gstor = guild2storage2(sd->status.guild_id);

		if (gstor && !sd->state.storage_flag) {
			loc = TABLE_GUILD_STORAGE;
			size = MAX_GUILD_STORAGE;
			items = gstor->u.items_guild;
		} else {
			script_pushint(st, -1);
			return 0;
		}
	}
	else {
		size = MAX_INVENTORY;
		items = sd->inventory.u.items_inventory;
	}

	if( loc == TABLE_CART && !pc_iscarton(sd) ) {
		ShowError("buildin_%s: Player doesn't have cart (CID:%d).\n", command, sd->status.char_id);
		script_pushint(st,-1);
		return 0;
	}
	
	data = script_getdata(st, 2);
	get_val(st, data); // Convert into value in case of a variable

	if( data_isstring(data) ) // item name
		id = itemdb_searchname(conv_str(st, data));
	else // item id
		id = itemdb_exists(conv_num(st, data));

	if( id == NULL ) {
		ShowError("buildin_%s: Invalid item '%s'.\n", command, script_getstr(st,2));  // returns string, regardless of what it was
		script_pushint(st,0);
		return 1;
	}

	if (loc == TABLE_GUILD_STORAGE)
		gstor->lock = true;

	if( !i ) { // For count/cart/storagecountitem function
		unsigned short nameid = id->nameid;
		for( i = 0; i < size; i++ )
			if( &items[i] && items[i].nameid == nameid )
				count += items[i].amount;
	}
	else { // For count/cart/storagecountitem2 function
		struct item it;
		//bool check_randomopt = false;
		memset(&it, 0, sizeof(it));

		it.nameid = id->nameid;
		it.identify = script_getnum(st,3);
		it.refine  = script_getnum(st,4);
		it.attribute = script_getnum(st,5);
		it.card[0] = script_getnum(st,6);
		it.card[1] = script_getnum(st,7);
		it.card[2] = script_getnum(st,8);
		it.card[3] = script_getnum(st,9);

		/*if (command[strlen(command)-1] == '3') {
			int res = script_getitem_randomoption(st, &it, command, 10);
			if (res != 0)
				return 1;
			check_randomopt = true;
		}*/

		for( i = 0; i < size; i++ ) {
			struct item *itm = &items[i];
			if (!itm || !itm->nameid || itm->amount < 1)
				continue;
			if (itm->nameid != it.nameid || itm->identify != it.identify || itm->refine != it.refine || itm->attribute != it.attribute)
				continue;
			if (memcmp(it.card, itm->card, sizeof(it.card)))
				continue;
			/*if (check_randomopt) {
				uint8 j;
				for (j = 0; j < MAX_ITEM_RDM_OPT; j++) {
					if (itm->option[j].id != it.option[j].id || itm->option[j].value != it.option[j].value || itm->option[j].param != it.option[j].param)
						break;
				}
				if (j != MAX_ITEM_RDM_OPT)
					continue;
			}*/
			count += items[i].amount;
		}
	}

	if (loc == TABLE_GUILD_STORAGE) {
		storage_guild_storageclose(sd);
		gstor->lock = false;
	}

	script_pushint(st, count);
	return 0;
}

/*==========================================
 * �d�ʃ`�F�b�N
 *------------------------------------------*/
BUILDIN_FUNC(checkweight) {
	unsigned short nameid, amount;
	int slots;
	unsigned int weight;
	struct item_data* id = NULL;
	struct map_session_data* sd;
	struct script_data* data;

	if( ( sd = script_rid2sd(st) ) == NULL )
		return 0;

	data = script_getdata(st,2);
	get_val(st, data);  // convert into value in case of a variable

	if( data_isstring(data) )
		// item name
		id = itemdb_searchname(conv_str(st, data));
	else
		// item id
		id = itemdb_exists(conv_num(st, data));

	if( id == NULL ) {
		ShowError("buildin_checkweight: Invalid item '%s'.\n", script_getstr(st,2));  // returns string, regardless of what it was
		script_pushint(st,0);
		return 1;
	}

	nameid = id->nameid;
	amount = script_getnum(st,3);

	if( amount < 1 ) {
		ShowError("buildin_checkweight: Invalid amount '%d'.\n", amount);
		script_pushint(st,0);
		return 1;
	}

	weight = itemdb_weight(nameid)*amount;

	if( weight + sd->weight > sd->max_weight )
	{// too heavy
		script_pushint(st,0);
		return 0;
	}

	switch( pc_checkadditem(sd, nameid, amount) ) {
		case ADDITEM_EXIST:
			// item is already in inventory, but there is still space for the requested amount
			break;
		case ADDITEM_NEW:
			slots = pc_inventoryblank(sd);

			if( itemdb_isstackable(nameid) )
			{// stackable
				if( slots < 1 ) {
					script_pushint(st,0);
					return 0;
				}
			}
			else
			{// non-stackable
				if( slots < amount ) {
					script_pushint(st,0);
					return 0;
				}
			}
			break;
		case ADDITEM_OVERAMOUNT:
			script_pushint(st,0);
			return 0;
	}

	script_pushint(st,1);
	return 0;
}

BUILDIN_FUNC(checkweight2)
{
	//variable sub checkweight
	unsigned short nameid, amount;
	int i = 0, slots = 0, weight = 0;
	short fail = 0;
	unsigned short amount2 = 0;

	//variable for array parsing
	struct script_data* data_it;
	struct script_data* data_nb;
	const char* name_it;
	const char* name_nb;
	int32 id_it, id_nb;
	int32 idx_it, idx_nb;
	int nb_it, nb_nb; //array size

	TBL_PC *sd;

	if ((sd = script_rid2sd(st)) == NULL)
		return 0;

	data_it = script_getdata(st, 2);
	data_nb = script_getdata(st, 3);

	if (!data_isreference(data_it) || !data_isreference(data_nb)) {
		ShowError("buildin_checkweight2: parameter not a variable\n");
		script_pushint(st, 0);
		return 1;// not a variable
	}

	id_it = reference_getid(data_it);
	id_nb = reference_getid(data_nb);
	idx_it = reference_getindex(data_it);
	idx_nb = reference_getindex(data_nb);
	name_it = reference_getname(data_it);
	name_nb = reference_getname(data_nb);

	if (is_string_variable(name_it) || is_string_variable(name_nb)) {
		ShowError("buildin_checkweight2: illegal type, need int\n");
		script_pushint(st, 0);
		return 1;// not supported
	}

	nb_it = getarraysize(st, id_it, idx_it, 0, reference_getref(data_it));
	nb_nb = getarraysize(st, id_nb, idx_nb, 0, reference_getref(data_nb));
	if (nb_it != nb_nb) {
		ShowError("buildin_checkweight2: Size mistmatch: nb_it=%d, nb_nb=%d\n", nb_it, nb_nb);
		fail = 1;
	}

	slots = pc_inventoryblank(sd);
	for (i = 0; i<nb_it; i++) {
		nameid = (unsigned short)__64BPRTSIZE(get_val2(st, reference_uid(id_it, idx_it + i), reference_getref(data_it)));
		script_removetop(st, -1, 0);
		amount = (unsigned short)__64BPRTSIZE(get_val2(st, reference_uid(id_nb, idx_nb + i), reference_getref(data_nb)));
		script_removetop(st, -1, 0);

		if (fail)
			continue; //cpntonie to depop rest

		if (itemdb_exists(nameid) == NULL) {
			ShowError("buildin_checkweight2: Invalid item '%d'.\n", nameid);
			fail = 1;
			continue;
		}
		if (amount < 0) {
			ShowError("buildin_checkweight2: Invalid amount '%d'.\n", amount);
			fail = 1;
			continue;
		}

		weight += itemdb_weight(nameid)*amount;
		if (weight + sd->weight > sd->max_weight) {
			fail = 1;
			continue;
		}
		switch (pc_checkadditem(sd, nameid, amount)) {
		case ADDITEM_EXIST:
			// item is already in inventory, but there is still space for the requested amount
			break;
		case ADDITEM_NEW:
			if (itemdb_isstackable(nameid)) {// stackable
				amount2++;
				if (slots < amount2)
					fail = 1;
			}
			else {// non-stackable
				amount2 += amount;
				if (slots < amount2) {
					fail = 1;
				}
			}
			break;
		case ADDITEM_OVERAMOUNT:
			fail = 1;
		} //end switch
	} //end loop DO NOT break it prematurly we need to depop all stack

	fail ? script_pushint(st, 0) : script_pushint(st, 1);
	return 0;
}

/*==========================================
 * getitem <item id>,<amount>{,<account ID>};
 * getitem "<item name>",<amount>{,<account ID>};
 * 
 * getitembound <item id>,<amount>,<type>{,<account ID>}; 
 * getitembound "<item id>",<amount>,<type>{,<account ID>}; 
 * Type: 
 *	1 - Account Bound 
 *	2 - Guild Bound 
 *	3 - Party Bound 
 *	4 - Character Bound
 *------------------------------------------*/
BUILDIN_FUNC(getitem) {
	unsigned short nameid, amount;
	int get_count,i,flag = 0;
	struct item it;
	TBL_PC *sd;
	struct script_data *data;

	data=script_getdata(st,2);
	get_val(st,data);
	if( data_isstring(data) )
	{// "<item name>"
		const char *name=conv_str(st,data);
		struct item_data *item_data = itemdb_searchname(name);
		if( item_data == NULL ) {
			ShowError("buildin_getitem: Nonexistant item %s requested.\n", name);
			return 1; //No item created.
		}
		nameid=item_data->nameid;
	} else if( data_isint(data) )
	{// <item id>
		nameid=conv_num(st,data);
		//Violet Box, Blue Box, etc - random item pick
		if( nameid < 0 ) {
			nameid=itemdb_searchrandomid(-nameid);
			flag = 1;
		}
		if( nameid <= 0 || !itemdb_exists(nameid) ) {
			ShowError("buildin_getitem: Nonexistant item %d requested.\n", nameid);
			return 1; //No item created.
		}
	} else {
		ShowError("buildin_getitem: invalid data type for argument #1 (%d).", data->type);
		return 1;
	}

	// <amount>
	if( (amount=script_getnum(st,3)) <= 0)
		return 0; //return if amount <=0, skip the useles iteration

	memset(&it,0,sizeof(it));
	it.nameid=nameid;
	if(!flag)
		it.identify=1;
	else
		it.identify=itemdb_isidentified(nameid);

	if( !strcmp(script_getfuncname(st),"getitembound") ) { 
		char bound = script_getnum(st,4); 
		if( bound < 1 || bound > 4) { //Not a correct bound type 
			ShowError("script_getitembound: Not a correct bound type! Type=%d\n",bound); 
			return 1; 
		} 
		it.bound = bound; 
		if( script_hasdata(st,5) ) 
			sd=map_id2sd(script_getnum(st,5)); 
		else 
			sd=script_rid2sd(st); // Attached player 
	} else if( script_hasdata(st,4) ) 
		sd=map_id2sd(script_getnum(st,4)); // <Account ID>
	else
		sd=script_rid2sd(st); // Attached player

	if( sd == NULL ) // no target
		return 0;

	//Check if it's stackable.
	if (!itemdb_isstackable(nameid))
		get_count = 1;
	else
		get_count = amount;

	for (i = 0; i < amount; i += get_count) {
		// if not pet egg
		if (!pet_create_egg(sd, nameid)) {
			if ((flag = pc_additem(sd, &it, get_count))) {
				clif_additem(sd, 0, 0, flag);
				if( pc_candrop(sd,&it) )
					map_addflooritem(&it,get_count,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0);
			}
		}
	}

	//Logs items, got from (N)PC scripts [Lupus]
	log_pick(&sd->bl, LOG_TYPE_SCRIPT, nameid, amount, NULL);

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(getitem2) {
	unsigned short nameid, amount;
	int get_count,i,flag = 0;
	int iden,ref,attr,c1,c2,c3,c4;
	char bound=0;
	struct item_data *item_data;
	struct item item_tmp;
	TBL_PC *sd;
	struct script_data *data;

	if( !strcmp(script_getfuncname(st),"getitembound2") ) { 
		bound = script_getnum(st,11); 
		if( bound < 1 || bound > 3) { //Not a correct bound type 
			ShowError("script_getitembound2: Not a correct bound type! Type=%d\n",bound); 
			return 1; 
		} 
		if( script_hasdata(st,12) ) 
			sd=map_id2sd(script_getnum(st,12)); 
		else 
			sd=script_rid2sd(st); // Attached player 
	} else if( script_hasdata(st,11) )
		sd=map_id2sd(script_getnum(st,11)); // <Account ID>
	else
		sd=script_rid2sd(st); // Attached player

	if( sd == NULL ) // no target
		return 0;

	data=script_getdata(st,2);
	get_val(st,data);
	if( data_isstring(data) ) {
		const char *name=conv_str(st,data);
		struct item_data *item_data = itemdb_searchname(name);
		if( item_data )
			nameid=item_data->nameid;
		else
			nameid=UNKNOWN_ITEM_ID;
	} else
		nameid=conv_num(st,data);

	amount=script_getnum(st,3);
	iden=script_getnum(st,4);
	ref=script_getnum(st,5);
	attr=script_getnum(st,6);
	c1=(short)script_getnum(st,7);
	c2=(short)script_getnum(st,8);
	c3=(short)script_getnum(st,9);
	c4=(short)script_getnum(st,10);

	if(nameid<0) { // �����_��
		nameid=itemdb_searchrandomid(-nameid);
		flag = 1;
	}

	if(nameid > 0) {
		memset(&item_tmp,0,sizeof(item_tmp));
		item_data=itemdb_exists(nameid);
		if (item_data == NULL)
			return -1;
		if(item_data->type==IT_WEAPON || item_data->type==IT_ARMOR)
			if(ref > MAX_REFINE) ref = MAX_REFINE;
		else if(item_data->type==IT_PETEGG) {
			iden = 1;
			ref = 0;
		} else {
			iden = 1;
			ref = attr = 0;
		}

		item_tmp.nameid=nameid;
		if(!flag)
			item_tmp.identify=iden;
		else if(item_data->type==IT_WEAPON || item_data->type==IT_ARMOR)
			item_tmp.identify=0;
		item_tmp.refine=ref;
		item_tmp.attribute=attr;
		item_tmp.card[0]=(short)c1;
		item_tmp.card[1]=(short)c2;
		item_tmp.card[2]=(short)c3;
		item_tmp.card[3]=(short)c4;
		item_tmp.bound=bound;

		//Check if it's stackable.
		if (!itemdb_isstackable(nameid))
			get_count = 1;
		else
			get_count = amount;

		for (i = 0; i < amount; i += get_count) {
			// if not pet egg
			if (!pet_create_egg(sd, nameid)) {
				if ((flag = pc_additem(sd, &item_tmp, get_count))) {
					clif_additem(sd, 0, 0, flag);
					if( pc_candrop(sd,&item_tmp) )
						map_addflooritem(&item_tmp,get_count,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0);
				}
			}
		}

		//Logs items, got from (N)PC scripts [Lupus]
		log_pick(&sd->bl, LOG_TYPE_SCRIPT, nameid, amount, &item_tmp);
	}

	return 0;
}

/*==========================================
 * rentitem <item id>,<seconds>
 * rentitem "<item name>",<seconds>
 *------------------------------------------*/
BUILDIN_FUNC(rentitem)
{
	struct map_session_data *sd;
	struct script_data *data;
	struct item it;
	int seconds;
	unsigned short nameid = 0;
	int flag;

	data = script_getdata(st,2);
	get_val(st,data);

	if( (sd = script_rid2sd(st)) == NULL )
		return 0;

	if( data_isstring(data) )
	{
		const char *name = conv_str(st,data);
		struct item_data *itd = itemdb_searchname(name);
		if( itd == NULL )
		{
			ShowError("buildin_rentitem: Nonexistant item %s requested.\n", name);
			return 1;
		}
		nameid = itd->nameid;
	}
	else if( data_isint(data) )
	{
		nameid = conv_num(st,data);
		if( nameid == 0 || !itemdb_exists(nameid) )
		{
			ShowError("buildin_rentitem: Nonexistant item %hu requested.\n", nameid);
			return 1;
		}
	}
	else
	{
		ShowError("buildin_rentitem: invalid data type for argument #1 (%d).\n", data->type);
		return 1;
	}

	seconds = script_getnum(st,3);
	memset(&it, 0, sizeof(it));
	it.nameid = nameid;
	it.identify = 1;
	it.expire_time = (unsigned int)(time(NULL) + seconds);
	it.bound = 0;

	if( (flag = pc_additem(sd, &it, 1)) )
	{
		clif_additem(sd, 0, 0, flag);
		return 1;
	}

	clif_rental_time(sd->fd, nameid, seconds);
	pc_inventory_rental_add(sd, seconds);

	log_pick(&sd->bl, LOG_TYPE_SCRIPT, nameid, 1, NULL);
	
	return 0;
}

/*==========================================
 * gets an item with someone's name inscribed [Skotlex]
 * getinscribeditem item_num, character_name
 * Returned Qty is always 1, only works on equip-able
 * equipment
 *------------------------------------------*/
BUILDIN_FUNC(getnameditem)
{
	unsigned short nameid;
	struct item item_tmp;
	TBL_PC *sd, *tsd;
	struct script_data *data;

	sd = script_rid2sd(st);
	if (sd == NULL)
	{	//Player not attached!
		script_pushint(st,0);
		return 0; 
	}
	
	data=script_getdata(st,2);
	get_val(st,data);
	if( data_isstring(data) ){
		const char *name=conv_str(st,data);
		struct item_data *item_data = itemdb_searchname(name);
		if( item_data == NULL)
		{	//Failed
			script_pushint(st,0);
			return 0;
		}
		nameid = item_data->nameid;
	}else
		nameid = conv_num(st,data);

	if(!itemdb_exists(nameid)/* || itemdb_isstackable(nameid)*/)
	{	//Even though named stackable items "could" be risky, they are required for certain quests.
		script_pushint(st,0);
		return 0;
	}

	data=script_getdata(st,3);
	get_val(st,data);
	if( data_isstring(data) )	//Char Name
		tsd=map_nick2sd(conv_str(st,data));
	else	//Char Id was given
		tsd=map_charid2sd(conv_num(st,data));
	
	if( tsd == NULL )
	{	//Failed
		script_pushint(st,0);
		return 0;
	}

	memset(&item_tmp,0,sizeof(item_tmp));
	item_tmp.nameid=nameid;
	item_tmp.amount=1;
	item_tmp.identify=1;
	item_tmp.card[0]=CARD0_CREATE; //we don't use 255! because for example SIGNED WEAPON shouldn't get TOP10 BS Fame bonus [Lupus]
	item_tmp.card[2]=tsd->status.char_id;
	item_tmp.card[3]=tsd->status.char_id >> 16;
	if(pc_additem(sd,&item_tmp,1)) {
		script_pushint(st,0);
		return 0;	//Failed to add item, we will not drop if they don't fit
	}

	//Logs items, got from (N)PC scripts [Lupus]
	log_pick(&sd->bl, LOG_TYPE_SCRIPT, item_tmp.nameid, item_tmp.amount, &item_tmp);

	script_pushint(st,1);
	return 0;
}

/*==========================================
 * gets a random item ID from an item group [Skotlex]
 * groupranditem group_num
 *------------------------------------------*/
BUILDIN_FUNC(grouprandomitem)
{
	int group;

	group = script_getnum(st,2);
	script_pushint(st,itemdb_searchrandomid(group));
	return 0;
}

/* getitempackage <package_id>;
 * Gives item(s) to the attached player based on item group contents
 */
BUILDIN_FUNC(getitempackage) {
	struct map_session_data *sd = NULL;
	int packageid;

	if( !( sd = script_rid2sd(st) ) ) {
		ShowWarning("buildin_getitempackage: no player attached!!\n");
		script_pushint(st, 1);
	}

	packageid = script_getnum(st,2);
	itemdb_package_item(sd,packageid);
	script_pushint(st,0);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(makeitem)
{
	unsigned short nameid, amount;
	int flag = 0;
	int x,y,m;
	const char *mapname;
	struct item item_tmp;
	struct script_data *data;

	data=script_getdata(st,2);
	get_val(st,data);
	if( data_isstring(data) ) {
		const char *name=conv_str(st,data);
		struct item_data *item_data = itemdb_searchname(name);
		if( item_data )
			nameid=item_data->nameid;
		else
			nameid=UNKNOWN_ITEM_ID;
	} else
		nameid=conv_num(st,data);

	amount=script_getnum(st,3);
	mapname	=script_getstr(st,4);
	x	=script_getnum(st,5);
	y	=script_getnum(st,6);

	if(strcmp(mapname,"this")==0) {
		TBL_PC *sd;
		sd = script_rid2sd(st);
		if (!sd) return 0; //Failed...
		m=sd->bl.m;
	} else
		m=map_mapname2mapid(mapname);

	if(nameid<0) { // �����_��
		nameid=itemdb_searchrandomid(-nameid);
		flag = 1;
	}

	if(nameid > 0) {
		memset(&item_tmp,0,sizeof(item_tmp));
		item_tmp.nameid=nameid;
		if(!flag)
			item_tmp.identify=1;
		else
			item_tmp.identify=itemdb_isidentified(nameid);

		map_addflooritem(&item_tmp,amount,m,x,y,0,0,0,0);
	}

	return 0;
}

/**
* makeitem2 <item id>,<amount>,"<map name>",<X>,<Y>,<identify>,<refine>,<attribute>,<card1>,<card2>,<card3>,<card4>;
* makeitem2 "<item name>",<amount>,"<map name>",<X>,<Y>,<identify>,<refine>,<attribute>,<card1>,<card2>,<card3>,<card4>;
*/
BUILDIN_FUNC(makeitem2) {
	uint16 nameid, amount, x, y;
	const char *mapname;
	int m;
	struct item item_tmp;
	struct item_data *id;
	const char *funcname = script_getfuncname(st);

	if (script_isstring(st, 2)){
		const char *name = script_getstr(st, 2);
		struct item_data *item_data = itemdb_searchname(name);

		if (item_data)
			nameid = item_data->nameid;
		else
			nameid = UNKNOWN_ITEM_ID;
	}
	else
		nameid = script_getnum(st, 2);

	amount = script_getnum(st, 3);
	mapname = script_getstr(st, 4);
	x = script_getnum(st, 5);
	y = script_getnum(st, 6);

	if (strcmp(mapname, "this") == 0) {
		TBL_PC *sd;
		sd = script_rid2sd(st);
		if (!sd) return 0; //Failed...
		m = sd->bl.m;
	}
	else
		m = map_mapname2mapid(mapname);

	if ((id = itemdb_search(nameid))) {
		char iden, ref, attr;
		memset(&item_tmp, 0, sizeof(item_tmp));
		item_tmp.nameid = nameid;

		iden = (char)script_getnum(st, 7);
		ref = (char)script_getnum(st, 8);
		attr = (char)script_getnum(st, 9);

		if (id->type == IT_WEAPON || id->type == IT_ARMOR) {
			if (ref > MAX_REFINE) ref = MAX_REFINE;
		}
		else if (id->type == IT_PETEGG) {
			iden = 1;
			ref = 0;
		}
		else {
			iden = 1;
			ref = attr = 0;
		}

		item_tmp.identify = iden;
		item_tmp.refine = ref;
		item_tmp.attribute = attr;
		item_tmp.card[0] = script_getnum(st, 10);
		item_tmp.card[1] = script_getnum(st, 11);
		item_tmp.card[2] = script_getnum(st, 12);
		item_tmp.card[3] = script_getnum(st, 13);

		map_addflooritem(&item_tmp, amount, m, x, y, 0, 0, 0, 4);
	}
	else
		return 1;
	return 0;
}


/// Counts / deletes the current item given by idx.
/// Used by buildin_delitem_search
/// Relies on all input data being already fully valid.
static void buildin_delitem_delete(struct map_session_data* sd, int idx, int* amount, uint8 loc, bool delete_items)
{
	int delamount;
	struct item *itm = NULL;
	struct s_storage *gstor = NULL;

	switch(loc) {
		case TABLE_CART:
			itm = &sd->cart.u.items_cart[idx];
			break;
		case TABLE_STORAGE:
			itm = &sd->storage.u.items_storage[idx];
			break;
		case TABLE_GUILD_STORAGE:
 		{
 			gstor = guild2storage2(sd->status.guild_id);
 
 			itm = &gstor->u.items_guild[idx];
 		}
 			break;
		default: // TABLE_INVENTORY
			itm = &sd->inventory.u.items_inventory[idx];
			break;
	}

	delamount = ( amount[0] < itm->amount ) ? amount[0] : itm->amount;

	if( delete_items )
	{
		if( itemdb_type(itm->nameid) == IT_PETEGG && itm->card[0] == CARD0_PET )
		{// delete associated pet
			intif_delete_petdata(MakeDWord(itm->card[1], itm->card[2]));
		}
		switch(loc) {
			case TABLE_CART:
				pc_cart_delitem(sd,idx,delamount,0);
				break;
			case TABLE_STORAGE:
				storage_delitem(sd,idx,delamount);
				break;
			case TABLE_GUILD_STORAGE:
				gstor->lock = true;
				storage_guild_delitem(sd, gstor, idx, delamount);
				storage_guild_storageclose(sd);
				gstor->lock = false;
				break;
			default: // TABLE_INVENTORY
				pc_delitem(sd, idx, delamount, 0, 0);
				break;
		}

		//Logs items, got from (N)PC scripts [Lupus]
		log_pick(&sd->bl, LOG_TYPE_SCRIPT, itm->nameid, -delamount, itm);
		//Logs
	}

	amount[0]-= delamount;
}


/// Searches for item(s) and checks, if there is enough of them.
/// Used by delitem and delitem2
/// Relies on all input data being already fully valid.
/// @param exact_match will also match item attributes and cards, not just name id
/// @return true when all items could be deleted, false when there were not enough items to delete
static bool buildin_delitem_search(struct map_session_data* sd, struct item* it, bool exact_match, uint8 loc)
{
	bool delete_items = false;
	int i, amount, important, size;
	struct item *items;

	// prefer always non-equipped items
	it->equip = 0;

	// when searching for nameid only, prefer additionally
	if( !exact_match )
	{
		// non-refined items
		it->refine = 0;
		// card-less items
		memset(it->card, 0, sizeof(it->card));
	}

	switch(loc) {
		case TABLE_CART:
			size = MAX_CART;
			items = sd->cart.u.items_cart;
			break;
		case TABLE_STORAGE:
			size = MAX_STORAGE;
			items = sd->storage.u.items_storage;
			break;
		case TABLE_GUILD_STORAGE:
		{
			struct s_storage *gstor = guild2storage2(sd->status.guild_id);

			size = MAX_GUILD_STORAGE;
			items = gstor->u.items_guild;
		}
			break;
		default: // TABLE_INVENTORY
			size = MAX_INVENTORY;
			items = sd->inventory.u.items_inventory;
			break;
	}

	for(;;)
	{
		amount = it->amount;
		important = 0;

		// 1st pass -- less important items / exact match
		for( i = 0; amount && i < size; i++ )
		{
			struct item *itm = NULL;

			if( !&items[i] || !(itm = &items[i])->nameid || itm->nameid != it->nameid )
			{// wrong/invalid item
				continue;
			}

			if( itm->equip != it->equip || itm->refine != it->refine )
			{// not matching attributes
				important++;
				continue;
			}

			if( exact_match )
			{
				if( itm->identify != it->identify || itm->attribute != it->attribute || memcmp(itm->card, it->card, sizeof(itm->card)) )
				{// not matching exact attributes
					continue;
				}
			}
			else
			{
				if( itemdb_type(itm->nameid) == IT_PETEGG )
				{
					if( itm->card[0] == CARD0_PET && CheckForCharServer() )
					{// pet which cannot be deleted
						continue;
					}
				}
				else if( memcmp(itm->card, it->card, sizeof(itm->card)) )
				{// named/carded item
					important++;
					continue;
				}
			}

			// count / delete item
			buildin_delitem_delete(sd, i, &amount, loc, delete_items);
		}

		// 2nd pass -- any matching item
		if( amount == 0 || important == 0 )
		{// either everything was already consumed or no items were skipped
			;
		}
		else for( i = 0; amount && i < size; i++ )
		{
			struct item *itm = NULL;

			if( !&items[i] || !(itm = &items[i])->nameid || itm->nameid != it->nameid )
			{// wrong/invalid item
				continue;
			}

			if( itemdb_type(itm->nameid) == IT_PETEGG && itm->card[0] == CARD0_PET && CheckForCharServer() )
			{// pet which cannot be deleted
				continue;
			}

			if( exact_match )
			{
				if( itm->refine != it->refine || itm->identify != it->identify || itm->attribute != it->attribute || memcmp(itm->card, it->card, sizeof(itm->card)) )
				{// not matching attributes
					continue;
				}
			}

			// count / delete item
			buildin_delitem_delete(sd, i, &amount, loc, delete_items);
		}

		if( amount )
		{// not enough items
			return false;
		}
		else if( delete_items )
		{// we are done with the work
			return true;
		}
		else
		{// get rid of the items now
			delete_items = true;
		}
	}
}


/// Deletes items from the target/attached player.
/// Prioritizes ordinary items.
///
/// delitem <item id>,<amount>{,<account id>}
/// delitem "<item name>",<amount>{,<account id>}
/// cartdelitem <item id>,<amount>{,<account id>}
/// cartdelitem "<item name>",<amount>{,<account id>}
/// storagedelitem <item id>,<amount>{,<account id>}
/// storagedelitem "<item name>",<amount>{,<account id>}
BUILDIN_FUNC(delitem)
{
	TBL_PC *sd;
	struct item it;
	struct script_data *data;

	uint8 loc = 0;
	char* command = (char*)script_getfuncname(st);

	if(!strncmp(command, "cart", 4))
		loc = TABLE_CART;
	else if(!strncmp(command, "storage", 7))
		loc = TABLE_STORAGE;
 	else if(!strncmp(command, "guildstorage", 12))
		loc = TABLE_GUILD_STORAGE;

	if( script_hasdata(st,4) )
	{
		int account_id = script_getnum(st,4);
		sd = map_id2sd(account_id); // <account id>
		if( sd == NULL )
		{
			ShowError("buildin_%s: player not found (AID=%d).\n", command, account_id);
			st->state = END;
			return 1;
		}
	}
	else
	{
		sd = script_rid2sd(st);// attached player
		if( sd == NULL )
			return 0;
	}

	if (loc == TABLE_CART && !pc_iscarton(sd)) {
		ShowError("buildin_cartdelitem: player doesn't have cart (CID=%d).\n", sd->status.char_id);
		return 1;
	}
	if (loc == TABLE_GUILD_STORAGE) {
		struct s_storage *gstor = guild2storage2(sd->status.guild_id);

		if (gstor == NULL || sd->state.storage_flag) {
			script_pushint(st, -1);
			return 1;
		}
}

	data = script_getdata(st,2);
	get_val(st,data);
	if( data_isstring(data) )
	{
		const char* item_name = conv_str(st,data);
		struct item_data* id = itemdb_searchname(item_name);
		if( id == NULL )
		{
			ShowError("buildin_%s: unknown item \"%s\".\n", command, item_name);
			st->state = END;
			return 1;
		}
		it.nameid = id->nameid;// "<item name>"
	}
	else
	{
		it.nameid = conv_num(st,data);// <item id>
		if( !itemdb_exists( it.nameid ) )
		{
			ShowError("buildin_%s: unknown item \"%d\".\n", command, it.nameid);
			st->state = END;
			return 1;
		}
	}

	it.amount=script_getnum(st,3);

	if( it.amount <= 0 )
		return 0;// nothing to do

	if( buildin_delitem_search(sd, &it, false, loc) )
	{// success
		return 0;
	}

	ShowError("buildin_%s: failed to delete %d items (AID=%d item_id=%d).\n", command, it.amount, sd->status.account_id, it.nameid); 
	st->state = END;
	clif_scriptclose(sd, st->oid);
	return 1;
}

/// Deletes items from the target/attached player.
///
/// delitem2 <item id>,<amount>,<identify>,<refine>,<attribute>,<card1>,<card2>,<card3>,<card4>{,<account ID>}
/// delitem2 "<Item name>",<amount>,<identify>,<refine>,<attribute>,<card1>,<card2>,<card3>,<card4>{,<account ID>}
/// cartdelitem2 <item id>,<amount>,<identify>,<refine>,<attribute>,<card1>,<card2>,<card3>,<card4>{,<account ID>}
/// cartdelitem2 "<Item name>",<amount>,<identify>,<refine>,<attribute>,<card1>,<card2>,<card3>,<card4>{,<account ID>}
/// storagedelitem2 <item id>,<amount>,<identify>,<refine>,<attribute>,<card1>,<card2>,<card3>,<card4>{,<account ID>}
/// storagedelitem2 "<Item name>",<amount>,<identify>,<refine>,<attribute>,<card1>,<card2>,<card3>,<card4>{,<account ID>}
BUILDIN_FUNC(delitem2)
{
	TBL_PC *sd;
	struct item it;
	struct script_data *data;
	uint8 loc = 0;
	char* command = (char*)script_getfuncname(st);

	if(!strncmp(command, "cart", 4))
		loc = TABLE_CART;
	else if(!strncmp(command, "storage", 7))
		loc = TABLE_STORAGE;
	else if(!strncmp(command, "guildstorage", 12))
		loc = TABLE_GUILD_STORAGE;

	if( script_hasdata(st,11) )
	{
		int account_id = script_getnum(st,11);
		sd = map_id2sd(account_id); // <account id>
		if( sd == NULL )
		{
			ShowError("buildin_%s: player not found (AID=%d).\n", command, account_id);
			st->state = END;
			return 1;
		}
	}
	else
	{
		sd = script_rid2sd(st);// attached player
		if( sd == NULL )
			return 0;
	}

	if (loc == TABLE_CART && !pc_iscarton(sd)) {
		ShowError("buildin_cartdelitem: player doesn't have cart (CID=%d).\n", sd->status.char_id);
		script_pushint(st,-1);
		return 1;
	}
	if (loc == TABLE_GUILD_STORAGE) {
		struct s_storage *gstor = guild2storage2(sd->status.guild_id);

		if (gstor == NULL || sd->state.storage_flag) {
			script_pushint(st, -1);
			return 1;
		}
	}

	data = script_getdata(st,2);
	get_val(st,data);
	if( data_isstring(data) )
	{
		const char* item_name = conv_str(st,data);
		struct item_data* id = itemdb_searchname(item_name);
		if( id == NULL )
		{
			ShowError("buildin_%s: unknown item \"%s\".\n", command, item_name);
			st->state = END;
			return 1;
		}
		it.nameid = id->nameid;// "<item name>"
	}
	else
	{
		it.nameid = conv_num(st,data);// <item id>
		if( !itemdb_exists( it.nameid ) )
		{
			ShowError("buildin_%s: unknown item \"%d\".\n", command, it.nameid);
			st->state = END;
			return 1;
		}
	}

	it.amount=script_getnum(st,3);
	it.identify=script_getnum(st,4);
	it.refine=script_getnum(st,5);
	it.attribute=script_getnum(st,6);
	it.card[0]=(short)script_getnum(st,7);
	it.card[1]=(short)script_getnum(st,8);
	it.card[2]=(short)script_getnum(st,9);
	it.card[3]=(short)script_getnum(st,10);

	if( it.amount <= 0 )
		return 0;// nothing to do

	if( buildin_delitem_search(sd, &it, true, loc) )
	{// success
		return 0;
	}

	ShowError("buildin_%s: failed to delete %d items (AID=%d item_id=%d).\n", command, it.amount, sd->status.account_id, it.nameid); 
	st->state = END;
	clif_scriptclose(sd, st->oid);
	return 1;
}

/*==========================================
 * Enables/Disables use of items while in an NPC [Skotlex]
 *------------------------------------------*/
BUILDIN_FUNC(enableitemuse)
{
	TBL_PC *sd;
	sd=script_rid2sd(st);
	if (sd)
		sd->npc_item_flag = st->oid;
	return 0;
}

BUILDIN_FUNC(disableitemuse)
{
	TBL_PC *sd;
	sd=script_rid2sd(st);
	if (sd)
		sd->npc_item_flag = 0;
	return 0;
}

/*==========================================
 * return the basic stats of sd
 * chk pc_readparam for available type
 *------------------------------------------*/
BUILDIN_FUNC(readparam)
{
	int type;
	TBL_PC *sd;

	type=script_getnum(st,2);
	if( script_hasdata(st,3) )
		sd=map_nick2sd(script_getstr(st,3));
	else
		sd=script_rid2sd(st);

	if(sd==NULL){
		script_pushint(st,-1);
		return 0;
	}

	script_pushint(st,pc_readparam(sd,type));

	return 0;
}
/*==========================================
 * Return charid identification
 * return by @num :
 *	0 : char_id
 *	1 : party_id
 *	2 : guild_id
 *	3 : account_id
 *	4 : bg_id
 *	5 : clan_id
 *------------------------------------------*/
BUILDIN_FUNC(getcharid)
{
	int num;
	TBL_PC *sd;

	num = script_getnum(st,2);
	if( script_hasdata(st,3) )
		sd=map_nick2sd(script_getstr(st,3));
	else
		sd=script_rid2sd(st);

	if(sd==NULL){
		script_pushint(st,0);	//return 0, according docs
		return 0;
	}

	switch(num) 
	{
		case 0: script_pushint(st, sd->status.char_id); break;
		case 1: script_pushint(st, sd->status.party_id); break;
		case 2: script_pushint(st, sd->status.guild_id); break;
		case 3: script_pushint(st, sd->status.account_id); break;
		case 4: script_pushint(st, sd->bg_id); break;
		case 5: script_pushint(st, sd->status.clan_id); break;
		default:
			ShowError("buildin_getcharid: invalid parameter (%d).\n", num);
			script_pushint(st, 0);
			break;
	}
		
	return 0;
}
/*==========================================
 * [Paradox924X]
 *------------------------------------------*/
BUILDIN_FUNC(getnpcid)
{
	int num = script_getnum(st, 2);
	struct npc_data* nd = NULL;

	if (script_hasdata(st, 3))
	{// unique npc name
		if ((nd = npc_name2id(script_getstr(st, 3))) == NULL)
		{
			ShowError("buildin_getnpcid: No such NPC '%s'.\n", script_getstr(st, 3));
			script_pushint(st, 0);
			return 1;
		}
	}

	switch(num)
	{
		case 0:
			script_pushint(st, nd ? nd->bl.id : st->oid);
			break;
		default:
			ShowError("buildin_getnpcid: invalid parameter (%d).\n", num);
			script_pushint(st, 0);
			return 1;
	}

	return 0;
}
/*==========================================
 *�w��ID��PT���擾
 *------------------------------------------*/
BUILDIN_FUNC(getpartyname)
{
	int party_id;
	struct party_data* p;

	party_id = script_getnum(st,2);

	if( ( p = party_search(party_id) ) != NULL )
	{
		script_pushstrcopy(st,p->party.name);
	}
	else
	{
		script_pushconststr(st,"null");
	}
	return 0;
}
/*==========================================
 *�w��ID��PT�l���ƃ����o�[ID�擾
 *------------------------------------------*/
BUILDIN_FUNC(getpartymember)
{
	struct party_data *p;
	int i,j=0,type=0;

	p=party_search(script_getnum(st,2));

	if( script_hasdata(st,3) )
 		type=script_getnum(st,3);
	
	if(p!=NULL){
		for(i=0;i<MAX_PARTY;i++){
			if(p->party.member[i].account_id){
				switch (type) {
				case 2:
					mapreg_setreg(reference_uid(add_str("$@partymemberaid"), j),p->party.member[i].account_id);
					break;
				case 1:
					mapreg_setreg(reference_uid(add_str("$@partymembercid"), j),p->party.member[i].char_id);
					break;
				default:
					mapreg_setregstr(reference_uid(add_str("$@partymembername$"), j),p->party.member[i].name);
				}
				j++;
			}
		}
	}
	mapreg_setreg(add_str("$@partymembercount"),j);

	return 0;
}

/*==========================================
 * Retrieves party leader. if flag is specified, 
 * return some of the leader data. Otherwise, return name.
 *------------------------------------------*/
BUILDIN_FUNC(getpartyleader)
{
	int party_id, type = 0, i=0;
	struct party_data *p;

	party_id=script_getnum(st,2);
	if( script_hasdata(st,3) )
 		type=script_getnum(st,3);

	p=party_search(party_id);

	if (p) //Search leader
	for(i = 0; i < MAX_PARTY && !p->party.member[i].leader; i++);

	if (!p || i == MAX_PARTY) { //leader not found
		if (type)
			script_pushint(st,-1);
		else
			script_pushconststr(st,"null");
		return 0;
	}

	switch (type) {
		case 1: script_pushint(st,p->party.member[i].account_id); break;
		case 2: script_pushint(st,p->party.member[i].char_id); break;
		case 3: script_pushint(st,p->party.member[i].class_); break;
		case 4: script_pushstrcopy(st,mapindex_id2name(p->party.member[i].map)); break;
		case 5: script_pushint(st,p->party.member[i].lv); break;
		default: script_pushstrcopy(st,p->party.member[i].name); break;
	}
	return 0;
}

/*==========================================
 *�w��ID�̃M���h���擾
 *------------------------------------------*/
BUILDIN_FUNC(getguildname)
{
	int guild_id;
	struct guild* g;

	guild_id = script_getnum(st,2);

	if( ( g = guild_search(guild_id) ) != NULL )
	{
		script_pushstrcopy(st,g->name);
	}
	else
	{
		script_pushconststr(st,"null");
	}
	return 0;
}

/*==========================================
 *�w��ID��GuildMaster���擾
 *------------------------------------------*/
BUILDIN_FUNC(getguildmaster)
{
	int guild_id;
	struct guild* g;

	guild_id = script_getnum(st,2);

	if( ( g = guild_search(guild_id) ) != NULL )
	{
		script_pushstrcopy(st,g->member[0].name);
	}
	else
	{
		script_pushconststr(st,"null");
	}
	return 0;
}

BUILDIN_FUNC(getguildmasterid)
{
	int guild_id;
	struct guild* g;

	guild_id = script_getnum(st,2);

	if( ( g = guild_search(guild_id) ) != NULL )
	{
		script_pushint(st,g->member[0].char_id);
	}
	else
	{
		script_pushint(st,0);
	}
	return 0;
}

/*==========================================
 * �L�����N�^�̖��O
 *------------------------------------------*/
BUILDIN_FUNC(strcharinfo)
{
	TBL_PC *sd;
	int num;
	struct guild* g;
	struct party_data* p;

	sd=script_rid2sd(st);
	if (!sd) { //Avoid crashing....
		script_pushconststr(st,"");
		return 0;
	}
	num=script_getnum(st,2);
	switch(num){
		case 0:
			script_pushstrcopy(st,sd->status.name);
			break;
		case 1:
			if( ( p = party_search(sd->status.party_id) ) != NULL )
			{
				script_pushstrcopy(st,p->party.name);
			}
			else
			{
				script_pushconststr(st,"");
			}
			break;
		case 2:
			if( ( g = guild_search(sd->status.guild_id) ) != NULL )
			{
				script_pushstrcopy(st,g->name);
			}
			else
			{
				script_pushconststr(st,"");
			}
			break;
		case 3:
			script_pushconststr(st,map[sd->bl.m].name);
			break;
		default:
			ShowWarning("buildin_strcharinfo: unknown parameter.\n");
			script_pushconststr(st,"");
			break;
	}

	return 0;
}

/*==========================================
 * �Ăяo������NPC�����擾����
 *------------------------------------------*/
BUILDIN_FUNC(strnpcinfo)
{
	TBL_NPC* nd;
	int num;
	char *buf,*name=NULL;

	nd = map_id2nd(st->oid);
	if (!nd) {
		script_pushconststr(st, "");
		return 0;
	}

	num = script_getnum(st,2);
	switch(num){
		case 0: // display name
			name = aStrdup(nd->name);
			break;
		case 1: // visible part of display name
			if((buf = strchr(nd->name,'#')) != NULL)
			{
				name = aStrdup(nd->name);
				name[buf - nd->name] = 0;
			} else // Return the name, there is no '#' present
				name = aStrdup(nd->name);
			break;
		case 2: // # fragment
			if((buf = strchr(nd->name,'#')) != NULL)
				name = aStrdup(buf+1);
			break;
		case 3: // unique name
			name = aStrdup(nd->exname);
			break;
		case 4: // map name
			name = aStrdup(map[nd->bl.m].name);
			break;
	}

	if(name)
		script_pushstr(st, name);
	else
		script_pushconststr(st, "");

	return 0;
}


// aegis->athena slot position conversion table
unsigned int equip[] = {EQP_HEAD_TOP,EQP_ARMOR,EQP_HAND_L,EQP_HAND_R,EQP_GARMENT,EQP_SHOES,EQP_ACC_L,EQP_ACC_R,EQP_HEAD_MID,EQP_HEAD_LOW,EQP_COSTUME_HEAD_LOW,EQP_COSTUME_HEAD_MID,EQP_COSTUME_HEAD_TOP,EQP_COSTUME_GARMENT,EQP_COSTUME_FLOOR,EQP_SHADOW_ARMOR,EQP_SHADOW_WEAPON,EQP_SHADOW_SHIELD,EQP_SHADOW_SHOES,EQP_SHADOW_ACC_R,EQP_SHADOW_ACC_L};

/*==========================================
 * GetEquipID(Pos);     Pos: 1-10
 *------------------------------------------*/
BUILDIN_FUNC(getequipid)
{
	int i, num;
	TBL_PC* sd;
	struct item_data* item;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	num = script_getnum(st,2) - 1;
	if( num < 0 || num >= ARRAYLENGTH(equip) )
	{
		script_pushint(st,-1);
		return 0;
	}

	// get inventory position of item
	i = pc_checkequip(sd,equip[num], false);
	if( i < 0 )
	{
		script_pushint(st,-1);
		return 0;
	}
		
	item = sd->inventory_data[i];
	if( item != 0 )
		script_pushint(st,item->nameid);
	else
		script_pushint(st,0);

	return 0;
}

/*==========================================
 * ������������i���B���j���[�p�j
 *------------------------------------------*/
BUILDIN_FUNC(getequipname)
{
	int i, num;
	TBL_PC* sd;
	struct item_data* item;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	num = script_getnum(st,2) - 1;
	if( num < 0 || num >= ARRAYLENGTH(equip) )
	{
		script_pushconststr(st,"");
		return 0;
	}

	// get inventory position of item
	i = pc_checkequip(sd,equip[num], false);
	if( i < 0 )
	{
		script_pushint(st,-1);
		return 0;
	}

	item = sd->inventory_data[i];
	if( item != 0 )
		script_pushstrcopy(st,item->jname);
	else
		script_pushconststr(st,"");

	return 0;
}

/*==========================================
 * getbrokenid [Valaris]
 *------------------------------------------*/
BUILDIN_FUNC(getbrokenid)
{
	int i,num,id=0,brokencounter=0;
	TBL_PC *sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	num=script_getnum(st,2);
	for(i=0; i<MAX_INVENTORY; i++) {
		if(sd->inventory.u.items_inventory[i].attribute){
				brokencounter++;
				if(num==brokencounter){
					id=sd->inventory.u.items_inventory[i].nameid;
					break;
				}
		}
	}

	script_pushint(st,id);

	return 0;
}

/*==========================================
 * repair [Valaris]
 *------------------------------------------*/
BUILDIN_FUNC(repair)
{
	int i,num;
	int repaircounter=0;
	TBL_PC *sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	num=script_getnum(st,2);
	for(i=0; i<MAX_INVENTORY; i++) {
		if(sd->inventory.u.items_inventory[i].attribute){
				repaircounter++;
				if(num==repaircounter){
					sd->inventory.u.items_inventory[i].attribute=0;
					clif_equiplist(sd);
					clif_produceeffect(sd, 0, sd->inventory.u.items_inventory[i].nameid);
					clif_misceffect(&sd->bl, 3);
					break;
				}
		}
	}

	return 0;
}

/*==========================================
* repairall {<char_id>}
*------------------------------------------*/
BUILDIN_FUNC(repairall)
{
	int i, repaircounter = 0;
	TBL_PC *sd;

	if (!script_charid2sd(2, sd))
		return 1;

	for (i = 0; i < MAX_INVENTORY; i++)
	{
		if (sd->inventory.u.items_inventory[i].nameid && sd->inventory.u.items_inventory[i].attribute)
		{
			sd->inventory.u.items_inventory[i].attribute = 0;
			clif_produceeffect(sd, 0, sd->inventory.u.items_inventory[i].nameid);
			repaircounter++;
		}
	}

	if (repaircounter)
	{
		clif_misceffect(&sd->bl, 3);
		clif_equiplist(sd);
	}

	return 0;
}

/*==========================================
 * �����`�F�b�N
 *------------------------------------------*/
BUILDIN_FUNC(getequipisequiped)
{
	int i=-1,num;
	TBL_PC *sd;

	num=script_getnum(st,2);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if (num > 0 && num <= ARRAYLENGTH(equip))
		i=pc_checkequip(sd,equip[num-1],false);

	if(i >= 0)
		script_pushint(st,1);
	else
		 script_pushint(st,0);
	return 0;
}

/*==========================================
 * �����i���B�\�`�F�b�N
 *------------------------------------------*/
BUILDIN_FUNC(getequipisenableref)
{
	int i=-1,num;
	TBL_PC *sd;

	num=script_getnum(st,2);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if( num > 0 && num <= ARRAYLENGTH(equip) )
		i = pc_checkequip(sd,equip[num-1],false);
	if( i >= 0 && sd->inventory_data[i] && !sd->inventory_data[i]->flag.no_refine && !sd->inventory.u.items_inventory[i].expire_time )
		script_pushint(st,1);
	else
		script_pushint(st,0);

	return 0;
}

/*==========================================
 * �����i�Ӓ�`�F�b�N
 *------------------------------------------*/
BUILDIN_FUNC(getequipisidentify)
{
	int i=-1,num;
	TBL_PC *sd;

	num=script_getnum(st,2);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if (num > 0 && num <= ARRAYLENGTH(equip))
		i=pc_checkequip(sd,equip[num-1],false);
	if(i >= 0)
		script_pushint(st,sd->inventory.u.items_inventory[i].identify);
	else
		script_pushint(st,0);

	return 0;
}

/*==========================================
 * �����i���B�x
 *------------------------------------------*/
BUILDIN_FUNC(getequiprefinerycnt)
{
	int i=-1,num;
	TBL_PC *sd;

	num=script_getnum(st,2);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if (num > 0 && num <= ARRAYLENGTH(equip))
		i=pc_checkequip(sd,equip[num-1],false);
	if(i >= 0)
		script_pushint(st,sd->inventory.u.items_inventory[i].refine);
	else
		script_pushint(st,0);

	return 0;
}

/*==========================================
 * �����i����LV
 *------------------------------------------*/
BUILDIN_FUNC(getequipweaponlv)
{
	int i=-1,num;
	TBL_PC *sd;

	num=script_getnum(st,2);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if (num > 0 && num <= ARRAYLENGTH(equip))
		i=pc_checkequip(sd,equip[num-1],false);
	if(i >= 0 && sd->inventory_data[i])
		script_pushint(st,sd->inventory_data[i]->wlv);
	else
		script_pushint(st,0);

	return 0;
}

/*==========================================
 * �����i���B������
 *------------------------------------------*/
BUILDIN_FUNC(getequippercentrefinery)
{
	int i=-1,num;
	TBL_PC *sd;

	num=script_getnum(st,2);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if (num > 0 && num <= ARRAYLENGTH(equip))
		i=pc_checkequip(sd,equip[num-1],false);
	if(i >= 0 && sd->inventory.u.items_inventory[i].nameid && sd->inventory.u.items_inventory[i].refine < MAX_REFINE)
		script_pushint(st,percentrefinery[itemdb_wlv(sd->inventory.u.items_inventory[i].nameid)][(int)sd->inventory.u.items_inventory[i].refine]);
	else
		script_pushint(st,0);

	return 0;
}

/*==========================================
 * �Refine +1 item at pos and log and display refine
 *------------------------------------------*/
BUILDIN_FUNC(successrefitem)
{
	int i=-1,num,ep;
	TBL_PC *sd;

	num=script_getnum(st,2);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if (num > 0 && num <= ARRAYLENGTH(equip))
		i=pc_checkequip(sd,equip[num-1],false);
	if(i >= 0) {
		ep=sd->inventory.u.items_inventory[i].equip;

		//Logs items, got from (N)PC scripts [Lupus]
		log_pick(&sd->bl, LOG_TYPE_SCRIPT, sd->inventory.u.items_inventory[i].nameid, -1, &sd->inventory.u.items_inventory[i]);

		sd->inventory.u.items_inventory[i].refine++;
		pc_unequipitem(sd,i,2); // status calc will happen in pc_equipitem() below

		clif_refine(sd->fd,0,i,sd->inventory.u.items_inventory[i].refine);
		clif_delitem(sd,i,1,3);

		//Logs items, got from (N)PC scripts [Lupus]
		log_pick(&sd->bl, LOG_TYPE_SCRIPT, sd->inventory.u.items_inventory[i].nameid, 1, &sd->inventory.u.items_inventory[i]);

		clif_additem(sd,i,1,0);
		pc_equipitem(sd,i,ep,false);
		clif_misceffect(&sd->bl,3);

		achievement_validate_refine(sd, i, true); // Achievements [Smokexyz/Hercules]

		/* The following check is exclusive to characters (possibly only whitesmiths)
		 * that create equipments and refine them to level 10. */
		if(sd->inventory.u.items_inventory[i].refine == MAX_REFINE &&
			sd->inventory.u.items_inventory[i].card[0] == CARD0_FORGE &&
		  	sd->status.char_id == (int)MakeDWord(sd->inventory.u.items_inventory[i].card[2],sd->inventory.u.items_inventory[i].card[3])
		){ // Fame point system [DracoRPG]
	 		switch (sd->inventory_data[i]->wlv){
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
	}

	return 0;
}

/*==========================================
 * Show a failed Refine +1 attempt
 *------------------------------------------*/
BUILDIN_FUNC(failedrefitem)
{
	int i=-1,num;
	TBL_PC *sd;

	num=script_getnum(st,2);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if (num > 0 && num <= ARRAYLENGTH(equip))
		i=pc_checkequip(sd,equip[num-1],false);
	if(i >= 0) {
		// Call before changing refine to 0.
		achievement_validate_refine(sd, i, false);

		//Logs items, got from (N)PC scripts [Lupus]
		log_pick(&sd->bl, LOG_TYPE_SCRIPT, sd->inventory.u.items_inventory[i].nameid, -1, &sd->inventory.u.items_inventory[i]);

		sd->inventory.u.items_inventory[i].refine = 0;
		pc_unequipitem(sd,i,3);
		// ���B���s�G�t�F�N�g�̃p�P�b�g
		clif_refine(sd->fd,1,i,sd->inventory.u.items_inventory[i].refine);

		pc_delitem(sd,i,1,0,2);
		// ���̐l�ɂ����s��ʒm
		clif_misceffect(&sd->bl,2);
	}

	return 0;
}

/*==========================================
 * Downgrades an Equipment Part by -1 . [Masao]
 *------------------------------------------*/
BUILDIN_FUNC(downrefitem) {
	short i = -1, down = 1;
	int pos;
	TBL_PC *sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;
	pos = script_getnum(st,2);
	if (script_hasdata(st, 3))
		down = script_getnum(st, 3);

	if (pos > 0 && pos <= ARRAYLENGTH(equip))
		i = pc_checkequip(sd,equip[pos-1],false);
	if (i >= 0) {
		unsigned int ep = sd->inventory.u.items_inventory[i].equip;

		//Logs items, got from (N)PC scripts [Lupus]
		log_pick(&sd->bl, LOG_TYPE_SCRIPT, sd->inventory.u.items_inventory[i].nameid, -1, &sd->inventory.u.items_inventory[i]);

		pc_unequipitem(sd,i,2); // status calc will happen in pc_equipitem() below
		sd->inventory.u.items_inventory[i].refine -= down;
		sd->inventory.u.items_inventory[i].refine = cap_value( sd->inventory.u.items_inventory[i].refine, 0, MAX_REFINE);

		clif_refine(sd->fd,2,i,sd->inventory.u.items_inventory[i].refine);
		clif_delitem(sd,i,1,3);

		//Logs items, got from (N)PC scripts [Lupus]
		log_pick(&sd->bl, LOG_TYPE_SCRIPT, sd->inventory.u.items_inventory[i].nameid, -1, &sd->inventory.u.items_inventory[i]);

		clif_additem(sd,i,1,0);
		pc_equipitem(sd,i,ep,false);
		achievement_validate_refine(sd, i, false); // Achievements [Smokexyz/Hercules]
		clif_misceffect(&sd->bl,2);
		script_pushint(st, sd->inventory.u.items_inventory[i].refine);
		return 1;
	}

	ShowError("buildin_downrefitem: No item equipped at pos %d (CID=%d/AID=%d).\n", pos, sd->status.char_id, sd->status.account_id);
	script_pushint(st, -1);
	return 0;
}

/**
 * Delete the item equipped at pos.
 * delequip <equipment slot>{,<char_id>};
 **/
BUILDIN_FUNC(delequip) {
	short i = -1;
	int pos;
	int8 ret;
	TBL_PC *sd;

	pos = script_getnum(st,2);
	if (!(sd = map_charid2sd(script_getnum(st,3)))) {
		st->state = END;
		return 1;
	}

	if (pos > 0 && pos <= ARRAYLENGTH(equip))
		i = pc_checkequip(sd,equip[pos-1],false);
	if (i >= 0) {
		pc_unequipitem(sd,i,3); //recalculate bonus
		ret = !(pc_delitem(sd,i,1,0,2));
	}
	else {
		ShowError("buildin_delequip: No item equipped at pos %d (CID=%d/AID=%d).\n", pos, sd->status.char_id, sd->status.account_id);
		st->state = END;
		return 1;
	}

	script_pushint(st,ret);
	return 0;
}

/**
* Break the item equipped at pos.
* breakequip <equipment slot>{,<char_id>};
**/
BUILDIN_FUNC(breakequip) {
	short i = -1;
	int pos;
	TBL_PC *sd;

	pos = script_getnum(st, 2);
	if (!script_charid2sd(3, sd))
		return 1;

	if (equip_index_check(pos))
		i = pc_checkequip(sd, equip[pos-1],false);
	if (i >= 0) {
		sd->inventory.u.items_inventory[i].attribute = 1;
		pc_unequipitem(sd, i, 3);
		clif_equiplist(sd);
		script_pushint(st, 1);
		return 0;
	}

	ShowError("buildin_breakequip: No item equipped at pos %d (CID=%d/AID=%d).\n", pos, sd->status.char_id, sd->status.account_id);
	script_pushint(st, 0);
	return 1;
}

/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(statusup)
{
	int type;
	TBL_PC *sd;

	type=script_getnum(st,2);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	pc_statusup(sd,type,1);

	return 0;
}
/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(statusup2)
{
	int type,val;
	TBL_PC *sd;

	type=script_getnum(st,2);
	val=script_getnum(st,3);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	pc_statusup2(sd,type,val);

	return 0;
}

/// See 'doc/item_bonus.txt'
///
/// bonus <bonus type>,<val1>;
/// bonus2 <bonus type>,<val1>,<val2>;
/// bonus3 <bonus type>,<val1>,<val2>,<val3>;
/// bonus4 <bonus type>,<val1>,<val2>,<val3>,<val4>;
/// bonus5 <bonus type>,<val1>,<val2>,<val3>,<val4>,<val5>;
BUILDIN_FUNC(bonus)
{
	int type;
	int val1;
	int val2 = 0;
	int val3 = 0;
	int val4 = 0;
	int val5 = 0;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0; // no player attached

	type = script_getnum(st,2);
	switch( type )
	{
	case SP_AUTOSPELL:
	case SP_AUTOSPELL_WHENHIT:
	case SP_AUTOSPELL_ONSKILL:
	case SP_SKILL_ATK:
	case SP_SKILL_HEAL:
	case SP_SKILL_HEAL2:
	case SP_ADD_SKILL_BLOW:
	case SP_CASTRATE:
	case SP_FIXEDCASTRATE:
	case SP_SKILL_COOLDOWN:
	case SP_ADDEFF_ONSKILL:
		// these bonuses support skill names
		val1 = ( script_isstring(st,3) ? skill_name2id(script_getstr(st,3)) : script_getnum(st,3) );
		break;
	default:
		val1 = script_getnum(st,3);
		break;
	}

	switch( script_lastdata(st)-2 )
	{
	case 1:
		pc_bonus(sd, type, val1);
		break;
	case 2:	
		val2 = script_getnum(st,4);
		pc_bonus2(sd, type, val1, val2);
		break;
	case 3:
		val2 = script_getnum(st,4);
		val3 = script_getnum(st,5);
		pc_bonus3(sd, type, val1, val2, val3);
		break;
	case 4:
		if( type == SP_AUTOSPELL_ONSKILL && script_isstring(st,4) )
			val2 = skill_name2id(script_getstr(st,4)); // 2nd value can be skill name
		else
			val2 = script_getnum(st,4);

		val3 = script_getnum(st,5);
		val4 = script_getnum(st,6);
		pc_bonus4(sd, type, val1, val2, val3, val4);
		break;
	case 5:
		if( type == SP_AUTOSPELL_ONSKILL && script_isstring(st,4) )
			val2 = skill_name2id(script_getstr(st,4)); // 2nd value can be skill name
		else
			val2 = script_getnum(st,4);

		val3 = script_getnum(st,5);
		val4 = script_getnum(st,6);
		val5 = script_getnum(st,7);
		pc_bonus5(sd, type, val1, val2, val3, val4, val5);
		break;
	default:
		ShowDebug("buildin_bonus: unexpected number of arguments (%d)\n", (script_lastdata(st) - 1));
		break;
	}

	return 0;
}

BUILDIN_FUNC(autobonus)
{
	unsigned int dur;
	short rate;
	short atk_type = 0;
	TBL_PC* sd;
	const char *bonus_script, *other_script = NULL;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0; // no player attached

	if( sd->state.autobonus&sd->inventory.u.items_inventory[current_equip_item_index].equip )
		return 0;

	rate = script_getnum(st,3);
	dur = script_getnum(st,4);
	bonus_script = script_getstr(st,2);
	if( !rate || !dur || !bonus_script )
		return 0;

	if( script_hasdata(st,5) )
		atk_type = script_getnum(st,5);
	if( script_hasdata(st,6) )
		other_script = script_getstr(st,6);

	if( pc_addautobonus(sd->autobonus,ARRAYLENGTH(sd->autobonus),
		bonus_script,rate,dur,atk_type,other_script,sd->inventory.u.items_inventory[current_equip_item_index].equip,false) )
	{
		script_add_autobonus(bonus_script);
		if( other_script )
			script_add_autobonus(other_script);
	}

	return 0;
}

BUILDIN_FUNC(autobonus2)
{
	unsigned int dur;
	short rate;
	short atk_type = 0;
	TBL_PC* sd;
	const char *bonus_script, *other_script = NULL;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0; // no player attached

	if( sd->state.autobonus&sd->inventory.u.items_inventory[current_equip_item_index].equip )
		return 0;

	rate = script_getnum(st,3);
	dur = script_getnum(st,4);
	bonus_script = script_getstr(st,2);
	if( !rate || !dur || !bonus_script )
		return 0;

	if( script_hasdata(st,5) )
		atk_type = script_getnum(st,5);
	if( script_hasdata(st,6) )
		other_script = script_getstr(st,6);

	if( pc_addautobonus(sd->autobonus2,ARRAYLENGTH(sd->autobonus2),
		bonus_script,rate,dur,atk_type,other_script,sd->inventory.u.items_inventory[current_equip_item_index].equip,false) )
	{
		script_add_autobonus(bonus_script);
		if( other_script )
			script_add_autobonus(other_script);
	}

	return 0;
}

BUILDIN_FUNC(autobonus3)
{
	unsigned int dur;
	short rate,atk_type;
	TBL_PC* sd;
	const char *bonus_script, *other_script = NULL;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0; // no player attached

	if( sd->state.autobonus&sd->inventory.u.items_inventory[current_equip_item_index].equip )
		return 0;

	rate = script_getnum(st,3);
	dur = script_getnum(st,4);
	atk_type = ( script_isstring(st,5) ? skill_name2id(script_getstr(st,5)) : script_getnum(st,5) );
	bonus_script = script_getstr(st,2);
	if( !rate || !dur || !atk_type || !bonus_script )
		return 0;

	if( script_hasdata(st,6) )
		other_script = script_getstr(st,6);

	if( pc_addautobonus(sd->autobonus3,ARRAYLENGTH(sd->autobonus3),
		bonus_script,rate,dur,atk_type,other_script,sd->inventory.u.items_inventory[current_equip_item_index].equip,true) )
	{
		script_add_autobonus(bonus_script);
		if( other_script )
			script_add_autobonus(other_script);
	}

	return 0;
}

/// Changes the level of a player skill.
/// <flag> defaults to 1
/// <flag>=0 : set the level of the skill
/// <flag>=1 : set the temporary level of the skill
/// <flag>=2 : add to the level of the skill
///
/// skill <skill id>,<level>,<flag>
/// skill <skill id>,<level>
/// skill "<skill name>",<level>,<flag>
/// skill "<skill name>",<level>
BUILDIN_FUNC(skill)
{
	int id;
	int level;
	int flag = 1;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	id = ( script_isstring(st,2) ? skill_name2id(script_getstr(st,2)) : script_getnum(st,2) );
	level = script_getnum(st,3);
	if( script_hasdata(st,4) )
		flag = script_getnum(st,4);
	pc_skill(sd, id, level, flag);

	return 0;
}

/// Changes the level of a player skill.
/// like skill, but <flag> defaults to 2
///
/// addtoskill <skill id>,<amount>,<flag>
/// addtoskill <skill id>,<amount>
/// addtoskill "<skill name>",<amount>,<flag>
/// addtoskill "<skill name>",<amount>
///
/// @see skill
BUILDIN_FUNC(addtoskill)
{
	int id;
	int level;
	int flag = 2;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	id = ( script_isstring(st,2) ? skill_name2id(script_getstr(st,2)) : script_getnum(st,2) );
	level = script_getnum(st,3);
	if( script_hasdata(st,4) )
		flag = script_getnum(st,4);
	pc_skill(sd, id, level, flag);

	return 0;
}

/// Increases the level of a guild skill.
///
/// guildskill <skill id>,<amount>;
/// guildskill "<skill name>",<amount>;
BUILDIN_FUNC(guildskill)
{
	int id;
	int level;
	TBL_PC* sd;
	int i;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	id = ( script_isstring(st,2) ? skill_name2id(script_getstr(st,2)) : script_getnum(st,2) );
	level = script_getnum(st,3);
	for( i=0; i < level; i++ )
		guild_skillup(sd, id);

	return 0;
}

/// Returns the level of the player skill.
///
/// getskilllv(<skill id>) -> <level>
/// getskilllv("<skill name>") -> <level>
BUILDIN_FUNC(getskilllv)
{
	int id;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	id = ( script_isstring(st,2) ? skill_name2id(script_getstr(st,2)) : script_getnum(st,2) );
	script_pushint(st, pc_checkskill(sd,id));

	return 0;
}

/// Returns the level of the guild skill.
///
/// getgdskilllv(<guild id>,<skill id>) -> <level>
/// getgdskilllv(<guild id>,"<skill name>") -> <level>
BUILDIN_FUNC(getgdskilllv)
{
	int guild_id;
	int skill_id;
	struct guild* g;

	guild_id = script_getnum(st,2);
	skill_id = ( script_isstring(st,3) ? skill_name2id(script_getstr(st,3)) : script_getnum(st,3) );
	g = guild_search(guild_id);
	if( g == NULL )
		script_pushint(st, -1);
	else
		script_pushint(st, guild_checkskill(g,skill_id));

	return 0;
}

/// Returns the 'basic_skill_check' setting.
/// This config determines if the server checks the skill level of NV_BASIC 
/// before allowing the basic actions.
///
/// basicskillcheck() -> <bool>
BUILDIN_FUNC(basicskillcheck)
{
	script_pushint(st, battle_config.basic_skill_check);
	return 0;
}

/// Returns the GM level of the player.
///
/// getgmlevel() -> <level>
BUILDIN_FUNC(getgmlevel)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	script_pushint(st, pc_isGM(sd));

	return 0;
}

/// Terminates the execution of this script instance.
///
/// end
BUILDIN_FUNC(end)
{
	st->state = END;
	return 0;
}

/// Checks if the player has that effect state (option).
///
/// checkoption(<option>) -> <bool>
BUILDIN_FUNC(checkoption)
{
	int option;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	option = script_getnum(st,2);
	if( sd->sc.option&option )
		script_pushint(st, 1);
	else
		script_pushint(st, 0);

	return 0;
}

/// Checks if the player is in that body state (opt1).
///
/// checkoption1(<opt1>) -> <bool>
BUILDIN_FUNC(checkoption1)
{
	int opt1;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	opt1 = script_getnum(st,2);
	if( sd->sc.opt1 == opt1 )
		script_pushint(st, 1);
	else
		script_pushint(st, 0);

	return 0;
}

/// Checks if the player has that health state (opt2).
///
/// checkoption2(<opt2>) -> <bool>
BUILDIN_FUNC(checkoption2)
{
	int opt2;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	opt2 = script_getnum(st,2);
	if( sd->sc.opt2&opt2 )
		script_pushint(st, 1);
	else
		script_pushint(st, 0);

	return 0;
}

/// Changes the effect state (option) of the player.
/// <flag> defaults to 1
/// <flag>=0 : removes the option
/// <flag>=other : adds the option
///
/// setoption <option>,<flag>;
/// setoption <option>;
BUILDIN_FUNC(setoption)
{
	int option;
	int flag = 1;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	option = script_getnum(st,2);
	if( script_hasdata(st,3) )
		flag = script_getnum(st,3);
	else if( !option ){// Request to remove everything.
		flag = 0;
		option = OPTION_CART|OPTION_FALCON|OPTION_RIDING|OPTION_DRAGON|OPTION_WUG|OPTION_WUGRIDER|OPTION_MADOGEAR;
	}
	if( flag ){// Add option
		if( option&OPTION_WEDDING && !battle_config.wedding_modifydisplay )
			option &= ~OPTION_WEDDING;// Do not show the wedding sprites
		pc_setoption(sd, sd->sc.option|option);
	} else// Remove option
		pc_setoption(sd, sd->sc.option&~option);

	return 0;
}

/// Returns if the player has a cart.
///
/// checkcart() -> <bool>
///
/// @author Valaris
BUILDIN_FUNC(checkcart)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( pc_iscarton(sd) )
		script_pushint(st, 1);
	else
		script_pushint(st, 0);

	return 0;
}

/// Sets the cart of the player.
/// <type> defaults to 1
/// <type>=0 : removes the cart
/// <type>=1 : Normal cart
/// <type>=2 : Wooden cart
/// <type>=3 : Covered cart with flowers and ferns
/// <type>=4 : Wooden cart with a Panda doll on the back
/// <type>=5 : Normal cart with bigger wheels, a roof and a banner on the back
///
/// setcart <type>;
/// setcart;
BUILDIN_FUNC(setcart)
{
	int type = 1;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( script_hasdata(st,2) )
		type = script_getnum(st,2);
	pc_setcart(sd, type);

	return 0;
}

/// Returns if the player has a falcon.
///
/// checkfalcon() -> <bool>
///
/// @author Valaris
BUILDIN_FUNC(checkfalcon)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( pc_isfalcon(sd) )
		script_pushint(st, 1);
	else
		script_pushint(st, 0);

	return 0;
}

/// Sets if the player has a falcon or not.
/// <flag> defaults to 1
///
/// setfalcon <flag>;
/// setfalcon;
BUILDIN_FUNC(setfalcon)
{
	int flag = 1;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( script_hasdata(st,2) )
		flag = script_getnum(st,2);

	pc_setfalcon(sd, flag);

	return 0;
}

/// Returns if the player is riding.
///
/// checkriding() -> <bool>
///
/// @author Valaris
BUILDIN_FUNC(checkriding)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( pc_isriding(sd) )
		script_pushint(st, 1);
	else
		script_pushint(st, 0);

	return 0;
}

/// Sets if the player is riding.
/// <flag> defaults to 1
///
/// setriding <flag>;
/// setriding;
BUILDIN_FUNC(setriding)
{
	int flag = 1;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( script_hasdata(st,2) )
		flag = script_getnum(st,2);
	pc_setriding(sd, flag);

	return 0;
}

/// Returns if the player is on a dragon.
///
/// checkdragon() -> <bool>
BUILDIN_FUNC(checkdragon)
{
	TBL_PC* sd;
	
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( pc_isdragon(sd) )
		script_pushint(st, 1);
	else
	script_pushint(st, 0);

	return 0;
}

/// Sets if the player is on a dragon.
/// <flag> defaults to 1
///
/// setdragon <flag>;
/// setdragon;
BUILDIN_FUNC(setdragon)
{
	int flag = 1;
	TBL_PC* sd;
	
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( script_hasdata(st,2) )
		flag = script_getnum(st,2);
	pc_setdragon(sd, flag);

	return 0;
}

/// Returns if the player has a warg.
///
/// checkwug() -> <bool>
BUILDIN_FUNC(checkwug)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( pc_iswug(sd) )
		script_pushint(st, 1);
	else
		script_pushint(st, 0);

	return 0;
}

/// Sets if the player has a warg or not.
/// <flag> defaults to 1
///
/// setwug <flag>;
/// setwug;
BUILDIN_FUNC(setwug)
{
	int flag = 1;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( script_hasdata(st,2) )
		flag = script_getnum(st,2);

	pc_setwug(sd, flag);

	return 0;
}

/// Returns if the player is on a warg.
///
/// checkwugrider() -> <bool>
BUILDIN_FUNC(checkwugrider)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( pc_iswugrider(sd) )
		script_pushint(st, 1);
	else
		script_pushint(st, 0);

	return 0;
}

/// Sets if the player is on a warg.
/// <flag> defaults to 1
///
/// setwugrider <flag>;
/// setwugrider;
BUILDIN_FUNC(setwugrider)
{
	int flag = 1;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( script_hasdata(st,2) )
		flag = script_getnum(st,2);
	pc_setwugrider(sd, flag);

	return 0;
}
	
/// Returns if the player is on a mado.
///
/// checkmadogear() -> <bool>
BUILDIN_FUNC(checkmadogear)
{
	TBL_PC* sd;
	
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source
	
	if( pc_ismadogear(sd) )
		script_pushint(st, 1);
	else
		script_pushint(st, 0);

	return 0;
}

/// Sets if the player is on a mado.
/// <flag> defaults to 1
///
/// setmadogear <flag>;
/// setmadogear;
BUILDIN_FUNC(setmadogear)
{
	int flag = 1;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	if( script_hasdata(st,2) )
		flag = script_getnum(st,2);
	pc_setmadogear(sd, flag);

	return 0;
}

/// Sets the save point of the player.
///
/// save "<map name>",<x>,<y>
/// savepoint "<map name>",<x>,<y>
BUILDIN_FUNC(savepoint)
{
	int x;
	int y;
	short map;
	const char* str;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;// no player attached, report source

	str = script_getstr(st, 2);
	x   = script_getnum(st,3);
	y   = script_getnum(st,4);
	map = mapindex_name2id(str);
	if( map )
		pc_setsavepoint(sd, map, x, y);

	return 0;
}

/*==========================================
 * GetTimeTick(0: System Tick, 1: Time Second Tick)
 *------------------------------------------*/
BUILDIN_FUNC(gettimetick)	/* Asgard Version */
{
	int type;
	time_t timer;
	struct tm *t;

	type=script_getnum(st,2);

	switch(type){
	case 2: 
		//type 2:(Get the number of seconds elapsed since 00:00 hours, Jan 1, 1970 UTC
		//        from the system clock.)
		script_pushint(st,(int)time(NULL));
		break;
	case 1:
		//type 1:(Second Ticks: 0-86399, 00:00:00-23:59:59)
		time(&timer);
		t=localtime(&timer);
		script_pushint(st,((t->tm_hour)*3600+(t->tm_min)*60+t->tm_sec));
		break;
	case 0:
	default:
		//type 0:(System Ticks)
		script_pushint(st,(int)gettick());
		break;
	}
	return 0;
}

/*==========================================
 * GetTime(Type);
 * 1: Sec     2: Min     3: Hour
 * 4: WeekDay     5: MonthDay     6: Month
 * 7: Year
 *------------------------------------------*/
BUILDIN_FUNC(gettime)	/* Asgard Version */
{
	int type;
	time_t timer;
	struct tm *t;

	type=script_getnum(st,2);

	time(&timer);
	t=localtime(&timer);

	switch(type){
	case 1://Sec(0~59)
		script_pushint(st,t->tm_sec);
		break;
	case 2://Min(0~59)
		script_pushint(st,t->tm_min);
		break;
	case 3://Hour(0~23)
		script_pushint(st,t->tm_hour);
		break;
	case 4://WeekDay(0~6)
		script_pushint(st,t->tm_wday);
		break;
	case 5://MonthDay(01~31)
		script_pushint(st,t->tm_mday);
		break;
	case 6://Month(01~12)
		script_pushint(st,t->tm_mon+1);
		break;
	case 7://Year(20xx)
		script_pushint(st,t->tm_year+1900);
		break;
	case 8://Year Day(01~366)
		script_pushint(st,t->tm_yday+1);
		break;
	default://(format error)
		script_pushint(st,-1);
		break;
	}
	return 0;
}

/*==========================================
 * GetTimeStr("TimeFMT", Length);
 *------------------------------------------*/
BUILDIN_FUNC(gettimestr)
{
	char *tmpstr;
	const char *fmtstr;
	int maxlen;
	time_t now = time(NULL);

	fmtstr=script_getstr(st,2);
	maxlen=script_getnum(st,3);

	tmpstr=(char *)aMallocA((maxlen+1)*sizeof(char));
	strftime(tmpstr,maxlen,fmtstr,localtime(&now));
	tmpstr[maxlen]='\0';

	script_pushstr(st,tmpstr);
	return 0;
}

/*==========================================
 * �J�v���q�ɂ��J��
 *------------------------------------------*/
BUILDIN_FUNC(openstorage)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	storage_storageopen(sd);
	return 0;
}

BUILDIN_FUNC(guildopenstorage)
{
	TBL_PC* sd;
	int ret;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	ret = storage_guild_storageopen(sd);
	script_pushint(st,ret);
	return 0;
}

/*==========================================
 * �A�C�e���ɂ��X�L������
 *------------------------------------------*/
/// itemskill <skill id>,<level>
/// itemskill "<skill name>",<level>
BUILDIN_FUNC(itemskill)
{
	int id;
	int lv;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL || sd->ud.skilltimer != INVALID_TIMER )
		return 0;

	id = ( script_isstring(st,2) ? skill_name2id(script_getstr(st,2)) : script_getnum(st,2) );
	lv = script_getnum(st,3);

	sd->skillitem=id;
	sd->skillitemlv=lv;
	clif_item_skill(sd,id,lv);
	return 0;
}
/*==========================================
 * �A�C�e���쐬
 *------------------------------------------*/
BUILDIN_FUNC(produce)
{
	int trigger;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	trigger=script_getnum(st,2);
	clif_skill_produce_mix_list(sd,-1,trigger);
	return 0;
}
/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(cooking)
{
	int trigger;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	trigger=script_getnum(st,2);
	clif_cooking_list(sd, trigger, AM_PHARMACY, 1, 1);
	return 0;
}
/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(makerune)
{
	int rune_ore;
	TBL_PC* sd;

	sd = script_rid2sd(st);

	if( sd == NULL )
		return 0;

	rune_ore=script_getnum(st,2);
	sd->menuskill_itemused = rune_ore;
	clif_skill_produce_mix_list(sd,RK_RUNEMASTERY,24);
	return 0;

}

/**
 * hascashmount() returns 1 if mounting a cash mount or 0 otherwise
 **/
BUILDIN_FUNC(hascashmount)
{
	struct map_session_data *sd = script_rid2sd(st);

	if (sd == NULL)
		return true;

	if (sd->sc.data[SC_ALL_RIDING]) {
		script_pushint(st, 1);
	} else {
		script_pushint(st, 0);
	}

	return true;
}

/**
 * setcashmount() returns 1 on success or 0 otherwise
 *
 * - Toggles cash mounts on a player when he can mount
 * - Will fail if the player is already riding a standard mount e.g. dragon, peco, wug, mado, etc.
 * - Will unmount the player is he is already mounting a cash mount
 **/
BUILDIN_FUNC(setcashmount)
{
	struct map_session_data *sd = script_rid2sd(st);

	if (sd == NULL)
		return true;

	if (pc_hasmount(sd)) {
		clif_msgtable(sd->fd, 1931);
		script_pushint(st, 0); // Can't mount with one of these
	} else {
		if (sd->sc.data[SC_ALL_RIDING]) {
			status_change_end(&sd->bl, SC_ALL_RIDING, INVALID_TIMER);
		} else {
			sc_start(&sd->bl, SC_ALL_RIDING, 100, battle_config.rental_mount_speed_boost, -1);
		}
		script_pushint(st, 1); // In both cases, return 1.
	}

	return true;
}

/*==========================================
 * NPC�Ńy�b�g���
 *------------------------------------------*/
BUILDIN_FUNC(makepet)
{
	TBL_PC* sd;
	int id,pet_id;

	id=script_getnum(st,2);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	pet_id = search_petDB_index(id, PET_CLASS);

	if (pet_id < 0)
		pet_id = search_petDB_index(id, PET_EGG);
	if (pet_id >= 0 && sd) {
		sd->catch_target_class = pet_db[pet_id].class_;
		intif_create_pet(
			sd->status.account_id, sd->status.char_id,
			(short)pet_db[pet_id].class_, (short)mob_db(pet_db[pet_id].class_)->lv,
			(short)pet_db[pet_id].EggID, 0, (short)pet_db[pet_id].intimate,
			100, 0, 1, pet_db[pet_id].jname);
	}

	return 0;
}
/*==========================================
 * NPC�Ōo���l�グ��
 *------------------------------------------*/
BUILDIN_FUNC(getexp)
{
	TBL_PC* sd;
	int base=0,job=0;
	double bonus;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	base=script_getnum(st,2);
	job =script_getnum(st,3);
	if(base<0 || job<0)
		return 0;

	// bonus for npc-given exp
	bonus = battle_config.quest_exp_rate / 100.;
	base = (int) cap_value(base * bonus, 0, INT_MAX);
	job = (int) cap_value(job * bonus, 0, INT_MAX);

	pc_gainexp(sd, NULL, base, job, true);

	return 0;
}

/*==========================================
 * Gain guild exp [Celest]
 *------------------------------------------*/
BUILDIN_FUNC(guildgetexp)
{
	TBL_PC* sd = script_rid2sd(st);
	int exp = script_getnum(st,2);

	if( exp < 0 || sd == NULL || sd->status.guild_id == 0 )
		return 0;

	guild_addexp(sd->status.guild_id, sd->status.account_id, sd->status.char_id, exp);
	return 0;
}

/*==========================================
 * Changes the guild master of a guild [Skotlex]
 *------------------------------------------*/
BUILDIN_FUNC(guildchangegm)
{
	TBL_PC *sd;
	int guild_id;
	const char *name;

	guild_id = script_getnum(st,2);
	name = script_getstr(st,3);
	sd=map_nick2sd(name);

	if (!sd)
		script_pushint(st,0);
	else
		script_pushint(st,guild_gm_change(guild_id, sd));

	return 0;
}

/*==========================================
 * Spawn a monster:
 * *monster "<map name>",<x>,<y>,"<name to show>",<mob id>,<amount>{,"<event label>",<size>,<ai>};
 *------------------------------------------*/
BUILDIN_FUNC(monster)
{
	const char* mapn  = script_getstr(st,2);
	int x             = script_getnum(st,3);
	int y             = script_getnum(st,4);
	const char* str   = script_getstr(st,5);
	int class_        = script_getnum(st,6);
	int amount        = script_getnum(st,7);
	const char* event = "";

	struct map_session_data* sd;
	int m,i;


	if( script_hasdata(st,8) )
	{
		event = script_getstr(st,8);
		check_event(st, event);
	}

	if (class_ >= 0 && !mobdb_checkid(class_)) {
		ShowWarning("buildin_monster: Attempted to spawn non-existing monster class %d\n", class_);
		return 1;
	}

	sd = map_id2sd(st->rid);

	if( sd && strcmp(mapn,"this") == 0 )
		m = sd->bl.m;
	else
	{
		m = map_mapname2mapid(mapn);
		if( map[m].flag.src4instance && st->instance_id )
		{ // Try to redirect to the instance map, not the src map
			if( (m = instance_mapid2imapid(m, st->instance_id)) < 0 )
			{
				ShowError("buildin_monster: Trying to spawn monster (%d) on instance map (%s) without instance attached.\n", class_, mapn);
				return 1;
			}
		}
	}

	for(i = 0; i < amount; i++) { //not optimised
		int mobid = mob_once_spawn(sd,m,x,y,str,class_,1,event);
		if (mobid)
			mapreg_setreg(reference_uid(add_str("$@mobid"), i), mobid);
	}

	return 0;
}
/*==========================================
 * Request List of Monster Drops
 *------------------------------------------*/
BUILDIN_FUNC(getmobdrops)
{
	int class_ = script_getnum(st,2);
	int i, j = 0;
	struct mob_db *mob;

	if( !mobdb_checkid(class_) )
	{
		script_pushint(st, 0);
		return 0;
	}

	mob = mob_db(class_);

	for( i = 0; i < MAX_MOB_DROP; i++ )
	{
		if( mob->dropitem[i].nameid < 1 )
			continue;
		if( itemdb_exists(mob->dropitem[i].nameid) == NULL )
			continue;

		mapreg_setreg(reference_uid(add_str("$@MobDrop_item"), j), mob->dropitem[i].nameid);
		mapreg_setreg(reference_uid(add_str("$@MobDrop_rate"), j), mob->dropitem[i].p);

		j++;
	}

	mapreg_setreg(add_str("$@MobDrop_count"), j);
	script_pushint(st, 1);

	return 0;
}
/*==========================================
 * Spawn a monster in a random location
 * in x0,x1,y0,y1 area.
 *------------------------------------------*/
BUILDIN_FUNC(areamonster)
{
	const char* mapn  = script_getstr(st,2);
	int x0            = script_getnum(st,3);
	int y0            = script_getnum(st,4);
	int x1            = script_getnum(st,5);
	int y1            = script_getnum(st,6);
	const char* str   = script_getstr(st,7);
	int class_        = script_getnum(st,8);
	int amount        = script_getnum(st,9);
	const char* event = "";

	struct map_session_data* sd;
	int m,i;

	if( script_hasdata(st,10) )
	{
		event = script_getstr(st,10);
		check_event(st, event);
	}

	if (class_ >= 0 && !mobdb_checkid(class_)) {
		ShowWarning("buildin_monster: Attempted to spawn non-existing monster class %d\n", class_);
		return 1;
	}

	sd = map_id2sd(st->rid);

	if( sd && strcmp(mapn,"this") == 0 )
		m = sd->bl.m;
	else
	{
		m = map_mapname2mapid(mapn);
		if( map[m].flag.src4instance && st->instance_id  >= 0 )
		{ // Try to redirect to the instance map, not the src map
			if( (m = instance_mapid2imapid(m, st->instance_id)) < 0 )
			{
				ShowError("buildin_areamonster: Trying to spawn monster (%d) on instance map (%s) without instance attached.\n", class_, mapn);
				return 1;
			}
		}
	}
	
	for(i = 0; i < amount; i++) { //not optimised
		int mobid = mob_once_spawn_area(sd, m, x0, y0, x1, y1, str, class_, 1, event);

		if (mobid)
			mapreg_setreg(reference_uid(add_str("$@mobid"), i), mobid);
	}
	return 0;
}

/*==========================================
 * �����X�^�[�폜
 *------------------------------------------*/
 static int buildin_killmonster_sub_strip(struct block_list *bl,va_list ap)
{ //same fix but with killmonster instead - stripping events from mobs.
	TBL_MOB* md = (TBL_MOB*)bl;
	char *event=va_arg(ap,char *);
	int allflag=va_arg(ap,int);

	md->state.npc_killmonster = 1;
	
	if(!allflag){
		if(strcmp(event,md->npc_event)==0)
			status_kill(bl);
	}else{
		if(!md->spawn)
			status_kill(bl);
	}
	md->state.npc_killmonster = 0;
	return 0;
}
static int buildin_killmonster_sub(struct block_list *bl,va_list ap)
{
	TBL_MOB* md = (TBL_MOB*)bl;
	char *event=va_arg(ap,char *);
	int allflag=va_arg(ap,int);

	if(!allflag){
		if(strcmp(event,md->npc_event)==0)
			status_kill(bl);
	}else{
		if(!md->spawn)
			status_kill(bl);
	}
	return 0;
}
BUILDIN_FUNC(killmonster)
{
	const char *mapname,*event;
	int m,allflag=0;
	mapname=script_getstr(st,2);
	event=script_getstr(st,3);
	if(strcmp(event,"All")==0)
		allflag = 1;
	else
		check_event(st, event);

	if( (m=map_mapname2mapid(mapname))<0 )
		return 0;
		
	if( map[m].flag.src4instance && st->instance_id && (m = instance_mapid2imapid(m, st->instance_id)) < 0 )
		return 0;

	if( script_hasdata(st,4) ) {
		if ( script_getnum(st,4) == 1 ) {
			map_foreachinmap(buildin_killmonster_sub, m, BL_MOB, event ,allflag);
			return 0;
		}
	}
	
	map_freeblock_lock();
	map_foreachinmap(buildin_killmonster_sub_strip, m, BL_MOB, event ,allflag);
	map_freeblock_unlock();
	return 0;
}

static int buildin_killmonsterall_sub_strip(struct block_list *bl,va_list ap)
{ //Strips the event from the mob if it's killed the old method.
	struct mob_data *md;
	
	md = BL_CAST(BL_MOB, bl);
	if (md->npc_event[0])
		md->npc_event[0] = 0;
		
	status_kill(bl);
	return 0;
}
static int buildin_killmonsterall_sub(struct block_list *bl,va_list ap)
{
	status_kill(bl);
	return 0;
}
BUILDIN_FUNC(killmonsterall)
{
	const char *mapname;
	int m;
	mapname=script_getstr(st,2);
	
	if( (m = map_mapname2mapid(mapname))<0 )
		return 0;

	if( map[m].flag.src4instance && st->instance_id && (m = instance_mapid2imapid(m, st->instance_id)) < 0 )
		return 0;

	if( script_hasdata(st,3) ) {
		if ( script_getnum(st,3) == 1 ) {
			map_foreachinmap(buildin_killmonsterall_sub,m,BL_MOB);
			return 0;
		}
	}
		
	map_foreachinmap(buildin_killmonsterall_sub_strip,m,BL_MOB);
	return 0;
}

/*==========================================
 * Creates a clone of a player.
 * clone map, x, y, event, char_id, master_id, mode, flag, duration
 *------------------------------------------*/
BUILDIN_FUNC(clone)
{
	TBL_PC *sd, *msd=NULL;
	int char_id,master_id=0,x,y, mode = 0, flag = 0, m;
	unsigned int duration = 0;
	const char *map,*event="";

	map=script_getstr(st,2);
	x=script_getnum(st,3);
	y=script_getnum(st,4);
	event=script_getstr(st,5);
	char_id=script_getnum(st,6);

	if( script_hasdata(st,7) )
		master_id=script_getnum(st,7);

	if( script_hasdata(st,8) )
		mode=script_getnum(st,8);

	if( script_hasdata(st,9) )
		flag=script_getnum(st,9);
	
	if( script_hasdata(st,10) )
		duration=script_getnum(st,10);

	check_event(st, event);

	m = map_mapname2mapid(map);
	if (m < 0) return 0;

	sd = map_charid2sd(char_id);

	if (master_id) {
		msd = map_charid2sd(master_id);
		if (msd)
			master_id = msd->bl.id;
		else
			master_id = 0;
	}
	if (sd) //Return ID of newly crafted clone.
		script_pushint(st,mob_clone_spawn(sd, m, x, y, event, master_id, mode, flag, 1000*duration));
	else //Failed to create clone.
		script_pushint(st,0);

	return 0;
}
/*==========================================
 * �C�x���g���s
 *------------------------------------------*/
BUILDIN_FUNC(doevent)
{
	const char* event = script_getstr(st,2);
	struct map_session_data* sd;

	if( ( sd = script_rid2sd(st) ) == NULL )
	{
		return 0;
	}

	check_event(st, event);
	npc_event(sd, event, 0);
	return 0;
}
/*==========================================
 * NPC��̃C�x���g���s
 *------------------------------------------*/
BUILDIN_FUNC(donpcevent)
{
	const char* event = script_getstr(st,2);
	check_event(st, event);
	npc_event_do(event);
	return 0;
}

/// for Aegis compatibility
/// basically a specialized 'donpcevent', with the event specified as two arguments instead of one
BUILDIN_FUNC(cmdothernpc)	// Added by RoVeRT
{
	const char* npc = script_getstr(st,2);
	const char* command = script_getstr(st,3);
	char event[EVENT_NAME_LENGTH];
	snprintf(event, sizeof(event), "%s::OnCommand%s", npc, command);
	check_event(st, event);
	npc_event_do(event);
	return 0;
}

/*==========================================
 * �C�x���g�^�C�}�[�ǉ�
 *------------------------------------------*/
BUILDIN_FUNC(addtimer)
{
	int tick = script_getnum(st,2);
	const char* event = script_getstr(st, 3);
	TBL_PC* sd;

	check_event(st, event);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	pc_addeventtimer(sd,tick,event);
	return 0;
}
/*==========================================
 * �C�x���g�^�C�}�[�폜
 *------------------------------------------*/
BUILDIN_FUNC(deltimer)
{
	const char *event;
	TBL_PC* sd;

	event=script_getstr(st, 2);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	check_event(st, event);
	pc_deleventtimer(sd,event);
	return 0;
}
/*==========================================
 * �C�x���g�^�C�}�[�̃J�E���g�l�ǉ�
 *------------------------------------------*/
BUILDIN_FUNC(addtimercount)
{
	const char *event;
	int tick;
	TBL_PC* sd;

	event=script_getstr(st, 2);
	tick=script_getnum(st,3);
	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	check_event(st, event);
	pc_addeventtimercount(sd,event,tick);
	return 0;
}

/*==========================================
 * NPC�^�C�}�[������
 *------------------------------------------*/
BUILDIN_FUNC(initnpctimer)
{
	struct npc_data *nd;
	int flag = 0;

	if( script_hasdata(st,3) )
	{	//Two arguments: NPC name and attach flag.
		nd = npc_name2id(script_getstr(st, 2));
		flag = script_getnum(st,3);
	}
	else if( script_hasdata(st,2) )
	{	//Check if argument is numeric (flag) or string (npc name)
		struct script_data *data;
		data = script_getdata(st,2);
		get_val(st,data);
		if( data_isstring(data) ) //NPC name
			nd = npc_name2id(conv_str(st, data));
		else if( data_isint(data) ) //Flag
		{
			nd = (struct npc_data *)map_id2bl(st->oid);
			flag = conv_num(st,data);
		}
		else
		{
			ShowError("initnpctimer: invalid argument type #1 (needs be int or string)).\n");
			return 1;
		}
	}
	else
		nd = (struct npc_data *)map_id2bl(st->oid);

	if( !nd )
		return 0;
	if( flag ) //Attach
	{
		TBL_PC* sd = script_rid2sd(st);
		if( sd == NULL )
			return 0;
		nd->u.scr.rid = sd->bl.id;
	}

	npc_settimerevent_tick(nd,0);
	npc_timerevent_start(nd, st->rid);
	return 0;
}
/*==========================================
 * NPC�^�C�}�[�J�n
 *------------------------------------------*/
BUILDIN_FUNC(startnpctimer)
{
	struct npc_data *nd;
	int flag = 0;

	if( script_hasdata(st,3) )
	{	//Two arguments: NPC name and attach flag.
		nd = npc_name2id(script_getstr(st, 2));
		flag = script_getnum(st,3);
	}
	else if( script_hasdata(st,2) )
	{	//Check if argument is numeric (flag) or string (npc name)
		struct script_data *data;
		data = script_getdata(st,2);
		get_val(st,data);
		if( data_isstring(data) ) //NPC name
			nd = npc_name2id(conv_str(st, data));
		else if( data_isint(data) ) //Flag
		{
			nd = (struct npc_data *)map_id2bl(st->oid);
			flag = conv_num(st,data);
		}
		else
		{
			ShowError("initnpctimer: invalid argument type #1 (needs be int or string)).\n");
			return 1;
		}
	}
	else
		nd=(struct npc_data *)map_id2bl(st->oid);

	if( !nd )
		return 0;
	if( flag ) //Attach
	{
		TBL_PC* sd = script_rid2sd(st);
		if( sd == NULL )
			return 0;
		nd->u.scr.rid = sd->bl.id;
	}

	npc_timerevent_start(nd, st->rid);
	return 0;
}
/*==========================================
 * NPC�^�C�}�[��~
 *------------------------------------------*/
BUILDIN_FUNC(stopnpctimer)
{
	struct npc_data *nd;
	int flag = 0;

	if( script_hasdata(st,3) )
	{	//Two arguments: NPC name and attach flag.
		nd = npc_name2id(script_getstr(st, 2));
		flag = script_getnum(st,3);
	}
	else if( script_hasdata(st,2) )
	{	//Check if argument is numeric (flag) or string (npc name)
		struct script_data *data;
		data = script_getdata(st,2);
		get_val(st,data);
		if( data_isstring(data) ) //NPC name
			nd = npc_name2id(conv_str(st, data));
		else if( data_isint(data) ) //Flag
		{
			nd = (struct npc_data *)map_id2bl(st->oid);
			flag = conv_num(st,data);
		}
		else
		{
			ShowError("initnpctimer: invalid argument type #1 (needs be int or string)).\n");
			return 1;
		}
	}
	else
		nd=(struct npc_data *)map_id2bl(st->oid);

	if( !nd )
		return 0;
	if( flag ) //Detach
		nd->u.scr.rid = 0;

	npc_timerevent_stop(nd);
	return 0;
}
/*==========================================
 * NPC�^�C�}�[��񏊓�
 *------------------------------------------*/
BUILDIN_FUNC(getnpctimer)
{
	struct npc_data *nd;
	TBL_PC *sd;
	int type = script_getnum(st,2);
	int val = 0;

	if( script_hasdata(st,3) )
		nd = npc_name2id(script_getstr(st,3));
	else
		nd = (struct npc_data *)map_id2bl(st->oid);
	
	if( !nd || nd->bl.type != BL_NPC )
	{
		script_pushint(st,0);
		ShowError("getnpctimer: Invalid NPC.\n");
		return 1;
	}

	switch( type )
	{
	case 0: val = (int)npc_gettimerevent_tick(nd); break;
	case 1:
		if( nd->u.scr.rid )
		{
			sd = map_id2sd(nd->u.scr.rid);
			if( !sd )
			{
				ShowError("buildin_getnpctimer: Attached player not found!\n");
				break;
			}
			val = (sd->npc_timer_id != INVALID_TIMER);
		}
		else
			val = (nd->u.scr.timerid != INVALID_TIMER);
		break;
	case 2: val = nd->u.scr.timeramount; break;
	}

	script_pushint(st,val);
	return 0;
}
/*==========================================
 * NPC�^�C�}�[�l�ݒ�
 *------------------------------------------*/
BUILDIN_FUNC(setnpctimer)
{
	int tick;
	struct npc_data *nd;

	tick = script_getnum(st,2);
	if( script_hasdata(st,3) )
		nd = npc_name2id(script_getstr(st,3));
	else
		nd = (struct npc_data *)map_id2bl(st->oid);

	if( !nd || nd->bl.type != BL_NPC )
	{
		script_pushint(st,1);
		ShowError("setnpctimer: Invalid NPC.\n");
		return 1;
	}

	npc_settimerevent_tick(nd,tick);
	script_pushint(st,0);
	return 0;
}

/*==========================================
 * attaches the player rid to the timer [Celest]
 *------------------------------------------*/
BUILDIN_FUNC(attachnpctimer)
{
	TBL_PC *sd;
	struct npc_data *nd = (struct npc_data *)map_id2bl(st->oid);

	if( !nd || nd->bl.type != BL_NPC )
	{
		script_pushint(st,1);
		ShowError("setnpctimer: Invalid NPC.\n");
		return 1;
	}

	if( script_hasdata(st,2) )
		sd = map_nick2sd(script_getstr(st,2));
	else
		sd = script_rid2sd(st);

	if( !sd )
	{
		script_pushint(st,1);
		ShowWarning("attachnpctimer: Invalid player.\n");
		return 1;
	}

	nd->u.scr.rid = sd->bl.id;
	script_pushint(st,0);
	return 0;
}

/*==========================================
 * detaches a player rid from the timer [Celest]
 *------------------------------------------*/
BUILDIN_FUNC(detachnpctimer)
{
	struct npc_data *nd;

	if( script_hasdata(st,2) )
		nd = npc_name2id(script_getstr(st,2));
	else
		nd = (struct npc_data *)map_id2bl(st->oid);

	if( !nd || nd->bl.type != BL_NPC )
	{
		script_pushint(st,1);
		ShowError("detachnpctimer: Invalid NPC.\n");
		return 1;
	}

	nd->u.scr.rid = 0;
	script_pushint(st,0);
	return 0;
}

/*==========================================
 * To avoid "player not attached" script errors, this function is provided,
 * it checks if there is a player attached to the current script. [Skotlex]
 * If no, returns 0, if yes, returns the account_id of the attached player.
 *------------------------------------------*/
BUILDIN_FUNC(playerattached)
{
	if(st->rid == 0 || map_id2sd(st->rid) == NULL)
		script_pushint(st,0);
	else
		script_pushint(st,st->rid);
	return 0;
}

/*==========================================
 * �V�̐��A�i�E���X
 *------------------------------------------*/
BUILDIN_FUNC(announce)
{
	const char *mes       = script_getstr(st,2);
	int         flag      = script_getnum(st,3);
	const char *fontColor = script_hasdata(st,4) ? script_getstr(st,4) : NULL;
	int         fontType  = script_hasdata(st,5) ? script_getnum(st,5) : 0x190; // default fontType (FW_NORMAL)
	int         fontSize  = script_hasdata(st,6) ? script_getnum(st,6) : 12;    // default fontSize
	int         fontAlign = script_hasdata(st,7) ? script_getnum(st,7) : 0;     // default fontAlign
	int         fontY     = script_hasdata(st,8) ? script_getnum(st,8) : 0;     // default fontY
	
	if (flag&0x0f) // Broadcast source or broadcast region defined
	{
		send_target target;
		struct block_list *bl = (flag&0x08) ? map_id2bl(st->oid) : (struct block_list *)script_rid2sd(st); // If bc_npc flag is set, use NPC as broadcast source
		if (bl == NULL)
			return 0;
		
		flag &= 0x07;
		target = (flag == 1) ? ALL_SAMEMAP :
		         (flag == 2) ? AREA :
		         (flag == 3) ? SELF :
		                       ALL_CLIENT;
		if (fontColor)
			clif_broadcast2(bl, mes, (int)strlen(mes)+1, strtol(fontColor, (char **)NULL, 0), fontType, fontSize, fontAlign, fontY, target);
		else
			clif_broadcast(bl, mes, (int)strlen(mes)+1, flag&0xf0, target);
	}
	else
	{
		if (fontColor)
			intif_broadcast2(mes, (int)strlen(mes)+1, strtol(fontColor, (char **)NULL, 0), fontType, fontSize, fontAlign, fontY);
		else
			intif_broadcast(mes, (int)strlen(mes)+1, flag&0xf0);
	}
	return 0;
}
/*==========================================
 * �V�̐��A�i�E���X�i����}�b�v�j
 *------------------------------------------*/
static int buildin_announce_sub(struct block_list *bl, va_list ap)
{
	char *mes       = va_arg(ap, char *);
	int   len       = va_arg(ap, int);
	int   type      = va_arg(ap, int);
	char *fontColor = va_arg(ap, char *);
	short fontType  = (short)va_arg(ap, int);
	short fontSize  = (short)va_arg(ap, int);
	short fontAlign = (short)va_arg(ap, int);
	short fontY     = (short)va_arg(ap, int);
	if (fontColor)
		clif_broadcast2(bl, mes, len, strtol(fontColor, (char **)NULL, 0), fontType, fontSize, fontAlign, fontY, SELF);
	else
		clif_broadcast(bl, mes, len, type, SELF);
	return 0;
}

BUILDIN_FUNC(mapannounce)
{
	const char *mapname   = script_getstr(st,2);
	const char *mes       = script_getstr(st,3);
	int         flag      = script_getnum(st,4);
	const char *fontColor = script_hasdata(st,5) ? script_getstr(st,5) : NULL;
	int         fontType  = script_hasdata(st,6) ? script_getnum(st,6) : 0x190; // default fontType (FW_NORMAL)
	int         fontSize  = script_hasdata(st,7) ? script_getnum(st,7) : 12;    // default fontSize
	int         fontAlign = script_hasdata(st,8) ? script_getnum(st,8) : 0;     // default fontAlign
	int         fontY     = script_hasdata(st,9) ? script_getnum(st,9) : 0;     // default fontY
	int m;

	if ((m = map_mapname2mapid(mapname)) < 0)
		return 0;

	map_foreachinmap(buildin_announce_sub, m, BL_PC,
			mes, strlen(mes)+1, flag&0xf0, fontColor, fontType, fontSize, fontAlign, fontY);
	return 0;
}
/*==========================================
 * �V�̐��A�i�E���X�i����G���A�j
 *------------------------------------------*/
BUILDIN_FUNC(areaannounce)
{
	const char *mapname   = script_getstr(st,2);
	int         x0        = script_getnum(st,3);
	int         y0        = script_getnum(st,4);
	int         x1        = script_getnum(st,5);
	int         y1        = script_getnum(st,6);
	const char *mes       = script_getstr(st,7);
	int         flag      = script_getnum(st,8);
	const char *fontColor = script_hasdata(st,9) ? script_getstr(st,9) : NULL;
	int         fontType  = script_hasdata(st,10) ? script_getnum(st,10) : 0x190; // default fontType (FW_NORMAL)
	int         fontSize  = script_hasdata(st,11) ? script_getnum(st,11) : 12;    // default fontSize
	int         fontAlign = script_hasdata(st,12) ? script_getnum(st,12) : 0;     // default fontAlign
	int         fontY     = script_hasdata(st,13) ? script_getnum(st,13) : 0;     // default fontY
	int m;

	if ((m = map_mapname2mapid(mapname)) < 0)
		return 0;

	map_foreachinarea(buildin_announce_sub, m, x0, y0, x1, y1, BL_PC,
		mes, strlen(mes)+1, flag&0xf0, fontColor, fontType, fontSize, fontAlign, fontY);
	return 0;
}

/*==========================================
 * ���[�U�[������
 *------------------------------------------*/
BUILDIN_FUNC(getusers)
{
	int flag, val = 0;
	struct map_session_data* sd;
	struct block_list* bl = NULL;

	flag = script_getnum(st,2);

	switch(flag&0x07)
	{
		case 0:
			if(flag&0x8)
			{// npc
				bl = map_id2bl(st->oid);
			}
			else if((sd = script_rid2sd(st))!=NULL)
			{// pc
				bl = &sd->bl;
			}

			if(bl)
			{
				val = map[bl->m].users;
			}
			break;
		case 1:
			val = map_getusers();
			break;
		default:
			ShowWarning("buildin_getusers: Unknown type %d.\n", flag);
			script_pushint(st,0);
			return 1;
	}

	script_pushint(st,val);
	return 0;
}
/*==========================================
 * Works like @WHO - displays all online users names in window
 *------------------------------------------*/
BUILDIN_FUNC(getusersname)
{
	TBL_PC *sd, *pl_sd;
	int disp_num=1;
	struct s_mapiterator* iter;

	sd = script_rid2sd(st);
	if (!sd) return 0;

	iter = mapit_getallusers();
	for( pl_sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); pl_sd = (TBL_PC*)mapit_next(iter) )
	{
		if( battle_config.hide_GM_session && pc_isGM(pl_sd) )
			continue; // skip hidden GMs

		if((disp_num++)%10==0)
			clif_scriptnext(sd,st->oid);
		clif_scriptmes(sd,st->oid,pl_sd->status.name);
	}
	mapit_free(iter);
	
	return 0;
}
/*==========================================
 * getmapguildusers("mapname",guild ID) Returns the number guild members present on a map [Reddozen]
 *------------------------------------------*/
BUILDIN_FUNC(getmapguildusers)
{
	const char *str;
	int m, gid;
	int i=0,c=0;
	struct guild *g = NULL;
	str=script_getstr(st,2);
	gid=script_getnum(st,3);
	if ((m = map_mapname2mapid(str)) < 0) { // map id on this server (m == -1 if not in actual map-server)
		script_pushint(st,-1);
		return 0;
	}
	g = guild_search(gid);

	if (g){
		for(i = 0; i < g->max_member; i++)
		{
			if (g->member[i].sd && g->member[i].sd->bl.m == m)
				c++;
		}
	}

	script_pushint(st,c);
	return 0;
}
/*==========================================
 * �}�b�v�w�胆�[�U�[������
 *------------------------------------------*/
BUILDIN_FUNC(getmapusers)
{
	const char *str;
	int m;
	str=script_getstr(st,2);
	if( (m=map_mapname2mapid(str))< 0){
		script_pushint(st,-1);
		return 0;
	}
	script_pushint(st,map[m].users);
	return 0;
}
/*==========================================
 * �G���A�w�胆�[�U�[������
 *------------------------------------------*/
static int buildin_getareausers_sub(struct block_list *bl,va_list ap)
{
	int *users=va_arg(ap,int *);
	(*users)++;
	return 0;
}
BUILDIN_FUNC(getareausers)
{
	const char *str;
	int m,x0,y0,x1,y1,users=0;
	str=script_getstr(st,2);
	x0=script_getnum(st,3);
	y0=script_getnum(st,4);
	x1=script_getnum(st,5);
	y1=script_getnum(st,6);
	if( (m=map_mapname2mapid(str))< 0){
		script_pushint(st,-1);
		return 0;
	}
	map_foreachinarea(buildin_getareausers_sub,
		m,x0,y0,x1,y1,BL_PC,&users);
	script_pushint(st,users);
	return 0;
}

/*==========================================
 * �G���A�w��h���b�v�A�C�e��������
 *------------------------------------------*/
static int buildin_getareadropitem_sub(struct block_list *bl,va_list ap) {
	unsigned short item = va_arg(ap, unsigned short);
	unsigned short *amount = va_arg(ap, unsigned short *);
	struct flooritem_data *drop=(struct flooritem_data *)bl;

	if(drop->item_data.nameid==item)
		(*amount)+=drop->item_data.amount;

	return 0;
}

BUILDIN_FUNC(getareadropitem) {
	const char *str;
	int16 m,x0,y0,x1,y1=0;
	unsigned short item, amount = 0;
	struct script_data *data;

	str=script_getstr(st,2);
	x0=script_getnum(st,3);
	y0=script_getnum(st,4);
	x1=script_getnum(st,5);
	y1=script_getnum(st,6);

	data=script_getdata(st,7);
	get_val(st,data);
	if( data_isstring(data) ) {
		const char *name=conv_str(st,data);
		struct item_data *item_data = itemdb_searchname(name);
		item=UNKNOWN_ITEM_ID;
		if( item_data )
			item=item_data->nameid;
	} else
		item=conv_num(st,data);

	if( (m=map_mapname2mapid(str))< 0) {
		script_pushint(st,-1);
		return 0;
	}
	map_foreachinarea(buildin_getareadropitem_sub,
		m,x0,y0,x1,y1,BL_ITEM,item,&amount);
	script_pushint(st,amount);
	return 0;
}

/*==========================================
 * NPC�̗L����
 *------------------------------------------*/
BUILDIN_FUNC(enablenpc)
{
	const char *str = script_getstr(st, 2);
	if (npc_enable(str, 1))
		return 0;

	return 1;
}
/*==========================================
 * NPC�̖�����
 *------------------------------------------*/
BUILDIN_FUNC(disablenpc)
{
	const char *str = script_getstr(st, 2);
	if (npc_enable(str, 0))
		return 0;

	return 1;
}

/*==========================================
 * �B��Ă���NPC�̕\��
 *------------------------------------------*/
BUILDIN_FUNC(hideoffnpc)
{
	const char *str = script_getstr(st, 2);
	if (npc_enable(str, 2))
		return 0;

	return 1;
}
/*==========================================
 * NPC���n�C�f�B���O
 *------------------------------------------*/
BUILDIN_FUNC(hideonnpc)
{
	const char *str = script_getstr(st, 2);
	if (npc_enable(str, 4))
		return 0;

	return 1;
}

/*==========================================
 *------------------------------------------*/
BUILDIN_FUNC(cloakonnpc)
{
	struct npc_data *nd = npc_name2id(script_getstr(st, 2));
	if (nd == NULL) {
		ShowError("buildin_cloakonnpc: invalid npc name '%s'.\n", script_getstr(st, 2));
		return 1;
	}

	if (script_hasdata(st, 3)) {
		struct map_session_data *sd = map_id2sd(script_getnum(st, 3));
		if (sd == NULL)
			return 1;

		uint32 val = nd->sc.option;
		nd->sc.option |= OPTION_CLOAK;
		clif_changeoption_target(&nd->bl, &sd->bl, SELF);
		nd->sc.option = val;
	}
	else {
		nd->sc.option |= OPTION_CLOAK;
		clif_changeoption(&nd->bl);
	}
	return 0;
}
/*==========================================
 *------------------------------------------*/
BUILDIN_FUNC(cloakoffnpc)
{
	struct npc_data *nd = npc_name2id(script_getstr(st, 2));
	if (nd == NULL) {
		ShowError("buildin_cloakoffnpc: invalid npc name '%s'.\n", script_getstr(st, 2));
		return 1;
	}

	if (script_hasdata(st, 3)) {
		struct map_session_data *sd = map_id2sd(script_getnum(st, 3));
		if (sd == NULL)
			return 1;

		uint32 val = nd->sc.option;
		nd->sc.option &= ~OPTION_CLOAK;
		clif_changeoption_target(&nd->bl, &sd->bl, SELF);
		nd->sc.option = val;
	}
	else {
		nd->sc.option &= ~OPTION_CLOAK;
		clif_changeoption(&nd->bl);
	}
	return 0;
}

/// Starts a status effect on the target unit or on the attached player.
///
/// sc_start <effect_id>,<duration>,<val1>{,<unit_id>};
BUILDIN_FUNC(sc_start)
{
	struct block_list* bl;
	enum sc_type type;
	int tick;
	int val1;
	int val4 = 0;

	type = (sc_type)script_getnum(st,2);
	tick = script_getnum(st,3);
	val1 = script_getnum(st,4);
	if( script_hasdata(st,5) )
		bl = map_id2bl(script_getnum(st,5));
	else
		bl = map_id2bl(st->rid);

	if( tick == 0 && val1 > 0 && type > SC_NONE && type < SC_MAX && status_sc2skill(type) != 0 )
	{// When there isn't a duration specified, try to get it from the skill_db
		tick = skill_get_time(status_sc2skill(type), val1);
	}

	if( potion_flag == 1 && potion_target )
	{	//skill.c set the flags before running the script, this must be a potion-pitched effect.
		bl = map_id2bl(potion_target);
		tick /= 2;// Thrown potions only last half.
		val4 = 1;// Mark that this was a thrown sc_effect
	}

	if( bl )
		status_change_start(bl, type, 10000, val1, 0, 0, val4, tick, 2);

	return 0;
}

/// Starts a status effect on the target unit or on the attached player.
///
/// sc_start2 <effect_id>,<duration>,<val1>,<percent chance>{,<unit_id>};
BUILDIN_FUNC(sc_start2)
{
	struct block_list* bl;
	enum sc_type type;
	int tick;
	int val1;
	int val4 = 0;
	int rate;

	type = (sc_type)script_getnum(st,2);
	tick = script_getnum(st,3);
	val1 = script_getnum(st,4);
	rate = script_getnum(st,5);
	if( script_hasdata(st,6) )
		bl = map_id2bl(script_getnum(st,6));
	else
		bl = map_id2bl(st->rid);

	if( tick == 0 && val1 > 0 && type > SC_NONE && type < SC_MAX && status_sc2skill(type) != 0 )
	{// When there isn't a duration specified, try to get it from the skill_db
		tick = skill_get_time(status_sc2skill(type), val1);
	}

	if( potion_flag == 1 && potion_target )
	{	//skill.c set the flags before running the script, this must be a potion-pitched effect.
		bl = map_id2bl(potion_target);
		tick /= 2;// Thrown potions only last half.
		val4 = 1;// Mark that this was a thrown sc_effect
	}

	if( bl )
		status_change_start(bl, type, rate, val1, 0, 0, val4, tick, 2);

	return 0;
}

/// Starts a status effect on the target unit or on the attached player.
///
/// sc_start4 <effect_id>,<duration>,<val1>,<val2>,<val3>,<val4>{,<unit_id>};
BUILDIN_FUNC(sc_start4)
{
	struct block_list* bl;
	enum sc_type type;
	int tick;
	int val1;
	int val2;
	int val3;
	int val4;

	type = (sc_type)script_getnum(st,2);
	tick = script_getnum(st,3);
	val1 = script_getnum(st,4);
	val2 = script_getnum(st,5);
	val3 = script_getnum(st,6);
	val4 = script_getnum(st,7);
	if( script_hasdata(st,8) )
		bl = map_id2bl(script_getnum(st,8));
	else
		bl = map_id2bl(st->rid);

	if( tick == 0 && val1 > 0 && type > SC_NONE && type < SC_MAX && status_sc2skill(type) != 0 )
	{// When there isn't a duration specified, try to get it from the skill_db
		tick = skill_get_time(status_sc2skill(type), val1);
	}

	if( potion_flag == 1 && potion_target )
	{	//skill.c set the flags before running the script, this must be a potion-pitched effect.
		bl = map_id2bl(potion_target);
		tick /= 2;// Thrown potions only last half.
	}

	if( bl )
		status_change_start(bl, type, 10000, val1, val2, val3, val4, tick, 2);

	return 0;
}

/// Ends one or all status effects on the target unit or on the attached player.
///
/// sc_end <effect_id>{,<unit_id>};
BUILDIN_FUNC(sc_end)
{
	struct block_list* bl;
	int type;

	type = script_getnum(st,2);
	if( script_hasdata(st,3) )
		bl = map_id2bl(script_getnum(st,3));
	else
		bl = map_id2bl(st->rid);
	
	if( potion_flag==1 && potion_target )
	{//##TODO how does this work [FlavioJS]
		bl = map_id2bl(potion_target);
	}

	if( !bl ) return 0;

	if( type >= 0 && type < SC_MAX )
	{
		struct status_change *sc = status_get_sc(bl);
		struct status_change_entry *sce = sc?sc->data[type]:NULL;
		if (!sce) return 0;
		//This should help status_change_end force disabling the SC in case it has no limit.
		sce->val1 = sce->val2 = sce->val3 = sce->val4 = 0;
		status_change_end(bl, (sc_type)type, INVALID_TIMER);
	} else
		status_change_clear(bl, 2);// remove all effects
	return 0;
}

/*==========================================
 * ��Ԉُ�ϐ����v�Z�����m����Ԃ�
 *------------------------------------------*/
BUILDIN_FUNC(getscrate)
{
	struct block_list *bl;
	int type,rate;

	type=script_getnum(st,2);
	rate=script_getnum(st,3);
	if( script_hasdata(st,4) ) //�w�肵���L�����̑ϐ����v�Z����
		bl = map_id2bl(script_getnum(st,4));
	else
		bl = map_id2bl(st->rid);

	if (bl)
		rate = status_get_sc_def(bl, (sc_type)type, 10000, 10000, 0);

	script_pushint(st,rate);
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(debugmes)
{
	const char *str;
	str=script_getstr(st,2);
	ShowDebug("script debug : %d %d : %s\n",st->rid,st->oid,str);
	return 0;
}

/*==========================================
 *�ߊl�A�C�e���g�p
 *------------------------------------------*/
BUILDIN_FUNC(catchpet)
{
	int pet_id;
	TBL_PC *sd;

	pet_id= script_getnum(st,2);
	sd=script_rid2sd(st);
	if( sd == NULL )
		return 0;

	pet_catch_process1(sd,pet_id);
	return 0;
}

/*==========================================
 * [orn]
 *------------------------------------------*/
BUILDIN_FUNC(homunculus_evolution)
{
	TBL_PC *sd;

	sd=script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if(merc_is_hom_active(sd->hd))
	{
		if (sd->hd->homunculus.intimacy > 91000)
			merc_hom_evolution(sd->hd);
		else
			clif_emotion(&sd->hd->bl, E_SWT);
	}
	return 0;
}

// [Rytech]
BUILDIN_FUNC(homunculus_mutation)
{
	TBL_PC *sd;

	sd=script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if(merc_is_hom_active(sd->hd))
		merc_hom_mutation(sd->hd, script_getnum(st,2));

	return 0;
}

// [Zephyrus]
BUILDIN_FUNC(homunculus_shuffle)
{
	TBL_PC *sd;

	sd=script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if(merc_is_hom_active(sd->hd))
		merc_hom_shuffle(sd->hd);

	return 0;
}

//These two functions bring the eA MAPID_* class functionality to scripts.
BUILDIN_FUNC(eaclass)
{
	int class_;
	if( script_hasdata(st,2) )
		class_ = script_getnum(st,2);
	else {
		TBL_PC *sd;
		sd=script_rid2sd(st);
		if (!sd) {
			script_pushint(st,-1);
			return 0;
		}
		class_ = sd->status.class_;
	}
	script_pushint(st,pc_jobid2mapid(class_));
	return 0;
}

BUILDIN_FUNC(roclass)
{
	int class_ =script_getnum(st,2);
	int sex;
	if( script_hasdata(st,3) )
		sex = script_getnum(st,3);
	else {
		TBL_PC *sd;
		if (st->rid && (sd=script_rid2sd(st)))
			sex = sd->status.sex;
		else
			sex = 1; //Just use male when not found.
	}
	script_pushint(st,pc_mapid2jobid(class_, sex));
	return 0;
}

/*==========================================
 *�g�ї��z���@�g�p
 *------------------------------------------*/
BUILDIN_FUNC(birthpet)
{
	TBL_PC *sd;
	sd=script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if( sd->status.pet_id )
	{// do not send egg list, when you already have a pet
		return 0;
	}

	clif_sendegg(sd);
	return 0;
}

/*==========================================
 * Added - AppleGirl For Advanced Classes, (Updated for Cleaner Script Purposes)
 *------------------------------------------*/
BUILDIN_FUNC(resetlvl)
{
	TBL_PC *sd;

	int type=script_getnum(st,2);

	sd=script_rid2sd(st);
	if( sd == NULL )
		return 0;

	pc_resetlvl(sd,type);
	return 0;
}
/*==========================================
 * �X�e�[�^�X���Z�b�g
 *------------------------------------------*/
BUILDIN_FUNC(resetstatus)
{
	TBL_PC *sd;
	sd=script_rid2sd(st);
	pc_resetstate(sd);
	return 0;
}

/*==========================================
 * script command resetskill
 *------------------------------------------*/
BUILDIN_FUNC(resetskill)
{
	TBL_PC *sd;
	sd=script_rid2sd(st);
	pc_resetskill(sd,1);
	return 0;
}

/*==========================================
 * Counts total amount of skill points.
 *------------------------------------------*/
BUILDIN_FUNC(skillpointcount)
{
	TBL_PC *sd;
	sd=script_rid2sd(st);
	script_pushint(st,sd->status.skill_point + pc_resetskill(sd,2));
	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(changebase)
{
	TBL_PC *sd=NULL;
	int vclass;

	if( script_hasdata(st,3) )
		sd=map_id2sd(script_getnum(st,3));
	else
	sd=script_rid2sd(st);

	if(sd == NULL)
		return 0;

	vclass = script_getnum(st,2);
	if(vclass == JOB_WEDDING)
	{
		if (!battle_config.wedding_modifydisplay || //Do not show the wedding sprites
			sd->class_&JOBL_BABY //Baby classes screw up when showing wedding sprites. [Skotlex] They don't seem to anymore.
			) 
		return 0;
	}

	if(!sd->disguise && vclass != sd->vd.class_) {
		status_set_viewdata(&sd->bl, vclass);
		//Updated client view. Base, Weapon and Cloth Colors.
		clif_changelook(&sd->bl,LOOK_BASE,sd->vd.class_);
		clif_changelook(&sd->bl,LOOK_WEAPON,sd->status.weapon);
		if (sd->vd.cloth_color)
			clif_changelook(&sd->bl,LOOK_CLOTHES_COLOR,sd->vd.cloth_color);
		if (sd->vd.body_style)
			clif_changelook(&sd->bl,LOOK_BODY2,sd->vd.body_style);
		clif_skillupdateinfoblock(sd);
	}

	return 0;
}

/*==========================================
 * Change account sex and unequip all item and request for a changesex to char-serv
 *------------------------------------------*/
BUILDIN_FUNC(changesex)
{
	int i;
	TBL_PC *sd = NULL;
	sd = script_rid2sd(st);

 	// to avoid any problem with equipment and invalid sex, equipment is unequiped.
	for(i = 0; i < EQI_MAX; i++) {
		if (sd->equip_index[i] >= 0)
			pc_unequipitem(sd, sd->equip_index[i], 3);
	}

	chrif_changesex(sd, true);
	return 0;
}

/*==========================================
+ * Change character's sex and unequip all item and request for a changesex to char-serv
*------------------------------------------*/
BUILDIN_FUNC(changecharsex)
{
#if PACKETVER >= 20141016
	int i;
	TBL_PC *sd = NULL;

	if (!script_charid2sd(2,sd))
		return 1;

	pc_resetskill(sd,4);
	// to avoid any problem with equipment and invalid sex, equipment is unequiped.
	for (i = 0; i < EQI_MAX; i++) {
		if (sd->equip_index[i] >= 0)
			pc_unequipitem(sd, sd->equip_index[i], 3);
	}

	chrif_changesex(sd, false);
	return 0;
#else
	return 1;
#endif
}

/*==========================================
 * Works like 'announce' but outputs in the common chat window
 *------------------------------------------*/
BUILDIN_FUNC(globalmes)
{
	struct block_list *bl = map_id2bl(st->oid);
	struct npc_data *nd = (struct npc_data *)bl;
	const char *name=NULL,*mes;

	mes=script_getstr(st,2);	// ���b�Z�[�W�̎擾
	if(mes==NULL) return 0;
	
	if(script_hasdata(st,3)){	// NPC���̎擾(123#456)
		name=script_getstr(st,3);
	} else {
		name=nd->name;
	}

	npc_globalmessage(name,mes);	// �O���[�o�����b�Z�[�W���M

	return 0;
}

/////////////////////////////////////////////////////////////////////
// NPC waiting room (chat room)
//

/// Creates a waiting room (chat room) for this npc.
///
/// waitingroom "<title>",<limit>{,"<event>"{,<trigger>{,<zeny>{,<minlvl>{,<maxlvl>}}}}};
BUILDIN_FUNC(waitingroom)
{
	struct npc_data* nd;	
	int pub = 1;
	const char* title = script_getstr(st, 2);
	int limit = script_getnum(st, 3);
	const char* ev = script_hasdata(st,4) ? script_getstr(st,4) : "";
	int trigger =  script_hasdata(st,5) ? script_getnum(st,5) : limit;
	int zeny =  script_hasdata(st,6) ? script_getnum(st,6) : 0;
	int minLvl =  script_hasdata(st,7) ? script_getnum(st,7) : 1;
	int maxLvl =  script_hasdata(st,8) ? script_getnum(st,8) : MAX_LEVEL;

	nd = (struct npc_data *)map_id2bl(st->oid);
	if( nd != NULL )
		chat_createnpcchat(nd, title, limit, pub, trigger, ev, zeny, minLvl, maxLvl);

	return 0;
}

/// Removes the waiting room of the current or target npc.
///
/// delwaitingroom "<npc_name>";
/// delwaitingroom;
BUILDIN_FUNC(delwaitingroom)
{
	struct npc_data* nd;
	if( script_hasdata(st,2) )
		nd = npc_name2id(script_getstr(st, 2));
	else
		nd = (struct npc_data *)map_id2bl(st->oid);
	if( nd != NULL )
		chat_deletenpcchat(nd);
	return 0;
}

/// Kicks all the players from the waiting room of the current or target npc.
///
/// kickwaitingroomall "<npc_name>";
/// kickwaitingroomall;
BUILDIN_FUNC(waitingroomkickall)
{
	struct npc_data* nd;
	struct chat_data* cd;

	if( script_hasdata(st,2) )
		nd = npc_name2id(script_getstr(st,2));
	else
		nd = (struct npc_data *)map_id2bl(st->oid);

	if( nd != NULL && (cd=(struct chat_data *)map_id2bl(nd->chat_id)) != NULL )
		chat_npckickall(cd);
	return 0;
}

/// Enables the waiting room event of the current or target npc.
///
/// enablewaitingroomevent "<npc_name>";
/// enablewaitingroomevent;
BUILDIN_FUNC(enablewaitingroomevent)
{
	struct npc_data* nd;
	struct chat_data* cd;

	if( script_hasdata(st,2) )
		nd = npc_name2id(script_getstr(st, 2));
	else
		nd = (struct npc_data *)map_id2bl(st->oid);

	if( nd != NULL && (cd=(struct chat_data *)map_id2bl(nd->chat_id)) != NULL )
		chat_enableevent(cd);
	return 0;
}

/// Disables the waiting room event of the current or target npc.
///
/// disablewaitingroomevent "<npc_name>";
/// disablewaitingroomevent;
BUILDIN_FUNC(disablewaitingroomevent)
{
	struct npc_data *nd;
	struct chat_data *cd;

	if( script_hasdata(st,2) )
		nd = npc_name2id(script_getstr(st, 2));
	else
		nd = (struct npc_data *)map_id2bl(st->oid);

	if( nd != NULL && (cd=(struct chat_data *)map_id2bl(nd->chat_id)) != NULL )
		chat_disableevent(cd);
	return 0;
}

/// Returns info on the waiting room of the current or target npc.
/// Returns -1 if the type unknown
/// <type>=0 : current number of users
/// <type>=1 : maximum number of users allowed
/// <type>=2 : the number of users that trigger the event
/// <type>=3 : if the trigger is disabled
/// <type>=4 : the title of the waiting room
/// <type>=5 : the password of the waiting room
/// <type>=16 : the name of the waiting room event
/// <type>=32 : if the waiting room is full
/// <type>=33 : if there are enough users to trigger the event
///
/// getwaitingroomstate(<type>,"<npc_name>") -> <info>
/// getwaitingroomstate(<type>) -> <info>
BUILDIN_FUNC(getwaitingroomstate)
{
	struct npc_data *nd;
	struct chat_data *cd;
	int type;

	type = script_getnum(st,2);
	if( script_hasdata(st,3) )
		nd = npc_name2id(script_getstr(st, 3));
	else
		nd = (struct npc_data *)map_id2bl(st->oid);

	if( nd == NULL || (cd=(struct chat_data *)map_id2bl(nd->chat_id)) == NULL )
	{
		script_pushint(st, -1);
		return 0;
	}

	switch(type)
	{
	case 0:  script_pushint(st, cd->users); break;
	case 1:  script_pushint(st, cd->limit); break;
	case 2:  script_pushint(st, cd->trigger&0x7f); break;
	case 3:  script_pushint(st, ((cd->trigger&0x80)!=0)); break;
	case 4:  script_pushstrcopy(st, cd->title); break;
	case 5:  script_pushstrcopy(st, cd->pass); break;
	case 16: script_pushstrcopy(st, cd->npc_event);break;
	case 32: script_pushint(st, (cd->users >= cd->limit)); break;
	case 33: script_pushint(st, (cd->users >= cd->trigger)); break;
	default: script_pushint(st, -1); break;
	}
	return 0;
}

/// Warps the trigger or target amount of players to the target map and position.
/// Players are automatically removed from the waiting room.
/// Those waiting the longest will get warped first.
/// The target map can be "Random" for a random position in the current map,
/// and "SavePoint" for the savepoint map+position.
/// The map flag noteleport of the current map is only considered when teleporting to the savepoint.
///
/// The id's of the teleported players are put into the array $@warpwaitingpc[]
/// The total number of teleported players is put into $@warpwaitingpcnum
///
/// warpwaitingpc "<map name>",<x>,<y>,<number of players>;
/// warpwaitingpc "<map name>",<x>,<y>;
BUILDIN_FUNC(warpwaitingpc)
{
	int x;
	int y;
	int i;
	int n;
	const char* map_name;
	struct npc_data* nd;
	struct chat_data* cd;
	TBL_PC* sd;

	nd = (struct npc_data *)map_id2bl(st->oid);
	if( nd == NULL || (cd=(struct chat_data *)map_id2bl(nd->chat_id)) == NULL )
		return 0;

	map_name = script_getstr(st,2);
	x = script_getnum(st,3);
	y = script_getnum(st,4);
	n = cd->trigger&0x7f;

	if( script_hasdata(st,5) )
		n = script_getnum(st,5);

	for( i = 0; i < n && cd->users > 0; i++ )
	{
		sd = cd->usersd[0];

		if( strcmp(map_name,"SavePoint") == 0 && map[sd->bl.m].flag.noteleport )
		{// can't teleport on this map
			break;
		}

		if( cd->zeny )
		{// fee set
			if( (uint32)sd->status.zeny < cd->zeny )
			{// no zeny to cover set fee
				break;
			}
			pc_payzeny(sd, cd->zeny);
		}

		mapreg_setreg(reference_uid(add_str("$@warpwaitingpc"), i), sd->bl.id);

		if( strcmp(map_name,"Random") == 0 )
			pc_randomwarp(sd,CLR_TELEPORT);
		else if( strcmp(map_name,"SavePoint") == 0 )
			pc_setpos(sd, sd->status.save_point.map, sd->status.save_point.x, sd->status.save_point.y, CLR_TELEPORT);
		else
			pc_setpos(sd, mapindex_name2id(map_name), x, y, CLR_OUTSIGHT);
	}
	mapreg_setreg(add_str("$@warpwaitingpcnum"), i);
	return 0;
}

/////////////////////////////////////////////////////////////////////
// ...
//

/// Detaches a character from a script.
///
/// @param st Script state to detach the character from.
static void script_detach_rid(struct script_state* st)
{
	if(st->rid)
	{
		script_detach_state(st, false);
		st->rid = 0;
	}
}

/*==========================================
 * RID�̃A�^�b�`
 *------------------------------------------*/
BUILDIN_FUNC(attachrid)
{
	int rid = script_getnum(st,2);
	struct map_session_data* sd;

	if ((sd = map_id2sd(rid))!=NULL) {
		script_detach_rid(st);

		st->rid = rid;
		script_attach_state(st);
		script_pushint(st,1);
	} else
		script_pushint(st,0);
	return 0;
}
/*==========================================
 * RID�̃f�^�b�`
 *------------------------------------------*/
BUILDIN_FUNC(detachrid)
{
	script_detach_rid(st);
	return 0;
}
/*==========================================
 * ���݃`�F�b�N
 *------------------------------------------*/
BUILDIN_FUNC(isloggedin)
{
	TBL_PC* sd = map_id2sd(script_getnum(st,2));
	if (script_hasdata(st,3) && sd &&
		sd->status.char_id != script_getnum(st,3))
		sd = NULL;
	push_val(st->stack,C_INT,sd!=NULL);
	return 0;
}


/*==========================================
 *
 *------------------------------------------*/
BUILDIN_FUNC(setmapflagnosave)
{
	int m,x,y;
	unsigned short mapindex;
	const char *str,*str2;

	str=script_getstr(st,2);
	str2=script_getstr(st,3);
	x=script_getnum(st,4);
	y=script_getnum(st,5);
	m = map_mapname2mapid(str);

    if(m >= 0) {
		if(strcmp(str2,"SavePoint")==0) {
				map[m].flag.nosave=1;
				map[m].save.map = 0;
				map[m].save.x = -1;
				map[m].save.y = -1;
		} else {
			mapindex = mapindex_name2id(str2);
			if(mapindex > 0) {
				map[m].flag.nosave=1;
				map[m].save.map=mapindex;
				map[m].save.x=x;
				map[m].save.y=y;
			}
		}
	}

	return 0;
}

BUILDIN_FUNC(getmapflag)
{
	int m,i;
	const char *str;

	str=script_getstr(st,2);
	i=script_getnum(st,3);

	m = map_mapname2mapid(str);
	if(m >= 0) {
		switch(i) {
			case MF_NOMEMO:				script_pushint(st,map[m].flag.nomemo); break;
			case MF_NOTELEPORT:			script_pushint(st,map[m].flag.noteleport); break;
			case MF_NOBRANCH:			script_pushint(st,map[m].flag.nobranch); break;
			case MF_NOPENALTY:			script_pushint(st,map[m].flag.noexppenalty); break;
			case MF_NOZENYPENALTY:		script_pushint(st,map[m].flag.nozenypenalty); break;
			case MF_PVP:				script_pushint(st,map[m].flag.pvp); break;
			case MF_PVP_NOPARTY:		script_pushint(st,map[m].flag.pvp_noparty); break;
			case MF_PVP_NOGUILD:		script_pushint(st,map[m].flag.pvp_noguild); break;
			case MF_GVG:				script_pushint(st,map[m].flag.gvg); break;
			case MF_GVG_NOPARTY:		script_pushint(st,map[m].flag.gvg_noparty); break;
			case MF_GVG_DUNGEON:		script_pushint(st,map[m].flag.gvg_dungeon); break;
			case MF_GVG_CASTLE:			script_pushint(st,map[m].flag.gvg_castle); break;
			case MF_NOTRADE:			script_pushint(st,map[m].flag.notrade); break;
			case MF_NODROP:				script_pushint(st,map[m].flag.nodrop); break;
			case MF_NOSKILL:			script_pushint(st,map[m].flag.noskill); break;
			case MF_NOWARP:				script_pushint(st,map[m].flag.nowarp); break;
			case MF_NOICEWALL:			script_pushint(st,map[m].flag.noicewall); break;
			case MF_SNOW:				script_pushint(st,map[m].flag.snow); break;
			case MF_CLOUDS:				script_pushint(st,map[m].flag.clouds); break;
			case MF_CLOUDS2:			script_pushint(st,map[m].flag.clouds2); break;
			case MF_FOG:				script_pushint(st,map[m].flag.fog); break;
			case MF_FIREWORKS:			script_pushint(st,map[m].flag.fireworks); break;
			case MF_SAKURA:				script_pushint(st,map[m].flag.sakura); break;
			case MF_LEAVES:				script_pushint(st,map[m].flag.leaves); break;
			case MF_RAIN:				script_pushint(st,map[m].flag.rain); break;
			case MF_NIGHTENABLED:		script_pushint(st,map[m].flag.nightenabled); break;
			case MF_NOGO:				script_pushint(st,map[m].flag.nogo); break;
			case MF_NOBASEEXP:			script_pushint(st,map[m].flag.nobaseexp); break;
			case MF_NOJOBEXP:			script_pushint(st,map[m].flag.nojobexp); break;
			case MF_NOMOBLOOT:			script_pushint(st,map[m].flag.nomobloot); break;
			case MF_NOMVPLOOT:			script_pushint(st,map[m].flag.nomvploot); break;
			case MF_NORETURN:			script_pushint(st,map[m].flag.noreturn); break;
			case MF_NOWARPTO:			script_pushint(st,map[m].flag.nowarpto); break;
			case MF_NIGHTMAREDROP:		script_pushint(st,map[m].flag.pvp_nightmaredrop); break;
			case MF_RESTRICTED:			script_pushint(st,map[m].flag.restricted); break;
			case MF_NOCOMMAND:			script_pushint(st,map[m].nocommand); break;
			case MF_JEXP:				script_pushint(st,map[m].jexp); break;
			case MF_BEXP:				script_pushint(st,map[m].bexp); break;
			case MF_NOVENDING:			script_pushint(st,map[m].flag.novending); break;
			case MF_LOADEVENT:			script_pushint(st,map[m].flag.loadevent); break;
			case MF_NOCHAT:				script_pushint(st,map[m].flag.nochat); break;
			case MF_PARTYLOCK:			script_pushint(st,map[m].flag.partylock); break;
			case MF_GUILDLOCK:			script_pushint(st,map[m].flag.guildlock); break;
			case MF_TOWN:				script_pushint(st,map[m].flag.town); break;
			case MF_AUTOTRADE:			script_pushint(st,map[m].flag.autotrade); break;
			case MF_ALLOWKS:			script_pushint(st,map[m].flag.allowks); break;
			case MF_MONSTER_NOTELEPORT:	script_pushint(st,map[m].flag.monster_noteleport); break;
			case MF_PVP_NOCALCRANK:		script_pushint(st,map[m].flag.pvp_nocalcrank); break;
			case MF_BATTLEGROUND:		script_pushint(st,map[m].flag.battleground); break;
			case MF_RESET:				script_pushint(st,map[m].flag.reset); break;
			case MF_GVG_TE_CASTLE:		script_pushint(st,map[m].flag.gvg_te_castle); break;
			case MF_GVG_TE:				script_pushint(st,map[m].flag.gvg_te); break;
			case MF_NOSUNMOONSTARMIRACLE: script_pushint(st, map[m].flag.nosunmoonstarmiracle); break;
		}
	}

	return 0;
}

BUILDIN_FUNC(setmapflag)
{
	int m,i;
	const char *str;
	const char *val=NULL;

	str=script_getstr(st,2);
	i=script_getnum(st,3);
	if(script_hasdata(st,4)){
		val=script_getstr(st,4);
	}
	m = map_mapname2mapid(str);
	if(m >= 0) {
		switch(i) {
			case MF_NOMEMO:				map[m].flag.nomemo=1; break;
			case MF_NOTELEPORT:			map[m].flag.noteleport=1; break;
			case MF_NOBRANCH:			map[m].flag.nobranch=1; break;
			case MF_NOPENALTY:			map[m].flag.noexppenalty=1; map[m].flag.nozenypenalty=1; break;
			case MF_NOZENYPENALTY:		map[m].flag.nozenypenalty=1; break;
			case MF_PVP:				map[m].flag.pvp=1; break;
			case MF_PVP_NOPARTY:		map[m].flag.pvp_noparty=1; break;
			case MF_PVP_NOGUILD:		map[m].flag.pvp_noguild=1; break;
			case MF_GVG:				map[m].flag.gvg=1; break;
			case MF_GVG_NOPARTY:		map[m].flag.gvg_noparty=1; break;
			case MF_GVG_DUNGEON:		map[m].flag.gvg_dungeon=1; break;
			case MF_GVG_CASTLE:			map[m].flag.gvg_castle=1; break;
			case MF_NOTRADE:			map[m].flag.notrade=1; break;
			case MF_NODROP:				map[m].flag.nodrop=1; break;
			case MF_NOSKILL:			map[m].flag.noskill=1; break;
			case MF_NOWARP:				map[m].flag.nowarp=1; break;
			case MF_NOICEWALL:			map[m].flag.noicewall=1; break;
			case MF_SNOW:				map[m].flag.snow=1; break;
			case MF_CLOUDS:				map[m].flag.clouds=1; break;
			case MF_CLOUDS2:			map[m].flag.clouds2=1; break;
			case MF_FOG:				map[m].flag.fog=1; break;
			case MF_FIREWORKS:			map[m].flag.fireworks=1; break;
			case MF_SAKURA:				map[m].flag.sakura=1; break;
			case MF_LEAVES:				map[m].flag.leaves=1; break;
			case MF_RAIN:				map[m].flag.rain=1; break;
			case MF_NIGHTENABLED:		map[m].flag.nightenabled=1; break;
			case MF_NOGO:				map[m].flag.nogo=1; break;
			case MF_NOBASEEXP:			map[m].flag.nobaseexp=1; break;
			case MF_NOJOBEXP:			map[m].flag.nojobexp=1; break;
			case MF_NOMOBLOOT:			map[m].flag.nomobloot=1; break;
			case MF_NOMVPLOOT:			map[m].flag.nomvploot=1; break;
			case MF_NORETURN:			map[m].flag.noreturn=1; break;
			case MF_NOWARPTO:			map[m].flag.nowarpto=1; break;
			case MF_NIGHTMAREDROP:		map[m].flag.pvp_nightmaredrop=1; break;
			case MF_RESTRICTED:
				map[m].zone |= 1<<((int)atoi(val)+1);
				map[m].flag.restricted=1;
				break;
			case MF_NOCOMMAND:			map[m].nocommand = (!val || atoi(val) <= 0) ? 100 : atoi(val); break;
			case MF_JEXP:				map[m].jexp = (!val || atoi(val) < 0) ? 100 : atoi(val); break;
			case MF_BEXP:				map[m].bexp = (!val || atoi(val) < 0) ? 100 : atoi(val); break;
			case MF_NOVENDING:			map[m].flag.novending=1; break;
			case MF_LOADEVENT:			map[m].flag.loadevent=1; break;
			case MF_NOCHAT:				map[m].flag.nochat=1; break;
			case MF_PARTYLOCK:			map[m].flag.partylock=1; break;
			case MF_GUILDLOCK:			map[m].flag.guildlock=1; break;
			case MF_TOWN:				map[m].flag.town=1; break;
			case MF_AUTOTRADE:			map[m].flag.autotrade=1; break;
			case MF_ALLOWKS:			map[m].flag.allowks=1; break;
			case MF_MONSTER_NOTELEPORT:	map[m].flag.monster_noteleport=1; break;
			case MF_PVP_NOCALCRANK:		map[m].flag.pvp_nocalcrank=1; break;
			case MF_BATTLEGROUND:		map[m].flag.battleground = (!val || atoi(val) < 0 || atoi(val) > 2) ? 1 : atoi(val); break;
			case MF_RESET:				map[m].flag.reset=1; break;
			case MF_GVG_TE_CASTLE:		map[m].flag.gvg_te_castle = 1; break;
			case MF_GVG_TE:
				map[m].flag.gvg_te = 1;
				clif_map_property_mapall(m, MAPPROPERTY_AGITZONE);
				break;
			case MF_NOSUNMOONSTARMIRACLE: map[m].flag.nosunmoonstarmiracle = 1; break;
		}
	}

	return 0;
}

BUILDIN_FUNC(removemapflag)
{
	int m,i;
	const char *str;
	const char *val=NULL;

	str=script_getstr(st,2);
	i=script_getnum(st,3);
	if(script_hasdata(st,4)){
		val=script_getstr(st,4);
	}
	m = map_mapname2mapid(str);
	if(m >= 0) {
		switch(i) {
			case MF_NOMEMO:				map[m].flag.nomemo=0; break;
			case MF_NOTELEPORT:			map[m].flag.noteleport=0; break;
			case MF_NOSAVE:				map[m].flag.nosave=0; break;
			case MF_NOBRANCH:			map[m].flag.nobranch=0; break;
			case MF_NOPENALTY:			map[m].flag.noexppenalty=0; map[m].flag.nozenypenalty=0; break;
			case MF_PVP:				map[m].flag.pvp=0; break;
			case MF_PVP_NOPARTY:		map[m].flag.pvp_noparty=0; break;
			case MF_PVP_NOGUILD:		map[m].flag.pvp_noguild=0; break;
			case MF_GVG:				map[m].flag.gvg=0; break;
			case MF_GVG_NOPARTY:		map[m].flag.gvg_noparty=0; break;
			case MF_GVG_DUNGEON:		map[m].flag.gvg_dungeon=0; break;
			case MF_GVG_CASTLE:			map[m].flag.gvg_castle=0; break;
			case MF_NOZENYPENALTY:		map[m].flag.nozenypenalty=0; break;
			case MF_NOTRADE:			map[m].flag.notrade=0; break;
			case MF_NODROP:				map[m].flag.nodrop=0; break;
			case MF_NOSKILL:			map[m].flag.noskill=0; break;
			case MF_NOWARP:				map[m].flag.nowarp=0; break;
			case MF_NOICEWALL:			map[m].flag.noicewall=0; break;
			case MF_SNOW:				map[m].flag.snow=0; break;
			case MF_CLOUDS:				map[m].flag.clouds=0; break;
			case MF_CLOUDS2:			map[m].flag.clouds2=0; break;
			case MF_FOG:				map[m].flag.fog=0; break;
			case MF_FIREWORKS:			map[m].flag.fireworks=0; break;
			case MF_SAKURA:				map[m].flag.sakura=0; break;
			case MF_LEAVES:				map[m].flag.leaves=0; break;
			case MF_RAIN:				map[m].flag.rain=0; break;
			case MF_NIGHTENABLED:		map[m].flag.nightenabled=0; break;
			case MF_NOGO:				map[m].flag.nogo=0; break;
			case MF_NOBASEEXP:			map[m].flag.nobaseexp=0; break;
			case MF_NOJOBEXP:			map[m].flag.nojobexp=0; break;
			case MF_NOMOBLOOT:			map[m].flag.nomobloot=0; break;
			case MF_NOMVPLOOT:			map[m].flag.nomvploot=0; break;
			case MF_NORETURN:			map[m].flag.noreturn=0; break;
			case MF_NOWARPTO:			map[m].flag.nowarpto=0; break;
			case MF_NIGHTMAREDROP:		map[m].flag.pvp_nightmaredrop=0; break;
			case MF_RESTRICTED:
				map[m].zone ^= 1<<((int)atoi(val)+1);
				if (map[m].zone == 0){
					map[m].flag.restricted=0;
				}
				break;
			case MF_NOCOMMAND:			map[m].nocommand=0; break;
			case MF_JEXP:				map[m].jexp=100; break;
			case MF_BEXP:				map[m].bexp=100; break;
			case MF_NOVENDING:			map[m].flag.novending=0; break;
			case MF_LOADEVENT:			map[m].flag.loadevent=0; break;
			case MF_NOCHAT:				map[m].flag.nochat=0; break;
			case MF_PARTYLOCK:			map[m].flag.partylock=0; break;
			case MF_GUILDLOCK:			map[m].flag.guildlock=0; break;
			case MF_TOWN:				map[m].flag.town=0; break;
			case MF_AUTOTRADE:			map[m].flag.autotrade=0; break;
			case MF_ALLOWKS:			map[m].flag.allowks=0; break;
			case MF_MONSTER_NOTELEPORT:	map[m].flag.monster_noteleport=0; break;
			case MF_PVP_NOCALCRANK:		map[m].flag.pvp_nocalcrank=0; break;
			case MF_BATTLEGROUND:		map[m].flag.battleground=0; break;
			case MF_RESET:				map[m].flag.reset=0; break;
			case MF_GVG_TE_CASTLE:		map[m].flag.gvg_te_castle = 0; break;
			case MF_GVG_TE:
				map[m].flag.gvg_te = 0;
				clif_map_property_mapall(m, MAPPROPERTY_NOTHING);
				break;
			case MF_NOSUNMOONSTARMIRACLE: map[m].flag.nosunmoonstarmiracle = 0; break;
		}
	}

	return 0;
}

BUILDIN_FUNC(pvpon)
{
	int m;
	const char *str;
	TBL_PC* sd = NULL;
	struct s_mapiterator* iter;

	str = script_getstr(st,2);
	m = map_mapname2mapid(str);
	if( m < 0 || map[m].flag.pvp )
		return 0; // nothing to do

	map[m].flag.pvp = 1;
	clif_map_property_mapall(m, MAPPROPERTY_FREEPVPZONE);

	if(battle_config.pk_mode) // disable ranking functions if pk_mode is on [Valaris]
		return 0;

	iter = mapit_getallusers();
	for( sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); sd = (TBL_PC*)mapit_next(iter) )
	{
		if( sd->bl.m != m || sd->pvp_timer != INVALID_TIMER )
			continue; // not applicable

		sd->pvp_timer = add_timer(gettick()+200,pc_calc_pvprank_timer,sd->bl.id,0);
		sd->pvp_rank = 0;
		sd->pvp_lastusers = 0;
		sd->pvp_point = 5;
		sd->pvp_won = 0;
		sd->pvp_lost = 0;
	}
	mapit_free(iter);

	return 0;
}

static int buildin_pvpoff_sub(struct block_list *bl,va_list ap)
{
	TBL_PC* sd = (TBL_PC*)bl;
	clif_pvpset(sd, 0, 0, 2);
	if (sd->pvp_timer != INVALID_TIMER) {
		delete_timer(sd->pvp_timer, pc_calc_pvprank_timer);
		sd->pvp_timer = INVALID_TIMER;
	}
	return 0;
}

BUILDIN_FUNC(pvpoff)
{
	int m;
	const char *str;

	str=script_getstr(st,2);
	m = map_mapname2mapid(str);
	if(m < 0 || !map[m].flag.pvp)
		return 0; //fixed Lupus

	map[m].flag.pvp = 0;
	clif_map_property_mapall(m, MAPPROPERTY_NOTHING);

	if(battle_config.pk_mode) // disable ranking options if pk_mode is on [Valaris]
		return 0;
	
	map_foreachinmap(buildin_pvpoff_sub, m, BL_PC);
	return 0;
}

BUILDIN_FUNC(gvgon)
{
	int m;
	const char *str;

	str=script_getstr(st,2);
	m = map_mapname2mapid(str);
	if(m >= 0 && !map[m].flag.gvg) {
		map[m].flag.gvg = 1;
		clif_map_property_mapall(m, MAPPROPERTY_AGITZONE);
	}

	return 0;
}
BUILDIN_FUNC(gvgoff)
{
	int m;
	const char *str;

	str=script_getstr(st,2);
	m = map_mapname2mapid(str);
	if(m >= 0 && map[m].flag.gvg) {
		map[m].flag.gvg = 0;
		clif_map_property_mapall(m, MAPPROPERTY_NOTHING);
	}

	return 0;
}

BUILDIN_FUNC(gvgon3)
{
	int16 m;
	const char *str;

	str = script_getstr(st,2);
	m = map_mapname2mapid(str);
	if (m >= 0 && !map[m].flag.gvg_te) {
		map[m].flag.gvg_te = 1;
		clif_map_property_mapall(m, MAPPROPERTY_AGITZONE);
	}
	return 0;
}

BUILDIN_FUNC(gvgoff3)
{
	int16 m;
	const char *str;

	str = script_getstr(st,2);
	m = map_mapname2mapid(str);
	if (m >= 0 && map[m].flag.gvg_te) {
		map[m].flag.gvg_te = 0;
		clif_map_property_mapall(m, MAPPROPERTY_NOTHING);
	}
	return 0;
}

/*==========================================
 *	Shows an emoticon on top of the player/npc
 *	emotion emotion#, <target: 0 - NPC, 1 - PC>, <NPC/PC name>
 *------------------------------------------*/
//Optional second parameter added by [Skotlex]
BUILDIN_FUNC(emotion)
{
	int type;
	int player=0;
	
	type=script_getnum(st,2);
	if(type < 0 || type > 100)
		return 0;

	if( script_hasdata(st,3) )
		player=script_getnum(st,3);
	
	if (player) {
		TBL_PC *sd = NULL;
		if( script_hasdata(st,4) )
			sd = map_nick2sd(script_getstr(st,4));
		else
			sd = script_rid2sd(st);
		if (sd)
			clif_emotion(&sd->bl,type);
	} else
		if( script_hasdata(st,4) )
		{
			TBL_NPC *nd = npc_name2id(script_getstr(st,4));
			if(nd)
				clif_emotion(&nd->bl,type);
		}
		else
			clif_emotion(map_id2bl(st->oid),type);
	return 0;
}

static int buildin_maprespawnguildid_sub_pc(struct map_session_data* sd, va_list ap)
{
	int m=va_arg(ap,int);
	int g_id=va_arg(ap,int);
	int flag=va_arg(ap,int);

	if(!sd || sd->bl.m != m)
		return 0;
	if(
		(sd->status.guild_id == g_id && flag&1) || //Warp out owners
		(sd->status.guild_id != g_id && flag&2) || //Warp out outsiders
		(sd->status.guild_id == 0)	// Warp out players not in guild [Valaris]
	)
		pc_setpos(sd,sd->status.save_point.map,sd->status.save_point.x,sd->status.save_point.y,CLR_TELEPORT);
	return 1;
}

static int buildin_maprespawnguildid_sub_mob(struct block_list *bl,va_list ap)
{
	struct mob_data *md=(struct mob_data *)bl;

	if(!md->guardian_data && md->class_ != MOBID_EMPERIUM)
		status_kill(bl);

	return 0;
}

BUILDIN_FUNC(maprespawnguildid)
{
	const char *mapname=script_getstr(st,2);
	int g_id=script_getnum(st,3);
	int flag=script_getnum(st,4);

	int m=map_mapname2mapid(mapname);

	if(m == -1)
		return 0;

	//Catch ALL players (in case some are 'between maps' on execution time)
	map_foreachpc(buildin_maprespawnguildid_sub_pc,m,g_id,flag);
	if (flag&4) //Remove script mobs.
		map_foreachinmap(buildin_maprespawnguildid_sub_mob,m,BL_MOB);
	return 0;
}

/// Siege commands

/*==========================================
 * Start WoE:FE
 * agitstart();
 *------------------------------------------*/
BUILDIN_FUNC(agitstart)
{
	if(agit_flag)
		return 0; // Agit already Start.
	
	agit_flag = true;
	guild_agit_start();
	return 0;
}

/*==========================================
 * End WoE:FE
 * agitend();
 *------------------------------------------*/
BUILDIN_FUNC(agitend)
{
	if(!agit_flag)
		return 0; // Agit already End.
	
	agit_flag = false;
	guild_agit_end();
	return 0;
}

/*==========================================
 * Start WoE:SE
 * agitstart2();
 *------------------------------------------*/
BUILDIN_FUNC(agitstart2)
{
	if(agit2_flag)
		return 0; // Agit2 already Start.
	
	agit2_flag = true;
	guild_agit2_start();
	return 0;
}

/*==========================================
 * End WoE:SE
 * agitend2();
 *------------------------------------------*/
BUILDIN_FUNC(agitend2)
{
	if(!agit2_flag)
		return 0; // Agit2 already End.
	
	agit2_flag = false;
	guild_agit2_end();
	return 0;
}

/*==========================================
 * Start WoE:TE
 * agitstart3();
 *------------------------------------------*/
BUILDIN_FUNC(agitstart3)
{
	if(agit3_flag)
		return 0; // Agit3 already Start.
	
	agit3_flag = true;
	guild_agit3_start();
	return 0;
}

/*==========================================
 * End WoE:TE
 * agitend3();
 *------------------------------------------*/
BUILDIN_FUNC(agitend3)
{
	if(!agit3_flag)
		return 0; // Agit3 already End.
	
	agit3_flag = false;
	guild_agit3_end();
	return 0;
}

/*==========================================
 * Returns whether WoE:FE is on or off.
 * agitcheck();
 *------------------------------------------*/
BUILDIN_FUNC(agitcheck)
{
	script_pushint(st, agit_flag);
	return 0;
}

/*==========================================
 * Returns whether WoE:SE is on or off.
 * agitcheck2();
 *------------------------------------------*/
BUILDIN_FUNC(agitcheck2)
{
	script_pushint(st,agit2_flag);
	return 0;
}

/*==========================================
 * Returns whether WoE:TE is on or off.
 * agitcheck3();
 *------------------------------------------*/
BUILDIN_FUNC(agitcheck3)
{
	script_pushint(st,agit3_flag);
	return 0;
}

/// Sets the guild_id of this npc.
///
/// flagemblem <guild_id>;
BUILDIN_FUNC(flagemblem)
{
	TBL_NPC* nd;
	int g_id=script_getnum(st,2);

	if(g_id < 0) return 0;

	nd = (TBL_NPC*)map_id2nd(st->oid);
	if( nd == NULL )
	{
		ShowError("script:flagemblem: npc %d not found\n", st->oid);
	}
	else if( nd->subtype != NPCTYPE_SCRIPT )
	{
		ShowError("script:flagemblem: unexpected subtype %d for npc %d '%s'\n", nd->subtype, st->oid, nd->exname);
	}
	else
	{
		nd->u.scr.guild_id = g_id;
		clif_guild_emblem_area(&nd->bl);
	}
	return 0;
}

BUILDIN_FUNC(getcastlename)
{
	const char* mapname = mapindex_getmapname(script_getstr(st,2),NULL);
	struct guild_castle* gc = guild_mapname2gc(mapname);
	const char* name = (gc) ? gc->castle_name : "";
	script_pushstrcopy(st,name);
	return 0;
}

BUILDIN_FUNC(getcastledata)
{
	const char* mapname = mapindex_getmapname(script_getstr(st,2),NULL);
	int index = script_getnum(st,3);

	struct guild_castle* gc = guild_mapname2gc(mapname);

	if(script_hasdata(st,4) && index==0 && gc) {
		const char* event = script_getstr(st,4);
		check_event(st, event);
		guild_addcastleinfoevent(gc->castle_id,17,event);
	}

	if(gc){
		switch(index){
			case 0: {
				int i;
				for(i=1;i<18;i++) // Initialize[AgitInit]
					guild_castledataload(gc->castle_id,i);
				} break;
			case 1:
				script_pushint(st,gc->guild_id); break;
			case 2:
				script_pushint(st,gc->economy); break;
			case 3:
				script_pushint(st,gc->defense); break;
			case 4:
				script_pushint(st,gc->triggerE); break;
			case 5:
				script_pushint(st,gc->triggerD); break;
			case 6:
				script_pushint(st,gc->nextTime); break;
			case 7:
				script_pushint(st,gc->payTime); break;
			case 8:
				script_pushint(st,gc->createTime); break;
			case 9:
				script_pushint(st,gc->visibleC); break;
			case 10:
			case 11:
			case 12:
			case 13:
			case 14:
			case 15:
			case 16:
			case 17:
				script_pushint(st,gc->guardian[index-10].visible); break;
			default:
				script_pushint(st,0); break;
		}
		return 0;
	}
	script_pushint(st,0);
	return 0;
}

BUILDIN_FUNC(setcastledata)
{
	const char* mapname = mapindex_getmapname(script_getstr(st,2),NULL);
	int index = script_getnum(st,3);
	int value = script_getnum(st,4);

	struct guild_castle* gc = guild_mapname2gc(mapname);

	if(gc) {
		// Save Data byself First
		switch(index){
			case 1:
				gc->guild_id = value; break;
			case 2:
				gc->economy = value; break;
			case 3:
				gc->defense = value; break;
			case 4:
				gc->triggerE = value; break;
			case 5:
				gc->triggerD = value; break;
			case 6:
				gc->nextTime = value; break;
			case 7:
				gc->payTime = value; break;
			case 8:
				gc->createTime = value; break;
			case 9:
				gc->visibleC = value; break;
			case 10:
			case 11:
			case 12:
			case 13:
			case 14:
			case 15:
			case 16:
			case 17:
				gc->guardian[index-10].visible = value; break;
			default:
				return 0;
		}
		guild_castledatasave(gc->castle_id,index,value);
	}
	return 0;
}

/* =====================================================================
 * �M���h����v������
 * ---------------------------------------------------------------------*/
BUILDIN_FUNC(requestguildinfo)
{
	int guild_id=script_getnum(st,2);
	const char *event=NULL;

	if( script_hasdata(st,3) ){
		event=script_getstr(st,3);
		check_event(st, event);
	}

	if(guild_id>0)
		guild_npc_request_info(guild_id,event);
	return 0;
}

/// Returns the number of cards that have been compounded onto the specified equipped item.
/// getequipcardcnt(<equipment slot>);
BUILDIN_FUNC(getequipcardcnt)
{
	int i=-1,j,num;
	TBL_PC *sd;
	int count;

	num=script_getnum(st,2);
	sd=script_rid2sd(st);
	if (num > 0 && num <= ARRAYLENGTH(equip))
		i=pc_checkequip(sd,equip[num-1],false);

	if (i < 0 || !sd->inventory_data[i]) {
		script_pushint(st,0);
		return 0;
	}

	if(itemdb_isspecial(sd->inventory.u.items_inventory[i].card[0]))
	{
		script_pushint(st,0);
		return 0;
	}

	count = 0;
	for( j = 0; j < sd->inventory_data[i]->slot; j++ )
		if( sd->inventory.u.items_inventory[i].card[j] && itemdb_type(sd->inventory.u.items_inventory[i].card[j]) == IT_CARD )
			count++;

	script_pushint(st,count);
	return 0;
}

/// Removes all cards from the item found in the specified equipment slot of the invoking character,
/// and give them to the character. If any cards were removed in this manner, it will also show a success effect.
/// successremovecards <slot>;
BUILDIN_FUNC(successremovecards)
{
	int i=-1,j,c,cardflag=0;

	TBL_PC* sd = script_rid2sd(st);
	int num = script_getnum(st,2);

	if (num > 0 && num <= ARRAYLENGTH(equip))
		i=pc_checkequip(sd,equip[num-1],false);

	if (i < 0 || !sd->inventory_data[i]) {
		return 0;
	}

	if(itemdb_isspecial(sd->inventory.u.items_inventory[i].card[0])) 
		return 0;

	for( c = sd->inventory_data[i]->slot - 1; c >= 0; --c )
	{
		if( sd->inventory.u.items_inventory[i].card[c] && itemdb_type(sd->inventory.u.items_inventory[i].card[c]) == IT_CARD )
		{// extract this card from the item
			int flag;
			struct item item_tmp;
			cardflag = 1;
			item_tmp.id=0,item_tmp.nameid=sd->inventory.u.items_inventory[i].card[c];
			item_tmp.equip=0,item_tmp.identify=1,item_tmp.refine=0;
			item_tmp.attribute=0,item_tmp.expire_time=0,item_tmp.bound=0;
			for (j = 0; j < MAX_SLOTS; j++)
				item_tmp.card[j]=0;

			for (j = 0; j < MAX_ITEM_RDM_OPT; j++)
			{
				item_tmp.option[j].id = 0;
				item_tmp.option[j].value = 0;
				item_tmp.option[j].param = 0;
			}

			//Logs items, got from (N)PC scripts [Lupus]
			log_pick(&sd->bl, LOG_TYPE_SCRIPT, item_tmp.nameid, 1, NULL);

			if((flag=pc_additem(sd,&item_tmp,1))){	// ���ĂȂ��Ȃ�h���b�v
				clif_additem(sd,0,0,flag);
				map_addflooritem(&item_tmp,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0);
			}
		}
	}

	if(cardflag == 1)
	{	//if card was removed replace item with no card
		int flag;
		struct item item_tmp;
		item_tmp.id			=0,item_tmp.nameid=sd->inventory.u.items_inventory[i].nameid;
		item_tmp.equip		=0,item_tmp.identify=1,item_tmp.refine=sd->inventory.u.items_inventory[i].refine;
		item_tmp.attribute	=sd->inventory.u.items_inventory[i].attribute,item_tmp.expire_time=sd->inventory.u.items_inventory[i].expire_time;
		item_tmp.bound      = sd->inventory.u.items_inventory[i].bound;
		for (j = 0; j < sd->inventory_data[i]->slot; j++)
			item_tmp.card[j]=0;
		for (j = sd->inventory_data[i]->slot; j < MAX_SLOTS; j++)
			item_tmp.card[j]=sd->inventory.u.items_inventory[i].card[j];

		for (j = 0; j < MAX_ITEM_RDM_OPT; j++)
		{
			item_tmp.option[j].id=sd->inventory.u.items_inventory[i].option[j].id;
			item_tmp.option[j].value=sd->inventory.u.items_inventory[i].option[j].value;
			item_tmp.option[j].param=sd->inventory.u.items_inventory[i].option[j].param;
		}

		//Logs items, got from (N)PC scripts [Lupus]
		log_pick(&sd->bl, LOG_TYPE_SCRIPT, sd->inventory.u.items_inventory[i].nameid, -1, &sd->inventory.u.items_inventory[i]);

		pc_delitem(sd,i,1,0,3);

		//Logs items, got from (N)PC scripts [Lupus]
		log_pick(&sd->bl, LOG_TYPE_SCRIPT, item_tmp.nameid, 1, &item_tmp);

		if((flag=pc_additem(sd,&item_tmp,1))){	// ���ĂȂ��Ȃ�h���b�v
			clif_additem(sd,0,0,flag);
			map_addflooritem(&item_tmp,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0);
		}

		clif_misceffect(&sd->bl,3);
	}
	return 0;
}

/// Removes all cards from the item found in the specified equipment slot of the invoking character.
/// failedremovecards <slot>, <type>;
/// <type>=0 : will destroy both the item and the cards.
/// <type>=1 : will keep the item, but destroy the cards.
/// <type>=2 : will keep the cards, but destroy the item.
/// <type>=? : will just display the failure effect.
BUILDIN_FUNC(failedremovecards)
{
	int i=-1,j,c,cardflag=0;

	TBL_PC* sd = script_rid2sd(st);
	int num = script_getnum(st,2);
	int typefail = script_getnum(st,3);

	if (num > 0 && num <= ARRAYLENGTH(equip))
		i=pc_checkequip(sd,equip[num-1],false);

	if (i < 0 || !sd->inventory_data[i])
		return 0;

	if(itemdb_isspecial(sd->inventory.u.items_inventory[i].card[0]))
		return 0;

	for( c = sd->inventory_data[i]->slot - 1; c >= 0; --c )
	{
		if( sd->inventory.u.items_inventory[i].card[c] && itemdb_type(sd->inventory.u.items_inventory[i].card[c]) == IT_CARD )
		{
			cardflag = 1;

			if(typefail == 2)
			{// add cards to inventory, clear 
				int flag;
				struct item item_tmp;
				item_tmp.id=0,item_tmp.nameid=sd->inventory.u.items_inventory[i].card[c];
				item_tmp.equip=0,item_tmp.identify=1,item_tmp.refine=0;
				item_tmp.attribute=0,item_tmp.expire_time=0;
				for (j = 0; j < MAX_SLOTS; j++)
					item_tmp.card[j]=0;

				for (j = 0; j < MAX_ITEM_RDM_OPT; j++)
				{
					item_tmp.option[j].id=sd->inventory.u.items_inventory[i].option[j].id;
					item_tmp.option[j].value=sd->inventory.u.items_inventory[i].option[j].value;
					item_tmp.option[j].param=sd->inventory.u.items_inventory[i].option[j].param;
				}

				//Logs items, got from (N)PC scripts [Lupus]
				log_pick(&sd->bl, LOG_TYPE_SCRIPT, item_tmp.nameid, 1, NULL);

				if((flag=pc_additem(sd,&item_tmp,1))){
					clif_additem(sd,0,0,flag);
					map_addflooritem(&item_tmp,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0);
				}
			}
		}
	}

	if(cardflag == 1)
	{
		if(typefail == 0 || typefail == 2){	// �����
			//Logs items, got from (N)PC scripts [Lupus]
			log_pick(&sd->bl, LOG_TYPE_SCRIPT, sd->inventory.u.items_inventory[i].nameid, -1, &sd->inventory.u.items_inventory[i]);

			pc_delitem(sd,i,1,0,2);
		}
		if(typefail == 1){	// �J�[�h�̂ݑ����i�����Ԃ��j
			int flag;
			struct item item_tmp;
			item_tmp.id			=0,item_tmp.nameid=sd->inventory.u.items_inventory[i].nameid;
			item_tmp.equip		=0,item_tmp.identify=1,item_tmp.refine=sd->inventory.u.items_inventory[i].refine;
			item_tmp.attribute	=sd->inventory.u.items_inventory[i].attribute,item_tmp.expire_time=sd->inventory.u.items_inventory[i].expire_time;
			item_tmp.bound      = sd->inventory.u.items_inventory[i].bound;

			//Logs items, got from (N)PC scripts [Lupus]
			log_pick(&sd->bl, LOG_TYPE_SCRIPT, sd->inventory.u.items_inventory[i].nameid, -1, &sd->inventory.u.items_inventory[i]);

			for (j = 0; j < sd->inventory_data[i]->slot; j++)
				item_tmp.card[j]=0;
			for (j = sd->inventory_data[i]->slot; j < MAX_SLOTS; j++)
				item_tmp.card[j]=sd->inventory.u.items_inventory[i].card[j];
			pc_delitem(sd,i,1,0,2);

			//Logs items, got from (N)PC scripts [Lupus]
			log_pick(&sd->bl, LOG_TYPE_SCRIPT, item_tmp.nameid, 1, &item_tmp);

			if((flag=pc_additem(sd,&item_tmp,1))){
				clif_additem(sd,0,0,flag);
				map_addflooritem(&item_tmp,1,sd->bl.m,sd->bl.x,sd->bl.y,0,0,0,0);
			}
		}
		clif_misceffect(&sd->bl,2);
	}

	return 0;
}

/* ================================================================
 * mapwarp "<from map>","<to map>",<x>,<y>,<type>,<ID for Type>;
 * type: 0=everyone, 1=guild, 2=party;	[Reddozen]
 * improved by [Lance]
 * ================================================================*/
BUILDIN_FUNC(mapwarp)	// Added by RoVeRT
{
	int x,y,m,check_val=0,check_ID=0,i=0;
	struct guild *g = NULL;
	struct party_data *p = NULL;
	const char *str;
	const char *mapname;
	unsigned int index;
	mapname=script_getstr(st,2);
	str=script_getstr(st,3);
	x=script_getnum(st,4);
	y=script_getnum(st,5);
	if(script_hasdata(st,7)){
		check_val=script_getnum(st,6);
		check_ID=script_getnum(st,7);
	}

	if((m=map_mapname2mapid(mapname))< 0)
		return 0;

	if(!(index=mapindex_name2id(str)))
		return 0;

	switch(check_val){
		case 1:
			g = guild_search(check_ID);
			if (g){
				for( i=0; i < g->max_member; i++)
				{
					if(g->member[i].sd && g->member[i].sd->bl.m==m){
						pc_setpos(g->member[i].sd,index,x,y,CLR_TELEPORT);
					}
				}
			}
			break;
		case 2:
			p = party_search(check_ID);
			if(p){
				for(i=0;i<MAX_PARTY; i++){
					if(p->data[i].sd && p->data[i].sd->bl.m == m){
						pc_setpos(p->data[i].sd,index,x,y,CLR_TELEPORT);
					}
				}
			}
			break;
		default:
			map_foreachinmap(buildin_areawarp_sub,m,BL_PC,index,x,y);
			break;
	}

	return 0;
}

static int buildin_mobcount_sub(struct block_list *bl,va_list ap)	// Added by RoVeRT
{
	char *event=va_arg(ap,char *);
	struct mob_data *md = ((struct mob_data *)bl);
	if(strcmp(event,md->npc_event)==0 && md->status.hp > 0)
		return 1;
	return 0;
}

BUILDIN_FUNC(mobcount)	// Added by RoVeRT
{
	const char *mapname,*event;
	int m;
	mapname=script_getstr(st,2);
	event=script_getstr(st,3);
	check_event(st, event);

	if( (m = map_mapname2mapid(mapname)) < 0 ) {
		script_pushint(st,-1);
		return 0;
	}
	
	if( map[m].flag.src4instance && map[m].instance_id == 0 && st->instance_id && (m = instance_mapid2imapid(m, st->instance_id)) < 0 )
	{
		script_pushint(st,-1);
		return 0;
	}

	script_pushint(st,map_foreachinmap(buildin_mobcount_sub, m, BL_MOB, event));

	return 0;
}
BUILDIN_FUNC(marriage)
{
	const char *partner=script_getstr(st,2);
	TBL_PC *sd=script_rid2sd(st);
	TBL_PC *p_sd=map_nick2sd(partner);

	if(sd==NULL || p_sd==NULL || pc_marriage(sd,p_sd) < 0){
		script_pushint(st,0);
		return 0;
	}
	script_pushint(st,1);
	return 0;
}
BUILDIN_FUNC(wedding_effect)
{
	TBL_PC *sd=script_rid2sd(st);
	struct block_list *bl;

	if(sd==NULL) {
		bl=map_id2bl(st->oid);
	} else
		bl=&sd->bl;
	clif_wedding_effect(bl);
	return 0;
}
BUILDIN_FUNC(divorce)
{
	TBL_PC *sd=script_rid2sd(st);
	if(sd==NULL || pc_divorce(sd) < 0){
		script_pushint(st,0);
		return 0;
	}
	script_pushint(st,1);
	return 0;
}

BUILDIN_FUNC(ispartneron)
{
	TBL_PC *sd=script_rid2sd(st);

	if(sd==NULL || !pc_ismarried(sd) ||
            map_charid2sd(sd->status.partner_id) == NULL) {
		script_pushint(st,0);
		return 0;
	}

	script_pushint(st,1);
	return 0;
}

BUILDIN_FUNC(getpartnerid)
{
    TBL_PC *sd=script_rid2sd(st);
    if (sd == NULL) {
        script_pushint(st,0);
        return 0;
    }

    script_pushint(st,sd->status.partner_id);
    return 0;
}

BUILDIN_FUNC(getchildid)
{
    TBL_PC *sd=script_rid2sd(st);
    if (sd == NULL) {
        script_pushint(st,0);
        return 0;
    }

    script_pushint(st,sd->status.child);
    return 0;
}

BUILDIN_FUNC(getmotherid)
{
    TBL_PC *sd=script_rid2sd(st);
    if (sd == NULL) {
        script_pushint(st,0);
        return 0;
    }

    script_pushint(st,sd->status.mother);
    return 0;
}

BUILDIN_FUNC(getfatherid)
{
    TBL_PC *sd=script_rid2sd(st);
    if (sd == NULL) {
        script_pushint(st,0);
        return 0;
    }

    script_pushint(st,sd->status.father);
    return 0;
}

BUILDIN_FUNC(warppartner)
{
	int x,y;
	unsigned short mapindex;
	const char *str;
	TBL_PC *sd=script_rid2sd(st);
	TBL_PC *p_sd=NULL;

	if(sd==NULL || !pc_ismarried(sd) ||
   	(p_sd=map_charid2sd(sd->status.partner_id)) == NULL) {
		script_pushint(st,0);
		return 0;
	}

	str=script_getstr(st,2);
	x=script_getnum(st,3);
	y=script_getnum(st,4);

	mapindex = mapindex_name2id(str);
	if (mapindex) {
		pc_setpos(p_sd,mapindex,x,y,CLR_OUTSIGHT);
		script_pushint(st,1);
	} else
		script_pushint(st,0);
	return 0;
}

/*================================================
 * Script for Displaying MOB Information [Valaris]
 *------------------------------------------------*/
BUILDIN_FUNC(strmobinfo)
{

	int num=script_getnum(st,2);
	int class_=script_getnum(st,3);

	if(!mobdb_checkid(class_))
	{
		script_pushint(st,0);
		return 0;
	}

	switch (num) {
	case 1: script_pushstrcopy(st,mob_db(class_)->name); break;
	case 2: script_pushstrcopy(st,mob_db(class_)->jname); break;
	case 3: script_pushint(st,mob_db(class_)->lv); break;
	case 4: script_pushint(st,mob_db(class_)->status.max_hp); break;
	case 5: script_pushint(st,mob_db(class_)->status.max_sp); break;
	case 6: script_pushint(st,mob_db(class_)->base_exp); break;
	case 7: script_pushint(st,mob_db(class_)->job_exp); break;
	default:
		script_pushint(st,0);
		break;
	}
	return 0;
}

/*==========================================
 * Summon guardians [Valaris]
 * guardian("<map name>",<x>,<y>,"<name to show>",<mob id>{,"<event label>"}{,<guardian index>}) -> <id>
 *------------------------------------------*/
BUILDIN_FUNC(guardian)
{
	int class_=0,x=0,y=0,guardian=0;
	const char *str,*map,*evt="";
	struct script_data *data;
	bool has_index = false;

	map	  =script_getstr(st,2);
	x	  =script_getnum(st,3);
	y	  =script_getnum(st,4);
	str	  =script_getstr(st,5);
	class_=script_getnum(st,6);

	if( script_hasdata(st,8) )
	{// "<event label>",<guardian index>
		evt=script_getstr(st,7);
		guardian=script_getnum(st,8);
		has_index = true;
	} else if( script_hasdata(st,7) ){
		data=script_getdata(st,7);
		get_val(st,data);
		if( data_isstring(data) )
		{// "<event label>"
			evt=script_getstr(st,7);
		} else if( data_isint(data) )
		{// <guardian index>
			guardian=script_getnum(st,7);
			has_index = true;
		} else {
			ShowError("script:guardian: invalid data type for argument #6 (from 1)\n");
			script_reportdata(data);
			return 1;
		}
	}

	check_event(st, evt);
	script_pushint(st, mob_spawn_guardian(map,x,y,str,class_,evt,guardian,has_index));

	return 0;
}
/*==========================================
 * Invisible Walls [Zephyrus]
 *------------------------------------------*/
BUILDIN_FUNC(setwall)
{
	const char *map, *name;
	int x, y, m, size, dir;
	bool shootable;
	
	map = script_getstr(st,2);
	x = script_getnum(st,3);
	y = script_getnum(st,4);
	size = script_getnum(st,5);
	dir = script_getnum(st,6);
	shootable = script_getnum(st,7);
	name = script_getstr(st,8);

	if( (m = map_mapname2mapid(map)) < 0 )
		return 0; // Invalid Map

	map_iwall_set(m, x, y, size, dir, shootable, name);
	return 0;
}
BUILDIN_FUNC(delwall)
{
	const char *name = script_getstr(st,2);
	map_iwall_remove(name);

	return 0;
}

/// Retrieves various information about the specified guardian.
///
/// guardianinfo("<map_name>", <index>, <type>) -> <value>
/// type: 0 - whether it is deployed or not
///       1 - maximum hp
///       2 - current hp
///
BUILDIN_FUNC(guardianinfo)
{
	const char* mapname = mapindex_getmapname(script_getstr(st,2),NULL);
	int id = script_getnum(st,3);
	int type = script_getnum(st,4);

	struct guild_castle* gc = guild_mapname2gc(mapname);
	struct mob_data* gd;

	if( gc == NULL || id < 0 || id >= MAX_GUARDIANS )
	{
		script_pushint(st,-1);
		return 0;
	}

	if( type == 0 )
		script_pushint(st, gc->guardian[id].visible);
	else
	if( !gc->guardian[id].visible )
		script_pushint(st,-1);
	else
	if( (gd = map_id2md(gc->guardian[id].id)) == NULL )
		script_pushint(st,-1);
	else
	{
		if     ( type == 1 ) script_pushint(st,gd->status.max_hp);
		else if( type == 2 ) script_pushint(st,gd->status.hp);
		else
			script_pushint(st,-1);
	}

	return 0;
}

/*==========================================
 * ID����Item��
 *------------------------------------------*/
BUILDIN_FUNC(getitemname)
{
	unsigned short item_id=0;
	struct item_data *i_data;
	char *item_name;
	struct script_data *data;

	data=script_getdata(st,2);
	get_val(st,data);

	if( data_isstring(data) ){
		const char *name=conv_str(st,data);
		struct item_data *item_data = itemdb_searchname(name);
		if( item_data )
			item_id=item_data->nameid;
	}else
		item_id=conv_num(st,data);

	i_data = itemdb_exists(item_id);
	if (i_data == NULL)
	{
		script_pushconststr(st,"null");
		return 0;
	}
	item_name=(char *)aMallocA(ITEM_NAME_LENGTH*sizeof(char));

	memcpy(item_name, i_data->jname, ITEM_NAME_LENGTH);
	script_pushstr(st,item_name);
	return 0;
}
/*==========================================
 * Returns number of slots an item has. [Skotlex]
 *------------------------------------------*/
BUILDIN_FUNC(getitemslots)
{
	unsigned short item_id;
	struct item_data *i_data;

	item_id=script_getnum(st,2);

	i_data = itemdb_exists(item_id);

	if (i_data)
		script_pushint(st,i_data->slot);
	else
		script_pushint(st,-1);
	return 0;
}

/*==========================================
 * Returns some values of an item [Lupus]
 * Price, Weight, etc...
	getiteminfo(itemID,n), where n
		0 value_buy;
		1 value_sell;
		2 type;
		3 maxchance = Max drop chance of this item e.g. 1 = 0.01% , etc..
				if = 0, then monsters don't drop it at all (rare or a quest item)
				if = -1, then this item is sold in NPC shops only
		4 sex;
		5 equip;
		6 weight;
		7 atk;
		8 def;
		9 range;
		10 slot;
		11 look;
		12 elv;
		13 wlv;
		14 view id
 *------------------------------------------*/
BUILDIN_FUNC(getiteminfo)
{
	unsigned short item_id,n;
	int *item_arr;
	struct item_data *i_data;

	item_id	= script_getnum(st,2);
	n	= script_getnum(st,3);
	i_data = itemdb_exists(item_id);

	if (i_data && n>=0 && n<=14) {
		item_arr = (int*)&i_data->value_buy;
		script_pushint(st,item_arr[n]);
	} else
		script_pushint(st,-1);
	return 0;
}

/*==========================================
 * Set some values of an item [Lupus]
 * Price, Weight, etc...
	setiteminfo(itemID,n,Value), where n
		0 value_buy;
		1 value_sell;
		2 type;
		3 maxchance = Max drop chance of this item e.g. 1 = 0.01% , etc..
				if = 0, then monsters don't drop it at all (rare or a quest item)
				if = -1, then this item is sold in NPC shops only
		4 sex;
		5 equip;
		6 weight;
		7 atk;
		8 def;
		9 range;
		10 slot;
		11 look;
		12 elv;
		13 wlv;
		14 view id
  * Returns Value or -1 if the wrong field's been set
 *------------------------------------------*/
BUILDIN_FUNC(setiteminfo)
{
	unsigned short item_id;
	int n,value;
	int *item_arr;
	struct item_data *i_data;

	item_id	= script_getnum(st,2);
	n	= script_getnum(st,3);
	value	= script_getnum(st,4);
	i_data = itemdb_exists(item_id);

	if (i_data && n>=0 && n<=14) {
		item_arr = (int*)&i_data->value_buy;
		item_arr[n] = value;
		script_pushint(st,value);
	} else
		script_pushint(st,-1);
	return 0;
}

/*==========================================
 * Returns value from equipped item slot n [Lupus]
	getequipcardid(num,slot)
	where
		num = eqip position slot
		slot = 0,1,2,3 (Card Slot N)

	This func returns CARD ID, 255,254,-255 (for card 0, if the item is produced)
		it's useful when you want to check item cards or if it's signed
	Useful for such quests as "Sign this refined item with players name" etc
		Hat[0] +4 -> Player's Hat[0] +4
 *------------------------------------------*/
BUILDIN_FUNC(getequipcardid)
{
	int i=-1,num,slot;
	TBL_PC *sd;

	num=script_getnum(st,2);
	slot=script_getnum(st,3);
	sd=script_rid2sd(st);
	if (num > 0 && num <= ARRAYLENGTH(equip))
		i=pc_checkequip(sd,equip[num-1],false);
	if(i >= 0 && slot>=0 && slot<4)
		script_pushint(st,sd->inventory.u.items_inventory[i].card[slot]);
	else
		script_pushint(st,0);

	return 0;
}

/*==========================================
 * petskillbonus [Valaris] //Rewritten by [Skotlex]
 *------------------------------------------*/
BUILDIN_FUNC(petskillbonus)
{
	struct pet_data *pd;

	TBL_PC *sd=script_rid2sd(st);

	if(sd==NULL || sd->pd==NULL)
		return 0;

	pd=sd->pd;
	if (pd->bonus)
	{ //Clear previous bonus
		if (pd->bonus->timer != INVALID_TIMER)
			delete_timer(pd->bonus->timer, pet_skill_bonus_timer);
	} else //init
		pd->bonus = (struct pet_bonus *) aMalloc(sizeof(struct pet_bonus));

	pd->bonus->type=script_getnum(st,2);
	pd->bonus->val=script_getnum(st,3);
	pd->bonus->duration=script_getnum(st,4);
	pd->bonus->delay=script_getnum(st,5);

	if (pd->state.skillbonus == 1)
		pd->state.skillbonus=0;	// waiting state

	// wait for timer to start
	if (battle_config.pet_equip_required && pd->pet.equip == 0)
		pd->bonus->timer = INVALID_TIMER;
	else
		pd->bonus->timer = add_timer(gettick()+pd->bonus->delay*1000, pet_skill_bonus_timer, sd->bl.id, 0);

	return 0;
}

/*==========================================
 * pet looting [Valaris] //Rewritten by [Skotlex]
 *------------------------------------------*/
BUILDIN_FUNC(petloot)
{
	int max;
	struct pet_data *pd;
	TBL_PC *sd=script_rid2sd(st);
	
	if(sd==NULL || sd->pd==NULL)
		return 0;

	max=script_getnum(st,2);

	if(max < 1)
		max = 1;	//Let'em loot at least 1 item.
	else if (max > MAX_PETLOOT_SIZE)
		max = MAX_PETLOOT_SIZE;
	
	pd = sd->pd;
	if (pd->loot != NULL)
	{	//Release whatever was there already and reallocate memory
		pet_lootitem_drop(pd, pd->msd);
		aFree(pd->loot->item);
	}
	else
		pd->loot = (struct pet_loot *)aMalloc(sizeof(struct pet_loot));

	pd->loot->item = (struct item *)aCalloc(max,sizeof(struct item));
	
	pd->loot->max=max;
	pd->loot->count = 0;
	pd->loot->weight = 0;

	return 0;
}
/*==========================================
 * PC�̏����i���ǂݎ��
 *------------------------------------------*/
BUILDIN_FUNC(getinventorylist)
{
	TBL_PC *sd=script_rid2sd(st);
	char card_var[NAME_LENGTH];
	
	int i,j=0,k;
	if(!sd) return 0;
	for(i=0;i<MAX_INVENTORY;i++){
		if(sd->inventory.u.items_inventory[i].nameid > 0 && sd->inventory.u.items_inventory[i].amount > 0){
			pc_setreg(sd,reference_uid(add_str("@inventorylist_id"), j),sd->inventory.u.items_inventory[i].nameid);
			pc_setreg(sd,reference_uid(add_str("@inventorylist_amount"), j),sd->inventory.u.items_inventory[i].amount);
			pc_setreg(sd,reference_uid(add_str("@inventorylist_equip"), j),sd->inventory.u.items_inventory[i].equip);
			pc_setreg(sd,reference_uid(add_str("@inventorylist_refine"), j),sd->inventory.u.items_inventory[i].refine);
			pc_setreg(sd,reference_uid(add_str("@inventorylist_identify"), j),sd->inventory.u.items_inventory[i].identify);
			pc_setreg(sd,reference_uid(add_str("@inventorylist_attribute"), j),sd->inventory.u.items_inventory[i].attribute);
			for (k = 0; k < MAX_SLOTS; k++)
			{
				sprintf(card_var, "@inventorylist_card%d",k+1);
				pc_setreg(sd,reference_uid(add_str(card_var), j),sd->inventory.u.items_inventory[i].card[k]);
			}
			pc_setreg(sd,reference_uid(add_str("@inventorylist_expire"), j),sd->inventory.u.items_inventory[i].expire_time);
			pc_setreg(sd,reference_uid(add_str("@inventorylist_bound"), j),sd->inventory.u.items_inventory[i].bound);
			j++;
		}
	}
	pc_setreg(sd,add_str("@inventorylist_count"),j);
	return 0;
}

BUILDIN_FUNC(getskilllist)
{
	TBL_PC *sd=script_rid2sd(st);
	int i,j=0;
	if(!sd) return 0;
	for(i=0;i<MAX_SKILL;i++){
		if(sd->status.skill[i].id > 0 && sd->status.skill[i].lv > 0){
			pc_setreg(sd,reference_uid(add_str("@skilllist_id"), j),sd->status.skill[i].id);
			pc_setreg(sd,reference_uid(add_str("@skilllist_lv"), j),sd->status.skill[i].lv);
			pc_setreg(sd,reference_uid(add_str("@skilllist_flag"), j),sd->status.skill[i].flag);
			j++;
		}
	}
	pc_setreg(sd,add_str("@skilllist_count"),j);
	return 0;
}

BUILDIN_FUNC(clearitem)
{
	TBL_PC *sd=script_rid2sd(st);
	int i;
	if(sd==NULL) return 0;
	for (i=0; i<MAX_INVENTORY; i++) {
		if (sd->inventory.u.items_inventory[i].amount) {

			//Logs items, got from (N)PC scripts [Lupus]
			log_pick(&sd->bl, LOG_TYPE_SCRIPT, sd->inventory.u.items_inventory[i].nameid, -sd->inventory.u.items_inventory[i].amount, &sd->inventory.u.items_inventory[i]);

			pc_delitem(sd, i, sd->inventory.u.items_inventory[i].amount, 0, 0);
		}
	}
	return 0;
}

/*==========================================
 * Disguise Player (returns Mob/NPC ID if success, 0 on fail)
 *------------------------------------------*/
BUILDIN_FUNC(disguise)
{
	int id;
	TBL_PC* sd = script_rid2sd(st);
	if (sd == NULL) return 0;

	id = script_getnum(st,2);

	if (mobdb_checkid(id) || npcdb_checkid(id)) {
		pc_disguise(sd, id);
		script_pushint(st,id);
	} else
		script_pushint(st,0);

	return 0;
}

/*==========================================
 * Undisguise Player (returns 1 if success, 0 on fail)
 *------------------------------------------*/
BUILDIN_FUNC(undisguise)
{
	TBL_PC* sd = script_rid2sd(st);
	if (sd == NULL) return 0;

	if (sd->disguise) {
		pc_disguise(sd, 0);
		script_pushint(st,0);
	} else {
		script_pushint(st,1);
	}
	return 0;
}

/**
* Transform an NPC to another _class
*
* classchange(<view id>{,"<NPC name>","<flag>"});
* @param flag: Specify target
*   BC_AREA - Sprite is sent to players in the vicinity of the source (default).
*   BC_SELF - Sprite is sent only to player attached.
*/
BUILDIN_FUNC(classchange)
{
	int _class, type = 1;
	struct npc_data* nd = NULL;
	TBL_PC *sd = map_id2sd(st->rid);
	send_target target = AREA;

	_class = script_getnum(st, 2);

	if (script_hasdata(st, 3) && strlen(script_getstr(st, 3)) > 0)
		nd = npc_name2id(script_getstr(st, 3));
	else
		nd = (struct npc_data *)map_id2bl(st->oid);

	if (nd == NULL)
		return 0;

	if (script_hasdata(st, 4)) {
		switch (script_getnum(st, 4)) {
		case BC_SELF:	target = SELF;			break;
		case BC_AREA:
		default:		target = AREA;			break;
		}
	}
	if (target != SELF)
		clif_class_change(&nd->bl, _class, type);
	else if (sd == NULL)
		return 1;
	else
		clif_class_change_target(&nd->bl, _class, type, target, sd);

	return 0;
}

/*==========================================
 * NPC���甭������G�t�F�N�g
 *------------------------------------------*/
BUILDIN_FUNC(misceffect)
{
	int type;

	type=script_getnum(st,2);
	if(st->oid && st->oid != fake_nd->bl.id) {
		struct block_list *bl = map_id2bl(st->oid);
		if (bl)
			clif_specialeffect(bl,type,AREA);
	} else{
		TBL_PC *sd=script_rid2sd(st);
		if(sd)
			clif_specialeffect(&sd->bl,type,AREA);
	}
	return 0;
}
/*==========================================
 * Play a BGM on a single client [Rikter/Yommy]
 *------------------------------------------*/
BUILDIN_FUNC(playBGM)
{
	const char* name;
	struct map_session_data* sd;

	if( ( sd = script_rid2sd(st) ) != NULL )
	{
		name = script_getstr(st,2);

		clif_playBGM(sd, name);
	}

	return 0;
}

static int playBGM_sub(struct block_list* bl,va_list ap)
{
	const char* name = va_arg(ap,const char*);

	clif_playBGM(BL_CAST(BL_PC, bl), name);

	return 0;
}

static int playBGM_foreachpc_sub(struct map_session_data* sd, va_list args)
{
	const char* name = va_arg(args, const char*);

	clif_playBGM(sd, name);
	return 0;
}

/*==========================================
 * Play a BGM on multiple client [Rikter/Yommy]
 *------------------------------------------*/
BUILDIN_FUNC(playBGMall)
{
	const char* name;

	name = script_getstr(st,2);

	if( script_hasdata(st,7) )
	{// specified part of map
		const char* map = script_getstr(st,3);
		int x0 = script_getnum(st,4);
		int y0 = script_getnum(st,5);
		int x1 = script_getnum(st,6);
		int y1 = script_getnum(st,7);

		map_foreachinarea(playBGM_sub, map_mapname2mapid(map), x0, y0, x1, y1, BL_PC, name);
	}
	else if( script_hasdata(st,3) )
	{// entire map
		const char* map = script_getstr(st,3);

		map_foreachinmap(playBGM_sub, map_mapname2mapid(map), BL_PC, name);
	}
	else
	{// entire server
		map_foreachpc(&playBGM_foreachpc_sub, name);
	}

	return 0;
}

/*==========================================
 * �T�E���h�G�t�F�N�g
 *------------------------------------------*/
BUILDIN_FUNC(soundeffect)
{
	TBL_PC* sd = script_rid2sd(st);
	const char* name = script_getstr(st,2);
	int type = script_getnum(st,3);

	if(sd)
	{
		clif_soundeffect(sd,&sd->bl,name,type);
	}
	return 0;
}

int soundeffect_sub(struct block_list* bl,va_list ap)
{
	char* name = va_arg(ap,char*);
	int type = va_arg(ap,int);

	clif_soundeffect((TBL_PC *)bl, bl, name, type);

    return 0;	
}

/*==========================================
 * Play a sound effect (.wav) on multiple clients
 * soundeffectall "<filepath>",<type>{,"<map name>"}{,<x0>,<y0>,<x1>,<y1>};
 *------------------------------------------*/
BUILDIN_FUNC(soundeffectall)
{
	struct block_list* bl;
	const char* name;
	int type;

	bl = (st->rid) ? &(script_rid2sd(st)->bl) : map_id2bl(st->oid);
	if (!bl)
		return 0;

	name = script_getstr(st,2);
	type = script_getnum(st,3);

	//FIXME: enumerating map squares (map_foreach) is slower than enumerating the list of online players (map_foreachpc?) [ultramage]

	if(!script_hasdata(st,4))
	{	// area around
		clif_soundeffectall(bl, name, type, AREA);
	}
	else
	if(!script_hasdata(st,5))
	{	// entire map
		const char* map = script_getstr(st,4);
		map_foreachinmap(soundeffect_sub, map_mapname2mapid(map), BL_PC, name, type);
	}
	else
	if(script_hasdata(st,8))
	{	// specified part of map
		const char* map = script_getstr(st,4);
		int x0 = script_getnum(st,5);
		int y0 = script_getnum(st,6);
		int x1 = script_getnum(st,7);
		int y1 = script_getnum(st,8);
		map_foreachinarea(soundeffect_sub, map_mapname2mapid(map), x0, y0, x1, y1, BL_PC, name, type);
	}
	else
	{
		ShowError("buildin_soundeffectall: insufficient arguments for specific area broadcast.\n");
	}

	return 0;
}
/*==========================================
 * pet status recovery [Valaris] / Rewritten by [Skotlex]
 *------------------------------------------*/
BUILDIN_FUNC(petrecovery)
{
	struct pet_data *pd;
	TBL_PC *sd=script_rid2sd(st);

	if(sd==NULL || sd->pd==NULL)
		return 0;

	pd=sd->pd;
	
	if (pd->recovery)
	{ //Halt previous bonus
		if (pd->recovery->timer != INVALID_TIMER)
			delete_timer(pd->recovery->timer, pet_recovery_timer);
	} else //Init
		pd->recovery = (struct pet_recovery *)aMalloc(sizeof(struct pet_recovery));
		
	pd->recovery->type = (sc_type)script_getnum(st,2);
	pd->recovery->delay = script_getnum(st,3);
	pd->recovery->timer = INVALID_TIMER;

	return 0;
}

/*==========================================
 * pet healing [Valaris] //Rewritten by [Skotlex]
 *------------------------------------------*/
BUILDIN_FUNC(petheal)
{
	struct pet_data *pd;
	TBL_PC *sd=script_rid2sd(st);

	if(sd==NULL || sd->pd==NULL)
		return 0;

	pd=sd->pd;
	if (pd->s_skill)
	{ //Clear previous skill
		if (pd->s_skill->timer != INVALID_TIMER)
		{
			if (pd->s_skill->id)
				delete_timer(pd->s_skill->timer, pet_skill_support_timer);
			else
				delete_timer(pd->s_skill->timer, pet_heal_timer);
		}
	} else //init memory
		pd->s_skill = (struct pet_skill_support *) aMalloc(sizeof(struct pet_skill_support)); 
	
	pd->s_skill->id=0; //This id identifies that it IS petheal rather than pet_skillsupport
	//Use the lv as the amount to heal
	pd->s_skill->lv=script_getnum(st,2);
	pd->s_skill->delay=script_getnum(st,3);
	pd->s_skill->hp=script_getnum(st,4);
	pd->s_skill->sp=script_getnum(st,5);

	//Use delay as initial offset to avoid skill/heal exploits
	if (battle_config.pet_equip_required && pd->pet.equip == 0)
		pd->s_skill->timer = INVALID_TIMER;
	else
		pd->s_skill->timer = add_timer(gettick()+pd->s_skill->delay*1000,pet_heal_timer,sd->bl.id,0);

	return 0;
}

/*==========================================
 * pet attack skills [Valaris] //Rewritten by [Skotlex]
 *------------------------------------------*/
/// petskillattack <skill id>,<level>,<rate>,<bonusrate>
/// petskillattack "<skill name>",<level>,<rate>,<bonusrate>
BUILDIN_FUNC(petskillattack)
{
	struct pet_data *pd;
	TBL_PC *sd=script_rid2sd(st);

	if(sd==NULL || sd->pd==NULL)
		return 0;

	pd=sd->pd;
	if (pd->a_skill == NULL)
		pd->a_skill = (struct pet_skill_attack *)aMalloc(sizeof(struct pet_skill_attack));
				
	pd->a_skill->id=( script_isstring(st,2) ? skill_name2id(script_getstr(st,2)) : script_getnum(st,2) );
	pd->a_skill->lv=script_getnum(st,3);
	pd->a_skill->div_ = 0;
	pd->a_skill->rate=script_getnum(st,4);
	pd->a_skill->bonusrate=script_getnum(st,5);

	return 0;
}

/*==========================================
 * pet attack skills [Valaris]
 *------------------------------------------*/
/// petskillattack2 <skill id>,<level>,<div>,<rate>,<bonusrate>
/// petskillattack2 "<skill name>",<level>,<div>,<rate>,<bonusrate>
BUILDIN_FUNC(petskillattack2)
{
	struct pet_data *pd;
	TBL_PC *sd=script_rid2sd(st);

	if(sd==NULL || sd->pd==NULL)
		return 0;

	pd=sd->pd;
	if (pd->a_skill == NULL)
		pd->a_skill = (struct pet_skill_attack *)aMalloc(sizeof(struct pet_skill_attack));
				
	pd->a_skill->id=( script_isstring(st,2) ? skill_name2id(script_getstr(st,2)) : script_getnum(st,2) );
	pd->a_skill->lv=script_getnum(st,3);
	pd->a_skill->div_ = script_getnum(st,4);
	pd->a_skill->rate=script_getnum(st,5);
	pd->a_skill->bonusrate=script_getnum(st,6);

	return 0;
}

/*==========================================
 * pet support skills [Skotlex]
 *------------------------------------------*/
/// petskillsupport <skill id>,<level>,<delay>,<hp>,<sp>
/// petskillsupport "<skill name>",<level>,<delay>,<hp>,<sp>
BUILDIN_FUNC(petskillsupport)
{
	struct pet_data *pd;
	TBL_PC *sd=script_rid2sd(st);

	if(sd==NULL || sd->pd==NULL)
		return 0;

	pd=sd->pd;
	if (pd->s_skill)
	{ //Clear previous skill
		if (pd->s_skill->timer != INVALID_TIMER)
		{
			if (pd->s_skill->id)
				delete_timer(pd->s_skill->timer, pet_skill_support_timer);
			else
				delete_timer(pd->s_skill->timer, pet_heal_timer);
		}
	} else //init memory
		pd->s_skill = (struct pet_skill_support *) aMalloc(sizeof(struct pet_skill_support)); 
	
	pd->s_skill->id=( script_isstring(st,2) ? skill_name2id(script_getstr(st,2)) : script_getnum(st,2) );
	pd->s_skill->lv=script_getnum(st,3);
	pd->s_skill->delay=script_getnum(st,4);
	pd->s_skill->hp=script_getnum(st,5);
	pd->s_skill->sp=script_getnum(st,6);

	//Use delay as initial offset to avoid skill/heal exploits
	if (battle_config.pet_equip_required && pd->pet.equip == 0)
		pd->s_skill->timer = INVALID_TIMER;
	else
		pd->s_skill->timer = add_timer(gettick()+pd->s_skill->delay*1000,pet_skill_support_timer,sd->bl.id,0);

	return 0;
}

/*==========================================
 * Scripted skill effects [Celest]
 *------------------------------------------*/
/// skilleffect <skill id>,<level>
/// skilleffect "<skill name>",<level>
BUILDIN_FUNC(skilleffect)
{
	TBL_PC *sd;

	int skillid=( script_isstring(st,2) ? skill_name2id(script_getstr(st,2)) : script_getnum(st,2) );
	int skilllv=script_getnum(st,3);
	sd=script_rid2sd(st);

	clif_skill_nodamage(&sd->bl,&sd->bl,skillid,skilllv,1);

	return 0;
}

/*==========================================
 * NPC skill effects [Valaris]
 *------------------------------------------*/
/// npcskilleffect <skill id>,<level>,<x>,<y>
/// npcskilleffect "<skill name>",<level>,<x>,<y>
BUILDIN_FUNC(npcskilleffect)
{
	struct block_list *bl= map_id2bl(st->oid);

	int skillid=( script_isstring(st,2) ? skill_name2id(script_getstr(st,2)) : script_getnum(st,2) );
	int skilllv=script_getnum(st,3);
	int x=script_getnum(st,4);
	int y=script_getnum(st,5);

	if (bl)
		clif_skill_poseffect(bl,skillid,skilllv,x,y,gettick());

	return 0;
}

/* Consumes an item.
 * consumeitem <item id>{,<char_id>};
 * consumeitem "<item name>"{,<char_id>};
 * @param item: Item ID or name
 */
BUILDIN_FUNC(consumeitem)
{
	TBL_PC *sd;
	struct script_data *data;
	struct item_data *item_data;

	if (!script_charid2sd(3, sd))
		return 1;

	data = script_getdata( st, 2 );
	get_val( st, data );

	if( data_isstring( data ) ){
		const char *name = conv_str( st, data );

		if( ( item_data = itemdb_searchname( name ) ) == NULL ){
			ShowError( "buildin_consumeitem: Nonexistant item %s requested.\n", name );
			return 1;
		}
	}else if( data_isint( data ) ){
		unsigned short nameid = conv_num( st, data );

		if( ( item_data = itemdb_exists( nameid ) ) == NULL ){
			ShowError("buildin_consumeitem: Nonexistant item %hu requested.\n", nameid );
			return 1;
		}
	}else{
		ShowError("buildin_consumeitem: invalid data type for argument #1 (%d).\n", data->type );
		return 1;
	}

	run_script( item_data->script, 0, sd->bl.id, 0 );
	return 0;
}

/*==========================================
 * Special effects [Valaris]
 *------------------------------------------*/
BUILDIN_FUNC(specialeffect)
{
	struct block_list *bl=map_id2bl(st->oid);
	int type = script_getnum(st,2);
	enum send_target target = script_hasdata(st,3) ? (send_target)script_getnum(st,3) : AREA;

	if(bl==NULL)
		return 0;

	if( script_hasdata(st,4) )
	{
		TBL_NPC *nd = npc_name2id(script_getstr(st,4));
		if(nd)
			clif_specialeffect(&nd->bl, type, target);
	}
	else
	{
		if (target == SELF) {
			TBL_PC *sd=script_rid2sd(st);
			if (sd)
				clif_specialeffect_single(bl,type,sd->fd);
		} else {
			clif_specialeffect(bl, type, target);
		}
	}

	return 0;
}

BUILDIN_FUNC(specialeffect2)
{
	TBL_PC *sd=script_rid2sd(st);
	int type = script_getnum(st,2);
	enum send_target target = script_hasdata(st,3) ? (send_target)script_getnum(st,3) : AREA;

	if( script_hasdata(st,4) )
		sd = map_nick2sd(script_getstr(st,4));

	if (sd)
		clif_specialeffect(&sd->bl, type, target);

	return 0;
}

/*==========================================
 * Nude [Valaris]
 *------------------------------------------*/
BUILDIN_FUNC(nude)
{
	TBL_PC *sd=script_rid2sd(st);
	int i,calcflag=0;

	if(sd==NULL)
		return 0;

	for(i=0;i<11;i++)
		if(sd->equip_index[i] >= 0) {
			if(!calcflag)
				calcflag=1;
			pc_unequipitem(sd,sd->equip_index[i],2);
		}

	if(calcflag)
		status_calc_pc(sd,0);

	return 0;
}

/*==========================================
 * gmcommand [MouseJstr]
 *
 * suggested on the forums...
 * splitted into atcommand & charcommand by [Skotlex]
 *------------------------------------------*/
BUILDIN_FUNC(atcommand)
{
	TBL_PC dummy_sd;
	TBL_PC* sd;
	int fd;
	const char* cmd;

	cmd = script_getstr(st,2);

	if (st->rid) {
		sd = script_rid2sd(st);
		fd = sd->fd;
	} else { //Use a dummy character.
		sd = &dummy_sd;
		fd = 0;

		memset(&dummy_sd, 0, sizeof(TBL_PC));
		if (st->oid)
		{
			struct block_list* bl = map_id2bl(st->oid);
			memcpy(&dummy_sd.bl, bl, sizeof(struct block_list));
			if (bl->type == BL_NPC)
				safestrncpy(dummy_sd.status.name, ((TBL_NPC*)bl)->name, NAME_LENGTH);
		}
	}

	// compatibility with previous implementation (deprecated!)
	if(cmd[0] != atcommand_symbol)
	{
		cmd += strlen(sd->status.name);
		while(*cmd != atcommand_symbol && *cmd != 0)
			cmd++;
	}

	is_atcommand(fd, sd, cmd, 0);
	return 0;
}

BUILDIN_FUNC(charcommand)
{
	TBL_PC dummy_sd;
	TBL_PC* sd;
	int fd;
	const char* cmd;

	cmd = script_getstr(st,2);

	if (st->rid) {
		sd = script_rid2sd(st);
		fd = sd->fd;
	} else { //Use a dummy character.
		sd = &dummy_sd;
		fd = 0;

		memset(&dummy_sd, 0, sizeof(TBL_PC));
		if (st->oid)
		{
			struct block_list* bl = map_id2bl(st->oid);
			memcpy(&dummy_sd.bl, bl, sizeof(struct block_list));
			if (bl->type == BL_NPC)
				safestrncpy(dummy_sd.status.name, ((TBL_NPC*)bl)->name, NAME_LENGTH);
		}
	}

	if (*cmd != charcommand_symbol) {
		ShowWarning("script: buildin_charcommand: No '#' symbol!\n");
		script_reportsrc(st);
		return 1;
	}
	
	is_atcommand(fd, sd, cmd, 0);
	return 0;
}

/*==========================================
 * Displays a message for the player only (like system messages like "you got an apple" )
 *------------------------------------------*/
BUILDIN_FUNC(dispbottom)
{
	TBL_PC *sd=script_rid2sd(st);
	const char *message;
	message=script_getstr(st,2);
	if(sd)
		clif_disp_onlyself(sd,message,(int)strlen(message));
	return 0;
}

/*==========================================
 * Displays a colored message for the player only (like system messages like "you got an apple" )
 * Format : dispbottom2("0xFF00FF","Message"{,"Player Name"});
 *------------------------------------------*/
BUILDIN_FUNC(dispbottom2)
{
	TBL_PC *sd=script_rid2sd(st); //Player Data
	const char *message; //Message to Display
	unsigned long color; //Color to display
	message=script_getstr(st,3);
	color=strtoul(script_getstr(st,2),NULL,0);
	if(script_hasdata(st,4)){
		const char* player;
		TBL_PC *tsd;
		player = script_getstr(st,2);
		tsd=map_nick2sd((char *) player);
		if (tsd)
			clif_displaymessagecolor(tsd,message,color);
		return true;
	}
	if(sd)
		clif_displaymessagecolor(sd,message,color);
	return true;
}

/*==========================================
 * All The Players Full Recovery
 * (HP/SP full restore and resurrect if need)
 *------------------------------------------*/
BUILDIN_FUNC(recovery)
{
	TBL_PC* sd;
	struct s_mapiterator* iter;

	iter = mapit_getallusers();
	for( sd = (TBL_PC*)mapit_first(iter); mapit_exists(iter); sd = (TBL_PC*)mapit_next(iter) )
	{
		if(pc_isdead(sd))
			status_revive(&sd->bl, 100, 100);
		else
			status_percent_heal(&sd->bl, 100, 100);
		clif_displaymessage(sd->fd,"You have been recovered!");
	}
	mapit_free(iter);
	return 0;
}
/*==========================================
 * Get your pet info: getpetinfo(n)  
 * n -> 0:pet_id 1:pet_class 2:pet_name
 * 3:friendly 4:hungry, 5: rename flag.
 *------------------------------------------*/
BUILDIN_FUNC(getpetinfo)
{
	TBL_PC *sd=script_rid2sd(st);
	TBL_PET *pd;
	int type=script_getnum(st,2);
	
	if(!sd || !sd->pd) {
		if (type == 2)
			script_pushconststr(st,"null");
		else
			script_pushint(st,0);
		return 0;
	}
	pd = sd->pd;
	switch(type){
		case 0: script_pushint(st,pd->pet.pet_id); break;
		case 1: script_pushint(st,pd->pet.class_); break;
		case 2: script_pushstrcopy(st,pd->pet.name); break;
		case 3: script_pushint(st,pd->pet.intimate); break;
		case 4: script_pushint(st,pd->pet.hungry); break;
		case 5: script_pushint(st,pd->pet.rename_flag); break;
		default:
			script_pushint(st,0);
			break;
	}
	return 0;
}

/*==========================================
 * Get your homunculus info: gethominfo(n)  
 * n -> 0:hom_id 1:class 2:name
 * 3:friendly 4:hungry, 5: rename flag.
 * 6: level
 *------------------------------------------*/
BUILDIN_FUNC(gethominfo)
{
	TBL_PC *sd=script_rid2sd(st);
	TBL_HOM *hd;
	int type=script_getnum(st,2);

	hd = sd?sd->hd:NULL;
	if(!merc_is_hom_active(hd))
	{
		if (type == 2)
			script_pushconststr(st,"null");
		else
			script_pushint(st,0);
		return 0;
	}

	switch(type){
		case 0: script_pushint(st,hd->homunculus.hom_id); break;
		case 1: script_pushint(st,hd->homunculus.class_); break;
		case 2: script_pushstrcopy(st,hd->homunculus.name); break;
		case 3: script_pushint(st,hd->homunculus.intimacy); break;
		case 4: script_pushint(st,hd->homunculus.hunger); break;
		case 5: script_pushint(st,hd->homunculus.rename_flag); break;
		case 6: script_pushint(st,hd->homunculus.level); break;
		default:
			script_pushint(st,0);
			break;
	}
	return 0;
}

/// Retrieves information about character's mercenary
/// getmercinfo <type>[,<char id>];
BUILDIN_FUNC(getmercinfo)
{
	int type, char_id;
	struct map_session_data* sd;
	struct mercenary_data* md;

	type = script_getnum(st,2);

	if( script_hasdata(st,3) )
	{
		char_id = script_getnum(st,3);

		if( ( sd = map_charid2sd(char_id) ) == NULL )
		{
			ShowError("buildin_getmercinfo: No such character (char_id=%d).\n", char_id);
			script_pushnil(st);
			return 1;
		}
	}
	else
	{
		if( ( sd = script_rid2sd(st) ) == NULL )
		{
			script_pushnil(st);
			return 0;
		}
	}

	md = ( sd->status.mer_id && sd->md ) ? sd->md : NULL;

	switch( type )
	{
		case 0: script_pushint(st,md ? md->mercenary.mercenary_id : 0); break;
		case 1: script_pushint(st,md ? md->mercenary.class_ : 0); break;
		case 2:
			if( md )
				script_pushstrcopy(st,md->db->name);
			else
				script_pushconststr(st,"");
			break;
		case 3: script_pushint(st,md ? mercenary_get_faith(md) : 0); break;
		case 4: script_pushint(st,md ? mercenary_get_calls(md) : 0); break;
		case 5: script_pushint(st,md ? md->mercenary.kill_count : 0); break;
		case 6: script_pushint(st,md ? mercenary_get_lifetime(md) : 0); break;
		case 7: script_pushint(st,md ? md->db->lv : 0); break;
		default:
			ShowError("buildin_getmercinfo: Invalid type %d (char_id=%d).\n", type, sd->status.char_id);
			script_pushnil(st);
			return 1;
	}

	return 0;
}

/*==========================================
 * Shows wether your inventory(and equips) contain
   selected card or not.
	checkequipedcard(4001);
 *------------------------------------------*/
BUILDIN_FUNC(checkequipedcard)
{
	TBL_PC *sd=script_rid2sd(st);
	int n,i,c=0;
	c=script_getnum(st,2);

	if(sd){
		for(i=0;i<MAX_INVENTORY;i++){
			if(sd->inventory.u.items_inventory[i].nameid > 0 && sd->inventory.u.items_inventory[i].amount && sd->inventory_data[i]){
				if (itemdb_isspecial(sd->inventory.u.items_inventory[i].card[0]))
					continue;
				for(n=0;n<sd->inventory_data[i]->slot;n++){
					if(sd->inventory.u.items_inventory[i].card[n]==c){
						script_pushint(st,1);
						return 0;
					}
				}
			}
		}
	}
	script_pushint(st,0);
	return 0;
}

BUILDIN_FUNC(jump_zero)
{
	int sel;
	sel=script_getnum(st,2);
	if(!sel) {
		int pos;
		if( !data_islabel(script_getdata(st,3)) ){
			ShowError("script: jump_zero: not label !\n");
			st->state=END;
			return 1;
		}

		pos=script_getnum(st,3);
		st->pos=pos;
		st->state=GOTO;
	}
	return 0;
}

/*==========================================
 * GetMapMobs
	returns mob counts on a set map:
	e.g. GetMapMobs("prontera")
	use "this" - for player's map
 *------------------------------------------*/
BUILDIN_FUNC(getmapmobs)
{
	const char *str=NULL;
	int m=-1,bx,by;
	int count=0;
	struct block_list *bl;

	str=script_getstr(st,2);

	if(strcmp(str,"this")==0){
		TBL_PC *sd=script_rid2sd(st);
		if(sd)
			m=sd->bl.m;
		else{
			script_pushint(st,-1);
			return 0;
		}
	}else
		m=map_mapname2mapid(str);

	if(m < 0){
		script_pushint(st,-1);
		return 0;
	}

	for(by=0;by<=(map[m].ys-1)/BLOCK_SIZE;by++)
		for(bx=0;bx<=(map[m].xs-1)/BLOCK_SIZE;bx++)
			for( bl = map[m].block_mob[bx+by*map[m].bxs] ; bl != NULL ; bl = bl->next )
				if(bl->x>=0 && bl->x<=map[m].xs-1 && bl->y>=0 && bl->y<=map[m].ys-1)
					count++;

	script_pushint(st,count);
	return 0;
}

/*==========================================
 * movenpc [MouseJstr]
 *------------------------------------------*/
BUILDIN_FUNC(movenpc)
{
	TBL_NPC *nd = NULL;
	const char *npc;
	int x,y;

	npc = script_getstr(st,2);
	x = script_getnum(st,3);
	y = script_getnum(st,4);

	if ((nd = npc_name2id(npc)) == NULL)
		return -1;

	if (script_hasdata(st,5))
		nd->ud.dir = script_getnum(st,5) % 8;
	npc_movenpc(nd, x, y);
	return 0;
}

/*==========================================
 * message [MouseJstr]
 *------------------------------------------*/
BUILDIN_FUNC(message)
{
	const char *msg,*player;
	TBL_PC *pl_sd = NULL;

	player = script_getstr(st,2);
	msg = script_getstr(st,3);

	if((pl_sd=map_nick2sd((char *) player)) == NULL)
		return 0;
	clif_displaymessage(pl_sd->fd, msg);

	return 0;
}

/*==========================================
* npctalk (sends message to surrounding area)
* usage: npctalk("<message>"{, "<npc name>"{, <hide_name>}});
*------------------------------------------*/
BUILDIN_FUNC(npctalk)
{
	struct npc_data* nd;
	const char *str = script_getstr(st, 2);
	bool hide_name = false;

	if (script_hasdata(st, 3)) {
		nd = npc_name2id(script_getstr(st, 3));
	}
	else {
		nd = map_id2nd(st->oid);
	}

	if (script_hasdata(st, 4)) {
		hide_name = (script_getnum(st, 4) == 0) ? true : false;
	}

	if (nd != NULL) {
		char name[NAME_LENGTH], message[256];
		safestrncpy(name, nd->name, sizeof(name));
		strtok(name, "#"); // discard extra name identifier if present
		if (hide_name) {
			safesnprintf(message, sizeof(message), "%s", str);
		}
		else {
			safesnprintf(message, sizeof(message), "%s : %s", name, str);
		}
		clif_disp_overhead(&nd->bl, message);
	}

	return 0;
}

// change npc walkspeed [Valaris]
BUILDIN_FUNC(npcspeed)
{
	struct npc_data* nd;
	int speed;

	speed = script_getnum(st,2);
	nd =(struct npc_data *)map_id2bl(st->oid);

	if( nd )
	{
		nd->speed = speed;
		nd->ud.state.speed_changed = 1;
	}

	return 0;
}
// make an npc walk to a position [Valaris]
BUILDIN_FUNC(npcwalkto)
{
	struct npc_data *nd=(struct npc_data *)map_id2bl(st->oid);
	int x=0,y=0;

	x=script_getnum(st,2);
	y=script_getnum(st,3);

	if(nd) {
		unit_walktoxy(&nd->bl,x,y,0);
	}

	return 0;
}
// stop an npc's movement [Valaris]
BUILDIN_FUNC(npcstop)
{
	struct npc_data *nd=(struct npc_data *)map_id2bl(st->oid);

	if(nd) {
		unit_stop_walking(&nd->bl,1|4);
	}

	return 0;
}


/*==========================================
 * getlook char info. getlook(arg)
 *------------------------------------------*/
BUILDIN_FUNC(getlook)
{
        int type,val;
        TBL_PC *sd;
        sd=script_rid2sd(st);

        type=script_getnum(st,2);
        val=-1;
        switch(type) {
        case LOOK_HAIR: val=sd->status.hair; break; //1
        case LOOK_WEAPON: val=sd->status.weapon; break; //2
        case LOOK_HEAD_BOTTOM: val=sd->status.head_bottom; break; //3
        case LOOK_HEAD_TOP: val=sd->status.head_top; break; //4
        case LOOK_HEAD_MID: val=sd->status.head_mid; break; //5
        case LOOK_HAIR_COLOR: val=sd->status.hair_color; break; //6
        case LOOK_CLOTHES_COLOR: val=sd->status.clothes_color; break; //7
        case LOOK_SHIELD: val=sd->status.shield; break; //8
        case LOOK_SHOES: break; //9
		case LOOK_BODY: break; //10
		//case LOOK_RESET_COSTUMES: break; //11 - Not sure what exactly this is used for.
		case LOOK_ROBE: val=sd->status.robe; break; //12
        }

        script_pushint(st,val);
        return 0;
}

/*==========================================
 *     get char save point. argument: 0- map name, 1- x, 2- y
 *------------------------------------------*/
BUILDIN_FUNC(getsavepoint)
{
	TBL_PC* sd;
	int type;

	sd = script_rid2sd(st);
	if (sd == NULL) {
		script_pushint(st,0);
		return 0;
	}

	type = script_getnum(st,2);

	switch(type) {
		case 0: script_pushstrcopy(st,mapindex_id2name(sd->status.save_point.map)); break;
		case 1: script_pushint(st,sd->status.save_point.x); break;
		case 2: script_pushint(st,sd->status.save_point.y); break;
		default:
			script_pushint(st,0);
			break;
	}
	return 0;
}

/*==========================================
  * Get position for  char/npc/pet/mob objects. Added by Lorky
  *
  *     int getMapXY(MapName$,MapX,MapY,type,[CharName$]);
  *             where type:
  *                     MapName$ - String variable for output map name
  *                     MapX     - Integer variable for output coord X
  *                     MapY     - Integer variable for output coord Y
  *                     type     - type of object
  *                                0 - Character coord
  *                                1 - NPC coord
  *                                2 - Pet coord
  *                                3 - Mob coord (not released)
  *                                4 - Homun coord
  *                     CharName$ - Name object. If miss or "this" the current object
  *
  *             Return:
  *                     0        - success
  *                     -1       - some error, MapName$,MapX,MapY contains unknown value.
  *------------------------------------------*/
BUILDIN_FUNC(getmapxy)
{
	struct block_list *bl = NULL;
	TBL_PC *sd=NULL;

	int num;
	const char *name;
	char prefix;

	int x,y,type;
	char mapname[MAP_NAME_LENGTH];

	if( !data_isreference(script_getdata(st,2)) ){
		ShowWarning("script: buildin_getmapxy: not mapname variable\n");
		script_pushint(st,-1);
		return 1;
	}
	if( !data_isreference(script_getdata(st,3)) ){
		ShowWarning("script: buildin_getmapxy: not mapx variable\n");
		script_pushint(st,-1);
		return 1;
	}
	if( !data_isreference(script_getdata(st,4)) ){
		ShowWarning("script: buildin_getmapxy: not mapy variable\n");
		script_pushint(st,-1);
		return 1;
	}

	// Possible needly check function parameters on C_STR,C_INT,C_INT
	type=script_getnum(st,5);

	switch (type){
		case 0:	//Get Character Position
			if( script_hasdata(st,6) )
				sd=map_nick2sd(script_getstr(st,6));
			else
				sd=script_rid2sd(st);

			if (sd)
				bl = &sd->bl;
			break;
		case 1:	//Get NPC Position
			if( script_hasdata(st,6) )
			{
				struct npc_data *nd;
				nd=npc_name2id(script_getstr(st,6));
				if (nd)
					bl = &nd->bl;
			} else //In case the origin is not an npc?
				bl=map_id2bl(st->oid);
			break;
		case 2:	//Get Pet Position
			if(script_hasdata(st,6))
				sd=map_nick2sd(script_getstr(st,6));
			else
				sd=script_rid2sd(st);

			if (sd && sd->pd)
				bl = &sd->pd->bl;
			break;
		case 3:	//Get Mob Position
			break; //Not supported?
		case 4:	//Get Homun Position
			if(script_hasdata(st,6))
				sd=map_nick2sd(script_getstr(st,6));
			else
				sd=script_rid2sd(st);

			if (sd && sd->hd)
				bl = &sd->hd->bl;
			break;
		default:
			ShowWarning("script: buildin_getmapxy: Invalid type %d\n", type);
			script_pushint(st,-1);
			return 1;
	}
	if (!bl) { //No object found.
		script_pushint(st,-1);
		return 0;
	}

	x= bl->x;
	y= bl->y;
	safestrncpy(mapname, map[bl->m].name, MAP_NAME_LENGTH);
	
	//Set MapName$
	num=st->stack->stack_data[st->start+2].u.num;
	name=get_str(num&0x00ffffff);
	prefix=*name;

	if(not_server_variable(prefix))
		sd=script_rid2sd(st);
	else
		sd=NULL;
	set_reg(st,sd,num,name,(void*)mapname,script_getref(st,2));

	//Set MapX
	num=st->stack->stack_data[st->start+3].u.num;
	name=get_str(num&0x00ffffff);
	prefix=*name;

	if(not_server_variable(prefix))
		sd=script_rid2sd(st);
	else
		sd=NULL;
	set_reg(st,sd,num,name,(void*)x,script_getref(st,3));

	//Set MapY
	num=st->stack->stack_data[st->start+4].u.num;
	name=get_str(num&0x00ffffff);
	prefix=*name;

	if(not_server_variable(prefix))
		sd=script_rid2sd(st);
	else
		sd=NULL;
	set_reg(st,sd,num,name,(void*)y,script_getref(st,4));

	//Return Success value
	script_pushint(st,0);
	return 0;
}

/*==========================================
 * Allows player to write NPC logs (i.e. Bank NPC, etc) [Lupus]
 *------------------------------------------*/
BUILDIN_FUNC(logmes)
{
	const char *str;
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 1;

	str = script_getstr(st,2);
	log_npc(sd,str);
	return 0;
}

BUILDIN_FUNC(summon)
{
	int _class, timeout=0;
	const char *str,*event="";
	TBL_PC *sd;
	struct mob_data *md;
	int64 tick = gettick();

	sd=script_rid2sd(st);
	if (!sd) return 0;
	
	str	=script_getstr(st,2);
	_class=script_getnum(st,3);
	if( script_hasdata(st,4) )
		timeout=script_getnum(st,4);
	if( script_hasdata(st,5) ){
		event=script_getstr(st,5);
		check_event(st, event);
	}

	clif_skill_poseffect(&sd->bl,AM_CALLHOMUN,1,sd->bl.x,sd->bl.y,tick);

	md = mob_once_spawn_sub(&sd->bl, sd->bl.m, sd->bl.x, sd->bl.y, str, _class, event);
	if (md) {
		md->master_id=sd->bl.id;
		md->special_state.ai=1;
		if( md->deletetimer != INVALID_TIMER )
			delete_timer(md->deletetimer, mob_timer_delete);
		md->deletetimer = add_timer(tick+(timeout>0?timeout*1000:60000),mob_timer_delete,md->bl.id,0);
		mob_spawn (md); //Now it is ready for spawning.
		clif_specialeffect(&md->bl,344,AREA);
		sc_start4(&md->bl, SC_MODECHANGE, 100, 1, 0, MD_AGGRESSIVE, 0, 60000);
	}
	script_pushint(st, md->bl.id);

	return 0;
}

/*==========================================
 * Checks whether it is daytime/nighttime
 *------------------------------------------*/
BUILDIN_FUNC(isnight)
{
	script_pushint(st,(night_flag == 1));
	return 0;
}

/*================================================
 * Check how many items/cards in the list are
 * equipped - used for 2/15's cards patch [celest]
 *------------------------------------------------*/
BUILDIN_FUNC(isequippedcnt)
{
	TBL_PC *sd;
	int i, j, k, id = 1;
	int ret = 0;

	sd = script_rid2sd(st);
	if (!sd) { //If the player is not attached it is a script error anyway... but better prevent the map server from crashing...
		script_pushint(st,0);
		return 0;
	}
	
	for (i=0; id!=0; i++) {
		FETCH (i+2, id) else id = 0;
		if (id <= 0)
			continue;
		
		for (j=0; j<EQI_MAX; j++) {
			int index;
			index = sd->equip_index[j];
			if(index < 0) continue;
			if(j == EQI_HAND_R && sd->equip_index[EQI_HAND_L] == index) continue;
			if(j == EQI_HEAD_MID && sd->equip_index[EQI_HEAD_LOW] == index) continue;
			if(j == EQI_HEAD_TOP && (sd->equip_index[EQI_HEAD_MID] == index || sd->equip_index[EQI_HEAD_LOW] == index)) continue;
			if(j == EQI_COSTUME_HEAD_MID && sd->equip_index[EQI_COSTUME_HEAD_LOW] == index) continue;
			if(j == EQI_COSTUME_HEAD_TOP && (sd->equip_index[EQI_COSTUME_HEAD_MID] == index || sd->equip_index[EQI_COSTUME_HEAD_LOW] == index)) continue;
			
			if(!sd->inventory_data[index])
				continue;

			if (itemdb_type(id) != IT_CARD) { //No card. Count amount in inventory.
				if (sd->inventory_data[index]->nameid == id)
					ret+= sd->inventory.u.items_inventory[index].amount;
			} else { //Count cards.
				if (itemdb_isspecial(sd->inventory.u.items_inventory[index].card[0]))
					continue; //No cards
				for(k=0; k<sd->inventory_data[index]->slot; k++) {
					if (sd->inventory.u.items_inventory[index].card[k] == id) 
						ret++; //[Lupus]
				}				
			}
		}
	}
	
	script_pushint(st,ret);
	return 0;
}

/*================================================
 * Check whether another card has been
 * equipped - used for 2/15's cards patch [celest]
 * -- Items checked cannot be reused in another
 * card set to prevent exploits
 *------------------------------------------------*/
BUILDIN_FUNC(isequipped)
{
	TBL_PC *sd;
	int i, j, k, id = 1;
	int index, flag;
	int ret = -1;
	//Original hash to reverse it when full check fails.
	unsigned int setitem_hash = 0, setitem_hash2 = 0;

	sd = script_rid2sd(st);
	
	if (!sd) { //If the player is not attached it is a script error anyway... but better prevent the map server from crashing...
		script_pushint(st,0);
		return 0;
	}

	setitem_hash = sd->setitem_hash;
	setitem_hash2 = sd->setitem_hash2;
	for (i=0; id!=0; i++)
	{
		FETCH (i+2, id) else id = 0;
		if (id <= 0)
			continue;
		flag = 0;
		for (j=0; j<EQI_MAX; j++)
		{
			index = sd->equip_index[j];
			if(index < 0) continue;
			if(j == EQI_HAND_R && sd->equip_index[EQI_HAND_L] == index) continue;
			if(j == EQI_HEAD_MID && sd->equip_index[EQI_HEAD_LOW] == index) continue;
			if(j == EQI_HEAD_TOP && (sd->equip_index[EQI_HEAD_MID] == index || sd->equip_index[EQI_HEAD_LOW] == index)) continue;
			if(j == EQI_COSTUME_HEAD_MID && sd->equip_index[EQI_COSTUME_HEAD_LOW] == index) continue;
			if(j == EQI_COSTUME_HEAD_TOP && (sd->equip_index[EQI_COSTUME_HEAD_MID] == index || sd->equip_index[EQI_COSTUME_HEAD_LOW] == index)) continue;
	
			if(!sd->inventory_data[index])
				continue;
			
			if (itemdb_type(id) != IT_CARD) {
				if (sd->inventory_data[index]->nameid != id)
					continue;
				flag = 1;
				break;
			} else { //Cards
				if (sd->inventory_data[index]->slot == 0 ||
					itemdb_isspecial(sd->inventory.u.items_inventory[index].card[0]))
					continue;

				for (k = 0; k < sd->inventory_data[index]->slot; k++)
				{	//New hash system which should support up to 4 slots on any equipment. [Skotlex]
					unsigned int hash = 0;
					if (sd->inventory.u.items_inventory[index].card[k] != id)
						continue;

					hash = 1<<((j<5?j:j-5)*4 + k);
					// check if card is already used by another set
					if ((j<5?sd->setitem_hash:sd->setitem_hash2) & hash)	
						continue;

					// We have found a match
					flag = 1;
					// Set hash so this card cannot be used by another
					if (j<5)
						sd->setitem_hash |= hash;
					else
						sd->setitem_hash2 |= hash;
					break;
				}
			}
			if (flag) break; //Card found
		}
		if (ret == -1)
			ret = flag;
		else
			ret &= flag;
		if (!ret) break;
	}
	if (!ret)
  	{	//When check fails, restore original hash values. [Skotlex]
		sd->setitem_hash = setitem_hash;
		sd->setitem_hash2 = setitem_hash2;
	}
	script_pushint(st,ret);
	return 0;
}

/*================================================
 * Check how many given inserted cards in the CURRENT
 * weapon - used for 2/15's cards patch [Lupus]
 *------------------------------------------------*/
BUILDIN_FUNC(cardscnt)
{
	TBL_PC *sd;
	int i, k, id = 1;
	int ret = 0;
	int index;

	sd = script_rid2sd(st);
	
	for (i=0; id!=0; i++) {
		FETCH (i+2, id) else id = 0;
		if (id <= 0)
			continue;
		
		index = current_equip_item_index; //we get CURRENT WEAPON inventory index from status.c [Lupus]
		if(index < 0) continue;
			
		if(!sd->inventory_data[index])
			continue;

		if(itemdb_type(id) != IT_CARD) {
			if (sd->inventory_data[index]->nameid == id)
				ret+= sd->inventory.u.items_inventory[index].amount;
		} else {
			if (itemdb_isspecial(sd->inventory.u.items_inventory[index].card[0]))
				continue;
			for(k=0; k<sd->inventory_data[index]->slot; k++) {
				if (sd->inventory.u.items_inventory[index].card[k] == id)
					ret++;
			}				
		}
	}
	script_pushint(st,ret);
//	script_pushint(st,current_equip_item_index);
	return 0;
}

/*=======================================================
 * Returns the refined number of the current item, or an
 * item with inventory index specified
 *-------------------------------------------------------*/
BUILDIN_FUNC(getrefine)
{
	TBL_PC *sd;
	if ((sd = script_rid2sd(st))!= NULL)
		script_pushint(st,sd->inventory.u.items_inventory[current_equip_item_index].refine);
	else
		script_pushint(st,0);
	return 0;
}

/*=======================================================
 * Day/Night controls
 *-------------------------------------------------------*/
BUILDIN_FUNC(night)
{
	if (night_flag != 1) map_night_timer(night_timer_tid, 0, 0, 1);
	return 0;
}
BUILDIN_FUNC(day)
{
	if (night_flag != 0) map_day_timer(day_timer_tid, 0, 0, 1);
	return 0;
}

//=======================================================
// Unequip [Spectre]
//-------------------------------------------------------
BUILDIN_FUNC(unequip)
{
	int i;
	size_t num;
	TBL_PC *sd;

	num = script_getnum(st,2);
	sd = script_rid2sd(st);
	if( sd != NULL && num >= 1 && num <= ARRAYLENGTH(equip) )
	{
		i = pc_checkequip(sd,equip[num-1],false);
		if (i >= 0)
			pc_unequipitem(sd,i,1|2);
	}
	return 0;
}

BUILDIN_FUNC(equip)
{
	unsigned short nameid = 0;
	int i;
	TBL_PC *sd;
	struct item_data *item_data;

	sd = script_rid2sd(st);

	nameid=script_getnum(st,2);
	if((item_data = itemdb_exists(nameid)) == NULL)
	{
		ShowError("wrong item ID : equipitem(%hu)\n",nameid);
		return 1;
	}
	ARR_FIND( 0, MAX_INVENTORY, i, sd->inventory.u.items_inventory[i].nameid == nameid );
	if( i < MAX_INVENTORY )
		pc_equipitem(sd,i,item_data->equip,false);

	return 0;
}

BUILDIN_FUNC(autoequip)
{
	unsigned short nameid;
	int flag;
	struct item_data *item_data;
	nameid=script_getnum(st,2);
	flag=script_getnum(st,3);

	if( ( item_data = itemdb_exists(nameid) ) == NULL )
	{
		ShowError("buildin_autoequip: Invalid item '%hu'.\n", nameid);
		return 1;
	}

	if( !itemdb_isequip2(item_data) )
	{
		ShowError("buildin_autoequip: Item '%hu' cannot be equipped.\n", nameid);
		return 1;
	}

	item_data->flag.autoequip = flag>0?1:0;
	return 0;
}

BUILDIN_FUNC(setbattleflag)
{
	const char *flag, *value;

	flag = script_getstr(st,2);
	value = script_getstr(st,3);  // HACK: Retrieve number as string (auto-converted) for battle_set_value
	
	if (battle_set_value(flag, value) == 0)
		ShowWarning("buildin_setbattleflag: unknown battle_config flag '%s'\n",flag);
	else
		ShowInfo("buildin_setbattleflag: battle_config flag '%s' is now set to '%s'.\n",flag,value);

	return 0;
}

BUILDIN_FUNC(getbattleflag)
{
	const char *flag;
	flag = script_getstr(st,2);
	script_pushint(st,battle_get_value(flag));
	return 0;
}

//=======================================================
// strlen [Valaris]
//-------------------------------------------------------
BUILDIN_FUNC(getstrlen)
{

	const char *str = script_getstr(st,2);
	int len = (int)strlen(str);

	script_pushint(st,len);
	return 0;
}

//=======================================================
// isalpha [Valaris]
//-------------------------------------------------------
BUILDIN_FUNC(charisalpha)
{
	const char *str=script_getstr(st,2);
	int pos=script_getnum(st,3);

	int val = ( pos >= 0 && (unsigned int)pos < strlen(str) && ISALPHA(str[pos]) )? 1: 0;

	script_pushint(st,val);
	return 0;
}

//=======================================================
// charisupper <str>, <index>
//-------------------------------------------------------
BUILDIN_FUNC(charisupper)
{
	const char *str = script_getstr(st,2);
	int pos = script_getnum(st,3);

	int val = ( pos >= 0 && (unsigned int)pos < strlen(str) && ISUPPER(str[pos]) )? 1: 0;

	script_pushint(st,val);
	return 0;
}

//=======================================================
// charislower <str>, <index>
//-------------------------------------------------------
BUILDIN_FUNC(charislower)
{
	const char *str = script_getstr(st,2);
	int pos = script_getnum(st,3);

	int val = ( pos >= 0 && (unsigned int)pos < strlen(str) && ISLOWER(str[pos]) )? 1: 0;

	script_pushint(st,val);
	return 0;
}
//=======================================================
// charat <str>, <index>
//-------------------------------------------------------
BUILDIN_FUNC(charat)
{
	const char *str = script_getstr(st,2);
	int pos = script_getnum(st,3);

	if( pos >= 0 && (unsigned int)pos < strlen(str) )
	{
		char output[2];
		output[0] = str[pos];
		output[1] = '\0';
		script_pushstrcopy(st, output);
	}
	else
	{
		script_pushconststr(st, "");
	}

	return 0;
}

//=======================================================
// setchar <string>, <char>, <index>
//-------------------------------------------------------
BUILDIN_FUNC(setchar)
{
	const char *str = script_getstr(st,2);
	const char *c = script_getstr(st,3);
	int index = script_getnum(st,4);
	char *output = aStrdup(str);

	if( index >= 0 && (unsigned int)index < strlen(output) )
		output[index] = *c;

	script_pushstr(st, output);
	return 0;
}

//=======================================================
// insertchar <string>, <char>, <index>
//-------------------------------------------------------
BUILDIN_FUNC(insertchar)
{
	const char *str = script_getstr(st,2);
	const char *c = script_getstr(st,3);
	int index = script_getnum(st,4);
	char *output;
	size_t len = strlen(str);

	if(index < 0)
		index = 0;
	else if(index > len)
		index = len;

	output = (char*)aMalloc(len + 2);

	memcpy(output, str, index);
	output[index] = c[0];
	memcpy(&output[index+1], &str[index], len - index);
	output[len+1] = '\0';

	script_pushstr(st, output);
	return 0;
}

//=======================================================
// delchar <string>, <index>
//-------------------------------------------------------
BUILDIN_FUNC(delchar)
{
	const char *str = script_getstr(st,2);
	int index = script_getnum(st,3);
	char *output;
	size_t len = strlen(str);

	if( index < 0 || index >= len )
	{ // no change
		script_pushstrcopy(st, str);
		return 0;
	}

	output = (char*)aMalloc(len);

	memcpy(output, str, index);
	memcpy(&output[index], &str[index+1], len - index);

	script_pushstr(st, output);
	return 0;
}

//=======================================================
// strtoupper <str>
//-------------------------------------------------------
BUILDIN_FUNC(strtoupper)
{
	const char *str = script_getstr(st,2);
	char *output = aStrdup(str);
	char *cursor = output;

	while (*cursor != '\0') {
		*cursor = TOUPPER(*cursor);
		cursor++;
	}

	script_pushstr(st, output);
	return 0;
}

//=======================================================
// strtolower <str>
//-------------------------------------------------------
BUILDIN_FUNC(strtolower)
{
	const char *str = script_getstr(st,2);
	char *output = aStrdup(str);
	char *cursor = output;

	while (*cursor != '\0') {
		*cursor = TOLOWER(*cursor);
		cursor++;
	}

	script_pushstr(st, output);
	return 0;
}
//=======================================================
// substr <str>, <start>, <end>
//-------------------------------------------------------
BUILDIN_FUNC(substr)
{
	const char *str = script_getstr(st,2);
	int start = script_getnum(st,3);
	int end = script_getnum(st,4);

	if( start >= 0 && start <= end && (unsigned int)end < strlen(str) )
	{
		int len = end + 1 - start;
		char* output = (char*)aMalloc(len + 1);
		memcpy(output, &str[start], len);
		output[len] = '\0';
		script_pushstr(st, output);
	}
	else
	{
		script_pushconststr(st, "");
	}

	return 0;
}

//=======================================================
// explode <dest_string_array>, <str>, <delimiter>
// Note: delimiter is limited to 1 char
//-------------------------------------------------------
BUILDIN_FUNC(explode)
{
	struct script_data* data = script_getdata(st, 2);
	const char *str = script_getstr(st,3);
	const char delimiter = script_getstr(st, 4)[0];
	int32 id;
	size_t len = strlen(str);
	size_t i = 0, j = 0;
	int index;

	char *temp;
	const char* name;

	TBL_PC* sd = NULL;

	if( !data_isreference(data) )
	{
		ShowError("script:explode: not a variable\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not a variable
	}

	id = reference_getid(data);
	index = reference_getindex(data);
	name = reference_getname(data);

	if( not_array_variable(*name) )
	{
		ShowError("script:explode: illegal scope\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not supported
	}

	if( !is_string_variable(name) )
	{
		ShowError("script:explode: not string array\n");
		script_reportdata(data);
		st->state = END;
		return 1;// data type mismatch
	}

	if( not_server_variable(*name) )
	{
		sd = script_rid2sd(st);
		if( sd == NULL )
			return 0;// no player attached
	}

	temp = (char*)aMalloc(len + 1);

	for( i = 0; i < len; ++i )
	{
		if( index < SCRIPT_MAX_ARRAYSIZE-1 && str[i] == delimiter )
		{ // break string at delimiter while there is space in the array
			temp[j] = '\0';
			set_reg(st, sd, reference_uid(id, index), name, (void*)temp, reference_getref(data));
			++index;
			j = 0;
		}
		else
		{
			temp[j] = str[i];
			++j;
		}
	}
	//set last string
	temp[j] = '\0';
	set_reg(st, sd, reference_uid(id, index), name, (void*)temp, reference_getref(data));

	aFree(temp);
	return 0;
}
//=======================================================
// implode <string_array>
// implode <string_array>, <glue>
//-------------------------------------------------------
BUILDIN_FUNC(implode)
{
	struct script_data* data = script_getdata(st, 2);
	const char* name;
	int32 array_size, id;

	TBL_PC* sd = NULL;

	if( !data_isreference(data) )
	{
		ShowError("script:implode: not a variable\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not a variable
	}

	id = reference_getid(data);
	name = reference_getname(data);

	if( not_array_variable(*name) )
	{
		ShowError("script:implode: illegal scope\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not supported
	}

	if( !is_string_variable(name) )
	{
		ShowError("script:implode: not string array\n");
		script_reportdata(data);
		st->state = END;
		return 1;// data type mismatch
	}

	if( not_server_variable(*name) )
	{
		sd = script_rid2sd(st);
		if( sd == NULL )
			return 0;// no player attached
	}

	//count chars
	array_size = getarraysize(st, id, reference_getindex(data), is_string_variable(name), reference_getref(data));

	if( array_size < 0 || array_size >= SCRIPT_MAX_ARRAYSIZE )
	{
		ShowError("script:implode: invalid array length = %d\n", array_size);
		script_reportdata(data);
		st->state = END;
		return -1;
	}

	if( array_size == 0 ) //empty array check (AmsTaff)
	{
		ShowWarning("script:implode: array length = 0\n");
		script_reportdata(data);
		script_reportsrc(st);
		script_pushconststr(st, "NULL"); // XXX why return "NULL" for an empty array? [flaviojs]
	}
	else
	{
		const char* str[SCRIPT_MAX_ARRAYSIZE];
		size_t len[SCRIPT_MAX_ARRAYSIZE];
		size_t total_len = 0;
		const char* glue = "";
		size_t glue_len = 0;
		char *output;
		int i, k;

		// parse data
		for( i = 0; i < array_size; ++i )
		{
			str[i] = (const char*)get_val2(st, reference_uid(id, i), reference_getref(data)); // leave string data in the stack
			len[i] = strlen(str[i]);
			total_len += len[i];
		}

		if( script_hasdata(st,3) )
		{
			glue = script_getstr(st,3);
			glue_len = strlen(glue);
			total_len += glue_len * (array_size - 1);
		}

		//build output
		output = (char*)aMalloc(total_len + 1);
		for( i = 0, k = 0; i < array_size; ++i )
		{
			memcpy(&output[k], str[i], len[i]);
			k += len[i];
			if( glue_len > 0 && i < array_size - 1 )
			{
				memcpy(&output[k], glue, glue_len);
				k += glue_len;
			}
		}
		output[k] = '\0';
		script_removetop(st, -array_size, 0); // clear string data in the stack

		script_pushstr(st, output);
	}

	return 0;
}

//=======================================================
// sprintf(<format>, ...);
// Implements C sprintf, except format %n. The resulting string is
// returned, instead of being saved in variable by reference.
//-------------------------------------------------------
BUILDIN_FUNC(sprintf)
{
	unsigned int len, argc = 0, arg = 0, buf2_len = 0;
	const char* format;
	char* p;
	char* q;
	char* buf  = NULL;
	char* buf2 = NULL;
	struct script_data* data;
	StringBuf final_buf;

	// Fetch init data
	format = script_getstr(st, 2);
	argc = script_lastdata(st)-2;
	len = strlen(format);

	// Skip parsing, where no parsing is required.
	if(len == 0) {
		script_pushconststr(st,"");
		return 0;
	}

	// Pessimistic alloc
	CREATE(buf, char, len+1);

	// Need not be parsed, just solve stuff like %%.
	if(argc == 0) {
		memcpy(buf,format,len+1);
		script_pushstrcopy(st, buf);
		aFree(buf);
		return 0;
	}

	safestrncpy(buf, format, len+1);

	// Issue sprintf for each parameter
	StringBuf_Init(&final_buf);
	q = buf;
	while((p = strchr(q, '%')) != NULL) {
		if(p != q) {
			len = p - q + 1;

			if(buf2_len < len) {
				RECREATE(buf2, char, len);
				buf2_len = len;
			}

			safestrncpy(buf2, q, len);
			StringBuf_AppendStr(&final_buf, buf2);
			q = p;
		}

		p = q + 1;

		if(*p == '%') {  // %%
			StringBuf_AppendStr(&final_buf, "%");
			q+=2;
			continue;
		}

		if(*p == 'n') {  // %n
			ShowWarning("buildin_sprintf: Format %%n not supported! Skipping...\n");
			script_reportsrc(st);
			q+=2;
			continue;
		}

		if(arg >= argc) {
			ShowError("buildin_sprintf: Not enough arguments passed!\n");
			if(buf) aFree(buf);
			if(buf2) aFree(buf2);
			StringBuf_Destroy(&final_buf);
			script_pushconststr(st,"");
			return 1;
		}

		if((p = strchr(q+1, '%')) == NULL)
			p = strchr(q, 0);  // EOS

		len = p - q + 1;

		if(buf2_len < len) {
			RECREATE(buf2, char, len);
			buf2_len = len;
		}

		safestrncpy(buf2, q, len);
		q = p;

		// Note: This assumes the passed value being the correct
		// type to the current format specifier. If not, the server
		// probably crashes or returns anything else, than expected,
		// but it would behave in normal code the same way so it's
		// the scripter's responsibility.
		data = script_getdata(st, arg+3);

		if(data_isstring(data))  // String
			StringBuf_Printf(&final_buf, buf2, script_getstr(st, arg+3));
		else if(data_isint(data))  // Number
			StringBuf_Printf(&final_buf, buf2, script_getnum(st, arg+3));
		else if(data_isreference(data)) {  // Variable
			char* name = reference_getname(data);

			if(name[strlen(name)-1]=='$')  // var Str
				StringBuf_Printf(&final_buf, buf2, script_getstr(st, arg+3));
			else  // var Int
				StringBuf_Printf(&final_buf, buf2, script_getnum(st, arg+3));
		} else {  // Unsupported type
			ShowError("buildin_sprintf: Unknown argument type!\n");
			if(buf) aFree(buf);
			if(buf2) aFree(buf2);
			StringBuf_Destroy(&final_buf);
			script_pushconststr(st,"");
			return 1;
		}

		arg++;
	}

	// Append anything left
	if(*q)
		StringBuf_AppendStr(&final_buf, q);

	// Passed more, than needed
	if(arg < argc) {
		ShowWarning("buildin_sprintf: Unused arguments passed.\n");
		script_reportsrc(st);
	}

	script_pushstrcopy(st, StringBuf_Value(&final_buf));

	if(buf) aFree(buf);
	if(buf2) aFree(buf2);
	StringBuf_Destroy(&final_buf);

	return 0;
}

//=======================================================
// sscanf(<str>, <format>, ...);
// Implements C sscanf.
//-------------------------------------------------------
BUILDIN_FUNC(sscanf) {
	unsigned int argc, arg = 0, len;
	struct script_data* data;
	struct map_session_data* sd = NULL;
	const char* str;
	const char* format;
	const char* p;
	const char* q;
	char* buf = NULL;
	char* buf_p;
	char* ref_str = NULL;
	int ref_int;

	// Get data
	str = script_getstr(st, 2);
	format = script_getstr(st, 3);
	argc = script_lastdata(st) - 3;

	len = strlen(format);


	if (len != 0 && strlen(str) == 0) {
		// If the source string is empty but the format string is not, we return -1
		// according to the C specs. (if the format string is also empty, we shall
		// continue and return 0: 0 conversions took place out of the 0 attempted.)
		script_pushint(st, -1);
		return 0;
	}

	CREATE(buf, char, len * 2 + 1);

	// Issue sscanf for each parameter
	*buf = 0;
	q = format;
	while ((p = strchr(q, '%'))) {
		if (p != q) {
			strncat(buf, q, (size_t)(p - q));
			q = p;
		}
		p = q + 1;
		if (*p == '*' || *p == '%') {  // Skip
			strncat(buf, q, 2);
			q += 2;
			continue;
		}
		if (arg >= argc) {
			ShowError("buildin_sscanf: Not enough arguments passed!\n");
			script_pushint(st, -1);
			if (buf) aFree(buf);
			if (ref_str) aFree(ref_str);
			return 1;
		}
		if ((p = strchr(q + 1, '%')) == NULL) {
			p = strchr(q, 0);  // EOS
		}
		len = p - q;
		strncat(buf, q, len);
		q = p;

		// Validate output
		data = script_getdata(st, arg + 4);
		if (!data_isreference(data) || !reference_tovariable(data)) {
			ShowError("buildin_sscanf: Target argument is not a variable!\n");
			script_pushint(st, -1);
			if (buf) aFree(buf);
			if (ref_str) aFree(ref_str);
			return 1;
		}
		buf_p = reference_getname(data);
		if (not_server_variable(*buf_p) && !script_rid2sd(st)) {
			script_pushint(st, -1);
			if (buf) aFree(buf);
			if (ref_str) aFree(ref_str);
			return 0;
		}

		// Save value if any
		if (buf_p[strlen(buf_p) - 1] == '$') {  // String
			if (ref_str == NULL) {
				CREATE(ref_str, char, strlen(str) + 1);
			}
			if (sscanf(str, buf, ref_str) == 0) {
				break;
			}
			set_reg(st, sd, reference_uid(reference_getid(data), reference_getindex(data)), buf_p, (void*)ref_str, reference_getref(data));
		}
		else {  // Number
			if (sscanf(str, buf, &ref_int) == 0) {
				break;
			}
			set_reg(st, sd, reference_uid(reference_getid(data), reference_getindex(data)), buf_p, (void*)ref_int, reference_getref(data));
		}
		arg++;

		// Disable used format (%... -> %*...)
		buf_p = strchr(buf, 0);
		memmove(buf_p - len + 2, buf_p - len + 1, len);
		*(buf_p - len + 1) = '*';
	}

	script_pushint(st, arg);
	if (buf) aFree(buf);
	if (ref_str) aFree(ref_str);

	return 0;
}

//===============================================================
// replacestr <input>, <search>, <replace>{, <usecase>{, <count>}}
//
// Note: Finds all instances of <search> in <input> and replaces
// with <replace>. If specified will only replace as many
// instances as specified in <count>. By default will be case
// sensitive.
//---------------------------------------------------------------
BUILDIN_FUNC(replacestr)
{
	const char *input = script_getstr(st, 2);
	const char *find = script_getstr(st, 3);
	const char *replace = script_getstr(st, 4);
	size_t inputlen = strlen(input);
	size_t findlen = strlen(find);
	struct StringBuf output;
	bool usecase = true;

	int count = 0;
	int numFinds = 0;
	int i = 0, f = 0;

	if(findlen == 0) {
		ShowError("script:replacestr: Invalid search length.\n");
		st->state = END;
		return 1;
	}

	if(script_hasdata(st, 5)) {
		struct script_data *data = script_getdata(st, 5);

		get_val(st, data); // Convert into value in case of a variable
		if( !data_isstring(data) )
			usecase = script_getnum(st, 5) != 0;
		else {
			ShowError("script:replacestr: Invalid usecase value. Expected int got string\n");
			st->state = END;
			return 1;
		}
	}

	if(script_hasdata(st, 6)) {
		count = script_getnum(st, 6);
		if(count == 0) {
			ShowError("script:replacestr: Invalid count value. Expected int got string\n");
			st->state = END;
			return 1;
		}
	}

	StringBuf_Init(&output);

	for(; i < inputlen; i++) {
		if(count && count == numFinds) {	//found enough, stop looking
			break;
		}

		for(f = 0; f <= findlen; f++) {
			if(f == findlen) { //complete match
				numFinds++;
				StringBuf_AppendStr(&output, replace);

				i += findlen - 1;
				break;
			} else {
				if(usecase) {
					if((i + f) > inputlen || input[i + f] != find[f]) {
						StringBuf_Printf(&output, "%c", input[i]);
						break;
					}
				} else {
					if(((i + f) > inputlen || input[i + f] != find[f]) && TOUPPER(input[i+f]) != TOUPPER(find[f])) {
						StringBuf_Printf(&output, "%c", input[i]);
						break;
					}
				}
			}
		}
	}

	//append excess after enough found
	if(i < inputlen)
		StringBuf_AppendStr(&output, &(input[i]));

	script_pushstrcopy(st, StringBuf_Value(&output));
	StringBuf_Destroy(&output);
	return 0;
}

//========================================================
// countstr <input>, <search>{, <usecase>}
//
// Note: Counts the number of times <search> occurs in
// <input>. By default will be case sensitive.
//--------------------------------------------------------
BUILDIN_FUNC(countstr)
{
	const char *input = script_getstr(st, 2);
	const char *find = script_getstr(st, 3);
	size_t inputlen = strlen(input);
	size_t findlen = strlen(find);
	bool usecase = true;

	int numFinds = 0;
	int i = 0, f = 0;

	if(findlen == 0) {
		ShowError("script:countstr: Invalid search length.\n");
		st->state = END;
		return 1;
	}

	if(script_hasdata(st, 4)) {
		struct script_data *data;

		data = script_getdata(st, 4);
		get_val(st, data); // Convert into value in case of a variable
		if( !data_isstring(data) )
			usecase = script_getnum(st, 4) != 0;
		else {
			ShowError("script:countstr: Invalid usecase value. Expected int got string\n");
			st->state = END;
			return 1;
		}
	}

	for(; i < inputlen; i++) {
		for(f = 0; f <= findlen; f++) {
			if(f == findlen) { //complete match
				numFinds++;
				i += findlen - 1;
				break;
			} else {
				if(usecase) {
					if((i + f) > inputlen || input[i + f] != find[f]) {
						break;
					}
				} else {
					if(((i + f) > inputlen || input[i + f] != find[f]) && TOUPPER(input[i+f]) != TOUPPER(find[f])) {
						break;
					}
				}
			}
		}
	}
	script_pushint(st, numFinds);
	return 0;
}

/// Changes the display name and/or display class of the npc.
/// Returns 0 is successful, 1 if the npc does not exist.
///
/// setnpcdisplay("<npc name>", "<new display name>", <new class id>, <new size>) -> <int>
/// setnpcdisplay("<npc name>", "<new display name>", <new class id>) -> <int>
/// setnpcdisplay("<npc name>", "<new display name>") -> <int>
/// setnpcdisplay("<npc name>", <new class id>) -> <int>
BUILDIN_FUNC(setnpcdisplay)
{
	const char* name;
	const char* newname = NULL;
	int class_ = -1, size = -1;
	struct script_data* data;
	struct npc_data* nd;

	name = script_getstr(st,2);
	data = script_getdata(st,3);

	if( script_hasdata(st,4) )
		class_ = script_getnum(st,4);
	if( script_hasdata(st,5) )
		size = script_getnum(st,5);

	get_val(st, data);
	if( data_isstring(data) )
 		newname = conv_str(st,data);
	else if( data_isint(data) )
 		class_ = conv_num(st,data);
	else
	{
		ShowError("script:setnpcdisplay: expected a string or number\n");
		script_reportdata(data);
		return 1;
	}

	nd = npc_name2id(name);
	if( nd == NULL )
	{// not found
		script_pushint(st,1);
		return 0;
	}

	// update npc
	if( newname )
		npc_setdisplayname(nd, newname);

	if( size != -1 && size != (int)nd->size )
		nd->size = size;
	else
		size = -1;

	if( class_ != -1 && nd->class_ != class_ )
		npc_setclass(nd, class_);
	else if( size != -1 )
	{ // Required to update the visual size
		clif_clearunit_area(&nd->bl, CLR_OUTSIGHT);
		clif_spawn(&nd->bl);
	}

	script_pushint(st,0);
	return 0;
}

BUILDIN_FUNC(atoi)
{
	const char *value;
	value = script_getstr(st,2);
	script_pushint(st,atoi(value));
	return 0;
}

// case-insensitive substring search [lordalfa]
BUILDIN_FUNC(compare)
{
	const char *message;
	const char *cmpstring;
	message = script_getstr(st,2);
	cmpstring = script_getstr(st,3);
	script_pushint(st,(stristr(message,cmpstring) != NULL));
	return 0;
}

// [zBuffer] List of mathematics commands --->
BUILDIN_FUNC(sqrt)
{
	double i, a;
	i = script_getnum(st,2);
	a = sqrt(i);
	script_pushint(st,(int)a);
	return 0;
}

BUILDIN_FUNC(pow)
{
	double i, a, b;
	a = script_getnum(st,2);
	b = script_getnum(st,3);
	i = pow(a,b);
	script_pushint(st,(int)i);
	return 0;
}

BUILDIN_FUNC(min)
{
	int i, min;

	min = script_getnum(st, 2);
	for (i = 3; script_hasdata(st, i); i++) {
		int next = script_getnum(st, i);
		if (next < min)
			min = next;
	}
	script_pushint(st, min);

	return true;
}

BUILDIN_FUNC(max)
{
	int i, max;

	max = script_getnum(st, 2);
	for (i = 3; script_hasdata(st, i); i++) {
		int next = script_getnum(st, i);
		if (next > max)
			max = next;
	}
	script_pushint(st, max);

	return true;
}

BUILDIN_FUNC(distance)
{
	int x0, y0, x1, y1;

	x0 = script_getnum(st,2);
	y0 = script_getnum(st,3);
	x1 = script_getnum(st,4);
	y1 = script_getnum(st,5);

	script_pushint(st,distance_xy(x0,y0,x1,y1));
	return 0;
}

// <--- [zBuffer] List of mathematics commands

BUILDIN_FUNC(md5)
{
	const char *tmpstr;
	char *md5str;

	tmpstr = script_getstr(st,2);
	md5str = (char *)aMallocA((32+1)*sizeof(char));
	MD5_String(tmpstr, md5str);
	script_pushstr(st, md5str);
	return 0;
}

// [zBuffer] List of dynamic var commands --->

BUILDIN_FUNC(setd)
{
	TBL_PC *sd=NULL;
	char varname[100];
	const char *buffer;
	int elem;
	buffer = script_getstr(st, 2);

	if(sscanf(buffer, "%99[^[][%d]", varname, &elem) < 2)
		elem = 0;

	if( not_server_variable(*varname) )
	{
		sd = script_rid2sd(st);
		if( sd == NULL )
		{
			ShowError("script:setd: no player attached for player variable '%s'\n", buffer);
			return 0;
		}
	}

	if( is_string_variable(varname) ) {
		setd_sub(st, sd, varname, elem, (void *)script_getstr(st, 3), NULL);
	} else {
		setd_sub(st, sd, varname, elem, (void *)script_getnum(st, 3), NULL);
	}
	
	return 0;
}

#ifndef TXT_ONLY
int buildin_query_sql_sub(struct script_state* st, Sql* handle)
{
	int i, j;
	TBL_PC* sd = NULL;
	const char* query;
	struct script_data* data;
	const char* name;
	int max_rows = SCRIPT_MAX_ARRAYSIZE;// maximum number of rows
	int num_vars;
	int num_cols;

	// check target variables
	for( i = 3; script_hasdata(st,i); ++i )
	{
		data = script_getdata(st, i);
		if( data_isreference(data) && reference_tovariable(data) )
		{// it's a variable
			name = reference_getname(data);
			if( not_server_variable(*name) && sd == NULL )
			{// requires a player
				sd = script_rid2sd(st);
				if( sd == NULL )
				{// no player attached
					script_reportdata(data);
					st->state = END;
					return 1;
				}
			}
			if( not_array_variable(*name) )
				max_rows = 1;// not an array, limit to one row
		}
		else
		{
			ShowError("script:query_sql: not a variable\n");
			script_reportdata(data);
			st->state = END;
			return 1;
		}
	}
	num_vars = i - 3;

	// Execute the query
	query = script_getstr(st,2);
	if( SQL_ERROR == Sql_QueryStr(handle, query) )
	{
		Sql_ShowDebug(handle);
		script_pushint(st, 0);
		return 1;
	}

	if( Sql_NumRows(handle) == 0 )
	{// No data received
		Sql_FreeResult(handle);
		script_pushint(st, 0);
		return 0;
	}

	// Count the number of columns to store
	num_cols = Sql_NumColumns(handle);
	if( num_vars < num_cols )
	{
		ShowWarning("script:query_sql: Too many columns, discarding last %u columns.\n", (unsigned int)(num_cols-num_vars));
		script_reportsrc(st);
	}
	else if( num_vars > num_cols )
	{
		ShowWarning("script:query_sql: Too many variables (%u extra).\n", (unsigned int)(num_vars-num_cols));
		script_reportsrc(st);
	}

	// Store data
	for( i = 0; i < max_rows && SQL_SUCCESS == Sql_NextRow(handle); ++i )
	{
		for( j = 0; j < num_vars; ++j )
		{
			char* str = NULL;

			if( j < num_cols )
				Sql_GetData(handle, j, &str, NULL);

			data = script_getdata(st, j+3);
			name = reference_getname(data);
			if( is_string_variable(name) )
				setd_sub(st, sd, name, i, (void *)(str?str:""), reference_getref(data));
			else
				setd_sub(st, sd, name, i, (void *)(str?atoi(str):0), reference_getref(data));
		}
	}
	if( i == max_rows && max_rows < Sql_NumRows(handle) )
	{
		ShowWarning("script:query_sql: Only %d/%u rows have been stored.\n", max_rows, (unsigned int)Sql_NumRows(handle));
		script_reportsrc(st);
	}

	// Free data
	Sql_FreeResult(handle);
	script_pushint(st, i);
	return 0;
}
#endif

BUILDIN_FUNC(query_sql)
{
#ifndef TXT_ONLY
	return buildin_query_sql_sub(st, mmysql_handle);
#else
	//for TXT version, we always return -1
	script_pushint(st,-1);
	return 0;
#endif
}

BUILDIN_FUNC(query_logsql)
{
#ifndef TXT_ONLY
	if( !log_config.sql_logs )
	{// logmysql_handle == NULL
		ShowWarning("buildin_query_logsql: SQL logs are disabled, query '%s' will not be executed.\n", script_getstr(st,2));
		script_pushint(st,-1);
		return 1;
	}

	return buildin_query_sql_sub(st, logmysql_handle);
#else
	//for TXT version, we always return -1
	script_pushint(st,-1);
	return 0;
#endif
}

//Allows escaping of a given string.
BUILDIN_FUNC(escape_sql)
{
	const char *str;
	char *esc_str;
	size_t len;

	str = script_getstr(st,2);
	len = strlen(str);
	esc_str = (char*)aMallocA(len*2+1);
#if defined(TXT_ONLY)
	jstrescapecpy(esc_str, str);
#else
	Sql_EscapeStringLen(mmysql_handle, esc_str, str, len);
#endif
	script_pushstr(st, esc_str);
	return 0;
}

BUILDIN_FUNC(getd)
{
	const char* p;
	const char* name;
	int namelen;
	bool isarray;
	long idx;
	struct script_data* data;

	p = script_getstr(st, 2);
	p = skip_space(p);

	// parse name
	name = p; // not NUL terminated (not needed)
	namelen = skip_word(p) - p;
	p += namelen;
	p = skip_space(p);
	// parse index (optional)
	isarray = false;
	idx = 0;
	if( p[0] == '[' )
	{
		char* end = NULL;
		const char* p2 = skip_space(p + 1);
		idx = strtol(p2, &end, 0);
		if( p2 != NULL && p2 != end )
		{ // has a numeric index
			p2 = skip_space(end);
			if( p2[0] == ']' )
			{
				p = skip_space(p2 + 1);
				isarray = true;
			}
		}
	}

	// validate
	if( p[0] != '\0' )
	{
		ShowError("script:getd: failed to parse '%s'\n", p);
		script_reportdata(script_getdata(st, 2));
		st->state = END;
		return 1;
	}
	if( namelen == 0 )
	{
		ShowError("script:getd: variable name is empty\n");
		script_reportdata(script_getdata(st, 2));
		st->state = END;
		return 1;
	}
	if( isarray && not_array_variable(name[0]) )
	{
		ShowError("script:getd: not an array variable\n");
		script_reportdata(script_getdata(st, 2));
		st->state = END;
		return 1;
	}
	if( idx < 0 || idx >= SCRIPT_MAX_ARRAYSIZE )
	{
		ShowError("script:getd: index=%ld is invalid, must be a number from 0 to %d\n", idx, SCRIPT_MAX_ARRAYSIZE - 1);
		script_reportdata(script_getdata(st, 2));
		st->state = END;
		return 1;
	}

	// generate reference
	data = push_val(st->stack, C_NAME, reference_uid(add_word(name), idx));
	if( reference_tonil(data) )
		str_data[reference_getid(data)].type = C_NAME; // unused name, make it a reference to variable
	//XXX references can point to other types of data, not just variables

	return 0;
}

// <--- [zBuffer] List of dynamic var commands
// Pet stat [Lance]
BUILDIN_FUNC(petstat)
{
	TBL_PC *sd = NULL;
	struct pet_data *pd;
	int flag = script_getnum(st,2);
	sd = script_rid2sd(st);
	if(!sd || !sd->status.pet_id || !sd->pd){
		if(flag == 2)
			script_pushconststr(st, "");
		else
			script_pushint(st,0);
		return 0;
	}
	pd = sd->pd;
	switch(flag){
		case 1: script_pushint(st,(int)pd->pet.class_); break;
		case 2: script_pushstrcopy(st, pd->pet.name); break;
		case 3: script_pushint(st,(int)pd->pet.level); break;
		case 4: script_pushint(st,(int)pd->pet.hungry); break;
		case 5: script_pushint(st,(int)pd->pet.intimate); break;
		default:
			script_pushint(st,0);
			break;
	}
	return 0;
}

BUILDIN_FUNC(callshop)
{
	TBL_PC *sd = NULL;
	struct npc_data *nd;
	const char *shopname;
	int flag = 0;
	sd = script_rid2sd(st);
	if (!sd) {
		script_pushint(st,0);
		return 0;
	}
	shopname = script_getstr(st, 2);
	if (script_hasdata(st,3))
		flag = script_getnum(st,3);
	nd = npc_name2id(shopname);
	if( !nd || nd->bl.type != BL_NPC || (nd->subtype != NPCTYPE_SHOP && nd->subtype != NPCTYPE_CASHSHOP && nd->subtype != NPCTYPE_MARKETSHOP) ) {
		ShowError("buildin_callshop: Shop [%s] not found (or NPC is not shop type)\n", shopname);
		script_pushint(st,0);
		return 1;
	}

	if (nd->subtype == NPCTYPE_SHOP) {
		// flag the user as using a valid script call for opening the shop (for floating NPCs)
		sd->state.callshop = 1;

		switch (flag) {
			case 1: npc_buysellsel(sd,nd->bl.id,0); break; //Buy window
			case 2: npc_buysellsel(sd,nd->bl.id,1); break; //Sell window
			default: clif_npcbuysell(sd,nd->bl.id); break; //Show menu
		}
	}
#if PACKETVER >= 20131223
	else if (nd->subtype == NPCTYPE_MARKETSHOP) {
		unsigned short i;

		for (i = 0; i < nd->u.shop.count; i++) {
			if (nd->u.shop.shop_item[i].qty)
				break;
		}

		if (i == nd->u.shop.count) {
			clif_disp_overheadcolor_self(sd->fd, COLOR_RED, msg_txt(534));
			return false;
		}

		sd->npc_shopid = nd->bl.id;
		clif_npc_market_open(sd, nd);
		script_pushint(st,1);
		return 0;
	}
#endif
	else
		clif_cashshop_show(sd, nd);

	sd->npc_shopid = nd->bl.id;
	script_pushint(st,1);
	return 0;
}

BUILDIN_FUNC(npcshopitem)
{
	const char* npcname = script_getstr(st, 2);
	struct npc_data* nd = npc_name2id(npcname);
	int n, i;
	int amount;
	uint16 offs = 2;

	if( !nd || ( nd->subtype != NPCTYPE_SHOP && nd->subtype != NPCTYPE_CASHSHOP && nd->subtype != NPCTYPE_MARKETSHOP ) ) { // Not found.
		script_pushint(st,0);
		return 0;
	}

#if PACKETVER >= 20131223
	if (nd->subtype == NPCTYPE_MARKETSHOP) {
		offs = 3;
		npc_market_delfromsql_(nd->exname, 0, true);
	}
#endif

	// get the count of new entries
	amount = (script_lastdata(st)-2)/offs;

	// generate new shop item list
	RECREATE(nd->u.shop.shop_item, struct npc_item_list, amount);
	for (n = 0, i = 3; n < amount; n++, i+=offs) {
		nd->u.shop.shop_item[n].nameid = script_getnum(st,i);
		nd->u.shop.shop_item[n].value = script_getnum(st,i+1);
#if PACKETVER >= 20131223
		if (nd->subtype == NPCTYPE_MARKETSHOP) {
			nd->u.shop.shop_item[n].qty = script_getnum(st,i+2);
			nd->u.shop.shop_item[n].flag = 1;
			npc_market_tosql(nd->exname, &nd->u.shop.shop_item[n]);
		}
#endif
	}
	nd->u.shop.count = n;

	script_pushint(st,1);
	return 0;
}

BUILDIN_FUNC(npcshopadditem) {
	const char* npcname = script_getstr(st,2);
	struct npc_data* nd = npc_name2id(npcname);
	int n, i;
	uint16 offs = 2, amount;

	if (!nd || ( nd->subtype != NPCTYPE_SHOP && nd->subtype != NPCTYPE_CASHSHOP && nd->subtype != NPCTYPE_MARKETSHOP)) { // Not found.
		script_pushint(st,0);
		return 0;
	}

	if (nd->subtype == NPCTYPE_MARKETSHOP)
		offs = 3;

	// get the count of new entries
	amount = (script_lastdata(st)-2)/offs;

#if PACKETVER >= 20131223
	if (nd->subtype == NPCTYPE_MARKETSHOP) {
		for (n = 0, i = 3; n < amount; n++, i += offs) {
			uint16 nameid = script_getnum(st,i), j;

			// Check existing entries
			ARR_FIND(0, nd->u.shop.count, j, nd->u.shop.shop_item[j].nameid == nameid);
			if (j == nd->u.shop.count) {
				RECREATE(nd->u.shop.shop_item, struct npc_item_list, nd->u.shop.count+1);
				j = nd->u.shop.count;
				nd->u.shop.shop_item[j].flag = 1;
				nd->u.shop.count++;
			}

			nd->u.shop.shop_item[j].nameid = nameid;
			nd->u.shop.shop_item[j].value = script_getnum(st,i+1);
			nd->u.shop.shop_item[j].qty = script_getnum(st,i+2);

			npc_market_tosql(nd->exname, &nd->u.shop.shop_item[j]);
		}
		script_pushint(st,1);
		return 0;
	}
#endif

	// append new items to existing shop item list
	RECREATE(nd->u.shop.shop_item, struct npc_item_list, nd->u.shop.count+amount);
	for (n = nd->u.shop.count, i = 3; n < nd->u.shop.count+amount; n++, i+=offs) {
		nd->u.shop.shop_item[n].nameid = script_getnum(st,i);
		nd->u.shop.shop_item[n].value = script_getnum(st,i+1);
	}
	nd->u.shop.count = n;

	script_pushint(st,1);
	return 0;
}

BUILDIN_FUNC(npcshopdelitem) {
	const char* npcname = script_getstr(st,2);
	struct npc_data* nd = npc_name2id(npcname);
	int n, i, size;
	unsigned short amount;

	if (!nd || ( nd->subtype != NPCTYPE_SHOP && nd->subtype != NPCTYPE_CASHSHOP && nd->subtype != NPCTYPE_MARKETSHOP)) { // Not found.
		script_pushint(st,0);
		return 0;
	}

	amount = script_lastdata(st)-2;
	size = nd->u.shop.count;

	// remove specified items from the shop item list
	for( i = 3; i < 3 + amount; i++ ) {
		unsigned short nameid = script_getnum(st,i);

		ARR_FIND( 0, size, n, nd->u.shop.shop_item[n].nameid == nameid );
		if( n < size ) {
			if (n+1 != size)
				memmove(&nd->u.shop.shop_item[n], &nd->u.shop.shop_item[n+1], sizeof(nd->u.shop.shop_item[0])*(size-n));
#if PACKETVER >= 20131223
			if (nd->subtype == NPCTYPE_MARKETSHOP)
				npc_market_delfromsql_(nd->exname, nameid, false);
#endif
			size--;
		}
	}

	RECREATE(nd->u.shop.shop_item, struct npc_item_list, size);
	nd->u.shop.count = size;

	script_pushint(st,1);
	return 0;
}

/**
 * Sets a script to attach to a shop npc.
 * npcshopattach "<npc_name>";
 **/
BUILDIN_FUNC(npcshopattach)
{
	const char* npcname = script_getstr(st,2);
	struct npc_data* nd = npc_name2id(npcname);
	int flag = 1;

	if( script_hasdata(st,3) )
		flag = script_getnum(st,3);

	if (!nd || ( nd->subtype != NPCTYPE_SHOP && nd->subtype != NPCTYPE_CASHSHOP && nd->subtype != NPCTYPE_MARKETSHOP)) { // Not found.
		script_pushint(st,0);
		return 0;
	}

	if (flag)
		nd->master_nd = ((struct npc_data *)map_id2bl(st->oid));
	else
		nd->master_nd = NULL;

	script_pushint(st,1);
	return 0;
}

/**
 * Update an entry from NPC shop.
 * npcshopupdate "<name>",<item_id>,<price>{,<stock>}
 **/
BUILDIN_FUNC(npcshopupdate) {
	const char* npcname = script_getstr(st, 2);
	struct npc_data* nd = npc_name2id(npcname);
	uint16 nameid = script_getnum(st, 3);
	int price = script_getnum(st, 4);
	uint16 stock = 0;
	int i;

	if( !nd || ( nd->subtype != NPCTYPE_SHOP && nd->subtype != NPCTYPE_CASHSHOP && nd->subtype != NPCTYPE_MARKETSHOP ) ) { // Not found.
		script_pushint(st,0);
		return 1;
	}
	
	if (!nd->u.shop.count) {
		ShowError("buildin_npcshopupdate: Attempt to update empty shop from '%s'.\n", nd->exname);
		script_pushint(st,0);
		return 0;
	}

	if (nd->subtype == NPCTYPE_MARKETSHOP) {
		FETCH(5, stock);
	}
	else if ((price = cap_value(price, 0, INT_MAX)) == 0) { // Nothing to do here...
		script_pushint(st,1);
		return 0;
	}

	for (i = 0; i < nd->u.shop.count; i++) {
		if (nd->u.shop.shop_item[i].nameid == nameid) {

			if (price != 0)
				nd->u.shop.shop_item[i].value = price;
#if PACKETVER >= 20131223
			if (nd->subtype == NPCTYPE_MARKETSHOP) {
				nd->u.shop.shop_item[i].qty = stock;
				npc_market_tosql(nd->exname, &nd->u.shop.shop_item[i]);
			}
#endif
		}
	}

	script_pushint(st,1);
	return 0;
}
/*==========================================
 * Returns some values of an item [Lupus]
 * Price, Weight, etc...
	setitemscript(itemID,"{new item bonus script}",[n]);
   Where n:
	0 - script
	1 - Equip script
	2 - Unequip script
 *------------------------------------------*/
BUILDIN_FUNC(setitemscript)
{
	unsigned short item_id;
	int n=0;
	const char *script;
	struct item_data *i_data;
	struct script_code **dstscript;

	item_id	= script_getnum(st,2);
	script = script_getstr(st,3);
	if( script_hasdata(st,4) )
		n=script_getnum(st,4);
	i_data = itemdb_exists(item_id);

	if (!i_data || script==NULL || ( script[0] && script[0]!='{' )) {
		script_pushint(st,0);
		return 0;
	}
	switch (n) {
	case 2:
		dstscript = &i_data->unequip_script;
		break;
	case 1:
		dstscript = &i_data->equip_script;
		break;
	default:
		dstscript = &i_data->script;
		break;
	}
	if(*dstscript)
		script_free_code(*dstscript);

	*dstscript = script[0] ? parse_script(script, "script_setitemscript", 0, 0) : NULL;
	script_pushint(st,1);
	return 0;
}

/* Work In Progress [Lupus]
BUILDIN_FUNC(addmonsterdrop)
{
	int class_,item_id,chance;
	class_=script_getnum(st,2);
	item_id=script_getnum(st,3);
	chance=script_getnum(st,4);
	if(class_>1000 && item_id>500 && chance>0) {
		script_pushint(st,1);
	} else {
		script_pushint(st,0);
	}
}

BUILDIN_FUNC(delmonsterdrop)
{
	int class_,item_id;
	class_=script_getnum(st,2);
	item_id=script_getnum(st,3);
	if(class_>1000 && item_id>500) {
		script_pushint(st,1);
	} else {
		script_pushint(st,0);
	}
}
*/

/*==========================================
 * Returns some values of a monster [Lupus]
 * Name, Level, race, size, etc...
	getmonsterinfo(monsterID,queryIndex);
 *------------------------------------------*/
BUILDIN_FUNC(getmonsterinfo)
{
	struct mob_db *mob;
	int mob_id;

	mob_id	= script_getnum(st,2);
	if (!mobdb_checkid(mob_id)) {
		ShowError("buildin_getmonsterinfo: Wrong Monster ID: %i\n", mob_id);
		if ( !script_getnum(st,3) ) //requested a string
			script_pushconststr(st,"null");
		else
			script_pushint(st,-1);
		return -1;
	}
	mob = mob_db(mob_id);
	switch ( script_getnum(st,3) ) {
		case 0:  script_pushstrcopy(st,mob->jname); break;
		case 1:  script_pushint(st,mob->lv); break;
		case 2:  script_pushint(st,mob->status.max_hp); break;
		case 3:  script_pushint(st,mob->base_exp); break;
		case 4:  script_pushint(st,mob->job_exp); break;
		case 5:  script_pushint(st,mob->status.rhw.atk); break;
		case 6:  script_pushint(st,mob->status.rhw.atk2); break;
		case 7:  script_pushint(st,mob->status.def); break;
		case 8:  script_pushint(st,mob->status.mdef); break;
		case 9:  script_pushint(st,mob->status.str); break;
		case 10: script_pushint(st,mob->status.agi); break;
		case 11: script_pushint(st,mob->status.vit); break;
		case 12: script_pushint(st,mob->status.int_); break;
		case 13: script_pushint(st,mob->status.dex); break;
		case 14: script_pushint(st,mob->status.luk); break;
		case 15: script_pushint(st,mob->status.rhw.range); break;
		case 16: script_pushint(st,mob->range2); break;
		case 17: script_pushint(st,mob->range3); break;
		case 18: script_pushint(st,mob->status.size); break;
		case 19: script_pushint(st,mob->status.race); break;
		case 20: script_pushint(st,mob->status.def_ele); break;
		case 21: script_pushint(st,mob->status.mode); break;
		default: script_pushint(st,-1); //wrong Index
	}
	return 0;
}

BUILDIN_FUNC(checkvending) // check vending [Nab4]
{
	TBL_PC *sd = NULL;

	if(script_hasdata(st,2))
		sd = map_nick2sd(script_getstr(st,2));
	else
		sd = script_rid2sd(st);

	if(sd)
		script_pushint(st,sd->state.vending);
	else
		script_pushint(st,0);

	return 0;
}


BUILDIN_FUNC(checkchatting) // check chatting [Marka]
{
	TBL_PC *sd = NULL;

	if(script_hasdata(st,2))
		sd = map_nick2sd(script_getstr(st,2));
	else
		sd = script_rid2sd(st);

	if(sd)
		script_pushint(st,(sd->chatID != 0));
	else
		script_pushint(st,0);

	return 0;
}

BUILDIN_FUNC(searchitem)
{
	struct script_data* data = script_getdata(st, 2);
	const char *itemname = script_getstr(st,3);
	struct item_data *items[MAX_SEARCH];
	int count;

	char* name;
	int32 start;
	int32 id;
	int32 i;
	TBL_PC* sd = NULL;

	if ((items[0] = itemdb_exists(atoi(itemname))))
		count = 1;
	else {
		count = itemdb_searchname_array(items, ARRAYLENGTH(items), itemname);
		if (count > MAX_SEARCH) count = MAX_SEARCH;
	}

	if (!count) {
		script_pushint(st, 0);
		return 0;
	}

	if( !data_isreference(data) )
	{
		ShowError("script:searchitem: not a variable\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not a variable
	}

	id = reference_getid(data);
	start = reference_getindex(data);
	name = reference_getname(data);
	if( not_array_variable(*name) )
	{
		ShowError("script:searchitem: illegal scope\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not supported
	}

	if( not_server_variable(*name) )
	{
		sd = script_rid2sd(st);
		if( sd == NULL )
			return 0;// no player attached
	}

	if( is_string_variable(name) )
	{// string array
		ShowError("script:searchitem: not an integer array reference\n");
		script_reportdata(data);
		st->state = END;
		return 1;// not supported
	}

	for( i = 0; i < count; ++start, ++i )
	{// Set array
		void* v = (void*)items[i]->nameid;
		set_reg(st, sd, reference_uid(id, start), name, v, reference_getref(data));
	}

	script_pushint(st, count);
	return 0;
}

int axtoi(const char *hexStg)
{
	int n = 0;         // position in string
	int m = 0;         // position in digit[] to shift
	int count;         // loop index
	int intValue = 0;  // integer value of hex string
	int digit[11];      // hold values to convert
	while (n < 10) {
		if (hexStg[n]=='\0')
			break;
		if (hexStg[n] > 0x29 && hexStg[n] < 0x40 ) //if 0 to 9
			digit[n] = hexStg[n] & 0x0f;            //convert to int
		else if (hexStg[n] >='a' && hexStg[n] <= 'f') //if a to f
			digit[n] = (hexStg[n] & 0x0f) + 9;      //convert to int
		else if (hexStg[n] >='A' && hexStg[n] <= 'F') //if A to F
			digit[n] = (hexStg[n] & 0x0f) + 9;      //convert to int
		else break;
		n++;
	}
	count = n;
	m = n - 1;
	n = 0;
	while(n < count) {
		// digit[n] is value of hex digit at position n
		// (m << 2) is the number of positions to shift
		// OR the bits into return value
		intValue = intValue | (digit[n] << (m << 2));
		m--;   // adjust the position to set
		n++;   // next digit to process
	}
	return (intValue);
}

// [Lance] Hex string to integer converter
BUILDIN_FUNC(axtoi)
{
	const char *hex = script_getstr(st,2);
	script_pushint(st,axtoi(hex));	
	return 0;
}

// [zBuffer] List of player cont commands --->
BUILDIN_FUNC(rid2name)
{
	struct block_list *bl = NULL;
	int rid = script_getnum(st,2);
	if((bl = map_id2bl(rid)))
	{
		switch(bl->type) {
			case BL_MOB: script_pushstrcopy(st,((TBL_MOB*)bl)->name); break;
			case BL_PC:  script_pushstrcopy(st,((TBL_PC*)bl)->status.name); break;
			case BL_NPC: script_pushstrcopy(st,((TBL_NPC*)bl)->exname); break;
			case BL_PET: script_pushstrcopy(st,((TBL_PET*)bl)->pet.name); break;
			case BL_HOM: script_pushstrcopy(st,((TBL_HOM*)bl)->homunculus.name); break;
			case BL_MER: script_pushstrcopy(st,((TBL_MER*)bl)->db->name); break;
			default:
				ShowError("buildin_rid2name: BL type unknown.\n");
				script_pushconststr(st,"");
				break;
		}
	} else {
		ShowError("buildin_rid2name: invalid RID\n");
		script_pushconststr(st,"(null)");
	}
	return 0;
}

BUILDIN_FUNC(pcblockmove)
{
	int id, flag;
	TBL_PC *sd = NULL;

	id = script_getnum(st,2);
	flag = script_getnum(st,3);

	if(id)
		sd = map_id2sd(id);
	else
		sd = script_rid2sd(st);

	if(sd)
		sd->state.blockedmove = flag > 0;

	return 0;
}

BUILDIN_FUNC(pcfollow)
{
	int id, targetid;
	TBL_PC *sd = NULL;


	id = script_getnum(st,2);
	targetid = script_getnum(st,3);

	if(id)
		sd = map_id2sd(id);
	else
		sd = script_rid2sd(st);

	if(sd)
		pc_follow(sd, targetid);

    return 0;
}

BUILDIN_FUNC(pcstopfollow)
{
	int id;
	TBL_PC *sd = NULL;


	id = script_getnum(st,2);

	if(id)
		sd = map_id2sd(id);
	else
		sd = script_rid2sd(st);

	if(sd)
		pc_stop_following(sd);

	return 0;
}

/// Checks to see if the unit exists.
///
/// unitexists <unit id>;
BUILDIN_FUNC(unitexists) {
	struct block_list* bl;

	bl = map_id2bl(script_getnum(st, 2));

	if (!bl)
		script_pushint(st, false);
	else
		script_pushint(st, true);

	return 0;
}

// <--- [zBuffer] List of player cont commands
// [zBuffer] List of unit control commands --->

/// Gets specific live information of a bl.
///
/// getunitdata <unit id>,<arrayname>;
BUILDIN_FUNC(getunitdata) {
	TBL_PC *sd = st->rid ? map_id2sd(st->rid) : NULL;
	struct block_list* bl;
	TBL_MOB* md = NULL;
	TBL_HOM* hd = NULL;
	TBL_MER* mc = NULL;
	TBL_PET* pd = NULL;
	TBL_ELEM* ed = NULL;
	TBL_NPC* nd = NULL;
	int num;
	char* name;
	struct script_data *data = script_getdata(st, 3);

	if (!data_isreference(script_getdata(st, 3))) {
		ShowWarning("buildin_getunitdata: Error in argument! Please give a variable to store values in.\n");
		script_pushint(st, -1);
		return 1;
	}

	bl = map_id2bl(script_getnum(st, 2));

	if (!bl) {
		ShowWarning("buildin_getunitdata: Error in finding object with given game ID %d!\n", script_getnum(st, 2));
		script_pushint(st, -1);
		return 1;
	}

	switch (bl->type) {
		case BL_MOB:  md = map_id2md(bl->id); break;
		case BL_HOM:  hd = map_id2hd(bl->id); break;
		case BL_PET:  pd = map_id2pd(bl->id); break;
		case BL_MER:  mc = map_id2mc(bl->id); break;
		case BL_ELEM: ed = map_id2ed(bl->id); break;
		case BL_NPC:  nd = map_id2nd(bl->id); break;
	}

	num = st->stack->stack_data[st->start+3].u.num;
	name = (char *)(str_buf+str_data[num&0x00ffffff].str);

#define getunitdata_sub(idx__,var__) setd_sub(st,sd,name,(idx__),(void *)(int)(var__),data->ref)

	switch(bl->type) {
		case BL_MOB:
			if (!md) {
				ShowWarning("buildin_getunitdata: Error in finding object BL_MOB!\n");
				script_pushint(st, -1);
				return 1;
			}
			getunitdata_sub(UMOB_SIZE, md->status.size);
			getunitdata_sub(UMOB_LEVEL, md->level);
			getunitdata_sub(UMOB_HP, md->status.hp);
			getunitdata_sub(UMOB_MAXHP, md->status.max_hp);
			getunitdata_sub(UMOB_MASTERAID, md->master_id);
			getunitdata_sub(UMOB_MAPID, md->bl.m);
			getunitdata_sub(UMOB_X, md->bl.x);
			getunitdata_sub(UMOB_Y, md->bl.y);
			getunitdata_sub(UMOB_SPEED, md->status.speed);
			getunitdata_sub(UMOB_MODE, md->status.mode);
			getunitdata_sub(UMOB_AI, md->special_state.ai);
			getunitdata_sub(UMOB_SCOPTION, md->sc.option);
			getunitdata_sub(UMOB_SEX, md->vd->sex);
			getunitdata_sub(UMOB_CLASS, md->vd->class_);
			getunitdata_sub(UMOB_HAIRSTYLE, md->vd->hair_style);
			getunitdata_sub(UMOB_HAIRCOLOR, md->vd->hair_color);
			getunitdata_sub(UMOB_HEADBOTTOM, md->vd->head_bottom);
			getunitdata_sub(UMOB_HEADMIDDLE, md->vd->head_mid);
			getunitdata_sub(UMOB_HEADTOP, md->vd->head_top);
			getunitdata_sub(UMOB_CLOTHCOLOR, md->vd->cloth_color);
			getunitdata_sub(UMOB_SHIELD, md->vd->shield);
			getunitdata_sub(UMOB_WEAPON, md->vd->weapon);
			getunitdata_sub(UMOB_LOOKDIR, md->ud.dir);
			getunitdata_sub(UMOB_STR, md->status.str);
			getunitdata_sub(UMOB_AGI, md->status.agi);
			getunitdata_sub(UMOB_VIT, md->status.vit);
			getunitdata_sub(UMOB_INT, md->status.int_);
			getunitdata_sub(UMOB_DEX, md->status.dex);
			getunitdata_sub(UMOB_LUK, md->status.luk);
			getunitdata_sub(UMOB_SLAVECPYMSTRMD, md->state.copy_master_mode);
			getunitdata_sub(UMOB_DMGIMMUNE, md->ud.immune_attack);
			getunitdata_sub(UMOB_ATKRANGE, md->status.rhw.range);
			getunitdata_sub(UMOB_ATK, md->status.rhw.atk);
			getunitdata_sub(UMOB_MATK, md->status.rhw.atk2);
			getunitdata_sub(UMOB_DEF, md->status.def);
			getunitdata_sub(UMOB_MDEF, md->status.mdef);
			getunitdata_sub(UMOB_HIT, md->status.hit);
			getunitdata_sub(UMOB_FLEE, md->status.flee);
			getunitdata_sub(UMOB_PDODGE, md->status.flee2);
			getunitdata_sub(UMOB_CRIT, md->status.cri);
			getunitdata_sub(UMOB_RACE, md->status.race);
			getunitdata_sub(UMOB_ELETYPE, md->status.def_ele);
			getunitdata_sub(UMOB_ELELEVEL, md->status.ele_lv);
			getunitdata_sub(UMOB_AMOTION, md->status.amotion);
			getunitdata_sub(UMOB_ADELAY, md->status.adelay);
			getunitdata_sub(UMOB_DMOTION, md->status.dmotion);
			break;

		case BL_HOM:
			if (!hd) {
				ShowWarning("buildin_getunitdata: Error in finding object BL_HOM!\n");
				script_pushint(st, -1);
				return 1;
			}
			getunitdata_sub(UHOM_SIZE, hd->base_status.size);
			getunitdata_sub(UHOM_LEVEL, hd->homunculus.level);
			getunitdata_sub(UHOM_HP, hd->homunculus.hp);
			getunitdata_sub(UHOM_MAXHP, hd->homunculus.max_hp);
			getunitdata_sub(UHOM_SP, hd->homunculus.sp);
			getunitdata_sub(UHOM_MAXSP, hd->homunculus.max_sp);
			getunitdata_sub(UHOM_MASTERCID, hd->homunculus.char_id);
			getunitdata_sub(UHOM_MAPID, hd->bl.m);
			getunitdata_sub(UHOM_X, hd->bl.x);
			getunitdata_sub(UHOM_Y, hd->bl.y);
			getunitdata_sub(UHOM_HUNGER, hd->homunculus.hunger);
			getunitdata_sub(UHOM_INTIMACY, hd->homunculus.intimacy);
			getunitdata_sub(UHOM_SPEED, hd->base_status.speed);
			getunitdata_sub(UHOM_LOOKDIR, hd->ud.dir);
			getunitdata_sub(UHOM_CANMOVETICK, hd->ud.canmove_tick);
			getunitdata_sub(UHOM_STR, hd->base_status.str);
			getunitdata_sub(UHOM_AGI, hd->base_status.agi);
			getunitdata_sub(UHOM_VIT, hd->base_status.vit);
			getunitdata_sub(UHOM_INT, hd->base_status.int_);
			getunitdata_sub(UHOM_DEX, hd->base_status.dex);
			getunitdata_sub(UHOM_LUK, hd->base_status.luk);
			getunitdata_sub(UHOM_DMGIMMUNE, hd->ud.immune_attack);
			getunitdata_sub(UHOM_ATKRANGE, hd->battle_status.rhw.range);
			getunitdata_sub(UHOM_ATK, hd->battle_status.rhw.atk);
			getunitdata_sub(UHOM_MATK, hd->battle_status.rhw.atk2);
			getunitdata_sub(UHOM_DEF, hd->battle_status.def);
			getunitdata_sub(UHOM_MDEF, hd->battle_status.mdef);
			getunitdata_sub(UHOM_HIT, hd->battle_status.hit);
			getunitdata_sub(UHOM_FLEE, hd->battle_status.flee);
			getunitdata_sub(UHOM_PDODGE, hd->battle_status.flee2);
			getunitdata_sub(UHOM_CRIT, hd->battle_status.cri);
			getunitdata_sub(UHOM_RACE, hd->battle_status.race);
			getunitdata_sub(UHOM_ELETYPE, hd->battle_status.def_ele);
			getunitdata_sub(UHOM_ELELEVEL, hd->battle_status.ele_lv);
			getunitdata_sub(UHOM_AMOTION, hd->battle_status.amotion);
			getunitdata_sub(UHOM_ADELAY, hd->battle_status.adelay);
			getunitdata_sub(UHOM_DMOTION, hd->battle_status.dmotion);
			break;

		case BL_PET:
			if (!pd) {
				ShowWarning("buildin_getunitdata: Error in finding object BL_PET!\n");
				script_pushint(st, -1);
				return 1;
			}
			getunitdata_sub(UPET_SIZE, pd->status.size);
			getunitdata_sub(UPET_LEVEL, pd->pet.level);
			getunitdata_sub(UPET_HP, pd->status.hp);
			getunitdata_sub(UPET_MAXHP, pd->status.max_hp);
			getunitdata_sub(UPET_MASTERAID, pd->pet.account_id);
			getunitdata_sub(UPET_MAPID, pd->bl.m);
			getunitdata_sub(UPET_X, pd->bl.x);
			getunitdata_sub(UPET_Y, pd->bl.y);
			getunitdata_sub(UPET_HUNGER, pd->pet.hungry);
			getunitdata_sub(UPET_INTIMACY, pd->pet.intimate);
			getunitdata_sub(UPET_SPEED, pd->status.speed);
			getunitdata_sub(UPET_LOOKDIR, pd->ud.dir);
			getunitdata_sub(UPET_CANMOVETICK, pd->ud.canmove_tick);
			getunitdata_sub(UPET_STR, pd->status.str);
			getunitdata_sub(UPET_AGI, pd->status.agi);
			getunitdata_sub(UPET_VIT, pd->status.vit);
			getunitdata_sub(UPET_INT, pd->status.int_);
			getunitdata_sub(UPET_DEX, pd->status.dex);
			getunitdata_sub(UPET_LUK, pd->status.luk);
			getunitdata_sub(UPET_DMGIMMUNE, pd->ud.immune_attack);
			getunitdata_sub(UPET_ATKRANGE, pd->status.rhw.range);
			getunitdata_sub(UPET_ATK, pd->status.rhw.atk);
			getunitdata_sub(UPET_MATK, pd->status.rhw.atk2);
			getunitdata_sub(UPET_DEF, pd->status.def);
			getunitdata_sub(UPET_MDEF, pd->status.mdef);
			getunitdata_sub(UPET_HIT, pd->status.hit);
			getunitdata_sub(UPET_FLEE, pd->status.flee);
			getunitdata_sub(UPET_PDODGE, pd->status.flee2);
			getunitdata_sub(UPET_CRIT, pd->status.cri);
			getunitdata_sub(UPET_RACE, pd->status.race);
			getunitdata_sub(UPET_ELETYPE, pd->status.def_ele);
			getunitdata_sub(UPET_ELELEVEL, pd->status.ele_lv);
			getunitdata_sub(UPET_AMOTION, pd->status.amotion);
			getunitdata_sub(UPET_ADELAY, pd->status.adelay);
			getunitdata_sub(UPET_DMOTION, pd->status.dmotion);
			break;

		case BL_MER:
			if (!mc) {
				ShowWarning("buildin_getunitdata: Error in finding object BL_MER!\n");
				script_pushint(st, -1);
				return 1;
			}
			getunitdata_sub(UMER_SIZE, mc->base_status.size);
			getunitdata_sub(UMER_HP, mc->base_status.hp);
			getunitdata_sub(UMER_MAXHP, mc->base_status.max_hp);
			getunitdata_sub(UMER_MASTERCID, mc->mercenary.char_id);
			getunitdata_sub(UMER_MAPID, mc->bl.m);
			getunitdata_sub(UMER_X, mc->bl.x);
			getunitdata_sub(UMER_Y, mc->bl.y);
			getunitdata_sub(UMER_KILLCOUNT, mc->mercenary.kill_count);
			getunitdata_sub(UMER_LIFETIME, mc->mercenary.life_time);
			getunitdata_sub(UMER_SPEED, mc->base_status.speed);
			getunitdata_sub(UMER_LOOKDIR, mc->ud.dir);
			getunitdata_sub(UMER_CANMOVETICK, mc->ud.canmove_tick);
			getunitdata_sub(UMER_STR, mc->base_status.str);
			getunitdata_sub(UMER_AGI, mc->base_status.agi);
			getunitdata_sub(UMER_VIT, mc->base_status.vit);
			getunitdata_sub(UMER_INT, mc->base_status.int_);
			getunitdata_sub(UMER_DEX, mc->base_status.dex);
			getunitdata_sub(UMER_LUK, mc->base_status.luk);
			getunitdata_sub(UMER_DMGIMMUNE, mc->ud.immune_attack);
			getunitdata_sub(UMER_ATKRANGE, mc->base_status.rhw.range);
			getunitdata_sub(UMER_ATK, mc->base_status.rhw.atk);
			getunitdata_sub(UMER_MATK, mc->base_status.rhw.atk2);
			getunitdata_sub(UMER_DEF, mc->base_status.def);
			getunitdata_sub(UMER_MDEF, mc->base_status.mdef);
			getunitdata_sub(UMER_HIT, mc->base_status.hit);
			getunitdata_sub(UMER_FLEE, mc->base_status.flee);
			getunitdata_sub(UMER_PDODGE, mc->base_status.flee2);
			getunitdata_sub(UMER_CRIT, mc->base_status.cri);
			getunitdata_sub(UMER_RACE, mc->base_status.race);
			getunitdata_sub(UMER_ELETYPE, mc->base_status.def_ele);
			getunitdata_sub(UMER_ELELEVEL, mc->base_status.ele_lv);
			getunitdata_sub(UMER_AMOTION, mc->base_status.amotion);
			getunitdata_sub(UMER_ADELAY, mc->base_status.adelay);
			getunitdata_sub(UMER_DMOTION, mc->base_status.dmotion);
			break;
		case BL_ELEM:
			if (!ed) {
				ShowWarning("buildin_getunitdata: Error in finding object BL_ELEM!\n");
				script_pushint(st, -1);
				return 1;
			}
			getunitdata_sub(UELE_SIZE, ed->base_status.size);
			getunitdata_sub(UELE_HP, ed->elemental.hp);
			//getunitdata_sub(UELE_MAXHP, ed->elemental.max_hp);
			getunitdata_sub(UELE_SP, ed->elemental.sp);
			//getunitdata_sub(UELE_MAXSP, ed->elemental.max_sp);
			getunitdata_sub(UELE_MASTERCID, ed->elemental.char_id);
			getunitdata_sub(UELE_MAPID, ed->bl.m);
			getunitdata_sub(UELE_X, ed->bl.x);
			getunitdata_sub(UELE_Y, ed->bl.y);
			getunitdata_sub(UELE_LIFETIME, ed->elemental.life_time);
			getunitdata_sub(UELE_MODE, ed->elemental.mode);
			getunitdata_sub(UELE_SP, ed->base_status.speed);
			getunitdata_sub(UELE_LOOKDIR, ed->ud.dir);
			getunitdata_sub(UELE_CANMOVETICK, ed->ud.canmove_tick);
			getunitdata_sub(UELE_STR, ed->base_status.str);
			getunitdata_sub(UELE_AGI, ed->base_status.agi);
			getunitdata_sub(UELE_VIT, ed->base_status.vit);
			getunitdata_sub(UELE_INT, ed->base_status.int_);
			getunitdata_sub(UELE_DEX, ed->base_status.dex);
			getunitdata_sub(UELE_LUK, ed->base_status.luk);
			getunitdata_sub(UELE_DMGIMMUNE, ed->ud.immune_attack);
			getunitdata_sub(UELE_ATKRANGE, ed->base_status.rhw.range);
			getunitdata_sub(UELE_ATK, ed->base_status.rhw.atk);
			getunitdata_sub(UELE_MATK, ed->base_status.rhw.atk2);
			getunitdata_sub(UELE_DEF, ed->base_status.def);
			getunitdata_sub(UELE_MDEF, ed->base_status.mdef);
			getunitdata_sub(UELE_HIT, ed->base_status.hit);
			getunitdata_sub(UELE_FLEE, ed->base_status.flee);
			getunitdata_sub(UELE_PDODGE, ed->base_status.flee2);
			getunitdata_sub(UELE_CRIT, ed->base_status.cri);
			getunitdata_sub(UELE_RACE, ed->base_status.race);
			getunitdata_sub(UELE_ELETYPE, ed->base_status.def_ele);
			getunitdata_sub(UELE_ELELEVEL, ed->base_status.ele_lv);
			getunitdata_sub(UELE_AMOTION, ed->base_status.amotion);
			getunitdata_sub(UELE_ADELAY, ed->base_status.adelay);
			getunitdata_sub(UELE_DMOTION, ed->base_status.dmotion);
			break;
		case BL_NPC:
			if (!nd) {
				ShowWarning("buildin_getunitdata: Error in finding object BL_NPC!\n");
				script_pushint(st, -1);
				return 1;
			}
			getunitdata_sub(UNPC_DISPLAY, nd->class_);
			getunitdata_sub(UNPC_LEVEL, nd->level);
			getunitdata_sub(UNPC_HP, nd->status.hp);
			getunitdata_sub(UNPC_MAXHP, nd->status.max_hp);
			getunitdata_sub(UNPC_MAPID, nd->bl.m);
			getunitdata_sub(UNPC_X, nd->bl.x);
			getunitdata_sub(UNPC_Y, nd->bl.y);
			getunitdata_sub(UNPC_LOOKDIR, nd->ud.dir);
			getunitdata_sub(UNPC_STR, nd->status.str);
			getunitdata_sub(UNPC_AGI, nd->status.agi);
			getunitdata_sub(UNPC_VIT, nd->status.vit);
			getunitdata_sub(UNPC_INT, nd->status.int_);
			getunitdata_sub(UNPC_DEX, nd->status.dex);
			getunitdata_sub(UNPC_LUK, nd->status.luk);
			getunitdata_sub(UNPC_PLUSALLSTAT, nd->stat_point);
			getunitdata_sub(UNPC_DMGIMMUNE, nd->ud.immune_attack);
			getunitdata_sub(UNPC_ATKRANGE, nd->status.rhw.range);
			getunitdata_sub(UNPC_ATK, nd->status.rhw.atk);
			getunitdata_sub(UNPC_MATK, nd->status.rhw.atk2);
			getunitdata_sub(UNPC_DEF, nd->status.def);
			getunitdata_sub(UNPC_MDEF, nd->status.mdef);
			getunitdata_sub(UNPC_HIT, nd->status.hit);
			getunitdata_sub(UNPC_FLEE, nd->status.flee);
			getunitdata_sub(UNPC_PDODGE, nd->status.flee2);
			getunitdata_sub(UNPC_CRIT, nd->status.cri);
			getunitdata_sub(UNPC_RACE, nd->status.race);
			getunitdata_sub(UNPC_ELETYPE, nd->status.def_ele);
			getunitdata_sub(UNPC_ELELEVEL, nd->status.ele_lv);
			getunitdata_sub(UNPC_AMOTION,  nd->status.amotion);
			getunitdata_sub(UNPC_ADELAY, nd->status.adelay);
			getunitdata_sub(UNPC_DMOTION, nd->status.dmotion);
			break;

		default:
			ShowWarning("buildin_getunitdata: Unknown object type!\n");
			script_pushint(st, -1);
			return 1;
	}

	return 0;
}

/// Changes the live data of a bl.
///
/// setunitdata <unit id>,<type>,<value>;
BUILDIN_FUNC(setunitdata) {
	struct block_list* bl = NULL;
	struct script_data* data;
	const char *mapname = NULL;
	TBL_MOB* md = NULL;
	TBL_HOM* hd = NULL;
	TBL_MER* mc = NULL;
	TBL_PET* pd = NULL;
	TBL_ELEM* ed = NULL;
	TBL_NPC* nd = NULL;
	int type, value = 0;

	bl = map_id2bl(script_getnum(st, 2));

	if (!bl) {
		ShowWarning("buildin_setunitdata: Error in finding object with given game ID %d!\n", script_getnum(st, 2));
		script_pushint(st, -1);
		return 1;
	}

	switch (bl->type) {
		case BL_MOB:  md = map_id2md(bl->id); break;
		case BL_HOM:  hd = map_id2hd(bl->id); break;
		case BL_PET:  pd = map_id2pd(bl->id); break;
		case BL_MER:  mc = map_id2mc(bl->id); break;
		case BL_ELEM: ed = map_id2ed(bl->id); break;
		case BL_NPC:
			nd = map_id2nd(bl->id);
			if (!nd->status.hp)
				status_calc_npc(nd, SCO_FIRST);
			else
				status_calc_npc(nd, SCO_NONE);
			break;
		default:
			ShowError("buildin_setunitdata: Invalid object!");
			return 1;
	}

	type = script_getnum(st, 3);
	data = script_getdata(st, 4);
	get_val(st, data);

	if (type == 5 && data_isstring(data))
		mapname = conv_str(st, data);
	else if (data_isint(data))
		value = conv_num(st, data);
	else {
		ShowError("buildin_setunitdata: Invalid data type for argument #3 (%d).", data->type);
		return 1;
	}

	switch (bl->type) {
	case BL_MOB:
		if (!md) {
			ShowWarning("buildin_setunitdata: Error in finding object BL_MOB!\n");
			script_pushint(st, -1);
			return 1;
		}
		switch (type) {
			case UMOB_SIZE: md->status.size = (unsigned char)value; break;
			case UMOB_LEVEL: md->level = (unsigned short)value; break;
			case UMOB_HP: status_set_hp(bl, (unsigned int)value, 0); clif_charnameack(0, &md->bl); break;
			case UMOB_MAXHP: (unsigned int)value; clif_charnameack(0, &md->bl); break;
			case UMOB_MASTERAID: md->master_id = value; break;
			case UMOB_MAPID: if (mapname) value = map_mapname2mapid(mapname); unit_warp(bl, (short)value, 0, 0, CLR_TELEPORT); break;
			case UMOB_X: if (!unit_walktoxy(bl, (short)value, md->bl.y, 2)) unit_movepos(bl, (short)value, md->bl.y, 0, 0); break;
			case UMOB_Y: if (!unit_walktoxy(bl, md->bl.x, (short)value, 2)) unit_movepos(bl, md->bl.x, (short)value, 0, 0); break;
			case UMOB_SPEED: md->status.speed = (unsigned short)value; status_calc_misc(bl, &md->status, md->level); break;
			case UMOB_MODE: md->status.mode = (enum e_mode)value; break;
			case UMOB_AI: md->special_state.ai = (enum mob_ai)value; break;
			case UMOB_SCOPTION: md->sc.option = (unsigned short)value; break;
			case UMOB_SEX: md->vd->sex = (char)value; break;
			case UMOB_CLASS: status_set_viewdata(bl, (unsigned short)value); break;
			case UMOB_HAIRSTYLE: clif_changelook(bl, LOOK_HAIR, (unsigned short)value); break;
			case UMOB_HAIRCOLOR: clif_changelook(bl, LOOK_HAIR_COLOR, (unsigned short)value); break;
			case UMOB_HEADBOTTOM: clif_changelook(bl, LOOK_HEAD_BOTTOM, (unsigned short)value); break;
			case UMOB_HEADMIDDLE: clif_changelook(bl, LOOK_HEAD_MID, (unsigned short)value); break;
			case UMOB_HEADTOP: clif_changelook(bl, LOOK_HEAD_TOP, (unsigned short)value); break;
			case UMOB_CLOTHCOLOR: clif_changelook(bl, LOOK_CLOTHES_COLOR, (unsigned short)value); break;
			case UMOB_SHIELD: clif_changelook(bl, LOOK_SHIELD, (unsigned short)value); break;
			case UMOB_WEAPON: clif_changelook(bl, LOOK_WEAPON, (unsigned short)value); break;
			case UMOB_LOOKDIR: unit_setdir(bl, (uint8)value); break;
			case UMOB_STR: md->status.str = (unsigned short)value; status_calc_misc(bl, &md->status, md->level); break;
			case UMOB_AGI: md->status.agi = (unsigned short)value; status_calc_misc(bl, &md->status, md->level); break;
			case UMOB_VIT: md->status.vit = (unsigned short)value; status_calc_misc(bl, &md->status, md->level); break;
			case UMOB_INT: md->status.int_ = (unsigned short)value; status_calc_misc(bl, &md->status, md->level); break;
			case UMOB_DEX: md->status.dex = (unsigned short)value; status_calc_misc(bl, &md->status, md->level); break;
			case UMOB_LUK: md->status.luk = (unsigned short)value; status_calc_misc(bl, &md->status, md->level); break;
			case UMOB_SLAVECPYMSTRMD: md->state.copy_master_mode = value > 0 ? 1 : 0; if (value > 0) { TBL_MOB *md2 = map_id2md(md->master_id); md->status.mode = md2->status.mode; } break;
			case UMOB_DMGIMMUNE: md->ud.immune_attack = (bool)value > 0 ? 1 : 0; break;
			case UMOB_ATKRANGE: md->status.rhw.range = (unsigned short)value; break;
			case UMOB_ATK: md->status.rhw.atk = (unsigned short)value; break;
			case UMOB_MATK: md->status.rhw.atk2 = (unsigned short)value; break;
			case UMOB_DEF: md->status.def = (signed char)value; break;
			case UMOB_MDEF: md->status.mdef = (signed char)value; break;
			case UMOB_HIT: md->status.hit = (short)value; break;
			case UMOB_FLEE: md->status.flee = (short)value; break;
			case UMOB_PDODGE: md->status.flee2 = (short)value; break;
			case UMOB_CRIT: md->status.cri = (short)value; break;
			case UMOB_RACE: md->status.race = (unsigned char)value; break;
			case UMOB_ELETYPE: md->status.def_ele = (unsigned char)value; break;
			case UMOB_ELELEVEL: md->status.ele_lv = (unsigned char)value; break;
			case UMOB_AMOTION: md->status.amotion = (short)value; break;
			case UMOB_ADELAY: md->status.adelay = (short)value; break;
			case UMOB_DMOTION: md->status.dmotion = (short)value; break;
			default:
				ShowError("buildin_setunitdata: Unknown data identifier %d for BL_MOB.\n", type);
				return 1;
			}
		break;

	case BL_HOM:
		if (!hd) {
			ShowWarning("buildin_setunitdata: Error in finding object BL_HOM!\n");
			script_pushint(st, -1);
			return 1;
		}
		switch (type) {
			case UHOM_SIZE: hd->base_status.size = (unsigned char)value; break;
			case UHOM_LEVEL: hd->homunculus.level = (unsigned short)value; break;
			case UHOM_HP: status_set_hp(bl, (unsigned int)value, 0); break;
			case UHOM_MAXHP: (unsigned int)value; break;
			case UHOM_SP: status_set_sp(bl, (unsigned int)value, 0); break;
			case UHOM_MAXSP: (unsigned int)value; break;
			case UHOM_MASTERCID: hd->homunculus.char_id = (uint32)value; break;
			case UHOM_MAPID: if (mapname) value = map_mapname2mapid(mapname); unit_warp(bl, (short)value, 0, 0, CLR_TELEPORT); break;
			case UHOM_X: if (!unit_walktoxy(bl, (short)value, hd->bl.y, 2)) unit_movepos(bl, (short)value, hd->bl.y, 0, 0); break;
			case UHOM_Y: if (!unit_walktoxy(bl, hd->bl.x, (short)value, 2)) unit_movepos(bl, hd->bl.x, (short)value, 0, 0); break;
			case UHOM_HUNGER: hd->homunculus.hunger = (short)value; clif_send_homdata(map_charid2sd(hd->homunculus.char_id), SP_HUNGRY, hd->homunculus.hunger); break;
			case UHOM_INTIMACY: (unsigned int)value; clif_send_homdata(map_charid2sd(hd->homunculus.char_id), SP_INTIMATE, hd->homunculus.intimacy / 100); break;
			case UHOM_SPEED: hd->base_status.speed = (unsigned short)value; status_calc_misc(bl, &hd->base_status, hd->homunculus.level); break;
			case UHOM_LOOKDIR: unit_setdir(bl, (uint8)value); break;
			case UHOM_CANMOVETICK: hd->ud.canmove_tick = value > 0 ? (unsigned int)value : 0; break;
			case UHOM_STR: hd->base_status.str = (unsigned short)value; status_calc_misc(bl, &hd->base_status, hd->homunculus.level); break;
			case UHOM_AGI: hd->base_status.agi = (unsigned short)value; status_calc_misc(bl, &hd->base_status, hd->homunculus.level); break;
			case UHOM_VIT: hd->base_status.vit = (unsigned short)value; status_calc_misc(bl, &hd->base_status, hd->homunculus.level); break;
			case UHOM_INT: hd->base_status.int_ = (unsigned short)value; status_calc_misc(bl, &hd->base_status, hd->homunculus.level); break;
			case UHOM_DEX: hd->base_status.dex = (unsigned short)value; status_calc_misc(bl, &hd->base_status, hd->homunculus.level); break;
			case UHOM_LUK: hd->base_status.luk = (unsigned short)value; status_calc_misc(bl, &hd->base_status, hd->homunculus.level); break;
			case UHOM_DMGIMMUNE: hd->ud.immune_attack = (bool)value > 0 ? 1 : 0; break;
			case UHOM_ATKRANGE: hd->base_status.rhw.range = (unsigned short)value; break;
			case UHOM_ATK: hd->base_status.rhw.atk = (unsigned short)value; break;
			case UHOM_MATK: hd->base_status.rhw.atk2 = (unsigned short)value; break;
			case UHOM_DEF: hd->base_status.def = (signed char)value; break;
			case UHOM_MDEF: hd->base_status.mdef = (signed char)value; break;
			case UHOM_HIT: hd->base_status.hit = (short)value; break;
			case UHOM_FLEE: hd->base_status.flee = (short)value; break;
			case UHOM_PDODGE: hd->base_status.flee2 = (short)value; break;
			case UHOM_CRIT: hd->base_status.cri = (short)value; break;
			case UHOM_RACE: hd->base_status.race = (unsigned char)value; break;
			case UHOM_ELETYPE: hd->base_status.def_ele = (unsigned char)value; break;
			case UHOM_ELELEVEL: hd->base_status.ele_lv = (unsigned char)value; break;
			case UHOM_AMOTION: hd->base_status.amotion = (short)value; break;
			case UHOM_ADELAY: hd->base_status.adelay = (short)value; break;
			case UHOM_DMOTION: hd->base_status.dmotion = (short)value; break;
			default:
				ShowError("buildin_setunitdata: Unknown data identifier %d for BL_HOM.\n", type);
				return 1;
			}
		break;

	case BL_PET:
		if (!pd) {
			ShowWarning("buildin_setunitdata: Error in finding object BL_PET!\n");
			script_pushint(st, -1);
			return 1;
		}
		switch (type) {
			case UPET_SIZE: pd->status.size = (unsigned char)value; break;
			case UPET_LEVEL: pd->pet.level = (unsigned short)value; break;
			case UPET_HP: status_set_hp(bl, (unsigned int)value, 0); break;
			case UPET_MAXHP: (unsigned int)value; break;
			case UPET_MASTERAID: pd->pet.account_id = (unsigned int)value; break;
			case UPET_MAPID: if (mapname) value = map_mapname2mapid(mapname); unit_warp(bl, (short)value, 0, 0, CLR_TELEPORT); break;
			case UPET_X: if (!unit_walktoxy(bl, (short)value, pd->bl.y, 2)) unit_movepos(bl, (short)value, md->bl.y, 0, 0); break;
			case UPET_Y: if (!unit_walktoxy(bl, pd->bl.x, (short)value, 2)) unit_movepos(bl, pd->bl.x, (short)value, 0, 0); break;
			case UPET_HUNGER: pd->pet.hungry = cap_value((short)value, 0, 100); clif_send_petdata(map_id2sd(pd->pet.account_id), pd, 2, pd->pet.hungry); break;
			case UPET_INTIMACY: pet_set_intimate(pd, (unsigned int)value); clif_send_petdata(map_id2sd(pd->pet.account_id), pd, 1, pd->pet.intimate); break;
			case UPET_SPEED: pd->status.speed = (unsigned short)value; status_calc_misc(bl, &pd->status, pd->pet.level); break;
			case UPET_LOOKDIR: unit_setdir(bl, (uint8)value); break;
			case UPET_CANMOVETICK: pd->ud.canmove_tick = value > 0 ? (unsigned int)value : 0; break;
			case UPET_STR: pd->status.str = (unsigned short)value; status_calc_misc(bl, &pd->status, pd->pet.level); break;
			case UPET_AGI: pd->status.agi = (unsigned short)value; status_calc_misc(bl, &pd->status, pd->pet.level); break;
			case UPET_VIT: pd->status.vit = (unsigned short)value; status_calc_misc(bl, &pd->status, pd->pet.level); break;
			case UPET_INT: pd->status.int_ = (unsigned short)value; status_calc_misc(bl, &pd->status, pd->pet.level); break;
			case UPET_DEX: pd->status.dex = (unsigned short)value; status_calc_misc(bl, &pd->status, pd->pet.level); break;
			case UPET_LUK: pd->status.luk = (unsigned short)value; status_calc_misc(bl, &pd->status, pd->pet.level); break;
			case UPET_DMGIMMUNE: pd->ud.immune_attack = (bool)value > 0 ? 1 : 0; break;
			case UPET_ATKRANGE: pd->status.rhw.range = (unsigned short)value; break;
			case UPET_ATK: pd->status.rhw.atk = (unsigned short)value; break;
			case UPET_MATK: pd->status.rhw.atk2 = (unsigned short)value; break;
			case UPET_DEF: pd->status.def = (signed char)value; break;
			case UPET_MDEF: pd->status.mdef = (signed char)value; break;
			case UPET_HIT: pd->status.hit = (short)value; break;
			case UPET_FLEE: pd->status.flee = (short)value; break;
			case UPET_PDODGE: pd->status.flee2 = (short)value; break;
			case UPET_CRIT: pd->status.cri = (short)value; break;
			case UPET_RACE: pd->status.race = (unsigned char)value; break;
			case UPET_ELETYPE: pd->status.def_ele = (unsigned char)value; break;
			case UPET_ELELEVEL: pd->status.ele_lv = (unsigned char)value; break;
			case UPET_AMOTION: pd->status.amotion = (short)value; break;
			case UPET_ADELAY: pd->status.adelay = (short)value; break;
			case UPET_DMOTION: pd->status.dmotion = (short)value; break;
			default:
				ShowError("buildin_setunitdata: Unknown data identifier %d for BL_PET.\n", type);
				return 1;
			}
		break;

	case BL_MER:
		if (!mc) {
			ShowWarning("buildin_setunitdata: Error in finding object BL_MER!\n");
			script_pushint(st, -1);
			return 1;
		}
		switch (type) {
			case UMER_SIZE: mc->base_status.size = (unsigned char)value; break;
			case UMER_HP: status_set_hp(bl, (unsigned int)value, 0); break;
			case UMER_MAXHP: (unsigned int)value; break;
			case UMER_MASTERCID: mc->mercenary.char_id = (uint32)value; break;
			case UMER_MAPID: if (mapname) value = map_mapname2mapid(mapname); unit_warp(bl, (short)value, 0, 0, CLR_TELEPORT); break;
			case UMER_X: if (!unit_walktoxy(bl, (short)value, mc->bl.y, 2)) unit_movepos(bl, (short)value, mc->bl.y, 0, 0); break;
			case UMER_Y: if (!unit_walktoxy(bl, mc->bl.x, (short)value, 2)) unit_movepos(bl, mc->bl.x, (short)value, 0, 0); break;
			case UMER_KILLCOUNT: mc->mercenary.kill_count = (unsigned int)value; break;
			case UMER_LIFETIME: mc->mercenary.life_time = (unsigned int)value; break;
			case UMER_SPEED: mc->base_status.speed = (unsigned short)value; status_calc_misc(bl, &mc->base_status, mc->db->lv); break;
			case UMER_LOOKDIR: unit_setdir(bl, (uint8)value); break;
			case UMER_CANMOVETICK: mc->ud.canmove_tick = value > 0 ? (unsigned int)value : 0; break;
			case UMER_STR: mc->base_status.str = (unsigned short)value; status_calc_misc(bl, &mc->base_status, mc->db->lv); break;
			case UMER_AGI: mc->base_status.agi = (unsigned short)value; status_calc_misc(bl, &mc->base_status, mc->db->lv); break;
			case UMER_VIT: mc->base_status.vit = (unsigned short)value; status_calc_misc(bl, &mc->base_status, mc->db->lv); break;
			case UMER_INT: mc->base_status.int_ = (unsigned short)value; status_calc_misc(bl, &mc->base_status, mc->db->lv); break;
			case UMER_DEX: mc->base_status.dex = (unsigned short)value; status_calc_misc(bl, &mc->base_status, mc->db->lv); break;
			case UMER_LUK: mc->base_status.luk = (unsigned short)value; status_calc_misc(bl, &mc->base_status, mc->db->lv); break;
			case UMER_DMGIMMUNE: mc->ud.immune_attack = (bool)value > 0 ? 1 : 0; break;
			case UMER_ATKRANGE: mc->base_status.rhw.range = (unsigned short)value; break;
			case UMER_ATK: mc->base_status.rhw.atk = (unsigned short)value; break;
			case UMER_MATK: mc->base_status.rhw.atk2 = (unsigned short)value; break;
			case UMER_DEF: mc->base_status.def = (signed char)value; break;
			case UMER_MDEF: mc->base_status.mdef = (signed char)value; break;
			case UMER_HIT: mc->base_status.hit = (short)value; break;
			case UMER_FLEE: mc->base_status.flee = (short)value; break;
			case UMER_PDODGE: mc->base_status.flee2 = (short)value; break;
			case UMER_CRIT: mc->base_status.cri = (short)value; break;
			case UMER_RACE: mc->base_status.race = (unsigned char)value; break;
			case UMER_ELETYPE: mc->base_status.def_ele = (unsigned char)value; break;
			case UMER_ELELEVEL: mc->base_status.ele_lv = (unsigned char)value; break;
			case UMER_AMOTION: mc->base_status.amotion = (short)value; break;
			case UMER_ADELAY: mc->base_status.adelay = (short)value; break;
			case UMER_DMOTION: mc->base_status.dmotion = (short)value; break;
			default:
				ShowError("buildin_setunitdata: Unknown data identifier %d for BL_MER.\n", type);
				return 1;
			}
		break;

	case BL_ELEM:
		if (!ed) {
			ShowWarning("buildin_setunitdata: Error in finding object BL_ELEM!\n");
			script_pushint(st, -1);
			return 1;
		}
		switch (type) {
			case UELE_SIZE: ed->base_status.size = (unsigned char)value; break;
			case UELE_HP: status_set_hp(bl, (unsigned int)value, 0); break;
			//case UELE_MAXHP: status_set_maxhp(bl, (unsigned int)value, 0); break;
			case UELE_SP: status_set_sp(bl, (unsigned int)value, 0); break;
			//case UELE_MAXSP: status_set_maxsp(bl, (unsigned int)value, 0); break;
			case UELE_MASTERCID: ed->elemental.char_id = (uint32)value; break;
			case UELE_MAPID: if (mapname) value = map_mapname2mapid(mapname); unit_warp(bl, (short)value, 0, 0, CLR_TELEPORT); break;
			case UELE_X: if (!unit_walktoxy(bl, (short)value, ed->bl.y, 2)) unit_movepos(bl, (short)value, ed->bl.y, 0, 0); break;
			case UELE_Y: if (!unit_walktoxy(bl, ed->bl.x, (short)value, 2)) unit_movepos(bl, ed->bl.x, (short)value, 0, 0); break;
			case UELE_LIFETIME: ed->elemental.life_time = (unsigned int)value; break;
			case UELE_MODE: ed->elemental.mode = (enum e_mode)value; break;
			case UELE_SPEED: ed->base_status.speed = (unsigned short)value; status_calc_misc(bl, &ed->base_status, ed->db->lv); break;
			case UELE_LOOKDIR: unit_setdir(bl, (uint8)value); break;
			case UELE_CANMOVETICK: ed->ud.canmove_tick = value > 0 ? (unsigned int)value : 0; break;
			case UELE_STR: ed->base_status.str = (unsigned short)value; status_calc_misc(bl, &ed->base_status, ed->db->lv); break;
			case UELE_AGI: ed->base_status.agi = (unsigned short)value; status_calc_misc(bl, &ed->base_status, ed->db->lv); break;
			case UELE_VIT: ed->base_status.vit = (unsigned short)value; status_calc_misc(bl, &ed->base_status, ed->db->lv); break;
			case UELE_INT: ed->base_status.int_ = (unsigned short)value; status_calc_misc(bl, &ed->base_status, ed->db->lv); break;
			case UELE_DEX: ed->base_status.dex = (unsigned short)value; status_calc_misc(bl, &ed->base_status, ed->db->lv); break;
			case UELE_LUK: ed->base_status.luk = (unsigned short)value; status_calc_misc(bl, &ed->base_status, ed->db->lv); break;
			case UELE_DMGIMMUNE: ed->ud.immune_attack = (bool)value > 0 ? 1 : 0; break;
			case UELE_ATKRANGE: ed->base_status.rhw.range = (unsigned short)value; break;
			case UELE_ATK: ed->base_status.rhw.atk = (unsigned short)value; break;
			case UELE_MATK: ed->base_status.rhw.atk2 = (unsigned short)value; break;
			case UELE_DEF: ed->base_status.def = (signed char)value; break;
			case UELE_MDEF: ed->base_status.mdef = (signed char)value; break;
			case UELE_HIT: ed->base_status.hit = (short)value; break;
			case UELE_FLEE: ed->base_status.flee = (short)value; break;
			case UELE_PDODGE: ed->base_status.flee2 = (short)value; break;
			case UELE_CRIT: ed->base_status.cri = (short)value; break;
			case UELE_RACE: ed->base_status.race = (unsigned char)value; break;
			case UELE_ELETYPE: ed->base_status.def_ele = (unsigned char)value; break;
			case UELE_ELELEVEL: ed->base_status.ele_lv = (unsigned char)value; break;
			case UELE_AMOTION: ed->base_status.amotion = (short)value; break;
			case UELE_ADELAY: ed->base_status.adelay = (short)value; break;
			case UELE_DMOTION: ed->base_status.dmotion = (short)value; break;
			default:
				ShowError("buildin_setunitdata: Unknown data identifier %d for BL_ELEM.\n", type);
				return 1;
			}
		break;

	case BL_NPC:
		if (!md) {
			ShowWarning("buildin_setunitdata: Error in finding object BL_NPC!\n");
			script_pushint(st, -1);
			return 1;
		}
		switch (type) {
			case UNPC_DISPLAY: status_set_viewdata(bl, (unsigned short)value); break;
			case UNPC_LEVEL: nd->level = (unsigned int)value; break;
			case UNPC_HP: status_set_hp(bl, (unsigned int)value, 0); break;
			case UNPC_MAXHP: (unsigned int)value; break;
			case UNPC_MAPID: if (mapname) value = map_mapname2mapid(mapname); unit_warp(bl, (short)value, 0, 0, CLR_TELEPORT); break;
			case UNPC_X: if (!unit_walktoxy(bl, (short)value, nd->bl.y, 2)) unit_movepos(bl, (short)value, nd->bl.x, 0, 0); break;
			case UNPC_Y: if (!unit_walktoxy(bl, nd->bl.x, (short)value, 2)) unit_movepos(bl, nd->bl.x, (short)value, 0, 0); break;
			case UNPC_LOOKDIR: unit_setdir(bl, (uint8)value); break;
/*Disabled until supported [15peaces]
			case UNPC_STR: nd->params.str = (unsigned short)value; status_calc_misc(bl, &nd->status, nd->level); break;
			case UNPC_AGI: nd->params.agi = (unsigned short)value; status_calc_misc(bl, &nd->status, nd->level); break;
			case UNPC_VIT: nd->params.vit = (unsigned short)value; status_calc_misc(bl, &nd->status, nd->level); break;
			case UNPC_INT: nd->params.int_ = (unsigned short)value; status_calc_misc(bl, &nd->status, nd->level); break;
			case UNPC_DEX: nd->params.dex = (unsigned short)value; status_calc_misc(bl, &nd->status, nd->level); break;
			case UNPC_LUK: nd->params.luk = (unsigned short)value; status_calc_misc(bl, &nd->status, nd->level); break;
*/
			case UNPC_PLUSALLSTAT: nd->stat_point = (unsigned int)value; break;
			case UNPC_ATKRANGE: nd->status.rhw.range = (unsigned short)value; break;
			case UNPC_ATK: nd->status.rhw.atk = (unsigned short)value; break;
			case UNPC_MATK: nd->status.rhw.atk2 = (unsigned short)value; break;
			case UNPC_DEF: nd->status.def = (signed char)value; break;
			case UNPC_MDEF: nd->status.mdef = (signed char)value; break;
			case UNPC_HIT: nd->status.hit = (short)value; break;
			case UNPC_FLEE: nd->status.flee = (short)value; break;
			case UNPC_PDODGE: nd->status.flee2 = (short)value; break;
			case UNPC_CRIT: nd->status.cri = (short)value; break;
			case UNPC_RACE: nd->status.race = (unsigned char)value; break;
			case UNPC_ELETYPE: nd->status.def_ele = (unsigned char)value; break;
			case UNPC_ELELEVEL: nd->status.ele_lv = (unsigned char)value; break;
			case UNPC_AMOTION: nd->status.amotion = (short)value; break;
			case UNPC_ADELAY: nd->status.adelay = (short)value; break;
			case UNPC_DMOTION: nd->status.dmotion = (short)value; break;
			default:
				ShowError("buildin_setunitdata: Unknown data identifier %d for BL_NPC.\n", type);
				return 1;
			}
		break;

	default:
		ShowWarning("buildin_setunitdata: Unknown object type!\n");
		script_pushint(st, -1);
		return 1;
	}

	// Client information updates
	switch (bl->type) {
		case BL_HOM:
			clif_send_homdata(hd->master, SP_ACK, 0);
			break;
		case BL_PET:
			clif_send_petstatus(pd->msd);
			break;
		case BL_MER:
			clif_mercenary_info(map_charid2sd(md->master_id));
			clif_mercenary_skillblock(map_charid2sd(md->master_id));
			break;
		case BL_ELEM:
			clif_elemental_info(ed->master);
			break;
	}

	return 0;
}

/// Gets the name of a bl.
/// Supported types are [MOB|HOM|PET|NPC].
/// MER and ELEM don't support custom names.
///
/// getunitname <unit id>;
BUILDIN_FUNC(getunitname) {
	struct block_list* bl = NULL;

	bl = map_id2bl(script_getnum(st, 2));

	if (!bl) {
		ShowWarning("buildin_getunitname: Error in finding object with given game ID %d!\n", script_getnum(st, 2));
		script_pushconststr(st, "Unknown");
		return 1;
	}

	script_pushstrcopy(st, status_get_name(bl));

	return 0;
}

/// Changes the name of a bl.
/// Supported types are [MOB|HOM|PET].
/// For NPC see 'setnpcdisplay', MER and ELEM don't support custom names.
///
/// setunitname <unit id>,<name>;
BUILDIN_FUNC(setunitname) {
	struct block_list* bl = NULL;
	TBL_MOB* md = NULL;
	TBL_HOM* hd = NULL;
	TBL_PET* pd = NULL;

	bl = map_id2bl(script_getnum(st, 2));

	if (!bl) {
		ShowWarning("buildin_setunitname: Error in finding object with given game ID %d!\n", script_getnum(st, 2));
		script_pushconststr(st, "Unknown");
		return 1;
	}

	switch (bl->type) {
		case BL_MOB:  md = map_id2md(bl->id); break;
		case BL_HOM:  hd = map_id2hd(bl->id); break;
		case BL_PET:  pd = map_id2pd(bl->id); break;
	}

	switch (bl->type) {
		case BL_MOB:
			if (!md) {
				ShowWarning("buildin_setunitname: Error in finding object BL_MOB!\n");
				script_pushconststr(st, "Unknown");
				return 1;
			}
			safestrncpy(md->name, script_getstr(st, 3), NAME_LENGTH);
			break;
		case BL_HOM:
			if (!hd) {
				ShowWarning("buildin_setunitname: Error in finding object BL_HOM!\n");
				script_pushconststr(st, "Unknown");
				return 1;
			}
			safestrncpy(hd->homunculus.name, script_getstr(st, 3), NAME_LENGTH);
			break;
		case BL_PET:
			if (!pd) {
				ShowWarning("buildin_setunitname: Error in finding object BL_PET!\n");
				script_pushconststr(st, "Unknown");
				return 1;
			}
			safestrncpy(pd->pet.name, script_getstr(st, 3), NAME_LENGTH);
			break;
		default:
			ShowWarning("buildin_setunitname: Unknown object type!\n");
			script_pushconststr(st, "Unknown");
			return 1;
	}
	clif_charnameack(0, bl); // Send update to client.

	return 0;
}

/// Makes the unit walk to target position or map.
/// Returns if it was successful.
///
/// unitwalk(<unit_id>,<x>,<y>) -> <bool>
/// unitwalk(<unit_id>,<map_id>) -> <bool>
BUILDIN_FUNC(unitwalk)
{
	struct block_list* bl;

	bl = map_id2bl(script_getnum(st,2));
	if(!bl)
		script_pushint(st, 0);
	else if( script_hasdata(st,4) ){
		int x = script_getnum(st,3);
		int y = script_getnum(st,4);
		script_pushint(st, unit_walktoxy(bl,x,y,0));// We'll use harder calculations.
	}else{
		int map_id = script_getnum(st,3);
		script_pushint(st, unit_walktobl(bl,map_id2bl(map_id),65025,1));
	}

	return 0;
}

/// Kills the unit.
///
/// unitkill <unit_id>;
BUILDIN_FUNC(unitkill)
{
	struct block_list* bl = map_id2bl(script_getnum(st,2));
	if( bl != NULL )
		status_kill(bl);

	return 0;
}

/// Warps the unit to the target position in the target map
/// Returns if it was successfull
///
/// unitwarp(<unit_id>,"<map name>",<x>,<y>) -> <bool>
BUILDIN_FUNC(unitwarp)
{
	int unit_id;
	int map;
	short x;
	short y;
	struct block_list* bl;
	const char *mapname;

	unit_id = script_getnum(st,2);
	mapname = script_getstr(st, 3);
	x = (short)script_getnum(st,4);
	y = (short)script_getnum(st,5);
	
	if (!unit_id) //Warp the script's runner
		bl = map_id2bl(st->rid);
	else
		bl = map_id2bl(unit_id);

	if(!strcmp(mapname,"this"))
		map = bl?bl->m:-1;
	else
		map = map_mapname2mapid(mapname);

	if( map >= 0 && bl != NULL )
		script_pushint(st, unit_warp(bl,map,x,y,CLR_OUTSIGHT));
	else
		script_pushint(st, 0);

	return 0;
}

/// Makes the unit attack the target.
/// If the unit is a player and <action type> is not 0, it does a continuous 
/// attack instead of a single attack.
/// Returns if the request was successfull.
///
/// unitattack(<unit_id>,"<target name>"{,<action type>}) -> <bool>
/// unitattack(<unit_id>,<target_id>{,<action type>}) -> <bool>
BUILDIN_FUNC(unitattack)
{
	struct block_list* unit_bl;
	struct block_list* target_bl = NULL;
	struct script_data* data;
	int actiontype = 0;

	// get unit
	unit_bl = map_id2bl(script_getnum(st,2));
	if(!unit_bl) {
		script_pushint(st, 0);
		return 0;
	}
	
	data = script_getdata(st, 3);
	get_val(st, data);
	if( data_isstring(data) ) {
		TBL_PC* sd = map_nick2sd(conv_str(st, data));
		if( sd != NULL )
			target_bl = &sd->bl;
	} else
		target_bl = map_id2bl(conv_num(st, data));
	// request the attack
	if( target_bl == NULL )
	{
		script_pushint(st, 0);
		return 0;
	}
	
	// get actiontype
	if( script_hasdata(st,4) )
		actiontype = script_getnum(st,4);

	switch( unit_bl->type )
	{
	case BL_PC:
		// FIXME: Leeching off a parse function
		clif_parse_ActionRequest_sub(((TBL_PC *)unit_bl), actiontype > 0 ? 0x07 : 0x00, target_bl->id, gettick());
		script_pushint(st, 1);
		return 0;
	case BL_MOB:
		((TBL_MOB *)unit_bl)->target_id = target_bl->id;
		break;
	case BL_PET:
		((TBL_PET *)unit_bl)->target_id = target_bl->id;
		break;
	default:
		ShowError("script:unitattack: unsupported source unit type %d\n", unit_bl->type);
		script_pushint(st, 0);
		return 1;
	}
	script_pushint(st, unit_walktobl(unit_bl, target_bl, 65025, 2));
	return 0;
}

/// Makes the unit stop attacking.
///
/// unitstopattack <unit_id>;
BUILDIN_FUNC(unitstopattack)
{
	int unit_id;
	struct block_list* bl;

	unit_id = script_getnum(st,2);

	bl = map_id2bl(unit_id);
	if( bl != NULL ) {
		unit_stop_attack(bl);
		if( bl->type == BL_MOB )
			((TBL_MOB*)bl)->target_id = 0;
	}

	return 0;
}

/// Makes the unit stop walking.
///
/// unitstopwalk <unit_id>;
BUILDIN_FUNC(unitstopwalk)
{
	int unit_id;
	struct block_list* bl;

	unit_id = script_getnum(st,2);

	bl = map_id2bl(unit_id);

	if (bl != NULL) {
 		unit_stop_walking(bl,4);
		if (bl->type == BL_MOB)
 			((TBL_MOB*)bl)->target_id = 0;
 	}
 
 	return 0;
 }

/// Makes the unit say the given message.
///
/// unittalk <unit_id>,"<message>"{,hide_name};
BUILDIN_FUNC(unittalk)
{
	int unit_id;
	const char* message;
	struct block_list* bl;
	bool hide_name = false;

	unit_id = script_getnum(st,2);
	message = script_getstr(st, 3);

	if (script_hasdata(st, 4)) {
		hide_name = (script_getnum(st, 4) == 0) ? true : false;
	}

	bl = map_id2bl(unit_id);
	if( bl != NULL ) {
		struct StringBuf sbuf;

		StringBuf_Init(&sbuf);

		if (hide_name) {
			StringBuf_Printf(&sbuf, "%s", message);
		}
		else {
			StringBuf_Printf(&sbuf, "%s : %s", status_get_name(bl), message);
		}
		clif_disp_overhead(bl, StringBuf_Value(&sbuf));
		if( bl->type == BL_PC )
			clif_displaymessage(((TBL_PC*)bl)->fd, StringBuf_Value(&sbuf));
		StringBuf_Destroy(&sbuf);
	}

	return 0;
}

/// Makes the unit do an emotion.
///
/// unitemote <unit_id>,<emotion>;
///
/// @see e_* in const.txt
BUILDIN_FUNC(unitemote)
{
	int unit_id;
	int emotion;
	struct block_list* bl;

	unit_id = script_getnum(st,2);
	emotion = script_getnum(st,3);
	bl = map_id2bl(unit_id);
	
	if( bl != NULL )
		clif_emotion(bl, emotion);

	return 0;
}

/// Makes the unit cast the skill on the target or self if no target is specified.
///
/// unitskilluseid <unit_id>,<skill_id>,<skill_lv>{,<target_id>,<casttime>};
/// unitskilluseid <unit_id>,"<skill name>",<skill_lv>{,<target_id>,<casttime>};
BUILDIN_FUNC(unitskilluseid)
{
	int unit_id, target_id, casttime;
	uint16 skill_id, skill_lv;
	struct block_list* bl;

	unit_id  = script_getnum(st,2);
	skill_id = ( script_isstring(st,3) ? skill_name2id(script_getstr(st,3)) : script_getnum(st,3) );
	skill_lv = script_getnum(st,4);
	target_id = ( script_hasdata(st,5) ? script_getnum(st,5) : unit_id );

	casttime = ( script_hasdata(st,6) ? script_getnum(st,6) : 0 );
	bl = map_id2bl(unit_id);
	if (bl != NULL)
		unit_skilluse_id2(bl, target_id, skill_id, skill_lv, (casttime * 1000) + skill_castfix(bl, skill_id, skill_lv), skill_get_castcancel(skill_id));

	return 0;
}

/// Makes the unit cast the skill on the target position.
///
/// unitskillusepos <unit_id>,<skill_id>,<skill_lv>,<target_x>,<target_y>{,<casttime>};
/// unitskillusepos <unit_id>,"<skill name>",<skill_lv>,<target_x>,<target_y>{,<casttime>};
BUILDIN_FUNC(unitskillusepos)
{
	int unit_id, skill_x, skill_y, casttime;
	uint16 skill_id, skill_lv;
	struct block_list* bl;

	unit_id  = script_getnum(st,2);
	skill_id = ( script_isstring(st,3) ? skill_name2id(script_getstr(st,3)) : script_getnum(st,3) );
	skill_lv = script_getnum(st,4);
	skill_x  = script_getnum(st,5);
	skill_y  = script_getnum(st,6);

	casttime = ( script_hasdata(st,7) ? script_getnum(st,7) : 0 );
	bl = map_id2bl(unit_id);
	if (bl != NULL)
		unit_skilluse_pos2(bl, skill_x, skill_y, skill_id, skill_lv, (casttime * 1000) + skill_castfix(bl, skill_id, skill_lv), skill_get_castcancel(skill_id));

	return 0;
}

// <--- [zBuffer] List of mob control commands

/// Pauses the execution of the script, detaching the player
///
/// sleep <mili seconds>;
BUILDIN_FUNC(sleep)
{
	int ticks;
	
	ticks = script_getnum(st,2);

	// detach the player
	script_detach_rid(st);

	if( ticks <= 0 )
	{// do nothing
	}
	else if( st->sleep.tick == 0 )
	{// sleep for the target amount of time
		st->state = RERUNLINE;
		st->sleep.tick = ticks;
	}
	else
	{// sleep time is over
		st->state = RUN;
		st->sleep.tick = 0;
	}
	return 0;
}

/// Pauses the execution of the script, keeping the unit attached
/// Returns if the unit is still attached
///
/// sleep2(<mili secconds>) -> <bool>
BUILDIN_FUNC(sleep2)
{
	int ticks;
	
	ticks = script_getnum(st,2);

	if( ticks <= 0 )
	{// do nothing
		script_pushint(st, (map_id2bl(st->rid)!=NULL));
	}
	else if( !st->sleep.tick )
	{// sleep for the target amount of time
		st->state = RERUNLINE;
		st->sleep.tick = ticks;
	}
	else
	{// sleep time is over
		st->state = RUN;
		st->sleep.tick = 0;
		script_pushint(st, (map_id2bl(st->rid)!=NULL));
	}
	return 0;
}

/// Awakes all the sleep timers of the target npc
///
/// awake "<npc name>";
BUILDIN_FUNC(awake)
{
	struct npc_data* nd;
	struct linkdb_node *node = (struct linkdb_node *)sleep_db;

	nd = npc_name2id(script_getstr(st, 2));
	if (nd == NULL)
	{
		ShowError("awake: NPC \"%s\" not found\n", script_getstr(st, 2));
		return 1;
	}

	while (node)
	{
		if ((int)node->key == nd->bl.id)
		{// sleep timer for the npc
			struct script_state* tst = (struct script_state*)node->data;

			if (tst->sleep.timer == INVALID_TIMER)
			{// already awake ???
				node = node->next;
				continue;
			}

			delete_timer(tst->sleep.timer, run_script_timer);
			node = script_erase_sleepdb(node);
			// Trigger the timer function
			run_script_timer(INVALID_TIMER, gettick(), tst->sleep.charid, (intptr_t)tst);
		}
		else
		{
			node = node->next;
		}
	}
	return 0;
}

/// Returns a reference to a variable of the target NPC.
/// Returns 0 if an error occurs.
///
/// getvariableofnpc(<variable>, "<npc name>") -> <reference>
BUILDIN_FUNC(getvariableofnpc)
{
	struct script_data* data;
	const char* name;
	struct npc_data* nd;

	data = script_getdata(st,2);
	if( !data_isreference(data) )
	{// Not a reference (aka varaible name)
		ShowError("script:getvariableofnpc: not a variable\n");
		script_reportdata(data);
		script_pushnil(st);
		st->state = END;
		return 1;
	}

	name = reference_getname(data);
	if( *name != '.' || name[1] == '@' )
	{// not a npc variable
		ShowError("script:getvariableofnpc: invalid scope (not npc variable)\n");
		script_reportdata(data);
		script_pushnil(st);
		st->state = END;
		return 1;
	}

	nd = npc_name2id(script_getstr(st,3));
	if( nd == NULL || nd->subtype != NPCTYPE_SCRIPT || nd->u.scr.script == NULL )
	{// NPC not found or has no script
		ShowError("script:getvariableofnpc: can't find npc %s\n", script_getstr(st,3));
		script_pushnil(st);
		st->state = END;
		return 1;
	}

	push_val2(st->stack, C_NAME, reference_getuid(data), &nd->u.scr.script->script_vars );
	return 0;
}

/// Opens a warp portal.
/// Has no "portal opening" effect/sound, it opens the portal immediately.
///
/// warpportal <source x>,<source y>,"<target map>",<target x>,<target y>;
///
/// @author blackhole89
BUILDIN_FUNC(warpportal)
{
	int spx;
	int spy;
	unsigned short mapindex;
	int tpx;
	int tpy;
	struct skill_unit_group* group;
	struct block_list* bl;

	bl = map_id2bl(st->oid);
	if( bl == NULL )
	{
		ShowError("script:warpportal: npc is needed\n");
		return 1;
	}

	spx = script_getnum(st,2);
	spy = script_getnum(st,3);
	mapindex = mapindex_name2id(script_getstr(st, 4));
	tpx = script_getnum(st,5);
	tpy = script_getnum(st,6);

	if( mapindex == 0 )
		return 0;// map not found

	group = skill_unitsetting(bl, AL_WARP, 4, spx, spy, 0);
	if( group == NULL )
		return 0;// failed
	group->val2 = (tpx<<16) | tpy;
	group->val3 = mapindex;

	return 0;
}

BUILDIN_FUNC(openmail)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

#ifndef TXT_ONLY
	mail_openmail(sd);
#endif
	return 0;
}

BUILDIN_FUNC(openauction)
{
	TBL_PC* sd;

	sd = script_rid2sd(st);
	if( sd == NULL )
		return 0;

	if (!battle_config.feature_auction) {
		clif_disp_overheadcolor_self(sd->fd, COLOR_RED, msg_txt(517));
		return 0;
	}

#ifndef TXT_ONLY
	clif_Auction_openwindow(sd);
#endif
	return 0;
}

/// Retrieves the value of the specified flag of the specified cell.
///
/// checkcell("<map name>",<x>,<y>,<type>) -> <bool>
///
/// @see cell_chk* constants in const.txt for the types
BUILDIN_FUNC(checkcell)
{
	int m = map_mapname2mapid(script_getstr(st,2));
	int x = script_getnum(st,3);
	int y = script_getnum(st,4);
	cell_chk type = (cell_chk)script_getnum(st,5);

	script_pushint(st, map_getcell(m, x, y, type));

	return 0;
}

/// Modifies flags of cells in the specified area.
///
/// setcell "<map name>",<x1>,<y1>,<x2>,<y2>,<type>,<flag>;
///
/// @see cell_* constants in const.txt for the types
BUILDIN_FUNC(setcell)
{
	int m = map_mapname2mapid(script_getstr(st,2));
	int x1 = script_getnum(st,3);
	int y1 = script_getnum(st,4);
	int x2 = script_getnum(st,5);
	int y2 = script_getnum(st,6);
	cell_t type = (cell_t)script_getnum(st,7);
	bool flag = (bool)script_getnum(st,8);

	int x,y;

	if( x1 > x2 ) swap(x1,x2);
	if( y1 > y2 ) swap(y1,y2);

	for( y = y1; y <= y2; ++y )
		for( x = x1; x <= x2; ++x )
			map_setcell(m, x, y, type, flag);

	return 0;
}

/*==========================================
 * Mercenary Commands
 *------------------------------------------*/
BUILDIN_FUNC(mercenary_create)
{
#ifndef TXT_ONLY
	struct map_session_data *sd;
	int class_, contract_time;

	if( (sd = script_rid2sd(st)) == NULL || sd->md || sd->status.mer_id != 0 )
		return 0;
	
	class_ = script_getnum(st,2);

	if( !merc_class(class_) )
		return 0;

	if( sd->sc.data[SC__GROOMY] )
		return 0;

	contract_time = script_getnum(st,3);
	merc_create(sd, class_, contract_time);
#else
	script_pushint(st, 0);
#endif
	return 0;
}

BUILDIN_FUNC(mercenary_heal)
{
	struct map_session_data *sd = script_rid2sd(st);
	int hp, sp;

	if( sd == NULL || sd->md == NULL )
		return 0;
	hp = script_getnum(st,2);
	sp = script_getnum(st,3);

	status_heal(&sd->md->bl, hp, sp, 0);	
	return 0;
}

BUILDIN_FUNC(mercenary_sc_start)
{
	struct map_session_data *sd = script_rid2sd(st);
	enum sc_type type;
	int tick, val1;

	if( sd == NULL || sd->md == NULL )
		return 0;

	type = (sc_type)script_getnum(st,2);
	tick = script_getnum(st,3);
	val1 = script_getnum(st,4);

	status_change_start(&sd->md->bl, type, 10000, val1, 0, 0, 0, tick, 2);
	return 0;
}

BUILDIN_FUNC(mercenary_get_calls)
{
	struct map_session_data *sd = script_rid2sd(st);
	int guild;

	if( sd == NULL )
		return 0;

	guild = script_getnum(st,2);
	switch( guild )
	{
		case ARCH_MERC_GUILD:
			script_pushint(st,sd->status.arch_calls);
			break;
		case SPEAR_MERC_GUILD:
			script_pushint(st,sd->status.spear_calls);
			break;
		case SWORD_MERC_GUILD:
			script_pushint(st,sd->status.sword_calls);
			break;
		default:
			script_pushint(st,0);
			break;
	}

	return 0;
}

BUILDIN_FUNC(mercenary_set_calls)
{
	struct map_session_data *sd = script_rid2sd(st);
	int guild, value, *calls;

	if( sd == NULL )
		return 0;

	guild = script_getnum(st,2);
	value = script_getnum(st,3);

	switch( guild )
	{
		case ARCH_MERC_GUILD:
			calls = &sd->status.arch_calls;
			break;
		case SPEAR_MERC_GUILD:
			calls = &sd->status.spear_calls;
			break;
		case SWORD_MERC_GUILD:
			calls = &sd->status.sword_calls;
			break;
		default:
			return 0; // Invalid Guild
	}

	*calls += value;
	*calls = cap_value(*calls, 0, INT_MAX);

	return 0;
}

BUILDIN_FUNC(mercenary_get_faith)
{
	struct map_session_data *sd = script_rid2sd(st);
	int guild;

	if( sd == NULL )
		return 0;

	guild = script_getnum(st,2);
	switch( guild )
	{
		case ARCH_MERC_GUILD:
			script_pushint(st,sd->status.arch_faith);
			break;
		case SPEAR_MERC_GUILD:
			script_pushint(st,sd->status.spear_faith);
			break;
		case SWORD_MERC_GUILD:
			script_pushint(st,sd->status.sword_faith);
			break;
		default:
			script_pushint(st,0);
			break;
	}

	return 0;
}

BUILDIN_FUNC(mercenary_set_faith)
{
	struct map_session_data *sd = script_rid2sd(st);
	int guild, value, *calls;

	if( sd == NULL )
		return 0;

	guild = script_getnum(st,2);
	value = script_getnum(st,3);

	switch( guild )
	{
		case ARCH_MERC_GUILD:
			calls = &sd->status.arch_faith;
			break;
		case SPEAR_MERC_GUILD:
			calls = &sd->status.spear_faith;
			break;
		case SWORD_MERC_GUILD:
			calls = &sd->status.sword_faith;
			break;
		default:
			return 0; // Invalid Guild
	}

	*calls += value;
	*calls = cap_value(*calls, 0, INT_MAX);
	if( mercenary_get_guild(sd->md) == guild )
		clif_mercenary_updatestatus(sd,SP_MERCFAITH);

	return 0;
}

/*------------------------------------------
 * Book Reading
 *------------------------------------------*/
BUILDIN_FUNC(readbook)
{
	struct map_session_data *sd;
	int book_id, page;

	if( (sd = script_rid2sd(st)) == NULL )
		return 0;

	book_id = script_getnum(st,2);
	page = script_getnum(st,3);

	clif_readbook(sd->fd, book_id, page);
	return 0;
}

/******************
Questlog script commands
*******************/

 /**
 * Add job criteria to questinfo
 * @param qi Quest Info
 * @param job
 * @author [Cydh]
 **/
static void buildin_questinfo_setjob(struct questinfo *qi, int job) {
	RECREATE(qi->jobid, unsigned short, qi->jobid_count+1);
	qi->jobid[qi->jobid_count++] = job;
}

/**
 * questinfo <Quest ID>,<Icon>{,<Map Mark Color>{,<Job Class>}};
 **/
BUILDIN_FUNC(questinfo) {
	TBL_NPC* nd = map_id2nd(st->oid);
	int quest_id, icon;
	struct questinfo qi, *q2;

	if( nd == NULL || nd->bl.m == -1 ) {
		ShowError("buildin_questinfo: No NPC attached.\n");
		return 1;
	}

	quest_id = script_getnum(st, 2);
	icon = script_getnum(st, 3);

#if PACKETVER >= 20120410
	switch(icon) {
		case QTYPE_QUEST:
		case QTYPE_QUEST2:
		case QTYPE_JOB:
		case QTYPE_JOB2:
		case QTYPE_EVENT:
		case QTYPE_EVENT2:
		case QTYPE_WARG:
		case QTYPE_WARG2:
			// Leave everything as it is
			break;
		case QTYPE_NONE:
		default:
			// Default to nothing if icon id is invalid.
			icon = QTYPE_NONE;
			break;
	}
#else
	if(icon < QTYPE_QUEST || icon > 7) // TODO: check why 7 and not QTYPE_WARG, might be related to icon + 1 below
		icon = QTYPE_QUEST;
	else
		icon = icon + 1;
#endif

	qi.quest_id = quest_id;
	qi.icon = (unsigned char)icon;
	qi.nd = nd;

	if( script_hasdata(st, 4) ) {
		int color = script_getnum(st, 4);
		if( color < 0 || color > 3 ) {
			ShowWarning("buildin_questinfo: invalid color '%d', changing to 0\n",color);
			script_reportfunc(st);
			color = 0;
		}
		qi.color = (unsigned char)color;
	}

	qi.min_level = 1;
	qi.max_level = MAX_LEVEL;

	q2 = map_add_questinfo(nd->bl.m, &qi);
	q2->req = NULL;
	q2->req_count = 0;
	q2->jobid = NULL;
	q2->jobid_count = 0;

	if(script_hasdata(st, 5)) {
		int job = script_getnum(st, 5);

		if (!pcdb_checkid(job))
			ShowError("buildin_questinfo: Nonexistant Job Class.\n");
		else {
			buildin_questinfo_setjob(q2, job);
		}
	}

	return 0;
}

BUILDIN_FUNC(setquest)
{
	TBL_PC * sd = script_rid2sd(st);

	quest_add(sd, script_getnum(st, 2));

#if PACKETVER >= 20120410
	pc_show_questinfo(sd);
#endif

	return 0;
}

BUILDIN_FUNC(erasequest)
{
	TBL_PC * sd = script_rid2sd(st);

	quest_delete(sd, script_getnum(st, 2));
	return 0;
}

BUILDIN_FUNC(completequest)
{
	TBL_PC * sd = script_rid2sd(st);

	quest_update_status(sd, script_getnum(st, 2), Q_COMPLETE);

#if PACKETVER >= 20120410
	pc_show_questinfo(sd);
#endif

	return 0;
}

BUILDIN_FUNC(changequest)
{
	TBL_PC * sd = script_rid2sd(st);

	quest_change(sd, script_getnum(st, 2),script_getnum(st, 3));

#if PACKETVER >= 20120410
	pc_show_questinfo(sd);
#endif

	return 0;
}

BUILDIN_FUNC(checkquest)
{
	TBL_PC * sd = script_rid2sd(st);
	quest_check_type type = HAVEQUEST;

	if( script_hasdata(st, 3) )
		type = (quest_check_type)script_getnum(st, 3);

	script_pushint(st, quest_check(sd, script_getnum(st, 2), type));

	return 0;
}

BUILDIN_FUNC(isbegin_quest)
{
	TBL_PC * sd = script_rid2sd(st);
	int i;

	nullpo_ret(sd);

	i = quest_check(sd, script_getnum(st, 2), (enum quest_check_type) HAVEQUEST);
	script_pushint(st, i + (i < 1));

	return 0;
}

BUILDIN_FUNC(showevent)
{
  TBL_PC *sd = script_rid2sd(st);
  struct npc_data *nd = map_id2nd(st->oid);
  int state, color;

  if( sd == NULL || nd == NULL )
     return 0;
  state = script_getnum(st, 2);
  color = script_getnum(st, 3);

  if( color < 0 || color > 4 )
     color = 0; // set default color

  clif_quest_show_event(sd, &nd->bl, state, color);
  return 0;
}

/*==========================================
 * BattleGround System
 *------------------------------------------*/
BUILDIN_FUNC(waitingroom2bg)
{
	struct npc_data *nd;
	struct chat_data *cd;
	const char *map_name, *ev = "", *dev = "";
	int x, y, i, mapindex = 0, bg_id, n;
	struct map_session_data *sd;

	if( script_hasdata(st,7) )
		nd = npc_name2id(script_getstr(st,7));
	else
		nd = (struct npc_data *)map_id2bl(st->oid);

	if( nd == NULL || (cd = (struct chat_data *)map_id2bl(nd->chat_id)) == NULL )
	{
		script_pushint(st,0);
		return 0;
	}

	map_name = script_getstr(st,2);
	if( strcmp(map_name,"-") != 0 )
	{
		mapindex = mapindex_name2id(map_name);
		if( mapindex == 0 )
		{ // Invalid Map
			script_pushint(st,0);
			return 0;
		}
	}

	x = script_getnum(st,3);
	y = script_getnum(st,4);
	ev = script_getstr(st,5); // Logout Event
	dev = script_getstr(st,6); // Die Event

	if( (bg_id = bg_create(mapindex, x, y, ev, dev)) == 0 )
	{ // Creation failed
		script_pushint(st,0);
		return 0;
	}

	n = cd->users;
	for( i = 0; i < n && i < MAX_BG_MEMBERS; i++ )
	{
		if( (sd = cd->usersd[i]) != NULL && bg_team_join(bg_id, sd) )
			mapreg_setreg(reference_uid(add_str("$@arenamembers"), i), sd->bl.id);
		else
			mapreg_setreg(reference_uid(add_str("$@arenamembers"), i), 0);
	}

	mapreg_setreg(add_str("$@arenamembersnum"), i);
	script_pushint(st,bg_id);
	return 0;
}

BUILDIN_FUNC(waitingroom2bg_single)
{
	const char* map_name;
	struct npc_data *nd;
	struct chat_data *cd;
	struct map_session_data *sd;
	int x, y, mapindex, bg_id;

	bg_id = script_getnum(st,2);
	map_name = script_getstr(st,3);
	if( (mapindex = mapindex_name2id(map_name)) == 0 )
		return 0; // Invalid Map

	x = script_getnum(st,4);
	y = script_getnum(st,5);
	nd = npc_name2id(script_getstr(st,6));

	if( nd == NULL || (cd = (struct chat_data *)map_id2bl(nd->chat_id)) == NULL || cd->users <= 0 )
		return 0;

	if( (sd = cd->usersd[0]) == NULL )
		return 0;

	if( bg_team_join(bg_id, sd) )
	{
		pc_setpos(sd, mapindex, x, y, CLR_TELEPORT);
		script_pushint(st,1);
	}
	else
		script_pushint(st,0);

	return 0;
}

BUILDIN_FUNC(bg_team_setxy)
{
	struct battleground_data *bg;
	int bg_id;

	bg_id = script_getnum(st,2);
	if( (bg = bg_team_search(bg_id)) == NULL )
		return 0;

	bg->x = script_getnum(st,3);
	bg->y = script_getnum(st,4);
	return 0;
}

BUILDIN_FUNC(bg_warp)
{
	int x, y, mapindex, bg_id;
	const char* map_name;

	bg_id = script_getnum(st,2);
	map_name = script_getstr(st,3);
	if( (mapindex = mapindex_name2id(map_name)) == 0 )
		return 0; // Invalid Map
	x = script_getnum(st,4);
	y = script_getnum(st,5);
	bg_team_warp(bg_id, mapindex, x, y);
	return 0;
}

BUILDIN_FUNC(bg_monster)
{
	int class_ = 0, x = 0, y = 0, bg_id = 0;
	const char *str,*map, *evt="";

	bg_id  = script_getnum(st,2);
	map    = script_getstr(st,3);
	x      = script_getnum(st,4);
	y      = script_getnum(st,5);
	str    = script_getstr(st,6);
	class_ = script_getnum(st,7);
	if( script_hasdata(st,8) ) evt = script_getstr(st,8);
	check_event(st, evt);
	script_pushint(st, mob_spawn_bg(map,x,y,str,class_,evt,bg_id));
	return 0;
}

BUILDIN_FUNC(bg_monster_set_team)
{
	struct mob_data *md;
	struct block_list *mbl;
	int id = script_getnum(st,2),
		bg_id = script_getnum(st,3);
	
	if( (mbl = map_id2bl(id)) == NULL || mbl->type != BL_MOB )
		return 0;
	md = (TBL_MOB *)mbl;
	md->bg_id = bg_id;

	mob_stop_attack(md);
	mob_stop_walking(md, 0);
	md->target_id = md->attacked_id = 0;
	clif_charnameack(0, &md->bl);

	return 0;
}

BUILDIN_FUNC(bg_leave)
{
	struct map_session_data *sd = script_rid2sd(st);
	if( sd == NULL || !sd->bg_id )
		return 0;
	
	bg_team_leave(sd,0);
	return 0;
}

BUILDIN_FUNC(bg_destroy)
{
	int bg_id = script_getnum(st,2);
	bg_team_delete(bg_id);
	return 0;
}

BUILDIN_FUNC(bg_getareausers)
{
	const char *str;
	int m, x0, y0, x1, y1, bg_id;
	int i = 0, c = 0;
	struct battleground_data *bg = NULL;
	struct map_session_data *sd;

	bg_id = script_getnum(st,2);
	str = script_getstr(st,3);

	if( (bg = bg_team_search(bg_id)) == NULL || (m = map_mapname2mapid(str)) < 0 )
	{
		script_pushint(st,0);
		return 0;
	}

	x0 = script_getnum(st,4);
	y0 = script_getnum(st,5);
	x1 = script_getnum(st,6);
	y1 = script_getnum(st,7);

	for( i = 0; i < MAX_BG_MEMBERS; i++ )
	{
		if( (sd = bg->members[i].sd) == NULL )
			continue;
		if( sd->bl.m != m || sd->bl.x < x0 || sd->bl.y < y0 || sd->bl.x > x1 || sd->bl.y > y1 )
			continue;
		c++;
	}

	script_pushint(st,c);
	return 0;
}

BUILDIN_FUNC(bg_updatescore)
{
	const char *str;
	int m;

	str = script_getstr(st,2);
	if( (m = map_mapname2mapid(str)) < 0 )
		return 0;

	map[m].bgscore_lion = script_getnum(st,3);
	map[m].bgscore_eagle = script_getnum(st,4);

	clif_bg_updatescore(m);
	return 0;
}

BUILDIN_FUNC(bg_get_data)
{
	struct battleground_data *bg;
	int bg_id = script_getnum(st,2),
		type = script_getnum(st,3);

	if( (bg = bg_team_search(bg_id)) == NULL )
	{
		script_pushint(st,0);
		return 0;
	}

	switch( type )
	{
		case 0: script_pushint(st, bg->count); break;
		default:
			ShowError("script:bg_get_data: unknown data identifier %d\n", type);
			break;
	}

	return 0;
}

/*==========================================
 * Instancing Script Commands
 *------------------------------------------*/

BUILDIN_FUNC(instance_create)
{
	const char *name;
	int party_id, res;

	name = script_getstr(st, 2);
	party_id = script_getnum(st, 3);

	res = instance_create(party_id, name);
	if( res == -4 ) // Already exists
	{
		script_pushint(st, -1);
		return 0;
	}
	else if( res < 0 )
	{
		const char *err;
		switch(res)
		{
		case -3: err = "No free instances"; break;
		case -2: err = "Invalid party ID"; break;
		case -1: err = "Invalid type"; break;
		default: err = "Unknown"; break;
		}
		ShowError("buildin_instance_create: %s [%d].\n", err, res);
		script_pushint(st, -2);
		return 0;
	}
	
	script_pushint(st, res);
	return 0;
}

BUILDIN_FUNC(instance_destroy)
{
	int instance_id;
	struct map_session_data *sd;
	struct party_data *p;

	if( script_hasdata(st, 2) )
		instance_id = script_getnum(st, 2);
	else if( st->instance_id )
		instance_id = st->instance_id;
	else if( (sd = script_rid2sd(st)) != NULL && sd->status.party_id && (p = party_search(sd->status.party_id)) != NULL && p->instance_id )
		instance_id = p->instance_id;
	else return 0;

	if( instance_id <= 0 || instance_id >= MAX_INSTANCE )
	{
		ShowError("buildin_instance_destroy: Trying to destroy invalid instance %d.\n", instance_id);
		return 0;
	}

	instance_destroy(instance_id);
	return 0;
}

BUILDIN_FUNC(instance_attachmap)
{
	const char *name;
	int m;
	int instance_id;
	bool usebasename = false;
	
	name = script_getstr(st,2);
	instance_id = script_getnum(st,3);
	if( script_hasdata(st,4) && script_getnum(st,4) > 0)
		usebasename = true;

	if( (m = instance_add_map(name, instance_id, usebasename)) < 0 ) // [Saithis]
	{
		ShowError("buildin_instance_attachmap: instance creation failed (%s): %d\n", name, m);
		script_pushconststr(st, "");
		return 0;
	}
	//ShowDebug("buildin_instance_attachmap: Map attached (%s): %d\n", name, m);
	script_pushconststr(st, map[m].name);
	
	return 0;
}

BUILDIN_FUNC(instance_detachmap)
{
	struct map_session_data *sd;
	struct party_data *p;
	const char *str;
	int m, instance_id;
 	
	str = script_getstr(st, 2);
	if( script_hasdata(st, 3) )
		instance_id = script_getnum(st, 3);
	else if( st->instance_id )
		instance_id = st->instance_id;
	else if( (sd = script_rid2sd(st)) != NULL && sd->status.party_id && (p = party_search(sd->status.party_id)) != NULL && p->instance_id )
		instance_id = p->instance_id;
	else return 0;
 	
	if( (m = map_mapname2mapid(str)) < 0 || (m = instance_map2imap(m,instance_id)) < 0 )
 	{
		ShowError("buildin_instance_detachmap: Trying to detach invalid map %s\n", str);
 		return 0;
 	}

 	instance_del_map(m);
	return 0;
}

BUILDIN_FUNC(instance_attach)
{
	int instance_id;
	
	instance_id = script_getnum(st, 2);
	if( instance_id <= 0 || instance_id >= MAX_INSTANCE )
		return 0;
	
	st->instance_id = instance_id;
	return 0;
}

BUILDIN_FUNC(instance_id)
{
	int type, instance_id;
	struct map_session_data *sd;
	struct party_data *p;
	
	if( script_hasdata(st, 2) )
	{
		type = script_getnum(st, 2);
		if( type == 0 )
			instance_id = st->instance_id;
		else if( type == 1 && (sd = script_rid2sd(st)) != NULL && sd->status.party_id && (p = party_search(sd->status.party_id)) != NULL )
			instance_id = p->instance_id;
		else
			instance_id = 0;
	}
	else
		instance_id = st->instance_id;

	script_pushint(st, instance_id);
	return 0;
}

BUILDIN_FUNC(instance_set_timeout)
{
	int progress_timeout, idle_timeout;
	int instance_id;
	struct map_session_data *sd;
	struct party_data *p;
	
	progress_timeout = script_getnum(st, 2);
	idle_timeout = script_getnum(st, 3);

	if( script_hasdata(st, 4) )
		instance_id = script_getnum(st, 4);
	else if( st->instance_id )
		instance_id = st->instance_id;
	else if( (sd = script_rid2sd(st)) != NULL && sd->status.party_id && (p = party_search(sd->status.party_id)) != NULL && p->instance_id )
		instance_id = p->instance_id;
	else return 0;

	if( instance_id > 0 )
		instance_set_timeout(instance_id, progress_timeout, idle_timeout);
		
	return 0;
}

BUILDIN_FUNC(instance_init)
{
	int instance_id = script_getnum(st, 2);

	if( instance[instance_id].state != INSTANCE_IDLE )
	{
		ShowError("instance_init: instance already initialized.\n");
		return 0;
	}

	instance_init(instance_id);
	return 0;
}

BUILDIN_FUNC(instance_announce)
{
	int         instance_id = script_getnum(st,2);
	const char *mes         = script_getstr(st,3);
	int         flag        = script_getnum(st,4);
	const char *fontColor   = script_hasdata(st,5) ? script_getstr(st,5) : NULL;
	int         fontType    = script_hasdata(st,6) ? script_getnum(st,6) : 0x190; // default fontType (FW_NORMAL)
	int         fontSize    = script_hasdata(st,7) ? script_getnum(st,7) : 12;    // default fontSize
	int         fontAlign   = script_hasdata(st,8) ? script_getnum(st,8) : 0;     // default fontAlign
	int         fontY       = script_hasdata(st,9) ? script_getnum(st,9) : 0;     // default fontY

	int i;
	struct map_session_data *sd;
	struct party_data *p;

	if( instance_id == 0 )
	{
		if( st->instance_id )
			instance_id = st->instance_id;
		else if( (sd = script_rid2sd(st)) != NULL && sd->status.party_id && (p = party_search(sd->status.party_id)) != NULL && p->instance_id )
			instance_id = p->instance_id;
		else return 0;
	}

	if( instance_id <= 0 || instance_id >= MAX_INSTANCE )
		return 0;
		
	for( i = 0; i < instance[instance_id].num_map; i++ )
		map_foreachinmap(buildin_announce_sub, instance[instance_id].map[i], BL_PC,
			mes, strlen(mes)+1, flag&0xf0, fontColor, fontType, fontSize, fontAlign, fontY);

	return 0;
}

BUILDIN_FUNC(instance_npcname)
{
	const char *str;
	int instance_id = 0;

	struct map_session_data *sd;
	struct party_data *p;
	struct npc_data *nd;
	
	str = script_getstr(st, 2);
	if( script_hasdata(st, 3) )
		instance_id = script_getnum(st, 3);
	else if( st->instance_id )
		instance_id = st->instance_id;
	else if( (sd = script_rid2sd(st)) != NULL && sd->status.party_id && (p = party_search(sd->status.party_id)) != NULL && p->instance_id )
		instance_id = p->instance_id;

	if( instance_id && (nd = npc_name2id(str)) != NULL )
 	{
		static char npcname[NAME_LENGTH];
		snprintf(npcname, sizeof(npcname), "dup_%d_%d", instance_id, nd->bl.id);
 		script_pushconststr(st,npcname);
	}
	else
	{
		ShowError("script:instance_npcname: invalid instance NPC (instance_id: %d, NPC name: \"%s\".)\n", instance_id, str);
		st->state = END;
		return 1;
	}

	return 0;
}

BUILDIN_FUNC(has_instance)
{
	struct map_session_data *sd;
	struct party_data *p;
 	const char *str;
	int m, instance_id = 0;
 
 	str = script_getstr(st, 2);
	if( script_hasdata(st, 3) )
		instance_id = script_getnum(st, 3);
	else if( st->instance_id )
		instance_id = st->instance_id;
	else if( (sd = script_rid2sd(st)) != NULL && sd->status.party_id && (p = party_search(sd->status.party_id)) != NULL && p->instance_id )
		instance_id = p->instance_id;

	if( !instance_id || (m = map_mapname2mapid(str)) < 0 || (m = instance_map2imap(m, instance_id)) < 0 )
	{
		script_pushconststr(st, "");
		return 0;
	}

	script_pushconststr(st, map[m].name);
	return 0;
}

BUILDIN_FUNC(instance_warpall)
{
	struct map_session_data *pl_sd;
	int m, i, instance_id;
	const char *mapn;
	int x, y;
	unsigned short mapindex;
	struct party_data *p = NULL;

	mapn = script_getstr(st,2);
	x    = script_getnum(st,3);
	y    = script_getnum(st,4);
	if( script_hasdata(st,5) )
		instance_id = script_getnum(st,5);
	else if( st->instance_id )
		instance_id = st->instance_id;
	else if( (pl_sd = script_rid2sd(st)) != NULL && pl_sd->status.party_id && (p = party_search(pl_sd->status.party_id)) != NULL && p->instance_id )
		instance_id = p->instance_id;
	else return 0;

	if( (m = map_mapname2mapid(mapn)) < 0 || (map[m].flag.src4instance && (m = instance_mapid2imapid(m, instance_id)) < 0) )
		return 0;

	if( !(p = party_search(instance[instance_id].party_id)) )
		return 0;

	mapindex = map_id2index(m);
	for( i = 0; i < MAX_PARTY; i++ )
		if( (pl_sd = p->data[i].sd) && map[pl_sd->bl.m].instance_id == st->instance_id ) pc_setpos(pl_sd,mapindex,x,y,CLR_TELEPORT);

	return 0;
}

/*==========================================
 * instance_check_party [malufett]
 * Values:
 * party_id : Party ID of the invoking character. [Required Parameter]
 * amount : Amount of needed Partymembers for the Instance. [Optional Parameter]
 * min : Minimum Level needed to join the Instance. [Optional Parameter]
 * max : Maxium Level allowed to join the Instance. [Optional Parameter]
 * Example: instance_check_party (getcharid(1){,amount}{,min}{,max});
 * Example 2: instance_check_party (getcharid(1),1,1,99);
 *------------------------------------------*/
BUILDIN_FUNC(instance_check_party) {
	struct map_session_data *pl_sd;
	int amount, min, max, i, party_id, c = 0;
	struct party_data *p = NULL;
	
	amount = script_hasdata(st,3) ? script_getnum(st,3) : 1; // Amount of needed Partymembers for the Instance.
	min = script_hasdata(st,4) ? script_getnum(st,4) : 1; // Minimum Level needed to join the Instance.
	max  = script_hasdata(st,5) ? script_getnum(st,5) : MAX_LEVEL; // Maxium Level allowed to join the Instance.
	
	if( min < 1 || min > MAX_LEVEL){
		ShowError("instance_check_party: Invalid min level, %d\n", min);
		return 0;
	} else if(  max < 1 || max > MAX_LEVEL){
		ShowError("instance_check_party: Invalid max level, %d\n", max);
		return 0;
	}
	
	if( script_hasdata(st,2) )
		party_id = script_getnum(st,2);
	else return 0;
	
	if( !(p = party_search(party_id)) ){
		script_pushint(st, 0); // Returns false if party does not exist.
		return 0;
	}
	
	for( i = 0; i < MAX_PARTY; i++ )
		if( (pl_sd = p->data[i].sd) )
			if(map_id2bl(pl_sd->bl.id)){
				if(pl_sd->status.base_level < min){
					script_pushint(st, 0);
					return 0;
				}else if(pl_sd->status.base_level > max){
					script_pushint(st, 0);
					return 0;
				}
				c++;
			}
	
	if(c < amount){
		script_pushint(st, 0); // Not enough Members in the Party to join Instance.
	}else
		script_pushint(st, 1);
	
	return 0;
}

BUILDIN_FUNC(instance_mapname) {
 	const char *map_name;
	int m;
	short instance_id = -1;
	
 	map_name = script_getstr(st,2);
	
	if( script_hasdata(st,3) )
		instance_id = script_getnum(st,3);
	else
		instance_id = st->instance_id;
	
	// Check that instance mapname is a valid map
	if( instance_id == -1 || (m = instance_mapname2imap(map_name,instance_id)) == -1 )
		script_pushconststr(st, "");
	else
		script_pushconststr(st, map[m].name);
	
	return 0;
}
/* modify an instances' reload-spawn point */
/* instance_set_respawn <map_name>,<x>,<y>{,<instance_id>} */
/* returns 1 when successful, 0 otherwise. */
BUILDIN_FUNC(instance_set_respawn) {
	const char *map_name;
	short instance_id = -1;
	short mid;
	short x,y;
	
	map_name = script_getstr(st,2);
	x = script_getnum(st, 3);
	y = script_getnum(st, 4);
	
	if( script_hasdata(st, 5) )
		instance_id = script_getnum(st, 5);
	else
		instance_id = st->instance_id;
	
	if( instance_id == -1 || !instance_is_valid(instance_id) )
		script_pushint(st, 0);
	else if( (mid = map_mapname2mapid(map_name)) == -1 ) {
		ShowError("buildin_instance_set_respawn: unknown map '%s'\n",map_name);
		script_pushint(st, 0);
	} else {
		int i;
		
		for(i = 0; i < instance[instance_id].num_map; i++) {
			if( map[instance[instance_id].map[i]].m == mid ) {
				instance[instance_id].respawn.map = map_id2index(mid);
				instance[instance_id].respawn.x = x;
				instance[instance_id].respawn.y = y;
				break;
			}
		}
		
		if( i != instance[instance_id].num_map )
			script_pushint(st, 1);
		else {
			ShowError("buildin_instance_set_respawn: map '%s' not part of instance '%s'\n",map_name,instance[instance_id].name);
			script_pushint(st, 0);
		}
	}
	
	return 0;
}

/*==========================================
 * Custom Fonts
 *------------------------------------------*/
BUILDIN_FUNC(setfont)
{
	struct map_session_data *sd = script_rid2sd(st);
	int font = script_getnum(st,2);
	if( sd == NULL )
		return 0;

	if( sd->user_font != font )
		sd->user_font = font;
	else
		sd->user_font = 0;
	
	clif_font(sd);
	return 0;
}

static int buildin_mobuseskill_sub(struct block_list *bl,va_list ap)
{
	TBL_MOB* md		= (TBL_MOB*)bl;
	struct block_list *tbl;
	int mobid		= va_arg(ap,int);
	int skillid		= va_arg(ap,int);
	int skilllv		= va_arg(ap,int);
	int casttime	= va_arg(ap,int);
	int cancel		= va_arg(ap,int);
	int emotion		= va_arg(ap,int);
	int target		= va_arg(ap,int);

	if( md->class_ != mobid )
		return 0;

	// 0:self, 1:target, 2:master, default:random
	switch( target )
	{
		case 0: tbl = map_id2bl(md->bl.id); break;
		case 1: tbl = map_id2bl(md->target_id); break;
		case 2: tbl = map_id2bl(md->master_id); break;
		default:tbl = battle_getenemy(&md->bl, DEFAULT_ENEMY_TYPE(md),skill_get_range2(&md->bl, skillid, skilllv)); break;
	}

	if( !tbl )
		return 0;

	if( md->ud.skilltimer != INVALID_TIMER ) // Cancel the casting skill.
		unit_skillcastcancel(bl,0);

	if( skill_get_casttype(skillid) == CAST_GROUND )
		unit_skilluse_pos2(&md->bl, tbl->x, tbl->y, skillid, skilllv, casttime, cancel);
	else
		unit_skilluse_id2(&md->bl, tbl->id, skillid, skilllv, casttime, cancel);

	clif_emotion(&md->bl, emotion);

	return 0;
}
/*==========================================
 * areamobuseskill "Map Name",<x>,<y>,<range>,<Mob ID>,"Skill Name"/<Skill ID>,<Skill Lv>,<Cast Time>,<Cancelable>,<Emotion>,<Target Type>;
 *------------------------------------------*/
BUILDIN_FUNC(areamobuseskill)
{
	struct block_list center;
	int m,range,mobid,skillid,skilllv,casttime,emotion,target,cancel;

	if( (m = map_mapname2mapid(script_getstr(st,2))) < 0 )
	{
		ShowError("areamobuseskill: invalid map name.\n");
		return 0;
	}

	if( map[m].flag.src4instance && st->instance_id && (m = instance_mapid2imapid(m, st->instance_id)) < 0 )
		return 0;

	center.m = m;
	center.x = script_getnum(st,3);
	center.y = script_getnum(st,4);
	range = script_getnum(st,5);
	mobid = script_getnum(st,6);
	skillid = ( script_isstring(st,7) ? skill_name2id(script_getstr(st,7)) : script_getnum(st,7) );
	if( (skilllv = script_getnum(st,8)) > battle_config.mob_max_skilllvl )
		skilllv = battle_config.mob_max_skilllvl;

	casttime = script_getnum(st,9);
	cancel = script_getnum(st,10);
	emotion = script_getnum(st,11);
	target = script_getnum(st,12);
	
	map_foreachinrange(buildin_mobuseskill_sub, &center, range, BL_MOB, mobid, skillid, skilllv, casttime, cancel, emotion, target);
	return 0;
}


BUILDIN_FUNC(progressbar) {
#if PACKETVER >= 20080318
	struct map_session_data * sd = script_rid2sd(st);
	const char * color;
	unsigned int second;

	if( !st || !sd )
		return 0;

	st->state = STOP;

	color = script_getstr(st,2);
	second = script_getnum(st,3);

	sd->progressbar.npc_id = st->oid;
	sd->progressbar.timeout = gettick() + second*1000;
	sd->state.workinprogress = WIP_DISABLE_ALL;

	clif_progressbar(sd, strtol(color, (char **)NULL, 0), second);
#endif
    return 0;
}

BUILDIN_FUNC(pushpc)
{
	int direction, cells, dx, dy;
	struct map_session_data* sd;

	if((sd = script_rid2sd(st))==NULL)
	{
		return 0;
	}

	direction = script_getnum(st,2);
	cells     = script_getnum(st,3);

	if(direction<0 || direction>7)
	{
		ShowWarning("buildin_pushpc: Invalid direction %d specified.\n", direction);
		script_reportsrc(st);

		direction%= 8;  // trim spin-over
	}

	if(!cells)
	{// zero distance
		return 0;
	}
	else if(cells<0)
	{// pushing backwards
		direction = (direction+4)%8;  // turn around
		cells     = -cells;
	}

	dx = dirx[direction];
	dy = diry[direction];

	unit_blown(&sd->bl, dx, dy, cells, 0);
	return 0;
}


/// Invokes buying store preparation window
/// buyingstore <slots>;
BUILDIN_FUNC(buyingstore)
{
	struct map_session_data* sd;

	if( ( sd = script_rid2sd(st) ) == NULL )
	{
		return 0;
	}

	buyingstore_setup(sd, script_getnum(st,2));
	return 0;
}


/// Invokes search store info window
/// searchstores <uses>,<effect>;
BUILDIN_FUNC(searchstores)
{
	unsigned short effect;
	unsigned int uses;
	struct map_session_data* sd;

	if( ( sd = script_rid2sd(st) ) == NULL )
	{
		return 0;
	}

	uses   = script_getnum(st,2);
	effect = script_getnum(st,3);

	if( !uses )
	{
		ShowError("buildin_searchstores: Amount of uses cannot be zero.\n");
		return 1;
	}

	if( effect > 1 )
	{
		ShowError("buildin_searchstores: Invalid effect id %hu, specified.\n", effect);
		return 1;
	}

	searchstore_open(sd, uses, effect);
	return 0;
}


/// Displays a number as large digital clock.
/// showdigit <value>[,<type>];
BUILDIN_FUNC(showdigit)
{
	unsigned int type = 0;
	int value;
	struct map_session_data* sd;

	if( ( sd = script_rid2sd(st) ) == NULL )
	{
		return 0;
	}

	value = script_getnum(st,2);

	if( script_hasdata(st,3) )
	{
		type = script_getnum(st,3);

		if( type > 3 )
		{
			ShowError("buildin_showdigit: Invalid type %u.\n", type);
			return 1;
		}
	}

	clif_showdigit(sd, (unsigned char)type, value);
	return 0;
}

/*========================================== 
 * countbound {<type>}; 
 * Creates an array of bounded item IDs 
 * Returns amount of items found 
 * Type: 
 *      1 - Account Bound 
 *      2 - Guild Bound 
 *      3 - Party Bound 
 *------------------------------------------*/ 
BUILDIN_FUNC(countbound) 
{ 
	int i, type, j=0, k=0; 
	TBL_PC *sd; 

	if( (sd = script_rid2sd(st)) == NULL ) 
		return 0; 
	 
	type = script_hasdata(st,2)?script_getnum(st,2):0; 
 
	for(i=0;i<MAX_INVENTORY;i++){ 
		if(sd->inventory.u.items_inventory[i].nameid > 0 && ( 
			(!type && sd->inventory.u.items_inventory[i].bound > 0) || 
			(type && sd->inventory.u.items_inventory[i].bound == type) 
		)) { 
			pc_setreg(sd,reference_uid(add_str("@bound_items"), k),sd->inventory.u.items_inventory[i].nameid); 
			k++; 
			j += sd->inventory.u.items_inventory[i].amount; 
		} 
	} 
       
	script_pushint(st,j); 
	return 0; 
}

/**
 * freeloop(<toggle>) -> toggles this script instance's looping-check ability
 **/
BUILDIN_FUNC(freeloop) {

	if( script_hasdata(st,2) ) {
		if( script_getnum(st,2) )
			st->freeloop = 1;
		else
			st->freeloop = 0;
	}

	script_pushint(st, st->freeloop);
	return 0;
}

/** 
 * @commands (script based) 
 **/ 
BUILDIN_FUNC(bindatcmd) 
{ 
	const char* atcmd; 
	const char* eventName; 
	int i = 0, level = 0, level2 = 0; 

	atcmd = script_getstr(st,2); 
	eventName = script_getstr(st,3); 

	if( script_hasdata(st,4) ) level = script_getnum(st,4); 
	if( script_hasdata(st,5) ) level2 = script_getnum(st,5); 

	// check if event is already binded 
	ARR_FIND(0, MAX_ATCMD_BINDINGS, i, strcmp(atcmd_binding[i].command,atcmd) == 0); 
	if (i < MAX_ATCMD_BINDINGS) 
	{ 
		safestrncpy(atcmd_binding[i].npc_event, eventName, EVENT_NAME_LENGTH); 
		atcmd_binding[i].level = level; 
		atcmd_binding[i].level2 = level2; 
	} 
	else 
	{ // make new binding 
		ARR_FIND(0, MAX_ATCMD_BINDINGS, i, atcmd_binding[i].command[0] == '\0'); 
		if( i < MAX_ATCMD_BINDINGS ) 
		{ 
			safestrncpy(atcmd_binding[i].command, atcmd, 50); 
			safestrncpy(atcmd_binding[i].npc_event, eventName, EVENT_NAME_LENGTH); 
			atcmd_binding[i].level = level; 
			atcmd_binding[i].level2 = level2; 
		} 
	} 

	return 0; 
} 
 
BUILDIN_FUNC(unbindatcmd) 
{ 
	const char* atcmd; 
	int i =  0; 

	atcmd = script_getstr(st, 2); 

	ARR_FIND(0, MAX_ATCMD_BINDINGS, i, strcmp(atcmd_binding[i].command, atcmd) == 0); 
	if( i < MAX_ATCMD_BINDINGS ) 
		memset(&atcmd_binding[i],0,sizeof(atcmd_binding[0])); 

	return 0; 
} 
 
BUILDIN_FUNC(useatcmd) 
{ 
	TBL_PC dummy_sd; 
	TBL_PC* sd; 
	int fd; 
	const char* cmd; 

	cmd = script_getstr(st,2); 

	if( st->rid ) 
	{ 
		sd = script_rid2sd(st); 
		fd = sd->fd; 
	} 
	else 
	{ // Use a dummy character. 
		sd = &dummy_sd; 
		fd = 0; 
 
		memset(&dummy_sd, 0, sizeof(TBL_PC)); 
		if( st->oid ) 
		{ 
			struct block_list* bl = map_id2bl(st->oid); 
			memcpy(&dummy_sd.bl, bl, sizeof(struct block_list)); 
			if( bl->type == BL_NPC ) 
				safestrncpy(dummy_sd.status.name, ((TBL_NPC*)bl)->name, NAME_LENGTH); 
		} 
	} 
 
	// compatibility with previous implementation (deprecated!) 
	/*if( cmd[0] != atcommand_symbol ) 
	{ 
		cmd += strlen(sd->status.name); 
		while( *cmd != atcommand_symbol && *cmd != 0 ) 
			cmd++; 
	} */
 
	is_atcommand(fd, sd, cmd, 1); 
	return 0;
}

/* Cast a skill on the attached player.
 * npcskill <skill id>, <skill lvl>, <stat point>, <NPC level>;
 * npcskill "<skill name>", <skill lvl>, <stat point>, <NPC level>; */
BUILDIN_FUNC(npcskill)
{
	uint16 skill_id;
	unsigned short skill_level;
	unsigned int stat_point;
	unsigned int npc_level;
	struct npc_data *nd;
	struct map_session_data *sd;
	struct script_data *data;
	
	data = script_getdata(st, 2);
	get_val(st, data); // Convert into value in case of a variable
	skill_id	= data_isstring(data) ? skill_name2id(script_getstr(st, 2)) : script_getnum(st, 2);
	skill_level	= script_getnum(st, 3);
	stat_point	= script_getnum(st, 4);
	npc_level	= script_getnum(st, 5);
	sd			= script_rid2sd(st);
	nd			= (struct npc_data *)map_id2bl(sd->npc_id);

	if (stat_point > battle_config.max_parameter_renewal_jobs) {
		ShowError("npcskill: stat point exceeded maximum of %d.\n",battle_config.max_parameter_renewal_jobs );
		return 0;
	}
	if (npc_level > MAX_LEVEL) {
		ShowError("npcskill: level exceeded maximum of %d.\n", MAX_LEVEL);
		return 0;
	}
	if (sd == NULL || nd == NULL) { //ain't possible, but I don't trust people.
		return 0;
	}

	nd->level = npc_level;
	nd->stat_point = stat_point;

	if (!nd->status.hp)
		status_calc_npc(nd, SCO_FIRST);
	else
		status_calc_npc(nd, SCO_NONE);

	if (skill_get_inf(skill_id)&INF_GROUND_SKILL)
		unit_skilluse_pos(&nd->bl, sd->bl.x, sd->bl.y, skill_id, skill_level);
	else
		unit_skilluse_id(&nd->bl, sd->bl.id, skill_id, skill_level);

	return 1;
}

/** Creates new guild and makes the 'char_id' or the invoker as guild master (player must be online)
* guild_create "<name>"{,<char_id>};
* @return 1 if attempt is success, 0 otherwise
* @author [Cydh]
*/
BUILDIN_FUNC(guild_create) {
	TBL_PC *sd = NULL;
	char guild[NAME_LENGTH];
	struct guild *g = NULL;
	int prev;

	if (script_hasdata(st,3)) {
		if (!(sd = map_charid2sd(script_getnum(st,3)))) {
			ShowError("buildin_guild_create: Player not found (CID: %d).\n", script_getnum(st,3));
			script_pushint(st, 0);
			return 0;
		}
	}
	else {
		if (!(sd = script_rid2sd(st))) {
			script_pushint(st, 0);
			return 1;
		}
	}

	safestrncpy(guild, script_getstr(st,2), NAME_LENGTH);
	if ((g = guild_searchname(guild))) {
		ShowError("buildin_guild_create: Guild is already exists (%s).\n", guild);
		script_pushint(st, 0);
		return 0;
	}

	prev = battle_config.guild_emperium_check;
	battle_config.guild_emperium_check = 0;
	script_pushint(st, guild_create(sd, guild));
	battle_config.guild_emperium_check = prev;
	return 1;
}

/** Breaks a spesific guild by 'guild_id'
* guild_break {<guild_id>};
* @return 1 if attempt is success, 0 otherwise
* @author [Cydh]
*/
BUILDIN_FUNC(guild_break) {
	struct guild *g = NULL;
	TBL_PC *sd = NULL;

	if (script_hasdata(st,2)) {
		if (!(g = guild_search(script_getnum(st,2)))) {
			ShowError("buildin_guild_break: Guild is not exists (Guild ID: %d).\n", script_getnum(st,2));
			script_pushint(st, 0);
			return 0;
		}
		sd = map_nick2sd(g->master);
	}
	else {
		if (!(sd = script_rid2sd(st))) {
			script_pushint(st, 0);
			return 0;
		}
		if (!sd->status.guild_id) {
			ShowError("buildin_guild_break: Player isn't join in any guild (CID: %d).\n", sd->status.char_id);
			script_pushint(st, 0);
			return 0;
		}
	}

	if (sd)
		script_pushint(st, guild_break(sd, g->name));
	else {
		intif_guild_break(g->guild_id);
		script_pushint(st, 1);
	}
	return 1;
}

/** Requests guild information
* guild_info <guild_id>,<type>;
* guild_info "<guild_id>",<type>;
* @author [Cydh]
*/
BUILDIN_FUNC(guild_info) {
	struct guild *g = NULL;
	struct script_data *data = script_getdata(st, 2);
	uint8 type = script_getnum(st, 3);
	bool val = false;

	get_val(st, data);
	if (data_isint(data)) {
		int guild_id = conv_num(st, data);
		if (!(g = guild_search(guild_id))) {
			ShowError("buildin_guild_info: Guild not found (Guild ID: %d).\n", guild_id);
			script_pushconststr(st, "");
			return 0;
		}
	}
	else {
		char name[NAME_LENGTH];
		strcpy(name, conv_str(st,data));
		if (!(g = guild_searchname(name))) {
			ShowError("buildin_guild_info: Guild not found (Guild Name: %s).\n", name);
			script_pushint(st, -1);
			return 0;
		}
		val = true;
	}

	switch (type) {
		case 0: // If use guild_id, return "guild name". If use "guild name", return guild_id
			if (val)
				script_pushint(st, g->guild_id);
			else
				script_pushstrcopy(st, g->name);
			break;
		case 1: // Guild Level
			script_pushint(st, g->guild_lv);
			break;
		case 2: // Connected member
			script_pushint(st, g->connect_member);
			break;
		case 3: // Max members
			script_pushint(st, g->max_member);
			break;
		case 4: // Average member level
			script_pushint(st, g->average_lv);
			break;
		case 5: // Allies
		case 6: // Enemies
			{
				uint8 i = 0, count = 0;
				val = (type == 5); // True for ally; False for enemy
				for (i = 0; i < ARRAYLENGTH(g->alliance); i++) {
					if (!g->alliance[i].guild_id)
						continue;
					if (val && !g->alliance[i].opposition) {
						mapreg_setreg(reference_uid(add_str("$@guildallies_id"), count), g->alliance[i].guild_id);
						mapreg_setregstr(reference_uid(add_str("$@guildallies_name$"), count), g->alliance[i].name);
						count++;
					}
					else if (!val && g->alliance[i].opposition) {
						mapreg_setreg(reference_uid(add_str("$@guildenemies_id"), count), g->alliance[i].guild_id);
						mapreg_setregstr(reference_uid(add_str("$@guildenemies_name$"), count), g->alliance[i].name);
						count++;
					}
				}
				script_pushint(st, count);
			}
			break;
		case 7: // Castles
			{
				uint8 count = 0;
				struct guild_castle* gc = NULL;
				DBIterator *iter = db_iterator(guild_get_castle_db());
				for (gc = dbi_first(iter); dbi_exists(iter); gc = dbi_next(iter)) {
					if (gc->guild_id == g->guild_id) {
						mapreg_setreg(reference_uid(add_str("$@guildcastles_id"), count), gc->castle_id);
						mapreg_setregstr(reference_uid(add_str("$@guildcastles_name$"), count), gc->castle_name);
						count++;
					}
				}
				dbi_destroy(iter);
				script_pushint(st, count);
			}
			break;
		default:
			ShowDebug("buildin_guild_info: Invalid info type %d.\n", type);
			if (val)
				script_pushint(st, -1);
			else
				script_pushconststr(st, "");
			break;
	}
	return 1;
}

/** Adds a player to a guild (player must be online)
* guild_addmember <guild_id>{,<char_id>};
* @return 1 if attempt is success, 0 otherwise
* @author [Cydh]
*/
BUILDIN_FUNC(guild_addmember) {
	TBL_PC *sd = NULL;
	int guild_id = script_getnum(st,2);
	struct guild *g = guild_search(guild_id);

	if (!g) {
		ShowError("buildin_guild_addmember: Guild not found (ID: %d).\n", guild_id);
		script_pushint(st, 0);
		return 0;
	}

	if (script_hasdata(st,3)) {
		if (!(sd = map_charid2sd(script_getnum(st,3)))) {
			ShowError("buildin_guild_addmember: Player not found (CID: %d).\n", script_getnum(st,3));
			script_pushint(st, 0);
			return 0;
		}
	}
	else {
		if (!(sd = script_rid2sd(st))) {
			script_pushint(st, 0);
			return 1;
		}
	}

	sd->guild_invite = guild_id;
	sd->guild_invite_account = 0;
	script_pushint(st, guild_reply_invite(sd, guild_id, 1));
	clif_disp_overheadcolor_self(sd->fd, COLOR_WHITE, g->name);
	return 1;
}

/** Expel player from a guild. Can't expel guild leader.
* If player is online, 'guild_id' and 'account_id' will be ignore.
* Otherwise, it needs 'guild_id' and 'account_id'.
* guild_delmember <char_id>{,"<message>"{,<guild_id>,<account_id>}};
* @return 1 if attempt is success, 0 otherwise
* @author [Cydh]
*/
BUILDIN_FUNC(guild_delmember) {
	int char_id = script_getnum(st,2);
	TBL_PC *sd = map_charid2sd(char_id);

	// Player is online
	if (sd) {
		if (!sd->status.guild_id || !sd->status.guild_id) {
			ShowError("buildin_guild_delmember: Player doesn't join in any guild (CID: %d/AID: %d).\n", sd->status.char_id, sd->status.account_id);
			script_pushint(st, 0);
			return 0;
		}
		//script_pushint(st, guild_leave(sd, sd->status.guild_id, sd->status.account_id, sd->status.char_id, script_hasdata(st, 3) ? script_getstr(st,3) : "", true));
		intif_guild_leave(sd->status.guild_id, sd->status.account_id, sd->status.char_id, 1, script_hasdata(st,3) ? script_getstr(st,3) : "");
		script_pushint(st, 1);
		return 1;
	}
	// Player isn't online
	else {
		int guild_id = script_getnum(st,4);
		struct guild *g = NULL;

		if (!(g = guild_search(guild_id))) {
			ShowError("buildin_guild_delmember: Guild not found (Guild ID: %d).\n", guild_id);
			script_pushint(st, 0);
			return 0;
		}
		else {
			int account_id = script_getnum(st,5);
			unsigned short i;
			if (g->member[0].account_id == account_id && g->member[0].char_id == char_id) {
				ShowError("buildin_guild_delmember: Cannot expel guild leader (GID: %d/AID: %d/CID: %d).\n", g->guild_id, account_id, char_id);
				script_pushint(st, 0);
				return 0;
			}
			ARR_FIND(0, MAX_GUILD, i, g->member[i].char_id == char_id);
			if (i >= MAX_GUILD) {
				ShowError("buildin_guild_delmember: Player isn't not a guild member (GID: %d/AID: %d/CID: %d).\n", g->guild_id, account_id, char_id);
				script_pushint(st, 0);
				return 0;
			}
			intif_guild_leave(g->guild_id, account_id, char_id, 1, script_hasdata(st,3) ? script_getstr(st,3) : "");
			script_pushint(st, 1);
			return 1;
		}
	}
	script_pushint(st, 0);
	return 0;
}

/** Changes guild master, new guild master must be online
* guild_changegm <guild_id>,<char_id>;
* @return 1 if attempt is success, 0 otherwise
* @author [Cydh]
*/
BUILDIN_FUNC(guild_changegm) {
	int guild_id = script_getnum(st, 2);
	int char_id = script_getnum(st, 3);
	struct guild *g = guild_search(guild_id);
	TBL_PC *sd = map_charid2sd(char_id);

	if (!g) {
		ShowError("buildin_guild_changegm: Guild not found (Guild ID: %d).\n", guild_id);
		script_pushint(st, 0);
		return 0;
	}
	if (!sd) {
		ShowError("buildin_guild_changegm: Player not found (CID: %d).\n", char_id);
		script_pushint(st, 0);
		return 0;
	}
	if (sd->status.guild_id != g->guild_id) {
		ShowError("buildin_guild_changegm: Player must be a guild member.\n");
		script_pushint(st, 0);
		return 0;
	}

	intif_guild_change_gm(guild_id, sd->status.name, strlen(sd->status.name)+1);
	script_pushint(st, 1);
	return 1;
}

// Clan System
BUILDIN_FUNC(clan_join)
{
	struct map_session_data *sd;
	int clan_id = script_getnum(st, 2);

	if (!script_charid2sd(3, sd))
	{
		script_pushint(st, false);
		return 1;
	}

	if (clan_member_join(sd, clan_id, sd->status.account_id, sd->status.char_id))
		script_pushint(st, true);
	else
		script_pushint(st, false);

	return 0;
}

BUILDIN_FUNC(clan_leave)
{
	struct map_session_data *sd;

	if (!script_charid2sd(2, sd))
	{
		script_pushint(st, false);
		return 1;
	}

	if (clan_member_leave(sd, sd->status.clan_id, sd->status.account_id, sd->status.char_id))
		script_pushint(st, true);
	else
		script_pushint(st, false);

	return 0;
}

/**
 * Turns a player into a monster and optionally can grant a SC attribute effect.
 * montransform <monster name/ID>, <duration>, <sc type>, <val1>, <val2>, <val3>, <val4>;
 * active_transform <monster name/ID>, <duration>, <sc type>, <val1>, <val2>, <val3>, <val4>;
 * @param monster: Monster ID or name
 * @param duration: Transform duration in millisecond (ms)
 * @param sc_type: Type of SC that will be affected during the transformation
 * @param val1: Value for SC
 * @param val2: Value for SC
 * @param val3: Value for SC
 * @param val4: Value for SC
 * @author: malufett
 */
BUILDIN_FUNC(montransform) {
	TBL_PC *sd;
	enum sc_type type;
	int tick, mob_id, val1, val2, val3, val4;
	struct script_data *data;
	val1 = val2 = val3 = val4 = 0;

	if(!(sd = script_rid2sd(st)))
		return 1;

	data = script_getdata(st, 2);
	get_val(st, data); // Convert into value in case of a variable
	if( data_isstring(data) )
		mob_id = mobdb_searchname(script_getstr(st, 2));
	else
		mob_id = mobdb_checkid(script_getnum(st, 2));

	tick = script_getnum(st, 3);

	if (script_hasdata(st, 4))
		type = (sc_type)script_getnum(st, 4);
	else
		type = SC_NONE;

	if (mob_id == 0) {
		if( data_isstring(data) )
			ShowWarning("buildin_montransform: Attempted to use non-existing monster '%s'.\n", script_getstr(st, 2));
		else
			ShowWarning("buildin_montransform: Attempted to use non-existing monster of ID '%d'.\n", script_getnum(st, 2));
		return 1;
	}

	if (mob_id == MOBID_EMPERIUM) {
		ShowWarning("buildin_montransform: Monster 'Emperium' cannot be used.\n");
		return 1;
	}

	if (!(type >= SC_NONE && type < SC_MAX)) {
		ShowWarning("buildin_montransform: Unsupported status change id %d\n", type);
		return 1;
	}

	if (script_hasdata(st, 5))
		val1 = script_getnum(st, 5);

	if (script_hasdata(st, 6))
		val2 = script_getnum(st, 6);

	if (script_hasdata(st, 7))
		val3 = script_getnum(st, 7);

	if (script_hasdata(st, 8))
		val4 = script_getnum(st, 8);

	if (tick != 0) {
		if (battle_config.gvg_mon_trans_disable && map_flag_gvg2(sd->bl.m)) {
			clif_displaymessage(sd->fd, msg_txt(739)); // Transforming into monster is not allowed in Guild Wars.
			return 1;
		}

		if (sd->disguise){
			clif_displaymessage(sd->fd, msg_txt(737)); // Cannot transform into monster while in disguise.
			return 1;
		}

		if (!strcmp(script_getfuncname(st), "active_transform")) {
			status_change_end(&sd->bl, SC_ACTIVE_MONSTER_TRANSFORM, INVALID_TIMER); // Clear previous
			sc_start2(&sd->bl, SC_ACTIVE_MONSTER_TRANSFORM, 100, mob_id, type, tick);
		} else {
			status_change_end(&sd->bl, SC_MONSTER_TRANSFORM, INVALID_TIMER); // Clear previous
			sc_start2(&sd->bl, SC_MONSTER_TRANSFORM, 100, mob_id, type, tick);
		}
		if (type != SC_NONE)
			sc_start4(&sd->bl, type, 100, val1, val2, val3, val4, tick);
	}

	return 0;
}

/**
 * Attach script to player for certain duration
 * bonus_script "<script code>",<duration>{,<flag>{,<type>{,<status_icon>{,<char_id>}}}};
 * @param "script code"
 * @param duration
 * @param flag
 * @param icon
 * @param char_id
* @author [Cydh]
 **/
BUILDIN_FUNC(bonus_script) {
	uint16 flag = 0;
	int16 icon = SI_BLANK;
	uint32 dur;
	uint8 type = 0;
	TBL_PC* sd;
	const char *script_str = NULL;
	struct s_bonus_script_entry *entry = NULL;

	if (script_hasdata(st,7)) {
		if (!(sd = map_charid2sd(script_getnum(st,7)))) {
			ShowError("buildin_bonus_script: Player CID=%d is not found.\n", script_getnum(st,7));
			return 1;
		}
	}
	else
		sd = script_rid2sd(st);

	if (sd == NULL)
		return 1;
	
	script_str = script_getstr(st,2);
	dur = 1000 * abs(script_getnum(st,3));
	FETCH(4, flag);
	FETCH(5, type);
	FETCH(6, icon);

	// No Script string, No Duration!
	if (script_str[0] == '\0' || !dur) {
		ShowError("buildin_bonus_script: Invalid! Script: \"%s\". Duration: %d\n", script_str, dur);
		return 1;
	}

	if (strlen(script_str) >= MAX_BONUS_SCRIPT_LENGTH) {
		ShowError("buildin_bonus_script: Script string to long: \"%s\".\n", script_str);
		return 1;
	}

	if (icon <= SI_BLANK || icon >= SI_MAX)
		icon = SI_BLANK;

	if ((entry = pc_bonus_script_add(sd, script_str, dur, (enum si_type)icon, flag, type))) {
		linkdb_insert(&sd->bonus_script.head, (void *)((intptr_t)entry), entry);
		status_calc_pc(sd,SCO_NONE);
	}
	return 0;
}

/**
* Removes all bonus script from player
* bonus_script_clear {<flag>,{<char_id>}};
* @param flag 0 - Except permanent bonus, 1 - With permanent bonus
* @param char_id Clear script from this character
* @author [Cydh]
*/
BUILDIN_FUNC(bonus_script_clear) {
	TBL_PC* sd;
	bool flag = false;

	if (script_hasdata(st,2))
		flag = script_getnum(st,2);

	if (script_hasdata(st,3))
		sd = map_charid2sd(script_getnum(st,3));
	else
		sd = script_rid2sd(st);

	if (sd == NULL)
		return 1;

	pc_bonus_script_clear(sd,(flag ? BSF_PERMANENT : BSF_REM_ALL));
	return 0;
}

/**
 * Display script message
 * showscript "<message>"{,<GID>};
 **/
BUILDIN_FUNC(showscript) {
	struct block_list *bl = NULL;
	const char *msg = script_getstr(st,2);
	int id = 0;

	if (script_hasdata(st,3)) {
		id = script_getnum(st,3);
		bl = map_id2bl(id);
	}
	else {
		bl = st->rid ? map_id2bl(st->rid) : map_id2bl(st->oid);
	}

	if (!bl) {
		ShowError("buildin_showscript: Script not attached. (id=%d, rid=%d, oid=%d)\n", id, st->rid, st->oid);
		script_pushint(st,0);
		return 1;
	}

	clif_showscript(bl, msg);

	script_pushint(st,1);
	return 0;
}

/*==========================================
 * Returns the episode the server is running on.
 * getepisode();
 *------------------------------------------*/
BUILDIN_FUNC(getepisode)
{
	script_pushint(st,battle_config.feature_episode);
	return 0;
}

/*==========================================
 * Returns the PACKETVER the server is running on.
 * getpacketver();
 *------------------------------------------*/
BUILDIN_FUNC(getpacketver)
{
	script_pushint(st,PACKETVER);
	return 0;
}

/*==========================================
 * jobcanentermap("<mapname>"{,<JobID>});
 * Check if (player with) JobID can enter the map.
 * @param mapname Map name
 * @param JobID Player's JobID (optional)
 *------------------------------------------*/
BUILDIN_FUNC(jobcanentermap)
{
	const char *mapname = script_getstr(st, 2);
	int mapidx = mapindex_name2id(mapname), m = -1;
	int jobid = 0;
	TBL_PC *sd = NULL;

	if (!mapidx)
	{// Invalid map
		script_pushint(st, false);
		return 0;
	}
	m = map_mapindex2mapid(mapidx);
	if (m == -1)
	{ // Map is on different map server
		ShowError("buildin_jobcanentermap: Map '%s' is not found in this server.\n", mapname);
		script_pushint(st, false);
		return 0;
	}

	if (script_hasdata(st, 3))
	{
		jobid = script_getnum(st, 3);
	}
	else
	{
		if (!(sd = script_rid2sd(st)))
		{
			script_pushint(st, false);
			return 1;
		}
		jobid = sd->status.class_;
	}

	script_pushint(st, pc_job_can_entermap((enum e_job)jobid, m, sd ? sd->gmlevel : 0));
	return 0;
}

/*==========================================
 * Return alliance information between the two guilds.
 * getguildalliance(<guild id1>,<guild id2>);
 * Return values:
 *	-2 - Guild ID1 does not exist
 *	-1 - Guild ID2 does not exist
 *	 0 - Both guilds have no relation OR guild ID aren't given
 *	 1 - Both guilds are allies
 *	 2 - Both guilds are antagonists
 *------------------------------------------*/
BUILDIN_FUNC(getguildalliance)
{
	struct guild *guild_data1, *guild_data2;
	int guild_id1, guild_id2, i = 0;

	guild_id1 = script_getnum(st, 2);
	guild_id2 = script_getnum(st, 3);

	if (guild_id1 < 1 || guild_id2 < 1)
	{
		script_pushint(st, 0);
		return 0;
	}

	if (guild_id1 == guild_id2)
	{
		script_pushint(st, 1);
		return 0;
	}

	guild_data1 = guild_search(guild_id1);
	guild_data2 = guild_search(guild_id2);

	if (guild_data1 == NULL)
	{
		ShowWarning("buildin_getguildalliance: Requesting non-existent GuildID1 '%d'.\n", guild_id1);
		script_pushint(st, -2);
		return 1;
	}
	if (guild_data2 == NULL)
	{
		ShowWarning("buildin_getguildalliance: Requesting non-existent GuildID2 '%d'.\n", guild_id2);
		script_pushint(st, -1);
		return 1;
	}

	ARR_FIND(0, MAX_GUILDALLIANCE, i, guild_data1->alliance[i].guild_id == guild_id2);
	if (i == MAX_GUILDALLIANCE)
	{
		script_pushint(st, 0);
		return 0;
	}

	if (guild_data1->alliance[i].opposition)
		script_pushint(st, 2);
	else
		script_pushint(st, 1);

	return 0;
}

/**
 * opendressroom({<char_id>});
 */
BUILDIN_FUNC(opendressroom)
{
#if PACKETVER >= 20150513
    TBL_PC* sd;

    if (!script_charid2sd(2, sd))
        return 1;

    clif_dressing_room(sd, 1);

    return 0;
#else
    return 1;
#endif
}

/**
 * closedressroom({<char_id>});
 */
BUILDIN_FUNC(closedressroom)
{
#if PACKETVER >= 20150513
    TBL_PC* sd;

    if (!script_charid2sd(2, sd))
        return 1;

    clif_dressing_room(sd, 0);

    return 0;
#else
    return 1;
#endif
}

BUILDIN_FUNC(hateffect){
#if PACKETVER >= 20150513
	struct map_session_data* sd;
	bool enable;
	int i, effectID;

	if (!(sd = script_rid2sd(st)))
		return 1;

	effectID = script_getnum(st, 2);
	enable = script_getnum(st, 3) ? true : false;

	if (effectID <= HAT_EF_MIN || effectID >= HAT_EF_MAX){
		ShowError("buildin_hateffect: unsupported hat effect id %d\n", effectID);
		return 1;
	}

	ARR_FIND(0, sd->hatEffectCount, i, sd->hatEffectIDs[i] == effectID);

	if (enable){
		if (i < sd->hatEffectCount){
			return 0;
		}

		RECREATE(sd->hatEffectIDs, uint32, sd->hatEffectCount + 1);
		sd->hatEffectIDs[sd->hatEffectCount] = effectID;
		sd->hatEffectCount++;
	}
	else{
		if (i == sd->hatEffectCount){
			return 0;
		}

		for (; i < sd->hatEffectCount - 1; i++){
			sd->hatEffectIDs[i] = sd->hatEffectIDs[i + 1];
		}

		sd->hatEffectCount--;

		if (!sd->hatEffectCount){
			aFree(sd->hatEffectIDs);
			sd->hatEffectIDs = NULL;
		}
	}

	if (!sd->state.connect_new){
		clif_hat_effect_single(sd, effectID, enable);
	}

#endif
	return 0;
}

/**
* Retrieves param of current random option. Intended for random option script only.
* getrandomoptinfo(<type>);
* @author [secretdataz]
**/
BUILDIN_FUNC(getrandomoptinfo) {
	struct map_session_data *sd;
	int val;
	int param = script_getnum(st, 2);
	if ((sd = script_rid2sd(st)) != NULL && current_equip_item_index != -1 && current_equip_opt_index != -1 && sd->inventory.u.items_inventory[current_equip_item_index].option[current_equip_opt_index].id) { 
		switch (param) 
		{
			case ROA_ID:
				val = sd->inventory.u.items_inventory[current_equip_item_index].option[current_equip_opt_index].id;
				break;
			case ROA_VALUE:
				val = sd->inventory.u.items_inventory[current_equip_item_index].option[current_equip_opt_index].value;
				break;
			case ROA_PARAM:
				val = sd->inventory.u.items_inventory[current_equip_item_index].option[current_equip_opt_index].param;
				break;
			default:
				ShowWarning("buildin_getrandomoptinfo: Invalid attribute type %d (Max %d).\n", param, MAX_ITEM_RDM_OPT);
				val = 0;
				break;
		}
		script_pushint(st, val);
	}
	else {
		script_pushint(st, 0);
	}
	return 0;
}

/**
* Retrieves a random option on an equipped item.
* getequiprandomoption(<equipment slot>,<index>,<type>{,<char id>});
* @author [secretdataz]
*/
BUILDIN_FUNC(getequiprandomoption) {
	struct map_session_data *sd;
	int val;
	short i = -1;
	int pos = script_getnum(st, 2);
	int index = script_getnum(st, 3);
	int type = script_getnum(st, 4);
	if (!script_charid2sd(5, sd))
	{
		script_pushint(st, -1);
		return 1;
	}
	if (index < 0 || index >= MAX_ITEM_RDM_OPT) {
		ShowError("buildin_getequiprandomoption: Invalid random option index %d.\n", index);
		script_pushint(st, -1);
		return 1;
	}
	if (equip_index_check(pos))
		i = pc_checkequip(sd, equip[pos], false);
	if (i < 0) {
		ShowError("buildin_getequiprandomoption: No item equipped at pos %d (CID=%d/AID=%d).\n", pos, sd->status.char_id, sd->status.account_id);
		script_pushint(st, -1);
		return 1;
	}

	switch (type) {
		case ROA_ID:
			val = sd->inventory.u.items_inventory[i].option[index].id;
			break;
		case ROA_VALUE:
			val = sd->inventory.u.items_inventory[i].option[index].value;
			break;
		case ROA_PARAM:
			val = sd->inventory.u.items_inventory[i].option[index].param;
			break;
		default:
			ShowWarning("buildin_getequiprandomoption: Invalid attribute type %d (Max %d).\n", type, MAX_ITEM_RDM_OPT);
			val = 0;
			break;
	}
	script_pushint(st, val);
	return 0;
}

/**
* Adds a random option on a random option slot on an equipped item and overwrites
* existing random option in the process.
* setrandomoption(<equipment slot>,<index>,<id>,<value>,<param>{,<char id>});
* @author [secretdataz]
*/
BUILDIN_FUNC(setrandomoption) {
	struct map_session_data *sd;
	struct s_random_opt_data *opt;
	int pos, index, id, value, param, ep;
	int i = -1;
	if (!script_charid2sd(7, sd))
		return 1;
	pos = script_getnum(st, 2);
	index = script_getnum(st, 3);
	id = script_getnum(st, 4);
	value = script_getnum(st, 5);
	param = script_getnum(st, 6);

	if ((opt = itemdb_randomopt_exists((short)id)) == NULL) {
		ShowError("buildin_setrandomoption: Random option ID %d does not exists.\n", id);
		script_pushint(st, 0);
		return 1;
	}
	if (index < 0 || index >= MAX_ITEM_RDM_OPT) {
		ShowError("buildin_setrandomoption: Invalid random option index %d.\n", index);
		script_pushint(st, 0);
		return 1;
	}
	if (equip_index_check(pos))
		i = pc_checkequip(sd, equip[pos], false);
	if (i >= 0) {
		ep = sd->inventory.u.items_inventory[i].equip;
		log_pick(&sd->bl, LOG_TYPE_SCRIPT, sd->inventory.u.items_inventory[i].nameid,-1, &sd->inventory.u.items_inventory[i]);
		pc_unequipitem(sd, i, 2);
		sd->inventory.u.items_inventory[i].option[index].id = id;
		sd->inventory.u.items_inventory[i].option[index].value = value;
		sd->inventory.u.items_inventory[i].option[index].param = param;
		clif_delitem(sd, i, 1, 3);
		log_pick(&sd->bl, LOG_TYPE_SCRIPT, sd->inventory.u.items_inventory[i].nameid,-1, &sd->inventory.u.items_inventory[i]);
		clif_additem(sd, i, 1, 0);
		pc_equipitem(sd, i, ep, false);
		script_pushint(st, 1);
		return 0;
	}

	ShowError("buildin_setrandomoption: No item equipped at pos %d (CID=%d/AID=%d).\n", pos, sd->status.char_id, sd->status.account_id);
	script_pushint(st, 0);
	return 1;
}

/**
 * Creates a new queue.
 *
 * @return The index of the created queue.
 */
int script_queue_create(void)
{
	struct script_queue *queue = NULL;
	int i;

	ARR_FIND(0, VECTOR_LENGTH(sc_queue), i, !VECTOR_INDEX(sc_queue, i).valid);

	if (i == VECTOR_LENGTH(sc_queue)) {
		VECTOR_ENSURE(sc_queue, 1, 1);
		VECTOR_PUSHZEROED(sc_queue);
	}
	queue = &VECTOR_INDEX(sc_queue, i);

	memset(&VECTOR_INDEX(sc_queue, i), 0, sizeof(VECTOR_INDEX(sc_queue, i)));

	queue->id = i;
	queue->valid = true;
	return i;
}

/**
 * Returns the queue with he given index, if it exists.
 *
 * @param idx The queue index.
 *
 * @return The queue, or NULL if it doesn't exist.
 */
struct script_queue *script_queue_get(int idx)
{
	if (idx < 0 || idx >= VECTOR_LENGTH(sc_queue) || !VECTOR_INDEX(sc_queue, idx).valid)
		return NULL;
	return &VECTOR_INDEX(sc_queue, idx);
}

/**
 * Adds an entry to the given queue.
 *
 * @param idx The queue index.
 * @param var The entry to add.
 * @retval false if the queue is invalid or the entry is already in the queue.
 * @retval true in case of success.
 */
bool script_queue_add(int idx, int var)
{
	int i;
	struct map_session_data *sd = NULL;
	struct script_queue *queue = NULL;

	if (idx < 0 || idx >= VECTOR_LENGTH(sc_queue) || !VECTOR_INDEX(sc_queue, idx).valid) {
		ShowWarning("script_queue_add: unknown queue id %d\n",idx);
		return false;
	}

	queue = &VECTOR_INDEX(sc_queue, idx);

	ARR_FIND(0, VECTOR_LENGTH(queue->entries), i, VECTOR_INDEX(queue->entries, i) == var);
	if (i != VECTOR_LENGTH(queue->entries)) {
		return false; // Entry already exists
	}

	VECTOR_ENSURE(queue->entries, 1, 1);
	VECTOR_PUSH(queue->entries, var);

	if (var >= START_ACCOUNT_NUM && (sd = map_id2sd(var)) != NULL) {
		VECTOR_ENSURE(sd->script_queues, 1, 1);
		VECTOR_PUSH(sd->script_queues, idx);
	}
	return true;
}

/**
 * Removes an entry from the given queue.
 *
 * @param idx The queue index.
 * @param var The entry to remove.
 * @retval true if the entry was removed.
 * @retval false if the entry wasn't in queue.
 */
bool script_queue_remove(int idx, int var)
{
	int i;
	struct map_session_data *sd = NULL;
	struct script_queue *queue = NULL;

	if (idx < 0 || idx >= VECTOR_LENGTH(sc_queue) || !VECTOR_INDEX(sc_queue, idx).valid) {
		ShowWarning("script_queue_remove: unknown queue id %d (used with var %d)\n",idx,var);
		return false;
	}

	queue = &VECTOR_INDEX(sc_queue, idx);

	ARR_FIND(0, VECTOR_LENGTH(queue->entries), i, VECTOR_INDEX(queue->entries, i) == var);
	if (i == VECTOR_LENGTH(queue->entries))
		return false;

	VECTOR_ERASE(queue->entries, i);

	if (var >= START_ACCOUNT_NUM && (sd = map_id2sd(var)) != NULL) {
		ARR_FIND(0, VECTOR_LENGTH(sd->script_queues), i, VECTOR_INDEX(sd->script_queues, i) == queue->id);

		if (i != VECTOR_LENGTH(sd->script_queues))
			VECTOR_ERASE(sd->script_queues, i);
	}
	return true;
}

/**
 * Clears a queue.
 *
 * @param idx The queue index.
 *
 * @retval true if the queue was correctly cleared.
 * @retval false if the queue didn't exist.
 */
bool script_queue_clear(int idx)
{
	struct script_queue *queue = NULL;

	if (idx < 0 || idx >= VECTOR_LENGTH(sc_queue) || !VECTOR_INDEX(sc_queue, idx).valid) {
		ShowWarning("script_queue_clear: unknown queue id %d\n",idx);
		return false;
	}

	queue = &VECTOR_INDEX(sc_queue, idx);

	while (VECTOR_LENGTH(queue->entries) > 0) {
		int entry = VECTOR_POP(queue->entries);
		struct map_session_data *sd = NULL;

		if (entry >= START_ACCOUNT_NUM && (sd = map_id2sd(entry)) != NULL) {
			int i;
			ARR_FIND(0, VECTOR_LENGTH(sd->script_queues), i, VECTOR_INDEX(sd->script_queues, i) == queue->id);

			if (i != VECTOR_LENGTH(sd->script_queues))
				VECTOR_ERASE(sd->script_queues, i);
		}
	}
	VECTOR_CLEAR(queue->entries);

	return true;
}

BUILDIN_FUNC(unloadnpc) {
	const char *name;
	struct npc_data* nd;

	name = script_getstr(st, 2);
	nd = npc_name2id(name);

	if( nd == NULL ){
		ShowError( "buildin_unloadnpc: npc '%s' was not found.\n", name );
		return 1;
	}

	npc_unload_duplicates(nd);
	npc_unload(nd);
	npc_read_event_script();

	return 0;
}

/**
* Set the quest info of quest_id only showed on player in level range.
* setquestinfo_level <quest_id>,<min_level>,<max_level>
* @author [Cydh]
**/
BUILDIN_FUNC(setquestinfo_level) {
	TBL_NPC* nd = map_id2nd(st->oid);
	int quest_id = script_getnum(st, 2);
	struct questinfo *qi = map_has_questinfo(nd->bl.m, nd, quest_id);

	if (!qi) {
		ShowError("buildin_setquestinfo_level: Quest with '%d' is not defined yet.\n", quest_id);
		return 1;
	}

	qi->min_level = script_getnum(st, 3);
	qi->max_level = script_getnum(st, 4);
	if (!qi->max_level)
		qi->max_level = MAX_LEVEL;

	return 0;
}

/**
* Set the quest info of quest_id only showed for player that has quest criteria
* setquestinfo_req <quest_id>,<quest_req_id>,<state>{,<quest_req_id>,<state>,...};
* @author [Cydh]
**/
BUILDIN_FUNC(setquestinfo_req) {
	TBL_NPC* nd = map_id2nd(st->oid);
	int quest_id = script_getnum(st, 2);
	struct questinfo *qi = map_has_questinfo(nd->bl.m, nd, quest_id);
	uint8 i = 0;
	uint8 num = script_lastdata(st) + 1;

	if (!qi) {
		ShowError("buildin_setquestinfo_req: Quest with '%d' is not defined yet.\n", quest_id);
		return 1;
	}

	if ((num + 1) % 2 != 0) {
		ShowError("buildin_setquestinfo_req: Unmatched parameters. Num: '%d'\n", num);
		return 1;
	}

	for (i = 3; i < num; i += 2) {
		RECREATE(qi->req, struct questinfo_req, qi->req_count + 1);
		qi->req[qi->req_count].quest_id = script_getnum(st, i);
		qi->req[qi->req_count].state = (script_getnum(st, i + 1) >= 2) ? 2 : (script_getnum(st, i + 1) <= 0) ? 0 : 1;
		qi->req_count++;
	}

	return 0;
}

/**
* Set the quest info of quest_id only showed for player that has specified Job
* setquestinfo_job <quest_id>,<job>{,<job>...};
* @author [Cydh]
**/
BUILDIN_FUNC(setquestinfo_job) {
	TBL_NPC* nd = map_id2nd(st->oid);
	int quest_id = script_getnum(st, 2);
	struct questinfo *qi = map_has_questinfo(nd->bl.m, nd, quest_id);
	int job_id = 0;
	uint8 i = 0;
	uint8 num = script_lastdata(st) + 1;

	if (!qi) {
		ShowError("buildin_setquestinfo_job: Quest with '%d' is not defined yet.\n", quest_id);
		return 1;
	}

	for (i = 3; i < num; i++) {
		job_id = script_getnum(st, i);
		if (!pcdb_checkid(job_id)) {
			ShowError("buildin_setquestinfo_job: Invalid job with id '%d'.\n", job_id);
			continue;
		}

		buildin_questinfo_setjob(qi, job_id);
	}

	return 0;
}

/**
* navigateto("<map>"{,<x>,<y>,<flag>,<hide_window>,<monster_id>,<char_id>});
*/
BUILDIN_FUNC(navigateto){
#if PACKETVER >= 20111010
	TBL_PC* sd;
	const char *map;
	uint16 x = 0, y = 0, monster_id = 0;
	uint8 flag = NAV_KAFRA_AND_AIRSHIP;
	bool hideWindow = true;

	map = script_getstr(st, 2);

	if (script_hasdata(st, 3))
		x = script_getnum(st, 3);
	if (script_hasdata(st, 4))
		y = script_getnum(st, 4);
	if (script_hasdata(st, 5))
		flag = (uint8)script_getnum(st, 5);
	if (script_hasdata(st, 6))
		hideWindow = script_getnum(st, 6) ? true : false;
	if (script_hasdata(st, 7))
		monster_id = script_getnum(st, 7);

	if (!script_charid2sd(8, sd))
		return 1;

	clif_navigateTo(sd, map, x, y, flag, hideWindow, monster_id);

	return 0;
#else
	return 1;
#endif
}

/*==========================================
 * Achievement System [Smokexyz/Hercules]
 *-----------------------------------------*/

BUILDIN_FUNC(achievement_iscompleted)
{
	struct map_session_data *sd = script_hasdata(st, 3) ? map_id2sd(script_getnum(st, 3)) : script_rid2sd(st);
	if (sd == NULL)
		return 1;

	int aid = script_getnum(st, 2);
	const struct achievement_data *ad = achievement_get(aid);
	if (ad == NULL) {
		ShowError("buildin_achievement_iscompleted: Invalid Achievement %d provided.\n", aid);
		return 1;
	}

	script_pushint(st, achievement_check_complete(sd, ad));
	return 0;
}

 /**
  * Validates an objective index for the given achievement.
  * Can be used for any achievement type.
  * @command achievement_progress(<ach_id>,<obj_idx>,<progress>,<incremental?>{,<char_id>});
  * @param aid         - achievement ID
  * @param obj_idx     - achievement objective index.
  * @param progress    - objective progress towards goal.
  * @Param incremental - (boolean) true to add the progress towards the goal,
  *                      false to use the progress only as a comparand.
  * @param account_id  - (optional) character ID to perform on.
  * @return true on success, false on failure.
  * @push 1 on success, 0 on failure.
  */
BUILDIN_FUNC(achievement_progress)
{
	struct map_session_data *sd = script_rid2sd(st);
	int aid = script_getnum(st, 2);
	int obj_idx = script_getnum(st, 3);
	int progress = script_getnum(st, 4);
	int incremental = script_getnum(st, 5);
	int account_id = script_hasdata(st, 6) ? script_getnum(st, 6) : 0;
	const struct achievement_data *ad = NULL;

	if ((ad = achievement_get(aid)) == NULL) {
		ShowError("buildin_achievement_progress: Invalid achievement ID %d received.\n", aid);
		script_pushint(st, 0);
		return 1;
	}

	if (obj_idx <= 0 || obj_idx > VECTOR_LENGTH(ad->objective)) {
		ShowError("buildin_achievement_progress: Invalid objective index %d received. (min: %d, max: %d)\n", obj_idx, 0, VECTOR_LENGTH(ad->objective));
		script_pushint(st, 0);
		return 1;
	}

	obj_idx--; // convert to array index.

	if (progress <= 0 || progress > VECTOR_INDEX(ad->objective, obj_idx).goal) {
		ShowError("buildin_achievement_progress: Progress exceeds goal limit for achievement id %d.\n", aid);
		script_pushint(st, 0);
		return 1;
	}

	if (incremental < 0 || incremental > 1) {
		ShowError("buildin_achievement_progress: Argument 4 expects boolean (0/1). provided value: %d\n", incremental);
		script_pushint(st, 0);
		return 1;
	}

	if (script_hasdata(st, 6)) {
		if (account_id <= 0) {
			ShowError("buildin_achievement_progress: Invalid Account id %d provided.\n", account_id);
			script_pushint(st, 0);
			return 1;
		}
		else if ((sd = map_id2sd(account_id)) == NULL) {
			ShowError("buildin_achievement_progress: Account with id %d was not found.\n", account_id);
			script_pushint(st, 0);
			return 1;
		}
	}

	if (achievement_validate(sd, aid, obj_idx, progress, incremental ? true : false))
		script_pushint(st, progress);
	else
		script_pushint(st, 0);

	return 0;
}

/**
* adopt("<parent_name>","<baby_name>");
* adopt(<parent_id>,<baby_id>);
*/
BUILDIN_FUNC(adopt)
{
	TBL_PC *sd, *b_sd;
	struct script_data *data;
	enum adopt_responses response;

	data = script_getdata(st, 2);
	get_val(st, data);

	if (data_isstring(data)) {
		const char *name = conv_str(st, data);

		sd = map_nick2sd(name);
		if (sd == NULL) {
			ShowError("buildin_adopt: Non-existant parent character %s requested.\n", name);
			return 1;
		}
	}
	else if (data_isint(data)) {
		uint32 char_id = conv_num(st, data);

		sd = map_charid2sd(char_id);
		if (sd == NULL) {
			ShowError("buildin_adopt: Non-existant parent character %d requested.\n", char_id);
			return 1;
		}
	}
	else {
		ShowError("buildin_adopt: Invalid data type for argument #1 (%d).", data->type);
		return 1;
	}

	data = script_getdata(st, 3);
	get_val(st, data);

	if (data_isstring(data)) {
		const char *name = conv_str(st, data);

		b_sd = map_nick2sd(name);
		if (b_sd == NULL) {
			ShowError("buildin_adopt: Non-existant baby character %s requested.\n", name);
			return 1;
		}
	}
	else if (data_isint(data)) {
		uint32 char_id = conv_num(st, data);

		b_sd = map_charid2sd(char_id);
		if (b_sd == NULL) {
			ShowError("buildin_adopt: Non-existant baby character %d requested.\n", char_id);
			return 1;
		}
	}
	else {
		ShowError("buildin_adopt: Invalid data type for argument #2 (%d).", data->type);
		return 1;
	}

	response = pc_try_adopt(sd, map_charid2sd(sd->status.partner_id), b_sd);

	if (response == ADOPT_ALLOWED) {
		TBL_PC *p_sd = map_charid2sd(sd->status.partner_id);

		b_sd->adopt_invite = sd->status.account_id;
		clif_Adopt_request(b_sd, sd, p_sd->status.account_id);
		script_pushint(st, ADOPT_ALLOWED);
		return 0;
	}

	script_pushint(st, response);
	return 1;
}

/// declarations that were supposed to be exported from npc_chat.c
#ifdef PCRE_SUPPORT
BUILDIN_FUNC(defpattern);
BUILDIN_FUNC(activatepset);
BUILDIN_FUNC(deactivatepset);
BUILDIN_FUNC(deletepset);
#endif

/// script command definitions
/// for an explanation on args, see add_buildin_func
struct script_function buildin_func[] = {
	// NPC interaction
	BUILDIN_DEF(mes,"s"),
	BUILDIN_DEF(next,""),
	BUILDIN_DEF(close,""),
	BUILDIN_DEF(close2,""),
	BUILDIN_DEF(menu,"sl*"),
	BUILDIN_DEF(select,"s*"), //for future jA script compatibility
	BUILDIN_DEF(prompt,"s*"),
	//
	BUILDIN_DEF(goto,"l"),
	BUILDIN_DEF(callsub,"l*"),
	BUILDIN_DEF(callfunc,"s*"),
	BUILDIN_DEF(return,"?"),
	BUILDIN_DEF(getarg,"i?"),
	BUILDIN_DEF(getargcount,""),
	BUILDIN_DEF(jobchange,"i?"),
	BUILDIN_DEF(jobname,"i"),
	BUILDIN_DEF(input,"r??"),
	BUILDIN_DEF(warp,"sii"),
	BUILDIN_DEF(areawarp,"siiiisii"),
	BUILDIN_DEF(warpchar,"siii"), // [LuzZza]
	BUILDIN_DEF(warpparty,"siii?"), // [Fredzilla] [Paradox924X]
	BUILDIN_DEF(warpguild,"siii"), // [Fredzilla]
	BUILDIN_DEF(setlook,"ii"),
	BUILDIN_DEF(changelook,"ii"), // Simulates but don't Store it
	BUILDIN_DEF(set,"rv"),
	BUILDIN_DEF(setarray,"rv*"),
	BUILDIN_DEF(cleararray,"rvi"),
	BUILDIN_DEF(copyarray,"rri"),
	BUILDIN_DEF(getarraysize,"r"),
	BUILDIN_DEF(deletearray,"r?"),
	BUILDIN_DEF(getelementofarray,"ri"),
	BUILDIN_DEF(getitem,"vi?"),
	BUILDIN_DEF(rentitem,"vi"),
	BUILDIN_DEF(getitem2,"viiiiiiii?"),
	BUILDIN_DEF(getnameditem,"vv"),
	BUILDIN_DEF2(grouprandomitem,"groupranditem","i"),
	BUILDIN_DEF(getitempackage,"i"),
	BUILDIN_DEF(makeitem,"visii"),
	BUILDIN_DEF(makeitem2, "visiiiiiiiii"),
	BUILDIN_DEF(delitem,"vi?"),
	BUILDIN_DEF2(delitem,"storagedelitem","vi?"),
	BUILDIN_DEF2(delitem,"cartdelitem","vi?"),
	BUILDIN_DEF(delitem2,"viiiiiiii?"),
	BUILDIN_DEF2(delitem2,"storagedelitem2","viiiiiiii?"),
	BUILDIN_DEF2(delitem2,"cartdelitem2","viiiiiiii?"),
	BUILDIN_DEF2(enableitemuse,"enable_items",""),
	BUILDIN_DEF2(disableitemuse,"disable_items",""),
	BUILDIN_DEF(cutin,"si"),
	BUILDIN_DEF(viewpoint,"iiiii"),
	BUILDIN_DEF(heal,"ii"),
	BUILDIN_DEF(itemheal,"ii"),
	BUILDIN_DEF(percentheal,"ii"),
	BUILDIN_DEF(rand,"i?"),
	BUILDIN_DEF(countitem,"v?"),
	BUILDIN_DEF2(countitem,"storagecountitem","v?"),
	BUILDIN_DEF2(countitem,"cartcountitem","v?"),
	BUILDIN_DEF2(countitem,"countitem2","viiiiiii?"),
	BUILDIN_DEF2(countitem,"storagecountitem2","viiiiiii?"),
	BUILDIN_DEF2(countitem,"cartcountitem2","viiiiiii?"),
	BUILDIN_DEF(checkweight,"vi"),
	BUILDIN_DEF(checkweight2, "rr"),
	BUILDIN_DEF(readparam,"i?"),
	BUILDIN_DEF(getcharid,"i?"),
	BUILDIN_DEF(getnpcid,"i?"),
	BUILDIN_DEF(getpartyname,"i"),
	BUILDIN_DEF(getpartymember,"i?"),
	BUILDIN_DEF(getpartyleader,"i?"),
	// Guild related
	BUILDIN_DEF(guild_info,"vi"),
	BUILDIN_DEF(guild_create,"s?"),
	BUILDIN_DEF(guild_addmember,"i?"),
	BUILDIN_DEF(guild_delmember,"i???"),
	BUILDIN_DEF(guild_changegm,"ii"),
	BUILDIN_DEF(guild_break,"?"),
	BUILDIN_DEF(getguildname,"i"),
	BUILDIN_DEF(getguildmaster,"i"),
	BUILDIN_DEF(getguildmasterid,"i"),
	// Clan system
	BUILDIN_DEF(clan_join,"i?"),
	BUILDIN_DEF(clan_leave,"?"),
	BUILDIN_DEF(strcharinfo,"i"),
	BUILDIN_DEF(strnpcinfo,"i"),
	BUILDIN_DEF(getequipid,"i"),
	BUILDIN_DEF(getequipname,"i"),
	BUILDIN_DEF(getbrokenid,"i"), // [Valaris]
	BUILDIN_DEF(repair,"i"), // [Valaris]
	BUILDIN_DEF(repairall, "?"),
	BUILDIN_DEF(getequipisequiped,"i"),
	BUILDIN_DEF(getequipisenableref,"i"),
	BUILDIN_DEF(getequipisidentify,"i"),
	BUILDIN_DEF(getequiprefinerycnt,"i"),
	BUILDIN_DEF(getequipweaponlv,"i"),
	BUILDIN_DEF(getequippercentrefinery,"i"),
	BUILDIN_DEF(successrefitem,"i"),
	BUILDIN_DEF(failedrefitem,"i"),
	BUILDIN_DEF(downrefitem,"i?"),
	BUILDIN_DEF(delequip,"i?"),
	BUILDIN_DEF(breakequip, "i?"),
	BUILDIN_DEF(statusup,"i"),
	BUILDIN_DEF(statusup2,"ii"),
	BUILDIN_DEF(bonus,"iv"),
	BUILDIN_DEF2(bonus,"bonus2","ivi"),
	BUILDIN_DEF2(bonus,"bonus3","ivii"),
	BUILDIN_DEF2(bonus,"bonus4","ivvii"),
	BUILDIN_DEF2(bonus,"bonus5","ivviii"),
	BUILDIN_DEF(autobonus,"sii??"),
	BUILDIN_DEF(autobonus2,"sii??"),
	BUILDIN_DEF(autobonus3,"siiv?"),
	BUILDIN_DEF(skill,"vi?"),
	BUILDIN_DEF(addtoskill,"vi?"), // [Valaris]
	BUILDIN_DEF(guildskill,"vi"),
	BUILDIN_DEF(getskilllv,"v"),
	BUILDIN_DEF(getgdskilllv,"iv"),
	BUILDIN_DEF(basicskillcheck,""),
	BUILDIN_DEF(getgmlevel,""),
	BUILDIN_DEF(end,""),
	BUILDIN_DEF(checkoption,"i"),
	BUILDIN_DEF(setoption,"i?"),
	BUILDIN_DEF(setcart,"?"),
	BUILDIN_DEF(checkcart,""),
	BUILDIN_DEF(setfalcon,"?"),
	BUILDIN_DEF(checkfalcon,""),
	BUILDIN_DEF(setriding,"?"),
	BUILDIN_DEF(checkriding,""),
	BUILDIN_DEF(setdragon,"?"),
	BUILDIN_DEF(checkdragon,""),
	BUILDIN_DEF(setwug,"?"),
	BUILDIN_DEF(checkwug,""),
	BUILDIN_DEF(setwugrider,"?"),
	BUILDIN_DEF(checkwugrider,""),
	BUILDIN_DEF(setmadogear,"?"),
	BUILDIN_DEF(checkmadogear,""),
	BUILDIN_DEF2(savepoint,"save","sii"),
	BUILDIN_DEF(savepoint,"sii"),
	BUILDIN_DEF(gettimetick,"i"),
	BUILDIN_DEF(gettime,"i"),
	BUILDIN_DEF(gettimestr,"si"),
	BUILDIN_DEF(openstorage,""),
	BUILDIN_DEF(guildopenstorage,""),
	BUILDIN_DEF(itemskill,"vi"),
	BUILDIN_DEF(produce,"i"),
	BUILDIN_DEF(cooking,"i"),
	BUILDIN_DEF(makerune,"i"),
	BUILDIN_DEF(hascashmount,""),//[Ind]
	BUILDIN_DEF(setcashmount,""),//[Ind]
	BUILDIN_DEF(monster,"siisii?"),
	BUILDIN_DEF(getmobdrops,"i"),
	BUILDIN_DEF(areamonster,"siiiisii?"),
	BUILDIN_DEF(killmonster,"ss?"),
	BUILDIN_DEF(killmonsterall,"s?"),
	BUILDIN_DEF(clone,"siisi????"),
	BUILDIN_DEF(doevent,"s"),
	BUILDIN_DEF(donpcevent,"s"),
	BUILDIN_DEF(cmdothernpc,"ss"),
	BUILDIN_DEF(addtimer,"is"),
	BUILDIN_DEF(deltimer,"s"),
	BUILDIN_DEF(addtimercount,"si"),
	BUILDIN_DEF(initnpctimer,"??"),
	BUILDIN_DEF(stopnpctimer,"??"),
	BUILDIN_DEF(startnpctimer,"??"),
	BUILDIN_DEF(setnpctimer,"i?"),
	BUILDIN_DEF(getnpctimer,"i?"),
	BUILDIN_DEF(attachnpctimer,"?"), // attached the player id to the npc timer [Celest]
	BUILDIN_DEF(detachnpctimer,"?"), // detached the player id from the npc timer [Celest]
	BUILDIN_DEF(playerattached,""), // returns id of the current attached player. [Skotlex]
	BUILDIN_DEF(announce,"si?????"),
	BUILDIN_DEF(mapannounce,"ssi?????"),
	BUILDIN_DEF(areaannounce,"siiiisi?????"),
	BUILDIN_DEF(getusers,"i"),
	BUILDIN_DEF(getmapguildusers,"si"),
	BUILDIN_DEF(getmapusers,"s"),
	BUILDIN_DEF(getareausers,"siiii"),
	BUILDIN_DEF(getareadropitem,"siiiiv"),
	BUILDIN_DEF(enablenpc,"s"),
	BUILDIN_DEF(disablenpc,"s"),
	BUILDIN_DEF(hideoffnpc,"s"),
	BUILDIN_DEF(hideonnpc,"s"),
	BUILDIN_DEF(cloakonnpc, "s?"),
	BUILDIN_DEF(cloakoffnpc, "s?"),
	BUILDIN_DEF(sc_start,"iii?"),
	BUILDIN_DEF(sc_start2,"iiii?"),
	BUILDIN_DEF(sc_start4,"iiiiii?"),
	BUILDIN_DEF(sc_end,"i?"),
	BUILDIN_DEF(getscrate,"ii?"),
	BUILDIN_DEF(debugmes,"s"),
	BUILDIN_DEF2(catchpet,"pet","i"),
	BUILDIN_DEF2(birthpet,"bpet",""),
	BUILDIN_DEF(resetlvl,"i"),
	BUILDIN_DEF(resetstatus,""),
	BUILDIN_DEF(resetskill,""),
	BUILDIN_DEF(skillpointcount,""),
	BUILDIN_DEF(changebase,"i?"),
	BUILDIN_DEF(changesex,""),
	BUILDIN_DEF(changecharsex,"?"),
	BUILDIN_DEF(waitingroom,"si?????"),
	BUILDIN_DEF(delwaitingroom,"?"),
	BUILDIN_DEF2(waitingroomkickall,"kickwaitingroomall","?"),
	BUILDIN_DEF(enablewaitingroomevent,"?"),
	BUILDIN_DEF(disablewaitingroomevent,"?"),
	BUILDIN_DEF2(enablewaitingroomevent,"enablearena",""),		// Added by RoVeRT
	BUILDIN_DEF2(disablewaitingroomevent,"disablearena",""),	// Added by RoVeRT
	BUILDIN_DEF(getwaitingroomstate,"i?"),
	BUILDIN_DEF(warpwaitingpc,"sii?"),
	BUILDIN_DEF(attachrid,"i"),
	BUILDIN_DEF(detachrid,""),
	BUILDIN_DEF(isloggedin,"i?"),
	BUILDIN_DEF(setmapflagnosave,"ssii"),
	BUILDIN_DEF(getmapflag,"si"),
	BUILDIN_DEF(setmapflag,"si?"),
	BUILDIN_DEF(removemapflag,"si?"),
	BUILDIN_DEF(pvpon,"s"),
	BUILDIN_DEF(pvpoff,"s"),
	BUILDIN_DEF(gvgon,"s"),
	BUILDIN_DEF(gvgoff,"s"),
	BUILDIN_DEF(emotion,"i??"),
	BUILDIN_DEF(maprespawnguildid,"sii"),
	BUILDIN_DEF(agitstart,""),	// <Agit>
	BUILDIN_DEF(agitend,""),
	BUILDIN_DEF(agitcheck,""),   // <Agitcheck>
	BUILDIN_DEF(flagemblem,"i"),	// Flag Emblem
	BUILDIN_DEF(getcastlename,"s"),
	BUILDIN_DEF(getcastledata,"si?"),
	BUILDIN_DEF(setcastledata,"sii"),
	BUILDIN_DEF(requestguildinfo,"i?"),
	BUILDIN_DEF(getequipcardcnt,"i"),
	BUILDIN_DEF(successremovecards,"i"),
	BUILDIN_DEF(failedremovecards,"ii"),
	BUILDIN_DEF(marriage,"s"),
	BUILDIN_DEF2(wedding_effect,"wedding",""),
	BUILDIN_DEF(divorce,""),
	BUILDIN_DEF(ispartneron,""),
	BUILDIN_DEF(getpartnerid,""),
	BUILDIN_DEF(getchildid,""),
	BUILDIN_DEF(getmotherid,""),
	BUILDIN_DEF(getfatherid,""),
	BUILDIN_DEF(warppartner,"sii"),
	BUILDIN_DEF(getitemname,"v"),
	BUILDIN_DEF(getitemslots,"i"),
	BUILDIN_DEF(makepet,"i"),
	BUILDIN_DEF(getexp,"ii"),
	BUILDIN_DEF(getinventorylist,""),
	BUILDIN_DEF(getskilllist,""),
	BUILDIN_DEF(clearitem,""),
	BUILDIN_DEF(classchange,"i??"),
	BUILDIN_DEF(misceffect,"i"),
	BUILDIN_DEF(playBGM,"s"),
	BUILDIN_DEF(playBGMall,"s?????"),
	BUILDIN_DEF(soundeffect,"si"),
	BUILDIN_DEF(soundeffectall,"si?????"),	// SoundEffectAll [Codemaster]
	BUILDIN_DEF(strmobinfo,"ii"),	// display mob data [Valaris]
	BUILDIN_DEF(guardian,"siisi??"),	// summon guardians
	BUILDIN_DEF(guardianinfo,"sii"),	// display guardian data [Valaris]
	BUILDIN_DEF(petskillbonus,"iiii"), // [Valaris]
	BUILDIN_DEF(petrecovery,"ii"), // [Valaris]
	BUILDIN_DEF(petloot,"i"), // [Valaris]
	BUILDIN_DEF(petheal,"iiii"), // [Valaris]
	BUILDIN_DEF(petskillattack,"viii"), // [Skotlex]
	BUILDIN_DEF(petskillattack2,"viiii"), // [Valaris]
	BUILDIN_DEF(petskillsupport,"viiii"), // [Skotlex]
	BUILDIN_DEF(skilleffect,"vi"), // skill effect [Celest]
	BUILDIN_DEF(npcskilleffect,"viii"), // npc skill effect [Valaris]
	BUILDIN_DEF(consumeitem,"v?"),
	BUILDIN_DEF(specialeffect,"i??"), // npc skill effect [Valaris]
	BUILDIN_DEF(specialeffect2,"i??"), // skill effect on players[Valaris]
	BUILDIN_DEF(nude,""), // nude command [Valaris]
	BUILDIN_DEF(mapwarp,"ssii??"),		// Added by RoVeRT
	BUILDIN_DEF(atcommand,"s"), // [MouseJstr]
	BUILDIN_DEF(charcommand,"s"), // [MouseJstr]
	BUILDIN_DEF(movenpc,"sii?"), // [MouseJstr]
	BUILDIN_DEF(message,"ss"), // [MouseJstr]
	BUILDIN_DEF(npctalk, "s??"), // [Valaris][Murilo BiO]
	BUILDIN_DEF(mobcount,"ss"),
	BUILDIN_DEF(getlook,"i"),
	BUILDIN_DEF(getsavepoint,"i"),
	BUILDIN_DEF(npcspeed,"i"), // [Valaris]
	BUILDIN_DEF(npcwalkto,"ii"), // [Valaris]
	BUILDIN_DEF(npcstop,""), // [Valaris]
	BUILDIN_DEF(getmapxy,"rrri?"),	//by Lorky [Lupus]
	BUILDIN_DEF(checkoption1,"i"),
	BUILDIN_DEF(checkoption2,"i"),
	BUILDIN_DEF(guildgetexp,"i"),
	BUILDIN_DEF(guildchangegm,"is"),
	BUILDIN_DEF(logmes,"s"), //this command actls as MES but rints info into LOG file either SQL/TXT [Lupus]
	BUILDIN_DEF(summon,"si??"), // summons a slave monster [Celest]
	BUILDIN_DEF(isnight,""), // check whether it is night time [Celest]
	BUILDIN_DEF(isequipped,"i*"), // check whether another item/card has been equipped [Celest]
	BUILDIN_DEF(isequippedcnt,"i*"), // check how many items/cards are being equipped [Celest]
	BUILDIN_DEF(cardscnt,"i*"), // check how many items/cards are being equipped in the same arm [Lupus]
	BUILDIN_DEF(getrefine,""), // returns the refined number of the current item, or an item with index specified [celest]
	BUILDIN_DEF(night,""), // sets the server to night time
	BUILDIN_DEF(day,""), // sets the server to day time
#ifdef PCRE_SUPPORT
	BUILDIN_DEF(defpattern,"iss"), // Define pattern to listen for [MouseJstr]
	BUILDIN_DEF(activatepset,"i"), // Activate a pattern set [MouseJstr]
	BUILDIN_DEF(deactivatepset,"i"), // Deactive a pattern set [MouseJstr]
	BUILDIN_DEF(deletepset,"i"), // Delete a pattern set [MouseJstr]
#endif
	BUILDIN_DEF(dispbottom,"s"), //added from jA [Lupus]
	BUILDIN_DEF(dispbottom2,"ss?"), //added from hercules [15peaces]
	BUILDIN_DEF(getusersname,""),
	BUILDIN_DEF(recovery,""),
	BUILDIN_DEF(getpetinfo,"i"),
	BUILDIN_DEF(gethominfo,"i"),
	BUILDIN_DEF(getmercinfo,"i?"),
	BUILDIN_DEF(checkequipedcard,"i"),
	BUILDIN_DEF(jump_zero,"il"), //for future jA script compatibility
	BUILDIN_DEF(globalmes,"s?"),
	BUILDIN_DEF(getmapmobs,"s"), //end jA addition
	BUILDIN_DEF(unequip,"i"), // unequip command [Spectre]
	BUILDIN_DEF(getstrlen,"s"), //strlen [Valaris]
	BUILDIN_DEF(charisalpha,"si"), //isalpha [Valaris]
	BUILDIN_DEF(charat,"si"),
	BUILDIN_DEF(setchar,"ssi"),
	BUILDIN_DEF(insertchar,"ssi"),
	BUILDIN_DEF(delchar,"si"),
	BUILDIN_DEF(strtoupper,"s"),
	BUILDIN_DEF(strtolower,"s"),
	BUILDIN_DEF(charisupper, "si"),
	BUILDIN_DEF(charislower, "si"),
	BUILDIN_DEF(substr,"sii"),
	BUILDIN_DEF(explode, "rss"),
	BUILDIN_DEF(implode, "r?"),
	BUILDIN_DEF(sprintf,"s*"),  // Thanks to Mirei [15peaces]
	BUILDIN_DEF(sscanf, "ss*"),  // Thanks to Mirei [15peaces]
	BUILDIN_DEF(replacestr,"sss??"),
	BUILDIN_DEF(countstr,"ss?"),
	BUILDIN_DEF(setnpcdisplay,"sv??"),
	BUILDIN_DEF(compare,"ss"), // Lordalfa - To bring strstr to scripting Engine.
	BUILDIN_DEF(getiteminfo,"ii"), //[Lupus] returns Items Buy / sell Price, etc info
	BUILDIN_DEF(setiteminfo,"iii"), //[Lupus] set Items Buy / sell Price, etc info
	BUILDIN_DEF(getequipcardid,"ii"), //[Lupus] returns CARD ID or other info from CARD slot N of equipped item
	// [zBuffer] List of mathematics commands --->
	BUILDIN_DEF(sqrt,"i"),
	BUILDIN_DEF(pow,"ii"),
	BUILDIN_DEF(distance,"iiii"),
	BUILDIN_DEF(min, "i*"),
	BUILDIN_DEF(max, "i*"),
	// <--- [zBuffer] List of mathematics commands
	BUILDIN_DEF(md5,"s"),
	// [zBuffer] List of dynamic var commands --->
	BUILDIN_DEF(getd,"s"),
	BUILDIN_DEF(setd,"sv"),
	// <--- [zBuffer] List of dynamic var commands
	BUILDIN_DEF(petstat,"i"),
	BUILDIN_DEF(callshop,"s?"), // [Skotlex]
	BUILDIN_DEF(npcshopitem,"sii*"), // [Lance]
	BUILDIN_DEF(npcshopadditem,"sii*"),
	BUILDIN_DEF(npcshopdelitem,"si*"),
	BUILDIN_DEF(npcshopattach,"s?"),
	BUILDIN_DEF(npcshopupdate,"sii?"),
	BUILDIN_DEF(equip,"i"),
	BUILDIN_DEF(autoequip,"ii"),
	BUILDIN_DEF(setbattleflag,"si"),
	BUILDIN_DEF(getbattleflag,"s"),
	BUILDIN_DEF(setitemscript,"is?"), //Set NEW item bonus script. Lupus
	BUILDIN_DEF(disguise,"i"), //disguise player. Lupus
	BUILDIN_DEF(undisguise,""), //undisguise player. Lupus
	BUILDIN_DEF(getmonsterinfo,"ii"), //Lupus
	BUILDIN_DEF(axtoi,"s"),
	BUILDIN_DEF(query_sql,"s*"),
	BUILDIN_DEF(query_logsql,"s*"),
	BUILDIN_DEF(escape_sql,"v"),
	BUILDIN_DEF(atoi,"s"),
	// [zBuffer] List of player cont commands --->
	BUILDIN_DEF(rid2name,"i"),
	BUILDIN_DEF(pcfollow,"ii"),
	BUILDIN_DEF(pcstopfollow,"i"),
	BUILDIN_DEF(pcblockmove,"ii"),
	// <--- [zBuffer] List of player cont commands
	// [zBuffer] List of mob control commands --->
	BUILDIN_DEF(unitexists,"i"),
	BUILDIN_DEF(getunitname,"i"),
	BUILDIN_DEF(setunitname,"is"),
	BUILDIN_DEF(getunitdata,"i*"),
	BUILDIN_DEF(setunitdata,"iii"),
	BUILDIN_DEF(unitwalk,"ii?"),
	BUILDIN_DEF(unitkill,"i"),
	BUILDIN_DEF(unitwarp,"isii"),
	BUILDIN_DEF(unitattack,"iv?"),
	BUILDIN_DEF(unitstopattack,"i"),
	BUILDIN_DEF(unitstopwalk,"i"),
	BUILDIN_DEF(unittalk,"is?"),
	BUILDIN_DEF(unitemote,"ii"),
	BUILDIN_DEF(unitskilluseid,"ivi??"), // originally by Qamera [Celest]
	BUILDIN_DEF(unitskillusepos,"iviii?"), // [Celest]
// <--- [zBuffer] List of mob control commands
	BUILDIN_DEF(sleep,"i"),
	BUILDIN_DEF(sleep2,"i"),
	BUILDIN_DEF(awake,"s"),
	BUILDIN_DEF(getvariableofnpc,"rs"),
	BUILDIN_DEF(warpportal,"iisii"),
	BUILDIN_DEF2(homunculus_evolution,"homevolution",""),	//[orn]
	BUILDIN_DEF2(homunculus_mutation, "hommutation", "i"),//[Rytech]
	BUILDIN_DEF2(homunculus_shuffle,"homshuffle",""),	//[Zephyrus]
	BUILDIN_DEF(eaclass,"?"),	//[Skotlex]
	BUILDIN_DEF(roclass,"i?"),	//[Skotlex]
	BUILDIN_DEF(checkvending,"?"),
	BUILDIN_DEF(checkchatting,"?"),
	BUILDIN_DEF(openmail,""),
	BUILDIN_DEF(openauction,""),
	BUILDIN_DEF(checkcell,"siii"),
	BUILDIN_DEF(setcell,"siiiiii"),
	BUILDIN_DEF(setwall,"siiiiis"),
	BUILDIN_DEF(delwall,"s"),
	BUILDIN_DEF(searchitem,"rs"),
	BUILDIN_DEF(mercenary_create,"ii"),
	BUILDIN_DEF(mercenary_heal,"ii"),
	BUILDIN_DEF(mercenary_sc_start,"iii"),
	BUILDIN_DEF(mercenary_get_calls,"i"),
	BUILDIN_DEF(mercenary_get_faith,"i"),
	BUILDIN_DEF(mercenary_set_calls,"ii"),
	BUILDIN_DEF(mercenary_set_faith,"ii"),
	BUILDIN_DEF(readbook,"ii"),
	BUILDIN_DEF(setfont,"i"),
	BUILDIN_DEF(areamobuseskill,"siiiiviiiii"),
	BUILDIN_DEF(progressbar,"si"),
	BUILDIN_DEF(pushpc,"ii"),
	BUILDIN_DEF(buyingstore,"i"),
	BUILDIN_DEF(searchstores,"ii"),
	BUILDIN_DEF(showdigit,"i?"),
	// WoE SE
	BUILDIN_DEF(agitstart2,""),
	BUILDIN_DEF(agitend2,""),
	BUILDIN_DEF(agitcheck2,""),
	// BattleGround
	BUILDIN_DEF(waitingroom2bg,"siiss?"),
	BUILDIN_DEF(waitingroom2bg_single,"isiis"),
	BUILDIN_DEF(bg_team_setxy,"iii"),
	BUILDIN_DEF(bg_warp,"isii"),
	BUILDIN_DEF(bg_monster,"isiisi?"),
	BUILDIN_DEF(bg_monster_set_team,"ii"),
	BUILDIN_DEF(bg_leave,""),
	BUILDIN_DEF(bg_destroy,"i"),
	BUILDIN_DEF(areapercentheal,"siiiiii"),
	BUILDIN_DEF(bg_get_data,"ii"),
	BUILDIN_DEF(bg_getareausers,"isiiii"),
	BUILDIN_DEF(bg_updatescore,"sii"),
	// Instancing
	BUILDIN_DEF(instance_create,"si"),
	BUILDIN_DEF(instance_destroy,"?"),
	BUILDIN_DEF(instance_attachmap,"si?"),
	BUILDIN_DEF(instance_detachmap,"s?"),
	BUILDIN_DEF(instance_attach,"i"),
	BUILDIN_DEF(instance_id,"?"),
	BUILDIN_DEF(instance_set_timeout,"ii?"),
	BUILDIN_DEF(instance_init,"i"),
	BUILDIN_DEF(instance_announce,"isi?????"),
	BUILDIN_DEF(instance_npcname,"s?"),
	BUILDIN_DEF(has_instance,"s?"),
	BUILDIN_DEF(instance_warpall,"sii?"),
	BUILDIN_DEF(instance_check_party,"i???"),
	BUILDIN_DEF(instance_mapname,"s?"),
	BUILDIN_DEF(instance_set_respawn,"sii?"),
	// @commands (script based)
	BUILDIN_DEF(bindatcmd, "ss??"), 
	BUILDIN_DEF(unbindatcmd, "s"), 
	BUILDIN_DEF(useatcmd, "s"),
	//Quest Log System [Inkfish]
	BUILDIN_DEF(questinfo, "ii??"),
	BUILDIN_DEF(setquest, "i"),
	BUILDIN_DEF(erasequest, "i"),
	BUILDIN_DEF(completequest, "i"),
	BUILDIN_DEF(checkquest, "i?"),
	BUILDIN_DEF(isbegin_quest,"i"),
	BUILDIN_DEF(changequest, "ii"),
	BUILDIN_DEF(showevent, "ii"),
	// Bound items [Xantara] & [Akinari] 
	BUILDIN_DEF2(getitem,"getitembound","vii?"), 
	BUILDIN_DEF2(getitem2,"getitembound2","viiiiiiiii?"), 
	BUILDIN_DEF(countbound, "?"),
	// Others
	BUILDIN_DEF(npcskill,"viii"),
	BUILDIN_DEF(bonus_script,"si????"),
	BUILDIN_DEF(bonus_script_clear,"??"),
	BUILDIN_DEF(showscript,"s?"),
	BUILDIN_DEF(getepisode,""),
	BUILDIN_DEF(getpacketver,""),
	BUILDIN_DEF(freeloop,"?"),
	BUILDIN_DEF(getrandomoptinfo, "i"),
	BUILDIN_DEF(getequiprandomoption, "iii?"),
	BUILDIN_DEF(setrandomoption,"iiiii?"),
	BUILDIN_DEF(unloadnpc, "s"),
	BUILDIN_DEF(hateffect, "ii"),
	// Monster Transform [malufett/Hercules]
	BUILDIN_DEF(jobcanentermap,"s?"),
	BUILDIN_DEF2(montransform, "transform", "vi?????"),
	BUILDIN_DEF2(montransform, "active_transform", "vi?????"),
	// WoE TE
	BUILDIN_DEF(agitstart3,""),
	BUILDIN_DEF(agitend3,""),
	BUILDIN_DEF(agitcheck3,""),
	BUILDIN_DEF(gvgon3,"s"),
	BUILDIN_DEF(gvgoff3,"s"),
	BUILDIN_DEF(getguildalliance,"ii"),
	BUILDIN_DEF(opendressroom,"?"),
	BUILDIN_DEF(closedressroom,"?"),
	BUILDIN_DEF(setquestinfo_level, "iii"),
	BUILDIN_DEF(setquestinfo_req, "iii*"),
	BUILDIN_DEF(setquestinfo_job, "ii*"),
	BUILDIN_DEF(navigateto, "s???????"),
	// Achievements [Smokexyz/Hercules]
	BUILDIN_DEF(achievement_iscompleted, "i?"),
	BUILDIN_DEF(achievement_progress, "iiii?"),
	BUILDIN_DEF(adopt, "vv"),
	{NULL,NULL,NULL},
};
