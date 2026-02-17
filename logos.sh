#!/bin/bash

echo ""
echo "=== BOOT LOGO (large) ==="
echo ""

# Strategy: the main body strokes are bright white (97)
# The thinner/connecting parts are light grey (37)  
# Small detail chars like - . = % are dark grey (90)

W='\033[97m'  # bright white - main strokes
G='\033[37m'  # light grey - secondary strokes  
D='\033[90m'  # dark grey - fine details
R='\033[0m'   # reset

echo -e "        ${W}@@@@@@@@${R}"
echo -e "        ${G}####${W}@@@@${R}            ${D}@${R}      ${D}-${R}"
echo -e "           ${W}@@@${R}             ${W}@@@@@${R}  ${G}@@${D}.${R}"
echo -e "          ${W}@@${R}               ${W}@@${R}     ${W}@@${R}"
echo -e "        ${W}@@@${R}               ${G}@@@${R}    ${W}@@@${R}"
echo -e "      ${W}@@@${R}                 ${G}@@${R}     ${W}@@${R}"
echo -e "     ${W}@@@@@@@@@@@@@@@@@@@${D}-${R} ${W}@@${R}    ${G}@@@${R}"
echo -e "                          ${W}@@@@@@@@${R}"
echo -e "           ${W}@@${R}"
echo -e "           ${W}@@${R}                 ${W}@@${R}"
echo -e "           ${W}@@${R}     ${G}@@@@@@@${R}     ${W}@@${R}"
echo -e "           ${W}@@${R}   ${W}@@${R}      ${D}-${G}@@@${R}  ${W}@@${R}"
echo -e "           ${W}@@${R}  ${D}=${W}@@${R}       ${G}@@@${R}  ${W}@@${R}"
echo -e "           ${W}@@${R}  ${G}@@@${R}       ${G}@@@${R}  ${W}@@${R}"
echo -e "           ${W}@@@@@@${R}         ${W}@@@@@${R}"
echo ""

echo ""
echo "=== PANIC LOGO (small) ==="
echo ""

echo -e "      ${W}@@@@@@${R}"
echo -e "        ${G}%${W}@@${R}         ${G}%${W}@@@${R}  ${G}@${R}"
echo -e "       ${W}@@${R}           ${W}@@${R}   ${W}@@${R}"
echo -e "     ${W}@@${R}             ${G}@${R}    ${G}@${D}=${R}"
echo -e "    ${W}@@@@@@@@@@@@@@${R} ${W}@@${R}   ${G}@@${R}"
echo -e "        ${W}@@${R}          ${W}@@@@${R}"
echo -e "        ${W}@@${R}             ${W}@${R}"
echo -e "        ${W}@@${R}  ${G}%${W}@@@@@@@${R}  ${D}*${W}@${R}"
echo -e "        ${W}@@${R}  ${W}@${R}      ${W}@@${R} ${W}@@${R}"
echo -e "        ${W}@@${R}  ${W}@${R}      ${W}@@${R} ${W}@@${R}"
echo -e "         ${W}@@@${R}        ${W}@@@${R}"
echo ""