#!/usr/bin/env python3
import sys
import chess
import chess.engine

def handle_pv_line(info, bmlist, amlist):
    time = info.get("time")
    pv = info.get("pv")

    # Check the move suggested by the engine against bm and am
    move = pv[0]
    if len(bmlist) > 0 and move in bmlist:
        resultstr = 'Yes'
        test_pass = True
    elif len(amlist) > 0 and move not in amlist:
        resultstr = 'Yes'
        test_pass = True
    else:
        resultstr = 'No'
        test_pass = False

    # Display PV 
    pvstr = ""
    for move in pv:
        pvstr = pvstr + move.uci() + ' '
    print('%s\tpv %s' % (resultstr, pvstr))

    return (test_pass, time)

def run_epd_test(engine, epdline, testtime):
    board = chess.Board()
    params = board.set_epd(epdline)

    print(' ')
    if 'id' in params:
        print('Id: %s' % params['id'])
    print('Fen: %s' % board.fen())
    bmparams = {}
    if 'bm' in params:
        bmparams = params['bm']
        str = ''
        for move in params['bm']:
            str = str + move.uci() + ' '
        print('Bm: %s' % str)
    amparams = {}
    if 'am' in params:
        amparams = params['am']
        str = ''
        for move in params['am']:
            str = str + move.uci() + ' '
        print('Am: %s' % str)
    print(' ')

    limit = chess.engine.Limit(time=testtime)

    test_pass = False
    test_time = -1
    with engine.analysis(board, limit) as analysis:
        for info in analysis:
            if "time" in info:
                time = info.get("time")
                if time > testtime:
                    break
            if "pv" in info:
                (passed, time) = handle_pv_line(info, bmparams, amparams)
                if passed and test_time == -1:
                    test_pass = True
                    test_time = time
                elif not passed:
                    test_pass = False
                    test_time = -1

    print('')
    str = ''
    if test_pass:
        str = 'Pass'
    else:
        str = 'Fail'
    print('Result: %s' % str)
    print('')
    print('****************************************')

    if not test_pass:
        test_time = testtime

    return (test_pass, test_time)

if len(sys.argv) != 4:
    print('Missing arguments')
    print('epd2uci.py <engine> <testsuite> <time>')
    sys.exit(0)
   
print('Suite: %s' % sys.argv[2])
print('Time: %s' % sys.argv[3])
print('')
print('****************************************')

epdfile = open(sys.argv[2], 'r')
testtime = int(sys.argv[3])

engine = chess.engine.SimpleEngine.popen_uci(sys.argv[1])
engine.configure({"Hash":32,
#                  "OwnBook":"false",
                  "Threads":1,
                  "SyzygyPath":""})    

result_list = []
for line in epdfile.readlines():
    result = run_epd_test(engine, line, testtime)
    result_list.append(result)    

# Print test suite result
print(' ')
print('Test case\tSolved')
tc = 1
npass = 0
nfail = 0
total_time = 0
for result in result_list:
    r = 'No'
    if result[0]:
        npass = npass + 1
        r = 'Yes'
    else:
        nfail = nfail + 1
    str = '    %d\t\t   %s (%.3fs)' % (tc, r, result[1])
    print(str)
    total_time = total_time + result[1]
    tc = tc + 1

print(' ')
print('%d problems solved.' % npass)
print('%d problems unsolved.' % nfail)
avg_time = float(total_time)/float(npass+nfail)
print('Average time: %.3fs' % avg_time)
print(' ')

engine.quit()

