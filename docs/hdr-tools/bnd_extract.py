"""Extract FromSoftware DCX(DFLT) + BND3/BND4 archives (Bloodborne shaderbnd).
Usage: python -I bnd_extract.py <in.dcx> <out_dir>"""
import os, struct, sys, zlib


def undcx(data):
    if data[:4] != b'DCX\0':
        return data
    assert data[0x28:0x2c] == b'DFLT', data[0x28:0x2c]
    usize, csize = struct.unpack('>II', data[0x1c:0x24])
    raw = zlib.decompress(data[0x4c:0x4c + csize])
    assert len(raw) == usize
    return raw


def cstr(data, off, wide):
    if wide:
        end = off
        while data[end:end + 2] != b'\0\0':
            end += 2
        return data[off:end].decode('utf-16-le')
    end = data.index(b'\0', off)
    return data[off:end].decode('shift_jis', 'replace')


def bnd4(data):
    assert data[:4] == b'BND4'
    big = data[9] == 1
    e = '>' if big else '<'
    count = struct.unpack(e + 'i', data[0x0c:0x10])[0]
    entry_size = struct.unpack(e + 'q', data[0x20:0x28])[0]
    unicode_ = data[0x30] == 1
    raw_fmt = data[0x31]
    # bit-reversed format byte unless bitBigEndian
    fmt = int(f'{raw_fmt:08b}'[::-1], 2) if data[0x0a] == 0 or True else raw_fmt
    pos = 0x40
    out = []
    for _ in range(count):
        p = pos + 8  # flags(1)+pad(3), -1
        csize = struct.unpack(e + 'q', data[p:p + 8])[0]; p += 8
        if fmt & 0x20:
            p += 8  # uncompressed size
        if fmt & 0x10:
            off = struct.unpack(e + 'q', data[p:p + 8])[0]; p += 8
        else:
            off = struct.unpack(e + 'I', data[p:p + 4])[0]; p += 4
        if fmt & 0x02:
            p += 4
        name_off = 0
        if fmt & 0x0c:
            name_off = struct.unpack(e + 'I', data[p:p + 4])[0]; p += 4
        pos += entry_size
        name = cstr(data, name_off, unicode_) if name_off else f'file_{len(out)}'
        out.append((name, data[off:off + csize]))
    return out


def bnd3(data):
    assert data[:4] == b'BND3'
    fmt = data[0x0c]
    big = data[0x0d] == 1 or data[0x0f] == 0
    e = '>' if big else '<'
    count = struct.unpack(e + 'i', data[0x10:0x14])[0]
    pos = 0x20
    out = []
    for _ in range(count):
        pos += 4
        csize = struct.unpack(e + 'i', data[pos:pos + 4])[0]; pos += 4
        off = struct.unpack(e + 'i', data[pos:pos + 4])[0]; pos += 4
        if fmt & 0x40 or fmt & 0x02:
            pos += 4
        name_off = 0
        if fmt & 0x04 or fmt & 0x08:
            name_off = struct.unpack(e + 'i', data[pos:pos + 4])[0]; pos += 4
        if fmt & 0x04 and fmt & 0x20:
            pos += 4
        name = cstr(data, name_off, False) if name_off else f'file_{len(out)}'
        out.append((name, data[off:off + csize]))
    return out


def walk(data, out_dir, depth=0):
    data = undcx(data)
    if data[:4] == b'BND4':
        entries = bnd4(data)
    elif data[:4] == b'BND3':
        entries = bnd3(data)
    else:
        return
    for name, blob in entries:
        rel = name.replace('\\', '/').lstrip('/').split(':')[-1].lstrip('/')
        path = os.path.join(out_dir, rel)
        os.makedirs(os.path.dirname(path) or out_dir, exist_ok=True)
        blob = undcx(blob)
        with open(path, 'wb') as f:
            f.write(blob)
        if blob[:4] in (b'BND4', b'BND3'):
            walk(blob, path + '_x', depth + 1)


if __name__ == '__main__':
    with open(sys.argv[1], 'rb') as f:
        walk(f.read(), sys.argv[2])
