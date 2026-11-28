project(
  'ami-smbios-trace',
  'cpp',
  version: '1.0',
  default_options: [
    'cpp_std=c++23',
    'warning_level=3',
  ],
)

ipmid_dep = dependency('libipmid')
phosphor_logging_dep = dependency('phosphor-logging')
sdbusplus_dep = dependency('sdbusplus')

shared_module(
  'ami-smbios-trace',
  'trace.cpp',
  implicit_include_directories: false,
  dependencies: [
    ipmid_dep,
    phosphor_logging_dep,
    sdbusplus_dep,
  ],
  install: true,
  install_dir: get_option('libdir') / 'ipmid-providers',
)
