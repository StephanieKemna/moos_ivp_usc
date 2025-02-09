/*****************************************************************/
/*    NAME: Stephanie Kemna                                      */
/*    ORGN: Robotic Embedded Systems Lab, CS, USC, CA, USA       */
/*    FILE: GP.h                                                 */
/*    DATE: 2015 - 2018                                          */
/*                                                               */
/*****************************************************************/

#ifndef PGP_HEADER
#define PGP_HEADER

#include "MOOS/libMOOS/MOOSLib.h"

// lib GP
#include "gp.h"

// use unordered map rather than map, improve efficiency
#include <unordered_map>
// use unordered set for fast retrieval of keys in list
#include <unordered_set>

// multi-threading
#include <thread>
#include <future>

// std queue for FIFO queue
#include <queue>

// geodesy for conversion x/y to lon/lat
#include "MOOS/libMOOSGeodesy/MOOSGeodesy.h"

#ifdef BUILD_VORONOI
// boost geometry libraries for multipoint and convex hull
// requires boost 1.56 for multi_point
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/multi_point.hpp>
#endif

// include Eigen for OS X
#include <Eigen/Dense>

class GP : public CMOOSApp
{
 public:
   GP();
   ~GP();

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
   void initGeodesy();

   void handleMailDataFromSensor(double received_data);
   void handleMailSamplePoints(std::string input_string);
   void handleMailSamplePointsSpecs(std::string input_string);
   void handleMailNodeReports(std::string const & input_string);

   void updateVisitedSet(double veh_lon, double veh_lat, size_t index );

   void addPatternToGP(double veh_lon, double veh_lat, double value);
   void dataAddingThread();

   bool runHPOptimization(size_t nr_iterations);
   void runHPoptimizationOnDownsampledGP(Eigen::VectorXd & loghp, size_t nr_iterations);

   void findAndPublishNextWpt();
   void findNextSampleLocation();
   void getRandomStartLocation();

   // maximum entropy
   size_t calcMECriterion();
   void getLogGPPredMeanVarFromGPMeanVar(double gp_mean, double gp_cov, double & lgp_mean, double & lgp_cov);

   // path planning & passing on to behavior
   void greedyWptSelection(Eigen::Vector2d & best_location);
   void publishNextBestPosition();

   // timed saving of GP
   void makeAndStorePredictions(bool finished);

   // data sending surface
   void storeDataForSending(double vlon, double vlat, double data);
   void sendReady();
   void sendData();
   size_t handleMailReceivedDataPts(std::string incoming_data);

   // data sending acomms
   void handleMailDataAcomms(std::string css);

#ifdef BUILD_VORONOI
   // calculate Voronoi regions
   void calcVoronoi( double own_lon, double own_lat, std::map< std::string, std::pair<double,double> > other_centers );
   void voronoiConvexHull();
   bool needToRecalculateVoronoi();
   void calcVoronoiCentroids(double & own_centroid_lon, double & own_centroid_lat, std::map< std::string, std::pair<double,double> > & other_vehicle_centroids );
   void calcVoronoiPartitionCentroid( std::vector<size_t> voronoi_partition, double & centroid_lon, double & centroid_lat );
   void runVoronoiRoutine();
   bool centroidConvergence( double old_lon, double old_lat, std::map<std::string, std::pair<double, double> > old_centr,
                             double new_lon, double new_lat, std::map<std::string, std::pair<double, double> > new_centr );
#endif

   // helper/test functions
   int getIndexForMap(double veh_lon, double veh_lat);
   bool checkDistanceToSampledPoint(double veh_lon, double veh_lat, Eigen::Vector2d move_pt);
   bool checkGPHasData();
   void calcLonLatSpacing();
   bool convLonLatToUTM (double lon, double lat, double & lx, double & ly );
   bool convUTMToLonLat (double lx, double ly, double & lon, double & lat );
   bool inSampleRectangle(double veh_lon, double veh_lat, bool use_buffer) const;

   void clearTDSStateVars();
   void clearHandshakeVars();
   void tdsHandshake();
   void tdsReceiveData();

   size_t processReceivedData();

   bool ownMessage(std::string input);
   bool finalSurface(std::string input);

   void publishStates(std::string const calling_method);

   void endMission();

   void printCoutPrefix();

   double currentMOOSTime() const;

   // Configuration variables
   bool m_verbose;

   CMOOSGeodesy m_geodesy;

   std::string m_input_var_data;
   std::string m_input_var_sample_points;
   std::string m_input_var_sample_points_specs;
   std::string m_input_var_adaptive_trigger;
   std::string m_input_var_share_data;
   std::string m_output_var_pred;
   std::string m_output_filename_prefix;
   std::string m_output_var_share_data;

   size_t m_prediction_interval;
   size_t m_max_wait_for_other_vehicles;

   // State variables
   bool m_debug;
   std::string m_veh_name;
   bool m_use_log_gp;
   // vehicle location
   double m_lat;
   double m_lon;
   double m_dep;
   size_t m_surf_cnt;
   size_t m_at_depth_cnt;
   bool m_on_surface;

   bool m_adaptive;

   // process state
   double m_last_published;
   double m_last_pred_save;
   // sample points grid specs
   double m_min_lon;
   double m_min_lat;
   double m_max_lon;
   double m_max_lat;
   double m_pts_grid_width;
   double m_pts_grid_height;
   double m_pts_grid_spacing;
   double m_lon_spacing;
   double m_lat_spacing;
   double m_y_resolution;
   double m_lon_deg_to_m;
   double m_lat_deg_to_m;
   // mission status
   double m_start_time;
   bool m_finding_nxt;

   // states through enums
   enum MissionState{ STATE_IDLE, STATE_CALCWPT, STATE_SAMPLE,
                      STATE_HPOPTIM, STATE_DONE,
                      STATE_SURFACING, STATE_HANDSHAKE,
                      STATE_TX_DATA, STATE_RX_DATA,
                      STATE_CALCVOR, STATE_REQ_SURF, STATE_ACK_SURF };
   MissionState m_mission_state;
   char const * currentMissionStateString() { return (char const *[]) {
   "STATE_IDLE", "STATE_CALCWPT", "STATE_SAMPLE", "STATE_HPOPTIM", "STATE_DONE",
   "STATE_SURFACING", "STATE_HANDSHAKE", "STATE_TX_DATA", "STATE_RX_DATA",
   "STATE_CALCVOR", "STATE_REQ_SURF", "STATE_ACK_SURF" }[m_mission_state]; };

   std::unordered_map< size_t, Eigen::Vector2d > m_sample_points_unvisited;
   std::unordered_map< size_t, Eigen::Vector2d > m_sample_points_visited;
   std::vector< std::pair<double, double> > m_sample_locations;
   std::unordered_map<size_t, double> m_unvisited_pred_metric;

   // GP, and create mutex for protection of parts of code accessing m_gp
   libgp::GaussianProcess * m_gp;
   std::mutex m_gp_mutex;
   std::mutex m_sample_maps_mutex;

   // create queue for adding of points to GP
   std::queue< std::vector<double> > m_queue_data_points_for_gp;
   std::queue< std::vector<double> > m_data_for_hp_optim;

   // hyperparam optimization in multi-threading
   std::future<bool> m_future_hp_optim;
   bool m_hp_optim_running;
   bool m_final_hp_optim;
   size_t m_hp_optim_iterations;
   bool m_hp_optim_cg;
   double m_hp_dev_ratio;
   size_t m_last_hp_optim_done;

   // future for result MI criterion calculations
   std::future<size_t> m_future_next_pt;

   // to add only every other data point
   size_t m_data_mail_counter;

   // mission states,
   // check when returning to base
   bool m_finished;

   // file writing
   std::ofstream m_ofstream_pm_lGP, m_ofstream_pv_lGP;
   std::ofstream m_ofstream_pmu_GP, m_ofstream_psigma2_GP;

   // nr of vehicles (for determining data exchange)
   size_t m_num_vehicles;

   // vectors for storing data for sending
   std::vector< std::string > m_data_to_send;
   size_t m_data_pt_counter;
   size_t m_data_send_reserve;
   bool m_received_shared_data;
   std::future<size_t> m_future_received_data_pts_added;
   std::vector<std::string> m_incoming_data_to_be_added;
   double m_loiter_dist_to_poly;

   // check if data received
   bool m_data_received;

   // timed data sharing
   bool m_timed_data_sharing;
   size_t m_data_sharing_interval;
   std::future<size_t> m_future_received_data_processed;
   bool m_waiting;
   bool m_received_ready;
   std::string m_input_var_handshake_data_sharing;
   std::string m_output_var_handshake_data_sharing;
   size_t m_last_ready_sent;
   std::unordered_set<std::string> m_rec_ready_veh;
   size_t m_handshake_timer_counter;
   size_t m_tx_timer_counter;
   size_t m_rx_timer_counter;
   size_t m_req_surf_timer_counter;
   double m_last_surface;

   // acomms data sharing
   bool m_acomms_sharing;
   std::string m_last_acomms_string;

   // keep track of other vehicles' positions
   std::map< std::string, std::pair<double,double> > m_other_vehicles;

   // voronoi partitioning
   std::vector<size_t> m_voronoi_subset;
   std::map<std::string,std::vector<size_t>> m_voronoi_subset_other_vehicles;

   bool m_use_voronoi;
   bool m_running_voronoi_routine;
#ifdef BUILD_VORONOI
   typedef boost::geometry::model::point<double, 2, boost::geometry::cs::cartesian> boost_pt;
   typedef boost::geometry::model::multi_point< boost_pt > boost_multi_pt;
   boost_multi_pt m_voronoi_pts;
   boost::geometry::model::polygon<boost_pt> m_voronoi_conv_hull;

   bool inVoronoi ( double lon, double lat ) const;
   double distToVoronoi(double lon, double lat) const;
   void printVoronoi();
   void printVoronoiPartitions();
#endif

   // voronoi data sharing
   double m_last_voronoi_calc_time;
   std::unordered_set<std::string> m_rec_ack_veh;

   std::future<size_t> m_future_calc_prevoronoi;
   bool m_calc_prevoronoi;
   bool m_precalc_pred_voronoi_done;
   size_t m_vor_timeout;


   // downsampling data for HP optimization
   size_t m_downsample_factor;

   // do HP optim on first surface for adaptive
   bool m_first_surface;

   // to publish only once a second
   double m_last_published_req_surf;
   double m_last_published_req_surf_ack;
   size_t m_nr_ack_sent;

   // buffer around area for which vehicle is counted to be inside the area
   double m_area_buffer;

   // keep track of bhv state
   std::string m_bhv_state;
   std::string m_adp_state;

   // surface hub, track vehicle surfacing event
   std::map<std::string, size_t> m_map_vehicle_idx_data_last_sent;

   // surface hub addition: use a surface hub? (true, false)
   bool m_use_surface_hub;
   std::vector<std::string> m_final_sent_to;
   std::vector<std::string> m_final_received_from;
   bool m_veh_is_shub;
   bool m_cancel_hpo;

   double m_prev_length_scale;

   // choose method that is used for surfacing trigger in async surfacing scenario
   std::string m_async_trigger_method;
   double m_async_threshold;
   double m_async_prev_sum_var;
   bool m_async_prev_sum_var_reset;

   bool m_need_to_run_hpo;
   double m_survey_depth;
   double m_survey_speed;
   double m_dive_pitch_angle;
   double m_twoway_time_to_surf;
   double m_mission_duration;
   bool m_calcwpt_from_data_sharing;
};

#endif
