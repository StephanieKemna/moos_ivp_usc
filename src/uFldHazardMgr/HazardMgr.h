/*****************************************************************/
/*    NAME: Michael Benjamin, Henrik Schmidt, and John Leonard   */
/*    ORGN: Dept of Mechanical Eng / CSAIL, MIT Cambridge MA     */
/*    FILE: HazardMgr.h                                          */
/*    DATE: Oct 26th 2012                                        */
/*                                                               */
/* This program is free software; you can redistribute it and/or */
/* modify it under the terms of the GNU General Public License   */
/* as published by the Free Software Foundation; either version  */
/* 2 of the License, or (at your option) any later version.      */
/*                                                               */
/* This program is distributed in the hope that it will be       */
/* useful, but WITHOUT ANY WARRANTY; without even the implied    */
/* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR       */
/* PURPOSE. See the GNU General Public License for more details. */
/*                                                               */
/* You should have received a copy of the GNU General Public     */
/* License along with this program; if not, write to the Free    */
/* Software Foundation, Inc., 59 Temple Place - Suite 330,       */
/* Boston, MA 02111-1307, USA.                                   */
/*****************************************************************/

#ifndef UFLD_HAZARD_MGR_HEADER
#define UFLD_HAZARD_MGR_HEADER

#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "XYHazardSet.h"
#include <vector>

using namespace std;

class HazardMgr : public AppCastingMOOSApp
{
 public:
   HazardMgr();
   ~HazardMgr() {};

 protected: // Standard MOOSApp functions to overload  
   bool OnNewMail(MOOSMSG_LIST &NewMail);
   bool Iterate();
   bool OnConnectToServer();
   bool OnStartUp();

 protected: // Standard AppCastingMOOSApp function to overload 
   bool buildReport();

 protected: // Registration, Configuration, Mail handling utils
   void registerVariables();
   bool handleMailSensorConfigAck(std::string);
   bool handleMailSensorOptionsSummary(std::string) {return(true);};
   bool handleMailDetectionReport(std::string);
   void handleMailOwnHazardReport(std::string);
   void handleMailOtherHazardReport(std::string);
   void handleMailReportRequest();
   void handleMailOwnNodeReport(std::string sval);
   void buildHazardSet(std::string sval);
   
 protected: 
   void postSensorConfigRequest();
   void postSensorInfoRequest();
   void postHazardSetReport();
   
 private: // Configuration variables
   double      m_swath_width_desired;
   double      m_pd_desired;
   std::string m_report_name;

 private: // State variables
   bool   m_sensor_config_requested;
   bool   m_sensor_config_set;
   bool   m_new_resemblance_factor;
   bool   m_new_detection;
   bool   m_new_classification;

   unsigned int m_sensor_config_reqs;
   unsigned int m_sensor_config_acks;

   unsigned int m_sensor_report_reqs;
   unsigned int m_detection_reports;

   unsigned int m_summary_reports;

   double m_swath_width_granted;
   double m_pd_granted;
   double m_pfa;
   double m_pclass;

   std::string m_group_name;

   XYHazardSet m_hazard_set;
   XYHazardSet m_classification_hazard_set;
   XYHazardSet m_classified_set;
   
   vector<double> m_class_prob;
   vector<double> m_hazard_prob;
   vector<double> m_benign_prob;
   vector<int> m_count;


   void getClassifiedHazardReport();
};

#endif 

