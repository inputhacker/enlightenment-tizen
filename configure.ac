##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_define([v_maj], [0])
m4_define([v_min], [20])
m4_define([v_mic], [0])
m4_define([v_rev], m4_esyscmd([(git rev-list --count HEAD 2>/dev/null || echo 0) | tr -d '\n']))dnl
##--   When released, remove the dnl on the below line
m4_undefine([v_rev])
m4_define([relname], [0.20.0])
##--   When doing snapshots - change soname. remove dnl on below line
m4_define([relname], [ver-autocannoli-0.20])
dnl m4_define([v_rel], [-release relname])
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_ifdef([v_rev], [m4_define([v_ver], [v_maj.v_min.v_mic.v_rev])], [m4_define([v_ver], [v_maj.v_min.v_mic])])
m4_define([lt_cur], m4_eval(v_maj + v_min))
m4_define([lt_rev], v_mic)
m4_define([lt_age], v_min)
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##

AC_INIT([enlightenment], [v_ver], [enlightenment-devel@lists.sourceforge.net])
AC_PREREQ([2.52])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_MACRO_DIR([m4])
AC_CANONICAL_BUILD
AC_CANONICAL_HOST

AC_CONFIG_HEADERS([config.h])
AH_TOP([
#ifndef EFL_CONFIG_H__
#define EFL_CONFIG_H__
])
AH_BOTTOM([
#endif /* EFL_CONFIG_H__ */
])

AM_INIT_AUTOMAKE([1.11 dist-xz -Wno-portability])
AM_SILENT_RULES([yes])

AC_USE_SYSTEM_EXTENSIONS
AC_PROG_MKDIR_P
AC_C_BIGENDIAN
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_FUNC_ALLOCA

LT_INIT([disable-static])

##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
m4_ifdef([v_rev], , [m4_define([v_rev], [0])])
m4_ifdef([v_rel], , [m4_define([v_rel], [])])
AC_DEFINE_UNQUOTED(VMAJ, [v_maj], [Major version])
AC_DEFINE_UNQUOTED(VMIN, [v_min], [Minor version])
AC_DEFINE_UNQUOTED(VMIC, [v_mic], [Micro version])
AC_DEFINE_UNQUOTED(VREV, [v_rev], [Revison])
AC_DEFINE_UNQUOTED(E_RELEASE_BUILD, [1], [doin it])
version_info="lt_cur:lt_rev:lt_age"
release_info="v_rel"
AC_SUBST([version_info])
AC_SUBST([release_info])
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##
##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##--##

AC_ARG_VAR([TIZEN_REL_VERSION], [Tizen release version])
if test x"${TIZEN_REL_VERSION}" != x""; then
       AC_DEFINE_UNQUOTED([TIZEN_REL_VERSION], ["${TIZEN_REL_VERSION}"], [Tizen release version])
       AC_SUBST(TIZEN_REL_VERSION)
else
       echo "TIZEN_REL_VERSION is empty !"
fi

release=relname
AC_SUBST([release])

PKG_PROG_PKG_CONFIG

EFL_CHECK_PATH_MAX

AC_CHECK_FUNCS(setenv)
AC_CHECK_FUNCS(unsetenv)
AC_CHECK_FUNCS(clearenv)
AC_CHECK_HEADERS(features.h)

case "$host_os" in
   darwin*)
      AC_DEFINE([environ], [(*_NSGetEnviron())],
                ["apple doesn't follow POSIX in this case."])
      ;;
   *)
      ;;
esac
AC_TRY_COMPILE([
#define _GNU_SOURCE 1
#include <unistd.h>
    ],[
extern char **environ;
    ],[
AC_DEFINE(HAVE_ENVIRON, 1, [Have environ var])
    ])

efl_version="1.15.2"
AC_SUBST(efl_version)

AC_CHECK_HEADERS([sys/timerfd.h sys/ptrace.h arpa/inet.h netinet/in.h])

AC_CHECK_HEADERS(fnmatch.h,, AC_MSG_ERROR([Cannot find fnmatch.h. Make sure your CFLAGS environment variable contains include lines for the location of this file]))

AC_CHECK_FUNCS(fnmatch, res=yes, res=no)
if test "x$res" = "xno"; then
  AC_CHECK_LIB(fnmatch, fnmatch, res=yes, res=no)
  if test "x$res" = "xno"; then
    AC_MSG_ERROR([Cannot find fnmatch() in neither libc nor libfnmatch])
  else
    fnmatch_libs="-lfnmatch"
  fi
fi

AC_SUBST([fnmatch_libs])

dnl AC_E_CHECK_PKG(VALGRIND, [valgrind >= 2.4.0], [], [:])
AC_SUBST([VALGRIND_CFLAGS])
AC_SUBST([VALGRIND_LIBS])

MODULE_ARCH="$host_os-$host_cpu-relname"
AC_SUBST([MODULE_ARCH])
AC_DEFINE_UNQUOTED(MODULE_ARCH, "$MODULE_ARCH", "Module architecture")

if test "x${bindir}" = 'xNONE'; then
  if test "x${prefix}" = "xNONE"; then
    PACKAGE_BIN_DIR="${ac_default_prefix}/bin"
  else
    PACKAGE_BIN_DIR="${prefix}/bin"
  fi
else
  PACKAGE_BIN_DIR="${bindir}"
fi
AC_SUBST([PACKAGE_BIN_DIR])

if test "x${libdir}" = 'xNONE'; then
  if test "x${prefix}" = "xNONE"; then
    PACKAGE_LIB_DIR="${ac_default_prefix}/lib"
  else
    PACKAGE_LIB_DIR="${prefix}/lib"
  fi
else
  PACKAGE_LIB_DIR="${libdir}"
fi
AC_SUBST([PACKAGE_LIB_DIR])

if test "x${prefix}" = "xNONE"; then
  PACKAGE_DATA_DIR="${ac_default_prefix}/share/${PACKAGE}"
  LOCALE_DIR="${ac_default_prefix}/share/locale"
  PACKAGE_SYSCONF_DIR="${ac_default_prefix}/etc"
else
  PACKAGE_DATA_DIR="${prefix}/share/${PACKAGE}"
  LOCALE_DIR="${prefix}/share/locale"
  PACKAGE_SYSCONF_DIR="${sysconfdir}"
fi
AC_SUBST([PACKAGE_DATA_DIR])
AC_SUBST([LOCALE_DIR])
AC_SUBST([PACKAGE_SYSCONF_DIR])

cf_cflags=""
cf_libs=""
PCPPFLAGS="${CPPFLAGS}"
CPPFLAGS="${CPPFLAGS} -I /System/Library/Frameworks/CoreFoundation.framework/Headers"
AC_CHECK_HEADERS(CFBase.h,
  [
    cf_cflags="-I /System/Library/Frameworks/CoreFoundation.framework/Headers"
    cf_cflags="${cf_cflags} -I /System/Library/Frameworks/IOKit.framework/Headers"
    cf_libs="-framework 'CoreFoundation' -framework 'IOKit'"
  ]
)
CPPFLAGS="${PCPPFLAGS}"
AC_SUBST([cf_cflags])
AC_SUBST([cf_libs])

execinfo_libs=""
AC_CHECK_HEADERS([execinfo.h], [have_execinfo="yes"], [have_execinfo="no"])
if test "x${have_execinfo}" = "xyes" ; then
  AC_MSG_CHECKING([whether backtrace_symbols_fd() is in libc])

  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
      [[
#include <stdlib.h>
#include <execinfo.h>
      ]],
      [[
backtrace_symbols_fd(NULL, 0, 0);
      ]])],
    [link_execinfo="yes"],
    [link_execinfo="no"])

  AC_MSG_RESULT([${link_execinfo}])
fi

if test "x${link_execinfo}" = "xno" ; then
  AC_MSG_CHECKING([whether backtrace_symbols_fd() is in libexecinfo])

  LDFLAGS_save="${LDFLAGS}"
  LDFLAGS="${LDFLAGS} -lexecinfo"
  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
      [[
#include <stdlib.h>
#include <execinfo.h>
      ]],
      [[
backtrace_symbols_fd(NULL, 0, 0);
      ]])],
    [
      link_execinfo="yes"
      execinfo_libs="-lexecinfo"
    ],
    [link_execinfo="no"])
  LDFLAGS="${LDFLAGS_save}"

  AC_MSG_RESULT([${link_execinfo}])
fi

AC_CHECK_LIB(dl, dlopen, dlopen_libs=-ldl)
AC_SUBST([dlopen_libs])

AC_MSG_CHECKING([whether to enable Files menu item])
AC_ARG_ENABLE([files],
  AS_HELP_STRING([--disable-files],[disable Files menu item @<:@default=enabled@:>@]),
    [e_cv_enable_files=$enableval],
    AC_CACHE_VAL([e_cv_enable_files], [e_cv_enable_files=yes]))
if test "[${e_cv_enable_files}]" = yes; then
  AC_DEFINE_UNQUOTED([ENABLE_FILES],[1],[enable Files menu item])
fi
AC_MSG_RESULT([$e_cv_enable_files])


# verify "wayland-only" early to adapt dependencies

have_wayland_only=no
AC_ARG_ENABLE([wayland],
  AS_HELP_STRING([--enable-wayland],[enable wayland support in enlightenment @<:@default=disabled@:>@]),
  [e_cv_want_wayland_only=$enableval],
  [e_cv_want_wayland_only=no])
AC_MSG_CHECKING([whether wayland support is enabled])
AC_MSG_RESULT([${e_cv_want_wayland_only}])

quick_init=no
AC_ARG_ENABLE([quick-init],
  AS_HELP_STRING([--enable-quick-init],[]),
  [
   quick_init=yes
   AC_DEFINE_UNQUOTED([ENABLE_QUICK_INIT], [1], [enable quick init])
  ],
  [quick_init=no])
AC_MSG_CHECKING([whether enlightenment quick initialization is enabled])
AC_MSG_RESULT([${quick_init}])

function_trace=no
AC_ARG_ENABLE([function-trace],
  AS_HELP_STRING([--enable-function-trace],[]),
  [
   function_trace=yes
   AC_DEFINE_UNQUOTED([ENABLE_FUNCTION_TRACE], [1], [enable function trace])
  ],
  [function_trace=no])
AC_MSG_CHECKING([whether enlightenment function trace is enabled])
AC_MSG_RESULT([${function_trace}])

e_requires="\
  evas >= $efl_version \
  ecore >= $efl_version \
  ecore-evas >= $efl_version \
  ecore-input >= $efl_version \
  ecore-input-evas >= $efl_version \
  ecore-con >= $efl_version \
  ecore-ipc >= $efl_version \
  ecore-file >= $efl_version \
  eet >= $efl_version \
  edje >= $efl_version \
  eina >= $efl_version \
  eldbus >= $efl_version \
  eio >= $efl_version \
  eo >= $efl_version \
  eeze >= $efl_version \
  libinput \
  libtbm \
  libtdm >= "1.0.0" \
  "

PKG_CHECK_MODULES(E_INFO, [
  eina >= ${efl_version}
  ecore >= ${efl_version}
  eldbus >= ${efl_version}
  xkbcommon
])

PKG_CHECK_EXISTS([xkeyboard-config],[
  xkb_base=`$PKG_CONFIG --variable=xkb_base xkeyboard-config 2>/dev/null`
  AC_DEFINE_UNQUOTED([XKB_BASE],["$xkb_base"],[Define to the base directory for X keyboard configuration data])
])

profile="SLOW_PC"
AC_ARG_WITH(profile,
  AS_HELP_STRING([--with-profile=TARGET_PROFILE], 
  [specify a target format profile of:
                                    LOWRES_PDA, MEDIUMRES_PDA, HIRES_PDA, SLOW_PC, MEDIUM_PC, FAST_PC]),
  [profile=$withval])
AC_MSG_NOTICE([Enlightenment build profile set to $profile])
EDJE_DEF="-DLOWRES_PDA=1 -DMEDIUMRES_PDA=2 -DHIRES_PDA=3 -DSLOW_PC=4 -DMEDIUM_PC=5 -DFAST_PC=6"
EDJE_DEF=$EDJE_DEF" -DE_PROFILE"=$profile
AC_SUBST([EDJE_DEF])

PKG_CHECK_MODULES(EET, [eet >= ${efl_version}])
EFL_WITH_BIN([edje], [edje-cc], [edje_cc])
EFL_WITH_BIN([eet], [eet-eet], [eet])
EFL_WITH_BIN([eldbus], [eldbus_codegen], [eldbus-codegen])

AC_DEFINE(E_INTERNAL, 1, "This define can be used to wrap internal E stuff, as config.h isn't exported")

have_enotify=no
AM_CONDITIONAL([HAVE_ENOTIFY], [false])
AC_ARG_ENABLE([enotify],
  AS_HELP_STRING([--disable-enotify], [disable Enotify support @<:@default=detect@:>@]),
  [e_cv_want_enotify=$enableval],
  AC_CACHE_VAL([e_cv_want_enotify], [e_cv_want_enotify=yes])
)
if test "x$e_cv_want_enotify" != "xno" ; then
  AC_E_CHECK_PKG(ENOTIFY, [ eldbus >= $efl_version ],
  [
    AC_DEFINE_UNQUOTED([HAVE_ENOTIFY], [1], [enable enotify])
  ], 
  [
    AC_MSG_NOTICE([enotify disabled])
    e_cv_want_enotify=no
  ])
else
  AC_MSG_NOTICE([enotify disabled])
    e_cv_want_enotify=no
fi
AC_SUBST([ENOTIFY_CFLAGS])
AC_SUBST([ENOTIFY_LIBS])

SHM_OPEN_LIBS=""
AC_MSG_CHECKING([whether shm_open() is present])
LIBS_save=${LIBS}
LIBS="${LIBS} -lrt"
AC_LINK_IFELSE(
   [AC_LANG_PROGRAM(
      [[
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
      ]],
      [[
int fd;
fd = shm_open("/", O_RDWR | O_CREAT, S_IRUSR |
S_IWUSR);
shm_unlink("/");
      ]])],
   [
    have_shm_open="yes"
    AC_DEFINE(HAVE_SHM_OPEN, 1, [Have shm_open() call])
    SHM_OPEN_LIBS="-lrt"
   ])
LIBS=${LIBS_save}
AC_MSG_RESULT([${have_shm_open}])
AC_SUBST(SHM_OPEN_LIBS)

if test "x${e_cv_want_wayland_only}" != "xno" || test "x${e_cv_want_wayland_clients}" != "xno";then
  PKG_CHECK_MODULES([WAYLAND], [wayland-server >= 1.8.0 xkbcommon uuid xdg-shell-unstable-v5-server xdg-shell-unstable-v6-server tizen-remote-surface-server scaler-server screenshooter-server tizen-extension-server tizen-launch-server tizen-surface-server tizen-dpms-server eom-server],
    [
      have_wayland=yes
      AC_DEFINE_UNQUOTED([HAVE_WAYLAND],[1],[enable wayland support])
    ])
else
  have_wayland=no
fi

if test "x${have_wayland}" = "xyes"; then
  if test "x${e_cv_want_wayland_only}" != "xno"; then
    have_wayland_only=yes
    AC_DEFINE_UNQUOTED([HAVE_WAYLAND_ONLY],[1],[enable wayland-only version of enlightenment])
  fi

  have_wayland_tbm=no
  AC_ARG_ENABLE([wayland-tbm],
    AS_HELP_STRING([--enable-wayland-tbm],[enable wayland-tbm @<:@default=enabled@:>@]),
    [e_cv_want_wayland_tbm=$enableval],
    [e_cv_want_wayland_tbm=yes])
  AC_MSG_CHECKING([whether wayland TBM support is enabled])
  AC_MSG_RESULT([${e_cv_want_wayland_tbm}])

  if test "x$e_cv_want_wayland_tbm" != "xno";then
    PKG_CHECK_MODULES([WAYLAND_TBM], [wayland-tbm-server],
      [
        have_wayland_tbm=yes
        AC_DEFINE_UNQUOTED([HAVE_WAYLAND_TBM],[1],[enable wayland-tbm support])
      ])
  else
    have_wayland_tbm=no
  fi

  have_hwc=no
  AC_ARG_ENABLE([hwc],
    AS_HELP_STRING([--enable-hwc],[enable hwc @<:@default=enabled@:>@]),
    [e_cv_want_hwc=$enableval],
    [e_cv_want_hwc=yes])
  AC_MSG_CHECKING([whether HWC support is enabled])
  AC_MSG_RESULT([${e_cv_want_hwc}])

  if test "x$e_cv_want_hwc" != "xno";then
    PKG_CHECK_MODULES([HWC], [gbm],
      [
        have_hwc=yes
        AC_DEFINE_UNQUOTED([HAVE_HWC],[1],[enable hwc support])
      ])
  else
    have_hwc=no
  fi
fi

# does this compiler support -fopenmp, does it have the include file
# <omp.h> and does it have libgomp ?

AC_MSG_CHECKING([for OpenMP])

safe_CFLAGS=$CFLAGS
CFLAGS="-fopenmp $mflag_primary -Werror"

AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <omp.h>
int main(int argc, char** argv)
{
  omp_set_dynamic(0);
  return 0;
}
])],
[
   have_libgomp=yes
   AC_MSG_RESULT([yes])
], [
   have_libgomp=no
   AC_MSG_RESULT([no])
])
CFLAGS=$safe_CFLAGS

if test "x$have_libgomp" != "xno"; then
AC_DEFINE([HAVE_LIBGOMP], [1], [Enable libgomp])
   have_libgomp=yes
else
AC_DEFINE([HAVE_LIBGOMP], [0], [Disable libgomp])
   have_libgomp=no
fi
AM_CONDITIONAL([HAVE_LIBGOMP], [test "x${have_libgomp}" = "xyes"])

# Check for ttrace header files
PKG_CHECK_MODULES(TTRACE,
		[ttrace],
		[have_ttrace="yes"], [have_ttrace="no"])

if test "x$have_ttrace" = "xyes"; then
	AC_DEFINE(ENABLE_TTRACE, 1, [ttrace available])
fi

# dlog
PKG_CHECK_EXISTS([dlog], [have_dlog="yes"], [have_dlog="no"])
AC_MSG_CHECKING([Have dlog logger])
AC_MSG_RESULT([${have_dlog}])
if test "x${have_dlog}" = "xyes"; then
    AC_DEFINE([HAVE_DLOG], [1], [Define to 1 if you have dlog])
    PKG_CHECK_MODULES(DLOG, dlog)
fi

AM_CONDITIONAL([HAVE_DLOG], [test "x${have_dlog}" = "xyes"])

AM_CONDITIONAL([HAVE_WAYLAND], [test "x${have_wayland}" = "xyes"])
AM_CONDITIONAL([HAVE_WAYLAND_TBM], [test "x${have_wayland_tbm}" = "xyes"])
AM_CONDITIONAL([HAVE_HWC], [test "x${have_hwc}" = "xyes"])

#pixman
PKG_CHECK_MODULES([PIXMAN], [pixman-1])

# Check for systemd
want_systemd="yes"
AC_ARG_ENABLE([systemd],
  [AS_HELP_STRING([--disable-systemd], [disable systemd @<:@default=enabled@:>@])],
  [
    if test "x${enableval}" = "xyes"; then
      want_systemd="yes"
    else
      want_systemd="no"
    fi
  ])
AC_MSG_CHECKING([whether systemd is enabled])
AC_MSG_RESULT([${want_systemd}])

have_systemd="no"
if test "x$want_systemd" = "xyes"; then
  PKG_CHECK_MODULES([SYSTEMD], [libsystemd >= 209],
    [
      have_systemd="yes"
      AC_DEFINE_UNQUOTED([HAVE_SYSTEMD], [1], [enable systemd support])
    ])
else
  have_systemd="no"
fi

AM_CONDITIONAL([HAVE_SYSTEMD], [test "x${have_systemd}" = "xyes"])

#libinput
PKG_CHECK_MODULES([LIBINPUT], [libinput])

have_extra_touch_event="no"
AC_CHECK_LIB(input, libinput_event_touch_get_major, [have_extra_touch_event="yes"])
if test "x${have_extra_touch_event}" = "xyes";then
AC_DEFINE_UNQUOTED([LIBINPUT_SUPPORT_EXTRA_TOUCH_EVENT], [1], [libinput support extra touch event])
fi

#capi-system-device
PKG_CHECK_MODULES([CAPI_SYSTEM_DEVICE],
                  [capi-system-device])
#cynara
PKG_CHECK_MODULES(CYNARA,
                  [cynara-client, cynara-creds-socket, cynara-session, libsmack],
                  [have_cynara="yes"], [have_cynara="no"])
if test "x${have_cynara}" = "xyes"; then
    AC_DEFINE([HAVE_CYNARA], [1], [Define to 1 if you have cynara])
fi

#tzsh-server
PKG_CHECK_MODULES(TZSH,
                  [tzsh-server],
                  [have_tzsh="yes"])

POLICY_CFLAGS="${CAPI_SYSTEM_DEVICE_CFLAGS} ${CYNARA_CFLAGS} ${TZSH_CFLAGS}"
POLICY_LIBS="${CAPI_SYSTEM_DEVICE_LIBS} ${CYNARA_LIBS} ${TZSH_LIBS}"
AC_SUBST(POLICY_CFLAGS)
AC_SUBST(POLICY_LIBS)

define([CHECK_MODULE_BUFFERQUEUE],
[
  if test "x${have_wayland_clients}" = "xyes" || test "x${e_cv_want_wayland_only}" = "xyes"; then
    AC_E_CHECK_PKG(BUFFERQUEUE, [ ecore >= $efl_version eina >= $efl_version ], [BUFFERQUEUE=true], [BUFFERQUEUE=false])
  else
    BUFFERQUEUE=false
  fi
])
AM_CONDITIONAL([HAVE_BUFFERQUEUE], [test "x${BUFFERQUEUE}" = "xtrue"])

AC_E_OPTIONAL_MODULE([bufferqueue], $have_wayland, [CHECK_MODULE_BUFFERQUEUE])

if test "x${have_wayland}" = "xyes"; then
  AC_DEFINE_UNQUOTED([HAVE_WAYLAND_ONLY],[1],[enable wayland-only version of enlightenment])
  have_wayland_only=yes
else
  have_wayland_only=no
fi

AM_CONDITIONAL([HAVE_WAYLAND_ONLY], [test "x${have_wayland_only}" = "xyes"])

PKG_CHECK_MODULES(E, [$e_requires])
requirements_e="\
evas >= ${efl_version} \
ecore >= ${efl_version} \
ecore-evas >= ${efl_version} \
ecore-con >= ${efl_version} \
ecore-ipc >= ${efl_version} \
ecore-file >= ${efl_version} \
eet >= ${efl_version} \
edje >= ${efl_version} \
eina >= ${efl_version} \
eldbus >= ${efl_version} \
eio >= ${efl_version} \
eo >= ${efl_version} \
eeze >= ${efl_version} \
"

e_libs="$E_LIBS $fnmatch_libs $execinfo_libs $DLOG_LIBS"
e_cflags="-DUSE_E_CONFIG_H $E_CFLAGS -D_F_ZONE_WINDOW_ROTATION_ -D_F_E_VIRTUAL_KEYBOARD_TYPE_ -D_F_E_CLIENT_NEW_CLIENT_POST_HOOK_ -D_F_E_COMP_OBJECT_INTERCEPT_HOOK_ -D_F_TRANSIENT_FOR_PATCH -D_F_DISABLE_BACKLIGHT_MOD_SUPPORT $DLOG_CFLAGS"
e_configflags="-DUSE_E_CONFIG_H -D_F_ZONE_WINDOW_ROTATION_ -D_F_E_VIRTUAL_KEYBOARD_TYPE_ -D_F_E_CLIENT_NEW_CLIENT_POST_HOOK_ -D_F_E_COMP_OBJECT_INTERCEPT_HOOK_ -D_F_TRANSIENT_FOR_PATCH -D_F_DISABLE_BACKLIGHT_MOD_SUPPORT"

AC_SUBST([e_libs])
AC_SUBST([e_cflags])
AC_SUBST([e_configflags])
AC_SUBST([requirements_e])

m4_ifndef([v_rel],
   [
    EFL_COMPILER_FLAG([-Wshadow])
    EFL_COMPILER_FLAG([-Wall])
    EFL_COMPILER_FLAG([-Wpointer-arith])
    EFL_COMPILER_FLAG([-W])
    EFL_COMPILER_FLAG([-Wno-missing-field-initializers])
    EFL_COMPILER_FLAG([-Wfloat-equal])
    EFL_COMPILER_FLAG([-Wuninitialized])
    EFL_COMPILER_FLAG([-Wundef])
    EFL_COMPILER_FLAG([-Wcast-align])
    EFL_COMPILER_FLAG([-Wformat=2])
   ])

SUID_CFLAGS=-fPIE
SUID_LDFLAGS=-pie
case "$host_os" in
   freebsd*|pcbsd*)
      SUID_CFLAGS=
      SUID_LDFLAGS=
      ;;
esac
AC_SUBST([SUID_CFLAGS])
AC_SUBST([SUID_LDFLAGS])

AC_OUTPUT([
Makefile
enlightenment.pc
])

# report
txt_strip() {
  echo "[$]@" | sed -e 's/^[[ \t]]*\([[^ \t]]*\)[[ \t]]*$/\1/g'
}

echo
cat << SUMMARY_EOF
Summary:
 * project.........: $PACKAGE $VERSION
 * prefix..........: $(txt_strip $prefix)
 * CFLAGS..........: $(txt_strip $CFLAGS)
 * LDFLAGS.........: $(txt_strip $LDFLAGS)
 * edje_cc.........: ${edje_cc}
 * eet.............: ${eet_eet}
 * eldbus-codegen..: ${eldbus_codegen}
SUMMARY_EOF

echo

UNUSED_MODS=""
for mod in $UNUSED_OPTIONAL_MODULES; do
  UNUSED_MODS="$UNUSED_MODS${COLOR_HRED}$mod${COLOR_END} "
done

cat << MODULES_EOF
Modules:
 * disabled........: $(txt_strip $UNUSED_MODS)
MODULES_EOF
echo

cat << COMPILE_EOF
Compilation........: make (or gmake)
COMPILE_EOF
echo

cat << INSTALL_EOF
Installation.......: make all install (as root if needed, with 'su' or 'sudo')
INSTALL_EOF
echo
