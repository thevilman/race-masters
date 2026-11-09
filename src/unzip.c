// Zip archives in memory: the central directory, stored and deflated files, CRC-32 checks.
// DEFLATE (RFC 1951) is decoded with canonical Huffman tables built per block. See unzip.h.

#include "unzip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t tg_crc32(const uint8_t* data, size_t n) {
  static uint32_t table[256];
  static bool ready;
  if(!ready) {
    for(uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for(int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    ready = true;
  }
  uint32_t c = 0xFFFFFFFFu;
  for(size_t i = 0; i < n; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

// --- DEFLATE ------------------------------------------------------------------------------

#define MAX_BITS 15

typedef struct {
  const uint8_t* in;
  size_t inSize, inPos;
  uint32_t bits;  // bits read but not used yet, the next one lowest
  int nbits;      // always under 8 between calls
  uint8_t* out;
  size_t outSize, outPos;
  bool bad;       // ran past the input
} Inflate;

static unsigned getBits(Inflate* s, int n) {
  uint32_t v = s->bits;
  while(s->nbits < n) {
    if(s->inPos >= s->inSize) {
      s->bad = true;
      return 0;
    }
    v |= (uint32_t) s->in[s->inPos++] << s->nbits;
    s->nbits += 8;
  }
  s->bits = n < 32 ? v >> n : 0;
  s->nbits -= n;
  return n ? v & ((1u << n) - 1) : 0;
}

// A canonical Huffman code: how many codes of each length, and the symbols in code order.
typedef struct {
  uint16_t count[MAX_BITS + 1];
  uint16_t symbol[288];
} Huffman;

static bool buildHuffman(Huffman* h, const uint8_t* lengths, int n) {
  memset(h->count, 0, sizeof(h->count));
  for(int i = 0; i < n; i++) h->count[lengths[i]]++;
  if(h->count[0] == n) return true;  // no codes (an unused distance code)
  int left = 1;
  for(int len = 1; len <= MAX_BITS; len++) {
    left = (left << 1) - h->count[len];
    if(left < 0) return false;  // more codes than the lengths allow
  }
  uint16_t offs[MAX_BITS + 1];
  offs[1] = 0;
  for(int len = 1; len < MAX_BITS; len++) offs[len + 1] = (uint16_t) (offs[len] + h->count[len]);
  for(int i = 0; i < n; i++)
    if(lengths[i]) h->symbol[offs[lengths[i]]++] = (uint16_t) i;
  return true;
}

static int decodeSymbol(Inflate* s, const Huffman* h) {
  int code = 0, first = 0, index = 0;
  for(int len = 1; len <= MAX_BITS; len++) {
    code |= (int) getBits(s, 1);
    if(s->bad) return -1;
    int count = h->count[len];
    if(code - first < count) return h->symbol[index + code - first];
    index += count;
    first = (first + count) << 1;
    code <<= 1;
  }
  return -1;  // not a code
}

static const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
                                      99, 115, 131, 163, 195, 227, 258};
static const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
                                       12, 12, 13, 13};

static bool inflateCodes(Inflate* s, const Huffman* lit, const Huffman* dist) {
  for(;;) {
    int sym = decodeSymbol(s, lit);
    if(sym < 0) return false;
    if(sym < 256) {
      if(s->outPos >= s->outSize) return false;
      s->out[s->outPos++] = (uint8_t) sym;
    } else if(sym == 256) {
      return true;  // the end of the block
    } else {
      sym -= 257;
      if(sym >= 29) return false;
      size_t len = kLenBase[sym] + getBits(s, kLenExtra[sym]);
      int d = decodeSymbol(s, dist);
      if(d < 0 || d >= 30) return false;
      size_t back = kDistBase[d] + getBits(s, kDistExtra[d]);
      if(s->bad || back > s->outPos || len > s->outSize - s->outPos) return false;
      for(size_t i = 0; i < len; i++, s->outPos++) s->out[s->outPos] = s->out[s->outPos - back];
    }
  }
}

static bool inflateStored(Inflate* s) {
  s->bits = 0;  // to the next byte
  s->nbits = 0;
  if(s->inSize - s->inPos < 4) return false;
  const uint8_t* p = s->in + s->inPos;
  unsigned len = p[0] | p[1] << 8, nlen = p[2] | p[3] << 8;
  s->inPos += 4;
  if((len ^ 0xFFFFu) != nlen || len > s->inSize - s->inPos || len > s->outSize - s->outPos) return false;
  memcpy(s->out + s->outPos, s->in + s->inPos, len);
  s->inPos += len;
  s->outPos += len;
  return true;
}

static bool inflateFixed(Inflate* s) {
  static Huffman lit, dist;
  static bool built;
  if(!built) {
    uint8_t l[288];
    for(int i = 0; i < 288; i++) l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
    buildHuffman(&lit, l, 288);
    for(int i = 0; i < 30; i++) l[i] = 5;
    buildHuffman(&dist, l, 30);
    built = true;
  }
  return inflateCodes(s, &lit, &dist);
}

static bool inflateDynamic(Inflate* s) {
  static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  int nlen = (int) getBits(s, 5) + 257, ndist = (int) getBits(s, 5) + 1, ncode = (int) getBits(s, 4) + 4;
  if(s->bad || nlen > 286 || ndist > 30) return false;
  uint8_t lengths[286 + 30];
  memset(lengths, 0, sizeof(lengths));
  for(int i = 0; i < ncode; i++) lengths[kOrder[i]] = (uint8_t) getBits(s, 3);
  Huffman lencode;
  if(s->bad || !buildHuffman(&lencode, lengths, 19)) return false;
  int i = 0;
  while(i < nlen + ndist) {
    int sym = decodeSymbol(s, &lencode);
    if(sym < 0) return false;
    if(sym < 16) {
      lengths[i++] = (uint8_t) sym;
      continue;
    }
    int len = 0, rep;
    if(sym == 16) {
      if(i == 0) return false;
      len = lengths[i - 1];
      rep = 3 + (int) getBits(s, 2);
    } else if(sym == 17) {
      rep = 3 + (int) getBits(s, 3);
    } else {
      rep = 11 + (int) getBits(s, 7);
    }
    if(s->bad || i + rep > nlen + ndist) return false;
    while(rep--) lengths[i++] = (uint8_t) len;
  }
  if(lengths[256] == 0) return false;  // no end-of-block code
  Huffman lit, dist;
  if(!buildHuffman(&lit, lengths, nlen) || !buildHuffman(&dist, lengths + nlen, ndist)) return false;
  return inflateCodes(s, &lit, &dist);
}

bool tg_inflate(const uint8_t* in, size_t inSize, uint8_t* out, size_t outSize) {
  Inflate s;
  memset(&s, 0, sizeof(s));
  s.in = in, s.inSize = inSize, s.out = out, s.outSize = outSize;
  unsigned last;
  do {
    last = getBits(&s, 1);
    unsigned type = getBits(&s, 2);
    if(s.bad) return false;
    bool ok = type == 0 ? inflateStored(&s) : type == 1 ? inflateFixed(&s) : type == 2 ? inflateDynamic(&s) : false;
    if(!ok || s.bad) return false;
  } while(!last);
  return s.outPos == outSize;
}

// --- Zip ----------------------------------------------------------------------------------

static uint16_t rd16(const uint8_t* p) { return (uint16_t) (p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }

static bool safeName(const char* name) {
  if(!name[0] || name[0] == '/' || strchr(name, '\\') || strchr(name, ':')) return false;
  for(const char* p = name; *p; ) {
    const char* slash = strchr(p, '/');
    size_t n = slash ? (size_t) (slash - p) : strlen(p);
    if(n == 2 && p[0] == '.' && p[1] == '.') return false;
    if(!slash) break;
    p = slash + 1;
  }
  return true;
}

#define FAIL(...) do { if(errSize) snprintf(err, errSize, __VA_ARGS__); return false; } while(0)

bool tg_unzip(const uint8_t* zip, size_t size, TgUnzipWrite write, void* user, char* err, size_t errSize) {
  if(size < 22) FAIL("not a zip archive");
  size_t eocd = (size_t) -1;
  for(size_t i = size - 22 + 1; i-- > 0 && size - i <= 22 + 65535;)
    if(rd32(zip + i) == 0x06054b50u) {
      eocd = i;
      break;
    }
  if(eocd == (size_t) -1) FAIL("not a zip archive (no central directory)");
  unsigned entries = rd16(zip + eocd + 10);
  uint32_t cdSize = rd32(zip + eocd + 12), cdOff = rd32(zip + eocd + 16);
  if(cdOff > size || cdSize > size - cdOff) FAIL("damaged zip archive (central directory)");
  size_t p = cdOff;
  for(unsigned e = 0; e < entries; e++) {
    if(size - p < 46 || rd32(zip + p) != 0x02014b50u) FAIL("damaged zip archive (entry %u)", e);
    unsigned flags = rd16(zip + p + 8), method = rd16(zip + p + 10);
    uint32_t crc = rd32(zip + p + 16), csize = rd32(zip + p + 20), usize = rd32(zip + p + 24);
    unsigned nlen = rd16(zip + p + 28), xlen = rd16(zip + p + 30), clen = rd16(zip + p + 32);
    uint32_t local = rd32(zip + p + 42);
    char name[512];
    if(nlen == 0 || nlen >= sizeof(name) || size - p - 46 < nlen) FAIL("damaged zip archive (name of entry %u)", e);
    memcpy(name, zip + p + 46, nlen);
    name[nlen] = 0;
    p += 46 + (size_t) nlen + xlen + clen;
    if(p > size) FAIL("damaged zip archive (entry %u)", e);
    if(flags & 1) FAIL("%s is encrypted", name);
    if(csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu || local == 0xFFFFFFFFu) FAIL("%s needs zip64", name);
    if(!safeName(name)) FAIL("unsafe name in the archive: %s", name);
    if(name[nlen - 1] == '/') continue;  // a folder
    if(local > size || size - local < 30 || rd32(zip + local) != 0x04034b50u) FAIL("damaged zip archive (%s)", name);
    size_t data = local + 30 + (size_t) rd16(zip + local + 26) + rd16(zip + local + 28);
    if(data > size || csize > size - data) FAIL("damaged zip archive (%s)", name);
    uint8_t* buf = malloc(usize ? usize : 1);
    if(buf == NULL) FAIL("out of memory for %s", name);
    bool ok = false;
    if(method == 0) {
      ok = csize == usize;
      if(ok) memcpy(buf, zip + data, usize);
    } else if(method == 8) {
      ok = tg_inflate(zip + data, csize, buf, usize);
    }
    if(!ok) {
      free(buf);
      FAIL(method == 0 || method == 8 ? "damaged file in the archive: %s" : "unsupported compression for %s", name);
    }
    if(tg_crc32(buf, usize) != crc) {
      free(buf);
      FAIL("damaged file in the archive (CRC): %s", name);
    }
    bool written = write(name, buf, usize, user);
    free(buf);
    if(!written) FAIL("couldn't write %s", name);
  }
  return true;
}
