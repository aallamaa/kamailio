/*
 * Allow, Allow-Events Header Field Name Parsing Macros
 *
 * Copyright (C) 2001-2003 FhG Fokus
 *
 * This file is part of SIP-router, a free SIP server.
 *
 * SIP-router is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * SIP-router is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*! \file 
 * \brief Parser :: Allow, Allow-Events Header Field Name Parsing Macros
 *
 * \ingroup parser
 */

#ifndef CASE_ALLO_H
#define CASE_ALLO_H

#define allow_ev_ents_CASE         \
	switch(LOWER_DWORD(val)) { \
	case _ents_:               \
		p += 4;            \
		hdr->type = HDR_ALLOWEVENTS_T; \
		goto dc_end;       \
	}



#define allo_w_ev_CASE             \
        switch(LOWER_DWORD(val)) { \
        case _w_ev_:               \
                p += 4;            \
                val = READ(p);     \
                allow_ev_ents_CASE;\
                goto other;        \
        }


#define allo_CASE                  \
    p += 4;                        \
    val = READ(p);                 \
    allo_w_ev_CASE;                \
    if (LOWER_BYTE(*p) == 'w') {   \
            hdr->type = HDR_ALLOW_T; \
            p++;                   \
            goto dc_end;           \
    }                              \
    goto other;


#endif /* CASE_ALLO_H */
