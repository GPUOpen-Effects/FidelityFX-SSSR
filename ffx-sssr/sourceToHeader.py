import sys

def escape(line):
    line = line.replace ('"', '\\"')
    line = line.replace ('\n', '\\n')
    return line

if __name__=='__main__':
    print ('const char {}[] = '.format (sys.argv[2]))
    for l in open(sys.argv[1], 'r').readlines():
        print ('"{}"'.format (escape (l)))
    print (';')