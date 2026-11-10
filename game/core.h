/* core.h - BatakType game rules.
 *
 * Pure C, no raylib, no clock, no global RNG: time and randomness are passed
 * in, so every rule here can be tested without opening a window.
 */
#ifndef BATAK_CORE_H
#define BATAK_CORE_H

#define LETTER_COUNT 26

/* How hard the picker leans on troublesome letters at 100% strength.
 * A letter you miss on every first try ends up roughly this many times
 * more likely to appear than a letter you never miss. */
#define ADAPT_GAIN 8.0

/* Lifetime history for one letter (kept across rounds in a session). */
typedef struct {
    int attempts;       /* times the letter was completed */
    int firstTryMisses; /* times you pressed a wrong key before getting it */
    int hits;
    double totalTime;
} LetterStats;

/* One completed letter in a round. */
typedef struct {
    char letter;  /* 'A'..'Z' */
    double time;  /* reaction time in seconds, penalties included */
    int misses;   /* wrong keys pressed before the right one */
} Split;

typedef struct {
    int count;
    int cleanHits; /* splits with zero misses */
    double total;
    double average;
    double best;
    double worst;
    char bestLetter;
    char worstLetter;
} RoundSummary;

/* Selection weight for a letter. 1.0 is neutral; higher means "show more".
 * strength is 0..1 (the Adaptive focus slider). */
double letter_weight(const LetterStats *s, double strength);

/* Weighted pick of the next letter index (0..25).
 * exclude: index that must not be picked (the previous letter), or -1.
 * r: a uniform random number in [0, 1). */
int pick_letter(const LetterStats stats[LETTER_COUNT], double strength,
                int exclude, double r);

/* Totals, average, best and worst for a round. Safe for count == 0. */
RoundSummary summarize(const Split *splits, int count);

/* Letters you missed on the first try this round, worst first.
 * Writes up to max entries to outLetter ('A'..'Z') and outMisses.
 * Returns how many were written. */
int round_trouble(const Split *splits, int count, char *outLetter,
                  int *outMisses, int max);

#endif
