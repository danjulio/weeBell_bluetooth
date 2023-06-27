/*
 * SpanDSP - a series of DSP components for telephony
 *
 * tone_generate.c - General telephony tone generation, and specific
 *                   generation of Bell MF, MFC/R2, and network supervisory tones.
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2001 Steve Underwood
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
 * $Id: tone_generate.c,v 1.31 2006/11/28 16:59:56 steveu Exp $
 */

/*! \file */

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <math.h>

#include "telephony.h"
#include "dc_restore.h"
#include "dds.h"
#include "tone_generate.h"

#if !defined(M_PI)
/* C99 systems may not define M_PI */
#define M_PI 3.14159265358979323846264338327
#endif

#define ms_to_samples(t)            (((t)*SAMPLE_RATE)/1000)

void make_tone_gen_descriptor(tone_gen_descriptor_t *s,
                              int f1,
                              int l1,
                              int f2,
                              int l2,
                              int d1,
                              int d2,
                              int d3,
                              int d4,
                              int repeat)
{
    memset(s, 0, sizeof(*s));
    if (f1 >= 1)
    {
        s->phase_rate[0] = dds_phase_ratef((float) f1);
        s->gain[0] = dds_scaling_dbm0f((float) l1);
    }
    s->modulate = (f2 < 0);
    if (f2)
    {
        s->phase_rate[1] = dds_phase_ratef((float) abs(f2));
        s->gain[1] = (s->modulate)  ?  (float) l2/100.0f  :  dds_scaling_dbm0f((float) l2);
    }

    s->duration[0] = d1*8;
    s->duration[1] = d2*8;
    s->duration[2] = d3*8;
    s->duration[3] = d4*8;

    s->repeat = repeat;
}
/*- End of function --------------------------------------------------------*/

void make_tone_descriptor(tone_gen_descriptor_t *desc, cadenced_tone_t *tone)
{
    make_tone_gen_descriptor(desc,
                             tone->f1,
                             tone->level1,
                             tone->f2,
                             tone->level2,
                             tone->on_time1,
                             tone->off_time1,
                             tone->on_time2,
                             tone->off_time2,
                             tone->repeat);
}
/*- End of function --------------------------------------------------------*/

void tone_gen_init(tone_gen_state_t *s, tone_gen_descriptor_t *t)
{
    int i;

    s->phase_rate[0] = t->phase_rate[0];
    s->gain[0] = t->gain[0];
    s->phase_rate[1] = t->phase_rate[1];
    s->gain[1] = t->gain[1];
    s->modulate = t->modulate;

    for (i = 0;  i < 4;  i++)
        s->duration[i] = t->duration[i];
    s->repeat = t->repeat;

    s->phase[0] = 0;
    s->phase[1] = 0;

    s->current_section = 0;
    s->current_position = 0;
}
/*- End of function --------------------------------------------------------*/

int tone_gen(tone_gen_state_t *s, int16_t amp[], int max_samples)
{
    int samples;
    int limit;
    float xamp;
    float yamp;

    if (s->current_section < 0)
        return  0;

    for (samples = 0;  samples < max_samples;  )
    {
        limit = samples + s->duration[s->current_section] - s->current_position;
        if (limit > max_samples)
            limit = max_samples;
        
        s->current_position += (limit - samples);
        if (s->current_section & 1)
        {
            /* A silent section */
            for (  ;  samples < limit;  samples++)
                amp[samples] = 0;
        }
        else
        {
            for (  ;  samples < limit;  samples++)
            {
                xamp = 0.0;
                if (s->phase_rate[0])
                    xamp = dds_modf(&(s->phase[0]), s->phase_rate[0], s->gain[0], 0);
                if (s->phase_rate[1])
                {
                    yamp = dds_modf(&(s->phase[1]), s->phase_rate[1], s->gain[1], 0);
                    if (s->modulate)
                        xamp *= (1.0f + yamp);
                    else
                        xamp += yamp;
                }
                /* Saturation of the answer is the right thing at this point.
                   However, we are normally generating well controlled tones,
                   that cannot clip. So, the overhead of doing saturation is
                   a waste of valuable time. */
                amp[samples] = (int16_t) lrintf(xamp);
            }
        }
        if (s->current_position >= s->duration[s->current_section])
        {
            s->current_position = 0;
            if (++s->current_section > 3  ||  s->duration[s->current_section] == 0)
            {
                if (!s->repeat)
                {
                    /* Force a quick exit */
                    s->current_section = -1;
                    break;
                }
                s->current_section = 0;
            }
        }
    }
    return samples;
}
/*- End of function --------------------------------------------------------*/
/*- End of file ------------------------------------------------------------*/
