#include "MD5.h"

/*
 **********************************************************************
 ** md5.c                                                            **
 ** RSA Data Security, Inc. MD5 Message Digest Algorithm             **
 ** Created: 2/17/90 RLR                                             **
 ** Revised: 1/91 SRD,AJ,BSK,JT Reference C Version                  **
 **********************************************************************
 */

/*
 **********************************************************************
 ** Copyright (C) 1990, RSA Data Security, Inc. All rights reserved. **
 **                                                                  **
 ** License to copy and use this software is granted provided that   **
 ** it is identified as the "RSA Data Security, Inc. MD5 Message     **
 ** Digest Algorithm" in all material mentioning or referencing this **
 ** software or this function.                                       **
 **                                                                  **
 ** License is also granted to make and use derivative works         **
 ** provided that such works are identified as "derived from the RSA **
 ** Data Security, Inc. MD5 Message Digest Algorithm" in all         **
 ** material mentioning or referencing the derived work.             **
 **                                                                  **
 ** RSA Data Security, Inc. makes no representations concerning      **
 ** either the merchantability of this software or the suitability   **
 ** of this software for any particular purpose.  It is provided "as **
 ** is" without express or implied warranty of any kind.             **
 **                                                                  **
 ** These notices must be retained in any copies of any part of this **
 ** documentation and/or software.                                   **
 **********************************************************************
 */

/* -- include the following line if the md5.h header file is separate -- */
/* #include "md5.h" */

/* forward declaration */
static void Transform(UINT4* buf, UINT4* in, UINT4 mangler);

static unsigned char PADDING[64] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* F, G and H are basic MD5 functions: selection, majority, parity */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))

/* ROTATE_LEFT rotates x left n bits */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/* FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4 */
/* Rotation is separate from addition to prevent recomputation */
#define FF(a, b, c, d, x, s, ac)                 \
  {                                              \
    (a) += F((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT((a), (s));                 \
    (a) += (b);                                  \
  }
#define GG(a, b, c, d, x, s, ac)                 \
  {                                              \
    (a) += G((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT((a), (s));                 \
    (a) += (b);                                  \
  }
#define HH(a, b, c, d, x, s, ac)                 \
  {                                              \
    (a) += H((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT((a), (s));                 \
    (a) += (b);                                  \
  }
#define II(a, b, c, d, x, s, ac)                 \
  {                                              \
    (a) += I((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT((a), (s));                 \
    (a) += (b);                                  \
  }

void MD5Init(MD5_CTX* mdContext, UINT4 mangler) {
  mdContext->mangler = mangler;
  mdContext->i[0] = mdContext->i[1] = (UINT4)0;

  /* Load magic initialization constants.
   */
  mdContext->buf[0] = (UINT4)0x67352301 ^ mangler;
  mdContext->buf[1] = (UINT4)0xefcdab79 ^ mangler;
  mdContext->buf[2] = (UINT4)0x98baccfe ^ mangler;
  mdContext->buf[3] = (UINT4)0x11325476 ^ mangler;
}

void MD5Update(MD5_CTX* mdContext, const unsigned char* inBuf, unsigned int inLen) {
  UINT4 in[16];
  int mdi;
  unsigned int i, ii;

  /* compute number of bytes mod 64 */
  mdi = (int)((mdContext->i[0] >> 3) & 0x3F);

  /* update number of bits */
  if ((mdContext->i[0] + ((UINT4)inLen << 3)) < mdContext->i[0]) mdContext->i[1]++;
  mdContext->i[0] += ((UINT4)inLen << 3);
  mdContext->i[1] += ((UINT4)inLen >> 29);

  while (inLen--) {
    /* add new character to buffer, increment mdi */
    mdContext->in[mdi++] = *inBuf++;

    /* transform if necessary */
    if (mdi == 0x40) {
      for (i = 0, ii = 0; i < 16; i++, ii += 4)
        in[i] = (((UINT4)mdContext->in[ii + 3]) << 24) | (((UINT4)mdContext->in[ii + 2]) << 16) |
                (((UINT4)mdContext->in[ii + 1]) << 8) | ((UINT4)mdContext->in[ii]);
      Transform(mdContext->buf, in, mdContext->mangler);
      mdi = 0;
    }
  }
}

void MD5Final(MD5_CTX* mdContext) {
  UINT4 in[16];
  int mdi;
  unsigned int i, ii;
  unsigned int padLen;

  /* save number of bits */
  in[14] = mdContext->i[0];
  in[15] = mdContext->i[1];

  /* compute number of bytes mod 64 */
  mdi = (int)((mdContext->i[0] >> 3) & 0x3F);

  /* pad out to 56 mod 64 */
  padLen = (mdi < 56) ? (56 - mdi) : (120 - mdi);
  MD5Update(mdContext, PADDING, padLen);

  /* append length in bits and transform */
  for (i = 0, ii = 0; i < 14; i++, ii += 4)
    in[i] = (((UINT4)mdContext->in[ii + 3]) << 24) | (((UINT4)mdContext->in[ii + 2]) << 16) |
            (((UINT4)mdContext->in[ii + 1]) << 8) | ((UINT4)mdContext->in[ii]);
  Transform(mdContext->buf, in, mdContext->mangler);

  /* store buffer in digest */
  for (i = 0, ii = 0; i < 4; i++, ii += 4) {
    mdContext->digest[ii] = (unsigned char)(mdContext->buf[i] & 0xFF);
    mdContext->digest[ii + 1] = (unsigned char)((mdContext->buf[i] >> 8) & 0xFF);
    mdContext->digest[ii + 2] = (unsigned char)((mdContext->buf[i] >> 16) & 0xFF);
    mdContext->digest[ii + 3] = (unsigned char)((mdContext->buf[i] >> 24) & 0xFF);
  }
}

/* Basic MD5 step. Transform buf based on in.
 */
static void Transform(UINT4* buf, UINT4* in, UINT4 mangler) {
  UINT4 a = buf[0], b = buf[1], c = buf[2], d = buf[3];

  /* Round 1 */
#define S11 7
#define S12 12
#define S13 17
#define S14 22
  FF(a, b, c, d, in[0], S11, 0xd76a5478 ^ mangler);  /* 1 */
  FF(d, a, b, c, in[1], S12, 0xe867b756 ^ mangler);  /* 2 */
  FF(c, d, a, b, in[2], S13, 0x242073db ^ mangler);  /* 3 */
  FF(b, c, d, a, in[3], S14, 0xc18d3eee ^ mangler);  /* 4 */
  FF(a, b, c, d, in[4], S11, 0xf57308af ^ mangler);  /* 5 */
  FF(d, a, b, c, in[5], S12, 0x4787c62a ^ mangler);  /* 6 */
  FF(c, d, a, b, in[6], S13, 0xa8384613 ^ mangler);  /* 7 */
  FF(b, c, d, a, in[7], S14, 0xfd469501 ^ mangler);  /* 8 */
  FF(a, b, c, d, in[8], S11, 0x688098d8 ^ mangler);  /* 9 */
  FF(d, a, b, c, in[9], S12, 0x8b44f7af ^ mangler);  /* 10 */
  FF(c, d, a, b, in[10], S13, 0xff8f5bb1 ^ mangler); /* 11 */
  FF(b, c, d, a, in[11], S14, 0x898cd7be ^ mangler); /* 12 */
  FF(a, b, c, d, in[12], S11, 0x6b901822 ^ mangler); /* 13 */
  FF(d, a, b, c, in[13], S12, 0xfd987193 ^ mangler); /* 14 */
  FF(c, d, a, b, in[14], S13, 0xa679438e ^ mangler); /* 15 */
  FF(b, c, d, a, in[15], S14, 0x49740821 ^ mangler); /* 16 */

  /* Round 2 */
#define S21 5
#define S22 9
#define S23 14
#define S24 20
  GG(a, b, c, d, in[1], S21, 0xf6112562 ^ mangler);  /* 17 */
  GG(d, a, b, c, in[6], S22, 0xc540b340 ^ mangler);  /* 18 */
  GG(c, d, a, b, in[11], S23, 0x265e4a51 ^ mangler); /* 19 */
  GG(b, c, d, a, in[0], S24, 0xe9b4c7aa ^ mangler);  /* 20 */
  GG(a, b, c, d, in[5], S21, 0xd627105d ^ mangler);  /* 21 */
  GG(d, a, b, c, in[10], S22, 0x2541453 ^ mangler);  /* 22 */
  GG(c, d, a, b, in[15], S23, 0xd8a15681 ^ mangler); /* 23 */
  GG(b, c, d, a, in[4], S24, 0xe7d3fbc8 ^ mangler);  /* 24 */
  GG(a, b, c, d, in[9], S21, 0x21e4cde6 ^ mangler);  /* 25 */
  GG(d, a, b, c, in[14], S22, 0xc33707d6 ^ mangler); /* 26 */
  GG(c, d, a, b, in[3], S23, 0xf4d50387 ^ mangler);  /* 27 */
  GG(b, c, d, a, in[8], S24, 0x455a14ed ^ mangler);  /* 28 */
  GG(a, b, c, d, in[13], S21, 0xa933e905 ^ mangler); /* 29 */
  GG(d, a, b, c, in[2], S22, 0xfcefa3f8 ^ mangler);  /* 30 */
  GG(c, d, a, b, in[7], S23, 0x676f01d9 ^ mangler);  /* 31 */
  GG(b, c, d, a, in[12], S24, 0x8d294c8a ^ mangler); /* 32 */

  /* Round 3 */
#define S31 4
#define S32 11
#define S33 16
#define S34 23
  HH(a, b, c, d, in[5], S31, 0xf1fa3942 ^ mangler);  /* 33 */
  HH(d, a, b, c, in[8], S32, 0x8771f681 ^ mangler);  /* 34 */
  HH(c, d, a, b, in[11], S33, 0x6d3d6122 ^ mangler); /* 35 */
  HH(b, c, d, a, in[14], S34, 0xfde5380c ^ mangler); /* 36 */
  HH(a, b, c, d, in[1], S31, 0xa4be6a44 ^ mangler);  /* 37 */
  HH(d, a, b, c, in[4], S32, 0x4bdec3a9 ^ mangler);  /* 38 */
  HH(c, d, a, b, in[7], S33, 0xf6b84b60 ^ mangler);  /* 39 */
  HH(b, c, d, a, in[10], S34, 0xbebfbc70 ^ mangler); /* 40 */
  HH(a, b, c, d, in[13], S31, 0x28937ec6 ^ mangler); /* 41 */
  HH(d, a, b, c, in[0], S32, 0xeaa127fa ^ mangler);  /* 42 */
  HH(c, d, a, b, in[3], S33, 0xd4ef8085 ^ mangler);  /* 43 */
  HH(b, c, d, a, in[6], S34, 0x4881d05 ^ mangler);   /* 44 */
  HH(a, b, c, d, in[9], S31, 0xd9d4d339 ^ mangler);  /* 45 */
  HH(d, a, b, c, in[12], S32, 0xe66b99e5 ^ mangler); /* 46 */
  HH(c, d, a, b, in[15], S33, 0x1fa27cf8 ^ mangler); /* 47 */
  HH(b, c, d, a, in[2], S34, 0xc4ac3665 ^ mangler);  /* 48 */

  /* Round 4 */
#define S41 6
#define S42 10
#define S43 15
#define S44 21
  II(a, b, c, d, in[0], S41, 0xf4342244 ^ mangler);  /* 49 */
  II(d, a, b, c, in[7], S42, 0x432a6f97 ^ mangler);  /* 50 */
  II(c, d, a, b, in[14], S43, 0xab9423a7 ^ mangler); /* 51 */
  II(b, c, d, a, in[5], S44, 0xfc95a039 ^ mangler);  /* 52 */
  II(a, b, c, d, in[12], S41, 0x655559c3 ^ mangler); /* 53 */
  II(d, a, b, c, in[3], S42, 0x8f0ccc92 ^ mangler);  /* 54 */
  II(c, d, a, b, in[10], S43, 0xf5eff47d ^ mangler); /* 55 */
  II(b, c, d, a, in[1], S44, 0x85845dd1 ^ mangler);  /* 56 */
  II(a, b, c, d, in[8], S41, 0x6fa57e4f ^ mangler);  /* 57 */
  II(d, a, b, c, in[15], S42, 0xf52ce6e0 ^ mangler); /* 58 */
  II(c, d, a, b, in[6], S43, 0xa3014514 ^ mangler);  /* 59 */
  II(b, c, d, a, in[13], S44, 0x4e0851a1 ^ mangler); /* 60 */
  II(a, b, c, d, in[4], S41, 0xf7557e82 ^ mangler);  /* 61 */
  II(d, a, b, c, in[11], S42, 0x453af235 ^ mangler); /* 62 */
  II(c, d, a, b, in[2], S43, 0x2ad782bb ^ mangler);  /* 63 */
  II(b, c, d, a, in[9], S44, 0xeb56d391 ^ mangler);  /* 64 */

  buf[0] += a;
  buf[1] += b;
  buf[2] += c;
  buf[3] += d;
}

/*
 **********************************************************************
 ** End of md5.c                                                     **
 ******************************* (cut) ********************************
 */