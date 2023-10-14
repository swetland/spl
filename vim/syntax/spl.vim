
if exists("b:current_syntax")
  finish
endif

let b:current_syntax = "spl"

syntax case match

syntax match splNumber "\v<\d+>"
syntax match splNumber "\v<0x\x+>"
syntax match splNumber "\v<0b[01]+>"

" floating point
" syntax match splNumber "\v<\d+\.\d+>"
" syntax match splNumber "\v<\d*\.?\d+([Ee]-?)?\d+>"
" syntax match splNumber "\v<0x\x+([Pp]-?)?\x+>"

syntax keyword splStatement return break continue
syntax keyword splCond if else
syntax keyword splLoop while for
syntax keyword splDef fn var struct enum
syntax keyword splType u8 u32 i32 str bool
syntax keyword splConstant nil
syntax keyword splBool true false

syntax match splEscape display contained "\v\\[nt\\\'\"]"
syntax match splEscHex display contained "\v\\x\x{2}"
syntax region splString start=/"/ skip=/\\"/ end=/"/ oneline contains=splEscape,splEscHex
syntax region splChar start=/'/ skip=/\\'/ end=/'/ oneline contains=splEscape,splEscHex

syntax keyword splTodo contained TODO FIXME XXX
syntax region splComment start="//" end="$" contains=splTodo

syntax region splParen start='(' end=')' transparent
syntax region splBlock start="{" end="}" transparent

syntax keyword splBuiltin _hexout_

" map language-specifc names to standard highlight names
highlight default link splNumber Number

highlight default link splStatement Statement
highlight default link splCond Conditional
highlight default link splLoop Repeat
highlight default link splDef Keyword
highlight default link splType Type
highlight default link splConstant Constant
highlight default link splBool Boolean

highlight default link splString String
highlight default link splChar Character

highlight default link splTodo Todo
highlight default link splComment Comment

highlight default link splBuiltin Identifier

" highlight default link splOperator Operator
