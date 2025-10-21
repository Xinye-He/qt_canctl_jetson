# Qt CAN Control Tool (for Jetson Orin NX)

A Qt5-based CAN controller with send/receive, loop mode, and bitrate configuration.

## Dependencies
```bash
sudo apt install qtbase5-dev qt5-qmake qtbase5-dev-tools

#Build
mkdir build && cd build
cmake ..
make -j$(nproc)

#Run
./qt_canctl_2.2
