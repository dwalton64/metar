/* metar.c -- metar decoder
   $Id: main.c,v 1.9 2006/04/05 20:30:28 kees-guest Exp $
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
#include <stdio.h>
#include <ctype.h>
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "metar.h"

/* global variable so we don't have to mess with parameter passing */
char noaabuffer[METAR_MAXSIZE];

/* command line options */
int decode=0;
int verbose=0;
int location=0;
int datetime=0;
int category=0;

char *strupc(char *line) {
   char *p;
   for (p=line; *p; p++) *p= (char) toupper(*p);
   return line;
}

/* show brief usage info */
void usage(char *name) {
	printf("$Id: main.c,v 1.9 2006/04/05 20:30:28 kees-guest Exp $\n");
	printf("Usage: %s [OPTION]... STATION... \n", name);
    printf("Print meteorological reports (METARS) for STATIONs.\n");
    printf("Where STATIONs are one or more ICAO airport codes (e.x. ksfo).\n\n");
	printf("Options\n");
	printf("   -d        decode metar\n");
    printf("   -l        print location of the phenomenon\n");
    printf("   -t        print the time and date of the phenomenon\n");
    printf("   -c        print flight category (VFR, MVFR, IFR, LIFR)\n");
	printf("   -h        show this help\n");
	printf("   -v        be verbose\n");
	printf("Example: %s -d ehgr\n", name);
}


/* place NOAA data in buffer */
int cpReceivedData(void *buffer, size_t size, size_t nmemb, void *stream) {
	size *= nmemb;
	size = (size <= METAR_MAXSIZE) ? size : METAR_MAXSIZE;
	strncpy(noaabuffer, buffer, size);
	return (int) size;
}


/* fetch NOAA report
 * returns 0 for success, 1 for an error*/
int download_Metar(char *station) {
    CURL *curlhandle = NULL;
	CURLcode res;
    char url[URL_MAXSIZE];
	char tmp[URL_MAXSIZE];
    int retval = 0;

    curlhandle = curl_easy_init();
	if (!curlhandle) return 1;

	memset(tmp, 0x0, URL_MAXSIZE);
	if (getenv("METARURL") == NULL) {
		strncpy(tmp, METARURL, URL_MAXSIZE);
        tmp[URL_MAXSIZE-1]=0;
	} else {
		strncpy(tmp, getenv("METARURL"), URL_MAXSIZE);
        tmp[URL_MAXSIZE-1]=0;
        if (verbose) printf("Using environment variable METARURL: %s\n", tmp);
	}

    if (snprintf(url, URL_MAXSIZE, "%s%s", tmp, strupc(station)) < 0)
        return 1;
	if (verbose) printf("Retrieving URL %s\n", url);

    curl_easy_setopt(curlhandle, CURLOPT_URL, url);
	curl_easy_setopt(curlhandle, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curlhandle, CURLOPT_WRITEFUNCTION, cpReceivedData);
	memset(noaabuffer, 0x0, METAR_MAXSIZE);

	res = curl_easy_perform(curlhandle);
    if (res == CURLE_WRITE_ERROR) {
        /* If you pass a short ICAO airport code to NOAA such as "ED", the server will respond with all of the
         * METARs for airports that begin with ED (EDDT, EDDP, EDNY ...) and will overflow the noaabuffer,
         * causing a write error.
         */
        printf("%s is not a valid ICAO airport identifier.\n", station);
        retval = 1;
    } else if (res != CURLE_OK) {
        fprintf(stderr, "ERROR #%i: %s getting data for station %s\n", res, curl_easy_strerror(res), station);
        retval = 1;
    }
	curl_easy_cleanup(curlhandle);
	if (verbose) printf("Received XML:\n %s", noaabuffer);

    return retval;
}


/* decode metar */
// FIXME print flight category here also - may need to pass NOAA_t into the function.
void decode_Metar(metar_t metar) {
	cloud_list_t *curcloud;
	phenomena_list_t   *curphenomenon;
	int n = 0;
	double qnh;

	printf("Station       : %s\n", metar.station);
	printf("Day           : %i\n", metar.day);
	printf("Time          : %02i:%02i UTC\n", metar.time/100, metar.time%100);
	if (metar.winddir == -1) {
		printf("Wind direction: Variable\n");
	} else {
		static const char *winddirs[] = {
			"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
			"S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
	};
	n = ((metar.winddir * 4 + 45) / 90) % 16;
	printf("Wind direction: %i (%s)\n", metar.winddir, winddirs[n]);
	}
	printf("Wind speed    : %i %s\n", metar.windstr, metar.windunit);
	printf("Wind gust     : %i %s\n", metar.windgust, metar.windunit);
	printf("Visibility    : %i %s\n", metar.vis, metar.visunit);
	printf("Temperature   : %i C\n", metar.temp);
	printf("Dewpoint      : %i C\n", metar.dewp);

	qnh = metar.qnh;
	for (n = 0; n < metar.qnhfp; n++)
		qnh /= 10.0;
	printf("Pressure      : %.*f %s\n", metar.qnhfp, qnh, metar.qnhunit);
		
	printf("Clouds        : ");
	n = 0;
	for (curcloud = metar.clouds; curcloud != NULL; curcloud=curcloud->next) {
		if (n++ == 0) {
            // print the first cloud layer OR that no clouds were detected
            if(curcloud->cloud->print_altitude == PRINT_BASE) {
                printf("%s at %d00 ft%s\n",
                       curcloud->cloud->amount,
                       curcloud->cloud->layer_altitude,
                       curcloud->cloud->layer_modifier);
            } else {
                // There were no clouds reported, don't print layer_altitude
                printf("%s%s\n", curcloud->cloud->amount, curcloud->cloud->layer_modifier);
            }
        }
		else printf("%15s %s at %d00 ft%s\n",
				" ",curcloud->cloud->amount, curcloud->cloud->layer_altitude, curcloud->cloud->layer_modifier);
	}
	if (!n) printf("\n");

	printf("Phenomena     : ");
	n = 0;
	for (curphenomenon = metar.phenomena; curphenomenon != NULL; curphenomenon=curphenomenon->next) {
		if (n++ == 0) printf("%s\n", curphenomenon->phenomena);
		else printf("%15s %s\n", " ",curphenomenon->phenomena);
	}
	if (!n) printf("\n");

    if (metar.maintenance_needed == MAINTENANCE_NEEDED){
        printf("WARNING: Maintenance is needed on this station.\n");

    }
    printf("\n");
}


int main(int argc, char* argv[]) {
	int  res=0;
    char *station_id;
	metar_t metar;
	noaa_t  noaa;

	/* get options */
	opterr=0;
	if (argc == 1) {
		usage(argv[0]);
		return 1;
	}

	while ((res = getopt(argc, argv, "hvdltc")) != -1) {
		switch (res) {
            case 'l':
				location=1;
				break;
			case 't':
                datetime=1;
				break;
			case 'd':
				decode=1;
				break;
            case 'c':
                category=1;
                break;
			case 'v':
				verbose=1;
				break;
            case '?':
            case 'h':
            default:
                usage(argv[0]);
                return 1;
		}
	}
    
    curl_global_init(CURL_GLOBAL_DEFAULT);

	// clear out metar and noaa
	memset(&metar, 0x0, sizeof(metar_t));
	memset(&noaa, 0x0, sizeof(noaa_t));

	while (optind < argc) {
        station_id = argv[optind++];
		res = download_Metar(station_id);
        if (res == 0) {
			if(!parse_NOAA_data(noaabuffer, &noaa)){
                /* print spaces for the date and time if that option is enabled */
                if(datetime) printf("                     ");
                printf("%s is not a valid ICAO airport identifier.\n", station_id);
                continue;
            }


			if(datetime){
                printf("%s ", noaa.date);
            }

            printf("%s", noaa.report);

            if(category){
                printf(" %s", noaa.category); /* if selected, this is printed at the end of the raw METAR */
            }

            printf("\n");

            if (decode) {
                parse_Metar(noaa.report, &metar);
                decode_Metar(metar);
                //FIXME cleanup all the things that were malloc'ed in the metar struct (phenomena, clouds ...)
            }

            if(location) {
                printf("Lat, Lon      : %.3f, %.3f\n", noaa.latitude, noaa.longitude);
                printf("Elevation     : %.1f Meters, %.1f Feet\n",
                       noaa.elevation_m,
                       meters_to_feet(noaa.elevation_m));
            }

		}
	}
    
    return 0;
}

// EOF
