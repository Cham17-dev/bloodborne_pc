"""List the shadPS4 pgm_hash (OrbShdr BinaryInfo.shader_hash) of every extracted shader.
Usage: python -I shader_hashes.py <out_dir>"""
import os, struct, sys

root = sys.argv[1]
for dirpath, _, files in os.walk(root):
    for name in sorted(files):
        path = os.path.join(dirpath, name)
        data = open(path, 'rb').read()
        hits = []
        start = 0
        while True:
            i = data.find(b'OrbShdr', start)
            if i < 0:
                break
            h = struct.unpack('<Q', data[i + 16:i + 24])[0]
            length = struct.unpack('<I', data[i + 8:i + 12])[0] >> 8
            hits.append(f'{h:#x}(len {length})')
            start = i + 7
        print(f'{os.path.relpath(path, root)}\t{len(data)}\t{" ".join(hits)}')
