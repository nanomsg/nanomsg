// Description: Header file we can use to include the proper stdint.h since stdint.h 
//              doesn't exist in VS2008 and it is defined in current version of g++.

#ifndef STDINT_INC_H
#define STDINT_INC_H

#ifndef _MSC_VER
// Non-Microsoft compilers. Currently assume stdint.h exists.
#include <stdint.h>

#else

#if _MSC_VER <= 1500
// Only needed for visual studio 2008 and below.
// This file was downloaded from the web at http://code.google.com/p/msinttypes/
#include "stdint_ms.h"
#else
// Compilers VS2010 and above have stdint.h.
#include <stdint.h>
#endif

#endif
#endif

