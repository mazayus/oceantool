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

#ifndef DFT_H
#define DFT_H

#include <complex>

typedef float float32;
typedef double float64;
typedef std::complex<float> complex32;
typedef std::complex<double> complex64;

// NOTE: All DFTs and IDFTs are unnormalized.

void DFT1D_scalar(const complex64* in, complex64* out, int N);
void DFT2D_scalar(const complex64* in, complex64* out, int N1, int N2);

void DFT1D_sse(const complex64* in, complex64* out, int N);
void DFT2D_sse(const complex64* in, complex64* out, int N1, int N2);

void IDFT1D_scalar(const complex64* in, complex64* out, int N);
void IDFT2D_scalar(const complex64* in, complex64* out, int N1, int N2);

void IDFT1D_sse(const complex64* in, complex64* out, int N);
void IDFT2D_sse(const complex64* in, complex64* out, int N1, int N2);

#endif
