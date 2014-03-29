#include "mod2mml.h"
#include <getopt.h>
#include <assert.h>
#include <ctype.h>

#define DEBUG 0

/* Signature variables */

#define SIGNATURE_OFFSET 1080
#define SIGNATURE_LENGTH 4
char signature[SIGNATURE_LENGTH];

/* Patterns variables */

#define PATTERN_ARRAY_OFFSET 952
#define PATTERN_ARRAY_LENGTH 128
char pattern_array[PATTERN_ARRAY_LENGTH];

/* Song length variables */

#define SONG_LENGTH_OFFSET 950
unsigned char song_length;

/* Module related variables */

#define ROW_NUMBER_PER_PATTERN 64
#define ROW_CHANNEL_DATA_LENGTH 4
#define PATTERN_DATA_OFFSET 1084
#define MAX_CHANNEL 8

int nb_instrument;		/* maximum number of instrument available in the song */
int current_instrument;		/* current converted instrument */

int current_row;		/* current position within the pattern */

int nb_channel;			/* number of channel per pattern */
int current_channel;		/* current position within the row */

int current_song_position;	/* Current handled pattern */

int speed;			/* Speed of the song (the smaller, the fastest in fact) */
int bpm;			/* Number of Beat Per Minut */

#if 0
int convertion_inst[ /*MAX_INSTRUMENT */ 32];	/* corresponding pce instrument
						 * for each module instrument */
#endif
int instrument_map[32];
int percussion_map[32];
sample_info samples[32];

unsigned char pce_inst[64][32];	/* library builtin samples, normalized to 0..31 */

Channel_info channel[MAX_CHANNEL];	/* info concerning each of the channel */

void (*handle_note) (int, int, int, int);
				/* The function that convert
				 * the current note data into
				 * something (display, mml, bytecode ...)
				 */

void (*finish_parsing) ();
				/* Conclude the parsing for a
				 * given channel (global var argument)
				 */

/* period variables */
#define NB_OCTAVE 5
#define OCTAVE_OFFSET 2
#define NOTE_PER_OCTAVE 12

const int ft_period[NB_OCTAVE * NOTE_PER_OCTAVE] = {
	1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 906,
/*	C-0   C#0   D-0   D#0   E-0   F-0   F#0   G-0   G#0   A-0   A#0   B-0 */

	856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
/*	C-1   C#1   D-1   D#1   E-1   F-1   F#1   G-1   G#1   A-1   A#1   B-1 */

	428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
/*	C-2   C#2   D-2   D#2   E-2   F-2   F#2   G-2   G#2   A-2   A#2   B-2 */

	214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
/*	C-3   C#3   D-3   D#3   E-3   F-3   F#3   G-3   G#3   A-3   A#3   B-3 */

	107, 101, 95, 90, 85, 80, 75, 71, 67, 63, 60, 56
/*	C-4   C#4   D-4   D#4   E-4   F-4   F#4   G-4   G#4   A-4   A#4   B-4 */
};


/* Others variables */

#define LOG_FILENAME "mod2mml.log"
int nb_warning;			/* Number of non fatal errors */
char *input_filename;		/* Name of the current module */
char output_filename[256];	/* Name of the file used to output result */
char track_name[256];

/*
 * Little memento about fast tracker effects (from memory plus ft2 help)
 *
 * 0 -> arpegio
 *      3 notes will be played, one on each tick. The first one is the one
 *      indicated in the period field, the second will be this one plus
 *      (data >> 4) half tone and the last one is (data & 7) half tone
 *      superior to the first one
 * 1 -> portamento up
 * 2 -> portamento down
 * 3 -> tone portamento
 *      the sound slides toward the indicated period
 * 4 -> vibrato (may be non standard effect)
 * 5 -> tone portamento + volume slide (may be non standard effect)
 * 6 -> vibrato + volume slide (may be non standard effect)
 * 7 -> tremolo (may be non standard effect)
 * 8 -> set panning position
 *      0 is the leftmost position, and 0xff the rightmost one
 * 9 -> sample offset
 *      sound begins at position 0x100 * data
 * A -> volume slide
 *      higher nibble is slide up speed while lower nibble is slide down speed
 * B -> position jump
 *      jump to beginning of pattern number "data"
 * C -> set volume
 *      0x40 is the maximum
 * D -> pattern break
 *      jump to next pattern, at row "data"
 * E -> lots of non standard effects (higher nibble for type of effect, data
 *	in lower nibble)
 * F -> set speed
 *      if (data < 32) [ <= 32 ? ]
 *      then sets song speed else sets BPM
 */

#define FX_ARPEGGIO 0x0
#define FX_PORTAMENTO_UP 0x1
#define FX_PORTAMENTO_DOWN 0x2
#define FX_TONE_PORTAMENTO 0x3
#define FX_VIBRATO 0x4
#define FX_TONE_PORTAMENTO_VOLUME_SLIDE 0x5
#define FX_VIBRATO_VOLUME_SLIDE 0x6
#define FX_TREMOLO 0x7
#define FX_PANNING_POSITION 0x8
#define FX_SAMPLE_OFFSET 0x9
#define FX_VOLUME_SLIDE 0xA
#define FX_POSITION_JUMP 0xB
#define FX_VOLUME 0xC
#define FX_PATTERN_BREAK 0xD
#define FX_EXTENDED 0xE
#define FX_SPEED 0xF

/*****************************************************************************

    Function: convert_period

    Description: transform a period value into alpha/octave notation
    Parameters: int period
		char* alpha, the note in the octave ( 0 -> 'C' ,
						      1 -> 'C#',
						      2 -> 'D' ,
						      ...)
		char* octave
    Return: nothing

*****************************************************************************/
void convert_period(int period, char *alpha, char *octave)
{
	int index;

	/* In order to find the alpha/octave representation, we need to get the index
	 * of the period value egal to the one given in argument
	 * Note: since the array is sorted, we could even make a dichotomical
	 * research, much faster than the current non optimised linear one
	 * N.B. : I've slightly improved speed by putting a two scale linear
	 * research, first a rough one which searches for the octave and then a
	 * more fine one that looks out for the actual period.
	 */

	index = 0;

	while ((index < NB_OCTAVE * NOTE_PER_OCTAVE)
	       && (ft_period[index] > period))
		index += NOTE_PER_OCTAVE;

	if ((index == 0) && (ft_period[index] < period)) {	/* period too low */
		nb_warning++;
		log_warning("Note too low (%d > %d [C-%d] )",
			    period, ft_period[0], OCTAVE_OFFSET);
		*alpha = 0;
		*octave = 0;
		return;
	}

	index -= NOTE_PER_OCTAVE;

	while ((index < NB_OCTAVE * NOTE_PER_OCTAVE)
	       && (ft_period[index] > period))
		index++;

	if (index >= NB_OCTAVE * NOTE_PER_OCTAVE) {	/* period too high */
		nb_warning++;
		log_warning("Note too high (%d < %d [B-%d] )",
			    period,
			    ft_period[NB_OCTAVE * NOTE_PER_OCTAVE - 1],
			    OCTAVE_OFFSET + NB_OCTAVE);
		*alpha = 11;
		*octave = NB_OCTAVE - 1;
		return;
	}

	*alpha = index % NOTE_PER_OCTAVE;
	*octave = OCTAVE_OFFSET + (index / NOTE_PER_OCTAVE);
	return;

}

char out_ch[MAX_CHANNEL][100000];
char *out_ch_ptr[MAX_CHANNEL];

#define outmem(x...) out_ch_ptr[current_channel] += sprintf(out_ch_ptr[current_channel], x)

/*****************************************************************************

    Function: handle_note_mml

    Description: try to convert note in to our pseudo mml language
    Parameters: ( int current_channel from global variables )
		( int current_row from global variables )

		int period, data about the "height" (not sure this term is
				     the right one in english) of the note
		int instrument
		int effect_id, cf table above
		int effect_data
    Return: nothing

*****************************************************************************/
void handle_note_mml(int period, int instrument, int effect_id, int effect_data)
{
	char alpha, octave;
	int old_vol;
	static int prev_octave[MAX_CHANNEL] = { -1, -1, -1, -1 };
	static int charcnt[MAX_CHANNEL] = { 0 };
	static int rests[MAX_CHANNEL] = { 0 };
	static int last_instrument[MAX_CHANNEL] = {-1, -1, -1, -1};
	const char *alpha_to_disp[NOTE_PER_OCTAVE] =
	    { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };

	switch (effect_id) {
	case FX_VOLUME:
		old_vol = channel[current_channel].volume;
		if (effect_data >> 1 > 31) {
			channel[current_channel].volume = 31;
			nb_warning++;
			log_warning("Volume too high (0X%02X > 0X40)",
				    effect_data);
		} else
			channel[current_channel].volume = effect_data >> 1;
		if (channel[current_channel].volume != old_vol) {
			outmem("V%d ", channel[current_channel].volume);
			/* XXX: This may go in between a note and a length
			   modifier; workaround: truncate the note. */
			last_instrument[current_channel] = -1;
		}
		break;

	case FX_PATTERN_BREAK:
	case FX_POSITION_JUMP:
	case FX_SPEED:
		/* These are global effects and shouldn't trigger the warning for
		 * unsupported effects
		 */
		break;

	case FX_ARPEGGIO:
		if (effect_data == 0)
			break;

	default:
		nb_warning++;
		log_warning("Unsupported effect 0X%X", effect_id);
	}

	if (period != 0) {
		if (rests[current_channel] > 0) {
			if (last_instrument[current_channel] != -1) {
				/* If the last note played is longer than a row, we have to
				   extend it into the rest, i.e. we have to add a number
				   to extend the note and reduce the number of rests. */
				int ticks;
				sample_info *si = &samples[last_instrument[current_channel]];
				ticks = si->length / 1024;	/* This is a rather uneducated guess. */
				/* The note may at most last for as long as the following
				   rest. */
				if (ticks - 1 > rests[current_channel])
					ticks = rests[current_channel] + 1;
				/* The longest note is 1/1, i.e. 16 ticks. */
				if (ticks > 16)
					ticks = 16;
				else if (ticks < 1)	/* XXX: Shorter notes are possible. */
					ticks = 1;
				/* Go down to the nearest power of two. */
				/* XXX: Generate dot notes if appropriate. */
				while (__builtin_popcount(ticks) > 1)
					ticks--;
				//printf("lastinst %d samlen %d ticks %d\n", last_instrument[current_channel], si->length, ticks);
				assert(rests[current_channel] >= ticks - 1);
				if (ticks > 1) {
					/* Make sure we're following a note. */
					assert(isalpha(out_ch_ptr[current_channel][-1]) || out_ch_ptr[current_channel][-1] == '#');
					outmem("%d ", 16 / ticks);
					rests[current_channel] -= ticks - 1;
					assert(rests[current_channel] >= 0);
				}
				else
					outmem(" ");
			}
			while (rests[current_channel] >= 16) {
				outmem("R1 ");
				rests[current_channel] -= 16;
			}
			while (rests[current_channel] >= 8) {
				outmem("R2 ");
				rests[current_channel] -= 8;
			}
			while (rests[current_channel] >= 4) {
				outmem("R4 ");
				rests[current_channel] -= 4;
			}
			while (rests[current_channel] >= 2) {
				outmem("R8 ");
				rests[current_channel] -= 2;
			}
			while (rests[current_channel] > 0) {
				outmem("R ");
				rests[current_channel]--;
			}
			last_instrument[current_channel] = -1;
		}
		if (++charcnt[current_channel] > 20) {
			outmem("\n");
			charcnt[current_channel] = 0;
		}
		if (percussion_map[instrument]) {
			channel[current_channel].percussion++;
		}
		if (channel[current_channel].percussion && percussion_map[instrument]) {
			if (channel[current_channel].mode != 1) {
				channel[current_channel].mode = 1;
				outmem("@M1 ");
			}
			if (instrument != channel[current_channel].instrument) {
				channel[current_channel].instrument = instrument;
			}
			int drum = percussion_map[channel[current_channel].instrument];
			if (drum > 12 || drum < 1)
				drum = 1;
			outmem("%s ", alpha_to_disp[drum - 1]);
			if (channel[current_channel].volume != 31) {
				outmem("V31 ");
				channel[current_channel].volume = 31;
			}
			last_instrument[current_channel] = -1;
		}
		else {
			if (channel[current_channel].mode != 0) {
				channel[current_channel].mode = 0;
				outmem("@M0 ");
			}
			if (instrument != channel[current_channel].instrument) {
				outmem("@%d ", instrument_map[instrument]);
				channel[current_channel].instrument = instrument;
				if (channel[current_channel].volume != 31) {
					outmem("V31 ");
					channel[current_channel].volume = 31;
				}
			}
			convert_period(period, &alpha, &octave);
			if (octave != prev_octave[current_channel]) {
				if (octave == prev_octave[current_channel] + 1)
					outmem("> ");
				else if (octave == prev_octave[current_channel] - 1)
					outmem("< ");
				else
					outmem("O%d ", octave);
				prev_octave[current_channel] = octave;
			}
			outmem("%s", alpha_to_disp[alpha]);
			last_instrument[current_channel] = instrument;
		}
	} else {
		rests[current_channel]++;
	}

}

/*****************************************************************************

    Function: convert_row_to_duration

    Description: convert a number of row into a number of vertical
		 synchronisation waiting based on the speed and bpm of
		 the song
    Parameters: int row, the number of row
    Return: the number of vsync to wait

*****************************************************************************/
int convert_row_to_duration(int row)
{
	/*
	 * more experimentations must be intended here to determine a correct
	 * correspondancy between the speed, bpm, nb of row to wait for and
	 * duration in vsync unit
	 * We must keep in mind that in order to keep channels synchronised one
	 * with others, the following egality must always remain true :
	 * duration(a) + duration(b) = duration(a+b)
	 * Considered that we can't really adapt every module files already existing
	 * (taken in account the lack of sample "quality"), a first approach could
	 * be to ignore speed and bpm changes in the module file and assume a
	 * constant vsync duration for 1 row and thus, we would only multiply this
	 * constant by the number of row wanted and we would surely keep channels
	 * synchronised
	 */

	/*
	   return (speed * 15 / bpm) * row;
	 */
	return 6 * row;
}

/*****************************************************************************

    Function: output

    Description: output the given value, taking care of making asm lines
		 not too long and outputing duration opcode if needed
    Parameters: FILE* f, the file to output the result into (already opened)
		int channel_number, the channel number (to seek the volume)
		unsigned char opcode, the opcode to write
    Return: nothing

*****************************************************************************/
void output(FILE * f, int channel_number, unsigned char opcode)
{
	if (channel[channel_number].last_note != 0) {
		int duration =
		    convert_row_to_duration(channel[channel_number].last_note) -
		    1;

#if DEBUG > 1
		static unsigned long total_duration_elapsed[8] =
		    { 0, 0, 0, 0, 0, 0, 0, 0 };

		total_duration_elapsed[channel_number] += duration;

		fprintf(stderr,
			"Channel %d : %ld vsync elapsed\n",
			channel_number, total_duration_elapsed[channel_number]);

#endif

		channel[channel_number].last_note = 0;

		while (duration >= 63) {
			channel[channel_number].col_written++;
			fprintf(f, "%d,", 128 + 63);
			duration -= 63;
		}

		if (duration != 0) {
			channel[channel_number].col_written++;
			fprintf(f, "%d,", 128 + duration);
		}

	}

	fprintf(f, "%d", opcode);
	channel[channel_number].col_written++;

	if (channel[channel_number].col_written > 8) {
		channel[channel_number].col_written = 0;
		fprintf(f, "\n\tdb ");
	} else
		fprintf(f, ",");

}

/*****************************************************************************

    Function: gen_d2z_volume

    Description: output the opcode corresponding to volume in the given file for
		 the given channel
    Parameters: FILE* f, the file to output the result into (already opened)
		int channel_number, the channel number (to seek the volume)
    Return: nothing

*****************************************************************************/
void gen_d2z_volume(FILE * f, int channel_number)
{
	unsigned char volume = channel[channel_number].volume;

	if (volume > 31)
		volume = 31;

	output(f, channel_number, 192 + volume);
}

/*****************************************************************************

    Function: gen_d2z_instrument

    Description: output the opcode corresponding to instrument in the given file for
		 the given channel
    Parameters: FILE* f, the file to output the result into (already opened)
		int channel_number, the channel number (to seek the instrument)
    Return: nothing

*****************************************************************************/
void gen_d2z_instrument(FILE * f, int channel_number)
{
	unsigned char instrument = channel[channel_number].instrument;

	if (instrument > 63)
		instrument = 63;

	output(f, channel_number, 224 + instrument);
}

/*****************************************************************************

    Function: gen_d2z_period

    Description: output the opcode corresponding to period in the given file for
		 the given channel
    Parameters: FILE* f, the file to output the result into (already opened)
		int channel_number, the channel number
		int period, the period value
    Return: nothing

*****************************************************************************/
void gen_d2z_period(FILE * f, int channel_number, int period)
{
	char octave, alpha;
	int index;

	convert_period(period, &alpha, &octave);

	index = octave * NOTE_PER_OCTAVE + alpha;

	if (index > 91)
		index = 91;

	output(f, channel_number, index);
}

/*****************************************************************************

    Function: handle_note_d2z_mml

    Description: try to convert note in to our NEW David�Zeo ^^ pseudo mml language
    Parameters: ( int current_channel from global variables )
		( int current_row from global variables )

		int period, data about the "height" (not sure this term is
				     the right one in english) of the note
		int instrument
		int effect_id, cf table above
		int effect_data
    Return: nothing

*****************************************************************************/
void handle_note_d2z_mml(int period,
			 int instrument, int effect_id, int effect_data)
{
	FILE *out;

	strcpy(output_filename, input_filename);
	if (strrchr(output_filename, '.'))
		strrchr(output_filename, '.')[1] = '\0';

	sprintf(output_filename + strlen(output_filename),
		"%d", current_channel);

	out = fopen(output_filename, "at");

	if (out == NULL) {
		print_error("can't output to %s\n", output_filename);
	}

	switch (effect_id) {
	case FX_VOLUME:
		if ((effect_data >> 1 == channel[current_channel].volume) ||
		    ((effect_data >> 1 > 31)
		     && (channel[current_channel].volume == 31)))
			/* We're trying to set a volume already set for this channel */
			break;

		if (effect_data > 0x40) {
			channel[current_channel].volume = 31;
			nb_warning++;
			log_warning("Volume too high (0X%02X > 0X40)",
				    effect_data);
		} else {
			channel[current_channel].volume = effect_data >> 1;
			if (channel[current_channel].volume > 31)
				channel[current_channel].volume = 31;
		}

		gen_d2z_volume(out, current_channel);

		break;

	case FX_VOLUME_SLIDE:
		{
			effect_data =
			    ((effect_data >> 4) & 0xF) - (effect_data & 0xF);

			if (effect_data == 0)
				break;

			if (effect_data == 1)
				effect_data = 2;

			if (effect_data == -1)
				effect_data = -2;

			/* if the effect is not nul but the sliding of PCE volume (twice less
			   precise) would be nul, force the sliding such that we'll effectively
			   hear a change with the PCE replayer */

			effect_data /= 2;

			if ((channel[current_channel].volume + effect_data >=
			     31) && (channel[current_channel].volume == 31))
				break;
			/* the volume would be raised above the pce maximum while we're already
			   at the maximum */

			if (channel[current_channel].volume + effect_data >= 31) {
				channel[current_channel].volume = 31;
				gen_d2z_volume(out, current_channel);
				break;
			}

			if ((channel[current_channel].volume + effect_data <= 0)
			    && (channel[current_channel].volume == 0))
				break;
			/* the volume would be lowered under zero while already at zero */

			if (channel[current_channel].volume + effect_data <= 0) {
				channel[current_channel].volume = 0;
				gen_d2z_volume(out, current_channel);
				break;
			}

			channel[current_channel].volume += effect_data;
			gen_d2z_volume(out, current_channel);
			break;
		}

	case FX_PATTERN_BREAK:
	case FX_POSITION_JUMP:
	case FX_SPEED:
		/* These are global effects and shouldn't trigger the warning for
		 * unsupported effects
		 */
		break;

	case FX_ARPEGGIO:
		if (effect_data == 0)
			break;

	default:
		nb_warning++;
		log_warning("Unsupported effect 0X%X", effect_id);
	}

	if (instrument != 0) {
		if (instrument != channel[current_channel].instrument) {
			channel[current_channel].instrument = instrument;
			gen_d2z_instrument(out, current_channel);
		} else
			/* The instrument is already the good one, in order to
			 * fool the behaviour of volume setting when putting anew the same
			 * instrument number, I just set volume to max one (same effect
			 * but much faster when executed) and yet, only if max volume isn't set
			 */
		if (channel[current_channel].volume != 31) {
			channel[current_channel].volume = 31;
			gen_d2z_volume(out, current_channel);
		}
	}

	if (period != 0) {
		channel[current_channel].period = period;
		gen_d2z_period(out, current_channel, period);
	}

	fclose(out);

	channel[current_channel].last_note++;

}

/*****************************************************************************

    Function: finish_d2z

    Description: output a volume nul and the duration to conclude the parsing
    Parameters: none
    Return: nothing

*****************************************************************************/
void finish_d2z()
{

	FILE *out;

	strcpy(output_filename, input_filename);
	if (strrchr(output_filename, '.'))
		strrchr(output_filename, '.')[1] = '\0';

	sprintf(output_filename + strlen(output_filename),
		"%d", current_channel);

	out = fopen(output_filename, "at");

	if (out == NULL) {
		print_error("can't output to %s\n", output_filename);
	}

	if (channel[current_channel].last_note != 0) {
		int duration =
		    convert_row_to_duration(channel[current_channel].last_note);

		channel[current_channel].last_note = 0;

		while (duration >= 63) {
			channel[current_channel].col_written++;
			fprintf(out, "%d,", 128 + 63);
			duration -= 63;
		}

		if (duration != 0) {
			channel[current_channel].col_written++;
			fprintf(out, "%d,", 128 + duration);
		}

	}

	fprintf(out, "192");

}

/*****************************************************************************

    Function: read_byte

    Description: read a byte :)
    Parameters: FILE* in, the file to read in
		unsigned char* result, the place to store the result
    Return: nothing

*****************************************************************************/
void read_byte(FILE * in, unsigned char *result)
{
	fread(result, 1, 1, in);
}

/*****************************************************************************

    Function: read_byte_array

    Description: read an array of byte
    Parameters: FILE* in, the file to read in
		unsigned char* result, the place to store the result
		int length, the number of byte to read
    Return: nothing

*****************************************************************************/
void read_byte_array(FILE * in, char *result, int length)
{
	fread(result, length, 1, in);
}

/*****************************************************************************

    Function: read_word_motorola

    Description: read a word using the motorola order (hi byte first)
    Parameters: FILE* in, the file to read in
		unsigned int* result, the place to store the result
    Return: nothing

*****************************************************************************/
void read_word_motorola(FILE * in, unsigned int *result)
{
	unsigned char hi_byte, lo_byte;

	fread(&hi_byte, 1, 1, in);
	fread(&lo_byte, 1, 1, in);

	*result = lo_byte + (hi_byte << 8);
}

/*****************************************************************************

    Function: read_word_intel

    Description: read a word using the intel order (lo byte first)
    Parameters: FILE* in, the file to read in
		unsigned int* result, the place to store the result
    Return: nothing

*****************************************************************************/
void read_word_intel(FILE * in, unsigned int *result)
{
	unsigned char hi_byte, lo_byte;

	fread(&lo_byte, 1, 1, in);
	fread(&hi_byte, 1, 1, in);

	*result = lo_byte + (hi_byte << 8);
}

/*****************************************************************************

    Function: print_error

    Description: print error
    Parameters: like printf
    Return: nothing

*****************************************************************************/
void print_error(char *format, ...)
{
	char buf[256];

	va_list ap;
	va_start(ap, format);
	vsprintf(buf, format, ap);
	va_end(ap);

	fprintf(stderr, "\
--[ ERROR ]-------------------------------------------------------------------\n");

	fprintf(stderr, "%s", buf);

	return;
}

/*****************************************************************************

    Function: log_raw

    Description: log a raw string into a file
    Parameters: like printf
    Return: nothing

*****************************************************************************/
void log_raw(char *format, ...)
{
	char buf[256];
	FILE *out;

	va_list ap;
	va_start(ap, format);
	vsprintf(buf, format, ap);
	va_end(ap);

	out = fopen(LOG_FILENAME, "at");

	if (out == NULL) {
		print_error("can't output warning to %s", LOG_FILENAME);
		return;
	}

	fprintf(out, buf);

	fclose(out);

	return;
}

/*****************************************************************************

    Function: log_warning

    Description: log a string into a file preceded by the current module name
		 row and channel
    Parameters: like printf
    Return: nothing

*****************************************************************************/
void log_warning(char *format, ...)
{
	char buf[256];

	va_list ap;
	va_start(ap, format);
	vsprintf(buf, format, ap);
	va_end(ap);

	log_raw
	    ("Warning (%s, song position %d, row 0X%02X, channel %d) :\n   %s\n",
	     input_filename, current_song_position, current_row,
	     current_channel, buf);

	return;
}

/*****************************************************************************

    Function: print_usage

    Description: print usage :)
    Parameters: char* argv[] to display the exe name
    Return: nothing

*****************************************************************************/
void print_usage(char *argv[])
{
	fprintf(stderr, "\
--[ USAGE ]-------------------------------------------------------------------\n");
	fprintf(stderr, "%s <mod_filename>\n", argv[0]);
}

/*****************************************************************************

    Function: position_in_file

    Description: compute the position of the given pattern in the file
    Parameters: int pattern_number, number of the pattern in the file
    Return: absolute position of pattern data in the file

*****************************************************************************/
unsigned long position_in_file(int pattern_number)
{
	return PATTERN_DATA_OFFSET + pattern_number *
	    (ROW_NUMBER_PER_PATTERN * ROW_CHANNEL_DATA_LENGTH * nb_channel);
}

/*****************************************************************************

    Function: handle_pattern

    Description: parse the given pattern in the given file
    Parameters: FILE* in, the file in which we'll work
		int pattern_number, number of the pattern in the file
    Return: nothing

*****************************************************************************/
void handle_pattern(FILE * in, int pattern_number)
{

#if DEBUG > 1
	printf("Handling pattern %d\n at position %ld\n\n",
	       pattern_number, position_in_file(pattern_number));
#endif

	fseek(in, position_in_file(pattern_number), SEEK_SET);

	for (current_row = 0; current_row < ROW_NUMBER_PER_PATTERN;
	     current_row++) {
#if DEBUG > 1
		printf("row %02X\n", current_row);
#endif
		for (current_channel = 0; current_channel < nb_channel;
		     current_channel++) {
			unsigned int note_instrument;	/* record containing the current
							 * played note and the instrument
							 */
			unsigned char instrument_effect;	/* record containing the effect and
								 * a part of the instrument number
								 */
			unsigned char effect_data;	/* data for the current effect */

			read_word_motorola(in, &note_instrument);
			read_byte(in, &instrument_effect);
			read_byte(in, &effect_data);

#if DEBUG > 1
			printf
			    ("channel %d : Note %4d inst %2X FX %1X  data %02X\n",
			     current_channel, note_instrument & 0xFFF,
			     ((note_instrument >> 8) & 0xF0) +
			     (instrument_effect >> 4), instrument_effect & 0xF,
			     effect_data);
#endif

			(*handle_note) (note_instrument & 0xFFF,
					((note_instrument >> 8) & 0xF0) +
					(instrument_effect >> 4),
					instrument_effect & 0xF, effect_data);

		}
	}
}

int main(int argc, char *argv[])
{
	FILE *input, *output;	/* File to parse */
	
	int i;
	for (i = 0; i < 32; i++) {
		instrument_map[i] = i;
		percussion_map[i] = 0;
	}

	int c;
	char *eq;
	for(;;) {
		static struct option long_options[] = {
			{"track-name", required_argument, 0, 't'},
			{"map-instrument", required_argument, 0, 'm'},
			{"map-percussion", required_argument, 0, 'n'},
			{"percussion", required_argument, 0, 'p'},
			{0, 0, 0, 0}
		};
		int option_index = 0;
		c = getopt_long(argc, argv, "o:p:t:m:n:", long_options, &option_index);
		if (c == -1)
			break;
		switch (c) {
			case 'm':
				eq = strchr(optarg, '=');
				if (!eq) {
					fprintf(stderr, "invalid instrument mapping %s\n", optarg);
					exit(1);
				}
				*eq = 0;
				instrument_map[atoi(optarg)] = atoi(eq+1);
				break;
			case 'n':
				eq = strchr(optarg, '=');
				if (!eq) {
					fprintf(stderr, "invalid percussion mapping %s\n", optarg);
					exit(1);
				}
				*eq = 0;
				percussion_map[atoi(optarg)] = atoi(eq+1);
				break;
			case 'o':
				strcpy(output_filename, optarg);
				break;
			case 'p':
				channel[atoi(optarg)].percussion = 1000;
				break;
			case 't':
				strcpy(track_name, optarg);
				break;
			default:
				abort();
		}
	}

	for (i = 0; i < MAX_CHANNEL; i++)
		out_ch_ptr[i] = out_ch[i];

// handle_note = handle_note_display; /* We'll display each note encoutered by default */
	handle_note = handle_note_mml;	/* We'll try to output mml stuff there :) */
// handle_note = handle_note_d2z_mml; /* We'll *try* to output our new sound system */

// finish_parsing = finish_d2z;

	nb_warning = 0;
	unlink(LOG_FILENAME);

	input_filename = argv[optind];

	while (input_filename) {	/* For each filename on command line ... */

		input = fopen(input_filename, "rb");
		if (input == NULL) {	/* file couldn't be opened */
			print_error("input file (%s) not found\n",
				    input_filename);
			return 1;
		}

		log_raw(" *** %s tackled ***\n", input_filename);

		/* first, let's determine file type */

		fseek(input, SIGNATURE_OFFSET, SEEK_SET);
		read_byte_array(input, signature, SIGNATURE_LENGTH);

		/* setting default values in case of no known signature found */
		nb_instrument = 15;
		nb_channel = 4;

		if (!strncmp(signature, "M.K.", SIGNATURE_LENGTH)) {	/* Signature is egal to M.K. */
			nb_instrument = 31;
		} else if (!strncmp(signature, "4CHN", SIGNATURE_LENGTH)) {
			nb_instrument = 31;
		} else if (!strncmp(signature, "8CHN", SIGNATURE_LENGTH)) {
			nb_instrument = 31;
			nb_channel = 8;
		} else if (!strncmp(signature, "6CHN", SIGNATURE_LENGTH)) {
			nb_instrument = 31;
			nb_channel = 6;
		} else if (!strncmp(signature, "FLT4", SIGNATURE_LENGTH)) {
			nb_instrument = 31;
		} else if (!strncmp(signature, "FLT8", SIGNATURE_LENGTH)) {
			nb_instrument = 31;
			nb_channel = 8;
		}
#if DEBUG > 0
		printf("Module %s has %d channels and %d instruments\n",
		       input_filename, nb_channel, nb_instrument);
#endif

		fseek(input, 20, SEEK_SET);
		for (i = 0; i < nb_instrument; i++) {
			read_byte_array(input, samples[i].name, 22);
			read_word_motorola(input, &samples[i].length);
			read_byte(input, (unsigned char *)&samples[i].tune);
			read_byte(input, &samples[i].volume);
			read_word_motorola(input, &samples[i].repeat_at);
			read_word_motorola(input, &samples[i].repeat_length);
			if (samples[i].length > 0)
                                printf("sample %d: %s len %d vol %d repeat at %d for %d\n",
                                        i, samples[i].name, samples[i].length,
                                        samples[i].volume, samples[i].repeat_at,
                                        samples[i].repeat_length);
		}

		/* Clear files where results will be dumped */

		if (output_filename[0] == 0) {
			strcpy(output_filename, input_filename);
			if (strrchr(output_filename, '.'))
				strrchr(output_filename, '.')[0] = '\0';
			if (!track_name[0])
				strcpy(track_name, output_filename);
			strcat(output_filename, ".mml");
			//sprintf(output_filename + strlen(output_filename),
			//  "%d", current_channel);
		}

		unlink(output_filename);

		for (current_channel = 0; current_channel < nb_channel;
		     current_channel++) {
			channel[current_channel].last_note = 0;
			channel[current_channel].period = -1;
			channel[current_channel].instrument = -1;
			channel[current_channel].volume = -1;
			channel[current_channel].panning = 0;
			channel[current_channel].col_written = 0;
		}

		/* Initial song values */

		speed = 6;
		bpm = 125;

		/* Then, let's grab the sequence of played patterns */

		fseek(input, PATTERN_ARRAY_OFFSET, SEEK_SET);
		read_byte_array(input, pattern_array, PATTERN_ARRAY_LENGTH);

		/* As well as the number of played patterns
		 * This is not the number of pattern we'll found on the file
		 * but rather the number of useful items in the sequence playing just above
		 */
		fseek(input, SONG_LENGTH_OFFSET, SEEK_SET);
		read_byte(input, &song_length);

#if DEBUG > 0
		{
			unsigned char dummy;
			printf("Raw patterns in module :\n");

			for (dummy = 0; dummy < PATTERN_ARRAY_LENGTH; dummy++)
				printf("%4d", pattern_array[dummy]);

			printf("\nUseful patterns (%d) :\n", song_length);

			for (dummy = 0; dummy < song_length; dummy++)
				printf("%4d", pattern_array[dummy]);
			puts("");

		}
#endif

		for (current_instrument = 0;
		     current_instrument < nb_instrument; current_instrument++) {
			printf("%d/%d\r", current_instrument + 1,
			       nb_instrument);
#if 0
			convertion_inst[current_instrument] =
			    nearest_pce_instrument(current_instrument);
#endif
		}

		for (current_song_position = 0;
		     current_song_position < song_length;
		     current_song_position++) {
			printf("%d/%d\r", current_song_position + 1,
			       song_length);
			handle_pattern(input,
				       pattern_array[current_song_position]);
		}

		fclose(input);

		for (current_channel = 0;
		     current_channel < nb_channel; current_channel++)
			//(*finish_parsing)();

			log_raw(" *** %s processed ***\n", input_filename);

		optind++;
		input_filename = argv[optind];

	}

	output = fopen(output_filename, "w");
	fprintf(output, ".TRACK %s\n", track_name);
	fprintf(output, ".CHANNEL 0 Setup\n");
	fprintf(output, "T60 V31 L16 ^D0\n\n");

	/* There are only two noise-capable channels (5 and 6), so we
	   allocate them to the two MOD channels with the most noise. */
	int max = -1;
	int max2 = -1;
	for (i = 0; i < nb_channel; i++) {
		if (channel[i].percussion > max) {
			max2 = max;
			max = channel[i].percussion;
		}
		else if (channel[i].percussion > max2)
			max2 = channel[i].percussion;
	}

	for (current_channel = 0; current_channel < nb_channel;
	     current_channel++) {
		int outchan = current_channel + 1;
		if (channel[current_channel].percussion == max)
			outchan = 5;
		else if (channel[current_channel].percussion == max2)
			outchan = 6;
		else if (channel[current_channel].percussion) {
			log_warning("Cannot allocate channel %d to noise channel.", current_channel);
			/* Turn all @M1 to @M0. */
			char *c;
			for (c = out_ch[current_channel]; *c; c++) {
				if (c[0] == '@' && c[1] == 'M' && c[2] == '1')
					c[2] = '0';
			}
		}
		fprintf(output, ".CHANNEL %d\tch_%d\n", outchan,
			current_channel);
		fprintf(output, "P15,15");
		fputc('\n', output);
		fputs(out_ch[current_channel], output);
		fputc('\n', output);
	}
	fclose(output);

	log_raw("\
--[ END ]---------------------------------------------------------------------\n\
  Total number of warning%s : %d\n", nb_warning ? "s" : "", nb_warning);

	fprintf(stderr, "warning%s : %d\n%s\n",
		nb_warning ? "s" : "",
		nb_warning, nb_warning ? "check the log file" : "");

	return 0;
}