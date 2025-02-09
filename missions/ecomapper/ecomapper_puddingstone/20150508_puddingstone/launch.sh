#!/bin/bash 
#-------------------------------------------------------
#  Part 1: Check for and handle command-line arguments
#-------------------------------------------------------
TIME_WARP=1
JUST_MAKE="no"
SIMULATION_MODE="no"

for ARGI; do
    if [ "${ARGI}" = "--help" -o "${ARGI}" = "-h" ] ; then
        printf "%s [SWITCHES] [time_warp]   \n" $0
        printf "  --just_make, -j    \n" 
        printf "  --simulation, -s    \n" 
        printf "  --help, -h         \n" 
        exit 0;
    elif [ "${ARGI//[^0-9]/}" = "$ARGI" -a "$TIME_WARP" = 1 ]; then 
        TIME_WARP=$ARGI
    elif [ "${ARGI}" = "--just_build" -o "${ARGI}" = "-j" ] ; then
        JUST_MAKE="yes"
    elif [ "${ARGI}" = "--simulation" -o "${ARGI}" = "-s" ] ; then
        SIMULATION_MODE="yes"
    else 
        printf "Bad Argument: %s \n" $ARGI
        exit 0
    fi
done

#-------------------------------------------------------
#  Part 2: Create the .moos and .bhv files. 
#-------------------------------------------------------
# simulation set-up

if [ "${SIMULATION_MODE}" = "yes" ] ; then
  SERVERHOST_EM="localhost"
  SERVERHOST_SS="localhost"
fi
if [ "${SIMULATION_MODE}" = "no" ] ; then
  SERVERHOST_EM="192.168.10.11"
  SERVERHOST_SS="192.168.0.33"
fi
TIME_WARP=1

if [ "${SIMULATION_MODE}" = "yes" ] ; then
  # create shoreside.moos
  nsplug shoreside.meta shoreside.moos -f WARP=$TIME_WARP \
     VNAME="shoreside" USC_DATA_DIR="$MOOSIVP_USC_HOME/data"        \
     SHARE_LISTEN="9300" VPORT="9000" SERVER_HOST=$SERVERHOST_SS       \
     SERVER_HOST_EM=$SERVERHOST_EM
fi

# create ecomapper.moos
VNAME1="zoomer"        # The first  vehicle community
START_DEPTH1="0"
START_POS1="440,950"
START_HEADING1="0"
WAYPOINTS1="440,950:440,1000:440,1030:400,1015:400,975:440,950"
MODEMID1="1"
VTYPE1="UUV" # UUV, SHIP
nsplug ecomapper.meta ecomapper.moos -f WARP=$TIME_WARP  \
   VNAME=$VNAME1  START_POS=$START_POS1  START_HDG=$START_HEADING1 \
   VPORT="9001"       SHARE_LISTEN="9301"                      \
   VTYPE=$VTYPE1      MODEMID=$MODEMID1                        \
   SERVER_HOST=$SERVERHOST_EM SERVER_HOST_SS=$SERVERHOST_SS    \
   SIMULATION=$SIMULATION_MODE
nsplug ecomapper_bhv.meta ecomapper.bhv -f VNAME=$VNAME1      \
    START_POS=$START_POS1 WAYPOINTS=$WAYPOINTS1                \
    START_DEPTH=$START_DEPTH1 VTYPE=$VTYPE1

if [ ${JUST_MAKE} = "yes" ] ; then
    exit 0
fi

#-------------------------------------------------------
#  Part 3: Launch the processes
#-------------------------------------------------------
if [ "${SIMULATION_MODE}" = "yes" ] ; then
  printf "Launching shoreside MOOS Community (WARP=%s) \n"  $TIME_WARP
  pAntler shoreside.moos > log_shoreside.log &
fi

printf "Launching EcoMapper MOOS Community (WARP=%s) \n" $TIME_WARP
pAntler ecomapper.moos > log_ecomapper.log &

if [ "${SIMULATION_MODE}" = "yes" ]; then
  sleep .25
  printf "Done \n"

  uMAC shoreside.moos

  printf "Killing all processes ... \n"
  kill %1 %2
  printf "Done killing processes.   \n"
fi
