/* metar.c -- metar decoder
   $Id: metar.c,v 1.6 2006/10/30 22:18:58 kees-guest Exp $
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
#include <regex.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "metar.h"

extern int verbose;


typedef struct cloud_dict_entry {
	const char *abbreviation;
	const char *description;
    const int  print_altitude;  // Tells us whether the cloud base is relevant and whether it should be displayed
                            // We should not print cloud bases when the sky is clear
                            // The cloud base is irrelevant is the dict entry is for a cloud layer_modifier (e.g. TCU)
} cloud_dict_entry;

/* Structures used to map abbreviations for clouds to their meaning */
cloud_dict_entry cloud_dict[] = {
		{"SKC", "Sky Clear (no clouds within sensors range)", DONT_PRINT_BASE},
		{"CLR", "Sky Clear Below 12000ft", DONT_PRINT_BASE},
		{"NSC", "No Significant Clouds below 5000ft/1500m AGL", DONT_PRINT_BASE},
		{"NCD", "No Clouds Detected below 5000ft/1500m AGL", DONT_PRINT_BASE},
		{"FEW", "Few clouds", PRINT_BASE},
		{"SCT", "Scattered clouds", PRINT_BASE},
		{"BKN", "Broken clouds", PRINT_BASE},
		{"OVC", "Overcast", PRINT_BASE},
		{"VV", "Vertical Visibility", PRINT_BASE},
		{"TCU", ", Towering Cumulus clouds in vicinity", NOT_APPLICABLE},
		{"CU", ", Cumulus clouds in vicinity", NOT_APPLICABLE},
		{"CB", ", Cumulonimbus clouds in vicinity", NOT_APPLICABLE},
		{"CBMAM", ", Cumulonimbus Mammatus in vicinity (expect turbulent air)", NOT_APPLICABLE},
		{"ACC", ", Altocumulus Castellatus (medium layer_altitude, vigorous instability)", NOT_APPLICABLE},
		{"CLD", ", Standing lenticular or rotor clouds", NOT_APPLICABLE}
};

#define LONGEST_CLOUD_DICT_KEY 5 // CBMAM is the longest item in the dict above

/* Structures used to map abbreviations for weather phenomena to their meaning
 *   For example, "TS" is an abbreviation for Thunderstorm
 *
 *   It may help to remember that phenomenon is the singular form of the word and
 *   phenomena is the plural form.
 */

struct phenomenon {
	const char *code;
	const char *description;
};

typedef struct phenomenon phenomenon_t;

struct phenomenon phenomena[] = {
	{"MI", "Shallow "},
	{"BL", "Blowing "},
	{"BC", "Patches "},
	{"SH", "Showers "},
	{"PR", "Partials "},
	{"DR", "Drifting "},
	{"TS", "Thunderstorm "},
	{"FZ", "Freezing "},
	{"DZ", "Drizzle "},
	{"IC", "Ice Crystals "},
	{"UP", "Unknown Precipitation "},
	{"RA", "Rain "},
	{"PL", "Ice Pellets "},
	{"SN", "Snow "},
	{"GR", "Hail "},
	{"SG", "Snow Grains "},
	{"GS", "Small hail/snow pellets "},
	{"BR", "Mist "},
	{"SA", "Sand "},
	{"FU", "Smoke "},
	{"HZ", "Haze "},
	{"FG", "Fog "},
	{"VA", "Volcanic Ash "},
	{"PY", "Spray "},
	{"DU", "Widespread Dust "},
	{"SQ", "Squall "},
	{"FC", "Funnel Cloud "},
	{"SS", "Sand storm "},
	{"DS", "Dust storm "},
	{"PO", "Well developed dust/sand swirls "},
	{"VC", "Vicinity "}
};

/* convert meters to feet */
double meters_to_feet(double meters){
	return meters * 3.370079;
}

/* Add a cloud to a list of clouds */
static void add_cloud(cloud_list_t **head, cloud_t *cloud) {
	cloud_list_t *current;

	if (*head == NULL) {
		*head = malloc(sizeof(cloud_list_t));
		current = *head;
		current->cloud = cloud;
		current->next = NULL;
		return;
	}
	current = *head;
	while (current->next != NULL)
		current = current->next;

	current->next = (cloud_list_t *)malloc(sizeof(cloud_list_t));
	current = current->next;
	current->cloud = cloud;
	current->next = NULL;
} // add_cloud

/* get the description of weather phenomena section of the METAR */
static cloud_dict_entry *decode_cloud_abbreviation(char *pattern) {
    int i=0;
    int num_entries = sizeof(cloud_dict) / sizeof(cloud_dict_entry);
    size_t pattern_length = strnlen(pattern, LONGEST_CLOUD_DICT_KEY);
    size_t key_length;
    size_t search_length;

    for (i=0; i < num_entries; i++) {
        key_length = strlen(cloud_dict[i].abbreviation);
        if (key_length >= pattern_length) {
            search_length = pattern_length;
        } else {
            search_length = key_length;
        }

        if (strncmp(pattern, cloud_dict[i].abbreviation, search_length) == 0)
            return &cloud_dict[i];
    }
    return NULL;
}
//FIXME add function to delete cloud_list_t (currently leak the memory)

/* Add Phenomenon */
static void add_phenomenon(phenomena_list_t **head, char *phenomenon) {
	phenomena_list_t *current;

	if (*head == NULL) {
		*head = malloc(sizeof(phenomena_list_t));
		current = *head;
		current->phenomena = phenomenon;
		current->next = NULL;
		return;
	}
	current = *head;
	while (current->next != NULL)
		current = current->next;

	current->next = (phenomena_list_t *)malloc(sizeof(phenomena_list_t));
	current = current->next;
	current->phenomena = phenomenon;
	current->next = NULL;
} // add_phenomenon

//FIXME add function to delete cloud_list_t (currently leaks the memory)

/* build the phenomena regexp patterns*/
static void build_phenomena_regex_patterns(char *pattern, int len) {
    char head[] = "^([+-]?)((";
    char tail[] = ")+)$";

	int i=0, p=0;
	int num_phenomenon = sizeof(phenomena) / sizeof(phenomenon_t);

    // figure out how many phenomena can be added after reserving room for head, tail and NUL termination.
    int max_chars_added = len - (int)strlen(head) - (int)strlen(tail) - 1;

    if(max_chars_added < 0){
        fprintf(stderr, "len parameter to build_phenomena_regex_pattern() is too small: %i\n", len);
        exit(EXIT_FAILURE);
    }

    strcpy(&pattern[0], head);

    // add head of pattern

	for (i=0; i < num_phenomenon; i++) {
		// add phenomenon to string if we have enough space left
		p = (int) strlen(pattern);
		if (p + strlen(phenomena[i].code) < max_chars_added)
			strcpy(&pattern[p], phenomena[i].code);

		// add separator
		p = (int) strlen(pattern);
		if (p < len) pattern[p++] = '|';
	}

	// remove last |
	pattern[strlen(pattern)-1] = 0;

    // add tail to pattern
    p = (int) strlen(pattern);
    strcpy(&pattern[p], tail);
}


/* get the description of weather phenomena section of the METAR */
static char *decode_phenomena(char *pattern) {
	int i=0;
	int size = sizeof(phenomena) / sizeof(phenomenon_t);

	for (i=0; i < size; i++)
		if (strncmp(pattern, phenomena[i].code, 2) == 0)
			return (char*) phenomena[i].description;

	return NULL;
}


/* Analyse the token which is provided and, when possible, set the
 * corresponding value in the metar struct
 */
#define TMP_SIZE 99
#define PHENOMENA_REGEX_SIZE 275
#define MAX_REGEX_MATCHES 5
static void analyse_token(char *token, metar_t *metar) {
	regex_t preg;
	regmatch_t pmatch[MAX_REGEX_MATCHES];
	int match_size;
    size_t string_length;
	char tmp[TMP_SIZE];
	char phenomena_regex_pattern[PHENOMENA_REGEX_SIZE];

	if (verbose) printf("Parsing token `%s'\n", token);

	// find station
	if (metar->station[0] == 0) {
		if (regcomp(&preg, "^([A-Z]+)$", REG_EXTENDED)) {
			perror("parseMetar");
			exit(errno);
		}
		if (!regexec(&preg, token, MAX_REGEX_MATCHES, pmatch, 0)) {
			match_size = pmatch[1].rm_eo - pmatch[1].rm_so;
			memcpy(metar->station, token+pmatch[1].rm_so,
				   (size_t) (match_size < 10 ? match_size : 10));
			if (verbose) printf("   Found station %s\n", metar->station);

			/* Free memory allocated to the pattern buffer by regcomp() */
			regfree(&preg);

			return;
		}
		/* Free memory allocated to the pattern buffer by regcomp() */
		regfree(&preg);

	}

	// find day/time
	if (metar->day == 0) {
		if (regcomp(&preg, "^([0-9]{2})([0-9]{4})Z$", REG_EXTENDED)) {
			perror("parseMetar");
			exit(errno);
		}
		if (!regexec(&preg, token, MAX_REGEX_MATCHES, pmatch, 0)) {
			match_size = pmatch[1].rm_eo - pmatch[1].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			memcpy(tmp, token+pmatch[1].rm_so, (size_t) (match_size < TMP_SIZE ? match_size : TMP_SIZE));
			sscanf(tmp, "%d", &metar->day);

			match_size = pmatch[2].rm_eo - pmatch[2].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			memcpy(tmp, token+pmatch[2].rm_so, (size_t) (match_size < TMP_SIZE ? match_size : TMP_SIZE));
			sscanf(tmp, "%d", &metar->time);
			if (verbose) printf("   Found Day/Time %d/%d\n",
					metar->day, metar->time);

			/* Free memory allocated to the pattern buffer by regcomp() */
			regfree(&preg);

			return;
		}
		/* Free memory allocated to the pattern buffer by regcomp() */
		regfree(&preg);

	} // daytime

	// find wind
    // FIXME parse when windspeed is greater than 6 knots and is variable (e.g. 23013KT 210V250)
    //       where wind direction varies between 210 and 250 degrees
	if (metar->winddir == 0) {
		if (regcomp(&preg, "^(VRB|[0-9]{3})([0-9]{2})(G[0-9]+)?(KT)$",
					REG_EXTENDED)) {
			perror("parseMetar");
			exit(errno);
		}
		if (!regexec(&preg, token, MAX_REGEX_MATCHES, pmatch, 0)) {
			match_size = pmatch[1].rm_eo - pmatch[1].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			if (match_size) {
				memcpy(tmp, token+pmatch[1].rm_so, (size_t) (match_size < TMP_SIZE ? match_size : TMP_SIZE));
				if (strstr(tmp, "VRB") == NULL) { // winddir
					sscanf(tmp, "%d", &metar->winddir);
				} else { // vrb
					metar->winddir = -1;
				}
			}

			match_size = pmatch[2].rm_eo - pmatch[2].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			if (match_size) {
				memcpy(tmp, token+pmatch[2].rm_so, (size_t) (match_size < TMP_SIZE ? match_size : TMP_SIZE));
				sscanf(tmp, "%d", &metar->windstr);
			}

			match_size = pmatch[3].rm_eo - pmatch[3].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			if (match_size) {
				memcpy(tmp, token+pmatch[3].rm_so+1, (size_t) (match_size < TMP_SIZE ? match_size - 1 : TMP_SIZE));
				sscanf(tmp, "%d", &metar->windgust);
			} else {
				metar->windgust = metar->windstr;
			}

			match_size = pmatch[4].rm_eo - pmatch[4].rm_so;
			if (match_size) {
				memcpy(&metar->windunit, token+pmatch[4].rm_so,
					   (size_t) (match_size < 5 ? match_size : 5));
			}

			if (verbose) printf("   Found Winddir/str/gust/unit %d/%d/%d/%s\n",
					metar->winddir, metar->windstr, metar->windgust,
					metar->windunit);

			/* Free memory allocated to the pattern buffer by regcomp() */
			regfree(&preg);

			return;
		}
		/* Free memory allocated to the pattern buffer by regcomp() */
		regfree(&preg);

	} // wind

	// find visibility
    // FIXME add ability to parse Runway visual range
    //
    //    Format:	Rdd/vvvvFT
    //    Rdd/vvvvVvvvvFT
    //    dd - the runway identifier. e.g. R02, or possibly R02L if the left runway is reported.
    //
    //            vvvvFT - the reported visual range in feet.
    //
    //            vvvvVvvvvFT - a range of visual ranges for that runway.
    //
    //            M - The actual value is less than the reported value.
    //            P - The actual value is more than the reported value.
    //
    //            Examples: R26L/2400FT -- Runway 26 Left has a range of 2400 ft.
    //            R08/0400V0800FT -- Runway 08 has a visual range between 400 and 800 feet.
    //

    if (metar->vis == 0) {
		if (regcomp(&preg, "^([0-9]+)(SM)?$", REG_EXTENDED)) {
			perror("parsemetar");
			exit(errno);
		}
		if (!regexec(&preg, token, MAX_REGEX_MATCHES, pmatch, 0)) {
			match_size = pmatch[1].rm_eo - pmatch[1].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			memcpy(tmp, token+pmatch[1].rm_so, (size_t) (match_size < TMP_SIZE ? match_size : TMP_SIZE));
			sscanf(tmp, "%d", &metar->vis);

			match_size = pmatch[2].rm_eo - pmatch[2].rm_so;
			if (match_size) {
				memset(tmp, 0x0, TMP_SIZE);
				memcpy(&metar->visunit, token+pmatch[2].rm_so,
					   (size_t) (match_size < 5 ? match_size : 5));
			} else
				strncpy(metar->visunit, "M", 1);

			if (verbose) printf("   Visibility range/unit %d/%s\n", metar->vis,
					metar->visunit);

			/* Free memory allocated to the pattern buffer by regcomp() */
			regfree(&preg);

			return;
		}
		/* Free memory allocated to the pattern buffer by regcomp() */
		regfree(&preg);

	} // visibility

	// find temperature and dewpoint
	if (metar->temp == 0) {
		if (regcomp(&preg, "^(M?)([0-9]+)/(M?)([0-9]+)$", REG_EXTENDED)) {
			perror("parsemetar");
			exit(errno);
		}
		if (!regexec(&preg, token, MAX_REGEX_MATCHES, pmatch, 0)) {
			match_size = pmatch[2].rm_eo - pmatch[2].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			memcpy(tmp, token+pmatch[2].rm_so, (size_t) (match_size < TMP_SIZE ? match_size : TMP_SIZE));
			sscanf(tmp, "%d", &metar->temp);

			match_size = pmatch[1].rm_eo - pmatch[1].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			memcpy(tmp, token+pmatch[1].rm_so, (size_t) (match_size < TMP_SIZE ? match_size : TMP_SIZE));
			if (strncmp(tmp, "M", 1) == 0) metar->temp = -metar->temp;

			match_size = pmatch[4].rm_eo - pmatch[4].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			memcpy(tmp, token+pmatch[4].rm_so, (size_t) (match_size < TMP_SIZE ? match_size : TMP_SIZE));
			sscanf(tmp, "%d", &metar->dewp);

			match_size = pmatch[3].rm_eo - pmatch[3].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			memcpy(tmp, token+pmatch[3].rm_so, (size_t) (match_size < TMP_SIZE ? match_size : TMP_SIZE));
			if (strncmp(tmp, "M", 1) == 0) metar->dewp = -metar->dewp;

			if (verbose)
				printf("   Temp/dewpoint %d/%d\n", metar->temp, metar->dewp);

			/* Free memory allocated to the pattern buffer by regcomp() */
			regfree(&preg);

			return;
		}
		/* Free memory allocated to the pattern buffer by regcomp() */
		regfree(&preg);

	} // temp

	// find qnh
	if (metar->qnh == 0) {
		if (regcomp(&preg, "^([QA])([0-9]+)$", REG_EXTENDED)) {
			perror("parsemetar");
			exit(errno);
		}
		if (!regexec(&preg, token, MAX_REGEX_MATCHES, pmatch, 0)) {
			match_size = pmatch[1].rm_eo - pmatch[1].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			memcpy(tmp, token+pmatch[1].rm_so, (size_t) (match_size < 5 ? match_size : 5));
			if (strncmp(tmp, "Q", 1) == 0)
				strncpy(metar->qnhunit, "hPa", 3);
			else if (strncmp(tmp, "A", 1) == 0) {
				strncpy(metar->qnhunit, "\"Hg", 3);
				metar->qnhfp = 2;
			}
			else
				strncpy(metar->qnhunit, "Unkn", 4);

			match_size = pmatch[2].rm_eo - pmatch[2].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			memcpy(tmp, token+pmatch[2].rm_so, (size_t) (match_size < TMP_SIZE ? match_size : TMP_SIZE));
			sscanf(tmp, "%d", &metar->qnh);

			if (verbose)
				printf("   Pressure/unit %d/%s\n", metar->qnh, metar->qnhunit);

			/* Free memory allocated to the pattern buffer by regcomp() */
			regfree(&preg);

			return;
		}
		/* Free memory allocated to the pattern buffer by regcomp() */
		regfree(&preg);

	} // qnh

	// multiple cloud layers possible
		// if you change the regex below, make sure you also change the cloud_dict at the top of the file
	if (regcomp(&preg, "^(SKC|CLR|NSC|NCD)$|^(FEW|SCT|BKN|OVC|VV)([0-9]{3})(TCU|CU|CB|CBMAM|ACC|CLD)?$", REG_EXTENDED)) {
		perror("parsemetar");
		exit(errno);
	}

	if (!regexec(&preg, token, MAX_REGEX_MATCHES, pmatch, 0)) {
		cloud_t *cloud = malloc(sizeof(cloud_t));
        cloud_dict_entry *cloud_dict;
		memset(cloud, 0x0, sizeof(cloud_t));

		// Handle case where no clouds were detected (SKC, CLR, NSC, NCD)
		match_size=pmatch[1].rm_eo - pmatch[1].rm_so;
		if (match_size > 0) {
            cloud_dict = decode_cloud_abbreviation(token + pmatch[1].rm_so);
            string_length = strlen(cloud_dict->description);
            cloud->amount = malloc(string_length+1);
            memcpy(cloud->amount, cloud_dict->description, string_length);
            cloud->amount[string_length] = 0x0; //Force NUL termination
            cloud->print_altitude = cloud_dict->print_altitude;
			cloud->layer_altitude = -1;   // base of cloud layer is irrelevant; no clouds were detected

            // no clouds detected means no layer modifier, put empty string into layer_modifier
            cloud->layer_modifier = malloc(1);
            cloud->layer_modifier[0] = 0x0; //Force NUL termination
		} else {
			// Handle case where clouds were detected
            //FIXME should I calculate the ceilings?

            cloud_dict = decode_cloud_abbreviation(token + pmatch[2].rm_so);
            string_length = strlen(cloud_dict->description);
            cloud->amount = malloc(string_length+1);
            memcpy(cloud->amount, cloud_dict->description, string_length);
            cloud->amount[string_length] = 0x0; //Force NUL termination
            cloud->print_altitude = cloud_dict->print_altitude;

            // Write base of cloud layer
			match_size = pmatch[3].rm_eo - pmatch[3].rm_so;
			memset(tmp, 0x0, TMP_SIZE);
			memcpy(tmp, token + pmatch[3].rm_so, (size_t) (match_size < 3 ? match_size : 3));
			sscanf(tmp, "%d", &cloud->layer_altitude);

            //Process pmatch[4] for cloud layer modifier TCU|CU|CB|CBMAM|ACC|CLD
            match_size = pmatch[4].rm_eo - pmatch[4].rm_so;
            if (match_size > 0){
                cloud_dict = decode_cloud_abbreviation(token + pmatch[4].rm_so);
                string_length = strlen(cloud_dict->description);
                cloud->layer_modifier = malloc(string_length+1);
                memcpy(cloud->layer_modifier, cloud_dict->description, string_length);
                cloud->layer_modifier[string_length] = 0x0; //Force NUL termination

            } else {
                // no modifier, put empty string into layer_modifier
                cloud->layer_modifier = malloc(strlen(""));
                memcpy(cloud->layer_modifier, "", strlen(""));
                cloud->layer_modifier[strlen("")] = 0x0; //Force NUL termination

            }
		}

		add_cloud(&metar->clouds, cloud);
		if (verbose)
			printf("   Cloud cover/alt %s/%d00\n", cloud->amount, cloud->layer_altitude);

		/* Free memory allocated to the pattern buffer by regcomp() */
		regfree(&preg);

		return;
	} // cloud

	/* Free memory allocated to the pattern buffer by regcomp() */
	regfree(&preg);

	// phenomena
	memset(phenomena_regex_pattern, 0x0, PHENOMENA_REGEX_SIZE);
    build_phenomena_regex_patterns(phenomena_regex_pattern, PHENOMENA_REGEX_SIZE);

	// cannot expand CAVOK abbreviation in the array because it is more than
	// 2 characters long and that screws up my algorithm - so we special case it here
	if (strstr(token, "CAVOK") != NULL) {
        add_phenomenon(&metar->phenomena, "Ceiling and visibility OK");
	}

	if (regcomp(&preg, phenomena_regex_pattern, REG_EXTENDED)) {
		perror("parsemetar");
		exit(errno);
	}

	if (!regexec(&preg, token, MAX_REGEX_MATCHES, pmatch, 0)) {
		#define PHENOMENON_STR_SIZE 99
		char *phenomenon_str;
		phenomenon_str = malloc(PHENOMENON_STR_SIZE);
		memset(phenomenon_str, 0x0, PHENOMENON_STR_SIZE);

		match_size=pmatch[1].rm_eo - pmatch[1].rm_so;

		memset(tmp, 0x0, TMP_SIZE);
		memcpy(tmp, token+pmatch[1].rm_so, (size_t) (match_size < 1 ? match_size : 1));
		if (tmp[0] == '-') strncpy(phenomenon_str, "Light ", PHENOMENON_STR_SIZE);
		else if (tmp[0] == '+') strncpy(phenomenon_str, "Heavy ", PHENOMENON_STR_SIZE);

		// split up in groups of 2 chars and decode per group
		match_size=pmatch[2].rm_eo - pmatch[2].rm_so;
		memset(tmp, 0x0, TMP_SIZE);
		memcpy(tmp, token+pmatch[2].rm_so, (size_t) (match_size < TMP_SIZE ? match_size : TMP_SIZE));

		int i=0;
		char code[2];
		while (i < strlen(tmp)) {
			memset(code, 0x0, 2);
			memcpy(code, tmp+i, 2);
			strncat(phenomenon_str, decode_phenomena(code), PHENOMENON_STR_SIZE - strlen(phenomenon_str));
			i += 2;
		}

		// remove trailing space and ensure nul termination
		phenomenon_str[strlen(phenomenon_str)-1]=0;
        add_phenomenon(&metar->phenomena, phenomenon_str);
		if (verbose)
			printf("   Phenomena %s\n", phenomenon_str);

		/* Free memory allocated to the pattern buffer by regcomp() */
		regfree(&preg);

		return;
	}

	/* Free memory allocated to the pattern buffer by regcomp() */
	regfree(&preg);

	// Search for '$' at the end of the METAR (indicates maintenance needed on station)
    if (strncmp(token, "$", 1) == 0){
        metar->maintenance_needed = MAINTENANCE_NEEDED;
    }

	if (verbose) printf("   Unmatched token = %s\n", token);
}


/* PUBLIC--
 * Parse the METAR contain in the report string. Place the parsed report in
 * the metar struct.
 */
void parse_Metar(char *report, metar_t *metar) {
	char *token;
	char *last;

	// clear results
	memset(metar, 0x0, sizeof(metar_t));

    // init maintenance_needed flag
    metar->maintenance_needed = MAINTENANCE_NOT_NEEDED;

	// strip trailing newlines
	while ((last = strrchr(report, '\n')) != NULL)
		memset(last, 0, 1);

	token = strtok(report, " ");
	while (token != NULL) {
		analyse_token(token, metar);
		token = strtok(NULL, " ");
	}

} // parse_Metar

/* Dates from the NOAA XML have the following format: 2016-09-24T21:35:00Z
 * This function replaces the 'T' with a space to make the format clearer.
 */
void clean_date(char *date){
	size_t size = strlen(date);
	size_t pos;
	for (pos=0; pos < size; pos++){
		if (date[pos] == 'T'){
			date[pos] = ' ';
		}
	}
}

/* used to get an Xpath nodeset from the XML doc containing NOAA data */
xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath){

	xmlXPathContextPtr context;
	xmlXPathObjectPtr result;

	context = xmlXPathNewContext(doc);
	if (context == NULL) {
		fprintf(stderr, "Error in xmlXPathNewContext\n");
		return NULL;
	}
	result = xmlXPathEvalExpression(xpath, context);
	xmlXPathFreeContext(context);
	if (result == NULL) {
		fprintf(stderr, "Error in xmlXPathEvalExpression\n");
		return NULL;
	}
	if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
		xmlXPathFreeObject(result);
		fprintf(stderr, "Xpath %s was not found in the XML data from NOAA.\n", xpath);
		return NULL;
	}
	return result;
}

/* parse the NOAA report contained in the noaa_data buffer. Place a parsed
 * data in the metar struct.
 *
 * Returns: 1 if noaa_data was parsed sucessfully
 *          0 if noaa_data was not parsed successfully
 */
int parse_NOAA_data(char *noaa_data, noaa_t *noaa) {
    xmlDocPtr doc;
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *data;
	int length;

    /* Xpath definitions used to location fields in the NOAA XML data */
    /* If multiple METARs are returned (several phenomena), use the first phenomenon in the data -- METAR[1] */
    xmlChar *dataXpath = (xmlChar*) "/response/data";
    xmlChar *rawXpath = (xmlChar*) "/response/data/METAR[1]/raw_text";
    xmlChar *obsTimeXpath = (xmlChar*) "/response/data/METAR[1]/observation_time";
    xmlChar *latXPath = (xmlChar*) "/response/data/METAR[1]/latitude";
    xmlChar *longXPath = (xmlChar*) "/response/data/METAR[1]/longitude";
    xmlChar *elevXPath = (xmlChar*) "/response/data/METAR[1]/elevation_m";
    xmlChar *categoryXPath = (xmlChar*) "/response/data/METAR[1]/flight_category";

    xmlChar *num_result_str;
    int num_results;

    /*
     * this initialize libXML2 and checks for potential ABI mismatches
     * between the version it was compiled for and the actual shared
     * library used.
     */
    LIBXML_TEST_VERSION

    /*
     * The document being in memory, it have no base per RFC 2396,
     * and the "noname.xml" argument will serve as its base.
     */
	length = (int)strlen(noaa_data);
    if (verbose) {
        printf("Input XML is %i bytes.\n", length);
    }
    if(length >= METAR_MAXSIZE) {
        if(verbose) printf("Too much data returned from NOAA. Check for correct ICAO airport code.\n");
        return 0;
    }

    doc = xmlReadMemory(noaa_data, length, "noname.xml", NULL, 0);
    if (doc == NULL) {
        fprintf(stderr, "Failed to parse data from NOAA\n");
        exit(EXIT_FAILURE);
    }

    /* The calls to getnodeset return a set of nodes for the Xpath.  However, we are only asking for the latest
     * METAR and should always get 1 node back in the nodesets.  Thus, only the first node in the set is used:
     * nodeset->nodetab[0]
     */

    result = getnodeset(doc, dataXpath);
    if(result){
        num_result_str = xmlGetProp(result->nodesetval->nodeTab[0], (xmlChar *)"num_results");
        num_results = atoi((const char *) num_result_str);
        if(verbose) printf("num_results = %i\n", num_results);

        xmlFree(num_result_str);
        xmlXPathFreeObject(result);
        if (num_results==0){
            xmlFreeDoc(doc);
            xmlCleanupParser();
            return 0;
        }
        if (num_results > 1) {
            if (verbose) printf("Got %i results from NOAA. Check the ICAO airport code.\n", num_results);
            xmlFreeDoc(doc);
            xmlCleanupParser();
            return 0;
        }

    }
    else {
        if(verbose) printf("Unable to interpret XML data from NOAA.\n");
        xmlFreeDoc(doc);
        xmlCleanupParser();
        return 0;
    }

	result = getnodeset(doc, rawXpath);
	if (result) {
		nodeset = result->nodesetval;
        data = xmlNodeListGetString(doc, nodeset->nodeTab[0]->xmlChildrenNode, 1);
        /* For security, copy only enough bytes to fill report, making sure string is NUL terminated */
        strncpy(noaa->report, (char *)data, sizeof(noaa->report)-1);
        noaa->report[sizeof(noaa->report)-1] = 0;
		xmlFree(data);
		xmlXPathFreeObject (result);
	}
    else {
        if(verbose) printf("Unable to find METAR in the XML data from NOAA.\n");
        xmlFreeDoc(doc);
        xmlCleanupParser();
        return 0;
    }

	result = getnodeset (doc, obsTimeXpath);
	if (result) {
		nodeset = result->nodesetval;
		data = xmlNodeListGetString(doc, nodeset->nodeTab[0]->xmlChildrenNode, 1);
		clean_date((char *) data);
		/* For security, copy only enough bytes to fill report, making sure string is NUL terminated */
		strncpy(noaa->date, (char *)data, sizeof(noaa->date)-1);
		noaa->date[sizeof(noaa->date)-1] = 0;
		xmlFree(data);

		xmlXPathFreeObject (result);
	}
	result = getnodeset (doc, latXPath);
	if (result) {
		nodeset = result->nodesetval;
		data = xmlNodeListGetString(doc, nodeset->nodeTab[0]->xmlChildrenNode, 1);
		noaa->latitude = strtod((const char*)data, NULL);
		xmlFree(data);

		xmlXPathFreeObject (result);
	}
	result = getnodeset (doc, longXPath);
	if (result) {
		nodeset = result->nodesetval;
        data = xmlNodeListGetString(doc, nodeset->nodeTab[0]->xmlChildrenNode, 1);
		noaa->longitude = strtod((const char*)data, NULL);
		xmlFree(data);

		xmlXPathFreeObject (result);
	}
	result = getnodeset (doc, elevXPath);
	if (result) {
		nodeset = result->nodesetval;
		data = xmlNodeListGetString(doc, nodeset->nodeTab[0]->xmlChildrenNode, 1);
		noaa->elevation_m = strtod((const char*)data, NULL);
		xmlFree(data);

		xmlXPathFreeObject (result);
	}
	result = getnodeset (doc, categoryXPath);
	if (result) {
		nodeset = result->nodesetval;
		data = xmlNodeListGetString(doc, nodeset->nodeTab[0]->xmlChildrenNode, 1);
		/* For security, copy only enough bytes to fill report, making sure string is NUL terminated */
		strncpy(noaa->category, (char *)data, sizeof(noaa->category)-1);
		noaa->category[sizeof(noaa->category)-1] = 0;
		xmlFree(data);

		xmlXPathFreeObject (result);
	}
	xmlFreeDoc(doc);
    /*
     * Cleanup function for the XML library.
     */
    xmlCleanupParser();
    /*
     * this is to debug memory for regression tests
     */
    xmlMemoryDump();

    return 1;
} // parse_NOAA_data
