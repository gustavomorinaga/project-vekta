/* KZ_SKIP_INTRO (intro = off in kz1hd_pc.ini): which opens of Movies/Intro/Intro.avi play blank.
 *
 * The game opens that file from two places: at start-up, after the logos (MenuScript.Intro,
 * gGameSystem:PlayMovieNormalAspect), and from Extras > Game Intro (MenuScript.FrontEnd,
 * gMenu:PlayMovie). The option is for the first one only, so only the first open of the file
 * is blank; Extras plays the whole movie. A pure function for tools/tests/test_intro_skip.c. */
#ifndef KZ_INTRO_SKIP_H
#define KZ_INTRO_SKIP_H

/* skip: KZ_SKIP_INTRO is set; is_intro: this open is Intro.avi; opened: kept by the caller,
 * set once the file has been opened. Returns 1 when this open is to be blank. */
static inline int kz_intro_blank(int skip, int is_intro, int* opened)
{
    if (!is_intro) return 0;
    const int first = !*opened;
    *opened = 1;
    return skip && first;
}

#endif
