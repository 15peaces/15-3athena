// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include "../common/db.h"
#include "../common/malloc.h"
#include "../common/mmo.h"
#include "../common/showmsg.h"
#include "../common/socket.h"
#include "../common/sql.h"
#include "../common/strlib.h"
#include "../common/nullpo.h"

#include "char.h"
#include "inter.h"
#include "int_achievement.h"
#include "int_mail.h"

#include <stdio.h>
#include <stdlib.h>

/**
 * Saves changed achievements for a character.
 * @param[in]   char_id     character identifier.
 * @param[out]  cp          pointer to loaded achievements.
 * @param[in]   p           pointer to map-sent character achievements.
 * @return number of achievements saved.
 */
int inter_achievement_tosql(int char_id, struct char_achievements *cp, const struct char_achievements *p)
{
	StringBuf buf;
	int i = 0, rows = 0;

	nullpo_ret(cp);
	nullpo_ret(p);
	Assert_ret(char_id > 0);

	StringBuf_Init(&buf);
	StringBuf_Printf(&buf, "REPLACE INTO `%s` (`char_id`, `id`, `completed`, `rewarded`", achievement_db);
	for (i = 1; i <= MAX_ACHIEVEMENT_OBJECTIVES; i++)
		StringBuf_Printf(&buf, ", `count%d`", i);
	StringBuf_AppendStr(&buf, ") VALUES ");

	for (i = 0; i < (int)VECTOR_LENGTH(*p); i++) {
		int j = 0;
		bool save = false;
		struct achievement *pa = &VECTOR_INDEX(*p, i), *cpa = NULL;

		ARR_FIND(0, (int)VECTOR_LENGTH(*cp), j, ((cpa = &VECTOR_INDEX(*cp, j)) && cpa->id == pa->id));

		if (j == VECTOR_LENGTH(*cp))
			save = true;
		else if (memcmp(cpa, pa, sizeof(struct achievement)) != 0)
			save = true;

		if (save) {
			StringBuf_Printf(&buf, "%s('%d', '%d', '%"PRId64"', '%"PRId64"'", rows ? ", " : "", char_id, pa->id, (int64)pa->completed, (int64)pa->rewarded);
			for (j = 1; j <= MAX_ACHIEVEMENT_OBJECTIVES; j++)
				StringBuf_Printf(&buf, ", '%d'", pa->objective[j]);
			StringBuf_AppendStr(&buf, ")");
			rows++;
		}
	}

	if (rows > 0 && SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf))) {
		Sql_ShowDebug(sql_handle);
		StringBuf_Destroy(&buf); // Destroy the buffer.
		return 0;
	}
	// Destroy the buffer.
	StringBuf_Destroy(&buf);

	if (rows) {
		ShowInfo("achievements saved for char %d (total: %d, saved: %d)\n", char_id, VECTOR_LENGTH(*p), rows);

		/* Sync with inter-db acheivements. */
		VECTOR_CLEAR(*cp);
		VECTOR_ENSURE(*cp, VECTOR_LENGTH(*p), 1);
		VECTOR_PUSHARRAY(*cp, VECTOR_DATA(*p), VECTOR_LENGTH(*p));
	}

	return rows;
}

/**
 * Retrieves all achievements of a character.
 * @param[in]  char_id  character identifier.
 * @param[out] cp       pointer to character achievements structure.
 * @return true on success, false on failure.
 */
bool inter_achievement_fromsql(int char_id, struct char_achievements *cp)
{
	StringBuf buf;
	char *data;
	int i = 0, num_rows = 0;

	nullpo_ret(cp);

	Assert_ret(char_id > 0);

	// char_achievements (`char_id`, `ach_id`, `completed_at`, `rewarded_at`, `obj_0`, `obj_2`, ...`obj_9`)
	StringBuf_Init(&buf);
	StringBuf_AppendStr(&buf, "SELECT `id`, `completed`, `rewarded`");
	for (i = 1; i <= MAX_ACHIEVEMENT_OBJECTIVES; i++)
		StringBuf_Printf(&buf, ", `count%d`", i);
	StringBuf_Printf(&buf, " FROM `%s` WHERE `char_id` = '%d' ORDER BY `id`", achievement_db, char_id);

	if (SQL_ERROR == Sql_QueryStr(sql_handle, StringBuf_Value(&buf))) {
		Sql_ShowDebug(sql_handle);
		StringBuf_Destroy(&buf);
		return false;
	}

	VECTOR_CLEAR(*cp);

	if ((num_rows = (int)Sql_NumRows(sql_handle)) != 0) {
		int j = 0;

		VECTOR_ENSURE(*cp, (size_t)num_rows, 1);

		for (i = 0; i < num_rows && SQL_SUCCESS == Sql_NextRow(sql_handle); i++) {
			struct achievement t_ach = { 0 };
			Sql_GetData(sql_handle, 0, &data, NULL); t_ach.id = atoi(data);
			Sql_GetData(sql_handle, 1, &data, NULL); t_ach.completed = atoi(data);
			Sql_GetData(sql_handle, 2, &data, NULL); t_ach.rewarded = atoi(data);
			/* Objectives */
			for (j = 0; j < MAX_ACHIEVEMENT_OBJECTIVES; j++) {
				Sql_GetData(sql_handle, j + 3, &data, NULL);
				t_ach.objective[j] = atoi(data);
			}
			/* Add Entry */
			VECTOR_PUSH(*cp, t_ach);
		}
	}

	Sql_FreeResult(sql_handle);

	StringBuf_Destroy(&buf);

	if (num_rows > 0)
		ShowInfo("achievements loaded for char %d (total: %d)\n", char_id, num_rows);

	return true;
}

/**
 * Sends achievement data of a character to the map server.
 * @packet[out] 0x3862  <packet_id>.W <payload_size>.W <char_id>.L <char_achievements[]>.P
 * @param[in]  fd      socket descriptor.
 * @param[in]  char_id Character ID.
 * @param[in]  cp      Pointer to character's achievement data vector.
 */
static void inter_send_achievements_to_map(int fd, int char_id, const struct char_achievements *cp)
{
	int i = 0;
	int data_size = 0;

	nullpo_retv(cp);

	data_size = sizeof(struct achievement) * VECTOR_LENGTH(*cp);

	/* Send to the map server. */
	WFIFOHEAD(fd, (8 + data_size));
	WFIFOW(fd, 0) = 0x3862;
	WFIFOW(fd, 2) = (8 + data_size);
	WFIFOL(fd, 4) = char_id;
	for (i = 0; i < (int)VECTOR_LENGTH(*cp); i++)
		memcpy(WFIFOP(fd, 8 + i * sizeof(struct achievement)), &VECTOR_INDEX(*cp, i), sizeof(struct achievement));
	WFIFOSET(fd, 8 + data_size);
}

/**
 * This function ensures idb's entry.
 */
DBData inter_achievement_ensure_char_achievements(union DBKey key, va_list args)
{
	struct char_achievements *ca = NULL;

	CREATE(ca, struct char_achievements, 1);
	VECTOR_INIT(*ca);

	return db_ptr2data(ca);
}

/**
 * Loads achievements and sends to the map server.
 * @param[in] fd       socket descriptor
 * @param[in] char_id  character Id.
 */
static void inter_achievement_load(int fd, int char_id)
{
	struct char_achievements *cp = NULL;

	/* Ensure data exists */
	cp = idb_ensure(char_achievements, char_id, inter_achievement_ensure_char_achievements);

	/* Load storage for char-server. */
	inter_achievement_fromsql(char_id, cp);

	/* Send Achievements to map server. */
	inter_send_achievements_to_map(fd, char_id, cp);
}

/**
 * Parse achievement load request from the map server
 * @param[in] fd  socket descriptor.
 */
static void inter_parse_load_achievements(int fd)
{
	int char_id = 0;

	/* Read received information from map-server. */
	RFIFOHEAD(fd);
	char_id = RFIFOL(fd, 2);

	/* Load and send achievements to map */
	inter_achievement_load(fd, char_id);
}

/**
 * Handles inter-server achievement db ensuring
 * and saves current achievements to sql.
 * @param[in]  char_id      character identifier.
 * @param[out] p            pointer to character achievements vector.
 */
static void inter_achievement_save(int char_id, struct char_achievements *p)
{
	struct char_achievements *cp = NULL;

	nullpo_retv(p);

	/* Get loaded achievements. */
	cp = idb_ensure(char_achievements, char_id, inter_achievement_ensure_char_achievements);

	if (VECTOR_LENGTH(*p)) /* Save current achievements. */
		inter_achievement_tosql(char_id, cp, p);
}

/**
 * Handles achievement request and saves data from map server.
 * @packet[in] 0x3863 <packet_size>.W <char_id>.L <char_achievement>.P
 * @param[in]  fd     session socket descriptor.
 */
static void inter_parse_save_achievements(int fd)
{
	int size = 0, char_id = 0, payload_count = 0, i = 0;
	struct char_achievements p = { 0 };

	RFIFOHEAD(fd);
	size = RFIFOW(fd, 2);
	char_id = RFIFOL(fd, 4);

	payload_count = (size - 8) / sizeof(struct achievement);

	VECTOR_INIT(p);
	VECTOR_ENSURE(p, (size_t)payload_count, 1);

	for (i = 0; i < payload_count; i++) {
		struct achievement ach = { 0 };
		memcpy(&ach, RFIFOP(fd, 8 + i * sizeof(struct achievement)), sizeof(struct achievement));
		VECTOR_PUSH(p, ach);
	}

	inter_achievement_save(char_id, &p);

	VECTOR_CLEAR(p);
}

/**
 * Handles checking of map server packets and calls appropriate functions.
 * @param fd   socket descriptor.
 * @return 0 on failure, 1 on succes.
 */
int inter_achievement_parse_frommap(int fd)
{
	RFIFOHEAD(fd);

	switch (RFIFOW(fd, 0)) {
	case 0x3062:
		inter_parse_load_achievements(fd);
		break;
	case 0x3063:
		inter_parse_save_achievements(fd);
		break;
	default:
		return 0;
	}

	return 1;
}

/**
 * Initialization function
 */
int inter_achievement_sql_init(void)
{
	// Initialize the loaded db storage.
	// used as a comparand against map-server achievement data before saving.
	char_achievements = idb_alloc(DB_OPT_RELEASE_DATA);
	return 1;
}

/**
 * Cleaning function called through db_destroy()
 */
static int inter_achievement_char_achievements_clear(union DBKey key, struct DBData *data, va_list args)
{
	struct char_achievements *ca = db_data2ptr(data);

	VECTOR_CLEAR(*ca);

	return 0;
}

/**
 * Finalization function.
 */
void inter_achievement_sql_final(void)
{
	char_achievements->destroy(char_achievements, inter_achievement_char_achievements_clear);
}

