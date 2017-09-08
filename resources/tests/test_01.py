
def verify(output):
    scopes = False
    fail = False
    for line in output.split('\n'):
        if scopes:
            if line.startswith('  '):
                name,size = [x.strip() for x in line.split('-')]
                if size != '0':
                    print("unfreed memory in scope",name,":",size)
                    fail = True
            continue
        if line.startswith('Unfreed memory'):
            scopes = True
    if fail:
        raise Exception('unfreed memory')