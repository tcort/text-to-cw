 /*
    text-to-cw -- converts text into a morse code audio file
    Copyright (C) 2022, 2023, 2024  Thomas Cort

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, see <https://www.gnu.org/licenses/>.

    SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "version.h"

#define VERSION (TEXT_TO_CW_VERSION_STRING)
#define DEFAULT_FREQUENCY (600)
#define DEFAULT_WPM (18)
#define DEFAULT_FWPM DEFAULT_WPM
#define DEFAULT_SAMPLE_RATE (44100)
#define DEFAULT_CHANNELS (1)
#define DEFAULT_BPS (16)
#define DEFAULT_VOLUME (16384 * (0.50))
#define DEFAULT_COMPRESSION_LEVEL (8)
#define DEFAULT_VERIFY (1)

const int verify = DEFAULT_VERIFY;
const int volume = DEFAULT_VOLUME;
const int channels = DEFAULT_CHANNELS;
const int bps = DEFAULT_BPS;
const int compression_level = DEFAULT_COMPRESSION_LEVEL;
const double sample_rate = DEFAULT_SAMPLE_RATE;
double frequency = DEFAULT_FREQUENCY;

int16_t *result = NULL;
size_t result_len = 0;
size_t total_samples = 0;

static void write_result(int16_t *data, size_t len) {

	int16_t *new_result = NULL;
	size_t new_len = 0;

	new_len = result_len + len;

	new_result = (int16_t *) realloc(result, new_len * sizeof(int16_t));
	if (new_result == NULL) {
		fprintf(stderr, "malloc failed :(\n");
		exit(EXIT_FAILURE);
	}

	memcpy(new_result + result_len, data, len * sizeof(int16_t));

	result = new_result;
	result_len += len;
	total_samples = result_len;

}


/* useful timing details: https://morsecode.world/international/timing.html */
static int nsamples_unit(int wpm) {
	return sample_rate * (60.0 / (50.0 * wpm));
}

static int nsamples_dit(int wpm) { return 1 * nsamples_unit(wpm); }
static int nsamples_dah(int wpm) { return 3 * nsamples_unit(wpm); }
static int nsamples_intra_character_space(int wpm) { return 1 * nsamples_unit(wpm); }
static int nsamples_inter_character_space(int wpm) { return 3 * nsamples_unit(wpm); }
static int nsamples_inter_word_space(int wpm)      { return 5 * nsamples_unit(wpm); }
/* inter word space is 5 because there are nsamples_intra_character_space
   before and after the space to bring it up to 7 */

/* shape output waveform so sound isn't as harsh, rise and fall is 10% of dit */
static int nsamples_rise_time(int wpm) { return nsamples_dit(wpm) / 10; }
static int nsamples_fall_time(int wpm) { return nsamples_dit(wpm) / 10; }

static int16_t *dit_tone = NULL;
static size_t dit_tone_len = 0;
static int16_t *dah_tone = NULL;
static size_t dah_tone_len = 0;

static void make_tone(int16_t *samples, size_t nsamples, int rise_time, int fall_time) {
	int i;
	for (i = 0; i < nsamples; i++) {
		double t = (double) i / sample_rate;
		samples[i] = volume * sin(frequency*t*2*M_PI);

		if (i < rise_time) {
			samples[i] = samples[i] * (i*1.0/rise_time*1.0);
		} else if (i > nsamples - fall_time) {
			samples[i] = samples[i] * ((nsamples-i)*1.0/fall_time*1.0);
		}
	}
}

static int init_tone(int wpm) {

	int rise_time;
	int fall_time;

	rise_time = nsamples_rise_time(wpm);
	fall_time = nsamples_fall_time(wpm);

	dit_tone_len = nsamples_dit(wpm);
	dit_tone = (int16_t *) malloc(dit_tone_len * sizeof(int16_t));
	if (dit_tone == NULL) {
		return -1;
	}
	make_tone(dit_tone, dit_tone_len, rise_time, fall_time);

	dah_tone_len = nsamples_dah(wpm);
	dah_tone = (int16_t *) malloc(dah_tone_len * sizeof(int16_t));
	if (dah_tone == NULL) {
		return -1;
	}
	make_tone(dah_tone, dah_tone_len, rise_time, fall_time);

	return 0;
}

static void exit_tone(void) {
	free(dit_tone);
	dit_tone = NULL;
	dit_tone_len = 0;

	free(dah_tone);
	dah_tone = NULL;
	dah_tone_len = 0;
}


static void write_dit(void) {
	write_result(dit_tone, dit_tone_len);
}

static void write_dah(void) {
	write_result(dah_tone, dah_tone_len);
}

static void make_space(int16_t *samples, size_t nsamples) {
	int i;
	for (i = 0; i < nsamples; i++) {
		samples[i] = 0;
	}
}

static int16_t *inter_character_space = NULL;
static size_t inter_character_space_len = 0;
static int16_t *intra_character_space = NULL;
static size_t intra_character_space_len = 0;
static int16_t *inter_word_space = NULL;
static size_t inter_word_space_len = 0;

static int init_space(int wpm, int fwpm) {
	inter_character_space_len = nsamples_inter_character_space(fwpm);
	inter_character_space = (int16_t *) malloc(inter_character_space_len * sizeof(int16_t));
	if (inter_character_space == NULL) {
		return -1;
	}
	make_space(inter_character_space, inter_character_space_len);

	intra_character_space_len = nsamples_intra_character_space(wpm);
	intra_character_space = (int16_t *) malloc(intra_character_space_len * sizeof(int16_t));
	if (intra_character_space == NULL) {
		return -1;
	}
	make_space(intra_character_space, intra_character_space_len);

	inter_word_space_len = nsamples_inter_word_space(fwpm);
	inter_word_space = (int16_t *) malloc(inter_word_space_len * sizeof(int16_t));
	if (inter_word_space == NULL) {
		return -1;
	}
	make_space(inter_word_space, inter_word_space_len);

	return 0;
}

static void exit_space(void) {
	free(inter_character_space);
	inter_character_space = NULL;
	inter_character_space_len = 0;

	free(intra_character_space);
	intra_character_space = NULL;
	intra_character_space_len = 0;

	free(inter_word_space);
	inter_word_space = NULL;
	inter_word_space_len = 0;
}

static void write_inter_character_space(void) {
	write_result(inter_character_space, inter_character_space_len);
}

static void write_intra_character_space(void) {
	write_result(intra_character_space, intra_character_space_len);
}

static void write_inter_word_space(void) {
	write_result(inter_word_space, inter_word_space_len);
}

const char *alphabet[] = {
	"", /* 0 => '' */
	"", /* 1 => '' */
	"", /* 2 => '' */
	"", /* 3 => '' */
	"", /* 4 => '' */
	"", /* 5 => '' */
	"", /* 6 => '' */
	" ", /* 7 => '' */
	"", /* 8 => '' */
	"", /* 9 => '' */
	" ", /* 10 => '' */
	"", /* 11 => '' */
	"", /* 12 => '' */
	"", /* 13 => '' */
	"", /* 14 => '' */
	"", /* 15 => '' */
	"", /* 16 => '' */
	"", /* 17 => '' */
	"", /* 18 => '' */
	"", /* 19 => '' */
	"", /* 20 => '' */
	"", /* 21 => '' */
	"", /* 22 => '' */
	"", /* 23 => '' */
	"", /* 24 => '' */
	"", /* 25 => '' */
	"", /* 26 => '' */
	"", /* 27 => ' */
	"", /* 28 => '' */
	"", /* 29 => '' */
	"", /* 30 => '' */
	"", /* 31 => '' */
	" ", /* 32 => ' ' */
	"", /* 33 => '!' */
	"", /* 34 => '"' */
	"", /* 35 => '#' */
	"", /* 36 => '$' */
	"", /* 37 => '%' */
	"", /* 38 => '&' */
	"", /* 39 => ''' */
	"", /* 40 => '(' */
	"", /* 41 => ')' */
	"", /* 42 => '*' */
	"", /* 43 => '+' */
	"--..--", /* 44 => ',' */
	"", /* 45 => '-' */
	".-.-.-", /* 46 => '.' */
	"", /* 47 => '/' */
	"-----", /* 48 => '0' */
	".----", /* 49 => '1' */
	"..---", /* 50 => '2' */
	"...--", /* 51 => '3' */
	"....-", /* 52 => '4' */
	".....", /* 53 => '5' */
	"-....", /* 54 => '6' */
	"--...", /* 55 => '7' */
	"---..", /* 56 => '8' */
	"----.", /* 57 => '9' */
	"", /* 58 => ':' */
	"", /* 59 => ';' */
	"", /* 60 => '<' */
	"-...-", /* 61 => '=' */
	"", /* 62 => '>' */
	"..--..", /* 63 => '?' */
	"", /* 64 => '@' */
	".-", /* 65 => 'A' */
	"-...", /* 66 => 'B' */
	"-.-.", /* 67 => 'C' */
	"-..", /* 68 => 'D' */
	".", /* 69 => 'E' */
	"..-.", /* 70 => 'F' */
	"--.", /* 71 => 'G' */
	"....", /* 72 => 'H' */
	"..", /* 73 => 'I' */
	".---", /* 74 => 'J' */
	"-.-", /* 75 => 'K' */
	".-..", /* 76 => 'L' */
	"--", /* 77 => 'M' */
	"-.", /* 78 => 'N' */
	"---", /* 79 => 'O' */
	".--.", /* 80 => 'P' */
	"--.-", /* 81 => 'Q' */
	".-.", /* 82 => 'R' */
	"...", /* 83 => 'S' */
	"-", /* 84 => 'T' */
	"..-", /* 85 => 'U' */
	"...-", /* 86 => 'V' */
	".--", /* 87 => 'W' */
	"-..-", /* 88 => 'X' */
	"-.--", /* 89 => 'Y' */
	"--..", /* 90 => 'Z' */
	"", /* 91 => '[' */
	"", /* 92 => '\' */
	"", /* 93 => ']' */
	"", /* 94 => '^' */
	"", /* 95 => '_' */
	"", /* 96 => '`' */
	".-", /* 97 => 'a' */
	"-...", /* 98 => 'b' */
	"-.-.", /* 99 => 'c' */
	"-..", /* 100 => 'd' */
	".", /* 101 => 'e' */
	"..-.", /* 102 => 'f' */
	"--.", /* 103 => 'g' */
	"....", /* 104 => 'h' */
	"..", /* 105 => 'i' */
	".---", /* 106 => 'j' */
	"-.-", /* 107 => 'k' */
	".-..", /* 108 => 'l' */
	"--", /* 109 => 'm' */
	"-.", /* 110 => 'n' */
	"---", /* 111 => 'o' */
	".--.", /* 112 => 'p' */
	"--.-", /* 113 => 'q' */
	".-.", /* 114 => 'r' */
	"...", /* 115 => 's' */
	"-", /* 116 => 't' */
	"..-", /* 117 => 'u' */
	"...-", /* 118 => 'v' */
	".--", /* 119 => 'w' */
	"-..-", /* 120 => 'x' */
	"-.--", /* 121 => 'y' */
	"--..", /* 122 => 'z' */
	"", /* 123 => '{' */
	"", /* 124 => '|' */
	"", /* 125 => '}' */
	"", /* 126 => '~' */
	"", /* 127 => '' */
	"", /* 128 => '' */
	"", /* 129 => '' */
	"", /* 130 => '' */
	"", /* 131 => '' */
	"", /* 132 => '' */
	"", /* 133 => '' */
	"", /* 134 => '' */
	"", /* 135 => '' */
	"", /* 136 => '' */
	"", /* 137 => '' */
	"", /* 138 => '' */
	"", /* 139 => '' */
	"", /* 140 => '' */
	"", /* 141 => '' */
	"", /* 142 => '' */
	"", /* 143 => '' */
	"", /* 144 => '' */
	"", /* 145 => '' */
	"", /* 146 => '' */
	"", /* 147 => '' */
	"", /* 148 => '' */
	"", /* 149 => '' */
	"", /* 150 => '' */
	"", /* 151 => '' */
	"", /* 152 => '' */
	"", /* 153 => '' */
	"", /* 154 => '' */
	"", /* 155 => '' */
	"", /* 156 => '' */
	"", /* 157 => '' */
	"", /* 158 => '' */
	"", /* 159 => '' */
	"", /* 160 => ' ' */
	"", /* 161 => '¡' */
	"", /* 162 => '¢' */
	"", /* 163 => '£' */
	"", /* 164 => '¤' */
	"", /* 165 => '¥' */
	"", /* 166 => '¦' */
	"", /* 167 => '§' */
	"", /* 168 => '¨' */
	"", /* 169 => '©' */
	"", /* 170 => 'ª' */
	"", /* 171 => '«' */
	"", /* 172 => '¬' */
	"", /* 173 => '' */
	"", /* 174 => '®' */
	"", /* 175 => '¯' */
	"", /* 176 => '°' */
	"", /* 177 => '±' */
	"", /* 178 => '²' */
	"", /* 179 => '³' */
	"", /* 180 => '´' */
	"", /* 181 => 'µ' */
	"", /* 182 => '¶' */
	"", /* 183 => '·' */
	"", /* 184 => '¸' */
	"", /* 185 => '¹' */
	"", /* 186 => 'º' */
	"", /* 187 => '»' */
	"", /* 188 => '¼' */
	"", /* 189 => '½' */
	"", /* 190 => '¾' */
	"", /* 191 => '¿' */
	"", /* 192 => 'À' */
	"", /* 193 => 'Á' */
	"", /* 194 => 'Â' */
	"", /* 195 => 'Ã' */
	"", /* 196 => 'Ä' */
	"", /* 197 => 'Å' */
	"", /* 198 => 'Æ' */
	"", /* 199 => 'Ç' */
	"", /* 200 => 'È' */
	"", /* 201 => 'É' */
	"", /* 202 => 'Ê' */
	"", /* 203 => 'Ë' */
	"", /* 204 => 'Ì' */
	"", /* 205 => 'Í' */
	"", /* 206 => 'Î' */
	"", /* 207 => 'Ï' */
	"", /* 208 => 'Ð' */
	"", /* 209 => 'Ñ' */
	"", /* 210 => 'Ò' */
	"", /* 211 => 'Ó' */
	"", /* 212 => 'Ô' */
	"", /* 213 => 'Õ' */
	"", /* 214 => 'Ö' */
	"", /* 215 => '×' */
	"", /* 216 => 'Ø' */
	"", /* 217 => 'Ù' */
	"", /* 218 => 'Ú' */
	"", /* 219 => 'Û' */
	"", /* 220 => 'Ü' */
	"", /* 221 => 'Ý' */
	"", /* 222 => 'Þ' */
	"", /* 223 => 'ß' */
	"", /* 224 => 'à' */
	"", /* 225 => 'á' */
	"", /* 226 => 'â' */
	"", /* 227 => 'ã' */
	"", /* 228 => 'ä' */
	"", /* 229 => 'å' */
	"", /* 230 => 'æ' */
	"", /* 231 => 'ç' */
	"", /* 232 => 'è' */
	"", /* 233 => 'é' */
	"", /* 234 => 'ê' */
	"", /* 235 => 'ë' */
	"", /* 236 => 'ì' */
	"", /* 237 => 'í' */
	"", /* 238 => 'î' */
	"", /* 239 => 'ï' */
	"", /* 240 => 'ð' */
	"", /* 241 => 'ñ' */
	"", /* 242 => 'ò' */
	"", /* 243 => 'ó' */
	"", /* 244 => 'ô' */
	"", /* 245 => 'õ' */
	"", /* 246 => 'ö' */
	"", /* 247 => '÷' */
	"", /* 248 => 'ø' */
	"", /* 249 => 'ù' */
	"", /* 250 => 'ú' */
	"", /* 251 => 'û' */
	"", /* 252 => 'ü' */
	"", /* 253 => 'ý' */
	"", /* 254 => 'þ' */
	"", /* 255 => 'ÿ' */
};

static void write_character(unsigned char c) {
	int i;
	const char *s = alphabet[c];

	if (s == NULL || s[0] == '\0') return;
	for (i = 0; s[i] != '\0'; i++) {
		if (i != 0) {
			write_intra_character_space();
		}
		switch (s[i]) {
			case ' ':
				write_inter_word_space();
				break;
			case '.':
				write_dit();
				break;
			case '-':
				write_dah();
				break;
		}
	}
}

static void show_version(FILE *out, int exit_code) {
	fprintf(out, "text-to-cw v%s\n", VERSION);
	exit(exit_code);
}

static void show_usage(FILE *out, int exit_code) {

	fprintf(out, "usage: text-to-cw INPUT.TXT OUTPUT.FLAC\n");
	fprintf(out, "\n");
	fprintf(out, "-f NUM                Farnsworth spacing words per minute. Default %d\n", DEFAULT_FWPM);
	fprintf(out, "-h                    Display this help information and exit\n");
	fprintf(out, "-t NUM                Frequency of the generated tone in Hertz. Default %d\n", DEFAULT_FREQUENCY);
	fprintf(out, "-V                    Display version information and exit\n");
	fprintf(out, "-w NUM                Words per minute. Default %d\n", DEFAULT_WPM);
	fprintf(out, "\n");

	show_version(out, exit_code);
}

/**** ****/

/* example_c_encode_file - Simple FLAC file encoder using libFLAC
 * Copyright (C) 2007-2009  Josh Coalson
 * Copyright (C) 2011-2024  Xiph.Org Foundation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <inttypes.h>
#include <FLAC/metadata.h>
#include <FLAC/stream_encoder.h>

#define READSIZE (1024)

static FLAC__byte buffer[READSIZE * (DEFAULT_BPS/8) * DEFAULT_CHANNELS];
static FLAC__int32 pcm[READSIZE * DEFAULT_CHANNELS];

static void encode_result(char *filepath) {

	FILE *fin = NULL;
	FLAC__bool ok = true;
	FLAC__StreamEncoder *encoder = 0;
	FLAC__StreamEncoderInitStatus init_status;

	/* TC: sample_rate, channels, bps, total_samples are globals ready to go*/

	fin = fmemopen(result, sizeof(int16_t) * result_len, "r");
	if (fin == NULL) {
		fprintf(stderr, "ERROR: could not read resulting audio\n");
		exit(EXIT_FAILURE);
	}

	if ((encoder = FLAC__stream_encoder_new()) == NULL) {
		fprintf(stderr, "ERROR: allocating encoder\n");
		exit(EXIT_FAILURE);
	}

        ok &= FLAC__stream_encoder_set_verify(encoder, verify ? true : false);
        ok &= FLAC__stream_encoder_set_compression_level(encoder, compression_level);
        ok &= FLAC__stream_encoder_set_channels(encoder, channels);
        ok &= FLAC__stream_encoder_set_bits_per_sample(encoder, bps);
        ok &= FLAC__stream_encoder_set_sample_rate(encoder, sample_rate);
        ok &= FLAC__stream_encoder_set_total_samples_estimate(encoder, total_samples);

        /* initialize encoder */
        if (ok) {
                init_status = FLAC__stream_encoder_init_file(encoder, filepath, /*progress_callback*/NULL, /*client_data=*/NULL);
                if (init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
                        fprintf(stderr, "ERROR: initializing encoder: %s\n", FLAC__StreamEncoderInitStatusString[init_status]);
                        ok = false;
                }
        }


        /* read blocks of samples from WAVE file and feed to encoder */
        if (ok) {
                size_t left = (size_t)total_samples;
                while(ok && left) {
                        size_t need = (left>READSIZE? (size_t)READSIZE : (size_t)left);
			size_t count;

			count = fread(buffer, channels*(bps/8), need, fin);
                        if(count != need) {
                                fprintf(stderr, "ERROR: reading from buffer result_len=%lu read=%lu need=%lu left=%lu size_per_elem=%d\n", result_len, count, need, left, channels*(bps/8));
                                ok = false;
                        } else {
                                /* convert the packed little-endian 16-bit PCM samples from WAVE into an interleaved FLAC__int32 buffer for libFLAC */
                                size_t i;
                                for (i = 0; i < need*channels; i++) {
					pcm[i] = (FLAC__int32)(((FLAC__int16)(FLAC__int8)buffer[2*i+1] << 8) | (FLAC__int16)buffer[2*i]);
                                }
                                /* feed samples to encoder */
                                ok = FLAC__stream_encoder_process_interleaved(encoder, pcm, need);
                        }
                        left -= need;
                }
        }

        ok &= FLAC__stream_encoder_finish(encoder);


        fprintf(stderr, "encoding: %s\n", ok? "succeeded" : "FAILED");

        FLAC__stream_encoder_delete(encoder);
        fclose(fin);
}



/***** *****/

int main(int argc, char *argv[]) {

	FILE *input = NULL;
	char ch = '\0';
	int wpm = DEFAULT_WPM;
	int fwpm = 0;
	int rc = 0;
	int i = 0;

	while ((ch = getopt(argc, argv, "f:ht:Vw:")) != -1) {

		switch (ch) {
			case 'f':
				fwpm = atoi(optarg);
				fwpm = fwpm < 1 || fwpm > 100 ? 0 : fwpm;
				break;
			case 'h':
				show_usage(stdout, EXIT_SUCCESS);
				break;
			case 't':
				frequency = atoi(optarg);
				frequency = frequency < 60 || frequency > 3000 ? DEFAULT_FREQUENCY : frequency;
				break;
			case 'V':
				show_version(stdout, EXIT_SUCCESS);
				break;
			case 'w':
				wpm = atoi(optarg);
				wpm = wpm < 1 || wpm > 100 ? DEFAULT_WPM : wpm;
				break;
			default:
				show_usage(stderr, EXIT_FAILURE);
				break;
		}
	}

	if (fwpm == 0) {
		fwpm = wpm;
	}

	argc -= optind;
	argv += optind;

	if (argc != 2) {
		show_usage(stderr, EXIT_FAILURE);
	}

	input = fopen(argv[0], "r");
	if (input == NULL) {
		fprintf(stderr, "Could not open input file '%s'\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	rc = init_space(wpm, fwpm);
	if (rc == -1) {
		fprintf(stderr, "Failed to initialize space\n");
		exit(EXIT_FAILURE);
	}

	rc = init_tone(wpm);
	if (rc == -1) {
		fprintf(stderr, "Failed to initialize tones\n");
		exit(EXIT_FAILURE);
	}

	for (i = 0; (ch = getc(input)) != EOF; i++) {
		if (i != 0) {
			write_inter_character_space();
		}
		write_character(ch);
	}

	exit_tone();
	exit_space();

	fclose(input);

	encode_result(argv[1]);

	if (result != NULL) {
		free(result);
		result_len = 0;
	}

	exit(EXIT_SUCCESS);
}
