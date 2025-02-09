#!/bin/bash 
#-------------------------------------------------------
#  Part 1: Check for and handle command-line arguments
#-------------------------------------------------------
TIME_WARP=1
JUST_MAKE="no"

for ARGI; do
    if [ "${ARGI}" = "--help" -o "${ARGI}" = "-h" ] ; then
        printf "%s [SWITCHES] [time_warp]   \n" $0
        printf "  --just_make, -j    \n" 
        printf "  --help, -h         \n" 
        exit 0;
    elif [ "${ARGI//[^0-9]/}" = "$ARGI" -a "$TIME_WARP" = 1 ]; then 
        TIME_WARP=$ARGI
    elif [ "${ARGI}" = "--just_build" -o "${ARGI}" = "-j" ] ; then
        JUST_MAKE="yes"
    else 
        printf "Bad Argument: %s \n" $ARGI
        exit 0
    fi
done

#-------------------------------------------------------
#  Part 2: Create the .moos and .bhv files. 
#-------------------------------------------------------
# simulation set-up
EXP_LOCATION="santafe" # santafe, arrowhead
PLUGDIR="../../plugs" # no leading slash
PROTO_MSG_DIR="./msgs"

SHORE_VPORT="9000"
ANNA_VPORT="9001"
BERN_VPORT="9002"

SHORE_LISTEN="9300"
ANNA_LISTEN="9301"
BERN_LISTEN="9302"

SERVERHOST="localhost"

nsplug meta_shoreside.moos targ_shoreside.moos -f WARP=$TIME_WARP  \
   VNAME="shoreside" USC_DATA_DIR="$MOOSIVP_USC_HOME/data"         \
   VPORT=$SHORE_VPORT SHARE_LISTEN=$SHORE_LISTEN SERVER_HOST=$SERVERHOST \
   LOCATION=$EXP_LOCATION  PLUG_DIR=$PLUGDIR  MSG_DIR=$PROTO_MSG_DIR

# START HEADING same for all vehicles - can be customized (not needed here)
START_HEADING="230"

VNAME1="anna"        # The first  vehicle community
START_DEPTH1="0"
START_POS1="1450,275"
WAYPOINTS1="1355,220:1235,165:1180,130:1120,160:1190,200:1280,250:1385,290:1330,300:1160,255:1110,300:1065,350:1080,415:950,400:940,310:1075,330:1150,255:1490,295"
MODEMID1="1"
VTYPE1="UUV" # UUV, SHIP
nsplug meta_vehicle.moos targ_$VNAME1.moos -f WARP=$TIME_WARP  \
   VNAME=$VNAME1  START_POS=$START_POS1  START_HDG=$START_HEADING \
   VPORT=$ANNA_VPORT       SHARE_LISTEN=$ANNA_LISTEN           \
   VTYPE=$VTYPE1      MODEMID=$MODEMID1                        \
   SERVER_HOST=$SERVERHOST  SERVER_LISTEN=$SHORE_LISTEN        \
   LOCATION=$EXP_LOCATION                                      \
   PLUG_DIR=$PLUGDIR  MSG_DIR=$PROTO_MSG_DIR
nsplug meta_vehicle.bhv targ_$VNAME1.bhv -f VNAME=$VNAME1      \
    START_POS=$START_POS1 WAYPOINTS=$WAYPOINTS1                \
    START_DEPTH=$START_DEPTH1 VTYPE=$VTYPE1

VNAME2="bernard"      # The second vehicle community
START_POS2="1400,280"
WAYPOINTS2="1400,280:1350,200:1400,280"
START_DEPTH2="0"
MODEMID2="2"
VTYPE2="UUV" # UUV, SHIP
nsplug meta_vehicle.moos targ_$VNAME2.moos -f WARP=$TIME_WARP  \
   VNAME=$VNAME2  START_POS=$START_POS2  START_HDG=$START_HEADING \
   VPORT=$BERN_VPORT       SHARE_LISTEN=$BERN_LISTEN           \
   VTYPE=$VTYPE2      MODEMID=$MODEMID2                        \
   SERVER_HOST=$SERVERHOST  SERVER_LISTEN=$SHORE_LISTEN        \
   LOCATION=$EXP_LOCATION                                      \
   PLUG_DIR=$PLUGDIR  MSG_DIR=$PROTO_MSG_DIR
nsplug meta_vehicle.bhv targ_$VNAME2.bhv -f VNAME=$VNAME2      \
    START_POS=$START_POS2 WAYPOINTS=$WAYPOINTS2                \
    START_DEPTH=$START_DEPTH2 VTYPE=$VTYPE2


if [ ${JUST_MAKE} = "yes" ] ; then
    exit 0
fi

#-------------------------------------------------------
#  Part 3: Launch the processes
#-------------------------------------------------------
printf "Launching shoreside MOOS Community (WARP=%s) \n"  $TIME_WARP
pAntler targ_shoreside.moos > log_shoreside.log &

printf "Launching $VNAME1 MOOS Community (WARP=%s) \n" $TIME_WARP
pAntler targ_$VNAME1.moos > log_$VNAME1.log &
#>& /dev/null &
sleep .25

printf "Launching $VNAME2 MOOS Community (WARP=%s) \n" $TIME_WARP
pAntler targ_$VNAME2.moos > log_$VNAME2.log &
sleep .25

printf "Done \n"

uMAC targ_shoreside.moos

printf "Killing all processes ... \n"
kill %1 %2 #%3 %4
printf "Done killing processes.   \n"
