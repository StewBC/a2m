Pandoc/latex PDF manual maker for Linux.

1. Install Pandoc and XeLaTeX (TeX Live / MikTeX).  See below.
2. Run: ./build-manual.sh
3. Outputs: a2m User Manual.pdf

Customize pandoc.yaml to adjust fonts or margins.

Linux install commands:
sudo apt update
sudo apt install pandoc texlive-xetex texlive-fonts-recommended texlive-fonts-extra texlive-latex-extra fonts-inconsolata

The version of pandoc.yaml that works with Linix is:
from: markdown
to: pdf
pdf-engine: xelatex

include-before-body: titlepage.tex
include-in-header: header.tex

toc: false
number-sections: true

variables:
  geometry: "margin=1in"
  mainfont: "TeX Gyre Pagella"
  monofont: "Inconsolata"

metadata:
  lang: en-US

I have now switched to builing in macOS.  The version at HEAD:manual/pandoc.yaml is for macOS and has hard-coded paths.  Unfortunately I did not write down the steps to configure on macOS.  It was a slog though.
It looked something like this (from my shell history):

brew install pandoc
brew install --cask basictex
brew tap homebrew/cask-fonts
brew install --cask font-inconsolata
brew update && brew upgrade && brew cleanup
sudo tlmgr update --self
sudo tlmgr install xetex
export PATH="/usr/local/texlive/2026basic/bin/universal-darwin:/Library/TeX/texbin:$PATH"
which tlmgr
which xelatex
sudo tlmgr update --self
sudo tlmgr install collection-fontsrecommended
sudo mktexlsr
sudo tlmgr install tex-gyre
sudo mktexlsr
kpsewhich texgyrepagella-regular.otf
kpsewhich texgyrepagella-bold.otf
kpsewhich texgyrepagella-italic.otf
kpsewhich texgyrepagella-bolditalic.otf
kpsewhich lmmono10-regular.otf
sudo tlmgr install titlesec
sudo tlmgr install needspace
./build-manual.sh
