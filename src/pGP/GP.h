/*****************************************************************/
/*    NAME: Stephanie Kemna                                      */
/*    ORGN: Robotic Embedded Systems Lab, CS, USC, CA, USA       */
/*    FILE: GP.h                                                 */
/*    DATE: Mar 29, 2014                                         */
/*                                                               */
/*                                                               */
/*****************************************************************/

#ifndef PGP_HEADER
#define PGP_HEADER

#include "MOOS/libMOOS/MOOSLib.h"

// lib GP
#include "gp.h"

class GP : public CMOOSApp
{
 public:
   GP();
   ~GP() {};

 protected: 
   // Standard MOOSApp functions to overload
   bool OnNewMail(MOOSMSG_LIST &NewMail);
   bool Iterate();
   bool OnConnectToServer();
   bool OnStartUp();

   // Registration, Configuration, Mail handling utils
   void registerVariables();
   bool handleMailGPVarIn(std::string);

 private: 
   // Own functions
   bool handleMailData(double received_data);

   // Configuration variables
   std::string m_input_var;

   // State variables
   double m_lat;
   double m_lon;
   double m_dep;
   bool m_data_added;

   libgp::GaussianProcess m_gp;
};

#endif 
