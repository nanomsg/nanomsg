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
#include <string.h>

/*  This program generates asciidoc configuration file, filling in actual
    nanomsg version numbers. */

#define STR_EXPAND(x) #x
#define STR(x) STR_EXPAND(x)

int main ()
{
    int rc;
    size_t sz1, sz2;
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
        "endif::backend-xhtml11[]\n"
        "\n"
        "ifdef::doctype-manpage[]\n"
        "ifdef::backend-docbook[]\n"
        "[header]\n"
        "template::[header-declarations]\n"
        "<refentry>\n"
        "<refmeta>\n"
        "<refentrytitle>{mantitle}</refentrytitle>\n"
        "<manvolnum>{manvolnum}</manvolnum>\n"
        "<refmiscinfo class=\"source\">nanomsg</refmiscinfo>\n"
        "<refmiscinfo class=\"version\">" STR(SP_VERSION_MAJOR) "." STR(SP_VERSION_MINOR) "." STR(SP_VERSION_PATCH) "</refmiscinfo>\n"
        "<refmiscinfo class=\"manual\">nanomsg manual</refmiscinfo>\n"
        "</refmeta>\n"
        "<refnamediv>\n"
        "  <refname>{manname}</refname>\n"
        "  <refpurpose>{manpurpose}</refpurpose>\n"
        "</refnamediv>\n"
        "endif::backend-docbook[]\n"
        "endif::doctype-manpage[]\n"
        "\n"
        "ifdef::backend-xhtml11[]\n"
        "[footer]\n"
        "</div>\n"
        "{disable-javascript%<div id=\"footnotes\"><hr /></div>}\n"
        "<div id=\"footer\">\n"
        "<div id=\"footer-text\">\n"
        "nanomsg " STR(SP_VERSION_MAJOR) "." STR(SP_VERSION_MINOR) "." STR(SP_VERSION_PATCH) "<br />\n"
        "Last updated {docdate} {doctime}\n"
        "</div>\n"
        "</div>\n"
        "</body>\n"
        "</html>\n"
        "endif::backend-xhtml11[]\n";

    f = fopen ("asciidoc.conf", "w");
    errno_assert (f);
    sz1 = strlen (conf);
    sz2 = fwrite (conf, 1, sz1, f);
    sp_assert (sz1 == sz2);
    rc = fclose (f);
    errno_assert (rc == 0);

    return 0;
}
