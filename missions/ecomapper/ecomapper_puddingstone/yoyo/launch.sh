#!/bin/bash 
#-------------------------------------------------------
#  Part 1: Check for and handle command-line arguments
#-------------------------------------------------------

#default values
TIME_WARP=1
JUST_MAKE="no"
# standard: runtime
SIMULATION_MODE="no"
TOPSIDE="no"
ECOMAPPER="no"

#overwrite with arguments
printUsage ()
{
  printf "USAGE:\n"
  printf "%s [SWITCHES] [time_warp]   \n" $0
  printf "  --just_make, -j    \n" 
  printf "  --simulation, -s   \n"
  printf "  --ecomapper, -e    \n"
  printf "  --topside, -t      \n" 
  printf "  --help, -h         \n" 
  exit 0;
}

if [[ $# -eq 0 ]] ; then
  printUsage
fi

for ARGI; do
    if [ "${ARGI}" = "--help" -o "${ARGI}" = "-h" ] ; then
      printUsage
    elif [ "${ARGI//[^0-9]/}" = "$ARGI" -a "$TIME_WARP" = 1 ]; then 
        TIME_WARP=$ARGI
    elif [ "${ARGI}" = "--just_build" -o "${ARGI}" = "-j" ] ; then
        JUST_MAKE="yes"
    elif [ "${ARGI}" = "--simulation" -o "${ARGI}" = "-s" ] ; then
        SIMULATION_MODE="yes"
    elif [ "${ARGI}" = "--ecomapper" -o "${ARGI}" = "-e" ] ; then
        ECOMAPPER="yes"
    elif [ "${ARGI}" = "--topside" -o "${ARGI}" = "-t" ] ; then
        TOPSIDE="yes"
    else 
        printf "Bad Argument: %s \n" $ARGI
        exit 0
    fi
done

if [ "${SIMULATION_MODE}" = "yes" ] ; then
  ECOMAPPER="yes"
  TOPSIDE="yes"
fi

#-------------------------------------------------------
#  Part 2: Create the .moos and .bhv files. 
#-------------------------------------------------------
# simulation set-up
LAKE="puddingstone_bathy"
PLUGDIR="../../../plugs" # no leading slash

if [ "${SIMULATION_MODE}" = "yes" ] ; then
  SERVERHOST_EM="localhost"
  SERVERHOST_SS="localhost"
fi
if [ "${SIMULATION_MODE}" = "no" ] ; then
  SERVERHOST_EM="192.168.10.11"
  SERVERHOST_SS="192.168.10.117"
fi

if [ "${TOPSIDE}" = "yes" -o "${JUST_MAKE}" = "yes" ] ; then
  # create shoreside.moos
  nsplug shoreside.meta shoreside.moos -f WARP=$TIME_WARP \
     VNAME="shoreside" USC_DATA_DIR="$MOOSIVP_USC_HOME/data"        \
     SHARE_LISTEN="9300" VPORT="9000" SERVER_HOST=$SERVERHOST_SS       \
     SERVER_HOST_EM=$SERVERHOST_EM  PLUG_DIR=$PLUGDIR  LOCATION=$LAKE
fi

if [ "${ECOMAPPER}" = "yes" -o "${JUST_MAKE}" = "yes" ] ; then
  # create ecomapper.moos
  VNAME1="zoomer"        # The first  vehicle community
  START_DEPTH1="0"
  START_POS1="440,950"
  START_HEADING1="0"
  WAYPOINTS1="445,945:445,1045:500,1100:520,1100:520,1200:540,1200:540,1100:560,1100:560,1200:580,1200:580,1100:600,1100:600,1200:500,1100:425,1025:450,945"
#"445,945:445,1025:525,1155:525,1000:585,1000:585,1155:670,1155:670,1000:445,1000:445,945
  #"440,950:440,1000:440,1030:400,1015:400,975:440,950"
  MODEMID1="1"
  VTYPE1="UUV" # UUV, SHIP
  nsplug ecomapper.meta ecomapper.moos -f WARP=$TIME_WARP  \
     VNAME=$VNAME1  START_POS=$START_POS1  START_HDG=$START_HEADING1 \
     VPORT="9001"       SHARE_LISTEN="9301"                      \
     VTYPE=$VTYPE1      MODEMID=$MODEMID1                        \
     SERVER_HOST=$SERVERHOST_EM SERVER_HOST_SS=$SERVERHOST_SS    \
     SIMULATION=$SIMULATION_MODE  PLUG_DIR=$PLUGDIR  LOCATION=$LAKE
  nsplug ecomapper_bhv.meta ecomapper.bhv -f VNAME=$VNAME1       \
      START_POS=$START_POS1 WAYPOINTS=$WAYPOINTS1                \
      START_DEPTH=$START_DEPTH1 VTYPE=$VTYPE1
fi

if [ ${JUST_MAKE} = "yes" ] ; then
    exit 0
fi

#-------------------------------------------------------
#  Part 3: Launch the processes
#-------------------------------------------------------
if [ "${TOPSIDE}" = "yes" ] ; then
  printf "Launching shoreside MOOS Community (WARP=%s) \n"  $TIME_WARP
  pAntler shoreside.moos > log_shoreside.log &
fi

#if [ "${TOPSIDE}" = "yes" ]; then
#  uMAC shoreside.moos
#fi

if [ "${ECOMAPPER}" = "yes" ] ; then 
  printf "Launching EcoMapper MOOS Community (WARP=%s) \n" $TIME_WARP
  pAntler ecomapper.moos > log_ecomapper.log
fi

sleep .25
printf "Done \n"

printf "Killing all processes ... \n"
kill %1 %2
printf "Done killing processes.   \n"
