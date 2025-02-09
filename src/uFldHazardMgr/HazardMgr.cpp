/*****************************************************************/
/*    NAME: Michael Benjamin, Henrik Schmidt, and John Leonard   */
/*    ORGN: Dept of Mechanical Eng / CSAIL, MIT Cambridge MA     */
/*    FILE: HazardMgr.cpp                                        */
/*    DATE: Oct 26th 2012                                        */
/*                                                               */
/*    Extended by:                                               */
/*    NAME: Supreeth Subbaraya, Stephanie Kemna                  */
/*    ORGN: Robotic Embedded Systems Lab, CS, USC, CA, USA       */
/*    DATE: Apr, 2013                                            */
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

#include <iterator>
#include "MBUtils.h"
#include "HazardMgr.h"
#include "XYFormatUtilsHazard.h"
#include "ACTable.h"
#include <vector>

using namespace std;

//---------------------------------------------------------
// Constructor

HazardMgr::HazardMgr()
{
  // Config variables
  m_swath_width_desired = 25;
  m_pd_desired          = 0.9;

  // State Variables 
  m_sensor_config_requested = false;
  m_sensor_config_set   = false;
  m_swath_width_granted = 0;
  m_pd_granted          = 0;

  m_sensor_config_reqs = 0;
  m_sensor_config_acks = 0;
  m_sensor_report_reqs = 0;
  m_detection_reports  = 0;

  m_summary_reports = 0;
}

//---------------------------------------------------------
// Procedure: OnNewMail

bool HazardMgr::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key   = msg.GetKey();
    string sval  = msg.GetString(); 

#if 0 // Keep these around just for template
    string comm  = msg.GetCommunity();
    double dval  = msg.GetDouble();
    string msrc  = msg.GetSource();
    double mtime = msg.GetTime();
    bool   mdbl  = msg.IsDouble();
    bool   mstr  = msg.IsString();
#endif
    
    if(key == "UHZ_CONFIG_ACK") 
      handleMailSensorConfigAck(sval);

    else if(key == "UHZ_OPTIONS_SUMMARY") 
      handleMailSensorOptionsSummary(sval);

    else if(key == "UHZ_DETECTION_REPORT") 
      handleMailDetectionReport(sval);

    else if(key == "HAZARDSET_REQUEST") 
      handleMailReportRequest();
      
    else if(key == "NODE_REPORT_LOCAL")
      handleMailOwnNodeReport(sval);
    
    else if(key == "UHZ_HAZARD_REPORT")
      handleMailOwnHazardReport(sval);

    else if(key == "HAZARD_REPORT")
      handleMailOtherHazardReport(sval);

    else 
      reportRunWarning("Unhandled Mail: " + key);
  }
	
   return(true);
}

//---------------------------------------------------------
// Procedure: OnConnectToServer

bool HazardMgr::OnConnectToServer()
{
   registerVariables();
   return(true);
}

//---------------------------------------------------------
// Procedure: Iterate()
//            happens AppTick times per second

bool HazardMgr::Iterate()
{
  AppCastingMOOSApp::Iterate();

  if(!m_sensor_config_requested)
    postSensorConfigRequest();

  if(m_sensor_config_set)
    postSensorInfoRequest();
    
  if(m_new_resemblance_factor || m_new_detection || m_new_classification)
  {}

  AppCastingMOOSApp::PostReport();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()
//            happens before connection is open

bool HazardMgr::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST sParams;
  m_MissionReader.EnableVerbatimQuoting(true);
  if(!m_MissionReader.GetConfiguration(GetAppName(), sParams))
    reportConfigWarning("No config block found for " + GetAppName());

  STRING_LIST::iterator p;
  for(p=sParams.begin(); p!=sParams.end(); p++) {
    string orig  = *p;
    string line  = *p;
    string param = tolower(biteStringX(line, '='));
    string value = line;

    bool handled = false;
    if((param == "swath_width") && isNumber(value)) {
      m_swath_width_desired = atof(value.c_str());
      handled = true;
    }
    else if(((param == "sensor_pd") || (param == "pd")) && isNumber(value)) {
      m_pd_desired = atof(value.c_str());
      handled = true;
    }
    else if(param == "report_name") {
      value = stripQuotes(value);
      m_report_name = value;
      handled = true;
    }
    if(!handled)
      reportUnhandledConfigWarning(orig);
  }
  
  m_hazard_set.setSource(m_host_community);
  m_hazard_set.setName(m_report_name);
  
  registerVariables();	
  return(true);
}

//---------------------------------------------------------
// Procedure: registerVariables

void HazardMgr::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  m_Comms.Register("NODE_REPORT_LOCAL", 0); // to get vehicle group name

  m_Comms.Register("UHZ_DETECTION_REPORT", 0); // detected object
  m_Comms.Register("UHZ_HAZARD_REPORT", 0); // classified object
  m_Comms.Register("HAZARD_REPORT", 0); // classified object other vehicle(s)

  m_Comms.Register("UHZ_OPTIONS_SUMMARY", 0); // all sensor options
  m_Comms.Register("UHZ_CONFIG_ACK", 0); // swath width etc

  m_Comms.Register("HAZARDSET_REQUEST", 0); // need to create hazardset
}

//---------------------------------------------------------
// Procedure: postSensorConfigRequest

void HazardMgr::postSensorConfigRequest()
{
  string request = "vname=" + m_host_community;
  
  request += ",width=" + doubleToStringX(m_swath_width_desired,2);
  request += ",pd="    + doubleToStringX(m_pd_desired,2);

  m_sensor_config_requested = true;
  m_sensor_config_reqs++;
  Notify("UHZ_CONFIG_REQUEST", request);
}

//---------------------------------------------------------
// Procedure: postSensorInfoRequest

void HazardMgr::postSensorInfoRequest()
{
  string request = "vname=" + m_host_community;

  m_sensor_report_reqs++;
  Notify("UHZ_SENSOR_REQUEST", request);
}

//---------------------------------------------------------
// Procedure: handleMailSensorConfigAck
bool HazardMgr::handleMailSensorConfigAck(string str)
{
  // Expected ack parameters:
  string vname, width, pd, pfa, pclass;
  
  // Parse and handle ack message components
  bool   valid_msg = true;
  string original_msg = str;

  vector<string> svector = parseString(str, ',');
  unsigned int i, vsize = svector.size();
  for(i=0; i<vsize; i++) {
    string param = biteStringX(svector[i], '=');
    string value = svector[i];

    if(param == "vname")
      vname = value;
    else if(param == "pd")
      pd = value;
    else if(param == "width")
      width = value;
    else if(param == "pfa")
      pfa = value;
    else if(param == "pclass")
      pclass = value;
    else
      valid_msg = false;       

  }


  if((vname=="")||(width=="")||(pd=="")||(pfa=="")||(pclass==""))
    valid_msg = false;
  
  if(!valid_msg)
    reportRunWarning("Unhandled Sensor Config Ack:" + original_msg);

  
  if(valid_msg) {
    m_sensor_config_set = true;
    m_sensor_config_acks++;
    m_swath_width_granted = atof(width.c_str());
    m_pd_granted = atof(pd.c_str());
    m_pfa = atof(pfa.c_str());
    m_pclass = atof(pclass.c_str());
  }

  return(valid_msg);
}

//---------------------------------------------------------
// Procedure: handleMailDetectionReport
//      Note: The detection report should look something like:
//            UHZ_DETECTION_REPORT = vname=betty,x=51,y=11.3,label=12 

bool HazardMgr::handleMailDetectionReport(string str)
{
  // handle detection
  m_detection_reports++;

  XYHazard new_hazard = string2Hazard(str);

  string hazlabel = new_hazard.getLabel();
  if(hazlabel == "") {
    reportRunWarning("Detection report received for hazard w/out label");
    return(false);
  }

  int ix = m_hazard_set.findHazard(hazlabel);
  if(ix == -1)
    m_hazard_set.addHazard(new_hazard);
  else
    m_hazard_set.setHazard(ix, new_hazard);

// pfa(i) = ( m_pfa + fill_transparency ) / 2 
  string event = "New Detection, label=" + new_hazard.getLabel();
  event += ", x=" + doubleToString(new_hazard.getX(),1);
  event += ", y=" + doubleToString(new_hazard.getY(),1);
  reportEvent(event);

//  // test publish NODE_MESSAGE_LOCAL to share detection with other vehicle
//  // via uFldMessageHandler
//  string detection = "x=" + doubleToString(new_hazard.getX(),1) + 
//                     ",y=" + doubleToString(new_hazard.getY(),1);
//  string detect = "src_node=" + m_host_community + ",dest_group=" + m_group_name +
//                  ",var_name=DETECTION_REPORT,string_val=\"" + detection + "\"";
//  Notify("NODE_MESSAGE_LOCAL",detect);

  // requesting classification
  string req = "vname=" + m_host_community + ",label=" + hazlabel;
  Notify("UHZ_CLASSIFY_REQUEST", req);

  return(true);
}

//---------------------------------------------------------
// Procedure: handleMailHazardReport
//            handles the requested hazard report from shoreside
// Note: The hazard report should look something like:
//  UHZ_HAZARD_REPORT = x=-59.8,y=-294.1,label=13,type=benign,color=orange,hr=0.5 
//  UHZ_HAZARD_REPORT = x=-14.2,y=-293.6,label=08,type=hazard
void HazardMgr::handleMailOwnHazardReport(std::string str)
{
  string classified_object = str;
  classified_object = classified_object + ",pclass=" + doubleToStringX(m_pclass); // add ourself as source

  // test publish NODE_MESSAGE_LOCAL to share classification with other vehicle
  // via uFldMessageHandler
  string report = "src_node=" + m_host_community + ",dest_group=" + m_group_name +
                  ",var_name=HAZARD_REPORT,string_val=\"" + classified_object + "\"";
  Notify("NODE_MESSAGE_LOCAL",report);
  
  // add to the final hazardset
  buildHazardSet(classified_object);
}

//---------------------------------------------------------
// Procedure: handleMailHazardReport
//            handles the received hazard report from other vehicle(s)
// Note: The hazard report should look something like:
//  x=-146,y=-174,label=68,type=benign,hr=0.07473
void HazardMgr::handleMailOtherHazardReport(std::string str)
{
  string received_hazard = str;
  // add to the final hazardset
  buildHazardSet(received_hazard);
}

void HazardMgr::getClassifiedHazardReport()
{
   double probHazard, probBenign;
   
   for( int i = 0 ; i < m_classified_set.size(); i++ )
   {
     if( m_count[i] != 0 )
     {
       probHazard = m_hazard_prob[i] / (double)m_count[i];
       probBenign = m_benign_prob[i] / (double)m_count[i];
     }
     
     XYHazard hazard = m_classified_set.getHazard(i);
     
     if( probHazard >= probBenign )
     {
       hazard.setType("hazard");
       	m_classification_hazard_set.setSource(m_host_community);
       m_classification_hazard_set.addHazard(hazard);
     }
   }
   
}

//---------------------------------------------------------
// Procedure: handleMailReportRequest

void HazardMgr::handleMailReportRequest()
{
  getClassifiedHazardReport();
  m_summary_reports++;

  string summary_report = m_classification_hazard_set.getSpec("final_report");
  Notify("HAZARDSET_REPORT", summary_report);
}

//------------------------------------------------------------
// Procedure: handleMailOwnNodeReport

void HazardMgr::handleMailOwnNodeReport(string sval)
{
  vector<string> svector = parseString(sval, ',');
  unsigned int i, vsize = svector.size();
  for(i=0; i<vsize; i++) 
  {
    string field = biteStringX(svector[i], '=');
    string value = svector[i];

    if( tolower(field) == "group")
      m_group_name = value;
  }
}

//------------------------------------------------------------
//Procedure: buildHazardSet
void HazardMgr::buildHazardSet(std::string sval)
{
    string xString, yString, labelString, typeString, hrString, srcNodeString, pclassString;
    bool   valid_msg = true;
    vector<string> svector = parseString(sval, ',');
    unsigned int i, vsize = svector.size();
    for(i=0; i<vsize; i++)
    {
	string param = biteStringX(svector[i], '=');
	string value = svector[i];

	if(param == "x")
	  xString = value;
	else if(param == "y")
	  yString = value;
	else if(param == "label")
	  labelString = value;
	else if(param == "type")
	  typeString = value;
	else if(param == "hr")
	  hrString = value;
	else if(param == "pclass" )
	  pclassString = value;
	else
	  valid_msg = false;       
    }
    
    XYHazard hazard;

    int index = m_classified_set.findHazard(labelString);
    double classProb = atof( pclassString.c_str() );
    
    if( index == -1 )
    {
	hazard.setX(atoi(xString.c_str()));
	hazard.setY(atoi(yString.c_str()));
	hazard.setLabel(labelString);
	hazard.setType(typeString);
	m_classified_set.setSource(m_host_community);
	m_classified_set.addHazard(hazard);
    
	
	if( typeString == "hazard" )
	{
	  m_hazard_prob.push_back(classProb);
	  m_benign_prob.push_back(1.0 - classProb);
	  m_count.push_back(1);
	}
	else
	{
	  m_benign_prob.push_back(classProb);
	  m_hazard_prob.push_back(1.0 - classProb);
	  m_count.push_back(1);
	}
    }
    
    else
    {
	if( typeString == "hazard" )
	{
	  m_hazard_prob[index] += classProb;
	  m_benign_prob[index] += 1.0 - classProb;
	  m_count[index]++;
	}
	else
	{
	  m_benign_prob[index] += classProb;
	  m_hazard_prob[index] += 1.0 - classProb;
	  m_count[index]++;
	}
    }
}

//------------------------------------------------------------
// Procedure: buildReport()

bool HazardMgr::buildReport() 
{
  m_msgs << "Config Requested:"                                  << endl;
  m_msgs << "    swath_width_desired: " << m_swath_width_desired << endl;
  m_msgs << "             pd_desired: " << m_pd_desired          << endl;
  m_msgs << "   config requests sent: " << m_sensor_config_reqs  << endl;
  m_msgs << "                  acked: " << m_sensor_config_acks  << endl;
  m_msgs << "------------------------ "                          << endl;
  m_msgs << "Config Result:"                                     << endl;
  m_msgs << "       config confirmed: " << boolToString(m_sensor_config_set) << endl;
  m_msgs << "    swath_width_granted: " << m_swath_width_granted << endl;
  m_msgs << "             pd_granted: " << m_pd_granted          << endl << endl;
  m_msgs << "--------------------------------------------" << endl << endl;

  m_msgs << "               sensor requests: " << m_sensor_report_reqs << endl;
  m_msgs << "             detection reports: " << m_detection_reports  << endl << endl; 

  m_msgs << "   Hazardset Reports Requested: " << m_summary_reports << endl;
  m_msgs << "      Hazardset Reports Posted: " << m_summary_reports << endl;
  m_msgs << "                   Report Name: " << m_report_name << endl;

  return(true);
}
