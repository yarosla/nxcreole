from distutils.core import setup, Extension

ext = Extension('nxcreole._ext', sources = ['nxcreole_parser.c', 'nxcreole_ext.c'])

setup(name = 'nxcreole',
      version = '1.0',
      packages = ['nxcreole'],
      py_modules = ['nxcreole.parser'],
      ext_modules = [ext],
      description = 'Wiki Creole 1.0 markup parser (C extension)',
      author = 'Yaroslav Stavnichiy',
      author_email = 'yarosla@gmail.com',
      url = 'https://bitbucket.org/yarosla/nxcreole',
      keywords = ['wiki', 'creole'],
      license = 'LGPLv3',
      classifiers = [
        'License :: OSI Approved :: GNU Lesser General Public License v3 or later (LGPLv3+)',
        'Topic :: Text Processing :: Markup',
        'Intended Audience :: Developers',
      ])
