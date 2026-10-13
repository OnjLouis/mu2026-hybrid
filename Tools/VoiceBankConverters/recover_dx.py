"""DX parameter recovery, using Yamaha's documented VCED field ranges.

ROM dump credit: Zhiyuan Wan (rgwan/fs1r_firmware_RE). Yamaha owns the data.
Names/bank assignments: official PLG150-DX manual; no executable code copied.
"""
import re
import struct
from bank_common import main_cli, require
CORE_BASE, CORE_STRIDE = 0x5464E, 206
ALIAS_BASE, ALIAS_STRIDE = 0x3F3C6, 56
OP_LIMITS = [99]*11 + [3,3,7,3,7,99,1,31,99,14]
GLOBAL_LIMITS = [99]*8 + [31,7,1,99,99,99,99,1,5,7,48]

def normal(name): return re.sub(r"\s+", "", name).lower()

def valid_patch(patch):
    return (len(patch) == 155
            and all(x <= limit for op in range(6) for x,limit in zip(patch[op*21:op*21+21],OP_LIMITS))
            and all(x <= limit for x,limit in zip(patch[126:145],GLOBAL_LIMITS)))

def recover(rom, manual):
    from pypdf import PdfReader
    aliases = {}
    for index in range(1100):
        data = rom[ALIAS_BASE+index*ALIAS_STRIDE:ALIAS_BASE+(index+1)*ALIAS_STRIDE]
        if not all(32 <= x < 127 for x in data[:8]): continue
        name = data[:8].decode()
        elements = []
        for offset in range(24,56,8):
            voice_id = data[offset]*128+data[offset+1]
            if not 1 <= voice_id <= 1024: continue
            start = CORE_BASE+(voice_id-1)*CORE_STRIDE
            patch = rom[start:start+155]
            if not valid_patch(patch):
                continue
            elements.append({"id":voice_id,"offset":start,"patch":patch.hex(),
                "level":data[offset+2],"shift":data[offset+4]-64,
                "detune":data[offset+5]-64})
        if elements:
            aliases.setdefault(normal(name), []).append({"name":name,"offset":ALIAS_BASE+index*56,"elements":elements})
    print("Recovered named aliases:",len(aliases))
    reader = PdfReader(manual)
    require(len(reader.pages) >= 43, "DX manual has too few pages")
    entries = {}
    missing = []
    for page_index in range(35,42):
        page = reader.pages[page_index]
        fragments = []
        page.extract_text(visitor_text=lambda text,cm,tm,font,size:
            fragments.append((text.strip(),tm[4],tm[5])) if text.strip() else None)
        lsb_label = next((f for f in fragments if f[0] == "Bank Select LSB"),None)
        msb_label = next((f for f in fragments if f[0] == "Bank Select MSB"),None)
        if not lsb_label or not msb_label: continue
        lsbs = [(int(t),x) for t,x,y in fragments if t.isdigit() and abs(y-lsb_label[2]) < 1 and x > lsb_label[1]+40]
        msbs = [(int(t),x) for t,x,y in fragments if t.isdigit() and abs(y-msb_label[2]) < 1 and x > msb_label[1]+40]
        # PDF text combines adjacent identical MSB headings ("8383"/"9999").
        # Each of these pages is a single documented DX-XG bank family.
        msb = 83 if page_index <= 38 else 99
        columns = [(msb,lsb,x) for lsb,x in lsbs]
        # Program numbers and category starts sit left of all voice columns.
        boundary = min(x for _,_,x in columns)-22
        rows = []
        for text,x,y in fragments:
            if x > boundary or y >= lsb_label[2]-8: continue
            if page_index == 35 and x < 130: continue
            match = re.fullmatch(r"(?:[A-Za-z ]+)?(\d{1,3})",text)
            if match and 1 <= int(match[1]) <= 128:
                rows.append((int(match[1])-1,y))
        for text,x,y in fragments:
            if y >= lsb_label[2]-8 or x <= boundary: continue
            if page_index == 41 and x > 260: continue
            match = re.fullmatch(r"(.+?)\s+([124])(?:\s+\d+)?\s*\*?",text)
            if not match or not rows: continue
            name, element_count = match[1],int(match[2])
            column = min(columns,key=lambda c:abs(c[2]-x-17))
            row = min(rows,key=lambda r:abs(r[1]-y))
            if abs(row[1]-y)>1: continue
            key = (*column[:2],row[0])
            found = aliases.get(normal(name),[])
            # The same alias can occur in several tables; require matching element count.
            found = [a for a in found if len(a["elements"])==element_count]
            if not found:
                missing.append({"bank":key,"name":name,"elements":element_count,"page":page_index+1})
                continue
            entry = {"msb":key[0],"lsb":key[1],"program":key[2],"name":name,
                     "page":page_index+1,"alias":found[0]}
            if key in entries:
                require(normal(entries[key]["name"]) == normal(name), "Conflicting DX manual selection")
            entries[key] = entry
        if page_index == 35:
            sfx_rows = [(int(t)-1,y) for t,x,y in fragments if t.isdigit()
                        and 50 < x < 60 and y < 606 and 1 <= int(t) <= 128]
            sfx_rows.append((0,600.79))
            for text,x,y in fragments:
                if not 62 < x < 120: continue
                match = re.fullmatch(r"(.+?)\s+([124])(?:\s+\d+)?\s*\*?",text)
                if not match: continue
                row = min(sfx_rows,key=lambda r:abs(r[1]-y))
                if abs(row[1]-y)>1: continue
                name=match[1]
                found=[a for a in aliases.get(normal(name),[]) if len(a["elements"])==int(match[2])]
                key=(67,0,row[0])
                if found:
                    entries[key]={"msb":67,"lsb":0,"program":row[0],"name":name,
                                  "page":36,"alias":found[0]}
                else: missing.append({"bank":key,"name":name,"page":36})
    # Native preset page has complete program/name rows rather than sparse XG columns.
    native_lsb = 1
    lines = reader.pages[42].extract_text().splitlines()
    for text in lines:
        if re.fullmatch(r'Bank Select LSB\s+[12]', text.strip()):
            native_lsb = int(text.strip()[-1])
        match = re.fullmatch(r'(?:[A-Za-z ]+)?(\d{1,3})\s+(.+?)\s+([124])\s+(?:\d+|�).*', text.strip())
        if not match or not 1 <= int(match[1]) <= 128: continue
        pc, name, layers = int(match[1])-1, match[2], int(match[3])
        found = [a for a in aliases.get(normal(name),[]) if len(a['elements']) == layers]
        if found:
            entries[(35,native_lsb,pc)] = {'msb':35,'lsb':native_lsb,'program':pc,
                'name':name,'page':43,'alias':found[0]}
        else:
            missing.append({'bank':[35,native_lsb,pc],'name':name,'elements':layers,'page':43})
    print("Documented recovered bank selections:",len(entries),"unmatched",len(missing))
    require(len(entries) == 1602 and len(missing) == 97, "Unexpected DX selection coverage")
    output = bytearray(b"DXP1"+struct.pack("<I",len(entries)))
    for key,entry in sorted(entries.items()):
        elements=entry["alias"]["elements"]
        output.extend(bytes([*key,len(elements)]))
        for element in elements:
            output.extend(bytes([element["level"],element["shift"]+64,element["detune"]+64,0]))
            output.extend(bytes.fromhex(element["patch"]))
    return bytes(output)

if __name__ == "__main__":
    raise SystemExit(main_cli("dx", recover))
