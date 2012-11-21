/*
    Copyright (c) 2012 250bpm s.r.o.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#ifndef SP_ERR_INCLUDED
#define SP_ERR_INCLUDED

#include <errno.h>
#include <stdio.h>
#include <string.h>

/*  Include SP header to define SP-specific error codes. */
#include "../sp.h"

#include "fast.h"

#if defined _MSC_VER
#define SP_NORETURN __declspec(noreturn)
#elif defined __GNUC__
#define SP_NORETURN __attribute__ ((noreturn))
#else
#define SP_NORETURN
#endif

/*  Same as system assert(). However, under Win32 assert has some deficiencies.
    Thus this macro. */
#define sp_assert(x) \
    do {\
        if (sp_slow (!(x))) {\
            fprintf (stderr, "Assertion failed: %s (%s:%d)\n", #x, \
                __FILE__, __LINE__);\
            sp_err_abort ();\
        }\
    } while (0)

/*  Checks whether memory allocation was successful. */
#define alloc_assert(x) \
    do {\
        if (sp_slow (!x)) {\
            fprintf (stderr, "Out of memory (%s:%d)\n",\
                __FILE__, __LINE__);\
            sp_err_abort ();\
        }\
    } while (0)

/*  Check the condition. If false prints out the errno. */
#define errno_assert(x) \
    do {\
        if (sp_slow (!(x))) {\
            fprintf (stderr, "%s [%d] (%s:%d)\n", sp_err_strerror (errno),\
                (int) errno, __FILE__, __LINE__);\
            sp_err_abort ();\
        }\
    } while (0)

/*  Checks whether supplied errno number is an error. */
#define errnum_assert(cond, err) \
    do {\
        if (sp_slow (!(cond))) {\
            fprintf (stderr, "%s [%d] (%s:%d)\n", sp_err_strerror (err),\
                (int) (err), __FILE__, __LINE__);\
            sp_err_abort ();\
        }\
    } while (0)

/* Checks the condition. If false prints out the GetLastError info. */
#define win_assert(x) \
    do {\
        if (sp_slow (!(x))) {\
            char errstr [256];\
            sp_win_error ((int) GetLastError (), errstr, 256);\
            fprintf (stderr, "%s [%d] (%s:%d)\n",\
                errstr, (int) GetLastError (), __FILE__, __LINE__);\
            sp_err_abort ();\
        }\
    } while (0)

/* Checks the condition. If false prints out the WSAGetLastError info. */
#define wsa_assert(x) \
    do {\
        if (sp_slow (!(x))) {\
            char errstr [256];\
            sp_win_error (WSAGetLastError (), errstr, 256);\
            fprintf (stderr, "%s [%d] (%s:%d)\n",\
                errstr, (int) WSAGetLastError (), __FILE__, __LINE__);\
            sp_err_abort ();\
        }\
    } while (0)

SP_NORETURN void sp_err_abort (void);
int sp_err_errno (void);
const char *sp_err_strerror (int errnum);

#ifdef SP_HAVE_WINDOWS
int sp_err_wsa_to_posix (int wsaerr);
void sp_win_error (int err, char *buf, size_t bufsize);
#endif

#endif

