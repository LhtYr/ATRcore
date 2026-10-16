#ifndef ATRCORE_BUILD_INFO_H
#define ATRCORE_BUILD_INFO_H

/* Shared build identity also exercises compiler-generated header dependencies. */
#define ATRCORE_BUILD_STAGE "S05"

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ != 201112L
#error "ATRcore requires ISO C11 (-std=c11)."
#endif

#endif
