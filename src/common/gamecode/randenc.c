/* vi: set ts=2:
 *
 *
 *	Eressea PB(E)M host Copyright (C) 1998-2000
 *      Christian Schlittchen (corwin@amber.kn-bremen.de)
 *      Katja Zedel (katze@felidae.kn-bremen.de)
 *      Henning Peters (faroul@beyond.kn-bremen.de)
 *      Enno Rehling (enno@eressea-pbem.de)
 *      Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
 *
 *  based on:
 *
 * Atlantis v1.0  13 September 1993 Copyright 1993 by Russell Wallace
 * Atlantis v1.7                    Copyright 1996 by Alex Schr�der
 *
 * This program may not be used, modified or distributed without
 * prior permission by the authors of Eressea.
 * This program may not be sold or used commercially without prior written
 * permission from the authors.
 */

#include <config.h>
#include "eressea.h"
#include "randenc.h"

#include "unit.h"
#include "faction.h"
#include "alchemy.h"
#include "item.h"
#include "plane.h"
#include "economy.h"
#include "building.h"
#include "magic.h"
#include "message.h"
#include "race.h"
#include "monster.h"
#include "creation.h"
#include "names.h"
#include "pool.h"
#include "movement.h"
#include "curse.h"
#include "region.h"
#include "skill.h"
#include "karma.h"
#include "ship.h"
#include "battle.h"
#include "luck.h"

/* attributes includes */
#include <attributes/racename.h>

/* util includes */
#include <rand.h>
#include <util/message.h>

/* libc includes */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include <attributes/iceberg.h>
extern attrib_type at_unitdissolve;
extern attrib_type at_orcification;

int
nrand(int start, int sub)
{
	int res = 0;

	do {
		if (rand() % 100 < start)
			res++;
		start -= sub;
	} while (start > 0);

	return res;
}

/* In a->data.ca[1] steht der Prozentsatz mit dem sich die Einheit
 * aufl�st, in a->data.ca[0] kann angegeben werden, wohin die Personen
 * verschwinden. Passiert bereits in der ersten Runde! */
static void
dissolve_units(void)
{
	region *r;
	unit *u;
	int n;
	int i;

	for (r=regions;r;r=r->next) {
		for (u=r->units;u;u=u->next) {
			attrib * a = a_find(u->attribs, &at_unitdissolve);
			if (a) {
				if (u->age == 0 && a->data.ca[1] < 100) continue;

				/* TODO: Durch einzelne Berechnung ersetzen */
				if (a->data.ca[1] == 100) {
					n = u->number;
				} else {
					n = 0;
					for (i=0;i<u->number;i++) {
						if (rand()%100 < a->data.ca[1]) n++;
					}
				}

				/* wenn keiner verschwindet, auch keine Meldung */
				if (n == 0) {
					continue;
				}

				scale_number(u,u->number - n);

				sprintf(buf, "%s in %s: %d %s ", unitname(u), regionid(r),
						n, LOC(default_locale, rc_name(u->race, n!=1)));
				switch(a->data.ca[0]) {
				case 1:
					rsetpeasants(r, rpeasants(r) + n);
					if (n == 1) {
						scat("kehrte auf sein Feld zur�ck.");
					}else{
						scat("kehrten auf ihre Felder zur�ck.");
					}
					break;
				case 2:
					if(r->land) {
#if GROWING_TREES
						rsettrees(r, 2, rtrees(r,2) + n);
#else
						rsettrees(r, rtrees(r) + n);
#endif
						if (n == 1) {
							scat("wurde zum Baum.");
						}else{
							scat("wurden zu B�umen.");
						}
					} else {
						if(n == 1) {
							scat("verfaulte in der feuchten Seeluft.");
						} else {
							scat("verfaulten in der feuchten Seeluft.");
						}
					}
					break;
				default:
					if (u->race == new_race[RC_STONEGOLEM] || u->race == new_race[RC_IRONGOLEM]) {
						if (n == 1) {
							scat("zerfiel zu Staub.");
						}else{
							scat("zerfielen zu Staub.");
						}
					}else{
						if (n == 1) {
							scat("verschwand �ber Nacht.");
						}else{
							scat("verschwanden �ber Nacht.");
						}
					}
					break;
				}
				addmessage(r, u->faction, buf, MSG_EVENT, ML_INFO);
			}
		}
	}

	remove_empty_units();
}

static int
improve_all(faction * f, skill_t sk, int weeks)
{
	region *r;
	unit *u;
	int n = 0;
	region *last = lastregion(f);

	for (r = firstregion(f); r != last; r = r->next) {
		for (u = r->units; u; u = u->next) {
			if (u->faction == f && has_skill(u, sk)) {
				for (n=0;n!=weeks;++n) {
					learn_skill(u, sk, 1.0);
				}
			}
		}
	}

	return n;
}

void
find_manual(region * r, unit * u)
{
	skill_t skill = NOSKILL;
	sprintf(buf, "%s stolper%c bei der Erforschung der Region �ber ",
		unitname(u), "nt"[u->number == 1]);

	switch (rand() % 4) {
	case 0:
		scat("die Ruine eines alten Tempels");
		break;
	case 1:
		scat("eine alte Burgruine");
		break;
	case 2:
		scat("ein zerfallenes Bauernhaus");
		break;
	case 3:
		scat("eine Leiche am Wegesrand");
		break;
	}

	scat(". Bei der Durchsuchung ");
	if (u->number == 1) {
		scat("st��t");
	} else {
		scat("sto�en");
	}
	scat(" sie auf das zerfledderte Exemplar eines alten Buches, betitelt ");

	switch (rand() % 36) {
	case 0:
		scat("\'Magie der Elemente\'");
		skill = SK_MAGIC;
		break;
	case 1:
	case 2:
	case 3:
	case 4:
		scat("\'Schwerter, Armbr�ste, Langb�gen\'");
		skill = SK_WEAPONSMITH;
		break;
	case 5:
	case 6:
		scat("\'Gorms Almanach der Rationellen Kriegsf�hrung\'");
		skill = SK_TACTICS;
		break;
	case 7:
	case 8:
	case 9:
	case 10:
		scat("\'Katamarane, Koggen, Karavellen\'");
		skill = SK_SHIPBUILDING;
		break;
	case 11:
	case 12:
	case 13:
	case 14:
		scat("\'Wege der Sterne\'");
		skill = SK_SAILING;
		break;
	case 15:
	case 16:
	case 17:
		scat("\'Nadishahs Kleine Gift- und Kr�uterkunde\'");
		skill = SK_HERBALISM;
		break;
	case 18:
	case 19:
		scat("\'Mandricks Kompendium der Alchemie\'");
		skill = SK_ALCHEMY;
		break;
	case 20:
	case 21:
	case 22:
	case 23:
		scat("\'Die Konstruktion der Burgen und Schl�sser von Zentralandune\'");
		skill = SK_BUILDING;
		break;
	case 24:
	case 25:
	case 26:
	case 27:
		scat("\'Die Esse\'");
		skill = SK_ARMORER;
		break;
	case 28:
	case 29:
	case 30:
	case 31:
		scat("\'�ber die Gewinnung von Erzen\'");
		skill = SK_MINING;
		break;
	case 32:
	case 33:
	case 34:
	case 35:
		scat("\'Barinions Lieder, eine Einf�hrung f�r Unbedarfte\'");
		skill = SK_ENTERTAINMENT;
		break;
	}

	scat(". Der Wissensschub ist enorm.");
	addmessage(r, u->faction, buf, MSG_EVENT, ML_IMPORTANT);

	if (improve_all(u->faction, skill, 3) == 0) {
		int i;
		for (i=0;i!=9;++i) learn_skill(u, skill, 1.0);
	}
}

static void
get_unit(region * r, unit * u)
{
	unit *newunit;
	sprintf(buf, "%s entdeck%s ein kleines Dorf. Die meisten H�user "
			"wurden durch einen �ber die Ufer getretenen Flu� zerst�rt. Eine "
			"Gruppe der verzweifelten Menschen schlie�t sich deiner Partei an.",
			unitname(u), "en\0t" + (3 - 3 * (u->number == 1)));

	addmessage(r, u->faction, buf, MSG_EVENT, ML_IMPORTANT);

	newunit = createunit(r, u->faction, rand() % 20 + 3, u->faction->race);
	set_string(&newunit->name, "Dorfbewohner");
	set_money(newunit, (rand() % 26 + 10) * newunit->number);
	fset(newunit, UFL_ISNEW);
	if (fval(u, FL_PARTEITARNUNG)) fset(newunit, FL_PARTEITARNUNG);
	switch (rand() % 4) {
	case 0:
		set_level(newunit, SK_MINING, 1);
		break;
	case 1:
		set_level(newunit, SK_LUMBERJACK, 1);
		break;
	case 2:
		set_level(newunit, SK_CARTMAKER, 1);
		break;
	case 3:
		set_level(newunit, SK_QUARRYING, 1);
		break;
	}
	set_item(newunit, I_WAGON, rand() % 2);
	set_item(newunit, I_HORSE, min(get_item(newunit, I_WAGON) * 2, rand() % 5));
}

static void
get_allies(region * r, unit * u)
{
	unit *newunit = NULL;

	switch (rterrain(r)) {

	case T_PLAIN:
		if (!r_isforest(r)) {
			if (get_money(u) / u->number < 100 + rand() % 200)
				return;
			newunit = createunit(r, u->faction, rand() % 8 + 2, u->faction->race);
			set_string(&newunit->name, "S�ldner");
			set_money(newunit, (rand() % 80 + 20) * newunit->number);

			switch (rand() % 4) {
			case 0:
				set_level(newunit, SK_SWORD, 1+rand()%3);
				set_item(newunit, I_SWORD, newunit->number);
				break;
			case 1:
				set_level(newunit, SK_SPEAR, 1+rand()%3);
				set_item(newunit, I_SPEAR, newunit->number);
				break;
			case 2:
				set_level(newunit, SK_CROSSBOW, 1+rand()%3);
				set_item(newunit, I_CROSSBOW, newunit->number);
				break;
			case 3:
				set_level(newunit, SK_LONGBOW, 1+rand()%3);
				set_item(newunit, I_LONGBOW, newunit->number);
				break;
			}
			if (rand() % 100 < 40) {
				set_item(newunit, I_CHAIN_MAIL, rand() % (newunit->number + 1));
			}
			if (rand() % 100 < 30) {
				set_item(newunit, I_HORSE, newunit->number);
				set_level(newunit, SK_RIDING, 1+rand()%3);
			}
			break;
		} else {
			if (eff_skill(u, SK_LONGBOW, r) < 3
				&& eff_skill(u, SK_HERBALISM, r) < 2
				&& eff_skill(u, SK_MAGIC, r) < 2) {
				return;
			}
			newunit = createunit(r, u->faction, rand() % 6 + 2, u->faction->race);
			set_string(&newunit->name, "Waldbewohner");
			set_money(newunit, (rand() % 20 + 10) * newunit->number);
			set_level(newunit, SK_LONGBOW, 2+rand()%3);
			set_item(newunit, I_LONGBOW, newunit->number);
			set_level(newunit, SK_OBSERVATION, 2+rand()%2);
			set_level(newunit, SK_STEALTH, 1+rand()%2);
			if (rand() % 100 < 20) {
				set_level(newunit, SK_HERBALISM, 1+rand()%2);
			}
		}
		break;

	case T_SWAMP:
		if (eff_skill(u, SK_OBSERVATION, r) <= 3) {
			return;
		}
		newunit = createunit(r, u->faction, rand() % 6 + 2, u->faction->race);
		set_string(&newunit->name, "Sumpfbewohner");
		set_money(newunit, (rand() % 20 + 10) * newunit->number);
		set_level(newunit, SK_SPEAR, 2+rand()%3);
		set_item(newunit, I_SPEAR, newunit->number);
		set_level(newunit, SK_STEALTH, 2+rand()%3);
		break;

	case T_DESERT:
		if (eff_skill(u, SK_RIDING, r) <= 2) {
			return;
		}
		newunit = createunit(r, u->faction, rand() % 12 + 2, u->faction->race);
		set_string(&newunit->name, "Berber");
		set_money(newunit, (rand() % 30 + 20) * newunit->number);
		set_level(newunit, SK_SWORD, 1+rand()%2);
		set_item(newunit, I_SWORD, newunit->number);
		set_level(newunit, SK_TRADE, 1+rand()%3);
		set_level(newunit, SK_RIDING, 2+rand()%2);
		set_item(newunit, I_HORSE, newunit->number);
		set_level(newunit, SK_HORSE_TRAINING, 2+rand()%2);
		break;

	case T_HIGHLAND:
		if (eff_skill(u, SK_SWORD, r) <= 1) {
			return;
		}
		newunit = createunit(r, u->faction, rand() % 8 + 2, u->faction->race);
		set_string(&newunit->name, "Hochlandbarbaren");
		set_money(newunit, (rand() % 10 + 20) * newunit->number);
		set_level(newunit, SK_SWORD, 1+rand()%2);
		set_item(newunit, I_SWORD, newunit->number);
		break;

	case T_MOUNTAIN:
		if (eff_skill(u, SK_SWORD, r) <= 1
			|| eff_skill(u, SK_TRADE, r) <= 2) {
			return;
		}
		newunit = createunit(r, u->faction, rand() % 6 + 2, u->faction->race);
		set_string(&newunit->name, "Bergbewohner");
		set_money(newunit, (rand() % 40 + 60) * newunit->number);
		set_level(newunit, SK_SWORD, 2+rand()%2);
		set_item(newunit, I_SWORD, newunit->number);
		set_level(newunit, SK_ARMORER, 2+rand()%2);
		set_level(newunit, SK_TRADE, 1+rand()%3);
		if (rand() % 100 < 60) {
			set_item(newunit, I_PLATE_ARMOR, newunit->number);
		}
		break;

	case T_GLACIER:
		if (eff_skill(u, SK_SWORD, r) <= 1
			|| eff_skill(u, SK_TRADE, r) <= 1) {
			return;
		}
		newunit = createunit(r, u->faction, rand() % 4 + 2, u->faction->race);
		set_string(&newunit->name, "Eisleute");
		set_money(newunit, (rand() % 20 + 20) * newunit->number);
		set_level(newunit, SK_SWORD, 2+rand()%2);
		set_item(newunit, I_SWORD, newunit->number);
		set_level(newunit, SK_ARMORER, 2+rand()%2);
		break;
	}

	u_setfaction(newunit, u->faction);
	set_racename(&newunit->attribs, get_racename(u->attribs));
	if(u->race->flags & RCF_SHAPESHIFT) {
		newunit->irace = u->irace;
	}
	if (fval(u, FL_PARTEITARNUNG)) fset(newunit, FL_PARTEITARNUNG);
	fset(u, UFL_ISNEW);

	sprintf(buf, "Pl�tzlich stolper%c %s �ber einige %s. Nach kurzem "
		"Z�gern entschlie�en sich die %s, sich Deiner Partei anzuschlie�en.",
	 u->number == 1 ? 't' : 'n', unitname(u), newunit->name, newunit->name);

	addmessage(r, u->faction, buf, MSG_EVENT, ML_IMPORTANT);
}

static void
encounter(region * r, unit * u)
{
	if (!fval(r, RF_ENCOUNTER)) return;
	freset(r, RF_ENCOUNTER);
	if (rand() % 100>=ENCCHANCE) return;
	switch (rand() % 3) {
	case 0:
		find_manual(r, u);
		break;
	case 1:
		get_unit(r, u);
		break;
	case 2:
		get_allies(r, u);
		break;
	}
}

void
encounters(void)
{
	region *r;
	unit *u;
	int n;
	int c;
	int i;

	for (r = regions; r; r = r->next) {
		if (rterrain(r) != T_OCEAN && fval(r, RF_ENCOUNTER)) {
			c = 0;
			for (u = r->units; u; u = u->next) {
				c += u->number;
			}

			if (c > 0) {
				n = rand() % c;
				u = r->units;

				for (i = u->number; i < n; i += u->number) {
					u = u->next;
				}

				encounter(r, u);
			}
		}
	}
}

void
chaos(region * r)
{
	unit *u = NULL, *u2;
	building *b, *b2;

	if (rand() % 100 < 8) {
		switch (rand() % 3) {
		case 0:				/* Untote */
			if (rterrain(r) != T_OCEAN) {
				u = random_unit(r);
				if (u && playerrace(u->race)) {
					sprintf(buf, "%s scheint von einer seltsamen Krankheit befallen.",
							unitname(u));
					addmessage(0, u->faction, buf, MSG_EVENT, ML_IMPORTANT);
					u_setfaction(u, findfaction(MONSTER_FACTION));
					u->race = new_race[RC_GHOUL];
				}
			}
			break;
		case 1:				/* Drachen */
			if (random_unit(r)) {
				int mfac = 0;
				switch (rand() % 3) {
				case 0:
					mfac = 100;
					u = createunit(r, findfaction(MONSTER_FACTION), rand() % 8 + 1, new_race[RC_FIREDRAGON]);
					if (u->number == 1) {
						set_string(&u->name, "Feuerdrache");
					} else {
						set_string(&u->name, "Feuerdrachen");
					}
					break;
				case 1:
					mfac = 500;
					u = createunit(r, findfaction(MONSTER_FACTION), rand() % 4 + 1, new_race[RC_DRAGON]);
					if (u->number == 1) {
						set_string(&u->name, "Drache");
					} else {
						set_string(&u->name, "Drachen");
					}
					break;
				case 2:
					mfac = 1000;
					u = createunit(r, findfaction(MONSTER_FACTION), rand() % 2 + 1, new_race[RC_WYRM]);
					if (u->number == 1) {
						set_string(&u->name, "Wyrm");
					} else {
						set_string(&u->name, "Wyrme");
					}
					break;
				}
				if (mfac) set_money(u, u->number * (rand() % mfac));
				guard(u, GUARD_ALL);
			}
		case 2:	/* Terrainver�nderung */
			if (!(terrain[rterrain(r)].flags & FORBIDDEN_LAND)) {
				if (rterrain(r) != T_OCEAN) {
					direction_t dir;
					for (dir=0;dir!=MAXDIRECTIONS;++dir) {
						if (rconnect(r, dir) && rterrain(rconnect(r, dir)) == T_OCEAN) break;
					}
					if (dir!=MAXDIRECTIONS) {
						ship * sh = r->ships;
						while (sh) {
							ship * nsh = sh->next;
							damage_ship(sh, 0.50);
							if (sh->damage >= sh->size * DAMAGE_SCALE) destroy_ship(sh, r);
							sh = nsh;
						}

						for (u = r->units; u;) {
							u2 = u->next;
							if (u->race != new_race[RC_SPELL] && u->ship == 0) {
								set_number(u, 0);
							}
							u = u2;
						}
						sprintf(buf, "Ein gewaltige Flutwelle verschlingt %s und "
							"alle Bewohner.", regionid(r));
						addmessage(r, 0, buf, MSG_EVENT, ML_IMPORTANT);

						for (b = rbuildings(r); b;) {
							b2 = b->next;
							destroy_building(b);
							b = b2;
						}
						terraform(r, T_OCEAN);
					}
				} else {
					direction_t dir;
					for (dir=0;dir!=MAXDIRECTIONS;++dir) {
						if (rconnect(r, dir) && rterrain(rconnect(r, dir)) != T_OCEAN) break;
					}
					if (dir!=MAXDIRECTIONS) {
						switch (rand() % 8) {
						case 0:
							terraform(r, T_PLAIN);
							break;
						case 1:
							terraform(r, T_HIGHLAND);
							break;
						case 2:
							terraform(r, T_MOUNTAIN);
							break;
						case 3:
							terraform(r, T_GLACIER);
							break;
						case 4:
							terraform(r, T_DESERT);
							break;
						default:
							terraform(r, T_SWAMP);
							break;
						}
					}
				}
			}
		}
	}
}

double
chaosfactor(region * r)
{
	attrib * a = a_find(r->attribs, &at_chaoscount);
	if (!a) return 0;
	return ((double) a->data.i / 1000.0);
}

void
drown(region *r)
{
	if (rterrain(r) == T_OCEAN) {
		unit ** up = up=&r->units;
		while (*up) {
			unit *u = *up;
			int amphibian_level = fspecial(u->faction, FS_AMPHIBIAN);

			if (u->ship || u->race == new_race[RC_SPELL]) {
				up=&u->next;
				continue;
			}

			if (amphibian_level) {
				int dead = damage_unit(u, "5d1", false, false);
				if (dead) {
					ADDMSG(&u->faction->msgs, new_message(u->faction,
						"drown_amphibian_dead%d:amount%u:unit%r:region",dead, u, r));
				} else {
					ADDMSG(&u->faction->msgs, new_message(u->faction,
						"drown_amphibian_nodead%u:unit%r:region",u, r));
				}
			} else if (!canswim(u)) {
				scale_number(u, 0);
				ADDMSG(&u->faction->msgs, new_message(u->faction,
					"drown%u:unit%r:region", u, r));
			}
			if (*up==u) up=&u->next;
		}
		remove_empty_units_in_region(r);
	}
}

region *
rrandneighbour(region *r)
{
	direction_t i;
	region *rc = NULL;
	int rr, c = 0;

	/* Nachsehen, wieviele Regionen in Frage kommen */

	for (i = 0; i != MAXDIRECTIONS; i++) {
		c++;
	}
	/* Zuf�llig eine ausw�hlen */

	rr = rand() % c;

	/* Durchz�hlen */

	c = -1;
	for (i = 0; i != MAXDIRECTIONS; i++) {
		rc = rconnect(r, i);
		c++;
		if (c == rr) break;
	}
	assert(i!=MAXDIRECTIONS);
	return rc;
}

void
volcano_outbreak(region *r)
{
	attrib *a;
	region *rn;
	unit *u, **up;
	faction *f;

	for (u=r->units; u; u=u->next) {
		f = u->faction;
		freset(f, FL_DH);
	}
	rn = rrandneighbour(r);

	/* Vulkan-Region verw�sten */

#if GROWING_TREES
	rsettrees(r, 2, 0);
	rsettrees(r, 1, 0);
	rsettrees(r, 0, 0);
#else
	rsettrees(r, 0);
#endif

	a = a_find(r->attribs, &at_reduceproduction);
	if (!a) a = a_add(&r->attribs, a_new(&at_reduceproduction));

	/* Produktion vierteln ... */
	a->data.sa[0] = 25;
	/* F�r 6-17 Runden */
	a->data.sa[1] = (short)(a->data.sa[1] + 6 + rand()%12);

	/* Personen bekommen 4W10 Punkte Schaden. */

	for (up=&r->units; *up;) {
		unit * u = *up;
		int dead = damage_unit(u, "4d10", true, false);
		if (dead) {
			ADDMSG(&u->faction->msgs, new_message(u->faction,
				"volcano_dead%u:unit%r:region%i:dead", u, r, dead));
		}
		if (!fval(u->faction, FL_DH)) {
			fset(u->faction, FL_DH);
			if (rn) {
				ADDMSG(&u->faction->msgs, new_message(u->faction,
					"volcanooutbreak%r:regionv%r:regionn", r, rn));
			} else {
				ADDMSG(&u->faction->msgs, new_message(u->faction,
					"volcanooutbreaknn%r:region", r));
			}
		}
		if (u==*up) up=&u->next;
	}

	remove_empty_units_in_region(r);

	/* Zuf�llige Nachbarregion verw�sten */

	if (rn) {

#if GROWING_TREES
		rsettrees(r, 2, 0);
		rsettrees(r, 1, 0);
		rsettrees(r, 0, 0);
#else
		rsettrees(r, 0);
#endif

		a = a_add(&rn->attribs, a_new(&at_reduceproduction));
		if (!a) a = a_add(&r->attribs, a_new(&at_reduceproduction));

		/* Produktion vierteln ... */
		a->data.sa[0] = 25;
		/* F�r 6-17 Runden */
		a->data.sa[1] = (short)(a->data.sa[1] + 6 + rand()%12);

		/* Personen bekommen 3W10 Punkte Schaden. */
		for (up=&rn->units; *up;) {
			unit * u = *up;
			int dead = damage_unit(u, "3d10", true, false);
			if (dead) {
				ADDMSG(&u->faction->msgs, new_message(u->faction,
					"volcano_dead%u:unit%r:region%i:dead", u, rn, dead));
			}
			if (!fval(u->faction, FL_DH)) {
				ADDMSG(&u->faction->msgs, new_message(u->faction,
					"volcano_dead%u:unit%r:region%i:dead", u, r, dead));
				fset(u->faction, FL_DH);
			}
			if (u==*up) up=&u->next;
		}
		remove_empty_units_in_region(rn);
	}
}

static void
melt_iceberg(region *r)
{
	attrib *a;
	unit *u;
	building *b, *b2;

	for (u=r->units; u; u=u->next) freset(u->faction, FL_DH);
	for (u=r->units; u; u=u->next) if (!fval(u->faction, FL_DH)) {
		fset(u->faction, FL_DH);
		ADDMSG(&u->faction->msgs, new_message(u->faction,
			"iceberg_melt%r:region", r));
	}

	/* driftrichtung l�schen */
	a = a_find(r->attribs, &at_iceberg);
	if (a) a_remove(&r->attribs, a);

	/* Geb�ude l�schen */
	for (b = rbuildings(r); b; b = b2) {
		b2 = b->next;
		destroy_building(b);
	}

	/* in Ozean wandeln */
	terraform(r, T_OCEAN);

	/* Einheiten, die nicht schwimmen k�nnen oder in Schiffen sind,
	 * ertrinken */
	drown(r);
}

static void
move_iceberg(region *r)
{
	attrib *a;
	direction_t dir;
	region *rc;

	a = a_find(r->attribs, &at_iceberg);
	if (!a) {
		dir = (direction_t)(rand()%MAXDIRECTIONS);
		a = a_add(&r->attribs, make_iceberg(dir));
	} else {
		if (rand()%100 < 20) {
			dir = (direction_t)(rand()%MAXDIRECTIONS);
			a->data.i = dir;
		} else {
			dir = (direction_t)a->data.i;
		}
	}

	rc = rconnect(r, dir);

	if (rc && rterrain(rc) != T_GLACIER && rterrain(rc) != T_ICEBERG
			&& rterrain(rc) != T_ICEBERG_SLEEP) {
		if (rterrain(rc) == T_OCEAN) {	/* Eisberg treibt */
			ship *sh, *shn;
			unit *u;
			int x, y;


			for (u=r->units; u; u=u->next) freset(u->faction, FL_DH);
			for (u=r->units; u; u=u->next) if (!fval(u->faction, FL_DH)) {
				fset(u->faction, FL_DH);
				ADDMSG(&u->faction->msgs, new_message(u->faction,
					"iceberg_drift%r:region%d:dir", r, dir));
			}

			x = r->x;
			y = r->y;

			runhash(r);
			runhash(rc);
			r->x = rc->x;
			r->y = rc->y;
			rc->x = x;
			rc->y = y;
			rhash(rc);
			rhash(r);

			/* rc ist der Ozean (Ex-Eisberg), r der Eisberg (Ex-Ozean) */

			/* Schiffe aus dem Zielozean werden in den Eisberg transferiert
			 * und nehmen Schaden. */

			for (sh = r->ships; sh; sh=sh->next) freset(sh, FL_DH);

			for (sh = r->ships; sh; sh = sh->next) {
				/* Meldung an Kapit�n */
				damage_ship(sh, 0.10);
				fset(sh, FL_DH);
			}

			/* Personen, Schiffe und Geb�ude verschieben */
			while (rc->buildings) {
				rc->buildings->region = r;
				translist(&rc->buildings, &r->buildings, rc->buildings);
			}
			while (rc->ships) {
				fset(rc->ships, FL_DH);
				damage_ship(rc->ships, 0.10);
				move_ship(rc->ships, rc, r, NULL);
			}
			while (rc->units) {
				building * b = rc->units->building;
				u = rc->units;
				move_unit(rc->units, r, NULL);
				u->building = b; /* move_unit macht ein leave() */
			}

			/* Besch�digte Schiffe k�nnen sinken */

			for (sh = r->ships; sh;) {
				shn = sh->next;
				if (fval(sh, FL_DH)) {
					u = captain(sh, r);
					if (sh->damage>=sh->size * DAMAGE_SCALE) {
						if (u) ADDMSG(&u->faction->msgs, new_message(u->faction,
							"overrun_by_iceberg_des%h:ship", sh));
						destroy_ship(sh, r);
					} else {
						if (u) ADDMSG(&u->faction->msgs, new_message(u->faction,
							"overrun_by_iceberg%h:ship", sh));
					}
				}
				sh = shn;
			}

		} else if (rand()%100 < 20) {	/* Eisberg bleibt als Gletscher liegen */
			unit *u;

			rsetterrain(r, T_GLACIER);
			a_remove(&r->attribs, a);

			for (u=r->units; u; u=u->next) freset(u->faction, FL_DH);
			for (u=r->units; u; u=u->next) if (!fval(u->faction, FL_DH)) {
				fset(u->faction, FL_DH);
				ADDMSG(&u->faction->msgs, new_message(u->faction,
					"iceberg_land%r:region", r));
			}
		}
	}
}

void
move_icebergs(void)
{
	region *r;

	for (r=regions; r; r=r->next) if (rterrain(r) == T_ICEBERG && !fval(r, RF_DH)) {
		if (rand()%100 < 60) {
			fset(r, RF_DH);
			move_iceberg(r);
		} else if (rand()%100 < 10){
			fset(r, RF_DH);
			melt_iceberg(r);
		}
	}
}

void
create_icebergs(void)
{
	region *r;

	for (r=regions; r; r=r->next) if (rterrain(r) == T_ICEBERG_SLEEP && rand()%100 < 5) {
		boolean has_ocean_neighbour = false;
		direction_t dir;
		region *rc;
		unit *u;

		for (dir=0; dir < MAXDIRECTIONS; dir++) {
			rc = rconnect(r, dir);
			if (rc && rterrain(rc) == T_OCEAN) {
				has_ocean_neighbour = true;
				break;
			}
		}
		if (!has_ocean_neighbour) continue;

		rsetterrain(r, T_ICEBERG);

		fset(r, RF_DH);
		move_iceberg(r);

		for (u=r->units; u; u=u->next) freset(u->faction, FL_DH);
		for (u=r->units; u; u=u->next) if (!fval(u->faction, FL_DH)) {
			fset(u->faction, FL_DH);
			ADDMSG(&u->faction->msgs, msg_message("iceberg_create", "region", r));
		}
	}
}

void
godcurse(void)
{
	region *r;
	ship *sh, *shn;

	for(r=regions; r; r=r->next) {
		if(is_cursed(r->attribs, C_CURSED_BY_THE_GODS, 0)) {
			unit * u;
			for(u=r->units; u; u=u->next) {
				skill * sv = u->skills;
				while (sv!=u->skills+u->skill_size) {
					int weeks = 1+rand()%3;
					reduce_skill(u, sv, weeks);
					++sv;
				}
			}
		}
	}

	if (rterrain(r) == T_OCEAN) {
		for (sh = r->ships; sh;) {
			shn = sh->next;
			damage_ship(sh, 0.10);
			if (sh->damage>=sh->size * DAMAGE_SCALE) {
				unit * u = shipowner(r, sh);
				if (u) ADDMSG(&u->faction->msgs,
					msg_message("godcurse_destroy_ship", "ship", sh));
				destroy_ship(sh, r);
			}
			sh = shn;
		}
	}
}

void
randomevents(void)
{
	faction *f;
	region *r;
	building *b, *b2;
	unit *u;

	/* Eiseberge */
	for (r=regions; r; r=r->next) freset(r, RF_DH);
	create_icebergs();
	move_icebergs();

	godcurse();

	for (r=regions; r; r=r->next) {
		drown(r);
	}

	for (r = regions; r; r = r->next) {
		for (u = r->units; u; u = u->next) {
			curse *c = get_curse(u->attribs, ct_find("orcish"));
			if (c && !has_skill(u, SK_MAGIC) && !has_skill(u, SK_ALCHEMY)) {
				int n;
				int increase = 0;
				int num  = get_cursedmen(u, c);
				int prob = curse_geteffect(c);

				for (n = (num - get_item(u, I_CHASTITY_BELT)); n > 0; n--) {
					if (rand() % 100 < prob) {
						++increase;
					}
				}
				if (increase) {
					set_number(u, u->number + increase);

					u->hp += unit_max_hp(u) * increase;
					ADDMSG(&u->faction->msgs, msg_message("orcgrowth",
						"unit amount race", u, increase, u->race));
				}
			}
		}
	}

#if RACE_ADJUSTMENTS == 0
	/* Orks vermehren sich */

	for (r = regions; r; r = r->next) {

		plane * p = rplane(r);
		/* there is a flag for planes without orc growth: */
		if (p && (p->flags & PFL_NOORCGROWTH)) continue;
		for (u = r->units; u; u = u->next) {
			if ( (u->race == new_race[RC_ORC])
					&& !has_skill(u, SK_MAGIC) && !has_skill(u, SK_ALCHEMY)) {
				int n;
				int increase = 0;
				int num  = u->number;
				int prob = 5;

				for (n = (num - get_item(u, I_CHASTITY_BELT)); n > 0; n--) {
					if (rand() % 100 < prob) {
						++increase;
					}
				}
				if (increase) {
					if (u->race == new_race[RC_ORC]) {
						int i;
						struct orcskills {
							skill_t skill;
							int level;
						} skills [] = { 
							{ SK_SWORD, 1 }, { SK_SPEAR, 1 }, { SK_TACTICS, 0 }, 
							{ SK_LONGBOW, 0 }, { SK_CROSSBOW, 0 }, { SK_CATAPULT, 0 }, 
							{ SK_AUSDAUER, 0 }, { NOSKILL, 0 }
						};
						for (i=0;skills[i].skill!=NOSKILL;++i) {
							int k = get_level(u, skills[i].skill);
							change_skill(u, skills[i].skill, increase * max(k, s));
						}
					}

					set_number(u, u->number + increase);

					u->hp += unit_max_hp(u) * increase;
					ADDMSG(&u->faction->msgs, msg_message("orcgrowth",
						"unit amount race", u, increase, u->race));
				}
			}
		}
	}
#endif

	/* Talente von D�monen verschieben sich und D�monen fressen Bauern */

	for (r = regions; r; r = r->next) {
		int peasantfood = rpeasants(r)*10;
		int bauernblut = 0;
		boolean bfind = false;
		for (u = r->units; u; u = u->next) {
			if (u->race == new_race[RC_DAEMON]) {
				/* Alles Bauernblut der Region z�hlen.
				 * warnung: bauernblut einer partei hilft im moment der anderen
				 * so selten wie das benutzt wird, ist das erstmal wursht,
				 * aber ein TODO f�rs BUGS File.
				 * Es ist auch deshalb fast egal, weil es ja im Grunde nicht dem D�mon,
				 * sondern der Region zu gute kommt - und da ist der anwender schnuppe
				 */
				skill * sv;
				if (!bfind) {
					unit * ud = u;
					while (ud) {
						attrib * a = a_find(ud->attribs, &at_bauernblut);
						if (a) bauernblut += a->data.i;
						do { ud=ud->next; } while (ud && ud->race!=new_race[RC_DAEMON]);
					}
					bfind = true;
				}
				if (r->planep==0 || !fval(r->planep, PFL_NOFEED)) {
					int demons = u->number;
					if (bauernblut>=demons) {
						bauernblut -= demons;
						demons = 0;
					} else if (bauernblut) {
						demons-=bauernblut;
					}
					if (peasantfood>=demons) {
						peasantfood -= demons;
						demons = 0;
					} else {
						demons-=peasantfood;
					}
					if (demons > 0) {
#ifdef DAEMON_HUNGER
						hunger(u, demons); /* nicht gef�tterte d�monen hungern */
#else
						c = 0;
						for (n = 0; n != demons; n++) {
							if (rand() % 100 < 10) {
								c++;
							}
						}
						if (c) {
							scale_number(u, u->number - c);
							sprintf(buf, "%d D�monen von %s sind hungrig in "
								"ihre Sph�re zur�ckgekehrt.", c, unitname(u));
							addmessage(0, u->faction, buf, MSG_EVENT, ML_IMPORTANT);
						}
#endif
					}
				}
				sv = u->skills;
				while (sv!=u->skills+u->skill_size) {
					if (sv->level>0 && rand() % 100 < 25) {
						int weeks = 1+rand()%3;
						if (rand() % 100 < 40) {
							reduce_skill(u, sv, weeks);
						} else {
							while (weeks--) learn_skill(u, sv->id, 1.0);
						}
					}
					++sv;
				}
			}
		}
		rsetpeasants(r, peasantfood/10);
	}

	for (r = regions; r; r = r->next) {
#if !RACE_ADJUSTMENTS
	/* Elfen generieren Wald */
		if (r->land && !fval(r, RF_MALLORN)) {
#if GROWING_TREES
			int trees = rtrees(r, 2);
#else
			int trees = rtrees(r);
#endif
			int maxgen = (production(r) * MAXPEASANTS_PER_AREA)/8;
			for (u = r->units; u && maxgen > 0; u = u->next) {
				if (u->race == new_race[RC_ELF]) {
					for (n = u->number; n && maxgen > 0; n--) {
						if (rand() % 1000 < 15) {	/* 1.5% Chance */
							trees++;
						}
						maxgen--;
					}
				}
			}
#if GROWING_TREES
			rsettrees(r, 2, trees);
#else
			rsettrees(r, trees);
#endif /* GROWING_TREES */
		} /* !RACE_ADJUSTMENTS */
#endif

		for (u=r->units; u; u=u->next) {
			if (!(u->race->ec_flags & NOGIVE)) {
				struct building * b = inside_building(u);
				const struct building_type * btype = b?b->type:NULL;
				if (btype == bt_find("blessedstonecircle")) {
					int n, c = 0;
					for (n=0; n<u->number; n++) if (rand()%100 < 2) {
						change_item(u, I_UNICORN, 1);
						c++;
					}
					if (c) {
						ADDMSG(&u->faction->msgs, new_message(u->faction,
							"scunicorn%u:unit%i:amount%X:type",u,c,
							olditemtype[I_UNICORN]->rtype));
					}
				}
			}
		}
	}

	/* Orkifizierte Regionen mutieren und mutieren zur�ck */

	for (r = regions; r; r = r->next) {
		if (fval(r, RF_ORCIFIED)) {
			direction_t dir;
			int chance = 0;
			for (dir = 0; dir < MAXDIRECTIONS; dir++) {
				region *rc = rconnect(r, dir);
				if (rc && rpeasants(rc) > 0 && !fval(rc, RF_ORCIFIED)) chance += 2;
			}
			if (rand()%100 < chance) {
				ADDMSG(&r->msgs, msg_message("deorcified", "region", r));
				freset(r, RF_ORCIFIED);
			}
		} else {
			attrib *a = a_find(r->attribs, &at_orcification);
			if (a!=NULL) {
				int chance = 0;
				if (rpeasants(r) <= 0) continue;
				chance = (a->data.i*100)/rpeasants(r);
				if (rand()%100 < chance) {
					fset(r, RF_ORCIFIED);
					a_remove(&r->attribs, a);
					ADDMSG(&r->msgs, msg_message("orcified", "region", r));
				} else {
					a->data.i -= max(10,a->data.i/10);
					if (a->data.i <= 0) a_remove(&r->attribs, a);
				}
			}
		}
	}

	/* Vulkane qualmen, brechen aus ... */
	for (r = regions; r; r = r->next) {
		if (rterrain(r)==T_VOLCANO_SMOKING && a_find(r->attribs, &at_reduceproduction)) {
			ADDMSG(&r->msgs, msg_message("volcanostopsmoke", "region", r));
			rsetterrain(r, T_VOLCANO);
		} else switch(rterrain(r)) {
		case T_VOLCANO:
			if (rand()%100 < 4) {
				ADDMSG(&r->msgs, msg_message("volcanostartsmoke", "region", r));
				rsetterrain(r, T_VOLCANO_SMOKING);
			}
			break;
		case T_VOLCANO_SMOKING:
			if (rand()%100 < 12) {
				ADDMSG(&r->msgs, msg_message("volcanostopsmoke", "region", r));
				rsetterrain(r, T_VOLCANO);
			} else if (rand()%100 < 8) {
				volcano_outbreak(r);
			}
			break;
		}
	}

	/* Monumente zerfallen, Schiffe verfaulen */

	for (r = regions; r; r = r->next) {
		for (b = rbuildings(r); b; b = b2) {
			b2 = b->next;
			if (fval(b->type, BTF_DECAY) && !buildingowner(r, b)) {
				b->size -= max(1, (b->size * 20) / 100);
				if (b->size == 0) {
					destroy_building(b);
				}
			}
		}
	}

	/* Drachen und Seeschlangen k�nnen entstehen */

	printf("\n");

	for (r = regions; r; r = r->next) {
		unit * u;
		if (rterrain(r) == T_OCEAN && rand()%10000 < 1) {
			u = createunit(r, findfaction(MONSTER_FACTION), 1, new_race[RC_SEASERPENT]);
			set_level(u, SK_MAGIC, 4);
			set_level(u, SK_OBSERVATION, 3);
			set_level(u, SK_STEALTH, 2);
			set_level(u, SK_AUSDAUER, 1);
			set_string(&u->name, "Seeschlange");
		}

		if ((rterrain(r) == T_GLACIER
				|| rterrain(r) == T_SWAMP || rterrain(r) == T_DESERT)
		    && rand() % 10000 < (5 + 100 * chaosfactor(r))) {

			switch (rand() % 10) {
			case 0:
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
				u = createunit(r, findfaction(MONSTER_FACTION), nrand(60, 20) + 1, new_race[RC_FIREDRAGON]);
				break;
			default:
				u = createunit(r, findfaction(MONSTER_FACTION), nrand(30, 20) + 1, new_race[RC_DRAGON]);
				break;
			}

			set_money(u, u->number * (rand() % 500 + 100));
			set_level(u, SK_MAGIC, 4);
			set_level(u, SK_OBSERVATION, 1+rand()%3);
			set_level(u, SK_STEALTH, 1);
			set_level(u, SK_AUSDAUER, 1);
			log_printf("%d %s in %s.\n", u->number,
				LOC(default_locale, rc_name(u->race, u->number!=1)), regionname(r, NULL));

			name_unit(u);
			set_string(&u->lastorder, "WARTEN");

			if (u->number == 1) {
				sprintf(buf, "Es wurde ein %s gesichtet.",
					LOC(default_locale, rc_name(u->race, 0)));
			} else {
				sprintf(buf, "Es wurden %d %s gesichtet.",
					u->number, LOC(default_locale, rc_name(u->race, u->number!=1)));
			}
			addmessage(r, 0, buf, MSG_COMMENT, ML_IMPORTANT);
			if (u->number == 1) {
				sprintf(buf, "In %s wurde ein %s gesichtet.", regionid(r),
					LOC(default_locale, rc_name(u->race, u->number!=1)));
			} else {
				sprintf(buf, "In %s wurden %d %s gesichtet.", regionid(r),
					u->number, LOC(default_locale, rc_name(u->race, u->number!=1)));
			}
			for (u=r->units;u;u=u->next) freset(u->faction, FL_DH);
			for (u=r->units;u;u=u->next) {
				faction * f = u->faction;
				if (!fval(f, FL_DH)) {
					addmessage(0, f, buf, MSG_EVENT, ML_IMPORTANT);
					fset(f, FL_DH);
				}
			}
		}
	}

	/* Untote k�nnen entstehen */

	for (r = regions; r; r = r->next) {
		int unburied = deathcount(r);

		if(is_cursed(r->attribs, C_HOLYGROUND, 0)) continue;

		/* Chance 0.1% * chaosfactor */
		if (r->land && unburied > r->land->peasants / 20 && rand() % 10000 < (100 + 100 * chaosfactor(r))) {
			/* es ist sinnfrei, wenn irgendwo im Wald 3er-Einheiten Untote entstehen.
			 * Lieber sammeln lassen, bis sie mindestens 5% der Bev�lkerung sind, und
			 * dann erst auferstehen. */
			int undead = unburied / (rand() % 2 + 1);
			const race * rc;
			int i;

			if (!undead || r->age < 20) continue;

			switch(rand()%3) {
			case 0:
				rc = new_race[RC_SKELETON]; break;
			case 1:
				rc = new_race[RC_ZOMBIE]; break;
			default:
				rc = new_race[RC_GHOUL]; break;
			}

			u = createunit(r, findfaction(MONSTER_FACTION), undead, rc);
			if ((rc == new_race[RC_SKELETON] || rc == new_race[RC_ZOMBIE]) && rand()%10 < 4) {
				set_item(u, I_RUSTY_SWORD, undead);
				if (rand()%10 < 3) {
					set_item(u, I_RUSTY_SHIELD, undead);
				}
				if (rand()%10 < 2) {
					set_item(u, I_RUSTY_CHAIN_MAIL, undead);
				}
			}

			for (i=0;i < MAXSKILLS;i++) {
				if (rc->bonus[i] >= 1) {
					set_level(u, (skill_t)i, 1);
				}
			}
			u->hp = unit_max_hp(u) * u->number;

			deathcounts(r, -undead);
			set_string(&u->lastorder, "WARTEN");
			name_unit(u);

			log_printf("%d %s in %s.\n", u->number,
				LOC(default_locale, rc_name(u->race, u->number!=1)), regionname(r, NULL));

			{
				message * msg = msg_message("undeadrise", "region amount", r, undead);
				add_message(&r->msgs, msg);
				for (u=r->units;u;u=u->next) freset(u->faction, FL_DH);
				for (u=r->units;u;u=u->next) {
					if (fval(u->faction, FL_DH)) continue;
					fset(u->faction, FL_DH);
					add_message(&u->faction->msgs, msg);
				}
				msg_release(msg);
			}
		} else {
			int i = deathcount(r);
			if (i) {
				/* Gr�ber verwittern, 3% der Untoten finden die ewige Ruhe */
				deathcounts(r, (int)(-i*0.03));
			}
		}
	}

	for (r = regions; r; r=r->next) {
		for (u=r->units; u; u=u->next) {
			if (u->faction->no != MONSTER_FACTION
					&& (u->race->flags & RCF_DESERT)) {
				if (fval(u, UFL_ISNEW)) continue;
				if (rand()%100 < 5) {
					ADDMSG(&u->faction->msgs, msg_message("desertion",
						"unit region", u, r));
					u_setfaction(u, findfaction(MONSTER_FACTION));
				}
			}
		}
	}
	
	for (f = factions; f; f=f->next) {
		int level = fspecial(f, FS_LYCANTROPE);
		if(level > 0) {
			for(u = f->units; u; u=u->nextF) {
				if(rand()%100 < 2*level) {
					ADDMSG(&u->faction->msgs, msg_message("becomewere",
						"unit region", u, u->region));
					fset(u, UFL_WERE);
				}
			}
		}
	}

	/* Fr�hling, die B�ume schlagen aus. */

	for (r = regions; r; r = r->next) {
		if (fval(r, RF_CHAOTIC) ||(r->x >= -13  && r->x <= -6  && r->y >= 50 && r->y <= 57)) {
			if (woodcount(r) >= 40 && rand()%100 < 33) {
#if GROWING_TREES
				int trees = rtrees(r,2);
#else
				int trees = rtrees(r);
#endif
				int treemen = rand()%(max(50,trees)/3);
				struct message * msg;

				treemen = max(25, treemen);
				woodcounts(r, -40);
				trees = max(0, trees-treemen);
#if GROWING_TREES
				rsettrees(r, 2, trees);
#else
				rsettrees(r, trees);
#endif
				u = createunit(r, findfaction(MONSTER_FACTION),treemen, new_race[RC_TREEMAN]);
				set_string(&u->lastorder, "WARTEN");
				/* guard(u, GUARD_ALL); kein auto-guard! erst in monster.c! */

				set_level(u, SK_OBSERVATION, 2);
				if (u->number == 1)
					set_string(&u->name, "Ein w�tender Ent");
				else
					set_string(&u->name, "W�tende Ents");

				log_printf("%d Ents in %s.\n", u->number, regionname(r, NULL));

				msg = msg_message("entrise", "region amount", r, u->number);
				add_message(&r->msgs, msg);
				for (u=r->units;u;u=u->next) freset(u->faction, FL_DH);
				for (u=r->units;u;u=u->next) {
					if (fval(u->faction, FL_DH)) continue;
					fset(u->faction, FL_DH);
					add_message(&u->faction->msgs, msg);
				}
				msg_release(msg);
			}
		}
	}

	/* Chaos */

	for (r = regions; r; r = r->next) {
		int i;
		if (fval(r, RF_CHAOTIC)) {
			chaos(r);
		}
		i = chaoscount(r);
		if (!i) continue;
		chaoscounts(r, -(int) (i * ((double) (rand() % 10)) / 100.0));
	}

#ifdef HERBS_ROT
	/* Kr�uter verrotten */
	for (r = regions; r; r = r->next) {
		for (u = r->units; u; u=u->next) {
			item **itmp = &u->items, *hbag = *i_find(&u->items, olditemtype[I_SACK_OF_CONSERVATION]);
			int rot_chance = HERBROTCHANCE;

			if (hbag) rot_chance = (HERBROTCHANCE*2)/5;

			while (*itmp) {
				item * itm = *itmp;
				const herb_type * htype = resource2herb(itm->type->rtype);
				int n = itm->number;
				double k = n*rot_chance/100.0;
				if  (htype!=NULL) {
					n = (int)(min(n, normalvariate(k, k/4)));
					i_change(itmp, itm->type, -n);
				}
				if (itm==*itmp) itmp=&itm->next;
			}
	  }
	}
#endif


	dissolve_units();
	check_split();
	check_luck();
}

#if NEW_LAEN
void growlaen(void) {
	region *r;
	regionlist *Berge=NULL, *rl;
	unit *u;
	int b=0, Laen, z, add;
	short *add_laen;

	for (r = regions; r; r = r->next) {
		if (!fval(r, RF_CHAOTIC) &&
			rterrain(r) == T_MOUNTAIN)
		{
			add_regionlist(&Berge,r);
			b++;
		}
	}

	add_laen=(short *)malloc(b*sizeof(short));
	memset(add_laen, 0, b*sizeof(short));

	Laen=b*MAXLAENPERTURN/14; /* Anzahl Berge * MAXLAENPERTURN/2 * Chance pro Berg */

	while(Laen) { /* Laenverteilung �ber alle Berge */
		z = rand()%b;
		add = min(10 + rand() % 30, Laen);
		add_laen[z] = (short)(add_laen[z]+add);
		Laen -= add;
	}

	z=0;
	for (rl=Berge; rl; rl=rl->next, z++) {
		region * r = rl->region;
		if (add_laen[z]>0) {
			if (a_find(rl->region->attribs, &at_laen))
			rsetlaen(rl->region, add_laen[z]+rlaen(rl->region));
		}
		else {
			attrib *a=a_new(&at_laen);
			struct message * msg = NULL;
			a_add(&rl->region->attribs, a);
			rsetlaen(rl->region, add_laen[z]);

			/* Meldungen generieren */
			for (u=r->units;u;u=u->next) freset(u->faction, FL_DH);
			for (u = r->units; u; u = u->next ) {
				if (!fval(u->faction, FL_DH) && eff_skill(u, SK_MINING, rl->region) >= olditemtype[I_LAEN]->minskill)
				{
					if (!msg) msg = msg_message("unveileog", "unit region", u, rl->region);
					r_addmessage(rl->region, u->faction, msg);
					fset(u->faction, FL_DH);
				}
			}
			if (msg) msg_release(msg);
		}
	}
  free(add_laen);
  free_regionlist(Berge);

}
#endif
