# Set ALL_LINGUAS based on the .po files present. Optional argument is the name
# of the po directory.
AC_DEFUN([AS_ALL_LINGUAS],
[
 AC_MSG_CHECKING([for linguas])
 podir="m4_default([$1],[$srcdir/po])"
 ALL_LINGUAS=`cd $podir && ls *.po 2>/dev/null | awk 'BEGIN { FS="."; ORS=" " } { print $[]1 }'`
 AC_SUBST([ALL_LINGUAS])
 AC_MSG_RESULT($ALL_LINGUAS)
])
