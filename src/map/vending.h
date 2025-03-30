// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef	_VENDING_H_
#define	_VENDING_H_

#include "../common/cbasetypes.h"
#include "../common/mmo.h"

//#include "map.h"
struct map_session_data;
struct s_search_store_search;

struct s_vending {
	short index;
	short amount;
	unsigned int value;
};

DBMap * vending_getdb();
void do_final_vending(void);
void do_init_vending(void);

void vending_closevending(struct map_session_data* sd);
void vending_openvending(struct map_session_data* sd, const char* message, const uint8* data, int count);
void vending_vendinglistreq(struct map_session_data* sd, int id);
void vending_purchasereq(struct map_session_data* sd, int aid, int uid, const uint8* data, int count);
bool vending_search(struct map_session_data* sd, t_itemid nameid);
bool vending_searchall(struct map_session_data* sd, const struct s_search_store_search* s);
bool vending_checknearnpc(struct block_list * bl);

#endif /* _VENDING_H_ */
