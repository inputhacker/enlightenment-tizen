E_CPPFLAGS = \
-I$(top_builddir) \
-I$(top_builddir)/src/bin \
-I$(top_srcdir) \
-I$(top_srcdir)/src/bin \
@e_cflags@ \
@cf_cflags@ \
@VALGRIND_CFLAGS@ \
@EDJE_DEF@ \
@WAYLAND_CFLAGS@ \
@WAYLAND_TBM_CFLAGS@ \
@TIZEN_REMOTE_SURFACE_CFLAGS@ \
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
src/bin/e_comp_wl.h

if HAVE_WAYLAND_TBM
ENLIGHTENMENTHEADERS += \
src/bin/e_comp_wl_tbm.h
if HAVE_REMOTE_SURFACE
ENLIGHTENMENTHEADERS += \
 src/bin/e_comp_wl_rsm.h
endif
endif

ENLIGHTENMENTHEADERS += \
src/bin/services/e_service_gesture.h \
src/bin/services/e_service_lockscreen.h \
src/bin/services/e_service_quickpanel.h \
src/bin/services/e_service_region.h \
src/bin/services/e_service_volume.h \
src/bin/services/e_service_indicator.h \
src/bin/services/e_service_cbhm.h \
src/bin/e_policy.h \
src/bin/e_policy_keyboard.h \
src/bin/e_policy_private_data.h \
src/bin/e_policy_wl.h \
src/bin/e_policy_wl_display.h \
src/bin/e_process.h

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
src/bin/e_comp_wl_screenshooter_server.c \
src/bin/e_comp_wl_screenshooter_server.h \
src/bin/e_comp_wl_data.c \
src/bin/e_comp_wl_input.c \
src/bin/e_comp_wl.c \
src/bin/tizen-surface-protocol.c \
$(ENLIGHTENMENTHEADERS)

if HAVE_WAYLAND_TBM
enlightenment_src += \
src/bin/e_comp_wl_tbm.c
if HAVE_REMOTE_SURFACE
enlightenment_src += \
src/bin/e_comp_wl_rsm.c
endif
endif

enlightenment_src += \
src/bin/services/e_service_gesture.c \
src/bin/services/e_service_lockscreen.c \
src/bin/services/e_service_quickpanel.c \
src/bin/services/e_service_region.c \
src/bin/services/e_service_volume.c \
src/bin/services/e_service_indicator.c \
src/bin/services/e_service_cbhm.c \
src/bin/e_policy.c \
src/bin/e_policy_conformant.c \
src/bin/e_policy_keyboard.c \
src/bin/e_policy_softkey.c \
src/bin/e_policy_stack.c  \
src/bin/e_policy_visibility.c \
src/bin/e_policy_wl.c \
src/bin/e_policy_wl_display.c \
src/bin/e_process.c

src_bin_enlightenment_CPPFLAGS = $(E_CPPFLAGS) -DEFL_BETA_API_SUPPORT -DEFL_EO_API_SUPPORT -DE_LOGGING=1 @WAYLAND_CFLAGS@ $(TTRACE_CFLAGS) $(DLOG_CFLAGS) $(POLICY_CFLAGS)
if HAVE_WAYLAND_TBM
src_bin_enlightenment_CPPFLAGS += @WAYLAND_TBM_CFLAGS@ @ECORE_DRM_CFLAGS@
if HAVE_REMOTE_SURFACE
src_bin_enlightenment_CPPFLAGS += @TIZEN_REMOTE_SURFACE_CFLAGS@
endif
endif
if HAVE_HWC
src_bin_enlightenment_CPPFLAGS += @HWC_CFLAGS@
endif

src_bin_enlightenment_SOURCES = \
src/bin/e_main.c \
$(enlightenment_src)

src_bin_enlightenment_LDFLAGS = -export-dynamic
src_bin_enlightenment_LDADD = @e_libs@ @dlopen_libs@ @cf_libs@ @VALGRIND_LIBS@ @WAYLAND_LIBS@ -lm @SHM_OPEN_LIBS@ $(TTRACE_LIBS) $(DLOG_LIBS) $(POLICY_LIBS)
if HAVE_WAYLAND_TBM
src_bin_enlightenment_LDADD += @WAYLAND_TBM_LIBS@ @ECORE_DRM_LIBS@
if HAVE_REMOTE_SURFACE
src_bin_enlightenment_LDADD += @TIZEN_REMOTE_SURFACE_LIBS@
endif
endif
if HAVE_HWC
src_bin_enlightenment_LDADD += @HWC_LIBS@
endif

src_bin_enlightenment_info_SOURCES = \
src/bin/e.h \
src/bin/e_info_client.c
src_bin_enlightenment_info_LDADD = @E_INFO_LIBS@
src_bin_enlightenment_info_CPPFLAGS = $(E_CPPFLAGS) @E_INFO_CFLAGS@

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
