"""Put a Surge XT patch (.fxp chunk) into the plugin state pedalboard exposes (raw_state).

A VST3 state from JUCE is 'VC2!' + length + XML; the XML's <IComponent> holds the plugin's
own state chunk in JUCE's base-64 variant (6 bits per character, least significant first).
"""
import re
import struct

ALPH = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"


def jdec(s):
    size, enc = s.split(".", 1)
    size = int(size)
    out = bytearray(size)
    bit = 0
    for ch in enc:
        v = ALPH.index(ch)
        for b in range(6):
            if bit >= size * 8:
                break
            if v & (1 << b):
                out[bit >> 3] |= 1 << (bit & 7)
            bit += 1
    return bytes(out)


def jenc(data):
    """jdec's inverse: 6 bits a character, least significant first, the last character's
    missing bits 0. Three bytes (24 bits) make four characters."""
    n = len(data)
    padded = bytes(data) + bytes(-n % 3)
    chars = []
    for i in range(0, len(padded), 3):
        v = padded[i] | padded[i + 1] << 8 | padded[i + 2] << 16
        chars.append(ALPH[v & 63] + ALPH[v >> 6 & 63] + ALPH[v >> 12 & 63] + ALPH[v >> 18])
    return f"{n}." + "".join(chars)[:(n * 8 + 5) // 6]


def split(raw):
    assert raw[:4] == b"VC2!"
    ln = struct.unpack("<I", raw[4:8])[0]
    xml = raw[8:8 + ln].decode()
    m = re.search(r"<IComponent>([^<]*)</IComponent>", xml)
    return xml, m


def get_chunk(raw):
    xml, m = split(raw)
    return jdec(m.group(1))


def set_chunk(raw, chunk):
    xml, m = split(raw)
    xml2 = xml[:m.start(1)] + jenc(chunk) + xml[m.end(1):]
    b = xml2.encode()
    return b"VC2!" + struct.pack("<I", len(b)) + b + raw[8 + struct.unpack("<I", raw[4:8])[0]:]
