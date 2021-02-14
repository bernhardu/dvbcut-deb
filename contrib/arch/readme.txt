
### Install dependencies
# pacman -S git fakeroot base-devel qt5 libao libmad a52dec


mkdir dvbcut
cd dvbcut
wget https://raw.githubusercontent.com/bernhardu/dvbcut-deb/master/contrib/arch/PKGBUILD
wget https://raw.githubusercontent.com/bernhardu/dvbcut-deb/master/contrib/arch/dvbcut.install
### Taken and updated from https://aur.archlinux.org/dvbcut.git

makepkg
 

# pacman -U /home/user/path/to/dvbcut-0.7.3-1-x86_64.pkg.tar.zst
