// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _INTIF_H_
#define _INTIF_H_

#include "../common/cbasetypes.h" // bool

struct party_member;
struct guild_member;
struct guild_position;
struct s_pet;
struct s_homunculus;
struct s_mercenary;
struct s_elemental;
struct mail_message;
struct auction_data;
struct achievement_db;

int intif_parse(int fd);

int intif_broadcast(const char* mes, int len, int type);
int intif_broadcast2(const char* mes, int len, unsigned long fontColor, short fontType, short fontSize, short fontAlign, short fontY);
int intif_broadcast_obtain_special_item(struct map_session_data *sd, t_itemid nameid, t_itemid sourceid, unsigned char type);
int intif_broadcast_obtain_special_item_npc(struct map_session_data *sd, t_itemid nameid);
int intif_main_message(struct map_session_data* sd, const char* message);

int intif_wis_message(struct map_session_data *sd,char *nick,char *mes,int mes_len);
int intif_wis_message_to_gm(char *Wisp_name, int min_gm_level, char *mes);

int intif_saveregistry(struct map_session_data *sd, int type);
int intif_request_registry(struct map_session_data *sd, int flag);

bool intif_request_guild_storage(uint32 account_id, int guild_id);
bool intif_send_guild_storage(uint32 account_id, struct s_storage *gstor);


bool intif_create_party(struct party_member* member, const char* name, int item, int item2);
int intif_request_partyinfo(int party_id, uint32 char_id);

int intif_party_addmember(int party_id,struct party_member *member);
int intif_party_changeoption(int party_id, uint32 account_id, int exp, int item);
int intif_party_leave(int party_id, uint32 account_id, uint32 char_id, char *name, enum e_party_member_withdraw type); 
int intif_party_changemap(struct map_session_data *sd, int online);
int intif_break_party(int party_id);
int intif_party_message(int party_id, uint32 account_id, const char *mes,int len);
int intif_party_leaderchange(int party_id,uint32 account_id,uint32 char_id);


int intif_guild_create(const char *name, const struct guild_member *master);
int intif_guild_request_info(int guild_id);
int intif_guild_addmember(int guild_id, struct guild_member *m);
int intif_guild_leave(int guild_id, uint32 account_id, uint32 char_id, int flag, const char *mes);
int intif_guild_memberinfoshort(int guild_id, uint32 account_id, uint32 char_id, int online, int lv, int class_);
int intif_guild_break(int guild_id);
int intif_guild_message(int guild_id, uint32 account_id, const char *mes, int len);
int intif_guild_change_gm(int guild_id, const char* name, int len);
int intif_guild_change_basicinfo(int guild_id, int type, const void* data, int len);
int intif_guild_change_memberinfo(int guild_id, uint32 account_id, uint32 char_id, int type, const void* data, int len);
int intif_guild_position(int guild_id, int idx, struct guild_position *p);
int intif_guild_skillup(int guild_id, int skill_id, uint32 account_id, int max);
int intif_guild_alliance(int guild_id1, int guild_id2, int account_id1, int account_id2, int flag);
int intif_guild_notice(int guild_id, const char *mes1, const char *mes2);
int intif_guild_emblem(int guild_id, int len, const char *data);
int intif_guild_castle_dataload(int num, int *castle_ids);
int intif_guild_castle_datasave(int castle_id, int index, int value);
#ifdef BOUND_ITEMS
void intif_itembound_guild_retrieve(uint32 char_id, uint32 account_id, int guild_id);
#endif

int intif_create_pet(uint32 account_id, uint32 char_id, short pet_class, short pet_lv, t_itemid pet_egg_id, t_itemid pet_equip, short intimate, short hungry, char rename_flag, char incubate, char *pet_name);
int intif_request_petdata(uint32 account_id, uint32 char_id, int pet_id);
int intif_save_petdata(uint32 account_id, struct s_pet *p);
int intif_delete_petdata(int pet_id);
int intif_rename(struct map_session_data *sd, int type, char *name);
#define intif_rename_pc(sd, name) intif_rename(sd, 0, name)
#define intif_rename_pet(sd, name) intif_rename(sd, 1, name)
#define intif_rename_hom(sd, name) intif_rename(sd, 2, name)
int intif_homunculus_create(uint32 account_id, struct s_homunculus *sh);
int intif_homunculus_requestload(uint32 account_id, int homun_id);
int intif_homunculus_requestsave(uint32 account_id, struct s_homunculus* sh);
int intif_homunculus_requestdelete(int homun_id);

/******QUEST SYTEM*******/
void intif_request_questlog(struct map_session_data * sd);
int intif_quest_save(struct map_session_data * sd);

// MERCENARY SYSTEM
int intif_mercenary_create(struct s_mercenary *merc);
int intif_mercenary_request(int merc_id, uint32 char_id);
int intif_mercenary_delete(int merc_id);
int intif_mercenary_save(struct s_mercenary *merc);

// ELEMENTAL SYSTEM
int intif_elemental_create(struct s_elemental *elem);
int intif_elemental_request(int elem_id, uint32 char_id);
int intif_elemental_delete(int elem_id);
int intif_elemental_save(struct s_elemental *elem);

#ifndef TXT_ONLY
// MAIL SYSTEM
int intif_Mail_requestinbox(uint32 char_id, unsigned char flag, enum mail_inbox_type type);
int intif_Mail_read(int mail_id);
bool intif_mail_getattach( struct map_session_data* sd, struct mail_message *msg, enum mail_attachment_type type ); 
int intif_Mail_delete(uint32 char_id, int mail_id);
int intif_Mail_return(uint32 char_id, int mail_id);
int intif_Mail_send(uint32 account_id, struct mail_message *msg);
bool intif_mail_checkreceiver(struct map_session_data* sd, char* name); 
// AUCTION SYSTEM
int intif_Auction_requestlist(uint32 char_id, short type, int price, const char* searchtext, short page);
int intif_Auction_register(struct auction_data *auction);
int intif_Auction_cancel(uint32 char_id, unsigned int auction_id);
int intif_Auction_close(uint32 char_id, unsigned int auction_id);
int intif_Auction_bid(uint32 char_id, const char* name, unsigned int auction_id, int bid);
#endif

// CLAN SYSTEM
int intif_clan_requestclans();
int intif_clan_message(int clan_id, uint32 account_id, const char *mes, int len);
int intif_clan_member_joined(int clan_id);
int intif_clan_member_left(int clan_id);

// ACHIEVEMENT SYSTEM
void intif_request_achievements(uint32 char_id);
void intif_achievements_save(struct map_session_data *sd);

// STORAGE
bool intif_storage_request(struct map_session_data *sd, enum storage_type type, uint8 stor_id);
bool intif_storage_save(struct map_session_data *sd, struct s_storage *stor);

bool CheckForCharServer(void);

#endif /* _INTIF_H_ */
