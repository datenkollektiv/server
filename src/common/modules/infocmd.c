/* vi: set ts=2:
 +-------------------+  Christian Schlittchen <corwin@amber.kn-bremen.de>
 |                   |  Enno Rehling <enno@eressea-pbem.de>
 | Eressea PBEM host |  Katja Zedel <katze@felidae.kn-bremen.de>
 | (c) 1998 - 2001   |  Henning Peters <faroul@beyond.kn-bremen.de>
 |                   |  Ingo Wilken <Ingo.Wilken@informatik.uni-oldenburg.de>
 +-------------------+  Stefan Reich <reich@halbling.de>

 This program may not be used, modified or distributed 
 without prior permission by the authors of Eressea.
 
 */
#include <config.h>
#include <eressea.h>

#include "infocmd.h"

#include "command.h"

/* kernel includes */
#include <faction.h>
#include <region.h>
#include <unit.h>
#include <save.h>

/* util includes */
#include <base36.h>
#include <sql.h>

/* libc includes */
#include <string.h>
#include <time.h>
#include <stdlib.h>

static void
info_email(const char * str, void * data, const char * cmd)
{
	unused(str);
	unused(data);
	unused(cmd);
}

static void
info_name(const char * str, void * data, const char * cmd)
{
	unit * u = (unit*)data;
	faction * f = u->faction;
	const char * name = sqlquote(igetstrtoken(str));

	if (sqlstream!=NULL) {
		fprintf(sqlstream, "UPDATE users SET firstname = '%s' WHERE id = %u;\n", 
				name, f->unique_id);
	}
}

static void
info_address(const char * str, void * data, const char * cmd)
{
	unit * u = (unit*)data;
	faction * f = u->faction;
	const char * address = sqlquote(igetstrtoken(str));

	if (sqlstream!=NULL) {
		fprintf(sqlstream, "UPDATE users SET address = '%s' WHERE id = %u;\n", 
				address, f->unique_id);
	}
}

static void
info_phone(const char * str, void * data, const char * cmd)
{
	unit * u = (unit*)data;
	faction * f = u->faction;
	const char * phone = sqlquote(igetstrtoken(str));

	if (sqlstream!=NULL) {
		fprintf(sqlstream, "UPDATE users SET phone = '%s' WHERE id = %u;\n", 
				phone, f->unique_id);
	}
}

static void
info_vacation(const char * str, void * data, const char * cmd)
{
	unit * u = (unit*)data;
	faction * f = u->faction;
	const char * email = sqlquote(igetstrtoken(str));
	int duration = atoi(getstrtoken());
	time_t start_time = time(NULL);
	time_t end_time = start_time + 60*60*24*duration;
	struct tm start = *localtime(&start_time);
	struct tm end = *localtime(&end_time);

	if (sqlstream!=NULL) {
		fprintf(sqlstream, "UPDATE factions SET vacation = '%s' WHERE id = '%s';\n", email, itoa36(f->no));
		fprintf(sqlstream, "UPDATE factions SET vacation_start = '%04d-%02d-%02d' WHERE id = '%s';\n", 
			start.tm_year, start.tm_mon, start.tm_mday, itoa36(f->no));
		fprintf(sqlstream, "UPDATE factions SET vacation_end = '%04d-%02d-%02d' WHERE id = '%s';\n",
			end.tm_year, end.tm_mon, end.tm_mday, itoa36(f->no));
	}
}

static struct command * g_cmds;
static tnode g_keys;

void
infocommands(void)
{
	region ** rp = &regions;
	while (*rp) {
		region * r = *rp;
		unit **up = &r->units;
		while (*up) {
			unit * u = *up;
			strlist * order;
			for (order = u->orders; order; order = order->next)
				if (igetkeyword(order->s, u->faction->locale) == K_INFO) {
					do_command(&g_keys, u, order->s);
				}
			if (u==*up) up = &u->next;
		}
		if (*rp==r) rp = &r->next;
	}
	fflush(sqlstream);
}

static void
info_command(const char * str, void * data, const char * cmd)
{
	do_command(&g_keys, data, str);
}

void
init_info(void)
{
	add_command(&g_keys, &g_cmds, "info", &info_command);

	add_command(&g_keys, &g_cmds, "email", &info_email);
	add_command(&g_keys, &g_cmds, "name", &info_name);
	add_command(&g_keys, &g_cmds, "adresse", &info_address);
	add_command(&g_keys, &g_cmds, "telefon", &info_phone);
	add_command(&g_keys, &g_cmds, "urlaub", &info_vacation);
}
