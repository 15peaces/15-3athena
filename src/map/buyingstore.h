// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _BUYINGSTORE_H_
#define _BUYINGSTORE_H_

struct s_search_store_search;

#define MAX_BUYINGSTORE_SLOTS 5

struct s_buyingstore_item
{
	int price;
	unsigned short amount;
	t_itemid nameid;
};

struct s_buyingstore
{
	struct s_buyingstore_item items[MAX_BUYINGSTORE_SLOTS];
	int zenylimit;
	unsigned char slots;
};

bool buyingstore_setup(struct map_session_data* sd, unsigned char slots);
void buyingstore_create(struct map_session_data* sd, int zenylimit, unsigned char result, const char* storename, const struct PACKET_CZ_REQ_OPEN_BUYING_STORE_sub* itemlist, unsigned int count);
void buyingstore_close(struct map_session_data* sd);
void buyingstore_open(struct map_session_data* sd, uint32 account_id);
void buyingstore_trade(struct map_session_data* sd, uint32 account_id, unsigned int buyer_id, const struct PACKET_CZ_REQ_TRADE_BUYING_STORE_sub* itemlist, unsigned int count);
bool buyingstore_search(struct map_session_data* sd, t_itemid nameid);
bool buyingstore_searchall(struct map_session_data* sd, const struct s_search_store_search* s);

DBMap *buyingstore_getdb(void);
void do_final_buyingstore(void);
void do_init_buyingstore(void);

#endif  // _BUYINGSTORE_H_
