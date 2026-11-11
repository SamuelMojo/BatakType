/* core.c - the rules and the maths of BatakType (see core.h for the tour).
 *
 * Reading tips if your C is rusty:
 *   - "double" is a number with a decimal point; "int" is a whole number.
 *   - "const" means "this function only reads this, it won't change it".
 *   - "static" on a function means "private to this file".
 *   - a parameter like "const LetterStats *letter" is a pointer: the
 *     address of a LetterStats that lives somewhere else. "letter->attempts"
 *     reads the attempts field of the thing it points at.
 */
#include "core.h"

#include <math.h> /* pow, log2, round */

/* ------------------------------------------------------------------------ */
/* Picking the next letter                                                   */
/* ------------------------------------------------------------------------ */

double letter_weight(const LetterStats *letter, double focus) {
    if (focus <= 0.0 || letter->attempts == 0) {
        return 1.0; /* focus is off, or we know nothing about this letter */
    }
    /* The share of attempts where your first press was wrong. The "+ 2"
     * is a safety margin: one slip on a letter you've seen once shouldn't
     * count as "misses it 100% of the time". Each clean hit raises
     * attempts without raising misses, so the rate drifts back down. */
    double missRate = (double)letter->firstTryMisses / (letter->attempts + 2.0);
    return 1.0 + focus * FOCUS_GAIN * missRate;
}

/* Works out every letter's weight, with the excluded letter at 0 so it
 * can't be picked. Fills "weights" and returns their total. */
static double fill_weights(const LetterStats stats[LETTER_COUNT], double focus,
                           int exclude, double weights[LETTER_COUNT]) {
    double total = 0.0;
    for (int i = 0; i < LETTER_COUNT; i++) {
        weights[i] = (i == exclude) ? 0.0 : letter_weight(&stats[i], focus);
        total += weights[i];
    }
    return total;
}

int pick_letter(const LetterStats stats[LETTER_COUNT], double focus,
                int exclude, double random) {
    double weights[LETTER_COUNT];
    double total = fill_weights(stats, focus, exclude, weights);

    /* A "roulette wheel": imagine the weights laid end to end as slices of
     * one long strip. Pick a random point along the strip and see whose
     * slice it lands in. Bigger weight = bigger slice = picked more often. */
    double point = random * total;
    int lastAllowed = 0;
    for (int i = 0; i < LETTER_COUNT; i++) {
        if (weights[i] <= 0.0) continue; /* skip the excluded letter */
        if (point < weights[i]) return i;
        point -= weights[i];
        lastAllowed = i;
    }
    /* Only reached if rounding pushed the point just past the end. */
    return lastAllowed;
}

double pick_entropy(const LetterStats stats[LETTER_COUNT], double focus,
                    int exclude) {
    double weights[LETTER_COUNT];
    double total = fill_weights(stats, focus, exclude, weights);

    /* Shannon entropy: H = -(sum of p * log2(p)), where p is each letter's
     * chance of being picked. It measures how surprising the pick is. */
    double bits = 0.0;
    for (int i = 0; i < LETTER_COUNT; i++) {
        if (weights[i] <= 0.0) continue;
        double chance = weights[i] / total;
        bits -= chance * log2(chance);
    }
    return bits;
}

/* ------------------------------------------------------------------------ */
/* The speed scale                                                           */
/* ------------------------------------------------------------------------ */

double gap_seconds(int level, double random) {
    int levelsBelow = NO_GAP_LEVEL - level;
    if (levelsBelow <= 0) {
        return 0.0; /* level 8 and up: the next letter comes straight away */
    }
    /* 0.7 + 0.6 * random is somewhere between 0.7 and 1.3, so the pause is
     * never quite the same twice and you can't press to a rhythm. */
    return levelsBelow * GAP_PER_LEVEL * (0.7 + 0.6 * random);
}

/* The flash time the formula asks for, before any limits are applied.
 * Each level up multiplies it by 0.75 (25% shorter):
 *   level 8 = 0.12 s, level 9 = 0.09 s, level 10 = 0.0675 s ...
 *   level 15 = 0.016 s, just under one refresh of a 60 Hz screen. */
static double ideal_flash(int level) {
    return FLASH_AT_NO_GAP * pow(FLASH_SHRINK, level - NO_GAP_LEVEL);
}

double flash_seconds(int level, double refresh) {
    double flash = ideal_flash(level);
    if (flash > FLASH_LONGEST) flash = FLASH_LONGEST;
    if (flash < refresh) flash = refresh; /* a monitor can't show less */
    return flash;
}

int max_level(double refresh) {
    /* Walk up the scale until the flash is as short as one refresh.
     * Past that point the monitor couldn't show any difference. */
    int level = NO_GAP_LEVEL;
    while (level < LEVEL_LIMIT && ideal_flash(level) > refresh) {
        level++;
    }
    return level;
}

int competitive_level(double secondsSurvived, int topLevel) {
    int level = COMPETITIVE_START_LEVEL;
    double wait = COMPETITIVE_FIRST_WAIT; /* time spent on this level */
    double nextSpeedUp = wait;            /* when the next level starts */

    while (level < topLevel && secondsSurvived >= nextSpeedUp) {
        level++;
        wait *= COMPETITIVE_WAIT_GROWTH; /* each level lasts 1.4x longer */
        nextSpeedUp += round(wait);      /* 5, then 7, 10, 14, 19, 27... */
    }
    return level;
}

/* ------------------------------------------------------------------------ */
/* End-of-round statistics                                                   */
/* ------------------------------------------------------------------------ */

RoundSummary summarize(const Split *splits, int count) {
    /* Start with everything at zero. If the round has no letters this is
     * what gets returned, so nothing is ever divided by zero. */
    RoundSummary r = {0, 0, 0.0, 0.0, 0.0, 0.0, '-', '-', 0.0, 0.0};
    if (count <= 0) {
        return r;
    }

    r.count = count;
    r.best = r.worst = splits[0].time;
    r.bestLetter = r.worstLetter = splits[0].letter;

    for (int i = 0; i < count; i++) {
        const Split *s = &splits[i];
        r.total += s->time;
        r.bits += s->bits;
        if (s->misses == 0) r.cleanHits++;
        if (s->time < r.best) {
            r.best = s->time;
            r.bestLetter = s->letter;
        }
        if (s->time > r.worst) {
            r.worst = s->time;
            r.worstLetter = s->letter;
        }
    }

    r.average = r.total / count;
    r.throughput = (r.total > 0.0) ? r.bits / r.total : 0.0;
    return r;
}

int round_trouble(const Split *splits, int count, char *outLetters,
                  int *outMisses, int max) {
    /* Count, for each letter, how many times it needed a second press. */
    int missed[LETTER_COUNT] = {0};
    for (int i = 0; i < count; i++) {
        if (splits[i].misses > 0) {
            missed[splits[i].letter - 'A']++; /* 'A' - 'A' = 0, 'B' - 'A' = 1... */
        }
    }

    /* Repeatedly take the worst letter left, until we have "max" of them
     * or run out of letters with misses. */
    int written = 0;
    while (written < max) {
        int worst = -1;
        for (int i = 0; i < LETTER_COUNT; i++) {
            if (missed[i] > 0 && (worst < 0 || missed[i] > missed[worst])) {
                worst = i;
            }
        }
        if (worst < 0) break; /* nothing left */

        outLetters[written] = (char)('A' + worst);
        outMisses[written] = missed[worst];
        missed[worst] = 0; /* so it isn't chosen again */
        written++;
    }
    return written;
}
