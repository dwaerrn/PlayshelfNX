#include "lib/inflate.hpp"

// Small DEFLATE decoder (RFC 1951) based on the structure of Mark Adler's "puff"
// (zlib license). Used to read the compressed title block of control.nacp
// without needing an extra library.
namespace lib::inflate {
    namespace {
        const int MAXBITS = 15;
        const int MAXLCODES = 286;
        const int MAXDCODES = 30;
        const int FIXLCODES = 288;

        struct State {
            uint8_t * out;
            size_t outlen;
            size_t outcnt;
            const uint8_t * in;
            size_t inlen;
            size_t incnt;
            int bitbuf;
            int bitcnt;
        };

        struct Huffman {
            short * count;   // number of symbols of each length
            short * symbol;  // canonically ordered symbols
        };

        // Returns the next "need" bits, or -1 when the input is exhausted
        int bits(State * s, int need) {
            long val = s->bitbuf;
            while (s->bitcnt < need) {
                if (s->incnt == s->inlen) {
                    return -1;
                }
                val |= static_cast<long>(s->in[s->incnt++]) << s->bitcnt;
                s->bitcnt += 8;
            }
            s->bitbuf = static_cast<int>(val >> need);
            s->bitcnt -= need;
            return static_cast<int>(val & ((1L << need) - 1));
        }

        int stored(State * s) {
            s->bitbuf = 0;
            s->bitcnt = 0;
            if (s->incnt + 4 > s->inlen) {
                return 2;
            }
            unsigned len = s->in[s->incnt++];
            len |= static_cast<unsigned>(s->in[s->incnt++]) << 8;
            if (s->in[s->incnt++] != (~len & 0xff) || s->in[s->incnt++] != ((~len >> 8) & 0xff)) {
                return -2;
            }
            if (s->incnt + len > s->inlen) {
                return 2;
            }
            if (s->outcnt + len > s->outlen) {
                return 1;
            }
            while (len--) {
                s->out[s->outcnt++] = s->in[s->incnt++];
            }
            return 0;
        }

        int decode(State * s, const Huffman * h) {
            int len = 1;
            int code = 0;
            int first = 0;
            int index = 0;
            int bitbuf = s->bitbuf;
            int left = s->bitcnt;
            const short * next = h->count + 1;
            while (true) {
                while (left--) {
                    code |= bitbuf & 1;
                    bitbuf >>= 1;
                    int count = *next++;
                    if (code - count < first) {
                        s->bitbuf = bitbuf;
                        s->bitcnt = (s->bitcnt - len) & 7;
                        return h->symbol[index + (code - first)];
                    }
                    index += count;
                    first += count;
                    first <<= 1;
                    code <<= 1;
                    len++;
                }
                left = (MAXBITS + 1) - len;
                if (left == 0) {
                    break;
                }
                if (s->incnt == s->inlen) {
                    return -10;
                }
                bitbuf = s->in[s->incnt++];
                if (left > 8) {
                    left = 8;
                }
            }
            return -9;
        }

        int construct(Huffman * h, const short * length, int n) {
            short offs[MAXBITS + 1];
            for (int len = 0; len <= MAXBITS; len++) {
                h->count[len] = 0;
            }
            for (int symbol = 0; symbol < n; symbol++) {
                h->count[length[symbol]]++;
            }
            if (h->count[0] == n) {
                return 0;
            }

            // Check for an over-subscribed or incomplete set of lengths
            int left = 1;
            for (int len = 1; len <= MAXBITS; len++) {
                left <<= 1;
                left -= h->count[len];
                if (left < 0) {
                    return left;
                }
            }

            offs[1] = 0;
            for (int len = 1; len < MAXBITS; len++) {
                offs[len + 1] = offs[len] + h->count[len];
            }
            for (int symbol = 0; symbol < n; symbol++) {
                if (length[symbol] != 0) {
                    h->symbol[offs[length[symbol]]++] = static_cast<short>(symbol);
                }
            }
            return left;
        }

        int codes(State * s, const Huffman * lencode, const Huffman * distcode) {
            static const short lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
            static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
            static const short dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
            static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

            int symbol;
            do {
                symbol = decode(s, lencode);
                if (symbol < 0) {
                    return symbol;
                }
                if (symbol < 256) {
                    if (s->outcnt == s->outlen) {
                        return 1;
                    }
                    s->out[s->outcnt++] = static_cast<uint8_t>(symbol);
                } else if (symbol > 256) {
                    symbol -= 257;
                    if (symbol >= 29) {
                        return -10;
                    }
                    int extra = bits(s, lext[symbol]);
                    if (extra < 0) {
                        return -1;
                    }
                    int len = lens[symbol] + extra;

                    symbol = decode(s, distcode);
                    if (symbol < 0) {
                        return symbol;
                    }
                    extra = bits(s, dext[symbol]);
                    if (extra < 0) {
                        return -1;
                    }
                    size_t dist = static_cast<size_t>(dists[symbol] + extra);
                    if (dist > s->outcnt) {
                        return -11;
                    }
                    if (s->outcnt + static_cast<size_t>(len) > s->outlen) {
                        return 1;
                    }
                    while (len--) {
                        s->out[s->outcnt] = s->out[s->outcnt - dist];
                        s->outcnt++;
                    }
                }
            } while (symbol != 256);
            return 0;
        }

        int fixedBlock(State * s) {
            short lencnt[MAXBITS + 1];
            short lensym[FIXLCODES];
            short distcnt[MAXBITS + 1];
            short distsym[MAXDCODES];
            short lengths[FIXLCODES];
            int symbol;
            for (symbol = 0; symbol < 144; symbol++) { lengths[symbol] = 8; }
            for (; symbol < 256; symbol++) { lengths[symbol] = 9; }
            for (; symbol < 280; symbol++) { lengths[symbol] = 7; }
            for (; symbol < FIXLCODES; symbol++) { lengths[symbol] = 8; }
            Huffman lencode = {lencnt, lensym};
            construct(&lencode, lengths, FIXLCODES);

            for (symbol = 0; symbol < MAXDCODES; symbol++) { lengths[symbol] = 5; }
            Huffman distcode = {distcnt, distsym};
            construct(&distcode, lengths, MAXDCODES);

            return codes(s, &lencode, &distcode);
        }

        int dynamicBlock(State * s) {
            static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            short lengths[MAXLCODES + MAXDCODES];
            short lencnt[MAXBITS + 1];
            short lensym[MAXLCODES];
            short distcnt[MAXBITS + 1];
            short distsym[MAXDCODES];
            Huffman lencode = {lencnt, lensym};
            Huffman distcode = {distcnt, distsym};

            int nlen = bits(s, 5);
            int ndist = bits(s, 5);
            int ncode = bits(s, 4);
            if (nlen < 0 || ndist < 0 || ncode < 0) {
                return -1;
            }
            nlen += 257;
            ndist += 1;
            ncode += 4;
            if (nlen > MAXLCODES || ndist > MAXDCODES) {
                return -3;
            }

            int index;
            for (index = 0; index < ncode; index++) {
                int v = bits(s, 3);
                if (v < 0) {
                    return -1;
                }
                lengths[order[index]] = static_cast<short>(v);
            }
            for (; index < 19; index++) {
                lengths[order[index]] = 0;
            }

            int err = construct(&lencode, lengths, 19);
            if (err != 0) {
                return -4;
            }

            index = 0;
            while (index < nlen + ndist) {
                int symbol = decode(s, &lencode);
                if (symbol < 0) {
                    return symbol;
                }
                if (symbol < 16) {
                    lengths[index++] = static_cast<short>(symbol);
                } else {
                    int len = 0;
                    int rep;
                    if (symbol == 16) {
                        if (index == 0) {
                            return -5;
                        }
                        len = lengths[index - 1];
                        rep = bits(s, 2);
                        if (rep < 0) { return -1; }
                        rep += 3;
                    } else if (symbol == 17) {
                        rep = bits(s, 3);
                        if (rep < 0) { return -1; }
                        rep += 3;
                    } else {
                        rep = bits(s, 7);
                        if (rep < 0) { return -1; }
                        rep += 11;
                    }
                    if (index + rep > nlen + ndist) {
                        return -6;
                    }
                    while (rep--) {
                        lengths[index++] = static_cast<short>(len);
                    }
                }
            }

            if (lengths[256] == 0) {
                return -9;
            }

            err = construct(&lencode, lengths, nlen);
            if (err != 0 && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) {
                return -7;
            }
            err = construct(&distcode, lengths + nlen, ndist);
            if (err != 0 && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) {
                return -8;
            }
            return codes(s, &lencode, &distcode);
        }
    };

    bool raw(const uint8_t * in, size_t inLen, uint8_t * out, size_t outCap, size_t * outLen) {
        State s;
        s.out = out;
        s.outlen = outCap;
        s.outcnt = 0;
        s.in = in;
        s.inlen = inLen;
        s.incnt = 0;
        s.bitbuf = 0;
        s.bitcnt = 0;

        int last;
        int err = 0;
        do {
            last = bits(&s, 1);
            int type = bits(&s, 2);
            if (last < 0 || type < 0) {
                err = -1;
                break;
            }
            if (type == 0) {
                err = stored(&s);
            } else if (type == 1) {
                err = fixedBlock(&s);
            } else if (type == 2) {
                err = dynamicBlock(&s);
            } else {
                err = -1;
            }
            if (err != 0) {
                break;
            }
        } while (!last);

        if (outLen != nullptr) {
            *outLen = s.outcnt;
        }
        return err == 0;
    }
};
