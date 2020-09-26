#!/bin/bash
# -*- tab-width: 4; encoding: utf-8; -*-

## @author Subhankar Pal
## @brief Helper script with common function and variable definitions
## @version 1.0

LOGDIR=../logs

if tty -s; then
    red=`tput setaf 1`
    green=`tput setaf 2`
    bold=`tput bold`
    reset=`tput sgr0`
fi

seq=0

function warn {
    echo "${red}${bold}[$seq]: ${1}${reset}"
    seq=$((seq+1))
}
function info {
    echo "${green}${bold}[$seq]: ${1}${reset}"
    seq=$((seq+1))
}
function chk_var {
    var=$1
    eval "val=\$$var"
    if [ -z ${val} ]; then
        warn "Var ${var} is unset"
        exit 1
    fi
}
