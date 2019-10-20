#!/usr/bin/env python3
import os
import sys
import tempfile

def write_array_variable(outputfile, name, value):
    values = value[1:-1].split(',')
    str = f'int {name} = {{\n    '
    count = 0
    for v in values:
        v = v.lstrip().rstrip()
        str += f'{v}, '
        count += 1
        if count == 8:
            str = str[:-1] + '\n    '
            count = 0
    if count == 0:
        str = str[:-6]
    else:
        str = str[:-2]
    str += '\n};\n'

    outputfile.write(str)

def write_variable(outputfile, name, value):
    if not '[' in name:
        outputfile.write(f'int {name} = {value};\n')
    else:
        write_array_variable(outputfile, name, value)

def parse_array_value(paramfile, value):
    array_value = value;
    while not ';' in array_value:
        line = paramfile.readline()
        line = line.rstrip().lstrip()
        array_value += line

    return array_value

def parse_variable(paramfile, line):
    line = line[line.find(' ')+1:]

    name = line[:line.find(' ')]
    value = line[line.find('=')+1:]
    value = value.lstrip();

    if '[' in name:
        if not ';' in value:
            value = parse_array_value(paramfile, value)

    # Skip semicolon at the end
    value = value[:-1]

    return (name, value)

def parse_param(line):
    return (line[:line.find(' ')], line[line.find(' ')+1:])

def read_input_file(inputpath):
    tuning_dict = {}
    with open(inputpath) as inputfile:
        for line in inputfile:
            line = line.rstrip()
            if len(line) == 0:
                continue
            if line[0] == '#':
                continue
            name, value = parse_param(line)
            tuning_dict[name] = value
    return tuning_dict

def write_new_file(inputpath, outputpath):
    pass

if len(sys.argv) != 3:
    print('Missing arguments')
    print('genevalparams.py <input> <output>')
    sys.exit(0)

inputpath = sys.argv[1]
outputpath = sys.argv[2]

tuning_dict = read_input_file(inputpath)

tmpfile = tempfile.NamedTemporaryFile(mode='w', delete=False)

with open(outputpath) as paramfile:
    while True:
        line = paramfile.readline()
        if not line:
            break
        line = line.rstrip()
        if len(line) == 0:
            tmpfile.write('\n');
            continue
        if line.find('int') != 0:
            tmpfile.write(f'{line}\n')
            continue
        name, value = parse_variable(paramfile, line)

        tuning_name = name
        if '[' in tuning_name:
            tuning_name = tuning_name[:tuning_name.find('[')]
        if tuning_name.lower() in tuning_dict:
            value = tuning_dict[tuning_name.lower()]

        write_variable(tmpfile, name, value)

tmpname = tmpfile.name
tmpfile.close()
os.remove(outputpath)
os.rename(tmpname, outputpath)
