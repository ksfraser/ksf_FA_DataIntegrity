<?php
/**
 * integrity_db.inc — Data integrity check queries for KSF FA DataIntegrity
 *
 * Each public function runs a SQL check and returns a db result set (or
 * an integer count when $count_only=true).  All queries use FA's standard
 * 0_ placeholder and db_query() / db_fetch() layer so they work
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

// Floating-point comparison tolerance
define('INTEG_DELTA', 0.005);

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
        FROM 0_grn_items g
        JOIN 0_grn_batch gb   ON g.grn_batch_id = gb.id
        JOIN 0_suppliers s    ON gb.supplier_id  = s.supplier_id
        LEFT JOIN (
            SELECT si.grn_item_id, SUM(si.quantity) AS qty_invoiced
            FROM 0_supp_invoice_items si
            JOIN 0_supp_trans st
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
    $sql = "
        UPDATE 0_grn_items g
        LEFT JOIN (
            SELECT si.grn_item_id, SUM(si.quantity) AS qty_invoiced
            FROM 0_supp_invoice_items si
            JOIN 0_supp_trans st
                ON st.trans_no = si.supp_trans_no
               AND st.type     = si.supp_trans_type
            WHERE si.supp_trans_type = 20
              AND st.ov_amount != 0
            GROUP BY si.grn_item_id
        ) inv_sum ON inv_sum.grn_item_id = g.id
        SET g.quantity_inv = COALESCE(inv_sum.qty_invoiced, 0)
        WHERE ABS(g.quantity_inv - COALESCE(inv_sum.qty_invoiced, 0)) > " . INTEG_DELTA;

    db_query($sql, 'Fix P1: Recalculate grn_items.quantity_inv');
    return db_num_affected_rows();
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
        FROM 0_purch_order_details pod
        JOIN 0_purch_orders po  ON pod.order_no = po.order_no
        JOIN 0_suppliers s      ON po.supplier_id = s.supplier_id
        LEFT JOIN (
            SELECT si.po_detail_item_id, SUM(si.quantity) AS qty_invoiced
            FROM 0_supp_invoice_items si
            JOIN 0_supp_trans st
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
    $sql = "
        UPDATE 0_purch_order_details pod
        LEFT JOIN (
            SELECT si.po_detail_item_id, SUM(si.quantity) AS qty_invoiced
            FROM 0_supp_invoice_items si
            JOIN 0_supp_trans st
                ON st.trans_no = si.supp_trans_no
               AND st.type     = si.supp_trans_type
            WHERE si.supp_trans_type = 20
              AND st.ov_amount != 0
            GROUP BY si.po_detail_item_id
        ) inv_sum ON inv_sum.po_detail_item_id = pod.po_detail_item
        SET pod.qty_invoiced = COALESCE(inv_sum.qty_invoiced, 0)
        WHERE ABS(pod.qty_invoiced - COALESCE(inv_sum.qty_invoiced, 0)) > " . INTEG_DELTA;

    db_query($sql, 'Fix P2: Recalculate pod.qty_invoiced');
    return db_num_affected_rows();
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
        FROM 0_purch_order_details pod
        JOIN 0_purch_orders po  ON pod.order_no = po.order_no
        JOIN 0_suppliers s      ON po.supplier_id = s.supplier_id
        LEFT JOIN (
            SELECT po_detail_item, SUM(qty_recd) AS qty_recd
            FROM 0_grn_items
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
    $sql = "
        UPDATE 0_purch_order_details pod
        LEFT JOIN (
            SELECT po_detail_item, SUM(qty_recd) AS qty_recd
            FROM 0_grn_items
            GROUP BY po_detail_item
        ) grn_sum ON grn_sum.po_detail_item = pod.po_detail_item
        SET pod.quantity_received = COALESCE(grn_sum.qty_recd, 0)
        WHERE ABS(pod.quantity_received - COALESCE(grn_sum.qty_recd, 0)) > " . INTEG_DELTA;

    db_query($sql, 'Fix P3: Recalculate pod.quantity_received');
    return db_num_affected_rows();
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
            g.quantity_inv  AS qty_invoiced,
            g.quantity_inv - g.qty_recd AS over_by
        FROM 0_grn_items g
        JOIN 0_grn_batch gb  ON g.grn_batch_id = gb.id
        JOIN 0_suppliers s   ON gb.supplier_id  = s.supplier_id
        WHERE g.quantity_inv > g.qty_recd + " . INTEG_DELTA . "
        ORDER BY s.supp_name, gb.delivery_date";

    return db_query($sql, 'Check P4: Over-invoiced GRN lines');
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
        FROM 0_supp_invoice_items si
        JOIN 0_supp_trans st
            ON st.trans_no = si.supp_trans_no
           AND st.type     = si.supp_trans_type
        JOIN 0_suppliers s ON st.supplier_id = s.supplier_id
        LEFT JOIN 0_grn_items g ON g.id = si.grn_item_id
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
        FROM 0_grn_batch gb
        JOIN 0_suppliers s   ON gb.supplier_id = s.supplier_id
        LEFT JOIN 0_purch_orders po ON po.order_no = gb.purch_order_no
        LEFT JOIN 0_grn_items gi    ON gi.grn_batch_id = gb.id
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
        FROM 0_supp_trans st
        JOIN 0_suppliers s ON st.supplier_id = s.supplier_id
        LEFT JOIN 0_supp_invoice_items si
            ON si.supp_trans_no   = st.trans_no
           AND si.supp_trans_type = st.type
        WHERE st.type = 20
          AND st.ov_amount != 0
          AND si.id IS NULL
        ORDER BY s.supp_name, st.tran_date";

    return db_query($sql, 'Check P7: Supplier invoices with no line items');
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
        FROM 0_sales_order_details sod
        JOIN 0_sales_orders so
            ON so.order_no   = sod.order_no
           AND so.trans_type = sod.trans_type
        JOIN 0_debtors_master dm ON dm.debtor_no = so.debtor_no
        LEFT JOIN (
            SELECT dtd.src_id, SUM(dtd.qty_done) AS qty_sent
            FROM 0_debtor_trans_details dtd
            JOIN 0_debtor_trans dt
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
    $sql = "
        UPDATE 0_sales_order_details sod
        LEFT JOIN (
            SELECT dtd.src_id, SUM(dtd.qty_done) AS qty_sent
            FROM 0_debtor_trans_details dtd
            JOIN 0_debtor_trans dt
                ON dt.trans_no = dtd.debtor_trans_no
               AND dt.type     = dtd.debtor_trans_type
            WHERE dtd.debtor_trans_type = 13
              AND dt.ov_amount != 0
            GROUP BY dtd.src_id
        ) del_sum ON del_sum.src_id = sod.id
        SET sod.qty_sent = COALESCE(del_sum.qty_sent, 0)
        WHERE ABS(sod.qty_sent - COALESCE(del_sum.qty_sent, 0)) > " . INTEG_DELTA;

    db_query($sql, 'Fix S1: Recalculate sod.qty_sent');
    return db_num_affected_rows();
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
        FROM 0_sales_order_details sod
        JOIN 0_sales_orders so
            ON so.order_no   = sod.order_no
           AND so.trans_type = sod.trans_type
        JOIN 0_debtors_master dm ON dm.debtor_no = so.debtor_no
        LEFT JOIN (
            SELECT dtd.src_id, SUM(dtd.quantity) AS qty_invoiced
            FROM 0_debtor_trans_details dtd
            JOIN 0_debtor_trans dt
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
    $sql = "
        UPDATE 0_sales_order_details sod
        LEFT JOIN (
            SELECT dtd.src_id, SUM(dtd.quantity) AS qty_invoiced
            FROM 0_debtor_trans_details dtd
            JOIN 0_debtor_trans dt
                ON dt.trans_no = dtd.debtor_trans_no
               AND dt.type     = dtd.debtor_trans_type
            WHERE dtd.debtor_trans_type = 10
              AND dt.ov_amount != 0
            GROUP BY dtd.src_id
        ) inv_sum ON inv_sum.src_id = sod.id
        SET sod.invoiced = COALESCE(inv_sum.qty_invoiced, 0)
        WHERE ABS(sod.invoiced - COALESCE(inv_sum.qty_invoiced, 0)) > " . INTEG_DELTA;

    db_query($sql, 'Fix S2: Recalculate sod.invoiced');
    return db_num_affected_rows();
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
        FROM 0_debtor_trans dt
        JOIN 0_debtors_master dm ON dm.debtor_no = dt.debtor_no
        LEFT JOIN 0_debtor_trans_details dtd
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
        FROM 0_debtor_trans dt
        JOIN 0_debtors_master dm ON dm.debtor_no = dt.debtor_no
        LEFT JOIN 0_sales_orders so
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
        FROM 0_debtor_trans dt
        JOIN 0_debtors_master dm ON dm.debtor_no = dt.debtor_no
        LEFT JOIN 0_stock_moves sm
            ON sm.trans_no = dt.trans_no
           AND sm.type     = dt.type
        WHERE dt.type  = 13
          AND dt.ov_amount != 0
          AND sm.trans_id IS NULL
        ORDER BY dm.name, dt.tran_date";

    return db_query($sql, 'Check S5: Deliveries with no stock moves');
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
        FROM 0_supp_trans st
        JOIN 0_suppliers s ON st.supplier_id = s.supplier_id
        LEFT JOIN (
            SELECT person_id,
                   CASE
                       WHEN trans_type_from = st2.type AND trans_no_from = st2.trans_no
                           THEN 'from'
                       ELSE 'to'
                   END AS direction,
                   SUM(amt) AS total_alloc
            FROM 0_supp_allocations sa2
            JOIN 0_supp_trans st2
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
        FROM 0_supp_trans st
        JOIN 0_suppliers s ON st.supplier_id = s.supplier_id
        LEFT JOIN (
            SELECT person_id, trans_no_to AS trans_no, trans_type_to AS type,
                   SUM(amt) AS total_alloc
            FROM 0_supp_allocations
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
    $sql = "
        UPDATE 0_supp_trans st
        LEFT JOIN (
            SELECT person_id, trans_no_to AS trans_no, trans_type_to AS type,
                   SUM(amt) AS total_alloc
            FROM 0_supp_allocations
            GROUP BY person_id, trans_no_to, trans_type_to
        ) sa_sum ON sa_sum.person_id = st.supplier_id
               AND sa_sum.trans_no   = st.trans_no
               AND sa_sum.type       = st.type
        SET st.alloc = COALESCE(sa_sum.total_alloc, 0)
        WHERE st.ov_amount != 0
          AND ABS(st.alloc - COALESCE(sa_sum.total_alloc, 0)) > " . INTEG_DELTA;

    db_query($sql, 'Fix A1: Recalculate supp_trans.alloc');
    return db_num_affected_rows();
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
        FROM 0_debtor_trans dt
        JOIN 0_debtors_master dm ON dm.debtor_no = dt.debtor_no
        LEFT JOIN (
            SELECT person_id, trans_no_to AS trans_no, trans_type_to AS type,
                   SUM(amt) AS total_alloc
            FROM 0_cust_allocations
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
    $sql = "
        UPDATE 0_debtor_trans dt
        LEFT JOIN (
            SELECT person_id, trans_no_to AS trans_no, trans_type_to AS type,
                   SUM(amt) AS total_alloc
            FROM 0_cust_allocations
            GROUP BY person_id, trans_no_to, trans_type_to
        ) ca_sum ON ca_sum.person_id = dt.debtor_no
               AND ca_sum.trans_no   = dt.trans_no
               AND ca_sum.type       = dt.type
        SET dt.alloc = COALESCE(ca_sum.total_alloc, 0)
        WHERE dt.ov_amount != 0
          AND ABS(dt.alloc - COALESCE(ca_sum.total_alloc, 0)) > " . INTEG_DELTA;

    db_query($sql, 'Fix A2: Recalculate debtor_trans.alloc');
    return db_num_affected_rows();
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
            FROM 0_supp_trans st
            JOIN 0_suppliers s ON st.supplier_id = s.supplier_id
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
            FROM 0_debtor_trans dt
            JOIN 0_debtors_master dm ON dm.debtor_no = dt.debtor_no
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
        FROM 0_supp_allocations sa
        LEFT JOIN 0_suppliers s
            ON s.supplier_id = sa.person_id
        LEFT JOIN 0_supp_trans st_from
            ON st_from.trans_no = sa.trans_no_from
           AND st_from.type     = sa.trans_type_from
           AND st_from.supplier_id = sa.person_id
        LEFT JOIN 0_supp_trans st_to
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
        FROM 0_cust_allocations ca
        LEFT JOIN 0_debtors_master dm
            ON dm.debtor_no = ca.person_id
        LEFT JOIN 0_debtor_trans dt_from
            ON dt_from.trans_no   = ca.trans_no_from
           AND dt_from.type       = ca.trans_type_from
           AND dt_from.debtor_no  = ca.person_id
        LEFT JOIN 0_debtor_trans dt_to
            ON dt_to.trans_no  = ca.trans_no_to
           AND dt_to.type      = ca.trans_type_to
           AND dt_to.debtor_no = ca.person_id
        WHERE dt_from.trans_no IS NULL OR dt_to.trans_no IS NULL
        ORDER BY dm.name, ca.date_alloc";

    return db_query($sql, 'Check A5: Orphaned cust_allocations');
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
            FROM 0_purch_orders po

            /* ---- Stage 1: any GRN? ---- */
            LEFT JOIN (
                SELECT DISTINCT purch_order_no AS order_no
                FROM 0_grn_batch
            ) grn_e ON grn_e.order_no = po.order_no

            /* ---- Stage 2: any non-voided invoice linked via pod? ---- */
            LEFT JOIN (
                SELECT DISTINCT pod.order_no
                FROM 0_purch_order_details pod
                JOIN 0_supp_invoice_items sii
                    ON sii.po_detail_item_id = pod.po_detail_item
                JOIN 0_supp_trans st
                    ON st.trans_no = sii.supp_trans_no
                   AND st.type = 20
                   AND st.ov_amount != 0
            ) inv_e ON inv_e.order_no = po.order_no

            /* ---- Stage 3: any allocation against an invoice on this PO? ---- */
            LEFT JOIN (
                SELECT DISTINCT pod2.order_no
                FROM 0_purch_order_details pod2
                JOIN 0_supp_invoice_items sii2
                    ON sii2.po_detail_item_id = pod2.po_detail_item
                JOIN 0_supp_trans inv2
                    ON inv2.trans_no = sii2.supp_trans_no
                   AND inv2.type = 20
                JOIN 0_supp_allocations sa
                    ON sa.trans_no_to   = inv2.trans_no
                   AND sa.trans_type_to = 20
            ) pay_e ON pay_e.order_no = po.order_no

            /* ---- Counter mismatch: P1 OR P2 OR P3 drift on this PO ---- */
            LEFT JOIN (
                SELECT DISTINCT bad.order_no
                FROM (
                    /* P1-style: grn_items.quantity_inv drift */
                    SELECT gb.purch_order_no AS order_no
                    FROM 0_grn_batch gb
                    JOIN 0_grn_items gi ON gi.grn_batch_id = gb.id
                    LEFT JOIN (
                        SELECT sii3.grn_item_id, SUM(sii3.quantity) AS qty_inv
                        FROM 0_supp_invoice_items sii3
                        JOIN 0_supp_trans st3
                            ON st3.trans_no = sii3.supp_trans_no
                           AND st3.type = 20
                           AND st3.ov_amount != 0
                        GROUP BY sii3.grn_item_id
                    ) inv_g ON inv_g.grn_item_id = gi.id
                    WHERE ABS(gi.quantity_inv - COALESCE(inv_g.qty_inv, 0)) > $d

                    UNION

                    /* P2-style: purch_order_details.qty_invoiced drift */
                    SELECT pod3.order_no
                    FROM 0_purch_order_details pod3
                    LEFT JOIN (
                        SELECT sii4.po_detail_item_id, SUM(sii4.quantity) AS qty_inv
                        FROM 0_supp_invoice_items sii4
                        JOIN 0_supp_trans st4
                            ON st4.trans_no = sii4.supp_trans_no
                           AND st4.type = 20
                           AND st4.ov_amount != 0
                        GROUP BY sii4.po_detail_item_id
                    ) inv_p ON inv_p.po_detail_item_id = pod3.po_detail_item
                    WHERE ABS(pod3.qty_invoiced - COALESCE(inv_p.qty_inv, 0)) > $d

                    UNION

                    /* P3-style: purch_order_details.quantity_received drift */
                    SELECT pod4.order_no
                    FROM 0_purch_order_details pod4
                    LEFT JOIN (
                        SELECT gi2.po_detail_item, SUM(gi2.qty_recd) AS qty_recv
                        FROM 0_grn_items gi2
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
            FROM 0_sales_orders so
            WHERE so.trans_type = 30

            /* ---- Stage 1: any delivery note? ---- */
            LEFT JOIN (
                SELECT DISTINCT order_ AS order_no
                FROM 0_debtor_trans
                WHERE type = 13
                  AND ov_amount != 0
            ) del_e ON del_e.order_no = so.order_no

            /* ---- Stage 2: any customer invoice? ---- */
            LEFT JOIN (
                SELECT DISTINCT order_ AS order_no
                FROM 0_debtor_trans
                WHERE type = 10
                  AND ov_amount != 0
            ) inv_e ON inv_e.order_no = so.order_no

            /* ---- Stage 3: any allocation against a customer invoice? ---- */
            LEFT JOIN (
                SELECT DISTINCT dt.order_ AS order_no
                FROM 0_debtor_trans dt
                JOIN 0_cust_allocations ca
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
                    FROM 0_sales_order_details sod
                    JOIN 0_sales_orders so2
                        ON so2.order_no   = sod.order_no
                       AND so2.trans_type = sod.trans_type
                    LEFT JOIN (
                        SELECT dtd.src_id, SUM(dtd.qty_done) AS qty_sent
                        FROM 0_debtor_trans_details dtd
                        JOIN 0_debtor_trans dt
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
                    FROM 0_sales_order_details sod2
                    JOIN 0_sales_orders so3
                        ON so3.order_no   = sod2.order_no
                       AND so3.trans_type = sod2.trans_type
                    LEFT JOIN (
                        SELECT dtd2.src_id, SUM(dtd2.quantity) AS qty_inv
                        FROM 0_debtor_trans_details dtd2
                        JOIN 0_debtor_trans dt2
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
        'S1' => 'check_sales_sod_qty_sent_mismatch',
        'S2' => 'check_sales_sod_invoiced_mismatch',
        'S3' => 'check_sales_ghost_invoices',
        'S4' => 'check_sales_orphaned_deliveries',
        'S5' => 'check_sales_delivery_missing_stock_moves',
        'A1' => 'check_alloc_supplier_drift',
        'A2' => 'check_alloc_customer_drift',
        'A3' => 'check_alloc_over_allocated',
        'A4' => 'check_alloc_orphaned_supplier_allocs',
        'A5' => 'check_alloc_orphaned_customer_allocs',
    );

    foreach ($checks as $id => $func) {
        $result = call_user_func($func);
        if ($result) {
            $counts[$id] = db_num_rows($result);
            db_free_result($result);
        } else {
            $counts[$id] = 0;
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
        'P4' => _('GRN lines over-invoiced'),
        'P5' => _('Orphaned supplier invoice items (missing GRN)'),
        'P6' => _('Orphaned GRN batches (missing purchase order)'),
        'P7' => _('Ghost supplier invoices (no line items)'),
        'S1' => _('Sales order line qty_sent drift'),
        'S2' => _('Sales order line invoiced qty drift'),
        'S3' => _('Ghost customer invoices (no line items)'),
        'S4' => _('Orphaned delivery notes (missing sales order)'),
        'S5' => _('Delivery notes with no stock moves'),
        'A1' => _('Supplier alloc drift (alloc vs supp_allocations)'),
        'A2' => _('Customer alloc drift (alloc vs cust_allocations)'),
        'A3' => _('Over-allocated transactions'),
        'A4' => _('Orphaned supplier allocation records'),
        'A5' => _('Orphaned customer allocation records'),
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
        'P4' => null,   // over-invoiced: requires credit note, no auto-fix
        'P5' => null,   // orphaned invoice items: manual investigation
        'P6' => null,   // orphaned GRN batches: manual investigation
        'P7' => null,   // ghost invoices: manual void
        'S1' => 'fix_sales_sod_qty_sent',
        'S2' => 'fix_sales_sod_invoiced',
        'S3' => null,   // ghost invoices: manual void
        'S4' => null,   // orphaned deliveries: manual investigation
        'S5' => null,   // missing stock moves: requires manual GL correction
        'A1' => 'fix_alloc_supplier_drift',
        'A2' => 'fix_alloc_customer_drift',
        'A3' => null,   // over-allocated: requires re-allocation
        'A4' => null,   // orphaned alloc rows: manual delete after verification
        'A5' => null,   // orphaned alloc rows: manual delete after verification
    );
}
