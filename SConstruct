import os, glob, sys

def scanFiles(pattern, paths) :
	files = []
	for path in paths :
		files+=glob.glob(os.path.join(path,pattern))
	return files

def recursiveDirs(root) :
	return filter( (lambda a : a.rfind( ".svn")==-1 ),  [ a[0] for a in os.walk(root)]  )

options = Options('options.cache', ARGUMENTS)
options.Add(PathOption('clam_prefix', 'The prefix where CLAM was installed', '/usr/local'))

env = Environment(ENV=os.environ, options=options)
options.Save('options.cache', env)
Help(options.GenerateHelpText(env))
env.SConsignFile() # Single signature file

env.ParseConfig('pkg-config --cflags --libs libcurl')

CLAMInstallDir = env['clam_prefix']
clam_sconstoolspath = os.path.join(CLAMInstallDir,'share','clam','sconstools')

env.Tool('qt4', toolpath=[clam_sconstoolspath])
env.EnableQt4Modules([
	'QtCore',
	'QtGui',
#	'QtOpenGL',
#	'QtSql',
#	'QtNetwork',
#	'QtTest',
#	'QtXml',
#	'QtSvg',
#	'QtUiTools',
#	'QtDesigner',
#	'Qt3Support',
	], debug=False)

env['CXXFILESUFFIX'] = '.cxx'
env['QT4_UICDECLSUFFIX'] = '.hxx'
env['QT4_MOCHPREFIX'] = os.path.join('generated','moc_')
env['QT4_UICDECLPREFIX'] = os.path.join('generated','uic_')
env['QT4_QRCCXXPREFIX'] = os.path.join('generated','qrc_')

env.Tool('clam', toolpath=[clam_sconstoolspath])
env.EnableClamModules([
	'clam_core',
	'clam_audioio',
	'clam_processing',
	] , CLAMInstallDir)
 
mainSources = {
	'MyProgram' :'./main.cxx',
}
sourcePaths = recursiveDirs(".")
sources = scanFiles('*.cxx', sourcePaths)
sources = filter( (lambda a : a.rfind( "moc_")==-1 ),  sources )
sources = filter( (lambda a : a.rfind( "qrc_")==-1 ),  sources )
sources = dict.fromkeys(sources).keys()
for mainSource in mainSources.values() :
	sources.remove(mainSource)

# Qt4 stuff I think
qrcfiles = scanFiles("*.qrc", sourcePaths)
if qrcfiles : sources += env.Qrc(source=qrcfiles)

uifiles = scanFiles("*.ui", sourcePaths)
if uifiles: uiheaders = env.Uic4(source=uifiles)
# End Qt4 stuff

if sys.platform=='win32' :
	env.Append(CPPFLAGS=['-D_USE_MATH_DEFINES']) # to have M_PI defined

if sys.platform=='linux2' :
	env.Append( CCFLAGS=['-g','-O3','-Wall'] )

programs = [ env.Program(target=program, source = [main] + sources) 
	for program, main in mainSources.items()]

env.Default(programs)
