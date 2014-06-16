#ifndef H_DIRECTION_H
#define H_DIRECTION_H

#ifdef __cplusplus
extern "C"
{
#endif

struct locale;

typedef enum {
    D_NORTHWEST,
    D_NORTHEAST,
    D_EAST,
    D_SOUTHEAST,
    D_SOUTHWEST,
    D_WEST,
    MAXDIRECTIONS,
    D_PAUSE,
    D_SPECIAL,
    NODIRECTION = -1
} direction_t; 

direction_t finddirection(const char *s, const struct locale *);
void init_directions(const struct locale *lang);
void init_direction(const struct locale *lang, direction_t dir, const char *str);

#ifdef __cplusplus
#endif
#endif
