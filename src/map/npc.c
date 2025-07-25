// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/cbasetypes.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "../common/ers.h"
#include "../common/db.h"
#include "../common/socket.h"
#include "map.h"
#include "log.h"
#include "clif.h"
#include "intif.h"
#include "pc.h"
#include "status.h"
#include "itemdb.h"
#include "script.h"
#include "mob.h"
#include "pet.h"
#include "instance.h"
#include "battle.h"
#include "skill.h"
#include "unit.h"
#include "npc.h"
#include "chat.h"
#include "achievement.h"
#include "guild.h" // guild flag cache

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>


struct npc_data* fake_nd;

// linked list of npc source files
struct npc_src_list {
	struct npc_src_list* next;
	char name[4]; // dynamic array, the structure is allocated with extra bytes (string length)
};
static struct npc_src_list* npc_src_files = NULL;

static int npc_id=START_NPC_NUM;
static int npc_warp=0;
static int npc_shop=0;
static int npc_script=0;
static int npc_mob=0;
static int npc_delay_mob=0;
static int npc_cache_mob=0;

// Market Shop
#if PACKETVER >= 20131223
struct s_npc_market {
	struct npc_item_list *list;
	char exname[NPC_NAME_LENGTH+1];
	uint16 count;
};
static DBMap *NPCMarketDB; /// Stock persistency! Temporary market stocks from `market` table. struct s_npc_market, key: NPC exname
static void npc_market_checkall(void);
static void npc_market_fromsql(void);
#define npc_market_delfromsql(exname,nameid) (npc_market_delfromsql_((exname), (nameid), false))
#define npc_market_clearfromsql(exname) (npc_market_delfromsql_((exname), 0, true))
#endif

/// Returns a new npc id that isn't being used in id_db.
/// Fatal error if nothing is available.
int npc_get_new_npc_id(void)
{
	if (npc_id >= START_NPC_NUM && !map_blid_exists(npc_id))
		return npc_id++;// available
	else {// find next id
		int base_id = npc_id;
		while( base_id != ++npc_id )
		{
			if( npc_id < START_NPC_NUM )
				npc_id = START_NPC_NUM;
			if (!map_blid_exists(npc_id))
				return npc_id++;// available
		}
		// full loop, nothing available
		ShowFatalError("npc_get_new_npc_id: All ids are taken. Exiting...");
		exit(1);
	}
}

static struct eri *timer_event_ers; //For the npc timer data. [Skotlex]

//For holding the view data of npc classes. [Skotlex]
static struct view_data npc_viewdb[MAX_NPC_CLASS];
static struct view_data npc_viewdb_2[MAX_NPC_CLASS_2];

static struct script_event_s
{	//Holds pointers to the commonly executed scripts for speedup. [Skotlex]
	struct event_data *event[UCHAR_MAX];
	const char *event_name[UCHAR_MAX];
	uint8 event_count;
} script_event[NPCE_MAX];

struct view_data* npc_get_viewdata(int class_)
{	//Returns the viewdata for normal npc classes.
	if (class_ == INVISIBLE_CLASS)
		return &npc_viewdb[0];
	if (npcdb_checkid(class_) || class_ == WARP_CLASS)
		if ( class_ >= NPC_CLASS_BASE_2 )
			return &npc_viewdb_2[class_-NPC_CLASS_BASE_2];
		else
			return &npc_viewdb[class_];
	return NULL;
}

static int npc_isnear_sub(struct block_list* bl, va_list args)
{
	struct npc_data *nd = (struct npc_data*)bl;

	if (nd->sc.option & (OPTION_HIDE | OPTION_INVISIBLE))
		return 0;

	return 1;
}

bool npc_isnear(struct block_list * bl)
{
	if (battle_config.min_npc_vendchat_distance > 0 &&
		map_foreachinrange(npc_isnear_sub, bl, battle_config.min_npc_vendchat_distance, BL_NPC))
		return true;

	return false;
}

int npc_ontouch_event(struct map_session_data *sd, struct npc_data *nd)
{
	char name[EVENT_NAME_LENGTH];

	if( nd->touching_id )
		return 0; // Attached a player already. Can't trigger on anyone else.

	if( pc_ishiding(sd) )
		return 1; // Can't trigger 'OnTouch_'. try 'OnTouch' later.

	snprintf(name, ARRAYLENGTH(name), "%s::%s", nd->exname, script_config.ontouch_name);
	return npc_event(sd,name,1);
}

int npc_ontouch2_event(struct map_session_data *sd, struct npc_data *nd)
{
	char name[EVENT_NAME_LENGTH];

	if( sd->areanpc_id == nd->bl.id )
		return 0;

	snprintf(name, ARRAYLENGTH(name), "%s::%s", nd->exname, script_config.ontouch2_name);
	return npc_event(sd,name,2);
}

/*==========================================
 * Sub-function of npc_enable, runs OnTouch event when enabled
 *------------------------------------------*/
int npc_enable_sub(struct block_list *bl, va_list ap)
{
	struct npc_data *nd;

	nullpo_ret(bl);
	nullpo_ret(nd=va_arg(ap,struct npc_data *));
	if(bl->type == BL_PC)
	{
		TBL_PC *sd = (TBL_PC*)bl;

		if (nd->sc.option&OPTION_INVISIBLE)
			return 1;

		if( npc_ontouch_event(sd,nd) > 0 && npc_ontouch2_event(sd,nd) > 0 )
		{ // failed to run OnTouch event, so just click the npc
			if (sd->npc_id != 0)
				return 0;

			pc_stop_walking(sd,1);
			npc_click(sd,nd);
		}
	}
	return 0;
}

bool npc_enable(struct npc_data* nd, int flag)
{
	nullpo_ret(nd);

	if (flag & 1)
	{
		nd->sc.option &= ~OPTION_INVISIBLE;
		clif_spawn(&nd->bl);
	}
	else if (flag & 2)
		nd->sc.option &= ~OPTION_HIDE;
	else if (flag & 4)
		nd->sc.option|= OPTION_HIDE;
	else	//Can't change the view_data to invisible class because the view_data for all npcs is shared! [Skotlex]
	{
		nd->sc.option |= OPTION_INVISIBLE;
		clif_clearunit_area(&nd->bl, CLR_OUTSIGHT);
	}

	if (nd->class_ == WARP_CLASS || nd->class_ == FLAG_CLASS)
	{	//Client won't display option changes for these classes [Toms]
		if (nd->sc.option&(OPTION_HIDE|OPTION_INVISIBLE))
			clif_clearunit_area(&nd->bl, CLR_OUTSIGHT);
		else
			clif_spawn(&nd->bl);
	} else
		clif_changeoption(&nd->bl);
		
	if( flag&3 && (nd->u.scr.xs >= 0 || nd->u.scr.ys >= 0) )
		map_foreachinarea( npc_enable_sub, nd->bl.m, nd->bl.x-nd->u.scr.xs, nd->bl.y-nd->u.scr.ys, nd->bl.x+nd->u.scr.xs, nd->bl.y+nd->u.scr.ys, BL_PC, nd );

	return true;
}

/*==========================================
 * NPCを名前で探す
 *------------------------------------------*/
struct npc_data* npc_name2id(const char* name)
{
	return (struct npc_data *) strdb_get(npcname_db, name);
}

/*==========================================
 * イベントキューのイベント処理
 *------------------------------------------*/
int npc_event_dequeue(struct map_session_data* sd)
{
	nullpo_ret(sd);

	if(sd->npc_id)
	{	//Current script is aborted.
		if(sd->state.using_fake_npc){
			clif_clearunit_single(sd->npc_id, CLR_OUTSIGHT, sd->fd);
			sd->state.using_fake_npc = 0;
		}
		if (sd->st) {
			script_free_state(sd->st);
			sd->st = NULL;
		}
		sd->npc_id = 0;
	}

	if (!sd->eventqueue[0][0])
		return 0; //Nothing to dequeue

	if (!pc_addeventtimer(sd,100,sd->eventqueue[0]))
	{	//Failed to dequeue, couldn't set a timer.
		ShowWarning("npc_event_dequeue: event timer is full !\n");
		return 0;
	}
	//Event dequeued successfully, shift other elements.
	memmove(sd->eventqueue[0], sd->eventqueue[1], (MAX_EVENTQUEUE-1)*sizeof(sd->eventqueue[0]));
	sd->eventqueue[MAX_EVENTQUEUE-1][0]=0;
	return 1;
}

/*==========================================
 * exports a npc event label
 * called from npc_parse_script
 *------------------------------------------*/
static int npc_event_export(struct npc_data *nd, int i)
{
	char* lname = nd->u.scr.label_list[i].name;
	int pos = nd->u.scr.label_list[i].pos;
	if ((lname[0] == 'O' || lname[0] == 'o') && (lname[1] == 'N' || lname[1] == 'n')) {
		struct event_data *ev;
		char buf[EVENT_NAME_LENGTH];

		// NPC:<name> the prefix uses 4 characters
		if (!strncasecmp(lname, script_config.onwhisper_event_name, NAME_LENGTH) && strlen(nd->exname) > (NAME_LENGTH - 4)) {
			// The client only allows that many character so that NPC could not be whispered by unmodified clients
			ShowWarning("Whisper event in npc '"CL_WHITE"%s"CL_RESET"' was ignored, because it's name is too long.\n", nd->exname);
			return 0;
		}

		snprintf(buf, ARRAYLENGTH(buf), "%s::%s", nd->exname, lname);
		// generate the data and insert it
		CREATE(ev, struct event_data, 1);
		ev->nd = nd;
		ev->pos = pos;
		if (strdb_put(ev_db, buf, ev)) // There was already another event of the same name?
			return 1;
	}
	return 0;
}

int npc_event_sub(struct map_session_data* sd, struct event_data* ev, const char* eventname); //[Lance]
/*==========================================
 * 全てのNPCのOn*イベント実行
 *------------------------------------------*/
int npc_event_doall_sub(DBKey key, DBData *data, va_list ap)
{
	const char* p = key.str;
	struct event_data* ev;
	int* c;
	const char* name;
	int rid;

	nullpo_ret(ev = db_data2ptr(data));
	nullpo_ret(c = va_arg(ap, int *));
	nullpo_ret(name = va_arg(ap, const char *));
	rid = va_arg(ap, int);

	p = strchr(p, ':'); // match only the event name
	if( p && strcmpi(name, p) == 0 /* && !ev->nd->src_id */ ) // Do not run on duplicates. [Paradox924X]
	{
		if(rid) // a player may only have 1 script running at the same time
			npc_event_sub(map_id2sd(rid),ev,key.str);
		else
			run_script(ev->nd->u.scr.script,ev->pos,rid,ev->nd->bl.id);
		(*c)++;
	}

	return 0;
}

static int npc_event_do_sub(DBKey key, DBData *data, va_list ap)
{
	const char* p = key.str;
	struct event_data* ev;
	int* c;
	const char* name;

	nullpo_ret(ev = db_data2ptr(data));
	nullpo_ret(c = va_arg(ap, int *));
	nullpo_ret(name = va_arg(ap, const char *));

	if( p && strcmpi(name, p) == 0 )
	{
		run_script(ev->nd->u.scr.script,ev->pos,0,ev->nd->bl.id);
		(*c)++;
	}

	return 0;
}

// runs the specified event (supports both single-npc and global events)
int npc_event_do(const char* name)
{
	int c = 0;

	if( name[0] == ':' && name[1] == ':' )
		ev_db->foreach(ev_db,npc_event_doall_sub,&c,name,0);
	else
		ev_db->foreach(ev_db,npc_event_do_sub,&c,name);

	return c;
}
// runs the specified event (global only)
int npc_event_doall(const char* name)
{
	return npc_event_doall_id(name, 0);
}
// runs the specified event, with a RID attached (global only)
int npc_event_doall_id(const char* name, int rid)
{
	int c = 0;
	char buf[64];
	safesnprintf(buf, sizeof(buf), "::%s", name);
	ev_db->foreach(ev_db,npc_event_doall_sub,&c,buf,rid);
	return c;
}

/*==========================================
 * 時計イベント実行
 *------------------------------------------*/
int npc_event_do_clock(int tid, int64 tick, int id, intptr_t data)
{
	static struct tm ev_tm_b; // tracks previous execution time
	time_t timer;
	struct tm* t;
	char buf[64];
	int c = 0;

	timer = time(NULL);
	t = localtime(&timer);

	if (t->tm_min != ev_tm_b.tm_min ) {
		char* day;

		switch (t->tm_wday) {
			case 0: day = "Sun"; break;
			case 1: day = "Mon"; break;
			case 2: day = "Tue"; break;
			case 3: day = "Wed"; break;
			case 4: day = "Thu"; break;
			case 5: day = "Fri"; break;
			case 6: day = "Sat"; break;
			default:day = ""; break;
		}

		sprintf(buf,"OnMinute%02d",t->tm_min);
		c+=npc_event_doall(buf);
		sprintf(buf,"OnClock%02d%02d",t->tm_hour,t->tm_min);
		c+=npc_event_doall(buf);
		sprintf(buf,"On%s%02d%02d",day,t->tm_hour,t->tm_min);
		c+=npc_event_doall(buf);
	}
	if (t->tm_hour!= ev_tm_b.tm_hour) {
		sprintf(buf,"OnHour%02d",t->tm_hour);
		c+=npc_event_doall(buf);
	}
	if (t->tm_mday!= ev_tm_b.tm_mday) {
		sprintf(buf,"OnDay%02d%02d",t->tm_mon+1,t->tm_mday);
		c+=npc_event_doall(buf);
	}

	memcpy(&ev_tm_b,t,sizeof(ev_tm_b));
	return c;
}

/*==========================================
 * OnInitイベント実行(&時計イベント開始)
 *------------------------------------------*/
void npc_event_do_oninit(void)
{
	ShowStatus("Event '"CL_WHITE"OnInit"CL_RESET"' executed with '"CL_WHITE"%d"CL_RESET"' NPCs."CL_CLL"\n", npc_event_doall("OnInit"));

	add_timer_interval(gettick()+100,npc_event_do_clock,0,0,1000);
}

/*==========================================
 * タイマーイベント用ラベルの取り込み
 * called from npc_parse_script
 *------------------------------------------*/
int npc_timerevent_export(struct npc_data *nd, int i)
{
	int t = 0, k = 0;
	char *lname = nd->u.scr.label_list[i].name;
	int pos = nd->u.scr.label_list[i].pos;
	if (sscanf(lname, "OnTimer%d%n", &t, &k) == 1 && lname[k] == '\0') {
		// ????????
		struct npc_timerevent_list *te = nd->u.scr.timer_event;
		int j, k = nd->u.scr.timeramount;
		if (te == NULL)
			te = (struct npc_timerevent_list *)aMalloc(sizeof(struct npc_timerevent_list));
		else
			te = (struct npc_timerevent_list *)aRealloc(te, sizeof(struct npc_timerevent_list) * (k + 1));
		for (j = 0; j < k; j++) {
			if (te[j].timer > t) {
				memmove(te + j + 1, te + j, sizeof(struct npc_timerevent_list)*(k - j));
				break;
			}
		}

		te[j].timer = t;
		te[j].pos = pos;
		nd->u.scr.timer_event = te;
		nd->u.scr.timeramount++;
	}
	return 0;
}
struct timer_event_data {
	int rid; //Attached player for this timer.
	int next; //timer index (starts with 0, then goes up to nd->u.scr.timeramount)
	int time; //holds total time elapsed for the script from when timer was started to when last time the event triggered.
};

/*==========================================
 * triger 'OnTimerXXXX' events
 *------------------------------------------*/
int npc_timerevent(int tid, int64 tick, int id, intptr_t data)
{
	int old_rid, old_timer;
	int64 old_tick;
	struct npc_data* nd=(struct npc_data *)map_id2bl(id);
	struct npc_timerevent_list *te;
	struct timer_event_data *ted = (struct timer_event_data*)data;
	struct map_session_data *sd=NULL;

	if( nd == NULL )
	{
		ShowError("npc_timerevent: NPC not found??\n");
		return 0;
	}

	if( ted->rid && !(sd = map_id2sd(ted->rid)) )
	{
		ShowError("npc_timerevent: Attached player not found.\n");
		ers_free(timer_event_ers, ted);
		return 0;
	}

	// These stuffs might need to be restored.
	old_rid = nd->u.scr.rid;	
	old_tick = nd->u.scr.timertick;
	old_timer = nd->u.scr.timer;

	// Set the values of the timer
	nd->u.scr.rid = sd?sd->bl.id:0;	//attached rid
	nd->u.scr.timertick = tick;		//current time tick
	nd->u.scr.timer = ted->time;	//total time from beginning to now

	// Locate the event
	te = nd->u.scr.timer_event + ted->next;

	// Arrange for the next event
	ted->next++;	
	if( nd->u.scr.timeramount > ted->next )
	{
		int next = nd->u.scr.timer_event[ ted->next ].timer - nd->u.scr.timer_event[ ted->next - 1 ].timer;
		ted->time += next;
		if( sd )
			sd->npc_timer_id = add_timer(tick+next,npc_timerevent,id,(intptr_t)ted);
		else
			nd->u.scr.timerid = add_timer(tick+next,npc_timerevent,id,(intptr_t)ted);
	}
	else
	{
		if( sd )
			sd->npc_timer_id = INVALID_TIMER;
		else
			nd->u.scr.timerid = INVALID_TIMER;

		ers_free(timer_event_ers, ted);
	}

	// Run the script
	run_script(nd->u.scr.script,te->pos,nd->u.scr.rid,nd->bl.id);	
	
	nd->u.scr.rid = old_rid; // Attached-rid should be restored anyway.
	if( sd )
	{ // Restore previous data, only if this timer is a player-attached one.
		nd->u.scr.timer = old_timer;
		nd->u.scr.timertick = old_tick;
	}

	return 0;
}
/*==========================================
 * Start/Resume NPC timer
 *------------------------------------------*/
int npc_timerevent_start(struct npc_data* nd, int rid)
{
	int j;
	int64 tick = gettick();
	struct map_session_data *sd = NULL; //Player to whom script is attached.
		
	nullpo_ret(nd);

	// Check if there is an OnTimer Event
	ARR_FIND( 0, nd->u.scr.timeramount, j, nd->u.scr.timer_event[j].timer > nd->u.scr.timer );

	if( nd->u.scr.rid > 0 && !(sd = map_id2sd(nd->u.scr.rid)) )
	{ // Failed to attach timer to this player.
		ShowError("npc_timerevent_start: Attached player not found!\n");
		return 1;
	}

	// Check if timer is already started.
	if( sd )
	{
		if( sd->npc_timer_id != INVALID_TIMER )
			return 0;
	}
	else if( nd->u.scr.timerid != INVALID_TIMER || nd->u.scr.timertick )
		return 0;

	if (j < nd->u.scr.timeramount)
	{
		int next;
		struct timer_event_data *ted;

		// Arrange for the next event
		ted = ers_alloc(timer_event_ers, struct timer_event_data);
		ted->next = j; // Set event index
		ted->time = nd->u.scr.timer_event[j].timer;
		next = nd->u.scr.timer_event[j].timer - nd->u.scr.timer;
		if (sd)
		{
			ted->rid = sd->bl.id; // Attach only the player if attachplayerrid was used.
			sd->npc_timer_id = add_timer(tick + next, npc_timerevent, nd->bl.id, (intptr_t)ted);
		}
		else
		{
			ted->rid = 0;
			nd->u.scr.timertick = tick; // Set when timer is started
			nd->u.scr.timerid = add_timer(tick + next, npc_timerevent, nd->bl.id, (intptr_t)ted);
		}
	}
	else if (!sd)
	{
		nd->u.scr.timertick = tick;
	}

	return 0;
}
/*==========================================
 * Stop NPC timer
 *------------------------------------------*/
int npc_timerevent_stop(struct npc_data* nd)
{
	struct map_session_data *sd = NULL;
	const struct TimerData *td = NULL;
	int *tid;

	nullpo_ret(nd);

	if( nd->u.scr.rid && !(sd = map_id2sd(nd->u.scr.rid)) )
	{
		ShowError("npc_timerevent_stop: Attached player not found!\n");
		return 1;
	}
	
	tid = sd?&sd->npc_timer_id:&nd->u.scr.timerid;
	if( *tid == INVALID_TIMER && (sd || !nd->u.scr.timertick) ) // Nothing to stop
		return 0;

	// Delete timer
	if (*tid != INVALID_TIMER)
	{
		td = get_timer(*tid);
		if (td && td->data)
			ers_free(timer_event_ers, (void*)td->data);
		delete_timer(*tid, npc_timerevent);
		*tid = INVALID_TIMER;
	}

	if( !sd && nd->u.scr.timertick )
	{
		nd->u.scr.timer += DIFF_TICK32(gettick(),nd->u.scr.timertick); // Set 'timer' to the time that has passed since the beginning of the timers
		nd->u.scr.timertick = 0; // Set 'tick' to zero so that we know it's off.
	}

	return 0;
}
/*==========================================
 * Aborts a running NPC timer that is attached to a player.
 *------------------------------------------*/
void npc_timerevent_quit(struct map_session_data* sd)
{
	const struct TimerData *td;
	struct npc_data* nd;
	struct timer_event_data *ted;

	// Check timer existance
	if( sd->npc_timer_id == INVALID_TIMER )
		return;
	if( !(td = get_timer(sd->npc_timer_id)) )
	{
		sd->npc_timer_id = INVALID_TIMER;
		return;
	}

	// Delete timer
	nd = (struct npc_data *)map_id2bl(td->id);
	ted = (struct timer_event_data*)td->data;
	delete_timer(sd->npc_timer_id, npc_timerevent);
	sd->npc_timer_id = INVALID_TIMER;

	// Execute OnTimerQuit
	if( nd && nd->bl.type == BL_NPC )
	{
		char buf[EVENT_NAME_LENGTH];
		struct event_data *ev;

		snprintf(buf, ARRAYLENGTH(buf), "%s::OnTimerQuit", nd->exname);
		ev = (struct event_data*)strdb_get(ev_db, buf);
		if( ev && ev->nd != nd )
		{
			ShowWarning("npc_timerevent_quit: Unable to execute \"OnTimerQuit\", two NPCs have the same event name [%s]!\n",buf);
			ev = NULL;
		}
		if( ev )
		{
			int old_rid,old_timer;
			int64 old_tick;

			//Set timer related info.
			old_rid = (nd->u.scr.rid == sd->bl.id ? 0 : nd->u.scr.rid); // Detach rid if the last attached player logged off.
			old_tick = nd->u.scr.timertick;
			old_timer = nd->u.scr.timer;

			nd->u.scr.rid = sd->bl.id;			
			nd->u.scr.timertick = gettick();			
			nd->u.scr.timer = ted->time;
		
			//Execute label
			run_script(nd->u.scr.script,ev->pos,sd->bl.id,nd->bl.id);

			//Restore previous data.
			nd->u.scr.rid = old_rid;
			nd->u.scr.timer = old_timer;
			nd->u.scr.timertick = old_tick;
		}
	}
	ers_free(timer_event_ers, ted);
}

/*==========================================
 * Get the tick value of an NPC timer
 * If it's stopped, return stopped time
 *------------------------------------------*/
int64 npc_gettimerevent_tick(struct npc_data* nd)
{
	int64 tick;
	nullpo_ret(nd);

	// TODO: Get player attached timer's tick. Now we can just get it by using 'getnpctimer' inside OnTimer event.

	tick = nd->u.scr.timer; // The last time it's active(start, stop or event trigger)
	if( nd->u.scr.timertick ) // It's a running timer
		tick += DIFF_TICK(gettick(), nd->u.scr.timertick);

	return tick;
}

/*==========================================
 * Set tick for running and stopped timer
 *------------------------------------------*/
int npc_settimerevent_tick(struct npc_data* nd, int newtimer)
{
	bool flag;
	int old_rid;
	//struct map_session_data *sd = NULL;

	nullpo_ret(nd);

	// TODO: Set player attached timer's tick.	

	old_rid = nd->u.scr.rid;
	nd->u.scr.rid = 0;

	// Check if timer is started
	flag = (nd->u.scr.timerid != INVALID_TIMER);

	if( flag ) npc_timerevent_stop(nd);
	nd->u.scr.timer = newtimer;
	if( flag ) npc_timerevent_start(nd, -1);

	nd->u.scr.rid = old_rid;
	return 0;
}

int npc_event_sub(struct map_session_data* sd, struct event_data* ev, const char* eventname)
{
	nullpo_retr(2, sd);
	nullpo_retr(2, eventname);

	if ( sd->npc_id != 0 )
	{
		//Enqueue the event trigger.
		int i;
		ARR_FIND( 0, MAX_EVENTQUEUE, i, sd->eventqueue[i][0] == '\0' );
		if( i < MAX_EVENTQUEUE )
		{
			safestrncpy(sd->eventqueue[i],eventname,50); //Event enqueued.
			return 0;
		}

		ShowWarning("npc_event: player's event queue is full, can't add event '%s' !\n", eventname);
		return 1;
	}
	if( ev->nd->sc.option&OPTION_INVISIBLE )
	{
		//Disabled npc, shouldn't trigger event.
		npc_event_dequeue(sd);
		return 2;
	}
	run_script(ev->nd->u.scr.script,ev->pos,sd->bl.id,ev->nd->bl.id);
	return 0;
}

/*==========================================
 * イベント型のNPC処理
 *------------------------------------------*/
int npc_event(struct map_session_data* sd, const char* eventname, int ontouch)
{
	struct event_data* ev = (struct event_data*)strdb_get(ev_db, eventname);
	struct npc_data *nd;

	nullpo_ret(sd);

	if( ev == NULL || (nd = ev->nd) == NULL )
	{
		if( !ontouch )
			ShowError("npc_event: event not found [%s]\n", eventname);
		return ontouch;
	}

	switch(ontouch)
	{
	case 1:
		nd->touching_id = sd->bl.id;
		sd->touching_id = nd->bl.id;
		break;
	case 2:
		sd->areanpc_id = nd->bl.id;
		break;
	}

	return npc_event_sub(sd,ev,eventname);
}

int npc_touch_areanpc_sub(struct block_list *bl, va_list ap)
{
	struct map_session_data *sd;
	int pc_id;
	char *name;

	nullpo_ret(bl);
	nullpo_ret((sd = map_id2sd(bl->id)));

	pc_id = va_arg(ap,int);
	name = va_arg(ap,char*);

	if( sd->state.warping )
		return 0;
	if( pc_ishiding(sd) )
		return 0;
	if( pc_id == sd->bl.id )
		return 0;

	npc_event(sd,name,1);

	return 1;
}

int npc_touchnext_areanpc(struct map_session_data* sd, bool leavemap)
{
	struct npc_data *nd = map_id2nd(sd->touching_id);
	short xs, ys;

	if( !nd || nd->touching_id != sd->bl.id )
		return 1;

	xs = nd->u.scr.xs;
	ys = nd->u.scr.ys;

	if( sd->bl.m != nd->bl.m || 
		sd->bl.x < nd->bl.x - xs || sd->bl.x > nd->bl.x + xs ||
		sd->bl.y < nd->bl.y - ys || sd->bl.y > nd->bl.y + ys ||
		pc_ishiding(sd) || leavemap )
	{
		char name[EVENT_NAME_LENGTH];

		nd->touching_id = sd->touching_id = 0;
		snprintf(name, ARRAYLENGTH(name), "%s::%s", nd->exname, script_config.ontouch_name);
		map_forcountinarea(npc_touch_areanpc_sub,nd->bl.m,nd->bl.x - xs,nd->bl.y - ys,nd->bl.x + xs,nd->bl.y + ys,1,BL_PC,sd->bl.id,name);
	}
	return 0;
}

/*==========================================
 * Exec OnTouch for player if in range of area event
 *------------------------------------------*/
int npc_touch_areanpc(struct map_session_data* sd, int m, int x, int y)
{
	int xs,ys;
	int f = 1;
	int i;
	int j, found_warp = 0;

	nullpo_retr(1, sd);

	// Why not enqueue it? [Inkfish]
	//if(sd->npc_id)
	//	return 1;

	if (sd->state.block_action & PCBLOCK_NPCCLICK)
		return 0;

	for(i=0;i<map[m].npc_num;i++)
	{
		if (map[m].npc[i]->sc.option&OPTION_INVISIBLE) {
			f=0; // a npc was found, but it is disabled; don't print warning
			continue;
		}

		switch(map[m].npc[i]->subtype) {
		case NPCTYPE_WARP:
			xs=map[m].npc[i]->u.warp.xs;
			ys=map[m].npc[i]->u.warp.ys;
			break;
		case NPCTYPE_SCRIPT:
			xs=map[m].npc[i]->u.scr.xs;
			ys=map[m].npc[i]->u.scr.ys;
			break;
		default:
			continue;
		}
		if( x >= map[m].npc[i]->bl.x-xs && x <= map[m].npc[i]->bl.x+xs
		&&  y >= map[m].npc[i]->bl.y-ys && y <= map[m].npc[i]->bl.y+ys )
			break;
	}
	if( i == map[m].npc_num )
	{
		if( f == 1 ) // no npc found
			ShowError("npc_touch_areanpc : stray NPC cell on coordinates '%s',%d,%d\n", map[m].name, x, y);
		return 1;
	}
	switch(map[m].npc[i]->subtype) {
		case NPCTYPE_WARP:
			if ((!map[m].npc[i]->trigger_on_hidden && (pc_ishiding(sd) || (sd->sc.count && sd->sc.data[SC_CAMOUFLAGE]))) || pc_isdead(sd))
				break; // hidden chars cannot use warps
			pc_setpos(sd,map[m].npc[i]->u.warp.mapindex,map[m].npc[i]->u.warp.x,map[m].npc[i]->u.warp.y,CLR_OUTSIGHT);
			break;
		case NPCTYPE_SCRIPT:
			for (j = i; j < map[m].npc_num; j++) {
				if (map[m].npc[j]->subtype != NPCTYPE_WARP) {
					continue;
				}

				if ((sd->bl.x >= (map[m].npc[j]->bl.x - map[m].npc[j]->u.warp.xs) && sd->bl.x <= (map[m].npc[j]->bl.x + map[m].npc[j]->u.warp.xs)) &&
					(sd->bl.y >= (map[m].npc[j]->bl.y - map[m].npc[j]->u.warp.ys) && sd->bl.y <= (map[m].npc[j]->bl.y + map[m].npc[j]->u.warp.ys))) {
					if ((!map[m].npc[i]->trigger_on_hidden && (pc_ishiding(sd) || (sd->sc.count && sd->sc.data[SC_CAMOUFLAGE]))) || pc_isdead(sd))
						break; // hidden or dead chars cannot use warps
					pc_setpos(sd,map[m].npc[j]->u.warp.mapindex,map[m].npc[j]->u.warp.x,map[m].npc[j]->u.warp.y,CLR_OUTSIGHT);
					found_warp = 1;
					break;
				}
			}

			if (found_warp > 0) {
				break;
			}
			if( npc_ontouch_event(sd,map[m].npc[i]) > 0 && npc_ontouch2_event(sd,map[m].npc[i]) > 0 )
			{ // failed to run OnTouch event, so just click the npc
				struct unit_data *ud = unit_bl2ud(&sd->bl);
				if( ud && ud->walkpath.path_pos < ud->walkpath.path_len )
				{ // Since walktimer always == INVALID_TIMER at this time, we stop walking manually. [Inkfish]
					clif_fixpos(&sd->bl);
					ud->walkpath.path_pos = ud->walkpath.path_len;
				}
				sd->areanpc_id = map[m].npc[i]->bl.id;
				npc_click(sd,map[m].npc[i]);
			}
			break;
	}
	return 0;
}

// OnTouch NPC or Warp for Mobs
// Return 1 if Warped
int npc_touch_areanpc2(struct mob_data *md)
{
	int i, m = md->bl.m, x = md->bl.x, y = md->bl.y, id;
	char eventname[EVENT_NAME_LENGTH];
	struct event_data* ev;
	int xs, ys;

	for( i = 0; i < map[m].npc_num; i++ )
	{
		if( map[m].npc[i]->sc.option&OPTION_INVISIBLE )
			continue;

		switch( map[m].npc[i]->subtype )
		{
			case NPCTYPE_WARP:
				if( !( battle_config.mob_warp&1 ) )
					continue;
				xs = map[m].npc[i]->u.warp.xs;
				ys = map[m].npc[i]->u.warp.ys;
				break;
			case NPCTYPE_SCRIPT:
				xs = map[m].npc[i]->u.scr.xs;
				ys = map[m].npc[i]->u.scr.ys;
				break;
			default:
				continue; // Keep Searching
		}

		if( x >= map[m].npc[i]->bl.x-xs && x <= map[m].npc[i]->bl.x+xs && y >= map[m].npc[i]->bl.y-ys && y <= map[m].npc[i]->bl.y+ys )
		{ // In the npc touch area
			switch( map[m].npc[i]->subtype )
			{
				case NPCTYPE_WARP:
					xs = map_mapindex2mapid(map[m].npc[i]->u.warp.mapindex);
					if( m < 0 )
						break; // Cannot Warp between map servers
					if( unit_warp(&md->bl, xs, map[m].npc[i]->u.warp.x, map[m].npc[i]->u.warp.y, CLR_OUTSIGHT) == 0 )
						return 1; // Warped
					break;
				case NPCTYPE_SCRIPT:
					if( map[m].npc[i]->bl.id == md->areanpc_id )
						break; // Already touch this NPC
					snprintf(eventname, ARRAYLENGTH(eventname), "%s::OnTouchNPC", map[m].npc[i]->exname);
					if( (ev = (struct event_data*)strdb_get(ev_db, eventname)) == NULL || ev->nd == NULL )
						break; // No OnTouchNPC Event
					md->areanpc_id = map[m].npc[i]->bl.id;
					id = md->bl.id; // Stores Unique ID
					run_script(ev->nd->u.scr.script, ev->pos, md->bl.id, ev->nd->bl.id);
					if( map_id2md(id) == NULL ) return 1; // Not Warped, but killed
					break;
			}

			return 0;
		}
	}

	return 0;
}

//Checks if there are any NPC on-touch objects on the given range.
//Flag determines the type of object to check for:
//&1: NPC Warps
//&2: NPCs with on-touch events.
int npc_check_areanpc(int flag, int m, int x, int y, int range)
{
	int i;
	int x0,y0,x1,y1;
	int xs,ys;

	if (range < 0) return 0;
	x0 = max(x-range, 0);
	y0 = max(y-range, 0);
	x1 = min(x+range, map[m].xs-1);
	y1 = min(y+range, map[m].ys-1);
	
	//First check for npc_cells on the range given
	i = 0;
	for (ys = y0; ys <= y1 && !i; ys++) {
		for(xs = x0; xs <= x1 && !i; xs++){
			if (map_getcell(m,xs,ys,CELL_CHKNPC))
				i = 1;
		}
	}
	if (!i) return 0; //No NPC_CELLs.

	//Now check for the actual NPC on said range.
	for(i=0;i<map[m].npc_num;i++)
	{
		if (map[m].npc[i]->sc.option&OPTION_INVISIBLE)
			continue;

		switch(map[m].npc[i]->subtype)
		{
		case NPCTYPE_WARP:
			if (!(flag&1))
				continue;
			xs=map[m].npc[i]->u.warp.xs;
			ys=map[m].npc[i]->u.warp.ys;
			break;
		case NPCTYPE_SCRIPT:
			if (!(flag&2))
				continue;
			xs=map[m].npc[i]->u.scr.xs;
			ys=map[m].npc[i]->u.scr.ys;
			break;
		default:
			continue;
		}

		if( x1 >= map[m].npc[i]->bl.x-xs && x0 <= map[m].npc[i]->bl.x+xs
		&&  y1 >= map[m].npc[i]->bl.y-ys && y0 <= map[m].npc[i]->bl.y+ys )
			break; // found a npc
	}
	if (i==map[m].npc_num)
		return 0;

	return (map[m].npc[i]->bl.id);
}

struct npc_data* npc_checknear(struct map_session_data* sd, struct block_list* bl)
{
	struct npc_data *nd;

	nullpo_retr(NULL, sd);
	if(bl == NULL) return NULL;
	if(bl->type != BL_NPC) return NULL;
	nd = (TBL_NPC*)bl;

	if(sd->state.using_fake_npc && sd->npc_id == bl->id)
		return nd;

	if (nd->class_<0) //Class-less npc, enable click from anywhere.
		return nd;

	if (bl->m!=sd->bl.m ||
	   bl->x<sd->bl.x-AREA_SIZE-1 || bl->x>sd->bl.x+AREA_SIZE+1 ||
	   bl->y<sd->bl.y-AREA_SIZE-1 || bl->y>sd->bl.y+AREA_SIZE+1)
		return NULL;

	return nd;
}

/*==========================================
 * NPCのオープンチャット発言
 *------------------------------------------*/
int npc_globalmessage(const char* name, const char* mes)
{
	struct npc_data* nd = npc_name2id(name);
	char temp[100];

	if (!nd)
		return 0;

	snprintf(temp, sizeof(temp), "%s : %s", name, mes);
	clif_GlobalMessage(&nd->bl,temp);

	return 0;
}

// MvP tomb [GreenBox]
void run_tomb(struct map_session_data* sd, struct npc_data* nd)
{
	char buffer[200];
    char time[10];
	
    strftime(time, sizeof(time), "%H:%M", localtime(&nd->u.tomb.kill_time));

	// TODO: Find exact color?
	snprintf(buffer, sizeof(buffer), msg_txt(sd,727), nd->u.tomb.md->db->name);
    clif_scriptmes(sd, nd->bl.id, buffer);

    clif_scriptmes(sd, nd->bl.id, msg_txt(sd,728));

    snprintf(buffer, sizeof(buffer), msg_txt(sd,729), time);
    clif_scriptmes(sd, nd->bl.id, buffer);
	
    clif_scriptmes(sd, nd->bl.id, msg_txt(sd,730));

 	snprintf(buffer, sizeof(buffer), msg_txt(sd,731), nd->u.tomb.killer_name[0] ? nd->u.tomb.killer_name : "Unknown"); 
	clif_scriptmes(sd, nd->bl.id, buffer);

    clif_scriptclose(sd, nd->bl.id);
}

/*==========================================
 * NPC 1st call when clicking on npc
 * Do specific action for NPC type (openshop, run scripts...)
 *------------------------------------------*/
int npc_click(struct map_session_data* sd, struct npc_data* nd)
{
	nullpo_retr(1, sd);

	if (sd->npc_id != 0) {
		ShowError("npc_click: npc_id != 0\n");
		return 1;
	}

	if(!nd) return 1;
	if ((nd = npc_checknear(sd,&nd->bl)) == NULL)
		return 1;
	//Hidden/Disabled npc.
	if (nd->class_ < 0 || nd->sc.option&(OPTION_INVISIBLE|OPTION_HIDE))
		return 1;

	if (sd->state.block_action & PCBLOCK_NPCCLICK) {
		clif_msg(sd, WORK_IN_PROGRESS);
		return 1;
	}
	
	switch(nd->subtype) {
		case NPCTYPE_SHOP:
			clif_npcbuysell(sd,nd->bl.id);
			break;
		case NPCTYPE_CASHSHOP:
			clif_cashshop_show(sd,nd);
			break;
		case NPCTYPE_MARKETSHOP:
#if PACKETVER >= 20131223
			 {
				unsigned short i;

				for (i = 0; i < nd->u.shop.count; i++) {
					if (nd->u.shop.shop_item[i].qty)
						break;
				}

				if (i == nd->u.shop.count) {
					clif_disp_overheadcolor_self(sd->fd, COLOR_RED, msg_txt(sd,534));
					return false;
				}

				sd->npc_shopid = nd->bl.id;
				clif_npc_market_open(sd, nd);
			}
#endif
			break;
		case NPCTYPE_ITEMSHOP:
		{
			char output[CHAT_SIZE_MAX];
			struct item_data *id = itemdb_exists(nd->u.shop.itemshop_nameid);
			memset(output, '\0', sizeof(output));
			sprintf(output, msg_txt(sd, 765), id->jname, id->nameid); // Item Shop List: %s (%d)
			clif_broadcast(&sd->bl, output, strlen(output) + 1, 0x10, SELF);
			clif_npcbuysell(sd, nd->bl.id);
		} break;
		case NPCTYPE_POINTSHOP:
		{
			char output[CHAT_SIZE_MAX];
			memset(output, '\0', sizeof(output));
			sprintf(output, msg_txt(sd, 766), nd->u.shop.pointshop_str); // Point Shop List: '%s'
			clif_broadcast(&sd->bl, output, strlen(output) + 1, 0x10, SELF);
			clif_npcbuysell(sd, nd->bl.id);
		} break;
		case NPCTYPE_SCRIPT:
			run_script(nd->u.scr.script,0,sd->bl.id,nd->bl.id);
			break;
		case NPCTYPE_TOMB:
			run_tomb(sd,nd);
			break;
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------*/
int npc_scriptcont(struct map_session_data* sd, int id, bool closing)
{
	nullpo_retr(1, sd);

	if( id != sd->npc_id ){
		TBL_NPC* nd_sd=(TBL_NPC*)map_id2bl(sd->npc_id);
		TBL_NPC* nd=(TBL_NPC*)map_id2bl(id);
		ShowDebug("npc_scriptcont: %s (sd->npc_id=%d) is not %s (id=%d).\n",
			nd_sd?(char*)nd_sd->name:"'Unknown NPC'", (int)sd->npc_id,
		  	nd?(char*)nd->name:"'Unknown NPC'", (int)id);
		return 1;
	}
	
	if(id != fake_nd->bl.id) { // Not item script
		if ((npc_checknear(sd,map_id2bl(id))) == NULL){
			ShowWarning("npc_scriptcont: failed npc_checknear test.\n");
			return 1;
		}
	}

	if (closing && sd->st && sd->st->state == CLOSE)
		sd->st->state = END;

	run_script_main(sd->st);

	return 0;
}

/*==========================================
 * Chk if valid call then open buy or selling list
 *------------------------------------------*/
int npc_buysellsel(struct map_session_data* sd, int id, int type)
{
	struct npc_data *nd;

	nullpo_retr(1, sd);

	if ((nd = npc_checknear(sd,map_id2bl(id))) == NULL)
		return 1;

	if (nd->subtype != NPCTYPE_SHOP && nd->subtype != NPCTYPE_ITEMSHOP && nd->subtype != NPCTYPE_POINTSHOP) {
		ShowError("no such shop npc : %d\n",id);
		if (sd->npc_id == id)
			sd->npc_id=0;
		return 1;
	}
	if (nd->sc.option & OPTION_INVISIBLE) // can't buy if npc is not visible (hack?)
		return 1;

	sd->npc_shopid = id;

	if (type == 0) {
		clif_buylist(sd,nd);
	} else {
		clif_selllist(sd);
	}
	return 0;
}

/**
 * npc_buylist for script-controlled shops.
 * @param sd Player who bought
 * @param n Number of item
 * @param item_list List of item
 * @param nd Attached NPC
 **/
static int npc_buylist_sub(struct map_session_data* sd, uint16 n, struct s_npc_buy_list *item_list, struct npc_data* nd) {
	char npc_ev[EVENT_NAME_LENGTH];
	int i;
	int key_nameid = 0;
	int key_amount = 0;

	// discard old contents
	script_cleararray_pc(sd, "@bought_nameid", (void*)0);
	script_cleararray_pc(sd, "@bought_quantity", (void*)0);

	// save list of bought items
	for (i = 0; i < n; i++) {
		script_setarray_pc(sd, "@bought_nameid", i, (void*)(intptr_t)item_list[i].nameid, &key_nameid);
		script_setarray_pc(sd, "@bought_quantity", i, (void*)(intptr_t)item_list[i].qty, &key_amount);
	}

	// invoke event
	snprintf(npc_ev, ARRAYLENGTH(npc_ev), "%s::OnBuyItem", nd->exname);
	npc_event(sd, npc_ev, 0);

	return 0;
}

/*==========================================
 * Cash Shop Buy
 *------------------------------------------*/
int npc_cashshop_buy(struct map_session_data *sd, t_itemid nameid, int amount, int points)
{
	struct npc_data *nd = (struct npc_data *)map_id2bl(sd->npc_shopid);
	struct item_data *item;
	int i, price, w;

	if( amount <= 0 )
		return 5;

	if( points < 0 )
		return 6;

	if( !nd || nd->subtype != NPCTYPE_CASHSHOP )
		return 1;

	if( sd->state.trading )
		return 4;

	if( (item = itemdb_exists(nameid)) == NULL )
		return 5; // Invalid Item

	ARR_FIND(0, nd->u.shop.count, i, nd->u.shop.shop_item[i].nameid == nameid || itemdb_viewid(nd->u.shop.shop_item[i].nameid) == nameid);
	if( i == nd->u.shop.count )
		return 5;
	if( nd->u.shop.shop_item[i].value <= 0 )
		return 5;

	nameid = nd->u.shop.shop_item[i].nameid; //item_avail replacement

	if(!itemdb_isstackable(nameid) && amount > 1)
	{
		ShowWarning("Player %s (%d:%d) sent a hexed packet trying to buy %d of nonstackable item %u!\n",
			sd->status.name, sd->status.account_id, sd->status.char_id, amount, nameid);
		amount = 1;
	}

	switch( pc_checkadditem(sd, nameid, amount) )
	{
		case 1:
			if( pc_inventoryblank(sd) == 0 )
				return 3;
			break;
		case 2:
			return 3;
	}

	w = item->weight * amount;
	if( w + sd->weight > sd->max_weight )
		return 3;

	if( (double)nd->u.shop.shop_item[i].value * amount > INT_MAX )
	{
		ShowWarning("npc_cashshop_buy: Item '%s' (%u) price overflow attempt!\n", item->name, nameid);
		ShowDebug("(NPC:'%s' (%s,%d,%d), player:'%s' (%d/%d), value:%d, amount:%d)\n",
					nd->exname, map[nd->bl.m].name, nd->bl.x, nd->bl.y, sd->status.name, sd->status.account_id, sd->status.char_id, nd->u.shop.shop_item[i].value, amount);
		return 5;
	}

	price = nd->u.shop.shop_item[i].value * amount;
	if( points > price )
		points = price;

	if( (sd->kafraPoints < points) || (sd->cashPoints < price - points) )
		return 6;

	pc_paycash(sd, price, points, LOG_TYPE_NPC);

	if( !pet_create_egg(sd, nameid) )
	{
		struct item item_tmp;
		memset(&item_tmp, 0, sizeof(struct item));
		item_tmp.nameid = nameid;
		item_tmp.identify = 1;

		pc_additem(sd,&item_tmp, amount, LOG_TYPE_NPC);
	}

	return 0;
}

/// Player item purchase from npc cash shop.
///
/// @param item_list 'n' pairs <amount,itemid>
/// @return result code for clif_parse_CashShopListSend
int npc_cashshop_buylist(struct map_session_data* sd, int n, struct s_npc_buy_list *item_list, int points)
{
	struct npc_data* nd;
	int i,j,p,w,new_;

	nullpo_retr(3, sd);
	nullpo_retr(3, item_list);

	nd = npc_checknear(sd,map_id2bl(sd->npc_shopid));

	if( points < 0 )
		return 6;

	if( !nd || nd->subtype != NPCTYPE_CASHSHOP )
		return 1;

	if( sd->state.trading )
		return 4;

	p = 0;
	w = 0;
	new_ = 0;
	// process entries in buy list, one by one
	for( i = 0; i < n; ++i )
	{
		int nameid, amount, value;

		// find this entry in the shop's sell list
		ARR_FIND( 0, nd->u.shop.count, j, 
			item_list[i].nameid == nd->u.shop.shop_item[j].nameid || //Normal items
			item_list[i].nameid == itemdb_viewid(nd->u.shop.shop_item[j].nameid) //item_avail replacement
		);

		if( j == nd->u.shop.count )
			return 5; // no such item in shop

		amount = item_list[i].qty;
		nameid = item_list[i].nameid = nd->u.shop.shop_item[j].nameid; //item_avail replacement
		value = nd->u.shop.shop_item[j].value;

		if( !itemdb_exists(nameid) )
			return 5; // item no longer in itemdb

		if( !itemdb_isstackable(nameid) && amount > 1 )
		{	//Exploit? You can't buy more than 1 of equipment types o.O
			ShowWarning("Player %s (%d:%d) sent a hexed packet trying to buy %d of nonstackable Item %u!\n",
				sd->status.name, sd->status.account_id, sd->status.char_id, amount, nameid);
			amount = item_list[i].qty = 1;
		}

		if( nd->master_nd )
		{// Script-controlled shops decide by themselves, what can be bought and for what price.
			continue;
		}

		switch( pc_checkadditem(sd,nameid,amount) )
		{
			case CHKADDITEM_EXIST:
				break;

			case CHKADDITEM_NEW:
				new_++;
				break;

			case CHKADDITEM_OVERAMOUNT:
				return 9;
		}

		value = pc_modify_cashshop_buy_value(sd,value);

		p += value * amount;
		w += itemdb_weight(nameid) * amount;
	}

	if( nd->master_nd != NULL ) //Script-based shops.
		return npc_buylist_sub(sd,n,item_list,nd->master_nd);

	if( points > p )
		points = p;

	if( (sd->kafraPoints < points) || (sd->cashPoints < p - points) )
		return 6;// Not enough points
	if( w + sd->weight > sd->max_weight )
		return 3;	// Too heavy
	if( pc_inventoryblank(sd) < new_ )
		return 9;	// Not enough space to store items

	pc_paycash(sd, p, points, LOG_TYPE_NPC);

	for( i = 0; i < n; ++i )
	{
		int nameid = item_list[i].nameid;
		int amount = item_list[i].qty;
		struct item item_tmp;

		memset(&item_tmp,0,sizeof(item_tmp));
		item_tmp.nameid = nameid;
		item_tmp.identify = 1;

		pc_additem(sd,&item_tmp,amount, LOG_TYPE_NPC);
	}

	return 0;
}


/**
 * Shop buylist
 * @param sd Player who attempt to buy
 * @param n Number of item will be bought
 * @param *item_list List of item will be bought
 * @return result code for clif_parse_NpcBuyListSend/clif_npc_market_purchase_ack
 **/
uint8 npc_buylist(struct map_session_data* sd, uint16 n, struct s_npc_buy_list* item_list) {
	struct npc_data* nd;
	struct npc_item_list *shop = NULL;
	double z;
	int i,j,k,w,skill,new_,count = 0;
	uint8 market_index[MAX_INVENTORY];
	char output[CHAT_SIZE_MAX];

	nullpo_retr(3, sd);
	nullpo_retr(3, item_list);

	nd = npc_checknear(sd,map_id2bl(sd->npc_shopid));
	if( nd == NULL )
		return 3;
	if( nd->subtype != NPCTYPE_SHOP && nd->subtype != NPCTYPE_MARKETSHOP && nd->subtype != NPCTYPE_ITEMSHOP && nd->subtype != NPCTYPE_POINTSHOP)
		return 3; 
	if (!item_list || !n)
		return 3;

	z = 0;
	w = 0;
	new_ = 0;

	shop = nd->u.shop.shop_item;

	memset(market_index, 0, sizeof(market_index));
	// process entries in buy list, one by one
	for( i = 0; i < n; ++i ) {
		unsigned short amount;
		t_itemid nameid;
		int value;

		// find this entry in the shop's sell list
		ARR_FIND( 0, nd->u.shop.count, j,
			item_list[i].nameid == shop[j].nameid || //Normal items
			item_list[i].nameid == itemdb_viewid(shop[j].nameid) //item_avail replacement
		);

		if( j == nd->u.shop.count )
			return 3; // no such item in shop

#if PACKETVER >= 20131223
		if (nd->subtype == NPCTYPE_MARKETSHOP) {
			if (item_list[i].qty > shop[j].qty)
				return 3;
			market_index[i] = j;
		}
#endif

		amount = item_list[i].qty;
		nameid = item_list[i].nameid = shop[j].nameid; //item_avail replacement
		value = shop[j].value;

		if( !itemdb_exists(nameid) )
			return 3; // item no longer in itemdb

		if( !itemdb_isstackable(nameid) && amount > 1 ) { //Exploit? You can't buy more than 1 of equipment types o.O
			ShowWarning("Player %s (%d:%d) sent a hexed packet trying to buy %d of nonstackable item %u!\n",
				sd->status.name, sd->status.account_id, sd->status.char_id, amount, nameid);
			amount = item_list[i].qty = 1;
		}

		if( nd->master_nd ) { // Script-controlled shops decide by themselves, what can be bought and for what price.
			continue;
		}

		switch( pc_checkadditem(sd,nameid,amount) ) {
			case 0:
				break;

			case 1:
				new_++;
				break;

			case 2:
				return 2;
		}

		if (npc_shop_discount(nd->subtype, nd->u.shop.discount))
			value = pc_modifybuyvalue(sd, value);

		z += (double)value * amount;
		w += itemdb_weight(nameid) * amount;
	}

	if (nd->master_nd) //Script-based shops.
		return npc_buylist_sub(sd,n,item_list,nd->master_nd);

 	switch(nd->subtype) {
 		case NPCTYPE_SHOP:
		case NPCTYPE_MARKETSHOP:
 			if (z > (double)sd->status.zeny)
 				return 1;	// Not enough Zeny
 			break;
		case NPCTYPE_ITEMSHOP:
			for (k = 0; k < MAX_INVENTORY; k++) {
				if (sd->inventory.u.items_inventory[k].nameid == nd->u.shop.itemshop_nameid)
					count += sd->inventory.u.items_inventory[k].amount;
			}
			if (z > (double)count) {
				struct item_data *id = itemdb_exists(nd->u.shop.itemshop_nameid);

				sprintf(output, msg_txt(sd, 763), id->jname, id->nameid); // You do not have enough %s %d.
				clif_displaymessagecolor(sd, output, COLOR_RED);
				return 1;
			}
			break;
		case NPCTYPE_POINTSHOP:
			switch (nd->u.shop.pointshop_str[0]) {
			case '#':
				if (nd->u.shop.pointshop_str[1] == '#')
					count = pc_readaccountreg2(sd, nd->u.shop.pointshop_str);
				else
					count = pc_readaccountreg(sd, nd->u.shop.pointshop_str);
				break;
			case '@':
			default:
				count = pc_readglobalreg(sd, nd->u.shop.pointshop_str);
				break;
			}
			if (z > (double)count) {
				sprintf(output, msg_txt(sd, 764), nd->u.shop.pointshop_str); // You do not have enough '%s'.
				clif_displaymessagecolor(sd, output, COLOR_RED);
				return 1;
			}
			break;
	}

	if( w + sd->weight > sd->max_weight )
		return 2;	// Too heavy
	if( pc_inventoryblank(sd) < new_ )
		return 3;	// Not enough space to store items

 	switch(nd->subtype) {
 		case NPCTYPE_SHOP:
		case NPCTYPE_MARKETSHOP:
			pc_payzeny(sd, (int)z, LOG_TYPE_NPC, NULL);
 			break;
		case NPCTYPE_ITEMSHOP:
			pc_delitem(sd, pc_search_inventory(sd, nd->u.shop.itemshop_nameid), (int)z, 0, 0, LOG_TYPE_NPC);
			break;
		case NPCTYPE_POINTSHOP:
			switch (nd->u.shop.pointshop_str[0]) {
			case '#':
				if (nd->u.shop.pointshop_str[1] == '#')
					pc_setaccountreg2(sd, nd->u.shop.pointshop_str, count - (int)z);
				else
					pc_setaccountreg(sd, nd->u.shop.pointshop_str, count - (int)z);
				break;
			case '@':
			default:
				pc_setglobalreg(sd, nd->u.shop.pointshop_str, count - (int)z);
				break;
			}
			break;
 	}

	for( i = 0; i < n; ++i ) {
		t_itemid nameid = item_list[i].nameid;
		unsigned short amount = item_list[i].qty;

#if PACKETVER >= 20131223
		if (nd->subtype == NPCTYPE_MARKETSHOP) {
			j = market_index[i];
			if (amount > shop[j].qty)
				return 1;
			shop[j].qty -= amount;
			npc_market_tosql(nd->exname, &shop[j]);
		}
#endif 

		if (itemdb_type(nameid) == IT_PETEGG)
			pet_create_egg(sd, nameid);
		else {
			unsigned short get_amt = amount;

			if ((itemdb_search(nameid))->flag.guid)
				get_amt = 1;

			for (k = 0; k < amount; k += get_amt) {
				struct item item_tmp;
				memset(&item_tmp, 0, sizeof(item_tmp));
				item_tmp.nameid = nameid;
				item_tmp.identify = 1;

				pc_additem(sd, &item_tmp, get_amt, LOG_TYPE_NPC);
			}
		}
	}

	// custom merchant shop exp bonus
	if( battle_config.shop_exp > 0 && z > 0 && (skill = pc_checkskill(sd,MC_DISCOUNT)) > 0 ) {
		uint16 sk_idx = skill_get_index(MC_DISCOUNT);
		if( sd->status.skill[sk_idx].flag >= SKILL_FLAG_REPLACED_LV_0 )
			skill = sd->status.skill[sk_idx].flag - SKILL_FLAG_REPLACED_LV_0;

		if( skill > 0 ) {
			z = z * (double)skill * (double)battle_config.shop_exp/10000.;
			if( z < 1 )
				z = 1;
			pc_gainexp(sd,NULL,0,(int)z, false);
		}
	}
	if (nd->subtype == NPCTYPE_POINTSHOP) {
		sprintf(output, msg_txt(sd, 767), nd->u.shop.pointshop_str, count - (int)z); // Your '%s' now: %d
		clif_disp_onlyself(sd, output, strlen(output) + 1);
	}

	return 0;
}


/// npc_selllist for script-controlled shops
static int npc_selllist_sub(struct map_session_data* sd, int n, unsigned short* item_list, struct npc_data* nd)
{
	char npc_ev[EVENT_NAME_LENGTH];
	int i, idx;
	int key_nameid = 0;
	int key_amount = 0;

	// discard old contents
	script_cleararray_pc(sd, "@sold_nameid", (void*)0);
	script_cleararray_pc(sd, "@sold_quantity", (void*)0);

	// save list of to be sold items
	for( i = 0; i < n; i++ )
	{
		idx = item_list[i*2]-2;

		script_setarray_pc(sd, "@sold_nameid", i, (void*)(intptr_t)sd->inventory.u.items_inventory[idx].nameid, &key_nameid);
		script_setarray_pc(sd, "@sold_quantity", i, (void*)(intptr_t)item_list[i*2+1], &key_amount);
	}

	// invoke event
	snprintf(npc_ev, ARRAYLENGTH(npc_ev), "%s::OnSellItem", nd->exname);
	npc_event(sd, npc_ev, 0);
	return 0;
}


/// Player item selling to npc shop.
///
/// @param item_list 'n' pairs <index,amount>
/// @return result code for clif_parse_NpcSellListSend
int npc_selllist(struct map_session_data* sd, int n, unsigned short* item_list)
{
	double z;
	int i,skill;
	struct npc_data *nd;

	nullpo_retr(1, sd);
	nullpo_retr(1, item_list);

	if( ( nd = npc_checknear(sd, map_id2bl(sd->npc_shopid)) ) == NULL || nd->subtype != NPCTYPE_SHOP && nd->subtype != NPCTYPE_ITEMSHOP && nd->subtype != NPCTYPE_POINTSHOP)
	{
		return 1;
	}

	z = 0;

	if (sd->status.zeny >= MAX_ZENY && nd->master_nd == NULL)
		return 1;

	// verify the sell list
	for( i = 0; i < n; i++ )
	{
		t_itemid nameid;
		int amount, idx, value;

		idx    = item_list[i*2]-2;
		amount = item_list[i*2+1];

		if( idx >= MAX_INVENTORY || idx < 0 || amount < 0 )
		{
			return 1;
		}

		nameid = sd->inventory.u.items_inventory[idx].nameid;

		if( !nameid || !sd->inventory_data[idx] || sd->inventory.u.items_inventory[idx].amount < amount )
		{
			return 1;
		}

		if( nd->master_nd )
		{// Script-controlled shops decide by themselves, what can be sold and at what price.
			continue;
		}

		value = pc_modifysellvalue(sd, sd->inventory_data[idx]->value_sell);

		z+= (double)value*amount;
	}

	if( nd->master_nd )
	{// Script-controlled shops
		return npc_selllist_sub(sd, n, item_list, nd->master_nd);
	}

	// delete items
	for( i = 0; i < n; i++ )
	{
		t_itemid nameid;
		int amount, idx;

		idx    = item_list[i*2]-2;
		amount = item_list[i*2+1];
		nameid = sd->inventory.u.items_inventory[idx].nameid;

		if( sd->inventory_data[idx]->type == IT_PETEGG && sd->inventory.u.items_inventory[idx].card[0] == CARD0_PET )
		{
			if( search_petDB_index(sd->inventory.u.items_inventory[idx].nameid, PET_EGG) >= 0 )
			{
				intif_delete_petdata(MakeDWord(sd->inventory.u.items_inventory[idx].card[1], sd->inventory.u.items_inventory[idx].card[2]));
			}
		}

		// Achievements [Smokexyz/Hercules]
		achievement_validate_item_sell(sd, sd->inventory.u.items_inventory[idx].nameid, amount);

		pc_delitem(sd, idx, amount, 0, 6, LOG_TYPE_NPC);
	}

	if (z + sd->status.zeny > MAX_ZENY && nd->master_nd == NULL)
		return 1;

	if( z > MAX_ZENY )
		z = MAX_ZENY;

	pc_getzeny(sd, (int)z, LOG_TYPE_NPC, NULL);

	// custom merchant shop exp bonus
	if( battle_config.shop_exp > 0 && z > 0 && ( skill = pc_checkskill(sd,MC_OVERCHARGE) ) > 0)
	{
		if( sd->status.skill[MC_OVERCHARGE].flag >= SKILL_FLAG_REPLACED_LV_0 )
			skill = sd->status.skill[MC_OVERCHARGE].flag - SKILL_FLAG_REPLACED_LV_0;

		if( skill > 0 )
		{
			z = z * (double)skill * (double)battle_config.shop_exp/10000.;
			if( z < 1 )
				z = 1;
			pc_gainexp(sd, NULL, 0, (int)z, false);
		}
	}

	return 0;
}

#ifndef TXT_ONLY
#if PACKETVER >= 20131223
/**
 * Saves persistent NPC Market Data into SQL
 * @param exname NPC exname
 * @param nameid Item ID
 * @param qty Stock
 **/
void npc_market_tosql(const char *exname, struct npc_item_list *list) {
	SqlStmt* stmt = SqlStmt_Malloc(mmysql_handle);
	if (SQL_ERROR == SqlStmt_Prepare(stmt, "REPLACE INTO `%s` (`name`,`nameid`,`price`,`amount`,`flag`) VALUES ('%s','%u','%d','%hu','%"PRIu8"')",
		market_table, exname, list->nameid, list->value, list->qty, list->flag) ||
		SQL_ERROR == SqlStmt_Execute(stmt))
		SqlStmt_ShowDebug(stmt);
	SqlStmt_Free(stmt);
}

/**
 * Removes persistent NPC Market Data from SQL
 * @param exname NPC exname
 * @param nameid Item ID
 * @param clear True: will removes all records related with the NPC
 **/
void npc_market_delfromsql_(const char *exname, t_itemid nameid, bool clear) {
	SqlStmt* stmt = SqlStmt_Malloc(mmysql_handle);
	if (clear) {
		if( SQL_ERROR == SqlStmt_Prepare(stmt, "DELETE FROM `%s` WHERE `name`='%s'", market_table, exname) ||
			SQL_ERROR == SqlStmt_Execute(stmt))
			SqlStmt_ShowDebug(stmt);
	} else {
		if (SQL_ERROR == SqlStmt_Prepare(stmt, "DELETE FROM `%s` WHERE `name`='%s' AND `nameid`='%u' LIMIT 1", market_table, exname, nameid) ||
			SQL_ERROR == SqlStmt_Execute(stmt))
			SqlStmt_ShowDebug(stmt);
	}
	SqlStmt_Free(stmt);
}

/**
 * Check NPC Market Shop for each entry
 **/
static int npc_market_checkall_sub(DBKey key, DBData* data, va_list ap) {
	struct s_npc_market *market = db_data2ptr(data);
	struct npc_data *nd = NULL;
	uint16 i;

	if (!market)
		return 1;

	nd = npc_name2id(market->exname);
	if (!nd) {
		ShowInfo("npc_market_checkall_sub: NPC '%s' not found, removing...\n", market->exname);
		npc_market_clearfromsql(market->exname);
		return 1;
	}
	else if (nd->subtype != NPCTYPE_MARKETSHOP || !nd->u.shop.shop_item || !nd->u.shop.count ) {
		ShowError("npc_market_checkall_sub: NPC '%s' is not proper for market, removing...\n", nd->exname);
		npc_market_clearfromsql(nd->exname);
		return 1;
	}

	if (!market->count || !market->list)
		return 1;

	for (i = 0; i < market->count; i++) {
		struct npc_item_list *list = &market->list[i];
		uint16 j;

		if (!list->nameid || !itemdb_exists(list->nameid)) {
			ShowError("npc_market_checkall_sub: NPC '%s' sells invalid item '%u', deleting...\n", nd->exname, list->nameid);
			npc_market_delfromsql(nd->exname, list->nameid);
			continue;
		}

		// Reloading stock from `market` table
		ARR_FIND(0, nd->u.shop.count, j, nd->u.shop.shop_item[j].nameid == list->nameid);
		if (j != nd->u.shop.count) {
			nd->u.shop.shop_item[j].value = list->value;
			nd->u.shop.shop_item[j].qty = list->qty;
			nd->u.shop.shop_item[j].flag = list->flag;
			npc_market_tosql(nd->exname, &nd->u.shop.shop_item[j]);
			continue;
		}

		if (list->flag&1) { // Item added by npcshopitem/npcshopadditem, add new entry
			RECREATE(nd->u.shop.shop_item, struct npc_item_list, nd->u.shop.count+1);
			nd->u.shop.shop_item[j].nameid = list->nameid;
			nd->u.shop.shop_item[j].value = list->value;
			nd->u.shop.shop_item[j].qty = list->qty;
			nd->u.shop.shop_item[j].flag = list->flag;
			nd->u.shop.count++;
			npc_market_tosql(nd->exname, &nd->u.shop.shop_item[j]);
		}
		else { // Removing "out-of-date" entry
			ShowError("npc_market_checkall_sub: NPC '%s' does not sell item %u (qty %hu), deleting...\n", nd->exname, list->nameid, list->qty);
			npc_market_delfromsql(nd->exname, list->nameid);
		}
	}

	return 0;
}

/**
 * Clear NPC market single entry
 **/
static int npc_market_free(DBKey key, DBData *data, va_list ap) {
	struct s_npc_market *market = db_data2ptr(data);
	if (!market)
		return 0;
	if (market->list) {
		aFree(market->list);
		market->list = NULL;
	}
	aFree(market);
	return 1;
}

/**
 * Check all existing  NPC Market Shop after first loading map-server or after reloading scripts.
 * Overwrite stocks from NPC by using stock entries from `market` table.
 **/
static void npc_market_checkall(void) {
	if (!db_size(NPCMarketDB))
		return;
	else {
		ShowInfo("Checking '"CL_WHITE"%d"CL_RESET"' NPC Markets...\n", db_size(NPCMarketDB));
		NPCMarketDB->foreach(NPCMarketDB, npc_market_checkall_sub);
		ShowStatus("Done checking '"CL_WHITE"%d"CL_RESET"' NPC Markets.\n", db_size(NPCMarketDB));
		NPCMarketDB->clear(NPCMarketDB, npc_market_free);
	}
}

/**
 * Loads persistent NPC Market Data from SQL, use the records after NPCs init'd to reuse the stock values.
 **/
static void npc_market_fromsql(void) {
	uint32 count = 0;

	if (SQL_ERROR == Sql_Query(mmysql_handle, "SELECT `name`,`nameid`,`price`,`amount`,`flag` FROM `%s` ORDER BY `name`", market_table)) {
		Sql_ShowDebug(mmysql_handle);
		return;
	}

	while (SQL_SUCCESS == Sql_NextRow(mmysql_handle)) {
		char *data;
		struct s_npc_market *market;
		struct npc_item_list list;

		Sql_GetData(mmysql_handle, 0, &data, NULL);

		if (!(market = (struct s_npc_market *)strdb_get(NPCMarketDB,data))) {
			CREATE(market, struct s_npc_market, 1);
			market->count = 0;
			safestrncpy(market->exname, data, strlen(data)+1);
			strdb_put(NPCMarketDB, market->exname, market);
		}

		Sql_GetData(mmysql_handle, 1, &data, NULL); list.nameid = strtoul(data, NULL, 10);
		Sql_GetData(mmysql_handle, 2, &data, NULL); list.value = atoi(data);
		Sql_GetData(mmysql_handle, 3, &data, NULL); list.qty = atoi(data);
		Sql_GetData(mmysql_handle, 4, &data, NULL); list.flag = atoi(data);

		RECREATE(market->list, struct npc_item_list, market->count+1);
		market->list[market->count++] = list;
		count++;
	}
	Sql_FreeResult(mmysql_handle);

	ShowStatus("Done loading '"CL_WHITE"%d"CL_RESET"' entries for '"CL_WHITE"%d"CL_RESET"' NPC Markets from '"CL_WHITE"%s"CL_RESET"' table.\n", count, db_size(NPCMarketDB), market_table);
}
#endif
#endif

int npc_remove_map(struct npc_data* nd)
{
	int m,i;
	nullpo_retr(1, nd);

	if(nd->bl.prev == NULL || nd->bl.m < 0)
		return 1; //Not assigned to a map.
  	m = nd->bl.m;
	if (nd->subtype == NPCTYPE_SCRIPT)
		skill_clear_unitgroup(&nd->bl);
	clif_clearunit_area(&nd->bl,CLR_RESPAWN);
	npc_unsetcells(nd);
	map_delblock(&nd->bl);
	//Remove npc from map[].npc list. [Skotlex]
	ARR_FIND( 0, map[m].npc_num, i, map[m].npc[i] == nd );
	if( i == map[m].npc_num ) return 2; //failed to find it?

	map[m].npc_num--;
	map[m].npc[i] = map[m].npc[map[m].npc_num];
	map[m].npc[map[m].npc_num] = NULL;
	return 0;
}

static int npc_unload_ev(DBKey key, DBData *data, va_list ap)
{
	struct event_data* ev = db_data2ptr(data);
	char* npcname = va_arg(ap, char *);

	if(strcmp(ev->nd->exname,npcname)==0){
		db_remove(ev_db, key);
		return 1;
	}
	return 0;
}

static int npc_unload_dup_sub(struct npc_data* nd, va_list args)
{
	int src_id;

	src_id = va_arg(args, int);
	if (nd->src_id == src_id)
		npc_unload(nd,true);
	return 0;
}

//Removes all npcs that are duplicates of the passed one. [Skotlex]
void npc_unload_duplicates(struct npc_data* nd)
{
	map_foreachnpc(npc_unload_dup_sub,nd->bl.id);
}

int npc_unload(struct npc_data* nd, bool single)
{
	nullpo_ret(nd);

	status_change_clear(&nd->bl, 1);
	npc_remove_map(nd);
	map_deliddb(&nd->bl);
	if(single)
		strdb_remove(npcname_db, nd->exname);

	if (nd->chat_id) // remove npc chatroom object and kick users
		chat_deletenpcchat(nd);

#ifdef PCRE_SUPPORT
	npc_chat_finalize(nd); // deallocate npc PCRE data structures
#endif

	if (nd->path)
		aFree(nd->path);

	if( (nd->subtype == NPCTYPE_SHOP || nd->subtype == NPCTYPE_CASHSHOP || nd->subtype == NPCTYPE_MARKETSHOP || nd->subtype == NPCTYPE_ITEMSHOP || nd->subtype == NPCTYPE_POINTSHOP) && nd->src_id == 0) //src check for duplicate shops [Orcao]
		aFree(nd->u.shop.shop_item);
	else if( nd->subtype == NPCTYPE_SCRIPT )
	{
		struct s_mapiterator* iter;
		struct block_list* bl;		

		if(single)
			ev_db->foreach(ev_db,npc_unload_ev,nd->exname); //Clean up all events related

		iter = mapit_geteachpc();  
		for (bl = (struct block_list*)mapit_first(iter); mapit_exists(iter); bl = (struct block_list*)mapit_next(iter)) {
			struct map_session_data *sd = ((TBL_PC*)bl);
			if (sd && sd->npc_timer_id != INVALID_TIMER) {
				const struct TimerData *td = get_timer(sd->npc_timer_id);

				if( td && td->id != nd->bl.id )
					continue;

				if( td && td->data )
					ers_free(timer_event_ers, (void*)td->data);
				delete_timer(sd->npc_timer_id, npc_timerevent);
				sd->npc_timer_id = INVALID_TIMER;
			}
		}  
		mapit_free(iter);

		if (nd->u.scr.timerid != INVALID_TIMER) {
			const struct TimerData *td;
			td = get_timer(nd->u.scr.timerid);
			if (td && td->data) 
				ers_free(timer_event_ers, (void*)td->data);
			delete_timer(nd->u.scr.timerid, npc_timerevent);
		}
		if (nd->u.scr.timer_event)
			aFree(nd->u.scr.timer_event);
		if (nd->src_id == 0) {
			if(nd->u.scr.script) {
				script_free_code(nd->u.scr.script);
				nd->u.scr.script = NULL;
			}
			if (nd->u.scr.label_list) {
				aFree(nd->u.scr.label_list);
				nd->u.scr.label_list = NULL;
				nd->u.scr.label_list_num = 0;
			}
		}
		if (nd->sc_display_count) {
			unsigned char i;

			for (i = 0; i < nd->sc_display_count; i++)
				ers_free(npc_sc_display_ers, nd->sc_display[i]);
			nd->sc_display_count = 0;
			aFree(nd->sc_display);
			nd->sc_display = NULL;
		}
		if (nd->u.scr.guild_id)
			guild_flag_remove(nd);
	}

	script_stop_sleeptimers(nd->bl.id);

	aFree(nd);

	return 0;
}

//
// NPC Source Files
//

/// Clears the npc source file list
static void npc_clearsrcfile(void)
{
	struct npc_src_list* file = npc_src_files;

	while( file != NULL )
	{
		struct npc_src_list* file_tofree = file;
		file = file->next;
		aFree(file_tofree);
	}
	npc_src_files = NULL;
}

/// Adds a npc source file (or removes all)
void npc_addsrcfile(const char* name)
{
	struct npc_src_list* file;
	struct npc_src_list* file_prev = NULL;

	if( strcmpi(name, "clear") == 0 )
	{
		npc_clearsrcfile();
		return;
	}

	// prevent multiple insert of source files
	file = npc_src_files;
	while( file != NULL )
	{
		if( strcmp(name, file->name) == 0 )
			return;// found the file, no need to insert it again
		file_prev = file;
		file = file->next;
	}

	file = (struct npc_src_list*)aMalloc(sizeof(struct npc_src_list) + strlen(name));
	file->next = NULL;
	safestrncpy(file->name, name, strlen(name) + 1);
	if( file_prev == NULL )
		npc_src_files = file;
	else
		file_prev->next = file;
}

/// Removes a npc source file (or all)
void npc_delsrcfile(const char* name)
{
	struct npc_src_list* file = npc_src_files;
	struct npc_src_list* file_prev = NULL;

	if( strcmpi(name, "all") == 0 )
	{
		npc_clearsrcfile();
		return;
	}

	while( file != NULL )
	{
		if( strcmp(file->name, name) == 0 )
		{
			if( npc_src_files == file )
				npc_src_files = file->next;
			else
				file_prev->next = file->next;
			aFree(file);
			break;
		}
		file_prev = file;
		file = file->next;
	}
}

/// Parses and sets the name and exname of a npc.
/// Assumes that m, x and y are already set in nd.
static void npc_parsename(struct npc_data* nd, const char* name, const char* start, const char* buffer, const char* filepath)
{
	const char* p;
	struct npc_data* dnd;// duplicate npc
	char newname[NPC_NAME_LENGTH+1];

	// parse name
	p = strstr(name,"::");
	if( p )
	{// <Display name>::<Unique name>
		size_t len = p-name;
		if( len > NPC_NAME_LENGTH )
		{
			ShowWarning("npc_parsename: Display name of '%s' is too long (len=%u) in file '%s', line'%d'. Truncating to %u characters.\n", name, (unsigned int)len, filepath, strline(buffer,start-buffer), NPC_NAME_LENGTH);
			safestrncpy(nd->name, name, sizeof(nd->name));
		}
		else
		{
			memcpy(nd->name, name, len);
			memset(nd->name+len, 0, sizeof(nd->name)-len);
		}
		len = strlen(p+2);
		if( len > NPC_NAME_LENGTH )
			ShowWarning("npc_parsename: Unique name of '%s' is too long (len=%u) in file '%s', line'%d'. Truncating to %u characters.\n", name, (unsigned int)len, filepath, strline(buffer,start-buffer), NPC_NAME_LENGTH);
		safestrncpy(nd->exname, p+2, sizeof(nd->exname));
	}
	else
	{// <Display name>
		size_t len = strlen(name);
		if( len > NPC_NAME_LENGTH )
			ShowWarning("npc_parsename: Name '%s' is too long (len=%u) in file '%s', line'%d'. Truncating to %u characters.\n", name, (unsigned int)len, filepath, strline(buffer,start-buffer), NPC_NAME_LENGTH);
		safestrncpy(nd->name, name, sizeof(nd->name));
		safestrncpy(nd->exname, name, sizeof(nd->exname));
	}

	if( *nd->exname == '\0' || strstr(nd->exname,"::") != NULL )
	{// invalid
		snprintf(newname, ARRAYLENGTH(newname), "0_%d_%d_%d", nd->bl.m, nd->bl.x, nd->bl.y);
		ShowWarning("npc_parsename: Invalid unique name in file '%s', line'%d'. Renaming '%s' to '%s'.\n", filepath, strline(buffer,start-buffer), nd->exname, newname);
		safestrncpy(nd->exname, newname, sizeof(nd->exname));
	}

	if( (dnd=npc_name2id(nd->exname)) != NULL )
	{// duplicate unique name, generate new one
		char this_mapname[32];
		char other_mapname[32];
		int i = 0;

		do
		{
			++i;
			snprintf(newname, ARRAYLENGTH(newname), "%d_%d_%d_%d", i, nd->bl.m, nd->bl.x, nd->bl.y);
		}
		while( npc_name2id(newname) != NULL );

		strcpy(this_mapname, (nd->bl.m==-1?"(not on a map)":mapindex_id2name(map[nd->bl.m].index)));
		strcpy(other_mapname, (dnd->bl.m==-1?"(not on a map)":mapindex_id2name(map[dnd->bl.m].index)));

		ShowWarning("npc_parsename: Duplicate unique name in file '%s', line'%d'. Renaming '%s' to '%s'.\n", filepath, strline(buffer,start-buffer), nd->exname, newname);
		ShowDebug("this npc:\n   display name '%s'\n   unique name '%s'\n   map=%s, x=%d, y=%d\n", nd->name, nd->exname, this_mapname, nd->bl.x, nd->bl.y);
		ShowDebug("other npc:\n   display name '%s'\n   unique name '%s'\n   map=%s, x=%d, y=%d\n", dnd->name, dnd->exname, other_mapname, dnd->bl.x, dnd->bl.y);
		safestrncpy(nd->exname, newname, sizeof(nd->exname));
	}

	CREATE(nd->path, char, strlen(filepath) + 1);
	safestrncpy(nd->path, filepath, strlen(filepath) + 1);
}

/**
 * Create a bare NPC object.
 * @param m: Map ID
 * @param x: X location
 * @param y: Y location
 * @return npc_data
 */
struct npc_data *npc_create_npc(int16 m, int16 x, int16 y) {
	struct npc_data *nd;

	CREATE(nd, struct npc_data, 1);
	nd->bl.id = npc_get_new_npc_id();
	nd->bl.prev = nd->bl.next = NULL;
	nd->bl.m = m;
	nd->bl.x = x;
	nd->bl.y = y;
	safestrncpy(nd->name, "", ARRAYLENGTH(nd->name));// empty display name
	nd->sc_display = NULL;
	nd->sc_display_count = 0;

	return nd;
}

/**
 * Add then display an npc warp on map
 * @param name : warp unique name
 * @param from_mapid : mapid to warp from
 * @param from_x : x coordinate of warp
 * @param from_y : y coordinate of warp
 * @param xs : x lenght of warp (for trigger activation)
 * @param ys : y lenght of warp (for trigger activation)
 * @param to_mapindex : mapid to warp to
 * @param to_x : x coordinate to warp to
 * @param to_y : y coordinate to warp to
 * @return NULL:failed creation, npc_data* new warp
 */
struct npc_data* npc_add_warp(char* name, short from_mapid, short from_x, short from_y, short xs, short ys, unsigned short to_mapindex, short to_x, short to_y)
{
	int i, flag = 0;
	struct npc_data *nd;

	nd = npc_create_npc(from_mapid, from_x, from_y);
	map_addnpc(from_mapid, nd);
	
	if (name)
	{
		safestrncpy(nd->exname, name, ARRAYLENGTH(nd->exname));
		if (npc_name2id(nd->exname) != NULL)
			flag = 1;
	}

	if (name[0] == '\0' || flag == 1)
		snprintf(nd->exname, ARRAYLENGTH(nd->exname), "warp_%d_%d_%d", from_mapid, from_x, from_y);

	for( i = 0; npc_name2id(nd->exname) != NULL; ++i )
		snprintf(nd->exname, ARRAYLENGTH(nd->exname), "warp%d_%d_%d_%d", i, from_mapid, from_x, from_y);

	if( battle_config.warp_point_debug )
		nd->class_ = WARP_DEBUG_CLASS;
	else
		nd->class_ = WARP_CLASS;
	nd->speed = 200;

	nd->u.warp.mapindex = to_mapindex;
	nd->u.warp.x = to_x;
	nd->u.warp.y = to_y;
	nd->u.warp.xs = xs;
	nd->u.warp.ys = xs;
	nd->bl.type = BL_NPC;
	nd->subtype = NPCTYPE_WARP;
	nd->trigger_on_hidden = false;
	npc_setcells(nd);
	
	if (map_addblock(&nd->bl))
		return NULL;

	status_set_viewdata(&nd->bl, nd->class_);
	status_change_init(&nd->bl);
	unit_dataset(&nd->bl);
	if (map[nd->bl.m].users)
		clif_spawn(&nd->bl);
	strdb_put(npcname_db, nd->exname, nd);

	return nd;
}

/**
 * Parses a warp npc.
 * Line definition <from mapname>,<fromX>,<fromY>,<facing>%TAB%warp%TAB%<warp name>%TAB%<spanx>,<spany>,<to mapname>,<toX>,<toY>
 * @param w1 : word 1 before tab (<from map name>,<fromX>,<fromY>,<facing>)
 * @param w2 : word 2 before tab (warp), keyword that sent us in this parsing
 * @param w3 : word 3 before tab (<warp name>)
 * @param w4 : word 4 before tab (<spanx>,<spany>,<to mapname>,<toX>,<toY>)
 * @param start : index to start parsing
 * @param buffer : lines to parses
 * @param filepath : filename with path wich we are parsing
 * @return new index for next parsing
 */
static const char* npc_parse_warp(char* w1, char* w2, char* w3, char* w4, const char* start, const char* buffer, const char* filepath)
{
	int x, y, xs, ys, to_x, to_y, m;
	unsigned short i;
	char mapname[32], to_mapname[32];
	struct npc_data *nd;

	// w1=<from map name>,<fromX>,<fromY>,<facing>
	// w4=<spanx>,<spany>,<to map name>,<toX>,<toY>
	if( sscanf(w1, "%31[^,],%d,%d", mapname, &x, &y) != 3
	||	sscanf(w4, "%d,%d,%31[^,],%d,%d", &xs, &ys, to_mapname, &to_x, &to_y) != 5 )
	{
		ShowError("npc_parse_warp: Invalid warp definition in file '%s', line '%d'.\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
		return strchr(start,'\n');// skip and continue
	}

	m = map_mapname2mapid(mapname);
	i = mapindex_name2id(to_mapname);
	if( i == 0 )
	{
		ShowError("npc_parse_warp: Unknown destination map in file '%s', line '%d' : %s\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,start-buffer), to_mapname, w1, w2, w3, w4);
		return strchr(start,'\n');// skip and continue
	}

	nd = npc_create_npc(m, x, y);
	map_addnpc(m, nd);
	npc_parsename(nd, w3, start, buffer, filepath);

	if (!battle_config.warp_point_debug)
		nd->class_ = WARP_CLASS;
	else
		nd->class_ = WARP_DEBUG_CLASS;
	nd->speed = 200;

	nd->u.warp.mapindex = i;
	nd->u.warp.x = to_x;
	nd->u.warp.y = to_y;
	nd->u.warp.xs = xs;
	nd->u.warp.ys = ys;
	npc_warp++;
	nd->bl.type = BL_NPC;
	nd->subtype = NPCTYPE_WARP;
	if (strcasecmp("warp2", w2) == 0)
		nd->trigger_on_hidden = true;
	else
		nd->trigger_on_hidden = false;
	npc_setcells(nd);
	
	if (map_addblock(&nd->bl)) //couldn't add on map
		return strchr(start, '\n');

	status_set_viewdata(&nd->bl, nd->class_);
	status_change_init(&nd->bl);
	unit_dataset(&nd->bl);
	if (map[nd->bl.m].users)
		clif_spawn(&nd->bl);
	strdb_put(npcname_db, nd->exname, nd);

	return strchr(start,'\n');// continue
}

/**
 * Parses a shop/cashshop npc.
 * Line definition :
 * <map name>,<x>,<y>,<facing>{,<episode_flag>,<min_episode>,<max_episode>}%TAB%shop%TAB%<NPC Name>%TAB%<sprite id>,<itemid>:<price>{,<itemid>:<price>...}
 * <map name>,<x>,<y>,<facing>{,<episode_flag>,<min_episode>,<max_episode>}%TAB%cashshop%TAB%<NPC Name>%TAB%<sprite id>,<itemid>:<price>{,<itemid>:<price>...}
 * <map name>,<x>,<y>,<facing>{,<episode_flag>,<min_episode>,<max_episode>}%TAB%marketshop%TAB%<NPC Name>%TAB%<sprite id>,<itemid>:<price>:<quantity>{,<itemid>:<price>:<quantity>...}
 * @param w1 : word 1 before tab (<from map name>,<x>,<y>,<facing>{,<episode_flag>,<min_episode>,<max_episode>})
 * @param w2 : word 2 before tab (shop|cashshop|marketshop), keyword that sent us in this parsing
 * @param w3 : word 3 before tab (<NPC Name>)
 * @param w4 : word 4 before tab (<sprited id>,<shop definition...>)
 * @param start : index to start parsing
 * @param buffer : lines to parses
 * @param filepath : filename with path wich we are parsing
 * @return new index for next parsing
 */
static const char* npc_parse_shop(char* w1, char* w2, char* w3, char* w4, const char* start, const char* buffer, const char* filepath) {
	char *p, point_str[32];
	int x, y, dir, m, is_discount = 0, episode_ident = 0, min_episode = 0, max_episode = 0;
	t_itemid nameid = 0;
	struct npc_data *nd;
	enum npc_subtype type;

	if( strcmp(w1,"-") == 0 )
	{// 'floating' shop?
		x = y = dir = 0;
		m = -1;
	} else {
		char mapname[32];
		if( sscanf(w1, "%31[^,],%d,%d,%d,%d,%d,%d", mapname, &x, &y, &dir, &episode_ident, &min_episode, &max_episode) == 7); // w1=<map name>,<x>,<y>,<facing>,<episode_flag>,<min_episode>,<max_episode> [15peaces]
		else if( sscanf(w1, "%31[^,],%d,%d,%d", mapname, &x, &y, &dir) == 4);// w1=<map name>,<x>,<y>,<facing>
		else {
			ShowError("npc_parse_shop: Invalid shop definition in file '%s', line '%d'.\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
			return strchr(start,'\n');// skip and continue
		}

		m = map_mapname2mapid(mapname);
	}

	if( m != -1 && ( x < 0 || x >= map[m].xs || y < 0 || y >= map[m].ys ) ) {
		ShowWarning("npc_parse_shop: coordinates %d/%d are out of bounds in map %s(%dx%d), in file '%s', line '%d'\n", x, y, map[m].name, map[m].xs, map[m].ys,filepath,strline(buffer,start-buffer));
	}

	if( !strcasecmp(w2,"cashshop") )
		type = NPCTYPE_CASHSHOP;
	else if( !strcasecmp(w2, "marketshop") )
		type = NPCTYPE_MARKETSHOP;
	else if (!strcasecmp(w2, "itemshop"))
		type = NPCTYPE_ITEMSHOP;
	else if (!strcasecmp(w2, "pointshop"))
		type = NPCTYPE_POINTSHOP;
	else
		type = NPCTYPE_SHOP;

	p = strchr(w4,',');
	memset(point_str,'\0',sizeof(point_str));

	switch(type) {
		case NPCTYPE_MARKETSHOP:
#if PACKETVER < 20131223
			ShowError("npc_parse_shop: (MARKETSHOP) Feature is disabled, need client 20131223 or newer. Ignoring file '%s', line '%d\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer, start - buffer), w1, w2, w3, w4);
			return strchr(start, '\n'); // skip and continue
#else
			is_discount = 0;
			break;
#endif
		case NPCTYPE_ITEMSHOP: {
			if (sscanf(p, ",%u:%11d,", &nameid, &is_discount) < 1) {
				ShowError("npc_parse_shop: Invalid item cost definition in file '%s', line '%d'. Ignoring the rest of the line...\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer, start - buffer), w1, w2, w3, w4);
				return strchr(start, '\n'); // skip and continue
			}
			if (!itemdb_exists(nameid)) {
				ShowWarning("npc_parse_shop: Invalid item ID cost in file '%s', line '%d' (id '%u').\n", filepath, strline(buffer, start - buffer), nameid);
				return strchr(start, '\n'); // skip and continue
			}
			p = strchr(p + 1, ',');
			break;
		}
		case NPCTYPE_POINTSHOP: {
			if (sscanf(p, ",%32[^,:]:%11d,", point_str, &is_discount) < 1) {
				ShowError("npc_parse_shop: Invalid item cost definition in file '%s', line '%d'. Ignoring the rest of the line...\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer, start - buffer), w1, w2, w3, w4);
				return strchr(start, '\n'); // skip and continue
			}
			switch (point_str[0]) {
			case '$':
			case '.':
			case '\'':
				ShowWarning("npc_parse_shop: Invalid item cost variable type (must be permanent character or account based) in file '%s', line '%d'. Ignoring the rest of the line...\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer, start - buffer), w1, w2, w3, w4);
				return strchr(start, '\n'); // skip and continue
				break;
			}
			if (point_str[strlen(point_str) - 1] == '$') {
				ShowWarning("npc_parse_shop: Invalid item cost variable type (must be integer) in file '%s', line '%d'. Ignoring the rest of the line...\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer, start - buffer), w1, w2, w3, w4);
				return strchr(start, '\n'); // skip and continue
			}
			p = strchr(p + 1, ',');
			break;
		}
		default:
			if (sscanf(p, ",%32[^,:]:%11d,", point_str, &is_discount) == 2) {
				is_discount = 1;
			}
			else {
				if (!strcasecmp(point_str, "yes")) {
					is_discount = 1;
				}
				else if (!strcasecmp(point_str, "no")) {
					is_discount = 0;
				}
				else {
					ShowError("npc_parse_shop: unknown discount setting %s\n", point_str);
					return strchr(start, '\n'); // skip and continue
				}

				p = strchr(p + 1, ',');
			}
			break;
	}

	//Check Episode [15peaces]
	if(min_episode <= 0 && max_episode <= 0);
	else if(battle_config.feature_episode >= min_episode && max_episode == -1);
	else if(battle_config.feature_episode >= min_episode && battle_config.feature_episode <= max_episode);
	else
		return strchr(start, '\n'); // skip and continue
	
	nd = npc_create_npc(m, x, y);

	nd->u.shop.count = 0;
	while ( p ) {
		unsigned short qty = 0;
		t_itemid nameid2;
		int value;
		struct item_data* id;
		bool skip = false;

		if( p == NULL )
			break;
		switch(type) {
			case NPCTYPE_MARKETSHOP:
#if PACKETVER >= 20131223
				if (sscanf(p, ",%u:%d:%hu", &nameid2, &value, &qty) != 3) {
					ShowError("npc_parse_shop: (MARKETSHOP) Invalid item definition in file '%s', line '%d'. Ignoring the rest of the line...\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer, start - buffer), w1, w2, w3, w4);
					skip = true;
				}
#endif
				break;
			default:
				if (sscanf(p, ",%u:%d", &nameid2, &value) != 2) {
					ShowError("npc_parse_shop: Invalid item definition in file '%s', line '%d'. Ignoring the rest of the line...\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer, start - buffer), w1, w2, w3, w4);
					skip = true;
				}
				break;
		}

		if (skip)
			break;

		if( (id = itemdb_exists(nameid2)) == NULL ) {
			ShowWarning("npc_parse_shop: Invalid sell item in file '%s', line '%d' (id '%u').\n", filepath, strline(buffer,start-buffer), nameid2);
			p = strchr(p+1,',');
			continue;
		}
		if( value < 0 ) {
			if (type == NPCTYPE_SHOP || type == NPCTYPE_MARKETSHOP) value = id->value_buy;
			else value = 0; // Cashshop doesn't have a "buy price" in the item_db
		}
		if (value == 0 && (type == NPCTYPE_SHOP || type == NPCTYPE_MARKETSHOP))
		{ // NPC selling items for free!
			ShowWarning("npc_parse_shop: Item %s [%u] is being sold for FREE in file '%s', line '%d'.\n",
				id->name, nameid, filepath, strline(buffer, start - buffer));
		}
		if( type == NPCTYPE_SHOP && value*0.75 < id->value_sell*1.24 ) { // Exploit possible: you can buy and sell back with profit
			ShowWarning("npc_parse_shop: Item %s [%u] discounted buying price (%d->%d) is less than overcharged selling price (%d->%d) at file '%s', line '%d'.\n",
				id->name, nameid2, value, (int)(value*0.75), id->value_sell, (int)(id->value_sell*1.24), filepath, strline(buffer,start-buffer));
		}
		if (type == NPCTYPE_MARKETSHOP && (qty < -1 || qty > UINT16_MAX)) {
			ShowWarning("npc_parse_shop: Item %s [%u] is stocked with invalid value %d, changed to 1. File '%s', line '%d'.\n",
				id->name, nameid2, filepath, strline(buffer,start-buffer));
			qty = 1;
		}
		//for logs filters, atcommands and iteminfo script command
		if( id->maxchance == 0 )
			id->maxchance = -1; // -1 would show that the item's sold in NPC Shop
		
#if PACKETVER >= 20131223
		if (nd->u.shop.count && type == NPCTYPE_MARKETSHOP) {
			uint16 i;
			// Duplicate entry? Replace the value
			ARR_FIND(0, nd->u.shop.count, i, nd->u.shop.shop_item[i].nameid == nameid);
			if (i != nd->u.shop.count) {
				nd->u.shop.shop_item[i].qty = qty;
				nd->u.shop.shop_item[i].value = value;
				p = strchr(p+1,',');
				continue;
			}
		}
#endif

		RECREATE(nd->u.shop.shop_item, struct npc_item_list,nd->u.shop.count+1);

		nd->u.shop.shop_item[nd->u.shop.count].nameid = nameid2;
		nd->u.shop.shop_item[nd->u.shop.count].value = value;

#if PACKETVER >= 20131223
		nd->u.shop.shop_item[nd->u.shop.count].flag = 0;
		if (type == NPCTYPE_MARKETSHOP)
			nd->u.shop.shop_item[nd->u.shop.count].qty = qty;
#endif
		nd->u.shop.count++;
		p = strchr(p+1,',');
	}
	if( nd->u.shop.count == 0 ) {
		ShowWarning("npc_parse_shop: Ignoring empty shop in file '%s', line '%d'.\n", filepath, strline(buffer,start-buffer));
		aFree(nd);
		return strchr(start,'\n');// continue
	}

	if (type == NPCTYPE_ITEMSHOP) {
		// Item shop currency
		nd->u.shop.itemshop_nameid = nameid;
	}
	else if (type == NPCTYPE_POINTSHOP) {
		// Point shop currency
		safestrncpy(nd->u.shop.pointshop_str, point_str, strlen(point_str) + 1);
	}

	nd->u.shop.discount = is_discount > 0;

	npc_parsename(nd, w3, start, buffer, filepath);
	nd->class_ = m==-1?-1:atoi(w4);
	nd->speed = 200;

	++npc_shop;
	nd->bl.type = BL_NPC;
	nd->subtype = type;
#if PACKETVER >= 20131223
	// Insert market data to table
	if (nd->subtype == NPCTYPE_MARKETSHOP) {
		uint16 i;
		for (i = 0; i < nd->u.shop.count; i++)
			npc_market_tosql(nd->exname, &nd->u.shop.shop_item[i]);
	}
#endif
	if( m >= 0 )
	{// normal shop npc
		map_addnpc(m,nd);
		if(map_addblock(&nd->bl))
			return strchr(start,'\n');
		status_set_viewdata(&nd->bl, nd->class_);
		status_change_init(&nd->bl);
		unit_dataset(&nd->bl);
		nd->ud.dir = dir;
		if( map[nd->bl.m].users )
			clif_spawn(&nd->bl);
	} else
	{// 'floating' shop?
		map_addiddb(&nd->bl);
	}
	strdb_put(npcname_db, nd->exname, nd);
	return strchr(start,'\n');// continue
}

/** [Cydh]
* Check if the shop is affected by discount or not
* @param type Type of NPC shop (enum npc_subtype)
* @param discount Discount flag of NPC shop
* @return bool 'true' is discountable, 'false' otherwise
*/
bool npc_shop_discount(enum npc_subtype type, bool discount) {
	if (type == NPCTYPE_SHOP || (type != NPCTYPE_SHOP && discount))
		return true;

	if ((type == NPCTYPE_ITEMSHOP && battle_config.discount_item_point_shop & 1) ||
		(type == NPCTYPE_POINTSHOP && battle_config.discount_item_point_shop & 2))
		return true;
	return false;
}

/*==========================================
 * NPCのラベルデータコンバート
 *------------------------------------------*/
int npc_convertlabel_db(DBKey key, DBData *data, va_list ap)
{
	const char* lname = (const char*)key.str;
	int lpos = db_data2i(data);
	struct npc_label_list** label_list;
	int* label_list_num;
	const char* filepath;
	struct npc_label_list* label;
	const char *p;
	int len;

	nullpo_ret(label_list = va_arg(ap,struct npc_label_list**));
	nullpo_ret(label_list_num = va_arg(ap,int*));
	nullpo_ret(filepath = va_arg(ap,const char*));

	// In case of labels not terminated with ':', for user defined function support
	p = lname;
	while( ISALNUM(*p) || *p == '_' )
		++p;
	len = p-lname;

	// here we check if the label fit into the buffer
	if( len > 23 )
	{
		ShowError("npc_convertlabel_db: label name longer than 23 chars! '%s'\n (%s)", lname, filepath);
		return 0;
	}

	if( *label_list == NULL )
	{
		*label_list = (struct npc_label_list *) aCalloc (1, sizeof(struct npc_label_list));
		*label_list_num = 0;
	} else
		*label_list = (struct npc_label_list *) aRealloc (*label_list, sizeof(struct npc_label_list)*(*label_list_num+1));
	label = *label_list+*label_list_num;

	safestrncpy(label->name, lname, sizeof(label->name));
	label->pos = lpos;
	++(*label_list_num);

	return 0;
}

// Skip the contents of a script.
static const char* npc_skip_script(const char* start, const char* buffer, const char* filepath)
{
	const char* p;
	int curly_count;

	if( start == NULL )
		return NULL;// nothing to skip

	// initial bracket (assumes the previous part is ok)
	p = strchr(start,'{');
	if( p == NULL )
	{
		ShowError("npc_skip_script: Missing left curly in file '%s', line'%d'.", filepath, strline(buffer,start-buffer));
		return NULL;// can't continue
	}

	// skip everything
	for( curly_count = 1; curly_count > 0 ; )
	{
		p = skip_space(p+1) ;
		if( *p == '}' )
		{// right curly
			--curly_count;
		}
		else if( *p == '{' )
		{// left curly
			++curly_count;
		}
		else if( *p == '"' )
		{// string
			for( ++p; *p != '"' ; ++p )
			{
				if( *p == '\\' && (unsigned char)p[-1] <= 0x7e )
					++p;// escape sequence (not part of a multibyte character)
				else if( *p == '\0' )
				{
					script_error(buffer, filepath, 0, "Unexpected end of string.", p);
					return NULL;// can't continue
				}
				else if( *p == '\n' )
				{
					script_error(buffer, filepath, 0, "Unexpected newline at string.", p);
					return NULL;// can't continue
				}
			}
		}
		else if( *p == '\0' )
		{// end of buffer
			ShowError("Missing %d right curlys at file '%s', line '%d'.\n", curly_count, filepath, strline(buffer,p-buffer));
			return NULL;// can't continue
		}
	}

	return p+1;// return after the last '}'
}

/// Parses a npc script.
///
/// -%TAB%script%TAB%<NPC Name>%TAB%-1,{<code>}
/// <map name>,<x>,<y>,<facing>%TAB%script%TAB%<NPC Name>%TAB%<sprite id>,{<code>}
/// <map name>,<x>,<y>,<facing>%TAB%script%TAB%<NPC Name>%TAB%<sprite id>,<triggerX>,<triggerY>,{<code>}
/// <map name>,<x>,<y>,<facing>%TAB%script%TAB%<NPC Name>%TAB%<episode_flag>,<min_episode>,<max_episode>,<sprite id>,{<code>}
/// <map name>,<x>,<y>,<facing>%TAB%script%TAB%<NPC Name>%TAB%<episode_flag>,<min_episode>,<max_episode>,<sprite id>,<triggerX>,<triggerY>,{<code>}
static const char* npc_parse_script(char* w1, char* w2, char* w3, char* w4, const char* start, const char* buffer, const char* filepath, bool runOnInit)
{
	int x, y, dir = 0, m, xs = 0, ys = 0, class_ = 0, episode_ident, min_episode, max_episode = 0;	// [Valaris] thanks to fov
	char mapname[32];
	struct script_code *script;
	int i;
	bool ignore = false;
	const char* end;
	const char* script_start;

	struct npc_label_list* label_list;
	int label_list_num;
	struct npc_data* nd;

	if( strcmp(w1, "-") == 0 )
	{// floating npc
		x = 0;
		y = 0;
		m = -1;
	}
	else
	{// npc in a map
		if( sscanf(w1, "%31[^,],%d,%d,%d", mapname, &x, &y, &dir) != 4 )
		{
			ShowError("npc_parse_script: Invalid placement format for a script in file '%s', line '%d'. Skipping the rest of file...\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
			return NULL;// unknown format, don't continue
		}
		m = map_mapname2mapid(mapname);
	}

	script_start = strstr(start,",{");
	end = strchr(start,'\n');
	if( strstr(w4,",{") == NULL || script_start == NULL || (end != NULL && script_start > end) )
	{
		ShowError("npc_parse_script: Missing left curly ',{' in file '%s', line '%d'. Skipping the rest of the file.\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
		return NULL;// can't continue
	}
	++script_start;

	end = npc_skip_script(script_start, buffer, filepath);
	if( end == NULL )
		return NULL;// (simple) parse error, don't continue

	//Episode System [15peaces]
	if( sscanf(w4, "%d,%d,%d,%d,%d,%d", &episode_ident, &min_episode, &max_episode, &class_, &xs, &ys) == 6 ); // OnTouch area defined
	else if( sscanf(w4, "%d,%d,%d,%d", &episode_ident, &min_episode, &max_episode, &class_) == 4 ); // no OnTouch area
	else {// no episode
		min_episode = 0;
		max_episode = 0;
		if (sscanf(w4, "%d,%d,%d", &class_, &xs, &ys) == 3); // OnTouch area
		else { // no OnTouch area
			class_ = atoi(w4);
			xs = -1;
			ys = -1;
		}
	}

	//Check Episode
	if(min_episode == 0 && max_episode == 0);
	else if(battle_config.feature_episode >= min_episode && max_episode == -1);
	else if(battle_config.feature_episode >= min_episode && battle_config.feature_episode <= max_episode);
	else
		ignore = true;// ignore npc [15peaces]

	if(ignore == false){
		script = parse_script(script_start, filepath, strline(buffer,script_start-buffer), SCRIPT_USE_LABEL_DB);
		label_list = NULL;
		label_list_num = 0;
		if( script )
		{
			DBMap* label_db = script_get_label_db();
			label_db->foreach(label_db, npc_convertlabel_db, &label_list, &label_list_num, filepath);
			db_clear(label_db); // not needed anymore, so clear the db
		}
	}

	nd = npc_create_npc(m, x, y);

	if(ignore == false){
		nd->u.scr.ep_min = min_episode;
		nd->u.scr.ep_max = max_episode;
		nd->u.scr.xs = xs;
		nd->u.scr.ys = ys;
		npc_parsename(nd, w3, start, buffer, filepath);
		nd->class_ = class_;
		nd->speed = 200;
		nd->u.scr.script = script;
		nd->u.scr.label_list = label_list;
		nd->u.scr.label_list_num = label_list_num;

		++npc_script;
		nd->bl.type = BL_NPC;
		nd->subtype = NPCTYPE_SCRIPT;
	}

	if( m >= 0 && ignore == false)
	{
		map_addnpc(m, nd);
		status_change_init(&nd->bl);
		unit_dataset(&nd->bl);
		nd->ud.dir = dir;
		npc_setcells(nd);

		if (map_addblock(&nd->bl))
			return NULL;

		if( class_ >= 0 )
		{
			status_set_viewdata(&nd->bl, nd->class_);
			if (map[nd->bl.m].users)
				clif_spawn(&nd->bl);
		}
	}
	else
	{
		// we skip map_addnpc, but still add it to the list of ID's
		map_addiddb(&nd->bl);
	}
	strdb_put(npcname_db, nd->exname, nd);

	//-----------------------------------------
	// Loop through labels to export them as necessary
	for (i = 0; i < nd->u.scr.label_list_num; i++) {
		if (npc_event_export(nd, i)) {
			ShowWarning("npc_parse_script : duplicate event %s::%s (%s)\n",
				nd->exname, nd->u.scr.label_list[i].name, filepath);
		}
		npc_timerevent_export(nd, i);
	}

	nd->u.scr.timerid = INVALID_TIMER;

	if (runOnInit) {
		char evname[EVENT_NAME_LENGTH];
		struct event_data *ev;

		snprintf(evname, ARRAYLENGTH(evname), "%s::OnInit", nd->exname);

		if ((ev = (struct event_data*)strdb_get(ev_db, evname))) {

			//Execute OnInit
			run_script(nd->u.scr.script, ev->pos, 0, nd->bl.id);

		}
	}

	return end;
}

/// Duplicate a warp, shop, cashshop or script. [Orcao]
/// warp: <map name>,<x>,<y>,<facing>%TAB%duplicate(<name of target>)%TAB%<NPC Name>%TAB%<spanx>,<spany>
/// shop/cashshop/npc: -%TAB%duplicate(<name of target>)%TAB%<NPC Name>%TAB%<sprite id>
/// shop/cashshop/npc: <map name>,<x>,<y>,<facing>%TAB%duplicate(<name of target>)%TAB%<NPC Name>%TAB%<sprite id>
/// npc: -%TAB%duplicate(<name of target>)%TAB%<NPC Name>%TAB%<sprite id>,<triggerX>,<triggerY>
/// npc: <map name>,<x>,<y>,<facing>%TAB%duplicate(<name of target>)%TAB%<NPC Name>%TAB%<sprite id>,<triggerX>,<triggerY>
const char* npc_parse_duplicate(char* w1, char* w2, char* w3, char* w4, const char* start, const char* buffer, const char* filepath){
	int x, y, dir, m, xs = -1, ys = -1, class_ = 0, episode_ident = 0, min_episode = 0, max_episode = 0;
	char mapname[32];
	char srcname[128];
	int i;
	const char* end;
	size_t length;

	int src_id;
	int type;
	struct npc_data* nd;
	struct npc_data* dnd;

	end = strchr(start,'\n');
	length = strlen(w2);

	// get the npc being duplicated
	if( w2[length-1] != ')' || length <= 11 || length-11 >= sizeof(srcname) )
	{// does not match 'duplicate(%127s)', name is empty or too long
		ShowError("npc_parse_script: bad duplicate name in file '%s', line '%d' : %s\n", filepath, strline(buffer,start-buffer), w2);
		return end;// next line, try to continue
	}
	safestrncpy(srcname, w2+10, length-10);

	sscanf(w4, "%d,%d,%d", &episode_ident, &min_episode, &max_episode);

	//Check Episode [15peaces]
	if(episode_ident != 1) {
		min_episode = 0;
		max_episode = 0;
	}
	else if(min_episode <= 0 && max_episode <= 0);
	else if(battle_config.feature_episode >= min_episode && max_episode == -1);
	else if(battle_config.feature_episode >= min_episode && battle_config.feature_episode <= max_episode);
	else
		return end;// next line, try to continue

	dnd = npc_name2id(srcname);
	if( dnd == NULL) {
		ShowError("npc_parse_script: original npc not found for duplicate in file '%s', line '%d' : %s\n", filepath, strline(buffer,start-buffer), srcname);
		return end;// next line, try to continue
	}
	src_id = dnd->bl.id;
	type = dnd->subtype;

	// get placement
	if( (type==NPCTYPE_SHOP || type==NPCTYPE_CASHSHOP || type == NPCTYPE_ITEMSHOP || type == NPCTYPE_POINTSHOP || type==NPCTYPE_SCRIPT) && strcmp(w1, "-") == 0 )
	{// floating shop/chashshop/itemshop/pointshop/script
		x = y = dir = 0;
		m = -1;
	}
	else
	{
		if( sscanf(w1, "%31[^,],%d,%d,%d", mapname, &x, &y, &dir) != 4 )// <map name>,<x>,<y>,<facing>
		{
			ShowError("npc_parse_duplicate: Invalid placement format for duplicate in file '%s', line '%d'. Skipping line...\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
			return end;// next line, try to continue
		}
		m = map_mapname2mapid(mapname);
	}

	nd = npc_create_npc(m, x, y);

	nd->u.scr.ep_min = 0;
	nd->u.scr.ep_max = 0;

	if( type == NPCTYPE_WARP && sscanf(w4, "%d,%d,%d,%d,%d", &episode_ident, &min_episode, &max_episode, &xs, &ys) == 5 );// <ep_identifier>,<min_ep>,<max_ep>,<spanx>,<spany>
	else if( type == NPCTYPE_SCRIPT && sscanf(w4, "%d,%d,%d,%d,%d,%d",&episode_ident, &min_episode, &max_episode, &class_, &xs, &ys) == 6);// <ep_identifier>,<min_ep>,<max_ep>,<sprite id>,<triggerX>,<triggerY>
	else if( type == NPCTYPE_SCRIPT && sscanf(w4, "%d,%d,%d,%d",&episode_ident, &min_episode, &max_episode, &class_) == 4);// <ep_identifier>,<min_ep>,<max_ep>,<sprite id>
	else if (type == NPCTYPE_SCRIPT && sscanf(w4, "%d,%d,%d", &class_, &xs, &ys) == 3);// <sprite id>,<triggerX>,<triggerY>
	else if( type != NPCTYPE_WARP ) class_ = atoi(w4);// <sprite id>
	else {
		ShowError("npc_parse_duplicate: Invalid span format for duplicate warp in file '%s', line '%d'. Skipping line...\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
		return end;// next line, try to continue
	}

	nd->u.scr.ep_min = min_episode;
	nd->u.scr.ep_max = max_episode;

	npc_parsename(nd, w3, start, buffer, filepath);
	nd->class_ = class_;
	nd->speed = 200;
	nd->src_id = src_id;
	nd->bl.type = BL_NPC;
	nd->subtype = (enum npc_subtype)type;
	switch( type )
	{
	case NPCTYPE_SCRIPT:
		++npc_script;
		nd->u.scr.xs = xs;
		nd->u.scr.ys = ys;
		nd->u.scr.script = dnd->u.scr.script;
		nd->u.scr.label_list = dnd->u.scr.label_list;
		nd->u.scr.label_list_num = dnd->u.scr.label_list_num;
		break;

	case NPCTYPE_SHOP:
	case NPCTYPE_CASHSHOP:
	case NPCTYPE_MARKETSHOP:
	case NPCTYPE_ITEMSHOP:
	case NPCTYPE_POINTSHOP:
		++npc_shop;
		nd->u.shop.shop_item = dnd->u.shop.shop_item;
		nd->u.shop.count = dnd->u.shop.count;
		break;

	case NPCTYPE_WARP:
		++npc_warp;
		if( !battle_config.warp_point_debug )
			nd->class_ = WARP_CLASS;
		else
			nd->class_ = WARP_DEBUG_CLASS;
		nd->u.warp.xs = xs;
		nd->u.warp.ys = ys;
		nd->u.warp.mapindex = dnd->u.warp.mapindex;
		nd->u.warp.x = dnd->u.warp.x;
		nd->u.warp.y = dnd->u.warp.y;
		nd->trigger_on_hidden = dnd->trigger_on_hidden;
		break;
	}

	//Add the npc to its location
	if( m >= 0)
	{
		map_addnpc(m, nd);
		status_change_init(&nd->bl);
		unit_dataset(&nd->bl);
		nd->ud.dir = dir;
		npc_setcells(nd);

		if (map_addblock(&nd->bl))
			return end;

		if( class_ >= 0 )
		{
			status_set_viewdata(&nd->bl, nd->class_);
			if (map[nd->bl.m].users)
				clif_spawn(&nd->bl);
		}
	}
	else
	{
		// we skip map_addnpc, but still add it to the list of ID's
		map_addiddb(&nd->bl);
	}
	strdb_put(npcname_db, nd->exname, nd);

	if( type != NPCTYPE_SCRIPT )
		return end;

	//-----------------------------------------
	// Loop through labels to export them as necessary
	for (i = 0; i < nd->u.scr.label_list_num; i++) {
		if (npc_event_export(nd, i)) {
			ShowWarning("npc_parse_duplicate : duplicate event %s::%s (%s)\n",
				nd->exname, nd->u.scr.label_list[i].name, filepath);
		}
		npc_timerevent_export(nd, i);
	}

	nd->u.scr.timerid = INVALID_TIMER;

	return end;
}

int npc_duplicate4instance(struct npc_data *snd, int m)
{
	char newname[NPC_NAME_LENGTH+1];

	if( map[m].instance_id == 0 )
		return 1;

	snprintf(newname, ARRAYLENGTH(newname), "dup_%d_%d", map[m].instance_id, snd->bl.id);
	if( npc_name2id(newname) != NULL )
	{ // Name already in use
		ShowError("npc_duplicate4instance: the npcname (%s) is already in use while trying to duplicate npc %s in instance %d.\n", newname, snd->exname, map[m].instance_id);
		return 1;
	}

	if( snd->subtype == NPCTYPE_WARP )
	{ // Adjust destination, if instanced
		struct npc_data *wnd = NULL; // New NPC
		int dm = map_mapindex2mapid(snd->u.warp.mapindex), im;
		if( dm < 0 ) return 1;

		im = instance_mapid2imapid(dm, map[m].instance_id);
		if( im == -1 )
		{
			ShowError("npc_duplicate4instance: warp (%s) leading to instanced map (%s), but instance map is not attached to current instance (%d).\n", map[dm].name, snd->exname, map[m].instance_id);
			return 1;
		}

		wnd = npc_create_npc(m, snd->bl.x, snd->bl.y);
		map_addnpc(m, wnd);
		safestrncpy(wnd->exname, newname, ARRAYLENGTH(wnd->exname));
		wnd->class_ = WARP_CLASS;
		wnd->speed = 200;
		wnd->u.warp.mapindex = map_id2index(im);
		wnd->u.warp.x = snd->u.warp.x;
		wnd->u.warp.y = snd->u.warp.y;
		wnd->u.warp.xs = snd->u.warp.xs;
		wnd->u.warp.ys = snd->u.warp.ys;
		wnd->bl.type = BL_NPC;
		wnd->subtype = NPCTYPE_WARP;
		wnd->trigger_on_hidden = snd->trigger_on_hidden;
		npc_setcells(wnd);

		if (map_addblock(&wnd->bl))
			return 1;

		status_set_viewdata(&wnd->bl, wnd->class_);
		status_change_init(&wnd->bl);
		unit_dataset(&wnd->bl);
		if (map[wnd->bl.m].users)
			clif_spawn(&wnd->bl);
		strdb_put(npcname_db, wnd->exname, wnd);
	}
	else
	{
		static char w1[50], w2[50], w3[50], w4[50];
		const char* stat_buf = "- call from instancing subsystem -\n";

		snprintf(w1, sizeof(w1), "%s,%d,%d,%d", map[m].name, snd->bl.x, snd->bl.y, snd->ud.dir);
		snprintf(w2, sizeof(w2), "duplicate(%s)", snd->exname);
		snprintf(w3, sizeof(w3), "%s::%s", snd->name, newname);

		if( snd->u.scr.xs >= 0 && snd->u.scr.ys >= 0 )
			snprintf(w4, sizeof(w4), "%d,%d,%d", snd->class_, snd->u.scr.xs, snd->u.scr.ys); // Touch Area
		else
			snprintf(w4, sizeof(w4), "%d", snd->class_);

		npc_parse_duplicate(w1, w2, w3, w4, stat_buf, stat_buf, "INSTANCING");
	}

	return 0;
}

int npc_instanceinit(struct npc_data* nd)
{
	struct event_data *ev;
	char evname[EVENT_NAME_LENGTH];

	snprintf(evname, ARRAYLENGTH(evname), "%s::OnInstanceInit", nd->exname);

	if( ( ev = (struct event_data*)strdb_get(ev_db, evname) ) )
		run_script(nd->u.scr.script,ev->pos,0,nd->bl.id);

	return 0;
}

void npc_setcells(struct npc_data* nd)
{
	int m = nd->bl.m, x = nd->bl.x, y = nd->bl.y, xs, ys;
	int i,j;

	switch(nd->subtype)
	{
	case NPCTYPE_WARP:
		xs = nd->u.warp.xs;
		ys = nd->u.warp.ys;
		break;
	case NPCTYPE_SCRIPT:
		xs = nd->u.scr.xs;
		ys = nd->u.scr.ys;
		break;
	default:
		return; // Other types doesn't have touch area
	}

	if (m < 0 || xs < 0 || ys < 0)
		return;

	for (i = y-ys; i <= y+ys; i++) {
		for (j = x-xs; j <= x+xs; j++) {
			if (map_getcell(m, j, i, CELL_CHKNOPASS))
				continue;
			map_setcell(m, j, i, CELL_NPC, true);
		}
	}
}

int npc_unsetcells_sub(struct block_list* bl, va_list ap)
{
	struct npc_data *nd = (struct npc_data*)bl;
	int id =  va_arg(ap,int);
	if (nd->bl.id == id) return 0;
	npc_setcells(nd);
	return 1;
}

void npc_unsetcells(struct npc_data* nd)
{
	int m = nd->bl.m, x = nd->bl.x, y = nd->bl.y, xs, ys;
	int i,j, x0, x1, y0, y1;

	if (nd->subtype == NPCTYPE_WARP) {
		xs = nd->u.warp.xs;
		ys = nd->u.warp.ys;
	} else {
		xs = nd->u.scr.xs;
		ys = nd->u.scr.ys;
	}

	if (m < 0 || xs < 0 || ys < 0)
		return;

	//Locate max range on which we can locate npc cells
	//FIXME: does this really do what it's supposed to do? [ultramage]
	for(x0 = x-xs; x0 > 0 && map_getcell(m, x0, y, CELL_CHKNPC); x0--);
	for(x1 = x+xs; x1 < map[m].xs-1 && map_getcell(m, x1, y, CELL_CHKNPC); x1++);
	for(y0 = y-ys; y0 > 0 && map_getcell(m, x, y0, CELL_CHKNPC); y0--);
	for(y1 = y+ys; y1 < map[m].ys-1 && map_getcell(m, x, y1, CELL_CHKNPC); y1++);

	//Erase this npc's cells
	for (i = y-ys; i <= y+ys; i++)
		for (j = x-xs; j <= x+xs; j++)
			map_setcell(m, j, i, CELL_NPC, false);

	//Re-deploy NPC cells for other nearby npcs.
	map_foreachinarea( npc_unsetcells_sub, m, x0, y0, x1, y1, BL_NPC, nd->bl.id );
}

void npc_movenpc(struct npc_data* nd, int x, int y)
{
	const int m = nd->bl.m;
	if (m < 0 || nd->bl.prev == NULL) return;	//Not on a map.

	x = cap_value(x, 0, map[m].xs-1);
	y = cap_value(y, 0, map[m].ys-1);

	map_foreachinrange(clif_outsight, &nd->bl, AREA_SIZE, BL_PC, &nd->bl);
	map_moveblock(&nd->bl, x, y, gettick());
	map_foreachinrange(clif_insight, &nd->bl, AREA_SIZE, BL_PC, &nd->bl);
}

/// Changes the display name of the npc.
///
/// @param nd Target npc
/// @param newname New display name
void npc_setdisplayname(struct npc_data* nd, const char* newname)
{
	nullpo_retv(nd);

	safestrncpy(nd->name, newname, sizeof(nd->name));
	if (map[nd->bl.m].users)
		clif_name_area(&nd->bl);
}

/// Changes the display class of the npc.
///
/// @param nd Target npc
/// @param class_ New display class
void npc_setclass(struct npc_data* nd, short class_)
{
	nullpo_retv(nd);

	if( nd->class_ == class_ )
		return;

	if (map[nd->bl.m].users)
		clif_clearunit_area(&nd->bl, CLR_OUTSIGHT);// fade out
	nd->class_ = class_;
	status_set_viewdata(&nd->bl, class_);
	if (map[nd->bl.m].users)
		clif_spawn(&nd->bl);// fade in
}

// @commands (script based) 
int npc_do_atcmd_event(struct map_session_data *sd, const char *command, const char *message, const char *eventname)
{
	struct event_data *ev = (struct event_data *)strdb_get(ev_db, eventname);
	struct npc_data *nd;
	struct script_state *st;
	int i = 0, j = 0, k = 0;
	char *temp;

	nullpo_ret(sd);

	if(ev == NULL || (nd = ev->nd) == NULL) {
		ShowError("npc_event: event not found [%s]\n", eventname);
		return 0;
	}

	if(sd->npc_id != 0) {   // Enqueue the event trigger.
		int i;
		ARR_FIND(0, MAX_EVENTQUEUE, i, sd->eventqueue[i][0] == '\0');
		if(i < MAX_EVENTQUEUE) {
			safestrncpy(sd->eventqueue[i],eventname,50); //Event enqueued.
			return 0;
		}

		ShowWarning("npc_event: player's event queue is full, can't add event '%s' !\n", eventname);
		return 1;
	}

	if(ev->nd->sc.option&OPTION_INVISIBLE) {   // Disabled npc, shouldn't trigger event.
		npc_event_dequeue(sd);
		return 2;
	}

	st = script_alloc_state(ev->nd->u.scr.script, ev->pos, sd->bl.id, ev->nd->bl.id);
	setd_sub(st, NULL, ".@atcmd_command$", 0, (void *)command, NULL);

	// split atcmd parameters based on spaces

	temp = (char *)aMalloc(strlen(message) + 1);

	for(i = 0; i < (strlen(message) + 1 ) && k < 127; i ++) {
		if( message[i] == ' ' || message[i] == '\0') {
			if(message[ (i - 1) ] == ' ') {
				continue; // To prevent "@atcmd [space][space][space]" and .@atcmd_numparameters return 1 without any parameter.
			}
			temp[k] = '\0';
			k = 0;
			if( temp[0] != '\0' ) {
			setd_sub(st, NULL, ".@atcmd_parameters$", j++, (void *)temp, NULL);
			}
		} else {
			temp[k] = message[i];
			k++;
		}
	 }

	setd_sub(st, NULL, ".@atcmd_numparameters", 0, (void *)__64BPRTSIZE(j), NULL);
	 aFree(temp);

	run_script_main(st);
	return 0;
}

/// Parses a function.
/// function%TAB%script%TAB%<function name>%TAB%{<code>}
static const char* npc_parse_function(char* w1, char* w2, char* w3, char* w4, const char* start, const char* buffer, const char* filepath)
{
	DBMap* func_db;
	DBData old_data;
	struct script_code *script;
	const char* end;
	const char* script_start;

	script_start = strstr(start,"\t{");
	end = strchr(start,'\n');
	if( *w4 != '{' || script_start == NULL || (end != NULL && script_start > end) )
	{
		ShowError("npc_parse_function: Missing left curly '%%TAB%%{' in file '%s', line '%d'. Skipping the rest of the file.\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
		return NULL;// can't continue
	}
	++script_start;

	end = npc_skip_script(script_start,buffer,filepath);
	if( end == NULL )
		return NULL;// (simple) parse error, don't continue

	script = parse_script(script_start, filepath, strline(buffer,start-buffer), SCRIPT_RETURN_EMPTY_SCRIPT);
	if( script == NULL )// parse error, continue
		return end;

	func_db = script_get_userfunc_db();
	if (func_db->put(func_db, db_str2key(w3), db_ptr2data(script), &old_data))
	{
		struct script_code *oldscript = db_data2ptr(&old_data);
		ShowInfo("npc_parse_function: Overwriting user function [%s] (%s:%d)\n", w3, filepath, strline(buffer,start-buffer));
		script_free_vars(oldscript->script_vars);
		aFree(oldscript->script_buf);
		aFree(oldscript);
	}

	return end;
}


/*==========================================
 * Parse Mob 1 - Parse mob list into each map
 * Parse Mob 2 - Actually Spawns Mob
 * [Wizputer]
 *------------------------------------------*/
void npc_parse_mob2(struct spawn_data* mob)
{
	int i;

	for( i = mob->active; i < mob->num; ++i )
	{
		struct mob_data* md = mob_spawn_dataset(mob);
		md->spawn = mob;
		md->spawn->active++;
		mob_spawn(md);
	}
}

static const char* npc_parse_mob(char* w1, char* w2, char* w3, char* w4, const char* start, const char* buffer, const char* filepath)
{
	int num, class_, m,x,y,xs,ys, i,j;
	char mapname[32];
	int episode_ident = 0, min_episode = 0, max_episode = 0;
	int ai = -1, size = -1;
	struct spawn_data mob, *data;
	struct mob_db* db;

	memset(&mob, 0, sizeof(struct spawn_data));

	mob.state.boss = !strcmpi(w2,"boss_monster");

	// w1=<map name>,<x>,<y>,<xs>,<ys>{,<episode_flag>,<min_episode>,<max_episode>}
	// w4=<mob id>,<amount>,<delay1>,<delay2>,<event>{,<mob size>,<mob ai>}
	if( sscanf(w1, "%31[^,],%d,%d,%d,%d,%d,%d,%d", mapname, &x, &y, &xs, &ys, &episode_ident, &min_episode, &max_episode) == 8);
	else if( sscanf(w1, "%31[^,],%d,%d,%d,%d", mapname, &x, &y, &xs, &ys) == 5);
	else {
		ShowError("npc_parse_mob: Invalid mob definition in file '%s', line '%d'.\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
		return strchr(start,'\n');// skip and continue
	}

	if(sscanf(w4, "%d,%d,%u,%u,%127[^,],%d,%d[^\t\r\n]", &class_, &num, &mob.delay1, &mob.delay2, mob.eventname, &size, &ai) < 2) {
		ShowError("npc_parse_mob: Invalid mob definition in file '%s', line '%d'.\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
		return strchr(start,'\n');// skip and continue
	}

	if( mapindex_name2id(mapname) == 0 )
	{
		ShowError("npc_parse_mob: Unknown map '%s' in file '%s', line '%d'.\n", mapname, filepath, strline(buffer,start-buffer));
		return strchr(start,'\n');// skip and continue
	}
	m =  map_mapname2mapid(mapname);
	if( m < 0 )//Not loaded on this map-server instance.
		return strchr(start,'\n');// skip and continue
	mob.m = (unsigned short)m;

	if( x < 0 || x >= map[mob.m].xs || y < 0 || y >= map[mob.m].ys )
	{
		ShowError("npc_parse_mob: Spawn coordinates out of range: %s (%d,%d), map size is (%d,%d) - %s %s (file '%s', line '%d').\n", map[mob.m].name, x, y, (map[mob.m].xs-1), (map[mob.m].ys-1), w1, w3, filepath, strline(buffer,start-buffer));
		return strchr(start,'\n');// skip and continue
	}

	//Check Episode [15peaces]
	if(min_episode <= 0 && max_episode <= 0);
	else if(battle_config.feature_episode >= min_episode && max_episode == -1);
	else if(battle_config.feature_episode >= min_episode && battle_config.feature_episode <= max_episode);
	else
		return strchr(start, '\n'); // skip and continue

	// check monster ID if exists!
	if( mobdb_checkid(class_) == 0 )
	{
		ShowError("npc_parse_mob: Unknown mob ID %d (file '%s', line '%d').\n", class_, filepath, strline(buffer,start-buffer));
		return strchr(start,'\n');// skip and continue
	}

	if( num < 1 || num > 1000 )
	{
		ShowError("npc_parse_mob: Invalid number of monsters %d, must be inside the range [1,1000] (file '%s', line '%d').\n", num, filepath, strline(buffer,start-buffer));
		return strchr(start,'\n');// skip and continue
	}


	if ((mob.state.size < 0 || mob.state.size > 2) && size != -1)
	{
		ShowError("npc_parse_mob: Invalid size number %d for mob ID %d (file '%s', line '%d').\n", mob.state.size, class_, filepath, strline(buffer, start - buffer));
		return strchr(start, '\n');
	}

	if ((mob.state.ai < 0 || mob.state.ai > 6) && ai != -1)
	{
		ShowError("npc_parse_mob: Invalid ai %d for mob ID %d (file '%s', line '%d').\n", mob.state.ai, class_, filepath, strline(buffer, start - buffer));
		return strchr(start, '\n');
	}


	mob.num = (unsigned short)num;
	mob.active = 0;
	mob.class_ = (short) class_;
	mob.x = (unsigned short)x;
	mob.y = (unsigned short)y;
	mob.xs = (signed short)xs;
	mob.ys = (signed short)ys;
	if (size > 0 && size <= 2)
		mob.state.size = size;
	if (ai > 0 && ai <= 6)
		mob.state.ai = ai;

	if (mob.num > 1 && battle_config.mob_count_rate != 100) {
		if ((mob.num = mob.num * battle_config.mob_count_rate / 100) < 1)
			mob.num = 1;
	}

	if (battle_config.force_random_spawn || (mob.x == 0 && mob.y == 0))
	{	//Force a random spawn anywhere on the map.
		mob.x = mob.y = 0;
		mob.xs = mob.ys = -1;
	}

	// Check if monsters should have variance applied to their respawn time
	if (((battle_config.mob_spawn_variance & 1) == 0 && mob.state.boss) || ((battle_config.mob_spawn_variance & 2) == 0 && !mob.state.boss)) {
		// Remove the variance
		mob.delay2 = 0;
	}

	if(mob.delay1>0xfffffff || mob.delay2>0xfffffff) {
		ShowError("npc_parse_mob: Invalid spawn delays %u %u (file '%s', line '%d').\n", mob.delay1, mob.delay2, filepath, strline(buffer,start-buffer));
		return strchr(start,'\n');// skip and continue
	}

	//Use db names instead of the spawn file ones.
	if(battle_config.override_mob_names==1)
		strcpy(mob.name,"--en--");
	else if (battle_config.override_mob_names==2)
		strcpy(mob.name,"--ja--");
	else
		safestrncpy(mob.name, w3, sizeof(mob.name));

	//Verify dataset.
	if( !mob_parse_dataset(&mob) )
	{
		ShowError("npc_parse_mob: Invalid dataset for monster ID %d (file '%s', line '%d').\n", class_, filepath, strline(buffer,start-buffer));
		return strchr(start,'\n');// skip and continue
	}

	//Update mob spawn lookup database
	db = mob_db(class_);
	for( i = 0; i < ARRAYLENGTH(db->spawn); ++i )
	{
		if (map[mob.m].index == db->spawn[i].mapindex)
		{	//Update total
			db->spawn[i].qty += mob.num;
			//Re-sort list
			for( j = i; j > 0 && db->spawn[j-1].qty < db->spawn[i].qty; --j );
			if( j != i )
			{
				xs = db->spawn[i].mapindex;
				ys = db->spawn[i].qty;
				memmove(&db->spawn[j+1], &db->spawn[j], (i-j)*sizeof(db->spawn[0]));
				db->spawn[j].mapindex = xs;
				db->spawn[j].qty = ys;
			}
			break;
		}
		if (mob.num > db->spawn[i].qty)
		{	//Insert into list
			memmove(&db->spawn[i+1], &db->spawn[i], sizeof(db->spawn) -(i+1)*sizeof(db->spawn[0]));
			db->spawn[i].mapindex = map[mob.m].index;
			db->spawn[i].qty = mob.num;
			break;
		}
	}

	//Now that all has been validated. We allocate the actual memory that the re-spawn data will use.
	data = (struct spawn_data*)aMalloc(sizeof(struct spawn_data));
	memcpy(data, &mob, sizeof(struct spawn_data));

	// spawn / cache the new mobs
	if( battle_config.dynamic_mobs && map_addmobtolist(data->m, data) >= 0 )
	{
		data->state.dynamic = true;
		npc_cache_mob += data->num;

		// check if target map has players
		// (usually shouldn't occur when map server is just starting,
		// but not the case when we do @reloadscript
		if( map[data->m].users > 0 )
			npc_parse_mob2(data);
	}
	else
	{
		data->state.dynamic = false;
		npc_parse_mob2(data);
		npc_delay_mob += data->num;
	}

	npc_mob++;

	return strchr(start,'\n');// continue
}

/*==========================================
 * マップフラグ行の解析
 *------------------------------------------*/
static const char* npc_parse_mapflag(char* w1, char* w2, char* w3, char* w4, const char* start, const char* buffer, const char* filepath)
{
	int m;
	char mapname[32];
	int state = 1;

	// w1=<mapname>
	if( sscanf(w1, "%31[^,]", mapname) != 1 )
	{
		ShowError("npc_parse_mapflag: Invalid mapflag definition in file '%s', line '%d'.\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
		return strchr(start,'\n');// skip and continue
	}
	m = map_mapname2mapid(mapname);
	if( m < 0 )
	{
		ShowWarning("npc_parse_mapflag: Unknown map in file '%s', line '%d' : %s\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", mapname, filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
		return strchr(start,'\n');// skip and continue
	}

	if (w4 && !strcmpi(w4, "off"))
		state = 0;	//Disable mapflag rather than enable it. [Skotlex]
	
	if (!strcmpi(w3, "nosave")) {
		char savemap[32];
		int savex, savey;
		if (state == 0)
			; //Map flag disabled.
		else if (!strcmpi(w4, "SavePoint")) {
			map[m].save.map = 0;
			map[m].save.x = -1;
			map[m].save.y = -1;
		} else if (sscanf(w4, "%31[^,],%d,%d", savemap, &savex, &savey) == 3) {
			map[m].save.map = mapindex_name2id(savemap);
			map[m].save.x = savex;
			map[m].save.y = savey;
			if (!map[m].save.map) {
				ShowWarning("npc_parse_mapflag: Specified save point map '%s' for mapflag 'nosave' not found (file '%s', line '%d'), using 'SavePoint'.\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", savemap, filepath, strline(buffer,start-buffer), w1, w2, w3, w4);
				map[m].save.x = -1;
				map[m].save.y = -1;
			}
		}
		map[m].flag.nosave = state;
	}
	else if (!strcmpi(w3,"autotrade"))
		map[m].flag.autotrade=state;
	else if (!strcmpi(w3,"allowks"))
		map[m].flag.allowks=state; // [Kill Steal Protection]
	else if (!strcmpi(w3,"town"))
		map[m].flag.town=state;
	else if (!strcmpi(w3,"nomemo"))
		map[m].flag.nomemo=state;
	else if (!strcmpi(w3,"noteleport"))
		map[m].flag.noteleport=state;
	else if (!strcmpi(w3,"nowarp"))
		map[m].flag.nowarp=state;
	else if (!strcmpi(w3,"nowarpto"))
		map[m].flag.nowarpto=state;
	else if (!strcmpi(w3,"noreturn"))
		map[m].flag.noreturn=state;
	else if (!strcmpi(w3,"monster_noteleport"))
		map[m].flag.monster_noteleport=state;
	else if (!strcmpi(w3,"nobranch"))
		map[m].flag.nobranch=state;
	else if (!strcmpi(w3,"nopenalty")) {
		map[m].flag.noexppenalty=state;
		map[m].flag.nozenypenalty=state;
	}
	else if (!strcmpi(w3,"pvp")) {
		map[m].flag.pvp = state;
		if( state && (map[m].flag.gvg || map[m].flag.gvg_dungeon || map[m].flag.gvg_castle) )
		{
			map[m].flag.gvg = 0;
			map[m].flag.gvg_dungeon = 0;
			map[m].flag.gvg_castle = 0;
			ShowWarning("npc_parse_mapflag: You can't set PvP and GvG flags for the same map! Removing GvG flags from %s (file '%s', line '%d').\n", map[m].name, filepath, strline(buffer,start-buffer));
		}
		if( state && map[m].flag.battleground )
		{
			map[m].flag.battleground = 0;
			ShowWarning("npc_parse_mapflag: You can't set GvG and BattleGround flags for the same map! Removing BattleGround flag from %s (file '%s', line '%d').\n", map[m].name, filepath, strline(buffer,start-buffer));
		}
	}
	else if (!strcmpi(w3,"pvp_noparty"))
		map[m].flag.pvp_noparty=state;
	else if (!strcmpi(w3,"pvp_noguild"))
		map[m].flag.pvp_noguild=state;
	else if (!strcmpi(w3, "pvp_nightmaredrop")) {
		char drop_arg1[16], drop_arg2[16];
		int drop_per = 0;
		if (sscanf(w4, "%[^,],%[^,],%d", drop_arg1, drop_arg2, &drop_per) == 3) {
			int drop_id = 0, drop_type = 0;
			if (!strcmpi(drop_arg1, "random"))
				drop_id = -1;
			else if (itemdb_exists((strtol(drop_arg1, NULL, 10))) == NULL)
				drop_id = 0;
			if (!strcmpi(drop_arg2, "inventory"))
				drop_type = 1;
			else if (!strcmpi(drop_arg2,"equip"))
				drop_type = 2;
			else if (!strcmpi(drop_arg2,"all"))
				drop_type = 3;

			if (drop_id != 0){
				int i;
				for (i = 0; i < MAX_DROP_PER_MAP; i++) {
					if (map[m].drop_list[i].drop_id == 0){
						map[m].drop_list[i].drop_id = drop_id;
						map[m].drop_list[i].drop_type = drop_type;
						map[m].drop_list[i].drop_per = drop_per;
						break;
					}
				}
				map[m].flag.pvp_nightmaredrop = 1;
			}
		} else if (!state) //Disable
			map[m].flag.pvp_nightmaredrop = 0;
	}
	else if (!strcmpi(w3,"pvp_nocalcrank"))
		map[m].flag.pvp_nocalcrank=state;
	else if (!strcmpi(w3,"gvg")) {
		map[m].flag.gvg = state;
		if( state && map[m].flag.pvp )
		{
			map[m].flag.pvp = 0;
			ShowWarning("npc_parse_mapflag: You can't set PvP and GvG flags for the same map! Removing PvP flag from %s (file '%s', line '%d').\n", map[m].name, filepath, strline(buffer,start-buffer));
		}
		if( state && map[m].flag.battleground )
		{
			map[m].flag.battleground = 0;
			ShowWarning("npc_parse_mapflag: You can't set PvP and BattleGround flags for the same map! Removing BattleGround flag from %s (file '%s', line '%d').\n", map[m].name, filepath, strline(buffer,start-buffer));
		}
	}
	else if (!strcmpi(w3,"gvg_noparty"))
		map[m].flag.gvg_noparty=state;
	else if (!strcmpi(w3,"gvg_dungeon")) {
		map[m].flag.gvg_dungeon=state;
		if (state) map[m].flag.pvp=0;
	}
	else if (!strcmpi(w3,"gvg_castle")) {
		map[m].flag.gvg_castle=state;
		if (state) map[m].flag.pvp=0;
	}
	else if (!strcmpi(w3,"gvg_te")) {
		map[m].flag.gvg_te = state;
		if( state && map[m].flag.pvp ) {
			map[m].flag.pvp = 0;
			ShowWarning("npc_parse_mapflag: You can't set PvP and GvG flags for the same map! Removing PvP flag from %s (file '%s', line '%d').\n", map[m].name, filepath, strline(buffer,start-buffer));
		}
		if( state && map[m].flag.battleground ) {
			map[m].flag.battleground = 0;
			ShowWarning("npc_parse_mapflag: You can't set GvG and BattleGround flags for the same map! Removing BattleGround flag from %s (file '%s', line '%d').\n", map[m].name, filepath, strline(buffer,start-buffer));
		}
	}
	else if (!strcmpi(w3,"gvg_te_castle")) {
		map[m].flag.gvg_te_castle = state;
		if (state) {
			map[m].flag.gvg_castle = 0;
			map[m].flag.pvp = 0;
		}
	}
	else if (!strcmpi(w3,"battleground")) {
		if( state )
		{
			if( sscanf(w4, "%d", &state) == 1 )
				map[m].flag.battleground = state;
			else
				map[m].flag.battleground = 1; // Default value
		}
		else
			map[m].flag.battleground = 0;

		if( map[m].flag.battleground && map[m].flag.pvp )
		{
			map[m].flag.pvp = 0;
			ShowWarning("npc_parse_mapflag: You can't set PvP and BattleGround flags for the same map! Removing PvP flag from %s (file '%s', line '%d').\n", map[m].name, filepath, strline(buffer,start-buffer));
		}
		if( map[m].flag.battleground && (map[m].flag.gvg || map[m].flag.gvg_dungeon || map[m].flag.gvg_castle) )
		{
			map[m].flag.gvg = 0;
			map[m].flag.gvg_dungeon = 0;
			map[m].flag.gvg_castle = 0;
			ShowWarning("npc_parse_mapflag: You can't set GvG and BattleGround flags for the same map! Removing GvG flag from %s (file '%s', line '%d').\n", map[m].name, filepath, strline(buffer,start-buffer));
		}
	}
	else if (!strcmpi(w3,"noexppenalty"))
		map[m].flag.noexppenalty=state;
	else if (!strcmpi(w3,"nozenypenalty"))
		map[m].flag.nozenypenalty=state;
	else if (!strcmpi(w3,"notrade"))
		map[m].flag.notrade=state;
	else if (!strcmpi(w3,"novending"))
		map[m].flag.novending=state;
	else if (!strcmpi(w3,"nodrop"))
		map[m].flag.nodrop=state;
	else if (!strcmpi(w3,"noskill"))
		map[m].flag.noskill=state;
	else if (!strcmpi(w3,"noicewall"))
		map[m].flag.noicewall=state;
	else if (!strcmpi(w3,"snow"))
		map[m].flag.snow=state;
	else if (!strcmpi(w3,"clouds"))
		map[m].flag.clouds=state;
	else if (!strcmpi(w3,"clouds2"))
		map[m].flag.clouds2=state;
	else if (!strcmpi(w3,"fog"))
		map[m].flag.fog=state;
	else if (!strcmpi(w3,"fireworks"))
		map[m].flag.fireworks=state;
	else if (!strcmpi(w3,"sakura"))
		map[m].flag.sakura=state;
	else if (!strcmpi(w3,"leaves"))
		map[m].flag.leaves=state;
	else if (!strcmpi(w3,"rain"))
		map[m].flag.rain=state;
	else if (!strcmpi(w3,"nightenabled"))
		map[m].flag.nightenabled=state;
	else if (!strcmpi(w3,"nogo"))
		map[m].flag.nogo=state;
	else if (!strcmpi(w3,"noexp")) {
		map[m].flag.nobaseexp=state;
		map[m].flag.nojobexp=state;
	}
	else if (!strcmpi(w3,"nobaseexp"))
		map[m].flag.nobaseexp=state;
	else if (!strcmpi(w3,"nojobexp"))
		map[m].flag.nojobexp=state;
	else if (!strcmpi(w3,"noloot")) {
		map[m].flag.nomobloot=state;
		map[m].flag.nomvploot=state;
	}
	else if (!strcmpi(w3,"nomobloot"))
		map[m].flag.nomobloot=state;
	else if (!strcmpi(w3,"nomvploot"))
		map[m].flag.nomvploot=state;
	else if (!strcmpi(w3,"nocommand")) {
		if (state) {
			if (sscanf(w4, "%d", &state) == 1)
				map[m].nocommand =state;
			else //No level specified, block everyone.
				map[m].nocommand =100;
		} else
			map[m].nocommand=0;
	}
	else if (!strcmpi(w3,"restricted")) {
		if (state) {
			map[m].flag.restricted=1;
			sscanf(w4, "%d", &state);
			map[m].zone |= 1<<(state+1);
		} else {
			map[m].flag.restricted=0;
			map[m].zone = 0;
		}
	}
	else if (!strcmpi(w3,"jexp")) {
		map[m].jexp = (state) ? atoi(w4) : 100;
		if( map[m].jexp < 0 ) map[m].jexp = 100;
		map[m].flag.nojobexp = (map[m].jexp==0)?1:0;
	}
	else if (!strcmpi(w3,"bexp")) {
		map[m].bexp = (state) ? atoi(w4) : 100;
		if( map[m].bexp < 0 ) map[m].bexp = 100;
		 map[m].flag.nobaseexp = (map[m].bexp==0)?1:0;
	}
	else if (!strcmpi(w3,"loadevent"))
		map[m].flag.loadevent=state;
	else if (!strcmpi(w3,"nochat"))
		map[m].flag.nochat=state;
	else if (!strcmpi(w3,"partylock"))
		map[m].flag.partylock=state;
	else if (!strcmpi(w3,"guildlock"))
		map[m].flag.guildlock=state;
	else if (!strcmpi(w3,"reset"))
		map[m].flag.reset=state;
	else if ( !strcmpi(w3,"src4instance") )
		map[m].flag.src4instance = (state) ? 1 : 0;
	else if (!strcmpi(w3, "nosunmoonstarmiracle"))
		map[m].flag.nosunmoonstarmiracle = state;
	else if (!strcmpi(w3, "pairship_startable"))
		map[m].flag.pairship_startable = (state) ? 1 : 0;
	else if (!strcmpi(w3, "pairship_endable"))
		map[m].flag.pairship_endable = (state) ? 1 : 0;
	else if (!strcmpi(w3, "notomb"))
		map[m].flag.notomb = (state) ? 1 : 0;
	else if (!strcmpi(w3, "nocostume"))
		map[m].flag.nocostume = state;
	else if (!strcmpi(w3, "hidemobhpbar"))
		map[m].flag.hidemobhpbar = state;
	else
		ShowError("npc_parse_mapflag: unrecognized mapflag '%s' (file '%s', line '%d').\n", w3, filepath, strline(buffer,start-buffer));

	return strchr(start,'\n');// continue
}

void npc_parsesrcfile(const char* filepath, bool runOnInit)
{
	int m, lines = 0;
	FILE* fp;
	size_t len;
	char* buffer;
	const char* p;

	// read whole file to buffer
	fp = fopen(filepath, "rb");
	if( fp == NULL )
	{
		ShowError("npc_parsesrcfile: File not found '%s'.\n", filepath);
		return;
	}
	len = filesize(fp);
	buffer = (char*)aMalloc(len+1);
	len = fread(buffer, sizeof(char), len, fp);
	buffer[len] = '\0';
	if( ferror(fp) )
	{
		ShowError("npc_parsesrcfile: Failed to read file '%s' - %s\n", filepath, strerror(errno));
		aFree(buffer);
		fclose(fp);
		return;
	}
	fclose(fp);

	// parse buffer
	for( p = skip_space(buffer); p && *p ; p = skip_space(p) )
	{
		int pos[9];
		char w1[2048], w2[2048], w3[2048], w4[2048];
		int i, count;
		lines++;

		// w1<TAB>w2<TAB>w3<TAB>w4
		count = sv_parse(p, len+buffer-p, 0, '\t', pos, ARRAYLENGTH(pos), (e_svopt)(SV_TERMINATE_LF|SV_TERMINATE_CRLF));
		if( count < 0 )
		{
			ShowError("npc_parsesrcfile: Parse error in file '%s', line '%d'. Stopping...\n", filepath, strline(buffer,p-buffer));
			break;
		}
		// fill w1
		if( pos[3]-pos[2] > ARRAYLENGTH(w1)-1 )
			ShowWarning("npc_parsesrcfile: w1 truncated, too much data (%d) in file '%s', line '%d'.\n", pos[3]-pos[2], filepath, strline(buffer,p-buffer));
		i = min(pos[3]-pos[2], ARRAYLENGTH(w1)-1);
		memcpy(w1, p+pos[2], i*sizeof(char));
		w1[i] = '\0';
		// fill w2
		if( pos[5]-pos[4] > ARRAYLENGTH(w2)-1 )
			ShowWarning("npc_parsesrcfile: w2 truncated, too much data (%d) in file '%s', line '%d'.\n", pos[5]-pos[4], filepath, strline(buffer,p-buffer));
		i = min(pos[5]-pos[4], ARRAYLENGTH(w2)-1);
		memcpy(w2, p+pos[4], i*sizeof(char));
		w2[i] = '\0';
		// fill w3
		if( pos[7]-pos[6] > ARRAYLENGTH(w3)-1 )
			ShowWarning("npc_parsesrcfile: w3 truncated, too much data (%d) in file '%s', line '%d'.\n", pos[7]-pos[6], filepath, strline(buffer,p-buffer));
		i = min(pos[7]-pos[6], ARRAYLENGTH(w3)-1);
		memcpy(w3, p+pos[6], i*sizeof(char));
		w3[i] = '\0';
		// fill w4 (to end of line)
		if( pos[1]-pos[8] > ARRAYLENGTH(w4)-1 )
			ShowWarning("npc_parsesrcfile: w4 truncated, too much data (%d) in file '%s', line '%d'.\n", pos[1]-pos[8], filepath, strline(buffer,p-buffer));
		if( pos[8] != -1 )
		{
			i = min(pos[1]-pos[8], ARRAYLENGTH(w4)-1);
			memcpy(w4, p+pos[8], i*sizeof(char));
			w4[i] = '\0';
		}
		else
			w4[0] = '\0';

		if( count < 3 )
		{// Unknown syntax
			ShowError("npc_parsesrcfile: Unknown syntax in file '%s', line '%d'. Stopping...\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,p-buffer), w1, w2, w3, w4);
			break;
		}

		if( strcmp(w1,"-") !=0 && strcasecmp(w1,"function") != 0 )
		{// w1 = <map name>,<x>,<y>,<facing>
			char mapname[2048];
			sscanf(w1,"%[^,]",mapname);
			if( !mapindex_name2id(mapname) )
			{// Incorrect map, we must skip the script info...
				ShowError("npc_parsesrcfile: Unknown map '%s' in file '%s', line '%d'. Skipping line...\n", mapname, filepath, strline(buffer,p-buffer));
				if( strcasecmp(w2,"script") == 0 && count > 3 )
				{
					if((p = npc_skip_script(p,buffer,filepath)) == NULL)
					{
						break;
					}
				}
				p = strchr(p,'\n');// next line
				continue;
			}
			m = map_mapname2mapid(mapname);
			if( m < 0 )
			{// "mapname" is not assigned to this server, we must skip the script info...
				if( strcasecmp(w2,"script") == 0 && count > 3 )
				{
					if((p = npc_skip_script(p,buffer,filepath)) == NULL)
					{
						break;
					}
				}
				p = strchr(p,'\n');// next line
				continue;
			}
		}

		if ((strcasecmp(w2, "warp") == 0 || strcasecmp(w2, "warp2") == 0) && count > 3)
		{
			p = npc_parse_warp(w1,w2,w3,w4, p, buffer, filepath);
		}
		else if( (!strcasecmp(w2,"shop") || !strcasecmp(w2,"cashshop") || !strcasecmp(w2,"marketshop") || !strcasecmp(w2, "itemshop") || !strcasecmp(w2, "pointshop") ) && count > 3 )
		{
			p = npc_parse_shop(w1,w2,w3,w4, p, buffer, filepath);
		}
		else if( strcasecmp(w2,"script") == 0 && count > 3 )
		{
			if( strcasecmp(w1,"function") == 0 )
				p = npc_parse_function(w1, w2, w3, w4, p, buffer, filepath);
			else
				p = npc_parse_script(w1,w2,w3,w4, p, buffer, filepath, runOnInit);
		}
		else if( (i=0, sscanf(w2,"duplicate%n",&i), (i > 0 && w2[i] == '(')) && count > 3 )
		{
			p = npc_parse_duplicate(w1,w2,w3,w4, p, buffer, filepath);
		}
		else if( (strcmpi(w2,"monster") == 0 || strcmpi(w2,"boss_monster") == 0) && count > 3 )
		{
			p = npc_parse_mob(w1, w2, w3, w4, p, buffer, filepath);
		}
		else if( strcmpi(w2,"mapflag") == 0 && count >= 3 )
		{
			p = npc_parse_mapflag(w1, w2, trim(w3), trim(w4), p, buffer, filepath);
		}
		else
		{
			ShowError("npc_parsesrcfile: Unable to parse, probably a missing or extra TAB in file '%s', line '%d'. Skipping line...\n * w1=%s\n * w2=%s\n * w3=%s\n * w4=%s\n", filepath, strline(buffer,p-buffer), w1, w2, w3, w4);
			p = strchr(p,'\n');// skip and continue
		}
	}
	aFree(buffer);

	return;
}

int npc_script_event(struct map_session_data* sd, enum npce_event type)
{
	int i;
	if (type == NPCE_MAX)
		return 0;
	if (!sd) {
		ShowError("npc_script_event: NULL sd. Event Type %d\n", type);
		return 0;
	}
	for (i = 0; i<script_event[type].event_count; i++)
		npc_event_sub(sd,script_event[type].event[i],script_event[type].event_name[i]);
	return i;
}

void npc_read_event_script(void)
{
	int i;
	struct {
		char *name;
		const char *event_name;
	} config[] = {
		{"Login Event",script_config.login_event_name},
		{"Logout Event",script_config.logout_event_name},
		{"Load Map Event",script_config.loadmap_event_name},
		{"Base LV Up Event",script_config.baselvup_event_name},
		{"Job LV Up Event",script_config.joblvup_event_name},
		{"Die Event",script_config.die_event_name},
		{"Kill PC Event",script_config.kill_pc_event_name},
		{"Kill NPC Event",script_config.kill_mob_event_name},
	};

	for (i = 0; i < NPCE_MAX; i++)
	{
		DBIterator* iter;
		DBKey key;
		DBData *data;

		char name[64]="::";
		safestrncpy(name+2,config[i].event_name,62);

		script_event[i].event_count = 0;
		iter = db_iterator(ev_db);
		for( data = iter->first(iter,&key); iter->exists(iter); data = iter->next(iter,&key) )
		{
			const char* p = key.str;
			struct event_data* ed = db_data2ptr(data);
			unsigned char count = script_event[i].event_count;

			if( count >= ARRAYLENGTH(script_event[i].event) )
			{
				ShowWarning("npc_read_event_script: too many occurences of event '%s'!\n", config[i].event_name);
				break;
			}
			
			if( (p=strchr(p,':')) && p && strcmpi(name,p)==0 )
			{
				script_event[i].event[count] = ed;
				script_event[i].event_name[count] = key.str;
				script_event[i].event_count++;
			}
		}
		dbi_destroy(iter);
	}

	if (battle_config.etc_log) {
		//Print summary.
		for (i = 0; i < NPCE_MAX; i++)
			ShowInfo("%s: %d '%s' events.\n", config[i].name, script_event[i].event_count, config[i].event_name);
	}
}

int npc_reload(void)
{
	struct npc_src_list *nsl;
	int m, i;
	int npc_new_min = npc_id;
	struct s_mapiterator* iter;
	struct block_list* bl;

	/* clear guild flag cache */
	guild_flags_clear();

	db_clear(npcname_db);
	db_clear(ev_db);

	//Remove all npcs/mobs. [Skotlex]

#if PACKETVER >= 20131223
	npc_market_fromsql();
#endif

	iter = mapit_geteachiddb();
	for( bl = (struct block_list*)mapit_first(iter); mapit_exists(iter); bl = (struct block_list*)mapit_next(iter) ) {
		switch(bl->type) {
		case BL_NPC:
			if( bl->id != fake_nd->bl.id )// don't remove fake_nd
				npc_unload((struct npc_data *)bl, false);
			break;
		case BL_MOB:
			unit_free(bl,CLR_OUTSIGHT);
			break;
		}
	}
	mapit_free(iter);

	if(battle_config.dynamic_mobs)
	{// dynamic check by [random]
		for (m = 0; m < map_num; m++) {
			for (i = 0; i < MAX_MOB_LIST_PER_MAP; i++) {
				if (map[m].moblist[i] != NULL) {
					aFree(map[m].moblist[i]);
					map[m].moblist[i] = NULL;
				}
				if( map[m].mob_delete_timer != INVALID_TIMER )
				{ // Mobs were removed anyway,so delete the timer [Inkfish]
					delete_timer(map[m].mob_delete_timer, map_removemobs_timer);
					map[m].mob_delete_timer = INVALID_TIMER;
				}
			}
		}
		if (map[m].npc_num > 0)
			ShowWarning("npc_reload: %d npcs weren't removed at map %s!\n", map[m].npc_num, map[m].name);
	}

	// clear mob spawn lookup index
	mob_clear_spawninfo();

	npc_warp = npc_shop = npc_script = 0;
	npc_mob = npc_cache_mob = npc_delay_mob = 0;

	// reset mapflags
	map_flags_init();

	//TODO: the following code is copy-pasted from do_init_npc(); clean it up
	// Reloading npcs now
	for (nsl = npc_src_files; nsl; nsl = nsl->next)
	{
		ShowStatus("Loading NPC file: %s"CL_CLL"\r", nsl->name);
		npc_parsesrcfile(nsl->name, false);
	}

	ShowInfo ("Done loading '"CL_WHITE"%d"CL_RESET"' NPCs:"CL_CLL"\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Warps\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Shops\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Scripts\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Spawn sets\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Mobs Cached\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Mobs Not Cached\n",
		npc_id - npc_new_min, npc_warp, npc_shop, npc_script, npc_mob, npc_cache_mob, npc_delay_mob);

	//Re-read the NPC Script Events cache.
	npc_read_event_script();

	/* refresh guild castle flags on all woe setups */
	npc_event_doall("OnAgitInit");
	npc_event_doall("OnAgitInit2");
	npc_event_doall("OnAgitInit3");

	//Execute the OnInit event for freshly loaded npcs. [Skotlex]
	ShowStatus("Event '"CL_WHITE"OnInit"CL_RESET"' executed with '"CL_WHITE"%d"CL_RESET"' NPCs.\n",npc_event_doall("OnInit"));
	
	do_reload_instance();

	// Execute rest of the startup events if connected to char-server. [Lance]
	if(!CheckForCharServer()){
		ShowStatus("Event '"CL_WHITE"OnInterIfInit"CL_RESET"' executed with '"CL_WHITE"%d"CL_RESET"' NPCs.\n", npc_event_doall("OnInterIfInit"));
		ShowStatus("Event '"CL_WHITE"OnInterIfInitOnce"CL_RESET"' executed with '"CL_WHITE"%d"CL_RESET"' NPCs.\n", npc_event_doall("OnInterIfInitOnce"));
	}

#if PACKETVER >= 20131223
	npc_market_checkall();
#endif
	return 0;
}

bool npc_unloadfile(const char* path) {
	DBIterator * iter = db_iterator(npcname_db);
	struct npc_data* nd = NULL;
	bool found = false;

	for (nd = dbi_first(iter); dbi_exists(iter); nd = dbi_next(iter)) {
		if (nd->path && strcasecmp(nd->path, path) == 0) {
			found = true;
			npc_unload_duplicates(nd);/* unload any npcs which could duplicate this but be in a different file */
			npc_unload(nd, true);
		}
	}

	dbi_destroy(iter);

	if (found) /* refresh event cache */
		npc_read_event_script();

	npc_delsrcfile(path);

	return found;
}

void do_clear_npc(void) {
	db_clear(npcname_db);
	db_clear(ev_db);
}

/*==========================================
 * Destructor
 *------------------------------------------*/
void do_final_npc(void)
{
	ev_db->destroy(ev_db, NULL);
	//There is no free function for npcname_db because at this point there shouldn't be any npcs left!
	//So if there is anything remaining, let the memory manager catch it and report it.
	npcname_db->destroy(npcname_db, NULL);
#ifndef TXT_ONLY
#if PACKETVER >= 20131223
	NPCMarketDB->destroy(NPCMarketDB, npc_market_free);
#endif
#endif
	ers_destroy(timer_event_ers);
	ers_destroy(npc_sc_display_ers);
	npc_clearsrcfile();
}

static void npc_debug_warps_sub(struct npc_data* nd)
{
	int m;
	if (nd->bl.type != BL_NPC || nd->subtype != NPCTYPE_WARP || nd->bl.m < 0)
		return;

	m = map_mapindex2mapid(nd->u.warp.mapindex);
	if (m < 0) return; //Warps to another map, nothing to do about it.
	if (nd->u.warp.x == 0 && nd->u.warp.y == 0) return; // random warp

	if (map_getcell(m, nd->u.warp.x, nd->u.warp.y, CELL_CHKNPC)) {
		ShowWarning("Warp %s at %s(%d,%d) warps directly on top of an area npc at %s(%d,%d)\n",
			nd->name,
			map[nd->bl.m].name, nd->bl.x, nd->bl.y,
			map[m].name, nd->u.warp.x, nd->u.warp.y
			);
	}
	if (map_getcell(m, nd->u.warp.x, nd->u.warp.y, CELL_CHKNOPASS)) {
		ShowWarning("Warp %s at %s(%d,%d) warps to a non-walkable tile at %s(%d,%d)\n",
			nd->name,
			map[nd->bl.m].name, nd->bl.x, nd->bl.y,
			map[m].name, nd->u.warp.x, nd->u.warp.y
			);
	}
}

static void npc_debug_warps(void)
{
	int m, i;
	for (m = 0; m < map_num; m++)
		for (i = 0; i < map[m].npc_num; i++)
			npc_debug_warps_sub(map[m].npc[i]);
}

/*==========================================
 * npc initialization
 *------------------------------------------*/
void do_init_npc(void)
{
	struct npc_src_list *file;
	int i;

	//Stock view data for normal npcs.
	memset(&npc_viewdb, 0, sizeof(npc_viewdb));
	memset(&npc_viewdb_2, 0, sizeof(npc_viewdb_2));
	npc_viewdb[0].class_ = INVISIBLE_CLASS; //Invisible class is stored here.
	for( i = 1; i < MAX_NPC_CLASS; i++ ) 
		npc_viewdb[i].class_ = i;
	for( i = NPC_CLASS_BASE_2; i < NPC_CLASS_MAX_2; i++ )
		npc_viewdb_2[i-NPC_CLASS_BASE_2].class_ = i;

	ev_db = strdb_alloc((DBOptions)(DB_OPT_DUP_KEY|DB_OPT_RELEASE_DATA),2*NAME_LENGTH+2+1);
	npcname_db = strdb_alloc(DB_OPT_BASE,NPC_NAME_LENGTH+1);

#if PACKETVER >= 20131223
	NPCMarketDB = strdb_alloc(DB_OPT_BASE, NPC_NAME_LENGTH+1);
	npc_market_fromsql();
#endif

	timer_event_ers = ers_new(sizeof(struct timer_event_data), "clif.c::timer_event_ers", ERS_OPT_NONE);
	npc_sc_display_ers = ers_new(sizeof(struct sc_display_entry), "npc.c:npc_sc_display_ers", ERS_OPT_NONE);

	// process all npc files
	ShowStatus("Loading NPCs...\r");
	for( file = npc_src_files; file != NULL; file = file->next )
	{
		ShowStatus("Loading NPC file: %s"CL_CLL"\r", file->name);
		npc_parsesrcfile(file->name, false);
	}

	ShowInfo ("Done loading '"CL_WHITE"%d"CL_RESET"' NPCs:"CL_CLL"\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Warps\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Shops\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Scripts\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Spawn sets\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Mobs Cached\n"
		"\t-'"CL_WHITE"%d"CL_RESET"' Mobs Not Cached\n",
		npc_id - START_NPC_NUM, npc_warp, npc_shop, npc_script, npc_mob, npc_cache_mob, npc_delay_mob);

	// set up the events cache
	memset(script_event, 0, sizeof(script_event));
	npc_read_event_script();

#if PACKETVER >= 20131223
	npc_market_checkall();
#endif

	//Debug function to locate all endless loop warps.
	if (battle_config.warp_point_debug)
		npc_debug_warps();

	add_timer_func_list(npc_event_do_clock,"npc_event_do_clock");
	add_timer_func_list(npc_timerevent,"npc_timerevent");

	// Init dummy NPC
	fake_nd = (struct npc_data *)aCalloc(1,sizeof(struct npc_data));
	fake_nd->bl.m = -1;
	fake_nd->bl.id = npc_get_new_npc_id();
	fake_nd->class_ = -1;
	fake_nd->speed = 200;
	strcpy(fake_nd->name,"FAKE_NPC");
	memcpy(fake_nd->exname, fake_nd->name, 9);

	npc_script++;
	fake_nd->bl.type = BL_NPC;
	fake_nd->subtype = NPCTYPE_SCRIPT;

	strdb_put(npcname_db, fake_nd->exname, fake_nd);
	fake_nd->u.scr.timerid = INVALID_TIMER;
	map_addiddb(&fake_nd->bl);

	// End of initialization
}