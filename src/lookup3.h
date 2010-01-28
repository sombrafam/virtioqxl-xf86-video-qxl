#ifndef __LOOKUP3_H
#define __LOOKUP3_H

#if defined(__GNUC__) || defined(__sun)

#include <stdint.h>

#else

#ifdef QXLDD
#include <windef.h>
#include "os_dep.h"
#else
#include <stddef.h>
#include <basetsd.h>
#endif

typedef UINT32 uint32_t;
typedef UINT16 uint16_t;
typedef UINT8 uint8_t;

#endif

uint32_t hashlittle( const void *key, size_t length, uint32_t initval);

#endif
