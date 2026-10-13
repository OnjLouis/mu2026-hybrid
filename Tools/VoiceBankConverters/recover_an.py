"""AN parameter recovery, credited to Yamaha and dump contributor rgwan.

No executable firmware is embedded. This establishes data layout, not audio
equivalence to Yamaha's VOP3. An audio engine is a separate implementation.
"""
import re
import struct
from bank_common import main_cli, require
FACTORY_BASE = 0x30524
COMMON_SIZE, SCENE_SIZE, SEQUENCE_SIZE, FREE_EG_SIZE = 104, 122, 70, 768

def normal(text):
    return re.sub(r'\s+', '', text).casefold()

def valid_scene(scene):
    if len(scene) != SCENE_SIZE or any(b > 127 for b in scene):
        return False
    limits = {0:(0,2), 1:(40,88), 2:(40,88), 5:(1,3), 6:(0,1),
        8:(0,1), 9:(0,20), 10:(0,1), 13:(0,1), 15:(0,2),
        18:(0,4), 19:(1,3), 21:(0,4), 22:(0,7), 23:(0,6),
        25:(14,114), 29:(0,7), 30:(0,1), 32:(0,5), 34:(14,114),
        38:(0,7), 39:(0,1), 50:(0,5), 52:(13,127), 53:(0,1),
        56:(32,127), 66:(1,127), 117:(62,66), 119:(0,20),
        120:(0,15), 121:(1,3)}
    return all(lo <= scene[index] <= hi for index,(lo,hi) in limits.items())

def recover(raw, manual):
    from pypdf import PdfReader
    import pdfplumber
    rom = bytearray(raw)
    rom[0::2], rom[1::2] = raw[1::2], raw[0::2]
    reader = PdfReader(manual)
    require(len(reader.pages) >= 44, 'AN manual has too few pages')
    documented = []
    for page in reader.pages[37:41]:
        text = page.extract_text()
        pattern = r'(?:^|\n)(\d{1,3})\s*\n?\s*(.+?)\s+[A-Za-z]{2}\s+(?:Poly|Mono|Legato)'
        documented.extend((int(m[1])-1, m[2].strip()) for m in re.finditer(pattern,text))
    print('Documented factory names:', len(documented))
    cursor = FACTORY_BASE
    presets = []
    for index in range(256):
        if index == 128:
            require(cursor == 0x3E124, 'Unexpected AN factory-bank boundary')
            cursor = 0x40424
        common = bytes(rom[cursor:cursor+COMMON_SIZE])
        require(len(common) == COMMON_SIZE and all(32 <= v <= 126 for v in common[:10]), 'Invalid AN preset name')
        candidates = []
        for eg_size in (0,FREE_EG_SIZE):
            scene_start = cursor + COMMON_SIZE + eg_size
            scene = bytes(rom[scene_start:scene_start+SCENE_SIZE])
            end = scene_start + SCENE_SIZE + SEQUENCE_SIZE
            name_next = rom[end:end+10]
            if valid_scene(scene) and (index in (127,255) or all(32 <= v <= 126 for v in name_next)):
                candidates.append((eg_size,scene,end))
        require(len(candidates) == 1, 'Ambiguous or invalid AN scene boundary')
        eg_size,scene,end = candidates[0]
        name = common[:10].decode('ascii').strip()
        presets.append({'index':index,'name':name,'offset':cursor,'size':end-cursor,
            'common':common.hex(), 'scene':scene.hex(),
            'free_eg':bytes(rom[cursor+COMMON_SIZE:cursor+COMMON_SIZE+eg_size]).hex(),
            'sequence':bytes(rom[end-SEQUENCE_SIZE:end]).hex()})
        cursor = end
    require(len(documented) == 256, 'Unexpected AN manual preset count')
    disagreements=[]
    for index,(program,name) in enumerate(documented):
        require(program == index % 128, 'Unexpected AN manual program order')
        if normal(name) != normal(presets[index]['name']):
            disagreements.append({'index':index,'manual':name,'rom':presets[index]['name']})
    print('Factory names disagreements:', disagreements)
    require(not disagreements, 'AN factory names disagree with the manual')
    entries = {(36,index//128,index%128):index for index in range(256)}
    aliases = {normal(p['name']):p['index'] for p in presets}
    missing=[]
    with pdfplumber.open(manual) as pdf:
        for page_number in (42,43):
            words=pdf.pages[page_number].extract_words()
            headings=[w for w in words if w['text']=='LSB']
            for heading in headings:
                top=heading['top']
                end=min((w['top'] for w in headings if w['top']>top+1),default=750)
                columns=[(int(w['text']),(w['x0']+w['x1'])/2) for w in words
                    if w['text'].isdigit() and abs(w['top']-top)<1 and w['x0']>heading['x1']]
                require(columns, 'AN manual bank columns not found')
                rows=[w for w in words if w['text'].isdigit() and 205<w['x0']<216 and top+8<w['top']<end]
                for row in rows:
                    cells={}
                    for word in words:
                        if word['x0']<220 or abs(word['top']-row['top'])>0.6:
                            continue
                        lsb,_=min(columns,key=lambda c:abs(c[1]-(word['x0']+word['x1'])/2))
                        cells.setdefault(lsb,[]).append(word)
                    for lsb,cell in cells.items():
                        name=' '.join(w['text'] for w in sorted(cell,key=lambda w:w['x0']))
                        pc=int(row['text'])-1
                        if normal(name) not in aliases:
                            missing.append({'name':name,'program':pc,'lsb':lsb,'page':page_number+1})
                            continue
                        for msb in (84,100):
                            key=(msb,lsb,pc)
                            require(key not in entries, 'Duplicate AN bank selection')
                            entries[key]=aliases[normal(name)]
    for match in re.finditer(r'(?m)^(\d{1,3})\s+([12])\s+(\d{1,3})\s+(.+?)\s+[A-Za-z]{2}\s*$',reader.pages[41].extract_text()):
        pc,bank,native_pc,name=int(match[1])-1,int(match[2])-1,int(match[3])-1,match[4]
        index=bank*128+native_pc
        require(normal(name)==normal(presets[index]['name']), 'AN user-slot name mismatch')
        entries[(36,2,pc)]=index
    print('User slots:',sum(key[:2]==(36,2) for key in entries))
    print('Recovered',len(presets),'factory presets;',len(entries),'selections; end',hex(cursor),'unmatched',len(missing))
    require(len(entries) == 848 and len(missing) == 4, 'Unexpected AN selection coverage')
    output=bytearray(b'ANP1'+struct.pack('<II',len(presets),len(entries)))
    for p in presets:
        for field in ('common','scene','sequence'):
            output.extend(bytes.fromhex(p[field]))
        eg=bytes.fromhex(p['free_eg'])
        output.extend(struct.pack('<I',len(eg)))
        output.extend(eg)
    for key,idx in sorted(entries.items()):
        output.extend(struct.pack('<BBBH',*key,idx))
    return bytes(output)

if __name__=='__main__':
    raise SystemExit(main_cli('an', recover))
