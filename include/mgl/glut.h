/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The part of GLUT minigl.library offers, with GLUT's own numbers. The calls
 * are in mgl/minigl_calls.h. */
#ifndef MGL_GLUT_H
#define MGL_GLUT_H

#include <mgl/gl.h>

/* glutInitDisplayMode */
#define GLUT_RGB                        0x0000
#define GLUT_RGBA                       0x0000
#define GLUT_INDEX                      0x0001
#define GLUT_SINGLE                     0x0000
#define GLUT_DOUBLE                     0x0002
#define GLUT_ACCUM                      0x0004
#define GLUT_ALPHA                      0x0008
#define GLUT_DEPTH                      0x0010
#define GLUT_STENCIL                    0x0020
#define GLUT_MULTISAMPLE                0x0080
#define GLUT_STEREO                     0x0100
#define GLUT_LUMINANCE                  0x0200

/* glutGameModeGet */
#define GLUT_GAME_MODE_ACTIVE           0x0000
#define GLUT_GAME_MODE_POSSIBLE         0x0001
#define GLUT_GAME_MODE_WIDTH            0x0002
#define GLUT_GAME_MODE_HEIGHT           0x0003
#define GLUT_GAME_MODE_PIXEL_DEPTH      0x0004
#define GLUT_GAME_MODE_REFRESH_RATE     0x0005
#define GLUT_GAME_MODE_DISPLAY_CHANGED  0x0006

/* glutGet */
#define GLUT_WINDOW_X                   100
#define GLUT_WINDOW_Y                   101
#define GLUT_WINDOW_WIDTH               102
#define GLUT_WINDOW_HEIGHT              103
#define GLUT_WINDOW_BUFFER_SIZE         104
#define GLUT_WINDOW_STENCIL_SIZE        105
#define GLUT_WINDOW_DEPTH_SIZE          106
#define GLUT_WINDOW_RED_SIZE            107
#define GLUT_WINDOW_GREEN_SIZE          108
#define GLUT_WINDOW_BLUE_SIZE           109
#define GLUT_WINDOW_ALPHA_SIZE          110
#define GLUT_WINDOW_ACCUM_RED_SIZE      111
#define GLUT_WINDOW_ACCUM_GREEN_SIZE    112
#define GLUT_WINDOW_ACCUM_BLUE_SIZE     113
#define GLUT_WINDOW_ACCUM_ALPHA_SIZE    114
#define GLUT_WINDOW_DOUBLEBUFFER        115
#define GLUT_WINDOW_RGBA                116
#define GLUT_WINDOW_PARENT              117
#define GLUT_WINDOW_NUM_CHILDREN        118
#define GLUT_WINDOW_COLORMAP_SIZE       119
#define GLUT_WINDOW_NUM_SAMPLES         120
#define GLUT_WINDOW_STEREO              121
#define GLUT_WINDOW_CURSOR              122
#define GLUT_WINDOW_FORMAT_ID           123
#define GLUT_SCREEN_WIDTH               200
#define GLUT_SCREEN_HEIGHT              201
#define GLUT_SCREEN_WIDTH_MM            202
#define GLUT_SCREEN_HEIGHT_MM           203
#define GLUT_MENU_NUM_ITEMS             300
#define GLUT_DISPLAY_MODE_POSSIBLE      400
#define GLUT_INIT_WINDOW_X              500
#define GLUT_INIT_WINDOW_Y              501
#define GLUT_INIT_WINDOW_WIDTH          502
#define GLUT_INIT_WINDOW_HEIGHT         503
#define GLUT_INIT_DISPLAY_MODE          504
#define GLUT_ELAPSED_TIME               700
#define GLUT_CURSOR_INHERIT             100

#endif
