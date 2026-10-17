set(DMOD_MODULE_NAME ntpprobe)
set(DMOD_STACK_SIZE 2048)
dmod_add_executable(ntpprobe ${PROJECT_VERSION} ntpprobe.c)
target_link_libraries(ntpprobe dmntp_if)
