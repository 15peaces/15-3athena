// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef MSG_CONF_H
#define	MSG_CONF_H

#ifdef	__cplusplus
extern "C" {
#endif

enum lang_types {
	LANG_RUS = 0x01,
	LANG_SPN = 0x02,
	LANG_GER = 0x04,
	LANG_CHN = 0x08,
	LANG_MAL = 0x10,
	LANG_IND = 0x20,
	LANG_FRN = 0x40,
	LANG_MAX
};
// What languages are enabled? bitmask FF mean all
//#define LANG_ENABLE 0xFF
#define LANG_ENABLE 0x00

const char* _msg_txt(int msg_number, int size, char ** msg_table);
int _msg_config_read(const char* cfgName, int size, char ** msg_table);
void _do_final_msg(int size, char ** msg_table);
int msg_langstr2langtype(char * langtype);
//verify that the choosen langtype is enable
int msg_checklangtype(int lang, bool display);

#ifdef	__cplusplus
}
#endif

#endif	/* MSG_CONF_H */
