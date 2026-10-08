/* Copyright (c) 2026 Dalsin Limited. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Test.module's table (tests/modules/test_module.c). */
#ifndef TEST_MODULE_H
#define TEST_MODULE_H

#include <opengpu/module.h>

#define TEST_MODULE_VERSION 1

struct TestModuleTable {
    struct OGPUModuleTable head;
    void (*close)(void);
    long (*add)(long a, long b);
    long (*bump)(void);
    int (*ctor_ran)(void);
    const char *(*getenv)(const char *name);
    int (*say)(const char *what);
    struct Library *(*opengpu)(void);
};

#endif
