/*
 * Accounting module
 *
 * Copyright (C) 2011 - Sven Knoblich 1&1 Internet AG
 *
 * This file is part of Kamailio, a free SIP server.
 *
 * Kamailio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * Kamailio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*! \file
 * \ingroup acc
 * \brief Acc:: File to handle CDR generation with the help of the dialog module
 *
 * - Module: \ref acc
 */

/*! \defgroup acc ACC :: The Kamailio accounting Module
 *
 * The ACC module is used to account transactions information to
 *  different backends like syslog, SQL, RADIUS and DIAMETER (beta
 *  version).
 *
 */
#include "../../modules/tm/tm_load.h"
#include "../dialog/dlg_load.h"

#include "acc_api.h"
#include "acc_cdr.h"
#include "acc_mod.h"
#include "acc_extra.h"
#include "acc.h"

#include <sys/timeb.h>

#define TIME_STR_BUFFER_SIZE 20

struct dlg_binds dlgb;
struct acc_extra* cdr_extra = NULL;
int cdr_facility = LOG_DAEMON;

static const str start_id = { "sz", 2};
static const str end_id = { "ez", 2};
static const str duration_id = { "d", 1};
static const str zero_duration = { "0", 1};
static const struct timeb time_error = {0,0,0,0};
static const char time_separator = {'.'};
static const int milliseconds_max = 1000;
static const unsigned int time_buffer_length = 256;
static const str empty_string = { "", 0};

// buffers which are used to collect the crd data for writing
static str cdr_attrs[ MAX_CDR_CORE + MAX_CDR_EXTRA];
static str cdr_value_array[ MAX_CDR_CORE + MAX_CDR_EXTRA];
static int cdr_int_arr[ MAX_CDR_CORE + MAX_CDR_EXTRA];
static char cdr_type_array[ MAX_CDR_CORE + MAX_CDR_EXTRA];

extern struct tm_binds tmb;

/* compare two times */
static int is_time_equal( struct timeb first_time,
                          struct timeb second_time)
{
    if( first_time.time == second_time.time &&
        first_time.millitm == second_time.millitm &&
        first_time.timezone == second_time.timezone &&
        first_time.dstflag == second_time.dstflag )
    {
        return 1;
    }

    return 0;
}

/* write all basic information to buffers(e.g. start-time ...) */
static int cdr_core2strar( struct dlg_cell* dlg,
                           str* values,
                           int* unused,
                           char* types)
{
    str* start = NULL;
    str* end = NULL;
    str* duration = NULL;

    if( !dlg || !values || !types)
    {
        LM_ERR( "invalid input parameter!\n");
        return 0;
    }

    start = dlgb.get_dlg_var( dlg, (str*)&start_id);
    end = dlgb.get_dlg_var( dlg, (str*)&end_id);
    duration = dlgb.get_dlg_var( dlg, (str*)&duration_id);

    values[0] = ( start != NULL ? *start : empty_string);
    types[0] = ( start != NULL ? TYPE_STR : TYPE_NULL);

    values[1] = ( end != NULL ? *end : empty_string);
    types[1] = ( end != NULL ? TYPE_STR : TYPE_NULL);

    values[2] = ( duration != NULL ? *duration : empty_string);
    types[2] = ( duration != NULL ? TYPE_STR : TYPE_NULL);

    return MAX_CDR_CORE;
}

/* collect all crd data and write it to a syslog */
static int write_cdr( struct dlg_cell* dialog,
                      struct sip_msg* message)
{
    static char cdr_message[ MAX_SYSLOG_SIZE];
    static char* const cdr_message_end = cdr_message +
                                         MAX_SYSLOG_SIZE -
                                         2;// -2 because of the string ending '\n\0'
    char* message_position = NULL;
    int message_index = 0;
    int counter = 0;

    if( !dialog || !message)
    {
        LM_ERR( "dialog and/or message is/are empty!");
        return -1;
    }

    /* get default values */
    message_index = cdr_core2strar( dialog,
                                    cdr_value_array,
                                    cdr_int_arr,
                                    cdr_type_array);

    /* get extra values */
    message_index += extra2strar( cdr_extra,
                                  message,
                                  cdr_value_array + message_index,
                                  cdr_int_arr + message_index,
                                  cdr_type_array + message_index);

    for( counter = 0, message_position = cdr_message;
         counter < message_index ;
         counter++ )
    {
        const char* const next_message_end = message_position +
                                             2 + // ', ' -> two letters
                                             cdr_attrs[ counter].len +
                                             1 + // '=' -> one letter
                                             cdr_value_array[ counter].len;

        if( next_message_end >= cdr_message_end ||
            next_message_end < message_position)
        {
            LM_WARN("cdr message too long, truncating..\n");
            message_position = cdr_message_end;
            break;
        }

        if( counter > 0)
        {
            *(message_position++) = A_SEPARATOR_CHR;
            *(message_position++) = A_SEPARATOR_CHR_2;
        }

        memcpy( message_position,
                cdr_attrs[ counter].s,
                cdr_attrs[ counter].len);

        message_position += cdr_attrs[ counter].len;

        *( message_position++) = A_EQ_CHR;

        memcpy( message_position,
                cdr_value_array[ counter].s,
                cdr_value_array[ counter].len);

        message_position += cdr_value_array[ counter].len;
    }

    /* terminating line */
    *(message_position++) = '\n';
    *(message_position++) = '\0';

    LM_GEN2( cdr_facility, log_level, "%s", cdr_message);

    return 0;
}

/* convert a string into a timeb struct */
static struct timeb time_from_string( str* time_value)
{    
    char* dot_address = NULL;
    int dot_position = -1;
    char zero_terminated_value[TIME_STR_BUFFER_SIZE];

    if( !time_value)
    {
        LM_ERR( "time_value is empty!");
        return time_error;
    }
    
    if( time_value->len >= TIME_STR_BUFFER_SIZE)
    {
        LM_ERR( "time_value is to long %d >= %d!", 
		time_value->len, 
		TIME_STR_BUFFER_SIZE);
        return time_error;
    }
    
    memcpy( zero_terminated_value, time_value->s, time_value->len);
    zero_terminated_value[time_value->len] = '\0';
    
    dot_address = strchr( zero_terminated_value, time_separator);
    
    if( !dot_address)
    {
        LM_ERR( "failed to find separator('%c') in '%s'!\n",
                time_separator,
                zero_terminated_value);
        return time_error;
    }
    
    dot_position = dot_address-zero_terminated_value + 1;
    
    if( dot_position >= strlen(zero_terminated_value) ||
        strchr(dot_address + 1, time_separator))
    {
        LM_ERR( "invalid time-string '%s'\n", zero_terminated_value);
        return time_error;
    }
    
    return (struct timeb) { atoi( zero_terminated_value),
                            atoi( dot_address + 1),
                            0,
                            0};
}

/* set the duration in the dialog struct */
static int set_duration( struct dlg_cell* dialog)
{
    struct timeb start_timeb = time_error;
    struct timeb end_timeb = time_error;
    int milliseconds = -1;
    int seconds = -1;
    char buffer[ time_buffer_length];
    int buffer_length = -1;
    str duration_time = empty_string;

    if( !dialog)
    {
        LM_ERR("dialog is empty!\n");
        return -1;
    }

    start_timeb = time_from_string( dlgb.get_dlg_var( dialog, (str*)&start_id));
    end_timeb  = time_from_string( dlgb.get_dlg_var( dialog, (str*)&end_id));

    if( is_time_equal( start_timeb, time_error) ||
        is_time_equal( end_timeb, time_error))
    {
        LM_ERR( "failed to extract time from start or/and end-time\n");
        return -1;
    }

    if( start_timeb.millitm >= milliseconds_max ||
        end_timeb.millitm >= milliseconds_max)
    {
        LM_ERR( "start-(%d) or/and end-time(%d) is out of the maximum of %d\n",
                start_timeb.millitm,
                end_timeb.millitm,
                milliseconds_max);
        return -1;
    }

    milliseconds = end_timeb.millitm < start_timeb.millitm ?
                                ( milliseconds_max +
                                  end_timeb.millitm -
                                  start_timeb.millitm) :
                                ( end_timeb.millitm - start_timeb.millitm);

    seconds = end_timeb.time -
              start_timeb.time -
              ( end_timeb.millitm < start_timeb.millitm ? 1 : 0);

    if( seconds < 0)
    {
        LM_ERR( "negativ seconds(%d) for duration calculated.\n", seconds);
        return -1;
    }

    if( milliseconds < 0 || milliseconds >= milliseconds_max)
    {
        LM_ERR( "milliseconds %d are out of range 0 < x < %d.\n",
                milliseconds,
                milliseconds_max);
        return -1;
    }

    buffer_length = snprintf( buffer,
                              time_buffer_length,
                              "%d%c%03d",
                              seconds,
                              time_separator,
                              milliseconds);

    if( buffer_length < 0)
    {
        LM_ERR( "failed to write to buffer.\n");
        return -1;
    }

    duration_time = ( str){ buffer, buffer_length};

    if( dlgb.set_dlg_var( dialog,
                          (str*)&duration_id,
                          (str*)&duration_time) != 0)
    {
        LM_ERR( "failed to set duration time");
        return -1;
    }

    return 0;
}

/* set the current time as start-time in the dialog struct */
static int set_start_time( struct dlg_cell* dialog)
{
    char buffer[ time_buffer_length];
    struct timeb current_time = time_error;
    int buffer_length = -1;
    str start_time = empty_string;

    if( !dialog)
    {
        LM_ERR("dialog is empty!\n");
        return -1;
    }

    if( ftime( &current_time) < 0)
    {
        LM_ERR( "failed to get current time!\n");
        return -1;
    }

    buffer_length = snprintf( buffer,
                              time_buffer_length,
                              "%d%c%03d",
                              (int)current_time.time,
                              time_separator,
                              (int)current_time.millitm);

    if( buffer_length < 0)
    {
        LM_ERR( "reach buffer size\n");
        return -1;
    }

    start_time = (str){ buffer, buffer_length};

    if( dlgb.set_dlg_var( dialog,
                          (str*)&start_id,
                          (str*)&start_time) != 0)
    {
        LM_ERR( "failed to set start time\n");
        return -1;
    }

    if( dlgb.set_dlg_var( dialog,
                          (str*)&end_id,
                          (str*)&start_time) != 0)
    {
        LM_ERR( "failed to set initiation end time\n");
        return -1;
    }

    if( dlgb.set_dlg_var( dialog,
                          (str*)&duration_id,
                          (str*)&zero_duration) != 0)
    {
        LM_ERR( "failed to set initiation duration time\n");
        return -1;
    }

    return 0;
}

/* set the current time as end-time in the dialog struct */
static int set_end_time( struct dlg_cell* dialog)
{
    char buffer[ time_buffer_length];
    struct timeb current_time = time_error;
    int buffer_length = -1;
    str end_time = empty_string;

    if( !dialog)
    {
        LM_ERR("dialog is empty!\n");
        return -1;
    }

    if( ftime( &current_time) < 0)
    {
        LM_ERR( "failed to set time!\n");
        return -1;
    }

    buffer_length = snprintf( buffer,
                              time_buffer_length,
                              "%d%c%03d",
                              (int)current_time.time,
                              time_separator,
                              (int)current_time.millitm);

    if( buffer_length < 0)
    {
        LM_ERR( "failed to write buffer\n");
        return -1;
    }

    end_time = ( str){ buffer, buffer_length};

    if( dlgb.set_dlg_var( dialog,
                          (str*)&end_id,
                          (str*)&end_time) != 0)
    {
        LM_ERR( "failed to set start time");
        return -1;
    }

    return 0;
}

/* callback for a confirmed (INVITE) dialog. */
static void cdr_on_start( struct dlg_cell* dialog,
                          int type,
                          struct dlg_cb_params* params)
{
    if( !dialog || !params)
    {
        LM_ERR("invalid values\n!");
        return;
    }

    if( cdr_start_on_confirmed == 0)
    {
        return;
    }

    if( set_start_time( dialog) != 0)
    {
        LM_ERR( "failed to set start time!\n");
        return;
    }
}

/* callback for a failure during a dialog. */
static void cdr_on_failed( struct dlg_cell* dialog,
                           int type,
                           struct dlg_cb_params* params)
{
    struct sip_msg* msg = 0;

    if( !dialog || !params)
    {
        LM_ERR("invalid values\n!");
        return;
    }

    if( params->rpl && params->rpl != FAKED_REPLY)
    {
        msg = params->rpl;
    }
    else if( params->req)
    {
        msg = params->req;
    }
    else
    {
        LM_ERR( "request and response are invalid!");
        return;
    }

    if( write_cdr( dialog, msg) != 0)
    {
        LM_ERR( "failed to write cdr!\n");
        return;
    }
}

/* callback for the finish of a dialog (reply to BYE). */
void cdr_on_end_confirmed( struct dlg_cell* dialog,
                        int type,
                        struct dlg_cb_params* params)
{
    if( !dialog || !params || !params->req)
    {
        LM_ERR("invalid values\n!");
        return;
    }

    if( write_cdr( dialog, params->req) != 0)
    {
        LM_ERR( "failed to write cdr!\n");
        return;
    }
}

/* callback for the end of a dialog (BYE). */
static void cdr_on_end( struct dlg_cell* dialog,
                        int type,
                        struct dlg_cb_params* params)
{
    if( !dialog || !params || !params->req)
    {
        LM_ERR("invalid values\n!");
        return;
    }

    if( set_end_time( dialog) != 0)
    {
        LM_ERR( "failed to set end time!\n");
        return;
    }

    if( set_duration( dialog) != 0)
    {
        LM_ERR( "failed to set duration!\n");
        return;
    }
}

/* callback for a expired dialog. */
static void cdr_on_expired( struct dlg_cell* dialog,
                            int type,
                            struct dlg_cb_params* params)
{
    if( !dialog || !params)
    {
        LM_ERR("invalid values\n!");
        return;
    }

    LM_DBG("dialog '%p' expired!\n", dialog);
}

/* callback for the cleanup of a dialog. */
static void cdr_on_destroy( struct dlg_cell* dialog,
                            int type,
                            struct dlg_cb_params* params)
{
    if( !dialog || !params)
    {
        LM_ERR("invalid values\n!");
        return;
    }

    LM_DBG("dialog '%p' destroyed!\n", dialog);
}

/* callback for the creation of a dialog. */
static void cdr_on_create( struct dlg_cell* dialog,
                           int type,
                           struct dlg_cb_params* params)
{
    if( !dialog || !params || !params->req)
    {
        LM_ERR( "invalid values\n!");
        return;
    }

    if( cdr_enable == 0)
    {
        return;
    }

    if( dlgb.register_dlgcb( dialog, DLGCB_CONFIRMED, cdr_on_start, 0, 0) != 0)
    {
        LM_ERR("can't register create dialog CONFIRM callback\n");
        return;
    }

    if( dlgb.register_dlgcb( dialog, DLGCB_FAILED, cdr_on_failed, 0, 0) != 0)
    {
        LM_ERR("can't register create dialog FAILED callback\n");
        return;
    }

    if( dlgb.register_dlgcb( dialog, DLGCB_TERMINATED, cdr_on_end, 0, 0) != 0)
    {
        LM_ERR("can't register create dialog TERMINATED callback\n");
        return;
    }

    if( dlgb.register_dlgcb( dialog, DLGCB_TERMINATED_CONFIRMED, cdr_on_end_confirmed, 0, 0) != 0)
    {
        LM_ERR("can't register create dialog TERMINATED CONFIRMED callback\n");
        return;
    }

    if( dlgb.register_dlgcb( dialog, DLGCB_EXPIRED, cdr_on_expired, 0, 0) != 0)
    {
        LM_ERR("can't register create dialog EXPIRED callback\n");
        return;
    }

    if( dlgb.register_dlgcb( dialog, DLGCB_DESTROY, cdr_on_destroy, 0, 0) != 0)
    {
        LM_ERR("can't register create dialog DESTROY callback\n");
        return;
    }

    LM_DBG("dialog '%p' created!", dialog);

    if( set_start_time( dialog) != 0)
    {
        LM_ERR( "failed to set start time");
        return;
    }
}
/* convert the extra-data string into a list and store it */
int set_cdr_extra( char* cdr_extra_value)
{
    struct acc_extra* extra = 0;
    int counter = 0;

    if( cdr_extra_value && ( cdr_extra = parse_acc_extra( cdr_extra_value))==0)
    {
        LM_ERR("failed to parse crd_extra param\n");
        return -1;
    }

    /* fixed core attributes */
    cdr_attrs[ counter++] = start_id;
    cdr_attrs[ counter++] = end_id;
    cdr_attrs[ counter++] = duration_id;

    for(extra=cdr_extra; extra ; extra=extra->next)
    {
        cdr_attrs[ counter++] = extra->name;
    }

    return 0;
}

/* convert the facility-name string into a id and store it */
int set_cdr_facility( char* cdr_facility_str)
{
    int facility_id = -1;

    if( !cdr_facility_str)
    {
        LM_ERR( "facility is empty\n");
        return -1;
    }

    facility_id = str2facility( cdr_facility_str);

    if( facility_id == -1)
    {
        LM_ERR("invalid cdr facility configured\n");
        return -1;
    }

    cdr_facility = facility_id;

    return 0;
}

/* initialization of all necessary callbacks to track a dialog */
int init_cdr_generation( void)
{
    if( load_dlg_api( &dlgb) != 0)
    {
        LM_ERR("can't load dialog API\n");
        return -1;
    }

    if( dlgb.register_dlgcb( 0, DLGCB_CREATED, cdr_on_create, 0, 0) != 0)
    {
        LM_ERR("can't register create callback\n");
        return -1;
    }

    return 0;
}

/* convert the facility-name string into a id and store it */
void destroy_cdr_generation( void)
{
    if( !cdr_extra)
    {
        return;
    }

    destroy_extras( cdr_extra);
}