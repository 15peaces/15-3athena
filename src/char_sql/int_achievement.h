// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _INT_ACHIEVEMENT_SQL_H_
#define _INT_ACHIEVEMENT_SQL_H_

struct achievement;
struct char_achievements;

struct DBMap *char_achievements;

int inter_achievement_tosql(int char_id, struct char_achievements *cp, const struct char_achievements *p);
bool inter_achievement_fromsql(int char_id, struct char_achievements *cp);
void* inter_achievement_ensure_char_achievements(union DBKey key, va_list args);
int inter_achievement_parse_frommap(int fd);

int inter_achievement_sql_init(void);
void inter_achievement_sql_final(void);

#endif /* _INT_ACHIEVEMENT_SQL_H_ */
