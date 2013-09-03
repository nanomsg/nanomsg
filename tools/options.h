/*
    Copyright (c) 2013 Insollo Entertainment, LLC.  All rights reserved.

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

#ifndef NC_OPTIONS_HEADER
#define NC_OPTIONS_HEADER

enum nc_option_type {
    NC_OPT_HELP,
    NC_OPT_INT,
    NC_OPT_INCREMENT,
    NC_OPT_DECREMENT,
    NC_OPT_ENUM,
    NC_OPT_SET_ENUM,
    NC_OPT_STRING,
    NC_OPT_BLOB,
    NC_OPT_FLOAT,
    NC_OPT_LIST_APPEND,
    NC_OPT_LIST_APPEND_FMT,
    NC_OPT_READ_FILE
};

struct nc_option {
    /*  Option names  */
    char *longname;
    char shortname;
    char *arg0name;

    /*  Parsing specification  */
    enum nc_option_type type;
    int offset;  /*  offsetof() where to store the value  */
    const void *pointer;  /*  type specific pointer  */

    /*  Conflict mask for options  */
    unsigned long mask_set;
    unsigned long conflicts_mask;
    unsigned long requires_mask;

    /*  Group and description for --help  */
    char *group;
    char *metavar;
    char *description;
};

struct nc_commandline {
    char *short_description;
    char *long_description;
    struct nc_option *options;
    int required_options;
};

struct nc_enum_item {
    char *name;
    int value;
};

struct nc_string_list {
    char **items;
    int num;
};

struct nc_blob {
    char *data;
    int length;
};


void nc_parse_options (struct nc_commandline *cline,
                      void *target, int argc, char **argv);


#endif  /* NC_OPTIONS_HEADER */
