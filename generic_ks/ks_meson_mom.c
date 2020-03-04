/********************** ks_meson_mom.c *********************************/
/* MIMD version 7 */

/*  Tie together staggered propagators to make meson correlators
 *
 */

/* Here is the variety of correlators we need to consider  */

/*
spectrum_nd( dyn_mass[0], dyn_mass[1],  1e-2, &fn_links);
spectrum2( dyn_mass[i], F_OFFSET(phi1), F_OFFSET(xxx1), &fn_links);
spectrum_fzw( dyn_mass[i], F_OFFSET(phi1), F_OFFSET(xxx1), &fn_links);
nl_spectrum( dyn_mass[i], F_OFFSET(phi1),   F_OFFSET(xxx1), 
					    F_OFFSET(tempmat1),
					    F_OFFSET(staple),
					    &fn_links);
fpi_2( fpi_mass, fpi_nmasses, 2e-3,  &fn_links );
spectrum_mom( dyn_mass[i], dyn_mass[i],  F_OFFSET(phi1), 1e-1,
					     &fn_links);
spectrum_nlpi2( dyn_mass[i], dyn_mass[i],  F_OFFSET(phi1),1e-1,
					       &fn_links );
spectrum_singlets(dyn_mass[i], 5e-3, F_OFFSET(phi1), &fn_links );

spectrum_hybrids( dyn_mass[i], F_OFFSET(phi1),  5e-3, &fn_links);
*/

/*****************************************

This is a general function that can contract two quark
propagators together to form a meson correlator.

\sum_{x} exp( i p .x } 
     [ O_st src_1 ]^\dagger src_2  )

   where src_1 and src_2 are quark propagators and
   O_st is a spin-taste operator at the sink.

   Note that, unlike the analogous Dirac propagator routine, the
   source spin-taste can not be applied independently of the
   propagator calculation, so it is assumed already to be included in
   the propagators src_1 and/or src_2. Ordinarily, when converting a
   quark propagator running backwards in time to an antiquark running
   forwards in time, we take the adjoint and apply (-)^(x+y+z+t)
   factors at source and sink.  This sink sign factor is applied in
   the call to spin_taste_op_fn, so we don't do it explicitly here.
   This means, for example, that when we specify O_st = pion5 we get
   simply src_1^\dagger src_2.


  Function arguments

     On input 
        src1 :: su3_vector 
        src2 :: su3_vector 
        no_q_momenta :: number of momentum values p in table to use
	q_momstore[p] :: table of momentum values p to use
        q_parity[p] :: reflection parity for each momentum component
        no_gamma_corr :: number of source/sink gamma pairs G in table
        num_corr_mom[g] :: number of momentum/parity sets for each g
        corr_table[g] :: list of correlator indices c for gamma pair g
        p_index[c] :: p = p_index[c] is the momentum index for correlator c 
        gout[c] :: sink gamma matrix for correlator c
        gin[c] :: source gamma matrix for correlator c
        meson_phase[c] :: phase factor to multiply the correlator before
                          accumulating.  Encoded as in gammatypes.h
        meson_factor[c] :: normalization factor for each correlator c
        corr_index[c] :: correlator index m where like propagators are summed

     On output
         prop :: complex vector to the data correlators with indexing
                 prop[m][time] where m is the correlator index.


*******************************************/

#include "generic_ks_includes.h"
#include <string.h>

#define MAXQ 100
typedef struct {
  complex e[MAXQ];
} complex_v;



/* Normalize the correlator contributions */

static double norm_v(complex *tr, complex_v *src, 
		     int phase[], Real factor[],
		     int ct[], int nc, int p_index[])
{
  int k, c, p, ph;
  complex z = {0.,0.};
  Real fact;
  double flops = 0;
  
  /* For each momentum in list, normalize, and phase */

  for(k=0; k<nc; k++){
    c = ct[k];
    p = p_index[c];
    tr[k] = src->e[p];
    ph = phase[c];
    fact = factor[c];
    switch(ph){
    case 0:
      z =            tr[k];
      break;
    case 1:
      TIMESPLUSI(    tr[k], z);
      break;
    case 2:
      TIMESMINUSONE( tr[k], z);
      break;
    case 3:
      TIMESMINUSI(   tr[k], z);
    }
    CMULREAL(z,fact,tr[k]);
  }

  flops = 2*nc;
  
  return flops;
  
} /* norm_v */

/* Calculate FT weight factor */

static complex ff(Real theta, char parity, complex tmp)
{
  complex z = {0.,0.};
  
  if(parity == EVEN){
    z.real = tmp.real*cos(theta);
    z.imag = tmp.imag*cos(theta);
  }
  else if(parity == ODD){
    z.real = -tmp.imag*sin(theta);
    z.imag =  tmp.real*sin(theta);
  }
  else if(parity == EVENANDODD){
    z.real = tmp.real*cos(theta)-tmp.imag*sin(theta);
    z.imag = tmp.imag*cos(theta)+tmp.real*sin(theta);
  }
  else{
    printf("ff(%d): bad parity %d\n", this_node, parity);
    terminate(1);
  }
  return z;
} /* ff */


void ks_meson_cont_mom(
  complex **prop,           /* prop[m][t] is where result is accumulated */
  su3_vector *src1,         /* quark propagator (to become antiquark) */
  su3_vector *src2,         /* quark propagator */
  int no_q_momenta,         /* no of unique mom/parity values (gt p) */
  int **q_momstore,         /* q_momstore[p] are the momentum components */
  char **q_parity,          /* q_parity[p] the parity of each mom component */
  int no_spin_taste_corr,   /* Number of unique spin-taste assignments (gt g) */
  int num_corr_mom[],       /* number of momentum/parity values (gt k)*/
  int **corr_table,         /* c = corr_table[k] correlator index */
  int p_index[],            /* p = p_index[c] is the momentum index */
  imp_ferm_links_t *fn_src1, /* Needed for some spin-taste operators */
  imp_ferm_links_t *fn_src2, /* Needed for some spin-taste operators */
  int spin_taste_snk[],     /* spin_taste_snk[c] gives the s/t assignment */
  int meson_phase[],        /* meson_phase[c] is the correlator phase */
  Real meson_factor[],      /* meson_factor[c] scales the correlator */
  int corr_index[],         /* m = corr_index[c] is the correlator index */
  int r0[]                  /* origin for defining FT and KS phases */
		    )
{
  char myname[] = "meson_cont_mom";
  int i,k,c,m;
  site *s; 
  
  int spin_taste;
  int g,p,t;
  
  double factx = 2.0*PI/(1.0*nx) ; 
  double facty = 2.0*PI/(1.0*ny) ; 
  double factz = 2.0*PI/(1.0*nz) ; 
  int px,py,pz;
  char ex, ey, ez;
  complex fourier_fact ; 
  
  complex *meson;   /* temporary field of complex */
  complex_v *meson_q; /* temporary array of complex vectors of length MAXQ */

  int *nonzero;
  complex tr[MAXQ];
  complex tmp;
  complex *ftfact;
  
  /* performance */
  double dtime;
  double flops;
#ifdef WMTIME
  double mflops;
#endif
  su3_vector *antiquark = create_v_field();
  su3_vector *quark = create_v_field();

  dtime = -dclock();
  flops = 0;

  if(no_q_momenta > MAXQ)
    {
      printf("%s(%d): no_q_momenta %d exceeds max %d\n",
	     myname, this_node, no_q_momenta,MAXQ);
      terminate(1);
    }
  
  meson = (complex *)malloc(sites_on_node*sizeof(complex));
  if(meson == NULL){
    printf("%s(%d): No room for meson\n",myname,this_node);
    terminate(1);
  }
  
  meson_q = (complex_v *)malloc(nt*sizeof(complex_v));
  if(meson_q == NULL){
    printf("%s(%d): No room for meson_q\n",myname,this_node);
    terminate(1);
  }
  
  nonzero = (int *)malloc(nt*sizeof(int));
  if(nonzero == NULL){
    printf("%s(%d): No room for nonzero array\n",myname,this_node);
    terminate(1);
  }
  
  for(t = 0; t < nt; t++)nonzero[t] = 0;
  
  ftfact = (complex *)malloc(no_q_momenta*sites_on_node*sizeof(complex));
  if(ftfact == NULL)
    {
      printf("%s(%d): No room for FFT phases\n",myname,this_node);
      terminate(1);
    }

  /* ftfact contains factors such as cos(kx*x)*sin(ky*y)*exp(ikz*z)
     with factors of cos, sin, and exp selected according to the
     requested component parity */
  
  FORALLSITES(i,s) {
    for(p=0; p<no_q_momenta; p++)
      {
	px = q_momstore[p][0];
	py = q_momstore[p][1];
	pz = q_momstore[p][2];
	
	ex = q_parity[p][0];
	ey = q_parity[p][1];
	ez = q_parity[p][2];
	
	tmp.real = 1.;
	tmp.imag = 0.;
	
	tmp = ff(factx*(s->x-r0[0])*px, ex, tmp);
	tmp = ff(facty*(s->y-r0[1])*py, ey, tmp);
	tmp = ff(factz*(s->z-r0[2])*pz, ez, tmp);
	
	ftfact[p+no_q_momenta*i] = tmp;
      }
  }      
  
  flops += (double)sites_on_node*18*no_q_momenta;
  

  /* Run through the sink spin-taste combinations */

  for(g = 0; g < no_spin_taste_corr; g++)
    {
      /* All spin-taste assignments with the same index g must be the same */
      c = corr_table[g][0];  
      spin_taste = spin_taste_snk[c];

      /* Special treatment for vector-current operators */
      if(is_rhosfn_index(spin_taste) || is_rhosape_index(spin_taste)){
	/* Apply backward sink spin-taste operator to src1 */
	spin_taste_op_fn(fn_src1, backward_index(spin_taste), r0, antiquark, src1);
	/* Apply forward sink spin-taste operator to src2 */
	spin_taste_op_fn(fn_src2, forward_index(spin_taste), r0, quark, src2);
      } else if(is_rhosffn_index(spin_taste) || is_rhosfape_index(spin_taste)){
	/* Apply forward sink spin-taste operator to src2 */
	spin_taste_op_fn(fn_src2, forward_index(spin_taste), r0, quark, src2);
      } else if(is_rhosbfn_index(spin_taste) || is_rhosbape_index(spin_taste)){
	/* Apply backward sink spin-taste operator to src1 */
	spin_taste_op_fn(fn_src1, backward_index(spin_taste), r0, antiquark, src1);
      } else {
	/* Apply sink spin-taste operator to src1 */
	spin_taste_op_fn(fn_src1, spin_taste, r0, antiquark, src1);
      }
	
      /* Do FT on "meson" for momentum projection - 
	 Result in meson_q.  We use a dumb FT because there 
	 are usually very few momenta needed. */

      for(t = 0; t < nt; t++){
	for(k=0; k<num_corr_mom[g]; k++)
	  {
	    c = corr_table[g][k];
	    p = p_index[c];
	    
	    meson_q[t].e[p].real = 0.;
	    meson_q[t].e[p].imag = 0;
	  }
      }

      FORALLSITES(i,s) {

        /* Take dot product of propagators */
	/* Special treatment for vector-current fn spin_taste operators */
	if(is_rhosfn_index(spin_taste) || is_rhosape_index(spin_taste)){
	  complex db,df;
	  db = su3_dot(antiquark+i, src2+i);
	  df = su3_dot(src1+i, quark+i);
	  CADD(db,df,meson[i]);
	  CMULREAL(meson[i],0.5,meson[i]);
	} else if(is_rhosffn_index(spin_taste) || is_rhosfape_index(spin_taste)){
	  meson[i] = su3_dot(src1+i, quark+i);
	} else if(is_rhosbfn_index(spin_taste) || is_rhosbape_index(spin_taste)){
	  meson[i] = su3_dot(antiquark+i, src2+i);
	} else {
	  meson[i] = su3_dot(antiquark+i, src2+i);
	}
  
	/* To save steps below, in case this node doesn't have all
	   time slices */
	nonzero[s->t] = 1;

	for(k=0; k<num_corr_mom[g]; k++)
	  {
	    c = corr_table[g][k];
	    p = p_index[c];
	    fourier_fact = ftfact[p+no_q_momenta*i];
	    
	    meson_q[s->t].e[p].real += 
	      meson[i].real*fourier_fact.real -  
	      meson[i].imag*fourier_fact.imag;
	    meson_q[s->t].e[p].imag += 
	      meson[i].real*fourier_fact.imag +  
	      meson[i].imag*fourier_fact.real;
	  }
      }
      
      flops += (double)sites_on_node*8*num_corr_mom[g];
      
      /* Complete the propagator by tying in the sink gamma.
	 Then store it */
      
      for(t=0; t < nt; t++)if(nonzero[t]) {
	  /* Normalize for all sink momenta q */
	  flops += norm_v(tr, &meson_q[t], 
			  meson_phase, meson_factor, corr_table[g], 
			  num_corr_mom[g], p_index);
	  /* Accumulate in corr_index location */
	  for(k=0; k<num_corr_mom[g]; k++)
	    {
	      c = corr_table[g][k];
	      m = corr_index[c];
	      prop[m][t].real += tr[k].real;
	      prop[m][t].imag += tr[k].imag;
	    }
	}
    }  /**** end of the loop over the spin-taste table ******/

  free(meson);  free(meson_q);  free(nonzero);  free(ftfact);

  destroy_v_field(quark);
  destroy_v_field(antiquark);
  
  dtime += dclock();

/**  flops = sites_on_node*(nmeson_evals*1536 + nmeson_q_evals*128) +
     68*no_q_momenta) + 14*nprop_evals*ntslices*no_q_momenta; **/

#ifdef WMTIME
  if(dtime > 0)mflops = flops/(dtime*1e6);
  else mflops = 0;
  
  node0_printf("WMTIME: time %.1e sec %g flops %.1f MF\n",
	       dtime,flops,mflops);fflush(stdout);
#endif
  
} /* end of meson_cont_mom function  */



/************************************************* NEW FUNCTION WP **********************************************************/

void ks_tetraquark_cont_mom(
  complex **prop,           /* prop[m][t] is where result is accumulated */
  su3_vector *src1,         /* antiquark propagator 1*/
  su3_vector *src2,         /* antiquark propagator2 */
  su3_vector *src3,         /* quark propagator1 */ /* edited WP */
  su3_vector *src4,         /* quark propagator2 */ /* edited WP */
  int no_q_momenta_tetraquark,         /* no of unique mom/parity values (gt p) *//* edited WP */
  int **q_momstore_tetraquark,         /* q_momstore[p] are the momentum components *//* edited WP */
  char **q_parity_tetraquark,          /* q_parity[p] the parity of each mom component *//* edited WP */
  int no_spin_taste_corr_tetraquark,   /* Number of unique spin-taste assignments (gt g) *//* edited WP */
  int num_corr_mom_tetraquark[],       /* number of momentum/parity values (gt k)*//* edited WP */
  int **corr_table_tetraquark,         /* c = corr_table[k] correlator index *//* edited WP */
  int p_index_tetraquark[],            /* p = p_index[c] is the momentum index *//* edited WP */
  imp_ferm_links_t *fn_src1, /* Needed for some spin-taste operators *//* edited WP */
  imp_ferm_links_t *fn_src2, /* Needed for some spin-taste operators *//* edited WP */
  imp_ferm_links_t *fn_src3, /* Needed for some spin-taste operators *//* edited WP */
  imp_ferm_links_t *fn_src4, /* Needed for some spin-taste operators *//* edited WP */
  int spin_taste_snk_tetraquark[],     /* spin_taste_snk[c] gives the s/t assignment */ /* edited WP */
  int tetraquark_phase[],        /* meson_phase[c] is the correlator phase */ /* edited WP */
  Real tetraquark_factor[],      /* meson_factor[c] scales the correlator */ /* edited WP */
  int corr_index_tetraquark[],         /* m = corr_index[c] is the correlator index *//* edited WP */
  int r0_tetraquark[]                  /* origin for defining FT and KS phases *//* edited WP */
		    )
{
  char myname[] = "tetraquark_cont_mom"; /* edited WP */
  int i,k,c,m;
  site *s; 
  
  int spin_taste;
  int g,p,t;
  
  double factx = 2.0*PI/(1.0*nx) ; 
  double facty = 2.0*PI/(1.0*ny) ; 
  double factz = 2.0*PI/(1.0*nz) ; 
  int px,py,pz;
  char ex, ey, ez;
  complex fourier_fact ; 

  complex *mesonA;   /* temporary field of complex */ /* edited WP Meson A is 1st and 3rd quark, ie antiquark1 and quark 1 */
  complex_v *mesonA_q; /* temporary array of complex vectors of length M */ /* edited WP Meson B is 2nd and 4th quark ie antiquark2 and quark 2*/
  complex *mesonB;   /* temporary field of complex */ /* edited WP */
  complex_v *mesonB_q; /* temporary array of complex vectors of length M */ /* edited WP */
  
  complex *tetraquark;   /* temporary field of complex */ /* edited WP */
  complex_v *tetraquark_q; /* temporary array of complex vectors of length M */ /* edited WP */

  int *nonzero;
  complex tr[MAXQ];
  complex tmp;
  complex *ftfact;
  
  /* performance */
  double dtime;
  double flops;
#ifdef WMTIME
  double mflops;
#endif
  su3_vector *antiquark1 = create_v_field(); /* edited WP */
  su3_vector *antiquark2 = create_v_field(); /* edited WP */
  su3_vector *quark1 = create_v_field();  /* edited WP */
  su3_vector *quark2 = create_v_field(); /* edited WP */
  
  dtime = -dclock();
  flops = 0;

  if(no_q_momenta_tetraquark > MAXQ)  /* edited WP */
    {
      printf("%s(%d): no_q_momenta_tetraquark %d exceeds max %d\n",  /* edited WP */
	     myname, this_node, no_q_momenta_tetraquark,MAXQ);  /* edited WP */
      terminate(1);    /* edited WP */
    }
  
  tetraquark = (complex *)malloc(sites_on_node*sizeof(complex)); /* edited WP */
  if(tetraquark == NULL){                                       /* edited WP */
    printf("%s(%d): No room for tetraquark\n",myname,this_node); /* edited WP */
    terminate(1);
  }
  
  tetraquark_q = (complex_v *)malloc(nt*sizeof(complex_v)); /* edited WP */
  if(tetraquark_q == NULL){                                 /* edited WP */
    printf("%s(%d): No room for tetraquark_q\n",myname,this_node); /* edited WP */
    terminate(1);
  }
  
  nonzero = (int *)malloc(nt*sizeof(int));
  if(nonzero == NULL){
    printf("%s(%d): No room for nonzero array\n",myname,this_node);
    terminate(1);
  }
  
  for(t = 0; t < nt; t++)nonzero[t] = 0;
  
  ftfact = (complex *)malloc(no_q_momenta_tetraquark*sites_on_node*sizeof(complex)); /* edited WP */
  if(ftfact == NULL)
    {
      printf("%s(%d): No room for FFT phases\n",myname,this_node);
      terminate(1);
    }

  /* ftfact contains factors such as cos(kx*x)*sin(ky*y)*exp(ikz*z)
     with factors of cos, sin, and exp selected according to the
     requested component parity */
  
  FORALLSITES(i,s) {
    for(p=0; p<no_q_momenta_tetraquark; p++)   /* edited WP */
      {
	px = q_momstore_tetraquark[p][0];  /* edited WP */
	py = q_momstore_tetraquark[p][1]; /* edited WP */
	pz = q_momstore_tetraquark[p][2]; /* edited WP */
	
	ex = q_parity_tetraquark[p][0]; /* edited WP */
	ey = q_parity_tetraquark[p][1]; /* edited WP */
	ez = q_parity_tetraquark[p][2]; /* edited WP */
	
	tmp.real = 1.;
	tmp.imag = 0.;
	
	tmp = ff(factx*(s->x-r0[0])*px, ex, tmp);
	tmp = ff(facty*(s->y-r0[1])*py, ey, tmp);
	tmp = ff(factz*(s->z-r0[2])*pz, ez, tmp);
	
	ftfact[p+no_q_momenta_tetraquark*i] = tmp; /* edited WP */
      }
  }      
  
  flops += (double)sites_on_node*18*no_q_momenta_tetraquark; /* edited WP */
  

  /* Run through the sink spin-taste combinations */

  for(g = 0; g < no_spin_taste_corr_tetraquark; g++)  /* edited WP */
    {
      /* All spin-taste assignments with the same index g must be the same */
      c = corr_table_tetraquark[g][0]; /* edited WP */ 
      spin_taste = spin_taste_snk_tetraquark[c]; /* edited WP */
      /* Probably need to think about this as need a couple of spin tastes to make the tetraquark will get it working for mesons first */ 
      /* Special treatment for vector-current operators */ /* || means or */
      if(is_rhosfn_index(spin_taste) || is_rhosape_index(spin_taste)){
	/* Apply backward sink spin-taste operator to src1 */
	spin_taste_op_fn(fn_src1, backward_index(spin_taste), r0, antiquark1, src1); /*edited WP */
	spin_taste_op_fn(fn_src2, backward_index(spin_taste), r0, antiquark2, src2); /* New Wp */
	/* Apply forward sink spin-taste operator to src2 */
	spin_taste_op_fn(fn_src3, forward_index(spin_taste), r0, quark1, src3); /* edited Wp */
	spin_taste_op_fn(fn_src4, forward_index(spin_taste), r0, quark2, src4); /* New Wp */
      } else if(is_rhosffn_index(spin_taste) || is_rhosfape_index(spin_taste)){
	/* Apply forward sink spin-taste operator to src2 */
	spin_taste_op_fn(fn_src3, forward_index(spin_taste), r0, quark1, src3); /* Edited WP */
	spin_taste_op_fn(fn_src4, forward_index(spin_taste), r0, quark2, src4); /* NEW WP */
      } else if(is_rhosbfn_index(spin_taste) || is_rhosbape_index(spin_taste)){
	/* Apply backward sink spin-taste operator to src1 */
	spin_taste_op_fn(fn_src1, backward_index(spin_taste), r0, antiquark1, src1); /* edited WP */
	spin_taste_op_fn(fn_src2, backward_index(spin_taste), r0, antiquark2, src2); /* New WP */
      } else {
	/* Apply sink spin-taste operator to src1 */
	spin_taste_op_fn(fn_src1, spin_taste, r0, antiquark1, src1); /* edited WP */
	spin_taste_op_fn(fn_src2, spin_taste, r0, antiquark2, src2); /* New WP */
      }
	
      /* Do FT on "meson" for momentum projection - 
	 Result in meson_q.  We use a dumb FT because there 
	 are usually very few momenta needed. */

      for(t = 0; t < nt; t++){
	for(k=0; k<num_corr_mom_tetraquark[g]; k++)
	  {
	    c = corr_table_tetraquark[g][k];
	    p = p_index_tetraquark[c];
	    
	    mesonA_q[t].e[p].real = 0.; /*edited WP */
	    mesonA_q[t].e[p].imag = 0;  /* edited WP*/
	    mesonB_q[t].e[p].real = 0.; /* New WP */
	    mesonB_q[t].e[p].imag = 0;  /* News WP */
	  }
      }

      FORALLSITES(i,s) {

        /* Take dot product of propagators */
	/* Special treatment for vector-current fn spin_taste operators */
	if(is_rhosfn_index(spin_taste) || is_rhosape_index(spin_taste)){
	  complex dbA,dfA,dbB,dfB;
	  dbA = su3_dot(antiquark1+i, src3+i); /* Modified WP */
	  dfA = su3_dot(src1+i, quark1+i);     /* Modified WP */
	  dbB = su3_dot(antiquark2+i, src4+i);  /* New WP Not sure exactly what this is doing*/
	  dfB = su3_dot(src2+i, quark2+i);     /* New WP */
	  CADD(dbA,dfA,mesonA[i]);  /* Modified WP */
	  CADD(dbB,dfB,mesonB[i]);  /* New WP */	  
	  CMULREAL(mesonA[i],0.5,mesonA[i]); /* Modified WP */
	  CMULREAL(mesonB[i],0.5,mesonB[i]); /* New WP */
	} else if(is_rhosffn_index(spin_taste) || is_rhosfape_index(spin_taste)){
	  mesonA[i] = su3_dot(src1+i, quark1+i); /* Modified WP */
	  mesonB[i] = su3_dot(src2+i, quark2+i); /* New WP */
	} else if(is_rhosbfn_index(spin_taste) || is_rhosbape_index(spin_taste)){
	  mesonA[i] = su3_dot(antiquark1+i, src3+i); /* Modified WP */
	  mesonB[i] = su3_dot(antiquark2+i, src4+i); /* New WP */
	} else {
	  mesonA[i] = su3_dot(antiquark1+i, src3+i); /* Modified WP */
	  mesonB[i] = su3_dot(antiquark2+i, src4+i); /* New WP */
	}
  
	/* To save steps below, in case this node doesn't have all
	   time slices */
	nonzero[s->t] = 1;

	for(k=0; k<num_corr_mom_tetraquark[g]; k++) /* Modified WP */
	  {
	    c = corr_table_tetraquark[g][k];  /* Modified WP */
	    p = p_index_tetraquark[c]; /* modified WP */
	    fourier_fact = ftfact[p+no_q_momenta_tetraquark*i]; /* modified WP */
	    
	    mesonA_q[s->t].e[p].real += 
	      mesonA[i].real*fourier_fact.real -  
	      mesonA[i].imag*fourier_fact.imag;
	    mesonA_q[s->t].e[p].imag += 
	      mesonA[i].real*fourier_fact.imag +  
	      mesonA[i].imag*fourier_fact.real;
	    mesonB_q[s->t].e[p].real += 
	      mesonB[i].real*fourier_fact.real -  
	      mesonB[i].imag*fourier_fact.imag;
	    mesonB_q[s->t].e[p].imag += 
	      mesonB[i].real*fourier_fact.imag +  
	      mesonB[i].imag*fourier_fact.real;
	  }
      }
      
      flops += (double)sites_on_node*8*num_corr_mom_tetraquark[g];   
      
      /* Complete the propagator by tying in the sink gamma.
	 Then store it */
      
      for(t=0; t < nt; t++)if(nonzero[t]) {
	  /* Normalize for all sink momenta q */
	  flops += norm_v(tr, &mesonA_q[t], 
			  tetraquark_phase, tetraquark_factor, corr_table_tetraquark[g], 
			  num_corr_mom_tetraquark[g], p_index_tetraquark);   /* Edited WP, for the moment should just output mesonA*/
	  /* Accumulate in corr_index location */
	  for(k=0; k<num_corr_mom_tetraquark[g]; k++)
	    {
	      c = corr_table_tetraquark[g][k];
	      m = corr_index_tetraquark[c];
	      prop[m][t].real += tr[k].real;
	      prop[m][t].imag += tr[k].imag;
	    }
	}
    }  /**** end of the loop over the spin-taste table ******/

  free(mesonA);  free(mesonB);  free(mesonA_q);  free(mesonB_q);  free(nonzero);  free(ftfact);

  destroy_v_field(quark1);
  destroy_v_field(antiquark1);
  destroy_v_field(quark2);
  destroy_v_field(antiquark2);
  
  dtime += dclock();

/**  flops = sites_on_node*(nmeson_evals*1536 + nmeson_q_evals*128) +
     68*no_q_momenta) + 14*nprop_evals*ntslices*no_q_momenta; **/

#ifdef WMTIME
  if(dtime > 0)mflops = flops/(dtime*1e6);
  else mflops = 0;
  
  node0_printf("WMTIME: time %.1e sec %g flops %.1f MF\n",
	       dtime,flops,mflops);fflush(stdout);
#endif
  
} /* end of meson_cont_mom function  */
