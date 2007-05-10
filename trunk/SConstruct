import os
import sys

SConsignFile('.sconsign')

###### COMMAND LINE OPTIONS

opts=Options()

opt=opts.Add(PathOption('PREFIX', 'Directory to install under', '/usr/local'))
opt=opts.Add(PathOption('BINDIR', 'Directory to install under', os.path.join('$PREFIX','bin')))
opt=opts.Add(PathOption('MANPATH', 'Directory to install under', os.path.join('$PREFIX','man')))

### DEBUG MODE

opt=opts.Add('DEBUG','debug level',0)

### QT

defaultqtdir=None
if ("QTDIR" in os.environ):
  defaultqtdir=os.environ["QTDIR"]
else:
  for d in ("/usr/share/qt3","/usr/local/share/qt3","/usr/local/qt3","/usr/share/qt"):
    if (os.path.isdir(d)):
      defaultqtdir=d
      break
opts.Add(PathOption('QTDIR','Path to Qt installation',defaultqtdir))

opts.Add('QT_LIB',"Qt library name (qt, qt-mt)","qt-mt")

### FFMPEG

opts.Add('FFMPEG',"""Prefix path to the FFMPEG libraries.
        If set to '/usr', include files have to be in '/usr/include/ffmpeg'
        and static libraries in '/usr/lib'. If unset, FFMPEG will be
	compiled locally.""",None)

###### BUILD ENVIRONMENT

env=Environment(options=opts, ENV=os.environ)
debug=int(env['DEBUG'])

if (debug>0):
  env.Append(CCFLAGS=['-g3','-Wall'])
else:
  env.Append(CCFLAGS=['-O3','-Wall'])

env.Replace(CXXFILESUFFIX=".cpp")

env.Append(CPPDEFINES=[("_FILE_OFFSET_BITS", "64"), "_LARGEFILE_SOURCE"])

for v in ("CXX","LINK"):
  if (v in os.environ):
    env.Replace(**{v: os.environ[v]})
    
###### CONTEXT CHECKS

conf=Configure(env)

### LIBAO

if (not env.GetOption('clean')):
  if (conf.TryAction('pkg-config --exists ao')[0]):
    conf.env.Append(CPPDEFINES="HAVE_LIB_AO")
    conf.env.ParseConfig('pkg-config --cflags --libs ao')
    print "Checking for C library ao... yes"
  elif (conf.CheckLibWithHeader('ao', 'ao/ao.h', 'C')):
    conf.env.Append(CPPDEFINES="HAVE_LIB_AO")

### LIBMAD

if (not env.GetOption('clean')):
  if (conf.TryAction('pkg-config --exists mad')[0]):
    conf.env.Append(CPPDEFINES="HAVE_LIB_MAD")
    conf.env.ParseConfig('pkg-config --cflags --libs mad')
    print "Checking for C library mad... yes"
  elif (conf.CheckLibWithHeader('mad', 'mad.h', 'C')):
    conf.env.Append(CPPDEFINES="HAVE_LIB_MAD")

### LIBA52

if (not env.GetOption('clean')):
  if (conf.TryAction('pkg-config --exists a52')[0]):
    conf.env.Append(CPPDEFINES="HAVE_LIB_A52")
    conf.env.ParseConfig('pkg-config --cflags --libs a52')
    print "Checking for C library a52... yes"
  elif (conf.CheckLibWithHeader('a52', ['stdint.h','a52dec/a52.h'], 'C')):
    conf.env.Append(CPPDEFINES="HAVE_LIB_A52")
  
### FINISH
    
env=conf.Finish()

###### BUILD ENVIRONMENT (pt2)

### QT

qtlib=env["QT_LIB"]
env.Tool("qt")
env.Replace(QT_LIB=qtlib)

if (debug<=0):
  env.Append(CPPDEFINES="QT_NO_DEBUG")

### FFMPEG

if (env.GetOption('clean') or not ((env.has_key("FFMPEG")) and (env["FFMPEG"]!=None))):
  localffmpeg=True
  ffmpegpath=Dir("ffmpeg").abspath
else:
  localffmpeg=False
  ffmpegpath=env["FFMPEG"].rstrip("/")

if (ffmpegpath!='/usr'): 
  env.Append(CPPPATH=os.path.join(str(ffmpegpath),'include'))
  env.Append(LIBPATH=os.path.join(str(ffmpegpath),'lib'))
env.Append(LIBS=['avformat','avcodec','avutil'])
  
###### WORK

env.bin_targets=[]
if (localffmpeg):
  dtsenabled=False
  SConscript('SConscript.ffmpeg','debug dtsenabled')
SConscript(os.path.join('src','SConscript'),'env debug')

###### INSTALL TARGET

destdir=""
if ("DESTDIR" in os.environ):
  destdir=os.environ["DESTDIR"]

env.Install(destdir+env["BINDIR"],env.bin_targets)
env.Install(destdir+os.path.join(env["MANPATH"],"man1"),File("dvbcut.1"))
env.Alias("install",[destdir+env["BINDIR"],destdir+env["MANPATH"]] )

###### HELP TEXT

Help(opts.GenerateHelpText(env))
