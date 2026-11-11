/* core.h - the rules and the maths of BatakType.
 *
 * This is the "brain" of the game, and it is deliberately boring C:
 *   - it never draws anything (no raylib),
 *   - it never reads the clock,
 *   - it never makes its own random numbers.
 * Whatever it needs (a time, a random number, your monitor's refresh rate)
 * is handed to it as a function argument. That means every rule gives the
 * same answer for the same inputs, so tests/core_tests.c can check all of
 * them in a few milliseconds without opening a window.
 *
 * A ".h" file like this one is a list of what the matching ".c" file
 * offers. Other files #include it to use these functions; the code that
 * actually does the work is in core.c.
 */
#ifndef BATAK_CORE_H /* these two lines stop the file being included twice */
#define BATAK_CORE_H

/* ---- Numbers that shape the game ---------------------------------------
 * #define gives a number a name. Changing one here changes it everywhere.
 */

#define LETTER_COUNT 26 /* A to Z */

/* "Focus on missed letters": at 100% focus, a letter you miss every time
 * becomes roughly this many times more likely than one you never miss. */
#define FOCUS_GAIN 8.0

/* The speed scale ("Letter frequency"). Level 8 is the anchor point:
 * from level 8 up there is no pause between letters, and in flash mode a
 * letter stays on screen for 0.12 seconds. Each level above 8 shows the
 * letter for 25% less time (x 0.75), each level below for a third longer,
 * until the flash is a single refresh of the monitor: the shortest time a
 * screen can show anything at full brightness. That is where the scale
 * stops, so its top level depends on the monitor. */
#define MIN_LEVEL 1
#define NO_GAP_LEVEL 8
#define GAP_PER_LEVEL 0.15  /* seconds of pause added per level below 8 */
#define FLASH_AT_NO_GAP 0.12 /* flash time at level 8, in seconds */
#define FLASH_SHRINK 0.75    /* each level up multiplies the flash by this */
#define FLASH_LONGEST 1.0   /* a flash never lasts longer than this */
#define LEVEL_LIMIT 24      /* safety stop for extremely fast monitors */

/* Competitive mode starts at level 5 and speeds up as you survive. The
 * first speed-up comes after 5 seconds, and each wait after that is 1.4
 * times longer than the one before (5, 7, 10, 14, 19, 27... seconds). */
#define COMPETITIVE_START_LEVEL 5
#define COMPETITIVE_FIRST_WAIT 5.0
#define COMPETITIVE_WAIT_GROWTH 1.4

/* ---- The data the rules work with --------------------------------------
 * A "struct" groups related values under one name.
 */

/* What the game remembers about one letter, kept for the whole session. */
typedef struct {
    int attempts;       /* how many times you have completed this letter */
    int firstTryMisses; /* how many of those needed more than one press */
} LetterStats;

/* One finished letter in a round: a row in the "Times" list. */
typedef struct {
    char letter; /* 'A' to 'Z' */
    double time; /* reaction time in seconds */
    int misses;  /* wrong keys pressed before the right one */
    double bits; /* information this pick carried (see pick_entropy) */
} Split;

/* Everything the results screen shows about a round. */
typedef struct {
    int count;         /* letters completed */
    int cleanHits;     /* letters hit with no wrong presses */
    double total;      /* all reaction times added up, in seconds */
    double average;    /* total / count */
    double best;       /* fastest reaction */
    double worst;      /* slowest reaction */
    char bestLetter;
    char worstLetter;
    double bits;       /* total information processed */
    double throughput; /* bits per second: bits / total */
} RoundSummary;

/* ---- Picking the next letter -------------------------------------------
 * "focus" is the Focus on missed letters slider as a fraction: 0.0 to 1.0.
 * "exclude" is a letter (0 = A ... 25 = Z) that must not come up, used to
 * stop the same letter appearing twice in a row. Pass -1 for "none".
 */

/* How likely a letter is to be picked, relative to the others.
 * 1.0 is normal; higher means "show this one more often". */
double letter_weight(const LetterStats *letter, double focus);

/* Picks the next letter (0 = A ... 25 = Z).
 * "random" must be a random number from 0.0 up to (but not including) 1.0. */
int pick_letter(const LetterStats stats[LETTER_COUNT], double focus,
                int exclude, double random);

/* How much information, in bits, the next pick carries. Equal odds over 25
 * letters gives log2(25) = 4.64 bits; uneven odds give less. */
double pick_entropy(const LetterStats stats[LETTER_COUNT], double focus,
                    int exclude);

/* ---- The speed scale ---------------------------------------------------
 * "refresh" is how long your monitor takes to draw one frame, in seconds
 * (1/60 on a 60 Hz screen). Nothing can be shown for less time than that.
 */

/* The pause before the next letter appears, in seconds.
 * "random" (0.0 to 1.0) varies it by up to 30% either way. */
double gap_seconds(int level, double random);

/* Flash mode: how long a letter stays on screen, in seconds. */
double flash_seconds(int level, double refresh);

/* The highest level worth having on this monitor: the first one where the
 * flash is as short as a single refresh. 15 at 60 Hz, 18 at 120 Hz,
 * 20 at 240 Hz. */
int max_level(double refresh);

/* Competitive mode: the level you are on after surviving this long. */
int competitive_level(double secondsSurvived, int topLevel);

/* ---- End-of-round statistics ------------------------------------------- */

/* Adds up a round. Safe to call with no letters (count == 0). */
RoundSummary summarize(const Split *splits, int count);

/* Lists the letters you got wrong on the first try this round, worst
 * first. Fills outLetters and outMisses with up to "max" entries and
 * returns how many it wrote. */
int round_trouble(const Split *splits, int count, char *outLetters,
                  int *outMisses, int max);

#endif
