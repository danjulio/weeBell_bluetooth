/*
 * SpanDSP - a series of DSP components for telephony
 *
 * dds.h
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
 * $Id: dds.h,v 1.9 2006/10/24 13:45:28 steveu Exp $
 */

/*! \file */

#if !defined(_DDS_H_)
#define _DDS_H_

#include "complex.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Find the phase rate value to achieve a particular frequency.
    \param frequency The desired frequency, in Hertz.
    \return The phase rate which while achieve the desired frequency.
*/
int32_t dds_phase_rate(float frequency);

float dds_frequency(int32_t phase_rate);

/*! \brief Find the scaling factor needed to achieve a specified level in dBm0.
    \param level The desired signal level, in dBm0.
    \return The scaling factor.
*/
int dds_scaling_dbm0(float level);

int dds_scaling_dbov(float level);

/*! \brief Find the amplitude for a particular phase.
    \param phase The desired phase 32 bit phase.
    \return The signal amplitude.
*/
int16_t dds_lookup(uint32_t phase);

/*! \brief Find the amplitude for a particular phase offset from an accumulated phase.
    \param phase_acc The accumulated phase.
    \param phase_offset The phase offset.
    \return The signal amplitude.
*/
int16_t dds_offset(uint32_t phase_acc, int32_t phase_offset);

/*! \brief Generate an integer tone sample.
    \param phase_acc A pointer to a phase accumulator value.
    \param phase_rate The phase increment to be applied.
    \return The signal amplitude, between -32767 and 32767.
*/
int16_t dds(uint32_t *phase_acc, int32_t phase_rate);

/*! \brief Generate an integer tone sample, with modulation.
    \param phase_acc A pointer to a phase accumulator value.
    \param phase_rate The phase increment to be applied.
    \param scale The scaling factor.
    \param phase The phase offset.
    \return The signal amplitude.
*/
int16_t dds_mod(uint32_t *phase_acc, int32_t phase_rate, int scale, int32_t phase);

/*! \brief Generate a complex integer tone sample.
    \param phase_acc A pointer to a phase accumulator value.
    \param phase_rate The phase increment to be applied.
    \return The complex signal amplitude, between -32767 and 32767.
*/
icomplex_t dds_complex(uint32_t *phase_acc, int32_t phase_rate);

/*! \brief Generate a complex integer tone sample, with modulation.
    \param phase_acc A pointer to a phase accumulator value.
    \param phase_rate The phase increment to be applied.
    \param scale The scaling factor.
    \param phase The phase offset.
    \return The complex signal amplitude.
*/
icomplex_t dds_complex_mod(uint32_t *phase_acc, int32_t phase_rate, int scale, int32_t phase);

int32_t dds_phase_ratef(float frequency);

float dds_frequencyf(int32_t phase_rate);

float dds_scaling_dbm0f(float level);

float dds_scaling_dbovf(float level);

/*! \brief Generate a floating point tone sample.
    \param phase_acc A pointer to a phase accumulator value.
    \param phase_rate The phase increment to be applied.
    \return The signal amplitude, between -32767 and 32767.
*/
float ddsf(uint32_t *phase_acc, int32_t phase_rate);

/*! \brief Generate a floating point tone sample, with modulation.
    \param phase_acc A pointer to a phase accumulator value.
    \param phase_rate The phase increment to be applied.
    \param scale The scaling factor.
    \param phase The phase offset.
    \return The signal amplitude.
*/
float dds_modf(uint32_t *phase_acc, int32_t phase_rate, float scale, int32_t phase);

/*! \brief Generate a complex floating point tone sample.
    \param phase_acc A pointer to a phase accumulator value.
    \param phase_rate The phase increment to be applied.
    \return The complex signal amplitude, between -32767 and 32767.
*/
complexf_t dds_complexf(uint32_t *phase_acc, int32_t phase_rate);

/*! \brief Generate a complex floating point tone sample, with modulation.
    \param phase_acc A pointer to a phase accumulator value.
    \param phase_rate The phase increment to be applied.
    \param scale The scaling factor.
    \param phase The phase offset.
    \return The complex signal amplitude.
*/
complexf_t dds_complex_modf(uint32_t *phase_acc, int32_t phase_rate, float scale, int32_t phase);

#ifdef __cplusplus
}
#endif

#endif
/*- End of file ------------------------------------------------------------*/
