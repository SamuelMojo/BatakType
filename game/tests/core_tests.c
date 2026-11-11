/* core_tests.c - checks that the rules in core.c do what they should.
 *
 * Each test calls a rule with known inputs and CHECKs the answer. No
 * window, no waiting: the whole file runs in a few milliseconds.
 *
 * Run with:  ctest --test-dir build      (or ./build/batak_type_tests)
 */
#include "../core.h"

#include <math.h>
#include <stdio.h>

static int gFailures = 0;

/* CHECK(something that should be true). If it's false, print where. */
#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition);        \
            gFailures++;                                                       \
        }                                                                      \
    } while (0)

/* Decimal numbers pick up tiny rounding errors, so compare "close enough". */
static int near(double a, double b) { return fabs(a - b) < 1e-9; }

/* ---- Picking the next letter ------------------------------------------- */

static void test_weight_is_neutral_without_history(void) {
    LetterStats fresh = {0, 0};
    CHECK(near(letter_weight(&fresh, 1.0), 1.0));
}

static void test_weight_is_neutral_with_focus_off(void) {
    LetterStats alwaysMissed = {10, 10};
    CHECK(near(letter_weight(&alwaysMissed, 0.0), 1.0));
}

static void test_weight_grows_with_misses(void) {
    LetterStats clean = {10, 0}, some = {10, 3}, lots = {10, 9};
    CHECK(letter_weight(&clean, 1.0) < letter_weight(&some, 1.0));
    CHECK(letter_weight(&some, 1.0) < letter_weight(&lots, 1.0));
}

static void test_pick_never_returns_the_excluded_letter(void) {
    LetterStats stats[LETTER_COUNT] = {{0, 0}};
    for (int exclude = 0; exclude < LETTER_COUNT; exclude++) {
        for (int k = 0; k < 1000; k++) {
            CHECK(pick_letter(stats, 1.0, exclude, k / 1000.0) != exclude);
        }
    }
}

static void test_pick_at_the_ends_of_the_random_range(void) {
    LetterStats stats[LETTER_COUNT] = {{0, 0}};
    CHECK(pick_letter(stats, 1.0, -1, 0.0) == 0);                    /* A */
    CHECK(pick_letter(stats, 1.0, -1, 0.9999999) == LETTER_COUNT - 1); /* Z */
    CHECK(pick_letter(stats, 1.0, -1, 1.0) == LETTER_COUNT - 1);
    CHECK(pick_letter(stats, 1.0, LETTER_COUNT - 1, 1.0) == LETTER_COUNT - 2);
}

static void test_pick_favours_a_troublesome_letter(void) {
    LetterStats stats[LETTER_COUNT] = {{0, 0}};
    for (int i = 0; i < LETTER_COUNT; i++) stats[i].attempts = 10;
    stats['Q' - 'A'].firstTryMisses = 10;

    int timesQ = 0;
    const int samples = 10000;
    for (int k = 0; k < samples; k++) {
        if (pick_letter(stats, 1.0, -1, (double)k / samples) == 'Q' - 'A') timesQ++;
    }
    CHECK(timesQ > 1500); /* pure chance would be about 385 */
}

static void test_entropy_with_equal_odds(void) {
    LetterStats stats[LETTER_COUNT] = {{0, 0}};
    CHECK(near(pick_entropy(stats, 0.0, -1), log2(26.0)));
    CHECK(near(pick_entropy(stats, 0.6, 7), log2(25.0))); /* one excluded */
}

static void test_entropy_drops_when_focused(void) {
    LetterStats stats[LETTER_COUNT] = {{0, 0}};
    for (int i = 0; i < LETTER_COUNT; i++) stats[i].attempts = 10;
    stats['Q' - 'A'].firstTryMisses = 10;
    CHECK(pick_entropy(stats, 1.0, -1) < pick_entropy(stats, 0.0, -1));
    CHECK(pick_entropy(stats, 1.0, -1) > 0.0);
}

/* ---- The speed scale ---------------------------------------------------- */

static void test_gap(void) {
    CHECK(near(gap_seconds(8, 0.5), 0.0));
    CHECK(near(gap_seconds(12, 0.5), 0.0));
    CHECK(near(gap_seconds(5, 0.5), 0.45));        /* 3 levels x 0.15 s */
    CHECK(near(gap_seconds(5, 0.0), 0.45 * 0.7));  /* shortest */
    CHECK(gap_seconds(5, 0.999) < 0.45 * 1.3);     /* longest */
}

static void test_flash(void) {
    double hz60 = 1.0 / 60.0;
    CHECK(near(flash_seconds(8, hz60), 0.12));
    CHECK(near(flash_seconds(9, hz60), 0.09));          /* 1 level up = 25% less */
    CHECK(near(flash_seconds(7, hz60), 0.16));          /* 1 level down = a third more */
    CHECK(flash_seconds(1, hz60) < 1.0 + 1e-9);         /* never over 1 second */
    CHECK(flash_seconds(14, hz60) > hz60);              /* 14 is still above one refresh */
    CHECK(near(flash_seconds(15, hz60), hz60));         /* 15 is the limit at 60 Hz */
    CHECK(near(flash_seconds(20, hz60), hz60));
}

static void test_max_level_depends_on_the_monitor(void) {
    CHECK(max_level(1.0 / 60.0) == 15);
    CHECK(max_level(1.0 / 120.0) == 18);
    CHECK(max_level(1.0 / 144.0) == 18);
    CHECK(max_level(1.0 / 240.0) == 20);
}

static void test_competitive_schedule(void) {
    CHECK(competitive_level(0.0, 14) == 5);
    CHECK(competitive_level(4.9, 14) == 5);
    CHECK(competitive_level(5.0, 14) == 6);   /* +5 s */
    CHECK(competitive_level(12.0, 14) == 7);  /* +7 s */
    CHECK(competitive_level(22.0, 14) == 8);  /* +10 s */
    CHECK(competitive_level(36.0, 14) == 9);  /* +14 s */
    CHECK(competitive_level(55.0, 14) == 10); /* +19 s */
    CHECK(competitive_level(9999.0, 14) == 14); /* stops at the top level */
    CHECK(competitive_level(9999.0, 16) == 16);
}

/* ---- End-of-round statistics ------------------------------------------- */

static void test_summary_of_an_empty_round(void) {
    RoundSummary r = summarize(0, 0);
    CHECK(r.count == 0);
    CHECK(near(r.total, 0.0));
    CHECK(near(r.average, 0.0)); /* and not "nan" from dividing by zero */
    CHECK(near(r.throughput, 0.0));
}

static void test_summary_of_known_values(void) {
    Split s[3] = {{'A', 0.300, 0, 4.0}, {'B', 0.500, 2, 4.0}, {'C', 0.250, 0, 2.5}};
    RoundSummary r = summarize(s, 3);
    CHECK(r.count == 3);
    CHECK(r.cleanHits == 2);
    CHECK(near(r.total, 1.050));
    CHECK(near(r.average, 0.350));
    CHECK(near(r.best, 0.250) && r.bestLetter == 'C');
    CHECK(near(r.worst, 0.500) && r.worstLetter == 'B');
    CHECK(near(r.bits, 10.5));
    CHECK(near(r.throughput, 10.0)); /* 10.5 bits in 1.05 s */
}

static void test_trouble_is_sorted_worst_first(void) {
    Split s[5] = {{'A', 0.3, 1, 0.0}, {'B', 0.3, 0, 0.0}, {'Z', 0.3, 1, 0.0},
                  {'Z', 0.3, 3, 0.0}, {'C', 0.3, 0, 0.0}};
    char letters[5];
    int misses[5];
    int n = round_trouble(s, 5, letters, misses, 5);
    CHECK(n == 2);
    CHECK(letters[0] == 'Z' && misses[0] == 2);
    CHECK(letters[1] == 'A' && misses[1] == 1);
}

int main(void) {
    test_weight_is_neutral_without_history();
    test_weight_is_neutral_with_focus_off();
    test_weight_grows_with_misses();
    test_pick_never_returns_the_excluded_letter();
    test_pick_at_the_ends_of_the_random_range();
    test_pick_favours_a_troublesome_letter();
    test_entropy_with_equal_odds();
    test_entropy_drops_when_focused();
    test_gap();
    test_flash();
    test_max_level_depends_on_the_monitor();
    test_competitive_schedule();
    test_summary_of_an_empty_round();
    test_summary_of_known_values();
    test_trouble_is_sorted_worst_first();

    if (gFailures == 0) {
        printf("All core tests passed.\n");
        return 0;
    }
    printf("%d check(s) failed.\n", gFailures);
    return 1;
}
