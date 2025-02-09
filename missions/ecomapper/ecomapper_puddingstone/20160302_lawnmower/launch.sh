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
EXP_LOCATION="puddingstone" # puddingstone, santafe, arrowhead
PLUGDIR="../../../plugs" # no leading slash
MSGDIR="../../../modem-msgs"

# survey area
PAINTSEGLIST="pts={500,1200:500,1000:900,1000:900,1200:500,1200},label=survey_area,label_color=white,edge_color=green,vertex_color=green,vertex_size=2,edge_size=2"

# config for lawnmower used to get pilot data for autotuning hyperparams GP
PILOT_LAWNMOWER_CONFIG="format=lawnmower,label=pilot-survey,x=700,y=1100,width=400,height=200,lane_width=100,degs=0,startx=0,starty=0"
PILOT_LAWNMOWER_C_NS="$PILOT_LAWNMOWER_CONFIG,rows=north-south"
PILOT_LAWNMOWER_C_EW="$PILOT_LAWNMOWER_CONFIG,rows=east-west"

# config for lawnmower for actual GP model building
LAWNMOWER="format=lawnmower,label=east-west-survey,x=700,y=1100,width=400,height=200,lane_width=20,degs=0,startx=0,starty=0"
LAWNMOWEREW="$LAWNMOWER,rows=east-west"
LAWNMOWERNS="$LAWNMOWER,rows=north-south"

# ports
ANNA_LISTEN="8301"
ANNA_VPORT="8001"
SHORE_LISTEN="8300"
SHORE_VPORT="8000"

SERVERHOST="localhost" #"localhost"
nsplug meta_shoreside.moos targ_shoreside.moos -f WARP=$TIME_WARP \
   VNAME="shoreside" USC_DATA_DIR="$MOOSIVP_USC_HOME/data"        \
   SHARE_LISTEN=$SHORE_LISTEN VPORT=$SHORE_VPORT SERVER_HOST=$SERVERHOST \
   LOCATION=$EXP_LOCATION  PLUG_DIR=$PLUGDIR  MSG_DIR=$MSGDIR     \
   PAINT_SEGLIST=$PAINTSEGLIST

# START HEADING same for all vehicles - can be customized (not needed here)
START_HEADING="230"

VNAME1="ecomapper"        # The first  vehicle community
SURVEY_DEPTH1="5"
START_POS1="460,935"
WAYPOINTS1="445,945:445,1045:500,1100:520,1100:520,1200:540,1200:540,1100:560,1100:560,1200:580,1200:580,1100:600,1100:600,1200:500,1100:425,1025:450,945"
#"440,940:300,1000:600,1000:600,1100:400,1000"
MODEMID1="1"
VTYPE1="UUV" # UUV, SHIP
nsplug meta_vehicle.moos targ_$VNAME1.moos -f WARP=$TIME_WARP  \
   VNAME=$VNAME1  START_POS=$START_POS1  START_HDG=$START_HEADING \
   VPORT=$ANNA_VPORT SHARE_LISTEN=$ANNA_LISTEN SERVER_LISTEN=$SHORE_LISTEN \
   VTYPE=$VTYPE1      MODEMID=$MODEMID1                        \
   SERVER_HOST=$SERVERHOST LOCATION=$EXP_LOCATION             \
   PLUG_DIR=$PLUGDIR  MSG_DIR=$MSGDIR  LAWNMOWER_CONFIG=$LAWNMOWER
   
nsplug meta_vehicle.bhv targ_$VNAME1.bhv -f VNAME=$VNAME1      \
    START_POS=$START_POS1 WAYPOINTS=$WAYPOINTS1                \
    SURVEY_DEPTH=$SURVEY_DEPTH1 VTYPE=$VTYPE1                    \
    LAWNMOWER_NS=$LAWNMOWERNS LAWNMOWER_EW=$LAWNMOWEREW        \
    PILOT_LAWNMOWER_NS=$PILOT_LAWNMOWER_C_NS                   \
    PILOT_LAWNMOWER_EW=$PILOT_LAWNMOWER_C_EW


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

#printf "Launching $VNAME2 MOOS Community (WARP=%s) \n" $TIME_WARP
#pAntler targ_$VNAME2.moos > log_$VNAME2.log &
#sleep .25

printf "Done \n"

uMAC targ_shoreside.moos

printf "Killing all processes ... \n"
kill %1 %2 #%3 %4
printf "Done killing processes.   \n"
