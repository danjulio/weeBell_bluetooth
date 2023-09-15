/*
 * SpanDSP - a series of DSP components for telephony
 *
 * spandsp.h - The head guy amongst the headers.  Include this in any
 * source that will use spandsp.
 *
 * Written by Steve Underwood <steveu@coppice.org"
 *
 * Copyright (C) 2003 Steve Underwood
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*! \file */

#if !defined(_SPANDSP_H_)
#define _SPANDSP_H_

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#include "telephony.h"
#include "fast_convert.h"
#include "logging.h"
#include "complex.h"
#include "bit_operations.h"
#include "queue.h"
#include "vector_float.h"
#include "complex_vector_float.h"
#include "fir.h"
#include "power_meter.h"
#include "dc_restore.h"
#include "dds.h"
#include "echo.h"
#include "crc.h"
#include "async.h"
#include "tone_detect.h"
#include "tone_generate.h"
#include "super_tone_rx.h"
#include "super_tone_tx.h"
#include "dtmf.h"
#include "fsk.h"
#include "adsi.h"

#endif

/*- End of file ------------------------------------------------------------*/