netCDF Explorer
====================

<img src="https://cloud.githubusercontent.com/assets/6119070/11098722/66e4ad1c-886c-11e5-9bd2-097b15457102.png">

netCDF Explorer is a multi-platfrom graphical browser for netCDF files.

[netCDF](http://www.unidata.ucar.edu/software/netcdf) 

netCDF support includes
[DAP](http://opendap.org).


Dependencies
------------

<img src="https://cloud.githubusercontent.com/assets/6119070/13334137/231ea0f8-dbd0-11e5-8546-8a409d80aa6d.png">

[Qt](http://www.qt.io/)
Qt is a cross-platform application framework for creating graphical user interfaces.
<br /> 

[netCDF](http://www.unidata.ucar.edu/software/netcdf)
netCDF is a set of software libraries and self-describing, 
machine-independent data formats that support the creation, 
access, and sharing of array-oriented scientific data.
<br /> 

Building from source
------------


Install dependency packages (Ubuntu):
<pre>
sudo apt-get install build-essential
sudo apt-get build-dep qt5-default
sudo apt-get install "^libxcb.*" libx11-xcb-dev libglu1-mesa-dev libxrender-dev libxi-dev
sudo apt-get install libgl1-mesa-dev
sudo apt-get install libnetcdf-dev netcdf-bin netcdf-doc
</pre>

Get source:
<pre>
git clone https://github.com/pedro-vicente/netcdf_explorer_qt.git
</pre>

Build with:
<pre>
qmake
make
</pre>

Modifying dependencies location for Windows, Mac
------------

Qt uses project files with extension .pro to generate build files.
To use non standard dependency locations, add the location in the file
netcdf_explorer.pro.

Edit the relevant section. For Mac

<pre>
macx: {
 INCLUDEPATH += /usr/local/include
 LIBS += /usr/local/lib/libnetcdf.a
 LIBS += /usr/local/lib/libhdf5.a
 LIBS += /usr/local/lib/libhdf5_hl.a
 LIBS += /usr/local/lib/libsz.a
 LIBS += -lcurl -lz
}
</pre>

For Windows, add the location of the netCDF header and libraries 

<pre>
win32 {
 INCLUDEPATH += 
 LIBS += 
}
</pre>

For other Qt Linux supported system, edit the section
and add the location of the netCDF header and libraries 

<pre>
unix:!macx {

}
</pre>

Generate sample data
------------

To generate the included netCDF sample data in /data/netcdf:

<pre>
ncgen -k netCDF-4 -b -o data/test_01.nc data/test_01.cdl
ncgen -k netCDF-4 -b -o data/test_02.nc data/test_02.cdl
ncgen -k netCDF-4 -b -o data/test_03.nc data/test_03.cdl
ncgen -k netCDF-4 -b -o data/test_04.nc data/test_04.cdl
</pre>

test_01.cdl includes one, two and three dimensional variables with coordinate variables 
<br /> 
test_02.cdl includes a four dimensional variable 
<br /> 
test_03.cdl includes a five dimensional variable
<br />
test_04.cdl includes attributes for all netCDF types for variables and groups
<br />  

To run and open a sample file from the command line:

<pre>
./netcdf-explorer data/test_03.nc
</pre>

OpenDAP sample data.
Open URl from the OpenDap menu item

http://www.esrl.noaa.gov/psd/thredds/dodsC/Datasets/cmap/enh/precip.mon.mean.nc

<a target="_blank" href="http://www.space-research.org/">
<img src="https://cloud.githubusercontent.com/assets/6119070/11140582/b01b6454-89a1-11e5-8848-3ddbecf37bf5.png"></a>


