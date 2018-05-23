#!/bin/bash

# Copyright 2017 Garrett D'Amore <garrett@damore.org>
# Copyright 2017 Capitar IT Group BV <info@capitar.com>
#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.

if [ "${COVERAGE}" != ON ]
then
	echo "Code coverage not enabled."
	exit 0
fi

GCOV=${GCOV:-gcov}

bash <(curl -s https://codecov.io/bash) -x gcov || echo "Codecov did not collect coverage"
echo 0
