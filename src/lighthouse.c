#include <platform.h>
#include "lighthouse.h"

#include <kernel/config.h>
#include <kernel/building.h>
#include <kernel/faction.h>
#include <kernel/plane.h>
#include <kernel/region.h>
#include <kernel/terrain.h>
#include <kernel/unit.h>
#include <util/log.h>
#include <util/attrib.h>

#include <assert.h>
#include <math.h>

attrib_type at_lighthouse = {
    "lighthouse"
    /* Rest ist NULL; tempor�res, nicht alterndes Attribut */
};

/* update_lighthouse: call this function whenever the size of a lighthouse changes
* it adds temporary markers to the surrounding regions.
* The existence of markers says nothing about the quality of the observer in
* the lighthouse, for this may change more frequently.
*/
void update_lighthouse(building * lh)
{
    if (is_building_type(lh->type, "lighthouse")) {
        region *r = lh->region;

        r->flags |= RF_LIGHTHOUSE;
        if (lh->size > 0) {
            int d = (int)log10(lh->size) + 1;
            int x;
            for (x = -d; x <= d; ++x) {
                int y;
                for (y = -d; y <= d; ++y) {
                    attrib *a;
                    region *r2;
                    int px = r->x + x, py = r->y + y;

                    pnormalize(&px, &py, rplane(r));
                    r2 = findregion(px, py);
                    if (!r2 || !fval(r2->terrain, SEA_REGION))
                        continue;
                    if (distance(r, r2) > d)
                        continue;
                    a = a_find(r2->attribs, &at_lighthouse);
                    while (a && a->type == &at_lighthouse) {
                        building *b = (building *)a->data.v;
                        if (b == lh)
                            break;
                        a = a->next;
                    }
                    if (!a) {
                        a = a_add(&r2->attribs, a_new(&at_lighthouse));
                        a->data.v = (void *)lh;
                    }
                }
            }
        }
    }
}

int lighthouse_range(const building * b, const faction * f, const unit *u)
{
    if (fval(b, BLD_MAINTAINED) && b->size >= 10) {
        int maxd = (int)log10(b->size) + 1;

        if (u && skill_enabled(SK_PERCEPTION)) {
            int sk = effskill(u, SK_PERCEPTION, 0) / 3;
            assert(u->building == b);
            assert(u->faction == f);
            if (maxd > sk) maxd = sk;
        }
        /* E3A rule: no perception req'd */
        return maxd;
    }
    return 0;
}

bool check_leuchtturm(region * r, faction * f)
{
    attrib *a;

    if (!fval(r->terrain, SEA_REGION)) {
        return false;
    }
    for (a = a_find(r->attribs, &at_lighthouse); a && a->type == &at_lighthouse;
        a = a->next) {
        building *b = (building *)a->data.v;

        assert(is_building_type(b->type, "lighthouse"));
        if (fval(b, BLD_MAINTAINED) && b->size >= 10) {
            int maxd = (int)log10(b->size) + 1;

            if (skill_enabled(SK_PERCEPTION) && f) {
                region *r2 = b->region;
                unit *u;
                int c = 0;
                int d = 0;

                for (u = r2->units; u; u = u->next) {
                    if (u->building == b) {
                        c += u->number;
                        if (c > buildingcapacity(b))
                            break;
                        if (u->faction == f) {
                            if (!d)
                                d = distance(r, r2);
                            if (maxd < d)
                                break;
                            if (effskill(u, SK_PERCEPTION, 0) >= d * 3)
                                return true;
                        }
                    }
                    else if (c)
                        break;              /* first unit that's no longer in the house ends the search */
                }
            }
            else {
                /* E3A rule: no perception req'd */
                return true;
            }
        }
    }

    return false;
}
