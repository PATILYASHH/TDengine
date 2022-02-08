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

#include <executorimpl.h>
#include <gtest/gtest.h>
#include <tglobal.h>
#include <iostream>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "os.h"

#include "taos.h"
#include "tdef.h"
#include "tvariant.h"
#include "tep.h"
#include "trpc.h"
#include "stub.h"
#include "executor.h"

namespace {

typedef struct SDummyInputInfo {
  int32_t max;
  int32_t current;
  int32_t startVal;
} SDummyInputInfo;

SSDataBlock* getDummyBlock(void* param, bool* newgroup) {
  SOperatorInfo* pOperator = static_cast<SOperatorInfo*>(param);
  SDummyInputInfo* pInfo = static_cast<SDummyInputInfo*>(pOperator->info);
  if (pInfo->current >= pInfo->max) {
    return NULL;
  }

  SSDataBlock* pBlock = static_cast<SSDataBlock*>(calloc(1, sizeof(SSDataBlock)));
  assert(pBlock != NULL);

  pBlock->pDataBlock = taosArrayInit(4, sizeof(SColumnInfoData));

  int32_t numOfRows = 1000;

  SColumnInfoData colInfo = {0};
  colInfo.info.type = TSDB_DATA_TYPE_INT;
  colInfo.info.bytes = sizeof(int32_t);
  colInfo.info.colId = 1;
  colInfo.pData = static_cast<char*>(calloc(numOfRows, sizeof(int32_t)));
  colInfo.nullbitmap = static_cast<char*>(calloc(1, (numOfRows + 7) / 8));

  taosArrayPush(pBlock->pDataBlock, &colInfo);

  SColumnInfoData colInfo1 = {0};
  colInfo1.info.type = TSDB_DATA_TYPE_BINARY;
  colInfo1.info.bytes = 40;
  colInfo1.info.colId = 2;

  colInfo1.varmeta.allocLen = 0;//numOfRows * sizeof(int32_t);
  colInfo1.varmeta.length = 0;
  colInfo1.varmeta.offset = static_cast<int32_t*>(calloc(1, numOfRows * sizeof(int32_t)));

  taosArrayPush(pBlock->pDataBlock, &colInfo1);

  char buf[128] = {0};
  char b1[128] = {0};
  for(int32_t i = 0; i < numOfRows; ++i) {
    SColumnInfoData* pColInfo = static_cast<SColumnInfoData*>(taosArrayGet(pBlock->pDataBlock, 0));

    int32_t v = (--pInfo->startVal);
    colDataAppend(pColInfo, i, reinterpret_cast<const char*>(&v), false);

    sprintf(buf, "this is %d row", i);
    STR_TO_VARSTR(b1, buf);

    SColumnInfoData* pColInfo2 = static_cast<SColumnInfoData*>(taosArrayGet(pBlock->pDataBlock, 1));
    colDataAppend(pColInfo2, i, b1, false);
  }

  pBlock->info.rows = numOfRows;
  pBlock->info.numOfCols = 2;

  pInfo->current += 1;
  return pBlock;
}

SOperatorInfo* createDummyOperator(int32_t numOfBlocks) {
  SOperatorInfo* pOperator = static_cast<SOperatorInfo*>(calloc(1, sizeof(SOperatorInfo)));
  pOperator->name = "dummyInputOpertor4Test";
  pOperator->exec = getDummyBlock;

  SDummyInputInfo *pInfo = (SDummyInputInfo*) calloc(1, sizeof(SDummyInputInfo));
  pInfo->max = numOfBlocks;
  pInfo->startVal = 100000;

  pOperator->info = pInfo;
  return pOperator;
}
}
int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(testCase, build_executor_tree_Test) {
  const char* msg = "{\n"
      "\t\"Id\":\t{\n"
      "\t\t\"QueryId\":\t1.3108161807422521e+19,\n"
      "\t\t\"TemplateId\":\t0,\n"
      "\t\t\"SubplanId\":\t0\n"
      "\t},\n"
      "\t\"Node\":\t{\n"
      "\t\t\"Name\":\t\"TableScan\",\n"
      "\t\t\"Targets\":\t[{\n"
      "\t\t\t\t\"Base\":\t{\n"
      "\t\t\t\t\t\"Schema\":\t{\n"
      "\t\t\t\t\t\t\"Type\":\t9,\n"
      "\t\t\t\t\t\t\"ColId\":\t5000,\n"
      "\t\t\t\t\t\t\"Bytes\":\t8\n"
      "\t\t\t\t\t},\n"
      "\t\t\t\t\t\"Columns\":\t[{\n"
      "\t\t\t\t\t\t\t\"TableId\":\t1,\n"
      "\t\t\t\t\t\t\t\"Flag\":\t0,\n"
      "\t\t\t\t\t\t\t\"Info\":\t{\n"
      "\t\t\t\t\t\t\t\t\"ColId\":\t1,\n"
      "\t\t\t\t\t\t\t\t\"Type\":\t9,\n"
      "\t\t\t\t\t\t\t\t\"Bytes\":\t8\n"
      "\t\t\t\t\t\t\t}\n"
      "\t\t\t\t\t\t}],\n"
      "\t\t\t\t\t\"InterBytes\":\t0\n"
      "\t\t\t\t},\n"
      "\t\t\t\t\"Expr\":\t{\n"
      "\t\t\t\t\t\"Type\":\t4,\n"
      "\t\t\t\t\t\"Column\":\t{\n"
      "\t\t\t\t\t\t\"Type\":\t9,\n"
      "\t\t\t\t\t\t\"ColId\":\t1,\n"
      "\t\t\t\t\t\t\"Bytes\":\t8\n"
      "\t\t\t\t\t}\n"
      "\t\t\t\t}\n"
      "\t\t\t}, {\n"
      "\t\t\t\t\"Base\":\t{\n"
      "\t\t\t\t\t\"Schema\":\t{\n"
      "\t\t\t\t\t\t\"Type\":\t4,\n"
      "\t\t\t\t\t\t\"ColId\":\t5001,\n"
      "\t\t\t\t\t\t\"Bytes\":\t4\n"
      "\t\t\t\t\t},\n"
      "\t\t\t\t\t\"Columns\":\t[{\n"
      "\t\t\t\t\t\t\t\"TableId\":\t1,\n"
      "\t\t\t\t\t\t\t\"Flag\":\t0,\n"
      "\t\t\t\t\t\t\t\"Info\":\t{\n"
      "\t\t\t\t\t\t\t\t\"ColId\":\t2,\n"
      "\t\t\t\t\t\t\t\t\"Type\":\t4,\n"
      "\t\t\t\t\t\t\t\t\"Bytes\":\t4\n"
      "\t\t\t\t\t\t\t}\n"
      "\t\t\t\t\t\t}],\n"
      "\t\t\t\t\t\"InterBytes\":\t0\n"
      "\t\t\t\t},\n"
      "\t\t\t\t\"Expr\":\t{\n"
      "\t\t\t\t\t\"Type\":\t4,\n"
      "\t\t\t\t\t\"Column\":\t{\n"
      "\t\t\t\t\t\t\"Type\":\t4,\n"
      "\t\t\t\t\t\t\"ColId\":\t2,\n"
      "\t\t\t\t\t\t\"Bytes\":\t4\n"
      "\t\t\t\t\t}\n"
      "\t\t\t\t}\n"
      "\t\t\t}],\n"
      "\t\t\"InputSchema\":\t[{\n"
      "\t\t\t\t\"Type\":\t9,\n"
      "\t\t\t\t\"ColId\":\t5000,\n"
      "\t\t\t\t\"Bytes\":\t8\n"
      "\t\t\t}, {\n"
      "\t\t\t\t\"Type\":\t4,\n"
      "\t\t\t\t\"ColId\":\t5001,\n"
      "\t\t\t\t\"Bytes\":\t4\n"
      "\t\t\t}],\n"
      "\t\t\"TableScan\":\t{\n"
      "\t\t\t\"TableId\":\t1,\n"
      "\t\t\t\"TableType\":\t2,\n"
      "\t\t\t\"Flag\":\t0,\n"
      "\t\t\t\"Window\":\t{\n"
      "\t\t\t\t\"StartKey\":\t-9.2233720368547758e+18,\n"
      "\t\t\t\t\"EndKey\":\t9.2233720368547758e+18\n"
      "\t\t\t}\n"
      "\t\t}\n"
      "\t},\n"
      "\t\"DataSink\":\t{\n"
      "\t\t\"Name\":\t\"Dispatch\",\n"
      "\t\t\"Dispatch\":\t{\n"
      "\t\t}\n"
      "\t}\n"
      "}";

  SExecTaskInfo* pTaskInfo = nullptr;
  DataSinkHandle sinkHandle = nullptr;
  SReadHandle handle = {.reader = reinterpret_cast<void*>(0x1), .meta = reinterpret_cast<void*>(0x1)};

//  int32_t code = qCreateExecTask(&handle, 2, 1, NULL, (void**) &pTaskInfo, &sinkHandle);
}

//TEST(testCase, inMem_sort_Test) {
//  SArray* pOrderVal = taosArrayInit(4, sizeof(SOrder));
//  SOrder o = {.order = TSDB_ORDER_ASC};
//  o.col.info.colId = 1;
//  o.col.info.type = TSDB_DATA_TYPE_INT;
//  taosArrayPush(pOrderVal, &o);
//
//  SArray* pExprInfo = taosArrayInit(4, sizeof(SExprInfo));
//  SExprInfo *exp = static_cast<SExprInfo*>(calloc(1, sizeof(SExprInfo)));
//  exp->base.resSchema = createSchema(TSDB_DATA_TYPE_INT, sizeof(int32_t), 1, "res");
//  taosArrayPush(pExprInfo, &exp);
//
//  SExprInfo *exp1 = static_cast<SExprInfo*>(calloc(1, sizeof(SExprInfo)));
//  exp1->base.resSchema = createSchema(TSDB_DATA_TYPE_BINARY, 40, 2, "res1");
//  taosArrayPush(pExprInfo, &exp1);
//
//  SOperatorInfo* pOperator = createOrderOperatorInfo(createDummyOperator(5), pExprInfo, pOrderVal);
//
//  bool newgroup = false;
//  SSDataBlock* pRes = pOperator->exec(pOperator, &newgroup);
//
//  SColumnInfoData* pCol1 = static_cast<SColumnInfoData*>(taosArrayGet(pRes->pDataBlock, 0));
//  SColumnInfoData* pCol2 = static_cast<SColumnInfoData*>(taosArrayGet(pRes->pDataBlock, 1));
//  for(int32_t i = 0; i < pRes->info.rows; ++i) {
//    char* p = colDataGet(pCol2, i);
//    printf("%d: %d, %s\n", i, ((int32_t*)pCol1->pData)[i], (char*)varDataVal(p));
//  }
//}

TEST(testCase, external_sort_Test) {
  SArray* pOrderVal = taosArrayInit(4, sizeof(SOrder));
  SOrder o = {.order = TSDB_ORDER_ASC};
  o.col.info.colId = 1;
  o.col.info.type = TSDB_DATA_TYPE_INT;
  taosArrayPush(pOrderVal, &o);

  SArray* pExprInfo = taosArrayInit(4, sizeof(SExprInfo));
  SExprInfo *exp = static_cast<SExprInfo*>(calloc(1, sizeof(SExprInfo)));
  exp->base.resSchema = createSchema(TSDB_DATA_TYPE_INT, sizeof(int32_t), 1, "res");
  taosArrayPush(pExprInfo, &exp);

  SExprInfo *exp1 = static_cast<SExprInfo*>(calloc(1, sizeof(SExprInfo)));
  exp1->base.resSchema = createSchema(TSDB_DATA_TYPE_BINARY, 40, 2, "res1");
  taosArrayPush(pExprInfo, &exp1);

  SOperatorInfo* pOperator = createOrderOperatorInfo(createDummyOperator(100), pExprInfo, pOrderVal);

  bool newgroup = false;
  SSDataBlock* pRes = pOperator->exec(pOperator, &newgroup);

  SColumnInfoData* pCol1 = static_cast<SColumnInfoData*>(taosArrayGet(pRes->pDataBlock, 0));
  SColumnInfoData* pCol2 = static_cast<SColumnInfoData*>(taosArrayGet(pRes->pDataBlock, 1));
  for(int32_t i = 0; i < pRes->info.rows; ++i) {
    char* p = colDataGet(pCol2, i);
    printf("%d: %d, %s\n", i, ((int32_t*)pCol1->pData)[i], (char*)varDataVal(p));
  }
}
#pragma GCC diagnostic pop