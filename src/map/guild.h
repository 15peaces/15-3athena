// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _GUILD_H_
#define _GUILD_H_

#include "../common/cbasetypes.h" // bool
#include "../common/mmo.h" // struct guild, struct guild_position
struct map_session_data;

//For quick linking to a guardian's info.
struct guardian_data
{
	int number; //0-MAX_GUARDIANS-1 = Guardians. MAX_GUARDIANS = Emperium.
	int guild_id;
	int emblem_id;
	int guardup_lv; //Level of GD_GUARDUP skill.
	char guild_name[NAME_LENGTH];
	struct guild_castle* castle;
};

DBMap *guild_get_castle_db(void);

struct guild_castle* guild_castle_search(int castle_id);
struct guild_castle* guild_mapname2gc(const char* mapname);
struct guild_castle* guild_mapindex2gc(short mapindex);

int guild_skill_get_max(int id);

int guild_checkskill(struct guild *g,int id);
int guild_check_skill_require(struct guild *g,int id); // [Komurka]
bool guild_isallied(int guild_id, int guild_id2); //Checks alliance based on guild Ids. [Skotlex]

void do_init_guild(void);
struct guild *guild_search(int guild_id);
struct guild *guild_searchname(char *str);

struct map_session_data *guild_getavailablesd(struct guild *g);
int guild_getindex(struct guild *g,uint32 account_id,uint32 char_id);
int guild_getposition(struct map_session_data *sd);
unsigned int guild_payexp(struct map_session_data* sd, unsigned int exp);

bool guild_create(struct map_session_data *sd, const char *name);
void guild_created(uint32 account_id,int guild_id);
int guild_request_info(int guild_id);
void guild_recv_noinfo(int guild_id);
void guild_recv_info(struct guild *sg);
int guild_npc_request_info(int guild_id,const char *ev);
void guild_invite(struct map_session_data *sd,struct map_session_data *tsd);
bool guild_reply_invite(struct map_session_data *sd,int guild_id,int flag);
void guild_member_joined(struct map_session_data *sd);
void guild_member_added(int guild_id,uint32 account_id,uint32 char_id,int flag);
int guild_leave(struct map_session_data *sd,int guild_id,
	uint32 account_id,uint32 char_id,const char *mes);
int guild_member_withdraw(int guild_id,uint32 account_id,uint32 char_id,int flag,
	const char *name,const char *mes);
int guild_expulsion(struct map_session_data *sd,int guild_id,
	uint32 account_id,uint32 char_id,const char *mes);
int guild_skillup(struct map_session_data* sd, int skill_id);
void guild_block_skill(struct map_session_data *sd, int time);
int guild_reqalliance(struct map_session_data *sd,struct map_session_data *tsd);
int guild_reply_reqalliance(struct map_session_data *sd,uint32 account_id,int flag);
int guild_allianceack(int guild_id1,int guild_id2,int account_id1,int account_id2,
	int flag,const char *name1,const char *name2);
int guild_delalliance(struct map_session_data *sd,int guild_id,int flag);
int guild_opposition(struct map_session_data *sd,struct map_session_data *tsd);
int guild_check_alliance(int guild_id1, int guild_id2, int flag);

int guild_send_memberinfoshort(struct map_session_data *sd,int online);
void guild_recv_memberinfoshort(int guild_id, uint32 account_id, uint32 char_id, int online, int lv, int class_, int last_login);
int guild_change_memberposition(int guild_id,uint32 account_id,uint32 char_id,short idx);
int guild_memberposition_changed(struct guild *g,int idx,int pos);
int guild_change_position(int guild_id,int idx,int mode,int exp_mode,const char *name);
int guild_position_changed(int guild_id,int idx,struct guild_position *p);
int guild_change_notice(struct map_session_data *sd,int guild_id,const char *mes1,const char *mes2);
int guild_notice_changed(int guild_id,const char *mes1,const char *mes2);
int guild_change_emblem(struct map_session_data *sd,int len,const char *data);
int guild_emblem_changed(int len,int guild_id,int emblem_id,const char *data);
int guild_send_message(struct map_session_data *sd,const char *mes,int len);
int guild_recv_message(int guild_id,uint32 account_id,const char *mes,int len);
int guild_send_dot_remove(struct map_session_data *sd);
int guild_skillupack(int guild_id,int skill_id,uint32 account_id);
int guild_break(struct map_session_data *sd,char *name);
int guild_broken(int guild_id,int flag);
int guild_gm_change(int guild_id, uint32 char_id);
int guild_gm_changed(int guild_id, uint32 account_id, uint32 char_id, time_t time);

void guild_castle_reconnect(int castle_id, int index, int value);

int guild_agit_start(void);
int guild_agit_end(void);

int guild_agit2_start(void);
int guild_agit2_end(void);

int guild_agit3_start(void);
int guild_agit3_end(void);

void guild_guildaura_refresh(struct map_session_data *sd, int skill_id, int skill_lv);

void guild_castle_map_init(void);
int guild_castledatasave(int castle_id, int index, int value);
int guild_castledatasaveack(int castle_id, int index, int value);
int guild_castledataloadack(int len, struct guild_castle *gc);

int guild_castle_count(int guild_id);
void guild_castle_guardian_updateemblem(int guild_id, int emblem_id);

unsigned int guild_addexp(int guild_id, uint32 account_id, uint32 char_id, unsigned int exp);

/* guild flag cachin */
void guild_flag_add(struct npc_data *nd);
void guild_flag_remove(struct npc_data *nd);
void guild_flags_clear(void);

#ifdef BOUND_ITEMS
void guild_retrieveitembound(uint32 char_id, uint32 account_id, int guild_id); 
#endif

void do_final_guild(void);

#endif /* _GUILD_H_ */
