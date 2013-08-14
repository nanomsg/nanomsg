#!/bin/sh
# Copyright (c) 2013 Luca Barbato
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom
# the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.


packageversion(){
    if [ -d .git ]; then
        VER=$(git describe --always | sed -e "s:v::")
        if [ x"$(git diff-index --name-only HEAD)" != x ]; then
            VER="${VER}-dirty"
        fi
    elif [ -f .version ]; then
        VER=$(< .version)
    else
        VER="Unknown"
    fi

    printf '%s' "$VER"
}

soversion(){
    if [ ! -f src/nn.h ]; then
        echo "version.sh: error: src/nn.h does not exist" 1>&2
        exit 1
    fi

    MAJOR=$(egrep '^#define +NN_VERSION_MAJOR +[0-9]+$' src/nn.h)
    MINOR=$(egrep '^#define +NN_VERSION_MINOR +[0-9]+$' src/nn.h)
    PATCH=$(egrep '^#define +NN_VERSION_PATCH +[0-9]+$' src/nn.h)

    if [ -z "$MAJOR" -o -z "$MINOR" -o -z "$PATCH" ]; then
        echo "version.sh: error: could not extract version from src/nn.h" 1>&2
        exit 1
    fi

    MAJOR=$(printf '%s' "$MAJOR" | awk '{ print $3 }')
    MINOR=$(printf '%s' "$MINOR" | awk '{ print $3 }')
    PATCH=$(printf '%s' "$PATCH" | awk '{ print $3 }')

    printf '%s' "$MAJOR$1$MINOR$1$PATCH"
}


case $1 in
    -v)
        soversion ":"
    ;;
    -p)
        soversion "."
    ;;
    *)
        packageversion
    ;;
esac
