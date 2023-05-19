project(
	'VSK Shell',
	'c',
	'cpp',
	version: '0.1.0',
	license: 'MIT',
	meson_version: '>=0.51.0',
	default_options: [
		'cpp_std=c++17',
		'c_std=c11',
	],
)

add_project_arguments(['-DWLR_USE_UNSTABLE'], language: ['cpp', 'c'])
add_project_arguments(['-DWAYFIRE_PLUGIN'], language: ['cpp', 'c'])
add_project_link_arguments(['-rdynamic','-fPIC'], language:'cpp')

wayfire = dependency('wayfire')
wlroots = dependency('wlroots')

Sources = [
	'VSKShell.cpp',
]

shared_module( 'vsk-shell', [ Sources ],
	dependencies: [wayfire, wlroots],
	install: true,
	install_dir: join_paths(get_option('libdir'), 'wayfire')
)

install_data( 'vsk-shell.xml', install_dir: wayfire.get_variable( pkgconfig: 'metadatadir' ) )
