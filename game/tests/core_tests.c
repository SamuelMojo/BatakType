/* core_tests.c - tests for the BatakType rules in core.c.
 * Run with: ctest --test-dir build   (or ./build/batak_type_tests) */
#include "../core.h"

#include <math.h>
#include <stdio.h>

static int gFailures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
            gFailures++;                                                       \
        }                                                                      \
    } while (0)

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

static void test_weight_neutral_without_history(void) {
    LetterStats s = {0, 0, 0, 0.0};
    CHECK(near(letter_weight(&s, 1.0), 1.0));
}

static void test_weight_zero_strength_is_neutral(void) {
    LetterStats s = {10, 10, 10, 5.0};
    CHECK(near(letter_weight(&s, 0.0), 1.0));
}

static void test_weight_grows_with_misses(void) {
    LetterStats clean = {10, 0, 10, 5.0};
    LetterStats some = {10, 3, 10, 5.0};
    LetterStats lots = {10, 9, 10, 5.0};
    CHECK(letter_weight(&clean, 1.0) < letter_weight(&some, 1.0));
    CHECK(letter_weight(&some, 1.0) < letter_weight(&lots, 1.0));
}

static void test_pick_never_returns_excluded(void) {
    LetterStats stats[LETTER_COUNT] = {{0, 0, 0, 0.0}};
    for (int ex = 0; ex < LETTER_COUNT; ex++) {
        for (int k = 0; k < 1000; k++) {
            double r = (double)k / 1000.0;
            CHECK(pick_letter(stats, 1.0, ex, r) != ex);
        }
    }
}

static void test_pick_edges_of_r(void) {
    LetterStats stats[LETTER_COUNT] = {{0, 0, 0, 0.0}};
    CHECK(pick_letter(stats, 1.0, -1, 0.0) == 0);
    CHECK(pick_letter(stats, 1.0, -1, 0.9999999) == LETTER_COUNT - 1);
    CHECK(pick_letter(stats, 1.0, -1, 1.0) == LETTER_COUNT - 1);
    CHECK(pick_letter(stats, 1.0, LETTER_COUNT - 1, 1.0) == LETTER_COUNT - 2);
}

static void test_pick_favours_troublesome_letter(void) {
    LetterStats stats[LETTER_COUNT] = {{0, 0, 0, 0.0}};
    for (int i = 0; i < LETTER_COUNT; i++) {
        stats[i].attempts = 10;
    }
    stats['Q' - 'A'].firstTryMisses = 10;

    int qCount = 0;
    const int samples = 10000;
    for (int k = 0; k < samples; k++) {
        if (pick_letter(stats, 1.0, -1, (double)k / samples) == 'Q' - 'A') {
            qCount++;
        }
    }
    /* Uniform would be ~385 / 10000. Q should be far above that. */
    CHECK(qCount > 1500);
}

static void test_summary_empty_round(void) {
    RoundSummary r = summarize(0, 0);
    CHECK(r.count == 0);
    CHECK(near(r.total, 0.0));
    CHECK(near(r.average, 0.0));
    CHECK(!isnan(r.average));
}

static void test_summary_known_values(void) {
    Split s[3] = {{'A', 0.300, 0}, {'B', 0.500, 2}, {'C', 0.250, 0}};
    RoundSummary r = summarize(s, 3);
    CHECK(r.count == 3);
    CHECK(r.cleanHits == 2);
    CHECK(near(r.total, 1.050));
    CHECK(near(r.average, 0.350));
    CHECK(near(r.best, 0.250) && r.bestLetter == 'C');
    CHECK(near(r.worst, 0.500) && r.worstLetter == 'B');
}

static void test_trouble_sorted_worst_first(void) {
    Split s[5] = {{'A', 0.3, 1}, {'B', 0.3, 0}, {'Z', 0.3, 1},
                  {'Z', 0.3, 3}, {'C', 0.3, 0}};
    char letters[5];
    int misses[5];
    int n = round_trouble(s, 5, letters, misses, 5);
    CHECK(n == 2);
    CHECK(letters[0] == 'Z' && misses[0] == 2);
    CHECK(letters[1] == 'A' && misses[1] == 1);
}

int main(void) {
    test_weight_neutral_without_history();
    test_weight_zero_strength_is_neutral();
    test_weight_grows_with_misses();
    test_pick_never_returns_excluded();
    test_pick_edges_of_r();
    test_pick_favours_troublesome_letter();
    test_summary_empty_round();
    test_summary_known_values();
    test_trouble_sorted_worst_first();

    if (gFailures == 0) {
        printf("All core tests passed.\n");
        return 0;
    }
    printf("%d check(s) failed.\n", gFailures);
    return 1;
}
