#!/bin/sh

# This shell script takes our content from _adoc and runs it through
# asciidoctor and applies front matter that Jekyll will see.  Its kind
# of annoying, since Jekyll on github-pages doesn't support asciidoctor
# properly.

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

	when=$(git log -n1 --format='%ad' '--date=format-local:%s' $f)
	if [[ -z "${when}" ]]
	then
		when=$(date +%s)
	fi
	
	echo "Processing $input -> $output"
	infrontmatter=0
	manpage=0
	frontmatter=
        case $input in
        v[0-9]*)
		vers=${input#v}
		vers=${vers%%/*}
		aargs="${aargs} -aversion-label=nanomsg -arevnumber=${vers} -dmanpage"
		manpage=1
		;;
	esac
	while read line; do
		if [[ "$line" == "---" ]]
		then
			if (( $infrontmatter != 0 ))
			then
				break
			else
				infrontmatter=1
			fi
		elif [[ "$infrontmatter" != 0 ]]
		then
			if [[ -z "$frontmater" ]]
			then
				frontmatter="$line"
			else
				frontmatter="$frontmatter\n$line"
			fi
		elif (( $manpage != 0 ))
		then
			frontmatter="version: ${vers}\nlayout: default"
			break
		else
			break
		fi

	done < $input

	if [[ -n "$indir" ]] && [[ ! -d "$outdir" ]]
	then
		mkdir -p $outdir
	fi

	if [[ -n "$frontmatter" ]]
	then
		echo "---"
		echo "$frontmatter"
		echo "---"
		env SOURCE_DATE_EPOCH=${when} asciidoctor ${aargs} -b html5 -o - -a skip-front-matter $input
	fi > $output
done
