/*****************************************************************/
/*    NAME: Stephanie Kemna                                      */
/*    ORGN: Robotic Embedded Systems Lab, CS, USC, CA, USA       */
/*    FILE: GP.cpp                                               */
/*    DATE: Mar 29, 2014                                         */
/*                                                               */
/*                                                               */
/*****************************************************************/

#include "GP.h"

#include <iterator>
#include "MBUtils.h"

#include "math.h"
#include <limits>

#include <boost/lexical_cast.hpp>

#include <cov.h>
#include <cov_se_iso.h>

// check running time
#include <ctime>

// GPLib Rprop
#include "rprop.h"

// write to file
#include <fstream>

//---------------------------------------------------------
// Constructor
//
GP::GP() :
  m_input_var_data(""),
  m_input_var_sample_points(""),
  m_input_var_sample_points_specs(""),
  m_input_var_adaptive_trigger(""),
  m_input_var_share_data(""),
  m_output_var_pred(""),
  m_output_filename_prefix(""),
  m_output_var_share_data(""),
  m_prediction_interval(-1),
  m_use_MI(false),
  m_veh_name(""),
  m_use_log_gp(true),
  m_lat(0),
  m_lon(0),
  m_dep(0),
  m_pause_data_adding(false),
  m_last_published(std::numeric_limits<double>::max()),
  m_last_pred_save(std::numeric_limits<double>::max()),
  m_min_lon(0.0),
  m_min_lat(0.0),
  m_max_lon(0.0),
  m_max_lat(0.0),
  m_pts_grid_width(0.0),
  m_pts_grid_height(0.0),
  m_pts_grid_spacing(0.0),
  m_lon_spacing(0.0),
  m_lat_spacing(0.0),
  m_y_resolution(0.0),
  m_lon_deg_to_m(0.0),
  m_lat_deg_to_m(0.0),
  m_pilot_done_time(0.0),
  m_hp_optim_done_time(0.0),
  m_need_nxt_wpt(false),
  m_finding_nxt(false),
  m_gp(2, "CovSum(CovSEiso, CovNoise)"),
  m_hp_optim_running(false),
  m_hp_optim_done(false),
  m_data_mail_counter(1),
  m_finished(false),
  m_hp_optim_mode_cnt(0),
  m_num_vehicles(1),
  m_data_pt_counter(0),
  m_data_send_reserve(0),
  m_received_shared_data(false),
  m_timed_data_sharing(false),
  m_data_sharing_activated(false),
  m_sending_data(false),
  m_waiting(false),
  m_received_ready(false),
  m_output_var_handshake_data_sharing(""),
  m_last_ready_sent(0),
  m_acomms_sharing(false),
  m_last_acomms_string(""),
  m_use_voronoi(false),
  m_data_sharing_requested(false),
  m_send_surf_req(false),
  m_send_ack(false)
{
  // class variable instantiations can go here
  // as much as possible as function level initialization

  // TODO move this to library?
  m_lat_deg_to_m = 110923.99118801417;
  m_lon_deg_to_m = 92287.20804979937;

  // above we initialize a GP for 2D input data, //TODO convert to 3D
  // using the squared exponential covariance function,
  // with additive white noise

  // Set (log of) hyperparameters of the covariance function
  Eigen::VectorXd params(m_gp.covf().get_param_dim());
  // hyperparameters: length scale l^2, signal variance s_f^2, noise variance s_n^2
  // note, these will be optimized using cg or rprop
  // length scale: avg lat/lon_deg_to_m is 10000, 10m range = 0.001
  //               0.002^2=0.000004, ln() = -12.4292
  // signal: from 0 to ca 30, log scale <0 to 1.5
  //         stdev 1.5, ln() = 0.4055
  // noise: let's set 10x smaller than signal
  //        stdev 0.15, ln() = -1.8971
  params << -12.4292, 0.4055, -1.8971;
  m_gp.covf().set_loghyper(params);

  // seed the random number generator
  srand((int)time(0));
}

GP::~GP()
{
  if ( m_ofstream_pm.is_open() )
    m_ofstream_pm.close();
  if ( m_ofstream_pv.is_open() )
    m_ofstream_pv.close();
  if ( m_ofstream_pmu.is_open() )
    m_ofstream_pmu.close();
  if ( m_ofstream_psigma2.is_open() )
    m_ofstream_psigma2.close();
}

//---------------------------------------------------------
// Procedure: OnNewMail
//
// when variables are updated in the MOOSDB,
// there is 'new mail', check to see if
// there is anything for this process.
//
bool GP::OnNewMail(MOOSMSG_LIST &NewMail)
{
  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++)
  {
    CMOOSMsg &msg = *p;
    std::string key   = msg.GetKey();

    std::string sval  = msg.GetString();
    // separate way for getting the double val (sval was not working for DB_UPTIME)
    double dval  = msg.GetDouble();

#if 0 // Keep these around just for template
    std::string comm  = msg.GetCommunity();
    double dval  = msg.GetDouble();
    std::string msrc  = msg.GetSource();
    double mtime = msg.GetTime();
    bool   mdbl  = msg.IsDouble();
    bool   mstr  = msg.IsString();
#endif

    if ( key == m_input_var_data )
    {
      // add data to GP, coming from uSimBioSensor
      // note, if we are circling at location to calculate next waypoint,
      // then it makes no sense to keep adding those data points, so
      // let's not do that
      // presumably we will have added the location just before we reached
      // the waypoint (if not, maybe change this to add once)
      // similarly for when we are calculating the hyperparameters
      // although this is outside the area so should not be a problem anyway
      m_data_mail_counter++;
      if ( !m_pause_data_adding && !m_hp_optim_running &&
           (m_data_mail_counter % 2 == 0) && !m_data_sharing_activated)
        handleMailData(dval);
    }
    else if ( key == "NAV_LAT" )
      m_lat = dval;
    else if ( key == "NAV_LONG" )
      m_lon = dval;
    else if ( key == "NAV_DEPTH" )
      m_dep = dval;
    else if ( key == m_input_var_sample_points )
    {
      // store list of sample locations coming pSamplePoints
      // (done once)
      handleMailSamplePoints(sval);
    }
    else if ( key == m_input_var_sample_points_specs )
    {
      // store specs for sample points
      // (done once)
      handleMailSamplePointsSpecs(sval);
    }
    else if ( key == m_input_var_adaptive_trigger )
    {
      // check when adaptive waypoint reached
      // these are flags set by the adaptive wpt behavior
      // if we get it, it must mean a wpt/cycle was done
      m_need_nxt_wpt = true;
    }
    else if ( key == "MODE" )
    {
      // check the mission state for whether we need to do HP optimization
      if ( sval.find("HP_OPTIM") != std::string::npos )
      {
        m_hp_optim_mode_cnt++;
        if ( m_hp_optim_mode_cnt == 1 )
          m_pilot_done_time = MOOSTime();
      }
    }
    else if ( key == m_input_var_share_data )
    {
      // handle
      std::string incoming_data_string = sval;
      size_t index_colon = incoming_data_string.find_first_of(':');
      if ( incoming_data_string.substr(0, index_colon) != m_veh_name )
      {
        m_received_shared_data = true;
        std::cout << "received data from other vehicle" << std::endl;

        // extract actual data
        std::string incoming_data = incoming_data_string.substr(index_colon+1, incoming_data_string.length());

        m_incoming_data_to_be_added.push_back(incoming_data);
      }
      else
        std::cout << "skipping my own data" << std::endl;
    }
    else if ( key == "LOITER_DIST_TO_POLY" )
      m_loiter_dist_to_poly = dval;
    else if ( key == m_input_var_handshake_data_sharing )
    {
      if ( sval != m_veh_name )
      {
        if ( m_dep < 0.1 ) // simulate that we only received on surface
        {
          std::cout << "received READY from: " << sval << std::endl;
          m_received_ready = true;
        }
      }
    }
    else if ( key == "INCOMING_DATA_ACOMMS" )
    {
      handleMailDataAcomms(sval);
    }
    else if ( key == "NODE_REPORT")
    {
      handleMailNodeReports(sval);
    }
    else if ( key == "TEST_VORONOI" )
      calcVoronoi();
    else if ( key == "REQ_SURFACING_REC" )
    { // receive surfacing request from other vehicle
      // need to send ack, also start surfacing
      bool own_msg = ownMessage(sval);
      std::cout << "REQ_SURFACING_REC own msg? " << own_msg << std::endl;
      std::cout << "m_data_sharing_requested? " << m_data_sharing_requested << std::endl;
      if ( !own_msg && !m_data_sharing_requested && !m_send_ack )
      {
        // prep for surfacing
        m_data_sharing_requested = true;
        // send ack, do actual sending in Iterate so we send until surface handshake
        m_send_ack = true;
      }
    }
    else if ( key == "REQ_SURFACING_ACK_REC" )
    { // receive surfacing req ack from other vehicle)
      // ack received, start surfacing
      bool own_msg = ownMessage(sval);
      std::cout << "REQ_SURFACING_ACK_REC own msg? " << own_msg << std::endl;
      std::cout << "m_data_sharing_requested? " << m_data_sharing_requested << std::endl;
      if ( !own_msg && !m_data_sharing_requested && m_send_surf_req )
      {
        // prep for surfacing
        m_data_sharing_requested = true;
        // received ack, stop sending surfacing request
        m_send_surf_req = false;
      }
    }
    else
      std::cout << "pGP :: Unhandled Mail: " << key << std::endl;

  }

  return(true);
}


//---------------------------------------------------------
// Procedure: OnConnectToServer
//
bool GP::OnConnectToServer()
{
//  registerVariables();
  return(true);
}

//---------------------------------------------------------
// Procedure: Iterate()
//            happens AppTick times per second
//
bool GP::Iterate()
{
  if ( m_lon == 0 && m_lat == 0 && m_dep == 0 )
    return true;
  else
  {
    //// PILOT HP OPTIM ////////////////////////////////////////////////////////
    // when pilot is done,
    // we want to optimize the hyperparams of the GP
    if ( m_hp_optim_mode_cnt == 1 && !m_hp_optim_done && !m_finished )
    {
      if ( !m_hp_optim_running )
      {
        // start hyperparameter optimization
        m_hp_optim_running = true;

        // start thread for hyperparameter optimization,
        // because this will take a while..
        std::cout << "Starting hyperparameter optimization, current size GP: " << m_gp.get_sampleset_size() << std::endl;
        m_future_hp_optim = std::async(std::launch::async, &GP::runHPOptimization, this, std::ref(m_gp), 20);
      }
      else
      {
        // running HP optimization
        // check if the thread is done
        if ( m_future_hp_optim.wait_for(std::chrono::microseconds(1)) == std::future_status::ready )
        {
          m_hp_optim_done = m_future_hp_optim.get(); // should be true
          if ( !m_hp_optim_done )
            std::cout << "ERROR: should be done with HP optimization, but get() returns false!" << std::endl;
          m_hp_optim_running = false;
          m_hp_optim_done_time = MOOSTime();
          std::cout << "Done with hyperparameter optimization. New HPs: " << m_gp.covf().get_loghyper() << std::endl;
          m_Comms.Notify("STAGE","survey");
        }
      }
    }


    //// ADAPTIVE //////////////////////////////////////////////////////////////
    // when hyperparameter optimization is done,
    // we want to run adaptive; find next sample locations
    if ( m_hp_optim_done && m_hp_optim_mode_cnt < 2 && !m_finished )
    {
      // **** NXT WAYPOINT (GREEDY) ******************************************//
      // predict target value and variance for sample locations
      //    if ( (size_t)std::floor(MOOSTime()) % m_prediction_interval == 0  &&  ) // every 5 min, for now
      if ( m_need_nxt_wpt && (std::abs(m_last_published - MOOSTime()) > 1.0) && !m_data_sharing_activated  )
        findAndPublishNextWpt();


      // **** ACOMMS VORONOI PARTITIONING ************************************//
      // TODO: for TDS, need to request DS and then share after handshake and
      //                having shared data, calculate voronoi region
      // check if we need to recalculate Voronoi region
      if ( m_use_voronoi && m_acomms_sharing && m_voronoi_region.size() > 0 )
      {
          if ( needToRecalculateVoronoi() )
            calcVoronoi();
      }
      else if ( m_use_voronoi && m_voronoi_region.size() == 0 )
      {
        // we need to initialize the voronoi region
        calcVoronoi();
      }


      // **** TDS ************************************************************//
      // if we are doing timed data sharing,
      if ( m_timed_data_sharing )
      {
        // the vehicles may not be done with the pilot at the same time,
        // so let's time this based on MOOSTime(), and assume that clocks are
        // synchronised (to be verified)
        // note; may need to add buffer to block out x seconds after hpoptim_done
        if ( !m_use_voronoi &&
              m_num_vehicles > 1 &&
             ( (size_t)std::floor(MOOSTime()) % 600 ) == 0 &&
             !m_data_sharing_activated )
        {
          // switch to data sharing mode, to switch bhv to surface
          Notify("STAGE","data_sharing");
          m_data_sharing_activated = true;
        }

        //std::cout << " needToRecalculateVoronoi(): " << needToRecalculateVoronoi() << std::endl;
        //std::cout << " MOOSTime - last calc time: " << (MOOSTime()-m_last_voronoi_calc_time) << std::endl;
        // tds with voronoi, trigger for when to request data sharing
        if ( m_use_voronoi && m_voronoi_region.size() > 0 && needToRecalculateVoronoi()
             && !m_data_sharing_requested && !m_data_sharing_activated
             && ((MOOSTime()-m_last_voronoi_calc_time) > 300) )
        {
          // request surfacing through acomms
          m_send_surf_req = true;
        }
        // do actual sending of surface request and ack based on global vars
        // such that we do this until no longer desired
        if ( m_send_surf_req )
          m_Comms.Notify("REQ_SURFACING","true");
        if ( m_send_ack )
          m_Comms.Notify("REQ_SURFACING_ACK","true");
        //
        // if ack received, trigger requested (in handleMail)
        //
        // if received data sharing request, or if ack received, start ds
        if ( m_use_voronoi && m_data_sharing_requested && !m_data_sharing_activated )
        { // TODO added time check to avoid calc on first entry- TODO replace?
          Notify("STAGE","data_sharing");
          m_data_sharing_activated = true;
        }

        // when at the surface, send data
        if ( m_data_sharing_activated && !m_sending_data && m_dep < 0.1 )
          tdsHandshake();

        // next: check if received data added,
        // if so, then switch mode back to survey
        if ( m_data_sharing_activated && m_sending_data && m_received_shared_data )
          tdsReceiveData();
      }


      // **** SAVING GP TO FILE **********************************************//
      // TODO; change how this is done, given how data sharing is done?
      // periodically (every 600s = 10min), store all GP predictions
      // only after pilot done, first one 600 seconds after HP optimiz done
      // (storing of GP right after HP optimization is started in runHPOptimization)
      if ( (std::abs(m_last_pred_save - MOOSTime()) > 1.0 ) &&
           ((size_t)std::floor(MOOSTime()-m_hp_optim_done_time) % 600 == 0) )
      {
        std::thread pred_store(&GP::makeAndStorePredictions, this);
        pred_store.detach();
        m_last_pred_save = MOOSTime();
      }
    } // if, after hyperparam optim done


    //// FINAL HP OPTIM ////////////////////////////////////////////////////////
    // when returning, do a final HP optimization
    if ( m_hp_optim_mode_cnt == 2 && !m_finished )
    {
      if ( !m_hp_optim_running )
      {
        // start hyperparameter optimization
        m_hp_optim_running = true;

        // start thread for hyperparameter optimization,
        // because this will take a while..
        std::cout << "Starting hyperparameter optimization, current size GP: " << m_gp.get_sampleset_size() << std::endl;
        m_future_hp_optim = std::async(std::launch::async, &GP::runHPOptimization, this, std::ref(m_gp), 10);
      }
      else
      {
        // running HP optimization
        // check if the thread is done
        if ( m_future_hp_optim.wait_for(std::chrono::microseconds(1)) == std::future_status::ready )
        {
          m_hp_optim_done = m_future_hp_optim.get(); // should be true
          if ( !m_hp_optim_done )
            std::cout << "ERROR: should be done with HP optimization, but get() returns false!" << std::endl;
          m_hp_optim_running = false;
          std::cout << "Done with hyperparameter optimization. New HPs: " << m_gp.covf().get_loghyper() << std::endl;
          m_Comms.Notify("STAGE","return");

          // store predictions one last time
          std::thread pred_store(&GP::makeAndStorePredictions, this);
          pred_store.detach();

          m_finished = true;
        }
      }
    }
  }
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()
//            happens before connection is open
bool GP::OnStartUp()
{
  CMOOSApp::OnStartUp();

  STRING_LIST sParams;
  m_MissionReader.EnableVerbatimQuoting(true);
  if (!m_MissionReader.GetConfiguration(GetAppName(), sParams))
    std::cout << GetAppName() << " :: No config block found for " << GetAppName();

  initGeodesy();

  STRING_LIST::iterator p;
  for (p=sParams.begin(); p!=sParams.end(); p++)
  {
    std::string orig  = *p;
    std::string line  = *p;
    std::string param = tolower(biteStringX(line, '='));
    std::string value = line;

    bool handled = true;
    if ( param == "input_var_data" )
    {
      m_input_var_data = toupper(value);
    }
    else if ( param == "input_var_sample_points" )
    {
      m_input_var_sample_points = toupper(value);
    }
    else if ( param == "input_var_sample_points_specs")
    {
      m_input_var_sample_points_specs = toupper(value);
    }
    else if ( param == "input_var_adaptive_trigger" )
    {
      m_input_var_adaptive_trigger = toupper(value);
    }
    else if ( param == "output_var_predictions" )
    {
      m_output_var_pred = toupper(value);
    }
    else if ( param == "prediction_interval" )
    {
      m_prediction_interval = (size_t)atoi(value.c_str());
      if ( m_prediction_interval < 0 )
      {
        std::cout << GetAppName() << " :: ERROR, invalid prediction interval, needs to be > 0" << std::endl;
        RequestQuit();
      }
    }
    else if ( param == "output_filename_prefix" )
    {
      m_output_filename_prefix = value;

      // prepare file actions
      std::string file_pm = (m_output_filename_prefix + "_pred_mean.csv");
      std::string file_pv = (m_output_filename_prefix + "_pred_var.csv");
      m_ofstream_pm.open(file_pm, std::ios::app);
      m_ofstream_pv.open(file_pv, std::ios::app);
      if ( m_use_log_gp )
      {
        std::string file_pmu = (m_output_filename_prefix + "_post_mu.csv");
        std::string file_psigma = (m_output_filename_prefix + "_post_sigma2.csv");
        m_ofstream_pmu.open(file_pmu, std::ios::app);
        m_ofstream_psigma2.open(file_psigma, std::ios::app);
      }
    }
    else if ( param == "use_log_gp" )
    {
      m_use_log_gp = (value == "true" ? true : false);
      std::cout << GetAppName() << " :: using log GP? " << (m_use_log_gp ? "yes" : "no") << std::endl;
    }
    else if ( param == "nr_vehicles" )
    {
      m_num_vehicles = (size_t)atoi(value.c_str());
      std::cout << GetAppName() << " :: nr_vehicles: " << m_num_vehicles << std::endl;
    }
    else if ( param == "output_var_share_data" )
    {
      m_output_var_share_data = toupper(value);
    }
    else if ( param == "input_var_share_data" )
    {
      m_input_var_share_data = toupper(value);
    }
    else if ( param == "timed_data_sharing" )
    {
      m_timed_data_sharing = (value == "true" ? true : false);
    }
    else if ( param == "output_var_handshake_data_sharing" )
    {
      m_output_var_handshake_data_sharing = toupper(value);
    }
    else if ( param == "input_var_handshake_data_sharing" )
    {
      m_input_var_handshake_data_sharing = toupper(value);
    }
    else if ( param == "acomms_sharing" )
    {
      m_acomms_sharing = (value == "true" ? true : false);
    }
    else if ( param == "use_voronoi")
    {
      m_use_voronoi = (value == "true" ? true : false);
    }
    else
      handled = false;

    if ( !handled )
      std::cout << GetAppName() << " :: Unhandled Config: " << orig << std::endl;
  }

  if ( m_input_var_data == "" || m_input_var_sample_points == "" )
  {
    std::cout << GetAppName() << " :: ERROR, missing input variable name, exiting." << std::endl;
    RequestQuit();
  }
  else if ( m_output_var_pred == "" )
  {
    std::cout << GetAppName() << " :: ERROR, missing output variable name, exiting." << std::endl;
    RequestQuit();
  }

  // store vehicle name
  if ( !m_MissionReader.GetValue("Community",m_veh_name) )
  {
     m_veh_name = "error";
     std::cout << GetAppName() << " :: Unable to retrieve vehicle name! What is 'Community' set to?" << std::endl;
  }

  registerVariables();

  std::thread data_thread(&GP::dataAddingThread, this);
  data_thread.detach();

  m_last_voronoi_calc_time = MOOSTime();

  return(true);
}

//---------------------------------------------------------
// Procedure: initGeodesy
//            initialize MOOS Geodesy for lat/lon conversions
//
void GP::initGeodesy()
{
  // get lat/lon origin from MOOS file
  bool failed = false;
  double latOrigin, longOrigin;
  if ( !m_MissionReader.GetValue("LatOrigin", latOrigin) )
  {
    std::cout << GetAppName() << " :: LatOrigin not set in *.moos file." << std::endl;
    failed = true;
  }
  else if ( !m_MissionReader.GetValue("LongOrigin", longOrigin) )
  {
    std::cout << GetAppName() << " :: LongOrigin not set in *.moos file" << std::endl;
    failed = true;
  }
  else {
    // initialize m_geodesy
    if ( !m_geodesy.Initialise(latOrigin, longOrigin) )
    {
      std::cout << GetAppName() << " :: Geodesy init failed." << std::endl;
      failed = true;
    }
  }
  if ( failed )
  {
    std::cout << GetAppName() << " :: failed to initialize geodesy, exiting." << std::endl;
    RequestQuit();
  }
  else
    std::cout << GetAppName() << " :: Geodesy initialized: lonOrigin, latOrigin = " << longOrigin << ", " << latOrigin << std::endl;
}


//---------------------------------------------------------
// Procedure: registerVariables
//            at startup, let the MOOSDB know what you want
//            to receive
//
void GP::registerVariables()
{
  // get vehicle location
  m_Comms.Register("NAV_LAT", 0);
  m_Comms.Register("NAV_LONG", 0);
  m_Comms.Register("NAV_DEPTH", 0);

  // get data for GP
  m_Comms.Register(m_input_var_data, 0);

  // get sample points for GP
  m_Comms.Register(m_input_var_sample_points, 0);
  m_Comms.Register(m_input_var_sample_points_specs, 0);

  // get status mission
  m_Comms.Register("MODE", 0);

  // get when wpt cycle finished
  // (when to run adaptive predictions in adaptive state)
  m_Comms.Register(m_input_var_adaptive_trigger, 0);

  // data sharing
  m_Comms.Register(m_input_var_share_data, 0);
  m_Comms.Register("LOITER_DIST_TO_POLY",0);
  m_Comms.Register(m_input_var_handshake_data_sharing,0);

  // get data from other vehicles
  m_Comms.Register("INCOMING_DATA_ACOMMS",0);

  // get other vehicles' locations
  m_Comms.Register("NODE_REPORT",0);

  // tmp test voronoi partitioning
  m_Comms.Register("TEST_VORONOI",0);

  // surfacing requests
  m_Comms.Register("REQ_SURFACING_REC",0); // receive surfacing request from other vehicle
  m_Comms.Register("REQ_SURFACING_ACK_REC",0); // receive surfacing req ack from other vehicle

  std::cout << GetAppName() << " :: Done registering, registered for: " << std::endl;
  std::set<std::string> get_registered = m_Comms.GetRegistered();
  for ( auto registered : get_registered )
    std::cout << registered << std::endl;
}

//---------------------------------------------------------
// Procedure: handleMailData
//            handle the incoming message
//
void GP::handleMailData(double received_data)
{
  if ( m_lon == 0 && m_lat == 0 && m_dep == 0 )
    std::cout << GetAppName() << "No NAV_LAT/LON/DEPTH received, not processing data." << std::endl;
  else
  {
    // if not running hyperparameter optimization or calculating nxt wpt
    // add training data

    // get the current vehicle location, such that we can pass it on to the functions
    // that add the data and update the (un)visited maps,
    // which can run in parallel
    double veh_lon = m_lon;
    double veh_lat = m_lat;

    // try with threading, because this becomes more costly as GP grows
    // TODO pass into queue and run different function as thread to handle queue?
    std::vector<double> nw_data_pt{veh_lon, veh_lat, received_data};
    m_queue_data_points_for_gp.push(nw_data_pt);

    // if we need to exchange data, then store for this purpose
    if ( m_num_vehicles > 1 )
      storeDataForSending(veh_lon, veh_lat, received_data);
  }
}

//---------------------------------------------------------
// Procedure: handleMailReceivedDataPts
//            taken the received data, and add them to the GP
//
size_t GP::handleMailReceivedDataPts(std::string incoming_data)
{
  // take ownership of m_gp
  std::unique_lock<std::mutex> gp_lock(m_gp_mutex, std::defer_lock);
  while ( !gp_lock.try_lock() ) {}

  // parse string

  // 1. split all data points into a vector
  std::vector<std::string> sample_points = parseString(incoming_data, ';');

  // 2. for each, add to GP
  for ( std::string & data_pt_str : sample_points )
  {
    std::vector<std::string> data_pt_components = parseString(data_pt_str, ',');
    double veh_lon = atof(data_pt_components[0].c_str());
    double veh_lat = atof(data_pt_components[1].c_str());

    double loc [2] = {veh_lon, veh_lat};

    double data = atof(data_pt_components[2].c_str());
    double save_val = m_use_log_gp ? log(data) : data;

    m_gp.add_pattern(loc, save_val);

    // update visited set if needed
    int index = getIndexForMap(veh_lon, veh_lat);
    if ( index >= 0 && needToUpdateMaps((size_t)index) )
      updateVisitedSet(veh_lon, veh_lat, (size_t)index);

    // run via data adding thread? no, we want to make sure they've been added,
    // and not doing anything else anyway
//    std::vector<double> nw_data_pt{veh_lon, veh_lat, save_val};
//    m_queue_data_points_for_gp.push(nw_data_pt);

  }

  // release lock
  gp_lock.unlock();

  return sample_points.size();
}

//---------------------------------------------------------
// Procedure: handleMailSamplePoints
//            parse the string, store the sample locations
//
void GP::handleMailSamplePoints(std::string input_string)
{
  // input: semicolon separated string of comma separated locations
  // separate by semicolon
  std::vector<std::string> sample_points = parseString(input_string, ';');
  // for each, add to vector
  m_sample_locations.reserve(sample_points.size());

  std::ofstream ofstream_loc;
  std::string m_file_loc = (m_output_filename_prefix + "_locations.csv");
  ofstream_loc.open(m_file_loc, std::ios::out);

  for ( size_t id_pt = 0; id_pt < sample_points.size(); id_pt++ )
  {
    std::string location = sample_points.at(id_pt);
    size_t comma_pos = location.find(',');
    double lon = (double)atof(location.substr(0,comma_pos).c_str());
    double lat = (double)atof(location.substr(comma_pos+1,location.length()).c_str());

    // new storage:
    // 1. assume ordered as per creation (if needed, we can recalculate the v_id)
    // 2. store in a map; v_id is key, location is value
    // 3. later on, we can easily retrieve from map / store in other
    Eigen::Vector2d loc_vec;
    loc_vec(0) = lon;
    loc_vec(1) = lat;
    m_sample_points_unvisited.insert( std::pair<size_t, Eigen::Vector2d>(id_pt, loc_vec) );
    // copy the points to have all sample points for easier processing for predictions
    m_sample_locations.push_back(std::pair<double, double>(lon, lat));
    // save to file
    ofstream_loc << std::setprecision(15) << lon << ", " << lat << '\n';

    // the first location should be bottom left corner, store as minima
    if ( id_pt == 0 )
    {
      m_min_lon = lon;
      m_min_lat = lat;
      std::cout << GetAppName() << " :: SW Sample location: " << std::setprecision(10) << lon << " " << lat << std::endl;
    }
    if ( id_pt == sample_points.size()-1 )
    {
      // last location = top right corner, store as maxima
      m_max_lon = lon;
      m_max_lat = lat;
      std::cout << GetAppName() << " :: NE Sample location: " << std::setprecision(10) << lon << " " << lat << std::endl;
    }
  }
  ofstream_loc.close();
  // check / communicate what we did
  std::cout << GetAppName() << " :: stored " << m_sample_points_unvisited.size() << " sample locations" << std::endl;
}

//---------------------------------------------------------
// Procedure: storeSamplePointsSpecs
//            parse the string, store specs for sample locations
//
void GP::handleMailSamplePointsSpecs(std::string input_string)
{
  // input: comma-separated list of param=value pairs
  // separate by comma
  std::vector<std::string> sample_points_specs = parseString(input_string, ',');
  // for each, check what it is, and store
  for ( size_t param_idx = 0; param_idx < sample_points_specs.size(); param_idx++ )
  {
    std::string param = biteStringX(sample_points_specs[param_idx], '=');
    double value = (double)atof(sample_points_specs[param_idx].c_str());
    if ( param == "width" )
      m_pts_grid_width = value;
    else if ( param == "height" )
      m_pts_grid_height = value;
    else if ( param == "lane_width")
    {
      m_pts_grid_spacing = value;
      calcLonLatSpacing();
    }
    else
      std::cout << GetAppName() << " :: error, unhandled part of sample points specs: " << param << std::endl;
  }
  std::cout << GetAppName() << " :: width, height, spacing: " << m_pts_grid_width << ", " <<
               m_pts_grid_height << ", " << m_pts_grid_spacing << std::endl;
}

//---------------------------------------------------------
// Procedure: handleMailDataAcomms(std::string css)
//            take incoming comma-separated string (css)
//            parse, and if not from self, add data to GP
//
void GP::handleMailDataAcomms(std::string css)
{
  std::vector<std::string> incoming_str = parseString(css,',');
  std::string name = incoming_str[0].substr(5,incoming_str[0].length()-1);

  // do not do anything if this is my own message, or I am running HP optim
  // or I am finished
  if ( name == m_veh_name || m_hp_optim_running || m_finished )
    return;

  // if we immediately get the exact same data string, do not process
  if ( css == m_last_acomms_string )
  {
    std::cout << GetAppName() << " :: received the same data through acomms, ignoring" << std::endl;
    return;
  }
  m_last_acomms_string = css;

  std::cout << "Received data from: " << name << std::endl;

  // for all data points in this string,
  // add to data adding queue
  for ( size_t cnt = 0; cnt < (incoming_str.size()-1)/4; cnt++ )
  {
    // grab data value for current i
    std::string d_str = incoming_str[cnt*4+4];
    double d_i = atof(d_str.substr(d_str.find('=')+1, d_str.length()).c_str());

    // if data is valid, process
    if ( d_i > 0 )
    {
      // grab x,y,z for current i
      std::string x_str = incoming_str[cnt*4+1];
      std::string y_str = incoming_str[cnt*4+2];
      std::string z_str = incoming_str[cnt*4+3];
      // get the values from the xi=XX string
      double x_i = atof(x_str.substr(x_str.find('=')+1, x_str.length()).c_str());
      double y_i = atof(y_str.substr(y_str.find('=')+1, y_str.length()).c_str());
      double z_i = atof(z_str.substr(z_str.find('=')+1, z_str.length()).c_str());

      // convert x,y to lon,lat
      double lon, lat;
      bool converted = convUTMToLonLat(x_i, y_i, lon, lat);
      // add to data adding queue
      if ( converted )
      {
        std::cout << "adding: " << lon << "," << lat << "," << d_i << std::endl;
        std::vector<double> nw_data_pt{lon, lat, d_i};
        m_queue_data_points_for_gp.push(nw_data_pt);
      }
    }
    else
      std::cout << GetAppName() << " :: received invalid data, ignoring" << std::endl;
  }

}

//---------------------------------------------------------
// Procedure: handleMailNodeReports(const std::string &input_string)
//            take incoming comma-separated string (css)
//            parse, and if not from self, store vehicle location
//
void GP::handleMailNodeReports(const std::string &input_string)
{
  // eg. NAME=anna,LAT=0,LON=10
  std::vector<std::string> str_tok = parseString(input_string, ',');

  // init vars for data we want
  double veh_lon, veh_lat;
  std::string veh_nm;

  // for each var=val pair, store only those we are interested in
  // quit if it is our own node_report
  for ( std::string const & varval : str_tok )
  {
    size_t equal_index = varval.find('=');
    if ( equal_index == std::string::npos )
    {
      std::cout << "ERROR: cannot find '=' in string NODE_REPORT" << std::endl;
      return;
    }
    std::string var = varval.substr(0,equal_index);
    std::string val = varval.substr(equal_index+1,varval.length());

    if ( var == "NAME" && val == m_veh_name )
      return;

    if ( var == "NAME" )
      veh_nm = val;
    else if ( var == "LON" )
      veh_lon = atof(val.c_str());
    else if ( var == "LAT" )
      veh_lat = atof(val.c_str());
  }

  if ( veh_nm != "" )
  {
    // store the vehicle info
    if ( m_other_vehicles.find(veh_nm) == m_other_vehicles.end() )
      m_other_vehicles.insert(std::pair<std::string, std::pair<double, double> >(veh_nm,std::pair<double,double>(veh_lon, veh_lat)));
    else
      m_other_vehicles[veh_nm] = std::pair<double,double>(veh_lon,veh_lat);
  }
}


//---------------------------------------------------------
// Procedure: dataAddingThread()
//            continuously check the data point queue to see
//            if points need to be added to the GP, if so
//            then call func to add points to GP
//
void GP::dataAddingThread()
{
  while ( !m_finished )
  {
    if ( m_queue_data_points_for_gp.empty() )
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    else
    {
      // process data point
      std::vector<double> data_pt = m_queue_data_points_for_gp.front();
      m_queue_data_points_for_gp.pop();
      double veh_lon = data_pt[0];
      double veh_lat = data_pt[1];

      // add data pt
      addPatternToGP(veh_lon, veh_lat, data_pt[2]);

      // update visited set if needed
      int index = getIndexForMap(veh_lon, veh_lat);
      if ( index >= 0 && needToUpdateMaps((size_t)index) )
        updateVisitedSet(veh_lon, veh_lat, (size_t)index);
    }
  }
}

//---------------------------------------------------------
// Procedure: addPatternToGP(double veh_lon, double veh_lat, double value)
//            function to add points to GP, using mutex so as
//            to not disturb when other processes are reading from GP
//
void GP::addPatternToGP(double veh_lon, double veh_lat, double value)
{
  // limit scope mutex, protect when adding data
  // because this is now happening in a detached thread
  double location[2] = {veh_lon, veh_lat};

  // GP: pass in value
  // log GP: take log (ln) of measurement
  double save_val = m_use_log_gp ? log(value) : value;

  // Input vectors x must be provided as double[] and targets y as double.

  std::unique_lock<std::mutex> ap_lock(m_gp_mutex, std::defer_lock);
  // obtain lock
  while ( !ap_lock.try_lock() ) {}
  // add new data point to GP
  m_gp.add_pattern(location, save_val);
  // release mutex
  ap_lock.unlock();
}


//---------------------------------------------------------
// Procedure: storeDataForSending(double vlon, double vlat, double data)
//            for interval-based data sharing, push back data points that have
//            not been shared yet
//
void GP::storeDataForSending(double vlon, double vlat, double data)
{
  // save the data point in a vector that we will send
  std::ostringstream data_str_stream;
  data_str_stream << std::setprecision(10) << vlon << "," << vlat << ","
                  << std::setprecision(5) << data;

  if ( m_data_pt_counter > m_data_send_reserve || m_data_pt_counter == 0 )
  {
    // preallocate vector memory
    m_data_send_reserve += 1500;
    m_data_to_send.reserve(m_data_send_reserve);
  }

  m_data_to_send.push_back(data_str_stream.str());

  m_data_pt_counter++;
}


//---------------------------------------------------------
// Procedure: needToUpdateMaps
//            check if location's grid index is in unvisited map
//
bool GP::needToUpdateMaps(size_t grid_index)
{
    // add mutex for changing of global maps
    std::unique_lock<std::mutex> map_lock(m_sample_maps_mutex, std::defer_lock);
    while ( !map_lock.try_lock() ){}
    std::unordered_map<size_t, Eigen::Vector2d>::iterator curr_loc_itr = m_sample_points_unvisited.find(grid_index);
    map_lock.unlock();

    return ( curr_loc_itr != m_sample_points_unvisited.end() );
}

//---------------------------------------------------------
// Procedure: getIndexForMap
//            calculate the grid index for the vehicle location
//
int GP::getIndexForMap(double veh_lon, double veh_lat)
{
  if ( inSampleRectangle(veh_lon, veh_lat, true) )
  {
    // calculate the id of the location where the vehicle is currently
    // at, and move it to visited
    size_t x_cell_rnd = (size_t) round((veh_lon - m_min_lon)/m_lon_spacing);
    size_t y_cell_rnd = (size_t) round((veh_lat - m_min_lat)/m_lat_spacing);

    // calculate index into map (stored from SW, y first, then x)
    int index = m_y_resolution*x_cell_rnd + y_cell_rnd;

    return index;
  }
  return -1;
}

//---------------------------------------------------------
// Procedure: updateVisitedSet
//            given current vehicle location, move locations
//            from unvisited to visited set
//
void GP::updateVisitedSet(double veh_lon, double veh_lat, size_t index )
{
  // add mutex for changing of global maps
  std::unique_lock<std::mutex> map_lock(m_sample_maps_mutex, std::defer_lock);
  while ( !map_lock.try_lock() ){}

  std::unordered_map<size_t, Eigen::Vector2d>::iterator curr_loc_itr = m_sample_points_unvisited.find(index);
  if ( curr_loc_itr != m_sample_points_unvisited.end() )
  {
    // remove point from unvisited set
    Eigen::Vector2d move_pt = curr_loc_itr->second;

    // check if the sampled point was nearby, if not, there's something wrong
    checkDistanceToSampledPoint(veh_lon, veh_lat, move_pt);

    // add the point to the visited set
    m_sample_points_visited.insert(std::pair<size_t, Eigen::Vector2d>(index, move_pt));

    // and remove it from the unvisited set
    m_sample_points_unvisited.erase(curr_loc_itr);
    // if using voronoi region, remove from that set, if it is in there
    if ( m_use_voronoi )
    {
      auto v_itr = m_voronoi_region.find(index);
      if ( v_itr != m_voronoi_region.end() )
        m_voronoi_region.erase(v_itr);
    }

    // report
    std::cout << "\nmoved pt: " << std::setprecision(10) << move_pt(0) << ", " << move_pt(1);
    std::cout << " from unvisited to visited.\n";
    std::cout << "Unvisited size: " << m_sample_points_unvisited.size() << '\n';
    std::cout << "Visited size: " << m_sample_points_visited.size() << '\n' << std::endl;
  }

  map_lock.unlock();
}

//---------------------------------------------------------
// Procedure: checkDistanceToSampledPoint
//            we check if, after conversion, the distance
//            of sampled point to vehicle location is reasonable
//
void GP::checkDistanceToSampledPoint(double veh_lon, double veh_lat, Eigen::Vector2d move_pt)
{
  double dist_lon = std::abs(move_pt(0) - veh_lon);
  double dist_lat = std::abs(move_pt(1) - veh_lat);
  double dist_lon_m = dist_lon*m_lon_deg_to_m;
  double dist_lat_m = dist_lat*m_lat_deg_to_m;

  if ( dist_lon_m > m_pts_grid_spacing || dist_lat_m > m_pts_grid_spacing )
  {
    std::cout << GetAppName() << " :: ERROR: distance to chosen sample point is bigger than the grid spacing\n";
    std::cout << GetAppName() << " :: Distance to chosen point: " << dist_lon_m << " " << dist_lat_m << '\n';
    std::cout << GetAppName() << " :: Quitting in 2 seconds." << std::endl;
    sleep(2);
    RequestQuit();
  }
}

//---------------------------------------------------------
// Procedure: findNextSampleLocation
//            find next sample location, using mutual information
//
void GP::findNextSampleLocation()
{
  std::cout << "find nxt sample loc" << std::endl;

  if ( checkGPHasData() )
  {
    // for each y (from unvisited set only, as in greedy algorithm Krause'08)
    // calculate the mutual information term
    if ( m_sample_points_unvisited.size() > 0 )
    {
      m_finding_nxt = true;
      // use threading because this is a costly operation that would otherwise
      // interfere with the MOOS app rate
      if ( m_use_MI )
      {
        // get covariance function from GP
        // such that we can use the get() function from the CovarianceFunction
        libgp::CovarianceFunction & cov_f = m_gp.covf();
        m_future_next_pt = std::async(std::launch::async, &GP::calcMICriterion, this, std::ref(cov_f));
      }
      else
        m_future_next_pt = std::async(std::launch::async, &GP::calcMECriterion, this);
    }
  }
}

//---------------------------------------------------------
// Procedure: publishNextBestPosition
//            call Notify & publish location
//
void GP::publishNextBestPosition() //Eigen::Vector2d best_so_far_y)
{
  Eigen::Vector2d best_so_far_y = m_future_next_pt.get();

  // app feedback
  std::cout << GetAppName() << " :: publishing " << m_output_var_pred << '\n';
  std::cout << GetAppName() << " :: current best next y: " << std::setprecision(15) << best_so_far_y(0) << ", " << best_so_far_y(1) << '\n';

  std::ostringstream output_stream;
  output_stream << std::setprecision(15) << best_so_far_y(0) << "," << best_so_far_y(1);
  m_Comms.Notify(m_output_var_pred, output_stream.str());

  // update state vars
  m_last_published = MOOSTime();
  m_need_nxt_wpt = false;
}

//---------------------------------------------------------
// Procedure: calcMECriterion
//            calculate maximum entropy
//            for every unvisited location,
//            and pick best (greedy)
//
Eigen::Vector2d GP::calcMECriterion()
{
  std::cout << "max entropy start" << std::endl;

  std::clock_t begin = std::clock();

  Eigen::Vector2d best_so_far_y(2);
  double best_so_far = -1*std::numeric_limits<double>::max();
  std::unordered_map<size_t, Eigen::Vector2d>::iterator y_itr;

  std::cout << "try for lock gp" << std::endl;

  // lock for access to m_gp
  std::unique_lock<std::mutex> gp_lock(m_gp_mutex, std::defer_lock);
  // use unique_lock here, such that we can release mutex after m_gp operation
  while ( !gp_lock.try_lock() ) {}
  std::cout << "make copy GP" << std::endl;
  libgp::GaussianProcess gp_copy(m_gp);
  // release lock
  gp_lock.unlock();

  std::cout << "try for lock map" << std::endl;
  std::unique_lock<std::mutex> map_lock(m_sample_maps_mutex, std::defer_lock);
  while ( !map_lock.try_lock() ){}
  // make copy of map to use instead of map,
  // such that we do not have to lock it for long
  std::unordered_map<size_t, Eigen::Vector2d> unvisited_map_copy;
  if ( m_use_voronoi && m_voronoi_region.size() > 0 )
    unvisited_map_copy.insert(m_voronoi_region.begin(), m_voronoi_region.end());
  else
    unvisited_map_copy.insert(m_sample_points_unvisited.begin(), m_sample_points_unvisited.end());
  map_lock.unlock();

  std::cout << "calc max entropy" << std::endl;

  // for each unvisited location
  for ( y_itr = unvisited_map_copy.begin(); y_itr != unvisited_map_copy.end(); y_itr++ )
  {
    // get unvisited location
    Eigen::Vector2d y = y_itr->second;
    double y_loc[2] = {y(0), y(1)};

    // calculate its posterior entropy
    double pred_mean = gp_copy.f(y_loc);
    double pred_cov = gp_copy.var(y_loc);

    // normal distribution
    //  1/2 ln ( 2*pi*e*sigma^2 )
    double post_entropy;
    if ( !m_use_log_gp )
      post_entropy = log( 2 * M_PI * exp(1) * pred_cov);
    else
    {
      // lognormal distribution
      double var_part = (1/2.0) * log( 2 * M_PI * exp(1) * pred_cov);
      post_entropy = var_part + pred_mean;
    }

    if ( post_entropy > best_so_far )
    {
      best_so_far = post_entropy;
      best_so_far_y = y;
    }
  }

  std::clock_t end = std::clock();
  std::cout << "Max Entropy calc time: " << ( (double(end-begin) / CLOCKS_PER_SEC) ) << std::endl;

  // copy of GP and unvisited_map get destroyed when this function exits
  return best_so_far_y;
}

//---------------------------------------------------------
// Procedure: calcMICriterion
//            do the MI criterion calculation and find best y
//
Eigen::Vector2d GP::calcMICriterion(libgp::CovarianceFunction& cov_f)
{
  // for mutual information, we use visited and unvisited sets
  // we want to calculate, for each possible location y,
  // the values of k(y,y), k(y,a), k(a,a), k(y,a*), k(a*,a*)
  // i.e. we calculate the covariance with all the points in the visite
  // and unvisited sets
  // then we calculate sigma^2 (kyy - kya kaa kay) for each set
  // and divide sigma_y_visited by sigma_y_unvisited

  // calculate covariance matrices sets, and their inverses (costly operations)
  std::cout << "Calculate covariance matrices" << std::endl;

  std::unique_lock<std::mutex> mi_lock(m_gp_mutex, std::defer_lock);
  // use unique_lock here, such that we can release mutex after m_gp operation
  while ( !mi_lock.try_lock() ) {}

  size_t size_unvisited = m_sample_points_unvisited.size();

  // note; replaced with using predictions from GP
//    Eigen::MatrixXd K_aa(size_visited, size_visited);
//    createCovarMatrix(cov_f, "visited", K_aa);
//    Eigen::MatrixXd K_aa_inv = K_aa.inverse();

  Eigen::MatrixXd K_avav(size_unvisited, size_unvisited);
  createCovarMatrix(cov_f, "unvisited", K_avav);
  Eigen::MatrixXd K_avav_inv = K_avav.inverse();

  // TODO replace inverses using Cholesky decomposition

  // construct a vector of target values for the unvisited locations
  // using the GP
  std::cout << "Get target values for unvisited points" << std::endl;
  Eigen::VectorXd t_av(size_unvisited);
  getTgtValUnvisited(t_av);

  double best_so_far = -1*std::numeric_limits<double>::max();
  Eigen::Vector2d best_so_far_y(2);
  std::unordered_map<size_t, Eigen::Vector2d>::iterator y_itr;
  size_t best_cnt = 1;

  std::cout << "try for lock map" << std::endl;
  std::unique_lock<std::mutex> map_lock(m_sample_maps_mutex, std::defer_lock);
  while ( !map_lock.try_lock() ){}
  // make copy of map to use instead of map,
  // such that we do not have to lock it for long
  std::unordered_map<size_t, Eigen::Vector2d> unvisited_map_copy;
  if ( m_use_voronoi && m_voronoi_region.size() > 0 )
    unvisited_map_copy.insert(m_voronoi_region.begin(), m_voronoi_region.end());
  else
    unvisited_map_copy.insert(m_sample_points_unvisited.begin(), m_sample_points_unvisited.end());
  map_lock.unlock();

  std::cout << "calc for all y (" << unvisited_map_copy.size() << ")" << std::endl;

  std::clock_t begin = std::clock();
  for ( y_itr = unvisited_map_copy.begin(); y_itr != unvisited_map_copy.end(); y_itr++ )
  {
    Eigen::Vector2d y = y_itr->second;

    // calculate k(y,y)
    double k_yy = cov_f.get(y,y);

    // calculate covariance with visited set
//      Eigen::VectorXd k_ya(size_visited);
//      createCovarVector(cov_f, y, "visited", k_ya);
//      // TODO: add noise term?
//      double mat_ops_result = k_ya.transpose() * K_aa_inv * k_ya;
//      double sigma_y_A = k_yy - mat_ops_result;

    // covariance with visited set should be the predictive covariance
    // from the GP (note, GP actually contains more data pts .. but at least
    //              the visited locations)
    double y_loc[2] = {y(0), y(1)};
    double pred_mean_yA = m_gp.f(y_loc);
    double pred_cov_yA = m_gp.var(y_loc);

    // TODO if want to work with voronoi, then change these functions internally
    // calculate covariance with unvisited set
    Eigen::VectorXd k_yav(size_unvisited);
    createCovarVector(cov_f, y, "unvisited", k_yav);
    // TODO add noise term in both equations! TODO TODO
    double mat_ops_result = k_yav.transpose() * K_avav_inv * k_yav;
    double sigma_y_Av = k_yy - mat_ops_result;

    // predictive mean of unvisited set -- TODO check?
    double pred_mean_yAv = k_yav.transpose() * K_avav_inv * t_av;

    // calculate mutual information term
    double div;
    if ( m_use_log_gp )
    {
      // convert to log GP
      double mean_yA_lGP, var_yA_lGP; // visited set
      logGPfromGP(pred_mean_yA, pred_cov_yA, mean_yA_lGP, var_yA_lGP);
      double mean_yAv_lGP, var_yAv_lGP; // unvisited set
      logGPfromGP(pred_mean_yAv, sigma_y_Av, mean_yAv_lGP, var_yAv_lGP);

      // TODO change for log GP, incorp mean, current incorrect (is for GP)
      div = 0.5 * log( var_yA_lGP / var_yAv_lGP );
    }
    else
    {
      // double div = 0.5 * log(sigma_y_A / sigma_y_Av);
      div = 0.5 * log ( pred_cov_yA / sigma_y_Av );
    }

    // store max (greedy best)
    if ( div > best_so_far )
    {
      best_so_far = div;
      best_so_far_y = y;
      best_cnt++;
    }
  }
  std::clock_t end = std::clock();
  std::cout << "MI crit calc time: " << ( (double(end-begin) / CLOCKS_PER_SEC) ) << std::endl;

  std::cout << GetAppName() << " :: best_cnt: " << best_cnt << std::endl;

  mi_lock.unlock();

  // TODO: store all values, return sorted list?
  return best_so_far_y;
}

//---------------------------------------------------------
// Procedure: getTgtValUnvisited
//            construct vector with target values for
//            unvisited points, as extracted from GP
//
void GP::getTgtValUnvisited(Eigen::VectorXd & t_av)
{
  std::unordered_map<size_t, Eigen::Vector2d>::iterator av_itr;
  size_t t_cnt = 0;

  for ( av_itr = m_sample_points_unvisited.begin(); av_itr != m_sample_points_unvisited.end(); av_itr++, t_cnt++ )
  {
    double av_loc[2] = {av_itr->second(0), av_itr->second(1)};
    t_av(t_cnt) = m_gp.f(av_loc);
  }
}

//---------------------------------------------------------
// Procedure: checkGPHasData
//            check that the GP has been filled, else quit
//
bool GP::checkGPHasData()
{
  return ( m_gp.get_sampleset_size() > 0 );
}

//---------------------------------------------------------
// Procedure: createCovarVector
//            helper method for mutual information calculation
//            calculates K_ya
//
void GP::createCovarVector(libgp::CovarianceFunction& cov_f, Eigen::Vector2d y, std::string const & set_identifier, Eigen::VectorXd & k_ya)
{
  // choose which map to use
  std::unordered_map<size_t, Eigen::Vector2d> & map_ref = ( set_identifier == "visited" ? m_sample_points_visited : m_sample_points_unvisited);

  // calculate the covariances
  std::unordered_map<size_t, Eigen::Vector2d>::iterator a_itr;
  size_t a_cnt = 0;
  for ( a_itr = map_ref.begin(); a_itr != map_ref.end(); a_itr++, a_cnt++)
  {
    // calc k_ya
    Eigen::Vector2d a = a_itr->second;

    k_ya(a_cnt) = cov_f.get(y,a);
  }
}

//---------------------------------------------------------
// Procedure: createCovarMatrix
//            helper method for mutual information calculation
//            calculates K_AA
//
void GP::createCovarMatrix(libgp::CovarianceFunction& cov_f, std::string const & set_identifier, Eigen::MatrixXd & K_aa)
{
  // choose which map to use
  std::unordered_map<size_t, Eigen::Vector2d> & map_ref = ( set_identifier == "visited" ? m_sample_points_visited : m_sample_points_unvisited);

  std::unordered_map<size_t, Eigen::Vector2d>::iterator a_itr;
  size_t a_cnt = 0;
  for ( a_itr = map_ref.begin(); a_itr != map_ref.end(); a_itr++, a_cnt++)
  {
    Eigen::Vector2d a = a_itr->second;

    // calc K_aa
    std::unordered_map<size_t, Eigen::Vector2d>::iterator b_itr;
    size_t b_cnt = a_cnt;
    // iterate only over triangle of matrix to reduce computation
    for ( b_itr = a_itr; b_itr != map_ref.end(); b_itr++, b_cnt++ )
    {
      Eigen::Vector2d b = b_itr->second;

      double current_cov_val = cov_f.get(a, b);
      K_aa(a_cnt, b_cnt) = current_cov_val;
      if (a_cnt != b_cnt)
        K_aa(b_cnt, a_cnt) = current_cov_val;
    }
  }
}

//---------------------------------------------------------
// Procedure: processReceivedData()
//            pass on received data points to be added to GP
//            via handleMailReceivedDataPts
//
size_t GP::processReceivedData()
{
  size_t pts_added = 0;
  while ( !m_incoming_data_to_be_added.empty() )
  {
    std::string data = m_incoming_data_to_be_added.back();
    m_incoming_data_to_be_added.pop_back();
    pts_added += handleMailReceivedDataPts(data);
  }
  return pts_added;
}


//---------------------------------------------------------
// Procedure: runHPOptimization()
//            run in thread, call GP's hyperparam optimization
//
bool GP::runHPOptimization(libgp::GaussianProcess & gp, size_t nr_iterations)
{
  bool data_processed = false;
  size_t pts_added = 0;

  // first share and wait for data,
  // in case there are multiple vehicles
  // and data has not yet been shared through acomms
  if ( m_num_vehicles > 1 )
  {
    // 1. wait until the vehicle is at the surface and at the HP loiter
    while ( !(m_dep < 0.1 && m_loiter_dist_to_poly < 20) )
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // handshake

    // if received ready, let it be known we are ready as well
    if ( m_received_ready )
      sendReady();

    // if not received ready, let it be known we are ready, until other also is
    size_t prev_sent = 0;
    while ( !m_received_ready )
    {
      // this vehicle ready to exchange data, other vehicle not yet,
      // keep sending that we are ready:
      // every 30 seconds, notify that we are ready for data exchange
      // note; we are not in a mail cycle here, so make sure we do only once
      //       per time slot
      size_t time_moos = (size_t)std::floor(MOOSTime());
      if ( time_moos % 30 == 0 && time_moos-prev_sent > 1 )
      {
        sendReady();
        prev_sent = time_moos;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }

    // share data, if not already shared through acomms
    if ( !m_acomms_sharing )
    {
      sendData();
      m_received_ready = false;

      // 2. wait for received data to be processed
      while ( !data_processed )
      {
        if ( m_received_shared_data )
        {
          pts_added += processReceivedData();
          data_processed = true;
        }
        else
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      std::cout << "*added: " << pts_added << " data points." << std::endl;
      m_received_shared_data = false;
    }
  }

  // 3. run hyperparameter optimization

  // protect GP access with mutex
  std::unique_lock<std::mutex> hp_lock(m_gp_mutex, std::defer_lock);
  while ( !hp_lock.try_lock() ){}
  std::cout << "obtained lock, continuing HP optimization" << std::endl;
  std::cout << "current size GP: " << gp.get_sampleset_size() << std::endl;

  std::clock_t begin = std::clock();

  // optimization
  // there are 2 methods in gplib, conjugate gradient and RProp, the latter
  // should be more efficient
  libgp::RProp rprop;
  rprop.init();

  // RProp arguments: GP, 'n' (nr iterations), verbose
  rprop.maximize(&gp, nr_iterations, 0);

  std::clock_t end = std::clock();
  std::cout << "runtime hyperparam optimization: " << ( (double(end-begin) / CLOCKS_PER_SEC) ) << std::endl;

  // write new HP to file
  begin = std::clock();
  std::stringstream filenm;
  filenm << "hp_optim_" << m_veh_name << "_" << nr_iterations;
  gp.write(filenm.str().c_str());
  end = std::clock();
  std::cout << "HP param write to file time: " <<  ( (double(end-begin) / CLOCKS_PER_SEC) ) << std::endl;

  hp_lock.unlock();

  // first save of predictions
  std::thread pred_store(&GP::makeAndStorePredictions, this);
  pred_store.detach();
  m_last_pred_save = MOOSTime();

  return true;
}

//---------------------------------------------------------
// Procedure: sendReady()
//            notify other vehicles that this one is ready
//            for data exchange
//
void GP::sendReady()
{
  Notify(m_output_var_handshake_data_sharing,m_veh_name);
}

//---------------------------------------------------------
// Procedure: sendData()
//            send data in chunks (via pShare)
//
void GP::sendData()
{
  // we need to chunk the data because pShare has a limit of 64K per message
  // 2000 points should be ca. 53K, so let's try that first
  // (should be ca. the amount to send at first HP optimization point)
  // note; 2000 seemed to stretch it, some complaint of 48kB,
  // let's do 1500, should be conservative enough
  size_t msg_cnt = 0;
  size_t nr_points = 1500;
  std::cout << "**sending " << m_data_pt_counter << " points!" << std::endl;
  if ( m_data_pt_counter == 0 )
  {
    // no data points to send, send empty msg
    std::string empty_msg = m_veh_name + ':';
    m_Comms.Notify(m_output_var_share_data,empty_msg);
  }
  while ( m_data_pt_counter != 0 )
  {
    if ( m_data_pt_counter < 1500 )
      nr_points = m_data_pt_counter;

    // get data chunk
    std::ostringstream data_string_stream;

    // add vehicle name, because broadcast is also received by sending vehicle
    data_string_stream << m_veh_name << ":";

    std::copy(m_data_to_send.begin()+(msg_cnt*1500), m_data_to_send.begin()+(msg_cnt*1500+nr_points), std::ostream_iterator<std::string>(data_string_stream,";"));

    // send msg
    m_Comms.Notify(m_output_var_share_data,data_string_stream.str());

    msg_cnt++;
    m_data_pt_counter -= nr_points;
  }

  // remove data from vector
  m_data_to_send.clear();
  // reset counter
  m_data_pt_counter = 0;
}


//---------------------------------------------------------
// Procedure: logGPfromGP
//            convert pred mean/var from GP to pred mean/var for log GP
//
void GP::logGPfromGP(double gp_mean, double gp_cov, double & lgp_mean, double & lgp_cov )
{
  // convert GP mean to log GP mean (lognormal mean)
  lgp_mean = exp(gp_mean + gp_cov/2.0);
  lgp_cov = (lgp_mean*lgp_mean) * (exp(gp_cov) - 1.0);
}

//---------------------------------------------------------
// Procedure: makeAndStorePredictions
//            file writing
//
void GP::makeAndStorePredictions()
{
  // make a copy of the GP and use that below, to limit lock time
  std::unique_lock<std::mutex> gp_lock(m_gp_mutex, std::defer_lock);
  while ( !gp_lock.try_lock() ) {}
  std::cout << "store predictions" << std::endl;
  libgp::GaussianProcess gp_copy(m_gp);
  gp_lock.unlock();

  std::clock_t begin = std::clock();

  std::vector< std::pair<double, double> >::iterator loc_itr;
  // get the predictive mean and var values for all sample locations
  std::vector<double> all_pred_means;
  std::vector<double> all_pred_vars;
  std::vector<double> all_pred_mu;
  std::vector<double> all_pred_sigma2;
  // pre-alloc vectors
  size_t nr_sample_locations = m_sample_locations.size();
  all_pred_means.reserve(nr_sample_locations);
  all_pred_vars.reserve(nr_sample_locations);
  all_pred_mu.reserve(nr_sample_locations);
  all_pred_sigma2.reserve(nr_sample_locations);

  // make predictions for all sample locations
  for ( loc_itr = m_sample_locations.begin(); loc_itr < m_sample_locations.end(); loc_itr++ )
  {
    double loc[2] {loc_itr->first, loc_itr->second};
    double pred_mean;
    double pred_var;
    gp_copy.f_and_var(loc, pred_mean, pred_var);

    double pred_mean_lGP, pred_var_lGP;
    if ( m_use_log_gp )
    {
      // params lognormal distr
      // convert to lGP mean and variance
      // to get back in the 'correct' scale for comparison to generated data
      logGPfromGP(pred_mean, pred_var, pred_mean_lGP, pred_var_lGP);
    }

    if ( m_use_log_gp )
    {
      all_pred_means.push_back(pred_mean_lGP);
      all_pred_vars.push_back(pred_var_lGP);
      all_pred_mu.push_back(pred_mean);
      all_pred_sigma2.push_back(pred_var);
    }
    else
    {
      all_pred_means.push_back(pred_mean);
      all_pred_vars.push_back(pred_var);
    }
  }
  std::clock_t end = std::clock();
  std::cout << "runtime make predictions: " << ( (double(end-begin) / CLOCKS_PER_SEC) ) << std::endl;

  begin = std::clock();
  // grab file writing mutex
  std::unique_lock<std::mutex> file_write_lock(m_file_writing_mutex, std::defer_lock);
  while ( !file_write_lock.try_lock() ){}
  // write to file
  for ( size_t vector_idx = 0; vector_idx < nr_sample_locations; ++vector_idx )
  {
    // save to file, storing predictions
    if ( vector_idx > 0 )
    {
      m_ofstream_pm << ", ";
      m_ofstream_pv << ", ";
      if ( m_use_log_gp )
      {
        m_ofstream_pmu << ", ";
        m_ofstream_psigma2 << ", ";
      }
    }
    m_ofstream_pm << all_pred_means[vector_idx];
    m_ofstream_pv << all_pred_vars[vector_idx];
    if ( m_use_log_gp )
    {
      m_ofstream_pmu << all_pred_mu[vector_idx];
      m_ofstream_psigma2 << all_pred_sigma2[vector_idx];
    }
  }
  m_ofstream_pm << '\n';
  m_ofstream_pv << '\n';
  if ( m_use_log_gp )
  {
    m_ofstream_pmu << '\n';
    m_ofstream_psigma2 << '\n';
  }

  if ( m_finished )
  {
    m_ofstream_pm.close();
    m_ofstream_pv.close();
    if ( m_use_log_gp )
    {
      m_ofstream_pmu.close();
      m_ofstream_psigma2.close();
    }
  }

  file_write_lock.unlock();

  end = std::clock();
  std::cout << "runtime save to file: " << ( (double(end-begin) / CLOCKS_PER_SEC) ) << std::endl;

  std::cout << "done saving predictions to file" << std::endl;

  // copy of GP gets destroyed when this function exits
}

//---------------------------------------------------------
// Procedure: calcLonLatSpacing
//            separate out these calculations
//            such that we only do them once
//
void GP::calcLonLatSpacing()
{
  // convert lane widths in meters to lon/lat
  m_lon_spacing = m_pts_grid_spacing/m_lon_deg_to_m;
  m_lat_spacing = m_pts_grid_spacing/m_lat_deg_to_m;

  // calculate the amount of y vertices
  m_y_resolution = (size_t) round(m_pts_grid_height/m_pts_grid_spacing);
  // add one because of zero indexing
  m_y_resolution++;
}

//---------------------------------------------------------
// Procedure: convLonLatToUTM
//            use MOOS Geodesy to convert lon/lat to x/y
//
bool GP::convLonLatToUTM (double lon, double lat, double & lx, double & ly )
{
  bool successful = m_geodesy.LatLong2LocalUTM(lat, lon, ly, lx);

  if ( !successful )
    std::cout << GetAppName() << " :: ERROR converting lon/lat to x/y.\n";

  return successful;
}

//---------------------------------------------------------
// Procedure: convUTMToLonLat
//            use MOOS Geodesy to convert x/y to lon/lat
//
bool GP::convUTMToLonLat (double lx, double ly, double & lon, double & lat )
{
  bool successful = m_geodesy.UTM2LatLong(lx, ly, lat, lon);

  if ( !successful )
    std::cout << GetAppName() << " :: ERROR converting x/y to lon/lat.\n";

  return successful;
}

//---------------------------------------------------------
// Procedure: calcVoronoi
//            given all other vehicles (stored in map),
//            find the points within voronoi region,
//            over the unvisited set
//
void GP::calcVoronoi()
{
  // clear out previous set
  m_voronoi_region.clear();

  double own_lon = m_lon;
  double own_lat = m_lat;
  std::cout << "\nown vehicle at: " << own_lon << "," << own_lat << '\n';

  // if own vehicle is not in sample area,
  // we set Voronoi region to be whole area
  // this should only happen at the start of the adaptive sampling
  if ( !inSampleRectangle(own_lon, own_lat, true) )
    m_voronoi_region.insert(m_sample_points_unvisited.begin(), m_sample_points_unvisited.end());
  else
  {
    // else, split sample area, given other vehicles
    // for all points in the unvisited set
    for ( auto pt : m_sample_points_unvisited )
    {
      size_t pt_key = pt.first;
      Eigen::Vector2d pt_loc = pt.second;
      // calculate distance to all vehicles, including self,
      // and determine which is closest
      double min_dist = std::numeric_limits<double>::max();
      std::string closest_vehicle;
      if ( m_other_vehicles.size() > 0 )
      {
        for ( auto veh : m_other_vehicles )
        {
          double veh_lon = (veh.second).first;
          double veh_lat = (veh.second).second;
          if ( pt_key == m_sample_points_unvisited.begin()->first )
              std::cout << "other vehicle: " << veh.first << " at: " << veh_lon << "," << veh_lat << '\n';

          double dist_pt_to_veh = pow(pt_loc(0)-veh_lon,2) + pow(pt_loc(1)-veh_lat,2);
          if ( dist_pt_to_veh < min_dist )
          {
            min_dist = dist_pt_to_veh;
            closest_vehicle = veh.first;
          }
        }
      }
      // calculate distance to oneself
      double dist_to_self = pow(pt_loc(0)-own_lon, 2) + pow(pt_loc(1)-own_lat,2);
      if ( dist_to_self < min_dist )
      {
        closest_vehicle = m_veh_name;
        // only in this case do we add the location to our Voronoi set
        m_voronoi_region.insert(std::pair<size_t, Eigen::Vector2d>(pt_key,pt_loc));
      }
//      else
//        std::cout << "point should be for vehicle: " << closest_vehicle << std::endl;
    }
  }
  std::cout << "my set has: " << m_voronoi_region.size() << " sample locations out of " << m_sample_points_unvisited.size() << std::endl;

  // calculate the convex hull
  voronoiConvexHull();

  // print convex hull
  printVoronoi();
}

//---------------------------------------------------------
// Procedure: inSampleRectangle(double veh_lon, double veh_lat, bool use_buffer) const
//            see if the current vehicle position is inside the
//            prespecified sampling area (rectangle)
//
bool GP::inSampleRectangle(double veh_lon, double veh_lat, bool use_buffer) const
{
  // if just outside data grid, but close enough,
  // should be able to map to border points
  // buffer of 5 meters (TODO make parameter?)
  double buffer_lon = 5.0/m_lon_deg_to_m;
  double buffer_lat = 5.0/m_lat_deg_to_m;

  if ( use_buffer )
    return ( veh_lon >= (m_min_lon-buffer_lon) && veh_lat >= (m_min_lat-buffer_lat) &&
             veh_lon <= (m_max_lon+buffer_lon) && veh_lat <= (m_max_lat+buffer_lat) );
  else
    return ( veh_lon >= m_min_lon && veh_lat >= m_min_lat &&
             veh_lon <= m_max_lon && veh_lat <= m_max_lat );
}

//---------------------------------------------------------
// Procedure: voronoiConvexHull()
//            get the convex hull of the Voronoi region
//
void GP::voronoiConvexHull()
{
  // collect points inside voronoi region in boost geometry multi_point
  boost::geometry::clear(m_voronoi_pts);
  for ( auto vor_pt : m_voronoi_region )
  {
    auto vor_loc = vor_pt.second;
    double pt_lon = vor_loc(0);
    double pt_lat = vor_loc(1);
    boost::geometry::append(m_voronoi_pts, boost_pt(pt_lon, pt_lat));
  }
  std::cout << "nr points in voronoi region: " << m_voronoi_region.size() << std::endl;
  std::cout << "nr points in multipoint: " << (size_t)boost::geometry::num_points(m_voronoi_pts) << std::endl;

  // next, get the convex hull for these points
  m_voronoi_conv_hull.clear();
  boost::geometry::convex_hull(m_voronoi_pts, m_voronoi_conv_hull);
  std::cout << "convex hull pts: " << (size_t)boost::geometry::num_points(m_voronoi_conv_hull) << std::endl;
}

//---------------------------------------------------------
// Procedure: inVoronoi(double lon, double lat) const
//            check if given location is inside convex hull of Voronoi region
//
bool GP::inVoronoi(double lon, double lat) const
{
  boost_pt pt_to_check(lon, lat);
  return boost::geometry::intersects(pt_to_check, m_voronoi_conv_hull);
}

//---------------------------------------------------------
// Procedure: distToVoronoi(double lon, double lat) const
//            calculate distance vehicle location to boundaries voronoi convex hull
//
double GP::distToVoronoi(double lon, double lat) const
{
  boost_pt pt_to_check(lon, lat);

  // iterate over vertices in convex hull to create lines to calc distance to
  double min_dist = std::numeric_limits<double>::max();
  auto bst_ext_ring = boost::geometry::exterior_ring(m_voronoi_conv_hull);
  for ( auto itr = boost::begin(bst_ext_ring); itr != boost::end(bst_ext_ring); ++itr )
  {
    // line between current and next point
    boost::geometry::model::linestring<boost_pt> line;
    if ( itr+1 != boost::end(bst_ext_ring) )
    {
      boost_pt bpt = *itr;
      boost_pt nxt = *(itr+1);
      boost::geometry::append(line, bpt);
      boost::geometry::append(line, nxt);
    }
    // calculate distance to line
    if ( boost::geometry::num_points(line) > 0 )
    {
      double curr_dist = boost::geometry::distance(pt_to_check, line);

      // store if less than current min
      if ( curr_dist < min_dist )
        min_dist = curr_dist;
    }
  }

  return min_dist;
}

//---------------------------------------------------------
// Procedure: printVoronoiConvexHull()
//            print voronoi convex hull vertices to cout and MOOS
//
void GP::printVoronoi()
{
  auto bst_ext_ring = boost::geometry::exterior_ring(m_voronoi_conv_hull);
  std::ostringstream voronoi_str;

  std::cout << "voronoi convex hull: " << boost::geometry::dsv(bst_ext_ring) << std::endl;

  for ( auto itr = boost::begin(bst_ext_ring); itr != boost::end(bst_ext_ring); ++itr )
  {
    boost_pt bpt = *itr;
    voronoi_str << bpt.get<0>() << "," << bpt.get<1>() << ";";
  }

  m_Comms.Notify("VORONOI_REGION",voronoi_str.str());
}


//---------------------------------------------------------
// Procedure: tdsHandshake
//            broadcast that vehicle is ready when on surface
//            and share data when received 'ready' from other vehicle
//            assumes only 2 vehicles
//
void GP::tdsHandshake()
{
  size_t moos_t = (size_t)std::floor(MOOSTime());
  // send data
  if ( m_received_ready && m_dep < 0.1 )
  {
    m_send_ack = false;
    // other vehicle already ready to exchange data
    // send that we are ready, when we are at the surface
    sendReady();
    m_last_ready_sent = moos_t;
    m_sending_data = true;
    sendData();
    // reset for next time
    m_received_ready = false;
  }
  else
  {
    if ( m_dep < 0.1 ) // send only when at surface
    {
      // this vehicle ready to exchange data, other vehicle not yet,
      // keep sending that we are ready:
      // every 10 seconds, notify that we are ready for data exchange
      if ( moos_t % 30 == 0 &&
           moos_t - m_last_ready_sent > 1 )
      {
        sendReady();
        m_last_ready_sent = moos_t;
      }
    }
  }
}

//---------------------------------------------------------
// Procedure: tdsReceiveData
//            start processing received data, and when all
//            are process, then switch state to continue with
//            the survey mission
//
void GP::tdsReceiveData()
{
  if ( !m_waiting )
  {
    // data received, need to add
    // TODO make sure we only kick this off once
    m_future_received_data_processed = std::async(std::launch::async, &GP::processReceivedData, this);
    m_waiting = true;
  }
  else
  {
    if ( m_future_received_data_processed.wait_for(std::chrono::microseconds(1)) == std::future_status::ready )
    {
      size_t pts_added = m_future_received_data_processed.get();
      std::cout << " added: " << pts_added << " data points" << std::endl;

      if ( m_use_voronoi )
      {
        calcVoronoi();
        m_last_voronoi_calc_time = MOOSTime();
      }

      tdsResetStateVars();

      Notify("STAGE","survey");
    }
    // else, continue waiting
  }
}


//---------------------------------------------------------
// Procedure: tdsResetStateVars
//            reset state vars for TDS control
//
void GP::tdsResetStateVars()
{
  std::cout << "reset state vars" << std::endl;
  // resets for next time
  m_data_sharing_activated = false;
  m_received_shared_data = false;
  m_sending_data = false;
  m_waiting = false;
  m_need_nxt_wpt = true;
  m_received_ready = false;
  m_data_sharing_requested = false;
}

//---------------------------------------------------------
// Procedure: findAndPublishNextWpt
//            called upon m_need_nxt_wpt in Iterate
//            calculate the next sample location,
//            and publish to MOOSDB when found
//
void GP::findAndPublishNextWpt()
{
  if ( !m_finding_nxt )
  {
    std::cout << "calling to find next sample location" << std::endl;
    findNextSampleLocation();
  }
  else
  {
    m_pause_data_adding = true;
    // see if we can get result from future
    if ( m_future_next_pt.wait_for(std::chrono::microseconds(1)) == std::future_status::ready )
    {
      m_finding_nxt = false;
      // publish greedy best
      publishNextBestPosition();
      m_pause_data_adding = false;
    }
  }
}

//---------------------------------------------------------
// Procedure: needToRecalculateVoronoi
//            see if we need to recalculate the voronoi region
//            for now, when we are close to voronoi border
//
bool GP::needToRecalculateVoronoi()
{
  // TODO change threshold?
  double voronoi_threshold = ((m_lon_spacing + m_lat_spacing)/2.0) / 2.0;
  //std::cout << "Voronoi threshold: " << voronoi_threshold << std::endl;
  double dist_to_voronoi = distToVoronoi(m_lon, m_lat);
  m_Comms.Notify("DIST_TO_VORONOI", dist_to_voronoi);
  if ( dist_to_voronoi < voronoi_threshold )
    return true;
  return false;
}

bool GP::ownMessage(std::string input)
{
  size_t index = input.find(m_veh_name);
  return ( index != std::string::npos );
}
