#!/bin/sh

# This shell script takes our content from _adoc and runs it through
# asciidoctor and applies front matter that Jekyll will see.  Its kind
# of annoying, since Jekyll on github-pages doesn't support asciidoctor
# properly.

cd $(basename $0)
for f in $(find . -name '*.adoc'); do

	input=${f#./}
	indir=$(dirname $f)
	indir=${indir#./}
	output=../${input%.adoc}.html
        outdir=../${indir}

	if [ $output -nt $input ]
	then
		echo "$output up to date"
		continue
	fi

	echo "Processing $input -> $output"
	infrontmatter=0
	manpage=0
	frontmatter=
        case $input in
        v[0-9]*)
		vers=${input#v}
		vers=${vers%%/*}
		aargs="-aversion-label=nanomsg -arevnumber=${vers} -dmanpage"
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
		asciidoctor ${aargs} -b html5 -o - -a skip-front-matter $input 
	fi > $output
done
