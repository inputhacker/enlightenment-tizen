E_CPPFLAGS = \
-I$(top_builddir) \
-I$(top_builddir)/src/bin \
-I$(top_builddir)/src/bin/video \
-I$(top_srcdir) \
-I$(top_srcdir)/src/bin \
-I$(top_srcdir)/src/bin/video \
@e_cflags@ \
@cf_cflags@ \
@VALGRIND_CFLAGS@ \
@EDJE_DEF@ \
@WAYLAND_CFLAGS@ \
@WAYLAND_TBM_CFLAGS@ \
-DE_BINDIR=\"$(bindir)\" \
-DPACKAGE_BIN_DIR=\"@PACKAGE_BIN_DIR@\" \
-DPACKAGE_LIB_DIR=\"@PACKAGE_LIB_DIR@\" \
-DPACKAGE_DATA_DIR=\"@PACKAGE_DATA_DIR@\" \
-DLOCALE_DIR=\"@LOCALE_DIR@\" \
-DPACKAGE_SYSCONF_DIR=\"@PACKAGE_SYSCONF_DIR@\"

bin_PROGRAMS = \
src/bin/enlightenment \
src/bin/enlightenment_info

#internal_bindir = $(libdir)/enlightenment/utils
#internal_bin_PROGRAMS =

ENLIGHTENMENTHEADERS = \
src/bin/e_actions.h \
src/bin/e_bg.h \
src/bin/e_bindings.h \
src/bin/e_client.h \
src/bin/e_comp.h \
src/bin/e_comp_canvas.h \
src/bin/e_comp_cfdata.h \
src/bin/e_comp_object.h \
src/bin/e_config_data.h \
src/bin/e_config.h \
src/bin/e_dbusmenu.h \
src/bin/e_desk.h \
src/bin/e_dialog.h \
src/bin/e_dnd.h \
src/bin/e_dpms.h \
src/bin/e_env.h \
src/bin/e_eom.h \
src/bin/e_error.h \
src/bin/e_focus.h \
src/bin/e_grabinput.h \
src/bin/e.h \
src/bin/e_hints.h \
src/bin/e_icon.h \
src/bin/e_includes.h \
src/bin/e_info_shared_types.h \
src/bin/e_info_server.h \
src/bin/e_init.h \
src/bin/e_layout.h \
src/bin/e_log.h \
src/bin/e_main.h \
src/bin/e_maximize.h \
src/bin/e_module.h \
src/bin/e_mouse.h \
src/bin/e_msgbus.h \
src/bin/e_obj_dialog.h \
src/bin/e_object.h \
src/bin/e_output.h \
src/bin/e_path.h \
src/bin/e_pixmap.h \
src/bin/e_place.h \
src/bin/e_plane.h \
src/bin/e_pointer.h \
src/bin/e_prefix.h \
src/bin/e_plane_renderer.h \
src/bin/e_resist.h \
src/bin/e_scale.h \
src/bin/e_screensaver.h \
src/bin/e_signals.h \
src/bin/e_slot.h \
src/bin/e_splitlayout.h \
src/bin/e_test_helper.h \
src/bin/e_theme.h \
src/bin/e_user.h \
src/bin/e_utils.h \
src/bin/e_win.h \
src/bin/e_zoomap.h \
src/bin/e_zone.h \
src/bin/e_util_transform.h \
src/bin/e_comp_screen.h \
src/bin/e_info_protocol.h \
src/bin/e_uuid_store.h \
src/bin/e_comp_wl_data.h \
src/bin/e_comp_wl_input.h \
src/bin/e_comp_wl.h \
src/bin/e_hwc.h \
src/bin/e_hwc_planes.h \
src/bin/e_hwc_windows.h \
src/bin/e_hwc_window.h \
src/bin/e_hwc_window_queue.h

if HAVE_WAYLAND_TBM
ENLIGHTENMENTHEADERS += \
src/bin/e_comp_wl_tbm.h
endif

ENLIGHTENMENTHEADERS += \
src/bin/e_comp_wl_rsm.h \
src/bin/video/e_comp_wl_video.h \
src/bin/video/e_comp_wl_video_buffer.h \
src/bin/video/e_client_video.h \
src/bin/video/e_zone_video.h \
src/bin/video/e_video_debug.h \
src/bin/e_comp_wl_viewport.h \
src/bin/e_comp_wl_screenshooter.h \
src/bin/e_comp_wl_shell.h \
src/bin/e_comp_wl_capture.h \
src/bin/services/e_service_gesture.h \
src/bin/services/e_service_lockscreen.h \
src/bin/services/e_service_quickpanel.h \
src/bin/services/e_service_region.h \
src/bin/services/e_service_volume.h \
src/bin/services/e_service_indicator.h \
src/bin/services/e_service_cbhm.h \
src/bin/services/e_service_scrsaver.h \
src/bin/services/e_service_softkey.h \
src/bin/e_policy.h \
src/bin/e_policy_conformant.h \
src/bin/e_policy_visibility.h \
src/bin/e_policy_private_data.h \
src/bin/e_policy_wl.h \
src/bin/e_policy_wl_display.h \
src/bin/e_policy_appinfo.h \
src/bin/e_magnifier.h \
src/bin/e_process.h \
src/bin/e_privilege.h \
src/bin/e_security.h \
src/bin/e_keyrouter.h \
src/bin/e_keyrouter_private.h \
src/bin/e_gesture.h \
src/bin/e_input.h \
src/bin/e_dbus_conn.h \
src/bin/e_xdg_shell_v6.h \
src/bin/e_devicemgr.h \
src/bin/e_devicemgr_private.h \
src/bin/e_msg.h

enlightenment_src = \
src/bin/e_actions.c \
src/bin/e_bg.c \
src/bin/e_bindings.c \
src/bin/e_client.c \
src/bin/e_comp.c \
src/bin/e_comp_canvas.c \
src/bin/e_comp_cfdata.c \
src/bin/e_comp_object.c \
src/bin/e_comp_screen.c \
src/bin/e_config.c \
src/bin/e_config_data.c \
src/bin/e_dbusmenu.c \
src/bin/e_desk.c \
src/bin/e_dialog.c \
src/bin/e_dpms.c \
src/bin/e_dnd.c \
src/bin/e_env.c \
src/bin/e_eom.c \
src/bin/e_error.c \
src/bin/e_focus.c \
src/bin/e_grabinput.c \
src/bin/e_hints.c \
src/bin/e_icon.c \
src/bin/e_info_server.c \
src/bin/e_init.c \
src/bin/e_layout.c \
src/bin/e_log.c \
src/bin/e_maximize.c \
src/bin/e_module.c \
src/bin/e_mouse.c \
src/bin/e_msgbus.c \
src/bin/e_obj_dialog.c \
src/bin/e_object.c \
src/bin/e_path.c \
src/bin/e_pixmap.c \
src/bin/e_place.c \
src/bin/e_plane.c \
src/bin/e_pointer.c \
src/bin/e_prefix.c \
src/bin/e_plane_renderer.c \
src/bin/e_resist.c \
src/bin/e_scale.c \
src/bin/e_screensaver.c \
src/bin/e_signals.c \
src/bin/e_slot.c \
src/bin/e_splitlayout.c \
src/bin/e_test_helper.c \
src/bin/e_theme.c \
src/bin/e_user.c \
src/bin/e_utils.c \
src/bin/e_win.c \
src/bin/e_zoomap.c \
src/bin/e_zone.c \
src/bin/e_util_transform.c \
src/bin/e_output.c \
src/bin/e_info_protocol.c \
src/bin/e_uuid_store.c \
src/bin/session-recovery-protocol.c \
src/bin/session-recovery-server-protocol.h \
src/bin/e_comp_wl_data.c \
src/bin/e_comp_wl_input.c \
src/bin/e_comp_wl.c \
src/bin/e_hwc.c \
src/bin/e_hwc_planes.c \
src/bin/e_hwc_windows.c \
src/bin/e_hwc_window.c \
src/bin/e_hwc_window_queue.c \
$(ENLIGHTENMENTHEADERS)

if HAVE_WAYLAND_TBM
enlightenment_src += \
src/bin/e_comp_wl_tbm.c
endif

enlightenment_src += \
src/bin/e_comp_wl_rsm.c \
src/bin/video/e_comp_wl_video.c \
src/bin/video/e_comp_wl_video_buffer.c \
src/bin/video/e_client_video.c \
src/bin/video/e_zone_video.c \
src/bin/video/e_video_debug.c \
src/bin/video/iface/e_video_hwc.c \
src/bin/video/iface/e_video_hwc_planes.c \
src/bin/video/iface/e_video_hwc_windows.c \
src/bin/video/iface/e_video_external.c \
src/bin/video/iface/e_video_fallback.c \
src/bin/e_comp_wl_viewport.c \
src/bin/e_comp_wl_screenshooter.c \
src/bin/e_comp_wl_shell.c \
src/bin/e_comp_wl_capture.c \
src/bin/services/e_service_gesture.c \
src/bin/services/e_service_lockscreen.c \
src/bin/services/e_service_quickpanel.c \
src/bin/services/e_service_region.c \
src/bin/services/e_service_volume.c \
src/bin/services/e_service_indicator.c \
src/bin/services/e_service_cbhm.c \
src/bin/services/e_service_scrsaver.c \
src/bin/services/e_service_softkey.c \
src/bin/e_policy.c \
src/bin/e_policy_conformant.c \
src/bin/e_policy_softkey.c \
src/bin/e_policy_stack.c  \
src/bin/e_policy_visibility.c \
src/bin/e_policy_wl.c \
src/bin/e_policy_wl_display.c \
src/bin/e_policy_appinfo.c \
src/bin/e_magnifier.c \
src/bin/e_process.c \
src/bin/e_privilege.c \
src/bin/e_security.c \
src/bin/e_keyrouter_events.c \
src/bin/e_keyrouter_list.c \
src/bin/e_keyrouter_conf.c \
src/bin/e_keyrouter_wl.c \
src/bin/e_keyrouter.c \
src/bin/e_gesture.c \
src/bin/e_input_private.h \
src/bin/e_input.c \
src/bin/e_input_inputs.c \
src/bin/e_input_device.c \
src/bin/e_input_evdev.c \
src/bin/e_dbus_conn.c \
src/bin/e_xdg_shell_v6.c \
src/bin/e_devicemgr.c \
src/bin/e_devicemgr_conf.c \
src/bin/e_devicemgr_block.c \
src/bin/e_devicemgr_input.c \
src/bin/e_devicemgr_inputgen.c \
src/bin/e_devicemgr_wl.c \
src/bin/e_msg.c

src_bin_enlightenment_CPPFLAGS = $(E_CPPFLAGS) -DEFL_BETA_API_SUPPORT -DEFL_EO_API_SUPPORT -DE_LOGGING=2 @WAYLAND_CFLAGS@ $(TTRACE_CFLAGS) $(DLOG_CFLAGS) $(PIXMAN_CFLAGS) $(POLICY_CFLAGS)
if HAVE_LIBGOMP
src_bin_enlightenment_CPPFLAGS += -fopenmp
endif
if HAVE_WAYLAND_TBM
src_bin_enlightenment_CPPFLAGS += @WAYLAND_TBM_CFLAGS@
endif
if HAVE_HWC
src_bin_enlightenment_CPPFLAGS += @HWC_CFLAGS@
endif
if HAVE_SYSTEMD
src_bin_enlightenment_CPPFLAGS += @SYSTEMD_CFLAGS@
endif
src_bin_enlightenment_CPPFLAGS += @LIBINPUT_CFLAGS@

src_bin_enlightenment_SOURCES = \
src/bin/e_main.c \
$(enlightenment_src)

src_bin_enlightenment_LDFLAGS = -export-dynamic
if HAVE_LIBGOMP
src_bin_enlightenment_LDFLAGS += -fopenmp
endif
src_bin_enlightenment_LDADD = @e_libs@ @dlopen_libs@ @cf_libs@ @VALGRIND_LIBS@ @WAYLAND_LIBS@ -lm @SHM_OPEN_LIBS@ $(TTRACE_LIBS) $(DLOG_LIBS) $(PIXMAN_LIBS) $(POLICY_LIBS)
if HAVE_WAYLAND_TBM
src_bin_enlightenment_LDADD += @WAYLAND_TBM_LIBS@
endif
if HAVE_HWC
src_bin_enlightenment_LDADD += @HWC_LIBS@
endif
if HAVE_SYSTEMD
src_bin_enlightenment_LDFLAGS += @SYSTEMD_LIBS@
endif
src_bin_enlightenment_LDFLAGS += @LIBINPUT_LIBS@

src_bin_enlightenment_info_SOURCES = \
src/bin/e.h \
src/bin/e_info_client.c
src_bin_enlightenment_info_LDADD = @E_INFO_LIBS@
src_bin_enlightenment_info_CPPFLAGS = $(E_CPPFLAGS) @E_INFO_CFLAGS@
src_bin_enlightenment_info_LDFLAGS = -pie
src_bin_enlightenment_info_CPPFLAGS += -fPIE

# HACK! why install-data-hook? install-exec-hook is run after bin_PROGRAMS
# and before internal_bin_PROGRAMS are installed. install-data-hook is
# run after both
setuid_root_mode = a=rx,u+xs
installed_headersdir = $(prefix)/include/enlightenment
installed_headers_DATA = $(ENLIGHTENMENTHEADERS)

PHONIES += e enlightenment install-e install-enlightenment
e: $(bin_PROGRAMS)
enlightenment: e
install-e: install-binPROGRAMS
install-enlightenment: install-e 
