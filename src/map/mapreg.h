// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _MAPREG_H_
#define _MAPREG_H_

void mapreg_reload(void);
void mapreg_final(void);
void mapreg_init(void);
bool mapreg_config_read(const char* w1, const char* w2);

int64 mapreg_readreg(int64 uid);
char* mapreg_readregstr(int64 uid);
bool mapreg_setreg(int64 uid, int64 val);
bool mapreg_setregstr(int64 uid, const char* str);

#endif /* _MAPREG_H_ */
