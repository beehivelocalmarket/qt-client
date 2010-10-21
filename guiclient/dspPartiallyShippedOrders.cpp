/*
 * This file is part of the xTuple ERP: PostBooks Edition, a free and
 * open source Enterprise Resource Planning software suite,
 * Copyright (c) 1999-2010 by OpenMFG LLC, d/b/a xTuple.
 * It is licensed to you under the Common Public Attribution License
 * version 1.0, the full text of which (including xTuple-specific Exhibits)
 * is available at www.xtuple.com/CPAL.  By using this software, you agree
 * to be bound by its terms.
 */

#include "dspPartiallyShippedOrders.h"

#include <QMenu>
#include <QSqlError>
#include <QVariant>

#include <metasql.h>
#include <openreports.h>
#include <parameter.h>

#include "guiclient.h"
#include "printPackingList.h"
#include "salesOrder.h"

#define AMOUNT_COL	7
#define AMOUNT_CURR_COL	8
#define BASEAMOUNT_COL	9

dspPartiallyShippedOrders::dspPartiallyShippedOrders(QWidget* parent, const char* name, Qt::WFlags fl)
    : XWidget(parent, name, fl)
{
  setupUi(this);

//  (void)statusBar();

  connect(_print, SIGNAL(clicked()), this, SLOT(sPrint()));
  connect(_query, SIGNAL(clicked()), this, SLOT(sFillList()));
  connect(_showPrices, SIGNAL(toggled(bool)), this, SLOT(sHandlePrices(bool)));
  connect(_so, SIGNAL(populateMenu(QMenu*,QTreeWidgetItem*,int)), this, SLOT(sPopulateMenu(QMenu*)));

  _dates->setStartNull(tr("Earliest"), omfgThis->startOfTime(), TRUE);
  _dates->setEndNull(tr("Latest"), omfgThis->endOfTime(), TRUE);

  _so->addColumn(tr("Hold"),        0,           Qt::AlignCenter,true, "cohead_holdtype");
  _so->addColumn(tr("S/O #"),      _orderColumn, Qt::AlignRight, true, "cohead_number");
  _so->addColumn(tr("Customer"),    -1,          Qt::AlignLeft,  true, "cust_name");
  _so->addColumn(tr("Hold Type"),   _dateColumn, Qt::AlignCenter,true, "f_holdtype");
  _so->addColumn(tr("Ordered"),     _dateColumn, Qt::AlignRight, true, "cohead_orderdate");
  _so->addColumn(tr("Scheduled"),   _dateColumn, Qt::AlignRight, true, "minscheddate");
  _so->addColumn(tr("Pack Date"),   _dateColumn, Qt::AlignRight, true, "cohead_packdate");
  _so->addColumn(tr("Amount"),     _moneyColumn, Qt::AlignRight, true, "extprice");
  _so->addColumn(tr("Currency"),_currencyColumn, Qt::AlignLeft,  true, "currAbbr");
  _so->addColumn(tr("Amount\n(%1)").arg(CurrDisplay::baseCurrAbbr()),
                                   _moneyColumn, Qt::AlignRight, true, "extprice_base");

  sHandlePrices(_showPrices->isChecked());

  if ( (!_privileges->check("ViewCustomerPrices")) && (!_privileges->check("MaintainCustomerPrices")) )
    _showPrices->setEnabled(FALSE);

  sFillList();
}

dspPartiallyShippedOrders::~dspPartiallyShippedOrders()
{
  // no need to delete child widgets, Qt does it all for us
}

void dspPartiallyShippedOrders::languageChange()
{
  retranslateUi(this);
}

void dspPartiallyShippedOrders::sHandlePrices(bool pShowPrices)
{
  if (pShowPrices)
  {
    _so->showColumn(AMOUNT_COL);
    if (!omfgThis->singleCurrency())
      _so->showColumn(AMOUNT_CURR_COL);
    if (!omfgThis->singleCurrency())
      _so->showColumn(BASEAMOUNT_COL);
  }
  else
  {
    _so->hideColumn(AMOUNT_COL);
    _so->hideColumn(AMOUNT_CURR_COL);
    _so->hideColumn(BASEAMOUNT_COL);
  }
}

bool dspPartiallyShippedOrders::setParams(ParameterList &params)
{
  _warehouse->appendValue(params);
  if (_dates->allValid())
    _dates->appendValue(params);
  else
    return false;

  if(_showPrices->isChecked())
    params.append("showPrices");

  params.append("none",   tr("None"));
  params.append("credit", tr("Credit"));
  params.append("pack",   tr("Pack"));
  params.append("return", tr("Return"));
  params.append("ship",   tr("Ship"));
  params.append("other",  tr("Other"));

  if (omfgThis->singleCurrency())
    params.append("singlecurrency");

  return true;
}

void dspPartiallyShippedOrders::sPrint()
{
  ParameterList params;
  setParams(params);

  orReport report("PartiallyShippedOrders", params);
  if (report.isValid())
    report.print();
  else
    report.reportError(this);
}

void dspPartiallyShippedOrders::sEditOrder()
{
  salesOrder::editSalesOrder(_so->altId(), false);
}

void dspPartiallyShippedOrders::sViewOrder()
{
  salesOrder::viewSalesOrder(_so->altId());
}

void dspPartiallyShippedOrders::sPrintPackingList()
{
  ParameterList params;
  params.append("sohead_id", _so->altId());

  printPackingList newdlg(this, "", TRUE);
  newdlg.set(params);
  newdlg.exec();
}

void dspPartiallyShippedOrders::sPopulateMenu(QMenu *pMenu)
{
  int menuItem;

  menuItem = pMenu->insertItem(tr("Edit Order..."), this, SLOT(sEditOrder()), 0);
  if (!_privileges->check("MaintainSalesOrders"))
    pMenu->setItemEnabled(menuItem, FALSE);

  menuItem = pMenu->insertItem(tr("View Order..."), this, SLOT(sViewOrder()), 0);
  if ((!_privileges->check("MaintainSalesOrders")) && (!_privileges->check("ViewSalesOrders")))
    pMenu->setItemEnabled(menuItem, FALSE);

  pMenu->insertSeparator();

  if ( (_so->currentItem()->text(0) != "P") && (_so->currentItem()->text(0) != "C") )
  {
    menuItem = pMenu->insertItem(tr("Print Packing List..."), this, SLOT(sPrintPackingList()), 0);
    if (!_privileges->check("PrintPackingLists"))
      pMenu->setItemEnabled(menuItem, FALSE);
  }
}

void dspPartiallyShippedOrders::sFillList()
{
  _so->clear();

  ParameterList params;
  if (setParams(params))
  {
    QString sql( "SELECT CASE WHEN (cohead_holdtype IN ('P', 'C', 'R')) THEN -1"
		 "            ELSE cohead_id"
		 "       END AS _coheadid, cohead_id,"
		 "       cohead_holdtype, cohead_number, cust_name,"
		 "       CASE WHEN (cohead_holdtype='N') THEN <? value(\"none\") ?>"
		 "            WHEN (cohead_holdtype='C') THEN <? value(\"credit\") ?>"
		 "            WHEN (cohead_holdtype='S') THEN <? value(\"ship\") ?>"
		 "            WHEN (cohead_holdtype='P') THEN <? value(\"pack\") ?>"
		 "            WHEN (cohead_holdtype='R') THEN <? value(\"return\") ?>"
		 "            ELSE <? value(\"other\") ?>"
		 "       END AS f_holdtype,"
		 "       cohead_orderdate,"
		 "       (MIN(coitem_scheddate)) AS minscheddate,"
		 "       cohead_packdate,"
		 "       SUM( (noNeg(coitem_qtyord - coitem_qtyshipped + coitem_qtyreturned) * coitem_qty_invuomratio) *"
		 "                         (coitem_price / coitem_price_invuomratio) ) AS extprice,"
		 "       currConcat(cohead_curr_id) AS currAbbr,"
		 "       SUM(currToBase(cohead_curr_id,"
                 "           (noNeg(coitem_qtyord - coitem_qtyshipped + coitem_qtyreturned) * coitem_qty_invuomratio) *"
		 "            (coitem_price / coitem_price_invuomratio),"
                 "                      CURRENT_DATE)) AS extprice_base,"
                 "       'curr' AS extprice_xtnumericrole,"
                 "       'curr' AS extprice_base_xtnumericrole,"
                 "<? if exists(\"singlecurrency\") ?>"
                 "       0 AS extprice_xttotalrole "
                 "<? else ?>"
                 "       0 AS extprice_base_xttotalrole "
                 "<? endif ?>"
		 "FROM cohead, itemsite, item, cust, coitem "
		 "WHERE ( (coitem_cohead_id=cohead_id)"
		 " AND (cohead_cust_id=cust_id)"
		 " AND (coitem_itemsite_id=itemsite_id)"
		 " AND (itemsite_item_id=item_id)"
		 " AND (coitem_status='O')"
		 " AND (cohead_id IN ( SELECT DISTINCT coitem_cohead_id"
		 "                     FROM coitem"
		 "                     WHERE (coitem_qtyshipped > 0) ))"
		 " AND (coitem_qtyshipped < coitem_qtyord)"
		 " AND (coitem_scheddate BETWEEN <? value(\"startDate\") ?>"
		 "                           AND <? value(\"endDate\") ?>)"
		 "<? if exists(\"warehous_id\") ?>"
		 " AND (itemsite_warehous_id=<? value(\"warehous_id\") ?>)"
		 "<? endif ?>"
		 ") "
		 "GROUP BY cohead_id, cohead_number, cust_name,"
		 "         cohead_holdtype, cohead_orderdate, cohead_packdate,"
		 "         cohead_curr_id "
		 "ORDER BY minscheddate, cohead_number;");
    MetaSQLQuery mql(sql);
    q = mql.toQuery(params);
    _so->populate(q, true);
    if (q.lastError().type() != QSqlError::NoError)
    {
      systemError(this, q.lastError().databaseText(), __FILE__, __LINE__);
      return;
    }
    _so->setDragString("soheadid=");
  }
}