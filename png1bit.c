#include "png1bit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Error handling: the first problem sets reader_t.error (sticky), and from
 * then on every input function returns a harmless value and every loop
 * checks failed() and unwinds normally. No setjmp/longjmp -- an earlier
 * version used them to bail out, and on the device a bad file's bail-out
 * jumped into data instead of back to the decoder. */

/* ===================== input: the file, then the IDAT stream ===================== */

typedef struct {
    const png_source_t *src;
    uint8_t buf[512];
    int len, pos;
    /* bytes left in the current IDAT chunk; when it runs out, the next
     * chunk must be IDAT too or the compressed stream has ended */
    uint32_t idat_left;
    const char *error;
} reader_t;

static void fail(reader_t *r, const char *why) {
    if (r->error == NULL)
        r->error = why;
}

static bool failed(const reader_t *r) {
    return r->error != NULL;
}

/* -1 at end of file or after an error */
static int file_byte(reader_t *r) {
    if (failed(r))
        return -1;
    if (r->pos == r->len) {
        r->len = r->src->read(r->src->ctx, r->buf, (int) sizeof(r->buf));
        r->pos = 0;
        if (r->len < 0) {
            r->len = 0;
            fail(r, "read error");
            return -1;
        }
        if (r->len == 0)
            return -1;
    }
    return r->buf[r->pos++];
}

static uint32_t file_u32(reader_t *r) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        int c = file_byte(r);
        if (c < 0) {
            fail(r, "file is truncated");
            return 0;
        }
        v = (v << 8) | (uint32_t) c;
    }
    return v;
}

static void file_skip(reader_t *r, uint32_t n) {
    while (n-- > 0)
        if (file_byte(r) < 0) {
            fail(r, "file is truncated");
            return;
        }
}

/* Next byte of the zlib stream, which may be split over several IDAT
 * chunks; -1 once the IDATs end (or on error). */
static int idat_byte(reader_t *r) {
    while (r->idat_left == 0) {
        if (failed(r))
            return -1;
        file_skip(r, 4); /* CRC of the chunk just finished */
        uint32_t len = file_u32(r);
        uint32_t type = file_u32(r);
        if (failed(r) || type != 0x49444154u) /* "IDAT" */
            return -1;
        r->idat_left = len;
    }
    int c = file_byte(r);
    if (c < 0) {
        fail(r, "file is truncated");
        return -1;
    }
    r->idat_left--;
    return c;
}

/* ===================== output: rows to pixels ===================== */

typedef struct {
    /* image */
    uint32_t width, height;
    int depth, color, channels;
    int bpp;             /* bytes per complete pixel, for filtering (min 1) */
    uint32_t stride;     /* bytes per row, without the filter byte */
    bool palette_ink[256];
    /* rows */
    uint8_t *cur, *prev;
    uint32_t row, pos;   /* pos 0 = the filter byte comes next */
    uint8_t filter;
    /* destination */
    uint8_t *bits;
    int out_w, out_h, out_stride, ox, oy;
    void (*tick)(void);
    reader_t *r;
} image_t;

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

/* Sample n (0-based) of a row, scaled to 8 bits (palette: the raw index). */
static int sample8(const image_t *im, const uint8_t *row, uint32_t n) {
    switch (im->depth) {
        case 16:
            return row[n * 2];
        case 8:
            return row[n];
        default: {
            uint32_t bit = n * (uint32_t) im->depth;
            int v = (row[bit / 8] >> (8 - im->depth - (int) (bit % 8))) & ((1 << im->depth) - 1);
            return im->color == 3 ? v : v * 255 / ((1 << im->depth) - 1);
        }
    }
}

static bool pixel_ink(const image_t *im, const uint8_t *row, uint32_t x) {
    uint32_t n = x * (uint32_t) im->channels;
    switch (im->color) {
        case 0: /* gray */
            return sample8(im, row, n) < 128;
        case 2: /* RGB */
            return (sample8(im, row, n) + sample8(im, row, n + 1) + sample8(im, row, n + 2)) / 3 < 128;
        case 3: /* palette */
            return im->palette_ink[sample8(im, row, n) & 0xFF];
        case 4: /* gray + alpha */
            return sample8(im, row, n + 1) >= 128 && sample8(im, row, n) < 128;
        default: /* 6, RGBA */
            return sample8(im, row, n + 3) >= 128
                   && (sample8(im, row, n) + sample8(im, row, n + 1) + sample8(im, row, n + 2)) / 3 < 128;
    }
}

static void finish_row(image_t *im) {
    if (im->filter > 4) {
        fail(im->r, "bad row filter");
        return;
    }
    uint8_t *c = im->cur, *p = im->prev;
    int bpp = im->bpp;
    for (uint32_t i = 0; i < im->stride; i++) {
        int a = i >= (uint32_t) bpp ? c[i - bpp] : 0;
        int b = p[i];
        int d = i >= (uint32_t) bpp ? p[i - bpp] : 0;
        switch (im->filter) {
            case 0: break;
            case 1: c[i] = (uint8_t) (c[i] + a); break;
            case 2: c[i] = (uint8_t) (c[i] + b); break;
            case 3: c[i] = (uint8_t) (c[i] + (a + b) / 2); break;
            default: c[i] = (uint8_t) (c[i] + paeth(a, b, d)); break;
        }
    }

    int y = (int) im->row + im->oy;
    if (y >= 0 && y < im->out_h) {
        uint8_t *out = im->bits + y * im->out_stride;
        for (uint32_t x = 0; x < im->width; x++) {
            int px = (int) x + im->ox;
            if (px >= 0 && px < im->out_w && pixel_ink(im, c, x))
                out[px / 8] |= (uint8_t) (0x80 >> (px % 8));
        }
    }

    im->prev = c;
    im->cur = p;
    im->row++;
    if (im->tick != NULL && im->row % 16 == 0)
        im->tick();
}

static void put_byte(image_t *im, uint8_t b) {
    if (im->row >= im->height)
        return; /* trailing data after the last row -- ignore */
    if (im->pos == 0)
        im->filter = b;
    else
        im->cur[im->pos - 1] = b;
    if (++im->pos == im->stride + 1) {
        finish_row(im);
        im->pos = 0;
    }
}

/* ===================== inflate (RFC 1951) ===================== */

#define MAXBITS 15
#define WINDOW  32768u

typedef struct {
    short count[MAXBITS + 1];
    short symbol[288];
} huffman_t;

typedef struct {
    reader_t *r;
    image_t *im;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *window;
    uint32_t wpos;   /* total bytes output; window index = wpos % WINDOW */
    huffman_t lencode, distcode;
} inflate_t;

/* 0 once input runs out or after an error (with the error set) */
static int bits(inflate_t *s, int need) {
    while (s->bitcnt < need) {
        int c = idat_byte(s->r);
        if (c < 0) {
            fail(s->r, "compressed data ends early");
            return 0;
        }
        s->bitbuf |= (uint32_t) c << s->bitcnt;
        s->bitcnt += 8;
    }
    int v = (int) (s->bitbuf & ((1u << need) - 1));
    s->bitbuf >>= need;
    s->bitcnt -= need;
    return v;
}

static void out(inflate_t *s, uint8_t b) {
    s->window[s->wpos % WINDOW] = b;
    s->wpos++;
    put_byte(s->im, b);
}

/* Canonical Huffman table from code lengths. Over-subscribed sets are an
 * error; incomplete ones are allowed (deflate permits them for single
 * distance codes). */
static void construct(inflate_t *s, huffman_t *h, const short *length, int n) {
    short offs[MAXBITS + 1];
    memset(h->count, 0, sizeof(h->count));
    for (int i = 0; i < n; i++)
        h->count[length[i]]++;
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) {
            fail(s->r, "bad compressed data");
            return;
        }
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++)
        offs[len + 1] = (short) (offs[len] + h->count[len]);
    for (int i = 0; i < n; i++)
        if (length[i] != 0)
            h->symbol[offs[length[i]]++] = (short) i;
}

/* -1 on error */
static int decode(inflate_t *s, const huffman_t *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= bits(s, 1);
        if (failed(s->r))
            return -1;
        int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    fail(s->r, "bad compressed data");
    return -1;
}

static void stored(inflate_t *s) {
    s->bitbuf = 0; /* to a byte boundary */
    s->bitcnt = 0;
    int len = bits(s, 16);
    int nlen = bits(s, 16);
    if (failed(s->r))
        return;
    if (len != (~nlen & 0xFFFF)) {
        fail(s->r, "bad compressed data");
        return;
    }
    while (len-- > 0 && !failed(s->r))
        out(s, (uint8_t) bits(s, 8));
}

static void codes(inflate_t *s) {
    static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                   3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
                                    513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                   7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    while (!failed(s->r)) {
        int sym = decode(s, &s->lencode);
        if (sym < 0)
            return;
        if (sym < 256) {
            out(s, (uint8_t) sym);
            continue;
        }
        if (sym == 256)
            return;
        sym -= 257;
        if (sym >= 29) {
            fail(s->r, "bad compressed data");
            return;
        }
        int len = lbase[sym] + bits(s, lext[sym]);
        int dsym = decode(s, &s->distcode);
        if (dsym < 0)
            return;
        if (dsym >= 30) {
            fail(s->r, "bad compressed data");
            return;
        }
        uint32_t dist = (uint32_t) (dbase[dsym] + bits(s, dext[dsym]));
        if (failed(s->r))
            return;
        if (dist > s->wpos) {
            fail(s->r, "bad compressed data");
            return;
        }
        while (len-- > 0)
            out(s, s->window[(s->wpos - dist) % WINDOW]);
    }
}

static void fixed(inflate_t *s) {
    short lengths[288];
    int i = 0;
    for (; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    construct(s, &s->lencode, lengths, 288);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    construct(s, &s->distcode, lengths, 30);
    if (!failed(s->r))
        codes(s);
}

static void dynamic(inflate_t *s) {
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[320];
    int nlen = bits(s, 5) + 257;
    int ndist = bits(s, 5) + 1;
    int ncode = bits(s, 4) + 4;
    if (failed(s->r))
        return;
    if (nlen > 286 || ndist > 30) {
        fail(s->r, "bad compressed data");
        return;
    }

    int i;
    for (i = 0; i < ncode; i++) lengths[order[i]] = (short) bits(s, 3);
    for (; i < 19; i++) lengths[order[i]] = 0;
    construct(s, &s->lencode, lengths, 19);

    i = 0;
    while (i < nlen + ndist && !failed(s->r)) {
        int sym = decode(s, &s->lencode);
        if (sym < 0)
            return;
        if (sym < 16) {
            lengths[i++] = (short) sym;
            continue;
        }
        int len = 0, rep;
        if (sym == 16) {
            if (i == 0) {
                fail(s->r, "bad compressed data");
                return;
            }
            len = lengths[i - 1];
            rep = 3 + bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + bits(s, 3);
        } else {
            rep = 11 + bits(s, 7);
        }
        if (i + rep > nlen + ndist) {
            fail(s->r, "bad compressed data");
            return;
        }
        while (rep-- > 0)
            lengths[i++] = (short) len;
    }
    if (failed(s->r))
        return;
    if (lengths[256] == 0) {
        fail(s->r, "bad compressed data");
        return;
    }
    construct(s, &s->lencode, lengths, nlen);
    construct(s, &s->distcode, lengths + nlen, ndist);
    if (!failed(s->r))
        codes(s);
}

/* ===================== PNG ===================== */

/* Reads the chunks up to the first IDAT (header, palette, transparency).
 * Leaves r positioned at the IDAT data. */
static void read_header(reader_t *r, image_t *im) {
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    for (int i = 0; i < 8; i++)
        if (file_byte(r) != signature[i]) {
            fail(r, "not a PNG file");
            return;
        }

    bool have_header = false;
    uint8_t palette_alpha[256], palette_gray[256];
    memset(palette_alpha, 0xFF, sizeof(palette_alpha));
    memset(palette_gray, 0xFF, sizeof(palette_gray));
    while (!failed(r)) {
        uint32_t len = file_u32(r);
        uint32_t type = file_u32(r);
        if (failed(r))
            return;
        if (type == 0x49484452u) { /* IHDR */
            if (len != 13) {
                fail(r, "bad PNG header");
                return;
            }
            im->width = file_u32(r);
            im->height = file_u32(r);
            im->depth = file_byte(r);
            im->color = file_byte(r);
            int compression = file_byte(r), filter = file_byte(r), interlace = file_byte(r);
            if (failed(r) || interlace < 0) {
                fail(r, "file is truncated");
                return;
            }
            if (compression != 0 || filter != 0) {
                fail(r, "unknown PNG compression");
                return;
            }
            if (interlace != 0) {
                fail(r, "interlaced PNG -- save it non-interlaced");
                return;
            }
            if (im->width == 0 || im->height == 0 || im->width > 4096 || im->height > 4096) {
                fail(r, "image too large (max 4096x4096)");
                return;
            }
            static const int channels_for[7] = {1, 0, 3, 1, 2, 0, 4};
            if (im->color < 0 || im->color > 6 || channels_for[im->color] == 0) {
                fail(r, "unknown PNG color type");
                return;
            }
            im->channels = channels_for[im->color];
            bool depth_ok = im->depth == 8 || im->depth == 16
                            || ((im->color == 0 || im->color == 3)
                                && (im->depth == 1 || im->depth == 2 || im->depth == 4));
            if (!depth_ok || (im->color == 3 && im->depth == 16)) {
                fail(r, "unknown PNG bit depth");
                return;
            }
            have_header = true;
            file_skip(r, 4);
        } else if (type == 0x504C5445u) { /* PLTE */
            if (len % 3 != 0 || len > 768) {
                fail(r, "bad PNG palette");
                return;
            }
            for (uint32_t i = 0; i < len / 3; i++) {
                int red = file_byte(r), green = file_byte(r), blue = file_byte(r);
                palette_gray[i] = (uint8_t) ((red + green + blue) / 3);
            }
            file_skip(r, 4);
        } else if (type == 0x74524E53u && im->color == 3) { /* tRNS, palette alpha */
            for (uint32_t i = 0; i < len && !failed(r); i++) {
                int a = file_byte(r);
                if (i < 256)
                    palette_alpha[i] = (uint8_t) a;
            }
            file_skip(r, 4);
        } else if (type == 0x49444154u) { /* IDAT */
            if (!have_header) {
                fail(r, "bad PNG header");
                return;
            }
            r->idat_left = len;
            for (int i = 0; i < 256; i++)
                im->palette_ink[i] = palette_alpha[i] >= 128 && palette_gray[i] < 128;
            return;
        } else if (type == 0x49454E44u) { /* IEND */
            fail(r, "no image data");
            return;
        } else {
            file_skip(r, len + 4); /* other chunk, and its CRC */
        }
    }
}

bool png_decode_1bit(const png_source_t *src, uint8_t *bits_out, int width, int height, int stride,
                     char *error, size_t error_size) {
    static reader_t r;
    static image_t im;
    static inflate_t s;
    memset(&r, 0, sizeof(r));
    memset(&im, 0, sizeof(im));
    memset(&s, 0, sizeof(s));
    r.src = src;
    uint8_t *row_mem = NULL;

    read_header(&r, &im);

    if (!failed(&r)) {
        uint32_t bits_per_pixel = (uint32_t) (im.channels * im.depth);
        im.stride = (im.width * bits_per_pixel + 7) / 8;
        im.bpp = bits_per_pixel >= 8 ? (int) (bits_per_pixel / 8) : 1;
        row_mem = (uint8_t *) calloc(2, im.stride);
        s.window = (uint8_t *) malloc(WINDOW);
        if (row_mem == NULL || s.window == NULL)
            fail(&r, "out of memory");
    }

    if (!failed(&r)) {
        im.cur = row_mem;
        im.prev = row_mem + im.stride;
        im.bits = bits_out;
        im.out_w = width;
        im.out_h = height;
        im.out_stride = stride;
        im.ox = (width - (int) im.width) / 2;
        im.oy = (height - (int) im.height) / 2;
        im.tick = src->tick;
        im.r = &r;
        memset(bits_out, 0, (size_t) stride * (size_t) height);

        /* zlib wrapper, then deflate blocks. */
        s.r = &r;
        s.im = &im;
        int cmf = bits(&s, 8), flg = bits(&s, 8);
        if (!failed(&r) && ((cmf & 0x0F) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20)))
            fail(&r, "bad compressed data");
        int last = 0;
        while (!failed(&r) && !last && im.row < im.height) {
            last = bits(&s, 1);
            int type = bits(&s, 2);
            if (failed(&r))
                break;
            if (type == 0)
                stored(&s);
            else if (type == 1)
                fixed(&s);
            else if (type == 2)
                dynamic(&s);
            else
                fail(&r, "bad compressed data");
        }
        if (!failed(&r) && im.row < im.height)
            fail(&r, "image data ends early");
    }

    free(s.window);
    free(row_mem);
    if (failed(&r)) {
        snprintf(error, error_size, "%s", r.error);
        return false;
    }
    return true;
}
