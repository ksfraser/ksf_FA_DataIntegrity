<?php
/**
 * integrity_ui.inc — UI helpers for KSF FA DataIntegrity
 *
 * Provides view/edit link builders, per-check result table renderers, fix
 * action buttons, and the fix-POST handler.  All output uses FA's own HTML
 * helpers (start_table, table_header, label_cell, alt_table_row_color, etc.)
 * so theming and CSS class conventions remain consistent.
 *
 * PHP 5.6+ compatible (no short arrays, no null-coalescing, no typed props).
 *
 * @package  Ksfraser\FA\DataIntegrity
 * @since    1.0.0
 */

// FA transaction type constants (duplicated locally so this file is self-contained)
if (!defined('INTEG_ST_SALESINVOICE'))  define('INTEG_ST_SALESINVOICE',  10);
if (!defined('INTEG_ST_CUSTCREDIT'))    define('INTEG_ST_CUSTCREDIT',    11);
if (!defined('INTEG_ST_CUSTPAYMENT'))   define('INTEG_ST_CUSTPAYMENT',   12);
if (!defined('INTEG_ST_CUSTDELIVERY'))  define('INTEG_ST_CUSTDELIVERY',  13);
if (!defined('INTEG_ST_PURCHORDER'))    define('INTEG_ST_PURCHORDER',    18);
if (!defined('INTEG_ST_SUPPINVOICE'))   define('INTEG_ST_SUPPINVOICE',   20);
if (!defined('INTEG_ST_SUPPCREDIT'))    define('INTEG_ST_SUPPCREDIT',    21);
if (!defined('INTEG_ST_SUPPAYMENT'))    define('INTEG_ST_SUPPAYMENT',    22);
if (!defined('INTEG_ST_SUPPRECEIVE'))   define('INTEG_ST_SUPPRECEIVE',   25);
if (!defined('INTEG_ST_SALESORDER'))    define('INTEG_ST_SALESORDER',    30);

// ---------------------------------------------------------------------------
// Link helpers
// ---------------------------------------------------------------------------

/**
 * Return the short human-readable name for a FA transaction type integer.
 *
 * @param  int    $type  FA transaction type constant
 * @return string
 * @since  1.0.0
 */
function integ_type_name($type)
{
    $names = array(
        10 => _('Cust Invoice'),
        11 => _('Cust Credit'),
        12 => _('Cust Payment'),
        13 => _('Delivery'),
        18 => _('Purch Order'),
        20 => _('Supp Invoice'),
        21 => _('Supp Credit'),
        22 => _('Supp Payment'),
        25 => _('GRN'),
        30 => _('Sales Order'),
    );
    return isset($names[$type]) ? $names[$type] : _('Type ') . (int)$type;
}

/**
 * Return an HTML popup-view anchor for a FA transaction, or an empty string
 * if get_trans_view_str() cannot build one for this type.
 *
 * @param  int    $type      FA transaction type
 * @param  int    $trans_no  Transaction number
 * @param  string $label     Link text (defaults to the type name)
 * @return string  HTML anchor or plain label
 * @since  1.0.0
 */
function integ_view_link($type, $trans_no, $label = '')
{
    if ($label === '') {
        $label = '#' . (int)$trans_no;
    }
    $link = get_trans_view_str((int)$type, (int)$trans_no, $label);
    return ($link !== null && $link !== '') ? $link : htmlspecialchars($label);
}

/**
 * Return an HTML inline-edit anchor for a FA transaction, or '' if no edit
 * URL is defined for that type.
 *
 * @param  int    $type      FA transaction type
 * @param  int    $trans_no  Transaction number
 * @param  string $label     Link text (defaults to 'Edit')
 * @return string  HTML anchor or ''
 * @since  1.0.0
 */
function integ_edit_link($type, $trans_no, $label = '')
{
    global $path_to_root;

    if ($label === '') {
        $label = _('Edit');
    }

    $url = '';
    switch ((int)$type) {
        case INTEG_ST_PURCHORDER:
            $url = $path_to_root . '/purchasing/po_entry_items.php?ModifyOrderNumber=' . (int)$trans_no;
            break;
        case INTEG_ST_SUPPINVOICE:
            $url = $path_to_root . '/purchasing/supplier_invoice.php?ModifyInvoice=' . (int)$trans_no;
            break;
        case INTEG_ST_SUPPCREDIT:
            $url = $path_to_root . '/purchasing/supplier_credit.php?ModifyCreditNote=' . (int)$trans_no;
            break;
        case INTEG_ST_SALESORDER:
            $url = $path_to_root . '/sales/sales_order_entry.php?ModifyOrderNumber=' . (int)$trans_no;
            break;
        case INTEG_ST_SALESINVOICE:
            $url = $path_to_root . '/sales/customer_invoice.php?ModifyInvoice=' . (int)$trans_no;
            break;
        case INTEG_ST_CUSTDELIVERY:
            $url = $path_to_root . '/sales/customer_delivery.php?ModifyDelivery=' . (int)$trans_no;
            break;
        default:
            return '';
    }
    return '<a href="' . htmlspecialchars($url) . '">' . htmlspecialchars($label) . '</a>';
}

/**
 * Echo a "Run Fix" submit button that posts _fix_check=<check_id> to the
 * current page.  Only renders if a fix function is registered for the check.
 *
 * @param  string $check_id  e.g. 'P1'
 * @return void
 * @since  1.0.0
 */
function integ_fix_button($check_id)
{
    $fix_funcs = get_fix_functions();
    if (empty($fix_funcs[$check_id])) {
        return;
    }
    echo "<br>\n";
    echo "<input type='hidden' name='_fix_check' value='" . htmlspecialchars($check_id) . "'>\n";
    submit('_do_fix', _('Recalculate / Fix'), true,
        _('Recalculate counter values from actual transaction detail rows.  Safe to run multiple times.'),
        false);
}

/**
 * Inspect $_POST for a pending fix action and execute it if the user has
 * the SA_DATAINTEGRITY_FIX privilege.
 *
 * Must be called before any HTML output (before page()).
 *
 * @return array|null  array('check_id'=>string, 'rows_fixed'=>int) or null
 * @since  1.0.0
 */
function integ_handle_fix_post()
{
    if (!isset($_POST['_do_fix']) || !isset($_POST['_fix_check'])) {
        return null;
    }
    if (!check_has_access(SA_DATAINTEGRITY_FIX)) {
        return null;
    }

    $check_id  = $_POST['_fix_check'];
    $fix_funcs = get_fix_functions();

    if (empty($fix_funcs[$check_id])) {
        return null;
    }

    $func  = $fix_funcs[$check_id];
    $fixed = call_user_func($func);

    return array('check_id' => $check_id, 'rows_fixed' => (int)$fixed);
}

// ---------------------------------------------------------------------------
// Generic table helpers
// ---------------------------------------------------------------------------

/**
 * Echo a standard result-count badge with green (0) / red (>0) colour.
 *
 * @param  int    $count
 * @param  string $zero_class   CSS class when count is zero
 * @param  string $nonzero_class CSS class when count is non-zero
 * @return void
 * @since  1.0.0
 */
function integ_count_badge($count, $zero_class = 'integ-ok', $nonzero_class = 'integ-fail')
{
    $class = ((int)$count === 0) ? $zero_class : $nonzero_class;
    echo '<span class="' . $class . '">' . (int)$count . '</span>';
}

/**
 * Echo a "No issues found" table row spanning all columns.
 *
 * @param  int $colspan
 * @return void
 * @since  1.0.0
 */
function integ_no_issues_row($colspan)
{
    echo "<tr><td colspan='" . (int)$colspan . "' style='text-align:center;color:green;padding:6px'>"
        . _('No issues found.') . "</td></tr>\n";
}

/**
 * Dispatch to the appropriate per-check render function and, if a fix
 * function is registered, echo the fix button below the table.
 *
 * @param  string   $check_id  e.g. 'P1'
 * @param  resource $result    DB result from a check_* function (may be null)
 * @return void
 * @since  1.0.0
 */
function integ_render_check_result($check_id, $result)
{
    $dispatch = array(
        'P1' => 'integ_render_p1',
        'P2' => 'integ_render_p2',
        'P3' => 'integ_render_p3',
        'P4' => 'integ_render_p4',
        'P5' => 'integ_render_p5',
        'P6' => 'integ_render_p6',
        'P7' => 'integ_render_p7',
        'S1' => 'integ_render_s1',
        'S2' => 'integ_render_s2',
        'S3' => 'integ_render_s3',
        'S4' => 'integ_render_s4',
        'S5' => 'integ_render_s5',
        'A1' => 'integ_render_a1',
        'A2' => 'integ_render_a2',
        'A3' => 'integ_render_a3',
        'A4' => 'integ_render_a4',
        'A5' => 'integ_render_a5',
    );

    if (!isset($dispatch[$check_id])) {
        display_error(_('Unknown check ID: ') . htmlspecialchars($check_id));
        return;
    }

    call_user_func($dispatch[$check_id], $result);
    integ_fix_button($check_id);
}

// ---------------------------------------------------------------------------
// Pipeline / funnel summary renderer
// ---------------------------------------------------------------------------

/**
 * Render a pipeline health summary table for one chain (purchase or sales).
 *
 * The $rows parameter is an ordered array of display rows, each describing
 * one pipeline stage bucket.  Each element is an associative array with:
 *   'key'     — key into $data (from get_purchase/sales_pipeline_counts())
 *   'label'   — human-readable stage description
 *   'style'   — 'good' | 'warn' | 'bad' | 'normal' | 'anomalous'
 *   'note'    — optional short tooltip / parenthetical
 *
 * Percentage columns are computed relative to $data['total'].
 *
 * @param  array  $data   Return value from get_purchase_pipeline_counts() etc.
 * @param  string $title  Section heading text
 * @param  array  $rows   Row descriptor array (see above)
 * @return void
 * @since  1.0.0
 */
function integ_render_pipeline_summary($data, $title, $rows)
{
    $total = (int)$data['total'];

    echo "<h3 style='margin-top:14px;margin-bottom:4px'>" . htmlspecialchars($title) . "</h3>\n";

    // colour mapping
    $colours = array(
        'good'      => '#060',   // dark green
        'warn'      => '#996600',// amber
        'bad'       => '#c00',   // red
        'normal'    => '#333',   // neutral (open / in-progress)
        'anomalous' => '#800080',// purple — should never happen
    );

    start_table(TABLESTYLE, "width='70%'");
    table_header(array(_('Pipeline Stage'), _('Count'), _('% of Total'), _('Note')));

    $k = 0;
    foreach ($rows as $row) {
        $count  = isset($data[$row['key']]) ? (int)$data[$row['key']] : 0;
        $pct    = $total > 0 ? round($count / $total * 100, 1) : 0;
        $colour = isset($colours[$row['style']]) ? $colours[$row['style']] : '#333';
        $note   = isset($row['note']) ? htmlspecialchars($row['note']) : '';

        alt_table_row_color($k);
        label_cell(htmlspecialchars($row['label']));
        label_cell(
            '<span style="font-weight:bold;color:' . $colour . '">' . $count . '</span>'
        );
        label_cell($pct . '%');
        label_cell('<em style="color:#666;font-size:0.95em">' . $note . '</em>');
        end_row();
    }

    // Totals row
    start_row("style='border-top:2px solid #999;font-weight:bold'");
    label_cell(_('TOTAL'));
    label_cell($total);
    label_cell('100%');
    label_cell('');
    end_row();

    end_table(1);
}

/**
 * Build the purchase chain pipeline row descriptors for
 * integ_render_pipeline_summary().
 *
 * @return array
 * @since  1.0.0
 */
function integ_purchase_pipeline_rows()
{
    return array(
        array(
            'key'   => 'complete_clean',
            'label' => _('Complete & clean (PO → GRN → Invoice → Payment, counters agree)'),
            'style' => 'good',
            'note'  => _('All stages present, all counter values verified'),
        ),
        array(
            'key'   => 'complete_corrupted',
            'label' => _('Complete with data issues (all stages present, counter drift detected)'),
            'style' => 'bad',
            'note'  => _('Run fixes on P1/P2/P3 tabs to recalculate counters'),
        ),
        array(
            'key'   => 'invoiced_not_paid_clean',
            'label' => _('Invoiced, awaiting payment (counters OK)'),
            'style' => 'normal',
            'note'  => _('Normal in-progress state'),
        ),
        array(
            'key'   => 'invoiced_not_paid_corrupted',
            'label' => _('Invoiced, awaiting payment (counter drift detected)'),
            'style' => 'bad',
            'note'  => _('Run P1/P2/P3 fixes; may cause duplicate display on invoice entry screen'),
        ),
        array(
            'key'   => 'received_not_invoiced',
            'label' => _('GRN received, not yet invoiced'),
            'style' => 'warn',
            'note'  => _('May include Direct Invoice timeout failures — check P1 tab'),
        ),
        array(
            'key'   => 'open_po',
            'label' => _('Open PO (not yet received)'),
            'style' => 'normal',
            'note'  => _('Normal outstanding purchase orders'),
        ),
        array(
            'key'   => 'anomalous',
            'label' => _('Anomalous (payment exists but no invoice traced)'),
            'style' => 'anomalous',
            'note'  => _('Should be 0 — investigate immediately'),
        ),
    );
}

/**
 * Build the sales chain pipeline row descriptors for
 * integ_render_pipeline_summary().
 *
 * @return array
 * @since  1.0.0
 */
function integ_sales_pipeline_rows()
{
    return array(
        array(
            'key'   => 'complete_clean',
            'label' => _('Complete & clean (SO → Delivery → Invoice → Payment, counters agree)'),
            'style' => 'good',
            'note'  => _('All stages present, all counter values verified'),
        ),
        array(
            'key'   => 'complete_corrupted',
            'label' => _('Complete with data issues (all stages present, counter drift detected)'),
            'style' => 'bad',
            'note'  => _('Run fixes on S1/S2 tabs to recalculate counters'),
        ),
        array(
            'key'   => 'invoiced_not_paid_clean',
            'label' => _('Invoiced, awaiting payment (counters OK)'),
            'style' => 'normal',
            'note'  => _('Normal in-progress state'),
        ),
        array(
            'key'   => 'invoiced_not_paid_corrupted',
            'label' => _('Invoiced, awaiting payment (counter drift detected)'),
            'style' => 'bad',
            'note'  => _('Run S1/S2 fixes'),
        ),
        array(
            'key'   => 'delivered_not_invoiced',
            'label' => _('Delivered, not yet invoiced'),
            'style' => 'warn',
            'note'  => _('Normal if delivery precedes invoicing; investigate if old'),
        ),
        array(
            'key'   => 'open_order',
            'label' => _('Open sales order (not yet delivered)'),
            'style' => 'normal',
            'note'  => _('Normal outstanding sales orders'),
        ),
        array(
            'key'   => 'anomalous',
            'label' => _('Anomalous (payment exists but no invoice traced)'),
            'style' => 'anomalous',
            'note'  => _('Should be 0 — investigate immediately'),
        ),
    );
}

// ---------------------------------------------------------------------------
// Per-check render functions — PURCHASE
// ---------------------------------------------------------------------------

/**
 * Render P1 — grn_items.quantity_inv drift vs actual supp_invoice_items.
 *
 * @param  resource $result  DB result from check_purchase_grn_qty_inv_mismatch()
 * @return void
 * @since  1.0.0
 */
function integ_render_p1($result)
{
    $cols = array(
        _('GRN Batch'), _('Date'), _('Supplier'), _('Item Code'), _('Description'),
        _('Qty Recv\'d'), _('qty_inv stored'), _('qty_inv actual'), _('Discrepancy'),
        _('View GRN'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id'], '#' . $row['grn_batch_id']));
        label_cell($row['delivery_date']);
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(htmlspecialchars($row['item_code']));
        label_cell(htmlspecialchars($row['description']));
        label_cell(number_format($row['qty_recd'], 2));
        label_cell(number_format($row['qty_inv_stored'], 2));
        label_cell(number_format($row['qty_inv_actual'], 2));
        $disc = $row['discrepancy'];
        label_cell('<span style="color:' . ($disc == 0 ? 'green' : 'red') . '">'
            . number_format($disc, 4) . '</span>');
        label_cell(integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render P2 — purch_order_details.qty_invoiced drift.
 *
 * @param  resource $result  DB result from check_purchase_pod_qty_invoiced_mismatch()
 * @return void
 * @since  1.0.0
 */
function integ_render_p2($result)
{
    $cols = array(
        _('PO #'), _('PO Ref'), _('Supplier'), _('Item Code'),
        _('Qty Ordered'), _('Qty Recv\'d'),
        _('qty_inv stored'), _('qty_inv actual'), _('Discrepancy'),
        _('View PO'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell((int)$row['order_no']);
        label_cell(htmlspecialchars($row['po_ref']));
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(htmlspecialchars($row['item_code']));
        label_cell(number_format($row['quantity_ordered'], 2));
        label_cell(number_format($row['quantity_received'], 2));
        label_cell(number_format($row['qty_inv_stored'], 2));
        label_cell(number_format($row['qty_inv_actual'], 2));
        $disc = $row['discrepancy'];
        label_cell('<span style="color:' . ($disc == 0 ? 'green' : 'red') . '">'
            . number_format($disc, 4) . '</span>');
        label_cell(integ_view_link(INTEG_ST_PURCHORDER, $row['order_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render P3 — purch_order_details.quantity_received drift vs GRN items.
 *
 * @param  resource $result  DB result from check_purchase_pod_qty_received_mismatch()
 * @return void
 * @since  1.0.0
 */
function integ_render_p3($result)
{
    $cols = array(
        _('PO #'), _('PO Ref'), _('Supplier'), _('Item Code'),
        _('Qty Ordered'),
        _('qty_recv stored'), _('qty_recv actual'), _('Discrepancy'),
        _('View PO'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell((int)$row['order_no']);
        label_cell(htmlspecialchars($row['po_ref']));
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(htmlspecialchars($row['item_code']));
        label_cell(number_format($row['quantity_ordered'], 2));
        label_cell(number_format($row['qty_recv_stored'], 2));
        label_cell(number_format($row['qty_recv_actual'], 2));
        $disc = $row['discrepancy'];
        label_cell('<span style="color:' . ($disc == 0 ? 'green' : 'red') . '">'
            . number_format($disc, 4) . '</span>');
        label_cell(integ_view_link(INTEG_ST_PURCHORDER, $row['order_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render P4 — GRN lines where quantity_inv > qty_recd (over-invoiced).
 * No auto-fix available; view link provided.
 *
 * @param  resource $result  DB result from check_purchase_grn_over_invoiced()
 * @return void
 * @since  1.0.0
 */
function integ_render_p4($result)
{
    $cols = array(
        _('GRN Batch'), _('Date'), _('Supplier'), _('Item Code'), _('Description'),
        _('Qty Recv\'d'), _('Qty Invoiced'), _('Over By'),
        _('View GRN'), _('View PO'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id'], '#' . $row['grn_batch_id']));
        label_cell($row['delivery_date']);
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(htmlspecialchars($row['item_code']));
        label_cell(htmlspecialchars($row['description']));
        label_cell(number_format($row['qty_recd'], 2));
        label_cell(number_format($row['qty_invoiced'], 2));
        label_cell('<span style="color:red">' . number_format($row['over_by'], 4) . '</span>');
        label_cell(integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id']));
        label_cell(integ_view_link(INTEG_ST_PURCHORDER, $row['purch_order_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render P5 — supp_invoice_items with no matching grn_items row.
 *
 * @param  resource $result  DB result from check_purchase_orphaned_invoice_items()
 * @return void
 * @since  1.0.0
 */
function integ_render_p5($result)
{
    $cols = array(
        _('Invoice Item ID'), _('Invoice #'), _('Date'), _('Supplier'),
        _('Item Code'), _('Description'), _('Qty'), _('Unit Price'),
        _('Missing GRN ID'), _('View Invoice'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell((int)$row['invoice_item_id']);
        label_cell(integ_view_link(INTEG_ST_SUPPINVOICE, $row['supp_trans_no'], '#' . $row['supp_trans_no']));
        label_cell($row['tran_date']);
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(htmlspecialchars($row['stock_id']));
        label_cell(htmlspecialchars($row['description']));
        label_cell(number_format($row['quantity'], 2));
        label_cell(number_format($row['unit_price'], 4));
        label_cell('<span style="color:red">' . (int)$row['grn_item_id'] . ' (' . _('missing') . ')</span>');
        label_cell(integ_view_link(INTEG_ST_SUPPINVOICE, $row['supp_trans_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render P6 — grn_batch rows whose parent purch_order_no is missing.
 *
 * @param  resource $result  DB result from check_purchase_orphaned_grn_batches()
 * @return void
 * @since  1.0.0
 */
function integ_render_p6($result)
{
    $cols = array(
        _('GRN Batch ID'), _('Date'), _('Reference'), _('Supplier'),
        _('Missing PO #'), _('Lines'), _('Total Qty Recv\'d'),
        _('View GRN'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id'], '#' . $row['grn_batch_id']));
        label_cell($row['delivery_date']);
        label_cell(htmlspecialchars($row['grn_ref']));
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell('<span style="color:red">' . (int)$row['purch_order_no'] . ' (' . _('missing') . ')</span>');
        label_cell((int)$row['line_count']);
        label_cell(number_format($row['total_qty_recd'], 2));
        label_cell(integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render P7 — Supplier invoice headers with no supp_invoice_items lines.
 *
 * @param  resource $result  DB result from check_purchase_ghost_invoices()
 * @return void
 * @since  1.0.0
 */
function integ_render_p7($result)
{
    $cols = array(
        _('Invoice #'), _('Date'), _('Supplier Ref'), _('Reference'),
        _('Supplier'), _('Total'), _('Allocated'),
        _('View'), _('Edit'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_SUPPINVOICE, $row['trans_no'], '#' . $row['trans_no']));
        label_cell($row['tran_date']);
        label_cell(htmlspecialchars($row['supp_reference']));
        label_cell(htmlspecialchars($row['reference']));
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(number_format($row['total'], 2));
        label_cell(number_format($row['alloc'], 2));
        label_cell(integ_view_link(INTEG_ST_SUPPINVOICE, $row['trans_no']));
        label_cell(integ_edit_link(INTEG_ST_SUPPINVOICE, $row['trans_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

// ---------------------------------------------------------------------------
// Per-check render functions — SALES
// ---------------------------------------------------------------------------

/**
 * Render S1 — sales_order_details.qty_sent drift vs delivery note details.
 *
 * @param  resource $result  DB result from check_sales_sod_qty_sent_mismatch()
 * @return void
 * @since  1.0.0
 */
function integ_render_s1($result)
{
    $cols = array(
        _('Order #'), _('Order Ref'), _('Customer'), _('Item Code'), _('Description'),
        _('Qty Ordered'), _('qty_sent stored'), _('qty_sent actual'), _('Discrepancy'),
        _('View Order'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell((int)$row['order_no']);
        label_cell(htmlspecialchars($row['so_ref']));
        label_cell(htmlspecialchars($row['customer_name']));
        label_cell(htmlspecialchars($row['stk_code']));
        label_cell(htmlspecialchars($row['description']));
        label_cell(number_format($row['qty_ordered'], 2));
        label_cell(number_format($row['qty_sent_stored'], 2));
        label_cell(number_format($row['qty_sent_actual'], 2));
        $disc = $row['discrepancy'];
        label_cell('<span style="color:' . ($disc == 0 ? 'green' : 'red') . '">'
            . number_format($disc, 4) . '</span>');
        label_cell(integ_view_link(INTEG_ST_SALESORDER, $row['order_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render S2 — sales_order_details.invoiced drift vs customer invoice details.
 *
 * @param  resource $result  DB result from check_sales_sod_invoiced_mismatch()
 * @return void
 * @since  1.0.0
 */
function integ_render_s2($result)
{
    $cols = array(
        _('Order #'), _('Order Ref'), _('Customer'), _('Item Code'), _('Description'),
        _('Qty Ordered'), _('invoiced stored'), _('invoiced actual'), _('Discrepancy'),
        _('View Order'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell((int)$row['order_no']);
        label_cell(htmlspecialchars($row['so_ref']));
        label_cell(htmlspecialchars($row['customer_name']));
        label_cell(htmlspecialchars($row['stk_code']));
        label_cell(htmlspecialchars($row['description']));
        label_cell(number_format($row['qty_ordered'], 2));
        label_cell(number_format($row['qty_inv_stored'], 2));
        label_cell(number_format($row['qty_inv_actual'], 2));
        $disc = $row['discrepancy'];
        label_cell('<span style="color:' . ($disc == 0 ? 'green' : 'red') . '">'
            . number_format($disc, 4) . '</span>');
        label_cell(integ_view_link(INTEG_ST_SALESORDER, $row['order_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render S3 — Customer invoices with no debtor_trans_details lines.
 *
 * @param  resource $result  DB result from check_sales_ghost_invoices()
 * @return void
 * @since  1.0.0
 */
function integ_render_s3($result)
{
    $cols = array(
        _('Invoice #'), _('Date'), _('Reference'), _('Customer'),
        _('Total'), _('Allocated'),
        _('View'), _('Edit'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_SALESINVOICE, $row['trans_no'], '#' . $row['trans_no']));
        label_cell($row['tran_date']);
        label_cell(htmlspecialchars($row['reference']));
        label_cell(htmlspecialchars($row['customer_name']));
        label_cell(number_format($row['total'], 2));
        label_cell(number_format($row['alloc'], 2));
        label_cell(integ_view_link(INTEG_ST_SALESINVOICE, $row['trans_no']));
        label_cell(integ_edit_link(INTEG_ST_SALESINVOICE, $row['trans_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render S4 — Delivery notes whose parent sales order is missing.
 *
 * @param  resource $result  DB result from check_sales_orphaned_deliveries()
 * @return void
 * @since  1.0.0
 */
function integ_render_s4($result)
{
    $cols = array(
        _('Delivery #'), _('Date'), _('Reference'), _('Customer'),
        _('Missing SO #'), _('Total'),
        _('View Delivery'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_CUSTDELIVERY, $row['trans_no'], '#' . $row['trans_no']));
        label_cell($row['tran_date']);
        label_cell(htmlspecialchars($row['reference']));
        label_cell(htmlspecialchars($row['customer_name']));
        label_cell('<span style="color:red">' . (int)$row['sales_order_no'] . ' (' . _('missing') . ')</span>');
        label_cell(number_format($row['total'], 2));
        label_cell(integ_view_link(INTEG_ST_CUSTDELIVERY, $row['trans_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render S5 — Delivery notes with no stock_moves entries.
 *
 * @param  resource $result  DB result from check_sales_delivery_missing_stock_moves()
 * @return void
 * @since  1.0.0
 */
function integ_render_s5($result)
{
    $cols = array(
        _('Delivery #'), _('Date'), _('Reference'), _('Customer'), _('Total'),
        _('View Delivery'), _('Edit Delivery'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_CUSTDELIVERY, $row['trans_no'], '#' . $row['trans_no']));
        label_cell($row['tran_date']);
        label_cell(htmlspecialchars($row['reference']));
        label_cell(htmlspecialchars($row['customer_name']));
        label_cell(number_format($row['total'], 2));
        label_cell(integ_view_link(INTEG_ST_CUSTDELIVERY, $row['trans_no']));
        label_cell(integ_edit_link(INTEG_ST_CUSTDELIVERY, $row['trans_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

// ---------------------------------------------------------------------------
// Per-check render functions — ALLOCATIONS
// ---------------------------------------------------------------------------

/**
 * Render A1 — supp_trans.alloc drift vs supp_allocations sum.
 *
 * @param  resource $result  DB result from check_alloc_supplier_drift()
 * @return void
 * @since  1.0.0
 */
function integ_render_a1($result)
{
    $cols = array(
        _('Trans #'), _('Type'), _('Date'), _('Supp Ref'), _('Reference'),
        _('Supplier'), _('Total'), _('alloc stored'), _('alloc actual'), _('Discrepancy'),
        _('View'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell(integ_view_link($row['type'], $row['trans_no'], '#' . $row['trans_no']));
        label_cell(integ_type_name($row['type']));
        label_cell($row['tran_date']);
        label_cell(htmlspecialchars($row['supp_reference']));
        label_cell(htmlspecialchars($row['reference']));
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(number_format($row['total_amount'], 2));
        label_cell(number_format($row['alloc_stored'], 2));
        label_cell(number_format($row['alloc_actual'], 2));
        $disc = $row['discrepancy'];
        label_cell('<span style="color:' . ($disc == 0 ? 'green' : 'red') . '">'
            . number_format($disc, 4) . '</span>');
        label_cell(integ_view_link($row['type'], $row['trans_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render A2 — debtor_trans.alloc drift vs cust_allocations sum.
 *
 * @param  resource $result  DB result from check_alloc_customer_drift()
 * @return void
 * @since  1.0.0
 */
function integ_render_a2($result)
{
    $cols = array(
        _('Trans #'), _('Type'), _('Date'), _('Reference'),
        _('Customer'), _('Total'), _('alloc stored'), _('alloc actual'), _('Discrepancy'),
        _('View'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell(integ_view_link($row['type'], $row['trans_no'], '#' . $row['trans_no']));
        label_cell(integ_type_name($row['type']));
        label_cell($row['tran_date']);
        label_cell(htmlspecialchars($row['reference']));
        label_cell(htmlspecialchars($row['customer_name']));
        label_cell(number_format($row['total_amount'], 2));
        label_cell(number_format($row['alloc_stored'], 2));
        label_cell(number_format($row['alloc_actual'], 2));
        $disc = $row['discrepancy'];
        label_cell('<span style="color:' . ($disc == 0 ? 'green' : 'red') . '">'
            . number_format($disc, 4) . '</span>');
        label_cell(integ_view_link($row['type'], $row['trans_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render A3 — Transactions where alloc exceeds the absolute transaction amount.
 *
 * @param  resource $result  DB result from check_alloc_over_allocated()
 * @return void
 * @since  1.0.0
 */
function integ_render_a3($result)
{
    $cols = array(
        _('Side'), _('Trans #'), _('Type'), _('Date'), _('Reference'),
        _('Person'), _('Total'), _('alloc'), _('Over By'),
        _('View'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell(htmlspecialchars($row['side']));
        label_cell(integ_view_link($row['type'], $row['trans_no'], '#' . $row['trans_no']));
        label_cell(integ_type_name($row['type']));
        label_cell($row['tran_date']);
        label_cell(htmlspecialchars($row['reference']));
        label_cell(htmlspecialchars($row['person_name']));
        label_cell(number_format($row['total_amount'], 2));
        label_cell(number_format($row['alloc'], 2));
        label_cell('<span style="color:red">' . number_format($row['over_by'], 4) . '</span>');
        label_cell(integ_view_link($row['type'], $row['trans_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render A4 — Orphaned supp_allocations rows.
 *
 * @param  resource $result  DB result from check_alloc_orphaned_supplier_allocs()
 * @return void
 * @since  1.0.0
 */
function integ_render_a4($result)
{
    $cols = array(
        _('Alloc ID'), _('Supplier'), _('Date'), _('Amount'),
        _('From Type'), _('From #'), _('From Missing?'),
        _('To Type'), _('To #'), _('To Missing?'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell((int)$row['id']);
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell($row['date_alloc']);
        label_cell(number_format($row['amt'], 2));
        label_cell(integ_type_name($row['trans_type_from']));
        label_cell(integ_view_link($row['trans_type_from'], $row['trans_no_from'], '#' . $row['trans_no_from']));
        label_cell('<span style="color:' . ($row['from_missing'] === 'YES' ? 'red' : 'green') . '">'
            . htmlspecialchars($row['from_missing']) . '</span>');
        label_cell(integ_type_name($row['trans_type_to']));
        label_cell(integ_view_link($row['trans_type_to'], $row['trans_no_to'], '#' . $row['trans_no_to']));
        label_cell('<span style="color:' . ($row['to_missing'] === 'YES' ? 'red' : 'green') . '">'
            . htmlspecialchars($row['to_missing']) . '</span>');
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render A5 — Orphaned cust_allocations rows.
 *
 * @param  resource $result  DB result from check_alloc_orphaned_customer_allocs()
 * @return void
 * @since  1.0.0
 */
function integ_render_a5($result)
{
    $cols = array(
        _('Alloc ID'), _('Customer'), _('Date'), _('Amount'),
        _('From Type'), _('From #'), _('From Missing?'),
        _('To Type'), _('To #'), _('To Missing?'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell((int)$row['id']);
        label_cell(htmlspecialchars($row['customer_name']));
        label_cell($row['date_alloc']);
        label_cell(number_format($row['amt'], 2));
        label_cell(integ_type_name($row['trans_type_from']));
        label_cell(integ_view_link($row['trans_type_from'], $row['trans_no_from'], '#' . $row['trans_no_from']));
        label_cell('<span style="color:' . ($row['from_missing'] === 'YES' ? 'red' : 'green') . '">'
            . htmlspecialchars($row['from_missing']) . '</span>');
        label_cell(integ_type_name($row['trans_type_to']));
        label_cell(integ_view_link($row['trans_type_to'], $row['trans_no_to'], '#' . $row['trans_no_to']));
        label_cell('<span style="color:' . ($row['to_missing'] === 'YES' ? 'red' : 'green') . '">'
            . htmlspecialchars($row['to_missing']) . '</span>');
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}
