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

#include "racespoils.h"

#include <build.h>
#include <region.h>

/* kernel includes */
#include <item.h>

/* libc includes */
#include <assert.h>

resource_type rt_elfspoil = {
	{ "elfspoil", "elfspoil_p" },
	{ "elfspoil", "elfspoil_p" },
	RTF_ITEM|RTF_POOLED,
	&res_changeitem
};

item_type it_elfspoil = {
	&rt_elfspoil,  /* resourcetype */
	0, 1, 0,       /* flags, weight, capacity */
	NULL,	       /* construction */
	NULL,          /* use */
	NULL           /* give */
};

resource_type rt_demonspoil = {
	{ "demonspoil", "demonspoil_p" },
	{ "demonspoil", "demonspoil_p" },
	RTF_ITEM|RTF_POOLED,
	&res_changeitem
};

item_type it_demonspoil = {
	&rt_demonspoil,  /* resourcetype */
	0, 1, 0,       /* flags, weight, capacity */
	NULL,	       /* construction */
	NULL,          /* use */
	NULL           /* give */
};

resource_type rt_goblinspoil = {
	{ "goblinspoil", "goblinspoil_p" },
	{ "goblinspoil", "goblinspoil_p" },
	RTF_ITEM|RTF_POOLED,
	&res_changeitem
};

item_type it_goblinspoil = {
	&rt_goblinspoil,  /* resourcetype */
	0, 1, 0,       /* flags, weight, capacity */
	NULL,	       /* construction */
	NULL,          /* use */
	NULL           /* give */
};

resource_type rt_dwarfspoil = {
	{ "dwarfspoil", "dwarfspoil_p" },
	{ "dwarfspoil", "dwarfspoil_p" },
	RTF_ITEM|RTF_POOLED,
	&res_changeitem
};

item_type it_dwarfspoil = {
	&rt_dwarfspoil,  /* resourcetype */
	0, 1, 0,       /* flags, weight, capacity */
	NULL,	       /* construction */
	NULL,          /* use */
	NULL           /* give */
};

resource_type rt_halflingspoil = {
	{ "halflingspoil", "halflingspoil_p" },
	{ "halflingspoil", "halflingspoil_p" },
	RTF_ITEM|RTF_POOLED,
	&res_changeitem
};

item_type it_halflingspoil = {
	&rt_halflingspoil,  /* resourcetype */
	0, 1, 0,       /* flags, weight, capacity */
	NULL,	       /* construction */
	NULL,          /* use */
	NULL           /* give */
};

resource_type rt_halflingspoil = {
	{ "halflingspoil", "halflingspoil_p" },
	{ "halflingspoil", "halflingspoil_p" },
	RTF_ITEM|RTF_POOLED,
	&res_changeitem
};

item_type it_halflingspoil = {
	&rt_halflingspoil,  /* resourcetype */
	0, 1, 0,       /* flags, weight, capacity */
	NULL,	       /* construction */
	NULL,          /* use */
	NULL           /* give */
};

void
register_racespoils(void)
{
	it_register(&it_elvenspoil);
	it_register(&it_demonspoil);
	it_register(&it_goblinspoil);
	it_register(&it_dwarfspoil);
	it_register(&it_halflingspoil);
}

