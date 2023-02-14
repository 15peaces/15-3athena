// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "malloc.h"
#include "msg_conf.h"
#include "showmsg.h"

//-----------------------------------------------------------
// Return the message string of the specified number by [Yor]
// (read in table msg_table, with specified lenght table in size)
//-----------------------------------------------------------

const char* _msg_txt(int msg_number, int size, char ** msg_table)
{
	if (msg_number >= 0 && msg_number < size &&
		msg_table[msg_number] != NULL && msg_table[msg_number][0] != '\0')
		return msg_table[msg_number];

	return "??";
}

/*==========================================
 * Read txt file and store them into msg_table
 *------------------------------------------*/
int _msg_config_read(const char* cfgName, int size, char ** msg_table)
{
	int msg_number;
	char line[1024], w1[1024], w2[1024];
	FILE *fp;
	static int called = 1;

	if ((fp = fopen(cfgName, "r")) == NULL) {
		ShowError("Messages file not found: %s\n", cfgName);
		return -1;
	}

	if ((--called) == 0)
		memset(msg_table, 0, sizeof(msg_table[0]) * size);

	while (fgets(line, sizeof(line), fp)) {
		if (line[0] == '/' && line[1] == '/')
			continue;
		if (sscanf(line, "%[^:]: %[^\r\n]", w1, w2) != 2)
			continue;

		if (strcmpi(w1, "import") == 0)
			_msg_config_read(w2, size, msg_table);
		else {
			msg_number = atoi(w1);
			if (msg_number >= 0 && msg_number < size) {
				if (msg_table[msg_number] != NULL)
					aFree(msg_table[msg_number]);
				msg_table[msg_number] = (char *)aMalloc((strlen(w2) + 1) * sizeof(char));
				strcpy(msg_table[msg_number], w2);
			}
		}
	}

	fclose(fp);
	ShowInfo("Finished reading %s.\n", cfgName);

	return 0;
}

/*==========================================
 * Destroy msg_table (freeup mem)
 *------------------------------------------*/
void _do_final_msg(int size, char ** msg_table) {
	int i;
	for (i = 0; i < size; i++)
		aFree(msg_table[i]);
}

/*
 * lookup a langtype string into his associate langtype number
 * return -1 if not found
 */
int msg_langstr2langtype(char * langtype) {
	int lang = -1;
	if (!strncmpi(langtype, "eng", 2)) lang = 0;
	else if (!strncmpi(langtype, "rus", 2)) lang = 1;
	else if (!strncmpi(langtype, "spn", 2)) lang = 2;
	else if (!strncmpi(langtype, "ger", 2)) lang = 3;
	else if (!strncmpi(langtype, "chn", 2)) lang = 4;
	else if (!strncmpi(langtype, "mal", 2)) lang = 5;
	else if (!strncmpi(langtype, "idn", 2)) lang = 6;
	else if (!strncmpi(langtype, "frn", 2)) lang = 7;
	else if (!strncmpi(langtype, "por", 2)) lang = 8;

	return lang;
}

/*
 * lookup a langtype into his associate lang string
 * return ?? if not found
 */
const char* msg_langtype2langstr(int langtype) {
	switch (langtype) {
	case 0: return "English (ENG)";
	case 1: return "Russkiy (RUS)"; //transliteration
	case 2: return "Espa�ol (SPN)";
	case 3: return "Deutsch (GER)";
	case 4: return "H�nyu (CHN)"; //transliteration
	case 5: return "Bahasa Malaysia (MAL)";
	case 6: return "Bahasa Indonesia (IDN)";
	case 7: return "Fran�ais (FRN)";
	case 8: return "Portugu�s Brasileiro (POR)";
	default: return "??";
	}
}

/*
 * verify that the choosen langtype is enable
 * return
 *  1 : langage enable
 * -1 : false range
 * -2 : disable
 */
int msg_checklangtype(int lang, bool display) {
	uint16 test = (1 << (lang - 1));
	if (!lang) return 1; //default english
	else if (lang < 0 || test > LANG_MAX) return -1; //false range
	else if (LANG_ENABLE&test) return 1;
	else if (display) {
		ShowDebug("Unsupported langtype '%d'.\n", lang);
	}
	return -2;
}
