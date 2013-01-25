#
#   Copyright (c) 2013 250bpm s.r.o.
#
#   Permission is hereby granted, free of charge, to any person obtaining a copy
#   of this software and associated documentation files (the "Software"),
#   to deal in the Software without restriction, including without limitation
#   the rights to use, copy, modify, merge, publish, distribute, sublicense,
#   and/or sell copies of the Software, and to permit persons to whom
#   the Software is furnished to do so, subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be included
#   in all copies or substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
#   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
#   IN THE SOFTWARE.
#

#  Read the log file generated from the inproc_lat perf test.
lat <-read.csv ('latency.log')

# Compute the time intervals between individual measurements.
psend <- lat [,2] - lat [,1]
tsend <- lat [,3] - lat [,2]
transfer1 <- lat [,4] - lat [,3]
event <- lat [,5] - lat [,4]
transfer2 <- lat [,6] - lat [,5]
post <- lat [,7] - lat [,6]
transfer3 <- lat [,8] - lat [,7]
trecv <- lat [,9] - lat [,8]
precv <- lat [,10] - lat [,9]

#  Generate the graph.
png ('inproc_lat.png', width = 500, height = 500)
top <- max (psend, tsend, transfer1, event, transfer2, post, transfer3,
    trecv, precv)
bottom <- min (psend, tsend, transfer1, event, transfer2, post, transfer2,
    trecv, precv)
plot (c (), ylab = 'CPU ticks', ylim = c (bottom, top),
    xlab = 'percentiles', xlim = c (1,100))
points (quantile (psend, probs = seq (0, 1, .01)), pch = '+', col = 'red')
points (quantile (tsend, probs = seq (0, 1, .01)), pch = '+', col = 'blue')
points (quantile (transfer1, probs = seq (0, 1, .01)), pch = '+', col = 'black')
points (quantile (event, probs = seq (0, 1, .01)), pch = '+', col = 'brown')
points (quantile (transfer2, probs = seq (0, 1, .01)), pch = '+', col = 'grey')
points (quantile (post, probs = seq (0, 1, .01)), pch = '+', col = 'yellow')
points (quantile (transfer3, probs = seq (0, 1, .01)), pch = '+', col = 'magenta')
points (quantile (trecv, probs = seq (0, 1, .01)), pch = '+', col = 'green')
points (quantile (precv, probs = seq (0, 1, .01)), pch = '+', col = 'orange')
grid ()
legend (0, top, legend = c (
    paste ("protocol send (avg=", round (mean (psend)), ")"),
    paste ("transport send (avg=", round (mean (tsend)), ")"),
    paste ("transfer1 (avg=", round (mean (transfer1)), ")"),
    paste ("event (avg=", round (mean (event)), ")"),
    paste ("transfer2 (avg=", round (mean (transfer2)), ")"),
    paste ("post (avg=", round (mean (post)), ")"),
    paste ("transfer3 (avg=", round (mean (transfer3)), ")"),
    paste ("transport recv (avg=", round (mean (trecv)), ")"),
    paste ("protocol recv (avg=", round (mean (precv)), ")")),
    pch= '+', col = c ('red', 'blue', 'black', 'brown', 'grey', 'yellow',
        'magenta', 'green', 'orange'))
dev.off ()

