AC_PREREQ(2.53)

m4_define([mwb_version], [0.1.0])

AC_INIT(netpanel, [mwb_version], http://www.moblin.org/)
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR(netpanel/moblin-panel-internet.c)
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AC_PROG_CXX
AC_STDC_HEADERS
AC_PROG_LIBTOOL

MWB_VERSION=mwb_version
AC_SUBST(MWB_VERSION)

# ****
# i18n
# ****

AS_ALL_LINGUAS
GETTEXT_PACKAGE=netpanel
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE",
                   [The gettext package name])
AM_GLIB_DEFINE_LOCALEDIR(LOCALEDIR)
AM_GLIB_GNU_GETTEXT

IT_PROG_INTLTOOL([0.40.4])

dnl Both the panel and the browser need the following
PKG_CHECK_MODULES(MOZHELPER, mozhelper-1.0)
PKG_CHECK_MODULES(NBTK, nbtk-1.2)
PKG_CHECK_MODULES(GTK, gtk+-2.0)

NBTKDATADIR=`${PKG_CONFIG} --variable=prefix nbtk-1.2`"/share/nbtk"
AC_SUBST(NBTKDATADIR)

PKG_CHECK_MODULES(MPL, moblin-panel)
PKG_CHECK_MODULES(NETPANEL, clutter-x11-1.0 clutter-1.0)

AC_ARG_ENABLE([cache],
              [AC_HELP_STRING([--enable-cache],
                              [Enable Nbtk image cache generation])],
              [],
              [enable_cache=no])

AS_IF([test "x$enable_cache" = "xyes"],
      [
      AC_CHECK_PROG([NBTK_CREATE_IMAGE_CACHE],
                [nbtk-create-image-cache],
                [$(which nbtk-create-image-cache)])
])

AM_CONDITIONAL([ENABLE_CACHE],   [test "x$enable_cache" = "xyes"])

AC_OUTPUT([
Makefile
common/Makefile
netpanel/Makefile
data/Makefile
data/netpanel/Makefile
po/Makefile.in
])
