<?php
/**
 * integrity_db.inc — Data integrity check queries for KSF FA DataIntegrity
 *
 * Each public function runs a SQL check and returns a db result set (or
 * an integer count when $count_only=true).  All queries use FA's standard
 * &TB_PREF& placeholder and db_query() / db_fetch() layer so they work
 * across both MySQL and MariaDB and respect the multi-company table prefix.
 *
 * Naming convention:
 *   check_<area>_<issue>()   — returns result set of offending rows
 *   fix_<area>_<issue>()     — applies a recalculation; returns rows affected
 *   count_<area>_issues()    — returns integer count for dashboard summary
 *
 * PHP 5.6+ compatible (no short closures, no null-coalescing, no typed props).
 *
 * @package  Ksfraser\FA\DataIntegrity
 * @since    1.0.0
 */

// Load Composer autoloader (provides FrontAccounting\Repository\* classes)
$integrityAutoload = __DIR__ . '/../vendor/autoload.php';
if (file_exists($integrityAutoload)) {
    require_once $integrityAutoload;
}

// Load reconstruction fix functions (P5/P6/S4/S5 etc.)
$reconFile = __DIR__ . '/integrity_reconstruction.inc';
if (file_exists($reconFile)) {
    require_once $reconFile;
}

// Floating-point comparison tolerance
define('INTEG_DELTA', 0.005);

/**
 * Helper: create a FrontAccountingDbAdapter for the current company.
 * @return \Ksfraser\ModulesDAO\Db\FrontAccountingDbAdapter
 */
function integ_db_adapter()
{
    return new \Ksfraser\ModulesDAO\Db\FrontAccountingDbAdapter(
        defined('TB_PREF') ? TB_PREF : ''
    );
}

// ===========================================================================
// PURCHASE CHAIN CHECKS
// ===========================================================================

/**
 * CHECK P1 — grn_items.quantity_inv does not match the sum of
 * supp_invoice_items.quantity for all non-voided invoices referencing that
 * GRN line.
 *
 * This is the direct cause of the "Direct Invoice created GRN but not the
 * invoice" symptom: quantity_inv stays 0, so the line keeps reappearing on
 * the supplier invoice entry screen.
 *
 * @return resource  DB result set
 */
function check_purchase_grn_qty_inv_mismatch()
{
    $sql = "
        SELECT
            g.id                            AS grn_item_id,
            gb.id                           AS grn_batch_id,
            gb.delivery_date,
            gb.reference                    AS grn_ref,
            gb.purch_order_no,
            s.supplier_id,
            s.supp_name,
            g.item_code,
            g.description,
            g.qty_recd,
            g.quantity_inv                  AS qty_inv_stored,
            COALESCE(inv_sum.qty_invoiced, 0) AS qty_inv_actual,
            g.quantity_inv - COALESCE(inv_sum.qty_invoiced, 0) AS discrepancy
        FROM &TB_PREF&grn_items g
        JOIN &TB_PREF&grn_batch gb   ON g.grn_batch_id = gb.id
        JOIN &TB_PREF&suppliers s    ON gb.supplier_id  = s.supplier_id
        LEFT JOIN (
            SELECT si.grn_item_id, SUM(si.quantity) AS qty_invoiced
            FROM &TB_PREF&supp_invoice_items si
            JOIN &TB_PREF&supp_trans st
                ON st.trans_no = si.supp_trans_no
               AND st.type     = si.supp_trans_type
            WHERE si.supp_trans_type = 20
              AND st.ov_amount != 0
            GROUP BY si.grn_item_id
        ) inv_sum ON inv_sum.grn_item_id = g.id
        WHERE ABS(g.quantity_inv - COALESCE(inv_sum.qty_invoiced, 0)) > " . INTEG_DELTA . "
        ORDER BY s.supp_name, gb.delivery_date, gb.id, g.id";

    return db_query($sql, 'Check P1: GRN quantity_inv vs invoice items');
}

/**
 * FIX P1 — Recalculate grn_items.quantity_inv from actual supp_invoice_items.
 *
 * Safe: only updates rows where a discrepancy exists.  Does NOT touch
 * supp_trans or purch_order_details — run fix_purchase_pod_qty_invoiced()
 * after this to keep purch_order_details in sync.
 *
 * @return int  Number of rows updated
 */
function fix_purchase_grn_qty_inv()
{
    $repo = new \FrontAccounting\Repository\GrnItemsRepository(integ_db_adapter());
    return $repo->recalcQtyInv();
}

// ---------------------------------------------------------------------------

/**
 * CHECK P2 — purch_order_details.qty_invoiced does not match the sum of
 * supp_invoice_items.quantity for all non-voided invoices on that PO line.
 *
 * Note: update_po() in po_db.inc resets qty_invoiced to 0 on PO edit —
 * this check surfaces every PO line that suffered that reset.
 *
 * @return resource
 */
function check_purchase_pod_qty_invoiced_mismatch()
{
    $sql = "
        SELECT
            pod.po_detail_item,
            pod.order_no,
            po.reference                     AS po_ref,
            s.supplier_id,
            s.supp_name,
            pod.item_code,
            pod.quantity_ordered,
            pod.quantity_received,
            pod.qty_invoiced                 AS qty_inv_stored,
            COALESCE(inv_sum.qty_invoiced, 0) AS qty_inv_actual,
            pod.qty_invoiced - COALESCE(inv_sum.qty_invoiced, 0) AS discrepancy
        FROM &TB_PREF&purch_order_details pod
        JOIN &TB_PREF&purch_orders po  ON pod.order_no = po.order_no
        JOIN &TB_PREF&suppliers s      ON po.supplier_id = s.supplier_id
        LEFT JOIN (
            SELECT si.po_detail_item_id, SUM(si.quantity) AS qty_invoiced
            FROM &TB_PREF&supp_invoice_items si
            JOIN &TB_PREF&supp_trans st
                ON st.trans_no = si.supp_trans_no
               AND st.type     = si.supp_trans_type
            WHERE si.supp_trans_type = 20
              AND st.ov_amount != 0
            GROUP BY si.po_detail_item_id
        ) inv_sum ON inv_sum.po_detail_item_id = pod.po_detail_item
        WHERE ABS(pod.qty_invoiced - COALESCE(inv_sum.qty_invoiced, 0)) > " . INTEG_DELTA . "
        ORDER BY s.supp_name, po.reference, pod.po_detail_item";

    return db_query($sql, 'Check P2: PO line qty_invoiced vs invoice items');
}

/**
 * FIX P2 — Recalculate purch_order_details.qty_invoiced from actual invoices.
 *
 * @return int
 */
function fix_purchase_pod_qty_invoiced()
{
    $repo = new \FrontAccounting\Repository\PurchOrderDetailsRepository(integ_db_adapter());
    return $repo->recalcQtyInvoiced();
}

// ---------------------------------------------------------------------------

/**
 * CHECK P3 — purch_order_details.quantity_received does not match the sum
 * of grn_items.qty_recd for that PO line across all GRN batches.
 *
 * @return resource
 */
function check_purchase_pod_qty_received_mismatch()
{
    $sql = "
        SELECT
            pod.po_detail_item,
            pod.order_no,
            po.reference                      AS po_ref,
            s.supplier_id,
            s.supp_name,
            pod.item_code,
            pod.quantity_ordered,
            pod.quantity_received             AS qty_recv_stored,
            COALESCE(grn_sum.qty_recd, 0)     AS qty_recv_actual,
            pod.quantity_received - COALESCE(grn_sum.qty_recd, 0) AS discrepancy
        FROM &TB_PREF&purch_order_details pod
        JOIN &TB_PREF&purch_orders po  ON pod.order_no = po.order_no
        JOIN &TB_PREF&suppliers s      ON po.supplier_id = s.supplier_id
        LEFT JOIN (
            SELECT po_detail_item, SUM(qty_recd) AS qty_recd
            FROM &TB_PREF&grn_items
            GROUP BY po_detail_item
        ) grn_sum ON grn_sum.po_detail_item = pod.po_detail_item
        WHERE ABS(pod.quantity_received - COALESCE(grn_sum.qty_recd, 0)) > " . INTEG_DELTA . "
        ORDER BY s.supp_name, po.reference";

    return db_query($sql, 'Check P3: PO line quantity_received vs GRN items');
}

/**
 * FIX P3 — Recalculate purch_order_details.quantity_received from GRN items.
 *
 * @return int
 */
function fix_purchase_pod_qty_received()
{
    $repo = new \FrontAccounting\Repository\PurchOrderDetailsRepository(integ_db_adapter());
    return $repo->recalcQtyReceived();
}

// ---------------------------------------------------------------------------

/**
 * CHECK P4 — grn_items.quantity_inv exceeds qty_recd (over-invoiced GRN line).
 * These cannot be auto-fixed — a credit note or manual void is required.
 *
 * @return resource
 */
function check_purchase_grn_over_invoiced()
{
    $sql = "
        SELECT
            g.id            AS grn_item_id,
            gb.id           AS grn_batch_id,
            gb.delivery_date,
            gb.reference    AS grn_ref,
            gb.purch_order_no,
            s.supplier_id,
            s.supp_name,
            g.item_code,
            g.description,
            g.qty_recd,
            g.quantity_inv                      AS qty_invoiced_stored,
            COALESCE(inv_sum.inv_qty, 0)        AS qty_invoiced_actual,
            COALESCE(cn_sum.cn_qty, 0)          AS qty_credited,
            -- cn_qty is already negative (FA negates it for ST_SUPPCREDIT),
            -- so net = inv_qty + cn_qty (e.g. 1 + (-1) = 0)
            COALESCE(inv_sum.inv_qty, 0)
                + COALESCE(cn_sum.cn_qty, 0)    AS net_invoiced,
            COALESCE(inv_sum.inv_qty, 0)
                + COALESCE(cn_sum.cn_qty, 0)
                - g.qty_recd                    AS over_by
        FROM &TB_PREF&grn_items g
        JOIN &TB_PREF&grn_batch gb  ON g.grn_batch_id = gb.id
        JOIN &TB_PREF&suppliers s   ON gb.supplier_id  = s.supplier_id
        LEFT JOIN (
            SELECT grn_item_id, SUM(quantity) AS inv_qty
            FROM &TB_PREF&supp_invoice_items
            WHERE supp_trans_type = 20
            GROUP BY grn_item_id
        ) inv_sum ON inv_sum.grn_item_id = g.id
        LEFT JOIN (
            SELECT grn_item_id, SUM(quantity) AS cn_qty
            FROM &TB_PREF&supp_invoice_items
            WHERE supp_trans_type = 21
            GROUP BY grn_item_id
        ) cn_sum ON cn_sum.grn_item_id = g.id
        WHERE COALESCE(inv_sum.inv_qty, 0) + COALESCE(cn_sum.cn_qty, 0) > g.qty_recd + " . INTEG_DELTA . "
        ORDER BY s.supp_name, gb.delivery_date";

    return db_query($sql, 'Check P4: Over-invoiced GRN lines (net of credit notes)');
}

// ---------------------------------------------------------------------------

/**
 * CHECK P5 — supp_invoice_items rows where the referenced grn_item_id no
 * longer exists in grn_items (orphaned invoice line).
 *
 * @return resource
 */
function check_purchase_orphaned_invoice_items()
{
    $sql = "
        SELECT
            si.id               AS invoice_item_id,
            si.supp_trans_no,
            si.supp_trans_type,
            si.grn_item_id,
            si.po_detail_item_id,
            si.stock_id,
            si.description,
            si.quantity,
            si.unit_price,
            st.tran_date,
            st.supp_reference,
            s.supplier_id,
            s.supp_name
        FROM &TB_PREF&supp_invoice_items si
        JOIN &TB_PREF&supp_trans st
            ON st.trans_no = si.supp_trans_no
           AND st.type     = si.supp_trans_type
        JOIN &TB_PREF&suppliers s ON st.supplier_id = s.supplier_id
        LEFT JOIN &TB_PREF&grn_items g ON g.id = si.grn_item_id
        WHERE si.grn_item_id IS NOT NULL
          AND si.grn_item_id != 0
          AND g.id IS NULL
          AND st.ov_amount != 0
        ORDER BY s.supp_name, st.tran_date";

    return db_query($sql, 'Check P5: Orphaned supp_invoice_items');
}

// ---------------------------------------------------------------------------

/**
 * CHECK P6 — grn_batch rows whose parent purch_order_no no longer exists.
 *
 * @return resource
 */
function check_purchase_orphaned_grn_batches()
{
    $sql = "
        SELECT
            gb.id           AS grn_batch_id,
            gb.delivery_date,
            gb.reference    AS grn_ref,
            gb.purch_order_no,
            s.supplier_id,
            s.supp_name,
            COUNT(gi.id)    AS line_count,
            SUM(gi.qty_recd) AS total_qty_recd
        FROM &TB_PREF&grn_batch gb
        JOIN &TB_PREF&suppliers s   ON gb.supplier_id = s.supplier_id
        LEFT JOIN &TB_PREF&purch_orders po ON po.order_no = gb.purch_order_no
        LEFT JOIN &TB_PREF&grn_items gi    ON gi.grn_batch_id = gb.id
        WHERE gb.purch_order_no IS NOT NULL
          AND po.order_no IS NULL
        GROUP BY gb.id, gb.delivery_date, gb.reference, gb.purch_order_no,
                 s.supplier_id, s.supp_name
        ORDER BY s.supp_name, gb.delivery_date";

    return db_query($sql, 'Check P6: Orphaned GRN batches (missing PO)');
}

// ---------------------------------------------------------------------------

/**
 * CHECK P7 — Supplier invoices (supp_trans type 20) that have no
 * supp_invoice_items lines.  These are header-only / ghost invoices.
 *
 * @return resource
 */
function check_purchase_ghost_invoices()
{
    $sql = "
        SELECT
            st.trans_no,
            st.type,
            st.tran_date,
            st.supp_reference,
            st.reference,
            st.ov_amount,
            st.ov_gst,
            st.ov_discount,
            st.ov_amount + st.ov_gst + st.ov_discount AS total,
            st.alloc,
            s.supplier_id,
            s.supp_name
        FROM &TB_PREF&supp_trans st
        JOIN &TB_PREF&suppliers s ON st.supplier_id = s.supplier_id
        LEFT JOIN &TB_PREF&supp_invoice_items si
            ON si.supp_trans_no   = st.trans_no
           AND si.supp_trans_type = st.type
        WHERE st.type = 20
          AND st.ov_amount != 0
          AND si.id IS NULL
        ORDER BY s.supp_name, st.tran_date";

    return db_query($sql, 'Check P7: Supplier invoices with no line items');
}

/**
 * CHECK P8 — Purchase order detail lines not yet received.
 * Shows healthy POs still awaiting delivery (partial or full).
 *
 * @return resource
 * @since  2.5.0
 */
function check_purchase_po_not_received()
{
    $sql = "
        SELECT
            pod.order_no              AS po_no,
            po.reference              AS po_ref,
            po.ord_date,
            s.supplier_id,
            s.supp_name,
            pod.po_detail_item,
            pod.item_code,
            pod.description,
            pod.quantity_ordered,
            pod.quantity_received,
            pod.quantity_ordered - pod.quantity_received AS qty_outstanding,
            pod.unit_price,
            pod.qty_invoiced
        FROM &TB_PREF&purch_order_details pod
        JOIN &TB_PREF&purch_orders po        ON po.order_no = pod.order_no
        JOIN &TB_PREF&suppliers s            ON s.supplier_id = po.supplier_id
        WHERE pod.quantity_ordered > pod.quantity_received + " . INTEG_DELTA . "
        ORDER BY s.supp_name, po.order_no";

    return db_query($sql, 'Check P8: Purchase orders not received');
}

/**
 * CHECK P9 — GRN items not yet fully invoiced.
 * Shows GRN items where received quantity exceeds invoiced quantity.
 *
 * @return resource
 * @since  2.5.0
 */
function check_purchase_grn_not_invoiced()
{
    $sql = "
        SELECT
            g.id                AS grn_item_id,
            gb.id               AS grn_batch_id,
            gb.delivery_date,
            gb.reference        AS grn_ref,
            gb.purch_order_no,
            s.supplier_id,
            s.supp_name,
            g.item_code,
            g.description,
            g.qty_recd,
            g.quantity_inv,
            g.qty_recd - g.quantity_inv AS uninvoiced_qty
        FROM &TB_PREF&grn_batch gb
        JOIN &TB_PREF&grn_items g    ON g.grn_batch_id = gb.id
        JOIN &TB_PREF&suppliers s    ON s.supplier_id = gb.supplier_id
        WHERE g.qty_recd > g.quantity_inv + " . INTEG_DELTA . "
        ORDER BY s.supp_name, gb.delivery_date";

    return db_query($sql, 'Check P9: GRN items not invoiced');
}

/**
 * CHECK P10 — Supplier invoices not yet fully paid or credited.
 * Shows invoices where alloc < total amount (unpaid balance remains).
 *
 * @return resource
 * @since  2.5.0
 */
function check_purchase_invoice_unpaid()
{
    $sql = "
        SELECT
            st.trans_no            AS inv_no,
            st.reference           AS inv_ref,
            st.supp_reference,
            st.tran_date,
            st.due_date,
            s.supplier_id,
            s.supp_name,
            st.ov_amount,
            st.ov_gst,
            st.ov_discount,
            st.alloc,
            st.ov_amount + st.ov_gst + st.ov_discount AS total,
            st.ov_amount + st.ov_gst + st.ov_discount - st.alloc AS unpaid
        FROM &TB_PREF&supp_trans st
        JOIN &TB_PREF&suppliers s ON s.supplier_id = st.supplier_id
        WHERE st.type = 20
          AND st.ov_amount != 0
          AND st.alloc < st.ov_amount + st.ov_gst + st.ov_discount - " . INTEG_DELTA . "
        ORDER BY s.supp_name, st.tran_date";

    return db_query($sql, 'Check P10: Supplier invoices unpaid');
}

// ===========================================================================
// SALES CHAIN CHECKS
// ===========================================================================

/**
 * CHECK S1 — sales_order_details.qty_sent does not match the sum of
 * debtor_trans_details.qty_done on delivery notes (type 13) linked back
 * to this order line via debtor_trans.order_.
 *
 * @return resource
 */
function check_sales_sod_qty_sent_mismatch()
{
    $sql = "
        SELECT
            sod.id              AS sod_id,
            sod.order_no,
            so.trans_type       AS so_type,
            so.reference        AS so_ref,
            dm.debtor_no,
            dm.name             AS customer_name,
            sod.stk_code,
            sod.description,
            sod.quantity        AS qty_ordered,
            sod.qty_sent        AS qty_sent_stored,
            COALESCE(del_sum.qty_sent, 0) AS qty_sent_actual,
            sod.qty_sent - COALESCE(del_sum.qty_sent, 0) AS discrepancy
        FROM &TB_PREF&sales_order_details sod
        JOIN &TB_PREF&sales_orders so
            ON so.order_no   = sod.order_no
           AND so.trans_type = sod.trans_type
        JOIN &TB_PREF&debtors_master dm ON dm.debtor_no = so.debtor_no
        LEFT JOIN (
            SELECT dtd.src_id, SUM(dtd.qty_done) AS qty_sent
            FROM &TB_PREF&debtor_trans_details dtd
            JOIN &TB_PREF&debtor_trans dt
                ON dt.trans_no = dtd.debtor_trans_no
               AND dt.type     = dtd.debtor_trans_type
            WHERE dtd.debtor_trans_type = 13
              AND dt.ov_amount != 0
            GROUP BY dtd.src_id
        ) del_sum ON del_sum.src_id = sod.id
        WHERE ABS(sod.qty_sent - COALESCE(del_sum.qty_sent, 0)) > " . INTEG_DELTA . "
          AND so.trans_type = 30
        ORDER BY dm.name, so.reference";

    return db_query($sql, 'Check S1: Sales order line qty_sent vs delivery notes');
}

/**
 * FIX S1 — Recalculate sales_order_details.qty_sent from delivery notes.
 *
 * @return int
 */
function fix_sales_sod_qty_sent()
{
    $repo = new \FrontAccounting\Repository\SalesOrderDetailsRepository(integ_db_adapter());
    return $repo->recalcQtySent();
}

// ---------------------------------------------------------------------------

/**
 * CHECK S2 — sales_order_details.invoiced does not match the sum of
 * debtor_trans_details.quantity on customer invoices (type 10) linked to
 * this order line.
 *
 * @return resource
 */
function check_sales_sod_invoiced_mismatch()
{
    $sql = "
        SELECT
            sod.id              AS sod_id,
            sod.order_no,
            so.trans_type       AS so_type,
            so.reference        AS so_ref,
            dm.debtor_no,
            dm.name             AS customer_name,
            sod.stk_code,
            sod.description,
            sod.quantity        AS qty_ordered,
            sod.invoiced        AS qty_inv_stored,
            COALESCE(inv_sum.qty_invoiced, 0) AS qty_inv_actual,
            sod.invoiced - COALESCE(inv_sum.qty_invoiced, 0) AS discrepancy
        FROM &TB_PREF&sales_order_details sod
        JOIN &TB_PREF&sales_orders so
            ON so.order_no   = sod.order_no
           AND so.trans_type = sod.trans_type
        JOIN &TB_PREF&debtors_master dm ON dm.debtor_no = so.debtor_no
        LEFT JOIN (
            SELECT dtd.src_id, SUM(dtd.quantity) AS qty_invoiced
            FROM &TB_PREF&debtor_trans_details dtd
            JOIN &TB_PREF&debtor_trans dt
                ON dt.trans_no = dtd.debtor_trans_no
               AND dt.type     = dtd.debtor_trans_type
            WHERE dtd.debtor_trans_type = 10
              AND dt.ov_amount != 0
            GROUP BY dtd.src_id
        ) inv_sum ON inv_sum.src_id = sod.id
        WHERE ABS(sod.invoiced - COALESCE(inv_sum.qty_invoiced, 0)) > " . INTEG_DELTA . "
          AND so.trans_type = 30
        ORDER BY dm.name, so.reference";

    return db_query($sql, 'Check S2: Sales order line invoiced qty vs invoice details');
}

/**
 * FIX S2 — Recalculate sales_order_details.invoiced from customer invoices.
 *
 * @return int
 */
function fix_sales_sod_invoiced()
{
    $repo = new \FrontAccounting\Repository\SalesOrderDetailsRepository(integ_db_adapter());
    return $repo->recalcInvoiced();
}

// ---------------------------------------------------------------------------

/**
 * CHECK S3 — Customer invoices (type 10) that have no debtor_trans_details
 * lines.  Ghost invoices that won't appear on any statement correctly.
 *
 * @return resource
 */
function check_sales_ghost_invoices()
{
    $sql = "
        SELECT
            dt.trans_no,
            dt.type,
            dt.tran_date,
            dt.reference,
            dt.ov_amount + dt.ov_gst + dt.ov_freight + dt.ov_freight_tax AS total,
            dt.alloc,
            dt.debtor_no,
            dm.name AS customer_name
        FROM &TB_PREF&debtor_trans dt
        JOIN &TB_PREF&debtors_master dm ON dm.debtor_no = dt.debtor_no
        LEFT JOIN &TB_PREF&debtor_trans_details dtd
            ON dtd.debtor_trans_no   = dt.trans_no
           AND dtd.debtor_trans_type = dt.type
        WHERE dt.type = 10
          AND dt.ov_amount != 0
          AND dtd.id IS NULL
        ORDER BY dm.name, dt.tran_date";

    return db_query($sql, 'Check S3: Customer invoices with no line items');
}

// ---------------------------------------------------------------------------

/**
 * CHECK S4 — Delivery notes (type 13) whose parent sales order (debtor_trans.order_)
 * no longer exists in sales_orders.
 *
 * @return resource
 */
function check_sales_orphaned_deliveries()
{
    $sql = "
        SELECT
            dt.trans_no,
            dt.type,
            dt.tran_date,
            dt.reference,
            dt.order_          AS sales_order_no,
            dt.debtor_no,
            dm.name            AS customer_name,
            dt.ov_amount + dt.ov_freight AS total
        FROM &TB_PREF&debtor_trans dt
        JOIN &TB_PREF&debtors_master dm ON dm.debtor_no = dt.debtor_no
        LEFT JOIN &TB_PREF&sales_orders so
            ON so.order_no   = dt.order_
           AND so.trans_type = 30
        WHERE dt.type   = 13
          AND dt.order_ > 0
          AND so.order_no IS NULL
          AND dt.ov_amount != 0
        ORDER BY dm.name, dt.tran_date";

    return db_query($sql, 'Check S4: Orphaned delivery notes (missing sales order)');
}

// ---------------------------------------------------------------------------

/**
 * CHECK S5 — Delivery notes with no matching stock_moves entries.
 * Indicates a partial-write where the FA transaction was created but
 * inventory was never decremented.
 *
 * @return resource
 */
function check_sales_delivery_missing_stock_moves()
{
    $sql = "
        SELECT
            dt.trans_no,
            dt.type,
            dt.tran_date,
            dt.reference,
            dt.debtor_no,
            dm.name     AS customer_name,
            dt.ov_amount + dt.ov_freight AS total
        FROM &TB_PREF&debtor_trans dt
        JOIN &TB_PREF&debtors_master dm ON dm.debtor_no = dt.debtor_no
        LEFT JOIN &TB_PREF&stock_moves sm
            ON sm.trans_no = dt.trans_no
           AND sm.type     = dt.type
        WHERE dt.type  = 13
          AND dt.ov_amount != 0
          AND sm.trans_id IS NULL
        ORDER BY dm.name, dt.tran_date";

    return db_query($sql, 'Check S5: Deliveries with no stock moves');
}

// ---------------------------------------------------------------------------

/**
 * CHECK S6 &mdash; Sales orders with lines not yet fully delivered.
 *
 * Shows healthy SOs still awaiting delivery (partial or full).
 * Supports optional filtering by customer debtors_master.debtor_no
 * and/or cust_branch.branch_code for CRM module reuse.
 *
 * @param  int|null $debtorNo   Optional customer debtor_no filter
 * @param  int|null $branchCode Optional branch code filter
 * @return resource
 * @since  2.5.0
 */
function check_sales_so_not_delivered($debtorNo = null, $branchCode = null)
{
    $where  = " AND so.trans_type = 30\n";
    $where .= $debtorNo   ? " AND so.debtor_no   = " . (int)$debtorNo   . "\n" : '';
    $where .= $branchCode ? " AND so.branch_code = " . (int)$branchCode . "\n" : '';

    $sql = "
        SELECT
            sod.order_no,
            so.reference        AS so_ref,
            so.ord_date,
            dm.debtor_no,
            dm.name             AS customer_name,
            sod.stk_code,
            sod.description,
            sod.quantity        AS qty_ordered,
            sod.qty_sent,
            sod.quantity - sod.qty_sent AS qty_outstanding,
            sod.unit_price
        FROM &TB_PREF&sales_order_details sod
        JOIN &TB_PREF&sales_orders so
            ON so.order_no   = sod.order_no
           AND so.trans_type = sod.trans_type
        JOIN &TB_PREF&debtors_master dm
            ON dm.debtor_no = so.debtor_no
        WHERE sod.quantity > sod.qty_sent + " . INTEG_DELTA . "
          $where
        ORDER BY dm.name, so.reference";

    return db_query($sql, 'Check S6: Sales orders not delivered');
}

/**
 * CHECK S7 &mdash; Delivery-note items not yet fully invoiced.
 *
 * Shows debtor_trans_details rows from delivery notes (type 13) where
 * the qty_done exceeds the total invoiced qty on the same sales-order
 * detail line (via src_id).  Supports optional customer/branch filter.
 *
 * @param  int|null $debtorNo   Optional customer debtor_no filter
 * @param  int|null $branchCode Optional branch code filter
 * @return resource
 * @since  2.5.0
 */
function check_sales_delivery_not_invoiced($debtorNo = null, $branchCode = null)
{
    $where  = " AND dtd.debtor_trans_type = 13\n";
    $where .= $debtorNo   ? " AND dt.debtor_no    = " . (int)$debtorNo   . "\n" : '';
    $where .= $branchCode ? " AND dt.branch_code  = " . (int)$branchCode . "\n" : '';

    $sql = "
        SELECT
            dt.trans_no           AS delivery_no,
            dt.reference          AS delivery_ref,
            dt.tran_date          AS delivery_date,
            dt.order_             AS sales_order_no,
            so.reference          AS so_ref,
            dt.debtor_no,
            dm.name               AS customer_name,
            dtd.stock_id,
            dtd.description,
            dtd.qty_done          AS qty_delivered,
            COALESCE(inv_sum.qty_invoiced, 0) AS qty_invoiced,
            dtd.qty_done - COALESCE(inv_sum.qty_invoiced, 0) AS uninvoiced_qty,
            dtd.unit_price
        FROM &TB_PREF&debtor_trans_details dtd
        JOIN &TB_PREF&debtor_trans dt
            ON dt.trans_no = dtd.debtor_trans_no
           AND dt.type     = dtd.debtor_trans_type
        JOIN &TB_PREF&debtors_master dm
            ON dm.debtor_no = dt.debtor_no
        LEFT JOIN &TB_PREF&sales_orders so
            ON so.order_no   = dt.order_
           AND so.trans_type = 30
        LEFT JOIN (
            SELECT src_id, SUM(quantity) AS qty_invoiced
            FROM &TB_PREF&debtor_trans_details
            WHERE debtor_trans_type = 10
            GROUP BY src_id
        ) inv_sum ON inv_sum.src_id = dtd.src_id
        WHERE dtd.debtor_trans_type = 13
          AND dt.ov_amount != 0
          AND dtd.qty_done > COALESCE(inv_sum.qty_invoiced, 0) + " . INTEG_DELTA . "
          $where
        ORDER BY dm.name, dt.tran_date";

    return db_query($sql, 'Check S7: Delivery items not invoiced');
}

/**
 * CHECK S8 &mdash; Customer invoices not yet fully paid or credited (returned).
 *
 * Shows debtor_trans type 10 invoices where alloc < total.
 * Supports optional customer/branch filter.
 *
 * @param  int|null $debtorNo   Optional customer debtor_no filter
 * @param  int|null $branchCode Optional branch code filter
 * @return resource
 * @since  2.5.0
 */
function check_sales_invoice_unpaid($debtorNo = null, $branchCode = null)
{
    $where  = " AND dt.type = 10\n";
    $where .= $debtorNo   ? " AND dt.debtor_no   = " . (int)$debtorNo   . "\n" : '';
    $where .= $branchCode ? " AND dt.branch_code = " . (int)$branchCode . "\n" : '';

    $sql = "
        SELECT
            dt.trans_no            AS inv_no,
            dt.reference,
            dt.tran_date,
            dt.due_date,
            dt.debtor_no,
            dm.name                AS customer_name,
            dt.ov_amount,
            dt.ov_gst,
            dt.ov_freight,
            dt.ov_freight_tax,
            dt.ov_discount,
            dt.alloc,
            dt.ov_amount + dt.ov_gst + dt.ov_freight
                + dt.ov_freight_tax + dt.ov_discount  AS total,
            dt.ov_amount + dt.ov_gst + dt.ov_freight
                + dt.ov_freight_tax + dt.ov_discount - dt.alloc AS unpaid
        FROM &TB_PREF&debtor_trans dt
        JOIN &TB_PREF&debtors_master dm
            ON dm.debtor_no = dt.debtor_no
        WHERE dt.type = 10
          AND dt.ov_amount != 0
          AND dt.alloc < dt.ov_amount + dt.ov_gst + dt.ov_freight
              + dt.ov_freight_tax + dt.ov_discount - " . INTEG_DELTA . "
          $where
        ORDER BY dm.name, dt.tran_date";

    return db_query($sql, 'Check S8: Customer invoices unpaid');
}

// ===========================================================================
// ALLOCATION CHECKS — both purchase and sales sides
// ===========================================================================

/**
 * CHECK A1 — supp_trans.alloc does not match the sum of supp_allocations.amt
 * for that transaction.
 *
 * @param  int|null $supplier_id  Optionally filter to one supplier
 * @return resource
 */
function check_alloc_supplier_drift($supplier_id = null)
{
    $where = $supplier_id ? " AND st.supplier_id = " . (int)$supplier_id : '';

    $sql = "
        SELECT
            st.trans_no,
            st.type,
            st.tran_date,
            st.supp_reference,
            st.reference,
            st.ov_amount + st.ov_gst + st.ov_discount   AS total_amount,
            st.alloc                                      AS alloc_stored,
            COALESCE(sa_sum.total_alloc, 0)              AS alloc_actual,
            st.alloc - COALESCE(sa_sum.total_alloc, 0)  AS discrepancy,
            s.supplier_id,
            s.supp_name
        FROM &TB_PREF&supp_trans st
        JOIN &TB_PREF&suppliers s ON st.supplier_id = s.supplier_id
        LEFT JOIN (
            SELECT person_id,
                   CASE
                       WHEN trans_type_from = st2.type AND trans_no_from = st2.trans_no
                           THEN 'from'
                       ELSE 'to'
                   END AS direction,
                   SUM(amt) AS total_alloc
            FROM &TB_PREF&supp_allocations sa2
            JOIN &TB_PREF&supp_trans st2
                ON (sa2.trans_type_from = st2.type AND sa2.trans_no_from = st2.trans_no
                    AND sa2.person_id = st2.supplier_id)
                OR (sa2.trans_type_to   = st2.type AND sa2.trans_no_to   = st2.trans_no
                    AND sa2.person_id = st2.supplier_id)
            GROUP BY sa2.person_id, st2.trans_no, st2.type
        ) sa_sum ON sa_sum.person_id = st.supplier_id
        WHERE st.ov_amount != 0
          AND ABS(st.alloc - COALESCE(sa_sum.total_alloc, 0)) > " . INTEG_DELTA . "
          $where
        ORDER BY s.supp_name, st.tran_date";

    // Simpler and more reliable sub-query approach:
    $sql = "
        SELECT
            st.trans_no,
            st.type,
            st.tran_date,
            st.supp_reference,
            st.reference,
            ROUND(st.ov_amount + st.ov_gst + st.ov_discount, 4) AS total_amount,
            st.alloc                                              AS alloc_stored,
            ROUND(COALESCE(sa_sum.total_alloc, 0), 4)           AS alloc_actual,
            ROUND(st.alloc - COALESCE(sa_sum.total_alloc, 0), 4) AS discrepancy,
            s.supplier_id,
            s.supp_name
        FROM &TB_PREF&supp_trans st
        JOIN &TB_PREF&suppliers s ON st.supplier_id = s.supplier_id
        LEFT JOIN (
            SELECT person_id, trans_no_to AS trans_no, trans_type_to AS type,
                   SUM(amt) AS total_alloc
            FROM &TB_PREF&supp_allocations
            GROUP BY person_id, trans_no_to, trans_type_to
        ) sa_sum ON sa_sum.person_id = st.supplier_id
               AND sa_sum.trans_no   = st.trans_no
               AND sa_sum.type       = st.type
        WHERE st.ov_amount != 0
          AND ABS(st.alloc - COALESCE(sa_sum.total_alloc, 0)) > " . INTEG_DELTA . "
          $where
        ORDER BY s.supp_name, st.tran_date";

    return db_query($sql, 'Check A1: Supplier alloc vs supp_allocations');
}

/**
 * FIX A1 — Recalculate supp_trans.alloc from supp_allocations table.
 *
 * @return int
 */
function fix_alloc_supplier_drift()
{
    $repo = new \FrontAccounting\Repository\AllocationRepository(integ_db_adapter());
    return $repo->recalcSupplierAlloc();
}

// ---------------------------------------------------------------------------

/**
 * CHECK A2 — debtor_trans.alloc does not match the sum of cust_allocations.amt
 * for that customer transaction.
 *
 * @param  int|null $debtor_no  Optionally filter to one customer
 * @return resource
 */
function check_alloc_customer_drift($debtor_no = null)
{
    $where = $debtor_no ? " AND dt.debtor_no = " . (int)$debtor_no : '';

    $sql = "
        SELECT
            dt.trans_no,
            dt.type,
            dt.tran_date,
            dt.reference,
            ROUND(dt.ov_amount + dt.ov_gst + dt.ov_freight + dt.ov_freight_tax + dt.ov_discount, 4) AS total_amount,
            dt.alloc                                               AS alloc_stored,
            ROUND(COALESCE(ca_sum.total_alloc, 0), 4)            AS alloc_actual,
            ROUND(dt.alloc - COALESCE(ca_sum.total_alloc, 0), 4) AS discrepancy,
            dt.debtor_no,
            dm.name AS customer_name
        FROM &TB_PREF&debtor_trans dt
        JOIN &TB_PREF&debtors_master dm ON dm.debtor_no = dt.debtor_no
        LEFT JOIN (
            SELECT person_id, trans_no_to AS trans_no, trans_type_to AS type,
                   SUM(amt) AS total_alloc
            FROM &TB_PREF&cust_allocations
            GROUP BY person_id, trans_no_to, trans_type_to
        ) ca_sum ON ca_sum.person_id = dt.debtor_no
               AND ca_sum.trans_no   = dt.trans_no
               AND ca_sum.type       = dt.type
        WHERE dt.ov_amount != 0
          AND ABS(dt.alloc - COALESCE(ca_sum.total_alloc, 0)) > " . INTEG_DELTA . "
          $where
        ORDER BY dm.name, dt.tran_date";

    return db_query($sql, 'Check A2: Customer alloc vs cust_allocations');
}

/**
 * FIX A2 — Recalculate debtor_trans.alloc from cust_allocations.
 *
 * @return int
 */
function fix_alloc_customer_drift()
{
    $repo = new \FrontAccounting\Repository\AllocationRepository(integ_db_adapter());
    return $repo->recalcCustomerAlloc();
}

// ---------------------------------------------------------------------------

/**
 * CHECK A3 — Transactions where alloc exceeds the absolute transaction amount
 * (over-allocated — mathematically impossible without a data error).
 *
 * @return resource
 */
function check_alloc_over_allocated()
{
    $sql = "
        (
            SELECT
                'supplier'                                               AS side,
                st.trans_no,
                st.type,
                st.tran_date,
                st.supp_reference                                        AS reference,
                ROUND(ABS(st.ov_amount + st.ov_gst + st.ov_discount), 4) AS total_amount,
                st.alloc,
                ROUND(st.alloc - ABS(st.ov_amount + st.ov_gst + st.ov_discount), 4) AS over_by,
                st.supplier_id                                           AS person_id,
                s.supp_name                                              AS person_name
            FROM &TB_PREF&supp_trans st
            JOIN &TB_PREF&suppliers s ON st.supplier_id = s.supplier_id
            WHERE st.ov_amount != 0
              AND st.alloc > ABS(st.ov_amount + st.ov_gst + st.ov_discount) + " . INTEG_DELTA . "
        )
        UNION ALL
        (
            SELECT
                'customer'                                                          AS side,
                dt.trans_no,
                dt.type,
                dt.tran_date,
                dt.reference,
                ROUND(ABS(dt.ov_amount + dt.ov_gst + dt.ov_freight + dt.ov_freight_tax + dt.ov_discount), 4) AS total_amount,
                dt.alloc,
                ROUND(dt.alloc - ABS(dt.ov_amount + dt.ov_gst + dt.ov_freight + dt.ov_freight_tax + dt.ov_discount), 4) AS over_by,
                dt.debtor_no                                                        AS person_id,
                dm.name                                                             AS person_name
            FROM &TB_PREF&debtor_trans dt
            JOIN &TB_PREF&debtors_master dm ON dm.debtor_no = dt.debtor_no
            WHERE dt.ov_amount != 0
              AND dt.alloc > ABS(dt.ov_amount + dt.ov_gst + dt.ov_freight + dt.ov_freight_tax + dt.ov_discount) + " . INTEG_DELTA . "
        )
        ORDER BY side, person_name, tran_date";

    return db_query($sql, 'Check A3: Over-allocated transactions');
}

// ---------------------------------------------------------------------------

/**
 * CHECK A4 — Orphaned supp_allocations rows where either the from-transaction
 * or to-transaction no longer exists in supp_trans.
 *
 * @return resource
 */
function check_alloc_orphaned_supplier_allocs()
{
    $sql = "
        SELECT
            sa.id,
            sa.person_id,
            s.supp_name,
            sa.date_alloc,
            sa.amt,
            sa.trans_type_from,
            sa.trans_no_from,
            sa.trans_type_to,
            sa.trans_no_to,
            CASE WHEN st_from.trans_no IS NULL THEN 'YES' ELSE 'no' END AS from_missing,
            CASE WHEN st_to.trans_no   IS NULL THEN 'YES' ELSE 'no' END AS to_missing
        FROM &TB_PREF&supp_allocations sa
        LEFT JOIN &TB_PREF&suppliers s
            ON s.supplier_id = sa.person_id
        LEFT JOIN &TB_PREF&supp_trans st_from
            ON st_from.trans_no = sa.trans_no_from
           AND st_from.type     = sa.trans_type_from
           AND st_from.supplier_id = sa.person_id
        LEFT JOIN &TB_PREF&supp_trans st_to
            ON st_to.trans_no = sa.trans_no_to
           AND st_to.type     = sa.trans_type_to
           AND st_to.supplier_id = sa.person_id
        WHERE st_from.trans_no IS NULL OR st_to.trans_no IS NULL
        ORDER BY s.supp_name, sa.date_alloc";

    return db_query($sql, 'Check A4: Orphaned supp_allocations');
}

// ---------------------------------------------------------------------------

/**
 * CHECK A5 — Orphaned cust_allocations rows where either the from-transaction
 * or to-transaction no longer exists in debtor_trans.
 *
 * @return resource
 */
function check_alloc_orphaned_customer_allocs()
{
    $sql = "
        SELECT
            ca.id,
            ca.person_id,
            dm.name             AS customer_name,
            ca.date_alloc,
            ca.amt,
            ca.trans_type_from,
            ca.trans_no_from,
            ca.trans_type_to,
            ca.trans_no_to,
            CASE WHEN dt_from.trans_no IS NULL THEN 'YES' ELSE 'no' END AS from_missing,
            CASE WHEN dt_to.trans_no   IS NULL THEN 'YES' ELSE 'no' END AS to_missing
        FROM &TB_PREF&cust_allocations ca
        LEFT JOIN &TB_PREF&debtors_master dm
            ON dm.debtor_no = ca.person_id
        LEFT JOIN &TB_PREF&debtor_trans dt_from
            ON dt_from.trans_no   = ca.trans_no_from
           AND dt_from.type       = ca.trans_type_from
           AND dt_from.debtor_no  = ca.person_id
        LEFT JOIN &TB_PREF&debtor_trans dt_to
            ON dt_to.trans_no  = ca.trans_no_to
           AND dt_to.type      = ca.trans_type_to
           AND dt_to.debtor_no = ca.person_id
        WHERE dt_from.trans_no IS NULL OR dt_to.trans_no IS NULL
        ORDER BY dm.name, ca.date_alloc";

    return db_query($sql, 'Check A5: Orphaned cust_allocations');
}

/**
 * CHECK A6 — Supplier payments with no allocation record (orphaned payments).
 *
 * Returns one row per unallocated payment with match_count indicating how
 * many unpaid invoices from the same supplier have a balance matching the
 * payment amount exactly (within INTEG_DELTA).
 *
 * @return resource
 */
function check_alloc_unallocated_supplier_payments()
{
    $d = INTEG_DELTA;
    $sql = "
        SELECT
            p.trans_no           AS payment_no,
            p.tran_date          AS payment_date,
            p.reference          AS payment_ref,
            p.supplier_id,
            s.supp_name,
            -p.ov_amount         AS amount,
            (SELECT COUNT(*)
             FROM &TB_PREF&supp_trans i2
             WHERE i2.supplier_id = p.supplier_id
               AND i2.type = 20
               AND i2.ov_amount - i2.alloc > $d
               AND ABS(ABS(p.ov_amount) - (i2.ov_amount - i2.alloc)) < $d
            ) AS match_count
        FROM &TB_PREF&supp_trans p
        JOIN &TB_PREF&suppliers s ON s.supplier_id = p.supplier_id
        WHERE p.type = 22
            AND p.ov_amount < 0
            AND NOT EXISTS (
                SELECT 1 FROM &TB_PREF&supp_allocations sa
                WHERE sa.trans_no_from = p.trans_no
                  AND sa.trans_type_from = 22
            )
        ORDER BY p.tran_date, s.supp_name";

    return db_query($sql, 'Check A6: Unallocated supplier payments');
}

/**
 * Quick-allocate ONE supplier payment against its single matching invoice.
 *
 * Called per-row from the A6 tab — never bulk.  Returns a status string
 * for the caller to display as a notification.
 *
 * @param  int    $payment_no  supp_trans.trans_no where type=22
 * @return string  'ok', 'not_found', 'no_match', 'multiple_matches', 'already_allocated'
 */
function quick_allocate_supplier_payment($payment_no)
{
    $payment_no = (int)$payment_no;
    $d = INTEG_DELTA;

    // — 1. Load the payment record —
    $sql = "SELECT supplier_id, ov_amount, alloc
            FROM &TB_PREF&supp_trans
            WHERE trans_no = $payment_no AND type = 22";
    $result = db_query($sql, 'QuickAlloc: load payment');
    if (!$result || !($pay = db_fetch($result))) {
        return 'not_found';
    }
    if ($pay['ov_amount'] >= 0) {
        return 'not_found'; // not a payment (negative = payment)
    }
    $amount    = -1 * (float)$pay['ov_amount'];
    $person_id = (int)$pay['supplier_id'];

    // — 2. Check it's not already allocated —
    $sql = "SELECT COUNT(*) AS cnt
            FROM &TB_PREF&supp_allocations
            WHERE trans_no_from = $payment_no AND trans_type_from = 22";
    $r2 = db_query($sql, 'QuickAlloc: check existing');
    $row2 = db_fetch($r2);
    if ($row2 && (int)$row2['cnt'] > 0) {
        return 'already_allocated';
    }

    // — 3. Find matching unpaid invoices from the same supplier —
    $sql = "SELECT trans_no, ov_amount, alloc,
                   ov_amount - alloc AS balance
            FROM &TB_PREF&supp_trans
            WHERE supplier_id = $person_id
              AND type = 20
              AND ov_amount - alloc > $d
              AND ABS($amount - (ov_amount - alloc)) < $d
            ORDER BY tran_date";
    $r3 = db_query($sql, 'QuickAlloc: find matching invoices');
    if (!$r3) {
        return 'no_match';
    }

    $invoices = array();
    while ($inv = db_fetch($r3)) {
        $invoices[] = $inv;
    }

    if (count($invoices) === 0) {
        return 'no_match';
    }
    if (count($invoices) > 1) {
        return 'multiple_matches';
    }

    // — 4. Exactly one match — create the allocation —
    $inv = $invoices[0];
    $inv_no = (int)$inv['trans_no'];
    $today  = date('Y-m-d');

    $dto  = new \FrontAccounting\DTO\SupplierAllocation($amount, 22, $payment_no, 20, $inv_no, $person_id, $today);
    $repo = new \FrontAccounting\Repository\AllocationRepository(integ_db_adapter());
    $repo->createSupplierAllocation($dto);
    $repo->updateSupplierTransactionAllocation(22, $payment_no, $person_id);
    $repo->updateSupplierTransactionAllocation(20, $inv_no, $person_id);

    return 'ok';
}

// ===========================================================================
// PIPELINE / FUNNEL ANALYSIS
// ===========================================================================

/**
 * Classify every purchase order by how far it has progressed through the
 * purchase chain (PO → GRN → Supplier Invoice → Payment) and whether any
 * counter values disagree.
 *
 * Categories (mutually exclusive, collectively exhaustive):
 *   open_po                     — PO exists, no GRN, no invoice, no payment
 *   received_not_invoiced       — GRN exists, no invoice (includes the
 *                                  Direct-Invoice-timeout bug scenario)
 *   invoiced_not_paid_clean     — invoice exists, no payment, counters OK
 *   invoiced_not_paid_corrupted — invoice exists, no payment, counter drift
 *   complete_clean              — invoice + payment exist, counters OK
 *   complete_corrupted          — invoice + payment exist, counter drift
 *   anomalous                   — payment exists but no invoice (data error)
 *   total                       — sum of all categories
 *
 * "Has GRN"     — at least one grn_batch row references this PO.
 * "Has invoice" — at least one non-voided supp_trans (type 20) is linked to
 *                 a purch_order_details line for this PO.
 * "Has payment" — at least one supp_allocations row references an invoice
 *                 on this PO (partially or fully paid).
 * "Has mismatch"— any P1/P2/P3-style counter drift detected on this PO.
 *
 * PHP 5.6 / MySQL 5.5 compatible (no CTEs — uses derived-table UNION).
 *
 * @return array  Associative array of integer counts keyed as above.
 * @since  1.0.0
 */
function get_purchase_pipeline_counts()
{
    $d = INTEG_DELTA;

    $sql = "
        SELECT
            COUNT(*)                                                                            AS total,
            SUM(CASE WHEN has_grn=0 AND has_invoice=0 AND has_payment=0 THEN 1 ELSE 0 END)     AS open_po,
            SUM(CASE WHEN has_grn=1 AND has_invoice=0 AND has_payment=0 THEN 1 ELSE 0 END)     AS received_not_invoiced,
            SUM(CASE WHEN has_invoice=1 AND has_payment=0 AND has_mismatch=0 THEN 1 ELSE 0 END) AS invoiced_not_paid_clean,
            SUM(CASE WHEN has_invoice=1 AND has_payment=0 AND has_mismatch=1 THEN 1 ELSE 0 END) AS invoiced_not_paid_corrupted,
            SUM(CASE WHEN has_invoice=1 AND has_payment=1 AND has_mismatch=0 THEN 1 ELSE 0 END) AS complete_clean,
            SUM(CASE WHEN has_invoice=1 AND has_payment=1 AND has_mismatch=1 THEN 1 ELSE 0 END) AS complete_corrupted,
            SUM(CASE WHEN has_payment=1 AND has_invoice=0              THEN 1 ELSE 0 END)       AS anomalous
        FROM (
            SELECT
                po.order_no,
                CASE WHEN grn_e.order_no  IS NOT NULL THEN 1 ELSE 0 END AS has_grn,
                CASE WHEN inv_e.order_no  IS NOT NULL THEN 1 ELSE 0 END AS has_invoice,
                CASE WHEN pay_e.order_no  IS NOT NULL THEN 1 ELSE 0 END AS has_payment,
                CASE WHEN bad_e.order_no  IS NOT NULL THEN 1 ELSE 0 END AS has_mismatch
            FROM &TB_PREF&purch_orders po

            /* ---- Stage 1: any GRN? ---- */
            LEFT JOIN (
                SELECT DISTINCT purch_order_no AS order_no
                FROM &TB_PREF&grn_batch
            ) grn_e ON grn_e.order_no = po.order_no

            /* ---- Stage 2: any non-voided invoice linked via pod? ---- */
            LEFT JOIN (
                SELECT DISTINCT pod.order_no
                FROM &TB_PREF&purch_order_details pod
                JOIN &TB_PREF&supp_invoice_items sii
                    ON sii.po_detail_item_id = pod.po_detail_item
                JOIN &TB_PREF&supp_trans st
                    ON st.trans_no = sii.supp_trans_no
                   AND st.type = 20
                   AND st.ov_amount != 0
            ) inv_e ON inv_e.order_no = po.order_no

            /* ---- Stage 3: any allocation against an invoice on this PO? ---- */
            LEFT JOIN (
                SELECT DISTINCT pod2.order_no
                FROM &TB_PREF&purch_order_details pod2
                JOIN &TB_PREF&supp_invoice_items sii2
                    ON sii2.po_detail_item_id = pod2.po_detail_item
                JOIN &TB_PREF&supp_trans inv2
                    ON inv2.trans_no = sii2.supp_trans_no
                   AND inv2.type = 20
                JOIN &TB_PREF&supp_allocations sa
                    ON sa.trans_no_to   = inv2.trans_no
                   AND sa.trans_type_to = 20
            ) pay_e ON pay_e.order_no = po.order_no

            /* ---- Counter mismatch: P1 OR P2 OR P3 drift on this PO ---- */
            LEFT JOIN (
                SELECT DISTINCT bad.order_no
                FROM (
                    /* P1-style: grn_items.quantity_inv drift */
                    SELECT gb.purch_order_no AS order_no
                    FROM &TB_PREF&grn_batch gb
                    JOIN &TB_PREF&grn_items gi ON gi.grn_batch_id = gb.id
                    LEFT JOIN (
                        SELECT sii3.grn_item_id, SUM(sii3.quantity) AS qty_inv
                        FROM &TB_PREF&supp_invoice_items sii3
                        JOIN &TB_PREF&supp_trans st3
                            ON st3.trans_no = sii3.supp_trans_no
                           AND st3.type = 20
                           AND st3.ov_amount != 0
                        GROUP BY sii3.grn_item_id
                    ) inv_g ON inv_g.grn_item_id = gi.id
                    WHERE ABS(gi.quantity_inv - COALESCE(inv_g.qty_inv, 0)) > $d

                    UNION

                    /* P2-style: purch_order_details.qty_invoiced drift */
                    SELECT pod3.order_no
                    FROM &TB_PREF&purch_order_details pod3
                    LEFT JOIN (
                        SELECT sii4.po_detail_item_id, SUM(sii4.quantity) AS qty_inv
                        FROM &TB_PREF&supp_invoice_items sii4
                        JOIN &TB_PREF&supp_trans st4
                            ON st4.trans_no = sii4.supp_trans_no
                           AND st4.type = 20
                           AND st4.ov_amount != 0
                        GROUP BY sii4.po_detail_item_id
                    ) inv_p ON inv_p.po_detail_item_id = pod3.po_detail_item
                    WHERE ABS(pod3.qty_invoiced - COALESCE(inv_p.qty_inv, 0)) > $d

                    UNION

                    /* P3-style: purch_order_details.quantity_received drift */
                    SELECT pod4.order_no
                    FROM &TB_PREF&purch_order_details pod4
                    LEFT JOIN (
                        SELECT gi2.po_detail_item, SUM(gi2.qty_recd) AS qty_recv
                        FROM &TB_PREF&grn_items gi2
                        GROUP BY gi2.po_detail_item
                    ) grn_s ON grn_s.po_detail_item = pod4.po_detail_item
                    WHERE ABS(pod4.quantity_received - COALESCE(grn_s.qty_recv, 0)) > $d
                ) bad
            ) bad_e ON bad_e.order_no = po.order_no
        ) pipeline";

    $result = db_query($sql, 'Get purchase pipeline counts');
    if (!$result) {
        return array(
            'total' => 0, 'open_po' => 0, 'received_not_invoiced' => 0,
            'invoiced_not_paid_clean' => 0, 'invoiced_not_paid_corrupted' => 0,
            'complete_clean' => 0, 'complete_corrupted' => 0, 'anomalous' => 0,
        );
    }
    $row    = db_fetch($result);
    db_free_result($result);

    if (!$row) {
        return array(
            'total' => 0, 'open_po' => 0, 'received_not_invoiced' => 0,
            'invoiced_not_paid_clean' => 0, 'invoiced_not_paid_corrupted' => 0,
            'complete_clean' => 0, 'complete_corrupted' => 0, 'anomalous' => 0,
        );
    }
    return $row;
}

// ---------------------------------------------------------------------------

/**
 * Classify every sales order by how far it has progressed through the
 * sales chain (SO → Delivery → Customer Invoice → Customer Payment) and
 * whether any counter values disagree.
 *
 * Categories mirror get_purchase_pipeline_counts() with:
 *   open_order              → SO, no delivery, no invoice, no payment
 *   delivered_not_invoiced  → delivery exists, no invoice
 *   (plus the same invoiced/complete/corrupted/anomalous buckets)
 *
 * "Has delivery" — any debtor_trans type 13 with order_ = so.order_no.
 * "Has invoice"  — any debtor_trans type 10 with order_ = so.order_no.
 * "Has payment"  — any cust_allocations row referencing an invoice on this SO.
 * "Has mismatch" — S1 (sod.qty_sent drift) or S2 (sod.invoiced drift).
 *
 * @return array
 * @since  1.0.0
 */
function get_sales_pipeline_counts()
{
    $d = INTEG_DELTA;

    $sql = "
        SELECT
            COUNT(*)                                                                             AS total,
            SUM(CASE WHEN has_del=0 AND has_invoice=0 AND has_payment=0 THEN 1 ELSE 0 END)      AS open_order,
            SUM(CASE WHEN has_del=1 AND has_invoice=0 AND has_payment=0 THEN 1 ELSE 0 END)      AS delivered_not_invoiced,
            SUM(CASE WHEN has_invoice=1 AND has_payment=0 AND has_mismatch=0 THEN 1 ELSE 0 END) AS invoiced_not_paid_clean,
            SUM(CASE WHEN has_invoice=1 AND has_payment=0 AND has_mismatch=1 THEN 1 ELSE 0 END) AS invoiced_not_paid_corrupted,
            SUM(CASE WHEN has_invoice=1 AND has_payment=1 AND has_mismatch=0 THEN 1 ELSE 0 END) AS complete_clean,
            SUM(CASE WHEN has_invoice=1 AND has_payment=1 AND has_mismatch=1 THEN 1 ELSE 0 END) AS complete_corrupted,
            SUM(CASE WHEN has_payment=1 AND has_invoice=0              THEN 1 ELSE 0 END)        AS anomalous
        FROM (
            SELECT
                so.order_no,
                CASE WHEN del_e.order_no IS NOT NULL THEN 1 ELSE 0 END AS has_del,
                CASE WHEN inv_e.order_no IS NOT NULL THEN 1 ELSE 0 END AS has_invoice,
                CASE WHEN pay_e.order_no IS NOT NULL THEN 1 ELSE 0 END AS has_payment,
                CASE WHEN bad_e.order_no IS NOT NULL THEN 1 ELSE 0 END AS has_mismatch
            FROM &TB_PREF&sales_orders so

            /* ---- Stage 1: any delivery note? ---- */
            LEFT JOIN (
                SELECT DISTINCT order_ AS order_no
                FROM &TB_PREF&debtor_trans
                WHERE type = 13
                  AND ov_amount != 0
            ) del_e ON del_e.order_no = so.order_no

            /* ---- Stage 2: any customer invoice? ---- */
            LEFT JOIN (
                SELECT DISTINCT order_ AS order_no
                FROM &TB_PREF&debtor_trans
                WHERE type = 10
                  AND ov_amount != 0
            ) inv_e ON inv_e.order_no = so.order_no

            /* ---- Stage 3: any allocation against a customer invoice? ---- */
            LEFT JOIN (
                SELECT DISTINCT dt.order_ AS order_no
                FROM &TB_PREF&debtor_trans dt
                JOIN &TB_PREF&cust_allocations ca
                    ON ca.trans_no_to   = dt.trans_no
                   AND ca.trans_type_to = 10
                WHERE dt.type = 10
                  AND dt.ov_amount != 0
            ) pay_e ON pay_e.order_no = so.order_no

            /* ---- Counter mismatch: S1 OR S2 drift on this SO ---- */
            LEFT JOIN (
                SELECT DISTINCT bad.order_no
                FROM (
                    /* S1-style: sales_order_details.qty_sent drift */
                    SELECT sod.order_no
                    FROM &TB_PREF&sales_order_details sod
                    JOIN &TB_PREF&sales_orders so2
                        ON so2.order_no   = sod.order_no
                       AND so2.trans_type = sod.trans_type
                    LEFT JOIN (
                        SELECT dtd.src_id, SUM(dtd.qty_done) AS qty_sent
                        FROM &TB_PREF&debtor_trans_details dtd
                        JOIN &TB_PREF&debtor_trans dt
                            ON dt.trans_no = dtd.debtor_trans_no
                           AND dt.type     = dtd.debtor_trans_type
                        WHERE dtd.debtor_trans_type = 13
                          AND dt.ov_amount != 0
                        GROUP BY dtd.src_id
                    ) del_s ON del_s.src_id = sod.id
                    WHERE so2.trans_type = 30
                      AND ABS(sod.qty_sent - COALESCE(del_s.qty_sent, 0)) > $d

                    UNION

                    /* S2-style: sales_order_details.invoiced drift */
                    SELECT sod2.order_no
                    FROM &TB_PREF&sales_order_details sod2
                    JOIN &TB_PREF&sales_orders so3
                        ON so3.order_no   = sod2.order_no
                       AND so3.trans_type = sod2.trans_type
                    LEFT JOIN (
                        SELECT dtd2.src_id, SUM(dtd2.quantity) AS qty_inv
                        FROM &TB_PREF&debtor_trans_details dtd2
                        JOIN &TB_PREF&debtor_trans dt2
                            ON dt2.trans_no = dtd2.debtor_trans_no
                           AND dt2.type     = dtd2.debtor_trans_type
                        WHERE dtd2.debtor_trans_type = 10
                          AND dt2.ov_amount != 0
                        GROUP BY dtd2.src_id
                    ) inv_s ON inv_s.src_id = sod2.id
                    WHERE so3.trans_type = 30
                      AND ABS(sod2.invoiced - COALESCE(inv_s.qty_inv, 0)) > $d
                ) bad
            ) bad_e ON bad_e.order_no = so.order_no
            WHERE so.trans_type = 30
        ) pipeline";

    $result = db_query($sql, 'Get sales pipeline counts');
    if (!$result) {
        return array(
            'total' => 0, 'open_order' => 0, 'delivered_not_invoiced' => 0,
            'invoiced_not_paid_clean' => 0, 'invoiced_not_paid_corrupted' => 0,
            'complete_clean' => 0, 'complete_corrupted' => 0, 'anomalous' => 0,
        );
    }
    $row    = db_fetch($result);
    db_free_result($result);

    if (!$row) {
        return array(
            'total' => 0, 'open_order' => 0, 'delivered_not_invoiced' => 0,
            'invoiced_not_paid_clean' => 0, 'invoiced_not_paid_corrupted' => 0,
            'complete_clean' => 0, 'complete_corrupted' => 0, 'anomalous' => 0,
        );
    }
    return $row;
}

// ===========================================================================
// DASHBOARD SUMMARY COUNTS
// ===========================================================================

/**
 * Run all checks and return an associative array of issue counts,
 * keyed by check ID.  Used by the dashboard page.
 *
 * @return array  array('P1' => int, 'P2' => int, ... )
 */
function count_all_integrity_issues()
{
    $counts = array();

    $checks = array(
        'P1' => 'check_purchase_grn_qty_inv_mismatch',
        'P2' => 'check_purchase_pod_qty_invoiced_mismatch',
        'P3' => 'check_purchase_pod_qty_received_mismatch',
        'P4' => 'check_purchase_grn_over_invoiced',
        'P5' => 'check_purchase_orphaned_invoice_items',
        'P6' => 'check_purchase_orphaned_grn_batches',
        'P7' => 'check_purchase_ghost_invoices',
        'P8' => 'check_purchase_po_not_received',
        'P9' => 'check_purchase_grn_not_invoiced',
        'P10' => 'check_purchase_invoice_unpaid',
        'PCHAIN' => 'check_purchase_chain',
        'S1' => 'check_sales_sod_qty_sent_mismatch',
        'S2' => 'check_sales_sod_invoiced_mismatch',
        'S3' => 'check_sales_ghost_invoices',
        'S4' => 'check_sales_orphaned_deliveries',
        'S5' => 'check_sales_delivery_missing_stock_moves',
        'S6' => 'check_sales_so_not_delivered',
        'S7' => 'check_sales_delivery_not_invoiced',
        'S8' => 'check_sales_invoice_unpaid',
        'SCHAIN' => 'check_sales_chain',
        'A1' => 'check_alloc_supplier_drift',
        'A2' => 'check_alloc_customer_drift',
        'A3' => 'check_alloc_over_allocated',
        'A4' => 'check_alloc_orphaned_supplier_allocs',
        'A5' => 'check_alloc_orphaned_customer_allocs',
        'A6' => 'check_alloc_unallocated_supplier_payments',
    );

    foreach ($checks as $id => $func) {
        $result = call_user_func($func);
        if ($result === null) {
            $counts[$id] = 0;
        } elseif (is_array($result)) {
            $counts[$id] = count($result);
        } else {
            $counts[$id] = db_num_rows($result);
            db_free_result($result);
        }
    }

    return $counts;
}

/**
 * Return human-readable label for each check ID.
 *
 * @return array
 */
function get_check_labels()
{
    return array(
        'P1' => _('GRN quantity_inv drift (direct invoice bug)'),
        'P2' => _('PO line qty_invoiced drift'),
        'P3' => _('PO line quantity_received drift'),
        'P4' => _('GRN lines over-invoiced (net of supplier credits)'),
        'P5' => _('Orphaned supplier invoice items (missing GRN)'),
        'P6' => _('Orphaned GRN batches (missing purchase order)'),
        'P7' => _('Ghost supplier invoices (no line items)'),
        'P8' => _('Purchase orders not received (outstanding)'),
        'P9' => _('GRN items not invoiced'),
        'P10' => _('Supplier invoices not paid or credited'),
        'S1' => _('Sales order line qty_sent drift'),
        'S2' => _('Sales order line invoiced qty drift'),
        'S3' => _('Ghost customer invoices (no line items)'),
        'S4' => _('Orphaned delivery notes (missing sales order)'),
        'S5' => _('Delivery notes with no stock moves'),
        'S6' => _('Sales orders not delivered (outstanding)'),
        'S7' => _('Delivery items not invoiced'),
        'S8' => _('Customer invoices not paid or credited'),
        'PCHAIN' => _('Purchase chain — broken links (replaces P5/P6/P7)'),
        'SCHAIN' => _('Sales chain — broken links (replaces S3/S4/S5)'),
        'A1' => _('Supplier alloc drift (alloc vs supp_allocations)'),
        'A2' => _('Customer alloc drift (alloc vs cust_allocations)'),
        'A3' => _('Over-allocated transactions'),
        'A4' => _('Orphaned supplier allocation records'),
        'A5' => _('Orphaned customer allocation records'),
        'A6' => _('Unallocated supplier payments (missing allocation)'),
    );
}

/**
 * Return auto-fix function name for checks that support it (null = manual only).
 *
 * @return array
 */
function get_fix_functions()
{
    return array(
        'P1' => 'fix_purchase_grn_qty_inv',
        'P2' => 'fix_purchase_pod_qty_invoiced',
        'P3' => 'fix_purchase_pod_qty_received',
        'P4' => 'fix_purchase_grn_over_invoiced',    // credit-note link (no auto-fix)
        'P5' => null,  // orphaned invoice items: manual per-row fix (see PCHAIN)
        'P6' => null,  // orphaned GRN batches: manual per-row fix (see PCHAIN)
        'P7' => null,  // ghost invoices: manual per-row fix (see PCHAIN)
        'P8' => null,  // view-only — no auto-fix (healthy in-progress chain)
        'P9' => null,  // view-only — no auto-fix (healthy in-progress chain)
        'P10' => null,  // view-only — no auto-fix (healthy in-progress chain)
        'PCHAIN' => null,  // per-row chain fix actions (no bulk)
        'S1' => 'fix_sales_sod_qty_sent',
        'S2' => 'fix_sales_sod_invoiced',
        'S3' => null,  // ghost invoices: manual per-row fix (see SCHAIN)
        'S4' => null,  // orphaned deliveries: manual per-row fix (see SCHAIN)
        'S5' => null,  // missing stock moves: manual per-row fix (see SCHAIN)
        'S6' => null,  // view-only — no auto-fix (healthy in-progress chain)
        'S7' => null,  // view-only — no auto-fix (healthy in-progress chain)
        'S8' => null,  // view-only — no auto-fix (healthy in-progress chain)
        'SCHAIN' => null, // per-row chain fix actions (no bulk)
        'A1' => 'fix_alloc_supplier_drift',
        'A2' => 'fix_alloc_customer_drift',
        'A3' => 'fix_alloc_over_allocated',
        'A4' => 'fix_alloc_orphaned_supplier_allocs',
        'A5' => 'fix_alloc_orphaned_customer_allocs',
        'A6' => null,   // per-row Quick Allocate UI (no bulk fix)
    );
}

/**
 * Return user-facing descriptions explaining what each fix function does.
 *
 * Shown in the UI below the check results and above the fix button so the
 * user understands the scope and side effects before clicking.
 *
 * @return array  check_id => string description
 */
function get_fix_descriptions()
{
    return array(
        'P1' => _('Recalculates grn_items.quantity_inv from the sum of supp_invoice_items.quantity for non-voided supplier invoices. Safe to run multiple times.'),
        'P2' => _('Recalculates purch_order_details.qty_invoiced from actual supplier invoice line quantities. Fixes the "PO line qty_invoiced reset to 0 on PO edit" bug.'),
        'P3' => _('Recalculates purch_order_details.quantity_received from GRN item quantities.'),
        'P4' => _('These GRN lines have been invoiced for more than was received. A supplier credit note is required. Use the per-row "Credit Note" link to open FA\'s credit note entry with the supplier pre-filled.'),
        'P5' => _('Supp invoice items whose grn_item_id references a GRN item that no longer exists. Use per-row fix actions on the PCHAIN tab.'),
        'P6' => _('GRN batches whose purch_order_no references a purchase order that no longer exists. Use per-row fix actions on the PCHAIN tab.'),
        'P7' => _('Supplier invoice headers with no line items (ghost invoices). Use per-row Void on the PCHAIN tab.'),
        'P8' => _('Purchase orders with line items still awaiting delivery. These are healthy in-progress chains — no fix needed.'),
        'P9' => _('Goods-received notes where the received quantity has not yet been fully invoiced. Healthy in-progress chains — no fix needed.'),
        'P10' => _('Supplier invoices with an outstanding unpaid balance (alloc < total). View the invoice in FA to record payment or allocate a credit note.'),
        'PCHAIN' => _('Consolidated purchase chain view (replaces P5/P6/P7). Each row shows PO → GRN → Invoice. Green cells are healthy. Orange cells have a candidate match — click Match to link. Yellow cells have an issue with an Add button. Red cells are orphaned with no candidate, or have item mismatches. Ghost invoices show a Void button.'),
        'S1' => _('Recalculates sales_order_details.qty_sent from delivery note detail quantities.'),
        'S2' => _('Recalculates sales_order_details.invoiced from customer invoice detail quantities.'),
        'S3' => _('Customer invoice headers with no line items (ghost invoices). Use per-row Void on the SCHAIN tab.'),
        'S4' => _('Delivery notes whose order_ references a sales order that no longer exists. Use per-row fix actions on the SCHAIN tab.'),
        'S5' => _('Delivery notes with no matching stock_moves entries. Use per-row fix actions on the SCHAIN tab.'),
        'S6' => _('Sales orders with line items still awaiting delivery. These are healthy in-progress chains — no fix needed.'),
        'S7' => _('Delivery-note items where the delivered quantity has not yet been fully invoiced. Healthy in-progress chains — no fix needed.'),
        'S8' => _('Customer invoices with an outstanding unpaid balance (alloc < total). View the invoice in FA to record payment or allocate a credit note.'),
        'SCHAIN' => _('Consolidated sales chain view (replaces S3/S4/S5). Each row shows SO → Delivery → Stock Moves. Orange cells have a candidate match. Yellow cells have an Add button. Red cells are orphaned or missing stock moves. Ghost invoices show a Void button.'),
        'A1' => _('Recalculates supp_trans.alloc from the sum of supp_allocations.'),
        'A2' => _('Recalculates debtor_trans.alloc from the sum of cust_allocations.'),
        'A3' => _('Runs both A1 (supplier) and A2 (customer) alloc recalculations. This resets alloc to the actual allocation total, correcting any over-allocated condition.'),
        'A4' => _('Deletes orphaned supp_allocations rows where the from- or to-transaction no longer exists. Then recalculates supplier alloc (A1).'),
        'A5' => _('Deletes orphaned cust_allocations rows where the from- or to-transaction no longer exists. Then recalculates customer alloc (A2).'),
    );
}
