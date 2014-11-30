/*
** nw_pulsesamp~.c
** � 2002-2007, lowkey digital studio
**
** MSP object
** play sample through when pulse is received 
** 
** 2002/2/24 started by Nathan Wolek, based on bangsamp~ & grain.pulse~
**  >>> missing time ???
** 2004/2/24 restored messages to distribute to some people
** 2004/3/10 added "bang on init" outlet
** 2005/10/10 added start and end points; 
** 2006/11/18 moved to Xcode; compiled for UB
** 2007/04/10 added sample count outlet
**
** 
*/

#include "ext.h"		// required for all MAX external objects
#include "z_dsp.h"		// required for all MSP external objects
#include "buffer.h"		// required to deal with buffer object
#include <string.h>

//#define DEBUG			//enable debugging messages

#define OBJECT_NAME		"nw_pulsesamp~"		// name of the object

/* for the assist method */
#define ASSIST_INLET	1
#define ASSIST_OUTLET	2

/* for the grain stage */
#define NEW_GRAIN		2
#define FINISH_GRAIN	1
#define	NO_GRAIN		0

/* for direction flag */
#define FORWARD_GRAINS		0
#define REVERSE_GRAINS		1

/* for interpolation flag */
#define INTERP_OFF			0
#define INTERP_ON			1

/* for overflow flag, added 2002.10.28 */
#define OVERFLOW_OFF		0
#define OVERFLOW_ON			1

void *this_class;		// required global pointing to this class

typedef struct _nw_pulsesamp
{
	t_pxobject x_obj;
	// sound buffer info
	t_symbol *snd_sym;
	t_buffer *snd_buf_ptr;
	t_buffer *next_snd_buf_ptr;		//added 2002.07.24
	double snd_last_out;
	//long snd_buf_length;	//removed 2002.07.11
	short snd_interp;
	// current grain info
	double grain_samp_inc;		// in buffer_samples/playback_sample
	double grain_gain;	// as coef
	double grain_start;	// in samples; add 2005.10.10
	double grain_end;	// in samples; add 2005.10.10
	short grain_direction;	// forward or reverse
	double snd_step_size;	// in samples
	double curr_snd_pos;	// in samples
	short overflow_status;	//only used while grain is sounding
			//will produce false positives otherwise
	// defered grain info at control rate
	double next_grain_samp_inc;	// in samples/playback_sample
	double next_grain_gain;		// in milliseconds
	double next_grain_start;		// in milliseconds; add 2005.10.10
	double next_grain_end;		// in milliseconds; add 2005.10.10
	short next_grain_direction;	// forward or reverse
	// signal or control grain info
	short grain_samp_inc_connected;
	short grain_gain_connected;
	short grain_start_connected;	// add 2005.10.10
	short grain_end_connected;		// add 2005.10.10
	// grain tracking info
	long curr_count_samp;			// add 2007.04.10
	float last_pulse_in;
	double output_sr;
	double output_1oversr;
	//bang on init outlet, added 2004.03.10
	void *out_bangoninit;
} t_nw_pulsesamp;

void *nw_pulsesamp_new(t_symbol *snd);
t_int *nw_pulsesamp_perform(t_int *w);
t_int *nw_pulsesamp_perform0(t_int *w);
void nw_pulsesamp_dsp(t_nw_pulsesamp *x, t_signal **sp, short *count);
void nw_pulsesamp_setsnd(t_nw_pulsesamp *x, t_symbol *s);
void nw_pulsesamp_float(t_nw_pulsesamp *x, double f);
void nw_pulsesamp_int(t_nw_pulsesamp *x, long l);
void nw_pulsesamp_bangoninit(t_nw_pulsesamp *x, t_symbol *s, short argc, t_atom argv);
void nw_pulsesamp_initGrain(t_nw_pulsesamp *x, float in_samp_inc, float in_gain, float in_start, float in_end);
void nw_pulsesamp_sndInterp(t_nw_pulsesamp *x, long l);
void nw_pulsesamp_reverse(t_nw_pulsesamp *x, long l);
void nw_pulsesamp_assist(t_nw_pulsesamp *x, t_object *b, long msg, long arg, char *s);
void nw_pulsesamp_getinfo(t_nw_pulsesamp *x);
float allpassInterp(float *in_array, float index, float last_out, long buf_length);
double mcLinearInterp(float *in_array, long index_i, double index_frac, long in_size, short in_chans);


t_symbol *ps_buffer;

/********************************************************************************
void main(void)

inputs:			nothing
description:	called the first time the object is used in MAX environment; 
		defines inlets, outlets and accepted messages
returns:		nothing
********************************************************************************/
void main(void)
{
	setup((t_messlist **)&this_class, (method)nw_pulsesamp_new, (method)dsp_free, 
			(short)sizeof(t_nw_pulsesamp), 0L, A_SYM, 0);
	addmess((method)nw_pulsesamp_dsp, "dsp", A_CANT, 0);
	
	/* bind method "nw_pulsesamp_setsnd" to the 'setSound' message */
	addmess((method)nw_pulsesamp_setsnd, "setSound", A_SYM, 0);
	
	/* bind method "nw_pulsesamp_float" to incoming floats */
	addfloat((method)nw_pulsesamp_float);
	
	/* bind method "nw_pulsesamp_int" to incoming ints */
	addint((method)nw_pulsesamp_int);
	
	/* bind method "nw_pulsesamp_reverse" to the direction message */
	addmess((method)nw_pulsesamp_reverse, "reverse", A_LONG, 0);
	
	/* bind method "nw_pulsesamp_sndInterp" to the sndInterp message */
	addmess((method)nw_pulsesamp_sndInterp, "sndInterp", A_LONG, 0);
	
	/* bind method "nw_pulsesamp_assist" to the assistance message */
	addmess((method)nw_pulsesamp_assist, "assist", A_CANT, 0);
	
	/* bind method "nw_pulsesamp_getinfo" to the getinfo message */
	addmess((method)nw_pulsesamp_getinfo, "getinfo", A_NOTHING, 0);
	
	dsp_initclass();
	
	/* needed for 'buffer~' work, checks for validity of buffer specified */
	ps_buffer = gensym("buffer~");
	
	#ifndef DEBUG
		
	#endif /* DEBUG */
}

/********************************************************************************
void *nw_pulsesamp_new(double initial_pos)

inputs:			*snd		-- name of buffer holding sound
description:	called for each new instance of object in the MAX environment;
		defines inlets and outlets; sets variables and buffers
returns:		nothing
********************************************************************************/
void *nw_pulsesamp_new(t_symbol *snd)
{
	t_nw_pulsesamp *x = (t_nw_pulsesamp *)newobject(this_class);
	dsp_setup((t_pxobject *)x, 5);					// five inlets
	outlet_new((t_pxobject *)x, "signal");			// sample count outlet; add 2007.04.10
	x->out_bangoninit = bangout((t_pxobject *)x);	// "bang when samp begins" outlet
	outlet_new((t_pxobject *)x, "signal");			// overflow outlet
	outlet_new((t_pxobject *)x, "signal");			// signal outlet
	
	/* set buffer names */
	x->snd_sym = snd;
	
	/* zero pointers */
	x->snd_buf_ptr = x->next_snd_buf_ptr = NULL;
	
	/* setup variables */
	x->grain_samp_inc = x->next_grain_samp_inc = 1.0;
	x->grain_gain = x->next_grain_gain = 1.0;
	x->grain_start = x->next_grain_start = 0.0;  // add 2005.10.10
	x->grain_end = x->next_grain_end = -1.0;	//add 2005.10.10
	x->snd_step_size = 1.0;
	x->curr_snd_pos = 0.0;
	x->last_pulse_in = 0.0;
	x->curr_count_samp = 0;						// add 2007.04.10
	
	/* set flags to defaults */
	x->snd_interp = INTERP_ON;
	x->grain_direction = x->next_grain_direction = FORWARD_GRAINS;
	
	x->x_obj.z_misc = Z_NO_INPLACE;
	
	/* return a pointer to the new object */
	return (x);
}

/********************************************************************************
void nw_pulsesamp_dsp(t_cpPan *x, t_signal **sp, short *count)

inputs:			x		-- pointer to this object
				sp		-- array of pointers to input & output signals
				count	-- array of shorts detailing number of signals attached
					to each inlet
description:	called when DSP call chain is built; adds object to signal flow
returns:		nothing
********************************************************************************/
void nw_pulsesamp_dsp(t_nw_pulsesamp *x, t_signal **sp, short *count)
{
	/* set buffers */
	nw_pulsesamp_setsnd(x, x->snd_sym);
	
	/* set current snd position to 1 more than length */
	x->curr_snd_pos = (float)((x->snd_buf_ptr)->b_frames) + 1.0;
	
	/* test inlet 2 and 3 for signal data */
	x->grain_samp_inc_connected = count[1];
	x->grain_gain_connected = count[2];
	x->grain_start_connected = count[3];  // add 2005.10.10
	x->grain_end_connected = count[4];    // add 2005.10.10
	
	x->output_sr = sp[5]->s_sr;				// change 2005.10.10
	x->output_1oversr = 1.0 / x->output_sr;
	
	//set overflow status, added 2002.10.28
	x->overflow_status = OVERFLOW_OFF;
	
	if (count[5] && count[0]) {	// if input and output connected..
		// output is computed
		dsp_add(nw_pulsesamp_perform, 8, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, 
			sp[5]->s_vec, sp[6]->s_vec, sp[7]->s_vec, sp[5]->s_n);
		#ifdef DEBUG
			post("%s: output is being computed", OBJECT_NAME);
		#endif /* DEBUG */
	} else {					// if not...
		// no output computed
		#ifdef DEBUG
			post("%s: no output computed", OBJECT_NAME);
		#endif /* DEBUG */
	}
	
}

/********************************************************************************
t_int *nw_pulsesamp_perform(t_int *w)

inputs:			w		-- array of signal vectors specified in "nw_pulsesamp_dsp"
description:	called at interrupt level to compute object's output; used when
		outlets are connected; tests inlet 2 3 & 4 to use either control or audio
		rate data
returns:		pointer to the next 
********************************************************************************/
t_int *nw_pulsesamp_perform(t_int *w)
{
	t_nw_pulsesamp *x = (t_nw_pulsesamp *)(w[1]);		//pointer to class
	float *in_pulse = (float *)(w[2]);			//pulse control
	float *in_samp_inc = (float *)(w[3]);		//sample increment
	float *in_gain = (float *)(w[4]);			//gain
	t_float *out = (t_float *)(w[5]);			//signal output
	t_float *out2 = (t_float *)(w[6]); 			//overflow
	t_float *out3 = (t_float *)(w[7]); 			//sample count output; add 2007.04.10
	//float *in_start = (float *)(w[7]);			// where to start reading buffer
	//float *in_end = (float *)(w[8]);			// where to stop reading buffer
	int vec_size = (int)(w[8]);					//vector size
	t_buffer *s_ptr = x->snd_buf_ptr;
	float *tab_s;
	double s_step_size, g_gain;
	float  snd_out, last_s;
	double index_s, index_s_start, index_s_end;  //change 2005.10.10
	long size_s, count_samp;	// change 2007.04.10
	short interp_s, g_direction, of_status;
	float last_pulse;
	
	vec_size += 1;		//increase by one for pre-decrement
	--out;				//decrease by one for pre-increment
	--out2;
	--out3;		//add 2007.04.10
	
	/* check to make sure buffers are loaded with proper file types*/
	if (x->x_obj.z_disabled)		// object is enabled
		goto out;
	if (s_ptr == NULL)				// buffer pointers are defined
		goto zero;
	if (!s_ptr->b_valid)			// files are loaded
		goto zero;
	
	// get interpolation options
	interp_s = x->snd_interp;
	
	// get grain options
	g_gain = x->grain_gain;
	g_direction = x->grain_direction;
	of_status = x->overflow_status;
	// get pointer info
	s_step_size = x->snd_step_size;
	index_s = x->curr_snd_pos;
	index_s_start = x->grain_start; //add 2005.10.10
	index_s_end = x->grain_end; //add 2005.10.10
	// get buffer info
	tab_s = s_ptr->b_samples;
	size_s = s_ptr->b_frames;
	last_s = x->snd_last_out;
	last_pulse = x->last_pulse_in;		//added 2004.03.15
	count_samp = x->curr_count_samp;	//added 2007.04.10
	
	
	while (--vec_size) {
	
		/* check bounds of window index */
		if (index_s > size_s) {
			if (last_pulse == 0.0 && *in_pulse == 1.0) { // if pulse begins...
				nw_pulsesamp_initGrain(x, *in_samp_inc, *in_gain, 0., 0.);
				
				/* set local vars */
				// get grain options
				g_gain = x->grain_gain;
				g_direction = x->grain_direction;
				// get pointer info
				s_step_size = x->snd_step_size;
				index_s = x->curr_snd_pos;
				index_s_start = x->grain_start; //add 2005.10.10
				index_s_end = x->grain_end;		// add 2005.10.10
				// get buffer info
				s_ptr = x->snd_buf_ptr;
				tab_s = s_ptr->b_samples;
				size_s = s_ptr->b_frames;
				last_s = x->snd_last_out;
				count_samp = x->curr_count_samp;	//add 2007.04.10
				
				//pulse tracking for overflow
				of_status = OVERFLOW_OFF;
			} else { // if not...
				*++out = 0.0;
				*++out2 = 0.0;
				*++out3 = 0.0;	//add 2007.04.10
				last_pulse = *in_pulse;
				++in_pulse, ++in_samp_inc, ++in_gain;
				continue;
			}
		}
		
		//pulse tracking for overflow, added 2002.10.29
		if (!of_status) {
			if (last_pulse == 1.0 && *in_pulse == 0.0) { // if grain on & pulse ends...
				of_status = OVERFLOW_ON;	//start overflowing
			}
		}
		
		/* advance index of buffers */
		if (g_direction == FORWARD_GRAINS) {	// if forward...
			index_s += s_step_size;		// add to sound index
			
			/* check bounds of buffer index */
			if (index_s > index_s_end) {			// change 2005.10.10
				index_s = size_s + s_step_size;
				*++out = 0.0;
				*++out2 = 0.0;
				*++out3 = 0.0;	//add 2007.04.10
				last_pulse = *in_pulse;
				++in_pulse, ++in_samp_inc, ++in_gain;
				#ifdef DEBUG
					post("%s: end of grain", OBJECT_NAME);
				#endif /* DEBUG */
				continue;
			}
			
		} else {	// if reverse...
			index_s -= s_step_size;		// subtract from sound index
			
			/* check bounds of buffer index */
			if (index_s < index_s_start) {			// change 2005.10.10
				index_s = size_s + s_step_size;
				*++out = 0.0;
				*++out2 = 0.0;
				*++out3 = 0.0;	//add 2007.04.10
				last_pulse = *in_pulse;
				++in_pulse, ++in_samp_inc, ++in_gain;
				#ifdef DEBUG
					post("%s: end of grain", OBJECT_NAME);
				#endif /* DEBUG */
				continue;
			}
			
		}
		
		if (interp_s == INTERP_OFF) {
			/* interpolation sounds better than following, but uses more CPU */
			snd_out = tab_s[(long)index_s];
		} else { // if INTERP_ON //
			/* perform allpass interpolation on sound buffer output */
			snd_out = allpassInterp(tab_s, (float)index_s, last_s, size_s);
		}
		
		/* multiply snd_out by win_value */
		*++out = snd_out * g_gain;
		
		if (of_status) {
			*++out2 = *in_pulse;
		} else {
			*++out2 = 0.0;
		}
		
		*++out3 = (double)count_samp++;	//add 2007.04.10
		
		/* update last output variables */
		last_pulse = *in_pulse;
		last_s = snd_out;
		
		//advance input pointers
		++in_pulse, ++in_samp_inc, ++in_gain;
	}	
	
	/* update last output variables */
	x->snd_last_out = last_s;
	x->curr_snd_pos = index_s;
	x->last_pulse_in = last_pulse;
	x->overflow_status = of_status;
	x->curr_count_samp = count_samp;	//added 2007.04.10
		
	return (w + 9);

zero:
		while (--vec_size) {
			*++out = 0.0;
			*++out2 = -1.0;
			*++out3 = 0.0;	//add 2007.04.10
		}
out:
		return (w + 9);
}	

/********************************************************************************
t_int *nw_pulsesamp_perform0(t_int *w)

inputs:			w		-- array of signal vectors specified in "nw_pulsesamp_dsp"
description:	called at interrupt level to compute object's output; used when
		nothing is connected to output; saves CPU cycles
returns:		pointer to the next 
********************************************************************************/
t_int *nw_pulsesamp_perform0(t_int *w)
{
	t_float *out = (t_float *)(w[1]);
	int vec_size = (int)(w[2]);

	vec_size += 1;		//increase by one for pre-decrement
	--out;				//decrease by one for pre-increment

	while (--vec_size >= 0) {
		*++out = 0.;
	}

	return (w + 3);
}

/********************************************************************************
void nw_pulsesamp_initGrain(t_nw_pulsesamp *x, float in_samp_inc, float in_gain)

inputs:			x					-- pointer to this object
				in_samp_inc			-- playback sample increment, corrected for sr
				in_gain				-- gain multiplier for sample
				in_start			-- where to start reading buffer, in ms
				in_end				-- where to stop reading buffer, in ms
description:	initializes grain vars; called from perform method when bang is 
		received
returns:		nothing 
********************************************************************************/
void nw_pulsesamp_initGrain(t_nw_pulsesamp *x, float in_samp_inc, float in_gain, 
	float in_start, float in_end)
{
	t_buffer *s_ptr;
	//long temp, pretemp;
	
	#ifdef DEBUG
		post("%s: initializing grain", OBJECT_NAME);
	#endif /* DEBUG */
	
	if (x->next_snd_buf_ptr != NULL) {	//added 2002.07.24
		x->snd_buf_ptr = x->next_snd_buf_ptr;
		x->next_snd_buf_ptr = NULL;
		//x->snd_last_out = 0.0; // removed 2002.12.09, duplicated below
		
		#ifdef DEBUG
			post("%s: buffer pointer updated", OBJECT_NAME);
		#endif /* DEBUG */
	}
	
	s_ptr = x->snd_buf_ptr;
	
	x->grain_direction = x->next_grain_direction;
	
	/* test if variables should be at audio or control rate */
	if (x->grain_samp_inc_connected) { // if samp_inc is at audio rate
		x->grain_samp_inc = in_samp_inc;
	} else { // if samp_inc is at control rate
		x->grain_samp_inc = x->next_grain_samp_inc;
	}
	
	// compute sound buffer step size per vector sample
	x->snd_step_size = x->grain_samp_inc * s_ptr->b_sr * x->output_1oversr;
	
	if (x->grain_gain_connected) { // if gain multiplier is at audio rate
		x->grain_gain = in_gain;
	} else { // if pitch multiplier at control rate
		x->grain_gain = x->next_grain_gain;
	}
	
	/* start add 2005.10.10 */
	
	if (x->grain_start_connected) { // if start is at audio rate
		x->grain_start = in_start;
	} else { // if start at control rate
		x->grain_start = x->next_grain_start;
	}
	
	// convert start to samples
	x->grain_start = (long)((x->grain_start * s_ptr->b_msr) + 0.5);
	
	if (x->grain_end_connected) { // if end is at audio rate
		x->grain_end = in_end;
	} else { // if end at control rate
		x->grain_end = x->next_grain_end;
	}
	
	// convert end to samples
	x->grain_end = (long)((x->grain_end * s_ptr->b_msr) + 0.5);
	
	// test if end within bounds
	if (x->grain_end < 0. || x->grain_end > (double)(s_ptr->b_frames)) x->grain_end = (double)(s_ptr->b_frames);
	// test if start within bounds
	if (x->grain_start < 0. || x->grain_start > x->grain_end) x->grain_start = 0.;
	
	/* zx not working, try again later
	// search for start zx
	temp = (long)(x->grain_start);
	pretemp = --temp;
	while (true)
	{
		if (s_ptr->b_samples[pretemp] < 0. && s_ptr->b_samples[temp] >= 0.) {
			break;
		}
		--temp, --pretemp;
		if (pretemp < 0) 
		{
			temp = 0;
			break;
		}
	}
	x->grain_start = (double)temp;
	
	// search for end zx
	temp = (long)(x->grain_end);
	pretemp = --temp;
	while (true)
	{
		if (s_ptr->b_samples[pretemp] < 0. && s_ptr->b_samples[temp] >= 0.) {
			break;
		}
		++temp, ++pretemp;
		if (pretemp >= s_ptr->b_frames) 
		{
			temp = s_ptr->b_frames;
			break;
		}
	}
	x->grain_end = (double)temp;
	*/
	
	/* end add 2005.10.10 */
	
	// set initial sound position based on direction
	if (x->grain_direction == FORWARD_GRAINS) {	// if forward...
		x->curr_snd_pos = x->grain_start - x->snd_step_size;
	} else {	// if reverse...
		x->curr_snd_pos = x->grain_end + x->snd_step_size;
	}
	
	// reset history
	x->snd_last_out = 0.0;
	x->curr_count_samp = 0;		// add 2007.04.10
	
	// send bang out to notify of beginning samp
	defer(x, (void *)nw_pulsesamp_bangoninit,0L,0,0L); //added 2004.03.10
	
	#ifdef DEBUG
		post("%s: beginning of grain", OBJECT_NAME);
		post("%s: samp_inc = %f samps", OBJECT_NAME, x->snd_step_size);
	#endif /* DEBUG */
}

/********************************************************************************
void nw_pulsesamp_setsnd(t_index *x, t_symbol *s)

inputs:			x		-- pointer to this object
				s		-- name of buffer to link
description:	links buffer holding the grain sound source 
returns:		nothing
********************************************************************************/
void nw_pulsesamp_setsnd(t_nw_pulsesamp *x, t_symbol *s)
{
	t_buffer *b;
	
	if ((b = (t_buffer *)(s->s_thing)) && ob_sym(b) == ps_buffer) {
		if (b->b_nchans != 1) {
			error("%s: buffer~ > %s < must be mono", OBJECT_NAME, s->s_name);
			x->next_snd_buf_ptr = NULL;		//added 2002.07.15
		} else {
			if (x->snd_buf_ptr == NULL) { // make current buffer
				x->snd_sym = s;
				x->snd_buf_ptr = b;
				x->snd_last_out = 0.0;
				
				#ifdef DEBUG
					post("%s: current sound set to buffer~ > %s <", OBJECT_NAME, s->s_name);
				#endif /* DEBUG */
			} else { // defer to next buffer
				x->snd_sym = s;
				x->next_snd_buf_ptr = b;
				//x->snd_buf_length = b->b_frames;	//removed 2002.07.11
				//x->snd_last_out = 0.0;		//removed 2002.07.24
				
				#ifdef DEBUG
					post("%s: next sound set to buffer~ > %s <", OBJECT_NAME, s->s_name);
				#endif /* DEBUG */
			}
		}
	} else {
		error("%s: no buffer~ * %s * found", OBJECT_NAME, s->s_name);
		x->next_snd_buf_ptr = NULL;
	}
}

/********************************************************************************
void nw_pulsesamp_float(t_nw_pulsesamp *x, double f)

inputs:			x		-- pointer to our object
				f		-- value of float input
description:	handles floats sent to inlets; inlet 2 sets "next_grain_pos_start" 
		variable; inlet 3 sets "next_grain_length" variable; inlet 4 sets 
		"next_grain_pitch" variable; left inlet generates error message in max 
		window
returns:		nothing
********************************************************************************/
void nw_pulsesamp_float(t_nw_pulsesamp *x, double f)
{
	if (x->x_obj.z_in == 1) // if inlet 2
	{
		x->next_grain_samp_inc = f;
	}
	else if (x->x_obj.z_in == 2) // if inlet 3
	{
		x->next_grain_gain = f;
		
	}
	else if (x->x_obj.z_in == 3) // if inlet 4
	{
		x->next_grain_start = f;  // add 2005.10.10
	}
	else if (x->x_obj.z_in == 4) // if inlet 5
	{
		x->next_grain_end = f;    // add 2005.10.10
	}
	else
	{
		post("%s: this inlet does not accept floats", OBJECT_NAME);
	}
}

/********************************************************************************
void nw_pulsesamp_int(t_nw_pulsesamp *x, long l)

inputs:			x		-- pointer to our object
				l		-- value of int input
description:	handles ints sent to inlets; inlet 2 sets "next_grain_pos_start" 
		variable; inlet 3 sets "next_grain_length" variable; inlet 4 sets 
		"next_grain_pitch" variable; left inlet generates error message in max 
		window
returns:		nothing
********************************************************************************/
void nw_pulsesamp_int(t_nw_pulsesamp *x, long l)
{
	if (x->x_obj.z_in == 1) // if inlet 2
	{
		x->next_grain_samp_inc = (double) l;
	}
	else if (x->x_obj.z_in == 2) // if inlet 3
	{
		x->next_grain_gain = (double) l;
	}
	else if (x->x_obj.z_in == 3) // if inlet 4
	{
		x->next_grain_start = (double) l;  // add 2005.10.10
	}
	else if (x->x_obj.z_in == 4) // if inlet 5
	{
		x->next_grain_end = (double) l;    // add 2005.10.10
	}
	else
	{
		post("%s: this inlet does not accept ints", OBJECT_NAME);
	}
}

/********************************************************************************
void nw_pulsesamp_bangoninit(t_nw_pulsesamp *x, t_symbol *s, short argc, t_atom argv)

inputs:			x		-- pointer to our object
description:	sends bangs when samp is initialized; allows external settings
	to advance; extra arguments allow for use of defer method
returns:		nothing
********************************************************************************/
void nw_pulsesamp_bangoninit(t_nw_pulsesamp *x, t_symbol *s, short argc, t_atom argv)
{
	if (sys_getdspstate()) {
		outlet_bang(x->out_bangoninit);
	}
}

/********************************************************************************
void nw_pulsesamp_sndInterp(t_nw_pulsesamp *x, long l)

inputs:			x		-- pointer to our object
				l		-- flag value
description:	method called when "sndInterp" message is received; allows user 
		to define whether interpolation is used in pulling values from the sound
		buffer; default is on
returns:		nothing
********************************************************************************/
void nw_pulsesamp_sndInterp(t_nw_pulsesamp *x, long l)
{
	if (l == INTERP_OFF) {
		x->snd_interp = INTERP_OFF;
		#ifdef DEBUG
			post("%s: sndInterp is set to off", OBJECT_NAME);
		#endif // DEBUG //
	} else if (l == INTERP_ON) {
		x->snd_interp = INTERP_ON;
		#ifdef DEBUG
			post("%s: sndInterp is set to on", OBJECT_NAME);
		#endif // DEBUG //
	} else {
		error("%s: sndInterp message was not understood", OBJECT_NAME);
	}
}

/********************************************************************************
void nw_pulsesamp_reverse(t_nw_pulsesamp *x, long l)

inputs:			x		-- pointer to our object
				l		-- flag value
description:	method called when "reverse" message is received; allows user 
		to define whether sound is played forward or reverse; default is forward
returns:		nothing
********************************************************************************/
void nw_pulsesamp_reverse(t_nw_pulsesamp *x, long l)
{
	if (l == REVERSE_GRAINS) {
		x->next_grain_direction = REVERSE_GRAINS;
		#ifdef DEBUG
			post("%s: reverse is set to on", OBJECT_NAME);
		#endif // DEBUG //
	} else if (l == FORWARD_GRAINS) {
		x->next_grain_direction = FORWARD_GRAINS;
		#ifdef DEBUG
			post("%s: reverse is set to off", OBJECT_NAME);
		#endif // DEBUG //
	} else {
		error("%s: reverse was not understood", OBJECT_NAME);
	}
	
}

/********************************************************************************
void nw_pulsesamp_assist(t_nw_pulsesamp *x, t_object *b, long msg, long arg, char *s)

inputs:			x		-- pointer to our object
				b		--
				msg		--
				arg		--
				s		--
description:	method called when "assist" message is received; allows inlets 
		and outlets to display assist messages as the mouse passes over them
returns:		nothing
********************************************************************************/
void nw_pulsesamp_assist(t_nw_pulsesamp *x, t_object *b, long msg, long arg, char *s)
{
	if (msg==ASSIST_INLET) {
		switch (arg) {
			case 0:
				strcpy(s, "(signal) pulse to output a grain");
				break;
			case 1:
				strcpy(s, "(signal/float) sample increment, 1.0 = normal");
				break;
			case 2:
				strcpy(s, "(signal/float) gain, 1.0 = normal");
				break;
			case 3:
				strcpy(s, "(signal/float) start in ms");
				break;
			case 4:
				strcpy(s, "(signal/float) end in ms");
				break;
		}
	} else if (msg==ASSIST_OUTLET) {
		switch (arg) {
			case 0:
				strcpy(s, "(signal) sample output");
				break;
			case 1:
				strcpy(s, "(signal) pulse overflow");
				break;
			case 2:
				strcpy(s, "(bang) when sample starts");
				break;
			case 3:
				strcpy(s, "(signal) sample count");
				break;
		}
	}
	
	#ifdef DEBUG
		post("%s: assist message displayed", OBJECT_NAME);
	#endif /* DEBUG */
}

/********************************************************************************
void nw_pulsesamp_getinfo(t_nw_pulsesamp *x)

inputs:			x		-- pointer to our object
				
description:	method called when "getinfo" message is received; displays info
		about object and lst update
returns:		nothing
********************************************************************************/
void nw_pulsesamp_getinfo(t_nw_pulsesamp *x)
{
	post("%s object by Nathan Wolek", OBJECT_NAME);
	post("Last updated on %s - www.nathanwolek.com", __DATE__);
}

/********************************************************************************
float allpassInterp(float *in_array, float index, float last_out, long buf_length)

inputs:			*in_array -- name of array of input values
				index -- floating point index value to interpolate
				last_out -- value of last interpolated output
description:	performs allpass interpolation on an input array and returns the
	results to a location specified by a pointer; implements filter as specified
	in Dattorro 2: J. Audio Eng. Soc., Vol 45, No 10, 1997 October
returns:		interpolated output
********************************************************************************/
float allpassInterp(float *in_array, float index, float last_out, long buf_length)
{
	// index = i.frac
	long index_i = (long)index;					// i
	long index_iP1 = index_i + 1;				// i + 1
	float index_frac = index - (float)index_i;	// frac
	float out;
	
	// make sure that index_iP1 is not out of range
	while (index_iP1 >= buf_length) index_iP1 -= buf_length;
	
	// formula as on bottom of page 765 of above Dattorro article
	out = in_array[index_i] + index_frac * (in_array[index_iP1] - last_out);
	
	return out;
}

/********************************************************************************
double mcLinearInterp(float *in_array, long index_i, double index_frac, 
		long in_size, short in_chans)

inputs:			*in_array -- name of array of input values
				index_i -- index value of sample, specific channel within interleaved frame
				index_frac -- fractional portion of index value for interp
				in_size -- size of input buffer to perform wrapping
				in_chans -- number of channels in input buffer
description:	performs linear interpolation on an input array and to return 
	value of a fractional sample location
returns:		interpolated output
********************************************************************************/
double mcLinearInterp(float *in_array, long index_i, double index_frac, long in_size, short in_chans)
{
	double out, sample1, sample2;
	long index_iP1 = index_i + in_chans;		// corresponding sample in next frame
	
	// make sure that index_iP1 is not out of range
	while (index_iP1 >= in_size) index_iP1 -= in_size;
	
	// get samples
	sample1 = (double)in_array[index_i];
	sample2 = (double)in_array[index_iP1];
	
	//linear interp formula
	out = sample1 + index_frac * (sample2 - sample1);
	
	return out;
}