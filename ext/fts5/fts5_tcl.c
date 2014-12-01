/*
** 2014 Dec 01
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
*/


#include "fts5.h"
#include <tcl.h>
#include <string.h>
#include <assert.h>

/*************************************************************************
** This is a copy of the first part of the SqliteDb structure in 
** tclsqlite.c.  We need it here so that the get_sqlite_pointer routine
** can extract the sqlite3* pointer from an existing Tcl SQLite
** connection.
*/
struct SqliteDb {
  sqlite3 *db;
};

/*
** Decode a pointer to an sqlite3 object.
*/
static int f5tDbPointer(Tcl_Interp *interp, Tcl_Obj *pObj, sqlite3 **ppDb){
  struct SqliteDb *p;
  Tcl_CmdInfo cmdInfo;
  char *z = Tcl_GetString(pObj);
  if( Tcl_GetCommandInfo(interp, z, &cmdInfo) ){
    p = (struct SqliteDb*)cmdInfo.objClientData;
    *ppDb = p->db;
    return TCL_OK;
  }
  return TCL_ERROR;
}
/* End of code that accesses the SqliteDb struct.
**************************************************************************/

typedef struct F5tFunction F5tFunction;
struct F5tFunction {
  Tcl_Interp *interp;
  Tcl_Obj *pScript;
};

typedef struct F5tApi F5tApi;
struct F5tApi {
  const Fts5ExtensionApi *pApi;
  Fts5Context *pFts;
};

/*
**      api sub-command... 
**
** Description...
*/
static int xF5tApi(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  struct Sub {
    const char *zName;
    int nArg;
    const char *zMsg;
  } aSub[] = {
    { "xRowid",      0, "" },
    { "xInstCount",  0, "" },
    { "xInst",       1, "IDX" },
    { "xColumnText", 1, "COL" },
    { "xColumnSize", 1, "COL" },
  };
  int rc;
  int iSub = 0;
  F5tApi *p = (F5tApi*)clientData;

  if( objc<2 ){
    Tcl_WrongNumArgs(interp, 1, objv, "SUB-COMMAND");
    return TCL_ERROR;
  }

  rc = Tcl_GetIndexFromObjStruct(
      interp, objv[1], aSub, sizeof(aSub[0]), "SUB-COMMAND", 0, &iSub
  );
  if( rc!=TCL_OK ) return rc;
  if( aSub[iSub].nArg!=objc-2 ){
    Tcl_WrongNumArgs(interp, 1, objv, aSub[iSub].zMsg);
    return TCL_ERROR;
  }

  switch( iSub ){
    case 0: { /* xRowid */
      sqlite3_int64 iRowid = p->pApi->xRowid(p->pFts);
      Tcl_SetObjResult(interp, Tcl_NewWideIntObj(iRowid));
      break;
    }

    case 1: { /* xInstCount */
      int nInst;
      rc = p->pApi->xInstCount(p->pFts, &nInst);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewIntObj(nInst));
      }
      break;
    }

    case 2: { /* xInst */
      int iIdx, ip, ic, io;
      if( Tcl_GetIntFromObj(interp, objv[2], &iIdx) ){
        return TCL_ERROR;
      }
      rc = p->pApi->xInst(p->pFts, iIdx, &ip, &ic, &io);
      if( rc==SQLITE_OK ){
        Tcl_Obj *pList = Tcl_NewObj();
        Tcl_ListObjAppendElement(interp, pList, Tcl_NewIntObj(ip));
        Tcl_ListObjAppendElement(interp, pList, Tcl_NewIntObj(ic));
        Tcl_ListObjAppendElement(interp, pList, Tcl_NewIntObj(io));
        Tcl_SetObjResult(interp, pList);
      }
      break;
    }

    case 3: { /* xColumnText */
      const char *z = 0;
      int n = 0;
      int iCol;
      if( Tcl_GetIntFromObj(interp, objv[2], &iCol) ){
        return TCL_ERROR;
      }
      rc = p->pApi->xColumnText(p->pFts, iCol, &z, &n);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewStringObj(z, n));
      }
      break;
    }

    case 4: { /* xColumnSize */
      int n = 0;
      int iCol;
      if( Tcl_GetIntFromObj(interp, objv[2], &iCol) ){
        return TCL_ERROR;
      }
      rc = p->pApi->xColumnSize(p->pFts, iCol, &n);
      if( rc==SQLITE_OK ){
        Tcl_SetObjResult(interp, Tcl_NewIntObj(n));
      }
      break;
    }

    default: 
      assert( 0 );
      break;
  }

  if( rc!=SQLITE_OK ){
    Tcl_AppendResult(interp, "error in api call", 0);
    return TCL_ERROR;
  }

  return TCL_OK;
}

static void xF5tFunction(
  const Fts5ExtensionApi *pApi,   /* API offered by current FTS version */
  Fts5Context *pFts,              /* First arg to pass to pApi functions */
  sqlite3_context *pCtx,          /* Context for returning result/error */
  int nVal,                       /* Number of values in apVal[] array */
  sqlite3_value **apVal           /* Array of trailing arguments */
){
  F5tFunction *p = (F5tFunction*)pApi->xUserData(pFts);
  Tcl_Obj *pEval;                 /* Script to evaluate */
  int i;
  int rc;

  static sqlite3_int64 iCmd = 0;
  char zCmd[64];
  F5tApi sApi;
  sApi.pApi = pApi;
  sApi.pFts = pFts;

  sprintf(zCmd, "f5t_%lld", iCmd++);
  Tcl_CreateObjCommand(p->interp, zCmd, xF5tApi, &sApi, 0);
  pEval = Tcl_DuplicateObj(p->pScript);
  Tcl_IncrRefCount(pEval);
  Tcl_ListObjAppendElement(p->interp, pEval, Tcl_NewStringObj(zCmd, -1));

  for(i=0; i<nVal; i++){
    Tcl_Obj *pObj = 0;
    switch( sqlite3_value_type(apVal[i]) ){
      case SQLITE_TEXT:
        pObj = Tcl_NewStringObj((const char*)sqlite3_value_text(apVal[i]), -1);
        break;
      case SQLITE_BLOB:
        pObj = Tcl_NewByteArrayObj(
            sqlite3_value_blob(apVal[i]), sqlite3_value_bytes(apVal[i])
        );
        break;
      case SQLITE_INTEGER:
        pObj = Tcl_NewWideIntObj(sqlite3_value_int64(apVal[i]));
        break;
      case SQLITE_FLOAT:
        pObj = Tcl_NewDoubleObj(sqlite3_value_double(apVal[i]));
        break;
      default:
        pObj = Tcl_NewObj();
        break;
    }
    Tcl_ListObjAppendElement(p->interp, pEval, pObj);
  }

  rc = Tcl_EvalObjEx(p->interp, pEval, TCL_GLOBAL_ONLY);
  Tcl_DecrRefCount(pEval);
  Tcl_DeleteCommand(p->interp, zCmd);

  if( rc!=TCL_OK ){
    sqlite3_result_error(pCtx, Tcl_GetStringResult(p->interp), -1);
  }else{
    Tcl_Obj *pVar = Tcl_GetObjResult(p->interp);
    int n;
    const char *zType = (pVar->typePtr ? pVar->typePtr->name : "");
    char c = zType[0];
    if( c=='b' && strcmp(zType,"bytearray")==0 && pVar->bytes==0 ){
      /* Only return a BLOB type if the Tcl variable is a bytearray and
      ** has no string representation. */
      unsigned char *data = Tcl_GetByteArrayFromObj(pVar, &n);
      sqlite3_result_blob(pCtx, data, n, SQLITE_TRANSIENT);
    }else if( c=='b' && strcmp(zType,"boolean")==0 ){
      Tcl_GetIntFromObj(0, pVar, &n);
      sqlite3_result_int(pCtx, n);
    }else if( c=='d' && strcmp(zType,"double")==0 ){
      double r;
      Tcl_GetDoubleFromObj(0, pVar, &r);
      sqlite3_result_double(pCtx, r);
    }else if( (c=='w' && strcmp(zType,"wideInt")==0) ||
          (c=='i' && strcmp(zType,"int")==0) ){
      Tcl_WideInt v;
      Tcl_GetWideIntFromObj(0, pVar, &v);
      sqlite3_result_int64(pCtx, v);
    }else{
      unsigned char *data = (unsigned char *)Tcl_GetStringFromObj(pVar, &n);
      sqlite3_result_text(pCtx, (char *)data, n, SQLITE_TRANSIENT);
    }
  }
}

static void xF5tDestroy(void *pCtx){
  F5tFunction *p = (F5tFunction*)pCtx;
  Tcl_DecrRefCount(p->pScript);
  ckfree(p);
}

/*
**      sqlite3_fts5_create_function DB NAME SCRIPT
**
** Description...
*/
static int f5tCreateFunction(
  void * clientData,
  Tcl_Interp *interp,
  int objc,
  Tcl_Obj *CONST objv[]
){
  char *zName;
  Tcl_Obj *pScript;
  sqlite3 *db = 0;
  sqlite3_stmt *pStmt = 0;
  fts5_api *pApi = 0;
  F5tFunction *pCtx = 0;
  int rc;

  if( objc!=4 ){
    Tcl_WrongNumArgs(interp, 1, objv, "DB NAME SCRIPT");
    return TCL_ERROR;
  }
  if( f5tDbPointer(interp, objv[1], &db) ){
    return TCL_ERROR;
  }
  zName = Tcl_GetString(objv[2]);
  pScript = objv[3];

  rc = sqlite3_prepare_v2(db, "SELECT fts5()", -1, &pStmt, 0);
  if( rc!=SQLITE_OK ){
    Tcl_AppendResult(interp, "error: ", sqlite3_errmsg(db), 0);
    return TCL_ERROR;
  }

  if( SQLITE_ROW==sqlite3_step(pStmt) ){
    const void *pPtr = sqlite3_column_blob(pStmt, 0);
    memcpy((void*)&pApi, pPtr, sizeof(pApi));
  }
  if( sqlite3_finalize(pStmt)!=SQLITE_OK ){
    Tcl_AppendResult(interp, "error: ", sqlite3_errmsg(db), 0);
    return TCL_ERROR;
  }

  pCtx = (F5tFunction*)ckalloc(sizeof(F5tFunction));
  pCtx->interp = interp;
  pCtx->pScript = pScript;
  Tcl_IncrRefCount(pScript);

  rc = pApi->xCreateFunction(
      pApi, zName, (void*)pCtx, xF5tFunction, xF5tDestroy
  );
  if( rc!=SQLITE_OK ){
    Tcl_AppendResult(interp, "error: ", sqlite3_errmsg(db), 0);
    return TCL_ERROR;
  }

  return TCL_OK;
}

/*
** Entry point.
*/
int Fts5tcl_Init(Tcl_Interp *interp){
  static struct Cmd {
    char *zName;
    Tcl_ObjCmdProc *xProc;
    void *clientData;
  } aCmd[] = {
    { "sqlite3_fts5_create_function", f5tCreateFunction, 0 }
  };
  int i;

  for(i=0; i<sizeof(aCmd)/sizeof(aCmd[0]); i++){
    struct Cmd *p = &aCmd[i];
    Tcl_CreateObjCommand(interp, p->zName, p->xProc, p->clientData, 0);
  }

  return TCL_OK;
}

