#!/bin/bash
##########################################
#
#
#
##########################################



filename="user_settings.txt"
fileCopyLoc="out/build/user_settings.txt"

if [ -e "$filename" ]
then
    if diff "$filename" "$fileCopyLoc" >/dev/null
    then
        # run the framework
        echo "user_settings are already generated"
        echo "building framework"
        echo ""
        cmake -S . -B out/build/
        cd out/build/
        make ../../
        echo ""
        echo "finished framework build"  
        
    else
        bash create_user_settings.sh
    fi
    
else
    # create user_settings from console input
    bash create_user_settings.sh
fi

