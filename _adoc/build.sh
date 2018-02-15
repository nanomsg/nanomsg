#!/bin/sh

# This shell script takes our content from _adoc and runs it through
# asciidoctor and applies front matter that Jekyll will see.  Its kind
# of annoying, since Jekyll on github-pages doesn't support asciidoctor
# properly.

# File names starting with and underscore are skipped, as they are considered
# as included content.

dofront()
{
	read line
	if [ "${line}" != "---" ]
	then
		return
	fi
	printf "%s\n" "---"
	while read line
	do
		printf "%s\n" "${line}"
		if [ "${line}" = "---" ]
		then
			break
		fi
	done
}

cd $(dirname $0)
if [ -n "$1" ]
then
	files=$*
else
	files=$(find . -name '*.adoc')
fi
for f in $files
do
	input=${f#./}
	indir=$(dirname $f)
	indir=${indir#./}
	output=../${input%.adoc}.html
        outdir=../${indir}
	aargs=
	case "${input##*/}" in
	_*)	printf "Skipping %s\n" "$input"
		continue
		;;
	esac

	when=$(git log -n1 --format='%ad' '--date=format-local:%s' $f)
	if [[ -z "${when}" ]]
	then
		when=$(date +%s)
	fi
	
	echo "Processing $input -> $output"

	if [ -n "$indir" ] && [ ! -d "$outdir" ]
	then
		mkdir -p $outdir
	fi
	> $output

	aargs=
        case $input in
        v[0-9]*)
		vers=${input#v}
		vers=${vers%%/*}
		aargs="${aargs} -aversion-label=nanomsg -arevnumber=${vers} -dmanpage"
		# for man pages, we supply our own front matter
		printf "%s\n" "---" >> $output
		printf "version: %s\n" "${vers}" >> $output
		printf "layout: default\n" >> $output
		printf "%s\n" "---" >> $output
		;;
	*)
		# dofront >> $output < $input
		;;
	esac


	dofront < $input >> $output
	env SOURCE_DATE_EPOCH=${when} asciidoctor ${aargs} -b html5 -o - -a skip-front-matter $input >> $output
done
