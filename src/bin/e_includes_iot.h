#include "e_object.h"
#include "e_user.h"
#include "e_path.h"
#include "e_error.h"
#ifndef HAVE_IOT
#include "e_zone.h"
#include "e_desk.h"
#include "e_pixmap.h"
#include "e_comp_object.h"
#include "e_util_transform.h"
#endif//HAVE_IOT
#include "e_client_common.h"
#ifndef HAVE_IOT
#include "e_pointer.h"
#endif//HAVE_IOT
#include "e_config.h"
#include "e_config_data.h"
#include "e_module.h"
#ifndef HAVE_IOT
#include "e_icon.h"
#include "e_init.h"
#include "e_focus.h"
#include "e_place.h"
#include "e_resist.h"
#endif//HAVE_IOT
#include "e_signals.h"
#ifndef HAVE_IOT
#include "e_layout.h"
#include "e_theme.h"
#include "e_dnd.h"
#include "e_bindings.h"
#include "e_actions.h"
#include "e_test_helper.h"
#include "e_info_server.h"
#include "e_maximize.h"
#endif//HAVE_IOT
#include "e_prefix.h"
#ifndef HAVE_IOT
#include "e_grabinput.h"
#include "e_bg.h"
#include "e_win.h"
#include "e_zoomap.h"
#include "e_dialog.h"
#include "e_screensaver.h"
#include "e_dpms.h"
#include "e_eom.h"
#include "e_obj_dialog.h"
#include "e_mouse.h"
#include "e_msgbus.h"
#include "e_scale.h"
#endif//HAVE_IOT
#include "e_env.h"
#include "e_log.h"
#ifndef HAVE_IOT
#include "e_dbusmenu.h"
#include "e_comp_screen.h"
#include "e_comp.h"
#endif//HAVE_IOT
#include "e_comp_cfdata.h"
#ifndef HAVE_IOT
#include "e_comp_canvas.h"
#include "e_utils.h"//evas dependent
#include "e_hints.h"
#include "e_plane.h"
#include "e_plane_renderer.h"
#include "e_output.h"
#include "e_hwc.h"
#include "e_hwc_planes.h"
#include "e_hwc_windows.h"
#include "e_hwc_window.h"
#include "e_hwc_window_queue.h"
#include "e_comp_wl.h"//evas dependent
#include "e_comp_wl_data.h"
#include "e_comp_wl_input.h"//evas dependent
#include "e_uuid_store.h"
#ifdef HAVE_WAYLAND_TBM
# include "e_comp_wl_tbm.h"
#endif
#include "e_comp_wl_rsm.h"
#include "e_comp_wl_screenshooter.h"
#include "e_comp_wl_viewport.h"
#include "e_comp_wl_shell.h"
#include "e_policy.h"
#include "e_policy_conformant.h"
#include "e_policy_visibility.h"
#include "e_magnifier.h"
#include "e_process.h"
#include "e_splitlayout.h"
#include "e_slot.h"
#endif//HAVE_IOT
#include "e_privilege.h"
#include "e_security.h"
#include "e_main.h"
#include "e_keyrouter.h"
#ifndef HAVE_IOT
#include "e_gesture.h"
#include "e_dbus_conn.h"
#include "e_xdg_shell_v6.h"
#endif//HAVE_IOT
#include "e_input.h"
#include "e_devicemgr.h"
#ifndef HAVE_IOT
#include "e_video_debug.h"
#include "e_client_video.h"
#include "e_zone_video.h"
#include "e_comp_wl_video.h"
#include "e_comp_wl_video_buffer.h"
#endif//HAVE_IOT
