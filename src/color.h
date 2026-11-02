/*
 * Copyright (c) 2026 Tyler Anderson <achaean@sinelabs.ca>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Terminal colour escapes, if it hasn't already been included.
 *
 * Because "we can Do this with " "C-strings we can simply drop in " tfg_red 
 * "the terminal escape for red, then " tc_none "clear the color".
 * 
 * eg. printf("There was a problem with the " tfg_red "user" tc_none ". they "
 *            "will need to be replaced.\n");
 *
 * It's ruthlesly simple. These are ANSI terminal escape codes hence the t in 
 * the prefix "terminal-color". The spec defines both foreround, and background
 * colours. So terminal-foreground_<color>, and terminal-background_<color> 
 * lower case. because pretty.
 *
 * NOTE:  regex for example escape sequence: \x1b\[[0-9;]*m
 *
 * 
 * print("\033[39m\\033[49m                 - Reset color")
 * print("\\033[2K                          - Clear Line")
 * print("\\033[<L>;<C>H or \\033[<L>;<C>f  - Cursor to line L and column C.")
 * print("\\033[<N>A                        - Cursor up N lines")
 * print("\\033[<N>B                        - Cursor down N lines")
 * print("\\033[<N>C                        - Cursor forward N columns")
 * print("\\033[<N>D                        - Cursor backward N columns\n")
 * print("\\033[2J                          - Clear the screen, move to (0,0)")
 * print("\\033[K                           - Erase to end of line")
 * print("\\033[s                           - Save cursor position")
 * print("\\033[u                           - Restore cursor position\n")
 * print("\\033[4m                          - Underline on")
 * print("\\033[24m                         - Underline off\n")
 * print("\\033[1m                          - Bold on")
 * print("\\033[21m                         - Bold off")
*/


#ifndef _COLOR_H
#define _COLOR_H 1

// Clear color (Colour: none)
#define tc_none        "\033[m"

#define tfg_black     "\033[30m"
#define tfg_b_black   "\033[30;1m"
#define tfg_red       "\033[31m"
#define tfg_b_red     "\033[31;1m"
#define tfg_green     "\033[32m"
#define tfg_b_green   "\033[32;1m"
#define tfg_yellow    "\033[33m"
#define tfg_b_yellow  "\033[33;1m"
#define tfg_blue      "\033[34m"
#define tfg_b_blue    "\033[34;1m"
#define tfg_magenta   "\033[35m"
#define tfg_b_magenta "\033[35;1m"
#define tfg_cyan      "\033[36m"
#define tfg_b_cyan    "\033[36;1m"
#define tfg_white     "\033[37m"
#define tfg_b_white   "\033[37;1m"

#define tbg_none      "\033[m"
#define tbg_black     "\033[40m"
#define tbg_b_black   "\033[40;1m"
#define tbg_red       "\033[41m"
#define tbg_b_red     "\033[41;1m"
#define tbg_green     "\033[42m"
#define tbg_b_green   "\033[42;1m"
#define tbg_yellow    "\033[43m"
#define tbg_b_yellow  "\033[43;1m"
#define tbg_blue      "\033[44m"
#define tbg_b_blue    "\033[44;1m"
#define tbg_magenta   "\033[45m"
#define tbg_b_magenta "\033[45;1m"
#define tbg_cyan      "\033[46m"
#define tbg_b_cyan    "\033[1;46m"
#define tbg_white     "\033[47m"
#define tbg_b_white   "\033[47;1m"

#define tcm_cln       "\033[2K"
#define tcm_up        "\033[A"
#define tcm_up_start  "\033[F"
#define tcm_save      "\033[7"
#define tcm_restore   "\033[8"

#endif
