/*
 * SpanDSP - a series of DSP components for telephony
 *
 * super_tone_tx.c - Flexible telephony supervisory tone generation.
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
 * $Id: super_tone_tx.c,v 1.15 2006/11/19 14:07:25 steveu Exp $
 */
/*
 * Modified 4/2023 by Dan Julio to add 4 channel support (off-hook tone generation)
 */

/*! \file */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <inttypes.h>
#include <math.h>

#include "telephony.h"
#include "complex.h"
#include "dds.h"
#include "super_tone_tx.h"


super_tone_tx_step_t *super_tone_tx_make_step_4(super_tone_tx_step_t *s,
                                              float f1,
                                              float f2,
                                              float f3,
                                              float f4,
                                              float level,
                                              int length,
                                              int cycles)
{
	if (s == NULL)
    {
        s = (super_tone_tx_step_t *) malloc(sizeof(super_tone_tx_step_t));
        if (s == NULL)
            return NULL;
    }
    if (f1 >= 1.0)
    {    
        s->phase_rate[0] = dds_phase_ratef(f1);
        s->gain[0] = dds_scaling_dbm0f(level);
    }
    else
    {
        s->phase_rate[0] = 0;
        s->gain[0] = 0;
    }
    if (f2 >= 1.0)
    {
        s->phase_rate[1] = dds_phase_ratef(f2);
        s->gain[1] = dds_scaling_dbm0f(level);
    }
    else
    {
        s->phase_rate[1] = 0;
        s->gain[1] = 0;
    }
    if (f3 >= 1.0)
    {
        s->phase_rate[2] = dds_phase_ratef(f3);
        s->gain[2] = dds_scaling_dbm0f(level);
    }
    else
    {
        s->phase_rate[2] = 0;
        s->gain[2] = 0;
    }
    if (f4 >= 1.0)
    {
        s->phase_rate[3] = dds_phase_ratef(f4);
        s->gain[3] = dds_scaling_dbm0f(level);
    }
    else
    {
        s->phase_rate[3] = 0;
        s->gain[3] = 0;
    }
    s->tone = (f1 > 0.0);
    s->length = length*8;
    s->cycles = cycles;
    s->next = NULL;
    s->nest = NULL;
    return  s;
}
/*- End of function --------------------------------------------------------*/

super_tone_tx_step_t *super_tone_tx_make_step(super_tone_tx_step_t *s,
                                              float f1,
                                              float l1,
                                              float f2,
                                              float l2,
                                              int length,
                                              int cycles)
{
    if (s == NULL)
    {
        s = (super_tone_tx_step_t *) malloc(sizeof(super_tone_tx_step_t));
        if (s == NULL)
            return NULL;
    }
    if (f1 >= 1.0)
    {    
        s->phase_rate[0] = dds_phase_ratef(f1);
        s->gain[0] = dds_scaling_dbm0f(l1);
    }
    else
    {
        s->phase_rate[0] = 0;
        s->gain[0] = 0;
    }
    if (f2 >= 1.0)
    {
        s->phase_rate[1] = dds_phase_ratef(f2);
        s->gain[1] = dds_scaling_dbm0f(l2);
    }
    else
    {
        s->phase_rate[1] = 0;
        s->gain[1] = 0;
    }
    s->phase_rate[2] = 0;
    s->gain[2] = 0;
    s->phase_rate[3] = 0;
    s->gain[3] = 0;
    s->tone = (f1 > 0.0);
    s->length = length*8;
    s->cycles = cycles;
    s->next = NULL;
    s->nest = NULL;
    return  s;
}
/*- End of function --------------------------------------------------------*/

void super_tone_tx_free(super_tone_tx_step_t *s)
{
    super_tone_tx_step_t *t;

    while (s)
    {
        /* Follow nesting... */
        if (s->nest)
            super_tone_tx_free(s->nest);
        t = s;
        s = s->next;
        free(t);
    }
}
/*- End of function --------------------------------------------------------*/

super_tone_tx_state_t *super_tone_tx_init(super_tone_tx_state_t *s, super_tone_tx_step_t *tree)
{
    if (tree == NULL)
        return NULL;
    memset(s, 0, sizeof(*s));
    s->level = 0;
    s->levels[0] = tree;
    s->cycles[0] = tree->cycles;

    s->current_position = 0;
    return s;
}
/*- End of function --------------------------------------------------------*/

int super_tone_tx(super_tone_tx_state_t *s, int16_t amp[], int max_samples)
{
    int samples;
    int limit;
    int len;
    float xamp;
    super_tone_tx_step_t *tree;

    if (s->level < 0  ||  s->level > 3)
        return  0;
    samples = 0;
    tree = s->levels[s->level];
    while (tree  &&  samples < max_samples)
    {
        if (tree->tone)
        {
            /* A period of tone. A length of zero means infinite
               length. */
            if (s->current_position == 0)
            {
                /* New step - prepare the tone generator */
                for (int i=0; i<4; i++) {
                	s->phase_rate[i] = tree->phase_rate[i];
                	s->gain[i] = tree->gain[i];
                }
            }
            len = tree->length - s->current_position;
            if (tree->length == 0)
            {
                len = max_samples - samples;
                /* We just need to make current position non-zero */
                s->current_position = 1;
            }
            else if (len > max_samples - samples)
            {
                len = max_samples - samples;
                s->current_position += len;
            }
            else
            {
                s->current_position = 0;
            }
            for (limit = len + samples;  samples < limit;  samples++)
            {
                xamp = 0.0;
                if (s->phase_rate[0])
                    xamp += dds_modf(&(s->phase[0]), s->phase_rate[0], s->gain[0], 0);
                if (s->phase_rate[1])
                    xamp += dds_modf(&(s->phase[1]), s->phase_rate[1], s->gain[1], 0);
                if (s->phase_rate[2])
                    xamp += dds_modf(&(s->phase[2]), s->phase_rate[2], s->gain[2], 0);
                if (s->phase_rate[3])
                    xamp += dds_modf(&(s->phase[3]), s->phase_rate[3], s->gain[3], 0);
                amp[samples] = (int16_t) lrintf(xamp);
            }
            if (s->current_position)
                return samples;
        }
        else if (tree->length)
        {
            /* A period of silence. The length must always
               be explicitly stated. A length of zero does
               not give infinite silence. */
            len = tree->length - s->current_position;
            if (len > max_samples - samples)
            {
                len = max_samples - samples;
                s->current_position += len;
            }
            else
            {
                s->current_position = 0;
            }
            memset(amp + samples, 0, sizeof(uint16_t)*len);
            samples += len;
            if (s->current_position)
                return samples;
        }
        /* Nesting has priority... */
        if (tree->nest)
        {
            tree = tree->nest;
            s->levels[++s->level] = tree;
            s->cycles[s->level] = tree->cycles;
        }
        else
        {
            /* ...Next comes repeating, and finally moving forward a step. */
            /* When repeating, note that zero cycles really means endless cycles. */
            while (tree->cycles  &&  --s->cycles[s->level] <= 0)
            {
                tree = tree->next;
                if (tree)
                {
                    /* A fresh new step. */
                    s->levels[s->level] = tree;
                    s->cycles[s->level] = tree->cycles;
                    break;
                }
                /* If we are nested we need to pop, otherwise this is the end. */
                if (s->level <= 0)
                {
                    /* Mark the tone as completed */
                    s->levels[0] = NULL;
                    break;
                }
                tree = s->levels[--s->level];
            }
        }
        
    }
    return  samples;
}
/*- End of function --------------------------------------------------------*/
/*- End of file ------------------------------------------------------------*/
