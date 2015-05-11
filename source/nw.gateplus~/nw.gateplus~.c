/*
** nw.gateplus~.c
**
** MSP object
** open and close gate to allow a signal through but
** starts & stops only after zero crossing occurs
** 2015/05/10 started
**
** Copyright © 2015 by Nathan Wolek
** License: http://opensource.org/licenses/BSD-3-Clause
**
*/

#include "ext.h"		// required for all MAX external objects
#include "ext_obex.h"   // required for new style MAX objects
#include "z_dsp.h"		// required for all MSP external objects
#include <string.h>

#define DEBUG			//enable debugging messages

#define OBJECT_NAME		"nw.gateplus~"		// name of the object

/* for the assist method */
#define ASSIST_INLET	1
#define ASSIST_OUTLET	2

/* for gate stage flag */
#define GATE_CLOSED			0
#define MONITOR_OPEN		1
#define GATE_OPEN			2
#define MONITOR_CLOSED		3

static t_class *gateplus_class;		// required global pointing to this class

typedef struct _gateplus
{
    t_pxobject x_obj;					// <--
    
    // current gate info
    short gate_stage;       // see flags in header
    
    //history
    double last_ctrl_in;
    double last_sig_in;
    
} t_gateplus;

/* method definitions for this object */
void *gateplus_new(long outlets);
void gateplus_dsp64(t_gateplus *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void gateplus_perform64(t_gateplus *x, t_object *dsp64, double **ins, long numins, double **outs,long numouts, long vectorsize, long flags, void *userparam);


/********************************************************************************
 void main(void)
 
 inputs:			nothing
 description:	called the first time the object is used in MAX environment;
 defines inlets, outlets and accepted messages
 returns:		int
 ********************************************************************************/
int C74_EXPORT main(void)
{
    t_class *c;
    
    c = class_new(OBJECT_NAME, (method)gateplus_new, (method)dsp_free,
                  (short)sizeof(t_gateplus), 0L, 1, 0);
    class_dspinit(c); // add standard functions to class
    
    /* bind method "gateplus_dsp64" to the dsp64 message */
    class_addmethod(c, (method)gateplus_dsp64, "dsp64", A_CANT, 0);
    
    class_register(CLASS_BOX, c); // register the class w max
    gateplus_class = c;
    
    #ifndef DEBUG
        
    #endif /* DEBUG */
}

/********************************************************************************
 void *gateplus_new(long outlets)
 
 inputs:			outlets		-- NOT USED YET`
 description:	called for each new instance of object in the MAX environment;
 defines inlets and outlets; sets argument for number of outlets
 returns:		nothing
 ********************************************************************************/
void *gateplus_new(long outlets)
{
    t_gateplus *x = (t_gateplus *) object_alloc((t_class*) gateplus_class);
    
    dsp_setup((t_pxobject *)x, 2);					// two inlets
    outlet_new((t_pxobject *)x, "signal");			// one outlet
    
    /* setup variables */
    x->last_ctrl_in = 0.0;
    x->last_sig_in = 0.0;
    
    /* set flags to defaults */
    x->gate_stage = GATE_CLOSED;
    
    x->x_obj.z_misc = Z_NO_INPLACE;
    
    #ifdef DEBUG
        post("%s: new function was called", OBJECT_NAME);
    #endif /* DEBUG */
    
    /* return a pointer to the new object */
    return (x);
}

/********************************************************************************
 void gateplus_dsp64()
 
 inputs:     x		-- pointer to this object
 dsp64		-- signal chain to which object belongs
 count	-- array detailing number of signals attached to each inlet
 samplerate -- number of samples per second
 maxvectorsize -- sample frames per vector of audio
 flags --
 description:	called when 64 bit DSP call chain is built; adds object to signal flow
 returns:		nothing
 ********************************************************************************/
void gateplus_dsp64(t_gateplus *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
{
#ifdef DEBUG
    post("%s: adding 64 bit perform method", OBJECT_NAME);
#endif /* DEBUG */
    
    if (count[1]) { // if signal input is connected
        #ifdef DEBUG
            post("%s: output is being computed", OBJECT_NAME);
        #endif /* DEBUG */
        dsp_add64(dsp64, (t_object*)x, (t_perfroutine64)gateplus_perform64, 0, NULL);
    } else {
        #ifdef DEBUG
            post("%s: no output computed", OBJECT_NAME);
        #endif /* DEBUG */
    }
    
}

/********************************************************************************
 void *gateplus_perform64()
 
 inputs:	x		--
 dsp64   --
 ins     --
 numins  --
 outs    --
 numouts --
 vectorsize --
 flags   --
 userparam  --
 description:	called at interrupt level to compute object's output at 64-bit,
 writes zeros to every outlet
 returns:		nothing
 ********************************************************************************/
void gateplus_perform64(t_gateplus *x, t_object *dsp64, double **ins, long numins, double **outs, long numouts, long vectorsize, long flags, void *userparam)
{
    // local vars outlets and inlets
    t_double *in_ctrl = ins[0];
    t_double *in_signal = ins[1];
    t_double *out_signal = outs[0];
    
    // local vars for object vars and while loop
    long n;
    short g_stage;
    double lc_in, ls_in;
    
    // check to make sure object is enabled
    if (x->x_obj.z_disabled) goto out; // if not, skip ahead
    
    // assign values to local vars
    g_stage = x->gate_stage;
    lc_in = x->last_ctrl_in;
    ls_in = x->last_sig_in;
    
    n = vectorsize;
    while (n--) {
        
        // temporarily write zeros
        *out_signal = 0.;
        
        // advance pointers
        ++in_ctrl, ++in_signal, ++out_signal;
    }
    
    // update global vars
    x->gate_stage = g_stage;
    x->last_ctrl_in = lc_in;
    x->last_sig_in = ls_in;
    
out:
    return;
    
}

