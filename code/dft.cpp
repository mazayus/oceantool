/*
 * Copyright 2017 Milan Izai <milan.izai@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "common.h"
#include "dft.h"
#include "math.h"

#include <x86intrin.h>

// TODO: There's a lot of copy-paste happening in this file, e.g. DFTs and IDFTs differ by only a few characters.
// Factor common code into helper functions.

static inline bool IsPowerOf2(unsigned int n)
{
    return (n != 0) && !(n & (n - 1));
}

static inline int GetPowerOf2(unsigned int n)
{
    int p = -1;

    while (n)
    {
        n >>= 1;
        ++p;
    }

    return p;
}

static inline uint32_t BitReverse(uint32_t n, int bits)
{
    n = ((n & 0xFFFF0000) >> 16) | ((n & 0x0000FFFF) << 16);
    n = ((n & 0xFF00FF00) >>  8) | ((n & 0x00FF00FF) <<  8);
    n = ((n & 0xF0F0F0F0) >>  4) | ((n & 0x0F0F0F0F) <<  4);
    n = ((n & 0xCCCCCCCC) >>  2) | ((n & 0x33333333) <<  2);
    n = ((n & 0xAAAAAAAA) >>  1) | ((n & 0x55555555) <<  1);

    return n >> (32 - bits);
}

template <typename T>
static void Transpose(const T* in, T* out, int N1, int N2)
{
    for (int n1 = 0; n1 < N1; ++n1)
        for (int n2 = 0; n2 < N2; ++n2)
            out[n2 * N1 + n1] = in[n1 * N2 + n2];
}

//
// DFTs
//

void DFT1D_scalar(const complex64* in, complex64* out, int N)
{
    assert(N > 1);

    assert(IsPowerOf2(N));

    int p = GetPowerOf2(N);
    for (int i = 0; i < N; ++i)
        out[BitReverse(i, p)] = in[i];

    if (p >= 1)
    {
        for (int k = 0; k < N; k += 2)
        {
            complex64 x0 = out[k];
            complex64 x1 = out[k+1];

            out[k]   = x0 + x1;
            out[k+1] = x0 - x1;
        }
    }

    if (p >= 2)
    {
        for (int k = 0; k < N; k += 4)
        {
            complex64 x0 = out[k];
            complex64 x2 = out[k+2];
            complex64 x2_w = complex64(x2.real(), x2.imag()); // x2 * W

            out[k]   = x0 + x2_w;
            out[k+2] = x0 - x2_w;

            complex64 x1 = out[k+1];
            complex64 x3 = out[k+3];
            complex64 x3_w = complex64(x3.imag(), -x3.real()); // x3 * W

            out[k+1] = x1 + x3_w;
            out[k+3] = x1 - x3_w;
        }
    }

    for (int s = 3; s <= p; ++s)
    {
        int m = 1 << s;

        const complex64 Wm = std::exp(complex64(0, - 2 * Math::PI / m));

        for (int k = 0; k < N; k += m)
        {
            complex64 W = complex64(1, 0);

            for (int j = 0; j < m/2; ++j)
            {
                complex64 x0 = out[k+j];
                complex64 x1 = out[k+j+m/2] * W;

                out[k+j] = x0 + x1;
                out[k+j+m/2] = x0 - x1;

                W *= Wm;
            }
        }
    }
}

void DFT2D_scalar(const complex64* in, complex64* out, int N1, int N2)
{
    complex64* aux = (complex64*) new complex64[N1*N2];

    for (int n1 = 0; n1 < N1; ++n1)
        DFT1D_scalar(in + n1*N2, aux + n1*N2, N2);

    Transpose(aux, out, N1, N2);

    for (int n2 = 0; n2 < N2; ++n2)
        DFT1D_scalar(out + n2*N1, aux + n2*N1, N1);

    Transpose(aux, out, N2, N1);

    delete[] aux;
}

void DFT1D_sse(const complex64* in, complex64* out, int N)
{
    assert(N > 1);

    assert(IsPowerOf2(N));

    int p = GetPowerOf2(N);
    for (int i = 0; i < N; ++i)
        out[BitReverse(i, p)] = in[i];

    if (p >= 1)
    {
        for (int k = 0; k < N; k += 2)
        {
            __m128d x0 = _mm_loadu_pd((const double*) &out[k]);
            __m128d x1 = _mm_loadu_pd((const double*) &out[k+1]);

            __m128d y0 = _mm_add_pd(x0, x1);
            __m128d y1 = _mm_sub_pd(x0, x1);

            _mm_storeu_pd((double*) &out[k],   y0);
            _mm_storeu_pd((double*) &out[k+1], y1);
        }
    }

    if (p >= 2)
    {
        for (int k = 0; k < N; k += 4)
        {
            __m128d x0 = _mm_loadu_pd((const double*) &out[k]);
            __m128d x2 = _mm_loadu_pd((const double*) &out[k+2]);

            __m128d y0 = _mm_add_pd(x0, x2);
            __m128d y2 = _mm_sub_pd(x0, x2);

            _mm_storeu_pd((double*) &out[k],   y0);
            _mm_storeu_pd((double*) &out[k+2], y2);

            __m128d x1 = _mm_loadu_pd((const double*) &out[k+1]);
            __m128d x3 = _mm_loadu_pd((const double*) &out[k+3]);
            x3 = _mm_shuffle_pd(x3, x3, 0b01);
            x3 = _mm_mul_pd(x3, _mm_set_pd(-1, 1));

            __m128d y1 = _mm_add_pd(x1, x3);
            __m128d y3 = _mm_sub_pd(x1, x3);

            _mm_storeu_pd((double*) &out[k+1], y1);
            _mm_storeu_pd((double*) &out[k+3], y3);
        }
    }

    for (int s = 3; s <= p; ++s)
    {
        int m = 1 << s;

        const complex64 Wm = std::exp(complex64(0, - 2 * Math::PI / m));
        const complex64 Wm_2 = std::exp(complex64(0, - 4 * Math::PI / m));

        __m128d Wm_2_re = _mm_set1_pd(Wm_2.real());
        __m128d Wm_2_im = _mm_set1_pd(Wm_2.imag());

        __m128d W_init_re = _mm_set_pd(Wm.real(), 1);
        __m128d W_init_im = _mm_set_pd(Wm.imag(), 0);

        for (int k = 0; k < N; k += m)
        {
            __m128d W_re = W_init_re;
            __m128d W_im = W_init_im;

            for (int j = 0; j < m/2; j += 2)
            {
                __m128d x0 = _mm_loadu_pd((const double*)&out[k+j]);
                __m128d x1 = _mm_loadu_pd((const double*)&out[k+j+m/2]);
                __m128d x2 = _mm_loadu_pd((const double*)&out[k+j+1]);
                __m128d x3 = _mm_loadu_pd((const double*)&out[k+j+1+m/2]);

                __m128d x02_re = _mm_shuffle_pd(x0, x2, 0b00);
                __m128d x02_im = _mm_shuffle_pd(x0, x2, 0b11);
                __m128d x13_re = _mm_shuffle_pd(x1, x3, 0b00);
                __m128d x13_im = _mm_shuffle_pd(x1, x3, 0b11);

                __m128d x13_i_re = _mm_sub_pd(_mm_mul_pd(x13_re, W_re), _mm_mul_pd(x13_im, W_im));
                __m128d x13_i_im = _mm_add_pd(_mm_mul_pd(x13_re, W_im), _mm_mul_pd(x13_im, W_re));

                __m128d y02_re = _mm_add_pd(x02_re, x13_i_re);
                __m128d y02_im = _mm_add_pd(x02_im, x13_i_im);
                __m128d y13_re = _mm_sub_pd(x02_re, x13_i_re);
                __m128d y13_im = _mm_sub_pd(x02_im, x13_i_im);

                __m128d y0 = _mm_unpacklo_pd(y02_re, y02_im);
                __m128d y1 = _mm_unpacklo_pd(y13_re, y13_im);
                __m128d y2 = _mm_unpackhi_pd(y02_re, y02_im);
                __m128d y3 = _mm_unpackhi_pd(y13_re, y13_im);

                _mm_storeu_pd((double*)&out[k+j], y0);
                _mm_storeu_pd((double*)&out[k+j+m/2], y1);
                _mm_storeu_pd((double*)&out[k+j+1], y2);
                _mm_storeu_pd((double*)&out[k+j+1+m/2], y3);

                __m128d new_W_re = _mm_sub_pd(_mm_mul_pd(W_re, Wm_2_re), _mm_mul_pd(W_im, Wm_2_im));
                __m128d new_W_im = _mm_add_pd(_mm_mul_pd(W_re, Wm_2_im), _mm_mul_pd(W_im, Wm_2_re));

                W_re = new_W_re;
                W_im = new_W_im;
            }
        }
    }
}

void DFT2D_sse(const complex64* in, complex64* out, int N1, int N2)
{
    complex64* aux = new complex64[N1*N2];

    for (int n1 = 0; n1 < N1; ++n1)
        DFT1D_sse(in + n1*N2, aux + n1*N2, N2);

    Transpose(aux, out, N1, N2);

    for (int n2 = 0; n2 < N2; ++n2)
        DFT1D_sse(out + n2*N1, aux + n2*N1, N1);

    Transpose(aux, out, N2, N1);

    delete[] aux;
}

//
// IDFTs
//

void IDFT1D_scalar(const complex64* in, complex64* out, int N)
{
    assert(N > 1);

    assert(IsPowerOf2(N));

    int p = GetPowerOf2(N);
    for (int i = 0; i < N; ++i)
        out[BitReverse(i, p)] = in[i];

    if (p >= 1)
    {
        for (int k = 0; k < N; k += 2)
        {
            complex64 x0 = out[k];
            complex64 x1 = out[k+1];

            out[k]   = x0 + x1;
            out[k+1] = x0 - x1;
        }
    }

    if (p >= 2)
    {
        for (int k = 0; k < N; k += 4)
        {
            complex64 x0 = out[k];
            complex64 x2 = out[k+2];
            complex64 x2_w = complex64(x2.real(), x2.imag()); // x2 * W

            out[k]   = x0 + x2_w;
            out[k+2] = x0 - x2_w;

            complex64 x1 = out[k+1];
            complex64 x3 = out[k+3];
            complex64 x3_w = complex64(-x3.imag(), x3.real()); // x3 * W

            out[k+1] = x1 + x3_w;
            out[k+3] = x1 - x3_w;
        }
    }

    for (int s = 3; s <= p; ++s)
    {
        int m = 1 << s;

        const complex64 Wm = std::exp(complex64(0, 2 * Math::PI / m));

        for (int k = 0; k < N; k += m)
        {
            complex64 W = complex64(1, 0);

            for (int j = 0; j < m/2; ++j)
            {
                complex64 x0 = out[k+j];
                complex64 x1 = out[k+j+m/2] * W;

                out[k+j] = x0 + x1;
                out[k+j+m/2] = x0 - x1;

                W *= Wm;
            }
        }
    }
}

void IDFT2D_scalar(const complex64* in, complex64* out, int N1, int N2)
{
    complex64* aux = (complex64*) new complex64[N1*N2];

    for (int n1 = 0; n1 < N1; ++n1)
        IDFT1D_scalar(in + n1*N2, aux + n1*N2, N2);

    Transpose(aux, out, N1, N2);

    for (int n2 = 0; n2 < N2; ++n2)
        IDFT1D_scalar(out + n2*N1, aux + n2*N1, N1);

    Transpose(aux, out, N2, N1);

    delete[] aux;
}

void IDFT1D_sse(const complex64* in, complex64* out, int N)
{
    assert(N > 1);

    assert(IsPowerOf2(N));

    int p = GetPowerOf2(N);
    for (int i = 0; i < N; ++i)
        out[BitReverse(i, p)] = in[i];

    if (p >= 1)
    {
        for (int k = 0; k < N; k += 2)
        {
            __m128d x0 = _mm_loadu_pd((const double*) &out[k]);
            __m128d x1 = _mm_loadu_pd((const double*) &out[k+1]);

            __m128d y0 = _mm_add_pd(x0, x1);
            __m128d y1 = _mm_sub_pd(x0, x1);

            _mm_storeu_pd((double*) &out[k],   y0);
            _mm_storeu_pd((double*) &out[k+1], y1);
        }
    }

    if (p >= 2)
    {
        for (int k = 0; k < N; k += 4)
        {
            __m128d x0 = _mm_loadu_pd((const double*) &out[k]);
            __m128d x2 = _mm_loadu_pd((const double*) &out[k+2]);

            __m128d y0 = _mm_add_pd(x0, x2);
            __m128d y2 = _mm_sub_pd(x0, x2);

            _mm_storeu_pd((double*) &out[k],   y0);
            _mm_storeu_pd((double*) &out[k+2], y2);

            __m128d x1 = _mm_loadu_pd((const double*) &out[k+1]);
            __m128d x3 = _mm_loadu_pd((const double*) &out[k+3]);
            x3 = _mm_shuffle_pd(x3, x3, 0b01);
            x3 = _mm_mul_pd(x3, _mm_set_pd(1, -1));

            __m128d y1 = _mm_add_pd(x1, x3);
            __m128d y3 = _mm_sub_pd(x1, x3);

            _mm_storeu_pd((double*) &out[k+1], y1);
            _mm_storeu_pd((double*) &out[k+3], y3);
        }
    }

    for (int s = 3; s <= p; ++s)
    {
        int m = 1 << s;

        const complex64 Wm = std::exp(complex64(0, 2 * Math::PI / m));
        const complex64 Wm_2 = std::exp(complex64(0, 4 * Math::PI / m));

        __m128d Wm_2_re = _mm_set1_pd(Wm_2.real());
        __m128d Wm_2_im = _mm_set1_pd(Wm_2.imag());

        __m128d W_init_re = _mm_set_pd(Wm.real(), 1);
        __m128d W_init_im = _mm_set_pd(Wm.imag(), 0);

        for (int k = 0; k < N; k += m)
        {
            __m128d W_re = W_init_re;
            __m128d W_im = W_init_im;

            for (int j = 0; j < m/2; j += 2)
            {
                __m128d x0 = _mm_loadu_pd((const double*)&out[k+j]);
                __m128d x1 = _mm_loadu_pd((const double*)&out[k+j+m/2]);
                __m128d x2 = _mm_loadu_pd((const double*)&out[k+j+1]);
                __m128d x3 = _mm_loadu_pd((const double*)&out[k+j+1+m/2]);

                __m128d x02_re = _mm_shuffle_pd(x0, x2, 0b00);
                __m128d x02_im = _mm_shuffle_pd(x0, x2, 0b11);
                __m128d x13_re = _mm_shuffle_pd(x1, x3, 0b00);
                __m128d x13_im = _mm_shuffle_pd(x1, x3, 0b11);

                __m128d x13_i_re = _mm_sub_pd(_mm_mul_pd(x13_re, W_re), _mm_mul_pd(x13_im, W_im));
                __m128d x13_i_im = _mm_add_pd(_mm_mul_pd(x13_re, W_im), _mm_mul_pd(x13_im, W_re));

                __m128d y02_re = _mm_add_pd(x02_re, x13_i_re);
                __m128d y02_im = _mm_add_pd(x02_im, x13_i_im);
                __m128d y13_re = _mm_sub_pd(x02_re, x13_i_re);
                __m128d y13_im = _mm_sub_pd(x02_im, x13_i_im);

                __m128d y0 = _mm_unpacklo_pd(y02_re, y02_im);
                __m128d y1 = _mm_unpacklo_pd(y13_re, y13_im);
                __m128d y2 = _mm_unpackhi_pd(y02_re, y02_im);
                __m128d y3 = _mm_unpackhi_pd(y13_re, y13_im);

                _mm_storeu_pd((double*)&out[k+j], y0);
                _mm_storeu_pd((double*)&out[k+j+m/2], y1);
                _mm_storeu_pd((double*)&out[k+j+1], y2);
                _mm_storeu_pd((double*)&out[k+j+1+m/2], y3);

                __m128d new_W_re = _mm_sub_pd(_mm_mul_pd(W_re, Wm_2_re), _mm_mul_pd(W_im, Wm_2_im));
                __m128d new_W_im = _mm_add_pd(_mm_mul_pd(W_re, Wm_2_im), _mm_mul_pd(W_im, Wm_2_re));

                W_re = new_W_re;
                W_im = new_W_im;
            }
        }
    }
}

void IDFT2D_sse(const complex64* in, complex64* out, int N1, int N2)
{
    complex64* aux = new complex64[N1*N2];

    for (int n1 = 0; n1 < N1; ++n1)
        IDFT1D_sse(in + n1*N2, aux + n1*N2, N2);

    Transpose(aux, out, N1, N2);

    for (int n2 = 0; n2 < N2; ++n2)
        IDFT1D_sse(out + n2*N1, aux + n2*N1, N1);

    Transpose(aux, out, N2, N1);

    delete[] aux;
}
