// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _SEARCHSTORE_H_
#define _SEARCHSTORE_H_

#define SEARCHSTORE_RESULTS_PER_PAGE 10

/// information about the search being performed
struct s_search_store_search
{
	struct map_session_data* search_sd;  // sd of the searching player
	const struct PACKET_CZ_SEARCH_STORE_INFO_item* itemlist;
	const struct PACKET_CZ_SEARCH_STORE_INFO_item* cardlist;
	unsigned int item_count;
	unsigned int card_count;
	unsigned int min_price;
	unsigned int max_price;
};

struct s_search_store_info_item
{
	int store_id;
	uint32 account_id;
	char store_name[MESSAGE_SIZE];
	unsigned short nameid;
	unsigned short amount;
	unsigned int price;
	unsigned short card[MAX_SLOTS];
	unsigned char refine;
	struct item_option option[MAX_ITEM_RDM_OPT];
};

struct s_search_store_info
{
	unsigned int count;
	struct s_search_store_info_item* items;
	unsigned int pages;  // amount of pages already sent to client
	unsigned int uses;
	int remote_id;
	time_t nextquerytime;
	unsigned short effect;  // 0 = Normal (display coords), 1 = Cash (remote open store)
	unsigned char type;  // 0 = Vending, 1 = Buying Store
	bool open;
};

bool searchstore_open(struct map_session_data* sd, unsigned int uses, unsigned short effect);
void searchstore_query(struct map_session_data* sd, unsigned char type, unsigned int min_price, unsigned int max_price, const struct PACKET_CZ_SEARCH_STORE_INFO_item* itemlist, unsigned int item_count, const struct PACKET_CZ_SEARCH_STORE_INFO_item* cardlist, unsigned int card_count);
bool searchstore_querynext(struct map_session_data* sd);
void searchstore_next(struct map_session_data* sd);
void searchstore_clear(struct map_session_data* sd);
void searchstore_close(struct map_session_data* sd);
void searchstore_click(struct map_session_data* sd, uint32 account_id, int store_id, t_itemid nameid);
bool searchstore_queryremote(struct map_session_data* sd, uint32 account_id);
void searchstore_clearremote(struct map_session_data* sd);
bool searchstore_result(struct map_session_data* sd, int store_id, uint32 account_id, const char* store_name, t_itemid nameid, unsigned short amount, unsigned int price, const t_itemid* card, unsigned char refine, const struct item_option* option);

#endif  // _SEARCHSTORE_H_
