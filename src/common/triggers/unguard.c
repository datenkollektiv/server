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
#include "unguard.h"

/* kernel includes */
#include <building.h>
#include <region.h>
#include <unit.h>

/* util includes */
#include <event.h>
#include <log.h>

/* libc includes */
#include <stdlib.h>

static int
unguard_handle(trigger * t, void * data)
{
	building * b = (building*)t->data.v;

	if (b) {
		fset(b, BLD_UNGUARDED);
	} else {
		log_error(("could not perform unguard::handle()\n"));
		return -1;
	}
	unused(data);
	return 0;
}

static void
unguard_write(const trigger * t, FILE * F)
{
	write_building_reference((building*)t->data.v, F);
}

static int
unguard_read(trigger * t, FILE * F)
{
	read_building_reference((building**)&t->data.v, F);

	return 1;
}

struct trigger_type tt_unguard = {
	"building",
	NULL,
	NULL,
	unguard_handle,
	unguard_write,
	unguard_read
};

trigger *
trigger_unguard(building * b)
{
	trigger * t = t_new(&tt_unguard);
	t->data.v = (void*)b;
	return t;
}
