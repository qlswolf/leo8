#ifndef LEO8_PLATFORM_H
#define LEO8_PLATFORM_H

/*
 * Microsoft Visual C++ marks some standard C functions such as strtok()
 * as "unsafe". The LEO-8 intentionally uses the portable C standard
 * library, therefore the Microsoft-specific warning is disabled here.
 *
 * This header must be included before the standard library headers.
 */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#endif
