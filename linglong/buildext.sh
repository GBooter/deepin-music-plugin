apt -o APT::Sandbox::User=root update || echo "$?"
apt -o APT::Sandbox::User=root -y install libvlc5 libvlccore9 vlc-plugin-base libsdl2-2.0-0 libtag1v5 libmpris-qt6 libavcodec60 || echo "$?"
