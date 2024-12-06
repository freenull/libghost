PREP ?= ../../../../../build/examples/prep/prep
output.txt: index.txt web.txt embed.txt math.txt table.txt
	$(PREP) -p ../../plugins/web.lua -p ../../plugins/embed.lua -p ../../plugins/math.lua -p ../../plugins/table.lua index.txt > output.txt
	cat output.txt
