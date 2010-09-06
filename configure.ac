AC_PREREQ(2.53)

m4_define([mwb_version], [0.2.0])

AC_INIT(meego-panel-web, [mwb_version], http://www.meego.com/)
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR(netpanel/meego-panel-web.cc)
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
GETTEXT_PACKAGE=meego-panel-web
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE",
                   [The gettext package name])
AM_GLIB_DEFINE_LOCALEDIR(LOCALEDIR)
AM_GLIB_GNU_GETTEXT

IT_PROG_INTLTOOL([0.40.4])

PKG_CHECK_MODULES(MX, mx-1.0 >= 0.2.0)
PKG_CHECK_MODULES(GTK, gtk+-2.0)

MXDATADIR=`${PKG_CONFIG} --variable=prefix mx-1.0`"/share/mx"
AC_SUBST(MXDATADIR)

PKG_CHECK_MODULES(SQLITE, sqlite3)
PKG_CHECK_MODULES(MPL, meego-panel)
PKG_CHECK_MODULES(NETPANEL, clutter-x11-1.0 clutter-1.0)

AC_ARG_ENABLE([cache],
              [AC_HELP_STRING([--enable-cache],
                              [Enable Mx image cache generation])],
              [],
              [enable_cache=no])

AS_IF([test "x$enable_cache" = "xyes"],
      [
      AC_CHECK_PROG([MX_CREATE_IMAGE_CACHE],
                [mx-create-image-cache],
                [$(which mx-create-image-cache)])
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
