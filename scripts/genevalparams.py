#!/usr/bin/env python3
import sys
import chess
import chess.pgn

def print_gpl_header(output):
    output.write('/*\n');
    output.write(' * Marvin - an UCI/XBoard compatible chess engine\n')
    output.write(' * Copyright (C) 2015 Martin Danielsson\n')
    output.write(' *\n')
    output.write(' * This program is free software: you can redistribute it and/or modify\n')
    output.write(' * it under the terms of the GNU General Public License as published by\n')
    output.write(' * the Free Software Foundation, either version 3 of the License, or\n')
    output.write(' * (at your option) any later version.\n')
    output.write(' *\n')
    output.write(' * This program is distributed in the hope that it will be useful,\n')
    output.write(' * but WITHOUT ANY WARRANTY; without even the implied warranty of\n')
    output.write(' * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n')
    output.write(' * GNU General Public License for more details.\n')
    output.write(' *\n')
    output.write(' * You should have received a copy of the GNU General Public License\n')
    output.write(' * along with this program.  If not, see <http://www.gnu.org/licenses/>.\n')
    output.write(' */\n');

def print_include_directives(output):
    output.write('#include "evalparams.h"\n')

def print_psq_table(output, name, line):
    parts = line.split('{')
    values = parts[1][:-1].split(', ')
    str = 'int %s[NSQUARES] = {\n    ' % (name.upper())
    count = 0;
    for value in values:
        str = str + value + ', '
        count = count + 1;
        if (count == 8):
            str = str[:-1]
            str = str + '\n    '
            count = 0
    str = str[:-6]
    str = str + '\n};\n'
    output.write(str)

def print_param(output, name, value):
    str = 'int %s = %s;\n' % (name.upper(), value)
    output.write(str)

if len(sys.argv) != 2:
    print('Missing arguments')
    print('genevalparams.py <input>')
    sys.exit(0)

inputfile = sys.argv[1]
outputfile = 'evalparams.c'
output = open(outputfile, 'w')

print_gpl_header(output)
print_include_directives(output)
output.write('\n')

for line in open(inputfile):
    line = line.rstrip()
    parts = line.split(' ')
    if parts[0].startswith('psq_table_'):
        print_psq_table(output, parts[0], line)
    else:
        print_param(output, parts[0], parts[1])
