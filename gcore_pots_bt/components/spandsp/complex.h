/*
 * SpanDSP - a series of DSP components for telephony
 *
 * complex.h
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2003 Steve Underwood
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id: complex.h,v 1.8 2006/10/24 13:45:28 steveu Exp $
 */

/*! \file */

/*! \page complex_page Complex number support
\section complex_page_sec_1 What does it do?
Complex number support is part of the C99 standard. However, support for this
in C compilers is still patchy. A set of complex number feaures is provided as
a "temporary" measure, until native C language complex number support is
widespread.
*/

#if !defined(_COMPLEX_H_)
#define _COMPLEX_H_

/*!
    Floating complex type.
*/
typedef struct
{
    float re;
    float im;
} complexf_t;

/*!
    Floating complex type.
*/
typedef struct
{
    double re;
    double im;
} complex_t;

#if defined(HAVE_LONG_DOUBLE)
/*!
    Long double complex type.
*/
typedef struct
{
    long double re;
    long double im;
} complexl_t;
#endif

/*!
    Complex integer type.
*/
typedef struct
{
    int re;
    int im;
} icomplex_t;

/*!
    Complex 16 bit integer type.
*/
typedef struct
{
    int16_t re;
    int16_t im;
} i16complex_t;

/*!
    Complex 32 bit integer type.
*/
typedef struct
{
    int32_t re;
    int32_t im;
} i32complex_t;

#ifdef __cplusplus
extern "C" {
#endif

static __inline__ complexf_t complex_setf(float re, float im)
{
    complexf_t z;

    z.re = re;
    z.im = im;
    return z;
}
/*- End of function --------------------------------------------------------*/

static __inline__ complex_t complex_set(float re, float im)
{
    complex_t z;

    z.re = re;
    z.im = im;
    return z;
}
/*- End of function --------------------------------------------------------*/

#if defined(HAVE_LONG_DOUBLE)
static __inline__ complexl_t complex_setl(long double re, long double im)
{
    complexl_t z;

    z.re = re;
    z.im = im;
    return z;
}
/*- End of function --------------------------------------------------------*/
#endif

static __inline__ icomplex_t icomplex_set(int re, int im)
{
    icomplex_t z;

    z.re = re;
    z.im = im;
    return z;
}
/*- End of function --------------------------------------------------------*/

static __inline__ complexf_t complex_addf(const complexf_t *x, const complexf_t *y)
{
    complexf_t z;

    z.re = x->re + y->re;
    z.im = x->im + y->im;
    return z;
}
/*- End of function --------------------------------------------------------*/

static __inline__ complex_t complex_add(const complex_t *x, const complex_t *y)
{
    complex_t z;

    z.re = x->re + y->re;
    z.im = x->im + y->im;
    return z;
}
/*- End of function --------------------------------------------------------*/

#if defined(HAVE_LONG_DOUBLE)
static __inline__ complexl_t complex_addl(const complexl_t *x, const complexl_t *y)
{
    complexl_t z;

    z.re = x->re + y->re;
    z.im = x->im + y->im;
    return z;
}
/*- End of function --------------------------------------------------------*/
#endif

static __inline__ icomplex_t icomplex_add(const icomplex_t *x, const icomplex_t *y)
{
    icomplex_t z;

    z.re = x->re + y->re;
    z.im = x->im + y->im;
    return z;
}
/*- End of function --------------------------------------------------------*/

static __inline__ complexf_t complex_subf(const complexf_t *x, const complexf_t *y)
{
    complexf_t z;

    z.re = x->re - y->re;
    z.im = x->im - y->im;
    return z;
}
/*- End of function --------------------------------------------------------*/

static __inline__ complex_t complex_sub(const complex_t *x, const complex_t *y)
{
    complex_t z;

    z.re = x->re - y->re;
    z.im = x->im - y->im;
    return z;
}
/*- End of function --------------------------------------------------------*/

#if defined(HAVE_LONG_DOUBLE)
static __inline__ complexl_t complex_subl(const complexl_t *x, const complexl_t *y)
{
    complexl_t z;

    z.re = x->re - y->re;
    z.im = x->im - y->im;
    return z;
}
/*- End of function --------------------------------------------------------*/
#endif

static __inline__ icomplex_t icomplex_sub(const icomplex_t *x, const icomplex_t *y)
{
    icomplex_t z;

    z.re = x->re - y->re;
    z.im = x->im - y->im;
    return z;
}
/*- End of function --------------------------------------------------------*/

static __inline__ complexf_t complex_mulf(const complexf_t *x, const complexf_t *y)
{
    complexf_t z;

    z.re = x->re*y->re - x->im*y->im;
    z.im = x->re*y->im + x->im*y->re;
    return z;
}
/*- End of function --------------------------------------------------------*/

static __inline__ complex_t complex_mul(const complex_t *x, const complex_t *y)
{
    complex_t z;

    z.re = x->re*y->re - x->im*y->im;
    z.im = x->re*y->im + x->im*y->re;
    return z;
}
/*- End of function --------------------------------------------------------*/

#if defined(HAVE_LONG_DOUBLE)
static __inline__ complexl_t complex_mull(const complexl_t *x, const complexl_t *y)
{
    complexl_t z;

    z.re = x->re*y->re - x->im*y->im;
    z.im = x->re*y->im + x->im*y->re;
    return z;
}
/*- End of function --------------------------------------------------------*/
#endif

static __inline__ complexf_t complex_divf(const complexf_t *x, const complexf_t *y)
{
    complexf_t z;
    float f;
    
    f = y->re*y->re + y->im*y->im;
    z.re = ( x->re*y->re + x->im*y->im)/f;
    z.im = (-x->re*y->im + x->im*y->re)/f;
    return z;
}
/*- End of function --------------------------------------------------------*/

static __inline__ complex_t complex_div(const complex_t *x, const complex_t *y)
{
    complex_t z;
    double f;
    
    f = y->re*y->re + y->im*y->im;
    z.re = ( x->re*y->re + x->im*y->im)/f;
    z.im = (-x->re*y->im + x->im*y->re)/f;
    return z;
}
/*- End of function --------------------------------------------------------*/

#if defined(HAVE_LONG_DOUBLE)
static __inline__ complexl_t complex_divl(const complexl_t *x, const complexl_t *y)
{
    complexl_t z;
    long double f;
    
    f = y->re*y->re + y->im*y->im;
    z.re = ( x->re*y->re + x->im*y->im)/f;
    z.im = (-x->re*y->im + x->im*y->re)/f;
    return z;
}
/*- End of function --------------------------------------------------------*/
#endif

static __inline__ complexf_t complex_conjf(const complexf_t *x)
{
    complexf_t z;

    z.re = x->re;
    z.im = -x->im;
    return z;
}
/*- End of function --------------------------------------------------------*/

static __inline__ complex_t complex_conj(const complex_t *x)
{
    complex_t z;

    z.re = x->re;
    z.im = -x->im;
    return z;
}
/*- End of function --------------------------------------------------------*/

#if defined(HAVE_LONG_DOUBLE)
static __inline__ complexl_t complex_conjl(const complexl_t *x)
{
    complexl_t z;

    z.re = x->re;
    z.im = -x->im;
    return z;
}
/*- End of function --------------------------------------------------------*/
#endif

static __inline__ icomplex_t icomplex_conj(const icomplex_t *x)
{
    icomplex_t z;

    z.re = x->re;
    z.im = -x->im;
    return z;
}
/*- End of function --------------------------------------------------------*/

static __inline__ float powerf(const complexf_t *x)
{
    return x->re*x->re + x->im*x->im;
}
/*- End of function --------------------------------------------------------*/

static __inline__ double power(const complex_t *x)
{
    return x->re*x->re + x->im*x->im;
}
/*- End of function --------------------------------------------------------*/

#if defined(HAVE_LONG_DOUBLE)
static __inline__ long double powerl(const complexl_t *x)
{
    return x->re*x->re + x->im*x->im;
}
/*- End of function --------------------------------------------------------*/
#endif

#ifdef __cplusplus
}
#endif

#endif
/*- End of file ------------------------------------------------------------*/
