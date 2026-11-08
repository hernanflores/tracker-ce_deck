/*
 * cr_section_macros.h
 *
 * Copyright 2010-2012 Code Red Technologies Ltd
 * Copyright 2013-2014, 2017-2018, 2020-2022 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Set of macros to allow code/data to be placed into alternate memory banks.
 */

#ifndef CR_SECTION_MACROS_H_INCLUDED
#define CR_SECTION_MACROS_H_INCLUDED

// A macro for placing text (code), data, or bss into a named RAM section
// These will be automatically placed into the named section by the linker.
//
// RAM banks are numbered starting from 1. The actual configuration is 
// dependent on the selected MCU type.
//
// Example:
//        __SECTION(data,RAM1) char buffer[1024] ;
//
// This will place the 1024 byte buffer into the RAM1
//
#ifdef PLATFORM_HOST
#define __PT_SEC(s)
#else
#define __PT_SEC(s) __attribute__ ((section(s)))
#endif

#define __SECTION_EXT(type, bank, name) __PT_SEC("." #type ".$" #bank "." #name)
#define __SECTION(type, bank) __PT_SEC("." #type ".$" #bank)
#define __SECTION_SIMPLE(type) __PT_SEC("." #type)

#define __DATA_EXT(bank, name) __SECTION_EXT(data, bank, name)
#define __DATA(bank) __SECTION(data, bank)

#define __BSS_EXT(bank, name) __SECTION_EXT(bss, bank, name)
#define __BSS(bank) __SECTION(bss, bank)

// Macros for placing text (code), data, or bss into a section that is automatically
// placed after the vectors in the target image.
#define __AFTER_VECTORS_EXT(name) __PT_SEC(".after_vectors.$" #name)
#define __AFTER_VECTORS __PT_SEC(".after_vectors")

// Macros for causing functions to be relocated to RAM
#define __RAM_FUNC_EXT(name) __PT_SEC(".ramfunc.$" #name)
#define __RAM_FUNC __PT_SEC(".ramfunc")

// Macros to be used in preference to __RAM_FUNC to better match __DATA behaviour
#define __RAMFUNC_EXT(bank, name) __SECTION_EXT(ramfunc, bank, name)
#define __RAMFUNC(bank) __SECTION(ramfunc, bank)

// Macros for placing data or bss into a section that has the NOLOAD option set in the linker script
#define __NOINIT_DEF __SECTION_SIMPLE(noinit)
#define __NOINIT_EXT(bank, name) __SECTION_EXT(noinit, bank, name)
#define __NOINIT(bank) __SECTION(noinit, bank)

// Macros for placing text (code), or rodata into a different (flash) bank
#define __RODATA_EXT(bank,name) __SECTION_EXT(rodata, bank, name)
#define __RODATA(bank) __SECTION(rodata, bank)

#define __TEXT_EXT(bank,name) __SECTION_EXT(text, bank, name)
#define __TEXT(bank) __SECTION(text, bank)

#endif /* CR_SECTION_MACROS_H_INCLUDED */
