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
 * This program may not be used, modified or distributed without
 * prior permission by the authors of Eressea.
 */

#define SHOW_KILLS
#undef DELAYED_OFFENSE /* non-guarding factions cannot attack after moving */

#define TACTICS_RANDOM 5 /* define this as 1 to deactivate */
#define CATAPULT_INITIAL_RELOAD 4 /* erster schuss in runde 1 + rand() % INITIAL */
#define CATAPULT_STRUCTURAL_DAMAGE

#define BASE_CHANCE  70			/* 70% �berlebenschance */
#define TDIFF_CHANGE 10


typedef enum combatmagic {
	DO_PRECOMBATSPELL,
	DO_POSTCOMBATSPELL
} combatmagic_t;


#include <config.h>
#include "eressea.h"
#include "battle.h"

#include "item.h"
#include "alchemy.h"
#include "plane.h"
#include "magic.h"
#include "faction.h"
#include "reports.h"
#include "build.h"
#include "race.h"
#include "movement.h"
#include "movement.h"
#include "names.h"
#include "region.h"
#include "skill.h"
#include "goodies.h"
#include "unit.h"
#include "message.h"
#include "curse.h"
#include "spell.h"
#include "karma.h"
#include "ship.h"
#include "building.h"
#include "group.h"

/* util includes */
#include <base36.h>
#include <cvector.h>
#include <rand.h>
#include <log.h>

/* attributes includes */
#include <attributes/key.h>
#include <attributes/racename.h>
#include <attributes/otherfaction.h>
#ifdef AT_MOVED
# include <attributes/moved.h>
#endif

/* libc includes */
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <message.h>
#include <stdlib.h>
#include <string.h>

#if !defined(AT_MOVED) && defined(DELAYED_OFFENSE)
# error "must define AT_MOVED to use combat option DELAYED_OFFENSE"
#endif

#ifdef HAVE_ZLIB
# include <zlib.h>
# define dbgprintf(a) gzprintf a;
gzFile bdebug;
#elif HAVE_BZ2LIB
# include <bzlib.h>
# define dbgprintf(a) bz2printf a;
BZFILE *bdebug;
#else
# define dbgprintf(a) fprintf a;
FILE *bdebug;
#endif

/* TODO: header cleanup */
extern int dice_rand(const char *s);
extern item_type it_demonseye;

int  obs_count = 0;
boolean nobattledebug = false;

#define TACTICS_MALUS
#undef  MAGIC_TURNS

#define MINSPELLRANGE 1
#define MAXSPELLRANGE 7

#define COMBATEXP         0

#define COMBAT_TURNS			10

static const double EFFECT_PANIC_SPELL = 0.25;
static const double TROLL_REGENERATION = 0.10;

#define MAX_ADVANTAGE           5

enum {
	SI_DEFENDER,
	SI_ATTACKER
};

extern weapon_type * oldweapontype[];

/* Nach dem alten System: */
static int missile_range[2] = {FIGHT_ROW, BEHIND_ROW};
static int melee_range[2] = {FIGHT_ROW, FIGHT_ROW};

typedef struct armor_type {
	double penalty;
	double magres;
	int prot;
	char shield;
	char item;
} armor_type;

static armor_type armordata[AR_NONE + 1] =
/* penalty; magres; prot; shield; item; */
{
#ifdef COMPATIBILITY
	{-0.80, 5, 0, I_CLOAK_OF_INVULNERABILITY },
#endif
	{ 0.30, 0.00, 5, 0, I_PLATE_ARMOR},
	{ 0.15, 0.00, 3, 0, I_CHAIN_MAIL},
	{ 0.30, 0.00, 3, 0, I_RUSTY_CHAIN_MAIL},
	{-0.15, 0.00, 1, 1, I_SHIELD},
	{ 0.00, 0.00, 1, 1, I_RUSTY_SHIELD},
	{-0.25, 0.30, 2, 1, I_LAENSHIELD},
	{ 0.00, 0.30, 6, 0, I_LAENCHAIN},
	{ 0.00, 0.00, 0, 0, I_SWORD},
	{ 0.00, 0.00, 0, 0, I_SWORD}
};

const troop no_troop = {0, 0};

region *
fleeregion(const unit * u)
{
	const region *r = u->region;
	region *neighbours[MAXDIRECTIONS];
	int c = 0;
	direction_t i;

	if (u->ship && landregion(rterrain(r)))
		return NULL;

	if (u->ship &&
			!(u->race->flags & RCF_SWIM) &&
			!(u->race->flags & RCF_FLY)) {
		return NULL;
	}

#ifdef FLEE_TO
	/* Hat die Einheit ein NACH, wird die angegebene Richtung bevorzugt */
	if (igetkeyword(u->thisorder) == K_MOVE
				|| igetkeyword(u->thisorder) == K_ROUTE)
	{
		region *r2;
		r2 = movewhere(r, u);
		if (r2) {
			return r2;
		}
	}
#endif

	for (i = 0; i != MAXDIRECTIONS; ++i) {
		region * r2 = rconnect(r, i);
		if (r2) {
			if (can_survive(u,r2) && !move_blocked(u, r, i))
				neighbours[c++] = r2;
		}
	}

	if (!c)
		return NULL;
	return neighbours[rand() % c];
}

static char *
sidename(side * s, boolean truename)
{
#define SIDENAMEBUFLEN 256
	static char sidename_buf[SIDENAMEBUFLEN];

	if(s->stealthfaction && truename == false) {
		snprintf(sidename_buf, SIDENAMEBUFLEN,
			"%s", factionname(s->stealthfaction));
	} else {
		snprintf(sidename_buf, SIDENAMEBUFLEN,
			"%s", factionname(s->bf->faction));
	}
	return sidename_buf;
}

static char *
sideabkz(side *s, boolean truename)
{
	static char sideabkz_buf[4];

	if(s->stealthfaction && truename == false) {
		snprintf(sideabkz_buf, 4, "%s", abkz(s->stealthfaction->name, 3));
	} else {
		snprintf(sideabkz_buf, 4, "%s", abkz(s->bf->faction->name, 3));
	}
	return sideabkz_buf;
}

static void
brecord(faction * f, region * r, struct message * m)
{
	if (!f->battles || f->battles->r!=r) {
		struct bmsg * bm = calloc(1, sizeof(struct bmsg));
		bm->next = f->battles;
		f->battles = bm;
		bm->r = r;
	}
	add_message(&f->battles->msgs, m);
}

int
armedmen(const unit * u)
{
	item * itm;
	int n = 0;
	if (!(urace(u)->flags & RCF_NOWEAPONS)) {
		if (urace(u)->ec_flags & CANGUARD) {
			/* kann ohne waffen bewachen: fuer untote und drachen */
			n = u->number;
		} else {
			/* alle Waffen werden gezaehlt, und dann wird auf die Anzahl
			 * Personen minimiert */
			for (itm=u->items;itm;itm=itm->next) {
				const weapon_type * wtype = resource2weapon(itm->type->rtype);
				if (wtype==NULL) continue;
				if (effskill(u, wtype->skill) >= 1) n += itm->number;
				/* if (effskill(u, wtype->skill) >= wtype->minskill) n += itm->number; */
				if (n>u->number) break;
			}
			n = min(n, u->number);
		}
	}
	return n;
}

static void
battledebug(const char *s)
{
#if SHOW_DEBUG
	printf("%s\n", translate_regions(s, NULL));
#endif
	if (bdebug) {
		dbgprintf((bdebug, "%s\n", translate_regions(s, NULL)));
	}
}

void
battlerecord(battle * b, const char *s)
{
	bfaction * bf;
#if 0
	static char * lasts = NULL;

	if (lasts!=s) {
		battledebug(s);
		lasts = s;
	}
#endif
	s = gc_add(strdup(s));
	for (bf = b->factions;bf;bf=bf->next) {
		faction * f = bf->faction;
		struct message * m = new_message(f, "msg_battle%s:string", s);
		brecord(f, b->region, m);
	}
}

void
battlemsg(battle * b, unit * u, const char * s)
{
	bfaction * bf;

	sprintf(buf, "%s %s", unitname(u), s);
	for (bf=b->factions;bf;bf=bf->next) {
		faction * f = bf->faction;
		brecord(f, u->region, new_message(f, "msg_battle%s:string", strdup(buf)));
	}
}

static void
fbattlerecord(faction * f, region * r, const char *s)
{
#if 0
	static char * lasts = NULL;

	if (lasts!=s) {
		battledebug(s);
		lasts = s;
	}
#endif
	s = gc_add(strdup(s));
	brecord(f, r, new_message(f, "msg_battle%s:string", s));
}

boolean
enemy(const side * as, const side * ds)
{
#ifdef FASTENEMY
	switch(as->enemy[ds->index]) {
	case E_ENEMY:
	case E_ENEMY|E_ATTACKING:
		return true;
	default:
		return false;
	}
#else
	const struct enemy * e = as->enemies[ds->index % 16];
	while (e && e->side!=ds) e=e->nexthash;
	if (e) return true;
	return false;
#endif
}

static void
set_enemy(side * as, side * ds, boolean attacking)
{
#ifdef FASTENEMY
	ds->enemy[as->index] |= E_ENEMY;
	as->enemy[ds->index] |= E_ENEMY;
	if (attacking) as->enemy[ds->index] |= E_ATTACKING;
#else
	struct enemy * e = as->enemies[ds->index % 16];
	while (e && e->side!=ds) e=e->nexthash;
	if (e==NULL) {
		e = calloc(sizeof(struct enemy), 1);
		e->side = ds;
		e->attacking = attacking;
		e->nexthash = as->enemies[ds->index % 16];
		as->enemies[ds->index % 16] = e;

		e = calloc(sizeof(struct enemy), 1);
		e->side = as;
		e->nexthash = ds->enemies[as->index % 16];
		ds->enemies[as->index % 16] = e;
	}
	else e->attacking |= attacking;
#endif
}

extern int alliance(const ally * sf, const faction * f, int mode);

static int
allysf(side * s, faction * f)
{
	if (s->bf->faction==f) return true;
	if (s->group) return alliance(s->group->allies, f, HELP_FIGHT);
	return alliance(s->bf->faction->allies, f, HELP_FIGHT);
}

static int
allysfm(side * s, faction * f, int mode)
{
	if (s->bf->faction==f) return true;
	if (s->group) return alliance(s->group->allies, f, mode);
	return alliance(s->bf->faction->allies, f, mode);
}

troop
select_corpse(battle * b, fighter * af)
/* W�hlt eine Leiche aus, der af hilft. casualties ist die Anzahl der
 * Toten auf allen Seiten (im Array). Wenn af == NULL, wird die
 * Parteizugeh�rigkeit ignoriert, und irgendeine Leiche genommen. */
{
	troop dt =
	{0, 0};
	int di, maxcasualties = 0;
	fighter *df;
	side *side;

	for_each(side, b->sides) {
		if (!af || (!enemy(af->side, side) && allysf(af->side, side->bf->faction)))
			maxcasualties += side->casualties;
	}
	next(side);

	di = rand() % maxcasualties;
	for_each(df, b->fighters) {
		/* Geflohene haben auch 0 hp, d�rfen hier aber nicht ausgew�hlt
		 * werden! */
		int dead = df->unit->number - (df->alive + df->run.number);
		if (!playerrace(df->unit->race)) continue;

		if (af && !helping(af->side, df->side))
			continue;
		if (di < dead) {
			dt.fighter = df;
			dt.index = df->unit->number - di;
			break;
		}
		di -= dead;
	}
	next(df);
	return dt;
}

boolean
helping(side * as, side * ds)
{
	if (as->bf->faction==ds->bf->faction) return true;
	return (boolean)(!enemy(as, ds) && allysf(as, ds->bf->faction));
}


/* return the number of live allies warning: this function only considers
 * troops that are still alive, not those that are still fighting although
 * dead. */
int
countallies(side * as)
{
	battle *b = as->battle;
	side *s;
	int count = 0;

	for_each(s, b->sides) {
		if (!helping(as, s)) continue;
		count += s->size[SUM_ROW];
	}
	next(s);
	return count;
}

#ifdef NEW_TACTICS
static int
support(tactics * tac)
{
	return tac ? 100 : 0;
}

static int
tactics_bonus(troop at, troop dt, boolean attacking)
{
	battle *b = dt.fighter->side->battle;
	side *as = at.fighter->side;
	side *ds = dt.fighter->side;
	side *s;
	int abest = 0, dbest = 0;

	for_each(s, b->sides) {
		if (!enemy(s, as) && allysf(s, as->bf->faction)) {
			if (rand() % countallies(as) < support(&as->leader))
				abest = max(s->leader.value, abest);
		} else if (!enemy(s, as) && allysf(s, ds->bf->faction)) {
			if (rand() % countallies(ds) < support(&ds->leader))
				dbest = max(s->leader.value, dbest);
		}
	}
	next(s);
	if (abest > dbest)
		return attacking;
	return 0;
}
#endif /* NEW_TACTICS */

static int
statusrow(int status)
{
	switch (status) {
	case ST_AGGRO:
	case ST_FIGHT:
		return FIGHT_ROW;
	case ST_BEHIND:
	case ST_CHICKEN:
		return BEHIND_ROW;
	case ST_AVOID:
		return AVOID_ROW;
	case ST_FLEE:
		return FLEE_ROW;
	default:
		assert(!"unknown combatrow");
	}
	return FIGHT_ROW;
}

static double
hpflee(int status)
	/* if hp drop below this percentage, run away */
{
	switch (status) {
	case ST_AGGRO:
		return 0.0;
	case ST_FIGHT:
	case ST_BEHIND:
		return 0.2;
	case ST_CHICKEN:
	case ST_AVOID:
		return 0.9;
	case ST_FLEE:
		return 1.0;
	default:
		assert(!"unknown combatrow");
	}
	return 0.0;
}

int
get_unitrow(fighter * af)
{
	static boolean * counted = NULL;
	static size_t csize = 0;

	battle * b = af->side->battle;
	void **si;
	int enemyfront = 0;
	int line, result;
	int retreat = 0;
	int size[NUMROWS];
	int row = statusrow(af->status);
	int front = 0;
	size_t bsize;

#ifdef FAST_GETUNITROW
	if (!b->nonblockers && b->alive==af->row.alive) {
		return af->row.cached;
	}
#endif
	bsize = cv_size(&b->sides);

	if (csize<bsize) {
		if (counted) free(counted);
		csize=bsize;
		counted = calloc(sizeof(boolean), bsize);
	}
	else memset(counted, 0, bsize*sizeof(boolean));
	memset(size, 0, sizeof(size));
	for (line=FIRST_ROW;line!=NUMROWS;++line) {
		/* how many enemies are there in the first row? */
		for (si = b->sides.begin; si != b->sides.end; ++si) {
			side *s = *si;
			if (s->size[line] && enemy(s, af->side))
			{
				void ** sf;
				enemyfront += s->size[line]; /* - s->nonblockers[line] (nicht, weil angreifer) */
				for (sf = b->sides.begin; sf != b->sides.end; ++sf) {
					side * ally = *sf;
					if (!counted[ally->index] && enemy(s, ally) && !enemy(ally, af->side))
					{
						int i;
						counted[ally->index] = true;
						for (i=0;i!=NUMROWS;++i) size[i] += ally->size[i] - ally->nonblockers[i];
					}
				}
			}
		}
		if (enemyfront) break;
	}
	if (enemyfront) {
		for (line=FIRST_ROW;line!=NUMROWS;++line) {
			front += size[line];
			if (!front || front<enemyfront/10) ++retreat;
			else if (front) break;
		}
	}

	/* every entry in the size[] array means someone trying to defend us.
	 * 'retreat' is the number of rows falling.
	 */
	result = max(FIRST_ROW, row - retreat);

#ifdef FAST_GETUNITROW
	af->row.alive = b->alive;
	af->row.cached = result;
#endif
	return result;
}

static int
sort_fighterrow(fighter ** elem1, fighter ** elem2)
{
	int a, b;

	a = get_unitrow(*elem1);
	b = get_unitrow(*elem2);
	return (a < b) ? -1 : ((a == b) ? 0 : 1);
}

static void
reportcasualties(battle * b, fighter * fig, int dead)
{
	bfaction * bf;
	if (fig->alive == fig->unit->number)
		return;
	if (fig->run.region == NULL)
		fig->run.region = b->region;
	fbattlerecord(fig->unit->faction, b->region, " ");
	for (bf = b->factions;bf;bf=bf->next) {
		faction * f = bf->faction;
		struct message * m = msg_message("casualties", "unit runto run alive fallen",
			fig->unit, fig->run.region, fig->run.number, fig->alive, dead);
		brecord(f, b->region, m);
		msg_release(m);
	}
}


static int
contest(int skilldiff, armor_t ar, armor_t sh)
{
	int p, vw = BASE_CHANCE - TDIFF_CHANGE * skilldiff;
	double mod = 1.0;

	/* Hardcodet, mu� ge�ndert werden. */

#ifdef OLD_ARMOR
	if (ar != AR_NONE)
		mod *= (1 - armordata[ar].penalty);
	if (sh != AR_NONE)
		mod *= (1 - armordata[sh].penalty);
	vw = (int) (vw * mod);
#else
	if (ar != AR_NONE)
		mod *= (1 + armordata[ar].penalty);
	if (sh != AR_NONE)
		mod *= (1 + armordata[sh].penalty);
	vw = (int)(100 - ((100 - vw) * mod));
#endif

	do {
		p = rand() % 100;
		vw -= p;
	}
	while (vw >= 0 && p >= 90);
	return (vw <= 0);
}

static boolean
riding(const troop t) {
	if (t.fighter->building!=NULL) return false;
	if (t.fighter->horses + t.fighter->elvenhorses > t.index) return true;
	return false;
}

static weapon *
select_weapon(const troop t, boolean attacking, boolean missile)
	/* select the primary weapon for this trooper */
{
	weapon * weapon = NULL;

	/* Im Nahkampf: Verteidiger schalten auf eine Waffe, mit der das besser geht, um. */
	if (!attacking && !missile) weapon = t.fighter->person[t.index].secondary;
	/* Wenn keine Nahkampfwaffe vorhanden, oder nicht Nahampf, dann die normale Waffe nehmen: */
	if (weapon==NULL) weapon = t.fighter->person[t.index].weapon;
	/* Wenn keine Waffe, dann NULL==waffenlos */
	if (weapon==NULL) return NULL;
	else return weapon;
}

/* ------------------------------------------------------------- */

static int
weapon_skill(const weapon_type * wtype, const unit * u, boolean attacking)
	/* the 'pure' skill when using this weapon to attack or defend.
	 * only undiscriminate modifiers (not affected by troops or enemies)
	 * are taken into account, e.g. no horses, magic, etc. */
{
	int skill;

	if (wtype==NULL) {
		skill = effskill(u, SK_WEAPONLESS);
		if (skill==0) {
			/* wenn kein waffenloser kampf, dann den rassen-defaultwert */
			if (attacking) {
				skill = u->race->at_default;
			} else {
				skill = u->race->df_default;
			}
		} else {
			/* der rassen-defaultwert kann h�her sein als der Talentwert von
			 * waffenloser kampf */
			if (attacking) {
				if (skill < u->race->at_default) skill = u->race->at_default;
			} else {
				if (skill < u->race->df_default) skill = u->race->df_default;
			}
		}
	} else {
		/* changed: if we own a weapon, we have at least a skill of 0 */
		skill = effskill(u, wtype->skill);
		if (skill < wtype->minskill) skill = 0;
		if (attacking) {
			skill += wtype->offmod;
		} else {
			/* Fernk�mpfer haben im Nahkampf Talent 0 */
			if (fval(wtype, WTF_MISSILE)) skill = 0;
			else {
				skill += wtype->defmod;
			}
		}
	}
	/* Rassenbonus auf Kampf */
	if (attacking) {
		skill += u->race->at_bonus;
	} else {
		skill += u->race->df_bonus;
	}

	return skill;
}

static int
weapon_effskill(troop t, troop enemy, const weapon * w, boolean attacking, boolean missile)
	/* effektiver Waffenskill w�hrend des Kampfes */
{
	/* In dieser Runde alle die Modifier berechnen, die fig durch die
	 * Waffen bekommt. */
	fighter * tf = t.fighter;
	unit * tu = t.fighter->unit;
	int skill;
	const weapon_type * wtype = w?w->type:NULL;

	if (wtype==NULL) {
		/* Ohne Waffe: Waffenlose Angriffe */
		skill = weapon_skill(NULL, tu, attacking);
	} else {
		if (attacking) {
			skill = w->offskill;
		} else {
			skill = w->defskill;
		}
		if (wtype->modifiers) {
			/* Pferdebonus, Lanzenbonus, usw. */
			int m;
			unsigned int flags = WMF_SKILL|(attacking?WMF_OFFENSIVE:WMF_DEFENSIVE);

			if (riding(t)) flags |= WMF_RIDING;
			else flags |= WMF_WALKING;
			if (riding(enemy)) flags |= WMF_AGAINST_RIDING;
			else flags |= WMF_AGAINST_WALKING;

			for (m=0;wtype->modifiers[m].value;++m) {
				if ((wtype->modifiers[m].flags & flags) == flags) {
					skill += wtype->modifiers[m].value;
				}
			}
		}
	}

	/* Burgenbonus, Pferdebonus */
	if (riding(t) && (wtype==NULL || !fval(wtype, WTF_MISSILE))) {
		skill += 2;
		if (wtype) skill = skillmod(urace(tu)->attribs, tu, tu->region, wtype->skill, skill, SMF_RIDING);
	}

	if (t.index<tf->elvenhorses) {
		/* Elfenpferde: Helfen dem Reiter, egal ob und welche Waffe. Das ist
		 * eleganter, und vor allem einfacher, sonst mu� man noch ein
		 * WMF_ELVENHORSE einbauen. */
		skill += 2;
	}

	if (skill>0 && !attacking && missile) {
		/*
		 * Wenn ich verteidige, und nicht direkt meinem Feind gegen�berstehe,
		 * halbiert sich mein Skill: (z.B. gegen Fernk�mpfer. Nahk�mpfer
		 * k�nnen mich eh nicht treffen)
		 */
		skill /= 2;
	}
	return skill;
}

static char
select_armor(troop t)
{
	armor_t a = 0;
	int geschuetzt = 0;

	/* Drachen benutzen keine R�stungen */

	if (!(t.fighter->unit->race->battle_flags & BF_EQUIPMENT))
		return AR_NONE;

	do {
		if (armordata[a].shield == 0) {
			geschuetzt += t.fighter->armor[a];
			if (geschuetzt > t.index)	/* unser Kandidat wird geschuetzt */
				return a;
		}
		++a;
	}
	while (a != AR_MAX);

	return AR_NONE;
}

static char
select_shield(troop t)
{
	armor_t a = 0;
	int geschuetzt = 0;

	/* Drachen benutzen keine R�stungen */

	if (!(t.fighter->unit->race->battle_flags & BF_EQUIPMENT))
		return AR_NONE;

	do {
		if (armordata[a].shield == 1) {
			geschuetzt += t.fighter->armor[a];
			if (geschuetzt > t.index)	/* unser Kandidat wird
										 * * * geschuetzt */
				return a;
		}
		++a;
	}
	while (a != AR_MAX);

	return AR_NONE;
}

/* Hier ist zu beachten, ob und wie sich Zauber und Artefakte, die
 * R�stungschutz geben, addieren.
 * - Artefakt I_TROLLBELT gibt R�stung +1
 * - Zauber Rindenhaut gibt R�stung +3
 */
int
select_magicarmor(troop t)
{
	unit *u = t.fighter->unit;
	int geschuetzt = 0;
	int ma = 0;

	geschuetzt = min(get_item(u, I_TROLLBELT), u->number);

	if (geschuetzt > t.index)	/* unser Kandidat wird geschuetzt */
		ma += 1;

	return ma;
}

/* ------------------------------------------------------------- */

/* Sind side ds und Magier des meffect verb�ndet, dann return 1*/
boolean
meffect_protection(battle * b, meffect * s, side * ds)
{
	if (!s->magician->alive) return false;
	if (s->duration <= 0) return false;
	if (enemy(s->magician->side, ds)) return false;
	if (allysf(s->magician->side, ds->bf->faction)) return true;
	return false;
}

/* Sind side as und Magier des meffect verfeindet, dann return 1*/
boolean
meffect_blocked(battle *b, meffect *s, side *as)
{
	if (!s->magician->alive) return false;
	if (s->duration <= 0) return false;
	if (enemy(s->magician->side, as)) return true;
	return false;
}

/* ------------------------------------------------------------- */
/* rmfighter wird schon im PRAECOMBAT gebraucht, da gibt es noch keine
 * troops */
void
rmfighter(fighter *df, int i)
{
	side *ds = df->side;

	/* nicht mehr personen abziehen, als in der Einheit am Leben sind */
	assert(df->alive >= i);
	assert(df->alive <= df->unit->number);

	/* erst ziehen wir die Anzahl der Personen von den K�mpfern in der
	 * Schlacht, dann von denen auf dieser Seite ab*/
	df->side->alive -= i;
	df->side->battle->alive -= i;

	/* Dann die Kampfreihen aktualisieren */
	ds->size[SUM_ROW] -= i;
	ds->size[statusrow(df->status)] -= i;

	/* Spezialwirkungen, z.B. Schattenritter */
	if (df->unit->race->battle_flags & BF_NOBLOCK) {
		ds->nonblockers[SUM_ROW] -= i;
		ds->nonblockers[statusrow(df->status)] -= i;
	}

	/* und die Einheit selbst aktualisieren */
	df->alive -= i;
}


void
rmtroop(troop dt)
{
	fighter *df = dt.fighter;

	/* troop ist immer eine einzele Person */
	rmfighter(df, 1);

	assert(dt.index >= 0 && dt.index < df->unit->number);
	df->person[dt.index] = df->person[df->alive - df->removed];
	df->person[df->alive - df->removed].hp = 0;
}

void
remove_troop(troop dt)
{
	fighter * df = dt.fighter;
	unit * du = df->unit;

	rmtroop(dt);
	if (!df->alive && du->race->itemdrop) {
		item * drops = du->race->itemdrop(du->race, du->number-df->run.number);
		i_merge(&du->items, &drops);
	}
}

/* ------------------------------------------------------------- */
void
drain_exp(unit *u, int n)
{
	skill_t sk = (skill_t)(rand() % MAXSKILLS);
	skill_t ssk;

	ssk = sk;

	while(get_skill(u, sk) <= 0) {
		sk++;
		if (sk == MAXSKILLS)
			sk = 0;
		if (sk == ssk) {
			sk = NOSKILL;
			break;
		}
	}
	if (sk != NOSKILL) {
		n = min(n, get_skill(u, sk));
		change_skill(u, sk, -n);
	}
}

/* ------------------------------------------------------------- */

const char *
rel_dam(int dam, int hp)
{
	double q = (double)dam/(double)hp;

	if (q > 0.75) {
		return "eine klaffende Wunde";
	} else if (q > 0.5) {
		return "eine schwere Wunde";
	} else if (q > 0.25) {
		return "eine Wunde";
	}
	return "eine kleine Wunde";
}

boolean
terminate(troop dt, troop at, int type, const char *damage, boolean missile)
{
	item ** pitm;
	fighter *df = dt.fighter;
	fighter *af = at.fighter;
	unit *au = af->unit;
	unit *du = df->unit;
	battle *b = df->side->battle;
	int heiltrank = 0;
	int faerie_level;
	char debugbuf[512];
	char smallbuf[512];
	double kritchance;

	/* Schild */
	void **si;
	side *ds = df->side;
	int hp;

	int ar, an, am;
	int armor = select_armor(dt);
	int shield = select_shield(dt);

	int da = dice_rand(damage);
	int rda, sk = 0, sd;
	boolean magic = false;

	const weapon_type *dwtype = NULL;
	const weapon_type *awtype = NULL;
	const weapon * weapon;

	switch (type) {
	case AT_STANDARD:
		weapon = select_weapon(at, true, true);
		sk = weapon_effskill(at, dt, weapon, true, missile);
		if (weapon) awtype = weapon->type;
		if (awtype && fval(awtype, WTF_MAGICAL)) magic = true;
		break;
	case AT_NATURAL:
		sk = weapon_effskill(at, dt, NULL, true, missile);
		break;
	case AT_SPELL:
	case AT_COMBATSPELL:
		magic = true;
		break;
	default:
		break;
	}
	weapon = select_weapon(dt, false, true); /* missile=true to get the unmodified best weapon she has */
	sd = weapon_effskill(dt, at, weapon, false, false);
	if (weapon!=NULL) dwtype=weapon->type;

	/* Magischer Schaden durch Spr�che oder magische Waffen? */

	ar = armordata[armor].prot;
	ar += armordata[shield].prot;
	/* nat�rliche R�stung */
	an = du->race->armor;
	/* magische R�stung durch Artefakte oder Spr�che */
	/* Momentan nur Trollg�rtel */
	am = select_magicarmor(dt);

	/* nat�rliche R�stung ist halbkumulativ */
	if (ar>0) {
		ar += an/2;
	} else {
		ar = an;
	}
	ar += am;

	if (type!=AT_COMBATSPELL && type!=AT_SPELL) /* Kein Zauber, normaler Waffenschaden */
	{
		kritchance = max((sk * 3 - sd) / 200.0, 0.005);

		while (chance(kritchance)) {
			sprintf(debugbuf,
				"%s/%d landet einen kritischen Treffer", unitid(au), at.index);
			battledebug(debugbuf);
			da += dice_rand(damage);
		}

		da += rc_specialdamage(au->race, du->race, awtype);
		da += jihad(au->faction, du->race);

		faerie_level = fspecial(du->faction, FS_FAERIE);
		if (type == AT_STANDARD && faerie_level) {
			int c;

			for (c=0;weapon->type->itype->construction->materials[c].number; c++) {
				if(weapon->type->itype->construction->materials[c].type == R_IRON) {
					da += faerie_level;
					break;
				}
			}
		}

		if (awtype!=NULL && fval(awtype, WTF_MISSILE)) {
			/* Fernkampfschadenbonus */
			da += af->person[at.index].damage_rear;
		} else if (awtype==NULL) {
			/* Waffenloser kampf, bonus von talentwert*/
			da += effskill(au, SK_WEAPONLESS);
		} else {
			/* Nahkampfschadensbonus */
			da += af->person[at.index].damage;
		}

		/* Skilldifferenzbonus */
		da += max(0, sk-sd);
	}


	if (magic) /* Magischer Schaden durch Spruch oder magische Waffe */
	{
		double res = 1.0;

		/* magic_resistance gib x% Resistenzbonus zur�ck */
		res -= (double)((double)magic_resistance(du)/100.0)*3.0;

		if (du->race->battle_flags & BF_EQUIPMENT) {
#ifdef TODO_RUNESWORD
			/* Runenschwert gibt im Kampf 80% Resistenzbonus */
			if (dwp == WP_RUNESWORD) res -= 0.80;
#endif
			/* der Effekt von Laen steigt nicht linear */
			if (armor == AR_EOGCHAIN) res *= (1-armordata[armor].magres);
			if (shield == AR_EOGSHIELD) res *= (1-armordata[shield].magres);
			if (dwtype) res *= (1-dwtype->magres);
		}

		if (res > 0) {
			da = (int) (max(da * res, 0));
		}
		/* gegen Magie wirkt nur nat�rliche und magische R�stung */
		ar = an+am;
	}

	rda = max(da - ar,0);

	if ((du->race->battle_flags & BF_INV_NONMAGIC) && !magic) rda = 0;
	else {
		unsigned int i = 0;
		if (du->race->battle_flags & BF_RES_PIERCE) i |= WTF_PIERCE;
		if (du->race->battle_flags & BF_RES_CUT) i |= WTF_CUT;
		if (du->race->battle_flags & BF_RES_BASH) i |= WTF_BLUNT;

		if (i && awtype && fval(awtype, i)) rda /= 2;

		/* Schilde */
		for (si = b->meffects.begin; si != b->meffects.end; ++si) {
			meffect *meffect = *si;
			if (meffect_protection(b, meffect, ds) != 0) {
				assert(0 <= rda); /* rda sollte hier immer mindestens 0 sein */
				/* jeder Schaden wird um effect% reduziert bis der Schild duration
				 * Trefferpunkte aufgefangen hat */
				if (meffect->typ == SHIELD_REDUCE) {
					hp = rda * (meffect->effect/100);
					rda -= hp;
					meffect->duration -= hp;
				}
				/* gibt R�stung +effect f�r duration Treffer */
				if (meffect->typ == SHIELD_ARMOR) {
					rda = max(rda - meffect->effect, 0);
					meffect->duration--;
				}
			}
		}
	}

	sprintf(debugbuf, "Verursacht %dTP, R�stung %d: %d -> %d HP",
		da, ar, df->person[dt.index].hp, df->person[dt.index].hp - rda);

	if (b->small) {
		if (rda > 0) {
			sprintf(smallbuf, "Der Treffer verursacht %s",
				rel_dam(rda, df->person[dt.index].hp));
		} else {
			sprintf(smallbuf, "Der Treffer verursacht keinen Schaden");
		}
	}

	assert(dt.index<du->number);
	df->person[dt.index].hp -= rda;

	if (df->person[dt.index].hp > 0) {	/* Hat �berlebt */
		battledebug(debugbuf);
		if (old_race(au->race) == RC_DAEMON) {
#ifdef TODO_RUNESWORD
			if (select_weapon(dt, 0, -1) == WP_RUNESWORD) continue;
#endif
			if (!(df->person[dt.index].flags & FL_HERO)) {
				df->person[dt.index].flags |= FL_DAZZLED;
				df->person[dt.index].defence--;
			}
		}
		df->person[dt.index].flags = (df->person[dt.index].flags & ~FL_SLEEPING);
		if (b->small) {
			strcat(smallbuf, ".");
			battlerecord(b, smallbuf);
		}
		return false;
	}
#ifdef SHOW_KILLS
	++at.fighter->kills;
#endif

	if (b->small) {
		strcat(smallbuf, ", die t�dlich ist");
	}

	/* Sieben Leben */
	if (old_race(du->race) == RC_CAT && (chance(1.0 / 7))) {
		if (b->small) {
			strcat(smallbuf, ", doch die Katzeng�ttin ist gn�dig");
			battlerecord(b, smallbuf);
		} else {
			/* battlemsg(b, du, "verbraucht eins der 7 Leben einer Katze."); */
		}
		assert(dt.index>=0 && dt.index<du->number);
		df->person[dt.index].hp = unit_max_hp(du);
		return false;
	}

	/* Heiltrank schluerfen und hoffen */
	if (get_effect(du, oldpotiontype[P_HEAL]) > 0) {
		change_effect(du, oldpotiontype[P_HEAL], -1);
		heiltrank = 1;
	} else if (i_get(du->items, oldpotiontype[P_HEAL]->itype) > 0) {
		i_change(&du->items, oldpotiontype[P_HEAL]->itype, -1);
		change_effect(du, oldpotiontype[P_HEAL], 3);
		heiltrank = 1;
	}
	if (heiltrank && (chance(0.50))) {
		if (b->small) {
			strcat(smallbuf, ", doch ein Heiltrank bringt Rettung");
			battlerecord(b, smallbuf);
		} else {
			battlemsg(b, du, "konnte durch einen Heiltrank �berleben.");
		}
		assert(dt.index>=0 && dt.index<du->number);
		df->person[dt.index].hp = du->race->hitpoints;
		return false;
	}

	strcat(debugbuf, ", tot");
	battledebug(debugbuf);
	if (b->small) {
		strcat(smallbuf, ".");
		battlerecord(b, smallbuf);
	}
	for (pitm=&du->items; *pitm; pitm=&(*pitm)->next) {
		const item_type * itype = (*pitm)->type;
		if (!itype->flags & ITF_CURSED && dt.index < (*pitm)->number) {
			/* 25% Grundchance, das ein Item kaputtgeht. */
			if (rand() % 4 < 1) i_change(pitm, itype, -1);
		}
	}
	remove_troop(dt);

	return true;
}

int
lovar(int n)
{
	assert(n > 0);
	if (n == 1) return rand()%2;
	n /= 2;
	return (rand() % n + 1) + (rand() % n + 1);
}

/* ------------------------------------------------------------- */
int
count_enemies(side * as, int mask, int minrow, int maxrow)
/* new implementation of count_enemies ignores mask, since it was never used */
{
	battle *b = as->battle;
	int i = 0;
	void **si;

	for (si = b->sides.begin; si != b->sides.end; ++si) {
		side *side = *si;
		if (enemy(side, as))
		{
			void **fi;

			for (fi = side->fighters.begin; fi != side->fighters.end; ++fi) {
				fighter *fig = *fi;
				int row;

				if (fig->alive - fig->removed <= 0)
					continue;
				else
					row = get_unitrow(fig);

				if (row >= minrow && row <= maxrow)
					i += fig->alive - fig->removed;
			}
		}
	}
	return i;
}

/* ------------------------------------------------------------- */
troop
select_enemy(fighter * af, int minrow, int maxrow)
{
	side *as = af->side;
	battle *b = as->battle;
	troop dt = no_troop;
	void ** si;
	int enemies;

	enemies = count_enemies(af->side, FS_ENEMY, minrow, maxrow);

	if (!enemies)
		return dt;				/* Niemand ist in der angegebenen Entfernung */
	enemies = rand() % enemies;
	for (si=b->sides.begin;!dt.fighter && si!=b->sides.end;++si) {
		side *ds = *si;
		void ** fi;
		if (!enemy(as, ds)) continue;
		for (fi=ds->fighters.begin;fi!=ds->fighters.end;++fi) {
			fighter * df = *fi;
			int dr = get_unitrow(df);
			if (dr < minrow || dr > maxrow) continue;
			if (df->alive - df->removed > enemies) {
				dt.index = enemies;
				dt.fighter = df;
				enemies = 0;
				break;
			}
			else enemies -= (df->alive - df->removed);
		}
	}
	assert(!enemies);
	return dt;
}

/*
 * Abfrage mit
 *
 * cvector *fgs=fighters(b,af,FIGHT_ROW,BEHIND_ROW, FS_HELP|FS_ENEMY);
 * fighter *fig;
 *
 * Optional: Verwirbeln. Vorsicht: Aufwendig!
 * v_scramble(fgs->begin, fgs->end);
 *
 * for (fig = fgs->begin; fig != fgs->end; ++fig) {
 *     fighter *df = *fig;
 *
 * }
 *
 * cv_kill(fgs); Nicht vergessen
 */

cvector *
fighters(battle *b, fighter *af, int minrow, int maxrow, int mask)
{
	fighter *fig;
	cvector	*fightervp;
	int row;

	fightervp = malloc(sizeof(cvector));
	cv_init(fightervp);

	for_each(fig, b->fighters) {
		if (!fig->alive) continue;

		row = get_unitrow(fig);
		if (row >= minrow && row <= maxrow) {
			switch (mask) {
			case FS_ENEMY:
				if (enemy(fig->side, af->side)) cv_pushback(fightervp, fig);
				break;
			case FS_HELP:
				if (!enemy(fig->side, af->side) && allysf(fig->side, af->side->bf->faction))
					cv_pushback(fightervp, fig);
				break;
			case FS_HELP|FS_ENEMY:
				cv_pushback(fightervp, fig);
				break;
			default:
				assert(0 || !"Ung�ltiger Allianzstatus in fighters()");
			}

		}
	} next(fig);

	return fightervp;
}

/* ------------------------------------------------------------------ */

void
do_combatmagic(battle *b, combatmagic_t was)
{
	void **fi;
	spell *sp;
	fighter *fig;
	unit *mage;
	region *r = b->region;
	castorder *co;
	castorder *cll[MAX_SPELLRANK];
	int level, power;
	int spellrank;
	int sl;

	for (spellrank = 0; spellrank < MAX_SPELLRANK; spellrank++) {
		cll[spellrank] = (castorder*)NULL;
	}

	for (fi = b->fighters.begin; fi != b->fighters.end; ++fi) {
		fig = *fi;
		mage = fig->unit;

		if (fig->alive > 0) { /* fighter kann im Kampf get�tet worden sein */

			level = eff_skill(mage, SK_MAGIC, r);
			if (level > 0) {
				char cmd[128];

				switch(was) {
				case DO_PRECOMBATSPELL:
					sp = get_combatspell(mage, 0);
					sl = get_combatspelllevel(mage, 0);
					break;
				case DO_POSTCOMBATSPELL:
					sp = get_combatspell(mage, 2);
					sl = get_combatspelllevel(mage, 2);
					break;
				default:
					/* Fehler! */
					return;
				}
				if (sp == NULL)
					continue;

				snprintf(cmd, 128, "ZAUBER %s", sp->name);

				if (cancast(mage, sp, 1, 1, cmd) == false)
					continue;

				level = eff_spelllevel(mage, sp, level, 1);
				if (sl > 0) level = min(sl, level);
				if (level < 0) {
					sprintf(buf, "%s versucht %s zu zaubern, doch der Zauber schl�gt "
							"fehl!", unitname(mage), sp->name);
					battlerecord(b, buf);
					continue;
				}

				power = spellpower(r, mage, sp, level);
				if (power <= 0) {	/* Effekt von Antimagie */
					sprintf(buf, "%s versucht %s zu zaubern, doch der Zauber schl�gt "
							"fehl!", unitname(mage), sp->name);
					battlerecord(b, buf);
					continue;
				}

				if (fumble(r, mage, sp, sp->level) == true) {
					sprintf(buf, "%s versucht %s zu zaubern, doch der Zauber schl�gt "
							"fehl!", unitname(mage), sp->name);
					battlerecord(b, buf);
					pay_spell(mage, sp, level, 1);
					continue;
				}

				co = new_castorder(fig, 0, sp, r, level, power, 0, 0, 0);
				add_castorder(&cll[(int)(sp->rank)], co);
			}
		}
	}
	for (spellrank = 0; spellrank < MAX_SPELLRANK; spellrank++) {
		for (co = cll[spellrank]; co; co = co->next) {
			fig = (fighter*)co->magician;
			sp = co->sp;
			level = co->level;
			power = co->force;

			level = ((cspell_f)sp->sp_function)(fig, level, power, sp);
			if (level > 0) {
				pay_spell(fig->unit, sp, level, 1);
				if (was == DO_PRECOMBATSPELL) {
					get_mage(fig->unit)->precombataura = level;
				}
			}
		}
	}
	for (spellrank = 0; spellrank < MAX_SPELLRANK; spellrank++) {
		free_castorders(cll[spellrank]);
	}
}


void
do_combatspell(troop at)
{
	spell *sp;
	fighter *fi = at.fighter;
	unit *mage = fi->unit;
	battle *b = fi->side->battle;
	region *r = b->region;
	int level, power;
	int fumblechance = 0;
	void **mg;
	int sl;
	char cmd[128];

	sp = get_combatspell(mage, 1);
	if (sp == NULL) {
		fi->magic = 0; /* Hat keinen Kampfzauber, k�mpft nichtmagisch weiter */
		return;
	}
	snprintf(cmd, 128, "ZAUBER %s", sp->name);
	if (cancast(mage, sp, 1, 1, cmd) == false) {
		fi->magic = 0; /* Kann nicht mehr Zaubern, k�mpft nichtmagisch weiter */
		return;
	}

	level = eff_spelllevel(mage, sp, fi->magic, 1);
	if ((sl = get_combatspelllevel(mage, 1)) > 0) level = min(level, sl);

	if (fumble(r, mage, sp, sp->level) == true) {
		sprintf(buf, "%s versucht %s zu zaubern, doch der Zauber schl�gt "
				"fehl!", unitname(mage), sp->name);
		battlerecord(b, buf);
		pay_spell(mage, sp, level, 1);
		return;
	}

	for (mg = b->meffects.begin; mg != b->meffects.end; ++mg) {
		meffect *mblock = *mg;
		if (mblock->typ == SHIELD_BLOCK) {
			if (meffect_blocked(b, mblock, fi->side) != 0) {
				fumblechance += mblock->duration;
				mblock->duration -= mblock->effect;
			}
		}
	}

	/* Antimagie die Fehlschlag erh�ht */
	if (rand()%100 < fumblechance) {
		sprintf(buf, "%s versucht %s zu zaubern, doch der Zauber schl�gt "
				"fehl!", unitname(mage), sp->name);
		battlerecord(b, buf);
		return;
	}
	power = spellpower(r, mage, sp, level);
	if (power <= 0) {	/* Effekt von Antimagie */
		sprintf(buf, "%s versucht %s zu zaubern, doch der Zauber schl�gt "
				"fehl!", unitname(mage), sp->name);
		battlerecord(b, buf);
		pay_spell(mage, sp, level, 1);
		return;
	}

	level = ((cspell_f)sp->sp_function)(fi, level, power, sp);
	if (level > 0){
		pay_spell(mage, sp, level, 1);
		at.fighter->action_counter++;
	}
}


/* Sonderattacken: Monster patzern nicht und zahlen auch keine
 * Spruchkosten. Da die Spruchst�rke direkt durch den Level bestimmt
 * wird, wirkt auch keine Antimagie (wird sonst in spellpower
 * gemacht) */

static void
do_extra_spell(troop at, const att *a)
{
	spell *sp;
	fighter *fi = at.fighter;
	unit *au = fi->unit;
	int power;

	sp = find_spellbyid((spellid_t)(a->data.iparam));
	/* nur bei Monstern k�nnen mehrere 'Magier' in einer Einheit sein */
	power = sp->level * au->number;
	((cspell_f)sp->sp_function)(fi, sp->level, power, sp);
}

static int
skilldiff(troop at, troop dt, int dist)
{
	fighter *af = at.fighter, *df = dt.fighter;
	unit *au = af->unit, *du = df->unit;
	int is_protected = 0, skdiff = 0, sk;
	weapon * awp = select_weapon(at, true, dist>1);
	weapon * dwp = select_weapon(dt, false, dist>1);

	/* Effekte von Alchemie */
	if (get_effect(au, oldpotiontype[P_BERSERK]) > at.index)
		++skdiff;

	if (df->person[dt.index].flags & FL_SLEEPING)
		skdiff += 2;

	/* Effekte durch Rassen */
	if (awp!=NULL && au->race == new_race[RC_HALFLING] && dragonrace(du->race)) {
		skdiff += 5;
	}

	if (old_race(au->race) == RC_GOBLIN &&
	    af->side->size[SUM_ROW] >= df->side->size[SUM_ROW] * 10)
		skdiff += 1;

	/* TODO this should be a skillmod */
	skdiff += jihad(au->faction, du->race);

	skdiff += af->person[at.index].attack;
	skdiff -= df->person[dt.index].defence;

#ifdef NEW_TACTICS
	/* Effekte von Taktikern */
	skdiff += tactics_bonus(at, dt, true);
	skdiff -= tactics_bonus(dt, at, false);
#endif

	if (df->building) {
		if (df->building->type == &bt_castle) {
			if(fspecial(au->faction, FS_SAPPER)) {
				/* Halbe Schutzwirkung, aufgerundet */
				/* -1 because the tradepost has no protection value */
				skdiff -= (buildingeffsize(df->building, false)-1+1)/2;
			} else {
				skdiff -= buildingeffsize(df->building, false)-1;
			}
			is_protected = 2;
		}
		if (is_cursed(df->building->attribs, C_STRONGWALL, 0)) {
			/* wirkt auf alle Geb�ude */
			skdiff -= get_curseeffect(df->building->attribs, C_STRONGWALL, 0);
			is_protected = 2;
		}
		if (is_cursed(df->building->attribs, C_MAGICSTONE, 0)) {
			/* Verdoppelt Burgenbonus */
			skdiff -= buildingeffsize(df->building, false);
		}
	}
	/* Goblin-Verteidigung
	 * ist direkt in der Rassentabelle als df_default
	*/

	/* Effekte der Waffen */
	skdiff += weapon_effskill(at, dt, awp, true, dist>1);
	if (awp && fval(awp->type, WTF_MISSILE)) {
		skdiff -= is_protected;
		if (awp->type->modifiers) {
			int w;
			for (w=0;awp->type->modifiers[w].value!=0;++w) {
				if (awp->type->modifiers[w].flags & WMF_MISSILE_TARGET) {
					skdiff -= awp->type->modifiers[w].value;
					break;
				}
			}
		}
	}
	sk = weapon_effskill(dt, at, dwp, false, dist>1);
	skdiff -= sk;
	return skdiff;
}

static int
setreload(troop at)
{
	fighter * af = at.fighter;
	const weapon_type * wtype = af->person[at.index].weapon->type;
	if (wtype->reload == 0) return 0;
	return af->person[at.index].reload = wtype->reload;
}

int
getreload(troop at)
{
	return at.fighter->person[at.index].reload;
}

static char *
attack_message(const troop at, const troop dt, const weapon * wp, int dist)
{
	static char smallbuf[512];
	char a_unit[NAMESIZE+8], d_unit[NAMESIZE+8];
	const char *noweap_string[4] = {"schl�gt nach",
													"tritt nach",
													"bei�t nach",
													"kratzt nach"};

	if (at.fighter->unit->number > 1)
		sprintf(a_unit, "%s/%d", unitname(at.fighter->unit), at.index);
	else
		sprintf(a_unit, "%s", unitname(at.fighter->unit));

	if (dt.fighter->unit->number > 1)
		sprintf(d_unit, "%s/%d", unitname(dt.fighter->unit), dt.index);
	else
		sprintf(d_unit, "%s", unitname(dt.fighter->unit));

	if (wp == NULL) {
		sprintf(smallbuf, "%s %s %s",
			a_unit, noweap_string[rand()%4], d_unit);
		return smallbuf;
	}

	if (dist > 1) {
		sprintf(smallbuf, "%s schie�t mit %s auf %s",
			a_unit,
			locale_string(default_locale, resourcename(wp->type->itype->rtype, GR_INDEFINITE_ARTICLE)), d_unit);
		return smallbuf;
	}

		sprintf(smallbuf, "%s schl�gt mit %s nach %s",
			a_unit,
			locale_string(default_locale, resourcename(wp->type->itype->rtype, GR_INDEFINITE_ARTICLE)), d_unit);

	return smallbuf;
}

int
hits(troop at, troop dt, weapon * awp)
{
	battle * b = at.fighter->side->battle;
	fighter *af = at.fighter, *df = dt.fighter;
	unit *au = af->unit, *du = df->unit;
	char debugbuf[512];
	char *smallbuf = NULL;
	armor_t armor, shield;
	int skdiff = 0;
	int dist = get_unitrow(af) + get_unitrow(df) - 1;
	weapon * dwp = select_weapon(dt, false, dist>1);

	if (!df->alive) return 0;
	if (getreload(at)) return 0;
	if (dist>1 && !fval(awp->type, WTF_MISSILE)) return 0;
	if (af->person[at.index].flags & FL_STUNNED) {
			af->person[at.index].flags &= ~FL_STUNNED;
		return 0;
	}
	if ((af->person[at.index].flags & FL_TIRED && rand()%100 < 50)
			|| (af->person[at.index].flags & FL_SLEEPING))
		return 0;
	if (awp && fval(awp->type, WTF_MISSILE)
			&& af->side->battle->reelarrow == true
			&& rand()%100 < 50)
	{
		return 0;
	}

	skdiff = skilldiff(at, dt, dist);

	/* Verteidiger bekommt eine R�stung */
	armor = select_armor(dt);
	shield = select_shield(dt);
	sprintf(debugbuf, "%.4s/%d [%6s/%d] attackiert %.4s/%d [%6s/%d] mit %d dist %d",
			unitid(au), at.index,
			(awp != NULL) ?
				locale_string(default_locale, resourcename(awp->type->itype->rtype, 0)) : "unbewaffnet",
			weapon_effskill(at, dt, awp, true, dist>1),
			unitid(du), dt.index,
			(dwp != NULL) ?
				locale_string(default_locale, resourcename(dwp->type->itype->rtype, 0)) : "unbewaffnet",
			weapon_effskill(dt, at, dwp, true, dist>1),
			skdiff, dist);
	if (b->small) {
		smallbuf = attack_message(at, dt, awp, dist);
	}

	if (contest(skdiff, armor, shield)) {
		strcat(debugbuf, " und trifft.");
		battledebug(debugbuf);
		if (b->small) {
			strcat(smallbuf, " und trifft.");
			battlerecord(b,smallbuf);
		}
#ifdef SHOW_KILLS
		++at.fighter->hits;
#endif
		return 1;
	}
	strcat(debugbuf, ".");
	battledebug(debugbuf);
	if (b->small) {
		strcat(smallbuf, ".");
		battlerecord(b,smallbuf);
	}
	return 0;
}

void
dazzle(battle *b, troop *td)
{
	char smallbuf[512];

	/* Nicht kumulativ ! */
	if(td->fighter->person[td->index].flags & FL_DAZZLED) return;

#ifdef TODO_RUNESWORD
	if (td->fighter->weapon[WP_RUNESWORD].count > td->index) {
		if (b->small) {
			strcpy(smallbuf, "Das Runenschwert gl�ht kurz auf.");
			battlerecord(b,smallbuf);
		}
		return;
	}
#endif
	if (td->fighter->person[td->index].flags & FL_HERO) {
		if (b->small) {
			sprintf(smallbuf, "Eine kurze Schw�che erfa�t %s/%d, vergeht jedoch "
				"schnell wieder.", unitname(td->fighter->unit), td->index);
			battlerecord(b,smallbuf);
		}
		return;
	}

	if (td->fighter->person[td->index].flags & FL_DAZZLED) {
		if (b->small) {
			sprintf(smallbuf, "Eine kurze Schw�che erfa�t %s/%d, vergeht jedoch "
				"schnell wieder.", unitname(td->fighter->unit), td->index);
			battlerecord(b,smallbuf);
		}
		return;
	}

	if (b->small) {
		sprintf(smallbuf, "%s/%d f�hlt sich, als w�rde Kraft entzogen.",
			unitname(td->fighter->unit), td->index);
		battlerecord(b,smallbuf);
	}

	td->fighter->person[td->index].flags |= FL_DAZZLED;
	td->fighter->person[td->index].defence--;
}

/* TODO: Geb�ude/Schiffe sollten auch zerst�rbar sein. Schwierig im Kampf,
 * besonders bei Schiffen. */

void
damage_building(battle *b, building *bldg, int damage_abs)
{
	bldg->size = max(1, bldg->size-damage_abs);

	/* Wenn Burg, dann gucken, ob die Leute alle noch in das Geb�ude passen. */

	if (bldg->type == &bt_castle) {
		fighter *fi;

		bldg->sizeleft = bldg->size;

		for_each(fi, b->fighters) {
			if (fi->building == bldg) {
				if (bldg->sizeleft >= fi->unit->number) {
					fi->building = bldg;
					bldg->sizeleft -= fi->unit->number;
				} else {
					fi->building = NULL;
				}
			}
		} next(fi);
	}
}

static int
attacks_per_round(troop t)
{
	return t.fighter->person[t.index].speed;
}

static void
attack(battle *b, troop ta, const att *a)
{
	fighter *af = ta.fighter;
	troop td;
	unit *au = af->unit;
	int row = get_unitrow(af) - 1;

	switch(a->type) {
	case AT_STANDARD:		/* Waffen, mag. Gegenst�nde, Kampfzauber */
	case AT_COMBATSPELL:
		if (af->magic > 0) {
			/* Magier versuchen immer erstmal zu zaubern, erst wenn das
			 * fehlschl�gt, wird af->magic == 0 und  der Magier k�mpft
			 * konventionell weiter */
			do_combatspell(ta);
		} else {
			weapon * wp;

			wp = select_weapon(ta, true, true);
			/* Sonderbehandlungen */

			if (getreload(ta)) {
				ta.fighter->person[ta.index].reload--;
			} else {
				boolean standard_attack = true;
				if (wp && wp->type->attack) {
					int dead;
					standard_attack = wp->type->attack(&ta, &dead);
					af->catmsg += dead;
					/* TODO: dies hier ist nicht richtig. wenn die katapulte/etc.
					 * keinen gegner gefunden haben, sollte es nicht erh�ht werden.
					 * au�erdem m�sste allen gegenern der counter erh�ht werden.
					 */
					if (af->person[ta.index].last_action < b->turn) {
						af->person[ta.index].last_action = b->turn;
						af->action_counter++;
					}
				}
				if (standard_attack) {
					boolean missile = false;
					if (wp && fval(wp->type, WTF_MISSILE)) missile=true;
					if (missile) td = select_enemy(af, missile_range[0]-row, missile_range[1]-row);
					else td = select_enemy(af, melee_range[0]-row, melee_range[1]-row);
					if (!td.fighter) return;

					if(td.fighter->person[td.index].last_action < b->turn) {
						td.fighter->person[td.index].last_action = b->turn;
						td.fighter->action_counter++;
					}
					if(ta.fighter->person[ta.index].last_action < b->turn) {
						ta.fighter->person[ta.index].last_action = b->turn;
						ta.fighter->action_counter++;
					}

					if (hits(ta, td, wp)) {
						const char * d;
						if (wp == NULL) d = au->race->def_damage;
						else if (riding(ta)) d = wp->type->damage[1];
						else d = wp->type->damage[0];
						terminate(td, ta, a->type, d, missile);
					}
				}
				if (wp && wp->type->reload && !getreload(ta)) {
					int i = setreload(ta);
					sprintf(buf, " Nachladen gesetzt: %d", i);
					battledebug(buf);
				}
			}
		}
		break;
	case AT_SPELL:	/* Extra-Spr�che. Kampfzauber in AT_COMBATSPELL! */
		do_extra_spell(ta, a);
		break;
	case AT_NATURAL:
		td = select_enemy(af, FIGHT_ROW-row, FIGHT_ROW-row);
		if (!td.fighter) return;
		if(td.fighter->person[td.index].last_action < b->turn) {
			td.fighter->person[td.index].last_action = b->turn;
			td.fighter->action_counter++;
		}
		if(ta.fighter->person[ta.index].last_action < b->turn) {
			ta.fighter->person[ta.index].last_action = b->turn;
			ta.fighter->action_counter++;
		}
		if (hits(ta, td, NULL)) {
			terminate(td, ta, a->type, a->data.dice, false);
		}
		break;
	case AT_DRAIN_ST:
		td = select_enemy(af, FIGHT_ROW-row, FIGHT_ROW-row);
		if (!td.fighter) return;
		if(td.fighter->person[td.index].last_action < b->turn) {
			td.fighter->person[td.index].last_action = b->turn;
			td.fighter->action_counter++;
		}
		if(ta.fighter->person[ta.index].last_action < b->turn) {
			ta.fighter->person[ta.index].last_action = b->turn;
			ta.fighter->action_counter++;
		}
		if (hits(ta, td, NULL)) {
			int c = dice_rand(a->data.dice);
			while(c > 0) {
				if (rand()%2) {
					td.fighter->person[td.index].attack -= 1;
				} else {
					td.fighter->person[td.index].defence -= 1;
				}
				c--;
			}
		}
		break;
	case AT_DRAIN_EXP:
		td = select_enemy(af, FIGHT_ROW-row, FIGHT_ROW-row);
		if (!td.fighter) return;
		if(td.fighter->person[td.index].last_action < b->turn) {
			td.fighter->person[td.index].last_action = b->turn;
			td.fighter->action_counter++;
		}
		if(ta.fighter->person[ta.index].last_action < b->turn) {
			ta.fighter->person[ta.index].last_action = b->turn;
			ta.fighter->action_counter++;
		}
		if (hits(ta, td, NULL)) {
			drain_exp(td.fighter->unit, dice_rand(a->data.dice));
		}
		break;
	case AT_DAZZLE:
		td = select_enemy(af, FIGHT_ROW-row, FIGHT_ROW-row);
		if (!td.fighter) return;
		if(td.fighter->person[td.index].last_action < b->turn) {
			td.fighter->person[td.index].last_action = b->turn;
			td.fighter->action_counter++;
		}
		if(ta.fighter->person[ta.index].last_action < b->turn) {
			ta.fighter->person[ta.index].last_action = b->turn;
			ta.fighter->action_counter++;
		}
		if (hits(ta, td, NULL)) {
			dazzle(b, &td);
		}
		break;
	case AT_STRUCTURAL:
		td = select_enemy(af, FIGHT_ROW-row, FIGHT_ROW-row);
		if (!td.fighter) return;
		if(ta.fighter->person[ta.index].last_action < b->turn) {
			ta.fighter->person[ta.index].last_action = b->turn;
			ta.fighter->action_counter++;
		}
		if (td.fighter->unit->ship) {
			td.fighter->unit->ship->damage += DAMAGE_SCALE * dice_rand(a->data.dice);
		} else if (td.fighter->unit->building) {
			damage_building(b, td.fighter->unit->building,
											dice_rand(a->data.dice));
		}
	}

	/* Der letzte Katapultsch�tze setzt die
	 * Ladezeit neu und generiert die Meldung. */
	if (af->catmsg>=0 && ta.index==0) {
		sprintf(buf, "%d Opfer wurde%s get�tet.",
				af->catmsg, af->catmsg<=1?"":"n");
		battlerecord(b, buf);
		af->catmsg = -1;
	}
}

void
do_attack(fighter * af)
{
	troop ta;
	unit *au = af->unit;
	side *side = af->side;
	battle *b = side->battle;
	int apr;
	int a;

	ta.fighter = af;

	assert(au && au->number);
	/* Da das Zuschlagen auf Einheiten und nicht auf den einzelnen
	 * K�mpfern beruht, darf die Reihenfolge und Gr��e der Einheit keine
	 * Rolle spielen, Das tut sie nur dann, wenn jeder, der am Anfang der
	 * Runde lebte, auch zuschlagen darf. Ansonsten ist der, der zuf�llig
	 * mit einer gro�en Einheit zuerst drankommt, extrem bevorteilt. */
	ta.index = af->fighting;

	while (ta.index--) {
		/* Wir suchen eine beliebige Feind-Einheit aus. An der k�nnen
		 * wir feststellen, ob noch jemand da ist. */
		int enemies = count_enemies(af->side, FS_ENEMY, FIGHT_ROW, LAST_ROW);
		if (!enemies) break;

		for (apr=attacks_per_round(ta); apr > 0; apr--) {
			for (a = 0; a < 6; a++) {
				if (au->race->attack[a].type != AT_NONE)
					attack(b, ta, &(au->race->attack[a]));
			}
		}
	}

}

void
do_regenerate(fighter *af)
{
	troop ta;
	unit *au = af->unit;

	ta.fighter = af;
	ta.index = af->fighting;

	while(ta.index--) {
		af->person[ta.index].hp += effskill(au, SK_AUSDAUER);
		af->person[ta.index].hp =	min(unit_max_hp(au), af->person[ta.index].hp);
	}
}

static void
add_tactics(tactics * ta, fighter * fig, int value)
{
	if (value == 0 || value < ta->value)
		return;
	if (value > ta->value)
		cv_kill(&ta->fighters);
	cv_pushback(&ta->fighters, fig);
	cv_pushback(&fig->side->battle->leaders, fig);
	ta->value = value;
}

double
fleechance(unit * u)
{
	double c = 0.20;			/* Fluchtwahrscheinlichkeit in % */
	region *r = u->region;

	/* Einheit u versucht, dem Get�mmel zu entkommen */

	c += (eff_skill(u, SK_STEALTH, r) * 0.05);

	if (get_item(u, I_UNICORN) >= u->number && eff_skill(u, SK_RIDING, r) >= 5)
		c += 0.30;
	else if (get_item(u, I_HORSE) >= u->number && eff_skill(u, SK_RIDING, r) >= 1)
		c += 0.10;

	if (old_race(u->race) == RC_HALFLING) {
		c += 0.20;
		c = min(c, 0.90);
	} else {
		c = min(c, 0.75);
	}

	return c;
}

int nextside = 0;
side *
#ifdef STEALTHFACTION
make_side(battle * b, const faction * f, const group * g, boolean stealth, const faction *stealthfaction)
#else
make_side(battle * b, const faction * f, const group * g, boolean stealth)
#endif
{
	side *s1 = calloc(sizeof(struct side), 1);
	bfaction * bf;

	s1->battle = b;
	s1->group = g;
	s1->stealth = stealth;
#ifdef STEALTHFACTION
	s1->stealthfaction = stealthfaction;
#endif
	cv_pushback(&b->sides, s1);
	for (bf = b->factions;bf;bf=bf->next) {
		faction * f2 = bf->faction;

		if (f2 == f) {
			s1->bf = bf;
			s1->index = nextside++;
			s1->nextF = bf->sides;
			bf->sides = s1;
			break;
		}
	}
	assert(bf);
	return s1;
}

void
loot_items(fighter * corpse)
{
	unit * u = corpse->unit;
	item * itm = u->items;
	u->items = NULL;

	while (itm) {
		int i;
		if (itm->number) {
			for (i = 10; i != 0; i--) {
				int loot = itm->number / i;
				itm->number -= loot;
				/* Looten tun hier immer nur die Gegner. Das
				 * ist als Ausgleich f�r die neue Loot-regel
				 * (nur ganz tote Einheiten) fair.
				 * zus�tzlich looten auch geflohene, aber
				 * nach anderen Regeln.
				 */
				if (loot>0 && (itm->type->flags & (ITF_CURSED|ITF_NOTLOST)
						|| rand()%100 >= 50 - corpse->side->battle->keeploot)) {
					fighter *fig = select_enemy(corpse, FIGHT_ROW, LAST_ROW).fighter;
					if (fig) {
						item * l = fig->loot;
						while (l && l->type!=itm->type) l=l->next;
						if (!l) {
							l = calloc(sizeof(item), 1);
							l->next = fig->loot;
							fig->loot = l;
							l->type = itm->type;
						}
						l->number += loot;
					}
				}
			}
		}
		itm = itm->next;
	}
}

static void
loot_fleeing(fighter* fig, unit* runner)
{
	/* TODO: Vern�nftig fixen */
	runner->items = NULL;
	assert(runner->items == NULL);
	runner->items = fig->run.items;
	fig->run.items = NULL;
}

static void
merge_fleeloot(fighter* fig, unit* u)
{
	i_merge(&u->items, &fig->run.items);
}

static boolean
seematrix(const faction * f, const side * s)
{
	if (f==s->bf->faction) return true;
	if (s->stealth) return false;
	return true;
}

static void
aftermath(battle * b)
{
	int i;
	region *r = b->region;
	ship *sh;
	side *s;
	cvector *fighters = &b->fighters;
	void **fi;
	int is = 0;
	bfaction * bf;
	int dead_peasants;
	boolean battle_was_relevant = (boolean)(b->turn+(b->has_tactics_turn?1:0)>2);

#ifdef TROLLSAVE
	int *trollsave = calloc(2 * cv_size(&b->factions), sizeof(int));
#endif

	for (fi = fighters->begin; fi != fighters->end; ++fi) {
		fighter *df = *fi;
		unit *du = df->unit;
		int dead;
		const attrib *a;
		int pr_mercy = 0;

		for (a = a_find(du->attribs, &at_prayer_effect); a; a = a->nexttype) {
			if (a->data.sa[0] == PR_MERCY) {
				pr_mercy = a->data.sa[1];
			}
		}

		dead = du->number - df->alive;
		dead -= df->run.number;
#ifdef TROLLSAVE
		/* Trolle k�nnen regenerieren */
		if (df->alive > 0 && dead && old_race(du->race) == RC_TROLL)
			for (i = 0; i != dead; ++i) {
				if (chance(TROLL_REGENERATION)) {
					++df->alive;
					++df->side->alive;
					++df->side->battle->alive;
				}
			}
		trollsave[df->side->index] += dead - du->number + df->alive;
#endif
		/* Regeneration durch PR_MERCY */
		if (dead && pr_mercy)
			for (i = 0; i != dead; ++i)
				if (rand()%100 < pr_mercy) {
					++df->alive;
					++df->side->alive;
					++df->side->battle->alive;
				}

		/* Tote, die wiederbelebt werde k�nnen */
		if (playerrace(df->unit->race)) {
			df->side->casualties += dead;
		}
#ifdef SHOW_KILLS
		if (df->hits + df->kills) {
			struct message * m = new_message(du->faction, "killsandhits%u:unit%i:hits%i:kills", du, df->hits, df->kills);
			brecord(du->faction, b->region, m);
		}
#endif
	}

	/* Wenn die Schlacht kurz war, dann gib Aura f�r den Pr�combatzauber
	 * zur�ck. Nicht logisch, aber die einzige L�sung, den Verlust der
	 * Aura durch Dummy-Angriffe zu verhindern. */

	for_each(s, b->sides) {
		if (s->bf->lastturn+(b->has_tactics_turn?1:0)<=1) continue;
		/* Pr�fung, ob faction angegriffen hat. Geht nur �ber die Faction */
		if (!s->bf->attacker) {
			fighter *fig;
			for_each(fig, s->fighters) {
				sc_mage * mage = get_mage(fig->unit);
				if (mage)
					mage->spellpoints += mage->precombataura;
			} next(fig);
		}
		/* Alle Fighter durchgehen, Mages suchen, Precombataura zur�ck */
	} next(s);

	/* POSTCOMBAT */
	do_combatmagic(b, DO_POSTCOMBATSPELL);

	for_each(s, b->sides) {
		fighter *df;
		boolean relevant = false; /* Kampf relevant f�r dieses Heer? */
		if (s->bf->lastturn+(b->has_tactics_turn?1:0)>1) {
			relevant = true;
		}
		s->flee = 0;
		s->dead = 0;

		for_each(df, s->fighters) {
			unit *du = df->unit;
			int dead = du->number - df->alive - df->run.number;
			int sum_hp = 0;
			int n;
			if (relevant && df->action_counter >= du->number) {
				fset(du, FL_LONGACTION);
				/* TODO: das sollte hier weg sobald anderswo �b
				 * erall HADBATTLE getestet wird. */
				set_string(&du->thisorder, "");
			}
			for (n = 0; n != df->alive; ++n) {
				if (df->person[n].hp > 0)
					sum_hp += df->person[n].hp;
			}

			s->dead += dead;

			if (df->alive == du->number) continue; /* nichts passiert */

			/* die weggerannten werden sp�ter subtrahiert! */
			assert(du->number >= 0);

			if (df->run.hp) {
				if (df->alive == 0) {

					/* Report the casualties */
					reportcasualties(b, df, dead);

					/* Zuerst d�rfen die Feinde pl�ndern, die mitgenommenen Items
					 * stehen in fig->run.items. Dann werden die Fliehenden auf
					 * die leere (tote) alte Einheit gemapt */
					if (fval(df,FIG_NOLOOT)){
						merge_fleeloot(df, du);
					} else {
						loot_items(df);
						loot_fleeing(df, du);
					}
					scale_number(du, df->run.number);
					du->hp = df->run.hp;
					set_string(&du->thisorder, "");
					setguard(du, GUARD_NONE);
					fset(du, FL_MOVED);
					leave(du->region, du);
					if (df->run.region) {
						travel(r, du, df->run.region, 1);
						df->run.region = du->region;
					}
				} else {
					/* nur teilweise geflohene Einheiten mergen sich wieder */
					df->alive += df->run.number;
					sum_hp += df->run.hp;
					merge_fleeloot(df, du);
					df->run.number = 0;
					df->run.hp = 0;
					/* df->run.region = NULL;*/

					reportcasualties(b, df, dead);

					scale_number(du, df->alive);
					du->hp = sum_hp;
				}
			} else {
				if (df->alive==0) {
					/* alle sind tot, niemand geflohen. Einheit aufl�sen */
					df->run.number = 0;
					df->run.hp = 0;
					df->run.region = NULL;

					/* Report the casualties */
					reportcasualties(b, df, dead);

					setguard(du, GUARD_NONE);
					scale_number(du, 0);
					/* Distribute Loot */
					loot_items(df);
				} else {
					df->run.number = 0;
					df->run.hp = 0;

					reportcasualties(b, df, dead);

					scale_number(du, df->alive);
					du->hp = sum_hp;
				}
			}
			s->flee += df->run.number;

			if (playerrace(du->race)) {
				/* tote im kampf werden zu regionsuntoten:
				 * for each of them, a peasant will die as well */
				is += dead;
			}
		} next(df);
	} next(s);
	dead_peasants = min(rpeasants(r), is);
	deathcounts(r, dead_peasants + is);
	chaoscounts(r, dead_peasants / 2);
	rsetpeasants(r, rpeasants(r) - dead_peasants);

	for (bf=b->factions;bf;bf=bf->next) {
		faction * f = bf->faction;
		fbattlerecord(f, r, " ");
		for_each(s, b->sides) {
			if (seematrix(f, s)) {
				sprintf(buf, "Heer %2d(%s): %d Tote, %d Geflohene, %d �berlebende",
						s->index, sideabkz(s,false), s->dead, s->flee, s->alive);
			} else {
				sprintf(buf, "Heer %2d(Unb): %d Tote, %d Geflohene, %d �berlebende",
						s->index, s->dead, s->flee, s->alive);
			}
			fbattlerecord(f, r, buf);
		} next(s);
	}
#if COMBATEXP
	for (fi = fighters->begin; fi != fighters->end; ++fi) {
		fighter *df = *fi;
		troop t;

		t.fighter = df;
		if (df->alive && df->side->bf->lastturn > 2) {
			/* signifikanter Kampf, dann gibt es XP */
			unit *du = df->unit;
			cvector *tacs = &df->side->leader.fighters;

			if (v_find(tacs->begin, tacs->end, df) != tacs->end)
				change_skill(du, SK_TACTICS, du->number * COMBATEXP);
			if (!fval(df, FIG_COMBATEXP)) {
				t.index = df->alive;
				while (t.index) {
					--t.index;
					change_skill(du, SK_AUSDAUER, COMBATEXP);

					switch (select_weapon(t, 1, 1)) {
					case WP_RUNESWORD:
					case WP_GREATSWORD:
					case WP_EOGSWORD:
					case WP_SWORD:
					case WP_AXE:
						change_skill(du, SK_SWORD, COMBATEXP);
						break;
					case WP_SPEAR:
					case WP_HALBERD:
					case WP_LANCE:
						change_skill(du, SK_SPEAR, COMBATEXP);
						break;
					case WP_CROSSBOW:
						change_skill(du, SK_CROSSBOW, COMBATEXP);
						break;
					case WP_LONGBOW:
					case WP_GREATBOW:
						change_skill(du, SK_LONGBOW, COMBATEXP);
						break;
					case WP_CATAPULT:
						change_skill(du, SK_CATAPULT, COMBATEXP);
						break;
					}

					if (t.index < t.fighter->horses)
						change_skill(du, SK_RIDING, COMBATEXP);

				}

				if (get_skill(du, SK_MAGIC) > 0)
					change_skill(du, SK_MAGIC, COMBATEXP * du->number);

				fset(df, FIG_COMBATEXP);
			}
		}
	}
#endif

	/* Wir benutzen drifted, um uns zu merken, ob ein Schiff
	 * schonmal Schaden genommen hat. (moved und drifted
	 * sollten in flags �berf�hrt werden */

	for (sh=r->ships; sh; sh=sh->next) sh->drifted = false;

	for (fi = fighters->begin; fi != fighters->end; ++fi) {
		fighter *df = *fi;
		unit *du = df->unit;
		item * l;

		for (l=df->loot; l; l=l->next) {
			const item_type * itype = l->type;
			sprintf(buf, "%s erbeute%s %d %s.", unitname(du), du->number==1?"t":"n",
				l->number, locale_string(default_locale, resourcename(itype->rtype, l->number!=1)));
			fbattlerecord(du->faction, r, buf);
			i_change(&du->items, itype, l->number);
		}

		/* Wenn sich die Einheit auf einem Schiff befindet, wird
		 * dieses Schiff besch�digt. Andernfalls ein Schiff, welches
		 * evt. zuvor verlassen wurde. */

		if (du->ship) sh = du->ship; else sh = leftship(du);

		if (sh && sh->drifted == false && b->turn+(b->has_tactics_turn?1:0)>2) {
			damage_ship(sh, 0.20);
			sh->drifted = true;
		}
	}

	if (battle_was_relevant) {
		ship **sp = &r->ships;
		while (*sp) {
			ship * sh = *sp;
			sh->drifted = false;
			if (sh->damage >= sh->size * DAMAGE_SCALE) {
				destroy_ship(sh, r);
			}
			if (*sp==sh) sp=&sh->next;
		}
	}
#ifdef TROLLSAVE
	free(trollsave);
#endif

	sprintf(buf, "The battle lasted %d turns, %s and %s.\n",
			b->turn,
			b->has_tactics_turn==true?"had a tactic turn":"had no tactic turn",
			battle_was_relevant==true?"was relevant":"was not relevant.");
	battledebug(buf);
}

static void
battle_punit(unit * u, battle * b)
{
	bfaction * bf;
	strlist *S, *x;

	for (bf = b->factions;bf;bf=bf->next) {
		faction *f = bf->faction;

		S = 0;
		spunit(&S, f, u, 4, see_battle);
		for (x = S; x; x = x->next) {
			fbattlerecord(f, u->region, x->s);
			if (u->faction == f)
				battledebug(x->s);
		}
		if (S)
			freestrlist(S);
	}
}

static void
print_fighters(battle * b, cvector * fighters)
{
	fighter *df;
	int lastrow = -1;

	for_each(df, *fighters) {
		unit *du = df->unit;

		int row = get_unitrow(df);

		if (row != lastrow) {
			char zText[32];
			sprintf(zText, "... in der %d. Kampflinie:", row);
			battlerecord(b, zText);
			lastrow = row;
		}
		battle_punit(du, b);
	}
	next(df);
}

static void
print_header(battle * b)
{
	bfaction * bf;
	void **fi;
	cvector *fighters = &b->fighters;
	boolean * seen = malloc(sizeof(boolean)*cv_size(&b->sides));

	for (bf=b->factions;bf;bf=bf->next) {
		faction * f = bf->faction;
		const char * lastf = NULL;
		boolean first = false;

		strcpy(buf, "Der Kampf wurde ausgel�st von ");
		memset(seen, 0, sizeof(boolean)*cv_size(&b->sides));

		for (fi = fighters->begin; fi != fighters->end; ++fi) {
			fighter *df = *fi;
			if (!fval(df, FIG_ATTACKED)) continue;
			if (!seen[df->side->index] ) {
				if (first) strcat(buf, ", ");
				if (lastf) {
					strcat(buf, lastf);
					first = true;
				}
				if (seematrix(f, df->side) == true)
					lastf = sidename(df->side, false);
				else
					lastf = "einer unbekannten Partei";
				seen[df->side->index] = true;
			}
		}
		if (first) strcat(buf, " und ");
		if (lastf) strcat(buf, lastf);
		strcat(buf, ".");
		fbattlerecord(f, b->region, buf);
	}
	free(seen);
}

static void
print_stats(battle * b)
{
	side *s2;
	side *side;
	int i = 0;
	for_each(side, b->sides) {
		bfaction *bf;
		char *k;
		boolean komma;

		++i;

		for (bf=b->factions;bf;bf=bf->next) {
			faction * f = bf->faction;
#ifndef FASTENEMY
			struct enemy * e;
#endif
			fbattlerecord(f, b->region, " ");
			sprintf(buf, "Heer %d: %s", side->index,
					seematrix(f, side)
					? sidename(side,false) : LOC(f->locale, "unknown_faction"));
			fbattlerecord(f, b->region, buf);
			strcpy(buf, LOC(f->locale, "battle_opponents"));
			komma = false;
			for_each(s2, b->sides) {
				if (enemy(s2, side))
				{
					if (seematrix(f, s2) == true) {
						sprintf(buf, "%s%s Heer %d(%s)", buf, komma++ ? "," : "",
								s2->index, sideabkz(s2,false));
					} else {
						sprintf(buf, "%s%s Heer %d(Unb)", buf, komma++ ? "," : "",
								s2->index);
					}
				}
			}
			next(s2);
			fbattlerecord(f, b->region, buf);
			strcpy(buf, "Attacke gegen:");
			komma = false;
#ifdef FASTENEMY
			for_each(s2, b->sides) {
				if (side->enemy[s2->index] & E_ATTACKING) {
					if (seematrix(f, s2) == true) {
						sprintf(buf, "%s%s Heer %d(%s)", buf, komma++ ? "," : "",
								s2->index, sideabkz(s2,false));
					} else {
						sprintf(buf, "%s%s Heer %d(Unb)", buf, komma++ ? "," : "",
								s2->index);
					}
				}
			}
			next(s2);
#else
			for (i=0;i!=16;i++)
			for (e=side->enemies[i]; e; e=e->nexthash) if (e->attacking) {
				struct side * s2 = e->side;
				if (seematrix(f, s2) == true) {
					sprintf(buf, "%s%s Heer %d(%s)", buf, komma++ ? "," : "",
							s2->index, sideabkz(s2,false));
				} else {
					sprintf(buf, "%s%s Heer %d(Unb)", buf, komma++ ? "," : "",
							s2->index);
				}
			}
#endif
			fbattlerecord(f, b->region, buf);
		}
		buf[77] = (char)0;
		for (k = buf; *k; ++k) *k = '-';
		battlerecord(b, buf);
		print_fighters(b, &side->fighters);
	}
	next(side);

	battlerecord(b, " ");

	/* Besten Taktiker ermitteln */

	b->max_tactics = 0;

	for_each(side, b->sides) {
		if (cv_size(&side->leader.fighters))
			b->max_tactics = max(b->max_tactics, side->leader.value);
	} next(side);

	if (b->max_tactics > 0) {
		for_each(side, b->sides) {
			if (side->leader.value == b->max_tactics) {
				fighter *tf;
				for_each(tf, side->leader.fighters) {
					unit *u = tf->unit;
					if (!fval(tf, FIG_ATTACKED))
						battlemsg(b, u, "�berrascht den Gegner.");
					else
						battlemsg(b, u, "konnte dem Gegner eine Falle stellen.");
				} next(tf);
			}
		} next(side);
	}

}

fighter *
make_fighter(battle * b, unit * u, boolean attack)
{
#define WMAX 16
	weapon weapons[WMAX];
	int owp[WMAX];
	int dwp[WMAX];
	int w = 0;
	region *r = b->region;
	item * itm;
	fighter *fig = NULL;
	int i, t = eff_skill(u, SK_TACTICS, r);
	side *s2, *s1 = NULL;
	int h;
	int berserk;
	int strongmen;
	int speeded, speed;
	boolean pr_aid = false;
	boolean stealth = (boolean)((fval(u, FL_PARTEITARNUNG)!=0)?true:false);
	int rest;
	const attrib * a = a_find(u->attribs, &at_group);
	const group * g = a?(const group*)a->data.v:NULL;
#ifdef STEALTHFACTION
	const attrib *a2 = a_find(u->attribs, &at_otherfaction);
	const faction *stealthfaction = a2?findfaction(a2->data.i):NULL;
#endif

	/* Illusionen und Zauber kaempfen nicht */
	if (fval(u->race, RCF_ILLUSIONARY) || idle(u->faction))
		return NULL;

	for_each(s2, b->sides) {
		if (s2->bf->faction == u->faction
				&& s2->stealth==stealth
#ifdef STEALTHFACTION
				&& s2->stealthfaction == stealthfaction
#endif
				) {
			if (s2->group==g) {
				s1 = s2;
				break;
			}
		}
	} next(s2);

	/* aliances are moved out of make_fighter and will be handled later */
#ifdef STEALTHFACTION
	if (!s1) s1 = make_side(b, u->faction, g, stealth, stealthfaction);
#else
	if (!s1) s1 = make_side(b, u->faction, g, stealth);
#endif
	/* Zu diesem Zeitpunkt ist attacked noch 0, da die Einheit f�r noch
	 * keinen Kampf ausgew�hlt wurde (sonst w�rde ein fighter existieren) */
	fig = calloc(1, sizeof(struct fighter));

	cv_pushback(&s1->fighters, fig);
	cv_pushback(&b->fighters, fig);

	fig->unit = u;
	/* In einer Burg mu� man a) nicht Angreifer sein, und b) drin sein, und
	 * c) noch Platz finden. d) menschlich sein */
	if (attack) fset(fig, FIG_ATTACKED);
	if ((!attack) &&
	    u->building &&
	    u->building->sizeleft >= u->number &&
	    playerrace(u->race)) {
		fig->building = u->building;
		fig->building->sizeleft -= u->number;
	}
	fig->status = u->status;
	fig->side = s1;
	fig->alive = u->number;
	fig->side->alive += u->number;
	fig->side->battle->alive += u->number;
	fig->catmsg = -1;

	/* Freigeben nicht vergessen! */
	fig->person = calloc(fig->alive, sizeof(struct person));

	h = u->hp / u->number;
	assert(h);
	rest = u->hp % u->number;
	speeded = get_cursedmen(u->attribs, C_SPEED, 0);
	speed   = get_curseeffect(u->attribs, C_SPEED, 0);

	/* Effekte von Alchemie */
	berserk = get_effect(u, oldpotiontype[P_BERSERK]);

	/* Effekte von Spr�chen */
	/* Effekte von Artefakten */
	strongmen = min(fig->unit->number, get_item(u, I_TROLLBELT));

	for (a = a_find(u->attribs, &at_prayer_effect); a; a = a->nexttype) {
		if (a->data.sa[0] == PR_AID) {
			pr_aid = true;
			break;
		}
	}

	/* Hitpoints, Attack- und Defence-Boni f�r alle Personen */
	for (i = 0; i < fig->alive; i++) {
		assert(i < fig->unit->number);
		fig->person[i].hp = h;
		if (i < rest)
			fig->person[i].hp++;

		if (i < speeded)
			fig->person[i].speed = speed;
		else
			fig->person[i].speed = 1;

		if (i < berserk) {
			fig->person[i].attack++;
		}
		/* Leute mit einem Aid-Prayer bekommen +1 auf fast alles. */
		if (pr_aid) {
			fig->person[i].attack++;
			fig->person[i].defence++;
			fig->person[i].damage++;
			fig->person[i].damage_rear++;
			fig->person[i].flags |= FL_HERO;
		}
		/* Leute mit Kraftzauber machen +2 Schaden im Nahkampf. */
		if (i < strongmen) {
			fig->person[i].damage += 2;
		}
	}

	/* F�r alle Waffengattungne wird bestimmt, wie viele der Personen mit
	 * ihr k�mpfen k�nnten, und was ihr Wert darin ist. */
	if (u->race->battle_flags & BF_EQUIPMENT) {
		int oi=0, di=0;
		for (itm=u->items;itm;itm=itm->next) {
			const weapon_type * wtype = resource2weapon(itm->type->rtype);
			if (wtype==NULL || itm->number==0) continue;
			weapons[w].offskill = weapon_skill(wtype, u, true);
			weapons[w].defskill = weapon_skill(wtype, u, false);
			weapons[w].type = wtype;
			weapons[w].used = 0;
			weapons[w].count = itm->number;
			++w;
		}
		fig->weapons = calloc(sizeof(weapon), w+1);
		memcpy(fig->weapons, weapons, w*sizeof(weapon));

		for (i=0; i!=w; ++i) {
			int j, o=0, d=0;
			for (j=0; j!=i; ++j) {
				if (fig->weapons[j].defskill>=fig->weapons[i].defskill) ++d;
				if (fig->weapons[j].offskill>=fig->weapons[i].offskill) ++o;
			}
			for (j=i+1; j!=w; ++j) {
				if (fig->weapons[j].defskill>fig->weapons[i].defskill) ++d;
				if (fig->weapons[j].offskill>fig->weapons[i].offskill) ++o;
			}
			owp[o] = i;
			dwp[d] = i;
		}
		/* jetzt enthalten owp und dwp eine absteigend schlechter werdende Liste der Waffen
		 * oi and di are the current index to the sorted owp/dwp arrays
		 * owp, dwp contain indices to the figther::weapons array */

		/* hand out offensive weapons: */
		for (i=0; i!=fig->alive; ++i) {
			int wpless = weapon_skill(NULL, u, true);
			while (oi!=w && fig->weapons[owp[oi]].used==fig->weapons[owp[oi]].count) {
				++oi;
			}
			if (oi==w) break; /* no more weapons available */
			if (fig->weapons[owp[oi]].offskill<wpless) continue; /* we fight better with bare hands */
			fig->person[i].weapon = &fig->weapons[owp[oi]];
			if (fig->person[i].weapon->type->reload) {
				fig->person[i].reload = rand() % fig->person[i].weapon->type->reload;
			}
			++fig->weapons[owp[oi]].used;
		}
		/* hand out defensive weapons. No missiles, please. */
		for (di=0, i=0; i!=fig->alive; ++i) {
			if (fig->person[i].weapon==NULL) continue;
			while (di!=w && (fig->weapons[dwp[di]].used==fig->weapons[dwp[di]].count || fval(fig->weapons[dwp[di]].type, WTF_MISSILE))) {
				++di;
			}
			if (di==w) break; /* no more weapons available */
			if (fig->weapons[dwp[di]].defskill>fig->person[i].weapon->defskill) {
				fig->person[i].secondary = &fig->weapons[dwp[di]];
				++fig->weapons[dwp[di]].used;
			}
		}
	}

	if (i_get(u->items, &it_demonseye)) {
		char lbuf[80];
		const char * s = LOC(default_locale, rc_name(u->race, 3));
		char * c = lbuf;
		while (*s) *c++ = (char)toupper(*s++);
	 	*c = 0;
		fig->person[0].hp = unit_max_hp(u) * 3;
		sprintf(buf, "Eine Stimme ert�nt �ber dem Schlachtfeld. 'DIESES %sKIND IST MEIN. IHR SOLLT ES NICHT HABEN.'. Eine leuchtende Aura umgibt %s", lbuf, unitname(u));
		battlerecord(b, buf);
	}

	s1->size[statusrow(fig->status)] += u->number;
	s1->size[SUM_ROW] += u->number;
	if (u->race->battle_flags & BF_NOBLOCK) {
		s1->nonblockers[statusrow(fig->status)] += u->number;
#ifdef FAST_GETUNITROW
		b->nonblockers = true;
#endif
	}

	if (fig->unit->race->flags & RCF_HORSE) {
		fig->horses = fig->unit->number;
		fig->elvenhorses = 0;
	} else {
		fig->horses = get_item(u, I_HORSE);
		fig->elvenhorses = get_item(u, I_UNICORN);
	}

	for (i = 0; i != AR_MAX; ++i)
		if (u->race->battle_flags & BF_EQUIPMENT)
			fig->armor[i] = get_item(u, armordata[i].item);


	/* Jetzt mu� noch geschaut werden, wo die Einheit die jeweils besten
	 * Werte hat, das kommt aber erst irgendwo sp�ter. Ich entscheide
	 * w�rend des Kampfes, welche ich nehme, je nach Gegner. Deswegen auch
	 * keine addierten boni. */

	/* Zuerst mal die Spezialbehandlung gewisser Sonderf�lle. */
	fig->magic = eff_skill(u, SK_MAGIC, r);

	if (fig->horses) {
		if ((r->terrain != T_PLAIN && r->terrain != T_HIGHLAND
				&& r->terrain != T_DESERT) || r_isforest(r)
				|| eff_skill(u, SK_RIDING, r) < 2 || old_race(u->race) == RC_TROLL)
			fig->horses = 0;
	}

	if (fig->elvenhorses) {
		if (eff_skill(u, SK_RIDING, r) < 5 || old_race(u->race) == RC_TROLL)
			fig->elvenhorses = 0;
	}

	/* Schauen, wie gut wir in Taktik sind. */
	if (t > 0 && old_race(u->race) == RC_INSECT)
		t -= 1 - (int) log10(fig->side->size[SUM_ROW]);
	if (t > 0 && get_unitrow(fig) == FIGHT_ROW)
		t += 1;
#ifdef TACTICS_MALUS
	if (t > 0 && get_unitrow(fig) > BEHIND_ROW)
		t -= 1;
#endif
#if TACTICS_RANDOM
	if (t > 0) {
		int bonus = 0;

		for (i = 0; i < fig->alive; i++) {
			int p_bonus = 0;
			int rnd;

			do {
				rnd = rand()%100;
				if (rnd >= 40 && rnd <= 69)
					p_bonus += 1;
				else if (rnd <= 89)
					p_bonus += 2;
				else
					p_bonus += 3;
			} while(rnd >= 97);
			bonus = max(p_bonus, bonus);
		}
		t += bonus;
	}
	/* Nicht gut, da nicht personenbezogen. */
	/* if (t > 0) t += rand() % TACTICS_RANDOM; */
	/* statt +/- 2 kann man auch random(5) nehmen */
#endif
	add_tactics(&fig->side->leader, fig, t);
	return fig;
}


static fighter *
join_battle(battle * b, unit * u, boolean attack)
{
	fighter *c = NULL;
	fighter *fig;

	for_each(fig, b->fighters) {
		if (fig->unit == u) {
			c = fig;
			if (attack) fset(fig, FIG_ATTACKED);
			break;
		}
	}
	next(fig);
	if (!c) c = make_fighter(b, u, attack);
	return c;
}
static const char *
simplename(region * r)
{
	int i;
	static char name[17];
	const char * cp = rname(r, NULL);
	for (i=0;*cp && i!=16;++i, ++cp) {
		int c = *(unsigned char*)cp;
		while (c && !isalpha(c) && !isspace(c)) {
			++cp;
			c = *(unsigned char*)cp;
		}
		if (isspace(c)) name[i] = '_';
		else name[i] = *cp;
	}
	name[i]=0;
	return name;
}

static battle *
make_battle(region * r)
{
	battle *b = calloc(1, sizeof(struct battle));
	unit *u;
	faction *lastf = NULL;
	bfaction * bf;
	static int max_fac_no = 0; /* need this only once */

	if (!nobattledebug) {
		char zText[MAX_PATH];
		char zFilename[MAX_PATH];
		sprintf(zText, "%s/battles", basepath());
		makedir(zText, 0700);
#ifdef HAVE_ZLIB
		sprintf(zFilename, "%s/battle-%d-%s.log.gz", zText, obs_count, simplename(r));
		bdebug = gzopen(zFilename, "w");
#elif HAVE_BZ2LIB
		sprintf(zFilename, "%s/battle-%d-%s.log.bz2", zText, obs_count, simplename(r));
		bdebug = BZ2_bzopen(zFilename, "w");+
#else
		sprintf(zFilename, "%s/battle-%d-%s.log", zText, obs_count, simplename(r));
		bdebug = fopen(zFilename, "w");
#endif
		if (!bdebug) log_error(("battles k�nnen nicht debugged werden\n"));
		else {
			dbgprintf((bdebug, "In %s findet ein Kampf statt:", rname(r, NULL)));
		}
		obs_count++;
	}
	nextside = 0;

	/* cv_init(&b->sides); */
	b->region = r;
	b->plane = getplane(r);
	/* Finde alle Parteien, die den Kampf beobachten k�nnen: */
	list_foreach(unit, r->units, u)
		if (u->number > 0) {
		if (lastf != u->faction) {	/* Speed optimiert, aufeinanderfolgende
								 * * gleiche nicht getestet */
			lastf = u->faction;
			for (bf=b->factions;bf;bf=bf->next)
				if (bf->faction==lastf) break;
			if (!bf) {
				bf = calloc(sizeof(bfaction), 1);
				++b->nfactions;
				bf->faction = lastf;
				bf->next = b->factions;
				b->factions = bf;
			}
		}
	}
	list_next(u);

	for (bf=b->factions;bf;bf=bf->next) {
		faction * f = bf->faction;
		max_fac_no = max(max_fac_no, f->no);
	}
	return b;
}

static void
free_side(side * si)
{
#ifndef FASTENEMY
	int i;
	for (i=0;i!=16;++i) while (si->enemies[i]) {
		struct enemy * e = si->enemies[i]->nexthash;
		free(si->enemies[i]);
		si->enemies[i] = e;
	}
#endif
	cv_kill(&si->fighters);
}

static void
free_fighter(fighter * fig)
{
	free(fig->person);
}

static void
free_battle(battle * b)
{
	side *side;
	fighter *fighter;
	meffect *meffect;
	bfaction * bf;
	int max_fac_no = 0;

	if (bdebug) {
#ifdef HAVE_ZLIB
		gzclose(bdebug);
#elif HAVE_BZ2LIB
		BZ2_bzclose(bdebug);
#else
		fclose(bdebug);
#endif
	}

	for (bf=b->factions;bf;bf=bf->next) {
		faction * f = bf->faction;
		max_fac_no = max(max_fac_no, f->no);
	}

	for_each(side, b->sides) {
		free_side(side);
		free(side);
	}
	next(side);
	cv_kill(&b->sides);
	for_each(fighter, b->fighters) {
		free_fighter(fighter);
		free(fighter);
	}
	next(fighter);
	cv_kill(&b->fighters);
	for_each(meffect, b->meffects) {
		free(meffect);
	}
	next(meffect);
	cv_kill(&b->meffects);
/*
	cv_kill(&b->factions);
*/
}

static int *
get_alive(battle * b, side * s, faction * vf, boolean see)
{
	static int alive[NUMROWS];
	fighter *f;
	memset(alive, 0, NUMROWS * sizeof(int));
	for_each(f, s->fighters) {
		if (f->alive && seematrix(vf, s)==see)
			alive[get_unitrow(f)] += f->alive;
	} next(f);
	return alive;
}

static int
battle_report(battle * b)
{
	side *s, *s2;
	boolean cont = false;
	boolean komma;
	char buf2[1024];
	bfaction *bf;

	buf[0] = 0;

	for_each(s, b->sides) {
		for_each(s2, b->sides) {
			if (s->alive-s->removed > 0 && s2->alive-s2->removed > 0 && enemy(s, s2))
			{
				cont = true;
				s->bf->lastturn = b->turn;
				s2->bf->lastturn = b->turn;
			}
		} next(s2);
	} next(s);

	printf(" %d", b->turn);
	fflush(stdout);

	for (bf=b->factions;bf;bf=bf->next) {
		faction * fac = bf->faction;
		fbattlerecord(fac, b->region, " ");
		if (cont == true)
			sprintf(buf2, "Einheiten vor der %d. Runde:",
					b->turn);
		else
			sprintf(buf2, "Einheiten nach dem Kampf:");
		fbattlerecord(fac, b->region, buf2);
		buf2[0] = 0;
		komma   = false;
		for_each(s, b->sides) {
			if (s->alive) {
				int r, k = 0, * alive = get_alive(b, s, fac, seematrix(fac, s));
				if (!seematrix(fac, s)) {
					sprintf(buf, "%sHeer %2d(Unb): ",
						komma==true?", ":"", s->index);
				} else {
					sprintf(buf, "%sHeer %2d(%s): ",
						komma==true?", ":"", s->index,sideabkz(s,false));
				}
				{
					int l = FIGHT_ROW;
					for (r=FIGHT_ROW;r!=NUMROWS;++r) {
						if (alive[r]) {
							if (l!=FIGHT_ROW) scat("+");
							while(k--) scat("0+");
							icat(alive[r]);
							k = 0;
							l=r+1;
						} else ++k;
					}
				}

				strcat(buf2, buf);
				if (komma == false) {
					komma = true;
				}
			}
		} next(s);
		fbattlerecord(fac, b->region, buf2);
	}
	return cont;
}

static void
join_allies(battle * b)
{
	region * r = b->region;
	unit * u;
	/* Die Anzahl der Teilnehmer kann sich in dieser Routine �ndern.
	 * Deshalb mu� das Ende des Vektors vorher gemerkt werden, damit
	 * neue Parteien nicht mit betrachtet werden:
	 */
	int size = cv_size(&b->sides);
	for (u=r->units;u;u=u->next)
		/* Was ist mit Schiffen? */
		if (u->status != ST_FLEE && u->status != ST_AVOID && !fval(u, FL_MOVED) && u->number > 0)
	{
		int si;
		faction * f = u->faction;
		fighter * c = NULL;
		for (si = 0; si != size; ++si) {
			int se;
			side *s = b->sides.begin[si];
#ifdef NOAID
			/* Wenn alle attackierten noch FL_NOAIDF haben, dann k�mpfe nicht mit. */
			if (fval(s->bf->faction, FL_NOAIDF)) continue;
#endif
			if (s->bf->faction!=f) {
				/* Wenn wir attackiert haben, kommt niemand mehr hinzu: */
				if (s->bf->attacker) continue;
				/* Wenn alliierte attackiert haben, helfen wir nicht mit: */
				if (s->bf->faction!=f && s->bf->attacker) continue;
				/* alliiert m�ssen wir schon sein, sonst ist's eh egal : */
				if (!allied(u, s->bf->faction, HELP_FIGHT)) continue;
#ifdef STEALTHFACTION
				/* wenn die partei verborgen ist, oder gar eine andere
				 * vorgespiegelt wird, und er sich uns gegen�ber nicht zu
				 * erkennen gibt, helfen wir ihm nicht */
				if (s->stealthfaction){
					if(!allysfm(s, u->faction, HELP_FSTEALTH)) {
						continue;
					}
				}
#endif
			}
			/* einen alliierten angreifen d�rfen sie nicht, es sei denn, der
			 * ist mit einem alliierten verfeindet, der nicht attackiert
			 * hat: */
			for (se = 0; se != size; ++se) {
				side * evil = b->sides.begin[se];
				if (u->faction==evil->bf->faction) continue;
				if (allied(u, evil->bf->faction, HELP_FIGHT) &&
					!evil->bf->attacker) continue;
				if (enemy(s, evil)) break;
			}
			if (se==size) continue;
			/* Wenn die Einheit belagert ist, mu� auch einer der Alliierten belagert sein: */
			if (besieged(u)) {
				void ** fi;
				boolean siege = false;
				for (fi = s->fighters.begin; !siege && fi != s->fighters.end; ++fi) {
					fighter *ally = *fi;
					if (besieged(ally->unit)) siege = true;
				}
				if (!siege) continue;
			}
			/* keine Einw�nde, also soll er mitmachen: */
			if (!c) c = join_battle(b, u, false);
			if (!c) continue;
			/* Die Feinde meiner Freunde sind meine Feinde: */
			for (se = 0; se != size; ++se) {
				side * evil = b->sides.begin[se];
				if (evil->bf->faction!=u->faction && enemy(s, evil)) {
					set_enemy(evil, c->side, false);
				}
			}
		}
	}
}

extern struct item_type * i_silver;

void
flee(const troop dt)
{
	fighter * fig = dt.fighter;
	unit * u = fig->unit;
	int carry = personcapacity(u) - u->race->weight;
	int money;

	item ** ip = &u->items;
	while (*ip) {
		item * itm = *ip;
		const item_type * itype = itm->type;
		int keep = 0;

		if (fval(itype, ITF_ANIMAL)) {
			/* Regel�nderung: Man mu� das Tier nicht reiten k�nnen,
			 * um es vom Schlachtfeld mitzunehmen, ist ja nur
			 * eine Region weit. * */
			keep = min(1, itm->number);
			/* da ist das weight des tiers mit drin */
			carry += itype->capacity - itype->weight;
		} else if (itm->type->weight <= 0) {
			/* if it doesn't weigh anything, it won't slow us down */
			keep = itm->number;
		}
		/* jeder troop nimmt seinen eigenen Teil der Sachen mit */
		if (keep>0){
			if (itm->number==keep) {
				i_add(&fig->run.items, i_remove(ip, itm));
			} else {
				item *run_itm = i_new(itype, keep);
				i_add(&fig->run.items, run_itm);
				i_change(ip, itype, -keep);
			}
		}
		if (*ip==itm) ip = &itm->next;
	}

	/* we will take money with us */
	money = get_money(u);
	/* nur ganzgeflohene/resttote Einheiten verlassen die Region */
	/* money = min(money, MAINTENANCE); */
	if (money > carry) money = carry;
	if (money > 0) {
		i_change(&u->items, i_silver, -money);
		i_change(&fig->run.items, i_silver, +money);
	}

	fig->run.hp += fig->person[dt.index].hp;
	++fig->run.number;

	remove_troop(dt);
}

#ifdef DELAYED_OFFENSE
static boolean
guarded_by(region * r, faction * f)
{
	unit * u;
	for (u=r->units;u;u=u->next) {
		if (u->faction == f && getguard(u)) return true;
	}
	return false;
}
#endif

void
do_battle(void)
{
	region *r;
	char smallbuf[512];

	list_foreach(region, regions, r) {
		unit *u;
		battle *b = NULL;
		int sides = 0;
		side *s;
		void **fi;
		building *bu;

#ifdef NOAID
		for (u = r->units; u != NULL; u = u->next) {
			fset(u->faction, FL_NOAIDF);
		}
#endif

		/* list_foreach geht nicht, wegen flucht */
		for (u = r->units; u != NULL; u = u->next) {
			if (fval(u, FL_LONGACTION)) continue;
			if (u->number > 0) {
				strlist *sl;

				list_foreach(strlist, u->orders, sl) {
					if (igetkeyword(sl->s, u->faction->locale) == K_ATTACK) {
						unit *u2;
						fighter *c1, *c2;

						if(r->planep && fval(r->planep, PFL_NOATTACK)) {
							cmistake(u, sl->s, 271, MSG_BATTLE);
							list_continue(sl);
						}

						/**
						 ** Fehlerbehandlung Angreifer
						 **/
#ifdef DELAYED_OFFENSE
						if (get_moved(&u->attribs) && !guarded_by(r, u->faction)) {
							add_message(&u->faction->msgs,
								msg_message("no_attack_after_advance", "unit region command", u, u->region, sl->s));
						}
#endif
						if (fval(u, FL_HUNGER)) {
							cmistake(u, sl->s, 225, MSG_BATTLE);
							list_continue(sl);
						}

						if (u->status == ST_AVOID || u->status == ST_FLEE) {
							cmistake(u, sl->s, 226, MSG_BATTLE);
							list_continue(sl);
						}

						/* ist ein Fl�chtling aus einem andern Kampf */
						if (fval(u, FL_MOVED)) list_continue(sl);

						if (is_spell_active(r, C_PEACE)) {
							sprintf(buf, "Hier ist es so sch�n friedlich, %s m�chte "
									"hier niemanden angreifen.", unitname(u));
							mistake(u, sl->s, buf, MSG_BATTLE);
							list_continue(sl);
						}

						if (is_cursed(u->attribs, C_SLAVE, 0)) {
							sprintf(buf, "%s k�mpft nicht.", unitname(u));
							mistake(u, sl->s, buf, MSG_BATTLE);
							list_continue(sl);
						}

						/* Fehler: "Das Schiff mu� erst verlassen werden" */
						if (u->ship != NULL && rterrain(r) != T_OCEAN) {
							cmistake(u, sl->s, 19, MSG_BATTLE);
							list_continue(sl);
						}

						if (leftship(u)) {
							cmistake(u, sl->s, 234, MSG_BATTLE);
							list_continue(sl);
						}


						/* Ende Fehlerbehandlung Angreifer */

						/* attackierte Einheit ermitteln */
						u2 = getunit(r, u->faction);

						/* Beginn Fehlerbehandlung */
						/* Fehler: "Die Einheit wurde nicht gefunden" */
						if (!u2 || fval(u2, FL_MOVED) || u2->number == 0
								|| !cansee(u->faction, u->region, u2, 0)) {
							cmistake(u, sl->s, 64, MSG_BATTLE);
							list_continue(sl);
						}
						/* Fehler: "Die Einheit ist eine der unsrigen" */
						if (u2->faction == u->faction) {
							cmistake(u, sl->s, 45, MSG_BATTLE);
							list_continue(sl);
						}
						/* Fehler: "Die Einheit ist mit uns alliert" */
#ifdef STEALTHFACTION
						/* alliances are dissolved */
						if (allied(u, u2->faction, HELP_FIGHT)) {
							ally *sf, **sfp;
							attrib * a = a_find(u->attribs, &at_group);

							if (a) {
								sfp = &((group*)a->data.v)->allies;
							} else {
								sfp = &u->faction->allies;
							}

							for(sf=*sfp; sf; sf = sf->next) {
								if(sf->faction == u2->faction) {
									removelist(sfp, sf);
									break;
								}
							}

							a = a_find(u2->attribs, &at_group);

							if(a) {
								sfp = &((group*)a->data.v)->allies;
							} else {
								sfp = &u2->faction->allies;
							}

							for(sf=*sfp; sf; sf = sf->next) {
								if(sf->faction == u->faction) {
									removelist(sfp, sf);
									break;
								}
							}
						}
#else
						if (allied(u, u2->faction, HELP_FIGHT)) {
							cmistake(u, sl->s, 47, MSG_BATTLE);
							list_continue(sl);
						}
#endif
						/* xmas */
						if (u2->no==atoi36("xmas") && old_race(u2->irace)==RC_GNOME) {
							a_add(&u->attribs, a_new(&at_key))->data.i = atoi36("coal");
							sprintf(buf, "%s ist b�se gewesen...", unitname(u));
							mistake(u, sl->s, buf, MSG_BATTLE);
							list_continue(sl);
						}if (u2->faction->age < IMMUN_GEGEN_ANGRIFF) {
							add_message(&u->faction->msgs,
						  	msg_error(u, findorder(u, u->thisorder), "newbie_immunity_error", "turns", IMMUN_GEGEN_ANGRIFF));
							list_continue(sl);
						}
						/* Fehler: "Die Einheit ist mit uns alliert" */
						if (is_cursed(u->attribs, C_CALM, u2->faction->unique_id)) {
							cmistake(u, sl->s, 47, MSG_BATTLE);
							list_continue(sl);
						}
						/* Ende Fehlerbehandlung */

						if (!b) {
							b = make_battle(r);
						}
						c1 = join_battle(b, u, true);
						c2 = join_battle(b, u2, false);

#ifdef NOAID
						/* Hat die attackierte Einheit keinen Noaid-Status,
						 * wird das Flag von der Faction genommen, andere
						 * Einheiten greifen ein. */
						if (!fval(u2, FL_NOAID)) freset(u2->faction, FL_NOAIDF);
#endif
						if (c1 && c2) {
							/* Merken, wer Angreifer ist, f�r die R�ckzahlung der
							 * Pr�combataura bei kurzem Kampf. */
							c1->side->bf->attacker = true;

							set_enemy(c1->side, c2->side, true);
							if (!enemy(c1->side, c2->side)) {
								sprintf(buf, "%s attackiert %s", sidename(c1->side, false),
										sidename(c2->side, false));
								battledebug(buf);
							}
							sides = 1;
						}
					}
				}
				list_next(sl);
			}
		}
		if (!b)
			list_continue(r);

		/* Bevor wir die alliierten hineinziehen, sollten wir schauen, *
		 * Ob jemand fliehen kann. Dann er�brigt sich das ganze ja
		 * vielleicht schon. */
		print_header(b);
		if (!sides) {
			/* Niemand mehr da, Kampf kann nicht stattfinden. */
			battlerecord(b, "Der Kampf wurde abgebrochen, da alle "
					"Verteidiger flohen.");
			free_battle(b);
			free(b);
			list_continue(r);
		}
		join_allies(b);

		/* Jetzt reinitialisieren wir die Matrix nochmal, um auch die passiv
		 * in den Kampf gezogenen korrekt zu behandeln. */

		/* Jetzt wird gek�mpft, je COMBAT_TURNS Runden lang, plus
		 * evtl.taktik: */

		/* Alle Mann raus aus der Burg! */
		list_foreach(building, rbuildings(r), bu) {
			bu->sizeleft = bu->size;
		}
		list_next(bu);

		/* Gibt es eine Taktikrunde ? */
		b->turn = 1;
#ifndef NEW_TACTICS
		if (cv_size(&b->leaders)) {
			b->turn=0;
			b->has_tactics_turn = true;
		} else {
			b->has_tactics_turn = false;
		}
#endif

		/* PRECOMBATSPELLS */
		do_combatmagic(b, DO_PRECOMBATSPELL);

		/* Nun erstellen wir eine Liste von allen K�mpfern, die wir
		 * dann scramblen */
		/* Zuerst werden sie wild gemischt, und dann wird (stabil) *
		 * nach der Kampfreihe sortiert */
		v_scramble(b->fighters.begin, b->fighters.end);
		v_sort(b->fighters.begin, b->fighters.end, (v_sort_fun) sort_fighterrow);
		for_each(s, b->sides) {
			v_sort(s->fighters.begin, s->fighters.end, (v_sort_fun) sort_fighterrow);
		}
		next(side);

		print_stats(b); /* gibt die Kampfaufstellung aus */
		printf("%s (%d, %d) : ", rname(r, NULL), r->x, r->y);

		b->dh = 0;
		/* K�mpfer z�hlen und festlegen, ob es ein kleiner Kampf ist */
		for (fi = b->fighters.begin; fi != b->fighters.end; ++fi) {
			fighter *fig = *fi;
			b->dh += fig->unit->number;
		}
		if (b->dh <= 30) {
			b->small = true;
		} else {
			b->small = false;
		}

		for (;battle_report(b) && b->turn<=COMBAT_TURNS;++b->turn) {
			char lbuf[256];
			int flee_ops = 1;
			int i;

			sprintf(lbuf, "*** Runde: %d", b->turn);
			battledebug(lbuf);

			/* Der doppelte Versuch immer am Anfang der 1. Kampfrunde, nur
			 * ein Versuch vor der Taktikerrunde. Die 0. Runde ist immer
			 * die Taktikerrunde. */
			if (b->turn==1)
				flee_ops = 2;

			for (i=1;i<=flee_ops;i++) {
				for (fi = b->fighters.begin; fi != b->fighters.end; ++fi) {
					fighter *fig = *fi;
					unit *u = fig->unit;
					troop dt;
					int runners = 0;
					/* Flucht nicht bei mehr als 600 HP. Damit Wyrme t�tbar bleiben. */
					int runhp = min(600,(int)(0.9+unit_max_hp(u)*hpflee(u->status)));
					side *side = fig->side;
					region *r = NULL;
					if (fval(u->race, RCF_UNDEAD) || old_race(u->race) == RC_SHADOWKNIGHT) continue;
					if (u->ship) continue;
					dt.fighter = fig;
					if (!fig->run.region) fig->run.region = fleeregion(u);
					r = fig->run.region;
					if (!r)
						continue;
					dt.index = fig->alive - fig->removed;
					while (side->size[SUM_ROW] && dt.index != 0) {
						--dt.index;
						assert(dt.index>=0 && dt.index<fig->unit->number);
						assert(fig->person[dt.index].hp > 0);

						/* Versuche zu fliehen, wenn
						 * - Kampfstatus fliehe
						 * - schwer verwundet
						 * - in panik (Zauber)
						 * aber nicht, wenn der Zaubereffekt Held auf dir liegt!
						 */
						if ((u->status == ST_FLEE
									|| fig->person[dt.index].hp <= runhp
									|| (fig->person[dt.index].flags & FL_PANICED))
								&& !(fig->person[dt.index].flags & FL_HERO))
						{
							double ispaniced = 0.0;
							if (fig->person[dt.index].flags & FL_PANICED) {
								ispaniced = EFFECT_PANIC_SPELL;
							}
							if (chance(min(fleechance(u)+ispaniced, 0.90))) {
								++runners;
								flee(dt);
								if (b->small) {
									sprintf(smallbuf, "%s/%d gelingt es, vom Schlachtfeld zu entkommen.",
										unitname(fig->unit), dt.index);
									battlerecord(b, smallbuf);
								}
							} else if (b->small) {
								sprintf(smallbuf, "%s/%d versucht zu fliehen, wird jedoch aufgehalten.",
										unitname(fig->unit), dt.index);
								battlerecord(b, smallbuf);
							}
						}
					}
					if(runners > 0) {
						sprintf(lbuf, "Flucht: %d aus %s", runners, itoa36(fig->unit->no));
						battledebug(lbuf);
					}
				}
			}

			/* this has to be calculated before the actual attacks take
			 * place because otherwise the dead would not strike the
			 * round they die. */
			for (fi = b->fighters.begin; fi != b->fighters.end; ++fi) {
				fighter *fig = *fi;
				fig->fighting = fig->alive - fig->removed;
			}

			for (fi = b->fighters.begin; fi != b->fighters.end; ++fi) {
				fighter *fig = *fi;

				/* ist in dieser Einheit noch jemand handlungsf�hig? */
				if (fig->fighting <= 0) continue;

				/* Taktikrunde: */
				if (b->turn == 0) {
					side    *side;
					boolean yes = false;

					for_each(side, b->sides) {
						if (b->max_tactics > 0
							&& side->leader.value == b->max_tactics
							&& helping(side, fig->side))
						{
							yes = true;
							break;
						}
					} next(side);
					if (!yes)
						continue;
				}
				/* Handle the unit's attack on someone */
				do_attack(fig);
			}

			/* Regeneration */
			for (fi = b->fighters.begin; fi != b->fighters.end; ++fi) {
				fighter *fig = *fi;

				if(fspecial(fig->unit->faction, FS_REGENERATION)) {
					fig->fighting = fig->alive - fig->removed;
					if(fig->fighting == 0) continue;
					do_regenerate(fig);
				}
			}
		}

		printf("\n");

		/* Auswirkungen berechnen: */
		aftermath(b);
		/*
#if MALLOCDBG
		assert(_CrtCheckMemory());
#endif
		*/
		/* Hier ist das Gefecht beendet, und wir k�nnen die
		 * Hilfsstrukturen * wieder l�schen: */

		if (b) {
			free_battle(b);
			free(b);
		}
	}
	list_next(r);
}

/* ------------------------------------------------------------- */

/* Funktionen, die au�erhalb von battle.c verwendet werden. */
int
nb_armor(unit *u, int index)
{
	int a, av = 0;
	int geschuetzt = 0;

	if (!(u->race->battle_flags & BF_EQUIPMENT)) return AR_NONE;

	/* Normale R�stung */

	a = 0;
	do {
		if (armordata[a].shield == 0) {
			geschuetzt += get_item(u, armordata[a].item);
			if (geschuetzt > index)
				av += armordata[a].prot;
		}
		++a;
	}
	while (a != AR_MAX);

	/* Schild */

	a = 0;
	do {
		if (armordata[a].shield == 1) {
			geschuetzt += get_item(u, armordata[a].item);
			if (geschuetzt > index)
				av += armordata[a].prot;
		}
		++a;
	}
	while (a != AR_MAX);

	return av;
}


int
damage_unit(unit *u, const char *dam, boolean armor, boolean magic)
{
	int *hp = malloc(u->number * sizeof(int));
	int   h = u->hp/u->number;
	int   i, dead = 0, hp_rem = 0, heiltrank;

	/* HP verteilen */
	for (i=0; i<u->number; i++) hp[i] = h;
	h = u->hp - (u->number * h);
	for (i=0; i<h; i++) hp[i]++;

	/* Schaden */
	for (i=0; i<u->number; i++) {
		int damage = dice_rand(dam);
		if (magic) damage = (int)(damage * (100.0 - magic_resistance(u))/100.0);
		if (armor) damage -= nb_armor(u, i);
		hp[i] -= damage;
	}

	/* Auswirkungen */
	for (i=0; i<u->number; i++) {
		if (hp[i] < 0){
			heiltrank = 0;

			/* Sieben Leben */
			if (old_race(u->race) == RC_CAT && (chance(1.0 / 7))) {
				hp[i] = u->hp/u->number;
				hp_rem += hp[i];
				continue;
			}

			/* Heiltrank */
			if (get_effect(u, oldpotiontype[P_HEAL]) > 0) {
				change_effect(u, oldpotiontype[P_HEAL], -1);
				heiltrank = 1;
			} else if (get_potion(u, P_HEAL) > 0) {
				i_change(&u->items, oldpotiontype[P_HEAL]->itype, -1);
				change_effect(u, oldpotiontype[P_HEAL], 3);
				heiltrank = 1;
			}
			if (heiltrank && (chance(0.50))) {
				hp[i] = u->hp/u->number;
				hp_rem += hp[i];
				continue;
			}

		 	dead++;
		}	else {
			hp_rem += hp[i];
		}
	}

	scale_number(u, u->number - dead);
	u->hp = hp_rem;

	free(hp);

	return dead;
}
