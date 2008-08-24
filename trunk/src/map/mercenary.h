// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _MERCENARY_H_
#define _MERCENARY_H_

#include "status.h" // struct status_data, struct status_change
#include "unit.h" // struct unit_data

struct s_mercenary_db {
	int class_;
	char sprite[NAME_LENGTH], name[NAME_LENGTH];
	unsigned short lv;
	short range2, range3;
	struct status_data status;
	struct view_data vd;
	struct {
		unsigned short id, lv;
	} skill[MAX_MERCSKILL];
};

extern struct s_mercenary_db mercenary_db[MAX_MERCENARY_CLASS];

struct mercenary_data {
	struct block_list bl;
	struct unit_data ud;
	struct view_data *vd;
	struct status_data base_status, battle_status;
	struct status_change sc;
	struct regen_data regen;

	struct s_mercenary_db *db;
	struct s_mercenary mercenary;

	struct map_session_data *master; // Master of mercenary
};

bool merc_class(int class_);
struct view_data * merc_get_viewdata(int class_);
int merc_create(struct map_session_data *sd, int class_, unsigned int lifetime);
int merc_data_received(struct s_mercenary *merc, bool flag);
int mercenary_save(struct mercenary_data *md);
int do_init_mercenary(void);

#endif /* _MERCENARY_H_ */