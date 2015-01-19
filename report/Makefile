all: a2.pdf

a2.pdf: report.tex
	pdflatex -synctex=1 -interaction=nonstopmode -shell-escape report.tex
	pdflatex -synctex=1 -interaction=nonstopmode -shell-escape report.tex
	mv report.pdf a2.pdf

clean:
	rm -f report.aux report.dvi report.log a1.pdf report.ps report.synctex.gz a2.pdf
