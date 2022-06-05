/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tsdb.h"

typedef struct {
  SMemTable *pMemTable;
  int32_t    minutes;
  int8_t     precision;
  int32_t    sfid;
  int32_t    efid;
  SReadH     readh;
  SDFileSet  wSet;
  SArray    *aDelInfo;
  SArray    *aBlkIdx;
  SArray    *aSupBlk;
  SArray    *aSubBlk;
} SCommitH;

static int32_t tsdbStartCommit(SCommitH *pCHandle, STsdb *pTsdb);
static int32_t tsdbEndCommit(SCommitH *pCHandle);
static int32_t tsdbCommitToFile(SCommitH *pCHandle, int32_t fid);
static int32_t tsdbCommitDelete(SCommitH *pCHandle);

int32_t tsdbBegin2(STsdb *pTsdb) {
  int32_t code = 0;

  ASSERT(pTsdb->mem == NULL);
  code = tsdbMemTableCreate2(pTsdb, (SMemTable **)&pTsdb->mem);
  if (code) {
    tsdbError("vgId:%d failed to begin TSDB since %s", TD_VID(pTsdb->pVnode), tstrerror(code));
    goto _exit;
  }

_exit:
  return code;
}

int32_t tsdbCommit2(STsdb *pTsdb) {
  int32_t  code = 0;
  SCommitH ch = {0};

  // start to commit
  code = tsdbStartCommit(&ch, pTsdb);
  if (code) {
    goto _exit;
  }

  // commit
  for (int32_t fid = ch.sfid; fid <= ch.efid; fid++) {
    code = tsdbCommitToFile(&ch, fid);
    if (code) {
      goto _err;
    }
  }

  code = tsdbCommitDelete(&ch);
  if (code) {
    goto _err;
  }

  // end commit
  code = tsdbEndCommit(&ch);
  if (code) {
    goto _exit;
  }

_exit:
  return code;

_err:
  // TODO: rollback
  return code;
}

static int32_t tsdbStartCommit(SCommitH *pCHandle, STsdb *pTsdb) {
  int32_t    code = 0;
  SMemTable *pMemTable = (SMemTable *)pTsdb->mem;

  tsdbInfo("vgId:%d start to commit", TD_VID(pTsdb->pVnode));

  // switch to commit
  ASSERT(pTsdb->imem == NULL && pTsdb->mem);
  pTsdb->imem = pTsdb->mem;
  pTsdb->mem = NULL;

  // open handle
  pCHandle->pMemTable = pMemTable;
  pCHandle->minutes = pTsdb->keepCfg.days;
  pCHandle->precision = pTsdb->keepCfg.precision;
  pCHandle->sfid = TSDB_KEY_FID(pMemTable->minKey.ts, pCHandle->minutes, pCHandle->precision);
  pCHandle->efid = TSDB_KEY_FID(pMemTable->maxKey.ts, pCHandle->minutes, pCHandle->precision);

  code = tsdbInitReadH(&pCHandle->readh, pTsdb);
  if (code) {
    goto _err;
  }
  pCHandle->aBlkIdx = taosArrayInit(1024, sizeof(SBlockIdx));
  if (pCHandle->aBlkIdx == NULL) {
    code = TSDB_CODE_OUT_OF_MEMORY;
    goto _err;
  }
  pCHandle->aSupBlk = taosArrayInit(1024, sizeof(SBlock));
  if (pCHandle->aSupBlk == NULL) {
    code = TSDB_CODE_OUT_OF_MEMORY;
    goto _err;
  }
  pCHandle->aSubBlk = taosArrayInit(1024, sizeof(SBlock));
  if (pCHandle->aSubBlk == NULL) {
    code = TSDB_CODE_OUT_OF_MEMORY;
    goto _err;
  }

  // start FS transaction
  tsdbStartFSTxn(pTsdb, 0, 0);

  return code;

_err:
  return code;
}

static int32_t tsdbEndCommit(SCommitH *pCHandle) {
  int32_t    code = 0;
  STsdb     *pTsdb = pCHandle->pMemTable->pTsdb;
  SMemTable *pMemTable = (SMemTable *)pTsdb->imem;

  // end transaction
  code = tsdbEndFSTxn(pTsdb);
  if (code) {
    goto _err;
  }

  // close handle
  taosArrayClear(pCHandle->aSubBlk);
  taosArrayClear(pCHandle->aSupBlk);
  taosArrayClear(pCHandle->aBlkIdx);
  tsdbDestroyReadH(&pCHandle->readh);

  // destroy memtable (todo: unref it)
  pTsdb->imem = NULL;
  tsdbMemTableDestroy2(pMemTable);

  tsdbInfo("vgId:%d commit over", TD_VID(pTsdb->pVnode));
  return code;

_err:
  return code;
}

static int32_t tsdbCommitTableData(SCommitH *pCHandle, SMemData *pMemData, SBlockIdx *pBlockIdx) {
  int32_t      code = 0;
  SMemDataIter iter = {0};

  if (pMemData && pBlockIdx) {
    // merge
  } else if (pMemData) {
    // new one
  } else {
    // save old ones
  }

  return code;
}

static int32_t tsdbTableIdCmprFn(const void *p1, const void *p2) {
  TABLEID *pId1 = (TABLEID *)p1;
  TABLEID *pId2 = (TABLEID *)p2;

  if (pId1->suid < pId2->suid) {
    return -1;
  } else if (pId1->suid > pId2->suid) {
    return 1;
  }

  if (pId1->uid < pId2->uid) {
    return -1;
  } else if (pId1->uid > pId2->uid) {
    return 1;
  }

  return 0;
}
static int32_t tsdbCommitToFile(SCommitH *pCHandle, int32_t fid) {
  int32_t      code = 0;
  SMemDataIter iter = {0};
  TSDBROW     *pRow = NULL;
  int8_t       hasData = 0;
  TSKEY        fidSKey;
  TSKEY        fidEKey;
  int32_t      iMemData = 0;
  int32_t      nMemData = taosArrayGetSize(pCHandle->pMemTable->aMemData);
  int32_t      iBlockIdx = 0;
  int32_t      nBlockIdx;

  // check if there are data in the time range
  for (; iMemData < nMemData; iMemData++) {
    SMemData *pMemData = (SMemData *)taosArrayGetP(pCHandle->pMemTable->aMemData, iMemData);
    tsdbMemDataIterOpen(pMemData, &(TSDBKEY){.ts = fidSKey, .version = 0}, 0, &iter);
    tsdbMemDataIterGet(&iter, &pRow);

    if (pRow->tsRow.ts >= fidSKey && pRow->tsRow.ts <= fidEKey) {
      hasData = 1;
      break;
    }
  }

  if (!hasData) return code;

  // create or open the file to commit(todo)

  // loop to commit each table data
  nBlockIdx = 0;
  for (;;) {
    if (iBlockIdx >= nBlockIdx && iMemData >= nMemData) break;

    SMemData  *pMemData = NULL;
    SBlockIdx *pBlockIdx = NULL;
    if (iMemData < nMemData) {
      pMemData = (SMemData *)taosArrayGetP(pCHandle->pMemTable->aMemData, iBlockIdx);
    }
    if (iBlockIdx < nBlockIdx) {
      // pBlockIdx
    }

    if (pMemData && pBlockIdx) {
      int32_t c = tsdbTableIdCmprFn(&(TABLEID){.suid = pMemData->suid, .uid = pMemData->uid},
                                    &(TABLEID){.suid = pBlockIdx->suid, .uid = pBlockIdx->uid});
      if (c == 0) {
        iMemData++;
        iBlockIdx++;
      } else if (c < 0) {
        pBlockIdx = NULL;
        iMemData++;
      } else {
        pMemData = NULL;
        iBlockIdx++;
      }
    } else {
      if (pMemData) {
        iMemData++;
      } else {
        iBlockIdx++;
      }
    }

    code = tsdbCommitTableData(pCHandle, pMemData, pBlockIdx);
    if (code) {
      goto _err;
    }
  }

  return code;

_err:
  return code;
}

static int32_t delInfoCmprFn(const void *p1, const void *p2) {
  SDelInfo *pDelInfo1 = (SDelInfo *)p1;
  SDelInfo *pDelInfo2 = (SDelInfo *)p2;

  if (pDelInfo1->suid < pDelInfo2->suid) {
    return -1;
  } else if (pDelInfo1->suid > pDelInfo2->suid) {
    return 1;
  }

  if (pDelInfo1->uid < pDelInfo2->uid) {
    return -1;
  } else if (pDelInfo1->uid > pDelInfo2->uid) {
    return 1;
  }

  if (pDelInfo1->version < pDelInfo2->version) {
    return -1;
  } else if (pDelInfo1->version > pDelInfo2->version) {
    return 1;
  }

  return 0;
}
static int32_t tsdbCommitDelete(SCommitH *pCHandle) {
  int32_t   code = 0;
  SDelInfo  delInfo;
  SMemData *pMemData;

  if (pCHandle->pMemTable->nDelOp == 0) goto _exit;

  // load del array (todo)

  // loop to append SDelInfo
  for (int32_t iMemData = 0; iMemData < taosArrayGetSize(pCHandle->pMemTable->aMemData); iMemData++) {
    pMemData = (SMemData *)taosArrayGetP(pCHandle->pMemTable->aMemData, iMemData);

    for (SDelOp *pDelOp = pMemData->delOpHead; pDelOp; pDelOp = pDelOp->pNext) {
      delInfo = (SDelInfo){.suid = pMemData->suid,
                           .uid = pMemData->uid,
                           .version = pDelOp->version,
                           .sKey = pDelOp->sKey,
                           .eKey = pDelOp->eKey};
      if (taosArrayPush(pCHandle->aDelInfo, &delInfo) == NULL) {
        code = TSDB_CODE_OUT_OF_MEMORY;
        goto _err;
      }
    }
  }

  taosArraySort(pCHandle->aDelInfo, delInfoCmprFn);

_exit:
  return code;

_err:
  return code;
}