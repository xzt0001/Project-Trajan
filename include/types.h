#ifndef TYPES_H
#define TYPES_H

/* Basic type definitions for our kernel since we use -nostdinc */

/* Unsigned types */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

/* Signed types */
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;

/* Size types */
typedef uint64_t size_t;
typedef int64_t ssize_t;
typedef uint64_t uintptr_t;
typedef int64_t intptr_t;

/* NULL pointer */
#define NULL ((void*)0)

#endif /* TYPES_H */ 