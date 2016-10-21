/* metar.c -- metar decoder
   $Id: metar.h,v 1.6 2006/04/05 20:30:28 kees-guest Exp $
   Copyright 2004,2005 Kees Leune <kees@leune.org>
   Copyright 2016 Andrew Walton <dwalton64@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef Already_included_metar_h
#define Already_included_metar_h 1


#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

/* max size for a URL */
#define  URL_MAXSIZE 300

/* max size for a NOAA report */
#define  METAR_MAXSIZE 4096   /* actual size of the XML data is typically a little more than 1K */

/* where to fetch reports */
//#define  METARURL "http://weather.noaa.gov/pub/data/observations/metar/stations"
#define  METARURL "https://www.aviationweather.gov/adds/dataserver_current/httpparam?datasource=metars&requestType=retrieve&format=xml&mostRecentForEachStation=constraint&hoursBeforeNow=1.25&stationString="


/* clouds */

/* The following defines are used in the print_base field of cloud_dict_entry and in the print_altitude field of
 * cloud_t below.*/
#define PRINT_BASE 1
#define DONT_PRINT_BASE 0
#define NOT_APPLICABLE -1

typedef struct {
	char *amount;
	int  layer_altitude;
    int  print_altitude;
    char *layer_modifier;   // TCU etc . . .
} cloud_t;

/* linked list of clouds */
typedef struct cloud_list_el {
	cloud_t *cloud;
	struct cloud_list_el *next;
} cloud_list_t;

/* linked list of phenomena */
typedef struct phenomena_list_el {
	char *phenomena;
	struct phenomena_list_el *next;
} phenomena_list_t;

// The following defines are used in the metar_t structure below:
#define MAINTENANCE_NOT_NEEDED 0
#define MAINTENANCE_NEEDED 1

/* reports will be translated to this struct */
typedef struct {
	char station[10];
	int  day;
	int  time;
	int  winddir;  // winddir == -1 signifies variable winds
	int  windstr;
	int  windgust;
	char windunit[5];
	int  vis;
	char visunit[5];
	int  qnh;
	char qnhunit[5];
	int  qnhfp;	// fixed-point decimal places
	int  temp;
	int  dewp;
    int maintenance_needed;
    cloud_list_t *clouds;
	phenomena_list_t *phenomena;
    // FIXME Add ceiling to this and calculate ceiling
} metar_t;

typedef struct {  //FIXME use #defines for array sizes
	char date[36];
	char report[1024];
    double  latitude;
    double longitude;
    double  elevation_m;
    char category[8];  // VFR, LVFR, IFR, LIFR
} noaa_t;

/* convert meters to feet */
double meters_to_feet(double meters);

/* Parse the METAR contain in the report string. Place the parsed report in
 * the metar struct.
 */
void parse_Metar(char *report, metar_t *metar);

/* parse the NOAA report contained in the noaa_data buffer. Place a parsed
 * data in the metar struct. 
 */
int parse_NOAA_data(char *noaa_data, noaa_t *noaa);


#endif  /* End Include Guard - don't add code below */