#!/bin/bash
# run_classicplus_suite.sh: every Classic+ menu test in one command (1.1.3-insider.8m, the owner's Classic+
# audit: "I'm finding the same bugs over and over again"). The Xbox files and MCC's alone (the owner's own
# Vita), headless, the network rows on unroutable test brokers - nothing live is reached:
#   run_menus_test.sh   cpaudit (every Classic+ flow on both data sets, no empty stand-in, no "stands for
#                       nothing", no "failed to spawn widget", no Halo 3/ElDewrito screen in Classic+),
#                       ceplusmain ceplusmp ceplusprofile cepluspregame cepluspause ceplusforge experimental
#                       ceoriginal mccmp msgboxes thankyou fresh level xbox pause, cpworkshop cpworkshoptouch
#                       (Classic+'s STEAM WORKSHOP and DOWNLOAD FROM STEAM on the tests' fake Steam, 1.1.3)
#   run_mcc_content_test.sh menu (mccmenu, mcccplus: every Classic+ screen on MCC's ui.map, mcconline)
# Needs the menus data (HALO_TEST_DATA_MENUS), HALO_TEST_PROFILE (a blam.sav with every level) and MCC's
# halo1 (HALO_TEST_MCC_ROOT, default Steam's); a part without its data says SKIP. Exit 0 when all pass.
here=$(cd "$(dirname "$0")" && pwd)
status=0
cases=${HALO_CP_CASES:-"cpaudit ceplusmain ceplusmp ceplusprofile cepluspregame cepluspause ceplusforge experimental ceoriginal mccmp msgboxes thankyou fresh level xbox pause cpworkshop cpworkshoptouch"}
export SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
unset DISPLAY
echo "=== run_menus_test.sh $cases"
bash "$here/run_menus_test.sh" $cases || status=1
echo "=== run_mcc_content_test.sh menu"
bash "$here/run_mcc_content_test.sh" menu || status=1
[ $status = 0 ] && echo "CLASSIC+ SUITE: PASS" || echo "CLASSIC+ SUITE: FAIL"
exit $status
