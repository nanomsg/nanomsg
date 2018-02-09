
# This ruby script takes our content from _adoc and runs it through
# asciidoctor and applies front matter that Jekyll will see.  Its kind
# of annoying, since Jekyll on github-pages doesn't support asciidoctor
# properly.

require 'pathname'
require 'fileutils'

Dir.chdir("#{__dir__}")

if ARGV[0]
  adoc_files = [ARGV[0]]
else
  adoc_files = Dir.glob('**/*.adoc')
end

adoc_files.each do |adoc|
  aargs = ''
  out_dir = Pathname("../#{File.dirname(adoc)}").cleanpath
  out_file = "#{out_dir}/#{File.basename(adoc,'.adoc')}.html"
  puts "Processing #{adoc} -> #{out_file}"
  source_date_epoch = `git log -n1 --format='%ad' '--date=format-local:%s' #{adoc}`.chomp
  manpage = ! (adoc =~ /v[0-9].*/).nil?
  if manpage
    vers = adoc.split('/').first.gsub(/^v/,'')
    aargs += " -aversion-label=nanomsg -arevnumber=#{vers} -dmanpage"
  end
  content = File.read(adoc)
  matchdata = /^---\n.*\n---\n/.match(content)
  frontmatter = ''
  if matchdata
    frontmatter = matchdata[0]
  else
    if (manpage)
      frontmatter = <<EOS
---
version: #{vers}
layout: default
---
EOS
    end
  end

  # Write on filesystem
  FileUtils.mkdir_p(out_dir)
  File.open(out_file,'w') do |io|
    io.write(frontmatter)
  end
  cmd = "env SOURCE_DATE_EPOCH=#{source_date_epoch} asciidoctor #{aargs} -b html5 -o - -a skip-front-matter #{adoc} >> #{out_file}"
  system(cmd)
end
