srcs_weston = [
	git_version_h,
	'main.c',
	'text-backend.c',
	'config-helpers.c',
	'weston-screenshooter.c',
	text_input_unstable_v1_server_protocol_h,
	text_input_unstable_v1_protocol_c,
	input_method_unstable_v1_server_protocol_h,
	input_method_unstable_v1_protocol_c,
]
deps_weston = [
	dep_libshared,
	dep_libweston_public,
	dep_libinput,
	dep_libevdev,
	dep_libdl,
	dep_threads,
]

if get_option('xwayland')
	config_h.set('BUILD_XWAYLAND', '1')

	srcs_weston += 'xwayland.c'
	config_h.set_quoted('XSERVER_PATH', get_option('xwayland-path'))
endif

libexec_weston = shared_library(
	'exec_weston',
	sources: srcs_weston,
	include_directories: common_inc,
	dependencies: deps_weston,
	install_dir: dir_module_weston,
	install: true,
	version: '0.0.0',
	soversion: 0
)
dep_libexec_weston = declare_dependency(
	link_with: libexec_weston,
	include_directories: [ include_directories('.'), public_inc ],
	dependencies: dep_libweston_public
)
exe_weston = executable(
	'weston',
	'executable.c',
	include_directories: common_inc,
	dependencies: dep_libexec_weston,
	install_rpath: dir_module_weston,
	install: true
)
install_headers('weston.h', subdir: 'weston')

pkgconfig.generate(
	filebase: 'weston',
	name: 'Weston Plugin API',
	version: version_weston,
	description: 'Header files for Weston plugin development',
	requires_private: [ lib_weston ],
	variables: [
		'pkglibexecdir=${libexecdir}/weston'
	],
	subdirs: 'weston'
)

install_data(
	'weston.desktop',
	install_dir: dir_data / 'wayland-sessions'
)

if get_option('screenshare')
	srcs_screenshare = [
		'screen-share.c',
		fullscreen_shell_unstable_v1_client_protocol_h,
		fullscreen_shell_unstable_v1_protocol_c,
	]
	deps_screenshare = [
		dep_libexec_weston,
		dep_libshared,
		dep_libweston_public,
		dep_libweston_private_h, # XXX: https://gitlab.freedesktop.org/wayland/weston/issues/292
		dep_wayland_client,
	]
	plugin_screenshare = shared_library(
		'screen-share',
		srcs_screenshare,
		include_directories: common_inc,
		dependencies: deps_screenshare,
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston,
		install_rpath: '$ORIGIN'
	)
	env_modmap += 'screen-share.so=@0@;'.format(plugin_screenshare.full_path())
endif

if get_option('systemd')
	dep_libsystemd = dependency('libsystemd', required: false)
	if not dep_libsystemd.found()
		error('systemd-notify requires libsystemd which was not found. Or, you can use \'-Dsystemd=false\'.')
	endif

	plugin_systemd_notify = shared_library(
		'systemd-notify',
		'systemd-notify.c',
		include_directories: common_inc,
		dependencies: [ dep_libweston_public, dep_libsystemd ],
		name_prefix: '',
		install: true,
		install_dir: dir_module_weston
	)
	env_modmap += 'systemd-notify.so=@0@;'.format(plugin_systemd_notify.full_path())
endif

weston_ini_config = configuration_data()
weston_ini_config.set('bindir', dir_bin)
weston_ini_config.set('libexecdir', dir_libexec)
configure_file(
	input: '../weston.ini.in',
	output: 'weston.ini',
	configuration: weston_ini_config
)
