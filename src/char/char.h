// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _CHAR_H_
#define _CHAR_H_

#include "../common/core.h" // CORE_ST_LAST
#include "../common/mmo.h"
#include "../common/msg_conf.h"

#define MAX_MAP_SERVERS 30

#define DEFAULT_AUTOSAVE_INTERVAL 300*1000

#define msg_config_read(cfgName) char_msg_config_read(cfgName)
#define msg_txt(msg_number) char_msg_txt(msg_number)
#define do_final_msg() char_do_final_msg()
int char_msg_config_read(char *cfgName);
const char* char_msg_txt(int msg_number);
void char_do_final_msg(void);

struct character_data {
	struct mmo_charstatus status;
	int global_num;
	struct global_reg global[GLOBAL_REG_NUM];
};

enum e_char_delete {
	CHAR_DEL_EMAIL = 1,
	CHAR_DEL_BIRTHDATE
};

enum e_char_delete_restriction {
	CHAR_DEL_RESTRICT_PARTY = 1,
	CHAR_DEL_RESTRICT_GUILD,
	CHAR_DEL_RESTRICT_ALL
};

int memitemdata_to_txt(const struct item items[], int max, int id, int fileswitch);
int inventory_to_txt(const struct item items[], int max, int char_id);

struct mmo_charstatus* search_character(int aid, int cid);
struct mmo_charstatus* search_character_byname(char* character_name);
int search_character_index(char* character_name);
char* search_character_name(int index);
int search_character_online(int aid, int cid);

int mapif_sendall(unsigned char *buf, unsigned int len);
int mapif_sendallwos(int fd,unsigned char *buf, unsigned int len);
int mapif_send(int fd,unsigned char *buf, unsigned int len);

int char_married(int pl1,int pl2);
int char_child(int parent_id, int child_id);
int char_family(int cid1, int cid2, int cid3);

int char_log(char *fmt, ...);

int request_accreg2(int account_id, int char_id);
int char_parse_Registry(int account_id, int char_id, unsigned char *buf, int len);
int save_accreg2(unsigned char *buf, int len);
int char_account_reg_reply(int fd,int account_id,int char_id);

extern int char_name_option;
extern char char_name_letters[];
extern int autosave_interval;
extern char db_path[];
extern int guild_exp_rate;
extern int log_inter;
//Exported for use in the TXT-SQL converter.
extern char char_txt[];
int char_config_read(const char *cfgName);
int mmo_char_fromstr(char *str, struct mmo_charstatus *p, struct global_reg *reg, int *reg_num);
int parse_friend_txt(struct mmo_charstatus *p);

int char_mmo_gender(const struct char_session_data *sd, const struct mmo_charstatus *p, char sex);

#endif /* _CHAR_H_ */
