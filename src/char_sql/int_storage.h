// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _INT_STORAGE_SQL_H_
#define _INT_STORAGE_SQL_H_

struct s_storage;

void inter_storage_sql_init(void);
void inter_storage_sql_final(void);
void inter_storage_delete(uint32 account_id);
void inter_guild_storage_delete(int guild_id);

bool inter_storage_parse_frommap(int fd);

//Exported for use in the TXT-SQL converter.
int storage_fromsql(uint32 account_id, struct storage_data* p);
int storage_tosql(uint32 account_id,struct storage_data *p);
int guild_storage_tosql(int guild_id, struct guild_storage *p);

const char *inter_Storage_getTableName(uint8 id);

#endif /* _INT_STORAGE_SQL_H_ */
