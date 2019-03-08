#include "heating.h"
/** ENERGY INJECTION FUNCTIONS
 *
 * Developed by Vivian Poulin (added functions for energy repartition from DM annihilations or decays and f_eff),
 *              Patrick Stöcker (20.02.17: added external script to calculate the annihilation coefficients on the fly) and
 *              Matteo Lucca (11.02.19: rewrote section in CLASS style)
 *              Nils Schoeneberg (6.03.19: Added struct and module handling)
 */
#define deposit_on_the_spot 0
#define deposit_feff_from_file 1
#define deposit_from_dark_ages 2
#define deposit_analytical_integral 3

int heating_init(struct precision * ppr, struct background* pba, struct thermo* pth){

  struct heating* phe = &(pth->he);
  phe->H0 = pba->H0*_c_/_Mpc_over_m_;
  phe->Omega0_cdm = pba->Omega0_cdm;
  phe->rho_crit0 = phe->H0*phe->H0*3/8./_PI_/_G_*_c_*_c_;
  phe->last_index_bg = 0;
  phe->Gamma_dcdm = pba->Gamma_dcdm;

  phe->deposit_energy_as = 0; //TODO :: set in input
  /** - check energy injection parameters for annihilation */
  class_test((phe->annihilation_efficiency<0),
             phe->error_message,
             "annihilation parameter cannot be negative");

  class_test((phe->annihilation_efficiency>1.e-4),
             phe->error_message,
             "annihilation parameter suspiciously large (%e, while typical bounds are in the range of 1e-7 to 1e-6)",phe->annihilation_efficiency);

  class_test((phe->annihilation_variation>0),
             phe->error_message,
             "annihilation variation parameter must be negative (decreasing annihilation rate)");

  class_test((phe->annihilation_z<0),
             phe->error_message,
             "characteristic annihilation redshift cannot be negative");

  class_test((phe->annihilation_zmin<0),
             phe->error_message,
             "characteristic annihilation redshift cannot be negative");

  class_test((phe->annihilation_zmax<0),
             phe->error_message,
             "characteristic annihilation redshift cannot be negative");

  class_test((phe->annihilation_efficiency>0) && (pba->has_cdm==_FALSE_),
             phe->error_message,
             "CDM annihilation effects require the presence of CDM!");

  class_test((phe->annihilation_f_halo<0),
             phe->error_message,
             "Parameter for DM annihilation in halos cannot be negative");

  class_test((phe->annihilation_z_halo<0),
             phe->error_message,
             "Parameter for DM annihilation in halos cannot be negative");

  if (phe->heating_verbose > 0){
    if ((phe->annihilation_efficiency >0) && (pth->reio_parametrization == reio_none) && (ppr->recfast_Heswitch >= 3) && (pth->recombination==recfast))
      printf("Warning: if you have DM annihilation and you use recfast with option recfast_Heswitch >= 3, then the expression for CfHe_t and dy[1] becomes undefined at late times, producing nan's. This is however masked by reionization if you are not in reio_none mode.");
  } //TODO :: check if still occurs !!!

  class_test((phe->decay<0),
             phe->error_message,
             "decay parameter cannot be negative");

  class_test((phe->decay>0)&&(pba->has_cdm==_FALSE_),
             phe->error_message,
             "CDM decay effects require the presence of CDM!");

  //phe->has_exotic_injection = phe->annihilation!=0 || phe->decay!=0 || phe->PBH_accreting_mass!=0 || phe->PBH_evaporating_mass != 0;
  phe->has_exotic_injection = phe->annihilation!=0 || phe->decay!=0;

  printf("Known things : \n");
  printf("%.10e , %.10e , %.10e , %.10e , %.10e \n",phe->annihilation_f_halo,phe->annihilation_z_halo,phe->annihilation_efficiency,-1.,-1.);
  phe->z_size = pth->tt_size;
  class_alloc(phe->z_table,
              phe->z_size*sizeof(double),
              phe->error_message);

  memcpy(phe->z_table,pth->z_table,phe->z_size*sizeof(double));

  class_call(heating_indices(pth),
             phe->error_message,
             phe->error_message);


  phe->decay_fraction *= _c_/_Mpc_over_m_; //TODO :: units
  return _SUCCESS_;
}

int heating_indices(struct thermo* pth){

  struct heating* phe = &(pth->he);


  /* For injection table */
  int index_inj;
  class_define_index(phe->index_inj_BAO      ,_TRUE_            , index_inj, 1);
  class_define_index(phe->index_inj_CRR      ,_TRUE_            , index_inj, 1);
  class_define_index(phe->index_inj_DM_ann   ,phe->has_DM_ann   , index_inj, 1);
  class_define_index(phe->index_inj_DM_dec   ,phe->has_DM_dec   , index_inj, 1);
  class_define_index(phe->index_inj_BH_acc   ,phe->has_BH_acc   , index_inj, 1);
  class_define_index(phe->index_inj_BH_evap  ,phe->has_BH_evap  , index_inj, 1);
  class_define_index(phe->index_inj_tot      ,_TRUE_            , index_inj, 1);
  phe->inj_size = index_inj;


  /* For deposition (and chi) table */
  int index_dep;
  class_define_index(phe->index_dep_heat  ,_TRUE_, index_dep, 1);
  class_define_index(phe->index_dep_ionH  ,_TRUE_, index_dep, 1);
  class_define_index(phe->index_dep_ionHe ,_TRUE_, index_dep, 1);
  class_define_index(phe->index_dep_lya   ,_TRUE_, index_dep, 1);

  phe->dep_size = index_dep;



  return _SUCCESS_;
}

/*
 * At some point, distortions.c will call this function,
 * and the acoustic dissipation contributions will be added to the table of heatings
 * */
int heating_add_second_order_terms(struct thermo* pth, struct perturbs* ppt){

  struct heating* phe = &(pth->he);
  class_define_index(phe->index_ht_BAO,_TRUE_,phe->ht_size,1);

  return _SUCCESS_;
}


int heating_energy_injection_at_z(struct heating* phe, double z, double* dEdz_inj){

  /** Define local variable */
  double dEdz, rate;

  /* Initialize local variables */
  dEdz = 0.;

  /* Check if already in table */
  //IF TABLE
  // return_table = interpolate(phe->inj_table);
  // return _SUCCESS_; (TODO :: code this)

  /* Non-exotic energies ...  */

  // dEdz += non-exotic-stuff (TODO :: put here non-exotic stuff)

  /** exotic energy injection mechanisms´ǘp+ */
  if(phe->has_exotic_injection){

    /* Annihilating Dark Matter */
    if(phe->has_DM_ann){
      class_call(heating_DM_annihilation(phe,z,&rate),
                 phe->error_message,
                 phe->error_message);
      phe->injection_table[index_z*phe->inj_size+index_inj_DM_ann] = rate;
      phe->injection_table[index_z*phe->inj_size+index_inj_tot] += rate; 
    }

    /* Decaying Dark Matter */
    if(phe->has_DM_dec){
      class_call(heating_DM_decay(phe,z,&rate),
                 phe->error_message,
                 phe->error_message);
      phe->injection_table[index_z*phe->inj_size+index_inj_DM_dec] = rate;
      phe->injection_table[index_z*phe->inj_size+index_inj_tot] += rate; 
    }

    // dEdz += exotic-stuff (TODO :: put here exotic stuff)

  }


  return _SUCCESS_;
}






/* Check if table extends to given z
 *  If yes)
 *   Interpolate from table all types that are known
 *   (i.e. including acous. diss. if already added)
 *  If no)
 *   Calculate heating as required
 **/
int heating_at_z(struct background* pba, struct thermo* pth, double x, double z, double* dQdz, double* dxdz, double* pvecback){

  /** Define local variables */
  double tau;
  struct heating* phe = &(pth->he);


  // [J/m^3] //TODO :: fix this
  phe->rho_cdm = pvecback[pba->index_bg_rho_cdm]*
  //_GeVcm3_over_Mpc4_*_eV_*1e9*1e6;   
  _Mpc_over_m_*_Mpc_over_m_*3/8./_PI_/_G_*_c_*_c_;

  phe->rho_dcdm = pvecback[pba->index_bg_rho_dcdm]*
  //_GeVcm3_over_Mpc4_*_eV_*1e9*1e6;   
  _Mpc_over_m_*_Mpc_over_m_*3/8./_PI_/_G_*_c_*_c_;

  phe->t = pvecback[pba->index_bg_time];


  /* Check if already in table */
  //IF TABLE
  // return_table = interpolate(phe->dep_table);
  // return _SUCCESS_; (TODO :: code this)
  /** Step 1 - get the injected energy that needs to be deposited */
  class_call(heating_energy_injection_at_z(phe,z,&dEdz_inj),
             phe->error_message,
             phe->error_message);

  //Original ExoCLASS piece::
  //preco->xe_tmp=x;
  //preco->Tm_tmp=Tmat;
  //if( z > 2){//sometimes problem with interpolation //TODO :: why like this in ExoCLASS

  /* In the case of the analytical integral, the energy injection is somewhat special */
  if(phe->deposit_energy_as == deposit_analytical_integral){
    class_call(heating_deposit_analytical_integral(pba,pth,phe,z,&dEdz_inj),
               phe->error_message,
               phe->error_message);
  }

  /** Step 2 - Now deposit the energy we have injected */
  class_call(heating_deposition_function(phe,x,z),
             phe->error_message,
             phe->error_message);

  /** Step 3 - Put result into deposition table*/
  for(index_dep = 0; index_dep < phe->dep_size; ++index_dep){
    phe->deposition_table[index_z*phe->dep_size+index_dep] = phe->chi_table[index_dep] * dEdz_inj;
  }

  /** The output is now successfully stored in the deposition table */
  // return_table = phe->dep_table (TODO :: code this)

  //Original ExoCLASS piece ::
  //}
  //else energy_rate = 0;
  //preco->z_tmp=z;

  return _SUCCESS_;
}


int heating_deposition_function(){

  double f_eff;

  f_eff = 1.; //Default value

  /** Step 1 - Read the deposition factors for each channel */
  if (x < 1.){ //TODO :: why is this a good condition ???!?

    /* coefficient as revised by Galli et al. 2013 (in fact it is an interpolation by Vivian Poulin of columns 1 and 2 in Table V of Galli et al. 2013) */
    /* Read file in ionization fraction */
    if(phe->chi_type == chi_from_GSVI || phe->chi_type == chi_from_x_file){
      class_call(heating_chi_from_x(phe,x),
                 phe->error_message,
                 phe->error_message);
    }
    /* Read file in redshift */
    if(phe->chi_type == chi_from_DarkAges || phe->chi_type == chi_from_z_file){
      class_call(heating_chi_from_z(phe,z),
                 phe->error_message,
                 phe->error_message);
    }
    /* old approximation from Chen and Kamionkowski */
    if(phe->chi_type == chi_from_SSCK){
      phe->chi_table[phe->index_dep_heat]  = (1.+2.*x)/3.;
      phe->chi_table[phe->index_dep_ionH]  = (1.-x)/3.;
      phe->chi_table[phe->index_dep_ionHe] = 0.;
      phe->chi_table[phe->index_dep_lya]   = (1.-x)/3.;
    }

  }
  else{
    phe->chi_table[phe->index_dep_heat]  = 1.;
    phe->chi_table[phe->index_dep_ionH]  = 0.;
    phe->chi_table[phe->index_dep_ionHe] = 0.;
    phe->chi_table[phe->index_dep_lya]   = 0.;
  }

  //chi_heat = MIN(chi_heat,1.);
  //chi_heat = MAX(chi_heat,0.);




  /** Step 2 - Read the correction factor f_eff */

  /* For the analytical integral, we already integrated the deposition */
  if(phe->deposit_energy_as == deposit_analytical_integral){
    f_eff = 1.;
  }
  /* For the file, read in f_eff from file and multiply */
  else if(phe->deposit_energy_as == deposit_feff_from_file){
    class_call(thermodynamics_annihilation_f_eff_interpolate(ppr,pba,pth,z,&f_eff),
               pth->error_message,
               pth->error_message);
    f_eff = MAX(f_eff,0.);
  }
  /* For the DarkAges, the chi already contain everything */
  else if(phe->deposit_energy_as == deposit_from_Dark_Ages){
    f_eff = 1.;
  }
  /* For the on the spot, ... */ //TODO :: correct commenting
  else if(phe->deposit_energy_as == deposit_on_the_spot){
    if(pth->f_eff>0){ //DAFUQ IS THIS? ?? ?? ? TODO :: remove this crap
      f_eff = pth->f_eff; // If pth->f_eff is defined, here we multiply by f_eff.
    }
  }
  /* Otherwise, something must have gone wrong */
  else{
    class_stop(phe->error_message,
               "Unknown energy deposition mechanism");
  }




  /** Step 3 - Multiply both to get the desired result */

  /* Multiply deposition factors with overall correction factor */
  for(index_dep=0;index_dep<phe->dep_size;++index_dep){
    phe->chi_table[index_dep] *= f_eff;
  }

  return _SUCCESS_;
}

int heating_free(struct thermo* pth){

  struct heating* phe = &(pth->he);

  free(phe->z_table);
  free(phe->chi_table);

  return _SUCCESS_;
}








#pragma INCOMPLETE
int heating_DM_annihilation(struct heating * phe,
                            double z,
                            double * energy_rate){
  double boost_factor;

  if(phe->annihilation_z_halo > 0.){
    boost_factor = phe->annihilation_f_halo * erfc((1+z)/(1+phe->annihilation_z_halo)) / pow(1.+z,3);
  }
  else{
    boost_factor = 0;
  }

  *energy_rate = phe->rho_cdm*phe->rho_cdm/_c_/_c_ * phe->annihilation_efficiency * (1.+boost_factor);  // [J/(m^3 s)]

  return _SUCCESS_;
}

int heating_DM_decay(struct heating * phe,
                     double z,
                     double * energy_rate){
  double rho_cdm_today, rho_dcdm,decay_factor;

  /* Define energy density of decaying DM, rho_dcdm */
  if(pba->Omega_ini_dcdm!=0 || pba->Omega0_dcdmdr !=0){
    rho_dcdm = phe->rho_dcdm;
  }

  else{
    /* If Omega_dcdm is not given, rho_dcdm = rho_cdm*decay factor */
    if(phe->has_on_the_spot == _FALSE_){
      /* The exponential decay factor is already in the deposition functions */
      decay_factor=1;
    }
    else{
      /* Add the exponential decay factor*/
      decay_factor = exp(-phe->Gamma_dcdm*phe->t);
    }

    /* Surviving rho dcdm*/
    rho_dcdm = phe->rho_cdm*decay_factor;
  }

  *energy_rate = rho_dcdm*phe->decay_fraction*phe->Gamma_dcdm; //[J/m^3 * ? * Mpc^(-1)]

  return _SUCCESS_;
}






#pragma NOT_YET_PROPERLY_DONE

int heating_energy_integrated(struct background* pba, struct thermo* pth, double z, double* energy_rate){

  double zp,dz;
  double integrand,first_integrand;
  double factor,result;
  double nH0;
  double onthespot;
  double exponent_z,exponent_zp;

  nH0 = 3.*pba->H0*pba->H0*pba->Omega0_b/(8.*_PI_*_G_*_m_H_)*(1.-pth->YHe); // number of hydrogen nuclei today in m^-3

  /* Value from Poulin et al. 1508.01370 */
  /* 
  factor = c sigma_T n_H(0) / (H(0) \sqrt(Omega_m)) (dimensionless)
  factor = _sigma_ * nH0 / pba->H0 * _Mpc_over_m_ / sqrt(pba->Omega0_b+pba->Omega0_cdm);
  exponent_z = 8;
  exponent_zp = 7.5;
  */

  /* Value from Ali-Haimoud & Kamionkowski 1612.05644 */
  factor = 0.1*_sigma_ * nH0 / pba->H0 * _Mpc_over_m_ / sqrt(pba->Omega0_b+pba->Omega0_cdm);
  exponent_z = 7;
  exponent_zp = 6.5;

  /* integral over z'(=zp) with step dz */
  dz=1.;

  /* first point in trapezoidal integral */
  zp = z;
  class_call(thermodynamics_onthespot_energy_injection(ppr,pba,pth,zp,&onthespot,error_message),
             error_message,
             error_message);
  first_integrand = factor*pow(1+z,exponent_z)/pow(1+zp,exponent_zp)*exp(2./3.*factor*(pow(1+z,1.5)-pow(1+zp,1.5)))*onthespot; 
  // beware: versions before 2.4.3, there were rwrong exponents: 6 and 5.5 instead of 7 and 7.5
  result = 0.5*dz*first_integrand;

  /* other points in trapezoidal integral */
  do{
    zp += dz;
    class_call(thermodynamics_onthespot_energy_injection(ppr,pba,pth,zp,&onthespot,error_message),
               error_message,
               error_message);
    integrand = factor*pow(1+z,exponent_z)/pow(1+zp,exponent_zp)*exp(2./3.*factor*(pow(1+z,1.5)-pow(1+zp,1.5)))*onthespot; 
    // beware: versions before 2.4.3, there were rwrong exponents: 6 and 5.5 instead of 7 and 7.5
    result += dz*integrand;

  } while (integrand/first_integrand > 0.02);
  if(result < 1e-100) result=0.;

  return _SUCCESS_;
}

#ifdef NOTDEFINED_DEFINITELY_LUL

/**
 * Read and interpolate energy injection coefficients from external file
 *
 * @param ppr Input: pointer to precision structure
 * @param pba Input: pointer to background structure
 * @param pth Input/Output: pointer to initialized thermo structure
 * @return the error status
 */
int heating_annihilation_coefficients_init(struct precision * ppr,
                                           struct background * pba,
                                           struct thermo * pth) {

  FILE * fA = NULL;
  char line[_LINE_LENGTH_MAX_];
  char * left;

  /* variables related to the use of an external code to calculate the annihilation coefficients */
  char command_with_arguments[2*_ARGUMENT_LENGTH_MAX_];
  int status;

  int num_lines=0;
  int array_line=0;

  /** Find file containing injection coefficients */

  /* the following file is assumed to contain (apart from comments and blank lines):
     - One number (num_lines) = number of lines of the file
     - six columns (xe , chi_heat, chi_Lya, chi_H, chi_He, chi_lowE) where each chi represents the fraction 
       of energy going respectively into heat, excitation of lyman-alpha level, Hydrogen ionisation, Helium ionisation,
       photons below 10.2 eV unseeable by the IGM. */
  if (pth->energy_deposition_function == function_from_file || pth->energy_repart_coefficient == GSVI || 
      pth->energy_repart_coefficient == chi_from_file) {

    class_open(fA, ppr->energy_injec_coeff_file, "r", pth->error_message);
  } 
  else {
    /* Write the command */
    sprintf(command_with_arguments, "%s", ppr->command_fz);

    if (pth->heating_verbose > 0) {
      printf(" -> running: %s\n", command_with_arguments);
    }

    /* Launch the process and retrieve the output */
    fflush(fA);
    fA = popen(command_with_arguments, "r");
    class_test(fA == NULL, pth->error_message, "The program failed to set the environment for the external command.");
  }

  /** Read injection coefficients from file */

  while (fgets(line,_LINE_LENGTH_MAX_-1,fA) != NULL) {
    /* eliminate blank spaces at beginning of line */
    left=line;
    while (left[0]==' ') {
      left++;
    }

    /* check that the line is neither blank nor a comment. In ASCII, left[0]>39 means that first non-blank charachter might
       be the beginning of some data (it is not a newline, a #, a %, etc.) */
    if (left[0] > 39) {

      /* if the line contains data, we must interprete it. If num_lines == 0 , the current line must contain
         its value. Otherwise, it must contain (xe , chi_heat, chi_Lya, chi_H, chi_He, chi_lowE). */
      if (num_lines == 0) {

        /* read num_lines, infer size of arrays and allocate them */
        class_test(sscanf(line,"%d",&num_lines) != 1,
                   pth->error_message,
                   "could not read value of parameters num_lines in file %s\n",ppr->energy_injec_coeff_file);
        class_alloc(pth->annihil_coef_xe,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_coef_heat,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_coef_lya,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_coef_ionH,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_coef_ionHe,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_coef_lowE,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_coef_dd_heat,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_coef_dd_lya,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_coef_dd_ionH,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_coef_dd_ionHe,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_coef_dd_lowE,num_lines*sizeof(double),pth->error_message);
        pth->annihil_coef_num_lines = num_lines;

        array_line=0;

      }
      else {
        /* read coefficients */
        class_test(sscanf(line,"%lg %lg %lg %lg %lg %lg",
                          &(pth->annihil_coef_xe[array_line]),
                          &(pth->annihil_coef_heat[array_line]),
                          &(pth->annihil_coef_lya[array_line]),
                          &(pth->annihil_coef_ionH[array_line]),
                          &(pth->annihil_coef_ionHe[array_line]),
                          &(pth->annihil_coef_lowE[array_line])) != 6,
                   pth->error_message,
                   "could not read value of parameters coeeficients in file %s\n",ppr->energy_injec_coeff_file);
        if(pth->print_energy_deposition_function){
          if(array_line == 0){
                fprintf(stdout,"##################################################\n### This is the standardized output to be read by CLASS.\n### For the correct usage ensure that all other\n###'print'-commands in your script are silenced.\n##################################################\n#z_dep	f_heat	f_lya	f_ionH	f_ionHe	f_lowE\n");
          }
          printf("%e %e %e %e %e %e \n",
          (pth->annihil_coef_xe[array_line]),
          (pth->annihil_coef_heat[array_line]),
          (pth->annihil_coef_lya[array_line]),
          (pth->annihil_coef_ionH[array_line]),
          (pth->annihil_coef_ionHe[array_line]),
          (pth->annihil_coef_lowE[array_line]));
        }
        array_line ++;
      }
    }
  }

  /** Close file containing injection coefficients */

  if (pth->energy_deposition_function == function_from_file || pth->energy_repart_coefficient == GSVI || 
      pth->energy_repart_coefficient == chi_from_file) {
    fclose(fA);
  } 
  else {
    status = pclose(fA);
    class_test(status != 0., pth->error_message, "The attempt to launch the external command was not successful. Maybe the output of the external command is not in the right format.");
  }

  /** Interpolate injection coefficients */

  class_call(array_spline_table_lines(pth->annihil_coef_xe,
                                      num_lines,
                                      pth->annihil_coef_heat,
                                      1,
                                      pth->annihil_coef_dd_heat,
                                      _SPLINE_NATURAL_,
                                      pth->error_message),
             pth->error_message,
             pth->error_message);
  class_call(array_spline_table_lines(pth->annihil_coef_xe,
                                      num_lines,
                                      pth->annihil_coef_lya,
                                      1,
                                      pth->annihil_coef_dd_lya,
                                      _SPLINE_NATURAL_,
                                      pth->error_message),
             pth->error_message,
             pth->error_message);
  class_call(array_spline_table_lines(pth->annihil_coef_xe,
                                      num_lines,
                                      pth->annihil_coef_ionH,
                                      1,
                                      pth->annihil_coef_dd_ionH,
                                      _SPLINE_NATURAL_,
                                      pth->error_message),
             pth->error_message,
             pth->error_message);
  class_call(array_spline_table_lines(pth->annihil_coef_xe,
                                     num_lines,
                                     pth->annihil_coef_ionHe,
                                     1,
                                     pth->annihil_coef_dd_ionHe,
                                     _SPLINE_NATURAL_,
                                     pth->error_message),
              pth->error_message,
              pth->error_message);
  class_call(array_spline_table_lines(pth->annihil_coef_xe,
                                      num_lines,
                                      pth->annihil_coef_lowE,
                                      1,
                                      pth->annihil_coef_dd_lowE,
                                      _SPLINE_NATURAL_,
                                      pth->error_message),
               pth->error_message,
               pth->error_message);

  return _SUCCESS_;

}


/**
 * This function is used by the energy injection module for two different interpolations:
 * Either to directly interpolate the f(z) functions per channels or to interpolate
 * the chi(x_e) functions per channels when the factorisation approximation is assumed.
 *
 * @param ppr Input: pointer to precision structure
 * @param pba Input: pointer to background structure
 * @param pth Input/Output: pointer to initialized thermo structure
 * @return the error status
 */
int heating_annihilation_coefficients_interpolate(struct precision * ppr,
                                                         struct background * pba,
                                                         struct thermo * pth,
                                                         double xe_or_z) {

  int last_index;

  class_call(array_interpolate_spline(pth->annihil_coef_xe,
                                      pth->annihil_coef_num_lines,
                                      pth->annihil_coef_heat,
                                      pth->annihil_coef_dd_heat,
                                      1,
                                      xe_or_z,
                                      &last_index,
                                      &(pth->chi_heat),
                                      1,
                                      pth->error_message),
             pth->error_message,
             pth->error_message);
  class_call(array_interpolate_spline(pth->annihil_coef_xe,
                                      pth->annihil_coef_num_lines,
                                      pth->annihil_coef_lya,
                                      pth->annihil_coef_dd_lya,
                                      1,
                                      xe_or_z,
                                      &last_index,
                                      &(pth->chi_lya),
                                      1,
                                      pth->error_message),
             pth->error_message,
             pth->error_message);
  class_call(array_interpolate_spline(pth->annihil_coef_xe,
                                      pth->annihil_coef_num_lines,
                                      pth->annihil_coef_ionH,
                                      pth->annihil_coef_dd_ionH,
                                      1,
                                      xe_or_z,
                                      &last_index,
                                      &(pth->chi_ionH),
                                      1,
                                      pth->error_message),
             pth->error_message,
             pth->error_message);
    class_call(array_interpolate_spline(pth->annihil_coef_xe,
                                        pth->annihil_coef_num_lines,
                                        pth->annihil_coef_ionHe,
                                        pth->annihil_coef_dd_ionHe,
                                        1,
                                        xe_or_z,
                                        &last_index,
                                        &(pth->chi_ionHe),
                                        1,
                                        pth->error_message),
               pth->error_message,
               pth->error_message);
    class_call(array_interpolate_spline(pth->annihil_coef_xe,
                                        pth->annihil_coef_num_lines,
                                        pth->annihil_coef_lowE,
                                        pth->annihil_coef_dd_lowE,
                                        1,
                                        xe_or_z,
                                        &last_index,
                                        &(pth->chi_lowE),
                                        1,
                                        pth->error_message),
               pth->error_message,
               pth->error_message);

  return _SUCCESS_;

}


/**
 * Free all memory space allocated by heating_annihilation_coefficients_interpolate().
 *
 * @param pth Input/Output: pointer to thermo structure
 * @return the error status
 */
int heating_annihilation_coefficients_free(struct thermo * pth) {

  free(pth->annihil_coef_xe);
  free(pth->annihil_coef_heat);
  free(pth->annihil_coef_lya);
  free(pth->annihil_coef_ionH);
  free(pth->annihil_coef_ionHe);
  free(pth->annihil_coef_lowE);

  free(pth->annihil_coef_dd_heat);
  free(pth->annihil_coef_dd_lya);
  free(pth->annihil_coef_dd_ionH);
  free(pth->annihil_coef_dd_ionHe);
  free(pth->annihil_coef_dd_lowE);

  return _SUCCESS_;

}


/**
 * Read and interpolate f_eff from external file
 *
 * @param ppr   Input: pointer to precision structure
 * @param pba   Input: pointer to background structure
 * @param pth   Input/Output: pointer to initialized thermodynamics structure
 * @return the error status
 */
int heating_annihilation_f_eff_init(struct precision * ppr,
                                           struct background * pba,
                                           struct thermo * pth) {

  FILE * fA;
  char line[_LINE_LENGTH_MAX_];
  char * left;

  int num_lines=0;
  int array_line=0;

  /* the following file is assumed to contain (apart from comments and blank lines):
     - One number (num_lines) = number of lines of the file
     - One column (z , f(z)) where f(z) represents the "effective" fraction of energy deposited into the medium 
       at redshift z, in presence of halo formation. */
  class_open(fA,ppr->energy_injec_f_eff_file, "r",pth->error_message);

  while (fgets(line,_LINE_LENGTH_MAX_-1,fA) != NULL) {
    /* eliminate blank spaces at beginning of line */
    left=line;
    while (left[0]==' ') {
      left++;
    }

    /* check that the line is neither blank nor a comment. In ASCII, left[0]>39 means that first non-blank charachter might
       be the beginning of some data (it is not a newline, a #, a %, etc.) */
    if (left[0] > 39) {

      /* if the line contains data, we must interprete it. If num_lines == 0 , the current line must contain
         its value. Otherwise, it must contain (xe , chi_heat, chi_Lya, chi_H, chi_He, chi_lowE). */
      if (num_lines == 0) {

        /* read num_lines, infer size of arrays and allocate them */
        class_test(sscanf(line,"%d",&num_lines) != 1,
                   pth->error_message,
                   "could not read value of parameters num_lines in file %s\n",ppr->energy_injec_f_eff_file);
        class_alloc(pth->annihil_z,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_f_eff,num_lines*sizeof(double),pth->error_message);
        class_alloc(pth->annihil_dd_f_eff,num_lines*sizeof(double),pth->error_message);

        pth->annihil_f_eff_num_lines = num_lines;

        array_line=0;

      }
      else {

        /* read coefficients */
        class_test(sscanf(line,"%lg %lg",
                          &(pth->annihil_z[array_line]),
                          &(pth->annihil_f_eff[array_line]))!= 2,
                   pth->error_message,
                   "could not read value of parameters coefficients in file %s\n",ppr->energy_injec_f_eff_file);
        array_line ++;
      }
    }
  }

  fclose(fA);

  /* spline in one dimension */
  class_call(array_spline_table_lines(pth->annihil_z,
                                      num_lines,
                                      pth->annihil_f_eff,
                                      1,
                                      pth->annihil_dd_f_eff,
                                      _SPLINE_NATURAL_,
                                      pth->error_message),
             pth->error_message,
             pth->error_message);

  return _SUCCESS_;

}


/**
 * (TODO)
 *
 * @param ppr      Input: pointer to precision structure
 * @param pba      Input: pointer to background structure
 * @param pth      Input/Output: pointer to initialized thermodynamics structure
 * @param z        Input: redshift
 * @return the error status
 */
int heating_annihilation_f_eff_interpolate(struct precision * ppr,
                                                  struct background * pba,
                                                  struct thermo * pth,
                                                  double z) {

  int last_index;
  class_call(array_interpolate_spline(pth->annihil_z,
                                      pth->annihil_f_eff_num_lines,
                                      pth->annihil_f_eff,
                                      pth->annihil_dd_f_eff,
                                      1,
                                      z,
                                      &last_index,
                                      &(pth->f_eff),
                                      1,
                                      pth->error_message),
             pth->error_message,
             pth->error_message);

  return _SUCCESS_;

}


/**
 * Free all memory space allocated by heating_annihilation_f_eff_interpolate().
 *
 * @param pth Input/Output: pointer to thermodynamics structure
 * @return the error status
 */
int heating_annihilation_f_eff_free(struct thermo * pth) {

  free(pth->annihil_z);
  free(pth->annihil_f_eff);
  free(pth->annihil_dd_f_eff);

  return _SUCCESS_;

}



/**
 * In case of non-minimal cosmology, this function determines time evolution of the primordial black hole (PBH) mass.
 *
 * @param ppr            Input: pointer to precision structure
 * @param pba            Input: pointer to background structure
 * @param pth            Input: pointer to thermodynamics structure
 * @param z              Input: redshift
 * @param energy_rate    Output: energy density injection rate
 * @param error_message  Output: error message
 * @return the error status
 */
int PBH_evaporating_mass_time_evolution(struct precision * ppr,
                                        struct background * pba,
                                        struct thermo * pth,
                                        ErrorMsg error_message){
  double loop_z, loop_tau, current_mass, time_now, time_prev, dt, dz, f;
  double QCD_activation, current_pbh_temperature;
  double * pvecback_loop;
  int  i_step, last_index_back_loop;

  time_prev = 0.;
  pth->PBH_z_evaporation = 0;
  pth->PBH_table_size = ppr->recfast_Nz0;
  dz = ppr->recfast_z_initial/(pth->PBH_table_size);
  loop_z = ppr->recfast_z_initial-dz;
  current_mass = pth->PBH_evaporating_mass;

  class_alloc(pvecback_loop,pba->bg_size*sizeof(double),pba->error_message);
  class_alloc(pth->PBH_table_z,pth->PBH_table_size*sizeof(double),error_message);
  class_alloc(pth->PBH_table_mass,pth->PBH_table_size*sizeof(double),error_message);
  class_alloc(pth->PBH_table_mass_dd,pth->PBH_table_size*sizeof(double),error_message);
  class_alloc(pth->PBH_table_F,pth->PBH_table_size*sizeof(double),error_message);
  class_alloc(pth->PBH_table_F_dd,pth->PBH_table_size*sizeof(double),error_message);

  /* For the parametrization of F(M) we follow PRD44 (1991) 376 with the additional modification that we dress the "free 
     QCD-particles" (gluons and quarks) with an sigmoid-activation function (in log10-space: Mean at 0.3 GeV and a with 
     of 0.1*"order of magnitude") and the hadrons with 1 - activation to take the QCD-phase transition into account
     and to be in agreement with PRD41 (1990) 3052, where the Ansatz is taken that a black hole emmits those particles which appear
     elementary at the given energy. The order of the particles in the following definition of f: photon, neutrino, electron, muon, 
     tau, up, down, charm, strange, top, bottom, W, Z, gluon, Higgs, neutral Pion and charged pion  */
  for(i_step = 0; i_step < pth->PBH_table_size; i_step++) {
    current_pbh_temperature = 1.06e13/current_mass;
    QCD_activation = 1./(1.-exp(-(log(current_pbh_temperature)-log(0.3))/(log(10.)*0.1)));
    f = 2.*0.060
        +6.*0.147
        +4.*0.142*exp(-(current_mass*5.11e-4)/(4.53*1.06e13))
        +4.*0.142*exp(-(current_mass*0.1037)/(4.53*1.06e13))
        +4.*0.142*exp(-(current_mass*1.777)/(4.53*1.06e13))
        +12.*0.142*exp(-(current_mass*2.2e-3)/(4.53*1.06e13))*QCD_activation
        +12.*0.142*exp(-(current_mass*4.7e-3)/(4.53*1.06e13))*QCD_activation
        +12.*0.142*exp(-(current_mass*1.82)/(4.53*1.06e13))*QCD_activation
        +12.*0.142*exp(-(current_mass*9.6e-2)/(4.53*1.06e13))*QCD_activation
        +12.*0.142*exp(-(current_mass*173.1)/(4.53*1.06e13))*QCD_activation
        +12.*0.142*exp(-(current_mass*4.18)/(4.53*1.06e13))*QCD_activation
        +6.*0.060*exp(-(current_mass*80.39)/(6.04*1.06e13))
        +3.*0.060*exp(-(current_mass*91.19)/(6.04*1.06e13))
        +16.*0.060*exp(-(current_mass*6e-1)/(6.04*1.06e13))*QCD_activation
        +1.*0.267*exp(-(current_mass*125.06)/(2.66*1.06e13))
        +1.*0.267*exp(-(current_mass*1.350e-1)/(2.66*1.06e13))*(1-QCD_activation)
        +2.*0.267*exp(-(current_mass*1.396e-1)/(2.66*1.06e13))*(1-QCD_activation);

    class_call(background_tau_of_z(pba,
                                   loop_z,
                                   &loop_tau),
               pba->error_message,
               ppr->error_message);
    class_call(background_at_tau(pba,
                                 loop_tau,
                                 pba->long_info,
                                 pba->inter_normal,
                                 &last_index_back_loop,
                                 pvecback_loop),
               pba->error_message,
               ppr->error_message);
    time_now = pvecback_loop[pba->index_bg_time]/(_c_/_Mpc_over_m_);
    dt = time_now-time_prev;
    time_prev = time_now;

    if (i_step > 0) {
      if (current_mass > 0.5*pth->PBH_evaporating_mass) {
        current_mass = current_mass-5.34e-5*f*pow(current_mass/1e10,-2)*1e10*dt;
      }
      else {
        if(pth->PBH_z_evaporation == 0) pth->PBH_z_evaporation = loop_z;
        current_mass = 0.;
        f = 0.;
      }
    }

    pth->PBH_table_z[i_step] = loop_z;
    pth->PBH_table_mass[i_step] = current_mass;
    pth->PBH_table_F[i_step] = f;
    loop_z = MAX(0,loop_z-dz);

  }

  free(pvecback_loop);

  class_call(array_spline_table_lines(pth->PBH_table_z,
                                      pth->PBH_table_size,
                                      pth->PBH_table_mass,
                                      1,
                                      pth->PBH_table_mass_dd,
                                      _SPLINE_NATURAL_,
                                      error_message),
             pth->error_message,
             pth->error_message);
  class_call(array_spline_table_lines(pth->PBH_table_z,
                                      pth->PBH_table_size,
                                      pth->PBH_table_F,
                                      1,
                                      pth->PBH_table_F_dd,
                                      _SPLINE_NATURAL_,
                                      error_message),
             error_message,
             error_message);

}

/**
 * In case of non-minimal cosmology, this function determines the energy rate injected in the IGM at a given redshift z (= on-the-spot
 * annihilation) by evaporating primordial black holes.
 *
 * @param ppr            Input: pointer to precision structure
 * @param pba            Input: pointer to background structure
 * @param pth            Input: pointer to thermodynamics structure
 * @param z              Input: redshift
 * @param energy_rate    Output: energy density injection rate
 * @param error_message  Output: error message
 * @return the error status
 */
int heating_evaporating_pbh_energy_injection(struct precision * ppr,
                                                    struct background * pba,
                                                    struct thermo * pth,
                                                    double z,
                                                    double * energy_rate,
                                                    ErrorMsg error_message){

  double rho_cdm_today;
  //double tau;
  int last_index_back;
  double f, f_neutrinos, em_branching, pbh_mass;
  double dMdt;

  /* Calculate the PBH-mass evolution at first call of the function */
  if ((pth->PBH_table_is_initialized) == _FALSE_) {
    pth->PBH_table_is_initialized = _TRUE_;
    PBH_evaporating_mass_time_evolution(ppr,pba,pth,error_message);
  }

  rho_cdm_today = pow(pba->H0*_c_/_Mpc_over_m_,2)*3/8./_PI_/_G_*(pba->Omega0_cdm)*_c_*_c_;         // [J/m^3]

  class_test(pth->PBH_table_is_initialized == _FALSE_, error_message, "The PBH table is not initialized");
  class_call(array_interpolate_spline(pth->PBH_table_z,
				      pth->PBH_table_size,
				      pth->PBH_table_mass,
				      pth->PBH_table_mass_dd,
				      1,
				      z,
				      &last_index_back,
				      &(pbh_mass),
				      1,
				      error_message),
	     error_message,
	     error_message);
  class_call(array_interpolate_spline(pth->PBH_table_z,
				      pth->PBH_table_size,
				      pth->PBH_table_F,
				      pth->PBH_table_F_dd,
				      1,
				      z,
				      &last_index_back,
				      &f,
				      1,
				      error_message),
	     error_message,
	     error_message);

  f_neutrinos = 6*0.147;
  em_branching = 1.; // Currently incoporated in the computation of the f(z) functions.
  if(pbh_mass <= 0.0001*pth->PBH_evaporating_mass || f <= 0 || isnan(pbh_mass)==1 || isnan(f)==1 || z < pth->PBH_z_evaporation){
    pbh_mass = 0;
    dMdt = 0;
    f = 0.;
  }
  else {
    dMdt=5.34e-5*f*pow(pbh_mass/1e10,-2)*1e10;
  }

  *energy_rate = rho_cdm_today*pow((1+z),3)*pth->PBH_fraction/pth->PBH_evaporating_mass*em_branching*(dMdt);

  if(isnan(*energy_rate)==1 || *energy_rate < 0){
    *energy_rate=0.;
  }

}


/**
 * In case of non-minimal cosmology, this function determines the energy rate injected in the IGM at a given redshift z (= on-the-spot
 * annihilation) by accetion of matter into primordial black holes.
 *
 * @param ppr            Input: pointer to precision structure
 * @param pba            Input: pointer to background structure
 * @param pth            Input: pointer to thermodynamics structure
 * @param z              Input: redshift
 * @param energy_rate    Output: energy density injection rate
 * @param error_message  Output: error message
 * @return the error status
 */
int heating_accreting_pbh_energy_injection(struct precision * ppr,
                                                  struct background * pba,
                                                  struct thermo * pth,
                                                  double z,
                                                  double * energy_rate,
                                                  ErrorMsg error_message){

  double rho_cdm_today;
  double tau;
  int last_index_back;
  double * pvecback;
  //Parameters related to PBH
  double c_s, v_eff,v_eff_2,v_l, r_B,x_e,beta,beta_eff,beta_hat,x_cr,lambda,n_gas,M_b_dot,M_sun,M_ed_dot,epsilon,L_acc,Integrale,Normalization;
  double m_H, m_dot, m_dot_2, L_acc_2,L_ed,l,l2,M_crit;
  double rho, m_p = 938, m_e = 0.511, T_infinity = 0, rho_infinity = 0, x_e_infinity = 0, P_infinity = 0, rho_cmb = 0, t_B = 0, v_B = 0;
  double lambda_1,lambda_2,lambda_ad,lambda_iso,gamma_cooling,beta_compton_drag, T_s, T_ion, Y_s, J,tau_cooling;
  double Value_min, Value_med, Value_max, a=0, epsilon_0=0.1;
  rho_cdm_today = pow(pba->H0*_c_/_Mpc_over_m_,2)*3/8./_PI_/_G_*(pba->Omega0_cdm)*_c_*_c_;         // [J/m^3]

  class_alloc(pvecback,pba->bg_size*sizeof(double),pba->error_message);
  class_call(background_tau_of_z(pba,
                                 z,
                                 &tau),
             pba->error_message,
             pth->error_message);
  class_call(background_at_tau(pba,
                               tau,
                               pba->long_info,
                               pba->inter_normal,
                               &last_index_back,
                               pvecback),
             pba->error_message,
             pth->error_message);

  c_s = 5.7e3*pow(pth->Tm_tmp/2730,0.5);          // [m]
  M_sun = 2e30;                                   // [kg]
  n_gas = 200*1e6*pow((1+z)/1000,3);              // [1/m^3]
  m_H= 1.67e-27;                                  // [kg]
  x_e = pth->xe_tmp;
  T_infinity = pth->Tm_tmp*_eV_over_Kelvin_*1e-6; // [MeV]

  /** Disk accretion from Poulin et al. 1707.04206 */

  if(pth->PBH_accretion_recipe == disk_accretion){
    L_ed = 4*_PI_*_G_*pth->PBH_accreting_mass*M_sun*m_p*1e6/_eV_over_joules_/(_sigma_*_c_);
    M_ed_dot= 10*L_ed/(_c_*_c_);
    M_crit = 0.01*M_ed_dot;
    v_B = sqrt((1+x_e)*T_infinity/m_p)*_c_;

    if(pth->PBH_relative_velocities < 0.){
      v_l = 30*MIN(1,z/1000)*1e3; // [m/s]
      if(v_B < v_l) v_eff = sqrt(v_B*v_l);
      else v_eff = v_B;
    }
    else{
      v_l = pth->PBH_relative_velocities*1e3; // [m/s]
      v_eff = pow(v_l*v_l+v_B*v_B,0.5);
    }

    lambda = pth->PBH_accretion_eigenvalue;
    rho = pvecback[pba->index_bg_rho_b]/pow(_Mpc_over_m_,2)*3/8./_PI_/_G_*_c_*_c_; // [kg/m^3]
    M_b_dot = 4*_PI_*lambda*pow(_G_*pth->PBH_accreting_mass*M_sun,2)*rho*pow(v_eff,-3.);

    if(pth->PBH_ADAF_delta == 1e-3){
      Value_min = 7.6e-5;
      Value_med = 4.5e-3;
      Value_max = 7.1e-3;
      if(M_b_dot/M_ed_dot <= Value_min){
        epsilon_0 = 0.065;
        a = 0.71;
      }
      else if(Value_min < M_b_dot/M_ed_dot && M_b_dot/M_ed_dot  <= Value_med){
        epsilon_0 = 0.020;
        a = 0.47;
      }
      else if(Value_med < M_b_dot/M_ed_dot && M_b_dot/M_ed_dot <= Value_max){
        epsilon_0 = 0.26;
        a = 3.67;
      }
      else{
        epsilon_0 = 0.1;
        a = 0.;
      }
      epsilon = epsilon_0 * pow(M_b_dot / M_crit,a);
    }
    else if(pth->PBH_ADAF_delta == 0.1){
      Value_min = 9.4e-5;
      Value_med = 5e-3;
      Value_max = 6.6e-3;
      if(M_b_dot/M_ed_dot <= Value_min){
        epsilon_0 = 0.12;
        a = 0.59;
      }
      else if(Value_min < M_b_dot/M_ed_dot && M_b_dot/M_ed_dot  <= Value_med){
        epsilon_0 = 0.026;
        a = 0.27;
      }
      else if(Value_med < M_b_dot/M_ed_dot && M_b_dot/M_ed_dot <= Value_max){
        epsilon_0 = 0.50;
        a = 4.53;
      }
      else{
        epsilon_0 = 0.1;
        a = 0.;
      }
      epsilon = epsilon_0 * pow(M_b_dot / M_crit,a);
    }
    else if (pth->PBH_ADAF_delta == 0.5){
      Value_min = 2.9e-5;
      Value_med = 3.3e-3;
      Value_max = 5.3e-3;
      if(M_b_dot/M_ed_dot <= Value_min){
        epsilon_0 = 1.58;
        a = 0.65;
      }
      else if(Value_min < M_b_dot/M_ed_dot && M_b_dot/M_ed_dot  <= Value_med){
        epsilon_0 = 0.055;
        a = 0.076;
      }
      else if(Value_med < M_b_dot/M_ed_dot && M_b_dot/M_ed_dot <= Value_max){
        epsilon_0 = 0.17;
        a = 1.12;
      }
      else{
        epsilon_0 = 0.1;
        a = 0.;
      }
      epsilon = epsilon_0 * pow(M_b_dot / M_crit,a);
    }

    L_acc = epsilon*M_b_dot*_c_*_c_;

  }

  /** Spherical accretion from Ali-Haimoud et al. 1612.05644 */

  else if(pth->PBH_accretion_recipe == spherical_accretion){
    rho_cmb = pvecback[pba->index_bg_rho_g]/pow(_Mpc_over_m_,2)*3/8./_PI_/_G_*pow(_c_,4.)* 6.241509e12; // [MeV/m^3]
    // x_e_infinity = 1; // change to 1 for the strong-feedback case
    x_e_infinity = x_e; // change to x_e for the no-feedback case
    v_B = sqrt((1+x_e_infinity)*T_infinity/m_p)*_c_; //sound speed.
    if(pth->PBH_relative_velocities < 0.){
            v_l = 30*MIN(1,z/1000)*1e3; // [m/s]
            if(v_B < v_l) v_eff = sqrt(v_B*v_l);
            else v_eff = v_B;
    }
    else{
            v_l = pth->PBH_relative_velocities*1e3; // [m/s]
            v_eff = pow(v_l*v_l+v_B*v_B,0.5);
    }
    r_B = _G_*pth->PBH_accreting_mass*M_sun*pow(v_eff,-2); // [m]
    t_B = _G_*pth->PBH_accreting_mass*M_sun/pow(v_eff,3); // [s]
    beta_compton_drag = 4./3*x_e_infinity*_sigma_*rho_cmb*t_B/(m_p)*_c_;
    gamma_cooling = 2*m_p/(m_e*(1+x_e_infinity))*beta_compton_drag;
    lambda_iso = 0.25*exp(1.5);
    lambda_ad = 0.25*pow(3./5,1.5);
    lambda_1 = lambda_ad+(lambda_iso-lambda_ad)*pow(gamma_cooling*gamma_cooling/(88+gamma_cooling*gamma_cooling),0.22);
    lambda_2 = exp(4.5/(3+pow(beta_compton_drag,0.75)))*1/(pow(pow(1+beta_compton_drag,0.5)+1,2));
    lambda = lambda_1*lambda_2/lambda_iso;
    rho = pvecback[pba->index_bg_rho_b]/pow(_Mpc_over_m_,2)*3/8./_PI_/_G_*_c_*_c_; // [kg/m^3]
    M_b_dot = 4*_PI_*lambda*rho*r_B*r_B*v_eff; // [kg/s]
    T_ion = 1.5e4*_eV_over_Kelvin_;
    tau_cooling = 1.5/(5+pow(gamma_cooling,2./3));
    Y_s = pow((1+x_e_infinity)/2,2./3*13.6/T_ion)*tau_cooling/4*pow(1-5./2*tau_cooling,1./3)*m_p/m_e;
    T_s = m_e * Y_s*pow(1+Y_s/0.27,-1./3); // [MeV]
    if(T_s/m_e > 1){
      J = 27/(2*_PI_)*(log(2*T_s/(m_e)*exp(-0.577)+0.08)+4./3);
    }
    else{
      J = 4/_PI_*sqrt(2/_PI_)*pow(T_s/m_e,-0.5)*(1+5.5*pow(T_s/m_e,1.25));
    }
    L_ed = 4*_PI_*_G_*pth->PBH_accreting_mass*M_sun*m_p*1e6/_eV_over_joules_/(_sigma_*_c_);
    L_acc = 1./137*T_s/(m_p)*J*pow(M_b_dot*_c_*_c_,2)/L_ed;
  }

  *energy_rate =  (rho_cdm_today/(pth->PBH_accreting_mass*M_sun*_c_*_c_))*pow(1+z,3)*L_acc*pth->PBH_fraction;

  free(pvecback);

}


/**
 * In case of non-minimal cosmology, this function determines the total energy rate injected in the IGM at a given redshift z (= on-the-spot
 * annihilation).
 *
 * @param ppr            Input: pointer to precision structure
 * @param pba            Input: pointer to background structure
 * @param pth            Input: pointer to thermodynamics structure
 * @param z              Input: redshift
 * @param energy_rate    Output: energy density injection rate
 * @param error_message  Output: error message
 * @return the error status
 */
int heating_onthespot_energy_injection(
                                              struct precision * ppr,
                                              struct background * pba,
                                              struct thermo * pth,
                                              double z,
                                              double * energy_rate,
                                              ErrorMsg error_message
                                              ) {

  if(pth->annihilation > 0){
    heating_DM_annihilation_energy_injection(ppr,pba,pth,z,energy_rate,error_message);
  }
  if(pth->decay_fraction > 0.){
    heating_DM_decay_energy_injection(ppr,pba,pth,z,energy_rate,error_message);
  }
  if(pth->PBH_accreting_mass > 0.){
    heating_accreting_pbh_energy_injection(ppr,pba,pth,z,energy_rate,error_message);
  }
  if(pth->PBH_evaporating_mass > 0.){
    heating_evaporating_pbh_energy_injection(ppr,pba,pth,z,energy_rate,error_message);
  }

  /* energy density rate in J/(m^3 s) */
  return _SUCCESS_;

}


/**
 * In case of non-minimal cosmology, this function determines the effective energy rate absorbed by the IGM at a given redshift
 * (beyond the on-the-spot annihilation). This energy injection may come e.g. from dark matter annihilation or decay.
 *
 * @param ppr             Input: pointer to precision structure
 * @param pba             Input: pointer to background structure
 * @param pth             Input: pointer to thermodynamics structure
 * @param z               Input: redshift
 * @param energy_rate     Output: energy density injection rate
 * @param error_message   Output: error message
 * @return the error status
 */
int heating_energy_injection(
                                    struct precision * ppr,
                                    struct background * pba,
                                    struct thermo * pth,
                                    double z,
                                    double * energy_rate,
                                    ErrorMsg error_message
                                    ) {

  double zp,dz;
  double integrand,first_integrand;
  double factor,result;
  double nH0;
  double onthespot;
  double exponent_z,exponent_zp;

  if (pth->annihilation > 0 || pth->decay_fraction > 0 || pth->PBH_accreting_mass > 0 || pth->PBH_evaporating_mass > 0 ){

    if (pth->has_on_the_spot == _FALSE_) {

      if(pth->energy_deposition_function == Analytical_approximation){ 
        nH0 = 3.*pba->H0*pba->H0*pba->Omega0_b/(8.*_PI_*_G_*_m_H_)*(1.-pth->YHe); // number of hydrogen nuclei today in m^-3

        /* Value from Poulin et al. 1508.01370 */
        /* 
        factor = c sigma_T n_H(0) / (H(0) \sqrt(Omega_m)) (dimensionless)
        factor = _sigma_ * nH0 / pba->H0 * _Mpc_over_m_ / sqrt(pba->Omega0_b+pba->Omega0_cdm);
        exponent_z = 8;
        exponent_zp = 7.5;
        */

        /* Value from Ali-Haimoud & Kamionkowski 1612.05644 */
        factor = 0.1*_sigma_ * nH0 / pba->H0 * _Mpc_over_m_ / sqrt(pba->Omega0_b+pba->Omega0_cdm);
        exponent_z = 7;
        exponent_zp = 6.5;

        /* integral over z'(=zp) with step dz */
        dz=1.;

        /* first point in trapezoidal integral */
        zp = z;
        class_call(heating_onthespot_energy_injection(ppr,pba,pth,zp,&onthespot,error_message),
                   error_message,
                   error_message);
        first_integrand = factor*pow(1+z,exponent_z)/pow(1+zp,exponent_zp)*exp(2./3.*factor*(pow(1+z,1.5)-pow(1+zp,1.5)))*onthespot; 
        // beware: versions before 2.4.3, there were rwrong exponents: 6 and 5.5 instead of 7 and 7.5
        result = 0.5*dz*first_integrand;

        /* other points in trapezoidal integral */
        do{
          zp += dz;
          class_call(heating_onthespot_energy_injection(ppr,pba,pth,zp,&onthespot,error_message),
                     error_message,
                     error_message);
          integrand = factor*pow(1+z,exponent_z)/pow(1+zp,exponent_zp)*exp(2./3.*factor*(pow(1+z,1.5)-pow(1+zp,1.5)))*onthespot; 
          // beware: versions before 2.4.3, there were rwrong exponents: 6 and 5.5 instead of 7 and 7.5
          result += dz*integrand;

        } while (integrand/first_integrand > 0.02);
        if(result < 1e-100) result=0.;
      }

      else if(pth->energy_deposition_function == function_from_file){
        if(pth->energy_repart_coefficient!=no_factorization){
          class_call(heating_annihilation_f_eff_interpolate(ppr,pba,pth,z),
                     pth->error_message,
                     pth->error_message);
          pth->f_eff=MAX(pth->f_eff,0.);
        }
        else{
          pth->f_eff=1.;
        }

        class_call(heating_onthespot_energy_injection(ppr,pba,pth,z,&result,error_message),
                   error_message,
                   error_message);
        result =  result*pth->f_eff;
      }

      else if(pth->energy_deposition_function == DarkAges){
        class_call(heating_onthespot_energy_injection(ppr,pba,pth,z,&result,error_message),
                   error_message,
                   error_message);
      }

      // /* uncomment these lines if you also want to compute the on-the-spot for comparison */
      /*
      class_call(heating_onthespot_energy_injection(ppr,pba,pth,z,&onthespot,error_message),
                 error_message,
                 error_message);
      fprintf(stdout,"%e  %e  %e  %e\n", 1.+z,
                                         result/pow(1.+z,6),
                                         onthespot/pow(1.+z,6),result/onthespot);
      */
    }
    else {
      class_call(heating_onthespot_energy_injection(ppr,pba,pth,z,&result,error_message),
                 error_message,
                 error_message);
      if(pth->f_eff>0){
        result *= pth->f_eff; // If pth->f_eff is defined, here we multiply by f_eff.
      }
    }

    *energy_rate = result; // in J/m^3/s
  }
  else {

    *energy_rate = 0.;
  }

  return _SUCCESS_;

}



/**
 * In case of non-minimal cosmology, this function determines the energy rate injected in the IGM at a given redshift z
 * (= on-the-spot annihilation). This energy injection may come e.g. from dark matter annihilation or decay.
 *
 * @param ppr            Input: pointer to precision structure
 * @param pba            Input: pointer to background structure
 * @param ptw            Input: pointer to thermo_workspace structure
 * @param z              Input: redshift
 * @param energy_rate    Output: energy density injection rate
 * @param error_message  Output: error message
 * @return the error status
 */
int thermodynamics_solve_onthespot_energy_injection(struct precision * ppr,
                                                    struct background * pba,
                                                    struct thermo_workspace * ptw,
                                                    double z,
                                                    double * energy_rate,
                                                    ErrorMsg error_message){

  /** Summary: */

  /** Define local variables */
  struct thermo_heating_parameters* pthp = ptw->pthp;

  double annihilation_at_z;
  double rho_cdm_today;
  double u_min;
  double erfc;

  /* redshift-dependent annihilation parameter */

  if (z>pthp->annihilation_zmax) {

    annihilation_at_z = pthp->annihilation*
      exp(-pthp->annihilation_variation*pow(log((pthp->annihilation_z+1.)/(pthp->annihilation_zmax+1.)),2));
  }
  else if (z>pthp->annihilation_zmin) {

    annihilation_at_z = pthp->annihilation*
      exp(pthp->annihilation_variation*(-pow(log((pthp->annihilation_z+1.)/(pthp->annihilation_zmax+1.)),2)
                                         +pow(log((z+1.)/(pthp->annihilation_zmax+1.)),2)));
  }
  else {

    annihilation_at_z = pthp->annihilation*
      exp(pthp->annihilation_variation*(-pow(log((pthp->annihilation_z+1.)/(pthp->annihilation_zmax+1.)),2)
                                         +pow(log((pthp->annihilation_zmin+1.)/(pthp->annihilation_zmax+1.)),2)));
  }

  rho_cdm_today = pow(ptw->SIunit_H0,2)*3/8./_PI_/_G_*pba->Omega0_cdm*_c_*_c_; // in J/m^3

  u_min = (1+z)/(1+pthp->annihilation_z_halo);

  erfc = pow(1.+0.278393*u_min+0.230389*u_min*u_min+0.000972*u_min*u_min*u_min+0.078108*u_min*u_min*u_min*u_min,-4);

  *energy_rate = pow(rho_cdm_today,2)/_c_/_c_*pow((1+z),3)*
    (pow((1.+z),3)*annihilation_at_z+pthp->annihilation_f_halo*erfc)
    +rho_cdm_today*pow((1+z),3)*pthp->decay;  // in J/m^3/s

  return _SUCCESS_;

}


/**
 * In case of non-minimal cosmology, this function determines the effective energy rate absorbed by the IGM at a given redshift
 * (beyond the on-the-spot annihilation). This energy injection may come e.g. from dark matter annihilation or decay.
 *
 * @param ppr            Input: pointer to precision structure
 * @param pba            Input: pointer to background structure
 * @param ptw            Input: pointer to thermo_workspace structure
 * @param z              Input: redshift
 * @param energy_rate    Output: energy density injection rate
 * @param error_message  Output: error message
 * @return the error status
 */
int thermodynamics_solve_energy_injection(struct precision * ppr,
                                          struct background * pba,
                                          struct thermo_workspace * ptw,
                                          double z,
                                          double * energy_rate,
                                          ErrorMsg error_message){

  /** Summary: */

  /** Define local variables */
  struct thermo_heating_parameters* pthp = ptw->pthp;

  double zp,dz;
  double integrand,first_integrand;
  double factor,result;
  double nH0 = ptw->SIunit_nH0;
  double onthespot;

  if (pthp->annihilation > 0) {

    if (pthp->has_on_the_spot == _FALSE_) {

      /* factor = c sigma_T n_H(0) / (H(0) \sqrt(Omega_m)) (dimensionless) */ //TODO :: generalize !!
      factor = _sigma_ * nH0 / pba->H0 * _Mpc_over_m_ / sqrt(pba->Omega0_b+pba->Omega0_cdm);

      /* integral over z'(=zp) with step dz */
      dz=1.;

      /* first point in trapezoidal integral */
      zp = z;
      class_call(thermodynamics_solve_onthespot_energy_injection(ppr,pba,ptw,zp,&onthespot,error_message),
                 error_message,
                 error_message);
      first_integrand = factor*pow(1+z,8)/pow(1+zp,7.5)*exp(2./3.*factor*(pow(1+z,1.5)-pow(1+zp,1.5)))*onthespot; // beware: versions before 2.4.3, there were wrong exponents: 6 and 5.5 instead of 8 and 7.5
      result = 0.5*dz*first_integrand;

      /* other points in trapezoidal integral */
      do {

        zp += dz;
        class_call(thermodynamics_solve_onthespot_energy_injection(ppr,pba,ptw,zp,&onthespot,error_message),
                   error_message,
                   error_message);
        integrand = factor*pow(1+z,8)/pow(1+zp,7.5)*exp(2./3.*factor*(pow(1+z,1.5)-pow(1+zp,1.5)))*onthespot; // beware: versions before 2.4.3, there were wrong exponents: 6 and 5.5 instead of 8 and 7.5
        result += dz*integrand;

      } while (integrand/first_integrand > 0.02);

      /* uncomment these lines if you also want to compute the on-the-spot for comparison */
      class_call(thermodynamics_solve_onthespot_energy_injection(ppr,pba,ptw,z,&onthespot,error_message),
                 error_message,
                 error_message);

    }
    else {
      class_call(thermodynamics_solve_onthespot_energy_injection(ppr,pba,ptw,z,&result,error_message),
                 error_message,
                 error_message);
    }

    /* these test lines print the energy rate rescaled by (1+z)^6 in J/m^3/s, with or without the on-the-spot approximation */
    /*
      fprintf(stdout,"%e  %e  %e \n", 1.+z,
                                      result/pow(1.+z,6),
                                      onthespot/pow(1.+z,6));
    */

    /* effective energy density rate in J/m^3/s  */
    *energy_rate = result;

  }
  else {
    *energy_rate = 0.;
  }

  return _SUCCESS_;

}

#endif
