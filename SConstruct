import os
import sys

SConsignFile('.sconsign')

###### COMMAND LINE OPTIONS

opts=Options()

opt=opts.Add(PathOption('PREFIX', 'Directory to install under', '/usr/local'))
opt=opts.Add(PathOption('SBINDIR', 'Directory to install under', os.path.join('$PREFIX','sbin')))
opt=opts.Add(PathOption('MANPATH', 'Directory to install under', os.path.join('$PREFIX','man')))

### DEBUG MODE

opt=opts.Add('DEBUG','debug level',0)

###### BUILD ENVIRONMENT

env=Environment(options=opts, ENV=os.environ)
debug=1 #int(env['DEBUG'])

env.Append(CCFLAGS='-Wall')

if (debug>0):
  env.Append(CCFLAGS='-g')
  env.Append(CPPDEFINES=["DEBUG"])
else:
  env.Append(CCFLAGS='-O3')

env.Replace(CXXFILESUFFIX=".cpp")

#env.Append(CPPDEFINES={"_FILE_OFFSET_BITS": "64", "_LARGEFILE_SOURCE": None, "_REENTRANT": None,
#	"FUSE_USE_VERSION": 22})
env.Append(CPPDEFINES=["_FILE_OFFSET_BITS=64", "_LARGEFILE_SOURCE", "_REENTRANT",
	"FUSE_USE_VERSION=22"])

for v in ("CXX","LINK"):
  if (v in os.environ):
    env.Replace(**{v: os.environ[v]})

###### CONTEXT CHECKS

conf=Configure(env)

### LIBFUSE

if (not env.GetOption('clean')):
  if (conf.TryAction('pkg-config --exists fuse')[0]):
    conf.env.ParseConfig('pkg-config --cflags --libs fuse')
    print "Checking for libfuse... found"
  elif (conf.CheckLibWithHeader('fuse', 'fuse/fuse.h', 'C')):
    conf.env.Append(LIBS=['fuse'])
    print "Checking for libfuse... found"
  else:
    print "Checking for libfuse... not found"
    sys.stderr.write("fatal: libfuse not found\n")
    sys.exit(1)

  conf.env.Append(LIBS=['stdc++'])

### FINISH

env=conf.Finish()

###### WORK

env.bin_targets=[]
sources=[]

for i in "main.cpp tfdisk.cpp hexdump.c".split():
  sources.append(os.path.join("src",i))

env.bin_targets.append(env.Program(os.path.join("bin","mount.tffs"),sources))

###### INSTALL TARGET

destdir=""
if ("DESTDIR" in os.environ):
  destdir=os.environ["DESTDIR"]

env.Install(destdir+env["SBINDIR"],env.bin_targets)
env.Install(destdir+os.path.join(env["MANPATH"],"man1"),File("mount.tffs.1"))
env.Alias("install",[destdir+env["SBINDIR"],destdir+env["MANPATH"]] )

###### HELP TEXT

Help(opts.GenerateHelpText(env))
