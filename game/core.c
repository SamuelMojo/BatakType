/* core.c - BatakType game rules (see core.h). */
#include "core.h"

double letter_weight(const LetterStats *s, double strength) {
    if (strength <= 0.0 || s->attempts <= 0) {
        return 1.0;
    }
    /* Smoothed first-try miss rate. The +2 means a single slip on a letter
     * you've seen once nudges it up a little; a letter you miss again and
     * again climbs much higher. Getting it right pulls the rate back down. */
    double rate = (double)s->firstTryMisses / ((double)s->attempts + 2.0);
    return 1.0 + strength * ADAPT_GAIN * rate;
}

int pick_letter(const LetterStats stats[LETTER_COUNT], double strength,
                int exclude, double r) {
    double w[LETTER_COUNT];
    double total = 0.0;
    for (int i = 0; i < LETTER_COUNT; i++) {
        w[i] = (i == exclude) ? 0.0 : letter_weight(&stats[i], strength);
        total += w[i];
    }

    if (r < 0.0) r = 0.0;
    if (r >= 1.0) r = 0.999999999;

    double target = r * total;
    for (int i = 0; i < LETTER_COUNT; i++) {
        if (w[i] <= 0.0) continue;
        if (target < w[i]) return i;
        target -= w[i];
    }
    /* Floating point rounding can walk off the end: take the last valid. */
    for (int i = LETTER_COUNT - 1; i >= 0; i--) {
        if (w[i] > 0.0) return i;
    }
    return 0;
}

RoundSummary summarize(const Split *splits, int count) {
    RoundSummary r = {0, 0, 0.0, 0.0, 0.0, 0.0, '-', '-'};
    if (splits == 0 || count <= 0) {
        return r;
    }
    r.count = count;
    r.best = splits[0].time;
    r.worst = splits[0].time;
    r.bestLetter = splits[0].letter;
    r.worstLetter = splits[0].letter;
    for (int i = 0; i < count; i++) {
        const Split *s = &splits[i];
        r.total += s->time;
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
    r.average = r.total / (double)count;
    return r;
}

int round_trouble(const Split *splits, int count, char *outLetter,
                  int *outMisses, int max) {
    int missed[LETTER_COUNT] = {0};
    for (int i = 0; i < count; i++) {
        int idx = splits[i].letter - 'A';
        if (idx >= 0 && idx < LETTER_COUNT && splits[i].misses > 0) {
            missed[idx]++;
        }
    }

    int n = 0;
    while (n < max) {
        int bestIdx = -1;
        for (int i = 0; i < LETTER_COUNT; i++) {
            if (missed[i] > 0 && (bestIdx < 0 || missed[i] > missed[bestIdx])) {
                bestIdx = i;
            }
        }
        if (bestIdx < 0) break;
        outLetter[n] = (char)('A' + bestIdx);
        outMisses[n] = missed[bestIdx];
        missed[bestIdx] = 0;
        n++;
    }
    return n;
}
