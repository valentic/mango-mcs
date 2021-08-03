" An example for a vimrc file.
"
" Maintainer:   Bram Moolenaar <Bram@vim.org>
" Last change:  2000 Mar 29
"
" To use it, copy it to
"     for Unix and OS/2:  ~/.vimrc
"         for Amiga:  s:.vimrc
"  for MS-DOS and Win32:  $VIM\_vimrc
"       for OpenVMS:  sys$login:.vimrc

" Use Vim settings, rather then Vi settings (much better!).
" This must be first, because it changes other options as a side effect.
set nocompatible

" maps the F1 key to Esc to disable going into Help.
" On some IBM ThinkPad laptops, the ESC key is above F1
" you may keep hitting F1 and going into help!
map <F1> <ESC>
imap <F1> <ESC>
vmap <F1> <ESC>

set background=dark
set backupcopy=yes
set title
set wm=10
set tabstop=4
set shiftwidth=4
set softtabstop=4
set expandtab

"show trailing whitespace as highlighted periods
"set list listchars=trail:*

" allow backspacing over everything in insert mode
set backspace=indent,eol,start

set ai  " always set autoindenting on
set viminfo='20,\"50    " read/write a .viminfo file, don't store more than 50 lines of registers
set history=50 " keep 50 lines of command line history
set ruler " show the cursor position all the time

" Save .swp files to /tmp, avoid backup by dropbox. See
" http://stackoverflow.com/questions/821902/disabling-swap-files-creation-in-vim
if !isdirectory($HOME . '/.vim/backup')
    silent execute '!mkdir -p ~/.vim/backup'
endif
if !isdirectory($HOME . '/.vim/swap')
    silent execute '!mkdir ~/.vim/swap'
endif
if !isdirectory($HOME . '/.vim/undo')
    silent execute '!mkdir ~/.vim/undo'
endif
set backupdir=~/.vim/backup//
set directory=~/.vim/swap//
set undodir=~/.vim/undo//

" Switch syntax highlighting on, when the terminal has colors
" Also switch on highlighting the last used search pattern.
if &t_Co > 2 || has("gui_running")
  syntax on
  set hlsearch
endif

" automatically delete trailing whitespace
"autocmd BufRead * silent! %s/\s\+$//
"autocmd BufEnter *.py :%s/\s\+$//e

