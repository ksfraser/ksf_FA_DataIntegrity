<?php
/**
 * integrity_ui.inc &mdash; UI helpers for KSF FA DataIntegrity
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
    if (!user_check_access('SA_DATAINTEGRITY_FIX')) {
        echo "<br>\n";
        echo "<span style='color:#999;font-size:0.9em'>" . _('Fix not available &mdash; contact admin for SA_DATAINTEGRITY_FIX permission.') . "</span>\n";
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
    if (!user_check_access('SA_DATAINTEGRITY_FIX')) {
        return array('check_id' => $_POST['_fix_check'], 'rows_fixed' => -1);
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
        'A6' => 'integ_render_a6',
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
 *   'key'     &mdash; key into $data (from get_purchase/sales_pipeline_counts())
 *   'label'   &mdash; human-readable stage description
 *   'style'   &mdash; 'good' | 'warn' | 'bad' | 'normal' | 'anomalous'
 *   'note'    &mdash; optional short tooltip / parenthetical
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
        'anomalous' => '#800080',// purple &mdash; should never happen
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
            'note'  => _('May include Direct Invoice timeout failures &mdash; check P1 tab'),
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
            'note'  => _('Should be 0 &mdash; investigate immediately'),
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
            'note'  => _('Should be 0 &mdash; investigate immediately'),
        ),
    );
}

// ---------------------------------------------------------------------------
// Per-check render functions &mdash; PURCHASE
// ---------------------------------------------------------------------------

/**
 * Render P1 &mdash; grn_items.quantity_inv drift vs actual supp_invoice_items.
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
 * Render P2 &mdash; purch_order_details.qty_invoiced drift.
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
 * Render P3 &mdash; purch_order_details.quantity_received drift vs GRN items.
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
 * Render P4 &mdash; GRN lines where quantity_inv > qty_recd (over-invoiced).
 * No auto-fix available; view link provided.
 *
 * @param  resource $result  DB result from check_purchase_grn_over_invoiced()
 * @return void
 * @since  1.0.0
 */
function integ_render_p4($result)
{
    global $path_to_root;

    $cols = array(
        _('GRN Batch'), _('Date'), _('Supplier'), _('Item Code'), _('Description'),
        _('Qty Recv\'d'), _('Qty Invoiced'), _('Over By'),
        _('Create Credit Note'),
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

        // Credit-note link: open FA supplier credit with supplier pre-filled
        $cnUrl = $path_to_root . '/purchasing/supplier_credit.php?NewCredit=1'
            . '&supplier_id=' . (int)$row['supplier_id'];
        label_cell('<a href="' . htmlspecialchars($cnUrl) . '">'
            . htmlspecialchars(_('Credit Note')) . '</a>');

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
 * Render P5 &mdash; supp_invoice_items with no matching grn_items row.
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
 * Render P6 &mdash; grn_batch rows whose parent purch_order_no is missing.
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
 * Render P7 &mdash; Supplier invoice headers with no supp_invoice_items lines.
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
// Per-check render functions &mdash; SALES
// ---------------------------------------------------------------------------

/**
 * Render S1 &mdash; sales_order_details.qty_sent drift vs delivery note details.
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
 * Render S2 &mdash; sales_order_details.invoiced drift vs customer invoice details.
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
 * Render S3 &mdash; Customer invoices with no debtor_trans_details lines.
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
 * Render S4 &mdash; Delivery notes whose parent sales order is missing.
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
 * Render S5 &mdash; Delivery notes with no stock_moves entries.
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
// Per-check render functions &mdash; ALLOCATIONS
// ---------------------------------------------------------------------------

/**
 * Render A1 &mdash; supp_trans.alloc drift vs supp_allocations sum.
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
 * Render A2 &mdash; debtor_trans.alloc drift vs cust_allocations sum.
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
 * Render A3 &mdash; Transactions where alloc exceeds the absolute transaction amount.
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
 * Render A4 &mdash; Orphaned supp_allocations rows.
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
 * Return an HTML anchor to FA's supplier allocation page for a given payment.
 *
 * @param  int    $payment_no   Supplier payment transaction number
 * @param  int    $supplier_id  Supplier ID
 * @param  string $label        Link text (default 'Allocate')
 * @return string  HTML anchor
 * @since  1.0.0
 */
function integ_alloc_link($payment_no, $supplier_id, $label = '')
{
    global $path_to_root;

    if ($label === '') {
        $label = _('Allocate');
    }
    $url = $path_to_root . '/purchasing/allocations/supplier_allocate.php?trans_no='
        . (int)$payment_no . '&trans_type=22&supplier_id=' . (int)$supplier_id;
    return '<a href="' . htmlspecialchars($url) . '">' . htmlspecialchars($label) . '</a>';
}

/**
 * Render A5 &mdash; Orphaned cust_allocations rows.
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

/**
 * Render A6 &mdash; Unallocated supplier payments.
 *
 * Each row shows payment details.  When match_count = 1 a "Quick Allocate"
 * submit button is rendered alongside the standard Allocation link.
 *
 * @param  resource $result  DB result from check_alloc_unallocated_supplier_payments()
 * @return void
 * @since  1.0.0
 */
function integ_render_a6($result)
{
    $cols = array(
        _('Payment #'), _('Date'), _('Ref'), _('Supplier'),
        _('Amount'), _('Matching Invoices'),
        _('Quick Allocate'), _('Alloc Screen'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_SUPPAYMENT, $row['payment_no'], '#' . $row['payment_no']));
        label_cell($row['payment_date']);
        label_cell(htmlspecialchars($row['payment_ref']));
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(number_format($row['amount'], 2));

        $mc = (int)$row['match_count'];
        if ($mc === 0) {
            label_cell('<span style="color:#999">' . _('None &mdash; manual allocate required') . '</span>');
        } elseif ($mc === 1) {
            label_cell('<span style="color:#060">' . _('1 invoice matches exactly') . '</span>');
        } else {
            label_cell('<span style="color:#960">' . sprintf(_('%d invoices match'), $mc) . '</span>');
        }

        if ($mc === 1) {
            label_cell(
                '<button type="submit" name="_quick_alloc" value="' . (int)$row['payment_no']
                . '" style="padding:2px 6px">'
                . _('Quick Allocate') . '</button>'
            );
        } else {
            label_cell('<span style="color:#999;font-size:0.9em">' . _('N/A') . '</span>');
        }

        label_cell(integ_alloc_link($row['payment_no'], $row['supplier_id']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

// ===========================================================================
// CONSOLIDATED CHAIN VIEW &mdash; purchase (replaces P5/P6/P7)
// ===========================================================================

/**
 * Render the consolidated purchase chain table.
 *
 * PO | PO&#8596;GRN | GRN | GRN&#8596;Inv | Invoice | Actions
 *
 * @param  array $rows  Output from check_purchase_chain()
 * @return void
 */
function integ_render_purchase_chain($rows)
{
    $cols = array(
        _('Supplier'),
        _('Purchase Order'),
        _('PO&#8596;GRN'),
        _('GRN'),
        _('GRN&#8596;Inv'),
        _('Invoice'),
        _('Actions'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    if (empty($rows)) {
        integ_no_issues_row(count($cols));
        end_table(1);
        return;
    }

    $k = 0;
    foreach ($rows as $row) {
        $key  = htmlspecialchars($row['row_key']);
        $type = $row['row_type'];

        alt_table_row_color($k);

        // ---- Supplier ----
        label_cell(htmlspecialchars($row['supp_name']));

        // ---- PO ----
        $poCell = _chain_cell_po($row);
        label_cell($poCell);

        // ---- PO&#8596;GRN link status ----
        $linkPoGrn = _chain_cell_po_grn_link($row, $key);
        label_cell($linkPoGrn);

        // ---- GRN ----
        $grnCell = _chain_cell_grn($row);
        label_cell($grnCell);

        // ---- GRN&#8596;Inv link status ----
        $linkGrnInv = _chain_cell_grn_inv_link($row, $key);
        label_cell($linkGrnInv);

        // ---- Invoice ----
        $invCell = _chain_cell_invoice($row);
        label_cell($invCell);

        // ---- Actions ----
        $actions = _chain_cell_actions($row, $key);
        label_cell($actions);

        end_row();
    }

    end_table(1);
}

/**
 * Build the PO column content for a purchase chain row.
 */
function _chain_cell_po($row)
{
    if ($row['po_order_no']) {
        $bg = '#dfd'; // green &mdash; exists
        if ($row['row_type'] === 'orphan_inv' && $row['candidate_po_id']) {
            $bg = '#ffd'; // light yellow &mdash; has candidate PO
        }
        $text = integ_view_link(INTEG_ST_PURCHORDER, $row['po_order_no'], '#' . $row['po_order_no']);
        if ($row['po_ref']) {
            $text .= '<br><small>' . htmlspecialchars($row['po_ref']) . '</small>';
        }
        if ($row['po_item_code']) {
            $text .= '<br><small>' . htmlspecialchars($row['po_item_code']) . '</small>';
        }
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }
    return '<span style="color:#999">' . _('&mdash;') . '</span>';
}

/**
 * Build the PO&#8596;GRN link status column.
 */
function _chain_cell_po_grn_link($row, $key)
{
    $code = $row['row_type'];

    // Linked &mdash; green check
    if ($row['po_order_no'] && $row['grn_batch_id']) {
        return '<span style="color:#080;font-size:1.2em">&#10003;</span>';
    }

    if ($code === 'orphan_grn') {
        return '<span style="color:#c00">' . _('Missing PO') . '</span>';
    }

    return '<span style="color:#999">&mdash;</span>';
}

/**
 * Build the GRN column content.
 */
function _chain_cell_grn($row)
{
    $code = $row['row_type'];

    if ($row['grn_batch_id']) {
        $bg = '#dfd';
        if ($code === 'orphan_grn') {
            $bg = '#ffc'; // yellow &mdash; has invoice link but no PO
        } elseif ($code === 'item_mismatch') {
            $bg = '#ffc'; // yellow &mdash; linked to PO but item mismatch with invoice
        }
        $text = integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id'], '#' . $row['grn_batch_id']);
        if ($row['grn_item_code']) {
            $text .= '<br><small>' . htmlspecialchars($row['grn_item_code']) . '</small>';
        }
        if ($row['grn_qty'] > 0) {
            $text .= '<br><small>' . _('Qty:') . ' ' . number_format($row['grn_qty'], 2) . '</small>';
        }
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }

    if ($code === 'orphan_grn') {
        // Standalone orphaned GRN &mdash; show details even without invoice
        $text = integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id'], '#' . $row['grn_batch_id']);
        $text .= '<br><small>' . htmlspecialchars($row['grn_item_code']) . '</small>';
        if ($row['grn_qty'] > 0) {
            $text .= '<br><small>' . _('Qty:') . ' ' . number_format($row['grn_qty'], 2) . '</small>';
        }
        return "<div style='background:#ffc;padding:2px 4px'>$text</div>";
    }

    // No GRN &mdash; orphaned invoice item
    if ($code === 'orphan_inv' && $row['candidate_grn_batch_id']) {
        $bg = '#fdb'; // orange &mdash; candidate found
        $text = sprintf(_('Candidate GRN #%d'), $row['candidate_grn_batch_id']);
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }

    if ($code === 'orphan_inv') {
        return '<span style="color:#c00">' . _('Missing GRN') . '</span>';
    }

    return '<span style="color:#999">&mdash;</span>';
}

/**
 * Build the GRN&#8596;Inv link status column.
 */
function _chain_cell_grn_inv_link($row, $key)
{
    $hasFix = user_check_access('SA_DATAINTEGRITY_FIX');
    $code = $row['row_type'];

    if ($row['inv_trans_no'] && $row['grn_batch_id']
        && $code !== 'item_mismatch' && $code !== 'orphan_inv') {
        return '<span style="color:#080;font-size:1.2em">&#10003;</span>';
    }

    // Item mismatch &mdash; red with Disassociate button
    if ($code === 'item_mismatch') {
        if (!$hasFix) {
            return '<span style="color:#c00;font-size:0.9em">' . _('Mismatch') . '</span>';
        }
        $btn = '<button type="submit" name="_fix_action[' . $key . ']" value="disassociate"'
             . ' style="padding:2px 8px;background:#c00;color:#fff;border:1px solid #800;border-radius:2px;cursor:pointer;font-size:11px"'
             . ' onclick="return confirm(\'' . _('Disassociate this invoice from its GRN? It will be moved to its own orphaned row.') . '\')">'
             . _('Disassociate') . '</button>';
        return $btn;
    }

    // Orphaned invoice with candidate GRN &mdash; orange Match button
    if ($code === 'orphan_inv' && $row['candidate_grn_batch_id']) {
        if (!$hasFix) {
            return '<span style="color:#e80;font-size:0.9em">' . _('Candidate GRN #') . (int)$row['candidate_grn_batch_id'] . '</span>';
        }
        $btn = '<button type="submit" name="_fix_action[' . $key . ']" value="match_grn_inv"'
             . ' style="padding:2px 8px;background:#e80;color:#fff;border:1px solid #c60;border-radius:2px;cursor:pointer;font-size:11px">'
             . _('Match') . '</button>';
        $btn .= '<input type="hidden" name="_cand_grn_batch[' . $key . ']" value="' . (int)$row['candidate_grn_batch_id'] . '">';
        $btn .= '<input type="hidden" name="_cand_grn_item[' . $key . ']" value="' . (int)$row['candidate_grn_item_id'] . '">';
        return $btn;
    }

    // Orphaned invoice with no candidate &mdash; red
    if ($code === 'orphan_inv') {
        return '<span style="color:#c00">' . _('Broken') . '</span>';
    }

    return '<span style="color:#999">&mdash;</span>';
}

/**
 * Build the Invoice column content.
 */
function _chain_cell_invoice($row)
{
    $code = $row['row_type'];

    if ($code === 'ghost_inv') {
        // Ghost invoice &mdash; red background
        $bg = '#fdd';
        $text = integ_view_link(INTEG_ST_SUPPINVOICE, $row['inv_trans_no'], '#' . $row['inv_trans_no']);
        $text .= '<br><small>' . _('GHOST &mdash; no line items') . '</small>';
        if ($row['inv_total'] > 0) {
            $text .= '<br><small>' . _('Total:') . ' ' . number_format($row['inv_total'], 2) . '</small>';
        }
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }

    if ($code === 'item_mismatch') {
        // Item mismatch &mdash; red
        $bg = '#fdd';
        $text = integ_view_link(INTEG_ST_SUPPINVOICE, $row['inv_trans_no'], '#' . $row['inv_trans_no']);
        $text .= '<br><small>' . _('Item:') . ' ' . htmlspecialchars($row['inv_stock_id']) . '</small>';
        $text .= '<br><small style="color:#c00">' . _('MISMATCH: GRN says') . ' ' . htmlspecialchars($row['grn_item_code']) . '</small>';
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }

    if ($code === 'orphan_inv') {
        // Orphaned &mdash; orange if candidate, red if no candidate
        $bg = $row['candidate_grn_batch_id'] ? '#fdb' : '#fdd';
        $text = integ_view_link(INTEG_ST_SUPPINVOICE, $row['inv_trans_no'], '#' . $row['inv_trans_no']);
        if ($row['inv_stock_id']) {
            $text .= '<br><small>' . htmlspecialchars($row['inv_stock_id']) . '</small>';
        }
        $text .= '<br><small>' . _('Qty:') . ' ' . number_format($row['inv_qty'], 2) . '</small>';
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }

    if ($code === 'orphan_grn') {
        // Standalone GRN with no invoice &mdash; blank cell
        return '<span style="color:#999">&mdash;</span>';
    }

    // Healthy invoice
    $text = integ_view_link(INTEG_ST_SUPPINVOICE, $row['inv_trans_no'], '#' . $row['inv_trans_no']);
    if ($row['inv_stock_id']) {
        $text .= '<br><small>' . htmlspecialchars($row['inv_stock_id']) . '</small>';
    }
    return "<div style='background:#dfd;padding:2px 4px'>$text</div>";
}

/**
 * Build the Actions column for a purchase chain row.
 */
function _chain_cell_actions($row, $key)
{
    $hasFix = user_check_access('SA_DATAINTEGRITY_FIX');
    $code = $row['row_type'];
    $parts = array();

    if ($code === 'ghost_inv') {
        if (!$hasFix) {
            $parts[] = '<span style="color:#c00;font-size:0.9em">' . _('Ghost') . '</span>';
        } else {
            $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="void_ghost"'
                     . ' style="padding:2px 8px;background:#c00;color:#fff;border:1px solid #800;border-radius:2px;cursor:pointer;font-size:11px"'
                     . ' onclick="return confirm(\'' . _('Void this ghost invoice? Amounts will be zeroed.') . '\')">'
                     . _('Void') . '</button>';
        }
    }

    if ($code === 'orphan_grn') {
        if ($hasFix) {
            $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="add_po"'
                     . ' style="padding:2px 8px;background:#080;color:#fff;border:1px solid #060;border-radius:2px;cursor:pointer;font-size:11px">'
                     . _('Add PO') . '</button>';
        }
    }

    if ($code === 'orphan_inv') {
        if (!$hasFix) {
            $parts[] = '<span style="color:#999;font-size:0.9em">' . _('Orphaned') . '</span>';
        } else {
            if ($row['candidate_po_id']) {
                $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="add_grn_assoc_po"'
                         . ' style="padding:2px 8px;background:#080;color:#fff;border:1px solid #060;border-radius:2px;cursor:pointer;font-size:11px">'
                         . _('Add GRN') . '</button>';
                $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="add_grn_assoc_po_inv"'
                         . ' style="padding:2px 8px;background:#060;color:#fff;border:1px solid #040;border-radius:2px;cursor:pointer;font-size:11px">'
                         . _('Add GRN + Link') . '</button>';
            } else {
                $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="add_po_grn"'
                         . ' style="padding:2px 8px;background:#080;color:#fff;border:1px solid #060;border-radius:2px;cursor:pointer;font-size:11px">'
                         . _('Add PO + GRN') . '</button>';
            }
        }
    }

    // View invoice popup link (always visible)
    if ($row['inv_trans_no']) {
        $url = _integ_invoice_popup_url($row['inv_trans_no']);
        $parts[] = '<a href="' . htmlspecialchars($url) . '" target="_blank" style="padding:2px 6px">'
                 . _('View Invoice') . '</a>';
    }

    return implode('<br>', $parts);
}

// ===========================================================================
// CONSOLIDATED CHAIN VIEW &mdash; sales (replaces S3/S4/S5)
// ===========================================================================

/**
 * Render the consolidated sales chain table.
 *
 * SO | SO&#8596;Del | Delivery | Stock Moves | Actions
 *
 * @param  array $rows  Output from check_sales_chain()
 * @return void
 */
function integ_render_sales_chain($rows)
{
    $cols = array(
        _('Customer'),
        _('Sales Order'),
        _('SO&#8596;Del'),
        _('Delivery'),
        _('Stock'),
        _('Actions'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    if (empty($rows)) {
        integ_no_issues_row(count($cols));
        end_table(1);
        return;
    }

    $k = 0;
    foreach ($rows as $row) {
        $key  = htmlspecialchars($row['row_key']);
        $type = $row['row_type'];

        alt_table_row_color($k);

        // Customer
        label_cell(htmlspecialchars($row['customer_name']));

        // SO
        $soCell = _sales_cell_so($row);
        label_cell($soCell);

        // SO&#8596;Del link
        $linkCell = _sales_cell_so_del_link($row, $key);
        label_cell($linkCell);

        // Delivery
        $delCell = _sales_cell_delivery($row);
        label_cell($delCell);

        // Stock moves
        $stockCell = _sales_cell_stock($row, $key);
        label_cell($stockCell);

        // Actions
        $actions = _sales_cell_actions($row, $key);
        label_cell($actions);

        end_row();
    }

    end_table(1);
}

function _sales_cell_so($row)
{
    if ($row['so_order_no']) {
        $text = integ_view_link(INTEG_ST_SALESORDER, $row['so_order_no'], '#' . $row['so_order_no']);
        if ($row['so_ref']) {
            $text .= '<br><small>' . htmlspecialchars($row['so_ref']) . '</small>';
        }
        if ($row['so_item_code']) {
            $text .= '<br><small>' . htmlspecialchars($row['so_item_code']) . '</small>';
        }
        return "<div style='background:#dfd;padding:2px 4px'>$text</div>";
    }
    return '<span style="color:#999">&mdash;</span>';
}

function _sales_cell_so_del_link($row, $key)
{
    $type = $row['row_type'];

    if ($row['so_order_no'] && $row['del_trans_no']) {
        return '<span style="color:#080;font-size:1.2em">&#10003;</span>';
    }

    if ($type === 'orphan_del' && $row['candidate_so_id']) {
        $btn = '<button type="submit" name="_fix_action[' . $key . ']" value="match_so_del"'
             . ' style="padding:2px 6px;background:#e80;color:#fff;border:none">'
             . _('Match') . '</button>';
        $btn .= '<input type="hidden" name="_cand_so[' . $key . ']" value="' . (int)$row['candidate_so_id'] . '">';
        return $btn;
    }

    if ($type === 'orphan_del') {
        return '<span style="color:#c00">' . _('Missing SO') . '</span>';
    }

    return '<span style="color:#999">&mdash;</span>';
}

function _sales_cell_delivery($row)
{
    $type = $row['row_type'];

    if ($row['del_trans_no']) {
        $bg = ($type === 'orphan_del' || $type === 'missing_stock') ? '#ffc' : '#dfd';
        $text = integ_view_link(INTEG_ST_CUSTDELIVERY, $row['del_trans_no'], '#' . $row['del_trans_no']);
        if ($row['del_item_code']) {
            $text .= '<br><small>' . htmlspecialchars($row['del_item_code']) . '</small>';
        }
        if ($row['del_qty'] > 0) {
            $text .= '<br><small>' . _('Qty:') . ' ' . number_format($row['del_qty'], 2) . '</small>';
        }
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }

    if ($type === 'ghost_inv') {
        $text = integ_view_link(INTEG_ST_SALESINVOICE, $row['inv_trans_no'], '#' . $row['inv_trans_no']);
        $text .= '<br><small style="color:#c00">' . _('GHOST &mdash; no items') . '</small>';
        return "<div style='background:#fdd;padding:2px 4px'>$text</div>";
    }

    return '<span style="color:#999">&mdash;</span>';
}

function _sales_cell_stock($row, $key)
{
    if ($row['row_type'] === 'missing_stock') {
        return '<span style="color:#c00">' . _('Missing') . '</span>';
    }
    if ($row['del_trans_no'] && $row['row_type'] !== 'missing_stock') {
        return '<span style="color:#080;font-size:1.2em">&#10003;</span>';
    }
    return '<span style="color:#999">&mdash;</span>';
}

function _sales_cell_actions($row, $key)
{
    $type = $row['row_type'];
    $parts = array();

    if ($type === 'ghost_inv') {
        $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="void_ghost"'
                 . ' style="padding:2px 6px;background:#c00;color:#fff;border:none"'
                 . ' onclick="return confirm(\'' . _('Void this ghost invoice? Amounts will be zeroed.') . '\')">'
                 . _('Void') . '</button>';
    }

    if ($type === 'orphan_del') {
        if ($row['candidate_so_id']) {
            $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="match_so_del"'
                     . ' style="padding:2px 6px;background:#e80;color:#fff;border:none">'
                     . _('Match SO') . '</button>';
            $parts[] = '<input type="hidden" name="_cand_so[' . $key . ']" value="' . (int)$row['candidate_so_id'] . '">';
        } else {
            $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="add_so"'
                     . ' style="padding:2px 6px;background:#080;color:#fff;border:none">'
                     . _('Add SO') . '</button>';
        }
    }

    if ($type === 'missing_stock') {
        $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="add_stock_moves"'
                 . ' style="padding:2px 6px;background:#080;color:#fff;border:none">'
                 . _('Create Stock Moves') . '</button>';
    }

    if ($row['del_trans_no']) {
        $url = $GLOBALS['path_to_root'] . '/sales/customer_delivery.php?ModifyDelivery=' . (int)$row['del_trans_no'];
        $parts[] = '<a href="' . htmlspecialchars($url) . '" target="_blank" style="padding:2px 6px">'
                 . _('Edit Delivery') . '</a>';
    }

    return implode('<br>', $parts);
}

// ===========================================================================
// CHAIN POST HANDLER
// ===========================================================================

/**
 * Handle per-row fix action POST for a consolidated chain tab.
 *
 * $_POST['_fix_action'] is an associative array with one element:
 *   [row_key => action_name]
 *
 * Optional companion arrays:
 *   $_POST['_cand_po'][row_key]      = candidate PO order_no
 *   $_POST['_cand_grn_batch'][row_key] = candidate GRN batch id
 *   $_POST['_cand_grn_item'][row_key]  = candidate GRN item id
 *   $_POST['_cand_so'][row_key]       = candidate SO order_no
 *
 * @param  string $chainCheckId  'PCHAIN' or 'SCHAIN'
 * @return array|null  ['message'=>string, 'popup_url'=>string|null] or null
 */
function integ_handle_chain_fix_post($chainCheckId)
{
    if (!isset($_POST['_fix_action']) || !is_array($_POST['_fix_action'])) {
        return null;
    }
    if (!user_check_access('SA_DATAINTEGRITY_FIX')) {
        return array('message' => _('Fix not applied &mdash; missing SA_DATAINTEGRITY_FIX permission.'), 'popup_url' => null);
    }

    $rowKey = key($_POST['_fix_action']);
    $action = current($_POST['_fix_action']);

    // Load companion candidate IDs from POST
    $candPo   = isset($_POST['_cand_po'][$rowKey])        ? (int)$_POST['_cand_po'][$rowKey] : 0;
    $candGrnB = isset($_POST['_cand_grn_batch'][$rowKey]) ? (int)$_POST['_cand_grn_batch'][$rowKey] : 0;
    $candGrnI = isset($_POST['_cand_grn_item'][$rowKey])  ? (int)$_POST['_cand_grn_item'][$rowKey] : 0;
    $candSo   = isset($_POST['_cand_so'][$rowKey])        ? (int)$_POST['_cand_so'][$rowKey] : 0;

    // Set candidate globals for the fix functions to read
    if ($candPo)   $_POST['_cand_po']   = $candPo;
    if ($candGrnB) $_POST['_cand_grn_batch'] = $candGrnB;
    if ($candGrnI) $_POST['_cand_grn_item']  = $candGrnI;
    if ($candSo)   $_POST['_cand_so']   = $candSo;

    if ($chainCheckId === 'PCHAIN') {
        return fix_purchase_chain_row($rowKey, $action);
    }

    if ($chainCheckId === 'SCHAIN') {
        return fix_sales_chain_row($rowKey, $action);
    }

    return array('message' => _('Unknown chain: ') . $chainCheckId, 'popup_url' => null);
}
