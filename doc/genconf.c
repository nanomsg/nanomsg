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

#include "../src/sp.h"
#include "../src/utils/err.c"

#include <stdio.h>

/*  This program generates asciidoc.conf file, filling in actual nanomsg
    version numbers. */

int main ()
{
    int rc;
    size_t sz;
    FILE *f;

    const char *conf =
        "[paradef-default]\n"
        "literal-style=template=\"literalparagraph\"\n"
        "\n"
        "[macros]\n"
        "(?su)[\\\\]?(?P<name>linknanomsg):(?P<target>\\S*?)\\[(?P<attrlist>.*?)\\]=\n"
        "\n"
        "ifdef::backend-docbook[]\n"
        "[linknanomsg-inlinemacro]\n"
        "{0%{target}}\n"
        "{0#<citerefentry>}\n"
        "{0#<refentrytitle>{target}</refentrytitle><manvolnum>{0}</manvolnum>}\n"
        "{0#</citerefentry>}\n"
        "endif::backend-docbook[]\n"
        "\n"
        "ifdef::backend-xhtml11[]\n"
        "[linknanomsg-inlinemacro]\n"
        "<a href=\"{target}.{0}.html\">{target}{0?({0})}</a>\n"
        "endif::backend-xhtml11[]\n";

    f = fopen ("asciidoc.conf", "w");
    errno_assert (f);
    sz = fwrite (conf, 1, sizeof (conf), f);
    sp_assert (sz == sizeof (conf));
    rc = fclose (f);
    errno_assert (rc == 0);

    return 0;
}
