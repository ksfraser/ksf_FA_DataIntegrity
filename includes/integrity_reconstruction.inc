<?php
/**
 * integrity_reconstruction.inc &#8212; Data-reconstruction fix functions
 *
 * Consolidated chain-view actions for purchase (PCHAIN) and sales (SCHAIN)
 * tabs.  Each row in the chain table represents one broken link; per-row
 * Match / Add / Disassociate buttons execute the targeted fix.
 *
 * PHP 7.3+.
 *
 * @package  Ksfraser\FA\DataIntegrity
 * @since    2.5.0
 */

/**
 * DTO listing entities affected by a chain-modification action.
 * Passed to _chain_recalc_all() so only relevant counters are refreshed.
 */
class ChainRecalcScope
{
    /** @var int[] Purchase GRN item IDs affected */
    public $grnItemIds = array();
    /** @var int[] Purchase order detail item IDs affected */
    public $poDetailItemIds = array();
    /** @var int[] Supplier invoice item IDs affected */
    public $supplierInvoiceItemIds = array();
    /** @var int[] Sales order detail item IDs affected */
    public $soDetailItemIds = array();
    /** @var int[] Customer delivery detail IDs affected */
    public $deliveryDetailIds = array();
    /** @var int[] Customer invoice item IDs affected */
    public $customerInvoiceItemIds = array();
    /** @var bool Recalculate supplier allocations */
    public $recalcSupplierAlloc = false;
    /** @var bool Recalculate customer allocations */
    public $recalcCustomerAlloc = false;
}

/**
 * Run all relevant recalculations for the entities listed in the scope.
 * Every recalc is idempotent &#8212; only rows with actual drift are updated.
 *
 * @param  FrontAccounting\Db\DbAdapterInterface $db
 * @param  ChainRecalcScope|null                 $scope
 * @return void
 */
function _chain_recalc_all($db, $scope = null)
{
    // ---- Purchase side ----
    $grnRepo = new \FrontAccounting\Repository\GrnItemsRepository($db);
    $grnRepo->recalcQtyInv();

    $podRepo = new \FrontAccounting\Repository\PurchOrderDetailsRepository($db);
    $podRepo->recalcQtyInvoiced();
    $podRepo->recalcQtyReceived();

    // ---- Sales side ----
    $sodRepo = new \FrontAccounting\Repository\SalesOrderDetailsRepository($db);
    $sodRepo->recalcQtySent();
    $sodRepo->recalcInvoiced();

    // ---- Allocations ----
    $allocRepo = new \FrontAccounting\Repository\AllocationRepository($db);
    if ($scope && $scope->recalcSupplierAlloc) {
        $allocRepo->recalcSupplierAlloc();
    }
    if ($scope && $scope->recalcCustomerAlloc) {
        $allocRepo->recalcCustomerAlloc();
    }
}

// ===========================================================================
// CANDIDATE MATCHING HELPERS
// ===========================================================================

/**
 * Find the best candidate GRN item for an orphaned invoice item.
 *
 * Criteria (all must match):
 *   1. Same supplier_id
 *   2. Same stock_id (item_code)
 *   3. Delivery date within ±14 days of invoice date
 *   4. GRN item is NOT already linked to another non-voided invoice
 *
 * @param  int    $supplierId
 * @param  string $stockId
 * @param  string $invDate   YYYY-MM-DD
 * @return array|null  ['grn_item_id'=>int, 'grn_batch_id'=>int] or null
 */
function _integ_candidate_grn($supplierId, $stockId, $invDate)
{
    $sql = "SELECT g.id AS grn_item_id, gb.id AS grn_batch_id,
                   gb.delivery_date,
                   ABS(DATEDIFF(gb.delivery_date, " . db_escape($invDate) . ")) AS day_diff
            FROM " . TB_PREF . "grn_items g
            JOIN " . TB_PREF . "grn_batch gb ON gb.id = g.grn_batch_id
            WHERE gb.supplier_id = " . (int)$supplierId . "
              AND g.item_code = " . db_escape($stockId) . "
              AND ABS(DATEDIFF(gb.delivery_date, " . db_escape($invDate) . ")) <= 14
              AND g.quantity_inv < g.qty_recd - 0.001
              AND NOT EXISTS (
                  SELECT 1 FROM " . TB_PREF . "supp_invoice_items si2
                  JOIN " . TB_PREF . "supp_trans st2
                      ON st2.trans_no = si2.supp_trans_no
                     AND st2.type     = si2.supp_trans_type
                  WHERE si2.grn_item_id = g.id
                    AND si2.supp_trans_type = 20
                    AND st2.ov_amount != 0
              )
            ORDER BY day_diff
            LIMIT 1";

    $result = db_query($sql, 'Find candidate GRN for invoice item');

    if ($result) {
        if ($row = db_fetch($result)) {
            return $row;
        } else {
            return null;
        }
    }
    return null;
}

/**
 * Find the best candidate PO for an orphaned GRN batch.
 *
 * Criteria (all must match):
 *   1. Same supplier_id
 *   2. Same item_code (at least one PO detail line)
 *   3. Order date within ±14 days of GRN delivery date
 *   4. PO does NOT already have any GRN batch linked
 *
 * @param  int    $supplierId
 * @param  string $itemCode
 * @param  string $grnDate   YYYY-MM-DD
 * @return array|null  ['order_no'=>int, 'reference'=>string] or null
 */
function _integ_candidate_po($supplierId, $itemCode, $grnDate)
{
    $sql = "SELECT po.order_no, po.reference,
                   po.ord_date,
                   ABS(DATEDIFF(po.ord_date, " . db_escape($grnDate) . ")) AS day_diff
            FROM " . TB_PREF . "purch_orders po
            JOIN " . TB_PREF . "purch_order_details pod ON pod.order_no = po.order_no
            WHERE po.supplier_id = " . (int)$supplierId . "
              AND pod.item_code = " . db_escape($itemCode) . "
              AND ABS(DATEDIFF(po.ord_date, " . db_escape($grnDate) . ")) <= 14
              AND NOT EXISTS (
                  SELECT 1 FROM " . TB_PREF . "grn_batch gb2
                  WHERE gb2.purch_order_no = po.order_no
              )
            ORDER BY day_diff
            LIMIT 1";

    $result = db_query($sql, 'Find candidate PO for GRN batch');

    if ($result) {
        if ($row = db_fetch($result)) {
            return $row;
        } else {
            return null;
        }
    }
    return null;
}

/**
 * Find the best candidate SO for an orphaned delivery note.
 *
 * @param  int    $debtorNo
 * @param  string $stockId
 * @param  string $delDate  YYYY-MM-DD
 * @return array|null  ['order_no'=>int, 'reference'=>string] or null
 */
function _integ_candidate_so($debtorNo, $stockId, $delDate)
{
    $sql = "SELECT so.order_no, so.reference,
                   so.ord_date,
                   ABS(DATEDIFF(so.ord_date, " . db_escape($delDate) . ")) AS day_diff
            FROM " . TB_PREF . "sales_orders so
            JOIN " . TB_PREF . "sales_order_details sod
                ON sod.order_no = so.order_no AND sod.trans_type = so.trans_type
            WHERE so.debtor_no = " . (int)$debtorNo . "
              AND sod.stk_code = " . db_escape($stockId) . "
              AND so.trans_type = 30
              AND ABS(DATEDIFF(so.ord_date, " . db_escape($delDate) . ")) <= 14
              AND NOT EXISTS (
                  SELECT 1 FROM " . TB_PREF . "debtor_trans dt2
                  WHERE dt2.order_ = so.order_no AND dt2.type = 13
              )
            ORDER BY day_diff
            LIMIT 1";

    $result = db_query($sql, 'Find candidate SO for delivery');
    return ($result && ($row = db_fetch($result))) ? $row : null;
}

// ===========================================================================
// CONSOLIDATED PURCHASE CHAIN CHECK
// ===========================================================================

/**
 * Run all purchase chain issue checks and return a unified array of rows.
 *
 * Each row has:
 *   row_type        &#8212; 'orphan_inv' | 'orphan_grn' | 'item_mismatch' | 'ghost_inv'
 *   row_key         &#8212; unique string for matching POST actions
 *   supplier_id, supp_name
 *   po_order_no, po_ref, po_item_code
 *   grn_batch_id, grn_item_id, grn_ref, grn_date, grn_item_code, grn_qty
 *   inv_trans_no, inv_date, inv_ref, inv_stock_id, inv_qty, inv_total
 *   candidate_po_id, candidate_po_ref
 *   candidate_grn_batch_id, candidate_grn_item_id
 *
 * @return array
 */
function check_purchase_chain()
{
    $rows = array();
    error_log('PCHAIN_FUNC: entered check_purchase_chain()');

    // ---- Part 1: Orphaned invoice items (P5) + item mismatches ----
    $sql = "SELECT
                si.id                          AS invoice_item_id,
                si.supp_trans_no,
                si.supp_trans_type,
                si.grn_item_id                 AS inv_grn_item_id,
                si.po_detail_item_id           AS inv_po_detail_item_id,
                si.stock_id                    AS inv_stock_id,
                si.description                 AS inv_desc,
                si.quantity                    AS inv_qty,
                si.unit_price,
                st.tran_date                   AS inv_date,
                st.supp_reference              AS inv_ref,
                st.ov_amount + st.ov_gst       AS inv_total,
                s.supplier_id,
                s.supp_name,
                g.id                           AS grn_item_id,
                g.item_code                    AS grn_item_code,
                g.description                  AS grn_desc,
                g.qty_recd                     AS grn_qty,
                g.po_detail_item               AS grn_po_detail_item,
                gb.id                          AS grn_batch_id,
                gb.delivery_date               AS grn_date,
                gb.reference                   AS grn_ref,
                gb.purch_order_no              AS grn_po_no,
                pod.order_no                   AS po_order_no,
                pod.po_detail_item,
                pod.item_code                  AS po_item_code,
                pod.quantity_ordered           AS po_qty_ordered,
                po.reference                   AS po_ref
            FROM " . TB_PREF . "supp_invoice_items si
            JOIN " . TB_PREF . "supp_trans st
                ON st.trans_no   = si.supp_trans_no
               AND st.type       = si.supp_trans_type
            JOIN " . TB_PREF . "suppliers s ON s.supplier_id = st.supplier_id
            LEFT JOIN " . TB_PREF . "grn_items g ON g.id = si.grn_item_id
            LEFT JOIN " . TB_PREF . "grn_batch gb ON gb.id = g.grn_batch_id
            LEFT JOIN " . TB_PREF . "purch_order_details pod
                ON pod.po_detail_item = g.po_detail_item
            LEFT JOIN " . TB_PREF . "purch_orders po ON po.order_no = pod.order_no
            WHERE si.supp_trans_type = 20
              AND st.ov_amount != 0
              AND (
                  si.grn_item_id IS NULL
                  OR si.grn_item_id = 0
                  OR g.id IS NULL
                  OR (gb.id IS NOT NULL AND gb.purch_order_no IS NOT NULL AND po.order_no IS NULL)
                  OR (g.id IS NOT NULL AND g.item_code != si.stock_id)
              )
            ORDER BY s.supp_name, st.tran_date";

    error_log('PCHAIN_FUNC: Part 1 SQL: ' . $sql);
    $result = db_query($sql, 'Check purchase chain &#8212; orphaned items');
    error_log('PCHAIN_FUNC: Part 1 result=' . ($result ? 'OK' : 'FAIL'));
    if ($result) {
        $part1_count = 0;
        while ($r = db_fetch($result)) {
            $part1_count++;
            error_log('PCHAIN_FUNC: Part1 row#' . $part1_count . ' item_id=' . $r['invoice_item_id'] . ' grn_id=' . ($r['grn_item_id'] ?? 'null') . ' stock=' . $r['inv_stock_id']);

            $grnExists = ($r['grn_item_id'] !== null);
            $poExists  = ($r['po_order_no'] !== null);

            if ($grnExists && $r['grn_item_code'] !== $r['inv_stock_id']) {
                $rowType = 'item_mismatch';
            } elseif (!$grnExists) {
                $rowType = 'orphan_inv';
            } elseif (!$poExists && $r['grn_po_no'] !== null) {
                $rowType = 'orphan_grn';
            } else {
                continue; // healthy chain &#8212; skip
            }

            $row = array(
                'row_type'        => $rowType,
                'row_key'         => 'inv_item_' . $r['invoice_item_id'],
                'supplier_id'     => (int)$r['supplier_id'],
                'supp_name'       => $r['supp_name'],
                'po_order_no'     => $r['po_order_no'] ? (int)$r['po_order_no'] : null,
                'po_ref'          => $r['po_ref'],
                'po_item_code'    => $r['po_item_code'],
                'grn_batch_id'    => $r['grn_batch_id'] ? (int)$r['grn_batch_id'] : null,
                'grn_item_id'     => $r['grn_item_id'] ? (int)$r['grn_item_id'] : null,
                'grn_ref'         => $r['grn_ref'],
                'grn_date'        => $r['grn_date'],
                'grn_item_code'   => $r['grn_item_code'],
                'grn_qty'         => (float)$r['grn_qty'],
                'inv_trans_no'    => (int)$r['supp_trans_no'],
                'inv_date'        => $r['inv_date'],
                'inv_ref'         => $r['inv_ref'],
                'inv_stock_id'    => $r['inv_stock_id'],
                'inv_qty'         => (float)$r['inv_qty'],
                'inv_total'       => (float)$r['inv_total'],
                'candidate_po_id'         => null,
                'candidate_po_ref'        => null,
                'candidate_grn_batch_id'  => null,
                'candidate_grn_item_id'   => null,
            );

            // Find candidate matches
            if ($rowType === 'orphan_inv') {
                try {
                    $cand = _integ_candidate_grn($r['supplier_id'], $r['inv_stock_id'], $r['inv_date']);
                } catch (\Throwable $t) {
                    $cand = null;
                }
                if ($cand) {
                    $row['candidate_grn_batch_id'] = (int)$cand['grn_batch_id'];
                    $row['candidate_grn_item_id']  = (int)$cand['grn_item_id'];
                }
            } elseif ($rowType === 'orphan_grn') {
                try {
                    $cand = _integ_candidate_po($r['supplier_id'], $r['grn_item_code'], $r['grn_date']);
                } catch (\Throwable $t) {
                    $cand = null;
                }
                if ($cand) {
                    $row['candidate_po_id']  = (int)$cand['order_no'];
                    $row['candidate_po_ref'] = $cand['reference'];
                }
            }

            $rows[] = $row;
        }
        $added = count($rows);
        error_log('PCHAIN_FUNC: Part 1 produced ' . $part1_count . ' rows from query, ' . $added . ' added to array');
    }

    error_log('PCHAIN_FUNC: rows count before Part 2 = ' . count($rows));

    // ---- Part 2: Ghost invoices (no line items) ----
    $sql2 = "SELECT
                st.trans_no,
                st.type,
                st.tran_date,
                st.supp_reference,
                st.reference,
                st.ov_amount + st.ov_gst + st.ov_discount AS total,
                st.alloc,
                s.supplier_id,
                s.supp_name
            FROM " . TB_PREF . "supp_trans st
            JOIN " . TB_PREF . "suppliers s ON st.supplier_id = s.supplier_id
            LEFT JOIN " . TB_PREF . "supp_invoice_items si
                ON si.supp_trans_no = st.trans_no AND si.supp_trans_type = st.type
            WHERE st.type = 20
              AND st.ov_amount != 0
              AND si.id IS NULL
            ORDER BY s.supp_name, st.tran_date";

    $result2 = db_query($sql2, 'Check purchase chain &#8212; ghost invoices');
    if ($result2) {
        $part2_count = 0;
        while ($r = db_fetch($result2)) {
            $part2_count++;
            $rows[] = array(
                'row_type'        => 'ghost_inv',
                'row_key'         => 'ghost_sup_' . $r['trans_no'],
                'supplier_id'     => (int)$r['supplier_id'],
                'supp_name'       => $r['supp_name'],
                'po_order_no'     => null,
                'po_ref'          => null,
                'po_item_code'    => null,
                'grn_batch_id'    => null,
                'grn_item_id'     => null,
                'grn_ref'         => null,
                'grn_date'        => null,
                'grn_item_code'   => null,
                'grn_qty'         => 0.0,
                'inv_trans_no'    => (int)$r['trans_no'],
                'inv_date'        => $r['tran_date'],
                'inv_ref'         => $r['reference'],
                'inv_stock_id'    => null,
                'inv_qty'         => 0.0,
                'inv_total'       => (float)$r['total'],
                'candidate_po_id'         => null,
                'candidate_po_ref'        => null,
                'candidate_grn_batch_id'  => null,
                'candidate_grn_item_id'   => null,
            );
        }
    }

    // ---- Part 3: Orphaned GRN batches with no invoice items (standalone P6) ----
    $sql3 = "SELECT
                gb.id            AS grn_batch_id,
                gb.delivery_date AS grn_date,
                gb.reference     AS grn_ref,
                gb.purch_order_no,
                gb.supplier_id,
                s.supp_name,
                gi.id            AS grn_item_id,
                gi.item_code     AS grn_item_code,
                gi.description   AS grn_desc,
                gi.qty_recd      AS grn_qty
            FROM " . TB_PREF . "grn_batch gb
            JOIN " . TB_PREF . "suppliers s ON s.supplier_id = gb.supplier_id
            JOIN " . TB_PREF . "grn_items gi ON gi.grn_batch_id = gb.id
            LEFT JOIN " . TB_PREF . "purch_orders po ON po.order_no = gb.purch_order_no
            WHERE gb.purch_order_no IS NOT NULL
              AND gb.purch_order_no > 0
              AND po.order_no IS NULL
            ORDER BY s.supp_name, gb.delivery_date";

    $result3 = db_query($sql3, 'Check purchase chain &#8212; standalone orphaned GRN');
    if ($result3) {
        $part3_count = 0;
        while ($r = db_fetch($result3)) {
            $part3_count++;
            $row = array(
                'row_type'        => 'orphan_grn',
                'row_key'         => 'grn_' . $r['grn_batch_id'],
                'supplier_id'     => (int)$r['supplier_id'],
                'supp_name'       => $r['supp_name'],
                'po_order_no'     => null,
                'po_ref'          => null,
                'po_item_code'    => null,
                'grn_batch_id'    => (int)$r['grn_batch_id'],
                'grn_item_id'     => (int)$r['grn_item_id'],
                'grn_ref'         => $r['grn_ref'],
                'grn_date'        => $r['grn_date'],
                'grn_item_code'   => $r['grn_item_code'],
                'grn_qty'         => (float)$r['grn_qty'],
                'inv_trans_no'    => null,
                'inv_date'        => null,
                'inv_ref'         => null,
                'inv_stock_id'    => null,
                'inv_qty'         => 0.0,
                'inv_total'       => 0.0,
                'candidate_po_id'         => null,
                'candidate_po_ref'        => null,
                'candidate_grn_batch_id'  => null,
                'candidate_grn_item_id'   => null,
            );

            try {
                $cand = _integ_candidate_po($r['supplier_id'], $r['grn_item_code'], $r['grn_date']);
            } catch (\Throwable $t) {
                $cand = null;
            }
            if ($cand) {
                $row['candidate_po_id']  = (int)$cand['order_no'];
                $row['candidate_po_ref'] = $cand['reference'];
            }

            $rows[] = $row;
        }
    }

    // ---- Part 4: Standalone GRN items with valid PO but no invoice items ----
    $sql4 = "SELECT
                gb.id            AS grn_batch_id,
                gb.delivery_date AS grn_date,
                gb.reference     AS grn_ref,
                gb.supplier_id,
                s.supp_name,
                gi.id            AS grn_item_id,
                gi.item_code     AS grn_item_code,
                gi.description   AS grn_desc,
                gi.qty_recd      AS grn_qty,
                gi.quantity_inv  AS grn_qty_inv,
                pod.order_no     AS po_order_no,
                pod.po_detail_item,
                pod.item_code    AS po_item_code,
                pod.quantity_ordered AS po_qty_ordered,
                po.reference     AS po_ref
            FROM " . TB_PREF . "grn_batch gb
            JOIN " . TB_PREF . "suppliers s ON s.supplier_id = gb.supplier_id
            JOIN " . TB_PREF . "grn_items gi ON gi.grn_batch_id = gb.id
            JOIN " . TB_PREF . "purch_order_details pod ON pod.po_detail_item = gi.po_detail_item
            JOIN " . TB_PREF . "purch_orders po ON po.order_no = pod.order_no
            WHERE gi.quantity_inv < gi.qty_recd - 0.001
              AND NOT EXISTS (
                  SELECT 1 FROM " . TB_PREF . "supp_invoice_items si2
                  WHERE si2.grn_item_id = gi.id
                    AND si2.supp_trans_type = 20
              )
            ORDER BY s.supp_name, gb.delivery_date";

    $result4 = db_query($sql4, 'Check purchase chain &#8212; standalone GRN with PO');
    if ($result4) {
        while ($r = db_fetch($result4)) {
            $rows[] = array(
                'row_type'        => 'standalone_grn',
                'row_key'         => 'sgrn_' . $r['grn_item_id'],
                'supplier_id'     => (int)$r['supplier_id'],
                'supp_name'       => $r['supp_name'],
                'po_order_no'     => (int)$r['po_order_no'],
                'po_ref'          => $r['po_ref'],
                'po_item_code'    => $r['po_item_code'],
                'grn_batch_id'    => (int)$r['grn_batch_id'],
                'grn_item_id'     => (int)$r['grn_item_id'],
                'grn_ref'         => $r['grn_ref'],
                'grn_date'        => $r['grn_date'],
                'grn_item_code'   => $r['grn_item_code'],
                'grn_qty'         => (float)$r['grn_qty'],
                'inv_trans_no'    => null,
                'inv_date'        => null,
                'inv_ref'         => null,
                'inv_stock_id'    => null,
                'inv_qty'         => 0.0,
                'inv_total'       => 0.0,
                'candidate_po_id'         => null,
                'candidate_po_ref'        => null,
                'candidate_grn_batch_id'  => null,
                'candidate_grn_item_id'   => null,
            );
        }
    }

    $finalCount = count($rows);
    return $rows;
}

// ===========================================================================
// CONSOLIDATED SALES CHAIN CHECK
// ===========================================================================

/**
 * Run all sales chain issue checks and return a unified array.
 *
 * Each row:
 *   row_type &#8212; 'orphan_del' | 'missing_stock' | 'ghost_inv'
 *   debtor_no, customer_name
 *   so_order_no, so_ref, so_item_code
 *   del_trans_no, del_date, del_ref, del_item_code, del_qty
 *   inv_trans_no, inv_date, inv_ref
 *   candidate_so_id, candidate_so_ref
 *
 * @return array
 */
function check_sales_chain()
{
    $rows = array();
    error_log('SCHAIN: entered check_sales_chain()');

    // ---- Part 1: Orphaned deliveries (S4) + missing stock moves (S5) + ghost deliveries ----
    $sql = "SELECT
                dt.trans_no               AS del_trans_no,
                dt.type                   AS del_type,
                dt.tran_date              AS del_date,
                dt.reference              AS del_ref,
                dt.order_                 AS so_order_no,
                dt.debtor_no,
                dm.name                   AS customer_name,
                dtd.id                    AS del_detail_id,
                dtd.stock_id              AS del_item_code,
                dtd.description           AS del_desc,
                dtd.qty_done              AS del_qty,
                dtd.unit_price,
                dtd.src_id,
                sod.id                    AS sod_id,
                sod.stk_code              AS so_item_code,
                sod.order_no              AS sod_order_no,
                so.reference              AS so_ref,
                sm.trans_id               AS stock_move_id
            FROM " . TB_PREF . "debtor_trans dt
            JOIN " . TB_PREF . "debtors_master dm ON dm.debtor_no = dt.debtor_no
            LEFT JOIN " . TB_PREF . "debtor_trans_details dtd
                ON dtd.debtor_trans_no = dt.trans_no AND dtd.debtor_trans_type = dt.type
            LEFT JOIN " . TB_PREF . "sales_orders so
                ON so.order_no = dt.order_ AND so.trans_type = 30
            LEFT JOIN " . TB_PREF . "sales_order_details sod
                ON sod.id = dtd.src_id
            LEFT JOIN " . TB_PREF . "stock_moves sm
                ON sm.trans_no = dt.trans_no AND sm.type = dt.type
                    AND sm.stock_id = dtd.stock_id
            WHERE dt.type = 13
              AND dt.ov_amount != 0
              AND (
                  dt.order_ IS NULL
                  OR dt.order_ < 1
                  OR (dt.order_ > 0 AND so.order_no IS NULL)
                  OR sm.trans_id IS NULL
              )
            ORDER BY dm.name, dt.tran_date, dtd.id";

    error_log('SCHAIN: Part 1 SQL prepared');
    $result = db_query($sql, 'Check sales chain &#8212; deliveries');
    error_log('SCHAIN: Part 1 result=' . ($result ? 'OK' : 'FAIL'));
    if ($result) {
        $part1_count = 0;
        while ($r = db_fetch($result)) {
            $part1_count++;
            error_log('SCHAIN: Part1 row#' . $part1_count . ' del=' . $r['del_trans_no'] . ' order_=' . var_export($r['so_order_no'], true) . ' stock_move=' . var_export($r['stock_move_id'], true) . ' del_detail=' . var_export($r['del_detail_id'], true));

            $soMissing  = ($r['so_order_no'] < 1 || $r['so_order_no'] === null || $r['so_ref'] === null);
            $stkMissing = ($r['stock_move_id'] === null);

            if ($soMissing) {
                $rowType = 'orphan_del';
            } elseif ($stkMissing) {
                $rowType = 'missing_stock';
            } else {
                continue;
            }

            $row = array(
                'row_type'        => $rowType,
                'row_key'         => 'del_' . $r['del_trans_no'] . '_' . ($r['del_detail_id'] ?: '0'),
                'debtor_no'       => (int)$r['debtor_no'],
                'customer_name'   => $r['customer_name'],
                'so_order_no'     => $r['so_order_no'] ? (int)$r['so_order_no'] : null,
                'so_ref'          => $r['so_ref'],
                'so_item_code'    => $r['so_item_code'],
                'del_trans_no'    => (int)$r['del_trans_no'],
                'del_date'        => $r['del_date'],
                'del_ref'         => $r['del_ref'],
                'del_item_code'   => $r['del_item_code'],
                'del_qty'         => (float)$r['del_qty'],
                'candidate_so_id'   => null,
                'candidate_so_ref'  => null,
                'inv_trans_no'      => null,
                'inv_date'          => null,
                'inv_ref'           => null,
                'inv_total'         => 0.0,
                'inv_alloc'         => 0.0,
                'payment_trans_no'  => null,
                'payment_date'      => null,
                'payment_ref'       => null,
                'payment_amount'    => 0.0,
            );

            if ($rowType === 'orphan_del' && $r['del_item_code']) {
                $cand = _integ_candidate_so($r['debtor_no'], $r['del_item_code'], $r['del_date']);
                if ($cand) {
                    $row['candidate_so_id']  = (int)$cand['order_no'];
                    $row['candidate_so_ref'] = $cand['reference'];
                }
            }

            $rows[] = $row;
        }
    }

    // ---- Part 2: Ghost customer invoices (S3) ----
    $sql2 = "SELECT
                dt.trans_no,
                dt.type,
                dt.tran_date,
                dt.reference,
                dt.ov_amount + dt.ov_gst + dt.ov_freight + dt.ov_freight_tax AS total,
                dt.alloc,
                dt.debtor_no,
                dm.name AS customer_name
            FROM " . TB_PREF . "debtor_trans dt
            JOIN " . TB_PREF . "debtors_master dm ON dm.debtor_no = dt.debtor_no
            LEFT JOIN " . TB_PREF . "debtor_trans_details dtd
                ON dtd.debtor_trans_no = dt.trans_no AND dtd.debtor_trans_type = dt.type
            WHERE dt.type = 10
              AND dt.ov_amount != 0
              AND dtd.id IS NULL
            ORDER BY dm.name, dt.tran_date";

    $result2 = db_query($sql2, 'Check sales chain &#8212; ghost invoices');
    if ($result2) {
        $part2_count = 0;
        while ($r = db_fetch($result2)) {
            $part2_count++;
            $rows[] = array(
                'row_type'       => 'ghost_inv',
                'row_key'        => 'ghost_cus_' . $r['trans_no'],
                'debtor_no'      => (int)$r['debtor_no'],
                'customer_name'  => $r['customer_name'],
                'so_order_no'    => null,
                'so_ref'         => null,
                'so_item_code'   => null,
                'del_trans_no'   => null,
                'del_date'       => null,
                'del_ref'        => null,
                'del_item_code'  => null,
                'del_qty'        => 0.0,
                'inv_trans_no'      => (int)$r['trans_no'],
                'inv_date'          => $r['tran_date'],
                'inv_ref'           => $r['reference'],
                'inv_total'         => (float)$r['total'],
                'inv_alloc'         => (float)$r['alloc'],
                'payment_trans_no'  => null,
                'payment_date'      => null,
                'payment_ref'       => null,
                'payment_amount'    => 0.0,
                'candidate_so_id'   => null,
                'candidate_so_ref'  => null,
            );
        }
    }

    // ---- Attach invoice data to delivery rows by matching SO ----
    $soNos = array();
    foreach ($rows as $r) {
        if ($r['row_type'] !== 'ghost_inv' && $r['so_order_no']) {
            $soNos[(int)$r['so_order_no']] = true;
        }
    }

    if (!empty($soNos)) {
        $soList = implode(',', array_keys($soNos));
        $invSql = "SELECT order_ AS so_no, trans_no, tran_date, reference,
                          ov_amount + ov_gst + ov_freight + ov_freight_tax AS total,
                          alloc
                   FROM " . TB_PREF . "debtor_trans
                   WHERE type = 10 AND order_ IN ($soList)";
        $invResult = db_query($invSql, 'Load invoices for SCHAIN');

        $invBySO = array();
        while ($inv = db_fetch($invResult)) {
            $soNo = (int)$inv['so_no'];
            unset($inv['so_no']);
            $invBySO[$soNo][] = $inv;
        }

        foreach ($rows as &$r) {
            if ($r['row_type'] !== 'ghost_inv' && $r['so_order_no'] && isset($invBySO[(int)$r['so_order_no']])) {
                $invs = $invBySO[(int)$r['so_order_no']];
                $r['inv_trans_no'] = (int)$invs[0]['trans_no'];
                $r['inv_date']     = $invs[0]['tran_date'];
                $r['inv_ref']      = $invs[0]['reference'];
                $r['inv_total']    = (float)$invs[0]['total'];
                $r['inv_alloc']    = (float)$invs[0]['alloc'];
            }
        }
        unset($r);
    }

    // ---- Attach payment data for invoices ----
    $invNos = array();
    foreach ($rows as $r) {
        if ($r['inv_trans_no']) {
            $invNos[] = (int)$r['inv_trans_no'];
        }
    }
    $invNos = array_unique($invNos);

    if (!empty($invNos)) {
        $invList = implode(',', $invNos);
        $paySql = "SELECT ca.trans_no_from AS inv_no,
                          dt.trans_no AS pay_no, dt.tran_date AS pay_date,
                          dt.reference AS pay_ref, dt.ov_amount AS pay_amt
                   FROM " . TB_PREF . "cust_allocations ca
                   JOIN " . TB_PREF . "debtor_trans dt
                       ON dt.trans_no = ca.trans_no_to AND dt.type = ca.trans_type_to
                   WHERE ca.trans_no_from IN ($invList) AND ca.trans_type_from = 10
                   ORDER BY dt.tran_date";
        $payResult = db_query($paySql, 'Load payments for SCHAIN invoices');

        $payByInv = array();
        while ($pay = db_fetch($payResult)) {
            $invNo = (int)$pay['inv_no'];
            if (!isset($payByInv[$invNo])) {
                $payByInv[$invNo] = array();
            }
            $payByInv[$invNo][] = $pay;
        }

        foreach ($rows as &$r) {
            $r['payments'] = array();
            if ($r['inv_trans_no'] && isset($payByInv[(int)$r['inv_trans_no']])) {
                $r['payments'] = $payByInv[(int)$r['inv_trans_no']];
            }
        }
        unset($r);
    }

    $finalCount = count($rows);
    return $rows;
}

// ===========================================================================
// PER-ROW FIX ACTION HANDLERS &#8212; PURCHASE CHAIN
// ===========================================================================

/**
 * Handle a per-row fix action for the purchase chain.
 *
 * @param  string $rowKey  e.g. 'inv_item_123' or 'ghost_sup_456'
 * @param  string $action  'add_po' | 'add_grn' | 'add_grn_assoc_po' |
 *                         'add_grn_assoc_po_inv' | 'add_po_grn' |
 *                         'match_po_grn' | 'match_grn_inv' |
 *                         'disassociate' | 'void_ghost'
 * @return array  ['message'=>string, 'popup_url'=>string|null]
 */
function fix_purchase_chain_row($rowKey, $action)
{
    $parts = explode('_', $rowKey);

    $db = integ_db_adapter();

    // Row key formats:
    //   inv_item_{id}     &#8212; orphaned invoice item
    //   ghost_sup_{id}    &#8212; ghost supplier invoice
    //   grn_{batch_id}    &#8212; standalone orphaned GRN batch
    if (strpos($rowKey, 'inv_item_') === 0) {
        $invoiceItemId = (int)$parts[2];

        $sql = "SELECT si.*, st.tran_date, st.supplier_id, st.supp_reference
                FROM " . TB_PREF . "supp_invoice_items si
                JOIN " . TB_PREF . "supp_trans st
                    ON st.trans_no = si.supp_trans_no AND st.type = si.supp_trans_type
                WHERE si.id = $invoiceItemId";
        $result = db_query($sql, 'Load invoice item for fix');
        if (!$result || !($item = db_fetch($result))) {
            return array('message' => _('Invoice item not found: ') . $invoiceItemId, 'popup_url' => null);
        }

        switch ($action) {
            case 'add_grn':
                return _chain_do_add_grn($db, $item, null, false);

            case 'add_grn_assoc_po':
                return _chain_do_add_grn($db, $item, null, false);

            case 'add_grn_assoc_po_inv':
                return _chain_do_add_grn($db, $item, null, true);

            case 'add_po_grn':
                return _chain_do_add_po_grn($db, $item);

            case 'match_grn_inv':
                $candBatchId = isset($_POST['_cand_grn_batch']) ? (int)$_POST['_cand_grn_batch'] : 0;
                $candItemId  = isset($_POST['_cand_grn_item'])  ? (int)$_POST['_cand_grn_item'] : 0;
                if ($candBatchId && $candItemId) {
                    return _chain_do_match_grn_inv($db, $invoiceItemId, $candBatchId, $candItemId);
                }
                return array('message' => _('No candidate GRN specified for matching.'), 'popup_url' => null);

            case 'disassociate':
                return _chain_do_disassociate($db, $invoiceItemId);

            default:
                return array('message' => _('Unknown action: ') . $action, 'popup_url' => null);
        }
    }

    if (strpos($rowKey, 'ghost_sup_') === 0) {
        $transNo = (int)$parts[2];
        if ($action === 'void_ghost') {
            return _chain_do_void_ghost_supplier($db, $transNo);
        }
        return array('message' => _('Unknown action for ghost invoice: ') . $action, 'popup_url' => null);
    }

    if (strpos($rowKey, 'grn_') === 0) {
        $grnBatchId = (int)$parts[1];
        if ($action === 'add_po') {
            return _chain_do_add_po_from_grn($db, $grnBatchId);
        }
        return array('message' => _('Unknown action for orphaned GRN: ') . $action, 'popup_url' => null);
    }

    return array('message' => _('Unhandled row: ') . $rowKey, 'popup_url' => null);
}

/**
 * Handle a per-row fix action for the sales chain.
 *
 * @param  string $rowKey
 * @param  string $action
 * @return array
 */
function fix_sales_chain_row($rowKey, $action)
{
    $parts = explode('_', $rowKey);

    $db = integ_db_adapter();

    if (strpos($rowKey, 'del_') === 0) {
        $delTransNo = (int)$parts[1];

        if ($action === 'add_so') {
            return _chain_do_sales_add_so($db, $delTransNo);
        }
        if ($action === 'match_so_del') {
            $candSoNo = isset($_POST['_cand_so']) ? (int)$_POST['_cand_so'] : 0;
            if ($candSoNo) {
                return _chain_do_sales_match_so($db, $delTransNo, $candSoNo);
            }
            return array('message' => _('No candidate SO specified.'), 'popup_url' => null);
        }
        if ($action === 'add_stock_moves') {
            return _chain_do_sales_add_stock_moves($db, $delTransNo);
        }
    }

    if (strpos($rowKey, 'ghost_cus_') === 0) {
        $transNo = (int)$parts[2];
        if ($action === 'void_ghost') {
            return _chain_do_void_ghost_customer($db, $transNo);
        }
    }

    return array('message' => _('Unhandled row: ') . $rowKey, 'popup_url' => null);
}

// ---------------------------------------------------------------------------

/**
 * Handle a per-row fix action for SCUST (customer consistency).
 *
 * Actions:
 *   'use_so_cust'    — update debtor_no/branch_code to match the linked SO
 *   'use_branch_cust' — update debtor_no to match the branch's customer owner
 *   'apply_cust'     — apply debtor_no/branch_code from _scust_new_debtor/_scust_new_branch POST
 *
 * @param  string $rowKey  e.g. 'scust_13_123'
 * @param  string $action
 * @return array  ['message'=>string, 'popup_url'=>string|null]
 */
function fix_sales_customer_row($rowKey, $action)
{
    $parts = explode('_', $rowKey);
    if (count($parts) < 3 || $parts[0] !== 'scust') {
        return array('message' => _('Invalid row key: ') . $rowKey, 'popup_url' => null);
    }

    $type     = (int)$parts[1];
    $transNo  = (int)$parts[2];
    $db       = integ_db_adapter();

    // Load current transaction debtor/branch
    $sql = "SELECT debtor_no, branch_code, order_ FROM " . TB_PREF . "debtor_trans
            WHERE trans_no = $transNo AND type = $type";
    $r = db_query($sql, 'Load trans for SCUST fix');
    if (!$r || !($trans = db_fetch($r))) {
        return array('message' => _('Transaction not found: #') . $transNo, 'popup_url' => null);
    }

    if ($action === 'use_so_cust') {
        // Update debtor_no/branch_code to match the referenced SO
        $soSql = "SELECT debtor_no, branch_code FROM " . TB_PREF . "sales_orders
                  WHERE order_no = " . (int)$trans['order_'] . " AND trans_type = 30";
        $sr = db_query($soSql, 'Load SO for SCUST fix');
        if (!$sr || !($so = db_fetch($sr))) {
            return array('message' => _('Linked SO not found for trans #') . $transNo, 'popup_url' => null);
        }
        $db->execute("UPDATE " . TB_PREF . "debtor_trans
                      SET debtor_no = " . (int)$so['debtor_no'] . ",
                          branch_code = " . (int)$so['branch_code'] . "
                      WHERE trans_no = $transNo AND type = $type");
        return array('message' => sprintf(_('Trans #%d updated: debtor_no=%d, branch_code=%d (from SO #%d).'),
            $transNo, (int)$so['debtor_no'], (int)$so['branch_code'], (int)$trans['order_']), 'popup_url' => null);
    }

    if ($action === 'use_branch_cust') {
        // Update debtor_no to match the branch's customer owner
        $brSql = "SELECT debtor_no FROM " . TB_PREF . "cust_branch
                  WHERE branch_code = " . (int)$trans['branch_code'];
        $br = db_query($brSql, 'Load branch for SCUST fix');
        if (!$br || !($branch = db_fetch($br))) {
            return array('message' => _('Branch not found for branch_code ') . (int)$trans['branch_code'], 'popup_url' => null);
        }
        $db->execute("UPDATE " . TB_PREF . "debtor_trans
                      SET debtor_no = " . (int)$branch['debtor_no'] . "
                      WHERE trans_no = $transNo AND type = $type");
        return array('message' => sprintf(_('Trans #%d updated: debtor_no=%d (from branch owner).'),
            $transNo, (int)$branch['debtor_no']), 'popup_url' => null);
    }

    if ($action === 'apply_cust') {
        $newDebtor   = isset($_POST['_scust_new_debtor'])  ? (int)$_POST['_scust_new_debtor'] : 0;
        $newBranch   = isset($_POST['_scust_new_branch'])  ? (int)$_POST['_scust_new_branch'] : 0;
        if (!$newDebtor) {
            return array('message' => _('No customer selected.'), 'popup_url' => null);
        }
        $db->execute("UPDATE " . TB_PREF . "debtor_trans
                      SET debtor_no = $newDebtor, branch_code = $newBranch
                      WHERE trans_no = $transNo AND type = $type");
        return array('message' => sprintf(_('Trans #%d updated: debtor_no=%d, branch_code=%d.'),
            $transNo, $newDebtor, $newBranch), 'popup_url' => null);
    }

    return array('message' => _('Unknown SCUST action: ') . $action, 'popup_url' => null);
}

// ===========================================================================
// INTERNAL FIX ACTION IMPLEMENTATIONS
// ===========================================================================

/**
 * Add a PO for an orphaned GRN batch (no invoice item involved).
 */
function _chain_do_add_po_from_grn($db, $grnBatchId)
{
    // Load GRN batch + items
    $sql = "SELECT gb.*, gi.id AS grn_item_id, gi.item_code, gi.description,
                   gi.qty_recd, gi.unit_cost
            FROM " . TB_PREF . "grn_batch gb
            JOIN " . TB_PREF . "grn_items gi ON gi.grn_batch_id = gb.id
            WHERE gb.id = $grnBatchId";
    $r = db_query($sql, 'Load GRN for add_po');
    if (!$r || !($grn = db_fetch($r))) {
        return array('message' => _('GRN batch not found: ') . $grnBatchId, 'popup_url' => null);
    }

    // Create PO header + detail line
    $po = _integ_create_po(
        $db,
        $grn['supplier_id'],
        $grn['delivery_date'],
        'Auto-reconstructed from orphaned GRN #' . $grnBatchId,
        $grn['item_code'],
        $grn['description'],
        $grn['qty_recd'],
        $grn['unit_cost']
    );
    $poNo    = $po['order_no'];
    $podItem = $po['po_detail_item'];

    // Link GRN batch &#8594; PO
    $db->execute("UPDATE " . TB_PREF . "grn_batch SET purch_order_no = $poNo WHERE id = $grnBatchId");

    // Link GRN items &#8594; PO details
    if ($podItem) {
        $db->execute("UPDATE " . TB_PREF . "grn_items SET po_detail_item = $podItem WHERE grn_batch_id = $grnBatchId");
    }

    _chain_recalc_all($db);

    $message = sprintf(_('Created PO #%d from orphaned GRN #%d and linked.'), $poNo, $grnBatchId);
    return array('message' => $message, 'popup_url' => null);
}

/**
 * Add GRN for an orphaned invoice item, optionally linking to PO and/or Invoice.
 *
 * @param  bool  $alsoLinkInv  true = also link invoice item to new GRN
 */
function _chain_do_add_grn($db, $item, $assocPoNo = null, $alsoLinkInv = false)
{
    $supplierId = (int)$item['supplier_id'];
    $stockId    = $item['stock_id'];
    $qty        = (float)$item['quantity'];
    $price      = (float)$item['unit_price'];
    $date       = $item['tran_date'];
    $itemId     = (int)$item['id'];

    // Find or create PO if needed
    $podItem = 0;
    if (!$assocPoNo) {
        $cand = _integ_candidate_po($supplierId, $stockId, $date);
        if ($cand) {
            $assocPoNo = (int)$cand['order_no'];
        }
    }

    if ($assocPoNo) {
        // PO exists &#8212; find its detail line ID for linking
        $podSql = "SELECT po_detail_item FROM " . TB_PREF . "purch_order_details
                   WHERE order_no = $assocPoNo AND item_code = '" . db_escape($stockId) . "'
                   LIMIT 1";
        $podR = db_query($podSql, 'Find PO detail item for GRN link');
        if ($podR && ($podRow = db_fetch($podR))) {
            $podItem = (int)$podRow['po_detail_item'];
        }

        // Create GRN linked to PO
        $grnBatchId = _integ_create_grn($db, $assocPoNo, $supplierId, $date, 'Auto-reconstructed for invoice item #' . $itemId);
        $grnItemId  = _integ_create_grn_item($db, $grnBatchId, $podItem, $stockId, $item['description'], $qty, $price);
    } else {
        // No PO &#8212; create GRN only
        $grnBatchId = _integ_create_grn($db, 0, $supplierId, $date, 'Auto-reconstructed for invoice item #' . $itemId);
        $grnItemId  = _integ_create_grn_item($db, $grnBatchId, 0, $stockId, $item['description'], $qty, $price);
    }

    // Optionally link invoice item to new GRN
    if ($alsoLinkInv && $grnItemId) {
        $db->execute("UPDATE " . TB_PREF . "supp_invoice_items SET grn_item_id = $grnItemId WHERE id = $itemId");
    }

    _chain_recalc_all($db);

    $msg = sprintf(_('Created GRN #%d (item #%d) for invoice item #%d.'), $grnBatchId, $grnItemId, $itemId);
    if ($assocPoNo) {
        $msg .= ' ' . sprintf(_('Linked to PO #%d.'), $assocPoNo);
    }
    if ($alsoLinkInv) {
        $msg .= ' ' . _('Invoice item linked to GRN.');
    }
    $popupUrl = _integ_invoice_popup_url($item['supp_trans_no']);
    return array('message' => $msg, 'popup_url' => $popupUrl);
}

/**
 * Create both PO and GRN for a totally orphaned invoice item.
 */
function _chain_do_add_po_grn($db, $item)
{
    $supplierId = (int)$item['supplier_id'];
    $stockId    = $item['stock_id'];
    $qty        = (float)$item['quantity'];
    $price      = (float)$item['unit_price'];
    $date       = $item['tran_date'];
    $itemId     = (int)$item['id'];

    // Create PO
    $po = _integ_create_po(
        $db, $supplierId, $date,
        'Auto-reconstructed from orphaned invoice #' . $item['supp_trans_no'],
        $stockId, $item['description'], $qty, $price
    );
    $poNo    = $po['order_no'];
    $podItem = $po['po_detail_item'];

    // Create GRN linked to PO
    $grnBatchId = _integ_create_grn($db, $poNo, $supplierId, $date, 'Auto-reconstructed for invoice item #' . $itemId);
    $grnItemId  = _integ_create_grn_item($db, $grnBatchId, $podItem, $stockId, $item['description'], $qty, $price);

    // Link invoice to GRN
    if ($grnItemId) {
        $db->execute("UPDATE " . TB_PREF . "supp_invoice_items SET grn_item_id = $grnItemId WHERE id = $itemId");
    }

    _chain_recalc_all($db);

    $msg = sprintf(_('Created PO #%d and GRN #%d (item #%d) for invoice item #%d.'), $poNo, $grnBatchId, $grnItemId, $itemId);
    $popupUrl = _integ_invoice_popup_url($item['supp_trans_no']);
    return array('message' => $msg, 'popup_url' => $popupUrl);
}

/**
 * Match an orphaned GRN batch to a candidate PO.
 * Updates grn_batch.purch_order_no and grn_items.po_detail_item.
 */
function _chain_do_match_po_grn($db, $grnBatchId, $poNo)
{
    $db->execute("UPDATE " . TB_PREF . "grn_batch SET purch_order_no = $poNo WHERE id = $grnBatchId");

    // Link GRN items to PO detail items (by item_code match)
    $sql = "SELECT gi.id AS grn_item_id, gi.item_code,
                   pod.po_detail_item
            FROM " . TB_PREF . "grn_items gi
            JOIN " . TB_PREF . "purch_order_details pod
                ON pod.order_no = $poNo AND pod.item_code = gi.item_code
            WHERE gi.grn_batch_id = $grnBatchId";
    $r = db_query($sql, 'Match GRN items to PO details');
    if ($r) {
        while ($row = db_fetch($r)) {
            $db->execute("UPDATE " . TB_PREF . "grn_items SET po_detail_item = " . (int)$row['po_detail_item']
                . " WHERE id = " . (int)$row['grn_item_id']);
        }
    }

    _chain_recalc_all($db);

    $msg = sprintf(_('Linked GRN #%d to PO #%d.'), $grnBatchId, $poNo);
    return array('message' => $msg, 'popup_url' => null);
}

/**
 * Match an orphaned invoice item to a candidate GRN.
 */
function _chain_do_match_grn_inv($db, $invoiceItemId, $candBatchId, $candItemId)
{
    $db->execute("UPDATE " . TB_PREF . "supp_invoice_items
                  SET grn_item_id = $candItemId
                  WHERE id = $invoiceItemId");

    _chain_recalc_all($db);

    // Load invoice trans_no for popup
    $sql = "SELECT supp_trans_no FROM " . TB_PREF . "supp_invoice_items WHERE id = $invoiceItemId";
    $r = db_query($sql, 'Get inv trans for popup');
    $invTransNo = ($r && ($row = db_fetch($r))) ? (int)$row['supp_trans_no'] : 0;

    $msg = sprintf(_('Linked invoice item #%d to GRN item #%d (batch #%d).'), $invoiceItemId, $candItemId, $candBatchId);
    return array('message' => $msg, 'popup_url' => _integ_invoice_popup_url($invTransNo));
}

/**
 * Disassociate mismatched GRN↔Invoice links (clears both sides when GRN
 * also has no PO, or clears just GRN↔Inv when PO is fine).
 */
function _chain_do_disassociate($db, $invoiceItemId)
{
    $prefix = $db->getTablePrefix();

    // Load current links using Repository
    $repo = new \FrontAccounting\Repository\SupplierInvoiceItemRepository($db);
    $items = $repo->findWhere(array('id' => $invoiceItemId));
    $item = $items ? $items[0] : null;
    $grnItemId = $item ? (int)$item['grn_item_id'] : 0;

    // Clear both GRN and PO links from the invoice item.
    // The PO↔GRN relationship stays intact &#8212; only the invoice is detached.
    $db->execute(
        "UPDATE {$prefix}supp_invoice_items SET grn_item_id = NULL, po_detail_item_id = NULL WHERE id = ?",
        [$invoiceItemId]
    );

    _chain_recalc_all($db);

    $msg = sprintf(_('Disassociated invoice item #%d from GRN item #%d.'), $invoiceItemId, $grnItemId);
    $popupUrl = _integ_invoice_popup_url_for_item($invoiceItemId);
    return array('message' => $msg, 'popup_url' => $popupUrl);
}

/**
 * Void a ghost supplier invoice (no line items).
 */
function _chain_do_void_ghost_supplier($db, $transNo)
{
    // Zero the financial amounts
    $db->execute("UPDATE " . TB_PREF . "supp_trans SET
                  ov_amount = 0, ov_gst = 0, ov_discount = 0, alloc = 0
                  WHERE trans_no = $transNo AND type = 20");

    $msg = sprintf(_('Voided ghost supplier invoice #%d (amounts zeroed).'), $transNo);
    return array('message' => $msg, 'popup_url' => null);
}

/**
 * Void a ghost customer invoice (no line items).
 */
function _chain_do_void_ghost_customer($db, $transNo)
{
    $db->execute("UPDATE " . TB_PREF . "debtor_trans SET
                  ov_amount = 0, ov_gst = 0, ov_freight = 0, ov_freight_tax = 0,
                  ov_discount = 0, alloc = 0
                  WHERE trans_no = $transNo AND type = 10");

    $msg = sprintf(_('Voided ghost customer invoice #%d (amounts zeroed).'), $transNo);
    return array('message' => $msg, 'popup_url' => null);
}

// ===========================================================================
// INTERNAL FIX ACTION IMPLEMENTATIONS &#8212; SALES
// ===========================================================================

/**
 * Add SO for orphaned delivery note.
 */
function _chain_do_sales_add_so($db, $delTransNo)
{
    // Load delivery details
    $sql = "SELECT dt.*, dtd.stock_id, dtd.description, dtd.qty_done, dtd.unit_price
            FROM " . TB_PREF . "debtor_trans dt
            JOIN " . TB_PREF . "debtor_trans_details dtd
                ON dtd.debtor_trans_no = dt.trans_no AND dtd.debtor_trans_type = dt.type
            WHERE dt.trans_no = $delTransNo AND dt.type = 13
            LIMIT 1";
    $r = db_query($sql, 'Load delivery for add_so');
    if (!$r || !($del = db_fetch($r))) {
        return array('message' => _('Delivery not found: ') . $delTransNo, 'popup_url' => null);
    }

    $soNo = _integ_create_so(
        $db,
        $del['debtor_no'],
        $del['tran_date'],
        'Auto-reconstructed from orphaned delivery #' . $delTransNo,
        $del['stock_id'],
        $del['description'],
        (float)$del['qty_done'],
        (float)$del['unit_price']
    );

    // Link delivery &#8594; SO
    $db->execute("UPDATE " . TB_PREF . "debtor_trans SET order_ = $soNo WHERE trans_no = $delTransNo AND type = 13");

    _chain_recalc_all($db);

    $msg = sprintf(_('Created SO #%d from orphaned delivery #%d and linked.'), $soNo, $delTransNo);
    return array('message' => $msg, 'popup_url' => null);
}

/**
 * Match orphaned delivery to candidate SO.
 */
function _chain_do_sales_match_so($db, $delTransNo, $soNo)
{
    $db->execute("UPDATE " . TB_PREF . "debtor_trans SET order_ = $soNo WHERE trans_no = $delTransNo AND type = 13");

    // Link delivery detail src_id to SO detail
    $sql = "SELECT dtd.id, dtd.stock_id
            FROM " . TB_PREF . "debtor_trans_details dtd
            WHERE dtd.debtor_trans_no = $delTransNo AND dtd.debtor_trans_type = 13";
    $r = db_query($sql, 'Get delivery details');
    if ($r) {
        while ($dd = db_fetch($r)) {
            $sql2 = "SELECT id FROM " . TB_PREF . "sales_order_details
                     WHERE order_no = $soNo AND trans_type = 30 AND stk_code = '" . db_escape($dd['stock_id']) . "'
                     LIMIT 1";
            $r2 = db_query($sql2, 'Find SO detail');
            if ($r2 && ($sod = db_fetch($r2))) {
                $db->execute("UPDATE " . TB_PREF . "debtor_trans_details
                              SET src_id = " . (int)$sod['id'] . "
                              WHERE id = " . (int)$dd['id']);
            }
        }
    }

    _chain_recalc_all($db);

    $msg = sprintf(_('Linked delivery #%d to SO #%d.'), $delTransNo, $soNo);
    return array('message' => $msg, 'popup_url' => null);
}

/**
 * Add stock_moves entries for a delivery missing inventory movements.
 */
function _chain_do_sales_add_stock_moves($db, $delTransNo)
{
    $sql = "SELECT dtd.*, dt.tran_date, dt.debtor_no
            FROM " . TB_PREF . "debtor_trans_details dtd
            JOIN " . TB_PREF . "debtor_trans dt
                ON dt.trans_no = dtd.debtor_trans_no AND dt.type = dtd.debtor_trans_type
            WHERE dtd.debtor_trans_no = $delTransNo AND dtd.debtor_trans_type = 13
              AND NOT EXISTS (
                  SELECT 1 FROM " . TB_PREF . "stock_moves sm
                  WHERE sm.trans_no = dtd.debtor_trans_no
                    AND sm.type = dtd.debtor_trans_type
                AND sm.stock_id = dtd.stock_id
              )";
    $r = db_query($sql, 'Load delivery details for stock move creation');
    if (!$r) {
        return array('message' => _('Delivery not found: ') . $delTransNo, 'popup_url' => null);
    }

    $count = 0;
    $locSql = "SELECT loc_code FROM " . TB_PREF . "locations LIMIT 1";
    $locR = db_query($locSql, 'Get default location');
    $locCode = ($locR && ($locRow = db_fetch($locR))) ? $locRow['loc_code'] : '';

    while ($dd = db_fetch($r)) {
        $qty = -(float)$dd['qty_done']; // negative for delivery

        $db->execute("INSERT INTO " . TB_PREF . "stock_moves
            (stock_id, type, trans_no, tran_date, debtor_no, loc_code, qty, price, standard_cost)
            VALUES ('" . db_escape($dd['stock_id']) . "', 13, $delTransNo,
                    '" . db_escape($dd['tran_date']) . "',
                    " . (int)$dd['debtor_no'] . ",
                    '" . db_escape($locCode) . "',
                    $qty, " . (float)$dd['unit_price'] . ", 0)");
        $count++;
    }

    $msg = sprintf(_('Created %d stock move(s) for delivery #%d.'), $count, $delTransNo);
    return array('message' => $msg, 'popup_url' => null);
}

// ===========================================================================
// EXISTING BULK FIX FUNCTIONS (P5/P6/P7/S3/S4/S5) &#8212; kept for backward compat
// but now delegates to the consolidated chain logic.
// ===========================================================================

function fix_purchase_orphaned_invoice_items()
{
    $rows = check_purchase_chain();
    $fixed = 0;
    foreach ($rows as $row) {
        if ($row['row_type'] === 'orphan_inv' || $row['row_type'] === 'item_mismatch') {
            $result = fix_purchase_chain_row($row['row_key'], 'add_grn');
            if (strpos($result['message'], 'Created GRN') !== false) {
                $fixed++;
            }
        }
    }
    return $fixed;
}

function fix_purchase_orphaned_grn_batches()
{
    $sql = "SELECT gb.id AS grn_batch_id
            FROM " . TB_PREF . "grn_batch gb
            LEFT JOIN " . TB_PREF . "purch_orders po ON po.order_no = gb.purch_order_no
            WHERE gb.purch_order_no IS NOT NULL AND gb.purch_order_no > 0 AND po.order_no IS NULL";
    $r = db_query($sql, 'Legacy P6: find orphaned GRN batches');
    $fixed = 0;
    if ($r) {
        while ($row = db_fetch($r)) {
            $result = fix_purchase_chain_row('grn_' . $row['grn_batch_id'], 'add_po');
            if (strpos($result['message'], 'Created PO') !== false) {
                $fixed++;
            }
        }
    }
    return $fixed;
}

function fix_purchase_ghost_invoices()
{
    $rows = check_purchase_chain();
    $fixed = 0;
    foreach ($rows as $row) {
        if ($row['row_type'] === 'ghost_inv') {
            fix_purchase_chain_row($row['row_key'], 'void_ghost');
            $fixed++;
        }
    }
    return $fixed;
}

function fix_sales_orphaned_deliveries()
{
    $rows = check_sales_chain();
    $fixed = 0;
    foreach ($rows as $row) {
        if ($row['row_type'] === 'orphan_del') {
            $result = fix_sales_chain_row($row['row_key'], 'add_so');
            if (strpos($result['message'], 'Created SO') !== false) {
                $fixed++;
            }
        }
    }
    return $fixed;
}

function fix_sales_delivery_missing_stock_moves()
{
    $rows = check_sales_chain();
    $fixed = 0;
    foreach ($rows as $row) {
        if ($row['row_type'] === 'missing_stock') {
            fix_sales_chain_row($row['row_key'], 'add_stock_moves');
            $fixed++;
        }
    }
    return $fixed;
}

function fix_sales_ghost_invoices()
{
    $rows = check_sales_chain();
    $fixed = 0;
    foreach ($rows as $row) {
        if ($row['row_type'] === 'ghost_inv') {
            fix_sales_chain_row($row['row_key'], 'void_ghost');
            $fixed++;
        }
    }
    return $fixed;
}

// ===========================================================================
// POPUP URL HELPERS
// ===========================================================================

/**
 * Return FA popup URL for viewing a supplier invoice.
 */
function _integ_invoice_popup_url($invTransNo)
{
    global $path_to_root;
    return $path_to_root . '/purchasing/supplier_invoice.php?ModifyInvoice=' . (int)$invTransNo;
}

/**
 * Load invoice item to find its trans_no, then return popup URL.
 */
function _integ_invoice_popup_url_for_item($invoiceItemId)
{
    $sql = "SELECT supp_trans_no FROM " . TB_PREF . "supp_invoice_items WHERE id = " . (int)$invoiceItemId;
    $r = db_query($sql, 'Get invoice trans for popup');
    $transNo = ($r && ($row = db_fetch($r))) ? (int)$row['supp_trans_no'] : 0;
    return $transNo ? _integ_invoice_popup_url($transNo) : null;
}

// ===========================================================================
// RECORD-CREATION HELPERS
// ===========================================================================

/**
 * Create a purchase order header + one detail line.
 *
 * @return array  ['order_no'=>int, 'po_detail_item'=>int]
 */
function _integ_create_po($db, $supplierId, $date, $ref, $itemCode, $description, $qty, $price)
{
    $poRepo = new \FrontAccounting\Repository\PurchaseOrderRepository($db);
    $poNo = $poRepo->insert(array(
        'supplier_id' => (int)$supplierId,
        'ord_date' => $date,
        'reference' => $ref,
        'delivery_address' => '',
        'currency' => '',
        'alloc_type' => 0,
        'requisition_no' => 0,
        'comments' => '',
        'tax_included' => 0,
        'prep_amount' => 0,
        'floor_stock' => 0,
        'supplier_ref' => '',
    ));

    // Create PO detail line and capture the auto-increment po_detail_item
    $podRepo = new \FrontAccounting\Repository\PurchaseOrderDetailRepository($db);
    $podItem = $podRepo->insert(array(
        'order_no' => $poNo,
        'item_code' => $itemCode,
        'description' => $description,
        'quantity_ordered' => (float)$qty,
        'unit_price' => (float)$price,
        'quantity_received' => 0,
        'qty_invoiced' => 0,
        'standard_cost' => 0,
        'line_no' => 1,
        'completed' => 0,
        'tax_free' => 0,
    ));

    return array('order_no' => $poNo, 'po_detail_item' => $podItem);
}

/**
 * Create a GRN batch (used by both P5 and P6 helpers).
 *
 * @return int  New grn_batch.id
 */
function _integ_create_grn($db, $poNo, $supplierId, $date, $ref)
{
    $repo = new \FrontAccounting\Repository\GrnBatchRepository($db);
    return $repo->insert(array(
        'purch_order_no' => (int)$poNo,
        'supplier_id' => (int)$supplierId,
        'delivery_date' => $date,
        'reference' => $ref,
    ));
}

/**
 * Create a GRN item row.
 *
 * @return int  New grn_items.id
 */
function _integ_create_grn_item($db, $grnBatchId, $poDetailItem, $itemCode, $description, $qty, $price)
{
    $repo = new \FrontAccounting\Repository\GrnItemsRepository($db);
    return $repo->insert(array(
        'grn_batch_id' => (int)$grnBatchId,
        'po_detail_item' => (int)$poDetailItem,
        'item_code' => $itemCode,
        'description' => $description,
        'qty_recd' => (float)$qty,
        'quantity_inv' => 0,
        'unit_cost' => (float)$price,
    ));
}

/**
 * Create a sales order header + one detail line.
 *
 * @return int  New sales_orders.order_no
 */
function _integ_create_so($db, $debtorNo, $date, $ref, $itemCode, $description, $qty, $price)
{
    $soRepo = new \FrontAccounting\Repository\SalesOrderRepository($db);
    $soNo = $soRepo->insert(array(
        'debtor_no' => (int)$debtorNo,
        'trans_type' => 30,
        'order_no' => 0,     // auto-increment
        'reference' => $ref,
        'ord_date' => $date,
        'customer_ref' => '',
        'delivery_date' => $date,
        'delivery_address' => '',
        'comments' => '',
        'sales_type' => 0,
        'dimension_id' => 0,
        'dimension2_id' => 0,
        'payment_terms' => 0,
        'prep_amount' => 0,
        'alloc' => 0,
        'freight_cost' => 0,
        'tax_included' => 0,
        'additional_discount' => 0,
        'from_stk' => '',
    ));

    // Create SO detail line &#8212; need to figure out the id since it's referenced by delivery notes
    $sodRepo = new \FrontAccounting\Repository\SalesOrderDetailRepository($db);
    $sodId = $sodRepo->insert(array(
        'order_no' => $soNo,
        'trans_type' => 30,
        'stk_code' => $itemCode,
        'description' => $description,
        'quantity' => (float)$qty,
        'unit_price' => (float)$price,
        'discount_percent' => 0,
        'qty_sent' => 0,
        'invoiced' => 0,
        'standard_cost' => 0,
        'src_id' => 0,
    ));

    return $soNo;
}
