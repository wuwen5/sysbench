dnl ---------------------------------------------------------------------------
dnl Macro: SB_CHECK_DM
dnl First check if the DM root directory is specified with --with-dm.
dnl Otherwise check for custom DM paths in --with-dm-includes and
dnl --with-dm-libs. If some paths are not specified explicitly, try to get
dnl them from dm_config.
dnl ---------------------------------------------------------------------------

AC_DEFUN([SB_CHECK_DM],[

AS_IF([test "x$with_dm" != xno], [

# Check for custom DM root directory
if test [ "x$with_dm" != xyes -a "x$with_dm" != xno ] 
then
    ac_cv_dm_root=`echo "$with_dm" | sed -e 's+/$++'`
    if test [ -d "$ac_cv_dm_root/include" -a \
              -d "$ac_cv_dm_root/libdm_r" ]
    then
        ac_cv_dm_includes="$ac_cv_dm_root/include"
        ac_cv_dm_libs="$ac_cv_dm_root/libdm_r"
    elif test [ -x "$ac_cv_dm_root/bin/dm_config" ]
    then
        dmconfig="$ac_cv_dm_root/bin/dm_config"
    else 
        AC_MSG_ERROR([invalid DM root directory: $ac_cv_dm_root])
    fi
fi

# Check for custom includes path
if test [ -z "$ac_cv_dm_includes" ] 
then 
    AC_ARG_WITH([dm-includes], 
                AC_HELP_STRING([--with-dm-includes], [path to dm header files]),
                [ac_cv_dm_includes=$withval])
fi
if test [ -n "$ac_cv_dm_includes" ]
then
    AC_CACHE_CHECK([DM includes], [ac_cv_dm_includes], [ac_cv_dm_includes=""])
    DM_CFLAGS="-I$ac_cv_dm_includes"
fi

# Check for custom library path

if test [ -z "$ac_cv_dm_libs" ]
then
    AC_ARG_WITH([dm-libs], 
                AC_HELP_STRING([--with-dm-libs], [path to dm libraries]),
                [ac_cv_dm_libs=$withval])
fi
if test [ -n "$ac_cv_dm_libs" ]
then
    # Trim trailing '.libs' if user passed it in --with-dm-libs option
    ac_cv_dm_libs=`echo ${ac_cv_dm_libs} | sed -e 's/.libs$//' \
                      -e 's+.libs/$++'`
    AC_CACHE_CHECK([DM libraries], [ac_cv_dm_libs], [ac_cv_dm_libs=""])
    save_LDFLAGS="$LDFLAGS"
    save_LIBS="$LIBS"
    LDFLAGS="-L$ac_cv_dm_libs"
    LIBS=""

    # libdmclient_r has been removed in dm 5.7
    AC_SEARCH_LIBS([dm_real_connect],
      [dmclient_r dmclient],
      [],
      AC_MSG_ERROR([cannot find dm client libraries in $ac_cv_dm_libs]))

    DM_LIBS="$LDFLAGS $LIBS"
    LIBS="$save_LIBS"
    LDFLAGS="$save_LDFLAGS"
fi

# If some path is missing, try to autodetermine with DM_HOME
if test [ -z "$ac_cv_dm_includes" -o -z "$ac_cv_dm_libs" ]
then
    if test [ -z "$DM_HOME" ]
    then 
        AC_PATH_PROG(DM_HOME,dm_config)
    fi
    if test [ -z "$DM_HOME" ]
    then
        AC_MSG_ERROR([DM_HOME executable not found
********************************************************************************
ERROR: cannot find DM libraries. If you want to compile with DM support,
       please install the package containing DM client libraries and headers.
       If you have those libraries installed in non-standard locations,
       you must either specify file locations explicitly using
       --with-dm-includes and --with-dm-libs options, or make sure path to
       DM_HOME is listed in your PATH environment variable. If you want to
       disable DM support, use --without-dm option.
********************************************************************************
])
    else
        if test [ -z "$ac_cv_dm_includes" ]
        then
            AC_MSG_CHECKING(DM C flags)
            DM_CFLAGS=`${DM_HOME} --cflags -DDM64`
            AC_MSG_RESULT($DM_CFLAGS)
        fi
        if test [ -z "$ac_cv_dm_libs" ]
        then
            AC_MSG_CHECKING(DM linker flags)
#            DM_LIBS=${DM_HOME}/include/libdmoci.a
            DM_LIBS=${DM_HOME}/include/libdmdpi.a
            AC_MSG_RESULT($DM_LIBS)
        fi
    fi
fi

AC_DEFINE([USE_DM], 1,
          [Define to 1 if you want to compile with DM support])

USE_DM=1
AC_SUBST([DM_LIBS])
AC_SUBST([DM_CFLAGS])


AC_MSG_CHECKING([if dm.h defines DM_OPT_SSL_MODE])

SAVE_CFLAGS="${CFLAGS}"
CFLAGS="${CFLAGS} ${DM_CFLAGS}"
])
CFLAGS="${SAVE_CFLAGS}"


AM_CONDITIONAL([USE_DM], test "x$with_dm" != xno)
AC_SUBST([USE_DM])
])
