
# minimal arch qemu VM 2021-02-19


### Install graphical environment
# pacman -Syy  # similar: apt update
# pacman -Syu  # similar: apt update && apt dist-upgrade
# pacman -S xorg-server xorg-xinit xf86-video-vesa xfce4 lightdm lightdm-gtk-greeter
# systemctl enable lightdm


### Install dependencies
# pacman -S git fakeroot base-devel qt5 libao libmad a52dec wget desktop-file-utils gdb


mkdir dvbcut
cd dvbcut
wget https://raw.githubusercontent.com/bernhardu/dvbcut-deb/master/contrib/arch/PKGBUILD
wget https://raw.githubusercontent.com/bernhardu/dvbcut-deb/master/contrib/arch/dvbcut.install
    # Taken and updated from https://aur.archlinux.org/dvbcut.git

makepkg


### Install package:
# pacman -U /home/user/path/to/dvbcut-0.7.3-1-x86_64.pkg.tar.zst


### Debug
# uncomment "options=(debug !strip)" in PKGBUILD and rebuild (with a clean directory), reinstall

    # Run from the build directory, as somehow the binary in the package still gets stripped.
$ gdb -q --args ./src/dvbcut-deb-0.7.3/src/dvbcut
(gdb) run

    # When gdb stopps because of the crash
(gdb) bt full


    # Maybe some hints in debugging: https://wiki.archlinux.org/index.php/Debug_-_Getting_Traces
