// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/mmo.h"
#include "../common/malloc.h"
#include "../common/strlib.h"
#include "../common/showmsg.h"
#include "../common/socket.h"
#include "../common/utils.h"
#include "../common/sql.h"
#include "char.h"
#include "inter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool mapif_elemental_save(struct s_elemental* elem) {
	bool flag = true;

	if( elem->elemental_id == 0 )
	{ // Create new DB entry
		if( SQL_ERROR == Sql_Query(sql_handle,
			"INSERT INTO `elemental` (`char_id`,`class`,`hp`,`max_hp`,`sp`,`max_sp`,`batk`,`matk`,`def`,`mdef`,`hit`,`flee`,`amotion`,`life_time`) VALUES ('%d','%d','%d','%d','%d','%d','%u','%u','%d','%d','%d','%d','%u','%u')",
			elem->char_id, elem->class_, elem->hp, elem->max_hp, elem->sp, elem->max_sp, elem->batk, elem->matk, elem->def, elem->mdef, elem->hit, elem->flee, elem->amotion, elem->life_time))
		{
			Sql_ShowDebug(sql_handle);
			flag = false;
		}
		else
			elem->elemental_id = (int)Sql_LastInsertId(sql_handle);
	}
	else if( SQL_ERROR == Sql_Query(sql_handle,
		"UPDATE `elemental` SET `char_id` = '%d', `class` = '%d', `hp` = '%d', `max_hp` = '%d', `sp` = '%d', `max_sp` = '%d', `batk` = '%u', `matk` = '%u', `def` = '%d', `mdef` = '%d', `hit` = '%d', `flee` = '%d', `amotion` = '%u', `life_time` = '%u' WHERE `ele_id` = '%d'",
		elem->char_id, elem->class_, elem->hp, elem->max_hp, elem->sp, elem->max_sp, elem->batk, elem->matk, elem->def, elem->mdef, elem->hit, elem->flee, elem->amotion, elem->life_time, elem->elemental_id))
	{ // Update DB entry
		Sql_ShowDebug(sql_handle);
		flag = false;
	}
	return flag;
}

bool mapif_elemental_load(int elem_id, int char_id, struct s_elemental *elem) {
	char* data;

	memset(elem, 0, sizeof(struct s_elemental));
	elem->elemental_id = elem_id;
	elem->char_id = char_id;

	if (SQL_ERROR == Sql_Query(sql_handle, "SELECT `class`, `hp`, `max_hp`, `sp`, `max_sp`, `batk`, `matk`, `def`, `mdef`, `hit`, `flee`, `amotion`, `life_time` FROM `elemental` WHERE `ele_id` = '%d' AND `char_id` = '%d'", elem_id, char_id))
	{
		Sql_ShowDebug(sql_handle);
		return false;
	}

	if( SQL_SUCCESS != Sql_NextRow(sql_handle) ) {
		Sql_FreeResult(sql_handle);
		return false;
	}

	Sql_GetData(sql_handle, 0, &data, NULL); elem->class_ = atoi(data);
	Sql_GetData(sql_handle, 1, &data, NULL); elem->hp = atoi(data);
	Sql_GetData(sql_handle, 2, &data, NULL); elem->max_hp = atoi(data);
	Sql_GetData(sql_handle, 3, &data, NULL); elem->sp = atoi(data);
	Sql_GetData(sql_handle, 4, &data, NULL); elem->max_sp = atoi(data);
	Sql_GetData(sql_handle, 5, &data, NULL); elem->batk = atoi(data);
	Sql_GetData(sql_handle, 6, &data, NULL); elem->matk = atoi(data);
	Sql_GetData(sql_handle, 7, &data, NULL); elem->def = atoi(data);
	Sql_GetData(sql_handle, 8, &data, NULL); elem->mdef = atoi(data);
	Sql_GetData(sql_handle, 9, &data, NULL); elem->hit = atoi(data);
	Sql_GetData(sql_handle, 10, &data, NULL); elem->flee = atoi(data);
	Sql_GetData(sql_handle, 11, &data, NULL); elem->amotion = atoi(data);
	Sql_GetData(sql_handle, 12, &data, NULL); elem->life_time = atoi(data);
	Sql_FreeResult(sql_handle);
	if( save_log )
		ShowInfo("Elemental loaded (%d - %d).\n", elem->elemental_id, elem->char_id);
	
	return true;
}

bool mapif_elemental_delete(int elem_id)
{
	if( SQL_ERROR == Sql_Query(sql_handle, "DELETE FROM `elemental` WHERE `ele_id` = '%d'", elem_id) ) {
		Sql_ShowDebug(sql_handle);
		return false;
	}

	return true;
}

#ifndef TXT_SQL_CONVERT

static void mapif_elemental_send(int fd, struct s_elemental *elem, unsigned char flag) {
	int size = sizeof(struct s_elemental) + 5;

	WFIFOHEAD(fd,size);
	WFIFOW(fd,0) = 0x3878;
	WFIFOW(fd,2) = size;
	WFIFOB(fd,4) = flag;
	memcpy(WFIFOP(fd,5),elem,sizeof(struct s_elemental));
	WFIFOSET(fd,size);
}

static void mapif_parse_elemental_create(int fd, struct s_elemental* elem) {
	bool result = mapif_elemental_save(elem);
	mapif_elemental_send(fd, elem, result);
}

static void mapif_parse_elemental_load(int fd, int elem_id, int char_id) {
	struct s_elemental elem;
	bool result = mapif_elemental_load(elem_id, char_id, &elem);
	mapif_elemental_send(fd, &elem, result);
}

static void mapif_elemental_deleted(int fd, unsigned char flag) {
	WFIFOHEAD(fd,3);
	WFIFOW(fd,0) = 0x3879;
	WFIFOB(fd,2) = flag;
	WFIFOSET(fd,3);
}

static void mapif_parse_elemental_delete(int fd, int elem_id) {
	bool result = mapif_elemental_delete(elem_id);
	mapif_elemental_deleted(fd, result);
}

static void mapif_elemental_saved(int fd, unsigned char flag) {
	WFIFOHEAD(fd,3);
	WFIFOW(fd,0) = 0x387a;
	WFIFOB(fd,2) = flag;
	WFIFOSET(fd,3);
}

static void mapif_parse_elemental_save(int fd, struct s_elemental* elem) {
	bool result = mapif_elemental_save(elem);
	mapif_elemental_saved(fd, result);
}

int inter_elemental_sql_init(void) {
	return 0;
}

void inter_elemental_sql_final(void) {
	return;
}

/*==========================================
 * Inter Packets
 *------------------------------------------*/
int inter_elemental_parse_frommap(int fd) {
	unsigned short cmd = RFIFOW(fd,0);

	switch( cmd ) {
		case 0x3078: mapif_parse_elemental_create(fd, (struct s_elemental*)RFIFOP(fd,4)); break;
		case 0x3079: mapif_parse_elemental_load(fd, (int)RFIFOL(fd,2), (int)RFIFOL(fd,6)); break;
		case 0x307a: mapif_parse_elemental_delete(fd, (int)RFIFOL(fd,2)); break;
		case 0x307b: mapif_parse_elemental_save(fd, (struct s_elemental*)RFIFOP(fd,4)); break;
		default:
			return 0;
	}
	return 1;
}
#endif //TXT_SQL_CONVERT