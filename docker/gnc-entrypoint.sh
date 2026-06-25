#!/bin/bash
set -e
source /opt/ros/humble/setup.bash
source /opt/gnc_ws/install/setup.bash
exec "$@"
