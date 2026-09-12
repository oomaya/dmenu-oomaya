/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

static int topbar = 1;                      /* -b  option; if 0, dmenu appears at bottom     */
static int centered = 1;                    /* -c  option; centers dmenu on screen           */
static int min_width = 650;                 /* minimum width when centered                   */
static const float menu_height_ratio = 3.0f;/* ratio for vertical centering positioning      */
static int fuzzy = 1;                       /* -F  option; if 0, disables fuzzy matching     */

/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = {
	"JetBrainsMono Nerd Font:size=16",
	"monospace:size=16"
};
static const char *prompt      = NULL;      /* -p  option; prompt to the left of input field */

/* Tokyo Night Colorway (Parity with Tony Banters & ChadDWM) */
static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */
	[SchemeNorm] = { "#7dcfff", "#24283b" }, /* Tokyo Night storm bg & cyan text */
	[SchemeSel]  = { "#1a1b26", "#7aa2f7" }, /* Deep dark text on vibrant blue accent bg */
	[SchemeOut]  = { "#1a1b26", "#7dcfff" }, /* Multi-select / Output highlight */
};

/* -l option; if nonzero, dmenu uses vertical list with given number of lines */
static unsigned int lines      = 10;

/* -h option; minimum height of a menu line */
static unsigned int lineheight = 34;
static unsigned int min_lineheight = 8;

/* Size of the window border */
static unsigned int border_width = 2;

/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";

