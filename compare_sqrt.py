import os
import sys
import random
import math

# Original bc
bc1 = '/usr/local/bin/bc'

# New bc
bc2 = './bin/bc'

cnt = 100

print("")
for i in range(1, cnt+1):
    print(f'[ {i} of {cnt} ]')
    scale1 = math.floor(10000*random.random())
    scale2 = math.floor(10000*random.random())
    a = 1 + math.floor(9*random.random())
    n = math.floor(10000*random.random())

    cmd = f'echo "scale={scale1}; a=1/{a}^{n}; scale={scale2}; sqrt(a);" | {bc2} > result2.txt'
    os.system(cmd)
    print('Finished new:      ' + cmd)

    cmd = f'echo "scale={scale1}; a=1/{a}^{n}; scale={scale2}; sqrt(a);" | {bc1} > result1.txt'
    os.system(cmd)
    print('Finished original: ' + cmd)

    cmd = 'diff result1.txt result2.txt'
    ret = os.system(cmd)
    print('Finished diff:     ' + cmd)
    if ret == 0:
        print("==== PASS ====\n")
    else:
        print("==== FAIL ====\n")
        sys.exit(-1)

print('OK')