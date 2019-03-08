#include "wrap_recfast.h"

int thermodynamics_recfast_init(struct precision* ppr,
                                struct background* pba,
                                struct thermo* pth,
                                double fHe,
                                struct thermorecfast * pre) {

  /** Define local quantities */
  double Lalpha,Lalpha_He,DeltaB,DeltaB_He;

  /** - read a few precision/cosmological parameters */
  pre->AGauss1 = ppr->recfast_AGauss1;
  pre->AGauss2 = ppr->recfast_AGauss2;
  pre->zGauss1 = ppr->recfast_zGauss1;
  pre->zGauss2 = ppr->recfast_zGauss2;
  pre->wGauss1 = ppr->recfast_wGauss1;
  pre->wGauss2 = ppr->recfast_wGauss2;
  pre->Hswitch = ppr->recfast_Hswitch;
  pre->Heswitch = ppr->recfast_Heswitch;
  //pre->H_frac = ppr->recfast_H_frac;
  pre->x_H0_trigger2 = ppr->recfast_x_H0_trigger2;
  pre->x_He0_trigger2 = ppr->recfast_x_He0_trigger2;
  pre->fudge_H = ppr->recfast_fudge_H;
  pre->fudge_He = ppr->recfast_fudge_He;
  pre->x_H_limit_KHe = 0.9999999; /* threshold changed by Antony Lewis in 2008 to get smoother Helium */
  pre->x_H_limit_CfHe_t = 0.99999;
  pre->max_exp_boltz = 680.;
  pre->fHe = fHe;

  /** - Adjust fudging factors if needed */
  if (pre->Hswitch == _TRUE_){
    pre->fudge_H += ppr->recfast_delta_fudge_H;
  }


  /** - Assign quantities that have to be calculated first */
  Lalpha = 1./_L_H_alpha_;
  Lalpha_He = 1./_L_He_2p_;
  DeltaB = _h_P_*_c_*(_L_H_ion_-_L_H_alpha_);
  pre->CDB = DeltaB/_k_B_;
  DeltaB_He = _h_P_*_c_*(_L_He1_ion_-_L_He_2s_);
  pre->CDB_He = DeltaB_He/_k_B_;
  pre->CB1 = _h_P_*_c_*_L_H_ion_/_k_B_;
  pre->CB1_He1 = _h_P_*_c_*_L_He1_ion_/_k_B_;
  pre->CB1_He2 = _h_P_*_c_*_L_He2_ion_/_k_B_;
  pre->CR = 2.*_PI_*(_m_e_/_h_P_)*(_k_B_/_h_P_);
  pre->CK = pow(Lalpha,3)/(8.*_PI_);
  pre->CK_He = pow(Lalpha_He,3)/(8.*_PI_);
  pre->CL = _c_*_h_P_/(_k_B_*Lalpha);
  pre->CL_He = _c_*_h_P_/(_k_B_/_L_He_2s_);
  pre->CT = (8./3.) * (_sigma_/(_m_e_*_c_)) *
    (8.*pow(_PI_,5)*pow(_k_B_,4)/ 15./ pow(_h_P_,3)/pow(_c_,3));
  pre->Bfact = _h_P_*_c_*(_L_He_2p_-_L_He_2s_)/_k_B_;

  /** - Test schemes */
  /* He fudging */
  class_test((ppr->recfast_Heswitch < 0) || (ppr->recfast_Heswitch > 6),
             pth->error_message,
             "RECFAST error: unknown He fudging scheme");
  /* H fudging */
  class_test((ppr->recfast_Hswitch != _TRUE_) && (ppr->recfast_Hswitch != _FALSE_),
             pth->error_message,
             "RECFAST error: unknown H fudging scheme");

  return _SUCCESS_;

}
int thermodynamics_recfast_dx_H_dz(struct thermorecfast * pre, double x_H, double x, double n,
                                   double z, double Hz, double Tmat, double Trad,
                                   double* dxH_dz, double energy_injection) {

  /** Define local variables */
  /* new in recfast 1.4: */
  double Rup,Rdown,K,C;
  double chi_ion_H;

  /** - Get necessary coefficients */
  Rdown=1.e-19*_a_PPB_*pow((Tmat/1.e4),_b_PPB_)/(1.+_c_PPB_*pow((Tmat/1.e4),_d_PPB_));
  //Rup = Rdown * pow((pre->CR*Tmat),1.5)*exp(-pre->CDB/Tmat);
  Rup = 1.e-19*_a_PPB_*pow((Trad/1.e4),_b_PPB_)/(1.+_c_PPB_*pow((Trad/1.e4),_d_PPB_)) * pow((pre->CR*Trad),1.5)*exp(-pre->CDB/Trad);

  K = pre->CK/Hz;

  /* following is from recfast 1.5 */
  /** - Adjust the K constant with double gaussian fit */
  if (pre->Hswitch == _TRUE_ ){
    K *= 1.
      + pre->AGauss1*exp(-pow((log(1.+z)-pre->zGauss1)/pre->wGauss1,2))
      + pre->AGauss2*exp(-pow((log(1.+z)-pre->zGauss2)/pre->wGauss2,2));
  }
  /* end of new recfast 1.5 piece */

  /** - Calculate Peebles' coefficient */
  /* Peebles' coefficient (approximated as one when the Hydrogen
   * ionization fraction is very close to one) */
  if (x_H < pre->x_H0_trigger2) {
    C = (1. + K*_Lambda_*n*(1.-x_H))/(1./pre->fudge_H+K*_Lambda_*n*(1.-x_H)/pre->fudge_H +K*Rup*n*(1.-x_H));
  }
  else {
    C = 1.;
  }

  /* - old approximation from Chen and Kamionkowski: */
  //chi_ion_H = (1.-x)/3.;

  /** - Calculate chi_ion */
  /* coefficient as revised by Slatyer et al. 2013 (in fact it is a fit by Vivian Poulin of columns 1 and 2 in Table V of Slatyer et al. 2013): */
  if (x < 1.){
    chi_ion_H = 0.369202*pow(1.-pow(x,0.463929),1.70237);
  }
  else{
    chi_ion_H = 0.;
  }

  /** - Evolve system by fudged Peebles' equation, use fudged Peebles' coefficient C */
  *dxH_dz = (x*x_H*n*Rdown - Rup*(1.-x_H)*exp(-pre->CL/Tmat)) * C / (Hz*(1.+z));

  /** - Energy injection */
  *dxH_dz += -energy_injection*chi_ion_H/n*(1./_L_H_ion_+(1.-C)/_L_H_alpha_)/(_h_P_*_c_*Hz*(1.+z));

  return _SUCCESS_;
}

int thermodynamics_recfast_dx_He_dz(struct thermorecfast * pre, double x_He, double x, double x_H, double n,
                                    double z, double Hz, double Tmat, double Trad,
                                    double* dxHe_dz, double energy_injection) {

  /** Define local variables */
  double Rdown_trip,Rup_trip,tauHe_s,pHe_s,tauHe_t,pHe_t,CL_PSt;
  double Doppler,gamma_2Ps,gamma_2Pt,pb,qb,AHcon;
  double sq_0,sq_1;
  double K_He,Rup_He,Rdown_He,He_Boltz;
  double CfHe_t=0.;
  double n_He;
  int Heflag;


  /** - Get necessary coefficients */
  sq_0 = sqrt(Tmat/_T_0_);
  sq_1 = sqrt(Tmat/_T_1_);
  Rdown_He = _a_VF_/(sq_0 * pow((1.+sq_0),(1.-_b_VF_)) * pow((1. + sq_1),(1. + _b_VF_)));
  //Rup_He = 4.*Rdown_He*pow((pre->CR*Tmat),1.5)*exp(-pre->CDB_He/Tmat);
  Rup_He = 4.*_a_VF_/(sqrt(Trad/_T_0_) * pow((1.+sqrt(Trad/_T_0_)),(1.-_b_VF_)) * pow((1. + sqrt(Trad/_T_1_)),(1. + _b_VF_)))
      * pow((pre->CR*Trad),1.5)*exp(-pre->CDB_He/Trad);
  n_He = pre->fHe * n;

  /** - The K_He is calculated up to the required accuracy  */
  if ((x_He < 5.e-9) || (x_He > pre->x_He0_trigger2)){
    Heflag = 0;
  }
  else{
    Heflag = pre->Heswitch;
  }
  if (Heflag == 0){
    K_He = pre->CK_He/Hz;
  }
  else {
    tauHe_s = _A2P_s_*pre->CK_He*3.*n_He*(1.-x_He)/Hz;
    pHe_s = (1.-exp(-tauHe_s))/tauHe_s;
    K_He = 1./(_A2P_s_*pHe_s*3.*n_He*(1.-x_He));

    if (((Heflag == 2) || (Heflag >= 5)) && (x_H < pre->x_H_limit_KHe)) {

      Doppler = 2.*_k_B_*Tmat/(_m_H_*_not4_*_c_*_c_);
      Doppler = _c_*_L_He_2p_*sqrt(Doppler);
      gamma_2Ps = 3.*_A2P_s_*pre->fHe*(1.-x_He)*_c_*_c_
        /(sqrt(_PI_)*_sigma_He_2Ps_*8.*_PI_*Doppler*(1.-x_H))
        /pow(_c_*_L_He_2p_,2);
      pb = 0.36;
      qb = pre->fudge_He;
      AHcon = _A2P_s_/(1.+pb*pow(gamma_2Ps,qb));
      K_He=1./((_A2P_s_*pHe_s+AHcon)*3.*n_He*(1.-x_He));
    }

    /* Do we want to also calculate the additional correction of CfHe_t ? */
    if (Heflag >= 3) {
      Rdown_trip = _a_trip_/(sq_0*pow((1.+sq_0),(1.-_b_trip_)) * pow((1.+sq_1),(1.+_b_trip_)));
      //Rup_trip = Rdown_trip*exp(-_h_P_*_c_*_L_He2St_ion_/(_k_B_*Tmat))*pow(pre->CR*Tmat,1.5)*4./3.;
      Rup_trip = _a_trip_/(sqrt(Trad/_T_0_)*pow((1.+sqrt(Trad/_T_0_)),(1.-_b_trip_)) * pow((1.+sqrt(Trad/_T_1_)),(1.+_b_trip_)))
        *exp(-_h_P_*_c_*_L_He2St_ion_/(_k_B_*Tmat))*pow(pre->CR*Tmat,1.5)*4./3.;

      tauHe_t = _A2P_t_*n_He*(1.-x_He)*3./(8.*_PI_*Hz*pow(_L_He_2Pt_,3));
      pHe_t = (1. - exp(-tauHe_t))/tauHe_t;
      CL_PSt = _h_P_*_c_*(_L_He_2Pt_ - _L_He_2St_)/_k_B_;
      if ((Heflag == 3) || (Heflag == 5) || (x_H >= pre->x_H_limit_CfHe_t)) {
        CfHe_t = _A2P_t_*pHe_t*exp(-CL_PSt/Tmat);
        CfHe_t = CfHe_t/(Rup_trip+CfHe_t);
      }
      else {
        Doppler = 2.*_k_B_*Tmat/(_m_H_*_not4_*_c_*_c_);
        Doppler = _c_*_L_He_2Pt_*sqrt(Doppler);
        gamma_2Pt = 3.*_A2P_t_*pre->fHe*(1.-x_He)*_c_*_c_
          /(sqrt(_PI_)*_sigma_He_2Pt_*8.*_PI_*Doppler*(1.-x_H))
          /pow(_c_*_L_He_2Pt_,2);
        pb = 0.66;
        qb = 0.9;
        AHcon = _A2P_t_/(1.+pb*pow(gamma_2Pt,qb))/3.;
        CfHe_t = (_A2P_t_*pHe_t+AHcon)*exp(-CL_PSt/Tmat);
        CfHe_t = CfHe_t/(Rup_trip+CfHe_t);
      }
    }
  }

  /** - Final helium equations, again fudged Peebles' equations are used */
  if (x_He < 1.e-15){
    *dxHe_dz=0.;
  }
  else {

    /* Calculate first the boltzmann factor (limited for numerical reasons) */
    if (pre->Bfact/Tmat < pre->max_exp_boltz){
      He_Boltz=exp(pre->Bfact/Tmat);
    }
    else{
      He_Boltz=exp(pre->max_exp_boltz);
    }

    /* equations modified to take into account energy injection from dark matter */
    //C_He=(1. + K_He*_Lambda_He_*n_He*(1.-x_He)*He_Boltz)/(1. + K_He*(_Lambda_He_+Rup_He)*n_He*(1.-x_He)*He_Boltz);

    /** Final He quations by Peebles with K_He*/
    *dxHe_dz = ((x*x_He*n*Rdown_He - Rup_He*(1.-x_He)*exp(-pre->CL_He/Tmat))
             *(1. + K_He*_Lambda_He_*n_He*(1.-x_He)*He_Boltz))
      /(Hz*(1+z)* (1. + K_He*(_Lambda_He_+Rup_He)*n_He*(1.-x_He)*He_Boltz));
    /* in case of energy injection due to DM, we neglect the contribution to helium ionization ! */

    /* following is from recfast 1.4 (now reordered) */
    /* this correction is not self-consistent when there is energy injection from dark matter, and leads to nan's at small redshift (unimportant when reionization takes over before that redshift) */
    if (Heflag >= 3){
      /** CfHe_t correction */
      *dxHe_dz +=
          (x*x_He*n*Rdown_trip
         - (1.-x_He)*3.*Rup_trip*exp(-_h_P_*_c_*_L_He_2St_/(_k_B_*Tmat)))
        *CfHe_t/(Hz*(1.+z));
    }
    /* end of new recfast 1.4 piece */
  }

  /** - No He Energy injection */
  return _SUCCESS_;
}
