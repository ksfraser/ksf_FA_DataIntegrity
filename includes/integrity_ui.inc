<?php
/**
 * integrity_ui.inc &#8212; UI helpers for KSF FA DataIntegrity
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
 * Render a horizontal navigation bar for the Data Integrity module pages.
 * Call once after page() &#8212; replaces reliance on FA's left-side sidebar.
 *
 * @param string $activePage  Identifier of the active page:
 *                            'dashboard', 'purchase', 'sales', 'allocation', 'report'
 * @return void
 * @since  2.5.0
 */
function integ_page_nav($activePage)
{
    $pages = array(
        'dashboard'  => array('label' => _('Dashboard'),       'url' => 'integrity_dashboard.php'),
        'purchase'   => array('label' => _('Purchase Chain'),  'url' => 'purchase_integrity.php'),
        'sales'      => array('label' => _('Sales Chain'),     'url' => 'sales_integrity.php'),
        'allocation' => array('label' => _('Allocations'),     'url' => 'allocation_integrity.php'),
        'report'     => array('label' => _('Full Report'),     'url' => 'integrity_report.php'),
    );

    echo "<div style='margin:6px 0 10px 0;padding:4px 0;border-bottom:1px solid #ccc;'>\n";
    $first = true;
    foreach ($pages as $id => $page) {
        if (!$first) {
            echo "<span style='color:#aaa;padding:0 2px;'>|</span>\n";
        }
        $first = false;
        $isActive = ($id === $activePage);
        if ($isActive) {
            echo "<span style='background:#e0e0e0;font-weight:bold;padding:3px 10px;border:1px solid #aaa;border-radius:3px;'>"
                . $page['label'] . "</span>\n";
        } else {
            echo "<a href='" . $page['url'] . "' style='padding:3px 10px;text-decoration:none;'>"
                . $page['label'] . "</a>\n";
        }
    }
    echo "</div>\n";
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
 * Inspect $_POST for a pending fix action and execute it.
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
        'P8' => 'integ_render_p8',
        'P9' => 'integ_render_p9',
        'P10' => 'integ_render_p10',
        'P11' => 'integ_render_p11',
        'S1' => 'integ_render_s1',
        'S2' => 'integ_render_s2',
        'S3' => 'integ_render_s3',
        'S4' => 'integ_render_s4',
        'S5' => 'integ_render_s5',
        'S6' => 'integ_render_s6',
        'S7' => 'integ_render_s7',
        'S8' => 'integ_render_s8',
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
 *   'key'     &#8212; key into $data (from get_purchase/sales_pipeline_counts())
 *   'label'   &#8212; human-readable stage description
 *   'style'   &#8212; 'good' | 'warn' | 'bad' | 'normal' | 'anomalous'
 *   'note'    &#8212; optional short tooltip / parenthetical
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
        'anomalous' => '#800080',// purple &#8212; should never happen
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
            'label' => _('Complete & clean (PO &#8594; GRN &#8594; Invoice &#8594; Payment, counters agree)'),
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
            'note'  => _('May include Direct Invoice timeout failures &#8212; check P1 tab'),
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
            'note'  => _('Should be 0 &#8212; investigate immediately'),
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
            'label' => _('Complete & clean (SO &#8594; Delivery &#8594; Invoice &#8594; Payment, counters agree)'),
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
            'note'  => _('Should be 0 &#8212; investigate immediately'),
        ),
    );
}

// ---------------------------------------------------------------------------
// Per-check render functions &#8212; PURCHASE
// ---------------------------------------------------------------------------

/**
 * Render P1 &#8212; grn_items.quantity_inv drift vs actual supp_invoice_items.
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
 * Render P2 &#8212; purch_order_details.qty_invoiced drift.
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
 * Render P3 &#8212; purch_order_details.quantity_received drift vs GRN items.
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
 * Render P4 &#8212; GRN lines over-invoiced (net of supplier credit notes).
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
        _('Qty Recv\'d'), _('Invoiced (stored)'), _('Credited'),
        _('Net Invoiced'), _('Over By'),
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
        label_cell(number_format($row['qty_invoiced_stored'], 2));
        label_cell(number_format($row['qty_credited'], 2));
        label_cell(number_format($row['net_invoiced'], 2));
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
 * Render P5 &#8212; supp_invoice_items with no matching grn_items row.
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
 * Render P6 &#8212; grn_batch rows whose parent purch_order_no is missing.
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
 * Render P7 &#8212; Supplier invoice headers with no supp_invoice_items lines.
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

/**
 * Render P8 &#8212; Purchase order lines not yet received.
 *
 * @param  resource $result  DB result from check_purchase_po_not_received()
 * @return void
 * @since  2.5.0
 */
function integ_render_p8($result)
{
    $cols = array(
        _('PO #'), _('Reference'), _('Date'), _('Supplier'),
        _('Item Code'), _('Description'),
        _('Qty Ordered'), _('Qty Received'), _('Outstanding'),
        _('Unit Price'), _('View PO'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $rows = is_array($result) ? $result : array();
    $k = 0;
    foreach ($rows as $row) {
        alt_table_row_color($k);
        label_cell(integ_edit_link(INTEG_ST_PURCHORDER, $row['po_no'], '#' . $row['po_no']));
        label_cell(htmlspecialchars($row['po_ref']));
        label_cell($row['ord_date']);
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(htmlspecialchars($row['item_code']));
        label_cell(htmlspecialchars($row['description']));
        label_cell(number_format($row['quantity_ordered'], 2));
        label_cell(number_format($row['quantity_received'], 2));
        label_cell('<span style="color:#a50">' . number_format($row['qty_outstanding'], 2) . '</span>');
        label_cell(number_format($row['unit_price'], 4));
        label_cell(integ_edit_link(INTEG_ST_PURCHORDER, $row['po_no']));
        end_row();
        $k++;
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render P9 &#8212; GRN items not yet invoiced.
 *
 * @param  resource $result  DB result from check_purchase_grn_not_invoiced()
 * @return void
 * @since  2.5.0
 */
function integ_render_p9($result)
{
    $cols = array(
        _('GRN Batch'), _('Date'), _('Supplier'), _('Item Code'), _('Description'),
        _('Qty Received'), _('Qty Invoiced'), _('Uninvoiced'),
        _('View GRN'), _('View PO'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $rows = is_array($result) ? $result : array();
    $k = 0;
    foreach ($rows as $row) {
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id'], '#' . $row['grn_batch_id']));
        label_cell($row['delivery_date']);
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(htmlspecialchars($row['item_code']));
        label_cell(htmlspecialchars($row['description']));
        label_cell(number_format($row['qty_recd'], 2));
        label_cell(number_format($row['quantity_inv'], 2));
        label_cell('<span style="color:#a50">' . number_format($row['uninvoiced_qty'], 2) . '</span>');
        label_cell(integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id']));
        label_cell(integ_view_link(INTEG_ST_PURCHORDER, $row['purch_order_no']));
        end_row();
        $k++;
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render P10 &#8212; Supplier invoices not yet paid or credited.
 *
 * @param  resource $result  DB result from check_purchase_invoice_unpaid()
 * @return void
 * @since  2.5.0
 */
function integ_render_p10($result)
{
    global $path_to_root;

    $cols = array(
        _('Invoice #'), _('Reference'), _('Supp Reference'), _('Date'), _('Due Date'),
        _('Supplier'),
        _('Amount'), _('GST'), _('Total'), _('Allocated'), _('Unpaid'),
        _('View Invoice'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $rows = is_array($result) ? $result : array();
    $k = 0;
    foreach ($rows as $row) {
        $total  = (float)$row['total'];
        $unpaid = (float)$row['unpaid'];
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_SUPPINVOICE, $row['inv_no'], '#' . $row['inv_no']));
        label_cell(htmlspecialchars($row['inv_ref']));
        label_cell(htmlspecialchars($row['supp_reference']));
        label_cell($row['tran_date']);
        label_cell($row['due_date']);
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(number_format($row['ov_amount'], 2));
        label_cell(number_format($row['ov_gst'], 2));
        label_cell(number_format($total, 2));
        label_cell(number_format($row['alloc'], 2));
        label_cell('<span style="color:' . ($unpaid > 0 ? '#c00' : '#080') . '">'
            . number_format($unpaid, 2) . '</span>');
        label_cell(integ_view_link(INTEG_ST_SUPPINVOICE, $row['inv_no']));
        end_row();
        $k++;
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render P11 &#8212; Supplier invoices with zero-charged GRN items + active GL.
 *
 * Each row is labelled with a pattern:
 *   "Voided"    &#8212; all GRN items have qty=0,price=0 (line-level void)
 *   "Zero-cost" &#8212; all GRN items have qty>0,price=0 (non-stock receipt tracking)
 *   "Mixed"     &#8212; combination of voided and zero-cost lines
 *
 * @param  resource $result  DB result from check_purchase_voided_grn_items()
 * @return void
 * @since  2.5.0
 */
function integ_render_p11($result)
{
    $cols = array(
        _('Invoice #'), _('Supplier'), _('Reference'), _('Date'),
        _('Total'), _('Pattern'),
        _('GRN Lines'), _('Voided'), _('Zero-cost'), _('Active'),
        _('Total Charged'), _('GL Lines'), _('GL Debit Total'),
        _('View Invoice'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $k = 0;
    while ($row = db_fetch($result)) {
        $total_items  = (int)$row['grn_item_count'];
        $voided       = (int)$row['voided_count'];
        $zero_cost    = (int)$row['zero_cost_count'];
        $active       = (int)$row['active_charged_count'];

        if ($voided == $total_items) {
            $pattern = _('Voided');
            $pstyle  = 'color:red';
        } elseif ($zero_cost == $total_items) {
            $pattern = _('Zero-cost');
            $pstyle  = 'color:#a50';
        } else {
            $pattern = _('Mixed');
            $pstyle  = 'color:#800080';
        }

        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_SUPPINVOICE, $row['trans_no'], '#' . $row['trans_no']));
        label_cell(htmlspecialchars($row['supp_name']));
        label_cell(htmlspecialchars($row['reference']));
        label_cell($row['tran_date']);
        label_cell(number_format($row['total_amount'], 2));
        label_cell('<span style="font-weight:bold;' . $pstyle . '">' . $pattern . '</span>');
        label_cell($total_items);
        label_cell('<span style="color:' . ($voided > 0 ? 'red' : '#999') . '">' . $voided . '</span>');
        label_cell('<span style="color:' . ($zero_cost > 0 ? '#a50' : '#999') . '">' . $zero_cost . '</span>');
        label_cell('<span style="color:' . ($active > 0 ? 'green' : '#999') . '">' . $active . '</span>');
        label_cell(number_format($row['total_charged'], 2));
        label_cell((int)$row['gl_line_count']);
        label_cell(number_format($row['gl_debit_total'], 2));
        label_cell(integ_view_link(INTEG_ST_SUPPINVOICE, $row['trans_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

// ---------------------------------------------------------------------------
// Per-check render functions &#8212; SALES
// ---------------------------------------------------------------------------

/**
 * Render S1 &#8212; sales_order_details.qty_sent drift vs delivery note details.
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
        _('Deliveries'), _('Voided Del'), _('Likely Cause'),
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
        label_cell((int)$row['del_count']);
        $vd = (int)$row['voided_del_count'];
        label_cell('<span style="color:' . ($vd > 0 ? 'red' : '#999') . '">' . $vd . '</span>');
        label_cell(htmlspecialchars($row['likely_cause']));
        label_cell(integ_view_link(INTEG_ST_SALESORDER, $row['order_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render S2 &#8212; sales_order_details.invoiced drift vs customer invoice details.
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
        _('Inv Lines'), _('Voided Inv'), _('Credit Notes'), _('Credit Qty'),
        _('Likely Cause'),
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
        label_cell((int)$row['inv_count']);
        $vi = (int)$row['voided_inv_count'];
        label_cell('<span style="color:' . ($vi > 0 ? 'red' : '#999') . '">' . $vi . '</span>');
        label_cell((int)$row['cn_count']);
        label_cell(number_format($row['cn_qty_total'], 2));
        label_cell(htmlspecialchars($row['likely_cause']));
        label_cell(integ_view_link(INTEG_ST_SALESORDER, $row['order_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render S3 &#8212; Customer invoices with no debtor_trans_details lines.
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
 * Render S4 &#8212; Delivery notes whose parent sales order is missing.
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
 * Render S5 &#8212; Delivery notes with no stock_moves entries.
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

/**
 * Render S6 &#8212; Sales orders not yet fully delivered.
 *
 * @param  resource $result  DB result from check_sales_so_not_delivered()
 * @return void
 * @since  2.5.0
 */
function integ_render_s6($result)
{
    $cols = array(
        _('Order #'), _('Reference'), _('Date'), _('Customer'),
        _('Item Code'), _('Description'),
        _('Qty Ordered'), _('Qty Sent'), _('Outstanding'),
        _('Unit Price'),
        _('Direct Inv Matches'),
        _('Void Inv'),
        _('View Order'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $rows = is_array($result) ? $result : array();
    $k = 0;
    foreach ($rows as $row) {
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_SALESORDER, $row['order_no'], '#' . $row['order_no']));
        label_cell(htmlspecialchars($row['so_ref']));
        label_cell($row['ord_date']);
        label_cell(htmlspecialchars($row['customer_name']));
        label_cell(htmlspecialchars($row['stk_code']));
        label_cell(htmlspecialchars($row['description']));
        label_cell(number_format($row['qty_ordered'], 2));
        label_cell(number_format($row['qty_sent'], 2));
        label_cell('<span style="color:#a50">' . number_format($row['qty_outstanding'], 2) . '</span>');
        label_cell(number_format($row['unit_price'], 4));

        $mc = (int)$row['match_count'];
        if ($mc > 0) {
            label_cell('<span style="color:#a50;font-size:0.9em">' . $row['possible_matches'] . '</span>');
            $firstInv = (int)$row['first_match_inv_no'];
            if ($firstInv) {
                $voidUrl = $GLOBALS['path_to_root']
                    . '/gl/void_transaction.php?type=10&trans_no=' . $firstInv;
                label_cell('<a href="' . htmlspecialchars($voidUrl) . '" target="_blank" style="color:red;font-weight:bold">'
                    . _('Void') . '</a>');
            } else {
                label_cell('');
            }
        } else {
            label_cell('<span style="color:#999">' . _('None') . '</span>');
            label_cell('');
        }

        label_cell(integ_view_link(INTEG_ST_SALESORDER, $row['order_no']));
        end_row();
        $k++;
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render S7 &#8212; Delivery-note items not yet fully invoiced.
 *
 * @param  resource $result  DB result from check_sales_delivery_not_invoiced()
 * @return void
 * @since  2.5.0
 */
function integ_render_s7($result)
{
    $cols = array(
        _('Delivery #'), _('Delivery Ref'), _('Date'), _('Customer'),
        _('SO Ref'), _('Item Code'), _('Description'),
        _('Qty Delivered'), _('Qty Invoiced'), _('Uninvoiced'),
        _('View Delivery'), _('View SO'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $rows = is_array($result) ? $result : array();
    $k = 0;
    foreach ($rows as $row) {
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_CUSTDELIVERY, $row['delivery_no'], '#' . $row['delivery_no']));
        label_cell(htmlspecialchars($row['delivery_ref']));
        label_cell($row['delivery_date']);
        label_cell(htmlspecialchars($row['customer_name']));
        label_cell(htmlspecialchars($row['so_ref']));
        label_cell(htmlspecialchars($row['stock_id']));
        label_cell(htmlspecialchars($row['description']));
        label_cell(number_format($row['qty_delivered'], 2));
        label_cell(number_format($row['qty_invoiced'], 2));
        label_cell('<span style="color:#a50">' . number_format($row['uninvoiced_qty'], 2) . '</span>');
        label_cell(integ_view_link(INTEG_ST_CUSTDELIVERY, $row['delivery_no']));
        label_cell(integ_view_link(INTEG_ST_SALESORDER, $row['sales_order_no']));
        end_row();
        $k++;
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render S8 &#8212; Customer invoices not yet paid or credited.
 *
 * @param  resource $result  DB result from check_sales_invoice_unpaid()
 * @return void
 * @since  2.5.0
 */
function integ_render_s8($result)
{
    $cols = array(
        _('Invoice #'), _('Reference'), _('Date'), _('Due Date'),
        _('Customer'),
        _('Amount'), _('GST'), _('Freight'), _('Total'), _('Allocated'), _('Unpaid'),
        _('View Invoice'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    $rows = is_array($result) ? $result : array();
    $k = 0;
    foreach ($rows as $row) {
        $total  = (float)$row['total'];
        $unpaid = (float)$row['unpaid'];
        alt_table_row_color($k);
        label_cell(integ_view_link(INTEG_ST_SALESINVOICE, $row['inv_no'], '#' . $row['inv_no']));
        label_cell(htmlspecialchars($row['reference']));
        label_cell($row['tran_date']);
        label_cell($row['due_date']);
        label_cell(htmlspecialchars($row['customer_name']));
        label_cell(number_format($row['ov_amount'], 2));
        label_cell(number_format($row['ov_gst'], 2));
        label_cell(number_format($row['ov_freight'], 2));
        label_cell(number_format($total, 2));
        label_cell(number_format($row['alloc'], 2));
        label_cell('<span style="color:' . ($unpaid > 0 ? '#c00' : '#080') . '">'
            . number_format($unpaid, 2) . '</span>');
        label_cell(integ_view_link(INTEG_ST_SALESINVOICE, $row['inv_no']));
        end_row();
        $k++;
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

// ---------------------------------------------------------------------------
// Per-check render functions &#8212; ALLOCATIONS
// ---------------------------------------------------------------------------

/**
 * Render A1 &#8212; supp_trans.alloc drift vs supp_allocations sum.
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
        _('Likely Cause'),
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
        label_cell(htmlspecialchars($row['likely_cause']));
        label_cell(integ_view_link($row['type'], $row['trans_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render A2 &#8212; debtor_trans.alloc drift vs cust_allocations sum.
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
        _('Likely Cause'),
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
        label_cell(htmlspecialchars($row['likely_cause']));
        label_cell(integ_view_link($row['type'], $row['trans_no']));
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render A3 &#8212; Transactions where alloc exceeds the absolute transaction amount.
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
 * Render A4 &#8212; Orphaned supp_allocations rows.
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
        $from_missing = $row['from_missing'] === 'YES';
        if ($from_missing) {
            label_cell('<span style="color:red">#' . (int)$row['trans_no_from'] . ' (' . _('missing') . ')</span>');
        } else {
            label_cell(integ_view_link($row['trans_type_from'], $row['trans_no_from'], '#' . $row['trans_no_from']));
        }
        label_cell('<span style="color:' . ($from_missing ? 'red' : 'green') . '">'
            . htmlspecialchars($row['from_missing']) . '</span>');
        label_cell(integ_type_name($row['trans_type_to']));
        $to_missing = $row['to_missing'] === 'YES';
        if ($to_missing) {
            label_cell('<span style="color:red">#' . (int)$row['trans_no_to'] . ' (' . _('missing') . ')</span>');
        } else {
            label_cell(integ_view_link($row['trans_type_to'], $row['trans_no_to'], '#' . $row['trans_no_to']));
        }
        label_cell('<span style="color:' . ($to_missing ? 'red' : 'green') . '">'
            . htmlspecialchars($row['to_missing']) . '</span>');
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render A5 &#8212; Orphaned cust_allocations rows.
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
        $from_missing = $row['from_missing'] === 'YES';
        if ($from_missing) {
            label_cell('<span style="color:red">#' . (int)$row['trans_no_from'] . ' (' . _('missing') . ')</span>');
        } else {
            label_cell(integ_view_link($row['trans_type_from'], $row['trans_no_from'], '#' . $row['trans_no_from']));
        }
        label_cell('<span style="color:' . ($from_missing ? 'red' : 'green') . '">'
            . htmlspecialchars($row['from_missing']) . '</span>');
        label_cell(integ_type_name($row['trans_type_to']));
        $to_missing = $row['to_missing'] === 'YES';
        if ($to_missing) {
            label_cell('<span style="color:red">#' . (int)$row['trans_no_to'] . ' (' . _('missing') . ')</span>');
        } else {
            label_cell(integ_view_link($row['trans_type_to'], $row['trans_no_to'], '#' . $row['trans_no_to']));
        }
        label_cell('<span style="color:' . ($to_missing ? 'red' : 'green') . '">'
            . htmlspecialchars($row['to_missing']) . '</span>');
        end_row();
    }

    if ($k === 0) {
        integ_no_issues_row(count($cols));
    }
    end_table(1);
}

/**
 * Render A6 &#8212; Unallocated supplier payments.
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
            label_cell('<span style="color:#999">' . _('None &#8212; manual allocate required') . '</span>');
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
// CONSOLIDATED CHAIN VIEW &#8212; purchase (replaces P5/P6/P7)
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
        $bg = '#dfd'; // green &#8212; exists
        if ($row['row_type'] === 'orphan_inv' && $row['candidate_po_id']) {
            $bg = '#ffd'; // light yellow &#8212; has candidate PO
        }
        $text = integ_edit_link(INTEG_ST_PURCHORDER, $row['po_order_no'], sprintf(_('Edit PO #%d'), $row['po_order_no']));
        if ($row['po_ref']) {
            $text .= '<br><small>' . htmlspecialchars($row['po_ref']) . '</small>';
        }
        if ($row['po_item_code']) {
            $text .= '<br><small>' . htmlspecialchars($row['po_item_code']) . '</small>';
        }
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }
    return '<span style="color:#999">' . _('&#8212;') . '</span>';
}

/**
 * Build the PO&#8596;GRN link status column.
 */
function _chain_cell_po_grn_link($row, $key)
{
    $code = $row['row_type'];

    // Linked &#8212; green check
    if ($row['po_order_no'] && $row['grn_batch_id']) {
        return '<span style="color:#080;font-size:1.2em">&#10003;</span>';
    }

    if ($code === 'orphan_grn') {
        return '<span style="color:#c00">' . _('Missing PO') . '</span>';
    }

    return '<span style="color:#999">&#8212;</span>';
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
            $bg = '#ffc'; // yellow &#8212; has invoice link but no PO
        } elseif ($code === 'item_mismatch') {
            $bg = '#ffc'; // yellow &#8212; linked to PO but item mismatch with invoice
        }
        $text = integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id'], sprintf(_('View GRN #%d'), $row['grn_batch_id']));
        if ($row['grn_item_code']) {
            $text .= '<br><small>' . htmlspecialchars($row['grn_item_code']) . '</small>';
        }
        if ($row['grn_qty'] > 0) {
            $text .= '<br><small>' . _('Qty:') . ' ' . number_format($row['grn_qty'], 2) . '</small>';
        }
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }

    if ($code === 'orphan_grn') {
        // Standalone orphaned GRN &#8212; show details even without invoice
        $text = integ_view_link(INTEG_ST_SUPPRECEIVE, $row['grn_batch_id'], sprintf(_('View GRN #%d'), $row['grn_batch_id']));
        $text .= '<br><small>' . htmlspecialchars($row['grn_item_code']) . '</small>';
        if ($row['grn_qty'] > 0) {
            $text .= '<br><small>' . _('Qty:') . ' ' . number_format($row['grn_qty'], 2) . '</small>';
        }
        return "<div style='background:#ffc;padding:2px 4px'>$text</div>";
    }

    // No GRN &#8212; orphaned invoice item
    if ($code === 'orphan_inv' && $row['candidate_grn_batch_id']) {
        $bg = '#fdb'; // orange &#8212; candidate found
        $text = sprintf(_('Candidate GRN #%d'), $row['candidate_grn_batch_id']);
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }

    if ($code === 'orphan_inv') {
        return '<span style="color:#c00">' . _('Missing GRN') . '</span>';
    }

    return '<span style="color:#999">&#8212;</span>';
}

/**
 * Build the GRN&#8596;Inv link status column.
 */
function _chain_cell_grn_inv_link($row, $key)
{
    $code = $row['row_type'];

    if ($row['inv_trans_no'] && $row['grn_batch_id']
        && $code !== 'item_mismatch' && $code !== 'orphan_inv') {
        return '<span style="color:#080;font-size:1.2em">&#10003;</span>';
    }

    // Item mismatch &#8212; red with Disassociate button
    if ($code === 'item_mismatch') {
        $btn = '<button type="submit" name="_fix_action[' . $key . ']" value="disassociate"'
             . ' style="padding:2px 8px;background:#c00;color:#fff;border:1px solid #800;border-radius:2px;cursor:pointer;font-size:11px"'
             . ' onclick="return confirm(\'' . _('Disassociate this invoice from its GRN? It will be moved to its own orphaned row.') . '\')">'
             . _('Disassociate') . '</button>';
        return $btn;
    }

    // Orphaned invoice with candidate GRN &#8212; orange Match button
    if ($code === 'orphan_inv' && $row['candidate_grn_batch_id']) {
        $btn = '<button type="submit" name="_fix_action[' . $key . ']" value="match_grn_inv"'
             . ' style="padding:2px 8px;background:#e80;color:#fff;border:1px solid #c60;border-radius:2px;cursor:pointer;font-size:11px">'
             . _('Match') . '</button>';
        $btn .= '<input type="hidden" name="_cand_grn_batch[' . $key . ']" value="' . (int)$row['candidate_grn_batch_id'] . '">';
        $btn .= '<input type="hidden" name="_cand_grn_item[' . $key . ']" value="' . (int)$row['candidate_grn_item_id'] . '">';
        return $btn;
    }

    // Orphaned invoice with no candidate &#8212; red
    if ($code === 'orphan_inv') {
        return '<span style="color:#c00">' . _('Broken') . '</span>';
    }

    // Standalone GRN (valid PO, no invoice) &#8212; Create Invoice link
    if ($code === 'standalone_grn') {
        global $path_to_root;
        $url = $path_to_root . '/purchasing/supplier_invoice.php?New=1';
        return '<a href="' . htmlspecialchars($url) . '" target="_blank" style="padding:2px 8px;background:#080;color:#fff;border:1px solid #060;border-radius:2px;cursor:pointer;font-size:11px;text-decoration:none">'
             . _('Create Invoice') . '</a>';
    }

    return '<span style="color:#999">&#8212;</span>';
}

/**
 * Build the Invoice column content.
 */
function _chain_cell_invoice($row)
{
    $code = $row['row_type'];

    if ($code === 'ghost_inv') {
        // Ghost invoice &#8212; red background
        $bg = '#fdd';
        $text = integ_edit_link(INTEG_ST_SUPPINVOICE, $row['inv_trans_no'], sprintf(_('Edit Invoice #%d'), $row['inv_trans_no']));
        $text .= '<br><small>' . _('GHOST &#8212; no line items') . '</small>';
        if ($row['inv_total'] > 0) {
            $text .= '<br><small>' . _('Total:') . ' ' . number_format($row['inv_total'], 2) . '</small>';
        }
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }

    if ($code === 'item_mismatch') {
        // Item mismatch &#8212; red
        $bg = '#fdd';
        $text = integ_edit_link(INTEG_ST_SUPPINVOICE, $row['inv_trans_no'], sprintf(_('Edit Invoice #%d'), $row['inv_trans_no']));
        $text .= '<br><small>' . _('Item:') . ' ' . htmlspecialchars($row['inv_stock_id']) . '</small>';
        $text .= '<br><small style="color:#c00">' . _('MISMATCH: GRN says') . ' ' . htmlspecialchars($row['grn_item_code']) . '</small>';
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }

    if ($code === 'orphan_inv') {
        // Orphaned &#8212; orange if candidate, red if no candidate
        $bg = $row['candidate_grn_batch_id'] ? '#fdb' : '#fdd';
        $text = integ_edit_link(INTEG_ST_SUPPINVOICE, $row['inv_trans_no'], sprintf(_('Edit Invoice #%d'), $row['inv_trans_no']));
        if ($row['inv_stock_id']) {
            $text .= '<br><small>' . htmlspecialchars($row['inv_stock_id']) . '</small>';
        }
        $text .= '<br><small>' . _('Qty:') . ' ' . number_format($row['inv_qty'], 2) . '</small>';
        return "<div style='background:$bg;padding:2px 4px'>$text</div>";
    }

    if ($code === 'orphan_grn') {
        // Standalone GRN with no invoice &#8212; blank cell
        return '<span style="color:#999">&#8212;</span>';
    }

    if ($code === 'standalone_grn') {
        // Valid PO+GRN with no invoice &#8212; blank cell
        return '<span style="color:#999">' . _('&#8212;') . '</span>';
    }

    // Healthy invoice
    $text = integ_edit_link(INTEG_ST_SUPPINVOICE, $row['inv_trans_no'], sprintf(_('Edit Invoice #%d'), $row['inv_trans_no']));
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
    $code = $row['row_type'];
    $parts = array();

    if ($code === 'ghost_inv') {
        $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="void_ghost"'
                 . ' style="padding:2px 8px;background:#c00;color:#fff;border:1px solid #800;border-radius:2px;cursor:pointer;font-size:11px"'
                 . ' onclick="return confirm(\'' . _('Void this ghost invoice? Amounts will be zeroed.') . '\')">'
                 . _('Void') . '</button>';
    }

    if ($code === 'orphan_grn') {
        $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="add_po"'
                 . ' style="padding:2px 8px;background:#080;color:#fff;border:1px solid #060;border-radius:2px;cursor:pointer;font-size:11px">'
                 . _('Add PO') . '</button>';
    }

    if ($code === 'orphan_inv') {
        if ($row['candidate_po_id']) {
            $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="add_grn_assoc_po"'
                     . ' style="padding:2px 8px;background:#080;color:#fff;border:1px solid #060;border-radius:2px;cursor:pointer;font-size:11px">'
                     . _('Add GRN (Assoc PO)') . '</button>';
            $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="add_grn_assoc_po_inv"'
                     . ' style="padding:2px 8px;background:#060;color:#fff;border:1px solid #040;border-radius:2px;cursor:pointer;font-size:11px">'
                     . _('Add GRN (Assoc PO/Inv)') . '</button>';
        } else {
            $parts[] = '<button type="submit" name="_fix_action[' . $key . ']" value="add_po_grn"'
                     . ' style="padding:2px 8px;background:#080;color:#fff;border:1px solid #060;border-radius:2px;cursor:pointer;font-size:11px">'
                     . _('Add PO + GRN') . '</button>';
        }
    }

    return implode('<br>', $parts);
}

// ===========================================================================
// CONSOLIDATED CHAIN VIEW &#8212; sales (replaces S3/S4/S5)
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
        $text = integ_edit_link(INTEG_ST_SALESORDER, $row['so_order_no'], sprintf(_('Edit SO #%d'), $row['so_order_no']));
        if ($row['so_ref']) {
            $text .= '<br><small>' . htmlspecialchars($row['so_ref']) . '</small>';
        }
        if ($row['so_item_code']) {
            $text .= '<br><small>' . htmlspecialchars($row['so_item_code']) . '</small>';
        }
        return "<div style='background:#dfd;padding:2px 4px'>$text</div>";
    }
    return '<span style="color:#999">&#8212;</span>';
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

    return '<span style="color:#999">&#8212;</span>';
}

function _sales_cell_delivery($row)
{
    $type = $row['row_type'];

    if ($row['del_trans_no']) {
        $bg = ($type === 'orphan_del' || $type === 'missing_stock') ? '#ffc' : '#dfd';
        $text = integ_edit_link(INTEG_ST_CUSTDELIVERY, $row['del_trans_no'], sprintf(_('Edit Delivery #%d'), $row['del_trans_no']));
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
        $text .= '<br><small style="color:#c00">' . _('GHOST &#8212; no items') . '</small>';
        return "<div style='background:#fdd;padding:2px 4px'>$text</div>";
    }

    return '<span style="color:#999">&#8212;</span>';
}

function _sales_cell_stock($row, $key)
{
    if ($row['row_type'] === 'missing_stock') {
        return '<span style="color:#c00">' . _('Missing') . '</span>';
    }
    if ($row['del_trans_no'] && $row['row_type'] !== 'missing_stock') {
        return '<span style="color:#080;font-size:1.2em">&#10003;</span>';
    }
    return '<span style="color:#999">&#8212;</span>';
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

// ---------------------------------------------------------------------------

/**
 * Render the SCUST customer/branch consistency check results.
 *
 * Shows each inconsistent transaction with three fix buttons:
 *   "Use SO" — update debtor/branch from linked SO
 *   "Use Branch" — update debtor from branch owner
 *   "Set Customer" — inline dropdown to pick customer/branch
 *
 * @param array $rows  Result from check_sales_customer_mismatch()
 */
function integ_render_sales_customer_mismatch($rows)
{
    $cols = array(
        _('Trans'),
        _('Date'),
        _('Charge To'),
        _('Branch'),
        _('SO'),
        _('Issue'),
        _('Actions'),
    );
    start_table(TABLESTYLE, "width='98%'");
    table_header($cols);

    if (empty($rows)) {
        integ_no_issues_row(count($cols));
        end_table(1);
        return;
    }

    // ---- Manual trans lookup form ----
    echo "<div style='margin:8px 0;padding:6px;background:#f0f0f0;border:1px solid #ccc'>\n";
    echo "<strong>" . _('Manual lookup:') . "</strong><br>\n";
    echo _('Trans #:') . " <input type='text' name='_scust_lookup_no' size='8' value='"
        . (isset($_POST['_scust_lookup_no']) ? htmlspecialchars($_POST['_scust_lookup_no']) : '') . "'>\n";
    echo " <select name='_scust_lookup_type'>\n";
    echo "  <option value='13'" . (isset($_POST['_scust_lookup_type']) && $_POST['_scust_lookup_type'] == '13' ? ' selected' : '') . ">" . _('Delivery') . "</option>\n";
    echo "  <option value='10'" . (isset($_POST['_scust_lookup_type']) && $_POST['_scust_lookup_type'] == '10' ? ' selected' : '') . ">" . _('Invoice') . "</option>\n";
    echo " </select>\n";
    echo " <button type='submit' name='_scust_lookup' value='1' style='padding:2px 6px'>" . _('Load') . "</button>\n";
    echo "</div>\n";

    // ---- Show looked-up trans for manual fix ----
    if (isset($_POST['_scust_lookup']) && $_POST['_scust_lookup'] && !empty($_POST['_scust_lookup_no'])) {
        $lookupNo = (int)$_POST['_scust_lookup_no'];
        $lookupType = (int)$_POST['_scust_lookup_type'];
        $lr = db_query("SELECT dt.trans_no, dt.type, dt.tran_date, dt.reference,
                               dt.debtor_no, charge_to.name AS charge_to_name,
                               dt.branch_code, cb.br_name AS branch_name,
                               dt.order_ AS so_order_no
                        FROM " . TB_PREF . "debtor_trans dt
                        JOIN " . TB_PREF . "debtors_master charge_to ON charge_to.debtor_no = dt.debtor_no
                        LEFT JOIN " . TB_PREF . "cust_branch cb ON cb.branch_code = dt.branch_code
                        WHERE dt.trans_no = $lookupNo AND dt.type = $lookupType", 'SCUST lookup');
        if ($lr && ($lrRow = db_fetch($lr))) {
            $lkKey = 'scust_' . $lrRow['type'] . '_' . $lrRow['trans_no'];
            $isEditing = (isset($_POST['_scust_edit_key']) && $_POST['_scust_edit_key'] === $lkKey);
            echo "<table class='tablestyle' style='width:98%'>\n";
            echo "<tr><th>" . _('Trans') . "</th><th>" . _('Date') . "</th><th>" . _('Charge To') . "</th><th>" . _('Branch') . "</th><th>" . _('Actions') . "</th></tr>\n";
            $typeLabel = ($lrRow['type'] == 13) ? 'DN' : (($lrRow['type'] == 10) ? 'INV' : '#' . $lrRow['type']);
            $transText = $typeLabel . ' #' . $lrRow['trans_no'];
            if ($lrRow['reference']) {
                $transText .= '<br><small>' . htmlspecialchars($lrRow['reference']) . '</small>';
            }
            echo "<tr class='even'><td>$transText</td><td>" . $lrRow['tran_date'] . "</td>";
            echo "<td>" . htmlspecialchars($lrRow['charge_to_name']) . " (#" . (int)$lrRow['debtor_no'] . ")</td>";
            echo "<td>" . htmlspecialchars($lrRow['branch_name'] ?: '#' . $lrRow['branch_code']) . "</td>";
            echo "<td>";
            if ($isEditing) {
                $custOptions = _build_customer_options($lrRow['debtor_no']);
                $branchOptions = _build_branch_options($lrRow['debtor_no']);
                echo "<div style='border:1px solid #e80;background:#fff8e0;padding:4px'>";
                echo "<label>" . _('Customer:') . "<br><select name='_scust_new_debtor' style='width:180px'>$custOptions</select></label>";
                echo "<br><label>" . _('Branch:') . "<br><select name='_scust_new_branch' style='width:80px'>$branchOptions</select></label>";
                echo "<br><button type='submit' name='_fix_action[$lkKey]' value='apply_cust'"
                    . " style='padding:2px 6px;background:#080;color:#fff;border:none;margin-top:3px'>" . _('Apply') . "</button>";
                echo " <button type='submit' name='_scust_cancel' value='1' style='padding:2px 6px'>" . _('Cancel') . "</button>";
                echo "</div>";
            } else {
                echo "<button type='submit' name='_fix_action[$lkKey]' value='set_cust'"
                    . " style='padding:2px 6px;background:#080;color:#fff;border:none'>" . _('Set Customer') . "</button>";
            }
            echo "</td></tr></table>\n";
        } else {
            echo "<p style='color:#c00'>" . _('Transaction not found.') . "</p>\n";
        }
    }

    $editKey = isset($_POST['_scust_edit_key']) ? $_POST['_scust_edit_key'] : '';
    $k = 0;

    foreach ($rows as $r) {
        $typeLabel = ($r['type'] == 13) ? 'DN' : (($r['type'] == 10) ? 'INV' : '#' . $r['type']);
        $rowKey    = 'scust_' . $r['type'] . '_' . $r['trans_no'];
        $isEditing = ($editKey === $rowKey);

        alt_table_row_color($k);

        // Trans
        $transText = $typeLabel . ' #' . $r['trans_no'];
        if ($r['reference']) {
            $transText .= '<br><small>' . htmlspecialchars($r['reference']) . '</small>';
        }
        label_cell($transText);

        // Date
        label_cell($r['tran_date']);

        // Charge To (current)
        $chargeText = htmlspecialchars($r['charge_to_name']);
        if ((int)$r['charge_to_no'] != (int)$r['branch_owner_no']
            && $r['branch_owner_no'] !== null) {
            $chargeText .= '<br><small style="color:#c00">' . _('Should be: ')
                . htmlspecialchars($r['branch_owner_name']) . '</small>';
        }
        label_cell($chargeText);

        // Branch
        $branchText = htmlspecialchars($r['branch_name'] ?: '#' . $r['branch_code']);
        if ($r['so_branch_code'] !== null && (int)$r['branch_code'] != (int)$r['so_branch_code']) {
            $branchText .= '<br><small style="color:#c00">' . _('SO branch: ')
                . (int)$r['so_branch_code'] . '</small>';
        }
        label_cell($branchText);

        // SO
        if ($r['so_order_no']) {
            $soText = '#' . $r['so_order_no'];
            if ($r['so_ref']) {
                $soText .= '<br><small>' . htmlspecialchars($r['so_ref']) . '</small>';
            }
            if ($r['so_customer_name']) {
                $soText .= '<br><small>' . htmlspecialchars($r['so_customer_name']) . '</small>';
            }
        } else {
            $soText = '<span style="color:#999">&#8212;</span>';
        }
        label_cell($soText);

        // Issue
        $issueMap = array(
            'branch_owner_mismatch'  => _('Branch belongs to different customer'),
            'so_customer_mismatch'   => _('SO has different customer'),
            'so_branch_mismatch'     => _('SO has different branch'),
            'unknown'                => _('Unknown'),
        );
        $issueText = isset($issueMap[$r['issue_type']]) ? $issueMap[$r['issue_type']] : $r['issue_type'];
        label_cell('<span style="color:#c00">' . $issueText . '</span>');

        // Actions
        if ($isEditing) {
            // Show inline customer/branch selector
            $custOptions = _build_customer_options($r['charge_to_no']);
            $branchOptions = _build_branch_options($r['charge_to_no']);

            $actionHtml = '<div style="border:1px solid #e80;background:#fff8e0;padding:4px;margin:2px">';
            $actionHtml .= '<label>' . _('Customer:') . '<br>';
            $actionHtml .= '<select name="_scust_new_debtor" style="width:180px">' . $custOptions . '</select></label>';
            $actionHtml .= '<br><label>' . _('Branch:') . '<br>';
            $actionHtml .= '<select name="_scust_new_branch" style="width:80px">' . $branchOptions . '</select></label>';
            $actionHtml .= '<br>';
            $actionHtml .= '<button type="submit" name="_fix_action[' . $rowKey . ']" value="apply_cust"'
                       . ' style="padding:2px 6px;background:#080;color:#fff;border:none;margin-top:3px">'
                       . _('Apply') . '</button>';
            $actionHtml .= ' <button type="submit" name="_scust_cancel" value="1"'
                       . ' style="padding:2px 6px">' . _('Cancel') . '</button>';
            $actionHtml .= '</div>';
            label_cell($actionHtml);
        } else {
            $parts = array();

            // Use SO customer button
            if ($r['so_order_no'] && $r['so_debtor_no']
                && (int)$r['so_debtor_no'] != (int)$r['charge_to_no']) {
                $parts[] = '<button type="submit" name="_fix_action[' . $rowKey . ']" value="use_so_cust"'
                         . ' style="padding:2px 6px;background:#e80;color:#fff;border:none">'
                         . _('Use SO') . '</button>';
            }

            // Use branch customer button
            if ($r['branch_owner_no'] !== null
                && (int)$r['branch_owner_no'] != (int)$r['charge_to_no']) {
                $parts[] = '<button type="submit" name="_fix_action[' . $rowKey . ']" value="use_branch_cust"'
                         . ' style="padding:2px 6px;background:#e80;color:#fff;border:none">'
                         . _('Use Branch') . '</button>';
            }

            // Set Customer button — triggers inline edit on reload
            $parts[] = '<button type="submit" name="_fix_action[' . $rowKey . ']" value="set_cust"'
                     . ' style="padding:2px 6px;background:#080;color:#fff;border:none">'
                     . _('Set Customer') . '</button>';

            label_cell(implode('<br>', $parts));
        }

        end_row();
    }

    end_table(1);
}

/**
 * Build <option> HTML for the customer dropdown.
 */
function _build_customer_options($selectedNo)
{
    $sql = "SELECT debtor_no, name FROM " . TB_PREF . "debtors_master ORDER BY name";
    $r = db_query($sql, 'Load customers for SCUST dropdown');
    $out = '';
    if ($r) {
        while ($row = db_fetch($r)) {
            $sel = ((int)$row['debtor_no'] === (int)$selectedNo) ? ' selected' : '';
            $out .= '<option value="' . (int)$row['debtor_no'] . '"' . $sel . '>'
                  . htmlspecialchars($row['name']) . ' (#' . (int)$row['debtor_no'] . ')</option>';
        }
    }
    return $out;
}

/**
 * Build <option> HTML for branch dropdown.
 */
function _build_branch_options($debtorNo)
{
    $where = $debtorNo ? " WHERE debtor_no = " . (int)$debtorNo : '';
    $sql = "SELECT branch_code, br_name AS branch_name, debtor_no FROM " . TB_PREF . "cust_branch$where ORDER BY branch_code";
    $r = db_query($sql, 'Load branches for SCUST dropdown');
    $out = '';
    if ($r) {
        while ($row = db_fetch($r)) {
            $sel = ((int)$row['branch_code'] === (int)$debtorNo) ? ' selected' : '';
            $out .= '<option value="' . (int)$row['branch_code'] . '"' . $sel . '>'
                  . (int)$row['branch_code'] . ' - ' . htmlspecialchars($row['branch_name']) . '</option>';
        }
    }
    return $out;
}

/**
 * Handle per-row fix action POST for SCUST.
 *
 * @return array|null ['message'=>string, 'popup_url'=>string|null] or null
 */
function integ_handle_scust_fix_post()
{
    // Cancel inline edit
    if (isset($_POST['_scust_cancel'])) {
        unset($_POST['_scust_edit_key']);
        return null;
    }

    if (!isset($_POST['_fix_action']) || !is_array($_POST['_fix_action'])) {
        return null;
    }

    $rowKey = key($_POST['_fix_action']);
    $action = current($_POST['_fix_action']);

    if ($action === 'set_cust') {
        $_POST['_scust_edit_key'] = $rowKey;
        return null;
    }

    if ($action === 'apply_cust') {
        $result = fix_sales_customer_row($rowKey, $action);
        unset($_POST['_scust_edit_key']);
        return $result;
    }

    if (in_array($action, array('use_so_cust', 'use_branch_cust'), true)) {
        return fix_sales_customer_row($rowKey, $action);
    }

    return null;
}
