#--------------------------------------------------------
# The CMakeLists.txt for:                           pGP
# Author(s): Stephanie Kemna, USC
# Note: this is a template dir, so you can copy to start
#       making your MOOSApp
#--------------------------------------------------------

SET(SRC
  PGP.cpp
  main.cpp
)

ADD_EXECUTABLE(pGP ${SRC})

target_compile_definitions(pGP PRIVATE BUILD_VORONOI=${BUILD_VORONOI})

TARGET_LINK_LIBRARIES(pGP
   ${MOOS_LIBRARIES}
   ${MOOSGeodesy_LIBRARIES}
   geometry
   apputil
   mbutil
   ${SYSTEM_LIBS}
   # whatever other libs you need
   ${LIBGP_LIBRARIES}
   ${Boost_LIBRARIES}
)

