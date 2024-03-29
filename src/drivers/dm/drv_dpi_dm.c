/* Copyright (C) 2005 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "DPIext.h"
#include "DPI.h"
#include "DPItypes.h"
#include <ctype.h>

#include "sb_options.h"
#include "db_driver.h"

#define CHECKERR(hndl_type, hndl)                          \
  do { \
    if (rc != DSQL_SUCCESS) \
    { \
      log_text(LOG_FATAL, "failed in %s:%d", __FILE__, __LINE__); \
      checkerr(hndl_type, hndl, rc); \
      goto error; \
    } \
  } while(0);

static sb_arg_t dm_drv_args[] =
{
  SB_OPT("dm-user", "dm user", "SYSDBA", STRING),
  SB_OPT("dm-password", "dm password", "SYSDBA", STRING),
  SB_OPT("dm-db", "DM database connect url", "localhost:5236", STRING),
  SB_OPT_END
};

typedef struct
{
  dhcon     hdbc;
} dm_conn_t;

typedef enum
{
  STMT_TYPE_BEGIN,
  STMT_TYPE_COMMIT,
  STMT_TYPE_SELECT,
  STMT_TYPE_UPDATE
} dm_stmt_type_t;

typedef struct
{
  dhstmt            ptr;
  dm_stmt_type_t    type;
  slength           *buf_len;
  slength           *ind_ptr;
  dpointer          *buf;
} dm_stmt_t;

typedef struct
{
  char               *user;
  char               *password;
  char      *db;
} dm_drv_args_t;

/* Structure used for DB-to-DM bind types map */
/*
typedef struct
{
  db_bind_type_t   db_type;
  sdint2           dm_type;
  sdint2           c_type;
} db_dm_bind_map_t;
*/
/* DB-to-DM bind types map */
/*
db_dm_bind_map_t db_dm_bind_map[] =
{

  {DB_TYPE_TINYINT,   DSQL_TINYINT,     DSQL_C_STINYINT},
  {DB_TYPE_SMALLINT,  DSQL_SMALLINT,    DSQL_C_SSHORT},
  {DB_TYPE_INT,       DSQL_INT,         DSQL_C_SLONG},
  {DB_TYPE_BIGINT,    DSQL_BIGINT,      DSQL_C_SBIGINT},
  {DB_TYPE_FLOAT,     DSQL_DOUBLE,      DSQL_C_DOUBLE},
  {DB_TYPE_DOUBLE,    DSQL_DOUBLE,      DSQL_C_DOUBLE},
  {DB_TYPE_DATETIME,  DSQL_DATE,        DSQL_C_DATE},
  {DB_TYPE_TIMESTAMP, DSQL_TIMESTAMP,   DSQL_C_TIMESTAMP},
  {DB_TYPE_CHAR,      DSQL_CHAR,        DSQL_C_NCHAR},
  {DB_TYPE_VARCHAR,   DSQL_VARCHAR,     DSQL_C_NCHAR},
  {DB_TYPE_NONE,      0,                0}
};
*/

/* DM driver capabilities */
static drv_caps_t dm_drv_caps =
{
  .multi_rows_insert = 1,
  .prepared_statements = 1,
  .auto_increment = 1,
  .needs_commit = 1,
  .serial = 0,
  .unsigned_int = 1
};

static dhenv    dm_env; /* DPI environmental handle */
static dm_drv_args_t args;          /* driver args */



/* DM driver operations */

static int dm_drv_init(void);
static int dm_drv_describe(drv_caps_t *);
static int dm_drv_connect(db_conn_t *);
static int dm_drv_disconnect(db_conn_t *);
static int dm_drv_prepare(db_stmt_t *, const char *, size_t);
static int dm_drv_bind_param(db_stmt_t *, db_bind_t *, size_t);
static int dm_drv_bind_result(db_stmt_t *, db_bind_t *, size_t);
static db_error_t dm_drv_execute(db_stmt_t *, db_result_t *);
static int dm_drv_fetch(db_result_t *);
static int dm_drv_fetch_row(db_result_t *, db_row_t *);
static db_error_t dm_drv_query(db_conn_t *, const char *, size_t, 
								db_result_t *);
static int dm_drv_free_results(db_result_t *);
static int dm_drv_close(db_stmt_t *);
static int dm_drv_store_results(db_result_t *);
static int dm_drv_done(void);

/* DM driver definition */

static db_driver_t dm_driver =
{
  .sname = "dm",
  .lname = "dm driver",
  .args = dm_drv_args,
  .ops =
  {
    .init = dm_drv_init,
    .describe = dm_drv_describe,
    .connect = dm_drv_connect,
    .disconnect = dm_drv_disconnect,
    .prepare = dm_drv_prepare,
    .bind_param = dm_drv_bind_param,
    .bind_result = dm_drv_bind_result,
    .execute = dm_drv_execute,
    .fetch = dm_drv_fetch,
    .fetch_row = dm_drv_fetch_row,
    .free_results = dm_drv_free_results,
    .close = dm_drv_close,
    .query = dm_drv_query,
    .done = dm_drv_done
  },
  .listitem = {NULL, NULL}
};

/* Local functions */
static int get_dm_bind_type(db_bind_t *, sdint2 *, slength *, sdint2 *);
static dm_stmt_type_t get_stmt_type(const char *);
static void checkerr(sdint2, dhandle, DPIRETURN);

/* Register DM driver */
int register_driver_dm(sb_list_t *drivers)
{
  SB_LIST_ADD_TAIL(&dm_driver.listitem, drivers);

  return 0;
}


/* DM driver initialization */
int dm_drv_init(void)
{
  DPIRETURN   rc;

  args.user = sb_get_value_string("dm-user");
  args.password = sb_get_value_string("dm-password");
  args.db = sb_get_value_string("dm-db");

  /* Initialize the environment */
  rc = dpi_alloc_env(&dm_env);
  if (rc != DSQL_SUCCESS || dm_env == NULL)
  {
    log_text(LOG_FATAL, "dm DPIEnvCreate failed!");
    return 1;
  }
  
  return 0;
}

/* Describe database capabilities  */
int dm_drv_describe(drv_caps_t *caps)
{
  *caps = dm_drv_caps;
  
  return 0;
}

/* Connect to the database */
int dm_drv_connect(db_conn_t *sb_conn)
{
  DPIRETURN     rc;
  dm_conn_t     *dm_con = NULL;

  dm_con = (dm_conn_t *)malloc(sizeof(dm_conn_t));
  if (dm_con == NULL)
    goto error;

  rc = dpi_alloc_con(dm_env, &dm_con->hdbc);
  if (rc != DSQL_SUCCESS)
  {
    log_text(LOG_FATAL, "dm Alloc DSQL_HANDLE_DBC failed!");
    goto error;
  }
  
  rc = dpi_login(dm_con->hdbc, (sdbyte *)args.db, (sdbyte *)args.user, (sdbyte *)args.password);
  CHECKERR(DSQL_HANDLE_DBC, dm_con->hdbc);
  sb_conn->ptr = dm_con;
  return 0;
  
 error:
  if (dm_con != NULL)
    free(dm_con);
  
  return 1;
}

/* Disconnect from database */
int dm_drv_disconnect(db_conn_t *sb_conn)
{
  dm_conn_t *dm_con = sb_conn->ptr;
  DPIRETURN     rc;
  int           res = 0;
  
  if (dm_con == NULL)
    return 1;

  rc = dpi_logout(dm_con->hdbc);
  if (rc != DSQL_SUCCESS){
    log_text(LOG_FATAL, "DPISessionEnd failed!");
    res = 1;
  }
  
  rc = dpi_free_con(dm_con->hdbc);
  if (rc != DSQL_SUCCESS){
    log_text(LOG_FATAL, "dm Free DSQL_HANDLE_DBC failed!");
    res = 1;
  }
  free(dm_con);
  
  return res;
}

/* Prepare statement */
int dm_drv_prepare(db_stmt_t *stmt, const char *query, size_t len)
{
  dm_conn_t   *dm_con = (dm_conn_t *)stmt->connection->ptr;
  DPIRETURN   rc;
  dm_stmt_t   *dm_stmt = NULL;
  char        *buf = NULL;
  size_t vcnt;
  size_t need_realloc;
  size_t i,j;
  size_t buflen;
  int          n;
  
  if (dm_con == NULL)
    return 1;

  if (db_globals.ps_mode != DB_PS_MODE_DISABLE)
  {
    dm_stmt = (dm_stmt_t *)calloc(1, sizeof(dm_stmt_t));
    if (dm_stmt == NULL)
      goto error;
    dm_stmt->buf = NULL;
    dm_stmt->buf_len = NULL;
    dm_stmt->ind_ptr = NULL;

    /* Convert query to DM-style named placeholders */
    need_realloc = 1;
    vcnt = 1;
    buflen = 0;
    for (i = 0, j = 0; query[i] != '\0'; i++)
    {
    again:
      if (j+1 >= buflen || need_realloc)
      {
        buflen = (buflen > 0) ? buflen * 2 : 256;
        buf = realloc(buf, buflen);
        if (buf == NULL)
          goto error;
        need_realloc = 0;
      }

      if (query[i] != '?')
      {
        buf[j++] = query[i];
        continue;
      }

      n = snprintf(buf + j, buflen - j, ":%d", vcnt);
      if (n < 0 || n >= (int)(buflen - j))
      {
        need_realloc = 1;
        goto again;
      }

      j += n;
      vcnt++;
    }
    buf[j] = '\0';
    
    dm_stmt->type = get_stmt_type(buf);
	if(dm_stmt->type == STMT_TYPE_BEGIN || dm_stmt->type== STMT_TYPE_COMMIT){
		stmt->counter = SB_CNT_OTHER;
	}else if(dm_stmt->type == STMT_TYPE_SELECT){
		stmt->counter = SB_CNT_READ;
	}else if(dm_stmt->type == STMT_TYPE_UPDATE){
		stmt->counter = SB_CNT_WRITE;
	}
    
	if(dm_stmt->type != STMT_TYPE_BEGIN)
	{
	  rc = dpi_alloc_stmt(dm_con->hdbc, &dm_stmt->ptr);
	  CHECKERR(DSQL_HANDLE_DBC, dm_con->hdbc);
      if (rc != DSQL_SUCCESS)
        goto error;
	}
	
    if (dm_stmt->type != STMT_TYPE_BEGIN &&
        dm_stmt->type != STMT_TYPE_COMMIT)
    {
      rc = dpi_prepare(dm_stmt->ptr, buf);
      CHECKERR(DSQL_HANDLE_STMT, dm_stmt->ptr);
    }
    
    free(buf);
    
    stmt->ptr = (void *)dm_stmt;
  }
  else
  {
    /* Use client-side PS */
    stmt->emulated = 1;
  }
  stmt->query = strdup(query);
  
  return 0;
  
 error:
  if (dm_stmt != NULL)
  {
    if (dm_stmt->ptr != NULL)
      dpi_free_stmt(dm_stmt->ptr);
    free(dm_stmt);
  }
  log_text(LOG_FATAL, "Failed to prepare statement: '%s'", query);
  
  return 1;
}


/* Bind parameters for prepared statement */
int dm_drv_bind_param(db_stmt_t *stmt, db_bind_t *params, size_t len)
{
  dm_conn_t  *con = (dm_conn_t *)stmt->connection->ptr;
  dm_stmt_t  *dm_stmt = (dm_stmt_t *)stmt->ptr;
  size_t i;
  DPIRETURN   rc;
  
  if (con == NULL)
    return 1;

  if (!stmt->emulated)
  {
    if (dm_stmt == NULL || dm_stmt->ptr == NULL)
      return 1;

    dm_stmt->buf = malloc(len * sizeof(dpointer));
    dm_stmt->buf_len = malloc(len * sizeof(slength));
    dm_stmt->ind_ptr = malloc(len * sizeof(slength));
    if ( dm_stmt->buf == NULL || dm_stmt->buf_len == NULL || dm_stmt->ind_ptr == NULL)
      return 1;
    
    /* Convert SysBench bind structures to Oracle ones */
    for (i = 0; i < len; i++)
    {
      switch((params+i)->type){
        case DB_TYPE_BIGINT:
        case DB_TYPE_INT:
          dm_stmt->buf[i] = params[i].buffer;
          dm_stmt->buf_len[i] = sizeof(int);
          dm_stmt->ind_ptr[i] = sizeof(int);
          rc = dpi_bind_param(dm_stmt->ptr, i+1, DSQL_PARAM_INPUT, DSQL_C_SLONG, DSQL_INT, dm_stmt->buf_len[i], 0, dm_stmt->buf[i], dm_stmt->buf_len[i], dm_stmt->ind_ptr + i);
          break;
        case DB_TYPE_VARCHAR:
        case DB_TYPE_CHAR:
          dm_stmt->buf[i] = (params+i)->buffer;
          dm_stmt->buf_len[i] = (params+i)->max_len - 1;
          dm_stmt->ind_ptr[i] = (params+i)->max_len - 1;
          rc = dpi_bind_param(dm_stmt->ptr, i+1, DSQL_PARAM_INPUT, DSQL_C_NCHAR, DSQL_CHAR, dm_stmt->buf_len[i], 0, dm_stmt->buf[i], dm_stmt->buf_len[i], dm_stmt->ind_ptr + i);
          break;
        default:
          log_text(LOG_FATAL, "[query:%s] params[%d] can't get bind_type!", stmt->query, i);
          return 1;
      }
      
      if (rc != DSQL_SUCCESS)
      {
        log_text(LOG_FATAL, "[query:%s] params[%d] bind failed!", stmt->query, i);
        CHECKERR(DSQL_HANDLE_STMT, dm_stmt->ptr);
        return 1;
      }
    }
    return 0;
  }

  /* Use emulation */
  if (stmt->bound_param != NULL)
    free(stmt->bound_param);
  stmt->bound_param = (db_bind_t *)malloc(len * sizeof(db_bind_t));
  if (stmt->bound_param == NULL)
    return 1;
  memcpy(stmt->bound_param, params, len * sizeof(db_bind_t));
  stmt->bound_param_len = len;
  
  return 0;
  
 error:
  return 1;
}


/* Execute prepared statement */
db_error_t dm_drv_execute(db_stmt_t *stmt, db_result_t *rs)
{
  db_conn_t      *db_con = (dm_conn_t *)stmt->connection;
  dm_stmt_t      *dm_stmt = (dm_stmt_t *)stmt->ptr;
  dm_conn_t      *dm_con;
  DPIRETURN      rc;
  
  (void)rs; /* unused */

  if (db_con == NULL)
    return DB_ERROR_FATAL;
  dm_con = db_con->ptr;
  if (dm_con == NULL)
    return DB_ERROR_FATAL;
  
  if (!stmt->emulated)
  {
    if (stmt->ptr == NULL)
      return DB_ERROR_FATAL;

    if (dm_stmt->type == STMT_TYPE_BEGIN)
    {
	  rs->counter = SB_CNT_OTHER;
      return DB_ERROR_NONE;
    }
    else if (dm_stmt->type == STMT_TYPE_COMMIT)
    {
      rc = dpi_commit(dm_con->hdbc);
      CHECKERR(DSQL_HANDLE_DBC, dm_con->hdbc);
	  rs->counter = SB_CNT_OTHER;
      return DB_ERROR_NONE;
    }
    else if (dm_stmt->type == STMT_TYPE_SELECT){
	  rs->counter = SB_CNT_READ;
      rs->statement = stmt;
	}
    else{
	  rs->counter = SB_CNT_WRITE;
   }
    rc = dpi_exec(dm_stmt->ptr);
    CHECKERR(DSQL_HANDLE_STMT, dm_stmt->ptr);
    if(dm_stmt->type == STMT_TYPE_SELECT){
      dm_drv_fetch(rs);
      rs->counter = (rs->nrows > 0) ? SB_CNT_READ : SB_CNT_OTHER;
      rc = dpi_close_cursor(dm_stmt->ptr);
      CHECKERR(DSQL_HANDLE_STMT, dm_stmt->ptr);
    }else if(dm_stmt->type == STMT_TYPE_UPDATE){
      rc = dpi_row_count(dm_stmt->ptr, &rs->nrows);
	  CHECKERR(DSQL_HANDLE_STMT, dm_stmt->ptr);
	  rs->counter = (rs->nrows > 0) ? SB_CNT_WRITE : SB_CNT_OTHER;
    }
    return DB_ERROR_NONE;
  }

 error:
  log_text(LOG_FATAL, "failed query was: '%s'", stmt->query);
  return DB_ERROR_IGNORABLE;
}


/* Execute SQL query */
db_error_t dm_drv_query(db_conn_t *sb_conn, const char *query, size_t len,
                      db_result_t *rs)
{
  DPIRETURN   rc;
  dm_conn_t   *dm_con = NULL;
  dhstmt      hstmt = NULL;
  dm_stmt_type_t type;
  
  if (sb_conn == NULL)
    return DB_ERROR_FATAL;

  dm_con = (dm_conn_t *)sb_conn->ptr;
  
  if (dm_con == NULL || dm_con->hdbc == NULL)
    return DB_ERROR_FATAL;

  rc = dpi_alloc_stmt(dm_con->hdbc, &hstmt);
  if (rc != DSQL_SUCCESS)
    goto error;

  type = get_stmt_type(query);
  if(type == STMT_TYPE_BEGIN){
      rs->counter = SB_CNT_OTHER;
  }else if(type == STMT_TYPE_COMMIT){
      rs->counter = SB_CNT_OTHER;
  }else if(type == STMT_TYPE_SELECT){
      rs->counter = SB_CNT_READ;
  }else{
      rs->counter = SB_CNT_WRITE;
  }
  
  rc = dpi_exec_direct(hstmt, query);
  CHECKERR(DSQL_HANDLE_STMT, hstmt);
  if(type == STMT_TYPE_SELECT){
      rs->statement = NULL;
      rs->ptr = hstmt;
      dm_drv_fetch(rs);
      dpi_close_cursor(hstmt);
      rs->counter = (rs->nrows > 0) ? SB_CNT_READ : SB_CNT_OTHER;
  }else if(type == STMT_TYPE_UPDATE){
      rc = dpi_row_count(hstmt, &rs->nrows);
	  CHECKERR(DSQL_HANDLE_STMT, hstmt);
	  rs->counter = (rs->nrows > 0) ? SB_CNT_WRITE : SB_CNT_OTHER;
  }
  rc = dpi_free_stmt(hstmt);
  return 0;
  
  error:
    if(hstmt != NULL){
      dpi_free_stmt(hstmt);
    }
    log_text(LOG_FATAL, "failed query was: '%s'", query);
    return DB_ERROR_IGNORABLE;
}


/* Fetch row from result set */
int dm_drv_fetch(db_result_t *rs)
{
  DPIRETURN   rc;
  db_stmt_t   *db_stmt = NULL;
  dm_stmt_t   *dm_stmt = NULL;
  dhstmt      hstmt = NULL;
  sdint2      col_cnt = 0;
  ulength     row_num = 0;
  int         i = 0;
  
  sdbyte      name[10];
  sdint2      buf_len = 9;
  sdint2      name_len = 9;
  sdint2      sqltype;
  ulength     col_sz = 0;
  sdint2      dec_digits;
  sdint2      nullable;
  db_value_t *colI = NULL;
  
  if (rs == NULL )
   return DB_ERROR_FATAL;

  if(rs->ptr != NULL)
    hstmt = (dhstmt)rs->ptr;
  else{
    db_stmt = rs->statement;
    dm_stmt = (dm_conn_t *)db_stmt->ptr;
    hstmt = dm_stmt->ptr;
  }
  /*  get columns numbers  */
  rc = dpi_number_columns(hstmt, &col_cnt);
  CHECKERR(DSQL_HANDLE_STMT, hstmt);
  rs->nfields = col_cnt;
  
  rs->row.values = (db_value_t *)calloc(col_cnt, sizeof(db_value_t));
  
  /*  get colums info */
  for(i = 0; i < col_cnt; i++)
  {
      rc = dpi_desc_column(hstmt, i+1, name, buf_len, &name_len, &sqltype, &col_sz, &dec_digits, &nullable);
      CHECKERR(DSQL_HANDLE_STMT, hstmt);
      colI = &(rs->row.values[i]);
      colI->len = col_sz;
      colI->ptr = (const char*)calloc(col_sz + 1, sizeof(char));
      rc = dpi_bind_col(hstmt, i+1, DSQL_C_NCHAR, colI->ptr, colI->len, &colI->len);
      CHECKERR(DSQL_HANDLE_STMT, hstmt);
  }
  
  /*  get rows */
  while(dpi_fetch(hstmt, &row_num) != DSQL_NO_DATA)
  {
     colI = &(rs->row.values[i]);
     rs->nrows += row_num;
  }
  rs->nrows += row_num;
  return 0;
  
  error:
    return DB_ERROR_IGNORABLE;
}


/* Fetch row from result set of a query */
int dm_drv_fetch_row(db_result_t *rs, db_row_t *row)
{
  /* NYI */
  (void)rs;  /* unused */
  (void)row; /* unused */
  
  return 1;
}

/* Store results from the last query */
int dm_drv_store_results(db_result_t *rs)
{
  (void)rs;  /* unused */
  
  return 1;
}

/* Free result set */
int dm_drv_free_results(db_result_t * rs)
{
  db_value_t	*colI = NULL;
  int 			i = 0;
  
  // result is NULL
  if(rs->row.values == NULL)
	return 0;
  
  for(i = 0; i < rs->nfields; i ++)
  {
	colI = &(rs->row.values[i]);
	if(colI->ptr != NULL)
	{
	  free(colI->ptr);
	}
  }
  
  rs->nfields = 0;
  rs->nrows = 0;
  
  return 0;
}

/* Bind results for prepared statement */
int dm_drv_bind_result(db_stmt_t *stmt, db_bind_t *params, size_t len)
{
  /* NYI */

  (void)stmt;
  (void)params;
  (void)len;
  
  return 1;
}

/* Close prepared statement */
int dm_drv_close(db_stmt_t *stmt)
{
  DPIRETURN           rc;
  dm_stmt_t *dm_stmt = stmt->ptr;
  int 			ret = 0;

  if (dm_stmt == NULL)
    return 1;
  if (dm_stmt->ptr != NULL)
  {
	rc = dpi_free_stmt(dm_stmt->ptr);
    if (rc != DSQL_SUCCESS)
    {
      log_text(LOG_FATAL, "dm Free Hstmt failed!");
	  checkerr(DSQL_HANDLE_STMT, dm_stmt->ptr, rc);
      ret = 1;
    }
  }
  
  free(dm_stmt->buf);
  free(dm_stmt->buf_len);
  free(dm_stmt->ind_ptr);
  free(dm_stmt);
  
  return ret;
}

/* Uninitialize driver */
int dm_drv_done(void)
{  
  DPIRETURN           rc;
  if(dm_env == NULL){
      return 1;
  }
  
  rc = dpi_free_env(dm_env);
  if (rc != DSQL_SUCCESS)
  {
    log_text(LOG_FATAL, "dm Free ENV failed!");
    checkerr(DSQL_HANDLE_ENV, dm_env, rc);
    return 1;
  }
  return 0;
}


dm_stmt_type_t get_stmt_type(const char *query)
{
    int i = 0;
    for(; query[i] != '\0'; i++){
        if(isalpha(query[i])){
            break;
        }
    }
    
    if (!strncmp(query + i, "BEGIN", 5) || !strncmp(query + i, "begin", 5))
      return STMT_TYPE_BEGIN;
    else if (!strncmp(query + i, "COMMIT", 6) || !strncmp(query + i, "commit", 6))
      return STMT_TYPE_COMMIT;
    else if (!strncmp(query + i, "SELECT", 6) || !strncmp(query + i, "select", 6))
      return STMT_TYPE_SELECT;

    return STMT_TYPE_UPDATE;
}

void checkerr(sdint2 hndl_type, dhandle hndl, DPIRETURN status)
{
  sdbyte err_msg[512];
  sdint2 msg_len;
  sdint4 err_code;

  switch (status)
  {
    case DSQL_SUCCESS:
      break;
    case DSQL_SUCCESS_WITH_INFO:
      log_text(LOG_ALERT, "Error - DM_DPI_SUCCESS_WITH_INFO");
      break;
    case DSQL_NEED_DATA:
      log_text(LOG_ALERT, "Error - DM_OPI_NEED_DATA");
      break;
    case DSQL_NO_DATA:
      log_text(LOG_ALERT, "Error - DM_DPI_NO_DATA");
      break;
    case DSQL_ERROR:
      dpi_get_diag_rec(hndl_type, hndl, 1, &err_code, err_msg, sizeof(err_msg), &msg_len);
      log_text(LOG_ALERT, "Error - DM_%d_%s", err_code, err_msg);
      break;
    case DSQL_INVALID_HANDLE:
      log_text(LOG_ALERT, "Error - DM_DPI_INVALID_HANDLE");
      break;
    case DSQL_STILL_EXECUTING:
      log_text(LOG_ALERT, "Error - DM_DPI_STILL_EXECUTE");
      break;
    case DSQL_PARAM_DATA_AVAILABLE:
      log_text(LOG_ALERT, "Error - DM_DPI_PARAM_DATA_AVAILABLE");
      break;
    default:
      break;
  }
}