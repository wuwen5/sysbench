dnl ---------------------------------------------------------------------------
dnl Macro: SB_CHECK_ORACLE
dnl First check if the Oracle root directory is specified with --with-oracle.
dnl Otherwise check for custom Oracle paths in --with-oracle-includes and
dnl --with-oracle-libs. If some paths are not specified explicitly, try to get
dnl them from oracle_config.
dnl ---------------------------------------------------------------------------

AC_DEFUN([SB_CHECK_ORACLE],[

AS_IF([test "x$with_oracle" != xno], [

# Check for custom Oracle root directory
if test [ "x$with_oracle" != xyes -a "x$with_oracle" != xno ] 
then
    ac_cv_oracle_root=`echo "$with_oracle" | sed -e 's+/$++'`
    if test [ -d "$ac_cv_oracle_root/include" -a \
              -d "$ac_cv_oracle_root/liboracle_r" ]
    then
        ac_cv_oracle_includes="$ac_cv_oracle_root/include"
        ac_cv_oracle_libs="$ac_cv_oracle_root/liboracle_r"
    elif test [ -x "$ac_cv_oracle_root/bin/oracle_config" ]
    then
        oracleconfig="$ac_cv_oracle_root/bin/oracle_config"
    else 
        AC_MSG_ERROR([invalid Oracle root directory: $ac_cv_oracle_root])
    fi
fi

# Check for custom includes path
if test [ -z "$ac_cv_oracle_includes" ] 
then 
    AC_ARG_WITH([oracle-includes], 
                AC_HELP_STRING([--with-oracle-includes], [path to oracle header files]),
                [ac_cv_oracle_includes=$withval])
fi
if test [ -n "$ac_cv_oracle_includes" ]
then
    AC_CACHE_CHECK([ORACLE includes], [ac_cv_oracle_includes], [ac_cv_oracle_includes=""])
    ORACLE_CFLAGS="-I$ac_cv_oracle_includes"
fi

# Check for custom library path

if test [ -z "$ac_cv_oracle_libs" ]
then
    AC_ARG_WITH([oracle-libs], 
                AC_HELP_STRING([--with-oracle-libs], [path to oracle libraries]),
                [ac_cv_oracle_libs=$withval])
fi
if test [ -n "$ac_cv_oracle_libs" ]
then
    # Trim trailing '.libs' if user passed it in --with-oracle-libs option
    ac_cv_oracle_libs=`echo ${ac_cv_oracle_libs} | sed -e 's/.libs$//' \
                      -e 's+.libs/$++'`
    AC_CACHE_CHECK([ORACLE libraries], [ac_cv_oracle_libs], [ac_cv_oracle_libs=""])
    save_LDFLAGS="$LDFLAGS"
    save_LIBS="$LIBS"
    LDFLAGS="-L$ac_cv_oracle_libs -lclntsh -Wl,-rpath=$ac_cv_oracle_libs"
    LIBS=""

    ORA_LIBS="$LDFLAGS $LIBS"
    LIBS="$save_LIBS"
    LDFLAGS="$save_LDFLAGS"
fi

AC_DEFINE([USE_ORACLE], 1,
          [Define to 1 if you want to compile with ORACLE support])

USE_ORACLE=1
AC_SUBST([ORA_LIBS])
AC_SUBST([ORACLE_CFLAGS])


SAVE_CFLAGS="${CFLAGS}"
CFLAGS="${CFLAGS} ${ORACLE_CFLAGS}"
])
CFLAGS="${SAVE_CFLAGS}"


AM_CONDITIONAL([USE_ORACLE], test "x$with_oracle" != xno)
AC_SUBST([USE_ORACLE])
])
