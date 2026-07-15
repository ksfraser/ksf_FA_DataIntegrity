<?php
/**
 * integrity_db.inc &#8212; Data integrity check queries for KSF FA DataIntegrity
 *
 * Each public function runs a SQL check and returns a db result set (or
 * an integer count when $count_only=true).  All queries use FA's standard
 * &TB_PREF& placeholder and db_query() / db_fetch() layer so they work
 * across both MySQL and MariaDB and respect the multi-company table prefix.
 *
 * Naming convention:
 *   check_<area>_<issue>()   &#8212; returns result set of offending rows
 *   fix_<area>_<issue>()     &#8212; applies a recalculation; returns rows affected
 *   count_<area>_issues()    &#8212; returns integer count for dashboard summary
 *
 * PHP 7.3+.
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
 * Chain arrow symbol for pipeline descriptions.
 * Use HTML entity &#8594; (→) rather than raw UTF-8 to avoid encoding
 * issues on FA's Latin-1 output.
 */
define('INTEG_ARROW', '&#8594;');

/**
 * Em dash symbol for labels and descriptions.
 * Use HTML entity &#8212; (—) for same encoding-safety reason.
 */
define('INTEG_EMDASH', '&#8212;');

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
 * CHECK P1 &#8212; grn_items.quantity_inv does not match the sum of
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
 * FIX P1 &#8212; Recalculate grn_items.quantity_inv from actual supp_invoice_items.
 *
 * Safe: only updates rows where a discrepancy exists.  Does NOT touch
 * supp_trans or purch_order_details &#8212; run fix_purchase_pod_qty_invoiced()
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
 * CHECK P2 &#8212; purch_order_details.qty_invoiced does not match the sum of
 * supp_invoice_items.quantity for all non-voided invoices on that PO line.
 *
 * Note: update_po() in po_db.inc resets qty_invoiced to 0 on PO edit &#8212;
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
 * FIX P2 &#8212; Recalculate purch_order_details.qty_invoiced from actual invoices.
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
 * CHECK P3 &#8212; purch_order_details.quantity_received does not match the sum
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
 * FIX P3 &#8212; Recalculate purch_order_details.quantity_received from GRN items.
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
 * CHECK P4 &#8212; grn_items.quantity_inv exceeds qty_recd (over-invoiced GRN line).
 * These cannot be auto-fixed &#8212; a credit note or manual void is required.
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
 * CHECK P5 &#8212; supp_invoice_items rows where the referenced grn_item_id no
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
 * CHECK P6 &#8212; grn_batch rows whose parent purch_order_no no longer exists.
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
 * CHECK P7 &#8212; Supplier invoices (supp_trans type 20) that have no
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
 * CHECK P8 &#8212; Purchase order detail lines not yet received.
 * Shows healthy POs still awaiting delivery (partial or full).
 *
 * @return resource
 * @since  2.5.0
 */
function check_purchase_po_not_received()
{
    $repo = new \FrontAccounting\Repository\PurchOrderDetailsRepository(integ_db_adapter());
    return $repo->findUnreceivedLines();
}

/**
 * CHECK P9 &#8212; GRN items not yet fully invoiced.
 * Shows GRN items where received quantity exceeds invoiced quantity.
 *
 * @return resource
 * @since  2.5.0
 */
function check_purchase_grn_not_invoiced()
{
    $repo = new \FrontAccounting\Repository\GrnItemsRepository(integ_db_adapter());
    return $repo->findUninvoicedLines();
}

/**
 * CHECK P10 &#8212; Supplier invoices not yet fully paid or credited.
 * Shows invoices where alloc < total amount (unpaid balance remains).
 *
 * @return resource
 * @since  2.5.0
 */
function check_purchase_invoice_unpaid()
{
    $repo = new \FrontAccounting\Repository\SupplierTransactionRepository(integ_db_adapter());
    return $repo->findUnpaidInvoices();
}

// ---------------------------------------------------------------------------

/**
 * CHECK P11 &#8212; Supplier invoices where GRN-linked items have zero total charge
 * (voided or zero-cost) but GL entries remain, producing "Received Items
 * Charged" = 0 while the invoice shows a GL total.
 *
 * Two patterns are caught:
 *   1. Voided GRN lines  &#8212; quantity = 0 AND unit_price = 0 (line-level void
 *      after posting left GL entries orphaned).
 *   2. Zero-cost GRN lines &#8212; quantity > 0, unit_price = 0 (non-stock items
 *      tracked via GRN for receipt only; actual cost entered as GL items).
 *
 * No auto-fix &#8212; a credit note is the recommended resolution for pattern 1.
 * Pattern 2 may be legitimate; each invoice should be reviewed.
 *
 * @return resource
 */
function check_purchase_voided_grn_items()
{
    $d = INTEG_DELTA;
    $sql = "
        SELECT
            st.trans_no,
            st.supplier_id,
            s.supp_name,
            st.reference,
            st.tran_date,
            ROUND(st.ov_amount + st.ov_gst + st.ov_discount, 4) AS total_amount,
            COALESCE(grn_stats.item_count, 0)       AS grn_item_count,
            COALESCE(grn_stats.voided_count, 0)     AS voided_count,
            COALESCE(grn_stats.zero_cost_count, 0)  AS zero_cost_count,
            COALESCE(grn_stats.active_charged_count, 0) AS active_charged_count,
            COALESCE(grn_stats.total_charged, 0)    AS total_charged,
            COALESCE(gl_stats.line_count, 0)        AS gl_line_count,
            ROUND(COALESCE(gl_stats.debit_total, 0), 4) AS gl_debit_total
        FROM &TB_PREF&supp_trans st
        JOIN &TB_PREF&suppliers s ON st.supplier_id = s.supplier_id
        LEFT JOIN (
            SELECT supp_trans_no,
                   COUNT(*) AS item_count,
                   SUM(CASE WHEN quantity = 0 AND unit_price = 0 THEN 1 ELSE 0 END) AS voided_count,
                   SUM(CASE WHEN quantity != 0 AND unit_price = 0 THEN 1 ELSE 0 END) AS zero_cost_count,
                   SUM(CASE WHEN quantity != 0 AND unit_price != 0 THEN 1 ELSE 0 END) AS active_charged_count,
                   SUM(quantity * unit_price) AS total_charged
            FROM &TB_PREF&supp_invoice_items
            WHERE supp_trans_type = 20 AND grn_item_id IS NOT NULL
            GROUP BY supp_trans_no
        ) grn_stats ON grn_stats.supp_trans_no = st.trans_no
        LEFT JOIN (
            SELECT type_no,
                   COUNT(*) AS line_count,
                   SUM(CASE WHEN amount > 0 THEN amount ELSE 0 END) AS debit_total
            FROM &TB_PREF&gl_trans
            WHERE type = 20
            GROUP BY type_no
        ) gl_stats ON gl_stats.type_no = st.trans_no
        WHERE st.type = 20
          AND st.ov_amount != 0
          AND COALESCE(grn_stats.item_count, 0) > 0
          AND ABS(COALESCE(grn_stats.total_charged, 0)) < $d
          AND COALESCE(gl_stats.line_count, 0) > 0
        ORDER BY s.supp_name, st.tran_date";

    return db_query($sql, 'Check P11: Zero-charged GRN items with active GL');
}

// ===========================================================================
// SALES CHAIN CHECKS
// ===========================================================================

/**
 * CHECK S1 &#8212; sales_order_details.qty_sent does not match the sum of
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
            COALESCE(del_sum.qty_sent, 0)         AS qty_sent_actual,
            sod.qty_sent - COALESCE(del_sum.qty_sent, 0) AS discrepancy,
            COALESCE(del_sum.del_count, 0)        AS del_count,
            COALESCE(del_sum.voided_del_count, 0) AS voided_del_count,
            CASE
                WHEN sod.qty_sent = 0 AND COALESCE(del_sum.qty_sent, 0) > 0 THEN 'Stored 0 &#8212; stored counter never set'
                WHEN sod.qty_sent > COALESCE(del_sum.qty_sent, 0) THEN 'Stored too high'
                WHEN sod.qty_sent < COALESCE(del_sum.qty_sent, 0) THEN 'Stored too low'
                ELSE ''
            END AS likely_cause
        FROM &TB_PREF&sales_order_details sod
        JOIN &TB_PREF&sales_orders so
            ON so.order_no   = sod.order_no
           AND so.trans_type = sod.trans_type
        JOIN &TB_PREF&debtors_master dm ON dm.debtor_no = so.debtor_no
        LEFT JOIN (
            SELECT dtd.src_id,
                   COUNT(*) AS del_count,
                   SUM(CASE WHEN dt.ov_amount = 0 THEN 1 ELSE 0 END) AS voided_del_count,
                   SUM(CASE WHEN dt.ov_amount != 0 THEN dtd.qty_done ELSE 0 END) AS qty_sent
            FROM &TB_PREF&debtor_trans_details dtd
            JOIN &TB_PREF&debtor_trans dt
                ON dt.trans_no = dtd.debtor_trans_no
               AND dt.type     = dtd.debtor_trans_type
            WHERE dtd.debtor_trans_type = 13
            GROUP BY dtd.src_id
        ) del_sum ON del_sum.src_id = sod.id
        WHERE ABS(sod.qty_sent - COALESCE(del_sum.qty_sent, 0)) > " . INTEG_DELTA . "
          AND so.trans_type = 30
        ORDER BY dm.name, so.reference";

    return db_query($sql, 'Check S1: Sales order line qty_sent vs delivery notes');
}

/**
 * FIX S1 &#8212; Recalculate sales_order_details.qty_sent from delivery notes.
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
 * CHECK S2 &#8212; sales_order_details.invoiced does not match the sum of
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
            COALESCE(inv_sum.qty_invoiced, 0)   AS qty_inv_actual,
            sod.invoiced - COALESCE(inv_sum.qty_invoiced, 0) AS discrepancy,
            COALESCE(inv_sum.inv_count, 0)      AS inv_count,
            COALESCE(inv_sum.voided_inv_count, 0) AS voided_inv_count,
            COALESCE(cn_sum.cn_count, 0)        AS cn_count,
            COALESCE(cn_sum.cn_qty_total, 0)    AS cn_qty_total,
            CASE
                WHEN sod.invoiced = 0 AND COALESCE(inv_sum.qty_invoiced, 0) > 0 THEN
                    CASE
                        WHEN COALESCE(cn_sum.cn_count, 0) > 0 THEN 'Stored 0 &#8212; credit notes exist'
                        ELSE 'Stored 0 &#8212; stored counter never set'
                    END
                WHEN sod.invoiced > COALESCE(inv_sum.qty_invoiced, 0) THEN
                    CASE
                        WHEN COALESCE(cn_sum.cn_count, 0) > 0
                             AND sod.invoiced = COALESCE(inv_sum.qty_invoiced, 0) + ABS(COALESCE(cn_sum.cn_qty_total, 0))
                        THEN 'Explained by credit notes'
                        ELSE 'Stored too high'
                    END
                WHEN sod.invoiced < COALESCE(inv_sum.qty_invoiced, 0) THEN 'Stored too low'
                ELSE ''
            END AS likely_cause
        FROM &TB_PREF&sales_order_details sod
        JOIN &TB_PREF&sales_orders so
            ON so.order_no   = sod.order_no
           AND so.trans_type = sod.trans_type
        JOIN &TB_PREF&debtors_master dm ON dm.debtor_no = so.debtor_no
        LEFT JOIN (
            SELECT dtd.src_id,
                   COUNT(*) AS inv_count,
                   SUM(CASE WHEN dt.ov_amount = 0 THEN 1 ELSE 0 END) AS voided_inv_count,
                   SUM(CASE WHEN dt.ov_amount != 0 THEN dtd.quantity ELSE 0 END) AS qty_invoiced
            FROM &TB_PREF&debtor_trans_details dtd
            JOIN &TB_PREF&debtor_trans dt
                ON dt.trans_no = dtd.debtor_trans_no
               AND dt.type     = dtd.debtor_trans_type
            WHERE dtd.debtor_trans_type = 10
            GROUP BY dtd.src_id
        ) inv_sum ON inv_sum.src_id = sod.id
        LEFT JOIN (
            SELECT dtd.src_id,
                   COUNT(*) AS cn_count,
                   SUM(dtd.quantity) AS cn_qty_total
            FROM &TB_PREF&debtor_trans_details dtd
            JOIN &TB_PREF&debtor_trans dt
                ON dt.trans_no = dtd.debtor_trans_no
               AND dt.type     = dtd.debtor_trans_type
            WHERE dtd.debtor_trans_type = 11
              AND dt.ov_amount != 0
            GROUP BY dtd.src_id
        ) cn_sum ON cn_sum.src_id = sod.id
        WHERE ABS(sod.invoiced - COALESCE(inv_sum.qty_invoiced, 0)) > " . INTEG_DELTA . "
          AND so.trans_type = 30
        ORDER BY dm.name, so.reference";

    return db_query($sql, 'Check S2: Sales order line invoiced qty vs invoice details');
}

/**
 * FIX S2 &#8212; Recalculate sales_order_details.invoiced from customer invoices.
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
 * CHECK S3 &#8212; Customer invoices (type 10) that have no debtor_trans_details
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
 * CHECK S4 &#8212; Delivery notes (type 13) whose parent sales order (debtor_trans.order_)
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
 * CHECK S5 &#8212; Delivery notes with no matching stock_moves entries.
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
 * CHECK S6 &#8212; Sales orders with lines not yet fully delivered.
 *
 * Shows healthy SOs still awaiting delivery (partial or full).
 * Supports optional filtering by customer debtors_master.debtor_no
 * and/or cust_branch.branch_code for CRM module reuse.
 *
 * For each row, enriches with possible "Direct Invoice" matches
 * (customer invoices created after the SO date that might bypass
 * the normal SO&#8594;Delivery&#8594;Invoice chain).
 *
 * @param  int|null $debtorNo   Optional customer debtor_no filter
 * @param  int|null $branchCode Optional branch code filter
 * @return array
 * @since  2.5.0
 */
function check_sales_so_not_delivered($debtorNo = null, $branchCode = null)
{
    global $path_to_root;

    $repo = new \FrontAccounting\Repository\SalesOrderDetailsRepository(integ_db_adapter());

    if (!empty($debtorNo)) {
        $rows = $repo->findUndeliveredLines($debtorNo, $branchCode);
    } else {
        $rows = $repo->findUndeliveredLines();
    }

    if (empty($rows)) {
        return $rows;
    }

    // Collect unique debtors and their earliest SO date
    $debtorData = array();
    foreach ($rows as $r) {
        $dno = (int)$r['debtor_no'];
        if (!isset($debtorData[$dno])) {
            $debtorData[$dno] = array('min_date' => $r['ord_date']);
        } elseif ($r['ord_date'] < $debtorData[$dno]['min_date']) {
            $debtorData[$dno]['min_date'] = $r['ord_date'];
        }
    }

    // Fetch customer invoices (type 10) for those debtors
    $ids = implode(',', array_keys($debtorData));
    $sql = "SELECT debtor_no, trans_no, reference, tran_date
            FROM &TB_PREF&debtor_trans
            WHERE type = 10 AND ov_amount != 0
              AND debtor_no IN ($ids)
            ORDER BY debtor_no, tran_date";
    $invResult = db_query($sql, 'S6: find direct invoice matches');
    $invoicesByDebtor = array();
    while ($inv = db_fetch($invResult)) {
        $dno = (int)$inv['debtor_no'];
        $invoicesByDebtor[$dno][] = $inv;
    }

    // Enrich each row
    foreach ($rows as &$row) {
        $dno = (int)$row['debtor_no'];
        $matches = array();
        if (isset($invoicesByDebtor[$dno])) {
            foreach ($invoicesByDebtor[$dno] as $inv) {
                if ($inv['tran_date'] >= $row['ord_date']) {
                    $matches[] = $inv;
                }
            }
        }
        $row['match_count'] = count($matches);
        $row['first_match_inv_no'] = !empty($matches) ? (int)$matches[0]['trans_no'] : 0;

        $matchLinks = array();
        foreach ($matches as $m) {
            $url = $path_to_root
                . '/gl/void_transaction.php?type=10&trans_no=' . (int)$m['trans_no'];
            $matchLinks[] = '<a href="' . htmlspecialchars($url) . '" target="_blank">#'
                . (int)$m['trans_no'] . ' ' . htmlspecialchars($m['reference'])
                . ' (' . $m['tran_date'] . ')</a>';
        }
        $row['possible_matches'] = !empty($matchLinks)
            ? implode('<br>', $matchLinks)
            : '';
    }
    unset($row);

    return $rows;
}

/**
 * CHECK S7 &#8212; Delivery-note items not yet fully invoiced.
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
    $repo = new \FrontAccounting\Repository\DebtorTransactionDetailRepository(integ_db_adapter());
    return $repo->findUninvoicedDeliveries($debtorNo, $branchCode);
}

/**
 * CHECK S8 &#8212; Customer invoices not yet fully paid or credited (returned).
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
    $repo = new \FrontAccounting\Repository\DebtorTransactionRepository(integ_db_adapter());
    return $repo->findUnpaidInvoices($debtorNo, $branchCode);
}

// ---------------------------------------------------------------------------

/**
 * CHECK SCUST &#8212; Sales transactions where charge-to customer/branch differs
 * from the linked SO or from the branch's customer owner.
 *
 * Finds deliveries (type=13) and invoices (type=10) where:
 *   - debtor_no != cust_branch.debtor_no (transaction's branch belongs to a different customer)
 *   - OR debtor_no != sales_orders.debtor_no (SO has a different customer)
 *   - OR branch_code != sales_orders.branch_code (SO has a different branch)
 *
 * Returns array (post-processed, not a raw db result).
 *
 * @return array
 */
function check_sales_customer_mismatch()
{
    $sql = "SELECT
                dt.trans_no,
                dt.type,
                dt.tran_date,
                dt.reference,
                dt.debtor_no                         AS charge_to_no,
                charge_to.name                       AS charge_to_name,
                dt.branch_code,
                cb.branch_code                       AS branch_code_actual,
                cb.br_name AS branch_name,
                cb.debtor_no                         AS branch_owner_no,
                branch_owner.name                    AS branch_owner_name,
                dt.order_                            AS so_order_no,
                so.debtor_no                         AS so_debtor_no,
                so_cust.name                         AS so_customer_name,
                so.branch_code                       AS so_branch_code,
                so.reference                         AS so_ref,
                CASE
                    WHEN cb.debtor_no IS NOT NULL AND dt.debtor_no != cb.debtor_no THEN 'branch_owner_mismatch'
                    WHEN dt.order_ > 0 AND so.debtor_no IS NOT NULL AND so.debtor_no != dt.debtor_no THEN 'so_customer_mismatch'
                    WHEN dt.order_ > 0 AND so.branch_code IS NOT NULL AND so.branch_code != dt.branch_code THEN 'so_branch_mismatch'
                    ELSE 'unknown'
                END AS issue_type
            FROM " . TB_PREF . "debtor_trans dt
            JOIN " . TB_PREF . "debtors_master charge_to ON charge_to.debtor_no = dt.debtor_no
            LEFT JOIN " . TB_PREF . "cust_branch cb ON cb.branch_code = dt.branch_code
            LEFT JOIN " . TB_PREF . "debtors_master branch_owner ON branch_owner.debtor_no = cb.debtor_no
            LEFT JOIN " . TB_PREF . "sales_orders so ON so.order_no = dt.order_ AND so.trans_type = 30
            LEFT JOIN " . TB_PREF . "debtors_master so_cust ON so_cust.debtor_no = so.debtor_no
            WHERE dt.type IN (10, 13)
              AND dt.ov_amount != 0
              AND (
                  (cb.debtor_no IS NOT NULL AND dt.debtor_no != cb.debtor_no)
                  OR (dt.order_ > 0 AND so.debtor_no IS NOT NULL AND so.debtor_no != dt.debtor_no)
                  OR (dt.order_ > 0 AND so.branch_code IS NOT NULL AND so.branch_code != dt.branch_code)
              )
            ORDER BY charge_to.name, dt.tran_date";

    $result = db_query($sql, 'Check SCUST: customer/branch inconsistency');
    $rows = array();
    if ($result) {
        while ($r = db_fetch($result)) {
            $rows[] = $r;
        }
        db_free_result($result);
    }
    return $rows;
}

// ===========================================================================
// ALLOCATION CHECKS &#8212; both purchase and sales sides
// ===========================================================================

/**
 * CHECK A1 &#8212; supp_trans.alloc does not match the sum of supp_allocations.amt
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
            s.supp_name,
            CASE
                WHEN COALESCE(sa_sum.total_alloc, 0) = 0 AND st.alloc > 0 THEN
                    CASE
                        WHEN COALESCE(pmts.voided_pmt_count, 0) > 0 THEN 'Voided payment'
                        WHEN COALESCE(pmts.active_pmt_count, 0)  > 0 THEN 'Active payment exists'
                        ELSE 'No payments found'
                    END
                ELSE ''
            END AS likely_cause,
            pmts.voided_pmt_count,
            pmts.active_pmt_count
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
        LEFT JOIN (
            SELECT supplier_id,
                   COUNT(*)                                                        AS payment_count,
                   SUM(CASE WHEN ov_amount = 0 THEN 1 ELSE 0 END)                 AS voided_pmt_count,
                   SUM(CASE WHEN ov_amount < 0 THEN 1 ELSE 0 END)                 AS active_pmt_count
            FROM &TB_PREF&supp_trans
            WHERE type = 22
            GROUP BY supplier_id
        ) pmts ON pmts.supplier_id = st.supplier_id
        WHERE st.ov_amount != 0
          AND ABS(st.alloc - COALESCE(sa_sum.total_alloc, 0)) > " . INTEG_DELTA . "
          $where
        ORDER BY s.supp_name, st.tran_date";

    return db_query($sql, 'Check A1: Supplier alloc vs supp_allocations');
}

/**
 * FIX A1 &#8212; Recalculate supp_trans.alloc from supp_allocations table.
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
 * CHECK A2 &#8212; debtor_trans.alloc does not match the sum of cust_allocations.amt
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
            dm.name AS customer_name,
            CASE
                WHEN COALESCE(ca_sum.total_alloc, 0) = 0 AND dt.alloc > 0 THEN
                    CASE
                        WHEN COALESCE(pmts.voided_pmt_count, 0) > 0 THEN 'Voided payment'
                        WHEN COALESCE(pmts.active_pmt_count, 0)  > 0 THEN 'Active payment exists'
                        ELSE 'No payments found'
                    END
                ELSE ''
            END AS likely_cause,
            pmts.voided_pmt_count,
            pmts.active_pmt_count
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
        LEFT JOIN (
            SELECT debtor_no,
                   COUNT(*)                                                        AS payment_count,
                   SUM(CASE WHEN ov_amount = 0 THEN 1 ELSE 0 END)                 AS voided_pmt_count,
                   SUM(CASE WHEN ov_amount < 0 THEN 1 ELSE 0 END)                 AS active_pmt_count
            FROM &TB_PREF&debtor_trans
            WHERE type = 12
            GROUP BY debtor_no
        ) pmts ON pmts.debtor_no = dt.debtor_no
        WHERE dt.ov_amount != 0
          AND ABS(dt.alloc - COALESCE(ca_sum.total_alloc, 0)) > " . INTEG_DELTA . "
          $where
        ORDER BY dm.name, dt.tran_date";

    return db_query($sql, 'Check A2: Customer alloc vs cust_allocations');
}

/**
 * FIX A2 &#8212; Recalculate debtor_trans.alloc from cust_allocations.
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
 * CHECK A3 &#8212; Transactions where alloc exceeds the absolute transaction amount
 * (over-allocated &#8212; mathematically impossible without a data error).
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
 * CHECK A4 &#8212; Orphaned supp_allocations rows where either the from-transaction
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
 * CHECK A5 &#8212; Orphaned cust_allocations rows where either the from-transaction
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
 * CHECK A6 &#8212; Supplier payments with no allocation record (orphaned payments).
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
 * Called per-row from the A6 tab &#8212; never bulk.  Returns a status string
 * for the caller to display as a notification.
 *
 * @param  int    $payment_no  supp_trans.trans_no where type=22
 * @return string  'ok', 'not_found', 'no_match', 'multiple_matches', 'already_allocated'
 */
function quick_allocate_supplier_payment($payment_no)
{
    $payment_no = (int)$payment_no;
    $d = INTEG_DELTA;

    // &#8212; 1. Load the payment record &#8212;
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

    // &#8212; 2. Check it's not already allocated &#8212;
    $sql = "SELECT COUNT(*) AS cnt
            FROM &TB_PREF&supp_allocations
            WHERE trans_no_from = $payment_no AND trans_type_from = 22";
    $r2 = db_query($sql, 'QuickAlloc: check existing');
    $row2 = db_fetch($r2);
    if ($row2 && (int)$row2['cnt'] > 0) {
        return 'already_allocated';
    }

    // &#8212; 3. Find matching unpaid invoices from the same supplier &#8212;
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

    // &#8212; 4. Exactly one match &#8212; create the allocation &#8212;
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
 * purchase chain (PO &#8594; GRN &#8594; Supplier Invoice &#8594; Payment) and whether any
 * counter values disagree.
 *
 * Categories (mutually exclusive, collectively exhaustive):
 *   open_po                     &#8212; PO exists, no GRN, no invoice, no payment
 *   received_not_invoiced       &#8212; GRN exists, no invoice (includes the
 *                                  Direct-Invoice-timeout bug scenario)
 *   invoiced_not_paid_clean     &#8212; invoice exists, no payment, counters OK
 *   invoiced_not_paid_corrupted &#8212; invoice exists, no payment, counter drift
 *   complete_clean              &#8212; invoice + payment exist, counters OK
 *   complete_corrupted          &#8212; invoice + payment exist, counter drift
 *   anomalous                   &#8212; payment exists but no invoice (data error)
 *   total                       &#8212; sum of all categories
 *
 * "Has GRN"     &#8212; at least one grn_batch row references this PO.
 * "Has invoice" &#8212; at least one non-voided supp_trans (type 20) is linked to
 *                 a purch_order_details line for this PO.
 * "Has payment" &#8212; at least one supp_allocations row references an invoice
 *                 on this PO (partially or fully paid).
 * "Has mismatch"&#8212; any P1/P2/P3-style counter drift detected on this PO.
 *
 * PHP 7.3 / MySQL 5.7+ compatible.
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
 * sales chain (SO &#8594; Delivery &#8594; Customer Invoice &#8594; Customer Payment) and
 * whether any counter values disagree.
 *
 * Categories mirror get_purchase_pipeline_counts() with:
 *   open_order              &#8594; SO, no delivery, no invoice, no payment
 *   delivered_not_invoiced  &#8594; delivery exists, no invoice
 *   (plus the same invoiced/complete/corrupted/anomalous buckets)
 *
 * "Has delivery" &#8212; any debtor_trans type 13 with order_ = so.order_no.
 * "Has invoice"  &#8212; any debtor_trans type 10 with order_ = so.order_no.
 * "Has payment"  &#8212; any cust_allocations row referencing an invoice on this SO.
 * "Has mismatch" &#8212; S1 (sod.qty_sent drift) or S2 (sod.invoiced drift).
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

// ===========================================================================
// DUPLICATE DOCUMENT CHECKS
// ===========================================================================

/**
 * Check S9 &#8212; Duplicate sales documents.
 *
 * Detects duplicate (type,trans_no) in debtor_trans (invoices, credits,
 * payments, deliveries) and duplicate order_no in sales_orders.
 *
 * @return array
 */
function check_sales_duplicate_docs()
{
    $rows = array();

    // Duplicate (type, trans_no) in debtor_trans
    $result = db_query(
        "SELECT dt.type, dt.trans_no,
                COUNT(*) AS cnt,
                GROUP_CONCAT(DISTINCT dt.debtor_no ORDER BY dt.tran_date) AS debtor_nos,
                GROUP_CONCAT(DISTINCT dt.reference ORDER BY dt.tran_date) AS refs
         FROM " . TB_PREF . "debtor_trans dt
         WHERE dt.type IN (" . ST_SALESINVOICE . "," . ST_CUSTCREDIT . ","
            . ST_CUSTPAYMENT . "," . ST_CUSTDELIVERY . ")
         GROUP BY dt.type, dt.trans_no
         HAVING COUNT(*) > 1
         ORDER BY dt.type, dt.trans_no",
        'S9: duplicate debtor_trans'
    );
    while ($row = db_fetch($result)) {
        $typeLabel = '';
        switch ((int)$row['type']) {
            case ST_SALESINVOICE: $typeLabel = _('Cust Invoice'); break;
            case ST_CUSTCREDIT:   $typeLabel = _('Cust Credit');  break;
            case ST_CUSTPAYMENT:  $typeLabel = _('Cust Payment'); break;
            case ST_CUSTDELIVERY: $typeLabel = _('Delivery');     break;
        }
        $rows[] = array(
            'doc_type'  => $typeLabel,
            'trans_no'  => (int)$row['trans_no'],
            'type'      => (int)$row['type'],
            'count'     => (int)$row['cnt'],
            'refs'      => $row['refs'],
        );
    }

    // Duplicate (trans_type, order_no) in sales_orders
    $result2 = db_query(
        "SELECT so.trans_type, so.order_no,
                COUNT(*) AS cnt,
                GROUP_CONCAT(DISTINCT so.reference ORDER BY so.ord_date) AS refs
         FROM " . TB_PREF . "sales_orders so
         GROUP BY so.trans_type, so.order_no
         HAVING COUNT(*) > 1
         ORDER BY so.trans_type, so.order_no",
        'S9: duplicate sales_orders'
    );
    while ($row = db_fetch($result2)) {
        $rows[] = array(
            'doc_type'  => _('Sales Order'),
            'trans_no'  => (int)$row['order_no'],
            'type'      => (int)$row['trans_type'],
            'count'     => (int)$row['cnt'],
            'refs'      => $row['refs'],
        );
    }

    return $rows;
}

/**
 * Check P12 &#8212; Duplicate purchase documents.
 *
 * Detects duplicate (type,trans_no) in supp_trans (invoices, credits,
 * payments) and duplicate order_no in purch_orders.
 *
 * @return array
 */
function check_purchase_duplicate_docs()
{
    $rows = array();

    // Duplicate (type, trans_no) in supp_trans
    $result = db_query(
        "SELECT st.type, st.trans_no,
                COUNT(*) AS cnt,
                GROUP_CONCAT(DISTINCT st.supplier_id ORDER BY st.tran_date) AS supp_ids,
                GROUP_CONCAT(DISTINCT st.reference ORDER BY st.tran_date) AS refs
         FROM " . TB_PREF . "supp_trans st
         WHERE st.type IN (" . ST_SUPPINVOICE . "," . ST_SUPPCREDIT . ","
            . ST_SUPPAYMENT . ")
         GROUP BY st.type, st.trans_no
         HAVING COUNT(*) > 1
         ORDER BY st.type, st.trans_no",
        'P12: duplicate supp_trans'
    );
    while ($row = db_fetch($result)) {
        $typeLabel = '';
        switch ((int)$row['type']) {
            case ST_SUPPINVOICE: $typeLabel = _('Supp Invoice'); break;
            case ST_SUPPCREDIT:  $typeLabel = _('Supp Credit');  break;
            case ST_SUPPAYMENT:  $typeLabel = _('Supp Payment'); break;
        }
        $rows[] = array(
            'doc_type'  => $typeLabel,
            'trans_no'  => (int)$row['trans_no'],
            'type'      => (int)$row['type'],
            'count'     => (int)$row['cnt'],
            'refs'      => $row['refs'],
        );
    }

    // Duplicate order_no in purch_orders
    $result2 = db_query(
        "SELECT po.order_no,
                COUNT(*) AS cnt,
                GROUP_CONCAT(DISTINCT po.reference ORDER BY po.ord_date) AS refs
         FROM " . TB_PREF . "purch_orders po
         GROUP BY po.order_no
         HAVING COUNT(*) > 1
         ORDER BY po.order_no",
        'P12: duplicate purch_orders'
    );
    while ($row = db_fetch($result2)) {
        $rows[] = array(
            'doc_type'  => _('Purch Order'),
            'trans_no'  => (int)$row['order_no'],
            'type'      => 18,
            'count'     => (int)$row['cnt'],
            'refs'      => $row['refs'],
        );
    }

    return $rows;
}

// ===========================================================================
// P13 / S10 &#8212; Edit vs Drift Diagnostic
// ===========================================================================

/**
 * CHECK P13 &#8212; PO edit vs qty_invoiced drift correlation diagnostic.
 *
 * For every purchase order, shows whether it was edited (audit_trail
 * "Updated." entries for type=18) and whether its qty_invoiced has drifted
 * (i.e. P2 condition).  Helps confirm whether FA's update_po() bug is the
 * root cause of P2 drift.
 *
 * @return array  Each element: order_no, edited, has_drift, discrepancy
 */
function check_purchase_po_edit_diagnostic()
{
    $sql = "SELECT pod.order_no,
                   SUM(pod.qty_invoiced) AS qty_inv_stored,
                   COALESCE(inv_sum.qty_invoiced, 0) AS qty_inv_actual,
                   SUM(pod.qty_invoiced) - COALESCE(inv_sum.qty_invoiced, 0) AS discrepancy
            FROM " . TB_PREF . "purch_order_details pod
            LEFT JOIN (
                SELECT si.po_detail_item_id, SUM(si.quantity) AS qty_invoiced
                FROM " . TB_PREF . "supp_invoice_items si
                JOIN " . TB_PREF . "supp_trans st
                    ON st.trans_no = si.supp_trans_no
                   AND st.type = si.supp_trans_type
                WHERE si.supp_trans_type = 20 AND st.ov_amount != 0
                GROUP BY si.po_detail_item_id
            ) inv_sum ON inv_sum.po_detail_item_id = pod.po_detail_item
            GROUP BY pod.order_no
            ORDER BY pod.order_no";

    $result = db_query($sql, 'P13: PO drift summary per order');

    $poDrift = array();
    while ($row = db_fetch($result)) {
        $disc = (float)$row['discrepancy'];
        $poDrift[(int)$row['order_no']] = array(
            'drift'       => abs($disc) > INTEG_DELTA,
            'discrepancy' => $disc,
            'stored'      => (float)$row['qty_inv_stored'],
            'actual'      => (float)$row['qty_inv_actual'],
        );
    }

    $editSql = "SELECT DISTINCT trans_no
                FROM " . TB_PREF . "audit_trail
                WHERE type = 18 AND description = 'Updated.'";
    $editResult = db_query($editSql, 'P13: PO edit audit trail');
    $editedPos = array();
    while ($row = db_fetch($editResult)) {
        $editedPos[(int)$row['trans_no']] = true;
    }

    $rows = array();
    $allOrderNos = array_unique(array_merge(array_keys($poDrift), array_keys($editedPos)));
    sort($allOrderNos);

    foreach ($allOrderNos as $orderNo) {
        $driftInfo = isset($poDrift[$orderNo])
            ? $poDrift[$orderNo]
            : array('drift' => false, 'discrepancy' => 0, 'stored' => 0, 'actual' => 0);
        $rows[] = array(
            'order_no'    => (int)$orderNo,
            'edited'      => isset($editedPos[$orderNo]),
            'has_drift'   => $driftInfo['drift'],
            'discrepancy' => $driftInfo['discrepancy'],
            'stored'      => $driftInfo['stored'],
            'actual'      => $driftInfo['actual'],
        );
    }

    return $rows;
}

/**
 * CHECK S10 &#8212; SO edit vs drift correlation diagnostic.
 *
 * For every sales order, shows whether it was edited (audit_trail "Updated."
 * entries for type=30), whether any of its lines have qty_sent drift (S1),
 * and whether any lines have invoiced drift (S2).
 *
 * @return array  Each element: order_no, so_type, edited, s1_drift, s2_drift
 */
function check_sales_so_edit_diagnostic()
{
    $sql = "SELECT
                sod.order_no,
                sod.trans_type AS so_type,
                COUNT(*) AS total_lines,
                SUM(ABS(sod.qty_sent - COALESCE(del_sum.qty_sent, 0))) AS s1_total_disc,
                SUM(ABS(sod.invoiced - COALESCE(inv_sum.qty_inv, 0))) AS s2_total_disc
            FROM " . TB_PREF . "sales_order_details sod
            LEFT JOIN (
                SELECT dtd.src_id,
                       SUM(CASE WHEN dt.ov_amount != 0 THEN dtd.qty_done ELSE 0 END) AS qty_sent
                FROM " . TB_PREF . "debtor_trans_details dtd
                JOIN " . TB_PREF . "debtor_trans dt
                    ON dt.trans_no = dtd.debtor_trans_no
                   AND dt.type = dtd.debtor_trans_type
                WHERE dtd.debtor_trans_type = 13
                GROUP BY dtd.src_id
            ) del_sum ON del_sum.src_id = sod.id
            LEFT JOIN (
                SELECT dtd.src_id,
                       SUM(CASE WHEN dt.ov_amount != 0 THEN dtd.quantity ELSE 0 END) AS qty_inv
                FROM " . TB_PREF . "debtor_trans_details dtd
                JOIN " . TB_PREF . "debtor_trans dt
                    ON dt.trans_no = dtd.debtor_trans_no
                   AND dt.type = dtd.debtor_trans_type
                WHERE dtd.debtor_trans_type = 10
                GROUP BY dtd.src_id
            ) inv_sum ON inv_sum.src_id = sod.id
            WHERE sod.trans_type = 30
            GROUP BY sod.order_no, sod.trans_type
            ORDER BY sod.order_no";

    $result = db_query($sql, 'S10: SO drift summary per order');

    $soDrift = array();
    while ($row = db_fetch($result)) {
        $soDrift[(int)$row['order_no']] = array(
            's1_drift'    => (float)$row['s1_total_disc'] > INTEG_DELTA,
            's2_drift'    => (float)$row['s2_total_disc'] > INTEG_DELTA,
            'so_type'     => (int)$row['so_type'],
            'total_lines' => (int)$row['total_lines'],
        );
    }

    $editSql = "SELECT DISTINCT trans_no
                FROM " . TB_PREF . "audit_trail
                WHERE type = 30 AND description = 'Updated.'";
    $editResult = db_query($editSql, 'S10: SO edit audit trail');
    $editedSos = array();
    while ($row = db_fetch($editResult)) {
        $editedSos[(int)$row['trans_no']] = true;
    }

    $rows = array();
    $allOrderNos = array_unique(array_merge(array_keys($soDrift), array_keys($editedSos)));
    sort($allOrderNos);

    foreach ($allOrderNos as $orderNo) {
        $driftInfo = isset($soDrift[$orderNo])
            ? $soDrift[$orderNo]
            : array('s1_drift' => false, 's2_drift' => false, 'so_type' => 30, 'total_lines' => 0);
        $rows[] = array(
            'order_no'    => (int)$orderNo,
            'so_type'     => $driftInfo['so_type'],
            'total_lines' => $driftInfo['total_lines'],
            'edited'      => isset($editedSos[$orderNo]),
            's1_drift'    => $driftInfo['s1_drift'],
            's2_drift'    => $driftInfo['s2_drift'],
        );
    }

    return $rows;
}

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
        'P11' => 'check_purchase_voided_grn_items',
        'P12' => 'check_purchase_duplicate_docs',
        'P13' => 'check_purchase_po_edit_diagnostic',
        'PCHAIN' => 'check_purchase_chain',
        'S1' => 'check_sales_sod_qty_sent_mismatch',
        'S2' => 'check_sales_sod_invoiced_mismatch',
        'S3' => 'check_sales_ghost_invoices',
        'S4' => 'check_sales_orphaned_deliveries',
        'S5' => 'check_sales_delivery_missing_stock_moves',
        'S6' => 'check_sales_so_not_delivered',
        'S7' => 'check_sales_delivery_not_invoiced',
        'S8' => 'check_sales_invoice_unpaid',
        'S9' => 'check_sales_duplicate_docs',
        'S10' => 'check_sales_so_edit_diagnostic',
        'SCHAIN' => 'check_sales_chain',
        'SCUST' => 'check_sales_customer_mismatch',
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
        'P11' => _('Supplier invoices with zero-charged or voided GRN items but active GL entries'),
        'P12' => _('Duplicate purchase documents (supplier invoices/credits/payments, purchase orders)'),
        'P13' => _('PO edit vs qty_invoiced drift correlation diagnostic'),
        'S1' => _('Sales order line qty_sent drift'),
        'S2' => _('Sales order line invoiced qty drift'),
        'S3' => _('Ghost customer invoices (no line items)'),
        'S4' => _('Orphaned delivery notes (missing sales order)'),
        'S5' => _('Delivery notes with no stock moves'),
        'S6' => _('Sales orders not delivered (outstanding)'),
        'S7' => _('Delivery items not invoiced'),
        'S8' => _('Customer invoices not paid or credited'),
        'S9' => _('Duplicate sales documents (invoices, credits, payments, deliveries, sales orders)'),
        'S10' => _('SO edit vs drift correlation diagnostic (S1 qty_sent + S2 invoiced)'),
        'PCHAIN' => _('Purchase chain &#8212; broken links (replaces P5/P6/P7)'),
        'SCHAIN' => _('Sales chain &#8212; broken links (replaces S3/S4/S5)'),
        'SCUST' => _('Sales transactions where charge-to customer/branch differs from the linked SO or branch owner'),
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
        'P8' => null,  // view-only &#8212; no auto-fix (healthy in-progress chain)
        'P9' => null,  // view-only &#8212; no auto-fix (healthy in-progress chain)
        'P10' => null,  // view-only &#8212; no auto-fix (healthy in-progress chain)
        'P11' => null,  // view-only &#8212; no auto-fix (credit note required)
        'P12' => null,  // view-only &#8212; manual DB cleanup required
        'P13' => null,  // diagnostic only &#8212; no auto-fix
        'PCHAIN' => null,  // per-row chain fix actions (no bulk)
        'S1' => 'fix_sales_sod_qty_sent',
        'S2' => 'fix_sales_sod_invoiced',
        'S3' => null,  // ghost invoices: manual per-row fix (see SCHAIN)
        'S4' => null,  // orphaned deliveries: manual per-row fix (see SCHAIN)
        'S5' => null,  // missing stock moves: manual per-row fix (see SCHAIN)
        'S6' => null,  // view-only &#8212; no auto-fix (healthy in-progress chain)
        'S7' => null,  // view-only &#8212; no auto-fix (healthy in-progress chain)
        'S8' => null,  // view-only &#8212; no auto-fix (healthy in-progress chain)
        'S9' => null,  // view-only &#8212; manual DB cleanup required
        'S10' => null, // diagnostic only &#8212; no auto-fix
        'SCHAIN' => null, // per-row chain fix actions (no bulk)
        'SCUST' => null,  // per-row fix actions (no bulk)
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
        'P8' => _('Purchase orders with line items still awaiting delivery. These are healthy in-progress chains &#8212; no fix needed.'),
        'P9' => _('Goods-received notes where the received quantity has not yet been fully invoiced. Healthy in-progress chains &#8212; no fix needed.'),
        'P10' => _('Supplier invoices with an outstanding unpaid balance (alloc < total). View the invoice in FA to record payment or allocate a credit note.'),
        'P11' => _('Supplier invoices where GRN-linked items have a total charge of $0 (voided lines where qty/price were zeroed, or zero-cost non-stock items) but GL entries remain. Voided lines likely need a credit note. Zero-cost lines may be legitimate &#8212; review each invoice\'s account coding.'),
        'PCHAIN' => _('Consolidated purchase chain view (replaces P5/P6/P7). Each row shows PO &#8594; GRN &#8594; Invoice. Green cells are healthy. Orange cells have a candidate match &#8212; click Match to link. Yellow cells have an issue with an Add button. Red cells are orphaned with no candidate, or have item mismatches. Ghost invoices show a Void button.'),
        'S1' => _('Recalculates sales_order_details.qty_sent from delivery note detail quantities.'),
        'S2' => _('Recalculates sales_order_details.invoiced from customer invoice detail quantities.'),
        'S3' => _('Customer invoice headers with no line items (ghost invoices). Use per-row Void on the SCHAIN tab.'),
        'S4' => _('Delivery notes whose order_ references a sales order that no longer exists. Use per-row fix actions on the SCHAIN tab.'),
        'S5' => _('Delivery notes with no matching stock_moves entries. Use per-row fix actions on the SCHAIN tab.'),
        'S6' => _('Sales orders with line items still awaiting delivery. These are healthy in-progress chains &#8212; no fix needed.'),
        'S7' => _('Delivery-note items where the delivered quantity has not yet been fully invoiced. Healthy in-progress chains &#8212; no fix needed.'),
        'S8' => _('Customer invoices with an outstanding unpaid balance (alloc < total). View the invoice in FA to record payment or allocate a credit note.'),
        'S10' => _('Diagnostic: correlates SO edits (audit_trail "Updated." entries) with qty_sent drift (S1) and invoiced drift (S2) in sales_order_details. Shows each SO, whether it was edited, and whether its qty_sent or invoiced differs from delivery note / customer invoice totals. Use this to assess whether the FA update_sales_order() bug is the root cause of S1 and S2 drift.'),
        'SCHAIN' => _('Consolidated sales chain view (replaces S3/S4/S5). Each row shows SO &#8594; Delivery &#8594; Stock Moves. Orange cells have a candidate match. Yellow cells have an Add button. Red cells are orphaned or missing stock moves. Ghost invoices show a Void button.'),
        'S9' => _('Sales documents that appear more than once in the database. The row count indicates how many copies exist for each document number. Review the duplicate rows in FA and manually delete or void the extras via phpMyAdmin or direct SQL &#8212; this tool cannot auto-fix duplicates.'),
        'P12' => _('Purchase documents that appear more than once in the database. The row count indicates how many copies exist for each document number. Review the duplicate rows in FA and manually delete or void the extras via phpMyAdmin or direct SQL &#8212; this tool cannot auto-fix duplicates.'),
        'P13' => _('Diagnostic: correlates PO edits (audit_trail "Updated." entries) with qty_invoiced drift in purch_order_details. Shows each PO, whether it was edited, and whether its invoiced qty differs from actual supplier invoice line totals. Use this to assess whether the FA update_po() reset-on-edit bug is the root cause of P2 drift.'),
        'A1' => _('Recalculates supp_trans.alloc from the sum of supp_allocations.'),
        'A2' => _('Recalculates debtor_trans.alloc from the sum of cust_allocations.'),
        'A3' => _('Runs both A1 (supplier) and A2 (customer) alloc recalculations. This resets alloc to the actual allocation total, correcting any over-allocated condition.'),
        'A4' => _('Deletes orphaned supp_allocations rows where the from- or to-transaction no longer exists. Then recalculates supplier alloc (A1).'),
        'A5' => _('Deletes orphaned cust_allocations rows where the from- or to-transaction no longer exists. Then recalculates customer alloc (A2).'),
    );
}
