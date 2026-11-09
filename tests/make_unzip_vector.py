"""Makes tests/unzip_vector.h, the zip test_music_build unpacks (src/unzip.c): every DEFLATE
block type, stored files, a folder, an empty file. Random words and bytes, nothing else.

    python3 tests/make_unzip_vector.py > tests/unzip_vector.h
"""
import io
import random
import zipfile
import zlib

rnd = random.Random(7)
words = ["race", "masters", "lap", "nitro", "pit", "stop", "turbo", "grid", "finish", "corner"]
text = " ".join(rnd.choice(words) for _ in range(3000)).encode()
far = bytes(rnd.randrange(256) for _ in range(300)) + bytes(30000) + bytes(rnd.randrange(256) for _ in range(300))
far = far + far[:4000]  # back-references close to the 32 KB window
noise = bytes(rnd.randrange(256) for _ in range(700))  # incompressible: a stored block inside deflate
files = [
    ("a/", None, None),
    ("a/hello.txt", b"hello hello hello\n", zipfile.ZIP_DEFLATED),  # fixed Huffman
    ("a/b/text.txt", text, zipfile.ZIP_DEFLATED),                  # dynamic Huffman
    ("stored.bin", b"stored as is" * 3, zipfile.ZIP_STORED),
    ("empty.txt", b"", zipfile.ZIP_DEFLATED),
    ("far.bin", far, zipfile.ZIP_DEFLATED),
    ("noise.bin", noise, zipfile.ZIP_DEFLATED),
]
buf = io.BytesIO()
with zipfile.ZipFile(buf, "w") as z:
    for name, data, method in files:
        info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))  # fixed: the same bytes every time
        if data is None:
            z.writestr(info, b"")
        else:
            info.compress_type = method
            z.writestr(info, data, compresslevel=9 if method == zipfile.ZIP_DEFLATED else None)
data = buf.getvalue()
print(f"// {len(data)} bytes, made by Python's zipfile (stored, fixed and dynamic Huffman, a stored")
print("// DEFLATE block, long back-references, an empty file, a folder).")
print("static const unsigned char kZip[] = {")
for i in range(0, len(data), 24):
    print("  " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
print("};")
print("static const struct { const char* name; unsigned size; unsigned crc; } kZipFiles[] = {")
for name, d, _ in files:
    if d is None:
        continue
    print(f'  {{"{name}", {len(d)}u, 0x{zlib.crc32(d):08x}u}},')
print("};")
