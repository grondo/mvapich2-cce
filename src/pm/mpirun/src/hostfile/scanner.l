%{
/*
 * Copyright (c) 2003-2011, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */

#include <parser.h>
#include <string.h>
%}

%option noyywrap
%option nounput
%option noinput

decimal     ([[:digit:]]+)
whitespace  ([ \t]+)
text        ([^[:space:]:#]+)
comment     (#.*)

%%

{comment}|{whitespace}  /* eat up comments and whitespace */

{decimal}               { yylval.decimal = (size_t)atoi(yytext);
                          return DECIMAL; }
{text}                  { yylval.text = strdup(yytext); return TEXT; }
:                       { return ':'; }
\n                      { return '\n'; }

