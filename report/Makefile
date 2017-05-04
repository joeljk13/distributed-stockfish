SHELL = /bin/bash

report.pdf: report.tex
	pdflatex --output-directory=pdf-aux -interaction nonstopmode report.tex
	pdflatex --output-directory=pdf-aux -interaction nonstopmode report.tex
	ln -sf pdf-aux/report.pdf report.pdf
	killall -HUP mupdf || true

.PHONY: edit
edit:
	$(MAKE) report.pdf
	bgrun mupdf report.pdf
	vim report.tex

.PHONY: clean
clean:
	rm -f report.pdf pdf-aux/*
